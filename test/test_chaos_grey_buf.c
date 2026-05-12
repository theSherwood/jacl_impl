/*
 * test_chaos_grey_buf.c — Reproducer for AUDIT.md §3.
 *
 * `grey_buf_push` (src/gc.c) reallocates `gb->entries` when the buffer fills,
 * publishing the new count with `MEM_RELEASE`. `gc__drain_grey_bufs`
 * (src/runtime.c) does an `MEM_ACQUIRE` load of `gb->count`, then iterates
 * `gb->entries[j]` for j in [drained, count).
 *
 * The audit calls out: between the acquire on count and the loop reading
 * `gb->entries[j]`, the writer can realloc AGAIN, freeing the buffer the
 * reader is iterating. The release/acquire on count only synchronizes the
 * *first* realloc's pointer publication; subsequent reallocs free the
 * pointer the reader still holds → UAF.
 *
 * This test hammers grey_buf_push from a writer thread while a reader
 * thread drains. Under TSAN it surfaces a heap-use-after-free on the old
 * entries buffer or a data race on the entries pointer.
 *
 * The fix in src/gc.c is to serialize push and drain via a mutex on the
 * GreyBuffer, since pushes are already rare (only during gc_active).
 */

#include "test_helpers.h"
#include "../src/jacl.h"

#define PUSH_COUNT  20000
#define DRAIN_ITERS  5000

typedef struct {
    GreyBuffer     *gb;
    volatile int   *start_flag;
    volatile int   *writer_done;
} WriterCtx;

typedef struct {
    GreyBuffer     *gb;
    volatile int   *start_flag;
    volatile int   *writer_done;
    volatile uint64_t observed_sum;
} ReaderCtx;

static THREAD_PROC_RETURN THREAD_PROC_TYPE writer_fn(void *arg) {
    WriterCtx *ctx = (WriterCtx *)arg;
    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }

    for (int i = 0; i < PUSH_COUNT; i++) {
        /* Push real JaclVal payloads. Value doesn't matter; we just want
         * to force many reallocs (initial cap 256, then 512, 1024, ...). */
        grey_buf_push(ctx->gb, (JaclVal)(uintptr_t)(i + 1));
    }
    ATOMIC_STORE_EXPLICIT(ctx->writer_done, 1, MEM_RELEASE);
    return (THREAD_PROC_RETURN)0;
}

/* Mimics the fixed gc__drain_grey_bufs: lock the buffer's mutex around the
 * read so realloc in grey_buf_push can't pull the entries pointer out from
 * under us. */
static THREAD_PROC_RETURN THREAD_PROC_TYPE reader_fn(void *arg) {
    ReaderCtx *ctx = (ReaderCtx *)arg;
    GreyBuffer *gb = ctx->gb;
    uint64_t sum = 0;
    uint32_t drained = 0;
    int rounds = 0;
    while (!ATOMIC_LOAD_EXPLICIT(ctx->start_flag, MEM_ACQUIRE)) { }

    while (rounds < DRAIN_ITERS) {
        MUTEX_LOCK(gb->mutex);
        uint32_t current = gb->count;
        if (current > drained) {
            for (uint32_t j = drained; j < current; j++) {
                sum += (uint64_t)gb->entries[j];
            }
            drained = current;
        }
        MUTEX_UNLOCK(gb->mutex);

        if (current <= drained &&
            ATOMIC_LOAD_EXPLICIT(ctx->writer_done, MEM_ACQUIRE)) {
            /* Final drain attempt then exit */
            MUTEX_LOCK(gb->mutex);
            uint32_t final = gb->count;
            for (uint32_t j = drained; j < final; j++) {
                sum += (uint64_t)gb->entries[j];
            }
            drained = final;
            MUTEX_UNLOCK(gb->mutex);
            break;
        }
        rounds++;
    }
    ctx->observed_sum = sum;
    ctx->observed_sum += drained; /* keep used */
    return (THREAD_PROC_RETURN)0;
}

static int test_grey_buf_concurrent_drain(void) {
    GreyBuffer gb;
    grey_buf_init(&gb);

    volatile int start_flag = 0;
    volatile int writer_done = 0;

    WriterCtx wctx = { .gb = &gb, .start_flag = &start_flag,
                       .writer_done = &writer_done };
    ReaderCtx rctx = { .gb = &gb, .start_flag = &start_flag,
                       .writer_done = &writer_done, .observed_sum = 0 };

    thread_t writer, reader;
    THREAD_CREATE(&writer, NULL, writer_fn, &wctx);
    THREAD_CREATE(&reader, NULL, reader_fn, &rctx);

    ATOMIC_STORE_EXPLICIT(&start_flag, 1, MEM_RELEASE);

    THREAD_JOIN(writer, NULL);
    THREAD_JOIN(reader, NULL);

    /* Sanity: writer pushed PUSH_COUNT items */
    ASSERT_U32_EQ(gb.count, (uint32_t)PUSH_COUNT);

    /* observed_sum is racy under the bug. Just make sure it's consumed. */
    (void)rctx.observed_sum;

    grey_buf_destroy(&gb);
    TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        { "grey_buf_concurrent_drain", test_grey_buf_concurrent_drain },
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
