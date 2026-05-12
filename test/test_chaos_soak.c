/*
 * test_chaos_soak.c — Randomized concurrent-workload soak test.
 *
 * Hammers the runtime + GC via N driver threads that submit a randomized
 * mix of operations:
 *
 *   - SUBMIT_ALLOC    : push a task that allocates many objects and
 *                       discards them (forces GC pressure on the worker)
 *   - SUBMIT_RETAIN   : push a task that allocates and stashes one value
 *                       into a shared atom (exercises write barrier)
 *   - SUBMIT_PINNED   : push a pinned task targeting a specific worker
 *                       (exercises the pinned inbox / private deque drain)
 *   - SWAP_ATOM       : CAS-swap a shared atom from the driver thread
 *
 * Tasks themselves are plain C functions (no JACL bytecode pipeline),
 * which keeps the soak focused on the runtime+GC layer and avoids the
 * compiler/VM as a confounding variable.
 *
 * Duration is configurable via SOAK_DURATION_SEC (default 5s). Seed is
 * configurable via SOAK_SEED (default time-based, printed at start for
 * reproducibility).
 *
 * Pass criteria:
 *   - No crashes (under normal mode)
 *   - No TSAN reports (under --tsan)
 *   - Submitted-task counter == executed-task counter at end
 *   - Final heap walk finds no malformed headers
 */

#include "test_helpers.h"
#include "../src/jacl.h"
#include <time.h>

#define DEFAULT_DURATION_SEC 5
#define NUM_DRIVERS          4
#define NUM_WORKERS          4
#define ATOM_POOL_SIZE       8

/* Shared state used by tasks */
static Runtime          *g_rt;
static volatile intptr_t g_submitted;
static volatile intptr_t g_executed;
static JaclVal           g_atoms[ATOM_POOL_SIZE];
static volatile int      g_stop;

/* --- Task bodies ----------------------------------------------------- */

/* Allocate a chain of objects and discard them. Exercises gc_alloc fast
 * path, slow path, line transitions, and may trigger a concurrent GC. */
static void task_alloc_discard(void *data) {
    int n = (int)(intptr_t)data;
    WorkerThread *self = rt__current_worker;
    if (!self) { ATOMIC_INC(&g_executed); return; }
    ThreadHeap *heap = &self->vm.heap;
    for (int i = 0; i < n; i++) {
        size_t sz = (i & 3) == 0 ? 200 : 16;
        void *p = gc_alloc(heap, OBJ_HEAP_I64, sz);
        if (p) *(volatile uint64_t *)p = (uint64_t)i;
    }
    ATOMIC_INC(&g_executed);
}

/* Allocate one object and atom-store it. Fires the write barrier (when
 * gc_active is set) and the generational remembered-set barrier. */
static void task_alloc_retain(void *data) {
    int atom_idx = (int)(intptr_t)data % ATOM_POOL_SIZE;
    WorkerThread *self = rt__current_worker;
    if (!self) { ATOMIC_INC(&g_executed); return; }
    JaclVal atom_val = g_atoms[atom_idx];
    JaclMutableRef *ref = (JaclMutableRef *)jacl_as_ptr(atom_val);

    JaclVal new_val = jacl_i64(&self->vm.heap, (int64_t)atom_idx + 100);

    /* Atom semantics: the value slot is shared across workers, so load
     * and store must be atomic. Production goes through OP_RESET/OP_SWAP
     * with ATOMIC_CAS; we approximate with acquire load + release store,
     * which is sufficient for exercising the write barrier path. */
    volatile uint64_t *slot = (volatile uint64_t *)&MREF_VAL(ref);
    JaclVal old_val = (JaclVal)ATOMIC_LOAD_EXPLICIT(slot, MEM_ACQUIRE);
    gc_write_barrier(&self->grey_buf, &self->runtime->gc_active,
                     old_val, new_val);
    ATOMIC_STORE_EXPLICIT(slot, (uint64_t)new_val, MEM_RELEASE);
    ATOMIC_INC(&g_executed);
}

/* --- Driver thread --------------------------------------------------- */

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

#define MAX_PENDING 2000  /* keep the queue bounded so drain is finite */

