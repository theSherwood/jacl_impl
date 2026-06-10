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

/* Captured first error from a typer pass; passed back to callers (mainly
 * compiler.c) so compilation can fail with a type-error message that
 * was detected during typing rather than codegen. The buffer is inline
 * so no arena allocation is needed at the typer boundary; the caller
 * copies into its own arena-backed error storage if it needs the
 * message to outlive the typer pass. */
typedef struct TyperResult {
  uint32_t error_count;
  uint32_t first_error_line;
  uint32_t first_error_col;
  char     first_error[256];   /* "" when error_count == 0 */
} TyperResult;

typedef struct {
  const char* name;
  uint32_t    name_len;
  uint32_t    scope_mark;       /* hygiene mark from binding's AST node */
  uint8_t     type;             /* JaclType */
  uint32_t    struct_idx;       /* UINT32_MAX if not a struct/typed-collection;
                                 * for typed-vec/map: element idx */
  uint32_t    key_struct_idx;   /* TYPE_TYPED_MAP key idx; UINT32_MAX otherwise */
  uint32_t    buf_len;          /* TYPE_BUF N; 0 otherwise. See BUFFER_DESIGN.md */
  uint32_t    buf_inner_len;    /* TYPE_BUF M for nested [Buf N [Buf M T]]; 0 for scalar/struct element. M4.2 */
  uint32_t    scope_depth;      /* depth at which this binding was pushed */
  /* Typed-closure signature (TYPE_CLOSURE bound via a [Proc [P…] R]
   * annotation; TYPED_CLOSURES_DESIGN.md Phase B). is_typed_closure gates a
   * proxy at the call site so `[f args]` narrows to the declared return and
   * checks arg types — mirroring a named proc's TyperProc. */
  bool        is_typed_closure;
  uint8_t     proc_param_count;
  uint8_t     proc_param_types[TYPER_MAX_PROC_PARAMS];
  uint8_t     proc_return_type;
  uint32_t    proc_return_struct_idx;
} TyperBinding;

typedef struct {
  const char* name;
  uint32_t    name_len;
  uint8_t     return_type;          /* JaclType */
  uint32_t    return_struct_idx;    /* UINT32_MAX if not struct */
  uint8_t     param_count;
  uint8_t     param_types[TYPER_MAX_PROC_PARAMS];
  /* For TYPE_PTR / TYPE_STRUCT params, the pointee/struct idx
   * (scalar-encoded via JACL_SCALAR_TYPE_IDX or real struct registry
   * idx). UINT32_MAX otherwise. Used by extern call sites to verify
   * [Buf N T] → [Ptr T] decay matches the param's declared pointee
   * exactly. See BUFFER_DESIGN.md M3. */
  uint32_t    param_struct_idxs[TYPER_MAX_PROC_PARAMS];
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

/* Imported entity from an AST_USE — see jacl.h forward declaration.
 * The compiler builds an array of these from each AST_USE's
 * resolved exports and hands the array to the typer.
 *
 * The struct carries both procs and value imports (def/mut). The
 * `arity` field disambiguates: arity >= 0 → callable proc (param
 * info in param_types); arity < 0 → static value (return_type is
 * the value's type, param_types unused).
 *
 * Two binding flavors share this struct:
 *   - Top-level destructuring (`use "path" {names}`): `binding == NULL`.
 *     The pre-pass registers callable entries as TyperProcs in
 *     tc.procs and value entries as TyperBindings.
 *   - Module-binding (`use "path" name` then `[$name->fn]` or
 *     `$name->field`): `binding` set to the binding name. Resolved
 *     at use sites by typer__find_bound_export when an AST_COMMAND
 *     head or HEAD_DOT 2-arg expression names that binding. */
typedef struct TyperImportProc {
  const char* name;
  uint32_t    name_len;
  uint8_t     return_type;
  uint8_t     param_types[TYPER_MAX_PROC_PARAMS];
  uint8_t     param_count;
  int16_t     arity;          /* >= 0: callable proc; < 0: static value */
  const char* binding;        /* NULL for top-level destructuring imports */
  uint32_t    binding_len;
} TyperImportProc;

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
  /* Element-type idx of the enclosing generator's declared [Stream T]
   * return annotation. UINT32_MAX means either "not inside a generator"
   * or "[Stream dyn] / no annotation" — in both cases yield is lenient.
   * Saved on the stack across typer__handle_proc so nested procs don't
   * leak through. Encoded the same way as inferred_struct_idx on a
   * stream-valued node (JACL_SCALAR_TYPE_IDX sentinel for scalar
   * elements, real struct registry idx for struct elements). */
  uint32_t     yield_elem_struct_idx;
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
  /* Imports array, kept alive by the caller for the duration of the
   * walk. Only entries with binding != NULL are read post-pre-pass —
   * those represent module-binding form imports
   * (`use "lib" m` → `[$m->fn]`) and are looked up by
   * typer__find_bound_export when a HEAD_DOT command resolves to a
   * binding. Top-level destructuring entries (binding == NULL) are
   * already copied into tc->procs by the pre-pass. */
  TyperImportProc*        imports;
  uint32_t                import_count;
  /* First-error capture. NULL when the caller doesn't want typer
   * errors (syntax.c macro-body pre-typing, test_typer harness).
   * typer__error is a no-op when result is NULL. */
  TyperResult* result;
  /* Type-shape registry for typed-vec / typed-map shapes encountered
   * during typing. Bindings store a registry idx in struct_idx for
   * typed-collection buf-element types; narrowing in HEAD_BUF_GET
   * reads the shape entry to recover K / V. Separate from the
   * compiler's runtime StructTypeRegistry -- both maintain their own,
   * indexed differently but representing the same shapes. Lifetime is
   * bounded to typer_infer; freed at the end. See
   * docs/TYPE_REGISTRY_REFACTOR.md. */
  StructTypeRegistry shape_reg;
} TyperCtx;

/* Record a type error. Stores location + message of the first error;
 * later errors only bump the count. The caller (compiler.c) copies
 * the captured message into its own arena-backed reporting before
 * compilation continues. No-op when tc->result is NULL. */
static void typer__error(TyperCtx* tc, uint32_t line, uint32_t col,
                         const char* msg) {
  if (!tc->result) return;
  tc->result->error_count++;
  if (tc->result->first_error[0] == '\0') {
    tc->result->first_error_line = line;
    tc->result->first_error_col  = col;
    size_t cap = sizeof(tc->result->first_error);
    size_t len = strlen(msg);
    if (len >= cap) len = cap - 1;
    memcpy(tc->result->first_error, msg, len);
    tc->result->first_error[len] = '\0';
  }
}

static void typer__infer_node(TyperCtx* tc, AstNode* node);
static const TyperStruct* typer__find_struct(TyperCtx* tc, const char* name, uint32_t name_len);
static bool typer__body_yields(AstNode* node);
static uint32_t typer__register_inline_struct(TyperCtx* tc,
                                               const char* spec, uint32_t spec_len);

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
  b->name           = name;
  b->name_len       = name_len;
  b->scope_mark     = scope_mark;
  b->type           = type;
  b->struct_idx     = struct_idx;
  b->key_struct_idx = UINT32_MAX;
  b->buf_len        = 0;
  b->buf_inner_len  = 0;
  b->scope_depth    = tc->scope_depth;
  b->is_typed_closure = false;
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

/* Mirror of compiler__is_typed_collection_scalar — the JaclTypes valid
 * as scalar element/key/value types in [Vec T] / [Map T] / [Map K V]
 * constructors. Used by the typer's element-type checks so we only
 * fire when the declared scalar type is actually a supported one;
 * otherwise we leave the error to the compiler's separate
 * "only value-type scalars supported" diagnostic. */
static bool typer__is_typed_collection_scalar(JaclType t) {
  return t == TYPE_I32 || t == TYPE_I64 || t == TYPE_U32 || t == TYPE_U64 ||
         t == TYPE_F32 || t == TYPE_F64;
}

/* Recognize [Future T] type-annotation expressions. Returns true and
 * writes *out_struct_idx with the element type encoding (scalar
 * sentinel for type keywords, real struct idx for struct names) when
 * the node is a valid [Future T]. Used by typer__handle_def_or_mut
 * and typer__parse_params to set the binding's TYPE_FUTURE element
 * type. The compiler doesn't need a parallel recognizer because
 * futures don't have a typed-constructor surface form like
 * [[Vec T] e1 e2 ...] — the only producer is `spawn`. */
static bool typer__future_type(TyperCtx* tc, AstNode* node,
                               uint32_t* out_struct_idx) {
  *out_struct_idx = UINT32_MAX;
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 6 ||
      memcmp(h->data.lit_string.value, "Future", 6) != 0) return false;
  if (node->data.command.arg_count != 1) return false;
  AstNode* arg = node->data.command.args[0];
  if (arg->type != AST_LIT_STRING) return false;
  const char* nm = arg->data.lit_string.value;
  uint32_t    nl = arg->data.lit_string.length;
  if (is_type_keyword(nm, nl)) {
    *out_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
    return true;
  }
  for (uint32_t si = 0; si < tc->struct_count; si++) {
    if (tc->structs[si].name_len == nl &&
        memcmp(tc->structs[si].name, nm, nl) == 0) {
      *out_struct_idx = si;
      return true;
    }
  }
  /* Unknown type name — still recognize as Future so the compiler
   * can backstop the unknown-type error. Element idx stays sentinel. */
  return true;
}

/* Recognize [Box T] type-annotation expressions. Returns true and writes
 * *out_struct_idx with the boxed-value encoding (scalar sentinel for
 * type keywords like i64, real struct idx for Point). Used wherever a
 * [Box T] annotation can appear (def/mut bindings, proc params/return,
 * and as a [Buf N [Box T]] element type). Same shape as
 * typer__future_type — single type-name argument. */
static bool typer__box_type(TyperCtx* tc, AstNode* node,
                            uint32_t* out_struct_idx) {
  *out_struct_idx = UINT32_MAX;
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 3 ||
      memcmp(h->data.lit_string.value, "Box", 3) != 0) return false;
  if (node->data.command.arg_count != 1) return false;
  AstNode* arg = node->data.command.args[0];
  if (arg->type != AST_LIT_STRING) return false;
  const char* nm = arg->data.lit_string.value;
  uint32_t    nl = arg->data.lit_string.length;
  if (is_type_keyword(nm, nl)) {
    *out_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
    return true;
  }
  for (uint32_t si = 0; si < tc->struct_count; si++) {
    if (tc->structs[si].name_len == nl &&
        memcmp(tc->structs[si].name, nm, nl) == 0) {
      *out_struct_idx = si;
      return true;
    }
  }
  /* Unknown type name — still recognize as Box; element idx stays
   * sentinel so downstream code treats it as box-of-dyn. */
  return true;
}

/* Recognize [Arr T] type-annotation expressions (mutable array). Returns
 * true and writes *out_elem_idx with the element encoding (scalar sentinel
 * for type keywords like i32, real struct idx for Point). Same shape as
 * typer__box_type. See ARR_DESIGN.md. */
static bool typer__arr_type(TyperCtx* tc, AstNode* node,
                            uint32_t* out_elem_idx) {
  *out_elem_idx = UINT32_MAX;
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 3 ||
      memcmp(h->data.lit_string.value, "Arr", 3) != 0) return false;
  if (node->data.command.arg_count != 1) return false;
  AstNode* arg = node->data.command.args[0];
  if (arg->type != AST_LIT_STRING) return false;
  const char* nm = arg->data.lit_string.value;
  uint32_t    nl = arg->data.lit_string.length;
  if (is_type_keyword(nm, nl)) {
    *out_elem_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
    return true;
  }
  for (uint32_t si = 0; si < tc->struct_count; si++) {
    if (tc->structs[si].name_len == nl &&
        memcmp(tc->structs[si].name, nm, nl) == 0) {
      *out_elem_idx = si;
      return true;
    }
  }
  /* Unknown element type name — still recognize as Arr; elem idx stays
   * sentinel so downstream treats it as arr-of-dyn. */
  return true;
}

/* Recognize [Stream T] type-annotation expressions. Returns true and
 * writes *out_struct_idx with the element encoding (scalar sentinel
 * for type keywords like i64, real struct idx for Point). Used on the
 * proc return-type position so generator return narrows from a bare
 * TYPE_STREAM to a stream-of-T. Same shape as typer__future_type —
 * single type-name argument. */
static bool typer__stream_type(TyperCtx* tc, AstNode* node,
                               uint32_t* out_struct_idx) {
  *out_struct_idx = UINT32_MAX;
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 6 ||
      memcmp(h->data.lit_string.value, "Stream", 6) != 0) return false;
  if (node->data.command.arg_count != 1) return false;
  AstNode* arg = node->data.command.args[0];
  if (arg->type != AST_LIT_STRING) return false;
  const char* nm = arg->data.lit_string.value;
  uint32_t    nl = arg->data.lit_string.length;
  if (nl == 3 && memcmp(nm, "dyn", 3) == 0) {
    /* Explicit [Stream dyn] — element idx stays UINT32_MAX, same as
     * an unannotated generator. Recognized so the syntax is permitted
     * without falling through to the "unknown type name" backstop. */
    return true;
  }
  if (is_type_keyword(nm, nl)) {
    *out_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
    return true;
  }
  for (uint32_t si = 0; si < tc->struct_count; si++) {
    if (tc->structs[si].name_len == nl &&
        memcmp(tc->structs[si].name, nm, nl) == 0) {
      *out_struct_idx = si;
      return true;
    }
  }
  /* Unknown element — still recognize as Stream; element idx stays
   * sentinel so downstream consumers treat it as [Stream dyn]. */
  return true;
}

/* Resolve one type-name node inside a [Proc …] signature to a portable
 * encoding. Scalar keywords (incl. `dyn`) are supported; struct names and
 * compound types ([Vec T], nested [Proc …], …) are Phase B3 — recognized as
 * a closure type but reported unsupported so the caller can error rather than
 * silently bind a rep-mismatched closure. Returns the JaclType; *out_idx is a
 * scalar sentinel (JACL_SCALAR_TYPE_IDX) and *out_supported reflects support. */
static JaclType typer__proc_sig_elem(TyperCtx* tc, AstNode* n,
                                     uint32_t* out_idx, bool* out_supported) {
  (void)tc;
  *out_idx = UINT32_MAX;
  *out_supported = false;
  if (!n || n->type != AST_LIT_STRING) return TYPE_DYN;  /* compound → B3 */
  const char* nm = n->data.lit_string.value;
  uint32_t    nl = n->data.lit_string.length;
  if (nl == 3 && memcmp(nm, "dyn", 3) == 0) {
    *out_supported = true;
    return TYPE_DYN;  /* dyn is an ordinary type (TYPED_CLOSURES_DESIGN §2.5) */
  }
  if (is_type_keyword(nm, nl)) {
    JaclType t = type_from_keyword(nm, nl);
    *out_idx = JACL_SCALAR_TYPE_IDX(t);
    *out_supported = true;
    return t;
  }
  return TYPE_DYN;  /* struct name → B3 */
}

/* Recognize a [Proc [P…] R] type-annotation expression (TYPED_CLOSURES_DESIGN
 * §3). Layout: head "Proc"; args[0] is the bracketed PARAM LIST (an
 * AST_COMMAND — `[]`, `[i64]`, `[i64 i64]`); optional args[1] is the RETURN
 * type (bare keyword), defaulting to nil. Returns true iff the node is a
 * [Proc …] shape (so the caller binds a TYPE_CLOSURE). Fills the param/return
 * signature; *out_supported is false when any param/return is a struct or
 * compound type (Phase B3) so the caller can emit a "not yet supported" error
 * instead of silently degrading. Param names are not permitted — the list is
 * a list of TYPES (positional calls), enforced by requiring lit-string types. */
static bool typer__proc_type(TyperCtx* tc, AstNode* node,
                             uint8_t* out_param_count,
                             JaclType* out_param_types,
                             uint32_t* out_param_struct_idxs,
                             JaclType* out_return_type,
                             uint32_t* out_return_struct_idx,
                             bool* out_supported) {
  *out_param_count = 0;
  *out_return_type = TYPE_DYN;  /* no declared return ⇒ dyn (see compiler mirror) */
  *out_return_struct_idx = UINT32_MAX;
  *out_supported = true;
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 4 ||
      memcmp(h->data.lit_string.value, "Proc", 4) != 0) return false;
  uint32_t ac = node->data.command.arg_count;
  if (ac != 1 && ac != 2) { *out_supported = false; return true; } /* malformed */
  AstNode* plist = node->data.command.args[0];
  if (!plist || plist->type != AST_COMMAND) { *out_supported = false; return true; }
  /* Collect param type nodes: [head] + args, skipping the empty-`[]` head. */
  AstNode* phead = plist->data.command.head;
  bool empty_head = (phead && phead->type == AST_LIT_STRING &&
                     phead->data.lit_string.length == 0);
  uint8_t pc = 0;
  if (phead && !empty_head) {
    if (pc >= TYPER_MAX_PROC_PARAMS) { *out_supported = false; return true; }
    bool sup; uint32_t idx;
    out_param_types[pc] = typer__proc_sig_elem(tc, phead, &idx, &sup);
    out_param_struct_idxs[pc] = idx;
    if (!sup) *out_supported = false;
    pc++;
  }
  for (uint32_t i = 0; i < plist->data.command.arg_count; i++) {
    if (pc >= TYPER_MAX_PROC_PARAMS) { *out_supported = false; return true; }
    bool sup; uint32_t idx;
    out_param_types[pc] = typer__proc_sig_elem(tc, plist->data.command.args[i],
                                               &idx, &sup);
    out_param_struct_idxs[pc] = idx;
    if (!sup) *out_supported = false;
    pc++;
  }
  *out_param_count = pc;
  if (ac == 2) {
    bool sup; uint32_t idx;
    *out_return_type = typer__proc_sig_elem(tc, node->data.command.args[1],
                                            &idx, &sup);
    *out_return_struct_idx = idx;
    if (!sup) *out_supported = false;
  }
  return true;
}

/* Recognize [Ptr T] type-annotation expressions. Returns true and writes
 * *out_struct_idx with the pointee encoding (scalar sentinel for type
 * keywords like *i32, real struct idx for *Point). Used everywhere a
 * [Ptr T] annotation appears (def/mut/set, proc params/return, extern).
 * Mirrors typer__future_type's shape — single type-name argument. */
/* Forward declaration: defined further down. Used here to support
 * [Ptr [Buf N T]] slice-passing param types -- the inner buf shape is
 * interned via this helper. */
static bool typer__buf_type_full(TyperCtx* tc, AstNode* node,
                                 uint32_t* out_struct_idx, uint32_t* out_len,
                                 uint32_t* out_inner_len,
                                 uint32_t* out_typed_elem_idx);
static bool typer__ptr_type(TyperCtx* tc, AstNode* node,
                            uint32_t* out_struct_idx) {
  *out_struct_idx = UINT32_MAX;
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 3 ||
      memcmp(h->data.lit_string.value, "Ptr", 3) != 0) return false;
  if (node->data.command.arg_count != 1) return false;
  AstNode* arg = node->data.command.args[0];
  if (arg->type == AST_LIT_STRING) {
    const char* nm = arg->data.lit_string.value;
    uint32_t    nl = arg->data.lit_string.length;
    if (is_type_keyword(nm, nl)) {
      *out_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
      return true;
    }
    for (uint32_t si = 0; si < tc->struct_count; si++) {
      if (tc->structs[si].name_len == nl &&
          memcmp(tc->structs[si].name, nm, nl) == 0) {
        *out_struct_idx = si;
        return true;
      }
    }
    /* Unknown pointee name — still recognize as Ptr. */
    return true;
  }
  /* [Ptr [Buf N T]] -- slice passing across proc boundaries. Intern
   * the inner [Buf N T] as a TYPE_SHAPE_BUF in the typer's registry
   * (including the outer N) and use its idx as the pointee. */
  if (arg->type == AST_COMMAND && arg->data.command.head &&
      arg->data.command.head->type == AST_LIT_STRING &&
      arg->data.command.head->data.lit_string.length == 3 &&
      memcmp(arg->data.command.head->data.lit_string.value, "Buf", 3) == 0) {
    uint32_t inner_sidx;
    uint32_t inner_len;
    uint32_t inner_inner_len = 0;
    if (typer__buf_type_full(tc, arg, &inner_sidx, &inner_len,
                             &inner_inner_len, NULL) &&
        inner_len > 0 && inner_sidx != UINT32_MAX) {
      uint32_t shape_idx = type_shape_intern_buf(&tc->shape_reg,
                                                  inner_len, inner_sidx);
      if (shape_idx != UINT32_MAX) {
        *out_struct_idx = shape_idx;
        return true;
      }
    }
    /* Recognized as Ptr but inner buf shape malformed -- still a Ptr. */
    return true;
  }
  return true;
}

/* Recognize [Buf N T] type-annotation expressions. Returns true and
 * writes *out_struct_idx with the element encoding (scalar sentinel
 * via JACL_SCALAR_TYPE_IDX, or a real struct registry idx for struct
 * elements) and *out_len with N. Returns false if the node isn't a
 * [Buf ...] shape at all.
 *
 * Validation errors (zero/negative N, non-literal N, unrecognized T)
 * still return true (recognized as a buf annotation) but write
 * sentinel values so the caller can emit a precise diagnostic:
 *   - *out_len = 0 means "N not a positive int literal"
 *   - *out_struct_idx = UINT32_MAX means "T not a recognized scalar
 *     keyword or struct name"
 *
 * See BUFFER_DESIGN.md M1 / M4.1. */
/* Decode a typer-side buf-element encoding. Three ranges:
 *   - scalar sentinel (>= JACL_SCALAR_VEC_BASE): primitive elem.
 *   - shape idx (>= TYPER_MAX_STRUCTS, < JACL_SCALAR_VEC_BASE):
 *     typed-vec / typed-map shape from tc->shape_reg.
 *   - struct idx (< TYPER_MAX_STRUCTS): user struct from tc->structs.
 *
 * Writes V and K's encodings via the out params for typed-collection
 * shapes (out_inner_value_idx, out_inner_key_idx). For struct/scalar
 * elements they're left UINT32_MAX. */
static JaclType typer__buf_elem_decode(TyperCtx* tc, uint32_t encoded,
                                        uint32_t* out_inner_value_idx,
                                        uint32_t* out_inner_key_idx) {
  if (out_inner_value_idx) *out_inner_value_idx = UINT32_MAX;
  if (out_inner_key_idx)   *out_inner_key_idx   = UINT32_MAX;
  if (encoded == UINT32_MAX) return TYPE_DYN;
  if (JACL_IS_SCALAR_TYPE_IDX(encoded)) {
    return JACL_TYPE_IDX_TO_SCALAR(encoded);
  }
  if (encoded >= TYPER_MAX_STRUCTS && encoded < tc->shape_reg.count) {
    TypeShape* shape = &tc->shape_reg.shapes[encoded];
    switch (shape->kind) {
      case TYPE_SHAPE_TYPED_VEC:
        if (out_inner_value_idx) *out_inner_value_idx = shape->u.tvec.elem_idx;
        return TYPE_TYPED_VEC;
      case TYPE_SHAPE_TYPED_MAP:
        if (out_inner_value_idx) *out_inner_value_idx = shape->u.tmap.value_idx;
        if (out_inner_key_idx)   *out_inner_key_idx   = shape->u.tmap.key_idx;
        return TYPE_TYPED_MAP;
      case TYPE_SHAPE_BUF:
        /* T's encoding via out_inner_value_idx. N reachable via
         * shape->u.buf.len (callers that need stride consult it). */
        if (out_inner_value_idx) *out_inner_value_idx = shape->u.buf.elem_idx;
        return TYPE_BUF;
      case TYPE_SHAPE_PTR:
        if (out_inner_value_idx) *out_inner_value_idx = shape->u.ptr.pointee_idx;
        return TYPE_PTR;
      case TYPE_SHAPE_FUTURE:
        if (out_inner_value_idx) *out_inner_value_idx = shape->u.future.resolves_to_idx;
        return TYPE_FUTURE;
      case TYPE_SHAPE_BOX:
        if (out_inner_value_idx) *out_inner_value_idx = shape->u.box.boxes_idx;
        return TYPE_BOX;
      case TYPE_SHAPE_PROC:
        if (out_inner_value_idx) *out_inner_value_idx = shape->u.proc.return_idx;
        return TYPE_CLOSURE;
      default:
        return TYPE_DYN;
    }
  }
  /* Struct idx range. */
  return TYPE_STRUCT;
}

/* Resolve a typed-collection ELEMENT type expression — `[Vec T]`,
 * `[Map V]`, `[Map K V]` as the element of an outer ref-element vec
 * ([Vec [Vec i64]] etc.) — into a TYPER-SIDE shape idx (interned in
 * tc->shape_reg). Returns UINT32_MAX when the expression isn't a
 * recognizable nested collection. Cross-registry rule: the returned idx
 * lives only in typer bindings / typer-local decisions, NEVER on AST
 * stamps (docs/TYPE_REGISTRY_REFACTOR.md). */
static uint32_t typer__nested_elem_shape(TyperCtx* tc, AstNode* en) {
  if (!en || en->type != AST_COMMAND || !en->data.command.head ||
      en->data.command.head->type != AST_LIT_STRING) return UINT32_MAX;
  const char* hn = en->data.command.head->data.lit_string.value;
  uint32_t    hl = en->data.command.head->data.lit_string.length;
  uint32_t    ac = en->data.command.arg_count;
  /* Inner type-name → portable encoding (scalar sentinel / struct idx). */
  uint32_t inner[2] = { UINT32_MAX, UINT32_MAX };
  for (uint32_t i = 0; i < ac && i < 2; i++) {
    AstNode* a = en->data.command.args[i];
    if (a && a->type == AST_COMMAND) {
      /* Depth-2+ element type expression ([Vec [Vec [Vec T]]] …): recurse —
       * the inner shape idx becomes this level's element encoding. Shape
       * idxs nest fine inside the typer's own registry (they only ever live
       * in typer bindings / typer-local decode). */
      inner[i] = typer__nested_elem_shape(tc, a);
      if (inner[i] == UINT32_MAX) return UINT32_MAX;
      continue;
    }
    if (!a || a->type != AST_LIT_STRING) return UINT32_MAX;
    const char* nm = a->data.lit_string.value;
    uint32_t    nl = a->data.lit_string.length;
    if (is_type_keyword(nm, nl)) {
      inner[i] = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
    } else {
      for (uint32_t si = 0; si < tc->struct_count; si++) {
        if (tc->structs[si].name_len == nl &&
            memcmp(tc->structs[si].name, nm, nl) == 0) { inner[i] = si; break; }
      }
      if (inner[i] == UINT32_MAX) return UINT32_MAX;
    }
  }
  if (hl == 3 && memcmp(hn, "Vec", 3) == 0 && ac == 1)
    return type_shape_intern_typed_vec(&tc->shape_reg, inner[0]);
  /* One-arg [Map V] removed — dyn keys are spelled [Map dyn V]. A dyn
   * key keyword normalizes to the UINT32_MAX dyn-key convention (the
   * VM's 0xFFFF) rather than the scalar-dyn sentinel. */
  if (hl == 3 && memcmp(hn, "Map", 3) == 0 && ac == 2) {
    uint32_t kidx = (inner[0] == JACL_SCALAR_TYPE_IDX(TYPE_DYN))
                    ? UINT32_MAX : inner[0];
    return type_shape_intern_typed_map(&tc->shape_reg, kidx, inner[1]);
  }
  return UINT32_MAX;
}

/* Decode an interned typed-map SHAPE (from typer__nested_elem_shape) to
 * its PORTABLE binding encoding, honoring the map rep rule: ref-kind
 * VALUES (str/dyn) select the PLAIN traced map rep (TYPE_MAP + stamps);
 * value-kind values keep TYPE_TYPED_MAP. Without this, a nested
 * [Vec [Map K str]] element would be stamped TYPE_TYPED_MAP and typed
 * map opcodes would run against the plain rep. */
static void typer__decode_map_shape(uint32_t iv, uint32_t ik,
                                    JaclType* t, uint32_t* si,
                                    uint32_t* ki) {
  bool val_ref = JACL_IS_SCALAR_TYPE_IDX(iv) &&
                 (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR ||
                  JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_DYN);
  if (val_ref) {
    *t  = TYPE_MAP;
    *si = (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR) ? iv : UINT32_MAX;
  } else {
    *t  = TYPE_TYPED_MAP;
    *si = iv;
  }
  *ki = ik;
}

/* For a HEAD_DOT arrow `recv->lit_int_idx`, walk inside-out through
 * consecutive HEAD_DOT(_, LIT_INT) arrows to find an innermost
 * AST_VAR_REF bound to either:
 *   - TYPE_BUF with nested-buf shape (depth >= 3, encoded via the
 *     typer's TYPE_SHAPE_BUF chain), or
 *   - TYPE_PTR whose pointee shape idx is a TYPE_SHAPE_BUF (decomposed
 *     chain: `let p = $cube->1; $p->2->3`).
 *
 * Returns true with the result type for the chain segment up to and
 * including this arrow: TYPE_PTR for intermediate steps; the leaf
 * scalar/struct for the deepest arrow.
 *
 * For intermediate steps, also writes `*out_remaining_shape_idx` =
 * the typer-side shape idx for the remaining buf-shape after this
 * arrow. NULL-tolerant. Callers that bind a name to an intermediate
 * pointer (e.g. typer__handle_def_or_mut) read it to keep the
 * binding's shape live; callers that just need the result type pass
 * NULL.
 *
 * Returns false for non-chains, depth-2 receivers (handled by the
 * existing per-arrow path), or non-buf/non-ptr bases. Callers fall
 * through to the existing handlers.
 *
 * The intermediate-step result's `inferred_struct_idx` stays
 * UINT32_MAX on the AST node -- the cross-registry rule
 * (docs/TYPE_REGISTRY_REFACTOR.md) forbids typer-side shape idxes
 * from reaching the compiler via AST node fields. The compiler
 * recovers the shape by re-walking the AST chain itself; only the
 * typer's binding table carries the typer-side shape idx. */
