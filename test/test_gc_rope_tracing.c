#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-008: GC tracing of rope trees with structural sharing ===== */

/* --- Helpers --- */

static void fill_pattern(uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) buf[i] = 'a' + (uint8_t)(i % 26);
}

/* Count live GC objects of a specific type by walking the heap */
static size_t count_objects_of_type(ThreadHeap *heap, uint8_t obj_type) {
  size_t count = 0;
  for (GCBlock *b = heap->blocks; b; b = b->next) {
    uint8_t *ptr = b->payload;
    uint8_t *end = b->payload + GC_BLOCK_SIZE;
    while (ptr < end) {
      GCHeader *hdr = (GCHeader *)ptr;
      uint16_t total = hdr->alloc_total;
      if (total == 0) {
        size_t offset = (size_t)(ptr - b->payload);
        size_t next_line = ((offset / GC_LINE_SIZE) + 1) * GC_LINE_SIZE;
        if (next_line >= GC_BLOCK_SIZE) break;
        ptr = b->payload + next_line;
        continue;
      }
      if (hdr->obj_type == obj_type) count++;
      ptr += total;
    }
  }
  return count;
}

/* Check if all objects of a given type in the heap have gen == expected_gen */
static bool all_objects_have_gen(ThreadHeap *heap, uint8_t obj_type,
                                uint8_t expected_gen) {
  for (GCBlock *b = heap->blocks; b; b = b->next) {
    uint8_t *ptr = b->payload;
    uint8_t *end = b->payload + GC_BLOCK_SIZE;
    while (ptr < end) {
      GCHeader *hdr = (GCHeader *)ptr;
      uint16_t total = hdr->alloc_total;
      if (total == 0) {
        size_t offset = (size_t)(ptr - b->payload);
        size_t next_line = ((offset / GC_LINE_SIZE) + 1) * GC_LINE_SIZE;
        if (next_line >= GC_BLOCK_SIZE) break;
        ptr = b->payload + next_line;
        continue;
      }
      if (hdr->obj_type == obj_type && hdr->gen != expected_gen) return false;
      ptr += total;
    }
  }
  return true;
}

/* Concat two rope strings, creating structural sharing */
static JaclVal mk_rope_concat(ThreadHeap *heap, JaclVal a, JaclVal b) {
  rope ra, rb;
  if (jacl_is_rope_string(a)) {
    ra = jacl_as_rope_string(a)->r;
  } else {
    char buf[128];
    uint32_t len_a = jacl_string_byte_len(a);
    jacl_string_data(a, buf, len_a);
    ra = rope_from_str((const uint8_t *)buf, len_a);
  }
  if (jacl_is_rope_string(b)) {
    rb = jacl_as_rope_string(b)->r;
  } else {
    char buf[128];
    uint32_t len_b = jacl_string_byte_len(b);
    jacl_string_data(b, buf, len_b);
    rb = rope_from_str((const uint8_t *)buf, len_b);
  }
  rope rc = rope_concat(ra, rb);
  uint32_t hash = rope_string__compute_hash(rc);
  JaclRopeString *rs =
      (JaclRopeString *)gc_alloc(heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
  rs->hash = hash;
  rs->r = rc;
  return jacl_rope_string_ptr(rs);
}

/* --- Test 1: Rooted rope string survives full GC --- */

static int test_rope_survives_gc(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);

  uint8_t data[300];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&vm.heap, data, sizeof(data));
  vm.stack[0] = v;
  vm.stack_top = 1;

  /* Verify rope nodes exist before GC */
  size_t rope_count = count_objects_of_type(&vm.heap, OBJ_ROPE_STRING);
  size_t leaf_count = count_objects_of_type(&vm.heap, OBJ_ROPE_LEAF);
  ASSERT(rope_count >= 1);
  ASSERT(leaf_count >= 1);

  gc_collect(&vm.heap, &vm);

  /* Rope string and all nodes survive */
  ASSERT(count_objects_of_type(&vm.heap, OBJ_ROPE_STRING) == rope_count);
  ASSERT(count_objects_of_type(&vm.heap, OBJ_ROPE_LEAF) >= leaf_count);

  /* Content is intact */
  char buf[300];
  uint32_t len = jacl_string_data(vm.stack[0], buf, sizeof(buf));
  ASSERT_U32_EQ(len, 300);
  ASSERT(memcmp(buf, data, 300) == 0);

  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test 2: Unrooted rope string is collected --- */

static int test_rope_collected_when_unrooted(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);

  uint8_t data[300];
  fill_pattern(data, sizeof(data));

  JaclVal v = jacl_rope_string_create(&vm.heap, data, sizeof(data));
  (void)v; /* don't root it */

  /* Verify rope objects were created */
  size_t rope_count = count_objects_of_type(&vm.heap, OBJ_ROPE_STRING);
  size_t leaf_count = count_objects_of_type(&vm.heap, OBJ_ROPE_LEAF);
  ASSERT(rope_count >= 1);
  ASSERT(leaf_count >= 1);

  /* Nothing rooted */
  vm.stack_top = 0;

  gc_collect(&vm.heap, &vm);

  /* All rope objects should be collected */
  ASSERT_SIZE_EQ(count_objects_of_type(&vm.heap, OBJ_ROPE_STRING), 0);
  ASSERT_SIZE_EQ(count_objects_of_type(&vm.heap, OBJ_ROPE_LEAF), 0);
  ASSERT_SIZE_EQ(count_objects_of_type(&vm.heap, OBJ_ROPE_INTERNAL), 0);

  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test 3: Structural sharing — drop one rope, shared nodes survive --- */

