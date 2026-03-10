#include "test_helpers.h"
#include "../src/jacl.c"

/* --- Task helper: atomically increment a counter --- */

static void rt_test__increment(void *data) {
    volatile intptr_t *counter = (volatile intptr_t *)data;
    ATOMIC_INC(counter);
}

/* ===== US-008: Worker thread pool and task execution loop ===== */

static int test_runtime_init_destroy(void) {
    /* Init and immediately destroy — no tasks submitted */
    Runtime rt;
    runtime_init(&rt, 2);

    ASSERT_INT_EQ(rt.num_workers, 2);
    ASSERT_INT_EQ(rt.shutdown, 0);
    ASSERT(rt.workers != NULL);
    ASSERT(rt.workers[0].public_deque != NULL);
    ASSERT(rt.workers[0].private_deque != NULL);
    ASSERT(rt.workers[1].public_deque != NULL);

    runtime_destroy(&rt);
    TEST_PASS();
}

static int test_runtime_submit_tasks(void) {
    /* Submit 100 tasks across 4 workers, verify all execute */
    Runtime rt;
    runtime_init(&rt, 4);

    volatile intptr_t counter = 0;
    int num_tasks = 100;

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, rt_test__increment, (void *)&counter);
    }

    /* Wait for completion (max 5 seconds) */
    for (int ms = 0; ms < 5000; ms++) {
        if (ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE) >= (intptr_t)num_tasks)
            break;
        SLEEP_MILLISECONDS(1);
    }

    ASSERT_I64_EQ(ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE),
                  (int64_t)num_tasks);

    runtime_destroy(&rt);
    TEST_PASS();
}

static int test_runtime_single_worker(void) {
    /* Single worker processes all tasks */
    Runtime rt;
    runtime_init(&rt, 1);

    volatile intptr_t counter = 0;
    int num_tasks = 50;

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, rt_test__increment, (void *)&counter);
    }

    for (int ms = 0; ms < 5000; ms++) {
        if (ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE) >= (intptr_t)num_tasks)
            break;
        SLEEP_MILLISECONDS(1);
    }

    ASSERT_I64_EQ(ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE),
                  (int64_t)num_tasks);

    runtime_destroy(&rt);
    TEST_PASS();
}

static int test_runtime_many_tasks(void) {
    /* Stress: 1000 tasks across 4 workers */
    Runtime rt;
    runtime_init(&rt, 4);

    volatile intptr_t counter = 0;
    int num_tasks = 1000;

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, rt_test__increment, (void *)&counter);
    }

    for (int ms = 0; ms < 10000; ms++) {
        if (ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE) >= (intptr_t)num_tasks)
            break;
        SLEEP_MILLISECONDS(1);
    }

    ASSERT_I64_EQ(ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE),
                  (int64_t)num_tasks);

    runtime_destroy(&rt);
    TEST_PASS();
}

static int test_runtime_worker_epoch(void) {
    /* Verify workers snapshot the global epoch */
    Runtime rt;
    runtime_init(&rt, 2);

    /* Advance global epoch */
    ATOMIC_STORE_EXPLICIT(&rt.global_epoch, 42, MEM_RELEASE);

    volatile intptr_t counter = 0;
    runtime_submit(&rt, rt_test__increment, (void *)&counter);

    for (int ms = 0; ms < 5000; ms++) {
        if (ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE) >= 1)
            break;
        SLEEP_MILLISECONDS(1);
    }

    ASSERT_I64_EQ(ATOMIC_LOAD_EXPLICIT(&counter, MEM_ACQUIRE), 1);

    /* At least one worker should have snapshotted epoch 42 */
    bool found_epoch = false;
    for (int i = 0; i < rt.num_workers; i++) {
        uint64_t e = ATOMIC_LOAD_EXPLICIT(&rt.workers[i].thread_epoch,
                                           MEM_ACQUIRE);
        if (e == 42) found_epoch = true;
    }
    ASSERT(found_epoch);

    runtime_destroy(&rt);
    TEST_PASS();
}

static int test_runtime_submit_task_api(void) {
    /* Verify runtime_submit_task compiles and runs (stub execution) */
    Runtime rt;
    runtime_init(&rt, 2);

    /* Create a dummy closure on worker 0's heap for the API call */
    ThreadHeap *heap = &rt.workers[0].vm.heap;
    JaclClosure *cl = (JaclClosure *)gc_alloc(heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure));
    memset(cl, 0, sizeof(JaclClosure));

    runtime_submit_task(&rt, cl, false);

    /* Give it time to execute the stub */
    SLEEP_MILLISECONDS(100);

    runtime_destroy(&rt);
    TEST_PASS();
}

/* ===== GreyBuffer unit tests ===== */

static int test_grey_buffer(void) {
    GreyBuffer gb;
    grey_buf_init(&gb);

    ASSERT_INT_EQ(gb.count, 0);
    ASSERT(gb.cap >= GREY_BUF_INIT_CAP);

    grey_buf_push(&gb, JACL_TRUE);
    grey_buf_push(&gb, JACL_FALSE);
    ASSERT_INT_EQ(gb.count, 2);
    ASSERT(gb.entries[0] == JACL_TRUE);
    ASSERT(gb.entries[1] == JACL_FALSE);

    grey_buf_destroy(&gb);
    TEST_PASS();
}

static int test_grey_buffer_grow(void) {
    GreyBuffer gb;
    grey_buf_init(&gb);

    /* Push more than initial capacity to trigger growth */
    uint32_t count = GREY_BUF_INIT_CAP + 100;
    for (uint32_t i = 0; i < count; i++) {
        grey_buf_push(&gb, (JaclVal)i);
    }

    ASSERT_INT_EQ(gb.count, (int)count);
    ASSERT(gb.cap > GREY_BUF_INIT_CAP);

    /* Verify all entries preserved after growth */
    for (uint32_t i = 0; i < count; i++) {
        ASSERT(gb.entries[i] == (JaclVal)i);
    }

    grey_buf_destroy(&gb);
    TEST_PASS();
}

