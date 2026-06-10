/*
 * JACL Persistent Collections
 *
 * Instantiates the RRB vector and HAMT templates with JaclVal types.
 * Provides hash and equality functions for JaclVal keys.
 */

#ifndef COLLECTIONS_C
#define COLLECTIONS_C

/* --- Forward declarations (defined after template instantiation) --- */

uint32_t jacl_val_hash(JaclVal v);
bool jacl_val_eq(JaclVal a, JaclVal b);

/* --- Allocator for collection auxiliary allocations (size tables, temp arrays) --- */

#define JACL_COLL_ALLOCATOR { .alloc = libc_alloc, .free = libc_free, .ctx = NULL }

/* --- Instantiate RRB vector template: jacl_vec (GC mode) --- */

#define RRB_VEC_T                JaclVal
#define RRB_VEC_NAME             jacl_vec
#define RRB_VEC_ALLOCATOR        JACL_COLL_ALLOCATOR
#define RRB_VEC_GC_MODE
#define RRB_VEC_GC_ALLOC(t, sz)  gc_alloc(gc__current_heap, (t), (sz))
#define RRB_VEC_GC_OBJ_INTERNAL  OBJ_RRB_INTERNAL
#define RRB_VEC_GC_OBJ_LEAF      OBJ_RRB_LEAF
#define RRB_VEC_GC_OBJ_ROOT      OBJ_RRB_ROOT
#include "../lib/rrb_vec/rrb_vec.h"

/* --- Instantiate HAMT template: jacl_map (GC mode) --- */

#define HAMT_KEY_T             JaclVal
#define HAMT_VAL_T             JaclVal
#define HAMT_NAME              jacl_map
#define HAMT_GC_MODE
#define HAMT_GC_ALLOC(t, sz)   gc_alloc(gc__current_heap, (t), (sz))
#define HAMT_GC_OBJ_INTERNAL   OBJ_HAMT_INTERNAL
#define HAMT_GC_OBJ_LEAF       OBJ_HAMT_LEAF
#define HAMT_GC_OBJ_COLLISION  OBJ_HAMT_COLLISION
#include "../lib/hamt/hamt.h"

/* --- Instantiate typed RRB vector template: jacl_typed_vec (GC-safe for raw struct bytes) --- */

#define RRB_VEC_T                JaclVal
#define RRB_VEC_NAME             jacl_typed_vec
#define RRB_VEC_ALLOCATOR        JACL_COLL_ALLOCATOR
#define RRB_VEC_GC_MODE
#define RRB_VEC_GC_ALLOC(t, sz)  gc_alloc(gc__current_heap, (t), (sz))
#define RRB_VEC_GC_OBJ_INTERNAL  OBJ_RRB_INTERNAL
#define RRB_VEC_GC_OBJ_LEAF      OBJ_TYPED_RRB_LEAF
#define RRB_VEC_GC_OBJ_ROOT      OBJ_RRB_ROOT
#include "../lib/rrb_vec/rrb_vec.h"

/* --- Instantiate typed HAMT template: jacl_typed_map (GC-safe for raw struct value bytes) --- */

#define HAMT_KEY_T             JaclVal
#define HAMT_VAL_T             JaclVal
#define HAMT_NAME              jacl_typed_map
#define HAMT_GC_MODE
#define HAMT_GC_ALLOC(t, sz)   gc_alloc(gc__current_heap, (t), (sz))
#define HAMT_GC_OBJ_INTERNAL   OBJ_HAMT_INTERNAL
#define HAMT_GC_OBJ_LEAF       OBJ_TYPED_HAMT_LEAF
#define HAMT_GC_OBJ_COLLISION  OBJ_HAMT_COLLISION
#include "../lib/hamt/hamt.h"

