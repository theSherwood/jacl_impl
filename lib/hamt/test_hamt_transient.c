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

static uint32_t str_hash(const char* key) {
  return string_hash(key);
}

static bool str_eq(const char* a, const char* b) {
  return string_eq(a, b);
}

/* --- Forced hash for collision testing --- */

static uint32_t forced_hash_value = 42;

static uint32_t forced_hash(const char* key) {
  (void)key;
  return forced_hash_value;
}

/* --- Lifecycle hook tracking --- */

static int create_count = 0;
static int destroy_count = 0;

void counting_create(const char* key, void* val) {
  (void)key;
  (void)val;
  create_count++;
}

void counting_destroy(const char* key, void* val) {
  (void)val;
  destroy_count++;
  tracked_free((void*)key);
}

/* --- Tests --- */

hamt_node* update_root(hamt_node* root, hamt_node* new_root) {
  if (root) hamt_unref(root);
  return new_root;
}

/* Test: create transient from NULL root (empty HAMT), persist immediately */
int test_transient_empty(void) {
  printf("  test_transient_empty: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);
  ASSERT(t != NULL);

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT_PTR_EQ(root, NULL);

  /* Clean up transient */
  hamt_unref((hamt_node*)t);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: create transient from non-empty HAMT (100 entries), persist, verify */
int test_transient_roundtrip(void) {
  printf("  test_transient_roundtrip: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  /* Build a persistent HAMT with 100 entries */
  hamt_node* root = NULL;
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", i);
    root = update_root(root, hamt_set(root, my_strdup(buf), (void*)(intptr_t)(i + 1)));
  }
  ASSERT_INT_EQ((int)hamt_count(root), 100);

  /* Create transient and persist immediately */
  hamt_transient* t = hamt_transient_fn(root);
  ASSERT(t != NULL);

  hamt_node* new_root = hamt_persistent_fn(t);
  ASSERT(new_root != NULL);

  /* Verify all 100 entries */
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", i);
    ASSERT(hamt_has(new_root, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(new_root, buf), i + 1);
  }
  ASSERT_INT_EQ((int)hamt_count(new_root), 100);

  /* Clean up */
  hamt_unref((hamt_node*)t);
  hamt_unref(new_root);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: source persistent root unchanged after transient creation */
int test_transient_source_unchanged(void) {
  printf("  test_transient_source_unchanged: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  /* Build persistent HAMT */
  hamt_node* root = NULL;
  for (int i = 0; i < 50; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "src_%d", i);
    root = update_root(root, hamt_set(root, my_strdup(buf), (void*)(intptr_t)(i + 100)));
  }

  /* Create transient */
  hamt_transient* t = hamt_transient_fn(root);

  /* Verify source root is still intact */
  for (int i = 0; i < 50; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "src_%d", i);
    ASSERT(hamt_has(root, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, buf), i + 100);
  }
  ASSERT_INT_EQ((int)hamt_count(root), 50);

  /* Persist and clean up */
  hamt_node* new_root = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  hamt_unref(new_root);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mutating an invalidated (already persisted) transient returns NULL */
int test_transient_double_persist(void) {
  printf("  test_transient_double_persist: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_node* root = NULL;
  root = update_root(root, hamt_set(root, my_strdup("hello"), (void*)1));

  hamt_transient* t = hamt_transient_fn(root);
  hamt_node* r1 = hamt_persistent_fn(t);
  ASSERT(r1 != NULL);

  /* Second persist should return NULL */
  hamt_node* r2 = hamt_persistent_fn(t);
  ASSERT_PTR_EQ(r2, NULL);

  hamt_unref((hamt_node*)t);
  hamt_unref(r1);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- US-002: Transient set tests --- */

/* Test: insert 10000 key-value pairs via transient, persist, verify all via get */
int test_transient_set_10000(void) {
  printf("  test_transient_set_10000: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);
  ASSERT(t != NULL);

  for (int i = 0; i < 10000; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "tkey_%d", i);
    hamt_transient* r = hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
    ASSERT_PTR_EQ(r, t);
  }

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT(root != NULL);
  ASSERT_INT_EQ((int)hamt_count(root), 10000);

  for (int i = 0; i < 10000; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "tkey_%d", i);
    ASSERT(hamt_has(root, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, buf), i + 1);
  }

  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: update existing keys — insert 100, then update all 100 values */
int test_transient_set_update(void) {
  printf("  test_transient_set_update: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);

  /* Insert 100 entries */
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ukey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }

  /* Update all 100 entries with new values */
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ukey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1000));
  }

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT(root != NULL);
  ASSERT_INT_EQ((int)hamt_count(root), 100);

  /* Verify new values */
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ukey_%d", i);
    ASSERT(hamt_has(root, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, buf), i + 1000);
  }

  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: insert keys that produce hash collisions */
int test_transient_set_collision(void) {
  printf("  test_transient_set_collision: ");
  tracker_reset();
  hamt_set_key_handlers(forced_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  forced_hash_value = 42;

  hamt_transient* t = hamt_transient_fn(NULL);

  /* Insert 5 colliding keys */
  hamt_transient_set(t, my_strdup("alpha"), (void*)1);
  hamt_transient_set(t, my_strdup("beta"), (void*)2);
  hamt_transient_set(t, my_strdup("gamma"), (void*)3);
  hamt_transient_set(t, my_strdup("delta"), (void*)4);
  hamt_transient_set(t, my_strdup("epsilon"), (void*)5);

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT(root != NULL);
  ASSERT_INT_EQ((int)hamt_count(root), 5);

  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "alpha"), 1);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "beta"), 2);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "gamma"), 3);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "delta"), 4);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "epsilon"), 5);

  /* Update a colliding key */
  hamt_set_key_handlers(forced_hash, str_eq);
  hamt_transient* t2 = hamt_transient_fn(root);
  hamt_transient_set(t2, my_strdup("gamma"), (void*)33);

  hamt_node* root2 = hamt_persistent_fn(t2);
  ASSERT_INT_EQ((int)hamt_count(root2), 5);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root2, "gamma"), 33);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root2, "alpha"), 1);

  hamt_unref((hamt_node*)t);
  hamt_unref((hamt_node*)t2);
  hamt_unref(root);
  hamt_unref(root2);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: second set to same key region uses in-place mutation (source unchanged) */
