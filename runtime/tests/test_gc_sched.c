/* P3.4c — sound multi-vCPU stop-the-world GC over a fiber scheduler.
 *
 * Each worker vCPU runs a scheduler loop that resumes ONE task fiber. The task holds a
 * stamped "keeper" object live on its FIBER stack while it allocates garbage in a loop
 * and periodically triggers a collection. A collection driven by one worker quiesces the
 * others: each other worker's task hits a safepoint (in jacl_alloc) and SUSPENDS back to
 * its scheduler, which parks the (root-free) vCPU. The collector's gc.roots then scans
 * every suspended task fiber + its own (caller) frames (svm GC.md §3.1), so each worker's
 * keeper survives a collection run by a DIFFERENT worker — the property that bare-vCPU
 * futex-parking could not give. After joining, every keeper is intact and the STW
 * mutual-exclusion violation count is 0. Returns 999.
 *
 * `run` is module function 0; the scheduler loop, the task, and the runtime follow. */
#include "jaclrt.h"

int  __vm_thread_spawn(long (*fn)(long), void *stack, long arg);
long __vm_thread_join(int h);
long __vm_fiber_new(long (*f)(long), void *stack);
long __vm_fiber_resume(long k, long arg, int *done);
long __vm_vcpu_tls_get(void);

#define GC_WORKERS 4
#define GC_ITERS   200
#define GC_MAXW    8
#define GC_STRIDE  1000000L
#define GC_STACK   (1u << 16)

static char    gc_stack[GC_MAXW][GC_STACK] __attribute__((aligned(16)));
static int32_t gc_result[GC_MAXW];   /* the task's return: 1 = keeper survived every collection */

long gc_worker(long arg);   /* the thread.spawn entry: a scheduler loop over one task fiber */
long gc_task(long arg);     /* the task fiber: holds a keeper, allocates, triggers collections */

int run(int n) {
  (void)n;
  jacl_heap_init();
  for (int w = 0; w < GC_MAXW; w++) gc_result[w] = 0;
  jacl_gc_worker_unregister();   /* root only orchestrates; it must not be in the quiesce set */

  int h[GC_WORKERS];
  for (int k = 0; k < GC_WORKERS; k++) h[k] = __vm_thread_spawn(gc_worker, (void *)0, 0);
  for (int k = 0; k < GC_WORKERS; k++) __vm_thread_join(h[k]);

  int ok = (jacl_gc_violation_count() == 0);
  int survived = 0;
  for (int w = 0; w < GC_MAXW; w++) if (gc_result[w]) survived++;
  ok = ok && (survived == GC_WORKERS);
  if (ok) return 999;
  return 1000000 + (int)jacl_gc_violation_count() * 10000 + survived * 100;
}

/* A worker vCPU: register, run a scheduler loop that drives one task fiber, unregister.
 * The loop top is a safepoint that parks this (root-free) vCPU while a collection runs;
 * a task that suspended for GC is re-resumed after the collection. */
long gc_worker(long arg) {
  (void)arg;
  int w = (int)__vm_vcpu_tls_get();
  if (w < 0 || w >= GC_MAXW) return -1;
  jacl_gc_worker_register();

  long task = __vm_fiber_new(gc_task, gc_stack[w]);
  long resume_arg = (long)w;
  int done = 0;
  for (;;) {
    jacl_gc_worker_park_if_requested();   /* quiesce here (no task running, no roots on this stack) */
    jacl_gc_task_begin();
    long v = __vm_fiber_resume(task, resume_arg, &done);
    jacl_gc_task_end();
    resume_arg = 0;
    if (done) { gc_result[w] = (int)v; break; }
    /* else: the task suspended at a GC safepoint — loop; park_if_requested will quiesce us */
  }
  jacl_gc_worker_unregister();
  return 0;
}

/* The task fiber: the keeper is a local held live for the whole loop, so it sits on this
 * fiber's control stack across every suspend — exactly what the collector's gc.roots must
 * find. Returns 1 iff the keeper stayed intact through every collection. */
long gc_task(long arg) {
  int w = (int)arg;
  JaclObj *keeper = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
  long stamp = (long)w * GC_STRIDE + 7;
  *(long *)jacl_obj_payload(keeper) = stamp;

  for (long i = 0; i < GC_ITERS; i++) {
    JaclObj *g = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));  /* garbage (dropped) */
    if (g) *(long *)jacl_obj_payload(g) = i;
    if ((i % 12) == 0) jacl_gc_collect();              /* trigger STW collections during the run */
    if (*(long *)jacl_obj_payload(keeper) != stamp) return 0;   /* keeper must survive */
  }
  return 1;
}

#include "jaclrt.c"   /* runtime impl last, so run() is module function 0 */
