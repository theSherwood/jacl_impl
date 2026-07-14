/* collections.c — JACL runtime collections (P1.5): persistent vector.
 *
 * Instantiates the real `lib/rrb_vec` (an include-as-template, proven to compile
 * through svm-llvm in Spike-1b) for `JaclVal` elements, with its allocation hooks
 * wired to the GC heap:
 *   - nodes (leaf/internal/root) -> jacl_alloc(JOBJ_NODE) so the GC traces them
 *     (conservative payload scan + interior-pointer resolution finds child node
 *     pointers AND heap-valued elements);
 *   - size tables / temp arrays -> jacl_alloc(JOBJ_BLOB) (marked via the owning
 *     node's pointer, never traced; freed by the GC, so the allocator's free is a
 *     no-op).
 *
 * A vector value is JACL_TAG_VECTOR over the RRB root cell. Reachability flows
 * value -> root -> internals -> leaves -> elements, so a live vector keeps its
 * heap elements alive; a dropped vector's nodes (and exclusively-owned elements)
 * are reclaimed.
 */
#include "jaclrt.h"
#include <stddef.h>

/* node allocation -> a traced GC cell; returns the node payload (rrb's storage). */
static void* jvec_gc_alloc(int obj_type, unsigned long sz) {
  (void)obj_type;
  return jacl_obj_payload((JaclObj*)jacl_alloc(JOBJ_NODE, (uint32_t)sz));
}
/* size-table / temp allocator -> an untraced GC blob; free is a no-op (GC owns it). */
static void* jvec_aux_alloc(void *c, size_t n) { (void)c; return jacl_obj_payload((JaclObj*)jacl_alloc(JOBJ_BLOB, (uint32_t)n)); }
static void* jvec_aux_realloc(void *c, void *p, size_t n) { (void)c; (void)p; return jvec_aux_alloc(c, n); }
static void  jvec_aux_free(void *c, void *p) { (void)c; (void)p; }

#define RRB_VEC_T               JaclVal
#define RRB_VEC_NAME            jvec
#define RRB_VEC_GC_ALLOC(ot,sz) jvec_gc_alloc((ot),(sz))
#define RRB_VEC_GC_OBJ_INTERNAL JOBJ_NODE
#define RRB_VEC_GC_OBJ_LEAF     JOBJ_NODE
#define RRB_VEC_GC_OBJ_ROOT     JOBJ_NODE
#define RRB_VEC_ALLOCATOR       ((Allocator){ .alloc=jvec_aux_alloc, .realloc=jvec_aux_realloc, .free=jvec_aux_free, .ctx=0 })
#include "../lib/rrb_vec/rrb_vec.h"

/* Public vector API — noinline so vectors are opaque runtime values (live on the
 * stack across a collection, visible to gc.roots), per the runtime-opacity rule. */
__attribute__((noinline)) JaclVal jacl_vec_empty(void) {
  return jaclrt_from_ptr(JACL_TAG_VECTOR, jvec_empty());
}
__attribute__((noinline)) JaclVal jacl_vec_push(JaclVal vec, JaclVal elem) {
  jvec_root *r = (jvec_root*)jaclrt_as_ptr(vec);
  return jaclrt_from_ptr(JACL_TAG_VECTOR, jvec_push_back(r, elem));
}
__attribute__((noinline)) JaclVal jacl_vec_get(JaclVal vec, uint32_t idx) {
  jvec_root *r = (jvec_root*)jaclrt_as_ptr(vec);
  jvec_get_result g = jvec_get(r, idx);
  return g.found ? g.value : JACL_NIL;
}
__attribute__((noinline)) uint32_t jacl_vec_count(JaclVal vec) {
  return jvec_count((jvec_root*)jaclrt_as_ptr(vec));
}

/* ===================================================================
 * Persistent map (HAMT) over the GC heap — JaclVal -> JaclVal (P1.5).
 * Nodes are JOBJ_NODE (traced: leaf slots hold key/value JaclVals, internal
 * nodes hold child pointers — all found by the conservative payload scan).
 * Empty map = JACL_TAG_MAP over a NULL root.
 * =================================================================== */
static void* jmap_gc_alloc(int obj_type, unsigned long sz) {
  (void)obj_type;
  return jacl_obj_payload((JaclObj*)jacl_alloc(JOBJ_NODE, (uint32_t)sz));
}

/* value-aware key hash/eq: strings by content, vectors structurally (elementwise,
 * recursive — consistent with jacl_val_equal so composite keys work), everything
 * else by bits. jacl_val_equal lives in builtins.c (later in the unity TU). */
