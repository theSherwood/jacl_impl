/*
 * JACL Compiler
 *
 * Translates AST (from parser) into bytecode chunks for the VM.
 */

#ifndef COMPILER_C
#define COMPILER_C

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
#define COMPILER_SCALAR_VEC_BASE 0xFF00u
#define COMPILER_IS_SCALAR_TYPE_IDX(idx) ((idx) >= COMPILER_SCALAR_VEC_BASE && (idx) < 0x10000u)
#define COMPILER_SCALAR_TYPE_IDX(t) (COMPILER_SCALAR_VEC_BASE + (uint32_t)(t))
#define COMPILER_TYPE_IDX_TO_SCALAR(idx) ((JaclType)((idx) - COMPILER_SCALAR_VEC_BASE))

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
  if (reg->ctx_type_idx == 0) return true;
  return sdef != reg->defs[reg->ctx_type_idx];
}

/* Initialize a struct type registry. Container is arena-allocated; defs array is heap-allocated.
   arena: the arena used for StructTypeDef allocations (must outlive the registry). */
static void struct_registry__init(StructTypeRegistry* reg, arena_t* arena) {
  reg->arena = arena;
  reg->capacity = STRUCT_REGISTRY_INIT_CAP;
  reg->defs = (StructTypeDef**)calloc(reg->capacity, sizeof(StructTypeDef*));
  reg->count = 1; /* slot 0 is reserved for plain dyn JaclVal boxes */
  reg->defs[0] = NULL;
  reg->ctx_type_idx = 0; /* not yet registered */
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

  /* Ensure capacity */
  if (!struct_registry__grow(reg)) return 0;

  uint32_t type_idx = reg->count;
  reg->count++;
  reg->defs[type_idx] = NULL; /* placeholder */

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
    reg->count = type_idx; /* rollback */
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
  reg->ctx_type_idx = type_idx;
  return type_idx;
}

/* C-ABI size and alignment for a JaclType */
uint32_t struct__type_size(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx) {
  switch (t) {
    case TYPE_BOOL:    return 1;
    case TYPE_NIL:     return 0;
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
  }
  return 8;
}

uint32_t struct__type_align(JaclType t, StructTypeRegistry* reg, uint32_t struct_idx) {
  switch (t) {
    case TYPE_BOOL:    return 1;
    case TYPE_NIL:     return 1;
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
  char resolved[1024];
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
void analyze__walk_body(AstNode* node, ProcSuspendInfo* info,
                        ThreadHeap* heap, JaclInternTable* intern_table) {
  if (!node) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* name = head->data.lit_string.value;
        uint32_t len = head->data.lit_string.length;

        /* Direct suspension points */
        if ((len == 5 && memcmp(name, "await", 5) == 0) ||
            (len == 8 && memcmp(name, "parallel", 8) == 0) ||
            (len == 4 && memcmp(name, "race", 4) == 0)) {
          info->direct_suspends = true;
          /* Still recurse into args (they might contain calls) */
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            analyze__walk_body(node->data.command.args[i], info, heap, intern_table);
          }
          return;
        }

        /* Yield is a suspension point and marks proc as generator */
        if (len == 5 && memcmp(name, "yield", 5) == 0) {
          info->direct_suspends = true;
          info->has_yield = true;
          /* Still recurse into args (they might contain calls) */
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            analyze__walk_body(node->data.command.args[i], info, heap, intern_table);
          }
          return;
        }

        /* Skip recursion INTO nested proc bodies (they have their own scope) */
        if (len == 4 && memcmp(name, "proc", 4) == 0) {
          return;
        }

        /* spawn is NOT a suspension point; its block arg is a separate
           closure scope — skip recursion (like proc) */
        if (len == 5 && memcmp(name, "spawn", 5) == 0) {
          return;
        }

        /* Record callee name for named calls (for transitive propagation) */
        if (info->callee_count < SUSPENSION_CALLEES_MAX) {
          info->callees[info->callee_count++] =
              compiler__name_val(heap, intern_table, name, len);
        }
      } else if (head->type == AST_VAR_REF) {
        /* Indirect call through variable ($f ...) */
        info->has_indirect_call = true;
      }

      /* Recurse into arguments */
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        analyze__walk_body(node->data.command.args[i], info, heap, intern_table);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        analyze__walk_body(node->data.block.commands[i], info, heap, intern_table);
      }
      break;
    }
    case AST_INTERP_STRING: {
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        analyze__walk_body(node->data.interp_string.segments[i], info, heap, intern_table);
      }
      break;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        analyze__walk_body(node->data.break_stmt.value, info, heap, intern_table);
      }
      break;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        analyze__walk_body(node->data.return_stmt.value, info, heap, intern_table);
      }
      break;
    }
    case AST_SHELL_CMD: {
      /* Shell commands call exec - record as a callee */
      if (info->callee_count < SUSPENSION_CALLEES_MAX) {
        info->callees[info->callee_count++] =
            compiler__name_val(heap, intern_table, "exec", 4);
      }
      /* Recurse into head and args */
      analyze__walk_body(node->data.shell_cmd.head, info, heap, intern_table);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        analyze__walk_body(node->data.shell_cmd.args[i], info, heap, intern_table);
      }
      break;
    }
    default:
      break;
  }
}

/* Recursively collect proc definitions from AST, analyzing each body */
void analyze__collect_procs(AstNode* node, ProcSuspendInfoList* list,
                             ThreadHeap* heap, JaclInternTable* intern_table) {
  if (!node) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      uint32_t argc = node->data.command.arg_count;
      AstNode** args = node->data.command.args;

      if (head->type == AST_LIT_STRING &&
          head->data.lit_string.length == 4 &&
          memcmp(head->data.lit_string.value, "proc", 4) == 0) {

        /* Determine name and body indices based on argc */
        uint32_t name_idx, body_idx;
        if (argc == 4)      { name_idx = 1; body_idx = 3; }
        else if (argc == 3) { name_idx = 0; body_idx = 2; }
        else goto recurse_args;

        if (args[name_idx]->type != AST_LIT_STRING) goto recurse_args;
        uint32_t name_len = args[name_idx]->data.lit_string.length;
        if (name_len > 128) goto recurse_args;

        JaclVal proc_name = compiler__name_val(
            heap, intern_table,
            args[name_idx]->data.lit_string.value, name_len);

        if (list->count < MAX_PROC_INFOS) {
          ProcSuspendInfo* info = &list->procs[list->count++];
          info->name = proc_name;
          info->direct_suspends = false;
          info->has_yield = false;
          info->has_indirect_call = false;
          info->callee_count = 0;

          /* Walk the body to find suspension points and callees */
          if (args[body_idx]->type == AST_BLOCK) {
            analyze__walk_body(args[body_idx], info, heap, intern_table);
          }
        }

        /* Recurse into body to find nested procs */
        if (args[body_idx]->type == AST_BLOCK) {
          analyze__collect_procs(args[body_idx], list, heap, intern_table);
        }
        return;
      }

      recurse_args:
      for (uint32_t i = 0; i < argc; i++) {
        analyze__collect_procs(args[i], list, heap, intern_table);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        analyze__collect_procs(node->data.block.commands[i], list, heap, intern_table);
      }
      break;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        analyze__collect_procs(node->data.break_stmt.value, list, heap, intern_table);
      }
      break;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        analyze__collect_procs(node->data.return_stmt.value, list, heap, intern_table);
      }
      break;
    }
    case AST_SHELL_CMD: {
      /* Recurse into head and args to find nested procs */
      analyze__collect_procs(node->data.shell_cmd.head, list, heap, intern_table);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        analyze__collect_procs(node->data.shell_cmd.args[i], list, heap, intern_table);
      }
      break;
    }
    default:
      break;
  }
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
void sm__walk_suspensions(AstNode* node, SuspensionAnalysis* analysis,
                                  SuspensionMap* map,
                                  ThreadHeap* heap, JaclInternTable* intern_table) {
  if (!node) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* name = head->data.lit_string.value;
        uint32_t len = head->data.lit_string.length;

        /* yield is a suspension point */
        if (len == 5 && memcmp(name, "yield", 5) == 0) {
          if (analysis->suspension_count < SM_MAX_SUSPENSION_POINTS) {
            SuspensionPoint* sp =
                &analysis->suspension_points[analysis->suspension_count];
            sp->id     = analysis->suspension_count;
            sp->type   = SUSPEND_YIELD;
            sp->node   = node;
            sp->line   = node->start.line;
            sp->column = node->start.column;
            analysis->suspension_count++;
          }
          /* Still recurse into args (they might contain nested suspension) */
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            sm__walk_suspensions(node->data.command.args[i], analysis, map,
                                 heap, intern_table);
          }
          return;
        }

        /* await is a suspension point */
        if (len == 5 && memcmp(name, "await", 5) == 0) {
          if (analysis->suspension_count < SM_MAX_SUSPENSION_POINTS) {
            SuspensionPoint* sp =
                &analysis->suspension_points[analysis->suspension_count];
            sp->id     = analysis->suspension_count;
            sp->type   = SUSPEND_AWAIT;
            sp->node   = node;
            sp->line   = node->start.line;
            sp->column = node->start.column;
            analysis->suspension_count++;
          }
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            sm__walk_suspensions(node->data.command.args[i], analysis, map,
                                 heap, intern_table);
          }
          return;
        }

        /* parallel is a suspension point */
        if (len == 8 && memcmp(name, "parallel", 8) == 0) {
          if (analysis->suspension_count < SM_MAX_SUSPENSION_POINTS) {
            SuspensionPoint* sp =
                &analysis->suspension_points[analysis->suspension_count];
            sp->id     = analysis->suspension_count;
            sp->type   = SUSPEND_PARALLEL;
            sp->node   = node;
            sp->line   = node->start.line;
            sp->column = node->start.column;
            analysis->suspension_count++;
          }
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            sm__walk_suspensions(node->data.command.args[i], analysis, map,
                                 heap, intern_table);
          }
          return;
        }

        /* race is a suspension point */
        if (len == 4 && memcmp(name, "race", 4) == 0) {
          if (analysis->suspension_count < SM_MAX_SUSPENSION_POINTS) {
            SuspensionPoint* sp =
                &analysis->suspension_points[analysis->suspension_count];
            sp->id     = analysis->suspension_count;
            sp->type   = SUSPEND_RACE;
            sp->node   = node;
            sp->line   = node->start.line;
            sp->column = node->start.column;
            analysis->suspension_count++;
          }
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            sm__walk_suspensions(node->data.command.args[i], analysis, map,
                                 heap, intern_table);
          }
          return;
        }

        /* Do NOT recurse into nested proc or spawn definitions —
           they are separate closure scopes with their own analysis */
        if ((len == 4 && memcmp(name, "proc", 4) == 0) ||
            (len == 5 && memcmp(name, "spawn", 5) == 0)) {
          return;
        }

        /* Call to a known suspending proc is a suspension point */
        if (map) {
          JaclVal name_val = compiler__name_val(heap, intern_table, name, len);
          if (suspension_map_lookup(map, name_val) &&
              !suspension_map_is_generator(map, name_val)) {
            if (analysis->suspension_count < SM_MAX_SUSPENSION_POINTS) {
              SuspensionPoint* sp =
                  &analysis->suspension_points[analysis->suspension_count];
              sp->id     = analysis->suspension_count;
              sp->type   = SUSPEND_CALL;
              sp->node   = node;
              sp->line   = node->start.line;
              sp->column = node->start.column;
              analysis->suspension_count++;
            }
            for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
              sm__walk_suspensions(node->data.command.args[i], analysis, map,
                                   heap, intern_table);
            }
            return;
          }
        }
      }

      /* Recurse into arguments for all other commands */
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        sm__walk_suspensions(node->data.command.args[i], analysis, map,
                             heap, intern_table);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        sm__walk_suspensions(node->data.block.commands[i], analysis, map,
                             heap, intern_table);
      }
      break;
    }
    case AST_INTERP_STRING: {
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        sm__walk_suspensions(node->data.interp_string.segments[i], analysis, map,
                             heap, intern_table);
      }
      break;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        sm__walk_suspensions(node->data.break_stmt.value, analysis, map,
                             heap, intern_table);
      }
      break;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        sm__walk_suspensions(node->data.return_stmt.value, analysis, map,
                             heap, intern_table);
      }
      break;
    }
    case AST_SHELL_CMD: {
      /* Recurse into head and args to find suspension points */
      sm__walk_suspensions(node->data.shell_cmd.head, analysis, map,
                           heap, intern_table);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        sm__walk_suspensions(node->data.shell_cmd.args[i], analysis, map,
                             heap, intern_table);
      }
      break;
    }
    default:
      break;
  }
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
void sm__walk_locals(AstNode* node, StateLayout* layout,
                            StructTypeRegistry* reg) {
  if (!node) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;
        uint32_t argc = node->data.command.arg_count;
        AstNode** args = node->data.command.args;

        /* def / mut / = / : — local bindings (= is sugar for def, : for mut) */
        if ((hlen == 3 && memcmp(hname, "def", 3) == 0) ||
            (hlen == 3 && memcmp(hname, "mut", 3) == 0) ||
            (hlen == 1 && hname[0] == '=') ||
            (hlen == 1 && hname[0] == ':')) {
          bool is_mut = (hname[0] == 'm' || hname[0] == ':');

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
            sm__walk_locals(args[i], layout, reg);
          }
          return;
        }

        /* for — creates loop bindings */
        if (hlen == 3 && memcmp(hname, "for", 3) == 0) {
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
            sm__walk_locals(args[i], layout, reg);
          }
          return;
        }

        /* try — catch binding is scope-local (handler cannot suspend),
           so do NOT add it to the state layout.  Only recurse into
           the try-body and handler body for nested bindings. */
        if (hlen == 3 && memcmp(hname, "try", 3) == 0) {
          for (uint32_t i = 0; i < argc; i++) {
            if (i == 1 && argc == 3 && args[1]->type == AST_LIT_STRING)
              continue;  /* skip catch binding name */
            sm__walk_locals(args[i], layout, reg);
          }
          return;
        }

        /* proc — named proc creates a binding; do NOT recurse into body */
        if (hlen == 4 && memcmp(hname, "proc", 4) == 0) {
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
        if (hlen == 5 && memcmp(hname, "spawn", 5) == 0) {
          return;
        }
      }

      /* Recurse into arguments for all other commands */
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        sm__walk_locals(node->data.command.args[i], layout, reg);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        sm__walk_locals(node->data.block.commands[i], layout, reg);
      }
      break;
    }
    case AST_INTERP_STRING: {
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        sm__walk_locals(node->data.interp_string.segments[i], layout, reg);
      }
      break;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        sm__walk_locals(node->data.break_stmt.value, layout, reg);
      }
      break;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        sm__walk_locals(node->data.return_stmt.value, layout, reg);
      }
      break;
    }
    case AST_SHELL_CMD: {
      /* Recurse into head and args to find nested locals */
      sm__walk_locals(node->data.shell_cmd.head, layout, reg);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        sm__walk_locals(node->data.shell_cmd.args[i], layout, reg);
      }
      break;
    }
    default:
      break;
  }
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
void sm__liveness_walk(AstNode* node, const StateLayout* layout,
                               FieldLiveness* liveness, int32_t* segment) {
  if (!node) return;

  switch (node->type) {
    case AST_VAR_REF: {
      if (node->data.var_ref.length <= 128) {
        JaclVal name = compiler__name_val(layout->heap, layout->intern_table,
                                          node->data.var_ref.name,
                                          node->data.var_ref.length);
        sm__liveness_mark_read(liveness, layout, name, *segment);
      }
      break;
    }

    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      uint32_t argc = node->data.command.arg_count;
      AstNode** args = node->data.command.args;

      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;

        /* --- Suspension points: increment segment AFTER evaluating args --- */
        if ((hlen == 5 && memcmp(hname, "yield", 5) == 0) ||
            (hlen == 5 && memcmp(hname, "await", 5) == 0) ||
            (hlen == 8 && memcmp(hname, "parallel", 8) == 0) ||
            (hlen == 4 && memcmp(hname, "race", 4) == 0)) {
          /* Walk args (evaluated before suspension) */
          for (uint32_t i = 0; i < argc; i++) {
            sm__liveness_walk(args[i], layout, liveness, segment);
          }
          /* Suspension occurs — next code is in a new segment */
          (*segment)++;
          return;
        }

        /* --- def / mut / = / : — mark binding names as writes --- */
        if ((hlen == 3 && memcmp(hname, "def", 3) == 0) ||
            (hlen == 3 && memcmp(hname, "mut", 3) == 0) ||
            (hlen == 1 && hname[0] == '=') ||
            (hlen == 1 && hname[0] == ':')) {
          /* Walk RHS first (it may reference variables) */
          uint32_t val_idx = (argc == 3) ? 2 : 1;
          if (val_idx < argc) {
            sm__liveness_walk(args[val_idx], layout, liveness, segment);
          }
          /* Mark binding name as write */
          if (argc >= 2) {
            uint32_t name_idx = (argc == 3) ? 1 : 0;
            sm__liveness_mark_binding_names(args[name_idx], layout, liveness,
                                            *segment);
          }
          return;
        }

        /* --- set: mark target as write, walk value --- */
        if ((hlen == 3 && memcmp(hname, "set", 3) == 0) ||
            (hlen == 2 && memcmp(hname, "::", 2) == 0)) {
          if (argc >= 2) {
            /* Walk value expression (may read variables) */
            sm__liveness_walk(args[1], layout, liveness, segment);
            /* Mark target as write */
            if (args[0]->type == AST_LIT_STRING) {
              sm__liveness_mark_write(liveness, layout,
                  sm__lit_string_name(layout, args[0]), *segment);
            }
          }
          return;
        }

        /* --- while: handle suspending loops with back-edge expansion --- */
        if (hlen == 5 && memcmp(hname, "while", 5) == 0) {
          if (argc >= 2) {
            AstNode* cond = args[0];
            AstNode* body = args[argc - 1];
            bool loop_suspends = sm__loop_body_suspends(body);
            if (loop_suspends) {
              /* Record segment at loop entry */
              int32_t loop_start = *segment;
              /* Walk condition and body normally */
              sm__liveness_walk(cond, layout, liveness, segment);
              sm__liveness_walk(body, layout, liveness, segment);
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
              sm__liveness_walk(cond, layout, liveness, segment);
              sm__liveness_walk(body, layout, liveness, segment);
            }
          }
          return;
        }

        /* --- for: loop variable binding + suspending loop handling --- */
        if (hlen == 3 && memcmp(hname, "for", 3) == 0) {
          if (argc >= 2) {
            AstNode* body = args[argc - 1];
            bool loop_suspends = (body->type == AST_BLOCK) &&
                                 sm__loop_body_suspends(body);
            int32_t loop_start = *segment;

            /* Walk collection expression */
            sm__liveness_walk(args[0], layout, liveness, segment);

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
            sm__liveness_walk(body, layout, liveness, segment);

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
        if (hlen == 3 && memcmp(hname, "try", 3) == 0) {
          /* Walk try body */
          if (argc >= 1) sm__liveness_walk(args[0], layout, liveness, segment);
          /* Skip catch binding name — not a state field */
          /* Walk catch body */
          if (argc >= 3) sm__liveness_walk(args[2], layout, liveness, segment);
          return;
        }

        /* --- proc: named proc = write; don't recurse into body --- */
        if (hlen == 4 && memcmp(hname, "proc", 4) == 0) {
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
        if (hlen == 5 && memcmp(hname, "spawn", 5) == 0) {
          return;
        }
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
        sm__liveness_walk(args[i], layout, liveness, segment);
      }
      break;
    }

    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        sm__liveness_walk(node->data.block.commands[i], layout, liveness,
                          segment);
      }
      break;
    }

    case AST_INTERP_STRING: {
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        sm__liveness_walk(node->data.interp_string.segments[i], layout,
                          liveness, segment);
      }
      break;
    }

    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        sm__liveness_walk(node->data.break_stmt.value, layout, liveness,
                          segment);
      }
      break;
    }

    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        sm__liveness_walk(node->data.return_stmt.value, layout, liveness,
                          segment);
      }
      break;
    }

    case AST_SHELL_CMD: {
      /* Recurse into head and args */
      sm__liveness_walk(node->data.shell_cmd.head, layout, liveness, segment);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        sm__liveness_walk(node->data.shell_cmd.args[i], layout, liveness,
                          segment);
      }
      break;
    }

    case AST_SPREAD: {
      if (node->data.spread.expr) {
        sm__liveness_walk(node->data.spread.expr, layout, liveness, segment);
      }
      break;
    }

    default:
      break;
  }
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

  return analysis;
}

