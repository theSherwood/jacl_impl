/*
 * test_chase_lev_stress.c — Multi-threaded stress tests for Chase-Lev deque.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

/* Instantiate deque for int with tracked allocator */
#define CHASE_LEV_T       int
#define CHASE_LEV_NAME    int_cl
#define CHASE_LEV_MALLOC(sz) tracked_malloc(sz)
#define CHASE_LEV_FREE(p)    tracked_free(p)
#include "chase_lev.h"

/* ---- US-008: 1 owner + 1 thief ---- */

#define STRESS_N 100000

typedef struct {
  int_cl_deque*  dq;
  int*           stolen_values;
  int            stolen_count;
  volatile int   owner_done;
} stress_1t_ctx;

static THREAD_PROC_RETURN THREAD_PROC_TYPE thief_fn(void* arg) {
  stress_1t_ctx* ctx = (stress_1t_ctx*)arg;
  int tid = int_cl_deque_register_thief(ctx->dq);
  int count = 0;

  for (;;) {
    int val;
    if (int_cl_deque_steal(ctx->dq, &val, tid)) {
      ctx->stolen_values[count++] = val;
    } else if (ATOMIC_LOAD_EXPLICIT(&ctx->owner_done, MEM_ACQUIRE)) {
      /* Owner finished — drain remaining with sleep backoff */
      int misses = 0;
      while (misses < 128) {
        if (int_cl_deque_steal(ctx->dq, &val, tid)) {
          ctx->stolen_values[count++] = val;
          misses = 0;
        } else {
          misses++;
          if (misses >= 64) {
            SLEEP_MILLISECONDS(1);
          }
        }
      }
      break;
    }
  }

  ctx->stolen_count = count;
  int_cl_deque_unregister_thief(ctx->dq, tid);
  return (THREAD_PROC_RETURN)0;
}

