#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/test_helpers.h"

#define HAMT_ALLOCATOR                  \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#define HAMT_KEY_T  int
#define HAMT_VAL_T  int
#define HAMT_NAME   ih
#include "hamt.h"

/* --- Hash functions for int keys/values --- */

static uint32_t int_key_hash(int key) {
  uint32_t x = (uint32_t)key;
  x ^= x >> 16;
  x *= 0x45D9F3Bu;
  x ^= x >> 16;
  return x;
}

static bool int_key_eq(int a, int b) {
  return a == b;
}

static uint32_t int_val_hash(int val) {
  uint32_t x = (uint32_t)val;
  x ^= x >> 16;
  x *= 0x85EBCA6Bu;
  x ^= x >> 16;
  return x;
}

/* --- Helper --- */

static ih_node* update_root(ih_node* root, ih_node* new_root) {
  if (root) ih_unref(root);
  return new_root;
}

/* --- Tests --- */

static int test_empty_hash(void) {
  printf("Running test_empty_hash... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  /* NULL root has hash 0 */
  ASSERT(ih_node_hash(NULL) == 0);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_single_leaf_hash(void) {
  printf("Running test_single_leaf_hash... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  ih_node* root = ih_set(NULL, 1, 100);

  /* Root is a leaf — hash should be key_hash(1) ^ val_hash(100) */
  uint32_t expected = int_key_hash(1) ^ int_val_hash(100);
  ASSERT(root->hash == expected);

  ih_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_consistent_insertion_order(void) {
  printf("Running test_hash_consistent_insertion_order... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  /* Build map {1:10, 2:20, 3:30} in order 1,2,3 */
  ih_node* a = NULL;
  a = ih_set(a, 1, 10);
  a = update_root(a, ih_set(a, 2, 20));
  a = update_root(a, ih_set(a, 3, 30));

  /* Build same map in order 3,1,2 */
  ih_node* b = NULL;
  b = ih_set(b, 3, 30);
  b = update_root(b, ih_set(b, 1, 10));
  b = update_root(b, ih_set(b, 2, 20));

  /* XOR is order-independent — structural hashes must match */
  ASSERT(a->hash == b->hash);

  ih_unref(a);
  ih_unref(b);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_changes_on_value_update(void) {
  printf("Running test_hash_changes_on_value_update... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  ih_node* a = NULL;
  a = ih_set(a, 1, 10);
  a = update_root(a, ih_set(a, 2, 20));

  ih_node* b = ih_set(a, 1, 99);  /* change value of key 1 */

  ASSERT(a->hash != b->hash);

  ih_unref(a);
  ih_unref(b);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_changes_on_key_add(void) {
  printf("Running test_hash_changes_on_key_add... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  ih_node* a = ih_set(NULL, 1, 10);
  ih_node* b = ih_set(a, 2, 20);

  ASSERT(a->hash != b->hash);

  ih_unref(a);
  ih_unref(b);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_changes_on_unset(void) {
  printf("Running test_hash_changes_on_unset... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  ih_node* a = NULL;
  a = ih_set(a, 1, 10);
  a = update_root(a, ih_set(a, 2, 20));
  a = update_root(a, ih_set(a, 3, 30));

  ih_node* b = ih_unset(a, 2);

  ASSERT(a->hash != b->hash);

  ih_unref(a);
  ih_unref(b);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_restored_after_unset_set(void) {
  printf("Running test_hash_restored_after_unset_set... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  ih_node* a = NULL;
  a = ih_set(a, 1, 10);
  a = update_root(a, ih_set(a, 2, 20));
  uint32_t original_hash = a->hash;

  /* Remove key 2 then re-add it */
  ih_node* b = ih_unset(a, 2);
  ih_node* c = ih_set(b, 2, 20);

  ASSERT(c->hash == original_hash);

  ih_unref(a);
  ih_unref(b);
  ih_unref(c);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_empty_map_is_zero(void) {
  printf("Running test_hash_empty_map_is_zero... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  /* Empty map = NULL root → hash 0 */
  ASSERT(ih_node_hash(NULL) == 0);

  /* Add then remove → back to NULL */
  ih_node* a = ih_set(NULL, 1, 10);
  ih_node* b = ih_unset(a, 1);

  /* b should be NULL (last element removed) */
  ASSERT(b == NULL);
  ASSERT(ih_node_hash(b) == 0);

  ih_unref(a);
  /* b is NULL, no unref needed */
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_large_map_consistency(void) {
  printf("Running test_hash_large_map_consistency... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  /* Build map with 1000 entries forward */
  ih_node* forward = NULL;
  for (int i = 0; i < 1000; i++) {
    forward = update_root(forward, ih_set(forward, i, i * 10));
  }

  /* Build same map in reverse */
  ih_node* reverse = NULL;
  for (int i = 999; i >= 0; i--) {
    reverse = update_root(reverse, ih_set(reverse, i, i * 10));
  }

  ASSERT(forward->hash == reverse->hash);

  ih_unref(forward);
  ih_unref(reverse);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_no_handler_gives_zero(void) {
  printf("Running test_hash_no_handler_gives_zero... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(NULL, NULL);  /* no structural hash handlers */

  ih_node* root = ih_set(NULL, 1, 10);
  ASSERT(root->hash == 0);

  root = update_root(root, ih_set(root, 2, 20));
  ASSERT(root->hash == 0);

  ih_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_node_hash_accessor(void) {
  printf("Running test_hash_node_hash_accessor... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  ih_node* root = ih_set(NULL, 42, 7);

  /* ih_node_hash should return same as root->hash */
  ASSERT(ih_node_hash(root) == root->hash);
  ASSERT(ih_node_hash(NULL) == 0);

  ih_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Collision tests: forced hash to create collision nodes --- */

static int forced_key_hash_value = 42;

static uint32_t forced_key_hash(int key) {
  (void)key;
  return (uint32_t)forced_key_hash_value;
}

static int test_hash_collision_consistency(void) {
  printf("Running test_hash_collision_consistency... ");
  tracker_reset();
  ih_set_key_handlers(forced_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);
  forced_key_hash_value = 42;

  /* Force collision: all keys get same hash */
  ih_node* a = NULL;
  a = ih_set(a, 1, 10);
  a = update_root(a, ih_set(a, 2, 20));
  a = update_root(a, ih_set(a, 3, 30));

  /* Build same in different order */
  ih_node* b = NULL;
  b = ih_set(b, 3, 30);
  b = update_root(b, ih_set(b, 1, 10));
  b = update_root(b, ih_set(b, 2, 20));

  /* XOR is commutative — collision node hashes should match */
  ASSERT(a->hash == b->hash);

  ih_unref(a);
  ih_unref(b);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_collision_changes_on_unset(void) {
  printf("Running test_hash_collision_changes_on_unset... ");
  tracker_reset();
  ih_set_key_handlers(forced_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);
  forced_key_hash_value = 42;

  ih_node* a = NULL;
  a = ih_set(a, 1, 10);
  a = update_root(a, ih_set(a, 2, 20));
  a = update_root(a, ih_set(a, 3, 30));

  ih_node* b = ih_unset(a, 2);

  ASSERT(a->hash != b->hash);

  ih_unref(a);
  ih_unref(b);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_transient_consistent(void) {
  printf("Running test_hash_transient_consistent... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  /* Build via persistent API */
  ih_node* persistent = NULL;
  for (int i = 0; i < 100; i++) {
    persistent = update_root(persistent, ih_set(persistent, i, i * 10));
  }

  /* Build via transient API */
  ih_transient* t = ih_transient_fn(NULL);
  for (int i = 0; i < 100; i++) {
    ih_transient_set(t, i, i * 10);
  }
  ih_node* from_transient = ih_persistent_fn(t);
  ih_rc_unref(t);

  /* Hashes should match */
  ASSERT(persistent->hash == from_transient->hash);

  ih_unref(persistent);
  ih_unref(from_transient);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hash_transient_unset(void) {
  printf("Running test_hash_transient_unset... ");
  tracker_reset();
  ih_set_key_handlers(int_key_hash, int_key_eq);
  ih_set_hash_handlers(int_key_hash, int_val_hash);

  /* Build {0:0, 1:10, 2:20, ..., 9:90} persistent */
  ih_node* persistent = NULL;
  for (int i = 0; i < 10; i++) {
    persistent = update_root(persistent, ih_set(persistent, i, i * 10));
  }

  /* Remove evens persistent */
  ih_node* p_removed = ih_ref(persistent);
  for (int i = 0; i < 10; i += 2) {
    p_removed = update_root(p_removed, ih_unset(p_removed, i));
  }

  /* Same via transient */
  ih_transient* t = ih_transient_fn(persistent);
  for (int i = 0; i < 10; i += 2) {
    ih_transient_unset(t, i);
  }
  ih_node* t_removed = ih_persistent_fn(t);
  ih_rc_unref(t);

  ASSERT(p_removed->hash == t_removed->hash);

  ih_unref(persistent);
  ih_unref(p_removed);
  ih_unref(t_removed);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */

int main(void) {
  int passed = 0;
  int failed = 0;

  typedef int (*test_fn)(void);
  struct { const char* name; test_fn fn; } tests[] = {
    {"test_empty_hash",                   test_empty_hash},
    {"test_single_leaf_hash",             test_single_leaf_hash},
    {"test_hash_consistent_insertion_order", test_hash_consistent_insertion_order},
    {"test_hash_changes_on_value_update", test_hash_changes_on_value_update},
    {"test_hash_changes_on_key_add",      test_hash_changes_on_key_add},
    {"test_hash_changes_on_unset",        test_hash_changes_on_unset},
    {"test_hash_restored_after_unset_set", test_hash_restored_after_unset_set},
    {"test_hash_empty_map_is_zero",       test_hash_empty_map_is_zero},
    {"test_hash_large_map_consistency",   test_hash_large_map_consistency},
    {"test_hash_no_handler_gives_zero",   test_hash_no_handler_gives_zero},
    {"test_hash_node_hash_accessor",      test_hash_node_hash_accessor},
    {"test_hash_collision_consistency",   test_hash_collision_consistency},
    {"test_hash_collision_changes_on_unset", test_hash_collision_changes_on_unset},
    {"test_hash_transient_consistent",    test_hash_transient_consistent},
    {"test_hash_transient_unset",         test_hash_transient_unset},
  };
  int num_tests = (int)(sizeof(tests) / sizeof(tests[0]));

  for (int i = 0; i < num_tests; i++) {
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n=== Test Summary ===\n");
  printf("Passed: %d\n", passed);
  printf("Failed: %d\n", failed);

  return failed > 0 ? 1 : 0;
}
