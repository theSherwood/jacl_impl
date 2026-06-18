/* P1.5 — vector/GC interaction: a vector of heap-string elements held across a
 * collection survives with every element intact (the GC traces vector -> nodes ->
 * elements), while n unreachable heap strings are reclaimed. Returns 444.
 *
 * The string elements are reachable ONLY through the vector (never kept as locals),
 * so their survival proves vector tracing. */
#include "jaclrt.h"

static JaclVal build_vec_with_strings(int N);
static void     make_garbage(long n);

int run(int n) {
  jacl_heap_init();
  jacl_intern_init();
  int N = 12;

  JaclVal vec = build_vec_with_strings(N);   /* held across the collection */
  make_garbage(n);                           /* n unreachable heap strings */

  long before = jacl_live_count();
  jacl_gc_collect();
  long after = jacl_live_count();

  /* GC reclaims the n external garbage strings AND the intermediate persistent
   * vector versions (each push_back copies a path), so it must reclaim something;
   * the live vector + its elements must survive intact. (Exact counts are not
   * asserted — persistent versioning makes the reclaimed count implementation- and
   * n-dependent; conservative GC may also retain a little.) */
  int ok = (after < before) && (jacl_vec_count(vec) == (uint32_t)N);

  char buf[32];
  for (int i = 0; i < N; i++) {
    JaclVal e = jacl_vec_get(vec, i);        /* element reachable ONLY via the vector */
    ok &= jaclrt_is_heap(e);                 /* survived collection -> tracing works */
    jacl_str_bytes(e, buf, sizeof buf);
    ok &= (buf[0] == 'e' && buf[5] == (char)('0' + (i % 10)));
  }

  /* stability: a second collection (vec still held, no new garbage) reclaims
   * nothing and leaves the vector + elements intact. */
  long live2 = jacl_live_count();
  jacl_gc_collect();
  ok &= (jacl_live_count() == live2) && (jacl_vec_count(vec) == (uint32_t)N);
  for (int i = 0; i < N; i++) {
    JaclVal e = jacl_vec_get(vec, i);
    jacl_str_bytes(e, buf, sizeof buf);
    ok &= (buf[0] == 'e' && buf[5] == (char)('0' + (i % 10)));
  }
  return ok ? 444 : -1;
}

__attribute__((noinline)) static JaclVal build_vec_with_strings(int N) {
  JaclVal v = jacl_vec_empty();
  for (int i = 0; i < N; i++) {
    char b[24];
    b[0] = 'e'; b[1] = 'l'; b[2] = 'e'; b[3] = 'm'; b[4] = '-';
    b[5] = (char)('0' + (i % 10));
    for (int k = 6; k < 20; k++) b[k] = (char)('a' + ((i + k) % 26));
    b[20] = '\0';
    v = jacl_vec_push(v, jacl_str_new(b, 20));   /* heap string (len 20) */
  }
  return v;
}
__attribute__((noinline)) static void make_garbage(long n) {
  for (long i = 0; i < n; i++) {
    char b[24];
    for (int k = 0; k < 20; k++) b[k] = (char)('A' + ((i + k) % 26));
    b[20] = '\0';
    JaclVal s = jacl_str_new(b, 20);             /* heap, non-interned, dropped */
    (void)s;
  }
}

#include "jaclrt.c"
