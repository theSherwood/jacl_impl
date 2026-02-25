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
