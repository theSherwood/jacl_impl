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
#define TYPER_MAX_STRUCTS      256
#define TYPER_MAX_STRUCT_FIELDS 32
#define TYPER_MAX_NARROWINGS   8

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
  const char* name;
  uint32_t    name_len;
  uint8_t     field_count;
  uint8_t     field_types[TYPER_MAX_STRUCT_FIELDS]; /* JaclType per field; TYPE_STRUCT if a nested struct */
  const char* field_names[TYPER_MAX_STRUCT_FIELDS];
  uint32_t    field_name_lens[TYPER_MAX_STRUCT_FIELDS];
  /* For TYPE_STRUCT fields, the index of the nested struct in
   * tc->structs (so chained `$x.field.subfield` can resolve the
   * subfield's type). UINT32_MAX for non-struct fields or unresolved. */
  uint32_t    field_struct_idxs[TYPER_MAX_STRUCT_FIELDS];
} TyperStruct;

typedef struct {
  TyperBinding bindings[TYPER_MAX_BINDINGS];
  uint32_t     binding_count;
  uint32_t     scope_depth;
  /* Global proc registry — populated by a pre-pass over top-level so
   * that calls (which may appear before the proc definition) can look
   * up signatures. */
  TyperProc    procs[TYPER_MAX_PROCS];
  uint32_t     proc_count;
  /* Global struct registry — populated by the same pre-pass. Used by
   * struct constructor narrowing: when a command head matches a
   * registered struct name, propagate field types to the args. */
  TyperStruct  structs[TYPER_MAX_STRUCTS];
  uint32_t     struct_count;
  /* Contextual type hint: parent's "expected_type". Mirrors compiler.c's
   * c->expected_type. Set by callers (e.g., typed def/mut) before recursing
   * into the value expression; restored after. */
  JaclType     expected_type;
  /* Flow-typing narrowings from [box? Type $var] guards in if-branches.
   * Mirrors compiler.c:2810-2817. Active only inside the then-branch of
   * a box?-guarded if; saved/restored across branch boundaries. */
  struct {
    const char* name;
    uint32_t    name_len;
    uint32_t    scope_mark;
    uint8_t     box_type;        /* JaclType */
    uint32_t    box_struct_idx;  /* UINT32_MAX if not struct */
  } narrowings[TYPER_MAX_NARROWINGS];
  uint32_t     narrowing_count;
} TyperCtx;

static void typer__infer_node(TyperCtx* tc, AstNode* node);
static const TyperStruct* typer__find_struct(TyperCtx* tc, const char* name, uint32_t name_len);
static bool typer__body_yields(AstNode* node);

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

/* Recognize [Vec T] / [Map V] / [Map K V] type expressions. Returns
 * 1 for [Vec T], 2 for [Map V] (dyn keys), 3 for [Map K V] (struct
 * keys), 0 if not a typed-collection expression. Mirrors
 * compiler__typed_collection_expr in compiler.c. */
static int typer__typed_collection_kind(AstNode* node) {
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return 0;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 3) return 0;
  int kind = 0;
  if (memcmp(h->data.lit_string.value, "Vec", 3) == 0) kind = 1;
  else if (memcmp(h->data.lit_string.value, "Map", 3) == 0) kind = 2;
  if (kind == 0) return 0;
  uint32_t ac = node->data.command.arg_count;
  if (kind == 2 && ac == 2 &&
      node->data.command.args[0]->type == AST_LIT_STRING &&
      node->data.command.args[1]->type == AST_LIT_STRING) {
    return 3;
  }
  if (ac != 1 || node->data.command.args[0]->type != AST_LIT_STRING) return 0;
  return kind;
}

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

/* Parse the family of binding forms that resolve to def/mut/set:
 *   def NAME EXPR / mut NAME EXPR
 *   def TYPE NAME EXPR / mut TYPE NAME EXPR
 *   NAME = EXPR / NAME : EXPR     (sugar — argc=2, args[0]=name)
 *   [TYPE NAME] = EXPR / [TYPE NAME] : EXPR  (sugar — argc=2, args[0]=AST_COMMAND)
 * Adds the binding to the current scope and recurses into EXPR with
 * declared_type pushed as expected_type. Returns true if handled.
 * Does not handle destructuring forms — those default to TYPE_DYN. */
