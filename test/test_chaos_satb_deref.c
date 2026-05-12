/*
 * test_chaos_satb_deref.c — TSAN stress test for the §9 fix.
 *
 * The actual UAF reproducer lives in test_runtime.c
 * (`test_write_barrier_satb_protects_held_value`) — it runs
 * gc_concurrent_collect synchronously to make the
 * "mutate-while-gc_active-is-false, then GC reclaims V_old" race
 * deterministic.
 *
 * This file is the chaos counterpart: hammer reset!/swap-style
 * mutations on a shared atom from multiple workers while concurrent
 * GC cycles fire. Pre-fix, gc_write_barrier was gated on gc_active
 * and fast-pathed out between cycles. Post-fix, the deletion side
 * pushes V_old unconditionally. Either way, the chaos test is
 * primarily a TSAN canary — it verifies no race shows up around the
 * unconditional grey-buffer push that the fix introduces, especially
 * the interaction between barrier pushes (from many workers) and the
 * GC drain.
 *
 * Pass criteria: no crashes, no TSAN reports, no race-mode hangs.
 */

#include "test_helpers.h"
#include "../src/jacl.h"
#include <time.h>

#define DEFAULT_DURATION_SEC 3
#define NUM_DRIVERS          4
#define NUM_WORKERS          4
#define MAX_PENDING          1500

static Runtime          *g_rt;
static JaclMutableRef   *g_atom;
static volatile intptr_t g_submitted;
static volatile intptr_t g_executed;
static volatile int      g_stop;

/* Allocate a fresh heap-i64 and atomically reset! the shared atom to
 * it. Fires the SATB deletion barrier on whatever the atom was holding,
 * and the insertion barrier if gc_active. */
static void task_mutate(void *data) {
    intptr_t seed = (intptr_t)data;
    WorkerThread *self = rt__current_worker;
    if (!self) { ATOMIC_INC(&g_executed); return; }

    JaclVal new_val = jacl_i64(&self->vm.heap, (int64_t)seed);
    JaclVal old_val = (JaclVal)ATOMIC_LOAD_EXPLICIT(
        (volatile uint64_t *)&MREF_VAL(g_atom), MEM_ACQUIRE);
    gc_write_barrier(&self->grey_buf, &self->runtime->gc_active,
                     old_val, new_val);
    ATOMIC_STORE_EXPLICIT((volatile uint64_t *)&MREF_VAL(g_atom),
                          (uint64_t)new_val, MEM_RELEASE);

    ATOMIC_INC(&g_executed);
}

/* Deref the atom and use the returned value briefly. The deref is the
 * same load OP_DEREF does on a real atom; the brief use simulates a
 * CPS segment computing something off V before the next opcode. */
static void task_deref_use(void *data) {
    (void)data;
    WorkerThread *self = rt__current_worker;
    if (!self) { ATOMIC_INC(&g_executed); return; }

    JaclVal v = (JaclVal)ATOMIC_LOAD_EXPLICIT(
        (volatile uint64_t *)&MREF_VAL(g_atom), MEM_ACQUIRE);
    if (jacl_is_heap_type(v)) {
        /* Touch the header so we'd notice if it got reclaimed. The
         * deterministic test in test_runtime.c is the actual regression
         * guard; here we just want to load and not crash. */
        void *ptr = jacl_as_ptr(v);
        GCHeader *hdr = gc_header_of(ptr);
        volatile uint8_t t = hdr->obj_type;
        (void)t;
    }
    ATOMIC_INC(&g_executed);
}

/* Allocate a chain of objects to keep the GC trigger pump primed. */
static void task_alloc_pressure(void *data) {
    int n = (int)(intptr_t)data;
    WorkerThread *self = rt__current_worker;
    if (!self) { ATOMIC_INC(&g_executed); return; }
    ThreadHeap *heap = &self->vm.heap;
    for (int i = 0; i < n; i++) {
        void *p = gc_alloc(heap, OBJ_HEAP_I64, 64);
        if (p) *(volatile uint64_t *)p = (uint64_t)i;
    }
    ATOMIC_INC(&g_executed);
}

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
        if (sub - exe > MAX_PENDING) { SLEEP_MILLISECONDS(1); continue; }

        uint64_t r = xorshift64(&state);
        intptr_t seed = (ctx->driver_id * 1000000) + local_seq++;
        switch ((int)(r & 3)) {
        case 0: /* mutate — fires the barrier */
            runtime_submit(g_rt, task_mutate, (void *)seed);
            ATOMIC_INC(&g_submitted);
            break;
        case 1: /* deref */
            runtime_submit(g_rt, task_deref_use, NULL);
            ATOMIC_INC(&g_submitted);
            break;
        default: { /* allocation pressure → triggers concurrent GC */
            int n = 20 + (int)((r >> 2) % 60);
            runtime_submit(g_rt, task_alloc_pressure,
                           (void *)(intptr_t)n);
            ATOMIC_INC(&g_submitted);
            break;
        }
        }
    }
    return (THREAD_PROC_RETURN)0;
}