/* --- Mutable [Arr T] backing: runtime-stride segment array (sa_var) ---
 *
 * Backs the mutable [Arr T] reference type. Unlike vec/map (whose tree nodes
 * are each GC objects), the segment backing store is malloc'd via this
 * allocator and freed at finalize time; the JaclArr header is the single GC
 * object. A single sa_var backs every arr: dyn arrays set elem_size = 8 and
 * store tagged JaclVals; typed arrays set elem_size to the element's byte
 * width and store raw bytes (M4c+). See ARR_DESIGN.md. */

#include "../lib/segment_array/segment_array_var.h"

/* Stable malloc-backed allocator for arr segments. Referenced by sa_var's
 * allocator field and used by the GC finalizer at sweep, so it must outlive
 * every arr — hence file-scope constant. */
static const Allocator jacl_arr_seg_allocator = JACL_COLL_ALLOCATOR;

/* The single GC object for an [Arr T]. elem_idx carries the element-type
 * encoding (JACL_SCALAR_TYPE_IDX(TYPE_DYN) for the dyn [arr ...] form;
 * scalar sentinel / registry idx for typed arrays). The runtime-stride
 * backing (sa.elem_size = element bytes) is inline. */
typedef struct {
  uint32_t elem_idx;
  sa_var_t sa;
} JaclArr;

/* Allocate + initialize an empty arr. elem_size is the per-element byte width
 * (sizeof(JaclVal) for dyn / scalar-as-tagged; element byte size for flat
 * typed arrays). */
JaclArr* jacl_arr_new(uint32_t elem_idx, uint32_t elem_size) {
  JaclArr* a = (JaclArr*)gc_alloc(gc__current_heap, OBJ_ARR, sizeof(JaclArr));
  if (!a) return NULL;
  a->elem_idx = elem_idx;
  sa_var_init(&a->sa, &jacl_arr_seg_allocator, elem_size);
  return a;
}

/* Free only the malloc'd segment backing store; the JaclArr header itself is
 * GC memory, reclaimed by sweep. Called from gc__finalize_dead on OBJ_ARR. */
void jacl_arr_free_segments(JaclArr* a) {
  if (a) sa_var_free_segments(&a->sa);
}

/* --- JaclVal hash function (dispatches on type tag) --- */

