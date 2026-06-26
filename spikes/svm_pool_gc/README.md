# Spike — persistent M:N worker pool vs. svm stop-the-world `gc.roots`

**Status: BLOCKED** (svm `vendor/svm` @ pin `1737fe7`). This spike is a *negative*
result: it characterizes, with a runnable reproducer, why JACL's Phase-3 **persistent
worker-pool** sub-slice (P3.4d, the M:N scheduler keystone) cannot be made GC-sound on the
current svm fiber/GC primitives, and states the precise svm ask that would unblock it.

The rest of Phase 3 stands: generators, `spawn`/`await`, `parallel`/`race`, the per-vCPU
allocator + multi-vCPU STW GC (P3.4a–c), program-as-a-job, and host I/O are all done and
tested interp==jit. `parallel`/`race` currently run on a **transient** pool
(`jacl_sched_run_batch` — fresh `__vm_thread_spawn` workers per construct, joined at the
end), which is proven GC-sound (`runtime/harness/tests/mt.rs::{sched_batch,batch_heap,
gc_sched}`). The persistent pool was meant to replace that (and to carry multi-threaded
`spawn`/`await` + async/parking builtins). It is the replacement that is blocked.

## The bug, precisely

When `parallel` runs allocating blocks across a **persistent** pool of spawned vCPU workers
while those blocks force collections, a block's live root (a `keeper` cell on its task
fiber's stack) is **intermittently not scanned** by the collector's `gc.roots` and gets
swept — a use-after-free / missed-root. It is timing-dependent: the interpreter and the JIT
schedule the worker fibers differently, so the differential oracle catches it as a
divergence (e.g. interp returns `1000001` = "a block's keeper was corrupted, 0 STW
violations" while the JIT returns `555` = "all good"). It is **not** a stop-the-world
mutual-exclusion violation (the `jacl_gc_stopped` probe stays 0) — it is a root-**coverage**
gap: at the instant a non-collector worker runs `gc.roots`, some other worker's task fiber
is not in a state where its stack is scanned.

The structurally identical **transient** path (`jacl_sched_run_batch`, same per-vCPU
allocator, same P3.4c quiesce primitives, same per-block `keeper`-on-fiber pattern) is
rock-solid across hundreds of runs (`gc_sched`). The only material differences in the
persistent pool are: a shared MPMC task queue, workers that stay **registered while idle**
(parking on a 1 ms-timeout futex between tasks rather than exiting), and `run_child`'s
nested GC-suspend propagation for helping-`await`. The divergence reproduces even with
helping removed (flat blocks only), which points the finger at the persistent/idle worker
lifecycle interacting with `gc.roots`' notion of which fibers are quiescent — not at the
helping/nesting logic.

The single-worker configuration (`-DJACL_POOL_WORKERS=1`, everything on the main thread) is
fully sound, including deep nested `await` under GC. So the hazard is specifically
**concurrent task fibers on _spawned_ vCPUs** being scanned by a stop-the-world collector
elected on another spawned vCPU.

## Why this is hard to fix guest-side

`gc.roots` (svm-jit `crates/svm-jit/src/fiber_rt.rs::gc_roots`, svm-interp
`crates/svm-interp/src/bytecode.rs`) scans every **parked** fiber's saved extent plus the
collector's own running resume-chain, and **refuses** (FiberFault) if any fiber is `RUNNING`
on a vCPU other than the collector's. For that scan to cover every live root, every
non-collector worker's task fiber must be **suspended** (parked) before the collector reads
the table. JACL's P3.4c barrier enforces exactly this for the transient pool. Reproducing it
for a persistent pool — where workers continuously transition idle⇄running and a fiber's
data stack is recycled from a shared pool across workers — appears to leave a narrow window
the barrier doesn't cover, and closing it from guest C (without svm telling us a fiber's
precise quiescent SP, or supporting fiber migration with a stable scan contract) has not
been achievable.

The proper guest-side design that would sidestep nesting entirely is a **continuation
scheduler**: `await` suspends its task fiber back to the bare worker loop (so no worker ever
holds a nested fiber chain) and a ready fiber is resumed by *whatever* worker picks it up
(fiber **migration** across vCPUs). That needs svm fiber resume to be sound from a different
vCPU than the one that created/suspended the fiber, with a `gc.roots` contract that covers a
migrated-but-suspended fiber. Whether svm supports that today is unverified.

## The svm ask (see also `docs/SVM_PHASE3_ASKS.md`)

To unblock a persistent M:N pool, **one** of:

1. **A documented, tested `gc.roots` contract for a multi-worker persistent pool**: a
   guest-visible "this vCPU is now quiescent for GC; my current/last fiber's roots are
   spilled at SP=X" handshake, so a collector on another vCPU is guaranteed to scan an idle
   worker's last fiber — independent of the futex/park dance. Equivalently: confirm + test
   that a fiber suspended via `cont` and then *not resumed* has its full live extent scanned
   by `gc.roots` regardless of which vCPU suspended it.
2. **Sound fiber migration**: `__vm_fiber_resume(k, …)` callable from a vCPU other than the
   one that created/suspended `k`, with `gc.roots` covering such migrated fibers. This
   enables the continuation scheduler (no nesting on any worker).

Either makes the persistent pool — and multi-threaded `spawn`/`await` + async/parking
builtins (`sleep`, channels) that ride on it — implementable and verifiable.

## Reproduce

The reproducer reuses the runtime harness (`runtime/harness`), which compiles a
`runtime/tests/*.c` driver + the runtime through `clang -emit-llvm` → `svm-llvm` and runs
`run(0)` (func 0) on interp **and** JIT, asserting they agree.

```
# from repo root — overlay the experimental persistent pool + the stress driver:
cp spikes/svm_pool_gc/sched_pool.c        runtime/sched.c
cp spikes/svm_pool_gc/test_parallel_gc.c  runtime/tests/test_parallel_gc.c
# add a test entry:
cat >> runtime/harness/tests/mt.rs <<'EOF'

#[test]
fn parallel_pool_gc() {
    let r = jacl_runtime_harness::run_test("test_parallel_gc.c", 0);
    assert_eq!(r, 555, "persistent pool parallel survives concurrent GC (diag {r})");
}
EOF
cd runtime/harness
# run a few times — it is FLAKY: interp != jit (missed root, e.g. 1000001 vs 555), or hangs:
for i in 1 2 3 4 5; do cargo test --release --test mt parallel_pool_gc -- --exact; done
# contrast: the proven transient path is stable across the same stress:
cargo test --release --test mt gc_sched -- --exact   # PASS, every time
```

(Use `--release`: svm-jit's `gc.roots` stack scan trips Rust's debug-only `read_unaligned`
UB precondition check — a separate, pre-existing svm debug assertion, benign in release.)

`sched_pool.c` is the full experimental persistent pool (lazy-started pool, shared queue,
`run_child` GC-cooperative fiber runner with nested-suspend propagation, helping-`await`
confined to the main thread, demand-driven `spawn`/`await`). It is kept here, out of the
production build, until the svm ask above lands.