static THREAD_PROC_RETURN THREAD_PROC_TYPE driver_fn(void *arg) {
    DriverCtx *ctx = (DriverCtx *)arg;
    uint64_t state = ctx->seed;

    while (!ATOMIC_LOAD_EXPLICIT(&g_stop, MEM_ACQUIRE)) {
        /* Self-throttle: if submitted has run far ahead of executed,
         * back off so the queue doesn't grow unbounded (especially under
         * TSAN's slowdown). */
        intptr_t sub = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
        intptr_t exe = ATOMIC_LOAD_EXPLICIT(&g_executed,  MEM_ACQUIRE);
        if (sub - exe > MAX_PENDING) {
            SLEEP_MILLISECONDS(1);
            continue;
        }
        uint64_t r = xorshift64(&state);
        int op = (int)(r & 3);
        switch (op) {
        case 0: { /* SUBMIT_ALLOC: many small allocations */
            int n = 20 + (int)((r >> 2) % 80);
            runtime_submit(g_rt, task_alloc_discard, (void *)(intptr_t)n);
            ATOMIC_INC(&g_submitted);
            break;
        }
        case 1: { /* SUBMIT_RETAIN: one alloc + atom write barrier */
            int idx = (int)((r >> 2) % ATOM_POOL_SIZE);
            runtime_submit(g_rt, task_alloc_retain, (void *)(intptr_t)idx);
            ATOMIC_INC(&g_submitted);
            break;
        }
        case 2: { /* SUBMIT_PINNED: pinned task to a specific worker */
            int target = (int)((r >> 2) % NUM_WORKERS);
            int n = 10 + (int)((r >> 10) % 40);
            RuntimeTask *task = (RuntimeTask *)malloc(sizeof(RuntimeTask));
            task->fn = task_alloc_discard;
            task->data = (void *)(intptr_t)n;
            task->gc_root  = JACL_NIL;
            task->gc_root2 = JACL_NIL;
            task->gc_root3 = JACL_NIL;
            runtime__push_pinned(g_rt, task, target);
            ATOMIC_INC(&g_submitted);
            break;
        }
        case 3: /* yield briefly to vary scheduling */
            SLEEP_MILLISECONDS(0);
            break;
        }
    }
    return (THREAD_PROC_RETURN)0;
}

/* --- Heap integrity check at end ------------------------------------ */

static int verify_heap_integrity(Runtime *rt) {
    int problems = 0;
    for (int w = 0; w < rt->num_workers; w++) {
        ThreadHeap *heap = &rt->workers[w].vm.heap;
        MUTEX_LOCK(heap->blocks_mutex);
        for (GCBlock *b = heap->blocks; b; b = b->next) {
            uint8_t *ptr = b->payload;
            uint8_t *end = b->payload + GC_BLOCK_SIZE;
            while (ptr < end) {
                GCHeader *hdr = (GCHeader *)ptr;
                uint16_t total = hdr->alloc_total;
                if (total == 0) {
                    /* free line — skip to next line boundary */
                    size_t offset = (size_t)(ptr - b->payload);
                    size_t next = ((offset / GC_LINE_SIZE) + 1) * GC_LINE_SIZE;
                    if (next >= GC_BLOCK_SIZE) break;
                    ptr = b->payload + next;
                    continue;
                }
                /* Live object: basic sanity */
                if (total < sizeof(GCHeader) || total > GC_BLOCK_SIZE) {
                    fprintf(stderr,
                            "  bad alloc_total=%u at worker %d block %p offset %zu\n",
                            total, w, (void *)b, (size_t)(ptr - b->payload));
                    problems++;
                    break;
                }
                /* obj_type is uint8_t — anything in [0,255] is a possible
                 * enum value. Without a tighter bound we can't verify the
                 * type byte itself; just check it doesn't trip the GC
                 * tracer indirectly (caught by other sanity checks). */
                ptr += total;
            }
        }
        MUTEX_UNLOCK(heap->blocks_mutex);
    }
    return problems;
}

/* --- Main test ------------------------------------------------------- */