int test_transient_set_inplace(void) {
  printf("  test_transient_set_inplace: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  /* Build persistent HAMT with 100 entries */
  hamt_node* src_root = NULL;
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "src_%d", i);
    src_root = update_root(src_root, hamt_set(src_root, my_strdup(buf), (void*)(intptr_t)(i + 1)));
  }

  /* Create transient and insert into same key region */
  hamt_transient* t = hamt_transient_fn(src_root);
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "new_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 200));
  }
  /* Second batch — now internal nodes should be owned (in-place) */
  for (int i = 100; i < 200; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "new_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 200));
  }

  /* Verify source persistent root is unchanged */
  ASSERT_INT_EQ((int)hamt_count(src_root), 100);
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "src_%d", i);
    ASSERT(hamt_has(src_root, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(src_root, buf), i + 1);
  }
  for (int i = 0; i < 200; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "new_%d", i);
    ASSERT(!hamt_has(src_root, buf));
  }

  /* Persist and verify transient result */
  hamt_node* result = hamt_persistent_fn(t);
  ASSERT_INT_EQ((int)hamt_count(result), 300);

  hamt_unref((hamt_node*)t);
  hamt_unref(result);
  hamt_unref(src_root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: lifecycle hooks fire correctly */
int test_transient_set_lifecycle(void) {
  printf("  test_transient_set_lifecycle: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(counting_create, counting_destroy);
  create_count = 0;
  destroy_count = 0;

  /* Insert 10 entries via transient */
  hamt_transient* t = hamt_transient_fn(NULL);
  for (int i = 0; i < 10; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "lkey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }
  ASSERT_INT_EQ(create_count, 10);

  /* Update 5 entries — should fire on_create for new leaf, on_destroy for replaced */
  int creates_before = create_count;
  int destroys_before = destroy_count;
  for (int i = 0; i < 5; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "lkey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 100));
  }
  ASSERT_INT_EQ(create_count - creates_before, 5);
  ASSERT_INT_EQ(destroy_count - destroys_before, 5);

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT_INT_EQ((int)hamt_count(root), 10);

  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set on invalidated transient returns NULL */
int test_transient_set_invalidated(void) {
  printf("  test_transient_set_invalidated: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);
  hamt_transient_set(t, my_strdup("key1"), (void*)1);

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT(root != NULL);

  /* Set after persist should return NULL */
  char* orphan_key = my_strdup("key2");
  hamt_transient* r = hamt_transient_set(t, orphan_key, (void*)2);
  ASSERT_PTR_EQ(r, NULL);
  /* key2 was never inserted, need to free the strdup'd key */
  tracked_free(orphan_key);

  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- US-003: Transient unset tests --- */

/* Test: insert 1000 entries via transient, remove 500, persist, verify */
int test_transient_unset_partial(void) {
  printf("  test_transient_unset_partial: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);

  /* Insert 1000 entries */
  for (int i = 0; i < 1000; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "rkey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }

  /* Remove first 500 */
  for (int i = 0; i < 500; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "rkey_%d", i);
    hamt_transient* r = hamt_transient_unset(t, buf);
    ASSERT_PTR_EQ(r, t);
  }

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT(root != NULL);
  ASSERT_INT_EQ((int)hamt_count(root), 500);

  /* Verify remaining 500 present */
  for (int i = 500; i < 1000; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "rkey_%d", i);
    ASSERT(hamt_has(root, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, buf), i + 1);
  }

  /* Verify removed 500 absent */
  for (int i = 0; i < 500; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "rkey_%d", i);
    ASSERT(!hamt_has(root, buf));
  }

  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: remove all entries, persist, verify empty root (NULL) */
int test_transient_unset_all(void) {
  printf("  test_transient_unset_all: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);

  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "akey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }

  /* Remove all 100 */
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "akey_%d", i);
    hamt_transient_unset(t, buf);
  }

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT_PTR_EQ(root, NULL);

  hamt_unref((hamt_node*)t);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: remove non-existent key — no-op, count unchanged */
int test_transient_unset_nonexistent(void) {
  printf("  test_transient_unset_nonexistent: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);

  for (int i = 0; i < 10; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "nkey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }

  /* Remove non-existent key */
  hamt_transient* r = hamt_transient_unset(t, "does_not_exist");
  ASSERT_PTR_EQ(r, t);

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT_INT_EQ((int)hamt_count(root), 10);

  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: remove from collision nodes — shrinks and collapses to leaf */
int test_transient_unset_collision(void) {
  printf("  test_transient_unset_collision: ");
  tracker_reset();
  hamt_set_key_handlers(forced_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  forced_hash_value = 77;

  hamt_transient* t = hamt_transient_fn(NULL);

  /* Insert 4 colliding keys */
  hamt_transient_set(t, my_strdup("col_a"), (void*)1);
  hamt_transient_set(t, my_strdup("col_b"), (void*)2);
  hamt_transient_set(t, my_strdup("col_c"), (void*)3);
  hamt_transient_set(t, my_strdup("col_d"), (void*)4);

  /* Remove one — collision node should shrink from 4 to 3 */
  hamt_transient_unset(t, "col_b");
  hamt_node* root = hamt_persistent_fn(t);
  ASSERT_INT_EQ((int)hamt_count(root), 3);
  ASSERT(!hamt_has(root, "col_b"));
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "col_a"), 1);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "col_c"), 3);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root, "col_d"), 4);

  /* Now remove down to 2 then 1 — should collapse to leaf */
  hamt_set_key_handlers(forced_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
  hamt_transient* t2 = hamt_transient_fn(root);
  hamt_transient_unset(t2, "col_a");
  hamt_transient_unset(t2, "col_c");

  hamt_node* root2 = hamt_persistent_fn(t2);
  ASSERT_INT_EQ((int)hamt_count(root2), 1);
  ASSERT_INT_EQ((int)(intptr_t)hamt_get(root2, "col_d"), 4);

  hamt_unref((hamt_node*)t);
  hamt_unref((hamt_node*)t2);
  hamt_unref(root);
  hamt_unref(root2);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: lifecycle hooks fire correctly — on_destroy called for each removed entry */
int test_transient_unset_lifecycle(void) {
  printf("  test_transient_unset_lifecycle: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(counting_create, counting_destroy);
  create_count = 0;
  destroy_count = 0;

  hamt_transient* t = hamt_transient_fn(NULL);

  /* Insert 10 entries */
  for (int i = 0; i < 10; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "dkey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }
  ASSERT_INT_EQ(create_count, 10);
  int destroys_before = destroy_count;

  /* Remove 5 entries */
  for (int i = 0; i < 5; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "dkey_%d", i);
    hamt_transient_unset(t, buf);
  }

  /* on_destroy should have been called 5 times (when old leaves are unref'd) */
  ASSERT_INT_EQ(destroy_count - destroys_before, 5);

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT_INT_EQ((int)hamt_count(root), 5);

  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: source persistent root unchanged after transient removes */
int test_transient_unset_source_unchanged(void) {
  printf("  test_transient_unset_source_unchanged: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  /* Build persistent HAMT with 100 entries */
  hamt_node* src_root = NULL;
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "skey_%d", i);
    src_root = update_root(src_root, hamt_set(src_root, my_strdup(buf), (void*)(intptr_t)(i + 1)));
  }

  /* Create transient and remove 50 entries */
  hamt_transient* t = hamt_transient_fn(src_root);
  for (int i = 0; i < 50; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "skey_%d", i);
    hamt_transient_unset(t, buf);
  }

  /* Verify source persistent root is unchanged */
  ASSERT_INT_EQ((int)hamt_count(src_root), 100);
  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "skey_%d", i);
    ASSERT(hamt_has(src_root, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(src_root, buf), i + 1);
  }

  hamt_node* result = hamt_persistent_fn(t);
  ASSERT_INT_EQ((int)hamt_count(result), 50);

  hamt_unref((hamt_node*)t);
  hamt_unref(result);
  hamt_unref(src_root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- US-004: Transient iteration tests --- */

/* Test: insert 100 entries via transient, iterate, verify all 100 visited exactly once */
int test_transient_iter_100(void) {
  printf("  test_transient_iter_100: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);

  for (int i = 0; i < 100; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ikey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }

  /* Iterate and track which entries we visit */
  bool seen[100];
  memset(seen, 0, sizeof(seen));
  int count = 0;

  hamt_iter it = hamt_transient_iter_init(t);
  hamt_iter_result result;
  while (true) {
    result = hamt_next_leaf(&it);
    if (result.done) break;
    count++;
    const char* key = hamt_key_from_leaf(result.item);
    int idx = -1;
    if (sscanf(key, "ikey_%d", &idx) == 1 && idx >= 0 && idx < 100) {
      ASSERT(!seen[idx]);
      seen[idx] = true;
      ASSERT_INT_EQ((int)(intptr_t)hamt_value_from_leaf(result.item), idx + 1);
    } else {
      ASSERT(0 && "unexpected key");
    }
  }

  ASSERT_INT_EQ(count, 100);
  for (int i = 0; i < 100; i++) {
    ASSERT(seen[i]);
  }

  hamt_node* root = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: insert entries, remove some, iterate, verify only remaining visited */
int test_transient_iter_after_unset(void) {
  printf("  test_transient_iter_after_unset: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);

  /* Insert 50 entries */
  for (int i = 0; i < 50; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "iukey_%d", i);
    hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
  }

  /* Remove first 20 */
  for (int i = 0; i < 20; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "iukey_%d", i);
    hamt_transient_unset(t, buf);
  }

  /* Iterate — should see only entries 20..49 */
  bool seen[50];
  memset(seen, 0, sizeof(seen));
  int count = 0;

  hamt_iter it = hamt_transient_iter_init(t);
  hamt_iter_result result;
  while (true) {
    result = hamt_next_leaf(&it);
    if (result.done) break;
    count++;
    const char* key = hamt_key_from_leaf(result.item);
    int idx = -1;
    if (sscanf(key, "iukey_%d", &idx) == 1 && idx >= 0 && idx < 50) {
      ASSERT(!seen[idx]);
      seen[idx] = true;
    } else {
      ASSERT(0 && "unexpected key");
    }
  }

  ASSERT_INT_EQ(count, 30);
  for (int i = 0; i < 20; i++) {
    ASSERT(!seen[i]);
  }
  for (int i = 20; i < 50; i++) {
    ASSERT(seen[i]);
  }

  hamt_node* root = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: iterate empty transient — next_leaf returns NULL immediately */
int test_transient_iter_empty(void) {
  printf("  test_transient_iter_empty: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_transient* t = hamt_transient_fn(NULL);

  hamt_iter it = hamt_transient_iter_init(t);
  hamt_iter_result result = hamt_next_leaf(&it);
  ASSERT(result.done == true);
  ASSERT(result.item == NULL);

  hamt_node* root = hamt_persistent_fn(t);
  ASSERT_PTR_EQ(root, NULL);

  hamt_unref((hamt_node*)t);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */

int main(void) {
  int passed = 0;
  int failed = 0;

  printf("HAMT Transient Tests:\n");

#define RUN_TEST(test_fn)                                     \
  do {                                                        \
    hamt_set_key_handlers(str_hash, str_eq);                  \
    hamt_set_lifecycle_hooks(NULL, test_key_destroy);          \
    if (test_fn()) {                                          \
      passed++;                                               \
      hamt_set_key_handlers(str_hash, str_eq);                \
      hamt_set_lifecycle_hooks(NULL, test_key_destroy);       \
    } else {                                                  \
      failed++;                                               \
    }                                                         \
  } while (0)

  RUN_TEST(test_transient_empty);
  RUN_TEST(test_transient_roundtrip);
  RUN_TEST(test_transient_source_unchanged);
  RUN_TEST(test_transient_double_persist);
  RUN_TEST(test_transient_set_10000);
  RUN_TEST(test_transient_set_update);
  RUN_TEST(test_transient_set_collision);
  RUN_TEST(test_transient_set_inplace);
  RUN_TEST(test_transient_set_lifecycle);
  RUN_TEST(test_transient_set_invalidated);
  RUN_TEST(test_transient_unset_partial);
  RUN_TEST(test_transient_unset_all);
  RUN_TEST(test_transient_unset_nonexistent);
  RUN_TEST(test_transient_unset_collision);
  RUN_TEST(test_transient_unset_lifecycle);
  RUN_TEST(test_transient_unset_source_unchanged);
  RUN_TEST(test_transient_iter_100);
  RUN_TEST(test_transient_iter_after_unset);
  RUN_TEST(test_transient_iter_empty);

  printf("\n=== Test Summary ===\n");
  printf("Passed: %d\n", passed);
  printf("Failed: %d\n", failed);

  return failed > 0 ? 1 : 0;
}