uint32_t jacl_val_hash(JaclVal v) {
  uint64_t tag = jacl_type_tag(v);

  if (tag == JACL_TAG_NIL) {
    return 0x9E3779B9u; /* golden ratio hash for nil */
  }
  if (tag == JACL_TAG_BOOL) {
    return jacl_as_bool(v) ? 0x01000193u : 0x811C9DC5u;
  }
  if (tag == JACL_TAG_I32) {
    uint32_t x = (uint32_t)jacl_as_i32(v);
    /* murmur-style mixing */
    x ^= x >> 16;
    x *= 0x45D9F3Bu;
    x ^= x >> 16;
    x *= 0x45D9F3Bu;
    x ^= x >> 16;
    return x;
  }
  if (tag == JACL_TAG_F32) {
    uint32_t bits = (uint32_t)(v & JACL_PAYLOAD_MASK);
    /* Normalize -0.0 to +0.0 */
    if (bits == 0x80000000u) bits = 0;
    bits ^= bits >> 16;
    bits *= 0x45D9F3Bu;
    bits ^= bits >> 16;
    return bits;
  }
  if (tag == JACL_TAG_INLINE_STRING) {
    /* FNV-1a over inline bytes */
    uint64_t payload = v & JACL_PAYLOAD_MASK;
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 7; i++) {
      unsigned char c = (unsigned char)((payload >> (i * 8)) & 0xFF);
      if (c == 0) break;
      hash ^= c;
      hash *= 16777619u;
    }
    return hash;
  }
  if (tag == JACL_TAG_STRING) {
    /* Use precomputed hash from heap string */
    JaclHeapString* hs = jacl_as_heap_string(v);
    return hs->hash;
  }
  if (tag == JACL_TAG_ROPE_STRING) {
    /* Use precomputed hash from rope string */
    JaclRopeString* rs = jacl_as_rope_string(v);
    return rs->hash;
  }
  /* Boxed wide scalars (heap-allocated i64/u64/f64): hash the VALUE, not
   * the pointer — two separately boxed i64(1)s must collide so they work
   * as map keys (jacl_val_eq compares them by value, below). */
  if (tag == JACL_TAG_I64 || tag == JACL_TAG_U64) {
    uint64_t x = (tag == JACL_TAG_I64)
        ? (uint64_t)jacl_as_i64(v) : jacl_as_u64(v);
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33;
    return (uint32_t)x;
  }
  if (tag == JACL_TAG_F64) {
    double d = jacl_as_f64(v);
    if (d == 0.0) d = 0.0;  /* normalize -0.0 to +0.0 */
    uint64_t x;
    memcpy(&x, &d, sizeof(x));
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33;
    return (uint32_t)x;
  }

  if (tag == JACL_TAG_VECTOR) {
    /* O(1) cached structural hash from RRB root */
    jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(v);
    return jacl_vec_hash(vec);
  }
  if (tag == JACL_TAG_MAP) {
    /* O(1) cached structural hash from HAMT root node */
    jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(v);
    return jacl_map_node_hash(map);
  }

  /* Struct: hash type_idx + raw data bytes */
  if (tag == JACL_TAG_STRUCT) {
    HeapRecord* s = jacl_as_heap_record_ptr(v);
    uint32_t h = s->type_idx * 0x9E3779B9u;
    const uint8_t* p = s->data;
    for (uint32_t i = 0; i < s->total_size; i++) {
      h = h * 31 + p[i];
    }
    return h;
  }

  /* Box: hash type_idx + data bytes. For untyped boxes (including the
   * compact OBJ_BOX_INLINE layout) type_idx is 0 and the contents are
   * a single JaclVal — hash its raw bytes via the same loop. */
  if (tag == JACL_TAG_BOX) {
    void* payload = jacl_as_ptr(v);
    uint32_t type_idx;
    const uint8_t* p;
    uint32_t size;
    if (jacl_box_is_typed(payload)) {
      JaclMutableRef* ref = (JaclMutableRef*)payload;
      type_idx = ref->type_idx;
      p = ref->data;
      size = ref->total_size;
    } else {
      type_idx = 0;
      p = (const uint8_t*)jacl_box_untyped_val(payload);
      size = sizeof(JaclVal);
    }
    uint32_t h = type_idx * 0x9E3779B9u;
    for (uint32_t i = 0; i < size; i++) {
      h = h * 31 + p[i];
    }
    return h;
  }

  /* Fallback: hash the raw payload bits */
  uint64_t payload = v & JACL_PAYLOAD_MASK;
  return (uint32_t)(payload ^ (payload >> 32));
}

/* --- JaclVal equality function (for HAMT key comparison) --- */

