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

/* ---- spawn / await: demand-driven futures (P3.4d, sub-slice 1: nested await) ----
 *
 * `spawn { block }` records the block closure as a future and returns it; nothing runs
 * yet. `await $f` runs `f`'s task **on demand** to completion — on a fresh fiber, with the
 * closure as `self` — and caches the result. If the task itself `await`s another future,
 * that nests: this `jacl_await` re-enters for the inner future, running its task on another
 * fiber while the outer task is suspended at the resume (exactly how a generator drives a
 * sub-fiber). So a task awaiting another task's future works — the case the prior
 * lazy-flush model raced on. A future resolves once and is awaitable repeatedly.
 *
 * This is single-threaded (correctness first); spawn/await lose the lazy-flush's
 * parallelism (the corpus is value-identical either way). Restoring multi-threaded
 * spawn/await on a persistent pool with work-stealing is the next keystone sub-slice;
 * `parallel`/`race` keep their real parallelism (run_batch) meanwhile.
 *
 * future payload (JOBJ_NODE, traced): int32 [0] state (0 NEW / 1 RUNNING / 2 DONE);
 * i64 [2..3] closure; i64 [4..5] result. */
enum { JACL_FUT_NEW = 0, JACL_FUT_RUNNING = 1, JACL_FUT_DONE = 2 };

/* fiber data-stack pool for on-demand task runs (grab/release; nesting = await depth). */
#define JACL_TASK_STACKS 64
#define JACL_TASK_STACK_BYTES (1u << 16)
static char  jacl_task_stack[JACL_TASK_STACKS][JACL_TASK_STACK_BYTES] __attribute__((aligned(16)));
static int   jacl_task_stack_free[JACL_TASK_STACKS];
static int   jacl_task_stack_nfree = -1;   /* -1 = uninitialized */

static void *jacl_grab_task_stack(void) {
  if (jacl_task_stack_nfree < 0) {           /* lazy init: all free */
    for (int i = 0; i < JACL_TASK_STACKS; i++) jacl_task_stack_free[i] = i;
    jacl_task_stack_nfree = JACL_TASK_STACKS;
  }
  if (jacl_task_stack_nfree == 0) return 0;
  return jacl_task_stack[jacl_task_stack_free[--jacl_task_stack_nfree]];
}
static void jacl_release_task_stack(void *stk) {
  long idx = ((char *)stk - (char *)jacl_task_stack) / JACL_TASK_STACK_BYTES;
  if (idx >= 0 && idx < JACL_TASK_STACKS) jacl_task_stack_free[jacl_task_stack_nfree++] = (int)idx;
}

/* Live futures registry — rooted during a collection (a future may be reachable only via a
 * program local on an unscanned bare stack while a demand-run task collects). Bounded. */
#define JACL_FUT_MAX 256
static JaclObj *jacl_futures[JACL_FUT_MAX];
static long     jacl_nfutures;

/* Root the in-flight scheduler state during a collection: live spawn futures + the current
 * parallel/race batch's args (task closures) and completed results (the batch path keeps
 * these in globals while main is parked in thread_join with its stack unscanned). */
void jacl_sched_mark_roots(void) {
  for (long i = 0; i < jacl_nfutures; i++)
    jacl_gc_mark(jaclrt_from_ptr(JACL_TAG_STREAM, jacl_futures[i]));
  for (long i = 0; i < jacl_batch_n; i++) {
    jacl_gc_mark_word(jacl_batch_arg[i]);     /* task closures / raw arg ptrs (non-heap ignored) */
    jacl_gc_mark_word(jacl_batch_result[i]);  /* completed results, tagged or raw (i32 ignored) */
  }
}

JaclVal jacl_spawn(JaclVal closure) {
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, 24);
  int32_t *p = (int32_t *)jacl_obj_payload(o);
  p[0] = JACL_FUT_NEW;
  *(int64_t *)(p + 2) = (int64_t)closure;    /* the block closure (traced) */
  *(int64_t *)(p + 4) = 0;
  if (jacl_nfutures < JACL_FUT_MAX) jacl_futures[jacl_nfutures++] = o;
  return jaclrt_from_ptr(JACL_TAG_STREAM, o);
}

JaclVal jacl_await(JaclVal future) {
  JaclObj *fo = (JaclObj *)jaclrt_as_ptr(future);
  int32_t *fp = (int32_t *)jacl_obj_payload(fo);
  if (fp[0] == JACL_FUT_DONE) return (JaclVal) * (int64_t *)(fp + 4);  /* cached */
  if (fp[0] == JACL_FUT_RUNNING) return JACL_NIL;                      /* cycle guard */

  /* NEW: run the task on demand. A nested await inside the task re-enters here for the
   * inner future on another fiber (the outer task suspends at this resume). */
  fp[0] = JACL_FUT_RUNNING;
  JaclVal closure = (JaclVal) * (int64_t *)(fp + 2);
  JaclVal fnref = jacl_closure_fn(closure);
  void *stk = jacl_grab_task_stack();
  if (!stk) { fp[0] = JACL_FUT_NEW; return jaclrt_error(); }
  long k = __vm_fiber_new((long (*)(long))(uintptr_t)(uint64_t)fnref, stk);
  long prime = (long)closure;   /* primed as `self` on the first resume */
  int done = 0;
  long v = 0;
  do { v = __vm_fiber_resume(k, prime, &done); prime = 0; } while (!done);
  jacl_release_task_stack(stk);

  /* re-read the payload pointer (a collection during the run may have run the conservative
   * sweep, but the non-moving GC keeps `fo` valid) and cache the result. */
  *(int64_t *)(fp + 4) = (int64_t)v;
  fp[0] = JACL_FUT_DONE;
  return v;
}
