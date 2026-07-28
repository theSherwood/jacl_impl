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

That reframes the work: the compile-time gate is **one seam** (`gc.roots` coexisting
with threads), not the cap ops the spike named. But a follow-up experiment (below)
lifted that veto and drove the module through both the single- and multi-vCPU engines,
and both trap `Malformed` at runtime on a **different** op — `vcpu.tls`
(`VcpuTlsGet`), which the fused bytecode engine has no `Op` for. So the blocker is a
**layered op-coverage list** discovered one gap at a time (`gc.roots+thread` veto →
`vcpu.tls` → …), all real bytecode-engine/TCB slices differentially tested against the
tree-walker. The multi-vCPU path is the right execution *destination* but **not a
shortcut** — it shares the same fused-op coverage.

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

So there are (at least) **two layered bytecode-engine gaps** for JACL, and they are
discovered one at a time:

1. **Compile-time:** the `gc.roots + thread` seam-combination veto (declines the module).
2. **Runtime (once the veto is lifted):** `vcpu.tls` (`VcpuTlsGet`/`VcpuTlsSet`) has no
   fused `Op` and traps `Malformed` via `eval_inst`. Likely more gaps sit behind it.

## Recommended next steps (revised)

1. **The parity work is an op-coverage list, discovered iteratively — not one seam.**
   Add a dedicated fused `Op::VcpuTlsGet`/`Op::VcpuTlsSet` to the bytecode engine
   (mirroring the tree-walker's `tls`-register handling), lift the `gc.roots + thread`
   compile veto behind a soundly-driven all-vCPU root scan, re-run `probe_svmb`, and
   read off the next gap. Each is a small, differentially-tested slice against the
   tree-walker oracle; iterate until `print "hi"` runs green on the bytecode engine.

2. **The multi-vCPU driver is still the right execution model** (it services threads
   and scans all-vCPU `gc.roots`), but it shares the same fused-op coverage as the
   single-vCPU path — so it is a *destination*, not a shortcut around the op work.

3. **Independently useful:** narrow `dead_func_elim`'s indirect-dispatch gate. It bails
   on `cont.new`/`ref.func`, whose targets are statically known; only true
   `call_indirect` needs the conservative bail. Letting DCE fire shrinks the linked
   module and helps the `svm-wasm-jit` mixed tier regardless of the veto.

## Reproduction

```sh
bash runtime/build.sh                                   # -> runtime/build/jaclrt.svm
cd runtime/harness
cargo run --bin probe_svmb -- /path/to/prog.jacl        # measures the linked module
cargo run --bin jacl_svm -- run --bytecode prog.jacl    # exercise the wasm-safe engine
```

The "veto lifted → ACCEPTED" and "→ `Malformed`" rows require temporarily guarding
the seam-veto terms in `vendor/svm/crates/svm-interp/src/bytecode.rs`
(`compile_module_with`); `probe_svmb` and `jacl_svm --bytecode` report the result
unmodified.
