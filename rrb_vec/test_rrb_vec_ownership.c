/* test_rrb_vec_ownership.c - Thread ownership enforcement tests for RRB transients
 *
 * IMPORTANT: This file is compiled with NDEBUG defined so that ownership
 * violations return NULL instead of aborting the test process via assert().
 */
/* Ensure NDEBUG is defined — build.sh passes -DNDEBUG, but define here too as a guard */
#ifndef NDEBUG
#define NDEBUG
#endif

#include "../test/test_helpers.h"

#define TRACKED_ALLOC                     \
  {                                       \
      .alloc = tracked_malloc_with_ctx,   \
      .free  = tracked_free_with_ctx,     \
  }

#define RRB_VEC_T         int
#define RRB_VEC_NAME      int_vec
#define RRB_VEC_ALLOCATOR TRACKED_ALLOC
#include "rrb_vec.h"

/* --- Thread worker args --- */

typedef struct {
  int_vec_transient* transient;
  int_vec_transient* push_result;
} thread_push_arg;

typedef struct {
  int_vec_transient* transient;
  int_vec_root* persist_result;
} thread_persist_arg;

/* Worker: attempt push_back from non-owner thread */
static void* thread_push_worker(void* arg) {
  thread_push_arg* a = (thread_push_arg*)arg;
  a->push_result = int_vec_transient_push_back(a->transient, 999);
  return NULL;
}

/* Worker: attempt persist from non-owner thread */
static void* thread_persist_worker(void* arg) {
  thread_persist_arg* a = (thread_persist_arg*)arg;
  a->persist_result = int_vec_persistent_fn(a->transient);
  return NULL;
}

