/*
 * test_chaos_gc_alloc_sweep.c — Reproducer for AUDIT.md §§7 and 15.
 *
 * Two related races on `ThreadHeap.blocks` (a plain singly-linked list of
 * GCBlock):
 *
 *   §7: Concurrent sweep's `skip_block` is snapshotted from
 *       `heap->current_block` at sweep start. If the owning worker switches
 *       `current_block` to a different block during the sweep, the sweep
 *       walks the new active block. `gc_alloc` writes `GCHeader` fields
 *       (epoch, mark, obj_type, alloc_total) non-atomically while sweep
 *       reads them.
 *
 *   §15: `gc_sweep_concurrent` unlinks fully-empty blocks from
 *       `heap->blocks` inline and returns them to the pool. The owner's
 *       `gc__find_fit_in_block` scan walks `heap->blocks` via plain `next`
 *       pointer reads — concurrent unlink + read is UB.
 *
 * This test runs an allocator thread that forces many block switches
 * (allocating until current_block exhausts, then moving to a new one) and
 * a sweep thread that repeatedly calls gc_sweep_concurrent on the same
 * heap. Under TSAN before the fix, expect races on heap->blocks or
 * GCHeader fields. After the fix (per-heap mutex), should be clean.
 */

#include "test_helpers.h"
#include "../src/jacl.h"

#define ALLOC_ITERS  4000
#define SWEEP_ITERS  3000

typedef struct {
    ThreadHeap     *heap;
    volatile int   *start_flag;
    volatile int   *done_flag;
} AllocCtx;

typedef struct {
    ThreadHeap     *heap;
    BlockPool      *pool;
    volatile int   *start_flag;
    volatile int   *done_flag;
} SweepCtx;

static THREAD_PROC_RETURN THREAD_PROC_TYPE alloc_fn(void *arg) {
    AllocCtx *ctx = (AllocCtx *)arg;
    /* gc_alloc reads gc__thread_epoch (TLS) when stamping headers. Set it
     * to a stable nonzero value so the watermark check has well-defined
     * input on the sweeper side. */
    extern JACL_THREAD_LOCAL uint32_t gc__thread_epoch;
    gc__thread_epoch = 5;

    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }

    for (int i = 0; i < ALLOC_ITERS; i++) {
        /* Vary object size so we force many line transitions and
         * occasional new-block acquisition. Some are line-sized (128B),
         * some are larger to span lines. */
        size_t sz = (i & 3) == 0 ? 200 : 16;
        void *p = gc_alloc(ctx->heap, OBJ_HEAP_I64, sz);
        if (p) {
            /* Write something into the payload — typical use */
            *(volatile uint64_t *)p = (uint64_t)i;
        }
    }
    ATOMIC_STORE_EXPLICIT(ctx->done_flag, 1, MEM_RELEASE);
    return (THREAD_PROC_RETURN)0;
}

static THREAD_PROC_RETURN THREAD_PROC_TYPE sweep_fn(void *arg) {
    SweepCtx *ctx = (SweepCtx *)arg;
    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }

    int rounds = 0;
    /* Use a watermark of 1 so most allocations (stamped at epoch 5) survive.
     * We just want sweep to walk the list and touch headers. */
    uint32_t watermark = 1;
    uint8_t  current_mark = 1;

    while (rounds < SWEEP_ITERS &&
           !ATOMIC_LOAD_EXPLICIT(ctx->done_flag, MEM_ACQUIRE)) {
        /* Pass NULL for skip_block: gc_sweep_concurrent re-snapshots
         * heap->current_block under heap->blocks_mutex internally. */
        gc_sweep_concurrent(ctx->heap, NULL, watermark, current_mark,
                            ctx->pool);
        rounds++;
    }
    /* Final sweep after allocator done */
    gc_sweep_concurrent(ctx->heap, NULL, watermark, current_mark, ctx->pool);
    return (THREAD_PROC_RETURN)0;
}

static int test_alloc_sweep_concurrent(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);

    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    volatile int start_flag = 0;
    volatile int done_flag  = 0;

    AllocCtx actx = { .heap = &heap, .start_flag = &start_flag,
                      .done_flag = &done_flag };
    SweepCtx sctx = { .heap = &heap, .pool = &pool, .start_flag = &start_flag,
                      .done_flag = &done_flag };

    thread_t allocator, sweeper;
    THREAD_CREATE(&allocator, NULL, alloc_fn, &actx);
    THREAD_CREATE(&sweeper, NULL, sweep_fn, &sctx);

    ATOMIC_STORE_EXPLICIT(&start_flag, 1, MEM_RELEASE);

    THREAD_JOIN(allocator, NULL);
    THREAD_JOIN(sweeper, NULL);

    /* Sanity: did at least one block get allocated? */
    ASSERT(heap.blocks != NULL);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        { "alloc_sweep_concurrent", test_alloc_sweep_concurrent },
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
