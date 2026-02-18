#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

/* ========================================================================
 * Instantiation 1: size-rope — char elements, size_t summary (length)
 * ======================================================================== */

#define STREE_ALLOCATOR                 \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#define STREE_T          char
#define STREE_SUMMARY_T  size_t
#define STREE_NAME       rope
#define STREE_LEAF_MAX   4
#include "sum_tree.h"

static size_t rope_identity(void) { return 0; }
static size_t rope_combine(size_t a, size_t b) { return a + b; }
static size_t rope_summarize(const char* elems, size_t count) {
  (void)elems;
  return count;
}

/* ========================================================================
 * Instantiation 2: sum-tree — int elements, int summary (sum)
 * ======================================================================== */

#define STREE_ALLOCATOR                 \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#define STREE_T          int
#define STREE_SUMMARY_T  int
#define STREE_NAME       sumtree
#define STREE_LEAF_MAX   4
#include "sum_tree.h"

static int sumtree_identity_fn(void) { return 0; }
static int sumtree_combine_fn(int a, int b) { return a + b; }
static int sumtree_summarize_fn(const int* elems, size_t count) {
  int s = 0;
  for (size_t i = 0; i < count; i++) s += elems[i];
  return s;
}

/* === US-001 Tests === */

int test_empty_tree() {
  printf("Running test_empty_tree... ");
  tracker_reset();

  rope_set_handlers((rope_handlers){
    .identity  = rope_identity,
    .combine   = rope_combine,
    .summarize = rope_summarize
  });

  rope_root r = rope_empty();
  ASSERT_INT_EQ(rope_count(r), 0);
  ASSERT_INT_EQ(r.height, 0);
  ASSERT(r.node == NULL);
  rope_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_empty_sumtree() {
  printf("Running test_empty_sumtree... ");
  tracker_reset();

  sumtree_set_handlers((sumtree_handlers){
    .identity  = sumtree_identity_fn,
    .combine   = sumtree_combine_fn,
    .summarize = sumtree_summarize_fn
  });

  sumtree_root r = sumtree_empty();
  ASSERT_INT_EQ(sumtree_count(r), 0);
  ASSERT_INT_EQ(sumtree_summary(r), 0);
  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_two_instantiations() {
  printf("Running test_two_instantiations... ");
  tracker_reset();

  rope_set_handlers((rope_handlers){
    .identity  = rope_identity,
    .combine   = rope_combine,
    .summarize = rope_summarize
  });
  sumtree_set_handlers((sumtree_handlers){
    .identity  = sumtree_identity_fn,
    .combine   = sumtree_combine_fn,
    .summarize = sumtree_summarize_fn
  });

  /* Use both in the same scope */
  rope_root rr = rope_empty();
  sumtree_root sr = sumtree_empty();

  ASSERT_INT_EQ(rope_count(rr), 0);
  ASSERT_INT_EQ(sumtree_count(sr), 0);

  rope_unref(rr);
  sumtree_unref(sr);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-002 Tests === */

int test_from_array_empty() {
  printf("Running test_from_array_empty... ");
  tracker_reset();
  rope_set_handlers((rope_handlers){
    .identity = rope_identity, .combine = rope_combine, .summarize = rope_summarize
  });

  rope_root r = rope_from_array(NULL, 0);
  ASSERT_INT_EQ(rope_count(r), 0);
  rope_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_from_array_one() {
  printf("Running test_from_array_one... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {42};
  sumtree_root r = sumtree_from_array(data, 1);
  ASSERT_INT_EQ(sumtree_count(r), 1);
  ASSERT_INT_EQ(sumtree_summary(r), 42);

  sumtree_get_result g = sumtree_get(r, 0);
  ASSERT(g.found);
  ASSERT_INT_EQ(g.value, 42);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_from_array_five() {
  printf("Running test_from_array_five... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {10, 20, 30, 40, 50};
  sumtree_root r = sumtree_from_array(data, 5);
  ASSERT_INT_EQ(sumtree_count(r), 5);
  ASSERT_INT_EQ(sumtree_summary(r), 150);

  for (int i = 0; i < 5; i++) {
    sumtree_get_result g = sumtree_get(r, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_from_array_33() {
  printf("Running test_from_array_33... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[33];
  int expected_sum = 0;
  for (int i = 0; i < 33; i++) {
    data[i] = i + 1;
    expected_sum += i + 1;
  }

  sumtree_root r = sumtree_from_array(data, 33);
  ASSERT_INT_EQ(sumtree_count(r), 33);
  ASSERT_INT_EQ(sumtree_summary(r), expected_sum);

  for (int i = 0; i < 33; i++) {
    sumtree_get_result g = sumtree_get(r, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_from_array_1000() {
  printf("Running test_from_array_1000... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[1000];
  int expected_sum = 0;
  for (int i = 0; i < 1000; i++) {
    data[i] = i;
    expected_sum += i;
  }

  sumtree_root r = sumtree_from_array(data, 1000);
  ASSERT_INT_EQ(sumtree_count(r), 1000);
  ASSERT_INT_EQ(sumtree_summary(r), expected_sum);

  for (int i = 0; i < 1000; i++) {
    sumtree_get_result g = sumtree_get(r, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_get_out_of_bounds() {
  printf("Running test_get_out_of_bounds... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3};
  sumtree_root r = sumtree_from_array(data, 3);

  sumtree_get_result g = sumtree_get(r, 3);
  ASSERT(!g.found);
  g = sumtree_get(r, 100);
  ASSERT(!g.found);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_summary_rope() {
  printf("Running test_summary_rope... ");
  tracker_reset();
  rope_set_handlers((rope_handlers){
    .identity = rope_identity, .combine = rope_combine, .summarize = rope_summarize
  });

  const char* text = "hello";
  rope_root r = rope_from_array(text, 5);
  ASSERT_INT_EQ(rope_count(r), 5);
  ASSERT_INT_EQ(rope_summary(r), 5);

  for (size_t i = 0; i < 5; i++) {
    rope_get_result g = rope_get(r, i);
    ASSERT(g.found);
    ASSERT(g.value == text[i]);
  }

  rope_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-003 Tests (Split) === */

int test_split_at_zero() {
  printf("Running test_split_at_zero... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3, 4, 5};
  sumtree_root r = sumtree_from_array(data, 5);

  sumtree_split_result s = sumtree_split(r, 0);
  ASSERT(s.ok);
  ASSERT_INT_EQ(sumtree_count(s.left), 0);
  ASSERT_INT_EQ(sumtree_count(s.right), 5);

  for (int i = 0; i < 5; i++) {
    sumtree_get_result g = sumtree_get(s.right, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(s.left);
  sumtree_unref(s.right);
  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_split_at_end() {
  printf("Running test_split_at_end... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3, 4, 5};
  sumtree_root r = sumtree_from_array(data, 5);

  sumtree_split_result s = sumtree_split(r, 5);
  ASSERT(s.ok);
  ASSERT_INT_EQ(sumtree_count(s.left), 5);
  ASSERT_INT_EQ(sumtree_count(s.right), 0);

  for (int i = 0; i < 5; i++) {
    sumtree_get_result g = sumtree_get(s.left, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(s.left);
  sumtree_unref(s.right);
  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_split_at_middle() {
  printf("Running test_split_at_middle... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
  sumtree_root r = sumtree_from_array(data, 10);

  sumtree_split_result s = sumtree_split(r, 4);
  ASSERT(s.ok);
  ASSERT_INT_EQ(sumtree_count(s.left), 4);
  ASSERT_INT_EQ(sumtree_count(s.right), 6);

  /* Verify left: [10,20,30,40] */
  for (int i = 0; i < 4; i++) {
    sumtree_get_result g = sumtree_get(s.left, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  /* Verify right: [50,60,70,80,90,100] */
  for (int i = 0; i < 6; i++) {
    sumtree_get_result g = sumtree_get(s.right, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[4 + i]);
  }

  /* Verify summaries */
  ASSERT_INT_EQ(sumtree_summary(s.left), 10 + 20 + 30 + 40);
  ASSERT_INT_EQ(sumtree_summary(s.right), 50 + 60 + 70 + 80 + 90 + 100);

  sumtree_unref(s.left);
  sumtree_unref(s.right);
  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_split_original_unchanged() {
  printf("Running test_split_original_unchanged... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3, 4, 5, 6, 7, 8};
  sumtree_root r = sumtree_from_array(data, 8);

  sumtree_split_result s = sumtree_split(r, 3);
  ASSERT(s.ok);

  /* Original should be unchanged */
  ASSERT_INT_EQ(sumtree_count(r), 8);
  for (int i = 0; i < 8; i++) {
    sumtree_get_result g = sumtree_get(r, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(s.left);
  sumtree_unref(s.right);
  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_split_out_of_bounds() {
  printf("Running test_split_out_of_bounds... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3};
  sumtree_root r = sumtree_from_array(data, 3);

  sumtree_split_result s = sumtree_split(r, 10);
  ASSERT(!s.ok);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-004 Tests (Concat) === */

int test_concat_two_nonempty() {
  printf("Running test_concat_two_nonempty... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int left_data[] = {1, 2, 3};
  int right_data[] = {4, 5, 6};
  sumtree_root left = sumtree_from_array(left_data, 3);
  sumtree_root right = sumtree_from_array(right_data, 3);

  sumtree_root result = sumtree_concat(left, right);
  ASSERT_INT_EQ(sumtree_count(result), 6);
  ASSERT_INT_EQ(sumtree_summary(result), 21);

  for (int i = 0; i < 6; i++) {
    sumtree_get_result g = sumtree_get(result, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, i + 1);
  }

  sumtree_unref(left);
  sumtree_unref(right);
  sumtree_unref(result);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_concat_with_empty() {
  printf("Running test_concat_with_empty... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3};
  sumtree_root tree = sumtree_from_array(data, 3);
  sumtree_root empty = sumtree_empty();

  /* Concat with empty right */
  sumtree_root r1 = sumtree_concat(tree, empty);
  ASSERT_INT_EQ(sumtree_count(r1), 3);
  for (int i = 0; i < 3; i++) {
    sumtree_get_result g = sumtree_get(r1, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  /* Concat with empty left */
  sumtree_root r2 = sumtree_concat(empty, tree);
  ASSERT_INT_EQ(sumtree_count(r2), 3);
  for (int i = 0; i < 3; i++) {
    sumtree_get_result g = sumtree_get(r2, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(tree);
  sumtree_unref(empty);
  sumtree_unref(r1);
  sumtree_unref(r2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_concat_large() {
  printf("Running test_concat_large... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int left_data[500];
  int right_data[500];
  int expected_sum = 0;
  for (int i = 0; i < 500; i++) {
    left_data[i] = i;
    right_data[i] = 500 + i;
    expected_sum += i + 500 + i;
  }

  sumtree_root left = sumtree_from_array(left_data, 500);
  sumtree_root right = sumtree_from_array(right_data, 500);
  sumtree_root result = sumtree_concat(left, right);

  ASSERT_INT_EQ(sumtree_count(result), 1000);
  ASSERT_INT_EQ(sumtree_summary(result), expected_sum);

  for (int i = 0; i < 1000; i++) {
    sumtree_get_result g = sumtree_get(result, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, i);
  }

  sumtree_unref(left);
  sumtree_unref(right);
  sumtree_unref(result);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_concat_originals_unchanged() {
  printf("Running test_concat_originals_unchanged... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int left_data[] = {1, 2, 3};
  int right_data[] = {4, 5};
  sumtree_root left = sumtree_from_array(left_data, 3);
  sumtree_root right = sumtree_from_array(right_data, 2);
  sumtree_root result = sumtree_concat(left, right);

  /* Originals should be unchanged */
  ASSERT_INT_EQ(sumtree_count(left), 3);
  ASSERT_INT_EQ(sumtree_count(right), 2);
  ASSERT_INT_EQ(sumtree_summary(left), 6);
  ASSERT_INT_EQ(sumtree_summary(right), 9);

  sumtree_unref(left);
  sumtree_unref(right);
  sumtree_unref(result);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_concat_summary() {
  printf("Running test_concat_summary... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int left_data[] = {10, 20, 30};
  int right_data[] = {40, 50};
  sumtree_root left = sumtree_from_array(left_data, 3);
  sumtree_root right = sumtree_from_array(right_data, 2);
  sumtree_root result = sumtree_concat(left, right);

  int ls = sumtree_summary(left);
  int rs = sumtree_summary(right);
  ASSERT_INT_EQ(sumtree_summary(result), ls + rs);

  sumtree_unref(left);
  sumtree_unref(right);
  sumtree_unref(result);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-005 Tests (Set, push_back, push_front) === */

int test_set_element() {
  printf("Running test_set_element... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {10, 20, 30, 40, 50};
  sumtree_root old = sumtree_from_array(data, 5);

  /* Set at beginning */
  sumtree_root r0 = sumtree_set(old, 0, 99);
  ASSERT(r0.node != NULL);
  ASSERT_INT_EQ(sumtree_count(r0), 5);
  sumtree_get_result g = sumtree_get(r0, 0);
  ASSERT(g.found);
  ASSERT_INT_EQ(g.value, 99);

  /* Set at middle */
  sumtree_root r2 = sumtree_set(old, 2, 77);
  ASSERT(r2.node != NULL);
  g = sumtree_get(r2, 2);
  ASSERT(g.found);
  ASSERT_INT_EQ(g.value, 77);

  /* Set at end */
  sumtree_root r4 = sumtree_set(old, 4, 55);
  ASSERT(r4.node != NULL);
  g = sumtree_get(r4, 4);
  ASSERT(g.found);
  ASSERT_INT_EQ(g.value, 55);

  /* Old tree unchanged */
  for (int i = 0; i < 5; i++) {
    g = sumtree_get(old, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, data[i]);
  }

  sumtree_unref(old);
  sumtree_unref(r0);
  sumtree_unref(r2);
  sumtree_unref(r4);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_set_out_of_bounds() {
  printf("Running test_set_out_of_bounds... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3};
  sumtree_root old = sumtree_from_array(data, 3);

  sumtree_root bad = sumtree_set(old, 10, 99);
  ASSERT(bad.node == NULL);

  sumtree_unref(old);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_push_back() {
  printf("Running test_push_back... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  sumtree_root r = sumtree_empty();
  for (int i = 0; i < 100; i++) {
    sumtree_root new_r = sumtree_push_back(r, i);
    sumtree_unref(r);
    r = new_r;
  }

  ASSERT_INT_EQ(sumtree_count(r), 100);
  int expected_sum = 0;
  for (int i = 0; i < 100; i++) {
    expected_sum += i;
    sumtree_get_result g = sumtree_get(r, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, i);
  }
  ASSERT_INT_EQ(sumtree_summary(r), expected_sum);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_push_front() {
  printf("Running test_push_front... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  sumtree_root r = sumtree_empty();
  for (int i = 0; i < 100; i++) {
    sumtree_root new_r = sumtree_push_front(r, i);
    sumtree_unref(r);
    r = new_r;
  }

  ASSERT_INT_EQ(sumtree_count(r), 100);

  /* push_front reverses order: last pushed is at index 0 */
  for (int i = 0; i < 100; i++) {
    sumtree_get_result g = sumtree_get(r, (size_t)i);
    ASSERT(g.found);
    ASSERT_INT_EQ(g.value, 99 - i);
  }

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-001 Search Tests === */

/* Dimension extractor for sumtree: the summary IS the dimension */
static size_t sumtree_dim_sum(int summary) { return (size_t)summary; }

/* Dimension extractor for rope: the summary IS the count */
static size_t rope_dim_count(size_t summary) { return summary; }

int test_search_sum_target() {
  printf("Running test_search_sum_target... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  /* Elements: [10, 20, 30, 40, 50], cumulative sums: [10, 30, 60, 100, 150] */
  int data[] = {10, 20, 30, 40, 50};
  sumtree_root r = sumtree_from_array(data, 5);

  /* Search for cumulative sum target=25: should land at index 1 (cum sum 10+20=30 > 25) */
  sumtree_search_result sr = sumtree_search(r, sumtree_dim_sum, 25);
  ASSERT(sr.found);
  ASSERT_INT_EQ((int)sr.index, 1);
  ASSERT_INT_EQ((int)sumtree_dim_sum(sr.prefix_summary), 10); /* prefix before index 1 = sum of [10] */

  /* Search for target=0: should land at index 0 */
  sr = sumtree_search(r, sumtree_dim_sum, 0);
  ASSERT(sr.found);
  ASSERT_INT_EQ((int)sr.index, 0);
  ASSERT_INT_EQ((int)sumtree_dim_sum(sr.prefix_summary), 0);

  /* Search for target=59: should land at index 2 (cum sum 60 > 59) */
  sr = sumtree_search(r, sumtree_dim_sum, 59);
  ASSERT(sr.found);
  ASSERT_INT_EQ((int)sr.index, 2);
  ASSERT_INT_EQ((int)sumtree_dim_sum(sr.prefix_summary), 30);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_search_rope_offset() {
  printf("Running test_search_rope_offset... ");
  tracker_reset();
  rope_set_handlers((rope_handlers){
    .identity = rope_identity, .combine = rope_combine, .summarize = rope_summarize
  });

  const char* text = "hello world!";
  rope_root r = rope_from_array(text, 12);

  /* Search for offset 5: should find index 5 (each char has count 1) */
  rope_search_result sr = rope_search(r, rope_dim_count, 5);
  ASSERT(sr.found);
  ASSERT_INT_EQ((int)sr.index, 5);
  ASSERT_INT_EQ((int)sr.prefix_summary, 5);

  /* Verify we get the right char */
  rope_get_result g = rope_get(r, sr.index);
  ASSERT(g.found);
  ASSERT(g.value == ' ');

  rope_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_search_target_zero() {
  printf("Running test_search_target_zero... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {5, 10, 15};
  sumtree_root r = sumtree_from_array(data, 3);

  sumtree_search_result sr = sumtree_search(r, sumtree_dim_sum, 0);
  ASSERT(sr.found);
  ASSERT_INT_EQ((int)sr.index, 0);
  ASSERT_INT_EQ((int)sumtree_dim_sum(sr.prefix_summary), 0);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_search_beyond_total() {
  printf("Running test_search_beyond_total... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {10, 20, 30};
  sumtree_root r = sumtree_from_array(data, 3);

  /* Total sum = 60, search for 60 (at or beyond total) */
  sumtree_search_result sr = sumtree_search(r, sumtree_dim_sum, 60);
  ASSERT(!sr.found);

  /* Search for 100 (well beyond) */
  sr = sumtree_search(r, sumtree_dim_sum, 100);
  ASSERT(!sr.found);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_search_empty_tree() {
  printf("Running test_search_empty_tree... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  sumtree_root r = sumtree_empty();
  sumtree_search_result sr = sumtree_search(r, sumtree_dim_sum, 0);
  ASSERT(!sr.found);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-002 (rope PRD): Copy range to buffer === */

int test_copy_range_entire() {
  printf("Running test_copy_range_entire... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
  sumtree_root r = sumtree_from_array(data, 10);

  int buf[10];
  size_t copied = sumtree_copy_range(r, 0, 10, buf);
  ASSERT_INT_EQ((int)copied, 10);
  for (int i = 0; i < 10; i++) {
    ASSERT_INT_EQ(buf[i], data[i]);
  }

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_copy_range_partial_middle() {
  printf("Running test_copy_range_partial_middle... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
  sumtree_root r = sumtree_from_array(data, 10);

  int buf[4];
  size_t copied = sumtree_copy_range(r, 3, 4, buf);
  ASSERT_INT_EQ((int)copied, 4);
  ASSERT_INT_EQ(buf[0], 40);
  ASSERT_INT_EQ(buf[1], 50);
  ASSERT_INT_EQ(buf[2], 60);
  ASSERT_INT_EQ(buf[3], 70);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_copy_range_beyond_bounds() {
  printf("Running test_copy_range_beyond_bounds... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {1, 2, 3};
  sumtree_root r = sumtree_from_array(data, 3);

  int buf[1];
  size_t copied = sumtree_copy_range(r, 10, 1, buf);
  ASSERT_INT_EQ((int)copied, 0);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_copy_range_extends_past_end() {
  printf("Running test_copy_range_extends_past_end... ");
  tracker_reset();
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn, .summarize = sumtree_summarize_fn
  });

  int data[] = {10, 20, 30, 40, 50};
  sumtree_root r = sumtree_from_array(data, 5);

  int buf[10];
  size_t copied = sumtree_copy_range(r, 3, 100, buf);
  ASSERT_INT_EQ((int)copied, 2);
  ASSERT_INT_EQ(buf[0], 40);
  ASSERT_INT_EQ(buf[1], 50);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-020 Tests (Custom leaf_search) === */

/* Custom leaf_search that reimplements the default element-by-element logic */
static size_t custom_sumtree_leaf_search(const int* elements, size_t count,
                                          int* accumulated,
                                          size_t (*dimension_fn)(int),
                                          size_t target) {
  for (size_t i = 0; i < count; i++) {
    int elem_summary = sumtree_summarize_fn(&elements[i], 1);
    size_t acc_dim = dimension_fn(*accumulated);
    size_t elem_dim = dimension_fn(elem_summary);
    if (acc_dim + elem_dim > target) {
      return i;
    }
    *accumulated = sumtree_combine_fn(*accumulated, elem_summary);
  }
  return count; /* not found */
}

int test_custom_leaf_search_matches_default() {
  printf("Running test_custom_leaf_search_matches_default... ");
  tracker_reset();

  /* First run: default leaf_search (NULL) */
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn,
    .summarize = sumtree_summarize_fn, .leaf_search = NULL
  });

  int data[] = {10, 20, 30, 40, 50};
  sumtree_root r = sumtree_from_array(data, 5);

  /* Collect results with default scan */
  size_t targets[] = {0, 9, 10, 25, 29, 30, 59, 60, 99, 100, 149};
  size_t n_targets = sizeof(targets) / sizeof(targets[0]);
  sumtree_search_result default_results[11];

  for (size_t t = 0; t < n_targets; t++) {
    default_results[t] = sumtree_search(r, sumtree_dim_sum, targets[t]);
  }

  /* Second run: custom leaf_search */
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn,
    .summarize = sumtree_summarize_fn, .leaf_search = custom_sumtree_leaf_search
  });

  for (size_t t = 0; t < n_targets; t++) {
    sumtree_search_result sr = sumtree_search(r, sumtree_dim_sum, targets[t]);
    ASSERT(sr.found == default_results[t].found);
    if (sr.found) {
      ASSERT_INT_EQ((int)sr.index, (int)default_results[t].index);
      ASSERT_INT_EQ((int)sumtree_dim_sum(sr.prefix_summary),
                     (int)sumtree_dim_sum(default_results[t].prefix_summary));
    }
  }

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_custom_leaf_search_large_tree() {
  printf("Running test_custom_leaf_search_large_tree... ");
  tracker_reset();

  /* Test with a larger tree that spans multiple leaves and internal nodes */
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn,
    .summarize = sumtree_summarize_fn, .leaf_search = NULL
  });

  int data[100];
  for (int i = 0; i < 100; i++) data[i] = i + 1;
  sumtree_root r = sumtree_from_array(data, 100);

  /* Collect default results for various targets */
  size_t test_targets[] = {0, 1, 50, 100, 500, 1000, 4999, 5049};
  size_t n = sizeof(test_targets) / sizeof(test_targets[0]);
  sumtree_search_result defaults[8];

  for (size_t t = 0; t < n; t++) {
    defaults[t] = sumtree_search(r, sumtree_dim_sum, test_targets[t]);
  }

  /* Run with custom leaf_search */
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn,
    .summarize = sumtree_summarize_fn, .leaf_search = custom_sumtree_leaf_search
  });

  for (size_t t = 0; t < n; t++) {
    sumtree_search_result sr = sumtree_search(r, sumtree_dim_sum, test_targets[t]);
    ASSERT(sr.found == defaults[t].found);
    if (sr.found) {
      ASSERT_INT_EQ((int)sr.index, (int)defaults[t].index);
      ASSERT_INT_EQ((int)sumtree_dim_sum(sr.prefix_summary),
                     (int)sumtree_dim_sum(defaults[t].prefix_summary));
    }
  }

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_null_leaf_search_unchanged() {
  printf("Running test_null_leaf_search_unchanged... ");
  tracker_reset();

  /* Verify that zero-initialized handlers (leaf_search = NULL) work as before */
  sumtree_set_handlers((sumtree_handlers){
    .identity = sumtree_identity_fn, .combine = sumtree_combine_fn,
    .summarize = sumtree_summarize_fn
    /* leaf_search not set — defaults to NULL via zero-init */
  });

  int data[] = {10, 20, 30, 40, 50};
  sumtree_root r = sumtree_from_array(data, 5);

  sumtree_search_result sr = sumtree_search(r, sumtree_dim_sum, 25);
  ASSERT(sr.found);
  ASSERT_INT_EQ((int)sr.index, 1);
  ASSERT_INT_EQ((int)sumtree_dim_sum(sr.prefix_summary), 10);

  sr = sumtree_search(r, sumtree_dim_sum, 0);
  ASSERT(sr.found);
  ASSERT_INT_EQ((int)sr.index, 0);

  sumtree_unref(r);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === main === */

int main(void) {
  int passed = 0;
  int failed = 0;

#define RUN_TEST(func) \
  do {                 \
    if (func()) {      \
      passed++;        \
    } else {           \
      failed++;        \
    }                  \
  } while (0)

  /* US-001: Core data structure */
  RUN_TEST(test_empty_tree);
  RUN_TEST(test_empty_sumtree);
  RUN_TEST(test_two_instantiations);

  /* US-002: Build from array and get */
  RUN_TEST(test_from_array_empty);
  RUN_TEST(test_from_array_one);
  RUN_TEST(test_from_array_five);
  RUN_TEST(test_from_array_33);
  RUN_TEST(test_from_array_1000);
  RUN_TEST(test_get_out_of_bounds);
  RUN_TEST(test_summary_rope);

  /* US-003: Split */
  RUN_TEST(test_split_at_zero);
  RUN_TEST(test_split_at_end);
  RUN_TEST(test_split_at_middle);
  RUN_TEST(test_split_original_unchanged);
  RUN_TEST(test_split_out_of_bounds);

  /* US-004: Concat */
  RUN_TEST(test_concat_two_nonempty);
  RUN_TEST(test_concat_with_empty);
  RUN_TEST(test_concat_large);
  RUN_TEST(test_concat_originals_unchanged);
  RUN_TEST(test_concat_summary);

  /* US-005: Set, push_back, push_front */
  RUN_TEST(test_set_element);
  RUN_TEST(test_set_out_of_bounds);
  RUN_TEST(test_push_back);
  RUN_TEST(test_push_front);

  /* US-001 (rope PRD): Search by summary */
  RUN_TEST(test_search_sum_target);
  RUN_TEST(test_search_rope_offset);
  RUN_TEST(test_search_target_zero);
  RUN_TEST(test_search_beyond_total);
  RUN_TEST(test_search_empty_tree);

  /* US-002 (rope PRD): Copy range */
  RUN_TEST(test_copy_range_entire);
  RUN_TEST(test_copy_range_partial_middle);
  RUN_TEST(test_copy_range_beyond_bounds);
  RUN_TEST(test_copy_range_extends_past_end);

  /* US-020: Custom leaf_search */
  RUN_TEST(test_custom_leaf_search_matches_default);
  RUN_TEST(test_custom_leaf_search_large_tree);
  RUN_TEST(test_null_leaf_search_unchanged);

  printf("\n=== Test Summary ===\n");
  printf("Passed: %d\n", passed);
  printf("Failed: %d\n", failed);

  return failed > 0 ? 1 : 0;
}
