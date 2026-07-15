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

## Concurrency: correct on the JIT, unscorable on the interpreter

`spawn` / `await` / `parallel` / `race` lower to `jacl_spawn` / `jacl_await` /
`jacl_parallel` / `jacl_race` over the runtime's M:N worker pool (`runtime/sched.c`).
`spawn { … }` packages the block as a zero-arg closure (capturing its free variables) run
on a task fiber and returns a future; `await` drives that future to completion.

These run correctly under the **JIT**, which has a real OS-thread worker pool. But the
parity harness scores each case with a differential `run_diff` that requires the
**interpreter** and JIT to agree, and the interpreter is single-threaded — it cannot spin
up the worker pool, so any program that actually blocks on cross-task progress never
terminates under interp. The harness records these as `hang` (~72 cases: the `spawn` /
`await` / `parallel` / `race` / `cps` / `gc`-stress / `sleep` families). They are not
codegen breadth gaps — the IR is emitted fine — but scheduler/runtime work that has to make
the interpreter quiesce under the differential oracle. This is the single largest bucket on
the scoreboard and the one least addressable from the codegen side.

`print` is the one host effect that *is* scored: it lowers to `jacl_print` → the powerbox
`Stream.write` on the stdout handle the harness stashes before the program body runs as the
root task on a fiber.

## Why the program runs as a fiber at all

The entry function inits the runtime and then runs `__jacl_main` as the **root task on a
fiber** — "program as a job." That places the program's roots on a scannable fiber stack,
so the body can be quiesced like any task; it is the prerequisite for sound GC under the
M:N pool (the collector's `gc.roots` scan needs every live root on a fiber stack it can
walk, including the root task's).
