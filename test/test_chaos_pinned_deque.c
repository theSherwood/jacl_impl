/*
 * test_chaos_pinned_deque.c — Validates the multi-producer pinned-inbox fix
 * for AUDIT.md §1.
 *
 * Before the fix, `runtime__push_pinned` pushed directly onto another
 * worker's Chase-Lev `private_deque`, violating the deque's SPSC contract.
 * The fix routes pinned tasks through a per-worker `pinned_inbox` (a
 * mutex-protected MPSC queue) which the owner drains into its private deque
 * at the top of each loop iteration.
 *
 * This test hammers `runtime__push_pinned` from N external threads targeting
 * the same worker, while the runtime's worker threads drain and execute.
 * It checks that:
 *   - Every submitted task runs exactly once (no losses, no duplicates).
 *   - The process doesn't crash (no UAF on a buffer).
 *   - Under TSAN, no data race fires.
 *
 * Each task increments a shared atomic counter, plus sets a per-task "seen"
 * flag so we can detect duplicates and losses.
 */

#include "test_helpers.h"
#include "../src/jacl.h"

#define NUM_PRODUCERS       4
#define PUSHES_PER_PRODUCER 2000
#define TOTAL_PUSHES        (NUM_PRODUCERS * PUSHES_PER_PRODUCER)
#define TARGET_WORKER       0

/* Shared counters across all producer threads / worker threads */
static volatile intptr_t g_completed;
static volatile int      g_seen[TOTAL_PUSHES];

typedef struct {
    int task_id;  /* 0..TOTAL_PUSHES-1 */
} PinnedTaskData;

static void pinned_task_fn(void *data) {
    PinnedTaskData *d = (PinnedTaskData *)data;
    /* Mark this task seen exactly once (relaxed; we'll inspect after join). */
    g_seen[d->task_id] = 1;
    ATOMIC_INC(&g_completed);
    free(d);
}

typedef struct {
    Runtime          *rt;
    int               producer_id;
    volatile int     *start_flag;
} ProducerCtx;

static THREAD_PROC_RETURN THREAD_PROC_TYPE producer_fn(void *arg) {
    ProducerCtx *ctx = (ProducerCtx *)arg;
    /* All producers fire after the start flag, racing as tightly as possible. */
    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }

    for (int i = 0; i < PUSHES_PER_PRODUCER; i++) {
        int task_id = ctx->producer_id * PUSHES_PER_PRODUCER + i;
        PinnedTaskData *d = (PinnedTaskData *)malloc(sizeof(PinnedTaskData));
        d->task_id = task_id;

        RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
        task->fn       = pinned_task_fn;
        task->data     = d;
        task->gc_root  = JACL_NIL;
        task->gc_root2 = JACL_NIL;
        task->gc_root3 = JACL_NIL;

        runtime__push_pinned(ctx->rt, task, TARGET_WORKER);
    }
    return (THREAD_PROC_RETURN)0;
}

static int test_pinned_inbox_multi_producer(void) {
    /* Reset shared state */
    ATOMIC_STORE_EXPLICIT(&g_completed, 0, MEM_RELEASE);
    for (int i = 0; i < TOTAL_PUSHES; i++) g_seen[i] = 0;

    /* 2 workers is enough: target worker (0) drains its pinned inbox, the
     * other can steal from worker 0's public deque if needed. The test never
     * pushes to public though, so worker 1 mostly sleeps. */
    Runtime rt;
    runtime_init(&rt, 2);

    volatile int start_flag = 0;
    ProducerCtx pctx[NUM_PRODUCERS];
    thread_t producers[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pctx[i].rt          = &rt;
        pctx[i].producer_id = i;
        pctx[i].start_flag  = &start_flag;
        THREAD_CREATE(&producers[i], NULL, producer_fn, &pctx[i]);
    }

    /* Release all producers simultaneously */
    ATOMIC_STORE_EXPLICIT(&start_flag, 1, MEM_RELEASE);

    /* Wait for producers to finish pushing */
    for (int i = 0; i < NUM_PRODUCERS; i++) THREAD_JOIN(producers[i], NULL);

    /* Wait for all tasks to execute (up to 30s) */
    for (int ms = 0; ms < 30000; ms++) {
        if (ATOMIC_LOAD_EXPLICIT(&g_completed, MEM_ACQUIRE) >= TOTAL_PUSHES)
            break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t completed = ATOMIC_LOAD_EXPLICIT(&g_completed, MEM_ACQUIRE);
    if (completed != TOTAL_PUSHES) {
        fprintf(stderr, "completed=%ld expected=%d\n",
                (long)completed, TOTAL_PUSHES);
    }
    ASSERT_I64_EQ(completed, (int64_t)TOTAL_PUSHES);

    /* Every task ran exactly once */
    int missing = 0;
    for (int i = 0; i < TOTAL_PUSHES; i++) {
        if (g_seen[i] == 0) missing++;
    }
    if (missing != 0) {
        fprintf(stderr, "missing=%d of %d\n", missing, TOTAL_PUSHES);
    }
    ASSERT_INT_EQ(missing, 0);

    runtime_destroy(&rt);
    TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        { "pinned_inbox_multi_producer", test_pinned_inbox_multi_producer },
    };

    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;
    int failed = 0;

    for (int i = 0; i < total; i++) {
        printf("  %-50s ", tests[i].name);
        if (tests[i].fn()) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("\n  %d/%d passed\n", passed, total);
    return failed > 0 ? 1 : 0;
}
