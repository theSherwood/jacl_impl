/* sched.c — P3.4d persistent M:N scheduler (continuation style).
 *
 * One persistent pool of vCPU workers (real OS threads via thread.spawn) runs the whole
 * program. The program body itself is the root "job" (program-as-a-job); `spawn` creates more
 * jobs; `parallel`/`race` create a batch of jobs. Every job is a stackful fiber that runs to
 * completion across the workers. When a job must wait (`await` an unresolved job), it
 * **suspends back to the bare worker loop** — it does not busy-cycle and it is not nested on
 * another fiber — and is re-enqueued (and resumed by whatever worker grabs it — svm fiber
 * migration) once its target completes. So at any instant a job is either RUNNING on exactly
 * one worker or RUNNABLE-suspended (parked on a target / waiting in the ready queue); it is
 * **never a RUNNING fiber while it is logically blocked**.
 *
 * This is the keystone the earlier transient pool couldn't reach. Root-causing the transient
 * attempt (`spikes/svm_pool_gc/`) showed its corruption came from keeping the program on the
 * main thread as a *busy-cycling* fiber in the multi-worker quiesce set, with its roots on
 * main's bare stack. The continuation model dissolves all of that:
 *  - GC roots: all scheduler state lives in globals scanned by `jacl_sched_mark_roots` (the
 *    ready queue + each worker's current job); every program/task local lives on a job fiber
 *    that is always either RUNNING-and-will-suspend-at-its-next-safepoint or RUNNABLE-suspended
 *    (and svm `gc.roots` scans suspended fibers). No root ever lives only on a bare OS stack.
 *  - GC quiesce (P3.4c): a worker resuming a job brackets it with task_begin/end; a GC
 *    safepoint suspends the running fiber (value 0) and the worker re-resumes it after the
 *    collection. An `await` suspends with the target job pointer (non-zero), which the worker
 *    distinguishes from a GC suspend.
 *  - Result handoff: a job's completion (store result, mark DONE, wake waiters) runs under the
 *    one scheduler lock, so a waiter that observes DONE also observes the result.
 *
 * Relies only on svm primitives that already exist and are tested, including cross-vCPU fiber
 * migration (`svm/tests/fiber_migrate.rs`, `gc_roots.rs`).
 */
#include "jaclrt.h"
#include <string.h>

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

/* legacy transient-batch globals (defined at the bottom; forward-declared for the root scan). */
#define JACL_BATCH_MAX 64
extern long jacl_batch_arg[JACL_BATCH_MAX];
extern long jacl_batch_result[JACL_BATCH_MAX];
extern long jacl_batch_n;

/* ---- fiber data-stack pool ---- */
#define JACL_TASK_STACKS 64
#define JACL_TASK_STACK_BYTES (1u << 16)
static char  jacl_task_stack[JACL_TASK_STACKS][JACL_TASK_STACK_BYTES] __attribute__((aligned(16)));
static int   jacl_task_stack_free[JACL_TASK_STACKS];
static int   jacl_task_stack_nfree = -1;   /* -1 = uninitialized */

/* ---- the one scheduler lock (futex mutex) ----
 * Guards the ready queue, every job state transition, and the stack-pool free list. Held only
 * for short non-allocating critical sections — never across a fiber resume or a GC safepoint —
 * so it cannot deadlock a collection. Acquisition is GC-cooperative, respecting the quiesce
 * contract for each context: a FIBER caller suspends (jacl_gc_safepoint — the fiber switch
 * spills its registers and parks it in the scanned table), a BARE worker parks its vCPU.
 * Parking a vCPU while its fiber is RUNNING would make the collector's gc.roots refuse
 * (FiberFault: running fiber on another vCPU), so park_if_requested no-ops in-task. */
