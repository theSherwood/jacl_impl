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
#define JACL_BATCH_MAX         64

/* the current batch — in guest-global buffers (not caller-stack arrays) so the collector
 * can root the in-flight args (task closures) and completed results during a mid-batch
 * collection: the main thread is parked in thread_join with its stack unscanned, a task's
 * arg is only on its own fiber once it starts, and a heap result is unrooted between the
 * task returning and the caller consuming it. jacl_sched_mark_roots scans these. */
static JaclTaskFn jacl_batch_fn[JACL_BATCH_MAX];
static long       jacl_batch_arg[JACL_BATCH_MAX];
static long       jacl_batch_result[JACL_BATCH_MAX];
static long       jacl_batch_n;
static long       jacl_batch_next;   /* atomic: next task index to claim */
static char       jacl_worker_stack[JACL_SCHED_MAX_WORKERS][JACL_SCHED_STACK] __attribute__((aligned(16)));

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
 * whole pool, and the in-flight args + completed results are rooted via the global batch
 * buffers (jacl_sched_mark_roots). The caller (main) leaves the quiesce set while it blocks
 * in join, then rejoins — it cannot answer a stop while parked in thread_join. */
void jacl_sched_run_batch(JaclTaskFn *fn, long *arg, long *result, long n, int nworkers) {
  if (nworkers < 1) nworkers = 1;
  if (nworkers > JACL_SCHED_MAX_WORKERS) nworkers = JACL_SCHED_MAX_WORKERS;
  if (n > JACL_BATCH_MAX) n = JACL_BATCH_MAX;
  for (long i = 0; i < n; i++) { jacl_batch_fn[i] = fn[i]; jacl_batch_arg[i] = arg[i]; jacl_batch_result[i] = 0; }
  jacl_batch_next = 0;
  jacl_batch_n = n;            /* publish last: mark_roots scans [0, jacl_batch_n) */

  jacl_gc_worker_unregister();   /* main only orchestrates; not a mutator while joining */
  int h[JACL_SCHED_MAX_WORKERS];
  for (int k = 0; k < nworkers; k++) h[k] = __vm_thread_spawn(jacl_sched_worker, (void *)0, 0);
  for (int k = 0; k < nworkers; k++) __vm_thread_join(h[k]);
  jacl_gc_worker_register();     /* main rejoins the set for any later single-thread GC */

  for (long i = 0; i < n; i++) result[i] = jacl_batch_result[i];
  jacl_batch_n = 0;            /* batch over: stop rooting its buffers */
}

/* ---- parallel / race on the real worker pool (P3.4d) ----
 *
 * A block is a 0-param closure (capturing its free vars); its task runs the closure's
 * function on a task fiber with the closure passed as `self` (the resume arg), exactly as
 * the cooperative jacl_spawn does — only now across real vCPU workers. The closures (batch
 * args) and any heap-valued results survive a mid-batch collection: run_batch keeps them in
 * the global batch buffers, which jacl_sched_mark_roots scans during STW.
 *
 * NB: each batch spawns fresh vCPU workers and joins them, and svm vCPU ids are not
 * reused, so a program with very many parallel/race constructs would grow ids past the
 * per-vCPU arrays. The corpus uses a bounded few; a persistent reused worker pool (which
 * also carries spawn/await + nested-await parking) is the next P3.4d step. */
#define JACL_PAR_MAX 32

static int jacl_par_workers(long n) {
  long nw = n < 4 ? n : 4;        /* don't spawn more workers than tasks; cap at 4 */
  if (nw < 1) nw = 1;
  return (int)nw;
}

JaclVal jacl_parallel(JaclVal closures) {
  long n = (long)jacl_vec_count(closures);
  if (n > JACL_PAR_MAX) n = JACL_PAR_MAX;
  JaclTaskFn fn[JACL_PAR_MAX];
  long arg[JACL_PAR_MAX], res[JACL_PAR_MAX];
  for (long i = 0; i < n; i++) {
    JaclVal c = jacl_vec_get(closures, (uint32_t)i);
    fn[i] = (JaclTaskFn)(uintptr_t)(uint64_t)jacl_closure_fn(c);   /* the block's function */
    arg[i] = (long)c;                                             /* the closure, passed as self */
    res[i] = 0;
  }
  jacl_sched_run_batch(fn, arg, res, n, jacl_par_workers(n));
  JaclVal out = jacl_vec_empty();
  for (long i = 0; i < n; i++) out = jacl_vec_push(out, (JaclVal)res[i]);
  return out;
}

