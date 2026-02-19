/* test_hamt_concurrent.c - Concurrent correctness tests for HAMT transients
 *
 * Verifies that multiple threads can safely create independent transients
 * from the same shared persistent HAMT and mutate them concurrently.
 */

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

/* Lock-free instantiation using default libc allocator (no tracker mutex) */
#define HAMT_KEY_T  const char*
#define HAMT_VAL_T  void*
#define HAMT_NAME   fast
#include "hamt.h"

/* --- Key Helpers --- */

static char* my_strdup(const char* s) {
  size_t len = strlen(s) + 1;
  char*  d   = tracked_malloc(len);
  memcpy(d, s, len);
  return d;
}

static char* fast_strdup(const char* s) {
  size_t len = strlen(s) + 1;
  char*  d   = malloc(len);
  memcpy(d, s, len);
  return d;
}

static void test_key_destroy(const char* key, void* val) {
  (void)val;
  tracked_free((void*)key);
}

static void fast_key_destroy(const char* key, void* val) {
  (void)val;
  free((void*)key);
}

static uint32_t str_hash(const char* key) {
  return string_hash(key);
}

static bool str_eq(const char* a, const char* b) {
  return string_eq(a, b);
}

/* --- Constants --- */

#define NUM_THREADS 8
#define SHARED_SIZE 1000

/* --- Helper: build persistent HAMT with n entries --- */

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

/* --- Helper: set up thread-local handlers --- */

static void setup_handlers(void) {
  hamt_set_key_handlers(str_hash, str_eq);
  hamt_set_lifecycle_hooks(NULL, test_key_destroy);
}

/* --- Test 1: 8 threads each create own transient, insert 1000, persist --- */

typedef struct {
  hamt_node*  shared;
  int         thread_id;
  hamt_node*  result;
  int         ok;
} thread_insert_arg;

static void* thread_insert_worker(void* arg) {
  thread_insert_arg* a = (thread_insert_arg*)arg;
  a->ok = 0;
  setup_handlers();

  hamt_transient* t = hamt_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < SHARED_SIZE; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "t%d_%d", a->thread_id, i);
    hamt_transient* r = hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)((a->thread_id + 1) * 100000 + i));
    if (r != t) {
      hamt_unref((hamt_node*)t);
      return NULL;
    }
  }

  a->result = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  if (!a->result) return NULL;

  a->ok = 1;
  return NULL;
}

int test_concurrent_transient_insert(void) {
  printf("  test_concurrent_transient_insert: ");
  tracker_reset();
  setup_handlers();

  hamt_node* shared = build_persistent(SHARED_SIZE);

  thread_insert_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].result = NULL;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, thread_insert_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  /* Verify each thread's result */
  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
    ASSERT(args[i].result != NULL);
    ASSERT_INT_EQ((int)hamt_count(args[i].result), 2000);

    /* All 1000 shared entries should be present */
    for (int j = 0; j < SHARED_SIZE; j++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "key_%d", j);
      ASSERT(hamt_has(args[i].result, buf));
      ASSERT_INT_EQ((int)(intptr_t)hamt_get(args[i].result, buf), j + 1);
    }

    /* All 1000 thread-specific entries should be present */
    for (int j = 0; j < SHARED_SIZE; j++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "t%d_%d", i, j);
      ASSERT(hamt_has(args[i].result, buf));
      ASSERT_INT_EQ((int)(intptr_t)hamt_get(args[i].result, buf), (i + 1) * 100000 + j);
    }
  }

  /* Original shared HAMT should be unchanged */
  ASSERT_INT_EQ((int)hamt_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", j);
    ASSERT(hamt_has(shared, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(shared, buf), j + 1);
  }

  /* Cleanup */
  for (int i = 0; i < NUM_THREADS; i++) {
    hamt_unref(args[i].result);
  }
  hamt_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test 2: Concurrent reads from source while one thread mutates transient --- */

typedef struct {
  hamt_node*  shared;
  int         ok;
} thread_read_arg;

static void* thread_reader_worker(void* arg) {
  thread_read_arg* a = (thread_read_arg*)arg;
  a->ok = 0;
  setup_handlers();

  /* Read all elements from the shared persistent HAMT multiple times */
  for (int round = 0; round < 10; round++) {
    for (int i = 0; i < SHARED_SIZE; i++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "key_%d", i);
      if (!hamt_has(a->shared, buf)) return NULL;
      int val = (int)(intptr_t)hamt_get(a->shared, buf);
      if (val != i + 1) return NULL;
    }
  }

  a->ok = 1;
  return NULL;
}