static int test_chaos_soak(void) {
    int duration_sec = DEFAULT_DURATION_SEC;
    const char *dur_env = getenv("SOAK_DURATION_SEC");
    if (dur_env) duration_sec = atoi(dur_env);

    uint64_t seed = (uint64_t)time(NULL);
    const char *seed_env = getenv("SOAK_SEED");
    if (seed_env) seed = (uint64_t)strtoull(seed_env, NULL, 10);

    printf("\n  duration=%ds  seed=%llu  drivers=%d  workers=%d  ",
           duration_sec, (unsigned long long)seed,
           NUM_DRIVERS, NUM_WORKERS);
    fflush(stdout);

    Runtime rt;
    runtime_init(&rt, NUM_WORKERS);
    g_rt = &rt;
    ATOMIC_STORE_EXPLICIT(&g_submitted, 0, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&g_executed, 0, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&g_stop, 0, MEM_RELEASE);

    /* Pre-allocate the shared atom pool on worker 0's heap, and root each
     * one in worker 0's VM env so the GC won't sweep them. Without this,
     * the atoms are reachable only from g_atoms[] (not a GC root) and
     * concurrent GC eventually frees them out from under us. */
    gc__thread_epoch = 1;
    VM *vm0 = &rt.workers[0].vm;
    ThreadHeap *h0 = &vm0->heap;
    for (int i = 0; i < ATOM_POOL_SIZE; i++) {
        JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(
            h0, OBJ_MUTABLE_REF, sizeof(JaclMutableRef) + sizeof(JaclVal));
        ref->type_idx = 0;
        ref->total_size = sizeof(JaclVal);
        MREF_VAL(ref) = JACL_NIL;
        g_atoms[i] = jacl_atom_ptr(ref);
        /* Root in the env so GC sees it */
        vm0->env.names[vm0->env.count]  =
            jacl_inline_string("a", 1);
        vm0->env.values[vm0->env.count] = g_atoms[i];
        vm0->env.count++;
    }

    /* Launch drivers */
    DriverCtx dctxs[NUM_DRIVERS];
    thread_t  dthreads[NUM_DRIVERS];
    for (int i = 0; i < NUM_DRIVERS; i++) {
        dctxs[i].driver_id = i;
        /* Per-driver seed so the streams are independent but reproducible */
        dctxs[i].seed = seed ^ (((uint64_t)i + 1) * 0x9e3779b97f4a7c15ULL);
        if (dctxs[i].seed == 0) dctxs[i].seed = 1;
        THREAD_CREATE(&dthreads[i], NULL, driver_fn, &dctxs[i]);
    }

    /* Sleep for the configured duration */
    for (int s = 0; s < duration_sec; s++) {
        SLEEP_MILLISECONDS(1000);
    }

    /* Signal drivers to stop and join them */
    ATOMIC_STORE_EXPLICIT(&g_stop, 1, MEM_RELEASE);
    for (int i = 0; i < NUM_DRIVERS; i++) {
        THREAD_JOIN(dthreads[i], NULL);
    }

    /* Wait for in-flight tasks to drain (cap at 30s) */
    for (int s = 0; s < 30; s++) {
        intptr_t sub = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
        intptr_t exe = ATOMIC_LOAD_EXPLICIT(&g_executed, MEM_ACQUIRE);
        if (exe >= sub) break;
        SLEEP_MILLISECONDS(1000);
    }

    intptr_t submitted = ATOMIC_LOAD_EXPLICIT(&g_submitted, MEM_ACQUIRE);
    intptr_t executed  = ATOMIC_LOAD_EXPLICIT(&g_executed,  MEM_ACQUIRE);
    printf("\n  submitted=%ld  executed=%ld  ",
           (long)submitted, (long)executed);

    /* Stop workers BEFORE the integrity check so we can walk heap blocks
     * without racing with concurrent allocators or sweeps. */
    runtime__stop_threads(&rt);

    int problems = verify_heap_integrity(&rt);
    printf("heap_problems=%d  ", problems);
    fflush(stdout);

    runtime__teardown_state(&rt);
    gc__thread_epoch = 0;

    ASSERT_I64_EQ(executed, submitted);
    ASSERT_INT_EQ(problems, 0);
    TEST_PASS();
}

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        { "chaos_soak", test_chaos_soak },
    };

    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;
    int failed = 0;

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