static int32_t jacl_sched_lock;
static void slock(void) {
  while (__vm_atomic_cas32(&jacl_sched_lock, 0, 1) != 0) {
    jacl_gc_safepoint();                 /* fiber context: suspend for the collection */
    jacl_gc_worker_park_if_requested();  /* bare context: park the vCPU (no-op in-task) */
    __vm_wait32(&jacl_sched_lock, 1, JACL_WAIT_NS);
  }
}
static void sunlock(void) { __vm_atomic_store32(&jacl_sched_lock, 0); __vm_notify(&jacl_sched_lock, 1); }

/* stack pool (called under slock) */
static void *grab_task_stack(void) {
  if (jacl_task_stack_nfree < 0) {
    for (int i = 0; i < JACL_TASK_STACKS; i++) jacl_task_stack_free[i] = i;
    jacl_task_stack_nfree = JACL_TASK_STACKS;
  }
  if (jacl_task_stack_nfree == 0) return 0;
  return jacl_task_stack[jacl_task_stack_free[--jacl_task_stack_nfree]];
}
static void release_task_stack(void *stk) {
  if (!stk) return;
  long idx = ((char *)stk - (char *)jacl_task_stack) / JACL_TASK_STACK_BYTES;
  if (idx >= 0 && idx < JACL_TASK_STACKS) {
    /* Zero the released stack: in-use stacks are conservatively scanned as GC roots
     * (mark_task_stacks), so stale frames from a finished job must not retain garbage
     * when the slot is reused. */
    memset(jacl_task_stack[idx], 0, JACL_TASK_STACK_BYTES);
    jacl_task_stack_free[jacl_task_stack_nfree++] = (int)idx;
  }
}

/* Conservatively scan every IN-USE fiber data stack for heap roots. Address-taken locals
 * (parallel's futs[], any array/struct local of a job) live on these guest-memory data
 * stacks — svm's gc.roots covers the NATIVE side (control stacks; registers via the #217
 * flush trampoline), and the vCPU-top contract makes guest-memory roots the guest's own
 * to report. This is that report: without it, a job referenced only by a parked fiber's
 * futs[] is swept mid-collection. Runs under STW (free list stable). */
static void mark_task_stacks(void) {
  char inuse[JACL_TASK_STACKS];
  for (int i = 0; i < JACL_TASK_STACKS; i++) inuse[i] = (char)(jacl_task_stack_nfree >= 0);
  for (int i = 0; i < jacl_task_stack_nfree; i++) inuse[jacl_task_stack_free[i]] = 0;
  for (int s = 0; s < JACL_TASK_STACKS; s++) {
    if (!inuse[s]) continue;
    long *w = (long *)jacl_task_stack[s];
    for (unsigned k = 0; k < JACL_TASK_STACK_BYTES / sizeof(long); k++)
      if (w[k]) jacl_gc_mark_word(w[k]);
  }
}

/* ---- jobs ----
 * A job is a JOBJ_NODE (traced). Payload (int32_t *p):
 *   p[0]  state (NEW/DONE)            p[1]  is_root
 *   i64@(p+2)  fn (fiber funcref)     i64@(p+4)  prime (closure for blocks; 0 for root)
 *   i64@(p+6)  fiber handle (0=unstarted)   i64@(p+8)  result
 *   i64@(p+10) resume_arg (await result, set when woken)
 *   i64@(p+12) next_waiter (job link) i64@(p+14) waiters_head (jobs parked on this)
 *   i64@(p+16) stack ptr
 * Conservative tracing keeps prime/closure, result, resume_arg, and the waiter chain alive;
 * fn/fiber/stack are out-of-heap small/addresses and are ignored. */
enum { JACL_JOB_NEW = 0, JACL_JOB_DONE = 2 };
#define JOB_PAYLOAD 88     /* p[18]=in_rq, p[19]=resuming, p[20]=owner worker */
static inline int32_t *jp(JaclObj *j)              { return (int32_t *)jacl_obj_payload(j); }
static inline long  job_get(JaclObj *j, int i)     { return *(int64_t *)(jp(j) + i); }
static inline void  job_set(JaclObj *j, int i, long v) { *(int64_t *)(jp(j) + i) = v; }

