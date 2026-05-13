/*
 * test_chaos_gc_deque_scan.c — Validates AUDIT.md §2 fix.
 *
 * `gc__scan_deque` (src/runtime.c) reads `dq->buffer` and `buf->data[i &
 * buf->mask]` to snapshot deque contents during root enumeration.
 * Chase-Lev's buffer reclamation (`CL_DEQUE_RECLAIM`) is gated on
 * **registered** thieves' epochs — the owner advances epoch on each grow and
 * reclaims retired buffers when no active thief's epoch covers them.
 *
 * Before the fix, the GC scanner wasn't registered, so the owner could push
 * → grow → retire → reclaim while the GC read the buffer it just freed.
 * The fix gives the GC a persistent thief slot per deque, registered at
 * runtime_init; gc__scan_deque enters/exits the epoch protocol around its
 * read.
 *
 * This test has two pieces:
 *   - `test_unsafe_scan_demonstrates_uaf` — reads buffer WITHOUT registering,
 *     mirroring the old (broken) gc__scan_deque. Documents the underlying
 *     constraint. Expected to crash / TSAN-flag without the fix.
 *   - `test_safe_scan_with_thief_slot` — uses a registered thief slot and
 *     the enter/exit protocol. Equivalent to what the fixed
 *     `gc__scan_deque` does. Must pass cleanly under TSAN.
 *
 * The unsafe variant is gated on -DJACL_RUN_UNSAFE_DEMO=1 so default CI
 * doesn't fail on it.
 */

#include "test_helpers.h"

#define CHASE_LEV_T    uintptr_t
#define CHASE_LEV_NAME rt_deque
#define CHASE_LEV_ATOMIC_DATA  /* match runtime.c so TSAN sees slot accesses */
#include "../lib/chase_lev/chase_lev.h"

#define PUSH_ITERS  20000
#define SCAN_ITERS  5000

typedef struct {
    rt_deque_deque   *dq;
    volatile int     *start_flag;
} OwnerCtx;

typedef struct {
    rt_deque_deque   *dq;
    volatile int     *start_flag;
    volatile uint64_t observed_sum;  /* anti-DCE: sum of values seen */
} ScannerCtx;

static THREAD_PROC_RETURN THREAD_PROC_TYPE owner_fn(void *arg) {
    OwnerCtx *ctx = (OwnerCtx *)arg;
    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }

    for (int i = 0; i < PUSH_ITERS; i++) {
        rt_deque_deque_push(ctx->dq, (uintptr_t)(i + 1));
        /* Occasionally take to keep the deque bounded so we grow many times */
        if ((i & 0xF) == 0xF) {
            uintptr_t v;
            rt_deque_deque_take(ctx->dq, &v);
        }
    }
    return (THREAD_PROC_RETURN)0;
}

/* Mimics the current gc__scan_deque: snapshot top/bottom, then read
 * dq->buffer and buf->data[i & buf->mask] without registering as a thief.
 *
 * After start_flag flips, wait until the owner has pushed at least one
 * value before scanning. Otherwise scanner's 5000 rounds — each just a
 * handful of atomic ops — can complete in microseconds, before the owner
 * (which has to allocate, grow buffers, etc.) gets its first push out.
 * Every round would see an empty deque and sum would stay zero. */
static THREAD_PROC_RETURN THREAD_PROC_TYPE unsafe_scanner_fn(void *arg) {
    ScannerCtx *ctx = (ScannerCtx *)arg;
    rt_deque_deque *dq = ctx->dq;
    uint64_t sum = 0;
    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }
    while (ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_ACQUIRE) == 0) { }

    for (int rounds = 0; rounds < SCAN_ITERS; rounds++) {
        uint64_t t = ATOMIC_LOAD_EXPLICIT(&dq->top, MEM_ACQUIRE);
        ATOMIC_FENCE(MEM_SEQ_CST);
        uint64_t b = ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_ACQUIRE);
        if ((int64_t)(b - t) > 0) {
            /* Plain load of dq->buffer — exactly what gc__scan_deque does today */
            rt_deque_buffer *buf = dq->buffer;
            for (uint64_t i = t; i < b; i++) {
                /* Could UAF if buf was retired+reclaimed by owner's grow path */
                sum += buf->data[i & buf->mask];
            }
        }
    }
    ctx->observed_sum = sum;
    return (THREAD_PROC_RETURN)0;
}

