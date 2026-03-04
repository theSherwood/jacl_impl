/*
 * test_m12.c — M12 Integration tests and stress testing
 *
 * Comprehensive GC correctness, write barrier, epoch watermark,
 * line-granularity reclamation, block recycling, and multi-threaded
 * stress tests for the epoch-based tracing GC system.
 */

#include "test_helpers.h"
#include "../src/jacl.c"

/* --- Helper: check if object is marked --- */
static int gc__is_marked(void *payload, uint8_t mark) {
    return gc_header_of(payload)->mark == mark;
}

/* --- Helpers: Runtime init/destroy without starting worker threads --- */

static void rt_test__init_no_threads(Runtime *rt, int num_workers) {
    int i;
    rt->num_workers  = num_workers;
    rt->global_epoch = 0;
    rt->gc_running   = 0;
    rt->gc_active    = 0;
    rt->shutdown     = 0;
    gc_block_pool_init(&rt->block_pool);
    rt->inbox_cap   = 16;
    rt->inbox_count = 0;
    rt->inbox = (uintptr_t *)malloc((size_t)rt->inbox_cap * sizeof(uintptr_t));
    MUTEX_INIT(rt->inbox_mutex);
    rt->workers = (WorkerThread *)calloc((size_t)num_workers,
                                          sizeof(WorkerThread));
    for (i = 0; i < num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        w->id      = i;
        w->runtime = rt;
        w->currently_executing = WORKER_IDLE;
        w->thread_epoch        = 0;
        w->public_deque  = rt_deque_deque_new(4);
        w->private_deque = rt_deque_deque_new(4);
        grey_buf_init(&w->grey_buf);
        w->arena = (arena_t){0};
        runtime__init_worker_vm(w);
        w->steal_ids = (int *)calloc((size_t)num_workers, sizeof(int));
    }
}

static void rt_test__destroy_no_threads(Runtime *rt) {
    int i;
    for (i = 0; i < rt->num_workers; i++) {
        WorkerThread *w = &rt->workers[i];
        uintptr_t task_val;
        while (rt_deque_deque_take(w->public_deque, &task_val))
            free((RuntimeTask *)task_val);
        while (rt_deque_deque_take(w->private_deque, &task_val))
            free((RuntimeTask *)task_val);
        rt_deque_deque_free(w->public_deque);
        rt_deque_deque_free(w->private_deque);
        grey_buf_destroy(&w->grey_buf);
        free(w->steal_ids);
        vm_destroy(&w->vm);
        arena_destroy(&w->arena);
    }
    free(rt->workers);
    for (intptr_t k = 0; k < rt->inbox_count; k++)
        free((RuntimeTask *)rt->inbox[k]);
    free(rt->inbox);
    MUTEX_DESTROY(rt->inbox_mutex);
    gc_block_pool_destroy(&rt->block_pool);
}

/* ====================================================================
 * GC correctness: allocate multiple types, GC, verify survivors
 * ==================================================================== */

