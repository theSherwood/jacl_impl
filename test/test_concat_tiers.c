#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-010: Tier-aware string concatenation ===== */

static void fill_ascii(char* buf, size_t len) {
  for (size_t i = 0; i < len; i++) buf[i] = 'a' + (char)(i % 26);
}

/* Replicate OP_CONCAT logic for C-level tier verification */
static JaclVal do_concat(ThreadHeap* heap, JaclInternTable* table,
                          JaclVal a, JaclVal b) {
  uint32_t len_a = jacl_string_byte_len(a);
  uint32_t len_b = jacl_string_byte_len(b);
  uint32_t total = len_a + len_b;

  if (total <= 128) {
    char buf[256];
    jacl_string_data(a, buf, len_a);
    jacl_string_data(b, buf + len_a, len_b);
    return jacl_string_new(heap, table, buf, total);
  } else {
    rope ra, rb;
    if (jacl_is_rope_string(a)) {
      ra = jacl_as_rope_string(a)->r;
      rope_ref(ra);
    } else {
      char bufa[128];
      jacl_string_data(a, bufa, len_a);
      ra = rope_from_str((const uint8_t*)bufa, len_a);
    }
    if (jacl_is_rope_string(b)) {
      rb = jacl_as_rope_string(b)->r;
      rope_ref(rb);
    } else {
      char bufb[128];
      jacl_string_data(b, bufb, len_b);
      rb = rope_from_str((const uint8_t*)bufb, len_b);
    }
    rope rc = rope_concat(ra, rb);
    uint32_t hash = rope_string__compute_hash(rc);
    JaclRopeString* rs = (JaclRopeString*)gc_alloc(
        heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
    rs->hash = hash;
    rs->r = rc;
    JaclVal res = jacl_rope_string_ptr(rs);
    rope_unref(ra);
    rope_unref(rb);
    return res;
  }
}

/* Verify result content matches expected concatenation */
static bool verify_content(JaclVal v, const char* expected, size_t expected_len) {
  char buf[512];
  uint32_t actual_len = jacl_string_data(v, buf, sizeof(buf));
  if (actual_len != (uint32_t)expected_len) return false;
  return memcmp(buf, expected, expected_len) == 0;
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

/* ==== Tier combination tests (C-level) ==== */

/* 1. inline + inline → inline (3+4=7 bytes) */
static int test_inline_inline_to_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abc", 3);
  JaclVal b = jacl_string_new(&heap, &table, "defg", 4);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_inline_string(r));
  ASSERT(jacl_string_byte_len(r) == 7);
  ASSERT(verify_content(r, "abcdefg", 7));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 2. inline + inline → flat (4+4=8 bytes) */
static int test_inline_inline_to_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "abcd", 4);
  JaclVal b = jacl_string_new(&heap, &table, "efgh", 4);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_heap_string(r));
  ASSERT(jacl_string_byte_len(r) == 8);
  ASSERT(verify_content(r, "abcdefgh", 8));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 3. inline + flat → flat (5+50=55 bytes) */
static int test_inline_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char flat_buf[50]; fill_ascii(flat_buf, 50);
  JaclVal a = jacl_string_new(&heap, &table, "hello", 5);
  JaclVal b = jacl_string_new(&heap, &table, flat_buf, 50);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_heap_string(r));
  ASSERT(jacl_string_byte_len(r) == 55);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 4. flat + inline → flat (50+5=55 bytes) */
static int test_flat_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char flat_buf[50]; fill_ascii(flat_buf, 50);
  JaclVal a = jacl_string_new(&heap, &table, flat_buf, 50);
  JaclVal b = jacl_string_new(&heap, &table, "world", 5);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_heap_string(r));
  ASSERT(jacl_string_byte_len(r) == 55);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 5. flat + flat → flat (60+60=120 bytes, ≤128) */
static int test_flat_flat_to_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[60], buf_b[60];
  fill_ascii(buf_a, 60);
  /* Different content for b */
  for (size_t i = 0; i < 60; i++) buf_b[i] = 'A' + (char)(i % 26);

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 60);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 60);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_heap_string(r));
  ASSERT(jacl_string_byte_len(r) == 120);

  /* Verify content is concatenation of both */
  char expected[120];
  memcpy(expected, buf_a, 60);
  memcpy(expected + 60, buf_b, 60);
  ASSERT(verify_content(r, expected, 120));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 6. flat + flat → rope (70+70=140 bytes, >128) */
