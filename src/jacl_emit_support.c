/* jacl_emit_support.c — the small surface the emit path needs that normally comes from the
 * legacy bytecode backend (bytecode.c / compiler.c / vm.c), which the emit-only unity
 * (jacl_emit.c) deliberately excludes.
 *
 * These mirror the definitions in those files. They never share a translation unit with them
 * (the emit unity omits the legacy TUs; the native unity omits this file), so the parallel
 * copies never conflict — exactly the pattern src/jacl.h already uses to mirror the tree for
 * foreign linkers. Include AFTER value.c/gc.c/ast.c (needs JaclVal, ThreadHeap,
 * JaclInternTable, HeadId + ast__head_id_for) and BEFORE syntax.c (which uses these types).
 *
 * Everything here is reachable from the emit build only through the macro-expansion engine in
 * syntax.c; the legacy bytecode evaluator it used to call is guarded out by JACL_EMIT_ONLY. */
#ifndef JACL_EMIT_SUPPORT_C
#define JACL_EMIT_SUPPORT_C

/* jacl_context_s is only ever named through a pointer here — incomplete is sufficient. */
typedef struct jacl_context_s jacl_context_t;   /* ExpandState.ctx — pointer only, never derefed */

/* BytecodeChunk + JaclClosure need FULL layouts: gc_collect.c traces closure upvalues, and
 * MacroEntry embeds a JaclClosure*. Mirror of bytecode.c:393-514 (that file is not linked here). */
typedef struct {
  uint8_t*  code;
  uint32_t  code_count;
  uint32_t  code_cap;
  JaclVal*  constants;
  uint32_t  const_count;
  uint32_t  const_cap;
  uint32_t* lines;
  uint32_t  lines_cap;
  arena_t*  arena;
} BytecodeChunk;

typedef struct {
  BytecodeChunk chunk;
  uint8_t       param_count;
  uint8_t       param_total_slots;
  bool          has_inline_params;
  JaclVal*      param_names;
  JaclVal*      upvalues;
  uint8_t       upvalue_count;
  uint16_t      upvalue_total_slots;
  const char*   name;
  uint8_t       min_args;
  bool          variadic;
  bool          pinned;
  int8_t        pin_worker_id;
  bool          is_generator;
  bool          is_sm_compiled;
  uint8_t       sm_field_count;
  uint8_t       upvalue_inline_bitmap[32];
  uint32_t      gen_elem_idx;
  uint32_t      proc_shape_idx;
} JaclClosure;

/* --- Macro table (mirrors compiler.c:3121-3138) --- */
#define MACRO_TABLE_MAX 64
typedef struct {
  const char*   name;
  uint32_t      name_len;
  uint32_t      param_count;
  bool          variadic;
  bool          is_builtin;
  const char**  param_names;
  uint32_t*     param_name_lens;
  JaclClosure*  closure;
  AstNode*      body;
} MacroEntry;

typedef struct MacroTable MacroTable;
struct MacroTable {
  MacroEntry entries[MACRO_TABLE_MAX];
  uint32_t   count;
};

/* --- Macro expansion state (mirrors compiler.c:39-57) --- */
typedef struct {
    const char *name;
    uint32_t    name_len;
    uint32_t    line;
    uint32_t    col;
} ExpandFrame;

#define EXPAND_FRAME_MAX 256

typedef struct {
    const char  *error_msg;
    uint32_t     error_line;
    uint32_t     error_col;
    ExpandFrame  frames[EXPAND_FRAME_MAX];
    uint32_t     frame_top;
    uint32_t     scope_counter;
    uint32_t     gensym_counter;
    jacl_context_t *ctx;   /* NULL on the emit path — the legacy evaluator is never entered */
} ExpandState;

/* --- Scoped-context snapshot (mirrors vm.c:282-286) --- */
typedef struct {
    ThreadHeap *heap;
    void       (*gc_fn)(void *);
    void        *gc_ctx;
} jacl_ctx_saved_t;

/* --- Macro-table helpers (mirror compiler.c:3140-3173) — pure heap/name logic, no bytecode. --- */
void macro_table_init(MacroTable* t) { t->count = 0; }

MacroEntry* macro_table_lookup(MacroTable* t, const char* name, uint32_t name_len) {
  for (uint32_t i = 0; i < t->count; i++) {
    if (t->entries[i].name_len == name_len &&
        memcmp(t->entries[i].name, name, name_len) == 0) {
      return &t->entries[i];
    }
  }
  return NULL;
}

bool macro__is_special_form(const char* name, uint32_t len) {
  switch (ast__head_id_for(name, len)) {
    case HEAD_IF:           case HEAD_TO:
    case HEAD_DEF:          case HEAD_MUT:
    case HEAD_SET:          case HEAD_FOR:
    case HEAD_TRY:          case HEAD_PROC:
    case HEAD_WHILE:        case HEAD_BREAK:
    case HEAD_MATCH:        case HEAD_QUOTE:
    case HEAD_SPAWN:        case HEAD_YIELD:
    case HEAD_AWAIT:        case HEAD_RETURN:
    case HEAD_SLEEP:
    case HEAD_DEFMACRO:     case HEAD_CONTINUE:
    case HEAD_PARALLEL:     case HEAD_DEFSTRUCT:
    case HEAD_SYNTAX_QUOTE: return true;
    default:                return false;
  }
}

/* --- Emit-only stubs for the legacy context lifecycle (vm.c). The emit path never runs the
 * bytecode macro evaluator (the SVM staging hook does all expansion), so a NULL context with
 * no-op save/restore is correct: `es->ctx`/`tmp_ctx` stay NULL and every `if (tmp_ctx)` cleanup
 * in ast_expand_macros is skipped. --- */
void jacl_ctx_save(jacl_ctx_saved_t *saved) { (void)saved; }
jacl_context_t *jacl_ctx_new(jacl_context_t *parent) { (void)parent; return NULL; }
void jacl_ctx_destroy(jacl_context_t *ctx) { (void)ctx; }
void jacl_ctx_restore(jacl_ctx_saved_t saved) { (void)saved; }

#endif /* JACL_EMIT_SUPPORT_C */
