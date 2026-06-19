/* sched.c — P3.4d reusable worker pool (batch scheduler).
 *
 * Runs a batch of independent task fibers across N vCPU workers (real OS threads via
 * thread.spawn), GC-safe through the P3.4c quiesce primitives: each worker registers in
 * the quiesce set, parks at its scheduler-loop top while a collection runs, and brackets
 * every task resume with task_begin/end so a task that hits a GC safepoint suspends to
 * the worker (its roots scannable) and is re-resumed after the collection.
 *
 * This is the substrate `parallel`/`race`/`await` move onto: submit a batch of 0-arg
 * task functions, run them to completion across the pool, collect their results. Workers
 * claim tasks with a single atomic counter (a fixed batch — dynamic queues + work-
 * stealing + nested await/park are the next P3.4d steps). The main thread orchestrates
 * (spawns + joins) and is not a mutator during the batch.
 */
#include "jaclrt.h"

int  __vm_thread_spawn(long (*fn)(long), void *stack, long arg);
long __vm_thread_join(int h);
long __vm_fiber_new(long (*f)(long), void *stack);
long __vm_fiber_resume(long k, long arg, int *done);
long __vm_atomic_add(void *p, long v);
long __vm_vcpu_tls_get(void);

#define JACL_SCHED_MAX_WORKERS 16
#define JACL_SCHED_STACK       (1u << 16)

/* the current batch (set up by jacl_sched_run_batch before the workers start) */
static JaclTaskFn *jacl_batch_fn;
static long       *jacl_batch_arg;
static long       *jacl_batch_result;
static long        jacl_batch_n;
static long        jacl_batch_next;   /* atomic: next task index to claim */
static char        jacl_worker_stack[JACL_SCHED_MAX_WORKERS][JACL_SCHED_STACK] __attribute__((aligned(16)));

/* A worker vCPU: claim tasks until the batch is drained, running each as a task fiber to
 * completion. The scheduler-loop safepoint (park_if_requested) quiesces this root-free
 * vCPU for GC; task_begin/end bracket each resume so the running task's roots live on a
 * fiber the collector can scan, and a GC-suspended task is simply re-resumed. */
static long jacl_sched_worker(long arg) {
  (void)arg;
  int w = (int)__vm_vcpu_tls_get();
  if (w < 0 || w >= JACL_SCHED_MAX_WORKERS) return -1;
  jacl_gc_worker_register();
  for (;;) {
    jacl_gc_worker_park_if_requested();
    long i = __vm_atomic_add(&jacl_batch_next, 1);
    if (i >= jacl_batch_n) break;
    long task = __vm_fiber_new(jacl_batch_fn[i], jacl_worker_stack[w]);
    long prime = jacl_batch_arg[i];
    int done = 0;
    long v = 0;
    do {
      jacl_gc_worker_park_if_requested();   /* park before (re)resuming if a collection runs */
      jacl_gc_task_begin();
      v = __vm_fiber_resume(task, prime, &done);
      jacl_gc_task_end();
      prime = 0;
    } while (!done);                         /* a GC-suspended task returns !done; re-resume it */
    jacl_batch_result[i] = v;
  }
  jacl_gc_worker_unregister();
  return 0;
}

/* Run `n` tasks (`fn[i](arg[i]) -> result[i]`) across `nworkers` vCPU workers, returning
 * once every task has completed. GC-safe: a collection triggered by any task quiesces the
 * whole pool. The caller (main) leaves the quiesce set while it blocks in join, then
 * rejoins — it cannot answer a stop while parked in thread_join. */
void jacl_sched_run_batch(JaclTaskFn *fn, long *arg, long *result, long n, int nworkers) {
  if (nworkers < 1) nworkers = 1;
  if (nworkers > JACL_SCHED_MAX_WORKERS) nworkers = JACL_SCHED_MAX_WORKERS;
  jacl_batch_fn = fn;
  jacl_batch_arg = arg;
  jacl_batch_result = result;
  jacl_batch_n = n;
  jacl_batch_next = 0;

  jacl_gc_worker_unregister();   /* main only orchestrates; not a mutator while joining */
  int h[JACL_SCHED_MAX_WORKERS];
  for (int k = 0; k < nworkers; k++) h[k] = __vm_thread_spawn(jacl_sched_worker, (void *)0, 0);
  for (int k = 0; k < nworkers; k++) __vm_thread_join(h[k]);
  jacl_gc_worker_register();     /* main rejoins the set for any later single-thread GC */
}
