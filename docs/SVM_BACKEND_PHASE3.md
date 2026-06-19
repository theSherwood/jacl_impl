# JACL on SVM — Phase 3 scope: concurrency on fibers

> Status: **in progress.** Branch: `svm-backend-phase3` (off merged `main`, svm pin
> `a7cbcf1`). Companions: `SVM_BACKEND_DESIGN.md` §4.3, `SVM_BACKEND_PLAN.md` §5,
> `SVM_BACKEND_PHASE2.md` (the sequential codegen this builds on).
>
> Phase 2 (sequential codegen: literals → closures, type-driven lowering, strings/
> vectors, STW conservative GC) is merged. Phase 3 makes JACL **concurrent** on svm's
> **stackful fibers** (`cont.*`), deleting the compile-time state-machine transform.

## Goal

Lower JACL's concurrency surface — `yield` generators, `spawn`/`await`, `parallel`,
`race`, async (parking) builtins — onto svm **fibers** + the M:N scheduler, matching
the old VM's behaviour. The headline simplification (Spike-3): a `yield` is a runtime
**fiber suspend**, working from any call depth, so the **SM transform is deleted** —
no coloring suspending functions, no rewriting call sites.

**Definition of done:** generator procs, `spawn`/`await`, `parallel`/`race`, and the
common async builtins compile through the codegen, run on interp + JIT, and match the
old VM for representative concurrent programs.

## The fiber primitive (Spike-3, `spikes/svm_fiber_generator`)

svm exposes `cont.*` to C as:

```c
int  __vm_fiber_new(long (*f)(long), void *stack);  /* create; returns a fiber handle */
long __vm_fiber_resume(int k, long arg, int *done); /* resume k; *done=1 when f returns */
long __vm_fiber_suspend(long value);                /* from inside: yield value, get resume arg */
```

Each fiber owns its own data stack (§3d). A function run on a fiber has the data-SP
ABI: svm-llvm threads `sp` as the leading parameter, and resume passes `(fiber_sp, arg)`,
so a JACL generator function is `(i64 sp, i64 arg) -> i64`. `suspend` switches the whole
control stack, so a `yield` deep inside nested calls just works — what the SM never had.

## Concurrency AST surface (parser, unchanged)

| Form | Head | Shape |
|---|---|---|
| `yield V` | `HEAD_YIELD` | `args = [V]` (inside a proc body ⇒ the proc is a generator) |
| `for $coll name { body }` | `HEAD_FOR` | `args = [$coll, name, body]` (collection-first — canonical) |
| `spawn { block }` | `HEAD_SPAWN` | `args = [block]` → a future/Job |
| `await $f` | `HEAD_AWAIT` | `args = [$f]` |
| `parallel { } { } …` | `HEAD_PARALLEL` | `args = N blocks` → vector of N results |
| `race { } { } …` | `HEAD_RACE` | `args = N blocks` → first result |
| `sleep N` | `HEAD_SLEEP` | parks the fiber |

(`for` here is the canonical collection-first form — Phase 2's `for NAME in RANGE` was a
non-canonical shape flagged at bring-up and is reconciled here.)

## Concrete steps

### P3.1 — Generators (`yield`) on fibers — **DONE (scheduler-free)**
- A proc whose body contains `yield` is a **generator**: it lowers to an SVM function
  `(i64 sp, i64 resume_arg) -> i64`; `yield V` → the `suspend` op; the body returning
  is the `done` signal (`cont.resume`'s status), so no sentinel value is needed.
- **Done:** runtime `runtime/fiber.c` — `jacl_gen_new(fnref, arg)` builds a generator
  object (a fiber via `__vm_fiber_new` over a pooled data stack + the prime arg),
  `jacl_gen_next` resumes it (priming the first resume with the param), `jacl_gen_done`
  reports completion. IR builder gained the `suspend` op. Codegen: a generator proc is
  detected (`contains_yield`) and given the `(sp, arg)` signature with its one declared
  param bound to the resume arg; `yield V` → `suspend`; a generator-proc call
  `[g a]` builds the generator (`jacl_gen_new` over `ref.func`) instead of calling it;
  and the **canonical** `for [g a] name { body }` drives it (resume/`done` loop, binding
  `name` to each yielded value). Validated on interp + JIT against the old VM:
  `[for [upto 5] x …]` → 10 and a squares generator → 30.
- **Limits (current):** a generator takes ≤1 param (the single resume arg); a generator
  must not hold heap roots across a `yield` until multi-fiber GC quiesce (P3.6) scans
  parked fibers' data stacks (i32 generators are safe); the data-stack pool is fixed.

### P3.2 — `spawn` / `await` — **DONE (cooperative, single-thread)**
- `spawn { block }` → a future; `await $f` drives it to completion, caching the result.
- **Done:** `spawn { block }` lowers the block to a **0-param closure** (capturing free
  vars) and `jacl_spawn` runs it on a task fiber; `jacl_await` resumes to `done`, caches
  and returns the value (so a future resolves once, awaitable repeatedly). Validated on
  interp + JIT vs the old VM: spawn+await (42), capturing spawn, two spawns, double-await.
- **Limit:** a task that awaits another task's *unresolved* future needs the run-queue
  scheduler (P3.4); leaf and yield-internal tasks run to completion here.

### P3.3 — `parallel` / `race` — **DONE (cooperative)**
- `parallel { } …` spawns N task fibers, awaits all, returns a **vector** of results
  (consumed via positional destructuring `def [a b] …`, also added). `race { } …`
  returns the first block's result (cooperative: leaf tasks complete in order, so the
  first wins). Validated vs the old VM: `parallel` (45), 3-way `parallel` (42), `race` (42).
- **Limit:** true parallelism / cancellation lands with the M:N scheduler (P3.4).

### P3.4 — Re-platform the M:N scheduler
- Port the NxM Chase-Lev work-stealing scheduler (`runtime.c`) onto fibers +
  `thread.spawn` + futex (real OS threads as workers, fibers as tasks).

### P3.5 — Async (parking) builtins
- Convert blocking builtins (I/O, `sleep`, channel ops) to **async-form** capabilities
  that park the fiber instead of blocking the worker thread.

### P3.6 — Multi-fiber GC quiesce
- The P2.9 collector is STW for one fiber. Here: a safe point that **quiesces all
  vCPUs/fibers** before collecting (svm's stop-the-world barrier), so conservative
  `gc.roots` scans every parked fiber's stack.

## Carried constraints
- **No SM transform** (deleted) — generators are fibers (Spike-3).
- **Conservative STW GC** (P2.9) — extended to multi-fiber quiesce (P3.6); a parked
  fiber's stack is scanned conservatively, so its roots are found with no protocol.
- **Data-SP ABI / block-local SSA** (Phase 2) — generator/task functions follow it.

## Deferred (not Phase 3)
- Full corpus parity + perf pass → **Phase 4**.
- Cutover / retiring the old VM → **Phase 5**.
- Stackless-generator lowering (a narrow optimization) — only on a named trigger.

## Note on size
Phase 3 is large (re-platforms the scheduler + adds all concurrency primitives + the
multi-fiber GC). It sub-phases like Phase 2: P3.1 (generators) is scheduler-free and
lands first; P3.2–P3.3 add tasks/futures on a minimal scheduler; P3.4 swaps in the M:N
work-stealing version; P3.5–P3.6 finish async builtins + multi-fiber GC.
