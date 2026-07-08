/* sched.c — P3.4d persistent worker pool for parallel/race + demand-driven spawn/await.
 *
 * `parallel`/`race` run their blocks on a PERSISTENT pool of vCPU workers (real OS threads via
 * thread.spawn), started once on first use and reused for the whole program. Blocks are
 * enqueued on a shared MPMC queue; idle workers pull and run them in parallel; the caller
 * helping-awaits the results. This replaces the prior transient model (fresh threads spawned
 * per construct): svm vCPU ids are dense and never reused, so a program with many
 * parallel/race sites would otherwise grow ids past the per-vCPU allocator/quiesce arrays.
 * One pool, started once, fixes that and gives real cross-construct parallelism.
 *
 * `spawn`/`await` stay demand-driven and single-threaded: `spawn` records a future, `await`
 * runs it on demand (nesting a sub-fiber for a nested await). With program-as-a-job the
 * program body runs as the root task fiber on the main thread, so these run on main and their
 * roots are always scannable. (Full multi-threaded spawn/await needs a continuation scheduler
 * — fibers that suspend back to the bare loop and migrate between workers — because a NESTED
 * fiber chain on a *spawned* vCPU cannot quiesce svm's stop-the-world gc.roots scan cleanly;
 * that is the next keystone slice. Here the invariant is simpler: fiber NESTING happens only
 * on the main thread (worker 0); spawned workers run flat, depth-0 tasks only.)
 *
 * GC soundness (on the P3.4c quiesce protocol): workers are registered in the quiesce set for
 * their whole life; an idle worker waits on the pool event with a 1 ms futex timeout, so it
 * re-checks the GC park request within the collector's own 1 ms barrier window. Running a task
 * is bracketed by task_begin/end; a GC safepoint suspends the running fiber, and `run_child`
 * unwinds a nested chain to the parking worker before the vCPU parks.
 *
 * Limitations (not exercised by the corpus, documented): a parallel/race block that itself
 * calls parallel/race from a spawned worker can block-wait without a free worker (nested
 * parallel), and a demand-driven `await` that runs heavy-allocation work from inside a pool
 * block (i.e. off the main thread) hits the spawned-vCPU nesting hazard above.
 */
#include "jaclrt.h"

int  __vm_thread_spawn(long (*fn)(long), void *stack, long arg);
long __vm_thread_join(int h);
long __vm_fiber_new(long (*f)(long), void *stack);
long __vm_fiber_resume(long k, long arg, int *done);
long __vm_fiber_suspend(long value);
long __vm_atomic_add(void *p, long v);
int  __vm_atomic_add32(void *p, int v);
int  __vm_atomic_load32(void *p);
void __vm_atomic_store32(void *p, int v);
int  __vm_atomic_cas32(void *p, int expected, int desired);
int  __vm_wait32(void *p, int expected, long timeout_ns);
int  __vm_notify(void *p, int count);
long __vm_vcpu_tls_get(void);

#define JACL_SCHED_MAX_WORKERS 16
#define JACL_SCHED_STACK       (1u << 16)
#ifndef JACL_POOL_WORKERS
#define JACL_POOL_WORKERS      4            /* persistent pool size (main = worker 0) */
#endif
#define JACL_WAIT_NS           1000000L     /* 1 ms futex timeout: every wait re-checks GC */

static int jacl_sched_self(void) {
  int w = (int)__vm_vcpu_tls_get();
  return (w < 0 || w >= JACL_SCHED_MAX_WORKERS) ? 0 : w;
}

/* legacy transient-batch globals (defined at the bottom; forward-declared so the GC root
 * scan below can reach them). */
#define JACL_BATCH_MAX 64
extern long jacl_batch_arg[JACL_BATCH_MAX];
extern long jacl_batch_result[JACL_BATCH_MAX];
extern long jacl_batch_n;

/* ---- fiber data-stack pool (grab/release; nesting depth = await/parallel depth) ---- */
#define JACL_TASK_STACKS 64
#define JACL_TASK_STACK_BYTES (1u << 16)
static char  jacl_task_stack[JACL_TASK_STACKS][JACL_TASK_STACK_BYTES] __attribute__((aligned(16)));
static int   jacl_task_stack_free[JACL_TASK_STACKS];
static int   jacl_task_stack_nfree = -1;   /* -1 = uninitialized */
static int32_t jacl_task_stack_lock;