static JaclObj *make_job(long fn, long prime, int is_root, int owner) {
  JaclObj *j = (JaclObj *)jacl_alloc(JOBJ_NODE, JOB_PAYLOAD);
  int32_t *p = jp(j);
  p[0] = JACL_JOB_NEW; p[1] = is_root;
  job_set(j, 2, fn); job_set(j, 4, prime);
  job_set(j, 6, 0); job_set(j, 8, 0); job_set(j, 10, 0);
  job_set(j, 12, 0); job_set(j, 14, 0); job_set(j, 16, 0);
  p[18] = 0; p[19] = 0; p[20] = owner;
  return j;
}

/* ---- per-worker ready queues + pool state (all under slock unless noted) ----
 * Each job is PINNED to an owner worker (round-robin at creation) and runs only on that worker
 * — no fiber ever migrates between vCPUs, so svm's cross-vCPU claim is never contended. */
#define JACL_RQ_CAP 1024
static JaclObj *jacl_rq[JACL_SCHED_MAX_WORKERS][JACL_RQ_CAP];
static int32_t  jacl_rq_head[JACL_SCHED_MAX_WORKERS], jacl_rq_count[JACL_SCHED_MAX_WORKERS];
static JaclObj *jacl_running_job[JACL_SCHED_MAX_WORKERS];  /* each worker's in-flight job (rooted) */
static int32_t  jacl_pool_event;     /* bumped+notified on enqueue/shutdown (wakeups) */
static int32_t  jacl_pool_shutdown;
static int32_t  jacl_pool_started;
static int32_t  jacl_owner_rr;       /* round-robin owner assignment */
static int      jacl_pool_handles[JACL_POOL_WORKERS];
static int      jacl_pool_nthreads;
static JaclObj *jacl_root_job;

/* Jobs are ordinary GC objects — no global registry, no cap, no leak. Root coverage, per the
 * Ask 3 contract (docs/SVM_PHASE3_ASKS.md; svm >= vm#217):
 *   - WINDOW (jacl_sched_mark_roots): ready queues + jacl_running_job + the root job. A job
 *     parked on a target is reachable from the target's traced waiter chain.
 *   - NATIVE side (svm gc.roots): parked fibers' control stacks (the fiber switch spills all
 *     callee-saved registers) and, via #217's flush trampoline, the collector's own
 *     call-surviving register roots — so scalar locals (a `def`d future handle) are covered.
 *   - GUEST-MEMORY side (ours to report): address-taken locals (parallel's futs[]) live on the
 *     fiber DATA stacks (jacl_task_stack) — plain guest memory gc.roots never sees — so
 *     mark_task_stacks conservatively scans the in-use stacks each collection.
 * A completed, unreferenced job is simply collected (mt::job_gc proves the reclamation bound
 * and held-future re-await). The vCPU-top contract: a bare worker top holds no root that isn't
 * mirrored in the window before it can park (worker_loop mirrors its `job` into
 * jacl_running_job before any park point). */

static void rq_push(JaclObj *j) {            /* under slock — to the job's OWNER queue */
  int o = jp(j)[20];
  if (jp(j)[18]) return;                      /* already queued — never enqueue a job twice */
  if (jacl_rq_count[o] < JACL_RQ_CAP) {
    jacl_rq[o][(jacl_rq_head[o] + jacl_rq_count[o]) % JACL_RQ_CAP] = j;
    jacl_rq_count[o]++; jp(j)[18] = 1;
  }
}
static JaclObj *rq_pop(int self) {           /* under slock — from THIS worker's queue */
  if (jacl_rq_count[self] == 0) return 0;
  JaclObj *j = jacl_rq[self][jacl_rq_head[self]];
  jacl_rq_head[self] = (jacl_rq_head[self] + 1) % JACL_RQ_CAP; jacl_rq_count[self]--;
  jp(j)[18] = 0;
  return j;
}
static void pool_wake(void) { __vm_atomic_add32(&jacl_pool_event, 1); __vm_notify(&jacl_pool_event, JACL_POOL_WORKERS); }

