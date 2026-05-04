/*
 * JACL Type Inference Pass — Phase 3 foundation + scope skeleton.
 *
 * Walks the AST after parsing/macro-expansion and before codegen, populating
 * `node->inferred_type` (and `inferred_struct_idx` for structs).
 *
 * Phase 3a: literals + structural recursion.
 * Phase 3b (this commit): scope tracker + var-ref + simple def/mut.
 *
 * Dual-track contract: compiler.c continues to compute types its own way;
 * the typer pass runs alongside but does not yet drive codegen decisions.
 * When typer and compiler disagree, that's a bug in the typer (compiler.c
 * is the ground truth until Phase 3c switches consumers).
 */

#ifndef TYPER_C
#define TYPER_C

#define TYPER_MAX_BINDINGS    1024
#define TYPER_MAX_PROCS        256
#define TYPER_MAX_PROC_PARAMS   32

typedef struct {
  const char* name;
  uint32_t    name_len;
  uint32_t    scope_mark;   /* hygiene mark from binding's AST node */
  uint8_t     type;         /* JaclType */
  uint32_t    struct_idx;   /* UINT32_MAX if not a struct */
  uint32_t    scope_depth;  /* depth at which this binding was pushed */
} TyperBinding;

typedef struct {
  const char* name;
  uint32_t    name_len;
  uint8_t     return_type;          /* JaclType */
  uint32_t    return_struct_idx;    /* UINT32_MAX if not struct */
  uint8_t     param_count;
  uint8_t     param_types[TYPER_MAX_PROC_PARAMS];
} TyperProc;

typedef struct {
  TyperBinding bindings[TYPER_MAX_BINDINGS];
  uint32_t     binding_count;
  uint32_t     scope_depth;
  /* Global proc registry — populated by a pre-pass over top-level so
   * that calls (which may appear before the proc definition) can look
   * up signatures. */
  TyperProc    procs[TYPER_MAX_PROCS];
  uint32_t     proc_count;
  /* Contextual type hint: parent's "expected_type". Mirrors compiler.c's
   * c->expected_type. Set by callers (e.g., typed def/mut) before recursing
   * into the value expression; restored after. */
  JaclType     expected_type;
} TyperCtx;

static void typer__infer_node(TyperCtx* tc, AstNode* node);

/* --- Scope helpers --- */

static void typer__scope_push(TyperCtx* tc) {
  tc->scope_depth++;
}

static void typer__scope_pop(TyperCtx* tc) {
  while (tc->binding_count > 0 &&
         tc->bindings[tc->binding_count - 1].scope_depth >= tc->scope_depth) {
    tc->binding_count--;
  }
  if (tc->scope_depth > 0) tc->scope_depth--;
}

static void typer__scope_add(TyperCtx* tc, const char* name, uint32_t name_len,
                             uint32_t scope_mark, uint8_t type,
                             uint32_t struct_idx) {
  if (tc->binding_count >= TYPER_MAX_BINDINGS) return;
  TyperBinding* b = &tc->bindings[tc->binding_count++];
  b->name        = name;
  b->name_len    = name_len;
  b->scope_mark  = scope_mark;
  b->type        = type;
  b->struct_idx  = struct_idx;
  b->scope_depth = tc->scope_depth;
}

static const TyperBinding* typer__scope_resolve(TyperCtx* tc,
                                                 const char* name,
                                                 uint32_t name_len,
                                                 uint32_t scope_mark) {
  /* Walk inward-out: most recently pushed binding wins. Match scope_mark
   * for hygiene — macro-introduced bindings shouldn't clash with user
   * bindings of the same spelling. */
  for (int32_t i = (int32_t)tc->binding_count - 1; i >= 0; i--) {
    TyperBinding* b = &tc->bindings[i];
    if (b->name_len == name_len &&
        b->scope_mark == scope_mark &&
        memcmp(b->name, name, name_len) == 0) {
      return b;
    }
  }
  /* Fallback: ignore scope_mark for unprefixed names (most common case). */
  for (int32_t i = (int32_t)tc->binding_count - 1; i >= 0; i--) {
    TyperBinding* b = &tc->bindings[i];
    if (b->name_len == name_len &&
        memcmp(b->name, name, name_len) == 0) {
      return b;
    }
  }
  return NULL;
}

/* --- Command handlers --- */

/* Try to extract a JaclType keyword from a string literal node.
 * Returns true and writes *out_type if the node is a known type keyword. */
