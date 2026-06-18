/* P1.5 — persistent vector (non-GC): build / count / get / persistence. Returns 333. */
#include "jaclrt.h"

int run(int n) {
  jacl_heap_init();
  jacl_intern_init();
  int ok = 1;
  int N = n > 0 ? n : 20;

  JaclVal empty = jacl_vec_empty();
  ok &= (jacl_vec_count(empty) == 0);

  JaclVal cur = empty;
  for (int i = 0; i < N; i++) cur = jacl_vec_push(cur, jaclrt_i32(i * 7));
  ok &= (jacl_vec_count(cur) == (uint32_t)N);
  for (int i = 0; i < N; i++) ok &= (jaclrt_as_i32(jacl_vec_get(cur, i)) == i * 7);
  ok &= jaclrt_is_nil(jacl_vec_get(cur, N));            /* out of range */

  /* persistence: pushing onto `empty` leaves `empty` and `cur` unchanged */
  JaclVal w = jacl_vec_push(empty, jaclrt_i32(999));
  ok &= (jacl_vec_count(empty) == 0)
     && (jacl_vec_count(w) == 1)
     && (jacl_vec_count(cur) == (uint32_t)N)
     && (jaclrt_as_i32(jacl_vec_get(w, 0)) == 999);

  return ok ? 333 : -1;
}

#include "jaclrt.c"