static int test_chaos_satb_deref(void) {
    int duration_sec = DEFAULT_DURATION_SEC;
    const char *dur_env = getenv("SATB_CHAOS_DURATION_SEC");
    if (dur_env) duration_sec = atoi(dur_env);

    uint64_t seed = (uint64_t)time(NULL);
    const char *seed_env = getenv("SATB_CHAOS_SEED");
    if (seed_env) seed = (uint64_t)strtoull(seed_env, NULL, 10);

    printf("\n  duration=%ds  seed=%llu  workers=%d  ",
           duration_sec, (unsigned long long)seed, NUM_WORKERS);
    fflush(stdout);

    Runtime rt;
    runtime_init(&rt, NUM_WORKERS);
    g_rt = &rt;
    ATOMIC_STORE_EXPLICIT(&g_submitted, 0, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&g_executed,  0, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&g_stop, 0, MEM_RELEASE);

    /* Initialise the shared atom on worker 0 with a heap-i64 value.
     * Root it in worker 0's env so the atom itself stays alive. */
    gc__thread_epoch = 1;
    VM *vm0 = &rt.workers[0].vm;
    ThreadHeap *h0 = &vm0->heap;
    JaclVal initial_val = jacl_i64(h0, 0x77LL);
    JaclMutableRef *atom = (JaclMutableRef *)gc_alloc(
        h0, OBJ_MUTABLE_REF, sizeof(JaclMutableRef) + sizeof(JaclVal));
    atom->type_idx   = 0;
    atom->total_size = sizeof(JaclVal);
    MREF_VAL(atom)   = initial_val;
    g_atom = atom;
    vm0->env.names[vm0->env.count]  = jacl_inline_string("a", 1);
    vm0->env.values[vm0->env.count] = jacl_atom_ptr(atom);
    vm0->env.count++;

    /* Launch drivers */
    DriverCtx dctxs[NUM_DRIVERS];
    thread_t  dthreads[NUM_DRIVERS];
    for (int i = 0; i < NUM_DRIVERS; i++) {
        dctxs[i].driver_id = i;
        dctxs[i].seed = seed ^ (((uint64_t)i + 1) * 0x9e3779b97f4a7c15ULL);
        if (dctxs[i].seed == 0) dctxs[i].seed = 1;
        THREAD_CREATE(&dthreads[i], NULL, driver_fn, &dctxs[i]);
    }

    for (int s = 0; s < duration_sec; s++) SLEEP_MILLISECONDS(1000);

    ATOMIC_STORE_EXPLICIT(&g_stop, 1, MEM_RELEASE);
    for (int i = 0; i < NUM_DRIVERS; i++) THREAD_JOIN(dthreads[i], NULL);

    for (int s = 0; s < 30; s++) {
        intptr_t sub = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
        intptr_t exe = ATOMIC_LOAD_EXPLICIT(&g_executed,  MEM_ACQUIRE);
        if (exe >= sub) break;
        SLEEP_MILLISECONDS(1000);
    }

    intptr_t submitted = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
    intptr_t executed  = ATOMIC_LOAD_EXPLICIT(&g_executed,  MEM_ACQUIRE);
    runtime__stop_threads(&rt);

    printf("submitted=%ld executed=%ld  ", (long)submitted, (long)executed);
    fflush(stdout);

    runtime__teardown_state(&rt);
    gc__thread_epoch = 0;

    ASSERT_I64_EQ(executed, submitted);
    TEST_PASS();
}

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        { "chaos_satb_deref", test_chaos_satb_deref },
    };
    int total  = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0, failed = 0;
    for (int i = 0; i < total; i++) {
        printf("  %-30s", tests[i].name);
        if (tests[i].fn()) passed++; else failed++;
    }
    printf("\n  %d/%d passed\n", passed, total);
    return failed > 0 ? 1 : 0;
}