static bool typer__node_as_type_keyword(AstNode* node, JaclType* out_type) {
  if (!node || node->type != AST_LIT_STRING) return false;
  const char* w = node->data.lit_string.value;
  uint32_t    n = node->data.lit_string.length;
  if (!is_type_keyword(w, n)) return false;
  *out_type = type_from_keyword(w, n);
  return true;
}

/* Parse "def NAME EXPR" or "def TYPE NAME EXPR" or "mut ..." (same shape).
 * Adds the binding to the current scope and recurses into EXPR.
 * Returns true if handled (so the generic command handler can skip it).
 * Does not handle destructuring forms — those default to TYPE_DYN. */
static bool typer__handle_def_or_mut(TyperCtx* tc, AstNode* node) {
  AstNode** args = node->data.command.args;
  uint32_t  argc = node->data.command.arg_count;

  JaclType  declared_type = TYPE_DYN;
  uint32_t  name_arg_idx  = 0;
  uint32_t  value_arg_idx = 1;

  if (argc == 3) {
    if (!typer__node_as_type_keyword(args[0], &declared_type)) {
      /* args[0] may be a struct name; the typer doesn't have the struct
       * registry yet, so defer to TYPE_DYN. */
      return false;
    }
    name_arg_idx  = 1;
    value_arg_idx = 2;
  } else if (argc != 2) {
    return false;
  }

  AstNode* name_node = args[name_arg_idx];
  if (name_node->type != AST_LIT_STRING) {
    /* Destructuring or hygienic var-ref name forms — defer. */
    return false;
  }

  /* Recurse into the value expression first (it must not see the new
   * binding — bindings come into scope only after their definition).
   * Push declared_type as expected_type so int/float literals can be
   * narrowed (mirrors compiler.c:6127-6129 / 6939-6941). */
  AstNode* value_node = args[value_arg_idx];
  JaclType saved_et   = tc->expected_type;
  tc->expected_type   = declared_type;
  typer__infer_node(tc, value_node);
  tc->expected_type   = saved_et;

  /* Effective type: declared wins; otherwise inherit from RHS. */
  JaclType effective = (declared_type != TYPE_DYN)
                       ? declared_type : (JaclType)value_node->inferred_type;
  uint32_t struct_idx = UINT32_MAX;
  if (effective == TYPE_STRUCT) {
    struct_idx = value_node->inferred_struct_idx;
  }

  typer__scope_add(tc, name_node->data.lit_string.value,
                   name_node->data.lit_string.length,
                   name_node->scope_mark,
                   (uint8_t)effective,
                   struct_idx);

  /* def/mut returns nil. (compiler.c's last_expr_type is sometimes left
   * as the value's type and sometimes set to NIL depending on the binding
   * path — that's the kind of inconsistency the typer pass is replacing.) */
  node->inferred_type = TYPE_NIL;
  node->inferred_struct_idx = UINT32_MAX;
  return true;
}

/* Set helper: "set NAME EXPR" (and "::" / set! sugar). The typer doesn't
 * change the binding's type — set! must agree with the existing type
 * (compiler.c enforces this). Propagate the target's type as
 * expected_type so int/float literals on the RHS narrow correctly
 * (mirrors compiler.c:6424-6426). */
static bool typer__handle_set(TyperCtx* tc, AstNode* node) {
  AstNode** args = node->data.command.args;
  uint32_t  argc = node->data.command.arg_count;
  if (argc != 2) return false;

  AstNode* target = args[0];
  AstNode* value  = args[1];

  /* Resolve target's type if it's a simple var-ref (skip field access etc.). */
  JaclType target_type = TYPE_DYN;
  if (target->type == AST_VAR_REF) {
    const TyperBinding* b = typer__scope_resolve(tc,
        target->data.var_ref.name, target->data.var_ref.length,
        target->scope_mark);
    if (b) target_type = (JaclType)b->type;
  }
  typer__infer_node(tc, target);

  JaclType saved_et = tc->expected_type;
  tc->expected_type = target_type;
  typer__infer_node(tc, value);
  tc->expected_type = saved_et;

  node->inferred_type = TYPE_NIL;
  return true;
}

/* Walk a proc's params node and emit (name, type) pairs to the caller's
 * callback via the out-arrays. Mirrors compiler.c:7100-7180 simple cases:
 * plain name → TYPE_DYN, "TYPE name" pair → that type. Skips compound
 * types ([Vec T], [Map K V]) — those mark the param TYPE_DYN here so we
 * don't hand back wrong info. Returns the number of params written. */