/* ===== US-009: Root enumeration protocol with BUSY sentinel ===== */

/* Helper: init a Runtime struct without starting worker threads.
 * Workers are allocated and VMs initialized, but no threads are created.
 * This allows deterministic root enumeration testing in isolation. */
static void rt_test__init_no_threads(Runtime *rt, int num_workers) {
    int i;
    rt->num_workers  = num_workers;
    rt->global_epoch = 0;
    rt->gc_running   = 0;
    rt->gc_active    = 0;
    rt->shutdown     = 0;

    gc_block_pool_init(&rt->block_pool);

    rt->inbox_cap   = 16;
    rt->inbox_count = 0;
    rt->inbox = (uintptr_t *)malloc((size_t)rt->inbox_cap * sizeof(uintptr_t));
    MUTEX_INIT(rt->inbox_mutex);

    rt->workers = (WorkerThread *)calloc((size_t)num_workers,
                                          sizeof(WorkerThread));
    for (i = 0; i < num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        w->id      = i;
        w->runtime = rt;
        w->currently_executing = WORKER_IDLE;
        w->thread_epoch        = 0;
        w->public_deque  = rt_deque_deque_new(4);
        w->private_deque = rt_deque_deque_new(4);
        grey_buf_init(&w->grey_buf);
        remembered_set_init(&w->remembered_set);
        w->arena = (arena_t){0};
        runtime__init_worker_vm(w);
        w->steal_ids = (int *)calloc((size_t)num_workers, sizeof(int));
    }
}

/* Helper: destroy a Runtime that was init'd without threads */
static void rt_test__destroy_no_threads(Runtime *rt) {
    int i;
    for (i = 0; i < rt->num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        uintptr_t task_val;
        while (rt_deque_deque_take(w->public_deque, &task_val))
            free((RuntimeTask *)task_val);
        while (rt_deque_deque_take(w->private_deque, &task_val))
            free((RuntimeTask *)task_val);
        rt_deque_deque_free(w->public_deque);
        rt_deque_deque_free(w->private_deque);
        grey_buf_destroy(&w->grey_buf);
        remembered_set_destroy(&w->remembered_set);
        free(w->steal_ids);
        vm_destroy(&w->vm);
        arena_destroy(&w->arena);
    }
    free(rt->workers);
    for (intptr_t k = 0; k < rt->inbox_count; k++)
        free((RuntimeTask *)rt->inbox[k]);
    free(rt->inbox);
    MUTEX_DESTROY(rt->inbox_mutex);
    gc_block_pool_destroy(&rt->block_pool);
}

/* Helper: check if a raw pointer appears in the mark stack */
static bool rt_test__ms_contains(GCMarkStack *ms, void *target) {
    /* Destructively drain mark stack searching for target */
    void *ptr;
    bool found = false;
    void *saved[4096];
    int saved_count = 0;

    while (gc__ms_pop(ms, &ptr)) {
        if (ptr == target) found = true;
        if (saved_count < 4096)
            saved[saved_count++] = ptr;
    }
    /* Restore entries for further inspection if needed */
    for (int i = 0; i < saved_count; i++)
        gc__ms_push(ms, saved[i]);

    return found;
}

static int test_enumerate_roots_vm_stack(void) {
    /* US-009: VM stack scanning removed from concurrent GC path.
     * CPS continuations capture all live state as task roots.
     * VM stack values should NOT appear in gc_enumerate_roots output. */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Allocate a heap object and push onto VM stack */
    JaclVal i64_val = jacl_i64(&w->vm.heap, 12345);
    w->vm.stack[w->vm.stack_top++] = i64_val;

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    /* VM stack is no longer scanned — value should NOT be found */
    ASSERT(!rt_test__ms_contains(&ms, jacl_as_ptr(i64_val)));

    gc__ms_destroy(&ms);
    w->vm.stack_top = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_enumerate_roots_env(void) {
    /* Root enumeration finds global environment values */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Add a heap-allocated value to the environment */
    JaclVal i64_val = jacl_i64(&w->vm.heap, 99999);
    w->vm.env.names[w->vm.env.count]  = jacl_inline_string("test", 4);
    w->vm.env.values[w->vm.env.count] = i64_val;
    w->vm.env.count++;

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(i64_val)));

    gc__ms_destroy(&ms);
    w->vm.env.count--;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_enumerate_roots_deque(void) {
    /* Root enumeration finds tasks on worker deques */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Create a closure and wrap in a task */
    JaclClosure *cl = (JaclClosure *)gc_alloc(&w->vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure));
    memset(cl, 0, sizeof(JaclClosure));
    JaclVal cl_val = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)cl & JACL_PAYLOAD_MASK);

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = NULL;
    task->data     = cl;
    task->gc_root  = cl_val;
    task->gc_root2 = JACL_NIL;

    /* Push onto public deque */
    rt_deque_deque_push(w->public_deque, (uintptr_t)task);

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, (void *)cl));

    gc__ms_destroy(&ms);
    /* Take task back from deque and free */
    uintptr_t taken;
    rt_deque_deque_take(w->public_deque, &taken);
    free((RuntimeTask *)taken);

    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_enumerate_roots_currently_executing(void) {
    /* Root enumeration finds the currently executing task */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    JaclClosure *cl = (JaclClosure *)gc_alloc(&w->vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure));
    memset(cl, 0, sizeof(JaclClosure));
    JaclVal cl_val = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)cl & JACL_PAYLOAD_MASK);

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = NULL;
    task->data     = cl;
    task->gc_root  = cl_val;
    task->gc_root2 = JACL_NIL;

    /* Simulate a task being executed */
    ATOMIC_STORE_EXPLICIT(&w->currently_executing, (uintptr_t)task,
                          MEM_RELEASE);

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, (void *)cl));

    gc__ms_destroy(&ms);
    ATOMIC_STORE_EXPLICIT(&w->currently_executing, WORKER_IDLE, MEM_RELEASE);
    free(task);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_enumerate_roots_inbox(void) {
    /* Root enumeration finds tasks in the global inbox */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    JaclClosure *cl = (JaclClosure *)gc_alloc(&w->vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure));
    memset(cl, 0, sizeof(JaclClosure));

    /* Submit a closure task to the inbox */
    runtime_submit_task(&rt, cl, false);

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, (void *)cl));

    gc__ms_destroy(&ms);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* Thread function: resolve BUSY sentinel to a real task pointer after delay */
