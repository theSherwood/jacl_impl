#include <stdio.h>
#include <string.h>

#include "../../test/test_helpers.h"

#define HAMT_ALLOCATOR                  \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#define HAMT_KEY_T  int
#define HAMT_VAL_T  int
#define HAMT_NAME   shamt
#include "hamt.h"

#define STRIDE 3

static uint32_t int_hash(int key) {
  uint32_t k = (uint32_t)key;
  k ^= k >> 16;
  k *= 0x45d9f3b;
  k ^= k >> 16;
  return k;
}

static bool int_eq(int a, int b) {
  return a == b;
}

/* Force collisions: all keys hash to the same value */
static uint32_t collision_hash(int key) {
  (void)key;
  return 42;
}

/* Helper: build a wide value from a key */
static void make_val(int key, int out[STRIDE]) {
  out[0] = key * 100;
  out[1] = key * 100 + 1;
  out[2] = key * 100 + 2;
}

/* Helper: check a wide value via get_ptr */
static int check_val(shamt_node* root, int key) {
  const int* p = shamt_get_ptr(root, key);
  if (!p) return 0;
  int expected[STRIDE];
  make_val(key, expected);
  return p[0] == expected[0] && p[1] == expected[1] && p[2] == expected[2];
}

/* Test: set_wide and get_ptr for a few entries */
int test_hamt_stride_basic(void) {
  printf("  test_hamt_stride_basic: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 10; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, i, vals, STRIDE);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 10);

  for (int i = 0; i < 10; i++) {
    ASSERT(check_val(root, i));
  }

  /* Key not present returns NULL */
  ASSERT(shamt_get_ptr(root, 999) == NULL);

  shamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: update existing key preserves stride */
int test_hamt_stride_update(void) {
  printf("  test_hamt_stride_update: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  int vals[STRIDE] = {10, 20, 30};
  root = shamt_set_wide(NULL, 1, vals, STRIDE);

  int new_vals[STRIDE] = {99, 88, 77};
  shamt_node* root2 = shamt_set_wide(root, 1, new_vals, STRIDE);

  /* Original unchanged */
  const int* p1 = shamt_get_ptr(root, 1);
  ASSERT(p1[0] == 10 && p1[1] == 20 && p1[2] == 30);

  /* Updated */
  const int* p2 = shamt_get_ptr(root2, 1);
  ASSERT(p2[0] == 99 && p2[1] == 88 && p2[2] == 77);

  shamt_unref(root);
  shamt_unref(root2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: unset works on strided map */
int test_hamt_stride_unset(void) {
  printf("  test_hamt_stride_unset: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 20; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, i, vals, STRIDE);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 20);

  /* Remove key 5 */
  shamt_node* root2 = shamt_unset(root, 5);
  ASSERT_INT_EQ((int)shamt_count(root2), 19);
  ASSERT(shamt_get_ptr(root2, 5) == NULL);
  ASSERT(check_val(root2, 4));
  ASSERT(check_val(root2, 6));

  /* Original unchanged */
  ASSERT(check_val(root, 5));

  shamt_unref(root);
  shamt_unref(root2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: large scale — many entries */
int test_hamt_stride_large(void) {
  printf("  test_hamt_stride_large: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  int N = 1000;
  for (int i = 0; i < N; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, i, vals, STRIDE);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), N);

  /* Spot check */
  ASSERT(check_val(root, 0));
  ASSERT(check_val(root, 500));
  ASSERT(check_val(root, N - 1));

  shamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: collision handling with strided values */
int test_hamt_stride_collision(void) {
  printf("  test_hamt_stride_collision: ");
  tracker_reset();

  /* Use collision hash — all keys hash to 42 */
  shamt_set_key_handlers(collision_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 5; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, i, vals, STRIDE);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 5);

  for (int i = 0; i < 5; i++) {
    ASSERT(check_val(root, i));
  }

  /* Update in collision chain */
  int new_vals[STRIDE] = {777, 888, 999};
  shamt_node* root2 = shamt_set_wide(root, 2, new_vals, STRIDE);
  const int* p = shamt_get_ptr(root2, 2);
  ASSERT(p[0] == 777 && p[1] == 888 && p[2] == 999);

  /* Remove from collision chain */
  shamt_node* root3 = shamt_unset(root2, 3);
  ASSERT_INT_EQ((int)shamt_count(root3), 4);
  ASSERT(shamt_get_ptr(root3, 3) == NULL);
  ASSERT(check_val(root3, 0));

  shamt_unref(root);
  shamt_unref(root2);
  shamt_unref(root3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: iterator on strided map returns leaves with value_ptr */
int test_hamt_stride_iter(void) {
  printf("  test_hamt_stride_iter: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 20; i++) {
    int vals[STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, i, vals, STRIDE);
    if (root) shamt_unref(root);
    root = next;
  }

  /* Iterate and verify each leaf via value_ptr_from_leaf */
  int seen[20] = {0};
  shamt_iter it = shamt_iter_init(root);
  int count = 0;
  while (1) {
    shamt_iter_result res = shamt_next_leaf(&it);
    if (res.done) break;
    count++;
    int key = shamt_key_from_leaf(res.item);
    ASSERT(key >= 0 && key < 20);
    const int* vp = shamt_value_ptr_from_leaf(res.item);
    ASSERT(vp != NULL);
    int expected[STRIDE];
    make_val(key, expected);
    ASSERT(vp[0] == expected[0] && vp[1] == expected[1] && vp[2] == expected[2]);
    seen[key] = 1;
  }
  ASSERT_INT_EQ(count, 20);
  for (int i = 0; i < 20; i++) {
    ASSERT(seen[i] == 1);
  }

  shamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: has() works on strided map */
int test_hamt_stride_has(void) {
  printf("  test_hamt_stride_has: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  int vals[STRIDE] = {1, 2, 3};
  shamt_node* root = shamt_set_wide(NULL, 42, vals, STRIDE);

  ASSERT(shamt_has(root, 42));
  ASSERT(!shamt_has(root, 99));

  shamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: stride=1 via set_wide behaves same as normal set */
int test_hamt_stride_one_compat(void) {
  printf("  test_hamt_stride_one_compat: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 50; i++) {
    int val = i * 10;
    shamt_node* next = shamt_set_wide(root, i, &val, 1);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 50);

  for (int i = 0; i < 50; i++) {
    int v = shamt_get(root, i);
    ASSERT_INT_EQ(v, i * 10);
  }

  shamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  printf("hamt stride tests:\n");
  int pass = 0, fail = 0;

  test_hamt_stride_basic()       ? pass++ : fail++;
  test_hamt_stride_update()      ? pass++ : fail++;
  test_hamt_stride_unset()       ? pass++ : fail++;
  test_hamt_stride_large()       ? pass++ : fail++;
  test_hamt_stride_collision()   ? pass++ : fail++;
  test_hamt_stride_iter()        ? pass++ : fail++;
  test_hamt_stride_has()         ? pass++ : fail++;
  test_hamt_stride_one_compat()  ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