static bool typer__nested_buf_chain_result(TyperCtx* tc,
                                            AstNode* outer_recv,
                                            AstNode* outer_int,
                                            JaclType* out_type,
                                            uint32_t* out_sidx,
                                            uint32_t* out_remaining_shape_idx) {
  if (out_remaining_shape_idx) *out_remaining_shape_idx = UINT32_MAX;
  /* Index-arrow check: anything other than a name field (LIT_STRING)
   * counts as an index. Dynamic indices like `$slice->$i` parse with
   * AST_VAR_REF here. */
  if (!outer_int || outer_int->type == AST_LIT_STRING) return false;
  AstNode* idx_nodes[16];
  int chain_depth = 1;
  idx_nodes[0] = outer_int;
  AstNode* cur = outer_recv;
  while (cur && cur->type == AST_COMMAND &&
         cur->data.command.head_id == HEAD_DOT &&
         cur->data.command.arg_count == 2 &&
         cur->data.command.args[1] &&
         cur->data.command.args[1]->type != AST_LIT_STRING) {
    if (chain_depth >= 16) return false;
    idx_nodes[chain_depth++] = cur->data.command.args[1];
    cur = cur->data.command.args[0];
  }
  /* Validate that non-literal indices type as integer (i8..u64) or
   * dyn. Anything else (struct, string, float, ...) means this isn't a
   * buf chain after all -- bail so the existing handler reports it. */
  for (int k = 0; k < chain_depth; k++) {
    if (idx_nodes[k]->type == AST_LIT_INT) continue;
    JaclType it = (JaclType)idx_nodes[k]->inferred_type;
    if (it == TYPE_DYN) continue;
    if (it != TYPE_I8 && it != TYPE_U8 &&
        it != TYPE_I16 && it != TYPE_U16 &&
        it != TYPE_I32 && it != TYPE_U32 &&
        it != TYPE_I64 && it != TYPE_U64) {
      return false;
    }
  }
  if (!cur || cur->type != AST_VAR_REF) return false;
  const TyperBinding* b = typer__scope_resolve(tc,
      cur->data.var_ref.name, cur->data.var_ref.length, cur->scope_mark);
  if (!b || b->struct_idx == UINT32_MAX) return false;

  /* shape_after[k] = encoding visible after taking k arrows (1..num_dims).
   * The encoding at index num_dims is the leaf (non-BUF). */
  uint32_t shape_after[16];
  int      num_dims = 1;
  if (b->type == TYPE_BUF) {
    /* TYPE_BUF binding: dim 0 = b->buf_len; shape after 1 arrow lives at
     * b->struct_idx (a TYPE_SHAPE_BUF or a leaf encoding). */
    shape_after[1] = b->struct_idx;
  } else if (b->type == TYPE_PTR) {
    /* TYPE_PTR binding with a buf-shape pointee: the pointer addresses
     * the start of the shape entry's [Buf N T] region. Dim 0 = shape
     * length; shape after 1 arrow = shape.elem_idx. */
    if (b->struct_idx < TYPER_MAX_STRUCTS ||
        b->struct_idx >= tc->shape_reg.count ||
        tc->shape_reg.shapes[b->struct_idx].kind != TYPE_SHAPE_BUF) {
      return false;
    }
    shape_after[1] = tc->shape_reg.shapes[b->struct_idx].u.buf.elem_idx;
  } else {
    return false;
  }
  uint32_t cur_enc = shape_after[1];
  while (cur_enc >= TYPER_MAX_STRUCTS && cur_enc < tc->shape_reg.count &&
         tc->shape_reg.shapes[cur_enc].kind == TYPE_SHAPE_BUF) {
    if (num_dims >= 15) return false;
    cur_enc = tc->shape_reg.shapes[cur_enc].u.buf.elem_idx;
    num_dims++;
    shape_after[num_dims] = cur_enc;
  }
  uint32_t leaf_enc = cur_enc;

  /* Gate:
   *   TYPE_BUF base, num_dims == 2 -- defer to existing per-arrow path
   *     when ALL indices are LIT_INT (preserves the well-tested
   *     depth-2 codegen). Intervene only if any index is dynamic.
   *   TYPE_BUF base, num_dims >= 3 -- always intervene.
   *   TYPE_PTR-with-buf-shape base -- always intervene; the existing
   *     per-arrow handler doesn't recognize buf-shape pointees. */
  if (b->type == TYPE_BUF && num_dims < 2) return false;
  if (b->type == TYPE_BUF && num_dims == 2) {
    bool any_dyn = false;
    for (int k = 0; k < chain_depth; k++) {
      if (idx_nodes[k]->type != AST_LIT_INT) { any_dyn = true; break; }
    }
    if (!any_dyn) return false;
  }
  if (chain_depth > num_dims) return false;

  if (chain_depth == num_dims) {
    if (JACL_IS_SCALAR_TYPE_IDX(leaf_enc)) {
      JaclType t = JACL_TYPE_IDX_TO_SCALAR(leaf_enc);
      switch (t) {
        case TYPE_I8: case TYPE_U8:
        case TYPE_I16: case TYPE_U16:
          *out_type = TYPE_I32; break;
        default:
          *out_type = t; break;
      }
      *out_sidx = UINT32_MAX;
    } else {
      *out_type = TYPE_STRUCT;
      *out_sidx = leaf_enc;
    }
  } else {
    /* Intermediate: yield [Ptr <UINT32_MAX>] on the AST; the typer-side
     * remaining-shape idx goes back via out_remaining_shape_idx so the
     * def handler can store it on the binding (kept off the AST node by
     * the cross-registry rule). */
    *out_type = TYPE_PTR;
    *out_sidx = UINT32_MAX;
    if (out_remaining_shape_idx) {
      *out_remaining_shape_idx = shape_after[chain_depth];
    }
  }
  return true;
}

/* Compile-time type check on a buf-set value. Decodes the buf's
 * element type from `recv_encoded` (binding.struct_idx) and compares
 * to the value's inferred type. Emits a typer error on mismatch.
 * Lenient for dyn values (they may narrow at runtime and the compiler
 * keeps its own runtime check) and for unknown buf encodings (the
 * compiler will reject at codegen).
 *
 * Closes the soundness gap noted in BUFFER_DESIGN.md M4.4 ext
 * follow-ups: without this, a `[Buf N [Vec i64]]` would silently
 * accept a `[Vec str]` value, and a later vec-get on the buf-derived
 * typed-vec would interpret bytes as the wrong type. */
static void typer__check_buf_set_value(TyperCtx* tc, uint32_t recv_encoded,
                                        AstNode* val) {
  if (recv_encoded == UINT32_MAX) return;
  JaclType vt = (JaclType)val->inferred_type;
  if (vt == TYPE_DYN) return;  /* dyn always lenient */
  uint32_t inner_v = UINT32_MAX, inner_k = UINT32_MAX;
  JaclType elem = typer__buf_elem_decode(tc, recv_encoded,
                                          &inner_v, &inner_k);
  bool ok = false;
  switch (elem) {
    case TYPE_BOOL: ok = (vt == TYPE_BOOL); break;
    case TYPE_I8: case TYPE_U8:
    case TYPE_I16: case TYPE_U16:
    case TYPE_I32: case TYPE_U32:
      ok = (vt == TYPE_I32 || vt == TYPE_U32); break;
    case TYPE_I64: ok = (vt == TYPE_I64 || vt == TYPE_I32); break;
    case TYPE_U64: ok = (vt == TYPE_U64); break;
    case TYPE_F32:
    case TYPE_F64: ok = (vt == TYPE_F32 || vt == TYPE_F64); break;
    case TYPE_TYPED_VEC:
      ok = (vt == TYPE_TYPED_VEC && val->inferred_struct_idx == inner_v);
      break;
    case TYPE_TYPED_MAP:
      ok = (vt == TYPE_TYPED_MAP &&
            val->inferred_struct_idx == inner_v &&
            val->inferred_key_struct_idx == inner_k);
      break;
    case TYPE_STRUCT:
      /* struct buf elements encode the struct idx in recv_encoded
       * directly (not via a registry shape). Compare struct idx. */
      ok = (vt == TYPE_STRUCT && val->inferred_struct_idx == recv_encoded);
      break;
    case TYPE_PTR:
      ok = (vt == TYPE_PTR && val->inferred_struct_idx == inner_v);
      break;
    case TYPE_FUTURE:
      ok = (vt == TYPE_FUTURE && val->inferred_struct_idx == inner_v);
      break;
    case TYPE_STR: ok = (vt == TYPE_STR); break;
    case TYPE_VEC:
      /* plain vec accepts both typed and untyped vec values */
      ok = (vt == TYPE_VEC || vt == TYPE_TYPED_VEC); break;
    case TYPE_MAP:
      ok = (vt == TYPE_MAP || vt == TYPE_TYPED_MAP); break;
    case TYPE_STREAM:  ok = (vt == TYPE_STREAM); break;
    case TYPE_CLOSURE: ok = (vt == TYPE_CLOSURE); break;
    case TYPE_BOX:     ok = (vt == TYPE_BOX); break;
    default: ok = true; break;  /* unknown elem encoding: defer to compiler */
  }
  if (!ok) {
    char err[256];
    /* When both sides are the same outer kind (typed-vec / typed-map),
     * the bare "X does not match X" is unhelpful. Distinguish by inner
     * T when possible. */
    if (elem == TYPE_TYPED_VEC && vt == TYPE_TYPED_VEC) {
      snprintf(err, sizeof(err),
          "type error: buf-set value is a typed-vec with a different "
          "element type than the buf's [Vec T]");
    } else if (elem == TYPE_TYPED_MAP && vt == TYPE_TYPED_MAP) {
      snprintf(err, sizeof(err),
          "type error: buf-set value is a typed-map with different K or V "
          "than the buf's [Map K V]");
    } else if (elem == TYPE_STRUCT && vt == TYPE_STRUCT) {
      snprintf(err, sizeof(err),
          "type error: buf-set value is a different struct type than the "
          "buf's element struct");
    } else if (elem == TYPE_PTR && vt == TYPE_PTR) {
      snprintf(err, sizeof(err),
          "type error: buf-set value is a pointer to a different type than "
          "the buf's [Ptr T]");
    } else if (elem == TYPE_FUTURE && vt == TYPE_FUTURE) {
      snprintf(err, sizeof(err),
          "type error: buf-set value is a future of a different type than "
          "the buf's [Future T]");
    } else {
      snprintf(err, sizeof(err),
          "type error: buf-set value type %s does not match buf element type %s",
          type_name(vt), type_name(elem));
    }
    typer__error(tc, val->start.line, val->start.column, err);
  }
}

/* Recognize `[Buf N T]` -- and its nested form `[Buf N [Buf M T]]` --
 * and the typed-collection element form `[Buf N [Vec T]]` (M4.4 ext).
 *
 *   *out_struct_idx: element T encoding -- JACL_SCALAR_TYPE_IDX(scalar)
 *                    for scalar T, or struct registry idx for value-
 *                    struct T (M4.1). For the nested form, the encoding
 *                    is the *inner* scalar T (M4.2 limits inner element
 *                    to scalars; deeper nesting / struct-inner are
 *                    deferred). For the typed-vec form, the encoding is
 *                    JACL_SCALAR_TYPE_IDX(TYPE_TYPED_VEC).
 *   *out_len:        outer N.
 *   *out_inner_len:  M for the nested form; 0 for the flat / typed-vec forms.
 *   *out_typed_elem_idx (NULL-tolerant): for the typed-vec form, the
 *                    inner T's encoding (scalar sentinel or struct idx).
 *                    UINT32_MAX otherwise.
 *
 * Returns true if the head is "Buf" (shape may still be invalid -- the
 * caller distinguishes via the sentinel out values for the precise
 * diagnostic).
 *
 * See BUFFER_DESIGN.md M1 / M4.1 / M4.2 / M4.4. */
static bool typer__buf_type_full(TyperCtx* tc, AstNode* node,
                                 uint32_t* out_struct_idx, uint32_t* out_len,
                                 uint32_t* out_inner_len,
                                 uint32_t* out_typed_elem_idx) {
  *out_struct_idx = UINT32_MAX;
  *out_len = 0;
  *out_inner_len = 0;
  if (out_typed_elem_idx) *out_typed_elem_idx = UINT32_MAX;
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 3 ||
      memcmp(h->data.lit_string.value, "Buf", 3) != 0) return false;
  if (node->data.command.arg_count != 2) return true; /* shape error; caller reports */
  AstNode* n_arg = node->data.command.args[0];
  AstNode* t_arg = node->data.command.args[1];
  if (n_arg->type == AST_LIT_INT && n_arg->data.lit_int.value > 0) {
    *out_len = (uint32_t)n_arg->data.lit_int.value;
  }
  if (t_arg->type == AST_LIT_STRING) {
    const char* nm = t_arg->data.lit_string.value;
    uint32_t    nl = t_arg->data.lit_string.length;
    if (is_type_keyword(nm, nl)) {
      *out_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
    } else {
      /* Struct element (M4.1): look up by name in the typer's struct
       * registry. Real struct indices live below JACL_SCALAR_VEC_BASE
       * so the encoding is unambiguous with the scalar sentinel. */
      for (uint32_t si = 0; si < tc->struct_count; si++) {
        if (tc->structs[si].name_len == nl &&
            memcmp(tc->structs[si].name, nm, nl) == 0) {
          *out_struct_idx = si;
          break;
        }
      }
    }
  } else if (t_arg->type == AST_COMMAND &&
             t_arg->data.command.head &&
             t_arg->data.command.head->type == AST_LIT_STRING &&
             t_arg->data.command.head->data.lit_string.length == 3 &&
             memcmp(t_arg->data.command.head->data.lit_string.value, "Buf", 3) == 0) {
    /* Nested buf element type: [Buf N [Buf M ...]] -- recursively
     * intern the inner buf as a TYPE_SHAPE_BUF entry (Phase 5b of
     * TYPE_REGISTRY_REFACTOR.md). The outer buf binding's struct_idx
     * holds the inner shape's registry idx; the inner shape itself
     * carries len + elem (which may be another shape idx, allowing
     * arbitrary nesting depth at declaration time). Arrow-chain
     * indexing semantics for depth >= 3 are still being wired up;
     * declarations type cleanly and [buf-len] / [addr] work today. */
    uint32_t inner_elem_sidx = UINT32_MAX;
    uint32_t inner_M = 0;
    uint32_t inner_inner_M = 0;
    if (typer__buf_type_full(tc, t_arg, &inner_elem_sidx, &inner_M,
                             &inner_inner_M, NULL) &&
        inner_M > 0 && inner_elem_sidx != UINT32_MAX) {
      uint32_t inner_shape_idx =
          type_shape_intern_buf(&tc->shape_reg, inner_M, inner_elem_sidx);
      if (inner_shape_idx != UINT32_MAX) {
        *out_struct_idx = inner_shape_idx;
      }
    }
    /* If the inner form is malformed (zero/missing N or unresolved
     * T), leave *out_struct_idx as UINT32_MAX so the caller's
     * `bad_elem` diagnostic fires. */
  } else if (t_arg->type == AST_COMMAND &&
             t_arg->data.command.head &&
             t_arg->data.command.head->type == AST_LIT_STRING &&
             t_arg->data.command.head->data.lit_string.length == 3 &&
             memcmp(t_arg->data.command.head->data.lit_string.value, "Vec", 3) == 0 &&
             t_arg->data.command.arg_count == 1 &&
             t_arg->data.command.args[0]->type == AST_LIT_STRING) {
    /* Typed-vec element type: [Buf N [Vec T]] (M4.4 ext, now via the
     * typer's shape registry as of TYPE_REGISTRY_REFACTOR.md Phase 3).
     * Interns a TYPE_SHAPE_TYPED_VEC entry; binding.struct_idx holds
     * the resulting registry idx (>= TYPER_MAX_STRUCTS, disjoint from
     * struct indices in tc->structs[]). */
    AstNode* t_inner = t_arg->data.command.args[0];
    const char* nm = t_inner->data.lit_string.value;
    uint32_t    nl = t_inner->data.lit_string.length;
    uint32_t inner_idx = UINT32_MAX;
    if (is_type_keyword(nm, nl)) {
      inner_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
    } else {
      for (uint32_t si = 0; si < tc->struct_count; si++) {
        if (tc->structs[si].name_len == nl &&
            memcmp(tc->structs[si].name, nm, nl) == 0) {
          inner_idx = si;
          break;
        }
      }
    }
    if (inner_idx != UINT32_MAX) {
      uint32_t shape_idx = type_shape_intern_typed_vec(&tc->shape_reg, inner_idx);
      if (shape_idx != UINT32_MAX) *out_struct_idx = shape_idx;
      if (out_typed_elem_idx) *out_typed_elem_idx = inner_idx; /* legacy mirror */
    }
  } else if (t_arg->type == AST_COMMAND &&
             t_arg->data.command.head &&
             t_arg->data.command.head->type == AST_LIT_STRING &&
             t_arg->data.command.head->data.lit_string.length == 3 &&
             memcmp(t_arg->data.command.head->data.lit_string.value, "Map", 3) == 0 &&
             t_arg->data.command.arg_count == 2 &&
             t_arg->data.command.args[0]->type == AST_LIT_STRING &&
             t_arg->data.command.args[1]->type == AST_LIT_STRING) {
    /* Typed-map element type: [Buf N [Map K V]] (Phase 3; the one-arg
     * [Map V] form was removed — dyn keys are spelled [Map dyn V]).
     * Each slot is a tagged JaclVal pointing to a typed-map value.
     * K + V both ride on one shape registry entry; binding stores the
     * entry's idx, freeing aux fields permanently for any future
     * nesting depth. */
    AstNode* k_node = t_arg->data.command.args[0];
    AstNode* v_node = t_arg->data.command.args[1];
    uint32_t key_idx = UINT32_MAX;
    if (k_node) {
      const char* nm = k_node->data.lit_string.value;
      uint32_t    nl = k_node->data.lit_string.length;
      if (is_type_keyword(nm, nl)) {
        JaclType kkw = type_from_keyword(nm, nl);
        /* dyn keys normalize to the UINT32_MAX convention (VM 0xFFFF). */
        key_idx = (kkw == TYPE_DYN) ? UINT32_MAX : JACL_SCALAR_TYPE_IDX(kkw);
      } else {
        for (uint32_t si = 0; si < tc->struct_count; si++) {
          if (tc->structs[si].name_len == nl &&
              memcmp(tc->structs[si].name, nm, nl) == 0) {
            key_idx = si;
            break;
          }
        }
        if (key_idx == UINT32_MAX) return true; /* bad_elem */
      }
    }
    uint32_t value_idx = UINT32_MAX;
    {
      const char* nm = v_node->data.lit_string.value;
      uint32_t    nl = v_node->data.lit_string.length;
      if (is_type_keyword(nm, nl)) {
        value_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
      } else {
        for (uint32_t si = 0; si < tc->struct_count; si++) {
          if (tc->structs[si].name_len == nl &&
              memcmp(tc->structs[si].name, nm, nl) == 0) {
            value_idx = si;
            break;
          }
        }
        if (value_idx == UINT32_MAX) return true; /* bad_elem */
      }
    }
    uint32_t shape_idx = type_shape_intern_typed_map(&tc->shape_reg,
                                                     key_idx, value_idx);
    if (shape_idx != UINT32_MAX) *out_struct_idx = shape_idx;
  } else if (t_arg->type == AST_COMMAND) {
    /* Pointer element [Buf N [Ptr T]] (Phase 5 compose case). Each
     * slot stores a tagged JaclVal (the [Ptr T] value -- runtime
     * encoding is a heap-boxed u64). The pointee T is carried by the
     * shape entry so buf-get narrows downstream pointer-arrow chains
     * to T. */
    uint32_t pointee_idx = UINT32_MAX;
    if (typer__ptr_type(tc, t_arg, &pointee_idx)) {
      uint32_t shape_idx = type_shape_intern_ptr(&tc->shape_reg, pointee_idx);
      if (shape_idx != UINT32_MAX) *out_struct_idx = shape_idx;
    } else {
      /* Future element [Buf N [Future T]] (Phase 5 compose case).
       * Same ref-elem storage pattern as Ptr; the resolved-to type T
       * is carried by the shape entry. */
      uint32_t fut_idx = UINT32_MAX;
      if (typer__future_type(tc, t_arg, &fut_idx)) {
        uint32_t shape_idx = type_shape_intern_future(&tc->shape_reg, fut_idx);
        if (shape_idx != UINT32_MAX) *out_struct_idx = shape_idx;
      } else {
        /* Box element [Buf N [Box T]] (Phase 5 compose case). Tagged
         * slot, runtime is OBJ_BOX_INLINE or OBJ_MUT_REF; the inner
         * T rides on the shape entry. */
        uint32_t box_idx = UINT32_MAX;
        if (typer__box_type(tc, t_arg, &box_idx)) {
          uint32_t shape_idx = type_shape_intern_box(&tc->shape_reg, box_idx);
          if (shape_idx != UINT32_MAX) *out_struct_idx = shape_idx;
        }
      }
    }
  }
  return true;
}

/* Compatibility wrapper -- the same as typer__buf_type_full but
 * discards the inner-len + typed-elem out params. Existing callers
 * that don't yet care about nested or typed-collection elements use
 * this. */
static bool typer__buf_type(TyperCtx* tc, AstNode* node,
                            uint32_t* out_struct_idx, uint32_t* out_len) {
  uint32_t inner_len_discard;
  return typer__buf_type_full(tc, node, out_struct_idx, out_len,
                              &inner_len_discard, NULL);
}

/* Recognize [Vec T] / [Map K V] type expressions. Returns 1 for
 * [Vec T], 3 for [Map K V], 0 if not a typed-collection expression.
 * Mirrors compiler__typed_collection_expr in compiler.c.
 *
 * The one-arg [Map V] form was REMOVED (2026-06-10): maps always take a
 * key and a value type, `map` is shorthand for [Map dyn dyn], and dyn
 * keys are spelled [Map dyn V]. Kind 2 is never returned anymore; the
 * surfaces that resolve type expressions call typer__map_v_form to emit
 * a targeted migration diagnostic. */
static int typer__typed_collection_kind(AstNode* node) {
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return 0;
  AstNode* h = node->data.command.head;
  if (h->type != AST_LIT_STRING || h->data.lit_string.length != 3) return 0;
  int kind = 0;
  if (memcmp(h->data.lit_string.value, "Vec", 3) == 0) kind = 1;
  else if (memcmp(h->data.lit_string.value, "Map", 3) == 0) kind = 2;
  if (kind == 0) return 0;
  uint32_t ac = node->data.command.arg_count;
  if (kind == 2) {
    if (ac == 2 &&
        node->data.command.args[0]->type == AST_LIT_STRING &&
        node->data.command.args[1]->type == AST_LIT_STRING) {
      return 3;
    }
    return 0;  /* one-arg [Map V] removed — see typer__map_v_form */
  }
  if (ac != 1 || node->data.command.args[0]->type != AST_LIT_STRING) return 0;
  return kind;
}

/* Detect the removed one-arg [Map V] form so resolution surfaces (ctor,
 * def annotation, params, return types) can emit the migration error
 * instead of a generic unknown-type diagnostic. */
static bool typer__map_v_form(AstNode* node) {
  if (!node || node->type != AST_COMMAND || !node->data.command.head) return false;
  AstNode* h = node->data.command.head;
  return h->type == AST_LIT_STRING && h->data.lit_string.length == 3 &&
         memcmp(h->data.lit_string.value, "Map", 3) == 0 &&
         node->data.command.arg_count == 1;
}

#define TYPER_MAP_V_REMOVED_MSG \
  "[Map V] was removed: maps are always [Map K V] — use [Map dyn V] " \
  "for dyn keys (`map` is shorthand for [Map dyn dyn])"

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
/* Forward decl: defined below; needed by the Phase B typed-closure
 * monomorphization walk in handle_def_or_mut. */
static uint32_t typer__parse_params(TyperCtx* tc, AstNode* params,
                                    AstNode* (*name_nodes_out)[TYPER_MAX_PROC_PARAMS],
                                    JaclType (*types_out)[TYPER_MAX_PROC_PARAMS],
                                    uint32_t (*struct_idxs_out)[TYPER_MAX_PROC_PARAMS]);
/* Forward decl: B3b call-return closure-binding stamp uses it before its def. */
static bool typer__decode_proc_shape(TyperCtx* tc, uint32_t shape_idx,
                                     uint8_t* out_pcount, uint8_t* out_ptypes,
                                     uint8_t* out_rtype);

