#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/test_helpers.h"

/* Hook tracked allocator before including chase_lev.h */
#define CHASE_LEV_MALLOC(sz) tracked_malloc(sz)
#define CHASE_LEV_FREE(p)    tracked_free(p)

/* Instantiate for int */
#define CHASE_LEV_T    int
#define CHASE_LEV_NAME int_cl
#include "chase_lev.h"

/* Instantiate for a struct to verify polymorphism */
typedef struct {
  int x;
  int y;
} Point;

#define CHASE_LEV_T    Point
#define CHASE_LEV_NAME point_cl
#include "chase_lev.h"

/* ---- int tests ---- */

static int test_take_empty(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  int val = -1;
  ASSERT(int_cl_deque_take(dq, &val) == false);

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_steal_empty(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  int tid = int_cl_deque_register_thief(dq);
  ASSERT(tid >= 0);

  int val = -1;
  ASSERT(int_cl_deque_steal(dq, &val, tid) == false);

  int_cl_deque_unregister_thief(dq, tid);
  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_push_take_lifo(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3); /* capacity 8 */
  ASSERT(dq != NULL);

  int n = 8;
  for (int i = 0; i < n; i++) {
    int_cl_deque_push(dq, i);
  }

  /* Take returns in LIFO order (bottom to top) */
  for (int i = n - 1; i >= 0; i--) {
    int val = -1;
    ASSERT(int_cl_deque_take(dq, &val) == true);
    ASSERT_INT_EQ(val, i);
  }

  /* Now empty */
  int val;
  ASSERT(int_cl_deque_take(dq, &val) == false);

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_push_steal_fifo(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3); /* capacity 8 */
  ASSERT(dq != NULL);

  int tid = int_cl_deque_register_thief(dq);
  ASSERT(tid >= 0);

  int n = 8;
  for (int i = 0; i < n; i++) {
    int_cl_deque_push(dq, i);
  }

  /* Steal returns in FIFO order (top to bottom) */
  for (int i = 0; i < n; i++) {
    int val = -1;
    ASSERT(int_cl_deque_steal(dq, &val, tid) == true);
    ASSERT_INT_EQ(val, i);
  }

  /* Now empty */
  int val;
  ASSERT(int_cl_deque_steal(dq, &val, tid) == false);

  int_cl_deque_unregister_thief(dq, tid);
  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interleaved_push_take(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  /* Push 3, take 1, push 2, take all */
  int_cl_deque_push(dq, 10);
  int_cl_deque_push(dq, 20);
  int_cl_deque_push(dq, 30);

  int val;
  ASSERT(int_cl_deque_take(dq, &val) == true);
  ASSERT_INT_EQ(val, 30); /* LIFO */

  int_cl_deque_push(dq, 40);
  int_cl_deque_push(dq, 50);

  /* Deque now: 10, 20, 40, 50 (bottom=50) */
  ASSERT(int_cl_deque_take(dq, &val) == true);
  ASSERT_INT_EQ(val, 50);
  ASSERT(int_cl_deque_take(dq, &val) == true);
  ASSERT_INT_EQ(val, 40);
  ASSERT(int_cl_deque_take(dq, &val) == true);
  ASSERT_INT_EQ(val, 20);
  ASSERT(int_cl_deque_take(dq, &val) == true);
  ASSERT_INT_EQ(val, 10);

  ASSERT(int_cl_deque_take(dq, &val) == false);

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_resize_single(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(2); /* capacity 4 */
  ASSERT(dq != NULL);

  /* Push beyond initial capacity to trigger resize */
  int n = 8;
  for (int i = 0; i < n; i++) {
    int_cl_deque_push(dq, i * 10);
  }

  /* All items should still be correct (LIFO via take) */
  for (int i = n - 1; i >= 0; i--) {
    int val;
    ASSERT(int_cl_deque_take(dq, &val) == true);
    ASSERT_INT_EQ(val, i * 10);
  }

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_resize_multiple(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(1); /* capacity 2 — will resize many times */
  ASSERT(dq != NULL);

  int tid = int_cl_deque_register_thief(dq);
  ASSERT(tid >= 0);

  int n = 100;
  for (int i = 0; i < n; i++) {
    int_cl_deque_push(dq, i);
  }

  /* Verify via steal (FIFO) */
  for (int i = 0; i < n; i++) {
    int val;
    ASSERT(int_cl_deque_steal(dq, &val, tid) == true);
    ASSERT_INT_EQ(val, i);
  }

  int val;
  ASSERT(int_cl_deque_steal(dq, &val, tid) == false);

  int_cl_deque_unregister_thief(dq, tid);
  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_resize_interleaved(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(1); /* capacity 2 */
  ASSERT(dq != NULL);

  /* Push and take interleaved across resizes */
  for (int round = 0; round < 5; round++) {
    for (int i = 0; i < 10; i++) {
      int_cl_deque_push(dq, round * 100 + i);
    }
    for (int i = 9; i >= 5; i--) {
      int val;
      ASSERT(int_cl_deque_take(dq, &val) == true);
      ASSERT_INT_EQ(val, round * 100 + i);
    }
  }

  /* Drain remaining via steal */
  int tid = int_cl_deque_register_thief(dq);
  ASSERT(tid >= 0);
  int count = 0;
  int val;
  while (int_cl_deque_steal(dq, &val, tid)) {
    count++;
  }
  /* 5 rounds * 5 remaining per round = 25 */
  ASSERT_INT_EQ(count, 25);

  int_cl_deque_unregister_thief(dq, tid);
  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Point (struct) tests ---- */

static int test_point_push_take(void) {
  tracker_reset();
  point_cl_deque* dq = point_cl_deque_new(3);
  ASSERT(dq != NULL);

  Point p1 = {1, 2};
  Point p2 = {3, 4};
  Point p3 = {5, 6};
  point_cl_deque_push(dq, p1);
  point_cl_deque_push(dq, p2);
  point_cl_deque_push(dq, p3);

  Point out;
  ASSERT(point_cl_deque_take(dq, &out) == true);
  ASSERT_INT_EQ(out.x, 5);
  ASSERT_INT_EQ(out.y, 6);

  ASSERT(point_cl_deque_take(dq, &out) == true);
  ASSERT_INT_EQ(out.x, 3);
  ASSERT_INT_EQ(out.y, 4);

  ASSERT(point_cl_deque_take(dq, &out) == true);
  ASSERT_INT_EQ(out.x, 1);
  ASSERT_INT_EQ(out.y, 2);

  ASSERT(point_cl_deque_take(dq, &out) == false);

  point_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_point_push_steal(void) {
  tracker_reset();
  point_cl_deque* dq = point_cl_deque_new(2);
  ASSERT(dq != NULL);

  int tid = point_cl_deque_register_thief(dq);
  ASSERT(tid >= 0);

  for (int i = 0; i < 8; i++) {
    Point p = {i, i * 10};
    point_cl_deque_push(dq, p);
  }

  /* Steal returns FIFO */
  for (int i = 0; i < 8; i++) {
    Point out;
    ASSERT(point_cl_deque_steal(dq, &out, tid) == true);
    ASSERT_INT_EQ(out.x, i);
    ASSERT_INT_EQ(out.y, i * 10);
  }

  Point out;
  ASSERT(point_cl_deque_steal(dq, &out, tid) == false);

  point_cl_deque_unregister_thief(dq, tid);
  point_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_free_no_leaks(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(1);
  ASSERT(dq != NULL);

  /* Push enough to cause several resizes, then free without draining */
  for (int i = 0; i < 50; i++) {
    int_cl_deque_push(dq, i);
  }

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Thief registration tests ---- */

static int test_register_thief(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  /* Register a thief and get a valid ID */
  int id = int_cl_deque_register_thief(dq);
  ASSERT(id >= 0);
  ASSERT_INT_EQ(id, 0);

  /* Register another thief */
  int id2 = int_cl_deque_register_thief(dq);
  ASSERT(id2 >= 0);
  ASSERT_INT_EQ(id2, 1);

  /* Unregister and verify no crash */
  int_cl_deque_unregister_thief(dq, id);
  int_cl_deque_unregister_thief(dq, id2);

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_register_thief_max(void) {
  tracker_reset();

  /* Use a small max to test the limit — instantiate with custom max */
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  /* Register up to CL_DEQUE_MAX_THIEVES (64) */
  int ids[64];
  for (int i = 0; i < 64; i++) {
    ids[i] = int_cl_deque_register_thief(dq);
    ASSERT(ids[i] >= 0);
    ASSERT_INT_EQ(ids[i], i);
  }

  /* 65th should fail */
  int overflow = int_cl_deque_register_thief(dq);
  ASSERT_INT_EQ(overflow, -1);

  /* Unregister all */
  for (int i = 0; i < 64; i++) {
    int_cl_deque_unregister_thief(dq, ids[i]);
  }

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_unregister_thief_sentinel(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  int id = int_cl_deque_register_thief(dq);
  ASSERT(id >= 0);

  /* After registration, slot should be inactive (UINT64_MAX) */
  ASSERT(dq->thieves[id].epoch == UINT64_MAX);

  /* Unregister — slot should still be UINT64_MAX */
  int_cl_deque_unregister_thief(dq, id);
  ASSERT(dq->thieves[id].epoch == UINT64_MAX);

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_epoch_initial_state(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  /* Global epoch starts at 0 */
  ASSERT(dq->epoch == 0);

  /* All thief slots should be inactive */
  for (int i = 0; i < 64; i++) {
    ASSERT(dq->thieves[i].epoch == UINT64_MAX);
  }

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Epoch annotation tests ---- */

static int test_steal_epoch_annotation(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(3);
  ASSERT(dq != NULL);

  int tid = int_cl_deque_register_thief(dq);
  ASSERT(tid >= 0);

  /* Before steal, slot should be inactive */
  ASSERT(dq->thieves[tid].epoch == UINT64_MAX);

  /* Push an element and steal it */
  int_cl_deque_push(dq, 42);
  int val = -1;
  ASSERT(int_cl_deque_steal(dq, &val, tid) == true);
  ASSERT_INT_EQ(val, 42);

  /* After steal completes, slot should be back to inactive sentinel */
  ASSERT(dq->thieves[tid].epoch == UINT64_MAX);

  /* Failed steal (empty) should also leave slot inactive */
  ASSERT(int_cl_deque_steal(dq, &val, tid) == false);
  ASSERT(dq->thieves[tid].epoch == UINT64_MAX);

  int_cl_deque_unregister_thief(dq, tid);
  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Reclamation tests ---- */

static int test_reclaim_with_thief(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(1); /* capacity 2 — resizes often */
  ASSERT(dq != NULL);

  int tid = int_cl_deque_register_thief(dq);
  ASSERT(tid >= 0);

  /* Push enough to resize several times (capacity 2 -> 4 -> 8 -> 16 -> 32).
   * Each resize retires a buffer. With the thief registered but inactive
   * (sentinel), reclaim should free all retired buffers immediately. */
  for (int i = 0; i < 30; i++) {
    int_cl_deque_push(dq, i);
  }

  /* Thief is inactive (sentinel) — reclaim should have freed all retired buffers */
  ASSERT(dq->retired == NULL);

  /* Now simulate thief in critical section: write a non-sentinel epoch */
  dq->thieves[tid].epoch = 0;

  /* Push more to trigger more resizes — retired buffers should NOT be freed
   * because the thief is observing epoch 0 */
  for (int i = 30; i < 60; i++) {
    int_cl_deque_push(dq, i);
  }

  /* Some retired buffers should remain because thief holds epoch 0 */
  /* (The ones with retire_epoch >= 0 are held back since thief sees epoch 0,
   *  meaning min_epoch=0, and retire_epoch < 0 is impossible for uint64_t,
   *  so nothing gets freed.) */
  ASSERT(dq->retired != NULL);

  /* Thief exits critical section */
  dq->thieves[tid].epoch = UINT64_MAX;

  /* Explicitly reclaim — now all should be freed */
  int_cl_deque_reclaim(dq);
  ASSERT(dq->retired == NULL);

  /* Drain and cleanup */
  int val;
  while (int_cl_deque_take(dq, &val)) {}

  int_cl_deque_unregister_thief(dq, tid);
  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_reclaim_no_thieves(void) {
  tracker_reset();
  int_cl_deque* dq = int_cl_deque_new(1); /* capacity 2 */
  ASSERT(dq != NULL);

  /* No thieves registered — all retired buffers should be freed immediately
   * on each resize (reclaim is called from push). */
  for (int i = 0; i < 30; i++) {
    int_cl_deque_push(dq, i);
  }

  /* All retired buffers should have been reclaimed */
  ASSERT(dq->retired == NULL);

  /* Drain and cleanup */
  int val;
  while (int_cl_deque_take(dq, &val)) {}

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Main ---- */

int main(void) {
  int pass = 0, fail = 0;

  typedef int (*test_fn)(void);
  struct { const char* name; test_fn fn; } tests[] = {
    {"take_empty",          test_take_empty},
    {"steal_empty",         test_steal_empty},
    {"push_take_lifo",      test_push_take_lifo},
    {"push_steal_fifo",     test_push_steal_fifo},
    {"interleaved",         test_interleaved_push_take},
    {"resize_single",       test_resize_single},
    {"resize_multiple",     test_resize_multiple},
    {"resize_interleaved",  test_resize_interleaved},
    {"point_push_take",     test_point_push_take},
    {"point_push_steal",    test_point_push_steal},
    {"free_no_leaks",       test_free_no_leaks},
    {"register_thief",      test_register_thief},
    {"register_thief_max",  test_register_thief_max},
    {"unregister_sentinel", test_unregister_thief_sentinel},
    {"epoch_initial_state", test_epoch_initial_state},
    {"steal_epoch_annot",   test_steal_epoch_annotation},
    {"reclaim_with_thief",  test_reclaim_with_thief},
    {"reclaim_no_thieves",  test_reclaim_no_thieves},
  };
  int n = (int)(sizeof(tests) / sizeof(tests[0]));

  for (int i = 0; i < n; i++) {
    printf("  %-24s ", tests[i].name);
    if (tests[i].fn()) {
      pass++;
    } else {
      fail++;
    }
  }

  printf("\nchase_lev: %d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