static void *jacl_grab_task_stack(void) {
  while (__vm_atomic_cas32(&jacl_task_stack_lock, 0, 1) != 0)
    __vm_wait32(&jacl_task_stack_lock, 1, JACL_WAIT_NS);
  if (jacl_task_stack_nfree < 0) {           /* lazy init: all free */
    for (int i = 0; i < JACL_TASK_STACKS; i++) jacl_task_stack_free[i] = i;
    jacl_task_stack_nfree = JACL_TASK_STACKS;
  }
  void *r = 0;
  if (jacl_task_stack_nfree > 0) r = jacl_task_stack[jacl_task_stack_free[--jacl_task_stack_nfree]];
  __vm_atomic_store32(&jacl_task_stack_lock, 0);
  __vm_notify(&jacl_task_stack_lock, 1);
  return r;
}
static void jacl_release_task_stack(void *stk) {
  long idx = ((char *)stk - (char *)jacl_task_stack) / JACL_TASK_STACK_BYTES;
  while (__vm_atomic_cas32(&jacl_task_stack_lock, 0, 1) != 0)
    __vm_wait32(&jacl_task_stack_lock, 1, JACL_WAIT_NS);
  if (idx >= 0 && idx < JACL_TASK_STACKS) jacl_task_stack_free[jacl_task_stack_nfree++] = (int)idx;
  __vm_atomic_store32(&jacl_task_stack_lock, 0);
  __vm_notify(&jacl_task_stack_lock, 1);
}

/* ---- the GC-cooperative fiber runner ----
 *
 * Run `fnptr(prime)` to completion on a fresh task fiber and return its result. A task fiber
 * suspends (returns !done) only at a GC safepoint while another worker is collecting. On such
 * a suspend we hand the vCPU to the collector with the *entire* nested chain suspended:
 *   - bare context (`on_fiber == 0`: the worker loop / run_main top): park the vCPU, then
 *     re-resume the child after the collection.
 *   - on-fiber context (`on_fiber == 1`: we are ourselves running on a task fiber — main
 *     helping or a demand-driven nested await): re-suspend ourselves so the suspend propagates
 *     up to our resumer; the chain collapses to a bare loop that parks. On resume we re-resume
 *     the child, which continues past its safepoint.
 * (`on_fiber == 1` runs only on the main thread, per the nesting invariant.) The child's
 * fiber handle and `stk` live on the caller's (preserved) fiber stack, so they survive across
 * our own suspend/resume. */
static long run_child(long fnptr, long prime, int on_fiber) {
  void *stk = jacl_grab_task_stack();
  if (!stk) return (long)jaclrt_error();
  long k = __vm_fiber_new((long (*)(long))(uintptr_t)fnptr, stk);
  int done = 0;
  long v = 0;
  for (;;) {
    if (!on_fiber) jacl_gc_worker_park_if_requested();  /* park BEFORE (re)resuming, like the
                                                           proven batch worker — never resume a
                                                           task into an in-progress collection */
    jacl_gc_task_begin();
    v = __vm_fiber_resume(k, prime, &done);
    jacl_gc_task_end();
    prime = 0;
    if (done) break;
    if (on_fiber) __vm_fiber_suspend(0);                /* propagate the stop up our chain */
  }
  jacl_release_task_stack(stk);
  return v;
}

/* ---- futures + the shared task queue ----
 *
 * future payload (JOBJ_NODE, traced): int32 [0] state; int32 [1] owner vCPU (-1 = none);
 * i64 [2..3] closure; i64 [4..5] result. The state word is the futex an awaiter waits on. */
enum { JACL_FUT_NEW = 0, JACL_FUT_RUNNING = 1, JACL_FUT_DONE = 2 };

#define JACL_QCAP 4096
static JaclObj  *jacl_q_buf[JACL_QCAP];
static int32_t   jacl_q_head;       /* index of the next task to pop */
static int32_t   jacl_q_count;      /* number queued */
static int32_t   jacl_q_lock;       /* futex mutex over head/count/buf */
static int32_t   jacl_pool_event;   /* bumped+notified on enqueue/shutdown (idle wakeup) */
static int32_t   jacl_pool_shutdown;
static int32_t   jacl_pool_started; /* 0 until the first parallel/race lazily starts the pool */
static int       jacl_pool_handles[JACL_POOL_WORKERS];
static int       jacl_pool_nthreads;

