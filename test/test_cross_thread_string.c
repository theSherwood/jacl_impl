#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-012: Cross-thread string passing ===== */

/* --- Shared communication struct --- */

typedef struct {
    JaclVal value;
    int ready;                 /* 0 = not ready, 1 = value ready, 2 = done.
                                  Accessed via ATOMIC_LOAD/STORE_EXPLICIT
                                  with release/acquire — .ready is the
                                  publication signal that orders .value
                                  reads on the consumer side. */
    platform_mutex_t mutex;
} SharedSlot;

static void shared_slot_init(SharedSlot *s) {
    s->value = JACL_NIL;
    s->ready = 0;
    MUTEX_INIT(s->mutex);
}

/* --- Helper: set up a VM with GC handle slots for pinning --- */

#define MAX_HANDLES 64

static void setup_vm_with_handles(VM *vm, arena_t *arena,
                                   JaclVal *handle_slots) {
    vm_init(vm, arena);
    /* Wire handle slots for GC root scanning */
    for (uint32_t i = 0; i < MAX_HANDLES; i++) handle_slots[i] = JACL_NIL;
    vm->gc_handle_slots = handle_slots;
    vm->gc_handle_count = MAX_HANDLES;
}

/* Pin a value in a handle slot, returns the slot index */
static uint32_t pin_value(VM *vm, JaclVal val) {
    for (uint32_t i = 0; i < vm->gc_handle_count; i++) {
        if (vm->gc_handle_slots[i] == JACL_NIL) {
            vm->gc_handle_slots[i] = val;
            return i;
        }
    }
    return UINT32_MAX;
}

static void unpin_value(VM *vm, uint32_t idx) {
    if (idx < vm->gc_handle_count) {
        vm->gc_handle_slots[idx] = JACL_NIL;
    }
}

/* ===== Test 1: Inline string cross-thread ===== */

typedef struct {
    SharedSlot *slot;
} ThreadArg;

static THREAD_PROC_RETURN THREAD_PROC_TYPE inline_receiver_fn(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    /* Wait for value to be ready */
    while (ATOMIC_LOAD_EXPLICIT(&ta->slot->ready, MEM_ACQUIRE) != 1) {
        SLEEP_MILLISECONDS(1);
    }

    JaclVal v = ta->slot->value;

    /* Verify it's an inline string with expected content */
    char buf[8];
    uint32_t len = jacl_string_data(v, buf, sizeof(buf));
    if (len != 3 || memcmp(buf, "abc", 3) != 0) {
        fprintf(stderr, "FAIL: inline string content mismatch\n");
        ATOMIC_STORE_EXPLICIT(&ta->slot->ready, -1, MEM_RELEASE);
        return 0;
    }

    ATOMIC_STORE_EXPLICIT(&ta->slot->ready, 2, MEM_RELEASE); /* done */
    return 0;
}

