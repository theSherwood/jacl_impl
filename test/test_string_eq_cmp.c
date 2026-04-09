#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-011: String equality and comparison across all tiers ===== */

static void fill_ascii(char* buf, size_t len) {
  for (size_t i = 0; i < len; i++) buf[i] = 'a' + (char)(i % 26);
}

/* Print capture for jacl_run tests */
typedef struct { char buf[4096]; uint32_t len; } PrintCapture;

static void capture_print(const char* text, uint32_t len, void* ctx) {
  PrintCapture* cap = (PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

/* ==== Equality tests ==== */

/* 1. inline == inline (same content) */
static int test_eq_inline_inline_same(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "hello", 5);
  JaclVal b = jacl_string_new(&heap, &table, "hello", 5);

  ASSERT(jacl_is_inline_string(a));
  ASSERT(jacl_is_inline_string(b));
  ASSERT(jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 2. inline != inline (different content) */
static int test_eq_inline_inline_diff(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "hello", 5);
  JaclVal b = jacl_string_new(&heap, &table, "world", 5);

  ASSERT(!jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 3. flat == flat (same content, interning gives same pointer) */
static int test_eq_flat_flat_same(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[20]; fill_ascii(buf, 20);
  JaclVal a = jacl_string_new(&heap, &table, buf, 20);
  JaclVal b = jacl_string_new(&heap, &table, buf, 20);

  ASSERT(jacl_is_heap_string(a));
  ASSERT(jacl_is_heap_string(b));
  ASSERT(jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 4. flat != flat (different content) */
static int test_eq_flat_flat_diff(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[20], buf_b[20];
  fill_ascii(buf_a, 20);
  for (size_t i = 0; i < 20; i++) buf_b[i] = 'A' + (char)(i % 26);

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 20);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 20);

  ASSERT(!jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 5. inline == flat cross-tier: same 7 bytes inline vs 8 bytes flat → not equal (diff length) */
static int test_eq_inline_flat_diff_len(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abcdefg", 7);
  JaclVal b = jacl_string_new(&heap, &table, "abcdefgh", 8);

  ASSERT(jacl_is_inline_string(a));
  ASSERT(jacl_is_heap_string(b));
  ASSERT(!jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 6. rope == rope (same content) */
static int test_eq_rope_rope_same(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[200]; fill_ascii(buf, 200);
  JaclVal a = jacl_string_new(&heap, &table, buf, 200);
  JaclVal b = jacl_string_new(&heap, &table, buf, 200);

  ASSERT(jacl_is_rope_string(a));
  ASSERT(jacl_is_rope_string(b));
  ASSERT(jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 7. rope != rope (different content) */
static int test_eq_rope_rope_diff(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[200], buf_b[200];
  fill_ascii(buf_a, 200);
  for (size_t i = 0; i < 200; i++) buf_b[i] = 'A' + (char)(i % 26);

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 200);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 200);

  ASSERT(jacl_is_rope_string(a));
  ASSERT(jacl_is_rope_string(b));
  ASSERT(!jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 8. rope vs flat: same content → equal (cross-tier) */
static int test_eq_rope_flat_same(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Build a 50-byte flat string and a rope with the same content.
   * To get a rope with 50-byte content, we must build it from >128 bytes
   * then slice, or construct it directly. Let's use rope_from_str directly. */
  char buf[50]; fill_ascii(buf, 50);
  JaclVal flat = jacl_string_new(&heap, &table, buf, 50);
  ASSERT(jacl_is_heap_string(flat));

  /* Create rope with same 50-byte content by constructing manually */
  rope r = rope_from_str((const uint8_t*)buf, 50);
  uint32_t hash = rope_string__compute_hash(r);
  JaclRopeString* rs = (JaclRopeString*)gc_alloc(&heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
  rs->hash = hash;
  rs->r = r;
  JaclVal rope_val = jacl_rope_string_ptr(rs);

  ASSERT(jacl_is_rope_string(rope_val));
  ASSERT(jacl_string_eq(flat, rope_val));
  ASSERT(jacl_string_eq(rope_val, flat));  /* symmetric */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 9. rope vs inline: different content → not equal */
static int test_eq_rope_inline_diff(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal inl = jacl_string_new(&heap, &table, "hi", 2);
  char buf[200]; fill_ascii(buf, 200);
  JaclVal rp = jacl_string_new(&heap, &table, buf, 200);

  ASSERT(jacl_is_inline_string(inl));
  ASSERT(jacl_is_rope_string(rp));
  ASSERT(!jacl_string_eq(inl, rp));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 10. Hash collision handling: different strings with forced same hash still unequal */
static int test_eq_hash_collision(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Even if hashes collide (unlikely for most strings), equality still
   * falls through to content comparison. Test with different-length strings
   * to ensure the length check catches it before hash comparison. */
  JaclVal a = jacl_string_new(&heap, &table, "abcdefgh", 8);
  JaclVal b = jacl_string_new(&heap, &table, "abcdefghi", 9);

  ASSERT(!jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 11. Equality with combining marks: same NFD content across tiers */
static int test_eq_combining_marks(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "e\xCC\x81" (e + combining acute) = 3 bytes, inline tier */
  JaclVal a = jacl_string_new(&heap, &table, "e\xCC\x81", 3);
  /* Same content via NFC U+00E9 → NFD e + U+0301 */
  JaclVal b = jacl_string_new(&heap, &table, "\xC3\xA9", 2);

  ASSERT(jacl_is_inline_string(a));
  ASSERT(jacl_is_inline_string(b));
  /* Both should be NFD-normalized to same content */
  ASSERT(jacl_string_eq(a, b));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* ==== Comparison tests ==== */

/* 12. cmp: inline < inline */
static int test_cmp_inline_lt(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abc", 3);
  JaclVal b = jacl_string_new(&heap, &table, "abd", 3);

  ASSERT(jacl_string_cmp(a, b) < 0);
  ASSERT(jacl_string_cmp(b, a) > 0);
  ASSERT(jacl_string_cmp(a, a) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 13. cmp: flat strings */
static int test_cmp_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[20], buf_b[20];
  fill_ascii(buf_a, 20);   /* "abcdefghijklmnopqrst" */
  fill_ascii(buf_b, 20);
  buf_b[19] = 'z';         /* Make last byte different (larger) */

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 20);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 20);

  ASSERT(jacl_is_heap_string(a));
  ASSERT(jacl_is_heap_string(b));
  ASSERT(jacl_string_cmp(a, b) < 0);
  ASSERT(jacl_string_cmp(b, a) > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 14. cmp: rope vs rope */
static int test_cmp_rope_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[200], buf_b[200];
  fill_ascii(buf_a, 200);
  fill_ascii(buf_b, 200);
  buf_b[0] = 'z';  /* Make buf_b > buf_a */

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 200);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 200);

  ASSERT(jacl_is_rope_string(a));
  ASSERT(jacl_is_rope_string(b));
  ASSERT(jacl_string_cmp(a, b) < 0);
  ASSERT(jacl_string_cmp(b, a) > 0);

  /* Same content → 0 */
  JaclVal c = jacl_string_new(&heap, &table, buf_a, 200);
  ASSERT(jacl_string_cmp(a, c) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 15. cmp: cross-tier rope vs flat */
static int test_cmp_rope_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char flat_buf[50], rope_buf[200];
  fill_ascii(flat_buf, 50);    /* "abcde..." */
  fill_ascii(rope_buf, 200);   /* "abcde..." same prefix */

  JaclVal flat = jacl_string_new(&heap, &table, flat_buf, 50);
  JaclVal rp = jacl_string_new(&heap, &table, rope_buf, 200);

  ASSERT(jacl_is_heap_string(flat));
  ASSERT(jacl_is_rope_string(rp));

  /* flat (50 bytes) < rope (200 bytes) — same prefix, shorter is less */
  ASSERT(jacl_string_cmp(flat, rp) < 0);
  ASSERT(jacl_string_cmp(rp, flat) > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 16. cmp: cross-tier rope vs inline */
static int test_cmp_rope_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char rope_buf[200]; fill_ascii(rope_buf, 200);
  JaclVal inl = jacl_string_new(&heap, &table, "zzz", 3);
  JaclVal rp = jacl_string_new(&heap, &table, rope_buf, 200);

  ASSERT(jacl_is_inline_string(inl));
  ASSERT(jacl_is_rope_string(rp));

  /* "zzz" > "abcde..." (first byte differs) */
  ASSERT(jacl_string_cmp(inl, rp) > 0);
  ASSERT(jacl_string_cmp(rp, inl) < 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 17. cmp: prefix length ordering */
static int test_cmp_prefix_length(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abc", 3);
  JaclVal b = jacl_string_new(&heap, &table, "abcdef", 6);

  /* "abc" is a prefix of "abcdef", so "abc" < "abcdef" */
  ASSERT(jacl_string_cmp(a, b) < 0);
  ASSERT(jacl_string_cmp(b, a) > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 18. cmp: combining marks affect byte ordering */
static int test_cmp_combining(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "e\xCC\x81" (e + combining acute = 3 bytes NFD) */
  JaclVal a = jacl_string_new(&heap, &table, "e\xCC\x81", 3);
  /* "f" (1 byte) */
  JaclVal b = jacl_string_new(&heap, &table, "f", 1);

  /* "e..." < "f" in byte order */
  ASSERT(jacl_string_cmp(a, b) < 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* ==== VM integration tests ==== */

/* 19. [== "hello" "hello"] → true via VM */
static int test_vm_eq_true(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm; vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [== \"hello\" \"hello\"]]", &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 20. [== "abc" "abd"] → false via VM */
static int test_vm_eq_false(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm; vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [== \"abc\" \"abd\"]]", &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "false\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 21. [< "abc" "abd"] → true via VM */
static int test_vm_lt(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm; vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [< \"abc\" \"abd\"]]", &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 22. [> "xyz" "abc"] → true via VM */
static int test_vm_gt(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm; vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [> \"xyz\" \"abc\"]]", &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
    test_eq_inline_inline_same,
    test_eq_inline_inline_diff,
    test_eq_flat_flat_same,
    test_eq_flat_flat_diff,
    test_eq_inline_flat_diff_len,
    test_eq_rope_rope_same,
    test_eq_rope_rope_diff,
    test_eq_rope_flat_same,
    test_eq_rope_inline_diff,
    test_eq_hash_collision,
    test_eq_combining_marks,
    test_cmp_inline_lt,
    test_cmp_flat,
    test_cmp_rope_rope,
    test_cmp_rope_flat,
    test_cmp_rope_inline,
    test_cmp_prefix_length,
    test_cmp_combining,
    test_vm_eq_true,
    test_vm_eq_false,
    test_vm_lt,
    test_vm_gt,
  };

  int num_tests = (int)(sizeof(tests) / sizeof(tests[0]));
  int pass = 0, fail = 0;

  for (int i = 0; i < num_tests; i++) {
    if (tests[i]()) pass++;
    else fail++;
  }

  printf("%d/%d tests passed\n", pass, num_tests);
  return fail > 0 ? 1 : 0;
}