static void q_lock(void)   { while (__vm_atomic_cas32(&jacl_q_lock, 0, 1) != 0) __vm_wait32(&jacl_q_lock, 1, JACL_WAIT_NS); }
static void q_unlock(void) { __vm_atomic_store32(&jacl_q_lock, 0); __vm_notify(&jacl_q_lock, 1); }

/* Enqueue a NEW future; returns 0 if the queue is full (caller spins until a slot frees). */
static int q_push(JaclObj *f) {
  q_lock();
  if (jacl_q_count >= JACL_QCAP) { q_unlock(); return 0; }
  jacl_q_buf[(jacl_q_head + jacl_q_count) % JACL_QCAP] = f;
  jacl_q_count++;
  q_unlock();
  __vm_atomic_add32(&jacl_pool_event, 1);
  __vm_notify(&jacl_pool_event, JACL_POOL_WORKERS);
  return 1;
}

/* Pop the next queued future and claim it (NEW -> RUNNING, owner = self). The pop is under
 * the lock, so each future is handed to exactly one worker. Returns NULL if empty. */
static JaclObj *q_pop_claim(int self) {
  q_lock();
  if (jacl_q_count == 0) { q_unlock(); return 0; }
  JaclObj *f = jacl_q_buf[jacl_q_head];
  jacl_q_head = (jacl_q_head + 1) % JACL_QCAP;
  jacl_q_count--;
  q_unlock();
  int32_t *p = (int32_t *)jacl_obj_payload(f);
  __vm_atomic_store32(&p[1], self);                 /* owner first … */
  __vm_atomic_store32(&p[0], JACL_FUT_RUNNING);     /* … then publish RUNNING */
  return f;
}

/* Publish a future's i64 result then mark it DONE, with proper release ordering so an awaiter
 * that observes DONE also observes the result. The result is written as two atomic 32-bit
 * halves *before* the DONE store: a plain i64 store is not reliably ordered before the atomic
 * DONE store under svm-llvm + the JIT's real threads (the awaiter could see DONE with a stale
 * 0 result). All three stores are SeqCst, so the total order guarantees visibility. */
static void fut_publish(int32_t *p, long v) {
  __vm_atomic_store32(&p[4], (int)(uint32_t)(uint64_t)v);
  __vm_atomic_store32(&p[5], (int)(uint32_t)((uint64_t)v >> 32));
  __vm_atomic_store32(&p[0], JACL_FUT_DONE);
  __vm_notify(&p[0], JACL_POOL_WORKERS);
}
/* Read a DONE future's i64 result via the matching atomic halves (acquire side). */
static long fut_result(int32_t *p) {
  uint64_t lo = (uint32_t)__vm_atomic_load32(&p[4]);
  uint64_t hi = (uint32_t)__vm_atomic_load32(&p[5]);
  return (long)(lo | (hi << 32));
}

/* Run a claimed (RUNNING) future to completion, store + publish its result, wake awaiters.
 * `on_fiber` describes the caller's context (see run_child). */
static void run_task(JaclObj *t, int on_fiber) {
  int32_t *p = (int32_t *)jacl_obj_payload(t);
  JaclVal closure = (JaclVal) * (int64_t *)(p + 2);
  long fnptr = (long)(uintptr_t)(uint64_t)jacl_closure_fn(closure);
  long v = run_child(fnptr, (long)closure, on_fiber);   /* closure passed as `self` */
  fut_publish(p, v);
}

/* ---- worker loop + pool lifecycle ---- */

/* Pop a task to run, or NULL once the pool is shutting down and drained. Idle waits use a
 * 1 ms timeout so the GC park request is re-checked promptly; drains the queue before
 * honoring shutdown, so spawned-but-unawaited blocks still complete. */
static JaclObj *pool_get_task(void) {
  int self = jacl_sched_self();
  for (;;) {
    jacl_gc_worker_park_if_requested();
    JaclObj *t = q_pop_claim(self);
    if (t) return t;
    if (__vm_atomic_load32(&jacl_pool_shutdown)) return 0;
    int ev = __vm_atomic_load32(&jacl_pool_event);
    __vm_wait32(&jacl_pool_event, ev, JACL_WAIT_NS);
  }
}