/* Check if an AST subtree contains any suspension points.
   When map is non-NULL, also checks if named proc calls are suspending. */
bool ast__contains_suspension(AstNode* node, SuspensionMap* map,
                               ThreadHeap* heap, JaclInternTable* intern_table) {
  if (!node) return false;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* name = head->data.lit_string.value;
        uint32_t len = head->data.lit_string.length;
        if ((len == 5 && memcmp(name, "await", 5) == 0) ||
            (len == 8 && memcmp(name, "parallel", 8) == 0) ||
            (len == 4 && memcmp(name, "race", 4) == 0) ||
            (len == 5 && memcmp(name, "yield", 5) == 0)) {
          return true;
        }
        /* Don't recurse into nested proc or spawn definitions
           (their block args are separate closure scopes) */
        if ((len == 4 && memcmp(name, "proc", 4) == 0) ||
            (len == 5 && memcmp(name, "spawn", 5) == 0)) {
          return false;
        }
        /* Check if this is a call to a known suspending proc.
           Generator calls return a stream immediately — they don't suspend. */
        if (map) {
          JaclVal name_val = compiler__name_val(heap, intern_table, name, len);
          if (suspension_map_lookup(map, name_val) &&
              !suspension_map_is_generator(map, name_val)) return true;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (ast__contains_suspension(node->data.command.args[i], map,
                                     heap, intern_table))
          return true;
      }
      return false;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        if (ast__contains_suspension(node->data.block.commands[i], map,
                                     heap, intern_table))
          return true;
      }
      return false;
    }
    case AST_BREAK: {
      if (node->data.break_stmt.value) {
        return ast__contains_suspension(node->data.break_stmt.value, map,
                                        heap, intern_table);
      }
      return false;
    }
    case AST_RETURN: {
      if (node->data.return_stmt.value) {
        return ast__contains_suspension(node->data.return_stmt.value, map,
                                        heap, intern_table);
      }
      return false;
    }
    case AST_SHELL_CMD: {
      /* Check head and args for suspension */
      if (ast__contains_suspension(node->data.shell_cmd.head, map,
                                   heap, intern_table))
        return true;
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        if (ast__contains_suspension(node->data.shell_cmd.args[i], map,
                                     heap, intern_table))
          return true;
      }
      return false;
    }
    default:
      return false;
  }
}

/* Check if an AST subtree contains any set! calls (mutable global mutation).
   Skips nested proc/spawn/parallel/race definitions since those are separate
   closure scopes with independent pinning decisions. */
/**
 * Collect all mut declaration names directly in this AST subtree.
 * Skips nested proc/spawn/parallel/race scopes (they are separate bodies).
 */
#define AST_LOCAL_MUTS_MAX 64
void ast__collect_local_muts(AstNode* node, JaclVal* names,
                                     uint32_t* count,
                                     ThreadHeap* heap,
                                     JaclInternTable* intern_table) {
  if (!node || *count >= AST_LOCAL_MUTS_MAX) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;
        /* Record mut declarations */
        if (hlen == 3 && memcmp(hname, "mut", 3) == 0) {
          uint32_t argc = node->data.command.arg_count;
          if (argc >= 2 && node->data.command.args[0]->type == AST_LIT_STRING) {
            AstNode* name_node = node->data.command.args[0];
            names[*count] = compiler__name_val(
                heap, intern_table,
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            (*count)++;
          }
          return;
        }
        /* Skip nested scope boundaries */
        if ((hlen == 4 && memcmp(hname, "proc", 4) == 0) ||
            (hlen == 5 && memcmp(hname, "spawn", 5) == 0) ||
            (hlen == 8 && memcmp(hname, "parallel", 8) == 0) ||
            (hlen == 4 && memcmp(hname, "race", 4) == 0)) {
          return;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        ast__collect_local_muts(node->data.command.args[i], names, count,
                                heap, intern_table);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        ast__collect_local_muts(node->data.block.commands[i], names, count,
                                heap, intern_table);
      }
      break;
    }
    case AST_SHELL_CMD: {
      /* Recurse into head and args */
      ast__collect_local_muts(node->data.shell_cmd.head, names, count,
                              heap, intern_table);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        ast__collect_local_muts(node->data.shell_cmd.args[i], names, count,
                                heap, intern_table);
      }
      break;
    }
    default:
      break;
  }
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
bool ast__contains_nonlocal_set_impl(AstNode* node,
                                             JaclVal* local_muts,
                                             uint32_t local_mut_count,
                                             ThreadHeap* heap,
                                             JaclInternTable* intern_table) {
  if (!node) return false;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* name = head->data.lit_string.value;
        uint32_t len = head->data.lit_string.length;
        if (len == 3 && memcmp(name, "set", 3) == 0) {
          /* Check if the target is a local mut */
          uint32_t argc = node->data.command.arg_count;
          if (argc >= 1 && node->data.command.args[0]->type == AST_LIT_STRING) {
            AstNode* target = node->data.command.args[0];
            JaclVal target_name = compiler__name_val(
                heap, intern_table,
                target->data.lit_string.value,
                target->data.lit_string.length);
            for (uint32_t i = 0; i < local_mut_count; i++) {
              if (local_muts[i] == target_name) return false; /* local mut */
            }
          }
          return true; /* non-local or unresolved — needs pinning */
        }
        /* Skip nested scope boundaries — they get their own pinning */
        if ((len == 4 && memcmp(name, "proc", 4) == 0) ||
            (len == 5 && memcmp(name, "spawn", 5) == 0) ||
            (len == 8 && memcmp(name, "parallel", 8) == 0) ||
            (len == 4 && memcmp(name, "race", 4) == 0)) {
          return false;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (ast__contains_nonlocal_set_impl(node->data.command.args[i],
                                             local_muts, local_mut_count,
                                             heap, intern_table))
          return true;
      }
      return false;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        if (ast__contains_nonlocal_set_impl(node->data.block.commands[i],
                                             local_muts, local_mut_count,
                                             heap, intern_table))
          return true;
      }
      return false;
    }
    case AST_SHELL_CMD: {
      /* Check head and args */
      if (ast__contains_nonlocal_set_impl(node->data.shell_cmd.head,
                                           local_muts, local_mut_count,
                                           heap, intern_table))
        return true;
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        if (ast__contains_nonlocal_set_impl(node->data.shell_cmd.args[i],
                                             local_muts, local_mut_count,
                                             heap, intern_table))
          return true;
      }
      return false;
    }
    default:
      return false;
  }
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
  uint32_t local_count_at_loop; /* local_count before loop locals were pushed */
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
  switch (len) {
    case 2: return memcmp(name, "if", 2) == 0 ||
                   memcmp(name, "to", 2) == 0;
    case 3: return memcmp(name, "def", 3) == 0 ||
                   memcmp(name, "mut", 3) == 0 ||
                   memcmp(name, "set", 3) == 0 ||
                   memcmp(name, "for", 3) == 0 ||
                   memcmp(name, "try", 3) == 0;
    case 4: return memcmp(name, "proc", 4) == 0;
    case 5: return memcmp(name, "while", 5) == 0 ||
                   memcmp(name, "break", 5) == 0 ||
                   memcmp(name, "match", 5) == 0 ||
                   memcmp(name, "quote", 5) == 0 ||
                   memcmp(name, "spawn", 5) == 0 ||
                   memcmp(name, "yield", 5) == 0 ||
                   memcmp(name, "await", 5) == 0;
    case 6: return memcmp(name, "return", 6) == 0;
    case 8: return memcmp(name, "defmacro", 8) == 0 ||
                   memcmp(name, "continue", 8) == 0 ||
                   memcmp(name, "parallel", 8) == 0;
    case 9: return memcmp(name, "defstruct", 9) == 0;
    case 12: return memcmp(name, "syntax-quote", 12) == 0;
    default: return false;
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
  JaclType         expected_type;   /* contextual type hint for RHS compilation */
  JaclType         last_expr_type;  /* type of the last compiled expression */
  uint32_t         last_struct_idx; /* struct type index when last_expr_type==TYPE_STRUCT */
  uint32_t         last_key_struct_idx; /* key struct type for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
#define CTX_STRUCT_PENDING (UINT32_MAX - 1) /* sentinel: ctx struct not yet finalized */
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
};