/* Complete a job (under slock): publish result, mark DONE, move its waiters to the ready
 * queue (each gets the result as its resume arg). The lock orders the result write before any
 * waiter observes DONE. The root job's completion triggers shutdown. */
static void complete_job(JaclObj *j, long v) {
  job_set(j, 8, v);
  __vm_atomic_store32(&jp(j)[0], JACL_JOB_DONE);
  JaclObj *w = (JaclObj *)job_get(j, 14);
  job_set(j, 14, 0);
  while (w) {
    JaclObj *next = (JaclObj *)job_get(w, 12);
    job_set(w, 12, 0);
    job_set(w, 10, v);          /* resume_arg = result */
    rq_push(w);
    w = next;
  }
  if (jp(j)[1]) __vm_atomic_store32(&jacl_pool_shutdown, 1);   /* root done → shut the pool */
}

/* ---- the GC-cooperative resume ----
 * Resume job `j` once (continuing past GC safepoints). Returns 0 (RETURNED; *out=result),
 * 1 (AWAIT; *out=target-job-ptr), or 2 (SKIP — a concurrent claim is already running this
 * job's fiber; svm would FiberFault a second claimant, so we bail and let the owner finish).
 * p[19] is the single-claim guard: exactly one worker may resume a given job at a time. */
static int resume_job(JaclObj *j, long *out) {
  int self = jacl_sched_self();
  if (__vm_atomic_cas32(&jp(j)[19], 0, 1) != 0) return 2;   /* already being resumed elsewhere */
  long arg;
  if (job_get(j, 6) == 0) {                    /* not started: create fiber + stack */
    slock();
    void *stk = grab_task_stack();
    sunlock();
    job_set(j, 16, (long)stk);
    job_set(j, 6, __vm_fiber_new((long (*)(long))(uintptr_t)job_get(j, 2), stk));
    arg = job_get(j, 4);                        /* prime */
  } else {
    arg = job_get(j, 10);                       /* resume_arg (await result), or 0 */
  }
  int d = 0; long v = 0;
  for (;;) {
    jacl_gc_worker_park_if_requested();         /* never resume into an in-progress collection */
    jacl_running_job[self] = j;
    jacl_gc_task_begin();
    v = __vm_fiber_resume(job_get(j, 6), arg, &d);
    jacl_gc_task_end();
    arg = 0;
    if (d) { __vm_atomic_store32(&jp(j)[19], 0); *out = v; return 0; }
    if (v == 0) continue;                        /* GC safepoint suspend → re-resume after park */
    __vm_atomic_store32(&jp(j)[19], 0);          /* await: v = target job ptr */
    *out = v;
    return 1;
  }
}

/* ---- worker loop ---- */
static void worker_loop(int is_main) {
  int self = jacl_sched_self();
  for (;;) {
    jacl_gc_worker_park_if_requested();
    slock();
    JaclObj *job = rq_pop(self);
    sunlock();
    if (!job) {
      if (__vm_atomic_load32(&jacl_pool_shutdown)) break;
      jacl_running_job[self] = 0;                 /* idle: hold no root */
      int ev = __vm_atomic_load32(&jacl_pool_event);
      __vm_wait32(&jacl_pool_event, ev, JACL_WAIT_NS);
      continue;
    }
    jacl_running_job[self] = job;                 /* root it before resume (closes the pop→resume window) */
    long out;
    int oc = resume_job(job, &out);               /* 0=returned, 1=await, 2=skip(claim lost) */
    slock();
    if (oc == 2) {
      /* a concurrent claim already owns this job's fiber — nothing to do here */
    } else if (oc == 0) {
      release_task_stack((void *)job_get(job, 16));
      complete_job(job, out);
    } else {
      JaclObj *target = (JaclObj *)out;
      if (target == job || __vm_atomic_load32(&jp(target)[0]) == JACL_JOB_DONE) {
        /* self-cycle, or target already finished: re-run us with the result (nil for a cycle) */
        job_set(job, 10, (target == job) ? (long)JACL_NIL : job_get(target, 8));
        rq_push(job);
      } else {
        job_set(job, 12, job_get(target, 14));    /* park job on target's waiter list */
        job_set(target, 14, (long)job);
      }
    }
    sunlock();
    pool_wake();                                   /* newly-ready jobs / shutdown */
    if (is_main && __vm_atomic_load32(&jp(jacl_root_job)[0]) == JACL_JOB_DONE) break;
  }
}