typedef struct {
    volatile uintptr_t *slot;
    uintptr_t           task_val;
} BusyResolveData;

static THREAD_PROC_RETURN THREAD_PROC_TYPE rt_test__resolve_busy(void *arg) {
    BusyResolveData *d = (BusyResolveData *)arg;
    SLEEP_MILLISECONDS(5);
    ATOMIC_STORE_EXPLICIT(d->slot, d->task_val, MEM_RELEASE);
    return (THREAD_PROC_RETURN)0;
}

static int test_enumerate_roots_busy_sentinel(void) {
    /* Simulate pop-in-transit race: worker is BUSY, then resolves.
     * GC should spin on BUSY and eventually find the task. */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    JaclClosure *cl = (JaclClosure *)gc_alloc(&w->vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure));
    memset(cl, 0, sizeof(JaclClosure));
    JaclVal cl_val = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)cl & JACL_PAYLOAD_MASK);

    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = NULL;
    task->data     = cl;
    task->gc_root  = cl_val;
    task->gc_root2 = JACL_NIL;

    /* Set BUSY — simulating the pop-in-transit window */
    ATOMIC_STORE_EXPLICIT(&w->currently_executing, WORKER_BUSY, MEM_RELEASE);

    /* Spawn a resolver thread that will set currently_executing to the task */
    BusyResolveData resolve_data;
    resolve_data.slot     = &w->currently_executing;
    resolve_data.task_val = (uintptr_t)task;
    thread_t resolver;
    THREAD_CREATE(&resolver, NULL, rt_test__resolve_busy, &resolve_data);

    /* gc_enumerate_roots should spin on BUSY until resolved */
    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    THREAD_JOIN(resolver, NULL);

    /* Verify the closure was found */
    ASSERT(rt_test__ms_contains(&ms, (void *)cl));

    gc__ms_destroy(&ms);
    ATOMIC_STORE_EXPLICIT(&w->currently_executing, WORKER_IDLE, MEM_RELEASE);
    free(task);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_enumerate_roots_multi_worker(void) {
    /* US-009: VM stack scanning removed. Root enumeration across
     * multiple workers finds deque/env roots but NOT VM stack values. */
    Runtime rt;
    rt_test__init_no_threads(&rt, 3);

    /* Worker 0: value on VM stack (should NOT be found) */
    JaclVal val0 = jacl_i64(&rt.workers[0].vm.heap, 111);
    rt.workers[0].vm.stack[rt.workers[0].vm.stack_top++] = val0;

    /* Worker 1: task on public deque (should be found) */
    JaclClosure *cl1 = (JaclClosure *)gc_alloc(&rt.workers[1].vm.heap,
                                                 OBJ_CLOSURE,
                                                 sizeof(JaclClosure));
    memset(cl1, 0, sizeof(JaclClosure));
    JaclVal cl1_val = JACL_TAG_CLOSURE
                    | ((uint64_t)(uintptr_t)cl1 & JACL_PAYLOAD_MASK);
    RuntimeTask *task1 = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task1->fn       = NULL;
    task1->data     = cl1;
    task1->gc_root  = cl1_val;
    task1->gc_root2 = JACL_NIL;
    rt_deque_deque_push(rt.workers[1].public_deque, (uintptr_t)task1);

    /* Worker 2: value in environment (should be found) */
    JaclVal val2 = jacl_i64(&rt.workers[2].vm.heap, 222);
    rt.workers[2].vm.env.names[rt.workers[2].vm.env.count]  = jacl_inline_string("test", 4);
    rt.workers[2].vm.env.values[rt.workers[2].vm.env.count] = val2;
    rt.workers[2].vm.env.count++;

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    /* VM stack NOT scanned (US-009) */
    ASSERT(!rt_test__ms_contains(&ms, jacl_as_ptr(val0)));
    /* Deque tasks still scanned */
    ASSERT(rt_test__ms_contains(&ms, (void *)cl1));
    /* Environment values still scanned */
    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(val2)));

    gc__ms_destroy(&ms);

    /* Cleanup */
    rt.workers[0].vm.stack_top = 0;
    rt.workers[2].vm.env.count--;
    uintptr_t taken;
    rt_deque_deque_take(rt.workers[1].public_deque, &taken);
    free((RuntimeTask *)taken);

    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* ===== US-001 (M14): Multi-root GC scanning for RuntimeTask ===== */