/* --- Phase 2 helper: compile a typed-collection element argument.
 * Pushes expected_type for scalar-typed targets so literal narrowing
 * fires, compiles the arg, restores expected_type, and returns true
 * iff the result type matches. Caller emits the error on false. */
void compiler__compile_node(Compiler* c, AstNode* node); /* fwd decl */

static bool compiler__compile_typed_elem_arg(Compiler* c, AstNode* arg,
                                             uint32_t expected_type_idx) {
  bool is_scalar = COMPILER_IS_SCALAR_TYPE_IDX(expected_type_idx);
  if (is_scalar) {
    c->expected_type = COMPILER_TYPE_IDX_TO_SCALAR(expected_type_idx);
  }
  compiler__compile_node(c, arg);
  c->expected_type = TYPE_DYN;
  if (is_scalar) {
    return c->last_expr_type == COMPILER_TYPE_IDX_TO_SCALAR(expected_type_idx);
  }
  return c->last_expr_type == TYPE_STRUCT && c->last_struct_idx == expected_type_idx;
}

/* --- TypeInfo accessors --- */

static inline TypeInfo compiler__get_type(Compiler* c) {
  return (TypeInfo){ c->last_expr_type, c->last_struct_idx, c->last_key_struct_idx };
}

static inline void compiler__set_type(Compiler* c, TypeInfo ti) {
  c->last_expr_type      = ti.type;
  c->last_struct_idx     = ti.struct_idx;
  c->last_key_struct_idx = ti.key_struct_idx;
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
  c->expected_type   = TYPE_DYN;
  compiler__set_type(c, (TypeInfo){ TYPE_DYN, UINT32_MAX, UINT32_MAX });
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

/* --- Internal: Struct type registry access --- */

StructTypeRegistry* compiler__get_struct_registry(Compiler* c) {
  Compiler* root = c;
  while (root->enclosing) root = root->enclosing;
  return root->struct_registry;
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
void ast__collect_local_names(AstNode* node, JaclVal* names,
                                      uint32_t* count,
                                      ThreadHeap* heap,
                                      JaclInternTable* intern_table) {
  if (!node || *count >= AST_LOCAL_NAMES_MAX) return;

  switch (node->type) {
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;
        /* Record def and mut declarations */
        if ((hlen == 3 && memcmp(hname, "def", 3) == 0) ||
            (hlen == 3 && memcmp(hname, "mut", 3) == 0)) {
          uint32_t argc = node->data.command.arg_count;
          if (argc >= 2 && node->data.command.args[0]->type == AST_LIT_STRING) {
            AstNode* name_node = node->data.command.args[0];
            names[*count] = compiler__name_val(
                heap, intern_table,
                name_node->data.lit_string.value,
                name_node->data.lit_string.length);
            (*count)++;
          }
          return;
        }
        /* Skip nested scope boundaries */
        if ((hlen == 4 && memcmp(hname, "proc", 4) == 0) ||
            (hlen == 5 && memcmp(hname, "spawn", 5) == 0) ||
            (hlen == 8 && memcmp(hname, "parallel", 8) == 0) ||
            (hlen == 4 && memcmp(hname, "race", 4) == 0)) {
          return;
        }
      }
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        ast__collect_local_names(node->data.command.args[i], names, count,
                                 heap, intern_table);
      }
      break;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        ast__collect_local_names(node->data.block.commands[i], names, count,
                                 heap, intern_table);
      }
      break;
    }
    case AST_SHELL_CMD: {
      /* Recurse into head and args */
      ast__collect_local_names(node->data.shell_cmd.head, names, count,
                               heap, intern_table);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        ast__collect_local_names(node->data.shell_cmd.args[i], names, count,
                                 heap, intern_table);
      }
      break;
    }
    default:
      break;
  }
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

bool ast__refs_nonlocal_mutable_impl(AstNode* node,
                                             JaclVal* local_names,
                                             uint32_t local_name_count,
                                             Compiler* enclosing) {
  if (!node) return false;

  switch (node->type) {
    case AST_VAR_REF: {
      uint32_t len = node->data.var_ref.length;
      if (len > 128) return false;
      JaclVal name = compiler__name_val(enclosing->heap, enclosing->intern_table, node->data.var_ref.name, len);
      /* Check if locally declared in the body */
      for (uint32_t i = 0; i < local_name_count; i++) {
        if (local_names[i] == name) return false;
      }
      /* Not local — check if mutable or captures_mutable in enclosing scope */
      return compiler__name_touches_mutable(enclosing, name);
    }
    case AST_COMMAND: {
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING) {
        const char* hname = head->data.lit_string.value;
        uint32_t hlen = head->data.lit_string.length;
        /* Skip nested concurrent scopes — they get their own pinning */
        if ((hlen == 5 && memcmp(hname, "spawn", 5) == 0) ||
            (hlen == 8 && memcmp(hname, "parallel", 8) == 0) ||
            (hlen == 4 && memcmp(hname, "race", 4) == 0)) {
          return false;
        }
        /* Check if function call target is a non-local closure that
           transitively captures mutable state (US-003). */
        if (hlen <= 128) {
          JaclVal fname = compiler__name_val(enclosing->heap, enclosing->intern_table, hname, hlen);
          bool is_local_name = false;
          for (uint32_t i = 0; i < local_name_count; i++) {
            if (local_names[i] == fname) { is_local_name = true; break; }
          }
          if (!is_local_name &&
              compiler__name_touches_mutable(enclosing, fname))
            return true;
        }
      }
      /* Check head (for $var calls) */
      if (ast__refs_nonlocal_mutable_impl(head, local_names,
                                           local_name_count, enclosing))
        return true;
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (ast__refs_nonlocal_mutable_impl(node->data.command.args[i],
                                             local_names, local_name_count,
                                             enclosing))
          return true;
      }
      return false;
    }
    case AST_BLOCK: {
      for (uint32_t i = 0; i < node->data.block.count; i++) {
        if (ast__refs_nonlocal_mutable_impl(node->data.block.commands[i],
                                             local_names, local_name_count,
                                             enclosing))
          return true;
      }
      return false;
    }
    case AST_SHELL_CMD: {
      /* Check head and args */
      if (ast__refs_nonlocal_mutable_impl(node->data.shell_cmd.head,
                                           local_names, local_name_count,
                                           enclosing))
        return true;
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        if (ast__refs_nonlocal_mutable_impl(node->data.shell_cmd.args[i],
                                             local_names, local_name_count,
                                             enclosing))
          return true;
      }
      return false;
    }
    default:
      return false;
  }
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
    c->expected_type = c->return_type;
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
  if (is_unboxed_type(c->last_expr_type)) {
    compiler__emit_byte(c, OP_TO_DYN, line);
    compiler__emit_byte(c, (uint8_t)c->last_expr_type, line);
    c->last_expr_type = TYPE_DYN;
  }
  /* Struct values never reach here in valid code: every consumer that
     would otherwise need a JaclVal-shaped struct now has a typed inline
     path (OP_PRINT_STRUCT, OP_STRUCT_EQ_TOS, OP_STRUCT_NEW_INLINE
     consuming inline args, etc.) or a compile-time rejection. */
}

/* --- Internal: Reject bare struct in dyn context (compile-time error) --- */