typedef struct {
  hamt_node*  shared;
  hamt_node*  result;
  int         ok;
} thread_mutate_arg;

static void* thread_mutator_worker(void* arg) {
  thread_mutate_arg* a = (thread_mutate_arg*)arg;
  a->ok = 0;
  setup_handlers();

  hamt_transient* t = hamt_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < 5000; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "mut_%d", i);
    hamt_transient* r = hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 50000));
    if (r != t) {
      hamt_unref((hamt_node*)t);
      return NULL;
    }
  }

  a->result = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  if (!a->result) return NULL;

  a->ok = 1;
  return NULL;
}

int test_concurrent_read_while_mutate(void) {
  printf("  test_concurrent_read_while_mutate: ");
  tracker_reset();
  setup_handlers();

  hamt_node* shared = build_persistent(SHARED_SIZE);

  /* Start reader threads and one mutator thread concurrently */
  thread_read_arg read_args[NUM_THREADS - 1];
  thread_t read_threads[NUM_THREADS - 1];
  for (int i = 0; i < NUM_THREADS - 1; i++) {
    read_args[i].shared = shared;
    read_args[i].ok = 0;
    THREAD_CREATE(&read_threads[i], NULL, thread_reader_worker, &read_args[i]);
  }

  thread_mutate_arg mut_arg = {.shared = shared, .result = NULL, .ok = 0};
  thread_t mut_thread;
  THREAD_CREATE(&mut_thread, NULL, thread_mutator_worker, &mut_arg);

  /* Wait for all threads */
  for (int i = 0; i < NUM_THREADS - 1; i++) {
    THREAD_JOIN(read_threads[i], NULL);
  }
  THREAD_JOIN(mut_thread, NULL);

  /* All readers should have succeeded */
  for (int i = 0; i < NUM_THREADS - 1; i++) {
    ASSERT(read_args[i].ok);
  }

  /* Mutator should have succeeded */
  ASSERT(mut_arg.ok);
  ASSERT(mut_arg.result != NULL);
  ASSERT_INT_EQ((int)hamt_count(mut_arg.result), SHARED_SIZE + 5000);

  /* Source should be unchanged */
  ASSERT_INT_EQ((int)hamt_count(shared), SHARED_SIZE);

  hamt_unref(mut_arg.result);
  hamt_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test 3: Concurrent insert + unset ---
 *
 * 8 threads each create a transient from a shared HAMT (1000 entries).
 * Each thread inserts 500 new unique entries AND removes 500 of the original
 * shared entries. Persist, verify each result has exactly 1000 entries
 * (500 kept + 500 new). This exercises concurrent ATOMIC_DEC on shared leaf
 * nodes being removed from different transients simultaneously.
 */

#define INSERT_UNSET_NEW   500
#define INSERT_UNSET_REMOVE 500

typedef struct {
  hamt_node*  shared;
  int         thread_id;
  hamt_node*  result;
  int         ok;
} thread_insert_unset_arg;

static void* thread_insert_unset_worker(void* arg) {
  thread_insert_unset_arg* a = (thread_insert_unset_arg*)arg;
  a->ok = 0;
  setup_handlers();

  hamt_transient* t = hamt_transient_fn(a->shared);
  if (!t) return NULL;

  /* Insert 500 new unique entries */
  for (int i = 0; i < INSERT_UNSET_NEW; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "new%d_%d", a->thread_id, i);
    hamt_transient* r = hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)((a->thread_id + 1) * 100000 + i));
    if (r != t) {
      hamt_unref((hamt_node*)t);
      return NULL;
    }
  }

  /* Remove 500 of the original shared entries (all threads remove the same ones) */
  for (int i = 0; i < INSERT_UNSET_REMOVE; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", i);
    hamt_transient* r = hamt_transient_unset(t, buf);
    if (r != t) {
      hamt_unref((hamt_node*)t);
      return NULL;
    }
  }

  a->result = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  if (!a->result) return NULL;

  a->ok = 1;
  return NULL;
}