static long worker_thread(long arg) { (void)arg; jacl_gc_worker_register(); worker_loop(0); jacl_gc_worker_unregister(); return 0; }

/* Initialize the scheduler state (queue, lock, flags). No worker threads yet — a purely
 * sequential program (no spawn/parallel/race) runs its root job on main alone and never spawns
 * a thread. */
static void sched_init(void) {
  jacl_sched_lock = 0; jacl_pool_event = 0; jacl_pool_shutdown = 0;
  jacl_pool_started = 0; jacl_pool_nthreads = 0; jacl_owner_rr = 0;
  for (int w = 0; w < JACL_SCHED_MAX_WORKERS; w++) { jacl_rq_head[w] = 0; jacl_rq_count[w] = 0; jacl_running_job[w] = 0; }
}

/* Round-robin owner for a newly spawned job (pinning). pool_ensure has set nthreads. */
static int next_owner(void) {
  int nw = jacl_pool_nthreads + 1;
  return (int)((unsigned)__vm_atomic_add32(&jacl_owner_rr, 1) % (unsigned)nw);
}

/* Lazily spawn the worker threads on the first concurrent construct. Idempotent. Does NOT touch
 * the queue (the root job is already enqueued by the time this runs). */
static void pool_ensure(void) {
  if (__vm_atomic_cas32(&jacl_pool_started, 0, 1) != 0) return;
  int nw = JACL_POOL_WORKERS;
  if (nw > JACL_SCHED_MAX_WORKERS) nw = JACL_SCHED_MAX_WORKERS;
  jacl_pool_nthreads = nw - 1;
  for (int k = 0; k < jacl_pool_nthreads; k++)
    jacl_pool_handles[k] = __vm_thread_spawn(worker_thread, (void *)0, 0);
}

/* Root the scheduler's WINDOW state during a collection: the per-worker ready queues, each
 * worker's in-flight job, and the root job. Every other live job is reachable from these — a
 * parked job via its target's traced waiter chain, a held future via the holding fiber's
 * scanned stack (see the root-coverage note above). Jobs trace their own closures, results,
 * resume args, and waiter links (JOBJ_NODE, conservative). Runs under STW. */
void jacl_sched_mark_roots(void) {
  for (int w = 0; w < JACL_SCHED_MAX_WORKERS; w++) {
    for (int i = 0; i < jacl_rq_count[w]; i++)
      jacl_gc_mark(jaclrt_from_ptr(JACL_TAG_STREAM, jacl_rq[w][(jacl_rq_head[w] + i) % JACL_RQ_CAP]));
    if (jacl_running_job[w]) jacl_gc_mark(jaclrt_from_ptr(JACL_TAG_STREAM, jacl_running_job[w]));
  }
  if (jacl_root_job) jacl_gc_mark(jaclrt_from_ptr(JACL_TAG_STREAM, jacl_root_job));
  mark_task_stacks();   /* guest-memory roots on fiber data stacks (futs[] etc.) */
  for (long i = 0; i < jacl_batch_n; i++) { jacl_gc_mark_word(jacl_batch_arg[i]); jacl_gc_mark_word(jacl_batch_result[i]); }
}

/* Run the program body as the root job on the persistent pool; main is worker 0 and returns
 * the root job's result once it (and thus the program) completes. */