static bool typer__handle_def_or_mut(TyperCtx* tc, AstNode* node) {
  AstNode** args = node->data.command.args;
  uint32_t  argc = node->data.command.arg_count;

  JaclType  declared_type = TYPE_DYN;
  AstNode*  name_node     = NULL;
  AstNode*  value_node    = NULL;

  uint32_t declared_struct_idx = UINT32_MAX;
  if (argc == 3) {
    /* Keyword form: def TYPE NAME VALUE, or def StructName NAME VALUE,
     * or def [Vec T] / [Map K V] NAME VALUE for typed collections. */
    if (typer__node_as_type_keyword(args[0], &declared_type)) {
      /* type keyword */
    } else if (args[0]->type == AST_LIT_STRING) {
      /* Possibly a registered struct name. */
      const TyperStruct* sd = typer__find_struct(tc,
          args[0]->data.lit_string.value, args[0]->data.lit_string.length);
      if (sd) {
        declared_type = TYPE_STRUCT;
        for (uint32_t si = 0; si < tc->struct_count; si++) {
          if (&tc->structs[si] == sd) { declared_struct_idx = si; break; }
        }
      } else {
        return false;
      }
    } else {
      int tcoll = typer__typed_collection_kind(args[0]);
      if (tcoll == 1) declared_type = TYPE_TYPED_VEC;
      else if (tcoll == 2 || tcoll == 3) declared_type = TYPE_TYPED_MAP;
      else return false;
      /* Element struct_idx for [Vec T] / [Map V] / [Map K V] declared types. */
      AstNode* type_arg = (tcoll == 3)
          ? args[0]->data.command.args[1]
          : args[0]->data.command.args[0];
      if (type_arg && type_arg->type == AST_LIT_STRING) {
        const char* nm = type_arg->data.lit_string.value;
        uint32_t    nl = type_arg->data.lit_string.length;
        if (is_type_keyword(nm, nl)) {
          declared_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
        } else {
          for (uint32_t si = 0; si < tc->struct_count; si++) {
            if (tc->structs[si].name_len == nl &&
                memcmp(tc->structs[si].name, nm, nl) == 0) {
              declared_struct_idx = si;
              break;
            }
          }
        }
      }
    }
    name_node  = args[1];
    value_node = args[2];
  } else if (argc == 2) {
    /* Two-arg shapes:
     *   def NAME VALUE                           — keyword + bare name
     *   [TYPE NAME] = VALUE / [TYPE NAME] : VALUE — sugar with typed LHS
     *   def DESTRUCTURE_VEC VALUE                 — vec destructuring
     *   def DESTRUCTURE_NAMED VALUE               — struct/map destructuring
     * The sugar form's LHS is an AST_COMMAND with head=type and one arg=name. */
    if (args[0]->type == AST_DESTRUCTURE_VEC) {
      JaclType saved_et = tc->expected_type;
      tc->expected_type = TYPE_DYN;
      typer__infer_node(tc, args[1]);
      tc->expected_type = saved_et;
      uint32_t cnt = args[0]->data.destructure_vec.count;
      for (uint32_t i = 0; i < cnt; i++) {
        JaclType t = TYPE_DYN;
        if (args[0]->data.destructure_vec.types &&
            args[0]->data.destructure_vec.types[i] &&
            is_type_keyword(args[0]->data.destructure_vec.types[i],
                            args[0]->data.destructure_vec.type_lens[i])) {
          t = type_from_keyword(args[0]->data.destructure_vec.types[i],
                                args[0]->data.destructure_vec.type_lens[i]);
        }
        typer__scope_add(tc,
            args[0]->data.destructure_vec.names[i],
            args[0]->data.destructure_vec.name_lens[i],
            args[0]->scope_mark, (uint8_t)t, UINT32_MAX);
      }
      node->inferred_type = TYPE_NIL;
      return true;
    }
    if (args[0]->type == AST_DESTRUCTURE_NAMED) {
      JaclType saved_et = tc->expected_type;
      tc->expected_type = TYPE_DYN;
      typer__infer_node(tc, args[1]);
      tc->expected_type = saved_et;
      uint32_t cnt = args[0]->data.destructure_named.count;
      /* If the value is a struct, look up field types from the registry
       * so destructured names get the right type. */
      const TyperStruct* src_struct = NULL;
      if (args[1]->inferred_type == TYPE_STRUCT &&
          args[1]->inferred_struct_idx != UINT32_MAX &&
          args[1]->inferred_struct_idx < tc->struct_count) {
        src_struct = &tc->structs[args[1]->inferred_struct_idx];
      }
      for (uint32_t i = 0; i < cnt; i++) {
        JaclType t = TYPE_DYN;
        /* Explicit type annotation wins. */
        if (args[0]->data.destructure_named.types &&
            args[0]->data.destructure_named.types[i] &&
            is_type_keyword(args[0]->data.destructure_named.types[i],
                            args[0]->data.destructure_named.type_lens[i])) {
          t = type_from_keyword(args[0]->data.destructure_named.types[i],
                                args[0]->data.destructure_named.type_lens[i]);
        } else if (src_struct) {
          /* Match destructured name against source struct's field names. */
          const char* dn = args[0]->data.destructure_named.names[i];
          uint32_t    dl = args[0]->data.destructure_named.name_lens[i];
          for (uint32_t fi = 0; fi < src_struct->field_count; fi++) {
            if (src_struct->field_name_lens[fi] == dl &&
                memcmp(src_struct->field_names[fi], dn, dl) == 0) {
              t = (JaclType)src_struct->field_types[fi];
              break;
            }
          }
        }
        typer__scope_add(tc,
            args[0]->data.destructure_named.names[i],
            args[0]->data.destructure_named.name_lens[i],
            args[0]->scope_mark, (uint8_t)t, UINT32_MAX);
      }
      node->inferred_type = TYPE_NIL;
      return true;
    }
    if (args[0]->type == AST_BLOCK) {
      /* `{x, y}` named destructuring (parser produces AST_BLOCK
       * with each name as a zero-arg AST_COMMAND inside).
       * Match destructured names against the source struct's field
       * types when available. */
      JaclType saved_et = tc->expected_type;
      tc->expected_type = TYPE_DYN;
      typer__infer_node(tc, args[1]);
      tc->expected_type = saved_et;
      const TyperStruct* src_struct = NULL;
      if (args[1]->inferred_type == TYPE_STRUCT &&
          args[1]->inferred_struct_idx < tc->struct_count) {
        src_struct = &tc->structs[args[1]->inferred_struct_idx];
      }
      uint32_t bcount = args[0]->data.block.count;
      for (uint32_t i = 0; i < bcount; i++) {
        AstNode* item = args[0]->data.block.commands[i];
        const char* nm = NULL;
        uint32_t    nl = 0;
        JaclType    item_t = TYPE_DYN;
        if (item->type == AST_COMMAND && item->data.command.head &&
            item->data.command.head->type == AST_LIT_STRING) {
          if (item->data.command.arg_count == 0) {
            /* Bare name */
            nm = item->data.command.head->data.lit_string.value;
            nl = item->data.command.head->data.lit_string.length;
          } else if (item->data.command.arg_count == 1 &&
                     item->data.command.args[0]->type == AST_LIT_STRING &&
                     is_type_keyword(item->data.command.head->data.lit_string.value,
                                     item->data.command.head->data.lit_string.length)) {
            /* Typed: `i32 x` */
            item_t = type_from_keyword(item->data.command.head->data.lit_string.value,
                                       item->data.command.head->data.lit_string.length);
            nm = item->data.command.args[0]->data.lit_string.value;
            nl = item->data.command.args[0]->data.lit_string.length;
          }
        }
        if (!nm) continue;
        if (item_t == TYPE_DYN && src_struct) {
          for (uint32_t fi = 0; fi < src_struct->field_count; fi++) {
            if (src_struct->field_name_lens[fi] == nl &&
                memcmp(src_struct->field_names[fi], nm, nl) == 0) {
              item_t = (JaclType)src_struct->field_types[fi];
              break;
            }
          }
        }
        typer__scope_add(tc, nm, nl, item->scope_mark,
                         (uint8_t)item_t, UINT32_MAX);
      }
      node->inferred_type = TYPE_NIL;
      return true;
    }
    if (args[0]->type == AST_COMMAND &&
        args[0]->data.command.arg_count == 0 &&
        args[0]->data.command.head &&
        args[0]->data.command.head->type == AST_LIT_STRING) {
      /* `[name] = value` — bare identifier wrapped as a zero-arg command.
       * Mirrors compiler__rewrite_binding_op's "unwrap to [target name RHS]"
       * branch (compiler.c:4650). Without this, `x = 5` falls through to
       * the AST_COMMAND name branch and bails with return false. */
      name_node  = args[0]->data.command.head;
      value_node = args[1];
    } else if (args[0]->type == AST_COMMAND &&
        args[0]->data.command.arg_count == 1 &&
        args[0]->data.command.head &&
        args[0]->data.command.head->type == AST_LIT_STRING) {
      /* Two shapes:
       *   [type_kw name] = value  → typed binding (declared_type set)
       *   [def name] = value      → surface `def x = 5` parses as
       *                             [= [def x] 5]; the redundant 'def'
       *                             head means "just bind name to value"
       *   [Struct name] = value   → typed struct binding (handled below
       *                             via the struct-name branch in argc==3)
       * For the typer, we just need to extract name_node; declared_type
       * is set only when the outer head is a type keyword. */
      typer__node_as_type_keyword(args[0]->data.command.head, &declared_type);
      name_node  = args[0]->data.command.args[0];
      value_node = args[1];
    } else {
      name_node  = args[0];
      value_node = args[1];
    }
  } else {
    return false;
  }

  if (name_node->type != AST_LIT_STRING) {
    /* Hygienic var-ref name forms — defer. */
    return false;
  }

  /* Recurse into the value expression first (it must not see the new
   * binding — bindings come into scope only after their definition).
   * Push declared_type as expected_type so int/float literals can be
   * narrowed (mirrors compiler.c:6127-6129 / 6939-6941). */
  JaclType saved_et   = tc->expected_type;
  tc->expected_type   = declared_type;
  typer__infer_node(tc, value_node);
  tc->expected_type   = saved_et;

  /* Effective type: declared wins. For an untyped def/mut, mirror
   * compiler.c:7013-7019: only unboxed scalars (i64/u64/f64), structs,
   * streams, and typed collections are inherited from the RHS; tagged
   * scalars (i32/u32/f32/bool/etc.) collapse to DYN. */
  JaclType effective;
  if (declared_type != TYPE_DYN) {
    effective = declared_type;
  } else {
    JaclType rhs_t = (JaclType)value_node->inferred_type;
    if (is_unboxed_type(rhs_t) || rhs_t == TYPE_STRUCT ||
        rhs_t == TYPE_STREAM || is_typed_collection(rhs_t)) {
      effective = rhs_t;
    } else {
      effective = TYPE_DYN;
    }
  }
  uint32_t struct_idx = UINT32_MAX;
  if (effective == TYPE_STRUCT || is_typed_collection(effective)) {
    /* Declared struct (def Point r ...) / typed-collection elem
     * (def [Vec Point] ps ...) wins; otherwise inherit from RHS. */
    if (declared_struct_idx != UINT32_MAX) {
      struct_idx = declared_struct_idx;
    } else {
      struct_idx = value_node->inferred_struct_idx;
    }
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

  /* Resolve target's type. For `set name value`, the name is parsed as a
   * bare string literal (AST_LIT_STRING). For `$name :: value`, it's a
   * var-ref. Both forms — plus the AST_COMMAND arrow form (handled
   * separately in compiler.c) — should look up the binding. */
  JaclType target_type = TYPE_DYN;
  const char* tname = NULL;
  uint32_t    tlen  = 0;
  if (target->type == AST_VAR_REF) {
    tname = target->data.var_ref.name;
    tlen  = target->data.var_ref.length;
  } else if (target->type == AST_LIT_STRING) {
    tname = target->data.lit_string.value;
    tlen  = target->data.lit_string.length;
  } else if (target->type == AST_COMMAND &&
             target->data.command.arg_count == 0 &&
             target->data.command.head &&
             target->data.command.head->type == AST_LIT_STRING) {
    /* `x :: value` parses x as AST_COMMAND with head LIT_STRING and no
     * args — a "command call with no args" form for bare identifiers
     * on the LHS of an infix. Treat as a name lookup. */
    tname = target->data.command.head->data.lit_string.value;
    tlen  = target->data.command.head->data.lit_string.length;
  }
  if (tname) {
    const TyperBinding* b = typer__scope_resolve(tc, tname, tlen, target->scope_mark);
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
 * plain name → TYPE_DYN, "TYPE name" pair → that type, "Struct name"
 * pair → TYPE_STRUCT. Skips compound types ([Vec T], [Map K V]) —
 * those mark the param TYPE_DYN. Returns the number of params written. */
static uint32_t typer__parse_params(TyperCtx* tc, AstNode* params,
                                    AstNode* (*name_nodes_out)[TYPER_MAX_PROC_PARAMS],
                                    JaclType (*types_out)[TYPER_MAX_PROC_PARAMS],
                                    uint32_t (*struct_idxs_out)[TYPER_MAX_PROC_PARAMS]) {
  uint32_t count = 0;
  if (!params || params->type != AST_COMMAND) return 0;
  /* Build flat element list: head + args. Head may be a LIT_STRING
   * (typical, e.g., `{i32 x}`) OR an AST_COMMAND for compound type
   * params (`{[Vec Point] pts}` → head=[Vec Point], args=[pts]). */
  AstNode* flat[TYPER_MAX_PROC_PARAMS * 2 + 2];
  uint32_t flat_n = 0;
  AstNode* phead = params->data.command.head;
  bool head_is_named =
      phead && phead->type == AST_LIT_STRING && phead->data.lit_string.length > 0;
  bool head_is_compound = phead && phead->type == AST_COMMAND;
  if (head_is_named || head_is_compound) {
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
      /* compound type expr: [Vec T] / [Map K V] / [Map V] resolves to a
       * typed collection; anything else falls back to dyn. Element
       * struct_idx is propagated so vec-get/map-get on the param can
       * narrow the result to TYPE_STRUCT. */
      int tcoll = typer__typed_collection_kind(elem);
      JaclType t = TYPE_DYN;
      uint32_t elem_sidx = UINT32_MAX;
      if (tcoll == 1) t = TYPE_TYPED_VEC;
      else if (tcoll == 2 || tcoll == 3) t = TYPE_TYPED_MAP;
      if (tcoll == 1 || tcoll == 2 || tcoll == 3) {
        AstNode* type_arg = (tcoll == 3)
            ? elem->data.command.args[1]
            : elem->data.command.args[0];
        if (type_arg && type_arg->type == AST_LIT_STRING) {
          const char* nm = type_arg->data.lit_string.value;
          uint32_t    nl = type_arg->data.lit_string.length;
          if (is_type_keyword(nm, nl)) {
            elem_sidx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
          } else {
            for (uint32_t si = 0; si < tc->struct_count; si++) {
              if (tc->structs[si].name_len == nl &&
                  memcmp(tc->structs[si].name, nm, nl) == 0) {
                elem_sidx = si;
                break;
              }
            }
          }
        }
      }
      fi++;
      if (fi >= flat_n) break;
      elem = flat[fi];
      if (elem->type != AST_LIT_STRING) continue;
      (*name_nodes_out)[count]   = elem;
      (*types_out)[count]        = t;
      (*struct_idxs_out)[count]  = elem_sidx;
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
      (*name_nodes_out)[count]   = next;
      (*types_out)[count]        = t;
      (*struct_idxs_out)[count]  = UINT32_MAX;
      count++;
    } else if (elem->data.lit_string.length == 3 &&
               memcmp(elem->data.lit_string.value, "...", 3) == 0) {
      /* variadic marker — skip */
      continue;
    } else {
      /* Struct-name + name pair? Look up. */
      uint32_t found_idx = UINT32_MAX;
      for (uint32_t si = 0; si < tc->struct_count; si++) {
        if (tc->structs[si].name_len == elem->data.lit_string.length &&
            memcmp(tc->structs[si].name, elem->data.lit_string.value,
                   elem->data.lit_string.length) == 0) {
          found_idx = si;
          break;
        }
      }
      if (found_idx != UINT32_MAX) {
        fi++;
        if (fi >= flat_n) break;
        AstNode* next = flat[fi];
        if (next->type != AST_LIT_STRING) continue;
        (*name_nodes_out)[count]   = next;
        (*types_out)[count]        = TYPE_STRUCT;
        (*struct_idxs_out)[count]  = found_idx;
        count++;
      } else {
        (*name_nodes_out)[count]   = elem;
        (*types_out)[count]        = TYPE_DYN;
        (*struct_idxs_out)[count]  = UINT32_MAX;
        count++;
      }
    }
  }
  return count;
}

/* Register a proc signature (used both by the top-level pre-pass and
 * lazily for nested procs encountered during walk). Idempotent: a proc
 * already registered (e.g., by the pre-pass) is updated, not duplicated. */
static void typer__register_proc(TyperCtx* tc, AstNode* name_node,
                                  JaclType return_type,
                                  uint32_t return_struct_idx,
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
  p->return_struct_idx = return_struct_idx;
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
  uint32_t  return_struct_idx = UINT32_MAX;
  if (argc == 4) {
    AstNode* tn = args[0];
    if (tn->type == AST_LIT_STRING) {
      if (is_type_keyword(tn->data.lit_string.value, tn->data.lit_string.length)) {
        return_type = type_from_keyword(tn->data.lit_string.value, tn->data.lit_string.length);
      } else {
        for (uint32_t si = 0; si < tc->struct_count; si++) {
          if (tc->structs[si].name_len == tn->data.lit_string.length &&
              memcmp(tc->structs[si].name, tn->data.lit_string.value,
                     tn->data.lit_string.length) == 0) {
            return_type = TYPE_STRUCT;
            return_struct_idx = si;
            break;
          }
        }
      }
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
  uint32_t ps[TYPER_MAX_PROC_PARAMS];
  uint32_t pcount = typer__parse_params(tc, params, &pn, &pt, &ps);

  /* Generator detection: yielding body without declared return type
   * → returns TYPE_STREAM. Mirrors the pre-pass detection in
   * typer__register_procs (handle_proc updates the entry, so we
   * detect again here to avoid clobbering the pre-pass). */
  if (return_type == TYPE_DYN && typer__body_yields(body)) {
    return_type = TYPE_STREAM;
  }

  /* Register (idempotent) so nested procs are visible to subsequent
   * calls in the same scope. */
  typer__register_proc(tc, name_node, return_type, return_struct_idx, &pn, &pt, pcount);

  typer__scope_push(tc);
  for (uint32_t i = 0; i < pcount; i++) {
    typer__scope_add(tc, pn[i]->data.lit_string.value,
                     pn[i]->data.lit_string.length,
                     pn[i]->scope_mark, (uint8_t)pt[i], ps[i]);
  }

  /* Walk body. For the last statement, push return_type as expected_type
   * so int/float literals at the tail position get narrowed (mirrors
   * compiler.c:3851-3853). */
  uint32_t body_count = body->data.block.count;
  if (body_count == 0) {
    body->inferred_type = TYPE_NIL;
  } else {
    for (uint32_t i = 0; i + 1 < body_count; i++) {
      typer__infer_node(tc, body->data.block.commands[i]);
    }
    JaclType saved_et = tc->expected_type;
    if (return_type != TYPE_DYN) tc->expected_type = return_type;
    typer__infer_node(tc, body->data.block.commands[body_count - 1]);
    tc->expected_type = saved_et;
    if (!body->data.block.trailing_semi) {
      AstNode* last = body->data.block.commands[body_count - 1];
      body->inferred_type = last->inferred_type;
      body->inferred_struct_idx = last->inferred_struct_idx;
    } else {
      body->inferred_type = TYPE_NIL;
    }
  }

  typer__scope_pop(tc);
  node->inferred_type = TYPE_CLOSURE; /* proc def emits a closure value */
  return true;
}

/* Pre-pass: collect AST_CTX_DECL nodes and populate a synthetic
 * "ctx" entry in the typer's struct registry. Mirrors the compiler's
 * CtxFieldList: each `ctx Type field_name = default` declaration adds
 * one field. After this runs, the `ctx` binding (added by typer_infer)
 * can be retargeted to point at the synthetic struct so `$ctx.field`
 * resolves the field's type via the existing HEAD_DOT path. Returns
 * the struct index of the ctx entry, or UINT32_MAX if no ctx fields
 * were declared. */
static uint32_t typer__register_ctx_struct(TyperCtx* tc,
                                            AstNode** nodes, uint32_t count) {
  /* Count fields first to decide whether to allocate a slot. */
  uint32_t fcount = 0;
  for (uint32_t ni = 0; ni < count; ni++) {
    if (nodes[ni]->type == AST_CTX_DECL) fcount++;
  }
  if (fcount == 0) return UINT32_MAX;

  /* Ctx occupies the pre-reserved slot 1 (see typer_infer init).
   * Mirrors the compiler's struct_registry slot reservation. */
  uint32_t ctx_idx = 1;
  TyperStruct* s = &tc->structs[ctx_idx];
  s->name = "ctx";
  s->name_len = 3;
  if (fcount > TYPER_MAX_STRUCT_FIELDS) fcount = TYPER_MAX_STRUCT_FIELDS;
  s->field_count = (uint8_t)fcount;
  uint32_t fi = 0;
  for (uint32_t ni = 0; ni < count && fi < fcount; ni++) {
    AstNode* node = nodes[ni];
    if (node->type != AST_CTX_DECL) continue;
    const char* tn = node->data.ctx_decl.type_name;
    uint32_t    tl = node->data.ctx_decl.type_name_len;
    JaclType ft = TYPE_DYN;
    uint32_t f_struct_idx = UINT32_MAX;
    if (tn && is_type_keyword(tn, tl)) {
      ft = type_from_keyword(tn, tl);
    } else if (tn) {
      ft = TYPE_STRUCT;
      const TyperStruct* found = typer__find_struct(tc, tn, tl);
      if (found) f_struct_idx = (uint32_t)(found - tc->structs);
    }
    s->field_types[fi]        = (uint8_t)ft;
    s->field_names[fi]        = node->data.ctx_decl.field_name;
    s->field_name_lens[fi]    = node->data.ctx_decl.field_name_len;
    s->field_struct_idxs[fi]  = f_struct_idx;
    fi++;
  }
  return ctx_idx;
}

/* Pre-pass: collect struct definitions so struct constructor calls
 * (which propagate field types to args) can resolve. */
static void typer__register_structs(TyperCtx* tc, AstNode** nodes, uint32_t count) {
  /* Pass 1: register names and primitive field types. Defer struct-
   * typed field idx resolution to pass 2 so forward references work
   * (struct A with field of type B, where B is defined after A). */
  uint32_t first_added = tc->struct_count;
  for (uint32_t ni = 0; ni < count; ni++) {
    AstNode* node = nodes[ni];
    if (node->type != AST_DEFSTRUCT) continue;
    if (tc->struct_count >= TYPER_MAX_STRUCTS) break;
    TyperStruct* s = &tc->structs[tc->struct_count++];
    s->name     = node->data.defstruct.name;
    s->name_len = node->data.defstruct.name_len;
    uint32_t fc = node->data.defstruct.field_count;
    if (fc > TYPER_MAX_STRUCT_FIELDS) fc = TYPER_MAX_STRUCT_FIELDS;
    s->field_count = (uint8_t)fc;
    for (uint32_t i = 0; i < fc; i++) {
      const char* tn = node->data.defstruct.field_types[i];
      uint32_t    tl = node->data.defstruct.field_type_lens[i];
      JaclType ft;
      if (is_type_keyword(tn, tl)) {
        ft = type_from_keyword(tn, tl);
      } else {
        /* Nested struct — type set to TYPE_STRUCT here; struct_idx
         * resolved in pass 2 below. */
        ft = TYPE_STRUCT;
      }
      s->field_types[i]        = (uint8_t)ft;
      s->field_names[i]        = node->data.defstruct.field_names[i];
      s->field_name_lens[i]    = node->data.defstruct.field_name_lens[i];
      s->field_struct_idxs[i]  = UINT32_MAX;
    }
  }
  /* Pass 2: for each struct registered in pass 1, resolve struct-
   * typed field indices by looking up the field's type-name in the
   * (now complete) registry. Enables `$x.field.subfield` chains. */
  for (uint32_t ni = 0; ni < count; ni++) {
    AstNode* node = nodes[ni];
    if (node->type != AST_DEFSTRUCT) continue;
    /* Find the matching TyperStruct entry. */
    TyperStruct* s = NULL;
    for (uint32_t si = first_added; si < tc->struct_count; si++) {
      if (tc->structs[si].name_len == node->data.defstruct.name_len &&
          memcmp(tc->structs[si].name, node->data.defstruct.name,
                 node->data.defstruct.name_len) == 0) {
        s = &tc->structs[si];
        break;
      }
    }
    if (!s) continue;
    for (uint32_t i = 0; i < s->field_count; i++) {
      if (s->field_types[i] != TYPE_STRUCT) continue;
      const char* tn = node->data.defstruct.field_types[i];
      uint32_t    tl = node->data.defstruct.field_type_lens[i];
      const TyperStruct* found = typer__find_struct(tc, tn, tl);
      if (found) {
        s->field_struct_idxs[i] = (uint32_t)(found - tc->structs);
      }
    }
  }
}

static const TyperStruct* typer__find_struct(TyperCtx* tc,
                                              const char* name, uint32_t name_len) {
  for (uint32_t i = 0; i < tc->struct_count; i++) {
    if (tc->structs[i].name_len == name_len &&
        memcmp(tc->structs[i].name, name, name_len) == 0) {
      return &tc->structs[i];
    }
  }
  return NULL;
}

/* Pre-pass: collect proc signatures from top-level so calls can look
 * them up regardless of definition order. Matches compiler.c's
 * Phase 1 proc registration behavior. */
/* Returns true if the AST subtree contains an AST_COMMAND with HEAD_YIELD,
 * not crossing into nested proc bodies. Used to detect generator procs. */
static bool typer__body_yields(AstNode* node) {
  if (!node) return false;
  if (node->type == AST_COMMAND) {
    AstNode* h = node->data.command.head;
    if (h && h->type == AST_LIT_STRING &&
        node->data.command.head_id == HEAD_YIELD) {
      return true;
    }
    /* Don't descend into nested proc bodies — their yield belongs to them. */
    if (h && h->type == AST_LIT_STRING &&
        node->data.command.head_id == HEAD_PROC) {
      return false;
    }
    if (h && typer__body_yields(h)) return true;
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      if (typer__body_yields(node->data.command.args[i])) return true;
    }
    return false;
  }
  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) {
      if (typer__body_yields(node->data.block.commands[i])) return true;
    }
  }
  return false;
}

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
    uint32_t  return_struct_idx = UINT32_MAX;
    if (argc == 4) {
      AstNode* tn = args[0];
      if (tn->type == AST_LIT_STRING) {
        if (is_type_keyword(tn->data.lit_string.value, tn->data.lit_string.length)) {
          return_type = type_from_keyword(tn->data.lit_string.value, tn->data.lit_string.length);
        } else {
          for (uint32_t si = 0; si < tc->struct_count; si++) {
            if (tc->structs[si].name_len == tn->data.lit_string.length &&
                memcmp(tc->structs[si].name, tn->data.lit_string.value,
                       tn->data.lit_string.length) == 0) {
              return_type = TYPE_STRUCT;
              return_struct_idx = si;
              break;
            }
          }
        }
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

    /* Generator detection: if the body contains a yield (and the user
     * didn't declare a non-DYN return type), the proc returns a stream.
     * Mirrors the compiler's runtime behavior: any yielding proc body
     * is wrapped in a generator that produces a stream value. */
    if (return_type == TYPE_DYN) {
      uint32_t body_idx = (argc == 4) ? 3 : 2;
      if (body_idx < argc && typer__body_yields(args[body_idx])) {
        return_type = TYPE_STREAM;
      }
    }
    p->return_type = (uint8_t)return_type;
    p->return_struct_idx = return_struct_idx;

    AstNode* pn[TYPER_MAX_PROC_PARAMS];
    JaclType pt[TYPER_MAX_PROC_PARAMS];
    uint32_t ps[TYPER_MAX_PROC_PARAMS];
    uint32_t pcount = typer__parse_params(tc, args[params_idx], &pn, &pt, &ps);
    (void)ps;
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

static void typer__infer_command_inner(TyperCtx* tc, AstNode* node);

static void typer__infer_command(TyperCtx* tc, AstNode* node) {
  /* Reset expected_type at command boundaries so sub-expressions don't
   * inherit parent context. Individual handlers (typed def/mut, set,
   * binary ops, proc calls) re-establish it for their own arguments.
   * Restored on exit so the caller's expected_type is preserved.
   * Mirrors compiler.c:5199. */
  JaclType outer_et = tc->expected_type;
  tc->expected_type = TYPE_DYN;
  typer__infer_command_inner(tc, node);
  tc->expected_type = outer_et;
}

static void typer__infer_command_inner(TyperCtx* tc, AstNode* node) {
  AstNode* head = node->data.command.head;

  /* Recognize a few common command shapes. Compiler.c rewrites `::` →
   * set during its compile walk (compiler.c:5556); the typer runs
   * before that rewrite, so it must recognize the sugar form directly.
   * (`=` → def, `:` → mut have different AST shapes — LHS is a typed
   * sub-command — so we handle those only via the keyword forms after
   * the compiler's rewrite. Future: handle the sugar shapes too.)
   * Anything not handled falls through to generic call dispatch. */
  if (head && head->type == AST_LIT_STRING) {
    HeadId hid = (HeadId)node->data.command.head_id;
    if (hid == HEAD_DEF || hid == HEAD_MUT ||
        hid == HEAD_EQUALS || hid == HEAD_COLON) {
      if (typer__handle_def_or_mut(tc, node)) return;
      /* Even if the def/mut shape didn't match a typed handler (e.g.,
       * destructure with non-LIT_STRING name), all def/mut commands
       * return NIL at runtime. Type as NIL here so the typer agrees
       * with the compiler's HEAD_DEF/HEAD_MUT pin. */
      node->inferred_type = TYPE_NIL;
      return;
    } else if (hid == HEAD_SET || hid == HEAD_COLON_COLON) {
      if (typer__handle_set(tc, node)) return;
      node->inferred_type = TYPE_NIL;
      return;
    } else if (hid == HEAD_PROC) {
      if (typer__handle_proc(tc, node)) return;
      /* proc def emits a closure value regardless of which shape was
       * recognized. Pin closure for shapes handle_proc bailed on. */
      node->inferred_type = TYPE_CLOSURE;
      return;
    } else if (hid == HEAD_IF &&
               (node->data.command.arg_count == 2 || node->data.command.arg_count == 3)) {
      /* if [cond] {then} {else?} — detect [box? Type $var] for flow
       * typing. Mirrors compiler.c:7651-7708. */
      AstNode** as = node->data.command.args;
      AstNode* cond = as[0];
      bool pushed = false;
      if (cond->type == AST_COMMAND &&
          cond->data.command.head_id == HEAD_BOX_Q &&
          cond->data.command.arg_count == 2 &&
          cond->data.command.args[1]->type == AST_VAR_REF &&
          tc->narrowing_count < TYPER_MAX_NARROWINGS) {
        AstNode* type_arg = cond->data.command.args[0];
        AstNode* var_node = cond->data.command.args[1];
        JaclType bt = TYPE_DYN;
        uint32_t bsidx = UINT32_MAX;
        if (type_arg->type == AST_LIT_STRING) {
          if (is_type_keyword(type_arg->data.lit_string.value,
                              type_arg->data.lit_string.length)) {
            bt = type_from_keyword(type_arg->data.lit_string.value,
                                   type_arg->data.lit_string.length);
          } else {
            for (uint32_t si = 0; si < tc->struct_count; si++) {
              if (tc->structs[si].name_len == type_arg->data.lit_string.length &&
                  memcmp(tc->structs[si].name, type_arg->data.lit_string.value,
                         type_arg->data.lit_string.length) == 0) {
                bt = TYPE_STRUCT;
                bsidx = si;
                break;
              }
            }
          }
        } else if (type_arg->type == AST_COMMAND) {
          /* [box? [Vec T] $x] / [box? [Map K V] $x] — narrow to typed
           * collection. Look up element struct_idx so post-narrow
           * vec-get/map-get can narrow further to the elem type. */
          int tcoll = typer__typed_collection_kind(type_arg);
          if (tcoll == 1) bt = TYPE_TYPED_VEC;
          else if (tcoll == 2 || tcoll == 3) bt = TYPE_TYPED_MAP;
          if (tcoll == 1 || tcoll == 2 || tcoll == 3) {
            AstNode* elem_node = (tcoll == 3)
                ? type_arg->data.command.args[1]
                : type_arg->data.command.args[0];
            if (elem_node && elem_node->type == AST_LIT_STRING &&
                !is_type_keyword(elem_node->data.lit_string.value,
                                 elem_node->data.lit_string.length)) {
              for (uint32_t si = 0; si < tc->struct_count; si++) {
                if (tc->structs[si].name_len == elem_node->data.lit_string.length &&
                    memcmp(tc->structs[si].name, elem_node->data.lit_string.value,
                           elem_node->data.lit_string.length) == 0) {
                  bsidx = si;
                  break;
                }
              }
            }
          }
        }
        if (bt != TYPE_DYN) {
          tc->narrowings[tc->narrowing_count].name       = var_node->data.var_ref.name;
          tc->narrowings[tc->narrowing_count].name_len   = var_node->data.var_ref.length;
          tc->narrowings[tc->narrowing_count].scope_mark = var_node->scope_mark;
          tc->narrowings[tc->narrowing_count].box_type   = (uint8_t)bt;
          tc->narrowings[tc->narrowing_count].box_struct_idx = bsidx;
          tc->narrowing_count++;
          pushed = true;
        }
      }
      typer__infer_node(tc, cond);
      typer__infer_node(tc, as[1]);
      if (pushed) tc->narrowing_count--;
      if (node->data.command.arg_count == 3) {
        typer__infer_node(tc, as[2]);
      }
      /* Block-result unification: if both branches agree, propagate. */
      JaclType then_t = (JaclType)as[1]->inferred_type;
      JaclType else_t = (node->data.command.arg_count == 3)
                          ? (JaclType)as[2]->inferred_type : TYPE_NIL;
      if (then_t == else_t) {
        node->inferred_type = then_t;
        node->inferred_struct_idx = as[1]->inferred_struct_idx;
      } else {
        node->inferred_type = TYPE_DYN;
      }
      return;
    } else if (hid == HEAD_UNBOX &&
               node->data.command.arg_count == 1 &&
               node->data.command.args[0]->type == AST_VAR_REF) {
      /* [unbox $var] inside a box?-guarded branch — look up the
       * narrowing and adopt its type. */
      AstNode* var_node = node->data.command.args[0];
      typer__infer_node(tc, var_node);
      for (uint32_t ni = 0; ni < tc->narrowing_count; ni++) {
        if (tc->narrowings[ni].name_len == var_node->data.var_ref.length &&
            memcmp(tc->narrowings[ni].name, var_node->data.var_ref.name,
                   var_node->data.var_ref.length) == 0) {
          node->inferred_type = tc->narrowings[ni].box_type;
          node->inferred_struct_idx = tc->narrowings[ni].box_struct_idx;
          return;
        }
      }
      node->inferred_type = TYPE_DYN;
      return;
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
    /* Unary minus: `[- $x]` — result preserves operand's numeric type. */
    if (is_arith && hlen == 1 && hname[0] == '-' &&
        node->data.command.arg_count == 1) {
      AstNode* arg = node->data.command.args[0];
      typer__infer_node(tc, arg);
      JaclType t = (JaclType)arg->inferred_type;
      if (t == TYPE_I32 || t == TYPE_I64 ||
          t == TYPE_F32 || t == TYPE_F64 ||
          t == TYPE_U32 || t == TYPE_U64) {
        node->inferred_type = t;
      } else {
        node->inferred_type = TYPE_DYN;
      }
      return;
    }
  }

  /* Generic call/constructor dispatch: head may be a known proc name
   * or a registered struct name. In both cases we propagate declared
   * arg types as expected_type for literals to narrow. */
  const TyperProc*   proc   = NULL;
  const TyperStruct* sdef   = NULL;
  uint32_t           sdef_idx = UINT32_MAX;
  if (head && head->type == AST_LIT_STRING) {
    proc = typer__find_proc(tc,
        head->data.lit_string.value, head->data.lit_string.length);
    if (!proc) {
      for (uint32_t i = 0; i < tc->struct_count; i++) {
        if (tc->structs[i].name_len == head->data.lit_string.length &&
            memcmp(tc->structs[i].name, head->data.lit_string.value,
                   head->data.lit_string.length) == 0) {
          sdef = &tc->structs[i];
          sdef_idx = i;
          break;
        }
      }
    }
  }
  if (head) typer__infer_node(tc, head);

  /* Typed-collection ctor: propagate elem (and key) types as expected_type
   * so int/float literal args narrow to the declared element type.
   * Mirrors the compiler's c->expected_type = elem_t / key_t before
   * each compile_node call. */
  int ctor_kind = (head && head->type == AST_COMMAND)
                  ? typer__typed_collection_kind(head) : 0;
  JaclType ctor_elem_t = TYPE_DYN;
  JaclType ctor_key_t  = TYPE_DYN;
  if (ctor_kind == 1 || ctor_kind == 2 || ctor_kind == 3) {
    AstNode* elem_node = (ctor_kind == 3)
        ? head->data.command.args[1]
        : head->data.command.args[0];
    if (elem_node && elem_node->type == AST_LIT_STRING &&
        is_type_keyword(elem_node->data.lit_string.value,
                        elem_node->data.lit_string.length)) {
      ctor_elem_t = type_from_keyword(elem_node->data.lit_string.value,
                                      elem_node->data.lit_string.length);
    }
    if (ctor_kind == 3) {
      AstNode* key_node = head->data.command.args[0];
      if (key_node && key_node->type == AST_LIT_STRING &&
          is_type_keyword(key_node->data.lit_string.value,
                          key_node->data.lit_string.length)) {
        ctor_key_t = type_from_keyword(key_node->data.lit_string.value,
                                       key_node->data.lit_string.length);
      }
    }
  }

  for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
    AstNode* arg = node->data.command.args[i];
    JaclType saved_et = tc->expected_type;
    if (proc && i < proc->param_count) {
      tc->expected_type = (JaclType)proc->param_types[i];
    } else if (sdef && i < sdef->field_count) {
      tc->expected_type = (JaclType)sdef->field_types[i];
    } else if (ctor_kind == 1 && ctor_elem_t != TYPE_DYN) {
      tc->expected_type = ctor_elem_t;
    } else if ((ctor_kind == 2 || ctor_kind == 3) &&
               (ctor_elem_t != TYPE_DYN || ctor_key_t != TYPE_DYN)) {
      /* Map ctor: alternating key/value pairs. Even idx → key, odd → val. */
      tc->expected_type = (i % 2 == 0) ? ctor_key_t : ctor_elem_t;
    } else {
      tc->expected_type = TYPE_DYN;
    }
    typer__infer_node(tc, arg);
    tc->expected_type = saved_et;
  }
  if (proc) {
    node->inferred_type = proc->return_type;
    node->inferred_struct_idx = proc->return_struct_idx;
  } else if (sdef) {
    node->inferred_type = TYPE_STRUCT;
    node->inferred_struct_idx = sdef_idx;
  } else if (head && head->type == AST_COMMAND) {
    /* Typed-collection constructor: [[Vec T] e1 ...] / [[Map K V] ...].
     * Mirrors compiler__compile_command's typed-vec/typed-map branch.
     * Element struct_idx is propagated via inferred_struct_idx so
     * vec-get/map-get can narrow the result type. Scalar element types
     * (i32/i64/etc.) use the shared JACL_SCALAR_TYPE_IDX sentinel
     * encoding so the compiler can read the same idx. */
    int tc_kind = typer__typed_collection_kind(head);
    if (tc_kind == 1 || tc_kind == 2 || tc_kind == 3) {
      node->inferred_type = (tc_kind == 1) ? TYPE_TYPED_VEC : TYPE_TYPED_MAP;
      AstNode* elem_node = (tc_kind == 3)
          ? head->data.command.args[1]
          : head->data.command.args[0];
      if (elem_node && elem_node->type == AST_LIT_STRING) {
        const char* nm = elem_node->data.lit_string.value;
        uint32_t    nl = elem_node->data.lit_string.length;
        if (is_type_keyword(nm, nl)) {
          node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
        } else {
          for (uint32_t si = 0; si < tc->struct_count; si++) {
            if (tc->structs[si].name_len == nl &&
                memcmp(tc->structs[si].name, nm, nl) == 0) {
              node->inferred_struct_idx = si;
              break;
            }
          }
        }
      }
    } else {
      node->inferred_type = TYPE_DYN;
    }
  } else if (head && head->type == AST_LIT_STRING) {
    HeadId hid = (HeadId)node->data.command.head_id;
    const char* hn = head->data.lit_string.value;
    uint32_t    hl = head->data.lit_string.length;

    /* Pipe operator: the compiler's compile_pipe_op rewrites
     * `[| lhs rhs]` into a synthetic command that prepends lhs as
     * the first arg of rhs (or wraps rhs as head with lhs as the
     * arg). The result type matches that synthetic call.
     *
     * Special case: rhs is a 1-arg binary-op call like `[* 3]` —
     * after pipe rewrite this becomes `[* lhs 3]` (a binary op).
     * The typer's binary-op rule only fires for arg_count==2, so
     * we replay it here using lhs and rhs's single arg. */
    if (hid == HEAD_PIPE && node->data.command.arg_count == 2) {
      AstNode* lhs = node->data.command.args[0];
      AstNode* rhs = node->data.command.args[1];
      JaclType lhs_t = (JaclType)lhs->inferred_type;
      if (rhs->type == AST_COMMAND && rhs->data.command.arg_count == 1 &&
          rhs->data.command.head &&
          rhs->data.command.head->type == AST_LIT_STRING) {
        const char* hn2 = rhs->data.command.head->data.lit_string.value;
        uint32_t    hl2 = rhs->data.command.head->data.lit_string.length;
        bool is_arith2 = (hl2 == 1 && (hn2[0] == '+' || hn2[0] == '-' ||
                                       hn2[0] == '*' || hn2[0] == '/' ||
                                       hn2[0] == '%'));
        bool is_cmp2 = (hl2 == 1 && (hn2[0] == '<' || hn2[0] == '>')) ||
                       (hl2 == 2 && (memcmp(hn2, "<=", 2) == 0 ||
                                     memcmp(hn2, ">=", 2) == 0 ||
                                     memcmp(hn2, "==", 2) == 0));
        if (is_arith2 || is_cmp2) {
          AstNode* arg = rhs->data.command.args[0];
          JaclType arg_t = (JaclType)arg->inferred_type;
          if (is_cmp2) {
            node->inferred_type = TYPE_BOOL;
          } else if (lhs_t == arg_t && lhs_t != TYPE_DYN) {
            node->inferred_type = lhs_t;
          } else {
            node->inferred_type = TYPE_DYN;
          }
          return;
        }
      }
      node->inferred_type = rhs->inferred_type;
      node->inferred_struct_idx = rhs->inferred_struct_idx;
      return;
    }

    /* Receiver-preserving vec/map builtins. Result depends on whether
     * the typer knows the receiver type:
     *   typed vec/map → typed result (with elem-type idx propagated)
     *   plain vec/map → plain result
     *   DYN          → DYN (compiler's c->last_expr_type fallback wins)
     * Annotating DYN as VEC/MAP would mask a typed receiver that the
     * compiler tracks but the typer does not, leading to silent miscompile. */
    if (node->data.command.arg_count >= 1) {
      AstNode*  recv = node->data.command.args[0];
      JaclType  recv_t = (JaclType)recv->inferred_type;
      switch (hid) {
        case HEAD_VEC_PUSH:   case HEAD_VEC_SET:
        case HEAD_VEC_CONCAT: case HEAD_VEC_SLICE:
          if (recv_t == TYPE_TYPED_VEC) {
            node->inferred_type = TYPE_TYPED_VEC;
            node->inferred_struct_idx = recv->inferred_struct_idx;
          } else if (recv_t == TYPE_VEC) {
            node->inferred_type = TYPE_VEC;
          }
          return;
        case HEAD_MAP_SET: case HEAD_MAP_REMOVE:
          if (recv_t == TYPE_TYPED_MAP) {
            node->inferred_type = TYPE_TYPED_MAP;
            node->inferred_struct_idx = recv->inferred_struct_idx;
          } else if (recv_t == TYPE_MAP) {
            node->inferred_type = TYPE_MAP;
          }
          return;
        case HEAD_MAP_KEYS: case HEAD_MAP_VALS:
          if (recv_t == TYPE_TYPED_MAP) {
            node->inferred_type = TYPE_TYPED_VEC;
            node->inferred_struct_idx = recv->inferred_struct_idx;
          } else if (recv_t == TYPE_MAP) {
            node->inferred_type = TYPE_VEC;
          }
          return;
        case HEAD_VEC_GET:
          /* Element narrowing: typed-vec elem_idx is either a real struct
           * registry index (→ TYPE_STRUCT) or a JACL_SCALAR_TYPE_IDX
           * sentinel (→ that scalar JaclType). */
          if (recv_t == TYPE_TYPED_VEC &&
              recv->inferred_struct_idx != UINT32_MAX) {
            uint32_t eidx = recv->inferred_struct_idx;
            if (JACL_IS_SCALAR_TYPE_IDX(eidx)) {
              node->inferred_type = JACL_TYPE_IDX_TO_SCALAR(eidx);
            } else if (eidx < tc->struct_count) {
              node->inferred_type = TYPE_STRUCT;
              node->inferred_struct_idx = eidx;
            }
          }
          return;
        case HEAD_MAP_GET:
          if (recv_t == TYPE_TYPED_MAP &&
              recv->inferred_struct_idx != UINT32_MAX) {
            uint32_t eidx = recv->inferred_struct_idx;
            if (JACL_IS_SCALAR_TYPE_IDX(eidx)) {
              node->inferred_type = JACL_TYPE_IDX_TO_SCALAR(eidx);
            } else if (eidx < tc->struct_count) {
              node->inferred_type = TYPE_STRUCT;
              node->inferred_struct_idx = eidx;
            }
          }
          return;
        case HEAD_RESET:
          /* reset on struct-box → returns the new struct bytes (TOS).
           * reset on plain box → returns NIL. */
          if (node->data.command.arg_count == 2) {
            AstNode* val = node->data.command.args[1];
            if ((JaclType)val->inferred_type == TYPE_STRUCT &&
                val->inferred_struct_idx != UINT32_MAX) {
              node->inferred_type = TYPE_STRUCT;
              node->inferred_struct_idx = val->inferred_struct_idx;
            } else {
              node->inferred_type = TYPE_NIL;
            }
          } else {
            node->inferred_type = TYPE_NIL;
          }
          return;
        case HEAD_FILTER:
          /* filter preserves the receiver's collection type, including
           * elem struct_idx. Mirrors compiler__compile_hof_builtin: typed
           * receiver → typed result; plain → plain; stream → stream. */
          if (recv_t == TYPE_TYPED_VEC || recv_t == TYPE_TYPED_MAP ||
              recv_t == TYPE_VEC || recv_t == TYPE_MAP ||
              recv_t == TYPE_STREAM) {
            node->inferred_type = recv_t;
            node->inferred_struct_idx = recv->inferred_struct_idx;
          }
          return;
        case HEAD_TRANSFORM:
          /* transform on typed_vec → plain TYPE_VEC (loses elem typing).
           * On plain receivers preserves the receiver type (vec/stream).
           * Mirrors compiler__compile_hof_builtin. */
          if (recv_t == TYPE_TYPED_VEC || recv_t == TYPE_TYPED_MAP) {
            node->inferred_type = TYPE_VEC;
          } else if (recv_t == TYPE_VEC || recv_t == TYPE_MAP ||
                     recv_t == TYPE_STREAM) {
            node->inferred_type = recv_t;
          }
          return;
        default: break;
      }
    }

    /* Fixed-return table: builtins where the compiler's typed and untyped
     * paths agree on the result. Vec/map mutations on typed receivers are
     * handled above; the entries below cover the dyn-receiver path. */
    static const struct { HeadId hid; uint8_t ret; } fixed_returns[] = {
      /* Predicates and short-circuit logicals — always bool. */
      { HEAD_ATOM_Q,      TYPE_BOOL   },
      { HEAD_FUTURE_Q,    TYPE_BOOL   },
      { HEAD_ERROR_Q,     TYPE_BOOL   },
      { HEAD_BOX_Q,       TYPE_BOOL   },
      { HEAD_MAP_HAS,     TYPE_BOOL   },
      { HEAD_AMP_AMP,     TYPE_BOOL   },
      { HEAD_PIPE_PIPE,   TYPE_BOOL   },
      { HEAD_TILDE,       TYPE_BOOL   },
      /* Length-style builtins — always i32 (typed and untyped). */
      { HEAD_LENGTH,      TYPE_I32    },
      { HEAD_BYTE_LENGTH, TYPE_I32    },
      { HEAD_COUNT,       TYPE_I32    },
      { HEAD_VEC_LEN,     TYPE_I32    },
      { HEAD_MAP_LEN,     TYPE_I32    },
      /* String results. */
      { HEAD_TO_STRING,   TYPE_STR    },
      { HEAD_SLICE,       TYPE_STR    },
      { HEAD_CONCAT,      TYPE_STR    },
      /* Stream constructors. */
      { HEAD_DOTDOT_LT,   TYPE_STREAM },
      { HEAD_DOTDOT_EQ,   TYPE_STREAM },
      { HEAD_LINES,       TYPE_STREAM },
      /* Constructors that always produce dyn collections. */
      { HEAD_VEC,         TYPE_VEC    },
      { HEAD_MAP,         TYPE_MAP    },
      { HEAD_COLLECT,     TYPE_VEC    },
      /* Side-effecting — always nil. */
      { HEAD_PRINT,       TYPE_NIL    },
      /* Loop forms — emit OP_NIL at normal exit. break-with-value
       * paths could carry a different type but are conservatively
       * unified to nil here; refine in a later commit if needed. */
      { HEAD_WHILE,       TYPE_NIL    },
      { HEAD_FOR,         TYPE_NIL    },
      /* Yield — pushes nil after resume in current SM compilation. */
      { HEAD_YIELD,       TYPE_NIL    },
      /* Job control — bool indicates delivered/cancelled. */
      { HEAD_SIGNAL,      TYPE_BOOL   },
      { HEAD_CANCEL,      TYPE_BOOL   },
      /* Syntax-object introspection (US-015) — fixed result types. */
      { HEAD_SYNTAX_KIND,     TYPE_STR },
      { HEAD_SYNTAX_ARGS,     TYPE_VEC },
      { HEAD_SYNTAX_COMMANDS, TYPE_VEC },
      { HEAD_SYNTAX_POS,      TYPE_MAP },
      { HEAD_SYNTAX_STR,      TYPE_STR },
    };
    bool matched = false;
    for (size_t fi = 0; fi < sizeof(fixed_returns)/sizeof(fixed_returns[0]); fi++) {
      if (hid == fixed_returns[fi].hid) {
        node->inferred_type = fixed_returns[fi].ret;
        matched = true;
        break;
      }
    }
    if (matched) {
      /* handled */
    } else if (hl == 4 && memcmp(hn, "puts", 4) == 0) {
      /* "puts" is not in the HeadId table — keep the memcmp here. */
      node->inferred_type = TYPE_NIL;
    } else if (hid == HEAD_TO &&
               node->data.command.arg_count >= 1 &&
               node->data.command.args[0]->type == AST_LIT_STRING &&
               is_type_keyword(node->data.command.args[0]->data.lit_string.value,
                               node->data.command.args[0]->data.lit_string.length)) {
      /* [to TYPE expr] — the result type is the keyword. */
      node->inferred_type =
          type_from_keyword(node->data.command.args[0]->data.lit_string.value,
                            node->data.command.args[0]->data.lit_string.length);
    } else if (hid == HEAD_DOT &&
               node->data.command.arg_count == 3) {
      /* [. struct field new_value] field-set — emits OP_HEAP_RECORD_SET,
       * leaves nil. Mirrors compiler.c's set path. */
      node->inferred_type = TYPE_NIL;
    } else if (hid == HEAD_DOT &&
               node->data.command.arg_count == 2) {
      /* [. struct field] arrow access — result type is the accessed
       * field's declared type. For struct-typed fields, propagate
       * inferred_struct_idx so chained access (`$x.field.subfield`)
       * resolves the subfield's type. */
      AstNode* tgt = node->data.command.args[0];
      AstNode* fld = node->data.command.args[1];
      if (tgt->inferred_type == TYPE_STRUCT &&
          tgt->inferred_struct_idx < tc->struct_count &&
          fld->type == AST_LIT_STRING) {
        const TyperStruct* sd = &tc->structs[tgt->inferred_struct_idx];
        const char* fn = fld->data.lit_string.value;
        uint32_t    fnl = fld->data.lit_string.length;
        node->inferred_type = TYPE_DYN;
        for (uint32_t fi = 0; fi < sd->field_count; fi++) {
          if (sd->field_name_lens[fi] == fnl &&
              memcmp(sd->field_names[fi], fn, fnl) == 0) {
            node->inferred_type = (uint8_t)sd->field_types[fi];
            if (sd->field_types[fi] == TYPE_STRUCT) {
              node->inferred_struct_idx = sd->field_struct_idxs[fi];
            }
            break;
          }
        }
      } else {
        node->inferred_type = TYPE_DYN;
      }
    } else {
      node->inferred_type = TYPE_DYN;
    }
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
      /* Foreground shell command (`!cmd`) compiles to OP_EXEC and
       * returns a stream. Background (`!cmd &`) returns a Job map
       * (typed dyn). The compiler also downgrades to a custom-exec
       * closure call (returns DYN) when prelude provides a non-native
       * `exec`; we don't try to detect that — leave such cases as the
       * residual EXTRA in the audit. */
      node->inferred_type =
          node->data.shell_cmd.background ? TYPE_DYN : TYPE_STREAM;
      break;
    case AST_CTX_DECL: {
      /* ctx [mut] Type name = default_expr — recurse into default_expr
       * with declared type as expected_type so int/float literals
       * narrow correctly (mirrors compiler.c's ctx-decl handling).
       * The compiler leaves last_expr_type as the value's type after
       * emitting, so propagate that here for dual-track agreement. */
      JaclType declared = TYPE_DYN;
      if (node->data.ctx_decl.type_name) {
        declared = type_from_keyword(node->data.ctx_decl.type_name,
                                     node->data.ctx_decl.type_name_len);
      }
      if (node->data.ctx_decl.default_expr) {
        JaclType saved_et = tc->expected_type;
        tc->expected_type = declared;
        typer__infer_node(tc, node->data.ctx_decl.default_expr);
        tc->expected_type = saved_et;
        node->inferred_type = node->data.ctx_decl.default_expr->inferred_type;
        node->inferred_struct_idx = node->data.ctx_decl.default_expr->inferred_struct_idx;
      } else {
        node->inferred_type = TYPE_NIL;
      }
      break;
    }
    case AST_DEFMACRO:
      /* Recurse into the body so any literals/var-refs inside get
       * default types. The typer doesn't otherwise track macro semantics. */
      if (node->data.defmacro.body) {
        typer__infer_node(tc, node->data.defmacro.body);
      }
      node->inferred_type = TYPE_DYN;
      break;
    case AST_CONTINUE:
      /* continue is non-returning control flow; its compiled form
       * leaves nothing meaningful on stack. The compiler reports nil. */
      node->inferred_type = TYPE_NIL;
      break;
    case AST_USE:
    case AST_DEFSTRUCT:
    case AST_DESTRUCTURE_VEC:
    case AST_DESTRUCTURE_NAMED:
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
  /* Reserve struct indices 0 and 1 so typer indices align with the
   * compiler's StructTypeRegistry: slot 0 is "dyn placeholder", slot
   * 1 is reserved for the ctx struct (see compiler.c
   * struct_registry__init). User structs start at slot 2.
   * Without this alignment, typer would report different struct_idx
   * values than the compiler for the same Point/Line/etc. */
  tc.struct_count  = 2;
  memset(&tc.structs[0], 0, sizeof(tc.structs[0]) * 2);
  tc.expected_type = TYPE_DYN;
  tc.narrowing_count = 0;

  /* Pre-pass: register top-level struct definitions, the synthetic
   * ctx struct, and proc signatures so constructor calls and proc
   * calls (which may appear before the definition) resolve correctly.
   *
   * Imported names from `use "path" {Name1, Name2}` are registered as
   * placeholder structs (no fields) so `[Name ...]` constructor calls
   * type as TYPE_STRUCT. Field access and proc-call narrowing for
   * imported names stay DYN (we don't load the imported file), but
   * the compiler's own struct registry has the real fields, so field
   * access compiles correctly via the compiler's path. */
  for (uint32_t ni = 0; ni < count; ni++) {
    AstNode* node = nodes[ni];
    if (node->type != AST_USE) continue;
    for (uint32_t i = 0; i < node->data.use_decl.name_count; i++) {
      if (tc.struct_count >= TYPER_MAX_STRUCTS) break;
      const char* nm = node->data.use_decl.names[i];
      uint32_t    nl = node->data.use_decl.name_lens[i];
      /* Heuristic: only CapitalCase names are likely struct types.
       * lowercase imports are treated as procs (typer leaves DYN; the
       * compiler resolves them via its own proc registry). */
      if (nl == 0 || !(nm[0] >= 'A' && nm[0] <= 'Z')) continue;
      bool dup = false;
      for (uint32_t si = 0; si < tc.struct_count; si++) {
        if (tc.structs[si].name_len == nl &&
            memcmp(tc.structs[si].name, nm, nl) == 0) { dup = true; break; }
      }
      if (dup) continue;
      TyperStruct* s = &tc.structs[tc.struct_count++];
      s->name        = nm;
      s->name_len    = nl;
      s->field_count = 0;
    }
  }
  typer__register_structs(&tc, nodes, count);
  uint32_t ctx_struct_idx = typer__register_ctx_struct(&tc, nodes, count);

  /* Builtin: $ctx is always a struct (the ctx record). Bind it after
   * the ctx struct is registered so $ctx.field resolves through the
   * existing HEAD_DOT field-type path. If no ctx fields were declared,
   * fall back to UINT32_MAX (no struct registry entry). */
  typer__scope_add(&tc, "ctx", 3, 0, TYPE_STRUCT, ctx_struct_idx);

  typer__register_procs(&tc, nodes, count);

  for (uint32_t i = 0; i < count; i++) {
    typer__infer_node(&tc, nodes[i]);
  }
}

#endif /* TYPER_C */
