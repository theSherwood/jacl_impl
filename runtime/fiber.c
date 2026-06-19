/* fiber.c — P3.1 generators on svm stackful fibers (cont.*).
 *
 * A JACL generator (a proc whose body contains `yield`) lowers to a codegen function
 * `(i64 sp, i64 arg) -> i64`; `yield V` is the `suspend` op. This runtime side wraps
 * such a function into a generator object and drives it:
 *   jacl_gen_new(fnref, arg) — create a fiber over the function + a fresh data stack;
 *   jacl_gen_next(gen)       — resume it (priming the first resume with `arg`);
 *   jacl_gen_done(gen)       — true once the function returned (vs. suspended).
 * No state-machine transform: a `yield` deep in nested calls just suspends the fiber
 * (Spike-3). Multi-fiber GC of a parked generator's data stack is Phase 3 P3.6; until
 * then a generator must not hold heap roots across a `yield` (i32 generators are safe).
 */
#include "jaclrt.h"

/* svm fiber intrinsics (svm-llvm lowers these to cont.new / cont.resume). */
int  __vm_fiber_new(long (*f)(long), void *stack);
long __vm_fiber_resume(int k, long arg, int *done);

#define JACL_GEN_STACKS      32
#define JACL_GEN_STACK_BYTES (1u << 16)
static char jacl_gen_stacks[JACL_GEN_STACKS][JACL_GEN_STACK_BYTES] __attribute__((aligned(16)));
static int  jacl_gen_stack_next;

/* Grab the next fiber data stack from the pool (NULL when exhausted). Shared by
 * generators and spawned tasks. */
static void *jacl_grab_fiber_stack(void) {
  if (jacl_gen_stack_next >= JACL_GEN_STACKS) return 0;
  return jacl_gen_stacks[jacl_gen_stack_next++];
}

/* generator payload (int32 words): [0] fiber handle, [1] done, [2] started, [3] pad,
 * [4..5] the i64 prime arg passed on the first resume. */
JaclVal jacl_gen_new(JaclVal fnref, JaclVal arg) {
  void *stk = jacl_grab_fiber_stack();
  if (!stk) return jaclrt_error();
  int h = __vm_fiber_new((long (*)(long))(uintptr_t)(uint64_t)fnref, stk);
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_BLOB, 24);
  int32_t *p = (int32_t *)jacl_obj_payload(o);
  p[0] = h; p[1] = 0; p[2] = 0;
  *(int64_t *)(p + 4) = (int64_t)arg;
  return jaclrt_from_ptr(JACL_TAG_STREAM, o);
}

JaclVal jacl_gen_next(JaclVal gen) {
  int32_t *p = (int32_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(gen));
  long resume_arg = p[2] ? 0 : (long)*(int64_t *)(p + 4); /* prime once */
  p[2] = 1;
  int done = 0;
  long v = __vm_fiber_resume(p[0], resume_arg, &done);
  p[1] = done;
  return (JaclVal)v;
}

JaclVal jacl_gen_done(JaclVal gen) {
  int32_t *p = (int32_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(gen));
  return jaclrt_bool(p[1] != 0);
}

/* ---- P3.2 spawn / await (futures over fibers) ----
 *
 * `spawn { block }` is a no-param closure (capturing free vars) run on a task fiber;
 * the future is that fiber. `await` drives it to completion and caches the result, so
 * a future resolves once and is awaitable repeatedly. (Single-thread cooperative: a
 * task that itself awaits an *unresolved* future of another task needs the scheduler —
 * P3.4; leaf and yield-internal tasks run to completion here.)
 *
 * future payload (JOBJ_NODE; closure + cached value are heap pointers, so traced):
 *   int32 [0] fiber handle, [1] resolved, [2] started; i64 [4..5] closure (prime arg),
 *   i64 [6..7] cached result. */
JaclVal jacl_spawn(JaclVal closure) {
  void *stk = jacl_grab_fiber_stack();
  if (!stk) return jaclrt_error();
  JaclVal fnref = jacl_closure_fn(closure); /* raw funcref of the block function */
  int h = __vm_fiber_new((long (*)(long))(uintptr_t)(uint64_t)fnref, stk);
  JaclObj *o = (JaclObj *)jacl_alloc(JOBJ_NODE, 32);
  int32_t *p = (int32_t *)jacl_obj_payload(o);
  p[0] = h; p[1] = 0; p[2] = 0;
  *(int64_t *)(p + 4) = (int64_t)closure; /* primed as `self` on the first resume */
  *(int64_t *)(p + 6) = 0;
  return jaclrt_from_ptr(JACL_TAG_STREAM, o);
}

JaclVal jacl_await(JaclVal future) {
  int32_t *p = (int32_t *)jacl_obj_payload((JaclObj *)jaclrt_as_ptr(future));
  if (p[1]) return (JaclVal) * (int64_t *)(p + 6); /* already resolved */
  long prime = p[2] ? 0 : (long)*(int64_t *)(p + 4);
  p[2] = 1;
  int done = 0;
  long v = 0;
  do {
    v = __vm_fiber_resume(p[0], prime, &done);
    prime = 0;
  } while (!done);
  p[1] = 1;
  *(int64_t *)(p + 6) = (int64_t)v;
  return (JaclVal)v;
}
