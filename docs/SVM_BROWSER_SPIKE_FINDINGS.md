# SVM-in-browser: re-measuring the blocker

> **Status:** experiment results. Corrects the root-cause diagnosis in
> `SVM_BROWSER_PLAN.md` §"Spike results / B". Same goal: move the playground off
> the old bytecode VM onto the SVM backend. Tooling: `runtime/harness/src/bin/probe_svmb.rs`
> (new) and `jacl_svm --bytecode`.

## TL;DR

The original spike concluded that JACL modules fail on the browser's wasm-safe
bytecode engine because the linked runtime uses `cap.self` ops 9/10
(`svc.poll`/`svc.wait`) and other declined capability ops — i.e. an **op-coverage**
gap. **Direct measurement contradicts that.** The reachable slice of a JACL program
contains **zero** declined op sites, and the engine lowers **every** instruction the
runtime emits. The sole reason `compile_module` returns `None` is a single
**module-level seam veto**: `gc.roots + thread`. Lift that one term and the linked
module for both `print "hi"` and a generator program compiles with zero per-op
declines.

That was a **layered gap list**, discovered one at a time, not the cap ops the spike
named — and it is now **fully closed** by two small bytecode-engine slices:

1. **Compile-time:** the `gc.roots + thread` seam veto (declined the module). **Removed**
   — `gc.roots` is per-vCPU conservative root enumeration (caller + the run-shared fiber
   registry), a sound superset of the tree-walker (`tw ⊆ bc`); cross-thread GC soundness
   is the guest's quiesce, not this op's. See "the veto".
2. **Runtime:** `vcpu.tls` (`VcpuTlsGet`) — the fused bytecode engine had no `Op` for it
   and trapped `Malformed`. **Lowered** (see "Op::VcpuTlsGet/Set").
3. **…nothing.**

**End state: the linked JACL runtime compiles and runs on the wasm-safe bytecode engine.**
With both slices in the vendored svm, `bytecode::compile_module` **accepts** the linked
module (no env hack) and `jacl_svm run --bytecode` prints `hi` / `1\n2` on the bytecode
engine — single- **and** multi-vCPU. The multi-vCPU path was the right execution
*destination* but was **not** a shortcut: it shares the same fused-op coverage (both
engines failed identically before the fix, both run green after).

## Method

`bash runtime/build.sh` bakes `jaclrt.svm` (clang-18 + svm-llvm). `probe_svmb`
mirrors `emit_svmb`/`jacl_svm` up to `synth_manifest_start`, then instead of
encoding it **measures** the linked module:

1. `bytecode::compile_module` verdict (the browser wasm-safe engine, all-or-nothing).
2. `svm_opt::optimize_module` (devirt + inter-fn DCE) — does it prune?
3. Per-instruction declined-op census + reachability from `_start`.
4. `svm_wasm_jit::analyze` mixed-tier verdict (`mixed_ok`).

Two programs: `print "hi"` and a two-`yield` generator driven by `for [g] n { print $n }`.

## Results

| Measurement | `print "hi"` | generator |
|---|---|---|
| Linked functions | 238 | 240 |
| `bytecode::compile_module` (as the spike ran it) | `None` (STATUS_UNSUPPORTED) | `None` |
| Declined `cap.self` / `YIELDER` / `INSTANTIATOR` op sites | **0** | **0** |
| `svc.poll` / `svc.wait` (cap.self 9/10) sites | **0** | **0** |
| Actual trip cause | **`gc.roots + thread` seam veto** | same |
| `optimize_module` DCE prune | none (238→238) | none (240→240) |
| Why DCE bailed | `cont.new`=1, `ref.func`=1 reachable; `call_indirect`=**0** | same |
| `gc` / `thread` reachable from `_start` | **yes / yes** | yes / yes |
| Veto term lifted → `compile_module` | **ACCEPTED, 0 declines** | ACCEPTED |
| Veto lifted → run on the bytecode engine | **traps `Malformed`** | traps `Malformed` |
| `Backend::Bytecode`, veto in place | falls back to tree-walk → prints `hi` | → `1\n2` |
| `Backend::Jit` (control) | `hi` | `1\n2` |

## What this changes

- **The blocker is not op coverage.** The bytecode engine already lowers everything
  JACL emits, generators included (`svc.poll`/`svc.wait` in the canonical
  one-result shape, `spawn_coroutine`/`resume`, `co_yield`, `instantiate`/`join`
  are all already supported arms in `svm-interp/src/bytecode.rs`). The reachable
  declined-op count is zero.

- **One veto gates all JACL programs.** `compile_module_with`'s module-level seam
  veto (`svm-interp/src/bytecode.rs`, the `has_gc && has_thread` term) declines any
  module containing both `gc.roots` and `thread.*` ops. JACL's runtime reaches both
  from `_start` (GC-root machinery + scheduler init), so the veto fires for the
  trivial program and the generator identically.

- **Tree-shaking cannot sidestep it.** `svm_opt::optimize_module`'s inter-function
  DCE (`dead_func_elim`) does **not** fire here: it bails on `has_indirect_funcref_dispatch`,
  tripped by the runtime's reachable `cont.new` / `ref.func` (note `call_indirect`=0).
  And even if it fired, `gc.roots`/`thread` are reachable, so they would not be pruned.

- **The veto is load-bearing, not merely conservative.** Bypassing it lets the
  single-vCPU bytecode engine *compile* the module, but it then **traps `Malformed`
  at runtime** — the machinery behind `gc.roots`-with-threads (multi-vCPU root
  scanning / driving the scheduler seam) is genuinely not wired in the wasm-safe
  fast path. So the fix is engine work, not deleting a guard.

## Follow-up experiment: the multi-vCPU path (REFUTED) and the deeper trap

