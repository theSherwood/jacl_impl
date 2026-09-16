/* jacl #141 — a `thread.spawn`ed vCPU needs its own in-window data stack.
 *
 * `thread.spawn` takes the new vCPU's data-stack base and **reserves nothing for it**: only the
 * root gets a stack carved out of the window (temen-llvm's `entry_sp` + `STACK_RESERVE`, with a
 * faulting guard beyond). The IR's own words for `ThreadSpawn` — *"running `funcs[func]` on the
 * data stack based at `sp` … every vCPU owns its own in-window data stack, exactly like a fiber"*.
 *
 * Every spawn in `runtime/sched.c` used to pass `(void *)0`, which based the worker's stack at
 * address 0. That is not a stack, and the first frame the worker needed faulted. Three tests
 * (`sched_batch`, `batch_heap`, `gc_sched`) failed with `MemoryFault` on **both** backends, and
 * the failure was misread as an interpreter limitation because `run_test` runs interp first.
 *
 * What made it survive so long is that the fault is *conditional*: a worker whose locals all stay
 * in SSA registers never touches its data stack, so `alloc_mt` passed with the same NULL stack.
 * This case removes that luck — its worker provably needs a frame (a volatile array, summed
 * through a `noinline` callee, so nothing can keep it in registers).
 *
 * This file is the **positive** half: given a real per-worker stack, the framed worker runs and its
 * sum is exact. The negative half is `test_vcpu_stack_null.c` — the identical worker with
 * `(void *)0` — and it lives in the Rust harness rather than here, because a trap terminates the
 * whole run and so cannot be observed from inside the guest. The two together pin the rule; the
 * three scheduler tests (`sched_batch`, `batch_heap`, `gc_sched`) guard `sched.c` itself.
 *
 * Returns 141; 201 if the framed worker did not report its sum. */
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
  int h = __vm_thread_spawn(vs_worker, vs_stack, 0);
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
