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

#define VAL_STRIDE 3
#define KEY_STRIDE 2

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
static void make_val(int key, int out[VAL_STRIDE]) {
  out[0] = key * 100;
  out[1] = key * 100 + 1;
  out[2] = key * 100 + 2;
}

/* Helper: check a wide value via get_ptr */
static int check_val(shamt_node* root, int key) {
  const int* p = shamt_get_ptr(root, key);
  if (!p) return 0;
  int expected[VAL_STRIDE];
  make_val(key, expected);
  return p[0] == expected[0] && p[1] == expected[1] && p[2] == expected[2];
}

/* ===== Value stride tests (key_stride=1) ===== */

/* Test: set_wide and get_ptr for a few entries */
int test_hamt_stride_basic(void) {
  printf("  test_hamt_stride_basic: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 10; i++) {
    int vals[VAL_STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, &i, 1, vals, VAL_STRIDE);
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

  int vals[VAL_STRIDE] = {10, 20, 30};
  int key1 = 1;
  root = shamt_set_wide(NULL, &key1, 1, vals, VAL_STRIDE);

  int new_vals[VAL_STRIDE] = {99, 88, 77};
  shamt_node* root2 = shamt_set_wide(root, &key1, 1, new_vals, VAL_STRIDE);

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
    int vals[VAL_STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, &i, 1, vals, VAL_STRIDE);
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
    int vals[VAL_STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, &i, 1, vals, VAL_STRIDE);
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
    int vals[VAL_STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, &i, 1, vals, VAL_STRIDE);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 5);

  for (int i = 0; i < 5; i++) {
    ASSERT(check_val(root, i));
  }

  /* Update in collision chain */
  int new_vals[VAL_STRIDE] = {777, 888, 999};
  int key2 = 2;
  shamt_node* root2 = shamt_set_wide(root, &key2, 1, new_vals, VAL_STRIDE);
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
    int vals[VAL_STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, &i, 1, vals, VAL_STRIDE);
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
    int expected[VAL_STRIDE];
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
  int vals[VAL_STRIDE] = {1, 2, 3};
  int key42 = 42;
  shamt_node* root = shamt_set_wide(NULL, &key42, 1, vals, VAL_STRIDE);

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
    shamt_node* next = shamt_set_wide(root, &i, 1, &val, 1);
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

/* ===== Key stride tests (key_stride=2) ===== */

/* Helper: build a 2-int wide key */
static void make_wide_key(int id, int wk[KEY_STRIDE]) {
  wk[0] = id;
  wk[1] = id + 1000;
}

/* Test: wide key set/get basic */
int test_hamt_key_stride_basic(void) {
  printf("  test_hamt_key_stride_basic: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 10; i++) {
    int wk[KEY_STRIDE];
    make_wide_key(i, wk);
    int val = i * 10;
    shamt_node* next = shamt_set_wide(root, wk, KEY_STRIDE, &val, 1);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 10);

  /* Lookup by wide key */
  for (int i = 0; i < 10; i++) {
    int wk[KEY_STRIDE];
    make_wide_key(i, wk);
    ASSERT(shamt_has_wide(root, wk, KEY_STRIDE));
    shamt_leaf* leaf = shamt_get_leaf(root, wk, KEY_STRIDE);
    ASSERT(leaf != NULL);
    const int* vp = shamt_value_ptr_from_leaf(leaf);
    ASSERT_INT_EQ(vp[0], i * 10);
  }

  /* Non-existent wide key */
  int missing[KEY_STRIDE] = {999, 1999};
  ASSERT(!shamt_has_wide(root, missing, KEY_STRIDE));

  shamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: wide key update */
int test_hamt_key_stride_update(void) {
  printf("  test_hamt_key_stride_update: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  int wk[KEY_STRIDE] = {5, 1005};
  int val1 = 42;
  shamt_node* root = shamt_set_wide(NULL, wk, KEY_STRIDE, &val1, 1);

  int val2 = 99;
  shamt_node* root2 = shamt_set_wide(root, wk, KEY_STRIDE, &val2, 1);

  /* Original unchanged */
  shamt_leaf* leaf1 = shamt_get_leaf(root, wk, KEY_STRIDE);
  ASSERT(shamt_value_ptr_from_leaf(leaf1)[0] == 42);

  /* Updated */
  shamt_leaf* leaf2 = shamt_get_leaf(root2, wk, KEY_STRIDE);
  ASSERT(shamt_value_ptr_from_leaf(leaf2)[0] == 99);

  shamt_unref(root);
  shamt_unref(root2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: wide key unset */
int test_hamt_key_stride_unset(void) {
  printf("  test_hamt_key_stride_unset: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 10; i++) {
    int wk[KEY_STRIDE];
    make_wide_key(i, wk);
    int val = i;
    shamt_node* next = shamt_set_wide(root, wk, KEY_STRIDE, &val, 1);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 10);

  /* Remove key {3, 1003} */
  int wk_remove[KEY_STRIDE];
  make_wide_key(3, wk_remove);
  shamt_node* root2 = shamt_unset_wide(root, wk_remove, KEY_STRIDE);
  ASSERT_INT_EQ((int)shamt_count(root2), 9);
  ASSERT(!shamt_has_wide(root2, wk_remove, KEY_STRIDE));

  /* Neighbours still present */
  int wk2[KEY_STRIDE], wk4[KEY_STRIDE];
  make_wide_key(2, wk2);
  make_wide_key(4, wk4);
  ASSERT(shamt_has_wide(root2, wk2, KEY_STRIDE));
  ASSERT(shamt_has_wide(root2, wk4, KEY_STRIDE));

  /* Original unchanged */
  ASSERT(shamt_has_wide(root, wk_remove, KEY_STRIDE));

  shamt_unref(root);
  shamt_unref(root2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: wide key + wide value together */
int test_hamt_key_val_stride(void) {
  printf("  test_hamt_key_val_stride: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);
  shamt_node* root = NULL;

  for (int i = 0; i < 20; i++) {
    int wk[KEY_STRIDE];
    make_wide_key(i, wk);
    int vals[VAL_STRIDE];
    make_val(i, vals);
    shamt_node* next = shamt_set_wide(root, wk, KEY_STRIDE, vals, VAL_STRIDE);
    if (root) shamt_unref(root);
    root = next;
  }
  ASSERT_INT_EQ((int)shamt_count(root), 20);

  /* Verify via iteration */
  shamt_iter it = shamt_iter_init(root);
  int count = 0;
  while (1) {
    shamt_iter_result res = shamt_next_leaf(&it);
    if (res.done) break;
    count++;

    const int* kp = shamt_key_ptr_from_leaf(res.item);
    ASSERT(kp != NULL);
    /* key[1] == key[0] + 1000 */
    ASSERT_INT_EQ(kp[1], kp[0] + 1000);

    const int* vp = shamt_value_ptr_from_leaf(res.item);
    ASSERT(vp != NULL);
    /* val[0] == key[0] * 100 */
    ASSERT_INT_EQ(vp[0], kp[0] * 100);
    ASSERT_INT_EQ(vp[1], kp[0] * 100 + 1);
    ASSERT_INT_EQ(vp[2], kp[0] * 100 + 2);
  }
  ASSERT_INT_EQ(count, 20);

  shamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: wide key collision (real FNV-1a collision pair)
 * {54000, 378003} and {60270, 421893} both hash to 0x196d2cad via FNV-1a
 * on their raw bytes. This exercises the collision node path with memcmp
 * key comparison (H_WIDE_KEY_EQ). */
int test_hamt_key_stride_collision(void) {
  printf("  test_hamt_key_stride_collision: ");
  tracker_reset();

  shamt_set_key_handlers(int_hash, int_eq);

  int ka[KEY_STRIDE] = {54000, 378003};
  int kb[KEY_STRIDE] = {60270, 421893};
  int val_a = 11, val_b = 22;

  /* Insert both — should create a collision node */
  shamt_node* r1 = shamt_set_wide(NULL, ka, KEY_STRIDE, &val_a, 1);
  shamt_node* r2 = shamt_set_wide(r1, kb, KEY_STRIDE, &val_b, 1);
  ASSERT_INT_EQ((int)shamt_count(r2), 2);

  /* Both retrievable */
  ASSERT(shamt_has_wide(r2, ka, KEY_STRIDE));
  ASSERT(shamt_has_wide(r2, kb, KEY_STRIDE));
  ASSERT_INT_EQ(shamt_value_ptr_from_leaf(shamt_get_leaf(r2, ka, KEY_STRIDE))[0], 11);
  ASSERT_INT_EQ(shamt_value_ptr_from_leaf(shamt_get_leaf(r2, kb, KEY_STRIDE))[0], 22);

  /* Update one key in the collision chain */
  int val_a2 = 99;
  shamt_node* r3 = shamt_set_wide(r2, ka, KEY_STRIDE, &val_a2, 1);
  ASSERT_INT_EQ(shamt_value_ptr_from_leaf(shamt_get_leaf(r3, ka, KEY_STRIDE))[0], 99);
  ASSERT_INT_EQ(shamt_value_ptr_from_leaf(shamt_get_leaf(r3, kb, KEY_STRIDE))[0], 22);

  /* Original unchanged */
  ASSERT_INT_EQ(shamt_value_ptr_from_leaf(shamt_get_leaf(r2, ka, KEY_STRIDE))[0], 11);

  /* Remove one — collapses collision back to leaf */
  shamt_node* r4 = shamt_unset_wide(r3, kb, KEY_STRIDE);
  ASSERT_INT_EQ((int)shamt_count(r4), 1);
  ASSERT(shamt_has_wide(r4, ka, KEY_STRIDE));
  ASSERT(!shamt_has_wide(r4, kb, KEY_STRIDE));

  shamt_unref(r1);
  shamt_unref(r2);
  shamt_unref(r3);
  shamt_unref(r4);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  printf("hamt stride tests:\n");
  int pass = 0, fail = 0;

  /* Value stride tests (key_stride=1) */
  test_hamt_stride_basic()       ? pass++ : fail++;
  test_hamt_stride_update()      ? pass++ : fail++;
  test_hamt_stride_unset()       ? pass++ : fail++;
  test_hamt_stride_large()       ? pass++ : fail++;
  test_hamt_stride_collision()   ? pass++ : fail++;
  test_hamt_stride_iter()        ? pass++ : fail++;
  test_hamt_stride_has()         ? pass++ : fail++;
  test_hamt_stride_one_compat()  ? pass++ : fail++;

  /* Key stride tests (key_stride=2) */
  test_hamt_key_stride_basic()      ? pass++ : fail++;
  test_hamt_key_stride_update()     ? pass++ : fail++;
  test_hamt_key_stride_unset()      ? pass++ : fail++;
  test_hamt_key_val_stride()        ? pass++ : fail++;
  test_hamt_key_stride_collision()  ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