static long jacl_pool_worker(long arg) {
  (void)arg;
  jacl_gc_worker_register();
  for (;;) {
    JaclObj *t = pool_get_task();
    if (!t) break;
    run_task(t, /*on_fiber=*/0);   /* spawned workers run flat, depth-0 tasks only */
  }
  jacl_gc_worker_unregister();
  return 0;
}

/* Start the pool lazily on first parallel/race use (called from the main root fiber). Programs
 * that never use parallel/race never spawn worker threads. */
static void pool_ensure(void) {
  if (__vm_atomic_cas32(&jacl_pool_started, 0, 1) != 0) return;
  jacl_q_head = 0; jacl_q_count = 0; jacl_q_lock = 0;
  jacl_pool_event = 0; jacl_pool_shutdown = 0;
  int nw = JACL_POOL_WORKERS;
  if (nw > JACL_SCHED_MAX_WORKERS) nw = JACL_SCHED_MAX_WORKERS;
  jacl_pool_nthreads = nw - 1;   /* main is worker 0 */
  for (int k = 0; k < jacl_pool_nthreads; k++)
    jacl_pool_handles[k] = __vm_thread_spawn(jacl_pool_worker, (void *)0, 0);
}

static void pool_shutdown(void) {
  if (!__vm_atomic_load32(&jacl_pool_started)) return;
  __vm_atomic_store32(&jacl_pool_shutdown, 1);
  __vm_atomic_add32(&jacl_pool_event, 1);
  __vm_notify(&jacl_pool_event, JACL_POOL_WORKERS);
  jacl_gc_worker_unregister();   /* main leaves the set while it blocks in join */
  for (int k = 0; k < jacl_pool_nthreads; k++) __vm_thread_join(jacl_pool_handles[k]);
  jacl_gc_worker_register();
}

/* ---- demand-driven spawn / await (single-threaded, on the main root fiber) ---- */

/* Live futures registry — rooted during a collection (a spawn future may be reachable only
 * via a program local while a demand-run task collects; program-as-a-job already keeps those
 * on the scannable root fiber, so this is belt-and-suspenders). Bounded; overflow is safe. */
#define JACL_FUT_MAX 256
static JaclObj *jacl_futures[JACL_FUT_MAX];
static long     jacl_nfutures;

/* Root the in-flight scheduler state during a collection: live spawn futures + the legacy
 * batch buffers. parallel/race futures need no entry here — they sit in a local array on the
 * calling (root) fiber's stack, which gc.roots scans directly. */
/* In-flight parallel/race state — GLOBAL buffers (not main's fiber stack), so a collection
 * driven by a pool worker roots them via jacl_sched_mark_roots. This mirrors the proven
 * jacl_batch_* buffers: gc.roots does NOT reliably cover the main thread's stack/registers for
 * a collection elected on another vCPU, so jacl_parallel must not hold its roots there. The
 * input closures vector is rooted through the caller's pointer while the call is in flight. */
#define JACL_PAR_MAX 32
static JaclObj *jacl_par_fut[JACL_PAR_MAX];     /* the per-block future objects (traced) */
static JaclVal  jacl_par_result[JACL_PAR_MAX];  /* collected results (may be heap) */
static long     jacl_par_n;
static JaclVal *jacl_par_closures;              /* the input closures vector, in flight */

void jacl_sched_mark_roots(void) {
  for (long i = 0; i < jacl_nfutures; i++)
    jacl_gc_mark(jaclrt_from_ptr(JACL_TAG_STREAM, jacl_futures[i]));
  for (long i = 0; i < jacl_batch_n; i++) {
    jacl_gc_mark_word(jacl_batch_arg[i]);
    jacl_gc_mark_word(jacl_batch_result[i]);
  }
  for (long i = 0; i < jacl_par_n; i++) {
    if (jacl_par_fut[i]) jacl_gc_mark(jaclrt_from_ptr(JACL_TAG_STREAM, jacl_par_fut[i]));
    jacl_gc_mark(jacl_par_result[i]);
  }
  if (jacl_par_closures) jacl_gc_mark(*jacl_par_closures);
}

/* Run the program body `fnref` as the ROOT TASK fiber on the main thread (program-as-a-job),
 * so its roots — and every demand-run task's — live on a scannable fiber. The persistent pool
 * (for parallel/race) is started lazily; shut down here once the body returns. */
JaclVal jacl_sched_run_main(JaclVal fnref) {
  long v = run_child((long)(uintptr_t)(uint64_t)fnref, 0, /*on_fiber=*/0);
  pool_shutdown();
  return (JaclVal)v;
}

