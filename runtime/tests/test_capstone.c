/* P1 DoD capstone — exercises the whole runtime substrate in one program:
 *   allocate heap objects; build a persistent vector + map; intern strings; run a
 *   stackless stream pipeline; call value-op builtins; and trigger a GC that
 *   reclaims garbage while keeping the entire reachable graph intact (vector, map
 *   keys+values, interned strings) — verified after the collection. Returns 1234.
 *
 * Run on interp + JIT by the harness. This is Phase 1's exit criterion. */
#include "jaclrt.h"

static inline int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static JaclVal word(int i);          /* "wd-NN-xyzw" (len 10, heap) */
static JaclVal slen(JaclVal s);      /* string -> i32 length */
static void    make_garbage(long n);

int run(int n) {
  jacl_heap_init();
  jacl_intern_init();
  jacl_map_init();
  int ok = 1;
  char buf[32];
  const int N = 8;

  /* vector of heap strings + map (heap string key -> i32 index) */
  JaclVal words = jacl_vec_empty();
  JaclVal index = jacl_map_empty();
  for (int i = 0; i < N; i++) {
    words = jacl_vec_push(words, word(i));
    index = jacl_map_set(index, word(i), jaclrt_i32(i));
  }

  /* interning */
  JaclVal a = jacl_str_intern("interned-token-A", 16);
  ok &= (a == jacl_str_intern("interned-token-A", 16));

  /* spawn garbage, then collect with words/index/a all live across the call */
  make_garbage(n);
  jacl_gc_collect();

  /* everything reachable survived the collection, intact */
  ok &= (jacl_vec_count(words) == (uint32_t)N);
  ok &= (jacl_map_count(index) == (uint32_t)N);
  for (int i = 0; i < N; i++) {
    JaclVal e = jacl_vec_get(words, i);                 /* element string survived */
    ok &= jaclrt_is_heap(e) && (jacl_str_len(e) == 10);
    ok &= (jaclrt_as_i32(jacl_map_get(index, word(i))) == i);  /* content-key lookup post-GC */
  }
  ok &= (a == jacl_str_intern("interned-token-A", 16)); /* interned string still canonical */

  /* stackless stream pipeline over the (post-GC) vector: map(len) | collect */
  JaclVal lens = jacl_stream_collect(jacl_stream_map(jacl_stream_vec(words), slen));
  ok &= (jacl_vec_count(lens) == (uint32_t)N);

  /* builtins: sum the lengths, render a label */
  JaclVal sum = jaclrt_i32(0);
  for (int i = 0; i < N; i++) sum = jacl_add(sum, jacl_vec_get(lens, i));
  ok &= (jaclrt_as_i32(sum) == N * 10);                 /* 8 words * 10 = 80 */
  JaclVal label = jacl_str_concat(jacl_str_new("sum=", 4), jacl_to_string(sum));
  jacl_str_bytes(label, buf, sizeof buf);
  ok &= seq(buf, "sum=80");

  /* typeof across the value kinds */
  jacl_str_bytes(jacl_typeof(words), buf, sizeof buf); ok &= seq(buf, "vec");
  jacl_str_bytes(jacl_typeof(index), buf, sizeof buf); ok &= seq(buf, "map");
  jacl_str_bytes(jacl_typeof(a), buf, sizeof buf);     ok &= seq(buf, "string");
  ok &= (jaclrt_as_i32(jacl_len(words)) == N);

  return ok ? 1234 : -1;
}

static JaclVal word(int i) {
  char b[16];
  b[0] = 'w'; b[1] = 'd'; b[2] = '-';
  b[3] = (char)('0' + i / 10); b[4] = (char)('0' + i % 10);
  b[5] = '-'; b[6] = 'x'; b[7] = 'y'; b[8] = 'z'; b[9] = 'w'; b[10] = '\0';
  return jacl_str_new(b, 10);
}
static JaclVal slen(JaclVal s) { return jaclrt_i32((int32_t)jacl_str_len(s)); }
static void make_garbage(long n) {
  for (long i = 0; i < n; i++) {
    char b[16];
    for (int k = 0; k < 12; k++) b[k] = (char)('A' + ((i + k) % 26));
    b[12] = '\0';
    JaclVal s = jacl_str_new(b, 12);
    (void)s;
  }
}

#include "jaclrt.c"
