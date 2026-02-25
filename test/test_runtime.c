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
    /* Root enumeration finds values on a worker's VM stack */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Allocate a heap object and push onto VM stack */
    JaclVal i64_val = jacl_i64(&w->vm.heap, 12345);
    w->vm.stack[w->vm.stack_top++] = i64_val;

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(i64_val)));

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
    task->fn      = NULL;
    task->data    = cl;
    task->gc_root = cl_val;

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
    task->fn      = NULL;
    task->data    = cl;
    task->gc_root = cl_val;

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
    task->fn      = NULL;
    task->data    = cl;
    task->gc_root = cl_val;

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
    /* Root enumeration across multiple workers finds all roots */
    Runtime rt;
    rt_test__init_no_threads(&rt, 3);

    /* Worker 0: value on VM stack */
    JaclVal val0 = jacl_i64(&rt.workers[0].vm.heap, 111);
    rt.workers[0].vm.stack[rt.workers[0].vm.stack_top++] = val0;

    /* Worker 1: task on public deque */
    JaclClosure *cl1 = (JaclClosure *)gc_alloc(&rt.workers[1].vm.heap,
                                                 OBJ_CLOSURE,
                                                 sizeof(JaclClosure));
    memset(cl1, 0, sizeof(JaclClosure));
    JaclVal cl1_val = JACL_TAG_CLOSURE
                    | ((uint64_t)(uintptr_t)cl1 & JACL_PAYLOAD_MASK);
    RuntimeTask *task1 = (RuntimeTask *)malloc(sizeof(RuntimeTask));
    task1->fn      = NULL;
    task1->data    = cl1;
    task1->gc_root = cl1_val;
    rt_deque_deque_push(rt.workers[1].public_deque, (uintptr_t)task1);

    /* Worker 2: value on VM stack */
    JaclVal val2 = jacl_i64(&rt.workers[2].vm.heap, 222);
    rt.workers[2].vm.stack[rt.workers[2].vm.stack_top++] = val2;

    GCMarkStack ms;
    gc__ms_init(&ms);
    gc_enumerate_roots(&rt, &ms);

    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(val0)));
    ASSERT(rt_test__ms_contains(&ms, (void *)cl1));
    ASSERT(rt_test__ms_contains(&ms, jacl_as_ptr(val2)));

    gc__ms_destroy(&ms);

    /* Cleanup */
    rt.workers[0].vm.stack_top = 0;
    rt.workers[2].vm.stack_top = 0;
    uintptr_t taken;
    rt_deque_deque_take(rt.workers[1].public_deque, &taken);
    free((RuntimeTask *)taken);

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