JaclVal jacl_race(JaclVal closures) {
  long n = (long)jacl_vec_count(closures);
  if (n <= 0) return JACL_NIL;
  if (n > JACL_PAR_MAX) n = JACL_PAR_MAX;
  JaclTaskFn fn[JACL_PAR_MAX];
  long arg[JACL_PAR_MAX], res[JACL_PAR_MAX];
  for (long i = 0; i < n; i++) {
    JaclVal c = jacl_vec_get(closures, (uint32_t)i);
    fn[i] = (JaclTaskFn)(uintptr_t)(uint64_t)jacl_closure_fn(c);
    arg[i] = (long)c;
    res[i] = 0;
  }
  jacl_sched_run_batch(fn, arg, res, n, jacl_par_workers(n));
  return (JaclVal)res[0];   /* block 0's result (deterministic; cancellation is a follow-on) */
}

/* ---- spawn / await: lazy futures flushed onto the worker pool (P3.4d) ----
 *
 * `spawn { block }` records the block closure as a PENDING task and returns a future;
 * it does not run yet. The first `await` FLUSHES all pending tasks onto the pool
 * (jacl_sched_run_batch — real parallelism), caches each result in its future, and
 * returns. For the pure-compute corpus this is value-equivalent to eager scheduling
 * (tasks are independent; only the degree of overlap differs, not the result). A true
 * persistent pool with per-future waiter queues + nested-await parking (a task awaiting
 * an unresolved future) is the follow-on.
 *
 * future payload (JOBJ_NODE, traced): int32 [0] done; i64 [2..3] closure; i64 [4..5] result. */
#define JACL_PENDING_MAX 64
static JaclObj *jacl_pending[JACL_PENDING_MAX];   /* futures awaiting their first flush */
static long     jacl_npending;

/* Root the in-flight scheduler state during a collection: the pending spawn futures, and
 * the current batch's args (task closures) + completed results. While a batch/flush runs,
 * the main thread is parked in thread_join (its stack unscanned) and these live in plain
 * globals, so without this a task closure or a heap result could be collected mid-batch.
 * The collector calls this during STW (see jacl_gc_collect_stw). */
void jacl_sched_mark_roots(void) {
  for (long i = 0; i < jacl_npending; i++)
    jacl_gc_mark(jaclrt_from_ptr(JACL_TAG_STREAM, jacl_pending[i]));
  for (long i = 0; i < jacl_batch_n; i++) {
    jacl_gc_mark_word(jacl_batch_arg[i]);     /* task closures / raw arg ptrs (non-heap ignored) */
    jacl_gc_mark_word(jacl_batch_result[i]);  /* completed results, tagged or raw (i32 ignored) */
  }
}

JaclVal jacl_spawn(JaclVal closure) {
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, 24);
  int32_t *p = (int32_t *)jacl_obj_payload(o);
  p[0] = 0;                                  /* pending */
  *(int64_t *)(p + 2) = (int64_t)closure;    /* the block closure (traced) */
  *(int64_t *)(p + 4) = 0;
  if (jacl_npending < JACL_PENDING_MAX) jacl_pending[jacl_npending++] = o;
  return jaclrt_from_ptr(JACL_TAG_STREAM, o);
}

JaclVal jacl_await(JaclVal future) {
  JaclObj *fo = (JaclObj *)jaclrt_as_ptr(future);
  int32_t *fp = (int32_t *)jacl_obj_payload(fo);
  if (fp[0]) return (JaclVal) * (int64_t *)(fp + 4);   /* already resolved */

  /* flush every currently-pending task onto the pool, then cache results */
  long n = jacl_npending;
  JaclTaskFn fn[JACL_PENDING_MAX];
  long arg[JACL_PENDING_MAX], res[JACL_PENDING_MAX];
  for (long i = 0; i < n; i++) {
    int32_t *pp = (int32_t *)jacl_obj_payload(jacl_pending[i]);
    JaclVal c = (JaclVal) * (int64_t *)(pp + 2);
    fn[i] = (JaclTaskFn)(uintptr_t)(uint64_t)jacl_closure_fn(c);
    arg[i] = (long)c;
    res[i] = 0;
  }
  /* keep jacl_pending intact across the batch so jacl_sched_mark_roots can root the
   * futures during any collection a task triggers; clear it only after. */
  jacl_sched_run_batch(fn, arg, res, n, jacl_par_workers(n));
  for (long i = 0; i < n; i++) {
    int32_t *pp = (int32_t *)jacl_obj_payload(jacl_pending[i]);
    pp[0] = 1;                               /* done */
    *(int64_t *)(pp + 4) = (int64_t)res[i];  /* cache the result (i32 in the corpus) */
  }
  jacl_npending = 0;
  return (JaclVal) * (int64_t *)(fp + 4);
}
