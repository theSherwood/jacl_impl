/*
 * JACL Type-Shape Registry
 *
 * Unified type-shape registry shared by the typer and the compiler.
 * Holds entries for: user-declared struct shapes, the ctx struct,
 * typed-vec shapes ([Vec T]), and typed-map shapes ([Map K V]).
 *
 * This file is included into the unity build between ast.c and typer.c
 * so the typer has access to the same intern helpers as the compiler.
 * Without this, typed-collection narrowing during type checking would
 * need per-binding aux fields growing with every nesting depth; the
 * registry collapses that to a single 32-bit idx per shape regardless
 * of nesting. See docs/TYPE_REGISTRY_REFACTOR.md.
 *
 * Registry indices share the 32-bit type-idx namespace with scalar
 * sentinels (JACL_SCALAR_TYPE_IDX in jacl.h): values in [0..0xFEFF]
 * are registry indices; [0xFF00..0xFFFF] are scalar JaclType
 * sentinels. The kind tag on each registry entry distinguishes
 * struct shapes from typed-collection shapes within the registry
 * range.
 */

#ifndef SHAPES_C
#define SHAPES_C

#include <stdlib.h>
#include <string.h>

/* Sentinel base must match the value carved out in jacl.h
 * (JACL_SCALAR_VEC_BASE / COMPILER_SCALAR_VEC_BASE). Registry growth
 * must stop before colliding with the sentinel range. */
#ifndef SHAPES_SCALAR_VEC_BASE
#define SHAPES_SCALAR_VEC_BASE 0xFF00u
#endif

#define STRUCT_REGISTRY_INIT_CAP 32
#define STRUCT_MAX_FIELDS   256   /* stack buffer limit for temp field arrays */

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclType    type;
  uint32_t    struct_type_idx; /* registry idx for type==STRUCT; for TYPE_BUF
                                * the element encoding (scalar sentinel or
                                * registry idx). */
  uint32_t    offset;          /* byte offset in struct memory (C-ABI) */
  uint32_t    size;            /* field size in bytes (C-ABI) */
  bool        is_mutable;
  JaclVal     default_val;     /* default value for ctx fields (JACL_NIL if none) */
  uint32_t    buf_len;         /* TYPE_BUF: N (in elements). 0 otherwise. */
} StructTypeField;

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclVal     name_val;        /* inline string (for global_arities lookup) */
  uint32_t    field_count;
  uint32_t    total_size;      /* total size including trailing padding */
  uint32_t    alignment;       /* max alignment of all fields */
  StructTypeField fields[];    /* flexible array member */
} StructTypeDef;

typedef enum {
  TYPE_SHAPE_NONE = 0,         /* reserved / slot 0 (dyn placeholder) */
  TYPE_SHAPE_STRUCT,           /* user-declared struct -- u.struct_def */
  TYPE_SHAPE_CTX,              /* the lone HeapRecord builtin */
  TYPE_SHAPE_TYPED_VEC,        /* [Vec T] -- u.tvec.elem_idx */
  TYPE_SHAPE_TYPED_MAP,        /* [Map K V] -- u.tmap.{key,value}_idx */
  TYPE_SHAPE_BUF,              /* [Buf N T] -- u.buf.{len,elem_idx} */
  TYPE_SHAPE_PTR,              /* [Ptr T] -- u.ptr.pointee_idx */
  TYPE_SHAPE_FUTURE,           /* [Future T] -- u.future.resolves_to_idx */
  TYPE_SHAPE_BOX,              /* [Box T] -- u.box.boxes_idx */
} TypeShapeKind;

typedef struct {
  TypeShapeKind kind;
  union {
    StructTypeDef* struct_def;          /* STRUCT, CTX */
    struct { uint32_t elem_idx; } tvec; /* TYPED_VEC */
    struct { uint32_t key_idx;
             uint32_t value_idx; } tmap; /* TYPED_MAP. key_idx == UINT32_MAX
                                          * for [Map V] (dyn keys). */
    struct { uint32_t len;
             uint32_t elem_idx; } buf;   /* BUF */
    struct { uint32_t pointee_idx; } ptr;        /* PTR */
    struct { uint32_t resolves_to_idx; } future; /* FUTURE */
    struct { uint32_t boxes_idx; } box;          /* BOX */
  } u;
} TypeShape;

