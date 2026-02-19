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

#define HAMT_KEY_T  const char*
#define HAMT_VAL_T  void*
#define HAMT_NAME   hamt
#include "hamt.h"

/* --- Key Helpers --- */

char* my_strdup(const char* s) {
  size_t len = strlen(s) + 1;
  char*  d   = tracked_malloc(len);
  memcpy(d, s, len);
  return d;
}

void test_key_destroy(const char* key, void* val) {
  (void)val;
  tracked_free((void*)key);
}

/* Typed wrappers for hash/eq that match HAMT_KEY_T = const char* */
static uint32_t str_hash(const char* key) {
  return string_hash(key);
}

static bool str_eq(const char* a, const char* b) {
  return string_eq(a, b);
}

/* --- Debug Visualization --- */

void print_indent(int level) {
  for (int i = 0; i < level; i++) printf("  ");
}

void hamt_print_rec(hamt_node* node, int level) {
  if (!node) {
    print_indent(level);
    printf("NULL\n");
    return;
  }

  print_indent(level);

  switch (node->type) {
    case hamt_NODE_LEAF: {
      hamt_leaf* leaf = (hamt_leaf*)node;
      printf("Leaf: \"%s\" (Hash: %u, Refs: %ld)\n", leaf->key, leaf->hash, hamt_ref_count(leaf));
      break;
    }
    case hamt_NODE_COLLISION: {
      hamt_collision* col = (hamt_collision*)node;
      printf("Collision: Count %u (Hash: %u, Refs: %ld)\n", col->count, col->hash, hamt_ref_count(node));
      for (uint32_t i = 0; i < col->count; i++) {
        hamt_print_rec((hamt_node*)col->items[i], level + 1);
      }
      break;
    }
    case hamt_NODE_INTERNAL: {
      hamt_internal* in    = (hamt_internal*)node;
      int            count = 0;
      uint32_t       v     = in->bitmap;
      while (v) {
        count++;
        v &= v - 1;
      }

      printf("Internal: Bitmap %08X (Count: %d, Refs: %ld)\n", in->bitmap, count, hamt_ref_count(node));
      for (int i = 0; i < count; i++) {
        hamt_print_rec(in->children[i], level + 1);
      }
      break;
    }
  }
}

void hamt_print(hamt_node* root) {
  printf("--- HAMT Dump ---\n");
  hamt_print_rec(root, 0);
  printf("-----------------\n");
}

/* --- Tests --- */

hamt_node* update_root(hamt_node* root, hamt_node* new_root) {
  if (root) hamt_unref(root);
  return new_root;
}