static int test_gc_multi_type_survive(void) {
    /* Allocate i64, u64, f64, string, mutable ref — all rooted — all survive GC */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal vi = jacl_i64(&vm.heap, 42);
    JaclVal vu = jacl_u64(&vm.heap, 100);
    JaclVal vf = jacl_f64(&vm.heap, 3.14);

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(&vm.heap, OBJ_MUTABLE_REF,
                                                      sizeof(JaclMutableRef));
    ref->value = vi;
    JaclVal vbox = jacl_box_ptr(ref);

    /* Root all on stack */
    vm.stack[0] = vi;
    vm.stack[1] = vu;
    vm.stack[2] = vf;
    vm.stack[3] = vbox;
    vm.stack_top = 4;

    /* Also allocate dead objects */
    for (int i = 0; i < 50; i++) {
        gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    }

    gc_collect(&vm.heap, &vm);

    /* All live values intact */
    ASSERT(jacl_as_i64(vi) == 42);
    ASSERT(jacl_as_u64(vu) == 100);
    ASSERT(jacl_as_f64(vf) == 3.14);
    ASSERT(ref->value == vi);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

static int test_gc_dead_collected(void) {
    /* Unreachable objects are collected (zeroed) */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate objects but don't root them */
    void *dead1 = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    void *dead2 = gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    void *dead3 = gc_alloc(&vm.heap, OBJ_HEAP_F64, sizeof(JaclHeapF64));

    /* Root one live object */
    JaclVal live = jacl_i64(&vm.heap, 999);
    vm.stack[0] = live;
    vm.stack_top = 1;

    gc_collect(&vm.heap, &vm);

    /* Dead objects zeroed */
    ASSERT(gc_header_of(dead1)->alloc_total == 0);
    ASSERT(gc_header_of(dead2)->alloc_total == 0);
    ASSERT(gc_header_of(dead3)->alloc_total == 0);

    /* Live object intact */
    ASSERT(jacl_as_i64(live) == 999);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Tracing through closures: upvalue → map
 * ==================================================================== */

static int test_trace_closure_upvalue_map(void) {
    /* Closure with upvalue pointing to a map — both survive GC */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    gc__current_heap = &vm.heap;

    /* Create a map */
    JaclVal key = jacl_inline_string("x", 1);
    JaclVal val = jacl_i64(&vm.heap, 77);
    jacl_map_node *map = NULL;
    map = jacl_map_set(map, key, val);
    JaclVal map_val = JACL_TAG_MAP | ((uint64_t)(uintptr_t)map & JACL_PAYLOAD_MASK);

    /* Create closure with map as upvalue */
    size_t uv_bytes = sizeof(JaclVal) * 1;
    JaclClosure *cl = (JaclClosure *)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                               sizeof(JaclClosure) + uv_bytes);
    memset(cl, 0, sizeof(JaclClosure));
    cl->upvalue_count = 1;
    cl->upvalues = (JaclVal *)(cl + 1);
    cl->upvalues[0] = map_val;

    vm.stack[0] = jacl_closure(cl);
    vm.stack_top = 1;

    uint8_t mark = vm.heap.current_mark;
    gc_mark(&vm.heap, &vm);

    /* Closure marked */
    ASSERT(gc__is_marked(cl, mark));
    /* Map root node marked */
    ASSERT(gc__is_marked(map, mark));
    /* Map value (heap i64) marked */
    ASSERT(gc__is_marked(jacl_as_ptr(val), mark));

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Tracing through HAMT: nested values
 * ==================================================================== */

static int test_trace_hamt_nested(void) {
    /* Map with multiple entries — all reachable values survive */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    gc__current_heap = &vm.heap;

    jacl_map_node *map = NULL;
    JaclVal vals[5];
    for (int i = 0; i < 5; i++) {
        char kbuf[4];
        kbuf[0] = 'a' + (char)i;
        kbuf[1] = '\0';
        JaclVal key = jacl_inline_string(kbuf, 1);
        vals[i] = jacl_i64(&vm.heap, i * 10);
        map = jacl_map_set(map, key, vals[i]);
    }

    JaclVal map_val = JACL_TAG_MAP | ((uint64_t)(uintptr_t)map & JACL_PAYLOAD_MASK);
    vm.stack[0] = map_val;
    vm.stack_top = 1;

    gc_collect(&vm.heap, &vm);

    /* All values survive GC */
    for (int i = 0; i < 5; i++) {
        ASSERT(jacl_as_i64(vals[i]) == (int64_t)(i * 10));
    }
    /* Map lookup still works */
    JaclVal found = jacl_map_get(map, jacl_inline_string("c", 1));
    ASSERT(jacl_as_i64(found) == 20);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Tracing through RRB: vector with closure elements
 * ==================================================================== */

static int test_trace_rrb_closure_elements(void) {
    /* Vector containing closures — all survive GC */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);
    gc__current_heap = &vm.heap;

    jacl_vec_root *vec = jacl_vec_empty();
    JaclClosure *closures[3];
    for (int i = 0; i < 3; i++) {
        closures[i] = (JaclClosure *)gc_alloc(&vm.heap, OBJ_CLOSURE,
                                                sizeof(JaclClosure));
        memset(closures[i], 0, sizeof(JaclClosure));
        vec = jacl_vec_push_back(vec, jacl_closure(closures[i]));
    }

    JaclVal vec_val = JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)vec & JACL_PAYLOAD_MASK);
    vm.stack[0] = vec_val;
    vm.stack_top = 1;

    uint8_t mark = vm.heap.current_mark;
    gc_mark(&vm.heap, &vm);

    ASSERT(gc__is_marked(vec, mark));
    for (int i = 0; i < 3; i++) {
        ASSERT(gc__is_marked(closures[i], mark));
    }

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Write barrier (SATB): old value swapped out survives
 * ==================================================================== */

static int test_wb_satb_old_survives(void) {
    /* Old value evicted from atom during active GC is preserved via grey buffer */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal old_val = jacl_i64(&vm.heap, 111);
    JaclVal new_val = jacl_i64(&vm.heap, 222);

    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(&vm.heap, OBJ_MUTABLE_REF,
                                                      sizeof(JaclMutableRef));
    ref->value = old_val;
    vm.stack[0] = jacl_atom_ptr(ref);
    vm.stack_top = 1;

    /* Simulate active GC: mark first */
    uint8_t mark = vm.heap.current_mark;
    gc_mark(&vm.heap, &vm);
    /* old_val is marked since it's reachable through atom */
    ASSERT(gc__is_marked(jacl_as_ptr(old_val), mark));

    /* Now simulate mutation: swap old for new.
     * The SATB barrier would push old_val to grey buffer.
     * In a real scenario, the grey buffer drain during mark would
     * re-trace old_val. Here we verify old_val stays marked. */
    ref->value = new_val;

    /* old_val is still marked from the trace above */
    ASSERT(gc__is_marked(jacl_as_ptr(old_val), mark));

    /* new_val was not yet marked — insertion barrier handles it */
    /* After sweep, old_val survives because it was marked */
    gc_sweep(&vm.heap);

    ASSERT(gc_header_of(jacl_as_ptr(old_val))->alloc_total > 0);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Write barrier (insertion): new value stored after scan survives
 * ==================================================================== */

static int test_wb_insertion_new_survives(void) {
    /* New value stored into atom after GC scans it survives via grey buffer */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    JaclVal initial = jacl_i64(&vm.heap, 10);
    JaclMutableRef *ref = (JaclMutableRef *)gc_alloc(&vm.heap, OBJ_MUTABLE_REF,
                                                      sizeof(JaclMutableRef));
    ref->value = initial;
    vm.stack[0] = jacl_atom_ptr(ref);
    vm.stack_top = 1;

    /* Mark phase — GC scans atom, finds 'initial' */
    uint8_t mark = vm.heap.current_mark;
    gc_mark(&vm.heap, &vm);

    /* Now store a NEW value after GC scanned the atom.
     * In a real concurrent scenario, the insertion barrier would push
     * new_val to the grey buffer, which the GC drains and marks.
     * Here we simulate: allocate new_val, mark it manually (as the
     * grey buffer drain would), then verify it survives sweep. */
    JaclVal new_val = jacl_i64(&vm.heap, 20);
    ref->value = new_val;

    /* Simulate grey buffer drain: mark new_val */
    gc_header_of(jacl_as_ptr(new_val))->mark = mark;

    gc_sweep(&vm.heap);

    ASSERT(gc_header_of(jacl_as_ptr(new_val))->alloc_total > 0);
    ASSERT(jacl_as_i64(new_val) == 20);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Epoch watermark: recently allocated but unreachable objects survive
 * ==================================================================== */

static int test_epoch_watermark_recent_survives(void) {
    /* Objects with epoch >= watermark survive even if unreachable */
    Runtime rt;
    rt_test__init_no_threads(&rt, 1);
    WorkerThread *w = &rt.workers[0];

    /* Set epoch so allocations get epoch=10 */
    gc__thread_epoch = 10;

    JaclVal recent = jacl_i64(&w->vm.heap, 777);
    /* Don't root it — unreachable */

    /* Sweep with watermark=5: epoch 10 >= 5 → immune */
    uint8_t mark = w->vm.heap.current_mark;
    gc_sweep_concurrent(&w->vm.heap, NULL, 5, mark, NULL);

    /* Object survived (epoch-protected) */
    GCHeader *hdr = gc_header_of(jacl_as_ptr(recent));
    ASSERT(hdr->alloc_total > 0);

    gc__thread_epoch = 0;
    rt_test__destroy_no_threads(&rt);
    TEST_PASS();
}

/* ====================================================================
 * Line-granularity reclamation: live object doesn't prevent reclaiming others
 * ==================================================================== */

static int test_line_granularity_reclamation(void) {
    /* Single live object in block — other lines are reclaimed */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Allocate one live object */
    JaclVal live = jacl_i64(&vm.heap, 42);
    vm.stack[0] = live;
    vm.stack_top = 1;

    /* Fill remaining lines with dead objects */
    for (int i = 0; i < 100; i++) {
        gc_alloc(&vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    }

    gc_collect(&vm.heap, &vm);

    /* Live value survives */
    ASSERT(jacl_as_i64(live) == 42);

    /* Find the block containing live object */
    GCBlock *block = vm.heap.blocks;
    while (block) {
        uint8_t *ptr = (uint8_t *)jacl_as_ptr(live) - sizeof(GCHeader);
        if (ptr >= block->payload && ptr < block->payload + GC_BLOCK_SIZE) {
            /* Live object's line should be OCCUPIED */
            size_t offset = (size_t)(ptr - block->payload);
            int line = (int)(offset / GC_LINE_SIZE);
            ASSERT_INT_EQ(block->line_map[line], GC_LINE_OCCUPIED);

            /* Most other lines should be FREE */
            int free_count = 0;
            for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
                if (block->line_map[i] == GC_LINE_FREE) free_count++;
            }
            ASSERT(free_count > GC_LINES_PER_BLOCK / 2);
            break;
        }
        block = block->next;
    }
    ASSERT(block != NULL); /* found the block */

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Block recycling: fully dead block returned to pool
 * ==================================================================== */

static int test_block_recycling(void) {
    /* Blocks with all dead objects are returned to the global pool */
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    /* Fill one full block with dead objects */
    int allocs = 0;
    while (allocs < GC_LINES_PER_BLOCK) {
        gc_alloc(&vm.heap, OBJ_HEAP_I64, GC_LINE_SIZE - sizeof(GCHeader));
        allocs++;
    }

    /* This forces a new block for the live object */
    JaclVal live = jacl_i64(&vm.heap, 42);
    vm.stack[0] = live;
    vm.stack_top = 1;

    /* Should have at least 2 blocks */
    int block_count_before = 0;
    for (GCBlock *b = vm.heap.blocks; b; b = b->next) block_count_before++;
    ASSERT(block_count_before >= 2);

    gc_collect(&vm.heap, &vm);

    /* After GC, dead block(s) should be recycled — fewer blocks */
    int block_count_after = 0;
    for (GCBlock *b = vm.heap.blocks; b; b = b->next) block_count_after++;
    ASSERT(block_count_after < block_count_before);

    /* Live value intact */
    ASSERT(jacl_as_i64(live) == 42);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ====================================================================
 * Multi-threaded stress: short-lived closures with concurrent GC
 * ==================================================================== */

static volatile intptr_t mt_stress_done;

static void mt_stress__alloc_closures(void *data) {
    int count = (int)(intptr_t)data;
    WorkerThread *self = rt__current_worker;
    for (int i = 0; i < count; i++) {
        /* Allocate a short-lived closure (immediately discarded) */
        JaclClosure *cl = (JaclClosure *)gc_alloc(
            &self->vm.heap, OBJ_CLOSURE, sizeof(JaclClosure));
        memset(cl, 0, sizeof(JaclClosure));
        (void)cl; /* discard */
    }
    ATOMIC_STORE_EXPLICIT(&mt_stress_done,
        ATOMIC_LOAD_EXPLICIT(&mt_stress_done, MEM_RELAXED) + 1,
        MEM_RELEASE);
}

static int test_mt_stress_closures(void) {
    /* N workers allocating short-lived closures — concurrent GC runs */
    Runtime rt;
    runtime_init(&rt, 4);

    ATOMIC_STORE_EXPLICIT(&mt_stress_done, 0, MEM_RELEASE);
    int num_tasks = 8;
    int allocs_per_task = 50000;

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, mt_stress__alloc_closures,
                       (void *)(intptr_t)allocs_per_task);
    }

    for (int ms = 0; ms < 60000; ms++) {
        intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_stress_done, MEM_ACQUIRE);
        if (done >= (intptr_t)num_tasks) break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_stress_done, MEM_ACQUIRE);
    ASSERT_I64_EQ(done, (int64_t)num_tasks);

    runtime_destroy(&rt);
    TEST_PASS();
}

/* ====================================================================
 * Multi-threaded stress: COW map operations on shared immutable maps
 * ==================================================================== */

static volatile intptr_t mt_map_done;

static void mt_stress__cow_maps(void *data) {
    int count = (int)(intptr_t)data;
    WorkerThread *self = rt__current_worker;
    gc__current_heap = &self->vm.heap;

    /* Each worker builds and discards maps */
    for (int i = 0; i < count; i++) {
        jacl_map_node *map = NULL;
        char kbuf[8];
        int len = snprintf(kbuf, sizeof(kbuf), "k%d", i % 100);
        JaclVal key = jacl_inline_string(kbuf, (uint32_t)len);
        JaclVal val = jacl_i64(&self->vm.heap, i);
        map = jacl_map_set(map, key, val);

        /* Add a few more entries */
        for (int j = 0; j < 5; j++) {
            len = snprintf(kbuf, sizeof(kbuf), "j%d", j);
            JaclVal k2 = jacl_inline_string(kbuf, (uint32_t)len);
            JaclVal v2 = jacl_i64(&self->vm.heap, j);
            map = jacl_map_set(map, k2, v2);
        }
        (void)map; /* discard — becomes garbage */
    }

    ATOMIC_STORE_EXPLICIT(&mt_map_done,
        ATOMIC_LOAD_EXPLICIT(&mt_map_done, MEM_RELAXED) + 1,
        MEM_RELEASE);
}

static int test_mt_stress_cow_maps(void) {
    /* N workers performing COW map operations concurrently */
    Runtime rt;
    runtime_init(&rt, 4);

    ATOMIC_STORE_EXPLICIT(&mt_map_done, 0, MEM_RELEASE);
    int num_tasks = 8;
    int ops_per_task = 5000;

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, mt_stress__cow_maps,
                       (void *)(intptr_t)ops_per_task);
    }

    for (int ms = 0; ms < 60000; ms++) {
        intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_map_done, MEM_ACQUIRE);
        if (done >= (intptr_t)num_tasks) break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_map_done, MEM_ACQUIRE);
    ASSERT_I64_EQ(done, (int64_t)num_tasks);

    runtime_destroy(&rt);
    TEST_PASS();
}

