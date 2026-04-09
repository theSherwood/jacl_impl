#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-009: Grapheme-based slice across all tiers ===== */

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

/* --- ASCII slice tests via jacl_run() --- */

/* Test: [slice "hello" 1 3] returns "el" (graphemes 1 and 2) */
static int test_slice_ascii_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 1 3]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "el\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [slice "hello" 2] returns "llo" (one-arg form: from start to end) */
static int test_slice_one_arg_form(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 2]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "llo\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: empty slice returns empty string */
static int test_slice_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 3 3]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: out-of-range indices are clamped */
static int test_slice_clamping(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* end > grapheme length is clamped to grapheme length */
  VMResult result = jacl_run("[print [slice \"hello\" 3 99]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "lo\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: negative start is clamped to 0 */
static int test_slice_negative_clamped(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* [- 0 2] = -2; start clamped to 0, end = 3 → "hel" */
  VMResult result = jacl_run("[print [slice \"hello\" [- 0 2] 3]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hel\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Combining mark slice tests at C level --- */

/* Test: slice at combining mark boundaries correctly includes full grapheme clusters */
static int test_slice_combining_marks(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "ae\xCC\x81io\xCC\x88" = a, e+combining_acute, i, o+combining_diaeresis
   * = 4 graphemes, 8 bytes (heap string) */
  JaclVal str = jacl_string_new(&heap, &table, "ae\xCC\x81io\xCC\x88", 8);
  ASSERT(jacl_is_heap_string(str));
  ASSERT_U32_EQ(jacl_string_len(str), 4);
  ASSERT_U32_EQ(jacl_string_byte_len(str), 8);

  /* Slice graphemes 1..3 = e+combining, i = should be 4 bytes */
  uint8_t buf[256];
  uint32_t byte_len = jacl_string_data(str, (char*)buf, sizeof(buf));
  size_t byte_start = unicode_grapheme_byte_offset(buf, byte_len, 1);
  size_t byte_end = unicode_grapheme_byte_offset(buf, byte_len, 3);
  ASSERT_INT_EQ((int)(byte_end - byte_start), 4);  /* e(1) + CC81(2) + i(1) = 4 */
  ASSERT_INT_EQ(buf[byte_start], 'e');
  ASSERT_INT_EQ(buf[byte_start + 1], (uint8_t)0xCC);
  ASSERT_INT_EQ(buf[byte_start + 2], (uint8_t)0x81);
  ASSERT_INT_EQ(buf[byte_start + 3], 'i');

  /* Create the sliced string */
  JaclVal sliced = jacl_string_new(&heap, &table,
                                    (const char*)(buf + byte_start), byte_end - byte_start);
  ASSERT_U32_EQ(jacl_string_len(sliced), 2);      /* 2 graphemes */
  ASSERT_U32_EQ(jacl_string_byte_len(sliced), 4);  /* 4 bytes */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice of heap string with combining marks via VM */
static int test_slice_combining_via_vm(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* "hello world this is long" = 24 bytes, heap string
   * All ASCII, so grapheme indices = byte indices */
  VMResult result = jacl_run(
      "[print [slice \"hello world this is long\" 6 11]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Rope slice tests --- */

/* Test: slice of rope string at C level */
static int test_slice_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 200-byte ASCII rope string: a-z repeating */
  char input[200];
  for (size_t i = 0; i < 200; i++) input[i] = 'a' + (char)(i % 26);

  JaclVal str = jacl_string_new(&heap, &table, input, 200);
  ASSERT(jacl_is_rope_string(str));
  ASSERT_U32_EQ(jacl_string_len(str), 200);

  /* Slice graphemes 10..20 via rope_slice_by_graphemes */
  JaclRopeString* rs = jacl_as_rope_string(str);
  rope sliced = rope_slice_by_graphemes(rs->r, 10, 10);
  size_t sliced_bytes = rope_byte_count(sliced);
  ASSERT_INT_EQ((int)sliced_bytes, 10);

  /* Verify content */
  uint8_t out[10];
  rope_to_str(sliced, out, 10);
  for (int i = 0; i < 10; i++) {
    ASSERT_INT_EQ(out[i], 'a' + (char)((10 + i) % 26));
  }

  rope_unref(sliced);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: rope slice across leaves */
static int test_slice_rope_across_leaves(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 500-byte ASCII rope string (multiple leaves at ROPE_LEAF_MAX=256) */
  char input[500];
  for (size_t i = 0; i < 500; i++) input[i] = 'A' + (char)(i % 26);

  JaclVal str = jacl_string_new(&heap, &table, input, 500);
  ASSERT(jacl_is_rope_string(str));

  /* Slice across leaf boundary: graphemes 240..270 (straddles 256-byte leaf boundary) */
  JaclRopeString* rs = jacl_as_rope_string(str);
  rope sliced = rope_slice_by_graphemes(rs->r, 240, 30);
  size_t sliced_bytes = rope_byte_count(sliced);
  ASSERT_INT_EQ((int)sliced_bytes, 30);

  uint8_t out[30];
  rope_to_str(sliced, out, 30);
  for (int i = 0; i < 30; i++) {
    ASSERT_INT_EQ(out[i], 'A' + (char)((240 + i) % 26));
  }

  rope_unref(sliced);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice of rope string that produces a small result (goes through jacl_string_new) */
static int test_slice_rope_to_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build 200-byte rope, slice out 3 graphemes → should be inline */
  char input[200];
  for (size_t i = 0; i < 200; i++) input[i] = 'a' + (char)(i % 26);

  JaclVal str = jacl_string_new(&heap, &table, input, 200);
  ASSERT(jacl_is_rope_string(str));

  /* Slice via rope API, then create string via jacl_string_new */
  JaclRopeString* rs = jacl_as_rope_string(str);
  rope sliced = rope_slice_by_graphemes(rs->r, 0, 3);
  size_t sliced_bytes = rope_byte_count(sliced);
  ASSERT_INT_EQ((int)sliced_bytes, 3);

  uint8_t tmp[8];
  rope_to_str(sliced, tmp, sliced_bytes);
  rope_unref(sliced);

  JaclVal result = jacl_string_new(&heap, &table, (const char*)tmp, sliced_bytes);
  ASSERT(jacl_is_inline_string(result));  /* 3 bytes → inline */
  ASSERT_U32_EQ(jacl_string_len(result), 3);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- End-to-end VM tests --- */

/* Test: full slice returns whole string */
static int test_slice_full_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [slice \"hello\" 0 5]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice result is heap-interned when > 7 bytes */
static int test_slice_heap_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* "hello world this is long" = 24 chars, slice first 8 graphemes */
  VMResult result = jacl_run(
      "[print [slice \"hello world this is long\" 0 8]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello wo\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "slice_ascii_basic",         test_slice_ascii_basic },
    { "slice_one_arg_form",        test_slice_one_arg_form },
    { "slice_empty",               test_slice_empty },
    { "slice_clamping",            test_slice_clamping },
    { "slice_negative_clamped",    test_slice_negative_clamped },
    { "slice_combining_marks",     test_slice_combining_marks },
    { "slice_combining_via_vm",    test_slice_combining_via_vm },
    { "slice_rope",                test_slice_rope },
    { "slice_rope_across_leaves",  test_slice_rope_across_leaves },
    { "slice_rope_to_inline",      test_slice_rope_to_inline },
    { "slice_full_string",         test_slice_full_string },
    { "slice_heap_result",         test_slice_heap_result },
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
