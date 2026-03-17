#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-007: Grapheme-based length and byte-length builtin ===== */

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

/* --- [length] tests via jacl_run() --- */

/* Test: [length "hello"] returns 5 (ASCII) */
static int test_length_ascii(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [length \"hello\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [length ""] returns 0 */
static int test_length_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [length \"\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [length] with combining marks returns grapheme count < byte count
 * "e\u0301" is 'e' + combining acute accent = 1 grapheme, 3 bytes.
 * But since jacl_string_new NFD-normalizes, we test with already-NFD input.
 * We construct this via concat: [concat "e" "\xCC\x81"] but we can't easily
 * embed raw bytes in JACL source. Instead, test at C level. */
static int test_length_combining_marks_c_level(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "e\xCC\x81" = e + combining acute = 3 bytes, 1 grapheme (already NFD) */
  JaclVal v = jacl_string_new(&heap, &table, "e\xCC\x81", 3);
  ASSERT(jacl_is_inline_string(v));
  ASSERT_U32_EQ(jacl_string_len(v), 1);      /* 1 grapheme */
  ASSERT_U32_EQ(jacl_string_byte_len(v), 3);  /* 3 bytes */

  /* "ae\xCC\x81i" = 'a' + 'e'+combining + 'i' = 5 bytes, 3 graphemes */
  JaclVal v2 = jacl_string_new(&heap, &table, "ae\xCC\x81i", 5);
  ASSERT(jacl_is_inline_string(v2));
  ASSERT_U32_EQ(jacl_string_len(v2), 3);      /* 3 graphemes */
  ASSERT_U32_EQ(jacl_string_byte_len(v2), 5);  /* 5 bytes */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: grapheme count for heap string with combining marks */
static int test_length_combining_heap(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 10-byte string with combining marks: "aae\xCC\x81bcde\xCC\x81f"
   * = a, a, e+combining, b, c, d, e+combining, f = 8 graphemes, 10 bytes */
  JaclVal v = jacl_string_new(&heap, &table, "aae\xCC\x81" "bcde\xCC\x81" "f", 12);
  ASSERT(jacl_is_heap_string(v));
  ASSERT_U32_EQ(jacl_string_len(v), 8);        /* 8 graphemes */
  ASSERT_U32_EQ(jacl_string_byte_len(v), 12);   /* 12 bytes */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: grapheme count for rope string */
static int test_length_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 200-byte ASCII string → rope tier */
  char buf[200];
  for (size_t i = 0; i < 200; i++) buf[i] = 'a' + (char)(i % 26);

  JaclVal v = jacl_string_new(&heap, &table, buf, 200);
  ASSERT(jacl_is_rope_string(v));
  ASSERT_U32_EQ(jacl_string_len(v), 200);        /* ASCII: graphemes == bytes */
  ASSERT_U32_EQ(jacl_string_byte_len(v), 200);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [length] via jacl_run() for heap string */
static int test_length_heap_via_run(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [length \"hello world\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "11\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- [byte-length] tests via jacl_run() --- */

/* Test: [byte-length "hello"] returns 5 */
static int test_byte_length_ascii(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [byte-length \"hello\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [byte-length ""] returns 0 */
static int test_byte_length_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [byte-length \"\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [byte-length] for heap string */
static int test_byte_length_heap(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [byte-length \"hello world\"]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "11\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: byte-length for all tiers at C level */
static int test_byte_length_all_tiers(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Inline: 5 bytes */
  JaclVal v_inline = jacl_string_new(&heap, &table, "hello", 5);
  ASSERT(jacl_is_inline_string(v_inline));
  ASSERT_U32_EQ(jacl_string_byte_len(v_inline), 5);

  /* Flat: 12 bytes */
  JaclVal v_flat = jacl_string_new(&heap, &table, "hello world!", 12);
  ASSERT(jacl_is_heap_string(v_flat));
  ASSERT_U32_EQ(jacl_string_byte_len(v_flat), 12);

  /* Rope: 200 bytes */
  char buf[200];
  for (size_t i = 0; i < 200; i++) buf[i] = 'a' + (char)(i % 26);
  JaclVal v_rope = jacl_string_new(&heap, &table, buf, 200);
  ASSERT(jacl_is_rope_string(v_rope));
  ASSERT_U32_EQ(jacl_string_byte_len(v_rope), 200);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: grapheme length for inline with combining vs byte-length */
static int test_length_vs_byte_length_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "e\xCC\x81" = e + combining acute = 3 bytes, 1 grapheme */
  JaclVal v = jacl_string_new(&heap, &table, "e\xCC\x81", 3);
  ASSERT_U32_EQ(jacl_string_len(v), 1);
  ASSERT_U32_EQ(jacl_string_byte_len(v), 3);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* [length] tests */
    { "length_ascii",                  test_length_ascii },
    { "length_empty",                  test_length_empty },
    { "length_combining_marks_c",      test_length_combining_marks_c_level },
    { "length_combining_heap",         test_length_combining_heap },
    { "length_rope",                   test_length_rope },
    { "length_heap_via_run",           test_length_heap_via_run },
    /* [byte-length] tests */
    { "byte_length_ascii",             test_byte_length_ascii },
    { "byte_length_empty",             test_byte_length_empty },
    { "byte_length_heap",              test_byte_length_heap },
    { "byte_length_all_tiers",         test_byte_length_all_tiers },
    { "length_vs_byte_length_inline",  test_length_vs_byte_length_inline },
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
