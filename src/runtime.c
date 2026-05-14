/*
 * JACL Runtime — Worker thread pool and task execution.
 *
 * Provides a fixed-size worker thread pool with Chase-Lev work-stealing
 * deques, per-thread GC heaps, and grey buffers for concurrent GC support.
 * This file is included after gc_collect.c in the unity build.
 */

#ifndef RUNTIME_C
#define RUNTIME_C

/* ======================================================================
 * Chase-Lev work-stealing deque instantiation for runtime tasks.
 * Element type is uintptr_t (stores RuntimeTask pointers).
 * ====================================================================== */

#define CHASE_LEV_T    uintptr_t
#define CHASE_LEV_NAME rt_deque
/* gc__scan_deque reads buf->data concurrently with the owner's push, so we
 * need TSAN-visible synchronization on the slot accesses. See chase_lev.h. */
#define CHASE_LEV_ATOMIC_DATA
#include "../lib/chase_lev/chase_lev.h"

/* ======================================================================
 * RuntimeTask: unit of work for the thread pool
 * ====================================================================== */

typedef struct {
    void (*fn)(void *data);
    void *data;
    JaclVal gc_root;   /* Tagged value for GC root scanning, JACL_NIL if none */
    JaclVal gc_root2;  /* Second GC root: result for continuations, closure for spawn/parallel/race */
    JaclVal gc_root3;  /* Third GC root: parent ctx for spawn/parallel/race ctx forking */
} RuntimeTask;

/* ======================================================================
 * Sentinels for WorkerThread.currently_executing
 *
 * WORKER_IDLE (0): worker is idle, no task in progress.
 * WORKER_BUSY (1): worker is about to pop a task — GC should spin-wait
 *                   until this resolves to a real task pointer or IDLE.
 * Any other value: pointer to the RuntimeTask being executed.
 * ====================================================================== */

#define WORKER_IDLE ((uintptr_t)0)
#define WORKER_BUSY ((uintptr_t)1)

/* Perf counter typedefs — mirror the definitions in jacl.h. Unity build
 * does not include jacl.h (separately-compiled tests do), so both copies
 * must be kept in sync. Validated by test/test_struct_sizes.c. */
typedef struct {
    uint64_t tasks_executed;
    uint64_t steal_attempts;
    uint64_t steal_successes;
    uint64_t idle_sleep_ns;
    uint64_t inbox_pops;
    uint64_t pinned_inbox_pops;
} WorkerStats;

typedef struct {
    uint64_t total_cycles;
    uint64_t total_cycle_ns;
    uint64_t max_cycle_ns;
    uint64_t total_mark_rounds;
} GCStats;

typedef struct {
    int      num_workers;
    GCStats  gc;
    uint64_t total_bytes_allocated;
    uint64_t total_allocs;
    uint64_t slow_path_allocs;
    WorkerStats workers_total;
    uint32_t current_heap_blocks;
    intptr_t current_inbox_depth;
} JaclPerfSnapshot;

/* GreyBuffer is defined in gc.c (part of the GC subsystem, before vm.c in
 * the unity build). grey_buf_init/push/destroy are available here. */

/* Single-writer relaxed-atomic add for perf counters. Sole writer is the
 * heap/worker's owning thread; cross-thread reads happen in
 * jacl_perf_snapshot post-quiesce (workers are still running, so the
 * reader sees relaxed-consistent values, not necessarily latest, but
 * that's fine for telemetry). Pattern matches the existing
 * total_bytes_allocated bookkeeping in gc__bump_alloc. */
static inline void stats_add_u64(uint64_t *p, uint64_t delta) {
    uint64_t v = ATOMIC_LOAD_EXPLICIT(p, MEM_RELAXED);
    ATOMIC_STORE_EXPLICIT(p, v + delta, MEM_RELAXED);
}

/* Monotonic nanosecond clock for perf counters. Emscripten + Windows fall
 * back to zero so perf telemetry stays consistent (degraded, not broken)
 * on platforms without clock_gettime(CLOCK_MONOTONIC). */
#if defined(__EMSCRIPTEN__) || defined(_WIN32)
static inline uint64_t runtime__now_ns(void) { return 0; }
#else
#include <time.h>
static inline uint64_t runtime__now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
#endif

/* ======================================================================
 * WorkerThread and Runtime structures
 * ====================================================================== */

typedef struct Runtime Runtime;

typedef struct WorkerThread {
    rt_deque_deque    *public_deque;         /* stealable tasks */
    rt_deque_deque    *private_deque;        /* thread-local tasks (not stolen) */
    /* Per-thread GC heap lives in vm.heap — uses shared BlockPool */
    GreyBuffer         grey_buf;             /* write barrier entries */
    RememberedSet      remembered_set;      /* generational old→young pointer tracking */
    volatile uintptr_t currently_executing;  /* IDLE / BUSY / task ptr */
    volatile uint64_t  thread_epoch;         /* set to global_epoch before each task */
    VM                 vm;                   /* per-thread VM instance */
    Runtime           *runtime;              /* back-pointer */
    int                id;                   /* worker index */
    thread_t           thread;               /* OS thread handle */
    arena_t            arena;                /* per-worker arena for VM */
    int               *steal_ids;            /* thief IDs on other workers' public deques */
    /* Pinned inbox: other workers route tasks here when escape analysis pins
     * a closure to this worker. The owner drains this into private_deque at
     * the top of each loop iteration, preserving private_deque's SPSC
     * contract. Mutex-protected MPSC. */
    uintptr_t         *pinned_inbox;
    volatile intptr_t  pinned_inbox_count;
    intptr_t           pinned_inbox_cap;
    platform_mutex_t   pinned_inbox_mutex;
    /* Retired tasks: epoch-deferred free list. After running a task, the
     * worker can't free it immediately because gc_enumerate_roots may have
     * observed the task pointer via currently_executing and be about to
     * dereference task->gc_root*. We defer freeing until a new GC cycle
     * has started (global_epoch > retire_epoch), which means the previous
     * GC's scan has completed (gc_running CAS guarantees no overlap). */
    RuntimeTask      **retired_tasks;
    uint64_t          *retired_epochs;
    uint32_t           retired_count;
    uint32_t           retired_cap;
    /* Perf counters — see WorkerStats in jacl.h. */
    WorkerStats        stats;
} WorkerThread;

struct Runtime {
    WorkerThread       *workers;
    int                 num_workers;
    volatile uint64_t   global_epoch;        /* incremented at each GC start */
    volatile uint32_t   gc_running;          /* 0 = idle, 1 = GC in progress (CAS) */
    volatile uint32_t   gc_active;           /* 1 during mark phase (write barriers check) */
    BlockPool           block_pool;          /* shared block pool for all workers */
    volatile int        shutdown;            /* signal workers to stop */
    /* Task inbox: external submissions (mutex-protected LIFO stack) */
    uintptr_t          *inbox;
    volatile intptr_t   inbox_count;
    intptr_t            inbox_cap;
    platform_mutex_t    inbox_mutex;
    /* Wakeup CV: signaled by push_inbox/push_pinned after appending; workers
     * park on it in the idle path. Replaces 1-10ms sleep backoff. AUDIT §11. */
    platform_cond_t     work_cv;
    /* GC scanner thief slots: registered once at init so gc__scan_deque can
     * safely read deque buffers under epoch-based reclamation. One slot per
     * (worker, deque) pair. Index by worker_id. */
    int                *gc_thief_public_ids;
    int                *gc_thief_private_ids;
    /* External GC roots — JaclVals pinned by C embedders (e.g. completion
     * futures held by polling threads). Without this, the future allocated
     * inside rt_run_to_completion / test_perf is rooted only via the spawn
     * task's gc_root; once the task retires, a concurrent GC reclaims the
     * future and the polling C thread reads zeroed memory (state=PENDING
     * forever — AUDIT.md #0c). */
    JaclVal            *external_roots;
    uint32_t            external_root_count;
    uint32_t            external_root_cap;
    platform_mutex_t    external_roots_mutex;
    /* Perf counters — see GCStats in jacl.h. */
    GCStats             gc_stats;
};

/* Forward declarations for functions used in the worker loop */
void gc__concurrent_task(void *data);
void runtime_submit(Runtime *rt, void (*fn)(void *), void *data);
void gc_concurrent_collect(Runtime *rt);

/* Thread-local worker ID (set in worker loop, -1 for non-worker threads) */
JACL_THREAD_LOCAL int rt__worker_id = -1;

/* Thread-local pointer to current worker (set in worker loop) */
JACL_THREAD_LOCAL WorkerThread *rt__current_worker = NULL;

/* ======================================================================
 * Worker VM initialization — uses the shared block pool
 * ====================================================================== */

void runtime__init_worker_vm(WorkerThread *w) {
    VM *vm        = &w->vm;
    arena_t *arena = &w->arena;

    memset(vm->stack, 0, sizeof(vm->stack));
    vm->stack_top     = 0;
    vm->ip            = NULL;
    vm->chunk         = NULL;
    vm->print_fn      = vm__default_print;
    vm->print_ctx     = NULL;
    vm->arena         = arena;
    vm->intern_table  = NULL;
    vm->top_chunk     = NULL;
    vm->grey_buf      = &w->grey_buf;
    vm->remembered_set = &w->remembered_set;
    vm->gc_active_ptr = &w->runtime->gc_active;
    vm->runtime       = (void *)w->runtime;
    vm->worker_id     = w->id;
    vm->frame_count   = 0;
    vm->error_message = NULL;
    vm->error_line    = 0;
    vm->stack_trace.count = 0;
    vm->struct_registry = NULL;
    vm->ctx_pool        = NULL;
    vm->ctx             = JACL_NIL;
    vm->saved_ctx_count = 0;

    /* Init a local block pool (unused — heap points to shared pool).
     * We still init it so vm_destroy can safely destroy it. */
    gc_block_pool_init(&vm->block_pool);

    /* Initialize heap with the SHARED block pool */
    gc_heap_init(&vm->heap, &w->runtime->block_pool);

    collections__init();

    /* Initialize environment with builtins */
    vm->env.count  = 0;
    vm->env.cap    = VM_ENV_INIT_CAP;
    vm->env.names  = (JaclVal *)arena_alloc(arena, VM_ENV_INIT_CAP * sizeof(JaclVal));
    vm->env.values = (JaclVal *)arena_alloc(arena, VM_ENV_INIT_CAP * sizeof(JaclVal));
    vm->env.names[0]  = jacl_inline_string("true", 4);
    vm->env.values[0] = JACL_TRUE;
    vm->env.names[1]  = jacl_inline_string("false", 5);
    vm->env.values[1] = JACL_FALSE;
    vm->env.names[2]  = jacl_inline_string("nil", 3);
    vm->env.values[2] = JACL_NIL;
    vm->env.count = 3;
}

/* ======================================================================
 * Emergency GC callback for concurrent mode (OOM escalation Tier 1)
 *
 * Attempts to run a full concurrent GC cycle inline. If another GC is
 * already running, waits for it to complete.
 * ====================================================================== */

