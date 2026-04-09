#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-009: Intern table eviction ===== */

/* Helper: directly insert a JaclHeapString into the intern table,
 * bypassing jacl_intern's resize logic. Allows test setup with
 * load factor > 0.75 in a cap=16 table. */
static JaclHeapString *intern_insert_direct(ThreadHeap *heap,
                                             JaclInternTable *table,
                                             const char *data,
                                             uint32_t length) {
  uint32_t hash = string__fnv1a(data, length);
  JaclHeapString *hs = (JaclHeapString *)gc_alloc(
      heap, OBJ_STRING, sizeof(JaclHeapString) + length);
  hs->byte_len = length;
  hs->grapheme_len =
      (uint32_t)unicode_grapheme_count((const uint8_t *)data, (size_t)length);
  hs->hash = hash;
  memcpy(hs->data, data, length);

  JaclHeapString **slot =
      intern__find_slot(table->entries, table->cap, data, length, hash);
  *slot = hs;
  table->count++;
  return hs;
}

/* --- Test 1: Intern 100 strings, verify table->count == 100 --- */

static int test_intern_100_count(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);
  JaclInternTable table;
  intern_table_init(&table, &arena);
  vm.intern_table = &table;

  /* Intern 100 unique strings, each 20 bytes (in 8-128 range) */
  for (int i = 0; i < 100; i++) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "intern_test_str_%04d", i);
    jacl_intern(&vm.heap, &table, buf, (uint32_t)len);
  }

  ASSERT_U32_EQ(table.count, 100);

  intern_table_destroy(&table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test 2: Drop all references, trigger full GC with load > 0.75,
 *             verify table->count has decreased --- */

static int test_intern_eviction_after_gc(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);
  JaclInternTable table;
  intern_table_init(&table, &arena);
  vm.intern_table = &table;

  /* Insert 13 entries directly into cap=16 table.
   * Load factor = 13/16 = 0.8125 > 0.75, so gc_sweep_intern_table
   * will evict dead entries instead of retroactively marking them. */
  for (int i = 0; i < 13; i++) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "evict_str_%04d", i);
    intern_insert_direct(&vm.heap, &table, buf, (uint32_t)len);
  }

  ASSERT_U32_EQ(table.count, 13);
  ASSERT_U32_EQ(table.cap, 16);

  /* Nothing rooted — all strings are unreachable */
  vm.stack_top = 0;

  /* Full GC: mark phase won't trace intern table (removed from roots),
   * gc_sweep_intern_table evicts dead entries as tombstones,
   * gc_sweep zeroes dead objects. */
  gc_collect(&vm.heap, &vm);

  /* All 13 entries evicted */
  ASSERT_U32_EQ(table.count, 0);
  ASSERT_U32_EQ(table.tombstone_count, 13);

  intern_table_destroy(&table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test 3: After eviction, re-intern a previously evicted string —
 *             verify it creates a new JaclHeapString (different pointer) --- */

static int test_reintern_after_eviction(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);
  JaclInternTable table;
  intern_table_init(&table, &arena);
  vm.intern_table = &table;

  /* Insert 13 entries, save pointer to the first one */
  const char *target_str = "reintern_target";  /* 15 bytes, in 8-128 range */
  uint32_t target_len = (uint32_t)strlen(target_str);
  JaclHeapString *original_ptr =
      intern_insert_direct(&vm.heap, &table, target_str, target_len);

  for (int i = 1; i < 13; i++) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "evict_rein_%04d", i);
    intern_insert_direct(&vm.heap, &table, buf, (uint32_t)len);
  }

  ASSERT_U32_EQ(table.count, 13);

  /* Nothing rooted */
  vm.stack_top = 0;

  /* Full GC → evicts all 13 entries */
  gc_collect(&vm.heap, &vm);
  ASSERT_U32_EQ(table.count, 0);

  /* Advance allocator past potential same-address reuse */
  (void)gc_alloc(&vm.heap, OBJ_STRING, 64);

  /* Re-intern the same string content */
  JaclVal new_val =
      jacl_intern(&vm.heap, &table, target_str, target_len);
  JaclHeapString *new_ptr = jacl_as_heap_string(new_val);

  /* Must be a different pointer (new allocation, old was swept) */
  ASSERT(new_ptr != original_ptr);
  ASSERT_U32_EQ(table.count, 1);

  /* Content matches */
  ASSERT_U32_EQ(new_ptr->byte_len, target_len);
  ASSERT(memcmp(new_ptr->data, target_str, target_len) == 0);

  intern_table_destroy(&table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test 4: Tombstone compaction — intern strings, evict most,
 *             trigger insert that causes compaction, verify
 *             tombstones are removed and survivors rehashed --- */

static int test_tombstone_compaction(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);
  JaclInternTable table;
  intern_table_init(&table, &arena);
  vm.intern_table = &table;

  /* Insert 13 entries directly (cap=16, load > 0.75) */
  JaclVal survivors[3];
  for (int i = 0; i < 13; i++) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "compact_str_%03d", i);
    JaclHeapString *hs =
        intern_insert_direct(&vm.heap, &table, buf, (uint32_t)len);
    if (i < 3) {
      survivors[i] = jacl_string_ptr(hs);
    }
  }

  ASSERT_U32_EQ(table.count, 13);
  ASSERT_U32_EQ(table.cap, 16);

  /* Root first 3 strings on VM stack */
  vm.stack[0] = survivors[0];
  vm.stack[1] = survivors[1];
  vm.stack[2] = survivors[2];
  vm.stack_top = 3;

  /* Full GC: evicts 10 dead entries, keeps 3 alive (marked via stack roots) */
  gc_collect(&vm.heap, &vm);

  ASSERT_U32_EQ(table.count, 3);
  ASSERT_U32_EQ(table.tombstone_count, 10);

  /* Insert a new string via jacl_intern.
   * tombstone_count (10) * 4 = 40 > cap (16) → triggers compaction.
   * Compaction rehashes survivors, removes tombstones. */
  const char *new_str = "compact_newstr_";  /* 15 bytes */
  jacl_intern(&vm.heap, &table, new_str, (uint32_t)strlen(new_str));

  /* After compaction: tombstones removed, count = 3 survivors + 1 new */
  ASSERT_U32_EQ(table.tombstone_count, 0);
  ASSERT_U32_EQ(table.count, 4);

  /* Verify the 3 survivors are still findable (same pointer) */
  for (int i = 0; i < 3; i++) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "compact_str_%03d", i);
    JaclVal found = jacl_intern(&vm.heap, &table, buf, (uint32_t)len);
    ASSERT(jacl_as_heap_string(found) == jacl_as_heap_string(survivors[i]));
  }

  intern_table_destroy(&table);
  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
      test_intern_100_count,
      test_intern_eviction_after_gc,
      test_reintern_after_eviction,
      test_tombstone_compaction,
  };

  const char *names[] = {
      "test_intern_100_count",
      "test_intern_eviction_after_gc",
      "test_reintern_after_eviction",
      "test_tombstone_compaction",
  };

  int n = (int)(sizeof(tests) / sizeof(tests[0]));
  int pass = 0, fail = 0;

  printf("=== US-009: Intern table eviction ===\n");
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