typedef struct StructTypeRegistry StructTypeRegistry;
struct StructTypeRegistry {
  StructTypeDef** defs;       /* defs[idx] -> StructTypeDef* (NULL for non-struct
                               * kinds). Kept as a parallel view so the existing
                               * compiler readers stay working until they migrate
                               * to consult `shapes[idx]` directly. */
  TypeShape* shapes;          /* shapes[idx] -- kind tag + payload. Source of
                               * truth for kind-dispatching readers. */
  uint32_t count;             /* next available idx (starts at 2; 0/1 reserved) */
  uint32_t capacity;
  arena_t* arena;             /* arena for StructTypeDef allocations (not owned) */
  uint32_t ctx_type_idx;      /* idx of the ctx struct (0 = not yet registered) */
};

/* --- Helpers --- */

static inline TypeShapeKind type_shape_kind(const StructTypeRegistry* reg,
                                             uint32_t idx) {
  if (!reg || idx >= reg->count) return TYPE_SHAPE_NONE;
  return reg->shapes[idx].kind;
}

static StructTypeDef* struct_registry__alloc_def(StructTypeRegistry* reg,
                                                  uint32_t field_count) {
  size_t sz = sizeof(StructTypeDef) + field_count * sizeof(StructTypeField);
  return (StructTypeDef*)arena_alloc(reg->arena, sz);
}

static bool struct_registry__grow(StructTypeRegistry* reg) {
  if (reg->count < reg->capacity) return true;
  uint32_t new_cap = reg->capacity * 2;
  if (new_cap < STRUCT_REGISTRY_INIT_CAP) new_cap = STRUCT_REGISTRY_INIT_CAP;
  if (new_cap >= SHAPES_SCALAR_VEC_BASE) return false;
  StructTypeDef** new_defs = (StructTypeDef**)realloc(reg->defs,
      new_cap * sizeof(StructTypeDef*));
  if (!new_defs) return false;
  TypeShape* new_shapes = (TypeShape*)realloc(reg->shapes,
      new_cap * sizeof(TypeShape));
  if (!new_shapes) { reg->defs = new_defs; return false; }
  for (uint32_t i = reg->capacity; i < new_cap; i++) {
    new_defs[i] = NULL;
    new_shapes[i].kind = TYPE_SHAPE_NONE;
    new_shapes[i].u.struct_def = NULL;
  }
  reg->defs = new_defs;
  reg->shapes = new_shapes;
  reg->capacity = new_cap;
  return true;
}

static inline bool struct_def_is_user(const StructTypeDef* sdef,
                                       const StructTypeRegistry* reg) {
  if (!sdef || !reg) return false;
  return sdef != reg->defs[reg->ctx_type_idx];
}

static void struct_registry__init(StructTypeRegistry* reg, arena_t* arena) {
  reg->arena = arena;
  reg->capacity = STRUCT_REGISTRY_INIT_CAP;
  reg->defs = (StructTypeDef**)calloc(reg->capacity, sizeof(StructTypeDef*));
  reg->shapes = (TypeShape*)calloc(reg->capacity, sizeof(TypeShape));
  /* slot 0 reserved for dyn placeholder; slot 1 reserved for ctx. */
  reg->count = 2;
  reg->defs[0] = NULL;
  reg->defs[1] = NULL;
  reg->ctx_type_idx = 1;
}

/* Initialize a shape registry whose entries occupy a specific index
 * window. Used by the typer to keep shape indices disjoint from
 * struct indices in tc.structs[] -- typer struct indices are in
 * [0, TYPER_MAX_STRUCTS); typer shape indices start at
 * TYPER_MAX_STRUCTS. The decoder distinguishes the two ranges from
 * the value alone, no kind tag or extra aux field needed. */
