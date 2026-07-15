# Streams, generators, and concurrency on the SVM backend

How JACL's lazy sequences and task primitives are lowered by the codegen
(`codegen/codegen.c`) and its runtime, and — importantly — where the single-threaded
interpreter draws the line on what can be scored by the `interp == jit` parity oracle.

## Generators are fibers

A `proc` whose body contains `yield` is a **generator**: `contains_yield` flags it during
registration, and it is compiled as a one-argument fiber function `(sp, arg)`. `yield V`
lowers to a fiber `suspend` that hands `V` out and resumes with the caller's next value;
running off the end of the body — or a bare `return` / `[return]` reached as a top-level
statement — ends the fiber, which is how a stream signals exhaustion.

Driving a generator goes through three runtime calls:

- `jacl_gen_new(func_ref, arg)` — create the generator object over the fiber function.
- `jacl_gen_next(g)` — resume the fiber to its next `yield` (or completion).
- `jacl_gen_done(g)` — has the fiber finished?

`for $g x { … }` compiles to the obvious loop: `next`, bind `x`, test `done`, repeat —
with the induction state threaded as block parameters across every control-flow edge, the
same block-local-SSA frame every loop lowering uses.

A value-returning `[return V]` inside a generator is a **compile error** (`cannot return a
value from a generator`) — stream consumers discard the value, so the old VM rejects it and
the SVM codegen mirrors that via `Cx.cur_is_generator`.

## Streams collect eagerly

The corpus builds stream pipelines — `collect`, `transform`, `filter`, `take` — that are
lazy in the old VM. The SVM backend materialises them **eagerly**: `collect` drains its
source into a vector right away, and `transform` / `filter` / `take` produce a fully
realised vector rather than a deferred view. Because the corpus programs collect their
pipelines immediately (they print or fold the result on the next line), this is
output-equivalent to the lazy semantics — the observable result is identical, only the
timing of the work differs. `first` / `count` likewise drain a generator source to answer.

The one visible divergence is *print formatting* of a **typed** collect result: the old VM
prints a `[Vec T]` as `[1, 2, 3]` (comma form) while the backend prints the dynamic
`[vec 1 2 3]`. Closing that needs a distinct typed-vector runtime type — see the GC-tag
discussion in `docs/SVM_PARITY_NOTES.md`.

## Concurrency: cooperative single-worker scheduling

`spawn` / `await` / `parallel` / `race` lower to `jacl_spawn` / `jacl_await` /
`jacl_parallel` / `jacl_race` in `runtime/sched.c`. `spawn { … }` packages the block as a
zero-arg closure (capturing its free variables) as a *job* fiber and returns a future;
`await` drives that future to completion.

The scheduler runs in **cooperative single-worker mode** (`JACL_POOL_WORKERS` defaults to
1). This is the configuration correct on BOTH svm backends — and the reference
interpreter, which the differential `run_diff` oracle requires to agree with the JIT, is
one of them. The interpreter multiplexes every `thread.spawn`ed vCPU cooperatively onto one
OS thread, so the multi-worker pool (jobs pinned round-robin to per-worker queues that only
their owner drains) livelocks there — historically these ~72 cases all hung. With one
worker, every job lands on worker 0's queue and `worker_loop` drains it inline: `await`
suspends the fiber back to the loop (a fiber `suspend`, not a spin), the loop runs the
awaited job to completion, then resumes the waiter with the result. No cross-vCPU wakeup is
needed, so it progresses under the interpreter and gives identical output under the JIT.
The multi-worker real-parallelism pool is retained and opt-in via `-DJACL_POOL_WORKERS=N`.

Semantics layered on top: an **error raised in a task** surfaces through `await` (a binding
captures it for `error?`/`error-val` rather than auto-returning); `parallel` is
**first-error-wins**; and each task **snapshots `$ctx`** at spawn so dynamic scope is
task-isolated (`resume_job` installs/captures the snapshot per slice).

What remains: the **CPS / state-machine lowering** for a proc that suspends mid-body
(`sm_*`, some `cps_*`, wide-scalar-across-suspension) still re-executes statements before
the suspension point on resume — a codegen problem in the suspend-checkpoint transform, not
a scheduler one — and the heaviest `gc`-under-concurrency stress cases.

`print` is the one host effect that *is* scored: it lowers to `jacl_print` → the powerbox
`Stream.write` on the stdout handle the harness stashes before the program body runs as the
root task on a fiber.

## Why the program runs as a fiber at all

The entry function inits the runtime and then runs `__jacl_main` as the **root task on a
fiber** — "program as a job." That places the program's roots on a scannable fiber stack,
so the body can be quiesced like any task; it is the prerequisite for sound GC under the
M:N pool (the collector's `gc.roots` scan needs every live root on a fiber stack it can
walk, including the root task's).
