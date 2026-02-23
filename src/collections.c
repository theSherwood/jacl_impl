/*
 * JACL Persistent Collections
 *
 * Instantiates the RRB vector and HAMT templates with JaclVal types.
 * Provides hash and equality functions for JaclVal keys.
 */

#ifndef COLLECTIONS_C
#define COLLECTIONS_C

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

  /* For all other types: same tag + same payload = equal */
  if (tag_a != tag_b) return false;
  return (a & JACL_PAYLOAD_MASK) == (b & JACL_PAYLOAD_MASK);
}

/* --- Allocator for collection templates (static compound literal) --- */

#define JACL_COLL_ALLOCATOR { .alloc = libc_alloc, .free = libc_free, .ctx = NULL }

/* --- Instantiate RRB vector template: jacl_vec --- */

#define RRB_VEC_T         JaclVal
#define RRB_VEC_NAME      jacl_vec
#define RRB_VEC_ALLOCATOR JACL_COLL_ALLOCATOR
#include "../lib/rrb_vec/rrb_vec.h"

/* Clean up RC_ALLOCATOR leaked by rrb_vec.h's rc.h inclusion */
#undef RC_ALLOCATOR

/* --- Instantiate HAMT template: jacl_map --- */

#define HAMT_KEY_T    JaclVal
#define HAMT_VAL_T    JaclVal
#define HAMT_NAME     jacl_map
#define HAMT_ALLOCATOR JACL_COLL_ALLOCATOR
#include "../lib/hamt/hamt.h"

/* --- Initialization: wire up HAMT key handlers --- */

static void collections__init(void) {
  jacl_map_set_key_handlers(jacl_val_hash, jacl_val_eq);
}

#endif /* COLLECTIONS_C */