JaclVal jacl_spawn(JaclVal closure) {
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, 24);
  int32_t *p = (int32_t *)jacl_obj_payload(o);
  p[0] = JACL_FUT_NEW;
  p[1] = -1;
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

  /* NEW: run the task on demand on a fresh fiber (a nested await inside re-enters here for the
   * inner future). run_child makes the run GC-cooperative; on the main root fiber its nesting
   * is sound. */
  fp[0] = JACL_FUT_RUNNING;
  JaclVal closure = (JaclVal) * (int64_t *)(fp + 2);
  long fnptr = (long)(uintptr_t)(uint64_t)jacl_closure_fn(closure);
  long v = run_child(fnptr, (long)closure, /*on_fiber=*/1);
  *(int64_t *)(fp + 4) = (int64_t)v;
  fp[0] = JACL_FUT_DONE;
  return (JaclVal)v;
}

/* ---- parallel / race on the persistent pool ----
 *
 * Each block is a 0-param closure. The implementation mirrors the proven run_batch discipline
 * so that gc.roots never has to scan the main thread's stack for a collection a pool worker
 * elects (it doesn't cover that reliably):
 *   1. allocate the per-block futures into the GLOBAL jacl_par_fut[] buffer (rooted by
 *      jacl_sched_mark_roots) — done while no worker is running a block, so no concurrent
 *      collection races the allocation;
 *   2. enqueue + wait, collecting each result into the GLOBAL jacl_par_result[] buffer — the
 *      caller does NO heap allocation in this concurrent phase, so it holds no main-stack root;
 *   3. once every block is done (workers idle), build the result vector — now the main thread
 *      is the sole mutator, so a collection it triggers scans its own (collector) frames.
 * `parallel` returns the vector of results; `race` returns block 0's (all still run to
 * completion — cancellation is a follow-on). */

/* Allocate a NEW future over `closure` into global slot `i` (rooted from publication). */
static JaclObj *par_alloc_future(long i, JaclVal closure) {
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, 24);
  int32_t *p = (int32_t *)jacl_obj_payload(o);
  p[0] = JACL_FUT_NEW;
  p[1] = -1;
  *(int64_t *)(p + 2) = (int64_t)closure;   /* the block closure (traced) */
  *(int64_t *)(p + 4) = 0;
  jacl_par_fut[i] = o;
  if (i + 1 > jacl_par_n) jacl_par_n = i + 1;   /* publish: now rooted by mark_roots */
  return o;
}

/* Wait for a pool future to resolve, GC-cooperatively. The caller allocates nothing here. With
 * spawned workers (the normal case) the blocks are flat + independent, so workers always drain
 * them and the caller only waits — no fiber nesting on any worker. A size-1 pool (no spawned
 * workers) has the caller run the blocks itself on its own root fiber (sound: main-only
 * nesting). A future RUNNING on *this* worker is a self-cycle (nil). */
static JaclVal par_await(JaclObj *fo) {
  int32_t *p = (int32_t *)jacl_obj_payload(fo);
  int self = jacl_sched_self();
  int run_self = (jacl_pool_nthreads == 0);
  for (;;) {
    jacl_gc_safepoint();
    int st = __vm_atomic_load32(&p[0]);
    if (st == JACL_FUT_DONE) return (JaclVal)fut_result(p);
    if (st == JACL_FUT_RUNNING && __vm_atomic_load32(&p[1]) == self) return JACL_NIL;
    if (run_self && self == 0) {
      JaclObj *t = q_pop_claim(self);
      if (t) { run_task(t, /*on_fiber=*/1); continue; }
    }
    int s = __vm_atomic_load32(&p[0]);
    if (s == JACL_FUT_DONE) return (JaclVal)fut_result(p);
    __vm_wait32(&p[0], s, JACL_WAIT_NS);
  }
}

/* Phase 1+2: build futures (global, no concurrent collection yet), start the pool, enqueue,
 * and collect every result into the global buffer. On return all blocks are done and workers
 * are idle, so the caller can allocate freely. */
