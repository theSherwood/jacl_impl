#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-010: Concurrent intern table access ===== */

/* --- Thread argument structure --- */

typedef struct {
    JaclInternTable *table;
    BlockPool       *pool;
    int              thread_id;
    int              n_strings;
    ThreadHeap       heap;  /* per-thread heap; lifetime managed by caller */
} InternArgs;

/* --- Thread: intern N unique strings (prefixed by thread_id) --- */

static THREAD_PROC_RETURN THREAD_PROC_TYPE intern_unique_fn(void *arg) {
    InternArgs *a = (InternArgs *)arg;
    gc_heap_init(&a->heap, a->pool);
    gc__current_heap = &a->heap;

    for (int i = 0; i < a->n_strings; i++) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "t%d_u_%04d", a->thread_id, i);
        jacl_intern(&a->heap, a->table, buf, (uint32_t)len);
    }

    /* Heap stays alive — caller destroys after verification.
     * Destroying here would return GC blocks to the pool; another thread's
     * gc_alloc could reuse the block, overwriting interned string data and
     * causing false "already exists" matches via dangling intern table pointers. */
    return 0;
}

/* --- Thread: intern N strings from a shared set (same across threads) --- */

static THREAD_PROC_RETURN THREAD_PROC_TYPE intern_shared_fn(void *arg) {
    InternArgs *a = (InternArgs *)arg;
    gc_heap_init(&a->heap, a->pool);
    gc__current_heap = &a->heap;

    for (int i = 0; i < a->n_strings; i++) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "shared_%04d", i);
        jacl_intern(&a->heap, a->table, buf, (uint32_t)len);
    }

    /* Heap destroyed by caller after all threads join — blocks must
     * remain valid while other threads may read intern table entries. */
    return 0;
}

/* --- Thread: repeatedly look up pre-inserted strings (reader) --- */

static THREAD_PROC_RETURN THREAD_PROC_TYPE reader_fn(void *arg) {
    InternArgs *a = (InternArgs *)arg;
    gc_heap_init(&a->heap, a->pool);
    gc__current_heap = &a->heap;

    for (int round = 0; round < 50; round++) {
        for (int i = 0; i < a->n_strings; i++) {
            char buf[64];
            int len = snprintf(buf, sizeof(buf), "pre_%04d", i);
            JaclVal v = jacl_intern(&a->heap, a->table, buf, (uint32_t)len);
            (void)v;
        }
    }

    /* Heap stays alive — caller destroys after verification. */
    return 0;
}

/* ===== Test 1: 8 threads × 1000 unique strings = 8000 entries ===== */

static int test_concurrent_unique_8000(void) {
    tracker_reset();
    BlockPool pool;
    gc_block_pool_init(&pool);
    arena_t arena = {0};
    JaclInternTable table;
    intern_table_init(&table, &arena);

    InternArgs args[8];
    thread_t threads[8];

    for (int i = 0; i < 8; i++) {
        args[i].table = &table;
        args[i].pool = &pool;
        args[i].thread_id = i;
        args[i].n_strings = 1000;
        THREAD_CREATE(&threads[i], NULL, intern_unique_fn, &args[i]);
    }

    for (int i = 0; i < 8; i++) {
        THREAD_JOIN(threads[i], NULL);
    }

    ASSERT_U32_EQ(table.count, 8000);

    /* Destroy thread heaps after verification — blocks must stay valid
     * while intern table entries reference them. */
    for (int i = 0; i < 8; i++) {
        gc_heap_destroy(&args[i].heap);
    }

    intern_table_destroy(&table);
    gc_block_pool_destroy(&pool);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== Test 2: 8 threads × same 100 strings = 100 entries ===== */

static int test_concurrent_shared_100(void) {
    tracker_reset();
    BlockPool pool;
    gc_block_pool_init(&pool);
    arena_t arena = {0};
    JaclInternTable table;
    intern_table_init(&table, &arena);

    InternArgs args[8];
    thread_t threads[8];

    for (int i = 0; i < 8; i++) {
        args[i].table = &table;
        args[i].pool = &pool;
        args[i].thread_id = i;
        args[i].n_strings = 100;
        THREAD_CREATE(&threads[i], NULL, intern_shared_fn, &args[i]);
    }

    for (int i = 0; i < 8; i++) {
        THREAD_JOIN(threads[i], NULL);
    }

    ASSERT_U32_EQ(table.count, 100);

    /* Destroy thread heaps after verification — blocks must stay valid
     * while intern table entries reference them. */
    for (int i = 0; i < 8; i++) {
        gc_heap_destroy(&args[i].heap);
    }

    intern_table_destroy(&table);
    gc_block_pool_destroy(&pool);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== Test 3: 4 readers + 4 writers, all strings findable ===== */

static int test_concurrent_readers_writers(void) {
    tracker_reset();
    BlockPool pool;
    gc_block_pool_init(&pool);
    arena_t arena = {0};
    JaclInternTable table;
    intern_table_init(&table, &arena);

    /* Pre-insert 100 strings for readers to find */
    ThreadHeap setup_heap;
    gc_heap_init(&setup_heap, &pool);
    gc__current_heap = &setup_heap;

    for (int i = 0; i < 100; i++) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "pre_%04d", i);
        jacl_intern(&setup_heap, &table, buf, (uint32_t)len);
    }

    ASSERT_U32_EQ(table.count, 100);

    /* Spawn 4 reader threads + 4 writer threads */
    InternArgs rargs[4], wargs[4];
    thread_t rthreads[4], wthreads[4];

    for (int i = 0; i < 4; i++) {
        rargs[i].table = &table;
        rargs[i].pool = &pool;
        rargs[i].thread_id = 100 + i;
        rargs[i].n_strings = 100;
        THREAD_CREATE(&rthreads[i], NULL, reader_fn, &rargs[i]);

        wargs[i].table = &table;
        wargs[i].pool = &pool;
        wargs[i].thread_id = i;
        wargs[i].n_strings = 1000;
        THREAD_CREATE(&wthreads[i], NULL, intern_unique_fn, &wargs[i]);
    }

    for (int i = 0; i < 4; i++) {
        THREAD_JOIN(rthreads[i], NULL);
        THREAD_JOIN(wthreads[i], NULL);
    }

    /* 100 pre-inserted + 4 × 1000 writer-unique = 4100 */
    ASSERT_U32_EQ(table.count, 4100);

    /* Verify all pre-inserted strings are still findable */
    for (int i = 0; i < 100; i++) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "pre_%04d", i);
        JaclVal v = jacl_intern(&setup_heap, &table, buf, (uint32_t)len);
        ASSERT(v != JACL_NIL);
    }

    /* Destroy thread heaps after all verification — blocks must stay valid
     * while intern table entries reference them. */
    for (int i = 0; i < 4; i++) {
        gc_heap_destroy(&rargs[i].heap);
        gc_heap_destroy(&wargs[i].heap);
    }
    gc_heap_destroy(&setup_heap);
    intern_table_destroy(&table);
    gc_block_pool_destroy(&pool);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== Test 4: Concurrent interning during GC eviction ===== */

