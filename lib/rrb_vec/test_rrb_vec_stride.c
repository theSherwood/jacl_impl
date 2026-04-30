#include "../../test/test_helpers.h"

#define TRACKED_ALLOC                     \
  {                                       \
      .alloc = tracked_malloc_with_ctx,   \
      .free  = tracked_free_with_ctx,     \
  }

/* Instantiate an int vector for stride testing */
#define RRB_VEC_T         int
#define RRB_VEC_NAME      sv
#define RRB_VEC_ALLOCATOR TRACKED_ALLOC
#include "rrb_vec.h"

/* Stride=3: each logical element occupies 3 int slots */
#define STRIDE 3

/* Helper: build a wide value from an index (deterministic, easy to verify) */
static void make_val(int idx, int out[STRIDE]) {
  out[0] = idx * 100;
  out[1] = idx * 100 + 1;
  out[2] = idx * 100 + 2;
}

/* Helper: check a wide value at the given index */
static int check_val(sv_root* v, int idx) {
  const int* p = sv_get_ptr(v, (uint32_t)idx);
  if (!p) return 0;
  int expected[STRIDE];
  make_val(idx, expected);
  return p[0] == expected[0] && p[1] == expected[1] && p[2] == expected[2];
}

/* Test: empty strided vec has count 0 and correct stride */
int test_stride_empty(void) {
  printf("  test_stride_empty: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  ASSERT(v != NULL);
  ASSERT_INT_EQ(sv_count(v), 0);
  ASSERT_INT_EQ(sv_stride(v), STRIDE);

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push_back_wide and get_ptr for small count */
int test_stride_push_get_small(void) {
  printf("  test_stride_push_get_small: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  for (int i = 0; i < 10; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(v, vals);
    sv_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(sv_count(v), 10);

  for (int i = 0; i < 10; i++) {
    ASSERT(check_val(v, i));
  }

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push enough elements to overflow tail and build tree (>32) */
int test_stride_push_get_large(void) {
  printf("  test_stride_push_get_large: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  int N = 200;
  for (int i = 0; i < N; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(v, vals);
    sv_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(sv_count(v), (uint32_t)N);

  for (int i = 0; i < N; i++) {
    ASSERT(check_val(v, i));
  }

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set_wide updates in tail and tree */
int test_stride_set_wide(void) {
  printf("  test_stride_set_wide: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  for (int i = 0; i < 100; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(v, vals);
    sv_unref(v);
    v = next;
  }

  /* Overwrite element 5 (in tree) and element 99 (in tail) */
  int new5[STRIDE] = {555, 556, 557};
  sv_root* v2 = sv_set_wide(v, 5, new5);

  int new99[STRIDE] = {999, 998, 997};
  sv_root* v3 = sv_set_wide(v2, 99, new99);
  sv_unref(v2);

  /* Original is unchanged */
  ASSERT(check_val(v, 5));
  ASSERT(check_val(v, 99));

  /* New vec has updated values */
  const int* p5 = sv_get_ptr(v3, 5);
  ASSERT(p5[0] == 555 && p5[1] == 556 && p5[2] == 557);

  const int* p99 = sv_get_ptr(v3, 99);
  ASSERT(p99[0] == 999 && p99[1] == 998 && p99[2] == 997);

  /* Other elements untouched */
  ASSERT(check_val(v3, 0));
  ASSERT(check_val(v3, 50));

  sv_unref(v);
  sv_unref(v3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pop_back on strided vec */
int test_stride_pop_back(void) {
  printf("  test_stride_pop_back: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  for (int i = 0; i < 50; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(v, vals);
    sv_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(sv_count(v), 50);

  /* Pop 10 elements */
  for (int i = 0; i < 10; i++) {
    sv_pop_result pr = sv_pop_back(v);
    ASSERT(pr.root != NULL);
    sv_unref(v);
    v = pr.root;
  }
  ASSERT_INT_EQ(sv_count(v), 40);

  /* Remaining elements intact */
  for (int i = 0; i < 40; i++) {
    ASSERT(check_val(v, i));
  }

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: concat two strided vecs */
int test_stride_concat(void) {
  printf("  test_stride_concat: ");
  tracker_reset();

  sv_root* a = sv_empty_strided(STRIDE);
  sv_root* b = sv_empty_strided(STRIDE);
  for (int i = 0; i < 40; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(a, vals);
    sv_unref(a);
    a = next;
  }
  for (int i = 40; i < 80; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(b, vals);
    sv_unref(b);
    b = next;
  }

  sv_root* c = sv_concat(a, b);
  ASSERT_INT_EQ(sv_count(c), 80);

  for (int i = 0; i < 80; i++) {
    ASSERT(check_val(c, i));
  }

  sv_unref(a);
  sv_unref(b);
  sv_unref(c);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: slice a strided vec */
int test_stride_slice(void) {
  printf("  test_stride_slice: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  for (int i = 0; i < 100; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(v, vals);
    sv_unref(v);
    v = next;
  }

  sv_root* s = sv_slice(v, 20, 60);
  ASSERT_INT_EQ(sv_count(s), 40);

  for (int i = 0; i < 40; i++) {
    const int* p = sv_get_ptr(s, (uint32_t)i);
    ASSERT(p != NULL);
    int expected[STRIDE];
    make_val(i + 20, expected);
    ASSERT(p[0] == expected[0] && p[1] == expected[1] && p[2] == expected[2]);
  }

  sv_unref(v);
  sv_unref(s);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: from_array_strided builds a vec from flat data */
int test_stride_from_array(void) {
  printf("  test_stride_from_array: ");
  tracker_reset();

  int N = 50;
  int flat[50 * STRIDE];
  for (int i = 0; i < N; i++) {
    make_val(i, &flat[i * STRIDE]);
  }

  sv_root* v = sv_from_array_strided(flat, (uint32_t)N, STRIDE);
  ASSERT_INT_EQ(sv_count(v), (uint32_t)N);
  ASSERT_INT_EQ(sv_stride(v), STRIDE);

  for (int i = 0; i < N; i++) {
    ASSERT(check_val(v, i));
  }

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: to_array round-trips with from_array_strided */
int test_stride_to_array(void) {
  printf("  test_stride_to_array: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  int N = 70;
  for (int i = 0; i < N; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(v, vals);
    sv_unref(v);
    v = next;
  }

  uint32_t out_count;
  int* arr = sv_to_array(v, &out_count);
  ASSERT_INT_EQ(out_count, (uint32_t)N);

  /* Verify the flat array matches */
  for (int i = 0; i < N; i++) {
    int expected[STRIDE];
    make_val(i, expected);
    ASSERT(arr[i * STRIDE] == expected[0]);
    ASSERT(arr[i * STRIDE + 1] == expected[1]);
    ASSERT(arr[i * STRIDE + 2] == expected[2]);
  }

  tracked_free_with_ctx(NULL, arr);
  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: get_ptr returns NULL for out-of-bounds */
int test_stride_get_ptr_oob(void) {
  printf("  test_stride_get_ptr_oob: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  ASSERT(sv_get_ptr(v, 0) == NULL);

  int vals[STRIDE] = {1, 2, 3};
  sv_root* v2 = sv_push_back_wide(v, vals);
  ASSERT(sv_get_ptr(v2, 0) != NULL);
  ASSERT(sv_get_ptr(v2, 1) == NULL);

  sv_unref(v);
  sv_unref(v2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set_wide returns NULL for out-of-bounds */
int test_stride_set_wide_oob(void) {
  printf("  test_stride_set_wide_oob: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  int vals[STRIDE] = {1, 2, 3};
  ASSERT(sv_set_wide(v, 0, vals) == NULL);

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: persistence — push_back_wide doesn't mutate original */
int test_stride_persistence(void) {
  printf("  test_stride_persistence: ");
  tracker_reset();

  sv_root* v1 = sv_empty_strided(STRIDE);
  int a[STRIDE] = {10, 20, 30};
  sv_root* v2 = sv_push_back_wide(v1, a);
  int b[STRIDE] = {40, 50, 60};
  sv_root* v3 = sv_push_back_wide(v2, b);

  ASSERT_INT_EQ(sv_count(v1), 0);
  ASSERT_INT_EQ(sv_count(v2), 1);
  ASSERT_INT_EQ(sv_count(v3), 2);

  const int* p = sv_get_ptr(v2, 0);
  ASSERT(p[0] == 10 && p[1] == 20 && p[2] == 30);

  p = sv_get_ptr(v3, 0);
  ASSERT(p[0] == 10 && p[1] == 20 && p[2] == 30);
  p = sv_get_ptr(v3, 1);
  ASSERT(p[0] == 40 && p[1] == 50 && p[2] == 60);

  sv_unref(v1);
  sv_unref(v2);
  sv_unref(v3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: stride=1 via empty_strided behaves same as normal empty */
int test_stride_one_compat(void) {
  printf("  test_stride_one_compat: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(1);
  ASSERT_INT_EQ(sv_stride(v), 1);

  for (int i = 0; i < 50; i++) {
    sv_root* next = sv_push_back(v, i * 10);
    sv_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(sv_count(v), 50);

  for (int i = 0; i < 50; i++) {
    sv_get_result gr = sv_get(v, (uint32_t)i);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, i * 10);
  }

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: large strided vec (>1024 elements, multi-level tree) */
int test_stride_large(void) {
  printf("  test_stride_large: ");
  tracker_reset();

  sv_root* v = sv_empty_strided(STRIDE);
  int N = 2000;
  for (int i = 0; i < N; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    sv_root* next = sv_push_back_wide(v, vals);
    sv_unref(v);
    v = next;
  }
  ASSERT_INT_EQ(sv_count(v), (uint32_t)N);

  /* Spot check */
  ASSERT(check_val(v, 0));
  ASSERT(check_val(v, 31));
  ASSERT(check_val(v, 32));
  ASSERT(check_val(v, 999));
  ASSERT(check_val(v, 1023));
  ASSERT(check_val(v, 1024));
  ASSERT(check_val(v, N - 1));

  sv_unref(v);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  printf("rrb_vec stride tests:\n");
  int pass = 0, fail = 0;

  test_stride_empty()          ? pass++ : fail++;
  test_stride_push_get_small() ? pass++ : fail++;
  test_stride_push_get_large() ? pass++ : fail++;
  test_stride_set_wide()       ? pass++ : fail++;
  test_stride_pop_back()       ? pass++ : fail++;
  test_stride_concat()         ? pass++ : fail++;
  test_stride_slice()          ? pass++ : fail++;
  test_stride_from_array()     ? pass++ : fail++;
  test_stride_to_array()       ? pass++ : fail++;
  test_stride_get_ptr_oob()    ? pass++ : fail++;
  test_stride_set_wide_oob()   ? pass++ : fail++;
  test_stride_persistence()    ? pass++ : fail++;
  test_stride_one_compat()     ? pass++ : fail++;
  test_stride_large()          ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