static bool compiler__reject_bare_typed(Compiler* c, uint32_t line, uint32_t col,
                                        const char* context) {
  if (c->last_expr_type == TYPE_STRUCT || is_typed_collection(c->last_expr_type)) {
    char err_msg[128];
    snprintf(err_msg, sizeof(err_msg),
             "cannot store bare %s in %s; use [box ...] to box it",
             type_name(c->last_expr_type), context);
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
  /* Phase 3c: read LHS type from the typer pass's pre-computed
   * annotation on the AST instead of walking LHS first to discover it.
   * Fall back to c->last_expr_type for any node the typer left as
   * TYPE_DYN (typer gaps) — the fallback can be removed once the typer
   * covers all relevant shapes. */
  JaclType lhs_type = (JaclType)args[0]->inferred_type;

  /* Compile LHS — caller already reset expected_type at command entry. */
  compiler__compile_node(c, args[0]);
  if (lhs_type == TYPE_DYN) lhs_type = c->last_expr_type;

  /* Set contextual type for RHS so int/float literals on the RHS narrow
   * to the LHS's type. (Compiling LHS may have left expected_type at
   * TYPE_DYN even though we knew the type up front, so set it here.) */
  if (lhs_type != TYPE_DYN) {
    c->expected_type = lhs_type;
  }

  /* Compile RHS */
  compiler__compile_node(c, args[1]);
  JaclType rhs_type = (JaclType)args[1]->inferred_type;
  if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;
  c->expected_type = TYPE_DYN;

  /* Static typing for struct comparisons */
  if (lhs_type == TYPE_STRUCT || rhs_type == TYPE_STRUCT) {
    if (lhs_type != rhs_type || c->last_struct_idx == UINT32_MAX) {
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
    compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
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
      compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
      if (lhs_type == TYPE_TYPED_MAP)
        compiler__emit_u16(c, (uint16_t)c->last_key_struct_idx, line);
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
  TypeInfo col_ti = compiler__get_type(c);
  JaclType col_type = col_ti.type;
  uint32_t col_struct_idx = col_ti.struct_idx;
  uint32_t col_key_struct_idx = col_ti.key_struct_idx;
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
      compiler__set_type(c, (TypeInfo){ col_type, col_struct_idx, col_key_struct_idx });
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
            if (d_types && d_types[i]) {
              JaclType t;
              if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
                c->locals[c->local_count - 1].type = t;
              }
            }
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
          if (d_types && d_types[i]) {
            JaclType t;
            if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
              c->locals[c->local_count - 1].type = t;
            }
          }
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
      if (is_mutable) {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t j = 0; j < root->global_arity_count; j++) {
          if (root->global_arities[j].name == rest_val) {
            root->global_arities[j].is_mutable = true;
            break;
          }
        }
      }
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
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
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
          if (d_types && d_types[i]) {
            JaclType t;
            if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
              c->locals[c->local_count - 1].type = t;
            }
          }
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
          if (d_types && d_types[i]) {
            JaclType t;
            if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
              c->locals[c->local_count - 1].type = t;
            }
          }
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
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
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
  /* Phase 3c: read result type from the typer's pre-computed AST
   * annotation; fall back to c->last_expr_type for typer gaps. */
  JaclType rhs_type = (JaclType)value_expr->inferred_type;
  if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;
  uint32_t rhs_struct_idx = c->last_struct_idx;

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
        if (d_types && d_types[i]) {
          JaclType t;
          if (compiler__resolve_type(c, d_types[i], d_type_lens[i], &t)) {
            c->locals[c->local_count - 1].type = t;
          }
        }
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
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
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
      if (is_mutable) {
        Compiler* root = c;
        while (root->enclosing) root = root->enclosing;
        for (uint32_t j = 0; j < root->global_arity_count; j++) {
          if (root->global_arities[j].name == rest_val) {
            root->global_arities[j].is_mutable = true;
            break;
          }
        }
      }
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
        if (is_mutable) {
          Compiler* root = c;
          while (root->enclosing) root = root->enclosing;
          for (uint32_t j = 0; j < root->global_arity_count; j++) {
            if (root->global_arities[j].name == name_val) {
              root->global_arities[j].is_mutable = true;
              break;
            }
          }
        }
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
 *   lines, stream_next
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
  "lines", "stream_next",
  "exec", "signal", "cancel",
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
  uint32_t argc = node->data.command.arg_count;
  AstNode** args = node->data.command.args;
  uint32_t line = node->start.line;
  uint32_t col  = node->start.column;

  /* Reset expected_type so sub-expressions don't inherit parent context.
     Individual handlers (e.g. typed def) set it explicitly for their RHS. */
  c->expected_type = TYPE_DYN;

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
      /* Compile each element with declared type as expected_type so
       * literals narrow correctly. Verify resulting type matches. */
      for (uint32_t i = 0; i < argc; i++) {
        c->expected_type = elem_t;
        compiler__compile_node(c, args[i]);
        c->expected_type = TYPE_DYN;
        if (c->last_expr_type != elem_t) {
          char err[160];
          snprintf(err, sizeof(err),
                   "[Vec %.*s]: element %u is not a %.*s value (got %s)",
                   (int)type_name_len, type_name_str, i,
                   (int)type_name_len, type_name_str,
                   type_name(c->last_expr_type));
          compiler__error(c, line, col, err);
          return;
        }
      }
      compiler__emit_byte(c, OP_TYPED_VEC, line);
      compiler__emit_u16(c, (uint16_t)COMPILER_SCALAR_TYPE_IDX(elem_t), line);
      compiler__emit_byte(c, (uint8_t)argc, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      c->last_struct_idx = COMPILER_SCALAR_TYPE_IDX(elem_t);
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
      if (c->last_expr_type != TYPE_STRUCT || c->last_struct_idx != type_idx) {
        char err[128];
        snprintf(err, sizeof(err),
                 "[Vec %.*s]: element %u is not a %.*s struct",
                 (int)type_name_len, type_name_str, i,
                 (int)type_name_len, type_name_str);
        compiler__error(c, line, col, err);
        return;
      }
      /* OP_TYPED_VEC consumes inline struct bytes directly via vm__pop_struct. */
    }
    compiler__emit_byte(c, OP_TYPED_VEC, line);
    compiler__emit_u16(c, (uint16_t)type_idx, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    c->last_expr_type = TYPE_TYPED_VEC;
    c->last_struct_idx = type_idx;
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
        c->expected_type = val_t;
        compiler__compile_node(c, args[i * 2 + 1]); /* value: must match scalar */
        c->expected_type = TYPE_DYN;
        if (c->last_expr_type != val_t) {
          char err[160];
          snprintf(err, sizeof(err),
                   "[Map %.*s]: value %u is not a %.*s value (got %s)",
                   (int)type_name_len, type_name_str, i,
                   (int)type_name_len, type_name_str,
                   type_name(c->last_expr_type));
          compiler__error(c, line, col, err);
          return;
        }
      }
      compiler__emit_byte(c, OP_TYPED_MAP, line);
      compiler__emit_u16(c, (uint16_t)val_type_idx, line);
      compiler__emit_u16(c, (uint16_t)0xFFFF, line);  /* dyn keys */
      compiler__emit_byte(c, (uint8_t)pair_count, line);
      compiler__set_type(c, (TypeInfo){ TYPE_TYPED_MAP, val_type_idx, UINT32_MAX });
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
      if (c->last_expr_type != TYPE_STRUCT || c->last_struct_idx != type_idx) {
        char err[128];
        snprintf(err, sizeof(err),
                 "[Map %.*s]: value %u is not a %.*s struct",
                 (int)type_name_len, type_name_str, i,
                 (int)type_name_len, type_name_str);
        compiler__error(c, line, col, err);
        return;
      }
      /* OP_TYPED_MAP consumes inline struct values directly via vm__pop_struct. */
    }
    compiler__emit_byte(c, OP_TYPED_MAP, line);
    compiler__emit_u16(c, (uint16_t)type_idx, line);
    compiler__emit_u16(c, (uint16_t)0xFFFF, line);  /* key_type_idx: dyn keys */
    compiler__emit_byte(c, (uint8_t)pair_count, line);
    compiler__set_type(c, (TypeInfo){ TYPE_TYPED_MAP, type_idx, UINT32_MAX });
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
      if (key_is_scalar) c->expected_type = key_t;
      compiler__compile_node(c, args[i * 2]);
      c->expected_type = TYPE_DYN;
      bool k_ok = key_is_scalar
        ? (c->last_expr_type == key_t)
        : (c->last_expr_type == TYPE_STRUCT && c->last_struct_idx == key_type_idx);
      if (!k_ok) {
        char err[160];
        snprintf(err, sizeof(err),
                 "[Map %.*s %.*s]: key %u is not a %.*s",
                 (int)key_name_len, key_name_str,
                 (int)val_name_len, val_name_str, i,
                 (int)key_name_len, key_name_str);
        compiler__error(c, line, col, err);
        return;
      }
      /* value */
      if (val_is_scalar) c->expected_type = val_t;
      compiler__compile_node(c, args[i * 2 + 1]);
      c->expected_type = TYPE_DYN;
      bool v_ok = val_is_scalar
        ? (c->last_expr_type == val_t)
        : (c->last_expr_type == TYPE_STRUCT && c->last_struct_idx == val_type_idx);
      if (!v_ok) {
        char err[160];
        snprintf(err, sizeof(err),
                 "[Map %.*s %.*s]: value %u is not a %.*s",
                 (int)key_name_len, key_name_str,
                 (int)val_name_len, val_name_str, i,
                 (int)val_name_len, val_name_str);
        compiler__error(c, line, col, err);
        return;
      }
    }
    compiler__emit_byte(c, OP_TYPED_MAP, line);
    compiler__emit_u16(c, (uint16_t)val_type_idx, line);
    compiler__emit_u16(c, (uint16_t)key_type_idx, line);
    compiler__emit_byte(c, (uint8_t)pair_count, line);
    compiler__set_type(c, (TypeInfo){ TYPE_TYPED_MAP, val_type_idx, key_type_idx });
    return;
  }

  /* --- Spread call path: handles both builtins and user procs --- */
  if (has_spread) {
    /* Check for known binary builtins → use OP_FOLD_SPREAD */
    int fold_op = -1;
    if (compiler__head_matches(head, "+", 1))      fold_op = 0;
    else if (compiler__head_matches(head, "*", 1)) fold_op = 2;
    else if (compiler__head_matches(head, "-", 1)) fold_op = 1;
    else if (compiler__head_matches(head, "/", 1)) fold_op = 3;

    /* vec with spread args → OP_VEC_SPREAD */
    if (compiler__head_matches(head, "vec", 3)) {
      uint8_t fixed_args = 0;
      uint8_t num_spreads = 0;
      for (uint32_t i = 0; i < argc; i++) {
        if (args[i]->type == AST_SPREAD) {
          compiler__compile_node(c, args[i]->data.spread.expr);
          compiler__emit_byte(c, OP_SPREAD, line);
          num_spreads++;
        } else {
          compiler__compile_node(c, args[i]);
          if (compiler__reject_bare_typed(c, line, col, "dyn vec")) return;
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
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
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
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
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
  if (compiler__head_matches(head, "=", 1)) {
    if (argc != 2) {
      compiler__error(c, line, col, "'=' requires exactly 2 operands");
      return;
    }
    compiler__rewrite_binding_op(c, node, "def", 3);
    return;
  }

  /* : → mut (mutable binding) */
  if (compiler__head_matches(head, ":", 1)) {
    if (argc != 2) {
      compiler__error(c, line, col, "':' requires exactly 2 operands");
      return;
    }
    compiler__rewrite_binding_op(c, node, "mut", 3);
    return;
  }

  /* :: → set (reassignment) */
  if (compiler__head_matches(head, "::", 2)) {
    if (argc != 2) {
      compiler__error(c, line, col, "'::' requires exactly 2 operands");
      return;
    }
    compiler__rewrite_binding_op(c, node, "set", 3);
    return;
  }

  /* | → pipe threading */
  if (compiler__head_matches(head, "|", 1)) {
    if (argc != 2) {
      compiler__error(c, line, col, "'|' requires exactly 2 operands");
      return;
    }
    compiler__compile_pipe_op(c, node);
    return;
  }

  /* && → short-circuit logical AND: if LHS { RHS } { false } */
  if (compiler__head_matches(head, "&&", 2)) {
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
  if (compiler__head_matches(head, "||", 2)) {
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
  if (compiler__head_matches(head, "~", 1) && argc == 1) {
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

  /* Arithmetic builtins */
  if (compiler__head_matches(head, "+", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "+", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_ADD, "add", line, col);
    return;
  }
  if (compiler__head_matches(head, "-", 1)) {
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
      /* Phase 3c: read result type from the typer's pre-computed AST
       * annotation; fall back to c->last_expr_type for typer gaps. */
      JaclType arg_type = (JaclType)args[0]->inferred_type;
      if (arg_type == TYPE_DYN) arg_type = c->last_expr_type;
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
  if (compiler__head_matches(head, "*", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "*", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_MUL, "multiply", line, col);
    return;
  }
  if (compiler__head_matches(head, "/", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "/", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_DIV, "divide", line, col);
    return;
  }
  if (compiler__head_matches(head, "%", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "%", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_MOD, "modulo", line, col);
    return;
  }

  /* Comparison builtins */
  if (compiler__head_matches(head, "==", 2)) {
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
          c->last_expr_type = TYPE_DYN;
          return;
        }
      }
    }
    compiler__compile_binary(c, args, OP_EQ, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, "<", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "<", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_LT, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, ">", 1)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, ">", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_GT, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, "<=", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "<=", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_LE, "compare", line, col);
    return;
  }
  if (compiler__head_matches(head, ">=", 2)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, ">=", "2 arguments", argc); return; }
    compiler__compile_binary(c, args, OP_GE, "compare", line, col);
    return;
  }

  /* Range operators: ..< (exclusive) and ..= (inclusive) */
  if (compiler__head_matches(head, "..<", 3)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "..<", "2 arguments", argc); return; }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_RANGE, line);
    compiler__emit_byte(c, 0, line); /* 0 = exclusive */
    c->last_expr_type = TYPE_STREAM;
    return;
  }
  if (compiler__head_matches(head, "..=", 3)) {
    if (argc != 2) { compiler__builtin_arity_error(c, line, col, "..=", "2 arguments", argc); return; }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_RANGE, line);
    compiler__emit_byte(c, 1, line); /* 1 = inclusive */
    c->last_expr_type = TYPE_STREAM;
    return;
  }

  /* Print builtin */
  if (compiler__head_matches(head, "print", 5)) {
    if (argc != 1) { compiler__builtin_arity_error(c, line, col, "print", "1 argument", argc); return; }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_VEC) {
      compiler__emit_byte(c, OP_TYPED_VEC_PRINT, line);
      compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
      c->last_expr_type = TYPE_NIL;
      return;
    }
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      compiler__emit_byte(c, OP_TYPED_MAP_PRINT, line);
      compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
      compiler__emit_u16(c, (uint16_t)c->last_key_struct_idx, line);
      c->last_expr_type = TYPE_NIL;
      return;
    }
    if (c->last_expr_type == TYPE_STRUCT && c->last_struct_idx != UINT32_MAX) {
      /* Typed struct print — no heap reify, formatter walks inline bytes. */
      compiler__emit_byte(c, OP_PRINT_STRUCT, line);
      compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
      c->inline_repr = INLINE_NONE;
      c->last_expr_type = TYPE_NIL;
      return;
    }
    compiler__ensure_boxed(c, line);
    compiler__emit_byte(c, OP_PRINT, line);
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* length builtin */
  if (compiler__head_matches(head, "length", 6)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "length", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_STR_LEN, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* byte-length builtin */
  if (compiler__head_matches(head, "byte-length", 11)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "byte-length", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_STR_BYTE_LEN, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* index builtin */
  if (compiler__head_matches(head, "index", 5)) {
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
  if (compiler__head_matches(head, "slice", 5)) {
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
  if (compiler__head_matches(head, "concat", 6)) {
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
  if (compiler__head_matches(head, "mut", 3)) {
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
    c->expected_type = declared_type;
    compiler__compile_node(c, args[value_arg_idx]);
    c->expected_type = TYPE_DYN;
    /* Phase 3c: read result type from the typer's pre-computed AST
     * annotation; fall back to c->last_expr_type for typer gaps. */
    JaclType rhs_type = (JaclType)args[value_arg_idx]->inferred_type;
    if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;

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
      snprintf(err_msg, sizeof(err_msg),
               "type error: cannot assign dyn to %s binding — use [to %s $val] to cast",
               type_name(declared_type), type_name(declared_type));
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
      { TypeInfo ti = compiler__get_type(c); ti.type = effective_type;
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
            { TypeInfo ti = compiler__get_type(c); ti.type = effective_type;
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
  if (compiler__head_matches(head, "set", 3)) {
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
        c->expected_type = target_type;
        compiler__compile_node(c, args[1]);
        c->expected_type = TYPE_DYN;
        /* Phase 3c: read result type from the typer's pre-computed AST
         * annotation; fall back to c->last_expr_type for typer gaps. */
        JaclType rhs_type = (JaclType)args[1]->inferred_type;
        if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;
        /* Type check */
        if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign %s to %s binding '%.*s'",
                   type_name(rhs_type), type_name(target_type),
                   (int)name_len, set_name_ptr);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign dyn to %s binding '%.*s'",
                   type_name(target_type),
                   (int)name_len, set_name_ptr);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type == TYPE_DYN && rhs_type == TYPE_STRUCT) {
          snprintf(err_msg, sizeof(err_msg),
                   "cannot assign struct value to dyn binding '%.*s' — "
                   "wrap with [box $val]",
                   (int)name_len, set_name_ptr);
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
        c->expected_type = target_type;
        compiler__compile_node(c, args[1]);
        c->expected_type = TYPE_DYN;
        /* Phase 3c: read result type from the typer's pre-computed AST
         * annotation; fall back to c->last_expr_type for typer gaps. */
        JaclType rhs_type = (JaclType)args[1]->inferred_type;
        if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;
        /* Type check */
        if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign %s to %s binding '%.*s'",
                   type_name(rhs_type), type_name(target_type),
                   (int)name_len, set_name_ptr);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
          snprintf(err_msg, sizeof(err_msg),
                   "type error: cannot assign dyn to %s binding '%.*s'",
                   type_name(target_type),
                   (int)name_len, set_name_ptr);
          compiler__error(c, line, col, err_msg);
          return;
        }
        if (target_type == TYPE_DYN && rhs_type == TYPE_STRUCT) {
          snprintf(err_msg, sizeof(err_msg),
                   "cannot assign struct value to dyn binding '%.*s' — "
                   "wrap with [box $val]",
                   (int)name_len, set_name_ptr);
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
          compiler__emit_byte(c, OP_GET_GLOBAL, line);
          compiler__emit_u16(c, name_idx, line);
          c->expected_type = target_type;
          compiler__compile_node(c, args[1]);
          c->expected_type = TYPE_DYN;
          /* Phase 3c: read result type from the typer's pre-computed AST
           * annotation; fall back to c->last_expr_type for typer gaps. */
          JaclType rhs_type = (JaclType)args[1]->inferred_type;
          if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;
          if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign %s to %s binding '%.*s'",
                     type_name(rhs_type), type_name(target_type),
                     (int)name_len, set_name_ptr);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign dyn to %s binding '%.*s'",
                     type_name(target_type),
                     (int)name_len, set_name_ptr);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (is_unboxed_type(target_type)) {
            compiler__emit_byte(c, OP_TO_DYN, line);
            compiler__emit_byte(c, (uint8_t)target_type, line);
          }
          if (rhs_type == TYPE_STRUCT && c->last_struct_idx != UINT32_MAX) {
            /* Struct-box reset: inline bytes write directly. */
            compiler__emit_byte(c, OP_RESET_INLINE, line);
            compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
          } else {
            compiler__emit_byte(c, OP_RESET, line);
          }
        } else {
          c->expected_type = target_type;
          compiler__compile_node(c, args[1]);
          c->expected_type = TYPE_DYN;
          /* Phase 3c: read result type from the typer's pre-computed AST
           * annotation; fall back to c->last_expr_type for typer gaps. */
          JaclType rhs_type = (JaclType)args[1]->inferred_type;
          if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;
          /* Type check */
          if (target_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != target_type) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign %s to %s binding '%.*s'",
                     type_name(rhs_type), type_name(target_type),
                     (int)name_len, set_name_ptr);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type != TYPE_DYN && rhs_type == TYPE_DYN) {
            snprintf(err_msg, sizeof(err_msg),
                     "type error: cannot assign dyn to %s binding '%.*s'",
                     type_name(target_type),
                     (int)name_len, set_name_ptr);
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (target_type == TYPE_DYN && rhs_type == TYPE_STRUCT) {
            snprintf(err_msg, sizeof(err_msg),
                     "cannot assign struct value to dyn binding '%.*s' — "
                     "wrap with [box $val]",
                     (int)name_len, set_name_ptr);
            compiler__error(c, line, col, err_msg);
            return;
          }
          /* Box unboxed types for global storage */
          if (is_unboxed_type(target_type)) {
            compiler__emit_byte(c, OP_TO_DYN, line);
            compiler__emit_byte(c, (uint8_t)target_type, line);
          }
          uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
          compiler__emit_byte(c, OP_SET_GLOBAL, line);
          compiler__emit_u16(c, name_idx, line);
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
  if (compiler__head_matches(head, "def", 3)) {
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
    c->expected_type = declared_type;
    compiler__compile_node(c, args[value_arg_idx]);
    c->expected_type = TYPE_DYN;
    /* Phase 3c: read result type from the typer's pre-computed AST
     * annotation; fall back to c->last_expr_type for typer gaps. */
    JaclType rhs_type = (JaclType)args[value_arg_idx]->inferred_type;
    if (rhs_type == TYPE_DYN) rhs_type = c->last_expr_type;

    /* US-007: activate inline for function calls returning struct types.
     * If the RHS isn't already inline (struct constructor or inline get) but
     * returns a struct type with a known struct index, post-activate inline
     * and plan to emit OP_STRUCT_STORE_INLINE after adding the local. */
    bool needs_store_inline = false;
    if (!activate_inline && rhs_type == TYPE_STRUCT &&
        c->last_struct_idx != UINT32_MAX &&
        c->scope_depth > 0 && !c->sm_analysis) {
      /* US-015: only inline value-type structs; legacy structs use heap */
      StructTypeRegistry* reg2 = compiler__get_struct_registry(c);
      if (reg2 && c->last_struct_idx < reg2->count) {
        StructTypeDef* ret_sdef = reg2->defs[c->last_struct_idx];
        if (struct_def_is_user(ret_sdef, reg2)) {
          activate_inline = true;
          /* If RHS already pushed inline slots (INLINE_STACK or INLINE_REF
             from a chained nested-struct access), no de-materialization
             needed — slots ARE the local. */
          needs_store_inline = (c->inline_repr == INLINE_NONE);
        }
      }
    }

    /* Type check for typed def */
    if (declared_type != TYPE_DYN && rhs_type != TYPE_DYN && rhs_type != declared_type) {
      char err_msg[128];
      snprintf(err_msg, sizeof(err_msg), "type error: expected %s, got %s",
               type_name(declared_type), type_name(rhs_type));
      compiler__error(c, line, col, err_msg);
      return;
    }
    if (declared_type != TYPE_DYN && rhs_type == TYPE_DYN) {
      char err_msg[160];
      snprintf(err_msg, sizeof(err_msg),
               "type error: cannot assign dyn to %s binding — use [to %s $val] to cast",
               type_name(declared_type), type_name(declared_type));
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
    } else if (c->scope_depth > 0) {
      /* Local variable: value is on stack as the local slot.
         US-013: use the name arg's scope mark (0 for ^caret, else the
         def command's stamped mark) so caret bindings land in the
         caller's scope while normal macro bindings stay hygienic. */
      {
        uint32_t prev_mark = c->current_scope_mark;
        c->current_scope_mark = bind_scope_mark;
        compiler__add_local(c, name_val, line, col);
        c->current_scope_mark = prev_mark;
      }
      c->locals[c->local_count - 1].known_arity = rhs_arity;
      { TypeInfo ti = compiler__get_type(c); ti.type = effective_type;
        TYPEINFO_SAVE(c->locals[c->local_count - 1], ti); }
      if (effective_type == TYPE_STRUCT) {
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        uint32_t width = struct__slot_width(reg, c->last_struct_idx);
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
            compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
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
          c->last_struct_idx != UINT32_MAX &&
          c->last_struct_idx != CTX_STRUCT_PENDING) {
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
            { TypeInfo ti = compiler__get_type(c); ti.type = effective_type;
              TYPEINFO_SAVE(root->global_arities[i], ti); }
            break;
          }
        }
      }
    }
    c->last_expr_type = TYPE_NIL;
    return;
  }

  /* proc definition */
  if (compiler__head_matches(head, "proc", 4)) {
    /* Disambiguate: 4 args + first is type keyword → has return type.
       3 args → no return type (existing). */
    JaclType proc_return_type = TYPE_DYN;
    uint32_t proc_return_struct_idx = UINT32_MAX;
    uint32_t name_arg_idx, params_arg_idx, body_arg_idx;

    if (argc == 4) {
      /* [proc TYPE name params body] */
      if (args[0]->type != AST_LIT_STRING ||
          !compiler__resolve_type(c, args[0]->data.lit_string.value,
                                  args[0]->data.lit_string.length,
                                  &proc_return_type)) {
        compiler__error(c, line, col,
            "proc with 4 arguments requires type keyword as first argument");
        return;
      }
      if (proc_return_type == TYPE_STRUCT) {
        StructTypeRegistry* reg = compiler__get_struct_registry(c);
        if (reg) {
          proc_return_struct_idx = struct_registry__find(reg,
              args[0]->data.lit_string.value,
              args[0]->data.lit_string.length);
        }
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
      {
        AstNode* body_block = args[body_arg_idx];
        uint32_t stmt_count = body_block->data.block.count;
        AstNode** body_stmts = body_block->data.block.commands;
        compiler__compile_sm_stmts(&body_compiler, body_stmts, stmt_count,
                                    line, !has_yield);
      }
    } else {
      /* Normal non-suspending proc */
      compiler__compile_block_expr(&body_compiler, args[body_arg_idx]);

      /* Return type checking: body's last expression type must match declared */
      if (proc_return_type != TYPE_DYN) {
        JaclType body_type = body_compiler.last_expr_type;
        if (body_type != TYPE_DYN && body_type != proc_return_type) {
          char err_msg[128];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: proc %.*s declared return type %s, but body returns %s",
                   (int)proc_name_len, proc_name,
                   type_name(proc_return_type), type_name(body_type));
          compiler__error(c, line, col, err_msg);
        }
      } else if (body_compiler.last_expr_type == TYPE_STRUCT) {
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
  if (compiler__head_matches(head, "if", 2)) {
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

    /* Save then-branch type for unification.
     * Phase 3c: read from the typer's pre-computed AST annotation;
     * fall back to c->last_expr_type for typer gaps. */
    JaclType then_type = (JaclType)args[1]->inferred_type;
    if (then_type == TYPE_DYN) then_type = c->last_expr_type;
    uint32_t then_struct_idx = c->last_struct_idx;

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
      /* Phase 3c: read from typer's pre-computed AST annotation. */
      else_type = (JaclType)args[2]->inferred_type;
      if (else_type == TYPE_DYN) else_type = c->last_expr_type;
    } else {
      /* No else: push nil */
      compiler__emit_byte(c, OP_NIL, line);
      else_type = TYPE_NIL;
    }

    /* Patch JUMP to here */
    compiler__patch_jump(c, else_jump);

    /* Type unification: preserve type if both branches agree */
    if (then_type == else_type) {
      if (then_type != TYPE_STRUCT || then_struct_idx == c->last_struct_idx) {
        c->last_expr_type = then_type;
        c->last_struct_idx = then_struct_idx;
      } else {
        c->last_expr_type = TYPE_DYN;
      }
    } else {
      c->last_expr_type = TYPE_DYN;
    }
    return;
  }

  /* while loop */
  if (compiler__head_matches(head, "while", 5)) {
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
    return;
  }

  /* for — collection-based iteration with inlined body
     Forms:
       [for {init; cond; step} { body }]   — C-style counted loop
       [for $collection { body }]           — implicit $it binding
       [for $collection name { body }]      — explicit binding
       [for $collection $callback]           — HOF via OP_EACH
  */
  if (compiler__head_matches(head, "for", 3)) {
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

      /* Begin scope for init variable(s) — not visible after loop */
      compiler__begin_scope(c);
      uint32_t saved_local_count = c->local_count;

      /* Compile init (runs once before the loop) */
      compiler__compile_node(c, init_node);
      compiler__emit_check_error(c, line);

      /* Push loop context — is_for_loop=true for forward-jump continue */
      LoopContext* lctx = &c->loop_stack[c->loop_depth++];
      lctx->break_patch_count    = 0;
      lctx->continue_patch_count = 0;
      lctx->local_count_at_loop  = saved_local_count;
      lctx->is_for_loop          = true;

      /* Loop start: condition check */
      uint32_t loop_start = c->chunk->code_count;
      lctx->loop_start = loop_start;

      /* Compile condition */
      compiler__compile_node(c, cond_node);

      /* JUMP_IF_FALSE → exit */
      uint32_t exit_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

      /* Compile body statements inline */
      uint32_t body_count = body_block->data.block.count;
      for (uint32_t i = 0; i < body_count; i++) {
        compiler__compile_node(c, body_block->data.block.commands[i]);
        compiler__emit_check_error(c, line);
      }

      /* Continue target: patch all continue forward jumps here */
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

      /* End scope: pop init variable(s) */
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
    /* Phase 3c: read result type from the typer's pre-computed AST
     * annotation; fall back to c->last_expr_type for typer gaps. */
    JaclType col_type = (JaclType)args[0]->inferred_type;
    if (col_type == TYPE_DYN) col_type = c->last_expr_type;
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

      /* Push loop context */
      LoopContext* lctx = &c->loop_stack[c->loop_depth++];
      lctx->break_patch_count = 0;
      lctx->continue_patch_count = 0;
      lctx->local_count_at_loop = saved_local_count;
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
    uint32_t elem_struct_idx = c->last_struct_idx;

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

    /* Push loop context */
    LoopContext* lctx = &c->loop_stack[c->loop_depth++];
    lctx->break_patch_count = 0;
    lctx->continue_patch_count = 0;
    lctx->local_count_at_loop = saved_local_count;
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
  if (compiler__head_matches(head, "break", 5)) {
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
  if (compiler__head_matches(head, "continue", 8)) {
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
      /* For-loop: forward-jump to increment code (patched later) */
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
  if (compiler__head_matches(head, "return", 6)) {
    if (argc > 1) {
      compiler__builtin_arity_error(c, line, col, "return", "0 or 1 arguments", argc);
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
  if (compiler__head_matches(head, "try", 3)) {
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
  if (compiler__head_matches(head, "with-ctx", 8)) {
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
      c->expected_type = cf->type;
      compiler__compile_node(c, override_cmd->data.command.args[0]);
      c->expected_type = TYPE_DYN;
      /* Phase 3c: read result type from the typer's pre-computed AST
       * annotation; fall back to c->last_expr_type for typer gaps. */
      JaclType val_type = (JaclType)override_cmd->data.command.args[0]->inferred_type;
      if (val_type == TYPE_DYN) val_type = c->last_expr_type;
      if (cf->type != TYPE_DYN && val_type != TYPE_DYN && val_type != cf->type) {
        char err[192];
        snprintf(err, sizeof(err),
                 "type error: field '%.*s' of struct 'ctx' expected %s, got %s",
                 (int)cf->name_len, cf->name,
                 type_name(cf->type), type_name(val_type));
        compiler__error(c, line, col, err);
        return;
      }
      if (cf->type != TYPE_DYN && val_type == TYPE_DYN) {
        char err[224];
        snprintf(err, sizeof(err),
                 "type error: field '%.*s' of struct 'ctx' expected %s, got dyn — use [to %s $val] to cast",
                 (int)cf->name_len, cf->name,
                 type_name(cf->type), type_name(cf->type));
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
  if (compiler__head_matches(head, "vec", 3)) {
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      if (compiler__reject_bare_typed(c, line, col, "dyn vec")) return;
    }
    compiler__emit_byte(c, OP_VEC, line);
    compiler__emit_byte(c, (uint8_t)argc, line);
    return;
  }

  /* vec-get builtin (exactly 2 args) */
  if (compiler__head_matches(head, "vec-get", 7)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-get", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STREAM) {
      compiler__error(c, line, col, "vec-get requires a vector; got stream (use collect to materialize)");
      return;
    }
    if (c->last_expr_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = c->last_struct_idx;
      compiler__compile_node(c, args[1]);
      compiler__emit_byte(c, OP_TYPED_VEC_GET_INLINE, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      if (COMPILER_IS_SCALAR_TYPE_IDX(elem_type_idx)) {
        /* Scalar element typed vec: result is a single value of that
         * scalar JaclType; not inline struct bytes. */
        c->last_expr_type = COMPILER_TYPE_IDX_TO_SCALAR(elem_type_idx);
        c->last_struct_idx = UINT32_MAX;
      } else {
        c->inline_repr = INLINE_STACK;
        c->last_expr_type = TYPE_STRUCT;
        c->last_struct_idx = elem_type_idx;
      }
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_VEC_GET, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* vec-len builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "vec-len", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "vec-len", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STREAM) {
      compiler__error(c, line, col, "vec-len requires a vector; got stream (use collect to materialize)");
      return;
    }
    if (c->last_expr_type == TYPE_TYPED_VEC) {
      compiler__emit_byte(c, OP_TYPED_VEC_LEN, line);
      c->last_expr_type = TYPE_I32;
      return;
    }
    compiler__emit_byte(c, OP_VEC_LEN, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* vec-push builtin (exactly 2 args) */
  if (compiler__head_matches(head, "vec-push", 8)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-push", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STREAM) {
      compiler__error(c, line, col, "vec-push requires a vector; got stream (use collect to materialize)");
      return;
    }
    if (c->last_expr_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = c->last_struct_idx;
      if (!compiler__compile_typed_elem_arg(c, args[1], elem_type_idx)) {
        compiler__error(c, line, col, "vec-push: element type does not match typed vec element type");
        return;
      }
      compiler__emit_byte(c, OP_TYPED_VEC_PUSH, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      c->last_struct_idx = elem_type_idx;
      return;
    }
    compiler__compile_node(c, args[1]);
    if (compiler__reject_bare_typed(c, line, col, "dyn vec")) return;
    compiler__emit_byte(c, OP_VEC_PUSH, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* vec-set builtin (exactly 3 args) */
  if (compiler__head_matches(head, "vec-set", 7)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "vec-set", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STREAM) {
      compiler__error(c, line, col, "vec-set requires a vector; got stream (use collect to materialize)");
      return;
    }
    if (c->last_expr_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = c->last_struct_idx;
      compiler__compile_node(c, args[1]); /* index */
      if (!compiler__compile_typed_elem_arg(c, args[2], elem_type_idx)) {
        compiler__error(c, line, col, "vec-set: element type does not match typed vec element type");
        return;
      }
      compiler__emit_byte(c, OP_TYPED_VEC_SET, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      c->last_struct_idx = elem_type_idx;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    if (compiler__reject_bare_typed(c, line, col, "dyn vec")) return;
    compiler__emit_byte(c, OP_VEC_SET, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* vec-concat builtin (exactly 2 args) */
  if (compiler__head_matches(head, "vec-concat", 10)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "vec-concat", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STREAM) {
      compiler__error(c, line, col, "vec-concat requires a vector; got stream (use collect to materialize)");
      return;
    }
    if (c->last_expr_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = c->last_struct_idx;
      compiler__compile_node(c, args[1]);
      if (c->last_expr_type != TYPE_TYPED_VEC || c->last_struct_idx != elem_type_idx) {
        compiler__error(c, line, col, "vec-concat: both vectors must have the same typed element type");
        return;
      }
      compiler__emit_byte(c, OP_TYPED_VEC_CONCAT, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      c->last_struct_idx = elem_type_idx;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_VEC_CONCAT, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* vec-slice builtin (exactly 3 args) */
  if (compiler__head_matches(head, "vec-slice", 9)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "vec-slice", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STREAM) {
      compiler__error(c, line, col, "vec-slice requires a vector; got stream (use collect to materialize)");
      return;
    }
    if (c->last_expr_type == TYPE_TYPED_VEC) {
      uint32_t elem_type_idx = c->last_struct_idx;
      compiler__compile_node(c, args[1]);
      compiler__compile_node(c, args[2]);
      compiler__emit_byte(c, OP_TYPED_VEC_SLICE, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;
      c->last_struct_idx = elem_type_idx;
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    compiler__emit_byte(c, OP_VEC_SLICE, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* map constructor (0 or any even number of args) */
  if (compiler__head_matches(head, "map", 3)) {
    if (argc % 2 != 0) {
      compiler__builtin_arity_error(c, line, col, "map",
                                     "an even number of arguments", argc);
      return;
    }
    for (uint32_t i = 0; i < argc; i++) {
      compiler__compile_node(c, args[i]);
      if (compiler__reject_bare_typed(c, line, col, "dyn map")) return;
    }
    compiler__emit_byte(c, OP_MAP, line);
    compiler__emit_byte(c, (uint8_t)(argc / 2), line);
    return;
  }

  /* map-get builtin (exactly 2 args) */
  if (compiler__head_matches(head, "map-get", 7)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-get", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = c->last_struct_idx;
      uint32_t key_type_idx = c->last_key_struct_idx;
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
        c->last_struct_idx = UINT32_MAX;
      } else {
        c->inline_repr = INLINE_STACK;
        c->last_expr_type = TYPE_STRUCT;
        c->last_struct_idx = elem_type_idx;
      }
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_GET, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* map-has builtin (exactly 2 args) */
  if (compiler__head_matches(head, "map-has", 7)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-has", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      uint32_t key_type_idx = c->last_key_struct_idx;
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
  if (compiler__head_matches(head, "map-len", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-len", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      compiler__emit_byte(c, OP_TYPED_MAP_LEN, line);
      c->last_expr_type = TYPE_I32;
      return;
    }
    compiler__emit_byte(c, OP_MAP_LEN, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* map-set builtin (exactly 3 args) */
  if (compiler__head_matches(head, "map-set", 7)) {
    if (argc != 3) {
      compiler__builtin_arity_error(c, line, col, "map-set", "3 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = c->last_struct_idx;
      uint32_t key_type_idx = c->last_key_struct_idx;
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
      compiler__set_type(c, (TypeInfo){ TYPE_TYPED_MAP, elem_type_idx, key_type_idx });
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__compile_node(c, args[2]);
    if (compiler__reject_bare_typed(c, line, col, "dyn map")) return;
    compiler__emit_byte(c, OP_MAP_SET, line);
    c->last_expr_type = TYPE_MAP;
    return;
  }

  /* map-remove builtin (exactly 2 args) */
  if (compiler__head_matches(head, "map-remove", 10)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "map-remove", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = c->last_struct_idx;
      uint32_t key_type_idx = c->last_key_struct_idx;
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
      compiler__set_type(c, (TypeInfo){ TYPE_TYPED_MAP, elem_type_idx, key_type_idx });
      return;
    }
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_MAP_REMOVE, line);
    c->last_expr_type = TYPE_MAP;
    return;
  }

  /* map-keys builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "map-keys", 8)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-keys", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      uint32_t key_type_idx = c->last_key_struct_idx;
      compiler__emit_byte(c, OP_TYPED_MAP_KEYS, line);
      compiler__emit_u16(c, (uint16_t)key_type_idx, line);
      if (key_type_idx != UINT32_MAX) {
        c->last_expr_type = TYPE_TYPED_VEC;  /* struct keys → typed vec */
        c->last_struct_idx = key_type_idx;
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
  if (compiler__head_matches(head, "map-vals", 8)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "map-vals", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_TYPED_MAP) {
      uint32_t elem_type_idx = c->last_struct_idx;
      compiler__emit_byte(c, OP_TYPED_MAP_VALS, line);
      compiler__emit_u16(c, (uint16_t)elem_type_idx, line);
      c->last_expr_type = TYPE_TYPED_VEC;  /* returns typed vec of values */
      c->last_struct_idx = elem_type_idx;
      return;
    }
    compiler__emit_byte(c, OP_MAP_VALS, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* transform builtin (exactly 2 args — non-suspending callback) */
  if (compiler__head_matches(head, "transform", 9)) {
    compiler__compile_hof_builtin(c, "transform", args, argc, OP_TRANSFORM, line, col);
    return;
  }


  /* filter builtin (exactly 2 args — non-suspending callback) */
  if (compiler__head_matches(head, "filter", 6)) {
    compiler__compile_hof_builtin(c, "filter", args, argc, OP_FILTER, line, col);
    return;
  }

  /* error builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "error", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "error", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_ERROR, line);
    return;
  }

  /* error? builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "error?", 6)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "error?", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_IS_ERROR, line);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* error-val builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "error-val", 9)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "error-val", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_ERROR_VAL, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* stack-trace builtin (exactly 0 args) */
  if (compiler__head_matches(head, "stack-trace", 11)) {
    if (argc != 0) {
      compiler__builtin_arity_error(c, line, col, "stack-trace", "0 arguments", argc);
      return;
    }
    compiler__emit_byte(c, OP_STACK_TRACE, line);
    return;
  }

  /* to-string builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "to-string", 9)) {
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
  if (compiler__head_matches(head, "hash", 4)) {
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

  if (compiler__head_matches(head, "interpret", 9)) {
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

  if (compiler__head_matches(head, "interpret-prelude", 17)) {
    if (argc != 0) {
      compiler__builtin_arity_error(c, line, col, "interpret-prelude", "0 arguments", argc);
      return;
    }
    compiler__emit_byte(c, OP_INTERPRET_PRELUDE, line);
    return;
  }

  /* US-015: syntax object introspection builtins. Each compiles the single
   * argument to a syntax object value, then emits OP_SYNTAX_OP with a subop
   * byte indicating which introspection operation to perform. */
  if (compiler__head_matches(head, "syntax-kind", 11)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "syntax-kind", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, 0 /* SYNTAX_OP_KIND */, line);
    c->last_expr_type = TYPE_STR;
    return;
  }
  if (compiler__head_matches(head, "syntax-datum", 12)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "syntax-datum", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, 1 /* SYNTAX_OP_DATUM */, line);
    return;
  }
  if (compiler__head_matches(head, "syntax-head", 11)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "syntax-head", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, 2 /* SYNTAX_OP_HEAD */, line);
    return;
  }
  if (compiler__head_matches(head, "syntax-args", 11)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "syntax-args", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, 3 /* SYNTAX_OP_ARGS */, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }
  if (compiler__head_matches(head, "syntax-commands", 15)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "syntax-commands", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, 4 /* SYNTAX_OP_COMMANDS */, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }
  if (compiler__head_matches(head, "syntax-pos", 10)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "syntax-pos", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, 5 /* SYNTAX_OP_POS */, line);
    c->last_expr_type = TYPE_MAP;
    return;
  }
  /* NOTE: PRD calls this syntax->string but the lexer tokenizes -> as a
   * separator (arrow), so the hyphenated form 'syntax-str' is used instead,
   * consistent with existing builtins like to-string and byte-length. */
  if (compiler__head_matches(head, "syntax-str", 10)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "syntax-str", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_SYNTAX_OP, line);
    compiler__emit_byte(c, 6 /* SYNTAX_OP_STRING */, line);
    c->last_expr_type = TYPE_STR;
    return;
  }

  /* US-016: make-syntax — programmatic construction of syntax objects.
   * First arg is a bare word naming the kind; remaining args are the
   * payload. Kind dispatch happens at compile time, so the opcode only
   * needs the kind-specific subop. Subops 7..12 share OP_SYNTAX_OP. */
  if (compiler__head_matches(head, "make-syntax", 11)) {
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
    return;
  }

  /* US-017: syntax-error — signal a custom error with an optional syntax
   * object for source-location reporting. Two forms:
   *   syntax-error $message                 → subop 13 (message only)
   *   syntax-error $message $syntax-obj     → subop 14 (message + pos)
   * Implementation is a runtime error — macros in JACL are template-based,
   * so "compile-time" in the PRD sense means "at macro-expanded-code
   * execution time", which is the program's normal runtime. */
  if (compiler__head_matches(head, "syntax-error", 12)) {
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
  if (compiler__head_matches(head, "box", 3)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "box", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STRUCT && c->last_struct_idx != UINT32_MAX) {
      /* Box accepts inline struct bytes directly — no reify. */
      compiler__emit_byte(c, OP_BOX_STRUCT, line);
      compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
      c->inline_repr = INLINE_NONE;
    } else {
      compiler__emit_byte(c, OP_BOX, line);
    }
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* atom builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "atom", 4)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "atom", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    if (c->last_expr_type == TYPE_STRUCT) {
      compiler__error(c, line, col, "atom: struct values cannot be stored in atoms; use [box] instead");
      return;
    }
    compiler__emit_byte(c, OP_ATOM, line);
    return;
  }

  /* box? builtin (1 or 2 args) */
  if (compiler__head_matches(head, "box?", 4)) {
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
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* atom? builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "atom?", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "atom?", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_IS_ATOM, line);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* future? builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "future?", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "future?", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_IS_FUTURE, line);
    c->last_expr_type = TYPE_BOOL;
    return;
  }

  /* deref builtin (exactly 1 arg) */
  if (compiler__head_matches(head, "deref", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "deref", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_DEREF, line);
    return;
  }

  /* unbox builtin (exactly 1 arg) — requires flow-typed narrowing from box? guard */
  if (compiler__head_matches(head, "unbox", 5)) {
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
                compiler__set_type(c, (TypeInfo){ bt, tidx, c->narrowings[ni].box_key_type_idx });
              } else if (tidx > 0) {
                /* Phase 5d: deref struct box directly to inline bytes */
                StructTypeRegistry* reg = compiler__get_struct_registry(c);
                StructTypeDef* sdef = reg && tidx < reg->count ? reg->defs[tidx] : NULL;
                if (struct_def_is_user(sdef, reg)) {
                  compiler__emit_byte(c, OP_DEREF_INLINE, line);
                  compiler__emit_u16(c, (uint16_t)tidx, line);
                  c->last_expr_type = TYPE_STRUCT;
                  c->last_struct_idx = tidx;
                  c->inline_repr = INLINE_STACK;
                } else {
                  compiler__emit_byte(c, OP_DEREF, line);
                  c->last_expr_type = TYPE_STRUCT;
                  c->last_struct_idx = tidx;
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
  if (compiler__head_matches(head, "reset", 5)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "reset", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__compile_node(c, args[1]);
    if (c->last_expr_type == TYPE_STRUCT && c->last_struct_idx != UINT32_MAX) {
      /* Struct-box reset: inline bytes write directly to box->data. */
      compiler__emit_byte(c, OP_RESET_INLINE, line);
      compiler__emit_u16(c, (uint16_t)c->last_struct_idx, line);
      c->inline_repr = INLINE_STACK;  /* new struct bytes left on TOS */
    } else {
      compiler__emit_byte(c, OP_RESET, line);
      c->last_expr_type = TYPE_NIL;
    }
    return;
  }

  /* swap builtin (exactly 2 args) */
  if (compiler__head_matches(head, "swap", 4)) {
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

  /* to builtin — explicit type conversion: [to TYPE expr] */
  if (compiler__head_matches(head, "to", 2)) {
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
    /* Phase 3c: read result type from the typer's pre-computed AST
     * annotation; fall back to c->last_expr_type for typer gaps. */
    JaclType src_type = (JaclType)args[1]->inferred_type;
    if (src_type == TYPE_DYN) src_type = c->last_expr_type;

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

  /* await — suspension point (state machine) or job wait */
  if (compiler__head_matches(head, "await", 5)) {
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
      /* SM await: compile future, set resume_point, emit OP_AWAIT_SM.
         Inline (resolved): OP_AWAIT_SM pushes result, jump past resume push.
         Resume (pending):  dispatch table lands at resume label, push __rv. */
      compiler__compile_node(c, args[0]);
      uint32_t sp_idx = c->sm_suspension_idx++;
      compiler__emit_constant(c, jacl_i32((int32_t)(sp_idx + 1)), line);
      compiler__emit_byte(c, OP_SET_RESUME_POINT, line);
      compiler__emit_byte(c, OP_AWAIT_SM, line);
      /* Inline path: result already on stack; jump past resume value push */
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      /* Push resume value from slot 1 (__rv) onto stack */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
      /* Common path: result on stack */
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

  /* yield — generator suspension point (state machine). */
  if (compiler__head_matches(head, "yield", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "yield", "1 argument", argc);
      return;
    }
    if (c->sm_analysis) {
      /* SM yield: compile value, set resume_point, emit OP_YIELD_SM,
         backpatch dispatch label, push nil as yield expression result */
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
      c->has_yield = true;
      c->last_expr_type = TYPE_NIL;
      return;
    }
    compiler__error(c, line, col,
        "yield requires state machine compilation (internal error)");
    return;
  }

  /* stream_next — pull next element from a stream */
  if (compiler__head_matches(head, "stream_next", 11)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "stream_next", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_STREAM_NEXT, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* collect — materialize stream into vector (identity on vectors) */
  if (compiler__head_matches(head, "collect", 7)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "collect", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_COLLECT, line);
    c->last_expr_type = TYPE_VEC;
    return;
  }

  /* count — count elements in stream or vector */
  if (compiler__head_matches(head, "count", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "count", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_COUNT, line);
    c->last_expr_type = TYPE_I32;
    return;
  }

  /* take — take first N elements from stream or vector */
  if (compiler__head_matches(head, "take", 4)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "take", "2 arguments", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    /* Phase 3c: read result type from the typer's pre-computed AST
     * annotation; fall back to c->last_expr_type for typer gaps. */
    JaclType col_type = (JaclType)args[0]->inferred_type;
    if (col_type == TYPE_DYN) col_type = c->last_expr_type;
    compiler__compile_node(c, args[1]);
    compiler__emit_byte(c, OP_TAKE, line);
    c->last_expr_type = col_type;
    return;
  }

  /* first — get first element from stream or vector */
  if (compiler__head_matches(head, "first", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "first", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_FIRST, line);
    c->last_expr_type = TYPE_DYN;
    return;
  }

  /* lines — split string into lazy line stream */
  if (compiler__head_matches(head, "lines", 5)) {
    if (argc != 1) {
      compiler__builtin_arity_error(c, line, col, "lines", "1 argument", argc);
      return;
    }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_LINES, line);
    c->last_expr_type = TYPE_STREAM;
    return;
  }

  /* exec — spawn external command, return map {stdout, stderr, exit} (US-006)
   * [exec cmd arg1 arg2 ...] spawns a subprocess, waits for completion,
   * and returns a map with stdout (stream), stderr (string), exit (i32).
   * This is the "full form" that gives access to all process outputs. */
  if (compiler__head_matches(head, "exec", 4)) {
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
  if (compiler__head_matches(head, "signal", 6)) {
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
  if (compiler__head_matches(head, "cancel", 6)) {
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

  /* parallel — suspension point (state machine) */
  if (compiler__head_matches(head, "parallel", 8)) {
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
      /* SM parallel: compile bodies, set resume_point, push state object,
         emit OP_PARALLEL. Two paths like SM await:
         Inline (single-threaded): result on stack, jump past resume push.
         Resume (runtime): dispatch table lands at resume label, push __rv. */
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
      /* Inline path: result already on stack; jump past resume value push */
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      /* Push resume value from slot 1 (__rv) onto stack */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
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
  if (compiler__head_matches(head, "race", 4)) {
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
      /* SM race: compile bodies, set resume_point, push state object,
         emit OP_RACE. Two paths like SM await/parallel:
         Inline (single-threaded): result on stack, jump past resume push.
         Resume (runtime): dispatch table lands at resume label, push __rv. */
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
      /* Inline path: result already on stack; jump past resume value push */
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      /* Push resume value from slot 1 (__rv) onto stack */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
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
  if (compiler__head_matches(head, "spawn", 5)) {
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
  if (compiler__head_matches(head, ".", 1)) {
    bool is_set = (argc == 3);
    if (argc != 2 && argc != 3) {
      compiler__builtin_arity_error(c, line, col, ".", "2 or 3 arguments", argc);
      return;
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
          compiler__emit_byte(c, OP_GET_GLOBAL, line);
          compiler__emit_u16(c, get_idx, line);

          /* Set type info from export */
          c->last_expr_type = found_export->type;
          c->last_struct_idx = UINT32_MAX;
          return;
        }
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
          uint32_t inner_width = struct__slot_width(reg, c->last_struct_idx);
          if (inner_width == 1) {
            compiler__emit_byte(c, OP_POP, line);
          } else {
            compiler__emit_byte(c, OP_POP_N, line);
            compiler__emit_byte(c, (uint8_t)inner_width, line);
          }
          is_inline_access = true;
          inline_base = c->inline_ref_base;
          inline_offset = c->inline_ref_offset;
          inline_sidx = c->last_struct_idx;
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
            c->expected_type = field_type;
            compiler__compile_node(c, args[2]);
            c->expected_type = TYPE_DYN;
            /* Phase 3c: read result type from the typer's pre-computed AST
             * annotation; fall back to c->last_expr_type for typer gaps. */
            JaclType val_type = (JaclType)args[2]->inferred_type;
            if (val_type == TYPE_DYN) val_type = c->last_expr_type;
            if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) {
              char err_msg[192];
              snprintf(err_msg, sizeof(err_msg),
                       "type error: field '%.*s' of struct '%.*s' expected %s, got %s",
                       (int)sdef->fields[fi].name_len, sdef->fields[fi].name,
                       (int)sdef->name_len, sdef->name,
                       type_name(field_type), type_name(val_type));
              compiler__error(c, line, col, err_msg);
              return;
            }
            if (field_type != TYPE_DYN && val_type == TYPE_DYN) {
              char err_msg[224];
              snprintf(err_msg, sizeof(err_msg),
                       "type error: field '%.*s' of struct '%.*s' expected %s, got dyn — use [to %s $val] to cast",
                       (int)sdef->fields[fi].name_len, sdef->fields[fi].name,
                       (int)sdef->name_len, sdef->name,
                       type_name(field_type), type_name(field_type));
              compiler__error(c, line, col, err_msg);
              return;
            }
            compiler__emit_byte(c, set_op, line);
            compiler__emit_byte(c, inline_base, line);
            compiler__emit_u16(c, total_offset, line);
            compiler__emit_byte(c, (uint8_t)field_type, line);
            c->last_expr_type = TYPE_NIL;
            c->last_struct_idx = UINT32_MAX;
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
              c->last_struct_idx = sdef->fields[fi].struct_type_idx;
            } else {
              /* Scalar field — emit inline get */
              compiler__emit_byte(c, get_op, line);
              compiler__emit_byte(c, inline_base, line);
              compiler__emit_u16(c, total_offset, line);
              compiler__emit_byte(c, (uint8_t)sdef->fields[fi].type, line);
              c->last_expr_type = sdef->fields[fi].type;
              c->last_struct_idx = UINT32_MAX;
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
    /* Phase 3c: read result type from the typer's pre-computed AST
     * annotation; fall back to c->last_expr_type for typer gaps. */
    JaclType struct_type = (JaclType)args[0]->inferred_type;
    if (struct_type == TYPE_DYN) struct_type = c->last_expr_type;
    uint32_t struct_idx = c->last_struct_idx;

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
    if (struct_type == TYPE_STRUCT && struct_idx == CTX_STRUCT_PENDING) {
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
          c->expected_type = field_type;
          compiler__compile_node(c, args[2]);
          c->expected_type = TYPE_DYN;
          /* Phase 3c: read result type from the typer's pre-computed AST
           * annotation; fall back to c->last_expr_type for typer gaps. */
          JaclType val_type = (JaclType)args[2]->inferred_type;
          if (val_type == TYPE_DYN) val_type = c->last_expr_type;
          if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) {
            char err_msg[192];
            snprintf(err_msg, sizeof(err_msg),
                     "type error: field '%.*s' of struct 'ctx' expected %s, got %s",
                     (int)cf->name_len, cf->name,
                     type_name(field_type), type_name(val_type));
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (field_type != TYPE_DYN && val_type == TYPE_DYN) {
            char err_msg[224];
            snprintf(err_msg, sizeof(err_msg),
                     "type error: field '%.*s' of struct 'ctx' expected %s, got dyn — use [to %s $val] to cast",
                     (int)cf->name_len, cf->name,
                     type_name(field_type), type_name(field_type));
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
          c->last_struct_idx = CTX_STRUCT_PENDING;
        } else {
          if (cf->type == TYPE_STRUCT) {
            /* Inline struct field: push N inline slots from ctx.data. */
            compiler__emit_byte(c, OP_HEAP_RECORD_GET_INLINE, line);
            compiler__emit_u16(c, (uint16_t)cf->offset, line);
            compiler__emit_u16(c, (uint16_t)cf->struct_type_idx, line);
            c->inline_repr = INLINE_STACK;
            c->last_expr_type = TYPE_STRUCT;
            c->last_struct_idx = cf->struct_type_idx;
          } else {
            compiler__emit_byte(c, OP_HEAP_RECORD_GET, line);
            compiler__emit_u16(c, (uint16_t)cf->offset, line);
            compiler__emit_byte(c, (uint8_t)cf->type, line);
            c->last_expr_type = cf->type;
            c->last_struct_idx = UINT32_MAX;
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
          c->expected_type = field_type;
          compiler__compile_node(c, args[2]);
          c->expected_type = TYPE_DYN;
          /* Phase 3c: read result type from the typer's pre-computed AST
           * annotation; fall back to c->last_expr_type for typer gaps. */
          JaclType val_type = (JaclType)args[2]->inferred_type;
          if (val_type == TYPE_DYN) val_type = c->last_expr_type;

          if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) {
            char err_msg[192];
            snprintf(err_msg, sizeof(err_msg),
                     "type error: field '%.*s' of struct '%.*s' expected %s, got %s",
                     (int)sdef->fields[fi].name_len, sdef->fields[fi].name,
                     (int)sdef->name_len, sdef->name,
                     type_name(field_type), type_name(val_type));
            compiler__error(c, line, col, err_msg);
            return;
          }
          if (field_type != TYPE_DYN && val_type == TYPE_DYN) {
            char err_msg[224];
            snprintf(err_msg, sizeof(err_msg),
                     "type error: field '%.*s' of struct '%.*s' expected %s, got dyn — use [to %s $val] to cast",
                     (int)sdef->fields[fi].name_len, sdef->fields[fi].name,
                     (int)sdef->name_len, sdef->name,
                     type_name(field_type), type_name(field_type));
            compiler__error(c, line, col, err_msg);
            return;
          }

          /* Emit OP_HEAP_RECORD_SET + field_offset (u16) + field_type (u8) */
          compiler__emit_byte(c, OP_HEAP_RECORD_SET, line);
          compiler__emit_u16(c, (uint16_t)sdef->fields[fi].offset, line);
          compiler__emit_byte(c, (uint8_t)field_type, line);

          /* Returns struct value */
          c->last_expr_type = TYPE_STRUCT;
          c->last_struct_idx = struct_idx;
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
          if (sdef->fields[fi].type == TYPE_STRUCT)
            c->last_struct_idx = sdef->fields[fi].struct_type_idx;
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
        c->last_struct_idx = UINT32_MAX;
      } else {
        /* Push key, then emit OP_MAP_GET */
        compiler__emit_byte(c, OP_CONST, line);
        compiler__emit_u16(c, name_idx, line);
        compiler__emit_byte(c, OP_MAP_GET, line);
        c->last_expr_type = TYPE_DYN;
        c->last_struct_idx = UINT32_MAX;
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
        c->last_struct_idx = UINT32_MAX;
      } else {
        /* Emit OP_HEAP_RECORD_GET_DYN + const_idx (field name) */
        compiler__emit_byte(c, OP_HEAP_RECORD_GET_DYN, line);
        compiler__emit_u16(c, name_idx, line);
        /* Result type is dyn (field type unknown at compile time) */
        c->last_expr_type = TYPE_DYN;
        c->last_struct_idx = UINT32_MAX;
      }
    }
    return;
  }

  /* Optional chaining: [?. expr field] — nil-safe field access */
  if (compiler__head_matches(head, "?.", 2)) {
    if (argc != 2) {
      compiler__builtin_arity_error(c, line, col, "?.", "2 arguments", argc);
      return;
    }
    /* Compile the object expression */
    compiler__compile_node(c, args[0]);
    /* Reject structs at compile time — use -> for struct field access */
    if (c->last_expr_type == TYPE_STRUCT && c->last_struct_idx != UINT32_MAX) {
      StructTypeRegistry* reg = compiler__get_struct_registry(c);
      StructTypeDef* sdef = reg->defs[c->last_struct_idx];
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
    c->last_struct_idx = UINT32_MAX;
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
        c->expected_type = field_type;
        compiler__compile_node(c, args[i]);
        c->expected_type = TYPE_DYN;
        /* Phase 3c: read result type from the typer's pre-computed AST
         * annotation; fall back to c->last_expr_type for typer gaps. */
        JaclType arg_type = (JaclType)args[i]->inferred_type;
        if (arg_type == TYPE_DYN) arg_type = c->last_expr_type;

        if (field_type != TYPE_DYN && arg_type != TYPE_DYN &&
            arg_type != field_type) {
          char err_msg[192];
          snprintf(err_msg, sizeof(err_msg),
                   "type error: field '%.*s' of struct '%.*s' expected %s, got %s",
                   (int)sdef->fields[i].name_len, sdef->fields[i].name,
                   (int)name_len, head->data.lit_string.value,
                   type_name(field_type), type_name(arg_type));
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
      c->last_struct_idx = struct_idx;
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
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
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
        c->expected_type = expected_param_type;
      }
      compiler__compile_node(c, args[i]);
      c->expected_type = TYPE_DYN;
      /* Phase 3c: read result type from the typer's pre-computed AST
       * annotation; fall back to c->last_expr_type for typer gaps. */
      JaclType arg_type = (JaclType)args[i]->inferred_type;
      if (arg_type == TYPE_DYN) arg_type = c->last_expr_type;

      /* Phase 5a: struct args passed inline (multi-slot) instead of as heap copies.
       * If the arg is already inline (from constructor or typed-get), nothing to do.
       * If it's a heap struct (from OP_STRUCT_MATERIALIZE or function return),
       * expand it to inline slots via OP_STRUCT_EXPAND. */
      if (expected_param_type == TYPE_STRUCT && arg_type == TYPE_STRUCT) {
        uint32_t sidx = c->last_struct_idx;
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

    /* Check if callee is a known suspending proc in SM context */
    bool use_call_suspend = false;
    if (c->sm_analysis && c->suspension_map && callee_name_str && callee_name_len <= 128) {
      JaclVal cname = compiler__name_val(c->heap, c->intern_table, callee_name_str, callee_name_len);
      if (suspension_map_lookup(c->suspension_map, cname) &&
          !suspension_map_is_generator(c->suspension_map, cname)) {
        use_call_suspend = true;
      }
    }

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
      /* Inline path: result already on stack; jump past resume value push */
      uint32_t skip_jump = compiler__emit_jump(c, OP_JUMP, line);
      /* Resume label: dispatch table backpatch lands here */
      if (sp_idx < c->sm_dispatch.label_count) {
        compiler__patch_jump(c, c->sm_dispatch.label_patches[sp_idx]);
      }
      /* Push resume value from slot 1 (__rv) onto stack */
      compiler__emit_byte(c, OP_GET_LOCAL, line);
      compiler__emit_byte(c, 1, line);
      /* Common path: result on stack */
      compiler__patch_jump(c, skip_jump);
    } else {
      /* Regular call — Phase 5a: use total_arg_slots for slot-based arg count */
      compiler__emit_byte(c, OP_CALL, line);
      compiler__emit_byte(c, (uint8_t)total_arg_slots, line);
    }

    /* Set result type from callee's return type */
    c->last_expr_type = call_return_type;
    c->last_struct_idx = call_return_struct_idx;
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
      JaclType et = c->expected_type;
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
      JaclType et = c->expected_type;
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
          c->last_struct_idx = CTX_STRUCT_PENDING;
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
            c->last_struct_idx = sf->struct_type_idx;
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
        compiler__set_type(c, TYPEINFO_LOAD(c->locals[local_slot]));
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
          compiler__set_type(c, TYPEINFO_LOAD(c->upvalues[upvalue_idx]));
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
          compiler__emit_byte(c, OP_GET_GLOBAL, line);
          compiler__emit_u16(c, name_idx, line);
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
          if (ga) {
            compiler__set_type(c, TYPEINFO_LOAD(*ga));
          } else {
            c->last_expr_type = TYPE_DYN;
          }
        }
      }
      break;
    }

    case AST_COMMAND: {
      compiler__compile_command(c, node);
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
          compiler__emit_byte(c, OP_GET_GLOBAL, line);
          compiler__emit_u16(c, get_idx, line);
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
            compiler__emit_byte(c, OP_GET_GLOBAL, line);
            compiler__emit_u16(c, get_idx, line);

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
        /* For-loop: forward-jump to increment code (patched later) */
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
      break;
    }

    case AST_RETURN: {
      /* Compile return value (or nil) */
      if (node->data.return_stmt.value) {
        compiler__compile_node(c, node->data.return_stmt.value);
        /* Phase 5b: wide return for struct-returning procs */
        compiler__emit_return(c, line);
      } else {
        compiler__emit_byte(c, OP_NIL, line);
        compiler__emit_byte(c, OP_RETURN, line);
      }
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
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
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

#ifdef JACL_TYPER_DUAL_TRACK
  /* Phase 3 dual-track check: warn if typer disagrees with compile-time
   * inference. Skip AST_COMMAND: compiler.c's last_expr_type for
   * commands is inconsistent across branches.
   *
   * Two flavors:
   *   MISMATCH — both sides non-DYN and disagree. This is a typer bug.
   *   GAP      — compiler is non-DYN but typer is DYN. Typer is more
   *              conservative; consumers that switch to the typer would
   *              get worse codegen (unboxed → boxed) here. */
  if (node->type != AST_COMMAND) {
    JaclType nt = (JaclType)node->inferred_type;
    JaclType ct = c->last_expr_type;
    if (nt != TYPE_DYN && ct != TYPE_DYN && nt != ct) {
      fprintf(stderr,
          "TYPER MISMATCH at %u:%u (AST_%d): typer=%s, compiler=%s\n",
          node->start.line, node->start.column, (int)node->type,
          type_name(nt), type_name(ct));
    } else if (nt == TYPE_DYN && ct != TYPE_DYN) {
      fprintf(stderr,
          "TYPER GAP at %u:%u (AST_%d): typer=dyn, compiler=%s\n",
          node->start.line, node->start.column, (int)node->type,
          type_name(ct));
    }
  }
#endif
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
static bool compiler__node_needs_expansion(AstNode *node) {
  if (!node) return false;
  if (node->type == AST_DEFMACRO) return true;
  if (node->type == AST_COMMAND) {
    AstNode *head = node->data.command.head;
    if (head && head->type == AST_LIT_STRING
        && head->data.lit_string.length == 1
        && head->data.lit_string.value[0] == '\\')
      return true;
    /* Recurse into head and args */
    if (compiler__node_needs_expansion(head)) return true;
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      if (compiler__node_needs_expansion(node->data.command.args[i]))
        return true;
    }
  }
  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) {
      if (compiler__node_needs_expansion(node->data.block.commands[i]))
        return true;
    }
  }
  return false;
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

  /* Pre-compilation suspension analysis */
  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count, heap, intern_table);

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
   * Runs after parsing, before the main compilation pass.
   * Fast-path: skip entirely if the AST has no defmacros and no command
   * heads that match a built-in macro name (currently just \). */
  if (parse.error_count == 0 && compiler__needs_expansion(parse.nodes, parse.count)) {
    uint32_t err_line = 0, err_col = 0;
    const char *expand_err = ast_expand_macros(
        parse.nodes, parse.count, c.macro_table, heap,
        intern_table, arena, es, &err_line, &err_col);
    if (expand_err) {
      compiler__error(&c, err_line, err_col, expand_err);
    }
  }

  /* Phase 3 foundation: type-inference pass. Currently populates
   * inferred_type for literals, blocks, and structural recursion only.
   * Codegen does not yet consume these results — the existing
   * expected_type/last_expr_type plumbing remains the source of truth.
   * Future subphases will expand coverage and switch consumers. */
  if (parse.error_count == 0) {
    typer_infer(parse.nodes, parse.count);
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

    /* Phase 2: Hoist top-level proc definitions into the outer chunk so
       they are defined via OP_SET_GLOBAL before the SM closure executes.
       This ensures all workers have proc definitions in their env when
       running in concurrent mode. */
    uint32_t non_proc_count = 0;
    AstNode** non_proc_stmts = (AstNode**)arena_alloc(arena,
        parse.count * sizeof(AstNode*));
    for (uint32_t i = 0; i < parse.count; i++) {
      AstNode* node = parse.nodes[i];
      if (node->type == AST_COMMAND &&
          compiler__head_matches(node->data.command.head, "proc", 4)) {
        /* Compile proc definition into outer (top-level) chunk */
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
    /* Normal non-suspending top-level compilation */
    for (uint32_t i = 0; i < parse.count; i++) {
      compiler__compile_node(&c, parse.nodes[i]);

      /* Emit OP_CHECK_ERROR between statements: auto-return on error */
      if (i < parse.count - 1) {
        compiler__emit_check_error(&c, parse.nodes[i]->start.line);
      }
    }

    compiler__emit_byte(&c, OP_HALT,
                        parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);
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

  /* Phase 3 typer pass: walk the module AST so dual-track invariants
   * hold during compile, and so consumer sites that read from
   * inferred_type don't fall back unnecessarily. */
  typer_infer(parse.nodes, parse.count);

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
  char resolved[1024];
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

  /* Phase 3 typer pass for module programs (mirrors compiler_compile and
   * compiler__compile_module). */
  typer_infer(parse.nodes, parse.count);

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
          node->type == AST_USE) {
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
