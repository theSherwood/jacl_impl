#include "test_helpers.h"
#include "../src/jacl.h"

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
                                                      sizeof(JaclMutableRef) + sizeof(JaclVal));
    ref->type_idx = 0; ref->total_size = sizeof(JaclVal);
    MREF_VAL(ref) = inner;

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
    void *dead3 = gc_alloc(&vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef) + sizeof(JaclVal));
    ((JaclMutableRef *)dead3)->type_idx = 0; ((JaclMutableRef *)dead3)->total_size = sizeof(JaclVal);
    MREF_VAL((JaclMutableRef *)dead3) = JACL_NIL;

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

/* ===== US-006 (M15): GC edge case tests ===== */

static int test_gc_fragmentation_reuse(void) {
    /* Allocate mixed sizes, free some via GC, re-allocate — verify
     * freed line runs are reused without unnecessary block growth. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Phase 1: allocate mixed sizes — 1 rooted, rest dead */
    JaclVal live = jacl_i64(&vm.heap, 42);            /* small (1 line) */
    gc_alloc(&vm.heap, OBJ_STRING, 248);              /* medium: 256 bytes = 2 lines, dead */
    gc_alloc(&vm.heap, OBJ_STRING, 504);              /* multi-line: 512 = 4 lines, dead */
    gc_alloc(&vm.heap, OBJ_STRING, GC_LINE_SIZE * 3); /* 3 lines, dead */
    gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64)); /* small, dead */

    vm.stack[0] = live;
    vm.stack_top = 1;

    ASSERT(vm.heap.blocks != NULL);
    ASSERT(vm.heap.blocks->next == NULL); /* single block */

    gc_collect(&vm.heap, &vm);

    /* After GC: only the live i64's line is OCCUPIED, rest free */
    ASSERT(vm.heap.blocks != NULL);
    ASSERT(vm.heap.blocks->next == NULL);

    /* Phase 2: re-allocate mixed sizes — should fit in freed lines */
    void *r1 = gc_alloc(&vm.heap, OBJ_STRING, 504);              /* 4 lines */
    void *r2 = gc_alloc(&vm.heap, OBJ_STRING, 248);              /* 2 lines */
    void *r3 = gc_alloc(&vm.heap, OBJ_STRING, GC_LINE_SIZE * 3); /* 3 lines */
    void *r4 = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    ASSERT(r1 != NULL);
    ASSERT(r2 != NULL);
    ASSERT(r3 != NULL);
    ASSERT(r4 != NULL);

    /* Still single block — freed space was reused, no block growth */
    ASSERT(vm.heap.blocks != NULL);
    ASSERT(vm.heap.blocks->next == NULL);

    /* Original value survived */
    ASSERT(jacl_as_i64(live) == 42);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_multi_cycle_mixed_lifetimes(void) {
    /* Run 12 GC cycles where each cycle some objects survive and others die.
     * Verify heap doesn't grow unboundedly and mark-bit alternation works. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    for (int cycle = 0; cycle < 12; cycle++) {
        /* 3 survivors per cycle (replace previous cycle's survivors) */
        JaclVal s1 = jacl_i64(&vm.heap, (int64_t)(cycle * 3 + 1));
        JaclVal s2 = jacl_i64(&vm.heap, (int64_t)(cycle * 3 + 2));
        JaclVal s3 = jacl_i64(&vm.heap, (int64_t)(cycle * 3 + 3));

        /* 50 dead objects per cycle */
        for (int j = 0; j < 50; j++) {
            gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
        }

        vm.stack[0] = s1;
        vm.stack[1] = s2;
        vm.stack[2] = s3;
        vm.stack_top = 3;

        gc_collect(&vm.heap, &vm);

        /* Verify mark bit alternation (initial mark=1, toggles each cycle) */
        ASSERT_INT_EQ(vm.heap.current_mark, cycle % 2);

        /* Verify all survivors intact */
        ASSERT(jacl_as_i64(vm.stack[0]) == (int64_t)(cycle * 3 + 1));
        ASSERT(jacl_as_i64(vm.stack[1]) == (int64_t)(cycle * 3 + 2));
        ASSERT(jacl_as_i64(vm.stack[2]) == (int64_t)(cycle * 3 + 3));

        /* bytes_since_gc reset each cycle */
        ASSERT_SIZE_EQ(vm.heap.bytes_since_gc, 0);
    }

    /* After 12 cycles with only 3 live objects, heap should be 1 block */
    ASSERT(vm.heap.blocks != NULL);
    ASSERT(vm.heap.blocks->next == NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static bool test_oom_handler_called = false;
static void test_oom_noop_handler(ThreadHeap *heap, size_t size) {
    (void)heap; (void)size;
    test_oom_handler_called = true;
}

static int test_gc_alloc_pool_exhaustion(void) {
    /* With max_blocks=2, fill both blocks via gc_alloc, then verify
     * the next allocation returns NULL (hits OOM path). */
    BlockPool pool;
    gc_block_pool_init(&pool);
    pool.max_blocks = 2;

    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Override OOM handler to avoid abort */
    void (*saved)(ThreadHeap *, size_t) = gc__oom_handler;
    gc__oom_handler = test_oom_noop_handler;
    test_oom_handler_called = false;

    /* Clear emergency GC callback (may be stale from prior VM-based tests) */
    void (*saved_gc_fn)(void *) = gc__emergency_gc_fn;
    void *saved_gc_ctx = gc__emergency_gc_ctx;
    gc__emergency_gc_fn = NULL;
    gc__emergency_gc_ctx = NULL;

    /* Fill exactly 2 blocks with line-sized allocations */
    for (int i = 0; i < GC_LINES_PER_BLOCK * 2; i++) {
        void *p = gc_alloc(&heap, OBJ_HEAP_I64,
                           GC_LINE_SIZE - sizeof(GCHeader));
        ASSERT(p != NULL);
    }

    /* Next allocation should trigger OOM — no blocks available */
    void *p = gc_alloc(&heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    ASSERT(p == NULL);
    ASSERT(test_oom_handler_called);

    gc__oom_handler = saved;
    gc__emergency_gc_fn = saved_gc_fn;
    gc__emergency_gc_ctx = saved_gc_ctx;
    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

static int test_gc_deferred_block_recycling(void) {
    /* Allocate objects across 3 blocks, let blocks 1-2 die entirely,
     * run gc_sweep_concurrent — verify empty blocks are unlinked inline
     * and returned to the pool. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Fill 2 full blocks with line-sized objects */
    for (int i = 0; i < GC_LINES_PER_BLOCK * 2; i++) {
        gc_alloc(&vm.heap, OBJ_HEAP_I64,
                 GC_LINE_SIZE - sizeof(GCHeader));
    }

    /* Allocate 1 object in a third block — this is the only survivor */
    JaclVal live = jacl_i64(&vm.heap, 999);

    /* Verify 3 blocks */
    {
        int count = 0;
        for (GCBlock *b = vm.heap.blocks; b; b = b->next) count++;
        ASSERT(count == 3);
    }

    /* Mark with only 'live' rooted — blocks 1 and 2 are fully dead */
    vm.stack[0] = live;
    vm.stack_top = 1;
    gc_mark(&vm.heap, &vm);

    /* Run concurrent sweep — empty blocks are unlinked and returned to
     * the pool inline (no deferred phase needed). */
    gc_sweep_concurrent(&vm.heap, vm.heap.current_block, UINT32_MAX,
                        vm.heap.current_mark, vm.heap.pool);

    /* Heap should have only 1 block remaining (the current_block with live) */
    {
        int count = 0;
        for (GCBlock *b = vm.heap.blocks; b; b = b->next) count++;
        ASSERT(count == 1);
    }

    /* Pool free list should have grown by at least 2 */
    {
        int free_count = 0;
        for (GCBlock *f = vm.heap.pool->free_list; f; f = f->next)
            free_count++;
        ASSERT(free_count >= 2);
    }

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* US-002 (M16): Grey buffer realloc safety regression test */
static int test_gc_grey_buffer_drain_after_realloc(void) {
    /* Push GREY_BUF_INIT_CAP * 3 entries to force at least one realloc,
     * then verify all entries are readable and correct, and cap grew. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    GreyBuffer gb;
    grey_buf_init(&gb);

    uint32_t total = GREY_BUF_INIT_CAP * 3;
    uint32_t orig_cap = gb.cap;

    /* Allocate real GC heap objects and push them into the grey buffer */
    for (uint32_t i = 0; i < total; i++) {
        JaclVal v = jacl_i64(&vm.heap, (int64_t)i);
        grey_buf_push(&gb, v);
    }

    /* Verify cap grew (at least one realloc occurred) */
    ASSERT(gb.cap > orig_cap);

    /* Verify count matches */
    ASSERT(gb.count == total);

    /* Verify all entries are readable and point to correct heap objects */
    for (uint32_t i = 0; i < total; i++) {
        JaclVal v = gb.entries[i];
        ASSERT(jacl_is_heap_type(v));
        /* Decode the heap i64 and verify its value */
        JaclHeapI64 *h = (JaclHeapI64 *)jacl_as_ptr(v);
        ASSERT(h->value == (int64_t)i);
    }

    grey_buf_destroy(&gb);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* US-008 (M16): Concurrent allocation stress test */

#define STRESS_THREADS      4
#define STRESS_ALLOCS       12500   /* per thread, 50000 total */

typedef struct {
    BlockPool  *pool;
    ThreadHeap  heap;
    int         thread_id;
    void      **ptrs;       /* saved payload pointers */
    size_t     *sizes;      /* saved payload sizes */
    int         alloc_count;
} StressThreadCtx;

static void *stress_alloc_thread(void *arg) {
    StressThreadCtx *ctx = (StressThreadCtx *)arg;
    gc__emergency_gc_fn = NULL;  /* clear __thread stale callbacks */

    /* Vary sizes: 16, 32, 64, 128, 256, 512, 1024, 2048 */
    static const size_t size_table[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    int n_sizes = (int)(sizeof(size_table) / sizeof(size_table[0]));

    for (int i = 0; i < STRESS_ALLOCS; i++) {
        size_t payload_sz = size_table[i % n_sizes];
        void *p = gc_alloc(&ctx->heap, OBJ_HEAP_I64, payload_sz);
        if (!p) break;

        /* Write known pattern: thread_id repeated across the payload */
        memset(p, (uint8_t)(ctx->thread_id + 1), payload_sz);

        ctx->ptrs[i]  = p;
        ctx->sizes[i] = payload_sz;
        ctx->alloc_count++;
    }
    return NULL;
}

static int test_gc_concurrent_alloc_stress(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);

    StressThreadCtx ctxs[STRESS_THREADS];
    pthread_t threads[STRESS_THREADS];

    for (int t = 0; t < STRESS_THREADS; t++) {
        ctxs[t].pool = &pool;
        gc_heap_init(&ctxs[t].heap, &pool);
        ctxs[t].thread_id   = t;
        ctxs[t].ptrs         = (void **)malloc(STRESS_ALLOCS * sizeof(void *));
        ctxs[t].sizes        = (size_t *)malloc(STRESS_ALLOCS * sizeof(size_t));
        ctxs[t].alloc_count  = 0;
    }

    /* Launch threads */
    for (int t = 0; t < STRESS_THREADS; t++) {
        THREAD_CREATE(&threads[t], NULL, stress_alloc_thread, &ctxs[t]);
    }

    /* Join all */
    for (int t = 0; t < STRESS_THREADS; t++) {
        THREAD_JOIN(threads[t], NULL);
    }

    /* Verify all allocations succeeded */
    int total_allocs = 0;
    for (int t = 0; t < STRESS_THREADS; t++) {
        ASSERT(ctxs[t].alloc_count == STRESS_ALLOCS);
        total_allocs += ctxs[t].alloc_count;
    }
    ASSERT(total_allocs == STRESS_THREADS * STRESS_ALLOCS);

    /* Verify known patterns are intact for all live objects */
    for (int t = 0; t < STRESS_THREADS; t++) {
        uint8_t expected = (uint8_t)(t + 1);
        for (int i = 0; i < ctxs[t].alloc_count; i++) {
            uint8_t *payload = (uint8_t *)ctxs[t].ptrs[i];
            size_t sz = ctxs[t].sizes[i];
            for (size_t b = 0; b < sz; b++) {
                if (payload[b] != expected) {
                    fprintf(stderr, "FAIL: thread %d, alloc %d, byte %zu: "
                            "got %u, expected %u\n",
                            t, i, b, payload[b], expected);
                    return 0;
                }
            }
        }
    }

    /* Verify block pool accounting: total_blocks_allocated matches
     * actual blocks across all heaps plus free list */
    uint32_t counted_blocks = 0;
    for (int t = 0; t < STRESS_THREADS; t++) {
        for (GCBlock *b = ctxs[t].heap.blocks; b; b = b->next)
            counted_blocks++;
    }
    for (GCBlock *f = pool.free_list; f; f = f->next)
        counted_blocks++;
    ASSERT_U32_EQ(pool.total_blocks_allocated, counted_blocks);

    /* Cleanup */
    for (int t = 0; t < STRESS_THREADS; t++) {
        gc_heap_destroy(&ctxs[t].heap);
        free(ctxs[t].ptrs);
        free(ctxs[t].sizes);
    }
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* ===== US-010: OOM escalation tier tests ===== */

/* Tier 1: Emergency GC callback that sweeps dead objects, freeing space */
static ThreadHeap *tier1_sweep_heap = NULL;

static void tier1_emergency_sweep(void *ctx) {
    (void)ctx;
    gc_sweep(tier1_sweep_heap);
}

static int test_gc_oom_tier1_emergency_gc(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    pool.max_blocks = 2;

    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Clear stale callbacks */
    gc__emergency_gc_fn  = NULL;
    gc__emergency_gc_ctx = NULL;

    /* Fill both blocks with line-sized allocations.
     * All objects get mark=0; heap->current_mark=1, so all are "dead"
     * from sweep's perspective. */
    for (int i = 0; i < GC_LINES_PER_BLOCK * 2; i++) {
        void *p = gc_alloc(&heap, OBJ_HEAP_I64,
                           GC_LINE_SIZE - sizeof(GCHeader));
        ASSERT(p != NULL);
    }

    /* Install emergency GC callback that sweeps (frees all dead objects) */
    tier1_sweep_heap     = &heap;
    gc__emergency_gc_fn  = tier1_emergency_sweep;
    gc__emergency_gc_ctx = NULL;

    /* Next alloc triggers: pool_get fails → emergency GC sweeps →
     * sweep returns blocks to pool → pool_get retry succeeds */
    void *p = gc_alloc(&heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    ASSERT(p != NULL);

    gc__emergency_gc_fn = NULL;
    tier1_sweep_heap    = NULL;
    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* Tier 2: Heap growth — raise max_blocks to allow a third block */
static int test_gc_oom_tier2_heap_growth(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    pool.max_blocks = 2;

    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Clear stale callbacks — no emergency GC for this test */
    gc__emergency_gc_fn  = NULL;
    gc__emergency_gc_ctx = NULL;

    /* Fill both blocks */
    for (int i = 0; i < GC_LINES_PER_BLOCK * 2; i++) {
        void *p = gc_alloc(&heap, OBJ_HEAP_I64,
                           GC_LINE_SIZE - sizeof(GCHeader));
        ASSERT(p != NULL);
    }

    ASSERT_U32_EQ(pool.total_blocks_allocated, 2);

    /* Raise limit to allow growth — simulates runtime increasing the cap */
    pool.max_blocks = 3;

    /* Next alloc: existing blocks full, pool_get succeeds (2 < 3),
     * allocates a third block */
    void *p = gc_alloc(&heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    ASSERT(p != NULL);
    ASSERT_U32_EQ(pool.total_blocks_allocated, 3);

    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* Tier 3: OOM panic handler — verify it's called with correct args */
static ThreadHeap *tier3_received_heap = NULL;
static size_t      tier3_received_size = 0;
static bool        tier3_handler_called = false;

static void tier3_oom_handler(ThreadHeap *heap, size_t size) {
    tier3_received_heap = heap;
    tier3_received_size = size;
    tier3_handler_called = true;
}

static int test_gc_oom_tier3_panic(void) {
    BlockPool pool;
    gc_block_pool_init(&pool);
    pool.max_blocks = 1;

    ThreadHeap heap;
    gc_heap_init(&heap, &pool);

    /* Clear stale callbacks */
    gc__emergency_gc_fn  = NULL;
    gc__emergency_gc_ctx = NULL;

    /* Override OOM handler */
    void (*saved)(ThreadHeap *, size_t) = gc__oom_handler;
    gc__oom_handler      = tier3_oom_handler;
    tier3_handler_called = false;

    /* Fill the single block */
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        void *p = gc_alloc(&heap, OBJ_HEAP_I64,
                           GC_LINE_SIZE - sizeof(GCHeader));
        ASSERT(p != NULL);
    }

    /* Compute expected request size: sizeof(GCHeader) + payload, aligned to 8 */
    size_t payload = sizeof(JaclHeapI64);
    size_t expected_total = (sizeof(GCHeader) + payload + 7) & ~(size_t)7;

    /* Next alloc should hit tier-3 OOM handler */
    void *p = gc_alloc(&heap, OBJ_HEAP_I64, payload);
    ASSERT(p == NULL);
    ASSERT(tier3_handler_called);
    ASSERT(tier3_received_heap == &heap);
    ASSERT_SIZE_EQ(tier3_received_size, expected_total);

    gc__oom_handler = saved;
    gc_heap_destroy(&heap);
    gc_block_pool_destroy(&pool);
    TEST_PASS();
}

/* ===== US-014: Remove GC tracing for value-type structs ===== */

static int test_gc_inline_struct_bitmap_skip(void) {
    /* Stack slots marked as inline struct data should be skipped by GC.
       Write a bit pattern that looks like a heap pointer tag into a struct slot,
       then run GC — it must NOT crash or follow the garbage pointer. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate a real heap object and put it on the stack as a root */
    JaclVal vi = jacl_i64(&vm.heap, 42);
    vm.stack[0] = vi;

    /* Simulate inline struct data at slots [1, 2]:
       Write a value that looks like a tagged heap pointer (JACL_TAG_STRING with a garbage address) */
    uint64_t fake_heap_val = ((uint64_t)0x05 << 56) | 0xDEADBEEF;
    vm.stack[1] = (JaclVal)fake_heap_val;
    vm.stack[2] = (JaclVal)fake_heap_val;
    vm.stack_top = 3;

    /* Mark slots 1 and 2 as inline struct data */
    BITMAP_SET(vm.inline_slot_bitmap, 1);
    BITMAP_SET(vm.inline_slot_bitmap, 2);

    uint8_t mark = vm.heap.current_mark;

    /* GC should skip slots 1 and 2 — must not crash following garbage pointers */
    gc_mark(&vm.heap, &vm);

    /* The real heap object should be marked */
    ASSERT(gc__is_marked(jacl_as_ptr(vi), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_inline_struct_non_struct_survives(void) {
    /* Non-struct stack values alongside inline struct data must still be traced.
       Verify that a real heap object on the stack survives GC when struct
       bitmap bits are set for adjacent slots. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Slot 0: real heap value */
    JaclVal vi = jacl_i64(&vm.heap, 100);
    vm.stack[0] = vi;

    /* Slot 1: inline struct data (bitmap set) */
    vm.stack[1] = (JaclVal)0xFFFFFFFFFFFFFFFFULL;
    BITMAP_SET(vm.inline_slot_bitmap, 1);

    /* Slot 2: another real heap value */
    JaclVal vf = jacl_f64(&vm.heap, 3.14);
    vm.stack[2] = vf;

    vm.stack_top = 3;

    uint8_t mark = vm.heap.current_mark;
    gc_mark(&vm.heap, &vm);

    /* Both real heap objects survive */
    ASSERT(gc__is_marked(jacl_as_ptr(vi), mark));
    ASSERT(gc__is_marked(jacl_as_ptr(vf), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_sm_field_bitmap_skip(void) {
    /* State machine field slots marked as inline struct data should be skipped.
       Write garbage into SM fields at bitmap-marked positions, then GC. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate a state machine with 4 fields */
    JaclVal sm_val = gc_alloc_state_machine(&vm.heap, 4);
    JaclStateMachine *sm = jacl_as_state_machine(sm_val);

    /* Field 0: real heap value */
    JaclVal vi = jacl_i64(&vm.heap, 99);
    sm->fields[0] = vi;

    /* Fields 1-2: inline struct data (garbage that looks like heap pointer) */
    uint64_t fake_heap_val = ((uint64_t)0x05 << 56) | 0xCAFEBABE;
    sm->fields[1] = (JaclVal)fake_heap_val;
    sm->fields[2] = (JaclVal)fake_heap_val;
    BITMAP_SET(sm->field_inline_bitmap, 1);
    BITMAP_SET(sm->field_inline_bitmap, 2);

    /* Field 3: real heap value */
    JaclVal vf = jacl_f64(&vm.heap, 2.71);
    sm->fields[3] = vf;

    /* Root the SM on the stack */
    vm.stack[0] = sm_val;
    vm.stack_top = 1;

    uint8_t mark = vm.heap.current_mark;
    gc_mark(&vm.heap, &vm);

    /* Real heap objects in SM fields survive */
    ASSERT(gc__is_marked(jacl_as_ptr(vi), mark));
    ASSERT(gc__is_marked(jacl_as_ptr(vf), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_value_type_struct_no_trace(void) {
    /* A heap-allocated JaclStruct with is_value_type=true should not
       have its fields traced by GC (no reference fields).
       Use gc_collect which sets gc__struct_registry internally. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Set up a minimal struct registry with a value-type struct */
    StructTypeRegistry reg;
    reg.defs = (StructTypeDef**)calloc(4, sizeof(StructTypeDef*));
    reg.count = 1;
    reg.capacity = 4;
    reg.arena = &arena;
    reg.defs[0] = NULL; /* reserved */

    /* Create a struct type def with 2 i32 fields (value type) */
    size_t sdef_sz = sizeof(StructTypeDef) + 2 * sizeof(StructTypeField);
    StructTypeDef *sdef = (StructTypeDef*)arena_alloc(&arena, sdef_sz);
    sdef->name = "Point";
    sdef->name_len = 5;
    sdef->name_val = JACL_NIL;
    sdef->field_count = 2;
    sdef->total_size = 8;
    sdef->alignment = 4;
    sdef->is_value_type = true;
    sdef->fields[0] = (StructTypeField){ "x", 1, TYPE_I32, 0, 0, 4 };
    sdef->fields[1] = (StructTypeField){ "y", 1, TYPE_I32, 0, 4, 4 };
    reg.defs[1] = sdef;
    reg.count = 2;

    vm.struct_registry = &reg;

    /* Allocate a JaclStruct and put i32 data */
    JaclStruct *s = (JaclStruct*)gc_alloc(&vm.heap, OBJ_STRUCT,
                                           sizeof(JaclStruct) + 8);
    s->type_idx = 1;
    s->total_size = 8;
    int32_t x = 42, y = 99;
    memcpy(s->data, &x, 4);
    memcpy(s->data + 4, &y, 4);

    /* Root the struct on the stack */
    vm.stack[0] = jacl_struct_val(s);
    vm.stack_top = 1;

    /* gc_collect sets gc__struct_registry from vm.struct_registry internally */
    gc_collect(&vm.heap, &vm);

    /* After gc_collect, mark is toggled. Live objects have previous mark. */
    uint8_t mark = 1 - vm.heap.current_mark;
    ASSERT(gc__is_marked(s, mark));

    free(reg.defs);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_stress_inline_struct_gc_cycles(void) {
    /* Stress test: run multiple GC cycles with inline struct data on the stack.
       Ensures no crashes or memory corruption over repeated collections. */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    for (int cycle = 0; cycle < 20; cycle++) {
        /* Allocate some heap objects */
        JaclVal vi = jacl_i64(&vm.heap, cycle);
        vm.stack[0] = vi;

        /* Simulate inline struct data at slots 1-2 */
        vm.stack[1] = (JaclVal)(0x0512345678000000ULL + (uint64_t)cycle);
        vm.stack[2] = (JaclVal)(0x0EDEADBEEF000000ULL + (uint64_t)cycle);
        BITMAP_SET(vm.inline_slot_bitmap, 1);
        BITMAP_SET(vm.inline_slot_bitmap, 2);

        /* Allocate more objects to fill memory */
        JaclVal vf = jacl_f64(&vm.heap, (double)cycle);
        vm.stack[3] = vf;
        vm.stack_top = 4;

        /* Run a full GC cycle */
        gc_collect(&vm.heap, &vm);

        /* Verify heap objects survived */
        uint8_t mark = vm.heap.current_mark;
        /* After gc_collect, mark is already toggled, so live objects have
           the previous mark value (1 - current) */
        uint8_t prev_mark = 1 - mark;
        ASSERT(gc__is_marked(jacl_as_ptr(vi), prev_mark));
        ASSERT(gc__is_marked(jacl_as_ptr(vf), prev_mark));
    }

    /* Clear bitmap and cleanup */
    BITMAP_CLR(vm.inline_slot_bitmap, 1);
    BITMAP_CLR(vm.inline_slot_bitmap, 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-004: Ctx pool — GC-managed free-list ===== */

/* Helper: set up a minimal struct registry with a ctx struct type.
   ctx has one str field (pwd) at offset 0, mutable. */
static void setup_ctx_registry(StructTypeRegistry *reg, arena_t *arena) {
    reg->defs = (StructTypeDef**)calloc(4, sizeof(StructTypeDef*));
    reg->count = 1; /* 0 is reserved */
    reg->capacity = 4;
    reg->arena = arena;
    reg->ctx_type_idx = 0;
    reg->defs[0] = NULL;

    /* Create ctx struct type: one str field "pwd" */
    size_t sdef_sz = sizeof(StructTypeDef) + 1 * sizeof(StructTypeField);
    StructTypeDef *sdef = (StructTypeDef*)arena_alloc(arena, sdef_sz);
    sdef->name = "ctx";
    sdef->name_len = 3;
    sdef->name_val = JACL_NIL;
    sdef->field_count = 1;
    sdef->total_size = 8;   /* one JaclVal-sized str field */
    sdef->alignment = 8;
    sdef->is_value_type = false;  /* str field = reference type */
    sdef->fields[0] = (StructTypeField){ "pwd", 3, TYPE_STR, 0, 0, 8, true };

    uint32_t idx = reg->count;
    reg->defs[idx] = sdef;
    reg->count = idx + 1;
    reg->ctx_type_idx = idx;
}

static void teardown_ctx_registry(StructTypeRegistry *reg) {
    free(reg->defs);
}

static int test_ctx_pool_alloc_valid(void) {
    /* alloc from pool returns valid ctx struct with correct type_idx */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);

    /* Alloc from pool */
    JaclStruct *s = ctx_pool_alloc(&pool, &vm.heap);
    ASSERT(s != NULL);
    ASSERT_U32_EQ(s->type_idx, reg.ctx_type_idx);
    ASSERT_U32_EQ(s->total_size, 8);

    /* Verify it's a valid GC object */
    GCHeader *hdr = gc_header_of(s);
    ASSERT_INT_EQ(hdr->obj_type, OBJ_STRUCT);

    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_ctx_pool_free_reuse(void) {
    /* free and re-alloc reuses the same object (pool hit) */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);

    /* Drain the pool completely */
    JaclStruct *entries[CTX_POOL_INIT_SIZE];
    for (int i = 0; i < CTX_POOL_INIT_SIZE; i++) {
        entries[i] = ctx_pool_alloc(&pool, &vm.heap);
        ASSERT(entries[i] != NULL);
    }

    /* Free one back */
    JaclStruct *freed = entries[0];
    ctx_pool_free(&pool, freed);

    /* Re-alloc should return the same object */
    JaclStruct *reused = ctx_pool_alloc(&pool, &vm.heap);
    ASSERT(reused == freed);

    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_ctx_pool_exhaustion_fallback(void) {
    /* pool exhaustion falls back to gc_alloc (pool miss) */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);

    /* Drain the entire pool */
    for (int i = 0; i < CTX_POOL_INIT_SIZE; i++) {
        JaclStruct *s = ctx_pool_alloc(&pool, &vm.heap);
        ASSERT(s != NULL);
    }

    /* Pool is now empty — next alloc should fall back to gc_alloc */
    JaclStruct *fallback = ctx_pool_alloc(&pool, &vm.heap);
    ASSERT(fallback != NULL);
    ASSERT_U32_EQ(fallback->type_idx, reg.ctx_type_idx);
    ASSERT_U32_EQ(fallback->total_size, 8);

    /* Verify it's a valid GC object */
    GCHeader *hdr = gc_header_of(fallback);
    ASSERT_INT_EQ(hdr->obj_type, OBJ_STRUCT);

    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_ctx_pool_free_clears_refs(void) {
    /* freed ctx has reference fields cleared (no stale pointers) */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);

    /* Alloc and set a string field */
    JaclStruct *s = ctx_pool_alloc(&pool, &vm.heap);
    ASSERT(s != NULL);

    /* Write a non-nil value to the pwd field (offset 0, str type) */
    JaclVal str_val = jacl_inline_string("hello", 5);
    memcpy(s->data + 0, &str_val, sizeof(JaclVal));

    /* Verify the field is set */
    JaclVal read_val;
    memcpy(&read_val, s->data + 0, sizeof(JaclVal));
    ASSERT(read_val != JACL_NIL);

    /* Free the struct back to pool */
    ctx_pool_free(&pool, s);

    /* Reference fields should be cleared to JACL_NIL.
       Note: data[0] now holds the free-list next pointer, but for the purpose
       of the test, the ctx_pool_free should have written JACL_NIL to the pwd
       field BEFORE storing the next pointer. Let's verify by re-allocating
       and checking the data is zeroed. */
    JaclStruct *reused = ctx_pool_alloc(&pool, &vm.heap);
    ASSERT(reused == s);

    /* After re-alloc, data should be zeroed */
    memcpy(&read_val, reused->data + 0, sizeof(JaclVal));
    ASSERT(read_val == JACL_NIL);

    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_ctx_pool_gc_root(void) {
    /* Pooled entries survive GC when pool is set as GC root via vm->ctx_pool */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);
    vm.ctx_pool = &pool;

    /* Get a pointer to a pooled entry (peek at free list head) */
    uintptr_t head = ATOMIC_LOAD_EXPLICIT(&pool.free_list_head, MEM_ACQUIRE);
    ASSERT(head != 0);
    JaclStruct *pooled = (JaclStruct *)head;

    /* Run GC — pooled entries should survive because pool is a GC root */
    gc_collect(&vm.heap, &vm);

    /* After GC, the pooled struct should still be marked alive */
    uint8_t mark = 1 - vm.heap.current_mark;
    ASSERT(gc__is_marked(pooled, mark));

    /* Alloc from pool should still work */
    JaclStruct *s = ctx_pool_alloc(&pool, &vm.heap);
    ASSERT(s != NULL);
    ASSERT_U32_EQ(s->type_idx, reg.ctx_type_idx);

    vm.ctx_pool = NULL;
    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== US-005: vm->ctx register and GC root integration ===== */

static int test_vm_ctx_set_at_startup(void) {
    /* vm->ctx is set at startup and contains valid struct with pwd field */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);
    vm.ctx_pool = &pool;

    /* Allocate initial ctx (simulating startup) */
    JaclStruct *ctx_struct = ctx_pool_alloc(&pool, &vm.heap);
    ASSERT(ctx_struct != NULL);
    ASSERT_U32_EQ(ctx_struct->type_idx, reg.ctx_type_idx);

    /* Set pwd field to a test string */
    JaclVal pwd_str = jacl_inline_string("/tmp", 4);
    vm__struct_write_field(ctx_struct, 0, TYPE_STR, pwd_str);

    /* Store as vm->ctx */
    vm.ctx = jacl_struct_val(ctx_struct);
    ASSERT(vm.ctx != JACL_NIL);

    /* Read back the struct and verify pwd */
    JaclStruct *read_back = jacl_as_struct_ptr(vm.ctx);
    ASSERT(read_back == ctx_struct);
    JaclVal read_pwd = vm__struct_read_field(&vm.heap, read_back, 0, TYPE_STR);
    ASSERT(read_pwd == pwd_str);

    vm.ctx = JACL_NIL;
    vm.ctx_pool = NULL;
    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_vm_ctx_gc_survives(void) {
    /* GC cycle does not collect ctx or its pwd string */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);
    vm.ctx_pool = &pool;

    /* Allocate ctx and set a heap-allocated string as pwd */
    JaclStruct *ctx_struct = ctx_pool_alloc(&pool, &vm.heap);
    JaclVal pwd_str = jacl_rope_string_create(&vm.heap,
                          (const uint8_t *)"/home/test/long-path-name", 25);
    vm__struct_write_field(ctx_struct, 0, TYPE_STR, pwd_str);
    vm.ctx = jacl_struct_val(ctx_struct);

    /* Run GC — ctx struct and its pwd string should survive */
    gc_collect(&vm.heap, &vm);

    /* After GC, verify ctx struct is still marked alive */
    uint8_t mark = 1 - vm.heap.current_mark;
    ASSERT(gc__is_marked(ctx_struct, mark));

    /* Verify pwd string is also alive (it's a heap object) */
    void *pwd_ptr = jacl_as_ptr(pwd_str);
    ASSERT(gc__is_marked(pwd_ptr, mark));

    /* Verify ctx value is still valid and readable */
    JaclStruct *read_back = jacl_as_struct_ptr(vm.ctx);
    ASSERT(read_back == ctx_struct);
    JaclVal read_pwd = vm__struct_read_field(&vm.heap, read_back, 0, TYPE_STR);
    ASSERT(read_pwd == pwd_str);

    vm.ctx = JACL_NIL;
    vm.ctx_pool = NULL;
    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_vm_ctx_multiple_gc_cycles(void) {
    /* After multiple GC cycles, ctx fields remain valid and accessible */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);
    vm.ctx_pool = &pool;

    /* Allocate ctx with a heap string */
    JaclStruct *ctx_struct = ctx_pool_alloc(&pool, &vm.heap);
    JaclVal pwd_str = jacl_rope_string_create(&vm.heap,
                          (const uint8_t *)"/workspace/project", 18);
    vm__struct_write_field(ctx_struct, 0, TYPE_STR, pwd_str);
    vm.ctx = jacl_struct_val(ctx_struct);

    /* Run multiple GC cycles */
    for (int cycle = 0; cycle < 5; cycle++) {
        /* Allocate some garbage to make GC do real work */
        for (int j = 0; j < 10; j++) {
            jacl_rope_string_create(&vm.heap,
                (const uint8_t *)"garbage string data!", 20);
        }

        gc_collect(&vm.heap, &vm);

        /* After each cycle, ctx struct should survive */
        uint8_t mark = 1 - vm.heap.current_mark;
        ASSERT(gc__is_marked(ctx_struct, mark));

        /* pwd string should also survive */
        void *pwd_ptr = jacl_as_ptr(pwd_str);
        ASSERT(gc__is_marked(pwd_ptr, mark));

        /* Read back to verify data integrity */
        JaclStruct *read_back = jacl_as_struct_ptr(vm.ctx);
        ASSERT(read_back == ctx_struct);
        JaclVal read_pwd = vm__struct_read_field(&vm.heap, read_back, 0, TYPE_STR);
        ASSERT(read_pwd == pwd_str);
    }

    vm.ctx = JACL_NIL;
    vm.ctx_pool = NULL;
    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_ctx_set_write_barrier(void) {
    /* Write barrier fires when updating reference-type ctx field during active GC */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    StructTypeRegistry reg;
    setup_ctx_registry(&reg, &arena);
    vm.struct_registry = &reg;

    JaclCtxPool pool;
    ctx_pool_init(&pool, &vm.heap, &reg);
    vm.ctx_pool = &pool;

    /* Allocate ctx with a heap string */
    JaclStruct *ctx_struct = ctx_pool_alloc(&pool, &vm.heap);
    JaclVal old_str = jacl_rope_string_create(&vm.heap,
                          (const uint8_t *)"/old/path", 9);
    vm__struct_write_field(ctx_struct, 0, TYPE_STR, old_str);
    vm.ctx = jacl_struct_val(ctx_struct);

    /* Set up grey buffer and gc_active to simulate active GC */
    GreyBuffer gb;
    grey_buf_init(&gb);
    volatile uint32_t gc_active = 1;
    vm.grey_buf = &gb;
    vm.gc_active_ptr = (volatile uint32_t *)&gc_active;

    /* Update pwd field with new string — should fire write barrier */
    JaclVal new_str = jacl_rope_string_create(&vm.heap,
                          (const uint8_t *)"/new/path", 9);

    /* Read old value, fire barrier, write new value (same as OP_STRUCT_SET) */
    JaclVal read_old;
    memcpy(&read_old, ctx_struct->data + 0, sizeof(JaclVal));
    gc_write_barrier(&gb, vm.gc_active_ptr, read_old, new_str);
    vm__struct_write_field(ctx_struct, 0, TYPE_STR, new_str);

    /* Grey buffer should contain old and new values */
    ASSERT(gb.count >= 2);  /* old_str + new_str */

    /* Verify the old string was pushed to grey buffer */
    bool found_old = false;
    for (uint32_t i = 0; i < gb.count; i++) {
        if (gb.entries[i] == old_str) found_old = true;
    }
    ASSERT(found_old);

    /* Clean up */
    gc_active = 0;
    vm.grey_buf = NULL;
    vm.gc_active_ptr = NULL;
    grey_buf_destroy(&gb);
    vm.ctx = JACL_NIL;
    vm.ctx_pool = NULL;
    teardown_ctx_registry(&reg);
    vm.struct_registry = NULL;
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
        /* US-006 (M15): GC edge case tests */
        { "gc_fragmentation_reuse",      test_gc_fragmentation_reuse },
        { "gc_multi_cycle_mixed",        test_gc_multi_cycle_mixed_lifetimes },
        { "gc_alloc_pool_exhaustion",    test_gc_alloc_pool_exhaustion },
        { "gc_deferred_recycling",       test_gc_deferred_block_recycling },
        /* US-002 (M16): Grey buffer realloc safety */
        { "gc_grey_buf_drain_realloc",   test_gc_grey_buffer_drain_after_realloc },
        /* US-008 (M16): Concurrent allocation stress */
        { "gc_concurrent_alloc_stress",  test_gc_concurrent_alloc_stress },
        /* US-010 (M16): OOM escalation tiers */
        { "gc_oom_tier1_emergency_gc",   test_gc_oom_tier1_emergency_gc },
        { "gc_oom_tier2_heap_growth",    test_gc_oom_tier2_heap_growth },
        { "gc_oom_tier3_panic",          test_gc_oom_tier3_panic },
        /* US-014: Remove GC tracing for value-type structs */
        { "gc_inline_bitmap_skip",       test_gc_inline_struct_bitmap_skip },
        { "gc_inline_nonstr_survives",   test_gc_inline_struct_non_struct_survives },
        { "gc_sm_field_bitmap_skip",     test_gc_sm_field_bitmap_skip },
        { "gc_value_type_no_trace",      test_gc_value_type_struct_no_trace },
        { "gc_stress_inline_struct",     test_gc_stress_inline_struct_gc_cycles },
        /* US-004: Ctx pool — GC-managed free-list */
        { "ctx_pool_alloc_valid",        test_ctx_pool_alloc_valid },
        { "ctx_pool_free_reuse",         test_ctx_pool_free_reuse },
        { "ctx_pool_exhaustion_fallback",test_ctx_pool_exhaustion_fallback },
        { "ctx_pool_free_clears_refs",   test_ctx_pool_free_clears_refs },
        { "ctx_pool_gc_root",            test_ctx_pool_gc_root },
        /* US-005: vm->ctx register and GC root integration */
        { "vm_ctx_set_at_startup",       test_vm_ctx_set_at_startup },
        { "vm_ctx_gc_survives",          test_vm_ctx_gc_survives },
        { "vm_ctx_multiple_gc_cycles",   test_vm_ctx_multiple_gc_cycles },
        /* US-008: ctx field write barrier */
        { "ctx_set_write_barrier",       test_ctx_set_write_barrier },
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