void runtime__emergency_gc(void *ctx) {
    Runtime *rt = (Runtime *)ctx;
    uint32_t expected = 0;
    if (ATOMIC_CAS(&rt->gc_running, &expected, 1,
                    MEM_ACQ_REL, MEM_RELAXED)) {
        gc_concurrent_collect(rt);
        /* gc_concurrent_collect sets gc_running = 0 */
    } else {
        /* Another GC is already running — wait for it to finish */
        while (ATOMIC_LOAD_EXPLICIT(&rt->gc_running, MEM_ACQUIRE)) {
            SLEEP_MILLISECONDS(1);
        }
    }
}

/* ======================================================================
 * Worker thread main loop
 *
 * Each iteration:
 *   1. Set currently_executing = BUSY (pop-in-transit protocol)
 *   2. Try: inbox → private deque → public deque → steal from others
 *   3. If found: publish task, set epoch, execute, mark idle
 *   4. If not found: mark idle, park on rt->work_cv with 1ms timeout
 * ====================================================================== */

/* Retire a task: append to the deferred-free list with the current global
 * epoch. The actual free happens later, once global_epoch has advanced past
 * retire_epoch (which means the GC scan that may have observed this task
 * via currently_executing has completed). */
static void runtime__retire_task(WorkerThread *self, RuntimeTask *task) {
    if (self->retired_count >= self->retired_cap) {
        uint32_t new_cap = self->retired_cap * 2;
        self->retired_tasks = (RuntimeTask **)realloc(self->retired_tasks,
            (size_t)new_cap * sizeof(RuntimeTask *));
        self->retired_epochs = (uint64_t *)realloc(self->retired_epochs,
            (size_t)new_cap * sizeof(uint64_t));
        self->retired_cap = new_cap;
    }
    uint64_t e = ATOMIC_LOAD_EXPLICIT(&self->runtime->global_epoch,
                                       MEM_ACQUIRE);
    self->retired_tasks[self->retired_count]  = task;
    self->retired_epochs[self->retired_count] = e;
    self->retired_count++;
}

/* Free any retired tasks whose retire_epoch is strictly less than the
 * current global_epoch. By that point, gc_running has been reset since the
 * task was retired, so any GC scan that observed the task pointer has
 * completed and no longer references it. */
static void runtime__drain_retired(WorkerThread *self) {
    uint64_t now = ATOMIC_LOAD_EXPLICIT(&self->runtime->global_epoch,
                                         MEM_ACQUIRE);
    uint32_t write = 0;
    for (uint32_t i = 0; i < self->retired_count; i++) {
        if (self->retired_epochs[i] < now) {
            free(self->retired_tasks[i]);
        } else {
            self->retired_tasks[write]  = self->retired_tasks[i];
            self->retired_epochs[write] = self->retired_epochs[i];
            write++;
        }
    }
    self->retired_count = write;
}

THREAD_PROC_RETURN THREAD_PROC_TYPE runtime__worker_loop(void *arg) {
    WorkerThread *self = (WorkerThread *)arg;
    Runtime *rt = self->runtime;

    rt__worker_id = self->id;

    while (!ATOMIC_LOAD_EXPLICIT(&rt->shutdown, MEM_ACQUIRE)) {
        /* Drain any retired tasks whose epoch has expired. This is the
         * deferred free for tasks finished on previous iterations. */
        if (self->retired_count > 0) runtime__drain_retired(self);

        /* Announce BUSY before any deque/inbox operation */
        ATOMIC_STORE_EXPLICIT(&self->currently_executing, WORKER_BUSY,
                              MEM_RELEASE);

        uintptr_t task_val = 0;
        bool found = false;

        /* 1. Check global inbox (lockless empty check, batched drain).
         * Relaxed load is sufficient: a stale "empty" read just defers
         * pickup by one loop iteration; the under-lock count is
         * authoritative. The under-lock drain mutates count via a local
         * variable and publishes the final value atomically so concurrent
         * lockless readers never race a plain RMW. */
        if (ATOMIC_LOAD_EXPLICIT(&rt->inbox_count, MEM_RELAXED) > 0) {
            MUTEX_LOCK(rt->inbox_mutex);
            intptr_t local = rt->inbox_count;
            uint64_t popped_here = 0;
            while (local > 0) {
                uintptr_t tv = rt->inbox[--local];
                if (!found) {
                    task_val = tv;
                    found = true;
                } else {
                    rt_deque_deque_push(self->private_deque, tv);
                }
                popped_here++;
            }
            ATOMIC_STORE_EXPLICIT(&rt->inbox_count, local, MEM_RELEASE);
            MUTEX_UNLOCK(rt->inbox_mutex);
            stats_add_u64(&self->stats.inbox_pops, popped_here);
        }

        /* 2. Drain our pinned inbox into private_deque. Tasks routed here
         * by other workers when their closure was pinned to us. Same
         * lockless-empty-check + batched-drain pattern as the global inbox. */
        if (ATOMIC_LOAD_EXPLICIT(&self->pinned_inbox_count, MEM_RELAXED) > 0) {
            MUTEX_LOCK(self->pinned_inbox_mutex);
            intptr_t local = self->pinned_inbox_count;
            uint64_t popped_here = 0;
            while (local > 0) {
                uintptr_t tv = self->pinned_inbox[--local];
                if (!found) {
                    task_val = tv;
                    found = true;
                } else {
                    rt_deque_deque_push(self->private_deque, tv);
                }
                popped_here++;
            }
            ATOMIC_STORE_EXPLICIT(&self->pinned_inbox_count, local,
                                  MEM_RELEASE);
            MUTEX_UNLOCK(self->pinned_inbox_mutex);
            stats_add_u64(&self->stats.pinned_inbox_pops, popped_here);
        }

        /* 3. Check own private deque (priority for thread-local tasks) */
        if (!found)
            found = rt_deque_deque_take(self->private_deque, &task_val);

        /* 3. Check own public deque */
        if (!found)
            found = rt_deque_deque_take(self->public_deque, &task_val);

        /* 4. Try stealing from other workers' public deques */
        if (!found) {
            stats_add_u64(&self->stats.steal_attempts, 1);
            for (int i = 0; i < rt->num_workers && !found; i++) {
                if (i == self->id) continue;
                found = rt_deque_deque_steal(
                    rt->workers[i].public_deque,
                    &task_val,
                    self->steal_ids[i]);
            }
            if (found) stats_add_u64(&self->stats.steal_successes, 1);
        }

        if (found) {
            RuntimeTask *task = (RuntimeTask *)task_val;

            /* Publish the task pointer (resolves BUSY for GC) */
            ATOMIC_STORE_EXPLICIT(&self->currently_executing, task_val,
                                  MEM_RELEASE);

            /* Snapshot global epoch for watermark protection */
            ATOMIC_STORE_EXPLICIT(&self->thread_epoch,
                ATOMIC_LOAD_EXPLICIT(&rt->global_epoch, MEM_ACQUIRE),
                MEM_RELEASE);

            /* Set thread-local state for this task */
            gc__current_heap = &self->vm.heap;
            rt__current_worker = self;
            gc__thread_epoch = (uint32_t)ATOMIC_LOAD_EXPLICIT(
                &self->thread_epoch, MEM_RELAXED);
            gc__emergency_gc_fn  = runtime__emergency_gc;
            gc__emergency_gc_ctx = self->runtime;

            /* Execute the task */
            task->fn(task->data);
            stats_add_u64(&self->stats.tasks_executed, 1);

            /* Mark idle */
            ATOMIC_STORE_EXPLICIT(&self->currently_executing, WORKER_IDLE,
                                  MEM_RELEASE);

            /* Retire the task — deferred free. Cannot free immediately
             * because gc_enumerate_roots may have observed the task pointer
             * via currently_executing and not yet dereferenced gc_root*.
             * The actual free happens once global_epoch advances past the
             * retire_epoch (the previous GC's scan has completed by then). */
            runtime__retire_task(self, task);

            /* GC trigger: if allocation threshold exceeded, try to start GC.
             * Uses the adaptive heap->gc_threshold (tuned by
             * gc__adjust_threshold based on survival rate), not the static
             * GC_THRESHOLD constant — otherwise adaptive scheduling is dead
             * in multi-threaded mode (AUDIT.md §6). */
            size_t bsg = ATOMIC_LOAD_EXPLICIT(
                &self->vm.heap.bytes_since_gc, MEM_RELAXED);
            size_t thresh = ATOMIC_LOAD_EXPLICIT(
                &self->vm.heap.gc_threshold, MEM_RELAXED);
            if (bsg > thresh) {
                ATOMIC_STORE_EXPLICIT(&self->vm.heap.bytes_since_gc, 0,
                                      MEM_RELAXED);
                ATOMIC_STORE_EXPLICIT(&self->vm.heap.needs_gc, false,
                                      MEM_RELAXED);
                {
                    uint32_t gc_expected = 0;
                    if (ATOMIC_CAS(&rt->gc_running, &gc_expected, 1,
                                   MEM_ACQ_REL, MEM_RELAXED)) {
                        runtime_submit(rt, gc__concurrent_task, rt);
                    }
                }
            }
        } else {
            /* No work found — park on rt->work_cv with 1ms timeout.
             * Lost-wakeup defense: re-check inbox_count under inbox_mutex
             * before waiting (pushers signal under the same mutex).
             * Pinned pushes don't share the inbox_mutex predicate, so they
             * acquire it briefly to signal; we also check pinned_inbox_count
             * inside the predicate as a defensive double-check. Shutdown is
             * a predicate too so stop_threads' broadcast exits us promptly. */
            ATOMIC_STORE_EXPLICIT(&self->currently_executing, WORKER_IDLE,
                                  MEM_RELEASE);
            uint64_t sleep_start = runtime__now_ns();
            MUTEX_LOCK(rt->inbox_mutex);
            if (rt->inbox_count == 0 &&
                ATOMIC_LOAD_EXPLICIT(&self->pinned_inbox_count,
                                     MEM_RELAXED) == 0 &&
                !ATOMIC_LOAD_EXPLICIT(&rt->shutdown, MEM_ACQUIRE)) {
                COND_WAIT_FOR_MS(rt->work_cv, rt->inbox_mutex, 1);
            }
            MUTEX_UNLOCK(rt->inbox_mutex);
            stats_add_u64(&self->stats.idle_sleep_ns,
                          runtime__now_ns() - sleep_start);
        }
    }

    return (THREAD_PROC_RETURN)0;
}

/* ======================================================================
 * runtime_init: create worker threads and start the pool
 * ====================================================================== */

void runtime_init(Runtime *rt, int num_workers) {
    runtime__init_state(rt, num_workers);
    runtime__start_threads(rt);
}

/* runtime__init_state — initialize all Runtime/WorkerThread fields except
 * the OS-level worker threads. Factored out so test helpers can build a
 * Runtime that doesn't run any worker threads (for unit tests of GC roots,
 * write barriers, etc.) without duplicating the setup. */