int jacl_val_equal(JaclVal a, JaclVal b); /* fwd (unity) */
static uint32_t jmap_key_hash(JaclVal k) {
  if (jaclrt_is_string(k)) return jacl_str_hash(k);
  if (jaclrt_type_index(k) == 0x06) {              /* VECTOR: FNV over element hashes */
    uint32_t n = jacl_vec_count(k);
    uint32_t h = 0x811c9dc5u ^ n;
    for (uint32_t i = 0; i < n; i++) h = (h ^ jmap_key_hash(jacl_vec_get(k, i))) * 16777619u;
    return h;
  }
  uint64_t x = k;                                  /* scalar: mix the bits (splitmix64-ish) */
  x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
  return (uint32_t)x;
}
static bool jmap_key_eq(JaclVal a, JaclVal b) { return jacl_val_equal(a, b) != 0; }

#define HAMT_KEY_T              JaclVal
#define HAMT_VAL_T              JaclVal
#define HAMT_NAME               jmap
#define HAMT_GC_ALLOC(ot,sz)    jmap_gc_alloc((ot),(sz))
#define HAMT_GC_OBJ_INTERNAL    JOBJ_NODE
#define HAMT_GC_OBJ_LEAF        JOBJ_NODE
#define HAMT_GC_OBJ_COLLISION   JOBJ_NODE
#include "../lib/hamt/hamt.h"

void jacl_map_init(void) { jmap_set_key_handlers(jmap_key_hash, jmap_key_eq); }

__attribute__((noinline)) JaclVal jacl_map_empty(void) {
  return jaclrt_from_ptr(JACL_TAG_MAP, (void*)0);
}
__attribute__((noinline)) JaclVal jacl_map_set(JaclVal m, JaclVal key, JaclVal val) {
  jmap_node *r = (jmap_node*)jaclrt_as_ptr(m);
  return jaclrt_from_ptr(JACL_TAG_MAP, jmap_set(r, key, val));
}
__attribute__((noinline)) int jacl_map_has(JaclVal m, JaclVal key) {
  return jmap_has((jmap_node*)jaclrt_as_ptr(m), key);
}
__attribute__((noinline)) JaclVal jacl_map_get(JaclVal m, JaclVal key) {
  jmap_node *r = (jmap_node*)jaclrt_as_ptr(m);
  if (!jmap_has(r, key)) return JACL_NIL;
  return jmap_get(r, key);
}
__attribute__((noinline)) JaclVal jacl_map_remove(JaclVal m, JaclVal key) {
  jmap_node *r = (jmap_node*)jaclrt_as_ptr(m);
  return jaclrt_from_ptr(JACL_TAG_MAP, jmap_unset(r, key));
}
/* Persistent element update: a new vector with v[idx] = elem (nil-vec/OOB -> error). */
__attribute__((noinline)) JaclVal jacl_vec_set(JaclVal vec, uint32_t idx, JaclVal elem) {
  jvec_root *r = (jvec_root*)jaclrt_as_ptr(vec);
  jvec_root *out = jvec_set(r, idx, elem);
  if (!out) return jaclrt_error();
  return jaclrt_from_ptr(JACL_TAG_VECTOR, out);
}
/* Persistent concatenation of two vectors. */
__attribute__((noinline)) JaclVal jacl_vec_concat(JaclVal a, JaclVal b) {
  if (jaclrt_is_error(a)) return a;
  if (jaclrt_is_error(b)) return b;
  jvec_root *ra = (jvec_root*)jaclrt_as_ptr(a), *rb = (jvec_root*)jaclrt_as_ptr(b);
  return jaclrt_from_ptr(JACL_TAG_VECTOR, jvec_concat(ra, rb));
}
/* Copy up to `cap` (key, value) entries into ks/vs; returns the number written.
 * Structural equality, rendering, and map-keys/vals build on this. */
__attribute__((noinline)) uint32_t jacl_map_entries(JaclVal m, JaclVal *ks, JaclVal *vs, uint32_t cap) {
  jmap_node *root = (jmap_node*)jaclrt_as_ptr(m);
  if (!root) return 0;
  jmap_iter it = jmap_iter_init(root);
  uint32_t n = 0;
  while (n < cap) {
    jmap_iter_result r = jmap_next_leaf(&it);
    if (r.done) break;
    ks[n] = r.item->slots[0];
    vs[n] = *(JaclVal *)((char *)r.item->slots + sizeof(JaclVal) * r.item->key_stride);
    n++;
  }
  return n;
}
__attribute__((noinline)) uint32_t jacl_map_count(JaclVal m) {
  return (uint32_t)jmap_count((jmap_node*)jaclrt_as_ptr(m));
}

