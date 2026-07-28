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

That reframes the work as a **layered gap list** discovered one at a time, not the cap
ops the spike named:

1. **Compile-time:** the `gc.roots + thread` seam veto (declines the module).
2. **Runtime (veto lifted):** `vcpu.tls` (`VcpuTlsGet`) — the fused bytecode engine had
   no `Op` for it and trapped `Malformed`. **Now fixed** (see "Op::VcpuTlsGet/Set").
3. **…nothing.** With `vcpu.tls` lowered *and* the veto lifted, `print "hi"` **and** a
   generator now run to completion on the bytecode engine — single- **and** multi-vCPU —
   with correct output (`"hi\n"`, `"1\n2\n"`). **There is no gate #3 for this class.**

So after the `vcpu.tls` slice, the **only** remaining blocker for simple programs is the
`gc.roots + thread` compile-time veto — and lifting it needs a *sound* fix (a bytecode
`gc.roots` that scans every vCPU, matching the tree-walker), not just the env-gated
bypass the probe uses. The multi-vCPU path is the right execution *destination* but was
**not** a shortcut — it shares the same fused-op coverage (both engines trapped
identically before the fix, and both run green after).

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

Re-running `probe_svmb` with that engine change and the veto lifted:

```
imports: ["write"]
single-vCPU  (compile_and_run_with_host):  Ok([I64(0)])   stdout="hi\n"
multi-vCPU   (drive_parallel):             Ok([I64(0)])   stdout="hi\n"
```

and the generator prints `"1\n2\n"` on both engines. **The gap list bottoms out — no
gate #3.** Once `vcpu.tls` is lowered, the only thing standing between the bytecode
engine and a running JACL program is the compile-time `gc.roots + thread` veto.

## Recommended next steps (revised)

1. **Sound-lift the `gc.roots + thread` veto — the sole remaining blocker.** The probe
   lifts it with an env-gated bypass, which is unsound for a *real* multi-worker run: the
   bytecode `gc.roots` driver scans only the calling vCPU's continuation, so a sibling
   worker's roots go unscanned. The proper fix is a bytecode `gc.roots` that scans **every**
   vCPU (the tree-walker's behaviour), after which the veto term can be removed. For the
   **browser tier specifically** — single-OS-thread cooperative, one worker — there are no
   siblings, so a narrower "single-worker ⇒ sound" lift may suffice; either way it is one
   focused TCB slice, differentially tested. This unblocks the whole simple-program class.

2. **The multi-vCPU driver is the right execution *destination*** (it services threads and
   scans all-vCPU `gc.roots`), but it was **not** a shortcut — before the `vcpu.tls` fix
   both engines trapped identically; after it, both run green. Route JACL through it once
   the veto is sound.

3. **Independently useful:** narrow `dead_func_elim`'s indirect-dispatch gate. It bails on
   `cont.new`/`ref.func`, whose targets are statically known; only true `call_indirect`
   needs the conservative bail. Letting DCE fire shrinks the linked module and helps the
   `svm-wasm-jit` mixed tier regardless of the veto.

> **Reproduction note.** The `vcpu.tls` lowering lives on the vm repo's
> `claude/jacl-svm-backend-migration-1hulv3` branch; the `vendor/svm` submodule here still
> pins the pre-change commit, so reproducing the "runs green" rows needs a vendored svm that
> includes that commit (plus the env-gated veto lift). Bumping the submodule pin is a
> follow-up once the veto is soundly lifted.

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