static uint32_t typer__parse_params(AstNode* params,
                                    AstNode* (*name_nodes_out)[TYPER_MAX_PROC_PARAMS],
                                    JaclType (*types_out)[TYPER_MAX_PROC_PARAMS]) {
  uint32_t count = 0;
  if (!params || params->type != AST_COMMAND) return 0;
  /* Build flat element list: head + args */
  AstNode* flat[TYPER_MAX_PROC_PARAMS * 2 + 2];
  uint32_t flat_n = 0;
  AstNode* phead = params->data.command.head;
  if (phead && phead->type == AST_LIT_STRING && phead->data.lit_string.length > 0) {
    flat[flat_n++] = phead;
    for (uint32_t i = 0; i < params->data.command.arg_count
                          && flat_n < sizeof(flat)/sizeof(flat[0]); i++) {
      flat[flat_n++] = params->data.command.args[i];
    }
  }
  for (uint32_t fi = 0; fi < flat_n; fi++) {
    AstNode* elem = flat[fi];
    if (count >= TYPER_MAX_PROC_PARAMS) break;
    if (elem->type == AST_COMMAND) {
      /* compound type expr — skip for now, conservatively dyn */
      fi++;
      if (fi >= flat_n) break;
      elem = flat[fi];
      if (elem->type != AST_LIT_STRING) continue;
      (*name_nodes_out)[count] = elem;
      (*types_out)[count]      = TYPE_DYN;
      count++;
      continue;
    }
    if (elem->type != AST_LIT_STRING) continue;
    /* Type-keyword + name pair? */
    if (is_type_keyword(elem->data.lit_string.value, elem->data.lit_string.length)) {
      JaclType t = type_from_keyword(elem->data.lit_string.value, elem->data.lit_string.length);
      fi++;
      if (fi >= flat_n) break;
      AstNode* next = flat[fi];
      if (next->type != AST_LIT_STRING) continue;
      (*name_nodes_out)[count] = next;
      (*types_out)[count]      = t;
      count++;
    } else if (elem->data.lit_string.length == 3 &&
               memcmp(elem->data.lit_string.value, "...", 3) == 0) {
      /* variadic marker — skip */
      continue;
    } else {
      /* Plain name — TYPE_DYN. Could also be a struct name; treat as dyn for now. */
      (*name_nodes_out)[count] = elem;
      (*types_out)[count]      = TYPE_DYN;
      count++;
    }
  }
  return count;
}

/* Register a proc signature (used both by the top-level pre-pass and
 * lazily for nested procs encountered during walk). Idempotent: a proc
 * already registered (e.g., by the pre-pass) is updated, not duplicated. */
static void typer__register_proc(TyperCtx* tc, AstNode* name_node,
                                  JaclType return_type,
                                  AstNode* (*pn)[TYPER_MAX_PROC_PARAMS],
                                  JaclType (*pt)[TYPER_MAX_PROC_PARAMS],
                                  uint32_t pcount) {
  TyperProc* p = NULL;
  for (uint32_t i = 0; i < tc->proc_count; i++) {
    if (tc->procs[i].name_len == name_node->data.lit_string.length &&
        memcmp(tc->procs[i].name, name_node->data.lit_string.value,
               name_node->data.lit_string.length) == 0) {
      p = &tc->procs[i];
      break;
    }
  }
  if (!p) {
    if (tc->proc_count >= TYPER_MAX_PROCS) return;
    p = &tc->procs[tc->proc_count++];
    p->name     = name_node->data.lit_string.value;
    p->name_len = name_node->data.lit_string.length;
  }
  p->return_type = (uint8_t)return_type;
  p->return_struct_idx = UINT32_MAX;
  if (pcount > TYPER_MAX_PROC_PARAMS) pcount = TYPER_MAX_PROC_PARAMS;
  p->param_count = (uint8_t)pcount;
  for (uint32_t i = 0; i < pcount; i++) {
    p->param_types[i] = (uint8_t)(*pt)[i];
  }
}

/* Proc definition introduces a new isolated scope for params + body.
 * Adds typed params (parsed via typer__parse_params) into scope so
 * var-refs inside the body resolve correctly. Also registers the proc
 * in the global registry so subsequent calls in the same compilation
 * unit can resolve its signature (handles nested-proc case where the
 * top-level pre-pass missed it). */