static int test_flat_flat_to_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[70], buf_b[70];
  fill_ascii(buf_a, 70);
  for (size_t i = 0; i < 70; i++) buf_b[i] = 'A' + (char)(i % 26);

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 70);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 70);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  ASSERT(jacl_string_byte_len(r) == 140);

  /* Verify content */
  char expected[140];
  memcpy(expected, buf_a, 70);
  memcpy(expected + 70, buf_b, 70);
  ASSERT(verify_content(r, expected, 140));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 7. inline + rope → rope (5+200=205 bytes) */
static int test_inline_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char rope_buf[200]; fill_ascii(rope_buf, 200);
  JaclVal a = jacl_string_new(&heap, &table, "hello", 5);
  JaclVal b = jacl_string_new(&heap, &table, rope_buf, 200);
  ASSERT(jacl_is_rope_string(b));
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  ASSERT(jacl_string_byte_len(r) == 205);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 8. rope + inline → rope (200+5=205 bytes) */
static int test_rope_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char rope_buf[200]; fill_ascii(rope_buf, 200);
  JaclVal a = jacl_string_new(&heap, &table, rope_buf, 200);
  JaclVal b = jacl_string_new(&heap, &table, "world", 5);
  ASSERT(jacl_is_rope_string(a));
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  ASSERT(jacl_string_byte_len(r) == 205);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 9. flat + rope → rope (50+200=250 bytes) */
static int test_flat_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char flat_buf[50]; fill_ascii(flat_buf, 50);
  char rope_buf[200]; fill_ascii(rope_buf, 200);
  JaclVal a = jacl_string_new(&heap, &table, flat_buf, 50);
  JaclVal b = jacl_string_new(&heap, &table, rope_buf, 200);
  ASSERT(jacl_is_heap_string(a));
  ASSERT(jacl_is_rope_string(b));
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  ASSERT(jacl_string_byte_len(r) == 250);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 10. rope + flat → rope (200+50=250 bytes) */
static int test_rope_flat(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char rope_buf[200]; fill_ascii(rope_buf, 200);
  char flat_buf[50]; fill_ascii(flat_buf, 50);
  JaclVal a = jacl_string_new(&heap, &table, rope_buf, 200);
  JaclVal b = jacl_string_new(&heap, &table, flat_buf, 50);
  ASSERT(jacl_is_rope_string(a));
  ASSERT(jacl_is_heap_string(b));
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  ASSERT(jacl_string_byte_len(r) == 250);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 11. rope + rope → rope (200+200=400 bytes) */
static int test_rope_rope(void) {
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
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  ASSERT(jacl_string_byte_len(r) == 400);

  /* Verify content */
  char expected[400], out[400];
  memcpy(expected, buf_a, 200);
  memcpy(expected + 200, buf_b, 200);
  jacl_string_data(r, out, 400);
  ASSERT(memcmp(out, expected, 400) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* ==== Size boundary tests ==== */

/* 12. 64+64=128 → flat (boundary) */
static int test_boundary_128(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[64], buf_b[64];
  fill_ascii(buf_a, 64);
  for (size_t i = 0; i < 64; i++) buf_b[i] = 'A' + (char)(i % 26);

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 64);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 64);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_heap_string(r));
  ASSERT(jacl_string_byte_len(r) == 128);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 13. 65+64=129 → rope (boundary) */
static int test_boundary_129(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[65], buf_b[64];
  fill_ascii(buf_a, 65);
  for (size_t i = 0; i < 64; i++) buf_b[i] = 'A' + (char)(i % 26);

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 65);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 64);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  ASSERT(jacl_string_byte_len(r) == 129);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* ==== Integration tests via jacl_run ==== */