static bool typer__handle_def_or_mut(TyperCtx* tc, AstNode* node) {
  AstNode** args = node->data.command.args;
  uint32_t  argc = node->data.command.arg_count;

  JaclType  declared_type = TYPE_DYN;
  AstNode*  name_node     = NULL;
  AstNode*  value_node    = NULL;

  uint32_t declared_struct_idx        = UINT32_MAX;
  uint32_t declared_buf_len           = 0;
  uint32_t declared_buf_typed_elem_idx = UINT32_MAX;

  /* Typed-closure signature from a [Proc [P…] R] annotation (Phase B). */
  bool     declared_proc          = false;
  uint8_t  declared_proc_pcount    = 0;
  JaclType declared_proc_ptypes[TYPER_MAX_PROC_PARAMS];
  uint32_t declared_proc_pidxs[TYPER_MAX_PROC_PARAMS];
  JaclType declared_proc_rtype     = TYPE_NIL;
  uint32_t declared_proc_ridx      = UINT32_MAX;

  /* No-value buf form: `def [Buf N T] NAME` — argc==2 with args[0] a
   * Buf type annotation and args[1] the bare name. Zero-init is
   * implicit (the compiler emits OP_BUF_ZERO_LOCAL). See
   * BUFFER_DESIGN.md M2 / M4.2. */
  if (argc == 2 && args[1]->type == AST_LIT_STRING) {
    uint32_t buf_sidx;
    uint32_t buf_len;
    uint32_t buf_inner_len = 0;
    uint32_t buf_typed_elem_idx = UINT32_MAX;
    if (typer__buf_type_full(tc, args[0], &buf_sidx, &buf_len,
                             &buf_inner_len, &buf_typed_elem_idx)) {
      char err[256];
      if (buf_len == 0) {
        jacl_format_buf_bad_len(err, sizeof(err));
        typer__error(tc, args[0]->start.line, args[0]->start.column, err);
      }
      if (buf_sidx == UINT32_MAX) {
        jacl_format_buf_bad_elem(err, sizeof(err));
        typer__error(tc, args[0]->start.line, args[0]->start.column, err);
      }
      typer__scope_add(tc, args[1]->data.lit_string.value,
                       args[1]->data.lit_string.length,
                       args[1]->scope_mark,
                       (uint8_t)TYPE_BUF, buf_sidx);
      if (buf_len > 0 && tc->binding_count > 0) {
        tc->bindings[tc->binding_count - 1].buf_len = buf_len;
        tc->bindings[tc->binding_count - 1].buf_inner_len = buf_inner_len;
        /* For [Buf N [Vec T]] (M4.4 ext) the inner T idx is stashed in
         * key_struct_idx so buf-get can narrow to TYPE_TYPED_VEC + T. */
        tc->bindings[tc->binding_count - 1].key_struct_idx = buf_typed_elem_idx;
      }
      node->inferred_type       = TYPE_NIL;
      node->inferred_struct_idx = UINT32_MAX;
      return true;
    }
  }

  if (argc == 3) {
    /* Keyword form: def TYPE NAME VALUE, or def StructName NAME VALUE,
     * or def [Vec T] / [Map K V] / [Buf N T] NAME VALUE for typed
     * collections / buffers. */
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
      uint32_t fut_sidx;
      uint32_t ptr_sidx;
      uint32_t buf_sidx;
      uint32_t buf_len;
      uint32_t buf_inner_len_3 = 0;
      uint32_t buf_typed_elem_idx_3 = UINT32_MAX;
      uint32_t box_sidx;
      uint32_t arr_elem_idx;
      bool proc_supported = true;
      if (typer__proc_type(tc, args[0], &declared_proc_pcount,
                           declared_proc_ptypes, declared_proc_pidxs,
                           &declared_proc_rtype, &declared_proc_ridx,
                           &proc_supported)) {
        if (!proc_supported) {
          typer__error(tc, args[0]->start.line, args[0]->start.column,
                       "typed-closure [Proc …] struct/compound param or return "
                       "types are not yet supported (Phase B3) — use scalar "
                       "types or `dyn`");
        }
        declared_type = TYPE_CLOSURE;
        declared_proc = true;
      } else if (typer__future_type(tc, args[0], &fut_sidx)) {
        declared_type = TYPE_FUTURE;
        declared_struct_idx = fut_sidx;
      } else if (typer__ptr_type(tc, args[0], &ptr_sidx)) {
        declared_type = TYPE_PTR;
        declared_struct_idx = ptr_sidx;
      } else if (typer__box_type(tc, args[0], &box_sidx)) {
        declared_type = TYPE_BOX;
        declared_struct_idx = box_sidx;
      } else if (typer__arr_type(tc, args[0], &arr_elem_idx)) {
        /* Ref-element [Arr T] annotations (def [Arr str] / [Arr dyn] a):
         * dyn arr rep — resolve to TYPE_ARR (+ str stamp), mirroring the
         * [Vec str] annotation arm. */
        if (arr_elem_idx == JACL_SCALAR_TYPE_IDX(TYPE_STR) ||
            arr_elem_idx == JACL_SCALAR_TYPE_IDX(TYPE_DYN)) {
          declared_type = TYPE_ARR;
          declared_struct_idx =
              (arr_elem_idx == JACL_SCALAR_TYPE_IDX(TYPE_STR))
                  ? arr_elem_idx : UINT32_MAX;
        } else {
          declared_type = TYPE_TYPED_ARR;
          declared_struct_idx = arr_elem_idx;
        }
      } else if (typer__buf_type_full(tc, args[0], &buf_sidx, &buf_len,
                                      &buf_inner_len_3, &buf_typed_elem_idx_3)) {
        char err[256];
        if (buf_len == 0) {
          jacl_format_buf_bad_len(err, sizeof(err));
          typer__error(tc, args[0]->start.line, args[0]->start.column, err);
        }
        if (buf_sidx == UINT32_MAX) {
          jacl_format_buf_bad_elem(err, sizeof(err));
          typer__error(tc, args[0]->start.line, args[0]->start.column, err);
        }
        declared_type = TYPE_BUF;
        declared_struct_idx = buf_sidx;
        declared_buf_len = buf_len;
        declared_buf_typed_elem_idx = buf_typed_elem_idx_3;
        (void)buf_inner_len_3;
      } else {
        if (typer__map_v_form(args[0])) {
          typer__error(tc, args[0]->start.line, args[0]->start.column,
                       TYPER_MAP_V_REMOVED_MSG);
          return false;
        }
        int tcoll = typer__typed_collection_kind(args[0]);
        /* Ref-element / nested-element [Vec T] annotations resolve to
         * TYPE_VEC (+ stamp / typer-side shape idx in the binding). */
        bool vec_ref_ann = false;
        if (tcoll == 1 &&
            args[0]->data.command.args[0]->type == AST_LIT_STRING &&
            is_type_keyword(args[0]->data.command.args[0]->data.lit_string.value,
                            args[0]->data.command.args[0]->data.lit_string.length)) {
          JaclType aet = type_from_keyword(
              args[0]->data.command.args[0]->data.lit_string.value,
              args[0]->data.command.args[0]->data.lit_string.length);
          if (aet == TYPE_STR || aet == TYPE_DYN) {
            declared_type = TYPE_VEC;
            declared_struct_idx = (aet == TYPE_STR)
                ? JACL_SCALAR_TYPE_IDX(TYPE_STR) : UINT32_MAX;
            vec_ref_ann = true;
          }
        } else if (tcoll == 0 && args[0]->data.command.head &&
                   args[0]->data.command.head->type == AST_LIT_STRING &&
                   args[0]->data.command.head->data.lit_string.length == 3 &&
                   memcmp(args[0]->data.command.head->data.lit_string.value,
                          "Vec", 3) == 0 &&
                   args[0]->data.command.arg_count == 1 &&
                   args[0]->data.command.args[0]->type == AST_COMMAND) {
          uint32_t esh =
              typer__nested_elem_shape(tc, args[0]->data.command.args[0]);
          if (esh != UINT32_MAX) {
            declared_type = TYPE_VEC;
            declared_struct_idx = esh;
            vec_ref_ann = true;
          }
        }
        /* Ref-kind VALUE [Map K V] annotations (def [Map K str] m /
         * def [Map str dyn] m): plain-map rep — resolve to TYPE_MAP;
         * stamps inherit from the RHS ctor via the TYPE_MAP def arm
         * (declared value stamp wins below). */
        if (!vec_ref_ann && tcoll == 3) {
          AstNode* vn = args[0]->data.command.args[1];
          if (vn && vn->type == AST_LIT_STRING &&
              is_type_keyword(vn->data.lit_string.value,
                              vn->data.lit_string.length)) {
            JaclType avt = type_from_keyword(vn->data.lit_string.value,
                                             vn->data.lit_string.length);
            if (avt == TYPE_STR || avt == TYPE_DYN) {
              declared_type = TYPE_MAP;
              declared_struct_idx = (avt == TYPE_STR)
                  ? JACL_SCALAR_TYPE_IDX(TYPE_STR) : UINT32_MAX;
              vec_ref_ann = true;  /* reuse the skip-typed-resolution gate */
            }
          }
        }
        if (!vec_ref_ann) {
          if (tcoll == 1) declared_type = TYPE_TYPED_VEC;
          else if (tcoll == 2 || tcoll == 3) declared_type = TYPE_TYPED_MAP;
          else return false;
        }
        if (!vec_ref_ann) {
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

  /* Phase B: monomorphize an inline proc-literal RHS against the declared
   * [Proc [P…] R] param types. The value walk above typed the body with its
   * OWN (untyped → dyn) params; re-type it ONCE more with the declared param
   * types bound, so body nodes carry typed stamps consistent with the
   * compiler's monomorphization. This is the LAST walk, so its stamps win.
   * Strict (unlike HOF inference's rollback): an explicit annotation pins the
   * types — a body that doesn't type under them is a real error. */
  if (declared_proc && value_node->type == AST_COMMAND &&
      value_node->data.command.head_id == HEAD_PROC) {
    uint32_t pac = value_node->data.command.arg_count;
    AstNode* params = (pac == 3 || pac == 4) ? value_node->data.command.args[1] : NULL;
    AstNode* body   = (pac == 4) ? value_node->data.command.args[3]
                    : (pac == 3) ? value_node->data.command.args[2] : NULL;
    if (params && body && body->type == AST_BLOCK && body->data.block.count > 0) {
      AstNode* pn[TYPER_MAX_PROC_PARAMS];
      JaclType pt[TYPER_MAX_PROC_PARAMS];
      uint32_t ps[TYPER_MAX_PROC_PARAMS];
      uint32_t pcount = typer__parse_params(tc, params, &pn, &pt, &ps);
      typer__scope_push(tc);
      for (uint32_t i = 0; i < pcount; i++) {
        uint8_t  bt = (uint8_t)pt[i];
        uint32_t bs = ps[i];
        if (pt[i] == TYPE_DYN && i < declared_proc_pcount) {
          bt = (uint8_t)declared_proc_ptypes[i];
          bs = (declared_proc_ptypes[i] == TYPE_STRUCT) ? declared_proc_pidxs[i]
                                                        : UINT32_MAX;
        }
        typer__scope_add(tc, pn[i]->data.lit_string.value,
                         pn[i]->data.lit_string.length,
                         pn[i]->scope_mark, bt, bs);
      }
      /* Push the declared return as expected_type on the TAIL so a bare
       * literal (`{99}` in `[Proc [] i64]`) narrows — mirrors handle_proc. */
      uint32_t bcount = body->data.block.count;
      for (uint32_t i = 0; i + 1 < bcount; i++)
        typer__infer_node(tc, body->data.block.commands[i]);
      JaclType saved_bet = tc->expected_type;
      if (declared_proc_rtype != TYPE_DYN) tc->expected_type = declared_proc_rtype;
      typer__infer_node(tc, body->data.block.commands[bcount - 1]);
      tc->expected_type = saved_bet;
      typer__scope_pop(tc);
    }
  }

  /* Type-check declared vs. actual. Mirrors compiler.c:6714-6726 (def)
   * and 5983-5995 (mut), but uses the shared formatters consistently.
   * Skipped for DYN declarations (decision 2 — DYN binding is itself
   * the explicit boundary marker; no cast required). Also skipped for
   * struct-to-struct since the typer doesn't yet enforce same-struct-
   * idx narrowing (compiler still owns that check). */
  if (declared_type != TYPE_DYN) {
    JaclType rhs_t = (JaclType)value_node->inferred_type;
    if (rhs_t == TYPE_DYN) {
      char err[160];
      jacl_format_assign_dyn_unnamed(err, sizeof(err), declared_type);
      typer__error(tc, name_node->start.line, name_node->start.column, err);
    } else if (rhs_t != declared_type &&
               !(declared_type == TYPE_STRUCT && rhs_t == TYPE_STRUCT)) {
      char err[160];
      jacl_format_assign_mismatch(err, sizeof(err),
          declared_type, rhs_t,
          name_node->data.lit_string.value,
          name_node->data.lit_string.length);
      typer__error(tc, name_node->start.line, name_node->start.column, err);
    } else if (declared_type == TYPE_PTR && rhs_t == TYPE_PTR &&
               declared_struct_idx != UINT32_MAX &&
               value_node->inferred_struct_idx != UINT32_MAX &&
               declared_struct_idx != value_node->inferred_struct_idx) {
      /* Both sides typed pointers but pointee idx mismatch — different
       * concrete types in spirit even though the JaclType tag matches. */
      char err[200];
      jacl_format_ptr_assign_pointee_mismatch(err, sizeof(err),
          name_node->data.lit_string.value,
          name_node->data.lit_string.length);
      typer__error(tc, name_node->start.line, name_node->start.column, err);
    }
  }

  /* Effective type: declared wins. For an untyped def/mut, mirror
   * compiler.c:7013-7019: only unboxed scalars (i64/u64/f64), structs,
   * streams, and typed collections are inherited from the RHS; tagged
   * scalars (i32/u32/f32/bool/etc.) collapse to DYN.
   *
   * TYPE_BUF and TYPE_PTR are also inherited when the RHS is a typed-
   * constructor form (`[[Buf N T] ...]`, `[ptr-null [Ptr T]]`,
   * `[ptr-cast [Ptr T] $u]`, `[addr $p->field]`, etc.), letting users
   * drop the redundant LHS annotation. See BUFFER_DESIGN.md "LHS type
   * inference". */
  JaclType effective;
  if (declared_type != TYPE_DYN) {
    effective = declared_type;
  } else {
    JaclType rhs_t = (JaclType)value_node->inferred_type;
    if (is_unboxed_type(rhs_t) || rhs_t == TYPE_STRUCT ||
        rhs_t == TYPE_STREAM || is_typed_collection(rhs_t) ||
        rhs_t == TYPE_FUTURE || rhs_t == TYPE_BOX ||
        rhs_t == TYPE_BUF || rhs_t == TYPE_PTR ||
        rhs_t == TYPE_ARR || rhs_t == TYPE_TYPED_ARR) {
      effective = rhs_t;
    } else {
      effective = TYPE_DYN;
    }
  }
  uint32_t struct_idx = UINT32_MAX;
  uint32_t key_struct_idx = UINT32_MAX;
  uint32_t inherited_buf_len = 0;
  /* Ref-element [Vec T] (e.g. [Vec str]): plain-vec rep carrying a static
   * element stamp — propagate it into the binding so var-ref reads keep the
   * element type and for-loops over the binding narrow. */
  if (effective == TYPE_DYN && (JaclType)value_node->inferred_type == TYPE_VEC &&
      value_node->inferred_struct_idx != UINT32_MAX) {
    effective = TYPE_VEC;
  }
  if (effective == TYPE_VEC &&
      value_node->inferred_struct_idx != UINT32_MAX) {
    struct_idx = value_node->inferred_struct_idx;
  }
  /* Ref-kind [Map ...] forms ([Map str], [Map K str], [Map str dyn]):
   * plain-map rep carrying static key/value stamps — propagate both into
   * the binding so var-ref reads keep them (mirrors the [Vec str] arm). */
  if (effective == TYPE_DYN &&
      (JaclType)value_node->inferred_type == TYPE_MAP &&
      (value_node->inferred_struct_idx != UINT32_MAX ||
       value_node->inferred_key_struct_idx != UINT32_MAX)) {
    effective = TYPE_MAP;
  }
  if (effective == TYPE_MAP) {
    if (declared_struct_idx != UINT32_MAX)
      struct_idx = declared_struct_idx;  /* def [Map K str] annotation wins */
    else if (value_node->inferred_struct_idx != UINT32_MAX)
      struct_idx = value_node->inferred_struct_idx;
    if (value_node->inferred_key_struct_idx != UINT32_MAX)
      key_struct_idx = value_node->inferred_key_struct_idx;
  }
  /* Nested compound VALUE map ([Map str [Vec i64]] …): the AST value
   * stamp is UINT32_MAX by the cross-registry rule — re-derive the
   * TYPER-SIDE value shape from the ctor head into the BINDING (mirrors
   * the [Vec [Vec T]] block above). Key stamp flows via the TYPE_MAP arm. */
  if ((JaclType)value_node->inferred_type == TYPE_MAP &&
      value_node->inferred_struct_idx == UINT32_MAX &&
      value_node->type == AST_COMMAND &&
      value_node->data.command.head &&
      value_node->data.command.head->type == AST_COMMAND &&
      value_node->data.command.head->data.command.head &&
      value_node->data.command.head->data.command.head->type ==
          AST_LIT_STRING &&
      value_node->data.command.head->data.command.head
          ->data.lit_string.length == 3 &&
      memcmp(value_node->data.command.head->data.command.head
                 ->data.lit_string.value, "Map", 3) == 0 &&
      value_node->data.command.head->data.command.arg_count == 2 &&
      value_node->data.command.head->data.command.args[1]->type ==
          AST_COMMAND) {
    uint32_t vsh = typer__nested_elem_shape(
        tc, value_node->data.command.head->data.command.args[1]);
    if (vsh != UINT32_MAX) {
      effective = TYPE_MAP;
      struct_idx = vsh;  /* typer-side shape — binding-only */
    }
  }
  /* Typed stream ([Stream T] / derived transform/filter/take streams):
   * propagate the element stamp into the binding so a def-bound stream
   * keeps narrowing at downstream consumers (for-loops, chained HOFs,
   * collect). Struct elements especially NEED this — an unstamped
   * struct-element stream compiles the dyn consumer path and trips the
   * runtime "requires a typed consumer" guard. */
  if (effective == TYPE_STREAM &&
      value_node->inferred_struct_idx != UINT32_MAX) {
    struct_idx = value_node->inferred_struct_idx;
  }
  /* Ref-element [Arr T] ([Arr str]): dyn-arr rep with a static element
   * stamp — propagate into the binding (mirrors the [Vec str] arm). */
  if (effective == TYPE_DYN &&
      (JaclType)value_node->inferred_type == TYPE_ARR &&
      value_node->inferred_struct_idx != UINT32_MAX) {
    effective = TYPE_ARR;
  }
  if (effective == TYPE_ARR) {
    if (declared_struct_idx != UINT32_MAX)
      struct_idx = declared_struct_idx;  /* def [Arr str] annotation wins */
    else if (value_node->inferred_struct_idx != UINT32_MAX)
      struct_idx = value_node->inferred_struct_idx;
  }
  /* Nested compound element ([Vec [Vec i64]] …): the AST stamp is
   * UINT32_MAX by the cross-registry rule, so re-derive the TYPER-SIDE
   * element shape from the ctor head's type expression and carry it in the
   * BINDING (typer bindings may hold typer shape idxs; AST may not). */
  if ((JaclType)value_node->inferred_type == TYPE_VEC &&
      value_node->inferred_struct_idx == UINT32_MAX &&
      value_node->type == AST_COMMAND &&
      value_node->data.command.head &&
      value_node->data.command.head->type == AST_COMMAND &&
      value_node->data.command.head->data.command.head &&
      value_node->data.command.head->data.command.head->type ==
          AST_LIT_STRING &&
      value_node->data.command.head->data.command.head
          ->data.lit_string.length == 3 &&
      memcmp(value_node->data.command.head->data.command.head
                 ->data.lit_string.value, "Vec", 3) == 0 &&
      value_node->data.command.head->data.command.arg_count == 1 &&
      value_node->data.command.head->data.command.args[0]->type ==
          AST_COMMAND) {
    uint32_t esh = typer__nested_elem_shape(
        tc, value_node->data.command.head->data.command.args[0]);
    if (esh != UINT32_MAX) {
      effective = TYPE_VEC;
      struct_idx = esh;
    }
  }
  /* `def row [vec-get $nested i]` where $nested's binding carries a
   * typer-side shape: when the element is itself a depth-2+ shape
   * ([Vec [Vec [Vec T]]] rows), it can't ride the AST stamp — re-derive
   * it into the NEW binding so the next get/for level narrows too. */
  if ((JaclType)value_node->inferred_type == TYPE_VEC &&
      value_node->inferred_struct_idx == UINT32_MAX &&
      value_node->type == AST_COMMAND &&
      value_node->data.command.head_id == HEAD_VEC_GET &&
      value_node->data.command.arg_count >= 1 &&
      value_node->data.command.args[0]->type == AST_VAR_REF) {
    AstNode* rv = value_node->data.command.args[0];
    const TyperBinding* vb = typer__scope_resolve(tc,
        rv->data.var_ref.name, rv->data.var_ref.length, rv->scope_mark);
    if (vb && vb->type == TYPE_VEC && vb->struct_idx != UINT32_MAX &&
        !JACL_IS_SCALAR_TYPE_IDX(vb->struct_idx) &&
        vb->struct_idx >= TYPER_MAX_STRUCTS &&
        vb->struct_idx < tc->shape_reg.count) {
      uint32_t iv = UINT32_MAX, ik = UINT32_MAX;
      JaclType ek = typer__buf_elem_decode(tc, vb->struct_idx, &iv, &ik);
      if (ek == TYPE_TYPED_VEC && !JACL_IS_SCALAR_TYPE_IDX(iv) &&
          iv >= TYPER_MAX_STRUCTS && iv < tc->shape_reg.count) {
        effective = TYPE_VEC;
        struct_idx = iv;  /* typer-side inner shape — binding-only */
      }
    }
  }
  if (effective == TYPE_STRUCT || is_typed_collection(effective) ||
      effective == TYPE_FUTURE || effective == TYPE_PTR ||
      effective == TYPE_BOX || effective == TYPE_BUF ||
      effective == TYPE_TYPED_ARR) {
    /* Declared struct (def Point r ...) / typed-collection elem
     * (def [Vec Point] ps ...) / pointer pointee
     * (def [Ptr Point] p ...) / buf elem (def [Buf N T] b ...) wins;
     * otherwise inherit from RHS. */
    if (declared_struct_idx != UINT32_MAX) {
      struct_idx = declared_struct_idx;
    } else {
      struct_idx = value_node->inferred_struct_idx;
    }
    /* Decomposed nested-buf chain: when the RHS is an intermediate
     * arrow on a nested buf chain (e.g. `let p = $cube->1`), the AST
     * node's inferred_struct_idx is UINT32_MAX by design (cross-registry
     * rule). Re-walk the chain here to get the typer-side remaining
     * shape idx and store it on the binding so subsequent `$p->...`
     * uses can re-decode the shape. */
    if (effective == TYPE_PTR && struct_idx == UINT32_MAX &&
        value_node->type == AST_COMMAND &&
        value_node->data.command.head_id == HEAD_DOT &&
        value_node->data.command.arg_count == 2) {
      JaclType chain_t = TYPE_DYN;
      uint32_t chain_sidx = UINT32_MAX;
      uint32_t remaining_shape = UINT32_MAX;
      AstNode* recv = value_node->data.command.args[0];
      AstNode* fld  = value_node->data.command.args[1];
      if (typer__nested_buf_chain_result(tc, recv, fld,
                                         &chain_t, &chain_sidx,
                                         &remaining_shape) &&
          chain_t == TYPE_PTR && remaining_shape != UINT32_MAX) {
        struct_idx = remaining_shape;
      }
    }
    /* Typed-map: also inherit key idx from RHS. */
    if (effective == TYPE_TYPED_MAP) {
      key_struct_idx = value_node->inferred_key_struct_idx;
    }
    /* Buf: also inherit N from RHS when not declared. */
    if (effective == TYPE_BUF && declared_buf_len == 0) {
      inherited_buf_len = value_node->inferred_buf_len;
    }
  }

  typer__scope_add(tc, name_node->data.lit_string.value,
                   name_node->data.lit_string.length,
                   name_node->scope_mark,
                   (uint8_t)effective,
                   struct_idx);
  /* scope_add doesn't take key_struct_idx as a param; patch in place. */
  if (key_struct_idx != UINT32_MAX && tc->binding_count > 0) {
    tc->bindings[tc->binding_count - 1].key_struct_idx = key_struct_idx;
  }
  /* Phase B: stash the typed-closure signature on the binding so a call
   * `[f args]` narrows to the declared return + checks args (via the
   * bound_proxy at the call dispatch). */
  /* Record f's declared signature for an inline-literal RHS (B1/B2) OR a named
   * typed-closure RHS (`def [Proc …] f $g`, B3d — conformance enforced by the
   * compiler). Either way f's signature is the declared annotation. */
  if (declared_proc && effective == TYPE_CLOSURE && tc->binding_count > 0) {
    TyperBinding* b = &tc->bindings[tc->binding_count - 1];
    b->is_typed_closure = true;
    b->proc_param_count = declared_proc_pcount;
    for (uint32_t i = 0; i < declared_proc_pcount &&
                         i < TYPER_MAX_PROC_PARAMS; i++)
      b->proc_param_types[i] = (uint8_t)declared_proc_ptypes[i];
    b->proc_return_type = (uint8_t)declared_proc_rtype;
    b->proc_return_struct_idx = declared_proc_ridx;
  }
  /* B3b: `def f [make-adder 5]` — the RHS is a call returning a [Proc …]
   * (typer stamped the call node TYPE_CLOSURE + return shape). Stamp f as a
   * typed closure from that signature so `[f …]` narrows to the declared
   * return (via the bound_proxy). */
  if (!declared_proc && (JaclType)value_node->inferred_type == TYPE_CLOSURE &&
      value_node->inferred_struct_idx != UINT32_MAX && tc->binding_count > 0) {
    uint8_t pc2 = 0, rt2 = (uint8_t)TYPE_DYN, pts2[TYPER_MAX_PROC_PARAMS];
    if (typer__decode_proc_shape(tc, value_node->inferred_struct_idx,
                                 &pc2, pts2, &rt2)) {
      TyperBinding* b = &tc->bindings[tc->binding_count - 1];
      b->is_typed_closure = true;
      b->proc_param_count = pc2;
      for (uint8_t k = 0; k < pc2 && k < TYPER_MAX_PROC_PARAMS; k++)
        b->proc_param_types[k] = pts2[k];
      b->proc_return_type = rt2;
      b->proc_return_struct_idx = UINT32_MAX;
    }
  }
  /* Same patch-in-place for buf_len. See BUFFER_DESIGN.md.
   * Picks the declared length when annotated; otherwise the length
   * inferred from a typed-RHS constructor. inner_len M (for the
   * nested [Buf N [Buf M T]] form) is always inherited from the RHS
   * constructor when present -- the LHS-inferred form is the only
   * way `inner_buf_len` gets populated here since the explicit
   * `def [Buf N [Buf M T]] NAME` path goes through the buf-form
   * branch above and stamps inner_len directly. M4.2. */
  if (effective == TYPE_BUF && tc->binding_count > 0) {
    uint32_t bl = declared_buf_len > 0 ? declared_buf_len : inherited_buf_len;
    if (bl > 0) tc->bindings[tc->binding_count - 1].buf_len = bl;
    /* [Buf N [Vec T]] typed-elem-T: stash on the binding's key_struct_idx
     * so buf-get can narrow the result to TYPE_TYPED_VEC + T. M4.4 ext. */
    if (declared_buf_typed_elem_idx != UINT32_MAX) {
      tc->bindings[tc->binding_count - 1].key_struct_idx =
          declared_buf_typed_elem_idx;
    }
    if (value_node->inferred_buf_inner_len > 0) {
      tc->bindings[tc->binding_count - 1].buf_inner_len =
          value_node->inferred_buf_inner_len;
    }
  }

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

  /* Arrow form: `set $recv->field val` parses as
   * `set [. $recv field] val`. The compiler's HEAD_SET rewrite later
   * morphs this into a 3-arg dot field-set; we don't see that tree
   * yet, so detect the pre-rewrite shape here and apply the same
   * field-type check as the 3-arg dot handler in
   * typer__infer_command_inner. Covers the ctx-field-set arrow form
   * (`set $ctx->pwd val`) plus arbitrary chained struct field-set. */
  if (target->type == AST_COMMAND &&
      target->data.command.head &&
      target->data.command.head->type == AST_LIT_STRING &&
      target->data.command.head->data.lit_string.length == 1 &&
      target->data.command.head->data.lit_string.value[0] == '.' &&
      target->data.command.arg_count == 2) {
    /* handle_set runs before the args walk in typer__infer_command_inner
     * (early-return dispatch), so type target and value ourselves. Typing
     * target as a 2-arg dot recursively types its sub-args (the receiver
     * and the field literal). We re-derive the receiver struct from
     * target's args[0] to look up the field's *declared* type
     * (target->inferred_type would be the field's value type — what's
     * stored, not what we want for the check). */
    typer__infer_node(tc, target);
    typer__infer_node(tc, value);
    AstNode* recv  = target->data.command.args[0];
    AstNode* field = target->data.command.args[1];
    JaclType recv_t    = (JaclType)recv->inferred_type;
    uint32_t recv_sidx = recv->inferred_struct_idx;
    /* Bare-name fallback: the parser produces a bare LIT_STRING for
     * the innermost name in arrow chains (the compiler's rewrite
     * later converts to a var-ref). Resolve it directly so the
     * type check fires on the pre-rewrite tree. */
    if (recv_t != TYPE_STRUCT && recv->type == AST_LIT_STRING &&
        recv->data.lit_string.length > 0) {
      const TyperBinding* b = typer__scope_resolve(tc,
          recv->data.lit_string.value,
          recv->data.lit_string.length,
          recv->scope_mark);
      if (b && b->type == TYPE_STRUCT) {
        recv_t = TYPE_STRUCT;
        recv_sidx = b->struct_idx;
      }
    }
    /* Stage 5b: auto-deref [Ptr Struct] receiver for the set-arrow
     * form too, so `set $p->x val` field-checks against the pointee. */
    if (recv_t == TYPE_PTR && recv_sidx != UINT32_MAX &&
        !JACL_IS_SCALAR_TYPE_IDX(recv_sidx)) {
      recv_t = TYPE_STRUCT;
    }
    if (recv_t == TYPE_STRUCT && recv_sidx < tc->struct_count &&
        field->type == AST_LIT_STRING) {
      const TyperStruct* sd = &tc->structs[recv_sidx];
      const char* fn  = field->data.lit_string.value;
      uint32_t    fnl = field->data.lit_string.length;
      for (uint32_t fi = 0; fi < sd->field_count; fi++) {
        if (sd->field_name_lens[fi] != fnl ||
            memcmp(sd->field_names[fi], fn, fnl) != 0) continue;
        JaclType field_t = (JaclType)sd->field_types[fi];
        JaclType val_t   = (JaclType)value->inferred_type;
        if (field_t != TYPE_DYN && val_t != TYPE_DYN &&
            val_t != field_t &&
            !(field_t == TYPE_STRUCT && val_t == TYPE_STRUCT)) {
          char err[224];
          jacl_format_field_mismatch(err, sizeof(err),
              sd->name, sd->name_len, fn, fnl, field_t, val_t);
          typer__error(tc, value->start.line, value->start.column, err);
        } else if (field_t != TYPE_DYN && val_t == TYPE_DYN) {
          char err[256];
          jacl_format_field_dyn_assign(err, sizeof(err),
              sd->name, sd->name_len, fn, fnl, field_t);
          typer__error(tc, value->start.line, value->start.column, err);
        }
        break;
      }
    } else if (recv_t == TYPE_BUF && recv_sidx != UINT32_MAX &&
               field->type == AST_LIT_INT) {
      /* `set $b->i val` -- arrow buf-set. Same value-vs-element type
       * check as the [buf-set ...] builtin form above. */
      typer__check_buf_set_value(tc, recv_sidx, value);
    }
    node->inferred_type = TYPE_NIL;
    return true;
  }

  /* Resolve target's type. For `set name value`, the name is parsed as a
   * bare string literal (AST_LIT_STRING). For `$name :: value`, it's a
   * var-ref. Both forms — plus the AST_COMMAND arrow form (handled
   * above) — should look up the binding. */
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

  /* Type-check value vs. target binding's type. Mirrors compiler.c:
   * 6204-6221 (local mut path) etc. Same rules as def: skip when
   * target is DYN (boundary marker), skip struct-to-struct (compiler
   * still owns same-struct-idx narrowing). */
  if (target_type != TYPE_DYN && tname) {
    JaclType rhs_t = (JaclType)value->inferred_type;
    if (rhs_t == TYPE_DYN) {
      char err[160];
      jacl_format_assign_dyn_named(err, sizeof(err), target_type, tname, tlen);
      typer__error(tc, target->start.line, target->start.column, err);
    } else if (rhs_t != target_type &&
               !(target_type == TYPE_STRUCT && rhs_t == TYPE_STRUCT)) {
      char err[160];
      jacl_format_assign_mismatch(err, sizeof(err),
          target_type, rhs_t, tname, tlen);
      typer__error(tc, target->start.line, target->start.column, err);
    }
  }

  node->inferred_type = TYPE_NIL;
  return true;
}

/* Phase B3a: intern a [Proc [P…] R] annotation as a TYPE_SHAPE_PROC in the
 * typer's shape registry; returns the shape idx (or UINT32_MAX). *out_supported
 * is false for struct/compound param/return types (B3b). Param/return idxs are
 * scalar sentinels, consistent with the compiler's intern. */
static uint32_t typer__intern_proc_shape(TyperCtx* tc, AstNode* node,
                                         bool* out_supported) {
  uint8_t  pcount;
  JaclType pts[TYPER_MAX_PROC_PARAMS];
  uint32_t pidxs[TYPER_MAX_PROC_PARAMS];
  JaclType rt; uint32_t ridx;
  if (!typer__proc_type(tc, node, &pcount, pts, pidxs, &rt, &ridx, out_supported))
    return UINT32_MAX;
  if (!*out_supported) return UINT32_MAX;
  uint32_t pool[TYPER_MAX_PROC_PARAMS];
  for (uint8_t k = 0; k < pcount; k++) pool[k] = JACL_SCALAR_TYPE_IDX(pts[k]);
  return type_shape_intern_proc(&tc->shape_reg, pool, pcount,
                                JACL_SCALAR_TYPE_IDX(rt));
}

/* Phase B3a: decode a TYPE_SHAPE_PROC shape idx into a flat signature for
 * stamping a TyperBinding (so a body call `[f …]` narrows via the proxy). */
static bool typer__decode_proc_shape(TyperCtx* tc, uint32_t shape_idx,
                                     uint8_t* out_pcount, uint8_t* out_ptypes,
                                     uint8_t* out_rtype) {
  StructTypeRegistry* reg = &tc->shape_reg;
  if (shape_idx >= reg->count || reg->shapes[shape_idx].kind != TYPE_SHAPE_PROC)
    return false;
  TypeShape* s = &reg->shapes[shape_idx];
  uint16_t pc = s->u.proc.param_count;
  if (pc > TYPER_MAX_PROC_PARAMS) pc = TYPER_MAX_PROC_PARAMS;
  *out_pcount = (uint8_t)pc;
  for (uint16_t k = 0; k < pc; k++) {
    uint32_t pidx = reg->proc_param_pool[s->u.proc.params_off + k];
    out_ptypes[k] = JACL_IS_SCALAR_TYPE_IDX(pidx)
                      ? (uint8_t)JACL_TYPE_IDX_TO_SCALAR(pidx) : (uint8_t)TYPE_DYN;
  }
  uint32_t rid = s->u.proc.return_idx;
  *out_rtype = JACL_IS_SCALAR_TYPE_IDX(rid)
                 ? (uint8_t)JACL_TYPE_IDX_TO_SCALAR(rid) : (uint8_t)TYPE_DYN;
  return true;
}

/* Phase B3a: monomorphize an inline proc literal against a declared param-type
 * list (a closure ARG to a [Proc …] closure param). Types the body ONCE with
 * untyped params bound to the declared types and the declared return pushed on
 * the tail — so body nodes carry typed stamps the compiler's monomorphization
 * agrees with (literal narrowing, typed param reads). Mirrors the def-site
 * monomorphization walk in handle_def_or_mut. */
static void typer__monomorphize_proc_literal(TyperCtx* tc, AstNode* literal,
                                             const uint8_t* ptypes,
                                             uint8_t pcount, uint8_t rtype) {
  if (!literal || literal->type != AST_COMMAND ||
      literal->data.command.head_id != HEAD_PROC) return;
  uint32_t ac = literal->data.command.arg_count;
  AstNode* params = (ac == 3 || ac == 4) ? literal->data.command.args[1] : NULL;
  AstNode* body   = (ac == 4) ? literal->data.command.args[3]
                  : (ac == 3) ? literal->data.command.args[2] : NULL;
  if (!params || !body || body->type != AST_BLOCK ||
      body->data.block.count == 0) return;
  AstNode* pn[TYPER_MAX_PROC_PARAMS];
  JaclType pt[TYPER_MAX_PROC_PARAMS];
  uint32_t ps[TYPER_MAX_PROC_PARAMS];
  uint32_t pc = typer__parse_params(tc, params, &pn, &pt, &ps);
  typer__scope_push(tc);
  for (uint32_t i = 0; i < pc; i++) {
    uint8_t bt = (uint8_t)pt[i];
    if (pt[i] == TYPE_DYN && i < pcount) bt = ptypes[i];
    typer__scope_add(tc, pn[i]->data.lit_string.value,
                     pn[i]->data.lit_string.length, pn[i]->scope_mark,
                     bt, UINT32_MAX);
  }
  uint32_t bc = body->data.block.count;
  for (uint32_t i = 0; i + 1 < bc; i++)
    typer__infer_node(tc, body->data.block.commands[i]);
  JaclType saved = tc->expected_type;
  if ((JaclType)rtype != TYPE_DYN) tc->expected_type = (JaclType)rtype;
  typer__infer_node(tc, body->data.block.commands[bc - 1]);
  tc->expected_type = saved;
  typer__scope_pop(tc);
  literal->inferred_type = TYPE_CLOSURE;
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
       * typed collection; [Ptr T] resolves to a typed pointer; anything
       * else falls back to dyn. Element/pointee struct_idx is
       * propagated so vec-get/map-get/auto-deref on the param can
       * narrow the result. */
      uint32_t ptr_sidx;
      if (typer__ptr_type(tc, elem, &ptr_sidx)) {
        fi++;
        if (fi >= flat_n) break;
        AstNode* nameelem = flat[fi];
        if (nameelem->type != AST_LIT_STRING) continue;
        (*name_nodes_out)[count]   = nameelem;
        (*types_out)[count]        = TYPE_PTR;
        (*struct_idxs_out)[count]  = ptr_sidx;
        count++;
        continue;
      }
      /* Phase B3a: [Proc [P…] R] closure param → TYPE_CLOSURE + interned shape
       * idx (so the body call narrows). Unsupported (struct/compound) types are
       * errored at the compiler; here they fall back to dyn so the typer won't
       * false-narrow. */
      if (elem->data.command.head &&
          elem->data.command.head->type == AST_LIT_STRING &&
          elem->data.command.head->data.lit_string.length == 4 &&
          memcmp(elem->data.command.head->data.lit_string.value, "Proc", 4) == 0) {
        bool sup = true;
        uint32_t shape_idx = typer__intern_proc_shape(tc, elem, &sup);
        fi++;
        if (fi >= flat_n) break;
        AstNode* nameelem = flat[fi];
        if (nameelem->type != AST_LIT_STRING) continue;
        (*name_nodes_out)[count]  = nameelem;
        (*types_out)[count]       = (sup && shape_idx != UINT32_MAX)
                                      ? TYPE_CLOSURE : TYPE_DYN;
        (*struct_idxs_out)[count] = (sup && shape_idx != UINT32_MAX)
                                      ? shape_idx : UINT32_MAX;
        count++;
        continue;
      }
      if (typer__map_v_form(elem)) {
        typer__error(tc, elem->start.line, elem->start.column,
                     TYPER_MAP_V_REMOVED_MSG);
      }
      int tcoll = typer__typed_collection_kind(elem);
      JaclType t = TYPE_DYN;
      uint32_t elem_sidx = UINT32_MAX;
      uint32_t arr_p_idx = UINT32_MAX;
      bool is_arr_param = (tcoll == 0) && typer__arr_type(tc, elem, &arr_p_idx);
      if (tcoll == 1) t = TYPE_TYPED_VEC;
      else if (tcoll == 2 || tcoll == 3) t = TYPE_TYPED_MAP;
      else if (is_arr_param) { t = TYPE_TYPED_ARR; elem_sidx = arr_p_idx; }
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
      /* Ref-kind element/value types resolve to the PLAIN rep (the
       * erasure scheme): [Vec str] param → TYPE_VEC + str stamp,
       * [Vec dyn] → plain vec, [Arr str/dyn] → TYPE_ARR (+stamp),
       * [Map K str/dyn] → TYPE_MAP + value stamp. Mirrors the def
       * annotation resolvers. */
      if (elem_sidx != UINT32_MAX && JACL_IS_SCALAR_TYPE_IDX(elem_sidx)) {
        JaclType es = JACL_TYPE_IDX_TO_SCALAR(elem_sidx);
        if (es == TYPE_STR || es == TYPE_DYN) {
          if (t == TYPE_TYPED_VEC)      t = TYPE_VEC;
          else if (t == TYPE_TYPED_MAP) t = TYPE_MAP;
          else if (t == TYPE_TYPED_ARR) t = TYPE_ARR;
          if (es == TYPE_DYN) elem_sidx = UINT32_MAX;
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

/* Resolve a proc/extern return-type AST node to (type, struct_idx).
 * Handles all annotation shapes: type keyword (i32, str, ...),
 * struct name (Point), and compound forms ([Ptr T], [Future T],
 * [Vec T], [Map K V]). Falls back to (TYPE_DYN, UINT32_MAX) on
 * unrecognized shapes — the caller can leave the proc untyped. */
static void typer__resolve_return_type(TyperCtx* tc, AstNode* tn,
                                       JaclType* out_type,
                                       uint32_t* out_struct_idx) {
  *out_type = TYPE_DYN;
  *out_struct_idx = UINT32_MAX;
  if (!tn) return;
  if (tn->type == AST_LIT_STRING) {
    const char* nm = tn->data.lit_string.value;
    uint32_t    nl = tn->data.lit_string.length;
    if (is_type_keyword(nm, nl)) {
      *out_type = type_from_keyword(nm, nl);
      return;
    }
    for (uint32_t si = 0; si < tc->struct_count; si++) {
      if (tc->structs[si].name_len == nl &&
          memcmp(tc->structs[si].name, nm, nl) == 0) {
        *out_type = TYPE_STRUCT;
        *out_struct_idx = si;
        return;
      }
    }
    return;
  }
  if (tn->type == AST_COMMAND) {
    uint32_t sidx;
    /* Phase B3b: [Proc [P…] R] return type → TYPE_CLOSURE + interned shape so
     * a closure-returning proc's binding/call narrows. */
    if (tn->data.command.head && tn->data.command.head->type == AST_LIT_STRING &&
        tn->data.command.head->data.lit_string.length == 4 &&
        memcmp(tn->data.command.head->data.lit_string.value, "Proc", 4) == 0) {
      bool sup = true;
      uint32_t pshape = typer__intern_proc_shape(tc, tn, &sup);
      if (sup && pshape != UINT32_MAX) {
        *out_type = TYPE_CLOSURE;
        *out_struct_idx = pshape;
      }
      return;
    }
    if (typer__ptr_type(tc, tn, &sidx)) {
      *out_type = TYPE_PTR;
      *out_struct_idx = sidx;
      return;
    }
    if (typer__future_type(tc, tn, &sidx)) {
      *out_type = TYPE_FUTURE;
      *out_struct_idx = sidx;
      return;
    }
    if (typer__box_type(tc, tn, &sidx)) {
      *out_type = TYPE_BOX;
      *out_struct_idx = sidx;
      return;
    }
    if (typer__stream_type(tc, tn, &sidx)) {
      *out_type = TYPE_STREAM;
      *out_struct_idx = sidx;
      return;
    }
    int tcoll = typer__typed_collection_kind(tn);
    /* Ref-element / nested-element [Vec T] annotations (vec is [Vec dyn]):
     * [Vec str] / [Vec dyn] -> TYPE_VEC (+ str stamp); a compound element
     * ([Vec [Vec T]]) -> TYPE_VEC + TYPER-SIDE shape idx (binding-only;
     * never reaches AST stamps). typed_collection_kind returns 0 for
     * compound elements, so check the head directly for that case. */
    if (tcoll == 1 && tn->data.command.args[0]->type == AST_LIT_STRING &&
        is_type_keyword(tn->data.command.args[0]->data.lit_string.value,
                        tn->data.command.args[0]->data.lit_string.length)) {
      JaclType ann_et = type_from_keyword(
          tn->data.command.args[0]->data.lit_string.value,
          tn->data.command.args[0]->data.lit_string.length);
      if (ann_et == TYPE_STR || ann_et == TYPE_DYN) {
        *out_type = TYPE_VEC;
        *out_struct_idx = (ann_et == TYPE_STR)
            ? JACL_SCALAR_TYPE_IDX(TYPE_STR) : UINT32_MAX;
        return;
      }
    }
    if (tcoll == 0 && tn->data.command.head &&
        tn->data.command.head->type == AST_LIT_STRING &&
        tn->data.command.head->data.lit_string.length == 3 &&
        memcmp(tn->data.command.head->data.lit_string.value, "Vec", 3) == 0 &&
        tn->data.command.arg_count == 1 &&
        tn->data.command.args[0]->type == AST_COMMAND) {
      uint32_t esh = typer__nested_elem_shape(tc, tn->data.command.args[0]);
      if (esh != UINT32_MAX) {
        *out_type = TYPE_VEC;
        *out_struct_idx = esh;
        return;
      }
    }
    if (typer__map_v_form(tn)) {
      typer__error(tc, tn->start.line, tn->start.column,
                   TYPER_MAP_V_REMOVED_MSG);
      return;
    }
    if (tcoll == 1 || tcoll == 2 || tcoll == 3) {
      *out_type = (tcoll == 1) ? TYPE_TYPED_VEC : TYPE_TYPED_MAP;
      AstNode* type_arg = (tcoll == 3)
          ? tn->data.command.args[1]
          : tn->data.command.args[0];
      if (type_arg && type_arg->type == AST_LIT_STRING) {
        const char* nm = type_arg->data.lit_string.value;
        uint32_t    nl = type_arg->data.lit_string.length;
        if (is_type_keyword(nm, nl)) {
          *out_struct_idx = JACL_SCALAR_TYPE_IDX(type_from_keyword(nm, nl));
        } else {
          for (uint32_t si = 0; si < tc->struct_count; si++) {
            if (tc->structs[si].name_len == nl &&
                memcmp(tc->structs[si].name, nm, nl) == 0) {
              *out_struct_idx = si;
              break;
            }
          }
        }
      }
      return;
    }
  }
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
  /* New layout: [name params (ret_type)? body]. */
  if (argc == 4) {
    typer__resolve_return_type(tc, args[2], &return_type, &return_struct_idx);
    name_idx = 0; params_idx = 1; body_idx = 3;
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
    /* Phase B3a: a [Proc …] closure param carries a shape idx — decode it onto
     * the binding so a body call `[f …]` narrows to the declared return (via
     * the typed-closure bound_proxy at the call dispatch). */
    if (pt[i] == TYPE_CLOSURE && ps[i] != UINT32_MAX && tc->binding_count > 0) {
      TyperBinding* b = &tc->bindings[tc->binding_count - 1];
      uint8_t pc2, rt2; uint8_t ptypes2[TYPER_MAX_PROC_PARAMS];
      if (typer__decode_proc_shape(tc, ps[i], &pc2, ptypes2, &rt2)) {
        b->is_typed_closure = true;
        b->proc_param_count = pc2;
        for (uint8_t k = 0; k < pc2 && k < TYPER_MAX_PROC_PARAMS; k++)
          b->proc_param_types[k] = ptypes2[k];
        b->proc_return_type = rt2;
        b->proc_return_struct_idx = UINT32_MAX;
      }
    }
  }

  /* Stash the enclosing generator's stream-element idx so [yield X] in
   * this body can type-check X against the declared element type.
   * UINT32_MAX for non-generators or [Stream dyn] — yields stay lenient. */
  uint32_t saved_yield_idx = tc->yield_elem_struct_idx;
  tc->yield_elem_struct_idx =
      (return_type == TYPE_STREAM) ? return_struct_idx : UINT32_MAX;

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
    AstNode* tail_stmt = body->data.block.commands[body_count - 1];
    /* Phase B3b: a [Proc …]-returning proc whose body tail is an inline proc
     * literal monomorphizes that literal against the declared return signature
     * (so the returned closure's body types with the declared params — captures
     * resolve from the enclosing scope). */
    bool tail_mono = false;
    if (return_type == TYPE_CLOSURE && return_struct_idx != UINT32_MAX &&
        tail_stmt->type == AST_COMMAND &&
        tail_stmt->data.command.head_id == HEAD_PROC) {
      uint8_t pc2 = 0, rt2 = (uint8_t)TYPE_DYN, pts2[TYPER_MAX_PROC_PARAMS];
      if (typer__decode_proc_shape(tc, return_struct_idx, &pc2, pts2, &rt2)) {
        typer__monomorphize_proc_literal(tc, tail_stmt, pts2, pc2, rt2);
        tail_stmt->inferred_struct_idx = return_struct_idx;
        tail_mono = true;
      }
    }
    if (!tail_mono) typer__infer_node(tc, tail_stmt);
    tc->expected_type = saved_et;
    if (!body->data.block.trailing_semi) {
      AstNode* last = body->data.block.commands[body_count - 1];
      body->inferred_type = last->inferred_type;
      body->inferred_struct_idx = last->inferred_struct_idx;
    } else {
      body->inferred_type = TYPE_NIL;
    }
  }

  /* Check declared return type vs body's tail type. Mirrors
   * compiler.c:7272-7293: peek through a tail-position AST_RETURN to
   * the returned value's type, then compare. Skipped for procs with
   * no declared return and for generators (return_type synthesized to
   * TYPE_STREAM).
   *
   * Two error cases:
   *  - Concrete-mismatch: declared and body tail are both concrete
   *    but different — long-standing rule.
   *  - Dyn-into-typed-return: body tail is dyn, declared is concrete.
   *    Per decision 2's commitment-site rule, an explicit
   *    `[to T $val]` cast is required at the tail. */
  if (return_type != TYPE_DYN && return_type != TYPE_STREAM &&
      body->type == AST_BLOCK && body->data.block.count > 0 &&
      !body->data.block.trailing_semi) {
    AstNode* tail = body->data.block.commands[body->data.block.count - 1];
    JaclType body_t = TYPE_DYN;
    AstNode* tail_value = NULL;
    if (tail->type == AST_RETURN && tail->data.return_stmt.value) {
      tail_value = tail->data.return_stmt.value;
      body_t = (JaclType)tail_value->inferred_type;
    } else if (tail->type != AST_RETURN) {
      tail_value = tail;
      body_t = (JaclType)tail->inferred_type;
    }
    if (body_t == TYPE_DYN && tail_value) {
      char err[224];
      jacl_format_proc_return_dyn(err, sizeof(err),
          name_node->data.lit_string.value,
          name_node->data.lit_string.length,
          return_type);
      typer__error(tc, tail_value->start.line, tail_value->start.column, err);
    } else if (body_t != TYPE_DYN && body_t != return_type &&
        !(return_type == TYPE_STRUCT && body_t == TYPE_STRUCT)) {
      char err[200];
      jacl_format_proc_return_mismatch(err, sizeof(err),
          name_node->data.lit_string.value,
          name_node->data.lit_string.length,
          return_type, body_t);
      typer__error(tc, name_node->start.line, name_node->start.column, err);
    }
  }

  typer__scope_pop(tc);
  tc->yield_elem_struct_idx = saved_yield_idx;
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
                                            AstNode** nodes, uint32_t count,
                                            StructTypeRegistry* seed) {
  /* Ctx always has at least the built-in `pwd` field (see compiler.c
   * ctx_field_list__init), so always register the ctx struct in the
   * pre-reserved slot 1. User AST_CTX_DECL nodes append more fields. */
  uint32_t ctx_idx = 1;
  TyperStruct* s = &tc->structs[ctx_idx];
  s->name = "ctx";
  s->name_len = 3;
  s->field_count = 0;

  /* Built-in: mut str pwd */
  s->field_types[s->field_count]      = (uint8_t)TYPE_STR;
  s->field_names[s->field_count]      = "pwd";
  s->field_name_lens[s->field_count]  = 3;
  s->field_struct_idxs[s->field_count] = UINT32_MAX;
  s->field_count++;

  /* Seed from prior-compile ctx fields. The persistent struct
   * registry's ctx StructTypeDef has every field declared by a
   * `ctx <type> <name>` in any prior jacl_eval; without this, eval
   * #2's `$ctx->name` fails the typer with "no field 'name' on
   * ctx" even though the runtime ctx struct still holds it. */
  if (seed && seed->ctx_type_idx < seed->count && seed->defs[seed->ctx_type_idx]) {
    StructTypeDef* csd = seed->defs[seed->ctx_type_idx];
    for (uint32_t fi = 0; fi < csd->field_count &&
                       s->field_count < TYPER_MAX_STRUCT_FIELDS; fi++) {
      const char* fn  = csd->fields[fi].name;
      uint32_t    fnl = csd->fields[fi].name_len;
      bool exists = false;
      for (uint32_t exf = 0; exf < s->field_count; exf++) {
        if (s->field_name_lens[exf] == fnl &&
            memcmp(s->field_names[exf], fn, fnl) == 0) {
          exists = true; break;
        }
      }
      if (exists) continue;
      s->field_types[s->field_count]       = (uint8_t)csd->fields[fi].type;
      s->field_names[s->field_count]       = fn;
      s->field_name_lens[s->field_count]   = fnl;
      s->field_struct_idxs[s->field_count] = csd->fields[fi].struct_type_idx;
      s->field_count++;
    }
  }

  for (uint32_t ni = 0; ni < count && s->field_count < TYPER_MAX_STRUCT_FIELDS; ni++) {
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
    s->field_types[s->field_count]       = (uint8_t)ft;
    s->field_names[s->field_count]       = node->data.ctx_decl.field_name;
    s->field_name_lens[s->field_count]   = node->data.ctx_decl.field_name_len;
    s->field_struct_idxs[s->field_count] = f_struct_idx;
    s->field_count++;
  }
  return ctx_idx;
}

/* Pre-pass: collect struct definitions so struct constructor calls
 * (which propagate field types to args) can resolve. */
/* Register an anonymous inline struct from a canonical string like
 * "struct{x:i32,y:i32}". Mirrors compiler__register_inline_struct so
 * the typer's struct_idx for each anonymous struct aligns with the
 * compiler's (both register on demand in source order during defstruct
 * walk). Returns the typer idx, or UINT32_MAX on parse error. */
static uint32_t typer__register_inline_struct(TyperCtx* tc,
                                               const char* spec, uint32_t spec_len) {
  /* Already registered? Look up by canonical string. */
  for (uint32_t i = 0; i < tc->struct_count; i++) {
    if (tc->structs[i].name_len == spec_len &&
        memcmp(tc->structs[i].name, spec, spec_len) == 0) {
      return i;
    }
  }
  /* Validate "struct{...}" wrapper */
  if (spec_len < 9 || memcmp(spec, "struct{", 7) != 0 || spec[spec_len - 1] != '}')
    return UINT32_MAX;
  if (tc->struct_count >= TYPER_MAX_STRUCTS) return UINT32_MAX;

  /* Reserve slot and parse fields. */
  uint32_t idx = tc->struct_count++;
  TyperStruct* s = &tc->structs[idx];
  s->name        = spec;
  s->name_len    = spec_len;
  s->field_count = 0;

  const char* p   = spec + 7;
  const char* end = spec + spec_len - 1;
  while (p < end && s->field_count < TYPER_MAX_STRUCT_FIELDS) {
    /* Field name up to ':' */
    const char* colon = p;
    while (colon < end && *colon != ':') colon++;
    if (colon >= end) return UINT32_MAX;
    uint32_t fname_len = (uint32_t)(colon - p);
    const char* fname = p;

    /* Field type up to ',' or end (handle nested struct{} braces) */
    const char* tstart = colon + 1;
    const char* tp = tstart;
    int depth = 0;
    while (tp < end) {
      if (*tp == '{') depth++;
      else if (*tp == '}') { if (depth == 0) break; depth--; }
      else if (*tp == ',' && depth == 0) break;
      tp++;
    }
    uint32_t tlen = (uint32_t)(tp - tstart);

    JaclType ftype = TYPE_DYN;
    uint32_t f_struct_idx = UINT32_MAX;
    if (is_type_keyword(tstart, tlen)) {
      ftype = type_from_keyword(tstart, tlen);
    } else if (tlen > 7 && memcmp(tstart, "struct{", 7) == 0) {
      uint32_t nested = typer__register_inline_struct(tc, tstart, tlen);
      if (nested == UINT32_MAX) return UINT32_MAX;
      ftype = TYPE_STRUCT;
      f_struct_idx = nested;
      /* re-fetch: recursive registration may have moved tc->structs base
       * (no — fixed-size array), but reassign s in case. */
      s = &tc->structs[idx];
    } else {
      /* Named struct — resolve in pass 2 (or now if already registered) */
      const TyperStruct* found = typer__find_struct(tc, tstart, tlen);
      if (found) {
        ftype = TYPE_STRUCT;
        f_struct_idx = (uint32_t)(found - tc->structs);
      }
    }
    uint32_t fi = s->field_count++;
    s->field_types[fi]       = (uint8_t)ftype;
    s->field_names[fi]       = fname;
    s->field_name_lens[fi]   = fname_len;
    s->field_struct_idxs[fi] = f_struct_idx;

    p = tp;
    if (p < end && *p == ',') p++;
  }
  return idx;
}

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
      uint32_t f_sidx = UINT32_MAX;
      if (is_type_keyword(tn, tl)) {
        ft = type_from_keyword(tn, tl);
      } else if (tl > 4 && memcmp(tn, "Buf{", 4) == 0 &&
                 tn[tl - 1] == '}') {
        /* Buf field (M4.3): canonical "Buf{N,T}". Decode element
         * encoding now; T must be a scalar keyword or already-
         * registered struct (pass-2 resolution NYI for buf elem). */
        ft = TYPE_BUF;
        const char* p = tn + 4;
        const char* end = tn + tl - 1;
        while (p < end && *p >= '0' && *p <= '9') p++;
        if (p < end && *p == ',') {
          p++;
          uint32_t elen = (uint32_t)(end - p);
          if (is_type_keyword(p, elen)) {
            f_sidx = JACL_SCALAR_TYPE_IDX(type_from_keyword(p, elen));
          } else {
            const TyperStruct* found = typer__find_struct(tc, p, elen);
            if (found) f_sidx = (uint32_t)(found - tc->structs);
          }
        }
      } else {
        /* Nested struct — type set to TYPE_STRUCT here; struct_idx
         * resolved in pass 2 below. */
        ft = TYPE_STRUCT;
      }
      s->field_types[i]        = (uint8_t)ft;
      s->field_names[i]        = node->data.defstruct.field_names[i];
      s->field_name_lens[i]    = node->data.defstruct.field_name_lens[i];
      s->field_struct_idxs[i]  = f_sidx;
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
      if (tl > 7 && memcmp(tn, "struct{", 7) == 0) {
        /* Anonymous inline struct field — register on demand so the
         * typer's struct_idx aligns with the compiler's (which calls
         * compiler__register_inline_struct in the same defstruct walk). */
        uint32_t nested = typer__register_inline_struct(tc, tn, tl);
        /* Re-fetch s in case TyperStruct array layout shifts (it
         * doesn't — fixed-size — but defensive). */
        s = &tc->structs[(uint32_t)(s - tc->structs)];
        if (nested != UINT32_MAX) s->field_struct_idxs[i] = nested;
      } else {
        const TyperStruct* found = typer__find_struct(tc, tn, tl);
        if (found) {
          s->field_struct_idxs[i] = (uint32_t)(found - tc->structs);
        }
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
    if (!head || head->type != AST_LIT_STRING) continue;
    bool is_proc =
        head->data.lit_string.length == 4 &&
        memcmp(head->data.lit_string.value, "proc", 4) == 0;
    bool is_extern =
        head->data.lit_string.length == 6 &&
        memcmp(head->data.lit_string.value, "extern", 6) == 0;
    if (!is_proc && !is_extern) continue;

    AstNode** args = node->data.command.args;
    uint32_t  argc = node->data.command.arg_count;
    uint32_t  name_idx, params_idx;
    JaclType  return_type = TYPE_DYN;
    uint32_t  return_struct_idx = UINT32_MAX;
    /* Layout (June 2026 redesign §4):
     *   proc   name params (TYPE)? body     (argc==3 or 4; TYPE at [2])
     *   extern name params (TYPE)?          (argc==2 or 3; TYPE at [2]) */
    if (is_extern) {
      if (argc == 3) {
        typer__resolve_return_type(tc, args[2], &return_type, &return_struct_idx);
        name_idx = 0; params_idx = 1;
      } else if (argc == 2) {
        name_idx = 0; params_idx = 1;
      } else continue;
    } else {
      if (argc == 4) {
        typer__resolve_return_type(tc, args[2], &return_type, &return_struct_idx);
        name_idx = 0; params_idx = 1;
      } else if (argc == 3) {
        name_idx = 0; params_idx = 1;
      } else continue;
    }

    AstNode* name_node = args[name_idx];
    if (name_node->type != AST_LIT_STRING) continue;

    if (tc->proc_count >= TYPER_MAX_PROCS) break;
    TyperProc* p = &tc->procs[tc->proc_count++];
    p->name        = name_node->data.lit_string.value;
    p->name_len    = name_node->data.lit_string.length;

    /* Generator detection: only meaningful for procs with a body —
     * externs are host-provided so we trust the declared return type. */
    if (!is_extern && return_type == TYPE_DYN) {
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
    if (pcount > TYPER_MAX_PROC_PARAMS) pcount = TYPER_MAX_PROC_PARAMS;
    p->param_count = (uint8_t)pcount;
    for (uint32_t i = 0; i < pcount; i++) {
      p->param_types[i] = (uint8_t)pt[i];
      p->param_struct_idxs[i] = ps[i];
    }

    /* Nested procs: recurse into the proc body so inner `proc`
     * declarations are visible from sibling positions inside the
     * same scope. The typer's proc registry is flat, so a nested
     * proc with a name colliding with a top-level proc would shadow
     * the top-level one — same behavior as the runtime, which
     * resolves names lexically. Externs have no body, skip them. */
    if (!is_extern) {
      uint32_t body_idx = (argc == 4) ? 3 : 2;
      if (body_idx < argc && args[body_idx]->type == AST_BLOCK) {
        AstNode* body = args[body_idx];
        typer__register_procs(tc, body->data.block.commands,
                              body->data.block.count);
      }
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

/* Look up an imported export reached through a module binding
 * (`use "lib" m` → `[$m->fn args]` or `$m->field`). Returns NULL
 * if no entry matches both the binding name and the field name.
 * Procs and values share this lookup; the caller uses `arity` to
 * tell them apart (>= 0 → proc, < 0 → value).
 *
 * The compiler stuffs all imports (top-level destructuring and
 * module-binding) into the same flat array; the binding column
 * disambiguates which form an entry belongs to. */
static const TyperImportProc* typer__find_bound_export(TyperCtx* tc,
                                                       const char* binding, uint32_t binding_len,
                                                       const char* name, uint32_t name_len) {
  if (!tc->imports) return NULL;
  for (uint32_t i = 0; i < tc->import_count; i++) {
    TyperImportProc* imp = &tc->imports[i];
    if (!imp->binding) continue;
    if (imp->binding_len != binding_len) continue;
    if (memcmp(imp->binding, binding, binding_len) != 0) continue;
    if (imp->name_len != name_len) continue;
    if (memcmp(imp->name, name, name_len) != 0) continue;
    return imp;
  }
  return NULL;
}

/* Infer the return type of a `[\ body]` lambda with its implicit $it param
 * bound to a known element type. Used by `transform` to type the output stream
 * by the lambda's return type (a scoped case of anon-closure return typing).
 *
 * A lambda `[\ * $it 10]` parses as a HEAD_NONE command with head "\" and args
 * [*, $it, 10]; its body is the application reconstructed as {head: args[0],
 * args: args[1..]}. We bind $it to it_enc's type, type that body, and read its
 * inferred type. Returns the element encoding (scalar sentinel / struct idx)
 * or UINT32_MAX (dyn) when the lambda isn't a recognizable single-expression
 * form. */
static uint32_t typer__proc_result_enc(TyperCtx* tc, AstNode* proc,
                                       const uint32_t* arg_encs,
                                       uint32_t argc, bool* out_param_wide) {
  /* Infer the result-type encoding of applying an inline proc to arguments
   * of known type at a call site. `proc` is a proc command (head HEAD_PROC;
   * layout [name params (rettype)? body]); `arg_encs[i]` is the type encoding
   * of the i-th call-site argument (JACL_SCALAR_TYPE_IDX sentinel / struct
   * registry idx / UINT32_MAX = dyn). Binds the proc's OWN params to those
   * arg types and returns the body tail's type encoding (UINT32_MAX = dyn).
   *
   * `out_param_wide` (nullable): set true iff a param was bound to a wide
   * scalar (i64/u64/f64) AND the body typed cleanly with that binding — i.e.
   * the body was monomorphized and reads the param wide. Callers use this to
   * decide whether the runtime can hand the value over wide (no box). false
   * when the body fell back to dyn (see rollback below) or no param is wide.
   *
   * General, not lambda-specific: it reads real param names via
   * typer__parse_params, so a hand-written inline `proc {^x} { … }` gets the
   * same inference as a `[\ … ]` lambda. The `\` prelude macro stays pure
   * sugar — the typer has no knowledge of its expansion (no hardcoded `it`). */
  if (out_param_wide) *out_param_wide = false;
  if (!proc || proc->type != AST_COMMAND) return UINT32_MAX;
  if (proc->data.command.head_id != HEAD_PROC) return UINT32_MAX;
  uint32_t ac = proc->data.command.arg_count;
  if (ac != 3 && ac != 4) return UINT32_MAX;
  AstNode* params = proc->data.command.args[1];
  AstNode* body   = proc->data.command.args[ac == 4 ? 3 : 2];
  if (!body || body->type != AST_BLOCK) return UINT32_MAX;
  uint32_t bc = body->data.block.count;
  if (bc == 0) return UINT32_MAX;

  AstNode* pn[TYPER_MAX_PROC_PARAMS];
  JaclType pt[TYPER_MAX_PROC_PARAMS];
  uint32_t ps[TYPER_MAX_PROC_PARAMS];
  uint32_t pcount = typer__parse_params(tc, params, &pn, &pt, &ps);

  /* Snapshot error state so a failed wide probe can be rolled back. Binding a
   * param wide can surface a strict-numeric error the dyn body never hits
   * (e.g. `== i32-literal i64-expr` — the typer rejects mixed unboxed
   * comparison, but the runtime is permissive). In that case we do NOT commit
   * the wide typing: roll the errors back and fall to a dyn body (the value is
   * boxed at the boundary), preserving the lenient runtime semantics. Only a
   * clean wide probe is monomorphized. */
  bool have_result = (tc->result != NULL);
  uint32_t saved_errc = have_result ? tc->result->error_count : 0;
  bool saved_first_empty = have_result ? (tc->result->first_error[0] == '\0')
                                       : true;

  /* Probe: bind each param to the call-site arg type when the param is
   * untyped (dyn — the lambda case) and an arg type was supplied; otherwise
   * keep its declared type. Type the body; the tail's type is the result. */
  typer__scope_push(tc);
  bool body_bound = false; /* a param was bound to a call-site arg type */
  for (uint32_t i = 0; i < pcount; i++) {
    uint8_t  bt = (uint8_t)pt[i];
    uint32_t bs = ps[i];
    if (i < argc && arg_encs[i] != UINT32_MAX && pt[i] == TYPE_DYN) {
      if (JACL_IS_SCALAR_TYPE_IDX(arg_encs[i])) {
        bt = (uint8_t)JACL_TYPE_IDX_TO_SCALAR(arg_encs[i]);
        bs = UINT32_MAX;
        if (bt != TYPE_DYN) body_bound = true; /* dyn element: no binding */
      } else {
        bt = (uint8_t)TYPE_STRUCT;
        bs = arg_encs[i];
        body_bound = true;
      }
    }
    typer__scope_add(tc, pn[i]->data.lit_string.value,
                     pn[i]->data.lit_string.length,
                     pn[i]->scope_mark, bt, bs);
  }
  for (uint32_t i = 0; i < bc; i++)
    typer__infer_node(tc, body->data.block.commands[i]);
  AstNode* tail = body->data.block.commands[bc - 1];
  JaclType rt = (JaclType)tail->inferred_type;
  uint32_t rsidx = tail->inferred_struct_idx;
  typer__scope_pop(tc);

  bool probe_errored = have_result && tc->result->error_count > saved_errc;
  if (probe_errored) {
    tc->result->error_count = saved_errc;
    if (saved_first_empty) tc->result->first_error[0] = '\0';
  }

  /* Keep the element-typed body whenever a param was bound to a call-site
   * type AND the probe typed cleanly. The rep the runtime delivers matches
   * what the typed body expects for EVERY bindable element type:
   *  - wide scalar (i64/u64/f64): the producer-wide flip yields wide and
   *    the HOF pushes it straight into the wide-compiled body.
   *  - struct: the multi-slot channel + by-value struct param convention
   *    deliver the element as N inline slots (struct HOF monomorphization).
   *  - tagged-fit scalars (i32/u32/f32/bool/str): tagged on both sides —
   *    a typed body read of a non-unboxed type IS a tagged read, so the
   *    bytecode is unchanged; keeping the typed body fixes body-internal
   *    static typing (assert-type, propagation) and kills the double-walk
   *    (STREAM_TYPING_DEBT item 4, final piece).
   * The restore pass below survives ONLY as the rollback for a failed
   * probe (mixed-type bodies degrade to the lenient dyn path). */
  bool keep_typed = body_bound && !probe_errored;
  if (!keep_typed) {
    typer__scope_push(tc);
    for (uint32_t i = 0; i < pcount; i++)
      typer__scope_add(tc, pn[i]->data.lit_string.value,
                       pn[i]->data.lit_string.length,
                       pn[i]->scope_mark, (uint8_t)pt[i], ps[i]);
    for (uint32_t i = 0; i < bc; i++)
      typer__infer_node(tc, body->data.block.commands[i]);
    typer__scope_pop(tc);
  }

  if (probe_errored) return UINT32_MAX;  /* dyn output; not monomorphized */
  if (out_param_wide) *out_param_wide = keep_typed;

  if (rt == TYPE_STRUCT && rsidx != UINT32_MAX) return rsidx;
  if (rt != TYPE_DYN && rt != TYPE_NIL) return JACL_SCALAR_TYPE_IDX(rt);
  return UINT32_MAX;
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
    } else if (hid == HEAD_TRY &&
               node->data.command.arg_count == 3 &&
               node->data.command.args[0]->type == AST_BLOCK &&
               node->data.command.args[1]->type == AST_LIT_STRING &&
               node->data.command.args[2]->type == AST_BLOCK) {
      /* try body err handler — result is body's tail value (no error)
       * or handler's tail value (error). Unify the two; if they agree,
       * propagate the type. The error binding (args[1]) is the local
       * the handler scope binds the trapped error to — typed as DYN
       * since we don't track error tag types. Push a scope with the
       * binding before walking the handler so var-refs to the err
       * name resolve correctly. */
      AstNode** as = node->data.command.args;
      typer__infer_node(tc, as[0]);
      typer__scope_push(tc);
      typer__scope_add(tc, as[1]->data.lit_string.value,
                       as[1]->data.lit_string.length,
                       as[1]->scope_mark,
                       (uint8_t)TYPE_DYN, UINT32_MAX);
      typer__infer_node(tc, as[2]);
      typer__scope_pop(tc);
      JaclType body_t    = (JaclType)as[0]->inferred_type;
      JaclType handler_t = (JaclType)as[2]->inferred_type;
      if (body_t == handler_t) {
        node->inferred_type = body_t;
        node->inferred_struct_idx = as[0]->inferred_struct_idx;
      } else {
        node->inferred_type = TYPE_DYN;
      }
      return;
    } else if (hid == HEAD_WITH_CTX &&
               node->data.command.arg_count == 2 &&
               node->data.command.args[0]->type == AST_BLOCK &&
               node->data.command.args[1]->type == AST_BLOCK) {
      /* with-ctx overrides body — result is the body's tail value
       * (overrides block produces nil). Mirrors compiler.c:8214. */
      AstNode** as = node->data.command.args;
      typer__infer_node(tc, as[0]);
      typer__infer_node(tc, as[1]);
      node->inferred_type = as[1]->inferred_type;
      node->inferred_struct_idx = as[1]->inferred_struct_idx;
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
    } else if (hid == HEAD_ASSERT_TYPE) {
      /* [assert-type EXPR TYPE] — compile-time static type assertion.
       * EXPR is typer-walked so its inferred_type lands on the node;
       * the result is compared to TYPE (a bare type-keyword or a
       * registered struct name). No runtime evaluation: the compiler
       * emits a single OP_NIL and discards EXPR. Whole form has
       * static type nil. */
      AstNode** as = node->data.command.args;
      uint32_t  ac = node->data.command.arg_count;
      if (ac != 2) {
        typer__error(tc, node->start.line, node->start.column,
                     "assert-type expects 2 arguments");
        node->inferred_type = TYPE_NIL;
        return;
      }
      AstNode* expr_node = as[0];
      AstNode* type_node = as[1];
      JaclType saved_et = tc->expected_type;
      tc->expected_type = TYPE_DYN;
      typer__infer_node(tc, expr_node);
      tc->expected_type = saved_et;
      if (type_node->type != AST_LIT_STRING) {
        typer__error(tc, type_node->start.line, type_node->start.column,
                     "assert-type: second argument must be a type name");
        node->inferred_type = TYPE_NIL;
        return;
      }
      const char* tname = type_node->data.lit_string.value;
      uint32_t    tlen  = type_node->data.lit_string.length;
      JaclType actual_t = (JaclType)expr_node->inferred_type;
      JaclType expected_t = TYPE_DYN;
      bool expected_known = false;
      uint32_t expected_struct_idx = UINT32_MAX;
      if (is_type_keyword(tname, tlen)) {
        expected_t = type_from_keyword(tname, tlen);
        expected_known = true;
      } else {
        for (uint32_t si = 0; si < tc->struct_count; si++) {
          if (tc->structs[si].name_len == tlen &&
              memcmp(tc->structs[si].name, tname, tlen) == 0) {
            expected_t = TYPE_STRUCT;
            expected_struct_idx = si;
            expected_known = true;
            break;
          }
        }
      }
      if (!expected_known) {
        char err[160];
        snprintf(err, sizeof(err),
                 "assert-type: unknown type '%.*s'", (int)tlen, tname);
        typer__error(tc, type_node->start.line, type_node->start.column, err);
        node->inferred_type = TYPE_NIL;
        return;
      }
      bool match = (actual_t == expected_t);
      if (match && expected_t == TYPE_STRUCT &&
          expected_struct_idx != UINT32_MAX) {
        match = (expr_node->inferred_struct_idx == expected_struct_idx);
      }
      if (!match) {
        char err[224];
        const char* actual_name =
            (actual_t == TYPE_STRUCT &&
             expr_node->inferred_struct_idx < tc->struct_count)
            ? tc->structs[expr_node->inferred_struct_idx].name
            : type_name(actual_t);
        snprintf(err, sizeof(err),
                 "assert-type failed: expected %.*s, got %s",
                 (int)tlen, tname, actual_name);
        typer__error(tc, node->start.line, node->start.column, err);
      }
      node->inferred_type = TYPE_NIL;
      return;
    } else if (hid == HEAD_FOR &&
               (node->data.command.arg_count == 2 ||
                node->data.command.arg_count == 3)) {
      /* for [coll] { body }  /  for [coll] name { body } — narrow the loop
       * binding to the collection's element type so typed body operations
       * (typed arithmetic, assigning to a typed local, assert-type) see the
       * right type. Currently only typed arr narrows; dyn arr / vec / stream
       * keep a dyn binding (stream/vec for-binding narrowing is deferred —
       * NOT_IMPLEMENTED §4 wide-cell note), preserving existing behavior.
       * The callback/each form (args[1] is a var-ref or command) and any
       * malformed shape fall through to the generic walker. */
      AstNode** as = node->data.command.args;
      uint32_t  ac = node->data.command.arg_count;
      const char* bn = "it"; uint32_t bnl = 2;
      AstNode* body = NULL; uint32_t bmark = 0;
      if (ac == 2 && as[1]->type == AST_BLOCK) {
        body = as[1]; bmark = as[1]->scope_mark;
      } else if (ac == 3 && as[1]->type == AST_LIT_STRING &&
                 as[2]->type == AST_BLOCK) {
        bn = as[1]->data.lit_string.value;
        bnl = as[1]->data.lit_string.length;
        body = as[2]; bmark = as[1]->scope_mark;
      }
      if (body) {
        typer__infer_node(tc, as[0]);
        JaclType bt = TYPE_DYN; uint32_t bsi = UINT32_MAX;
        uint32_t for_bind_key_idx = UINT32_MAX;
        JaclType coll_t = (JaclType)as[0]->inferred_type;
        if ((coll_t == TYPE_TYPED_ARR || coll_t == TYPE_TYPED_VEC ||
             coll_t == TYPE_STREAM ||
             /* Ref-element [Vec T] / [Arr T] (e.g. [Vec str], [Arr str]):
              * plain (dyn) rep with a static element stamp — narrow the
              * binding like any typed collection. Only tagged-fit ref
              * elements can be stamped on TYPE_VEC/TYPE_ARR, so the tagged
              * binding rep is always correct. */
             coll_t == TYPE_VEC || coll_t == TYPE_ARR)) {
          uint32_t eidx = as[0]->inferred_struct_idx;
          /* Nested-element vec ([Vec [Vec i64]] …): the AST stamp is
           * suppressed by the cross-registry rule, so resolve the TYPER-SIDE
           * shape idx directly — from the binding for a var-ref receiver, or
           * re-derived from the ctor head for a literal receiver. */
          if (coll_t == TYPE_VEC && eidx == UINT32_MAX) {
            if (as[0]->type == AST_VAR_REF) {
              const TyperBinding* vb = typer__scope_resolve(tc,
                  as[0]->data.var_ref.name, as[0]->data.var_ref.length,
                  as[0]->scope_mark);
              if (vb && vb->type == TYPE_VEC) eidx = vb->struct_idx;
            } else if (as[0]->type == AST_COMMAND &&
                       as[0]->data.command.head &&
                       as[0]->data.command.head->type == AST_COMMAND &&
                       as[0]->data.command.head->data.command.head &&
                       as[0]->data.command.head->data.command.head->type ==
                           AST_LIT_STRING &&
                       as[0]->data.command.head->data.command.head
                           ->data.lit_string.length == 3 &&
                       memcmp(as[0]->data.command.head->data.command.head
                                  ->data.lit_string.value, "Vec", 3) == 0 &&
                       as[0]->data.command.head->data.command.arg_count == 1 &&
                       as[0]->data.command.head->data.command.args[0]->type ==
                           AST_COMMAND) {
              eidx = typer__nested_elem_shape(
                  tc, as[0]->data.command.head->data.command.args[0]);
            }
          }
          if (eidx == UINT32_MAX) {
            /* untyped element — binding stays dyn */
          } else if (eidx >= TYPER_MAX_STRUCTS &&
                     eidx < tc->shape_reg.count) {
            /* Typer-side shape: decode to PORTABLE inner encodings for the
             * loop binding (scalar sentinel / aligned struct idx). Inner
             * ref-kinds ([Vec str]/[Vec dyn] elements) are plain-rep'd. */
            uint32_t iv = UINT32_MAX, ik = UINT32_MAX;
            JaclType ek = typer__buf_elem_decode(tc, eidx, &iv, &ik);
            (void)ik;
            if (ek == TYPE_TYPED_VEC) {
              bool inner_ref = JACL_IS_SCALAR_TYPE_IDX(iv) &&
                               (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR ||
                                JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_DYN);
              bool inner_shape = !JACL_IS_SCALAR_TYPE_IDX(iv) &&
                                 iv >= TYPER_MAX_STRUCTS &&
                                 iv < tc->shape_reg.count;
              if (inner_ref) {
                bt = TYPE_VEC;
                bsi = (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR)
                          ? iv : UINT32_MAX;
              } else if (inner_shape) {
                /* Element is itself a nested-element vec: bind as a
                 * ref-element TYPE_VEC carrying the inner shape idx — the
                 * next loop level resolves it from this binding (the
                 * recursion point for depth-N narrowing). */
                bt = TYPE_VEC; bsi = iv;
              } else {
                bt = TYPE_TYPED_VEC; bsi = iv;
              }
            } else if (ek == TYPE_TYPED_MAP) {
              uint32_t bki;
              typer__decode_map_shape(iv, ik, &bt, &bsi, &bki);
              /* scope_add below has no key slot — patch it in after. */
              for_bind_key_idx = bki;
            }
          } else if (JACL_IS_SCALAR_TYPE_IDX(eidx)) {
            /* Narrow to the element scalar — all scalars, including wide
             * i64/u64/f64. Non-SM loops store the wide binding in a typed
             * local; SM loops store it boxed in a state field and the
             * var-ref read unboxes via the node's inferred_type. Applies to
             * [Arr T], [Vec T], and [Stream T] element bindings. */
            bt = JACL_TYPE_IDX_TO_SCALAR(eidx);
          } else if (eidx < tc->struct_count) {
            /* Struct-element narrowing: arr/vec, and now streams too — the
             * multi-slot stream channel (OP_STREAM_NEXT_INLINE) delivers the
             * element as inline value bytes and the for-loop binds it in an
             * N-slot inline local (NOT_IMPLEMENTED.md §4.1b). */
            bt = TYPE_STRUCT; bsi = eidx;
          }
        }
        typer__scope_push(tc);
        typer__scope_add(tc, bn, bnl, bmark, (uint8_t)bt, bsi);
        /* Map-element bindings carry the key idx too (scope_add has no
         * key slot — patch in place, like the def handler). */
        if (for_bind_key_idx != UINT32_MAX && tc->binding_count > 0) {
          tc->bindings[tc->binding_count - 1].key_struct_idx =
              for_bind_key_idx;
        }
        typer__infer_node(tc, body);
        typer__scope_pop(tc);
        node->inferred_type = TYPE_NIL;
        return;
      }
      /* Callback/each form: [for $coll [\ body]] / [for $coll $cb]. Type the
       * collection and the callback; for an inline lambda over a typed wide
       * stream, monomorphize the callback body (type its param wide) and stamp
       * it so the compiler bakes the wide param rep onto OP_EACH (the pull then
       * hands the element over wide, no box). Mirrors HEAD_FILTER. A var-ref
       * callback or a fallback (mixed-type) body stays dyn → boxed. */
      if (ac == 2 && (as[1]->type == AST_COMMAND ||
                      as[1]->type == AST_VAR_REF)) {
        typer__infer_node(tc, as[0]);
        /* Type-once: an inline callback over a typed stream is typed ONLY
         * via proc_result_enc below (param bound to the element type) —
         * a generic walk first would type the body with the param unbound
         * and record spurious sticky errors. Mirrors the transform/filter
         * pre-walk skip. */
        bool cb_inline_typed_stream =
            (as[1]->type == AST_COMMAND &&
             as[1]->data.command.head_id == HEAD_PROC &&
             (JaclType)as[0]->inferred_type == TYPE_STREAM &&
             as[0]->inferred_struct_idx != UINT32_MAX);
        if (!cb_inline_typed_stream) typer__infer_node(tc, as[1]);
        if (as[1]->type == AST_COMMAND) {
          uint32_t cb_enc = UINT32_MAX;
          if (cb_inline_typed_stream) {
            uint32_t arg_enc = as[0]->inferred_struct_idx;
            bool cb_typed = false;
            (void)typer__proc_result_enc(tc, as[1], &arg_enc, 1, &cb_typed);
            if (cb_typed) cb_enc = arg_enc;
            /* The skipped pre-walk would have stamped this (handle_proc). */
            as[1]->inferred_type = TYPE_CLOSURE;
          }
          as[1]->inferred_struct_idx = cb_enc;
        }
        node->inferred_type = TYPE_NIL;
        return;
      }
      /* else: malformed — fall through to generic. */
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
      /* Concrete-mismatch (both sides non-DYN, different types):
       *  - Arithmetic (+ - * / %): always error per decision 1
       *    (no implicit widening; explicit cast required).
       *  - Comparison (== < > etc.) of unboxed scalars (i64/u64/f64):
       *    error to mirror the compiler at compiler.c:3859-3868
       *    (unboxed values can't go through dynamic dispatch).
       *  - Comparison of tagged scalars: still allowed; cross-type
       *    equality is meaningful (always false) and tests rely on it.
       *  - Mixed dyn/typed: stays permissive (decision 2 deferred). */
      /* TYPE_PTR-specific rules (Stage 5a):
       *  - Arithmetic with any pointer operand → error (use [ptr-offset]).
       *  - Comparison of two TYPE_PTR values → require matching pointee
       *    idx; otherwise it's a type error.
       *  - Comparison of TYPE_PTR with a non-pointer concrete type
       *    falls through to the generic concrete-mismatch check below. */
      if ((lhs_t == TYPE_PTR || rhs_t == TYPE_PTR) && is_arith) {
        char err[160];
        jacl_format_ptr_arithmetic(err, sizeof(err));
        typer__error(tc, lhs->start.line, lhs->start.column, err);
      } else if (lhs_t == TYPE_PTR && rhs_t == TYPE_PTR && is_cmp &&
                 lhs->inferred_struct_idx != rhs->inferred_struct_idx &&
                 lhs->inferred_struct_idx != UINT32_MAX &&
                 rhs->inferred_struct_idx != UINT32_MAX) {
        char err[160];
        jacl_format_ptr_compare_pointee_mismatch(err, sizeof(err));
        typer__error(tc, lhs->start.line, lhs->start.column, err);
      }
      bool concrete_mismatch = (lhs_t != rhs_t &&
                                lhs_t != TYPE_DYN && rhs_t != TYPE_DYN);
      bool unboxed_either = is_unboxed_type(lhs_t) || is_unboxed_type(rhs_t);
      if (concrete_mismatch && (is_arith || unboxed_either)) {
        const char* verb = is_cmp ? "compare" :
                           (hname[0] == '+' ? "add" :
                            hname[0] == '-' ? "subtract" :
                            hname[0] == '*' ? "multiply" :
                            hname[0] == '/' ? "divide" : "compute");
        char err[160];
        snprintf(err, sizeof(err),
                 "type error: cannot %s %s and %s",
                 verb, type_name(lhs_t), type_name(rhs_t));
        typer__error(tc, lhs->start.line, lhs->start.column, err);
      }
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
  /* Stack-allocated proxy for module-binding-resolved imports. The
   * generic dispatch below reads through `proc`, and an imported
   * proc reached via `[$mod->fn args]` has the same downstream
   * shape as a local proc — so we copy the resolved
   * TyperImportProc into this proxy and point `proc` at it. The
   * proxy's lifetime spans this function, which is sufficient. */
  TyperProc bound_proxy;
  if (head && head->type == AST_LIT_STRING) {
    proc = typer__find_proc(tc,
        head->data.lit_string.value, head->data.lit_string.length);
    if (!proc) {
      /* Phase B: a typed-closure binding (`def [Proc …] f …`) called by name
       * narrows to its declared signature via a proxy, exactly like a named
       * proc. A plain (untyped) closure binding has is_typed_closure=false and
       * falls through to the dyn path (status quo). */
      const TyperBinding* cb = typer__scope_resolve(tc,
          head->data.lit_string.value, head->data.lit_string.length,
          head->scope_mark);
      if (cb && cb->is_typed_closure) {
        bound_proxy.name              = cb->name;
        bound_proxy.name_len          = cb->name_len;
        bound_proxy.return_type       = cb->proc_return_type;
        bound_proxy.return_struct_idx = cb->proc_return_struct_idx;
        bound_proxy.param_count       = cb->proc_param_count;
        for (uint32_t i = 0; i < cb->proc_param_count &&
                             i < TYPER_MAX_PROC_PARAMS; i++) {
          bound_proxy.param_types[i]       = cb->proc_param_types[i];
          bound_proxy.param_struct_idxs[i] = UINT32_MAX;
        }
        proc = &bound_proxy;
      }
    }
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
  } else if (head && head->type == AST_COMMAND &&
             head->data.command.head_id == HEAD_DOT &&
             head->data.command.arg_count == 2 &&
             head->data.command.args[0]->type == AST_VAR_REF &&
             head->data.command.args[1]->type == AST_LIT_STRING) {
    /* Module-binding call: `[$mod->fn args]` parses as an outer
     * command whose head is a HEAD_DOT command with a var-ref
     * receiver and a literal field name. Resolve via the imports
     * array if `$mod` doesn't shadow a local binding. */
    AstNode* recv = head->data.command.args[0];
    AstNode* fld  = head->data.command.args[1];
    const TyperBinding* shadow = typer__scope_resolve(tc,
        recv->data.var_ref.name, recv->data.var_ref.length,
        recv->scope_mark);
    if (!shadow) {
      const TyperImportProc* bound = typer__find_bound_export(tc,
          recv->data.var_ref.name, recv->data.var_ref.length,
          fld->data.lit_string.value, fld->data.lit_string.length);
      /* Only callable exports (arity >= 0) drive the call dispatch;
       * value imports reached through `[$mod->VALUE]` (which is rare
       * and runtime-erroneous anyway) fall through to dyn here. */
      if (bound && bound->arity >= 0) {
        bound_proxy.name              = bound->name;
        bound_proxy.name_len          = bound->name_len;
        bound_proxy.return_type       = bound->return_type;
        bound_proxy.return_struct_idx = UINT32_MAX;
        bound_proxy.param_count       = bound->param_count;
        for (uint32_t i = 0; i < bound->param_count && i < TYPER_MAX_PROC_PARAMS; i++) {
          bound_proxy.param_types[i] = bound->param_types[i];
        }
        proc = &bound_proxy;
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
  /* [[Arr T] ...] flexes element literals just like [[Vec T] ...]; detect it
   * separately since typed_collection_kind only knows vec/map. */
  bool ctor_is_arr = false;
  if (ctor_kind == 0 && head && head->type == AST_COMMAND) {
    uint32_t arr_ctor_ei;
    if (typer__arr_type(tc, head, &arr_ctor_ei)) {
      ctor_is_arr = true;
      AstNode* elem_node = head->data.command.args[0];
      if (elem_node && elem_node->type == AST_LIT_STRING &&
          is_type_keyword(elem_node->data.lit_string.value,
                          elem_node->data.lit_string.length)) {
        ctor_elem_t = type_from_keyword(elem_node->data.lit_string.value,
                                        elem_node->data.lit_string.length);
      }
    }
  }
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
  /* Compound-VALUE map ctor ([[Map i64 [Vec i64]] k v …]): the kind
   * recognizer needs LIT_STRING args and returns 0 — derive the key
   * expectation directly so key literals narrow to the declared type
   * (values are collections; they need no literal flexing). */
  if (ctor_kind == 0 && head && head->type == AST_COMMAND &&
      head->data.command.head &&
      head->data.command.head->type == AST_LIT_STRING &&
      head->data.command.head->data.lit_string.length == 3 &&
      memcmp(head->data.command.head->data.lit_string.value, "Map", 3)
          == 0 &&
      head->data.command.arg_count == 2 &&
      head->data.command.args[0]->type == AST_LIT_STRING &&
      head->data.command.args[1]->type == AST_COMMAND &&
      is_type_keyword(head->data.command.args[0]->data.lit_string.value,
                      head->data.command.args[0]->data.lit_string.length)) {
    ctor_kind = 3;  /* reuse the even/odd key/value expected-type split */
    ctor_key_t = type_from_keyword(
        head->data.command.args[0]->data.lit_string.value,
        head->data.command.args[0]->data.lit_string.length);
  }

  /* For typed-collection mutators (vec-push/-set, map-set/-remove/-get/-has),
   * once args[0] (the receiver) is typed, derive elem_t / key_t from its
   * inferred_struct_idx / inferred_key_struct_idx so subsequent value/key
   * args narrow correctly. Scalar idx → scalar JaclType; struct idx →
   * TYPE_STRUCT. */
  HeadId mutator_hid = (head && head->type == AST_LIT_STRING)
                       ? (HeadId)node->data.command.head_id : HEAD_NONE;

  for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
    AstNode* arg = node->data.command.args[i];
    /* Type-once for monomorphized HOF callbacks: an inline proc passed to
     * transform/filter over a typed stream is typed by the HOF case below
     * (typer__proc_result_enc, with its param bound to the element type).
     * Skip it here — this generic walk would type the body with the param
     * UNBOUND (dyn), recording spurious sticky errors (e.g. an in-body
     * assert-type) and paying an extra body walk. The skip condition
     * mirrors the HOF case's exactly (args[0] is typed before i==1, so its
     * stream/elem verdict is available). */
    if (i == 1 && node->data.command.arg_count == 2 &&
        (mutator_hid == HEAD_TRANSFORM || mutator_hid == HEAD_FILTER) &&
        (JaclType)node->data.command.args[0]->inferred_type == TYPE_STREAM &&
        node->data.command.args[0]->inferred_struct_idx != UINT32_MAX &&
        arg->type == AST_COMMAND &&
        arg->data.command.head_id == HEAD_PROC) {
      continue;
    }
    JaclType saved_et = tc->expected_type;
    if (proc && i < proc->param_count) {
      tc->expected_type = (JaclType)proc->param_types[i];
    } else if (sdef) {
      /* Named struct constructor: args are field/value pairs. The
       * value position is at odd `i`; the preceding even-position arg
       * carries the field name. Look up the field and push its type
       * so literal narrowing fires. Positional struct constructors
       * are no longer accepted — only the named form. */
      if ((i & 1u) == 1) {
        AstNode* fname_node = node->data.command.args[i - 1];
        if (fname_node->type == AST_LIT_STRING) {
          for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
            if (sdef->field_name_lens[fi] == fname_node->data.lit_string.length &&
                memcmp(sdef->field_names[fi], fname_node->data.lit_string.value,
                       fname_node->data.lit_string.length) == 0) {
              tc->expected_type = (JaclType)sdef->field_types[fi];
              break;
            }
          }
        }
      }
    } else if ((ctor_kind == 1 || ctor_is_arr) && ctor_elem_t != TYPE_DYN) {
      tc->expected_type = ctor_elem_t;
    } else if ((ctor_kind == 2 || ctor_kind == 3) &&
               (ctor_elem_t != TYPE_DYN || ctor_key_t != TYPE_DYN)) {
      /* Map ctor: alternating key/value pairs. Even idx → key, odd → val. */
      tc->expected_type = (i % 2 == 0) ? ctor_key_t : ctor_elem_t;
    } else if (i > 0 && (mutator_hid == HEAD_VEC_PUSH ||
                         mutator_hid == HEAD_VEC_SET ||
                         mutator_hid == HEAD_ARR_PUSH ||
                         mutator_hid == HEAD_ARR_SET ||
                         mutator_hid == HEAD_MAP_SET ||
                         mutator_hid == HEAD_MAP_REMOVE ||
                         mutator_hid == HEAD_MAP_GET ||
                         mutator_hid == HEAD_MAP_HAS)) {
      AstNode* recv = node->data.command.args[0];
      JaclType recv_t = (JaclType)recv->inferred_type;
      uint32_t e_idx = recv->inferred_struct_idx;
      uint32_t k_idx = recv->inferred_key_struct_idx;
      JaclType target = TYPE_DYN;
      /* arr-push i=1 → elem; arr-set i=1 → idx (dyn), i=2 → elem.
       * TYPE_ARR with a stamp = ref-element [Arr str]: same narrowing. */
      if (recv_t == TYPE_TYPED_ARR || recv_t == TYPE_ARR) {
        if (mutator_hid == HEAD_ARR_PUSH ||
            (mutator_hid == HEAD_ARR_SET && i == 2)) {
          if (e_idx != UINT32_MAX && JACL_IS_SCALAR_TYPE_IDX(e_idx)) {
            target = JACL_TYPE_IDX_TO_SCALAR(e_idx);
          }
        }
      }
      /* Pick the right slot:
       *   vec-push i=1 → elem
       *   vec-set  i=1 → idx (DYN), i=2 → elem
       *   map-set  i=1 → key, i=2 → val
       *   map-remove/get/has i=1 → key
       */
      if (recv_t == TYPE_VEC && e_idx != UINT32_MAX &&
          JACL_IS_SCALAR_TYPE_IDX(e_idx) &&
          (mutator_hid == HEAD_VEC_PUSH ||
           (mutator_hid == HEAD_VEC_SET && i == 2))) {
        /* Stamped ref-element vec ([Vec str]): pushed/assigned values take
         * the element type as expected_type (literals narrow; mismatched
         * concrete values surface through the generic checks). */
        target = JACL_TYPE_IDX_TO_SCALAR(e_idx);
      }
      if (recv_t == TYPE_TYPED_VEC) {
        if (mutator_hid == HEAD_VEC_PUSH ||
            (mutator_hid == HEAD_VEC_SET && i == 2)) {
          if (e_idx != UINT32_MAX && JACL_IS_SCALAR_TYPE_IDX(e_idx)) {
            target = JACL_TYPE_IDX_TO_SCALAR(e_idx);
          }
        }
      } else if (recv_t == TYPE_TYPED_MAP || recv_t == TYPE_MAP) {
        /* TYPE_MAP with stamps = ref-kind [Map ...] form on the plain rep
         * ([Map str], [Map i64 str]): same static key/value narrowing as
         * the typed rep. Unstamped plain maps have UINT32_MAX idxs and
         * fall through to dyn. */
        bool is_key_slot =
            (mutator_hid == HEAD_MAP_REMOVE ||
             mutator_hid == HEAD_MAP_GET ||
             mutator_hid == HEAD_MAP_HAS ||
             (mutator_hid == HEAD_MAP_SET && i == 1));
        bool is_val_slot = (mutator_hid == HEAD_MAP_SET && i == 2);
        if (is_key_slot && k_idx != UINT32_MAX &&
            JACL_IS_SCALAR_TYPE_IDX(k_idx)) {
          target = JACL_TYPE_IDX_TO_SCALAR(k_idx);
        } else if (is_val_slot && e_idx != UINT32_MAX &&
                   JACL_IS_SCALAR_TYPE_IDX(e_idx)) {
          target = JACL_TYPE_IDX_TO_SCALAR(e_idx);
        }
      }
      tc->expected_type = target;
    } else if (mutator_hid == HEAD_YIELD && i == 0 &&
               tc->yield_elem_struct_idx != UINT32_MAX &&
               JACL_IS_SCALAR_TYPE_IDX(tc->yield_elem_struct_idx)) {
      /* yield X in a [Stream T] generator: narrow a numeric literal X to the
       * element type T (yield 1.5 in [Stream f64] -> f64; yield 100 in
       * [Stream i64] -> i64). Mirrors typed def/mut/push expected_type
       * propagation. The yield codegen boxes a wide tail before OP_YIELD_SM. */
      tc->expected_type = JACL_TYPE_IDX_TO_SCALAR(tc->yield_elem_struct_idx);
    } else {
      tc->expected_type = TYPE_DYN;
    }
    /* Phase B3a: an inline closure literal passed to a [Proc …] closure param
     * is monomorphized against the param's signature, so its body types with
     * the declared param/return (mirrors the def-site walk). Otherwise the body
     * stays dyn and the typed call at the callee reads a tagged param. */
    if (proc && i < proc->param_count &&
        (JaclType)proc->param_types[i] == TYPE_CLOSURE &&
        proc->param_struct_idxs[i] != UINT32_MAX &&
        arg->type == AST_COMMAND && arg->data.command.head_id == HEAD_PROC) {
      uint8_t pc2 = 0, rt2 = (uint8_t)TYPE_DYN, pts2[TYPER_MAX_PROC_PARAMS];
      if (typer__decode_proc_shape(tc, proc->param_struct_idxs[i],
                                   &pc2, pts2, &rt2)) {
        typer__monomorphize_proc_literal(tc, arg, pts2, pc2, rt2);
        tc->expected_type = saved_et;
        continue;
      }
    }
    typer__infer_node(tc, arg);
    tc->expected_type = saved_et;
  }
  /* §4 Phase 2: `yield $typed` in an unannotated generator (proc
   * has no `[Stream T]` return type, so the stream is implicitly
   * `[Stream dyn]`) is a compile error. Literals stay flex/dyn and
   * are allowed. The user must annotate the proc with `[Stream T]`
   * to yield typed values — no inference, mirroring proc return
   * types. */
  if (mutator_hid == HEAD_YIELD && node->data.command.arg_count == 1 &&
      tc->yield_elem_struct_idx == UINT32_MAX) {
    AstNode* arg = node->data.command.args[0];
    bool is_literal = (arg->type == AST_LIT_INT ||
                       arg->type == AST_LIT_FLOAT ||
                       arg->type == AST_LIT_STRING);
    JaclType arg_t = (JaclType)arg->inferred_type;
    if (!is_literal && arg_t != TYPE_DYN && arg_t != TYPE_NIL) {
      char err[224];
      snprintf(err, sizeof(err),
               "type error: yield of typed value (%s) in an unannotated "
               "generator; annotate the proc with `[Stream %s]` or "
               "convert the value to dyn",
               type_name(arg_t), type_name(arg_t));
      typer__error(tc, arg->start.line, arg->start.column, err);
    }
  }
  /* [yield X] inside `[Stream T]` (T != dyn): verify the yielded
   * expression's type matches T. Yield-into-[Stream dyn] stays lenient
   * (typed→dyn widens implicitly, parallel to proc returns/dyn args).
   * Only the first arg matters — yield has arity 1. */
  if (mutator_hid == HEAD_YIELD && node->data.command.arg_count == 1 &&
      tc->yield_elem_struct_idx != UINT32_MAX) {
    AstNode* arg = node->data.command.args[0];
    JaclType arg_t = (JaclType)arg->inferred_type;
    uint32_t elem_idx = tc->yield_elem_struct_idx;
    /* Integer / float literals have flex type — accepted into any
     * numeric stream element. Note: literal stays at its default
     * (i32 / f32) at codegen time. The wide-typed-i64 unboxed yield
     * path has an unrelated bug (see NOT_IMPLEMENTED.md), so we
     * deliberately don't push expected_type onto the arg upstream. */
    bool literal_flex = false;
    if (JACL_IS_SCALAR_TYPE_IDX(elem_idx)) {
      JaclType elem_t = JACL_TYPE_IDX_TO_SCALAR(elem_idx);
      if (arg->type == AST_LIT_INT && is_numeric_type(elem_t)) literal_flex = true;
      if (arg->type == AST_LIT_FLOAT && (elem_t == TYPE_F32 || elem_t == TYPE_F64))
        literal_flex = true;
      if (literal_flex) {
        /* No-op: literal is accepted. */
      } else if (arg_t != elem_t && arg_t != TYPE_DYN) {
        char err[224];
        snprintf(err, sizeof(err),
                 "type error: yield expected %s, got %s",
                 type_name(elem_t), type_name(arg_t));
        typer__error(tc, arg->start.line, arg->start.column, err);
      } else if (arg_t == TYPE_DYN) {
        char err[224];
        snprintf(err, sizeof(err),
                 "type error: yield expected %s, got dyn (use [to %s $val])",
                 type_name(elem_t), type_name(elem_t));
        typer__error(tc, arg->start.line, arg->start.column, err);
      }
    } else {
      /* Struct element type: compare via struct registry idx. The
       * typer's struct-idx alignment with the compiler holds for
       * locally-defined structs (pre-pass sets indices); imported
       * structs are still placeholder entries, in which case the
       * arg's struct_idx may be UINT32_MAX even on a real Struct
       * value. Skip the check in that case rather than false-positive. */
      if (arg_t == TYPE_DYN) {
        char err[224];
        const TyperStruct* s = (elem_idx < tc->struct_count) ? &tc->structs[elem_idx] : NULL;
        if (s) {
          snprintf(err, sizeof(err),
                   "type error: yield expected %.*s, got dyn (use [to %.*s $val])",
                   (int)s->name_len, s->name, (int)s->name_len, s->name);
        } else {
          snprintf(err, sizeof(err),
                   "type error: yield expected struct, got dyn");
        }
        typer__error(tc, arg->start.line, arg->start.column, err);
      } else if (arg_t != TYPE_STRUCT) {
        char err[224];
        const TyperStruct* s = (elem_idx < tc->struct_count) ? &tc->structs[elem_idx] : NULL;
        if (s) {
          snprintf(err, sizeof(err),
                   "type error: yield expected %.*s, got %s",
                   (int)s->name_len, s->name, type_name(arg_t));
        } else {
          snprintf(err, sizeof(err),
                   "type error: yield expected struct, got %s", type_name(arg_t));
        }
        typer__error(tc, arg->start.line, arg->start.column, err);
      } else if (arg->inferred_struct_idx != UINT32_MAX &&
                 arg->inferred_struct_idx != elem_idx) {
        const TyperStruct* expected = (elem_idx < tc->struct_count) ? &tc->structs[elem_idx] : NULL;
        const TyperStruct* got = (arg->inferred_struct_idx < tc->struct_count) ? &tc->structs[arg->inferred_struct_idx] : NULL;
        char err[224];
        snprintf(err, sizeof(err),
                 "type error: yield expected %.*s, got %.*s",
                 expected ? (int)expected->name_len : 6,
                 expected ? expected->name : "struct",
                 got ? (int)got->name_len : 6,
                 got ? got->name : "struct");
        typer__error(tc, arg->start.line, arg->start.column, err);
      }
    }
  }
  if (proc) {
    node->inferred_type = proc->return_type;
    node->inferred_struct_idx = proc->return_struct_idx;
    /* Check positional arg types against declared param types. Mirrors
     * the compiler's typed-call check (compiler.c:10359-10378). Both
     * concrete-mismatch and dyn-into-typed (decision 2: no implicit
     * coercion) fire; struct-to-struct stays compiler-owned (typer's
     * struct-idx tracking is not fully aligned across modules). */
    uint32_t argc = node->data.command.arg_count;
    AstNode** as = node->data.command.args;
    uint32_t check_n = argc < proc->param_count ? argc : proc->param_count;
    for (uint32_t i = 0; i < check_n; i++) {
      JaclType param_t = (JaclType)proc->param_types[i];
      if (param_t == TYPE_DYN) continue;
      JaclType arg_t = (JaclType)as[i]->inferred_type;
      if (arg_t == param_t) continue;
      if (param_t == TYPE_STRUCT && arg_t == TYPE_STRUCT) continue;
      /* Plain-rep collection params accept their typed siblings (a typed
       * vec/map/arr IS a vec/map/arr value through dyn; mirrors the
       * def-annotation acceptance). */
      if ((param_t == TYPE_VEC && arg_t == TYPE_TYPED_VEC) ||
          (param_t == TYPE_MAP && arg_t == TYPE_TYPED_MAP) ||
          (param_t == TYPE_ARR && arg_t == TYPE_TYPED_ARR)) continue;
      /* [Buf N T] → [Ptr T] implicit decay at call sites. The buf's
       * element type must match the param's pointee type exactly, and
       * the element must be C-ABI compatible: scalars, value-structs,
       * [Ptr U]. Ref-element bufs (M4.4: dyn/str/vec/map/closure/stream)
       * hold tagged JaclVals and are rejected -- nothing C-side
       * understands the encoding. Use [addr $b->0] explicitly if you
       * need a raw pointer into a tagged-slot buf. See BUFFER_DESIGN.md
       * M3 / M4.4. */
      if (param_t == TYPE_PTR && arg_t == TYPE_BUF) {
        uint32_t arg_elem = as[i]->inferred_struct_idx;
        if (JACL_IS_SCALAR_TYPE_IDX(arg_elem)) {
          JaclType et = JACL_TYPE_IDX_TO_SCALAR(arg_elem);
          if (et == TYPE_DYN || et == TYPE_STR || et == TYPE_VEC ||
              et == TYPE_MAP || et == TYPE_CLOSURE || et == TYPE_STREAM) {
            char err[224];
            snprintf(err, sizeof(err),
                "type error: argument %u of %.*s: [Buf N %s] cannot decay "
                "to [Ptr %s] -- reference-element bufs are not C-ABI "
                "compatible; use [addr $b->0] for an explicit raw pointer",
                i + 1, (int)proc->name_len, proc->name,
                type_name(et), type_name(et));
            typer__error(tc, as[i]->start.line, as[i]->start.column, err);
            break;
          }
        }
        uint32_t param_pointee = proc->param_struct_idxs[i];
        if (param_pointee == arg_elem ||
            param_pointee == UINT32_MAX) {
          /* Either the param doesn't constrain pointee (rare) or the
           * encoded element matches. Accept the decay; the compiler
           * emits address-of at the call site. */
          continue;
        }
      }
      char err[224];
      /* Read name from `proc` rather than `head`: for module-binding
       * calls (`[$mod->fn args]`) the head is an AST_COMMAND, not the
       * literal proc name. The name was stamped on the proxy. */
      if (arg_t == TYPE_DYN) {
        snprintf(err, sizeof(err),
                 "type error: argument %u of %.*s expected %s, got dyn (use [to %s $val])",
                 i + 1,
                 (int)proc->name_len, proc->name,
                 type_name(param_t), type_name(param_t));
      } else {
        snprintf(err, sizeof(err),
                 "type error: argument %u of %.*s expected %s, got %s",
                 i + 1,
                 (int)proc->name_len, proc->name,
                 type_name(param_t), type_name(arg_t));
      }
      typer__error(tc, as[i]->start.line, as[i]->start.column, err);
      break;
    }
  } else if (sdef) {
    node->inferred_type = TYPE_STRUCT;
    node->inferred_struct_idx = sdef_idx;
    /* Named struct constructor: args are field/value pairs. The
     * compiler enforces uniqueness, unknown-name, and buf-vs-value
     * arity errors; the typer's job here is to type-check each
     * provided value against the declared field type so call-site
     * narrowing produces the right diagnostics. */
    uint32_t argc = node->data.command.arg_count;
    AstNode** as = node->data.command.args;
    if ((argc & 1u) == 0) {
      uint32_t pair_count = argc / 2;
      for (uint32_t p = 0; p < pair_count; p++) {
        AstNode* name_node = as[p * 2];
        AstNode* val_node  = as[p * 2 + 1];
        if (name_node->type != AST_LIT_STRING) continue;
        const char* fname = name_node->data.lit_string.value;
        uint32_t    fname_len = name_node->data.lit_string.length;
        uint32_t found = UINT32_MAX;
        for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->field_name_lens[fi] == fname_len &&
              memcmp(sdef->field_names[fi], fname, fname_len) == 0) {
            found = fi;
            break;
          }
        }
        if (found == UINT32_MAX) continue;  /* compiler reports */
        JaclType field_t = (JaclType)sdef->field_types[found];
        if (field_t == TYPE_DYN) continue;
        JaclType arg_t = (JaclType)val_node->inferred_type;
        if (arg_t == field_t) continue;
        if (field_t == TYPE_STRUCT && arg_t == TYPE_STRUCT) continue;
        if (arg_t == TYPE_DYN) {
          char err[224];
          snprintf(err, sizeof(err),
                   "type error: field '%.*s' of struct '%.*s' expected %s, got dyn",
                   (int)sdef->field_name_lens[found], sdef->field_names[found],
                   (int)sdef->name_len, sdef->name,
                   type_name(field_t));
          typer__error(tc, val_node->start.line, val_node->start.column, err);
        } else {
          char err[224];
          jacl_format_field_mismatch(err, sizeof(err),
              sdef->name, sdef->name_len,
              sdef->field_names[found], sdef->field_name_lens[found],
              field_t, arg_t);
          typer__error(tc, val_node->start.line, val_node->start.column, err);
        }
        break;
      }
    }
  } else if (head && head->type == AST_COMMAND) {
    /* Typed-collection constructor: [[Vec T] e1 ...] / [[Map K V] ...].
     * Mirrors compiler__compile_command's typed-vec/typed-map branch.
     * Element struct_idx is propagated via inferred_struct_idx so
     * vec-get/map-get can narrow the result type. Scalar element types
     * (i32/i64/etc.) use the shared JACL_SCALAR_TYPE_IDX sentinel
     * encoding so the compiler can read the same idx. */
    if (typer__map_v_form(head)) {
      typer__error(tc, node->start.line, node->start.column,
                   TYPER_MAP_V_REMOVED_MSG);
      node->inferred_type = TYPE_MAP;
      return;
    }
    /* Nested compound VALUE map ctor ([[Map str [Vec i64]] ...]): the
     * value is a collection — a tagged heap value, i.e. a REF kind — so
     * the map uses the PLAIN traced rep. The key type stamps as usual;
     * the value SHAPE is interned TYPER-SIDE and lives in bindings only
     * (def re-derives it; the AST value stamp stays UINT32_MAX per the
     * cross-registry rule). Mirrors the [Vec [Vec T]] ctor. */
    if (head->data.command.head &&
        head->data.command.head->type == AST_LIT_STRING &&
        head->data.command.head->data.lit_string.length == 3 &&
        memcmp(head->data.command.head->data.lit_string.value, "Map", 3)
            == 0 &&
        head->data.command.arg_count == 2 &&
        head->data.command.args[0]->type == AST_LIT_STRING &&
        head->data.command.args[1]->type == AST_COMMAND) {
      uint32_t vsh = typer__nested_elem_shape(tc, head->data.command.args[1]);
      if (vsh != UINT32_MAX) {
        node->inferred_type = TYPE_MAP;
        node->inferred_struct_idx = UINT32_MAX;  /* shape: binding-only */
        AstNode* kn = head->data.command.args[0];
        JaclType ref_kt = TYPE_DYN;
        if (is_type_keyword(kn->data.lit_string.value,
                            kn->data.lit_string.length)) {
          ref_kt = type_from_keyword(kn->data.lit_string.value,
                                     kn->data.lit_string.length);
          if (ref_kt != TYPE_DYN)
            node->inferred_key_struct_idx = JACL_SCALAR_TYPE_IDX(ref_kt);
        }
        /* Decode the value shape to the PORTABLE expected kind. */
        uint32_t iv = UINT32_MAX, ik = UINT32_MAX;
        JaclType ekind = typer__buf_elem_decode(tc, vsh, &iv, &ik);
        JaclType want = TYPE_DYN;
        uint32_t want_idx = UINT32_MAX;
        if (ekind == TYPE_TYPED_VEC) {
          bool inner_ref = JACL_IS_SCALAR_TYPE_IDX(iv) &&
                           (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR ||
                            JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_DYN);
          bool inner_shape = !JACL_IS_SCALAR_TYPE_IDX(iv) &&
                             iv >= TYPER_MAX_STRUCTS &&
                             iv < tc->shape_reg.count;
          if (inner_ref) {
            want = TYPE_VEC;
            if (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR) want_idx = iv;
          } else if (inner_shape) {
            want = TYPE_VEC;  /* inner shape: no idx compare */
          } else {
            want = TYPE_TYPED_VEC;
            want_idx = iv;
          }
        } else if (ekind == TYPE_TYPED_MAP) {
          uint32_t mki;
          typer__decode_map_shape(iv, ik, &want, &want_idx, &mki);
          (void)mki;
        }
        for (uint32_t ei = 0; ei < node->data.command.arg_count; ei++) {
          bool is_key = (ei % 2 == 0);
          AstNode* av = node->data.command.args[ei];
          JaclType at = (JaclType)av->inferred_type;
          if (at == TYPE_DYN) continue;
          bool ok = true;
          if (is_key) {
            if (ref_kt != TYPE_DYN) ok = (at == ref_kt);
          } else {
            ok = (at == want);
            if (ok && want_idx != UINT32_MAX &&
                av->inferred_struct_idx != UINT32_MAX &&
                av->inferred_struct_idx != want_idx)
              ok = false;
          }
          if (!ok) {
            char err[192];
            snprintf(err, sizeof(err),
                     "type error: [Map ...] %s %u does not match the "
                     "declared nested %s type",
                     is_key ? "key" : "value", ei / 2,
                     is_key ? "key" : "value");
            typer__error(tc, av->start.line, av->start.column, err);
            break;
          }
        }
        return;
      }
    }
    int tc_kind = typer__typed_collection_kind(head);
    /* [[Arr T] ...] constructor: element semantics are identical to vec
     * (single element type, one element per arg), so reuse the kind==1
     * machinery below but stamp TYPE_TYPED_ARR. See ARR_DESIGN.md M4c. */
    uint32_t arr_ctor_ei;
    bool is_arr_ctor = (tc_kind == 0) && typer__arr_type(tc, head, &arr_ctor_ei);
    if (is_arr_ctor) tc_kind = 1;
    /* Nested compound element ctor ([[Vec [Vec i64]] ...]):
     * typer__typed_collection_kind requires a LIT_STRING element and
     * returns 0 — recognize the compound-element form directly so the
     * ref-element branch below handles it. */
    if (tc_kind == 0 && !is_arr_ctor && head->data.command.head &&
        head->data.command.head->type == AST_LIT_STRING &&
        head->data.command.head->data.lit_string.length == 3 &&
        memcmp(head->data.command.head->data.lit_string.value, "Vec", 3)
            == 0 &&
        head->data.command.arg_count == 1 &&
        head->data.command.args[0]->type == AST_COMMAND) {
      tc_kind = 1;
    }
    if (tc_kind == 1 || tc_kind == 2 || tc_kind == 3) {
      /* Ref-element [Vec T] (T = str or dyn): `vec` is shorthand for
       * [Vec dyn] — the whole family is legal. Ref elements share the
       * plain traced vec REP (no strided rep win exists for tagged heap
       * values); the element type lives statically as TYPE_VEC + elem
       * stamp, exactly the scheme typed streams use. [Vec dyn] IS the
       * plain vec (no stamp). Through a dyn slot the element type widens
       * to dyn — same as scalars. */
      if (tc_kind == 1 && !is_arr_ctor) {
        AstNode* en = head->data.command.args[0];
        /* Nested compound element ([Vec [Vec i64]], [Vec [Map K V]]):
         * also a REF element (collections are tagged heap values) → plain
         * traced vec rep. The element shape is interned TYPER-SIDE for
         * binding-level narrowing (def re-derives it; see the def handler);
         * the AST stamp stays UINT32_MAX per the cross-registry rule. */
        if (en && en->type == AST_COMMAND) {
          uint32_t esh = typer__nested_elem_shape(tc, en);
          if (esh != UINT32_MAX) {
            node->inferred_type = TYPE_VEC;
            node->inferred_struct_idx = UINT32_MAX;
            uint32_t iv = UINT32_MAX, ik = UINT32_MAX;
            JaclType ekind = typer__buf_elem_decode(tc, esh, &iv, &ik);
            /* Inner ref-kinds ([Vec str]/[Vec dyn] elements) are themselves
             * plain-rep'd vecs. */
            bool inner_ref = (ekind == TYPE_TYPED_VEC &&
                              JACL_IS_SCALAR_TYPE_IDX(iv) &&
                              (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR ||
                               JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_DYN));
            /* A nested element whose OWN element is a shape ([Vec [Vec T]]
             * elements of a depth-3 ctor) is itself plain-rep'd, and its AST
             * stamp is suppressed — expect TYPE_VEC with no idx compare. */
            bool inner_shape = (ekind == TYPE_TYPED_VEC &&
                                !JACL_IS_SCALAR_TYPE_IDX(iv) &&
                                iv >= TYPER_MAX_STRUCTS &&
                                iv < tc->shape_reg.count);
            if (inner_shape) { inner_ref = true; iv = UINT32_MAX; }
            JaclType want;
            if (inner_ref) {
              want = TYPE_VEC;
            } else if (ekind == TYPE_TYPED_MAP) {
              /* Rep rule for inner maps: ref-kind VALUES → plain TYPE_MAP
               * (compare against the decoded value stamp); value kinds →
               * typed rep. */
              uint32_t mki;
              typer__decode_map_shape(iv, ik, &want, &iv, &mki);
              (void)mki;
            } else {
              want = TYPE_TYPED_VEC;
            }
            for (uint32_t ei = 0; ei < node->data.command.arg_count; ei++) {
              AstNode* av = node->data.command.args[ei];
              JaclType at = (JaclType)av->inferred_type;
              bool ok = (at == TYPE_DYN) || (at == want);
              if (ok && !inner_ref && at == want &&
                  av->inferred_struct_idx != UINT32_MAX && iv != UINT32_MAX &&
                  av->inferred_struct_idx != iv)
                ok = false;
              if (!ok) {
                char err[192];
                snprintf(err, sizeof(err),
                         "type error: [Vec ...] element %u does not match "
                         "the declared nested element type", ei);
                typer__error(tc, node->start.line, node->start.column, err);
              }
            }
            return;
          }
        }
        if (en && en->type == AST_LIT_STRING &&
            is_type_keyword(en->data.lit_string.value,
                            en->data.lit_string.length)) {
          JaclType ref_et = type_from_keyword(en->data.lit_string.value,
                                              en->data.lit_string.length);
          if (ref_et == TYPE_DYN || ref_et == TYPE_STR) {
            node->inferred_type = TYPE_VEC;
            node->inferred_struct_idx = (ref_et == TYPE_STR)
                ? JACL_SCALAR_TYPE_IDX(TYPE_STR) : UINT32_MAX;
            if (ref_et == TYPE_STR) {
              for (uint32_t ei = 0; ei < node->data.command.arg_count; ei++) {
                JaclType at =
                    (JaclType)node->data.command.args[ei]->inferred_type;
                if (at != TYPE_STR && at != TYPE_DYN) {
                  char err[160];
                  jacl_format_typed_vec_elem(err, sizeof(err),
                      en->data.lit_string.value, en->data.lit_string.length,
                      ei, true, at);
                  typer__error(tc, node->start.line, node->start.column, err);
                }
              }
            }
            return;
          }
        }
      }
      /* Ref-element [Arr T] (T = str or dyn): `arr` is shorthand for
       * [Arr dyn]. Ref elements use the DYN arr rep (tagged traced slots,
       * elem_size 8) — no flat-bytes rep win exists for tagged heap
       * values; the element type lives statically as TYPE_ARR + stamp.
       * [Arr dyn] IS the plain arr (no stamp). Mirrors [Vec str]. */
      if (tc_kind == 1 && is_arr_ctor) {
        AstNode* en = head->data.command.args[0];
        if (en && en->type == AST_LIT_STRING &&
            is_type_keyword(en->data.lit_string.value,
                            en->data.lit_string.length)) {
          JaclType ref_et = type_from_keyword(en->data.lit_string.value,
                                              en->data.lit_string.length);
          if (ref_et == TYPE_DYN || ref_et == TYPE_STR) {
            node->inferred_type = TYPE_ARR;
            node->inferred_struct_idx = (ref_et == TYPE_STR)
                ? JACL_SCALAR_TYPE_IDX(TYPE_STR) : UINT32_MAX;
            if (ref_et == TYPE_STR) {
              for (uint32_t ei = 0; ei < node->data.command.arg_count; ei++) {
                JaclType at =
                    (JaclType)node->data.command.args[ei]->inferred_type;
                if (at != TYPE_STR && at != TYPE_DYN) {
                  char err[160];
                  jacl_format_typed_arr_elem(err, sizeof(err),
                      en->data.lit_string.value, en->data.lit_string.length,
                      ei, true, at);
                  typer__error(tc, node->start.line, node->start.column, err);
                }
              }
            }
            return;
          }
        }
      }
      /* Ref-kind VALUE [Map K V] forms ([Map K str] / [Map K dyn]):
       * plain traced map rep; the key/value types live statically as
       * TYPE_MAP + stamps (value on inferred_struct_idx, key on
       * inferred_key_struct_idx) — the [Vec str] scheme. [Map dyn dyn] IS
       * the plain map (no stamps). Mirrors the compiler's plain-rep route. */
      if (tc_kind == 3) {
        AstNode* vn = head->data.command.args[1];
        AstNode* kn = head->data.command.args[0];
        if (vn && vn->type == AST_LIT_STRING &&
            is_type_keyword(vn->data.lit_string.value,
                            vn->data.lit_string.length)) {
          JaclType ref_vt = type_from_keyword(vn->data.lit_string.value,
                                              vn->data.lit_string.length);
          if (ref_vt == TYPE_STR || ref_vt == TYPE_DYN) {
            node->inferred_type = TYPE_MAP;
            node->inferred_struct_idx = (ref_vt == TYPE_STR)
                ? JACL_SCALAR_TYPE_IDX(TYPE_STR) : UINT32_MAX;
            JaclType ref_kt = TYPE_DYN;
            if (kn && kn->type == AST_LIT_STRING &&
                is_type_keyword(kn->data.lit_string.value,
                                kn->data.lit_string.length)) {
              ref_kt = type_from_keyword(kn->data.lit_string.value,
                                         kn->data.lit_string.length);
              if (ref_kt != TYPE_DYN)
                node->inferred_key_struct_idx = JACL_SCALAR_TYPE_IDX(ref_kt);
            }
            /* Per-pair checks (dyn flow-in left to the compiler's mirror). */
            for (uint32_t ei = 0; ei < node->data.command.arg_count; ei++) {
              bool is_key = (ei % 2 == 0);
              JaclType at = (JaclType)node->data.command.args[ei]->inferred_type;
              if (at == TYPE_DYN) continue;
              bool ok = true;
              if (is_key) {
                if (ref_kt != TYPE_DYN) ok = (at == ref_kt);
              } else {
                if (ref_vt == TYPE_STR) ok = (at == TYPE_STR);
              }
              if (!ok) {
                char err[224];
                jacl_format_typed_map_kv(err, sizeof(err),
                    kn->data.lit_string.value, kn->data.lit_string.length,
                    vn->data.lit_string.value, vn->data.lit_string.length,
                    ei / 2, !is_key);
                typer__error(tc, node->data.command.args[ei]->start.line,
                             node->data.command.args[ei]->start.column, err);
                break;
              }
            }
            return;
          }
        }
      }
      node->inferred_type = is_arr_ctor ? TYPE_TYPED_ARR
                          : (tc_kind == 1) ? TYPE_TYPED_VEC : TYPE_TYPED_MAP;
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
      /* For [Map K V] (kind=3), also propagate the key type idx. */
      AstNode* key_node = NULL;
      if (tc_kind == 3) {
        key_node = head->data.command.args[0];
        if (key_node && key_node->type == AST_LIT_STRING) {
          const char* nm = key_node->data.lit_string.value;
          uint32_t    nl = key_node->data.lit_string.length;
          if (is_type_keyword(nm, nl)) {
            /* Explicit dyn keys ([Map dyn V]) are the [Map V] dyn-key form:
             * no key stamp (UINT32_MAX ≡ the VM's 0xFFFF convention). */
            JaclType kkw = type_from_keyword(nm, nl);
            if (kkw != TYPE_DYN)
              node->inferred_key_struct_idx = JACL_SCALAR_TYPE_IDX(kkw);
          } else {
            for (uint32_t si = 0; si < tc->struct_count; si++) {
              if (tc->structs[si].name_len == nl &&
                  memcmp(tc->structs[si].name, nm, nl) == 0) {
                node->inferred_key_struct_idx = si;
                break;
              }
            }
          }
        }
      }
      /* Element-type checks: each arg's typer-inferred type must match
       * the declared element (and key, for kind=3) type. Mirrors the
       * compiler's per-element check in compiler__compile_command's
       * typed-vec/typed-map branches; uses the same shared formatters
       * so wording stays in sync. We skip when the declared scalar is
       * not a supported typed-collection scalar (compiler reports the
       * "only value-type scalars supported" error first), and skip
       * struct checks for unknown struct names (compiler backstops
       * unknown-type errors). */
      if (elem_node && elem_node->type == AST_LIT_STRING) {
        const char* elem_nm = elem_node->data.lit_string.value;
        uint32_t    elem_nl = elem_node->data.lit_string.length;
        bool elem_is_scalar = is_type_keyword(elem_nm, elem_nl);
        JaclType elem_t = elem_is_scalar
                          ? type_from_keyword(elem_nm, elem_nl) : TYPE_DYN;
        uint32_t elem_sidx = node->inferred_struct_idx;
        bool elem_known = elem_is_scalar
            ? typer__is_typed_collection_scalar(elem_t)
            : (elem_sidx != UINT32_MAX &&
               !JACL_IS_SCALAR_TYPE_IDX(elem_sidx));

        const char* key_nm = NULL;
        uint32_t    key_nl = 0;
        bool key_is_scalar = false;
        JaclType key_t = TYPE_DYN;
        uint32_t key_sidx = UINT32_MAX;
        bool key_known = false;
        if (tc_kind == 3 && key_node && key_node->type == AST_LIT_STRING) {
          key_nm = key_node->data.lit_string.value;
          key_nl = key_node->data.lit_string.length;
          key_is_scalar = is_type_keyword(key_nm, key_nl);
          key_t = key_is_scalar
                  ? type_from_keyword(key_nm, key_nl) : TYPE_DYN;
          key_sidx = node->inferred_key_struct_idx;
          /* str keys are first-class on the typed rep (1 tagged traced
           * slot) — include them in the static key checks. Explicit dyn
           * keys have no stamp and skip checks. */
          key_known = key_is_scalar
              ? (typer__is_typed_collection_scalar(key_t) ||
                 key_t == TYPE_STR)
              : (key_sidx != UINT32_MAX &&
                 !JACL_IS_SCALAR_TYPE_IDX(key_sidx));
        }

        uint32_t argc = node->data.command.arg_count;
        AstNode** as = node->data.command.args;
        for (uint32_t i = 0; i < argc; i++) {
          /* For Map kinds, even idx → key, odd idx → value.
           * For Vec, every idx → element. */
          bool is_map = (tc_kind == 2 || tc_kind == 3);
          bool is_value_slot = !is_map || (i % 2 == 1);
          /* kind=2 keys are dyn — skip key slots. */
          if (tc_kind == 2 && !is_value_slot) continue;
          /* kind=3 key slot uses key_t/key_sidx; otherwise elem. */
          bool slot_is_key = (tc_kind == 3 && !is_value_slot);
          bool       slot_known      = slot_is_key ? key_known      : elem_known;
          bool       slot_is_scalar  = slot_is_key ? key_is_scalar  : elem_is_scalar;
          JaclType   slot_t          = slot_is_key ? key_t          : elem_t;
          uint32_t   slot_sidx       = slot_is_key ? key_sidx       : elem_sidx;
          if (!slot_known) continue;
          AstNode* arg = as[i];
          JaclType arg_t = (JaclType)arg->inferred_type;
          if (arg_t == TYPE_DYN) continue;  /* dyn flow-in: compiler handles */
          bool ok;
          if (slot_is_scalar) {
            ok = (arg_t == slot_t);
          } else {
            ok = (arg_t == TYPE_STRUCT && arg->inferred_struct_idx == slot_sidx);
          }
          if (ok) continue;
          char err[224];
          uint32_t pair_or_elem_idx = is_map ? (i / 2) : i;
          if (tc_kind == 1) {
            if (is_arr_ctor)
              jacl_format_typed_arr_elem(err, sizeof(err),
                  elem_nm, elem_nl, pair_or_elem_idx, slot_is_scalar, arg_t);
            else
              jacl_format_typed_vec_elem(err, sizeof(err),
                  elem_nm, elem_nl, pair_or_elem_idx, slot_is_scalar, arg_t);
          } else if (tc_kind == 2) {
            jacl_format_typed_map_value(err, sizeof(err),
                elem_nm, elem_nl, pair_or_elem_idx, slot_is_scalar, arg_t);
          } else {
            jacl_format_typed_map_kv(err, sizeof(err),
                key_nm, key_nl, elem_nm, elem_nl,
                pair_or_elem_idx, is_value_slot);
          }
          typer__error(tc, arg->start.line, arg->start.column, err);
          break;
        }
      }
    } else {
      /* [[Buf N T] v0 v1 ...] constructor — same shape as the typed-
       * vec/map constructors above but with a value (N) in the head's
       * second slot. The compiler integrates with the def site for
       * codegen; the typer just stamps TYPE_BUF + element/len so the
       * def-site type check sees a matching RHS. Nested form
       * `[[Buf N [Buf M T]] ...]` also tracked via inner_len (M4.2). */
      uint32_t buf_sidx_ctor;
      uint32_t buf_len_ctor;
      uint32_t buf_inner_len_ctor = 0;
      if (typer__buf_type_full(tc, head, &buf_sidx_ctor, &buf_len_ctor,
                               &buf_inner_len_ctor, NULL) &&
          buf_len_ctor > 0 && buf_sidx_ctor != UINT32_MAX) {
        node->inferred_type           = TYPE_BUF;
        node->inferred_struct_idx     = buf_sidx_ctor;
        node->inferred_buf_len        = buf_len_ctor;
        node->inferred_buf_inner_len  = buf_inner_len_ctor;
        return;
      }
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
            node->inferred_key_struct_idx = recv->inferred_key_struct_idx;
          } else if (recv_t == TYPE_MAP) {
            /* Stamped ref-kind maps keep their static key/value types
             * through set/remove (the result is the same plain map rep). */
            node->inferred_type = TYPE_MAP;
            node->inferred_struct_idx = recv->inferred_struct_idx;
            node->inferred_key_struct_idx = recv->inferred_key_struct_idx;
          }
          return;
        case HEAD_MAP_KEYS: case HEAD_MAP_VALS:
          if (recv_t == TYPE_TYPED_MAP) {
            /* keys: dyn-keyed maps return a plain vec (matches the
             * runtime path in OP_TYPED_MAP_KEYS keyed on
             * key_type_idx == 0xFFFF); str keys return a PLAIN vec too
             * (str can't live in GC-opaque typed-vec storage) but carry
             * the [Vec str] stamp. Struct/numeric keys yield a typed
             * vec. vals on a TYPE_TYPED_MAP always have a declared
             * value type, so they're always typed-vec. */
            if (hid == HEAD_MAP_KEYS &&
                recv->inferred_key_struct_idx == UINT32_MAX) {
              node->inferred_type = TYPE_VEC;
            } else if (hid == HEAD_MAP_KEYS &&
                       recv->inferred_key_struct_idx ==
                           JACL_SCALAR_TYPE_IDX(TYPE_STR)) {
              node->inferred_type = TYPE_VEC;
              node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(TYPE_STR);
            } else {
              node->inferred_type = TYPE_TYPED_VEC;
              node->inferred_struct_idx =
                  (hid == HEAD_MAP_KEYS) ? recv->inferred_key_struct_idx
                                         : recv->inferred_struct_idx;
            }
          } else if (recv_t == TYPE_MAP) {
            /* Plain rep: result is a plain vec; a str stamp on the
             * relevant side ([Map str V] keys / [Map K str] vals)
             * propagates as [Vec str]. Other stamps (numeric plain-rep
             * keys) stay unstamped — the vec holds tagged values. */
            node->inferred_type = TYPE_VEC;
            uint32_t side_idx =
                (hid == HEAD_MAP_KEYS) ? recv->inferred_key_struct_idx
                                       : recv->inferred_struct_idx;
            if (side_idx == JACL_SCALAR_TYPE_IDX(TYPE_STR))
              node->inferred_struct_idx = side_idx;
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
          } else if (recv_t == TYPE_VEC &&
                     recv->inferred_struct_idx != UINT32_MAX &&
                     JACL_IS_SCALAR_TYPE_IDX(recv->inferred_struct_idx)) {
            /* Stamped ref-element vec ([Vec str]): element narrows. */
            node->inferred_type =
                JACL_TYPE_IDX_TO_SCALAR(recv->inferred_struct_idx);
          } else if (recv_t == TYPE_VEC &&
                     recv->inferred_struct_idx == UINT32_MAX &&
                     recv->type == AST_VAR_REF) {
            /* Nested-element vec binding ([Vec [Vec i64]] …): the AST
             * stamp is suppressed by the cross-registry rule — resolve
             * the TYPER-SIDE shape from the binding and decode to a
             * PORTABLE result encoding, mirroring HEAD_FOR. A depth-3+
             * inner shape stays suppressed on the AST (the def handler
             * re-derives it into the new binding). */
            const TyperBinding* vb = typer__scope_resolve(tc,
                recv->data.var_ref.name, recv->data.var_ref.length,
                recv->scope_mark);
            if (vb && vb->type == TYPE_VEC && vb->struct_idx != UINT32_MAX &&
                !JACL_IS_SCALAR_TYPE_IDX(vb->struct_idx) &&
                vb->struct_idx >= TYPER_MAX_STRUCTS &&
                vb->struct_idx < tc->shape_reg.count) {
              uint32_t iv = UINT32_MAX, ik = UINT32_MAX;
              JaclType ek = typer__buf_elem_decode(tc, vb->struct_idx,
                                                   &iv, &ik);
              if (ek == TYPE_TYPED_VEC) {
                bool inner_ref = JACL_IS_SCALAR_TYPE_IDX(iv) &&
                                 (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR ||
                                  JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_DYN);
                bool inner_shape = !JACL_IS_SCALAR_TYPE_IDX(iv) &&
                                   iv >= TYPER_MAX_STRUCTS &&
                                   iv < tc->shape_reg.count;
                if (inner_ref) {
                  node->inferred_type = TYPE_VEC;
                  if (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR)
                    node->inferred_struct_idx = iv;
                } else if (inner_shape) {
                  node->inferred_type = TYPE_VEC;  /* stamp stays suppressed */
                } else {
                  node->inferred_type = TYPE_TYPED_VEC;
                  node->inferred_struct_idx = iv;
                }
              } else if (ek == TYPE_TYPED_MAP) {
                JaclType mt; uint32_t msi, mki;
                typer__decode_map_shape(iv, ik, &mt, &msi, &mki);
                node->inferred_type = mt;
                node->inferred_struct_idx = msi;
                node->inferred_key_struct_idx = mki;
              }
            }
          }
          return;
        /* arr ops mirror vec: get narrows to the element type for a typed
         * receiver; set returns the (reference) arr. See ARR_DESIGN.md M4c. */
        case HEAD_ARR_GET:
        case HEAD_ARR_POP:
          /* Both narrow to the element type for a typed receiver. arr-pop on a
           * struct array pushes inline struct slots, so the result type MUST
           * be TYPE_STRUCT or the compiler under-counts the stack. */
          if (recv_t == TYPE_TYPED_ARR &&
              recv->inferred_struct_idx != UINT32_MAX) {
            uint32_t eidx = recv->inferred_struct_idx;
            if (JACL_IS_SCALAR_TYPE_IDX(eidx)) {
              /* Narrow to the element scalar for ALL scalars. i64/u64/f64 come
               * back as unboxed wide bits (vm__arr_scalar_load), matching
               * typed-vec; OP_TO_DYN bridges them at dyn sinks. */
              node->inferred_type = JACL_TYPE_IDX_TO_SCALAR(eidx);
            } else if (eidx < tc->struct_count) {
              node->inferred_type = TYPE_STRUCT;
              node->inferred_struct_idx = eidx;
            }
          } else if (recv_t == TYPE_ARR &&
                     recv->inferred_struct_idx != UINT32_MAX &&
                     JACL_IS_SCALAR_TYPE_IDX(recv->inferred_struct_idx)) {
            /* Stamped ref-element arr ([Arr str]): element narrows. */
            node->inferred_type =
                JACL_TYPE_IDX_TO_SCALAR(recv->inferred_struct_idx);
          }
          return;
        case HEAD_ARR_SET:
          if (recv_t == TYPE_TYPED_ARR) {
            node->inferred_type = TYPE_TYPED_ARR;
            node->inferred_struct_idx = recv->inferred_struct_idx;
          } else if (recv_t == TYPE_ARR) {
            /* Stamped ref-element arrs keep their element type. */
            node->inferred_type = TYPE_ARR;
            node->inferred_struct_idx = recv->inferred_struct_idx;
          }
          return;
        case HEAD_BUF_SET:
        case HEAD_BUF_USET:
          /* [buf-set $b $i $v] / [buf-unchecked-set $b $i $v]:
           * compile-time type check on $v against the buf's element
           * type. Result type (TYPE_NIL) comes from fixed_returns
           * below; we just emit any mismatch error here. */
          if (recv_t == TYPE_BUF &&
              recv->inferred_struct_idx != UINT32_MAX &&
              node->data.command.arg_count == 3) {
            typer__check_buf_set_value(tc, recv->inferred_struct_idx,
                                        node->data.command.args[2]);
          }
          break;  /* fall through to fixed_returns for TYPE_NIL */
        case HEAD_BUF_GET:
        case HEAD_BUF_UGET:
          /* Result type of [buf-get $b $i] / [buf-unchecked-get $b $i].
           * Decode via the typer's shape registry: scalar elements
           * surface as their JaclType (small ints widen to i32 at the
           * dyn boundary); typed-vec / typed-map elements propagate
           * V (and K for maps) to the result AST so chained map-get /
           * vec-get / arrow chains narrow correctly. */
          if (recv_t == TYPE_BUF && recv->inferred_struct_idx != UINT32_MAX) {
            uint32_t inner_v = UINT32_MAX, inner_k = UINT32_MAX;
            JaclType elem = typer__buf_elem_decode(tc,
                recv->inferred_struct_idx, &inner_v, &inner_k);
            switch (elem) {
              case TYPE_I8: case TYPE_U8:
              case TYPE_I16: case TYPE_U16:
                node->inferred_type = TYPE_I32; break;
              case TYPE_TYPED_VEC:
                node->inferred_type = TYPE_TYPED_VEC;
                node->inferred_struct_idx = inner_v;
                break;
              case TYPE_TYPED_MAP:
                node->inferred_type = TYPE_TYPED_MAP;
                node->inferred_struct_idx = inner_v;
                node->inferred_key_struct_idx = inner_k;
                break;
              case TYPE_PTR:
                node->inferred_type = TYPE_PTR;
                node->inferred_struct_idx = inner_v;
                break;
              case TYPE_FUTURE:
                node->inferred_type = TYPE_FUTURE;
                node->inferred_struct_idx = inner_v;
                break;
              case TYPE_STRUCT:
                node->inferred_type = TYPE_STRUCT;
                node->inferred_struct_idx = recv->inferred_struct_idx;
                break;
              default:
                node->inferred_type = elem; break;
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
          } else if (recv_t == TYPE_MAP &&
                     recv->inferred_struct_idx ==
                         JACL_SCALAR_TYPE_IDX(TYPE_STR)) {
            /* Stamped ref-value map ([Map K str]): the value narrows. */
            node->inferred_type = TYPE_STR;
          } else if (recv_t == TYPE_MAP &&
                     recv->inferred_struct_idx == UINT32_MAX &&
                     recv->type == AST_VAR_REF) {
            /* Nested compound VALUE map binding ([Map str [Vec i64]]):
             * resolve the typer-side value shape from the binding and
             * decode to a PORTABLE result encoding (mirrors vec-get on
             * shape-carried vec bindings). */
            const TyperBinding* mb = typer__scope_resolve(tc,
                recv->data.var_ref.name, recv->data.var_ref.length,
                recv->scope_mark);
            if (mb && mb->type == TYPE_MAP && mb->struct_idx != UINT32_MAX &&
                !JACL_IS_SCALAR_TYPE_IDX(mb->struct_idx) &&
                mb->struct_idx >= TYPER_MAX_STRUCTS &&
                mb->struct_idx < tc->shape_reg.count) {
              uint32_t iv = UINT32_MAX, ik = UINT32_MAX;
              JaclType ek = typer__buf_elem_decode(tc, mb->struct_idx,
                                                   &iv, &ik);
              if (ek == TYPE_TYPED_VEC) {
                bool inner_ref = JACL_IS_SCALAR_TYPE_IDX(iv) &&
                                 (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR ||
                                  JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_DYN);
                bool inner_shape = !JACL_IS_SCALAR_TYPE_IDX(iv) &&
                                   iv >= TYPER_MAX_STRUCTS &&
                                   iv < tc->shape_reg.count;
                if (inner_ref) {
                  node->inferred_type = TYPE_VEC;
                  if (JACL_TYPE_IDX_TO_SCALAR(iv) == TYPE_STR)
                    node->inferred_struct_idx = iv;
                } else if (inner_shape) {
                  node->inferred_type = TYPE_VEC;  /* stamp suppressed */
                } else {
                  node->inferred_type = TYPE_TYPED_VEC;
                  node->inferred_struct_idx = iv;
                }
              } else if (ek == TYPE_TYPED_MAP) {
                JaclType mt; uint32_t msi, mki;
                typer__decode_map_shape(iv, ik, &mt, &msi, &mki);
                node->inferred_type = mt;
                node->inferred_struct_idx = msi;
                node->inferred_key_struct_idx = mki;
              }
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
           * elem (and key) struct_idx. Mirrors compiler__compile_hof_builtin:
           * typed receiver → typed result; plain → plain; stream → stream. */
          if (recv_t == TYPE_TYPED_VEC || recv_t == TYPE_TYPED_MAP ||
              recv_t == TYPE_VEC || recv_t == TYPE_MAP ||
              recv_t == TYPE_STREAM) {
            node->inferred_type = recv_t;
            node->inferred_struct_idx = recv->inferred_struct_idx;
            node->inferred_key_struct_idx = recv->inferred_key_struct_idx;
            /* Typed-closure predicate (TYPED_CLOSURES_DESIGN.md Phase A): over a
             * typed stream, type the predicate body with its param bound to the
             * source element type (the truthiness return is discarded, so we
             * ignore the result enc — we just want the body typed). For a wide
             * element this leaves the body compiled to read the param wide, so
             * the filter pull can skip the box-for-call. Only inline procs are
             * affected; typer__proc_result_enc no-ops on a var-ref predicate. */
            if (recv_t == TYPE_STREAM &&
                node->data.command.arg_count == 2 &&
                recv->inferred_struct_idx != UINT32_MAX) {
              uint32_t arg_enc = recv->inferred_struct_idx;
              bool pred_typed = false;
              (void)typer__proc_result_enc(
                  tc, node->data.command.args[1], &arg_enc, 1, &pred_typed);
              /* Stamp the predicate proc node so the compiler bakes the
               * param rep onto OP_FILTER iff the body was actually typed
               * against the element (a clean probe). On fallback/non-proc it
               * stays dyn → the pull boxes a wide element for the call.
               * An inline proc skipped the generic pre-walk (type-once), so
               * also give it the TYPE_CLOSURE stamp handle_proc would have. */
              node->data.command.args[1]->inferred_struct_idx =
                  pred_typed ? arg_enc : UINT32_MAX;
              if (node->data.command.args[1]->type == AST_COMMAND &&
                  node->data.command.args[1]->data.command.head_id == HEAD_PROC)
                node->data.command.args[1]->inferred_type = TYPE_CLOSURE;
            }
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
            /* transform over a typed stream → a stream of the mapper's return
             * type: bind the mapper proc's param to the source element type
             * and infer its body. Works for any inline proc, not just `\`
             * lambdas. (Output element falls back to dyn when the return type
             * can't be inferred.) */
            if (recv_t == TYPE_STREAM &&
                node->data.command.arg_count == 2 &&
                recv->inferred_struct_idx != UINT32_MAX) {
              uint32_t arg_enc = recv->inferred_struct_idx;
              bool mapper_typed = false;
              node->inferred_struct_idx = typer__proc_result_enc(
                  tc, node->data.command.args[1], &arg_enc, 1, &mapper_typed);
              /* Stamp the mapper node with the element enc iff its body was
               * monomorphized (typed against the element). The compiler
               * requires this for struct-element streams: only a
               * monomorphized inline mapper can take the N-slot element.
               * An inline proc skipped the generic pre-walk (type-once), so
               * also give it the TYPE_CLOSURE stamp handle_proc would have. */
              node->data.command.args[1]->inferred_struct_idx =
                  mapper_typed ? arg_enc : UINT32_MAX;
              if (node->data.command.args[1]->type == AST_COMMAND &&
                  node->data.command.args[1]->data.command.head_id == HEAD_PROC)
                node->data.command.args[1]->inferred_type = TYPE_CLOSURE;
            }
          }
          return;
        case HEAD_TAKE:
          /* take preserves the receiver's collection kind:
           * vec → vec, typed_vec → typed_vec (slice keeps element types),
           * stream → stream. Mirrors vm.c:OP_TAKE branches. */
          if (recv_t == TYPE_TYPED_VEC) {
            node->inferred_type = TYPE_TYPED_VEC;
            node->inferred_struct_idx = recv->inferred_struct_idx;
          } else if (recv_t == TYPE_VEC || recv_t == TYPE_STREAM) {
            node->inferred_type = recv_t;
            /* Preserve the element type for typed streams/vecs so a for-loop
             * over `take N <typed stream>` still narrows. Mirrors filter. */
            node->inferred_struct_idx = recv->inferred_struct_idx;
          }
          return;
        case HEAD_FIRST:
          /* first returns the receiver's first element. Narrow when
           * the receiver is a typed-vec with a knowable element type
           * (same encoding as vec-get / map-get). Plain vec / map /
           * stream → DYN since elements are heterogeneously typed. */
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
      { HEAD_BUF_LEN,     TYPE_I32    },
      { HEAD_ARR_LEN,     TYPE_I32    },
      /* arr-push mutates in place and returns the new length (i32). */
      { HEAD_ARR_PUSH,    TYPE_I32    },
      /* Hash — OP_HASH always pushes an i32 (vm.c:6859). */
      { HEAD_HASH,        TYPE_I32    },
      /* String results. */
      { HEAD_TO_STRING,   TYPE_STR    },
      { HEAD_SLICE,       TYPE_STR    },
      { HEAD_CONCAT,      TYPE_STR    },
      /* Stream constructors. */
      { HEAD_RANGE,            TYPE_STREAM },
      { HEAD_RANGE_INCLUSIVE,  TYPE_STREAM },
      { HEAD_LINES,       TYPE_STREAM },
      /* Constructors that always produce dyn collections. */
      { HEAD_VEC,         TYPE_VEC    },
      { HEAD_MAP,         TYPE_MAP    },
      { HEAD_COLLECT,     TYPE_VEC    },
      /* Mutable arr: constructor + in-place set (returns the arr). arr-get
       * and arr-pop stay dyn (fall through to the default). See ARR_DESIGN.md. */
      { HEAD_ARR,         TYPE_ARR    },
      { HEAD_ARR_SET,     TYPE_ARR    },
      /* Side-effecting — always nil. */
      { HEAD_PRINT,       TYPE_NIL    },
      { HEAD_BUF_SET,     TYPE_NIL    },
      { HEAD_BUF_USET,    TYPE_NIL    },
      /* File I/O — write/append produce nil on success, error value on
       * failure (catchable via try/catch).  read-file produces a string. */
      { HEAD_READ_FILE,   TYPE_STR    },
      { HEAD_WRITE_FILE,  TYPE_NIL    },
      { HEAD_APPEND_FILE, TYPE_NIL    },
      /* Atom watchers — both side-effecting, return nil. */
      { HEAD_WATCH,       TYPE_NIL    },
      { HEAD_UNWATCH,     TYPE_NIL    },
      /* Loop forms — emit OP_NIL at normal exit. break-with-value
       * paths could carry a different type but are conservatively
       * unified to nil here; refine in a later commit if needed. */
      { HEAD_WHILE,       TYPE_NIL    },
      { HEAD_FOR,         TYPE_NIL    },
      /* Yield — pushes nil after resume in current SM compilation. */
      { HEAD_YIELD,       TYPE_NIL    },
      /* Sleep — always evaluates to nil (after wake or after nanosleep). */
      { HEAD_SLEEP,       TYPE_NIL    },
      /* Job control — bool indicates delivered/cancelled. */
      { HEAD_SIGNAL,      TYPE_BOOL   },
      { HEAD_CANCEL,      TYPE_BOOL   },
      /* Concurrency: parallel resolves N futures and pushes a vec of
       * results in input order (vm.c:5066 — `cont_arg = jacl_vector_ptr(vec)`).
       * spawn/await/race stay DYN: spawn returns a future (no
       * TYPE_FUTURE in the type system today), await unwraps the
       * future to whatever type the body produced, race returns the
       * winner's value — all dynamically determined. */
      { HEAD_PARALLEL,    TYPE_VEC    },
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
      /* Stream constructors: stamp the element-type idx so `for x in s`
       * narrows the loop binding (parallel to TYPE_TYPED_VEC's idx).
       * `range` / `range-inclusive` yield i64 (vm.c OP_RANGE produces
       * i64 via jacl_i64), `lines` yields str. */
      if (hid == HEAD_RANGE || hid == HEAD_RANGE_INCLUSIVE) {
        node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(TYPE_I64);
      } else if (hid == HEAD_LINES) {
        node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(TYPE_STR);
      } else if (hid == HEAD_COLLECT && node->data.command.arg_count == 1) {
        /* Typed collect (STREAM_TYPING_DEBT item 2): collecting a TYPED
         * stream materializes a typed vec [Vec T] — wide elements stored
         * flat (no i32-for-small box-back), struct elements stored as
         * inline bytes. Dyn streams (and vec identity) keep TYPE_VEC.
         * The VM keys the same decision off the stream's runtime elem_idx,
         * so dyn-flow and typed-flow agree. */
        AstNode* recv = node->data.command.args[0];
        if ((JaclType)recv->inferred_type == TYPE_STREAM &&
            recv->inferred_struct_idx != UINT32_MAX) {
          uint32_t ce = recv->inferred_struct_idx;
          /* Value-type elements only — same rule as the [Vec T] constructor:
           * typed-vec storage is GC-OPAQUE raw bytes, so str (a heap
           * pointer) must stay in a plain (traced) vec. dyn streams keep
           * the plain vec too. */
          bool ce_ok = !JACL_IS_SCALAR_TYPE_IDX(ce) /* struct */ ||
                       (JACL_TYPE_IDX_TO_SCALAR(ce) != TYPE_DYN &&
                        JACL_TYPE_IDX_TO_SCALAR(ce) != TYPE_STR);
          if (ce_ok) {
            node->inferred_type = TYPE_TYPED_VEC;
            node->inferred_struct_idx = ce;
            node->inferred_key_struct_idx = UINT32_MAX;
          } else if (JACL_IS_SCALAR_TYPE_IDX(ce) &&
                     JACL_TYPE_IDX_TO_SCALAR(ce) == TYPE_STR) {
            /* str elements: ref-element [Vec str] — plain traced vec REP
             * (the VM keeps the plain-vec collect path), element type
             * carried statically so for-loops over the result narrow. */
            node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(TYPE_STR);
            node->inferred_key_struct_idx = UINT32_MAX;
          }
        }
      }
    } else if (hid == HEAD_BOX && node->data.command.arg_count == 1) {
      /* [box $val]: runtime returns a box wrapping the value. The
       * box's element type is the value's static type, encoded the
       * same way as TYPE_FUTURE / TYPE_TYPED_VEC — scalar sentinel
       * for scalar elements, real struct idx for struct elements. */
      AstNode* val = node->data.command.args[0];
      JaclType vt = (JaclType)val->inferred_type;
      node->inferred_type = TYPE_BOX;
      if (vt == TYPE_STRUCT) {
        node->inferred_struct_idx = val->inferred_struct_idx;
      } else if (vt != TYPE_DYN) {
        node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(vt);
      }
    } else if (hid == HEAD_DEREF && node->data.command.arg_count == 1) {
      /* [deref $box]: narrow to the box's element type. Scalar
       * elements use the JACL_SCALAR_TYPE_IDX sentinel encoding;
       * struct elements use the registry idx and the compiler
       * emits OP_DEREF_INLINE to materialize inline struct bytes
       * (parallel to the unbox path's struct-box handling). */
      AstNode* recv = node->data.command.args[0];
      JaclType rt = (JaclType)recv->inferred_type;
      uint32_t e_idx = recv->inferred_struct_idx;
      if (rt == TYPE_BOX && e_idx != UINT32_MAX) {
        if (JACL_IS_SCALAR_TYPE_IDX(e_idx)) {
          node->inferred_type = JACL_TYPE_IDX_TO_SCALAR(e_idx);
        } else {
          node->inferred_type = TYPE_STRUCT;
          node->inferred_struct_idx = e_idx;
        }
      } else {
        node->inferred_type = TYPE_DYN;
      }
    } else if (hid == HEAD_SWAP && node->data.command.arg_count == 2) {
      /* [swap $box $fn]: scalar-element narrowing only. Struct
       * elements stay dyn — swap's fn-return path doesn't have an
       * inline-struct opcode yet (would need OP_SWAP_INLINE). */
      AstNode* recv = node->data.command.args[0];
      JaclType rt = (JaclType)recv->inferred_type;
      uint32_t e_idx = recv->inferred_struct_idx;
      if (rt == TYPE_BOX && e_idx != UINT32_MAX &&
          JACL_IS_SCALAR_TYPE_IDX(e_idx)) {
        node->inferred_type = JACL_TYPE_IDX_TO_SCALAR(e_idx);
      } else {
        node->inferred_type = TYPE_DYN;
      }
    } else if (hid == HEAD_SPAWN && node->data.command.arg_count == 1 &&
               node->data.command.args[0]->type == AST_BLOCK) {
      /* spawn: runtime returns a future (vm.c:4763). Element type is
       * the body's tail type when concrete, dyn otherwise. The body
       * was already typed by the args walk; its inferred_type is the
       * type of the last expression (or NIL if trailing semi). */
      AstNode* body = node->data.command.args[0];
      node->inferred_type = TYPE_FUTURE;
      JaclType body_t = (JaclType)body->inferred_type;
      if (body_t == TYPE_STRUCT) {
        node->inferred_struct_idx = body->inferred_struct_idx;
      } else if (body_t != TYPE_DYN) {
        node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(body_t);
      }
    } else if (hid == HEAD_RACE && node->data.command.arg_count >= 2) {
      /* race: returns the first body's result to complete. If every
       * body has the same concrete tail type, narrow the result to
       * that type; otherwise dyn. Mirrors parallel's body-walk shape
       * but returns a single value (the winner) instead of a vec. */
      AstNode** as = node->data.command.args;
      uint32_t n = node->data.command.arg_count;
      JaclType  unified_t    = TYPE_DYN;
      uint32_t  unified_sidx = UINT32_MAX;
      bool      all_same     = true;
      for (uint32_t i = 0; i < n; i++) {
        AstNode* body = as[i];
        if (body->type != AST_BLOCK) { all_same = false; break; }
        JaclType bt = (JaclType)body->inferred_type;
        if (bt == TYPE_DYN) { all_same = false; break; }
        if (i == 0) {
          unified_t = bt;
          unified_sidx = body->inferred_struct_idx;
        } else if (bt != unified_t ||
                   (bt == TYPE_STRUCT &&
                    body->inferred_struct_idx != unified_sidx)) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        node->inferred_type = unified_t;
        if (unified_t == TYPE_STRUCT) {
          node->inferred_struct_idx = unified_sidx;
        }
      } else {
        node->inferred_type = TYPE_DYN;
      }
    } else if (hid == HEAD_AWAIT && node->data.command.arg_count == 1) {
      /* await: unwraps a future. If the operand is a TYPE_FUTURE with
       * a known element type, narrow the result to that element type.
       * If the operand has a known concrete non-future type, that's a
       * compile-time type error (await is only meaningful on futures).
       * Dyn operands are permitted; the runtime tag check catches
       * non-future values at await time. */
      AstNode* arg = node->data.command.args[0];
      JaclType arg_t = (JaclType)arg->inferred_type;
      if (arg_t == TYPE_FUTURE) {
        uint32_t e_idx = arg->inferred_struct_idx;
        if (e_idx == UINT32_MAX) {
          node->inferred_type = TYPE_DYN;
        } else if (JACL_IS_SCALAR_TYPE_IDX(e_idx)) {
          node->inferred_type = JACL_TYPE_IDX_TO_SCALAR(e_idx);
        } else {
          node->inferred_type = TYPE_STRUCT;
          node->inferred_struct_idx = e_idx;
        }
      } else if (arg_t == TYPE_DYN) {
        node->inferred_type = TYPE_DYN;
      } else {
        char err[256];
        jacl_format_await_non_future(err, sizeof(err), arg_t);
        typer__error(tc, arg->start.line, arg->start.column, err);
        node->inferred_type = TYPE_DYN;
      }
    } else if (hid == HEAD_PTR_NULL && node->data.command.arg_count == 1) {
      /* [ptr-null [Ptr T]]: typed null pointer literal. Pure
       * compile-time op — at runtime a null pointer is u64(0).
       * The annotation supplies pointee identity. */
      AstNode* type_node = node->data.command.args[0];
      uint32_t pointee_sidx = UINT32_MAX;
      if (!typer__ptr_type(tc, type_node, &pointee_sidx)) {
        char err[128];
        jacl_format_ptr_null_bad_arg(err, sizeof(err));
        typer__error(tc, type_node->start.line, type_node->start.column, err);
        node->inferred_type = TYPE_DYN;
      } else {
        node->inferred_type       = TYPE_PTR;
        node->inferred_struct_idx = pointee_sidx;
      }
    } else if (hid == HEAD_PTR_CAST && node->data.command.arg_count == 2) {
      /* [ptr-cast [Ptr T] $u64_value]: re-tag a u64 address as a typed
       * pointer. The annotation supplies pointee identity; the value
       * must be u64 (or dyn — the cast is the explicit boundary). */
      AstNode* type_node = node->data.command.args[0];
      AstNode* val_node  = node->data.command.args[1];
      uint32_t pointee_sidx = UINT32_MAX;
      if (!typer__ptr_type(tc, type_node, &pointee_sidx)) {
        char err[128];
        jacl_format_ptr_cast_bad_first_arg(err, sizeof(err));
        typer__error(tc, type_node->start.line, type_node->start.column, err);
        node->inferred_type = TYPE_DYN;
      } else {
        JaclType val_t = (JaclType)val_node->inferred_type;
        if (val_t != TYPE_U64 && val_t != TYPE_DYN) {
          char err[128];
          jacl_format_ptr_cast_value_not_u64(err, sizeof(err), val_t);
          typer__error(tc, val_node->start.line, val_node->start.column, err);
        }
        node->inferred_type       = TYPE_PTR;
        node->inferred_struct_idx = pointee_sidx;
      }
    } else if (hid == HEAD_PTR_ADDR && node->data.command.arg_count == 1) {
      /* [ptr-addr $p]: typed pointer → raw u64. Dyn is permitted; any
       * other concrete type is a compile-time error. */
      AstNode* arg = node->data.command.args[0];
      JaclType arg_t = (JaclType)arg->inferred_type;
      if (arg_t != TYPE_PTR && arg_t != TYPE_DYN) {
        char err[128];
        jacl_format_ptr_op_expects_ptr(err, sizeof(err), "ptr-addr", arg_t);
        typer__error(tc, arg->start.line, arg->start.column, err);
      }
      node->inferred_type = TYPE_U64;
    } else if (hid == HEAD_ADDR && node->data.command.arg_count == 1) {
      /* [addr $p->field->...]: result is [Ptr T] where T is the
       * accessed field's type. The typer trusts the typer-set
       * inferred_type/struct_idx on the inner chain expression. */
      AstNode* inner = node->data.command.args[0];

      /* [addr $buf->N] / [addr $p->N]: result is [Ptr ElemType] using
       * the buf or pointer's *declared* element/pointee type (not the
       * widened i32 surfaced by the arrow-read). Detect
       * `[. $varref $intlit]` whose receiver is a TYPE_BUF or TYPE_PTR
       * binding. See BUFFER_DESIGN.md M3 / M3.7. */
      if (inner->type == AST_COMMAND &&
          inner->data.command.head_id == HEAD_DOT &&
          inner->data.command.arg_count == 2 &&
          inner->data.command.args[0]->type == AST_VAR_REF &&
          inner->data.command.args[1]->type == AST_LIT_INT) {
        AstNode* recv = inner->data.command.args[0];
        const TyperBinding* b = typer__scope_resolve(tc,
            recv->data.var_ref.name, recv->data.var_ref.length,
            recv->scope_mark);
        if (b && (b->type == TYPE_BUF || b->type == TYPE_PTR) &&
            b->struct_idx != UINT32_MAX) {
          /* For nested bufs (Phase 5b: struct_idx is a TYPE_SHAPE_BUF
           * idx), walk the shape chain to the leaf element so the
           * resulting [Ptr T_leaf] points at scalar / struct bytes the
           * compiler can interpret. Without this peel, [addr $cube->0]
           * propagates a typer-side shape idx as the pointee, which the
           * compiler reads as a struct idx and either segfaults or
           * mis-typed-errors. */
          uint32_t pointee = b->struct_idx;
          if (b->type == TYPE_BUF) {
            uint32_t walk = pointee;
            while (walk >= TYPER_MAX_STRUCTS && walk < tc->shape_reg.count &&
                   tc->shape_reg.shapes[walk].kind == TYPE_SHAPE_BUF) {
              walk = tc->shape_reg.shapes[walk].u.buf.elem_idx;
            }
            pointee = walk;
          }
          node->inferred_type       = TYPE_PTR;
          node->inferred_struct_idx = pointee;
          return;
        }
      }

      /* [addr EXPR->N] where EXPR has inferred_type TYPE_PTR (covers
       * chained field access like `[addr $h->magic->0]`). The receiver
       * isn't a bare var-ref here; the pointee idx must come from
       * EXPR.inferred_struct_idx (the *un-widened* pointee), since
       * inner->inferred_type is the widened scalar (i32 for u8/i16/etc.).
       * See BUFFER_DESIGN.md "Receiver-shape generalization". */
      if (inner->type == AST_COMMAND &&
          inner->data.command.head_id == HEAD_DOT &&
          inner->data.command.arg_count == 2 &&
          inner->data.command.args[0]->type != AST_VAR_REF &&
          inner->data.command.args[1]->type == AST_LIT_INT) {
        AstNode* recv = inner->data.command.args[0];
        if ((JaclType)recv->inferred_type == TYPE_PTR &&
            recv->inferred_struct_idx != UINT32_MAX) {
          node->inferred_type       = TYPE_PTR;
          node->inferred_struct_idx = recv->inferred_struct_idx;
          return;
        }
      }

      JaclType inner_t = (JaclType)inner->inferred_type;
      uint32_t inner_sidx = inner->inferred_struct_idx;
      if (inner_t == TYPE_STRUCT && inner_sidx != UINT32_MAX) {
        /* addr of an embedded struct field → [Ptr InnerStruct] */
        node->inferred_type       = TYPE_PTR;
        node->inferred_struct_idx = inner_sidx;
      } else if (inner_t != TYPE_DYN && inner_t != TYPE_STRUCT) {
        /* Scalar leaf → [Ptr <scalar>] */
        node->inferred_type       = TYPE_PTR;
        node->inferred_struct_idx = JACL_SCALAR_TYPE_IDX(inner_t);
      } else {
        /* Unknown / dyn — fall back to dyn-pointer (caller-checked
         * at runtime via the chain walker's compile-time error if
         * the chain doesn't resolve). */
        node->inferred_type       = TYPE_PTR;
        node->inferred_struct_idx = UINT32_MAX;
      }
    } else if (hid == HEAD_PTR_OFFSET && node->data.command.arg_count == 2) {
      /* [ptr-offset $p $n]: typed pointer arithmetic. Result preserves
       * the operand's pointee type. The integer offset must be a
       * concrete numeric (or dyn). */
      AstNode* p   = node->data.command.args[0];
      AstNode* n   = node->data.command.args[1];
      JaclType p_t = (JaclType)p->inferred_type;
      JaclType n_t = (JaclType)n->inferred_type;
      if (p_t != TYPE_PTR && p_t != TYPE_DYN) {
        char err[160];
        jacl_format_ptr_op_expects_ptr(err, sizeof(err), "ptr-offset", p_t);
        typer__error(tc, p->start.line, p->start.column, err);
        node->inferred_type = TYPE_DYN;
      } else if (n_t != TYPE_DYN && !is_numeric_type(n_t)) {
        char err[160];
        jacl_format_ptr_offset_non_numeric(err, sizeof(err), n_t);
        typer__error(tc, n->start.line, n->start.column, err);
        node->inferred_type = TYPE_DYN;
      } else {
        node->inferred_type       = TYPE_PTR;
        node->inferred_struct_idx = p->inferred_struct_idx;
      }
    } else if (hid == HEAD_PTR_DIFF && node->data.command.arg_count == 2) {
      /* [ptr-diff $a $b]: subtract two pointers of the same pointee.
       * Result is i64 (signed element count). */
      AstNode* a = node->data.command.args[0];
      AstNode* b = node->data.command.args[1];
      JaclType a_t = (JaclType)a->inferred_type;
      JaclType b_t = (JaclType)b->inferred_type;
      if (a_t != TYPE_PTR || b_t != TYPE_PTR) {
        if (a_t != TYPE_DYN && b_t != TYPE_DYN) {
          char err[160];
          jacl_format_ptr_diff_expects_two(err, sizeof(err), a_t, b_t);
          typer__error(tc, a->start.line, a->start.column, err);
        }
      } else if (a->inferred_struct_idx != b->inferred_struct_idx &&
                 a->inferred_struct_idx != UINT32_MAX &&
                 b->inferred_struct_idx != UINT32_MAX) {
        char err[160];
        jacl_format_ptr_diff_pointee_mismatch(err, sizeof(err));
        typer__error(tc, a->start.line, a->start.column, err);
      }
      node->inferred_type = TYPE_I64;
    } else if (hid == HEAD_PTR_DEREF && node->data.command.arg_count == 1) {
      /* [ptr-deref $p]: load the value at *p. For [Ptr T] with a
       * scalar pointee, the result narrows to T. Struct pointees
       * route through $p->field; this builtin errors on them. */
      AstNode* arg = node->data.command.args[0];
      JaclType arg_t = (JaclType)arg->inferred_type;
      uint32_t arg_sidx = arg->inferred_struct_idx;
      if (arg_t == TYPE_PTR) {
        if (JACL_IS_SCALAR_TYPE_IDX(arg_sidx)) {
          node->inferred_type = JACL_TYPE_IDX_TO_SCALAR(arg_sidx);
        } else if (arg_sidx != UINT32_MAX) {
          char err[160];
          jacl_format_ptr_deref_struct(err, sizeof(err));
          typer__error(tc, arg->start.line, arg->start.column, err);
          node->inferred_type = TYPE_DYN;
        } else {
          /* Pointee unknown — fall through to dyn. */
          node->inferred_type = TYPE_DYN;
        }
      } else if (arg_t == TYPE_DYN) {
        node->inferred_type = TYPE_DYN;
      } else {
        char err[128];
        jacl_format_ptr_op_expects_ptr(err, sizeof(err), "ptr-deref", arg_t);
        typer__error(tc, arg->start.line, arg->start.column, err);
        node->inferred_type = TYPE_DYN;
      }
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
       * leaves nil. Mirrors compiler.c's set path. Also enforces the
       * field-type / value-type rule via the shared formatters
       * (compiler.c:9722-9738). */
      AstNode* tgt = node->data.command.args[0];
      AstNode* fld = node->data.command.args[1];
      AstNode* val = node->data.command.args[2];
      JaclType tgt_t    = (JaclType)tgt->inferred_type;
      uint32_t tgt_sidx = tgt->inferred_struct_idx;
      if (tgt_t != TYPE_STRUCT && tgt->type == AST_LIT_STRING &&
          tgt->data.lit_string.length > 0) {
        const TyperBinding* b = typer__scope_resolve(tc,
            tgt->data.lit_string.value,
            tgt->data.lit_string.length,
            tgt->scope_mark);
        if (b && b->type == TYPE_STRUCT) {
          tgt_t = TYPE_STRUCT;
          tgt_sidx = b->struct_idx;
        }
      }
      /* Stage 5b: auto-deref a [Ptr Struct] receiver. The field-set
       * then resolves against the pointee struct identically to a
       * direct struct receiver — the compiler emits OP_PTR_STORE
       * instead of OP_HEAP_RECORD_SET. Scalar pointees ([Ptr i32])
       * have no field surface and fall through to TYPE_DYN. */
      if (tgt_t == TYPE_PTR && tgt_sidx != UINT32_MAX &&
          !JACL_IS_SCALAR_TYPE_IDX(tgt_sidx)) {
        tgt_t = TYPE_STRUCT;
      }
      if (tgt_t == TYPE_STRUCT && tgt_sidx < tc->struct_count &&
          fld->type == AST_LIT_STRING) {
        const TyperStruct* sd = &tc->structs[tgt_sidx];
        const char* fn  = fld->data.lit_string.value;
        uint32_t    fnl = fld->data.lit_string.length;
        for (uint32_t fi = 0; fi < sd->field_count; fi++) {
          if (sd->field_name_lens[fi] != fnl ||
              memcmp(sd->field_names[fi], fn, fnl) != 0) continue;
          JaclType field_t = (JaclType)sd->field_types[fi];
          JaclType val_t   = (JaclType)val->inferred_type;
          if (field_t != TYPE_DYN && val_t != TYPE_DYN &&
              val_t != field_t &&
              !(field_t == TYPE_STRUCT && val_t == TYPE_STRUCT)) {
            char err[224];
            jacl_format_field_mismatch(err, sizeof(err),
                sd->name, sd->name_len, fn, fnl, field_t, val_t);
            typer__error(tc, val->start.line, val->start.column, err);
          } else if (field_t != TYPE_DYN && val_t == TYPE_DYN) {
            char err[256];
            jacl_format_field_dyn_assign(err, sizeof(err),
                sd->name, sd->name_len, fn, fnl, field_t);
            typer__error(tc, val->start.line, val->start.column, err);
          }
          break;
        }
      }
      node->inferred_type = TYPE_NIL;
    } else if (hid == HEAD_DOT &&
               node->data.command.arg_count == 2) {
      /* [. struct field] arrow access — result type is the accessed
       * field's declared type. For struct-typed fields, propagate
       * inferred_struct_idx so chained access (`$x.field.subfield`)
       * resolves the subfield's type. */
      AstNode* tgt = node->data.command.args[0];
      AstNode* fld = node->data.command.args[1];

      /* Module-binding value access: `$mod->field` where mod is a
       * `use "path" mod` binding. Narrow to the export's declared
       * type. Procs reached this way (not as a call head) are
       * closure values — narrow to TYPE_CLOSURE. Resolution falls
       * through if a local binding shadows the module name. */
      if (tgt->type == AST_VAR_REF && fld->type == AST_LIT_STRING) {
        const TyperBinding* shadow = typer__scope_resolve(tc,
            tgt->data.var_ref.name, tgt->data.var_ref.length,
            tgt->scope_mark);
        if (!shadow) {
          const TyperImportProc* bound = typer__find_bound_export(tc,
              tgt->data.var_ref.name, tgt->data.var_ref.length,
              fld->data.lit_string.value, fld->data.lit_string.length);
          if (bound) {
            node->inferred_type = (bound->arity >= 0)
                                  ? (uint8_t)TYPE_CLOSURE
                                  : bound->return_type;
            return;
          }
        }
      }

      /* Nested-buf chain at depth >= 3, or any depth-N chain rooted at
       * a TYPE_PTR-with-buf-shape binding (decomposed chains). Walks
       * the arrow chain inside-out and yields the chain's result type;
       * bails on depth <= 2 TYPE_BUF receivers so the existing
       * per-arrow handlers continue to drive those. */
      if (fld->type == AST_LIT_INT) {
        JaclType chain_t = TYPE_DYN;
        uint32_t chain_sidx = UINT32_MAX;
        if (typer__nested_buf_chain_result(tc, tgt, fld,
                                           &chain_t, &chain_sidx, NULL)) {
          node->inferred_type       = chain_t;
          node->inferred_struct_idx = chain_sidx;
          return;
        }
      }

      /* Buf or Ptr element access: `$x->N` parses as `[. $x N]` where
       * the field is an AST_LIT_INT.
       *   TYPE_BUF: bounds-checked at typer (N must be < buf_len).
       *   TYPE_PTR: no bounds check (pointer can target anything);
       *             pointee must be scalar.
       * Result narrows to the element scalar, widening small ints to
       * i32 (mirrors HEAD_BUF_GET / OP_PTR_LOAD widening). See
       * BUFFER_DESIGN.md M3. */
      if (tgt->type == AST_VAR_REF && fld->type == AST_LIT_INT) {
        const TyperBinding* b = typer__scope_resolve(tc,
            tgt->data.var_ref.name, tgt->data.var_ref.length,
            tgt->scope_mark);
        if (b && (b->type == TYPE_BUF || b->type == TYPE_PTR) &&
            b->struct_idx != UINT32_MAX) {
          int32_t idx_lit = fld->data.lit_int.value;
          if (b->type == TYPE_BUF &&
              (idx_lit < 0 || (uint32_t)idx_lit >= b->buf_len)) {
            char buf_ty[96];
            /* Nested form [Buf N [Buf M T]] (Phase 5b: registry-encoded).
             * b->struct_idx points at a TYPE_SHAPE_BUF entry; read M
             * and T from the shape. */
            if (b->struct_idx >= TYPER_MAX_STRUCTS &&
                b->struct_idx < tc->shape_reg.count &&
                tc->shape_reg.shapes[b->struct_idx].kind == TYPE_SHAPE_BUF) {
              TypeShape* inner = &tc->shape_reg.shapes[b->struct_idx];
              uint32_t inner_M = inner->u.buf.len;
              uint32_t inner_t_enc = inner->u.buf.elem_idx;
              if (JACL_IS_SCALAR_TYPE_IDX(inner_t_enc)) {
                const char* en = type_name(JACL_TYPE_IDX_TO_SCALAR(inner_t_enc));
                snprintf(buf_ty, sizeof(buf_ty),
                         "[Buf %u [Buf %u %s]]",
                         (unsigned)b->buf_len, (unsigned)inner_M, en);
              } else if (inner_t_enc < tc->struct_count) {
                const TyperStruct* sd = &tc->structs[inner_t_enc];
                snprintf(buf_ty, sizeof(buf_ty),
                         "[Buf %u [Buf %u %.*s]]",
                         (unsigned)b->buf_len, (unsigned)inner_M,
                         (int)sd->name_len, sd->name);
              } else {
                snprintf(buf_ty, sizeof(buf_ty),
                         "[Buf %u [Buf %u ...]]",
                         (unsigned)b->buf_len, (unsigned)inner_M);
              }
            } else if (JACL_IS_SCALAR_TYPE_IDX(b->struct_idx)) {
              const char* en = type_name(
                  JACL_TYPE_IDX_TO_SCALAR(b->struct_idx));
              jacl_format_buf_type(buf_ty, sizeof(buf_ty),
                                   b->buf_len, en, (uint32_t)strlen(en));
            } else if (b->struct_idx < tc->struct_count) {
              const TyperStruct* sd = &tc->structs[b->struct_idx];
              jacl_format_buf_type(buf_ty, sizeof(buf_ty),
                                   b->buf_len, sd->name, sd->name_len);
            } else {
              jacl_format_buf_type(buf_ty, sizeof(buf_ty),
                                   b->buf_len, "T", 1);
            }
            char err[192];
            snprintf(err, sizeof(err),
                "type error: buf index %d out of bounds for %s",
                (int)idx_lit, buf_ty);
            typer__error(tc, fld->start.line, fld->start.column, err);
            node->inferred_type = TYPE_DYN;
            return;
          }
          if (b->type == TYPE_BUF) {
            uint32_t inner_v = UINT32_MAX, inner_k = UINT32_MAX;
            JaclType elem = typer__buf_elem_decode(tc, b->struct_idx,
                                                   &inner_v, &inner_k);
            /* Nested buf [Buf N [Buf M T]]: $matrix->i yields [Ptr T]
             * (decay-style). Phase 5b moved the inner-M / T encoding
             * into a TYPE_SHAPE_BUF registry entry; decode to spot
             * the nested case via TYPE_BUF kind. For depth-3+, the
             * leaf T isn't directly addressable through a single
             * [Ptr T] decay -- error and point at the workaround. */
            if (elem == TYPE_BUF) {
              uint32_t leaf_inner_v = UINT32_MAX;
              JaclType leaf_kind = typer__buf_elem_decode(tc, inner_v,
                                                          &leaf_inner_v, NULL);
              if (leaf_kind == TYPE_BUF) {
                /* depth-3+ arrow chain: codegen isn't ready, but [addr]
                 * intercepts before this path (see HEAD_ADDR handler).
                 * Stay quiet here -- the compiler will catch any real
                 * misuse (e.g. `$cube->i->j->k`) with a clearer error
                 * at codegen time. Leaving as TYPE_DYN propagates to a
                 * compile-time error rather than a typer error. */
                node->inferred_type = TYPE_DYN;
                return;
              }
              /* depth-2: yield [Ptr T_leaf] (existing semantic). */
              node->inferred_type       = TYPE_PTR;
              node->inferred_struct_idx = inner_v;
              return;
            }
            switch (elem) {
              case TYPE_I8: case TYPE_U8:
              case TYPE_I16: case TYPE_U16:
                node->inferred_type = TYPE_I32; break;
              case TYPE_TYPED_VEC:
                node->inferred_type = TYPE_TYPED_VEC;
                node->inferred_struct_idx = inner_v;
                break;
              case TYPE_TYPED_MAP:
                node->inferred_type = TYPE_TYPED_MAP;
                node->inferred_struct_idx = inner_v;
                node->inferred_key_struct_idx = inner_k;
                break;
              case TYPE_PTR:
                node->inferred_type = TYPE_PTR;
                node->inferred_struct_idx = inner_v;
                break;
              case TYPE_FUTURE:
                node->inferred_type = TYPE_FUTURE;
                node->inferred_struct_idx = inner_v;
                break;
              case TYPE_STRUCT:
                node->inferred_type = TYPE_STRUCT;
                node->inferred_struct_idx = b->struct_idx;
                break;
              default:
                node->inferred_type = elem;
                break;
            }
          } else if (JACL_IS_SCALAR_TYPE_IDX(b->struct_idx)) {
            /* TYPE_PTR branch: scalar pointee. */
            JaclType elem = JACL_TYPE_IDX_TO_SCALAR(b->struct_idx);
            switch (elem) {
              case TYPE_I8: case TYPE_U8:
              case TYPE_I16: case TYPE_U16:
                node->inferred_type = TYPE_I32; break;
              default:
                node->inferred_type = elem; break;
            }
          } else {
            /* TYPE_PTR struct pointee: result is the inline struct value. */
            node->inferred_type       = TYPE_STRUCT;
            node->inferred_struct_idx = b->struct_idx;
          }
          return;
        }
      }

      /* Same shape but receiver is an arbitrary expression with
       * inferred_type TYPE_PTR — covers field-access chains like
       * `$h->magic->0` where `$h->magic` returns [Ptr u8]. Bufs
       * aren't expression-result types in JACL, so only TYPE_PTR
       * needs the generalization. No bounds check (TYPE_PTR
       * branch above is also bounds-check-free). For struct pointee
       * (`$grid->i->j` against `[Buf N [Buf M Point]]`), the result
       * is the inline struct value -- M4.2.2. See
       * BUFFER_DESIGN.md "Receiver-shape generalization". */
      if (fld->type == AST_LIT_INT &&
          (JaclType)tgt->inferred_type == TYPE_PTR &&
          tgt->inferred_struct_idx != UINT32_MAX) {
        if (JACL_IS_SCALAR_TYPE_IDX(tgt->inferred_struct_idx)) {
          JaclType elem = JACL_TYPE_IDX_TO_SCALAR(tgt->inferred_struct_idx);
          switch (elem) {
            case TYPE_I8: case TYPE_U8:
            case TYPE_I16: case TYPE_U16:
              node->inferred_type = TYPE_I32; break;
            default:
              node->inferred_type = elem; break;
          }
          return;
        }
        if (tgt->inferred_struct_idx < tc->struct_count) {
          node->inferred_type       = TYPE_STRUCT;
          node->inferred_struct_idx = tgt->inferred_struct_idx;
          return;
        }
      }

      /* Resolve target struct type. Two shapes:
       *   - tgt was already typed as STRUCT (e.g. $ln var-ref or a
       *     nested dot expression).
       *   - tgt is a bare LIT_STRING name on the LHS of a `set` chain
       *     (e.g. `set ln->start->x 77` parses with bare `ln`). The
       *     compiler's HEAD_SET rewrite later converts it to a
       *     VAR_REF; we look up the binding here so the typer's
       *     annotation matches what the rewrite produces. */
      JaclType    tgt_t = (JaclType)tgt->inferred_type;
      uint32_t    tgt_sidx = tgt->inferred_struct_idx;
      if (tgt_t != TYPE_STRUCT && tgt->type == AST_LIT_STRING &&
          tgt->data.lit_string.length > 0) {
        const TyperBinding* b = typer__scope_resolve(tc,
            tgt->data.lit_string.value,
            tgt->data.lit_string.length,
            tgt->scope_mark);
        if (b && b->type == TYPE_STRUCT) {
          tgt_t = TYPE_STRUCT;
          tgt_sidx = b->struct_idx;
        }
      }
      /* Stage 5b: auto-deref [Ptr Struct] receiver to its pointee for
       * field resolution. The compiler emits OP_PTR_LOAD with the
       * field's offset and type. Scalar pointees fall through. */
      if (tgt_t == TYPE_PTR && tgt_sidx != UINT32_MAX &&
          !JACL_IS_SCALAR_TYPE_IDX(tgt_sidx)) {
        tgt_t = TYPE_STRUCT;
      }
      if (tgt_t == TYPE_STRUCT &&
          tgt_sidx < tc->struct_count &&
          fld->type == AST_LIT_STRING) {
        const TyperStruct* sd = &tc->structs[tgt_sidx];
        const char* fn = fld->data.lit_string.value;
        uint32_t    fnl = fld->data.lit_string.length;
        node->inferred_type = TYPE_DYN;
        for (uint32_t fi = 0; fi < sd->field_count; fi++) {
          if (sd->field_name_lens[fi] == fnl &&
              memcmp(sd->field_names[fi], fn, fnl) == 0) {
            JaclType ft = (JaclType)sd->field_types[fi];
            if (ft == TYPE_BUF) {
              /* Buf field access: $h->field returns [Ptr ElemType]
               * pointing at the field's first byte. See
               * BUFFER_DESIGN.md M4.3. */
              node->inferred_type       = TYPE_PTR;
              node->inferred_struct_idx = sd->field_struct_idxs[fi];
            } else {
              node->inferred_type = (uint8_t)ft;
              if (ft == TYPE_STRUCT) {
                node->inferred_struct_idx = sd->field_struct_idxs[fi];
              }
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
    node->inferred_key_struct_idx = b->key_struct_idx;
    node->inferred_buf_len = b->buf_len;
    /* Cross-registry rule: a TYPE_VEC binding may carry a TYPER-SIDE shape
     * idx for a nested element ([Vec [Vec i64]]). Shape idxs must never
     * reach AST stamps (the compiler's registry numbers differ) — suppress;
     * narrowing sites (HEAD_FOR) resolve the binding directly. */
    if ((b->type == TYPE_VEC || b->type == TYPE_MAP) &&
        b->struct_idx != UINT32_MAX &&
        !JACL_IS_SCALAR_TYPE_IDX(b->struct_idx) &&
        b->struct_idx >= TYPER_MAX_STRUCTS)
      node->inferred_struct_idx = UINT32_MAX;
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

void typer_infer(AstNode** nodes, uint32_t count, TyperResult* result_or_null,
                 TyperImportProc* imports, uint32_t import_count,
                 StructTypeRegistry* seed_registry) {
  TyperCtx tc;
  tc.binding_count = 0;
  tc.scope_depth   = 0;
  tc.proc_count    = 0;
  tc.imports       = imports;
  tc.import_count  = import_count;
  tc.result        = result_or_null;
  if (result_or_null) {
    result_or_null->error_count       = 0;
    result_or_null->first_error_line  = 0;
    result_or_null->first_error_col   = 0;
    result_or_null->first_error[0]    = '\0';
  }
  /* Reserve struct indices 0 and 1 so typer indices align with the
   * compiler's StructTypeRegistry: slot 0 is "dyn placeholder", slot
   * 1 is reserved for the ctx struct (see compiler.c
   * struct_registry__init). User structs start at slot 2.
   * Without this alignment, typer would report different struct_idx
   * values than the compiler for the same Point/Line/etc. */
  tc.struct_count  = 2;
  memset(&tc.structs[0], 0, sizeof(tc.structs[0]) * 2);

  /* Seed user structs from the persistent registry so cross-eval
   * references to structs declared in a prior jacl_eval type-check
   * (the runtime / compiler side already resolves them; without this
   * the typer reports "unknown type Pt" or stamps DYN, which then
   * trips downstream type checks). Mirrors what
   * typer__register_structs does for AST_DEFSTRUCT, but populates
   * from the registry's StructTypeDef instead of the AST. Slot
   * indices are preserved 1:1 with the compiler's registry so
   * struct_idx values stamped on AST nodes agree across passes. */
  if (seed_registry) {
    for (uint32_t i = 2; i < seed_registry->count && i < TYPER_MAX_STRUCTS; i++) {
      StructTypeDef* sd = seed_registry->defs[i];
      while (tc.struct_count < i && tc.struct_count < TYPER_MAX_STRUCTS) {
        memset(&tc.structs[tc.struct_count++], 0, sizeof(tc.structs[0]));
      }
      if (tc.struct_count >= TYPER_MAX_STRUCTS) break;
      if (!sd) {  /* shape-only slot (e.g. [Vec T] interned shape) */
        memset(&tc.structs[tc.struct_count++], 0, sizeof(tc.structs[0]));
        continue;
      }
      TyperStruct* s = &tc.structs[tc.struct_count++];
      s->name     = sd->name;
      s->name_len = sd->name_len;
      uint32_t fc = sd->field_count;
      if (fc > TYPER_MAX_STRUCT_FIELDS) fc = TYPER_MAX_STRUCT_FIELDS;
      s->field_count = (uint8_t)fc;
      for (uint32_t fi = 0; fi < fc; fi++) {
        s->field_types[fi]       = (uint8_t)sd->fields[fi].type;
        s->field_names[fi]       = sd->fields[fi].name;
        s->field_name_lens[fi]   = sd->fields[fi].name_len;
        s->field_struct_idxs[fi] = sd->fields[fi].struct_type_idx;
      }
    }
  }
  tc.expected_type = TYPE_DYN;
  tc.yield_elem_struct_idx = UINT32_MAX;
  tc.narrowing_count = 0;
  /* Typer-side shape registry. Indices start at TYPER_MAX_STRUCTS so
   * they don't collide with struct indices in tc.structs[]. Decoders
   * tell struct-vs-shape apart from the value alone (struct idx <
   * TYPER_MAX_STRUCTS; shape idx >= TYPER_MAX_STRUCTS; scalar sentinel
   * >= JACL_SCALAR_VEC_BASE = 0xFF00). NULL arena because no
   * StructTypeDef allocations happen on the typer side. Freed below. */
  struct_registry__init_at_offset(&tc.shape_reg, TYPER_MAX_STRUCTS);

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
  uint32_t ctx_struct_idx = typer__register_ctx_struct(&tc, nodes, count, seed_registry);

  /* Builtin: $ctx is always a struct (the ctx record). Bind it after
   * the ctx struct is registered so $ctx.field resolves through the
   * existing HEAD_DOT field-type path. If no ctx fields were declared,
   * fall back to UINT32_MAX (no struct registry entry). */
  typer__scope_add(&tc, "ctx", 3, 0, TYPE_STRUCT, ctx_struct_idx);

  typer__register_procs(&tc, nodes, count);

  /* Imports pre-pass: register top-level destructuring imports
   * (`binding == NULL`). Callable procs (arity >= 0) go into
   * tc.procs; value imports (arity < 0) go into the binding scope
   * so `$x` var-ref narrows to the declared type. Module-binding
   * entries (`binding != NULL`, e.g. `use "lib" m` → `[$m->fn]`
   * or `$m->field`) stay in tc.imports and are looked up at use
   * sites by typer__find_bound_export.
   *
   * Skipped if a same-name proc/binding is already registered (a
   * local def/proc shadows the import — matches runtime lexical
   * resolution). Struct constructors are filtered out by the
   * compiler before the array reaches the typer. */
  for (uint32_t i = 0; i < import_count; i++) {
    TyperImportProc* imp = &imports[i];
    if (imp->binding) continue;  /* module-binding form: deferred to use site */
    if (imp->arity < 0) {
      /* Value import: register as a top-level TyperBinding. */
      if (typer__scope_resolve(&tc, imp->name, imp->name_len, 0)) continue;
      typer__scope_add(&tc, imp->name, imp->name_len, 0,
                       imp->return_type, UINT32_MAX);
      continue;
    }
    if (tc.proc_count >= TYPER_MAX_PROCS) break;
    if (typer__find_proc(&tc, imp->name, imp->name_len)) continue;
    TyperProc* p = &tc.procs[tc.proc_count++];
    p->name              = imp->name;
    p->name_len          = imp->name_len;
    p->return_type       = imp->return_type;
    p->return_struct_idx = UINT32_MAX;
    uint32_t pc = imp->param_count;
    if (pc > TYPER_MAX_PROC_PARAMS) pc = TYPER_MAX_PROC_PARAMS;
    p->param_count = (uint8_t)pc;
    for (uint32_t pi = 0; pi < pc; pi++) {
      p->param_types[pi] = imp->param_types[pi];
    }
  }

  for (uint32_t i = 0; i < count; i++) {
    typer__infer_node(&tc, nodes[i]);
  }

  struct_registry__destroy(&tc.shape_reg);
}

#endif /* TYPER_C */
