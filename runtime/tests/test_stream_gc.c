/* P1.6 — stream/GC interaction: a stream pipeline (vec-of-heap-strings | map(len) |
 * take) held across a collection survives — the source vector and its string
 * elements are kept alive transitively through the pipeline — then collects to the
 * right result. Returns 616. */
#include "jaclrt.h"

static JaclVal slen(JaclVal v);            /* heap string -> i32 length */
static JaclVal build_str_vec(int N);
static void    make_garbage(long n);

int run(int n) {
  jacl_heap_init();
  jacl_intern_init();
  int N = 10;

  /* base vector of N heap strings (len 20), reachable only via the pipeline */
  JaclVal base = build_str_vec(N);
  /* pipeline held (not yet drained) across the collection */
  JaclVal pipe = jacl_stream_take(jacl_stream_map(jacl_stream_vec(base), slen), N);

  make_garbage(n);
  jacl_gc_collect();   /* pipe -> map -> vec-stream -> base -> strings must all survive */

  JaclVal lens = jacl_stream_collect(pipe);
  int ok = (jacl_vec_count(lens) == (uint32_t)N);
  for (int i = 0; i < N; i++) ok &= (jaclrt_as_i32(jacl_vec_get(lens, i)) == 20);
  return ok ? 616 : -1;
}

static JaclVal slen(JaclVal v) { return jaclrt_i32((int32_t)jacl_str_len(v)); }

static JaclVal build_str_vec(int N) {
  JaclVal v = jacl_vec_empty();
  for (int i = 0; i < N; i++) {
    char b[24];
    for (int k = 0; k < 20; k++) b[k] = (char)('a' + ((i + k) % 26));
    b[20] = '\0';
    v = jacl_vec_push(v, jacl_str_new(b, 20));   /* heap string */
  }
  return v;
}
static void make_garbage(long n) {
  for (long i = 0; i < n; i++) {
    char b[24];
    for (int k = 0; k < 20; k++) b[k] = (char)('A' + ((i + k) % 26));
    b[20] = '\0';
    JaclVal s = jacl_str_new(b, 20);
    (void)s;
  }
}

#include "jaclrt.c"