static void struct_registry__init_at_offset(StructTypeRegistry* reg,
                                             uint32_t start_count) {
  reg->arena = NULL;
  reg->capacity = start_count + STRUCT_REGISTRY_INIT_CAP;
  reg->defs = (StructTypeDef**)calloc(reg->capacity, sizeof(StructTypeDef*));
  reg->shapes = (TypeShape*)calloc(reg->capacity, sizeof(TypeShape));
  /* Pre-mark the reserved prefix as NONE so any accidental read at
   * a struct-index range returns TYPE_SHAPE_NONE rather than garbage. */
  reg->count = start_count;
  reg->ctx_type_idx = 0;
}

static void struct_registry__destroy(StructTypeRegistry* reg) {
  if (!reg) return;
  free(reg->defs);
  free(reg->shapes);
  reg->defs = NULL;
  reg->shapes = NULL;
  reg->count = 0;
  reg->capacity = 0;
}

/* Intern a typed-vec shape. Returns the registry idx, or UINT32_MAX
 * on allocation failure. Identical (elem_idx) requests share an idx. */
static uint32_t type_shape_intern_typed_vec(StructTypeRegistry* reg,
                                            uint32_t elem_idx) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind == TYPE_SHAPE_TYPED_VEC &&
        reg->shapes[i].u.tvec.elem_idx == elem_idx) {
      return i;
    }
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_TYPED_VEC;
  reg->shapes[idx].u.tvec.elem_idx = elem_idx;
  reg->count++;
  return idx;
}

/* Intern a typed-map shape. key_idx == UINT32_MAX encodes dyn keys
 * (the [Map V] form). */
static uint32_t type_shape_intern_typed_map(StructTypeRegistry* reg,
                                            uint32_t key_idx,
                                            uint32_t value_idx) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind == TYPE_SHAPE_TYPED_MAP &&
        reg->shapes[i].u.tmap.key_idx == key_idx &&
        reg->shapes[i].u.tmap.value_idx == value_idx) {
      return i;
    }
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_TYPED_MAP;
  reg->shapes[idx].u.tmap.key_idx   = key_idx;
  reg->shapes[idx].u.tmap.value_idx = value_idx;
  reg->count++;
  return idx;
}

/* Intern a buf shape [Buf N T]. Phase 5. */
static uint32_t type_shape_intern_buf(StructTypeRegistry* reg,
                                       uint32_t len, uint32_t elem_idx) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind == TYPE_SHAPE_BUF &&
        reg->shapes[i].u.buf.len == len &&
        reg->shapes[i].u.buf.elem_idx == elem_idx) {
      return i;
    }
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_BUF;
  reg->shapes[idx].u.buf.len      = len;
  reg->shapes[idx].u.buf.elem_idx = elem_idx;
  reg->count++;
  return idx;
}

/* Intern a ptr shape [Ptr T]. Phase 5. */
static uint32_t type_shape_intern_ptr(StructTypeRegistry* reg,
                                       uint32_t pointee_idx) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind == TYPE_SHAPE_PTR &&
        reg->shapes[i].u.ptr.pointee_idx == pointee_idx) {
      return i;
    }
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_PTR;
  reg->shapes[idx].u.ptr.pointee_idx = pointee_idx;
  reg->count++;
  return idx;
}

/* Intern a future shape [Future T]. Phase 5. */
static uint32_t type_shape_intern_future(StructTypeRegistry* reg,
                                          uint32_t resolves_to_idx) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind == TYPE_SHAPE_FUTURE &&
        reg->shapes[i].u.future.resolves_to_idx == resolves_to_idx) {
      return i;
    }
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_FUTURE;
  reg->shapes[idx].u.future.resolves_to_idx = resolves_to_idx;
  reg->count++;
  return idx;
}

/* Intern a box shape [Box T]. Phase 5. */
static uint32_t type_shape_intern_box(StructTypeRegistry* reg,
                                       uint32_t boxes_idx) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind == TYPE_SHAPE_BOX &&
        reg->shapes[i].u.box.boxes_idx == boxes_idx) {
      return i;
    }
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_BOX;
  reg->shapes[idx].u.box.boxes_idx = boxes_idx;
  reg->count++;
  return idx;
}

#endif /* SHAPES_C */
