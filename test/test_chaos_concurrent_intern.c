/*
 * test_chaos_concurrent_intern.c — Reproducer for AUDIT.md §4.
 *
 * Before the fix, `gc_concurrent_collect` pushed every intern-table entry
 * as a strong root in `gc_enumerate_roots` and never called
 * `gc_sweep_intern_table`. Result: in multi-threaded mode interned strings
 * were immortal — the table grew without bound under churn (a silent
 * behavioral divergence from single-threaded major GC, which evicts them).
 *
 * This test attaches a fresh intern table to each worker's VM, then runs N
 * driver threads submitting tasks that intern many unique strings and drop
 * the references. Under sustained churn we expect the GC to evict dead
 * entries; the table count should stay bounded well below the total
 * number of distinct strings interned.
 *
 * Two threading hazards the test also exercises:
 *
 *   - The GC worker calling `gc_sweep_intern_table` concurrently with a
 *     worker calling `jacl_intern` on the same table. Serialized by
 *     `table->lock`.
 *   - A worker interning a string AFTER the marker enumerated roots but
 *     BEFORE sweep_intern_table runs. The entry isn't marked yet; without
 *     the new epoch-watermark check it would be wrongly tombstoned. With
 *     the watermark, fresh allocations (epoch >= watermark) survive sweep.
 *
 * Under `--tsan` the test should run clean.
 */

#include "test_helpers.h"
#include "../src/jacl.h"
#include <time.h>

#define DEFAULT_DURATION_SEC 3
#define NUM_DRIVERS          4
#define NUM_WORKERS          4
#define STRINGS_PER_TASK     50
#define MAX_PENDING          1500

/* Each worker's intern_table is backed by its own arena. The arena is
 * accessed from the worker thread (when jacl_intern resizes/rehashes
 * under table->lock) and never from the driver thread, so a per-worker
 * arena is sufficient. */
typedef struct {
    arena_t          arena;
    JaclInternTable  table;
} WorkerInternState;

static Runtime           *g_rt;
static WorkerInternState  g_intern[NUM_WORKERS];
static volatile intptr_t  g_submitted;
static volatile intptr_t  g_executed;
static volatile intptr_t  g_total_interned;
static volatile int       g_stop;

/* --- Task body ------------------------------------------------------- */

/* Intern STRINGS_PER_TASK unique strings on the current worker's table
 * and drop the references. Each string is "iN_dDD_tTTTT" so the keyspace
 * is large enough that we'd see unbounded growth if eviction were broken.
 * The intent: by the time gc_sweep_intern_table runs, nothing roots these
 * strings, so almost all should be evicted (modulo the epoch-watermark
 * protection for strings allocated during the GC cycle). */
static void task_intern_churn(void *data) {
    intptr_t seed = (intptr_t)data;
    WorkerThread *self = rt__current_worker;
    if (!self) { ATOMIC_INC(&g_executed); return; }

    JaclInternTable *table = self->vm.intern_table;
    if (!table) { ATOMIC_INC(&g_executed); return; }

    ThreadHeap *heap = &self->vm.heap;
    char buf[48];
    for (int i = 0; i < STRINGS_PER_TASK; i++) {
        int len = snprintf(buf, sizeof(buf),
                           "ic_w%d_s%ld_i%04d",
                           self->id, (long)seed, i);
        if (len < 8) len = 8;  /* keep above inline-string threshold */
        (void)jacl_intern(heap, table, buf, (uint32_t)len);
        ATOMIC_INC(&g_total_interned);
    }
    ATOMIC_INC(&g_executed);
}

/* --- Driver --------------------------------------------------------- */

typedef struct {
    int      driver_id;
    uint64_t seed;
} DriverCtx;

static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    *state = x;
    return x;
}

