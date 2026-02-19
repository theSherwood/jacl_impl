/* test_rrb_vec_concurrent.c - Concurrent correctness tests for RRB transients
 *
 * Verifies that multiple threads can safely create independent transients
 * from the same shared persistent vector and mutate them concurrently.
 */

#include "../../test/test_helpers.h"

#define TRACKED_ALLOC                     \
  {                                       \
      .alloc = tracked_malloc_with_ctx,   \
      .free  = tracked_free_with_ctx,     \
  }

#define RRB_VEC_T         int
#define RRB_VEC_NAME      int_vec
#define RRB_VEC_ALLOCATOR TRACKED_ALLOC
#include "rrb_vec.h"

#define NUM_THREADS 8
#define SHARED_SIZE 1000

/* Build a persistent vec with `count` elements: 0, 1, 2, ..., count-1 */
static int_vec_root* build_vec(int count) {
  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < count; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }
  return v;
}

/* --- Test 1: 8 threads each create own transient, push 1000, persist --- */

typedef struct {
  int_vec_root*  shared;
  int            thread_id;
  int_vec_root*  result;
  int            ok;
} thread_push_arg;

static void* thread_push_worker(void* arg) {
  thread_push_arg* a = (thread_push_arg*)arg;
  a->ok = 0;

  int_vec_transient* t = int_vec_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < SHARED_SIZE; i++) {
    int val = (a->thread_id + 1) * 100000 + i;
    int_vec_transient* r = int_vec_transient_push_back(t, val);
    if (r != t) { int_vec_rc_unref(t); return NULL; }
  }

  a->result = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  if (!a->result) return NULL;

  a->ok = 1;
  return NULL;
}

int test_concurrent_transient_push(void) {
  printf("  test_concurrent_transient_push: ");
  tracker_reset();

  int_vec_root* shared = build_vec(SHARED_SIZE);

  thread_push_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].result = NULL;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, thread_push_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  /* Verify each thread's result */
  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
    ASSERT(args[i].result != NULL);
    ASSERT_INT_EQ((int)int_vec_count(args[i].result), 2000);

    /* First 1000 elements should be the shared source: 0..999 */
    for (int j = 0; j < SHARED_SIZE; j++) {
      int_vec_get_result gr = int_vec_get(args[i].result, (uint32_t)j);
      ASSERT(gr.found);
      ASSERT_INT_EQ(gr.value, j);
    }

    /* Next 1000 elements should be thread-specific values */
    for (int j = 0; j < SHARED_SIZE; j++) {
      int_vec_get_result gr = int_vec_get(args[i].result, (uint32_t)(SHARED_SIZE + j));
      ASSERT(gr.found);
      ASSERT_INT_EQ(gr.value, (i + 1) * 100000 + j);
    }
  }

  /* Original shared vec should be unchanged */
  ASSERT_INT_EQ((int)int_vec_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    int_vec_get_result gr = int_vec_get(shared, (uint32_t)j);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, j);
  }

  /* Cleanup */
  for (int i = 0; i < NUM_THREADS; i++) {
    int_vec_unref(args[i].result);
  }
  int_vec_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test 2: Concurrent reads from source while one thread mutates transient --- */

typedef struct {
  int_vec_root*  shared;
  int            ok;
} thread_read_arg;

static void* thread_reader_worker(void* arg) {
  thread_read_arg* a = (thread_read_arg*)arg;
  a->ok = 0;

  /* Read all elements from the shared persistent vec multiple times */
  for (int round = 0; round < 10; round++) {
    for (int i = 0; i < SHARED_SIZE; i++) {
      int_vec_get_result gr = int_vec_get(a->shared, (uint32_t)i);
      if (!gr.found || gr.value != i) return NULL;
    }
  }

  a->ok = 1;
  return NULL;
}

typedef struct {
  int_vec_root*  shared;
  int_vec_root*  result;
  int            ok;
} thread_mutate_arg;

static void* thread_mutator_worker(void* arg) {
  thread_mutate_arg* a = (thread_mutate_arg*)arg;
  a->ok = 0;

  int_vec_transient* t = int_vec_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < 5000; i++) {
    int_vec_transient* r = int_vec_transient_push_back(t, SHARED_SIZE + i);
    if (r != t) { int_vec_rc_unref(t); return NULL; }
  }

  a->result = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  if (!a->result) return NULL;

  a->ok = 1;
  return NULL;
}

