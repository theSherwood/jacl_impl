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