static bool typer__handle_proc(TyperCtx* tc, AstNode* node) {
  AstNode** args = node->data.command.args;
  uint32_t  argc = node->data.command.arg_count;
  uint32_t  name_idx, params_idx, body_idx;
  JaclType  return_type = TYPE_DYN;
  if (argc == 4) {
    AstNode* tn = args[0];
    if (tn->type == AST_LIT_STRING &&
        is_type_keyword(tn->data.lit_string.value, tn->data.lit_string.length)) {
      return_type = type_from_keyword(tn->data.lit_string.value, tn->data.lit_string.length);
    }
    name_idx = 1; params_idx = 2; body_idx = 3;
  } else if (argc == 3) {
    name_idx = 0; params_idx = 1; body_idx = 2;
  } else return false;

  AstNode* name_node = args[name_idx];
  AstNode* params    = args[params_idx];
  AstNode* body      = args[body_idx];
  if (name_node->type != AST_LIT_STRING) return false;
  if (body->type != AST_BLOCK) return false;

  AstNode* pn[TYPER_MAX_PROC_PARAMS];
  JaclType pt[TYPER_MAX_PROC_PARAMS];
  uint32_t pcount = typer__parse_params(params, &pn, &pt);

  /* Register (idempotent) so nested procs are visible to subsequent
   * calls in the same scope. */
  typer__register_proc(tc, name_node, return_type, &pn, &pt, pcount);

  typer__scope_push(tc);
  for (uint32_t i = 0; i < pcount; i++) {
    typer__scope_add(tc, pn[i]->data.lit_string.value,
                     pn[i]->data.lit_string.length,
                     pn[i]->scope_mark, (uint8_t)pt[i], UINT32_MAX);
  }

  typer__infer_node(tc, body);

  typer__scope_pop(tc);
  node->inferred_type = TYPE_DYN; /* proc def itself returns nil-ish */
  return true;
}

/* Pre-pass: collect proc signatures from top-level so calls can look
 * them up regardless of definition order. Matches compiler.c's
 * Phase 1 proc registration behavior. */
static void typer__register_procs(TyperCtx* tc, AstNode** nodes, uint32_t count) {
  for (uint32_t ni = 0; ni < count; ni++) {
    AstNode* node = nodes[ni];
    if (node->type != AST_COMMAND) continue;
    AstNode* head = node->data.command.head;
    if (!head || head->type != AST_LIT_STRING ||
        head->data.lit_string.length != 4 ||
        memcmp(head->data.lit_string.value, "proc", 4) != 0) continue;

    AstNode** args = node->data.command.args;
    uint32_t  argc = node->data.command.arg_count;
    uint32_t  name_idx, params_idx;
    JaclType  return_type = TYPE_DYN;
    if (argc == 4) {
      AstNode* tn = args[0];
      if (tn->type == AST_LIT_STRING &&
          is_type_keyword(tn->data.lit_string.value, tn->data.lit_string.length)) {
        return_type = type_from_keyword(tn->data.lit_string.value, tn->data.lit_string.length);
      }
      name_idx = 1; params_idx = 2;
    } else if (argc == 3) {
      name_idx = 0; params_idx = 1;
    } else continue;

    AstNode* name_node = args[name_idx];
    if (name_node->type != AST_LIT_STRING) continue;

    if (tc->proc_count >= TYPER_MAX_PROCS) break;
    TyperProc* p = &tc->procs[tc->proc_count++];
    p->name        = name_node->data.lit_string.value;
    p->name_len    = name_node->data.lit_string.length;
    p->return_type = (uint8_t)return_type;
    p->return_struct_idx = UINT32_MAX;

    AstNode* pn[TYPER_MAX_PROC_PARAMS];
    JaclType pt[TYPER_MAX_PROC_PARAMS];
    uint32_t pcount = typer__parse_params(args[params_idx], &pn, &pt);
    if (pcount > TYPER_MAX_PROC_PARAMS) pcount = TYPER_MAX_PROC_PARAMS;
    p->param_count = (uint8_t)pcount;
    for (uint32_t i = 0; i < pcount; i++) {
      p->param_types[i] = (uint8_t)pt[i];
    }
  }
}

static const TyperProc* typer__find_proc(TyperCtx* tc,
                                         const char* name, uint32_t name_len) {
  for (uint32_t i = 0; i < tc->proc_count; i++) {
    if (tc->procs[i].name_len == name_len &&
        memcmp(tc->procs[i].name, name, name_len) == 0) {
      return &tc->procs[i];
    }
  }
  return NULL;
}

