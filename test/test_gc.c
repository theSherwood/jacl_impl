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
    /* total = 8 (header) + 8 (payload) = 16, aligned = 16 */
    ASSERT(hdr->alloc_total == 16);

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

/* ===== US-002 (M15): TOCTOU fix in gc_block_pool_get ===== */

static int test_gc_block_pool_limit(void) {
    /* With max_blocks = 2, exactly 2 blocks can be allocated, third returns NULL */
    BlockPool pool;
    gc_block_pool_init(&pool);
    pool.max_blocks = 2;

    GCBlock *b1 = gc_block_pool_get(&pool);
    ASSERT(b1 != NULL);

    GCBlock *b2 = gc_block_pool_get(&pool);
    ASSERT(b2 != NULL);

    /* Third allocation should fail — limit enforced */
    GCBlock *b3 = gc_block_pool_get(&pool);
    ASSERT(b3 == NULL);

    /* Returning a block to the pool and re-getting should work (recycle, not new alloc) */
    gc_block_pool_return(&pool, b1);
    GCBlock *b4 = gc_block_pool_get(&pool);
    ASSERT(b4 != NULL);
    ASSERT(b4 == b1); /* recycled from free list */

    /* Counter should still be 2 (no new allocation) */
    ASSERT(pool.total_blocks_allocated == 2);

    /* Clean up: return blocks to pool then destroy */
    gc_block_pool_return(&pool, b2);
    gc_block_pool_return(&pool, b4);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* ===== US-001 (M15): alloc_total uint16_t overflow boundary ===== */

static int test_gc_alloc_overflow_exact_boundary(void) {
    /* 65528 payload + 8 header = 65536 = GC_BLOCK_SIZE → would overflow uint16_t to 0 */
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    void *p = gc_alloc(&heap, OBJ_STRING, 65528);
    ASSERT(p == NULL);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_just_under_boundary(void) {
    /* 65520 payload + 8 header = 65528 < 65536 → should succeed, alloc_total > 0 */
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    void *p = gc_alloc(&heap, OBJ_STRING, 65520);
    ASSERT(p != NULL);
    ASSERT(gc_header_of(p)->alloc_total > 0);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_alloc_well_within_bounds(void) {
    /* 65512 payload + 8 header = 65520 → well within bounds */
    BlockPool pool;
    gc_block_pool_init(&pool);
    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    void *p = gc_alloc(&heap, OBJ_STRING, 65512);
    ASSERT(p != NULL);
    ASSERT(gc_header_of(p)->alloc_total > 0);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* ===== US-006: Single-threaded mark phase ===== */

/* Helper: check if an object (by payload pointer) is marked with the given mark */
static int gc__is_marked(void *payload, uint8_t mark) {
    return gc_header_of(payload)->mark == mark;
}

static int test_gc_mark_leaf_objects(void) {
    /* Leaf-type objects on the stack should be marked; unreachable should not */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    /* Allocate reachable leaf objects — push onto VM stack */
    JaclVal vi = jacl_i64(&vm.heap, 42);
    JaclVal vu = jacl_u64(&vm.heap, 99);
    JaclVal vf = jacl_f64(&vm.heap, 3.14);
    vm.stack[0] = vi;
    vm.stack[1] = vu;
    vm.stack[2] = vf;
    vm.stack_top = 3;

    /* Allocate unreachable object (not on stack, not in env) */
    void *dead = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));

    gc_mark(&vm.heap, &vm);

    /* Reachable objects marked */
    ASSERT(gc__is_marked(jacl_as_ptr(vi), mark));
    ASSERT(gc__is_marked(jacl_as_ptr(vu), mark));
    ASSERT(gc__is_marked(jacl_as_ptr(vf), mark));

    /* Unreachable object NOT marked */
    ASSERT(!gc__is_marked(dead, mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_closure_upvalues(void) {
    /* Closure on stack with upvalue pointing to a heap object — both survive */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    /* Create an i64 as the upvalue */
    JaclVal captured = jacl_i64(&vm.heap, 100);

    /* Create closure with 1 upvalue */
    size_t uv_bytes = sizeof(JaclVal) * 1;
    JaclClosure *cl = (JaclClosure *)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                               sizeof(JaclClosure) + uv_bytes);
    memset(cl, 0, sizeof(JaclClosure));
    cl->upvalue_count = 1;
    cl->upvalues = (JaclVal *)(cl + 1);
    cl->upvalues[0] = captured;

    /* Push closure onto VM stack */
    vm.stack[0] = jacl_closure(cl);
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);

    /* Closure is marked */
    ASSERT(gc__is_marked(cl, mark));
    /* Upvalue (i64) is also marked */
    ASSERT(gc__is_marked(jacl_as_ptr(captured), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_mutable_ref(void) {
    /* MutableRef (box) on stack referencing a heap value — both survive */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    JaclVal inner = jacl_i64(&vm.heap, 55);

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(&vm.heap, OBJ_MUTABLE_REF,
                                                      sizeof(JaclMutableRef));
    ref->value = inner;

    vm.stack[0] = jacl_box_ptr(ref);
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);

    ASSERT(gc__is_marked(ref, mark));
    ASSERT(gc__is_marked(jacl_as_ptr(inner), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_hamt(void) {
    /* HAMT map on stack — all reachable nodes survive */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    /* Create a map with a couple entries using the HAMT API */
    gc__current_heap = &vm.heap;
    JaclVal key1 = jacl_inline_string("a", 1);
    JaclVal val1 = jacl_i64(&vm.heap, 10);
    JaclVal key2 = jacl_inline_string("b", 1);
    JaclVal val2 = jacl_i64(&vm.heap, 20);

    jacl_map_node *map = NULL;
    map = jacl_map_set(map, key1, val1);
    map = jacl_map_set(map, key2, val2);

    /* Push map onto stack as a JaclVal */
    JaclVal map_val = JACL_TAG_MAP | ((uint64_t)(uintptr_t)map & JACL_PAYLOAD_MASK);
    vm.stack[0] = map_val;
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);

    /* Map root node is marked */
    ASSERT(gc__is_marked(map, mark));
    /* The values stored in the map are marked */
    ASSERT(gc__is_marked(jacl_as_ptr(val1), mark));
    ASSERT(gc__is_marked(jacl_as_ptr(val2), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_rrb_vec(void) {
    /* RRB vector on stack — root, tail, and elements survive */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    gc__current_heap = &vm.heap;
    JaclVal elem1 = jacl_i64(&vm.heap, 1);
    JaclVal elem2 = jacl_i64(&vm.heap, 2);
    JaclVal elem3 = jacl_i64(&vm.heap, 3);

    jacl_vec_root *vec = jacl_vec_empty();
    vec = jacl_vec_push_back(vec, elem1);
    vec = jacl_vec_push_back(vec, elem2);
    vec = jacl_vec_push_back(vec, elem3);

    JaclVal vec_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)vec & JACL_PAYLOAD_MASK);
    vm.stack[0] = vec_val;
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);

    /* Root struct is marked */
    ASSERT(gc__is_marked(vec, mark));
    /* Elements are marked */
    ASSERT(gc__is_marked(jacl_as_ptr(elem1), mark));
    ASSERT(gc__is_marked(jacl_as_ptr(elem2), mark));
    ASSERT(gc__is_marked(jacl_as_ptr(elem3), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_global_env(void) {
    /* Values in the global environment are roots */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    /* Put a heap value into the global env */
    JaclVal gval = jacl_i64(&vm.heap, 777);
    JaclVal gname = jacl_inline_string("myvar", 5);
    vm__env_set(&vm, gname, gval);

    /* Nothing on the stack */
    vm.stack_top = 0;

    gc_mark(&vm.heap, &vm);

    ASSERT(gc__is_marked(jacl_as_ptr(gval), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_unreachable_not_marked(void) {
    /* Objects not reachable from any root should NOT be marked */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    /* Allocate several objects but don't push any onto stack or env */
    void *dead1 = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    void *dead2 = gc_alloc(&vm.heap, OBJ_STRING, 32);
    void *dead3 = gc_alloc(&vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    ((JaclMutableRef *)dead3)->value = JACL_NIL;

    vm.stack_top = 0;

    gc_mark(&vm.heap, &vm);

    ASSERT(!gc__is_marked(dead1, mark));
    ASSERT(!gc__is_marked(dead2, mark));
    ASSERT(!gc__is_marked(dead3, mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_alternation(void) {
    /* Mark bit toggles each cycle — second mark uses different bit */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* First mark cycle: current_mark == 1 */
    ASSERT_INT_EQ(vm.heap.current_mark, 1);

    JaclVal vi = jacl_i64(&vm.heap, 42);
    vm.stack[0] = vi;
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);
    ASSERT_INT_EQ(gc_header_of(jacl_as_ptr(vi))->mark, 1);

    /* Toggle mark for next cycle */
    vm.heap.current_mark = 1 - vm.heap.current_mark;
    ASSERT_INT_EQ(vm.heap.current_mark, 0);

    /* Second mark cycle: should overwrite with 0 */
    gc_mark(&vm.heap, &vm);
    ASSERT_INT_EQ(gc_header_of(jacl_as_ptr(vi))->mark, 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_deep_closure_chain(void) {
    /* Chain of closures: cl1 captures cl2 which captures an i64 */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;

    /* Inner value */
    JaclVal leaf = jacl_i64(&vm.heap, 9999);

    /* cl2: captures the leaf */
    size_t uv2_bytes = sizeof(JaclVal) * 1;
    JaclClosure *cl2 = (JaclClosure *)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure) + uv2_bytes);
    memset(cl2, 0, sizeof(JaclClosure));
    cl2->upvalue_count = 1;
    cl2->upvalues = (JaclVal *)(cl2 + 1);
    cl2->upvalues[0] = leaf;

    /* cl1: captures cl2 */
    size_t uv1_bytes = sizeof(JaclVal) * 1;
    JaclClosure *cl1 = (JaclClosure *)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure) + uv1_bytes);
    memset(cl1, 0, sizeof(JaclClosure));
    cl1->upvalue_count = 1;
    cl1->upvalues = (JaclVal *)(cl1 + 1);
    cl1->upvalues[0] = jacl_closure(cl2);

    /* Only cl1 is on the stack */
    vm.stack[0] = jacl_closure(cl1);
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);

    ASSERT(gc__is_marked(cl1, mark));
    ASSERT(gc__is_marked(cl2, mark));
    ASSERT(gc__is_marked(jacl_as_ptr(leaf), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_mark_mixed_graph(void) {
    /* Closure with upvalue pointing to a map containing a vector */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    uint8_t mark = vm.heap.current_mark;
    gc__current_heap = &vm.heap;

    /* Build a vector with one element */
    JaclVal velem = jacl_i64(&vm.heap, 42);
    jacl_vec_root *vec = jacl_vec_empty();
    vec = jacl_vec_push_back(vec, velem);
    JaclVal vec_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)vec & JACL_PAYLOAD_MASK);

    /* Build a map with key "x" → the vector */
    JaclVal mkey = jacl_inline_string("x", 1);
    jacl_map_node *map = NULL;
    map = jacl_map_set(map, mkey, vec_val);
    JaclVal map_val = JACL_TAG_MAP | ((uint64_t)(uintptr_t)map & JACL_PAYLOAD_MASK);

    /* Build a closure that captures the map */
    size_t uv_bytes = sizeof(JaclVal) * 1;
    JaclClosure *cl = (JaclClosure *)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                               sizeof(JaclClosure) + uv_bytes);
    memset(cl, 0, sizeof(JaclClosure));
    cl->upvalue_count = 1;
    cl->upvalues = (JaclVal *)(cl + 1);
    cl->upvalues[0] = map_val;

    /* Only the closure is a root */
    vm.stack[0] = jacl_closure(cl);
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);

    /* The entire chain is reachable */
    ASSERT(gc__is_marked(cl, mark));
    ASSERT(gc__is_marked(map, mark));
    ASSERT(gc__is_marked(vec, mark));
    ASSERT(gc__is_marked(jacl_as_ptr(velem), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-007: Sweep phase and GC trigger ===== */

static int test_gc_sweep_dead_objects_freed(void) {
    /* Dead objects' lines should become FREE after sweep */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate objects but don't root them */
    gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    gc_alloc(&vm.heap, OBJ_HEAP_U64, sizeof(JaclHeapU64));
    gc_alloc(&vm.heap, OBJ_HEAP_F64, sizeof(JaclHeapF64));
    vm.stack_top = 0;

    /* Mark (nothing reachable) then sweep */
    gc_mark(&vm.heap, &vm);
    gc_sweep(&vm.heap);

    /* All lines should be free (all objects were dead) */
    /* Block should have been returned to pool since it's all-free */
    ASSERT(vm.heap.blocks == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_sweep_live_objects_survive(void) {
    /* Live objects' lines remain OCCUPIED after sweep */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal vi = jacl_i64(&vm.heap, 42);
    JaclVal vu = jacl_u64(&vm.heap, 99);
    vm.stack[0] = vi;
    vm.stack[1] = vu;
    vm.stack_top = 2;

    /* Also allocate a dead object */
    gc_alloc(&vm.heap, OBJ_HEAP_F64, sizeof(JaclHeapF64));

    gc_mark(&vm.heap, &vm);
    gc_sweep(&vm.heap);

    /* Block still exists (has live objects) */
    ASSERT(vm.heap.blocks != NULL);

    /* Live objects' data intact */
    ASSERT(jacl_as_i64(vi) == 42);
    ASSERT(jacl_as_u64(vu) == 99);

    /* Lines for live objects are OCCUPIED */
    GCBlock *block = vm.heap.blocks;
    ASSERT_INT_EQ(block->line_map[0], GC_LINE_OCCUPIED);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_sweep_line_granularity(void) {
    /* Single live object in a block doesn't prevent reclaiming other lines */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate objects across multiple lines */
    JaclVal live_val = jacl_i64(&vm.heap, 1);  /* line 0 */
    gc_alloc(&vm.heap, OBJ_STRING, GC_LINE_SIZE - sizeof(GCHeader)); /* fills rest of line 0 + line 1 */
    gc_alloc(&vm.heap, OBJ_STRING, GC_LINE_SIZE * 2);  /* lines 2-3+ */

    /* Only root the first small object */
    vm.stack[0] = live_val;
    vm.stack_top = 1;

    gc_mark(&vm.heap, &vm);
    gc_sweep(&vm.heap);

    GCBlock *block = vm.heap.blocks;
    ASSERT(block != NULL);

    /* Line 0 should be OCCUPIED (live object) */
    ASSERT_INT_EQ(block->line_map[0], GC_LINE_OCCUPIED);

    /* Lines further out should be FREE (dead objects reclaimed) */
    int free_count = 0;
    for (int i = 1; i < GC_LINES_PER_BLOCK; i++) {
        if (block->line_map[i] == GC_LINE_FREE) free_count++;
    }
    ASSERT(free_count > 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_sweep_block_recycling(void) {
    /* Fully dead blocks are returned to the global pool */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate enough to use two blocks */
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        gc_alloc(&vm.heap, OBJ_HEAP_I64, GC_LINE_SIZE - sizeof(GCHeader));
    }
    /* This triggers a second block */
    JaclVal live = jacl_i64(&vm.heap, 999);
    vm.stack[0] = live;
    vm.stack_top = 1;

    /* Two blocks in the heap */
    ASSERT(vm.heap.blocks != NULL);
    ASSERT(vm.heap.blocks->next != NULL);

    gc_mark(&vm.heap, &vm);
    gc_sweep(&vm.heap);

    /* Only one block should remain (the one with the live object) */
    ASSERT(vm.heap.blocks != NULL);
    ASSERT(vm.heap.blocks->next == NULL);

    /* The live value is intact */
    ASSERT(jacl_as_i64(live) == 999);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_collect_full_cycle(void) {
    /* gc_collect runs mark + sweep + toggles mark + resets counter */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    ASSERT_INT_EQ(vm.heap.current_mark, 1);

    JaclVal live = jacl_i64(&vm.heap, 42);
    vm.stack[0] = live;
    vm.stack_top = 1;

    /* Allocate dead objects to increase byte counter */
    for (int i = 0; i < 100; i++) {
        gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    }
    size_t before_bytes = vm.heap.bytes_since_gc;
    ASSERT(before_bytes > 0);

    gc_collect(&vm.heap, &vm);

    /* Mark toggled */
    ASSERT_INT_EQ(vm.heap.current_mark, 0);
    /* Byte counter reset */
    ASSERT_SIZE_EQ(vm.heap.bytes_since_gc, 0);
    /* needs_gc cleared */
    ASSERT(!vm.heap.needs_gc);
    /* Live value survives */
    ASSERT(jacl_as_i64(live) == 42);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_collect_multiple_cycles(void) {
    /* Multiple GC cycles: objects allocated between cycles survive if rooted */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Cycle 1 */
    JaclVal v1 = jacl_i64(&vm.heap, 100);
    vm.stack[0] = v1;
    vm.stack_top = 1;
    gc_collect(&vm.heap, &vm);
    ASSERT(jacl_as_i64(v1) == 100);

    /* Cycle 2: allocate more, root some */
    JaclVal v2 = jacl_i64(&vm.heap, 200);
    gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64)); /* dead */
    vm.stack[1] = v2;
    vm.stack_top = 2;
    gc_collect(&vm.heap, &vm);
    ASSERT(jacl_as_i64(v1) == 100);
    ASSERT(jacl_as_i64(v2) == 200);

    /* Cycle 3: drop v1 from roots */
    vm.stack[0] = v2;
    vm.stack_top = 1;
    gc_collect(&vm.heap, &vm);
    ASSERT(jacl_as_i64(v2) == 200);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_collect_closure_with_upvalues(void) {
    /* Closure survives GC, its upvalues and chunk constants survive too */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Create a captured value */
    JaclVal captured = jacl_i64(&vm.heap, 777);

    /* Create closure with 1 upvalue */
    size_t uv_bytes = sizeof(JaclVal) * 1;
    JaclClosure *cl = (JaclClosure *)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                               sizeof(JaclClosure) + uv_bytes);
    memset(cl, 0, sizeof(JaclClosure));
    cl->upvalue_count = 1;
    cl->upvalues = (JaclVal *)(cl + 1);
    cl->upvalues[0] = captured;

    vm.stack[0] = jacl_closure(cl);
    vm.stack_top = 1;

    /* Run 3 GC cycles */
    for (int i = 0; i < 3; i++) {
        gc_collect(&vm.heap, &vm);
    }

    /* Both closure and captured value survive */
    JaclClosure *cl2 = jacl_as_closure(vm.stack[0]);
    ASSERT(cl2 == cl);
    ASSERT(jacl_as_i64(cl2->upvalues[0]) == 777);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_collect_map_survives(void) {
    /* HAMT map and its values survive GC */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    gc__current_heap = &vm.heap;

    JaclVal key = jacl_inline_string("k", 1);
    JaclVal val = jacl_i64(&vm.heap, 42);

    jacl_map_node *map = NULL;
    map = jacl_map_set(map, key, val);

    JaclVal map_val = JACL_TAG_MAP | ((uint64_t)(uintptr_t)map & JACL_PAYLOAD_MASK);
    vm.stack[0] = map_val;
    vm.stack_top = 1;

    gc_collect(&vm.heap, &vm);

    /* Map survived — can still look up the value */
    jacl_map_node *map2 = (jacl_map_node *)(uintptr_t)(vm.stack[0] & JACL_PAYLOAD_MASK);
    JaclVal got = jacl_map_get(map2, key);
    ASSERT(jacl_as_i64(got) == 42);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_collect_vec_survives(void) {
    /* RRB vector and its elements survive GC */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    gc__current_heap = &vm.heap;

    JaclVal e1 = jacl_i64(&vm.heap, 10);
    JaclVal e2 = jacl_i64(&vm.heap, 20);
    jacl_vec_root *vec = jacl_vec_empty();
    vec = jacl_vec_push_back(vec, e1);
    vec = jacl_vec_push_back(vec, e2);

    JaclVal vec_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)vec & JACL_PAYLOAD_MASK);
    vm.stack[0] = vec_val;
    vm.stack_top = 1;

    gc_collect(&vm.heap, &vm);

    /* Vector survived — elements accessible */
    jacl_vec_root *vec2 = (jacl_vec_root *)(uintptr_t)(vm.stack[0] & JACL_PAYLOAD_MASK);
    ASSERT(jacl_as_i64(jacl_vec_get(vec2, 0).value) == 10);
    ASSERT(jacl_as_i64(jacl_vec_get(vec2, 1).value) == 20);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_trigger_threshold(void) {
    /* needs_gc flag is set when bytes_since_gc exceeds threshold */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    ASSERT(!vm.heap.needs_gc);

    /* Allocate until threshold is exceeded */
    while (vm.heap.bytes_since_gc <= GC_THRESHOLD) {
        void *p = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
        ASSERT(p != NULL);
    }

    ASSERT(vm.heap.needs_gc);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_alloc_after_sweep(void) {
    /* Allocation into freed lines after sweep works correctly */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate some objects, root only one */
    JaclVal live = jacl_i64(&vm.heap, 1);
    gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64)); /* dead */
    gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64)); /* dead */
    vm.stack[0] = live;
    vm.stack_top = 1;

    gc_collect(&vm.heap, &vm);

    /* Now allocate again — should use freed lines */
    JaclVal v2 = jacl_i64(&vm.heap, 2);
    JaclVal v3 = jacl_i64(&vm.heap, 3);
    ASSERT(jacl_as_i64(v2) == 2);
    ASSERT(jacl_as_i64(v3) == 3);

    /* Original live value still intact */
    ASSERT(jacl_as_i64(live) == 1);

    vm_destroy(&vm);
    arena_destroy(&arena);
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
        /* US-002 (M15): TOCTOU fix in block pool limit */
        { "gc_block_pool_limit",     test_gc_block_pool_limit },
        /* US-001 (M15): alloc_total overflow boundary */
        { "gc_alloc_overflow_exact", test_gc_alloc_overflow_exact_boundary },
        { "gc_alloc_just_under",     test_gc_alloc_just_under_boundary },
        { "gc_alloc_well_within",    test_gc_alloc_well_within_bounds },
        /* US-006: Single-threaded mark phase */
        { "gc_mark_leaf_objects",       test_gc_mark_leaf_objects },
        { "gc_mark_closure_upvalues",   test_gc_mark_closure_upvalues },
        { "gc_mark_mutable_ref",        test_gc_mark_mutable_ref },
        { "gc_mark_hamt",               test_gc_mark_hamt },
        { "gc_mark_rrb_vec",            test_gc_mark_rrb_vec },
        { "gc_mark_global_env",         test_gc_mark_global_env },
        { "gc_mark_unreachable",        test_gc_mark_unreachable_not_marked },
        { "gc_mark_alternation",        test_gc_mark_alternation },
        { "gc_mark_deep_closure_chain", test_gc_mark_deep_closure_chain },
        { "gc_mark_mixed_graph",        test_gc_mark_mixed_graph },
        /* US-007: Sweep phase and GC trigger */
        { "gc_sweep_dead_freed",        test_gc_sweep_dead_objects_freed },
        { "gc_sweep_live_survive",      test_gc_sweep_live_objects_survive },
        { "gc_sweep_line_granularity",  test_gc_sweep_line_granularity },
        { "gc_sweep_block_recycling",   test_gc_sweep_block_recycling },
        { "gc_collect_full_cycle",      test_gc_collect_full_cycle },
        { "gc_collect_multiple_cycles", test_gc_collect_multiple_cycles },
        { "gc_collect_closure_upvalues",test_gc_collect_closure_with_upvalues },
        { "gc_collect_map_survives",    test_gc_collect_map_survives },
        { "gc_collect_vec_survives",    test_gc_collect_vec_survives },
        { "gc_trigger_threshold",       test_gc_trigger_threshold },
        { "gc_alloc_after_sweep",       test_gc_alloc_after_sweep },
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