static THREAD_PROC_RETURN THREAD_PROC_TYPE driver_fn(void *arg) {
    DriverCtx *ctx = (DriverCtx *)arg;
    uint64_t state = ctx->seed;
    intptr_t local_seq = 0;

    while (!ATOMIC_LOAD_EXPLICIT(&g_stop, MEM_ACQUIRE)) {
        intptr_t sub = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
        intptr_t exe = ATOMIC_LOAD_EXPLICIT(&g_executed,  MEM_ACQUIRE);
        if (sub - exe > MAX_PENDING) {
            SLEEP_MILLISECONDS(1);
            continue;
        }

        /* Vary submission: most tasks go through the global inbox, some
         * are pinned to a specific worker (exercises pinned inbox path
         * + makes one worker's table grow faster than the others). */
        uint64_t r = xorshift64(&state);
        intptr_t seed = (ctx->driver_id * 1000000) + local_seq++;
        if ((r & 7) == 0) {
            int target = (int)((r >> 3) % NUM_WORKERS);
            RuntimeTask *t = (RuntimeTask *)malloc(sizeof(RuntimeTask));
            t->fn = task_intern_churn;
            t->data = (void *)seed;
            t->gc_root  = JACL_NIL;
            t->gc_root2 = JACL_NIL;
            t->gc_root3 = JACL_NIL;
            runtime__push_pinned(g_rt, t, target);
        } else {
            runtime_submit(g_rt, task_intern_churn, (void *)seed);
        }
        ATOMIC_INC(&g_submitted);
    }
    return (THREAD_PROC_RETURN)0;
}

/* --- Heap integrity scan: count surviving intern entries per worker -- */

static void summarize_tables(int *out_total_live,
                             int *out_total_tomb,
                             int *out_max_live) {
    int total_live = 0, total_tomb = 0, max_live = 0;
    for (int w = 0; w < NUM_WORKERS; w++) {
        JaclInternTable *t = &g_intern[w].table;
        int live = 0, tomb = 0;
        MUTEX_LOCK(t->lock);
        for (uint32_t i = 0; i < t->cap; i++) {
            JaclHeapString *e = t->entries[i];
            if (e == NULL) continue;
            if (e == INTERN_TOMBSTONE) tomb++;
            else live++;
        }
        MUTEX_UNLOCK(t->lock);
        total_live += live;
        total_tomb += tomb;
        if (live > max_live) max_live = live;
    }
    *out_total_live = total_live;
    *out_total_tomb = total_tomb;
    *out_max_live   = max_live;
}

/* --- Test driver ----------------------------------------------------- */