static int test_inline_string_cross_thread(void) {
    tracker_reset();
    arena_t arena = {0};
    VM vm;
    vm_init(&vm, &arena);

    SharedSlot slot;
    shared_slot_init(&slot);

    /* Thread A (main): create inline string */
    JaclVal v = jacl_inline_string("abc", 3);

    /* Launch receiver thread B */
    ThreadArg ta = { .slot = &slot };
    thread_t th;
    THREAD_CREATE(&th, NULL, inline_receiver_fn, &ta);

    /* Pass the value */
    slot.value = v;
    ATOMIC_STORE_EXPLICIT(&slot.ready, 1, MEM_RELEASE);

    THREAD_JOIN(th, NULL);
    ASSERT(slot.ready == 2);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== Test 2: Interned string with handle pin ===== */

typedef struct {
    SharedSlot *slot;
    BlockPool  *pool;
    VM         *vm;
} HeapThreadArg;

static THREAD_PROC_RETURN THREAD_PROC_TYPE interned_receiver_fn(void *arg) {
    HeapThreadArg *ta = (HeapThreadArg *)arg;

    /* Set up per-thread heap for GC */
    ThreadHeap heap;
    gc_heap_init(&heap, ta->pool);
    gc__current_heap = &heap;

    while (ATOMIC_LOAD_EXPLICIT(&ta->slot->ready, MEM_ACQUIRE) != 1) {
        SLEEP_MILLISECONDS(1);
    }

    JaclVal v = ta->slot->value;

    /* Trigger GC on receiving thread to test epoch watermarking */
    gc_collect(&heap, ta->vm);

    /* Verify content (the handle pin should keep it alive through GC) */
    char buf[32];
    uint32_t len = jacl_string_data(v, buf, sizeof(buf));
    if (len != 12 || memcmp(buf, "hello_intern", 12) != 0) {
        fprintf(stderr, "FAIL: interned string content mismatch (len=%u)\n", len);
        ATOMIC_STORE_EXPLICIT(&ta->slot->ready, -1, MEM_RELEASE);
        gc_heap_destroy(&heap);
        return 0;
    }

    ATOMIC_STORE_EXPLICIT(&ta->slot->ready, 2, MEM_RELEASE);
    gc_heap_destroy(&heap);
    return 0;
}

static int test_interned_string_cross_thread(void) {
    tracker_reset();
    arena_t arena = {0};
    VM vm;
    JaclVal handle_slots[MAX_HANDLES];
    setup_vm_with_handles(&vm, &arena, handle_slots);

    JaclInternTable table;
    intern_table_init(&table, &arena);
    vm.intern_table = &table;

    SharedSlot slot;
    shared_slot_init(&slot);

    /* Thread A (main): create interned string and pin it */
    JaclVal v = jacl_intern(&vm.heap, &table, "hello_intern", 12);
    uint32_t handle_idx = pin_value(&vm, v);
    ASSERT(handle_idx != UINT32_MAX);

    HeapThreadArg ta = { .slot = &slot, .pool = &vm.block_pool, .vm = &vm };
    thread_t th;
    THREAD_CREATE(&th, NULL, interned_receiver_fn, &ta);

    /* Pass value */
    slot.value = v;
    ATOMIC_STORE_EXPLICIT(&slot.ready, 1, MEM_RELEASE);

    THREAD_JOIN(th, NULL);
    ASSERT(slot.ready == 2);

    unpin_value(&vm, handle_idx);
    intern_table_destroy(&table);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== Test 3: Rope string with handle pin ===== */

static THREAD_PROC_RETURN THREAD_PROC_TYPE rope_receiver_fn(void *arg) {
    HeapThreadArg *ta = (HeapThreadArg *)arg;

    ThreadHeap heap;
    gc_heap_init(&heap, ta->pool);
    gc__current_heap = &heap;

    while (ATOMIC_LOAD_EXPLICIT(&ta->slot->ready, MEM_ACQUIRE) != 1) {
        SLEEP_MILLISECONDS(1);
    }

    JaclVal v = ta->slot->value;

    /* Trigger GC */
    gc_collect(&heap, ta->vm);

    /* Verify rope content via cursor */
    JaclRopeString *rs = jacl_as_rope_string(v);
    size_t byte_len = rope_byte_count(rs->r);
    if (byte_len != 200) {
        fprintf(stderr, "FAIL: rope byte_len=%zu expected 200\n", byte_len);
        ATOMIC_STORE_EXPLICIT(&ta->slot->ready, -1, MEM_RELEASE);
        gc_heap_destroy(&heap);
        return 0;
    }

    /* Read first 10 bytes and verify pattern */
    uint8_t buf[10];
    rope_cursor c = rope_cursor_new(rs->r, 0);
    size_t rd = rope_cursor_read(&c, buf, 10);
    if (rd != 10) {
        fprintf(stderr, "FAIL: rope cursor read %zu expected 10\n", rd);
        ATOMIC_STORE_EXPLICIT(&ta->slot->ready, -1, MEM_RELEASE);
        gc_heap_destroy(&heap);
        return 0;
    }
    /* Pattern: 'a' + (i % 26) */
    for (size_t i = 0; i < 10; i++) {
        if (buf[i] != (uint8_t)('a' + (i % 26))) {
            fprintf(stderr, "FAIL: rope byte[%zu]=%u expected %u\n",
                    i, buf[i], (uint8_t)('a' + (i % 26)));
            ATOMIC_STORE_EXPLICIT(&ta->slot->ready, -1, MEM_RELEASE);
            gc_heap_destroy(&heap);
            return 0;
        }
    }

    ATOMIC_STORE_EXPLICIT(&ta->slot->ready, 2, MEM_RELEASE);
    gc_heap_destroy(&heap);
    return 0;
}

static int test_rope_string_cross_thread(void) {
    tracker_reset();
    arena_t arena = {0};
    VM vm;
    JaclVal handle_slots[MAX_HANDLES];
    setup_vm_with_handles(&vm, &arena, handle_slots);

    SharedSlot slot;
    shared_slot_init(&slot);

    /* Create a rope string (200 bytes > 128 threshold) */
    uint8_t data[200];
    for (size_t i = 0; i < 200; i++) data[i] = (uint8_t)('a' + (i % 26));
    JaclVal v = jacl_rope_string_create(&vm.heap, data, 200);
    uint32_t handle_idx = pin_value(&vm, v);
    ASSERT(handle_idx != UINT32_MAX);

    HeapThreadArg ta = { .slot = &slot, .pool = &vm.block_pool, .vm = &vm };
    thread_t th;
    THREAD_CREATE(&th, NULL, rope_receiver_fn, &ta);

    slot.value = v;
    ATOMIC_STORE_EXPLICIT(&slot.ready, 1, MEM_RELEASE);

    THREAD_JOIN(th, NULL);
    ASSERT(slot.ready == 2);

    unpin_value(&vm, handle_idx);
    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* ===== Test 4: Two threads create ropes, third thread concatenates ===== */

typedef struct {
    SharedSlot *slot_a;
    SharedSlot *slot_b;
    BlockPool  *pool;
    VM         *vm;
} ConcatThreadArg;

static THREAD_PROC_RETURN THREAD_PROC_TYPE rope_creator_a_fn(void *arg) {
    HeapThreadArg *ta = (HeapThreadArg *)arg;

    ThreadHeap heap;
    gc_heap_init(&heap, ta->pool);
    gc__current_heap = &heap;

    uint8_t data[200];
    for (size_t i = 0; i < 200; i++) data[i] = 'A';
    JaclVal v = jacl_rope_string_create(&heap, data, 200);

    ta->slot->value = v;
    ATOMIC_STORE_EXPLICIT(&ta->slot->ready, 1, MEM_RELEASE);

    /* Keep heap alive until main is done */
    while (ATOMIC_LOAD_EXPLICIT(&ta->slot->ready, MEM_ACQUIRE) != 2) {
        SLEEP_MILLISECONDS(1);
    }

    gc_heap_destroy(&heap);
    return 0;
}

static THREAD_PROC_RETURN THREAD_PROC_TYPE rope_creator_b_fn(void *arg) {
    HeapThreadArg *ta = (HeapThreadArg *)arg;

    ThreadHeap heap;
    gc_heap_init(&heap, ta->pool);
    gc__current_heap = &heap;

    uint8_t data[200];
    for (size_t i = 0; i < 200; i++) data[i] = 'B';
    JaclVal v = jacl_rope_string_create(&heap, data, 200);

    ta->slot->value = v;
    ATOMIC_STORE_EXPLICIT(&ta->slot->ready, 1, MEM_RELEASE);

    while (ATOMIC_LOAD_EXPLICIT(&ta->slot->ready, MEM_ACQUIRE) != 2) {
        SLEEP_MILLISECONDS(1);
    }

    gc_heap_destroy(&heap);
    return 0;
}

static int test_rope_concat_cross_thread(void) {
    tracker_reset();
    arena_t arena = {0};
    VM vm;
    JaclVal handle_slots[MAX_HANDLES];
    setup_vm_with_handles(&vm, &arena, handle_slots);

    SharedSlot slot_a, slot_b;
    shared_slot_init(&slot_a);
    shared_slot_init(&slot_b);

    HeapThreadArg ta_a = { .slot = &slot_a, .pool = &vm.block_pool, .vm = &vm };
    HeapThreadArg ta_b = { .slot = &slot_b, .pool = &vm.block_pool, .vm = &vm };
    thread_t th_a, th_b;
    THREAD_CREATE(&th_a, NULL, rope_creator_a_fn, &ta_a);
    THREAD_CREATE(&th_b, NULL, rope_creator_b_fn, &ta_b);

    /* Wait for both ropes */
    while (ATOMIC_LOAD_EXPLICIT(&slot_a.ready, MEM_ACQUIRE) != 1 ||
           ATOMIC_LOAD_EXPLICIT(&slot_b.ready, MEM_ACQUIRE) != 1) {
        SLEEP_MILLISECONDS(1);
    }

    JaclVal val_a = slot_a.value;
    JaclVal val_b = slot_b.value;

    /* Pin both values */
    uint32_t h_a = pin_value(&vm, val_a);
    uint32_t h_b = pin_value(&vm, val_b);
    ASSERT(h_a != UINT32_MAX);
    ASSERT(h_b != UINT32_MAX);

    /* Trigger GC between receive and concat */
    gc_collect(&vm.heap, &vm);

    /* Thread C (main): concatenate the two ropes */
    JaclRopeString *rs_a = jacl_as_rope_string(val_a);
    JaclRopeString *rs_b = jacl_as_rope_string(val_b);
    rope result_rope = rope_concat(rs_a->r, rs_b->r);

    /* Verify result */
    ASSERT_SIZE_EQ(rope_byte_count(result_rope), 400);

    uint8_t buf[400];
    rope_to_str(result_rope, buf, 400);

    /* First 200 bytes should be 'A', next 200 should be 'B' */
    for (size_t i = 0; i < 200; i++) {
        ASSERT(buf[i] == 'A');
    }
    for (size_t i = 200; i < 400; i++) {
        ASSERT(buf[i] == 'B');
    }

    unpin_value(&vm, h_a);
    unpin_value(&vm, h_b);

    /* Release creator threads */
    ATOMIC_STORE_EXPLICIT(&slot_a.ready, 2, MEM_RELEASE);
    ATOMIC_STORE_EXPLICIT(&slot_b.ready, 2, MEM_RELEASE);
    THREAD_JOIN(th_a, NULL);
    THREAD_JOIN(th_b, NULL);

    vm_destroy(&vm);
    arena_destroy(&arena);
    TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
    test_fn tests[] = {
        test_inline_string_cross_thread,
        test_interned_string_cross_thread,
        test_rope_string_cross_thread,
        test_rope_concat_cross_thread,
    };

    const char *names[] = {
        "test_inline_string_cross_thread",
        "test_interned_string_cross_thread",
        "test_rope_string_cross_thread",
        "test_rope_concat_cross_thread",
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int pass = 0, fail = 0;

    printf("=== US-012: Cross-thread string passing ===\n");
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