int test_concurrent_insert_unset(void) {
  printf("  test_concurrent_insert_unset: ");
  tracker_reset();
  setup_handlers();

  hamt_node* shared = build_persistent(SHARED_SIZE);

  thread_insert_unset_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].result = NULL;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, thread_insert_unset_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  /* Verify each thread's result: 500 kept + 500 new = 1000 */
  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
    ASSERT(args[i].result != NULL);
    ASSERT_INT_EQ((int)hamt_count(args[i].result), SHARED_SIZE - INSERT_UNSET_REMOVE + INSERT_UNSET_NEW);

    /* Removed entries should be gone */
    for (int j = 0; j < INSERT_UNSET_REMOVE; j++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "key_%d", j);
      ASSERT(!hamt_has(args[i].result, buf));
    }

    /* Kept entries should still be present */
    for (int j = INSERT_UNSET_REMOVE; j < SHARED_SIZE; j++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "key_%d", j);
      ASSERT(hamt_has(args[i].result, buf));
      ASSERT_INT_EQ((int)(intptr_t)hamt_get(args[i].result, buf), j + 1);
    }

    /* New entries should be present */
    for (int j = 0; j < INSERT_UNSET_NEW; j++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "new%d_%d", i, j);
      ASSERT(hamt_has(args[i].result, buf));
      ASSERT_INT_EQ((int)(intptr_t)hamt_get(args[i].result, buf), (i + 1) * 100000 + j);
    }
  }

  /* Original shared HAMT should be unchanged */
  ASSERT_INT_EQ((int)hamt_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", j);
    ASSERT(hamt_has(shared, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(shared, buf), j + 1);
  }

  /* Cleanup */
  for (int i = 0; i < NUM_THREADS; i++) {
    hamt_unref(args[i].result);
  }
  hamt_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test 4: Concurrent unref of derived trees while readers access shared source ---
 *
 * Build shared HAMT. 4 threads create transients, insert entries, persist,
 * then unref the results (triggering cascading ATOMIC_DEC -> destroy on nodes
 * that were copied from shared). Meanwhile, 4 other reader threads continuously
 * hamt_get from the shared source. Verifies that concurrent destruction of
 * derived trees doesn't corrupt the shared source.
 */

#define DESTROYER_THREADS 4
#define READER_THREADS    4
#define DESTROYER_INSERTS 500

typedef struct {
  hamt_node*  shared;
  int         thread_id;
  int         ok;
} thread_destroy_arg;

static void* thread_destroy_worker(void* arg) {
  thread_destroy_arg* a = (thread_destroy_arg*)arg;
  a->ok = 0;
  setup_handlers();

  hamt_transient* t = hamt_transient_fn(a->shared);
  if (!t) return NULL;

  /* Insert entries to create derived structure sharing nodes with source */
  for (int i = 0; i < DESTROYER_INSERTS; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "d%d_%d", a->thread_id, i);
    hamt_transient* r = hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
    if (r != t) {
      hamt_unref((hamt_node*)t);
      return NULL;
    }
  }

  hamt_node* result = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  if (!result) return NULL;

  /* Immediately unref the result — triggers cascading ATOMIC_DEC on shared nodes */
  hamt_unref(result);

  a->ok = 1;
  return NULL;
}

