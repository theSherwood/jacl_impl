/* test_hamt_ownership.c - Thread ownership enforcement tests for HAMT transients
 *
 * IMPORTANT: This file is compiled with NDEBUG defined so that ownership
 * violations return NULL instead of aborting the test process via assert().
 */
/* Ensure NDEBUG is defined — build.sh passes -DNDEBUG, but define here too as a guard */
#ifndef NDEBUG
#define NDEBUG
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

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

static char* my_strdup(const char* s) {
  size_t len = strlen(s) + 1;
  char*  d   = tracked_malloc(len);
  memcpy(d, s, len);
  return d;
}

static void test_key_destroy(const char* key, void* val) {
  (void)val;
  tracked_free((void*)key);
}

static uint32_t str_hash(const char* key) {
  return string_hash(key);
}

static bool str_eq(const char* a, const char* b) {
  return string_eq(a, b);
}

/* --- Helper to build a persistent root --- */

static hamt_node* update_root(hamt_node* root, hamt_node* new_root) {
  if (root) hamt_unref(root);
  return new_root;
}

static hamt_node* build_persistent(int n) {
  hamt_node* root = NULL;
  for (int i = 0; i < n; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", i);
    root = update_root(root, hamt_set(root, my_strdup(buf), (void*)(intptr_t)(i + 1)));
  }
  return root;
}

/* --- Thread worker args --- */

typedef struct {
  hamt_transient* transient;
  hamt_transient* set_result;
  char*           key;
} thread_set_arg;

typedef struct {
  hamt_transient* transient;
  hamt_transient* unset_result;
} thread_unset_arg;

typedef struct {
  hamt_transient* transient;
  hamt_node*      persist_result;
} thread_persist_arg;

typedef struct {
  hamt_transient* transient;
  hamt_iter       iter_result;
  int             iter_depth;
} thread_iter_arg;

/* Worker: attempt transient_set from non-owner thread */
static void* thread_set_worker(void* arg) {
  thread_set_arg* a = (thread_set_arg*)arg;
  a->set_result = hamt_transient_set(a->transient, a->key, (void*)999);
  return NULL;
}

/* Worker: attempt transient_unset from non-owner thread */
static void* thread_unset_worker(void* arg) {
  thread_unset_arg* a = (thread_unset_arg*)arg;
  a->unset_result = hamt_transient_unset(a->transient, "key_0");
  return NULL;
}

/* Worker: attempt persist from non-owner thread */
static void* thread_persist_worker(void* arg) {
  thread_persist_arg* a = (thread_persist_arg*)arg;
  a->persist_result = hamt_persistent_fn(a->transient);
  return NULL;
}

/* Worker: attempt transient_iter_init from non-owner thread */
static void* thread_iter_worker(void* arg) {
  thread_iter_arg* a = (thread_iter_arg*)arg;
  a->iter_result = hamt_transient_iter_init(a->transient);
  a->iter_depth  = a->iter_result.depth;
  return NULL;
}

/* --- Tests --- */

