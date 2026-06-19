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
- **Limits (current):** a generator takes ≤1 param (the single resume arg); the
  data-stack pool is fixed. (Heap roots held across a `yield` **are** safe in the
  single-thread model: svm's `gc.roots` scans suspended fibers' stacks — see the svm
  `gc_roots_scans_suspended_fiber_stack` test — so a parked generator's roots are found.)

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

### P3.6 — Multi-fiber GC quiesce — **DONE for single-thread (svm-provided)**
- The cooperative scheduler (P3.1–P3.3) runs all fibers on one OS thread, so GC is
  already a correct STW collection: svm's `gc.roots` scans **every** suspended fiber's
  stack (control + data — proven by svm's `gc_roots_scans_suspended_fiber_stack` /
  `gc_roots_enumerates_every_parked_fiber`), so a parked generator's or task's
  conservative roots are found with no extra protocol.
- **Remaining:** a multi-**vCPU** quiesce barrier (stop all worker threads at a safe
  point before collecting) is only needed once P3.4 introduces real OS-thread workers;
  svm provides the STW barrier to build it on.

### P3.4 — Re-platform the M:N scheduler — **P3.4a/b/c done; P3.4d in progress (parallel/race/spawn/await on threads; persistent pool + work-stealing pending)**
- Port the NxM Chase-Lev work-stealing scheduler (`runtime.c`) onto fibers +
  `thread.spawn` + futex (real OS threads as workers, fibers as tasks).

> **Status.** P3.4a (per-vCPU TLAB allocator), P3.4b (concurrent allocation across real
> `thread.spawn` workers), and **P3.4c (sound multi-vCPU STW GC over the fiber scheduler)**
> are **done** and tested (interp==jit): `runtime/heap_gc.c`,
> `runtime/tests/test_alloc_mt.c`, `runtime/tests/test_gc_sched.c`. The remaining P3.4d
> work — a *reusable* scheduler (worker pool + task queue), moving `spawn`/`await`/
> `parallel`/`race` onto real worker threads, fiber parking on `await`, and work-stealing
> deques — builds directly on the P3.4c quiesce primitives.
>
> **P3.4c done — the quiesce barrier + scheduler-loop primitives.** The runtime now
> exposes the pure-guest STW barrier (svm GC.md §2.1) over the fiber scheduler:
> `jacl_gc_worker_register`/`unregister`, `jacl_gc_task_begin`/`end` (bracket a task
> resume), `jacl_gc_safepoint` (in `jacl_alloc` + loop back-edges: **suspends** the task
> to the scheduler so its roots are scannable), `jacl_gc_worker_park_if_requested`
> (scheduler-loop top: parks the root-free vCPU), and the `jacl_gc_collect` collector
> (elect via CAS → bump epoch → wait for all to suspend+park → scan+sweep STOPPED →
> resume). The collector is the `gc.roots` caller, so its own task's roots are covered;
> all other tasks are suspended fibers, which `gc.roots` scans. The parked flag is
> **epoch-stamped** (not a sticky boolean), which was the load-bearing fix — a sticky flag
> let a next collection mistake a stale park for a fresh one and a worker mutated during
> STW. Validated by `test_gc_sched.c`: each worker's on-stack-of-its-fiber keeper survives
> a collection run by a different worker, mutual-exclusion violations are 0, stress-tested
> under heavy contention; single-thread path unchanged.
>
> **P3.4c finding — STW root scanning needs suspend-to-scheduler, not futex-park.**
> A first cut put the GC safepoint at the allocation point and **parked the worker in
> `__vm_wait32`** while it held roots on its bare `thread.spawn` C stack. That is
> **unsound**: a worker blocked in `__vm_wait32` is *not* in the set `gc.roots` scans.
> svm's own cross-vCPU STW test (`crates/svm/tests/gc_roots.rs::
> gc_roots_cross_vcpu_stop_the_world_scan`) holds the root on a **suspended fiber**
> (`cont.new`/`resume`/`suspend`) and only *then* parks the vCPU in `atomic.wait`; the
> collector's `gc.roots` finds it there. GC.md §2/§3.1 confirms: `gc.roots` scans
> **every suspended fiber + the caller**, and the prescribed safepoint is *"the mutator
> fiber **suspends** to its vCPU's scheduler"* (suspend is call-clobbering, so it flushes
> live roots onto the fiber's control stack → scannable); the scheduler then parks the
> vCPU. A draft barrier proved this empirically — with futex-park, a worker's genuinely
> live on-stack keeper was **collected** on one backend (a true root missed), confirming
> the bare-vCPU stack is not scanned.
>
> **Consequence.** Sound STW GC requires roots to live on **fibers** and the safepoint
> to **`suspend`**, which only exists once worker vCPUs run a **scheduler loop** that
> resumes task fibers. So P3.4c (quiesce + scan) and P3.4d (the M:N scheduler) must land
> together: the safepoint poll lives in the scheduler, a task fiber suspends on a stop
> request, and the vCPU parks with no fiber running — at which point the collector's
> `gc.roots` sees all roots. The futex barrier itself (epoch/park/release, mutual
> exclusion) is the right mechanism (svm's `gc_quiesce.rs`); it just has to drive
> fiber-suspend, not block the bare worker.

> **Unblocked (svm `7a82f64`, PR #79).** The per-vCPU allocator needs each worker
> to know its own vCPU id cheaply, anywhere. svm shipped the ambient per-vCPU TLS
> register we asked for (`docs/SVM_PHASE3_ASKS.md` — RESOLVED): C intrinsics
> `long __vm_vcpu_tls_get(void)` / `void __vm_vcpu_tls_set(long)`, **read at the
> execution point** so a migrated fiber reads the *current* vCPU's word
> (work-stealing-correct), **seeded to a dense vCPU id** (so a bare `get` is the
> `per_worker[id]` index), and **overwritable with a per-CPU block pointer** for
> full `__thread`-style TLS. We implement per-vCPU TLABs **directly** on it — no
> throwaway global allocator. svm already provides the rest (`__vm_thread_spawn`/
> `join`, atomics + futex, multi-vCPU STW via `gc_quiesce.rs`, conservative roots
> over parked fibers).
>
> Same bump widened **fiber handles i32 → i64**; `runtime/fiber.c` updated to store
> i64 handles (required to verify against the new pin).

**Allocator + GC design (decided).** Real parallelism needs a thread-safe runtime. We
adopt JACL's existing approach — **per-worker (per-vCPU) heaps with a lock-free bump
fast path** — adapted to SVM's simpler collector. *Not* a per-allocation lock.

- **Per-vCPU heaps, lock-free fast path.** Each worker owns its own heap region and
  bump-allocates within it with no lock (the way `gc__bump_alloc` does today — "no lock
  needed"). The only coordination is refilling a worker's region from a shared free-region
  pool when it fills — amortized once per region (~tens of KB), a coarse mutex (or a
  lock-free Treiber free-list later). Per-worker free lists for swept cells, same idea.
- **Worker identity via the per-vCPU TLS register.** Each worker indexes
  `per_worker[id]` where `id = __vm_vcpu_tls_get()` — the ambient per-vCPU word svm
  seeds to a dense vCPU id (root 0, children sequential). It is **read at the
  execution point**, so a fiber that migrated under work-stealing reads its
  *current* vCPU, not a stale spawn-time id; the main thread is vCPU 0, preserving
  the single-thread path. (Optionally `__vm_vcpu_tls_set` a pointer to the worker's
  per-CPU block for direct `ThreadHeap*` access, `__thread`-style.)
- **GC: cooperative STW, conservative, non-moving (extends P2.9/P3.6).** Workers reach a
  safe point at allocation; a collection **quiesces all worker threads** via a guest
  futex barrier (reference: svm's `gc_quiesce.rs`; no new svm primitive — "quiescing the
  N threads quiesces the M fibers"), then conservative `gc.roots` scans every thread's
  and fiber's stack (svm already does), sweeps each per-worker heap, and resumes. No
  generations / write barriers / concurrent marking — far simpler than the old VM.

**Sub-steps.** P3.4a — refactor the runtime allocator to per-worker heaps (worker-id
indexed; lock-free bump; shared region pool), preserving the single-worker path. **(DONE.)**
P3.4b — real `__vm_thread_spawn` workers that allocate concurrently (validate no
corruption). **(DONE** — `test_alloc_mt.c`, interp==jit.**)** P3.4c+d **(merged)** — the
fiber-based M:N scheduler: worker vCPUs run a resume loop over task fibers; a GC safepoint
(loop back-edge / alloc) **suspends** the running task fiber to the scheduler, which parks
the vCPU on a futex when `gc_epoch` is set (epoch/park/release barrier per `gc_quiesce.rs`);
once all vCPUs are parked with no fiber running, the collector's `gc.roots` scans every
suspended fiber + itself and sweeps each per-worker heap. Then `parallel`/`race` run on
worker threads, work-stealing deques balance tasks, and `await` parks the awaiting fiber.

**P3.4d progress.** A reusable batch scheduler (`sched.c`,
`jacl_sched_run_batch`) runs a batch of task fibers across N vCPU workers, GC-safe via the
P3.4c primitives (`test_sched_mt.c`). **All four concurrency constructs now run on it**,
still matching the old VM:
- **`parallel`/`race`** — codegen builds a vector of the block closures and calls
  `jacl_parallel`/`jacl_race`, which run each block on the pool (real parallelism) and
  return the ordered result vector / block-0's result (`parallel_race_matches_old_vm`).
- **`spawn`/`await`** — lazy futures: `jacl_spawn` records the block closure as a pending
  task and returns a future; the first `jacl_await` flushes all pending tasks onto the pool
  and caches results (`spawn_await_matches_old_vm`). Value-equivalent to eager scheduling
  for the pure-compute corpus (independent tasks; only overlap differs). The collector
  roots pending futures during a flush (`jacl_sched_mark_roots`, since main is parked in
  join with its stack unscanned).

**Remaining:**
- **Persistent pool** (spawn once, reuse) with per-future **waiter queues** and
  **nested-await parking** (a task awaiting an *unresolved* future suspends to its worker
  and is re-enqueued on completion). This replaces both the transient per-construct pools
  and the lazy-flush, enabling eager spawn execution and arbitrary await nesting. It must
  be **shut down before the root returns** (an svm run completes only when *every* vCPU
  finishes, so idle workers on a futex would hang it) — i.e. a program entry/exit hook.
- **Work-stealing deques** to replace the fixed-batch atomic-counter dispatch.
- **vCPU-id growth**: each batch spawns fresh vCPUs (svm ids aren't reused), so very many
  concurrency constructs would outgrow the per-vCPU arrays; the persistent reused pool
  fixes this too.
- **Heap-valued results** surviving a mid-batch collection (the corpus returns i32; root
  each result as its task completes, or have workers retain them).

### P3.5 — Async (parking) builtins — **BLOCKED on the host powerbox**
- Convert blocking builtins (I/O, `sleep`, channel ops) to **async-form** capabilities
  that park the fiber instead of blocking the worker thread.
- **Blocker:** these are *host I/O* and depend on the **powerbox / capability layer**
  (the host-capability surface), which is not yet built — the same prerequisite that
  kept `print` out of the Phase-2 codegen. Async builtins land once the powerbox exists
  (it pairs naturally with P3.4's worker threads). Pure-compute concurrency (generators,
  spawn/await, parallel/race) needs none of it and is already done.

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