void runtime__init_state(Runtime *rt, int num_workers) {
    int i, j;

    rt->num_workers  = num_workers;
    rt->global_epoch = 0;
    rt->gc_running   = 0;
    rt->gc_active    = 0;
    rt->shutdown     = 0;
    memset(&rt->gc_stats, 0, sizeof(rt->gc_stats));

    gc_block_pool_init(&rt->block_pool);

    /* Initialize task inbox */
    rt->inbox_cap   = 64;
    rt->inbox_count = 0;
    rt->inbox = (uintptr_t *)malloc((size_t)rt->inbox_cap * sizeof(uintptr_t));
    MUTEX_INIT(rt->inbox_mutex);
    COND_INIT(rt->work_cv);

    /* External roots — initially empty, grown on first pin */
    rt->external_roots      = NULL;
    rt->external_root_count = 0;
    rt->external_root_cap   = 0;
    MUTEX_INIT(rt->external_roots_mutex);

    /* Allocate and initialize workers */
    rt->workers = (WorkerThread *)calloc((size_t)num_workers,
                                          sizeof(WorkerThread));

    for (i = 0; i < num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        w->id      = i;
        w->runtime = rt;
        w->currently_executing = WORKER_IDLE;
        w->thread_epoch        = 0;

        w->public_deque  = rt_deque_deque_new(6); /* initial capacity 64 */
        w->private_deque = rt_deque_deque_new(6);

        grey_buf_init(&w->grey_buf);
        remembered_set_init(&w->remembered_set);

        w->arena = (arena_t){0};
        runtime__init_worker_vm(w);

        /* Pinned inbox: per-worker MPSC queue for cross-worker pinned tasks */
        w->pinned_inbox_cap   = 32;
        w->pinned_inbox_count = 0;
        w->pinned_inbox = (uintptr_t *)malloc(
            (size_t)w->pinned_inbox_cap * sizeof(uintptr_t));
        MUTEX_INIT(w->pinned_inbox_mutex);

        /* Retired tasks: epoch-deferred free list */
        w->retired_cap    = 32;
        w->retired_count  = 0;
        w->retired_tasks  = (RuntimeTask **)malloc(
            (size_t)w->retired_cap * sizeof(RuntimeTask *));
        w->retired_epochs = (uint64_t *)malloc(
            (size_t)w->retired_cap * sizeof(uint64_t));

        w->steal_ids = (int *)calloc((size_t)num_workers, sizeof(int));
        for (j = 0; j < num_workers; j++)
            w->steal_ids[j] = -1;
    }

    /* Register each worker as a thief on every other worker's public deque */
    for (i = 0; i < num_workers; i++) {
        for (j = 0; j < num_workers; j++) {
            if (i != j) {
                rt->workers[i].steal_ids[j] =
                    rt_deque_deque_register_thief(rt->workers[j].public_deque);
            }
        }
    }

    /* Register a persistent GC scanner thief slot per deque so gc__scan_deque
     * can read buffers safely under chase-lev's epoch-based reclamation.
     * The slot stays INACTIVE except while gc__scan_deque is actively
     * reading; we set it to the current epoch during the scan to pin any
     * retired buffer the read might touch. */
    rt->gc_thief_public_ids  = (int *)malloc((size_t)num_workers * sizeof(int));
    rt->gc_thief_private_ids = (int *)malloc((size_t)num_workers * sizeof(int));
    for (i = 0; i < num_workers; i++) {
        rt->gc_thief_public_ids[i]  =
            rt_deque_deque_register_thief(rt->workers[i].public_deque);
        rt->gc_thief_private_ids[i] =
            rt_deque_deque_register_thief(rt->workers[i].private_deque);
    }
}

void runtime__start_threads(Runtime *rt) {
    for (int i = 0; i < rt->num_workers; i++) {
        THREAD_CREATE(&rt->workers[i].thread, NULL,
                      runtime__worker_loop, &rt->workers[i]);
    }
}

/* ======================================================================
 * runtime_destroy: signal shutdown, join threads, free resources
 * ====================================================================== */

void runtime_destroy(Runtime *rt) {
    runtime__stop_threads(rt);
    runtime__teardown_state(rt);
}

/* runtime__stop_threads — signal shutdown and join all worker threads.
 * Resources are NOT freed; call runtime__teardown_state afterwards.
 *
 * Broadcast under inbox_mutex wakes all parked workers immediately;
 * each re-checks shutdown after wake and exits the loop. AUDIT §11. */
void runtime__stop_threads(Runtime *rt) {
    ATOMIC_STORE_EXPLICIT(&rt->shutdown, 1, MEM_RELEASE);
    MUTEX_LOCK(rt->inbox_mutex);
    COND_BROADCAST(rt->work_cv);
    MUTEX_UNLOCK(rt->inbox_mutex);
    for (int i = 0; i < rt->num_workers; i++)
        THREAD_JOIN(rt->workers[i].thread, NULL);
}

/* runtime__teardown_state — release all Runtime/WorkerThread resources.
 * Assumes worker threads (if any were started) have already been joined.
 * Mirror of runtime__init_state. */
void runtime__teardown_state(Runtime *rt) {
    int i, j;

    /* Unregister all thieves BEFORE freeing any deques (avoids use-after-free
     * when worker i's thief slot references worker j's already-freed deque) */
    for (i = 0; i < rt->num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        for (j = 0; j < rt->num_workers; j++) {
            if (j != i && w->steal_ids[j] >= 0)
                rt_deque_deque_unregister_thief(
                    rt->workers[j].public_deque, w->steal_ids[j]);
        }
    }

    /* Unregister GC scanner thief slots */
    if (rt->gc_thief_public_ids) {
        for (i = 0; i < rt->num_workers; i++) {
            if (rt->gc_thief_public_ids[i] >= 0)
                rt_deque_deque_unregister_thief(
                    rt->workers[i].public_deque, rt->gc_thief_public_ids[i]);
            if (rt->gc_thief_private_ids[i] >= 0)
                rt_deque_deque_unregister_thief(
                    rt->workers[i].private_deque, rt->gc_thief_private_ids[i]);
        }
        free(rt->gc_thief_public_ids);
        free(rt->gc_thief_private_ids);
        rt->gc_thief_public_ids  = NULL;
        rt->gc_thief_private_ids = NULL;
    }

    /* Cleanup each worker */
    for (i = 0; i < rt->num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        uintptr_t task_val;

        /* Drain any remaining tasks from deques */
        while (rt_deque_deque_take(w->public_deque, &task_val))
            free((RuntimeTask *)task_val);
        while (rt_deque_deque_take(w->private_deque, &task_val))
            free((RuntimeTask *)task_val);

        /* Drain pending pinned tasks (threads are joined, no race) */
        for (intptr_t k = 0; k < w->pinned_inbox_count; k++)
            free((RuntimeTask *)w->pinned_inbox[k]);
        free(w->pinned_inbox);
        MUTEX_DESTROY(w->pinned_inbox_mutex);

        /* Free retired tasks (threads are joined, no GC running) */
        for (uint32_t k = 0; k < w->retired_count; k++)
            free(w->retired_tasks[k]);
        free(w->retired_tasks);
        free(w->retired_epochs);

        free(w->steal_ids);

        rt_deque_deque_free(w->public_deque);
        rt_deque_deque_free(w->private_deque);

        grey_buf_destroy(&w->grey_buf);
        remembered_set_destroy(&w->remembered_set);

        /* Cleanup VM: heap returns blocks to shared pool, local pool freed */
        vm_destroy(&w->vm);
        arena_destroy(&w->arena);
    }

    free(rt->workers);

    /* Drain and free inbox */
    for (intptr_t k = 0; k < rt->inbox_count; k++)
        free((RuntimeTask *)rt->inbox[k]);
    free(rt->inbox);
    COND_DESTROY(rt->work_cv);
    MUTEX_DESTROY(rt->inbox_mutex);

    /* Release external root storage (values themselves are not freed —
     * they live in GC heaps which are torn down by vm_destroy above) */
    free(rt->external_roots);
    rt->external_roots      = NULL;
    rt->external_root_count = 0;
    rt->external_root_cap   = 0;
    MUTEX_DESTROY(rt->external_roots_mutex);

    gc_block_pool_destroy(&rt->block_pool);
}

/* ======================================================================
 * Inbox push helper — all task submission funnels through here
 * ====================================================================== */

void runtime__push_inbox(Runtime *rt, RuntimeTask *task) {
    /* §10 fast path: from-worker submission pushes directly to the caller's
     * own public_deque, bypassing the global inbox mutex. The deque is
     * Chase-Lev SPSC on the owner-push side; other workers can steal from
     * the public deque, preserving load balancing. No CV signal is needed
     * — the submitter is itself active (will pop on its next iteration),
     * and parked workers wake on the 1ms CV timeout and find the task via
     * the steal path. The GC's deque scan is already registered as a
     * thief (§2) and epoch-protected, so concurrent collection is safe. */
    WorkerThread *self = rt__current_worker;
    if (self != NULL && self->runtime == rt) {
        rt_deque_deque_push(self->public_deque, (uintptr_t)task);
        return;
    }

    /* Slow path: external thread (no worker context). Use the global inbox
     * + CV signal as before. External submissions are low-volume relative
     * to from-worker, so the mutex here is no longer a hot-path concern. */
    MUTEX_LOCK(rt->inbox_mutex);
    intptr_t c = rt->inbox_count;
    if (c >= rt->inbox_cap) {
        intptr_t new_cap = rt->inbox_cap * 2;
        rt->inbox = (uintptr_t *)realloc(rt->inbox,
                                          (size_t)new_cap * sizeof(uintptr_t));
        rt->inbox_cap = new_cap;
    }
    rt->inbox[c] = (uintptr_t)task;
    /* Release on count: pairs with lockless MEM_RELAXED loads in the worker
     * loop and the MEM_ACQUIRE load in gc_enumerate_roots. */
    ATOMIC_STORE_EXPLICIT(&rt->inbox_count, c + 1, MEM_RELEASE);
    /* Wake one idle worker — signal-one is right since we added one task. */
    COND_SIGNAL(rt->work_cv);
    MUTEX_UNLOCK(rt->inbox_mutex);
}

/* ======================================================================
 * Pinned task routing — push to a specific worker's private deque so
 * only that worker executes the task. Currently all pinned tasks target
 * thread 0. Used when a concurrent body mutates non-local mutable state
 * (upvalue or global set!). Thread 0 is the single owner of all
 * non-local mutable state, ensuring consistency across workers.
 * ====================================================================== */

void runtime__push_pinned(Runtime *rt, RuntimeTask *task, int worker_id) {
    if (worker_id < 0 || worker_id >= rt->num_workers) {
        /* Fallback: worker ID not valid (e.g., task created outside workers) */
        runtime__push_inbox(rt, task);
        return;
    }
    WorkerThread *target = &rt->workers[worker_id];
    MUTEX_LOCK(target->pinned_inbox_mutex);
    intptr_t c = target->pinned_inbox_count;
    if (c >= target->pinned_inbox_cap) {
        intptr_t new_cap = target->pinned_inbox_cap * 2;
        target->pinned_inbox = (uintptr_t *)realloc(target->pinned_inbox,
                                          (size_t)new_cap * sizeof(uintptr_t));
        target->pinned_inbox_cap = new_cap;
    }
    target->pinned_inbox[c] = (uintptr_t)task;
    /* Release on count: pairs with lockless MEM_RELAXED loads in the target
     * worker's loop and the MEM_ACQUIRE load in gc_enumerate_roots. */
    ATOMIC_STORE_EXPLICIT(&target->pinned_inbox_count, c + 1, MEM_RELEASE);
    MUTEX_UNLOCK(target->pinned_inbox_mutex);

    /* Wake an idle worker (any worker — target may or may not be the one
     * that picks it up; non-targeted wakeups fall through within 1ms). */
    MUTEX_LOCK(rt->inbox_mutex);
    COND_SIGNAL(rt->work_cv);
    MUTEX_UNLOCK(rt->inbox_mutex);
}