int test_concurrent_unref_while_reading(void) {
  printf("  test_concurrent_unref_while_reading: ");
  tracker_reset();
  setup_handlers();

  hamt_node* shared = build_persistent(SHARED_SIZE);

  thread_destroy_arg destroy_args[DESTROYER_THREADS];
  thread_t destroy_threads[DESTROYER_THREADS];
  thread_read_arg read_args[READER_THREADS];
  thread_t read_threads[READER_THREADS];

  /* Launch reader threads */
  for (int i = 0; i < READER_THREADS; i++) {
    read_args[i].shared = shared;
    read_args[i].ok = 0;
    THREAD_CREATE(&read_threads[i], NULL, thread_reader_worker, &read_args[i]);
  }

  /* Launch destroyer threads */
  for (int i = 0; i < DESTROYER_THREADS; i++) {
    destroy_args[i].shared = shared;
    destroy_args[i].thread_id = i;
    destroy_args[i].ok = 0;
    THREAD_CREATE(&destroy_threads[i], NULL, thread_destroy_worker, &destroy_args[i]);
  }

  /* Wait for all threads */
  for (int i = 0; i < READER_THREADS; i++) {
    THREAD_JOIN(read_threads[i], NULL);
  }
  for (int i = 0; i < DESTROYER_THREADS; i++) {
    THREAD_JOIN(destroy_threads[i], NULL);
  }

  /* All readers should have succeeded */
  for (int i = 0; i < READER_THREADS; i++) {
    ASSERT(read_args[i].ok);
  }

  /* All destroyers should have succeeded */
  for (int i = 0; i < DESTROYER_THREADS; i++) {
    ASSERT(destroy_args[i].ok);
  }

  /* Shared source should be fully intact */
  ASSERT_INT_EQ((int)hamt_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", j);
    ASSERT(hamt_has(shared, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(shared, buf), j + 1);
  }

  hamt_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test 5: Concurrent persist + unref stress ---
 *
 * 8 threads each create a transient, insert entries, persist, verify their
 * result, then unref both transient and result. All running concurrently.
 * After all threads complete, verify the shared source is intact and
 * check_no_leaks() passes. The key stress is 8 threads all doing H_RC_UNREF
 * cascades concurrently on trees that share internal structure with the source.
 */

#define STRESS_INSERTS 500

typedef struct {
  hamt_node*  shared;
  int         thread_id;
  int         ok;
} thread_stress_unref_arg;

static void* thread_stress_unref_worker(void* arg) {
  thread_stress_unref_arg* a = (thread_stress_unref_arg*)arg;
  a->ok = 0;
  setup_handlers();

  hamt_transient* t = hamt_transient_fn(a->shared);
  if (!t) return NULL;

  /* Insert unique entries */
  for (int i = 0; i < STRESS_INSERTS; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "su%d_%d", a->thread_id, i);
    hamt_transient* r = hamt_transient_set(t, my_strdup(buf), (void*)(intptr_t)(i + 1));
    if (r != t) {
      hamt_unref((hamt_node*)t);
      return NULL;
    }
  }

  hamt_node* result = hamt_persistent_fn(t);
  hamt_unref((hamt_node*)t);
  if (!result) return NULL;

  /* Verify the result before destroying */
  if ((int)hamt_count(result) != SHARED_SIZE + STRESS_INSERTS) {
    hamt_unref(result);
    return NULL;
  }

  /* Spot-check shared entries */
  for (int j = 0; j < SHARED_SIZE; j += 100) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", j);
    if (!hamt_has(result, buf)) {
      hamt_unref(result);
      return NULL;
    }
  }

  /* Spot-check thread entries */
  for (int j = 0; j < STRESS_INSERTS; j += 50) {
    char buf[48];
    snprintf(buf, sizeof(buf), "su%d_%d", a->thread_id, j);
    if (!hamt_has(result, buf)) {
      hamt_unref(result);
      return NULL;
    }
  }

  /* Unref the result — triggers concurrent cascading destruction */
  hamt_unref(result);

  a->ok = 1;
  return NULL;
}

int test_concurrent_persist_unref_stress(void) {
  printf("  test_concurrent_persist_unref_stress: ");
  tracker_reset();
  setup_handlers();

  hamt_node* shared = build_persistent(SHARED_SIZE);

  thread_stress_unref_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, thread_stress_unref_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  /* All threads should have succeeded */
  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
  }

  /* Shared source should be fully intact */
  ASSERT_INT_EQ((int)hamt_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", j);
    ASSERT(hamt_has(shared, buf));
    ASSERT_INT_EQ((int)(intptr_t)hamt_get(shared, buf), j + 1);
  }

  hamt_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===================================================================
 * Lock-free tests (no tracked allocator — maximizes thread interleaving)
 *
 * These use the `fast` HAMT instantiation backed by plain malloc/free.
 * No leak checking is possible, but correctness is verified via counts
 * and value checks. The point is to remove the tracker mutex bottleneck
 * so threads actually race on the atomic refcount operations.
 * =================================================================== */

static void setup_fast_handlers(void) {
  fast_set_key_handlers(str_hash, str_eq);
  fast_set_lifecycle_hooks(NULL, fast_key_destroy);
}

static fast_node* fast_update_root(fast_node* root, fast_node* new_root) {
  if (root) fast_unref(root);
  return new_root;
}

static fast_node* fast_build_persistent(int n) {
  fast_node* root = NULL;
  for (int i = 0; i < n; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", i);
    root = fast_update_root(root, fast_set(root, fast_strdup(buf), (void*)(intptr_t)(i + 1)));
  }
  return root;
}

