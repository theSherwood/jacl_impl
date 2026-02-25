#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-002: Block-based heap allocator ===== */

static int test_gc_block_pool(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);

    /* Get from empty pool — allocates a fresh block */
    GCBlock *b1 = gc_block_pool_get(&pool);
    ASSERT(b1 != NULL);

    /* All lines should be free */
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        ASSERT_INT_EQ(b1->line_map[i], GC_LINE_FREE);
    }

    /* Mark some lines to prove return+get resets them */
    b1->line_map[0] = GC_LINE_OCCUPIED;
    b1->line_map[100] = GC_LINE_OCCUPIED;

    /* Return to pool */
    gc_block_pool_return(&pool, b1);

    /* Get again — should recycle the same block with reset line map */
    GCBlock *b2 = gc_block_pool_get(&pool);
    ASSERT(b2 == b1);
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        ASSERT_INT_EQ(b2->line_map[i], GC_LINE_FREE);
    }

    gc_block_pool_return(&pool, b2);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_small(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Allocate a small object (8 bytes payload = JaclHeapI64) */
    void *p = gc_alloc(&heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    ASSERT(p != NULL);

    GCHeader *hdr = gc_header_of(p);
    ASSERT_U32_EQ(hdr->epoch, 0);
    ASSERT_INT_EQ(hdr->mark, 0);
    ASSERT_INT_EQ(hdr->obj_type, OBJ_HEAP_I64);
    ASSERT_INT_EQ(hdr->gen, 0);

    /* total = 8 + 8 = 16, aligned = 16 — fits in one line */
    ASSERT_INT_EQ(heap.current_block->line_map[0], GC_LINE_OCCUPIED);
    ASSERT_INT_EQ(heap.current_block->line_map[1], GC_LINE_FREE);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_line_sized(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Exactly one line: payload = 128 - 8 = 120, total = 128 */
    void *p = gc_alloc(&heap, OBJ_STRING, GC_LINE_SIZE - sizeof(GCHeader));
    ASSERT(p != NULL);

    ASSERT_INT_EQ(gc_header_of(p)->obj_type, OBJ_STRING);
    ASSERT_INT_EQ(heap.current_block->line_map[0], GC_LINE_OCCUPIED);
    ASSERT_INT_EQ(heap.current_block->line_map[1], GC_LINE_FREE);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_multi_line(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* 400 bytes payload: total = 408, aligned = 408, spans lines 0-3 */
    void *p = gc_alloc(&heap, OBJ_CLOSURE, 400);
    ASSERT(p != NULL);

    ASSERT_INT_EQ(gc_header_of(p)->obj_type, OBJ_CLOSURE);
    ASSERT_INT_EQ(heap.current_block->line_map[0], GC_LINE_OCCUPIED);
    ASSERT_INT_EQ(heap.current_block->line_map[1], GC_LINE_OCCUPIED);
    ASSERT_INT_EQ(heap.current_block->line_map[2], GC_LINE_OCCUPIED);
    ASSERT_INT_EQ(heap.current_block->line_map[3], GC_LINE_OCCUPIED);
    ASSERT_INT_EQ(heap.current_block->line_map[4], GC_LINE_FREE);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_multiple(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    void *p1 = gc_alloc(&heap, OBJ_HEAP_I64, 8);
    void *p2 = gc_alloc(&heap, OBJ_HEAP_U64, 8);
    void *p3 = gc_alloc(&heap, OBJ_STRING, 32);
    ASSERT(p1 != NULL);
    ASSERT(p2 != NULL);
    ASSERT(p3 != NULL);

    /* All at different addresses */
    ASSERT(p1 != p2);
    ASSERT(p2 != p3);

    /* Headers have correct obj_type */
    ASSERT_INT_EQ(gc_header_of(p1)->obj_type, OBJ_HEAP_I64);
    ASSERT_INT_EQ(gc_header_of(p2)->obj_type, OBJ_HEAP_U64);
    ASSERT_INT_EQ(gc_header_of(p3)->obj_type, OBJ_STRING);

    /* All fit in a single block (small allocations) */
    ASSERT(heap.blocks != NULL);
    ASSERT(heap.blocks->next == NULL);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_header_recovery(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    void *p = gc_alloc(&heap, OBJ_CLOSURE, 64);
    ASSERT(p != NULL);

    /* gc_header_of(payload) == payload - sizeof(GCHeader) */
    GCHeader *hdr = gc_header_of(p);
    ASSERT((uint8_t *)hdr + sizeof(GCHeader) == (uint8_t *)p);
    ASSERT_INT_EQ(hdr->obj_type, OBJ_CLOSURE);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_bytes_since_gc(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    ASSERT_SIZE_EQ(heap.bytes_since_gc, 0);

    /* 8 + 8 = 16, aligned 16 */
    gc_alloc(&heap, OBJ_HEAP_I64, 8);
    ASSERT_SIZE_EQ(heap.bytes_since_gc, 16);

    /* 8 + 100 = 108, aligned 112 */
    gc_alloc(&heap, OBJ_STRING, 100);
    ASSERT_SIZE_EQ(heap.bytes_since_gc, 16 + 112);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_fills_block(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Fill one block: 512 line-sized allocations */
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        void *p = gc_alloc(&heap, OBJ_HEAP_I64,
                           GC_LINE_SIZE - sizeof(GCHeader));
        ASSERT(p != NULL);
    }

    /* One block, all lines occupied */
    ASSERT(heap.blocks != NULL);
    ASSERT(heap.blocks->next == NULL);
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        ASSERT_INT_EQ(heap.blocks->line_map[i], GC_LINE_OCCUPIED);
    }

    /* Next allocation triggers a second block */
    void *extra = gc_alloc(&heap, OBJ_HEAP_I64, 8);
    ASSERT(extra != NULL);
    ASSERT(heap.blocks != NULL);
    ASSERT(heap.blocks->next != NULL);
    ASSERT(heap.blocks->next->next == NULL);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_various_sizes(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Mix of tiny, medium, and large allocations */
    void *tiny   = gc_alloc(&heap, OBJ_HEAP_F64, 8);
    void *medium = gc_alloc(&heap, OBJ_MUTABLE_REF, 64);
    void *large  = gc_alloc(&heap, OBJ_HAMT_INTERNAL, 1024);
    void *xlarge = gc_alloc(&heap, OBJ_RRB_LEAF, 4096);

    ASSERT(tiny != NULL);
    ASSERT(medium != NULL);
    ASSERT(large != NULL);
    ASSERT(xlarge != NULL);

    /* Verify headers */
    ASSERT_INT_EQ(gc_header_of(tiny)->obj_type,   OBJ_HEAP_F64);
    ASSERT_INT_EQ(gc_header_of(medium)->obj_type,  OBJ_MUTABLE_REF);
    ASSERT_INT_EQ(gc_header_of(large)->obj_type,   OBJ_HAMT_INTERNAL);
    ASSERT_INT_EQ(gc_header_of(xlarge)->obj_type,  OBJ_RRB_LEAF);

    /* Large allocation (4096+8=4104, aligned 4104) spans ceil(4104/128)=33 lines */
    /* Verify the block has many occupied lines */
    int occupied = 0;
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        if (heap.current_block->line_map[i] == GC_LINE_OCCUPIED)
            occupied++;
    }
    ASSERT(occupied > 33); /* at least the xlarge allocation's lines */

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_too_large(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Allocation larger than a block should fail */
    void *p = gc_alloc(&heap, OBJ_STRING, GC_BLOCK_SIZE);
    ASSERT(p == NULL);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        /* US-002: Block-based heap allocator */
        { "gc_block_pool",         test_gc_block_pool },
        { "gc_alloc_small",        test_gc_alloc_small },
        { "gc_alloc_line_sized",   test_gc_alloc_line_sized },
        { "gc_alloc_multi_line",   test_gc_alloc_multi_line },
        { "gc_alloc_multiple",     test_gc_alloc_multiple },
        { "gc_header_recovery",    test_gc_header_recovery },
        { "gc_bytes_since_gc",     test_gc_bytes_since_gc },
        { "gc_alloc_fills_block",  test_gc_alloc_fills_block },
        { "gc_alloc_various_sizes", test_gc_alloc_various_sizes },
        { "gc_alloc_too_large",    test_gc_alloc_too_large },
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