/* ======================================================================
 * Task submission — external callers push to the global inbox
 * ====================================================================== */

/* ======================================================================
 * External root pinning — keep a JaclVal alive across GC cycles when held
 * only by a C pointer outside any GC-scanned structure. Required for
 * completion futures observed by polling C threads (rt_run_to_completion,
 * test_perf, embedders). Without pinning, a concurrent GC after task
 * retire reclaims the future and the C reader sees zeroed memory.
 * ====================================================================== */

uint32_t runtime_pin_value(Runtime *rt, JaclVal val) {
    MUTEX_LOCK(rt->external_roots_mutex);
    /* Reuse the first cleared slot to keep handles bounded under churn */
    for (uint32_t i = 0; i < rt->external_root_count; i++) {
        if (rt->external_roots[i] == JACL_NIL) {
            rt->external_roots[i] = val;
            MUTEX_UNLOCK(rt->external_roots_mutex);
            return i;
        }
    }
    if (rt->external_root_count >= rt->external_root_cap) {
        uint32_t new_cap = rt->external_root_cap ? rt->external_root_cap * 2 : 8;
        rt->external_roots = (JaclVal *)realloc(rt->external_roots,
                                  (size_t)new_cap * sizeof(JaclVal));
        rt->external_root_cap = new_cap;
    }
    uint32_t handle = rt->external_root_count++;
    rt->external_roots[handle] = val;
    MUTEX_UNLOCK(rt->external_roots_mutex);
    return handle;
}

void runtime_unpin_value(Runtime *rt, uint32_t handle) {
    MUTEX_LOCK(rt->external_roots_mutex);
    if (handle < rt->external_root_count) {
        rt->external_roots[handle] = JACL_NIL;
    }
    MUTEX_UNLOCK(rt->external_roots_mutex);
}

void runtime_submit(Runtime *rt, void (*fn)(void *), void *data) {
    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = fn;
    task->data     = data;
    task->gc_root  = JACL_NIL;
    task->gc_root2 = JACL_NIL;
    task->gc_root3 = JACL_NIL;

    runtime__push_inbox(rt, task);
}

/* ======================================================================
 * Closure task execution on worker VMs
 * ====================================================================== */

/* Reset VM state and set up a single closure call frame.
 * argc is the number of arguments (0 for no-arg calls, 1 for single-arg
 * like continuation/CPS calls). argv points to argument values (NULL if
 * argc==0). */
void runtime__setup_call(VM *vm, JaclClosure *cl,
                                int argc, JaclVal *argv) {
    vm->stack_top   = 0;
    vm->frame_count = 0;
    vm->error_message = NULL;
    vm->error_line    = 0;
    vm->stack_trace.count = 0;

    vm->stack[0] = jacl_closure_ptr(cl);
    for (int i = 0; i < argc; i++)
        vm->stack[1 + i] = argv[i];
    vm->stack_top = 1 + argc;

    vm->frames[0].closure    = cl;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 1;
    vm->frames[0].chunk      = &cl->chunk;
    vm->frame_count = 1;
    vm->ip        = cl->chunk.code;
    vm->chunk     = &cl->chunk;
    vm->top_chunk = &cl->chunk;
}

void runtime__exec_closure(void *data) {
    JaclClosure *cl = (JaclClosure *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;

    /* Safety: skip if closure has no valid bytecode */
    if (!cl || !cl->chunk.code) return;

    runtime__setup_call(vm, cl, 0, NULL);
    vm__run(vm, 0);
}

void runtime_submit_task(Runtime *rt, JaclClosure *closure,
                                 bool thread_local) {
    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__exec_closure;
    task->data     = closure;
    task->gc_root  = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);
    task->gc_root2 = JACL_NIL;
    task->gc_root3 = JACL_NIL;

    runtime__push_inbox(rt, task);

    (void)thread_local;
}

/* ======================================================================
 * Root enumeration for concurrent GC (US-009)
 *
 * Scans all GC roots across all worker threads without stopping the world.
 * Uses the BUSY sentinel protocol to handle the pop-in-transit race.
 * ====================================================================== */

/* Maximum spins when waiting for BUSY sentinel to resolve */
#define GC_BUSY_SPIN_MAX 10000

/* Snapshot a Chase-Lev deque's contents and push task gc_roots onto mark stack.
 * Conservative: may include already-completed or stolen tasks (harmless).
 *
 * Buffer-reclamation safety: chase-lev frees retired buffers only when no
 * registered thief has an epoch ≤ the buffer's retire_epoch. We hold a
 * persistent GC thief slot per deque (`thief_id`); writing our observed
 * `dq->epoch` to that slot before reading `dq->buffer` pins any retired
 * buffer we might still be reading from. Resetting to INACTIVE lets the
 * owner reclaim it on the next push grow. */
void gc__scan_deque(rt_deque_deque *dq, GCMarkStack *ms, int thief_id) {
    /* Epoch enter: announce we're reading at the current epoch.
     * RELEASE order pairs with the owner's ACQUIRE load in CL_DEQUE_RECLAIM,
     * giving a happens-before edge that pins any retired buffer with
     * retire_epoch <= our observed epoch. */
    uint64_t e = ATOMIC_LOAD_EXPLICIT(&dq->epoch, MEM_ACQUIRE);
    ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch, e, MEM_RELEASE);

    uint64_t t = ATOMIC_LOAD_EXPLICIT(&dq->top, MEM_ACQUIRE);
    ATOMIC_FENCE(MEM_SEQ_CST);
    uint64_t b = ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_ACQUIRE);

    if ((int64_t)(b - t) > 0) {
        /* Atomic-acquire load: pairs with the release store in PUSH's grow
         * path. Our thief epoch already pins the buffer against reclaim. */
        rt_deque_buffer *buf = ATOMIC_LOAD_EXPLICIT(&dq->buffer, MEM_ACQUIRE);
        for (uint64_t i = t; i < b; i++) {
            /* ACQUIRE: pairs with chase_lev.h's CL_DATA_STORE which uses
             * RELEASE (CHASE_LEV_ATOMIC_DATA mode). Without per-slot
             * acquire, the GC can read the pointer but miss the writer
             * thread's earlier writes to the pointed-to task struct. */
            uintptr_t val = ATOMIC_LOAD_EXPLICIT(
                &buf->data[i & buf->mask], MEM_ACQUIRE);
            if (val > WORKER_BUSY) {
                RuntimeTask *task = (RuntimeTask *)val;
                gc__ms_push_val(ms, task->gc_root);
                gc__ms_push_val(ms, task->gc_root2);
                gc__ms_push_val(ms, task->gc_root3);
            }
        }
    }

    /* Epoch exit: stop pinning retired buffers. UINT64_MAX is the sentinel
     * value chase-lev uses for "thief inactive" (CL_EPOCH_INACTIVE before
     * the template undefs it). RELEASE order ensures all our buffer reads
     * happen-before this slot reset is visible to the owner's reclaim. */
    ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch,
                          UINT64_MAX, MEM_RELEASE);
}

/* Enumerate all GC roots across the runtime and push onto mark stack.
 *
 * Root sources per worker:
 *   1. currently_executing slot (BUSY spin protocol)
 *   2. Public deque snapshot
 *   3. Private deque snapshot
 *   4. VM stack values
 *   5. Call frame closures
 *   6. Call frame chunk constants (heap literals)
 *   7. Global environment values
 *   8. Intern table entries (Phase 1: immortal strings)
 *
 * Plus global inbox tasks. */