static int test_rope_structural_sharing_gc(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);

  /* Create shared base rope (300 bytes) */
  uint8_t shared_data[300];
  for (size_t i = 0; i < sizeof(shared_data); i++) shared_data[i] = 'S';
  JaclVal valShared = jacl_rope_string_create(&vm.heap, shared_data,
                                               sizeof(shared_data));

  /* Create unique-A rope (300 bytes) */
  uint8_t dataA[300];
  for (size_t i = 0; i < sizeof(dataA); i++) dataA[i] = 'A';
  JaclVal valA_unique = jacl_rope_string_create(&vm.heap, dataA, sizeof(dataA));

  /* Create unique-B rope (300 bytes) */
  uint8_t dataB[300];
  for (size_t i = 0; i < sizeof(dataB); i++) dataB[i] = 'B';
  JaclVal valB_unique = jacl_rope_string_create(&vm.heap, dataB, sizeof(dataB));

  /* ropeA = concat(shared, uniqueA) — shares shared's subtree */
  JaclVal valA = mk_rope_concat(&vm.heap, valShared, valA_unique);

  /* ropeB = concat(shared, uniqueB) — shares shared's subtree */
  JaclVal valB = mk_rope_concat(&vm.heap, valShared, valB_unique);

  /* Save A's JaclRopeString pointer for post-GC verification */
  JaclRopeString *rsA = jacl_as_rope_string(valA);
  JaclRopeString *rsB = jacl_as_rope_string(valB);
  (void)rsA; /* used below to verify collection */

  /* Root only valB — drop valA, valShared, valA_unique, valB_unique */
  vm.stack[0] = valB;
  vm.stack_top = 1;

  gc_collect(&vm.heap, &vm);

  /* B's JaclRopeString survived — verify via GC header */
  ASSERT(gc_header_of(rsB)->alloc_total > 0);
  ASSERT(gc_header_of(rsB)->obj_type == OBJ_ROPE_STRING);

  /* Verify B's content is intact (shared + uniqueB).
   * This proves shared subtree nodes survived via B's tracing. */
  char buf[600];
  uint32_t lenB = jacl_string_data(valB, buf, sizeof(buf));
  ASSERT_U32_EQ(lenB, 600);
  ASSERT(memcmp(buf, shared_data, 300) == 0);
  ASSERT(memcmp(buf + 300, dataB, 300) == 0);

  /* A's JaclRopeString was collected (exclusive to A, zeroed by sweep) */
  ASSERT(gc_header_of(rsA)->alloc_total == 0);

  /* Run a second GC cycle to verify tracing works across cycles */
  gc_collect(&vm.heap, &vm);

  /* B still survives second cycle */
  char buf2[600];
  uint32_t lenB2 = jacl_string_data(valB, buf2, sizeof(buf2));
  ASSERT_U32_EQ(lenB2, 600);
  ASSERT(memcmp(buf2, shared_data, 300) == 0);
  ASSERT(memcmp(buf2 + 300, dataB, 300) == 0);

  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test 4: Large rope — minor GC promotes young-gen nodes after 2 cycles --- */

static int test_rope_minor_gc_promotion(void) {
  tracker_reset();
  arena_t arena = {0};
  VM vm;
  vm_init(&vm, &arena);

  /* Create a large rope (>10KB) */
  uint8_t *data = (uint8_t *)malloc(11000);
  ASSERT(data != NULL);
  fill_pattern(data, 11000);

  JaclVal v = jacl_rope_string_create(&vm.heap, data, 11000);
  vm.stack[0] = v;
  vm.stack_top = 1;

  /* Verify nodes are young generation initially */
  JaclRopeString *rs = jacl_as_rope_string(v);
  ASSERT(gc_header_of(rs)->gen == 0);
  ASSERT(all_objects_have_gen(&vm.heap, OBJ_ROPE_LEAF, 0));

  /* Minor GC needs a RememberedSet */
  RememberedSet rset;
  remembered_set_init(&rset);

  /* Run 2 minor GC cycles — objects should be promoted after surviving both */
  gc_collect_minor(&vm.heap, &vm, &rset);
  gc_collect_minor(&vm.heap, &vm, &rset);

  /* After 2 cycles, rope nodes should be promoted to old gen */
  ASSERT(gc_header_of(rs)->gen == 1);
  ASSERT(all_objects_have_gen(&vm.heap, OBJ_ROPE_LEAF, 1));
  ASSERT(all_objects_have_gen(&vm.heap, OBJ_ROPE_INTERNAL, 1));

  /* Content still intact after promotion */
  char *buf = (char *)malloc(11000);
  ASSERT(buf != NULL);
  uint32_t len = jacl_string_data(v, buf, 11000);
  ASSERT_U32_EQ(len, 11000);
  ASSERT(memcmp(buf, data, 11000) == 0);

  free(buf);
  free(data);
  remembered_set_destroy(&rset);
  vm_destroy(&vm);
  arena_destroy(&arena);
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
      test_rope_survives_gc,
      test_rope_collected_when_unrooted,
      test_rope_structural_sharing_gc,
      test_rope_minor_gc_promotion,
  };

  const char *names[] = {
      "test_rope_survives_gc",
      "test_rope_collected_when_unrooted",
      "test_rope_structural_sharing_gc",
      "test_rope_minor_gc_promotion",
  };

  int n = (int)(sizeof(tests) / sizeof(tests[0]));
  int pass = 0, fail = 0;

  printf("=== US-008: GC tracing of rope trees with structural sharing ===\n");
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
