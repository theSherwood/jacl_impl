#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-004: JACL_TAG_ROPE_STRING and rope value type ===== */

/* Build a >128 byte ASCII string for rope creation */
static void fill_pattern(uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    buf[i] = 'a' + (uint8_t)(i % 26);
  }
}

/* Test: create JaclRopeString from >128 byte input, verify tag */
static int test_rope_string_tag(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));

  ASSERT(jacl_is_rope_string(v));
  ASSERT((v & JACL_TYPE_MASK) == JACL_TAG_ROPE_STRING);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: jacl_is_string returns true for rope strings */
static int test_rope_string_is_string(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));

  ASSERT(jacl_is_string(v));
  ASSERT(!jacl_is_inline_string(v));
  ASSERT(!jacl_is_heap_string(v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: jacl_is_heap_type returns true for rope strings */
static int test_rope_string_is_heap_type(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));

  ASSERT(jacl_is_heap_type(v));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: FNV-1a hash matches flat content hash */
static int test_rope_string_hash(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));
  JaclRopeString* rs = jacl_as_rope_string(v);

  /* Compute expected FNV-1a hash directly on flat content */
  uint32_t expected_hash = string__fnv1a((const char*)data, sizeof(data));

  ASSERT_U32_EQ(rs->hash, expected_hash);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: hash matches for large rope (multiple leaves, >256 bytes) */
static int test_rope_string_hash_large(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[1024];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));
  JaclRopeString* rs = jacl_as_rope_string(v);

  uint32_t expected_hash = string__fnv1a((const char*)data, sizeof(data));
  ASSERT_U32_EQ(rs->hash, expected_hash);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: jacl_as_rope_string extractor works */
static int test_rope_string_extractor(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));
  JaclRopeString* rs = jacl_as_rope_string(v);

  /* Verify round-trip: ptr -> tag -> extract -> same ptr */
  JaclVal v2 = jacl_rope_string_ptr(rs);
  ASSERT_U64_EQ(v & (JACL_TYPE_MASK | JACL_PAYLOAD_MASK),
                v2 & (JACL_TYPE_MASK | JACL_PAYLOAD_MASK));
  ASSERT_PTR_EQ(jacl_as_rope_string(v2), rs);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: rope byte count matches input length */
static int test_rope_string_byte_len(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[300];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));

  ASSERT_U32_EQ(jacl_string_byte_len(v), 300);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: rope grapheme count (ASCII, so graphemes == bytes) */
static int test_rope_string_grapheme_len(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));

  /* ASCII: grapheme count == byte count */
  ASSERT_U32_EQ(jacl_string_len(v), 200);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: jacl_string_data materializes rope content */
static int test_rope_string_data(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));

  char buf[256];
  uint32_t len = jacl_string_data(v, buf, sizeof(buf));
  ASSERT_U32_EQ(len, 200);
  ASSERT(memcmp(buf, data, 200) == 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: GC header has correct object type */
static int test_rope_string_gc_obj_type(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  uint8_t data[200];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));
  JaclRopeString* rs = jacl_as_rope_string(v);
  GCHeader* hdr = gc_header_of(rs);

  ASSERT(hdr->obj_type == OBJ_ROPE_STRING);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* Test: rope string with combining marks (grapheme count < byte count) */
static int test_rope_string_combining_marks(void) {
  tracker_reset();
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Build 200-byte string with combining marks:
   * 50 copies of 'a' + U+0301 (combining acute) = 50 * 4 bytes = 200 bytes
   * Each 'a' + combining = 1 grapheme cluster, so 50 graphemes */
  uint8_t data[200];
  size_t pos = 0;
  for (int i = 0; i < 50; i++) {
    data[pos++] = 'a';              /* base character */
    data[pos++] = 0xCC; data[pos++] = 0x81;  /* U+0301 combining acute */
    data[pos++] = 'b';              /* padding to reach 200 bytes */
  }

  JaclVal v = jacl_rope_string_create(&heap, data, sizeof(data));

  ASSERT_U32_EQ(jacl_string_byte_len(v), 200);
  /* Grapheme count: 50 * ('a'+combining = 1 grapheme) + 50 * ('b' = 1) = 100 */
  ASSERT_U32_EQ(jacl_string_len(v), 100);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
    test_rope_string_tag,
    test_rope_string_is_string,
    test_rope_string_is_heap_type,
    test_rope_string_hash,
    test_rope_string_hash_large,
    test_rope_string_extractor,
    test_rope_string_byte_len,
    test_rope_string_grapheme_len,
    test_rope_string_data,
    test_rope_string_gc_obj_type,
    test_rope_string_combining_marks,
  };

  const char* names[] = {
    "test_rope_string_tag",
    "test_rope_string_is_string",
    "test_rope_string_is_heap_type",
    "test_rope_string_hash",
    "test_rope_string_hash_large",
    "test_rope_string_extractor",
    "test_rope_string_byte_len",
    "test_rope_string_grapheme_len",
    "test_rope_string_data",
    "test_rope_string_gc_obj_type",
    "test_rope_string_combining_marks",
  };

  int n = (int)(sizeof(tests) / sizeof(tests[0]));
  int pass = 0, fail = 0;

  printf("=== US-004: JACL_TAG_ROPE_STRING and rope value type ===\n");
  for (int i = 0; i < n; i++) {
    printf("  %-50s ", names[i]);
    if (tests[i]()) {
      printf("PASS\n");
      pass++;
    } else {
      printf("FAIL\n");
      fail++;
    }
  }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
