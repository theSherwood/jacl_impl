/* Functional check: jacl_parallel on the continuation scheduler, return-value checked (no
 * powerbox), differentially run interp==jit. Three blocks return i*i; verifies the pool runs
 * them across pinned workers and the awaited results come back correct. Returns 111. */
#include "jaclrt.h"

static long pm_blk(long self) {
  JaclVal c = (JaclVal)self;
  long a = (long)(uint64_t)jacl_closure_upval(c, (JaclVal)0);
  return a * a;
}

static long pm_root(long self) {
  (void)self;
  JaclVal v = jacl_vec_empty();
  for (long i = 0; i < 3; i++) {
    JaclVal c = jacl_closure_new((JaclVal)(uintptr_t)pm_blk, (JaclVal)1);
    c = jacl_closure_set(c, (JaclVal)0, (JaclVal)(uint64_t)i);
    v = jacl_vec_push(v, c);
  }
  JaclVal r = jacl_parallel(v);
  long ok = ((long)jacl_vec_get(r, 0) == 0) && ((long)jacl_vec_get(r, 1) == 1) && ((long)jacl_vec_get(r, 2) == 4);
  return ok ? 111 : 222;
}

int run(int n) {
  (void)n;
  jacl_heap_init();
  return (int)(long)jacl_sched_run_main((JaclVal)(uintptr_t)pm_root);
}

#include "jaclrt.c"
