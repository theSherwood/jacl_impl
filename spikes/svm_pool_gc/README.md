# Spike — persistent worker pool for parallel/race under multi-vCPU GC

**Status: root-caused; not shipped; no svm dependency.** This spike chased a flaky GC corruption
in a *persistent* worker pool for `parallel`/`race`, isolated it to three distinct causes, fixed
the two that are straightforward, and pinned the remaining one to a structural property the
shipped (transient) pool avoids. All three are **JACL-side**: the clean fix for the last one is a
continuation scheduler riding on svm's **already-implemented, already-tested** cross-vCPU fiber
migration — so the persistent M:N pool is **not** blocked on svm.

It corrects two earlier wrong conclusions: svm's `gc.roots` is **not** buggy, and fiber
migration is **not** a missing primitive (it exists and is tested). The proven transient
pool (`runtime/sched.c` `jacl_sched_run_batch`, exercised by `mt.rs::{sched_batch,batch_heap,
gc_sched}`) uses the same `gc.roots` and is rock-solid. The failures were in how the *persistent*
pool used it.

`sched_pool.c` is the experimental persistent pool with fixes #1 and #2 below applied (the
closest-to-sound version); `test_parallel_gc.c` is the instrumented stress driver used to
classify each failure. Neither is in the production build — `parallel`/`race` ship on the
transient `run_batch`.

## How it was root-caused

The driver runs `parallel` over N allocating blocks on the pool while they force collections,
and classifies any wrong answer (keeper swept vs overwritten; wrong result = block i got value
X). Progressive isolation:

1. **Disable forced GC →** 6/6 pass. So the corruption is GC-related, not a pure logic race.
2. **Instrument the wrong value →** block *i* came back as *another* block's result, or `0`.
   Since the futures/closures are allocated on **main** but blocks run on **workers**, this is a
   main-side object being swept/clobbered, not a worker keeper.
3. **Probe worker vCPU ids →** workers never see id 0, so it isn't allocator aliasing from a
   misread vCPU id.

### Cause #1 — main-side roots not scanned (primary; interp **and** jit). FIXED.
`jacl_parallel` ran on main as a *concurrent mutator* while pool workers triggered
stop-the-world collections, holding its in-flight roots — the input `closures` vector, each
freshly-allocated future, the accumulating result vector, and register-resident transients —
only on **main's fiber stack/registers**. svm's `gc.roots` does not reliably cover the main
thread's stack/registers for a collection elected on *another* vCPU (conservative stack scan
plus the documented unspilled-callee-saved-register gap). So those objects were swept and reused.

Fix (mirrors `run_batch` and the demand-driven `jacl_futures` registry): keep all in-flight
state in **global buffers** scanned by `jacl_sched_mark_roots`, and do main-side allocation only
when main is the **sole** mutator — phase 1 (build futures) before workers run, phase 3 (build
the result vector) after every block is done and workers are idle, so a collection main triggers
then scans its own (collector) frames. This eliminated **all** interp corruption.

### Cause #2 — result handoff ordering (jit only). FIXED.
`run_task` wrote the i64 result with a plain store, then atomic-stored the DONE flag. Under
svm-llvm + the JIT's real OS threads the plain store was not ordered before the atomic, so an
awaiter could observe DONE with a stale `0` result. Fix: publish the result as two SeqCst 32-bit
halves *before* DONE and read them back after (`fut_publish`/`fut_result`).

### Cause #3 — main's program-fiber in the multi-worker quiesce set (residual; jit only). NOT FIXED.
After #1 and #2 a rare failure remains on real-thread JIT (~1/40): either a `gc.roots`
`FiberFault` (it refuses when a fiber is RUNNING on another vCPU) or a stray stale result.
Unlike `run_batch` — where main **unregisters and parks on its bare thread** during the
concurrent phase, never a running fiber while a worker collects — this pool keeps main running
the program as a task fiber (program-as-a-job) that **cycles suspend/resume** while it awaits.
That leaves narrow windows the strict `gc.roots` occasionally trips. The single-worker
configuration (`-DJACL_POOL_WORKERS=1`, everything on main, main is the only collector) is fully
sound; the hazard is specifically main's program-fiber co-existing with worker-elected
collections.

Eliminating #3 needs the program fiber to step **entirely off** the workers during a wait — a
**continuation scheduler**: `await` suspends the task fiber back to a bare scheduler loop (so it
is never a RUNNING fiber mid-collection) and a ready fiber resumes on whatever worker grabs it.
That rides on cross-vCPU fiber **migration** (resume a fiber on a different vCPU than suspended
it, with `gc.roots` covering migrated-but-suspended fibers) — which **svm already implements and
tests** on both backends (`cont.resume` "any vCPU may resume … even when another OS thread
suspended it"; `svm/tests/fiber_migrate.rs::fiber_suspended_on_root_resumes_on_spawned_vcpu`,
`gc_roots.rs` cross-vCPU scan). So #3 is **not** blocked on svm — it is remaining JACL-side
work. `docs/SVM_PHASE3_ASKS.md` Ask 2 (which earlier requested migration) is **withdrawn**.

## Reproduce

```
cp spikes/svm_pool_gc/sched_pool.c        runtime/sched.c
cp spikes/svm_pool_gc/test_parallel_gc.c  runtime/tests/test_parallel_gc.c
cat >> runtime/harness/tests/mt.rs <<'EOF'

#[test]
fn parallel_pool_gc() {
    assert_eq!(jacl_runtime_harness::run_test("test_parallel_gc.c", 0), 555);
}
EOF
cd runtime/harness
for i in $(seq 1 40); do cargo test --release --test mt parallel_pool_gc -- --exact; done
# ~39/40 pass; the rare failure is a jit FiberFault or stale result (cause #3).
# Contrast: the shipped transient path is solid:
cargo test --release --test mt gc_sched -- --exact
```

(Use `--release`: svm-jit's `gc.roots` stack scan trips Rust's debug-only `read_unaligned` UB
precondition check — a separate, pre-existing svm debug assertion, benign in release.)