void gc_enumerate_roots(Runtime *rt, GCMarkStack *ms) {
    for (int w_idx = 0; w_idx < rt->num_workers; w_idx++) {
        WorkerThread *w = &rt->workers[w_idx];

        /* 1. Currently executing task (BUSY sentinel protocol)
         *    Fast spin first — BUSY typically resolves in nanoseconds (3–5
         *    instructions between setting BUSY and publishing the task ptr).
         *    Slow path yields to handle scheduling delays defensively. */
        uintptr_t ce = ATOMIC_LOAD_EXPLICIT(&w->currently_executing,
                                             MEM_ACQUIRE);
        if (ce == WORKER_BUSY) {
            for (int spin = 0; spin < GC_BUSY_SPIN_MAX; spin++) {
                ce = ATOMIC_LOAD_EXPLICIT(&w->currently_executing,
                                           MEM_ACQUIRE);
                if (ce != WORKER_BUSY) break;
            }
            while (ce == WORKER_BUSY) {
                SLEEP_MILLISECONDS(1);
                ce = ATOMIC_LOAD_EXPLICIT(&w->currently_executing,
                                           MEM_ACQUIRE);
            }
        }
        if (ce != WORKER_IDLE && ce != WORKER_BUSY) {
            RuntimeTask *task = (RuntimeTask *)ce;
            gc__ms_push_val(ms, task->gc_root);
            gc__ms_push_val(ms, task->gc_root2);
            gc__ms_push_val(ms, task->gc_root3);
        }

        /* 2–3. Deque snapshots (public + private) — use the GC thief slot
         * to pin buffers against concurrent grow-and-reclaim. */
        gc__scan_deque(w->public_deque,  ms, rt->gc_thief_public_ids[w_idx]);
        gc__scan_deque(w->private_deque, ms, rt->gc_thief_private_ids[w_idx]);

        /* CPS continuations capture all live state as task roots (per
         * GC_CONCURRENCY_DESIGN.md Section 7). VM stack scanning removed —
         * not thread-safe and unnecessary with CPS transform. */

        /* 4. Global environment values. Acquire-loads pair with the
         * worker's release-stores in vm__env_set / vm__env_grow. Load
         * count FIRST: writer assigns env.values (grow) before
         * incrementing count, so if we see a fresh count, the matching
         * fresh values pointer is also visible. Per-slot acquire on the
         * iterated reads makes TSAN happy about the slot accesses. */
        uint32_t env_n = ATOMIC_LOAD_EXPLICIT(&w->vm.env.count,
                                              MEM_ACQUIRE);
        JaclVal *env_vals = ATOMIC_LOAD_EXPLICIT(&w->vm.env.values,
                                                 MEM_ACQUIRE);
        for (uint32_t i = 0; i < env_n; i++) {
            JaclVal v = ATOMIC_LOAD_EXPLICIT(&env_vals[i], MEM_ACQUIRE);
            gc__ms_push_val(ms, v);
        }

        /* 5. Intern table entries — treated as weak roots, matching
         * single-threaded gc_mark. Live entries are kept by being reachable
         * from real roots (env, constants, in-flight task closures). Dead
         * entries are evicted by gc_sweep_intern_table after mark phase
         * converges (gc_concurrent_collect step 7.5). Without this, every
         * intern entry was pinned and the table grew without bound under
         * string churn (AUDIT.md §4). */

        /* 6. (formerly: walked ctx_pool->free_list_head). The pool
         * was removed in AUDIT.md §18 — ctx allocations now go
         * straight through gc_alloc, and freed ctxs become reachable
         * only via vm->ctx / vm->saved_ctx[] (scanned just below).
         * Free list is always empty; no walk needed. */

        /* 7. Current ctx register (implicit context struct).
         * ACQUIRE pairs with the RELEASE stores in ctx_fork / ctx_unfork /
         * OP_SET_CTX. Plain reads here can tear or miss the GCHeader fields
         * the allocator wrote before publishing the pointer. */
        {
            JaclVal cv = (JaclVal)ATOMIC_LOAD_EXPLICIT(
                (volatile uint64_t*)&w->vm.ctx, MEM_ACQUIRE);
            gc__ms_push_val(ms, cv);
        }

        /* 8. Saved ctx stack (with-ctx nesting).
         * Same RELEASE/ACQUIRE pairing — slots written by OP_CTX_FORK. */
        for (uint8_t sci = 0; sci < w->vm.saved_ctx_count; sci++) {
            JaclVal sv = (JaclVal)ATOMIC_LOAD_EXPLICIT(
                (volatile uint64_t*)&w->vm.saved_ctx[sci], MEM_ACQUIRE);
            gc__ms_push_val(ms, sv);
        }

        /* 9. Pinned inbox: tasks routed here by other workers awaiting
         * drain into this worker's private_deque. Acquired under lock so
         * we don't race with a concurrent push. */
        MUTEX_LOCK(w->pinned_inbox_mutex);
        for (intptr_t pi = 0; pi < w->pinned_inbox_count; pi++) {
            RuntimeTask *task = (RuntimeTask *)w->pinned_inbox[pi];
            gc__ms_push_val(ms, task->gc_root);
            gc__ms_push_val(ms, task->gc_root2);
            gc__ms_push_val(ms, task->gc_root3);
        }
        MUTEX_UNLOCK(w->pinned_inbox_mutex);
    }

    /* 10. Inbox tasks (external submissions awaiting pickup) */
    MUTEX_LOCK(rt->inbox_mutex);
    for (intptr_t i = 0; i < rt->inbox_count; i++) {
        RuntimeTask *task = (RuntimeTask *)rt->inbox[i];
        gc__ms_push_val(ms, task->gc_root);
        gc__ms_push_val(ms, task->gc_root2);
        gc__ms_push_val(ms, task->gc_root3);
    }
    MUTEX_UNLOCK(rt->inbox_mutex);

    /* 11. External roots pinned by C embedders. NIL slots are skipped by
     * gc__ms_push_val (heap-type check). */
    MUTEX_LOCK(rt->external_roots_mutex);
    for (uint32_t i = 0; i < rt->external_root_count; i++) {
        gc__ms_push_val(ms, rt->external_roots[i]);
    }
    MUTEX_UNLOCK(rt->external_roots_mutex);
}

/* ======================================================================
 * Concurrent mark-sweep GC with epoch watermark
 *
 * Runs as a task on any worker thread. Other threads continue executing
 * during mark and sweep phases (non-stop-the-world).
 *
 * Algorithm:
 *   1. Increment global_epoch
 *   2. Set gc_active = true (enables write barriers)
 *   3. Compute watermark = min(all thread_epochs)
 *   4. Enumerate roots (BUSY sentinel protocol)
 *   5. Mark phase with periodic grey buffer draining
 *   6. Final grey buffer drain + process remaining entries
 *   7. Set gc_active = false
 *   8. Sweep all workers' heaps (concurrent-safe, epoch watermark)
 *   9. Toggle current_mark on all heaps, reset counters
 *  10. Set gc_running = false
 * ====================================================================== */

/* Maximum convergence loop rounds for final grey buffer drain.
 * Prevents unbounded looping under pathological mutation rates. */
#ifndef GC_FINAL_DRAIN_MAX_ROUNDS
#define GC_FINAL_DRAIN_MAX_ROUNDS 8
#endif

/* Drain grey buffers from all workers, pushing new entries onto mark stack.
 * `drained` tracks how far each worker's buffer has been processed.
 * Returns true if any new entries were found.
 *
 * Synchronization: each GreyBuffer has a mutex held by grey_buf_push during
 * its realloc + entry write + count update. We acquire the same mutex here
 * to read the entries array safely. See AUDIT.md §3 and SYNCHRONIZATION.md. */
bool gc__drain_grey_bufs(Runtime *rt, GCMarkStack *ms,
                                 uint32_t *drained) {
    bool found_new = false;
    for (int i = 0; i < rt->num_workers; i++) {
        GreyBuffer *gb = &rt->workers[i].grey_buf;
        MUTEX_LOCK(gb->mutex);
        uint32_t current = gb->count;
        if (current > drained[i]) {
            found_new = true;
            for (uint32_t j = drained[i]; j < current; j++) {
                gc__ms_push_val(ms, gb->entries[j]);
            }
            drained[i] = current;
        }
        MUTEX_UNLOCK(gb->mutex);
    }
    return found_new;
}

void gc_concurrent_collect(Runtime *rt) {
    int i;
    GCMarkStack ms;
    gc__ms_init(&ms);

    uint64_t cycle_start_ns = runtime__now_ns();
    uint64_t mark_rounds_observed = 0;

    /* Set struct registry for GC tracing */
    if (rt->num_workers > 0) {
        gc__struct_registry = rt->workers[0].vm.struct_registry;
    }

    /* 1. Increment global epoch */
    uint64_t new_epoch = ATOMIC_LOAD_EXPLICIT(&rt->global_epoch,
                                               MEM_RELAXED) + 1;
    ATOMIC_STORE_EXPLICIT(&rt->global_epoch, new_epoch, MEM_RELEASE);

    /* 2. Set gc_active = true (enables write barriers on all workers) */
    ATOMIC_STORE_EXPLICIT(&rt->gc_active, 1, MEM_RELEASE);

    /* 3. Compute watermark = min(all thread_epochs) */
    uint64_t watermark64 = UINT64_MAX;
    for (i = 0; i < rt->num_workers; i++) {
        uint64_t te = ATOMIC_LOAD_EXPLICIT(&rt->workers[i].thread_epoch,
                                            MEM_ACQUIRE);
        if (te < watermark64) watermark64 = te;
    }
    uint32_t watermark = (uint32_t)watermark64;

    /* 4. Enumerate roots across all workers */
    gc_enumerate_roots(rt, &ms);

    /* Use a consistent current_mark from worker 0 (all heaps share same value) */
    uint8_t mark = rt->workers[0].vm.heap.current_mark;

    /* 5. Mark phase with periodic grey buffer draining */
    uint32_t *drained = (uint32_t *)calloc((size_t)rt->num_workers,
                                            sizeof(uint32_t));
    int mark_steps = 0;
    void *ptr;

    while (gc__ms_pop(&ms, &ptr)) {
        GCHeader *hdr = gc_header_of(ptr);
        /* Defensive: alloc_total==0 means either pre-init (allocator has
         * not yet written this field) or post-sweep (object was zeroed).
         * A valid live object always has alloc_total > 0. Skip rather than
         * dispatch on obj_type=0 (OBJ_CLOSURE) and read garbage payload. */
        if (hdr->alloc_total == 0) continue;
        if (hdr->mark == mark) continue; /* already marked this cycle */
        hdr->mark = mark;
        gc__trace_object(ptr, &ms);

        /* Periodic grey buffer drain every 256 objects */
        if (++mark_steps % 256 == 0) {
            gc__drain_grey_bufs(rt, &ms, drained);
        }
    }

    /* 6. Final grey buffer drain — convergence loop.
     * Repeatedly drain grey buffers and process the mark stack until no new
     * entries are found, or we hit the maximum round cap. This closes the
     * window where workers push to grey buffers between the final drain and
     * gc_active being set to false. */
    {
        int rounds = 0;
        bool has_new;
        do {
            has_new = gc__drain_grey_bufs(rt, &ms, drained);
            while (gc__ms_pop(&ms, &ptr)) {
                GCHeader *hdr = gc_header_of(ptr);
                if (hdr->alloc_total == 0) continue;
                if (hdr->mark == mark) continue;
                hdr->mark = mark;
                gc__trace_object(ptr, &ms);
            }
            rounds++;
        } while (has_new && rounds < GC_FINAL_DRAIN_MAX_ROUNDS);
        mark_rounds_observed = (uint64_t)rounds;
    }

    free(drained);

    /* 7. Set gc_active = false (disables write barriers) */
    ATOMIC_STORE_EXPLICIT(&rt->gc_active, 0, MEM_RELEASE);

    /* 7.5. Evict dead intern table entries on each worker's table before
     * sweep zeroes the underlying JaclHeapString memory. Mirrors the
     * single-threaded gc_collect order (mark → sweep_intern_table →
     * sweep). The watermark protects strings interned after this cycle's
     * root scan: gc_enumerate_roots no longer pushes intern entries, so
     * an entry inserted after marking would otherwise look dead.
     * gc_sweep_intern_table takes table->lock, serializing with any
     * concurrent jacl_intern call on a worker thread. */
    for (i = 0; i < rt->num_workers; i++) {
        JaclInternTable *it = rt->workers[i].vm.intern_table;
        if (it) {
            gc_sweep_intern_table(it, &rt->workers[i].vm.heap, watermark);
        }
    }

    /* 8. Sweep all workers' heaps with epoch watermark.
     * Skip each worker's active allocation block (current_block).
     * Track bytes_survived for adaptive threshold adjustment.
     * Fully-empty blocks are unlinked and returned to the pool inline. */
    int nw = rt->num_workers;

    for (i = 0; i < nw; i++) {
        ThreadHeap *heap = &rt->workers[i].vm.heap;
        /* Pass NULL: gc_sweep_concurrent re-snapshots heap->current_block
         * under heap->blocks_mutex internally (fixes AUDIT.md §7's stale
         * skip-block race). */
        size_t survived = gc_sweep_concurrent(heap, NULL, watermark, mark,
                                               &rt->block_pool);
        gc__adjust_threshold(heap, survived);
    }

    /* 9. Toggle current_mark on all heaps, reset allocation counters
     * and grey buffers.
     *
     * The grey_buf reset is done under the buffer's mutex. Even though
     * gc_active=0 (step 7) signals workers to stop firing the barrier, an
     * in-flight grey_buf_push that already passed the fast-path check may
     * still be in the slow path when we reach here. The mutex serializes
     * us with any such in-flight push. */
    uint8_t next_mark = 1 - mark;
    for (i = 0; i < rt->num_workers; i++) {
        ThreadHeap *wh = &rt->workers[i].vm.heap;
        wh->current_mark   = next_mark;
        ATOMIC_STORE_EXPLICIT(&wh->bytes_since_gc, 0, MEM_RELAXED);
        ATOMIC_STORE_EXPLICIT(&wh->needs_gc, false, MEM_RELAXED);
        wh->gc_cycle_count++;
        /* Concurrent GC is always major — snapshot old gen size */
        wh->last_major_old_gen_bytes = wh->old_gen_bytes;
        GreyBuffer *gb = &rt->workers[i].grey_buf;
        MUTEX_LOCK(gb->mutex);
        gb->count = 0;
        MUTEX_UNLOCK(gb->mutex);
    }

    gc__ms_destroy(&ms);

    /* Record cycle stats before releasing gc_running. Sole writer is
     * whichever worker won the gc_running CAS; reader is jacl_perf_snapshot
     * which can run concurrently (snapshot may be called from the main
     * thread mid-run, not just post-quiesce — see test_perf.c). Use the
     * same relaxed-atomic single-writer pattern as WorkerStats. */
    {
        uint64_t cycle_ns = runtime__now_ns() - cycle_start_ns;
        stats_add_u64(&rt->gc_stats.total_cycles, 1);
        stats_add_u64(&rt->gc_stats.total_cycle_ns, cycle_ns);
        uint64_t prev_max = ATOMIC_LOAD_EXPLICIT(
            &rt->gc_stats.max_cycle_ns, MEM_RELAXED);
        if (cycle_ns > prev_max) {
            ATOMIC_STORE_EXPLICIT(&rt->gc_stats.max_cycle_ns,
                                  cycle_ns, MEM_RELAXED);
        }
        stats_add_u64(&rt->gc_stats.total_mark_rounds,
                      mark_rounds_observed);
    }

    /* 10. Set gc_running = false (allow next GC cycle) */
    ATOMIC_STORE_EXPLICIT(&rt->gc_running, 0, MEM_RELEASE);
}

