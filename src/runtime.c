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
#include "../lib/chase_lev/chase_lev.h"

/* ======================================================================
 * RuntimeTask: unit of work for the thread pool
 * ====================================================================== */

typedef struct {
    void (*fn)(void *data);
    void *data;
    JaclVal gc_root;   /* Tagged value for GC root scanning, JACL_NIL if none */
    JaclVal gc_root2;  /* Second GC root: result for continuations, closure for spawn/parallel/race */
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

/* GreyBuffer is defined in gc.c (part of the GC subsystem, before vm.c in
 * the unity build). grey_buf_init/push/destroy are available here. */

/* ======================================================================
 * WorkerThread and Runtime structures
 * ====================================================================== */

typedef struct Runtime Runtime;

typedef struct WorkerThread {
    rt_deque_deque    *public_deque;         /* stealable tasks */
    rt_deque_deque    *private_deque;        /* thread-local tasks (not stolen) */
    /* Per-thread GC heap lives in vm.heap — uses shared BlockPool */
    GreyBuffer         grey_buf;             /* write barrier entries */
    volatile uintptr_t currently_executing;  /* IDLE / BUSY / task ptr */
    volatile uint64_t  thread_epoch;         /* set to global_epoch before each task */
    VM                 vm;                   /* per-thread VM instance */
    Runtime           *runtime;              /* back-pointer */
    int                id;                   /* worker index */
    thread_t           thread;               /* OS thread handle */
    arena_t            arena;                /* per-worker arena for VM */
    int               *steal_ids;            /* thief IDs on other workers' public deques */
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
};

/* Forward declarations for functions used in the worker loop */
static void gc__concurrent_task(void *data);
static void runtime_submit(Runtime *rt, void (*fn)(void *), void *data);
static void gc_concurrent_collect(Runtime *rt);

/* Thread-local worker ID (set in worker loop, -1 for non-worker threads) */
static __thread int rt__worker_id = -1;

/* Thread-local pointer to current worker (set in worker loop) */
static __thread WorkerThread *rt__current_worker = NULL;

/* ======================================================================
 * Worker VM initialization — uses the shared block pool
 * ====================================================================== */

static void runtime__init_worker_vm(WorkerThread *w) {
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
    vm->gc_active_ptr = &w->runtime->gc_active;
    vm->runtime       = (void *)w->runtime;
    vm->frame_count   = 0;
    vm->error_message = NULL;
    vm->error_line    = 0;
    vm->stack_trace.count = 0;

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

static void runtime__emergency_gc(void *ctx) {
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
 *   4. If not found: mark idle, backoff sleep
 * ====================================================================== */

static THREAD_PROC_RETURN THREAD_PROC_TYPE runtime__worker_loop(void *arg) {
    WorkerThread *self = (WorkerThread *)arg;
    Runtime *rt = self->runtime;
    int backoff = 0;

    rt__worker_id = self->id;

    while (!ATOMIC_LOAD_EXPLICIT(&rt->shutdown, MEM_ACQUIRE)) {
        /* Announce BUSY before any deque/inbox operation */
        ATOMIC_STORE_EXPLICIT(&self->currently_executing, WORKER_BUSY,
                              MEM_RELEASE);

        uintptr_t task_val = 0;
        bool found = false;

        /* 1. Check global inbox (lockless empty check, batched drain) */
        if (rt->inbox_count > 0) {
            MUTEX_LOCK(rt->inbox_mutex);
            while (rt->inbox_count > 0) {
                uintptr_t tv = rt->inbox[--rt->inbox_count];
                if (!found) {
                    task_val = tv;
                    found = true;
                } else {
                    rt_deque_deque_push(self->private_deque, tv);
                }
            }
            MUTEX_UNLOCK(rt->inbox_mutex);
        }

        /* 2. Check own private deque (priority for thread-local tasks) */
        if (!found)
            found = rt_deque_deque_take(self->private_deque, &task_val);

        /* 3. Check own public deque */
        if (!found)
            found = rt_deque_deque_take(self->public_deque, &task_val);

        /* 4. Try stealing from other workers' public deques */
        if (!found) {
            for (int i = 0; i < rt->num_workers && !found; i++) {
                if (i == self->id) continue;
                found = rt_deque_deque_steal(
                    rt->workers[i].public_deque,
                    &task_val,
                    self->steal_ids[i]);
            }
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

            /* Free the task envelope */
            free(task);

            /* Mark idle */
            ATOMIC_STORE_EXPLICIT(&self->currently_executing, WORKER_IDLE,
                                  MEM_RELEASE);

            /* GC trigger: if allocation threshold exceeded, try to start GC */
            if (self->vm.heap.bytes_since_gc > GC_THRESHOLD) {
                self->vm.heap.bytes_since_gc = 0;
                self->vm.heap.needs_gc = false;
                {
                    uint32_t gc_expected = 0;
                    if (ATOMIC_CAS(&rt->gc_running, &gc_expected, 1,
                                   MEM_ACQ_REL, MEM_RELAXED)) {
                        runtime_submit(rt, gc__concurrent_task, rt);
                    }
                }
            }

            backoff = 0;
        } else {
            /* No work found — go idle with progressive backoff */
            ATOMIC_STORE_EXPLICIT(&self->currently_executing, WORKER_IDLE,
                                  MEM_RELEASE);
            if (backoff < 10)
                backoff++;
            SLEEP_MILLISECONDS(backoff);
        }
    }

    return (THREAD_PROC_RETURN)0;
}

/* ======================================================================
 * runtime_init: create worker threads and start the pool
 * ====================================================================== */

static void runtime_init(Runtime *rt, int num_workers) {
    int i, j;

    rt->num_workers  = num_workers;
    rt->global_epoch = 0;
    rt->gc_running   = 0;
    rt->gc_active    = 0;
    rt->shutdown     = 0;

    gc_block_pool_init(&rt->block_pool);

    /* Initialize task inbox */
    rt->inbox_cap   = 64;
    rt->inbox_count = 0;
    rt->inbox = (uintptr_t *)malloc((size_t)rt->inbox_cap * sizeof(uintptr_t));
    MUTEX_INIT(rt->inbox_mutex);

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

        w->arena = (arena_t){0};
        runtime__init_worker_vm(w);

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

    /* Start worker threads */
    for (i = 0; i < num_workers; i++) {
        THREAD_CREATE(&rt->workers[i].thread, NULL,
                      runtime__worker_loop, &rt->workers[i]);
    }
}

/* ======================================================================
 * runtime_destroy: signal shutdown, join threads, free resources
 * ====================================================================== */

static void runtime_destroy(Runtime *rt) {
    int i, j;

    /* Signal all workers to stop */
    ATOMIC_STORE_EXPLICIT(&rt->shutdown, 1, MEM_RELEASE);

    /* Join all worker threads */
    for (i = 0; i < rt->num_workers; i++)
        THREAD_JOIN(rt->workers[i].thread, NULL);

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

    /* Cleanup each worker */
    for (i = 0; i < rt->num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        uintptr_t task_val;

        /* Drain any remaining tasks from deques */
        while (rt_deque_deque_take(w->public_deque, &task_val))
            free((RuntimeTask *)task_val);
        while (rt_deque_deque_take(w->private_deque, &task_val))
            free((RuntimeTask *)task_val);

        free(w->steal_ids);

        rt_deque_deque_free(w->public_deque);
        rt_deque_deque_free(w->private_deque);

        grey_buf_destroy(&w->grey_buf);

        /* Cleanup VM: heap returns blocks to shared pool, local pool freed */
        vm_destroy(&w->vm);
        arena_destroy(&w->arena);
    }

    free(rt->workers);

    /* Drain and free inbox */
    for (intptr_t k = 0; k < rt->inbox_count; k++)
        free((RuntimeTask *)rt->inbox[k]);
    free(rt->inbox);
    MUTEX_DESTROY(rt->inbox_mutex);

    gc_block_pool_destroy(&rt->block_pool);
}

/* ======================================================================
 * Inbox push helper — all task submission funnels through here
 * ====================================================================== */

static void runtime__push_inbox(Runtime *rt, RuntimeTask *task) {
    MUTEX_LOCK(rt->inbox_mutex);
    if (rt->inbox_count >= rt->inbox_cap) {
        intptr_t new_cap = rt->inbox_cap * 2;
        rt->inbox = (uintptr_t *)realloc(rt->inbox,
                                          (size_t)new_cap * sizeof(uintptr_t));
        rt->inbox_cap = new_cap;
    }
    rt->inbox[rt->inbox_count++] = (uintptr_t)task;
    MUTEX_UNLOCK(rt->inbox_mutex);
}

/* ======================================================================
 * Task submission — external callers push to the global inbox
 * ====================================================================== */

static void runtime_submit(Runtime *rt, void (*fn)(void *), void *data) {
    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = fn;
    task->data     = data;
    task->gc_root  = JACL_NIL;
    task->gc_root2 = JACL_NIL;

    runtime__push_inbox(rt, task);
}

/* ======================================================================
 * Closure task execution on worker VMs
 * ====================================================================== */

/* Reset VM state and set up a single closure call frame.
 * argc is the number of arguments (0 for no-arg calls, 1 for single-arg
 * like continuation/CPS calls). argv points to argument values (NULL if
 * argc==0). */
static void runtime__setup_call(VM *vm, JaclClosure *cl,
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

static void runtime__exec_closure(void *data) {
    JaclClosure *cl = (JaclClosure *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;

    /* Safety: skip if closure has no valid bytecode */
    if (!cl || !cl->chunk.code) return;

    runtime__setup_call(vm, cl, 0, NULL);
    vm__run(vm, 0);
}

static void runtime_submit_task(Runtime *rt, JaclClosure *closure,
                                 bool thread_local) {
    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__exec_closure;
    task->data     = closure;
    task->gc_root  = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);
    task->gc_root2 = JACL_NIL;

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
 * Conservative: may include already-completed or stolen tasks (harmless). */
static void gc__scan_deque(rt_deque_deque *dq, GCMarkStack *ms) {
    uint64_t t = ATOMIC_LOAD_EXPLICIT(&dq->top, MEM_ACQUIRE);
    ATOMIC_FENCE(MEM_SEQ_CST);
    uint64_t b = ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_ACQUIRE);

    if ((int64_t)(b - t) <= 0) return; /* empty */

    /* Read buffer pointer — visible after acquire on top/bottom */
    rt_deque_buffer *buf = dq->buffer;

    for (uint64_t i = t; i < b; i++) {
        uintptr_t val = buf->data[i & buf->mask];
        if (val > WORKER_BUSY) {
            RuntimeTask *task = (RuntimeTask *)val;
            gc__ms_push_val(ms, task->gc_root);
            gc__ms_push_val(ms, task->gc_root2);
        }
    }
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
static void gc_enumerate_roots(Runtime *rt, GCMarkStack *ms) {
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
        }

        /* 2–3. Deque snapshots (public + private) */
        gc__scan_deque(w->public_deque, ms);
        gc__scan_deque(w->private_deque, ms);

        /* CPS continuations capture all live state as task roots (per
         * GC_CONCURRENCY_DESIGN.md Section 7). VM stack scanning removed —
         * not thread-safe and unnecessary with CPS transform. */

        /* 4. Global environment values */
        for (uint32_t i = 0; i < w->vm.env.count; i++) {
            gc__ms_push_val(ms, w->vm.env.values[i]);
        }

        /* 5. Intern table entries (Phase 1: immortal strings) */
        if (w->vm.intern_table) {
            for (uint32_t i = 0; i < w->vm.intern_table->cap; i++) {
                if (w->vm.intern_table->entries[i]) {
                    gc__ms_push(ms, w->vm.intern_table->entries[i]);
                }
            }
        }
    }

    /* 6. Inbox tasks (external submissions awaiting pickup) */
    MUTEX_LOCK(rt->inbox_mutex);
    for (intptr_t i = 0; i < rt->inbox_count; i++) {
        RuntimeTask *task = (RuntimeTask *)rt->inbox[i];
        gc__ms_push_val(ms, task->gc_root);
        gc__ms_push_val(ms, task->gc_root2);
    }
    MUTEX_UNLOCK(rt->inbox_mutex);
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
 * Memory-ordering invariant (pairs with grey_buf_push in gc.c):
 * The acquire load of gb->count below synchronizes-with the release store
 * in grey_buf_push. This ensures visibility of:
 *   - All entry writes gb->entries[0..count-1]
 *   - The gb->entries pointer itself (which may have changed due to realloc)
 * Therefore reading gb->entries[j] for j < current is safe even if the
 * worker reallocated the buffer between our previous and current drain. */
static bool gc__drain_grey_bufs(Runtime *rt, GCMarkStack *ms,
                                 uint32_t *drained) {
    bool found_new = false;
    for (int i = 0; i < rt->num_workers; i++) {
        GreyBuffer *gb = &rt->workers[i].grey_buf;
        /* Acquire on count: synchronizes-with release in grey_buf_push.
         * Ensures we see the post-realloc entries pointer and all written
         * entries up to count. */
        uint32_t current = ATOMIC_LOAD_EXPLICIT(&gb->count, MEM_ACQUIRE);
        if (current > drained[i]) {
            found_new = true;
            for (uint32_t j = drained[i]; j < current; j++) {
                gc__ms_push_val(ms, gb->entries[j]);
            }
            drained[i] = current;
        }
    }
    return found_new;
}

static void gc_concurrent_collect(Runtime *rt) {
    int i;
    GCMarkStack ms;
    gc__ms_init(&ms);

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
                if (hdr->mark == mark) continue;
                hdr->mark = mark;
                gc__trace_object(ptr, &ms);
            }
            rounds++;
        } while (has_new && rounds < GC_FINAL_DRAIN_MAX_ROUNDS);
    }

    free(drained);

    /* 7. Set gc_active = false (disables write barriers) */
    ATOMIC_STORE_EXPLICIT(&rt->gc_active, 0, MEM_RELEASE);

    /* 8. Sweep all workers' heaps with epoch watermark.
     * Skip each worker's active allocation block (current_block).
     * Track bytes_survived for adaptive threshold adjustment.
     * Fully-empty blocks are unlinked and returned to the pool inline. */
    int nw = rt->num_workers;

    for (i = 0; i < nw; i++) {
        ThreadHeap *heap = &rt->workers[i].vm.heap;
        size_t survived = gc_sweep_concurrent(heap, heap->current_block,
                                               watermark, mark,
                                               &rt->block_pool);
        gc__adjust_threshold(heap, survived);
    }

    /* 9. Toggle current_mark on all heaps, reset allocation counters
     * and grey buffers.
     * Plain store on grey_buf.count is safe: gc_active=0 (release, step 7)
     * ensures workers have stopped calling grey_buf_push. The subsequent
     * gc_running=0 (release, step 10) publishes the reset before the next
     * cycle's gc_active=1 (release) re-enables write barriers. */
    uint8_t next_mark = 1 - mark;
    for (i = 0; i < rt->num_workers; i++) {
        rt->workers[i].vm.heap.current_mark   = next_mark;
        rt->workers[i].vm.heap.bytes_since_gc = 0;
        rt->workers[i].vm.heap.needs_gc       = false;
        rt->workers[i].grey_buf.count         = 0;
    }

    gc__ms_destroy(&ms);

    /* 10. Set gc_running = false (allow next GC cycle) */
    ATOMIC_STORE_EXPLICIT(&rt->gc_running, 0, MEM_RELEASE);
}

/* Task function for concurrent GC — submitted to the inbox */
static void gc__concurrent_task(void *data) {
    Runtime *rt = (Runtime *)data;
    gc_concurrent_collect(rt);
}

/* Trigger concurrent GC from vm.c's safepoint.
 * Attempts to CAS gc_running from 0 to 1; if successful, submits a GC task. */
static void gc_concurrent_trigger(void *runtime_ptr) {
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
static uint8_t resolve_k_code[] = {
    OP_GET_UPVALUE, 0,    /* push future (below) */
    OP_GET_LOCAL, 0,      /* push result param (top) */
    OP_RESOLVE_FUTURE,    /* pop result, pop future, resolve */
    OP_RETURN
};
static uint32_t resolve_k_lines[] = { 0, 0, 0, 0, 0, 0 };

static JaclVal runtime__create_resolve_closure(ThreadHeap *heap, arena_t *arena,
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
    cl->upvalue_count = 1;
    cl->upvalues      = (JaclVal *)(cl + 1); /* trailing array */
    cl->upvalues[0]   = future_val;
    cl->name          = "__resolve_k";
    cl->param_names   = NULL;
    cl->min_args      = 1;
    cl->variadic      = false;

    return jacl_closure(cl);
}

/* ======================================================================
 * Parallel-slot completion closure (parallel_k)
 *
 * Replaces resolve_k for CPS parallel bodies. When called with the
 * body's result, directly completes the parallel aggregation slot
 * (stores result, handles errors, increments counter, schedules join).
 * This avoids the internal-future-poll that breaks when CPS bodies
 * suspend at inner awaits.
 *
 * Bytecode:
 *   OP_GET_UPVALUE  0   ; push agg_val
 *   OP_GET_UPVALUE  1   ; push index (as i32)
 *   OP_GET_LOCAL    0   ; push result parameter
 *   OP_COMPLETE_PARALLEL ; complete slot, push nil
 *   OP_RETURN
 * ====================================================================== */

static uint8_t parallel_k_code[] = {
    OP_GET_UPVALUE, 0,    /* push agg_val */
    OP_GET_UPVALUE, 1,    /* push index */
    OP_GET_LOCAL, 0,      /* push result param */
    OP_COMPLETE_PARALLEL, /* complete parallel slot */
    OP_RETURN
};
static uint32_t parallel_k_lines[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static JaclVal runtime__create_parallel_k(ThreadHeap *heap, arena_t *arena,
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
    cl->upvalue_count = 2;
    cl->upvalues      = (JaclVal *)(cl + 1);
    cl->upvalues[0]   = agg_val;
    cl->upvalues[1]   = jacl_i32((int32_t)index);
    cl->name          = "__parallel_k";
    cl->param_names   = NULL;
    cl->min_args      = 1;
    cl->variadic      = false;

    return jacl_closure(cl);
}

/* --- Parallel slot completion logic (called from OP_COMPLETE_PARALLEL) --- */

static void runtime__complete_parallel_slot(void *runtime_ptr, VM *vm,
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
        JaclClosure *cont = jacl_as_closure(agg->continuation);
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

        runtime__schedule_continuation(rt, cont, cont_arg);
    }
}

/* ======================================================================
 * Race-slot completion closure (race_k)
 *
 * Replaces resolve_k for CPS race bodies. When called with the
 * body's result, CAS-settles the race and schedules the winner.
 *
 * Bytecode:
 *   OP_GET_UPVALUE  0   ; push agg_val
 *   OP_GET_LOCAL    0   ; push result parameter
 *   OP_COMPLETE_RACE    ; settle race, push nil
 *   OP_RETURN
 * ====================================================================== */

static uint8_t race_k_code[] = {
    OP_GET_UPVALUE, 0,    /* push agg_val */
    OP_GET_LOCAL, 0,      /* push result param */
    OP_COMPLETE_RACE,     /* settle race */
    OP_RETURN
};
static uint32_t race_k_lines[] = { 0, 0, 0, 0, 0, 0, 0 };

static JaclVal runtime__create_race_k(ThreadHeap *heap, arena_t *arena,
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
    cl->upvalue_count = 1;
    cl->upvalues      = (JaclVal *)(cl + 1);
    cl->upvalues[0]   = agg_val;
    cl->name          = "__race_k";
    cl->param_names   = NULL;
    cl->min_args      = 1;
    cl->variadic      = false;

    return jacl_closure(cl);
}

/* --- Race slot completion logic (called from OP_COMPLETE_RACE) --- */

static void runtime__complete_race_slot(void *runtime_ptr, VM *vm,
                                         JaclVal agg_val,
                                         JaclVal task_result) {
    (void)vm;
    Runtime *rt = (Runtime *)runtime_ptr;
    RaceAgg *agg = as_race_agg(agg_val);

    /* CAS settled: winner (0->1) schedules continuation, losers discard */
    uint32_t expected = 0;
    if (ATOMIC_CAS(&agg->settled, &expected, 1, MEM_ACQ_REL, MEM_RELAXED)) {
        JaclClosure *cont = jacl_as_closure(agg->continuation);
        runtime__schedule_continuation(rt, cont, task_result);
    }
}

/* ======================================================================
 * Continuation scheduling — used by OP_AWAIT and future resolution
 *
 * When a future resolves, registered waiter continuations need to be
 * scheduled as tasks so workers can execute them.
 * ====================================================================== */

typedef struct {
    JaclClosure *continuation;
    JaclVal      result;
} ContinuationTaskData;

static void runtime__continuation_task_exec(void *data) {
    ContinuationTaskData *ctd = (ContinuationTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;

    /* Set up: call continuation(result) */
    runtime__setup_call(vm, ctd->continuation, 1, &ctd->result);

    VMResult r = vm__run(vm, 0);

    /* If continuation failed, __k was not called. Forward error to __k
       to prevent completion future from hanging. __k is upvalue 0. */
    bool errored = (r != VM_OK) ||
        (vm->stack_top > 0 && jacl_is_error(vm->stack[vm->stack_top - 1]));
    if (errored && ctd->continuation->upvalue_count > 0) {
        JaclVal k_val = ctd->continuation->upvalues[0];
        if (jacl_is_closure(k_val)) {
            JaclClosure *k_cl = jacl_as_closure(k_val);
            JaclVal err;
            if (vm->stack_top > 0 && jacl_is_error(vm->stack[vm->stack_top - 1]))
                err = vm->stack[vm->stack_top - 1];
            else
                err = jacl_set_error(jacl_inline_string("error", 5));
            runtime__schedule_continuation(self->runtime, k_cl, err);
        }
    }
    free(ctd);
}

static void runtime__schedule_continuation(void *runtime_ptr,
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

    runtime__push_inbox(rt, task);
}

static void runtime__schedule_waiters(void *runtime_ptr,
                                       FutureWaiter *waiters,
                                       JaclVal result) {
    while (waiters) {
        JaclClosure *cont = jacl_as_closure(waiters->continuation);
        runtime__schedule_continuation(runtime_ptr, cont, result);
        waiters = waiters->next;
    }
}

/* ======================================================================
 * Spawn task data and execution
 * ====================================================================== */

typedef struct {
    JaclClosure *closure;
    JaclVal      future_val;
    bool         is_cps;
} SpawnTaskData;

static void runtime__spawn_task_exec(void *data) {
    SpawnTaskData *std = (SpawnTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;
    JaclClosure *cl = std->closure;
    JaclFuture *fut = jacl_as_future(std->future_val);

    if (std->is_cps) {
        /* CPS closure: create resolve_k and pass as __k argument */
        JaclVal resolve_k = runtime__create_resolve_closure(
            &vm->heap, &self->arena, std->future_val);

        runtime__setup_call(vm, cl, 1, &resolve_k);

        VMResult r = vm__run(vm, 0);
        /* For CPS, the resolve_k closure resolves the future during execution
           (via OP_RESOLVE_FUTURE which also schedules waiters).
           If vm__run returned with an error before __k was called, error the future. */
        if (r != VM_OK) {
            uint32_t state = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED);
            if (state == FUTURE_PENDING) {
                JaclVal err = jacl_set_error(jacl_inline_string("error", 5));
                FutureWaiter *waiters = jacl_future_error(fut, err,
                                  &self->grey_buf, &self->runtime->gc_active);
                runtime__schedule_waiters(self->runtime, waiters, err);
            }
        }
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

    free(std);
}

static void runtime__submit_spawn_task(void *runtime_ptr, JaclClosure *closure,
                                        JaclVal future_val, bool is_cps) {
    Runtime *rt = (Runtime *)runtime_ptr;

    SpawnTaskData *std = (SpawnTaskData *)malloc(sizeof(SpawnTaskData));
    std->closure    = closure;
    std->future_val = future_val;
    std->is_cps     = is_cps;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__spawn_task_exec;
    task->data     = std;
    task->gc_root  = future_val; /* Future is the GC root for this task */
    task->gc_root2 = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);

    runtime__push_inbox(rt, task);
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
    bool          is_cps;
} ParallelTaskData;

static void runtime__parallel_task_exec(void *data) {
    ParallelTaskData *ptd = (ParallelTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;
    JaclClosure *cl = ptd->closure;
    ParallelAgg *agg = as_parallel_agg(ptd->agg_val);

    if (ptd->is_cps) {
        /* CPS closure: create parallel_k that directly completes the slot.
           This handles both synchronous completion (body returns without
           suspending) and asynchronous completion (body suspends at an
           inner await — parallel_k runs later when the future resolves). */
        JaclVal parallel_k = runtime__create_parallel_k(
            &vm->heap, &self->arena, ptd->agg_val, ptd->index);

        runtime__setup_call(vm, cl, 1, &parallel_k);

        VMResult r = vm__run(vm, 0);

        /* If VM errored before __k was called, complete slot with error */
        if (r != VM_OK) {
            JaclVal err = jacl_set_error(jacl_inline_string("error", 5));
            runtime__complete_parallel_slot(self->runtime, vm,
                                             ptd->agg_val, ptd->index, err);
        }
        /* If VM_OK: either parallel_k already ran (synchronous completion)
           or the body suspended and parallel_k will run later. Either way,
           the slot completion is handled by parallel_k. */
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

    free(ptd);
}

static void runtime__submit_parallel_task(void *runtime_ptr,
                                           JaclClosure *closure,
                                           JaclVal agg_val,
                                           uint32_t index,
                                           bool is_cps) {
    Runtime *rt = (Runtime *)runtime_ptr;

    ParallelTaskData *ptd = (ParallelTaskData *)malloc(sizeof(ParallelTaskData));
    ptd->closure  = closure;
    ptd->agg_val  = agg_val;
    ptd->index    = index;
    ptd->is_cps   = is_cps;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__parallel_task_exec;
    task->data     = ptd;
    task->gc_root  = agg_val; /* Aggregate is the GC root — traces continuation + results */
    task->gc_root2 = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);

    runtime__push_inbox(rt, task);
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
    bool          is_cps;
} RaceTaskData;

static void runtime__race_task_exec(void *data) {
    RaceTaskData *rtd = (RaceTaskData *)data;
    WorkerThread *self = rt__current_worker;
    VM *vm = &self->vm;
    JaclClosure *cl = rtd->closure;
    RaceAgg *agg = as_race_agg(rtd->agg_val);

    if (rtd->is_cps) {
        /* CPS closure: create race_k that directly settles the race.
           Handles both synchronous and asynchronous (suspend) completion. */
        JaclVal race_k = runtime__create_race_k(
            &vm->heap, &self->arena, rtd->agg_val);

        runtime__setup_call(vm, cl, 1, &race_k);

        VMResult r = vm__run(vm, 0);

        /* If VM errored before __k was called, try to settle with error */
        if (r != VM_OK) {
            JaclVal err = jacl_set_error(jacl_inline_string("error", 5));
            runtime__complete_race_slot(self->runtime, vm,
                                         rtd->agg_val, err);
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

        /* CAS settled: winner (0->1) schedules continuation, losers discard */
        uint32_t expected = 0;
        if (ATOMIC_CAS(&agg->settled, &expected, 1,
                        MEM_ACQ_REL, MEM_RELAXED)) {
            JaclClosure *cont = jacl_as_closure(agg->continuation);
            runtime__schedule_continuation(self->runtime, cont, task_result);
        }
    }

    free(rtd);
}

static void runtime__submit_race_task(void *runtime_ptr,
                                       JaclClosure *closure,
                                       JaclVal agg_val,
                                       bool is_cps) {
    Runtime *rt = (Runtime *)runtime_ptr;

    RaceTaskData *rtd = (RaceTaskData *)malloc(sizeof(RaceTaskData));
    rtd->closure  = closure;
    rtd->agg_val  = agg_val;
    rtd->is_cps   = is_cps;

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = runtime__race_task_exec;
    task->data     = rtd;
    task->gc_root  = agg_val;
    task->gc_root2 = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)closure & JACL_PAYLOAD_MASK);

    runtime__push_inbox(rt, task);
}

/* ======================================================================
 * rt_run_to_completion: submit a CPS closure as a task and block until
 * its completion future resolves.
 * ====================================================================== */

static VMResult rt_run_to_completion(Runtime *rt, JaclClosure *closure,
                                      arena_t *arena) {
    /* Create a completion future on worker 0's heap */
    JaclVal completion = jacl_future(&rt->workers[0].vm.heap);
    JaclFuture *cfut = jacl_as_future(completion);

    bool is_cps = (closure->param_count == 1);
    runtime__submit_spawn_task(rt, closure, completion, is_cps);

    /* Block until the completion future resolves (spin/yield) */
    for (;;) {
        uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_ACQUIRE);
        if (state == FUTURE_RESOLVED || state == FUTURE_ERROR) break;
        SLEEP_MILLISECONDS(1);
    }

    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_ERROR) {
        return VM_RUNTIME_ERROR;
    }
    return VM_OK;

    (void)arena;
}

#endif /* RUNTIME_C */