bool jacl_val_eq(JaclVal a, JaclVal b) {
  uint64_t tag_a = jacl_type_tag(a);
  uint64_t tag_b = jacl_type_tag(b);

  /* String equality needs special handling (inline vs heap vs rope) */
  if (jacl_is_string(a) && jacl_is_string(b)) {
    return jacl_string_eq(a, b);
  }

  /* Different type tags → not equal */
  if (tag_a != tag_b) return false;

  /* Boxed wide scalars: compare by VALUE (the payload is a heap pointer;
   * pointer identity would make every boxed i64/u64/f64 unequal to every
   * other, breaking them as map keys). */
  if (tag_a == JACL_TAG_I64) return jacl_as_i64(a) == jacl_as_i64(b);
  if (tag_a == JACL_TAG_U64) return jacl_as_u64(a) == jacl_as_u64(b);
  if (tag_a == JACL_TAG_F64) return jacl_as_f64(a) == jacl_as_f64(b);

  /* Vector structural equality */
  if (tag_a == JACL_TAG_VECTOR) {
    jacl_vec_root* va = (jacl_vec_root*)jacl_as_ptr(a);
    jacl_vec_root* vb = (jacl_vec_root*)jacl_as_ptr(b);
    if (va == vb) return true;
    uint32_t len = jacl_vec_count(va);
    if (len != jacl_vec_count(vb)) return false;
    for (uint32_t i = 0; i < len; i++) {
      jacl_vec_get_result ga = jacl_vec_get(va, i);
      jacl_vec_get_result gb = jacl_vec_get(vb, i);
      if (!jacl_val_eq(ga.value, gb.value)) return false;
    }
    return true;
  }

  /* Map structural equality */
  if (tag_a == JACL_TAG_MAP) {
    jacl_map_node* ma = (jacl_map_node*)jacl_as_ptr(a);
    jacl_map_node* mb = (jacl_map_node*)jacl_as_ptr(b);
    if (ma == mb) return true;
    if (jacl_map_count(ma) != jacl_map_count(mb)) return false;
    jacl_map_iter it = jacl_map_iter_init(ma);
    jacl_map_iter_result ir;
    for (;;) {
      ir = jacl_map_next_leaf(&it);
      if (ir.done) break;
      JaclVal key = jacl_map_key_from_leaf(ir.item);
      JaclVal val_a = jacl_map_value_from_leaf(ir.item);
      if (!jacl_map_has(mb, key)) return false;
      JaclVal val_b = jacl_map_get(mb, key);
      if (!jacl_val_eq(val_a, val_b)) return false;
    }
    return true;
  }

  /* Struct equality: same type_idx AND memcmp of raw data bytes */
  if (tag_a == JACL_TAG_STRUCT) {
    HeapRecord* sa = jacl_as_heap_record_ptr(a);
    HeapRecord* sb = jacl_as_heap_record_ptr(b);
    if (sa == sb) return true;
    if (sa->type_idx != sb->type_idx) return false;
    return memcmp(sa->data, sb->data, sa->total_size) == 0;
  }

  /* Box equality: untyped boxes compare contained JaclVal; struct boxes
   * compare type_idx and raw bytes. Boxes carrying the compact
   * OBJ_BOX_INLINE layout are always untyped (effective type_idx = 0). */
  if (tag_a == JACL_TAG_BOX) {
    void* pa = jacl_as_ptr(a);
    void* pb = jacl_as_ptr(b);
    if (pa == pb) return true;
    bool a_typed = jacl_box_is_typed(pa);
    bool b_typed = jacl_box_is_typed(pb);
    if (a_typed != b_typed) return false;
    if (!a_typed) {
      /* both untyped — compare the single JaclVal */
      return jacl_val_eq(*jacl_box_untyped_val(pa),
                        *jacl_box_untyped_val(pb));
    }
    /* both typed (struct box) — both must have OBJ_MUTABLE_REF layout */
    JaclMutableRef* ra = (JaclMutableRef*)pa;
    JaclMutableRef* rb = (JaclMutableRef*)pb;
    if (ra->type_idx != rb->type_idx) return false;
    if (ra->total_size != rb->total_size) return false;
    return memcmp(ra->data, rb->data, ra->total_size) == 0;
  }

  /* For all other types: same tag + same payload = equal */
  return (a & JACL_PAYLOAD_MASK) == (b & JACL_PAYLOAD_MASK);
}

/* --- Initialization: wire up hash/eq handlers --- */

void collections__init(void) {
  jacl_map_set_key_handlers(jacl_val_hash, jacl_val_eq);
  jacl_vec_set_hash_handler(jacl_val_hash);
  jacl_map_set_hash_handlers(jacl_val_hash, jacl_val_hash);
  jacl_typed_map_set_key_handlers(jacl_val_hash, jacl_val_eq);
  jacl_typed_map_set_hash_handlers(jacl_val_hash, jacl_val_hash);
}

#endif /* COLLECTIONS_C */