/* ===================================================================
 * Mutable array (JACL_TAG_ARR, mirrors the old VM's [Arr T] semantics
 * dynamically): a HEADER cell so every alias sees mutations —
 *   header (JOBJ_NODE): [0] count (JaclVal i32), [1] cap (JaclVal i32),
 *                       [2] data (JaclVal -> data cell)
 *   data   (JOBJ_NODE): cap JaclVal element slots (traced).
 * Growth allocates a doubled data cell and repoints the header, so
 * outstanding references (aliases) keep observing the same array.
 * =================================================================== */
#define JACL_ARR_MIN_CAP 8
static JaclVal arr_data_new(int32_t cap) {
  JaclObj *d = (JaclObj *)jacl_alloc(JOBJ_NODE, (uint32_t)cap * 8u);
  return jaclrt_from_ptr(JACL_TAG_VECTOR, d);   /* tag irrelevant; traced via header */
}
__attribute__((noinline)) JaclVal jacl_arr_new(void) {
  JaclObj *h = (JaclObj *)jacl_alloc(JOBJ_NODE, 3 * 8u);
  JaclVal *p = (JaclVal *)jacl_obj_payload(h);
  p[0] = jaclrt_i32(0);
  p[1] = jaclrt_i32(JACL_ARR_MIN_CAP);
  p[2] = arr_data_new(JACL_ARR_MIN_CAP);
  return jaclrt_from_ptr(JACL_TAG_ARR, h);
}
static JaclVal *arr_hdr(JaclVal a) {
  if (jaclrt_type_index(a) != 0x1A) return 0;
  return (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(a));
}
__attribute__((noinline)) uint32_t jacl_arr_count(JaclVal a) {
  JaclVal *h = arr_hdr(a);
  return h ? (uint32_t)jaclrt_as_i32(h[0]) : 0;
}
__attribute__((noinline)) JaclVal jacl_arr_get(JaclVal a, uint32_t i) {
  JaclVal *h = arr_hdr(a);
  if (!h || i >= (uint32_t)jaclrt_as_i32(h[0])) return jaclrt_error();
  return ((JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(h[2])))[i];
}
__attribute__((noinline)) JaclVal jacl_arr_push(JaclVal a, JaclVal v) {
  JaclVal *h = arr_hdr(a);
  if (!h) return jaclrt_error();
  int32_t n = jaclrt_as_i32(h[0]), cap = jaclrt_as_i32(h[1]);
  if (n == cap) {                                  /* grow: copy into a doubled data cell */
    int32_t ncap = cap * 2;
    JaclVal nd = arr_data_new(ncap);
    JaclVal *src = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(h[2]));
    JaclVal *dst = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(nd));
    for (int32_t k = 0; k < n; k++) dst[k] = src[k];
    h = arr_hdr(a);                                /* re-read: alloc may have collected */
    h[2] = nd;
    h[1] = jaclrt_i32(ncap);
  }
  ((JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(h[2])))[n] = v;
  h[0] = jaclrt_i32(n + 1);
  return a;
}
__attribute__((noinline)) JaclVal jacl_arr_pop(JaclVal a) {
  JaclVal *h = arr_hdr(a);
  if (!h) return jaclrt_error();
  int32_t n = jaclrt_as_i32(h[0]);
  if (n <= 0) return jaclrt_error();
  JaclVal *d = (JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(h[2]));
  JaclVal v = d[n - 1];
  d[n - 1] = JACL_NIL;
  h[0] = jaclrt_i32(n - 1);
  return v;
}
JaclVal jacl_arr_get_at(JaclVal a, JaclVal idx) {
  if (jaclrt_is_error(a)) return a;
  if (!jaclrt_is_i32(idx) || jaclrt_as_i32(idx) < 0) return jaclrt_error();
  return jacl_arr_get(a, (uint32_t)jaclrt_as_i32(idx));
}
JaclVal jacl_arr_set_at(JaclVal a, JaclVal idx, JaclVal v) {
  JaclVal *h = arr_hdr(a);
  if (jaclrt_is_error(a)) return a;
  if (!h || !jaclrt_is_i32(idx)) return jaclrt_error();
  int32_t i = jaclrt_as_i32(idx), n = jaclrt_as_i32(h[0]);
  if (i < 0 || i >= n) return jaclrt_error();
  ((JaclVal *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(h[2])))[i] = v;
  return v;
}
/* Element access across index-able collections (for-each's accessor). */
JaclVal jacl_index_get(JaclVal coll, JaclVal idx) {
  if (jaclrt_type_index(coll) == 0x1A) return jacl_arr_get_at(coll, idx);
  return jacl_vec_get_at(coll, idx);
}