int test_basic_put_get() {
  printf("Running test_basic_put_get... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;

  root = hamt_set(root, my_strdup("apple"), (void*)1);
  root = update_root(root, hamt_set(root, my_strdup("banana"), (void*)2));

  ASSERT(hamt_get(root, "apple") == (void*)1);
  ASSERT(hamt_get(root, "banana") == (void*)2);
  ASSERT(hamt_get(root, "cherry") == NULL);

  ASSERT(hamt_count(root) == 2);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_persistence() {
  printf("Running test_persistence... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* t0 = NULL;
  hamt_node* t1 = hamt_set(t0, my_strdup("A"), (void*)10);
  hamt_node* t2 = hamt_set(t1, my_strdup("B"), (void*)20);

  ASSERT(hamt_get(t1, "A") == (void*)10);
  ASSERT(hamt_get(t1, "B") == NULL);
  ASSERT(hamt_count(t1) == 1);

  ASSERT(hamt_get(t2, "A") == (void*)10);
  ASSERT(hamt_get(t2, "B") == (void*)20);
  ASSERT(hamt_count(t2) == 2);

  hamt_unref(t2);

  ASSERT(hamt_get(t1, "A") == (void*)10);
  ASSERT(hamt_get(t1, "B") == NULL);

  hamt_unref(t1);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_update_immutability() {
  printf("Running test_update_immutability... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* t1 = hamt_set(NULL, my_strdup("key"), (void*)100);
  hamt_node* t2 = hamt_set(t1, my_strdup("key"), (void*)200);

  ASSERT(hamt_get(t1, "key") == (void*)100);
  ASSERT(hamt_get(t2, "key") == (void*)200);

  hamt_unref(t1);
  hamt_unref(t2);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_removal() {
  printf("Running test_removal... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* t1 = hamt_set(NULL, my_strdup("A"), (void*)1);
  t1            = update_root(t1, hamt_set(t1, my_strdup("B"), (void*)2));
  t1            = update_root(t1, hamt_set(t1, my_strdup("C"), (void*)3));

  hamt_node* t2 = hamt_unset(t1, "B");

  ASSERT(hamt_get(t1, "B") == (void*)2);
  ASSERT(hamt_count(t1) == 3);

  ASSERT(hamt_get(t2, "B") == NULL);
  ASSERT(hamt_get(t2, "A") == (void*)1);
  ASSERT(hamt_get(t2, "C") == (void*)3);
  ASSERT(hamt_count(t2) == 2);

  hamt_unref(t1);
  hamt_unref(t2);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_large_scale() {
  printf("Running test_large_scale... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;
  char       buffer[32];
  int        count = 10000;

  for (int i = 0; i < count; i++) {
    snprintf(buffer, sizeof(buffer), "key%d", i);
    root = update_root(root, hamt_set(root, my_strdup(buffer), (void*)(intptr_t)i));
  }

  ASSERT(hamt_count(root) == (size_t)count);

  for (int i = 0; i < count; i++) {
    snprintf(buffer, sizeof(buffer), "key%d", i);
    void* val = hamt_get(root, buffer);
    ASSERT((intptr_t)val == i);
  }

  snprintf(buffer, sizeof(buffer), "key%d", count + 1);
  ASSERT(hamt_get(root, buffer) == NULL);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_memory_correctness_grind() {
  printf("Running test_memory_correctness_grind... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;
  char       key[16];
  int        count = 1000;

  for (int i = 0; i < count; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    root = update_root(root, hamt_set(root, my_strdup(key), (void*)(intptr_t)i));
  }

  for (int i = 0; i < count / 2; i++) {
    snprintf(key, sizeof(key), "k%d", i * 2);
    root = update_root(root, hamt_unset(root, key));
  }

  ASSERT(hamt_count(root) == (size_t)(count / 2));

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_structural_sharing_memory() {
  printf("Running test_structural_sharing_memory... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* versions[10];
  int        count = 10;

  versions[0] = NULL;
  for (int i = 1; i < count; i++) {
    char key[16];
    snprintf(key, sizeof(key), "k%d", i);
    versions[i] = hamt_set(versions[i - 1], my_strdup(key), (void*)(intptr_t)i);
  }

  for (int i = 1; i < count; i++) {
    char key[16];
    snprintf(key, sizeof(key), "k%d", i);
    ASSERT(hamt_get(versions[i], key) == (void*)(intptr_t)i);
  }

  for (int i = count - 1; i >= 1; i--) {
    hamt_unref(versions[i]);
  }

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_leaf_update_memory_leak() {
  printf("Running test_leaf_update_memory_leak... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* a = hamt_set(NULL, my_strdup("key"), (void*)1);
  hamt_node* b = hamt_set(a, my_strdup("key"), (void*)2);

  ASSERT(hamt_get(a, "key") == (void*)1);
  ASSERT(hamt_get(b, "key") == (void*)2);

  hamt_unref(a);
  hamt_unref(b);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_remove_nonexistent_key_no_leak() {
  printf("Running test_remove_nonexistent_key_no_leak... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* a = hamt_set(NULL, my_strdup("key"), (void*)1);
  hamt_node* b = hamt_unset(a, "nonexistent");

  ASSERT(a == b);

  hamt_unref(a);
  hamt_unref(b);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Collision tests use forced hash --- */

static uint32_t forced_hash_value = 42;

static uint32_t forced_hash(const char* key) {
  (void)key;
  return forced_hash_value;
}

int test_collision_node_degradation_leak() {
  printf("Running test_collision_node_degradation_leak... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(forced_hash, str_eq);

  forced_hash_value = 42;

  hamt_node* a = NULL;
  a            = hamt_set(a, my_strdup("k1"), (void*)1);
  a            = update_root(a, hamt_set(a, my_strdup("k2"), (void*)2));
  a            = update_root(a, hamt_set(a, my_strdup("k3"), (void*)3));

  ASSERT(hamt_count(a) == 3);

  hamt_node* b = hamt_unset(a, "k2");
  ASSERT(hamt_count(b) == 2);

  hamt_node* c = hamt_unset(b, "k3");
  ASSERT(hamt_count(c) == 1);

  hamt_unref(a);
  hamt_unref(b);
  hamt_unref(c);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_merge_nodes_ref_counting() {
  printf("Running test_merge_nodes_ref_counting... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* a = hamt_set(NULL, my_strdup("shared"), (void*)1);
  hamt_node* b = hamt_set(a, my_strdup("branch1"), (void*)2);
  hamt_node* c = hamt_set(a, my_strdup("branch2"), (void*)3);

  hamt_unref(a);

  ASSERT(hamt_get(b, "shared") == (void*)1);
  ASSERT(hamt_get(c, "shared") == (void*)1);

  hamt_unref(b);
  hamt_unref(c);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_collision_with_forced_hash() {
  printf("Running test_collision_with_forced_hash... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(forced_hash, str_eq);

  forced_hash_value = 999;

  hamt_node* root = NULL;
  root            = hamt_set(root, my_strdup("alpha"), (void*)1);
  root            = update_root(root, hamt_set(root, my_strdup("beta"), (void*)2));
  root            = update_root(root, hamt_set(root, my_strdup("gamma"), (void*)3));

  ASSERT(hamt_count(root) == 3);
  ASSERT(hamt_get(root, "alpha") == (void*)1);
  ASSERT(hamt_get(root, "beta") == (void*)2);
  ASSERT(hamt_get(root, "gamma") == (void*)3);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_repeated_updates_same_key() {
  printf("Running test_repeated_updates_same_key... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;
  for (int i = 0; i < 100; i++) {
    hamt_node* next = hamt_set(root, my_strdup("same_key"), (void*)(intptr_t)i);
    if (root) hamt_unref(root);
    root = next;
  }

  ASSERT(hamt_get(root, "same_key") == (void*)99);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_remove_then_re_add_same_key() {
  printf("Running test_remove_then_re_add_same_key... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = hamt_set(NULL, my_strdup("key"), (void*)1);
  root            = update_root(root, hamt_unset(root, "key"));
  ASSERT(hamt_get(root, "key") == NULL);

  root = update_root(root, hamt_set(root, my_strdup("key"), (void*)2));
  ASSERT(hamt_get(root, "key") == (void*)2);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_deep_path_copy_persistence() {
  printf("Running test_deep_path_copy_persistence... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;
  for (int i = 0; i < 1000; i++) {
    char key[16];
    snprintf(key, sizeof(key), "key_%d", i);
    root = update_root(root, hamt_set(root, my_strdup(key), (void*)(intptr_t)i));
  }

  hamt_node* copy = hamt_set(root, my_strdup("new_key"), (void*)9999);

  ASSERT(hamt_get(copy, "new_key") == (void*)9999);
  ASSERT(hamt_get(root, "new_key") == NULL);

  hamt_unref(root);
  hamt_unref(copy);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_remove_from_collision_chain() {
  printf("Running test_remove_from_collision_chain... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(forced_hash, str_eq);

  forced_hash_value = 12345;

  hamt_node* root = NULL;
  root            = hamt_set(root, my_strdup("a"), (void*)1);
  root            = update_root(root, hamt_set(root, my_strdup("b"), (void*)2));
  root            = update_root(root, hamt_set(root, my_strdup("c"), (void*)3));
  root            = update_root(root, hamt_set(root, my_strdup("d"), (void*)4));

  ASSERT(hamt_count(root) == 4);

  root = update_root(root, hamt_unset(root, "b"));
  ASSERT(hamt_count(root) == 3);
  ASSERT(hamt_get(root, "b") == NULL);

  root = update_root(root, hamt_unset(root, "d"));
  ASSERT(hamt_count(root) == 2);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_mixed_operations_stress() {
  printf("Running test_mixed_operations_stress... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;
  char       key[32];

  for (int round = 0; round < 5; round++) {
    for (int i = 0; i < 200; i++) {
      snprintf(key, sizeof(key), "round%d_key%d", round, i);
      root = update_root(root, hamt_set(root, my_strdup(key), (void*)(intptr_t)(round * 1000 + i)));
    }

    for (int i = 0; i < 100; i++) {
      snprintf(key, sizeof(key), "round%d_key%d", round, i);
      root = update_root(root, hamt_unset(root, key));
    }
  }

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_hamt_has() {
  printf("Running test_hamt_has... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;
  root            = hamt_set(root, my_strdup("exists"), (void*)1);

  ASSERT(hamt_has(root, "exists") == true);
  ASSERT(hamt_has(root, "nope") == false);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_get_or_default() {
  printf("Running test_get_or_default... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* root = NULL;
  root            = hamt_set(root, my_strdup("key"), (void*)42);

  ASSERT(hamt_get_or_default(root, "key", (void*)999) == (void*)42);
  ASSERT(hamt_get_or_default(root, "missing", (void*)999) == (void*)999);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_nested_hamt_values() {
  printf("Running test_nested_hamt_values... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node* inner = hamt_set(NULL, my_strdup("inner_key"), (void*)123);
  hamt_node* outer = hamt_set(NULL, my_strdup("outer_key"), inner);

  hamt_node* retrieved = (hamt_node*)hamt_get(outer, "outer_key");
  ASSERT(retrieved == inner);
  ASSERT(hamt_get(retrieved, "inner_key") == (void*)123);

  hamt_unref(outer);
  hamt_unref(inner);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

static uint32_t ptr_hash(const char* key) {
  return (uint32_t)(uintptr_t)key;
}

static bool ptr_eq(const char* a, const char* b) {
  return a == b;
}

int test_hamt_as_keys() {
  printf("Running test_hamt_as_keys... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, NULL);
  hamt_set_key_handlers(ptr_hash, ptr_eq);

  hamt_node* key1 = hamt_set(NULL, my_strdup("a"), (void*)1);
  hamt_node* key2 = hamt_set(NULL, my_strdup("b"), (void*)2);

  hamt_node* map = NULL;
  map            = hamt_set(map, (const char*)key1, (void*)100);
  map            = update_root(map, hamt_set(map, (const char*)key2, (void*)200));

  ASSERT(hamt_get(map, (const char*)key1) == (void*)100);
  ASSERT(hamt_get(map, (const char*)key2) == (void*)200);

  hamt_unref(map);

  /* Need to clean up inner keys */
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_unref(key1);
  hamt_unref(key2);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Integer key tests --- */

static uint32_t int_ptr_hash(const char* key) {
  return (uint32_t)(intptr_t)key;
}

static bool int_ptr_eq(const char* a, const char* b) {
  return a == b;
}

int test_generic_keys_integer() {
  printf("Running test_generic_keys_integer... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, NULL);
  hamt_set_key_handlers(int_ptr_hash, int_ptr_eq);

  hamt_node* root = NULL;
  for (long i = 0; i < 100; i++) {
    root = update_root(root, hamt_set(root, (const char*)(intptr_t)i, (void*)(i * 10)));
  }

  ASSERT(hamt_count(root) == 100);

  for (long i = 0; i < 100; i++) {
    void* val = hamt_get(root, (const char*)(intptr_t)i);
    ASSERT((intptr_t)val == i * 10);
  }

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_overwrite_keys() {
  printf("Running test_overwrite_keys... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, NULL);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node *a = NULL, *b = NULL;
  char*      key = my_strdup("k0");

  a = hamt_set(a, key, (void*)1);
  b = hamt_set(a, key, (void*)2);

  ASSERT(hamt_get(a, key) == (void*)1);
  ASSERT(hamt_get(b, key) == (void*)2);

  hamt_unref(a);
  hamt_unref(b);

  tracked_free(key);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_setting_same_key_value_pair_multiple_times() {
  printf("Running test_setting_same_key_value_pair_multiple_times... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, NULL);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node *a = NULL, *b = NULL;
  char*      key = my_strdup("k0");

  a = hamt_set(a, key, (void*)1);
  b = hamt_set(a, key, (void*)1);

  hamt_unref(a);
  hamt_unref(b);

  tracked_free(key);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_removing_same_key_multiple_times() {
  printf("Running test_removing_same_key_multiple_times... ");

  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, NULL);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_node *a = NULL, *b = NULL, *c = NULL;
  char*      key = my_strdup("k0");

  a = hamt_set(a, key, (void*)1);
  b = hamt_unset(a, key);
  c = hamt_unset(b, key);

  ASSERT(b == c);

  hamt_unref(a);
  hamt_unref(b);
  hamt_unref(c);

  tracked_free(key);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_hamt_next_key() {
  printf("Running test_hamt_next_key... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_iter        it     = hamt_iter_init(NULL);
  hamt_iter_result result = hamt_next_key(&it);
  ASSERT(result.done == true);
  ASSERT(result.item == NULL);

  hamt_node* root = hamt_set(NULL, my_strdup("only"), (void*)1);
  it              = hamt_iter_init(root);
  result          = hamt_next_key(&it);
  ASSERT(result.done == false);
  ASSERT(strcmp(hamt_key_from_leaf(result.item), "only") == 0);
  result = hamt_next_key(&it);
  ASSERT(result.done == true);
  hamt_unref(root);

  root = NULL;
  root = hamt_set(root, my_strdup("apple"), (void*)1);
  root = update_root(root, hamt_set(root, my_strdup("banana"), (void*)2));
  root = update_root(root, hamt_set(root, my_strdup("cherry"), (void*)3));
  root = update_root(root, hamt_set(root, my_strdup("date"), (void*)4));

  bool found_apple = false, found_banana = false, found_cherry = false, found_date = false;
  int  count = 0;

  it = hamt_iter_init(root);
  while (true) {
    result = hamt_next_key(&it);
    if (result.done) break;
    count++;
    const char* key = hamt_key_from_leaf(result.item);
    if (strcmp(key, "apple") == 0)
      found_apple = true;
    else if (strcmp(key, "banana") == 0)
      found_banana = true;
    else if (strcmp(key, "cherry") == 0)
      found_cherry = true;
    else if (strcmp(key, "date") == 0)
      found_date = true;
  }

  ASSERT(count == 4);
  ASSERT(found_apple);
  ASSERT(found_banana);
  ASSERT(found_cherry);
  ASSERT(found_date);

  hamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_hamt_next_value() {
  printf("Running test_hamt_next_value... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_set_key_handlers(str_hash, str_eq);

  hamt_iter        it     = hamt_iter_init(NULL);
  hamt_iter_result result = hamt_next_value(&it);
  ASSERT(result.done == true);
  ASSERT(result.item == NULL);

  hamt_node* root = hamt_set(NULL, my_strdup("only"), (void*)42);
  it              = hamt_iter_init(root);
  result          = hamt_next_value(&it);
  ASSERT(result.done == false);
  ASSERT(hamt_value_from_leaf(result.item) == (void*)42);
  result = hamt_next_value(&it);
  ASSERT(result.done == true);
  hamt_unref(root);

  root = NULL;
  root = hamt_set(root, my_strdup("a"), (void*)10);
  root = update_root(root, hamt_set(root, my_strdup("b"), (void*)20));
  root = update_root(root, hamt_set(root, my_strdup("c"), (void*)30));
  root = update_root(root, hamt_set(root, my_strdup("d"), (void*)40));

  bool found_10 = false, found_20 = false, found_30 = false, found_40 = false;
  int  count = 0;

  it = hamt_iter_init(root);
  while (true) {
    result = hamt_next_value(&it);
    if (result.done) break;
    count++;
    intptr_t val = (intptr_t)hamt_value_from_leaf(result.item);
    if (val == 10)
      found_10 = true;
    else if (val == 20)
      found_20 = true;
    else if (val == 30)
      found_30 = true;
    else if (val == 40)
      found_40 = true;
  }

  ASSERT(count == 4);
  ASSERT(found_10);
  ASSERT(found_20);
  ASSERT(found_30);
  ASSERT(found_40);

  hamt_unref(root);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_hamt_iter_many() {
  printf("Running test_hamt_iter_many... ");
  tracker_reset();
  hamt_set_lifecycle_hooks(NULL, NULL);
  hamt_set_key_handlers(int_ptr_hash, int_ptr_eq);

  hamt_node* root = NULL;

  int count = 1000;

  for (long i = 0; i < count; i++) {
    root = update_root(root, hamt_set(root, (const char*)(intptr_t)i, (void*)i));
  }

  ASSERT(hamt_count(root) == (size_t)count);

  int sum  = 0;
  int seen = 0;

  hamt_iter        it = hamt_iter_init(root);
  hamt_iter_result result;
  while (true) {
    result = hamt_next_leaf(&it);
    if (result.done) break;
    sum += (int)(intptr_t)hamt_key_from_leaf(result.item) - (int)(intptr_t)hamt_value_from_leaf(result.item);
    seen++;
  }

  ASSERT(sum == 0);
  ASSERT(seen == count);

  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main() {
  int passed = 0;
  int failed = 0;

#define RUN_TEST(func)                                  \
  do {                                                  \
    if (func()) {                                       \
      passed++;                                         \
      hamt_set_lifecycle_hooks(NULL, test_key_destroy); \
    } else {                                            \
      failed++;                                         \
    }                                                   \
  } while (0)

  RUN_TEST(test_basic_put_get);
  RUN_TEST(test_persistence);
  RUN_TEST(test_update_immutability);
  RUN_TEST(test_removal);
  RUN_TEST(test_large_scale);

  RUN_TEST(test_memory_correctness_grind);
  RUN_TEST(test_structural_sharing_memory);

  RUN_TEST(test_leaf_update_memory_leak);
  RUN_TEST(test_remove_nonexistent_key_no_leak);
  RUN_TEST(test_collision_node_degradation_leak);
  RUN_TEST(test_merge_nodes_ref_counting);
  RUN_TEST(test_collision_with_forced_hash);
  RUN_TEST(test_repeated_updates_same_key);
  RUN_TEST(test_remove_then_re_add_same_key);
  RUN_TEST(test_deep_path_copy_persistence);
  RUN_TEST(test_remove_from_collision_chain);
  RUN_TEST(test_mixed_operations_stress);
  RUN_TEST(test_hamt_has);
  RUN_TEST(test_get_or_default);

  RUN_TEST(test_nested_hamt_values);
  RUN_TEST(test_hamt_as_keys);

  RUN_TEST(test_generic_keys_integer);

  RUN_TEST(test_overwrite_keys);
  RUN_TEST(test_setting_same_key_value_pair_multiple_times);
  RUN_TEST(test_removing_same_key_multiple_times);

  RUN_TEST(test_hamt_next_key);
  RUN_TEST(test_hamt_next_value);
  RUN_TEST(test_hamt_iter_many);

  printf("\n=== Test Summary ===\n");
  printf("Passed: %d\n", passed);
  printf("Failed: %d\n", failed);

  return failed > 0 ? 1 : 0;
}
