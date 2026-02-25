/*
 * JACL Persistent Collections
 *
 * Instantiates the RRB vector and HAMT templates with JaclVal types.
 * Provides hash and equality functions for JaclVal keys.
 */

#ifndef COLLECTIONS_C
#define COLLECTIONS_C

/* --- Forward declarations (defined after template instantiation) --- */

static uint32_t jacl_val_hash(JaclVal v);
static bool jacl_val_eq(JaclVal a, JaclVal b);

/* --- Allocator for collection templates (static compound literal) --- */

#define JACL_COLL_ALLOCATOR { .alloc = libc_alloc, .free = libc_free, .ctx = NULL }

/* --- Instantiate RRB vector template: jacl_vec --- */

#define RRB_VEC_T         JaclVal
#define RRB_VEC_NAME      jacl_vec
#define RRB_VEC_ALLOCATOR JACL_COLL_ALLOCATOR
#include "../lib/rrb_vec/rrb_vec.h"

/* Clean up RC_ALLOCATOR leaked by rrb_vec.h's rc.h inclusion */
#undef RC_ALLOCATOR

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

/* --- JaclVal hash function (dispatches on type tag) --- */

static uint32_t jacl_val_hash(JaclVal v) {
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

  /* Fallback: hash the raw payload bits */
  uint64_t payload = v & JACL_PAYLOAD_MASK;
  return (uint32_t)(payload ^ (payload >> 32));
}

/* --- JaclVal equality function (for HAMT key comparison) --- */

static bool jacl_val_eq(JaclVal a, JaclVal b) {
  uint64_t tag_a = jacl_type_tag(a);
  uint64_t tag_b = jacl_type_tag(b);

  /* String equality needs special handling (inline vs heap) */
  if ((tag_a == JACL_TAG_INLINE_STRING || tag_a == JACL_TAG_STRING) &&
      (tag_b == JACL_TAG_INLINE_STRING || tag_b == JACL_TAG_STRING)) {
    return jacl_string_eq(a, b);
  }

  /* Different type tags → not equal */
  if (tag_a != tag_b) return false;

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

  /* For all other types: same tag + same payload = equal */
  return (a & JACL_PAYLOAD_MASK) == (b & JACL_PAYLOAD_MASK);
}

/* --- Initialization: wire up hash/eq handlers --- */

static void collections__init(void) {
  jacl_map_set_key_handlers(jacl_val_hash, jacl_val_eq);
  jacl_vec_set_hash_handler(jacl_val_hash);
  jacl_map_set_hash_handlers(jacl_val_hash, jacl_val_hash);
}

#endif /* COLLECTIONS_C */