/* Test: transient_set from non-owner thread returns NULL */
int test_ownership_set_wrong_thread(void) {
  printf("  test_ownership_set_wrong_thread: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_node* root = build_persistent(10);
  hamt_transient* t = hamt_transient_fn(root);
  ASSERT(t != NULL);

  /* Spawn thread that attempts transient_set */
  char* orphan_key = my_strdup("from_other_thread");
  thread_set_arg arg = {.transient = t, .set_result = (hamt_transient*)1, .key = orphan_key};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_set_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner set should return NULL */
  ASSERT(arg.set_result == NULL);
  /* Key was never consumed — free it */
  tracked_free(orphan_key);

  /* Transient should still be usable by owner */
  hamt_transient* r = hamt_transient_set(t, my_strdup("owner_key"), (void*)42);
  ASSERT(r == t);

  hamt_node* result = hamt_persistent_fn(t);
  ASSERT(result != NULL);
  ASSERT_INT_EQ((int)hamt_count(result), 11);
  ASSERT(hamt_has(result, "owner_key"));

  hamt_unref((hamt_node*)t);
  hamt_unref(result);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: persist from non-owner thread returns NULL */
int test_ownership_persist_wrong_thread(void) {
  printf("  test_ownership_persist_wrong_thread: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_node* root = build_persistent(10);
  hamt_transient* t = hamt_transient_fn(root);
  ASSERT(t != NULL);

  /* Spawn thread that attempts persist */
  thread_persist_arg arg = {.transient = t, .persist_result = (hamt_node*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_persist_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner persist should return NULL */
  ASSERT(arg.persist_result == NULL);

  /* Transient should still be usable by owner */
  hamt_node* result = hamt_persistent_fn(t);
  ASSERT(result != NULL);
  ASSERT_INT_EQ((int)hamt_count(result), 10);

  hamt_unref((hamt_node*)t);
  hamt_unref(result);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: double-persist returns NULL on second call */
int test_ownership_double_persist(void) {
  printf("  test_ownership_double_persist: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_node* root = build_persistent(10);
  hamt_transient* t = hamt_transient_fn(root);
  ASSERT(t != NULL);

  hamt_node* r1 = hamt_persistent_fn(t);
  ASSERT(r1 != NULL);

  /* Second persist should return NULL */
  hamt_node* r2 = hamt_persistent_fn(t);
  ASSERT(r2 == NULL);

  /* All mutations on invalidated transient should return NULL */
  char* orphan_key = my_strdup("orphan");
  hamt_transient* sr = hamt_transient_set(t, orphan_key, (void*)99);
  ASSERT(sr == NULL);
  tracked_free(orphan_key);

  hamt_transient* ur = hamt_transient_unset(t, "key_0");
  ASSERT(ur == NULL);

  hamt_iter it = hamt_transient_iter_init(t);
  ASSERT_INT_EQ(it.depth, 0);

  hamt_unref((hamt_node*)t);
  hamt_unref(r1);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transient_unset from non-owner thread returns NULL */
int test_ownership_unset_wrong_thread(void) {
  printf("  test_ownership_unset_wrong_thread: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_node* root = build_persistent(10);
  hamt_transient* t = hamt_transient_fn(root);
  ASSERT(t != NULL);

  /* Spawn thread that attempts transient_unset */
  thread_unset_arg arg = {.transient = t, .unset_result = (hamt_transient*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_unset_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner unset should return NULL */
  ASSERT(arg.unset_result == NULL);

  /* Transient should still be usable by owner — all 10 entries intact */
  hamt_node* result = hamt_persistent_fn(t);
  ASSERT(result != NULL);
  ASSERT_INT_EQ((int)hamt_count(result), 10);
  ASSERT(hamt_has(result, "key_0"));

  hamt_unref((hamt_node*)t);
  hamt_unref(result);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transient_iter_init from non-owner thread returns zero-initialized iter */
int test_ownership_iter_wrong_thread(void) {
  printf("  test_ownership_iter_wrong_thread: ");
  tracker_reset();
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  hamt_node* root = build_persistent(10);
  hamt_transient* t = hamt_transient_fn(root);
  ASSERT(t != NULL);

  /* Spawn thread that attempts transient_iter_init */
  thread_iter_arg arg = {.transient = t, .iter_depth = 99};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_iter_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner iter_init should return depth=0 (no iteration possible) */
  ASSERT_INT_EQ(arg.iter_depth, 0);

  /* Verify next_leaf immediately returns done */
  hamt_iter_result res = hamt_next_leaf(&arg.iter_result);
  ASSERT(res.done == true);
  ASSERT(res.item == NULL);

  /* Transient should still be usable by owner */
  hamt_node* result = hamt_persistent_fn(t);
  ASSERT(result != NULL);
  ASSERT_INT_EQ((int)hamt_count(result), 10);

  hamt_unref((hamt_node*)t);
  hamt_unref(result);
  hamt_unref(root);

  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */

int main(void) {
  int pass = 0, fail = 0;

  printf("HAMT Ownership Tests:\n");

  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);

  test_ownership_set_wrong_thread()     ? pass++ : fail++;
  test_ownership_persist_wrong_thread() ? pass++ : fail++;
  test_ownership_double_persist()       ? pass++ : fail++;
  test_ownership_unset_wrong_thread()   ? pass++ : fail++;
  test_ownership_iter_wrong_thread()    ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
