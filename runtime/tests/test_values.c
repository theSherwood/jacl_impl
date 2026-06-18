/* P1.1 — value representation test. `run` returns 4242 iff every check passes.
 * Compiled with the runtime via svm-llvm; run on interp + JIT by the harness. */
#include "jaclrt.h"

int run(int n) {
  (void)n;
  int ok = 1;

  /* i32 round-trip + sign + not-heap */
  JaclVal a = jaclrt_i32(42);
  ok &= jaclrt_is_i32(a);
  ok &= (jaclrt_as_i32(a) == 42);
  ok &= !jaclrt_is_heap(a);
  ok &= (jaclrt_as_i32(jaclrt_i32(-7)) == -7);
  ok &= (jaclrt_as_i32(jaclrt_i32((int32_t)0x80000000)) == (int32_t)0x80000000);

  /* bool */
  JaclVal t = jaclrt_bool(true), f = jaclrt_bool(false);
  ok &= jaclrt_is_bool(t) && jaclrt_is_bool(f);
  ok &= (jaclrt_as_bool(t) == 1) && (jaclrt_as_bool(f) == 0);
  ok &= !jaclrt_is_i32(t) && !jaclrt_is_heap(t);

  /* nil */
  JaclVal nl = jaclrt_nil();
  ok &= jaclrt_is_nil(nl);
  ok &= !jaclrt_is_i32(nl) && !jaclrt_is_bool(nl) && !jaclrt_is_heap(nl);

  /* tag classification: scalar tags are non-heap; pointer tags are heap */
  ok &= !jaclrt_is_heap((JaclVal)JACL_TAG_F32);
  ok &= jaclrt_is_heap((JaclVal)JACL_TAG_STRING);
  ok &= jaclrt_is_heap((JaclVal)JACL_TAG_VECTOR);
  ok &= jaclrt_is_heap((JaclVal)JACL_TAG_MAP);
  ok &= jaclrt_is_heap((JaclVal)JACL_TAG_I64);   /* boxed scalars are heap too */

  /* error flag rides above the type and doesn't change the type query */
  JaclVal err_i = a | JACL_FLAG_ERROR;
  ok &= jaclrt_is_error(err_i) && jaclrt_is_i32(err_i) && (jaclrt_as_i32(err_i) == 42);
  ok &= !jaclrt_is_error(a);

  return ok ? 4242 : -1;
}
