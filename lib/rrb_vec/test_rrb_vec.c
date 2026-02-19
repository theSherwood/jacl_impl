#include "../../test/test_helpers.h"

#define TRACKED_ALLOC                     \
  {                                       \
      .alloc = tracked_malloc_with_ctx,   \
      .free  = tracked_free_with_ctx,     \
  }

/* --- Instantiation 1: double vector (must come first so int_vec can reference it) --- */
#define RRB_VEC_T         double
#define RRB_VEC_NAME      dbl_vec
#define RRB_VEC_ALLOCATOR TRACKED_ALLOC
#include "rrb_vec.h"

/* --- Instantiation 2: int vector with map_into targeting dbl_vec --- */
#define RRB_VEC_T         int
#define RRB_VEC_NAME      int_vec
#define RRB_VEC_ALLOCATOR TRACKED_ALLOC
#define RRB_VEC_MAP_INTO_DEST_NAME dbl_vec
#define RRB_VEC_MAP_INTO_DEST_T    double
#include "rrb_vec.h"

/* Test: create empty vec, verify count is 0, unref, verify no leaks */
int test_empty_vec(void) {
  printf("  test_empty_vec: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  ASSERT(v != NULL);
  ASSERT_INT_EQ(int_vec_count(v), 0);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: instantiate two different types in the same compilation unit */
int test_multiple_instantiations(void) {
  printf("  test_multiple_instantiations: ");
  tracker_reset();

  int_vec_root* iv = int_vec_empty();
  dbl_vec_root* dv = dbl_vec_empty();

  ASSERT(iv != NULL);
  ASSERT(dv != NULL);
  ASSERT_INT_EQ(int_vec_count(iv), 0);
  ASSERT_INT_EQ(dbl_vec_count(dv), 0);

  int_vec_unref(iv);
  dbl_vec_unref(dv);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: ref increments, unref decrements */
int test_ref_unref(void) {
  printf("  test_ref_unref: ");
  tracker_reset();

  int_vec_root* v  = int_vec_empty();
  int_vec_root* v2 = int_vec_ref(v);
  ASSERT(v == v2);

  int_vec_unref(v2);
  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push 1-5 elements, verify get returns correct values */
int test_push_and_get_small(void) {
  printf("  test_push_and_get_small: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i * 10);
    int_vec_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(int_vec_count(v), 5);
  for (int i = 0; i < 5; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i * 10);
  }

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push 1000+ elements, verify all values correct (exercises multi-level tree) */
int test_push_and_get_large(void) {
  printf("  test_push_and_get_large: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 2000; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(int_vec_count(v), 2000);
  for (int i = 0; i < 2000; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push to create v1, push again to create v2, verify v1 values unchanged */
int test_persistence(void) {
  printf("  test_persistence: ");
  tracker_reset();

  int_vec_root* v1 = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v1, i);
    int_vec_unref(v1);
    v1 = next;
  }

  /* v2 = v1 + more elements */
  int_vec_root* v2 = int_vec_ref(v1);
  for (int i = 10; i < 20; i++) {
    int_vec_root* next = int_vec_push_back(v2, i);
    int_vec_unref(v2);
    v2 = next;
  }

  /* v1 should be unchanged */
  ASSERT_INT_EQ(int_vec_count(v1), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(v1, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  /* v2 should have all 20 */
  ASSERT_INT_EQ(int_vec_count(v2), 20);
  for (int i = 0; i < 20; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v1);
  int_vec_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: create two versions from same base, unref base, verify both still valid */
int test_shared_base(void) {
  printf("  test_shared_base: ");
  tracker_reset();

  int_vec_root* base = int_vec_empty();
  for (int i = 0; i < 50; i++) {
    int_vec_root* next = int_vec_push_back(base, i);
    int_vec_unref(base);
    base = next;
  }

  int_vec_root* v1 = int_vec_push_back(base, 100);
  int_vec_root* v2 = int_vec_push_back(base, 200);
  int_vec_unref(base);

  /* Both should share the same tree structure but have different tails */
  ASSERT_INT_EQ(int_vec_count(v1), 51);
  ASSERT_INT_EQ(int_vec_count(v2), 51);

  int_vec_get_result r1 = int_vec_get(v1, 50);
  ASSERT(r1.found);
  ASSERT_INT_EQ(r1.value, 100);

  int_vec_get_result r2 = int_vec_get(v2, 50);
  ASSERT(r2.found);
  ASSERT_INT_EQ(r2.value, 200);

  /* First 50 elements should be the same */
  for (int i = 0; i < 50; i++) {
    int_vec_get_result g1 = int_vec_get(v1, (uint32_t)i);
    int_vec_get_result g2 = int_vec_get(v2, (uint32_t)i);
    ASSERT(g1.found);
    ASSERT(g2.found);
    ASSERT_INT_EQ(g1.value, i);
    ASSERT_INT_EQ(g2.value, i);
  }

  int_vec_unref(v1);
  int_vec_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: get with out-of-bounds index returns .found == false */
int test_get_out_of_bounds(void) {
  printf("  test_get_out_of_bounds: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_get_result r = int_vec_get(v, 0);
  ASSERT(!r.found);

  int_vec_root* v2 = int_vec_push_back(v, 42);
  r = int_vec_get(v2, 1);
  ASSERT(!r.found);
  r = int_vec_get(v2, 100);
  ASSERT(!r.found);

  /* Valid index still works */
  r = int_vec_get(v2, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 42);

  int_vec_unref(v);
  int_vec_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push N elements, set element at index 0, middle, and last — verify old and new versions */
int test_set(void) {
  printf("  test_set: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Set first element */
  int_vec_root* v2 = int_vec_set(v, 0, 999);
  ASSERT(v2 != NULL);
  int_vec_get_result r = int_vec_get(v2, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 999);

  /* Set middle element */
  int_vec_root* v3 = int_vec_set(v, 50, 888);
  ASSERT(v3 != NULL);
  r = int_vec_get(v3, 50);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 888);

  /* Set last element */
  int_vec_root* v4 = int_vec_set(v, 99, 777);
  ASSERT(v4 != NULL);
  r = int_vec_get(v4, 99);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 777);

  /* Original unchanged */
  r = int_vec_get(v, 0);
  ASSERT_INT_EQ(r.value, 0);
  r = int_vec_get(v, 50);
  ASSERT_INT_EQ(r.value, 50);
  r = int_vec_get(v, 99);
  ASSERT_INT_EQ(r.value, 99);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_unref(v3);
  int_vec_unref(v4);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set out-of-bounds returns NULL */
int test_set_out_of_bounds(void) {
  printf("  test_set_out_of_bounds: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  ASSERT(int_vec_set(v, 10, 42) == NULL);
  ASSERT(int_vec_set(v, 100, 42) == NULL);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push N elements, pop all elements one by one, verify count decreases, verify no leaks */
int test_pop_back(void) {
  printf("  test_pop_back: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Pop all elements, verify values in reverse order */
  for (int i = 99; i >= 0; i--) {
    int_vec_pop_result pr = int_vec_pop_back(v);
    ASSERT(pr.found);
    ASSERT_INT_EQ(pr.value, i);
    ASSERT_INT_EQ(int_vec_count(pr.root), (uint32_t)i);
    int_vec_unref(v);
    v = pr.root;
  }

  ASSERT_INT_EQ(int_vec_count(v), 0);
  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop on empty returns .found == false */
int test_pop_empty(void) {
  printf("  test_pop_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_pop_result pr = int_vec_pop_back(v);
  ASSERT(!pr.found);
  ASSERT(pr.root == NULL);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop preserves persistence — old root still valid after pop */
int test_pop_persistence(void) {
  printf("  test_pop_persistence: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 50; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_pop_result pr = int_vec_pop_back(v);
  ASSERT(pr.found);
  ASSERT_INT_EQ(pr.value, 49);
  ASSERT_INT_EQ(int_vec_count(pr.root), 49);

  /* Original should still be valid */
  ASSERT_INT_EQ(int_vec_count(v), 50);
  int_vec_get_result r = int_vec_get(v, 49);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 49);

  int_vec_unref(v);
  int_vec_unref(pr.root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop large vector — exercises tree shrinking */
int test_pop_large(void) {
  printf("  test_pop_large: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 2000; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Pop all 2000 elements */
  for (int i = 1999; i >= 0; i--) {
    int_vec_pop_result pr = int_vec_pop_back(v);
    ASSERT(pr.found);
    ASSERT_INT_EQ(pr.value, i);
    int_vec_unref(v);
    v = pr.root;
  }

  ASSERT_INT_EQ(int_vec_count(v), 0);
  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: concat two non-empty vectors, verify all elements in order */
int test_concat_basic(void) {
  printf("  test_concat_basic: ");
  tracker_reset();

  int_vec_root* va = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(va, i);
    int_vec_unref(va);
    va = next;
  }

  int_vec_root* vb = int_vec_empty();
  for (int i = 10; i < 20; i++) {
    int_vec_root* next = int_vec_push_back(vb, i);
    int_vec_unref(vb);
    vb = next;
  }

  int_vec_root* vc = int_vec_concat(va, vb);
  ASSERT_INT_EQ(int_vec_count(vc), 20);
  for (int i = 0; i < 20; i++) {
    int_vec_get_result r = int_vec_get(vc, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(va);
  int_vec_unref(vb);
  int_vec_unref(vc);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: concat with empty left or right vector */
int test_concat_empty(void) {
  printf("  test_concat_empty: ");
  tracker_reset();

  int_vec_root* empty = int_vec_empty();
  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* concat(empty, v) */
  int_vec_root* r1 = int_vec_concat(empty, v);
  ASSERT_INT_EQ(int_vec_count(r1), 5);
  for (int i = 0; i < 5; i++) {
    int_vec_get_result r = int_vec_get(r1, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  /* concat(v, empty) */
  int_vec_root* r2 = int_vec_concat(v, empty);
  ASSERT_INT_EQ(int_vec_count(r2), 5);
  for (int i = 0; i < 5; i++) {
    int_vec_get_result r = int_vec_get(r2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(empty);
  int_vec_unref(v);
  int_vec_unref(r1);
  int_vec_unref(r2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: concat two large vectors (150+ elements each), verify all elements */
int test_concat_large(void) {
  printf("  test_concat_large: ");
  tracker_reset();

  int_vec_root* va = int_vec_empty();
  for (int i = 0; i < 150; i++) {
    int_vec_root* next = int_vec_push_back(va, i);
    int_vec_unref(va);
    va = next;
  }

  int_vec_root* vb = int_vec_empty();
  for (int i = 150; i < 300; i++) {
    int_vec_root* next = int_vec_push_back(vb, i);
    int_vec_unref(vb);
    vb = next;
  }

  int_vec_root* vc = int_vec_concat(va, vb);
  ASSERT_INT_EQ(int_vec_count(vc), 300);
  for (int i = 0; i < 300; i++) {
    int_vec_get_result r = int_vec_get(vc, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(va);
  int_vec_unref(vb);
  int_vec_unref(vc);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: verify original vectors unchanged after concat */
int test_concat_persistence(void) {
  printf("  test_concat_persistence: ");
  tracker_reset();

  int_vec_root* va = int_vec_empty();
  for (int i = 0; i < 50; i++) {
    int_vec_root* next = int_vec_push_back(va, i);
    int_vec_unref(va);
    va = next;
  }

  int_vec_root* vb = int_vec_empty();
  for (int i = 100; i < 150; i++) {
    int_vec_root* next = int_vec_push_back(vb, i);
    int_vec_unref(vb);
    vb = next;
  }

  int_vec_root* vc = int_vec_concat(va, vb);
  ASSERT_INT_EQ(int_vec_count(vc), 100);

  /* Verify originals are unchanged */
  ASSERT_INT_EQ(int_vec_count(va), 50);
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(va, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  ASSERT_INT_EQ(int_vec_count(vb), 50);
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(vb, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, 100 + i);
  }

  /* Verify concatenated result */
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(vc, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(vc, (uint32_t)(50 + i));
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, 100 + i);
  }

  int_vec_unref(va);
  int_vec_unref(vb);
  int_vec_unref(vc);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set on concatenated vector works correctly */
int test_concat_set(void) {
  printf("  test_concat_set: ");
  tracker_reset();

  int_vec_root* va = int_vec_empty();
  for (int i = 0; i < 50; i++) {
    int_vec_root* next = int_vec_push_back(va, i);
    int_vec_unref(va);
    va = next;
  }

  int_vec_root* vb = int_vec_empty();
  for (int i = 50; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(vb, i);
    int_vec_unref(vb);
    vb = next;
  }

  int_vec_root* vc = int_vec_concat(va, vb);

  /* Set in left subtree */
  int_vec_root* vc2 = int_vec_set(vc, 10, 999);
  ASSERT(vc2 != NULL);
  int_vec_get_result r = int_vec_get(vc2, 10);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 999);

  /* Set in right subtree */
  int_vec_root* vc3 = int_vec_set(vc, 75, 888);
  ASSERT(vc3 != NULL);
  r = int_vec_get(vc3, 75);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 888);

  /* Original unchanged */
  r = int_vec_get(vc, 10);
  ASSERT_INT_EQ(r.value, 10);
  r = int_vec_get(vc, 75);
  ASSERT_INT_EQ(r.value, 75);

  int_vec_unref(va);
  int_vec_unref(vb);
  int_vec_unref(vc);
  int_vec_unref(vc2);
  int_vec_unref(vc3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice a vector at various boundaries, verify element values and count */
int test_slice(void) {
  printf("  test_slice: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Slice [10, 20) */
  int_vec_root* s1 = int_vec_slice(v, 10, 20);
  ASSERT(s1 != NULL);
  ASSERT_INT_EQ(int_vec_count(s1), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(s1, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, 10 + i);
  }

  /* Slice [0, 50) */
  int_vec_root* s2 = int_vec_slice(v, 0, 50);
  ASSERT(s2 != NULL);
  ASSERT_INT_EQ(int_vec_count(s2), 50);
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(s2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  /* Slice [50, 100) */
  int_vec_root* s3 = int_vec_slice(v, 50, 100);
  ASSERT(s3 != NULL);
  ASSERT_INT_EQ(int_vec_count(s3), 50);
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(s3, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, 50 + i);
  }

  int_vec_unref(v);
  int_vec_unref(s1);
  int_vec_unref(s2);
  int_vec_unref(s3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice with invalid ranges returns NULL */
int test_slice_invalid(void) {
  printf("  test_slice_invalid: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  ASSERT(int_vec_slice(v, 5, 5) == NULL);   /* start == end */
  ASSERT(int_vec_slice(v, 7, 3) == NULL);   /* start > end */
  ASSERT(int_vec_slice(v, 0, 11) == NULL);  /* end > count */
  ASSERT(int_vec_slice(v, 10, 11) == NULL); /* start == count */

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice full range returns ref'd copy */
int test_slice_full_range(void) {
  printf("  test_slice_full_range: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_root* s = int_vec_slice(v, 0, 10);
  ASSERT(s != NULL);
  ASSERT(s == v); /* same pointer since it's just a ref */
  ASSERT_INT_EQ(int_vec_count(s), 10);

  int_vec_unref(v);
  int_vec_unref(s);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: iterate over a vector, verify all elements visited in order and count matches */
int test_iter(void) {
  printf("  test_iter: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 200; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_iter it = int_vec_iter_init(v);
  int expected = 0;
  while (1) {
    int_vec_iter_result r = int_vec_iter_next(&it);
    if (r.done) break;
    ASSERT_INT_EQ(r.value, expected);
    expected++;
  }
  ASSERT_INT_EQ(expected, 200);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: iterate over an empty vector, first next returns .done == true */
int test_iter_empty(void) {
  printf("  test_iter_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_iter it = int_vec_iter_init(v);
  int_vec_iter_result r = int_vec_iter_next(&it);
  ASSERT(r.done);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push_front 5 elements, verify order is reversed compared to push_back */
int test_push_front(void) {
  printf("  test_push_front: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_front(v, i);
    int_vec_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(int_vec_count(v), 5);
  /* push_front 0,1,2,3,4 -> order is 4,3,2,1,0 */
  for (int i = 0; i < 5; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, 4 - i);
  }

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop_front returns first element, remaining vector is correct */
int test_pop_front(void) {
  printf("  test_pop_front: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i * 10);
    int_vec_unref(v);
    v = next;
  }

  int_vec_pop_result pr = int_vec_pop_front(v);
  ASSERT(pr.found);
  ASSERT_INT_EQ(pr.value, 0);
  ASSERT_INT_EQ(int_vec_count(pr.root), 9);

  /* Remaining elements should be 10,20,...,90 */
  for (int i = 0; i < 9; i++) {
    int_vec_get_result r = int_vec_get(pr.root, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, (i + 1) * 10);
  }

  int_vec_unref(v);
  int_vec_unref(pr.root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push_front and pop_front on empty vector */
int test_push_pop_front_empty(void) {
  printf("  test_push_pop_front_empty: ");
  tracker_reset();

  /* pop_front on empty */
  int_vec_root* v = int_vec_empty();
  int_vec_pop_result pr = int_vec_pop_front(v);
  ASSERT(!pr.found);
  ASSERT(pr.root == NULL);

  /* push_front on empty then pop_front */
  int_vec_root* v2 = int_vec_push_front(v, 42);
  ASSERT_INT_EQ(int_vec_count(v2), 1);
  int_vec_get_result gr = int_vec_get(v2, 0);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 42);

  int_vec_pop_result pr2 = int_vec_pop_front(v2);
  ASSERT(pr2.found);
  ASSERT_INT_EQ(pr2.value, 42);
  ASSERT_INT_EQ(int_vec_count(pr2.root), 0);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_unref(pr2.root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: persistence — original vector unchanged after push_front/pop_front */
int test_push_pop_front_persistence(void) {
  printf("  test_push_pop_front_persistence: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* push_front should not modify original */
  int_vec_root* v2 = int_vec_push_front(v, 99);
  ASSERT_INT_EQ(int_vec_count(v), 10);
  ASSERT_INT_EQ(int_vec_count(v2), 11);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }
  int_vec_get_result r = int_vec_get(v2, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 99);

  /* pop_front should not modify original */
  int_vec_pop_result pr = int_vec_pop_front(v);
  ASSERT(pr.found);
  ASSERT_INT_EQ(pr.value, 0);
  ASSERT_INT_EQ(int_vec_count(v), 10);
  ASSERT_INT_EQ(int_vec_count(pr.root), 9);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_unref(pr.root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: insert at beginning, middle, and end of a vector */
int test_insert(void) {
  printf("  test_insert: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i * 10); /* 0,10,20,30,40 */
    int_vec_unref(v);
    v = next;
  }

  /* Insert at beginning */
  int_vec_root* v2 = int_vec_insert(v, 0, 99);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ(int_vec_count(v2), 6);
  int_vec_get_result r = int_vec_get(v2, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 99);
  r = int_vec_get(v2, 1);
  ASSERT_INT_EQ(r.value, 0);

  /* Insert at middle (index 3) */
  int_vec_root* v3 = int_vec_insert(v, 3, 55);
  ASSERT(v3 != NULL);
  ASSERT_INT_EQ(int_vec_count(v3), 6);
  r = int_vec_get(v3, 3);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 55);
  r = int_vec_get(v3, 4);
  ASSERT_INT_EQ(r.value, 30);

  /* Insert at end (index == count) */
  int_vec_root* v4 = int_vec_insert(v, 5, 77);
  ASSERT(v4 != NULL);
  ASSERT_INT_EQ(int_vec_count(v4), 6);
  r = int_vec_get(v4, 5);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 77);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_unref(v3);
  int_vec_unref(v4);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: remove at beginning, middle, and end */
int test_remove(void) {
  printf("  test_remove: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i * 10); /* 0,10,20,30,40 */
    int_vec_unref(v);
    v = next;
  }

  /* Remove at beginning */
  int_vec_pop_result pr = int_vec_remove(v, 0);
  ASSERT(pr.found);
  ASSERT_INT_EQ(pr.value, 0);
  ASSERT_INT_EQ(int_vec_count(pr.root), 4);
  int_vec_get_result r = int_vec_get(pr.root, 0);
  ASSERT_INT_EQ(r.value, 10);
  int_vec_unref(pr.root);

  /* Remove at middle (index 2 -> value 20) */
  pr = int_vec_remove(v, 2);
  ASSERT(pr.found);
  ASSERT_INT_EQ(pr.value, 20);
  ASSERT_INT_EQ(int_vec_count(pr.root), 4);
  r = int_vec_get(pr.root, 2);
  ASSERT_INT_EQ(r.value, 30);
  int_vec_unref(pr.root);

  /* Remove at end (index 4 -> value 40) */
  pr = int_vec_remove(v, 4);
  ASSERT(pr.found);
  ASSERT_INT_EQ(pr.value, 40);
  ASSERT_INT_EQ(int_vec_count(pr.root), 4);
  r = int_vec_get(pr.root, 3);
  ASSERT_INT_EQ(r.value, 30);
  int_vec_unref(pr.root);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: insert/remove out-of-bounds */
int test_insert_remove_oob(void) {
  printf("  test_insert_remove_oob: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Insert at index > count returns NULL */
  ASSERT(int_vec_insert(v, 6, 42) == NULL);
  ASSERT(int_vec_insert(v, 100, 42) == NULL);

  /* Remove at index >= count returns .found == false */
  int_vec_pop_result pr = int_vec_remove(v, 5);
  ASSERT(!pr.found);
  ASSERT(pr.root == NULL);
  pr = int_vec_remove(v, 100);
  ASSERT(!pr.found);
  ASSERT(pr.root == NULL);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: persistence — original unchanged after insert/remove */
int test_insert_remove_persistence(void) {
  printf("  test_insert_remove_persistence: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Insert should not modify original */
  int_vec_root* v2 = int_vec_insert(v, 5, 99);
  ASSERT_INT_EQ(int_vec_count(v), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  /* Remove should not modify original */
  int_vec_pop_result pr = int_vec_remove(v, 5);
  ASSERT(pr.found);
  ASSERT_INT_EQ(int_vec_count(v), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_unref(pr.root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: insert then remove at same index yields original content */
int test_insert_then_remove(void) {
  printf("  test_insert_then_remove: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Insert 99 at index 5, then remove at index 5 */
  int_vec_root* v2 = int_vec_insert(v, 5, 99);
  int_vec_pop_result pr = int_vec_remove(v2, 5);
  ASSERT(pr.found);
  ASSERT_INT_EQ(pr.value, 99);
  ASSERT_INT_EQ(int_vec_count(pr.root), 10);

  /* Result should match original content */
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(pr.root, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_unref(pr.root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Map helper --- */
static int double_val(int x) { return x * 2; }

/* --- Filter helper --- */
static bool is_even(int x) { return x % 2 == 0; }

/* Test: map doubling values, verify all elements doubled */
int test_map(void) {
  printf("  test_map: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 20; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_root* v2 = int_vec_map(v, double_val);
  ASSERT_INT_EQ(int_vec_count(v2), 20);
  for (int i = 0; i < 20; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i * 2);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: filter even numbers from a vector */
int test_filter(void) {
  printf("  test_filter: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 20; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_root* v2 = int_vec_filter(v, is_even);
  ASSERT_INT_EQ(int_vec_count(v2), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i * 2);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: map/filter on empty vector returns empty */
int test_map_filter_empty(void) {
  printf("  test_map_filter_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();

  int_vec_root* mapped = int_vec_map(v, double_val);
  ASSERT_INT_EQ(int_vec_count(mapped), 0);

  int_vec_root* filtered = int_vec_filter(v, is_even);
  ASSERT_INT_EQ(int_vec_count(filtered), 0);

  int_vec_unref(v);
  int_vec_unref(mapped);
  int_vec_unref(filtered);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: persistence — original unchanged after map/filter */
int test_map_filter_persistence(void) {
  printf("  test_map_filter_persistence: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_root* mapped = int_vec_map(v, double_val);
  int_vec_root* filtered = int_vec_filter(v, is_even);

  /* Original unchanged */
  ASSERT_INT_EQ(int_vec_count(v), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(mapped);
  int_vec_unref(filtered);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Reduce helper --- */
static int sum_fn(int acc, int elem) { return acc + elem; }

/* --- Map_into helper --- */
static double int_to_dbl(int x) { return (double)x * 1.5; }

/* Test: reduce to compute sum of integer vector */
int test_reduce_sum(void) {
  printf("  test_reduce_sum: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 1; i <= 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int result = int_vec_reduce(v, sum_fn, 0);
  ASSERT_INT_EQ(result, 55); /* 1+2+...+10 = 55 */

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: reduce on empty returns initial value */
int test_reduce_empty(void) {
  printf("  test_reduce_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int result = int_vec_reduce(v, sum_fn, 42);
  ASSERT_INT_EQ(result, 42);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: map_into from int_vec to dbl_vec */
int test_map_into(void) {
  printf("  test_map_into: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  dbl_vec_root* dv = int_vec_map_into(v, int_to_dbl);
  ASSERT_INT_EQ(dbl_vec_count(dv), 10);
  for (int i = 0; i < 10; i++) {
    dbl_vec_get_result r = dbl_vec_get(dv, (uint32_t)i);
    ASSERT(r.found);
    double expected = (double)i * 1.5;
    ASSERT(r.value == expected);
  }

  int_vec_unref(v);
  dbl_vec_unref(dv);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: map_into on empty returns empty destination vec */
int test_map_into_empty(void) {
  printf("  test_map_into_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  dbl_vec_root* dv = int_vec_map_into(v, int_to_dbl);
  ASSERT_INT_EQ(dbl_vec_count(dv), 0);

  int_vec_unref(v);
  dbl_vec_unref(dv);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Find/index_of/equals helpers --- */
static bool int_eq(int a, int b) { return a == b; }

/* Test: find first even number in vector */
int test_find(void) {
  printf("  test_find: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 1; i <= 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  /* Find first even: should be 2 */
  int_vec_get_result r = int_vec_find(v, is_even);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 2);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: find in empty / no match returns .found == false */
int test_find_empty_no_match(void) {
  printf("  test_find_empty_no_match: ");
  tracker_reset();

  /* Empty */
  int_vec_root* v = int_vec_empty();
  int_vec_get_result r = int_vec_find(v, is_even);
  ASSERT(!r.found);
  int_vec_unref(v);

  /* No match: all odd */
  v = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(v, i * 2 + 1); /* 1,3,5,7,9 */
    int_vec_unref(v);
    v = next;
  }
  r = int_vec_find(v, is_even);
  ASSERT(!r.found);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: index_of for existing and non-existing elements */
int test_index_of(void) {
  printf("  test_index_of: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i * 10); /* 0,10,20,...,90 */
    int_vec_unref(v);
    v = next;
  }

  /* Find existing */
  ASSERT_INT_EQ(int_vec_index_of(v, 0, int_eq), 0);
  ASSERT_INT_EQ(int_vec_index_of(v, 50, int_eq), 5);
  ASSERT_INT_EQ(int_vec_index_of(v, 90, int_eq), 9);

  /* Non-existing */
  ASSERT_INT_EQ(int_vec_index_of(v, 42, int_eq), -1);
  ASSERT_INT_EQ(int_vec_index_of(v, 100, int_eq), -1);

  /* Empty */
  int_vec_root* empty = int_vec_empty();
  ASSERT_INT_EQ(int_vec_index_of(empty, 0, int_eq), -1);
  int_vec_unref(empty);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: equals on identical vectors, different vectors, different lengths, empty vectors */
int test_equals(void) {
  printf("  test_equals: ");
  tracker_reset();

  int_vec_root* v1 = int_vec_empty();
  int_vec_root* v2 = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* n1 = int_vec_push_back(v1, i);
    int_vec_unref(v1);
    v1 = n1;
    int_vec_root* n2 = int_vec_push_back(v2, i);
    int_vec_unref(v2);
    v2 = n2;
  }

  /* Identical content */
  ASSERT(int_vec_equals(v1, v2, int_eq));

  /* Different content (modify v2) */
  int_vec_root* v3 = int_vec_set(v2, 5, 999);
  ASSERT(!int_vec_equals(v1, v3, int_eq));
  int_vec_unref(v3);

  /* Different lengths */
  int_vec_root* v4 = int_vec_push_back(v1, 42);
  ASSERT(!int_vec_equals(v1, v4, int_eq));
  int_vec_unref(v4);

  /* Two empty vectors */
  int_vec_root* e1 = int_vec_empty();
  int_vec_root* e2 = int_vec_empty();
  ASSERT(int_vec_equals(e1, e2, int_eq));
  int_vec_unref(e1);
  int_vec_unref(e2);

  int_vec_unref(v1);
  int_vec_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: round-trip — push N elements, to_array, verify array, from_array, verify vector */
int test_to_from_array_roundtrip(void) {
  printf("  test_to_from_array_roundtrip: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  uint32_t arr_count;
  int* arr = int_vec_to_array(v, &arr_count);
  ASSERT(arr != NULL);
  ASSERT_INT_EQ((int)arr_count, 100);
  for (int i = 0; i < 100; i++) {
    ASSERT_INT_EQ(arr[i], i);
  }

  int_vec_root* v2 = int_vec_from_array(arr, arr_count);
  ASSERT_INT_EQ((int)int_vec_count(v2), 100);
  for (int i = 0; i < 100; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  tracked_free_with_ctx(NULL, arr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: to_array on empty */
int test_to_array_empty(void) {
  printf("  test_to_array_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  uint32_t arr_count;
  int* arr = int_vec_to_array(v, &arr_count);
  ASSERT(arr == NULL);
  ASSERT_INT_EQ((int)arr_count, 0);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: from_array on empty (count 0) */
int test_from_array_empty(void) {
  printf("  test_from_array_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_from_array(NULL, 0);
  ASSERT(v != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v), 0);

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: from_array with 1000+ elements, verify all via get */
int test_from_array_large(void) {
  printf("  test_from_array_large: ");
  tracker_reset();

  int arr[1500];
  for (int i = 0; i < 1500; i++) {
    arr[i] = i * 3;
  }

  int_vec_root* v = int_vec_from_array(arr, 1500);
  ASSERT_INT_EQ((int)int_vec_count(v), 1500);
  for (int i = 0; i < 1500; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i * 3);
  }

  int_vec_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Sort helpers --- */
static int cmp_asc(int a, int b) { return a - b; }
static int cmp_desc(int a, int b) { return b - a; }

/* Test: sort integers ascending and descending */
int test_sort(void) {
  printf("  test_sort: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int vals[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, vals[i]);
    int_vec_unref(v);
    v = next;
  }

  /* Sort ascending */
  int_vec_root* asc = int_vec_sort(v, cmp_asc);
  ASSERT_INT_EQ((int)int_vec_count(asc), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(asc, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  /* Sort descending */
  int_vec_root* desc = int_vec_sort(v, cmp_desc);
  ASSERT_INT_EQ((int)int_vec_count(desc), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(desc, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, 9 - i);
  }

  int_vec_unref(v);
  int_vec_unref(asc);
  int_vec_unref(desc);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: sort already-sorted vector */
int test_sort_already_sorted(void) {
  printf("  test_sort_already_sorted: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 20; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_root* sorted = int_vec_sort(v, cmp_asc);
  ASSERT_INT_EQ((int)int_vec_count(sorted), 20);
  for (int i = 0; i < 20; i++) {
    int_vec_get_result r = int_vec_get(sorted, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(sorted);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: sort empty and single-element vectors */
int test_sort_empty_single(void) {
  printf("  test_sort_empty_single: ");
  tracker_reset();

  /* Empty */
  int_vec_root* empty = int_vec_empty();
  int_vec_root* sorted_e = int_vec_sort(empty, cmp_asc);
  ASSERT_INT_EQ((int)int_vec_count(sorted_e), 0);
  int_vec_unref(empty);
  int_vec_unref(sorted_e);

  /* Single element */
  int_vec_root* single = int_vec_empty();
  int_vec_root* s1 = int_vec_push_back(single, 42);
  int_vec_unref(single);
  int_vec_root* sorted_s = int_vec_sort(s1, cmp_asc);
  ASSERT_INT_EQ((int)int_vec_count(sorted_s), 1);
  int_vec_get_result r = int_vec_get(sorted_s, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.value, 42);
  int_vec_unref(s1);
  int_vec_unref(sorted_s);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: persistence — original unchanged after sort */
int test_sort_persistence(void) {
  printf("  test_sort_persistence: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int vals[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, vals[i]);
    int_vec_unref(v);
    v = next;
  }

  int_vec_root* sorted = int_vec_sort(v, cmp_asc);

  /* Original unchanged */
  ASSERT_INT_EQ((int)int_vec_count(v), 10);
  for (int i = 0; i < 10; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, vals[i]);
  }

  int_vec_unref(v);
  int_vec_unref(sorted);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === Transient tests === */

/* Test: create transient from empty vec, persist immediately, verify count 0 */
int test_transient_empty_roundtrip(void) {
  printf("  test_transient_empty_roundtrip: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ(int_vec_count(v2), 0);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: create transient from non-empty vec (100 elements), persist immediately, verify all elements intact */
int test_transient_nonempty_roundtrip(void) {
  printf("  test_transient_nonempty_roundtrip: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ(int_vec_count(v2), 100);
  for (int i = 0; i < 100; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: source persistent vec unchanged after transient creation */
int test_transient_source_unchanged(void) {
  printf("  test_transient_source_unchanged: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 50; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  /* Source must be unchanged */
  ASSERT_INT_EQ(int_vec_count(v), 50);
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_root* v2 = int_vec_persistent_fn(t);
  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mutating an invalidated (already persisted) transient returns NULL */
int test_transient_double_persist(void) {
  printf("  test_transient_double_persist: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);

  /* Second persist should return NULL (already invalidated) */
  int_vec_root* v3 = int_vec_persistent_fn(t);
  ASSERT(v3 == NULL);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === Transient push_back tests === */

/* Test: push 10000 elements via transient, persist, verify all elements via get */
int test_transient_push_back_10k(void) {
  printf("  test_transient_push_back_10k: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  for (int i = 0; i < 10000; i++) {
    int_vec_transient* r = int_vec_transient_push_back(t, i);
    ASSERT(r == t);
  }

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 10000);
  for (int i = 0; i < 10000; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push elements that trigger multiple tail flushes (>32, >1024), verify after persist */
int test_transient_push_back_tail_flushes(void) {
  printf("  test_transient_push_back_tail_flushes: ");
  tracker_reset();

  /* Test with 1100 elements: triggers 34 tail flushes (1100/32) and exercises multi-level tree */
  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  for (int i = 0; i < 1100; i++) {
    int_vec_transient* r = int_vec_transient_push_back(t, i * 3);
    ASSERT(r == t);
  }

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 1100);
  for (int i = 0; i < 1100; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i * 3);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transient push_back on invalidated transient returns NULL */
int test_transient_push_back_after_persist(void) {
  printf("  test_transient_push_back_after_persist: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  int_vec_transient* r = int_vec_transient_push_back(t, 42);
  ASSERT(r == t);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);

  /* push_back on invalidated transient should return NULL */
  r = int_vec_transient_push_back(t, 99);
  ASSERT(r == NULL);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transient push_back from non-empty source, verify source unchanged */
int test_transient_push_back_source_unchanged(void) {
  printf("  test_transient_push_back_source_unchanged: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  for (int i = 100; i < 200; i++) {
    int_vec_transient_push_back(t, i);
  }

  /* Source must be unchanged */
  ASSERT_INT_EQ((int)int_vec_count(v), 100);
  for (int i = 0; i < 100; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT_INT_EQ((int)int_vec_count(v2), 200);
  for (int i = 0; i < 200; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === Transient set tests === */

/* Test: create transient from persistent vec with 1000 elements, set index 0, 500, 999 — verify after persist */
int test_transient_set_basic(void) {
  printf("  test_transient_set_basic: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 1000; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  /* Set index 0 */
  int_vec_transient* r = int_vec_transient_set(t, 0, 9999);
  ASSERT(r == t);

  /* Set index 500 */
  r = int_vec_transient_set(t, 500, 8888);
  ASSERT(r == t);

  /* Set index 999 */
  r = int_vec_transient_set(t, 999, 7777);
  ASSERT(r == t);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 1000);

  /* Verify modified indices */
  int_vec_get_result gr = int_vec_get(v2, 0);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 9999);

  gr = int_vec_get(v2, 500);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 8888);

  gr = int_vec_get(v2, 999);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 7777);

  /* Verify unmodified indices */
  gr = int_vec_get(v2, 1);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 1);

  gr = int_vec_get(v2, 499);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 499);

  gr = int_vec_get(v2, 998);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 998);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set on transient where nodes are shared with source — verify source persistent vec unchanged */
int test_transient_set_source_unchanged(void) {
  printf("  test_transient_set_source_unchanged: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 1000; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);

  /* Set various indices in transient */
  int_vec_transient_set(t, 0, -1);
  int_vec_transient_set(t, 100, -2);
  int_vec_transient_set(t, 500, -3);
  int_vec_transient_set(t, 999, -4);

  /* Source persistent vec must be unchanged */
  ASSERT_INT_EQ((int)int_vec_count(v), 1000);
  for (int i = 0; i < 1000; i++) {
    int_vec_get_result r = int_vec_get(v, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_root* v2 = int_vec_persistent_fn(t);
  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: second set to same region uses in-place mutation (no extra node copies) */
int test_transient_set_inplace_reuse(void) {
  printf("  test_transient_set_inplace_reuse: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);

  /* First set at index 5 — may copy nodes along path */
  int_vec_transient_set(t, 5, 555);

  /* Second set at index 5 — path nodes should now be owned (rc==1), no extra copies */
  int_vec_transient_set(t, 5, 666);

  /* Also set at index 6 — same leaf, should be in-place */
  int_vec_transient_set(t, 6, 777);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);

  int_vec_get_result gr = int_vec_get(v2, 5);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 666);

  gr = int_vec_get(v2, 6);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 777);

  /* Other elements unchanged */
  gr = int_vec_get(v2, 0);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 0);

  gr = int_vec_get(v2, 99);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 99);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transient set out of bounds returns NULL (release) / asserts (debug) */
int test_transient_set_oob(void) {
  printf("  test_transient_set_oob: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);

  /* Set at index == count should fail */
  /* Note: This test is compiled with NDEBUG not defined, but the function
   * returns NULL in release builds. We test the after-persist case instead. */

  /* Set on invalidated transient */
  int_vec_root* v2 = int_vec_persistent_fn(t);
  int_vec_transient* r = int_vec_transient_set(t, 0, 42);
  ASSERT(r == NULL);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === Transient pop_back tests === */

/* Test: push 100 elements via transient, pop all 100, verify each popped value correct */
int test_transient_pop_back_all(void) {
  printf("  test_transient_pop_back_all: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  for (int i = 0; i < 100; i++) {
    int_vec_transient_push_back(t, i);
  }

  /* Pop all 100 elements, verify values in reverse order */
  for (int i = 99; i >= 0; i--) {
    int_vec_transient_pop_result pr = int_vec_transient_pop_back(t);
    ASSERT(pr.found);
    ASSERT(pr.transient == t);
    ASSERT_INT_EQ(pr.value, i);
  }

  /* Transient should now be empty */
  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ(int_vec_count(v2), 0);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop on empty transient returns .found = false */
int test_transient_pop_back_empty(void) {
  printf("  test_transient_pop_back_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  int_vec_transient_pop_result pr = int_vec_transient_pop_back(t);
  ASSERT(!pr.found);
  ASSERT(pr.transient == NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push then pop across tail boundaries (push 64, pop 40, verify remaining 24) */
int test_transient_pop_back_tail_boundary(void) {
  printf("  test_transient_pop_back_tail_boundary: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  /* Push 64 elements (2 full tails) */
  for (int i = 0; i < 64; i++) {
    int_vec_transient_push_back(t, i);
  }

  /* Pop 40 elements — crosses tail boundary */
  for (int i = 63; i >= 24; i--) {
    int_vec_transient_pop_result pr = int_vec_transient_pop_back(t);
    ASSERT(pr.found);
    ASSERT(pr.transient == t);
    ASSERT_INT_EQ(pr.value, i);
  }

  /* Persist and verify remaining 24 elements */
  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ(int_vec_count(v2), 24);
  for (int i = 0; i < 24; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop on invalidated (already persisted) transient returns NULL */
int test_transient_pop_back_after_persist(void) {
  printf("  test_transient_pop_back_after_persist: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);
  int_vec_transient_push_back(t, 42);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);

  /* Pop on invalidated transient should return .found = false */
  int_vec_transient_pop_result pr = int_vec_transient_pop_back(t);
  ASSERT(!pr.found);
  ASSERT(pr.transient == NULL);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === Transient push_front / pop_front tests === */

/* Test: push_front 50 elements, verify order after persist */
int test_transient_push_front_50(void) {
  printf("  test_transient_push_front_50: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  for (int i = 0; i < 50; i++) {
    int_vec_transient* r = int_vec_transient_push_front(t, i);
    ASSERT(r == t);
  }

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 50);

  /* push_front 0,1,2,...,49 -> order is 49,48,...,1,0 */
  for (int i = 0; i < 50; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, 49 - i);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop_front returns first element, remaining elements correct after persist */
int test_transient_pop_front_basic(void) {
  printf("  test_transient_pop_front_basic: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  for (int i = 0; i < 20; i++) {
    int_vec_transient_push_back(t, i * 10);
  }

  /* Pop front — should get 0 */
  int_vec_transient_pop_result pr = int_vec_transient_pop_front(t);
  ASSERT(pr.found);
  ASSERT(pr.transient == t);
  ASSERT_INT_EQ(pr.value, 0);

  /* Pop front again — should get 10 */
  pr = int_vec_transient_pop_front(t);
  ASSERT(pr.found);
  ASSERT(pr.transient == t);
  ASSERT_INT_EQ(pr.value, 10);

  /* Persist and verify remaining 18 elements: 20,30,...,190 */
  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 18);
  for (int i = 0; i < 18; i++) {
    int_vec_get_result r = int_vec_get(v2, (uint32_t)i);
    ASSERT(r.found);
    ASSERT_INT_EQ(r.value, (i + 2) * 10);
  }

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push_front and pop_front on empty transient */
int test_transient_push_pop_front_empty(void) {
  printf("  test_transient_push_pop_front_empty: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);

  /* pop_front on empty transient */
  int_vec_transient_pop_result pr = int_vec_transient_pop_front(t);
  ASSERT(!pr.found);
  ASSERT(pr.transient == NULL);

  /* push_front on empty then verify */
  int_vec_transient* r = int_vec_transient_push_front(t, 42);
  ASSERT(r == t);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ(int_vec_count(v2), 1);
  int_vec_get_result gr = int_vec_get(v2, 0);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 42);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push_front/pop_front after persist returns NULL */
int test_transient_push_pop_front_after_persist(void) {
  printf("  test_transient_push_pop_front_after_persist: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(v);
  int_vec_transient_push_back(t, 1);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);

  /* push_front on invalidated transient */
  int_vec_transient* r = int_vec_transient_push_front(t, 99);
  ASSERT(r == NULL);

  /* pop_front on invalidated transient */
  int_vec_transient_pop_result pr = int_vec_transient_pop_front(t);
  ASSERT(!pr.found);
  ASSERT(pr.transient == NULL);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  int pass = 0, fail = 0;

  printf("rrb_vec tests:\n");

  test_empty_vec()               ? pass++ : fail++;
  test_multiple_instantiations() ? pass++ : fail++;
  test_ref_unref()               ? pass++ : fail++;
  test_push_and_get_small()      ? pass++ : fail++;
  test_push_and_get_large()      ? pass++ : fail++;
  test_persistence()             ? pass++ : fail++;
  test_shared_base()             ? pass++ : fail++;
  test_get_out_of_bounds()       ? pass++ : fail++;
  test_set()                     ? pass++ : fail++;
  test_set_out_of_bounds()       ? pass++ : fail++;
  test_pop_back()                ? pass++ : fail++;
  test_pop_empty()               ? pass++ : fail++;
  test_pop_persistence()         ? pass++ : fail++;
  test_pop_large()               ? pass++ : fail++;
  test_concat_basic()            ? pass++ : fail++;
  test_concat_empty()            ? pass++ : fail++;
  test_concat_large()            ? pass++ : fail++;
  test_concat_persistence()      ? pass++ : fail++;
  test_concat_set()              ? pass++ : fail++;
  test_slice()                   ? pass++ : fail++;
  test_slice_invalid()           ? pass++ : fail++;
  test_slice_full_range()        ? pass++ : fail++;
  test_iter()                    ? pass++ : fail++;
  test_iter_empty()              ? pass++ : fail++;
  test_push_front()              ? pass++ : fail++;
  test_pop_front()               ? pass++ : fail++;
  test_push_pop_front_empty()    ? pass++ : fail++;
  test_push_pop_front_persistence() ? pass++ : fail++;
  test_insert()                     ? pass++ : fail++;
  test_remove()                     ? pass++ : fail++;
  test_insert_remove_oob()          ? pass++ : fail++;
  test_insert_remove_persistence()  ? pass++ : fail++;
  test_insert_then_remove()         ? pass++ : fail++;
  test_map()                        ? pass++ : fail++;
  test_filter()                     ? pass++ : fail++;
  test_map_filter_empty()           ? pass++ : fail++;
  test_map_filter_persistence()     ? pass++ : fail++;
  test_reduce_sum()                 ? pass++ : fail++;
  test_reduce_empty()               ? pass++ : fail++;
  test_map_into()                   ? pass++ : fail++;
  test_map_into_empty()             ? pass++ : fail++;
  test_find()                       ? pass++ : fail++;
  test_find_empty_no_match()        ? pass++ : fail++;
  test_index_of()                   ? pass++ : fail++;
  test_equals()                     ? pass++ : fail++;
  test_to_from_array_roundtrip()    ? pass++ : fail++;
  test_to_array_empty()             ? pass++ : fail++;
  test_from_array_empty()           ? pass++ : fail++;
  test_from_array_large()           ? pass++ : fail++;
  test_sort()                        ? pass++ : fail++;
  test_sort_already_sorted()         ? pass++ : fail++;
  test_sort_empty_single()           ? pass++ : fail++;
  test_sort_persistence()            ? pass++ : fail++;

  /* Transient tests */
  test_transient_empty_roundtrip()     ? pass++ : fail++;
  test_transient_nonempty_roundtrip()  ? pass++ : fail++;
  test_transient_source_unchanged()    ? pass++ : fail++;
  test_transient_double_persist()      ? pass++ : fail++;

  /* Transient push_back tests */
  test_transient_push_back_10k()           ? pass++ : fail++;
  test_transient_push_back_tail_flushes()  ? pass++ : fail++;
  test_transient_push_back_after_persist() ? pass++ : fail++;
  test_transient_push_back_source_unchanged() ? pass++ : fail++;

  /* Transient set tests */
  test_transient_set_basic()            ? pass++ : fail++;
  test_transient_set_source_unchanged() ? pass++ : fail++;
  test_transient_set_inplace_reuse()    ? pass++ : fail++;
  test_transient_set_oob()              ? pass++ : fail++;

  /* Transient pop_back tests */
  test_transient_pop_back_all()            ? pass++ : fail++;
  test_transient_pop_back_empty()          ? pass++ : fail++;
  test_transient_pop_back_tail_boundary()  ? pass++ : fail++;
  test_transient_pop_back_after_persist()  ? pass++ : fail++;

  /* Transient push_front / pop_front tests */
  test_transient_push_front_50()              ? pass++ : fail++;
  test_transient_pop_front_basic()            ? pass++ : fail++;
  test_transient_push_pop_front_empty()       ? pass++ : fail++;
  test_transient_push_pop_front_after_persist() ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