/* 14. Variadic concat: [concat "a" "bc" "def"] → "abcdef" */
static int test_variadic_concat(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm; vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [concat \"hello\" \" \" \"world\"]]",
                              &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 15. Variadic concat length check */
static int test_variadic_length(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm; vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run(
      "[print [byte-length [concat \"abc\" \"defg\" \"hi\"]]]",
      &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  /* "abc" + "defg" + "hi" = "abcdefghi" = 9 bytes */
  ASSERT_STR_EQ(cap.buf, "9\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ==== Grapheme boundary tests ==== */

/* 16. Grapheme boundary at junction: right starts with combining mark */
static int test_grapheme_boundary_combining(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* left = "cafe" (4 bytes, 4 graphemes) */
  JaclVal left = jacl_string_new(&heap, &table, "cafe", 4);
  ASSERT(jacl_string_len(left) == 4);

  /* right = U+0301 (combining acute) + "st" = "\xCC\x81st" (4 bytes)
   * As standalone: combining mark is 1 grapheme + "st" = 3 graphemes */
  JaclVal right = jacl_string_new(&heap, &table, "\xCC\x81st", 4);
  ASSERT(jacl_string_len(right) == 3);

  /* concat = "cafe" + "\xCC\x81st" = "cafe\xCC\x81st" (8 bytes)
   * Graphemes: c, a, f, e+combining, s, t = 6 (not 4+3=7) */
  JaclVal r = do_concat(&heap, &table, left, right);
  ASSERT(jacl_is_heap_string(r));
  ASSERT(jacl_string_byte_len(r) == 8);
  ASSERT(jacl_string_len(r) == 6);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 17. No combining at junction: grapheme count is sum */
static int test_no_combining_boundary(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal a = jacl_string_new(&heap, &table, "hello", 5);
  JaclVal b = jacl_string_new(&heap, &table, " world!!!", 9);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_heap_string(r));
  ASSERT(jacl_string_byte_len(r) == 14);
  /* No combining at junction: grapheme count = 5+9 = 14 */
  ASSERT(jacl_string_len(r) == 14);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* ==== Hash consistency ==== */

/* 18. Rope hash matches flat FNV-1a of same content */
static int test_rope_hash_consistency(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf_a[100], buf_b[100];
  fill_ascii(buf_a, 100);
  for (size_t i = 0; i < 100; i++) buf_b[i] = 'A' + (char)(i % 26);

  JaclVal a = jacl_string_new(&heap, &table, buf_a, 100);
  JaclVal b = jacl_string_new(&heap, &table, buf_b, 100);
  JaclVal r = do_concat(&heap, &table, a, b);

  ASSERT(jacl_is_rope_string(r));
  JaclRopeString* rs = jacl_as_rope_string(r);

  /* Compute flat FNV-1a hash of the same content */
  char flat[200];
  memcpy(flat, buf_a, 100);
  memcpy(flat + 100, buf_b, 100);
  uint32_t flat_hash = string__fnv1a(flat, 200);

  ASSERT(rs->hash == flat_hash);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* ==== Empty string tests ==== */

/* 19. Concat with empty strings */
static int test_concat_empty(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal empty = jacl_string_new(&heap, &table, "", 0);
  JaclVal hello = jacl_string_new(&heap, &table, "hello", 5);

  /* empty + hello = hello */
  JaclVal r1 = do_concat(&heap, &table, empty, hello);
  ASSERT(jacl_is_inline_string(r1));
  ASSERT(jacl_string_byte_len(r1) == 5);
  ASSERT(verify_content(r1, "hello", 5));

  /* hello + empty = hello */
  JaclVal r2 = do_concat(&heap, &table, hello, empty);
  ASSERT(jacl_is_inline_string(r2));
  ASSERT(jacl_string_byte_len(r2) == 5);
  ASSERT(verify_content(r2, "hello", 5));

  /* empty + empty = empty */
  JaclVal r3 = do_concat(&heap, &table, empty, empty);
  ASSERT(jacl_is_inline_string(r3));
  ASSERT(jacl_string_byte_len(r3) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* 20. Concat via jacl_run: [concat "abc" "defgh"] = "abcdefgh" (inline→flat) */
static int test_concat_via_vm(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  PrintCapture cap = { .len = 0 };
  VM vm; vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  VMResult result = jacl_run("[print [concat \"abc\" \"defgh\"]]",
                              &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "abcdefgh\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* 21. NFD invariant: concat of two NFD strings stays NFD */
static int test_nfd_invariant(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "cafe" (4 bytes) + "\xCC\x81" (combining acute, 2 bytes) = "café" NFD (6 bytes) */
  JaclVal a = jacl_string_new(&heap, &table, "cafe", 4);
  JaclVal b = jacl_string_new(&heap, &table, "\xCC\x81", 2);
  JaclVal r = do_concat(&heap, &table, a, b);

  /* Verify result is valid NFD */
  char buf[8];
  uint32_t len = jacl_string_data(r, buf, sizeof(buf));
  ASSERT(len == 6);
  ASSERT(unicode_is_nfd((const uint8_t*)buf, len));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
    test_inline_inline_to_inline,
    test_inline_inline_to_flat,
    test_inline_flat,
    test_flat_inline,
    test_flat_flat_to_flat,
    test_flat_flat_to_rope,
    test_inline_rope,
    test_rope_inline,
    test_flat_rope,
    test_rope_flat,
    test_rope_rope,
    test_boundary_128,
    test_boundary_129,
    test_variadic_concat,
    test_variadic_length,
    test_grapheme_boundary_combining,
    test_no_combining_boundary,
    test_rope_hash_consistency,
    test_concat_empty,
    test_concat_via_vm,
    test_nfd_invariant,
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
