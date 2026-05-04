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

#define TYPER_MAX_BINDINGS 1024

typedef struct {
  const char* name;
  uint32_t    name_len;
  uint32_t    scope_mark;   /* hygiene mark from binding's AST node */
  uint8_t     type;         /* JaclType */
  uint32_t    struct_idx;   /* UINT32_MAX if not a struct */
  uint32_t    scope_depth;  /* depth at which this binding was pushed */
} TyperBinding;

typedef struct {
  TyperBinding bindings[TYPER_MAX_BINDINGS];
  uint32_t     binding_count;
  uint32_t     scope_depth;
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
 * (compiler.c enforces this). We just recurse into the value. */
static bool typer__handle_set(TyperCtx* tc, AstNode* node) {
  AstNode** args = node->data.command.args;
  uint32_t  argc = node->data.command.arg_count;
  if (argc != 2) return false;
  /* Recurse into both children but don't add a new binding. */
  typer__infer_node(tc, args[0]);
  typer__infer_node(tc, args[1]);
  node->inferred_type = TYPE_NIL;
  return true;
}

/* Proc definition introduces a new isolated scope for params + body.
 * Skeleton: enter scope, add params (typed if annotated), walk body, exit.
 * Doesn't yet register the proc itself in any global table — that comes
 * with the next subphase (proc-call return type tracking). */
static bool typer__handle_proc(TyperCtx* tc, AstNode* node) {
  AstNode** args = node->data.command.args;
  uint32_t  argc = node->data.command.arg_count;
  uint32_t  params_idx, body_idx;
  if (argc == 4) { params_idx = 2; body_idx = 3; }
  else if (argc == 3) { params_idx = 1; body_idx = 2; }
  else return false;

  AstNode* params = args[params_idx];
  AstNode* body   = args[body_idx];
  if (body->type != AST_BLOCK) return false;

  typer__scope_push(tc);

  /* Params: AST_COMMAND of the form [name1 name2 ...] or [TYPE name1 ...]
   * Compiler parses these in pairs, but for the skeleton we conservatively
   * mark all params as TYPE_DYN. Future subphase will walk type annotations. */
  if (params && params->type == AST_COMMAND) {
    AstNode* phead = params->data.command.head;
    if (phead && phead->type == AST_LIT_STRING) {
      typer__scope_add(tc, phead->data.lit_string.value,
                       phead->data.lit_string.length,
                       phead->scope_mark, TYPE_DYN, UINT32_MAX);
    }
    for (uint32_t i = 0; i < params->data.command.arg_count; i++) {
      AstNode* p = params->data.command.args[i];
      if (p->type == AST_LIT_STRING) {
        typer__scope_add(tc, p->data.lit_string.value,
                         p->data.lit_string.length,
                         p->scope_mark, TYPE_DYN, UINT32_MAX);
      }
    }
  }

  typer__infer_node(tc, body);

  typer__scope_pop(tc);
  node->inferred_type = TYPE_DYN; /* proc def itself returns nil-ish */
  return true;
}

/* --- Generic walkers --- */

static void typer__infer_command(TyperCtx* tc, AstNode* node) {
  AstNode* head = node->data.command.head;

  /* Recognize a few common command shapes. Anything not handled falls
   * through to TYPE_DYN with structural recursion. */
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

  /* Default: recurse, leave type as TYPE_DYN. */
  if (head) typer__infer_node(tc, head);
  for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
    typer__infer_node(tc, node->data.command.args[i]);
  }
  node->inferred_type = TYPE_DYN;
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
  tc.expected_type = TYPE_DYN;
  for (uint32_t i = 0; i < count; i++) {
    typer__infer_node(&tc, nodes[i]);
  }
}

#endif /* TYPER_C */
