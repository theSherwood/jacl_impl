/*
 * JACL Compiler
 *
 * Translates AST (from parser) into bytecode chunks for the VM.
 */

#ifndef COMPILER_C
#define COMPILER_C

#include <limits.h>
#include <string.h>

/* --- Forward declarations for struct types --- */
typedef struct StructTypeRegistry StructTypeRegistry;
typedef struct MacroTable MacroTable;

/* --- Compile Result --- */

typedef struct {
  BytecodeChunk chunk;
  uint32_t      error_count;
  const char*   error_message;  /* first error message, or NULL */
  bool          suspending;     /* true if top-level code is state-machine transformed */
  StructTypeRegistry* struct_registry; /* struct type metadata for VM */
  MacroTable*   macro_table;    /* compile-time macro definitions (NULL if none) */
} CompileResult;

/* Forward-declare jacl_context_t for ExpandState (defined later in vm.c) */
#ifndef JACL_CONTEXT_FWD
#define JACL_CONTEXT_FWD
typedef struct jacl_context_s jacl_context_t;
#endif

/* --- ExpandState: per-compilation macro expansion state (reentrancy-safe) ---
 * These types are also declared in jacl.h for external consumers. In the
 * unity build they are defined here (compiler.c comes before syntax.c). */
#ifndef EXPAND_FRAME_MAX

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
    jacl_context_t *ctx;              /* context for macro eval (NULL if N/A) */
} ExpandState;

#endif /* EXPAND_FRAME_MAX */

/* --- API --- */

CompileResult compiler_compile(ParseResult parse, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap,
                                      StructTypeRegistry* seed_registry,
                                      ExpandState* es,
                                      JaclVal prelude_map);

/* jacl_compile_program forward-declared after ProgramResult (below) */

/* Forward declarations for syntax.c functions used by the compiler
 * (syntax.c is included after compiler.c in the unity build) */
JaclVal syntax_from_ast(AstNode *node, ThreadHeap *heap, JaclInternTable *intern);
const char *ast_expand_macros(AstNode **program, uint32_t count,
                              MacroTable *macros, ThreadHeap *heap,
                              JaclInternTable *intern, arena_t *arena,
                              ExpandState *es,
                              uint32_t *out_error_line, uint32_t *out_error_col);

/* JaclType, type-keyword table, and type predicates moved to ast.c so
 * the typer pass (included before compiler.c in the unity build) can
 * use them. The definitions are now visible here via that earlier include. */

/* Sentinel range for scalar element types in typed collections:
 * 0xFF00 + (uint8_t)JaclType. Distinct from struct registry indices
 * (which top out around the few hundred range). */
#define COMPILER_SCALAR_VEC_BASE         JACL_SCALAR_VEC_BASE
#define COMPILER_IS_SCALAR_TYPE_IDX(idx) JACL_IS_SCALAR_TYPE_IDX(idx)
#define COMPILER_SCALAR_TYPE_IDX(t)      JACL_SCALAR_TYPE_IDX(t)
#define COMPILER_TYPE_IDX_TO_SCALAR(idx) JACL_TYPE_IDX_TO_SCALAR(idx)

/* Element-type keywords supported as typed-collection scalars.
 * Restricted to numeric value types — no GC tracing needed in typed
 * leaves. (bool literal forms parse heterogeneously across var-ref vs
 * call sites, so it's left out of the v1 set.) */
static bool compiler__is_typed_collection_scalar(JaclType t) {
  return t == TYPE_I32 || t == TYPE_I64 || t == TYPE_U32 || t == TYPE_U64 ||
         t == TYPE_F32 || t == TYPE_F64;
}

/* (compiler__compile_typed_elem_arg defined after the Compiler struct.) */

/* Check if an AST_COMMAND node is a typed collection expression.
   Returns 1 for [Vec Type], 2 for [Map Type] (dyn keys),
   3 for [Map KeyType ValueType] (struct keys), 0 if not a match.
   *out_elem is set to the value element type name node.
   *out_key_elem is set to the key type name node (kind==3 only). */
static int compiler__typed_collection_expr(AstNode* cmd, AstNode** out_elem,
                                           AstNode** out_key_elem) {
  if (cmd->type != AST_COMMAND || !cmd->data.command.head) return 0;
  AstNode* th = cmd->data.command.head;
  if (th->type != AST_LIT_STRING || th->data.lit_string.length != 3) return 0;
  int kind = 0;
  if (memcmp(th->data.lit_string.value, "Vec", 3) == 0) kind = 1;
  else if (memcmp(th->data.lit_string.value, "Map", 3) == 0) kind = 2;
  if (kind == 0) return 0;
  /* [Map K V] — struct keys (2 args) */
  if (kind == 2 && cmd->data.command.arg_count == 2 &&
      cmd->data.command.args[0]->type == AST_LIT_STRING &&
      cmd->data.command.args[1]->type == AST_LIT_STRING) {
    if (out_key_elem) *out_key_elem = cmd->data.command.args[0];
    if (out_elem) *out_elem = cmd->data.command.args[1];
    return 3;
  }
  if (cmd->data.command.arg_count != 1 ||
      cmd->data.command.args[0]->type != AST_LIT_STRING) return 0;
  if (out_elem) *out_elem = cmd->data.command.args[0];
  return kind;
}

/* Recognize a [Stream T] type-annotation expression. Returns true and
 * sets *out_elem to the element type-name node. Same shape as
 * [Future T] / [Ptr T] — single type-name argument. The compiler
 * doesn't drive stream-element typing; this is just a parser-shape
 * recognizer so proc-return annotations can carry the [Stream T]
 * syntax through to the typer, which then propagates the element
 * idx onto the node via JACL_SCALAR_TYPE_IDX / struct registry idx. */
static bool compiler__stream_type_expr(AstNode* cmd, AstNode** out_elem) {
  if (cmd->type != AST_COMMAND || !cmd->data.command.head) return false;
  AstNode* th = cmd->data.command.head;
  if (th->type != AST_LIT_STRING || th->data.lit_string.length != 6 ||
      memcmp(th->data.lit_string.value, "Stream", 6) != 0) return false;
  if (cmd->data.command.arg_count != 1) return false;
  if (cmd->data.command.args[0]->type != AST_LIT_STRING) return false;
  if (out_elem) *out_elem = cmd->data.command.args[0];
  return true;
}

/* Recognize a [Ptr T] type-annotation expression. Returns true and sets
 * *out_pointee to the pointee type-name node. The compiler doesn't drive
 * pointer typing — this is just a parser-shape recognizer so proc params
 * and def annotations can carry the [Ptr T] syntax through to the typer. */
static bool compiler__ptr_type_expr(AstNode* cmd, AstNode** out_pointee) {
  if (cmd->type != AST_COMMAND || !cmd->data.command.head) return false;
  AstNode* th = cmd->data.command.head;
  if (th->type != AST_LIT_STRING || th->data.lit_string.length != 3 ||
      memcmp(th->data.lit_string.value, "Ptr", 3) != 0) return false;
  if (cmd->data.command.arg_count != 1) return false;
  if (cmd->data.command.args[0]->type != AST_LIT_STRING) return false;
  if (out_pointee) *out_pointee = cmd->data.command.args[0];
  return true;
}

/* Returns true if a type is allowed as a struct field (value types only).
   Reference types (str, vec, map, closure, dyn, stream) are rejected. */
bool is_struct_value_type(JaclType t) {
  switch (t) {
    case TYPE_BOOL:
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_F32:
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_F64:
    case TYPE_STRUCT:
      return true;
    default:
      return false;
  }
}


/* --- Struct type registry --- */

#define STRUCT_REGISTRY_INIT_CAP 32
#define STRUCT_MAX_FIELDS   256   /* stack buffer limit for temp field arrays */

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclType    type;
  uint32_t    struct_type_idx; /* index into registry if type==TYPE_STRUCT */
  uint32_t    offset;          /* byte offset in struct memory (C-ABI) */
  uint32_t    size;            /* field size in bytes (C-ABI) */
  bool        is_mutable;      /* true if field can be written via set */
  JaclVal     default_val;     /* default value for ctx fields (JACL_NIL if none) */
} StructTypeField;

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclVal     name_val;       /* inline string (for global_arities lookup) */
  uint32_t    field_count;
  uint32_t    total_size;     /* total size including trailing padding */
  uint32_t    alignment;      /* max alignment of all fields */
  StructTypeField fields[];   /* flexible array member — variable field count */
} StructTypeDef;

struct StructTypeRegistry {
  StructTypeDef** defs;       /* defs[type_idx] → StructTypeDef* (defs[0] = NULL, reserved for dyn) */
  uint32_t count;             /* next available type_idx (starts at 1; 0 is reserved) */
  uint32_t capacity;          /* capacity of defs pointer array */
  arena_t* arena;             /* arena for StructTypeDef allocations (not owned) */
  uint32_t ctx_type_idx;      /* type_idx of the ctx struct (0 = not yet registered) */
};
/* typedef already forward-declared above */

/* Allocate a StructTypeDef with N fields in the registry's arena */
static StructTypeDef* struct_registry__alloc_def(StructTypeRegistry* reg, uint32_t field_count) {
  size_t sz = sizeof(StructTypeDef) + field_count * sizeof(StructTypeField);
  return (StructTypeDef*)arena_alloc(reg->arena, sz);
}

/* Ensure the defs pointer array has room for at least one more entry.
 * Returns false (without growing) if growing would push valid struct
 * indices into the COMPILER_SCALAR_VEC_BASE sentinel range used by
 * scalar-typed collections (see compiler.c near typed_collection_expr). */
static bool struct_registry__grow(StructTypeRegistry* reg) {
  if (reg->count < reg->capacity) return true;
  uint32_t new_cap = reg->capacity * 2;
  if (new_cap < STRUCT_REGISTRY_INIT_CAP) new_cap = STRUCT_REGISTRY_INIT_CAP;
  if (new_cap >= COMPILER_SCALAR_VEC_BASE) return false; /* sentinel collision */
  StructTypeDef** new_defs = (StructTypeDef**)realloc(reg->defs, new_cap * sizeof(StructTypeDef*));
  if (!new_defs) return false;
  /* Zero new slots */
  for (uint32_t i = reg->capacity; i < new_cap; i++) new_defs[i] = NULL;
  reg->defs = new_defs;
  reg->capacity = new_cap;
  return true;
}

/* True for user-defined structs (which use the inline representation).
   False for ctx, the lone HeapRecord builtin. With ref fields rejected at
   defstruct, every user struct is value-type by construction; the only
   non-inline struct is ctx, identified by reg->ctx_type_idx. */
static inline bool struct_def_is_user(const StructTypeDef* sdef,
                                      const StructTypeRegistry* reg) {
  if (!sdef || !reg) return false;
  /* ctx slot is fixed at index 1; reg->ctx_type_idx points at it.
   * Compare by pointer so a NULL-placeholder ctx doesn't mis-classify. */
  return sdef != reg->defs[reg->ctx_type_idx];
}

/* Initialize a struct type registry. Container is arena-allocated; defs array is heap-allocated.
   arena: the arena used for StructTypeDef allocations (must outlive the registry). */
static void struct_registry__init(StructTypeRegistry* reg, arena_t* arena) {
  reg->arena = arena;
  reg->capacity = STRUCT_REGISTRY_INIT_CAP;
  reg->defs = (StructTypeDef**)calloc(reg->capacity, sizeof(StructTypeDef*));
  /* slot 0 is reserved for plain dyn JaclVal boxes;
   * slot 1 is reserved for the ctx struct (filled by
   * ctx_field_list__finalize, which now patches in place). User
   * structs register starting at slot 2. */
  reg->count = 2;
  reg->defs[0] = NULL;
  reg->defs[1] = NULL;
  reg->ctx_type_idx = 1;
}

/* Free the heap-allocated defs pointer array. Does not free StructTypeDef entries
   (those live in the arena and are freed when the arena is destroyed). */
static void struct_registry__destroy(StructTypeRegistry* reg) {
  if (!reg) return;
  free(reg->defs);
  reg->defs = NULL;
  reg->count = 0;
  reg->capacity = 0;
}

/* --- Ctx field list: accumulates ctx declarations during compilation --- */

#define CTX_MAX_FIELDS 64

typedef struct {
  const char* name;
  uint32_t    name_len;
  const char* type_name;
  uint32_t    type_name_len;
  JaclType    type;
  uint32_t    struct_type_idx;
  bool        is_mutable;
  uint32_t    offset;   /* byte offset within ctx struct (computed on add) */
  uint32_t    size;     /* field size in bytes (computed on add) */
  JaclVal     default_val; /* default value for ctx initialization */
} CtxField;

typedef struct {
  CtxField fields[CTX_MAX_FIELDS];
  uint32_t count;
} CtxFieldList;

static void ctx_field_list__init(CtxFieldList* list) {
  list->count = 0;
}

/* Forward declarations for layout helpers used by ctx field offset computation */
uint32_t struct__type_size(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx);
uint32_t struct__type_align(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx);
uint32_t struct__align_up(uint32_t offset, uint32_t align);

/* Add a built-in or user-declared ctx field. Returns false if full or duplicate.
   Computes byte offset incrementally using C-ABI layout rules. */
static bool ctx_field_list__add(CtxFieldList* list,
                                const char* name, uint32_t name_len,
                                const char* type_name, uint32_t type_name_len,
                                JaclType type, uint32_t struct_type_idx,
                                bool is_mutable,
                                StructTypeRegistry* reg,
                                JaclVal default_val) {
  if (list->count >= CTX_MAX_FIELDS) return false;

  /* Compute offset from previous fields */
  uint32_t offset = 0;
  if (list->count > 0) {
    CtxField* prev = &list->fields[list->count - 1];
    offset = prev->offset + prev->size;
  }
  uint32_t fsize  = struct__type_size(type, reg, struct_type_idx);
  uint32_t falign = struct__type_align(type, reg, struct_type_idx);
  offset = struct__align_up(offset, falign);

  CtxField* f = &list->fields[list->count++];
  f->name           = name;
  f->name_len       = name_len;
  f->type_name      = type_name;
  f->type_name_len  = type_name_len;
  f->type           = type;
  f->struct_type_idx = struct_type_idx;
  f->is_mutable     = is_mutable;
  f->offset         = offset;
  f->size           = fsize;
  f->default_val    = default_val;
  return true;
}

/* Find a field by name; returns pointer or NULL */
static CtxField* ctx_field_list__find(CtxFieldList* list, const char* name, uint32_t name_len) {
  for (uint32_t i = 0; i < list->count; i++) {
    if (list->fields[i].name_len == name_len &&
        memcmp(list->fields[i].name, name, name_len) == 0)
      return &list->fields[i];
  }
  return NULL;
}

/* Check if a field name already exists in the list */
static bool ctx_field_list__has(CtxFieldList* list, const char* name, uint32_t name_len) {
  return ctx_field_list__find(list, name, name_len) != NULL;
}

/* Evaluate a compile-time constant AST node to a JaclVal default.
   Handles int, float, bool (true/false as AST_LIT_STRING), and short strings. */
static JaclVal ctx_eval_const_default(AstNode* dexpr, JaclType ftype) {
  if (!dexpr) return JACL_NIL;
  switch (dexpr->type) {
    case AST_LIT_INT:
      if (ftype == TYPE_I32) return jacl_i32(dexpr->data.lit_int.value);
      else if (ftype == TYPE_U32) return jacl_u32((uint32_t)dexpr->data.lit_int.value);
      else return jacl_i32(dexpr->data.lit_int.value);
    case AST_LIT_FLOAT:
      return jacl_f32(dexpr->data.lit_float.value);
    case AST_LIT_STRING: {
      const char* s = dexpr->data.lit_string.value;
      uint32_t sl = dexpr->data.lit_string.length;
      if (ftype == TYPE_BOOL) {
        if (sl == 4 && memcmp(s, "true", 4) == 0) return jacl_bool(true);
        else if (sl == 5 && memcmp(s, "false", 5) == 0) return jacl_bool(false);
      } else {
        /* Short strings fit inline; longer strings remain NIL (no heap at compile time) */
        if (sl <= 7) return jacl_inline_string(s, sl);
      }
      return JACL_NIL;
    }
    default:
      return JACL_NIL;
  }
}

/* Finalize the ctx struct: build a StructTypeDef from accumulated ctx fields
   and register it in the struct registry. Called after all modules are compiled.
   Returns the type_idx or 0 on error. */
static uint32_t ctx_field_list__finalize(CtxFieldList* list, StructTypeRegistry* reg) {
  if (!list || list->count == 0 || !reg) return 0;

  /* Ctx occupies the pre-reserved slot 1 (struct_registry__init).
   * Just fill the placeholder in place. */
  uint32_t type_idx = reg->ctx_type_idx;

  /* Compute C-ABI layout */
  StructTypeField tmp_fields[CTX_MAX_FIELDS];
  uint32_t offset = 0;
  uint32_t max_align = 1;

  for (uint32_t i = 0; i < list->count; i++) {
    CtxField* cf = &list->fields[i];
    uint32_t fsize  = struct__type_size(cf->type, reg, cf->struct_type_idx);
    uint32_t falign = struct__type_align(cf->type, reg, cf->struct_type_idx);
    offset = struct__align_up(offset, falign);

    tmp_fields[i].name           = cf->name;
    tmp_fields[i].name_len       = cf->name_len;
    tmp_fields[i].type           = cf->type;
    tmp_fields[i].struct_type_idx = cf->struct_type_idx;
    tmp_fields[i].offset         = offset;
    tmp_fields[i].size           = fsize;
    tmp_fields[i].is_mutable     = cf->is_mutable;
    tmp_fields[i].default_val    = cf->default_val;

    offset += fsize;
    if (falign > max_align) max_align = falign;
  }

  /* Allocate StructTypeDef */
  StructTypeDef* sdef = struct_registry__alloc_def(reg, list->count);
  if (!sdef) {
    return 0;
  }
  sdef->name        = "ctx";
  sdef->name_len    = 3;
  sdef->name_val    = JACL_NIL;
  sdef->field_count = list->count;
  sdef->total_size  = struct__align_up(offset, max_align);
  sdef->alignment   = max_align;
  /* Ctx is the lone HeapRecord — accessed via pointer-deref opcodes,
     not subject to the no-ref-fields rule that applies to user defstructs.
     The struct_def_is_user helper distinguishes ctx via reg->ctx_type_idx. */
  memcpy(sdef->fields, tmp_fields, list->count * sizeof(StructTypeField));

  reg->defs[type_idx] = sdef;
  return type_idx;
}

/* C-ABI size and alignment for a JaclType */
uint32_t struct__type_size(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx) {
  switch (t) {
    case TYPE_BOOL:    return 1;
    case TYPE_NIL:     return 0;
    case TYPE_I8:
    case TYPE_U8:      return 1;
    case TYPE_I16:
    case TYPE_U16:     return 2;
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_F32:     return 4;
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_F64:     return 8;
    case TYPE_STR:
    case TYPE_VEC:
    case TYPE_MAP:
    case TYPE_CLOSURE:
    case TYPE_DYN:
    case TYPE_STREAM:  return 8; /* JaclVal / pointer */
    case TYPE_STRUCT:
      if (reg && struct_idx < reg->count && reg->defs[struct_idx]) {
        return reg->defs[struct_idx]->total_size;
      }
      return 8; /* fallback */
    default:           return 8;
  }
}

uint32_t struct__type_align(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx) {
  switch (t) {
    case TYPE_BOOL:    return 1;
    case TYPE_NIL:     return 1;
    case TYPE_I8:
    case TYPE_U8:      return 1;
    case TYPE_I16:
    case TYPE_U16:     return 2;
    case TYPE_I32:
    case TYPE_U32:
    case TYPE_F32:     return 4;
    case TYPE_I64:
    case TYPE_U64:
    case TYPE_F64:     return 8;
    case TYPE_STR:
    case TYPE_VEC:
    case TYPE_MAP:
    case TYPE_CLOSURE:
    case TYPE_DYN:
    case TYPE_STREAM:  return 8;
    case TYPE_STRUCT:
      if (reg && struct_idx < reg->count && reg->defs[struct_idx]) {
        return reg->defs[struct_idx]->alignment;
      }
      return 8;
    default:           return 8;
  }
  return 8;
}

uint32_t struct__align_up(uint32_t offset, uint32_t align) {
  return (offset + align - 1) & ~(align - 1);
}

/* Compute the number of JaclVal stack slots needed to hold a struct inline.
 * Each slot is sizeof(JaclVal) = 8 bytes. */
uint32_t struct__slot_width(StructTypeRegistry* reg, uint32_t struct_idx) {
  if (!reg || struct_idx >= reg->count || !reg->defs[struct_idx]) return 1;
  return (reg->defs[struct_idx]->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
}

/* Look up a struct type by name in the registry. Returns type_idx or UINT32_MAX if not found.
   type_idx values start at 1 (0 is reserved for plain dyn JaclVal boxes). */
uint32_t struct_registry__find(StructTypeRegistry* reg, const char* name, uint32_t name_len) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->defs[i] &&
        reg->defs[i]->name_len == name_len &&
        memcmp(reg->defs[i]->name, name, name_len) == 0) {
      return i;
    }
  }
  return UINT32_MAX;
}

/* Register an inline anonymous struct type from a canonical string like "struct{x:i32,y:i32}".
   Returns type_idx or UINT32_MAX on error. Uses structural equivalence: if an identical
   canonical string already exists in the registry, returns that index. */
uint32_t compiler__register_inline_struct(
    StructTypeRegistry* reg, const char* spec, uint32_t spec_len) {
  if (!reg) return UINT32_MAX;

  /* Check for structural equivalence (same canonical string) */
  uint32_t existing = struct_registry__find(reg, spec, spec_len);
  if (existing != UINT32_MAX) return existing;

  if (!struct_registry__grow(reg)) return UINT32_MAX;

  /* Parse the canonical string: struct{name:type,name:type,...} */
  if (spec_len < 9 || memcmp(spec, "struct{", 7) != 0 || spec[spec_len - 1] != '}')
    return UINT32_MAX;

  /* First pass: parse into temporary stack array to count fields */
  StructTypeField tmp_fields[256]; /* generous stack limit */
  uint32_t tmp_count = 0;

  const char* p = spec + 7;
  const char* end = spec + spec_len - 1;
  uint32_t offset = 0;
  uint32_t max_align = 1;

  while (p < end) {
    if (tmp_count >= 256) return UINT32_MAX;

    /* Parse field name (up to ':') */
    const char* colon = p;
    while (colon < end && *colon != ':') colon++;
    if (colon >= end) return UINT32_MAX;

    uint32_t fname_len = (uint32_t)(colon - p);
    const char* fname = p;

    /* Parse field type (up to ',' or end, handling nested struct{} braces) */
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

    /* Resolve field type */
    JaclType ftype = TYPE_DYN;
    uint32_t f_struct_idx = 0;

    if (is_type_keyword(tstart, tlen)) {
      ftype = type_from_keyword(tstart, tlen);
    } else if (tlen > 7 && memcmp(tstart, "struct{", 7) == 0) {
      /* Nested inline struct — recursive registration */
      uint32_t nested_idx = compiler__register_inline_struct(reg, tstart, tlen);
      if (nested_idx == UINT32_MAX) return UINT32_MAX;
      ftype = TYPE_STRUCT;
      f_struct_idx = nested_idx;
    } else {
      /* Named struct type */
      uint32_t idx = struct_registry__find(reg, tstart, tlen);
      if (idx == UINT32_MAX) return UINT32_MAX;
      ftype = TYPE_STRUCT;
      f_struct_idx = idx;
    }

    /* Reject reference field types — structs hold value-type bytes only.
       The caller surfaces this as "invalid inline struct type". */
    if (!is_struct_value_type(ftype)) return UINT32_MAX;

    /* Compute C-ABI layout */
    uint32_t fsize  = struct__type_size(ftype, reg, f_struct_idx);
    uint32_t falign = struct__type_align(ftype, reg, f_struct_idx);
    offset = struct__align_up(offset, falign);

    tmp_fields[tmp_count].name           = fname;
    tmp_fields[tmp_count].name_len       = fname_len;
    tmp_fields[tmp_count].type           = ftype;
    tmp_fields[tmp_count].struct_type_idx = f_struct_idx;
    tmp_fields[tmp_count].offset         = offset;
    tmp_fields[tmp_count].size           = fsize;
    tmp_fields[tmp_count].is_mutable     = false;
    tmp_fields[tmp_count].default_val    = JACL_NIL;
    tmp_count++;

    offset += fsize;
    if (falign > max_align) max_align = falign;

    /* Skip comma separator */
    p = tp;
    if (p < end && *p == ',') p++;
  }

  if (tmp_count == 0) return UINT32_MAX;

  /* Allocate StructTypeDef with exact field count in the registry arena */
  StructTypeDef* sdef = struct_registry__alloc_def(reg, tmp_count);
  if (!sdef) return UINT32_MAX;
  sdef->name        = spec;
  sdef->name_len    = spec_len;
  sdef->name_val    = JACL_NIL; /* anonymous — no constructor */
  sdef->field_count = tmp_count;
  sdef->total_size  = struct__align_up(offset, max_align);
  sdef->alignment   = max_align;
  /* Ref fields were already rejected at the per-field check above, so all
     fields here are value types — no extra flag needed. */
  memcpy(sdef->fields, tmp_fields, tmp_count * sizeof(StructTypeField));

  uint32_t idx = reg->count;
  reg->defs[idx] = sdef;
  reg->count++;
  return idx;
}

/* --- Module system structs --- */

#define COMPILER_MAX_PROC_PARAMS 16
#define MODULE_CACHE_MAX 64
#define MODULE_EXPORTS_MAX 64

typedef struct {
  const char* name;
  uint32_t    name_len;
  int16_t     arity;          /* -1 for non-procs (def/mut values) */
  bool        is_mutable;     /* true if exported as a box */
  bool        suspends;       /* true if proc is suspending */
  JaclType    type;           /* value type or return type for procs */
  JaclType    return_type;    /* proc return type (TYPE_DYN for non-procs) */
  JaclType    param_types[COMPILER_MAX_PROC_PARAMS];
  uint32_t    param_count;
} ExportEntry;

typedef struct Module {
  BytecodeChunk* chunk;        /* compiled bytecode for this module */
  const char*    path;         /* canonical (absolute) path */
  const char*    source;       /* original source text */
  ExportEntry*   exports;      /* array of exported names */
  uint32_t       export_count; /* number of exports */
  uint32_t       topo_order;   /* post-order index for topological sort */
  bool           compiled;     /* true once compilation is complete */
} Module;

/* --- Program Result (multi-module) --- */

typedef struct {
  Module**    modules;       /* modules in topological order (root last) */
  uint32_t    module_count;
  uint32_t    error_count;
  const char* error_message; /* first error message, or NULL */
  bool        suspending;    /* true if root module is state-machine transformed */
  StructTypeRegistry* struct_registry; /* struct type metadata for VM */
} ProgramResult;

ProgramResult jacl_compile_program(const char* root_path,
                                          arena_t* arena,
                                          JaclInternTable* intern_table,
                                          ThreadHeap* heap);

typedef struct {
  Module*  modules[MODULE_CACHE_MAX]; /* compiled modules by slot */
  char*    paths[MODULE_CACHE_MAX];   /* canonical path per slot */
  uint32_t count;
  uint32_t topo_counter;              /* monotonic counter for post-order assignment */
  arena_t* arena;
} ModuleCache;

void module_cache__init(ModuleCache* cache, arena_t* arena) {
  cache->count = 0;
  cache->topo_counter = 0;
  cache->arena = arena;
  for (uint32_t i = 0; i < MODULE_CACHE_MAX; i++) {
    cache->modules[i] = NULL;
    cache->paths[i]   = NULL;
  }
}

Module* module_cache__find(ModuleCache* cache, const char* canonical_path) {
  for (uint32_t i = 0; i < cache->count; i++) {
    if (cache->paths[i] && strcmp(cache->paths[i], canonical_path) == 0) {
      return cache->modules[i];
    }
  }
  return NULL;
}

Module* module_cache__add(ModuleCache* cache, const char* canonical_path) {
  if (cache->count >= MODULE_CACHE_MAX) return NULL;
  uint32_t idx = cache->count++;
  size_t path_len = strlen(canonical_path);
  char* path_copy = (char*)arena_alloc(cache->arena, path_len + 1);
  memcpy(path_copy, canonical_path, path_len + 1);
  cache->paths[idx] = path_copy;

  Module* mod = (Module*)arena_alloc(cache->arena, sizeof(Module));
  mod->chunk        = NULL;
  mod->path         = path_copy;
  mod->source       = NULL;
  mod->exports      = NULL;
  mod->export_count = 0;
  mod->topo_order   = 0;
  mod->compiled     = false;
  cache->modules[idx] = mod;
  return mod;
}

/* --- Module path resolution and circular import detection --- */

#define MODULE_IMPORT_STACK_MAX 32

typedef struct {
  const char* paths[MODULE_IMPORT_STACK_MAX]; /* canonical paths being compiled */
  uint32_t    count;
} ImportStack;

void import_stack__init(ImportStack* stack) {
  stack->count = 0;
}

bool import_stack__contains(ImportStack* stack, const char* canonical_path) {
  for (uint32_t i = 0; i < stack->count; i++) {
    if (strcmp(stack->paths[i], canonical_path) == 0) return true;
  }
  return false;
}

bool import_stack__push(ImportStack* stack, const char* canonical_path) {
  if (stack->count >= MODULE_IMPORT_STACK_MAX) return false;
  stack->paths[stack->count++] = canonical_path;
  return true;
}

void import_stack__pop(ImportStack* stack) {
  if (stack->count > 0) stack->count--;
}

/* Build a circular import chain string: "A -> B -> C -> A"
   The cycle_path is the path that was found again in the stack. */
const char* import_stack__chain_str(ImportStack* stack,
                                           const char* cycle_path,
                                           arena_t* arena) {
  /* Find the start of the cycle in the stack */
  uint32_t start = 0;
  for (uint32_t i = 0; i < stack->count; i++) {
    if (strcmp(stack->paths[i], cycle_path) == 0) {
      start = i;
      break;
    }
  }

  /* Calculate total length: filenames joined by " -> " plus final " -> first" */
  size_t total = 0;
  for (uint32_t i = start; i < stack->count; i++) {
    /* Extract basename from path for readability */
    const char* p = stack->paths[i];
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    total += strlen(base);
    if (i > start) total += 4; /* " -> " */
  }
  /* Add " -> first" at the end to show the cycle */
  const char* first_slash = strrchr(stack->paths[start], '/');
  const char* first_base = first_slash ? first_slash + 1 : stack->paths[start];
  total += 4 + strlen(first_base); /* " -> basename" */

  char* buf = (char*)arena_alloc(arena, (uint32_t)(total + 1));
  size_t pos = 0;
  for (uint32_t i = start; i < stack->count; i++) {
    if (i > start) {
      memcpy(buf + pos, " -> ", 4);
      pos += 4;
    }
    const char* p = stack->paths[i];
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    size_t len = strlen(base);
    memcpy(buf + pos, base, len);
    pos += len;
  }
  memcpy(buf + pos, " -> ", 4);
  pos += 4;
  size_t flen = strlen(first_base);
  memcpy(buf + pos, first_base, flen);
  pos += flen;
  buf[pos] = '\0';
  return buf;
}

/* Resolve a use-declaration path relative to the importing file's directory.
   importer_path: canonical path of the file containing the `use` statement
   use_path:      the relative path string from the `use` declaration
   arena:         for allocating the result string
   Returns: canonical path on success, NULL on failure (file not found) */
const char* module__resolve_path(const char* importer_path,
                                        const char* use_path,
                                        arena_t* arena) {
  /* Find the directory of the importing file */
  const char* last_slash = strrchr(importer_path, '/');
  size_t dir_len = last_slash ? (size_t)(last_slash - importer_path) : 0;

  /* Build the joined path: dir/use_path */
  size_t use_len = strlen(use_path);
  size_t joined_len = dir_len + 1 + use_len;
  char joined[1024];
  if (joined_len >= sizeof(joined)) return NULL;

  if (dir_len > 0) {
    memcpy(joined, importer_path, dir_len);
    joined[dir_len] = '/';
    memcpy(joined + dir_len + 1, use_path, use_len + 1);
  } else {
    memcpy(joined, use_path, use_len + 1);
  }

  /* Canonicalize with realpath() — resolves .., ., and symlinks */
  char resolved[PATH_MAX];
  if (!realpath(joined, resolved)) return NULL; /* file not found */

  /* Copy into arena */
  size_t rlen = strlen(resolved);
  char* result = (char*)arena_alloc(arena, (uint32_t)(rlen + 1));
  memcpy(result, resolved, rlen + 1);
  return result;
}

/* --- Module privacy: underscore-prefix convention --- */

/* Returns true if a top-level name is private (underscore-prefixed).
   Private names are excluded from module export lists and cannot be
   imported by other modules. */
bool module__is_private(const char* name, uint32_t name_len) {
  return name_len > 0 && name[0] == '_';
}

/* Read a file into arena-allocated memory. Returns NULL on failure. */
char* module__read_file(const char* path, arena_t* arena) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 0) { fclose(f); return NULL; }
  char* buf = (char*)arena_alloc(arena, (uint32_t)(size + 1));
  size_t nread = fread(buf, 1, (size_t)size, f);
  buf[nread] = '\0';
  fclose(f);
  return buf;
}

/* --- Internal: Local variable tracking --- */

#define COMPILER_LOCALS_MAX 256
#define COMPILER_UPVALUES_MAX 256
#define COMPILER_TRY_PATCHES_MAX 128
/* COMPILER_MAX_PROC_PARAMS defined above with module structs */

/* --- TypeInfo: bundled type metadata for save/load of locals/globals/upvalues --- */

typedef struct {
  JaclType  type;
  uint32_t  struct_idx;      /* struct registry index (UINT32_MAX when N/A) */
  uint32_t  key_struct_idx;  /* key struct index for typed maps (UINT32_MAX = dyn) */
} TypeInfo;

/* Save/load between TypeInfo and any struct with .type/.struct_type_idx/.key_struct_idx */
#define TYPEINFO_SAVE(dest, ti) do { \
  (dest).type = (ti).type; \
  (dest).struct_type_idx = (ti).struct_idx; \
  (dest).key_struct_idx = (ti).key_struct_idx; \
} while(0)

#define TYPEINFO_LOAD(src) \
  ((TypeInfo){ (src).type, (src).struct_type_idx, (src).key_struct_idx })

typedef struct {
  JaclVal   name;         /* inline string name */
  int       depth;        /* scope depth when declared */
  int16_t   known_arity;  /* arity if bound to a proc, -1 = unknown */
  bool      is_mutable;   /* true if declared with mut */
  bool      is_param;     /* true if this is a function parameter */
  bool      suspends;     /* true if bound to a suspending proc */
  bool      captures_mutable; /* true if bound to a closure that captures mutable state */
  JaclType  type;         /* compile-time type (default TYPE_DYN) */
  uint32_t  struct_type_idx; /* struct registry index when type==TYPE_STRUCT */
  uint32_t  key_struct_idx;  /* key struct index for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
  JaclType  return_type;  /* proc return type (TYPE_DYN for non-procs) */
  uint32_t  return_struct_idx; /* struct registry index when return_type==TYPE_STRUCT */
  JaclType* param_types;  /* proc param types (NULL for non-procs, arena-allocated) */
  uint32_t  scope_mark;   /* hygiene: 0 = user code, >0 = macro-introduced */
  uint16_t  width;        /* stack slot count: 1 for scalars, N for inline structs */
  bool      is_inline;    /* true if struct is stored inline on stack (raw bytes, not heap pointer) */
  uint32_t  buf_len;      /* N for TYPE_BUF (in elements, not slots); 0 otherwise */
} Local;

/* --- Internal: Module binding tracking --- */

#define COMPILER_MODULE_BINDINGS_MAX 64

typedef struct {
  JaclVal name;       /* local variable name (interned) */
  int     local_slot; /* slot in the locals array */
  Module* module;     /* the module it binds to */
} ModuleBinding;

/* --- Internal: Global arity tracking --- */

#define COMPILER_GLOBAL_ARITIES_MAX 64

typedef struct {
  JaclVal   name;
  int16_t   known_arity;
  bool      is_mutable;   /* true if declared with mut */
  bool      suspends;     /* true if this is a suspending proc */
  bool      captures_mutable; /* true if bound to a closure that captures mutable state */
  bool      prelude_is_native_fn; /* true if prelude value is a native fn ref (emit direct opcode) */
  JaclType  type;         /* compile-time type (default TYPE_DYN) */
  uint32_t  struct_type_idx; /* struct registry index when type==TYPE_STRUCT */
  uint32_t  key_struct_idx;  /* key struct index for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
  JaclType  return_type;  /* proc return type (TYPE_DYN for non-procs) */
  uint32_t  return_struct_idx; /* struct registry index when return_type==TYPE_STRUCT */
  JaclType  param_types[COMPILER_MAX_PROC_PARAMS]; /* proc param types */
} GlobalArity;

typedef struct {
  uint8_t   index;    /* local slot (if is_local) or parent upvalue index */
  uint8_t   is_local; /* 1 = capture from enclosing locals, 0 = from parent upvalues */
  JaclVal   name;     /* for debug/lookup */
  bool      is_mutable; /* true if capturing a mut binding */
  bool      suspends;   /* true if capturing a suspending proc */
  bool      captures_mutable; /* true if capturing a closure that captures mutable state */
  JaclType  type;     /* compile-time type (default TYPE_DYN) */
  uint32_t  struct_type_idx; /* struct registry index when type==TYPE_STRUCT */
  uint32_t  key_struct_idx;  /* key struct index for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
  uint32_t  scope_mark; /* hygiene: mark of the captured binding */
  uint16_t  width;      /* JaclVal slot count: 1 for scalars, N for inline structs */
  bool      is_inline;  /* true if capturing an inline struct (raw bytes, not heap) */
  uint16_t  base_slot;  /* position in this closure's upvalue array */
} Upvalue;

/* --- Internal: AST walker shells ----------------------------------------
 *
 * Most AST analysis passes share the same recursion shape: at AST_BLOCK,
 * AST_INTERP_STRING, AST_BREAK, AST_RETURN, AST_SHELL_CMD they recurse into
 * their child nodes; at AST_COMMAND each pass does its own thing; and other
 * leaf-ish kinds (literals, var-refs, destructuring patterns) are inert
 * unless the pass cares about them specifically.
 *
 * These helpers centralize the boilerplate so each pass only writes what's
 * specific to it. ast__walk_children calls `recurse` on each child of the
 * shell-handled node kinds and returns true if it handled the node;
 * ast__any_child does the same with short-circuit boolean semantics.
 *
 * If a pass cares about AST_COMMAND or AST_VAR_REF (etc.), it handles them
 * before delegating to the helper.
 * ------------------------------------------------------------------------- */

static bool ast__walk_children(AstNode* node,
                               void (*recurse)(AstNode*, void*),
                               void* ctx) {
  if (!node) return true;
  switch (node->type) {
    case AST_BLOCK:
      for (uint32_t i = 0; i < node->data.block.count; i++)
        recurse(node->data.block.commands[i], ctx);
      return true;
    case AST_INTERP_STRING:
      for (uint32_t i = 0; i < node->data.interp_string.count; i++)
        recurse(node->data.interp_string.segments[i], ctx);
      return true;
    case AST_BREAK:
      if (node->data.break_stmt.value)
        recurse(node->data.break_stmt.value, ctx);
      return true;
    case AST_RETURN:
      if (node->data.return_stmt.value)
        recurse(node->data.return_stmt.value, ctx);
      return true;
    case AST_SHELL_CMD:
      recurse(node->data.shell_cmd.head, ctx);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++)
        recurse(node->data.shell_cmd.args[i], ctx);
      return true;
    default:
      return false;  /* caller handles AST_COMMAND, AST_VAR_REF, leaves */
  }
}

static bool ast__any_child(AstNode* node,
                           bool (*pred)(AstNode*, void*),
                           void* ctx) {
  if (!node) return false;
  switch (node->type) {
    case AST_BLOCK:
      for (uint32_t i = 0; i < node->data.block.count; i++)
        if (pred(node->data.block.commands[i], ctx)) return true;
      return false;
    case AST_INTERP_STRING:
      for (uint32_t i = 0; i < node->data.interp_string.count; i++)
        if (pred(node->data.interp_string.segments[i], ctx)) return true;
      return false;
    case AST_BREAK:
      return node->data.break_stmt.value &&
             pred(node->data.break_stmt.value, ctx);
    case AST_RETURN:
      return node->data.return_stmt.value &&
             pred(node->data.return_stmt.value, ctx);
    case AST_SHELL_CMD:
      if (pred(node->data.shell_cmd.head, ctx)) return true;
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++)
        if (pred(node->data.shell_cmd.args[i], ctx)) return true;
      return false;
    default:
      return false;
  }
}

/* --- Internal: Suspension analysis --- */

#define SUSPENSION_MAP_MAX 256
#define SUSPENSION_CALLEES_MAX 64

typedef struct {
  JaclVal name;
  bool    suspends;
  bool    is_generator; /* true if proc contains yield (calling returns stream) */
} SuspensionEntry;

typedef struct {
  SuspensionEntry entries[SUSPENSION_MAP_MAX];
  uint32_t count;
} SuspensionMap;

bool suspension_map_lookup(SuspensionMap* map, JaclVal name) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (map->entries[i].name == name) {
      return map->entries[i].suspends;
    }
  }
  return false;
}

bool suspension_map_is_generator(SuspensionMap* map, JaclVal name) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (map->entries[i].name == name) {
      return map->entries[i].is_generator;
    }
  }
  return false;
}

void suspension_map_set(SuspensionMap* map, JaclVal name,
                               bool suspends, bool is_generator) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (map->entries[i].name == name) {
      map->entries[i].suspends = suspends;
      map->entries[i].is_generator = is_generator;
      return;
    }
  }
  if (map->count < SUSPENSION_MAP_MAX) {
    map->entries[map->count].name = name;
    map->entries[map->count].suspends = suspends;
    map->entries[map->count].is_generator = is_generator;
    map->count++;
  }
}

/* Info collected per proc during suspension analysis */
typedef struct {
  JaclVal  name;
  bool     direct_suspends;   /* directly contains await/parallel/race */
  bool     has_yield;          /* directly contains yield */
  bool     has_indirect_call; /* calls through $var (unknown closure) */
  JaclVal  callees[SUSPENSION_CALLEES_MAX];
  uint32_t callee_count;
} ProcSuspendInfo;

#define MAX_PROC_INFOS 256

typedef struct {
  ProcSuspendInfo procs[MAX_PROC_INFOS];
  uint32_t count;
} ProcSuspendInfoList;

/* Forward declaration — defined later in this file. */
static JaclVal compiler__name_val(ThreadHeap* heap, JaclInternTable* table,
                                  const char* name, uint32_t len);

/* Walk an AST subtree within a proc body to find suspension points and callees.
   Does NOT recurse into nested proc definitions (they have their own scope). */
typedef struct {
  ProcSuspendInfo* info;
  ThreadHeap*      heap;
  JaclInternTable* intern_table;
} WalkBodyCtx;

static void analyze__walk_body__visit(AstNode* node, void* vctx) {
  if (!node) return;
  WalkBodyCtx* ctx = (WalkBodyCtx*)vctx;

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    HeadId hid = (HeadId)node->data.command.head_id;
    if (head->type == AST_LIT_STRING) {
      /* Direct suspension points */
      if (hid == HEAD_AWAIT || hid == HEAD_PARALLEL || hid == HEAD_RACE ||
          hid == HEAD_SLEEP) {
        ctx->info->direct_suspends = true;
      } else if (hid == HEAD_YIELD) {
        ctx->info->direct_suspends = true;
        ctx->info->has_yield = true;
      } else if (hid == HEAD_PROC || hid == HEAD_SPAWN) {
        /* Skip recursion INTO nested proc/spawn bodies (separate scopes) */
        return;
      } else if (ctx->info->callee_count < SUSPENSION_CALLEES_MAX) {
        /* Record callee name for transitive propagation */
        ctx->info->callees[ctx->info->callee_count++] =
            compiler__name_val(ctx->heap, ctx->intern_table,
                               head->data.lit_string.value,
                               head->data.lit_string.length);
      }
    } else if (head->type == AST_VAR_REF) {
      /* Indirect call through variable ($f ...) */
      ctx->info->has_indirect_call = true;
    }
    /* Recurse into arguments (and head, for $var heads) */
    analyze__walk_body__visit(head, ctx);
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      analyze__walk_body__visit(node->data.command.args[i], ctx);
    }
    return;
  }

  if (node->type == AST_SHELL_CMD &&
      ctx->info->callee_count < SUSPENSION_CALLEES_MAX) {
    /* Shell commands call exec - record as a callee */
    ctx->info->callees[ctx->info->callee_count++] =
        compiler__name_val(ctx->heap, ctx->intern_table, "exec", 4);
  }
  ast__walk_children(node, analyze__walk_body__visit, ctx);
}

void analyze__walk_body(AstNode* node, ProcSuspendInfo* info,
                        ThreadHeap* heap, JaclInternTable* intern_table) {
  WalkBodyCtx ctx = { info, heap, intern_table };
  analyze__walk_body__visit(node, &ctx);
}

/* Recursively collect proc definitions from AST, analyzing each body */
typedef struct {
  ProcSuspendInfoList* list;
  ThreadHeap*          heap;
  JaclInternTable*     intern_table;
} CollectProcsCtx;

static void analyze__collect_procs__visit(AstNode* node, void* vctx) {
  if (!node) return;
  CollectProcsCtx* ctx = (CollectProcsCtx*)vctx;

  if (node->type == AST_COMMAND) {
    uint32_t argc = node->data.command.arg_count;
    AstNode** args = node->data.command.args;
    bool handled = false;

    if (node->data.command.head_id == HEAD_PROC) {
      uint32_t name_idx, body_idx;
      bool ok = false;
      if (argc == 4)      { name_idx = 1; body_idx = 3; ok = true; }
      else if (argc == 3) { name_idx = 0; body_idx = 2; ok = true; }

      if (ok && args[name_idx]->type == AST_LIT_STRING &&
          args[name_idx]->data.lit_string.length <= 128) {
        uint32_t name_len = args[name_idx]->data.lit_string.length;
        JaclVal proc_name = compiler__name_val(
            ctx->heap, ctx->intern_table,
            args[name_idx]->data.lit_string.value, name_len);

        if (ctx->list->count < MAX_PROC_INFOS) {
          ProcSuspendInfo* info = &ctx->list->procs[ctx->list->count++];
          info->name = proc_name;
          info->direct_suspends = false;
          info->has_yield = false;
          info->has_indirect_call = false;
          info->callee_count = 0;
          if (args[body_idx]->type == AST_BLOCK) {
            analyze__walk_body(args[body_idx], info, ctx->heap, ctx->intern_table);
          }
        }
        if (args[body_idx]->type == AST_BLOCK) {
          analyze__collect_procs__visit(args[body_idx], ctx);
        }
        handled = true;
      }
    }
    if (!handled) {
      for (uint32_t i = 0; i < argc; i++) {
        analyze__collect_procs__visit(args[i], ctx);
      }
    }
    return;
  }
  ast__walk_children(node, analyze__collect_procs__visit, ctx);
}

void analyze__collect_procs(AstNode* node, ProcSuspendInfoList* list,
                             ThreadHeap* heap, JaclInternTable* intern_table) {
  CollectProcsCtx ctx = { list, heap, intern_table };
  analyze__collect_procs__visit(node, &ctx);
}

/* Pre-compilation suspension analysis: walk AST to determine which procs suspend.
   Returns a SuspensionMap that the compiler consults during code generation. */
SuspensionMap compiler__analyze_suspension(AstNode** nodes, uint32_t count,
                                           ThreadHeap* heap,
                                           JaclInternTable* intern_table) {
  SuspensionMap map;
  ProcSuspendInfoList proc_list;
  memset(&map, 0, sizeof(map));
  memset(&proc_list, 0, sizeof(proc_list));

  /* Step 1: Collect all proc definitions and analyze bodies */
  for (uint32_t i = 0; i < count; i++) {
    analyze__collect_procs(nodes[i], &proc_list, heap, intern_table);
  }

  /* Step 2: Initialize suspension map from direct suspension */
  for (uint32_t i = 0; i < proc_list.count; i++) {
    suspension_map_set(&map, proc_list.procs[i].name,
                       proc_list.procs[i].direct_suspends,
                       proc_list.procs[i].has_yield);
  }

  /* Step 3: Fixpoint propagation.
     Rule 1: proc calling a suspending proc becomes suspending.
     Rule 2: if any suspending proc exists in program, a proc with
             indirect calls ($var) is conservatively suspending. */
  bool changed = true;
  while (changed) {
    changed = false;

    bool any_suspending = false;
    for (uint32_t i = 0; i < map.count; i++) {
      if (map.entries[i].suspends) { any_suspending = true; break; }
    }

    for (uint32_t i = 0; i < proc_list.count; i++) {
      if (suspension_map_lookup(&map, proc_list.procs[i].name)) continue;

      /* Rule 1: direct call to suspending proc (skip generators —
         calling a generator just creates a stream, doesn't suspend caller) */
      for (uint32_t j = 0; j < proc_list.procs[i].callee_count; j++) {
        if (suspension_map_lookup(&map, proc_list.procs[i].callees[j]) &&
            !suspension_map_is_generator(&map, proc_list.procs[i].callees[j])) {
          suspension_map_set(&map, proc_list.procs[i].name, true, false);
          changed = true;
          break;
        }
      }

      /* Rule 2: indirect call when suspending procs exist */
      if (any_suspending &&
          !suspension_map_lookup(&map, proc_list.procs[i].name) &&
          proc_list.procs[i].has_indirect_call) {
        suspension_map_set(&map, proc_list.procs[i].name, true, false);
        changed = true;
      }
    }
  }

  return map;
}

/* --- State machine suspension point analysis --- */

#define SM_MAX_SUSPENSION_POINTS 256
#define SM_MAX_STATE_FIELDS     256

typedef enum {
  SUSPEND_YIELD,
  SUSPEND_AWAIT,
  SUSPEND_PARALLEL,
  SUSPEND_RACE,
  SUSPEND_CALL       /* call to a known suspending proc */
} SuspensionPointType;

typedef struct {
  uint32_t            id;        /* sequential index: 0, 1, 2, ... */
  SuspensionPointType type;      /* yield, await, parallel, or race */
  AstNode*            node;      /* AST node of the suspension point */
  uint32_t            line;      /* source line */
  uint32_t            column;    /* source column */
  /* Number of operand-stack values live at this suspension that were pushed
     by enclosing expression evaluators. These must be spilled into reserved
     state-machine slots before the suspension and restored after the resume
     label, because runtime__setup_call resets vm->stack_top on every SM
     re-entry. Computed statically by sm__walk_suspensions. */
  uint16_t            pre_stack_depth;
} SuspensionPoint;

/* --- State machine local classification (US-002) --- */

typedef struct {
  JaclVal  name;         /* variable name (for debug and lookup) */
  uint32_t field_index;  /* base slot index in state machine fields[] */
  bool     is_mutable;   /* true if declared with mut */
  bool     is_param;     /* true if this is a function parameter */
  uint16_t width;        /* number of JaclVal slots occupied (default 1, N for inline structs) */
  uint32_t struct_type_idx; /* struct registry index when width > 1, else 0 */
} StateField;

typedef struct {
  uint32_t         field_count;
  uint32_t         total_slots;   /* sum of all field widths — actual fields[] size needed */
  StateField       fields[SM_MAX_STATE_FIELDS];
  ThreadHeap*      heap;          /* for interning names > 7 bytes */
  JaclInternTable* intern_table;  /* for interning names > 7 bytes */
} StateLayout;

typedef struct {
  uint32_t        suspension_count;
  SuspensionPoint suspension_points[SM_MAX_SUSPENSION_POINTS];
  StateLayout     state_layout;
  uint32_t        ctx_field_idx;  /* state field index for __ctx (UINT32_MAX if absent) */
  /* Operand-stack spill area (see SuspensionPoint::pre_stack_depth).
     `spill_base_slot` is the first slot of the spill region; the SM has
     `max_pre_stack_depth` spill slots followed by one scratch slot (used
     to reshuffle the await/resume value above the restored stack). All
     three are zero when no suspension needs spilling. */
  uint16_t        max_pre_stack_depth;
  uint16_t        spill_base_slot;
  uint16_t        scratch_slot;
} SuspensionAnalysis;

/* Create a JaclVal from a name string, routing by length:
   <= 7 bytes: inline string.  8-128 bytes: interned (pointer-stable).
   Caller must ensure len <= 128 (enforced at validation sites). */
static JaclVal compiler__name_val(ThreadHeap* heap, JaclInternTable* table,
                                  const char* name, uint32_t len) {
  if (len <= 7) return jacl_inline_string(name, len);
  return jacl_intern(heap, table, name, len);
}

/* Walk an AST subtree to find suspension points for state machine compilation.
   Does NOT recurse into nested proc/spawn definitions (separate closure scopes).
   Assigns sequential IDs to each discovered suspension point.
   When map is non-NULL, also treats calls to known suspending procs as
   suspension points (SUSPEND_CALL). */
typedef struct {
  SuspensionAnalysis* analysis;
  SuspensionMap*      map;
  ThreadHeap*         heap;
  JaclInternTable*    intern_table;
  /* Operand-stack depth contributed by enclosing expressions at the current
     AST node. Each argument of an AST_COMMAND is evaluated with one extra
     value already on the stack per preceding sibling (so arg i sees depth
     `depth + i`). This is the spill-count we record at each suspension. */
  uint16_t            depth;
} WalkSuspensionsCtx;

static void sm__record_suspension(SuspensionAnalysis* a, AstNode* node,
                                  SuspensionPointType type,
                                  uint16_t pre_stack_depth) {
  if (a->suspension_count >= SM_MAX_SUSPENSION_POINTS) return;
  SuspensionPoint* sp = &a->suspension_points[a->suspension_count];
  sp->id              = a->suspension_count;
  sp->type            = type;
  sp->node            = node;
  sp->line            = node->start.line;
  sp->column          = node->start.column;
  sp->pre_stack_depth = pre_stack_depth;
  a->suspension_count++;
}

static void sm__walk_suspensions__visit(AstNode* node, void* vctx);

/* Adapter for ast__walk_children — it requires a (node, void*) visitor and
   doesn't know about depth, so non-AST_COMMAND children inherit the current
   depth unchanged (they don't introduce sibling-induced stack contributions
   the way command args do). */
static void sm__walk_suspensions__visit_child(AstNode* node, void* vctx) {
  sm__walk_suspensions__visit(node, vctx);
}

/* True when the head emits bytecode that pushes each compiled arg onto the
   operand stack in turn, then pops them all and pushes one result. For these
   heads, arg[i] is compiled with `i` sibling values already on the stack —
   so a suspension inside arg[i] has to spill those siblings.

   Special forms (the `false` cases) emit ad-hoc bytecode shapes: `def`/`mut`/
   `set` treat arg[0] as a name (not compiled as a value); control-flow heads
   pop the condition before running the body block, so suspensions inside the
   body see no operand-stack contribution from the surrounding form. For all
   of these, args compile at the *parent's* depth, not depth+i.

   HEAD_PARALLEL/HEAD_RACE/HEAD_SPAWN are listed as special forms because
   their args are closures compiled in fresh scopes (the walk doesn't recurse
   into them anyway), so the depth accounting is moot.

   Default-true: an unrecognized head is assumed to be function-call shape.
   A miscategorization here means a missed spill (silently wrong answer) for
   that head, the same class of bug we're fixing. Audit when adding new
   non-call-shape special forms. */
static bool sm__head_uses_operand_stack_for_args(HeadId hid) {
  switch (hid) {
    /* Bindings & assignments — args[0] is a name pattern (used at compile
       time, never compiled as a value). HEAD_EQUALS/HEAD_COLON/HEAD_COLON_COLON
       are syntactic sugar that compiler__rewrite_binding_op rewrites to
       HEAD_DEF/HEAD_MUT/HEAD_SET, but the walk runs on the original node so
       all six must be listed here. */
    case HEAD_DEF:
    case HEAD_MUT:
    case HEAD_SET:
    case HEAD_EQUALS:
    case HEAD_COLON:
    case HEAD_COLON_COLON:
    /* Declarations — separate scopes or compile-time-only. */
    case HEAD_PROC:
    case HEAD_DEFSTRUCT:
    case HEAD_DEFMACRO:
    case HEAD_EXTERN:
    /* Control flow — cond is popped before the branch body runs; branch
       bodies see depth = parent, not parent + i. */
    case HEAD_IF:
    case HEAD_WHILE:
    case HEAD_FOR:
    case HEAD_BREAK:
    case HEAD_CONTINUE:
    case HEAD_RETURN:
    case HEAD_TRY:
    case HEAD_WITH_CTX:
    case HEAD_MATCH:
    /* Short-circuit boolean — JUMP_IF_FALSE pops the LHS before compiling
       the RHS, so RHS suspensions see depth = parent, not parent + 1. */
    case HEAD_AMP_AMP:
    case HEAD_PIPE_PIPE:
    /* Type-coerce — args[0] is a type keyword (i32/f64/...), embedded as
       compile-time constant rather than pushed; only args[1] is a value. */
    case HEAD_TO:
    /* Closure-arg suspensions — bodies are compiled as separate closures
       with their own SuspensionAnalysis. The walk doesn't recurse here
       (see sm__walk_suspensions__visit), so this entry only matters for
       any ambient depth contribution from the head itself. */
    case HEAD_SPAWN:
    case HEAD_PARALLEL:
    case HEAD_RACE:
    /* Macro / syntax-quote forms — args are syntax objects, not compiled
       as runtime values in the surrounding SM context. */
    case HEAD_QUOTE:
    case HEAD_SYNTAX_QUOTE:
    /* Compile-time static type assertion — args are typer-only, never
       compiled to bytecode, so they cannot contribute to the runtime
       operand stack or generate suspension points. */
    case HEAD_ASSERT_TYPE:
      return false;
    default:
      return true;
  }
}

static void sm__walk_suspensions__visit(AstNode* node, void* vctx) {
  if (!node) return;
  WalkSuspensionsCtx* ctx = (WalkSuspensionsCtx*)vctx;

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING) {
      HeadId hid = (HeadId)node->data.command.head_id;
      SuspensionPointType sp_type;
      bool is_sp = true;
      switch (hid) {
        case HEAD_YIELD:    sp_type = SUSPEND_YIELD;    break;
        case HEAD_AWAIT:    sp_type = SUSPEND_AWAIT;    break;
        case HEAD_PARALLEL: sp_type = SUSPEND_PARALLEL; break;
        case HEAD_RACE:     sp_type = SUSPEND_RACE;     break;
        case HEAD_SLEEP:    sp_type = SUSPEND_AWAIT;    break;
        default:            is_sp = false; sp_type = SUSPEND_YIELD; break;
      }

      if (is_sp) {
        sm__record_suspension(ctx->analysis, node, sp_type, ctx->depth);
        /* HEAD_PARALLEL and HEAD_RACE: args are body blocks compiled into
           separate closures with their own SuspensionAnalysis (see
           compiler__compile_parallel_body). Recording their inner
           suspensions on this analysis would inflate suspension_count and
           desynchronize sm_suspension_idx from the bytecode the outer SM
           actually emits. Don't recurse. */
        if (hid == HEAD_PARALLEL || hid == HEAD_RACE) return;
      } else if (hid == HEAD_PROC || hid == HEAD_SPAWN) {
        /* Separate closure scopes — don't recurse */
        return;
      } else if (hid == HEAD_ASSERT_TYPE) {
        /* Compile-time-only — args are not emitted as bytecode, so any
           suspension points inside them never execute. Don't recurse. */
        return;
      } else if (ctx->map) {
        /* Call to a known suspending proc is a suspension point */
        JaclVal name_val = compiler__name_val(
            ctx->heap, ctx->intern_table,
            head->data.lit_string.value, head->data.lit_string.length);
        if (suspension_map_lookup(ctx->map, name_val) &&
            !suspension_map_is_generator(ctx->map, name_val)) {
          sm__record_suspension(ctx->analysis, node, SUSPEND_CALL, ctx->depth);
        }
      }
    }
    /* Recurse into args (suspension points may nest). For function-call-
       shape heads, each preceding sibling leaves one value on the stack when
       its compilation finishes, so arg i sees depth + i. For special forms,
       args don't accumulate on the operand stack (see
       sm__head_uses_operand_stack_for_args), so they all compile at the
       parent's depth. Save/restore around each call so siblings don't see
       each other's contributions on the way out. */
    uint16_t saved = ctx->depth;
    bool fn_shape = (head->type == AST_LIT_STRING) &&
        sm__head_uses_operand_stack_for_args((HeadId)node->data.command.head_id);
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      ctx->depth = fn_shape ? (uint16_t)(saved + i) : saved;
      sm__walk_suspensions__visit(node->data.command.args[i], ctx);
    }
    ctx->depth = saved;
    return;
  }
  ast__walk_children(node, sm__walk_suspensions__visit_child, ctx);
}

void sm__walk_suspensions(AstNode* node, SuspensionAnalysis* analysis,
                                  SuspensionMap* map,
                                  ThreadHeap* heap, JaclInternTable* intern_table) {
  WalkSuspensionsCtx ctx = { analysis, map, heap, intern_table, 0 };
  sm__walk_suspensions__visit(node, &ctx);
}

/* --- State layout helpers --- */

/* Add a field to the state layout, skipping empty names and duplicates. */
void sm__add_state_field(StateLayout* layout, JaclVal name,
                                bool is_mutable, bool is_param,
                                uint16_t width, uint32_t struct_type_idx) {
  if (layout->field_count >= SM_MAX_STATE_FIELDS) return;
  /* Skip empty/wildcard names (compiler uses empty string for _ wildcards) */
  if (name == jacl_inline_string("", 0)) return;
  /* Check for duplicates (same name in nested scopes) */
  for (uint32_t i = 0; i < layout->field_count; i++) {
    if (layout->fields[i].name == name) return;
  }
  StateField* f = &layout->fields[layout->field_count];
  f->name            = name;
  f->field_index     = layout->total_slots;  /* base slot index */
  f->is_mutable      = is_mutable;
  f->is_param        = is_param;
  f->width           = width;
  f->struct_type_idx = struct_type_idx;
  layout->total_slots += width;
  layout->field_count++;
}

/* Look up a variable name in the StateLayout.
   Returns the field index (0..field_count-1) or -1 if not found. */
const StateField* sm__get_field(const StateLayout* layout, JaclVal name) {
  for (uint32_t i = 0; i < layout->field_count; i++) {
    if (layout->fields[i].name == name) return &layout->fields[i];
  }
  return NULL;
}

int sm__find_field(const StateLayout* layout, JaclVal name) {
  const StateField* f = sm__get_field(layout, name);
  return f ? (int)f->field_index : -1;
}

bool sm__is_field_mutable(const StateLayout* layout, JaclVal name) {
  const StateField* f = sm__get_field(layout, name);
  return f ? f->is_mutable : false;
}

/* Collect names from an AST_DESTRUCTURE_VEC node into the state layout. */
void sm__collect_destructure_vec_names(AstNode* dv, StateLayout* layout,
                                              bool is_mutable) {
  for (uint32_t i = 0; i < dv->data.destructure_vec.count; i++) {
    const char* n = dv->data.destructure_vec.names[i];
    uint32_t nl = dv->data.destructure_vec.name_lens[i];
    if (nl == 1 && n[0] == '_') continue;  /* skip wildcard */
    sm__add_state_field(layout, compiler__name_val(layout->heap, layout->intern_table, n, nl), is_mutable, false, 1, 0);
  }
  if (dv->data.destructure_vec.rest_name) {
    sm__add_state_field(layout,
        compiler__name_val(layout->heap, layout->intern_table,
                           dv->data.destructure_vec.rest_name,
                           dv->data.destructure_vec.rest_name_len),
        is_mutable, false, 1, 0);
  }
}

/* Collect names from an AST_DESTRUCTURE_NAMED node into the state layout. */
void sm__collect_destructure_named_names(AstNode* dn, StateLayout* layout,
                                                bool is_mutable) {
  for (uint32_t i = 0; i < dn->data.destructure_named.count; i++) {
    const char* n = dn->data.destructure_named.names[i];
    uint32_t nl = dn->data.destructure_named.name_lens[i];
    sm__add_state_field(layout, compiler__name_val(layout->heap, layout->intern_table, n, nl), is_mutable, false, 1, 0);
  }
  if (dn->data.destructure_named.rest_name) {
    sm__add_state_field(layout,
        compiler__name_val(layout->heap, layout->intern_table,
                           dn->data.destructure_named.rest_name,
                           dn->data.destructure_named.rest_name_len),
        is_mutable, false, 1, 0);
  }
}

/* Collect names from a bracket destructure in AST_COMMAND form [a b c]. */
void sm__collect_command_destructure_names(AstNode* pat,
                                                  StateLayout* layout,
                                                  bool is_mutable) {
  /* Head element */
  if (pat->data.command.head->type == AST_LIT_STRING) {
    const char* s = pat->data.command.head->data.lit_string.value;
    uint32_t sl = pat->data.command.head->data.lit_string.length;
    if (!(sl == 2 && s[0] == '.' && s[1] == '.') &&
        !(sl == 1 && s[0] == '_')) {
      sm__add_state_field(layout, compiler__name_val(layout->heap, layout->intern_table, s, sl), is_mutable, false, 1, 0);
    }
  } else if (pat->data.command.head->type == AST_SPREAD) {
    AstNode* inner = pat->data.command.head->data.spread.expr;
    if (inner && inner->type == AST_LIT_STRING) {
      sm__add_state_field(layout,
          compiler__name_val(layout->heap, layout->intern_table,
                             inner->data.lit_string.value,
                             inner->data.lit_string.length),
          is_mutable, false, 1, 0);
    }
  }
  /* Arg elements */
  for (uint32_t i = 0; i < pat->data.command.arg_count; i++) {
    AstNode* elem = pat->data.command.args[i];
    if (elem->type == AST_LIT_STRING) {
      const char* s = elem->data.lit_string.value;
      uint32_t sl = elem->data.lit_string.length;
      if (sl == 2 && s[0] == '.' && s[1] == '.') continue;
      if (sl == 1 && s[0] == '_') continue;
      sm__add_state_field(layout, compiler__name_val(layout->heap, layout->intern_table, s, sl), is_mutable, false, 1, 0);
    } else if (elem->type == AST_SPREAD) {
      AstNode* inner = elem->data.spread.expr;
      if (inner && inner->type == AST_LIT_STRING) {
        sm__add_state_field(layout,
            compiler__name_val(layout->heap, layout->intern_table,
                               inner->data.lit_string.value,
                               inner->data.lit_string.length),
            is_mutable, false, 1, 0);
      }
    }
  }
}

/* Collect names from a curly-brace destructure in AST_BLOCK form {a, b, c}. */
void sm__collect_block_destructure_names(AstNode* blk,
                                                StateLayout* layout,
                                                bool is_mutable) {
  for (uint32_t i = 0; i < blk->data.block.count; i++) {
    AstNode* cmd = blk->data.block.commands[i];
    if (cmd->type == AST_LIT_STRING) continue;  /* bare ".." spread-all */
    if (cmd->type != AST_COMMAND) continue;
    const char* hstr = NULL;
    uint32_t hlen = 0;
    if (cmd->data.command.head->type == AST_LIT_STRING) {
      hstr = cmd->data.command.head->data.lit_string.value;
      hlen = cmd->data.command.head->data.lit_string.length;
    } else {
      continue;
    }
    /* ".." rest pattern */
    if (hlen == 2 && hstr[0] == '.' && hstr[1] == '.') {
      if (cmd->data.command.arg_count == 1 &&
          cmd->data.command.args[0]->type == AST_LIT_STRING) {
        sm__add_state_field(layout,
            compiler__name_val(layout->heap, layout->intern_table,
                               cmd->data.command.args[0]->data.lit_string.value,
                               cmd->data.command.args[0]->data.lit_string.length),
            is_mutable, false, 1, 0);
      }
      continue;
    }
    /* typed field: head=type, arg=name */
    if (cmd->data.command.arg_count == 1 &&
        cmd->data.command.args[0]->type == AST_LIT_STRING) {
      sm__add_state_field(layout,
          compiler__name_val(layout->heap, layout->intern_table,
                             cmd->data.command.args[0]->data.lit_string.value,
                             cmd->data.command.args[0]->data.lit_string.length),
          is_mutable, false, 1, 0);
    } else if (cmd->data.command.arg_count == 0) {
      /* simple name: head only */
      sm__add_state_field(layout, compiler__name_val(layout->heap, layout->intern_table, hstr, hlen),
                          is_mutable, false, 1, 0);
    }
  }
}

/* Walk AST to collect all local variable declarations for state layout.
   Conservative strategy: ALL locals are included.
   Does NOT recurse into nested proc/spawn body/params (separate scopes). */
typedef struct {
  StateLayout*        layout;
  StructTypeRegistry* reg;
} WalkLocalsCtx;

static void sm__walk_locals__visit(AstNode* node, void* vctx) {
  if (!node) return;
  WalkLocalsCtx* ctx = (WalkLocalsCtx*)vctx;
  StateLayout* layout = ctx->layout;
  StructTypeRegistry* reg = ctx->reg;

  if (node->type == AST_COMMAND) {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        HeadId hid = (HeadId)node->data.command.head_id;
        uint32_t argc = node->data.command.arg_count;
        AstNode** args = node->data.command.args;

        /* def / mut / = / : — local bindings (= is sugar for def, : for mut) */
        if (hid == HEAD_DEF || hid == HEAD_MUT ||
            hid == HEAD_EQUALS || hid == HEAD_COLON) {
          bool is_mut = (hid == HEAD_MUT || hid == HEAD_COLON);

          if (argc >= 2 && args[0]->type == AST_DESTRUCTURE_VEC) {
            sm__collect_destructure_vec_names(args[0], layout, is_mut);
          } else if (argc >= 2 && args[0]->type == AST_DESTRUCTURE_NAMED) {
            sm__collect_destructure_named_names(args[0], layout, is_mut);
          } else if (argc == 2 && args[0]->type == AST_COMMAND) {
            sm__collect_command_destructure_names(args[0], layout, is_mut);
          } else if (argc == 2 && args[0]->type == AST_BLOCK) {
            sm__collect_block_destructure_names(args[0], layout, is_mut);
          } else if (argc >= 2 && args[0]->type == AST_LIT_STRING) {
            /* Simple: [def name value] or typed: [def type name value] */
            uint32_t name_idx = 0;
            uint32_t value_idx = 1;
            uint16_t field_width = 1;
            uint32_t field_struct_idx = 0;
            if (argc == 3) {
              name_idx = 1;
              value_idx = 2;
              /* Check if the type is a struct — compute width from registry */
              const char* type_name = args[0]->data.lit_string.value;
              uint32_t type_len = args[0]->data.lit_string.length;
              if (reg) {
                uint32_t sidx = struct_registry__find(reg, type_name, type_len);
                if (sidx != UINT32_MAX) {
                  field_width = (uint16_t)struct__slot_width(reg, sidx);
                  field_struct_idx = sidx;
                }
              }
            }
            /* If width is still 1 (no explicit type) but the RHS is a
               struct constructor [StructName ...], infer struct width
               from the constructor name. This keeps untyped `def p [Point
               1 2]` consistent with typed `def Point p [Point 1 2]` for
               state field sizing. */
            if (field_width == 1 && reg && value_idx < argc &&
                args[value_idx]->type == AST_COMMAND &&
                args[value_idx]->data.command.head->type == AST_LIT_STRING) {
              const char* hd = args[value_idx]->data.command.head->data.lit_string.value;
              uint32_t hl = args[value_idx]->data.command.head->data.lit_string.length;
              uint32_t sidx = struct_registry__find(reg, hd, hl);
              if (sidx != UINT32_MAX) {
                field_width = (uint16_t)struct__slot_width(reg, sidx);
                field_struct_idx = sidx;
              }
            }
            if (args[name_idx]->type == AST_LIT_STRING) {
              sm__add_state_field(layout,
                  compiler__name_val(layout->heap, layout->intern_table,
                                     args[name_idx]->data.lit_string.value,
                                     args[name_idx]->data.lit_string.length),
                  is_mut, false, field_width, field_struct_idx);
            }
          }
          /* Recurse into value expressions (may contain nested blocks) */
          for (uint32_t i = 0; i < argc; i++) {
            if (i == 0 && (args[0]->type == AST_DESTRUCTURE_VEC ||
                           args[0]->type == AST_DESTRUCTURE_NAMED))
              continue;
            sm__walk_locals__visit(args[i], ctx);
          }
          return;
        }

        /* for — creates loop bindings */
        if (hid == HEAD_FOR) {
          /* C-style for: [for {init; cond; step} { body }] — init handled by recursion */
          if (argc == 3 && args[1]->type == AST_LIT_STRING &&
              args[2]->type == AST_BLOCK) {
            /* [for coll name { body }] */
            sm__add_state_field(layout,
                compiler__name_val(layout->heap, layout->intern_table,
                                   args[1]->data.lit_string.value,
                                   args[1]->data.lit_string.length),
                false, false, 1, 0);
          } else if (argc == 2 && args[1]->type == AST_BLOCK &&
                     !(args[0]->type == AST_BLOCK)) {
            /* [for coll { body }] — implicit "it" */
            sm__add_state_field(layout, jacl_inline_string("it", 2),  /* "it" is always <= 7 */
                                false, false, 1, 0);
          }
          /* Recurse into all sub-expressions */
          for (uint32_t i = 0; i < argc; i++) {
            sm__walk_locals__visit(args[i], ctx);
          }
          return;
        }

        /* try — catch binding is scope-local (handler cannot suspend),
           so do NOT add it to the state layout.  Only recurse into
           the try-body and handler body for nested bindings. */
        if (hid == HEAD_TRY) {
          for (uint32_t i = 0; i < argc; i++) {
            if (i == 1 && argc == 3 && args[1]->type == AST_LIT_STRING)
              continue;  /* skip catch binding name */
            sm__walk_locals__visit(args[i], ctx);
          }
          return;
        }

        /* proc — named proc creates a binding; do NOT recurse into body */
        if (hid == HEAD_PROC) {
          uint32_t name_idx;
          if (argc == 3) name_idx = 0;
          else if (argc == 4) name_idx = 1;
          else return;
          if (args[name_idx]->type == AST_LIT_STRING) {
            const char* pn = args[name_idx]->data.lit_string.value;
            uint32_t pnl = args[name_idx]->data.lit_string.length;
            if (pnl > 0) {
              sm__add_state_field(layout, compiler__name_val(layout->heap, layout->intern_table, pn, pnl),
                                  false, false, 1, 0);
            }
          }
          return;
        }

        /* spawn — separate scope, do not recurse */
        if (hid == HEAD_SPAWN) return;
      }

      /* Recurse into arguments for all other commands */
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        sm__walk_locals__visit(node->data.command.args[i], ctx);
      }
      return;
  }
  ast__walk_children(node, sm__walk_locals__visit, ctx);
}

void sm__walk_locals(AstNode* node, StateLayout* layout,
                            StructTypeRegistry* reg) {
  WalkLocalsCtx ctx = { layout, reg };
  sm__walk_locals__visit(node, &ctx);
}

/* --- Liveness analysis for state object field optimization (US-022) ---
 *
 * After building the full (conservative) StateLayout, this pass determines
 * which locals actually need to live in the state object.  A local crosses
 * a suspension boundary if its value from before a suspension is needed
 * after the suspension (on any execution path).
 *
 * Approach (O(n) single pass):
 *   1. Walk AST in pre-order, incrementing a "segment" counter at each
 *      suspension point (yield/await/parallel/race).
 *   2. For each variable definition (def/mut/set/for-bind), record the
 *      segment as a "write".
 *   3. For each variable reference (AST_VAR_REF), record the segment as
 *      a "read".
 *   4. For while/for loops containing suspension points, expand all
 *      variable ranges to cover the full loop span (handles back-edges).
 *   5. A variable crosses suspension if first_write < last_read.
 *   6. Parameters always cross (they are set at entry = segment 0).
 *   7. Filter the StateLayout, keeping only crossing locals + params.
 */

typedef struct {
  int32_t first_write;  /* earliest segment where defined/written (-1 = unseen) */
  int32_t last_read;    /* latest segment where read/referenced (-1 = unseen) */
} FieldLiveness;

/* Update liveness for a variable WRITE (def/mut/set/for-bind). */
void sm__liveness_mark_write(FieldLiveness* liveness,
                                     const StateLayout* layout,
                                     JaclVal name, int32_t segment) {
  int idx = sm__find_field(layout, name);
  if (idx >= 0) {
    if (liveness[idx].first_write < 0 || segment < liveness[idx].first_write)
      liveness[idx].first_write = segment;
  }
}

/* Update liveness for a variable READ (var_ref). */
void sm__liveness_mark_read(FieldLiveness* liveness,
                                    const StateLayout* layout,
                                    JaclVal name, int32_t segment) {
  int idx = sm__find_field(layout, name);
  if (idx >= 0) {
    if (liveness[idx].last_read < 0 || segment > liveness[idx].last_read)
      liveness[idx].last_read = segment;
  }
}

/* Helper: extract JaclVal name from an AST_LIT_STRING node. */
JaclVal sm__lit_string_name(const StateLayout* layout, AstNode* node) {
  return compiler__name_val(layout->heap, layout->intern_table,
                            node->data.lit_string.value,
                            node->data.lit_string.length);
}

/* Forward declaration. */
void sm__liveness_walk(AstNode* node, const StateLayout* layout,
                               FieldLiveness* liveness, int32_t* segment);

/* Walk a def/mut binding pattern to mark WRITE on all bound names. */
void sm__liveness_mark_binding_names(AstNode* pattern,
                                             const StateLayout* layout,
                                             FieldLiveness* liveness,
                                             int32_t segment) {
  if (!pattern) return;
  switch (pattern->type) {
    case AST_LIT_STRING:
      sm__liveness_mark_write(liveness, layout,
          sm__lit_string_name(layout, pattern), segment);
      break;
    case AST_DESTRUCTURE_VEC:
      for (uint32_t i = 0; i < pattern->data.destructure_vec.count; i++) {
        const char* n = pattern->data.destructure_vec.names[i];
        uint32_t nl = pattern->data.destructure_vec.name_lens[i];
        if (nl == 1 && n[0] == '_') continue;
        sm__liveness_mark_write(liveness, layout,
            compiler__name_val(layout->heap, layout->intern_table, n, nl), segment);
      }
      if (pattern->data.destructure_vec.rest_name) {
        sm__liveness_mark_write(liveness, layout,
            compiler__name_val(layout->heap, layout->intern_table,
                               pattern->data.destructure_vec.rest_name,
                               pattern->data.destructure_vec.rest_name_len),
            segment);
      }
      break;
    case AST_DESTRUCTURE_NAMED:
      for (uint32_t i = 0; i < pattern->data.destructure_named.count; i++) {
        const char* n = pattern->data.destructure_named.names[i];
        uint32_t nl = pattern->data.destructure_named.name_lens[i];
        sm__liveness_mark_write(liveness, layout,
            compiler__name_val(layout->heap, layout->intern_table, n, nl), segment);
      }
      if (pattern->data.destructure_named.rest_name) {
        sm__liveness_mark_write(liveness, layout,
            compiler__name_val(layout->heap, layout->intern_table,
                               pattern->data.destructure_named.rest_name,
                               pattern->data.destructure_named.rest_name_len),
            segment);
      }
      break;
    case AST_COMMAND: {
      /* Bracket destructure [a b c] */
      AstNode* hd = pattern->data.command.head;
      if (hd->type == AST_LIT_STRING) {
        const char* s = hd->data.lit_string.value;
        uint32_t sl = hd->data.lit_string.length;
        if (!(sl == 2 && s[0] == '.' && s[1] == '.') &&
            !(sl == 1 && s[0] == '_')) {
          sm__liveness_mark_write(liveness, layout,
              compiler__name_val(layout->heap, layout->intern_table, s, sl), segment);
        }
      } else if (hd->type == AST_SPREAD && hd->data.spread.expr &&
                 hd->data.spread.expr->type == AST_LIT_STRING) {
        sm__liveness_mark_write(liveness, layout,
            sm__lit_string_name(layout, hd->data.spread.expr), segment);
      }
      for (uint32_t i = 0; i < pattern->data.command.arg_count; i++) {
        AstNode* elem = pattern->data.command.args[i];
        if (elem->type == AST_LIT_STRING) {
          const char* s = elem->data.lit_string.value;
          uint32_t sl = elem->data.lit_string.length;
          if (sl == 2 && s[0] == '.' && s[1] == '.') continue;
          if (sl == 1 && s[0] == '_') continue;
          sm__liveness_mark_write(liveness, layout,
              compiler__name_val(layout->heap, layout->intern_table, s, sl), segment);
        } else if (elem->type == AST_SPREAD && elem->data.spread.expr &&
                   elem->data.spread.expr->type == AST_LIT_STRING) {
          sm__liveness_mark_write(liveness, layout,
              sm__lit_string_name(layout, elem->data.spread.expr), segment);
        }
      }
      break;
    }
    case AST_BLOCK: {
      /* Curly destructure {a, b, c} */
      for (uint32_t i = 0; i < pattern->data.block.count; i++) {
        AstNode* cmd = pattern->data.block.commands[i];
        if (cmd->type == AST_LIT_STRING) continue;
        if (cmd->type != AST_COMMAND) continue;
        AstNode* chd = cmd->data.command.head;
        if (chd->type != AST_LIT_STRING) continue;
        const char* hstr = chd->data.lit_string.value;
        uint32_t hlen = chd->data.lit_string.length;
        if (hlen == 2 && hstr[0] == '.' && hstr[1] == '.') {
          if (cmd->data.command.arg_count == 1 &&
              cmd->data.command.args[0]->type == AST_LIT_STRING) {
            sm__liveness_mark_write(liveness, layout,
                sm__lit_string_name(layout, cmd->data.command.args[0]), segment);
          }
        } else if (cmd->data.command.arg_count == 1 &&
                   cmd->data.command.args[0]->type == AST_LIT_STRING) {
          sm__liveness_mark_write(liveness, layout,
              sm__lit_string_name(layout, cmd->data.command.args[0]), segment);
        } else if (cmd->data.command.arg_count == 0) {
          sm__liveness_mark_write(liveness, layout,
              compiler__name_val(layout->heap, layout->intern_table, hstr, hlen), segment);
        }
      }
      break;
    }
    default:
      break;
  }
}

/* Forward declaration — defined later in this file. */
bool ast__contains_suspension(AstNode* node, SuspensionMap* map,
                               ThreadHeap* heap, JaclInternTable* intern_table);

/* Check if the body of a loop (while/for) directly contains suspension. */
bool sm__loop_body_suspends(AstNode* body) {
  return ast__contains_suspension(body, NULL, NULL, NULL);
}

/* Liveness walker: walks AST tracking suspension segments and recording
   variable reads/writes per segment. */
typedef struct {
  const StateLayout* layout;
  FieldLiveness*     liveness;
  int32_t*           segment;
} LivenessCtx;

static void sm__liveness_walk__visit(AstNode* node, void* vctx) {
  if (!node) return;
  LivenessCtx* ctx = (LivenessCtx*)vctx;
  const StateLayout* layout = ctx->layout;
  FieldLiveness* liveness = ctx->liveness;
  int32_t* segment = ctx->segment;

  if (node->type == AST_VAR_REF) {
    if (node->data.var_ref.length <= 128) {
      JaclVal name = compiler__name_val(layout->heap, layout->intern_table,
                                        node->data.var_ref.name,
                                        node->data.var_ref.length);
      sm__liveness_mark_read(liveness, layout, name, *segment);
    }
    return;
  }

  if (node->type == AST_COMMAND) {
      AstNode* head = node->data.command.head;
      HeadId hid = (HeadId)node->data.command.head_id;
      uint32_t argc = node->data.command.arg_count;
      AstNode** args = node->data.command.args;

      if (head->type == AST_LIT_STRING) {

        /* --- Suspension points: increment segment AFTER evaluating args --- */
        if (hid == HEAD_YIELD || hid == HEAD_AWAIT ||
            hid == HEAD_PARALLEL || hid == HEAD_RACE ||
            hid == HEAD_SLEEP) {
          for (uint32_t i = 0; i < argc; i++) {
            sm__liveness_walk__visit(args[i], ctx);
          }
          (*segment)++;
          return;
        }

        /* --- def / mut / = / : — mark binding names as writes --- */
        if (hid == HEAD_DEF || hid == HEAD_MUT ||
            hid == HEAD_EQUALS || hid == HEAD_COLON) {
          uint32_t val_idx = (argc == 3) ? 2 : 1;
          if (val_idx < argc) {
            sm__liveness_walk__visit(args[val_idx], ctx);
          }
          if (argc >= 2) {
            uint32_t name_idx = (argc == 3) ? 1 : 0;
            sm__liveness_mark_binding_names(args[name_idx], layout, liveness,
                                            *segment);
          }
          return;
        }

        /* --- set: mark target as write, walk value --- */
        if (hid == HEAD_SET || hid == HEAD_COLON_COLON) {
          if (argc >= 2) {
            sm__liveness_walk__visit(args[1], ctx);
            if (args[0]->type == AST_LIT_STRING) {
              sm__liveness_mark_write(liveness, layout,
                  sm__lit_string_name(layout, args[0]), *segment);
            }
          }
          return;
        }

        /* --- while: handle suspending loops with back-edge expansion --- */
        if (hid == HEAD_WHILE) {
          if (argc >= 2) {
            AstNode* cond = args[0];
            AstNode* body = args[argc - 1];
            bool loop_suspends = sm__loop_body_suspends(body);
            if (loop_suspends) {
              /* Record segment at loop entry */
              int32_t loop_start = *segment;
              /* Walk condition and body normally */
              sm__liveness_walk__visit(cond, ctx);
              sm__liveness_walk__visit(body, ctx);
              int32_t loop_end = *segment;
              /* Expand ranges: any field touched during the loop
                 must span the full loop range due to back-edge */
              for (uint32_t fi = 0; fi < layout->field_count; fi++) {
                bool touched =
                  (liveness[fi].first_write >= loop_start &&
                   liveness[fi].first_write <= loop_end) ||
                  (liveness[fi].last_read >= loop_start &&
                   liveness[fi].last_read <= loop_end);
                if (touched) {
                  if (liveness[fi].first_write < 0 ||
                      loop_start < liveness[fi].first_write)
                    liveness[fi].first_write = loop_start;
                  if (liveness[fi].last_read < 0 ||
                      loop_end > liveness[fi].last_read)
                    liveness[fi].last_read = loop_end;
                }
              }
            } else {
              /* Non-suspending loop: walk normally */
              sm__liveness_walk__visit(cond, ctx);
              sm__liveness_walk__visit(body, ctx);
            }
          }
          return;
        }

        /* --- for: loop variable binding + suspending loop handling --- */
        if (hid == HEAD_FOR) {
          if (argc >= 2) {
            AstNode* body = args[argc - 1];
            bool loop_suspends = (body->type == AST_BLOCK) &&
                                 sm__loop_body_suspends(body);
            int32_t loop_start = *segment;

            /* Walk collection expression */
            sm__liveness_walk__visit(args[0], ctx);

            /* Mark for-loop binding variable */
            if (argc == 3 && args[1]->type == AST_LIT_STRING) {
              sm__liveness_mark_write(liveness, layout,
                  sm__lit_string_name(layout, args[1]), *segment);
            } else if (argc == 2 && body->type == AST_BLOCK &&
                       !(args[0]->type == AST_BLOCK)) {
              sm__liveness_mark_write(liveness, layout,
                  jacl_inline_string("it", 2), *segment);
            }

            /* Walk body */
            sm__liveness_walk__visit(body, ctx);

            if (loop_suspends) {
              int32_t loop_end = *segment;
              for (uint32_t fi = 0; fi < layout->field_count; fi++) {
                bool touched =
                  (liveness[fi].first_write >= loop_start &&
                   liveness[fi].first_write <= loop_end) ||
                  (liveness[fi].last_read >= loop_start &&
                   liveness[fi].last_read <= loop_end);
                if (touched) {
                  if (liveness[fi].first_write < 0 ||
                      loop_start < liveness[fi].first_write)
                    liveness[fi].first_write = loop_start;
                  if (liveness[fi].last_read < 0 ||
                      loop_end > liveness[fi].last_read)
                    liveness[fi].last_read = loop_end;
                }
              }
            }
          }
          return;
        }

        /* --- try: catch binding is scope-local (cannot suspend), skip it --- */
        if (hid == HEAD_TRY) {
          if (argc >= 1) sm__liveness_walk__visit(args[0], ctx);
          if (argc >= 3) sm__liveness_walk__visit(args[2], ctx);
          return;
        }

        /* --- proc: named proc = write; don't recurse into body --- */
        if (hid == HEAD_PROC) {
          uint32_t name_idx;
          if (argc == 3) name_idx = 0;
          else if (argc == 4) name_idx = 1;
          else return;
          if (args[name_idx]->type == AST_LIT_STRING) {
            sm__liveness_mark_write(liveness, layout,
                sm__lit_string_name(layout, args[name_idx]), *segment);
          }
          return;
        }

        /* --- spawn: separate scope, don't recurse --- */
        if (hid == HEAD_SPAWN) return;
      }

      /* Head might be a var ref or a bare-word proc call */
      if (head->type == AST_VAR_REF && head->data.var_ref.length <= 128) {
        sm__liveness_mark_read(liveness, layout,
            compiler__name_val(layout->heap, layout->intern_table,
                               head->data.var_ref.name,
                               head->data.var_ref.length), *segment);
      } else if (head->type == AST_LIT_STRING &&
                 head->data.lit_string.length <= 128) {
        sm__liveness_mark_read(liveness, layout,
            compiler__name_val(layout->heap, layout->intern_table,
                               head->data.lit_string.value,
                               head->data.lit_string.length), *segment);
      }
      /* Walk all arguments for any other command */
      for (uint32_t i = 0; i < argc; i++) {
        sm__liveness_walk__visit(args[i], ctx);
      }
      return;
  }

  if (node->type == AST_SPREAD) {
    if (node->data.spread.expr) {
      sm__liveness_walk__visit(node->data.spread.expr, ctx);
    }
    return;
  }

  ast__walk_children(node, sm__liveness_walk__visit, ctx);
}

void sm__liveness_walk(AstNode* node, const StateLayout* layout,
                               FieldLiveness* liveness, int32_t* segment) {
  LivenessCtx ctx = { layout, liveness, segment };
  sm__liveness_walk__visit(node, &ctx);
}

/* Run liveness analysis and filter the state layout in-place.
   Keeps only fields that are parameters OR cross a suspension boundary
   (first_write < last_read with a suspension between them).
   When suspension_count == 0, skips analysis (no optimisation possible).
   body is the function body AST, needed for the liveness walk. */
void sm__optimize_state_layout(SuspensionAnalysis* analysis,
                                       AstNode* body) {
  StateLayout* layout = &analysis->state_layout;

  /* Nothing to optimize if no suspensions or no fields */
  if (analysis->suspension_count == 0 || layout->field_count == 0) return;

  /* Only optimize yield-only generators.  Await/parallel/race have a
     diamond control flow (inline resolution vs resume path) that makes
     stack-local slot numbering inconsistent across the two paths. */
  for (uint32_t i = 0; i < analysis->suspension_count; i++) {
    if (analysis->suspension_points[i].type != SUSPEND_YIELD) return;
  }

  /* Initialize liveness data */
  FieldLiveness liveness[SM_MAX_STATE_FIELDS];
  for (uint32_t i = 0; i < layout->field_count; i++) {
    liveness[i].first_write = -1;
    liveness[i].last_read   = -1;
  }

  /* Parameters are implicitly written at segment 0 (function entry) */
  for (uint32_t i = 0; i < layout->field_count; i++) {
    if (layout->fields[i].is_param) {
      liveness[i].first_write = 0;
    }
  }

  /* Walk the AST body to collect read/write segment info */
  int32_t segment = 0;
  if (body->type == AST_BLOCK) {
    for (uint32_t i = 0; i < body->data.block.count; i++) {
      sm__liveness_walk(body->data.block.commands[i], layout, liveness,
                        &segment);
    }
  } else {
    sm__liveness_walk(body, layout, liveness, &segment);
  }

  /* Determine which fields cross a suspension boundary.
     A field crosses if first_write < last_read (value written before
     a suspension is needed after it).  Parameters always cross. */
  bool crosses[SM_MAX_STATE_FIELDS];
  uint32_t keep_count = 0;
  for (uint32_t i = 0; i < layout->field_count; i++) {
    if (layout->fields[i].is_param) {
      crosses[i] = true;  /* params always kept */
    } else if (liveness[i].first_write >= 0 && liveness[i].last_read >= 0 &&
               liveness[i].first_write < liveness[i].last_read) {
      crosses[i] = true;
    } else {
      crosses[i] = false;
    }
    if (crosses[i]) keep_count++;
  }

  /* If all fields cross, nothing to filter */
  if (keep_count == layout->field_count) return;

  /* Compact the layout: remove non-crossing fields, re-index with
     width-aware slot assignment.  Multi-slot struct fields are treated
     as atomic units — never partially evicted. */
  StateField new_fields[SM_MAX_STATE_FIELDS];
  uint32_t new_count = 0;
  uint32_t new_total_slots = 0;
  for (uint32_t i = 0; i < layout->field_count; i++) {
    if (crosses[i]) {
      new_fields[new_count] = layout->fields[i];
      new_fields[new_count].field_index = new_total_slots;
      new_total_slots += layout->fields[i].width;
      new_count++;
    }
  }
  memcpy(layout->fields, new_fields, sizeof(StateField) * new_count);
  layout->field_count = new_count;
  layout->total_slots = new_total_slots;
}

/* Analyze a function body's AST for state machine compilation.
   Returns suspension points numbered sequentially and, for suspending
   functions, a StateLayout mapping every local to a state object field.
   param_names/param_count describe the function's parameters (placed first
   in the layout).  Pass NULL/0 for non-function contexts.
   When optimize_liveness is true, prunes locals that don't cross any
   suspension boundary (they remain as normal stack locals). */
SuspensionAnalysis compiler__analyze_suspensions(AstNode* body,
                                                        JaclVal* param_names,
                                                        uint8_t  param_count,
                                                        bool     optimize_liveness,
                                                        SuspensionMap* map,
                                                        ThreadHeap* heap,
                                                        JaclInternTable* intern_table,
                                                        StructTypeRegistry* struct_reg) {
  SuspensionAnalysis analysis;
  memset(&analysis, 0, sizeof(analysis));
  analysis.ctx_field_idx = UINT32_MAX;
  analysis.state_layout.heap = heap;
  analysis.state_layout.intern_table = intern_table;

  if (!body) return analysis;

  /* Pass 1: find suspension points */
  if (body->type == AST_BLOCK) {
    for (uint32_t i = 0; i < body->data.block.count; i++) {
      sm__walk_suspensions(body->data.block.commands[i], &analysis, map,
                           heap, intern_table);
    }
  } else {
    sm__walk_suspensions(body, &analysis, map, heap, intern_table);
  }

  /* Pass 2: build state layout.  Always build it so that transitively
     suspending procs (suspension_count == 0 but proc_suspends via callee)
     still get a proper state layout for their SM compilation. */
  {
    /* Parameters go first in the layout */
    for (uint8_t i = 0; i < param_count; i++) {
      sm__add_state_field(&analysis.state_layout, param_names[i], false, true, 1, 0);
    }
    /* Then body locals — pass struct registry for width computation */
    if (body->type == AST_BLOCK) {
      for (uint32_t i = 0; i < body->data.block.count; i++) {
        sm__walk_locals(body->data.block.commands[i], &analysis.state_layout,
                        struct_reg);
      }
    } else {
      sm__walk_locals(body, &analysis.state_layout, struct_reg);
    }
  }

  /* Pass 3 (optional): liveness optimization — remove locals that don't
     cross any suspension boundary from the state layout. */
  if (optimize_liveness) {
    sm__optimize_state_layout(&analysis, body);
  }

  /* Add implicit __ctx field (always crosses suspension boundaries).
     Added after liveness optimization so it's never pruned. */
  analysis.ctx_field_idx = analysis.state_layout.total_slots;
  sm__add_state_field(&analysis.state_layout,
                      jacl_inline_string("__ctx", 5), false, false, 1, 0);

  /* Reserve operand-stack spill slots. When a suspension occurs inside an
     expression that already has live operand-stack values (e.g.
     `[+ [+ [await $a] [await $b]] [await $c]]`), runtime__setup_call wipes
     the operand stack on re-entry, so any pre-suspension values must be
     stashed into state slots and restored after the resume label. We
     reserve max(pre_stack_depth) slots plus one scratch slot used to
     reshuffle the result of the await/sleep above the restored stack.
     These slots are unnamed (no fields[] entry); they only bump
     total_slots so the SM allocator sizes its fields[] array correctly
     and the GC traces them as ordinary roots. */
  {
    uint16_t max_depth = 0;
    for (uint32_t i = 0; i < analysis.suspension_count; i++) {
      if (analysis.suspension_points[i].pre_stack_depth > max_depth)
        max_depth = analysis.suspension_points[i].pre_stack_depth;
    }
    analysis.max_pre_stack_depth = max_depth;
    if (max_depth > 0) {
      analysis.spill_base_slot = (uint16_t)analysis.state_layout.total_slots;
      analysis.state_layout.total_slots += max_depth;
      analysis.scratch_slot = (uint16_t)analysis.state_layout.total_slots;
      analysis.state_layout.total_slots += 1;
    }
  }

  return analysis;
}

/* Check if an AST subtree contains any suspension points.
   When map is non-NULL, also checks if named proc calls are suspending. */
typedef struct {
  SuspensionMap*   map;
  ThreadHeap*      heap;
  JaclInternTable* intern_table;
} ContainsSuspCtx;

static bool ast__contains_suspension__pred(AstNode* node, void* vctx) {
  if (!node) return false;
  ContainsSuspCtx* ctx = (ContainsSuspCtx*)vctx;

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING) {
      HeadId hid = (HeadId)node->data.command.head_id;
      if (hid == HEAD_AWAIT || hid == HEAD_PARALLEL ||
          hid == HEAD_RACE  || hid == HEAD_YIELD ||
          hid == HEAD_SLEEP) {
        return true;
      }
      if (hid == HEAD_PROC || hid == HEAD_SPAWN) return false;
      if (ctx->map) {
        JaclVal name_val = compiler__name_val(
            ctx->heap, ctx->intern_table,
            head->data.lit_string.value, head->data.lit_string.length);
        if (suspension_map_lookup(ctx->map, name_val) &&
            !suspension_map_is_generator(ctx->map, name_val)) return true;
      }
    }
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      if (ast__contains_suspension__pred(node->data.command.args[i], ctx))
        return true;
    }
    return false;
  }
  return ast__any_child(node, ast__contains_suspension__pred, ctx);
}

bool ast__contains_suspension(AstNode* node, SuspensionMap* map,
                               ThreadHeap* heap, JaclInternTable* intern_table) {
  ContainsSuspCtx ctx = { map, heap, intern_table };
  return ast__contains_suspension__pred(node, &ctx);
}

/* Check if an AST subtree contains any set! calls (mutable global mutation).
   Skips nested proc/spawn/parallel/race definitions since those are separate
   closure scopes with independent pinning decisions. */
/**
 * Collect all mut declaration names directly in this AST subtree.
 * Skips nested proc/spawn/parallel/race scopes (they are separate bodies).
 */
#define AST_LOCAL_MUTS_MAX 64
typedef struct {
  JaclVal*         names;
  uint32_t*        count;
  ThreadHeap*      heap;
  JaclInternTable* intern_table;
} CollectMutsCtx;

static void ast__collect_local_muts__visit(AstNode* node, void* vctx) {
  CollectMutsCtx* ctx = (CollectMutsCtx*)vctx;
  if (!node || *ctx->count >= AST_LOCAL_MUTS_MAX) return;

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING) {
      HeadId hid = (HeadId)node->data.command.head_id;
      if (hid == HEAD_MUT) {
        uint32_t argc = node->data.command.arg_count;
        if (argc >= 2 && node->data.command.args[0]->type == AST_LIT_STRING) {
          AstNode* name_node = node->data.command.args[0];
          ctx->names[*ctx->count] = compiler__name_val(
              ctx->heap, ctx->intern_table,
              name_node->data.lit_string.value,
              name_node->data.lit_string.length);
          (*ctx->count)++;
        }
        return;
      }
      if (hid == HEAD_PROC || hid == HEAD_SPAWN ||
          hid == HEAD_PARALLEL || hid == HEAD_RACE) return;
    }
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      ast__collect_local_muts__visit(node->data.command.args[i], ctx);
    }
    return;
  }
  ast__walk_children(node, ast__collect_local_muts__visit, ctx);
}

void ast__collect_local_muts(AstNode* node, JaclVal* names,
                                     uint32_t* count,
                                     ThreadHeap* heap,
                                     JaclInternTable* intern_table) {
  CollectMutsCtx ctx = { names, count, heap, intern_table };
  ast__collect_local_muts__visit(node, &ctx);
}

/**
 * Check if an AST subtree contains set! targeting a non-local variable.
 * A "local" here means a mut declared within the same body scope.
 *
 * NOTE: This is a conservative syntactic analysis, NOT full escape analysis.
 * It covers:
 *   - Direct set! on non-local mutable variables
 *   - $var references to mut/box bindings from enclosing scopes (US-002)
 *   - Transitive capture: closure capturing another closure that captures
 *     a mutable binding (US-003). Detected via captures_mutable flag on
 *     Local/Upvalue/GlobalArity — set after proc compilation if any upvalue
 *     is_mutable or captures_mutable. Propagated through upvalue resolution,
 *     continuation eager capture, and checked for both $var refs and
 *     function call targets in concurrent bodies.
 * Out of scope:
 *   - Box references stored in collections then retrieved on another thread
 *   - Dynamic box creation passed indirectly through data structures
 */
typedef struct {
  JaclVal*         local_muts;
  uint32_t         local_mut_count;
  ThreadHeap*      heap;
  JaclInternTable* intern_table;
} NonlocalSetCtx;

static bool ast__contains_nonlocal_set__pred(AstNode* node, void* vctx) {
  if (!node) return false;
  NonlocalSetCtx* ctx = (NonlocalSetCtx*)vctx;

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING) {
      HeadId hid = (HeadId)node->data.command.head_id;
      if (hid == HEAD_SET) {
        uint32_t argc = node->data.command.arg_count;
        if (argc >= 1 && node->data.command.args[0]->type == AST_LIT_STRING) {
          AstNode* target = node->data.command.args[0];
          JaclVal target_name = compiler__name_val(
              ctx->heap, ctx->intern_table,
              target->data.lit_string.value,
              target->data.lit_string.length);
          for (uint32_t i = 0; i < ctx->local_mut_count; i++) {
            if (ctx->local_muts[i] == target_name) return false;
          }
        }
        return true;
      }
      if (hid == HEAD_PROC || hid == HEAD_SPAWN ||
          hid == HEAD_PARALLEL || hid == HEAD_RACE) return false;
    }
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      if (ast__contains_nonlocal_set__pred(node->data.command.args[i], ctx))
        return true;
    }
    return false;
  }
  return ast__any_child(node, ast__contains_nonlocal_set__pred, ctx);
}

bool ast__contains_nonlocal_set_impl(AstNode* node,
                                             JaclVal* local_muts,
                                             uint32_t local_mut_count,
                                             ThreadHeap* heap,
                                             JaclInternTable* intern_table) {
  NonlocalSetCtx ctx = { local_muts, local_mut_count, heap, intern_table };
  return ast__contains_nonlocal_set__pred(node, &ctx);
}

/**
 * Check if a block AST contains set! targeting a non-local mutable variable.
 * First collects all mut declarations in the block, then checks if any set!
 * targets a name not in that set. Skips nested proc/spawn/parallel/race scopes.
 *
 * Used to decide whether a concurrent body (spawn/parallel/race) needs to be
 * pinned to thread 0. Bodies with only local mutations can run on any worker.
 */
bool ast__contains_nonlocal_set(AstNode* block,
                                 ThreadHeap* heap,
                                 JaclInternTable* intern_table) {
  JaclVal local_muts[AST_LOCAL_MUTS_MAX];
  uint32_t local_mut_count = 0;

  /* First pass: collect all mut names declared in this body */
  ast__collect_local_muts(block, local_muts, &local_mut_count,
                          heap, intern_table);

  /* Second pass: check if any set! targets a non-local name */
  return ast__contains_nonlocal_set_impl(block, local_muts, local_mut_count,
                                          heap, intern_table);
}

/* --- Internal: Loop context for break/continue --- */

#define COMPILER_LOOP_DEPTH_MAX 16
#define COMPILER_BREAK_PATCHES_MAX 32
#define COMPILER_CONTINUE_PATCHES_MAX 32

typedef struct {
  uint32_t loop_start;     /* bytecode offset of loop condition (for OP_LOOP) */
  uint32_t break_patches[COMPILER_BREAK_PATCHES_MAX];
  uint32_t break_patch_count;
  uint32_t continue_patches[COMPILER_CONTINUE_PATCHES_MAX];
  uint32_t continue_patch_count;
  /* Snapshot of c->local_count just before the loop pushed *any* of its
   * own locals (iter-state hidden locals + init vars + body locals).
   * Used by break to clean up ALL of them when exiting the loop. */
  uint32_t local_count_at_loop;
  /* Snapshot AFTER iter-state / init locals but BEFORE body-declared
   * locals. Used by continue to clean up only body locals (iter state
   * must persist across iterations). For while-loops where there is no
   * separate iter-state phase, equals local_count_at_loop. */
  uint32_t body_local_count;
  bool     is_for_loop;         /* true for inlined for-loops */
} LoopContext;

/* --- State machine dispatch table context (US-006) --- */

typedef struct {
  uint32_t label_patches[SM_MAX_SUSPENSION_POINTS]; /* jump offsets to backpatch */
  uint32_t label_count;                             /* number of resume points (= suspension_count) */
} SMDispatchContext;

/* --- Macro table --- */

#define MACRO_TABLE_MAX 64

typedef struct {
  const char*   name;        /* macro name (arena-allocated, NUL-terminated) */
  uint32_t      name_len;    /* length of name */
  uint32_t      param_count; /* number of macro parameters */
  bool          variadic;    /* last param is rest-param (receives vec of remaining args) */
  bool          is_builtin;  /* true if registered in Phase 0 (can be overridden by user) */
  const char**  param_names; /* arena-allocated array of param name strings */
  uint32_t*     param_name_lens; /* lengths of each param name */
  JaclClosure*  closure;     /* compiled macro body closure */
  AstNode*      body;        /* original body AST for macro body compilation */
} MacroEntry;

struct MacroTable {
  MacroEntry entries[MACRO_TABLE_MAX];
  uint32_t   count;
};

void macro_table_init(MacroTable* t) {
  t->count = 0;
}

MacroEntry* macro_table_lookup(MacroTable* t, const char* name, uint32_t name_len) {
  for (uint32_t i = 0; i < t->count; i++) {
    if (t->entries[i].name_len == name_len &&
        memcmp(t->entries[i].name, name, name_len) == 0) {
      return &t->entries[i];
    }
  }
  return NULL;
}

/* Check if a name matches a compiler special form / builtin that macros
   must not shadow.  Only the core control-flow / binding keywords are
   blocked — library builtins (print, length, …) are fine to shadow. */
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

/* --- Internal: Compiler state --- */

/* Inline struct representation enum (stored as uint8_t in Compiler). */
enum {
  INLINE_NONE  = 0,  /* normal heap value (single stack slot) */
  INLINE_STACK = 1,  /* inline struct bytes on stack (multiple slots) */
  INLINE_REF   = 2,  /* byte-offset reference into an inline struct local */
};

typedef struct Compiler Compiler;
struct Compiler {
  BytecodeChunk*   chunk;
  arena_t*         arena;
  ThreadHeap*      heap;          /* GC heap for string interning */
  JaclInternTable* intern_table;  /* shared intern table for heap strings */
  uint32_t         error_count;
  const char*      first_error;
  Local            locals[COMPILER_LOCALS_MAX];
  uint32_t         local_count;
  int              scope_depth;
  Upvalue          upvalues[COMPILER_UPVALUES_MAX];
  uint32_t         upvalue_count;
  Compiler*        enclosing;  /* parent compiler for upvalue resolution */
  GlobalArity      global_arities[COMPILER_GLOBAL_ARITIES_MAX];
  uint32_t         global_arity_count;
  uint32_t         try_patches[COMPILER_TRY_PATCHES_MAX];
  uint32_t         try_patch_count;
  bool             in_try_body;
  bool             in_non_suspending_callback; /* error if suspension inside */
  SuspensionMap*   suspension_map;  /* pre-computed suspension analysis */
  bool             in_concurrent_body; /* true inside spawn/parallel/race body */
  bool             pin_all_closures;  /* true when concurrent body touches mutable globals */
  bool             force_global_procs; /* procs emit OP_DEF_GLOBAL even at scope>0 */
  bool             ctx_pre_registered; /* true when ctx fields were pre-registered (Phase 1c) */
  JaclType         last_expr_type;  /* runtime stack representation post-emit;
                                       read only by compiler__ensure_boxed
                                       (declared types live on AstNode) */
  JaclType         return_type;     /* declared return type for current function */
  uint32_t         return_struct_idx; /* struct registry index when return_type==TYPE_STRUCT */
  ModuleCache*     module_cache;    /* shared cache of compiled modules */
  Module*          current_module;  /* module currently being compiled */
  ImportStack*     import_stack;    /* shared import stack for circular detection */
  const char*      module_prefix;   /* "basename::" for namespace-prefixed globals */
  uint32_t         module_prefix_len;
  StructTypeRegistry* struct_registry; /* shared struct type registry (root compiler owns) */
  LoopContext          loop_stack[COMPILER_LOOP_DEPTH_MAX];
  uint32_t             loop_depth;     /* current nesting depth (0 = not in loop) */
  bool                 has_yield;      /* true if current proc body contains yield */
  uint32_t             sm_suspension_idx; /* next suspension point index for SM yield emission */
  SMDispatchContext     sm_dispatch;    /* dispatch table jump patches for SM compilation */
  SuspensionAnalysis*  sm_analysis;    /* suspension analysis for current SM function (or NULL) */
  MacroTable*          macro_table;    /* compile-time macro definitions (root compiler owns) */
  uint32_t             current_scope_mark; /* hygiene: mark for newly introduced bindings */
  bool                 has_prelude;    /* true when compiling under a caller-supplied prelude map */
  /* Inline struct representation for the last compiled expression. */
  uint8_t              inline_repr;     /* INLINE_NONE / INLINE_STACK / INLINE_REF */
  uint8_t              inline_ref_base;     /* base local slot (valid when INLINE_REF) */
  uint16_t             inline_ref_offset;   /* byte offset (valid when INLINE_REF) */
  bool                 shell_fallback; /* true in REPL mode: unknown commands try PATH lookup */
  ModuleBinding        module_bindings[COMPILER_MODULE_BINDINGS_MAX];
  uint32_t             module_binding_count;
  /* Flow typing: type narrowings from box? guards in if-branches */
  struct {
    uint16_t local_slot;     /* which local variable is narrowed */
    uint32_t box_type_idx;   /* struct element type_idx (0=dyn, >0=struct/collection element) */
    uint32_t box_key_type_idx; /* key struct idx for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
    JaclType box_type;       /* TYPE_STRUCT, TYPE_TYPED_VEC, TYPE_TYPED_MAP, or TYPE_DYN */
  } narrowings[8];
  uint32_t             narrowing_count;
  CtxFieldList*        ctx_fields;       /* ctx field accumulator (root compiler owns) */
  /* Top-level mut/def → depth-0 local lowering.
   *
   * When the chunk contains no closure-creating commands (proc, defmacro,
   * spawn, parallel, race, interpret) a pre-scan sets `lower_top_level`
   * true; in that mode, top-level `mut`/`def` skip OP_DEF_GLOBAL and add
   * a depth-0 local instead. Reads ($name) and writes (set name VAL) at
   * top-level then naturally resolve to OP_GET_LOCAL / OP_SET_LOCAL via
   * compiler__resolve_local. The env-side OP_GET_GLOBAL/OP_SET_GLOBAL
   * inline cache then has no work to do for these names (still needed
   * for prelude callables and any unrecognised reference).
   *
   * Conservative single-flag analysis (vs per-name capture tracking)
   * because procs that read top-level names would have to be captured
   * as upvalues — possible in principle but a substantial refactor.
   * For the existing JACL bench suite the no-closures case covers
   * sieve_primes, box_churn, map_lookup_hot, collection_churn, and
   * string_concat (the OP_GET_GLOBAL-bound scenarios); spawn_chain,
   * fib_recursive, parallel_map_reduce keep the env path. */
  bool                 lower_top_level;
};

/* --- Phase 2 helper: compile a typed-collection element argument.
 * Returns true iff the typer-annotated arg type matches the
 * expected element type. Caller emits the error on false. */
void compiler__compile_node(Compiler* c, AstNode* node); /* fwd decl */

static bool compiler__compile_typed_elem_arg(Compiler* c, AstNode* arg,
                                             uint32_t expected_type_idx) {
  bool is_scalar = COMPILER_IS_SCALAR_TYPE_IDX(expected_type_idx);
  compiler__compile_node(c, arg);
  if (is_scalar) {
    return (JaclType)arg->inferred_type ==
           COMPILER_TYPE_IDX_TO_SCALAR(expected_type_idx);
  }
  return (JaclType)arg->inferred_type == TYPE_STRUCT &&
         arg->inferred_struct_idx == expected_type_idx;
}

void compiler__init(Compiler* c, BytecodeChunk* chunk, arena_t* arena,
                           JaclInternTable* intern_table, ThreadHeap* heap) {
  c->chunk         = chunk;
  c->arena         = arena;
  c->heap          = heap;
  c->intern_table  = intern_table;
  c->error_count   = 0;
  c->first_error   = NULL;
  c->local_count   = 0;
  c->scope_depth   = 0;
  c->upvalue_count = 0;
  c->enclosing     = NULL;
  c->global_arity_count = 0;
  c->try_patch_count = 0;
  c->in_try_body     = false;
  c->in_non_suspending_callback = false;
  c->suspension_map  = NULL;
  c->in_concurrent_body = false;
  c->pin_all_closures   = false;
  c->force_global_procs = false;
  c->ctx_pre_registered = false;
  c->last_expr_type = TYPE_DYN;
  c->return_type     = TYPE_DYN;
  c->return_struct_idx = UINT32_MAX;
  c->module_cache    = NULL;
  c->current_module  = NULL;
  c->import_stack    = NULL;
  c->module_prefix     = NULL;
  c->module_prefix_len = 0;
  c->struct_registry   = NULL;
  c->loop_depth        = 0;
  c->has_yield         = false;
  c->sm_suspension_idx = 0;
  memset(&c->sm_dispatch, 0, sizeof(SMDispatchContext));
  c->sm_analysis       = NULL;
  c->macro_table       = NULL;
  c->current_scope_mark = 0;
  c->has_prelude       = false;
  c->inline_repr        = INLINE_NONE;
  c->inline_ref_base    = 0;
  c->inline_ref_offset  = 0;
  c->shell_fallback    = false;
  c->module_binding_count = 0;
  c->narrowing_count   = 0;
  c->ctx_fields        = NULL;
  c->lower_top_level   = false;
}

/* --- Top-level lowering pre-scan ---
 *
 * For lowering, we need TWO things to be true about the chunk:
 *   (a) no closure-creating / env-observing constructs anywhere
 *       (proc, defmacro, spawn, parallel, race, await, yield,
 *        interpret, use) — otherwise a callee could expect to read
 *        the top-level name through the env;
 *   (b) at least one loop construct (while, for) so the lowering
 *       actually pays for itself.
 *
 * (b) is what protects the embed / REPL multi-eval flow: `def x 42`
 * by itself doesn't loop, so the chunk falls back to OP_DEF_GLOBAL
 * and a follow-up `$x` chunk can read it from env. Loop-heavy
 * chunks (sieve_primes-style) get the local-slot fast path.
 *
 * Two scanners share the AST walk to keep compile-time cost flat. */
typedef enum {
  TL_SCAN_HAS_CLOSURE = 1 << 0,
  TL_SCAN_HAS_LOOP    = 1 << 1,
} TopLevelScanFlags;

static uint32_t compiler__top_level_scan(AstNode* node) {
  if (!node) return 0;
  uint32_t flags = 0;
  if (node->type == AST_COMMAND) {
    HeadId hid = (HeadId)node->data.command.head_id;
    if (hid == HEAD_PROC      || hid == HEAD_DEFMACRO ||
        hid == HEAD_SPAWN     || hid == HEAD_PARALLEL ||
        hid == HEAD_RACE      || hid == HEAD_AWAIT    ||
        hid == HEAD_YIELD     ||
        hid == HEAD_INTERPRET || hid == HEAD_INTERPRET_PRELUDE) {
      flags |= TL_SCAN_HAS_CLOSURE;
    }
    if (hid == HEAD_WHILE || hid == HEAD_FOR) {
      flags |= TL_SCAN_HAS_LOOP;
    }
    flags |= compiler__top_level_scan(node->data.command.head);
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      flags |= compiler__top_level_scan(node->data.command.args[i]);
    }
    return flags;
  }
  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) {
      flags |= compiler__top_level_scan(node->data.block.commands[i]);
    }
    return flags;
  }
  if (node->type == AST_USE) {
    return TL_SCAN_HAS_CLOSURE;  /* imports inject names at chunk boundary */
  }
  return 0;
}

/* --- Provable error-freeness ---
 *
 * Returns true iff the runtime value produced by `node` provably cannot
 * carry the JACL_FLAG_ERROR flag bit. Used by HEAD_BOX to choose between
 * OP_BOX (checked) and OP_BOX_UNCHECKED (skips the error-propagation
 * branch in the VM dispatch).
 *
 * The error flag propagates through arithmetic only when an operand
 * already carries it (see jacl_add_i32 et al. in value.c). So an
 * arithmetic expression on recursively-error-free operands is itself
 * error-free — except for div / mod, which produce a div-by-zero error
 * even on clean operands.
 *
 * Var refs, function calls, collection accesses, and anything reading
 * from the env conservatively return false: any of those could pull
 * in an error-flagged value the typer can't see. */
static bool compiler__expr_is_error_free(AstNode* node) {
  if (!node) return false;
  switch (node->type) {
    case AST_LIT_INT:
    case AST_LIT_FLOAT:
    case AST_LIT_STRING:
      return true;
    case AST_COMMAND: {
      HeadId hid = (HeadId)node->data.command.head_id;
      if (hid != HEAD_PLUS && hid != HEAD_MINUS && hid != HEAD_STAR) {
        return false;
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (!compiler__expr_is_error_free(node->data.command.args[i])) {
          return false;
        }
      }
      return true;
    }
    default:
      return false;
  }
}

/* Forward declarations for module compilation (defined after compiler_compile) */
bool compiler__compile_module(const char* canonical_path,
                                     Compiler* importer,
                                     uint32_t line, uint32_t col);

/* Build "basename::" prefix string for a module path (arena-allocated). */
const char* module__build_prefix(const char* canonical_path, arena_t* arena,
                                         uint32_t* out_len) {
  const char* slash = strrchr(canonical_path, '/');
  const char* basename = slash ? slash + 1 : canonical_path;
  uint32_t blen = (uint32_t)strlen(basename);
  uint32_t plen = blen + 2; /* "basename::" */
  char* prefix = (char*)arena_alloc(arena, plen + 1);
  memcpy(prefix, basename, blen);
  prefix[blen] = ':';
  prefix[blen + 1] = ':';
  prefix[plen] = '\0';
  *out_len = plen;
  return prefix;
}

/* Create a namespace-prefixed global name constant.
   In module context, returns interned "prefix::name".
   Outside module context, returns inline string. */
JaclVal compiler__global_name_val(Compiler* c, const char* name,
                                          uint32_t name_len) {
  /* Walk to root compiler to find module prefix */
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;

  if (root->module_prefix) {
    /* Build prefixed name and intern it */
    char buf[256];
    uint32_t total = root->module_prefix_len + name_len;
    if (total >= sizeof(buf)) total = sizeof(buf) - 1;
    memcpy(buf, root->module_prefix, root->module_prefix_len);
    memcpy(buf + root->module_prefix_len, name,
           total - root->module_prefix_len);
    buf[total] = '\0';
    return jacl_intern(c->heap, c->intern_table, buf, total);
  }

  return compiler__name_val(c->heap, c->intern_table, name, name_len);
}

/* --- Internal: Emit helpers --- */

void compiler__emit_byte(Compiler* c, uint8_t byte, uint32_t line) {
  chunk_write(c->chunk, byte, line);
}

void compiler__emit_u16(Compiler* c, uint16_t value, uint32_t line) {
  chunk_write_u16(c->chunk, value, line);
}

void compiler__emit_constant(Compiler* c, JaclVal value, uint32_t line) {
  uint16_t index = chunk_add_constant(c->chunk, value);
  compiler__emit_byte(c, OP_CONST, line);
  compiler__emit_u16(c, index, line);
}

/* Emit OP_GET_GLOBAL or OP_SET_GLOBAL with a trailing 2-byte inline-cache
 * slot. The cache slot starts as 0xFFFF (invalid env index, never matches)
 * and the VM patches it in-place on the first successful lookup. The IC
 * lookup at runtime is a single compare against env.names[cached_slot];
 * if it matches the name constant, the cache hits and skips the linear
 * scan. See vm.c CASE(OP_GET_GLOBAL) for the read side. */
void compiler__emit_global_op(Compiler* c, uint8_t op, uint16_t name_idx,
                              uint32_t line) {
  compiler__emit_byte(c, op, line);
  compiler__emit_u16(c, name_idx, line);
  compiler__emit_u16(c, 0xFFFF, line);
}

/* Look up the operand-stack depth recorded for the current suspension point.
   Peeks `c->sm_suspension_idx` without incrementing; the caller increments it
   when emitting the actual suspend op. Returns 0 when there is no suspension
   record at that index (defensive — under correct alignment, this should not
   happen, but a missed special-form entry in
   sm__head_uses_operand_stack_for_args would manifest here). */
static uint16_t compiler__suspension_pre_stack_depth(Compiler* c) {
  if (!c->sm_analysis) return 0;
  uint32_t sp_idx = c->sm_suspension_idx;
  if (sp_idx >= c->sm_analysis->suspension_count) return 0;
  return c->sm_analysis->suspension_points[sp_idx].pre_stack_depth;
}

/* Spill `depth` operand-stack values into the SM's reserved spill slots.
   Top of stack goes to spill_base + depth - 1; bottom to spill_base + 0.
   No-op when depth == 0. Emitted immediately before code that will trigger
   an SM suspension (OP_AWAIT_SM / OP_SLEEP_SM / OP_YIELD_SM / OP_PARALLEL /
   OP_RACE) so the values survive `runtime__setup_call` zeroing stack_top
   on resume. */
static void compiler__emit_spill_operand_stack(Compiler* c, uint16_t depth,
                                                uint32_t line) {
  if (depth == 0 || !c->sm_analysis) return;
  uint16_t spill_base = c->sm_analysis->spill_base_slot;
  for (int k = (int)depth - 1; k >= 0; k--) {
    compiler__emit_byte(c, OP_SET_STATE_FIELD, line);
    compiler__emit_byte(c, (uint8_t)(spill_base + (uint16_t)k), line);
  }
}

/* Restore previously-spilled operand-stack values below the value currently
   on top. Assumes top-of-stack holds the suspension's result (await result,
   resume __rv, parallel/race vector, ...). Pops it into the scratch slot,
   pushes the spilled values back in order (slot 0 first), then pushes the
   scratch back so the result lands above. No-op when depth == 0. */
static void compiler__emit_restore_operand_stack(Compiler* c, uint16_t depth,
                                                  uint32_t line) {
  if (depth == 0 || !c->sm_analysis) return;
  uint16_t spill_base = c->sm_analysis->spill_base_slot;
  uint16_t scratch    = c->sm_analysis->scratch_slot;
  compiler__emit_byte(c, OP_SET_STATE_FIELD, line);
  compiler__emit_byte(c, (uint8_t)scratch, line);
  for (uint16_t k = 0; k < depth; k++) {
    compiler__emit_byte(c, OP_GET_STATE_FIELD, line);
    compiler__emit_byte(c, (uint8_t)(spill_base + k), line);
  }
  compiler__emit_byte(c, OP_GET_STATE_FIELD, line);
  compiler__emit_byte(c, (uint8_t)scratch, line);
}

/* --- Internal: Error reporting --- */

void compiler__error(Compiler* c, uint32_t line, uint32_t col,
                            const char* message) {
  c->error_count++;
  if (!c->first_error) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "line %u, col %u: %s", line, col, message);
    if (n < 0) n = 0;
    char* msg = (char*)arena_alloc(c->arena, (uint32_t)n + 1);
    memcpy(msg, buf, (uint32_t)n + 1);
    c->first_error = msg;
  }
}

/* --- Internal: Scope and local variable helpers --- */

void compiler__begin_scope(Compiler* c) {
  c->scope_depth++;
}

void compiler__end_scope(Compiler* c, uint32_t line) {
  c->scope_depth--;
  uint32_t pop_count = 0;
  while (c->local_count > 0 &&
         c->locals[c->local_count - 1].depth > c->scope_depth) {
    c->local_count--;
    pop_count++;
  }
  if (pop_count > 0) {
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)pop_count, line);
  }
}

void compiler__add_local(Compiler* c, JaclVal name,
                                uint32_t line, uint32_t col) {
  if (c->local_count >= COMPILER_LOCALS_MAX) {
    compiler__error(c, line, col, "too many local variables in function");
    return;
  }

  /* Same-scope shadowing check: error if an immutable binding with the same
   * name already exists at this scope depth.  Mutable bindings (mut) may be
   * re-declared by anaphoric macros (^name), so we only reject when the
   * existing binding is immutable (def).
   * Skip for empty-name padding locals (inline struct wide slots). */
  {
    uint32_t name_len = jacl_string_byte_len(name);
    if (c->scope_depth > 0 && name_len > 0) {
      for (int i = (int)c->local_count - 1; i >= 0; i--) {
        if (c->locals[i].depth < c->scope_depth) break;
        if (c->locals[i].name == name && !c->locals[i].is_mutable) {
          char nbuf[128];
          uint32_t nlen = jacl_string_byte_len(name);
          jacl_string_data(name, nbuf, sizeof(nbuf) - 1);
          if (nlen >= sizeof(nbuf)) nlen = sizeof(nbuf) - 1;
          nbuf[nlen] = '\0';
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "variable '%s' already defined in this scope", nbuf);
          compiler__error(c, line, col, err_msg);
          return;
        }
      }
    }
  }

  Local* local = &c->locals[c->local_count++];
  local->name        = name;
  local->depth       = c->scope_depth;
  local->known_arity = -1;
  local->is_mutable  = false;
  local->is_param    = false;
  local->suspends    = false;
  local->captures_mutable = false;
  local->type        = TYPE_DYN;
  local->return_type = TYPE_DYN;
  local->param_types = NULL;
  local->scope_mark  = c->current_scope_mark;
  local->width       = 1;
  local->is_inline   = false;
  local->buf_len     = 0;
}

/* Find module binding by local slot index */
Module* compiler__find_module_binding(Compiler* c, int local_slot) {
  for (uint32_t i = 0; i < c->module_binding_count; i++) {
    if (c->module_bindings[i].local_slot == local_slot) {
      return c->module_bindings[i].module;
    }
  }
  return NULL;
}

int compiler__resolve_local(Compiler* c, JaclVal name) {
  uint32_t mark = c->current_scope_mark;
  /* First pass: match at current scope mark (hygiene) */
  for (int i = (int)c->local_count - 1; i >= 0; i--) {
    if (c->locals[i].name == name && c->locals[i].scope_mark == mark) {
      return i;
    }
  }
  /* Fallback: if inside macro expansion, also try mark 0 (user-visible bindings
     like globals captured as locals or caller-scope bindings visible through
     hygienic macros). */
  if (mark != 0) {
    for (int i = (int)c->local_count - 1; i >= 0; i--) {
      if (c->locals[i].name == name && c->locals[i].scope_mark == 0) {
        return i;
      }
    }
  }
  return -1;
}

/* --- Internal: Global arity helpers --- */

GlobalArity* compiler__find_global(Compiler* c, JaclVal name) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  for (uint32_t i = 0; i < root->global_arity_count; i++) {
    if (root->global_arities[i].name == name) {
      return &root->global_arities[i];
    }
  }
  return NULL;
}

void compiler__set_global_arity(Compiler* c, JaclVal name, int16_t arity) {
  for (uint32_t i = 0; i < c->global_arity_count; i++) {
    if (c->global_arities[i].name == name) {
      c->global_arities[i].known_arity = arity;
      return;
    }
  }
  if (c->global_arity_count < COMPILER_GLOBAL_ARITIES_MAX) {
    GlobalArity* ga = &c->global_arities[c->global_arity_count];
    ga->name = name;
    ga->known_arity = arity;
    ga->is_mutable = false;
    ga->suspends = false;
    ga->captures_mutable = false;
    ga->prelude_is_native_fn = false;
    ga->type = TYPE_DYN;
    ga->return_type = TYPE_DYN;
    memset(ga->param_types, 0, sizeof(ga->param_types));
    c->global_arity_count++;
  }
}

/* Mark a global as having a native fn ref prelude value (for compile-time resolution) */
void compiler__set_global_prelude_native_fn(Compiler* c, JaclVal name, bool is_native_fn) {
  GlobalArity* ga = compiler__find_global(c, name);
  if (ga) {
    ga->prelude_is_native_fn = is_native_fn;
  }
}

/* --- Internal: Mark a top-level global's GlobalArity as mutable.
 *
 * Both destructure compilers, when binding mutable globals, need to walk to
 * the root compiler and flip the is_mutable flag on the matching arity entry.
 * Centralized here. */
static void compiler__mark_global_mutable(Compiler* c, JaclVal name) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  for (uint32_t j = 0; j < root->global_arity_count; j++) {
    if (root->global_arities[j].name == name) {
      root->global_arities[j].is_mutable = true;
      return;
    }
  }
}

/* --- Internal: Apply a destructure-pattern type annotation to the most
 * recently added local.
 *
 * Both destructure compilers walk the d_types/d_type_lens arrays per binding
 * and, if a type is given, resolve it and set local.type. Centralized so
 * the boilerplate doesn't repeat 5+ times. */
bool compiler__resolve_type(Compiler* c, const char* word, uint32_t len, JaclType* out);
static void compiler__apply_destructure_type(Compiler* c,
                                             const char** d_types,
                                             uint32_t* d_type_lens,
                                             uint32_t i) {
  if (!d_types || !d_types[i]) return;
  JaclType t;
  if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
    c->locals[c->local_count - 1].type = t;
  }
}

/* --- Internal: Struct type registry access --- */

StructTypeRegistry* compiler__get_struct_registry(Compiler* c) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  return root->struct_registry;
}

/* Resolve a (receiver, field-name) pair as part of a typed-pointer
 * dot-chain. Used by HEAD_DOT compile (both 2-arg read and 3-arg
 * set forms) and by the [addr] builtin to collapse arbitrarily-deep
 * `$p->inner->...` chains into a single combined-offset opcode.
 *
 * Returns true (with all out-params set) if the chain bottoms out in
 * a [Ptr Struct] base. *out_base is the leftmost expression that
 * produces the pointer; *out_offset is the cumulative byte offset
 * of the terminal field within the base's pointee; *out_term_t /
 * *out_term_sidx describe the terminal field's type.
 *
 * Returns false if `recv` doesn't reach a [Ptr Struct] base — e.g.,
 * recv is a struct value materialized from a vec-get, function
 * return, or inline local. Those cases route through the existing
 * struct-field compile paths instead. */
static bool compiler__resolve_ptr_chain_step(Compiler* c,
                                             AstNode* recv,
                                             const char* fname,
                                             uint32_t    fnlen,
                                             AstNode** out_base,
                                             uint32_t* out_offset,
                                             JaclType* out_term_t,
                                             uint32_t* out_term_sidx) {
  if (!recv) return false;
  StructTypeRegistry* reg = compiler__get_struct_registry(c);
  if (!reg) return false;

  JaclType recv_t = (JaclType)recv->inferred_type;
  uint32_t recv_sidx = recv->inferred_struct_idx;

  /* Direct case: receiver is a [Ptr Struct]. */
  if (recv_t == TYPE_PTR && recv_sidx != UINT32_MAX &&
      !JACL_IS_SCALAR_TYPE_IDX(recv_sidx) &&
      recv_sidx < reg->count) {
    StructTypeDef* sdef = reg->defs[recv_sidx];
    for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
      if (sdef->fields[fi].name_len == fnlen &&
          memcmp(sdef->fields[fi].name, fname, fnlen) == 0) {
        *out_base       = recv;
        *out_offset     = sdef->fields[fi].offset;
        *out_term_t     = sdef->fields[fi].type;
        *out_term_sidx  = sdef->fields[fi].struct_type_idx;
        return true;
      }
    }
    return false;
  }

  /* Chained case: receiver is itself a 2-arg dot whose chain ends
   * at an embedded struct field. Recurse, then add this field's
   * offset on top. */
  if (recv->type == AST_COMMAND &&
      recv->data.command.head_id == HEAD_DOT &&
      recv->data.command.arg_count == 2 &&
      recv->data.command.args[1]->type == AST_LIT_STRING) {
    AstNode* inner_recv  = recv->data.command.args[0];
    AstNode* inner_fld   = recv->data.command.args[1];
    AstNode* inner_base;
    uint32_t inner_offset;
    JaclType inner_term_t;
    uint32_t inner_term_sidx;
    if (compiler__resolve_ptr_chain_step(c, inner_recv,
                                         inner_fld->data.lit_string.value,
                                         inner_fld->data.lit_string.length,
                                         &inner_base, &inner_offset,
                                         &inner_term_t, &inner_term_sidx) &&
        inner_term_t == TYPE_STRUCT &&
        inner_term_sidx < reg->count) {
      StructTypeDef* sdef = reg->defs[inner_term_sidx];
      for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
        if (sdef->fields[fi].name_len == fnlen &&
            memcmp(sdef->fields[fi].name, fname, fnlen) == 0) {
          *out_base       = inner_base;
          *out_offset     = inner_offset + sdef->fields[fi].offset;
          *out_term_t     = sdef->fields[fi].type;
          *out_term_sidx  = sdef->fields[fi].struct_type_idx;
          return true;
        }
      }
    }
  }

  return false;
}

/* --- Internal: emit struct-aware return --- */

/* Phase 5b: emit OP_RETURN_WIDE if returning a value-type struct, else OP_RETURN.
 * If the top of stack is a heap struct (inline_repr != INLINE_STACK), emits
 * OP_STRUCT_EXPAND first to convert to inline slots. */
static void compiler__emit_return(Compiler* c, uint32_t line) {
  if (c->return_type == TYPE_STRUCT && c->return_struct_idx != UINT32_MAX) {
    StructTypeRegistry* reg = compiler__get_struct_registry(c);
    if (reg && c->return_struct_idx < reg->count) {
      StructTypeDef* sdef = reg->defs[c->return_struct_idx];
      if (struct_def_is_user(sdef, reg)) {
        uint32_t width = struct__slot_width(reg, c->return_struct_idx);
        /* Both INLINE_STACK and INLINE_REF mean inline bytes are on TOS;
           only INLINE_NONE needs OP_STRUCT_EXPAND from a heap pointer. */
        if (c->inline_repr == INLINE_NONE) {
          compiler__emit_byte(c, OP_STRUCT_EXPAND, line);
          compiler__emit_u16(c, (uint16_t)c->return_struct_idx, line);
        }
        compiler__emit_byte(c, OP_RETURN_WIDE, line);
        compiler__emit_byte(c, (uint8_t)width, line);
        return;
      }
    }
  }
  compiler__emit_byte(c, OP_RETURN, line);
}

CtxFieldList* compiler__get_ctx_fields(Compiler* c) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  return root->ctx_fields;
}

/* Resolve a type annotation string to a JaclType.
   Handles built-in types and named struct types.
   Returns true if resolved, false if unknown type. */
bool compiler__resolve_type(Compiler* c, const char* word, uint32_t len,
                                    JaclType* out_type) {
  if (is_type_keyword(word, len)) {
    *out_type = type_from_keyword(word, len);
    return true;
  }
  /* Check for inline struct type string */
  if (len > 7 && memcmp(word, "struct{", 7) == 0) {
    *out_type = TYPE_STRUCT;
    return true;
  }
  /* Check struct registry */
  StructTypeRegistry* reg = compiler__get_struct_registry(c);
  if (reg && struct_registry__find(reg, word, len) != UINT32_MAX) {
    *out_type = TYPE_STRUCT;
    return true;
  }
  return false;
}

/* Check if a string is a valid type annotation (built-in or struct name) */
bool compiler__is_type_annotation(Compiler* c, const char* word, uint32_t len) {
  JaclType dummy;
  return compiler__resolve_type(c, word, len, &dummy);
}

/* --- Internal: Upvalue resolution --- */

int compiler__add_upvalue(Compiler* c, uint8_t index, uint8_t is_local,
                                  JaclVal name) {
  /* Check if this upvalue already exists */
  for (uint32_t i = 0; i < c->upvalue_count; i++) {
    if (c->upvalues[i].index == index &&
        c->upvalues[i].is_local == is_local) {
      return (int)i;
    }
  }
  if (c->upvalue_count >= COMPILER_UPVALUES_MAX) {
    return -1;
  }
  /* Compute base_slot: sum of all prior upvalue widths */
  uint16_t base = 0;
  for (uint32_t i = 0; i < c->upvalue_count; i++) {
    base += c->upvalues[i].width;
  }
  c->upvalues[c->upvalue_count].index      = index;
  c->upvalues[c->upvalue_count].is_local   = is_local;
  c->upvalues[c->upvalue_count].name       = name;
  c->upvalues[c->upvalue_count].is_mutable = false;
  c->upvalues[c->upvalue_count].suspends   = false;
  c->upvalues[c->upvalue_count].captures_mutable = false;
  c->upvalues[c->upvalue_count].type       = TYPE_DYN;
  c->upvalues[c->upvalue_count].scope_mark = c->current_scope_mark;
  c->upvalues[c->upvalue_count].width      = 1;
  c->upvalues[c->upvalue_count].is_inline  = false;
  c->upvalues[c->upvalue_count].base_slot  = base;
  return (int)c->upvalue_count++;
}

int compiler__resolve_upvalue(Compiler* c, JaclVal name,
                              uint32_t line, uint32_t col) {
  if (!c->enclosing) return -1;

  /* Propagate our current scope mark into the enclosing lookup so that
     hygienic identifier matching uses the reference site's mark rather
     than whatever the enclosing compiler happens to be processing. */
  uint32_t enc_saved_mark = c->enclosing->current_scope_mark;
  c->enclosing->current_scope_mark = c->current_scope_mark;

  /* Check if the variable is a local in the enclosing scope */
  int local = compiler__resolve_local(c->enclosing, name);
  if (local != -1) {
    c->enclosing->current_scope_mark = enc_saved_mark;
    /* Reject bare struct capture — closures store JaclVals (dyn boundary) */
    if (c->enclosing->locals[local].type == TYPE_STRUCT) {
      char err_msg[192];
      char nbuf[128];
      uint32_t nlen = jacl_string_byte_len(name);
      jacl_string_data(name, nbuf, sizeof(nbuf) - 1);
      nbuf[nlen < sizeof(nbuf) ? nlen : sizeof(nbuf) - 1] = '\0';
      snprintf(err_msg, sizeof(err_msg),
               "cannot capture bare struct '%s' in closure; "
               "use [box ...] to box it first", nbuf);
      compiler__error(c, line, col, err_msg);
      return -1;
    }
    int uv = compiler__add_upvalue(c, (uint8_t)local, 1, name);
    if (uv != -1) {
      if (c->enclosing->locals[local].is_mutable)
        c->upvalues[uv].is_mutable = true;
      c->upvalues[uv].captures_mutable = c->enclosing->locals[local].captures_mutable;
      c->upvalues[uv].suspends = c->enclosing->locals[local].suspends;
      TYPEINFO_SAVE(c->upvalues[uv], TYPEINFO_LOAD(c->enclosing->locals[local]));
      c->upvalues[uv].scope_mark = c->enclosing->locals[local].scope_mark;
      /* US-008: propagate inline struct info from enclosing local */
      if (c->enclosing->locals[local].is_inline) {
        c->upvalues[uv].width = c->enclosing->locals[local].width;
        c->upvalues[uv].is_inline = true;
      }
    }
    return uv;
  }

  /* Check if the variable is an SM state field in the enclosing scope.
     is_local=2 tells the VM to read from the SM object's fields array
     at closure creation time, so nested closures can capture SM variables. */
  if (c->enclosing->sm_analysis) {
    int field_idx = sm__find_field(&c->enclosing->sm_analysis->state_layout, name);
    if (field_idx >= 0) {
      c->enclosing->current_scope_mark = enc_saved_mark;
      /* is_local=2: copy SM field value into closure upvalue at creation
         time.  For mutable fields the SM field holds a cell, so the
         closure gets the shared cell pointer (FR-5 semantics). */
      bool is_mut = sm__is_field_mutable(&c->enclosing->sm_analysis->state_layout, name);
      int uv = compiler__add_upvalue(c, (uint8_t)field_idx, 2, name);
      if (uv != -1) {
        c->upvalues[uv].is_mutable = is_mut;
        c->upvalues[uv].captures_mutable = is_mut;
        c->upvalues[uv].type = TYPE_DYN;
      }
      return uv;
    }
  }

  /* Check if it's an upvalue in the enclosing scope (transitive capture) */
  int upvalue = compiler__resolve_upvalue(c->enclosing, name, line, col);
  if (upvalue != -1) {
    c->enclosing->current_scope_mark = enc_saved_mark;
    /* Reject transitive struct capture */
    if (c->enclosing->upvalues[upvalue].type == TYPE_STRUCT) {
      char err_msg[192];
      char nbuf[128];
      uint32_t nlen = jacl_string_byte_len(name);
      jacl_string_data(name, nbuf, sizeof(nbuf) - 1);
      nbuf[nlen < sizeof(nbuf) ? nlen : sizeof(nbuf) - 1] = '\0';
      snprintf(err_msg, sizeof(err_msg),
               "cannot capture bare struct '%s' in closure; "
               "use [box ...] to box it first", nbuf);
      compiler__error(c, line, col, err_msg);
      return -1;
    }
    /* US-008: for transitive capture, use base_slot as the index so the VM
       can locate wide upvalues in the parent's upvalue array correctly. */
    uint8_t uv_idx_for_vm = c->enclosing->upvalues[upvalue].is_inline
        ? (uint8_t)c->enclosing->upvalues[upvalue].base_slot
        : (uint8_t)upvalue;
    int uv = compiler__add_upvalue(c, uv_idx_for_vm, 0, name);
    if (uv != -1) {
      if (c->enclosing->upvalues[upvalue].is_mutable)
        c->upvalues[uv].is_mutable = true;
      c->upvalues[uv].captures_mutable = c->enclosing->upvalues[upvalue].captures_mutable;
      c->upvalues[uv].suspends = c->enclosing->upvalues[upvalue].suspends;
      TYPEINFO_SAVE(c->upvalues[uv], TYPEINFO_LOAD(c->enclosing->upvalues[upvalue]));
      c->upvalues[uv].scope_mark = c->enclosing->upvalues[upvalue].scope_mark;
      /* US-008: propagate inline struct info from parent upvalue */
      if (c->enclosing->upvalues[upvalue].is_inline) {
        c->upvalues[uv].width = c->enclosing->upvalues[upvalue].width;
        c->upvalues[uv].is_inline = true;
      }
    }
    return uv;
  }

  c->enclosing->current_scope_mark = enc_saved_mark;
  return -1;
}

/**
 * Collect all locally declared names (def + mut) in an AST body.
 * Used by compiler__body_captures_mutable to distinguish local vs captured vars.
 * Skips nested proc/spawn/parallel/race scopes (they are separate bodies).
 */
#define AST_LOCAL_NAMES_MAX 128
typedef struct {
  JaclVal*         names;
  uint32_t*        count;
  ThreadHeap*      heap;
  JaclInternTable* intern_table;
} CollectNamesCtx;

static void ast__collect_local_names__visit(AstNode* node, void* vctx) {
  CollectNamesCtx* ctx = (CollectNamesCtx*)vctx;
  if (!node || *ctx->count >= AST_LOCAL_NAMES_MAX) return;

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING) {
      HeadId hid = (HeadId)node->data.command.head_id;
      if (hid == HEAD_DEF || hid == HEAD_MUT) {
        uint32_t argc = node->data.command.arg_count;
        if (argc >= 2 && node->data.command.args[0]->type == AST_LIT_STRING) {
          AstNode* name_node = node->data.command.args[0];
          ctx->names[*ctx->count] = compiler__name_val(
              ctx->heap, ctx->intern_table,
              name_node->data.lit_string.value,
              name_node->data.lit_string.length);
          (*ctx->count)++;
        }
        return;
      }
      if (hid == HEAD_PROC || hid == HEAD_SPAWN ||
          hid == HEAD_PARALLEL || hid == HEAD_RACE) return;
    }
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      ast__collect_local_names__visit(node->data.command.args[i], ctx);
    }
    return;
  }
  ast__walk_children(node, ast__collect_local_names__visit, ctx);
}

void ast__collect_local_names(AstNode* node, JaclVal* names,
                                      uint32_t* count,
                                      ThreadHeap* heap,
                                      JaclInternTable* intern_table) {
  CollectNamesCtx ctx = { names, count, heap, intern_table };
  ast__collect_local_names__visit(node, &ctx);
}

/**
 * Check if an AST body contains $var references to mutable bindings
 * from an enclosing compiler scope, or references to closures that
 * transitively capture mutable state (US-003).
 *
 * Conservative: skips spawn/parallel/race scopes (they get own pinning)
 * but does NOT skip nested proc scopes (captures propagate upward).
 * May have false positives from proc-local parameter shadowing.
 */

/* Helper: check if a name resolves to a binding with is_mutable or
   captures_mutable in the enclosing scope chain. */
bool compiler__name_touches_mutable(Compiler* enclosing, JaclVal name) {
  for (Compiler* cc = enclosing; cc; cc = cc->enclosing) {
    int slot = compiler__resolve_local(cc, name);
    if (slot != -1)
      return cc->locals[slot].is_mutable || cc->locals[slot].captures_mutable;
    for (uint32_t i = 0; i < cc->upvalue_count; i++) {
      if (cc->upvalues[i].name == name)
        return cc->upvalues[i].is_mutable || cc->upvalues[i].captures_mutable;
    }
  }
  /* Check global arities */
  GlobalArity* ga = compiler__find_global(enclosing, name);
  if (ga) return ga->is_mutable || ga->captures_mutable;
  return false;
}

typedef struct {
  JaclVal*  local_names;
  uint32_t  local_name_count;
  Compiler* enclosing;
} NonlocalMutCtx;

static bool ast__refs_nonlocal_mutable__pred(AstNode* node, void* vctx) {
  if (!node) return false;
  NonlocalMutCtx* ctx = (NonlocalMutCtx*)vctx;

  if (node->type == AST_VAR_REF) {
    uint32_t len = node->data.var_ref.length;
    if (len > 128) return false;
    JaclVal name = compiler__name_val(ctx->enclosing->heap,
                                       ctx->enclosing->intern_table,
                                       node->data.var_ref.name, len);
    for (uint32_t i = 0; i < ctx->local_name_count; i++) {
      if (ctx->local_names[i] == name) return false;
    }
    return compiler__name_touches_mutable(ctx->enclosing, name);
  }

  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING) {
      HeadId hid = (HeadId)node->data.command.head_id;
      /* Skip nested concurrent scopes — they get their own pinning */
      if (hid == HEAD_SPAWN || hid == HEAD_PARALLEL || hid == HEAD_RACE)
        return false;
      /* Check if function call target is a non-local closure that
         transitively captures mutable state (US-003). */
      const char* hname = head->data.lit_string.value;
      uint32_t hlen = head->data.lit_string.length;
      if (hlen <= 128) {
        JaclVal fname = compiler__name_val(ctx->enclosing->heap,
                                            ctx->enclosing->intern_table,
                                            hname, hlen);
        bool is_local_name = false;
        for (uint32_t i = 0; i < ctx->local_name_count; i++) {
          if (ctx->local_names[i] == fname) { is_local_name = true; break; }
        }
        if (!is_local_name &&
            compiler__name_touches_mutable(ctx->enclosing, fname))
          return true;
      }
    }
    if (ast__refs_nonlocal_mutable__pred(head, ctx)) return true;
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      if (ast__refs_nonlocal_mutable__pred(node->data.command.args[i], ctx))
        return true;
    }
    return false;
  }
  return ast__any_child(node, ast__refs_nonlocal_mutable__pred, ctx);
}

bool ast__refs_nonlocal_mutable_impl(AstNode* node,
                                             JaclVal* local_names,
                                             uint32_t local_name_count,
                                             Compiler* enclosing) {
  NonlocalMutCtx ctx = { local_names, local_name_count, enclosing };
  return ast__refs_nonlocal_mutable__pred(node, &ctx);
}

/**
 * Check if a concurrent body captures a mutable (mut/box) binding from
 * an enclosing scope via $var references.
 *
 * Used alongside ast__contains_nonlocal_set() to decide pinning.
 * If either check is true, the body is pinned to thread 0.
 */
bool compiler__body_captures_mutable(Compiler* enclosing,
                                             AstNode* body_block) {
  JaclVal local_names[AST_LOCAL_NAMES_MAX];
  uint32_t local_name_count = 0;
  ast__collect_local_names(body_block, local_names, &local_name_count,
                           enclosing->heap, enclosing->intern_table);
  return ast__refs_nonlocal_mutable_impl(body_block, local_names,
                                          local_name_count, enclosing);
}

/* Forward declaration for compile_block_expr */
void compiler__compile_node(Compiler* c, AstNode* node);

/* --- Internal: Jump patching helpers --- */

uint32_t compiler__emit_jump(Compiler* c, uint8_t instruction,
                                     uint32_t line) {
  compiler__emit_byte(c, instruction, line);
  compiler__emit_byte(c, 0xFF, line);  /* placeholder high byte */
  compiler__emit_byte(c, 0xFF, line);  /* placeholder low byte */
  return c->chunk->code_count - 2;
}

void compiler__patch_jump(Compiler* c, uint32_t offset) {
  uint32_t jump = c->chunk->code_count - offset - 2;
  c->chunk->code[offset]     = (uint8_t)((jump >> 8) & 0xFF);
  c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

/* --- Internal: Emit OP_CHECK_ERROR with offset 0 (return from frame) --- */

void compiler__emit_check_error(Compiler* c, uint32_t line) {
  compiler__emit_byte(c, OP_CHECK_ERROR, line);
  if (c->in_try_body) {
    /* Record position for later patching to jump to try handler */
    if (c->try_patch_count < COMPILER_TRY_PATCHES_MAX) {
      c->try_patches[c->try_patch_count++] = c->chunk->code_count;
    }
    compiler__emit_u16(c, 0xFFFF, line);  /* placeholder */
  } else {
    compiler__emit_u16(c, 0, line);
  }
}

/* --- Internal: State machine dispatch table (US-006) --- */

/**
 * Emit the entry dispatch table for a state machine function.
 *
 * On entry, the SM function receives the state object in slot 0.
 * This function emits chained conditional jumps that compare
 * state->resume_point against each known resume point ID (1..N).
 * Case 0 (initial entry) falls through to the function body start.
 *
 * The jump targets are forward-jump placeholders that must be backpatched
 * later during body compilation when the compiler reaches each suspension
 * point. The patch offsets are stored in c->sm_dispatch.label_patches[].
 *
 * @param c                Compiler for the SM function body
 * @param suspension_count Number of suspension points (from SuspensionAnalysis)
 * @param line             Source line for debug info
 */
void compiler__emit_sm_dispatch_table(Compiler* c,
                                              uint32_t suspension_count,
                                              uint32_t line) {
  if (suspension_count == 0) return;

  c->sm_dispatch.label_count = suspension_count;

  /* For each resume point 1..N, emit:
   *   OP_GET_RESUME_POINT        ; push state->resume_point as i32
   *   OP_CONST <resume_id>       ; push integer constant (resume_id = sp_index + 1)
   *   OP_EQ                      ; compare (pops both, pushes bool)
   *   OP_JUMP_IF_FALSE <skip>    ; if not equal, skip to next check
   *   OP_JUMP <label_N>          ; forward jump to resume point (backpatched later)
   * skip:
   *
   * Case 0 (resume_point == 0) falls through to function body start.
   */
  for (uint32_t i = 0; i < suspension_count; i++) {
    uint32_t resume_id = i + 1;  /* resume_point 0 = initial entry, 1..N = after yields */

    /* Load resume_point from state object */
    compiler__emit_byte(c, OP_GET_RESUME_POINT, line);

    /* Push the resume ID constant */
    compiler__emit_constant(c, jacl_i32((int32_t)resume_id), line);

    /* Compare */
    compiler__emit_byte(c, OP_EQ, line);

    /* Skip to next check if not equal */
    uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Restore vm->ctx from __ctx state field before jumping to resume label */
    if (c->sm_analysis && c->sm_analysis->ctx_field_idx != UINT32_MAX) {
      compiler__emit_byte(c, OP_GET_STATE_FIELD, line);
      compiler__emit_byte(c, (uint8_t)c->sm_analysis->ctx_field_idx, line);
      compiler__emit_byte(c, OP_SET_CTX, line);
    }

    /* Forward jump to resume label (to be backpatched) */
    c->sm_dispatch.label_patches[i] = compiler__emit_jump(c, OP_JUMP, line);

    /* Patch the skip jump to land here (next check) */
    compiler__patch_jump(c, skip_jump);
  }

  /* Fall through: resume_point == 0, begin function body from the start */
}

/* --- Internal: State machine body compilation (US-007) --- */

/* Forward declarations needed by SM compilation */
void compiler__compile_node(Compiler* c, AstNode* node);
void compiler__emit_check_error(Compiler* c, uint32_t line);
int  compiler__head_matches(AstNode* head, const char* name, uint32_t len);

/**
 * Compile a generator body as a state machine function.
 *
 * The SM function receives (state_obj, resume_value) in frame slots 0 and 1.
 * All user locals and parameters are stored in the state object fields,
 * accessed via OP_GET_STATE_FIELD/OP_SET_STATE_FIELD (handled by SM-aware
 * AST_VAR_REF/def/mut/set handlers).
 *
 * Body compilation:
 *   1. Emit dispatch table (jumps to resume points 1..N)
 *   2. Compile body statements via compile_node — yield handler emits
 *      OP_YIELD_SM with resume_point update and dispatch label backpatching
 *   3. After all statements: emit OP_NIL + OP_RETURN (generator exhausted)
 */
/* Generator tail-position check.
 *
 * In a proc with `yield`, the proc's return value is silently discarded by
 * stream consumers. The narrow check at the `return X` sites catches explicit
 * returns; this walk catches an implicit value-producing tail expression.
 *
 * Returns NULL when the tail recurses to something in the nil-producing
 * whitelist; otherwise returns the offending AST node for error reporting.
 *
 * Whitelist (leaves): yield, for, while, def/mut/set, =/:/::, return,
 * break/continue, declarations (proc, defstruct, defmacro), one-armed if.
 * Recursive descent: blocks, two-armed if/else, try/catch (body + handler),
 * with-ctx (body). Blocks with trailing `;` (or empty blocks) are nil. */
static AstNode* compiler__find_disallowed_generator_tail(AstNode* node) {
  if (!node) return NULL;

  switch (node->type) {
    case AST_BLOCK: {
      uint32_t count = node->data.block.count;
      if (count == 0 || node->data.block.trailing_semi) return NULL;
      return compiler__find_disallowed_generator_tail(
          node->data.block.commands[count - 1]);
    }

    case AST_RETURN:
    case AST_BREAK:
    case AST_CONTINUE:
    case AST_DEFSTRUCT:
    case AST_DEFMACRO:
    case AST_USE:
    case AST_CTX_DECL:
      return NULL;

    case AST_COMMAND: {
      AstNode** args = node->data.command.args;
      uint32_t argc = node->data.command.arg_count;
      HeadId hid = (HeadId)node->data.command.head_id;
      switch (hid) {
        case HEAD_YIELD:
        case HEAD_FOR:
        case HEAD_WHILE:
        case HEAD_DEF:
        case HEAD_MUT:
        case HEAD_SET:
        case HEAD_EQUALS:
        case HEAD_COLON:
        case HEAD_COLON_COLON:
        case HEAD_RETURN:
        case HEAD_BREAK:
        case HEAD_CONTINUE:
        case HEAD_PROC:
        case HEAD_DEFSTRUCT:
        case HEAD_DEFMACRO:
          return NULL;

        case HEAD_IF:
          /* One-armed if is nil when the condition is false; treat as a leaf
             even though the then-arm's value is dropped when true. Two-armed
             requires both branches to be tail-safe. */
          if (argc < 3) return NULL;
          {
            AstNode* in_then = compiler__find_disallowed_generator_tail(args[1]);
            if (in_then) return in_then;
            return compiler__find_disallowed_generator_tail(args[2]);
          }

        case HEAD_TRY:
          /* [try { body } name { handler }] */
          if (argc != 3) return node;
          {
            AstNode* in_body = compiler__find_disallowed_generator_tail(args[0]);
            if (in_body) return in_body;
            return compiler__find_disallowed_generator_tail(args[2]);
          }

        case HEAD_WITH_CTX:
          /* [with-ctx { overrides } { body }] */
          if (argc != 2) return node;
          return compiler__find_disallowed_generator_tail(args[1]);

        default:
          return node;
      }
    }

    default:
      return node;
  }
}

/**
 * Compile a statement list in state machine mode.
 * When return_last_value is true, the last statement's result is kept on the
 * stack as the return value (for async functions, spawn/parallel bodies).
 * When false, all statement results are consumed by check_error and nil is
 * returned (generator exhaustion pattern).
 */
void compiler__compile_sm_stmts(Compiler* c, AstNode** stmts,
                                        uint32_t count, uint32_t line,
                                        bool return_last_value) {
  SuspensionAnalysis* analysis = c->sm_analysis;
  if (!analysis) {
    /* No analysis at all — shouldn't be called, but handle gracefully */
    compiler__emit_byte(c, OP_NIL, line);
    compiler__emit_byte(c, OP_RETURN, line);
    return;
  }

  /* Emit dispatch table only when there are direct suspension points */
  if (analysis->suspension_count > 0) {
    compiler__emit_sm_dispatch_table(c, analysis->suspension_count, line);
  }

  /* On initial entry (resume_point == 0, falls through dispatch table):
     save vm->ctx to the __ctx state field for later resume restore. */
  if (analysis->ctx_field_idx != UINT32_MAX) {
    compiler__emit_byte(c, OP_GET_CTX, line);
    compiler__emit_byte(c, OP_SET_STATE_FIELD, line);
    compiler__emit_byte(c, (uint8_t)analysis->ctx_field_idx, line);
  }

  /* Initialize suspension point counter for yield handler */
  c->sm_suspension_idx = 0;

  /* Compile body statements — the SM-aware yield handler (in compile_command)
     emits OP_YIELD_SM with resume_point updates and dispatch label backpatching.
     Variable accesses are redirected to state fields by the SM-aware handlers. */
  if (return_last_value && count > 0) {
    for (uint32_t i = 0; i + 1 < count; i++) {
      compiler__compile_node(c, stmts[i]);
      compiler__emit_check_error(c, line);
    }
    /* Last statement: keep result on stack */
    compiler__compile_node(c, stmts[count - 1]);
  } else {
    for (uint32_t i = 0; i < count; i++) {
      compiler__compile_node(c, stmts[i]);
      compiler__emit_check_error(c, line);
    }
    /* Generator exhausted: return nil */
    compiler__emit_byte(c, OP_NIL, line);
  }
  compiler__emit_byte(c, OP_RETURN, line);
}

void compiler__compile_sm_body(Compiler* c, AstNode* body_block,
                                       uint32_t line) {
  uint32_t stmt_count = body_block->data.block.count;
  AstNode** stmts = body_block->data.block.commands;
  compiler__compile_sm_stmts(c, stmts, stmt_count, line, false);
}

/* Forward declarations */
void compiler__compile_node(Compiler* c, AstNode* node);
int  compiler__head_matches(AstNode* head, const char* name, uint32_t len);
void compiler__emit_check_error(Compiler* c, uint32_t line);
void compiler__compile_command(Compiler* c, AstNode* node);
void compiler__compile_block_expr(Compiler* c, AstNode* block_node);

/**
 * Compile a parallel/race body block as a closure.
 * Each body becomes a zero-arg closure (non-suspending) or an SM closure
 * (suspending, with __sm/__rv params and state machine compilation).
 * Pushes the closure onto the stack.
 */
void compiler__compile_parallel_body(Compiler* c, AstNode* body_block,
                                             uint32_t line, uint32_t col) {
  if (body_block->type != AST_BLOCK) {
    compiler__error(c, line, col, "parallel body must be a block");
    return;
  }

  uint32_t stmt_count = body_block->data.block.count;
  AstNode** stmts = body_block->data.block.commands;

  /* Check if the body contains suspension points */
  bool body_suspends = ast__contains_suspension(body_block, c->suspension_map,
                                                c->heap, c->intern_table);

  /* Allocate anonymous closure for the parallel body */
  JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
  chunk_init(&closure->chunk, c->arena);
  closure->name         = "<parallel>";
  closure->upvalue_count = 0;
  closure->upvalue_total_slots = 0;
  closure->upvalues     = NULL;
  closure->param_names  = NULL;
  closure->min_args     = 0;
  closure->variadic     = false;
  closure->pin_worker_id = -1;

  /* Pin this body to thread 0 if it mutates non-local variables
     OR captures a mutable (mut/box) binding from an enclosing scope. */
  bool needs_pinning = ast__contains_nonlocal_set(body_block,
                                                   c->heap, c->intern_table)
                    || compiler__body_captures_mutable(c, body_block);
  closure->pinned = needs_pinning;

  SuspensionAnalysis sm_analysis_data;
  memset(&sm_analysis_data, 0, sizeof(sm_analysis_data));

  if (body_suspends) {
    /* SM parallel body: analyze suspensions, compile as state machine */
    sm_analysis_data = compiler__analyze_suspensions(
        body_block, NULL, 0, true, c->suspension_map, c->heap, c->intern_table,
        compiler__get_struct_registry(c));
    closure->param_count = 2;
    closure->param_total_slots = 2;
    JaclVal* pnames = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal) * 2);
    pnames[0] = jacl_inline_string("__sm", 4);
    pnames[1] = jacl_inline_string("__rv", 4);
    closure->param_names = pnames;
    closure->sm_field_count = (uint8_t)sm_analysis_data.state_layout.total_slots;
    closure->is_sm_compiled = true;
  } else {
    closure->param_count = 0;
    closure->param_total_slots = 0;
  }

  /* Create body compiler */
  Compiler body_compiler;
  compiler__init(&body_compiler, &closure->chunk, c->arena, c->intern_table, c->heap);
  body_compiler.scope_depth    = 1;
  body_compiler.enclosing      = c;
  body_compiler.suspension_map = c->suspension_map;
  body_compiler.pin_all_closures = needs_pinning;
  body_compiler.current_scope_mark = c->current_scope_mark;

  /* Copy global arities for suspension lookups */
  {
    Compiler* root = c;
    while (root->enclosing) root = root->enclosing;
    memcpy(body_compiler.global_arities, root->global_arities,
           sizeof(GlobalArity) * root->global_arity_count);
    body_compiler.global_arity_count = root->global_arity_count;
  }

  if (body_suspends) {
    /* SM body: add internal params as locals, compile via SM */
    compiler__add_local(&body_compiler, jacl_inline_string("__sm", 4), line, col);
    body_compiler.locals[body_compiler.local_count - 1].is_param = true;
    compiler__add_local(&body_compiler, jacl_inline_string("__rv", 4), line, col);
    body_compiler.locals[body_compiler.local_count - 1].is_param = true;
    body_compiler.in_concurrent_body = true;

    SuspensionAnalysis* analysis_ptr =
        (SuspensionAnalysis*)arena_alloc(c->arena, sizeof(SuspensionAnalysis));
    *analysis_ptr = sm_analysis_data;
    body_compiler.sm_analysis = analysis_ptr;

    compiler__compile_sm_stmts(&body_compiler, stmts, stmt_count, line, true);
  } else {
    /* Non-suspending body: compile as block expression */
    body_compiler.in_concurrent_body = true;
    compiler__compile_block_expr(&body_compiler, body_block);
    compiler__emit_byte(&body_compiler, OP_RETURN, line);
  }

  /* Propagate errors */
  c->error_count += body_compiler.error_count;
  if (!c->first_error && body_compiler.first_error) {
    c->first_error = body_compiler.first_error;
  }

  closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;
  /* US-008: compute upvalue_total_slots */
  {
    uint16_t total = 0;
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++)
      total += body_compiler.upvalues[i].width;
    closure->upvalue_total_slots = total;
  }

  /* Emit OP_CLOSURE + upvalue descriptors */
  uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(closure));
  compiler__emit_byte(c, OP_CLOSURE, line);
  compiler__emit_u16(c, closure_idx, line);
  for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
    compiler__emit_byte(c, body_compiler.upvalues[i].is_local, line);
    compiler__emit_byte(c, body_compiler.upvalues[i].index, line);
    compiler__emit_byte(c, (uint8_t)body_compiler.upvalues[i].width, line);
  }
}

/* All suspension is state-machine compiled (see US-007..US-019). */

/* --- Internal: Compile block as expression (last stmt value stays on stack) --- */

void compiler__compile_block_expr(Compiler* c, AstNode* block_node) {
  uint32_t line  = block_node->start.line;
  uint32_t count = block_node->data.block.count;
  uint32_t scope_start_locals = c->local_count;

  compiler__begin_scope(c);

  bool trailing_semi = block_node->data.block.trailing_semi;

  if (count == 0 || trailing_semi) {
    /* All commands run for side effects; block evaluates to nil */
    for (uint32_t i = 0; i < count; i++) {
      compiler__compile_node(c, block_node->data.block.commands[i]);
      compiler__emit_check_error(c, line);
    }
    compiler__end_scope(c, line);
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  for (uint32_t i = 0; i < count - 1; i++) {
    compiler__compile_node(c, block_node->data.block.commands[i]);
    compiler__emit_check_error(c, line);
  }
  /* For the last statement, apply return type context if declared */
  if (c->return_type != TYPE_DYN) {
  }
  compiler__compile_node(c, block_node->data.block.commands[count - 1]);

  /* Clean up locals while preserving the result on the stack top */
  uint32_t pop_count = c->local_count - scope_start_locals;
  c->scope_depth--;
  c->local_count = scope_start_locals;

  if (pop_count > 0) {
    /* Result is on top, locals are below it. Save result into the first
       local's slot, then POP_N removes the rest plus the old top copy. */
    compiler__emit_byte(c, OP_SET_LOCAL, line);
    compiler__emit_byte(c, (uint8_t)scope_start_locals, line);
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)pop_count, line);
  }
}

/* --- Internal: Command head matching --- */

int compiler__head_matches(AstNode* head, const char* name, uint32_t len) {
  return head->type == AST_LIT_STRING &&
         head->data.lit_string.length == len &&
         memcmp(head->data.lit_string.value, name, len) == 0;
}

/* --- Internal: Determine known arity of an AST expression --- */

int16_t compiler__node_known_arity(Compiler* c, AstNode* node) {
  if (node->type == AST_VAR_REF) {
    uint32_t name_len = node->data.var_ref.length;
    if (name_len <= 128) {
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, node->data.var_ref.name, name_len);
      int slot = compiler__resolve_local(c, name_val);
      if (slot != -1) {
        return c->locals[slot].known_arity;
      }
      GlobalArity* ga = compiler__find_global(c, name_val);
      return ga ? ga->known_arity : -1;
    }
  }
  if (node->type == AST_LIT_STRING) {
    uint32_t name_len = node->data.lit_string.length;
    if (name_len <= 128) {
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, node->data.lit_string.value, name_len);
      GlobalArity* ga = compiler__find_global(c, name_val);
      return ga ? ga->known_arity : -1;
    }
  }
  return -1;
}

/* --- Internal: Builtin arity error helper --- */

void compiler__builtin_arity_error(Compiler* c, uint32_t line,
                                           uint32_t col, const char* name,
                                           const char* expected_desc,
                                           uint32_t got) {
  char err_msg[128];
  snprintf(err_msg, sizeof(err_msg),
           "builtin '%s' expects %s but got %d",
           name, expected_desc, (int)got);
  compiler__error(c, line, col, err_msg);
}

/* --- Internal: Auto-box unboxed types (emit OP_TO_DYN if needed) --- */

void compiler__ensure_boxed(Compiler* c, uint32_t line) {
  /* State-dependent: reads c->last_expr_type because the just-emitted
   * code may have already unboxed (e.g. mutable cell loads emit
   * OP_GET_CELL_LOCAL + OP_TO_DYN, leaving last_expr_type=DYN). The
   * AST node's inferred_type is the binding's *declared* type, which
   * doesn't reflect runtime stack representation after such a load. */
  if (is_unboxed_type(c->last_expr_type)) {
    compiler__emit_byte(c, OP_TO_DYN, line);
    compiler__emit_byte(c, (uint8_t)c->last_expr_type, line);
    c->last_expr_type = TYPE_DYN;
  }
}

/* --- Internal: Compile a vec-* receiver and reject TYPE_STREAM operands.
 *
 * All vec-* builtins share the same preamble: compile arg[0], then refuse
 * to operate on a stream (the user must `collect` first). Centralizes the
 * boilerplate; returns true on success, false if an error was reported. */

static bool compiler__compile_vec_receiver(Compiler* c, AstNode* node,
                                           const char* opname,
                                           uint32_t line, uint32_t col) {
  compiler__compile_node(c, node);
  if ((JaclType)node->inferred_type == TYPE_STREAM) {
    char msg[128];
    snprintf(msg, sizeof(msg),
             "%s requires a vector; got stream (use collect to materialize)",
             opname);
    compiler__error(c, line, col, msg);
    return false;
  }
  return true;
}

/* --- Internal: Reject bare struct in dyn context (compile-time error) --- */

static bool compiler__reject_bare_typed(Compiler* c, AstNode* node,
                                        uint32_t line, uint32_t col,
                                        const char* context) {
  JaclType t = (JaclType)node->inferred_type;
  if (t == TYPE_STRUCT || is_typed_collection(t)) {
    char err_msg[128];
    snprintf(err_msg, sizeof(err_msg),
             "cannot store bare %s in %s; use [box ...] to box it",
             type_name(t), context);
    compiler__error(c, line, col, err_msg);
    return true;
  }
  return false;
}

/* --- Internal: Map dynamic opcode to typed opcode for a given type --- */

uint8_t compiler__typed_op(uint8_t dyn_op, JaclType type) {
  if (type == TYPE_I64) {
    switch (dyn_op) {
      case OP_ADD: return OP_ADD_I64;
      case OP_SUB: return OP_SUB_I64;
      case OP_MUL: return OP_MUL_I64;
      case OP_DIV: return OP_DIV_I64;
      case OP_MOD: return OP_MOD_I64;
      case OP_LT:  return OP_LT_I64;
      case OP_GT:  return OP_GT_I64;
      case OP_LE:  return OP_LE_I64;
      case OP_GE:  return OP_GE_I64;
      case OP_EQ:  return OP_EQ_I64;
      default: return dyn_op;
    }
  }
  if (type == TYPE_U64) {
    switch (dyn_op) {
      case OP_ADD: return OP_ADD_I64;  /* u64 reuses i64 add */
      case OP_SUB: return OP_SUB_I64;
      case OP_MUL: return OP_MUL_I64;
      case OP_DIV: return OP_DIV_U64;
      case OP_MOD: return OP_MOD_U64;
      case OP_LT:  return OP_LT_U64;
      case OP_GT:  return OP_GT_U64;
      case OP_LE:  return OP_LE_U64;
      case OP_GE:  return OP_GE_U64;
      case OP_EQ:  return OP_EQ_I64;  /* u64 reuses i64 eq */
      default: return dyn_op;
    }
  }
  if (type == TYPE_F64) {
    switch (dyn_op) {
      case OP_ADD: return OP_ADD_F64;
      case OP_SUB: return OP_SUB_F64;
      case OP_MUL: return OP_MUL_F64;
      case OP_DIV: return OP_DIV_F64;
      case OP_MOD: return OP_MOD_F64;
      case OP_LT:  return OP_LT_F64;
      case OP_GT:  return OP_GT_F64;
      case OP_LE:  return OP_LE_F64;
      case OP_GE:  return OP_GE_F64;
      case OP_EQ:  return OP_EQ_F64;
      default: return dyn_op;
    }
  }
  return dyn_op;
}

/* --- Internal: Compile a typed binary operation --- */

void compiler__compile_binary(Compiler* c, AstNode** args,
                                     uint8_t op, const char* op_verb,
                                     uint32_t line, uint32_t col) {
  compiler__compile_node(c, args[0]);
  JaclType lhs_type = (JaclType)args[0]->inferred_type;

  compiler__compile_node(c, args[1]);
  JaclType rhs_type = (JaclType)args[1]->inferred_type;

  /* Static typing for struct comparisons */
  if (lhs_type == TYPE_STRUCT || rhs_type == TYPE_STRUCT) {
    uint32_t rhs_struct_idx = args[1]->inferred_struct_idx;
    if (lhs_type != rhs_type || rhs_struct_idx == UINT32_MAX) {
      char err[128];
      snprintf(err, sizeof(err),
               "type error: cannot %s %s and %s — structs only compare against "
               "the same struct type; narrow with [box? Type $val] first",
               op_verb, type_name(lhs_type), type_name(rhs_type));
      compiler__error(c, line, col, err);
      return;
    }
    if (op != OP_EQ) {
      compiler__error(c, line, col,
                      "structs only support equality (==), not ordering");
      return;
    }
    /* Both same struct type. Use OP_STRUCT_EQ_TOS — handles inline and heap
       via vm__pop_struct dispatch, no transient heap allocation. */
    compiler__emit_byte(c, OP_STRUCT_EQ_TOS, line);
    compiler__emit_u16(c, (uint16_t)rhs_struct_idx, line);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* Typed collection equality */
  if (is_typed_collection(lhs_type) || is_typed_collection(rhs_type)) {
    if (lhs_type != rhs_type) {
      char err[128];
      snprintf(err, sizeof(err), "type error: cannot %s %s and %s",
               op_verb, type_name(lhs_type), type_name(rhs_type));
      compiler__error(c, line, col, err);
      return;
    }
    if (op == OP_EQ) {
      uint8_t eq_op = (lhs_type == TYPE_TYPED_VEC) ? OP_TYPED_VEC_EQ : OP_TYPED_MAP_EQ;
      compiler__emit_byte(c, eq_op, line);
      compiler__emit_u16(c, (uint16_t)args[1]->inferred_struct_idx, line);
      if (lhs_type == TYPE_TYPED_MAP)
        compiler__emit_u16(c, (uint16_t)args[1]->inferred_key_struct_idx, line);
      c->last_expr_type = TYPE_BOOL;
      return;
    }
    /* Typed collections only support equality, not ordering */
    char err[128];
    snprintf(err, sizeof(err), "type error: cannot %s typed collections (only == supported)",
             op_verb);
    compiler__error(c, line, col, err);
    return;
  }

  /* Type checking — only enforced when unboxed types (i64/u64/f64) are involved,
     since unboxed values can't go through dynamic dispatch */
  if (is_unboxed_type(lhs_type) || is_unboxed_type(rhs_type)) {
    if (lhs_type != rhs_type) {
      char err[128];
      snprintf(err, sizeof(err), "type error: cannot %s %s and %s",
               op_verb, type_name(lhs_type), type_name(rhs_type));
      compiler__error(c, line, col, err);
      return;
    }
    /* Both same unboxed type — emit typed opcode */
    compiler__emit_byte(c, compiler__typed_op(op, lhs_type), line);
    bool is_cmp = (op == OP_EQ || op == OP_LT || op == OP_GT ||
                   op == OP_LE || op == OP_GE);
    c->last_expr_type = is_cmp ? TYPE_BOOL : lhs_type;
  } else {
    /* Both boxed/dyn — generic dispatch */
    compiler__emit_byte(c, op, line);
    bool is_cmp = (op == OP_EQ || op == OP_LT || op == OP_GT ||
                   op == OP_LE || op == OP_GE);
    JaclType result_type;
    if (is_cmp) {
      result_type = TYPE_BOOL;
    } else if (lhs_type == rhs_type &&
               (lhs_type == TYPE_I32 || lhs_type == TYPE_U32 ||
                lhs_type == TYPE_F32)) {
      /* Tagged scalar arithmetic: result preserves operand tag */
      result_type = lhs_type;
    } else {
      result_type = TYPE_DYN;
    }
    c->last_expr_type = result_type;
  }
}

/* --- Internal: Shared HOF builtin compilation (transform/each/filter) --- */

void compiler__compile_hof_builtin(Compiler* c, const char* name,
                                           AstNode** args, uint32_t argc,
                                           uint8_t opcode,
                                           uint32_t line, uint32_t col) {
  if (argc != 2) {
    compiler__builtin_arity_error(c, line, col, name, "2 arguments", argc);
    return;
  }
  /* Check if callback is a known suspending proc ($var reference) */
  if (args[1]->type == AST_VAR_REF && args[1]->data.var_ref.length <= 128) {
    JaclVal cb_name = compiler__name_val(c->heap, c->intern_table, args[1]->data.var_ref.name,
                                          args[1]->data.var_ref.length);
    char err_msg[128];
    snprintf(err_msg, sizeof(err_msg),
             "cannot pass suspending closure to non-suspending builtin '%s'",
             name);
    int slot = compiler__resolve_local(c, cb_name);
    if (slot != -1 && c->locals[slot].suspends) {
      compiler__error(c, line, col, err_msg);
      return;
    }
    GlobalArity* ga = compiler__find_global(c, cb_name);
    if (ga && ga->suspends) {
      compiler__error(c, line, col, err_msg);
      return;
    }
  }
  /* Check if callback block contains suspension points */
  if (ast__contains_suspension(args[1], c->suspension_map,
                                c->heap, c->intern_table)) {
    compiler__error(c, line, col,
        "cannot suspend inside non-suspending callback");
    return;
  }
  compiler__compile_node(c, args[0]);
  JaclType col_type = (JaclType)args[0]->inferred_type;
  uint32_t col_struct_idx = args[0]->inferred_struct_idx;
  uint32_t col_key_struct_idx = args[0]->inferred_key_struct_idx;
  {
    bool saved = c->in_non_suspending_callback;
    c->in_non_suspending_callback = true;
    compiler__compile_node(c, args[1]);
    c->in_non_suspending_callback = saved;
  }
  /* Typed collection dispatch: emit typed HOF opcode with type_idx */
  if ((col_type == TYPE_TYPED_VEC || col_type == TYPE_TYPED_MAP) &&
      (opcode == OP_EACH || opcode == OP_TRANSFORM || opcode == OP_FILTER)) {
    uint8_t typed_op;
    if (opcode == OP_EACH)           typed_op = OP_TYPED_EACH;
    else if (opcode == OP_TRANSFORM) typed_op = OP_TYPED_TRANSFORM;
    else                             typed_op = OP_TYPED_FILTER;
    compiler__emit_byte(c, typed_op, line);
    compiler__emit_u16(c, (uint16_t)col_struct_idx, line);
    /* Always emit key_type_idx: 0xFFFF for typed vecs, actual idx for struct-key maps */
    compiler__emit_u16(c, (col_type == TYPE_TYPED_MAP) ? (uint16_t)col_key_struct_idx : (uint16_t)0xFFFF, line);
    if (opcode == OP_FILTER) {
      c->last_expr_type = col_type;
    } else if (opcode == OP_TRANSFORM) {
      c->last_expr_type = TYPE_VEC;
    } else {
      c->last_expr_type = col_type;
    }
    return;
  }
  compiler__emit_byte(c, opcode, line);
  /* Preserve collection type: stream→stream, vec→vec */
  c->last_expr_type = col_type;
}

/* --- Internal: Compile a vector destructuring binding ---
 *
 * Handles both def and mut forms. Compiles the value expression,
 * emits OP_DESTRUCTURE_VEC or OP_DESTRUCTURE_VEC_REST, and creates bindings.
 *
 * For def (immutable): OP_DESTRUCTURE_VEC pushes all elements, each becomes a local.
 * For mut (mutable): elements extracted individually and wrapped in cells.
 * For globals (scope_depth==0): each element stored with OP_DEF_GLOBAL.
 * rest_name/rest_name_len: if non-NULL, collects remaining elements into a vector.
 */
void compiler__compile_destructure_vec(
    Compiler* c,
    const char** d_names, uint32_t* d_name_lens,
    const char** d_types, uint32_t* d_type_lens,
    uint32_t d_count,
    const char* rest_name, uint32_t rest_name_len,
    AstNode* value_expr,
    bool is_mutable,
    uint32_t line, uint32_t col)
{
  int has_rest = (rest_name != NULL && rest_name_len > 0);

  /* Validate rest name length */
  if (has_rest && rest_name_len > 128) {
    compiler__error(c, line, col,
                    "variable name exceeds 128-byte limit");
    return;
  }

  /* Compute wildcard skip mask and validate binding names */
  uint8_t skip_mask = 0;
  for (uint32_t i = 0; i < d_count; i++) {
    if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
      skip_mask |= (uint8_t)(1u << i);
    } else if (d_name_lens[i] > 128) {
      compiler__error(c, line, col,
                      "variable name exceeds 128-byte limit");
      return;
    }
  }

  /* Rest pattern is incompatible with wildcards in skip_mask (simplification) */

  /* Compile RHS — pushes one value (should be a vector) onto stack */
  compiler__compile_node(c, value_expr);

  if (has_rest) {
    /* --- Rest pattern: use OP_DESTRUCTURE_VEC_REST ---
       Pushes d_count individual elements + 1 rest vector onto stack */
    if (c->scope_depth > 0) {
      /* --- Local scope --- */
      if (!is_mutable) {
        /* def: OP_DESTRUCTURE_VEC_REST pushes N elements + rest vector */
        compiler__emit_byte(c, OP_DESTRUCTURE_VEC_REST, line);
        compiler__emit_byte(c, (uint8_t)d_count, line);
        /* Register locals for positional elements */
        for (uint32_t i = 0; i < d_count; i++) {
          if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
            /* Wildcard: still on stack from OP_DESTRUCTURE_VEC_REST,
               but we need a placeholder local to keep stack alignment */
            JaclVal wc_name = jacl_inline_string("", 0);
            compiler__add_local(c, wc_name, line, col);
          } else {
            JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
            compiler__add_local(c, name_val, line, col);
            compiler__apply_destructure_type(c, d_types, d_type_lens, i);
          }
        }
        /* Register local for rest vector */
        JaclVal rest_val = compiler__name_val(c->heap, c->intern_table, rest_name, rest_name_len);
        compiler__add_local(c, rest_val, line, col);
        c->locals[c->local_count - 1].type = TYPE_VEC;
      } else {
        /* mut: store vec as temp, extract elements + rest individually */
        JaclVal temp_name = jacl_inline_string("", 0);
        compiler__add_local(c, temp_name, line, col);
        uint32_t vec_slot = c->local_count - 1;

        for (uint32_t i = 0; i < d_count; i++) {
          if (d_name_lens[i] == 1 && d_names[i][0] == '_') continue;
          compiler__emit_byte(c, OP_GET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)vec_slot, line);
          uint16_t idx = chunk_add_constant(c->chunk, jacl_i32((int32_t)i));
          compiler__emit_byte(c, OP_CONST, line);
          compiler__emit_u16(c, idx, line);
          compiler__emit_byte(c, OP_VEC_GET, line);
          compiler__emit_byte(c, OP_MAKE_CELL, line);
          JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
          compiler__add_local(c, name_val, line, col);
          c->locals[c->local_count - 1].is_mutable = true;
          compiler__apply_destructure_type(c, d_types, d_type_lens, i);
        }
        /* Rest: use OP_DESTRUCTURE_VEC_REST on a copy to get the rest vector,
           or manually build it with vec-slice. Simpler: push vec, emit
           OP_DESTRUCTURE_VEC_REST, pop the N elements, keep the rest vector. */
        /* Actually, use OP_VEC_SLICE to create rest vector: vec[d_count..] */
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)vec_slot, line);
        /* Push start index (d_count) */
        uint16_t start_idx = chunk_add_constant(c->chunk, jacl_i32((int32_t)d_count));
        compiler__emit_byte(c, OP_CONST, line);
        compiler__emit_u16(c, start_idx, line);
        /* Push end index — use vec-len for "to end" */
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)vec_slot, line);
        compiler__emit_byte(c, OP_VEC_LEN, line);
        /* Emit OP_VEC_SLICE */
        compiler__emit_byte(c, OP_VEC_SLICE, line);
        compiler__emit_byte(c, OP_MAKE_CELL, line);
        JaclVal rest_val = compiler__name_val(c->heap, c->intern_table, rest_name, rest_name_len);
        compiler__add_local(c, rest_val, line, col);
        c->locals[c->local_count - 1].is_mutable = true;
        c->locals[c->local_count - 1].type = TYPE_VEC;
      }
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* --- Global scope with rest --- */
      compiler__emit_byte(c, OP_DESTRUCTURE_VEC_REST, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      /* Stack: elem0 ... elemN-1 rest_vec (bottom to top)
         Process in reverse: rest first, then elements. */
      /* Define rest global (top of stack) */
      if (is_mutable && c->current_module) {
        compiler__emit_byte(c, OP_BOX, line);
      }
      JaclVal rest_val = compiler__name_val(c->heap, c->intern_table, rest_name, rest_name_len);
      JaclVal rest_gkey = compiler__global_name_val(c, rest_name, rest_name_len);
      uint16_t rest_idx = chunk_add_constant(c->chunk, rest_gkey);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, rest_idx, line);
      compiler__set_global_arity(c, rest_val, -1);
      if (is_mutable) compiler__mark_global_mutable(c, rest_val);
      /* Now define positional elements in reverse order */
      for (int i = (int)d_count - 1; i >= 0; i--) {
        compiler__emit_byte(c, OP_POP, line); /* pop nil from previous OP_DEF_GLOBAL */
        if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
          /* Wildcard: just pop the value */
          compiler__emit_byte(c, OP_POP, line);
          compiler__emit_byte(c, OP_NIL, line); /* push nil placeholder */
          continue;
        }
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) compiler__mark_global_mutable(c, name_val);
      }
    }
  } else {
    /* --- No rest pattern: original logic --- */

    if (c->scope_depth > 0) {
      /* --- Local scope --- */
      if (!is_mutable) {
        /* def: OP_DESTRUCTURE_VEC pushes non-wildcard elements as locals */
        compiler__emit_byte(c, OP_DESTRUCTURE_VEC, line);
        compiler__emit_byte(c, (uint8_t)d_count, line);
        compiler__emit_byte(c, skip_mask, line);
        for (uint32_t i = 0; i < d_count; i++) {
          if (skip_mask & (1u << i)) continue; /* wildcard: no local */
          JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
          compiler__add_local(c, name_val, line, col);
          compiler__apply_destructure_type(c, d_types, d_type_lens, i);
        }
      } else {
        /* mut: extract each element individually, wrap in cell.
           Store vec as temporary local to allow repeated access. */
        JaclVal temp_name = jacl_inline_string("", 0);
        compiler__add_local(c, temp_name, line, col);
        uint32_t vec_slot = c->local_count - 1;

        for (uint32_t i = 0; i < d_count; i++) {
          if (skip_mask & (1u << i)) continue; /* wildcard: skip */
          /* Push the vector again from temp local */
          compiler__emit_byte(c, OP_GET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)vec_slot, line);
          /* Push index */
          uint16_t idx = chunk_add_constant(c->chunk, jacl_i32((int32_t)i));
          compiler__emit_byte(c, OP_CONST, line);
          compiler__emit_u16(c, idx, line);
          /* Extract element */
          compiler__emit_byte(c, OP_VEC_GET, line);
          /* Wrap in cell for mutable binding */
          compiler__emit_byte(c, OP_MAKE_CELL, line);
          /* Register local */
          JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
          compiler__add_local(c, name_val, line, col);
          c->locals[c->local_count - 1].is_mutable = true;
          compiler__apply_destructure_type(c, d_types, d_type_lens, i);
        }
      }
      /* def/mut returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* --- Global scope --- */
      compiler__emit_byte(c, OP_DESTRUCTURE_VEC, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      compiler__emit_byte(c, skip_mask, line);
      /* Non-skipped elements are on stack (bottom to top).
         Process in reverse so we consume from top of stack.
         Only process non-wildcard positions. */
      int first_non_wildcard = 1;
      for (int i = (int)d_count - 1; i >= 0; i--) {
        if (skip_mask & (1u << i)) continue; /* wildcard: skip */
        if (!first_non_wildcard) {
          /* Pop the nil pushed by previous OP_DEF_GLOBAL */
          compiler__emit_byte(c, OP_POP, line);
        }
        first_non_wildcard = 0;
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        /* OP_DEF_GLOBAL pops value, pushes nil */
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) compiler__mark_global_mutable(c, name_val);
      }
      /* If all positions were wildcards, push nil as return value */
      if (first_non_wildcard) {
        compiler__emit_byte(c, OP_NIL, line);
      }
    }
  }

  c->last_expr_type = TYPE_NIL;
}

/* -------------------------------------------------------------------------
 * Compile named struct/map destructuring: def {x, y} $expr or mut {x, y} $expr
 *
 * For known struct types: emits OP_HEAP_RECORD_GET per field (compile-time resolved).
 * For dyn/map types: emits OP_DESTRUCTURE_NAMED (runtime resolved).
 * For mut: wraps each extracted value in a cell.
 * For globals: defines each extracted value as a global.
 * rest_name/rest_name_len: if non-NULL, collects remaining fields into a map.
 */
void compiler__compile_destructure_named(
    Compiler* c,
    const char** d_names, uint32_t* d_name_lens,
    const char** d_types, uint32_t* d_type_lens,
    uint32_t d_count,
    const char* rest_name, uint32_t rest_name_len,
    int spread_all,
    AstNode* value_expr,
    bool is_mutable,
    uint32_t line, uint32_t col)
{
  int has_rest = (rest_name != NULL && rest_name_len > 0);

  /* Validate rest name length */
  if (has_rest && rest_name_len > 128) {
    compiler__error(c, line, col,
                    "variable name exceeds 128-byte limit");
    return;
  }

  /* Validate binding names */
  for (uint32_t i = 0; i < d_count; i++) {
    if (d_name_lens[i] == 1 && d_names[i][0] == '_') {
      compiler__error(c, line, col,
                      "'_' is meaningless in named destructuring; just omit the field");
      return;
    }
    if (d_name_lens[i] > 128) {
      compiler__error(c, line, col,
                      "variable name exceeds 128-byte limit");
      return;
    }
  }

  /* Compile RHS — pushes the source value onto stack. */
  compiler__compile_node(c, value_expr);
  JaclType rhs_type = (JaclType)value_expr->inferred_type;
  uint32_t rhs_struct_idx = value_expr->inferred_struct_idx;

  /* Determine if we can use compile-time struct field resolution */
  int use_struct_path = 0;
  StructTypeDef* sdef = NULL;

  if (rhs_type == TYPE_STRUCT && rhs_struct_idx != UINT32_MAX) {
    StructTypeRegistry* reg = compiler__get_struct_registry(c);
    if (reg && rhs_struct_idx < reg->count && reg->defs[rhs_struct_idx]) {
      sdef = reg->defs[rhs_struct_idx];
      use_struct_path = 1;
      /* Validate all field names at compile time */
      for (uint32_t i = 0; i < d_count; i++) {
        uint32_t fi;
        for (fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->fields[fi].name_len == d_name_lens[i] &&
              memcmp(sdef->fields[fi].name, d_names[i], d_name_lens[i]) == 0)
            break;
        }
        if (fi == sdef->field_count) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "struct '%.*s' has no field '%.*s'",
                   (int)sdef->name_len, sdef->name,
                   (int)d_name_lens[i], d_names[i]);
          compiler__error(c, line, col, err_msg);
          return;
        }
      }
    }
  }

  /* --- Spread-all expansion: {..} or {x, ..} --- */
  const char* exp_names[STRUCT_MAX_FIELDS];
  uint32_t    exp_name_lens[STRUCT_MAX_FIELDS];
  const char* exp_types[STRUCT_MAX_FIELDS];
  uint32_t    exp_type_lens[STRUCT_MAX_FIELDS];
  uint32_t    exp_count = 0;

  if (spread_all) {
    if (!use_struct_path || !sdef) {
      compiler__error(c, line, col,
                      "spread-all {..} requires a known struct type; got dyn");
      return;
    }
    /* Build expanded field list: explicit fields + all remaining struct fields */

    /* Copy explicit fields first */
    for (uint32_t i = 0; i < d_count; i++) {
      exp_names[exp_count]     = d_names[i];
      exp_name_lens[exp_count] = d_name_lens[i];
      exp_types[exp_count]     = (d_types ? d_types[i] : NULL);
      exp_type_lens[exp_count] = (d_type_lens ? d_type_lens[i] : 0);
      exp_count++;
    }

    /* Add remaining struct fields not already listed */
    for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
      int already_listed = 0;
      for (uint32_t i = 0; i < d_count; i++) {
        if (sdef->fields[fi].name_len == d_name_lens[i] &&
            memcmp(sdef->fields[fi].name, d_names[i], d_name_lens[i]) == 0) {
          already_listed = 1;
          break;
        }
      }
      if (!already_listed) {
        if (sdef->fields[fi].name_len > 128) {
          compiler__error(c, line, col,
                          "variable name exceeds 128-byte limit");
          return;
        }
        exp_names[exp_count]     = sdef->fields[fi].name;
        exp_name_lens[exp_count] = sdef->fields[fi].name_len;
        exp_types[exp_count]     = NULL;
        exp_type_lens[exp_count] = 0;
        exp_count++;
      }
    }

    /* Check for same-scope shadowing */
    if (c->scope_depth > 0) {
      for (uint32_t i = 0; i < exp_count; i++) {
        JaclVal check_name = compiler__name_val(c->heap, c->intern_table, exp_names[i], exp_name_lens[i]);
        for (int j = (int)c->local_count - 1; j >= 0; j--) {
          if (c->locals[j].depth < c->scope_depth) break;
          if (c->locals[j].name == check_name) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg),
                     "spread-all would shadow existing variable '%.*s'",
                     (int)exp_name_lens[i], exp_names[i]);
            compiler__error(c, line, col, err_msg);
            return;
          }
        }
      }
    }

    /* Replace parameters with expanded list */
    d_names     = exp_names;
    d_name_lens = exp_name_lens;
    d_types     = exp_types;
    d_type_lens = exp_type_lens;
    d_count     = exp_count;
  }

  if (c->scope_depth > 0) {
    /* --- Local scope --- */
    /* Store source value in temp local for repeated access. For a typed
       struct RHS the source is a wide local holding inline bytes; we use
       OP_STRUCT_GET_INLINE for fields. For map/dyn it's a single slot. */
    JaclVal temp_name = jacl_inline_string("", 0);
    StructTypeRegistry* dreg = compiler__get_struct_registry(c);
    uint32_t struct_width = (use_struct_path && dreg)
                             ? struct__slot_width(dreg, rhs_struct_idx) : 1;
    /* If the typed-struct RHS produced a heap pointer (e.g. global var-ref),
       expand it to inline slots so we can adopt as a wide local. Both
       INLINE_STACK and INLINE_REF already have inline bytes on TOS. */
    if (use_struct_path && c->inline_repr == INLINE_NONE) {
      compiler__emit_byte(c, OP_STRUCT_EXPAND, line);
      compiler__emit_u16(c, (uint16_t)rhs_struct_idx, line);
      c->inline_repr = INLINE_STACK;
    }
    compiler__add_local(c, temp_name, line, col);
    uint32_t src_slot = c->local_count - 1;
    if (use_struct_path) {
      c->locals[src_slot].type = TYPE_STRUCT;
      c->locals[src_slot].struct_type_idx = rhs_struct_idx;
      c->locals[src_slot].is_inline = true;
      c->locals[src_slot].width = (uint16_t)struct_width;
      for (uint32_t w = 1; w < struct_width; w++) {
        compiler__add_local(c, jacl_inline_string("", 0), line, col);
        c->locals[c->local_count - 1].depth = c->scope_depth;
      }
    } else if (rhs_type == TYPE_STRUCT) {
      c->locals[src_slot].type = TYPE_STRUCT;
      c->locals[src_slot].struct_type_idx = rhs_struct_idx;
    }

    if (use_struct_path) {
      if (has_rest) {
        /* Rest patterns aren't supported on struct destructure — building
           the rest map would require materializing the inline struct to a
           heap HeapRecord (auto-allocation, against the design rule). The
           user can list every field explicitly, or destructure into a map
           via [box?] / [unbox] flow if they really want a dynamic rest. */
        compiler__error(c, line, col,
                        "rest pattern '..rest' is not supported when "
                        "destructuring a struct — list each field explicitly");
        return;
      }
      {
        /* No-rest path: extract each field with OP_STRUCT_GET_INLINE — reads
           bytes directly from the wide local, no heap allocation. */
        for (uint32_t i = 0; i < d_count; i++) {
          uint32_t fi;
          for (fi = 0; fi < sdef->field_count; fi++) {
            if (sdef->fields[fi].name_len == d_name_lens[i] &&
                memcmp(sdef->fields[fi].name, d_names[i], d_name_lens[i]) == 0)
              break;
          }
          compiler__emit_byte(c, OP_STRUCT_GET_INLINE, line);
          compiler__emit_byte(c, (uint8_t)src_slot, line);
          compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
          compiler__emit_byte(c, (uint8_t)sdef->fields[fi].type, line);
          if (sdef->fields[fi].type == TYPE_STRUCT) {
            compiler__emit_u16(c, (uint16_t)sdef->fields[fi].struct_type_idx, line);
          }

          if (is_mutable) {
            compiler__emit_byte(c, OP_MAKE_CELL, line);
          }

          JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
          compiler__add_local(c, name_val, line, col);
          if (is_mutable)
            c->locals[c->local_count - 1].is_mutable = true;
          if (d_types && d_types[i]) {
            JaclType t;
            if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
              c->locals[c->local_count - 1].type = t;
            }
          } else {
            c->locals[c->local_count - 1].type = sdef->fields[fi].type;
            if (sdef->fields[fi].type == TYPE_STRUCT)
              c->locals[c->local_count - 1].struct_type_idx = sdef->fields[fi].struct_type_idx;
          }
        }
      }
    } else {
      /* Dyn/map path: extract each field one at a time with runtime resolution.
         Use OP_DESTRUCTURE_NAMED with count=1 per field so missing-key errors
         are caught, and mutable wrapping works naturally. */
      for (uint32_t i = 0; i < d_count; i++) {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)src_slot, line);

        compiler__emit_byte(c, OP_DESTRUCTURE_NAMED, line);
        compiler__emit_byte(c, 1, line);
        JaclVal key_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
        compiler__emit_u16(c, key_idx, line);

        if (is_mutable) {
          compiler__emit_byte(c, OP_MAKE_CELL, line);
        }

        JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        compiler__add_local(c, name_val, line, col);
        if (is_mutable)
          c->locals[c->local_count - 1].is_mutable = true;
        compiler__apply_destructure_type(c, d_types, d_type_lens, i);
      }

      /* Rest: build map from remaining fields */
      if (has_rest) {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)src_slot, line);
        /* Emit OP_DESTRUCTURE_NAMED_REST with explicit field names to exclude */
        compiler__emit_byte(c, OP_DESTRUCTURE_NAMED_REST, line);
        compiler__emit_byte(c, (uint8_t)d_count, line);
        for (uint32_t i = 0; i < d_count; i++) {
          JaclVal key_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
          uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
          compiler__emit_u16(c, key_idx, line);
        }
        if (is_mutable) {
          compiler__emit_byte(c, OP_MAKE_CELL, line);
        }
        JaclVal rest_val = compiler__name_val(c->heap, c->intern_table, rest_name, rest_name_len);
        compiler__add_local(c, rest_val, line, col);
        if (is_mutable)
          c->locals[c->local_count - 1].is_mutable = true;
        c->locals[c->local_count - 1].type = TYPE_MAP;
      }
    }

    /* def/mut returns nil */
    compiler__emit_byte(c, OP_NIL, line);
  } else {
    /* --- Global scope --- */
    /* NOTE: The RHS value is already on the stack from compile_node above. */
    if (!has_rest) {
      compiler__emit_byte(c, OP_DESTRUCTURE_NAMED, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      for (uint32_t i = 0; i < d_count; i++) {
        JaclVal key_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
        compiler__emit_u16(c, key_idx, line);
      }
      /* Elements are on stack: elem0 (bottom) ... elemN-1 (top).
         Process in reverse so we consume from top of stack. */
      for (int i = (int)d_count - 1; i >= 0; i--) {
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) compiler__mark_global_mutable(c, name_val);
        if (i > 0) {
          compiler__emit_byte(c, OP_POP, line);
        }
      }
    } else {
      /* Rest path: OP_DESTRUCTURE_NAMED_REST pushes N fields + 1 rest map */
      compiler__emit_byte(c, OP_DESTRUCTURE_NAMED_REST, line);
      compiler__emit_byte(c, (uint8_t)d_count, line);
      for (uint32_t i = 0; i < d_count; i++) {
        JaclVal key_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
        compiler__emit_u16(c, key_idx, line);
      }
      /* Stack: elem0 ... elemN-1 rest_map (bottom to top)
         Process in reverse: rest first, then elements. */
      /* Define rest global (top of stack) */
      if (is_mutable && c->current_module) {
        compiler__emit_byte(c, OP_BOX, line);
      }
      JaclVal rest_val = compiler__name_val(c->heap, c->intern_table, rest_name, rest_name_len);
      JaclVal rest_gkey = compiler__global_name_val(c, rest_name, rest_name_len);
      uint16_t rest_idx = chunk_add_constant(c->chunk, rest_gkey);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, rest_idx, line);
      compiler__set_global_arity(c, rest_val, -1);
      if (is_mutable) compiler__mark_global_mutable(c, rest_val);
      /* Now define positional elements in reverse order */
      for (int i = (int)d_count - 1; i >= 0; i--) {
        compiler__emit_byte(c, OP_POP, line); /* pop nil from previous OP_DEF_GLOBAL */
        if (is_mutable && c->current_module) {
          compiler__emit_byte(c, OP_BOX, line);
        }
        JaclVal name_val = compiler__name_val(c->heap, c->intern_table, d_names[i], d_name_lens[i]);
        JaclVal global_key = compiler__global_name_val(c, d_names[i],
                                                        d_name_lens[i]);
        uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
        compiler__emit_byte(c, OP_DEF_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__set_global_arity(c, name_val, -1);
        if (is_mutable) compiler__mark_global_mutable(c, name_val);
      }
    }
  }

  c->last_expr_type = TYPE_NIL;
}

/* --- Internal: Rewrite operator node [op LHS RHS] into [target ...args]
 *
 * For = → def, : → mut, :: → set:
 *   [= name val]         →  [def name val]
 *   [= [type name] val]  →  [def type name val]  (typed binding)
 *   [= pattern val]      →  [def pattern val]    (destructuring)
 *
 * For | (pipe):
 *   [| [cmd1 a] [cmd2 b]]  →  [cmd2 [cmd1 a] b]  (first-arg threading)
 *   [| [cmd1 a] val]       →  [val [cmd1 a]]      (wrap as call)
 * ----------------------------------------------------------------------- */

void compiler__compile_command(Compiler* c, AstNode* node);

void compiler__rewrite_binding_op(Compiler* c, AstNode* node,
                                          const char* target, uint32_t target_len) {
  AstNode* lhs = node->data.command.args[0];
  AstNode* rhs = node->data.command.args[1];
  uint32_t line = node->start.line;

  /* Build synthetic command: [target ...normalized_args] */
  AstNode* new_head = ast_alloc(c->arena);
  new_head->type = AST_LIT_STRING;
  new_head->start = node->data.command.head->start;
  new_head->end   = node->data.command.head->end;
  new_head->data.lit_string.value  = target;
  new_head->data.lit_string.length = target_len;

  AstNode* synth = ast_alloc(c->arena);
  synth->type  = AST_COMMAND;
  synth->start = node->start;
  synth->end   = node->end;
  synth->data.command.head    = new_head;
  synth->data.command.head_id = ast__compute_head_id(new_head);

  /* Determine arg shape based on LHS type */
  if (lhs->type == AST_COMMAND && lhs->data.command.arg_count == 1 &&
      lhs->data.command.head->type == AST_LIT_STRING &&
      compiler__is_type_annotation(c,
          lhs->data.command.head->data.lit_string.value,
          lhs->data.command.head->data.lit_string.length)) {
    /* LHS is [type name] → typed binding: [target type name RHS] */
    AstNode** new_args = ast_alloc_array(c->arena, 3);
    new_args[0] = lhs->data.command.head;
    new_args[1] = lhs->data.command.args[0];
    new_args[2] = rhs;
    synth->data.command.args      = new_args;
    synth->data.command.arg_count = 3;
  } else if (lhs->type == AST_COMMAND && lhs->data.command.arg_count > 0) {
    /* LHS is a multi-word command like [a b c] → destructuring: [target [a b c] RHS] */
    AstNode** new_args = ast_alloc_array(c->arena, 2);
    new_args[0] = lhs;
    new_args[1] = rhs;
    synth->data.command.args      = new_args;
    synth->data.command.arg_count = 2;
  } else if (lhs->type == AST_COMMAND && lhs->data.command.arg_count == 0) {
    /* LHS is a zero-arg command [name] → unwrap to [target name RHS] */
    AstNode** new_args = ast_alloc_array(c->arena, 2);
    new_args[0] = lhs->data.command.head;
    new_args[1] = rhs;
    synth->data.command.args      = new_args;
    synth->data.command.arg_count = 2;
  } else {
    /* LHS is a destructuring pattern, block, or other node → [target LHS RHS] */
    AstNode** new_args = ast_alloc_array(c->arena, 2);
    new_args[0] = lhs;
    new_args[1] = rhs;
    synth->data.command.args      = new_args;
    synth->data.command.arg_count = 2;
  }

  compiler__compile_command(c, synth);
  (void)line;
}

/* Helper: Check if node is a shell command chain (either shell cmd or pipe of shell cmds) */
static int compiler__is_shell_cmd_chain(AstNode* node) {
  if (node->type == AST_SHELL_CMD) return 1;
  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING &&
        head->data.lit_string.length == 1 &&
        head->data.lit_string.value[0] == '|' &&
        node->data.command.arg_count == 2) {
      AstNode* lhs = node->data.command.args[0];
      AstNode* rhs = node->data.command.args[1];
      return compiler__is_shell_cmd_chain(lhs) && compiler__is_shell_cmd_chain(rhs);
    }
  }
  return 0;
}

/* Helper: Count shell commands in a chain */
static uint32_t compiler__count_shell_cmds(AstNode* node) {
  if (node->type == AST_SHELL_CMD) return 1;
  if (node->type == AST_COMMAND) {
    AstNode* head = node->data.command.head;
    if (head->type == AST_LIT_STRING &&
        head->data.lit_string.length == 1 &&
        head->data.lit_string.value[0] == '|' &&
        node->data.command.arg_count == 2) {
      return compiler__count_shell_cmds(node->data.command.args[0]) +
             compiler__count_shell_cmds(node->data.command.args[1]);
    }
  }
  return 0;
}

/* Helper: Compile shell command args into vector on stack (without exec) */
static void compiler__compile_shell_cmd_args(Compiler* c, AstNode* cmd) {
  AstNode* head = cmd->data.shell_cmd.head;
  uint32_t argc = cmd->data.shell_cmd.arg_count;
  AstNode** args = cmd->data.shell_cmd.args;
  uint32_t line = cmd->start.line;
  uint32_t col  = cmd->start.column;
  (void)col;

  /* Check if any args are spread */
  int has_spread = 0;
  for (uint32_t i = 0; i < argc; i++) {
    if (args[i]->type == AST_SPREAD) {
      has_spread = 1;
      break;
    }
  }

  /* Compile head (command name) as first element - always fixed */
  compiler__compile_node(c, head);
  compiler__ensure_boxed(c, line);

  if (has_spread) {
    /* US-014: Spread support - use OP_VEC_SPREAD */
    uint8_t fixed_args = 1;  /* head is always fixed */
    uint8_t num_spreads = 0;

    for (uint32_t i = 0; i < argc; i++) {
      if (args[i]->type == AST_SPREAD) {
        compiler__compile_node(c, args[i]->data.spread.expr);
        compiler__emit_byte(c, OP_SPREAD, line);
        num_spreads++;
      } else {
        compiler__compile_node(c, args[i]);
        compiler__ensure_boxed(c, line);
        fixed_args++;
      }
    }

    compiler__emit_byte(c, OP_VEC_SPREAD, line);
    compiler__emit_byte(c, fixed_args, line);
    compiler__emit_byte(c, num_spreads, line);
  } else {
    /* No spread - use simpler OP_VEC */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      compiler__ensure_boxed(c, line);
    }

    /* Build vector from stack elements */
    uint32_t total_elems = 1 + argc;
    if (total_elems > 255) {
      compiler__error(c, line, cmd->start.column, "too many arguments to shell command");
      return;
    }
    compiler__emit_byte(c, OP_VEC, line);
    compiler__emit_byte(c, (uint8_t)total_elems, line);
  }
}

/* Helper: Compile all shell commands in chain (left to right order) */
static void compiler__compile_shell_cmd_chain(Compiler* c, AstNode* node) {
  if (node->type == AST_SHELL_CMD) {
    compiler__compile_shell_cmd_args(c, node);
    return;
  }
  /* Must be a pipe node */
  AstNode* lhs = node->data.command.args[0];
  AstNode* rhs = node->data.command.args[1];
  /* Compile LHS first (leftmost commands), then RHS */
  compiler__compile_shell_cmd_chain(c, lhs);
  compiler__compile_shell_cmd_chain(c, rhs);
}

void compiler__compile_pipe_op(Compiler* c, AstNode* node) {
  AstNode* lhs = node->data.command.args[0];
  AstNode* rhs = node->data.command.args[1];
  uint32_t line = node->start.line;

  /* US-009: Adjacent shell commands use real OS pipes
   * [| !cmd1 [| !cmd2 !cmd3]] → compile all cmd vectors, OP_EXEC_PIPE count */
  if (rhs->type == AST_SHELL_CMD && compiler__is_shell_cmd_chain(lhs)) {
    uint32_t col = rhs->start.column;

    /* In prelude mode, check that `exec` is available */
    if (c->has_prelude) {
      JaclVal exec_name = compiler__name_val(c->heap, c->intern_table, "exec", 4);
      GlobalArity* ga = compiler__find_global(c, exec_name);
      if (!ga) {
        compiler__error(c, line, col, "exec not available");
        return;
      }
    }

    /* Count total commands in the chain */
    uint32_t cmd_count = compiler__count_shell_cmds(lhs) + 1; /* +1 for RHS */
    if (cmd_count > 255) {
      compiler__error(c, line, col, "too many commands in pipeline");
      return;
    }

    /* Compile all command vectors in order (left to right) */
    compiler__compile_shell_cmd_chain(c, lhs);
    compiler__compile_shell_cmd_args(c, rhs);

    /* Emit OP_EXEC with EXEC_FLAG_PIPE and command count */
    compiler__emit_byte(c, OP_EXEC, line);
    compiler__emit_byte(c, EXEC_FLAG_PIPE, line);
    compiler__emit_byte(c, (uint8_t)cmd_count, line);
    c->last_expr_type = TYPE_STREAM;
    return;
  }

  /* US-007: Shell command as pipe RHS — pipe LHS to stdin
   * [| expr [!cmd args...]] → compile LHS, compile cmd+args as vec, OP_EXEC_STDIN */
  if (rhs->type == AST_SHELL_CMD) {
    AstNode* head = rhs->data.shell_cmd.head;
    uint32_t argc = rhs->data.shell_cmd.arg_count;
    AstNode** args = rhs->data.shell_cmd.args;
    uint32_t col  = rhs->start.column;

    /* In prelude mode, check that `exec` is available */
    if (c->has_prelude) {
      JaclVal exec_name = compiler__name_val(c->heap, c->intern_table, "exec", 4);
      GlobalArity* ga = compiler__find_global(c, exec_name);
      if (!ga) {
        compiler__error(c, line, col, "exec not available");
        return;
      }
    }

    /* Compile head (command name) as first element of args vector */
    compiler__compile_node(c, head);
    compiler__ensure_boxed(c, line);

    /* Compile remaining arguments */
    for (uint32_t i = 0; i < argc; i++) {
      if (args[i]->type == AST_SPREAD) {
        compiler__error(c, line, col, "spread in shell commands not yet supported");
        return;
      }
      compiler__compile_node(c, args[i]);
      compiler__ensure_boxed(c, line);
    }

    /* Build vector from stack elements: [head, arg1, arg2, ...] */
    uint32_t total_elems = 1 + argc;
    if (total_elems > 255) {
      compiler__error(c, line, col, "too many arguments to shell command");
      return;
    }
    compiler__emit_byte(c, OP_VEC, line);
    compiler__emit_byte(c, (uint8_t)total_elems, line);

    /* Compile LHS (will be stdin) */
    compiler__compile_node(c, lhs);
    compiler__ensure_boxed(c, line);

    /* Emit OP_EXEC with EXEC_FLAG_STDIN: pops stdin, pops args_vec, spawns with stdin piped */
    compiler__emit_byte(c, OP_EXEC, line);
    compiler__emit_byte(c, EXEC_FLAG_STDIN, line);
    c->last_expr_type = TYPE_STREAM;
    return;
  }

  /* Build synthetic command: thread LHS result as first arg of RHS */
  AstNode* synth = ast_alloc(c->arena);
  synth->type  = AST_COMMAND;
  synth->start = node->start;
  synth->end   = node->end;

  if (rhs->type == AST_COMMAND) {
    /* [| [cmd1 a] [cmd2 b]] → [cmd2 [cmd1 a] b] */
    uint32_t old_count = rhs->data.command.arg_count;
    uint32_t new_count = old_count + 1;
    AstNode** new_args = ast_alloc_array(c->arena, new_count);
    new_args[0] = lhs;
    for (uint32_t i = 0; i < old_count; i++) {
      new_args[1 + i] = rhs->data.command.args[i];
    }
    synth->data.command.head      = rhs->data.command.head;
    synth->data.command.head_id   = rhs->data.command.head_id;
    synth->data.command.args      = new_args;
    synth->data.command.arg_count = new_count;
  } else {
    /* [| [cmd1 a] val] → [val [cmd1 a]] */
    AstNode** new_args = ast_alloc_array(c->arena, 1);
    new_args[0] = lhs;
    synth->data.command.head      = rhs;
    synth->data.command.head_id   = ast__compute_head_id(rhs);
    synth->data.command.args      = new_args;
    synth->data.command.arg_count = 1;
  }

  compiler__compile_command(c, synth);
}

/* --- Core vs non-core builtin classification for sandbox mode ---
 *
 * Returns true if `name` is a core builtin that always emits direct opcodes
 * regardless of prelude contents.  Returns false for non-core (capability-
 * sensitive) builtins that must go through env lookup in sandbox mode.
 *
 * Non-core builtins (downgraded to env lookup when prelude is active):
 *   print, interpret, interpret-prelude, spawn, await, parallel, race,
 *   yield, make-syntax, syntax-error, box, atom, deref, reset, swap,
 *   watch, unwatch,
 *   lines, stream_next, exec, signal, cancel,
 *   read-file, write-file, append-file
 *
 * Everything else (arithmetic, comparison, control flow, binding,
 * destructuring, immutable data ops, vec/map/set ops, string ops,
 * syntax-quote/unquote, type predicates) is core.
 */
/* Single source of truth for non-core builtins.  These are capability-
 * sensitive and subject to sandbox prelude gating.  Core builtins (arithmetic,
 * comparison, control flow, binding, data ops, etc.) are always available. */
const char *jacl_non_core_builtins[] = {
  "print", "interpret", "interpret-prelude",
  "spawn", "await", "parallel", "race", "yield",
  "make-syntax", "syntax-error",
  "box", "atom", "deref", "reset", "swap",
  "watch", "unwatch",
  "lines", "stream_next",
  "exec", "signal", "cancel",
  "read-file", "write-file", "append-file",
  NULL
};

bool compiler__is_core_builtin(const char *name, uint32_t len) {
  for (int i = 0; jacl_non_core_builtins[i]; i++) {
    uint32_t nclen = (uint32_t)strlen(jacl_non_core_builtins[i]);
    if (len == nclen && memcmp(name, jacl_non_core_builtins[i], len) == 0) {
      return false;
    }
  }
  return true;
}

/* --- Internal: Compile a command invocation --- */

void compiler__compile_command(Compiler* c, AstNode* node) {
  AstNode* head = node->data.command.head;
  HeadId   hid  = (HeadId)node->data.command.head_id;
  uint32_t argc = node->data.command.arg_count;
  AstNode** args = node->data.command.args;
  uint32_t line = node->start.line;
  uint32_t col  = node->start.column;

  /* Check if any arg is a spread expression */
  int has_spread = 0;
  for (uint32_t i = 0; i < argc; i++) {
    if (args[i]->type == AST_SPREAD) { has_spread = 1; break; }
  }

  /* --- Typed vec constructor: [[Vec Type] elem1 elem2 ...] --- */
  AstNode* _coll_elem = NULL;
  AstNode* _coll_key_elem = NULL;
  int _coll_kind = compiler__typed_collection_expr(head, &_coll_elem, &_coll_key_elem);
  if (_coll_kind == 1) {
    const char* type_name_str = _coll_elem->data.lit_string.value;
    uint32_t type_name_len = _coll_elem->data.lit_string.length;

    /* Scalar element type: [Vec i64], [Vec f64], etc. Encoded with a
     * sentinel type_idx in the COMPILER_SCALAR_VEC_BASE range. The VM's
     * OP_TYPED_VEC handler dispatches struct vs scalar via this range. */
    if (is_type_keyword(type_name_str, type_name_len)) {
      JaclType elem_t = type_from_keyword(type_name_str, type_name_len);
      if (!compiler__is_typed_collection_scalar(elem_t)) {
        char err[128];
        snprintf(err, sizeof(err),
                 "[Vec %.*s]: only value-type scalars supported "
                 "(i32, i64, u32, u64, f32, f64, bool)",
                 (int)type_name_len, type_name_str);
        compiler__error(c, line, col, err);
        return;
      }
      if (argc > 255) {
        compiler__error(c, line, col, "[Vec ...] too many initial elements (max 255)");
        return;
      }
      /* Verify each element's typer-annotated type matches. */
      for (uint32_t i = 0; i < argc; i++) {
        compiler__compile_node(c, args[i]);
        JaclType arg_t = (JaclType)args[i]->inferred_type;
        if (arg_t != elem_t) {
          char err[160];
          jacl_format_typed_vec_elem(err, sizeof(err),
              type_name_str, type_name_len, i, true, arg_t);
          compiler__error(c, line, col, err);
          return;
        }
      }
      compiler__emit_byte(c, OP_TYPED_VEC, line);
      compiler__emit_u16(c, (uint16_t)COMPILER_SCALAR_TYPE_IDX(elem_t), line);
      compiler__emit_byte(c, (uint8_t)argc, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      return;
    }

    StructTypeRegistry* reg = compiler__get_struct_registry(c);
    uint32_t type_idx = struct_registry__find(reg, type_name_str, type_name_len);
    if (type_idx == UINT32_MAX) {
      char err[128];
      snprintf(err, sizeof(err), "[Vec %.*s]: unknown struct type '%.*s'",
               (int)type_name_len, type_name_str,
               (int)type_name_len, type_name_str);
      compiler__error(c, line, col, err);
      return;
    }
    if (argc > 255) {
      compiler__error(c, line, col, "[Vec ...] too many initial elements (max 255)");
      return;
    }
    /* Compile each element, verify struct type, and reify inline to heap.
     * OP_TYPED_VEC expects heap struct pointers on the stack. */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      if ((JaclType)args[i]->inferred_type != TYPE_STRUCT ||
          args[i]->inferred_struct_idx != type_idx) {
        char err[128];
        jacl_format_typed_vec_elem(err, sizeof(err),
            type_name_str, type_name_len, i, false, TYPE_DYN);
        compiler__error(c, line, col, err);
        return;
      }
      /* OP_TYPED_VEC consumes inline struct bytes directly via vm__pop_struct. */
    }
    compiler__emit_byte(c, OP_TYPED_VEC, line);
    compiler__emit_u16(c, (uint16_t)type_idx, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    c->last_expr_type = TYPE_TYPED_VEC;
    return;
  }

  /* --- Typed map constructor: [[Map Type] key1 val1 key2 val2 ...] --- */
  if (_coll_kind == 2) {
    const char* type_name_str = _coll_elem->data.lit_string.value;
    uint32_t type_name_len = _coll_elem->data.lit_string.length;

    /* Scalar value type: [Map i64], [Map f64] etc. — dyn keys, scalar values. */
    if (is_type_keyword(type_name_str, type_name_len)) {
      JaclType val_t = type_from_keyword(type_name_str, type_name_len);
      if (!compiler__is_typed_collection_scalar(val_t)) {
        char err[160];
        snprintf(err, sizeof(err),
                 "[Map %.*s]: only value-type scalars supported "
                 "(i32, i64, u32, u64, f32, f64)",
                 (int)type_name_len, type_name_str);
        compiler__error(c, line, col, err);
        return;
      }
      if (argc % 2 != 0) {
        compiler__error(c, line, col, "[Map ...] requires an even number of arguments (key-value pairs)");
        return;
      }
      uint32_t pair_count = argc / 2;
      if (pair_count > 255) {
        compiler__error(c, line, col, "[Map ...] too many initial pairs (max 255)");
        return;
      }
      uint32_t val_type_idx = COMPILER_SCALAR_TYPE_IDX(val_t);
      for (uint32_t i = 0; i < pair_count; i++) {
        compiler__compile_node(c, args[i * 2]);     /* key: any dyn type */
        compiler__compile_node(c, args[i * 2 + 1]); /* value: must match scalar */
        JaclType v_t = (JaclType)args[i * 2 + 1]->inferred_type;
        if (v_t != val_t) {
          char err[160];
          jacl_format_typed_map_value(err, sizeof(err),
              type_name_str, type_name_len, i, true, v_t);
          compiler__error(c, line, col, err);
          return;
        }
      }
      compiler__emit_byte(c, OP_TYPED_MAP, line);
      compiler__emit_u16(c, (uint16_t)val_type_idx, line);
      compiler__emit_u16(c, (uint16_t)0xFFFF, line);  /* dyn keys */
      compiler__emit_byte(c, (uint8_t)pair_count, line);
      c->last_expr_type = TYPE_TYPED_MAP;
      return;
    }

    StructTypeRegistry* reg = compiler__get_struct_registry(c);
    uint32_t type_idx = struct_registry__find(reg, type_name_str, type_name_len);
    if (type_idx == UINT32_MAX) {
      char err[128];
      snprintf(err, sizeof(err), "[Map %.*s]: unknown struct type '%.*s'",
               (int)type_name_len, type_name_str,
               (int)type_name_len, type_name_str);
      compiler__error(c, line, col, err);
      return;
    }
    if (argc % 2 != 0) {
      compiler__error(c, line, col, "[Map ...] requires an even number of arguments (key-value pairs)");
      return;
    }
    uint32_t pair_count = argc / 2;
    if (pair_count > 255) {
      compiler__error(c, line, col, "[Map ...] too many initial pairs (max 255)");
      return;
    }
    /* Compile alternating key/value pairs. Keys are dyn, values must match struct type. */
    for (uint32_t i = 0; i < pair_count; i++) {
      compiler__compile_node(c, args[i * 2]);       /* key: any dyn type */
      compiler__compile_node(c, args[i * 2 + 1]);   /* value: must be matching struct */
      AstNode* val = args[i * 2 + 1];
      if ((JaclType)val->inferred_type != TYPE_STRUCT ||
          val->inferred_struct_idx != type_idx) {
        char err[128];
        jacl_format_typed_map_value(err, sizeof(err),
            type_name_str, type_name_len, i, false, TYPE_DYN);
        compiler__error(c, line, col, err);
        return;
      }
      /* OP_TYPED_MAP consumes inline struct values directly via vm__pop_struct. */
    }
    compiler__emit_byte(c, OP_TYPED_MAP, line);
    compiler__emit_u16(c, (uint16_t)type_idx, line);
    compiler__emit_u16(c, (uint16_t)0xFFFF, line);  /* key_type_idx: dyn keys */
    compiler__emit_byte(c, (uint8_t)pair_count, line);
    c->last_expr_type = TYPE_TYPED_MAP;
    return;
  }

  /* --- Typed map constructor with explicit key + value types:
   *      [[Map KeyType ValueType] k1 v1 ...]
   * Each of KeyType / ValueType may be a struct name or a numeric scalar
   * keyword. Mixed combinations are allowed (struct key + scalar value,
   * scalar key + struct value, both scalar, both struct). */
  if (_coll_kind == 3) {
    const char* val_name_str = _coll_elem->data.lit_string.value;
    uint32_t val_name_len = _coll_elem->data.lit_string.length;
    const char* key_name_str = _coll_key_elem->data.lit_string.value;
    uint32_t key_name_len = _coll_key_elem->data.lit_string.length;
    StructTypeRegistry* reg = compiler__get_struct_registry(c);

    /* Resolve key type: scalar keyword OR struct name. */
    JaclType key_t = TYPE_DYN;
    uint32_t key_type_idx;
    bool key_is_scalar = false;
    if (is_type_keyword(key_name_str, key_name_len)) {
      key_t = type_from_keyword(key_name_str, key_name_len);
      if (!compiler__is_typed_collection_scalar(key_t)) {
        char err[160];
        snprintf(err, sizeof(err),
                 "[Map %.*s %.*s]: only numeric value-type scalars supported as keys",
                 (int)key_name_len, key_name_str,
                 (int)val_name_len, val_name_str);
        compiler__error(c, line, col, err);
        return;
      }
      key_type_idx = COMPILER_SCALAR_TYPE_IDX(key_t);
      key_is_scalar = true;
    } else {
      key_type_idx = struct_registry__find(reg, key_name_str, key_name_len);
      if (key_type_idx == UINT32_MAX) {
        char err[128];
        snprintf(err, sizeof(err), "[Map %.*s %.*s]: unknown key type '%.*s'",
                 (int)key_name_len, key_name_str,
                 (int)val_name_len, val_name_str,
                 (int)key_name_len, key_name_str);
        compiler__error(c, line, col, err);
        return;
      }
    }

    /* Resolve value type: scalar keyword OR struct name. */
    JaclType val_t = TYPE_DYN;
    uint32_t val_type_idx;
    bool val_is_scalar = false;
    if (is_type_keyword(val_name_str, val_name_len)) {
      val_t = type_from_keyword(val_name_str, val_name_len);
      if (!compiler__is_typed_collection_scalar(val_t)) {
        char err[160];
        snprintf(err, sizeof(err),
                 "[Map %.*s %.*s]: only numeric value-type scalars supported as values",
                 (int)key_name_len, key_name_str,
                 (int)val_name_len, val_name_str);
        compiler__error(c, line, col, err);
        return;
      }
      val_type_idx = COMPILER_SCALAR_TYPE_IDX(val_t);
      val_is_scalar = true;
    } else {
      val_type_idx = struct_registry__find(reg, val_name_str, val_name_len);
      if (val_type_idx == UINT32_MAX) {
        char err[128];
        snprintf(err, sizeof(err), "[Map %.*s %.*s]: unknown value type '%.*s'",
                 (int)key_name_len, key_name_str,
                 (int)val_name_len, val_name_str,
                 (int)val_name_len, val_name_str);
        compiler__error(c, line, col, err);
        return;
      }
    }

    if (argc % 2 != 0) {
      compiler__error(c, line, col, "[Map K V ...] requires an even number of arguments (key-value pairs)");
      return;
    }
    uint32_t pair_count = argc / 2;
    if (pair_count > 255) {
      compiler__error(c, line, col, "[Map K V ...] too many initial pairs (max 255)");
      return;
    }
    for (uint32_t i = 0; i < pair_count; i++) {
      /* key */
      compiler__compile_node(c, args[i * 2]);
      AstNode* k_node = args[i * 2];
      bool k_ok = key_is_scalar
        ? ((JaclType)k_node->inferred_type == key_t)
        : ((JaclType)k_node->inferred_type == TYPE_STRUCT &&
           k_node->inferred_struct_idx == key_type_idx);
      if (!k_ok) {
        char err[160];
        jacl_format_typed_map_kv(err, sizeof(err),
            key_name_str, key_name_len,
            val_name_str, val_name_len, i, false);
        compiler__error(c, line, col, err);
        return;
      }
      /* value */
      compiler__compile_node(c, args[i * 2 + 1]);
      AstNode* v_node = args[i * 2 + 1];
      bool v_ok = val_is_scalar
        ? ((JaclType)v_node->inferred_type == val_t)
        : ((JaclType)v_node->inferred_type == TYPE_STRUCT &&
           v_node->inferred_struct_idx == val_type_idx);
      if (!v_ok) {
        char err[160];
        jacl_format_typed_map_kv(err, sizeof(err),
            key_name_str, key_name_len,
            val_name_str, val_name_len, i, true);
        compiler__error(c, line, col, err);
        return;
      }
    }
    compiler__emit_byte(c, OP_TYPED_MAP, line);
    compiler__emit_u16(c, (uint16_t)val_type_idx, line);
    compiler__emit_u16(c, (uint16_t)key_type_idx, line);
    compiler__emit_byte(c, (uint8_t)pair_count, line);
    c->last_expr_type = TYPE_TYPED_MAP;
    return;
  }

  /* --- Spread call path: handles both builtins and user procs --- */
  if (has_spread) {
    /* Check for known binary builtins → use OP_FOLD_SPREAD */
    int fold_op = -1;
    if (hid == HEAD_PLUS)      fold_op = 0;
    else if (hid == HEAD_STAR) fold_op = 2;
    else if (hid == HEAD_MINUS) fold_op = 1;
    else if (hid == HEAD_SLASH) fold_op = 3;

    /* vec with spread args → OP_VEC_SPREAD */
    if (hid == HEAD_VEC) {
      uint8_t fixed_args = 0;
      uint8_t num_spreads = 0;
      for (uint32_t i = 0; i < argc; i++) {
        if (args[i]->type == AST_SPREAD) {
          compiler__compile_node(c, args[i]->data.spread.expr);
          compiler__emit_byte(c, OP_SPREAD, line);
          num_spreads++;
        } else {
          compiler__compile_node(c, args[i]);
          if (compiler__reject_bare_typed(c, args[i], line, col, "dyn vec")) return;
          fixed_args++;
        }
      }
      compiler__emit_byte(c, OP_VEC_SPREAD, line);
      compiler__emit_byte(c, fixed_args, line);
      compiler__emit_byte(c, num_spreads, line);
      c->last_expr_type = TYPE_DYN;
      return;
    }

    if (fold_op >= 0) {
      /* Compile all args (fixed + spread) onto stack */
      uint8_t fixed_args = 0;
      uint8_t num_spreads = 0;
      for (uint32_t i = 0; i < argc; i++) {
        if (args[i]->type == AST_SPREAD) {
          compiler__compile_node(c, args[i]->data.spread.expr);
          compiler__emit_byte(c, OP_SPREAD, line);
          num_spreads++;
        } else {
          compiler__compile_node(c, args[i]);
          fixed_args++;
        }
      }
      compiler__emit_byte(c, OP_FOLD_SPREAD, line);
      compiler__emit_byte(c, (uint8_t)fold_op, line);
      compiler__emit_byte(c, fixed_args, line);
      compiler__emit_byte(c, num_spreads, line);
      c->last_expr_type = TYPE_DYN;
      return;
    }

    /* Generic spread call: resolve head as callable, args, then OP_CALL_SPREAD */
    if (head->type == AST_LIT_STRING) {
      uint32_t name_len = head->data.lit_string.length;
      if (name_len > 128) {
        compiler__error(c, line, col, "command name exceeds 128-byte limit");
        return;
      }
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, head->data.lit_string.value, name_len);
      int local_slot = compiler__resolve_local(c, name_val);
      if (local_slot != -1) {
        if (c->locals[local_slot].is_mutable) {
          compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
        } else {
          compiler__emit_byte(c, OP_GET_LOCAL, line);
        }
        compiler__emit_byte(c, (uint8_t)local_slot, line);
      } else {
        JaclVal gkey = compiler__global_name_val(c,
            head->data.lit_string.value, name_len);
        uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
        compiler__emit_global_op(c, OP_GET_GLOBAL, name_idx, line);
      }
    } else {
      compiler__compile_node(c, head);
    }
    uint8_t fixed_args = 0;
    uint8_t num_spreads = 0;
    for (uint32_t i = 0; i < argc; i++) {
      if (args[i]->type == AST_SPREAD) {
        compiler__compile_node(c, args[i]->data.spread.expr);
        compiler__emit_byte(c, OP_SPREAD, line);
        num_spreads++;
      } else {
        compiler__compile_node(c, args[i]);
        fixed_args++;
      }
    }
    compiler__emit_byte(c, OP_CALL_SPREAD, line);
    compiler__emit_byte(c, fixed_args, line);
    compiler__emit_byte(c, num_spreads, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* --- Sandbox mode: prelude controls name resolution ---
   * When compiling under a prelude (has_prelude), non-core builtins are
   * resolved via the prelude map.  If the prelude value is a native fn ref,
   * we emit the direct opcode (fast path).  If it's a closure or other value,
   * we emit OP_GET_GLOBAL + args + OP_CALL (runtime dispatch).
   * Non-core builtins absent from the prelude produce a compile error. */
  if (c->has_prelude && head->type == AST_LIT_STRING) {
    const char *hname = head->data.lit_string.value;
    uint32_t hlen = head->data.lit_string.length;
    if (!compiler__is_core_builtin(hname, hlen)) {
      /* Non-core builtin: must be in prelude globals to be callable */
      JaclVal nv = compiler__name_val(c->heap, c->intern_table, hname, hlen);
      GlobalArity* ga = compiler__find_global(c, nv);
      if (!ga) {
        /* REPL shell fallback: if enabled and exec is available,
         * treat unknown commands as shell commands (like !cmd args...) */
        if (c->shell_fallback) {
          JaclVal exec_name = compiler__name_val(c->heap, c->intern_table, "exec", 4);
          GlobalArity* exec_ga = compiler__find_global(c, exec_name);
          if (exec_ga) {
            /* Emit shell command: compile head + args into vector, then OP_EXEC */
            JaclVal cmd_str = compiler__name_val(c->heap, c->intern_table, hname, hlen);
            uint16_t cmd_idx = chunk_add_constant(c->chunk, cmd_str);
            compiler__emit_byte(c, OP_CONST, line);
            compiler__emit_u16(c, cmd_idx, line);
            for (uint32_t i = 0; i < argc; i++) {
              compiler__compile_node(c, args[i]);
              compiler__ensure_boxed(c, line);
            }
            uint32_t total_elems = 1 + argc;
            if (total_elems > 255) {
              compiler__error(c, line, col, "too many arguments to shell command");
              return;
            }
            compiler__emit_byte(c, OP_VEC, line);
            compiler__emit_byte(c, (uint8_t)total_elems, line);
            compiler__emit_byte(c, OP_EXEC, line);
            compiler__emit_byte(c, 0, line);  /* Basic mode: flags = 0 */
            c->last_expr_type = TYPE_STREAM;
            return;
          }
        }
        char err_msg[160];
        snprintf(err_msg, sizeof(err_msg),
                 "undefined name '%.*s'", (int)hlen, hname);
        compiler__error(c, line, col, err_msg);
        return;
      }
      /* Check if prelude value is a native fn ref — if so, emit direct opcode */
      if (ga->prelude_is_native_fn) {
        /* Native fn ref: fall through to normal builtin opcode emission below.
         * The name matches the builtin, so direct opcode is correct. */
      } else {
        /* Closure or other value: emit OP_GET_GLOBAL + args + OP_CALL.
         * Runtime looks up the prelude-seeded env value and calls it. */
        JaclVal gkey = compiler__global_name_val(c, hname, hlen);
        uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
        compiler__emit_global_op(c, OP_GET_GLOBAL, name_idx, line);
        for (uint32_t i = 0; i < argc; i++) {
          compiler__compile_node(c, args[i]);
        }
        compiler__emit_byte(c, OP_CALL, line);
        compiler__emit_byte(c, (uint8_t)argc, line);
        c->last_expr_type = TYPE_DYN;
        return;
      }
    }
  }

  /* --- Operator forms from uniform parsing --- */

  /* = → def (immutable binding) */
  if (hid == HEAD_EQUALS) {
    if (argc != 2) {
      compiler__error(c, line, col, "'=' requires exactly 2 operands");
      return;
    }
    compiler__rewrite_binding_op(c, node, "def", 3);
    return;
  }

  /* : → mut (mutable binding) */
  if (hid == HEAD_COLON) {
    if (argc != 2) {
      compiler__error(c, line, col, "':' requires exactly 2 operands");
      return;
    }
    compiler__rewrite_binding_op(c, node, "mut", 3);
    return;
  }

  /* :: → set (reassignment) */
  if (hid == HEAD_COLON_COLON) {
    if (argc != 2) {
      compiler__error(c, line, col, "'::' requires exactly 2 operands");
      return;
    }
    compiler__rewrite_binding_op(c, node, "set", 3);
    return;
  }

  /* | → pipe threading */
  if (hid == HEAD_PIPE) {
    if (argc != 2) {
      compiler__error(c, line, col, "'|' requires exactly 2 operands");
      return;
    }
    compiler__compile_pipe_op(c, node);
    return;
  }

  /* && → short-circuit logical AND: if LHS { RHS } { false } */
  if (hid == HEAD_AMP_AMP) {
    if (argc != 2) {
      compiler__error(c, line, col, "'&&' requires exactly 2 operands");
      return;
    }
    compiler__compile_node(c, args[0]);
    uint32_t false_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);
    compiler__compile_node(c, args[1]);
    uint32_t end_jump = compiler__emit_jump(c, OP_JUMP, line);
    compiler__patch_jump(c, false_jump);
    compiler__emit_byte(c, OP_FALSE, line);
    compiler__patch_jump(c, end_jump);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* || → short-circuit logical OR: if LHS { true } { RHS } */
  if (hid == HEAD_PIPE_PIPE) {
    if (argc != 2) {
      compiler__error(c, line, col, "'||' requires exactly 2 operands");
      return;
    }
    compiler__compile_node(c, args[0]);
    uint32_t true_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);
    compiler__emit_byte(c, OP_TRUE, line);
    uint32_t end_jump = compiler__emit_jump(c, OP_JUMP, line);
    compiler__patch_jump(c, true_jump);
    compiler__compile_node(c, args[1]);
    compiler__patch_jump(c, end_jump);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* ~ → logical NOT: if expr { false } { true } */
  if (hid == HEAD_TILDE && argc == 1) {
    compiler__compile_node(c, args[0]);
    uint32_t false_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);
    compiler__emit_byte(c, OP_FALSE, line);
    uint32_t end_jump = compiler__emit_jump(c, OP_JUMP, line);
    compiler__patch_jump(c, false_jump);
    compiler__emit_byte(c, OP_TRUE, line);
    compiler__patch_jump(c, end_jump);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* Arithmetic + comparison binary ops with uniform shape:
   *   exactly 2 args, dispatch to compile_binary with the matching opcode.
   * HEAD_MINUS and HEAD_EQ_EQ have extra paths and stay below. */
  {
    static const struct { HeadId hid; const char* name; uint8_t op;
                          const char* verb; } binop_table[] = {
      { HEAD_PLUS,    "+",  OP_ADD, "add"      },
      { HEAD_STAR,    "*",  OP_MUL, "multiply" },
      { HEAD_SLASH,   "/",  OP_DIV, "divide"   },
      { HEAD_PERCENT, "%",  OP_MOD, "modulo"   },
      { HEAD_LT,      "<",  OP_LT,  "compare"  },
      { HEAD_GT,      ">",  OP_GT,  "compare"  },
      { HEAD_LE,      "<=", OP_LE,  "compare"  },
      { HEAD_GE,      ">=", OP_GE,  "compare"  },
    };
    for (size_t bi = 0; bi < sizeof(binop_table)/sizeof(binop_table[0]); bi++) {
      if (hid != binop_table[bi].hid) continue;
      if (argc != 2) {
        compiler__builtin_arity_error(c, line, col,
            binop_table[bi].name, "2 arguments", argc);
        return;
      }
      compiler__compile_binary(c, args, binop_table[bi].op,
                               binop_table[bi].verb, line, col);
      return;
    }
  }

  if (hid == HEAD_MINUS) {
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
      JaclType arg_type = (JaclType)args[0]->inferred_type;
      if (arg_type == TYPE_U64) {
        compiler__error(c, line, col, "type error: cannot negate u64");
        return;
      }
      if (arg_type == TYPE_I64) {
        compiler__emit_byte(c, OP_NEG_I64, line);
      } else if (arg_type == TYPE_F64) {
        compiler__emit_byte(c, OP_NEG_F64, line);
      } else {
        compiler__emit_byte(c, OP_NEG, line);
      }
      c->last_expr_type = arg_type;
    } else if (argc == 2) {
      compiler__compile_binary(c, args, OP_SUB, "subtract", line, col);
    } else {
      compiler__builtin_arity_error(c, line, col, "-", "1 or 2 arguments", argc);
    }
    return;
  }
  /* Comparison builtins */
  if (hid == HEAD_EQ_EQ) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "==", "2 arguments", argc); return; }
    /* US-013: Check for inline struct comparison — avoid materialization */
    if (args[0]->type == AST_VAR_REF && args[1]->type == AST_VAR_REF) {
      JaclVal name_a = compiler__name_val(c->heap, c->intern_table,
          args[0]->data.var_ref.name, args[0]->data.var_ref.length);
      JaclVal name_b = compiler__name_val(c->heap, c->intern_table,
          args[1]->data.var_ref.name, args[1]->data.var_ref.length);
      int slot_a = compiler__resolve_local(c, name_a);
      int slot_b = compiler__resolve_local(c, name_b);
      if (slot_a != -1 && slot_b != -1 &&
          c->locals[slot_a].type == TYPE_STRUCT && c->locals[slot_a].is_inline &&
          c->locals[slot_b].type == TYPE_STRUCT && c->locals[slot_b].is_inline) {
        if (c->locals[slot_a].struct_type_idx != c->locals[slot_b].struct_type_idx) {
          compiler__error(c, line, col,
              "type error: cannot compare different struct types");
          return;
        }
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        uint32_t sidx = c->locals[slot_a].struct_type_idx;
        if (reg && sidx < reg->count && reg->defs[sidx]) {
          uint16_t total_size = (uint16_t)reg->defs[sidx]->total_size;
          compiler__emit_byte(c, OP_STRUCT_EQ_INLINE, line);
          compiler__emit_byte(c, (uint8_t)slot_a, line);
          compiler__emit_byte(c, (uint8_t)slot_b, line);
          compiler__emit_u16(c, total_size, line);
          c->last_expr_type = TYPE_BOOL;
          return;
        }
      }
    }
    compiler__compile_binary(c, args, OP_EQ, "compare", line, col);
    return;
  }
  /* Range operators: ..< (exclusive) and ..= (inclusive) */
  if (hid == HEAD_DOTDOT_LT || hid == HEAD_DOTDOT_EQ) {
    const char* rname = (hid == HEAD_DOTDOT_LT) ? "..<" : "..=";
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, rname, "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_RANGE, line);
    compiler__emit_byte(c, (hid == HEAD_DOTDOT_EQ) ? 1 : 0, line);
    c->last_expr_type = TYPE_STREAM;
    return;
  }

  /* Assert builtin: 1 arg (condition). On falsy, OP_ASSERT halts the VM
     with "assertion failed" at the source line of the assert call. */
  if (hid == HEAD_ASSERT) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "assert", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_ASSERT, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* [assert-type EXPR TYPE] — compile-time static type check.
   * The typer pass has already compared the expression's inferred type
   * to TYPE and emitted any mismatch error. Here we just emit a nil
   * placeholder so the form has a value at the bytecode level; the
   * expression itself is intentionally NOT compiled (no runtime
   * evaluation, no side effects, no operand-stack cost). */
  if (hid == HEAD_ASSERT_TYPE) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "assert-type",
                                     "2 arguments", argc);
      return;
    }
    compiler__emit_byte(c, OP_NIL, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* Print builtin */
  if (hid == HEAD_PRINT) {
    if (argc != 1) { compiler__builtin_arity_error(c, line, col, "print", "1 argument", argc); return; }
    compiler__compile_node(c, args[0]);
    JaclType arg_type = (JaclType)args[0]->inferred_type;
    uint32_t arg_struct_idx = args[0]->inferred_struct_idx;
    if (arg_type == TYPE_TYPED_VEC) {
      compiler__emit_byte(c, OP_TYPED_VEC_PRINT, line);
      compiler__emit_u16(c, (uint16_t)arg_struct_idx, line);
      c->last_expr_type = TYPE_NIL;
      return;
    }
    if (arg_type == TYPE_TYPED_MAP) {
      compiler__emit_byte(c, OP_TYPED_MAP_PRINT, line);
      compiler__emit_u16(c, (uint16_t)arg_struct_idx, line);
      compiler__emit_u16(c, (uint16_t)args[0]->inferred_key_struct_idx, line);
      c->last_expr_type = TYPE_NIL;
      return;
    }
    if (arg_type == TYPE_STRUCT && arg_struct_idx != UINT32_MAX) {
      /* Typed struct print — no heap reify, formatter walks inline bytes. */
      compiler__emit_byte(c, OP_PRINT_STRUCT, line);
      compiler__emit_u16(c, (uint16_t)arg_struct_idx, line);
      c->inline_repr = INLINE_NONE;
      c->last_expr_type = TYPE_NIL;
      return;
    }
    if (arg_type == TYPE_PTR && arg_struct_idx != UINT32_MAX &&
        arg_struct_idx < UINT16_MAX) {
      /* Typed pointer print — boxed u64 + pointee idx → "Ptr<T>(0xADDR)".
       * Falls through to OP_PRINT below when pointee is unknown
       * (UINT32_MAX) so the user still gets the raw address. */
      compiler__ensure_boxed(c, line);
      compiler__emit_byte(c, OP_PRINT_PTR, line);
      compiler__emit_u16(c, (uint16_t)arg_struct_idx, line);
      c->last_expr_type = TYPE_NIL;
      return;
    }
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_PRINT, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* deref builtin: scalar-element box → OP_DEREF (runtime tags the
   * scalar appropriately via the box's stored value); struct-element
   * box → OP_DEREF_INLINE (materializes inline struct bytes on TOS,
   * mirroring the unbox path's struct-box branch). The typer
   * narrows the AST node so this read is sufficient — no need to
   * walk narrowings here. */
  if (hid == HEAD_DEREF) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "deref", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    JaclType arg_t = (JaclType)args[0]->inferred_type;
    uint32_t arg_sidx = args[0]->inferred_struct_idx;
    if (arg_t == TYPE_BOX && arg_sidx != UINT32_MAX &&
        !JACL_IS_SCALAR_TYPE_IDX(arg_sidx)) {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      StructTypeDef* sdef = (reg && arg_sidx < reg->count) ? reg->defs[arg_sidx] : NULL;
      if (struct_def_is_user(sdef, reg)) {
        compiler__emit_byte(c, OP_DEREF_INLINE, line);
        compiler__emit_u16(c, (uint16_t)arg_sidx, line);
        c->last_expr_type = TYPE_STRUCT;
        c->inline_repr = INLINE_STACK;
        return;
      }
    }
    compiler__emit_byte(c, OP_DEREF, line);
    return;
  }

  /* length builtin */
  /* Table-driven dispatch for "compile single arg → emit one-byte op →
   * set result type" builtins. set_type=false means leave last_expr_type
   * unchanged (matches the few branches that don't reassign it). */
  {
    static const struct { HeadId hid; const char* name;
                          uint8_t op; JaclType result; bool set_type; } unary_emit[] = {
      { HEAD_LENGTH,      "length",      OP_STR_LEN,      TYPE_I32,    true  },
      { HEAD_BYTE_LENGTH, "byte-length", OP_STR_BYTE_LEN, TYPE_I32,    true  },
      { HEAD_ATOM_Q,      "atom?",       OP_IS_ATOM,      TYPE_BOOL,   true  },
      { HEAD_FUTURE_Q,    "future?",     OP_IS_FUTURE,    TYPE_BOOL,   true  },
      /* HEAD_DEREF is split out below to handle the struct-element
       * box case via OP_DEREF_INLINE (matches the unbox path's
       * narrowed struct-box branch in compile_command). */
      { HEAD_ERROR,       "error",       OP_ERROR,        TYPE_DYN,    false },
      { HEAD_ERROR_Q,     "error?",      OP_IS_ERROR,     TYPE_BOOL,   true  },
      { HEAD_ERROR_VAL,   "error-val",   OP_ERROR_VAL,    TYPE_DYN,    true  },
      { HEAD_STREAM_NEXT, "stream_next", OP_STREAM_NEXT,  TYPE_DYN,    true  },
      { HEAD_COLLECT,     "collect",     OP_COLLECT,      TYPE_VEC,    true  },
      { HEAD_COUNT,       "count",       OP_COUNT,        TYPE_I32,    true  },
      { HEAD_FIRST,       "first",       OP_FIRST,        TYPE_DYN,    true  },
      { HEAD_LINES,       "lines",       OP_LINES,        TYPE_STREAM, true  },
    };
    for (size_t ui = 0; ui < sizeof(unary_emit)/sizeof(unary_emit[0]); ui++) {
      if (hid != unary_emit[ui].hid) continue;
      if (argc != 1) {
        compiler__builtin_arity_error(c, line, col,
            unary_emit[ui].name, "1 argument", argc);
        return;
      }
      compiler__compile_node(c, args[0]);
      compiler__emit_byte(c, unary_emit[ui].op, line);
      if (unary_emit[ui].set_type) c->last_expr_type = unary_emit[ui].result;
      return;
    }
  }

  /* index builtin */
  if (hid == HEAD_INDEX) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "index", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_STR_INDEX, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* slice builtin (2 or 3 args) */
  if (hid == HEAD_SLICE) {
    if (argc != 2 && argc != 3) {
      compiler__builtin_arity_error(c, line, col, "slice", "2 or 3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    if (argc == 3) {
      compiler__compile_node(c, args[2]);
    } else {
      /* 2-arg form: nil sentinel means "to end of string" */
      compiler__emit_byte(c, OP_NIL, line);
    }
    compiler__emit_byte(c, OP_STR_SLICE, line);
    c->last_expr_type = TYPE_STR;
    return;
  }

  /* concat builtin (variadic: 2+ args) */
  if (hid == HEAD_CONCAT) {
    if (argc < 2) {
      compiler__builtin_arity_error(c, line, col, "concat", "at least 2 arguments", argc);
      return;
    }
    /* Compile first two args, emit OP_CONCAT */
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_CONCAT, line);
    /* Each subsequent arg: compile, emit OP_CONCAT (pairwise chain) */
    for (uint32_t i = 2; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      compiler__emit_byte(c, OP_CONCAT, line);
    }
    c->last_expr_type = TYPE_STR;
    return;
  }

  /* mut — mutable local binding with cell auto-boxing */
  if (hid == HEAD_MUT) {
    /* --- Destructuring: [mut [a b c] value] or [mut DESTRUCTURE_VEC value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_VEC) {
      compiler__compile_destructure_vec(
          c,
          args[0]->data.destructure_vec.names,
          args[0]->data.destructure_vec.name_lens,
          args[0]->data.destructure_vec.types,
          args[0]->data.destructure_vec.type_lens,
          args[0]->data.destructure_vec.count,
          args[0]->data.destructure_vec.rest_name,
          args[0]->data.destructure_vec.rest_name_len,
          args[1], true, line, col);
      return;
    }
    if (argc == 2 && args[0]->type == AST_COMMAND) {
      /* keyword form: mut [a b c] expr — convert AST_COMMAND to name arrays
       * Also handles rest patterns: [mut [head ..rest] expr] */
      AstNode* pat = args[0];
      uint32_t total_elems = 1 + pat->data.command.arg_count;
      if (total_elems > 255) {
        compiler__error(c, line, col, "too many bindings in destructuring");
        return;
      }
      const char* d_names[256];
      uint32_t d_name_lens[256];
      const char* rest_name = NULL;
      uint32_t rest_name_len = 0;
      int rest_seen = 0;
      uint32_t d_count = 0;
      if (pat->data.command.head->type == AST_SPREAD) {
        AstNode* inner = pat->data.command.head->data.spread.expr;
        if (inner && inner->type == AST_LIT_STRING) {
          rest_name = inner->data.lit_string.value;
          rest_name_len = inner->data.lit_string.length;
        }
        rest_seen = 1;
      } else if (pat->data.command.head->type == AST_LIT_STRING &&
                 pat->data.command.head->data.lit_string.length == 2 &&
                 pat->data.command.head->data.lit_string.value[0] == '.' &&
                 pat->data.command.head->data.lit_string.value[1] == '.') {
        /* ".." as head — [..rest ...] or [.. ...] */
        rest_seen = 1;
        if (pat->data.command.arg_count >= 1 &&
            pat->data.command.args[0]->type == AST_LIT_STRING) {
          rest_name = pat->data.command.args[0]->data.lit_string.value;
          rest_name_len = pat->data.command.args[0]->data.lit_string.length;
          for (uint32_t i = 1; i < pat->data.command.arg_count; i++) {
            if (pat->data.command.args[i]->type == AST_LIT_STRING) {
              d_names[d_count] = pat->data.command.args[i]->data.lit_string.value;
              d_name_lens[d_count] = pat->data.command.args[i]->data.lit_string.length;
              d_count++;
            }
          }
        }
        if (d_count > 0) {
          compiler__error(c, line, col,
                          "rest pattern '..' must be the last element in destructuring");
          return;
        }
        compiler__compile_destructure_vec(
            c, d_names, d_name_lens, NULL, NULL, d_count,
            rest_name, rest_name_len,
            args[1], true, line, col);
        return;
      } else if (pat->data.command.head->type != AST_LIT_STRING) {
        compiler__error(c, line, col,
                        "destructuring pattern elements must be names");
        return;
      } else {
        d_names[0] = pat->data.command.head->data.lit_string.value;
        d_name_lens[0] = pat->data.command.head->data.lit_string.length;
        d_count = 1;
      }
      for (uint32_t i = 0; i < pat->data.command.arg_count; i++) {
        AstNode* elem = pat->data.command.args[i];
        if (elem->type == AST_SPREAD) {
          if (rest_seen) {
            compiler__error(c, line, col,
                            "duplicate rest pattern '..' in destructuring");
            return;
          }
          AstNode* inner = elem->data.spread.expr;
          if (inner && inner->type == AST_LIT_STRING) {
            rest_name = inner->data.lit_string.value;
            rest_name_len = inner->data.lit_string.length;
          }
          rest_seen = 1;
        } else if (elem->type == AST_LIT_STRING) {
          if (rest_seen) {
            compiler__error(c, line, col,
                            "rest pattern '..' must be the last element in destructuring");
            return;
          }
          d_names[d_count] = elem->data.lit_string.value;
          d_name_lens[d_count] = elem->data.lit_string.length;
          d_count++;
        } else {
          compiler__error(c, line, col,
                          "destructuring pattern elements must be names");
          return;
        }
      }
      if (rest_seen && pat->data.command.head->type == AST_SPREAD &&
          pat->data.command.arg_count > 0) {
        compiler__error(c, line, col,
                        "rest pattern '..' must be the last element in destructuring");
        return;
      }
      compiler__compile_destructure_vec(
          c, d_names, d_name_lens, NULL, NULL, d_count,
          rest_name, rest_name_len,
          args[1], true, line, col);
      return;
    }
    /* --- Named destructuring: [mut {x, y} value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_NAMED) {
      compiler__compile_destructure_named(
          c,
          args[0]->data.destructure_named.names,
          args[0]->data.destructure_named.name_lens,
          args[0]->data.destructure_named.types,
          args[0]->data.destructure_named.type_lens,
          args[0]->data.destructure_named.count,
          args[0]->data.destructure_named.rest_name,
          args[0]->data.destructure_named.rest_name_len,
          args[0]->data.destructure_named.spread_all,
          args[1], true, line, col);
      return;
    }
    /* --- Named destructuring from block: [mut {x, y} value] (keyword form)
     * Also handles rest patterns: [mut {x, ..rest} value]
     * spread-all: [mut {..} value]
     * and typed fields: [mut {i32 x, i32 y} value] --- */
    if (argc == 2 && args[0]->type == AST_BLOCK) {
      AstNode* blk = args[0];
      uint32_t blk_count = blk->data.block.count;
      if (blk_count == 0 || blk_count > 255) {
        compiler__error(c, line, col, "invalid destructuring pattern");
        return;
      }
      const char* d_names_arr[256];
      uint32_t d_name_lens_arr[256];
      const char* d_types_arr[256];
      uint32_t d_type_lens_arr[256];
      const char* rest_nm = NULL;
      uint32_t rest_nm_len = 0;
      int spread_all_flag = 0;
      int has_types = 0;
      uint32_t d_count = 0;
      int valid = 1;
      for (uint32_t i = 0; i < blk_count; i++) {
        AstNode* cmd = blk->data.block.commands[i];
        /* Handle bare ".." token (AST_LIT_STRING, not wrapped in AST_COMMAND)
           — this is how {..} parses since ".." is not a TOKEN_WORD */
        if (cmd->type == AST_LIT_STRING &&
            cmd->data.lit_string.length == 2 &&
            cmd->data.lit_string.value[0] == '.' &&
            cmd->data.lit_string.value[1] == '.') {
          spread_all_flag = 1;
          continue;
        }
        if (cmd->type != AST_COMMAND) { valid = 0; break; }
        const char* head_str = NULL;
        uint32_t head_len = 0;
        if (cmd->data.command.head->type == AST_LIT_STRING) {
          head_str = cmd->data.command.head->data.lit_string.value;
          head_len = cmd->data.command.head->data.lit_string.length;
        } else {
          valid = 0; break;
        }
        /* Check for rest pattern: ..name (bare command form in block) */
        if (head_len == 2 && head_str[0] == '.' && head_str[1] == '.') {
          if (cmd->data.command.arg_count == 0) {
            /* spread-all: {..} — head is ".." in a command with no args */
            spread_all_flag = 1;
          } else if (cmd->data.command.arg_count == 1 &&
                     cmd->data.command.args[0]->type == AST_LIT_STRING) {
            /* rest pattern: ..name */
            rest_nm = cmd->data.command.args[0]->data.lit_string.value;
            rest_nm_len = cmd->data.command.args[0]->data.lit_string.length;
          } else {
            valid = 0; break;
          }
          continue;
        }
        /* Check for typed field: type name (head=type, args=[name]) */
        if (cmd->data.command.arg_count == 1 &&
            cmd->data.command.args[0]->type == AST_LIT_STRING) {
          d_types_arr[d_count] = head_str;
          d_type_lens_arr[d_count] = head_len;
          d_names_arr[d_count] = cmd->data.command.args[0]->data.lit_string.value;
          d_name_lens_arr[d_count] = cmd->data.command.args[0]->data.lit_string.length;
          has_types = 1;
          d_count++;
        } else if (cmd->data.command.arg_count == 0) {
          /* simple name */
          d_names_arr[d_count] = head_str;
          d_name_lens_arr[d_count] = head_len;
          d_types_arr[d_count] = NULL;
          d_type_lens_arr[d_count] = 0;
          d_count++;
        } else {
          valid = 0; break;
        }
      }
      if (valid) {
        compiler__compile_destructure_named(
            c, d_names_arr, d_name_lens_arr,
            has_types ? d_types_arr : NULL,
            has_types ? d_type_lens_arr : NULL,
            d_count,
            rest_nm, rest_nm_len, spread_all_flag,
            args[1], true, line, col);
        return;
      }
    }

    JaclType declared_type = TYPE_DYN;
    uint32_t name_arg_idx  = 0;
    uint32_t value_arg_idx = 1;

    if (argc == 3) {
      /* Typed mut: [mut TYPE name value] */
      if (args[0]->type != AST_LIT_STRING) {
        compiler__error(c, line, col, "mut type must be a keyword");
        return;
      }
      uint32_t first_len = args[0]->data.lit_string.length;
      const char* first_str = args[0]->data.lit_string.value;
      if (!compiler__resolve_type(c, first_str, first_len, &declared_type)) {
        compiler__error(c, line, col, "mut with 3 arguments requires type keyword as first argument");
        return;
      }
      name_arg_idx   = 1;
      value_arg_idx  = 2;
    } else if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "mut", "2 or 3 arguments", argc);
      return;
    }

    /* US-013: accept either AST_LIT_STRING (normal mut name) or
       AST_VAR_REF with is_caret set (^name inside syntax-quote, which
       introduces a binding in the caller's scope for anaphoric macros).
       US-014: also accept AST_VAR_REF with is_gensym set (gensym-produced
       unique names). Gensym bindings stay at the var-ref's scope mark. */
    const char* bind_name_ptr;
    uint32_t    name_len;
    uint32_t    bind_scope_mark;
    if (args[name_arg_idx]->type == AST_LIT_STRING) {
      bind_name_ptr   = args[name_arg_idx]->data.lit_string.value;
      name_len        = args[name_arg_idx]->data.lit_string.length;
      bind_scope_mark = args[name_arg_idx]->scope_mark;
    } else if (args[name_arg_idx]->type == AST_VAR_REF &&
               args[name_arg_idx]->is_caret) {
      bind_name_ptr   = args[name_arg_idx]->data.var_ref.name;
      name_len        = args[name_arg_idx]->data.var_ref.length;
      bind_scope_mark = 0;  /* ^name → caller's scope */
    } else if (args[name_arg_idx]->type == AST_VAR_REF &&
               args[name_arg_idx]->is_gensym) {
      bind_name_ptr   = args[name_arg_idx]->data.var_ref.name;
      name_len        = args[name_arg_idx]->data.var_ref.length;
      bind_scope_mark = args[name_arg_idx]->scope_mark;
    } else {
      compiler__error(c, line, col, "mut name must be a string");
      return;
    }
    if (name_len > 128) {
      compiler__error(c, line, col, "variable name exceeds 128-byte limit");
      return;
    }
    if (name_len == 3 && memcmp(bind_name_ptr, "ctx", 3) == 0) {
      compiler__error(c, line, col, "'ctx' is reserved");
      return;
    }

    /* Compile the value expression with type context */
    compiler__compile_node(c, args[value_arg_idx]);
    JaclType rhs_type = (JaclType)args[value_arg_idx]->inferred_type;
    uint32_t rhs_struct_idx = args[value_arg_idx]->inferred_struct_idx;

    /* Type check for typed mut */
    if (declared_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != declared_type) {
      char err_msg[128];
      snprintf(err_msg, sizeof(err_msg), "type error: expected %s, got %s",
               type_name(declared_type), type_name(rhs_type));
      compiler__error(c, line, col, err_msg);
      return;
    }
    if (declared_type != TYPE_DYN && rhs_type == TYPE_DYN) {
      char err_msg[160];
      jacl_format_assign_dyn_unnamed(err_msg, sizeof(err_msg), declared_type);
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Reject mut-bound struct: cells store JaclVals (8 bytes), so a struct
       binding would need heap reification. Per design, mut struct bindings
       must use [box $val] explicitly. */
    if (rhs_type == TYPE_STRUCT) {
      compiler__error(c, line, col,
                      "cannot bind struct to a mut variable — wrap with "
                      "[box $val] (mut p [box [Point 1 2]]) so the binding "
                      "holds a box reference");
      return;
    }

    /* Determine effective type: declared type wins, else infer unboxed/struct from RHS */
    JaclType effective_type;
    if (declared_type != TYPE_DYN) {
      effective_type = declared_type;
    } else if (is_unboxed_type(rhs_type) || rhs_type == TYPE_STRUCT ||
               rhs_type == TYPE_STREAM || is_typed_collection(rhs_type)) {
      effective_type = rhs_type;
    } else {
      effective_type = TYPE_DYN;
    }

    JaclVal name_val = compiler__name_val(c->heap, c->intern_table, bind_name_ptr, name_len);

    if (c->sm_analysis) {
      /* SM mode: wrap value in a cell and store in state field.
         This preserves shared-mutation semantics (FR-5) — nested
         closures capture the cell pointer, not a snapshot. */
      int field_idx = sm__find_field(&c->sm_analysis->state_layout, name_val);
      if (field_idx >= 0) {
        compiler__emit_byte(c, OP_MAKE_CELL, line);
        compiler__emit_byte(c, OP_SET_STATE_FIELD, line);
        compiler__emit_byte(c, (uint8_t)field_idx, line);
        /* mut returns nil */
        compiler__emit_byte(c, OP_NIL, line);
      } else {
        /* Name not in state layout — shouldn't happen, fall through to cell */
        if (is_unboxed_type(effective_type)) {
          compiler__emit_byte(c, OP_TO_DYN, line);
          compiler__emit_byte(c, (uint8_t)effective_type, line);
        }
        compiler__emit_byte(c, OP_MAKE_CELL, line);
        {
          uint32_t prev_mark = c->current_scope_mark;
          c->current_scope_mark = bind_scope_mark;
          compiler__add_local(c, name_val, line, col);
          c->current_scope_mark = prev_mark;
        }
        c->locals[c->local_count - 1].is_mutable = true;
        compiler__emit_byte(c, OP_NIL, line);
      }
    } else if (c->scope_depth > 0) {
      /* Local scope: box unboxed types for cell storage, then wrap in cell */
      if (is_unboxed_type(effective_type)) {
        compiler__emit_byte(c, OP_TO_DYN, line);
        compiler__emit_byte(c, (uint8_t)effective_type, line);
      }
      compiler__emit_byte(c, OP_MAKE_CELL, line);
      {
        /* US-013: use the name arg's scope mark (0 for ^caret, else the
           mut command's stamped mark) so caret bindings land in the
           caller's scope while normal macro bindings stay hygienic. */
        uint32_t prev_mark = c->current_scope_mark;
        c->current_scope_mark = bind_scope_mark;
        compiler__add_local(c, name_val, line, col);
        c->current_scope_mark = prev_mark;
      }
      c->locals[c->local_count - 1].is_mutable = true;
      { TypeInfo ti = { effective_type, rhs_struct_idx, args[value_arg_idx]->inferred_key_struct_idx };
        TYPEINFO_SAVE(c->locals[c->local_count - 1], ti); }
      /* mut returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* Global scope: box unboxed types before storage */
      if (is_unboxed_type(effective_type)) {
        compiler__emit_byte(c, OP_TO_DYN, line);
        compiler__emit_byte(c, (uint8_t)effective_type, line);
      }
      /* In module context, wrap mutable globals in a box so they can be
         shared across module boundaries as live references. */
      if (c->current_module) {
        compiler__emit_byte(c, OP_BOX, line);
      }
      JaclVal global_key = compiler__global_name_val(c, bind_name_ptr, name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, global_key);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      /* Record as mutable in global info with type (use plain name for metadata) */
      compiler__set_global_arity(c, name_val, -1);
      /* Walk to the entry we just set and mark mutable + type */
      {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t i = 0; i < root->global_arity_count; i++) {
          if (root->global_arities[i].name == name_val) {
            root->global_arities[i].is_mutable = true;
            { TypeInfo ti = { effective_type, rhs_struct_idx, args[value_arg_idx]->inferred_key_struct_idx };
              TYPEINFO_SAVE(root->global_arities[i], ti); }
            break;
          }
        }
      }
    }
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* set — reassign mutable binding */
  if (hid == HEAD_SET) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "set", "2 arguments", argc); return; }

    /* Arrow desugar: `set n->field value` is parsed as set([. n field], value).
       Rewrite to [. $n field value] — a 3-arg dot command (field mutation). */
    if (args[0]->type == AST_COMMAND &&
        args[0]->data.command.head->type == AST_LIT_STRING &&
        args[0]->data.command.head->data.lit_string.length == 1 &&
        args[0]->data.command.head->data.lit_string.value[0] == '.') {
      /* Unwrap the outermost dot to find the chain.
         For `n->x->y`, args[0] is [. [. n x] y].
         We need to find the innermost `n`, convert it to $n (var ref),
         then append `value` as the last arg of the outermost dot. */
      AstNode* dot_cmd = args[0];
      AstNode* value_expr = args[1];

      /* Find the innermost target: walk [. [. ... x] y] chains */
      AstNode* inner = dot_cmd;
      while (inner->data.command.args[0]->type == AST_COMMAND &&
             inner->data.command.args[0]->data.command.head->type == AST_LIT_STRING &&
             inner->data.command.args[0]->data.command.head->data.lit_string.length == 1 &&
             inner->data.command.args[0]->data.command.head->data.lit_string.value[0] == '.') {
        inner = inner->data.command.args[0];
      }
      /* inner->data.command.args[0] is the bare name "n" — convert to $n var ref */
      AstNode* bare_name = inner->data.command.args[0];
      if (bare_name->type == AST_LIT_STRING) {
        AstNode* var_ref = ast_alloc(c->arena);
        var_ref->type = AST_VAR_REF;
        var_ref->start = bare_name->start;
        var_ref->end   = bare_name->end;
        var_ref->data.var_ref.name   = bare_name->data.lit_string.value;
        var_ref->data.var_ref.length = bare_name->data.lit_string.length;
        inner->data.command.args[0] = var_ref;
      }

      /* Extend the outermost dot from 2 args to 3 args (add value) */
      AstNode** new_args = ast_alloc_array(c->arena, 3);
      new_args[0] = dot_cmd->data.command.args[0];
      new_args[1] = dot_cmd->data.command.args[1];
      new_args[2] = value_expr;
      dot_cmd->data.command.args = new_args;
      dot_cmd->data.command.arg_count = 3;

      /* Compile the rewritten dot command */
      compiler__compile_node(c, dot_cmd);
      return;
    }

    /* US-013: accept AST_LIT_STRING (normal set) or AST_VAR_REF with
       is_caret set (^name inside syntax-quote).
       US-014: also accept AST_VAR_REF with is_gensym set. */
    const char* set_name_ptr;
    uint32_t    name_len;
    if (args[0]->type == AST_LIT_STRING) {
      set_name_ptr = args[0]->data.lit_string.value;
      name_len     = args[0]->data.lit_string.length;
    } else if (args[0]->type == AST_VAR_REF &&
               (args[0]->is_caret || args[0]->is_gensym)) {
      set_name_ptr = args[0]->data.var_ref.name;
      name_len     = args[0]->data.var_ref.length;
    } else {
      compiler__error(c, line, col, "set first argument must be a name");
      return;
    }
    if (name_len > 128) {
      compiler__error(c, line, col, "variable name exceeds 128-byte limit");
      return;
    }
    JaclVal name_val = compiler__name_val(c->heap, c->intern_table, set_name_ptr, name_len);
    char err_msg[128];

    /* SM mode: write to state field (through cell if mutable) */
    if (c->sm_analysis) {
      int field_idx = sm__find_field(&c->sm_analysis->state_layout, name_val);
      if (field_idx >= 0) {
        bool is_mut = sm__is_field_mutable(&c->sm_analysis->state_layout, name_val);
        compiler__compile_node(c, args[1]);
        if (is_mut) {
          /* Write through cell with barrier; pushes NIL internally */
          compiler__emit_byte(c, OP_SET_STATE_FIELD_CELL, line);
          compiler__emit_byte(c, (uint8_t)field_idx, line);
        } else {
          compiler__emit_byte(c, OP_SET_STATE_FIELD, line);
          compiler__emit_byte(c, (uint8_t)field_idx, line);
          compiler__emit_byte(c, OP_NIL, line);
        }
        return;
      }
    }

    /* Resolve local */
    int local_slot = compiler__resolve_local(c, name_val);
    if (local_slot != -1) {
      if (c->locals[local_slot].is_mutable) {
        JaclType target_type = c->locals[local_slot].type;
        compiler__compile_node(c, args[1]);
        JaclType rhs_type = (JaclType)args[1]->inferred_type;
        /* Type check */
        if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
          jacl_format_assign_mismatch(err_msg, sizeof(err_msg),
              target_type, rhs_type, set_name_ptr, name_len);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
          jacl_format_assign_dyn_named(err_msg, sizeof(err_msg),
              target_type, set_name_ptr, name_len);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type == TYPE_DYN && rhs_type == TYPE_STRUCT) {
          jacl_format_assign_struct_to_dyn(err_msg, sizeof(err_msg),
              set_name_ptr, name_len);
          compiler__error(c, line, col, err_msg);
          return;
        }
        /* Box unboxed types for cell storage */
        if (is_unboxed_type(target_type)) {
          compiler__emit_byte(c, OP_TO_DYN, line);
          compiler__emit_byte(c, (uint8_t)target_type, line);
        }
        compiler__emit_byte(c, OP_SET_CELL_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)local_slot, line);
        return;
      }
      if (c->locals[local_slot].is_param) {
        snprintf(err_msg, sizeof(err_msg), "cannot mutate parameter '%.*s'",
                 (int)name_len, set_name_ptr);
      } else {
        snprintf(err_msg, sizeof(err_msg), "cannot mutate immutable binding '%.*s'",
                 (int)name_len, set_name_ptr);
      }
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Resolve upvalue */
    int upvalue_idx = compiler__resolve_upvalue(c, name_val, line, col);
    if (upvalue_idx != -1) {
      if (c->upvalues[upvalue_idx].is_mutable) {
        JaclType target_type = c->upvalues[upvalue_idx].type;
        compiler__compile_node(c, args[1]);
        JaclType rhs_type = (JaclType)args[1]->inferred_type;
        /* Type check */
        if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
          jacl_format_assign_mismatch(err_msg, sizeof(err_msg),
              target_type, rhs_type, set_name_ptr, name_len);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
          jacl_format_assign_dyn_named(err_msg, sizeof(err_msg),
              target_type, set_name_ptr, name_len);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type == TYPE_DYN && rhs_type == TYPE_STRUCT) {
          jacl_format_assign_struct_to_dyn(err_msg, sizeof(err_msg),
              set_name_ptr, name_len);
          compiler__error(c, line, col, err_msg);
          return;
        }
        /* Box unboxed types for cell storage */
        if (is_unboxed_type(target_type)) {
          compiler__emit_byte(c, OP_TO_DYN, line);
          compiler__emit_byte(c, (uint8_t)target_type, line);
        }
        compiler__emit_byte(c, OP_SET_CELL_UPVALUE, line);
        compiler__emit_byte(c, (uint8_t)c->upvalues[upvalue_idx].base_slot, line);
        return;
      }
      snprintf(err_msg, sizeof(err_msg), "cannot mutate immutable binding '%.*s'",
               (int)name_len, set_name_ptr);
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Resolve global */
    GlobalArity* set_ga = compiler__find_global(c, name_val);
    if (set_ga) {
      bool global_mutable = set_ga->is_mutable;
      if (global_mutable) {
        JaclType target_type = set_ga->type;
        if (c->current_module) {
          /* In module context, mutable globals are boxes — use reset!
             semantics to update the box in place (shared reference).
             Emit: GET_GLOBAL (push box), compile RHS, OP_RESET */
          JaclVal set_key = compiler__global_name_val(c, set_name_ptr, name_len);
          uint16_t name_idx = chunk_add_constant(c->chunk, set_key);
          compiler__emit_global_op(c, OP_GET_GLOBAL, name_idx, line);
          compiler__compile_node(c, args[1]);
          JaclType rhs_type = (JaclType)args[1]->inferred_type;
          if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
            jacl_format_assign_mismatch(err_msg, sizeof(err_msg),
                target_type, rhs_type, set_name_ptr, name_len);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
            jacl_format_assign_dyn_named(err_msg, sizeof(err_msg),
                target_type, set_name_ptr, name_len);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (is_unboxed_type(target_type)) {
            compiler__emit_byte(c, OP_TO_DYN, line);
            compiler__emit_byte(c, (uint8_t)target_type, line);
          }
          uint32_t reset_struct_idx = args[1]->inferred_struct_idx;
          if (rhs_type == TYPE_STRUCT && reset_struct_idx != UINT32_MAX) {
            /* Struct-box reset: inline bytes write directly. */
            compiler__emit_byte(c, OP_RESET_INLINE, line);
            compiler__emit_u16(c, (uint16_t)reset_struct_idx, line);
          } else {
            compiler__emit_byte(c, OP_RESET, line);
          }
        } else {
          compiler__compile_node(c, args[1]);
          JaclType rhs_type = (JaclType)args[1]->inferred_type;
          /* Type check */
          if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
            jacl_format_assign_mismatch(err_msg, sizeof(err_msg),
                target_type, rhs_type, set_name_ptr, name_len);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
            jacl_format_assign_dyn_named(err_msg, sizeof(err_msg),
                target_type, set_name_ptr, name_len);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type == TYPE_DYN && rhs_type == TYPE_STRUCT) {
            jacl_format_assign_struct_to_dyn(err_msg, sizeof(err_msg),
                set_name_ptr, name_len);
            compiler__error(c, line, col, err_msg);
            return;
          }
          /* Box unboxed types for global storage */
          if (is_unboxed_type(target_type)) {
            compiler__emit_byte(c, OP_TO_DYN, line);
            compiler__emit_byte(c, (uint8_t)target_type, line);
          }
          uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
          compiler__emit_global_op(c, OP_SET_GLOBAL, name_idx, line);
        }
        return;
      }
      snprintf(err_msg, sizeof(err_msg), "cannot mutate immutable binding '%.*s'",
               (int)name_len, set_name_ptr);
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Not found anywhere */
    snprintf(err_msg, sizeof(err_msg), "undefined variable '%.*s'",
             (int)name_len, set_name_ptr);
    compiler__error(c, line, col, err_msg);
    return;
  }

  /* def builtin — supports [def name value] and [def TYPE name value]
     and [def [a b c] value] for vector destructuring */
  if (hid == HEAD_DEF) {
    /* --- Destructuring: [def [a b c] value] or [def DESTRUCTURE_VEC value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_VEC) {
      compiler__compile_destructure_vec(
          c,
          args[0]->data.destructure_vec.names,
          args[0]->data.destructure_vec.name_lens,
          args[0]->data.destructure_vec.types,
          args[0]->data.destructure_vec.type_lens,
          args[0]->data.destructure_vec.count,
          args[0]->data.destructure_vec.rest_name,
          args[0]->data.destructure_vec.rest_name_len,
          args[1], false, line, col);
      return;
    }
    /* --- Buf def: `def [Buf N T] NAME` (zero-init) or
     *              `def [Buf N T] NAME [[Buf N T] v0 v1 ...]` (literal init).
     * Both share the multi-slot allocation + OP_BUF_ZERO_LOCAL prelude;
     * literal init adds per-element OP_BUF_SET_LOCAL stores. Partial
     * fill is allowed (rest stays zero). See BUFFER_DESIGN.md M2. */
    if ((argc == 2 || argc == 3) && args[1]->type == AST_LIT_STRING &&
        args[0]->type == AST_COMMAND &&
        args[0]->data.command.head &&
        args[0]->data.command.head->type == AST_LIT_STRING &&
        args[0]->data.command.head->data.lit_string.length == 3 &&
        memcmp(args[0]->data.command.head->data.lit_string.value, "Buf", 3) == 0 &&
        args[0]->data.command.arg_count == 2) {
      AstNode* tn   = args[0];
      AstNode* nlen = tn->data.command.args[0];
      AstNode* telt = tn->data.command.args[1];
      if (nlen->type != AST_LIT_INT || nlen->data.lit_int.value <= 0) {
        compiler__error(c, line, col,
                        "[Buf N T] length must be a positive integer literal");
        return;
      }
      JaclType elem_type;
      if (telt->type != AST_LIT_STRING ||
          !compiler__resolve_type(c, telt->data.lit_string.value,
                                  telt->data.lit_string.length, &elem_type)) {
        compiler__error(c, line, col,
                        "[Buf N T] element type must be a scalar keyword");
        return;
      }
      uint32_t n = (uint32_t)nlen->data.lit_int.value;
      uint32_t elem_sz = struct__type_size(elem_type, NULL, 0);
      uint64_t byte_count = (uint64_t)n * (uint64_t)elem_sz;
      if (byte_count > 0xFFFFu) {
        compiler__error(c, line, col,
                        "[Buf N T] byte size exceeds 65535 (M2 limit)");
        return;
      }
      uint32_t slot_count = (uint32_t)((byte_count + sizeof(JaclVal) - 1) / sizeof(JaclVal));
      if (slot_count == 0) slot_count = 1;

      /* For argc==3 literal init, validate the RHS constructor matches
       * the LHS type. The RHS must be [[Buf N T] v0 v1 ...] where the
       * inner [Buf N T] matches exactly. */
      AstNode* init_ctor = NULL;
      uint32_t init_count = 0;
      AstNode** init_vals = NULL;
      if (argc == 3) {
        AstNode* rhs = args[2];
        bool rhs_ok = rhs->type == AST_COMMAND &&
                      rhs->data.command.head &&
                      rhs->data.command.head->type == AST_COMMAND &&
                      rhs->data.command.head->data.command.head &&
                      rhs->data.command.head->data.command.head->type == AST_LIT_STRING &&
                      rhs->data.command.head->data.command.head->data.lit_string.length == 3 &&
                      memcmp(rhs->data.command.head->data.command.head->data.lit_string.value,
                             "Buf", 3) == 0 &&
                      rhs->data.command.head->data.command.arg_count == 2;
        if (!rhs_ok) {
          compiler__error(c, line, col,
              "def [Buf N T]: RHS must be omitted (zero-init) or "
              "[[Buf N T] v0 v1 ...] literal");
          return;
        }
        AstNode* rhs_tn   = rhs->data.command.head;
        AstNode* rhs_nlen = rhs_tn->data.command.args[0];
        AstNode* rhs_telt = rhs_tn->data.command.args[1];
        if (rhs_nlen->type != AST_LIT_INT ||
            (uint32_t)rhs_nlen->data.lit_int.value != n) {
          compiler__error(c, line, col,
              "[[Buf N T] ...] constructor length must match LHS exactly");
          return;
        }
        JaclType rhs_elem;
        if (rhs_telt->type != AST_LIT_STRING ||
            !compiler__resolve_type(c, rhs_telt->data.lit_string.value,
                                    rhs_telt->data.lit_string.length, &rhs_elem) ||
            rhs_elem != elem_type) {
          compiler__error(c, line, col,
              "[[Buf N T] ...] constructor element type must match LHS exactly");
          return;
        }
        init_ctor  = rhs;
        init_count = rhs->data.command.arg_count;
        init_vals  = rhs->data.command.args;
        if (init_count > n) {
          char err[128];
          snprintf(err, sizeof(err),
              "[[Buf %u %s] ...]: %u values provided, max %u",
              (unsigned)n, type_name(elem_type),
              (unsigned)init_count, (unsigned)n);
          compiler__error(c, line, col, err);
          return;
        }
      }

      uint32_t base_slot = c->local_count;

      JaclVal bind_val = compiler__name_val(c->heap, c->intern_table,
          args[1]->data.lit_string.value, args[1]->data.lit_string.length);
      /* Reserve runtime stack slots for the buf bytes. Each local needs
       * a corresponding stack push so subsequent OP_BUF_ZERO_LOCAL can
       * memset in place. */
      compiler__emit_byte(c, OP_NIL, line);
      compiler__add_local(c, bind_val, line, col);
      c->locals[c->local_count - 1].type = TYPE_BUF;
      c->locals[c->local_count - 1].struct_type_idx = JACL_SCALAR_TYPE_IDX(elem_type);
      c->locals[c->local_count - 1].width = (uint16_t)slot_count;
      c->locals[c->local_count - 1].is_inline = true;
      c->locals[c->local_count - 1].buf_len = n;
      for (uint32_t w = 1; w < slot_count; w++) {
        compiler__emit_byte(c, OP_NIL, line);
        compiler__add_local(c, jacl_inline_string("", 0), line, col);
        c->locals[c->local_count - 1].depth = c->scope_depth;
      }

      compiler__emit_byte(c, OP_BUF_ZERO_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)base_slot, line);
      compiler__emit_u16(c, (uint16_t)byte_count, line);

      /* Literal init: emit per-element OP_BUF_SET_LOCAL. Index is a
       * compile-time constant so we push it via OP_CONST, then the
       * value, then the store opcode. Element-type overflow checks on
       * literal values are deferred to M5 (see BUFFER_DESIGN.md). */
      if (init_ctor) {
        (void)init_ctor;
        for (uint32_t i = 0; i < init_count; i++) {
          compiler__emit_constant(c, jacl_i32((int32_t)i), line);
          compiler__compile_node(c, init_vals[i]);
          /* All M2 element types accept i32 on the value-pop path
           * (small ints narrow, i64/u64/f64 promote). The typer is
           * responsible for rejecting incompatible kinds (e.g. a
           * string literal in a u8 buf). */
          compiler__emit_byte(c, OP_BUF_SET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)base_slot, line);
          compiler__emit_byte(c, (uint8_t)elem_type, line);
          compiler__emit_u16(c, (uint16_t)n, line);
        }
      }

      /* Statement value: top-level statements push exactly one result
       * that OP_CHECK_ERROR consumes between statements. Our N storage
       * slots must NOT be consumed, so emit one extra NIL as the
       * disposable statement value. See vm.c:OP_CHECK_ERROR. */
      compiler__emit_byte(c, OP_NIL, line);
      c->last_expr_type = TYPE_NIL;
      return;
    }

    if (argc == 2 && args[0]->type == AST_COMMAND) {
      /* keyword form: def [a b c] expr — convert AST_COMMAND to name arrays
       * Also handles rest patterns: [def [head ..rest] expr] where ..rest
       * is parsed as AST_SPREAD */
      AstNode* pat = args[0];
      uint32_t total_elems = 1 + pat->data.command.arg_count; /* head + args */
      if (total_elems > 255) {
        compiler__error(c, line, col, "too many bindings in destructuring");
        return;
      }
      const char* d_names[256];
      uint32_t d_name_lens[256];
      const char* rest_name = NULL;
      uint32_t rest_name_len = 0;
      int rest_seen = 0;
      uint32_t d_count = 0;
      /* head is first name */
      if (pat->data.command.head->type == AST_SPREAD) {
        /* ..rest as first element (AST_SPREAD from bracket arg parsing) */
        AstNode* inner = pat->data.command.head->data.spread.expr;
        if (inner && inner->type == AST_LIT_STRING) {
          rest_name = inner->data.lit_string.value;
          rest_name_len = inner->data.lit_string.length;
        }
        rest_seen = 1;
      } else if (pat->data.command.head->type == AST_LIT_STRING &&
                 pat->data.command.head->data.lit_string.length == 2 &&
                 pat->data.command.head->data.lit_string.value[0] == '.' &&
                 pat->data.command.head->data.lit_string.value[1] == '.') {
        /* ".." as head — parsed when [..rest] or [..] is the inner bracket.
           The head becomes AST_LIT_STRING("..") and rest name (if any) is
           the first arg. */
        rest_seen = 1;
        if (pat->data.command.arg_count >= 1 &&
            pat->data.command.args[0]->type == AST_LIT_STRING) {
          rest_name = pat->data.command.args[0]->data.lit_string.value;
          rest_name_len = pat->data.command.args[0]->data.lit_string.length;
          /* Remaining args after the rest name are positional names after rest
             — this is an error (rest not last), but we collect them so the
             compile_destructure_vec function can detect the issue. */
          for (uint32_t i = 1; i < pat->data.command.arg_count; i++) {
            if (pat->data.command.args[i]->type == AST_LIT_STRING) {
              d_names[d_count] = pat->data.command.args[i]->data.lit_string.value;
              d_name_lens[d_count] = pat->data.command.args[i]->data.lit_string.length;
              d_count++;
            }
          }
        }
        /* rest_seen means rest is NOT at the end — report error */
        if (d_count > 0) {
          compiler__error(c, line, col,
                          "rest pattern '..' must be the last element in destructuring");
          return;
        }
        compiler__compile_destructure_vec(
            c, d_names, d_name_lens, NULL, NULL, d_count,
            rest_name, rest_name_len,
            args[1], false, line, col);
        return;
      } else if (pat->data.command.head->type != AST_LIT_STRING) {
        compiler__error(c, line, col,
                        "destructuring pattern elements must be names");
        return;
      } else {
        d_names[0] = pat->data.command.head->data.lit_string.value;
        d_name_lens[0] = pat->data.command.head->data.lit_string.length;
        d_count = 1;
      }
      for (uint32_t i = 0; i < pat->data.command.arg_count; i++) {
        AstNode* elem = pat->data.command.args[i];
        if (elem->type == AST_SPREAD) {
          /* ..rest pattern */
          if (rest_seen) {
            compiler__error(c, line, col,
                            "duplicate rest pattern '..' in destructuring");
            return;
          }
          AstNode* inner = elem->data.spread.expr;
          if (inner && inner->type == AST_LIT_STRING) {
            rest_name = inner->data.lit_string.value;
            rest_name_len = inner->data.lit_string.length;
          }
          rest_seen = 1;
        } else if (elem->type == AST_LIT_STRING) {
          if (rest_seen) {
            compiler__error(c, line, col,
                            "rest pattern '..' must be the last element in destructuring");
            return;
          }
          d_names[d_count] = elem->data.lit_string.value;
          d_name_lens[d_count] = elem->data.lit_string.length;
          d_count++;
        } else {
          compiler__error(c, line, col,
                          "destructuring pattern elements must be names");
          return;
        }
      }
      if (rest_seen && pat->data.command.head->type == AST_SPREAD &&
          pat->data.command.arg_count > 0) {
        /* rest was first (head), but there are more elements after it */
        compiler__error(c, line, col,
                        "rest pattern '..' must be the last element in destructuring");
        return;
      }
      compiler__compile_destructure_vec(
          c, d_names, d_name_lens, NULL, NULL, d_count,
          rest_name, rest_name_len,
          args[1], false, line, col);
      return;
    }
    /* --- Named destructuring: [def {x, y} value] --- */
    if (argc == 2 && args[0]->type == AST_DESTRUCTURE_NAMED) {
      compiler__compile_destructure_named(
          c,
          args[0]->data.destructure_named.names,
          args[0]->data.destructure_named.name_lens,
          args[0]->data.destructure_named.types,
          args[0]->data.destructure_named.type_lens,
          args[0]->data.destructure_named.count,
          args[0]->data.destructure_named.rest_name,
          args[0]->data.destructure_named.rest_name_len,
          args[0]->data.destructure_named.spread_all,
          args[1], false, line, col);
      return;
    }
    /* --- Named destructuring from block: [def {x, y} value] (keyword form)
     * Also handles rest patterns: [def {x, ..rest} value]
     * spread-all: [def {..} value]
     * and typed fields: [def {i32 x, i32 y} value] --- */
    if (argc == 2 && args[0]->type == AST_BLOCK) {
      AstNode* blk = args[0];
      uint32_t blk_count = blk->data.block.count;
      if (blk_count == 0 || blk_count > 255) {
        compiler__error(c, line, col, "invalid destructuring pattern");
        return;
      }
      const char* d_names_arr[256];
      uint32_t d_name_lens_arr[256];
      const char* d_types_arr[256];
      uint32_t d_type_lens_arr[256];
      const char* rest_nm = NULL;
      uint32_t rest_nm_len = 0;
      int spread_all_flag = 0;
      int has_types = 0;
      uint32_t d_count = 0;
      int valid = 1;
      for (uint32_t i = 0; i < blk_count; i++) {
        AstNode* cmd = blk->data.block.commands[i];
        /* Handle bare ".." token (AST_LIT_STRING, not wrapped in AST_COMMAND)
           — this is how {..} parses since ".." is not a TOKEN_WORD */
        if (cmd->type == AST_LIT_STRING &&
            cmd->data.lit_string.length == 2 &&
            cmd->data.lit_string.value[0] == '.' &&
            cmd->data.lit_string.value[1] == '.') {
          spread_all_flag = 1;
          continue;
        }
        if (cmd->type != AST_COMMAND) { valid = 0; break; }
        const char* head_str = NULL;
        uint32_t head_len = 0;
        if (cmd->data.command.head->type == AST_LIT_STRING) {
          head_str = cmd->data.command.head->data.lit_string.value;
          head_len = cmd->data.command.head->data.lit_string.length;
        } else {
          valid = 0; break;
        }
        /* Check for rest pattern: ..name (bare command form in block) */
        if (head_len == 2 && head_str[0] == '.' && head_str[1] == '.') {
          if (cmd->data.command.arg_count == 0) {
            /* spread-all: {..} — head is ".." in a command with no args */
            spread_all_flag = 1;
          } else if (cmd->data.command.arg_count == 1 &&
                     cmd->data.command.args[0]->type == AST_LIT_STRING) {
            /* rest pattern: ..name */
            rest_nm = cmd->data.command.args[0]->data.lit_string.value;
            rest_nm_len = cmd->data.command.args[0]->data.lit_string.length;
          } else {
            valid = 0; break;
          }
          continue;
        }
        /* Check for typed field: type name (head=type, args=[name]) */
        if (cmd->data.command.arg_count == 1 &&
            cmd->data.command.args[0]->type == AST_LIT_STRING) {
          d_types_arr[d_count] = head_str;
          d_type_lens_arr[d_count] = head_len;
          d_names_arr[d_count] = cmd->data.command.args[0]->data.lit_string.value;
          d_name_lens_arr[d_count] = cmd->data.command.args[0]->data.lit_string.length;
          has_types = 1;
          d_count++;
        } else if (cmd->data.command.arg_count == 0) {
          /* simple name */
          d_names_arr[d_count] = head_str;
          d_name_lens_arr[d_count] = head_len;
          d_types_arr[d_count] = NULL;
          d_type_lens_arr[d_count] = 0;
          d_count++;
        } else {
          valid = 0; break;
        }
      }
      if (valid) {
        compiler__compile_destructure_named(
            c, d_names_arr, d_name_lens_arr,
            has_types ? d_types_arr : NULL,
            has_types ? d_type_lens_arr : NULL,
            d_count,
            rest_nm, rest_nm_len, spread_all_flag,
            args[1], false, line, col);
        return;
      }
    }

    JaclType declared_type = TYPE_DYN;
    bool type_explicit = false;
    uint32_t name_arg_idx  = 0;
    uint32_t value_arg_idx = 1;

    if (argc == 3) {
      /* Typed def: [def TYPE name value] */
      if (args[0]->type == AST_COMMAND) {
        /* Compound type annotation: [Vec T], [Map T], [Map K V], [Future T].
         * The typer is the source of truth for these — it narrows
         * the binding's static type from inferred_type and tracks
         * element/key idxs separately. The compiler routes through
         * the untyped-def path so the binding inherits the RHS's
         * effective type (e.g., TYPE_FUTURE from spawn, TYPE_TYPED_VEC
         * from a typed-vec ctor). Recognized so the user can document
         * the intent in source even though codegen doesn't change. */
        AstNode* tn = args[0];
        if (tn->data.command.head &&
            tn->data.command.head->type == AST_LIT_STRING) {
          const char* hn = tn->data.command.head->data.lit_string.value;
          uint32_t    hl = tn->data.command.head->data.lit_string.length;
          bool is_ptr_type = (hl == 3 && memcmp(hn, "Ptr", 3) == 0);
          bool is_compound_type =
              (hl == 3 && (memcmp(hn, "Vec", 3) == 0 ||
                           memcmp(hn, "Map", 3) == 0)) ||
              is_ptr_type ||
              (hl == 6 && (memcmp(hn, "Future", 6) == 0 ||
                           memcmp(hn, "Stream", 6) == 0));
          if (is_compound_type) {
            /* [Ptr T] storage is a u64 (the runtime rep of any pointer);
             * the typer separately tracks the pointee identity on the
             * AST node. For other compound types ([Vec T], [Map T],
             * [Future T]) the binding inherits its type from the RHS,
             * since there's no single primitive storage rep. */
            if (is_ptr_type) {
              declared_type = TYPE_U64;
              type_explicit = true;
            } else {
              declared_type = TYPE_DYN;
              type_explicit = false;
            }
            name_arg_idx   = 1;
            value_arg_idx  = 2;
            goto def_args_resolved;
          }
        }
        compiler__error(c, line, col, "def type must be a keyword");
        return;
      }
      if (args[0]->type != AST_LIT_STRING) {
        compiler__error(c, line, col, "def type must be a keyword");
        return;
      }
      uint32_t first_len = args[0]->data.lit_string.length;
      const char* first_str = args[0]->data.lit_string.value;
      if (!compiler__resolve_type(c, first_str, first_len, &declared_type)) {
        compiler__error(c, line, col, "def with 3 arguments requires type keyword as first argument");
        return;
      }
      type_explicit = true;
      name_arg_idx   = 1;
      value_arg_idx  = 2;
    def_args_resolved: ;
    } else if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "def", "2 or 3 arguments", argc);
      return;
    }

    /* US-013: accept AST_LIT_STRING (normal def) or AST_VAR_REF with
       is_caret set (^name inside syntax-quote).
       US-014: also accept AST_VAR_REF with is_gensym set. */
    const char* bind_name_ptr;
    uint32_t    name_len;
    uint32_t    bind_scope_mark;
    if (args[name_arg_idx]->type == AST_LIT_STRING) {
      bind_name_ptr   = args[name_arg_idx]->data.lit_string.value;
      name_len        = args[name_arg_idx]->data.lit_string.length;
      bind_scope_mark = args[name_arg_idx]->scope_mark;
    } else if (args[name_arg_idx]->type == AST_VAR_REF &&
               args[name_arg_idx]->is_caret) {
      bind_name_ptr   = args[name_arg_idx]->data.var_ref.name;
      name_len        = args[name_arg_idx]->data.var_ref.length;
      bind_scope_mark = 0;  /* ^name → caller's scope */
    } else if (args[name_arg_idx]->type == AST_VAR_REF &&
               args[name_arg_idx]->is_gensym) {
      bind_name_ptr   = args[name_arg_idx]->data.var_ref.name;
      name_len        = args[name_arg_idx]->data.var_ref.length;
      bind_scope_mark = args[name_arg_idx]->scope_mark;
    } else {
      compiler__error(c, line, col, "def name must be a string");
      return;
    }
    if (name_len > 128) {
      compiler__error(c, line, col, "variable name exceeds 128-byte limit");
      return;
    }
    if (name_len == 3 && memcmp(bind_name_ptr, "ctx", 3) == 0) {
      compiler__error(c, line, col, "'ctx' is reserved");
      return;
    }

    /* US-005: check if RHS is a struct constructor or typed vec/map get
     * — activate inline storage. Only for local scope, non-SM mode. */
    bool activate_inline = false;
    c->inline_repr = INLINE_NONE;
    if (c->scope_depth > 0 && !c->sm_analysis) {
      AstNode* val_node = args[value_arg_idx];
      if (val_node->type == AST_COMMAND && val_node->data.command.head->type == AST_LIT_STRING) {
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        if (reg) {
          const char* rhs_head_name = val_node->data.command.head->data.lit_string.value;
          uint32_t rhs_head_len = val_node->data.command.head->data.lit_string.length;
          uint32_t rhs_sidx = struct_registry__find(reg, rhs_head_name, rhs_head_len);
          if (rhs_sidx != UINT32_MAX) {
            /* US-015: only inline value-type structs; legacy structs use heap */
            StructTypeDef* rhs_sdef = reg->defs[rhs_sidx];
            if (struct_def_is_user(rhs_sdef, reg)) {
              activate_inline = true;
              /* Phase 5c: want_inline_struct no longer needed here —
               * value-type struct constructors are always inline. */
            }
          }
          /* Hint: vec-get/map-get on typed collection may produce inline result.
           * The actual decision happens at compile time when we know the arg type. */
          /* Phase 5f: vec-get/map-get always produce inline results now */
        }
      }
    }

    /* Compile the value expression with type context */
    compiler__compile_node(c, args[value_arg_idx]);
    JaclType rhs_type = (JaclType)args[value_arg_idx]->inferred_type;
    uint32_t rhs_struct_idx = args[value_arg_idx]->inferred_struct_idx;

    /* US-007: activate inline for function calls returning struct types.
     * If the RHS isn't already inline (struct constructor or inline get) but
     * returns a struct type with a known struct index, post-activate inline
     * and plan to emit OP_STRUCT_STORE_INLINE after adding the local. */
    bool needs_store_inline = false;
    if (!activate_inline && rhs_type == TYPE_STRUCT &&
        rhs_struct_idx != UINT32_MAX &&
        c->scope_depth > 0 && !c->sm_analysis) {
      /* US-015: only inline value-type structs; legacy structs use heap */
      StructTypeRegistry* reg2 = compiler__get_struct_registry(c);
      if (reg2 && rhs_struct_idx < reg2->count) {
        StructTypeDef* ret_sdef = reg2->defs[rhs_struct_idx];
        if (struct_def_is_user(ret_sdef, reg2)) {
          activate_inline = true;
          /* If RHS already pushed inline slots (INLINE_STACK or INLINE_REF
             from a chained nested-struct access), no de-materialization
             needed — slots ARE the local. */
          needs_store_inline = (c->inline_repr == INLINE_NONE);
        }
      }
    }

    /* Type check for typed def. TYPE_PTR is treated as compatible with
     * TYPE_U64 here because pointer storage is u64 at runtime — the
     * pointer identity is tracked by the typer on the AST node. The
     * typer's pointee-mismatch check fires before reaching here. */
    bool ptr_u64_compat =
        (declared_type == TYPE_U64 && rhs_type == TYPE_PTR) ||
        (declared_type == TYPE_PTR && rhs_type == TYPE_U64);
    if (declared_type != TYPE_DYN && rhs_type != TYPE_DYN &&
        rhs_type != declared_type && !ptr_u64_compat) {
      char err_msg[128];
      snprintf(err_msg, sizeof(err_msg), "type error: expected %s, got %s",
               type_name(declared_type), type_name(rhs_type));
      compiler__error(c, line, col, err_msg);
      return;
    }
    if (declared_type != TYPE_DYN && rhs_type == TYPE_DYN) {
      char err_msg[160];
      jacl_format_assign_dyn_unnamed(err_msg, sizeof(err_msg), declared_type);
      compiler__error(c, line, col, err_msg);
      return;
    }

    /* Reject explicit `def dyn name [Point ...]` — structs cannot live in
       dyn slots. Use [box $val] or drop the `dyn` annotation. */
    if (type_explicit && declared_type == TYPE_DYN && rhs_type == TYPE_STRUCT) {
      compiler__error(c, line, col,
                      "cannot store struct value in dyn slot — wrap with "
                      "[box $val] or drop the 'dyn' annotation");
      return;
    }

    JaclVal name_val = compiler__name_val(c->heap, c->intern_table, bind_name_ptr, name_len);

    /* Determine effective type: declared type wins, else infer unboxed/struct from RHS */
    JaclType effective_type;
    if (declared_type != TYPE_DYN) {
      effective_type = declared_type;
    } else if (is_unboxed_type(rhs_type) || rhs_type == TYPE_STRUCT ||
               rhs_type == TYPE_STREAM || is_typed_collection(rhs_type)) {
      /* Infer unboxed types, struct types, stream types, and typed collection types from RHS */
      effective_type = rhs_type;
    } else {
      effective_type = TYPE_DYN;
    }

    int16_t rhs_arity = compiler__node_known_arity(c, args[value_arg_idx]);

    if (c->sm_analysis) {
      /* SM mode: write value to state object field instead of local slot. */
      const StateField* sf = sm__get_field(&c->sm_analysis->state_layout, name_val);
      if (sf) {
        if (sf->struct_type_idx != 0) {
          /* Struct state field: write N inline slots. */
          compiler__emit_byte(c, OP_SET_STATE_FIELD_WIDE, line);
          compiler__emit_byte(c, (uint8_t)sf->field_index, line);
          compiler__emit_byte(c, (uint8_t)sf->width, line);
        } else {
          compiler__emit_byte(c, OP_SET_STATE_FIELD, line);
          compiler__emit_byte(c, (uint8_t)sf->field_index, line);
        }
        /* def returns nil */
        compiler__emit_byte(c, OP_NIL, line);
      } else {
        /* Name not in state layout — shouldn't happen, but fall through */
        {
          uint32_t prev_mark = c->current_scope_mark;
          c->current_scope_mark = bind_scope_mark;
          compiler__add_local(c, name_val, line, col);
          c->current_scope_mark = prev_mark;
        }
        compiler__emit_byte(c, OP_NIL, line);
      }
    } else if (c->scope_depth > 0 ||
               (c->lower_top_level && effective_type != TYPE_STRUCT &&
                c->enclosing == NULL)) {
      /* Local variable: value is on stack as the local slot.
         US-013: use the name arg's scope mark (0 for ^caret, else the
         def command's stamped mark) so caret bindings land in the
         caller's scope while normal macro bindings stay hygienic.

         lower_top_level branch: top-level mut/def in chunks with no
         closures (proc/spawn/parallel/race/await/interpret/use) skip
         the env and live as stack locals on the chunk's frame. Reads
         and writes flow through resolve_local → OP_GET_LOCAL /
         OP_SET_LOCAL, bypassing the env lookup entirely. The
         lower_top_level pre-scan in compiler_compile (see Compiler
         struct docs) sets this flag only when no callee can observe
         the name through the env. */
      {
        uint32_t prev_mark = c->current_scope_mark;
        c->current_scope_mark = bind_scope_mark;
        compiler__add_local(c, name_val, line, col);
        c->current_scope_mark = prev_mark;
      }
      c->locals[c->local_count - 1].known_arity = rhs_arity;
      { TypeInfo ti = { effective_type, rhs_struct_idx, args[value_arg_idx]->inferred_key_struct_idx };
        TYPEINFO_SAVE(c->locals[c->local_count - 1], ti); }
      if (effective_type == TYPE_STRUCT) {
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        uint32_t width = struct__slot_width(reg, rhs_struct_idx);
        c->locals[c->local_count - 1].width = (uint16_t)width;
        if (activate_inline) {
          uint32_t base_local_idx = c->local_count - 1;
          c->locals[base_local_idx].is_inline = true;
          /* Reserve extra stack slots for wide inline structs (width > 1).
           * Add padding locals so subsequent locals get correct slot indices. */
          for (uint32_t w = 1; w < width; w++) {
            compiler__add_local(c, jacl_inline_string("", 0), line, col);
            c->locals[c->local_count - 1].depth = c->scope_depth;
          }
          /* US-007: de-materialize heap struct return value into inline slots */
          if (needs_store_inline) {
            compiler__emit_byte(c, OP_STRUCT_STORE_INLINE, line);
            compiler__emit_byte(c, (uint8_t)base_local_idx, line);
            compiler__emit_u16(c, (uint16_t)rhs_struct_idx, line);
          }
        }
      }
      /* def returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* Global variable. Reject struct values at top level — globals are
         JaclVal slots and storing a struct would auto-allocate a heap
         HeapRecord. The user must either wrap the body in a proc (struct
         lives inline as a wide local) or use [box $val] explicitly. Ctx
         is exempt: it's the lone HeapRecord builtin. */
      if (effective_type == TYPE_STRUCT &&
          rhs_struct_idx != UINT32_MAX &&
          rhs_struct_idx != compiler__get_struct_registry(c)->ctx_type_idx) {
        compiler__error(c, line, col,
                        "cannot define a struct value at top level — wrap "
                        "the body in a proc, or use [box $val] to box it "
                        "explicitly");
        return;
      }
      /* Box unboxed types before storage */
      if (is_unboxed_type(effective_type)) {
        compiler__emit_byte(c, OP_TO_DYN, line);
        compiler__emit_byte(c, (uint8_t)effective_type, line);
      }
      JaclVal def_key = compiler__global_name_val(c, bind_name_ptr, name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, def_key);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      /* Register global with arity and type (use plain name for metadata) */
      compiler__set_global_arity(c, name_val, rhs_arity);
      {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t i = 0; i < root->global_arity_count; i++) {
          if (root->global_arities[i].name == name_val) {
            { TypeInfo ti = { effective_type, rhs_struct_idx, args[value_arg_idx]->inferred_key_struct_idx };
              TYPEINFO_SAVE(root->global_arities[i], ti); }
            break;
          }
        }
      }
    }
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* extern declaration — host-provided native fn signature.
   *   extern TYPE name params         (argc==3)
   *   extern      name params         (argc==2)
   * The typer's pre-pass already registered the signature; the
   * compiler emits no body code. Runtime dispatch flows through the
   * existing native-fn registry by name (prelude_is_native_fn path
   * at compiler.c:5371). The extern statement itself produces nil. */
  if (hid == HEAD_EXTERN) {
    if (argc < 2 || argc > 3) {
      compiler__builtin_arity_error(c, line, col, "extern", "2 or 3 arguments", argc);
      return;
    }
    uint32_t name_idx = (argc == 3) ? 1 : 0;
    if (args[name_idx]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "extern: name must be a string");
      return;
    }
    compiler__emit_byte(c, OP_NIL, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* proc definition */
  if (hid == HEAD_PROC) {
    /* Disambiguate: 4 args + first is type keyword → has return type.
       3 args → no return type (existing). */
    JaclType proc_return_type = TYPE_DYN;
    uint32_t proc_return_struct_idx = UINT32_MAX;
    uint32_t name_arg_idx, params_arg_idx, body_arg_idx;

    if (argc == 4) {
      /* [proc TYPE name params body]. TYPE may be a keyword, struct
       * name, or a compound AST_COMMAND ([Ptr T], [Future T], [Vec T],
       * [Map K V]). For compound forms we route storage analogously to
       * the def path: [Ptr T] → TYPE_U64 storage (typer carries pointee
       * identity); other compound types → TYPE_DYN storage (typer is
       * the source of truth for the static return type). */
      AstNode* tn = args[0];
      bool resolved = false;
      if (tn->type == AST_LIT_STRING &&
          compiler__resolve_type(c, tn->data.lit_string.value,
                                  tn->data.lit_string.length,
                                  &proc_return_type)) {
        if (proc_return_type == TYPE_STRUCT) {
          StructTypeRegistry* reg = compiler__get_struct_registry(c);
          if (reg) {
            proc_return_struct_idx = struct_registry__find(reg,
                tn->data.lit_string.value,
                tn->data.lit_string.length);
          }
        }
        resolved = true;
      } else if (tn->type == AST_COMMAND && tn->data.command.head &&
                 tn->data.command.head->type == AST_LIT_STRING) {
        const char* hn = tn->data.command.head->data.lit_string.value;
        uint32_t    hl = tn->data.command.head->data.lit_string.length;
        bool is_ptr = (hl == 3 && memcmp(hn, "Ptr", 3) == 0);
        bool is_stream = (hl == 6 && memcmp(hn, "Stream", 6) == 0);
        bool is_compound =
            (hl == 3 && (memcmp(hn, "Vec", 3) == 0 ||
                         memcmp(hn, "Map", 3) == 0)) ||
            is_ptr ||
            is_stream ||
            (hl == 6 && memcmp(hn, "Future", 6) == 0);
        if (is_compound) {
          /* [Stream T] folds into the generator path: proc_return_type
           * stays DYN here, and the has_yield check below at the
           * closure-construction site bumps it to TYPE_STREAM. The
           * element idx is carried by the typer (proc->return_struct_idx)
           * onto call-site nodes via inferred_struct_idx, which is
           * where for-loop narrowing reads it. */
          proc_return_type = is_ptr ? TYPE_U64 : TYPE_DYN;
          resolved = true;
        }
      }
      if (!resolved) {
        compiler__error(c, line, col,
            "proc with 4 arguments requires a type annotation as first argument");
        return;
      }
      name_arg_idx   = 1;
      params_arg_idx = 2;
      body_arg_idx   = 3;
    } else if (argc == 3) {
      name_arg_idx   = 0;
      params_arg_idx = 1;
      body_arg_idx   = 2;
    } else {
      compiler__builtin_arity_error(c, line, col, "proc", "3 or 4 arguments", argc);
      return;
    }

    if (args[name_arg_idx]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "proc name must be a string");
      return;
    }
    if (args[params_arg_idx]->type != AST_COMMAND) {
      compiler__error(c, line, col, "proc params must be a bracketed list");
      return;
    }
    if (args[body_arg_idx]->type != AST_BLOCK) {
      compiler__error(c, line, col, "proc body must be a block");
      return;
    }

    /* Get proc name */
    const char* proc_name = args[name_arg_idx]->data.lit_string.value;
    uint32_t proc_name_len = args[name_arg_idx]->data.lit_string.length;
    if (proc_name_len > 128) {
      compiler__error(c, line, col, "proc name exceeds 128-byte limit");
      return;
    }

    /* Parse parameters with optional types from command node.
       Walk flat list: head + children. Type keywords interleaved:
       [i64 a i64 b] → a:i64, b:i64. [a b] → a:dyn, b:dyn. */
    AstNode* params_node = args[params_arg_idx];
    AstNode* params_head = params_node->data.command.head;

    /* Build flat element list from head + args */
    uint32_t flat_count = 0;
    AstNode* flat_elems[COMPILER_MAX_PROC_PARAMS * 2 + 2]; /* generous */

    if (params_head->data.lit_string.length > 0) {
      flat_elems[flat_count++] = params_head;
      for (uint32_t i = 0; i < params_node->data.command.arg_count; i++) {
        if (flat_count < sizeof(flat_elems)/sizeof(flat_elems[0]))
          flat_elems[flat_count++] = params_node->data.command.args[i];
      }
    }

    /* Walk flat list to extract (type, name) pairs */
    JaclVal param_names_arr[COMPILER_MAX_PROC_PARAMS];
    JaclType param_types_arr[COMPILER_MAX_PROC_PARAMS];
    uint32_t param_struct_idxs[COMPILER_MAX_PROC_PARAMS]; /* struct registry index per param */
    uint32_t param_key_struct_idxs[COMPILER_MAX_PROC_PARAMS]; /* key struct idx for typed maps */
    uint32_t param_scope_marks[COMPILER_MAX_PROC_PARAMS]; /* hygiene: per-param scope mark */
    uint8_t param_count = 0;
    bool is_variadic = false;
    memset(param_struct_idxs, 0xFF, sizeof(param_struct_idxs)); /* UINT32_MAX = no struct */
    memset(param_key_struct_idxs, 0xFF, sizeof(param_key_struct_idxs));

    for (uint32_t fi = 0; fi < flat_count; fi++) {
      AstNode* elem = flat_elems[fi];

      /* Check for [Ptr T] parameter — typed pointer. The typer is the
       * source of truth for pointee tracking; the compiler just needs
       * to consume the parameter's name and accept the annotation. */
      {
        AstNode* ptr_pointee = NULL;
        if (compiler__ptr_type_expr(elem, &ptr_pointee)) {
          fi++;
          if (fi >= flat_count) {
            compiler__error(c, line, col, "expected parameter name after [Ptr T] annotation");
            return;
          }
          elem = flat_elems[fi];
          if (elem->type != AST_LIT_STRING || elem->data.lit_string.length > 128) {
            compiler__error(c, line, col, "proc parameter name invalid");
            return;
          }
          if (param_count >= COMPILER_MAX_PROC_PARAMS) {
            compiler__error(c, line, col, "too many proc parameters");
            return;
          }
          param_names_arr[param_count] = compiler__name_val(c->heap, c->intern_table,
              elem->data.lit_string.value, elem->data.lit_string.length);
          param_types_arr[param_count] = TYPE_PTR;
          /* pointee idx isn't tracked by the compiler today — the typer
           * carries it on the AST node. Leave struct/key idxs sentinel. */
          param_scope_marks[param_count] = elem->scope_mark;
          param_count++;
          continue;
        }
      }

      /* Check for compound type expression: [Vec Type], [Map Type], [Map K V] */
      {
        AstNode* ct_elem = NULL;
        AstNode* ct_key_elem = NULL;
        int ct_kind = compiler__typed_collection_expr(elem, &ct_elem, &ct_key_elem);
        if (ct_kind > 0) {
          const char* ename = ct_elem->data.lit_string.value;
          uint32_t elen = ct_elem->data.lit_string.length;
          uint32_t elem_idx = struct_registry__find(compiler__get_struct_registry(c), ename, elen);
          if (elem_idx == UINT32_MAX) {
            compiler__error(c, line, col, "unknown element type in typed collection parameter");
            return;
          }
          uint32_t key_idx = UINT32_MAX;
          if (ct_kind == 3) {
            key_idx = struct_registry__find(compiler__get_struct_registry(c),
                ct_key_elem->data.lit_string.value, ct_key_elem->data.lit_string.length);
            if (key_idx == UINT32_MAX) {
              compiler__error(c, line, col, "unknown key type in typed map parameter");
              return;
            }
          }
          fi++;
          if (fi >= flat_count) {
            compiler__error(c, line, col, "expected parameter name after type annotation");
            return;
          }
          elem = flat_elems[fi];
          if (elem->type != AST_LIT_STRING || elem->data.lit_string.length > 128) {
            compiler__error(c, line, col, "proc parameter name invalid");
            return;
          }
          if (param_count >= COMPILER_MAX_PROC_PARAMS) {
            compiler__error(c, line, col, "too many proc parameters");
            return;
          }
          param_names_arr[param_count] = compiler__name_val(c->heap, c->intern_table,
              elem->data.lit_string.value, elem->data.lit_string.length);
          param_types_arr[param_count] = (ct_kind == 1) ? TYPE_TYPED_VEC : TYPE_TYPED_MAP;
          param_struct_idxs[param_count] = elem_idx;
          param_key_struct_idxs[param_count] = key_idx;
          param_scope_marks[param_count] = elem->scope_mark;
          param_count++;
          continue;
        }
      }

      if (elem->type != AST_LIT_STRING) {
        compiler__error(c, line, col, "proc parameter must be a name or type keyword");
        return;
      }
      const char* word = elem->data.lit_string.value;
      uint32_t wlen = elem->data.lit_string.length;

      /* Check for variadic marker & or .. */
      if ((wlen == 1 && word[0] == '&') ||
          (wlen == 2 && word[0] == '.' && word[1] == '.')) {
        if (is_variadic) {
          compiler__error(c, line, col, "multiple rest parameters not allowed");
          return;
        }
        is_variadic = true;
        fi++;
        if (fi >= flat_count) {
          compiler__error(c, line, col, "expected parameter name after rest marker");
          return;
        }
        /* Check for optional type annotation: ..type name */
        elem = flat_elems[fi];
        JaclType rest_type = TYPE_DYN;
        if (elem->type == AST_LIT_STRING && fi + 1 < flat_count) {
          JaclType maybe_type;
          if (compiler__resolve_type(c, elem->data.lit_string.value,
                                     elem->data.lit_string.length, &maybe_type)) {
            rest_type = maybe_type;
            fi++;
            elem = flat_elems[fi];
          }
        }
        if (elem->type != AST_LIT_STRING || elem->data.lit_string.length > 128) {
          compiler__error(c, line, col, "proc parameter name invalid");
          return;
        }
        /* rest param must be last */
        if (fi != flat_count - 1) {
          compiler__error(c, line, col, "rest parameter must be last");
          return;
        }
        if (param_count >= COMPILER_MAX_PROC_PARAMS) {
          compiler__error(c, line, col, "too many proc parameters");
          return;
        }
        param_names_arr[param_count] = compiler__name_val(c->heap, c->intern_table,
            elem->data.lit_string.value, elem->data.lit_string.length);
        param_types_arr[param_count] = rest_type;
        param_scope_marks[param_count] = elem->scope_mark;
        param_count++;
        continue;
      }

      /* Check if current element is a type keyword (including struct names) */
      JaclType ptype;
      if (compiler__resolve_type(c, word, wlen, &ptype) && fi + 1 < flat_count) {
        /* Type annotation followed by param name → typed param */
        fi++;
        elem = flat_elems[fi];
        if (elem->type != AST_LIT_STRING || elem->data.lit_string.length > 128) {
          compiler__error(c, line, col, "proc parameter name invalid");
          return;
        }
        if (param_count >= COMPILER_MAX_PROC_PARAMS) {
          compiler__error(c, line, col, "too many proc parameters");
          return;
        }
        param_names_arr[param_count] = compiler__name_val(c->heap, c->intern_table,
            elem->data.lit_string.value, elem->data.lit_string.length);
        param_types_arr[param_count] = ptype;
        /* If struct type, look up the struct registry index */
        if (ptype == TYPE_STRUCT) {
          StructTypeRegistry* reg = compiler__get_struct_registry(c);
          param_struct_idxs[param_count] = struct_registry__find(reg, word, wlen);
        }
        param_scope_marks[param_count] = elem->scope_mark;
        param_count++;
      } else {
        /* Untyped param name */
        if (wlen > 128) {
          compiler__error(c, line, col, "proc parameter name invalid");
          return;
        }
        if (param_count >= COMPILER_MAX_PROC_PARAMS) {
          compiler__error(c, line, col, "too many proc parameters");
          return;
        }
        param_names_arr[param_count] = compiler__name_val(c->heap, c->intern_table, word, wlen);
        param_types_arr[param_count] = TYPE_DYN;
        param_scope_marks[param_count] = elem->scope_mark;
        param_count++;
      }
    }

    uint8_t min_args = is_variadic ? (uint8_t)(param_count - 1) : param_count;

    /* Check if this proc suspends (yield/await/parallel/race) */
    JaclVal name_val_check = compiler__name_val(c->heap, c->intern_table, proc_name, proc_name_len);
    bool proc_suspends_early = false;
    if (c->suspension_map) {
      proc_suspends_early = suspension_map_lookup(c->suspension_map, name_val_check);
    }

    /* Analyze body for state machine compilation when proc suspends */
    uint8_t user_param_count = param_count;
    bool use_sm_path = false;
    SuspensionAnalysis sm_analysis_data;
    memset(&sm_analysis_data, 0, sizeof(sm_analysis_data));

    if (proc_suspends_early) {
      sm_analysis_data = compiler__analyze_suspensions(
          args[body_arg_idx], param_names_arr, user_param_count, true, c->suspension_map,
          c->heap, c->intern_table, compiler__get_struct_registry(c));
      /* Always SM-compile suspending procs — even if suspension_count == 0
         (transitively suspending via calling other suspending procs). */
      use_sm_path = true;
    }

    /* Allocate closure */
    JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
    chunk_init(&closure->chunk, c->arena);
    closure->upvalue_count = 0;
    closure->upvalues     = NULL;
    /* Copy proc name to a null-terminated arena string */
    char* name_copy = (char*)arena_alloc(c->arena, proc_name_len + 1);
    memcpy(name_copy, proc_name, proc_name_len);
    name_copy[proc_name_len] = '\0';
    closure->name         = name_copy;
    closure->min_args     = min_args;
    closure->variadic     = is_variadic;
    closure->pinned       = false;
    closure->pin_worker_id = -1;
    closure->is_generator  = false; /* set after body compilation */
    closure->sm_field_count = 0;

    if (use_sm_path) {
      /* SM generator: closure takes (state_obj, resume_value) internally.
         param_count stays as user's count for display; min_args used for arity checks.
         The body bytecode expects state_obj in slot 0, resume_value in slot 1. */
      closure->param_count = 2;
      closure->param_total_slots = 2; /* SM params are always 1 slot each */
      closure->param_names = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal) * 2);
      closure->param_names[0] = jacl_inline_string("__sm", 4);
      closure->param_names[1] = jacl_inline_string("__rv", 4);
      closure->sm_field_count = (uint8_t)sm_analysis_data.state_layout.total_slots;
      closure->is_sm_compiled = true;
    } else {
      closure->param_count = param_count;
      /* Phase 5a: compute param_total_slots (sum of widths for all params) */
      {
        uint8_t total_slots = 0;
        bool has_inline = false;
        StructTypeRegistry* preg = compiler__get_struct_registry(c);
        for (uint8_t pi = 0; pi < param_count; pi++) {
          if (param_types_arr[pi] == TYPE_STRUCT && param_struct_idxs[pi] != UINT32_MAX &&
              preg && param_struct_idxs[pi] < preg->count) {
            StructTypeDef* psdef = preg->defs[param_struct_idxs[pi]];
            if (struct_def_is_user(psdef, preg)) {
              total_slots += (uint8_t)struct__slot_width(preg, param_struct_idxs[pi]);
              has_inline = true;
            } else {
              total_slots += 1;
            }
          } else {
            total_slots += 1;
          }
        }
        closure->param_total_slots = total_slots;
        closure->has_inline_params = has_inline;
      }
      /* Allocate and fill param_names from parsed array */
      if (param_count > 0) {
        closure->param_names = (JaclVal*)arena_alloc(c->arena,
                                  sizeof(JaclVal) * param_count);
        memcpy(closure->param_names, param_names_arr,
               sizeof(JaclVal) * param_count);
      } else {
        closure->param_names = NULL;
      }
    }

    /* Create body compiler with function-level scope */
    Compiler body_compiler;
    compiler__init(&body_compiler, &closure->chunk, c->arena, c->intern_table, c->heap);
    body_compiler.scope_depth    = 1;
    body_compiler.enclosing      = c;
    body_compiler.return_type    = proc_return_type;
    body_compiler.return_struct_idx = proc_return_struct_idx;
    body_compiler.suspension_map = c->suspension_map;
    body_compiler.current_scope_mark = c->current_scope_mark;

    /* Copy global arities to body compiler for suspension lookups */
    {
      Compiler* root = c;
      while (root->enclosing) root = root->enclosing;
      memcpy(body_compiler.global_arities, root->global_arities,
             sizeof(GlobalArity) * root->global_arity_count);
      body_compiler.global_arity_count = root->global_arity_count;
    }

    if (use_sm_path) {
      /* SM: add internal params (__sm, __rv) as locals in slots 0 and 1 */
      compiler__add_local(&body_compiler, jacl_inline_string("__sm", 4), line, col);
      body_compiler.locals[body_compiler.local_count - 1].is_param = true;
      compiler__add_local(&body_compiler, jacl_inline_string("__rv", 4), line, col);
      body_compiler.locals[body_compiler.local_count - 1].is_param = true;
    } else {
      /* Normal: add params as locals in body compiler (slots 0..N-1) with types.
         Use each param's own scope mark (from its AST node) so that macro-generated
         procs correctly match param names to body var-refs at the same scope.
         Phase 5a: struct params are inline (multi-slot) from the start. */
      for (uint8_t i = 0; i < param_count; i++) {
        uint32_t prev_mark = body_compiler.current_scope_mark;
        body_compiler.current_scope_mark = param_scope_marks[i];
        compiler__add_local(&body_compiler, closure->param_names[i], line, col);
        body_compiler.current_scope_mark = prev_mark;
        body_compiler.locals[body_compiler.local_count - 1].is_param = true;
        TYPEINFO_SAVE(body_compiler.locals[body_compiler.local_count - 1],
          ((TypeInfo){ param_types_arr[i], param_struct_idxs[i], param_key_struct_idxs[i] }));
        /* Phase 5a: struct-typed params arrive inline on the stack */
        if (param_types_arr[i] == TYPE_STRUCT && param_struct_idxs[i] != UINT32_MAX) {
          StructTypeRegistry* reg = compiler__get_struct_registry(c);
          if (reg && param_struct_idxs[i] < reg->count) {
            StructTypeDef* psdef = reg->defs[param_struct_idxs[i]];
            if (struct_def_is_user(psdef, reg)) {
              uint32_t width = struct__slot_width(reg, param_struct_idxs[i]);
              body_compiler.locals[body_compiler.local_count - 1].width = (uint16_t)width;
              body_compiler.locals[body_compiler.local_count - 1].is_inline = true;
              /* Reserve padding locals for multi-slot params */
              for (uint32_t w = 1; w < width; w++) {
                compiler__add_local(&body_compiler, jacl_inline_string("", 0), line, col);
                body_compiler.locals[body_compiler.local_count - 1].depth = body_compiler.scope_depth;
                body_compiler.locals[body_compiler.local_count - 1].is_param = true;
              }
            }
          }
        }
      }
    }

    /* For variadic procs, emit OP_COLLECT_VARIADIC as first instruction */
    if (is_variadic && !use_sm_path) {
      compiler__emit_byte(&body_compiler, OP_COLLECT_VARIADIC, line);
      compiler__emit_byte(&body_compiler, min_args, line);
    }

    if (use_sm_path) {
      /* State machine compilation for suspending proc body */
      SuspensionAnalysis* analysis_ptr =
          (SuspensionAnalysis*)arena_alloc(c->arena, sizeof(SuspensionAnalysis));
      *analysis_ptr = sm_analysis_data;
      body_compiler.sm_analysis = analysis_ptr;

      /* Generators (yield) use return_last_value=false (return nil = exhausted).
         Async / transitively suspending procs use return_last_value=true. */
      bool has_yield = false;
      for (uint32_t sp = 0; sp < sm_analysis_data.suspension_count; sp++) {
        if (sm_analysis_data.suspension_points[sp].type == SUSPEND_YIELD) {
          has_yield = true;
          break;
        }
      }
      /* Set has_yield before body compilation so return-site checks see it,
         even for return statements that lexically precede the first yield. */
      body_compiler.has_yield = has_yield;
      {
        AstNode* body_block = args[body_arg_idx];
        if (has_yield) {
          AstNode* bad_tail =
              compiler__find_disallowed_generator_tail(body_block);
          if (bad_tail) {
            compiler__error(c, bad_tail->start.line, bad_tail->start.column,
                "generator tail expression produces a value that stream "
                "consumers silently discard; append `;` to drop it, "
                "`yield` it, bind it, or restructure");
          }
        }
        uint32_t stmt_count = body_block->data.block.count;
        AstNode** body_stmts = body_block->data.block.commands;
        compiler__compile_sm_stmts(&body_compiler, body_stmts, stmt_count,
                                    line, !has_yield);
      }
    } else {
      /* Normal non-suspending proc */
      compiler__compile_block_expr(&body_compiler, args[body_arg_idx]);

      /* Return type checking: body's last expression type must match declared.
       * Read from the AST. For an `AST_RETURN`-tail body, take the return
       * value's inferred type (the return wraps the value as the proc's
       * return); for any other tail, take the block's inferred type. */
      AstNode* body_blk = args[body_arg_idx];
      JaclType body_type = TYPE_DYN;
      uint32_t body_struct_idx = UINT32_MAX;
      if (body_blk->type == AST_BLOCK && body_blk->data.block.count > 0 &&
          !body_blk->data.block.trailing_semi) {
        AstNode* tail = body_blk->data.block.commands[body_blk->data.block.count - 1];
        if (tail->type == AST_RETURN && tail->data.return_stmt.value) {
          body_type = (JaclType)tail->data.return_stmt.value->inferred_type;
          body_struct_idx = tail->data.return_stmt.value->inferred_struct_idx;
        } else {
          body_type = (JaclType)tail->inferred_type;
          body_struct_idx = tail->inferred_struct_idx;
        }
      }
      (void)body_struct_idx;
      if (proc_return_type != TYPE_DYN) {
        if (body_type != TYPE_DYN && body_type != proc_return_type) {
          char err_msg[128];
          jacl_format_proc_return_mismatch(err_msg, sizeof(err_msg),
              proc_name, proc_name_len, proc_return_type, body_type);
          compiler__error(c, line, col, err_msg);
        }
      } else if (body_type == TYPE_STRUCT) {
        /* Untyped proc returning a struct — require a return type annotation */
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg),
                 "proc %.*s returns a struct but has no return type; "
                 "add a return type annotation",
                 (int)proc_name_len, proc_name);
        compiler__error(c, line, col, err_msg);
      }

      /* Emit implicit return (Phase 5b: wide return for struct-returning procs) */
      compiler__emit_return(&body_compiler, line);
    }

    /* Propagate errors from body compiler */
    c->error_count += body_compiler.error_count;
    if (!c->first_error && body_compiler.first_error) {
      c->first_error = body_compiler.first_error;
    }

    /* Set upvalue count and generator flag on the closure */
    closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;
    closure->is_generator  = body_compiler.has_yield;

    /* US-008: compute upvalue_total_slots (sum of all upvalue widths) */
    {
      uint16_t total = 0;
      for (uint32_t i = 0; i < body_compiler.upvalue_count; i++)
        total += body_compiler.upvalues[i].width;
      closure->upvalue_total_slots = total;
    }

    /* Generators return streams — override return type for type tracking */
    if (body_compiler.has_yield && proc_return_type == TYPE_DYN) {
      proc_return_type = TYPE_STREAM;
    }

    /* Store closure in parent's constant pool */
    uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(closure));

    /* Emit OP_CLOSURE to push the closure value, followed by upvalue descriptors */
    compiler__emit_byte(c, OP_CLOSURE, line);
    compiler__emit_u16(c, closure_idx, line);
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
      compiler__emit_byte(c, body_compiler.upvalues[i].is_local, line);
      compiler__emit_byte(c, body_compiler.upvalues[i].index, line);
      compiler__emit_byte(c, (uint8_t)body_compiler.upvalues[i].width, line);
    }

    /* Anonymous lambda (empty name): closure is already on stack, done */
    if (proc_name_len == 0) {
      c->last_expr_type = TYPE_CLOSURE;
      return;
    }

    /* Arena-allocate param_types array for binding */
    JaclType* stored_param_types = NULL;
    if (param_count > 0) {
      stored_param_types = (JaclType*)arena_alloc(c->arena,
                              sizeof(JaclType) * param_count);
      memcpy(stored_param_types, param_types_arr, sizeof(JaclType) * param_count);
    }

    /* Look up suspension status from analysis map */
    JaclVal name_val = compiler__name_val(c->heap, c->intern_table, proc_name, proc_name_len);
    bool proc_suspends = false;
    if (c->suspension_map) {
      proc_suspends = suspension_map_lookup(c->suspension_map, name_val);
    }

    /* Determine if this proc transitively captures mutable state (US-003).
       A proc captures_mutable if any of its upvalues is_mutable or
       captures_mutable (transitive through nested closures). */
    bool proc_captures_mutable = false;
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
      if (body_compiler.upvalues[i].is_mutable ||
          body_compiler.upvalues[i].captures_mutable) {
        proc_captures_mutable = true;
        break;
      }
    }

    /* Bind the name — use user_param_count for arity checks */
    if (c->sm_analysis) {
      /* SM mode: store closure in state object field (survives suspension) */
      int field_idx = sm__find_field(&c->sm_analysis->state_layout, name_val);
      if (field_idx >= 0) {
        compiler__emit_byte(c, OP_SET_STATE_FIELD, line);
        compiler__emit_byte(c, (uint8_t)field_idx, line);
        /* Push nil (proc definition is a statement) */
        compiler__emit_byte(c, OP_NIL, line);
      } else {
        /* Name not in state layout — shouldn't happen, fall through to local */
        compiler__add_local(c, name_val, line, col);
        c->locals[c->local_count - 1].known_arity = is_variadic ? -1 : (int16_t)user_param_count;
        c->locals[c->local_count - 1].return_type = proc_return_type;
        c->locals[c->local_count - 1].return_struct_idx = proc_return_struct_idx;
        c->locals[c->local_count - 1].param_types = stored_param_types;
        c->locals[c->local_count - 1].suspends    = proc_suspends;
        c->locals[c->local_count - 1].captures_mutable = proc_captures_mutable;
        compiler__emit_byte(c, OP_NIL, line);
      }
    } else if (c->scope_depth > 0 && !c->force_global_procs) {
      /* Local scope: closure is on stack as local */
      compiler__add_local(c, name_val, line, col);
      c->locals[c->local_count - 1].known_arity = is_variadic ? -1 : (int16_t)user_param_count;
      c->locals[c->local_count - 1].return_type = proc_return_type;
      c->locals[c->local_count - 1].return_struct_idx = proc_return_struct_idx;
      c->locals[c->local_count - 1].param_types = stored_param_types;
      c->locals[c->local_count - 1].suspends    = proc_suspends;
      c->locals[c->local_count - 1].captures_mutable = proc_captures_mutable;
      /* proc returns the closure value (enables make-adder pattern) */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)(c->local_count - 1), line);
    } else {
      /* Global scope */
      JaclVal proc_key = compiler__global_name_val(c, proc_name, proc_name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, proc_key);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      compiler__set_global_arity(c, name_val, is_variadic ? -1 : (int16_t)user_param_count);
      /* Store param types, return type, and suspension in GlobalArity */
      {
        GlobalArity* ga = compiler__find_global(c, name_val);
        if (ga) {
          ga->return_type = proc_return_type;
          ga->return_struct_idx = proc_return_struct_idx;
          ga->suspends    = proc_suspends;
          ga->captures_mutable = proc_captures_mutable;
          for (uint8_t i = 0; i < user_param_count && i < COMPILER_MAX_PROC_PARAMS; i++) {
            ga->param_types[i] = param_types_arr[i];
          }
        }
      }
    }
    c->last_expr_type = TYPE_CLOSURE;
    return;
  }

  /* if conditional */
  if (hid == HEAD_IF) {
    if (argc != 2 && argc != 3) {
      compiler__builtin_arity_error(c, line, col, "if", "2 or 3 arguments", argc);
      return;
    }
    if (args[1]->type != AST_BLOCK) {
      compiler__error(c, line, col, "if then-branch must be a block");
      return;
    }
    if (argc == 3 && args[2]->type != AST_BLOCK) {
      compiler__error(c, line, col, "if else-branch must be a block");
      return;
    }

    /* Detect [box? Type $var] or [box? [Vec Type] $var] condition for flow typing */
    bool has_narrowing = false;
    uint32_t saved_narrowing_count = c->narrowing_count;
    if (args[0]->type == AST_COMMAND &&
        args[0]->data.command.head &&
        compiler__head_matches(args[0]->data.command.head, "box?", 4) &&
        args[0]->data.command.arg_count == 2 &&
        args[0]->data.command.args[1]->type == AST_VAR_REF) {
      uint32_t type_idx = UINT32_MAX;
      uint32_t key_type_idx = UINT32_MAX;
      JaclType box_type = TYPE_DYN;
      AstNode* type_arg = args[0]->data.command.args[0];

      if (type_arg->type == AST_LIT_STRING) {
        /* [box? StructName $var] — struct narrowing */
        const char* tname = type_arg->data.lit_string.value;
        uint32_t tlen = type_arg->data.lit_string.length;
        if (tlen == 3 && memcmp(tname, "dyn", 3) == 0) {
          type_idx = 0;
          box_type = TYPE_DYN;
        } else {
          type_idx = struct_registry__find(compiler__get_struct_registry(c), tname, tlen);
          box_type = TYPE_STRUCT;
        }
      } else {
        /* [box? [Vec Type] $var] or [box? [Map Type] $var] — typed collection narrowing */
        AstNode* ct_en = NULL;
        AstNode* ct_kn = NULL;
        int ct_k = compiler__typed_collection_expr(type_arg, &ct_en, &ct_kn);
        if (ct_k > 0) {
          type_idx = struct_registry__find(compiler__get_struct_registry(c),
              ct_en->data.lit_string.value, ct_en->data.lit_string.length);
          box_type = (ct_k == 1) ? TYPE_TYPED_VEC : TYPE_TYPED_MAP;
          if (ct_k == 3 && ct_kn) {
            key_type_idx = struct_registry__find(compiler__get_struct_registry(c),
                ct_kn->data.lit_string.value, ct_kn->data.lit_string.length);
          }
        }
      }

      if (type_idx != UINT32_MAX) {
        /* Resolve the variable to a local slot */
        AstNode* var_node = args[0]->data.command.args[1];
        uint32_t vlen = var_node->data.var_ref.length;
        if (vlen <= 128) {
          JaclVal vname = compiler__name_val(c->heap, c->intern_table,
                                              var_node->data.var_ref.name, vlen);
          int slot = compiler__resolve_local(c, vname);
          if (slot >= 0 && c->narrowing_count < 8) {
            has_narrowing = true;
            c->narrowings[c->narrowing_count].local_slot = (uint16_t)slot;
            c->narrowings[c->narrowing_count].box_type_idx = type_idx;
            c->narrowings[c->narrowing_count].box_key_type_idx = key_type_idx;
            c->narrowings[c->narrowing_count].box_type = box_type;
            c->narrowing_count++;
          }
        }
      }
    }

    /* Compile condition */
    compiler__compile_node(c, args[0]);

    /* OP_JUMP_IF_FALSE over then-body */
    uint32_t then_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Compile then-body as expression (narrowing is active) */
    compiler__compile_block_expr(c, args[1]);

    /* Save then-branch type for unification. */
    JaclType then_type = (JaclType)args[1]->inferred_type;
    uint32_t then_struct_idx = args[1]->inferred_struct_idx;

    /* Pop narrowing before else-branch */
    if (has_narrowing) {
      c->narrowing_count = saved_narrowing_count;
    }

    /* OP_JUMP over else-body */
    uint32_t else_jump = compiler__emit_jump(c, OP_JUMP, line);

    /* Patch JUMP_IF_FALSE to here */
    compiler__patch_jump(c, then_jump);

    JaclType else_type;
    if (argc == 3) {
      /* Compile else-body as expression */
      compiler__compile_block_expr(c, args[2]);
      else_type = (JaclType)args[2]->inferred_type;
    } else {
      /* No else: push nil */
      compiler__emit_byte(c, OP_NIL, line);
      else_type = TYPE_NIL;
    }

    /* Patch JUMP to here */
    compiler__patch_jump(c, else_jump);

    /* Type unification: preserve type if both branches agree */
    if (then_type == else_type) {
      uint32_t else_struct_idx =
          (argc == 3) ? args[2]->inferred_struct_idx : UINT32_MAX;
      if (then_type != TYPE_STRUCT || then_struct_idx == else_struct_idx) {
        c->last_expr_type = then_type;
      } else {
        c->last_expr_type = TYPE_DYN;
      }
    } else {
      c->last_expr_type = TYPE_DYN;
    }
    return;
  }

  /* while loop */
  if (hid == HEAD_WHILE) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "while", "2 arguments", argc);
      return;
    }
    if (args[1]->type != AST_BLOCK) {
      compiler__error(c, line, col, "while body must be a block");
      return;
    }

    /* Push loop context for break/continue */
    if (c->loop_depth >= COMPILER_LOOP_DEPTH_MAX) {
      compiler__error(c, line, col, "too many nested loops");
      return;
    }
    LoopContext* lctx = &c->loop_stack[c->loop_depth++];
    lctx->break_patch_count = 0;
    lctx->continue_patch_count = 0;
    lctx->local_count_at_loop = c->local_count;
    /* while has no separate iter-state phase — body locals are
     * everything declared inside, so the two snapshots coincide. */
    lctx->body_local_count = c->local_count;
    lctx->is_for_loop = false;

    /* Loop-start label */
    uint32_t loop_start = c->chunk->code_count;
    lctx->loop_start = loop_start;

    /* Compile condition */
    compiler__compile_node(c, args[0]);

    /* OP_JUMP_IF_FALSE to exit */
    uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Scope for while body: ensures locals (def) are cleaned up each iteration */
    compiler__begin_scope(c);
    uint32_t body_count = args[1]->data.block.count;
    for (uint32_t i = 0; i < body_count; i++) {
      compiler__compile_node(c, args[1]->data.block.commands[i]);
      compiler__emit_check_error(c, line);
    }
    compiler__end_scope(c, line);

    /* OP_LOOP back to loop_start */
    compiler__emit_byte(c, OP_LOOP, line);
    uint32_t offset = c->chunk->code_count - loop_start + 2;
    compiler__emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
    compiler__emit_byte(c, (uint8_t)(offset & 0xFF), line);

    /* Patch exit jump to here */
    compiler__patch_jump(c, exit_jump);

    /* Normal exit: while returns nil */
    compiler__emit_byte(c, OP_NIL, line);

    /* Jump over the break landing zone */
    uint32_t skip_break = compiler__emit_jump(c, OP_JUMP, line);

    /* Patch all break jumps to here (break value already on stack) */
    for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
      compiler__patch_jump(c, lctx->break_patches[i]);
    }

    /* Patch skip_break to here */
    compiler__patch_jump(c, skip_break);

    /* Pop loop context */
    c->loop_depth--;
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* for — collection-based iteration with inlined body
     Forms:
       [for {init; cond; step} { body }]   — C-style counted loop
       [for $collection { body }]           — implicit $it binding
       [for $collection name { body }]      — explicit binding
       [for $collection $callback]           — HOF via OP_EACH
  */
  if (hid == HEAD_FOR) {
    /* C-style for: [for {init; cond; step} { body }] */
    if (argc == 2 && args[0]->type == AST_BLOCK && args[1]->type == AST_BLOCK) {
      AstNode* ctrl       = args[0];
      AstNode* body_block = args[1];

      if (ctrl->data.block.count != 3) {
        compiler__error(c, line, col,
            "C-style for control block must have exactly 3 parts: init; cond; step");
        return;
      }

      AstNode* init_node = ctrl->data.block.commands[0];
      AstNode* cond_node = ctrl->data.block.commands[1];
      AstNode* step_node = ctrl->data.block.commands[2];

      if (c->loop_depth >= COMPILER_LOOP_DEPTH_MAX) {
        compiler__error(c, line, col, "too many nested loops");
        return;
      }

      /* Two scopes:
       *   - OUTER (init): vars persist across iterations
       *   - INNER (body): vars declared inside the body are popped each
       *     iteration via OP_POP_N (compiler__end_scope), and again on
       *     `continue` (mirrors the while-loop cleanup pattern).
       * Without the inner scope, `def` / `mut` inside the body would
       * accumulate stack slots forever — eventually overflowing or
       * (worse) silently making later iterations read the FIRST iter's
       * value through the same compile-time slot index. */
      uint32_t saved_local_count = c->local_count;
      compiler__begin_scope(c);                /* OUTER (init) scope */

      /* Compile init (runs once before the loop) */
      compiler__compile_node(c, init_node);
      compiler__emit_check_error(c, line);

      /* Push loop context — is_for_loop=true for forward-jump continue.
       * local_count_at_loop snapshots count BEFORE init, used by break
       * to clean up ALL loop locals on exit. body_local_count snapshots
       * count AFTER init (before body), used by continue to clean up
       * body locals only (init vars persist across iterations). */
      LoopContext* lctx = &c->loop_stack[c->loop_depth++];
      lctx->break_patch_count    = 0;
      lctx->continue_patch_count = 0;
      lctx->local_count_at_loop  = saved_local_count;
      lctx->body_local_count     = c->local_count;
      lctx->is_for_loop          = true;

      /* Loop start: condition check */
      uint32_t loop_start = c->chunk->code_count;
      lctx->loop_start = loop_start;

      /* Compile condition */
      compiler__compile_node(c, cond_node);

      /* JUMP_IF_FALSE → exit */
      uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

      /* INNER (body) scope: opened per iteration in the compiled bytecode
       * (end_scope below emits OP_POP_N before the OP_LOOP back-edge). */
      compiler__begin_scope(c);

      /* Compile body statements inline */
      uint32_t body_count = body_block->data.block.count;
      for (uint32_t i = 0; i < body_count; i++) {
        compiler__compile_node(c, body_block->data.block.commands[i]);
        compiler__emit_check_error(c, line);
      }

      /* End INNER body scope — emits OP_POP_N for body-declared locals
       * so they don't accumulate iter-over-iter. */
      compiler__end_scope(c, line);

      /* Continue target: patch all continue forward jumps here. Each
       * continue site has already emitted its own OP_POP_N (see
       * HEAD_CONTINUE for-loop branch) so the stack state matches the
       * post-end_scope state. */
      for (uint32_t i = 0; i < lctx->continue_patch_count; i++) {
        compiler__patch_jump(c, lctx->continue_patches[i]);
      }

      /* Compile step expression */
      compiler__compile_node(c, step_node);
      compiler__emit_check_error(c, line);

      /* Loop back to condition */
      compiler__emit_byte(c, OP_LOOP, line);
      uint32_t back_offset = c->chunk->code_count - loop_start + 2;
      compiler__emit_byte(c, (uint8_t)((back_offset >> 8) & 0xFF), line);
      compiler__emit_byte(c, (uint8_t)(back_offset & 0xFF), line);

      /* Exit: patch conditional jump */
      compiler__patch_jump(c, exit_jump);

      /* End OUTER scope: pop init variable(s) */
      compiler__end_scope(c, line);

      /* Normal exit: push nil */
      compiler__emit_byte(c, OP_NIL, line);

      /* Jump over break landing zone */
      uint32_t skip_break = compiler__emit_jump(c, OP_JUMP, line);

      /* Break landing zone (break value already on stack via OP_CLOSE_LOOP) */
      for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
        compiler__patch_jump(c, lctx->break_patches[i]);
      }

      /* Convergence point */
      compiler__patch_jump(c, skip_break);

      /* Pop loop context */
      c->loop_depth--;
      return;
    }

    /* Detect HOF callback form: [for $collection $callback] */
    if (argc == 2 && args[1]->type == AST_VAR_REF) {
      compiler__compile_hof_builtin(c, "each", args, argc, OP_EACH, line, col);
      return;
    }

    /* Determine binding name and body block */
    const char* bind_name = "it";
    uint32_t bind_name_len = 2;
    AstNode* body_block = NULL;

    if (argc == 2 && args[1]->type == AST_BLOCK) {
      /* [for $collection { body }] — implicit $it */
      body_block = args[1];
    } else if (argc == 3 && args[1]->type == AST_LIT_STRING &&
               args[2]->type == AST_BLOCK) {
      /* [for $collection name { body }] — explicit binding */
      bind_name = args[1]->data.lit_string.value;
      bind_name_len = args[1]->data.lit_string.length;
      body_block = args[2];
    } else if (argc == 2 && args[1]->type == AST_COMMAND) {
      /* [for $collection [\ body]] — lambda callback via OP_EACH */
      compiler__compile_hof_builtin(c, "each", args, argc, OP_EACH, line, col);
      return;
    } else {
      compiler__error(c, line, col,
          "for requires: $collection { body }, $collection name { body }, "
          "or $collection $callback");
      return;
    }

    if (bind_name_len > 128) {
      compiler__error(c, line, col, "for binding name exceeds 128-byte limit");
      return;
    }

    /* Check for suspension in block body (inlined for still can't suspend) */
    if (ast__contains_suspension(body_block, c->suspension_map,
                                  c->heap, c->intern_table)) {
      compiler__error(c, line, col,
          "cannot suspend inside non-suspending callback");
      return;
    }

    /* Check loop depth */
    if (c->loop_depth >= COMPILER_LOOP_DEPTH_MAX) {
      compiler__error(c, line, col, "too many nested loops");
      return;
    }

    /* Begin scope for hidden locals */
    compiler__begin_scope(c);
    uint32_t saved_local_count = c->local_count;

    /* Compile collection → local __col */
    compiler__compile_node(c, args[0]);
    JaclType col_type = (JaclType)args[0]->inferred_type;
    compiler__add_local(c, jacl_inline_string("__col", 5), line, col);

    uint8_t col_slot = (uint8_t)(saved_local_count);

    if (col_type == TYPE_STREAM) {
      /* ====== Stream-specific inlined for loop ======
         Hidden locals: __col (stream), elem
         Loop: STREAM_NEXT → check exhausted → bind → body → LOOP */

      /* Element placeholder → local $it/name (starts as nil) */
      compiler__emit_byte(c, OP_NIL, line);
      JaclVal bind_val = compiler__name_val(c->heap, c->intern_table, bind_name, bind_name_len);
      compiler__add_local(c, bind_val, line, col);
      uint8_t elem_slot = (uint8_t)(saved_local_count + 1);
      /* Note: we don't narrow the loop binding's static type from the
       * stream's element idx here. The stream stores tagged JaclVals
       * (yield emits jacl_i32 / jacl_str / ...), but a narrowed local
       * type causes downstream codegen to expect the unboxed wide
       * representation used by typed-vec / OP_CONST_I64. Decoupling
       * the two needs an unboxing op on STREAM_NEXT — deferred
       * (see NOT_IMPLEMENTED.md §4). */

      /* Push loop context. local_count_at_loop is the count BEFORE
       * any iter-state locals (used by break to clean all of them).
       * body_local_count is the count AFTER __col + elem (used by
       * continue to clean only body locals, leaving iter state). */
      LoopContext* lctx = &c->loop_stack[c->loop_depth++];
      lctx->break_patch_count = 0;
      lctx->continue_patch_count = 0;
      lctx->local_count_at_loop = saved_local_count;
      lctx->body_local_count = c->local_count;
      lctx->is_for_loop = true;

      /* --- Loop start --- */
      uint32_t loop_start = c->chunk->code_count;
      lctx->loop_start = loop_start;

      /* Pull next element: push stream, OP_STREAM_NEXT */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, col_slot, line);
      compiler__emit_byte(c, OP_STREAM_NEXT, line);

      /* Check exhaustion: push stream, OP_IS_STREAM_EXHAUSTED */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, col_slot, line);
      compiler__emit_byte(c, OP_IS_STREAM_EXHAUSTED, line);

      /* If NOT exhausted, jump to body */
      uint32_t not_done_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

      /* Exhausted path: pop nil from STREAM_NEXT, jump to normal exit */
      compiler__emit_byte(c, OP_POP, line);
      uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP, line);

      /* --- Body start (not exhausted) --- */
      compiler__patch_jump(c, not_done_jump);

      /* US-005: Check if pulled value is an error — if so, exit loop with error */
      uint32_t error_exit_jump = compiler__emit_jump(c, OP_JUMP_IF_ERROR, line);

      /* Bind element: SET_LOCAL + POP */
      compiler__emit_byte(c, OP_SET_LOCAL, line);
      compiler__emit_byte(c, elem_slot, line);
      compiler__emit_byte(c, OP_POP, line);

      /* Compile body statements inline */
      uint32_t body_count = body_block->data.block.count;
      for (uint32_t i = 0; i < body_count; i++) {
        compiler__compile_node(c, body_block->data.block.commands[i]);
        compiler__emit_check_error(c, line);
      }

      /* --- Continue target --- */
      for (uint32_t i = 0; i < lctx->continue_patch_count; i++) {
        compiler__patch_jump(c, lctx->continue_patches[i]);
      }

      /* Loop back to start */
      compiler__emit_byte(c, OP_LOOP, line);
      uint32_t back_offset = c->chunk->code_count - loop_start + 2;
      compiler__emit_byte(c, (uint8_t)((back_offset >> 8) & 0xFF), line);
      compiler__emit_byte(c, (uint8_t)(back_offset & 0xFF), line);

      /* --- Normal exit --- */
      compiler__patch_jump(c, exit_jump);

      /* End scope: pop hidden locals (__col, elem) */
      compiler__end_scope(c, line);

      /* Normal exit: push nil */
      compiler__emit_byte(c, OP_NIL, line);

      /* Jump over break/error landing zone */
      uint32_t skip_landing = compiler__emit_jump(c, OP_JUMP, line);

      /* US-005: Error exit path — error value is on stack */
      compiler__patch_jump(c, error_exit_jump);
      /* Pop hidden locals under top, preserving error value */
      compiler__emit_byte(c, OP_CLOSE_LOOP, line);
      compiler__emit_byte(c, 2, line);  /* __col and elem */
      /* Jump to convergence (error value remains on stack) */
      uint32_t error_done_jump = compiler__emit_jump(c, OP_JUMP, line);

      /* Break landing zone */
      for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
        compiler__patch_jump(c, lctx->break_patches[i]);
      }

      /* Convergence */
      compiler__patch_jump(c, skip_landing);
      compiler__patch_jump(c, error_done_jump);

      /* Pop loop context */
      c->loop_depth--;
      return;
    }

    /* ====== Vector-based inlined for loop (original path) ======
       Hidden locals: __col, __len, __idx, $it/name */

    /* Track typed vec element type for the loop binding */
    bool is_typed_vec_loop = (col_type == TYPE_TYPED_VEC);
    uint32_t elem_struct_idx = args[0]->inferred_struct_idx;

    /* Compute length → local __len */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, (uint8_t)(c->local_count - 1), line);
    compiler__emit_byte(c, is_typed_vec_loop ? OP_TYPED_VEC_LEN : OP_VEC_LEN, line);
    compiler__add_local(c, jacl_inline_string("__len", 5), line, col);

    /* Counter → local __idx (starts at 0) */
    compiler__emit_constant(c, jacl_i32(0), line);
    compiler__add_local(c, jacl_inline_string("__idx", 5), line, col);

    /* Element placeholder → local $it/name (starts as nil) */
    compiler__emit_byte(c, OP_NIL, line);
    JaclVal bind_val = compiler__name_val(c->heap, c->intern_table, bind_name, bind_name_len);
    compiler__add_local(c, bind_val, line, col);
    if (is_typed_vec_loop) {
      c->locals[c->local_count - 1].type = TYPE_STRUCT;
      c->locals[c->local_count - 1].struct_type_idx = elem_struct_idx;
      /* Mark as inline and add padding locals for width > 1 */
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      uint32_t elem_width = struct__slot_width(reg, elem_struct_idx);
      c->locals[c->local_count - 1].width = (uint16_t)elem_width;
      c->locals[c->local_count - 1].is_inline = true;
      for (uint32_t w = 1; w < elem_width; w++) {
        compiler__emit_byte(c, OP_NIL, line);
        compiler__add_local(c, jacl_inline_string("", 0), line, col);
        c->locals[c->local_count - 1].depth = c->scope_depth;
      }
    }

    uint8_t len_slot = (uint8_t)(saved_local_count + 1);
    uint8_t idx_slot = (uint8_t)(saved_local_count + 2);
    uint8_t elem_slot = (uint8_t)(saved_local_count + 3);

    /* Push loop context. local_count_at_loop is the count BEFORE any
     * iter-state locals (used by break to clean them all on exit).
     * body_local_count is the count AFTER all iter-state hidden locals
     * (__col, __len, __idx, elem, any typed-vec padding) — used by
     * continue to clean body-declared locals only. */
    LoopContext* lctx = &c->loop_stack[c->loop_depth++];
    lctx->break_patch_count = 0;
    lctx->continue_patch_count = 0;
    lctx->local_count_at_loop = saved_local_count;
    lctx->body_local_count = c->local_count;
    lctx->is_for_loop = true;

    /* --- Condition check (loop start for OP_LOOP backward jumps) --- */
    uint32_t loop_start = c->chunk->code_count;
    lctx->loop_start = loop_start;

    /* __idx < __len */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, len_slot, line);
    compiler__emit_byte(c, OP_LT, line);

    /* Exit if false */
    uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Update element: $it = __col[__idx] */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, col_slot, line);
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    if (is_typed_vec_loop) {
      compiler__emit_byte(c, OP_TYPED_VEC_GET_INLINE, line);
      compiler__emit_u16(c, (uint16_t)elem_struct_idx, line);
      compiler__emit_byte(c, OP_INLINE_TO_LOCAL, line);
      compiler__emit_byte(c, elem_slot, line);
      compiler__emit_u16(c, (uint16_t)elem_struct_idx, line);
    } else {
      compiler__emit_byte(c, OP_VEC_GET, line);
      compiler__emit_byte(c, OP_SET_LOCAL, line);
      compiler__emit_byte(c, elem_slot, line);
      compiler__emit_byte(c, OP_POP, line);  /* discard SET_LOCAL's TOS */
    }

    /* Compile body statements inline */
    uint32_t body_count = body_block->data.block.count;
    for (uint32_t i = 0; i < body_count; i++) {
      compiler__compile_node(c, body_block->data.block.commands[i]);
      compiler__emit_check_error(c, line);
    }

    /* --- Continue target: patch forward jumps from continue --- */
    for (uint32_t i = 0; i < lctx->continue_patch_count; i++) {
      compiler__patch_jump(c, lctx->continue_patches[i]);
    }

    /* Increment: __idx = __idx + 1 */
    compiler__emit_byte(c, OP_GET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    compiler__emit_constant(c, jacl_i32(1), line);
    compiler__emit_byte(c, OP_ADD, line);
    compiler__emit_byte(c, OP_SET_LOCAL, line);
    compiler__emit_byte(c, idx_slot, line);
    compiler__emit_byte(c, OP_POP, line);  /* discard SET_LOCAL's TOS */

    /* Loop back to condition */
    compiler__emit_byte(c, OP_LOOP, line);
    uint32_t back_offset = c->chunk->code_count - loop_start + 2;
    compiler__emit_byte(c, (uint8_t)((back_offset >> 8) & 0xFF), line);
    compiler__emit_byte(c, (uint8_t)(back_offset & 0xFF), line);

    /* --- Exit --- */
    compiler__patch_jump(c, exit_jump);

    /* End scope: pop hidden locals (__col, __len, __idx, $it) */
    compiler__end_scope(c, line);

    /* Normal exit: push nil */
    compiler__emit_byte(c, OP_NIL, line);

    /* Jump over break landing zone */
    uint32_t skip_break = compiler__emit_jump(c, OP_JUMP, line);

    /* Break landing zone (break value already on stack, locals cleaned up) */
    for (uint32_t i = 0; i < lctx->break_patch_count; i++) {
      compiler__patch_jump(c, lctx->break_patches[i]);
    }

    /* Convergence */
    compiler__patch_jump(c, skip_break);

    /* Pop loop context */
    c->loop_depth--;
    return;
  }

  /* break [value] — bracket form */
  if (hid == HEAD_BREAK) {
    if (argc > 1) {
      compiler__builtin_arity_error(c, line, col, "break", "0 or 1 arguments", argc);
      return;
    }
    if (c->loop_depth == 0) {
      compiler__error(c, line, col, "break outside of loop");
      return;
    }
    LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
    } else {
      compiler__emit_byte(c, OP_NIL, line);
    }
    /* For inlined for-loops, clean up hidden locals under the break value */
    if (lctx->is_for_loop) {
      uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
      if (cleanup > 0) {
        compiler__emit_byte(c, OP_CLOSE_LOOP, line);
        compiler__emit_byte(c, (uint8_t)cleanup, line);
      }
    }
    if (lctx->break_patch_count < COMPILER_BREAK_PATCHES_MAX) {
      lctx->break_patches[lctx->break_patch_count++] =
          compiler__emit_jump(c, OP_JUMP, line);
    } else {
      compiler__error(c, line, col, "too many break statements in loop");
    }
    return;
  }

  /* continue — bracket form */
  if (hid == HEAD_CONTINUE) {
    if (argc != 0) {
      compiler__builtin_arity_error(c, line, col, "continue", "0 arguments", argc);
      return;
    }
    if (c->loop_depth == 0) {
      compiler__error(c, line, col, "continue outside of loop");
      return;
    }
    LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
    if (lctx->is_for_loop) {
      /* For-loop: pop any body-declared locals before forward-jumping
       * to the continue landing. body_local_count is the snapshot AFTER
       * iter-state locals (so we leave init/__col/elem/__idx alone) but
       * BEFORE body locals (so any `def`/`mut` inside the body gets
       * cleaned up). */
      uint32_t cleanup = c->local_count - lctx->body_local_count;
      if (cleanup > 0) {
        compiler__emit_byte(c, OP_POP_N, line);
        compiler__emit_byte(c, (uint8_t)cleanup, line);
      }
      if (lctx->continue_patch_count < COMPILER_CONTINUE_PATCHES_MAX) {
        lctx->continue_patches[lctx->continue_patch_count++] =
            compiler__emit_jump(c, OP_JUMP, line);
      } else {
        compiler__error(c, line, col, "too many continue statements in loop");
      }
    } else {
      /* While-loop: pop body-scope locals, then backward-jump to condition */
      uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
      if (cleanup > 0) {
        compiler__emit_byte(c, OP_POP_N, line);
        compiler__emit_byte(c, (uint8_t)cleanup, line);
      }
      compiler__emit_byte(c, OP_LOOP, line);
      uint32_t offset = c->chunk->code_count - lctx->loop_start + 2;
      compiler__emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
      compiler__emit_byte(c, (uint8_t)(offset & 0xFF), line);
    }
    /* continue must leave a value for the statement (popped by CHECK_ERROR) */
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  /* return [value] — bracket form */
  if (hid == HEAD_RETURN) {
    if (argc > 1) {
      compiler__builtin_arity_error(c, line, col, "return", "0 or 1 arguments", argc);
      return;
    }
    if (c->has_yield && argc == 1) {
      compiler__error(c, line, col,
          "cannot return a value from a generator (proc contains `yield`); "
          "stream consumers discard it. Use `[return]` for early exit.");
      return;
    }
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
    } else {
      compiler__emit_byte(c, OP_NIL, line);
    }
    /* Phase 5b: wide return for struct-returning procs */
    if (argc == 1) {
      compiler__emit_return(c, line);
    } else {
      compiler__emit_byte(c, OP_RETURN, line);
    }
    return;
  }

  /* try special form: [try { body } name { handler }] */
  if (hid == HEAD_TRY) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "try", "3 arguments", argc);
      return;
    }
    if (args[0]->type != AST_BLOCK) {
      compiler__error(c, line, col, "try body must be a block");
      return;
    }
    if (args[1]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "try binding must be a name");
      return;
    }
    if (args[2]->type != AST_BLOCK) {
      compiler__error(c, line, col, "try handler must be a block");
      return;
    }

    /* Get binding name */
    uint32_t bind_len = args[1]->data.lit_string.length;
    if (bind_len > 128) {
      compiler__error(c, line, col, "try binding name exceeds 128-byte limit");
      return;
    }
    JaclVal bind_name = compiler__name_val(c->heap, c->intern_table, args[1]->data.lit_string.value, bind_len);

    /* Save try context */
    bool saved_in_try = c->in_try_body;
    uint32_t saved_patch_start = c->try_patch_count;
    c->in_try_body = true;

    /* Compile try body as block expression */
    compiler__compile_block_expr(c, args[0]);

    /* Restore in_try_body (patches still need to be applied) */
    c->in_try_body = saved_in_try;

    /* After body: check if final result is an error */
    uint32_t handler_jump = compiler__emit_jump(c, OP_JUMP_IF_ERROR, line);

    /* Normal path: skip handler */
    uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);

    /* Handler entry point */
    uint32_t handler_pos = c->chunk->code_count;

    /* Patch OP_JUMP_IF_ERROR to handler */
    compiler__patch_jump(c, handler_jump);

    /* Patch all OP_CHECK_ERROR offsets from try body to handler */
    for (uint32_t i = saved_patch_start; i < c->try_patch_count; i++) {
      uint32_t patch_pos = c->try_patches[i];
      uint32_t jump_dist = handler_pos - (patch_pos + 2);
      c->chunk->code[patch_pos]     = (uint8_t)((jump_dist >> 8) & 0xFF);
      c->chunk->code[patch_pos + 1] = (uint8_t)(jump_dist & 0xFF);
    }
    c->try_patch_count = saved_patch_start;

    /* Handler: error value is on stack, bind as local */
    uint32_t handler_scope_start = c->local_count;
    compiler__begin_scope(c);
    compiler__add_local(c, bind_name, line, col);

    /* Compile handler block expression */
    compiler__compile_block_expr(c, args[2]);

    /* Clean up handler scope (pop binding while keeping result) */
    uint32_t handler_pop = c->local_count - handler_scope_start;
    c->scope_depth--;
    c->local_count = handler_scope_start;
    if (handler_pop > 0) {
      compiler__emit_byte(c, OP_SET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)handler_scope_start, line);
      compiler__emit_byte(c, OP_POP_N, line);
      compiler__emit_byte(c, (uint8_t)handler_pop, line);
    }

    /* Patch skip jump to here (end of try expression) */
    compiler__patch_jump(c, skip_jump);
    return;
  }

  /* with-ctx {overrides} { body } — explicit context forking */
  if (hid == HEAD_WITH_CTX) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "with-ctx", "2 arguments", argc);
      return;
    }
    if (args[0]->type != AST_BLOCK) {
      compiler__error(c, line, col, "with-ctx overrides must be a block");
      return;
    }
    if (args[1]->type != AST_BLOCK) {
      compiler__error(c, line, col, "with-ctx body must be a block");
      return;
    }

    CtxFieldList *ctx_fl = compiler__get_ctx_fields(c);
    if (!ctx_fl || ctx_fl->count == 0) {
      compiler__error(c, line, col, "with-ctx: no ctx fields declared");
      return;
    }

    /* Validate override fields at compile time */
    AstNode *overrides_block = args[0];
    uint32_t override_count = overrides_block->data.block.count;
    for (uint32_t i = 0; i < override_count; i++) {
      AstNode *override_cmd = overrides_block->data.block.commands[i];
      if (override_cmd->type != AST_COMMAND || override_cmd->data.command.arg_count != 1) {
        compiler__error(c, line, col, "with-ctx override must be: field_name value");
        return;
      }
      AstNode *field_head = override_cmd->data.command.head;
      if (field_head->type != AST_LIT_STRING) {
        compiler__error(c, line, col, "with-ctx override field name must be an identifier");
        return;
      }
      const char *fname = field_head->data.lit_string.value;
      uint32_t flen = field_head->data.lit_string.length;
      if (!ctx_field_list__find(ctx_fl, fname, flen)) {
        char err[128];
        snprintf(err, sizeof(err), "no field '%.*s' on ctx", (int)flen, fname);
        compiler__error(c, line, col, err);
        return;
      }
    }

    /* Emit OP_CTX_FORK: alloc new ctx, copy data, push old ctx, swap vm->ctx */
    compiler__emit_byte(c, OP_CTX_FORK, line);

    /* Compile field overrides: each sets a field on the new (forked) ctx */
    for (uint32_t i = 0; i < override_count; i++) {
      AstNode *override_cmd = overrides_block->data.block.commands[i];
      AstNode *field_head = override_cmd->data.command.head;
      const char *fname = field_head->data.lit_string.value;
      uint32_t flen = field_head->data.lit_string.length;
      CtxField *cf = ctx_field_list__find(ctx_fl, fname, flen);
      /* Type check the override value */
      compiler__emit_byte(c, OP_GET_CTX, line);
      compiler__compile_node(c, override_cmd->data.command.args[0]);
      JaclType val_type = (JaclType)override_cmd->data.command.args[0]->inferred_type;
      if (cf->type != TYPE_DYN && val_type != TYPE_DYN && val_type != cf->type) {
        char err[192];
        jacl_format_field_mismatch(err, sizeof(err),
            "ctx", 3, cf->name, cf->name_len, cf->type, val_type);
        compiler__error(c, line, col, err);
        return;
      }
      if (cf->type != TYPE_DYN && val_type == TYPE_DYN) {
        char err[224];
        jacl_format_field_dyn_assign(err, sizeof(err),
            "ctx", 3, cf->name, cf->name_len, cf->type);
        compiler__error(c, line, col, err);
        return;
      }
      /* Ctx struct fields store inline bytes embedded in ctx.data —
         use the inline-aware SET op to write bytes directly. */
      if (cf->type == TYPE_STRUCT) {
        compiler__emit_byte(c, OP_HEAP_RECORD_SET_INLINE, line);
        compiler__emit_u16(c, (uint16_t)cf->offset, line);
        compiler__emit_u16(c, (uint16_t)cf->struct_type_idx, line);
      } else {
        compiler__emit_byte(c, OP_HEAP_RECORD_SET, line);
        compiler__emit_u16(c, (uint16_t)cf->offset, line);
        compiler__emit_byte(c, (uint8_t)cf->type, line);
      }
      compiler__emit_byte(c, OP_POP, line); /* discard struct result */
    }

    /* Compile body block as expression (result stays on stack).
       OP_CTX_FORK/RESTORE use a VM-internal save stack — no value stack
       involvement for the saved ctx, so body_result is naturally on top. */
    compiler__compile_block_expr(c, args[1]);

    /* Restore original ctx from VM save stack, free forked ctx to pool */
    compiler__emit_byte(c, OP_CTX_RESTORE, line);
    return;
  }

  /* vec constructor (variadic: 0+ args) */
  if (hid == HEAD_VEC) {
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      if (compiler__reject_bare_typed(c, args[i], line, col, "dyn vec")) return;
    }
    compiler__emit_byte(c, OP_VEC, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* vec-get builtin (exactly 2 args) */
  if (hid == HEAD_VEC_GET) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-get", "2 arguments", argc);
      return;
    }
    if (!compiler__compile_vec_receiver(c, args[0], "vec-get", line, col)) return;
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      compiler__compile_node(c, args[1]);
      compiler__emit_byte(c, OP_TYPED_VEC_GET_INLINE, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      if (COMPILER_IS_SCALAR_TYPE_IDX(elem_type_idx)) {
        /* Scalar element typed vec: result is a single value of that
         * scalar JaclType; not inline struct bytes. */
        c->last_expr_type = COMPILER_TYPE_IDX_TO_SCALAR(elem_type_idx);
      } else {
        c->inline_repr = INLINE_STACK;
        c->last_expr_type = TYPE_STRUCT;
      }
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_VEC_GET, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* vec-len builtin (exactly 1 arg) */
  if (hid == HEAD_VEC_LEN) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "vec-len", "1 argument", argc);
      return;
    }
    if (!compiler__compile_vec_receiver(c, args[0], "vec-len", line, col)) return;
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_VEC) {
      compiler__emit_byte(c, OP_TYPED_VEC_LEN, line);
      c->last_expr_type = TYPE_I32;
      return;
    }
    compiler__emit_byte(c, OP_VEC_LEN, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* --- [buf-get $b $i] and [buf-set $b $i $v] for [Buf N T] locals ---
   * Receiver must be a bare var-ref to a TYPE_BUF local; we read
   * base_slot / elem_type / buf_len from the binding and bake them
   * into the opcode operands. Bounds-check is in the opcode. See
   * BUFFER_DESIGN.md M2. */
  if (hid == HEAD_BUF_GET || hid == HEAD_BUF_SET) {
    bool is_get = (hid == HEAD_BUF_GET);
    uint32_t expected_argc = is_get ? 2 : 3;
    if (argc != expected_argc) {
      compiler__builtin_arity_error(c, line, col,
          is_get ? "buf-get" : "buf-set",
          is_get ? "2 arguments" : "3 arguments", argc);
      return;
    }
    AstNode* recv = args[0];
    if (recv->type != AST_VAR_REF) {
      compiler__error(c, line, col,
          is_get ? "buf-get requires a buf-typed variable reference"
                 : "buf-set requires a buf-typed variable reference");
      return;
    }
    JaclVal recv_name = compiler__name_val(c->heap, c->intern_table,
        recv->data.var_ref.name, recv->data.var_ref.length);
    int found = -1;
    for (int i = (int)c->local_count - 1; i >= 0; i--) {
      if (c->locals[i].name == recv_name) { found = i; break; }
    }
    if (found < 0 || c->locals[found].type != TYPE_BUF) {
      compiler__error(c, line, col,
          "buf-get/buf-set: receiver is not a [Buf N T] local");
      return;
    }
    uint32_t base_slot = (uint32_t)found;
    uint32_t buf_len   = c->locals[found].buf_len;
    if (!JACL_IS_SCALAR_TYPE_IDX(c->locals[found].struct_type_idx)) {
      compiler__error(c, line, col,
          "buf-get/buf-set: only scalar element types are supported (M2)");
      return;
    }
    JaclType elem_type = JACL_TYPE_IDX_TO_SCALAR(c->locals[found].struct_type_idx);

    /* Compile index (must be i32). */
    compiler__compile_node(c, args[1]);
    if (c->last_expr_type != TYPE_I32) {
      compiler__error(c, line, col,
          "buf-get/buf-set: index must be i32");
      return;
    }

    if (is_get) {
      compiler__emit_byte(c, OP_BUF_GET_LOCAL, line);
      compiler__emit_byte(c, (uint8_t)base_slot, line);
      compiler__emit_byte(c, (uint8_t)elem_type, line);
      compiler__emit_u16(c, (uint16_t)buf_len, line);
      /* Result type mirrors the typer rule: small ints widen to i32. */
      switch (elem_type) {
        case TYPE_I8: case TYPE_U8:
        case TYPE_I16: case TYPE_U16:
          c->last_expr_type = TYPE_I32; break;
        default:
          c->last_expr_type = elem_type; break;
      }
      return;
    }

    /* buf-set: compile value. Int literals default to TYPE_I32 from
     * the typer; the VM widens / narrows at the store site. */
    compiler__compile_node(c, args[2]);

    compiler__emit_byte(c, OP_BUF_SET_LOCAL, line);
    compiler__emit_byte(c, (uint8_t)base_slot, line);
    compiler__emit_byte(c, (uint8_t)elem_type, line);
    compiler__emit_u16(c, (uint16_t)buf_len, line);
    /* buf-set leaves nothing on TOS — but the statement-level model
     * expects a value. Push NIL so the block cleanup balances. */
    compiler__emit_byte(c, OP_NIL, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* [buf-len $b] — compile-time fold. N is statically known on the
   * buf local, so we emit an i32 constant and never evaluate the
   * receiver at runtime. See BUFFER_DESIGN.md. */
  if (hid == HEAD_BUF_LEN) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "buf-len", "1 argument", argc);
      return;
    }
    AstNode* recv = args[0];
    if (recv->type != AST_VAR_REF) {
      compiler__error(c, line, col,
          "buf-len requires a buf-typed variable reference");
      return;
    }
    /* Look up the local by name in the current compiler's local table. */
    JaclVal recv_name = compiler__name_val(c->heap, c->intern_table,
        recv->data.var_ref.name, recv->data.var_ref.length);
    int found = -1;
    for (int i = (int)c->local_count - 1; i >= 0; i--) {
      if (c->locals[i].name == recv_name) { found = i; break; }
    }
    if (found < 0 || c->locals[found].type != TYPE_BUF) {
      compiler__error(c, line, col,
          "buf-len: argument is not a [Buf N T] local");
      return;
    }
    uint32_t n = c->locals[found].buf_len;
    compiler__emit_constant(c, jacl_i32((int32_t)n), line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* vec-push builtin (exactly 2 args) */
  if (hid == HEAD_VEC_PUSH) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-push", "2 arguments", argc);
      return;
    }
    if (!compiler__compile_vec_receiver(c, args[0], "vec-push", line, col)) return;
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      if (!compiler__compile_typed_elem_arg(c, args[1], elem_type_idx)) {
        compiler__error(c, line, col, "vec-push: element type does not match typed vec element type");
        return;
      }
      compiler__emit_byte(c, OP_TYPED_VEC_PUSH, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      return;
    }
    compiler__compile_node(c, args[1]);
    if (compiler__reject_bare_typed(c, args[1], line, col, "dyn vec")) return;
    compiler__emit_byte(c, OP_VEC_PUSH, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* vec-set builtin (exactly 3 args) */
  if (hid == HEAD_VEC_SET) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "vec-set", "3 arguments", argc);
      return;
    }
    if (!compiler__compile_vec_receiver(c, args[0], "vec-set", line, col)) return;
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      compiler__compile_node(c, args[1]); /* index */
      if (!compiler__compile_typed_elem_arg(c, args[2], elem_type_idx)) {
        compiler__error(c, line, col, "vec-set: element type does not match typed vec element type");
        return;
      }
      compiler__emit_byte(c, OP_TYPED_VEC_SET, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    if (compiler__reject_bare_typed(c, args[2], line, col, "dyn vec")) return;
    compiler__emit_byte(c, OP_VEC_SET, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* vec-concat builtin (exactly 2 args) */
  if (hid == HEAD_VEC_CONCAT) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-concat", "2 arguments", argc);
      return;
    }
    if (!compiler__compile_vec_receiver(c, args[0], "vec-concat", line, col)) return;
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      compiler__compile_node(c, args[1]);
      if ((JaclType)args[1]->inferred_type != TYPE_TYPED_VEC ||
          args[1]->inferred_struct_idx != elem_type_idx) {
        compiler__error(c, line, col, "vec-concat: both vectors must have the same typed element type");
        return;
      }
      compiler__emit_byte(c, OP_TYPED_VEC_CONCAT, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_VEC_CONCAT, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* vec-slice builtin (exactly 3 args) */
  if (hid == HEAD_VEC_SLICE) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "vec-slice", "3 arguments", argc);
      return;
    }
    if (!compiler__compile_vec_receiver(c, args[0], "vec-slice", line, col)) return;
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      compiler__compile_node(c, args[1]);
      compiler__compile_node(c, args[2]);
      compiler__emit_byte(c, OP_TYPED_VEC_SLICE, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    compiler__emit_byte(c, OP_VEC_SLICE, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* map constructor (0 or any even number of args) */
  if (hid == HEAD_MAP) {
    if (argc % 2 != 0) {
      compiler__builtin_arity_error(c, line, col, "map",
                                     "an even number of arguments", argc);
      return;
    }
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      if (compiler__reject_bare_typed(c, args[i], line, col, "dyn map")) return;
    }
    compiler__emit_byte(c, OP_MAP, line);
    compiler__emit_byte(c, (uint8_t)(argc / 2), line);
    c->last_expr_type = TYPE_MAP;
    return;
  }

  /* map-get builtin (exactly 2 args) */
  if (hid == HEAD_MAP_GET) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-get", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      uint32_t key_type_idx = args[0]->inferred_key_struct_idx;
      if (key_type_idx != UINT32_MAX) {
        if (!compiler__compile_typed_elem_arg(c, args[1], key_type_idx)) {
          compiler__error(c, line, col, "map-get: key type does not match typed map key type");
          return;
        }
      } else {
        compiler__compile_node(c, args[1]);
      }
      compiler__emit_byte(c, OP_TYPED_MAP_GET_INLINE, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      compiler__emit_u16(c, (uint16_t)key_type_idx, line);
      if (COMPILER_IS_SCALAR_TYPE_IDX(elem_type_idx)) {
        c->last_expr_type = COMPILER_TYPE_IDX_TO_SCALAR(elem_type_idx);
      } else {
        c->inline_repr = INLINE_STACK;
        c->last_expr_type = TYPE_STRUCT;
      }
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_GET, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* map-has builtin (exactly 2 args) */
  if (hid == HEAD_MAP_HAS) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-has", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_MAP) {
      uint32_t key_type_idx = args[0]->inferred_key_struct_idx;
      if (key_type_idx != UINT32_MAX) {
        if (!compiler__compile_typed_elem_arg(c, args[1], key_type_idx)) {
          compiler__error(c, line, col, "map-has: key type does not match typed map key type");
          return;
        }
      } else {
        compiler__compile_node(c, args[1]);
      }
      compiler__emit_byte(c, OP_TYPED_MAP_HAS, line);
      compiler__emit_u16(c, (uint16_t)key_type_idx, line);
      c->last_expr_type = TYPE_BOOL;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_HAS, line);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* map-len builtin (exactly 1 arg) */
  if (hid == HEAD_MAP_LEN) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-len", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_MAP) {
      compiler__emit_byte(c, OP_TYPED_MAP_LEN, line);
      c->last_expr_type = TYPE_I32;
      return;
    }
    compiler__emit_byte(c, OP_MAP_LEN, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* map-set builtin (exactly 3 args) */
  if (hid == HEAD_MAP_SET) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "map-set", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      uint32_t key_type_idx = args[0]->inferred_key_struct_idx;
      if (key_type_idx != UINT32_MAX) {
        if (!compiler__compile_typed_elem_arg(c, args[1], key_type_idx)) {
          compiler__error(c, line, col, "map-set: key type does not match typed map key type");
          return;
        }
      } else {
        compiler__compile_node(c, args[1]);
      }
      if (!compiler__compile_typed_elem_arg(c, args[2], elem_type_idx)) {
        compiler__error(c, line, col, "map-set: value type does not match typed map element type");
        return;
      }
      compiler__emit_byte(c, OP_TYPED_MAP_SET, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      compiler__emit_u16(c, (uint16_t)key_type_idx, line);
      c->last_expr_type = TYPE_TYPED_MAP;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    if (compiler__reject_bare_typed(c, args[2], line, col, "dyn map")) return;
    compiler__emit_byte(c, OP_MAP_SET, line);
    c->last_expr_type = TYPE_MAP;
    return;
  }

  /* map-remove builtin (exactly 2 args) */
  if (hid == HEAD_MAP_REMOVE) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-remove", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      uint32_t key_type_idx = args[0]->inferred_key_struct_idx;
      if (key_type_idx != UINT32_MAX) {
        if (!compiler__compile_typed_elem_arg(c, args[1], key_type_idx)) {
          compiler__error(c, line, col, "map-remove: key type does not match typed map key type");
          return;
        }
      } else {
        compiler__compile_node(c, args[1]);
      }
      compiler__emit_byte(c, OP_TYPED_MAP_REMOVE, line);
      compiler__emit_u16(c, (uint16_t)key_type_idx, line);
      c->last_expr_type = TYPE_TYPED_MAP;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_REMOVE, line);
    c->last_expr_type = TYPE_MAP;
    return;
  }

  /* map-keys builtin (exactly 1 arg) */
  if (hid == HEAD_MAP_KEYS) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-keys", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_MAP) {
      uint32_t key_type_idx = args[0]->inferred_key_struct_idx;
      compiler__emit_byte(c, OP_TYPED_MAP_KEYS, line);
      compiler__emit_u16(c, (uint16_t)key_type_idx, line);
      if (key_type_idx != UINT32_MAX) {
        c->last_expr_type = TYPE_TYPED_VEC;  /* struct keys → typed vec */
      } else {
        c->last_expr_type = TYPE_VEC;  /* dyn keys → dyn vec */
      }
      return;
    }
    compiler__emit_byte(c, OP_MAP_KEYS, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* map-vals builtin (exactly 1 arg) */
  if (hid == HEAD_MAP_VALS) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-vals", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = args[0]->inferred_struct_idx;
      compiler__emit_byte(c, OP_TYPED_MAP_VALS, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;  /* returns typed vec of values */
      return;
    }
    compiler__emit_byte(c, OP_MAP_VALS, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* transform builtin (exactly 2 args — non-suspending callback) */
  if (hid == HEAD_TRANSFORM) {
    compiler__compile_hof_builtin(c, "transform", args, argc, OP_TRANSFORM, line, col);
    return;
  }


  /* filter builtin (exactly 2 args — non-suspending callback) */
  if (hid == HEAD_FILTER) {
    compiler__compile_hof_builtin(c, "filter", args, argc, OP_FILTER, line, col);
    return;
  }

  /* stack-trace builtin (exactly 0 args) */
  if (hid == HEAD_STACK_TRACE) {
    if (argc != 0) {
      compiler__builtin_arity_error(c, line, col, "stack-trace", "0 arguments", argc);
      return;
    }
    compiler__emit_byte(c, OP_STACK_TRACE, line);
    return;
  }

  /* to-string builtin (exactly 1 arg) */
  if (hid == HEAD_TO_STRING) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "to-string", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_TO_STRING, line);
    c->last_expr_type = TYPE_STR;
    return;
  }

  /* US-013: hash builtin — hash any value, optimized for inline structs */
  if (hid == HEAD_HASH) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "hash", "1 argument", argc);
      return;
    }
    /* Check for inline struct local — hash directly from stack slots */
    if (args[0]->type == AST_VAR_REF) {
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table,
          args[0]->data.var_ref.name, args[0]->data.var_ref.length);
      int slot = compiler__resolve_local(c, name_val);
      if (slot != -1 && c->locals[slot].type == TYPE_STRUCT &&
          c->locals[slot].is_inline) {
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        uint32_t sidx = c->locals[slot].struct_type_idx;
        if (reg && sidx < reg->count && reg->defs[sidx]) {
          uint16_t total_size = (uint16_t)reg->defs[sidx]->total_size;
          compiler__emit_byte(c, OP_STRUCT_HASH_INLINE, line);
          compiler__emit_byte(c, (uint8_t)slot, line);
          compiler__emit_u16(c, total_size, line);
          compiler__emit_u16(c, (uint16_t)sidx, line);
          c->last_expr_type = TYPE_DYN;
          return;
        }
      }
    }
    /* Generic path: compile arg, push hash */
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_HASH, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  if (hid == HEAD_INTERPRET) {
    if (argc != 1 && argc != 2) {
      compiler__builtin_arity_error(c, line, col, "interpret", "1 or 2 arguments", argc);
      return;
    }
    if (argc == 2) {
      /* 2-arg form: [interpret $prelude $src] */
      compiler__compile_node(c, args[0]);  /* prelude map */
      compiler__ensure_boxed(c, line);
      compiler__compile_node(c, args[1]);  /* source string */
      compiler__ensure_boxed(c, line);
    } else {
      /* 1-arg form: [interpret $src] */
      compiler__compile_node(c, args[0]);
      compiler__ensure_boxed(c, line);
    }
    compiler__emit_byte(c, OP_INTERPRET, line);
    compiler__emit_byte(c, (uint8_t)argc, line);  /* arity byte */
    return;
  }

  if (hid == HEAD_INTERPRET_PRELUDE) {
    if (argc != 0) {
      compiler__builtin_arity_error(c, line, col, "interpret-prelude", "0 arguments", argc);
      return;
    }
    compiler__emit_byte(c, OP_INTERPRET_PRELUDE, line);
    return;
  }

  /* US-015: syntax object introspection builtins. Each compiles the single
   * argument to a syntax object value, then emits OP_SYNTAX_OP with a subop
   * byte indicating which introspection operation to perform.
   *
   * NOTE: PRD calls one of these syntax->string but the lexer tokenizes ->
   * as an arrow separator, so 'syntax-str' is used instead (consistent with
   * to-string and byte-length). */
  {
    static const struct { HeadId hid; const char* name; uint8_t subop;
                          JaclType result; } syntax_builtins[] = {
      { HEAD_SYNTAX_KIND,     "syntax-kind",     0, TYPE_STR },
      { HEAD_SYNTAX_DATUM,    "syntax-datum",    1, TYPE_DYN },
      { HEAD_SYNTAX_HEAD,     "syntax-head",     2, TYPE_DYN },
      { HEAD_SYNTAX_ARGS,     "syntax-args",     3, TYPE_VEC },
      { HEAD_SYNTAX_COMMANDS, "syntax-commands", 4, TYPE_VEC },
      { HEAD_SYNTAX_POS,      "syntax-pos",      5, TYPE_MAP },
      { HEAD_SYNTAX_STR,      "syntax-str",      6, TYPE_STR },
    };
    for (size_t si = 0; si < sizeof(syntax_builtins)/sizeof(syntax_builtins[0]); si++) {
      if (hid != syntax_builtins[si].hid) continue;
      if (argc != 1) {
        compiler__builtin_arity_error(c, line, col,
            syntax_builtins[si].name, "1 argument", argc);
        return;
      }
      compiler__compile_node(c, args[0]);
      compiler__emit_byte(c, OP_SYNTAX_OP, line);
      compiler__emit_byte(c, syntax_builtins[si].subop, line);
      if (syntax_builtins[si].result != TYPE_DYN)
        c->last_expr_type = syntax_builtins[si].result;
      return;
    }
  }

  /* US-016: make-syntax — programmatic construction of syntax objects.
   * First arg is a bare word naming the kind; remaining args are the
   * payload. Kind dispatch happens at compile time, so the opcode only
   * needs the kind-specific subop. Subops 7..12 share OP_SYNTAX_OP. */
  if (hid == HEAD_MAKE_SYNTAX) {
    if (argc < 1) {
      compiler__builtin_arity_error(c, line, col, "make-syntax",
                                    "at least 1 argument (kind)", argc);
      return;
    }
    AstNode* kind_node = args[0];
    if (kind_node->type != AST_LIT_STRING) {
      compiler__error(c, line, col,
        "make-syntax: first argument must be a kind name "
        "(one of: lit-int, lit-float, lit-string, var-ref, command, block)");
      return;
    }
    const char* kn = kind_node->data.lit_string.value;
    uint32_t    kl = kind_node->data.lit_string.length;

    uint8_t subop   = 0xFF;
    const char* nm  = NULL;
    uint32_t expect_args = 0;       /* excluding the kind arg */
    const char* expect_desc = NULL;

    if (kl == 7 && memcmp(kn, "lit-int", 7) == 0) {
      subop = 7; nm = "make-syntax lit-int"; expect_args = 1; expect_desc = "kind and i32 value";
    } else if (kl == 9 && memcmp(kn, "lit-float", 9) == 0) {
      subop = 8; nm = "make-syntax lit-float"; expect_args = 1; expect_desc = "kind and f32 value";
    } else if (kl == 10 && memcmp(kn, "lit-string", 10) == 0) {
      subop = 9; nm = "make-syntax lit-string"; expect_args = 1; expect_desc = "kind and string value";
    } else if (kl == 7 && memcmp(kn, "var-ref", 7) == 0) {
      subop = 10; nm = "make-syntax var-ref"; expect_args = 1; expect_desc = "kind and string name";
    } else if (kl == 7 && memcmp(kn, "command", 7) == 0) {
      subop = 11; nm = "make-syntax command"; expect_args = 2; expect_desc = "kind, head, args-vec";
    } else if (kl == 5 && memcmp(kn, "block", 5) == 0) {
      subop = 12; nm = "make-syntax block"; expect_args = 1; expect_desc = "kind and commands-vec";
    } else if (kl == 16 && memcmp(kn, "lit-string-caret", 16) == 0) {
      subop = 19; nm = "make-syntax lit-string-caret"; expect_args = 1; expect_desc = "kind and string value";
    } else {
      char err[128];
      snprintf(err, sizeof(err),
               "make-syntax: unknown kind '%.*s' (expected one of: "
               "lit-int, lit-float, lit-string, var-ref, command, block, lit-string-caret)",
               (int)kl, kn);
      compiler__error(c, line, col, err);
      return;
    }

    uint32_t got_extra = argc - 1;
    if (got_extra != expect_args) {
      compiler__builtin_arity_error(c, line, col, nm, expect_desc, argc);
      return;
    }

    /* Compile the payload args (skip the kind) in left-to-right order. */
    for (uint32_t i = 1; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      /* lit-int and lit-float keep unboxed types — the VM subop reads
       * them as i32/f32 directly. For the other kinds (string/vec/syntax),
       * the default boxing is already in place. */
    }
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, subop, line);
    /* make-syntax returns an opaque syntax object — typed as dyn since
     * the typer doesn't model syntax kinds. Pin instead of leaking the
     * last payload's type. */
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* US-017: syntax-error — signal a custom error with an optional syntax
   * object for source-location reporting. Two forms:
   *   syntax-error $message                 → subop 13 (message only)
   *   syntax-error $message $syntax-obj     → subop 14 (message + pos)
   * Implementation is a runtime error — macros in JACL are template-based,
   * so "compile-time" in the PRD sense means "at macro-expanded-code
   * execution time", which is the program's normal runtime. */
  if (hid == HEAD_SYNTAX_ERROR) {
    if (argc != 1 && argc != 2) {
      compiler__builtin_arity_error(c, line, col, "syntax-error",
                                    "1 or 2 arguments (message, optional syntax)", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (argc == 2) {
      compiler__compile_node(c, args[1]);
      compiler__emit_byte(c, OP_SYNTAX_OP, line);
      compiler__emit_byte(c, 14 /* SYNTAX_OP_ERROR_POS */, line);
    } else {
      compiler__emit_byte(c, OP_SYNTAX_OP, line);
      compiler__emit_byte(c, 13 /* SYNTAX_OP_ERROR */, line);
    }
    return;
  }

  /* box builtin (exactly 1 arg) */
  if (hid == HEAD_BOX) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "box", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    uint32_t box_arg_struct_idx = args[0]->inferred_struct_idx;
    if ((JaclType)args[0]->inferred_type == TYPE_STRUCT &&
        box_arg_struct_idx != UINT32_MAX) {
      /* Box accepts inline struct bytes directly — no reify. */
      compiler__emit_byte(c, OP_BOX_STRUCT, line);
      compiler__emit_u16(c, (uint16_t)box_arg_struct_idx, line);
      c->inline_repr = INLINE_NONE;
    } else if (compiler__expr_is_error_free(args[0])) {
      compiler__emit_byte(c, OP_BOX_UNCHECKED, line);
    } else {
      compiler__emit_byte(c, OP_BOX, line);
    }
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* atom builtin (exactly 1 arg) */
  if (hid == HEAD_ATOM) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "atom", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if ((JaclType)args[0]->inferred_type == TYPE_STRUCT) {
      compiler__error(c, line, col, "atom: struct values cannot be stored in atoms; use [box] instead");
      return;
    }
    compiler__emit_byte(c, OP_ATOM, line);
    /* atom returns an opaque atom value; element type isn't tracked. */
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* box? builtin (1 or 2 args) */
  if (hid == HEAD_BOX_Q) {
    if (argc != 1 && argc != 2) {
      compiler__builtin_arity_error(c, line, col, "box?", "1 or 2 arguments", argc);
      return;
    }
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
      compiler__emit_byte(c, OP_IS_BOX, line);
    } else {
      /* [box? [Vec Type] $val] or [box? [Map Type] $val] — typed collection box check,
         or [box? Type $val] — typed struct box check */
      AstNode* box_elem = NULL;
      AstNode* box_key_elem = NULL;
      int box_kind = compiler__typed_collection_expr(args[0], &box_elem, &box_key_elem);
      if (box_kind > 0) {
        uint32_t elem_idx = struct_registry__find(compiler__get_struct_registry(c),
            box_elem->data.lit_string.value, box_elem->data.lit_string.length);
        if (elem_idx == UINT32_MAX) {
          compiler__error(c, line, col, "box?: unknown element type name");
          return;
        }
        compiler__compile_node(c, args[1]);
        compiler__emit_byte(c, (box_kind == 1) ? OP_IS_BOX_TYPED_VEC : OP_IS_BOX_TYPED_MAP, line);
      } else if (args[0]->type == AST_LIT_STRING) {
        /* [box? Type $val] — typed struct box check */
        const char* tname = args[0]->data.lit_string.value;
        uint32_t tlen = args[0]->data.lit_string.length;
        uint32_t type_idx;
        if (tlen == 3 && memcmp(tname, "dyn", 3) == 0) {
          type_idx = 0;
        } else {
          type_idx = struct_registry__find(compiler__get_struct_registry(c), tname, tlen);
          if (type_idx == UINT32_MAX) {
            compiler__error(c, line, col, "box?: unknown type name");
            return;
          }
        }
        compiler__compile_node(c, args[1]);
        compiler__emit_byte(c, OP_IS_BOX_TYPED, line);
        compiler__emit_u16(c, (uint16_t)type_idx, line);
      } else {
        compiler__error(c, line, col, "box?: first argument must be a type name or [Vec/Map Type]");
        return;
      }
    }
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* atom? builtin (exactly 1 arg) */
  /* unbox builtin (exactly 1 arg) — requires flow-typed narrowing from box? guard */
  if (hid == HEAD_UNBOX) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "unbox", "1 argument", argc);
      return;
    }
    /* Check if argument is a narrowed variable */
    if (args[0]->type == AST_VAR_REF) {
      uint32_t vlen = args[0]->data.var_ref.length;
      if (vlen <= 128) {
        JaclVal vname = compiler__name_val(c->heap, c->intern_table,
                                            args[0]->data.var_ref.name, vlen);
        int slot = compiler__resolve_local(c, vname);
        if (slot >= 0) {
          for (uint32_t ni = 0; ni < c->narrowing_count; ni++) {
            if (c->narrowings[ni].local_slot == (uint16_t)slot) {
              /* Found narrowing — compile as deref with known type */
              compiler__compile_node(c, args[0]);
              JaclType bt = c->narrowings[ni].box_type;
              uint32_t tidx = c->narrowings[ni].box_type_idx;
              if (bt == TYPE_TYPED_VEC || bt == TYPE_TYPED_MAP) {
                compiler__emit_byte(c, OP_DEREF, line);
                c->last_expr_type = bt;
              } else if (tidx > 0) {
                /* Phase 5d: deref struct box directly to inline bytes */
                StructTypeRegistry* reg = compiler__get_struct_registry(c);
                StructTypeDef* sdef = reg && tidx < reg->count ? reg->defs[tidx] : NULL;
                if (struct_def_is_user(sdef, reg)) {
                  compiler__emit_byte(c, OP_DEREF_INLINE, line);
                  compiler__emit_u16(c, (uint16_t)tidx, line);
                  c->last_expr_type = TYPE_STRUCT;
                  c->inline_repr = INLINE_STACK;
                } else {
                  compiler__emit_byte(c, OP_DEREF, line);
                  c->last_expr_type = TYPE_STRUCT;
                }
              } else {
                compiler__emit_byte(c, OP_DEREF, line);
                c->last_expr_type = TYPE_DYN;
              }
              return;
            }
          }
        }
      }
    }
    compiler__error(c, line, col,
        "unbox: variable must be inside a box?-guarded branch");
    return;
  }

  /* reset builtin (exactly 2 args) */
  if (hid == HEAD_RESET) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "reset", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    uint32_t reset_val_struct_idx = args[1]->inferred_struct_idx;
    if ((JaclType)args[1]->inferred_type == TYPE_STRUCT &&
        reset_val_struct_idx != UINT32_MAX) {
      /* Struct-box reset: inline bytes write directly to box->data. */
      compiler__emit_byte(c, OP_RESET_INLINE, line);
      compiler__emit_u16(c, (uint16_t)reset_val_struct_idx, line);
      c->inline_repr = INLINE_STACK;  /* new struct bytes left on TOS */
    } else {
      compiler__emit_byte(c, OP_RESET, line);
      c->last_expr_type = TYPE_NIL;
    }
    return;
  }

  /* swap builtin (exactly 2 args) */
  if (hid == HEAD_SWAP) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "swap", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_SWAP, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* watch builtin — [watch ATOM KEY FN] → nil. Registers FN (a 2-arg
   * closure) under KEY on ATOM; re-registering replaces. */
  if (hid == HEAD_WATCH) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "watch", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__compile_node(c, args[1]);
    compiler__ensure_boxed(c, line);
    compiler__compile_node(c, args[2]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_WATCH, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* unwatch builtin — [unwatch ATOM KEY] → nil. No-op if no such key. */
  if (hid == HEAD_UNWATCH) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "unwatch", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__compile_node(c, args[1]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_UNWATCH, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* to builtin — explicit type conversion: [to TYPE expr] */
  if (hid == HEAD_TO) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "to", "2 arguments", argc);
      return;
    }
    /* First arg must be a bare word matching a type keyword */
    AstNode* type_node = args[0];
    if (type_node->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "to: first argument must be a type keyword (i32, i64, u32, u64, f32, f64, dyn)");
      return;
    }
    const char* type_word = type_node->data.lit_string.value;
    uint32_t type_len = type_node->data.lit_string.length;
    if (!is_type_keyword(type_word, type_len)) {
      char err[128];
      snprintf(err, sizeof(err), "to: unknown type '%.*s'", (int)type_len, type_word);
      compiler__error(c, line, col, err);
      return;
    }
    JaclType target_type = type_from_keyword(type_word, type_len);

    /* Compile the expression */
    compiler__compile_node(c, args[1]);
    JaclType src_type = (JaclType)args[1]->inferred_type;

    /* Validate conversion at compile time */
    if (src_type != TYPE_DYN && target_type != TYPE_DYN) {
      /* Both concrete types — check if conversion is valid */
      bool src_ok = is_numeric_type(src_type);
      bool tgt_ok = is_numeric_type(target_type);
      if (!src_ok || !tgt_ok) {
        /* Allow same-type no-op (e.g. to str $s where s is str) */
        if (src_type != target_type) {
          char err[128];
          snprintf(err, sizeof(err), "type error: cannot convert %s to %s",
                   type_name(src_type), type_name(target_type));
          compiler__error(c, line, col, err);
          return;
        }
      }
    }
    /* Check dyn→non-numeric target (dyn can only convert to numeric or dyn at compile time) */
    if (src_type != TYPE_DYN && target_type != TYPE_DYN &&
        !is_numeric_type(src_type) && src_type != target_type) {
      char err[128];
      snprintf(err, sizeof(err), "type error: cannot convert %s to %s",
               type_name(src_type), type_name(target_type));
      compiler__error(c, line, col, err);
      return;
    }

    /* Same type → no-op */
    if (src_type == target_type) {
      c->last_expr_type = target_type;
      return;
    }

    /* dyn→dyn → no-op */
    if (src_type == TYPE_DYN && target_type == TYPE_DYN) {
      return;
    }

    /* Emit appropriate conversion opcode */
    uint8_t opcode;
    switch (target_type) {
      case TYPE_I32: opcode = OP_TO_I32; break;
      case TYPE_I64: opcode = OP_TO_I64; break;
      case TYPE_U32: opcode = OP_TO_U32; break;
      case TYPE_U64: opcode = OP_TO_U64; break;
      case TYPE_F32: opcode = OP_TO_F32; break;
      case TYPE_F64: opcode = OP_TO_F64; break;
      case TYPE_DYN: opcode = OP_TO_DYN; break;
      default: {
        compiler__error(c, line, col, "to: unsupported target type");
        return;
      }
    }
    compiler__emit_byte(c, opcode, line);
    compiler__emit_byte(c, (uint8_t)src_type, line);
    c->last_expr_type = target_type;
    return;
  }

  /* ptr-null — [ptr-null [Ptr T]]: typed null pointer literal. At
   * runtime null is u64(0); the typer narrows the result to TYPE_PTR
   * with the supplied pointee idx. Pure compile-time op aside from
   * the constant push. */
  if (hid == HEAD_PTR_NULL) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "ptr-null", "1 argument", argc);
      return;
    }
    AstNode* type_node = args[0];
    AstNode* ptr_pointee = NULL;
    if (!compiler__ptr_type_expr(type_node, &ptr_pointee)) {
      char err[128];
      jacl_format_ptr_null_bad_arg(err, sizeof(err));
      char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(err) + 1));
      memcpy(msg, err, strlen(err) + 1);
      compiler__error(c, line, col, msg);
      return;
    }
    /* Mirror the AST_LIT_INT TYPE_U64 path: store raw u64(0) in the
     * constant pool and emit OP_CONST_U64 so ensure_boxed handles
     * boxing correctly via OP_TO_DYN U64. Pre-boxing at compile time
     * would let the tagged value flow through OP_TO_DYN and double-box. */
    uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)(uint64_t)0);
    compiler__emit_byte(c, OP_CONST_U64, line);
    compiler__emit_u16(c, idx, line);
    c->last_expr_type = TYPE_U64;
    return;
  }

  /* ptr-cast — [ptr-cast [Ptr T] $u64_value]: re-tags a u64 address as
   * a typed pointer. Pure compile-time op — at runtime a pointer is the
   * same bits as a u64. The typer marks this node as TYPE_PTR with the
   * pointee idx; the compiler just compiles the value expression. */
  if (hid == HEAD_PTR_CAST) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "ptr-cast", "2 arguments", argc);
      return;
    }
    AstNode* type_node = args[0];
    AstNode* ptr_pointee = NULL;
    if (!compiler__ptr_type_expr(type_node, &ptr_pointee)) {
      char err[128];
      jacl_format_ptr_cast_bad_first_arg(err, sizeof(err));
      char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(err) + 1));
      memcpy(msg, err, strlen(err) + 1);
      compiler__error(c, line, col, msg);
      return;
    }
    compiler__compile_node(c, args[1]);
    return;
  }

  /* ptr-addr — [ptr-addr $p]: typed pointer → raw u64. Same runtime bits;
   * the typer narrows the result type to TYPE_U64. */
  if (hid == HEAD_PTR_ADDR) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "ptr-addr", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    return;
  }

  /* ptr-offset — [ptr-offset $p $n]: typed pointer arithmetic. Looks
   * up the pointee's element size at compile time (struct registry
   * for struct pointees, scalar size for scalar pointees) and bakes
   * it into the OP_PTR_OFFSET operand. The result preserves the
   * pointer's pointee — same [Ptr T] returned. */
  if (hid == HEAD_PTR_OFFSET) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "ptr-offset", "2 arguments", argc);
      return;
    }
    JaclType recv_t = (JaclType)args[0]->inferred_type;
    uint32_t recv_sidx = args[0]->inferred_struct_idx;
    if (recv_t != TYPE_PTR) {
      compiler__error(c, line, col,
                      "ptr-offset: first argument must be a typed pointer ([Ptr T])");
      return;
    }
    if (recv_sidx == UINT32_MAX) {
      compiler__error(c, line, col,
                      "ptr-offset: pointer's pointee type is unknown");
      return;
    }
    uint32_t elem_size;
    if (JACL_IS_SCALAR_TYPE_IDX(recv_sidx)) {
      JaclType pointee = JACL_TYPE_IDX_TO_SCALAR(recv_sidx);
      elem_size = struct__type_size(pointee, NULL, 0);
    } else {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      if (!reg || recv_sidx >= reg->count || !reg->defs[recv_sidx]) {
        compiler__error(c, line, col,
                        "ptr-offset: unknown struct pointee type");
        return;
      }
      elem_size = reg->defs[recv_sidx]->total_size;
    }
    if (elem_size == 0 || elem_size > 0xFFFF) {
      compiler__error(c, line, col,
                      "ptr-offset: pointee size out of range");
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_PTR_OFFSET, line);
    compiler__emit_u16(c, (uint16_t)elem_size, line);
    return;
  }

  /* ptr-diff — [ptr-diff $a $b]: typed pointer subtraction. Same
   * pointee required (typer enforces). Result is i64 element count. */
  if (hid == HEAD_PTR_DIFF) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "ptr-diff", "2 arguments", argc);
      return;
    }
    JaclType lhs_t = (JaclType)args[0]->inferred_type;
    uint32_t lhs_sidx = args[0]->inferred_struct_idx;
    if (lhs_t != TYPE_PTR || lhs_sidx == UINT32_MAX) {
      compiler__error(c, line, col,
                      "ptr-diff: arguments must be typed pointers ([Ptr T])");
      return;
    }
    uint32_t elem_size;
    if (JACL_IS_SCALAR_TYPE_IDX(lhs_sidx)) {
      elem_size = struct__type_size(JACL_TYPE_IDX_TO_SCALAR(lhs_sidx), NULL, 0);
    } else {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      if (!reg || lhs_sidx >= reg->count || !reg->defs[lhs_sidx]) {
        compiler__error(c, line, col, "ptr-diff: unknown struct pointee type");
        return;
      }
      elem_size = reg->defs[lhs_sidx]->total_size;
    }
    if (elem_size == 0 || elem_size > 0xFFFF) {
      compiler__error(c, line, col, "ptr-diff: pointee size out of range");
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_PTR_DIFF, line);
    compiler__emit_u16(c, (uint16_t)elem_size, line);
    return;
  }

  /* addr — [addr $p->field->...]: take the address of a chain leaf
   * instead of loading its value. Walks the chain accumulator over
   * args[0]; emits the base pointer + OP_PTR_ADD_OFFSET with the
   * cumulative offset. Result type is [Ptr T] where T is the
   * accessed field's type. */
  if (hid == HEAD_ADDR) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "addr", "1 argument", argc);
      return;
    }
    AstNode* expr = args[0];

    /* [addr $buf->N]: push the address of buf element N as a tagged
     * u64 (TYPE_PTR with the element's scalar type). Bounds-checked
     * at compile time. See BUFFER_DESIGN.md M3. */
    if (expr->type == AST_COMMAND &&
        expr->data.command.head_id == HEAD_DOT &&
        expr->data.command.arg_count == 2 &&
        expr->data.command.args[0]->type == AST_VAR_REF &&
        expr->data.command.args[1]->type == AST_LIT_INT) {
      AstNode* recv = expr->data.command.args[0];
      JaclVal recv_name = compiler__name_val(c->heap, c->intern_table,
          recv->data.var_ref.name, recv->data.var_ref.length);
      int found = -1;
      for (int i = (int)c->local_count - 1; i >= 0; i--) {
        if (c->locals[i].name == recv_name) { found = i; break; }
      }
      if (found >= 0 && c->locals[found].type == TYPE_BUF &&
          JACL_IS_SCALAR_TYPE_IDX(c->locals[found].struct_type_idx)) {
        uint32_t base_slot = (uint32_t)found;
        uint32_t buf_len   = c->locals[found].buf_len;
        JaclType elem_type =
            JACL_TYPE_IDX_TO_SCALAR(c->locals[found].struct_type_idx);
        int32_t  idx_lit = expr->data.command.args[1]->data.lit_int.value;
        if (idx_lit < 0 || (uint32_t)idx_lit >= buf_len) {
          char err[160];
          snprintf(err, sizeof(err),
              "addr: buf index %d out of bounds for [Buf %u %s]",
              (int)idx_lit, (unsigned)buf_len, type_name(elem_type));
          compiler__error(c, line, col, err);
          return;
        }
        uint32_t elem_sz = struct__type_size(elem_type, NULL, 0);
        uint64_t byte_offset = (uint64_t)idx_lit * (uint64_t)elem_sz;
        if (byte_offset > 0xFFFFu) {
          compiler__error(c, line, col, "addr: buf byte offset exceeds 65535");
          return;
        }
        compiler__emit_byte(c, OP_BUF_ADDR_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)base_slot, line);
        compiler__emit_u16(c, (uint16_t)byte_offset, line);
        c->last_expr_type = TYPE_U64; /* runtime rep of pointer */
        return;
      }
    }

    if (expr->type != AST_COMMAND ||
        expr->data.command.head_id != HEAD_DOT ||
        expr->data.command.arg_count != 2 ||
        expr->data.command.args[1]->type != AST_LIT_STRING) {
      compiler__error(c, line, col,
                      "addr: argument must be a field-access chain "
                      "(e.g. $p->field or $p->inner->x)");
      return;
    }
    AstNode* base_node;
    uint32_t accumulated_offset;
    JaclType term_t;
    uint32_t term_sidx;
    AstNode* recv  = expr->data.command.args[0];
    AstNode* fld   = expr->data.command.args[1];
    if (!compiler__resolve_ptr_chain_step(c, recv,
                                          fld->data.lit_string.value,
                                          fld->data.lit_string.length,
                                          &base_node, &accumulated_offset,
                                          &term_t, &term_sidx)) {
      compiler__error(c, line, col,
                      "addr: chain does not bottom out in a [Ptr T] base");
      return;
    }
    (void)term_t; (void)term_sidx;  /* type info is for the typer, not codegen */
    compiler__compile_node(c, base_node);
    if (accumulated_offset != 0) {
      compiler__emit_byte(c, OP_PTR_ADD_OFFSET, line);
      compiler__emit_u16(c, (uint16_t)accumulated_offset, line);
    }
    /* Result type / pointee idx live on the AST node (typer-set). The
     * runtime value is already a u64-tagged pointer, ready for any
     * subsequent [Ptr T] use. */
    return;
  }

  /* ptr-deref — [ptr-deref $p]: load the value at *p. For [Ptr T]
   * with a scalar pointee, emits OP_PTR_LOAD at offset 0 with the
   * pointee's type. Struct pointees use $p->field for field access
   * (Stage 5b doesn't support whole-struct loads through pointers). */
  if (hid == HEAD_PTR_DEREF) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "ptr-deref", "1 argument", argc);
      return;
    }
    JaclType recv_t = (JaclType)args[0]->inferred_type;
    uint32_t recv_sidx = args[0]->inferred_struct_idx;
    /* Compile the pointer expression. */
    compiler__compile_node(c, args[0]);
    if (recv_t == TYPE_PTR && JACL_IS_SCALAR_TYPE_IDX(recv_sidx)) {
      JaclType pointee = JACL_TYPE_IDX_TO_SCALAR(recv_sidx);
      compiler__emit_byte(c, OP_PTR_LOAD, line);
      compiler__emit_u16(c, 0, line);
      compiler__emit_byte(c, (uint8_t)pointee, line);
      c->last_expr_type = pointee;
      return;
    }
    if (recv_t == TYPE_PTR && recv_sidx != UINT32_MAX &&
        !JACL_IS_SCALAR_TYPE_IDX(recv_sidx)) {
      compiler__error(c, line, col,
                      "ptr-deref: struct pointees not supported here — "
                      "use $p->field for field access");
      return;
    }
    if (recv_t != TYPE_DYN) {
      compiler__error(c, line, col,
                      "ptr-deref: expected a typed pointer ([Ptr T])");
      return;
    }
    /* Dyn operand: typer can't validate. The runtime will trap on
     * non-pointer values via OP_PTR_LOAD's tag check. Default to
     * 8-byte u64 load. */
    compiler__emit_byte(c, OP_PTR_LOAD, line);
    compiler__emit_u16(c, 0, line);
    compiler__emit_byte(c, (uint8_t)TYPE_U64, line);
    c->last_expr_type = TYPE_U64;
    return;
  }

  /* await — suspension point (state machine) or job wait */
  if (hid == HEAD_AWAIT) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "await", "1 argument", argc);
      return;
    }
    if (c->sm_analysis) {
      /* SM context: await is a suspension point */
      if (c->in_try_body) {
        compiler__error(c, line, col,
            "cannot suspend inside try/catch; use error capture on futures instead");
        return;
      }
      if (c->in_non_suspending_callback) {
        compiler__error(c, line, col,
            "cannot suspend inside non-suspending callback");
        return;
      }
      /* SM await: spill enclosing operand stack, compile future,
         set resume_point, emit OP_AWAIT_SM.
         Inline (resolved): OP_AWAIT_SM pushes result; restore spilled
         values below it via the scratch slot; jump past resume label.
         Resume (pending):  dispatch table lands at resume label;
         OP_GET_LOCAL 1 pushes __rv; same restore. See
         SuspensionPoint::pre_stack_depth. */
      uint16_t depth = compiler__suspension_pre_stack_depth(c);
      compiler__emit_spill_operand_stack(c, depth, line);
      compiler__compile_node(c, args[0]);
      uint32_t sp_idx = c->sm_suspension_idx++;
      compiler__emit_constant(c, jacl_i32((int32_t)(sp_idx + 1)), line);
      compiler__emit_byte(c, OP_SET_RESUME_POINT, line);
      compiler__emit_byte(c, OP_AWAIT_SM, line);
      /* Inline path: result already on stack. */
      compiler__emit_restore_operand_stack(c, depth, line);
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here. */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
      compiler__emit_restore_operand_stack(c, depth, line);
      /* Common path: result on stack. */
      compiler__patch_jump(c, skip_jump);
      c->last_expr_type = TYPE_DYN;
      return;
    }
    /* Non-SM context: blocking await for jobs (and resolved futures).
       Compile argument and emit OP_AWAIT_JOB which blocks on jobs. */
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_AWAIT_JOB, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* sleep — suspending pause. In SM context, registers a deadline with the
     runtime and suspends (worker idle loop fires the resumption); at
     toplevel/non-SM, blocks on nanosleep. Always evaluates to nil. */
  if (hid == HEAD_SLEEP) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "sleep", "1 argument", argc);
      return;
    }
    if (c->sm_analysis) {
      if (c->in_try_body) {
        compiler__error(c, line, col,
            "cannot suspend inside try/catch; use error capture on futures instead");
        return;
      }
      if (c->in_non_suspending_callback) {
        compiler__error(c, line, col,
            "cannot suspend inside non-suspending callback");
        return;
      }
      /* SM sleep: spill enclosing operand stack, compile duration,
         set resume_point, emit OP_SLEEP_SM (always suspends — no
         inline-resolved path). Resume label lands at dispatch backpatch;
         OP_GET_LOCAL 1 pushes __rv (nil); restore spilled values below
         nil via the scratch slot. See SuspensionPoint::pre_stack_depth. */
      uint16_t depth = compiler__suspension_pre_stack_depth(c);
      compiler__emit_spill_operand_stack(c, depth, line);
      compiler__compile_node(c, args[0]);
      uint32_t sp_idx = c->sm_suspension_idx++;
      compiler__emit_constant(c, jacl_i32((int32_t)(sp_idx + 1)), line);
      compiler__emit_byte(c, OP_SET_RESUME_POINT, line);
      compiler__emit_byte(c, OP_SLEEP_SM, line);
      /* Resume label: dispatch table backpatch lands here on wake. */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
      compiler__emit_restore_operand_stack(c, depth, line);
      c->last_expr_type = TYPE_NIL;
      return;
    }
    /* Non-SM context: just block the current thread. */
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SLEEP_BLOCK, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* yield — generator suspension point (state machine). */
  if (hid == HEAD_YIELD) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "yield", "1 argument", argc);
      return;
    }
    if (c->sm_analysis) {
      /* SM yield: spill enclosing operand stack, compile value,
         set resume_point, emit OP_YIELD_SM. After resume, push nil
         (yield's value) and restore spilled values below it. See
         SuspensionPoint::pre_stack_depth. */
      uint16_t depth = compiler__suspension_pre_stack_depth(c);
      compiler__emit_spill_operand_stack(c, depth, line);
      compiler__compile_node(c, args[0]);
      uint32_t sp_idx = c->sm_suspension_idx++;
      compiler__emit_constant(c, jacl_i32((int32_t)(sp_idx + 1)), line);
      compiler__emit_byte(c, OP_SET_RESUME_POINT, line);
      compiler__emit_byte(c, OP_YIELD_SM, line);
      /* Dispatch table label lands here after resume */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      /* On resume, only __sm and __rv exist on the stack.
         Reset local_count so non-crossing stack locals from the
         previous segment get fresh slot numbers. */
      if (c->local_count > 2) {
        c->local_count = 2;
      }
      /* Push nil as yield expression result (popped by check_error) */
      compiler__emit_byte(c, OP_NIL, line);
      compiler__emit_restore_operand_stack(c, depth, line);
      c->has_yield = true;
      c->last_expr_type = TYPE_NIL;
      return;
    }
    compiler__error(c, line, col,
        "yield requires state machine compilation (internal error)");
    return;
  }

  /* take — take first N elements from stream or vector */
  if (hid == HEAD_TAKE) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "take", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    JaclType col_type = (JaclType)args[0]->inferred_type;
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_TAKE, line);
    c->last_expr_type = col_type;
    return;
  }

  /* exec — spawn external command, return map {stdout, stderr, exit} (US-006)
   * [exec cmd arg1 arg2 ...] spawns a subprocess, waits for completion,
   * and returns a map with stdout (stream), stderr (string), exit (i32).
   * This is the "full form" that gives access to all process outputs. */
  if (hid == HEAD_EXEC) {
    if (argc < 1) {
      compiler__builtin_arity_error(c, line, col, "exec", "at least 1 argument", argc);
      return;
    }
    /* Compile all args (cmd + args) into a vector */
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      compiler__ensure_boxed(c, line);
    }
    if (argc > 255) {
      compiler__error(c, line, col, "too many arguments to exec");
      return;
    }
    compiler__emit_byte(c, OP_VEC, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    compiler__emit_byte(c, OP_EXEC, line);
    compiler__emit_byte(c, EXEC_FLAG_FULL, line);
    c->last_expr_type = TYPE_MAP;
    return;
  }

  /* signal — send signal to a background job (US-011)
   * [signal $job SIGTERM] sends signal to the job's process, returns $true/$false.
   * Valid signal names: SIGTERM, SIGKILL, SIGINT, SIGHUP, SIGUSR1, SIGUSR2 */
  if (hid == HEAD_SIGNAL) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "signal", "2 arguments", argc);
      return;
    }
    /* First arg is the job */
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    /* Second arg is the signal name (must resolve to string) */
    compiler__compile_node(c, args[1]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_SIGNAL, line);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* cancel — send SIGTERM to a background job (US-011)
   * [cancel $job] is shorthand for [signal $job SIGTERM] */
  if (hid == HEAD_CANCEL) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "cancel", "1 argument", argc);
      return;
    }
    /* Compile as [signal $job SIGTERM] */
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__emit_constant(c, jacl_intern(c->heap, c->intern_table, "SIGTERM", 7), line);
    compiler__emit_byte(c, OP_SIGNAL, line);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* read-file — [read-file path] → string contents (or error value) */
  if (hid == HEAD_READ_FILE) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "read-file", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_READ_FILE, line);
    c->last_expr_type = TYPE_STR;
    return;
  }

  /* write-file — [write-file content path] → nil (or error value).
   * Content can be a string or stream. Stream elements are stringified
   * and joined with newlines (matches exec stdin convention). */
  if (hid == HEAD_WRITE_FILE) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "write-file", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__compile_node(c, args[1]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_WRITE_FILE, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* append-file — [append-file content path] → nil (or error value). */
  if (hid == HEAD_APPEND_FILE) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "append-file", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__ensure_boxed(c, line);
    compiler__compile_node(c, args[1]);
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_APPEND_FILE, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* parallel — suspension point (state machine) */
  if (hid == HEAD_PARALLEL) {
    if (argc < 2) {
      compiler__builtin_arity_error(c, line, col, "parallel",
                                     "at least 2 arguments", argc);
      return;
    }
    if (c->in_try_body) {
      compiler__error(c, line, col,
          "cannot suspend inside try/catch; use error capture on futures instead");
      return;
    }
    if (c->in_non_suspending_callback) {
      compiler__error(c, line, col,
          "cannot suspend inside non-suspending callback");
      return;
    }
    if (c->sm_analysis) {
      /* SM parallel: spill enclosing operand stack, compile bodies into
         closures, set resume_point, push state object, emit OP_PARALLEL.
         Inline (single-threaded): result on stack; restore spilled values
         below it. Resume (runtime): dispatch label lands here; push __rv;
         same restore. See SuspensionPoint::pre_stack_depth. */
      uint16_t depth = compiler__suspension_pre_stack_depth(c);
      compiler__emit_spill_operand_stack(c, depth, line);
      for (uint32_t i = 0; i < argc; i++) {
        compiler__compile_parallel_body(c, args[i], line, col);
      }
      uint32_t sp_idx = c->sm_suspension_idx++;
      compiler__emit_constant(c, jacl_i32((int32_t)(sp_idx + 1)), line);
      compiler__emit_byte(c, OP_SET_RESUME_POINT, line);
      /* Push state machine object as "continuation" — VM detects SM path */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 0, line);
      compiler__emit_byte(c, OP_PARALLEL, line);
      compiler__emit_byte(c, (uint8_t)argc, line);
      /* Inline path: result already on stack. */
      compiler__emit_restore_operand_stack(c, depth, line);
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
      compiler__emit_restore_operand_stack(c, depth, line);
      /* Common path: result on stack */
      compiler__patch_jump(c, skip_jump);
      c->last_expr_type = TYPE_DYN;
      return;
    }
    compiler__error(c, line, col,
        "parallel requires state machine compilation (internal error)");
    return;
  }

  /* race — suspension point (state machine) */
  if (hid == HEAD_RACE) {
    if (argc < 2) {
      compiler__builtin_arity_error(c, line, col, "race",
                                     "at least 2 arguments", argc);
      return;
    }
    if (c->in_try_body) {
      compiler__error(c, line, col,
          "cannot suspend inside try/catch; use error capture on futures instead");
      return;
    }
    if (c->in_non_suspending_callback) {
      compiler__error(c, line, col,
          "cannot suspend inside non-suspending callback");
      return;
    }
    if (c->sm_analysis) {
      /* SM race: same shape as parallel above (different op, otherwise
         identical operand-stack discipline). See SuspensionPoint::
         pre_stack_depth. */
      uint16_t depth = compiler__suspension_pre_stack_depth(c);
      compiler__emit_spill_operand_stack(c, depth, line);
      for (uint32_t i = 0; i < argc; i++) {
        compiler__compile_parallel_body(c, args[i], line, col);
      }
      uint32_t sp_idx = c->sm_suspension_idx++;
      compiler__emit_constant(c, jacl_i32((int32_t)(sp_idx + 1)), line);
      compiler__emit_byte(c, OP_SET_RESUME_POINT, line);
      /* Push state machine object as "continuation" — VM detects SM path */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 0, line);
      compiler__emit_byte(c, OP_RACE, line);
      compiler__emit_byte(c, (uint8_t)argc, line);
      /* Inline path: result already on stack. */
      compiler__emit_restore_operand_stack(c, depth, line);
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
      compiler__emit_restore_operand_stack(c, depth, line);
      /* Common path: result on stack */
      compiler__patch_jump(c, skip_jump);
      c->last_expr_type = TYPE_DYN;
      return;
    }
    compiler__error(c, line, col,
        "race requires state machine compilation (internal error)");
    return;
  }

  /* spawn — NOT a suspension point (runtime task submission) */
  if (hid == HEAD_SPAWN) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "spawn", "1 argument", argc);
      return;
    }
    if (args[0]->type != AST_BLOCK) {
      compiler__error(c, line, col, "spawn body must be a block");
      return;
    }

    AstNode* body_block = args[0];
    uint32_t stmt_count = body_block->data.block.count;
    AstNode** stmts = body_block->data.block.commands;

    /* Check if the spawn body contains suspension points */
    bool spawn_suspends = ast__contains_suspension(body_block, c->suspension_map,
                                                    c->heap, c->intern_table);

    SuspensionAnalysis spawn_sm_analysis;
    memset(&spawn_sm_analysis, 0, sizeof(spawn_sm_analysis));

    /* Allocate anonymous closure for the spawn body */
    JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
    chunk_init(&closure->chunk, c->arena);
    closure->name         = "<spawn>";
    closure->upvalue_count = 0;
    closure->upvalues     = NULL;
    closure->param_names  = NULL;
    closure->min_args     = 0;
    closure->variadic     = false;
    closure->pin_worker_id = -1;

    /* Pin spawn body to thread 0 if it mutates non-local variables
       OR captures a mutable (mut/box) binding from an enclosing scope. */
    bool needs_pinning = ast__contains_nonlocal_set(body_block,
                                                     c->heap, c->intern_table)
                      || compiler__body_captures_mutable(c, body_block);
    closure->pinned = needs_pinning;

    if (spawn_suspends) {
      /* SM spawn body: analyze suspensions, compile as state machine */
      spawn_sm_analysis = compiler__analyze_suspensions(body_block, NULL, 0, true, c->suspension_map, c->heap, c->intern_table, compiler__get_struct_registry(c));
      closure->param_count = 2;
      closure->param_total_slots = 2;
      JaclVal* pnames = (JaclVal*)arena_alloc(c->arena, sizeof(JaclVal) * 2);
      pnames[0] = jacl_inline_string("__sm", 4);
      pnames[1] = jacl_inline_string("__rv", 4);
      closure->param_names = pnames;
      closure->sm_field_count = (uint8_t)spawn_sm_analysis.state_layout.total_slots;
      closure->is_sm_compiled = true;
    } else {
      closure->param_count = 0;
      closure->param_total_slots = 0;
    }

    /* Create body compiler */
    Compiler body_compiler;
    compiler__init(&body_compiler, &closure->chunk, c->arena, c->intern_table, c->heap);
    body_compiler.scope_depth    = 1;
    body_compiler.enclosing      = c;
    body_compiler.suspension_map = c->suspension_map;
    body_compiler.pin_all_closures = needs_pinning;
    body_compiler.current_scope_mark = c->current_scope_mark;

    /* Copy global arities for suspension lookups */
    {
      Compiler* root = c;
      while (root->enclosing) root = root->enclosing;
      memcpy(body_compiler.global_arities, root->global_arities,
             sizeof(GlobalArity) * root->global_arity_count);
      body_compiler.global_arity_count = root->global_arity_count;
    }

    if (spawn_suspends) {
      /* SM body: add internal params as locals, compile via SM */
      compiler__add_local(&body_compiler, jacl_inline_string("__sm", 4), line, col);
      body_compiler.locals[body_compiler.local_count - 1].is_param = true;
      compiler__add_local(&body_compiler, jacl_inline_string("__rv", 4), line, col);
      body_compiler.locals[body_compiler.local_count - 1].is_param = true;
      body_compiler.in_concurrent_body = true;

      SuspensionAnalysis* analysis_ptr =
          (SuspensionAnalysis*)arena_alloc(c->arena, sizeof(SuspensionAnalysis));
      *analysis_ptr = spawn_sm_analysis;
      body_compiler.sm_analysis = analysis_ptr;

      compiler__compile_sm_stmts(&body_compiler, stmts, stmt_count, line, true);
    } else {
      /* Non-suspending spawn body: compile as block expression */
      body_compiler.in_concurrent_body = true;
      compiler__compile_block_expr(&body_compiler, body_block);
      compiler__emit_byte(&body_compiler, OP_RETURN, line);
    }

    /* Propagate errors */
    c->error_count += body_compiler.error_count;
    if (!c->first_error && body_compiler.first_error) {
      c->first_error = body_compiler.first_error;
    }

    closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;
    /* US-008: compute upvalue_total_slots */
    {
      uint16_t total = 0;
      for (uint32_t i = 0; i < body_compiler.upvalue_count; i++)
        total += body_compiler.upvalues[i].width;
      closure->upvalue_total_slots = total;
    }

    /* Emit OP_CLOSURE + upvalue descriptors */
    uint16_t closure_idx = chunk_add_constant(c->chunk, jacl_closure(closure));
    compiler__emit_byte(c, OP_CLOSURE, line);
    compiler__emit_u16(c, closure_idx, line);
    for (uint32_t i = 0; i < body_compiler.upvalue_count; i++) {
      compiler__emit_byte(c, body_compiler.upvalues[i].is_local, line);
      compiler__emit_byte(c, body_compiler.upvalues[i].index, line);
      compiler__emit_byte(c, (uint8_t)body_compiler.upvalues[i].width, line);
    }

    compiler__emit_byte(c, OP_SPAWN, line);
    return;
  }

  /* Struct/map/module field access/mutation: [. $s field] or [. $s field value] */
  if (hid == HEAD_DOT) {
    bool is_set = (argc == 3);
    if (argc != 2 && argc != 3) {
      compiler__builtin_arity_error(c, line, col, ".", "2 or 3 arguments", argc);
      return;
    }

    /* Buf element access via arrow: `$buf->N` parses as `[. $buf N]`
     * where the field is AST_LIT_INT. Compile as OP_BUF_GET_LOCAL (read)
     * or OP_BUF_SET_LOCAL (write) with the index pushed via OP_CONST.
     * See BUFFER_DESIGN.md M3. */
    if (args[0]->type == AST_VAR_REF && args[1]->type == AST_LIT_INT) {
      AstNode* recv = args[0];
      JaclVal recv_name = compiler__name_val(c->heap, c->intern_table,
          recv->data.var_ref.name, recv->data.var_ref.length);
      int found = -1;
      for (int i = (int)c->local_count - 1; i >= 0; i--) {
        if (c->locals[i].name == recv_name) { found = i; break; }
      }
      if (found >= 0 && c->locals[found].type == TYPE_BUF &&
          JACL_IS_SCALAR_TYPE_IDX(c->locals[found].struct_type_idx)) {
        uint32_t base_slot = (uint32_t)found;
        uint32_t buf_len   = c->locals[found].buf_len;
        JaclType elem_type =
            JACL_TYPE_IDX_TO_SCALAR(c->locals[found].struct_type_idx);
        int32_t  idx_lit = args[1]->data.lit_int.value;
        if (idx_lit < 0 || (uint32_t)idx_lit >= buf_len) {
          char err[160];
          snprintf(err, sizeof(err),
              "buf index %d out of bounds for [Buf %u %s]",
              (int)idx_lit, (unsigned)buf_len, type_name(elem_type));
          compiler__error(c, line, col, err);
          return;
        }
        /* Push the constant index */
        compiler__emit_constant(c, jacl_i32(idx_lit), line);
        if (is_set) {
          /* `set $buf->N V` parses as HEAD_SET which rewrites to
           * `[. $buf N V]`; compile the value then emit the store. */
          compiler__compile_node(c, args[2]);
          compiler__emit_byte(c, OP_BUF_SET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)base_slot, line);
          compiler__emit_byte(c, (uint8_t)elem_type, line);
          compiler__emit_u16(c, (uint16_t)buf_len, line);
          compiler__emit_byte(c, OP_NIL, line);
          c->last_expr_type = TYPE_NIL;
          return;
        }
        compiler__emit_byte(c, OP_BUF_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)base_slot, line);
        compiler__emit_byte(c, (uint8_t)elem_type, line);
        compiler__emit_u16(c, (uint16_t)buf_len, line);
        switch (elem_type) {
          case TYPE_I8: case TYPE_U8:
          case TYPE_I16: case TYPE_U16:
            c->last_expr_type = TYPE_I32; break;
          default:
            c->last_expr_type = elem_type; break;
        }
        return;
      }
    }

    /* Check for module binding: $modname->field with literal field name
       This is resolved at compile time to a direct global access. */
    if (args[0]->type == AST_VAR_REF && args[1]->type == AST_LIT_STRING) {
      const char* var_name = args[0]->data.var_ref.name;
      uint32_t var_len = args[0]->data.var_ref.length;
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, var_name, var_len);
      int local_idx = compiler__resolve_local(c, name_val);

      if (local_idx != -1) {
        Module* mod = compiler__find_module_binding(c, local_idx);
        if (mod != NULL) {
          /* This is a module binding — resolve field at compile time */
          const char* field_name = args[1]->data.lit_string.value;
          uint32_t field_len = args[1]->data.lit_string.length;

          /* Mutation not supported on module bindings */
          if (is_set) {
            compiler__error(c, line, col,
                            "cannot set field on module binding");
            return;
          }

          /* Check for private field access */
          if (field_len > 0 && field_name[0] == '_') {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "cannot access private member '%.*s' of module",
                     (int)field_len, field_name);
            char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
            memcpy(msg, buf, strlen(buf) + 1);
            compiler__error(c, line, col, msg);
            return;
          }

          /* Find the export in the module */
          ExportEntry* found_export = NULL;
          for (uint32_t ei = 0; ei < mod->export_count; ei++) {
            if (mod->exports[ei].name_len == field_len &&
                memcmp(mod->exports[ei].name, field_name, field_len) == 0) {
              found_export = &mod->exports[ei];
              break;
            }
          }

          if (!found_export) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "'%.*s' is not exported by module",
                     (int)field_len, field_name);
            char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
            memcpy(msg, buf, strlen(buf) + 1);
            compiler__error(c, line, col, msg);
            return;
          }

          /* Emit direct global access to the module's export */
          uint32_t dep_prefix_len;
          const char* dep_prefix = module__build_prefix(mod->path, c->arena, &dep_prefix_len);
          char dep_buf[256];
          uint32_t dep_total = dep_prefix_len + field_len;
          if (dep_total >= sizeof(dep_buf)) dep_total = sizeof(dep_buf) - 1;
          memcpy(dep_buf, dep_prefix, dep_prefix_len);
          memcpy(dep_buf + dep_prefix_len, field_name, dep_total - dep_prefix_len);
          dep_buf[dep_total] = '\0';
          JaclVal dep_key = jacl_intern(c->heap, c->intern_table, dep_buf, dep_total);
          uint16_t get_idx = chunk_add_constant(c->chunk, dep_key);
          compiler__emit_global_op(c, OP_GET_GLOBAL, get_idx, line);

          /* Set type info from export */
          c->last_expr_type = found_export->type;
          return;
        }
      }
    }

    /* Stage 5b: typed pointer field access — `$p->x` / `[. $p field]`
     * (and arbitrarily nested chains $p->a->b->c through embedded
     * struct fields). Walk the dot chain to accumulate the byte
     * offset, then emit ONE opcode based on what the chain
     * terminates at: OP_PTR_LOAD/STORE for scalar leaves,
     * OP_PTR_LOAD_INLINE/STORE_INLINE for struct leaves. */
    if (args[1]->type == AST_LIT_STRING) {
      /* For both 2-arg read and 3-arg set, args[0] is the receiver
       * leading up to args[1] (the final field name). */
      AstNode* base_node;
      uint32_t accumulated_offset;
      JaclType term_t;
      uint32_t term_sidx;
      if (compiler__resolve_ptr_chain_step(c, args[0],
                                           args[1]->data.lit_string.value,
                                           args[1]->data.lit_string.length,
                                           &base_node, &accumulated_offset,
                                           &term_t, &term_sidx)) {
        /* Compile the base [Ptr Struct] expression — leaves u64 on TOS. */
        compiler__compile_node(c, base_node);
        if (is_set) {
          /* Compile the value to write. */
          compiler__compile_node(c, args[2]);
          if (term_t == TYPE_STRUCT) {
            compiler__emit_byte(c, OP_PTR_STORE_INLINE, line);
            compiler__emit_u16(c, (uint16_t)accumulated_offset, line);
            compiler__emit_u16(c, (uint16_t)term_sidx, line);
            /* Inline-store leaves the pointer on TOS; set produces nil. */
            compiler__emit_byte(c, OP_POP, line);
          } else {
            compiler__emit_byte(c, OP_PTR_STORE, line);
            compiler__emit_u16(c, (uint16_t)accumulated_offset, line);
            compiler__emit_byte(c, (uint8_t)term_t, line);
            compiler__emit_byte(c, OP_POP, line);
          }
          compiler__emit_byte(c, OP_NIL, line);
          c->last_expr_type = TYPE_NIL;
        } else {
          if (term_t == TYPE_STRUCT) {
            compiler__emit_byte(c, OP_PTR_LOAD_INLINE, line);
            compiler__emit_u16(c, (uint16_t)accumulated_offset, line);
            compiler__emit_u16(c, (uint16_t)term_sidx, line);
            c->last_expr_type = TYPE_STRUCT;
          } else {
            compiler__emit_byte(c, OP_PTR_LOAD, line);
            compiler__emit_u16(c, (uint16_t)accumulated_offset, line);
            compiler__emit_byte(c, (uint8_t)term_t, line);
            c->last_expr_type = term_t;
          }
        }
        return;
      }
    }

    /* US-005/US-008: Check for inline struct field access.
     * If args[0] is a var ref to an inline struct local or upvalue,
     * use byte-offset addressing directly (no heap dereference). */
    bool args0_compiled = false;  /* track Case 2 compilation to avoid double-compile */
    if (args[1]->type == AST_LIT_STRING && !c->sm_analysis) {
      const char* field_name_i = args[1]->data.lit_string.value;
      uint32_t field_name_len_i = args[1]->data.lit_string.length;
      bool is_inline_access = false;
      bool is_upvalue_inline = false; /* US-008: true for upvalue-based inline access */
      uint8_t inline_base = 0;
      uint16_t inline_offset = 0;
      uint32_t inline_sidx = UINT32_MAX;

      /* Case 1: direct var ref to inline struct local */
      if (args[0]->type == AST_VAR_REF && args[0]->data.var_ref.length <= 128) {
        JaclVal vname = compiler__name_val(c->heap, c->intern_table,
            args[0]->data.var_ref.name, args[0]->data.var_ref.length);
        int slot = compiler__resolve_local(c, vname);
        if (slot != -1 && c->locals[slot].type == TYPE_STRUCT &&
            c->locals[slot].is_inline && !c->locals[slot].is_mutable) {
          is_inline_access = true;
          inline_base = (uint8_t)slot;
          inline_offset = 0;
          inline_sidx = c->locals[slot].struct_type_idx;
        }

        /* US-008: Case 1b: var ref to inline struct upvalue.
           Note: resolve_upvalue rejects bare struct capture, so this path
           is currently unreachable. Kept for when typed closures allow it. */
        if (!is_inline_access && slot == -1) {
          int uv = compiler__resolve_upvalue(c, vname, line, col);
          if (uv != -1 && c->upvalues[uv].type == TYPE_STRUCT &&
              c->upvalues[uv].is_inline && !c->upvalues[uv].is_mutable) {
            is_inline_access = true;
            is_upvalue_inline = true;
            inline_base = (uint8_t)c->upvalues[uv].base_slot;
            inline_offset = 0;
            inline_sidx = c->upvalues[uv].struct_type_idx;
          }
        }
      }

      /* Case 2: chained access — compile inner expr, check for inline ref */
      if (!is_inline_access && args[0]->type == AST_COMMAND) {
        compiler__compile_node(c, args[0]);
        args0_compiled = true;
        if (c->inline_repr == INLINE_REF) {
          /* The inner expr pushed the nested struct as N inline slots.
             We're switching to byte-offset chaining on the OUTER struct,
             so drop the inner's slots. */
          StructTypeRegistry* reg = compiler__get_struct_registry(c);
          uint32_t inner_struct_idx = args[0]->inferred_struct_idx;
          uint32_t inner_width = struct__slot_width(reg, inner_struct_idx);
          if (inner_width == 1) {
            compiler__emit_byte(c, OP_POP, line);
          } else {
            compiler__emit_byte(c, OP_POP_N, line);
            compiler__emit_byte(c, (uint8_t)inner_width, line);
          }
          is_inline_access = true;
          inline_base = c->inline_ref_base;
          inline_offset = c->inline_ref_offset;
          inline_sidx = inner_struct_idx;
          c->inline_repr = INLINE_NONE;
        }
      }

      if (is_inline_access && inline_sidx != UINT32_MAX) {
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        if (reg && inline_sidx < reg->count && reg->defs[inline_sidx]) {
          StructTypeDef* sdef = reg->defs[inline_sidx];
          uint32_t fi;
          for (fi = 0; fi < sdef->field_count; fi++) {
            if (sdef->fields[fi].name_len == field_name_len_i &&
                memcmp(sdef->fields[fi].name, field_name_i, field_name_len_i) == 0)
              break;
          }
          if (fi == sdef->field_count) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg),
                     "struct '%.*s' has no field '%.*s'",
                     (int)sdef->name_len, sdef->name,
                     (int)field_name_len_i, field_name_i);
            compiler__error(c, line, col, err_msg);
            return;
          }
          uint16_t total_offset = inline_offset + (uint16_t)sdef->fields[fi].offset;

          /* US-008: select local vs upvalue variant of struct opcodes */
          uint8_t get_op = is_upvalue_inline ? OP_STRUCT_GET_UPVALUE : OP_STRUCT_GET_INLINE;
          uint8_t set_op = is_upvalue_inline ? OP_STRUCT_SET_UPVALUE : OP_STRUCT_SET_INLINE;

          if (is_set) {
            /* Per-field mutability check */
            if (!sdef->fields[fi].is_mutable) {
              char err_msg[192];
              snprintf(err_msg, sizeof(err_msg),
                       "cannot mutate immutable field '%.*s' on struct '%.*s'",
                       (int)sdef->fields[fi].name_len, sdef->fields[fi].name,
                       (int)sdef->name_len, sdef->name);
              compiler__error(c, line, col, err_msg);
              return;
            }
            /* Compile new value with type checking */
            JaclType field_type = sdef->fields[fi].type;
            compiler__compile_node(c, args[2]);
            JaclType val_type = (JaclType)args[2]->inferred_type;
            if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) {
              char err_msg[192];
              jacl_format_field_mismatch(err_msg, sizeof(err_msg),
                  sdef->name, sdef->name_len,
                  sdef->fields[fi].name, sdef->fields[fi].name_len,
                  field_type, val_type);
              compiler__error(c, line, col, err_msg);
              return;
            }
            if (field_type != TYPE_DYN && val_type == TYPE_DYN) {
              char err_msg[224];
              jacl_format_field_dyn_assign(err_msg, sizeof(err_msg),
                  sdef->name, sdef->name_len,
                  sdef->fields[fi].name, sdef->fields[fi].name_len,
                  field_type);
              compiler__error(c, line, col, err_msg);
              return;
            }
            compiler__emit_byte(c, set_op, line);
            compiler__emit_byte(c, inline_base, line);
            compiler__emit_u16(c, total_offset, line);
            compiler__emit_byte(c, (uint8_t)field_type, line);
            c->last_expr_type = TYPE_NIL;
          } else {
            if (sdef->fields[fi].type == TYPE_STRUCT) {
              /* Nested struct field — push N inline slots (the field's
               * bytes copied from the outer's stack region). If this is
               * chained (e.g. $ln->start->x), Case 2 will detect
               * inline_repr == INLINE_REF, pop those N slots, and use
               * byte-offset addressing on the OUTER struct directly —
               * no allocation either way. */
              compiler__emit_byte(c, get_op, line);
              compiler__emit_byte(c, inline_base, line);
              compiler__emit_u16(c, total_offset, line);
              compiler__emit_byte(c, (uint8_t)TYPE_STRUCT, line);
              compiler__emit_u16(c, (uint16_t)sdef->fields[fi].struct_type_idx, line);
              c->inline_repr = INLINE_REF;
              c->inline_ref_base = inline_base;
              c->inline_ref_offset = total_offset;
              c->last_expr_type = TYPE_STRUCT;
            } else {
              /* Scalar field — emit inline get */
              compiler__emit_byte(c, get_op, line);
              compiler__emit_byte(c, inline_base, line);
              compiler__emit_u16(c, total_offset, line);
              compiler__emit_byte(c, (uint8_t)sdef->fields[fi].type, line);
              c->last_expr_type = sdef->fields[fi].type;
            }
          }
          return;
        }
      }
    }

    /* Compile struct/map expression — only if Case 2 above didn't already
       compile it. Re-compiling re-emits args[0]'s bytecode (running side
       effects twice). */
    if (!args0_compiled) {
      compiler__compile_node(c, args[0]);
    }
    JaclType struct_type = (JaclType)args[0]->inferred_type;
    uint32_t struct_idx = args[0]->inferred_struct_idx;

    /* INLINE_REF only flows out of the inline-fast-path's nested-struct
       GET_INLINE inside this same function, and Case 2 above always catches
       it (its only producer is AST_COMMAND form). No cleanup needed here. */

    if (struct_type != TYPE_STRUCT && struct_type != TYPE_MAP && struct_type != TYPE_DYN) {
      compiler__error(c, line, col, "type error: '.' requires a struct or map value");
      return;
    }

    /* Field name must be a literal string */
    if (args[1]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "field name must be a literal identifier");
      return;
    }

    const char* field_name = args[1]->data.lit_string.value;
    uint32_t field_name_len = args[1]->data.lit_string.length;

    /* US-007: $ctx->field — resolve from CtxFieldList during compilation */
    if (struct_type == TYPE_STRUCT &&
        struct_idx == compiler__get_struct_registry(c)->ctx_type_idx) {
      CtxFieldList* ctx_fl = compiler__get_ctx_fields(c);
      if (ctx_fl) {
        uint32_t fi;
        for (fi = 0; fi < ctx_fl->count; fi++) {
          if (ctx_fl->fields[fi].name_len == field_name_len &&
              memcmp(ctx_fl->fields[fi].name, field_name, field_name_len) == 0)
            break;
        }
        if (fi == ctx_fl->count) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg), "no field '%.*s' on ctx",
                   (int)field_name_len, field_name);
          compiler__error(c, line, col, err_msg);
          return;
        }
        CtxField* cf = &ctx_fl->fields[fi];
        if (is_set) {
          if (!cf->is_mutable) {
            char err_msg[192];
            snprintf(err_msg, sizeof(err_msg),
                     "cannot mutate immutable field '%.*s' on struct 'ctx'",
                     (int)cf->name_len, cf->name);
            compiler__error(c, line, col, err_msg);
            return;
          }
          JaclType field_type = cf->type;
          compiler__compile_node(c, args[2]);
          JaclType val_type = (JaclType)args[2]->inferred_type;
          if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) {
            char err_msg[192];
            jacl_format_field_mismatch(err_msg, sizeof(err_msg),
                "ctx", 3, cf->name, cf->name_len, field_type, val_type);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (field_type != TYPE_DYN && val_type == TYPE_DYN) {
            char err_msg[224];
            jacl_format_field_dyn_assign(err_msg, sizeof(err_msg),
                "ctx", 3, cf->name, cf->name_len, field_type);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (field_type == TYPE_STRUCT) {
            /* Inline struct field: write bytes directly into ctx.data,
               no heap pointer intermediate. */
            compiler__emit_byte(c, OP_HEAP_RECORD_SET_INLINE, line);
            compiler__emit_u16(c, (uint16_t)cf->offset, line);
            compiler__emit_u16(c, (uint16_t)cf->struct_type_idx, line);
          } else {
            compiler__emit_byte(c, OP_HEAP_RECORD_SET, line);
            compiler__emit_u16(c, (uint16_t)cf->offset, line);
            compiler__emit_byte(c, (uint8_t)field_type, line);
          }
          c->last_expr_type = TYPE_STRUCT;
        } else {
          if (cf->type == TYPE_STRUCT) {
            /* Inline struct field: push N inline slots from ctx.data. */
            compiler__emit_byte(c, OP_HEAP_RECORD_GET_INLINE, line);
            compiler__emit_u16(c, (uint16_t)cf->offset, line);
            compiler__emit_u16(c, (uint16_t)cf->struct_type_idx, line);
            c->inline_repr = INLINE_STACK;
            c->last_expr_type = TYPE_STRUCT;
          } else {
            compiler__emit_byte(c, OP_HEAP_RECORD_GET, line);
            compiler__emit_u16(c, (uint16_t)cf->offset, line);
            compiler__emit_byte(c, (uint8_t)cf->type, line);
            c->last_expr_type = cf->type;
          }
        }
        return;
      }
    }

    if (struct_type == TYPE_STRUCT && struct_idx != UINT32_MAX) {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      if (reg && struct_idx < reg->count && reg->defs[struct_idx]) {
        StructTypeDef* sdef = reg->defs[struct_idx];
        /* Find field by name */
        uint32_t fi;
        for (fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->fields[fi].name_len == field_name_len &&
              memcmp(sdef->fields[fi].name, field_name, field_name_len) == 0) {
            break;
          }
        }
        if (fi == sdef->field_count) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "struct '%.*s' has no field '%.*s'",
                   (int)sdef->name_len, sdef->name,
                   (int)field_name_len, field_name);
          compiler__error(c, line, col, err_msg);
          return;
        }

        /* If struct is inline (e.g. from OP_RETURN_WIDE or a nested-struct
           field GET), use the TOS-aware inline opcodes — no heap reify.
           Field write on a transient inline struct is meaningless (the
           result is discarded), so reject it. */
        bool tos_inline = (c->inline_repr == INLINE_STACK ||
                           c->inline_repr == INLINE_REF);
        if (tos_inline && is_set) {
          compiler__error(c, line, col,
                          "cannot mutate a transient inline struct value — "
                          "assign it to a typed local first");
          return;
        }

        if (is_set) {
          /* Per-field mutability check */
          if (!sdef->fields[fi].is_mutable) {
            char err_msg[192];
            snprintf(err_msg, sizeof(err_msg),
                     "cannot mutate immutable field '%.*s' on struct '%.*s'",
                     (int)sdef->fields[fi].name_len, sdef->fields[fi].name,
                     (int)sdef->name_len, sdef->name);
            compiler__error(c, line, col, err_msg);
            return;
          }
          /* Compile new value with type checking */
          JaclType field_type = sdef->fields[fi].type;
          compiler__compile_node(c, args[2]);
          JaclType val_type = (JaclType)args[2]->inferred_type;

          if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) {
            char err_msg[192];
            jacl_format_field_mismatch(err_msg, sizeof(err_msg),
                sdef->name, sdef->name_len,
                sdef->fields[fi].name, sdef->fields[fi].name_len,
                field_type, val_type);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (field_type != TYPE_DYN && val_type == TYPE_DYN) {
            char err_msg[224];
            jacl_format_field_dyn_assign(err_msg, sizeof(err_msg),
                sdef->name, sdef->name_len,
                sdef->fields[fi].name, sdef->fields[fi].name_len,
                field_type);
            compiler__error(c, line, col, err_msg);
            return;
          }

          /* Emit OP_HEAP_RECORD_SET + field_offset (u16) + field_type (u8) */
          compiler__emit_byte(c, OP_HEAP_RECORD_SET, line);
          compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
          compiler__emit_byte(c, (uint8_t)field_type, line);

          /* Returns struct value */
          c->last_expr_type = TYPE_STRUCT;
        } else {
          if (tos_inline) {
            /* Pop inline struct bytes, push the field value (or sub-struct
               inline slots for TYPE_STRUCT). */
            compiler__emit_byte(c, OP_STRUCT_GET_INLINE_TOS, line);
            compiler__emit_u16(c, (uint16_t)struct_idx, line);
            compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
            compiler__emit_byte(c, (uint8_t)sdef->fields[fi].type, line);
            if (sdef->fields[fi].type == TYPE_STRUCT) {
              compiler__emit_u16(c, (uint16_t)sdef->fields[fi].struct_type_idx, line);
              c->inline_repr = INLINE_STACK;
            } else {
              c->inline_repr = INLINE_NONE;
            }
          } else {
            compiler__emit_byte(c, OP_HEAP_RECORD_GET, line);
            compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
            compiler__emit_byte(c, (uint8_t)sdef->fields[fi].type, line);
          }

          c->last_expr_type = sdef->fields[fi].type;
        }
        return;
      }
    }

    /* Known map type — use map operations */
    if (struct_type == TYPE_MAP) {
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, field_name, field_name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);

      if (is_set) {
        /* Push key then value, then emit OP_MAP_SET */
        compiler__emit_byte(c, OP_CONST, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__compile_node(c, args[2]);
        compiler__emit_byte(c, OP_MAP_SET, line);
        c->last_expr_type = TYPE_MAP;
      } else {
        /* Push key, then emit OP_MAP_GET */
        compiler__emit_byte(c, OP_CONST, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__emit_byte(c, OP_MAP_GET, line);
        c->last_expr_type = TYPE_DYN;
      }
      return;
    }

    /* Struct type unknown at compile time — emit runtime field resolution */
    {
      /* Store field name as a constant */
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, field_name, field_name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);

      if (is_set) {
        /* Compile the new value */
        compiler__compile_node(c, args[2]);
        /* Emit OP_HEAP_RECORD_SET_DYN + const_idx (field name) */
        compiler__emit_byte(c, OP_HEAP_RECORD_SET_DYN, line);
        compiler__emit_u16(c, name_idx, line);
        /* Result type is dyn (we don't know the struct type) */
        c->last_expr_type = TYPE_DYN;
      } else {
        /* Emit OP_HEAP_RECORD_GET_DYN + const_idx (field name) */
        compiler__emit_byte(c, OP_HEAP_RECORD_GET_DYN, line);
        compiler__emit_u16(c, name_idx, line);
        /* Result type is dyn (field type unknown at compile time) */
        c->last_expr_type = TYPE_DYN;
      }
    }
    return;
  }

  /* Optional chaining: [?. expr field] — nil-safe field access */
  if (hid == HEAD_QDOT) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "?.", "2 arguments", argc);
      return;
    }
    /* Compile the object expression */
    compiler__compile_node(c, args[0]);
    /* Reject structs at compile time — use -> for struct field access */
    if ((JaclType)args[0]->inferred_type == TYPE_STRUCT &&
        args[0]->inferred_struct_idx != UINT32_MAX) {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      StructTypeDef* sdef = reg->defs[args[0]->inferred_struct_idx];
      char err_msg[192];
      snprintf(err_msg, sizeof(err_msg),
               "?. cannot be used on struct '%.*s'; use -> for struct field access",
               (int)sdef->name_len, sdef->name);
      compiler__error(c, line, col, err_msg);
      return;
    }
    /* The field name must be a literal string */
    if (args[1]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "?. requires a literal field name");
      return;
    }
    JaclVal name_val = compiler__name_val(c->heap, c->intern_table,
        args[1]->data.lit_string.value,
        args[1]->data.lit_string.length);
    uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
    compiler__emit_byte(c, OP_OPTIONAL_GET, line);
    compiler__emit_u16(c, name_idx, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* Struct constructor: [StructName field1 field2 ...] */
  if (head->type == AST_LIT_STRING) {
    StructTypeRegistry* reg = compiler__get_struct_registry(c);
    uint32_t name_len = head->data.lit_string.length;
    uint32_t struct_idx = struct_registry__find(reg,
        head->data.lit_string.value, name_len);
    if (struct_idx != UINT32_MAX) {
      StructTypeDef* sdef = reg->defs[struct_idx];

      /* Arity check */
      if (argc != sdef->field_count) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg),
                 "struct '%.*s' has %u fields but got %u arguments",
                 (int)name_len, head->data.lit_string.value,
                 sdef->field_count, argc);
        compiler__error(c, line, col, err_msg);
        return;
      }

      /* User defstructs are always inline; ctx (the lone HeapRecord) uses
         the heap path. */
      bool use_inline = struct_def_is_user(sdef, reg);

      /* Compile and type-check each field argument */
      for (uint32_t i = 0; i < argc; i++) {
        JaclType field_type = sdef->fields[i].type;
        compiler__compile_node(c, args[i]);
        JaclType arg_type = (JaclType)args[i]->inferred_type;

        if (field_type != TYPE_DYN && arg_type != TYPE_DYN &&
            arg_type != field_type) {
          char err_msg[192];
          jacl_format_field_mismatch(err_msg, sizeof(err_msg),
              head->data.lit_string.value, name_len,
              sdef->fields[i].name, sdef->fields[i].name_len,
              field_type, arg_type);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (field_type != TYPE_DYN && arg_type == TYPE_DYN) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: field '%.*s' of struct '%.*s' expected %s, got dyn",
                   (int)sdef->fields[i].name_len, sdef->fields[i].name,
                   (int)name_len, head->data.lit_string.value,
                   type_name(field_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
        /* Nested struct field args stay inline — OP_STRUCT_NEW_INLINE
           consumes them as inline bytes directly. */
      }

      if (use_inline) {
        /* Emit OP_STRUCT_NEW_INLINE — stores raw bytes across stack slots */
        compiler__emit_byte(c, OP_STRUCT_NEW_INLINE, line);
        compiler__emit_u16(c, (uint16_t)struct_idx, line);
        c->inline_repr = INLINE_STACK;
      } else {
        /* Emit OP_HEAP_RECORD_NEW + uint16_t struct_type_index (heap path) */
        compiler__emit_byte(c, OP_HEAP_RECORD_NEW, line);
        compiler__emit_u16(c, (uint16_t)struct_idx, line);
        c->inline_repr = INLINE_NONE;
      }

      c->last_expr_type = TYPE_STRUCT;
      return;
    }
  }

  /* Dynamic call: unrecognized command head — look up and call */
  {
    /* Resolve callee param types for call-site type checking */
    JaclType* call_param_types = NULL;
    JaclType call_return_type = TYPE_DYN;
    uint32_t call_return_struct_idx = UINT32_MAX;
    int16_t call_param_count = -1;
    const char* callee_name_str = NULL;
    uint32_t callee_name_len = 0;

    /* Pre-resolve the callee name from the AST so we can detect a known
       suspending-proc call *before* compiling the head/args. If it is one,
       we must spill the enclosing operand-stack values into SM state fields
       first — otherwise an inline call like `+ [susp 10] [susp 16]` loses
       the first arg's result when the second call's OP_AWAIT_SM suspends.
       Mirrors the spill/restore discipline used by HEAD_AWAIT / HEAD_SLEEP /
       HEAD_YIELD / HEAD_PARALLEL / HEAD_RACE. */
    if (head->type == AST_LIT_STRING) {
      callee_name_str = head->data.lit_string.value;
      callee_name_len = head->data.lit_string.length;
    } else if (head->type == AST_VAR_REF) {
      callee_name_str = head->data.var_ref.name;
      callee_name_len = head->data.var_ref.length;
    }
    bool use_call_suspend = false;
    uint16_t suspend_spill_depth = 0;
    if (c->sm_analysis && c->suspension_map && callee_name_str &&
        callee_name_len <= 128) {
      JaclVal cname = compiler__name_val(c->heap, c->intern_table,
                                          callee_name_str, callee_name_len);
      if (suspension_map_lookup(c->suspension_map, cname) &&
          !suspension_map_is_generator(c->suspension_map, cname)) {
        use_call_suspend = true;
        suspend_spill_depth = compiler__suspension_pre_stack_depth(c);
        compiler__emit_spill_operand_stack(c, suspend_spill_depth, line);
      }
    }

    if (head->type == AST_LIT_STRING) {
      /* Look up bare word as a variable */
      uint32_t name_len = head->data.lit_string.length;
      if (name_len > 128) {
        compiler__error(c, line, col, "command name exceeds 128-byte limit");
        return;
      }
      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, head->data.lit_string.value, name_len);
      callee_name_str = head->data.lit_string.value;
      callee_name_len = name_len;

      /* SM mode: resolve callee from state object fields first */
      int sm_field_idx = -1;
      if (c->sm_analysis) {
        sm_field_idx = sm__find_field(&c->sm_analysis->state_layout, name_val);
      }

      int local_slot = compiler__resolve_local(c, name_val);

      /* Compile-time arity check and param type resolution */
      {
        int16_t head_arity = -1;
        if (local_slot != -1) {
          head_arity = c->locals[local_slot].known_arity;
          call_param_types = c->locals[local_slot].param_types;
          call_return_type = c->locals[local_slot].return_type;
          call_return_struct_idx = c->locals[local_slot].return_struct_idx;
          call_param_count = head_arity;
        } else {
          GlobalArity* ga = compiler__find_global(c, name_val);
          if (ga) {
            head_arity = ga->known_arity;
            call_param_types = ga->param_types;
            call_return_type = ga->return_type;
            call_return_struct_idx = ga->return_struct_idx;
            call_param_count = head_arity;
          }
        }
        if (head_arity != -1 && (int16_t)argc != head_arity) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "proc '%.*s' expects %d arguments but got %d",
                   (int)name_len, head->data.lit_string.value,
                   (int)head_arity, (int)argc);
          compiler__error(c, line, col, err_msg);
          return;
        }
      }

      if (sm_field_idx >= 0) {
        /* SM mode: read callee from state object field */
        bool is_mut = sm__is_field_mutable(&c->sm_analysis->state_layout, name_val);
        compiler__emit_byte(c, is_mut ? OP_GET_STATE_FIELD_CELL
                                      : OP_GET_STATE_FIELD, line);
        compiler__emit_byte(c, (uint8_t)sm_field_idx, line);
      } else if (local_slot != -1) {
        if (c->locals[local_slot].is_mutable) {
          compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
        } else {
          compiler__emit_byte(c, OP_GET_LOCAL, line);
        }
        compiler__emit_byte(c, (uint8_t)local_slot, line);
      } else {
        /* Prelude mode: reject names not in the prelude or source-defined globals */
        GlobalArity* ga = compiler__find_global(c, name_val);
        if (c->has_prelude && !ga) {
          /* REPL shell fallback: if enabled and exec is available,
           * treat unknown commands as shell commands (like !cmd args...) */
          if (c->shell_fallback) {
            JaclVal exec_name = compiler__name_val(c->heap, c->intern_table, "exec", 4);
            GlobalArity* exec_ga = compiler__find_global(c, exec_name);
            if (exec_ga) {
              /* Emit shell command: compile head + args into vector, then OP_EXEC */
              /* Compile command name as string constant */
              JaclVal cmd_str = compiler__name_val(c->heap, c->intern_table,
                  head->data.lit_string.value, name_len);
              uint16_t cmd_idx = chunk_add_constant(c->chunk, cmd_str);
              compiler__emit_byte(c, OP_CONST, line);
              compiler__emit_u16(c, cmd_idx, line);
              /* Compile arguments */
              for (uint32_t i = 0; i < argc; i++) {
                compiler__compile_node(c, args[i]);
                compiler__ensure_boxed(c, line);
              }
              /* Build vector: 1 (head) + argc */
              uint32_t total_elems = 1 + argc;
              if (total_elems > 255) {
                compiler__error(c, line, col, "too many arguments to shell command");
                return;
              }
              compiler__emit_byte(c, OP_VEC, line);
              compiler__emit_byte(c, (uint8_t)total_elems, line);
              compiler__emit_byte(c, OP_EXEC, line);
              compiler__emit_byte(c, 0, line);  /* Basic mode: flags = 0 */
              c->last_expr_type = TYPE_STREAM;
              return;
            }
          }
          char err_msg[160];
          snprintf(err_msg, sizeof(err_msg),
                   "undefined name '%.*s'", (int)name_len,
                   head->data.lit_string.value);
          compiler__error(c, line, col, err_msg);
          return;
        }
        JaclVal gkey = compiler__global_name_val(c,
            head->data.lit_string.value, name_len);
        uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
        compiler__emit_global_op(c, OP_GET_GLOBAL, name_idx, line);
      }
    } else {
      /* Non-string head (e.g. $var, nested command): compile as expression */
      /* Arity check for $var heads */
      if (head->type == AST_VAR_REF) {
        int16_t head_arity = compiler__node_known_arity(c, head);
        if (head_arity != -1 && (int16_t)argc != head_arity) {
          uint32_t name_len = head->data.var_ref.length;
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "proc '%.*s' expects %d arguments but got %d",
                   (int)name_len, head->data.var_ref.name,
                   (int)head_arity, (int)argc);
          compiler__error(c, line, col, err_msg);
          return;
        }
        /* Resolve param types for $var call-site checking */
        callee_name_str = head->data.var_ref.name;
        callee_name_len = head->data.var_ref.length;
        if (head->data.var_ref.length <= 128) {
          JaclVal vname = compiler__name_val(c->heap, c->intern_table, head->data.var_ref.name,
                                              head->data.var_ref.length);
          int slot = compiler__resolve_local(c, vname);
          if (slot != -1) {
            call_param_types = c->locals[slot].param_types;
            call_return_type = c->locals[slot].return_type;
            call_return_struct_idx = c->locals[slot].return_struct_idx;
            call_param_count = c->locals[slot].known_arity;
          } else {
            GlobalArity* ga = compiler__find_global(c, vname);
            if (ga) {
              call_param_types = ga->param_types;
              call_return_type = ga->return_type;
              call_return_struct_idx = ga->return_struct_idx;
              call_param_count = ga->known_arity;
            }
          }
        }
      }
      compiler__compile_node(c, head);
    }

    /* Compile arguments with call-site type checking.
       Phase 5a: track total slot count (struct params may occupy multiple slots). */
    uint32_t total_arg_slots = 0;
    for (uint32_t i = 0; i < argc; i++) {
      JaclType expected_param_type = TYPE_DYN;
      if (call_param_types && call_param_count > 0 && (int32_t)i < call_param_count) {
        expected_param_type = call_param_types[i];
      }

      /* Set contextual type for argument.
         Phase 5a: request inline struct for struct-typed params so constructors
         produce inline bytes directly. */
      if (expected_param_type != TYPE_DYN) {
      }
      compiler__compile_node(c, args[i]);
      JaclType arg_type = (JaclType)args[i]->inferred_type;

      /* Phase 5a: struct args passed inline (multi-slot) instead of as heap copies.
       * If the arg is already inline (from constructor or typed-get), nothing to do.
       * If it's a heap struct (from OP_STRUCT_MATERIALIZE or function return),
       * expand it to inline slots via OP_STRUCT_EXPAND. */
      if (expected_param_type == TYPE_STRUCT && arg_type == TYPE_STRUCT) {
        uint32_t sidx = args[i]->inferred_struct_idx;
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        if (reg && sidx != UINT32_MAX && sidx < reg->count &&
            struct_def_is_user(reg->defs[sidx], reg)) {
          uint32_t width = struct__slot_width(reg, sidx);
          if (c->inline_repr != INLINE_STACK) {
            /* Heap struct on stack — expand to inline slots */
            compiler__emit_byte(c, OP_STRUCT_EXPAND, line);
            compiler__emit_u16(c, (uint16_t)sidx, line);
          }
          total_arg_slots += width;
        } else {
          /* Non-value-type struct or unknown — treat as single slot (fallback) */
          total_arg_slots += 1;
        }
      } else {
        /* Reject struct passed to a dyn parameter — structs cannot cross
           dyn boundaries except via [box $val]. */
        if (arg_type == TYPE_STRUCT && expected_param_type != TYPE_STRUCT) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "cannot pass struct value to %s parameter — wrap with "
                   "[box $val] or change the parameter type to the struct type",
                   type_name(expected_param_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
        total_arg_slots += 1;
      }

      /* Reject typed collections passed to untyped params */
      if (expected_param_type == TYPE_DYN && is_typed_collection(arg_type)) {
        char err_msg[192];
        snprintf(err_msg, sizeof(err_msg),
                 "cannot pass bare %s to untyped parameter; use [box ...] to box it",
                 type_name(arg_type));
        compiler__error(c, line, col, err_msg);
        return;
      }

      /* Type check: argument vs declared param type */
      if (expected_param_type != TYPE_DYN) {
        if (arg_type != TYPE_DYN && arg_type != expected_param_type) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: argument %d of %.*s expected %s, got %s",
                   (int)(i + 1), (int)callee_name_len, callee_name_str,
                   type_name(expected_param_type), type_name(arg_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (arg_type == TYPE_DYN) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: argument %d of %.*s expected %s, got dyn (use [to %s $val])",
                   (int)(i + 1), (int)callee_name_len, callee_name_str,
                   type_name(expected_param_type), type_name(expected_param_type));
          compiler__error(c, line, col, err_msg);
          return;
        }
      }
    }

    /* `use_call_suspend` / `suspend_spill_depth` were computed up front so the
       enclosing operand stack could be spilled before head/args compiled. */
    if (use_call_suspend) {
      /* SM call to suspending proc: emit OP_CALL_SUSPEND + SM await sequence.
         OP_CALL_SUSPEND spawns inner SM as a task (concurrent) or falls through
         to synchronous call (single-threaded), pushing a future on stack.
         Then the SM await pattern handles suspension/inline resolution. */
      compiler__emit_byte(c, OP_CALL_SUSPEND, line);
      compiler__emit_byte(c, (uint8_t)argc, line);

      /* SM await sequence: set resume_point, OP_AWAIT_SM, dispatch backpatch */
      uint32_t sp_idx = c->sm_suspension_idx++;
      compiler__emit_constant(c, jacl_i32((int32_t)(sp_idx + 1)), line);
      compiler__emit_byte(c, OP_SET_RESUME_POINT, line);
      compiler__emit_byte(c, OP_AWAIT_SM, line);
      /* Inline path: result on stack — restore spilled enclosing operand
         stack below it via the scratch slot. */
      compiler__emit_restore_operand_stack(c, suspend_spill_depth, line);
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      /* Push resume value from slot 1 (__rv) onto stack, then restore. */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
      compiler__emit_restore_operand_stack(c, suspend_spill_depth, line);
      /* Common path: result on stack */
      compiler__patch_jump(c, skip_jump);
    } else {
      /* Regular call — Phase 5a: use total_arg_slots for slot-based arg count */
      compiler__emit_byte(c, OP_CALL, line);
      compiler__emit_byte(c, (uint8_t)total_arg_slots, line);
    }

    /* Set result type from callee's return type */
    c->last_expr_type = call_return_type;
    /* Phase 5b: struct-returning procs use OP_RETURN_WIDE → inline on caller's stack */
    if (call_return_type == TYPE_STRUCT && call_return_struct_idx != UINT32_MAX) {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      if (reg && call_return_struct_idx < reg->count) {
        StructTypeDef* sdef = reg->defs[call_return_struct_idx];
        if (struct_def_is_user(sdef, reg)) {
          c->inline_repr = INLINE_STACK;
        }
      }
    }
  }
}

/* -------------------------------------------------------------------------
 * Syntax-quote helpers
 * ------------------------------------------------------------------------- */

/* --- Syntax-quote compilation via make-syntax --- */

/* Compile an unquote child expression. Inside a syntax-quote context, a
 * bare word like `~cond` parses as AST_UNQUOTE(AST_LIT_STRING "cond") —
 * in that case we emit a variable reference to `cond` so the compiled
 * closure can look up the parameter/local by that name. For any other
 * expression kind, compile it normally. */
static void syntax__compile_unquote_child(Compiler *c, AstNode *child) {
    if (!child) return;
    if (child->type == AST_LIT_STRING) {
        /* Build a synthetic AST_VAR_REF and compile it. */
        AstNode vref;
        memset(&vref, 0, sizeof(vref));
        vref.type = AST_VAR_REF;
        vref.start = child->start;
        vref.end = child->end;
        vref.data.var_ref.name = child->data.lit_string.value;
        vref.data.var_ref.length = child->data.lit_string.length;
        compiler__compile_node(c, &vref);
        return;
    }
    compiler__compile_node(c, child);
}

/* Emit bytecode that constructs a JaclVal syntax object for the given AST
 * node using OP_SYNTAX_OP make-syntax subops (7-12).  Each call leaves
 * exactly one syntax JaclVal on the stack.
 *
 * Unquote (~expr): compiles inner expression normally — the result must
 * be a syntax object at runtime.
 *
 * Unquote-splicing (~@expr) is handled at the parent (command/block) level:
 * the parent builds the args/commands vec incrementally with vec-push and
 * vec-concat instead of a single OP_VEC instruction.
 *
 * Nested syntax-quote: emitted as a constant (no recursion into inner
 * unquotes — those belong to the inner quasiquotation level). */

static void syntax__compile_sq_node(Compiler *c, AstNode *node) {
    uint32_t line = node->start.line;

    switch (node->type) {
    case AST_LIT_INT:
        compiler__emit_constant(c, jacl_i32(node->data.lit_int.value), line);
        compiler__emit_byte(c, OP_SYNTAX_OP, line);
        compiler__emit_byte(c, 7, line);  /* make-syntax lit-int */
        break;

    case AST_LIT_FLOAT:
        compiler__emit_constant(c, jacl_f32(node->data.lit_float.value), line);
        compiler__emit_byte(c, OP_SYNTAX_OP, line);
        compiler__emit_byte(c, 8, line);  /* make-syntax lit-float */
        break;

    case AST_LIT_STRING: {
        JaclVal str = jacl_string_new(c->heap, c->intern_table,
                                       node->data.lit_string.value,
                                       (size_t)node->data.lit_string.length);
        compiler__emit_constant(c, str, line);
        compiler__emit_byte(c, OP_SYNTAX_OP, line);
        compiler__emit_byte(c, 9, line);  /* make-syntax lit-string */
        break;
    }

    case AST_VAR_REF: {
        JaclVal name = jacl_string_new(c->heap, c->intern_table,
                                        node->data.var_ref.name,
                                        (size_t)node->data.var_ref.length);
        compiler__emit_constant(c, name, line);
        compiler__emit_byte(c, OP_SYNTAX_OP, line);
        if (node->is_caret) {
            compiler__emit_byte(c, 15, line); /* make-syntax var-ref-caret (scope_mark=0) */
        } else {
            compiler__emit_byte(c, 10, line); /* make-syntax var-ref */
        }
        break;
    }

    case AST_COMMAND: {
        /* US-010: detect [gensym] / [gensym "prefix"] inside syntax-quote
         * and emit OP_SYNTAX_OP subop 16 (gensym builtin) instead of
         * compiling it as a normal make-syntax command. */
        {
            AstNode *head = node->data.command.head;
            uint32_t argc = node->data.command.arg_count;
            if (head->type == AST_LIT_STRING &&
                head->data.lit_string.length == 6 &&
                memcmp(head->data.lit_string.value, "gensym", 6) == 0 &&
                argc <= 1) {
                /* Emit prefix string constant */
                const char *prefix = "g";
                size_t prefix_len = 1;
                if (argc == 1 && node->data.command.args[0]->type == AST_LIT_STRING) {
                    prefix = node->data.command.args[0]->data.lit_string.value;
                    prefix_len = (size_t)node->data.command.args[0]->data.lit_string.length;
                }
                JaclVal pstr = jacl_string_new(c->heap, c->intern_table,
                                               prefix, prefix_len);
                compiler__emit_constant(c, pstr, line);
                compiler__emit_byte(c, OP_SYNTAX_OP, line);
                compiler__emit_byte(c, 16, line);  /* gensym */
                break;
            }
        }

        /* Head → syntax */
        syntax__compile_sq_node(c, node->data.command.head);

        /* Check for unquote-splicing in args */
        bool has_splice = false;
        for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            if (node->data.command.args[i]->type == AST_UNQUOTE_SPLICING)
                has_splice = true;
        }

        if (has_splice) {
            /* Build args vec incrementally: start with empty vec, push/concat */
            compiler__emit_byte(c, OP_VEC, line);
            compiler__emit_byte(c, 0, line);
            for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
                AstNode *arg = node->data.command.args[i];
                if (arg->type == AST_UNQUOTE_SPLICING) {
                    syntax__compile_unquote_child(c, arg->data.unquote_splicing.child);
                    /* US-012: validate splice operand before concat */
                    compiler__emit_byte(c, OP_SYNTAX_OP, line);
                    compiler__emit_byte(c, 18, line);  /* validate-unquote-splice */
                    compiler__emit_byte(c, OP_VEC_CONCAT, line);
                } else {
                    syntax__compile_sq_node(c, arg);
                    compiler__emit_byte(c, OP_VEC_PUSH, line);
                }
            }
        } else {
            /* Simple: compile all args, collect with OP_VEC */
            for (uint32_t i = 0; i < node->data.command.arg_count; i++)
                syntax__compile_sq_node(c, node->data.command.args[i]);
            compiler__emit_byte(c, OP_VEC, line);
            compiler__emit_byte(c, (uint8_t)node->data.command.arg_count, line);
        }

        /* make-syntax command (head, args-vec → syntax) */
        compiler__emit_byte(c, OP_SYNTAX_OP, line);
        compiler__emit_byte(c, 11, line);
        break;
    }

    case AST_BLOCK: {
        bool has_splice = false;
        for (uint32_t i = 0; i < node->data.block.count; i++) {
            if (node->data.block.commands[i]->type == AST_UNQUOTE_SPLICING)
                has_splice = true;
        }

        if (has_splice) {
            compiler__emit_byte(c, OP_VEC, line);
            compiler__emit_byte(c, 0, line);
            for (uint32_t i = 0; i < node->data.block.count; i++) {
                AstNode *cmd = node->data.block.commands[i];
                if (cmd->type == AST_UNQUOTE_SPLICING) {
                    syntax__compile_unquote_child(c, cmd->data.unquote_splicing.child);
                    /* US-012: validate splice operand before concat */
                    compiler__emit_byte(c, OP_SYNTAX_OP, line);
                    compiler__emit_byte(c, 18, line);  /* validate-unquote-splice */
                    compiler__emit_byte(c, OP_VEC_CONCAT, line);
                } else {
                    syntax__compile_sq_node(c, cmd);
                    compiler__emit_byte(c, OP_VEC_PUSH, line);
                }
            }
        } else {
            for (uint32_t i = 0; i < node->data.block.count; i++)
                syntax__compile_sq_node(c, node->data.block.commands[i]);
            compiler__emit_byte(c, OP_VEC, line);
            compiler__emit_byte(c, (uint8_t)node->data.block.count, line);
        }

        /* make-syntax block (commands-vec → syntax) */
        compiler__emit_byte(c, OP_SYNTAX_OP, line);
        compiler__emit_byte(c, 12, line);
        break;
    }

    case AST_UNQUOTE:
        /* Compile inner expression — must produce a syntax JaclVal at runtime */
        syntax__compile_unquote_child(c, node->data.unquote.child);
        /* US-012: validate that unquote result is a syntax object */
        compiler__emit_byte(c, OP_SYNTAX_OP, node->start.line);
        compiler__emit_byte(c, 17, node->start.line);  /* validate-unquote */
        break;

    case AST_SYNTAX_QUOTE:
        /* Nested syntax-quote: emit entire subtree as a constant */
        {
            JaclVal tmpl = syntax_from_ast(node, c->heap, c->intern_table);
            compiler__emit_constant(c, tmpl, line);
        }
        break;

    default:
        /* Other node types (spread, interp-string, etc.): fall back to constant */
        {
            JaclVal tmpl = syntax_from_ast(node, c->heap, c->intern_table);
            compiler__emit_constant(c, tmpl, line);
        }
        break;
    }
}

/* --- Internal: Compile a single AST node --- */

void compiler__compile_node(Compiler* c, AstNode* node) {
  uint32_t line = node->start.line;
  c->last_expr_type = TYPE_DYN;  /* default; specific cases override */

  /* Hygiene: every AST node carries its own scope mark (0 for hand-written
     code, non-zero for nodes stamped during macro expansion).  Switch to
     the node's mark while compiling this node so that name resolution
     (locals, upvalues) uses the binding context of the code that created
     this node — crucially, unquoted sub-nodes with mark 0 must look up
     names in the caller's scope even when nested inside a stamped
     template. */
  uint32_t prev_scope_mark = c->current_scope_mark;
  c->current_scope_mark = node->scope_mark;

  switch (node->type) {

    case AST_LIT_INT: {
      JaclType et = (JaclType)node->inferred_type;
      if (et == TYPE_I64) {
        int64_t v = (int64_t)node->data.lit_int.value;
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)(uint64_t)v);
        compiler__emit_byte(c, OP_CONST_I64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_I64;
      } else if (et == TYPE_U64) {
        uint64_t v = (uint64_t)(uint32_t)node->data.lit_int.value;
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)v);
        compiler__emit_byte(c, OP_CONST_U64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_U64;
      } else if (et == TYPE_F64) {
        double d = (double)node->data.lit_int.value;
        uint64_t raw;
        memcpy(&raw, &d, sizeof(raw));
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)raw);
        compiler__emit_byte(c, OP_CONST_F64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_F64;
      } else if (et == TYPE_U32) {
        compiler__emit_constant(c, jacl_u32((uint32_t)node->data.lit_int.value), line);
        c->last_expr_type = TYPE_U32;
      } else if (et == TYPE_F32) {
        compiler__emit_constant(c, jacl_f32((float)node->data.lit_int.value), line);
        c->last_expr_type = TYPE_F32;
      } else {
        /* Default: tagged i32 (works for expected TYPE_DYN or TYPE_I32) */
        compiler__emit_constant(c, jacl_i32(node->data.lit_int.value), line);
        c->last_expr_type = (et == TYPE_I32) ? TYPE_I32 : TYPE_I32;
      }
      break;
    }

    case AST_LIT_FLOAT: {
      JaclType et = (JaclType)node->inferred_type;
      if (et == TYPE_F64) {
        double d = (double)node->data.lit_float.value;
        uint64_t raw;
        memcpy(&raw, &d, sizeof(raw));
        uint16_t idx = chunk_add_constant(c->chunk, (JaclVal)raw);
        compiler__emit_byte(c, OP_CONST_F64, line);
        compiler__emit_u16(c, idx, line);
        c->last_expr_type = TYPE_F64;
      } else {
        /* Default: tagged f32 (works for expected TYPE_DYN or TYPE_F32) */
        compiler__emit_constant(c, jacl_f32(node->data.lit_float.value), line);
        c->last_expr_type = TYPE_F32;
      }
      break;
    }

    case AST_LIT_STRING: {
      uint32_t len = node->data.lit_string.length;
      JaclVal val = jacl_string_new(c->heap, c->intern_table,
                                     node->data.lit_string.value, (size_t)len);
      compiler__emit_constant(c, val, line);
      c->last_expr_type = TYPE_STR;
      break;
    }

    case AST_VAR_REF: {
      uint32_t name_len = node->data.var_ref.length;
      if (name_len > 128) {
        compiler__error(c, line, node->start.column,
                        "variable name exceeds 128-byte limit");
        break;
      }

      /* US-006: $ctx resolves to OP_GET_CTX — the implicit context struct */
      if (name_len == 3 && memcmp(node->data.var_ref.name, "ctx", 3) == 0) {
        CtxFieldList *ctx_fl = compiler__get_ctx_fields(c);
        if (ctx_fl && ctx_fl->count > 0) {
          compiler__emit_byte(c, OP_GET_CTX, line);
          c->last_expr_type = TYPE_STRUCT;
        } else {
          compiler__error(c, line, node->start.column,
                          "no ctx fields declared");
        }
        break;
      }

      JaclVal name_val = compiler__name_val(c->heap, c->intern_table, node->data.var_ref.name, name_len);

      /* SM mode: resolve variables from state object fields first */
      if (c->sm_analysis) {
        const StateField* sf = sm__get_field(&c->sm_analysis->state_layout, name_val);
        if (sf) {
          if (sf->struct_type_idx != 0 && !sf->is_mutable) {
            /* Struct state field: push N inline slots via WIDE op. */
            compiler__emit_byte(c, OP_GET_STATE_FIELD_WIDE, line);
            compiler__emit_byte(c, (uint8_t)sf->field_index, line);
            compiler__emit_byte(c, (uint8_t)sf->width, line);
            c->inline_repr = INLINE_STACK;
            c->last_expr_type = TYPE_STRUCT;
          } else {
            compiler__emit_byte(c, sf->is_mutable ? OP_GET_STATE_FIELD_CELL
                                                  : OP_GET_STATE_FIELD, line);
            compiler__emit_byte(c, (uint8_t)sf->field_index, line);
            c->last_expr_type = TYPE_DYN;
          }
          break;
        }
      }

      int local_slot = compiler__resolve_local(c, name_val);
      if (local_slot != -1) {
        if (c->locals[local_slot].is_mutable) {
          compiler__emit_byte(c, OP_GET_CELL_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)local_slot, line);
          /* Cells store boxed values; unbox if typed */
          JaclType local_type = c->locals[local_slot].type;
          if (is_unboxed_type(local_type)) {
            uint8_t to_op;
            switch (local_type) {
              case TYPE_I64: to_op = OP_TO_I64; break;
              case TYPE_U64: to_op = OP_TO_U64; break;
              case TYPE_F64: to_op = OP_TO_F64; break;
              default: to_op = 0; break;
            }
            if (to_op) {
              compiler__emit_byte(c, to_op, line);
              compiler__emit_byte(c, (uint8_t)TYPE_DYN, line);
            }
          }
        } else if (c->locals[local_slot].is_inline) {
          /* Inline struct local — load bytes onto TOS as N consecutive
             inline slots. Field access intercepts this in the [. ...]
             handler and uses OP_STRUCT_GET_INLINE directly against the
             local; this path is for when the whole struct value is needed. */
          compiler__emit_byte(c, OP_LOAD_INLINE_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)local_slot, line);
          compiler__emit_u16(c, (uint16_t)c->locals[local_slot].struct_type_idx, line);
          c->inline_repr = INLINE_STACK;
        } else {
          compiler__emit_byte(c, OP_GET_LOCAL, line);
          compiler__emit_byte(c, (uint8_t)local_slot, line);
        }
        c->last_expr_type = c->locals[local_slot].type;
      } else {
        int upvalue_idx = compiler__resolve_upvalue(c, name_val, line,
                                                     node->start.column);
        if (upvalue_idx != -1) {
          /* US-008: all upvalue opcodes use base_slot to index into the
             upvalue array, which accounts for wide struct upvalues. */
          uint8_t uv_base = (uint8_t)c->upvalues[upvalue_idx].base_slot;
          if (c->upvalues[upvalue_idx].is_mutable) {
            compiler__emit_byte(c, OP_GET_CELL_UPVALUE, line);
            compiler__emit_byte(c, uv_base, line);
            /* Cells store boxed values; unbox if typed */
            JaclType uv_type = c->upvalues[upvalue_idx].type;
            if (is_unboxed_type(uv_type)) {
              uint8_t to_op;
              switch (uv_type) {
                case TYPE_I64: to_op = OP_TO_I64; break;
                case TYPE_U64: to_op = OP_TO_U64; break;
                case TYPE_F64: to_op = OP_TO_F64; break;
                default: to_op = 0; break;
              }
              if (to_op) {
                compiler__emit_byte(c, to_op, line);
                compiler__emit_byte(c, (uint8_t)TYPE_DYN, line);
              }
            }
          } else if (c->upvalues[upvalue_idx].is_inline) {
            /* Inline struct upvalue — load bytes onto TOS as N inline slots. */
            compiler__emit_byte(c, OP_LOAD_INLINE_UPVALUE, line);
            compiler__emit_byte(c, (uint8_t)c->upvalues[upvalue_idx].base_slot, line);
            compiler__emit_u16(c, (uint16_t)c->upvalues[upvalue_idx].struct_type_idx, line);
            c->inline_repr = INLINE_STACK;
          } else {
            compiler__emit_byte(c, OP_GET_UPVALUE, line);
            compiler__emit_byte(c, uv_base, line);
          }
          c->last_expr_type = c->upvalues[upvalue_idx].type;
        } else {
          GlobalArity* ga = compiler__find_global(c, name_val);
          /* Prelude mode: reject names not in the prelude or source-defined globals */
          if (c->has_prelude && !ga) {
            char err_msg[160];
            snprintf(err_msg, sizeof(err_msg),
                     "undefined name '%.*s'", (int)name_len,
                     node->data.var_ref.name);
            compiler__error(c, line, node->start.column, err_msg);
            break;
          }
          JaclType global_type = ga ? ga->type : TYPE_DYN;
          JaclVal gkey = compiler__global_name_val(c,
              node->data.var_ref.name, name_len);
          uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
          compiler__emit_global_op(c, OP_GET_GLOBAL, name_idx, line);
          /* Unbox typed globals: globals store tagged JaclVal, convert to raw */
          if (is_unboxed_type(global_type)) {
            uint8_t to_op;
            switch (global_type) {
              case TYPE_I64: to_op = OP_TO_I64; break;
              case TYPE_U64: to_op = OP_TO_U64; break;
              case TYPE_F64: to_op = OP_TO_F64; break;
              default: to_op = 0; break;
            }
            if (to_op) {
              compiler__emit_byte(c, to_op, line);
              compiler__emit_byte(c, (uint8_t)TYPE_DYN, line);
            }
          }
          c->last_expr_type = ga ? ga->type : TYPE_DYN;
        }
      }
      break;
    }

    case AST_COMMAND: {
      compiler__compile_command(c, node);
      /* Statement-shaped heads pin last_expr_type to NIL — Stage 2
       * migrated downstream consumers off the leak. HEAD_DEF/HEAD_MUT
       * used to leak STRUCT after `def Point p ...` to drive inline
       * codegen; the def cluster now reads args[i]->inferred_*
       * directly, so pinning here closes that leak. */
      switch (node->data.command.head_id) {
        case HEAD_SET:
        case HEAD_COLON_COLON:
        case HEAD_FOR:
        case HEAD_DEF:
        case HEAD_MUT:
        case HEAD_WHILE:
        case HEAD_PRINT:
          c->last_expr_type = TYPE_NIL;
          break;
        default: break;
      }
      break;
    }

    case AST_BLOCK: {
      compiler__compile_block_expr(c, node);
      break;
    }

    case AST_INTERP_STRING: {
      uint32_t seg_count = node->data.interp_string.count;
      AstNode** segments = node->data.interp_string.segments;

      if (seg_count == 0) {
        /* Empty interpolated string → empty string constant */
        compiler__emit_constant(c, jacl_string_new(c->heap, c->intern_table, "", 0), line);
        c->last_expr_type = TYPE_STR;
        break;
      }

      /* Compile first segment */
      {
        AstNode* seg = segments[0];
        if (seg->type == AST_LIT_STRING) {
          compiler__compile_node(c, seg);
        } else {
          compiler__compile_node(c, seg);
          compiler__ensure_boxed(c, line);
          compiler__emit_byte(c, OP_TO_STRING, line);
        }
      }

      /* Compile remaining segments, each followed by OP_CONCAT */
      for (uint32_t i = 1; i < seg_count; i++) {
        AstNode* seg = segments[i];
        if (seg->type == AST_LIT_STRING) {
          compiler__compile_node(c, seg);
        } else {
          compiler__compile_node(c, seg);
          compiler__ensure_boxed(c, line);
          compiler__emit_byte(c, OP_TO_STRING, line);
        }
        compiler__emit_byte(c, OP_CONCAT, line);
      }
      c->last_expr_type = TYPE_STR;
      break;
    }

    case AST_USE: {
      /* Module import — compile the dependency module if not cached */
      const char* use_path = node->data.use_decl.path;

      /* Resolve path relative to the current module */
      const char* importer_path = c->current_module ? c->current_module->path : NULL;
      if (!importer_path) {
        /* No current module context — cannot resolve relative import */
        compiler__error(c, line, node->start.column,
                        "use declaration requires module context");
        break;
      }

      const char* canonical = module__resolve_path(importer_path, use_path, c->arena);
      if (!canonical) {
        char buf[256];
        snprintf(buf, sizeof(buf), "module not found: \"%s\"", use_path);
        char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
        memcpy(msg, buf, strlen(buf) + 1);
        compiler__error(c, line, node->start.column, msg);
        break;
      }

      /* Check circular import */
      if (c->import_stack && import_stack__contains(c->import_stack, canonical)) {
        const char* chain = import_stack__chain_str(c->import_stack, canonical,
                                                     c->arena);
        char buf[512];
        snprintf(buf, sizeof(buf), "circular import detected: %s", chain);
        char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
        memcpy(msg, buf, strlen(buf) + 1);
        compiler__error(c, line, node->start.column, msg);
        break;
      }

      /* Check cache — compile if not already compiled */
      Module* dep_mod = c->module_cache
                          ? module_cache__find(c->module_cache, canonical)
                          : NULL;
      if (!dep_mod) {
        /* Compile the dependency module */
        if (!compiler__compile_module(canonical, c, line, node->start.column)) {
          /* Error already reported by compile_module */
          break;
        }
        dep_mod = module_cache__find(c->module_cache, canonical);
        if (!dep_mod) {
          compiler__error(c, line, node->start.column,
                          "internal error: module not in cache after compile");
          break;
        }
      }

      if (node->data.use_decl.is_module_binding) {
        /* Module binding form: use "path" name
           Build a map of all exports at runtime and bind to a local variable.
           Track the module binding for compile-time -> resolution. */
        const char* binding_name = node->data.use_decl.binding_name;
        uint32_t binding_len = node->data.use_decl.binding_name_len;

        /* Check for conflict with existing local */
        JaclVal name_val = compiler__name_val(c->heap, c->intern_table,
                                               binding_name, binding_len);
        if (compiler__resolve_local(c, name_val) != -1) {
          char buf[256];
          snprintf(buf, sizeof(buf), "'%.*s' is already defined",
                   (int)binding_len, binding_name);
          char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
          memcpy(msg, buf, strlen(buf) + 1);
          compiler__error(c, line, node->start.column, msg);
          break;
        }

        /* Build dependency module prefix for looking up exports */
        uint32_t dep_prefix_len;
        const char* dep_prefix = module__build_prefix(
            dep_mod->path, c->arena, &dep_prefix_len);

        /* Count non-struct exports for map construction */
        uint32_t export_count = 0;
        for (uint32_t ei = 0; ei < dep_mod->export_count; ei++) {
          ExportEntry* exp = &dep_mod->exports[ei];
          /* Skip struct type exports (compile-time only) */
          if (!(exp->type == TYPE_STRUCT && exp->return_type == TYPE_STRUCT)) {
            export_count++;
          }
        }

        /* Push key-value pairs for each export, then emit OP_MAP */
        for (uint32_t ei = 0; ei < dep_mod->export_count; ei++) {
          ExportEntry* exp = &dep_mod->exports[ei];

          /* Skip struct type exports (compile-time only) */
          if (exp->type == TYPE_STRUCT && exp->return_type == TYPE_STRUCT) {
            continue;
          }

          /* Push key (export name as string) */
          JaclVal key_val = compiler__name_val(c->heap, c->intern_table,
                                                exp->name, exp->name_len);
          uint16_t key_idx = chunk_add_constant(c->chunk, key_val);
          compiler__emit_byte(c, OP_CONST, line);
          compiler__emit_u16(c, key_idx, line);

          /* Push value (get from dependency module's global) */
          char dep_buf[256];
          uint32_t dep_total = dep_prefix_len + exp->name_len;
          if (dep_total >= sizeof(dep_buf)) dep_total = sizeof(dep_buf) - 1;
          memcpy(dep_buf, dep_prefix, dep_prefix_len);
          memcpy(dep_buf + dep_prefix_len, exp->name,
                 dep_total - dep_prefix_len);
          dep_buf[dep_total] = '\0';
          JaclVal dep_key = jacl_intern(c->heap, c->intern_table,
                                         dep_buf, dep_total);
          uint16_t get_idx = chunk_add_constant(c->chunk, dep_key);
          compiler__emit_global_op(c, OP_GET_GLOBAL, get_idx, line);
        }

        /* Emit OP_MAP with the pair count */
        compiler__emit_byte(c, OP_MAP, line);
        compiler__emit_byte(c, (uint8_t)export_count, line);

        /* The map is now on the stack. Create a local binding for it. */
        compiler__add_local(c, name_val, line, node->start.column);
        c->locals[c->local_count - 1].type = TYPE_MAP;

        /* Track this as a module binding for compile-time resolution */
        if (c->module_binding_count < COMPILER_MODULE_BINDINGS_MAX) {
          ModuleBinding* mb = &c->module_bindings[c->module_binding_count++];
          mb->name = name_val;
          mb->local_slot = (int)(c->local_count - 1);
          mb->module = dep_mod;
        }

        /* use statement produces nil as its result value */
        compiler__emit_byte(c, OP_NIL, line);
        break;
      }

      /* Destructuring form: use "path" {name1, name2, ...}
         Import specific names into scope. */
      /* Register each imported name as a GlobalArity in this compiler */
      for (uint32_t ni = 0; ni < node->data.use_decl.name_count; ni++) {
        const char* imp_name  = node->data.use_decl.names[ni];
        uint32_t    imp_len   = node->data.use_decl.name_lens[ni];

        /* Find the name in the module's exports */
        ExportEntry* found_export = NULL;
        for (uint32_t ei = 0; ei < dep_mod->export_count; ei++) {
          if (dep_mod->exports[ei].name_len == imp_len &&
              memcmp(dep_mod->exports[ei].name, imp_name, imp_len) == 0) {
            found_export = &dep_mod->exports[ei];
            break;
          }
        }

        if (!found_export) {
          /* Check if it's a private name (underscore-prefixed) */
          if (imp_len > 0 && imp_name[0] == '_') {
            char buf[256];
            snprintf(buf, sizeof(buf), "cannot import private name '%.*s'",
                     (int)imp_len, imp_name);
            char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
            memcpy(msg, buf, strlen(buf) + 1);
            compiler__error(c, line, node->start.column, msg);
          } else {
            char buf[256];
            snprintf(buf, sizeof(buf), "'%.*s' is not exported by '%s'",
                     (int)imp_len, imp_name, use_path);
            char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
            memcpy(msg, buf, strlen(buf) + 1);
            compiler__error(c, line, node->start.column, msg);
          }
          continue;
        }

        /* Check for conflict with existing local or global definition */
        if (imp_len <= 128) {
          JaclVal name_val = compiler__name_val(c->heap, c->intern_table, imp_name, imp_len);

          /* Check conflict with existing global */
          GlobalArity* existing = compiler__find_global(c, name_val);
          if (existing) {
            char buf[256];
            snprintf(buf, sizeof(buf), "'%.*s' is already defined",
                     (int)imp_len, imp_name);
            char* msg = (char*)arena_alloc(c->arena, (uint32_t)(strlen(buf) + 1));
            memcpy(msg, buf, strlen(buf) + 1);
            compiler__error(c, line, node->start.column, msg);
            continue;
          }

          /* Register the import as a GlobalArity with full type info */
          if (c->global_arity_count < COMPILER_GLOBAL_ARITIES_MAX) {
            GlobalArity* ga = &c->global_arities[c->global_arity_count++];
            ga->name              = name_val;
            ga->known_arity       = found_export->arity;
            ga->is_mutable        = found_export->is_mutable;
            ga->suspends          = found_export->suspends;
            ga->captures_mutable  = false;
            ga->type              = found_export->type;
            ga->return_type       = found_export->return_type;
            memcpy(ga->param_types, found_export->param_types,
                   sizeof(ga->param_types));
          }

          /* Struct types are compile-time only (no runtime value).
             Skip runtime bytecode for struct constructors — the shared
             struct_registry handles constructor detection at compile time. */
          if (found_export->type == TYPE_STRUCT &&
              found_export->return_type == TYPE_STRUCT) {
            /* No runtime bytecode needed — struct constructor is
               resolved at compile time via struct_registry */
          } else {
            /* Build dependency module's prefixed name */
            uint32_t dep_prefix_len;
            const char* dep_prefix = module__build_prefix(
                dep_mod->path, c->arena, &dep_prefix_len);
            char dep_buf[256];
            uint32_t dep_total = dep_prefix_len + imp_len;
            if (dep_total >= sizeof(dep_buf)) dep_total = sizeof(dep_buf) - 1;
            memcpy(dep_buf, dep_prefix, dep_prefix_len);
            memcpy(dep_buf + dep_prefix_len, imp_name,
                   dep_total - dep_prefix_len);
            dep_buf[dep_total] = '\0';
            JaclVal dep_key = jacl_intern(c->heap, c->intern_table,
                                           dep_buf, dep_total);
            uint16_t get_idx = chunk_add_constant(c->chunk, dep_key);
            compiler__emit_global_op(c, OP_GET_GLOBAL, get_idx, line);

            /* Define under importing module's prefixed name */
            JaclVal self_key = compiler__global_name_val(c, imp_name, imp_len);
            uint16_t def_idx = chunk_add_constant(c->chunk, self_key);
            compiler__emit_byte(c, OP_DEF_GLOBAL, line);
            compiler__emit_u16(c, def_idx, line);
            /* Pop the nil pushed by OP_DEF_GLOBAL */
            compiler__emit_byte(c, OP_POP, line);
          } /* end else (non-struct runtime bytecode) */
        }
      }
      /* use statement produces nil as its result value */
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_DEFSTRUCT: {
      const char* struct_name     = node->data.defstruct.name;
      uint32_t    struct_name_len = node->data.defstruct.name_len;
      uint32_t    field_count     = node->data.defstruct.field_count;

      /* Must be at top level (scope_depth == 0) — parser already rejects
         defstruct inside blocks, but double-check */
      if (c->scope_depth > 0) {
        compiler__error(c, line, node->start.column,
                        "defstruct must appear at top level");
        break;
      }

      /* Get or create struct registry on the root compiler */
      Compiler* root = c;
      while (root->enclosing) root = root->enclosing;
      if (!root->struct_registry) {
        root->struct_registry = (StructTypeRegistry*)arena_alloc(
            root->arena, sizeof(StructTypeRegistry));
        struct_registry__init(root->struct_registry, root->arena);
      }
      StructTypeRegistry* reg = root->struct_registry;

      /* Error on duplicate struct name */
      if (struct_registry__find(reg, struct_name, struct_name_len) != UINT32_MAX) {
        char err[128];
        snprintf(err, sizeof(err), "duplicate struct definition '%.*s'",
                 (int)struct_name_len, struct_name);
        compiler__error(c, line, node->start.column, err);
        break;
      }

      /* Ensure capacity for the new type */
      if (!struct_registry__grow(reg)) {
        compiler__error(c, line, node->start.column,
                        "struct registry allocation failure");
        break;
      }

      /* Reserve slot first so inline struct registration doesn't overwrite it.
         We'll assign the actual StructTypeDef* after parsing fields. */
      uint32_t this_idx = reg->count;
      reg->count++;
      reg->defs[this_idx] = NULL; /* placeholder */

      /* Parse fields into temporary stack array (FAM requires knowing count upfront) */
      StructTypeField tmp_fields[256];
      uint32_t offset = 0;
      uint32_t max_align = 1;
      bool has_error = false;

      for (uint32_t fi = 0; fi < field_count; fi++) {
        const char* fname     = node->data.defstruct.field_names[fi];
        uint32_t    fname_len = node->data.defstruct.field_name_lens[fi];
        const char* ftype_str = node->data.defstruct.field_types[fi];
        uint32_t    ftype_len = node->data.defstruct.field_type_lens[fi];

        /* Check for duplicate field names */
        for (uint32_t j = 0; j < fi; j++) {
          if (tmp_fields[j].name_len == fname_len &&
              memcmp(tmp_fields[j].name, fname, fname_len) == 0) {
            char err[128];
            snprintf(err, sizeof(err), "duplicate field name '%.*s' in struct '%.*s'",
                     (int)fname_len, fname, (int)struct_name_len, struct_name);
            compiler__error(c, line, node->start.column, err);
            has_error = true;
            break;
          }
        }
        if (has_error) break;

        /* Resolve field type */
        JaclType ftype = TYPE_DYN;
        uint32_t f_struct_idx = 0;

        if (is_type_keyword(ftype_str, ftype_len)) {
          ftype = type_from_keyword(ftype_str, ftype_len);
        } else if (ftype_len > 7 && memcmp(ftype_str, "struct{", 7) == 0) {
          /* Inline anonymous struct type */
          uint32_t idx = compiler__register_inline_struct(reg, ftype_str, ftype_len);
          if (idx == UINT32_MAX) {
            char err[128];
            snprintf(err, sizeof(err),
                     "invalid inline struct type for field '%.*s' in struct '%.*s'",
                     (int)fname_len, fname,
                     (int)struct_name_len, struct_name);
            compiler__error(c, line, node->start.column, err);
            has_error = true;
            break;
          }
          ftype = TYPE_STRUCT;
          f_struct_idx = idx;
        } else {
          /* Check if it's a named struct type */
          uint32_t idx = struct_registry__find(reg, ftype_str, ftype_len);
          if (idx == UINT32_MAX) {
            char err[128];
            snprintf(err, sizeof(err),
                     "undefined type '%.*s' for field '%.*s' in struct '%.*s'",
                     (int)ftype_len, ftype_str,
                     (int)fname_len, fname,
                     (int)struct_name_len, struct_name);
            compiler__error(c, line, node->start.column, err);
            has_error = true;
            break;
          }
          ftype = TYPE_STRUCT;
          f_struct_idx = idx;
        }

        /* Reject reference field types — structs hold value-type bytes only.
           Use [box $val] to reference data through a struct. */
        if (!is_struct_value_type(ftype)) {
          char err[192];
          snprintf(err, sizeof(err),
                   "struct field '%.*s' has reference type '%s' — "
                   "struct fields must be value types (primitives or nested structs); "
                   "use [box $val] to hold a reference",
                   (int)fname_len, fname, type_name(ftype));
          compiler__error(c, line, node->start.column, err);
          has_error = true;
          break;
        }

        /* Compute C-ABI layout */
        uint32_t fsize  = struct__type_size(ftype, reg, f_struct_idx);
        uint32_t falign = struct__type_align(ftype, reg, f_struct_idx);
        offset = struct__align_up(offset, falign);

        tmp_fields[fi].name           = fname;
        tmp_fields[fi].name_len       = fname_len;
        tmp_fields[fi].type           = ftype;
        tmp_fields[fi].struct_type_idx = f_struct_idx;
        tmp_fields[fi].offset         = offset;
        tmp_fields[fi].size           = fsize;
        tmp_fields[fi].is_mutable     = node->data.defstruct.field_mutable[fi] != 0;
        tmp_fields[fi].default_val    = JACL_NIL;

        offset += fsize;
        if (falign > max_align) max_align = falign;
      }

      if (has_error) {
        reg->count = this_idx; /* rollback slot reservation */
        break;
      }

      /* Allocate StructTypeDef with exact field count in the registry arena */
      StructTypeDef* sdef = struct_registry__alloc_def(reg, field_count);
      if (!sdef) {
        reg->count = this_idx;
        compiler__error(c, line, node->start.column,
                        "struct registry allocation failure");
        break;
      }
      sdef->name     = struct_name;
      sdef->name_len = struct_name_len;
      if (struct_name_len <= 128) {
        sdef->name_val = compiler__name_val(c->heap, c->intern_table, struct_name, struct_name_len);
      } else {
        sdef->name_val = JACL_NIL;
      }
      sdef->field_count = field_count;
      sdef->total_size  = struct__align_up(offset, max_align);
      sdef->alignment   = max_align;
      /* Ref fields were already rejected at the per-field check above, so
         all fields here are value types — no extra flag needed. */
      memcpy(sdef->fields, tmp_fields, field_count * sizeof(StructTypeField));

      /* Assign to reserved slot */
      reg->defs[this_idx] = sdef;

      /* Register struct name as a global with arity = field_count (constructor)
         and type = TYPE_STRUCT */
      if (struct_name_len <= 128) {
        JaclVal name_val = sdef->name_val;
        compiler__set_global_arity(root, name_val, (int16_t)field_count);
        GlobalArity* ga = compiler__find_global(root, name_val);
        if (ga) {
          ga->type = TYPE_STRUCT;
          ga->return_type = TYPE_STRUCT;
        }
      }

      /* defstruct is a declaration, produces nil */
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_DEFMACRO: {
      uint32_t col = node->start.column;

      if (c->scope_depth > 0) {
        compiler__error(c, line, col, "defmacro must appear at top level");
        break;
      }

      const char* mname     = node->data.defmacro.name;
      uint32_t    mname_len = node->data.defmacro.name_len;

      /* Error: shadowing a special form */
      if (macro__is_special_form(mname, mname_len)) {
        char err[128];
        snprintf(err, sizeof(err),
                 "defmacro: '%.*s' shadows a special form",
                 (int)mname_len, mname);
        compiler__error(c, line, col, err);
        break;
      }

      /* Find the root compiler's macro table */
      MacroTable* mt = c->macro_table;
      if (!mt) {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        mt = root->macro_table;
      }

      if (mt) {
        /* Check if already registered by expansion pass */
        {
          MacroEntry* existing = macro_table_lookup(mt, mname, mname_len);
          if (existing) {
            if (existing->closure != NULL) {
              /* Macro body already compiled during expansion —
                 skip recompilation, just emit OP_NIL. */
              compiler__emit_byte(c, OP_NIL, line);
              break;
            }
            /* Registered by expansion pass (closure is NULL).
               Compile the body to fill in the closure. */
            uint32_t param_count = node->data.defmacro.param_count;

            JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena,
                                      sizeof(JaclClosure));
            chunk_init(&closure->chunk, c->arena);
            closure->upvalue_count  = 0;
            closure->upvalues       = NULL;
            closure->param_count    = (uint8_t)param_count;
            closure->param_total_slots = (uint8_t)param_count; /* macros never have struct params */
            closure->min_args       = node->data.defmacro.variadic
                                        ? (uint8_t)(param_count > 0 ? param_count - 1 : 0)
                                        : (uint8_t)param_count;
            closure->variadic       = node->data.defmacro.variadic;
            closure->pinned         = false;
            closure->pin_worker_id  = -1;
            closure->is_generator   = false;
            closure->is_sm_compiled = false;
            closure->sm_field_count = 0;

            char* name_copy2 = (char*)arena_alloc(c->arena, mname_len + 1);
            memcpy(name_copy2, mname, mname_len);
            name_copy2[mname_len] = '\0';
            closure->name = name_copy2;

            if (param_count > 0) {
              closure->param_names = (JaclVal*)arena_alloc(c->arena,
                                      sizeof(JaclVal) * param_count);
              for (uint32_t pi = 0; pi < param_count; pi++) {
                closure->param_names[pi] = compiler__name_val(c->heap, c->intern_table,
                    node->data.defmacro.param_names[pi],
                    node->data.defmacro.param_name_lens[pi]);
              }
            } else {
              closure->param_names = NULL;
            }

            Compiler body_compiler;
            compiler__init(&body_compiler, &closure->chunk, c->arena,
                           c->intern_table, c->heap);
            body_compiler.scope_depth = 1;
            body_compiler.enclosing   = c;
            body_compiler.macro_table = mt;
            body_compiler.current_scope_mark = c->current_scope_mark;

            for (uint32_t pi = 0; pi < param_count; pi++) {
              compiler__add_local(&body_compiler, closure->param_names[pi],
                                  line, col);
              body_compiler.locals[body_compiler.local_count - 1].is_param = true;
            }

            compiler__compile_block_expr(&body_compiler,
                                          node->data.defmacro.body);
            compiler__emit_byte(&body_compiler, OP_RETURN, line);

            c->error_count += body_compiler.error_count;
            if (!c->first_error && body_compiler.first_error) {
              c->first_error = body_compiler.first_error;
            }

            closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;
            existing->closure = closure;

            compiler__emit_byte(c, OP_NIL, line);
            break;
          }
        }

        if (mt->count >= MACRO_TABLE_MAX) {
          compiler__error(c, line, col, "too many macro definitions");
          break;
        }

        /* Compile macro body block to a closure */
        uint32_t param_count = node->data.defmacro.param_count;

        JaclClosure* closure = (JaclClosure*)arena_alloc(c->arena, sizeof(JaclClosure));
        chunk_init(&closure->chunk, c->arena);
        closure->upvalue_count  = 0;
        closure->upvalues       = NULL;
        closure->param_count    = (uint8_t)param_count;
        closure->param_total_slots = (uint8_t)param_count; /* macros never have struct params */
        closure->min_args       = node->data.defmacro.variadic
                                    ? (uint8_t)(param_count > 0 ? param_count - 1 : 0)
                                    : (uint8_t)param_count;
        closure->variadic       = node->data.defmacro.variadic;
        closure->pinned         = false;
        closure->pin_worker_id  = -1;
        closure->is_generator   = false;
        closure->is_sm_compiled = false;
        closure->sm_field_count = 0;

        /* Copy macro name to arena */
        char* name_copy = (char*)arena_alloc(c->arena, mname_len + 1);
        memcpy(name_copy, mname, mname_len);
        name_copy[mname_len] = '\0';
        closure->name = name_copy;

        /* Set up param_names as inline strings */
        if (param_count > 0) {
          closure->param_names = (JaclVal*)arena_alloc(c->arena,
                                    sizeof(JaclVal) * param_count);
          for (uint32_t i = 0; i < param_count; i++) {
            closure->param_names[i] = compiler__name_val(c->heap, c->intern_table,
                node->data.defmacro.param_names[i],
                node->data.defmacro.param_name_lens[i]);
          }
        } else {
          closure->param_names = NULL;
        }

        /* Create body compiler */
        Compiler body_compiler;
        compiler__init(&body_compiler, &closure->chunk, c->arena,
                       c->intern_table, c->heap);
        body_compiler.scope_depth = 1;
        body_compiler.enclosing   = c;
        body_compiler.macro_table = mt;
        body_compiler.current_scope_mark = c->current_scope_mark;

        /* Add params as locals */
        for (uint32_t i = 0; i < param_count; i++) {
          compiler__add_local(&body_compiler, closure->param_names[i], line, col);
          body_compiler.locals[body_compiler.local_count - 1].is_param = true;
        }

        /* Compile body block */
        compiler__compile_block_expr(&body_compiler, node->data.defmacro.body);
        compiler__emit_byte(&body_compiler, OP_RETURN, line);

        /* Propagate errors */
        c->error_count += body_compiler.error_count;
        if (!c->first_error && body_compiler.first_error) {
          c->first_error = body_compiler.first_error;
        }

        closure->upvalue_count = (uint8_t)body_compiler.upvalue_count;

        /* Register in macro table */
        MacroEntry* entry = &mt->entries[mt->count++];
        entry->name           = name_copy;
        entry->name_len       = mname_len;
        entry->param_count    = param_count;
        entry->variadic       = node->data.defmacro.variadic;
        entry->param_names    = node->data.defmacro.param_names;
        entry->param_name_lens = node->data.defmacro.param_name_lens;
        entry->closure        = closure;
      }

      /* defmacro is a declaration, produces nil */
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_QUOTE: {
      /* Convert the child AstNode to a syntax object at compile time,
       * then emit it as a constant. At runtime the quote pushes the
       * pre-built syntax object onto the stack. */
      JaclVal syn_val = syntax_from_ast(node->data.quote.child,
                                         c->heap, c->intern_table);
      compiler__emit_constant(c, syn_val, line);
      break;
    }

    case AST_SYNTAX_QUOTE: {
      AstNode *child = node->data.syntax_quote.child;
      /* Emit make-syntax ops to build the syntax tree bottom-up at runtime.
       * Each subtree is constructed via OP_SYNTAX_OP subops 7-12;
       * unquotes compile their inner expressions normally. */
      syntax__compile_sq_node(c, child);
      break;
    }

    case AST_UNQUOTE: {
      compiler__error(c, line, node->start.column,
                      "'~' (unquote) can only appear inside syntax-quote");
      break;
    }

    case AST_UNQUOTE_SPLICING: {
      compiler__error(c, line, node->start.column,
                      "'~@' (unquote-splicing) can only appear inside syntax-quote");
      break;
    }

    case AST_BREAK: {
      if (c->loop_depth == 0) {
        compiler__error(c, line, node->start.column,
                        "break outside of loop");
        break;
      }
      LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
      /* Compile break value (or nil) */
      if (node->data.break_stmt.value) {
        compiler__compile_node(c, node->data.break_stmt.value);
      } else {
        compiler__emit_byte(c, OP_NIL, line);
      }
      /* For inlined for-loops, clean up hidden locals under the break value */
      if (lctx->is_for_loop) {
        uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
        if (cleanup > 0) {
          compiler__emit_byte(c, OP_CLOSE_LOOP, line);
          compiler__emit_byte(c, (uint8_t)cleanup, line);
        }
      }
      /* Emit forward jump to be patched at loop exit */
      if (lctx->break_patch_count < COMPILER_BREAK_PATCHES_MAX) {
        lctx->break_patches[lctx->break_patch_count++] =
            compiler__emit_jump(c, OP_JUMP, line);
      } else {
        compiler__error(c, line, node->start.column,
                        "too many break statements in loop");
      }
      /* break compiles as a non-returning jump; the typer's view is
       * nil (the surrounding loop's result type). Match it here. */
      c->last_expr_type = TYPE_NIL;
      break;
    }

    case AST_CONTINUE: {
      if (c->loop_depth == 0) {
        compiler__error(c, line, node->start.column,
                        "continue outside of loop");
        break;
      }
      LoopContext* lctx = &c->loop_stack[c->loop_depth - 1];
      if (lctx->is_for_loop) {
        /* For-loop: pop any body-declared locals before forward-jumping
         * to the continue landing. body_local_count is the snapshot
         * AFTER iter-state locals (so we leave init/__col/elem alone)
         * but BEFORE body locals (so `def`/`mut` inside the body get
         * cleaned up). */
        uint32_t cleanup = c->local_count - lctx->body_local_count;
        if (cleanup > 0) {
          compiler__emit_byte(c, OP_POP_N, line);
          compiler__emit_byte(c, (uint8_t)cleanup, line);
        }
        if (lctx->continue_patch_count < COMPILER_CONTINUE_PATCHES_MAX) {
          lctx->continue_patches[lctx->continue_patch_count++] =
              compiler__emit_jump(c, OP_JUMP, line);
        } else {
          compiler__error(c, line, node->start.column,
                          "too many continue statements in loop");
        }
      } else {
        /* While-loop: pop body-scope locals, then backward-jump to condition */
        uint32_t cleanup = c->local_count - lctx->local_count_at_loop;
        if (cleanup > 0) {
          compiler__emit_byte(c, OP_POP_N, line);
          compiler__emit_byte(c, (uint8_t)cleanup, line);
        }
        compiler__emit_byte(c, OP_LOOP, line);
        uint32_t offset = c->chunk->code_count - lctx->loop_start + 2;
        compiler__emit_byte(c, (uint8_t)((offset >> 8) & 0xFF), line);
        compiler__emit_byte(c, (uint8_t)(offset & 0xFF), line);
      }
      c->last_expr_type = TYPE_NIL;
      break;
    }

    case AST_RETURN: {
      if (c->has_yield && node->data.return_stmt.value) {
        compiler__error(c, node->start.line, node->start.column,
            "cannot return a value from a generator (proc contains `yield`); "
            "stream consumers discard it. Use bare `return` for early exit.");
        c->last_expr_type = TYPE_NIL;
        break;
      }
      /* Compile return value (or nil) */
      if (node->data.return_stmt.value) {
        compiler__compile_node(c, node->data.return_stmt.value);
        /* Phase 5b: wide return for struct-returning procs */
        compiler__emit_return(c, line);
      } else {
        compiler__emit_byte(c, OP_NIL, line);
        compiler__emit_byte(c, OP_RETURN, line);
      }
      /* return is unreachable past this point — pin nil for audit
       * agreement with the typer. The proc-body return-type check
       * reads the AST tail directly (not last_expr_type), so this
       * is safe. */
      c->last_expr_type = TYPE_NIL;
      break;
    }

    case AST_DESTRUCTURE_VEC: {
      compiler__error(c, line, node->start.column,
                      "destructuring pattern can only appear in def or mut");
      break;
    }

    case AST_DESTRUCTURE_NAMED: {
      compiler__error(c, line, node->start.column,
                      "destructuring pattern can only appear in def or mut");
      break;
    }

    case AST_SHELL_CMD: {
      /* Shell command: !cmd args... → OP_VEC + OP_EXEC
       * Compiles to a vector of [head, arg1, arg2, ...] then OP_EXEC.
       * US-014: Supports spread args via OP_VEC_SPREAD.
       * In prelude/sandbox mode, `exec` must be available.
       * If exec is a custom closure (not native fn ref), downgrade to call. */
      AstNode* head = node->data.shell_cmd.head;
      uint32_t argc = node->data.shell_cmd.arg_count;
      AstNode** args = node->data.shell_cmd.args;
      uint32_t col  = node->start.column;

      /* In prelude mode, check that `exec` is available and track if native */
      int use_direct_opcode = 1;  /* Default: emit OP_EXEC directly */
      if (c->has_prelude) {
        JaclVal exec_name = compiler__name_val(c->heap, c->intern_table, "exec", 4);
        GlobalArity* ga = compiler__find_global(c, exec_name);
        if (!ga) {
          compiler__error(c, line, col, "exec not available");
          break;
        }
        /* If exec is a custom closure, we need to downgrade to a call */
        if (!ga->prelude_is_native_fn) {
          use_direct_opcode = 0;
        }
      }

      /* If downgrading to call, emit OP_GET_GLOBAL first so exec fn is under args */
      if (!use_direct_opcode) {
        JaclVal gkey = compiler__global_name_val(c, "exec", 4);
        uint16_t name_idx = chunk_add_constant(c->chunk, gkey);
        compiler__emit_global_op(c, OP_GET_GLOBAL, name_idx, line);
      }

      /* US-014: Check if any args are spread */
      int has_spread = 0;
      for (uint32_t i = 0; i < argc; i++) {
        if (args[i]->type == AST_SPREAD) {
          has_spread = 1;
          break;
        }
      }

      /* Compile head (command name) as first element - always fixed */
      compiler__compile_node(c, head);
      compiler__ensure_boxed(c, line);

      if (has_spread) {
        /* US-014: Spread support - use OP_VEC_SPREAD */
        uint8_t fixed_args = 1;  /* head is always fixed */
        uint8_t num_spreads = 0;

        for (uint32_t i = 0; i < argc; i++) {
          if (args[i]->type == AST_SPREAD) {
            compiler__compile_node(c, args[i]->data.spread.expr);
            compiler__emit_byte(c, OP_SPREAD, line);
            num_spreads++;
          } else {
            compiler__compile_node(c, args[i]);
            compiler__ensure_boxed(c, line);
            fixed_args++;
          }
        }

        compiler__emit_byte(c, OP_VEC_SPREAD, line);
        compiler__emit_byte(c, fixed_args, line);
        compiler__emit_byte(c, num_spreads, line);
      } else {
        /* No spread - use simpler OP_VEC */
        for (uint32_t i = 0; i < argc; i++) {
          compiler__compile_node(c, args[i]);
          compiler__ensure_boxed(c, line);
        }

        /* Total elements = 1 (head) + argc */
        uint32_t total_elems = 1 + argc;
        if (total_elems > 255) {
          compiler__error(c, line, col, "too many arguments to shell command");
          break;
        }

        /* Build vector from stack elements */
        compiler__emit_byte(c, OP_VEC, line);
        compiler__emit_byte(c, (uint8_t)total_elems, line);
      }

      if (use_direct_opcode) {
        /* Native exec: use direct opcode with flags byte */
        compiler__emit_byte(c, OP_EXEC, line);
        if (node->data.shell_cmd.background) {
          compiler__emit_byte(c, EXEC_FLAG_BG, line);
          c->last_expr_type = TYPE_DYN;  /* Returns a Job map */
        } else {
          compiler__emit_byte(c, 0, line);  /* Basic mode: flags = 0 */
          c->last_expr_type = TYPE_STREAM;
        }
      } else {
        /* Custom exec closure: call it with the args vector */
        compiler__emit_byte(c, OP_CALL, line);
        compiler__emit_byte(c, 1, line);  /* 1 argument: the args vector */
        c->last_expr_type = TYPE_DYN;
      }
      break;
    }

    case AST_SPREAD: {
      compiler__error(c, line, node->start.column,
                      "spread expression can only appear inside command arguments");
      break;
    }

    case AST_CTX_DECL: {
      /* ctx [mut] Type name = default_expr — collect field into ctx field list.
         Evaluate compile-time constant default to JaclVal for runtime init. */
      {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        if (c->scope_depth > 0 && !root->ctx_pre_registered) {
          compiler__error(c, line, node->start.column,
                          "ctx declarations must be at module top level");
          break;
        }
      }

      const char* type_name     = node->data.ctx_decl.type_name;
      uint32_t    type_name_len = node->data.ctx_decl.type_name_len;
      const char* field_name    = node->data.ctx_decl.field_name;
      uint32_t    field_name_len = node->data.ctx_decl.field_name_len;
      bool        is_mutable    = node->data.ctx_decl.is_mutable != 0;

      /* Resolve field type */
      JaclType ftype = TYPE_DYN;
      uint32_t f_struct_idx = 0;

      if (is_type_keyword(type_name, type_name_len)) {
        ftype = type_from_keyword(type_name, type_name_len);
      } else {
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        uint32_t idx = struct_registry__find(reg, type_name, type_name_len);
        if (idx == UINT32_MAX) {
          char err[128];
          snprintf(err, sizeof(err), "undefined type '%.*s' for ctx field '%.*s'",
                   (int)type_name_len, type_name,
                   (int)field_name_len, field_name);
          compiler__error(c, line, node->start.column, err);
          break;
        }
        ftype = TYPE_STRUCT;
        f_struct_idx = idx;
      }

      /* Get ctx field list from root compiler */
      CtxFieldList* ctx = compiler__get_ctx_fields(c);
      if (!ctx) {
        compiler__error(c, line, node->start.column,
                        "internal error: ctx field list not initialized");
        break;
      }

      /* Check for duplicate field name.  When top-level code suspends, ctx
         fields are pre-registered (Phase 1c) so procs compiled in Phase 2
         can resolve them.  In that case, skip the add but still emit bytecode. */
      bool already_registered = ctx_field_list__has(ctx, field_name, field_name_len);
      Compiler* root_c = c;
      while (root_c->enclosing) root_c = root_c->enclosing;
      if (already_registered && !root_c->ctx_pre_registered) {
        char err[128];
        snprintf(err, sizeof(err), "ctx field '%.*s' already declared",
                 (int)field_name_len, field_name);
        compiler__error(c, line, node->start.column, err);
        break;
      }

      if (!already_registered) {
        JaclVal def_val = ctx_eval_const_default(node->data.ctx_decl.default_expr, ftype);

        /* Add to ctx field list */
        if (!ctx_field_list__add(ctx, field_name, field_name_len,
                                 type_name, type_name_len,
                                 ftype, f_struct_idx, is_mutable,
                                 compiler__get_struct_registry(c), def_val)) {
          compiler__error(c, line, node->start.column,
                          "too many ctx fields (max 64)");
          break;
        }
      }

      /* Emit initialization bytecode. Struct fields write inline bytes
         directly into ctx.data via OP_HEAP_RECORD_SET_INLINE; primitive
         and reference fields use OP_HEAP_RECORD_SET. */
      if (node->data.ctx_decl.default_expr) {
        CtxField* added = ctx_field_list__find(ctx, field_name, field_name_len);
        compiler__emit_byte(c, OP_GET_CTX, line);
        compiler__compile_node(c, node->data.ctx_decl.default_expr);
        if (added->type == TYPE_STRUCT) {
          compiler__emit_byte(c, OP_HEAP_RECORD_SET_INLINE, line);
          compiler__emit_u16(c, (uint16_t)added->offset, line);
          compiler__emit_u16(c, (uint16_t)added->struct_type_idx, line);
        } else {
          compiler__emit_byte(c, OP_HEAP_RECORD_SET, line);
          compiler__emit_u16(c, (uint16_t)added->offset, line);
          compiler__emit_byte(c, (uint8_t)added->type, line);
        }
        /* SET leaves the heap record on stack — pop it, push nil. */
        compiler__emit_byte(c, OP_POP, line);
      }
      compiler__emit_byte(c, OP_NIL, line);
      break;
    }

    case AST_ERROR: {
      compiler__error(c, line, node->start.column, "parse error in AST");
      break;
    }
  }

  c->current_scope_mark = prev_scope_mark;
}

/* --- Public API --- */

/**
 * Check if any top-level statement is suspending (uses await/parallel/race
 * or calls a suspending proc). Used to decide if top-level SM wrapping is needed.
 */
bool compiler__top_level_suspends(AstNode** stmts, uint32_t count,
                                          SuspensionMap* map,
                                          ThreadHeap* heap,
                                          JaclInternTable* intern_table) {
  for (uint32_t i = 0; i < count; i++) {
    if (ast__contains_suspension(stmts[i], map, heap, intern_table)) return true;
  }
  return false;
}

/* Check if any AST node requires macro expansion (defmacro or \ command). */
/* Names of prelude macros that need to trigger macro expansion when used as
 * a command head. The list is auto-generated from src/prelude.jacl by
 * build.sh — adding a `defmacro NAME ...` to the prelude automatically
 * makes user invocations of NAME route through the expander. Adding a
 * prelude macro that would miscompile if expanded (e.g. one whose name
 * collides with a parser-level form like `=` or `:`) is now caller-beware
 * at the prelude-author level — see prelude.jacl for the note about why
 * binding/pipe operators must not be defined as macros. */
#include "prelude_macro_names.h"

static bool compiler__head_is_prelude_macro(const char *s, uint32_t len) {
  for (size_t i = 0; i < JACL_PRELUDE_MACRO_NAMES_COUNT; i++) {
    if (jacl_prelude_macro_names[i].len == len &&
        memcmp(jacl_prelude_macro_names[i].name, s, len) == 0) {
      return true;
    }
  }
  return false;
}

static bool compiler__node_needs_expansion__pred(AstNode *node, void *vctx) {
  (void)vctx;
  if (!node) return false;
  if (node->type == AST_DEFMACRO) return true;
  if (node->type == AST_COMMAND) {
    AstNode *head = node->data.command.head;
    if (head && head->type == AST_LIT_STRING
        && compiler__head_is_prelude_macro(head->data.lit_string.value,
                                           head->data.lit_string.length))
      return true;
    if (compiler__node_needs_expansion__pred(head, NULL)) return true;
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      if (compiler__node_needs_expansion__pred(node->data.command.args[i], NULL))
        return true;
    }
    return false;
  }
  return ast__any_child(node, compiler__node_needs_expansion__pred, NULL);
}

static bool compiler__node_needs_expansion(AstNode *node) {
  return compiler__node_needs_expansion__pred(node, NULL);
}

static bool compiler__needs_expansion(AstNode **nodes, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    if (compiler__node_needs_expansion(nodes[i])) return true;
  }
  return false;
}

CompileResult compiler_compile(ParseResult parse, arena_t* arena,
                                      JaclInternTable* intern_table,
                                      ThreadHeap* heap,
                                      StructTypeRegistry* seed_registry,
                                      ExpandState* es,
                                      JaclVal prelude_map) {
  CompileResult result;
  chunk_init(&result.chunk, arena);
  result.error_count   = parse.error_count;
  result.error_message = NULL;
  result.suspending    = false;
  result.macro_table   = NULL;

  /* Compiler init — suspension analysis runs after macro expansion below,
     so that macros expanding to suspending forms (e.g. `timeout` → `race`)
     mark the surrounding proc as suspending and trigger SM compilation. */
  SuspensionMap suspension_map;
  memset(&suspension_map, 0, sizeof(suspension_map));

  Compiler c;
  compiler__init(&c, &result.chunk, arena, intern_table, heap);
  c.suspension_map = &suspension_map;

  /* Prelude map: seed compile-time globals from caller-supplied map keys.
   * When a prelude is active, unresolved names produce compile errors
   * (closed-world assumption — only prelude + source-defined names are valid). */
  if (prelude_map != JACL_NIL && jacl_is_map(prelude_map)) {
    c.has_prelude = true;
    jacl_map_node* pmap = (jacl_map_node*)jacl_as_ptr(prelude_map);
    jacl_map_iter pit = jacl_map_iter_init(pmap);
    jacl_map_iter_result pir;
    for (;;) {
      pir = jacl_map_next_leaf(&pit);
      if (pir.done) break;
      JaclVal key = jacl_map_key_from_leaf(pir.item);
      if (!jacl_is_string(key)) {
        result.error_count++;
        result.error_message = "prelude map key must be a string";
        continue;
      }
      uint32_t klen = jacl_string_byte_len(key);
      if (klen == 0) {
        result.error_count++;
        result.error_message = "prelude map key cannot be empty";
        continue;
      }
      if (klen > 128) {
        result.error_count++;
        result.error_message = "prelude map key exceeds 128-byte limit";
        continue;
      }
      /* Reserved keys (starting with ':') are NOT registered as names —
       * reserved for future config flags (e.g. :core). */
      char kbuf[129];
      const char *kptr = jacl_string_flat_ptr(key, kbuf, sizeof(kbuf));
      if (kptr[0] == ':') continue;
      JaclVal name_val = compiler__name_val(heap, intern_table, kptr, klen);
      JaclVal value = jacl_map_value_from_leaf(pir.item);
      compiler__set_global_arity(&c, name_val, -1);
      compiler__set_global_prelude_native_fn(&c, name_val, jacl_is_native_fn(value));
    }
    /* Check for :shell-fallback config flag (REPL mode: unknown commands try PATH) */
    JaclVal sf_key = compiler__name_val(heap, intern_table, ":shell-fallback", 15);
    JaclVal sf_val = jacl_map_get(pmap, sf_key);
    if (sf_val != JACL_NIL && jacl_is_bool(sf_val) && jacl_as_bool(sf_val)) {
      c.shell_fallback = true;
    }
  }
  {
    if (seed_registry) {
      /* Use the seed registry directly (persistent across evals) */
      c.struct_registry = seed_registry;
    } else {
      /* Allocate a fresh registry in the compilation arena */
      StructTypeRegistry* reg = (StructTypeRegistry*)arena_alloc(arena, sizeof(StructTypeRegistry));
      struct_registry__init(reg, arena);
      c.struct_registry = reg;
    }
  }

  /* Allocate ctx field list and pre-populate built-in pwd field */
  {
    CtxFieldList* ctx = (CtxFieldList*)arena_alloc(arena, sizeof(CtxFieldList));
    ctx_field_list__init(ctx);
    /* Built-in: mut str pwd (default = process CWD) */
    ctx_field_list__add(ctx, "pwd", 3, "str", 3, TYPE_STR, 0, true, c.struct_registry, JACL_NIL);
    c.ctx_fields = ctx;
  }

  /* Allocate macro table for compile-time macro definitions */
  {
    MacroTable* mt = (MacroTable*)arena_alloc(arena, sizeof(MacroTable));
    macro_table_init(mt);
    c.macro_table = mt;
  }

  /* Macro expansion pass: compile defmacro bodies, expand macro calls.
   * Runs after parsing, before suspension analysis and the main compilation
   * pass. Fast-path: skip entirely if the AST has no defmacros and no
   * command heads that match a built-in macro name (currently just \). */
  if (parse.error_count == 0 && compiler__needs_expansion(parse.nodes, parse.count)) {
    uint32_t err_line = 0, err_col = 0;
    const char *expand_err = ast_expand_macros(
        parse.nodes, parse.count, c.macro_table, heap,
        intern_table, arena, es, &err_line, &err_col);
    if (expand_err) {
      compiler__error(&c, err_line, err_col, expand_err);
    }
  }

  /* Suspension analysis — runs on the post-expansion AST so macros that
     expand into await/race/sleep/etc. correctly mark their enclosing proc
     as suspending. */
  if (parse.error_count == 0 && result.error_count == 0) {
    suspension_map = compiler__analyze_suspension(
        parse.nodes, parse.count, heap, intern_table);
    c.suspension_map = &suspension_map;
  }

  /* Type-inference pass. Populates inferred_type / inferred_struct_idx /
   * inferred_key_struct_idx on every AST node the walk reaches. The
   * compiler reads these annotations rather than re-deriving types.
   * Typer-detected errors are captured in tr and threaded back through
   * compiler__error so compilation fails the same way it would if the
   * compiler itself had detected the mismatch. */
  TyperResult tr;
  if (parse.error_count == 0) {
    /* compiler_compile is the single-file entry; no module cache here.
     * AST_USE nodes will error out during codegen ("use declaration
     * requires module context"), which is the existing behavior. */
    typer_infer(parse.nodes, parse.count, &tr, NULL, 0);
    if (tr.error_count > 0) {
      compiler__error(&c, tr.first_error_line, tr.first_error_col,
                      tr.first_error);
    }
  }

  /* Check if top-level code is suspending */
  bool top_suspends = compiler__top_level_suspends(
      parse.nodes, parse.count, &suspension_map, heap, intern_table);

  if (top_suspends) {
    /* Wrap top-level suspending code into a __main SM closure.
     * Proc definitions use force_global_procs so they remain globals. */

    /* Phase 1: Register all proc global arities FIRST so SM body can
       resolve suspension status of proc calls. */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "proc", 4)) {
        AstNode** pargs = node->data.command.args;
        uint32_t pargc = node->data.command.arg_count;
        if (pargc >= 3) {
          AstNode* name_node = pargs[0];
          if (name_node->type == AST_LIT_STRING && name_node->data.lit_string.length <= 128) {
            JaclVal pname = compiler__name_val(c.heap, c.intern_table,
                name_node->data.lit_string.value, name_node->data.lit_string.length);
            /* Count user params from the param list (head + args) */
            AstNode* param_list = pargs[1];
            int16_t pcount = 0;
            if (param_list->type == AST_COMMAND) {
              AstNode* phead = param_list->data.command.head;
              if (phead && phead->type == AST_LIT_STRING &&
                  phead->data.lit_string.length > 0) {
                pcount = 1 + (int16_t)param_list->data.command.arg_count;
              }
            }
            compiler__set_global_arity(&c, pname, pcount);
            GlobalArity* ga = compiler__find_global(&c, pname);
            if (ga) {
              ga->suspends = suspension_map_lookup(&suspension_map, pname);
            }
          }
        }
      }
    }

    /* Phase 1b: Pre-register top-level mut declarations so proc bodies
       compiled in Phase 2 can resolve mutable globals (needed for set!
       inside spawn/parallel bodies that pin to the parent worker). */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "mut", 3)) {
        uint32_t margc = node->data.command.arg_count;
        if (margc >= 2) {
          /* mut [type] name value — name is last-but-one arg */
          AstNode* name_node = node->data.command.args[margc >= 3 ? 1 : 0];
          if (name_node->type == AST_LIT_STRING &&
              name_node->data.lit_string.length <= 128) {
            JaclVal mname = compiler__name_val(c.heap, c.intern_table,
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            compiler__set_global_arity(&c, mname, -1);
            GlobalArity* ga = compiler__find_global(&c, mname);
            if (ga) {
              ga->is_mutable = true;
            }
          }
        }
      }
    }

    /* Phase 1c: Pre-register ctx field declarations so proc bodies compiled
       in Phase 2 can resolve ctx field accesses.  Only the field metadata is
       registered here; the initialization bytecode is emitted during normal
       compilation in Phase 3 (compiler_compile_node for AST_CTX_DECL). */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_CTX_DECL) {
        const char* type_name     = node->data.ctx_decl.type_name;
        uint32_t    type_name_len = node->data.ctx_decl.type_name_len;
        const char* field_name    = node->data.ctx_decl.field_name;
        uint32_t    field_name_len = node->data.ctx_decl.field_name_len;
        bool        is_mutable    = node->data.ctx_decl.is_mutable != 0;
        JaclType ftype = TYPE_DYN;
        uint32_t f_struct_idx = 0;
        if (is_type_keyword(type_name, type_name_len)) {
          ftype = type_from_keyword(type_name, type_name_len);
        } else {
          uint32_t idx = struct_registry__find(c.struct_registry, type_name, type_name_len);
          if (idx != UINT32_MAX) { ftype = TYPE_STRUCT; f_struct_idx = idx; }
        }
        JaclVal def_val = ctx_eval_const_default(node->data.ctx_decl.default_expr, ftype);
        if (!ctx_field_list__has(c.ctx_fields, field_name, field_name_len)) {
          ctx_field_list__add(c.ctx_fields, field_name, field_name_len,
                             type_name, type_name_len,
                             ftype, f_struct_idx, is_mutable,
                             c.struct_registry, def_val);
        }
      }
    }
    c.ctx_pre_registered = true;

    /* Phase 2: Hoist top-level proc/defstruct/defmacro/use definitions
       into the outer chunk so they are defined via OP_SET_GLOBAL before
       the SM closure executes. proc/use ensure workers have definitions
       in their env in concurrent mode; defstruct/defmacro must compile
       at scope_depth==0 (the SM body runs at scope_depth==1). */
    uint32_t non_proc_count = 0;
    AstNode** non_proc_stmts = (AstNode**)arena_alloc(arena,
        parse.count * sizeof(AstNode*));
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if ((node->type == AST_COMMAND &&
           compiler__head_matches(node->data.command.head, "proc", 4)) ||
          node->type == AST_USE ||
          node->type == AST_DEFSTRUCT ||
          node->type == AST_DEFMACRO) {
        compiler__compile_node(&c, node);
        compiler__emit_check_error(&c, node->start.line);
      } else {
        non_proc_stmts[non_proc_count++] = node;
      }
    }

    /* Phase 3: Create __main SM closure (only non-proc statements) */
    /* Build a synthetic block node for suspension analysis */
    AstNode fake_block;
    memset(&fake_block, 0, sizeof(fake_block));
    fake_block.type = AST_BLOCK;
    fake_block.data.block.count = non_proc_count;
    fake_block.data.block.commands = non_proc_stmts;

    SuspensionAnalysis main_sm_analysis = compiler__analyze_suspensions(
        &fake_block, NULL, 0, true, &suspension_map, heap, intern_table,
        c.struct_registry);

    JaclClosure* main_cl = (JaclClosure*)arena_alloc(arena, sizeof(JaclClosure));
    chunk_init(&main_cl->chunk, arena);
    main_cl->param_count   = 2; /* __sm, __rv */
    main_cl->param_total_slots = 2;
    main_cl->upvalue_count = 0;
    main_cl->upvalue_total_slots = 0;
    main_cl->upvalues      = NULL;
    main_cl->name          = "__main";
    main_cl->min_args      = 0;
    main_cl->variadic      = false;
    main_cl->pinned        = false;
    main_cl->pin_worker_id = -1;
    main_cl->sm_field_count = (uint8_t)main_sm_analysis.state_layout.total_slots;
    main_cl->is_sm_compiled = true;
    JaclVal* main_pnames   = (JaclVal*)arena_alloc(arena, sizeof(JaclVal) * 2);
    main_pnames[0]         = jacl_inline_string("__sm", 4);
    main_pnames[1]         = jacl_inline_string("__rv", 4);
    main_cl->param_names   = main_pnames;

    Compiler body;
    compiler__init(&body, &main_cl->chunk, arena, intern_table, heap);
    body.scope_depth       = 1;
    body.enclosing         = &c;
    body.suspension_map    = &suspension_map;
    body.force_global_procs = true;

    /* Copy global arities */
    memcpy(body.global_arities, c.global_arities,
           sizeof(GlobalArity) * c.global_arity_count);
    body.global_arity_count = c.global_arity_count;

    /* Add __sm and __rv as local slots 0 and 1 */
    compiler__add_local(&body, jacl_inline_string("__sm", 4), 1, 0);
    body.locals[body.local_count - 1].is_param = true;
    compiler__add_local(&body, jacl_inline_string("__rv", 4), 1, 0);
    body.locals[body.local_count - 1].is_param = true;

    {
      SuspensionAnalysis* analysis_ptr =
          (SuspensionAnalysis*)arena_alloc(arena, sizeof(SuspensionAnalysis));
      *analysis_ptr = main_sm_analysis;
      body.sm_analysis = analysis_ptr;
    }

    /* Compile non-proc stmts through SM */
    compiler__compile_sm_stmts(&body, non_proc_stmts, non_proc_count, 1, true);

    /* Propagate errors */
    c.error_count += body.error_count;
    if (!c.first_error && body.first_error) {
      c.first_error = body.first_error;
    }

    main_cl->upvalue_count = (uint8_t)body.upvalue_count;
    /* US-008: compute upvalue_total_slots */
    {
      uint16_t total = 0;
      for (uint32_t i = 0; i < body.upvalue_count; i++)
        total += body.upvalues[i].width;
      main_cl->upvalue_total_slots = total;
    }

    /* Emit OP_CLOSURE in outer chunk */
    uint16_t cl_idx = chunk_add_constant(&result.chunk, jacl_closure(main_cl));
    compiler__emit_byte(&c, OP_CLOSURE, 1);
    compiler__emit_u16(&c, cl_idx, 1);
    for (uint32_t i = 0; i < body.upvalue_count; i++) {
      compiler__emit_byte(&c, body.upvalues[i].is_local, 1);
      compiler__emit_byte(&c, body.upvalues[i].index, 1);
      compiler__emit_byte(&c, (uint8_t)body.upvalues[i].width, 1);
    }
    compiler__emit_byte(&c, OP_HALT, 1);

    result.suspending = true;
  } else {
    /* Normal non-suspending top-level compilation.
     *
     * Pre-scan: lower top-level mut/def to depth-0 locals iff the chunk
     * has no closure/suspend constructs AND has at least one loop
     * (where the lowering actually pays off). The loop precondition
     * also keeps the embed/REPL `jacl_eval` flow on the env path so
     * follow-up evals can still read prior `def x 42` bindings. */
    uint32_t scan = 0;
    for (uint32_t i = 0; i < parse.count; i++) {
      scan |= compiler__top_level_scan(parse.nodes[i]);
    }
    c.lower_top_level = !(scan & TL_SCAN_HAS_CLOSURE) &&
                         (scan & TL_SCAN_HAS_LOOP);

    for (uint32_t i = 0; i < parse.count; i++) {
      compiler__compile_node(&c, parse.nodes[i]);

      /* Emit OP_CHECK_ERROR between statements: auto-return on error */
      if (i < parse.count - 1) {
        compiler__emit_check_error(&c, parse.nodes[i]->start.line);
      }
    }

    uint32_t halt_line =
        parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1;

    /* If lowering pushed top-level mut/def values as depth-0 locals, those
     * slots sit beneath the last statement's result. Strip them while
     * preserving the result on top so direct vm_exec callers (tests,
     * REPLs) see a clean stack — same semantics as the env path used
     * to produce. OP_CLOSE_LOOP takes a uint8_t count; cap at 255 and
     * fall back to no-lowering by erroring if exceeded. */
    if (c.lower_top_level && c.local_count > 0) {
      if (c.local_count > 255) {
        compiler__error(&c, halt_line, 1,
                        "too many top-level bindings to lower (limit 255)");
      } else {
        compiler__emit_byte(&c, OP_CLOSE_LOOP, halt_line);
        compiler__emit_byte(&c, (uint8_t)c.local_count, halt_line);
      }
    }

    compiler__emit_byte(&c, OP_HALT, halt_line);
  }

  /* Finalize ctx struct: register accumulated ctx fields as a StructTypeDef */
  if (c.ctx_fields && c.ctx_fields->count > 0 && c.error_count == 0) {
    ctx_field_list__finalize(c.ctx_fields, c.struct_registry);
  }

  result.error_count  += c.error_count;
  result.error_message = c.first_error;
  result.struct_registry = c.struct_registry;
  result.macro_table     = c.macro_table;

  return result;
}

/* --- Module compilation --- */

/* Populate a Module's export list from the compiler's global_arities,
   excluding underscore-prefixed (private) names. */
void module__populate_exports(Module* mod, Compiler* c) {
  /* Count non-private globals */
  uint32_t count = 0;
  for (uint32_t i = 0; i < c->global_arity_count; i++) {
    char name_buf[8];
    jacl_inline_string_get(c->global_arities[i].name, name_buf, sizeof(name_buf));
    size_t name_len = jacl_inline_string_len(c->global_arities[i].name);
    if (!module__is_private(name_buf, (uint32_t)name_len)) {
      count++;
    }
  }

  if (count == 0) {
    mod->exports = NULL;
    mod->export_count = 0;
    return;
  }

  mod->exports = (ExportEntry*)arena_alloc(c->arena, count * sizeof(ExportEntry));
  mod->export_count = 0;

  for (uint32_t i = 0; i < c->global_arity_count; i++) {
    char name_buf[8];
    jacl_inline_string_get(c->global_arities[i].name, name_buf, sizeof(name_buf));
    size_t name_len = jacl_inline_string_len(c->global_arities[i].name);
    if (module__is_private(name_buf, (uint32_t)name_len)) continue;

    ExportEntry* e = &mod->exports[mod->export_count++];
    /* Arena-copy the name so it outlives the stack buffer */
    char* stored_name = (char*)arena_alloc(c->arena, (uint32_t)(name_len + 1));
    memcpy(stored_name, name_buf, name_len);
    stored_name[name_len] = '\0';
    e->name       = stored_name;
    e->name_len   = (uint32_t)name_len;
    e->arity      = c->global_arities[i].known_arity;
    e->is_mutable = c->global_arities[i].is_mutable;
    e->suspends   = c->global_arities[i].suspends;
    e->type       = c->global_arities[i].type;
    e->return_type = c->global_arities[i].return_type;
    memcpy(e->param_types, c->global_arities[i].param_types, sizeof(e->param_types));
    e->param_count = (c->global_arities[i].known_arity >= 0) ?
                     (uint32_t)c->global_arities[i].known_arity : 0;
  }
}

/* Pre-compile dependency modules referenced by AST_USE nodes and collect
 * imported export signatures into a flat array for the typer.
 *
 * Two-in-one because both passes walk the same AST_USE list and resolve
 * the same canonical paths: pre-compile (so dep_mod->exports is populated)
 * has to run before collection (which reads exports), and the typer
 * pre-pass that consumes the array has to run after collection.
 *
 * Both AST_USE forms contribute entries; the entry's `arity` field
 * disambiguates callable procs (>= 0) from def/mut value imports (< 0):
 *   - Destructuring (`use "path" {names}`): one entry per name with
 *     `binding == NULL`. Procs become TyperProcs; values become
 *     top-level TyperBindings (so `$x` narrows).
 *   - Module-binding (`use "path" name`): one entry per export with
 *     `binding == name`. Looked up at use sites — procs drive
 *     `[$name->fn args]` call dispatch; values drive `$name->field`
 *     access narrowing.
 * Struct constructors (type==STRUCT && return_type==STRUCT) are
 * filtered out: structs are handled by the typer's CapitalCase
 * placeholder pre-pass.
 *
 * `out_imports` must point at an array of at least
 * COMPILER_GLOBAL_ARITIES_MAX entries. */
static uint32_t compiler__collect_typer_imports(Compiler* c,
                                                AstNode** nodes, uint32_t count,
                                                const char* importer_path,
                                                TyperImportProc* out_imports,
                                                uint32_t max_imports) {
  if (!c->module_cache || !importer_path) return 0;
  uint32_t out_count = 0;
  for (uint32_t i = 0; i < count; i++) {
    AstNode* n = nodes[i];
    if (n->type != AST_USE) continue;
    const char* dep_canonical = module__resolve_path(
        importer_path, n->data.use_decl.path, c->arena);
    if (!dep_canonical) continue;
    /* Pre-compile if not cached. Skip on cycle — the AST_USE codegen
     * pass reports the circular-import error with full chain context. */
    if (c->import_stack && import_stack__contains(c->import_stack, dep_canonical)) continue;
    Module* dep_mod = module_cache__find(c->module_cache, dep_canonical);
    if (!dep_mod) {
      (void)compiler__compile_module(dep_canonical, c,
                                     n->start.line, n->start.column);
      dep_mod = module_cache__find(c->module_cache, dep_canonical);
      if (!dep_mod) continue;
    }
    if (n->data.use_decl.is_module_binding) {
      /* `use "path" m`: emit one entry per module export with binding=m. */
      const char* binding     = n->data.use_decl.binding_name;
      uint32_t    binding_len = n->data.use_decl.binding_name_len;
      if (!binding || binding_len == 0) continue;
      for (uint32_t ei = 0; ei < dep_mod->export_count; ei++) {
        if (out_count >= max_imports) return out_count;
        ExportEntry* exp = &dep_mod->exports[ei];
        if (exp->type == TYPE_STRUCT && exp->return_type == TYPE_STRUCT) continue;
        TyperImportProc* slot = &out_imports[out_count++];
        slot->name        = exp->name;
        slot->name_len    = exp->name_len;
        /* Procs use return_type for the call-result narrow; values
         * (arity == -1) use it as the value's declared type. */
        slot->return_type = (uint8_t)((exp->arity >= 0) ? exp->return_type
                                                        : exp->type);
        uint32_t pc = exp->param_count;
        if (pc > COMPILER_MAX_PROC_PARAMS) pc = COMPILER_MAX_PROC_PARAMS;
        slot->param_count = (uint8_t)pc;
        for (uint32_t pi = 0; pi < pc; pi++) {
          slot->param_types[pi] = (uint8_t)exp->param_types[pi];
        }
        slot->arity       = exp->arity;
        slot->binding     = binding;
        slot->binding_len = binding_len;
      }
      continue;
    }
    if (n->data.use_decl.name_count == 0) continue;
    for (uint32_t ni = 0; ni < n->data.use_decl.name_count; ni++) {
      if (out_count >= max_imports) return out_count;
      const char* nm = n->data.use_decl.names[ni];
      uint32_t    nl = n->data.use_decl.name_lens[ni];
      ExportEntry* exp = NULL;
      for (uint32_t ei = 0; ei < dep_mod->export_count; ei++) {
        if (dep_mod->exports[ei].name_len == nl &&
            memcmp(dep_mod->exports[ei].name, nm, nl) == 0) {
          exp = &dep_mod->exports[ei];
          break;
        }
      }
      if (!exp) continue;
      if (exp->type == TYPE_STRUCT && exp->return_type == TYPE_STRUCT) continue;
      TyperImportProc* slot = &out_imports[out_count++];
      slot->name        = nm;
      slot->name_len    = nl;
      slot->return_type = (uint8_t)((exp->arity >= 0) ? exp->return_type
                                                      : exp->type);
      uint32_t pc = exp->param_count;
      if (pc > COMPILER_MAX_PROC_PARAMS) pc = COMPILER_MAX_PROC_PARAMS;
      slot->param_count = (uint8_t)pc;
      for (uint32_t pi = 0; pi < pc; pi++) {
        slot->param_types[pi] = (uint8_t)exp->param_types[pi];
      }
      slot->arity       = exp->arity;
      slot->binding     = NULL;
      slot->binding_len = 0;
    }
  }
  return out_count;
}

/* Compile a module from a canonical file path.
   Reads the file, lexes, parses, and compiles into a new Module in the cache.
   Returns true on success, false on error (error reported via importer). */
bool compiler__compile_module(const char* canonical_path,
                                     Compiler* importer,
                                     uint32_t line, uint32_t col) {
  arena_t* arena = importer->arena;

  /* Read source file */
  char* source = module__read_file(canonical_path, arena);
  if (!source) {
    char buf[256];
    snprintf(buf, sizeof(buf), "could not read module: \"%s\"", canonical_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    compiler__error(importer, line, col, msg);
    return false;
  }

  /* Create module in cache */
  Module* mod = module_cache__add(importer->module_cache, canonical_path);
  if (!mod) {
    compiler__error(importer, line, col, "too many modules (cache full)");
    return false;
  }
  mod->source = source;

  /* Push onto import stack for circular detection */
  import_stack__push(importer->import_stack, canonical_path);

  /* Lex and parse */
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  if (parse.error_count > 0) {
    char buf[256];
    snprintf(buf, sizeof(buf), "parse error in module \"%s\"", canonical_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    compiler__error(importer, line, col, msg);
    import_stack__pop(importer->import_stack);
    return false;
  }

  /* Suspension analysis */
  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count, importer->heap, importer->intern_table);

  /* Compile into a new chunk */
  BytecodeChunk* chunk = (BytecodeChunk*)arena_alloc(arena, sizeof(BytecodeChunk));
  chunk_init(chunk, arena);

  Compiler mc;
  compiler__init(&mc, chunk, arena, importer->intern_table, importer->heap);
  mc.suspension_map  = &suspension_map;
  mc.module_cache    = importer->module_cache;
  mc.current_module  = mod;
  mc.import_stack    = importer->import_stack;
  {
    /* Share struct registry and ctx field list from importer root with module compiler */
    Compiler* imp_root = importer;
    while (imp_root->enclosing) imp_root = imp_root->enclosing;
    mc.struct_registry = imp_root->struct_registry;
    mc.ctx_fields      = imp_root->ctx_fields;
  }
  mc.module_prefix   = module__build_prefix(canonical_path, arena,
                                              &mc.module_prefix_len);

  /* Pre-compile imports and collect proc signatures for the typer.
   * Triggers compilation of dependency modules so dep_mod->exports is
   * populated, then collects imported procs so cross-module calls
   * narrow to the declared return type instead of dyn. Errors during
   * dep compile propagate via the importer (mc) and are surfaced after
   * typing. */
  TyperImportProc imports[COMPILER_GLOBAL_ARITIES_MAX];
  uint32_t import_count = compiler__collect_typer_imports(
      &mc, parse.nodes, parse.count, canonical_path,
      imports, COMPILER_GLOBAL_ARITIES_MAX);

  /* Phase 3 typer pass: walk the module AST so dual-track invariants
   * hold during compile, and so consumer sites that read from
   * inferred_type don't fall back unnecessarily. */
  {
    TyperResult tr;
    typer_infer(parse.nodes, parse.count, &tr, imports, import_count);
    if (tr.error_count > 0) {
      compiler__error(&mc, tr.first_error_line, tr.first_error_col,
                      tr.first_error);
    }
  }

  /* Compile all top-level statements */
  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&mc, parse.nodes[i]);
    if (i < parse.count - 1) {
      compiler__emit_check_error(&mc, parse.nodes[i]->start.line);
    }
  }

  /* Every module chunk ends with OP_HALT */
  compiler__emit_byte(&mc, OP_HALT,
                      parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);

  /* Populate exports from global arities (exclude private names) */
  module__populate_exports(mod, &mc);

  mod->chunk      = chunk;
  mod->compiled   = true;
  mod->topo_order = importer->module_cache->topo_counter++;

  /* Pop import stack */
  import_stack__pop(importer->import_stack);

  /* Propagate errors to importer */
  if (mc.error_count > 0) {
    importer->error_count += mc.error_count;
    if (!importer->first_error && mc.first_error) {
      importer->first_error = mc.first_error;
    }
    return false;
  }

  return true;
}

/* --- Multi-file program compilation API --- */

/* Compile a program starting from a root file.
   Recursively compiles all dependency modules and returns them in
   topological order (dependencies first, root module last). */
ProgramResult jacl_compile_program(const char* root_path,
                                          arena_t* arena,
                                          JaclInternTable* intern_table,
                                          ThreadHeap* heap) {
  ProgramResult result;
  memset(&result, 0, sizeof(result));

  /* Canonicalize root path */
  char resolved[PATH_MAX];
  if (!realpath(root_path, resolved)) {
    result.error_count = 1;
    char buf[256];
    snprintf(buf, sizeof(buf), "module not found: \"%s\"", root_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    result.error_message = msg;
    return result;
  }
  char* canonical = (char*)arena_alloc(arena, (uint32_t)(strlen(resolved) + 1));
  memcpy(canonical, resolved, strlen(resolved) + 1);

  /* Read source file */
  char* source = module__read_file(canonical, arena);
  if (!source) {
    result.error_count = 1;
    char buf[256];
    snprintf(buf, sizeof(buf), "could not read module: \"%s\"", root_path);
    char* msg = (char*)arena_alloc(arena, (uint32_t)(strlen(buf) + 1));
    memcpy(msg, buf, strlen(buf) + 1);
    result.error_message = msg;
    return result;
  }

  /* Set up module infrastructure */
  ModuleCache cache;
  module_cache__init(&cache, arena);
  ImportStack istack;
  import_stack__init(&istack);

  /* Create root module in cache */
  Module* root_mod = module_cache__add(&cache, canonical);
  root_mod->source = source;

  import_stack__push(&istack, canonical);

  /* Lex and parse */
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  if (parse.error_count > 0) {
    result.error_count = parse.error_count;
    /* Extract first parse error message from AST_ERROR nodes */
    const char* parse_err = NULL;
    for (uint32_t i = 0; i < parse.count && !parse_err; i++) {
      if (parse.nodes[i]->type == AST_ERROR) {
        parse_err = parse.nodes[i]->data.error.message;
        fprintf(stderr, "[DEBUG] AST_ERROR[%u]: %s\n", i, parse_err ? parse_err : "(null)");
      }
    }
    result.error_message = parse_err ? parse_err : "parse error in root module";
    return result;
  }

  /* Suspension analysis */
  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count, heap, intern_table);
  bool top_suspends = compiler__top_level_suspends(
      parse.nodes, parse.count, &suspension_map, heap, intern_table);

  /* Create root module chunk */
  BytecodeChunk* root_chunk = (BytecodeChunk*)arena_alloc(
      arena, sizeof(BytecodeChunk));
  chunk_init(root_chunk, arena);

  /* Initialize compiler with module context */
  Compiler c;
  compiler__init(&c, root_chunk, arena, intern_table, heap);
  c.suspension_map = &suspension_map;
  c.module_cache   = &cache;
  c.current_module = root_mod;
  c.import_stack   = &istack;
  c.module_prefix  = module__build_prefix(canonical, arena, &c.module_prefix_len);
  {
    StructTypeRegistry* reg = (StructTypeRegistry*)arena_alloc(arena, sizeof(StructTypeRegistry));
    struct_registry__init(reg, arena);
    c.struct_registry = reg;
  }

  /* Allocate ctx field list and pre-populate built-in pwd field */
  {
    CtxFieldList* ctx = (CtxFieldList*)arena_alloc(arena, sizeof(CtxFieldList));
    ctx_field_list__init(ctx);
    ctx_field_list__add(ctx, "pwd", 3, "str", 3, TYPE_STR, 0, true, c.struct_registry, JACL_NIL);
    c.ctx_fields = ctx;
  }

  /* Pre-compile imports and collect proc signatures for the typer
   * (see compiler__collect_typer_imports). */
  TyperImportProc imports[COMPILER_GLOBAL_ARITIES_MAX];
  uint32_t import_count = compiler__collect_typer_imports(
      &c, parse.nodes, parse.count, canonical,
      imports, COMPILER_GLOBAL_ARITIES_MAX);

  /* Phase 3 typer pass for module programs (mirrors compiler_compile and
   * compiler__compile_module). */
  {
    TyperResult tr;
    typer_infer(parse.nodes, parse.count, &tr, imports, import_count);
    if (tr.error_count > 0) {
      compiler__error(&c, tr.first_error_line, tr.first_error_col,
                      tr.first_error);
    }
  }

  if (top_suspends) {
    /* Wrap top-level suspending code in SM closure — same logic as compiler_compile */

    /* Phase 1: Register proc global arities */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "proc", 4)) {
        AstNode** pargs = node->data.command.args;
        uint32_t pargc = node->data.command.arg_count;
        if (pargc >= 3) {
          AstNode* name_node = pargs[0];
          if (name_node->type == AST_LIT_STRING &&
              name_node->data.lit_string.length <= 128) {
            JaclVal pname = compiler__name_val(c.heap, c.intern_table,
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            AstNode* param_list = pargs[1];
            int16_t pcount = 0;
            if (param_list->type == AST_COMMAND) {
              AstNode* phead = param_list->data.command.head;
              if (phead && phead->type == AST_LIT_STRING &&
                  phead->data.lit_string.length > 0) {
                pcount = 1 + (int16_t)param_list->data.command.arg_count;
              }
            }
            compiler__set_global_arity(&c, pname, pcount);
            GlobalArity* ga = compiler__find_global(&c, pname);
            if (ga) {
              ga->suspends = suspension_map_lookup(&suspension_map, pname);
            }
          }
        }
      }
    }

    /* Phase 1b: Pre-register mut declarations */
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "mut", 3)) {
        uint32_t margc = node->data.command.arg_count;
        if (margc >= 2) {
          AstNode* name_node = node->data.command.args[margc >= 3 ? 1 : 0];
          if (name_node->type == AST_LIT_STRING &&
              name_node->data.lit_string.length <= 128) {
            JaclVal mname = compiler__name_val(c.heap, c.intern_table,
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            compiler__set_global_arity(&c, mname, -1);
            GlobalArity* ga = compiler__find_global(&c, mname);
            if (ga) {
              ga->is_mutable = true;
            }
          }
        }
      }
    }

    /* Phase 2: Hoist proc definitions, collect non-proc stmts */
    uint32_t non_proc_count = 0;
    AstNode** non_proc_stmts = (AstNode**)arena_alloc(arena,
        parse.count * sizeof(AstNode*));
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if ((node->type == AST_COMMAND &&
           compiler__head_matches(node->data.command.head, "proc", 4)) ||
          node->type == AST_USE ||
          node->type == AST_DEFSTRUCT ||
          node->type == AST_DEFMACRO) {
        compiler__compile_node(&c, node);
        compiler__emit_check_error(&c, node->start.line);
      } else {
        non_proc_stmts[non_proc_count++] = node;
      }
    }

    /* Phase 3: Create __main SM closure */
    AstNode fake_block2;
    memset(&fake_block2, 0, sizeof(fake_block2));
    fake_block2.type = AST_BLOCK;
    fake_block2.data.block.count = non_proc_count;
    fake_block2.data.block.commands = non_proc_stmts;

    SuspensionAnalysis main_sm_analysis2 = compiler__analyze_suspensions(
        &fake_block2, NULL, 0, true, &suspension_map, heap, intern_table,
        c.struct_registry);

    JaclClosure* main_cl = (JaclClosure*)arena_alloc(arena, sizeof(JaclClosure));
    chunk_init(&main_cl->chunk, arena);
    main_cl->param_count   = 2; /* __sm, __rv */
    main_cl->param_total_slots = 2;
    main_cl->upvalue_count = 0;
    main_cl->upvalue_total_slots = 0;
    main_cl->upvalues      = NULL;
    main_cl->name          = "__main";
    main_cl->min_args      = 0;
    main_cl->variadic      = false;
    main_cl->pinned        = false;
    main_cl->pin_worker_id = -1;
    main_cl->sm_field_count = (uint8_t)main_sm_analysis2.state_layout.total_slots;
    main_cl->is_sm_compiled = true;
    JaclVal* main_pnames   = (JaclVal*)arena_alloc(arena, sizeof(JaclVal) * 2);
    main_pnames[0]         = jacl_inline_string("__sm", 4);
    main_pnames[1]         = jacl_inline_string("__rv", 4);
    main_cl->param_names   = main_pnames;

    Compiler body;
    compiler__init(&body, &main_cl->chunk, arena, intern_table, heap);
    body.scope_depth       = 1;
    body.enclosing         = &c;
    body.suspension_map    = &suspension_map;
    body.force_global_procs = true;
    body.module_cache      = &cache;
    body.current_module    = root_mod;
    body.import_stack      = &istack;

    memcpy(body.global_arities, c.global_arities,
           sizeof(GlobalArity) * c.global_arity_count);
    body.global_arity_count = c.global_arity_count;

    compiler__add_local(&body, jacl_inline_string("__sm", 4), 1, 0);
    body.locals[body.local_count - 1].is_param = true;
    compiler__add_local(&body, jacl_inline_string("__rv", 4), 1, 0);
    body.locals[body.local_count - 1].is_param = true;

    {
      SuspensionAnalysis* analysis_ptr2 =
          (SuspensionAnalysis*)arena_alloc(arena, sizeof(SuspensionAnalysis));
      *analysis_ptr2 = main_sm_analysis2;
      body.sm_analysis = analysis_ptr2;
    }

    compiler__compile_sm_stmts(&body, non_proc_stmts, non_proc_count, 1, true);

    c.error_count += body.error_count;
    if (!c.first_error && body.first_error) {
      c.first_error = body.first_error;
    }

    main_cl->upvalue_count = (uint8_t)body.upvalue_count;
    /* US-008: compute upvalue_total_slots */
    {
      uint16_t total = 0;
      for (uint32_t i = 0; i < body.upvalue_count; i++)
        total += body.upvalues[i].width;
      main_cl->upvalue_total_slots = total;
    }

    uint16_t cl_idx = chunk_add_constant(root_chunk, jacl_closure(main_cl));
    compiler__emit_byte(&c, OP_CLOSURE, 1);
    compiler__emit_u16(&c, cl_idx, 1);
    for (uint32_t i = 0; i < body.upvalue_count; i++) {
      compiler__emit_byte(&c, body.upvalues[i].is_local, 1);
      compiler__emit_byte(&c, body.upvalues[i].index, 1);
      compiler__emit_byte(&c, (uint8_t)body.upvalues[i].width, 1);
    }
    compiler__emit_byte(&c, OP_HALT, 1);

    result.suspending = true;
  } else {
    /* Normal non-suspending compilation */
    for (uint32_t i = 0; i < parse.count; i++) {
      compiler__compile_node(&c, parse.nodes[i]);
      if (i < parse.count - 1) {
        compiler__emit_check_error(&c, parse.nodes[i]->start.line);
      }
    }
    compiler__emit_byte(&c, OP_HALT,
        parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);
  }

  /* Populate root module exports and finalize */
  module__populate_exports(root_mod, &c);
  root_mod->chunk      = root_chunk;
  root_mod->compiled   = true;
  root_mod->topo_order = cache.topo_counter++;

  import_stack__pop(&istack);

  /* Build topological module list: dependencies first, root last.
     Modules have topo_order assigned in DFS post-order during compilation,
     so sorting by topo_order gives correct dependency order. */
  result.module_count = cache.count;
  result.modules = (Module**)arena_alloc(arena,
      cache.count * sizeof(Module*));

  for (uint32_t i = 0; i < cache.count; i++) {
    result.modules[i] = cache.modules[i];
  }
  /* Insertion sort by topo_order (small N, arena-allocated) */
  for (uint32_t i = 1; i < cache.count; i++) {
    Module* key = result.modules[i];
    uint32_t j = i;
    while (j > 0 && result.modules[j - 1]->topo_order > key->topo_order) {
      result.modules[j] = result.modules[j - 1];
      j--;
    }
    result.modules[j] = key;
  }

  /* Finalize ctx struct: register accumulated ctx fields as a StructTypeDef */
  if (c.ctx_fields && c.ctx_fields->count > 0 && c.error_count == 0) {
    ctx_field_list__finalize(c.ctx_fields, c.struct_registry);
  }

  result.error_count   = c.error_count;
  result.error_message = c.first_error;
  result.struct_registry = c.struct_registry;
  return result;
}

#endif /* COMPILER_C */