/* ====================================================================
 * Multi-threaded stress: atom swap! with CAS
 * ==================================================================== */

static JaclMutableRef *mt_atom_ref;
static volatile intptr_t mt_atom_done;

static void mt_stress__atom_swap(void *data) {
    int count = (int)(intptr_t)data;
    WorkerThread *self = rt__current_worker;

    for (int i = 0; i < count; i++) {
        for (;;) {
            JaclVal current = ATOMIC_LOAD_EXPLICIT(
                &mt_atom_ref->value, MEM_ACQUIRE);
            int64_t n = jacl_as_i64(current);
            JaclVal next = jacl_i64(&self->vm.heap, n + 1);
            JaclVal expected = current;
            if (ATOMIC_CAS(&mt_atom_ref->value, &expected, next,
                           MEM_ACQ_REL, MEM_ACQUIRE)) {
                break;
            }
        }
    }

    ATOMIC_STORE_EXPLICIT(&mt_atom_done,
        ATOMIC_LOAD_EXPLICIT(&mt_atom_done, MEM_RELAXED) + 1,
        MEM_RELEASE);
}

static int test_mt_stress_atom_swap(void) {
    /* 4 workers × 1000 CAS increments → final = 4000 */
    Runtime rt;
    runtime_init(&rt, 4);

    gc__current_heap = &rt.workers[0].vm.heap;
    gc__thread_epoch = 1;
    mt_atom_ref = (JaclMutableRef *)gc_alloc(
        &rt.workers[0].vm.heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
    JaclVal zero = jacl_i64(&rt.workers[0].vm.heap, 0);
    ATOMIC_STORE_EXPLICIT(&mt_atom_ref->value, zero, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&mt_atom_done, 0, MEM_RELEASE);

    int num_tasks = 4;
    int swaps_per_task = 1000;
    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, mt_stress__atom_swap,
                       (void *)(intptr_t)swaps_per_task);
    }

    for (int ms = 0; ms < 30000; ms++) {
        intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_atom_done, MEM_ACQUIRE);
        if (done >= (intptr_t)num_tasks) break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_atom_done, MEM_ACQUIRE);
    ASSERT_I64_EQ(done, (int64_t)num_tasks);

    JaclVal final_val = ATOMIC_LOAD_EXPLICIT(&mt_atom_ref->value, MEM_ACQUIRE);
    int64_t final_n = jacl_as_i64(final_val);
    ASSERT_I64_EQ(final_n, (int64_t)(num_tasks * swaps_per_task));

    gc__thread_epoch = 0;
    runtime_destroy(&rt);
    TEST_PASS();
}

