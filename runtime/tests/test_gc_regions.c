/* jacl #159 — a sweep hands empty regions back to the pool, so a heap full of dead cells of one
 * size has room for any other size.
 *
 * The free lists are exact-size. Before the fix a swept cell could only be reused by its own
 * size class and a large cell's regions were never reused at all, so:
 *   1. fill the heap with dead 16-byte boxes (the wide-int loop of the issue), then ask for a
 *      string: every box is swept onto the 16-byte list, no fresh region is left, and the
 *      collection the string's allocation triggers finds nothing new to free — out of memory,
 *      with the heap all but empty;
 *   2. allocate and drop 1 MiB blobs: each claimed 16 fresh regions and the 16th never fit.
 * Both must succeed now, with the survivors intact. `run` returns 159 iff they do. */
#include "jaclrt.h"

static void    fill_with_boxes(long n);
static JaclVal make_strings(int n);
static int     check_strings(JaclVal v, int n);
static int     churn_large(int rounds);

int run(int n) {
  (void)n;
  jacl_heap_init();
  jacl_intern_init();
  jacl_map_init();
  fill_with_boxes(1100000);                 /* > 16 MiB of 16-byte cells, all dead */
  JaclVal v = make_strings(2000);           /* another size class entirely */
  if (!check_strings(v, 2000)) return -1;
  if (!churn_large(40)) return -2;          /* 40 MiB of 1 MiB blobs through a 16 MiB heap */
  if (!check_strings(v, 2000)) return -3;   /* the survivors outlived both churns */
  return 159;
}

__attribute__((noinline)) static void fill_with_boxes(long n) {
  for (long i = 0; i < n; i++) {
    JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_BLOB, 8);
    *(long *)jacl_obj_payload(o) = i;
  }
}
__attribute__((noinline)) static JaclVal make_strings(int n) {
  JaclVal v = jacl_vec_empty();
  for (int i = 0; i < n; i++) {
    char b[40];
    for (int k = 0; k < 39; k++) b[k] = (char)('a' + ((i + k) % 26));
    b[39] = '\0';
    v = jacl_vec_push(v, jacl_str_new(b, 39));
  }
  return v;
}
__attribute__((noinline)) static int check_strings(JaclVal v, int n) {
  if ((int)jacl_vec_count(v) != n) return 0;
  for (int i = 0; i < n; i++) {
    char b[48];
    jacl_str_bytes(jacl_vec_get(v, (uint32_t)i), b, sizeof b);
    for (int k = 0; k < 39; k++) if (b[k] != (char)('a' + ((i + k) % 26))) return 0;
  }
  return 1;
}
__attribute__((noinline)) static int churn_large(int rounds) {
  for (int r = 0; r < rounds; r++) {
    JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_BLOB, 1u << 20);
    long *p = (long *)jacl_obj_payload(o);
    p[0] = r; p[(1 << 17) - 1] = r;
    if (p[0] != r || p[(1 << 17) - 1] != r) return 0;
  }
  return 1;
}

#include "jaclrt.c"