static int test_chaos_concurrent_intern(void) {
    int duration_sec = DEFAULT_DURATION_SEC;
    const char *dur_env = getenv("INTERN_CHAOS_DURATION_SEC");
    if (dur_env) duration_sec = atoi(dur_env);

    uint64_t seed = (uint64_t)time(NULL);
    const char *seed_env = getenv("INTERN_CHAOS_SEED");
    if (seed_env) seed = (uint64_t)strtoull(seed_env, NULL, 10);

    printf("\n  duration=%ds  seed=%llu  drivers=%d  workers=%d  ",
           duration_sec, (unsigned long long)seed,
           NUM_DRIVERS, NUM_WORKERS);
    fflush(stdout);

    Runtime rt;
    runtime_init(&rt, NUM_WORKERS);
    g_rt = &rt;
    ATOMIC_STORE_EXPLICIT(&g_submitted, 0, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&g_executed,  0, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&g_total_interned, 0, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&g_stop, 0, MEM_RELEASE);

    /* One intern table per worker, each with its own arena. The table
     * is then wired into the worker's VM so jacl_intern can pick it up
     * via rt__current_worker->vm.intern_table. */
    for (int i = 0; i < NUM_WORKERS; i++) {
        memset(&g_intern[i].arena, 0, sizeof(arena_t));
        intern_table_init(&g_intern[i].table, &g_intern[i].arena);
        rt.workers[i].vm.intern_table = &g_intern[i].table;
    }

    DriverCtx dctxs[NUM_DRIVERS];
    thread_t  dthreads[NUM_DRIVERS];
    for (int i = 0; i < NUM_DRIVERS; i++) {
        dctxs[i].driver_id = i;
        dctxs[i].seed = seed ^ (((uint64_t)i + 1) * 0x9e3779b97f4a7c15ULL);
        if (dctxs[i].seed == 0) dctxs[i].seed = 1;
        THREAD_CREATE(&dthreads[i], NULL, driver_fn, &dctxs[i]);
    }

    for (int s = 0; s < duration_sec; s++) {
        SLEEP_MILLISECONDS(1000);
    }

    ATOMIC_STORE_EXPLICIT(&g_stop, 1, MEM_RELEASE);
    for (int i = 0; i < NUM_DRIVERS; i++) {
        THREAD_JOIN(dthreads[i], NULL);
    }

    /* Drain in-flight tasks (cap at 30s) */
    for (int s = 0; s < 30; s++) {
        intptr_t sub = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
        intptr_t exe = ATOMIC_LOAD_EXPLICIT(&g_executed,  MEM_ACQUIRE);
        if (exe >= sub) break;
        SLEEP_MILLISECONDS(1000);
    }

    intptr_t submitted = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
    intptr_t executed  = ATOMIC_LOAD_EXPLICIT(&g_executed,  MEM_ACQUIRE);
    intptr_t interned  = ATOMIC_LOAD_EXPLICIT(&g_total_interned, MEM_ACQUIRE);

    /* Stop workers BEFORE the table summary so we get a quiescent view. */
    runtime__stop_threads(&rt);

    int live = 0, tomb = 0, max_live = 0;
    summarize_tables(&live, &tomb, &max_live);

    uint64_t total_cycles = 0;
    for (int w = 0; w < NUM_WORKERS; w++) {
        total_cycles += rt.workers[w].vm.heap.gc_cycle_count;
    }

    printf("\n  submitted=%ld  executed=%ld  interned=%ld  gc_cycles=%llu  live=%d  tomb=%d  max_live=%d  ",
           (long)submitted, (long)executed, (long)interned,
           (unsigned long long)total_cycles,
           live, tomb, max_live);
    fflush(stdout);

    /* Detach tables from workers BEFORE teardown — teardown should not
     * touch them, but be explicit so we own the destroy order. */
    for (int i = 0; i < NUM_WORKERS; i++) {
        rt.workers[i].vm.intern_table = NULL;
    }

    runtime__teardown_state(&rt);

    /* Now safe to destroy tables and their arenas. */
    for (int i = 0; i < NUM_WORKERS; i++) {
        intern_table_destroy(&g_intern[i].table);
        arena_destroy(&g_intern[i].arena);
    }

    /* Correctness (i.e. all submitted work ran cleanly) */
    ASSERT_I64_EQ(executed, submitted);

    /* Eviction (i.e. AUDIT.md §4 is fixed): under sustained churn with
     * NO retained references, the GC must have evicted dead entries.
     * If concurrent GC did not call gc_sweep_intern_table, `live` would
     * track `interned` 1:1.
     *
     * Bound is intentionally loose (15% eviction) so the test isn't
     * flaky under TSAN's 20x slowdown, where fewer GC cycles complete
     * within the test window and the epoch watermark protects a larger
     * fraction of recently interned strings. The pre-fix bug shows up
     * as live == interned, which this still catches.
     *
     * Skip the check if we didn't intern enough to expect any GC. */
    if (interned > 1000 && total_cycles > 2) {
        intptr_t bound = (interned * 85) / 100;
        if ((intptr_t)live >= bound) {
            fprintf(stderr,
                    "  EVICTION FAILED: live=%d / interned=%ld (bound=%ld, "
                    "gc_cycles=%llu) — concurrent GC is not sweeping the "
                    "intern table.\n",
                    live, (long)interned, (long)bound,
                    (unsigned long long)total_cycles);
            return 0;
        }
    }

    TEST_PASS();
}

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        { "chaos_concurrent_intern", test_chaos_concurrent_intern },
    };

    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0, failed = 0;

    for (int i = 0; i < total; i++) {
        printf("  %-30s", tests[i].name);
        if (tests[i].fn()) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("\n  %d/%d passed\n", passed, total);
    return failed > 0 ? 1 : 0;
}