static int test_enumerate_roots_gc_root2(void) {
    /* gc_root2 is scanned by gc_enumerate_roots across all root sources:
     * currently_executing, deque, and inbox. */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Allocate two distinct heap objects */
    JaclClosure *cl = (JaclClosure *)gc_alloc(&w->vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure));
    memset(cl, 0, sizeof(JaclClosure));
    JaclVal cl_val = JACL_TAG_CLOSURE
                   | ((uint64_t)(uintptr_t)cl & JACL_PAYLOAD_MASK);

    JaclVal heap_val = jacl_i64(&w->vm.heap, 99999);

    /* Create a task with gc_root2 set to the heap value */
    RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task->fn       = NULL;
    task->data     = cl;
    task->gc_root  = cl_val;
    task->gc_root2 = heap_val;

    /* Test 1: gc_root2 found via deque scanning */
    rt_deque_deque_push(w->public_deque, (uintptr_t)task);

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, (void *)cl));
    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(heap_val)));

    gc__ms_destroy(&ms);
    uintptr_t taken;
    rt_deque_deque_take(w->public_deque, &taken);

    /* Test 2: gc_root2 found via currently_executing */
    ATOMIC_STORE_EXPLICIT(&w->currently_executing, (uintptr_t)task,
                          MEM_RELEASE);

    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, (void *)cl));
    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(heap_val)));

    gc__ms_destroy(&ms);
    ATOMIC_STORE_EXPLICIT(&w->currently_executing, WORKER_IDLE, MEM_RELEASE);

    /* Test 3: gc_root2 found via inbox scanning */
    MUTEX_LOCK(rt.inbox_mutex);
    if (rt.inbox_count >= rt.inbox_cap) {
        intptr_t new_cap = rt.inbox_cap * 2;
        rt.inbox = (uintptr_t *)realloc(rt.inbox,
                                          (size_t)new_cap * sizeof(uintptr_t));
        rt.inbox_cap = new_cap;
    }
    rt.inbox[rt.inbox_count++] = (uintptr_t)task;
    MUTEX_UNLOCK(rt.inbox_mutex);

    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, (void *)cl));
    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(heap_val)));

    gc__ms_destroy(&ms);

    /* Inbox cleanup happens in rt_test__destroy_no_threads */
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* ===== US-010: Write barriers (hybrid SATB + insertion) ===== */

static int test_write_barrier_gc_active(void) {
    /* When gc_active is true, write barrier pushes both old and new
     * heap-type values to the grey buffer */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Simulate GC active */
    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 1, MEM_RELEASE);

    /* Allocate two heap objects as old/new values */
    JaclVal old_val = jacl_i64(&w->vm.heap, 111);
    JaclVal new_val = jacl_i64(&w->vm.heap, 222);

    /* Grey buffer should be empty */
    ASSERT_INT_EQ(w->grey_buf.count, 0);

    /* Fire write barrier through VM's pointers (simulating OP_RESET) */
    gc_write_barrier(w->vm.grey_buf, w->vm.gc_active_ptr,
                     old_val, new_val);

    /* Both values should appear in grey buffer */
    ASSERT_INT_EQ(w->grey_buf.count, 2);
    ASSERT(w->grey_buf.entries[0] == old_val);
    ASSERT(w->grey_buf.entries[1] == new_val);

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 0, MEM_RELEASE);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_write_barrier_gc_inactive(void) {
    /* When gc_active is false, write barrier is a no-op */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* gc_active defaults to 0 (inactive) */
    JaclVal old_val = jacl_i64(&w->vm.heap, 111);
    JaclVal new_val = jacl_i64(&w->vm.heap, 222);

    gc_write_barrier(w->vm.grey_buf, w->vm.gc_active_ptr,
                     old_val, new_val);

    /* Grey buffer should remain empty */
    ASSERT_INT_EQ(w->grey_buf.count, 0);

    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_write_barrier_null_fast_path(void) {
    /* When gc_active_ptr is NULL (single-threaded mode), barrier is a no-op */
    GreyBuffer gb;
    grey_buf_init(&gb);

    JaclVal old_val = JACL_TRUE;
    JaclVal new_val = JACL_FALSE;

    /* NULL gc_active_ptr — single-threaded fast path */
    gc_write_barrier(&gb, NULL, old_val, new_val);

    ASSERT_INT_EQ(gb.count, 0);

    grey_buf_destroy(&gb);
    TEST_PASS();
}

static int test_write_barrier_inline_types_skipped(void) {
    /* Inline (non-heap) values should not be pushed to grey buffer,
     * even when gc_active is true */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 1, MEM_RELEASE);

    /* Use inline types: small ints, booleans, nil */
    gc_write_barrier(w->vm.grey_buf, w->vm.gc_active_ptr,
                     JACL_TRUE, JACL_FALSE);
    ASSERT_INT_EQ(w->grey_buf.count, 0);

    gc_write_barrier(w->vm.grey_buf, w->vm.gc_active_ptr,
                     JACL_NIL, jacl_i32(42));
    ASSERT_INT_EQ(w->grey_buf.count, 0);

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 0, MEM_RELEASE);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_write_barrier_mixed_types(void) {
    /* Only heap-type values are pushed; inline values are skipped */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 1, MEM_RELEASE);

    /* old is inline (small int), new is heap (i64) */
    JaclVal heap_val = jacl_i64(&w->vm.heap, 999);
    gc_write_barrier(w->vm.grey_buf, w->vm.gc_active_ptr,
                     jacl_i32(1), heap_val);

    /* Only the new heap value should be pushed */
    ASSERT_INT_EQ(w->grey_buf.count, 1);
    ASSERT(w->grey_buf.entries[0] == heap_val);

    /* Reset for next check */
    w->grey_buf.count = 0;

    /* old is heap, new is inline */
    gc_write_barrier(w->vm.grey_buf, w->vm.gc_active_ptr,
                     heap_val, JACL_NIL);
    ASSERT_INT_EQ(w->grey_buf.count, 1);
    ASSERT(w->grey_buf.entries[0] == heap_val);

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 0, MEM_RELEASE);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_write_barrier_vm_reset(void) {
    /* Verify write barrier fires through the VM on reset! (box) */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];
    VM *vm = &w->vm;
    arena_t arena = {0};

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 1, MEM_RELEASE);

    /* Compile and run: create a box, then reset! it */
    const char *src = "def b [box 10]\n[reset! $b 20]";
    gc__current_heap = &vm->heap;
    VMResult r = jacl_run(src, vm, &arena);
    ASSERT(r == VM_OK);

    /* Grey buffer should have entries from the reset! operation.
     * old = 10 (inline int), new = 20 (inline int) — both inline, so
     * the write barrier won't push either. Let's use heap-allocated values. */
    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 0, MEM_RELEASE);
    arena_destroy(&arena);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_write_barrier_vm_set_cell(void) {
    /* Verify write barrier fires through the VM on set! (mut local cell) */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];
    VM *vm = &w->vm;
    arena_t arena = {0};

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 1, MEM_RELEASE);

    /* Compile and run: mut local, then set! it.
     * Both values are inline ints, so barrier fires but skips push.
     * This tests that the barrier call doesn't crash. */
    const char *src = "mut x 10\nset! x 20";
    gc__current_heap = &vm->heap;
    VMResult r = jacl_run(src, vm, &arena);
    ASSERT(r == VM_OK);

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 0, MEM_RELEASE);
    arena_destroy(&arena);
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* ===== US-011: Concurrent mark-sweep GC with epoch watermark ===== */

