/* P1.6 — stackless stream pipelines (non-GC). Returns 606. */
#include "jaclrt.h"

static JaclVal dbl(JaclVal v);    /* x -> x*2 */
static int     gt10(JaclVal v);   /* x > 10 ? */

int run(int n) {
  (void)n;
  jacl_heap_init();
  jacl_intern_init();
  int ok = 1;

  /* range(0..9) | map(*2) | filter(>10) | take(3) | collect
   *   *2     -> 0,2,4,6,8,10,12,14,16,18
   *   >10    -> 12,14,16,18
   *   take 3 -> 12,14,16 */
  JaclVal s = jacl_stream_range(10);
  s = jacl_stream_map(s, dbl);
  s = jacl_stream_filter(s, gt10);
  s = jacl_stream_take(s, 3);
  JaclVal v = jacl_stream_collect(s);
  ok &= (jacl_vec_count(v) == 3);
  ok &= (jaclrt_as_i32(jacl_vec_get(v, 0)) == 12);
  ok &= (jaclrt_as_i32(jacl_vec_get(v, 1)) == 14);
  ok &= (jaclrt_as_i32(jacl_vec_get(v, 2)) == 16);

  /* vector source: [5,15,25] | filter(>10) | collect -> [15,25] */
  JaclVal base = jacl_vec_empty();
  base = jacl_vec_push(base, jaclrt_i32(5));
  base = jacl_vec_push(base, jaclrt_i32(15));
  base = jacl_vec_push(base, jaclrt_i32(25));
  JaclVal v2 = jacl_stream_collect(jacl_stream_filter(jacl_stream_vec(base), gt10));
  ok &= (jacl_vec_count(v2) == 2)
     && (jaclrt_as_i32(jacl_vec_get(v2, 0)) == 15)
     && (jaclrt_as_i32(jacl_vec_get(v2, 1)) == 25);

  /* take 0 -> empty */
  JaclVal v3 = jacl_stream_collect(jacl_stream_take(jacl_stream_range(100), 0));
  ok &= (jacl_vec_count(v3) == 0);

  return ok ? 606 : -1;
}

static JaclVal dbl(JaclVal v)  { return jaclrt_i32(jaclrt_as_i32(v) * 2); }
static int     gt10(JaclVal v) { return jaclrt_as_i32(v) > 10; }

#include "jaclrt.c"
