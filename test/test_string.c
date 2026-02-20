#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-001: Heap string type and intern table ===== */

/* Test: intern a string and get back a JACL_TAG_STRING value */
static int test_intern_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v = jacl_intern(&arena, &table, "hello world", 11);

  ASSERT(jacl_is_heap_string(v));
  ASSERT(jacl_is_string(v));

  JaclHeapString* hs = jacl_as_heap_string(v);
  ASSERT_U32_EQ(hs->length, 11);
  ASSERT(memcmp(hs->data, "hello world", 11) == 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: interning same bytes twice returns same pointer (deduplication) */
static int test_intern_dedup(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v1 = jacl_intern(&arena, &table, "hello world", 11);
  JaclVal v2 = jacl_intern(&arena, &table, "hello world", 11);

  /* Pointer equality guaranteed by interning */
  ASSERT_U64_EQ(v1, v2);

  JaclHeapString* hs1 = jacl_as_heap_string(v1);
  JaclHeapString* hs2 = jacl_as_heap_string(v2);
  ASSERT_PTR_EQ(hs1, hs2);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: different strings get different pointers */
static int test_intern_different(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal v1 = jacl_intern(&arena, &table, "hello world", 11);
  JaclVal v2 = jacl_intern(&arena, &table, "goodbye world", 13);

  ASSERT(v1 != v2);

  JaclHeapString* hs1 = jacl_as_heap_string(v1);
  JaclHeapString* hs2 = jacl_as_heap_string(v2);
  ASSERT(hs1 != hs2);
  ASSERT_U32_EQ(hs1->length, 11);
  ASSERT_U32_EQ(hs2->length, 13);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: strings with embedded NUL bytes intern correctly */
static int test_intern_embedded_nul(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  const char data_with_nul[] = "hel\0lo w\0rld";
  uint32_t len = 12;  /* includes the NUL bytes */

  JaclVal v1 = jacl_intern(&arena, &table, data_with_nul, len);
  JaclVal v2 = jacl_intern(&arena, &table, data_with_nul, len);

  /* Should still deduplicate correctly */
  ASSERT_U64_EQ(v1, v2);

  JaclHeapString* hs = jacl_as_heap_string(v1);
  ASSERT_U32_EQ(hs->length, 12);
  ASSERT(memcmp(hs->data, data_with_nul, 12) == 0);

  /* Different string without NUL should be different */
  JaclVal v3 = jacl_intern(&arena, &table, "hello world!", 12);
  ASSERT(v1 != v3);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: hash function produces consistent results */
static int test_fnv1a_hash(void) {
  uint32_t h1 = string__fnv1a("hello", 5);
  uint32_t h2 = string__fnv1a("hello", 5);
  ASSERT_U32_EQ(h1, h2);

  uint32_t h3 = string__fnv1a("world", 5);
  ASSERT(h1 != h3);  /* different strings should (almost certainly) differ */

  /* Hash uses length, not null-terminator */
  uint32_t h4 = string__fnv1a("hell", 4);
  ASSERT(h1 != h4);

  TEST_PASS();
}

/* Test: table resize triggers at > 0.75 load factor */
static int test_intern_resize(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  /* Initial cap is 16, so inserting 13+ should trigger resize */
  char buf[32];
  JaclVal values[20];
  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf, sizeof(buf), "string number %02d", i);
    values[i] = jacl_intern(&arena, &table, buf, (uint32_t)n);
    ASSERT(jacl_is_heap_string(values[i]));
  }

  ASSERT_U32_EQ(table.count, 20);
  ASSERT(table.cap > INTERN_INIT_CAP);  /* must have resized */

  /* Verify all strings are still findable after resize */
  for (int i = 0; i < 20; i++) {
    int n = snprintf(buf, sizeof(buf), "string number %02d", i);
    JaclVal v = jacl_intern(&arena, &table, buf, (uint32_t)n);
    ASSERT_U64_EQ(v, values[i]);  /* pointer equality */
  }

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: is_heap_string predicate */
static int test_is_heap_string_predicate(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclInternTable table;
  intern_table_init(&table, &arena);

  JaclVal heap_str = jacl_intern(&arena, &table, "long string here", 16);
  JaclVal inline_str = jacl_inline_string("short", 5);
  JaclVal integer = jacl_i32(42);
  JaclVal nil = JACL_NIL;

  ASSERT(jacl_is_heap_string(heap_str) == true);
  ASSERT(jacl_is_heap_string(inline_str) == false);
  ASSERT(jacl_is_heap_string(integer) == false);
  ASSERT(jacl_is_heap_string(nil) == false);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-001: Heap string type and intern table */
    { "intern_basic",              test_intern_basic },
    { "intern_dedup",              test_intern_dedup },
    { "intern_different",          test_intern_different },
    { "intern_embedded_nul",       test_intern_embedded_nul },
    { "fnv1a_hash",                test_fnv1a_hash },
    { "intern_resize",             test_intern_resize },
    { "is_heap_string_predicate",  test_is_heap_string_predicate },
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