int test_concurrent_read_while_mutate(void) {
  printf("  test_concurrent_read_while_mutate: ");
  tracker_reset();

  int_vec_root* shared = build_vec(SHARED_SIZE);

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
  ASSERT_INT_EQ((int)int_vec_count(mut_arg.result), SHARED_SIZE + 5000);

  /* Source should be unchanged */
  ASSERT_INT_EQ((int)int_vec_count(shared), SHARED_SIZE);

  int_vec_unref(mut_arg.result);
  int_vec_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test 3: Stress test — 8 threads x 10000 push_back each --- */

#define STRESS_PUSH_COUNT 10000

typedef struct {
  int_vec_root*  shared;
  int            thread_id;
  int_vec_root*  result;
  int            ok;
} thread_stress_arg;

static void* thread_stress_worker(void* arg) {
  thread_stress_arg* a = (thread_stress_arg*)arg;
  a->ok = 0;

  int_vec_transient* t = int_vec_transient_fn(a->shared);
  if (!t) return NULL;

  for (int i = 0; i < STRESS_PUSH_COUNT; i++) {
    int val = (a->thread_id + 1) * 100000 + i;
    int_vec_transient* r = int_vec_transient_push_back(t, val);
    if (r != t) { int_vec_rc_unref(t); return NULL; }
  }

  a->result = int_vec_persistent_fn(t);
  int_vec_rc_unref(t);
  if (!a->result) return NULL;

  a->ok = 1;
  return NULL;
}

int test_concurrent_stress(void) {
  printf("  test_concurrent_stress: ");
  tracker_reset();

  int_vec_root* shared = build_vec(SHARED_SIZE);

  thread_stress_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].result = NULL;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, thread_stress_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  /* Verify each thread's result: 1000 shared + 10000 pushed = 11000 */
  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
    ASSERT(args[i].result != NULL);
    ASSERT_INT_EQ((int)int_vec_count(args[i].result), SHARED_SIZE + STRESS_PUSH_COUNT);

    /* Spot-check: first element, last shared element, first pushed, last pushed */
    int_vec_get_result gr;

    gr = int_vec_get(args[i].result, 0);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, 0);

    gr = int_vec_get(args[i].result, SHARED_SIZE - 1);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, SHARED_SIZE - 1);

    gr = int_vec_get(args[i].result, SHARED_SIZE);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, (i + 1) * 100000);

    gr = int_vec_get(args[i].result, SHARED_SIZE + STRESS_PUSH_COUNT - 1);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, (i + 1) * 100000 + STRESS_PUSH_COUNT - 1);
  }

  /* Original shared vec still correct */
  ASSERT_INT_EQ((int)int_vec_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    int_vec_get_result gr = int_vec_get(shared, (uint32_t)j);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, j);
  }

  /* Cleanup */
  for (int i = 0; i < NUM_THREADS; i++) {
    int_vec_unref(args[i].result);
  }
  int_vec_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test 4: Source vec correct after all threads complete --- */

int test_concurrent_source_intact(void) {
  printf("  test_concurrent_source_intact: ");
  tracker_reset();

  int_vec_root* shared = build_vec(SHARED_SIZE);

  /* 8 threads each create transient, push, set, persist */
  thread_push_arg args[NUM_THREADS];
  thread_t threads[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    args[i].shared = shared;
    args[i].thread_id = i;
    args[i].result = NULL;
    args[i].ok = 0;
    THREAD_CREATE(&threads[i], NULL, thread_push_worker, &args[i]);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    THREAD_JOIN(threads[i], NULL);
  }

  /* Unref all results first */
  for (int i = 0; i < NUM_THREADS; i++) {
    ASSERT(args[i].ok);
    int_vec_unref(args[i].result);
  }

  /* Now verify shared is still fully correct and can be unref'd cleanly */
  ASSERT_INT_EQ((int)int_vec_count(shared), SHARED_SIZE);
  for (int j = 0; j < SHARED_SIZE; j++) {
    int_vec_get_result gr = int_vec_get(shared, (uint32_t)j);
    ASSERT(gr.found);
    ASSERT_INT_EQ(gr.value, j);
  }

  int_vec_unref(shared);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  int pass = 0, fail = 0;

  printf("rrb_vec concurrent tests:\n");

  test_concurrent_transient_push()    ? pass++ : fail++;
  test_concurrent_read_while_mutate() ? pass++ : fail++;
  test_concurrent_stress()            ? pass++ : fail++;
  test_concurrent_source_intact()     ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
