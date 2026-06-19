/* P3.4b — concurrent allocation across real vCPU workers (thread.spawn).
 *
 * N worker vCPUs allocate M objects each, simultaneously, into their own per-vCPU
 * heaps (heap_gc.c P3.4a). Each worker stamps every object's payload with a unique
 * value (worker_id * STRIDE + i). The root joins all workers and verifies:
 *   - the heap holds exactly N*M live cells (no lost / double-counted allocations);
 *   - every object still carries its own stamp (any overlap/corruption from a missing
 *     lock would clobber a stamp and be caught).
 * The allocator's only cross-worker state is the atomic region-pool cursor and the
 * region-owner table (distinct regions per worker); per-worker bump pointers, free
 * lists, and start-bitmap bytes are single-owner, so correct concurrent allocation
 * proves the lock-free fast path. Returns 777 on success.
 *
 * `run` is module function 0; the worker + runtime impl follow. */
#include "jaclrt.h"

int  __vm_thread_spawn(long (*fn)(long), void *stack, long arg);
long __vm_thread_join(int h);
long __vm_vcpu_tls_get(void);

#define MT_WORKERS 4
#define MT_PER     200
#define MT_MAXW    8           /* vCPU ids stay well under this */
#define MT_STRIDE  1000000L

static JaclObj *mt_ptr[MT_MAXW][MT_PER];   /* each worker's allocated cells */
static long     mt_count[MT_MAXW];         /* #cells a worker actually allocated */

long mt_worker(long arg);

int run(int n) {
  (void)n;
  jacl_heap_init();

  int h[MT_WORKERS];
  for (int k = 0; k < MT_WORKERS; k++)
    h[k] = __vm_thread_spawn(mt_worker, (void *)0, MT_PER);
  for (int k = 0; k < MT_WORKERS; k++)
    __vm_thread_join(h[k]);

  /* total allocated across workers */
  long total = 0;
  for (int w = 0; w < MT_MAXW; w++) total += mt_count[w];
  if (total != (long)MT_WORKERS * MT_PER) return 1000 + (int)total;
  if (jacl_live_count() != total)        return 2000 + (int)jacl_live_count();

  /* every object intact + carrying its own stamp (overlap would corrupt one) */
  for (int w = 0; w < MT_MAXW; w++) {
    for (long i = 0; i < mt_count[w]; i++) {
      JaclObj *o = mt_ptr[w][i];
      if (!o) return 3000 + w;
      long got = *(long *)jacl_obj_payload(o);
      if (got != (long)w * MT_STRIDE + i) return 4000 + w * 100 + (int)(i % 100);
    }
  }
  return 777;
}

/* Each worker runs on its own vCPU; its index is the per-vCPU TLS word (the same id
 * the allocator uses), so it bookkeeps under exactly the heap it allocates into. */
long mt_worker(long arg) {
  int w = (int)__vm_vcpu_tls_get();
  if (w < 0 || w >= MT_MAXW) return -1;
  long m = arg;
  for (long i = 0; i < m; i++) {
    JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)sizeof(long));
    if (!o) return -2;
    *(long *)jacl_obj_payload(o) = (long)w * MT_STRIDE + i;
    mt_ptr[w][i] = o;
  }
  mt_count[w] = m;
  return 0;
}

#include "jaclrt.c"   /* runtime impl last, so run() is module function 0 */