The single-vCPU powerbox (`compile_and_run_with_host`, what `svm_run_pb` uses) traps
`Malformed` once the compile-time veto is lifted. The obvious shortcut was to route
the linked module through the **multi-vCPU** driver (`drive_parallel` /
`compile_and_run_capture_over_parallel_with_host`, the native stand-in for the
browser's per-vCPU Workers, which already drives `thread.spawn`/join and scans
`gc.roots` across all vCPUs). `probe_svmb`'s `run_compare` tests both, with `write`
bound to a captured stdout:

```
imports: ["write"]
single-vCPU  (compile_and_run_with_host):  Trap::Malformed   stdout=""
multi-vCPU   (drive_parallel):             Trap::Malformed   stdout=""
```

**The multi-vCPU path is not a shortcut — it traps identically, before any output.**

A traced run locates the trap precisely, and it is **not** the `gc.roots + thread`
machinery the compile-time veto guards:

```
trapping frame: func 1 block 1 inst 38   (VcpuTlsGet; the trailing ConstI64 is the persisted cursor)
backtrace (inner→outer): [1, 236, 0]      (_start → f236 → f1)
```

The JACL runtime reads **per-vCPU thread-local storage** (`vcpu.tls`, `Inst::VcpuTlsGet`)
during startup. The tree-walker services it (`svm-interp/src/lib.rs:9183`, where the
stepper holds the `tls` register). The **fused bytecode engine has no dedicated `Op`
for it**, so it routes the instruction through the generic `eval_inst` fallback —
which returns `Trap::Malformed` for `VcpuTlsGet`/`VcpuTlsSet` unconditionally
(`svm-interp/src/lib.rs:10046`), because the pure evaluator has no vCPU context. Same
gap on single- and multi-vCPU, which is exactly why `drive_parallel` didn't help.

So there were **two layered bytecode-engine gaps** for JACL, discovered one at a time —
a compile-time veto and a runtime op. The runtime op is now closed.

## Op::VcpuTlsGet/Set — implemented, and the gap list bottoms out

The `vcpu.tls` gap is fixed in the vm repo (`interp: lower vcpu.tls on the bytecode
engine`): `compile_inst` now lowers `Inst::VcpuTlsGet`/`Set` to dedicated
`Op::VcpuTlsGet`/`Op::VcpuTlsSet` (mirroring `Op::DurableShadowBase`), the `Vm` carries a
`tls: i64` word seeded to the dense vCPU id (root = 0; `drive`'s `Spawn` arm re-seeds a
spawned thread to its index), and a differential test (`crates/svm-interp/tests/vcpu_tls.rs`)
pins it to the tree-walker. Full svm-interp suite green.

## The veto — removed, soundly

The `gc.roots + thread` term of `compile_module_with`'s seam veto is dropped (vm repo,
`interp: admit gc.roots + thread on the bytecode engine`). It was over-conservative:
`gc.roots` is per-vCPU conservative root enumeration, and the bytecode `step_vcpu` scans
the caller's continuation **plus the run-shared fiber registry** — exactly the
tree-walker's documented scope (`crates/svm/tests/gc_roots.rs`), a sound superset
(`tw ⊆ bc`, GC.md §3.2). Neither engine scans a *sibling thread's* own frames, and neither
has to: cross-thread GC soundness is the guest's stop-the-world quiesce
(`crates/svm/tests/gc_quiesce.rs`), and JACL's roots live in the migratable fibers the
shared registry covers. `drive_parallel` uses a per-thread fiber set, so there is no
cross-thread race. New tests (`crates/svm/tests/bytecode_gc_roots.rs`) pin that `gc.roots +
thread` and the JACL-shaped `gc.roots + thread + parked-fiber` run natively and stay sound;
full svm-interp + svm gc/concurrency suites green.

## End state — JACL runs on the wasm-safe engine

With both slices in the vendored svm (submodule bumped to the PR head), no env hack:

```
$ bash runtime/build.sh && cargo run --bin jacl_svm -- run --bytecode prog.jacl
# print "hi"  → hi
# generator   → 1 / 2
$ cargo run --bin probe_svmb -- prog.jacl
bytecode::compile_module (all-or-nothing): ACCEPTED ✓
single-vCPU  (compile_and_run_with_host):  Ok([I64(0)])   stdout="hi\n"
multi-vCPU   (drive_parallel):             Ok([I64(0)])   stdout="hi\n"
```

## Remaining work (beyond the engine gates, now closed)

1. **Multi-OS-thread fiber-migration `vcpu.tls` re-seed.** A fiber resumed on a *different*
   worker under `drive_parallel` work-stealing should read that worker's TLS word; today the
   `Vm.tls` seed follows creation, not migration. Not exercised by the browser tier
   (single-OS-thread cooperative, worker 0), so every browser read is a faithful `0`.
2. **The rest of the browser story** (unchanged from `SVM_BROWSER_PLAN.md`): the cdylib
   entry, Emscripten frontend→IR, `print`→JS, Pages CI. The *engine* blocker this doc chased
   is closed; that work is integration.
3. **Independently useful:** narrow `dead_func_elim`'s indirect-dispatch gate (bails on
   `cont.new`/`ref.func`, whose targets are statically known) so DCE prunes the linked module
   and helps the `svm-wasm-jit` mixed tier.

## Reproduction

```sh
bash runtime/build.sh                                   # -> runtime/build/jaclrt.svm
cd runtime/harness
cargo run --bin probe_svmb -- /path/to/prog.jacl        # measures + runs the linked module
cargo run --bin jacl_svm -- run --bytecode prog.jacl    # runs on the wasm-safe engine
```

No engine patch or env var is needed: the vendored svm carries the `vcpu.tls` lowering and
the veto removal.
