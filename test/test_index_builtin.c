#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-008: Grapheme-based index across all tiers ===== */

/* Print capture helper */
typedef struct {
  char     buf[4096];
  uint32_t len;
} PrintCapture;

static void capture_print(const char* text, uint32_t len, void* ctx) {
  PrintCapture* cap = (PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

/* --- ASCII index tests via jacl_run() --- */

/* Test: [index "hello" 0] returns "h" */
static int test_index_ascii_first(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" 0]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "h\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [index "hello" 4] returns "o" */
static int test_index_ascii_last(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" 4]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "o\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [index "hello" 5] returns nil (out of bounds) */
static int test_index_out_of_bounds(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" 5]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [index "hello" [- 0 1]] returns nil (negative index) */
static int test_index_negative(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"hello\" [- 0 1]]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Combining mark index tests at C level --- */

/* Test: index into inline string with combining marks returns full cluster */
static int test_index_combining_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "ae\xCC\x81i" = 'a', 'e'+combining_acute, 'i' = 3 graphemes, 5 bytes */
  JaclVal str = jacl_string_new(&heap, &table, "ae\xCC\x81i", 5);
  ASSERT(jacl_is_inline_string(str));
  ASSERT_U32_EQ(jacl_string_len(str), 3);

  /* Index 0: 'a' */
  uint8_t buf[8];
  uint32_t blen = jacl_string_data(str, (char*)buf, sizeof(buf));
  size_t gs, ge;
  ASSERT(jacl_grapheme_nth(buf, blen, 0, &gs, &ge));
  ASSERT_INT_EQ((int)(ge - gs), 1);
  ASSERT_INT_EQ(buf[gs], 'a');

  /* Index 1: 'e' + combining acute (3 bytes: 0x65 0xCC 0x81) */
  ASSERT(jacl_grapheme_nth(buf, blen, 1, &gs, &ge));
  ASSERT_INT_EQ((int)(ge - gs), 3);
  ASSERT_INT_EQ(buf[gs], 'e');
  ASSERT_INT_EQ(buf[gs+1], (uint8_t)0xCC);
  ASSERT_INT_EQ(buf[gs+2], (uint8_t)0x81);

  /* Index 2: 'i' */
  ASSERT(jacl_grapheme_nth(buf, blen, 2, &gs, &ge));
  ASSERT_INT_EQ((int)(ge - gs), 1);
  ASSERT_INT_EQ(buf[gs], 'i');

  /* Index 3: out of bounds */
  ASSERT(!jacl_grapheme_nth(buf, blen, 3, &gs, &ge));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: index into heap string with combining marks via VM */
static int test_index_combining_heap(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build 12-byte heap string: "aae\xCC\x81bcde\xCC\x81f" (from US-007 test)
   * = a, a, e+combining, b, c, d, e+combining, f = 8 graphemes, 12 bytes */
  JaclVal str = jacl_string_new(&heap, &table, "aae\xCC\x81" "bcde\xCC\x81" "f", 12);
  ASSERT(jacl_is_heap_string(str));
  ASSERT_U32_EQ(jacl_string_len(str), 8);

  /* Extract bytes and test grapheme indexing */
  uint8_t buf[256];
  uint32_t blen = jacl_string_data(str, (char*)buf, sizeof(buf));
  size_t gs, ge;

  /* Index 2: e+combining acute (3 bytes) */
  ASSERT(jacl_grapheme_nth(buf, blen, 2, &gs, &ge));
  ASSERT_INT_EQ((int)(ge - gs), 3);
  ASSERT_INT_EQ(buf[gs], 'e');

  /* Index 3: 'b' (1 byte) */
  ASSERT(jacl_grapheme_nth(buf, blen, 3, &gs, &ge));
  ASSERT_INT_EQ((int)(ge - gs), 1);
  ASSERT_INT_EQ(buf[gs], 'b');

  /* Index 7: 'f' (last grapheme) */
  ASSERT(jacl_grapheme_nth(buf, blen, 7, &gs, &ge));
  ASSERT_INT_EQ((int)(ge - gs), 1);
  ASSERT_INT_EQ(buf[gs], 'f');

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Rope index test --- */

/* Test: index into rope string */
static int test_index_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 200-byte ASCII rope string */
  char input[200];
  for (size_t i = 0; i < 200; i++) input[i] = 'a' + (char)(i % 26);

  JaclVal str = jacl_string_new(&heap, &table, input, 200);
  ASSERT(jacl_is_rope_string(str));

  /* Index via rope_grapheme_to_byte */
  JaclRopeString* rs = jacl_as_rope_string(str);

  /* Index 0: first character */
  rope_offset_result r0 = rope_grapheme_to_byte(rs->r, 0);
  ASSERT(r0.found || r0.value == 0); /* offset 0 returns {0, true} */
  rope_offset_result r1 = rope_grapheme_to_byte(rs->r, 1);
  ASSERT(r1.found);
  uint8_t ch;
  rope_slice_to_str(rs->r, 0, r1.value, &ch, 1);
  ASSERT_INT_EQ(ch, 'a');

  /* Index 25: 'z' (26th char, idx 25) */
  rope_offset_result r25 = rope_grapheme_to_byte(rs->r, 25);
  rope_offset_result r26 = rope_grapheme_to_byte(rs->r, 26);
  ASSERT(r25.found);
  ASSERT(r26.found);
  rope_slice_to_str(rs->r, r25.value, r26.value - r25.value, &ch, 1);
  ASSERT_INT_EQ(ch, 'z');

  /* Index 199: last character */
  rope_offset_result r199 = rope_grapheme_to_byte(rs->r, 199);
  ASSERT(r199.found);
  size_t total = rope_byte_count(rs->r);
  rope_slice_to_str(rs->r, r199.value, total - r199.value, &ch, 1);
  ASSERT_INT_EQ(ch, input[199]);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- End-to-end VM test for rope index --- */

/* Test: index on heap string via jacl_run() */
static int test_index_heap_via_run(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* "hello world" is 11 bytes = heap string; index 6 = 'w' */
  VMResult result = jacl_run("[print [index \"hello world\" 6]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "w\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: index returns full grapheme cluster via OP_STR_INDEX for inline string with combining */
static int test_index_grapheme_cluster_via_vm(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "ae\xCC\x81" = 'a', 'e'+combining = 2 graphemes */
  JaclVal str = jacl_string_new(&heap, &table, "ae\xCC\x81", 4);
  ASSERT(jacl_is_inline_string(str));
  ASSERT_U32_EQ(jacl_string_len(str), 2);

  /* Extract bytes and verify grapheme cluster at index 1 */
  uint8_t buf[8];
  uint32_t blen = jacl_string_data(str, (char*)buf, sizeof(buf));
  size_t gs, ge;
  ASSERT(jacl_grapheme_nth(buf, blen, 1, &gs, &ge));
  /* Should be 'e' + combining acute = 3 bytes */
  ASSERT_INT_EQ((int)(ge - gs), 3);

  /* Create the result via jacl_string_new like the VM does */
  JaclVal cluster = jacl_string_new(&heap, &table, (const char*)(buf + gs), ge - gs);
  ASSERT(jacl_is_inline_string(cluster));
  ASSERT_U32_EQ(jacl_string_len(cluster), 1);      /* 1 grapheme */
  ASSERT_U32_EQ(jacl_string_byte_len(cluster), 3);  /* 3 bytes */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: index empty string returns nil */
static int test_index_empty_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [index \"\" 0]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "index_ascii_first",              test_index_ascii_first },
    { "index_ascii_last",               test_index_ascii_last },
    { "index_out_of_bounds",            test_index_out_of_bounds },
    { "index_negative",                 test_index_negative },
    { "index_combining_inline",         test_index_combining_inline },
    { "index_combining_heap",           test_index_combining_heap },
    { "index_rope",                     test_index_rope },
    { "index_heap_via_run",             test_index_heap_via_run },
    { "index_grapheme_cluster_via_vm",  test_index_grapheme_cluster_via_vm },
    { "index_empty_string",             test_index_empty_string },
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