/* --- Test 6: Lock-free concurrent insert + unset ---
 *
 * Same shape as test 3 but with no allocator mutex.
 */

typedef struct {
  fast_node*  shared;
  int         thread_id;
  fast_node*  result;
  int         ok;
} fast_insert_unset_arg;

static void* fast_insert_unset_worker(void* arg) {
  fast_insert_unset_arg* a = (fast_insert_unset_arg*)arg;
  a->ok = 0;
  setup_fast_handlers();

  fast_transient* t = fast_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < INSERT_UNSET_NEW; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "new%d_%d", a->thread_id, i);
    fast_transient* r = fast_transient_set(t, fast_strdup(buf), (void*)(intptr_t)((a->thread_id + 1) * 100000 + i));
    if (r != t) {
      fast_unref((fast_node*)t);
      return NULL;
    }
  }

  for (int i = 0; i < INSERT_UNSET_REMOVE; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", i);
    fast_transient* r = fast_transient_unset(t, buf);
    if (r != t) {
      fast_unref((fast_node*)t);
      return NULL;
    }
  }

  a->result = fast_persistent_fn(t);
  fast_unref((fast_node*)t);
  if (!a->result) return NULL;

  a->ok = 1;
  return NULL;
}

int test_fast_concurrent_insert_unset(void) {
  printf("  test_fast_concurrent_insert_unset: ");
  setup_fast_handlers();

  fast_node* shared = fast_build_persistent(SHARED_SIZE);

  fast_insert_unset_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].result = NULL;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, fast_insert_unset_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
    ASSERT(args[i].result != NULL);
    ASSERT_INT_EQ((int)fast_count(args[i].result), SHARED_SIZE - INSERT_UNSET_REMOVE + INSERT_UNSET_NEW);

    /* Removed entries should be gone */
    for (int j = 0; j < INSERT_UNSET_REMOVE; j++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "key_%d", j);
      ASSERT(!fast_has(args[i].result, buf));
    }

    /* Kept entries present */
    for (int j = INSERT_UNSET_REMOVE; j < SHARED_SIZE; j++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "key_%d", j);
      ASSERT(fast_has(args[i].result, buf));
      ASSERT_INT_EQ((int)(intptr_t)fast_get(args[i].result, buf), j + 1);
    }

    /* New entries present */
    for (int j = 0; j < INSERT_UNSET_NEW; j++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "new%d_%d", i, j);
      ASSERT(fast_has(args[i].result, buf));
    }
  }

  ASSERT_INT_EQ((int)fast_count(shared), SHARED_SIZE);

  for (int i = 0; i < NUM_THREADS; i++) {
    fast_unref(args[i].result);
  }
  fast_unref(shared);
  TEST_PASS();
}

/* --- Test 7: Lock-free concurrent unref while reading --- */

typedef struct {
  fast_node*  shared;
  int         ok;
} fast_read_arg;

static void* fast_reader_worker(void* arg) {
  fast_read_arg* a = (fast_read_arg*)arg;
  a->ok = 0;
  setup_fast_handlers();

  for (int round = 0; round < 20; round++) {
    for (int i = 0; i < SHARED_SIZE; i++) {
      char buf[32];
      snprintf(buf, sizeof(buf), "key_%d", i);
      if (!fast_has(a->shared, buf)) return NULL;
      int val = (int)(intptr_t)fast_get(a->shared, buf);
      if (val != i + 1) return NULL;
    }
  }

  a->ok = 1;
  return NULL;
}

typedef struct {
  fast_node*  shared;
  int         thread_id;
  int         ok;
} fast_destroy_arg;

static void* fast_destroy_worker(void* arg) {
  fast_destroy_arg* a = (fast_destroy_arg*)arg;
  a->ok = 0;
  setup_fast_handlers();

  fast_transient* t = fast_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < DESTROYER_INSERTS; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "d%d_%d", a->thread_id, i);
    fast_transient* r = fast_transient_set(t, fast_strdup(buf), (void*)(intptr_t)(i + 1));
    if (r != t) {
      fast_unref((fast_node*)t);
      return NULL;
    }
  }

  fast_node* result = fast_persistent_fn(t);
  fast_unref((fast_node*)t);
  if (!result) return NULL;

  fast_unref(result);

  a->ok = 1;
  return NULL;
}