static int test_stress_1owner_1thief(void) {
  tracker_reset();

  int_cl_deque* dq = int_cl_deque_new(3); /* capacity 8, will resize many times */
  if (!dq) return 0;

  stress_1t_ctx ctx;
  ctx.dq            = dq;
  ctx.stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
  ctx.stolen_count  = 0;
  ctx.owner_done    = 0;

  int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
  int  taken_count  = 0;

  /* Launch thief */
  thread_t thief;
  THREAD_CREATE(&thief, NULL, thief_fn, &ctx);

  /* Owner: push N items, interleave some takes */
  for (int i = 0; i < STRESS_N; i++) {
    int_cl_deque_push(dq, i);

    /* Every 7th push, try to take one back */
    if (i % 7 == 0) {
      int val;
      if (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }
  }

  /* Owner: drain remaining */
  {
    int val;
    while (int_cl_deque_take(dq, &val)) {
      taken_values[taken_count++] = val;
    }
  }

  /* Signal thief */
  ATOMIC_STORE_EXPLICIT(&ctx.owner_done, 1, MEM_RELEASE);

  /* Wait for thief */
  THREAD_JOIN(thief, NULL);

  /* Verify totals */
  int total = taken_count + ctx.stolen_count;
  if (total != STRESS_N) {
    fprintf(stderr, "  FAIL: total=%d (taken=%d + stolen=%d), expected=%d\n",
            total, taken_count, ctx.stolen_count, STRESS_N);
    tracked_free(taken_values);
    tracked_free(ctx.stolen_values);
    int_cl_deque_free(dq);
    return 0;
  }

  /* Verify no duplicates via bitmap */
  uint8_t* seen = (uint8_t*)tracked_malloc(STRESS_N * sizeof(uint8_t));
  memset(seen, 0, STRESS_N * sizeof(uint8_t));
  int ok = 1;

  for (int i = 0; i < taken_count && ok; i++) {
    int v = taken_values[i];
    if (v < 0 || v >= STRESS_N || seen[v]) {
      fprintf(stderr, "  FAIL: duplicate or invalid taken value %d\n", v);
      ok = 0;
    }
    seen[v] = 1;
  }

  for (int i = 0; i < ctx.stolen_count && ok; i++) {
    int v = ctx.stolen_values[i];
    if (v < 0 || v >= STRESS_N || seen[v]) {
      fprintf(stderr, "  FAIL: duplicate or invalid stolen value %d\n", v);
      ok = 0;
    }
    seen[v] = 1;
  }

  for (int i = 0; i < STRESS_N && ok; i++) {
    if (!seen[i]) {
      fprintf(stderr, "  FAIL: value %d was lost\n", i);
      ok = 0;
    }
  }

  tracked_free(seen);
  tracked_free(taken_values);
  tracked_free(ctx.stolen_values);
  int_cl_deque_free(dq);

  if (ok) {
    ASSERT(check_no_leaks());
  }

  return ok;
}

/* ---- US-009: 1 owner + N thieves (generic) ---- */

#define STRESS_REPEAT 3

typedef struct {
  int_cl_deque*  dq;
  int*           stolen_values;
  int            stolen_count;
  volatile int   owner_done;
  int            thief_id;     /* for debugging */
} multi_thief_ctx;

static THREAD_PROC_RETURN THREAD_PROC_TYPE multi_thief_fn(void* arg) {
  multi_thief_ctx* ctx = (multi_thief_ctx*)arg;
  int tid = int_cl_deque_register_thief(ctx->dq);
  int count = 0;

  for (;;) {
    int val;
    if (int_cl_deque_steal(ctx->dq, &val, tid)) {
      ctx->stolen_values[count++] = val;
    } else if (ATOMIC_LOAD_EXPLICIT(&ctx->owner_done, MEM_ACQUIRE)) {
      /* Owner finished — drain remaining with sleep backoff */
      int misses = 0;
      while (misses < 128) {
        if (int_cl_deque_steal(ctx->dq, &val, tid)) {
          ctx->stolen_values[count++] = val;
          misses = 0;
        } else {
          misses++;
          if (misses >= 64) {
            SLEEP_MILLISECONDS(1);
          }
        }
      }
      break;
    }
  }

  ctx->stolen_count = count;
  int_cl_deque_unregister_thief(ctx->dq, tid);
  return (THREAD_PROC_RETURN)0;
}

/* Helper: verify consumed values == [0, N) with no duplicates or losses */
static int verify_values(int** value_arrays, int* counts, int num_arrays, int N) {
  uint8_t* seen = (uint8_t*)tracked_malloc(N * sizeof(uint8_t));
  memset(seen, 0, N * sizeof(uint8_t));
  int ok = 1;
  int total = 0;

  for (int a = 0; a < num_arrays && ok; a++) {
    for (int i = 0; i < counts[a] && ok; i++) {
      int v = value_arrays[a][i];
      if (v < 0 || v >= N || seen[v]) {
        fprintf(stderr, "  FAIL: duplicate or invalid value %d (array %d)\n", v, a);
        ok = 0;
      }
      seen[v] = 1;
      total++;
    }
  }

  if (ok && total != N) {
    fprintf(stderr, "  FAIL: total=%d, expected=%d\n", total, N);
    ok = 0;
  }

  if (ok) {
    for (int i = 0; i < N; i++) {
      if (!seen[i]) {
        fprintf(stderr, "  FAIL: value %d was lost\n", i);
        ok = 0;
        break;
      }
    }
  }

  tracked_free(seen);
  return ok;
}

/* Test: 1 owner + 4 thieves */
static int test_stress_1owner_4thieves(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    int_cl_deque* dq = int_cl_deque_new(3);
    if (!dq) return 0;

    #define NUM_THIEVES_4 4
    multi_thief_ctx thief_ctx[NUM_THIEVES_4];
    thread_t thief_threads[NUM_THIEVES_4];

    for (int t = 0; t < NUM_THIEVES_4; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: push N items */
    for (int i = 0; i < STRESS_N; i++) {
      int_cl_deque_push(dq, i);
    }

    /* Owner: drain remaining */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_4; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_4; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_4 + 1];
    int  counts[NUM_THIEVES_4 + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_4; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_4 + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_4; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_4
  }
  return 1;
}

/* Test: 1 owner (interleaved push/take) + 2 thieves */
static int test_stress_interleaved_2thieves(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    int_cl_deque* dq = int_cl_deque_new(3);
    if (!dq) return 0;

    #define NUM_THIEVES_2 2
    multi_thief_ctx thief_ctx[NUM_THIEVES_2];
    thread_t thief_threads[NUM_THIEVES_2];

    for (int t = 0; t < NUM_THIEVES_2; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: push N items with interleaved takes */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;

    for (int i = 0; i < STRESS_N; i++) {
      int_cl_deque_push(dq, i);

      /* Every 3rd push, try to take one back */
      if (i % 3 == 0) {
        int val;
        if (int_cl_deque_take(dq, &val)) {
          taken_values[taken_count++] = val;
        }
      }
    }

    /* Owner: drain remaining */
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_2; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_2; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_2 + 1];
    int  counts[NUM_THIEVES_2 + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_2; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_2 + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_2; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_2
  }
  return 1;
}

/* Test: rapid push with small initial capacity (log_capacity=2, start=4) + 4 thieves causing many resizes */
static int test_stress_resize_under_load(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    /* log_capacity=2 means initial capacity of 4, forcing many resizes */
    int_cl_deque* dq = int_cl_deque_new(2);
    if (!dq) return 0;

    #define NUM_THIEVES_R 4
    multi_thief_ctx thief_ctx[NUM_THIEVES_R];
    thread_t thief_threads[NUM_THIEVES_R];

    for (int t = 0; t < NUM_THIEVES_R; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: rapid push with no takes — maximize resize pressure */
    for (int i = 0; i < STRESS_N; i++) {
      int_cl_deque_push(dq, i);
    }

    /* Owner: drain */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_R; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_R; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_R + 1];
    int  counts[NUM_THIEVES_R + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_R; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_R + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_R; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_R
  }
  return 1;
}

/* ---- US-002: near-empty contention (size==0 CAS race) ---- */

static int test_stress_near_empty_contention(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    int_cl_deque* dq = int_cl_deque_new(3);
    if (!dq) return 0;

    #define NUM_THIEVES_NE 4
    multi_thief_ctx thief_ctx[NUM_THIEVES_NE];
    thread_t thief_threads[NUM_THIEVES_NE];

    for (int t = 0; t < NUM_THIEVES_NE; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: push 1 item, immediately try to take it back */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;

    for (int i = 0; i < STRESS_N; i++) {
      int_cl_deque_push(dq, i);
      int val;
      if (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Owner: drain any remaining */
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_NE; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_NE; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_NE + 1];
    int  counts[NUM_THIEVES_NE + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_NE; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_NE + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_NE; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_NE
  }
  return 1;
}

/* ---- US-003: 8 thieves high-contention ---- */

static int test_stress_8thieves(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    int_cl_deque* dq = int_cl_deque_new(3);
    if (!dq) return 0;

    #define NUM_THIEVES_8 8
    multi_thief_ctx thief_ctx[NUM_THIEVES_8];
    thread_t thief_threads[NUM_THIEVES_8];

    for (int t = 0; t < NUM_THIEVES_8; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: push N items with interleaved takes (every 5th push) */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;

    for (int i = 0; i < STRESS_N; i++) {
      int_cl_deque_push(dq, i);

      if (i % 5 == 0) {
        int val;
        if (int_cl_deque_take(dq, &val)) {
          taken_values[taken_count++] = val;
        }
      }
    }

    /* Owner: drain remaining */
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_8; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_8; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_8 + 1];
    int  counts[NUM_THIEVES_8 + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_8; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_8 + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_8; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_8
  }
  return 1;
}

/* ---- US-004: resize while thieves are actively reading ---- */

static int test_stress_resize_during_steal(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    /* log_capacity=1 means initial capacity 2, forces resize on 3rd push */
    int_cl_deque* dq = int_cl_deque_new(1);
    if (!dq) return 0;

    #define NUM_THIEVES_RS 4
    multi_thief_ctx thief_ctx[NUM_THIEVES_RS];
    thread_t thief_threads[NUM_THIEVES_RS];

    for (int t = 0; t < NUM_THIEVES_RS; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: push in bursts that fill the buffer, then push 1 more to resize.
     * No takes between bursts — maximizes chance thieves are mid-read when
     * the buffer pointer changes. */
    int pushed = 0;
    while (pushed < STRESS_N) {
      /* Read current capacity from the buffer */
      int_cl_buffer* buf = ATOMIC_LOAD_EXPLICIT(&dq->buffer, MEM_ACQUIRE);
      uint64_t cap = (uint64_t)1 << buf->log_capacity;

      /* Push exactly cap items (or remaining, whichever is less) to fill buffer */
      uint64_t burst = cap;
      if (pushed + (int)burst + 1 > STRESS_N) {
        /* Not enough room for a full burst + resize trigger; just push the rest */
        while (pushed < STRESS_N) {
          int_cl_deque_push(dq, pushed);
          pushed++;
        }
        break;
      }

      for (uint64_t j = 0; j < burst; j++) {
        int_cl_deque_push(dq, pushed);
        pushed++;
      }

      /* Push 1 more to trigger resize while thieves may be reading old buffer */
      int_cl_deque_push(dq, pushed);
      pushed++;
    }

    /* Owner: drain remaining */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_RS; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_RS; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_RS + 1];
    int  counts[NUM_THIEVES_RS + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_RS; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_RS + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_RS; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_RS
  }
  return 1;
}

/* ---- US-005: rapid push-take oscillation ---- */

static int test_stress_push_take_oscillation(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    int_cl_deque* dq = int_cl_deque_new(3);
    if (!dq) return 0;

    #define NUM_THIEVES_OS 4
    multi_thief_ctx thief_ctx[NUM_THIEVES_OS];
    thread_t thief_threads[NUM_THIEVES_OS];

    for (int t = 0; t < NUM_THIEVES_OS; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: push k items (k cycles 1,2,3), take k-1 back, leaving 1 for
     * thieves. This keeps the deque oscillating between 0 and a small size,
     * exercising empty/non-empty boundary transitions. */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;
    int pushed = 0;
    int k_cycle = 0; /* cycles through 0,1,2 -> k = 1,2,3 */

    while (pushed < STRESS_N) {
      int k = (k_cycle % 3) + 1;
      k_cycle++;

      /* Push k items (or remaining) */
      int push_count = k;
      if (pushed + push_count > STRESS_N) {
        push_count = STRESS_N - pushed;
      }
      for (int j = 0; j < push_count; j++) {
        int_cl_deque_push(dq, pushed);
        pushed++;
      }

      /* Take k-1 items back (leave 1 for thieves) */
      int take_target = push_count - 1;
      for (int j = 0; j < take_target; j++) {
        int val;
        if (int_cl_deque_take(dq, &val)) {
          taken_values[taken_count++] = val;
        }
      }
    }

    /* Owner: drain remaining */
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_OS; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_OS; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_OS + 1];
    int  counts[NUM_THIEVES_OS + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_OS; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_OS + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_OS; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_OS
  }
  return 1;
}

/* ---- US-005: Epoch reclamation stress tests ---- */

/* Test 1: 1 owner + 4 registered thieves, log_capacity=1, 100K pushes.
 * Verifies no crashes, no memory leaks, total items correct. */
static int test_stress_epoch_4thieves_tiny(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    /* log_capacity=1 means initial capacity 2, forces many resizes and
     * epoch advances — exercises reclaim under heavy resize pressure */
    int_cl_deque* dq = int_cl_deque_new(1);
    if (!dq) return 0;

    #define NUM_THIEVES_EP4 4
    multi_thief_ctx thief_ctx[NUM_THIEVES_EP4];
    thread_t thief_threads[NUM_THIEVES_EP4];

    for (int t = 0; t < NUM_THIEVES_EP4; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      THREAD_CREATE(&thief_threads[t], NULL, multi_thief_fn, &thief_ctx[t]);
    }

    /* Owner: push 100K items */
    for (int i = 0; i < STRESS_N; i++) {
      int_cl_deque_push(dq, i);
    }

    /* Owner: drain remaining */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal thieves */
    for (int t = 0; t < NUM_THIEVES_EP4; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join thieves */
    for (int t = 0; t < NUM_THIEVES_EP4; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[NUM_THIEVES_EP4 + 1];
    int  counts[NUM_THIEVES_EP4 + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < NUM_THIEVES_EP4; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, NUM_THIEVES_EP4 + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < NUM_THIEVES_EP4; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_THIEVES_EP4
  }
  return 1;
}

/* Test 2: Thieves register, steal for a while, unregister mid-run,
 * new thieves register in their place. Verifies correctness. */

typedef struct {
  int_cl_deque*  dq;
  int*           stolen_values;
  int            stolen_count;
  volatile int   owner_done;
  int            thief_id;
  int            phase;         /* 0 = first wave, 1 = second wave */
  volatile int*  wave1_done;    /* signal for wave 1 thieves to stop early */
} rotating_thief_ctx;

static THREAD_PROC_RETURN THREAD_PROC_TYPE rotating_thief_fn(void* arg) {
  rotating_thief_ctx* ctx = (rotating_thief_ctx*)arg;
  int tid = int_cl_deque_register_thief(ctx->dq);
  int count = 0;

  if (ctx->phase == 0) {
    /* Wave 1: steal for a while, then unregister mid-run */
    for (int iter = 0; iter < 50000; iter++) {
      int val;
      if (int_cl_deque_steal(ctx->dq, &val, tid)) {
        ctx->stolen_values[count++] = val;
      }
      /* Check if owner signaled done before our quota */
      if (ATOMIC_LOAD_EXPLICIT(&ctx->owner_done, MEM_ACQUIRE)) break;
    }
    ctx->stolen_count = count;
    int_cl_deque_unregister_thief(ctx->dq, tid);
    /* Signal that wave 1 is done */
    ATOMIC_STORE_EXPLICIT(ctx->wave1_done, 1, MEM_RELEASE);
  } else {
    /* Wave 2: wait for wave1_done signal, then steal until owner_done */
    while (!ATOMIC_LOAD_EXPLICIT(ctx->wave1_done, MEM_ACQUIRE)) {
      SLEEP_MILLISECONDS(0);
    }

    for (;;) {
      int val;
      if (int_cl_deque_steal(ctx->dq, &val, tid)) {
        ctx->stolen_values[count++] = val;
      } else if (ATOMIC_LOAD_EXPLICIT(&ctx->owner_done, MEM_ACQUIRE)) {
        int misses = 0;
        while (misses < 128) {
          if (int_cl_deque_steal(ctx->dq, &val, tid)) {
            ctx->stolen_values[count++] = val;
            misses = 0;
          } else {
            misses++;
            if (misses >= 64) SLEEP_MILLISECONDS(1);
          }
        }
        break;
      }
    }
    ctx->stolen_count = count;
    int_cl_deque_unregister_thief(ctx->dq, tid);
  }

  return (THREAD_PROC_RETURN)0;
}

static int test_stress_epoch_thief_rotation(void) {
  for (int rep = 0; rep < STRESS_REPEAT; rep++) {
    tracker_reset();

    int_cl_deque* dq = int_cl_deque_new(2);
    if (!dq) return 0;

    #define NUM_WAVE 2
    #define TOTAL_THIEVES_ROT (NUM_WAVE * 2)
    volatile int wave1_done_flags[NUM_WAVE];
    for (int i = 0; i < NUM_WAVE; i++) wave1_done_flags[i] = 0;

    rotating_thief_ctx thief_ctx[TOTAL_THIEVES_ROT];
    thread_t thief_threads[TOTAL_THIEVES_ROT];

    /* Launch wave 1 thieves (will unregister mid-run) */
    for (int t = 0; t < NUM_WAVE; t++) {
      thief_ctx[t].dq            = dq;
      thief_ctx[t].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[t].stolen_count  = 0;
      thief_ctx[t].owner_done    = 0;
      thief_ctx[t].thief_id      = t;
      thief_ctx[t].phase         = 0;
      thief_ctx[t].wave1_done    = &wave1_done_flags[t];
      THREAD_CREATE(&thief_threads[t], NULL, rotating_thief_fn, &thief_ctx[t]);
    }

    /* Launch wave 2 thieves (will wait for wave 1 to finish, then register) */
    for (int t = 0; t < NUM_WAVE; t++) {
      int idx = NUM_WAVE + t;
      thief_ctx[idx].dq            = dq;
      thief_ctx[idx].stolen_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
      thief_ctx[idx].stolen_count  = 0;
      thief_ctx[idx].owner_done    = 0;
      thief_ctx[idx].thief_id      = idx;
      thief_ctx[idx].phase         = 1;
      thief_ctx[idx].wave1_done    = &wave1_done_flags[t];
      THREAD_CREATE(&thief_threads[idx], NULL, rotating_thief_fn, &thief_ctx[idx]);
    }

    /* Owner: push N items */
    for (int i = 0; i < STRESS_N; i++) {
      int_cl_deque_push(dq, i);
    }

    /* Owner: drain remaining */
    int* taken_values = (int*)tracked_malloc(STRESS_N * sizeof(int));
    int taken_count = 0;
    {
      int val;
      while (int_cl_deque_take(dq, &val)) {
        taken_values[taken_count++] = val;
      }
    }

    /* Signal all thieves that owner is done */
    for (int t = 0; t < TOTAL_THIEVES_ROT; t++) {
      ATOMIC_STORE_EXPLICIT(&thief_ctx[t].owner_done, 1, MEM_RELEASE);
    }

    /* Join all thieves */
    for (int t = 0; t < TOTAL_THIEVES_ROT; t++) {
      THREAD_JOIN(thief_threads[t], NULL);
    }

    /* Verify */
    int* arrays[TOTAL_THIEVES_ROT + 1];
    int  counts[TOTAL_THIEVES_ROT + 1];
    arrays[0] = taken_values;
    counts[0] = taken_count;
    for (int t = 0; t < TOTAL_THIEVES_ROT; t++) {
      arrays[t + 1] = thief_ctx[t].stolen_values;
      counts[t + 1] = thief_ctx[t].stolen_count;
    }

    int ok = verify_values(arrays, counts, TOTAL_THIEVES_ROT + 1, STRESS_N);

    tracked_free(taken_values);
    for (int t = 0; t < TOTAL_THIEVES_ROT; t++) {
      tracked_free(thief_ctx[t].stolen_values);
    }
    int_cl_deque_free(dq);

    if (!ok) {
      fprintf(stderr, "  (failed on repetition %d)\n", rep);
      return 0;
    }

    ASSERT(check_no_leaks());
    #undef NUM_WAVE
    #undef TOTAL_THIEVES_ROT
  }
  return 1;
}

/* Test 3: Owner pushes rapidly with no thieves registered — all retired
 * buffers should be freed immediately. Peak retired count stays at 0 or 1. */
static int test_stress_epoch_no_thieves_reclaim(void) {
  tracker_reset();

  /* log_capacity=1 — capacity 2, forces many resizes */
  int_cl_deque* dq = int_cl_deque_new(1);
  if (!dq) return 0;

  /* Push 100K items — no thieves registered, so reclaim inside push
   * should free every retired buffer immediately. */
  for (int i = 0; i < STRESS_N; i++) {
    int_cl_deque_push(dq, i);

    /* After each push, check retired list length.
     * With no thieves, it should always be 0 (reclaim frees immediately). */
    int retired_count = 0;
    int_cl_retired_buffer* node = dq->retired;
    while (node) {
      retired_count++;
      node = node->next;
    }
    if (retired_count > 1) {
      fprintf(stderr, "  FAIL: retired_count=%d after push %d (expected 0 or 1)\n",
              retired_count, i);
      int val;
      while (int_cl_deque_take(dq, &val)) {}
      int_cl_deque_free(dq);
      return 0;
    }
  }

  /* All retired buffers should be freed */
  ASSERT(dq->retired == NULL);

  /* Drain and cleanup */
  int val;
  while (int_cl_deque_take(dq, &val)) {}

  int_cl_deque_free(dq);
  ASSERT(check_no_leaks());
  return 1;
}

/* ---- Main ---- */

int main(void) {
  int pass = 0, fail = 0;

  typedef int (*test_fn)(void);
  struct { const char* name; test_fn fn; } tests[] = {
    {"stress_1owner_1thief",       test_stress_1owner_1thief},
    {"stress_1owner_4thieves",     test_stress_1owner_4thieves},
    {"stress_interleaved_2thieves", test_stress_interleaved_2thieves},
    {"stress_resize_under_load",   test_stress_resize_under_load},
    {"stress_near_empty_contention", test_stress_near_empty_contention},
    {"stress_8thieves",              test_stress_8thieves},
    {"stress_resize_during_steal",    test_stress_resize_during_steal},
    {"stress_push_take_oscillation",  test_stress_push_take_oscillation},
    {"stress_epoch_4thieves_tiny",    test_stress_epoch_4thieves_tiny},
    {"stress_epoch_thief_rotation",   test_stress_epoch_thief_rotation},
    {"stress_epoch_no_thieves_reclaim", test_stress_epoch_no_thieves_reclaim},
  };
  int n = (int)(sizeof(tests) / sizeof(tests[0]));

  for (int i = 0; i < n; i++) {
    printf("  %-32s ", tests[i].name);
    if (tests[i].fn()) {
      printf("PASS\n");
      pass++;
    } else {
      printf("FAIL\n");
      fail++;
    }
  }

  printf("\nchase_lev_stress: %d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
