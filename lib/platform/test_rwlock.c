/* test_rwlock.c - Unit tests for platform_rwlock_t
 *
 * Verifies read-write lock: concurrent readers, exclusive writers.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../../test/test_helpers.h"

/* ---------- Shared state for threaded tests ---------- */

typedef struct {
  platform_rwlock_t rwlock;
  int               shared_value;
  int               readers_inside;  /* tracks concurrent readers */
  int               max_concurrent;  /* peak concurrent readers observed */
} RWLockTestState;

/* ---------- Test 1: Basic init/destroy and single-thread lock ---------- */

static int test_rwlock_basic(void) {
  platform_rwlock_t rw;
  RWLOCK_INIT(rw);

  /* Read lock / unlock */
  RWLOCK_RDLOCK(rw);
  RWLOCK_RDUNLOCK(rw);

  /* Write lock / unlock */
  RWLOCK_WRLOCK(rw);
  RWLOCK_WRUNLOCK(rw);

  RWLOCK_DESTROY(rw);
  TEST_PASS();
}

/* ---------- Test 2: Two concurrent readers ---------- */

static RWLockTestState g_state;

static THREAD_PROC_RETURN THREAD_PROC_TYPE reader_thread(void *arg) {
  int id = *(int *)arg;
  (void)id;

  RWLOCK_RDLOCK(g_state.rwlock);

  /* Increment readers_inside atomically */
  int cur = (int)ATOMIC_INC((intptr_t *)&g_state.readers_inside);
  /* Record peak */
  if (cur > g_state.max_concurrent) {
    g_state.max_concurrent = cur;
  }

  /* Hold the read lock briefly so the other reader can overlap */
  SLEEP_MILLISECONDS(50);

  ATOMIC_DEC((intptr_t *)&g_state.readers_inside);

  RWLOCK_RDUNLOCK(g_state.rwlock);
  return 0;
}

static int test_rwlock_concurrent_readers(void) {
  g_state.shared_value   = 42;
  g_state.readers_inside = 0;
  g_state.max_concurrent = 0;
  RWLOCK_INIT(g_state.rwlock);

  int id1 = 1, id2 = 2;
  thread_t t1, t2;
  THREAD_CREATE(&t1, NULL, reader_thread, &id1);
  THREAD_CREATE(&t2, NULL, reader_thread, &id2);

  THREAD_JOIN(t1, NULL);
  THREAD_JOIN(t2, NULL);

  /* Both readers should have been inside simultaneously */
  ASSERT(g_state.max_concurrent >= 2);

  RWLOCK_DESTROY(g_state.rwlock);
  TEST_PASS();
}

/* ---------- Test 3: Exclusive writer ---------- */

typedef struct {
  platform_rwlock_t rwlock;
  int               value;
  int               write_in_progress;
  int               saw_concurrent_write;
} WriterTestState;

static WriterTestState g_wstate;

static THREAD_PROC_RETURN THREAD_PROC_TYPE writer_thread(void *arg) {
  int iterations = *(int *)arg;

  for (int i = 0; i < iterations; i++) {
    RWLOCK_WRLOCK(g_wstate.rwlock);

    /* Check no other writer is inside */
    if (g_wstate.write_in_progress) {
      g_wstate.saw_concurrent_write = 1;
    }
    g_wstate.write_in_progress = 1;

    g_wstate.value++;

    g_wstate.write_in_progress = 0;
    RWLOCK_WRUNLOCK(g_wstate.rwlock);
  }
  return 0;
}

static int test_rwlock_exclusive_writer(void) {
  g_wstate.value                = 0;
  g_wstate.write_in_progress    = 0;
  g_wstate.saw_concurrent_write = 0;
  RWLOCK_INIT(g_wstate.rwlock);

  int iters = 1000;
  thread_t t1, t2;
  THREAD_CREATE(&t1, NULL, writer_thread, &iters);
  THREAD_CREATE(&t2, NULL, writer_thread, &iters);

  THREAD_JOIN(t1, NULL);
  THREAD_JOIN(t2, NULL);

  /* No concurrent writers should have been observed */
  ASSERT_INT_EQ(g_wstate.saw_concurrent_write, 0);
  /* Both threads should have completed all iterations */
  ASSERT_INT_EQ(g_wstate.value, 2000);

  RWLOCK_DESTROY(g_wstate.rwlock);
  TEST_PASS();
}

/* ---------- main ---------- */

int main(void) {
  int pass = 0, fail = 0;

  printf("=== platform_rwlock_t tests ===\n");

#define RUN(fn)                          \
  do {                                   \
    printf("  %-40s ", #fn);             \
    if (fn()) { pass++; }                \
    else      { fail++; }                \
  } while (0)

  RUN(test_rwlock_basic);
  RUN(test_rwlock_concurrent_readers);
  RUN(test_rwlock_exclusive_writer);

#undef RUN

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
