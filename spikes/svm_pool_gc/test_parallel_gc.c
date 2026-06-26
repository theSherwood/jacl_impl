/* INSTRUMENTED root-cause probe for the persistent-pool + GC corruption.
 * Classifies what happens to a block's keeper when it gets corrupted:
 *   category 1 = keeper cell is JOBJ_FREE   -> swept (missed root)
 *   category 2 = keeper cell is JOBJ_BLOB   -> overwritten in place (allocator aliasing)
 *   category 3 = keeper header is garbage   -> stomped
 * run() returns 555 if all good; else a diagnostic: 800000+violations if any STW violation,
 * 900000+i*100+cat for the first corrupted block i, or 700000+i for a non-negative wrong result. */
#include "jaclrt.h"
long __vm_vcpu_tls_get(void);

#define NT       12
#define SI_ITERS 40

static int  jacl_block_saw_zero;   /* set if any worker-run block observed vCPU id 0 */

static long par_block(long self) {
  if (__vm_vcpu_tls_get() == 0) jacl_block_saw_zero = 1;   /* a pool block on main's heap?! */
  JaclVal c = (JaclVal)self;
  long arg = (long)(uint64_t)jacl_closure_upval(c, (JaclVal)0);
  JaclObj *keeper = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
  long stamp = arg * 1000 + 7;
  *(long *)jacl_obj_payload(keeper) = stamp;
  for (long i = 0; i < SI_ITERS; i++) {
    JaclObj *g = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
    if (g) *(long *)jacl_obj_payload(g) = i;
    if ((i % 4) == 0) jacl_gc_collect();
    long got = *(long *)jacl_obj_payload(keeper);
    if (got != stamp) {
      int cat = (keeper->obj_type == JOBJ_FREE) ? 1
              : (keeper->obj_type == JOBJ_BLOB) ? 2 : 3;
      return -(arg * 10 + cat);     /* negative: encodes block + corruption category */
    }
  }
  return arg * arg;
}

static long par_root(long self) {
  (void)self;
  JaclVal blocks = jacl_vec_empty();
  for (long i = 0; i < NT; i++) {
    JaclVal cl = jacl_closure_new((JaclVal)(uintptr_t)par_block, (JaclVal)1);
    cl = jacl_closure_set(cl, (JaclVal)0, (JaclVal)(uint64_t)i);
    blocks = jacl_vec_push(blocks, cl);
  }
  JaclVal results = jacl_parallel(blocks);
  for (long i = 0; i < NT; i++) {
    long got = (long)jacl_vec_get(results, (uint32_t)i);
    if (got == i * i) continue;
    if (got < 0) { long code = -got; return 900000 + (code / 10) * 100 + (code % 10); } /* keeper corruption */
    return 100000 + i * 1000 + (int)(got & 0x3FF);   /* wrong result: encode block i and got */
  }
  return 555;
}

int run(int n) {
  (void)n;
  jacl_heap_init();
  long r = (long)jacl_sched_run_main((JaclVal)(uintptr_t)par_root);
  if (jacl_gc_violation_count() != 0) return 800000 + (int)jacl_gc_violation_count();
  if (jacl_block_saw_zero) return 600000 + (int)r;   /* a worker block ran on vCPU 0 (main's heap) */
  return (int)r;
}

#include "jaclrt.c"