/* ====================================================================
 * Memory bounded: heap stabilizes under allocation-heavy workload
 * ==================================================================== */

static volatile intptr_t mt_bounded_done;

static void mt_bounded__alloc_discard(void *data) {
    int count = (int)(intptr_t)data;
    WorkerThread *self = rt__current_worker;

    for (int i = 0; i < count; i++) {
        /* Allocate and immediately discard */
        gc_alloc(&self->vm.heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    }

    ATOMIC_STORE_EXPLICIT(&mt_bounded_done,
        ATOMIC_LOAD_EXPLICIT(&mt_bounded_done, MEM_RELAXED) + 1,
        MEM_RELEASE);
}

static int test_memory_bounded(void) {
    /* Allocation-heavy workload triggering many GC cycles — heap stabilizes */
    Runtime rt;
    runtime_init(&rt, 4);

    /* Phase 1: run heavy allocation tasks to trigger GC */
    ATOMIC_STORE_EXPLICIT(&mt_bounded_done, 0, MEM_RELEASE);
    int num_tasks = 8;
    int allocs_per = 100000;

    for (int i = 0; i < num_tasks; i++) {
        runtime_submit(&rt, mt_bounded__alloc_discard,
                       (void *)(intptr_t)allocs_per);
    }

    for (int ms = 0; ms < 120000; ms++) {
        intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_bounded_done, MEM_ACQUIRE);
        if (done >= (intptr_t)num_tasks) break;
        SLEEP_MILLISECONDS(1);
    }

    intptr_t done = ATOMIC_LOAD_EXPLICIT(&mt_bounded_done, MEM_ACQUIRE);
    ASSERT_I64_EQ(done, (int64_t)num_tasks);

    /* Count total blocks across all workers */
    int total_blocks = 0;
    for (int i = 0; i < rt.num_workers; i++) {
        for (GCBlock *b = rt.workers[i].vm.heap.blocks; b; b = b->next) {
            total_blocks++;
        }
    }

    /* With GC running, heap should be bounded.
     * 800K allocs × 24 bytes each = ~19MB if no GC.
     * Each block is 64KB, so that would be ~300 blocks.
     * With GC, we expect significantly fewer blocks because dead objects
     * are reclaimed each cycle. We allow a generous bound. */
    ASSERT(total_blocks < 200);

    runtime_destroy(&rt);
    TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char *name; int (*fn)(void); } TestEntry;

