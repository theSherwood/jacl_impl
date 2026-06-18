/* stream.c — JACL runtime streams (P1.6): stackless, pull-based iterators.
 *
 * Each combinator is a heap object (JOBJ_NODE so its `source` reference is traced;
 * JACL_TAG_STREAM). `next` pulls one element from the source and applies the
 * combinator's step — no fibers, no state machine (Design §4.3.1). Streams are
 * single-pass and mutate their index in place (the heap is non-moving, so the cell
 * address is stable). Transforms/predicates are C function pointers (stand-ins for
 * JACL closures, which arrive with Phase-2 codegen); stored as funcrefs in the
 * payload — code refs, never in the heap-data range, so conservative tracing skips
 * them.
 */
#include "jaclrt.h"
#include <stddef.h>

enum { STK_RANGE, STK_VEC, STK_MAP, STK_FILTER, STK_TAKE };

typedef struct {
  uint32_t kind;
  uint32_t _pad;
  JaclVal  source;   /* child stream (MAP/FILTER/TAKE) or vector (VEC); JACL_NIL for RANGE */
  int64_t  i;        /* current index (RANGE/VEC) */
  int64_t  n;        /* end (RANGE) / remaining (TAKE) */
  void    *fn;       /* JaclMapFn (MAP) / JaclPredFn (FILTER) */
} Stream;

static Stream* str_of(JaclVal v) { return (Stream*)jacl_obj_payload((JaclObj*)jaclrt_as_ptr(v)); }

static JaclVal mkstream(uint32_t kind, JaclVal source, int64_t i, int64_t n, void *fn) {
  JaclObj *o = (JaclObj*)jacl_alloc(JOBJ_NODE, (uint32_t)sizeof(Stream));
  Stream *s = (Stream*)jacl_obj_payload(o);
  s->kind = kind; s->_pad = 0; s->source = source; s->i = i; s->n = n; s->fn = fn;
  return jaclrt_from_ptr(JACL_TAG_STREAM, o);
}

/* constructors noinline so stream values are opaque runtime values (found by
   gc.roots when held across a collection). */
__attribute__((noinline)) JaclVal jacl_stream_range(int64_t n) { return mkstream(STK_RANGE, JACL_NIL, 0, n, NULL); }
__attribute__((noinline)) JaclVal jacl_stream_vec(JaclVal vec) { return mkstream(STK_VEC, vec, 0, 0, NULL); }
__attribute__((noinline)) JaclVal jacl_stream_map(JaclVal src, JaclMapFn f) { return mkstream(STK_MAP, src, 0, 0, (void*)f); }
__attribute__((noinline)) JaclVal jacl_stream_filter(JaclVal src, JaclPredFn p) { return mkstream(STK_FILTER, src, 0, 0, (void*)p); }
__attribute__((noinline)) JaclVal jacl_stream_take(JaclVal src, int64_t k) { return mkstream(STK_TAKE, src, 0, k, NULL); }

int jacl_stream_next(JaclVal sv, JaclVal *out) {
  Stream *s = str_of(sv);
  switch (s->kind) {                       /* switch on u32 kind -> i32 (svm-llvm OK) */
    case STK_RANGE:
      if (s->i < s->n) { *out = jaclrt_i32((int32_t)s->i); s->i++; return 1; }
      return 0;
    case STK_VEC: {
      uint32_t c = jacl_vec_count(s->source);
      if (s->i < (int64_t)c) { *out = jacl_vec_get(s->source, (uint32_t)s->i); s->i++; return 1; }
      return 0;
    }
    case STK_MAP: {
      JaclVal v;
      if (jacl_stream_next(s->source, &v)) { *out = ((JaclMapFn)s->fn)(v); return 1; }
      return 0;
    }
    case STK_FILTER: {
      JaclVal v;
      while (jacl_stream_next(s->source, &v)) { if (((JaclPredFn)s->fn)(v)) { *out = v; return 1; } }
      return 0;
    }
    case STK_TAKE: {
      if (s->n <= 0) return 0;
      JaclVal v;
      if (jacl_stream_next(s->source, &v)) { s->n--; *out = v; return 1; }
      s->n = 0;
      return 0;
    }
  }
  return 0;
}

JaclVal jacl_stream_collect(JaclVal sv) {
  JaclVal vec = jacl_vec_empty();
  JaclVal v;
  while (jacl_stream_next(sv, &v)) vec = jacl_vec_push(vec, v);
  return vec;
}