/* Task function for concurrent GC — submitted to the inbox */
void gc__concurrent_task(void *data) {
    Runtime *rt = (Runtime *)data;
    gc_concurrent_collect(rt);
}

/* ======================================================================
 * jacl_perf_snapshot — aggregate per-worker counters into one struct.
 *
 * Intended call site: after all submitted work has drained and workers
 * are quiesced (typically after a benchmark scenario completes). Reads
 * are relaxed atomic so we see eventually-consistent values even if a
 * worker is still mid-task; callers wanting precise numbers should
 * quiesce first.
 * ====================================================================== */
JaclPerfSnapshot jacl_perf_snapshot(Runtime *rt) {
    JaclPerfSnapshot s;
    memset(&s, 0, sizeof(s));
    if (!rt) return s;
    s.num_workers = rt->num_workers;
    /* Field-by-field atomic loads — same relaxed pattern as WorkerStats. */
    s.gc.total_cycles =
        ATOMIC_LOAD_EXPLICIT(&rt->gc_stats.total_cycles, MEM_RELAXED);
    s.gc.total_cycle_ns =
        ATOMIC_LOAD_EXPLICIT(&rt->gc_stats.total_cycle_ns, MEM_RELAXED);
    s.gc.max_cycle_ns =
        ATOMIC_LOAD_EXPLICIT(&rt->gc_stats.max_cycle_ns, MEM_RELAXED);
    s.gc.total_mark_rounds =
        ATOMIC_LOAD_EXPLICIT(&rt->gc_stats.total_mark_rounds, MEM_RELAXED);
    for (int i = 0; i < rt->num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        ThreadHeap   *h = &w->vm.heap;
        s.total_bytes_allocated +=
            ATOMIC_LOAD_EXPLICIT(&h->total_bytes_allocated, MEM_RELAXED);
        s.total_allocs +=
            ATOMIC_LOAD_EXPLICIT(&h->total_allocs, MEM_RELAXED);
        s.slow_path_allocs +=
            ATOMIC_LOAD_EXPLICIT(&h->slow_path_allocs, MEM_RELAXED);
        s.workers_total.tasks_executed +=
            ATOMIC_LOAD_EXPLICIT(&w->stats.tasks_executed, MEM_RELAXED);
        s.workers_total.steal_attempts +=
            ATOMIC_LOAD_EXPLICIT(&w->stats.steal_attempts, MEM_RELAXED);
        s.workers_total.steal_successes +=
            ATOMIC_LOAD_EXPLICIT(&w->stats.steal_successes, MEM_RELAXED);
        s.workers_total.idle_sleep_ns +=
            ATOMIC_LOAD_EXPLICIT(&w->stats.idle_sleep_ns, MEM_RELAXED);
        s.workers_total.inbox_pops +=
            ATOMIC_LOAD_EXPLICIT(&w->stats.inbox_pops, MEM_RELAXED);
        s.workers_total.pinned_inbox_pops +=
            ATOMIC_LOAD_EXPLICIT(&w->stats.pinned_inbox_pops, MEM_RELAXED);
    }
    s.current_heap_blocks  = rt->block_pool.total_blocks_allocated;
    s.current_inbox_depth  = ATOMIC_LOAD_EXPLICIT(&rt->inbox_count, MEM_RELAXED);
    return s;
}

void jacl_perf_snapshot_print_json(FILE *out, const JaclPerfSnapshot *s) {
    if (!out || !s) return;
    fprintf(out,
        "{"
        "\"num_workers\":%d,"
        "\"gc_cycles\":%llu,"
        "\"gc_total_ns\":%llu,"
        "\"gc_max_ns\":%llu,"
        "\"gc_mark_rounds\":%llu,"
        "\"total_bytes_allocated\":%llu,"
        "\"total_allocs\":%llu,"
        "\"slow_path_allocs\":%llu,"
        "\"tasks_executed\":%llu,"
        "\"steal_attempts\":%llu,"
        "\"steal_successes\":%llu,"
        "\"idle_sleep_ns\":%llu,"
        "\"inbox_pops\":%llu,"
        "\"pinned_inbox_pops\":%llu,"
        "\"current_heap_blocks\":%u,"
        "\"current_inbox_depth\":%lld"
        "}\n",
        s->num_workers,
        (unsigned long long)s->gc.total_cycles,
        (unsigned long long)s->gc.total_cycle_ns,
        (unsigned long long)s->gc.max_cycle_ns,
        (unsigned long long)s->gc.total_mark_rounds,
        (unsigned long long)s->total_bytes_allocated,
        (unsigned long long)s->total_allocs,
        (unsigned long long)s->slow_path_allocs,
        (unsigned long long)s->workers_total.tasks_executed,
        (unsigned long long)s->workers_total.steal_attempts,
        (unsigned long long)s->workers_total.steal_successes,
        (unsigned long long)s->workers_total.idle_sleep_ns,
        (unsigned long long)s->workers_total.inbox_pops,
        (unsigned long long)s->workers_total.pinned_inbox_pops,
        s->current_heap_blocks,
        (long long)s->current_inbox_depth);
}

/* Trigger concurrent GC from vm.c's safepoint.
 * Attempts to CAS gc_running from 0 to 1; if successful, submits a GC task. */
void gc_concurrent_trigger(void *runtime_ptr) {
    Runtime *rt = (Runtime *)runtime_ptr;
    uint32_t expected = 0;
    if (ATOMIC_CAS(&rt->gc_running, &expected, 1,
                    MEM_ACQ_REL, MEM_RELAXED)) {
        runtime_submit(rt, gc__concurrent_task, rt);
    }
}

/* ======================================================================
 * Resolve closure: dynamically-created JaclClosure that resolves a future.
 *
 * The resolve_k closure takes 1 parameter (the result value) and has
 * 1 upvalue (the future to resolve). Bytecode:
 *   OP_GET_LOCAL  0   ; get result parameter
 *   OP_GET_UPVALUE 0  ; get the future (captured)
 *   OP_RESOLVE_FUTURE ; resolve(future, result), push nil
 *   OP_RETURN         ; return nil
 * ====================================================================== */

/* Static bytecode for resolve_k closures (shared by all instances) */
uint8_t resolve_k_code[] = {
    OP_GET_UPVALUE, 0,    /* push future (below) */
    OP_GET_LOCAL, 0,      /* push result param (top) */
    OP_RESOLVE_FUTURE,    /* pop result, pop future, resolve */
    OP_RETURN
};
uint32_t resolve_k_lines[] = { 0, 0, 0, 0, 0, 0 };

JaclVal runtime__create_resolve_closure(ThreadHeap *heap, arena_t *arena,
                                                JaclVal future_val) {
    /* Allocate closure + 1 upvalue slot */
    size_t cl_size = sizeof(JaclClosure) + sizeof(JaclVal);
    JaclClosure *cl = (JaclClosure *)gc_alloc(heap, OBJ_CLOSURE, cl_size);

    /* Set up the bytecode chunk (points to static arrays) */
    cl->chunk.code        = resolve_k_code;
    cl->chunk.code_count  = (uint32_t)(sizeof(resolve_k_code));
    cl->chunk.lines       = resolve_k_lines;
    cl->chunk.constants   = NULL;
    cl->chunk.const_count = 0;
    cl->chunk.arena       = arena;

    cl->param_count   = 1;
    cl->param_total_slots = 1;
    cl->upvalue_count = 1;
    cl->upvalues      = (JaclVal *)(cl + 1); /* trailing array */
    cl->upvalues[0]   = future_val;
    cl->name          = "__resolve_k";
    cl->param_names   = NULL;
    cl->min_args      = 1;
    cl->variadic      = false;
    cl->pinned        = false;
    cl->pin_worker_id = -1;

    return jacl_closure(cl);
}

/* ======================================================================
 * Parallel-slot completion closure (parallel_k)
 *
 * Used as error_k on SM parallel body state machines. When called with
 * the body's result, directly completes the parallel aggregation slot
 * (stores result, handles errors, increments counter, schedules join).
 *
 * Bytecode:
 *   OP_GET_UPVALUE  0   ; push agg_val
 *   OP_GET_UPVALUE  1   ; push index (as i32)
 *   OP_GET_LOCAL    0   ; push result parameter
 *   OP_COMPLETE_PARALLEL ; complete slot, push nil
 *   OP_RETURN
 * ====================================================================== */