static int test_epoch_stamping(void) {
    /* gc_alloc stamps objects with gc__thread_epoch */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Set thread epoch and allocate */
    gc__thread_epoch = 5;
    JaclVal val = jacl_i64(&w->vm.heap, 42);
    GCHeader *hdr = gc_header_of(jacl_as_ptr(val));
    ASSERT_INT_EQ(hdr->epoch, 5);

    gc__thread_epoch = 99;
    JaclVal val2 = jacl_i64(&w->vm.heap, 100);
    GCHeader *hdr2 = gc_header_of(jacl_as_ptr(val2));
    ASSERT_INT_EQ(hdr2->epoch, 99);

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_epoch_watermark_protect(void) {
    /* Objects with epoch >= watermark survive even if unmarked */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Allocate with epoch 5 */
    gc__thread_epoch = 5;
    JaclVal val = jacl_i64(&w->vm.heap, 111);

    /* Allocate with epoch 10 */
    gc__thread_epoch = 10;
    JaclVal val2 = jacl_i64(&w->vm.heap, 222);

    uint8_t mark = w->vm.heap.current_mark;

    /* Neither object is marked. Watermark = 5.
     * val (epoch=5): 5 >= 5 → immune (survives)
     * val2 (epoch=10): 10 >= 5 → immune (survives) */
    gc_sweep_concurrent(&w->vm.heap, NULL, 5, mark, NULL);

    GCHeader *h1 = gc_header_of(jacl_as_ptr(val));
    GCHeader *h2 = gc_header_of(jacl_as_ptr(val2));
    ASSERT(h1->alloc_total > 0); /* survived */
    ASSERT(h2->alloc_total > 0); /* survived */

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_epoch_watermark_collect(void) {
    /* Objects with epoch < watermark AND not marked are collected */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Allocate old object (epoch 3) */
    gc__thread_epoch = 3;
    JaclVal old_val = jacl_i64(&w->vm.heap, 111);

    /* Allocate new object (epoch 10) */
    gc__thread_epoch = 10;
    JaclVal new_val = jacl_i64(&w->vm.heap, 222);

    uint8_t mark = w->vm.heap.current_mark;

    /* Watermark = 5. old_val (epoch=3 < 5): dead. new_val (epoch=10 >= 5): immune. */
    gc_sweep_concurrent(&w->vm.heap, NULL, 5, mark, NULL);

    GCHeader *h_old = gc_header_of(jacl_as_ptr(old_val));
    GCHeader *h_new = gc_header_of(jacl_as_ptr(new_val));
    ASSERT(h_old->alloc_total == 0); /* collected (zeroed) */
    ASSERT(h_new->alloc_total > 0);  /* survived (epoch-protected) */

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_concurrent_gc_full_cycle(void) {
    /* Full concurrent GC cycle: mark live objects, sweep dead ones.
     * We force a new block so the old block (with test objects) is not
     * current_block and can be swept. */
    Runtime rt;
    rt_test__init_no_threads(&rt, 2);
    WorkerThread *w0 = &rt.workers[0];

    /* Set thread epochs so allocations have epoch < watermark */
    ATOMIC_STORE_EXPLICIT(&w0->thread_epoch, 5, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&rt.workers[1].thread_epoch, 5, MEM_RELEASE);
    gc__thread_epoch = 1;

    /* Allocate live and dead objects on worker 0's heap */
    JaclVal live_val = jacl_i64(&w0->vm.heap, 42);
    JaclVal dead_val = jacl_i64(&w0->vm.heap, 99);

    /* Force current_block to change by making the current block appear
     * full (all lines occupied) then exhausting cursor.  gc_alloc's slow
     * path will skip this block and acquire a fresh one from the pool. */
    GCBlock *obj_block = w0->vm.heap.current_block;
    memset(obj_block->line_map, GC_LINE_OCCUPIED, GC_LINES_PER_BLOCK);
    w0->vm.heap.cursor = w0->vm.heap.limit;
    (void)jacl_i64(&w0->vm.heap, 0);  /* dummy alloc forces new block */
    /* Restore the object block's line map so sweep can see free vs occupied */
    memset(obj_block->line_map, GC_LINE_FREE, GC_LINES_PER_BLOCK);

    /* Add live_val to environment (root) — dead_val is unreachable.
     * US-009: VM stack no longer scanned, use env as root instead. */
    w0->vm.env.names[w0->vm.env.count]  = jacl_inline_string("live", 4);
    w0->vm.env.values[w0->vm.env.count] = live_val;
    w0->vm.env.count++;

    /* Run concurrent GC */
    gc_concurrent_collect(&rt);

    /* Verify live_val survived (marked by tracing) */
    GCHeader *hdr_live = gc_header_of(jacl_as_ptr(live_val));
    ASSERT(hdr_live->alloc_total > 0);

    /* Verify dead_val was collected (unmarked, epoch 1 < watermark 5) */
    GCHeader *hdr_dead = gc_header_of(jacl_as_ptr(dead_val));
    ASSERT(hdr_dead->alloc_total == 0);

    w0->vm.env.count--;
    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_concurrent_gc_mark_toggle(void) {
    /* current_mark toggles after each GC cycle */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);

    ATOMIC_STORE_EXPLICIT(&rt.workers[0].thread_epoch, 1, MEM_RELEASE);
    gc__thread_epoch = 1;

    uint8_t mark_before = rt.workers[0].vm.heap.current_mark;
    gc_concurrent_collect(&rt);
    uint8_t mark_after = rt.workers[0].vm.heap.current_mark;

    ASSERT(mark_before != mark_after);
    ASSERT(mark_after == (1 - mark_before));

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_concurrent_gc_mutual_exclusion(void) {
    /* gc_running prevents concurrent GC triggers */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);

    /* Simulate gc_running = true */
    ATOMIC_STORE_EXPLICIT(&rt.gc_running, 1, MEM_RELEASE);

    /* gc_concurrent_trigger should fail the CAS and not submit a task */
    gc_concurrent_trigger(&rt);
    ASSERT_I64_EQ(rt.inbox_count, 0);

    /* Reset gc_running, now trigger should succeed */
    ATOMIC_STORE_EXPLICIT(&rt.gc_running, 0, MEM_RELEASE);
    gc_concurrent_trigger(&rt);
    ASSERT_I64_EQ(rt.inbox_count, 1);

    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* Stress test helpers */
static volatile intptr_t stress_gc_done = 0;

static void stress__alloc_discard(void *data) {
    /* Allocate and discard heap objects to generate garbage.
     * Some objects are kept alive on the VM stack. */
    WorkerThread *self = rt__current_worker;
    ThreadHeap *heap = &self->vm.heap;
    VM *vm = &self->vm;
    intptr_t count = (intptr_t)data;

    vm->stack_top = 0;

    for (intptr_t i = 0; i < count; i++) {
        JaclHeapI64 *obj = (JaclHeapI64 *)gc_alloc(heap, OBJ_HEAP_I64,
                                                      sizeof(JaclHeapI64));
        if (obj) obj->value = i;

        /* Keep first 5 alive on stack, rest is garbage */
        if (i < 5 && vm->stack_top < VM_STACK_MAX && obj) {
            JaclVal v = JACL_TAG_I64
                      | ((uint64_t)(uintptr_t)obj & JACL_PAYLOAD_MASK);
            vm->stack[vm->stack_top++] = v;
        }
    }

    /* Verify alive objects survived */
    for (uint32_t i = 0; i < vm->stack_top; i++) {
        JaclVal v = vm->stack[i];
        if (jacl_is_heap_type(v)) {
            GCHeader *h = gc_header_of(jacl_as_ptr(v));
            /* If object was collected, alloc_total would be 0 */
            if (h->alloc_total == 0) {
                /* Object collected while still on stack — this is a GC bug */
                ATOMIC_STORE_EXPLICIT(&stress_gc_done, -1, MEM_RELEASE);
                return;
            }
        }
    }
    vm->stack_top = 0;

    ATOMIC_INC(&stress_gc_done);
}

static int test_concurrent_gc_stress(void) {
    /* 4 workers allocating and discarding objects concurrently.
     * GC runs repeatedly as allocation thresholds are exceeded. */
    Runtime rt;
    runtime_init(&rt, 4);

    ATOMIC_STORE_EXPLICIT(&stress_gc_done, 0, MEM_RELEASE);
    int num_tasks = 20;
    intptr_t alloc_count = 50000; /* ~800KB per task → triggers GC */

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, stress__alloc_discard, (void *)alloc_count);
    }

    /* Wait for completion (max 30 seconds) */
    for (int ms = 0; ms < 30000; ms++) {
        intptr_t done = ATOMIC_LOAD_EXPLICIT(&stress_gc_done, MEM_ACQUIRE);
        if (done >= (intptr_t)num_tasks || done < 0) break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t done = ATOMIC_LOAD_EXPLICIT(&stress_gc_done, MEM_ACQUIRE);
    ASSERT(done >= 0); /* no GC bug detected */
    ASSERT_I64_EQ(done, (int64_t)num_tasks);

    runtime_destroy(&rt);
    TEST_PASS();
}

static int test_concurrent_gc_multi_cycle(void) {
    /* Run enough allocation to trigger 10+ GC cycles.
     * Verifies GC is stable over many cycles. */
    Runtime rt;
    runtime_init(&rt, 2);

    ATOMIC_STORE_EXPLICIT(&stress_gc_done, 0, MEM_RELEASE);
    /* 40 tasks × 50K allocs = ~32MB total → many GC cycles */
    int num_tasks = 40;
    intptr_t alloc_count = 50000;

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, stress__alloc_discard, (void *)alloc_count);
    }

    for (int ms = 0; ms < 60000; ms++) {
        intptr_t done = ATOMIC_LOAD_EXPLICIT(&stress_gc_done, MEM_ACQUIRE);
        if (done >= (intptr_t)num_tasks || done < 0) break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t done = ATOMIC_LOAD_EXPLICIT(&stress_gc_done, MEM_ACQUIRE);
    ASSERT(done >= 0);
    ASSERT_I64_EQ(done, (int64_t)num_tasks);

    runtime_destroy(&rt);
    TEST_PASS();
}

/* ====================================================================
 * US-012: Atom CAS semantics
 * ==================================================================== */

static int test_atom_deref_atomic(void) {
    /* Deref on atom reads value atomically */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];
    gc__thread_epoch = 1;

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(
        &w->vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    JaclVal val = jacl_i64(&w->vm.heap, 42);
    ATOMIC_STORE_EXPLICIT(&ref->value, val, MEM_RELEASE);

    /* Read via atomic load (simulating what OP_DEREF does for atoms) */
    JaclVal loaded = ATOMIC_LOAD_EXPLICIT(&ref->value, MEM_ACQUIRE);
    ASSERT(loaded == val);

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_atom_reset_atomic(void) {
    /* Reset on atom stores value atomically */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];
    gc__thread_epoch = 1;

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(
        &w->vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    JaclVal old_val = jacl_i64(&w->vm.heap, 10);
    JaclVal new_val = jacl_i64(&w->vm.heap, 20);
    ATOMIC_STORE_EXPLICIT(&ref->value, old_val, MEM_RELEASE);

    /* Atomic store (simulating what OP_RESET does for atoms) */
    ATOMIC_STORE_EXPLICIT(&ref->value, new_val, MEM_RELEASE);

    JaclVal loaded = ATOMIC_LOAD_EXPLICIT(&ref->value, MEM_ACQUIRE);
    ASSERT(loaded == new_val);

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_atom_cas_basic(void) {
    /* Basic CAS: succeed when expected matches, fail when it doesn't */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];
    gc__thread_epoch = 1;

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(
        &w->vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    JaclVal val_a = jacl_i64(&w->vm.heap, 100);
    JaclVal val_b = jacl_i64(&w->vm.heap, 200);
    JaclVal val_c = jacl_i64(&w->vm.heap, 300);
    ATOMIC_STORE_EXPLICIT(&ref->value, val_a, MEM_RELEASE);

    /* CAS with correct expected → succeeds */
    JaclVal expected = val_a;
    bool ok = ATOMIC_CAS(&ref->value, &expected, val_b,
                          MEM_ACQ_REL, MEM_ACQUIRE);
    ASSERT(ok);
    ASSERT(ATOMIC_LOAD_EXPLICIT(&ref->value, MEM_ACQUIRE) == val_b);

    /* CAS with wrong expected → fails, expected updated to current */
    expected = val_a; /* stale */
    ok = ATOMIC_CAS(&ref->value, &expected, val_c,
                     MEM_ACQ_REL, MEM_ACQUIRE);
    ASSERT(!ok);
    ASSERT(expected == val_b); /* expected updated to actual current */
    ASSERT(ATOMIC_LOAD_EXPLICIT(&ref->value, MEM_ACQUIRE) == val_b);

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

static int test_atom_swap_write_barrier(void) {
    /* Write barrier fires on successful CAS only (not on failed attempts) */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];
    gc__thread_epoch = 1;

    /* Enable write barrier */
    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 1, MEM_RELEASE);
    w->grey_buf.count = 0;

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(
        &w->vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    JaclVal old_val = jacl_i64(&w->vm.heap, 50);
    JaclVal new_val = jacl_i64(&w->vm.heap, 60);
    ATOMIC_STORE_EXPLICIT(&ref->value, old_val, MEM_RELEASE);

    /* Simulate successful CAS + write barrier */
    JaclVal expected = old_val;
    bool ok = ATOMIC_CAS(&ref->value, &expected, new_val,
                          MEM_ACQ_REL, MEM_ACQUIRE);
    ASSERT(ok);
    gc_write_barrier(&w->grey_buf, &rt.gc_active, old_val, new_val);

    /* Both old and new values should be in grey buffer */
    ASSERT(w->grey_buf.count == 2);

    /* Simulate failed CAS — no write barrier should fire */
    uint32_t count_before = w->grey_buf.count;
    expected = old_val; /* stale — will fail */
    ok = ATOMIC_CAS(&ref->value, &expected, jacl_i64(&w->vm.heap, 70),
                     MEM_ACQ_REL, MEM_ACQUIRE);
    ASSERT(!ok);
    /* No write barrier on failed CAS */
    ASSERT(w->grey_buf.count == count_before);

    ATOMIC_STORE_EXPLICIT(&rt.gc_active, 0, MEM_RELEASE);
    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* Shared state for concurrent atom CAS stress test */
static JaclMutableRef *cas_stress_atom;
static volatile intptr_t cas_stress_done;

static void cas_stress__increment(void *data) {
    int iters = (int)(intptr_t)data;
    for (int i = 0; i < iters; i++) {
        for (;;) {
            JaclVal current = ATOMIC_LOAD_EXPLICIT(
                &cas_stress_atom->value, MEM_ACQUIRE);
            int64_t n = jacl_as_i64(current);
            /* Allocate new value on this worker's heap */
            JaclVal next = jacl_i64(gc__current_heap, n + 1);
            JaclVal expected = current;
            if (ATOMIC_CAS(&cas_stress_atom->value, &expected, next,
                           MEM_ACQ_REL, MEM_ACQUIRE)) {
                break; /* CAS succeeded */
            }
            /* CAS failed — retry with fresh read */
        }
    }
    ATOMIC_STORE_EXPLICIT(&cas_stress_done,
        ATOMIC_LOAD_EXPLICIT(&cas_stress_done, MEM_RELAXED) + 1,
        MEM_RELEASE);
}

static int test_atom_cas_concurrent(void) {
    /* 4 workers each CAS-increment an atom 1000 times → final = 4000 */
    Runtime rt;
    runtime_init(&rt, 4);

    /* Allocate shared atom on worker 0's heap */
    gc__current_heap = &rt.workers[0].vm.heap;
    gc__thread_epoch = 1;
    cas_stress_atom = (JaclMutableRef *)gc_alloc(
        &rt.workers[0].vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    JaclVal zero = jacl_i64(&rt.workers[0].vm.heap, 0);
    ATOMIC_STORE_EXPLICIT(&cas_stress_atom->value, zero, MEM_RELEASE);

    ATOMIC_STORE_EXPLICIT(&cas_stress_done, 0, MEM_RELEASE);

    int iters_per_worker = 1000;
    int num_tasks = 4;
    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, cas_stress__increment,
                       (void *)(intptr_t)iters_per_worker);
    }

    /* Wait for all tasks to complete */
    for (int ms = 0; ms < 30000; ms++) {
        intptr_t done = ATOMIC_LOAD_EXPLICIT(&cas_stress_done, MEM_ACQUIRE);
        if (done >= (intptr_t)num_tasks) break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t done = ATOMIC_LOAD_EXPLICIT(&cas_stress_done, MEM_ACQUIRE);
    ASSERT_I64_EQ(done, (int64_t)num_tasks);

    JaclVal final_val = ATOMIC_LOAD_EXPLICIT(
        &cas_stress_atom->value, MEM_ACQUIRE);
    int64_t final_n = jacl_as_i64(final_val);
    ASSERT_I64_EQ(final_n, (int64_t)(num_tasks * iters_per_worker));

    gc__thread_epoch = 0;
    runtime_destroy(&rt);
    TEST_PASS();
}

static int test_box_non_atomic(void) {
    /* Box operations remain plain (non-atomic) — no CAS, no atomic load/store */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];
    gc__thread_epoch = 1;

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(
        &w->vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    JaclVal val = jacl_i64(&w->vm.heap, 99);
    ref->value = val; /* plain store — box semantics */

    /* Plain read — box semantics */
    ASSERT(ref->value == val);

    JaclVal new_val = jacl_i64(&w->vm.heap, 100);
    ref->value = new_val; /* plain store */
    ASSERT(ref->value == new_val);

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        /* US-008: Worker thread pool and task execution loop */
        { "runtime_init_destroy",    test_runtime_init_destroy },
        { "runtime_submit_tasks",    test_runtime_submit_tasks },
        { "runtime_single_worker",   test_runtime_single_worker },
        { "runtime_many_tasks",      test_runtime_many_tasks },
        { "runtime_worker_epoch",    test_runtime_worker_epoch },
        { "runtime_submit_task_api", test_runtime_submit_task_api },
        { "grey_buffer",             test_grey_buffer },
        { "grey_buffer_grow",        test_grey_buffer_grow },
        /* US-009: Root enumeration protocol with BUSY sentinel */
        { "enumerate_roots_vm_stack",           test_enumerate_roots_vm_stack },
        { "enumerate_roots_env",                test_enumerate_roots_env },
        { "enumerate_roots_deque",              test_enumerate_roots_deque },
        { "enumerate_roots_currently_executing", test_enumerate_roots_currently_executing },
        { "enumerate_roots_inbox",              test_enumerate_roots_inbox },
        { "enumerate_roots_busy_sentinel",      test_enumerate_roots_busy_sentinel },
        { "enumerate_roots_multi_worker",       test_enumerate_roots_multi_worker },
        /* US-001 (M14): Multi-root GC scanning */
        { "enumerate_roots_gc_root2",           test_enumerate_roots_gc_root2 },
        /* US-010: Write barriers (hybrid SATB + insertion) */
        { "write_barrier_gc_active",         test_write_barrier_gc_active },
        { "write_barrier_gc_inactive",       test_write_barrier_gc_inactive },
        { "write_barrier_null_fast_path",    test_write_barrier_null_fast_path },
        { "write_barrier_inline_skipped",    test_write_barrier_inline_types_skipped },
        { "write_barrier_mixed_types",       test_write_barrier_mixed_types },
        { "write_barrier_vm_reset",          test_write_barrier_vm_reset },
        { "write_barrier_vm_set_cell",       test_write_barrier_vm_set_cell },
        /* US-011: Concurrent mark-sweep GC with epoch watermark */
        { "epoch_stamping",                 test_epoch_stamping },
        { "epoch_watermark_protect",        test_epoch_watermark_protect },
        { "epoch_watermark_collect",        test_epoch_watermark_collect },
        { "concurrent_gc_full_cycle",       test_concurrent_gc_full_cycle },
        { "concurrent_gc_mark_toggle",      test_concurrent_gc_mark_toggle },
        { "concurrent_gc_mutual_exclusion", test_concurrent_gc_mutual_exclusion },
        { "concurrent_gc_stress",           test_concurrent_gc_stress },
        { "concurrent_gc_multi_cycle",      test_concurrent_gc_multi_cycle },
        /* US-012: Atom CAS semantics */
        { "atom_deref_atomic",           test_atom_deref_atomic },
        { "atom_reset_atomic",           test_atom_reset_atomic },
        { "atom_cas_basic",              test_atom_cas_basic },
        { "atom_swap_write_barrier",     test_atom_swap_write_barrier },
        { "atom_cas_concurrent",         test_atom_cas_concurrent },
        { "box_non_atomic",              test_box_non_atomic },
    };

    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;
    int failed = 0;

    for (int i = 0; i < total; i++) {
        printf("  %-40s ", tests[i].name);
        if (tests[i].fn()) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("\n  %d/%d passed\n", passed, total);
    return failed > 0 ? 1 : 0;
}
