/* P3.4d — `parallel` on the persistent worker pool, GC-soundness under concurrent allocation.
 *
 * The program body runs as the root task fiber (jacl_sched_run_main). It builds NT 0-arg
 * blocks and runs them with one `jacl_parallel` call: the persistent pool spreads them across
 * vCPU workers. Each block holds a stamped keeper on its (flat) fiber stack while it allocates
 * garbage and forces collections, then returns arg*arg. A collection run by any worker must
 * keep every block's keeper live (its roots are on a suspended fiber) and must not let a
 * mutator run during the stop (violation count stays 0). The result vector is checked against
 * arg*arg. Returns 555 on success.
 *
 * NOTE: like the other multi-vCPU GC tests, run under `--release` — svm-jit's `gc.roots`
 * stack scan trips Rust's debug-only `read_unaligned` UB precondition check (a pre-existing
 * svm-side debug assertion, benign in release).
 *
 * `run` is module function 0; the block, root, and runtime follow. */
#include "jaclrt.h"

#define NT       6
#define SI_ITERS 24

/* a parallel block: hold a stamped keeper across allocations + forced collections, return
 * arg*arg. `self` is the closure; upval 0 is the integer arg. */
static long par_block(long self) {
  JaclVal c = (JaclVal)self;
  long arg = (long)(uint64_t)jacl_closure_upval(c, (JaclVal)0);
  JaclObj *keeper = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
  long stamp = arg * 1000 + 7;
  *(long *)jacl_obj_payload(keeper) = stamp;
  for (long i = 0; i < SI_ITERS; i++) {
    JaclObj *g = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));   /* garbage */
    if (g) *(long *)jacl_obj_payload(g) = i;
    if ((i % 7) == 0) jacl_gc_collect();
    if (*(long *)jacl_obj_payload(keeper) != stamp) return -1;   /* keeper must survive */
  }
  return arg * arg;
}

static long par_root(long self) {
  (void)self;
  JaclVal blocks = jacl_vec_empty();
  for (long i = 0; i < NT; i++) {
    JaclVal c = jacl_closure_new((JaclVal)(uintptr_t)par_block, (JaclVal)1);
    c = jacl_closure_set(c, (JaclVal)0, (JaclVal)(uint64_t)i);
    blocks = jacl_vec_push(blocks, c);
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
  long ok = (long)jacl_sched_run_main((JaclVal)(uintptr_t)par_root);
  if (ok && jacl_gc_violation_count() == 0) return 555;
  return 1000000 + (int)jacl_gc_violation_count() * 10000 + (int)(ok ? 0 : 1);
}

#include "jaclrt.c"   /* runtime impl last, so run() is module function 0 */