/* Test: push_back from non-owner thread returns NULL */
int test_ownership_push_back_wrong_thread(void) {
  printf("  test_ownership_push_back_wrong_thread: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  /* Spawn thread that attempts push_back */
  thread_push_arg arg = {.transient = t, .push_result = (int_vec_transient*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_push_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner push_back should return NULL */
  ASSERT(arg.push_result == NULL);

  /* Transient should still be usable by owner */
  int_vec_transient* r = int_vec_transient_push_back(t, 42);
  ASSERT(r == t);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 11);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: persist from non-owner thread returns NULL */
int test_ownership_persist_wrong_thread(void) {
  printf("  test_ownership_persist_wrong_thread: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  /* Spawn thread that attempts persist */
  thread_persist_arg arg = {.transient = t, .persist_result = (int_vec_root*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_persist_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner persist should return NULL */
  ASSERT(arg.persist_result == NULL);

  /* Transient should still be usable by owner */
  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 10);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: double-persist returns NULL on second call */
int test_ownership_double_persist(void) {
  printf("  test_ownership_double_persist: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);

  /* Second persist should return NULL */
  int_vec_root* v3 = int_vec_persistent_fn(t);
  ASSERT(v3 == NULL);

  /* All mutations on invalidated transient should return NULL */
  int_vec_transient* r = int_vec_transient_push_back(t, 99);
  ASSERT(r == NULL);

  r = int_vec_transient_set(t, 0, 99);
  ASSERT(r == NULL);

  int_vec_transient_pop_result pr = int_vec_transient_pop_back(t);
  ASSERT(!pr.found);
  ASSERT(pr.transient == NULL);

  r = int_vec_transient_push_front(t, 99);
  ASSERT(r == NULL);

  int_vec_transient_pop_result pfr = int_vec_transient_pop_front(t);
  ASSERT(!pfr.found);
  ASSERT(pfr.transient == NULL);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Worker for set test */
typedef struct {
  int_vec_transient* transient;
  int_vec_transient* set_result;
} thread_set_arg;

static void* thread_set_worker(void* arg) {
  thread_set_arg* a = (thread_set_arg*)arg;
  a->set_result = int_vec_transient_set(a->transient, 0, 999);
  return NULL;
}

/* Test: set from non-owner thread returns NULL */
int test_ownership_set_wrong_thread(void) {
  printf("  test_ownership_set_wrong_thread: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  thread_set_arg arg = {.transient = t, .set_result = (int_vec_transient*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_set_worker, &arg);
  THREAD_JOIN(th, NULL);

  ASSERT(arg.set_result == NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  /* Value should be unchanged — set was rejected */
  int_vec_get_result gr = int_vec_get(v2, 0);
  ASSERT(gr.found);
  ASSERT_INT_EQ(gr.value, 0);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Worker for pop_back test */
typedef struct {
  int_vec_transient* transient;
  int_vec_transient_pop_result pop_result;
} thread_pop_arg;

static void* thread_pop_worker(void* arg) {
  thread_pop_arg* a = (thread_pop_arg*)arg;
  a->pop_result = int_vec_transient_pop_back(a->transient);
  return NULL;
}

/* Test: pop_back from non-owner thread returns NULL */
int test_ownership_pop_back_wrong_thread(void) {
  printf("  test_ownership_pop_back_wrong_thread: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  thread_pop_arg arg;
  arg.transient = t;
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_pop_worker, &arg);
  THREAD_JOIN(th, NULL);

  ASSERT(!arg.pop_result.found);
  ASSERT(arg.pop_result.transient == NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 10);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Worker for push_front test */
typedef struct {
  int_vec_transient* transient;
  int_vec_transient* push_front_result;
} thread_push_front_arg;

static void* thread_push_front_worker(void* arg) {
  thread_push_front_arg* a = (thread_push_front_arg*)arg;
  a->push_front_result = int_vec_transient_push_front(a->transient, 999);
  return NULL;
}

/* Test: push_front from non-owner thread returns NULL */
int test_ownership_push_front_wrong_thread(void) {
  printf("  test_ownership_push_front_wrong_thread: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  thread_push_front_arg arg = {.transient = t, .push_front_result = (int_vec_transient*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_push_front_worker, &arg);
  THREAD_JOIN(th, NULL);

  ASSERT(arg.push_front_result == NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 10);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Worker for pop_front test */
typedef struct {
  int_vec_transient* transient;
  int_vec_transient_pop_result pop_front_result;
} thread_pop_front_arg;

static void* thread_pop_front_worker(void* arg) {
  thread_pop_front_arg* a = (thread_pop_front_arg*)arg;
  a->pop_front_result = int_vec_transient_pop_front(a->transient);
  return NULL;
}

/* Test: pop_front from non-owner thread returns NULL */
int test_ownership_pop_front_wrong_thread(void) {
  printf("  test_ownership_pop_front_wrong_thread: ");
  tracker_reset();

  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }

  int_vec_transient* t = int_vec_transient_fn(v);
  ASSERT(t != NULL);

  thread_pop_front_arg arg;
  arg.transient = t;
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_pop_front_worker, &arg);
  THREAD_JOIN(th, NULL);

  ASSERT(!arg.pop_front_result.found);
  ASSERT(arg.pop_front_result.transient == NULL);

  int_vec_root* v2 = int_vec_persistent_fn(t);
  ASSERT(v2 != NULL);
  ASSERT_INT_EQ((int)int_vec_count(v2), 10);

  int_vec_unref(v);
  int_vec_unref(v2);
  int_vec_rc_unref(t);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  int pass = 0, fail = 0;

  printf("rrb_vec ownership tests:\n");

  test_ownership_push_back_wrong_thread()   ? pass++ : fail++;
  test_ownership_persist_wrong_thread()     ? pass++ : fail++;
  test_ownership_double_persist()           ? pass++ : fail++;
  test_ownership_set_wrong_thread()         ? pass++ : fail++;
  test_ownership_pop_back_wrong_thread()    ? pass++ : fail++;
  test_ownership_push_front_wrong_thread()  ? pass++ : fail++;
  test_ownership_pop_front_wrong_thread()   ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
