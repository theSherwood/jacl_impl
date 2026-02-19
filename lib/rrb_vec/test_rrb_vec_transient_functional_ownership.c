/* test_rrb_vec_transient_functional_ownership.c
 *
 * Thread ownership enforcement tests for transient functional operations
 * (iter, for_each, map, filter).
 *
 * Compiled with -DNDEBUG so ownership violations return error values
 * instead of aborting via assert().
 */
#ifndef NDEBUG
#define NDEBUG
#endif

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

/* --- Helpers --- */

static int_vec_transient* make_transient_10(int_vec_root** out_root) {
  int_vec_root* v = int_vec_empty();
  for (int i = 0; i < 10; i++) {
    int_vec_root* next = int_vec_push_back(v, i);
    int_vec_unref(v);
    v = next;
  }
  int_vec_transient* t = int_vec_transient_fn(v);
  *out_root = v;
  return t;
}

/* --- Thread workers --- */

/* map worker */
typedef struct {
  int_vec_transient* transient;
  int_vec_transient* result;
} thread_map_arg;

static int double_val(int x) { return x * 2; }

static void* thread_map_worker(void* arg) {
  thread_map_arg* a = (thread_map_arg*)arg;
  a->result = int_vec_transient_map(a->transient, double_val);
  return NULL;
}

/* for_each worker */
typedef struct {
  int_vec_transient* transient;
  int callback_count;
} thread_for_each_arg;

static thread_for_each_arg* g_for_each_arg;

static void for_each_counter(int val) {
  (void)val;
  g_for_each_arg->callback_count++;
}

static void* thread_for_each_worker(void* arg) {
  thread_for_each_arg* a = (thread_for_each_arg*)arg;
  g_for_each_arg = a;
  int_vec_transient_for_each(a->transient, for_each_counter);
  return NULL;
}

/* filter worker */
typedef struct {
  int_vec_transient* transient;
  int_vec_transient* result;
} thread_filter_arg;

static bool is_even(int x) { return x % 2 == 0; }

static void* thread_filter_worker(void* arg) {
  thread_filter_arg* a = (thread_filter_arg*)arg;
  a->result = int_vec_transient_filter(a->transient, is_even);
  return NULL;
}

/* iter worker */
typedef struct {
  int_vec_transient* transient;
  bool first_done;
} thread_iter_arg;

static void* thread_iter_worker(void* arg) {
  thread_iter_arg* a = (thread_iter_arg*)arg;
  int_vec_transient_iter it = int_vec_transient_iter_init(a->transient);
  int_vec_iter_result ir = int_vec_transient_iter_next(&it);
  a->first_done = ir.done;
  return NULL;
}

/* === Tests === */

/* Test: transient_map from non-owner thread returns NULL */
int test_ownership_map_wrong_thread(void) {
  printf("  test_ownership_map_wrong_thread: ");
  tracker_reset();

  int_vec_root* v;
  int_vec_transient* t = make_transient_10(&v);

  thread_map_arg arg = {.transient = t, .result = (int_vec_transient*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_map_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner map should return NULL */
  ASSERT(arg.result == NULL);

  /* Transient should still be usable by owner */
  int_vec_transient* r = int_vec_transient_push_back(t, 99);
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

/* Test: transient_for_each from non-owner thread — callback never called */
int test_ownership_for_each_wrong_thread(void) {
  printf("  test_ownership_for_each_wrong_thread: ");
  tracker_reset();

  int_vec_root* v;
  int_vec_transient* t = make_transient_10(&v);

  thread_for_each_arg arg = {.transient = t, .callback_count = 0};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_for_each_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Callback should never have been called */
  ASSERT_INT_EQ(arg.callback_count, 0);

  /* Transient should still be usable by owner */
  int_vec_transient* r = int_vec_transient_push_back(t, 99);
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

/* Test: transient_filter from non-owner thread returns NULL */
int test_ownership_filter_wrong_thread(void) {
  printf("  test_ownership_filter_wrong_thread: ");
  tracker_reset();

  int_vec_root* v;
  int_vec_transient* t = make_transient_10(&v);

  thread_filter_arg arg = {.transient = t, .result = (int_vec_transient*)1};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_filter_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner filter should return NULL */
  ASSERT(arg.result == NULL);

  /* Transient should still be usable by owner */
  int_vec_transient* r = int_vec_transient_push_back(t, 99);
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

/* Test: transient_iter_init from non-owner thread — first next returns done */
int test_ownership_iter_wrong_thread(void) {
  printf("  test_ownership_iter_wrong_thread: ");
  tracker_reset();

  int_vec_root* v;
  int_vec_transient* t = make_transient_10(&v);

  thread_iter_arg arg = {.transient = t, .first_done = false};
  thread_t th;
  THREAD_CREATE(&th, NULL, thread_iter_worker, &arg);
  THREAD_JOIN(th, NULL);

  /* Non-owner iter should return done immediately */
  ASSERT(arg.first_done == true);

  /* Transient should still be usable by owner */
  int_vec_transient* r = int_vec_transient_push_back(t, 99);
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

int main(void) {
  int pass = 0, fail = 0;

  printf("rrb_vec transient functional ownership tests:\n");

  test_ownership_map_wrong_thread()       ? pass++ : fail++;
  test_ownership_for_each_wrong_thread()  ? pass++ : fail++;
  test_ownership_filter_wrong_thread()    ? pass++ : fail++;
  test_ownership_iter_wrong_thread()      ? pass++ : fail++;

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
