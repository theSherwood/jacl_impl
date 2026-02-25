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

/* ======================================================================
 * GreyBuffer: per-thread append-only buffer for write barrier entries.
 * Thread-local writes only (no contention). GC reads a snapshot of count
 * during mark phase draining.
 * ====================================================================== */

#define GREY_BUF_INIT_CAP 256

typedef struct {
    JaclVal  *entries;
    uint32_t  count;
    uint32_t  cap;
} GreyBuffer;

static void grey_buf_init(GreyBuffer *gb) {
    gb->entries = (JaclVal *)malloc(GREY_BUF_INIT_CAP * sizeof(JaclVal));
    gb->count   = 0;
    gb->cap     = GREY_BUF_INIT_CAP;
}

static void grey_buf_push(GreyBuffer *gb, JaclVal v) {
    if (gb->count >= gb->cap) {
        uint32_t new_cap = gb->cap * 2;
        gb->entries = (JaclVal *)realloc(gb->entries,
                                          (size_t)new_cap * sizeof(JaclVal));
        gb->cap = new_cap;
    }
    gb->entries[gb->count++] = v;
}

static void grey_buf_destroy(GreyBuffer *gb) {
    free(gb->entries);
    gb->entries = NULL;
    gb->count   = 0;
    gb->cap     = 0;
}

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

/* Thread-local worker ID (set in worker loop, -1 for non-worker threads) */
static __thread int rt__worker_id = -1;

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

        /* 1. Check global inbox */
        MUTEX_LOCK(rt->inbox_mutex);
        if (rt->inbox_count > 0) {
            task_val = rt->inbox[--rt->inbox_count];
            found = true;
        }
        MUTEX_UNLOCK(rt->inbox_mutex);

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

            /* Set thread-local heap for collection template allocations */
            gc__current_heap = &self->vm.heap;

            /* Execute the task */
            task->fn(task->data);

            /* Free the task envelope */
            free(task);

            /* Mark idle */
            ATOMIC_STORE_EXPLICIT(&self->currently_executing, WORKER_IDLE,
                                  MEM_RELEASE);
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

    /* Cleanup each worker */
    for (i = 0; i < rt->num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        uintptr_t task_val;

        /* Drain any remaining tasks from deques */
        while (rt_deque_deque_take(w->public_deque, &task_val))
            free((RuntimeTask *)task_val);
        while (rt_deque_deque_take(w->private_deque, &task_val))
            free((RuntimeTask *)task_val);

        /* Unregister thieves */
        for (j = 0; j < rt->num_workers; j++) {
            if (j != i && w->steal_ids[j] >= 0)
                rt_deque_deque_unregister_thief(
                    rt->workers[j].public_deque, w->steal_ids[j]);
        }
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
 * Task submission — external callers push to the global inbox
 * ====================================================================== */

static void runtime_submit(Runtime *rt, void (*fn)(void *), void *data) {
    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn   = fn;
    task->data = data;

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
 * Closure task submission (stub — full execution in US-011)
 * ====================================================================== */

static void runtime__exec_closure(void *data) {
    /* Placeholder: closure execution on worker VMs requires concurrent GC */
    (void)data;
}

static void runtime_submit_task(Runtime *rt, JaclClosure *closure,
                                 bool thread_local) {
    (void)thread_local;
    runtime_submit(rt, runtime__exec_closure, closure);
}

#endif /* RUNTIME_C */