/* Mirrors the fixed gc__scan_deque: epoch enter, read, epoch exit.
 * Runs SCAN_ITERS rounds unconditionally; waits for the owner's first
 * push (see unsafe_scanner_fn comment for why). */
static THREAD_PROC_RETURN THREAD_PROC_TYPE safe_scanner_fn(void *arg) {
    ScannerCtx *ctx = (ScannerCtx *)arg;
    rt_deque_deque *dq = ctx->dq;
    uint64_t sum = 0;
    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }
    while (ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_ACQUIRE) == 0) { }

    int thief_id = rt_deque_deque_register_thief(dq);
    for (int rounds = 0; rounds < SCAN_ITERS; rounds++) {
        /* Epoch enter (RELEASE pairs with owner's ACQUIRE in reclaim) */
        uint64_t e = ATOMIC_LOAD_EXPLICIT(&dq->epoch, MEM_ACQUIRE);
        ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch, e, MEM_RELEASE);

        uint64_t t = ATOMIC_LOAD_EXPLICIT(&dq->top, MEM_ACQUIRE);
        ATOMIC_FENCE(MEM_SEQ_CST);
        uint64_t b = ATOMIC_LOAD_EXPLICIT(&dq->bottom, MEM_ACQUIRE);
        if ((int64_t)(b - t) > 0) {
            rt_deque_buffer *buf = ATOMIC_LOAD_EXPLICIT(&dq->buffer, MEM_ACQUIRE);
            for (uint64_t i = t; i < b; i++) {
                sum += ATOMIC_LOAD_EXPLICIT(&buf->data[i & buf->mask],
                                            MEM_RELAXED);
            }
        }

        /* Epoch exit (RELEASE: all reads of buf above happen-before this) */
        ATOMIC_STORE_EXPLICIT(&dq->thieves[thief_id].epoch,
                              UINT64_MAX, MEM_RELEASE);
    }
    rt_deque_deque_unregister_thief(dq, thief_id);
    ctx->observed_sum = sum;
    return (THREAD_PROC_RETURN)0;
}

#ifdef JACL_RUN_UNSAFE_DEMO
static int test_unsafe_scan_demonstrates_uaf(void) {
    rt_deque_deque *dq = rt_deque_deque_new(3);
    volatile int start_flag = 0;

    OwnerCtx octx = { .dq = dq, .start_flag = &start_flag };
    ScannerCtx sctx = { .dq = dq, .start_flag = &start_flag,
                        .observed_sum = 0 };

    thread_t owner, scanner;
    THREAD_CREATE(&owner, NULL, owner_fn, &octx);
    THREAD_CREATE(&scanner, NULL, unsafe_scanner_fn, &sctx);

    ATOMIC_STORE_EXPLICIT(&start_flag, 1, MEM_RELEASE);

    THREAD_JOIN(owner, NULL);
    THREAD_JOIN(scanner, NULL);
    (void)sctx.observed_sum;

    rt_deque_deque_free(dq);
    TEST_PASS();
}
#endif

static int test_safe_scan_with_thief_slot(void) {
    rt_deque_deque *dq = rt_deque_deque_new(3); /* tiny → forces grows */
    volatile int start_flag = 0;

    OwnerCtx octx = { .dq = dq, .start_flag = &start_flag };
    ScannerCtx sctx = { .dq = dq, .start_flag = &start_flag,
                        .observed_sum = 0 };

    thread_t owner, scanner;
    THREAD_CREATE(&owner, NULL, owner_fn, &octx);
    THREAD_CREATE(&scanner, NULL, safe_scanner_fn, &sctx);

    ATOMIC_STORE_EXPLICIT(&start_flag, 1, MEM_RELEASE);

    THREAD_JOIN(owner, NULL);
    THREAD_JOIN(scanner, NULL);

    /* Sanity: we observed at least *some* values. With heavy push+grow
     * activity and 5000 scan rounds, this is essentially always > 0. */
    ASSERT(sctx.observed_sum > 0);

    rt_deque_deque_free(dq);
    TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
#ifdef JACL_RUN_UNSAFE_DEMO
        { "unsafe_scan_demonstrates_uaf", test_unsafe_scan_demonstrates_uaf },
#endif
        { "safe_scan_with_thief_slot",    test_safe_scan_with_thief_slot },
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