/* --- Generic walkers --- */

static void typer__infer_command(TyperCtx* tc, AstNode* node) {
  AstNode* head = node->data.command.head;

  /* Recognize a few common command shapes. Anything not handled falls
   * through to a generic call dispatch (which propagates known proc
   * param types as expected_type for arg literals). */
  if (head && head->type == AST_LIT_STRING) {
    const char* hname = head->data.lit_string.value;
    uint32_t    hlen  = head->data.lit_string.length;
    if ((hlen == 3 && memcmp(hname, "def", 3) == 0) ||
        (hlen == 3 && memcmp(hname, "mut", 3) == 0)) {
      if (typer__handle_def_or_mut(tc, node)) return;
    } else if (hlen == 3 && memcmp(hname, "set", 3) == 0) {
      if (typer__handle_set(tc, node)) return;
    } else if (hlen == 4 && memcmp(hname, "proc", 4) == 0) {
      if (typer__handle_proc(tc, node)) return;
    }
  }

  /* Recognize built-in binary ops where LHS type propagates to RHS
   * (mirrors compiler.c:4097-4100). For + - * / %, < > <= >= == ,
   * the compiler narrows literals on the RHS to match the LHS's type.
   * Result type follows compile_binary: same as operand for arithmetic,
   * BOOL for comparisons. */
  if (head && head->type == AST_LIT_STRING) {
    const char* hname = head->data.lit_string.value;
    uint32_t    hlen  = head->data.lit_string.length;
    bool is_arith = (hlen == 1 && (hname[0] == '+' || hname[0] == '-' ||
                                    hname[0] == '*' || hname[0] == '/' ||
                                    hname[0] == '%'));
    bool is_cmp = false;
    if (!is_arith) {
      if ((hlen == 1 && (hname[0] == '<' || hname[0] == '>')) ||
          (hlen == 2 && (memcmp(hname, "<=", 2) == 0 ||
                         memcmp(hname, ">=", 2) == 0 ||
                         memcmp(hname, "==", 2) == 0))) {
        is_cmp = true;
      }
    }
    if ((is_arith || is_cmp) && node->data.command.arg_count == 2) {
      AstNode* lhs = node->data.command.args[0];
      AstNode* rhs = node->data.command.args[1];
      typer__infer_node(tc, lhs);
      JaclType lhs_t = (JaclType)lhs->inferred_type;
      JaclType saved_et = tc->expected_type;
      if (lhs_t != TYPE_DYN) tc->expected_type = lhs_t;
      typer__infer_node(tc, rhs);
      tc->expected_type = saved_et;
      JaclType rhs_t = (JaclType)rhs->inferred_type;
      if (is_cmp) {
        node->inferred_type = TYPE_BOOL;
      } else if (lhs_t == rhs_t && lhs_t != TYPE_DYN) {
        /* Tagged or unboxed arithmetic, both sides same type — preserve. */
        node->inferred_type = lhs_t;
      } else {
        node->inferred_type = TYPE_DYN;
      }
      return;
    }
  }

  /* Generic call dispatch: if head is a known proc name, propagate
   * declared param types to args so int/float literals narrow. Then
   * propagate the proc's declared return type up. */
  const TyperProc* proc = NULL;
  if (head && head->type == AST_LIT_STRING) {
    proc = typer__find_proc(tc,
        head->data.lit_string.value, head->data.lit_string.length);
  }
  if (head) typer__infer_node(tc, head);
  for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
    AstNode* arg = node->data.command.args[i];
    JaclType saved_et = tc->expected_type;
    if (proc && i < proc->param_count) {
      tc->expected_type = (JaclType)proc->param_types[i];
    } else {
      tc->expected_type = TYPE_DYN;
    }
    typer__infer_node(tc, arg);
    tc->expected_type = saved_et;
  }
  if (proc) {
    node->inferred_type = proc->return_type;
    node->inferred_struct_idx = proc->return_struct_idx;
  } else {
    node->inferred_type = TYPE_DYN;
  }
}

static void typer__infer_block(TyperCtx* tc, AstNode* node) {
  typer__scope_push(tc);
  for (uint32_t i = 0; i < node->data.block.count; i++) {
    typer__infer_node(tc, node->data.block.commands[i]);
  }
  if (node->data.block.count > 0 && !node->data.block.trailing_semi) {
    AstNode* last = node->data.block.commands[node->data.block.count - 1];
    node->inferred_type = last->inferred_type;
    node->inferred_struct_idx = last->inferred_struct_idx;
  } else {
    node->inferred_type = TYPE_NIL;
  }
  typer__scope_pop(tc);
}

