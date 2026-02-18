#include "../test/test_helpers.h"

#define TRACKED_ALLOC                     \
  {                                       \
      .alloc = tracked_malloc_with_ctx,   \
      .free  = tracked_free_with_ctx,     \
  }

#define RRB_VEC_T         int
#define RRB_VEC_NAME      int_vec
#define RRB_VEC_ALLOCATOR TRACKED_ALLOC
#include "rrb_vec.h"

/* ===== US-001: Transient iterator tests ===== */

int test_transient_iter_empty(void) {
  printf("  test_transient_iter_empty: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  int_vec_transient_iter it = int_vec_transient_iter_init(t);
  int_vec_iter_result ir = int_vec_transient_iter_next(&it);
  ASSERT(ir.done == true);

  int_vec_root* p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_iter_100(void) {
  printf("  test_transient_iter_100: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(r, i);
    int_vec_unref(r);
    r = next;
  }

  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  int_vec_transient_iter it = int_vec_transient_iter_init(t);
  for (int i = 0; i < 100; i++) {
    int_vec_iter_result ir = int_vec_transient_iter_next(&it);
    ASSERT(ir.done == false);
    ASSERT_INT_EQ(ir.value, i);
  }
  int_vec_iter_result ir = int_vec_transient_iter_next(&it);
  ASSERT(ir.done == true);

  int_vec_root* p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_iter_10000(void) {
  printf("  test_transient_iter_10000: ");
  tracker_reset();

  /* Build via transient push_back for efficiency */
  int_vec_root* empty = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(empty);
  for (int i = 0; i < 10000; i++) {
    int_vec_transient_push_back(t, i);
  }

  int_vec_transient_iter it = int_vec_transient_iter_init(t);
  for (int i = 0; i < 10000; i++) {
    int_vec_iter_result ir = int_vec_transient_iter_next(&it);
    ASSERT(ir.done == false);
    ASSERT_INT_EQ(ir.value, i);
  }
  int_vec_iter_result ir = int_vec_transient_iter_next(&it);
  ASSERT(ir.done == true);

  int_vec_root* p = int_vec_persistent_fn(t);
  int_vec_unref(empty);
  int_vec_unref(p);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_iter_then_push(void) {
  printf("  test_transient_iter_then_push: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  for (int i = 0; i < 5; i++) {
    int_vec_root* next = int_vec_push_back(r, i);
    int_vec_unref(r);
    r = next;
  }

  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  /* First iteration: 0..4 */
  int_vec_transient_iter it = int_vec_transient_iter_init(t);
  int count1 = 0;
  while (1) {
    int_vec_iter_result ir = int_vec_transient_iter_next(&it);
    if (ir.done) break;
    ASSERT_INT_EQ(ir.value, count1);
    count1++;
  }
  ASSERT_INT_EQ(count1, 5);

  /* Push more elements */
  for (int i = 5; i < 10; i++) {
    t = int_vec_transient_push_back(t, i);
  }

  /* Second iteration: should see all 10 elements */
  int_vec_transient_iter it2 = int_vec_transient_iter_init(t);
  int count2 = 0;
  while (1) {
    int_vec_iter_result ir = int_vec_transient_iter_next(&it2);
    if (ir.done) break;
    ASSERT_INT_EQ(ir.value, count2);
    count2++;
  }
  ASSERT_INT_EQ(count2, 10);

  int_vec_root* p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-002: Transient for_each tests ===== */

static int foreach_counter;
static int foreach_values[1000];

static void foreach_callback(int val) {
  if (foreach_counter < 1000) {
    foreach_values[foreach_counter] = val;
  }
  foreach_counter++;
}

int test_transient_for_each_empty(void) {
  printf("  test_transient_for_each_empty: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  foreach_counter = 0;
  int_vec_transient_for_each(t, foreach_callback);
  ASSERT_INT_EQ(foreach_counter, 0);

  int_vec_root* p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_for_each_100(void) {
  printf("  test_transient_for_each_100: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(r, i * 3);
    int_vec_unref(r);
    r = next;
  }

  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  foreach_counter = 0;
  int_vec_transient_for_each(t, foreach_callback);
  ASSERT_INT_EQ(foreach_counter, 100);
  for (int i = 0; i < 100; i++) {
    ASSERT_INT_EQ(foreach_values[i], i * 3);
  }

  int_vec_root* p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_for_each_no_invalidate(void) {
  printf("  test_transient_for_each_no_invalidate: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(r, i);
    int_vec_unref(r);
    r = next;
  }

  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  foreach_counter = 0;
  int_vec_transient_for_each(t, foreach_callback);
  ASSERT_INT_EQ(foreach_counter, 10);

  /* push_back should still work */
  t = int_vec_transient_push_back(t, 99);
  ASSERT(t != NULL);

  int_vec_root* p = int_vec_persistent_fn(t);
  ASSERT_INT_EQ(int_vec_count(p), 11);
  int_vec_rc_unref(t);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-003: Transient map tests ===== */

static int double_val(int x) { return x * 2; }

int test_transient_map_1000(void) {
  printf("  test_transient_map_1000: ");
  tracker_reset();

  int_vec_root* empty = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(empty);
  int_vec_unref(empty);
  for (int i = 0; i < 1000; i++) {
    int_vec_transient_push_back(t, i);
  }

  int_vec_transient* mapped = int_vec_transient_map(t, double_val);
  ASSERT(mapped != NULL);

  int_vec_root* p = int_vec_persistent_fn(mapped);
  ASSERT_INT_EQ(int_vec_count(p), 1000);
  for (int i = 0; i < 1000; i++) {
    int_vec_get_result gr = int_vec_get(p, (uint32_t)i);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, i * 2);
  }

  /* Clean up source (not consumed) and result */
  int_vec_root* src_p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(src_p);
  int_vec_rc_unref(mapped);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_map_empty(void) {
  printf("  test_transient_map_empty: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  int_vec_transient* mapped = int_vec_transient_map(t, double_val);
  ASSERT(mapped != NULL);

  int_vec_root* p = int_vec_persistent_fn(mapped);
  ASSERT_INT_EQ(int_vec_count(p), 0);

  int_vec_root* src_p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(src_p);
  int_vec_rc_unref(mapped);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-004: Transient filter tests ===== */

static bool is_even(int x) { return x % 2 == 0; }
static bool always_false(int x) { (void)x; return false; }
static bool always_true(int x) { (void)x; return true; }

int test_transient_filter_even(void) {
  printf("  test_transient_filter_even: ");
  tracker_reset();

  int_vec_root* empty = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(empty);
  int_vec_unref(empty);
  for (int i = 0; i < 1000; i++) {
    int_vec_transient_push_back(t, i);
  }

  int_vec_transient* filtered = int_vec_transient_filter(t, is_even);
  ASSERT(filtered != NULL);

  int_vec_root* p = int_vec_persistent_fn(filtered);
  ASSERT_INT_EQ(int_vec_count(p), 500);
  for (int i = 0; i < 500; i++) {
    int_vec_get_result gr = int_vec_get(p, (uint32_t)i);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, i * 2);
  }

  int_vec_root* src_p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(src_p);
  int_vec_rc_unref(filtered);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_filter_none_match(void) {
  printf("  test_transient_filter_none_match: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(r, i);
    int_vec_unref(r);
    r = next;
  }

  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  int_vec_transient* filtered = int_vec_transient_filter(t, always_false);
  ASSERT(filtered != NULL);

  int_vec_root* p = int_vec_persistent_fn(filtered);
  ASSERT_INT_EQ(int_vec_count(p), 0);

  int_vec_root* src_p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(src_p);
  int_vec_rc_unref(filtered);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_filter_all_match(void) {
  printf("  test_transient_filter_all_match: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  for (int i = 0; i < 100; i++) {
    int_vec_root* next = int_vec_push_back(r, i);
    int_vec_unref(r);
    r = next;
  }

  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  int_vec_transient* filtered = int_vec_transient_filter(t, always_true);
  ASSERT(filtered != NULL);

  int_vec_root* p = int_vec_persistent_fn(filtered);
  ASSERT_INT_EQ(int_vec_count(p), 100);
  for (int i = 0; i < 100; i++) {
    int_vec_get_result gr = int_vec_get(p, (uint32_t)i);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, i);
  }

  int_vec_root* src_p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(src_p);
  int_vec_rc_unref(filtered);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_filter_empty(void) {
  printf("  test_transient_filter_empty: ");
  tracker_reset();

  int_vec_root* r = int_vec_empty();
  int_vec_transient* t = int_vec_transient_fn(r);
  int_vec_unref(r);

  int_vec_transient* filtered = int_vec_transient_filter(t, is_even);
  ASSERT(filtered != NULL);

  int_vec_root* p = int_vec_persistent_fn(filtered);
  ASSERT_INT_EQ(int_vec_count(p), 0);

  int_vec_root* src_p = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  int_vec_unref(src_p);
  int_vec_rc_unref(filtered);
  int_vec_unref(p);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== Main ===== */

int main(void) {
  int pass = 0, fail = 0;

  printf("US-001: Transient iterator\n");
  if (test_transient_iter_empty()) pass++; else fail++;
  if (test_transient_iter_100()) pass++; else fail++;
  if (test_transient_iter_10000()) pass++; else fail++;
  if (test_transient_iter_then_push()) pass++; else fail++;

  printf("US-002: Transient for_each\n");
  if (test_transient_for_each_empty()) pass++; else fail++;
  if (test_transient_for_each_100()) pass++; else fail++;
  if (test_transient_for_each_no_invalidate()) pass++; else fail++;

  printf("US-003: Transient map\n");
  if (test_transient_map_1000()) pass++; else fail++;
  if (test_transient_map_empty()) pass++; else fail++;

  printf("US-004: Transient filter\n");
  if (test_transient_filter_even()) pass++; else fail++;
  if (test_transient_filter_none_match()) pass++; else fail++;
  if (test_transient_filter_all_match()) pass++; else fail++;
  if (test_transient_filter_empty()) pass++; else fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