static long par_run(JaclVal closures, long n) {
  if (n > JACL_PAR_MAX) n = JACL_PAR_MAX;
  jacl_par_closures = &closures;
  jacl_par_n = 0;
  for (long i = 0; i < n; i++) { jacl_par_result[i] = JACL_NIL; jacl_par_fut[i] = 0; }
  for (long i = 0; i < n; i++) par_alloc_future(i, jacl_vec_get(closures, (uint32_t)i));
  pool_ensure();                                   /* idempotent; first call starts the workers */
  for (long i = 0; i < n; i++)
    while (!q_push(jacl_par_fut[i])) { jacl_gc_safepoint(); __vm_wait32(&jacl_pool_event, __vm_atomic_load32(&jacl_pool_event), JACL_WAIT_NS); }
  for (long i = 0; i < n; i++) jacl_par_result[i] = par_await(jacl_par_fut[i]);
  return n;
}

JaclVal jacl_parallel(JaclVal closures) {
  long n = par_run(closures, (long)jacl_vec_count(closures));
  JaclVal out = jacl_vec_empty();                  /* workers idle now: main is sole mutator */
  for (long i = 0; i < n; i++) out = jacl_vec_push(out, jacl_par_result[i]);
  jacl_par_n = 0; jacl_par_closures = 0;
  return out;
}

JaclVal jacl_race(JaclVal closures) {
  long total = (long)jacl_vec_count(closures);
  if (total <= 0) return JACL_NIL;
  par_run(closures, total);                         /* runs all to completion (drains the pool) */
  JaclVal first = jacl_par_result[0];
  jacl_par_n = 0; jacl_par_closures = 0;
  return first;
}

/* ---- legacy transient batch scheduler (test-only) ----
 *
 * The original P3.4d substrate: spawn `nworkers` fresh vCPU threads, run a fixed batch of
 * tasks across them via a single atomic claim counter, join. Superseded for parallel/race by
 * the persistent pool above; retained because the multi-vCPU GC tests (test_sched_mt /
 * test_batch_heap / test_gc_sched) exercise this path directly and it is a proven minimal
 * harness for the quiesce protocol. Not used by the language surface. */
JaclTaskFn jacl_batch_fn[JACL_BATCH_MAX];
long       jacl_batch_arg[JACL_BATCH_MAX];
long       jacl_batch_result[JACL_BATCH_MAX];
long       jacl_batch_n;
static long jacl_batch_next;
static char jacl_batch_worker_stack[JACL_SCHED_MAX_WORKERS][JACL_SCHED_STACK] __attribute__((aligned(16)));

static long jacl_batch_worker(long arg) {
  (void)arg;
  int w = jacl_sched_self();
  if (w < 0 || w >= JACL_SCHED_MAX_WORKERS) return -1;
  jacl_gc_worker_register();
  for (;;) {
    jacl_gc_worker_park_if_requested();
    long i = __vm_atomic_add(&jacl_batch_next, 1);
    if (i >= jacl_batch_n) break;
    long task = __vm_fiber_new(jacl_batch_fn[i], jacl_batch_worker_stack[w]);
    long prime = jacl_batch_arg[i];
    int done = 0;
    long v = 0;
    do {
      jacl_gc_worker_park_if_requested();
      jacl_gc_task_begin();
      v = __vm_fiber_resume(task, prime, &done);
      jacl_gc_task_end();
      prime = 0;
    } while (!done);
    jacl_batch_result[i] = v;
  }
  jacl_gc_worker_unregister();
  return 0;
}

void jacl_sched_run_batch(JaclTaskFn *fn, long *arg, long *result, long n, int nworkers) {
  if (nworkers < 1) nworkers = 1;
  if (nworkers > JACL_SCHED_MAX_WORKERS) nworkers = JACL_SCHED_MAX_WORKERS;
  if (n > JACL_BATCH_MAX) n = JACL_BATCH_MAX;
  for (long i = 0; i < n; i++) { jacl_batch_fn[i] = fn[i]; jacl_batch_arg[i] = arg[i]; jacl_batch_result[i] = 0; }
  jacl_batch_next = 0;
  jacl_batch_n = n;            /* publish last: mark_roots scans [0, jacl_batch_n) */

  jacl_gc_worker_unregister();
  int h[JACL_SCHED_MAX_WORKERS];
  for (int k = 0; k < nworkers; k++) h[k] = __vm_thread_spawn(jacl_batch_worker, (void *)0, 0);
  for (int k = 0; k < nworkers; k++) __vm_thread_join(h[k]);
  jacl_gc_worker_register();

  for (long i = 0; i < n; i++) result[i] = jacl_batch_result[i];
  jacl_batch_n = 0;
}
