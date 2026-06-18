/* P1.5 — persistent map (HAMT), non-GC: i32 + string keys, get/has/persistence/
 * overwrite/remove, content-equality lookup. Returns 555. */
#include "jaclrt.h"

int run(int n) {
  (void)n;
  jacl_heap_init();
  jacl_intern_init();
  jacl_map_init();
  int ok = 1;

  JaclVal m = jacl_map_empty();
  ok &= (jacl_map_count(m) == 0);
  ok &= !jacl_map_has(m, jaclrt_i32(1));
  ok &= jaclrt_is_nil(jacl_map_get(m, jaclrt_i32(1)));

  /* i32 keys */
  JaclVal m1 = jacl_map_set(m, jaclrt_i32(1), jaclrt_i32(100));
  m1 = jacl_map_set(m1, jaclrt_i32(2), jaclrt_i32(200));
  m1 = jacl_map_set(m1, jaclrt_i32(3), jaclrt_i32(300));
  ok &= (jacl_map_count(m1) == 3);
  ok &= jacl_map_has(m1, jaclrt_i32(2));
  ok &= (jaclrt_as_i32(jacl_map_get(m1, jaclrt_i32(2))) == 200);
  ok &= jaclrt_is_nil(jacl_map_get(m1, jaclrt_i32(9)));
  ok &= (jacl_map_count(m) == 0);                  /* persistence: original empty */

  /* overwrite is persistent */
  JaclVal m2 = jacl_map_set(m1, jaclrt_i32(2), jaclrt_i32(222));
  ok &= (jacl_map_count(m2) == 3) && (jaclrt_as_i32(jacl_map_get(m2, jaclrt_i32(2))) == 222);
  ok &= (jaclrt_as_i32(jacl_map_get(m1, jaclrt_i32(2))) == 200);

  /* string keys — looked up by CONTENT (fresh equal key strings) */
  JaclVal sm = jacl_map_empty();
  sm = jacl_map_set(sm, jacl_str_new("alpha-key-123", 13), jaclrt_i32(11));
  sm = jacl_map_set(sm, jacl_str_new("beta-key-456", 12), jaclrt_i32(22));
  ok &= (jacl_map_count(sm) == 2);
  ok &= (jaclrt_as_i32(jacl_map_get(sm, jacl_str_new("alpha-key-123", 13))) == 11);
  ok &= (jaclrt_as_i32(jacl_map_get(sm, jacl_str_new("beta-key-456", 12))) == 22);

  /* remove is persistent */
  JaclVal key_a = jacl_str_new("alpha-key-123", 13);
  JaclVal sm2 = jacl_map_remove(sm, key_a);
  ok &= (jacl_map_count(sm2) == 1) && !jacl_map_has(sm2, key_a);
  ok &= jacl_map_has(sm, key_a);                   /* sm unchanged */

  return ok ? 555 : -1;
}

#include "jaclrt.c"