uint8_t parallel_k_code[] = {
    OP_GET_UPVALUE, 0,    /* push agg_val */
    OP_GET_UPVALUE, 1,    /* push index */
    OP_GET_LOCAL, 0,      /* push result param */
    OP_COMPLETE_PARALLEL, /* complete parallel slot */
    OP_RETURN
};
uint32_t parallel_k_lines[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

JaclVal runtime__create_parallel_k(ThreadHeap *heap, arena_t *arena,
                                           JaclVal agg_val, uint32_t index) {
    /* Allocate closure + 2 upvalue slots */
    size_t cl_size = sizeof(JaclClosure) + 2 * sizeof(JaclVal);
    JaclClosure *cl = (JaclClosure *)gc_alloc(heap, OBJ_CLOSURE, cl_size);

    cl->chunk.code        = parallel_k_code;
    cl->chunk.code_count  = (uint32_t)(sizeof(parallel_k_code));
    cl->chunk.lines       = parallel_k_lines;
    cl->chunk.constants   = NULL;
    cl->chunk.const_count = 0;
    cl->chunk.arena       = arena;

    cl->param_count   = 1;
    cl->param_total_slots = 1;
    cl->upvalue_count = 2;
    cl->upvalues      = (JaclVal *)(cl + 1);
    cl->upvalues[0]   = agg_val;
    cl->upvalues[1]   = jacl_i32((int32_t)index);
    cl->name          = "__parallel_k";
    cl->param_names   = NULL;
    cl->min_args      = 1;
    cl->variadic      = false;
    cl->pinned        = false;
    cl->pin_worker_id = -1;

    return jacl_closure(cl);
}

/* Forward declaration for SM resumption (used by parallel slot completion) */
void runtime__schedule_sm_resumption(void *runtime_ptr,
                                             JaclVal state_machine,
                                             JaclVal result);

/* --- Parallel slot completion logic (called from OP_COMPLETE_PARALLEL) --- */

void runtime__complete_parallel_slot(void *runtime_ptr, VM *vm,
                                             JaclVal agg_val,
                                             uint32_t index,
                                             JaclVal task_result) {
    Runtime *rt = (Runtime *)runtime_ptr;
    ParallelAgg *agg = as_parallel_agg(agg_val);
    bool task_errored = jacl_is_error(task_result);

    /* Store result at our index (no contention — unique index per task) */
    gc_write_barrier(vm->grey_buf, &rt->gc_active,
                     agg->results[index], task_result);
    agg->results[index] = task_result;

    /* Handle error: first error wins via CAS */
    if (task_errored) {
        uint32_t expected = 0;
        if (ATOMIC_CAS(&agg->errored, &expected, 1,
                        MEM_ACQ_REL, MEM_RELAXED)) {
            gc_write_barrier(vm->grey_buf, &rt->gc_active,
                             JACL_NIL, task_result);
            ATOMIC_STORE_EXPLICIT(&agg->error_val, (uint64_t)task_result,
                                   MEM_RELEASE);
        }
    }

    /* Atomically increment completion counter */
    uint32_t prev = 0;
    uint32_t desired = 1;
    for (;;) {
        prev = ATOMIC_LOAD_EXPLICIT(&agg->completed, MEM_ACQUIRE);
        desired = prev + 1;
        if (ATOMIC_CAS(&agg->completed, &prev, desired,
                        MEM_ACQ_REL, MEM_RELAXED)) {
            break;
        }
    }

    /* If we're the last task to complete, schedule the join continuation */
    if (desired == agg->count) {
        JaclVal cont_arg;

        if (ATOMIC_LOAD_EXPLICIT(&agg->errored, MEM_ACQUIRE)) {
            cont_arg = (JaclVal)ATOMIC_LOAD_EXPLICIT(&agg->error_val,
                                                      MEM_ACQUIRE);
        } else {
            gc__current_heap = &vm->heap;
            jacl_vec_root *vec = jacl_vec_empty();
            for (uint32_t i = 0; i < agg->count; i++) {
                vec = jacl_vec_push_back(vec, agg->results[i]);
            }
            cont_arg = jacl_vector_ptr(vec);
        }

        /* Resume parent state machine with results */
        runtime__schedule_sm_resumption(rt, agg->state_machine, cont_arg);
    }
}

/* ======================================================================
 * Race-slot completion closure (race_k)
 *
 * Used as error_k on SM race body state machines. When called with
 * the body's result, CAS-settles the race and schedules the winner.
 *
 * Bytecode:
 *   OP_GET_UPVALUE  0   ; push agg_val
 *   OP_GET_LOCAL    0   ; push result parameter
 *   OP_COMPLETE_RACE    ; settle race, push nil
 *   OP_RETURN
 * ====================================================================== */

uint8_t race_k_code[] = {
    OP_GET_UPVALUE, 0,    /* push agg_val */
    OP_GET_LOCAL, 0,      /* push result param */
    OP_COMPLETE_RACE,     /* settle race */
    OP_RETURN
};
uint32_t race_k_lines[] = { 0, 0, 0, 0, 0, 0, 0 };

JaclVal runtime__create_race_k(ThreadHeap *heap, arena_t *arena,
                                       JaclVal agg_val) {
    /* Allocate closure + 1 upvalue slot */
    size_t cl_size = sizeof(JaclClosure) + sizeof(JaclVal);
    JaclClosure *cl = (JaclClosure *)gc_alloc(heap, OBJ_CLOSURE, cl_size);

    cl->chunk.code        = race_k_code;
    cl->chunk.code_count  = (uint32_t)(sizeof(race_k_code));
    cl->chunk.lines       = race_k_lines;
    cl->chunk.constants   = NULL;
    cl->chunk.const_count = 0;
    cl->chunk.arena       = arena;

    cl->param_count   = 1;
    cl->param_total_slots = 1;
    cl->upvalue_count = 1;
    cl->upvalues      = (JaclVal *)(cl + 1);
    cl->upvalues[0]   = agg_val;
    cl->name          = "__race_k";
    cl->param_names   = NULL;
    cl->min_args      = 1;
    cl->variadic      = false;
    cl->pinned        = false;
    cl->pin_worker_id = -1;

    return jacl_closure(cl);
}

/* --- Race slot completion logic (called from OP_COMPLETE_RACE) --- */

void runtime__complete_race_slot(void *runtime_ptr, VM *vm,
                                         JaclVal agg_val,
                                         JaclVal task_result) {
    (void)vm;
    Runtime *rt = (Runtime *)runtime_ptr;
    RaceAgg *agg = as_race_agg(agg_val);

    /* CAS settled: winner (0->1) resumes parent SM, losers discard */
    uint32_t expected = 0;
    if (ATOMIC_CAS(&agg->settled, &expected, 1, MEM_ACQ_REL, MEM_RELAXED)) {
        /* Resume parent state machine with winner result */
        runtime__schedule_sm_resumption(rt, agg->state_machine, task_result);
    }
}

/* ======================================================================
 * Continuation scheduling — used by internal closures (resolve_k,
 * parallel_k, race_k) that complete futures/aggregates.
 * ====================================================================== */

typedef struct {
    JaclClosure *continuation;
    JaclVal      result;
} ContinuationTaskData;

void runtime__continuation_task_exec(void *data) {
    ContinuationTaskData *ctd = (ContinuationTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;

    /* Set up: call continuation(result) */
    runtime__setup_call(vm, ctd->continuation, 1, &ctd->result);

    vm__run(vm, 0);
    free(ctd);
}

void runtime__schedule_continuation(void *runtime_ptr,
                                            JaclClosure *continuation,
                                            JaclVal result) {
    Runtime *rt = (Runtime *)runtime_ptr;

    ContinuationTaskData *ctd = (ContinuationTaskData *)malloc(
        sizeof(ContinuationTaskData));
    ctd->continuation = continuation;
    ctd->result       = result;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__continuation_task_exec;
    task->data     = ctd;
    task->gc_root  = jacl_closure_ptr(continuation);
    task->gc_root2 = result;
    task->gc_root3 = JACL_NIL;

    if (continuation->pinned && continuation->pin_worker_id >= 0) {
        runtime__push_pinned(rt, task, continuation->pin_worker_id);
    } else {
        runtime__push_inbox(rt, task);
    }
}

/* ======================================================================
 * State machine task data and execution (US-014)
 * ====================================================================== */

typedef struct {
    JaclVal state_machine;  /* tagged state machine object */
    JaclVal result;         /* resume value (future result) */
} SMTaskData;

void runtime__state_machine_task_exec(void *data) {
    SMTaskData *smd = (SMTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;

    JaclStateMachine *sm = jacl_as_state_machine(smd->state_machine);
    JaclClosure *sm_cl = jacl_as_closure(sm->sm_closure);

    /* Set up: call sm_closure(state_obj, resume_value) */
    JaclVal args[2];
    args[0] = smd->state_machine;
    args[1] = smd->result;
    runtime__setup_call(vm, sm_cl, 2, args);

    VMResult r = vm__run(vm, 0);

    /* Check if SM function completed (frame_count == 0) or suspended
       (frame_count > 0 means it hit OP_AWAIT_SM again). */
    bool completed = (vm->frame_count == 0);
    if (completed) {
        JaclVal result;
        if (r != VM_OK) {
            result = jacl_set_error(jacl_inline_string("error", 5));
        } else if (vm->stack_top > 0) {
            result = vm->stack[vm->stack_top - 1];
        } else {
            result = JACL_NIL;
        }

        if (!jacl_is_nil(sm->error_k) && jacl_is_closure(sm->error_k)) {
            /* SM has a completion callback (set for spawn/parallel/race bodies).
               Call it with the result (success or error). */
            JaclClosure *k = jacl_as_closure(sm->error_k);
            runtime__schedule_continuation(self->runtime, k, result);
        }
    }
    free(smd);
}

void runtime__schedule_sm_resumption(void *runtime_ptr,
                                             JaclVal state_machine,
                                             JaclVal result) {
    Runtime *rt = (Runtime *)runtime_ptr;

    SMTaskData *smd = (SMTaskData *)malloc(sizeof(SMTaskData));
    smd->state_machine = state_machine;
    smd->result        = result;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__state_machine_task_exec;
    task->data     = smd;
    task->gc_root  = state_machine;
    task->gc_root2 = result;
    task->gc_root3 = JACL_NIL;

    /* Respect pinning from the SM closure */
    JaclStateMachine *sm = jacl_as_state_machine(state_machine);
    JaclClosure *sm_cl = jacl_as_closure(sm->sm_closure);
    if (sm_cl->pinned && sm_cl->pin_worker_id >= 0) {
        runtime__push_pinned(rt, task, sm_cl->pin_worker_id);
    } else {
        runtime__push_inbox(rt, task);
    }
}

void runtime__schedule_waiters(void *runtime_ptr,
                                       FutureWaiter *waiters,
                                       JaclVal result) {
    while (waiters) {
        runtime__schedule_sm_resumption(runtime_ptr, waiters->continuation, result);
        waiters = waiters->next;
    }
}

/* ======================================================================
 * Spawn task data and execution
 * ====================================================================== */

typedef struct {
    JaclClosure *closure;
    JaclVal      future_val;
    JaclVal      parent_ctx;  /* parent's ctx to fork for spawned task */
} SpawnTaskData;

void runtime__spawn_task_exec(void *data) {
    SpawnTaskData *std = (SpawnTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;
    JaclClosure *cl = std->closure;
    JaclFuture *fut = jacl_as_future(std->future_val);

    JaclVal saved_worker_ctx = ctx_fork(vm, std->parent_ctx);

    if (cl->is_sm_compiled) {
        /* SM closure: create state machine, set error_k to resolve_k,
           call sm_closure(state_obj, nil). If SM completes on first run,
           resolve directly. If it suspends, error_k handles later completion. */
        JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
        JaclStateMachine *sm = jacl_as_state_machine(sm_val);
        sm->sm_closure = jacl_closure_ptr(cl);

        JaclVal resolve_k = runtime__create_resolve_closure(
            &vm->heap, &self->arena, std->future_val);
        sm->error_k = resolve_k;

        JaclVal args[2];
        args[0] = sm_val;
        args[1] = JACL_NIL;
        runtime__setup_call(vm, cl, 2, args);

        VMResult r = vm__run(vm, 0);

        /* If SM completed on first run, resolve directly */
        if (vm->frame_count == 0) {
            uint32_t fstate = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED);
            if (fstate == FUTURE_PENDING) {
                if (r == VM_OK && vm->stack_top > 0 &&
                    !jacl_is_error(vm->stack[vm->stack_top - 1])) {
                    JaclVal result = vm->stack[vm->stack_top - 1];
                    FutureWaiter *waiters = jacl_future_resolve(fut, result,
                                        &self->grey_buf, &self->runtime->gc_active);
                    runtime__schedule_waiters(self->runtime, waiters, result);
                } else {
                    JaclVal err = (vm->stack_top > 0 && jacl_is_error(vm->stack[vm->stack_top - 1]))
                        ? vm->stack[vm->stack_top - 1]
                        : jacl_set_error(jacl_inline_string("error", 5));
                    FutureWaiter *waiters = jacl_future_error(fut, err,
                                      &self->grey_buf, &self->runtime->gc_active);
                    runtime__schedule_waiters(self->runtime, waiters, err);
                }
            }
        }
        /* If frame_count > 0: SM suspended at OP_AWAIT_SM, will be resumed
           later by runtime__state_machine_task_exec which uses error_k */
    } else {
        /* Non-CPS closure: call directly, resolve future with return value */
        runtime__setup_call(vm, cl, 0, NULL);

        VMResult r = vm__run(vm, 0);

        if (r == VM_OK && vm->stack_top > 0) {
            JaclVal spawn_result = vm->stack[vm->stack_top - 1];
            FutureWaiter *waiters = jacl_future_resolve(fut, spawn_result,
                                &self->grey_buf, &self->runtime->gc_active);
            runtime__schedule_waiters(self->runtime, waiters, spawn_result);
        } else {
            JaclVal err = jacl_set_error(jacl_inline_string("error", 5));
            FutureWaiter *waiters = jacl_future_error(fut, err,
                              &self->grey_buf, &self->runtime->gc_active);
            runtime__schedule_waiters(self->runtime, waiters, err);
        }
    }

    ctx_unfork(vm, saved_worker_ctx);

    free(std);
}

void runtime__submit_spawn_task(void *runtime_ptr, JaclClosure *closure,
                                        JaclVal future_val, JaclVal parent_ctx) {
    Runtime *rt = (Runtime *)runtime_ptr;

    SpawnTaskData *std = (SpawnTaskData *)malloc(sizeof(SpawnTaskData));
    std->closure    = closure;
    std->future_val = future_val;
    std->parent_ctx = parent_ctx;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__spawn_task_exec;
    task->data     = std;
    task->gc_root  = future_val; /* Future is the GC root for this task */
    task->gc_root2 = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);
    task->gc_root3 = parent_ctx; /* Keep parent ctx alive until task starts */

    if (closure->pinned && closure->pin_worker_id >= 0) {
        runtime__push_pinned(rt, task, closure->pin_worker_id);
    } else {
        runtime__push_inbox(rt, task);
    }
}

/* ======================================================================
 * Parallel task data and execution
 *
 * Each parallel body becomes a task referencing a shared ParallelAgg.
 * When all tasks complete, the last one builds a result vector and
 * schedules the join continuation.
 * ====================================================================== */

typedef struct {
    JaclClosure  *closure;
    JaclVal       agg_val;      /* tagged pointer to ParallelAgg */
    uint32_t      index;        /* position in results array */
    JaclVal       parent_ctx;   /* parent's ctx to fork for parallel body */
} ParallelTaskData;

void runtime__parallel_task_exec(void *data) {
    ParallelTaskData *ptd = (ParallelTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;
    JaclClosure *cl = ptd->closure;
    (void)as_parallel_agg(ptd->agg_val); /* validate tag */

    JaclVal saved_worker_ctx = ctx_fork(vm, ptd->parent_ctx);

    if (cl->is_sm_compiled) {
        /* SM closure: create state machine, set error_k to parallel_k */
        JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
        JaclStateMachine *sm = jacl_as_state_machine(sm_val);
        sm->sm_closure = jacl_closure_ptr(cl);

        JaclVal parallel_k = runtime__create_parallel_k(
            &vm->heap, &self->arena, ptd->agg_val, ptd->index);
        sm->error_k = parallel_k;

        JaclVal args[2];
        args[0] = sm_val;
        args[1] = JACL_NIL;
        runtime__setup_call(vm, cl, 2, args);

        VMResult r = vm__run(vm, 0);

        /* If SM completed on first run, complete slot directly */
        if (vm->frame_count == 0) {
            JaclVal task_result;
            if (r == VM_OK && vm->stack_top > 0) {
                task_result = vm->stack[vm->stack_top - 1];
            } else {
                task_result = jacl_set_error(jacl_inline_string("error", 5));
            }
            runtime__complete_parallel_slot(self->runtime, vm,
                                             ptd->agg_val, ptd->index, task_result);
        }
        /* If frame_count > 0: SM suspended, error_k (parallel_k) handles later */
    } else {
        /* Non-CPS closure: call directly, complete slot via shared helper */
        JaclVal task_result;

        runtime__setup_call(vm, cl, 0, NULL);

        VMResult r = vm__run(vm, 0);

        if (r == VM_OK && vm->stack_top > 0) {
            task_result = vm->stack[vm->stack_top - 1];
        } else {
            task_result = jacl_set_error(jacl_inline_string("error", 5));
        }

        runtime__complete_parallel_slot(self->runtime, vm,
                                         ptd->agg_val, ptd->index, task_result);
    }

    ctx_unfork(vm, saved_worker_ctx);

    free(ptd);
}

void runtime__submit_parallel_task(void *runtime_ptr,
                                           JaclClosure *closure,
                                           JaclVal agg_val,
                                           uint32_t index,
                                           JaclVal parent_ctx) {
    Runtime *rt = (Runtime *)runtime_ptr;

    ParallelTaskData *ptd = (ParallelTaskData *)malloc(sizeof(ParallelTaskData));
    ptd->closure    = closure;
    ptd->agg_val    = agg_val;
    ptd->index      = index;
    ptd->parent_ctx = parent_ctx;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__parallel_task_exec;
    task->data     = ptd;
    task->gc_root  = agg_val; /* Aggregate is the GC root — traces continuation + results */
    task->gc_root2 = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);
    task->gc_root3 = parent_ctx;

    if (closure->pinned && closure->pin_worker_id >= 0) {
        runtime__push_pinned(rt, task, closure->pin_worker_id);
    } else {
        runtime__push_inbox(rt, task);
    }
}

/* ======================================================================
 * Race task data and execution
 *
 * Each race body becomes a task referencing a shared RaceAgg.
 * First task to complete (CAS settled 0->1) is the winner and
 * schedules the race continuation. Losers silently discard results.
 * ====================================================================== */

typedef struct {
    JaclClosure  *closure;
    JaclVal       agg_val;      /* tagged pointer to RaceAgg */
    JaclVal       parent_ctx;   /* parent's ctx to fork for race body */
} RaceTaskData;

void runtime__race_task_exec(void *data) {
    RaceTaskData *rtd = (RaceTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;
    JaclClosure *cl = rtd->closure;

    JaclVal saved_worker_ctx = ctx_fork(vm, rtd->parent_ctx);

    if (cl->is_sm_compiled) {
        /* SM closure: create state machine, set error_k to race_k */
        JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
        JaclStateMachine *sm = jacl_as_state_machine(sm_val);
        sm->sm_closure = jacl_closure_ptr(cl);

        JaclVal race_k = runtime__create_race_k(
            &vm->heap, &self->arena, rtd->agg_val);
        sm->error_k = race_k;

        JaclVal args[2];
        args[0] = sm_val;
        args[1] = JACL_NIL;
        runtime__setup_call(vm, cl, 2, args);

        VMResult r = vm__run(vm, 0);

        /* If SM completed on first run, settle race directly */
        if (vm->frame_count == 0) {
            JaclVal task_result;
            if (r == VM_OK && vm->stack_top > 0) {
                task_result = vm->stack[vm->stack_top - 1];
            } else {
                task_result = jacl_set_error(jacl_inline_string("error", 5));
            }
            runtime__complete_race_slot(self->runtime, vm,
                                         rtd->agg_val, task_result);
        }
    } else {
        JaclVal task_result = JACL_NIL;

        runtime__setup_call(vm, cl, 0, NULL);

        VMResult r = vm__run(vm, 0);

        if (r == VM_OK && vm->stack_top > 0) {
            task_result = vm->stack[vm->stack_top - 1];
        } else {
            task_result = jacl_set_error(jacl_inline_string("error", 5));
        }

        runtime__complete_race_slot(self->runtime, vm,
                                     rtd->agg_val, task_result);
    }

    ctx_unfork(vm, saved_worker_ctx);

    free(rtd);
}

void runtime__submit_race_task(void *runtime_ptr,
                                       JaclClosure *closure,
                                       JaclVal agg_val,
                                       JaclVal parent_ctx) {
    Runtime *rt = (Runtime *)runtime_ptr;

    RaceTaskData *rtd = (RaceTaskData *)malloc(sizeof(RaceTaskData));
    rtd->closure    = closure;
    rtd->agg_val    = agg_val;
    rtd->parent_ctx = parent_ctx;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__race_task_exec;
    task->data     = rtd;
    task->gc_root  = agg_val;
    task->gc_root2 = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);
    task->gc_root3 = parent_ctx;

    if (closure->pinned && closure->pin_worker_id >= 0) {
        runtime__push_pinned(rt, task, closure->pin_worker_id);
    } else {
        runtime__push_inbox(rt, task);
    }
}

/* ======================================================================
 * rt_run_to_completion: submit a closure as a task and block until
 * its completion future resolves.
 * ====================================================================== */

VMResult rt_run_to_completion(Runtime *rt, JaclClosure *closure,
                                      arena_t *arena) {
    /* Create a completion future on worker 0's heap */
    JaclVal completion = jacl_future(&rt->workers[0].vm.heap);
    JaclFuture *cfut = jacl_as_future(completion);

    /* Pin against GC so concurrent sweep after task retire doesn't zero
     * the future memory while this thread is still polling it. */
    uint32_t pin = runtime_pin_value(rt, completion);

    runtime__submit_spawn_task(rt, closure, completion, JACL_NIL);

    /* Block until the completion future resolves (with timeout) */
    for (int ms = 0; ms < 5000; ms++) {
        uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_ACQUIRE);
        if (state == FUTURE_RESOLVED || state == FUTURE_ERROR) break;
        SLEEP_MILLISECONDS(1);
    }

    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    runtime_unpin_value(rt, pin);
    if (state == FUTURE_ERROR || state == FUTURE_PENDING) {
        return VM_RUNTIME_ERROR;
    }
    return VM_OK;

    (void)arena;
}

#endif /* RUNTIME_C */
