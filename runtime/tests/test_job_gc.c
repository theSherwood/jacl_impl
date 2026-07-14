/* Jobs are ordinary GC objects (no registry): prove RECLAMATION and HELD-FUTURE liveness.
 *
 * R rounds x M spawned jobs, awaited each round, with forced collections throughout — then two
 * final collections. If completed jobs were force-rooted (the old registry) or leaked, ~R*M job
 * cells stay live; with jobs as plain GC objects the dead rounds are swept, so the final live
 * count stays far below R*M. Meanwhile a future `held` from round 0 is kept across every
 * collection and re-awaited at the end — its cached result must survive (a held future is
 * rooted via this fiber's scanned stack, svm >= #217).
 *
 * run() returns 555 on success; 900000+live if too many cells survive (leak / no reclamation);
 * 800000+got if the held future's re-await went wrong; 700000+round*100+i on a bad sum. */
#include "jaclrt.h"

#define ROUNDS 16
#define M      16

static long jg_block(long self) {
  JaclVal c = (JaclVal)self;
  long arg = (long)(uint64_t)jacl_closure_upval(c, (JaclVal)0);
  JaclObj *g = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
  if (g) *(long *)jacl_obj_payload(g) = arg;
  return arg * 3 + 1;
}

static JaclVal jg_spawn_one(long v) {
  JaclVal cl = jacl_closure_new((JaclVal)(uintptr_t)jg_block, (JaclVal)1);
  cl = jacl_closure_set(cl, (JaclVal)0, (JaclVal)(uint64_t)v);
  return jacl_spawn(cl);
}

static long jg_root(long self) {
  (void)self;
  JaclVal held = jg_spawn_one(41);                    /* round-0 future, held across all GCs */
  long want_held = 41 * 3 + 1;
  if ((long)jacl_await(held) != want_held) return -800001;
  for (long r = 0; r < ROUNDS; r++) {
    JaclVal futs[M];
    for (long i = 0; i < M; i++) futs[i] = jg_spawn_one(r * M + i);
    jacl_gc_collect();                                /* stress: collect with all M in flight */
    for (long i = 0; i < M; i++) {
      long got = (long)jacl_await(futs[i]);
      if (got != (r * M + i) * 3 + 1) return -(700000 + r * 100 + i);
    }
    jacl_gc_collect();                                /* the M completed jobs are now garbage */
  }
  long again = (long)jacl_await(held);                /* re-await the held round-0 future */
  if (again != want_held) return -(800000 + (again & 0xFF));
  jacl_gc_collect();
  jacl_gc_collect();
  long live = jacl_live_count();
  /* Without reclamation >= ROUNDS*M (256) job cells survive; allow slack for the held future,
   * scheduler state, and a few conservative stale-pointer survivors. */
  if (live > (ROUNDS * M) / 2) return -(900000 + live);
  return 555;
}

int run(int n) {
  (void)n;
  jacl_heap_init();
  long r = (long)jacl_sched_run_main((JaclVal)(uintptr_t)jg_root);
  if (r < 0) return (int)-r;
  if (jacl_gc_violation_count() != 0) return 600000 + (int)jacl_gc_violation_count();
  return (int)r;
}

#include "jaclrt.c"