/* Thread function that keeps the heap alive (caller manages lifetime).
 * This prevents use-after-free when gc_sweep_intern_table reads
 * GCHeaders on interning threads' heaps during concurrent GC. */
static THREAD_PROC_RETURN THREAD_PROC_TYPE intern_keep_heap_fn(void *arg) {
    InternArgs *a = (InternArgs *)arg;
    gc_heap_init(&a->heap, a->pool);
    gc__current_heap = &a->heap;

    for (int i = 0; i < a->n_strings; i++) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "t%d_u_%04d", a->thread_id, i);
        jacl_intern(&a->heap, a->table, buf, (uint32_t)len);
    }

    /* Heap stays alive — caller destroys after verification */
    return 0;
}

static int test_concurrent_gc_eviction(void) {
    tracker_reset();
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    JaclInternTable table;
    intern_table_init(&table, &arena);
    vm.intern_table = &table;

    /* Spawn 4 interning threads using the VM's shared block pool.
     * Heaps are stored in iargs and destroyed by main thread. */
    InternArgs iargs[4];
    thread_t ithreads[4];

    for (int i = 0; i < 4; i++) {
        iargs[i].table = &table;
        iargs[i].pool = &vm.block_pool;
        iargs[i].thread_id = i;
        iargs[i].n_strings = 500;
        THREAD_CREATE(&ithreads[i], NULL, intern_keep_heap_fn, &iargs[i]);
    }

    /* Main thread triggers GC cycles concurrently with interning.
     * gc_collect calls gc_sweep_intern_table under write lock,
     * which serializes with jacl_intern's read/write lock. */
    for (int cycle = 0; cycle < 10; cycle++) {
        vm.stack_top = 0;  /* No roots → entries appear unreachable */
        gc_collect(&vm.heap, vm__gc_roots_major, &vm, vm.struct_registry, vm.intern_table);
        SLEEP_MILLISECONDS(1);
    }

    for (int i = 0; i < 4; i++) {
        THREAD_JOIN(ithreads[i], NULL);
    }

    /* Verify table structural consistency:
     * walk entries and check count/tombstone_count match reality */
    uint32_t live = 0, tombstones = 0;
    for (uint32_t i = 0; i < table.cap; i++) {
        if (table.entries[i] == NULL) continue;
        if (table.entries[i] == INTERN_TOMBSTONE) {
            tombstones++;
            continue;
        }
        live++;
    }
    ASSERT_U32_EQ(live, table.count);
    ASSERT_U32_EQ(tombstones, table.tombstone_count);

    /* Destroy interning threads' heaps (returns blocks to pool) */
    for (int i = 0; i < 4; i++) {
        gc_heap_destroy(&iargs[i].heap);
    }

    intern_table_destroy(&table);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
    test_fn tests[] = {
        test_concurrent_unique_8000,
        test_concurrent_shared_100,
        test_concurrent_readers_writers,
        test_concurrent_gc_eviction,
    };

    const char *names[] = {
        "test_concurrent_unique_8000",
        "test_concurrent_shared_100",
        "test_concurrent_readers_writers",
        "test_concurrent_gc_eviction",
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int pass = 0, fail = 0;

    printf("=== US-010: Concurrent intern table access ===\n");
    for (int i = 0; i < n; i++) {
        printf("  %-50s ", names[i]);
        if (tests[i]()) {
            pass++;
        } else {
            printf("FAIL\n");
            fail++;
        }
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
