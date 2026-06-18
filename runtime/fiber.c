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

/* generator payload (int32 words): [0] fiber handle, [1] done, [2] started, [3] pad,
 * [4..5] the i64 prime arg passed on the first resume. */
JaclVal jacl_gen_new(JaclVal fnref, JaclVal arg) {
  if (jacl_gen_stack_next >= JACL_GEN_STACKS) return jaclrt_error();
  void *stk = jacl_gen_stacks[jacl_gen_stack_next++];
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
