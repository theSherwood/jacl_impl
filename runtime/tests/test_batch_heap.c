/* P3.4d — the batch scheduler roots in-flight heap args + heap results across a collection.
 *
 * Each task receives a heap "arg blob" (stamped), allocates a heap "result blob" derived
 * from it, churns garbage while triggering collections, then returns the result blob. The
 * arg blob (held only in the global batch-arg buffer once the task starts) and the returned
 * result blob (held only in the global batch-result buffer between the task returning and
 * the caller consuming it) must survive the mid-task collections — which they do only
 * because run_batch keeps them in markable global buffers (jacl_sched_mark_roots). Without
 * that rooting a later task's collection would reclaim them and a stamp check would fail.
 * Returns 808.
 *
 * `run` is module function 0; the task and the runtime follow. */
#include "jaclrt.h"

#define NT        6
#define NW        3
#define BI_ITERS  60
#define BI_STRIDE 1000L

long bi_task(long arg);   /* defined after run() so run() is module function 0 */

int run(int n) {
  (void)n;
  jacl_heap_init();

  JaclTaskFn fn[NT];
  long arg[NT], res[NT];
  for (int i = 0; i < NT; i++) {
    JaclObj *a = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
    *(long *)jacl_obj_payload(a) = (long)i * BI_STRIDE + 7;
    fn[i] = bi_task; arg[i] = (long)a; res[i] = 0;
  }
  jacl_sched_run_batch(fn, arg, res, NT, NW);

  int ok = (jacl_gc_violation_count() == 0);
  for (int i = 0; i < NT; i++) {
    JaclObj *r = (JaclObj *)res[i];
    if (!r) { ok = 0; break; }
    if (*(long *)jacl_obj_payload(r) != (long)i * BI_STRIDE + 7 + 1) { ok = 0; break; }
  }
  return ok ? 808 : -1;
}

long bi_task(long arg) {
  JaclObj *a = (JaclObj *)arg;                       /* heap arg blob (batch arg) */
  long argstamp = *(long *)jacl_obj_payload(a);
  JaclObj *r = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));   /* heap result */
  *(long *)jacl_obj_payload(r) = argstamp + 1;
  for (long i = 0; i < BI_ITERS; i++) {
    JaclObj *g = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));  /* garbage */
    if (g) *(long *)jacl_obj_payload(g) = i;
    if ((i % 15) == 0) jacl_gc_collect();
    if (*(long *)jacl_obj_payload(a) != argstamp) return 0;        /* arg blob corrupted */
    if (*(long *)jacl_obj_payload(r) != argstamp + 1) return 0;    /* result blob corrupted */
  }
  return (long)r;
}

#include "jaclrt.c"   /* runtime impl last, so run() is module function 0 */