int test_fast_concurrent_unref_while_reading(void) {
  printf("  test_fast_concurrent_unref_while_reading: ");
  setup_fast_handlers();

  fast_node* shared = fast_build_persistent(SHARED_SIZE);

  fast_destroy_arg destroy_args[DESTROYER_THREADS];
  thread_t destroy_threads[DESTROYER_THREADS];
  fast_read_arg read_args[READER_THREADS];
  thread_t read_threads[READER_THREADS];

  for (int i = 0; i < READER_THREADS; i++) {
    read_args[i].shared = shared;
    read_args[i].ok = 0;
    THREAD_CREATE(&read_threads[i], NULL, fast_reader_worker, &read_args[i]);
  }

  for (int i = 0; i < DESTROYER_THREADS; i++) {
    destroy_args[i].shared = shared;
    destroy_args[i].thread_id = i;
    destroy_args[i].ok = 0;
    THREAD_CREATE(&destroy_threads[i], NULL, fast_destroy_worker, &destroy_args[i]);
  }

  for (int i = 0; i < READER_THREADS; i++) {
    THREAD_JOIN(read_threads[i], NULL);
  }
  for (int i = 0; i < DESTROYER_THREADS; i++) {
    THREAD_JOIN(destroy_threads[i], NULL);
  }

  for (int i = 0; i < READER_THREADS; i++) {
    ASSERT(read_args[i].ok);
  }
  for (int i = 0; i < DESTROYER_THREADS; i++) {
    ASSERT(destroy_args[i].ok);
  }

  ASSERT_INT_EQ((int)fast_count(shared), SHARED_SIZE);

  fast_unref(shared);
  TEST_PASS();
}

/* --- Test 8: Lock-free concurrent persist + unref stress --- */

typedef struct {
  fast_node*  shared;
  int         thread_id;
  int         ok;
} fast_stress_arg;

static void* fast_stress_worker(void* arg) {
  fast_stress_arg* a = (fast_stress_arg*)arg;
  a->ok = 0;
  setup_fast_handlers();

  fast_transient* t = fast_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < STRESS_INSERTS; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "su%d_%d", a->thread_id, i);
    fast_transient* r = fast_transient_set(t, fast_strdup(buf), (void*)(intptr_t)(i + 1));
    if (r != t) {
      fast_unref((fast_node*)t);
      return NULL;
    }
  }

  fast_node* result = fast_persistent_fn(t);
  fast_unref((fast_node*)t);
  if (!result) return NULL;

  if ((int)fast_count(result) != SHARED_SIZE + STRESS_INSERTS) {
    fast_unref(result);
    return NULL;
  }

  /* Spot-check shared entries */
  for (int j = 0; j < SHARED_SIZE; j += 100) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", j);
    if (!fast_has(result, buf)) {
      fast_unref(result);
      return NULL;
    }
  }

  fast_unref(result);

  a->ok = 1;
  return NULL;
}

int test_fast_concurrent_persist_unref_stress(void) {
  printf("  test_fast_concurrent_persist_unref_stress: ");
  setup_fast_handlers();

  fast_node* shared = fast_build_persistent(SHARED_SIZE);

  fast_stress_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, fast_stress_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
  }

  ASSERT_INT_EQ((int)fast_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%d", j);
    ASSERT(fast_has(shared, buf));
    ASSERT_INT_EQ((int)(intptr_t)fast_get(shared, buf), j + 1);
  }

  fast_unref(shared);
  TEST_PASS();
}

int main(void) {
  int pass = 0, fail = 0;

  printf("HAMT Concurrent Tests:\n");

  test_concurrent_transient_insert()     ? pass++ : fail++;
  test_concurrent_read_while_mutate()    ? pass++ : fail++;
  test_concurrent_insert_unset()         ? pass++ : fail++;
  test_concurrent_unref_while_reading()  ? pass++ : fail++;
  test_concurrent_persist_unref_stress() ? pass++ : fail++;

  printf("\nHAMT Concurrent Tests (lock-free, no tracker):\n");

  test_fast_concurrent_insert_unset()         ? pass++ : fail++;
  test_fast_concurrent_unref_while_reading()  ? pass++ : fail++;
  test_fast_concurrent_persist_unref_stress() ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
