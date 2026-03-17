#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-006: jacl_string_new() unified constructor with tier routing ===== */

/* Helper: build a repeated ASCII string of exact byte length */
static void fill_ascii(char* buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    buf[i] = 'a' + (char)(i % 26);
  }
}

/* Test: 7-byte string → inline */
static int test_tier_inline(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "hello!!", 7);
  ASSERT(jacl_is_inline_string(v));
  ASSERT(jacl_string_byte_len(v) == 7);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: 8-byte string → flat interned */
static int test_tier_flat_8(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "hello!!!", 8);
  ASSERT(jacl_is_heap_string(v));
  ASSERT(jacl_string_byte_len(v) == 8);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: 128-byte string → flat interned */
static int test_tier_flat_128(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[128];
  fill_ascii(buf, 128);

  JaclVal v = jacl_string_new(&heap, &table, buf, 128);
  ASSERT(jacl_is_heap_string(v));
  ASSERT(jacl_string_byte_len(v) == 128);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: 129-byte string → rope */
static int test_tier_rope_129(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[129];
  fill_ascii(buf, 129);

  JaclVal v = jacl_string_new(&heap, &table, buf, 129);
  ASSERT(jacl_is_rope_string(v));
  ASSERT(jacl_string_byte_len(v) == 129);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: NFC 120-byte string expands to 180 bytes NFD → rope */
static int test_nfc_expansion_to_rope(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* 60 copies of U+00E9 (2 bytes NFC each) = 120 bytes NFC.
   * NFD: 60 copies of 'e' + U+0301 (3 bytes each) = 180 bytes → rope tier. */
  char nfc_buf[120];
  for (int i = 0; i < 60; i++) {
    nfc_buf[i * 2]     = (char)0xC3;
    nfc_buf[i * 2 + 1] = (char)0xA9;
  }

  JaclVal v = jacl_string_new(&heap, &table, nfc_buf, 120);
  /* Post-NFD is 180 bytes → should be rope */
  ASSERT(jacl_is_rope_string(v));
  ASSERT(jacl_string_byte_len(v) == 180);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: same content interned twice → same pointer (interning still works) */
static int test_interning_dedup(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v1 = jacl_string_new(&heap, &table, "interned!", 9);
  JaclVal v2 = jacl_string_new(&heap, &table, "interned!", 9);
  ASSERT(jacl_is_heap_string(v1));
  ASSERT(jacl_is_heap_string(v2));
  /* Same pointer from interning */
  ASSERT((v1 & JACL_PAYLOAD_MASK) == (v2 & JACL_PAYLOAD_MASK));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: invalid UTF-8 returns JACL_NIL */
static int test_invalid_utf8_returns_nil(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* Overlong encoding */
  JaclVal v = jacl_string_new(&heap, &table, "\xC0\xAF", 2);
  ASSERT(jacl_is_nil(v));

  /* Surrogate half */
  v = jacl_string_new(&heap, &table, "\xED\xA0\x80", 3);
  ASSERT(jacl_is_nil(v));

  /* Truncated sequence */
  v = jacl_string_new(&heap, &table, "abc\xC3", 4);
  ASSERT(jacl_is_nil(v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: empty string → inline */
static int test_empty_string(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "", 0);
  ASSERT(jacl_is_inline_string(v));
  ASSERT(jacl_string_byte_len(v) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: NFD normalization applied — NFC input is decomposed */
static int test_nfd_applied(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  /* "caf\xC3\xA9" = "café" in NFC (5 bytes)
   * NFD: "cafe\xCC\x81" (6 bytes) → inline (≤7) */
  JaclVal v = jacl_string_new(&heap, &table, "caf\xC3\xA9", 5);
  ASSERT(jacl_is_inline_string(v));
  /* NFD expansion: 5 NFC → 6 NFD bytes */
  ASSERT(jacl_string_byte_len(v) == 6);

  /* Verify content is NFD */
  char buf[8];
  jacl_string_data(v, buf, sizeof(buf));
  ASSERT(buf[0] == 'c');
  ASSERT(buf[1] == 'a');
  ASSERT(buf[2] == 'f');
  ASSERT(buf[3] == 'e');
  ASSERT((uint8_t)buf[4] == 0xCC);
  ASSERT((uint8_t)buf[5] == 0x81);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: already-NFD ASCII passes through without allocation */
static int test_ascii_fast_path(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  JaclVal v = jacl_string_new(&heap, &table, "hello", 5);
  ASSERT(jacl_is_inline_string(v));
  ASSERT(jacl_string_byte_len(v) == 5);

  /* Longer ASCII → flat */
  v = jacl_string_new(&heap, &table, "hello world!!", 13);
  ASSERT(jacl_is_heap_string(v));
  ASSERT(jacl_string_byte_len(v) == 13);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* Test: rope string content and hash are correct */
static int test_rope_content_and_hash(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  arena_t arena = { .allocator = tracked_allocator };
  JaclInternTable table; intern_table_init(&table, &arena);

  char buf[200];
  fill_ascii(buf, 200);

  JaclVal v = jacl_string_new(&heap, &table, buf, 200);
  ASSERT(jacl_is_rope_string(v));

  /* Verify hash matches flat FNV-1a */
  uint32_t flat_hash = string__fnv1a(buf, 200);
  JaclRopeString* rs = jacl_as_rope_string(v);
  ASSERT(rs->hash == flat_hash);

  /* Verify content roundtrip */
  char out[200];
  jacl_string_data(v, out, 200);
  ASSERT(memcmp(buf, out, 200) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
    test_tier_inline,
    test_tier_flat_8,
    test_tier_flat_128,
    test_tier_rope_129,
    test_nfc_expansion_to_rope,
    test_interning_dedup,
    test_invalid_utf8_returns_nil,
    test_empty_string,
    test_nfd_applied,
    test_ascii_fast_path,
    test_rope_content_and_hash,
  };

  int num_tests = (int)(sizeof(tests) / sizeof(tests[0]));
  int pass = 0, fail = 0;

  for (int i = 0; i < num_tests; i++) {
    if (tests[i]()) {
      pass++;
    } else {
      fail++;
    }
  }

  printf("%d/%d tests passed\n", pass, num_tests);
  return fail > 0 ? 1 : 0;
}
