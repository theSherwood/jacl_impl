/* P1.5 — map/GC interaction: a map of heap-string keys -> heap-string values, held
 * across a collection, survives with every key/value intact (both reachable only
 * via the map), while n unreachable heap strings are reclaimed. Returns 565. */
#include "jaclrt.h"

static JaclVal kv(char tag, int i);   /* "m<tag>-NN-xyzw" (len 10, heap) */
static JaclVal build_map(int N);
static void    make_garbage(long n);

int run(int n) {
  jacl_heap_init();
  jacl_intern_init();
  jacl_map_init();
  int N = 12;

  JaclVal m = build_map(N);   /* keys/values reachable only via the map; held across GC */
  make_garbage(n);
  jacl_gc_collect();

  int ok = (jacl_map_count(m) == (uint32_t)N);
  char buf[16];
  for (int i = 0; i < N; i++) {
    JaclVal got = jacl_map_get(m, kv('k', i));     /* look up by a fresh, content-equal key */
    ok &= jaclrt_is_heap(got);                      /* value string survived collection */
    jacl_str_bytes(got, buf, sizeof buf);
    ok &= (buf[0] == 'm' && buf[1] == 'v'
        && buf[3] == (char)('0' + i / 10) && buf[4] == (char)('0' + i % 10));
  }
  return ok ? 565 : -1;
}

static JaclVal kv(char tag, int i) {
  char b[16];
  b[0] = 'm'; b[1] = tag; b[2] = '-';
  b[3] = (char)('0' + i / 10); b[4] = (char)('0' + i % 10);
  b[5] = '-'; b[6] = 'x'; b[7] = 'y'; b[8] = 'z'; b[9] = 'w'; b[10] = '\0';
  return jacl_str_new(b, 10);                        /* len 10 -> heap string */
}
static JaclVal build_map(int N) {
  JaclVal m = jacl_map_empty();
  for (int i = 0; i < N; i++) m = jacl_map_set(m, kv('k', i), kv('v', i));
  return m;
}
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