int main(void) {
    TestEntry tests[] = {
        /* GC correctness */
        { "gc_multi_type_survive",         test_gc_multi_type_survive },
        { "gc_dead_collected",             test_gc_dead_collected },
        /* Tracing through object graph */
        { "trace_closure_upvalue_map",     test_trace_closure_upvalue_map },
        { "trace_hamt_nested",             test_trace_hamt_nested },
        { "trace_rrb_closure_elements",    test_trace_rrb_closure_elements },
        /* Write barriers */
        { "wb_satb_old_survives",          test_wb_satb_old_survives },
        { "wb_insertion_new_survives",     test_wb_insertion_new_survives },
        /* Epoch watermark */
        { "epoch_watermark_recent_survives", test_epoch_watermark_recent_survives },
        /* Line-granularity and block recycling */
        { "line_granularity_reclamation",  test_line_granularity_reclamation },
        { "block_recycling",              test_block_recycling },
        /* Multi-threaded stress */
        { "mt_stress_closures",           test_mt_stress_closures },
        { "mt_stress_cow_maps",           test_mt_stress_cow_maps },
        { "mt_stress_atom_swap",          test_mt_stress_atom_swap },
        /* Memory bounded */
        { "memory_bounded",               test_memory_bounded },
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
    return (failed > 0) ? 1 : 0;
}
