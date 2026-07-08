/* P3.4d — continuation-pool `parallel` GC-soundness under concurrent allocation.
 * The program body runs as the root job; jacl_parallel spreads NT allocating blocks across the
 * pinned worker pool. Each block holds a stamped keeper on its job fiber while it allocates
 * garbage and forces collections, then returns arg*arg. A collection run by any worker must
 * keep every block's keeper live (its roots are on a suspended job fiber) and let no mutator
 * run during the stop (violation count stays 0); results are checked against arg*arg.
 * Returns 555. Run under --release (svm-jit gc.roots trips a debug-only UB check, benign here). */
#include "jaclrt.h"

#define NT       8
#define SI_ITERS 24

static long pg_block(long self) {
  JaclVal c = (JaclVal)self;
  long arg = (long)(uint64_t)jacl_closure_upval(c, (JaclVal)0);
  JaclObj *keeper = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
  long stamp = arg * 1000 + 7;
  *(long *)jacl_obj_payload(keeper) = stamp;
  for (long i = 0; i < SI_ITERS; i++) {
    JaclObj *g = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
    if (g) *(long *)jacl_obj_payload(g) = i;
    if ((i % 4) == 0) jacl_gc_collect();
    if (*(long *)jacl_obj_payload(keeper) != stamp) return -1;   /* keeper must survive */
  }
  return arg * arg;
}

static long pg_root(long self) {
  (void)self;
  JaclVal blocks = jacl_vec_empty();
  for (long i = 0; i < NT; i++) {
    JaclVal cl = jacl_closure_new((JaclVal)(uintptr_t)pg_block, (JaclVal)1);
    cl = jacl_closure_set(cl, (JaclVal)0, (JaclVal)(uint64_t)i);
    blocks = jacl_vec_push(blocks, cl);
  }
  JaclVal results = jacl_parallel(blocks);
  long ok = 1;
  for (long i = 0; i < NT; i++)
    if ((long)jacl_vec_get(results, (uint32_t)i) != i * i) ok = 0;
  return ok;
}

int run(int n) {
  (void)n;
  jacl_heap_init();
  long ok = (long)jacl_sched_run_main((JaclVal)(uintptr_t)pg_root);
  if (ok && jacl_gc_violation_count() == 0) return 555;
  return 1000000 + (int)jacl_gc_violation_count() * 10000 + (int)(ok ? 0 : 1);
}

#include "jaclrt.c"
