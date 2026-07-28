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

That reframes the work from "extend the engine's op coverage (fibers / cap.self)"
to "**one seam** — `gc.roots` coexisting with threads at runtime startup — gates
every JACL program." It is real engine/TCB work (the veto guards a genuine runtime
trap, not just conservatism), but it is one well-localized capability, and there is
an existing multi-vCPU path that may already clear it.

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

## Recommended next steps

1. **Try the multi-vCPU path first (cheapest possible unlock).** The browser's
   `svm_run_pb` uses the **single-vCPU** powerbox (`compile_and_run_with_host`) —
   exactly what traps. The cdylib also exposes a **multi-vCPU resumable** path
   (`VcpuProgram` / `svm_par_run`) that already drives `thread.spawn`/join. Route
   the linked JACL module through it and re-check `print "hi"` + the generator.
   This may run green with no engine change.

2. **If not, the parity slice is specific:** make the wasm-safe interpreter drive
   JACL's `gc.roots`-with-threads startup — teach the fast path's `gc.roots` to scan
   sibling vCPU continuations and service the scheduler seam — so the `gc.roots +
   thread` veto can be lifted soundly. One capability, differentially tested against
   the tree-walker, gating the whole JACL class at once.

3. **Independently useful:** narrow `dead_func_elim`'s indirect-dispatch gate. It
   bails on `cont.new`/`ref.func`, whose targets are statically known; only true
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
