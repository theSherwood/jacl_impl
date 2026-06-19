/* P3.4d — reusable worker-pool batch scheduler (sched.c).
 *
 * Submit a batch of independent tasks; the pool runs them across N vCPU workers, GC-safe.
 * Each task holds a stamped keeper on its FIBER stack while it allocates and triggers
 * collections, then returns arg*arg. After the batch, every result matches and every
 * keeper stayed intact through the collections (a corrupt keeper returns -1, failing its
 * result check) and the STW mutual-exclusion violation count is 0. Returns 888.
 *
 * `run` is module function 0; the task and the runtime follow. */
#include "jaclrt.h"

#define NT       8
#define NW       4
#define SI_ITERS 80

static long sched_task(long arg) {
  JaclObj *keeper = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
  long stamp = arg * 1000 + 7;
  *(long *)jacl_obj_payload(keeper) = stamp;
  for (long i = 0; i < SI_ITERS; i++) {
    JaclObj *g = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));   /* garbage */
    if (g) *(long *)jacl_obj_payload(g) = i;
    if ((i % 20) == 0) jacl_gc_collect();
    if (*(long *)jacl_obj_payload(keeper) != stamp) return -1;   /* keeper must survive */
  }
  return arg * arg;
}

int run(int n) {
  (void)n;
  jacl_heap_init();

  JaclTaskFn fns[NT];
  long args[NT], results[NT];
  for (int i = 0; i < NT; i++) { fns[i] = sched_task; args[i] = i; results[i] = 0; }

  jacl_sched_run_batch(fns, args, results, NT, NW);

  int ok = (jacl_gc_violation_count() == 0);
  for (int i = 0; i < NT; i++) ok = ok && (results[i] == (long)i * i);
  if (ok) return 888;
  return 1000000 + (int)jacl_gc_violation_count() * 10000;
}

#include "jaclrt.c"   /* runtime impl last, so run() is module function 0 */