JaclVal jacl_sched_run_main(JaclVal fnref) {
  sched_init();
  JaclObj *root = make_job((long)(uintptr_t)(uint64_t)fnref, 0, /*is_root=*/1, /*owner=*/0);
  jacl_root_job = root;
  slock(); rq_push(root); sunlock(); pool_wake();
  worker_loop(/*is_main=*/1);
  jacl_gc_worker_unregister();                     /* leave the set while joining */
  for (int k = 0; k < jacl_pool_nthreads; k++) __vm_thread_join(jacl_pool_handles[k]);
  jacl_gc_worker_register();
  return (JaclVal)job_get(root, 8);
}

/* ---- spawn / await (multi-threaded, on the pool) ---- */
JaclVal jacl_spawn(JaclVal closure) {
  pool_ensure();                                   /* first concurrent construct starts the pool */
  JaclObj *j = make_job((long)(uintptr_t)(uint64_t)jacl_closure_fn(closure), (long)closure, 0, next_owner());
  slock(); rq_push(j); sunlock(); pool_wake();
  return jaclrt_from_ptr(JACL_TAG_STREAM, j);
}

JaclVal jacl_await(JaclVal future) {
  JaclObj *j = (JaclObj *)jaclrt_as_ptr(future);
  /* Always suspend back to the worker loop, asking to be parked on `j`. The worker re-checks
   * `j` under the scheduler lock and either parks us (re-resumed with j's result when it
   * completes) or, if `j` is already done, re-runs us immediately with the result. Routing the
   * result through the lock-ordered resume_arg keeps every result read synchronized — no
   * lockless DONE/result fast path (that pairing reorders under svm-llvm + the JIT). */
  return (JaclVal)__vm_fiber_suspend((long)j);
}

/* ---- parallel / race ----
 * Run on the calling job's fiber: create the block jobs, enqueue them, await each. The locals
 * (futs[], out) live on the calling fiber, which is always scannable (RUNNING-and-will-suspend
 * or suspended), so they need no global rooting. */
#define JACL_PAR_MAX 32

JaclVal jacl_parallel(JaclVal closures) {
  long n = (long)jacl_vec_count(closures);
  if (n > JACL_PAR_MAX) n = JACL_PAR_MAX;
  JaclVal futs[JACL_PAR_MAX];
  for (long i = 0; i < n; i++) futs[i] = jacl_spawn(jacl_vec_get(closures, (uint32_t)i));
  JaclVal out = jacl_vec_empty();
  for (long i = 0; i < n; i++) out = jacl_vec_push(out, jacl_await(futs[i]));
  return out;
}

JaclVal jacl_race(JaclVal closures) {
  long n = (long)jacl_vec_count(closures);
  if (n <= 0) return JACL_NIL;
  if (n > JACL_PAR_MAX) n = JACL_PAR_MAX;
  JaclVal futs[JACL_PAR_MAX];
  for (long i = 0; i < n; i++) futs[i] = jacl_spawn(jacl_vec_get(closures, (uint32_t)i));
  JaclVal first = jacl_await(futs[0]);             /* await all so they finish before we return */
  for (long i = 1; i < n; i++) (void)jacl_await(futs[i]);
  return first;
}

/* ---- legacy transient batch scheduler (test-only) ----
 * The original P3.4d substrate (spawn fresh vCPU threads, run a fixed batch, join). Retained
 * because the multi-vCPU GC tests (test_sched_mt / test_batch_heap / test_gc_sched) drive it
 * directly; not used by the language surface. */
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
  jacl_batch_n = n;
  jacl_gc_worker_unregister();
  int h[JACL_SCHED_MAX_WORKERS];
  for (int k = 0; k < nworkers; k++) h[k] = __vm_thread_spawn(jacl_batch_worker, (void *)0, 0);
  for (int k = 0; k < nworkers; k++) __vm_thread_join(h[k]);
  jacl_gc_worker_register();
  for (long i = 0; i < n; i++) result[i] = jacl_batch_result[i];
  jacl_batch_n = 0;
}
