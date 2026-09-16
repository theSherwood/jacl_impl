/* jacl #141 — the negative half of `test_vcpu_stack.c`: the identical framed worker, spawned with
 * `(void *)0` as its data-stack base.
 *
 * `thread.spawn` reserves nothing for the new vCPU, so a base of 0 is not a stack and the first
 * frame the worker needs faults. That is what every spawn in `runtime/sched.c` used to do, and why
 * `sched_batch`, `batch_heap` and `gc_sched` failed with `MemoryFault` on **both** backends.
 *
 * This driver is expected to **trap**, so its return value is never reached. A trap terminates the
 * run and cannot be caught from inside the guest, which is why the assertion lives in the Rust
 * harness (`a_spawned_vcpu_needs_its_own_data_stack`) rather than here. If a future VM change makes
 * a NULL stack work, that test goes red and this ruling gets revisited on purpose.
 *
 * See `test_vcpu_stack.c` for the positive half and the full write-up. */
#include "jaclrt.h"

int  __vm_thread_spawn(long (*fn)(long), void *stack, long arg);
long __vm_thread_join(int h);

#define VS_STACK (1u << 16)
#define VS_N     64

static char    vs_stack[VS_STACK] __attribute__((aligned(16)));
static int32_t vs_ok;                /* set by the worker iff its framed sum came out right */

long vs_sum(volatile long *p, int n);
long vs_worker(long arg);

int run(int n) {
  (void)n;
  /* A real in-window data stack: the worker's frame lands in it and the sum is exact. */
  vs_ok = 0;
  int h = __vm_thread_spawn(vs_worker, (void *)0, 0);
  (void)__vm_thread_join(h);
  return vs_ok ? 141 : 201;
}

/* Deliberately `noinline` and taking a `volatile *`, so the array cannot be promoted to
 * registers or constant-folded away: the worker genuinely needs a data-stack frame. */
long __attribute__((noinline)) vs_sum(volatile long *p, int n) {
  long s = 0;
  for (int i = 0; i < n; i++) s += p[i];
  return s;
}

long vs_worker(long arg) {
  volatile long buf[VS_N];
  for (int i = 0; i < VS_N; i++) buf[i] = arg + (long)i;
  /* sum(arg + i) for i in [0, 64) = 64*arg + 2016 */
  vs_ok = (vs_sum(buf, VS_N) == 64 * arg + 2016);
  return 0;
}

#include "jaclrt.c"   /* runtime impl last, so run() is module function 0 */