static void typer__infer_var_ref(TyperCtx* tc, AstNode* node) {
  const TyperBinding* b = typer__scope_resolve(tc,
      node->data.var_ref.name,
      node->data.var_ref.length,
      node->scope_mark);
  if (b) {
    node->inferred_type = b->type;
    node->inferred_struct_idx = b->struct_idx;
  } else {
    node->inferred_type = TYPE_DYN;
  }
}

static void typer__infer_node(TyperCtx* tc, AstNode* node) {
  if (!node) return;
  switch (node->type) {
    case AST_LIT_INT: {
      /* Mirror compiler.c:11061-11093: expected_type can promote an int
       * literal to i64/u64/f64/u32/f32. Default is i32. */
      switch (tc->expected_type) {
        case TYPE_I64: node->inferred_type = TYPE_I64; break;
        case TYPE_U64: node->inferred_type = TYPE_U64; break;
        case TYPE_F64: node->inferred_type = TYPE_F64; break;
        case TYPE_U32: node->inferred_type = TYPE_U32; break;
        case TYPE_F32: node->inferred_type = TYPE_F32; break;
        default:       node->inferred_type = TYPE_I32; break;
      }
      break;
    }
    case AST_LIT_FLOAT:
      /* Mirror compiler.c:11122-11138: expected_type can promote f32 → f64. */
      node->inferred_type = (tc->expected_type == TYPE_F64) ? TYPE_F64 : TYPE_F32;
      break;
    case AST_LIT_STRING:
      node->inferred_type = TYPE_STR;
      break;
    case AST_VAR_REF:
      typer__infer_var_ref(tc, node);
      break;
    case AST_BLOCK:
      typer__infer_block(tc, node);
      break;
    case AST_COMMAND:
      typer__infer_command(tc, node);
      break;
    case AST_INTERP_STRING:
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        typer__infer_node(tc, node->data.interp_string.segments[i]);
      }
      node->inferred_type = TYPE_STR;
      break;
    case AST_BREAK:
      if (node->data.break_stmt.value) typer__infer_node(tc, node->data.break_stmt.value);
      node->inferred_type = TYPE_NIL;
      break;
    case AST_RETURN:
      if (node->data.return_stmt.value) typer__infer_node(tc, node->data.return_stmt.value);
      node->inferred_type = TYPE_NIL;
      break;
    case AST_QUOTE:
      if (node->data.quote.child) typer__infer_node(tc, node->data.quote.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_SYNTAX_QUOTE:
      if (node->data.syntax_quote.child) typer__infer_node(tc, node->data.syntax_quote.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_UNQUOTE:
      if (node->data.unquote.child) typer__infer_node(tc, node->data.unquote.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_UNQUOTE_SPLICING:
      if (node->data.unquote_splicing.child) typer__infer_node(tc, node->data.unquote_splicing.child);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_SPREAD:
      if (node->data.spread.expr) typer__infer_node(tc, node->data.spread.expr);
      node->inferred_type = TYPE_DYN;
      break;
    case AST_SHELL_CMD:
      if (node->data.shell_cmd.head) typer__infer_node(tc, node->data.shell_cmd.head);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        typer__infer_node(tc, node->data.shell_cmd.args[i]);
      }
      node->inferred_type = TYPE_DYN;
      break;
    case AST_USE:
    case AST_DEFSTRUCT:
    case AST_DEFMACRO:
    case AST_DESTRUCTURE_VEC:
    case AST_DESTRUCTURE_NAMED:
    case AST_CONTINUE:
    case AST_CTX_DECL:
    case AST_ERROR:
    default:
      node->inferred_type = TYPE_DYN;
      break;
  }
}

void typer_infer(AstNode** nodes, uint32_t count) {
  TyperCtx tc;
  tc.binding_count = 0;
  tc.scope_depth   = 0;
  tc.proc_count    = 0;
  tc.expected_type = TYPE_DYN;

  /* Pre-pass: register top-level proc signatures so calls (which may
   * appear before the definition) resolve correctly. */
  typer__register_procs(&tc, nodes, count);

  for (uint32_t i = 0; i < count; i++) {
    typer__infer_node(&tc, nodes[i]);
  }
}

#endif /* TYPER_C */
