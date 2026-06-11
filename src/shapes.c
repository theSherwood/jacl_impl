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
  /* slot_ref_bitmap: bit k is 1 if JaclVal-sized slot k within the struct's
   * inline bytes holds a GC-traced tagged JaclVal (the slot range of a
   * ref-elem buf field). bit 0 (raw bytes) means the struct walker / inline
   * pusher treats the slot like every other value-type struct slot. 32 bytes
   * = 256 bits covers structs up to 2 KiB. Computed at decl time once all
   * field offsets are known. See BUFFER_DESIGN.md (Tier 1: ref-elem bufs as
   * struct fields). */
  uint8_t     slot_ref_bitmap[32];
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
  TYPE_SHAPE_TYPED_ARR,        /* [Arr T] -- u.tarr.elem_idx. See ARR_DESIGN.md */
  TYPE_SHAPE_PROC,             /* [Proc [P…] R] -- u.proc.{params_off into the
                                * registry's proc_param_pool, param_count,
                                * return_idx}. TYPED_CLOSURES_DESIGN.md Phase B3. */
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
    struct { uint32_t elem_idx; } tarr;          /* TYPED_ARR */
    struct { uint32_t params_off;                /* PROC: offset into
                                                  * proc_param_pool */
             uint16_t param_count;
             uint32_t return_idx; } proc;
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
  /* Side pool for TYPE_SHAPE_PROC param-idx lists (variable arity). Each proc
   * shape's u.proc.params_off indexes here. malloc-backed (not arena), grown
   * by doubling, freed in destroy. See TYPED_CLOSURES_DESIGN.md Phase B3. */
  uint32_t* proc_param_pool;
  uint32_t  proc_param_count;
  uint32_t  proc_param_cap;
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

/* INVARIANT (pointer stability): this reallocs reg->defs / reg->shapes (and the
 * intern fns separately realloc proc_param_pool), so any `StructTypeDef*` /
 * `TypeShape*` / proc_param_pool pointer is INVALIDATED across an intern. Entry
 * INDICES are stable; raw pointers are not. Callers must re-fetch by index
 * after any intern, never cache a pointer across one. All current call sites
 * honor this (read-only decode/format paths, plus intern fns that grow-then-
 * index). See docs/TYPER_SHAPE_UNIFICATION_AUDIT.md (pointer-stability audit). */
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
  reg->proc_param_pool = NULL;
  reg->proc_param_count = 0;
  reg->proc_param_cap = 0;
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
  reg->proc_param_pool = NULL;
  reg->proc_param_count = 0;
  reg->proc_param_cap = 0;
}

static void struct_registry__destroy(StructTypeRegistry* reg) {
  if (!reg) return;
  free(reg->defs);
  free(reg->shapes);
  free(reg->proc_param_pool);
  reg->defs = NULL;
  reg->shapes = NULL;
  reg->proc_param_pool = NULL;
  reg->proc_param_count = 0;
  reg->proc_param_cap = 0;
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

/* Intern a typed-arr shape (mutable [Arr T]). Mirrors typed-vec: returns
 * the registry idx, or UINT32_MAX on allocation failure; identical
 * (elem_idx) requests share an idx. See ARR_DESIGN.md. */
static uint32_t type_shape_intern_typed_arr(StructTypeRegistry* reg,
                                            uint32_t elem_idx) {
  if (!reg) return UINT32_MAX;
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind == TYPE_SHAPE_TYPED_ARR &&
        reg->shapes[i].u.tarr.elem_idx == elem_idx) {
      return i;
    }
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_TYPED_ARR;
  reg->shapes[idx].u.tarr.elem_idx = elem_idx;
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

/* Intern a [Proc [P…] R] shape. param_idxs[0..count) are portable encodings
 * (scalar sentinel / struct idx / nested shape idx); return_idx likewise.
 * Identical signatures share an idx. Returns UINT32_MAX on allocation
 * failure. TYPED_CLOSURES_DESIGN.md Phase B3. */
static uint32_t type_shape_intern_proc(StructTypeRegistry* reg,
                                       const uint32_t* param_idxs,
                                       uint16_t param_count,
                                       uint32_t return_idx) {
  if (!reg) return UINT32_MAX;
  /* Dedup: same return, same arity, same param idxs. */
  for (uint32_t i = 1; i < reg->count; i++) {
    if (reg->shapes[i].kind != TYPE_SHAPE_PROC) continue;
    if (reg->shapes[i].u.proc.param_count != param_count) continue;
    if (reg->shapes[i].u.proc.return_idx != return_idx) continue;
    uint32_t off = reg->shapes[i].u.proc.params_off;
    bool same = true;
    for (uint16_t k = 0; k < param_count; k++) {
      if (reg->proc_param_pool[off + k] != param_idxs[k]) { same = false; break; }
    }
    if (same) return i;
  }
  if (!struct_registry__grow(reg)) return UINT32_MAX;
  /* Append the param list to the pool (grow by doubling). */
  uint32_t off = reg->proc_param_count;
  if (reg->proc_param_count + param_count > reg->proc_param_cap) {
    uint32_t new_cap = reg->proc_param_cap ? reg->proc_param_cap * 2 : 16;
    while (new_cap < reg->proc_param_count + param_count) new_cap *= 2;
    uint32_t* np = (uint32_t*)realloc(reg->proc_param_pool,
                                      new_cap * sizeof(uint32_t));
    if (!np) return UINT32_MAX;
    reg->proc_param_pool = np;
    reg->proc_param_cap = new_cap;
  }
  for (uint16_t k = 0; k < param_count; k++)
    reg->proc_param_pool[off + k] = param_idxs[k];
  reg->proc_param_count += param_count;
  uint32_t idx = reg->count;
  reg->defs[idx] = NULL;
  reg->shapes[idx].kind = TYPE_SHAPE_PROC;
  reg->shapes[idx].u.proc.params_off = off;
  reg->shapes[idx].u.proc.param_count = param_count;
  reg->shapes[idx].u.proc.return_idx = return_idx;
  reg->count++;
  return idx;
}

#endif /* SHAPES_C */
