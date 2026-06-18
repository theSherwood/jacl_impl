# JACL on SVM — Phase 2 scope: program codegen

> Status: **scoping, pre-implementation.** Branch: `svm-backend-phase2` (stacked on
> `svm-backend-phase1`). Companions: `SVM_BACKEND_DESIGN.md`, `SVM_BACKEND_PLAN.md`
> (§4 Phase 2), `SVM_BACKEND_PHASE1.md` (the runtime this codegen calls into).
>
> Phase 0 (de-risk) and Phase 1 (runtime substrate: values, heap, GC, strings,
> collections, streams, builtins — green on interp + JIT) are done. Phase 2 makes
> JACL **programs** compile to SVM IR and run against that runtime.

## Goal

Retarget JACL's codegen: **AST → SVM IR** (via a JACL-owned IR builder), emitting
calls into the Phase-1 runtime, linked into one module and run on interp + JIT —
matching the existing bytecode VM's behaviour for **sequential** programs.
Concurrency (`yield`/`await`/`parallel`/`race`, the scheduler) is **Phase 3**.

**Definition of done:** a meaningful subset of sequential `test/jacl/*.jacl`
programs (literals, arithmetic, bindings, `if`/`while`/`for`, procs, closures,
strings, vectors, maps, error propagation) compile through the new codegen, link
against the runtime artifact, and run on interp + JIT with output matching the old
VM. (Full corpus parity is Phase 4.)

## What's preserved vs. new

- **Preserved (Phase-0 decision):** lexer, parser, AST, **typer**, macros (~14k).
  Phase 2 consumes the AST + `inferred_type` annotations; it does not change them.
- **New:** the IR builder + the codegen that walks the AST into it. The old
  `compiler.c` (bytecode emit + the SM transform) is the *reference*, not reused —
  its structure (type-aware walk, head dispatch) guides the rewrite.

## The two-producer / link model + calling convention

- **Runtime** = Phase-1 C → `svm-llvm` → an SVM IR artifact (exports `jacl_*`).
- **Program** = JACL AST → the IR builder → an SVM IR module that calls runtime
  functions via `call.import "jacl_add"` etc.
- **Link** = `svm_ir::link([runtime_unit, program_unit])` → one re-verified module.
- **ABI:** both producers must share the svm-llvm calling convention — **the data
  stack pointer is threaded as the leading parameter** of every function (svm-llvm
  §3d; Spike-1). JACL-emitted functions take `(sp, args…)` and pass `sp + frame`
  on calls, so program↔runtime calls interop. All `jacl_*` runtime functions take
  i64 `JaclVal`s and return i64.

## Prerequisites — the svm-side asks (P2.0) — **DONE** (svm pin `a7cbcf1`)

Separate-artifact linking + tagged-root GC needed three svm changes (the runtime
itself unchanged). All three landed in svm (`docs/SVM_PHASE2_ASKS.md`) and are
wired up + verified on the JACL side:

1. **`svm-llvm` name→index export map.** ✅ `Translated.exports: Vec<(String, u32)>`
   names each defined function so the runtime artifact's `jacl_*` exports populate
   `svm_ir::link`'s `LinkUnit.exports`.
2. **`svm-llvm-translate` CLI.** ✅ `runtime/build.sh` now runs `clang -emit-llvm`
   then `svm-llvm-translate` to emit `build/jaclrt.svm` (SVM-IR module) +
   `build/jaclrt.syms` (export sidecar) — a real linkable artifact, not just bitcode.
3. **`gc.roots` payload-mask parameter.** ✅ `heap_gc.c` passes `JACL_PAYLOAD_MASK`
   with the tight heap range; svm strips the tag byte and returns bare offsets, so
   tagged JaclVal roots are found without widening (the Phase-1 widen+mask workaround
   and its ASLR smell are gone).

**Verified:** the GC-path harness tests pass on interp + JIT with the new signature;
`build.sh` produces the artifact (58 `jacl_*` exports); and
`runtime/harness/tests/link_artifact.rs` proves a JACL-shaped program module links
against the separately-translated runtime (via the export map) and calls `jacl_add`
through `call.import`, matching on interp + JIT — the separate-artifact analogue of
Spike-1. JACL-side codegen (P2.1+) can use either the whole-program harness or this
separate-artifact path.

## Concrete steps

### P2.1 — The JACL IR builder (C, JACL-owned) — **DONE**
- **Goal:** an in-memory SVM IR module/func/block/inst representation, mirroring
  `svm-ir`'s shape, with a **text serializer** now and a **binary (`svm-encode`)**
  serializer later behind the same API (Design §4.4).
- **Deliverable:** ✅ `codegen/irbuilder.{h,c}`: `irb_module_new`, `irb_func_new`,
  `irb_block` (typed block params), the emit set (`irb_const_i32/i64`, `irb_intbin`,
  `irb_intcmp`, `irb_load`/`irb_store` + `irb_set_memory`, `irb_call`,
  `irb_call_import`, `irb_br`/`irb_br_if`/`irb_return`), and `irb_to_text`.
- **Validates:** ✅ `runtime/harness/tests/irbuilder.rs` — the builder's output for
  arithmetic, an SSA loop with block args, a direct call, a load/store memory
  round-trip, and a `call.import` linked against the runtime artifact all
  parse→verify→run identically on interp + JIT (Spike-1, but via the builder).
  Import-free output is asserted to be canonical svm-text.
- **Block-local SSA** discipline baked in: values are numbered per block and returned
  by each `irb_*`; cross-block values thread through branch args.

### P2.2 — Codegen scaffold: literals, arithmetic, return — **DONE**
- **Goal:** `compile_program(AST) → IR module`; lower literals, operator/builtin
  calls (→ runtime `call.import`), and top-level result.
- **Deliverable:** ✅ `codegen/codegen.{h,c}` — `svm_codegen_program(nodes, count)`
  walks the JACL AST (`src/jacl.h`) into the IR builder: an entry
  `func (i64 sp) -> (i64)` returning the last form's JaclVal. Lowers int literals
  (→ i32 JaclVal const) and `+ - * / %` (→ `jacl_add`/`sub`/`mul`/`div`/`mod` via
  `call.import`, left-folded for >2 args), threading `sp` (§3d ABI). JACL values are
  modelled as i64 SSA values (tagged JaclVals).
- **Validates:** ✅ `runtime/harness/tests/codegen.rs` parses real JACL source through
  the frontend (`codegen/tests/emit_jacl.c`, gcc), links each program against the
  runtime artifact, and runs on interp + JIT: `[+ 1 2]`, `[+ 1 [* 2 3]]`,
  `[- [* 4 5] 3]`, `[+ 1 2 3 4]`, `[+ [/ 20 6] [% 20 6]]` all match expected JaclVals.
  (`print` needs a host-I/O capability — deferred to the powerbox; full old-VM diff is
  P2.10 bring-up.)

### P2.3 — Bindings & scope — **DONE (sequential, single-block)**
- `def`/`mut`/`set`, lexical scopes, block bodies. Locals as SSA values threaded
  through blocks (or data-stack slots where address-taken). Same-scope shadow
  errors preserved.
- **Done:** `codegen.c` carries a compile-time lexical environment (a scope chain of
  name→SSA-value bindings). `def`/`mut` introduce a binding (mut flagged mutable),
  `set` updates the most-recent binding in place (errors on undefined / immutable),
  `$x` var-refs read it, and `{ … }` blocks push a nested scope evaluating to their
  last form. Same-scope immutable shadowing is rejected (matching the old compiler's
  "already defined in this scope"). Validated in `codegen.rs`: `def`+read, `set`
  mutation (incl. `set c [+ $c 41]`), block scope reading an outer binding, and the
  shadow / set-undefined / set-immutable error paths.
- **Deferred to P2.4:** mutation that must cross blocks (loop-carried `set`) needs
  block args; address-taken locals → data-stack slots. Today everything lives in one
  block, so a local is just its current SSA value.

### P2.4 — Control flow
- `if`/`elif`/`else`, `while`, `for` over ranges/collections → IR blocks +
  `br`/`br_if` with block args for loop-carried state. (`for` over a stream uses
  the runtime stream API.)

### P2.5 — Procs & calls
- JACL procs → SVM IR functions (data-SP ABI); args/returns, recursion, arity.
- Tail calls via `return_call` where applicable.

### P2.6 — Closures
- Captured environment → a runtime closure object (`JACL_TAG_CLOSURE`) holding
  upvalues + a funcref; call via `call_indirect`. Upvalue capture/mutation
  semantics (cells for `mut` captures). (Sequential only; generators are Phase 3.)

### P2.7 — Type-driven lowering (the gradual-typing payoff)
- Where the typer proved a static type, emit **unboxed** ops (native i32/f64
  arithmetic, direct field access) instead of dynamic `jacl_*` calls; fall back to
  dynamic dispatch for `dyn`. Mirrors the old compiler's type-aware codegen.

### P2.8 — Strings, structs, remaining value forms
- String literals + interpolation (→ runtime concat/format), struct construction +
  field access, vector/map literals → runtime calls.

### P2.9 — GC safepoints & root discipline
- Emit the `gc_epoch` safepoint poll at loop back-edges + call sites (obligation
  #1), piggybacked on svm's epoch site. Ensure live tagged JaclVals are findable as
  roots (needs P2.0 #3 / the gc.roots mask). Validate the GC under real programs.

### P2.10 — Bring-up (sequential subset)
- Drive a curated set of sequential `test/jacl/*.jacl` through the new codegen;
  diff output vs. the old VM. (Full parity = Phase 4.)

## Carried constraints

- **Block-local SSA** (Spike-1): cross-block values are block args, never
  dominance-use. The builder API should make this hard to get wrong.
- **Tagged roots / gc.roots mask** (P1.5): real programs hold tagged JaclVals on
  the stack — P2.0 #3 is the clean fix; until then the runtime's widen+mask
  workaround applies.
- **Runtime opacity** (P1.2/P1.3): keep the program↔runtime boundary a real call
  boundary (separate artifact / no cross-boundary inlining of allocation), or
  conservative roots constant-fold away.
- **svm-llvm gaps to respect in *generated* IR** (from Phase 1): i32 (not i64)
  `switch`; no `bcmp`/`memcmp`/`strcmp` (runtime provides what's needed); the
  generated IR uses the data-SP ABI.

## Deferred (not Phase 2)

- Concurrency: `yield`/`await`/`parallel`/`race`, fibers, the M:N scheduler,
  generators → **Phase 3**.
- Full corpus parity + perf pass → **Phase 4**.
- Cutover / retiring the old VM → **Phase 5**.

## Exit criteria

The DoD subset compiles via the new codegen, links against the runtime artifact,
and runs on interp + JIT matching the old VM; the codegen + IR-builder tests are
green. Then Phase 3 (concurrency on fibers) begins.

## Note on size

Phase 2 is the **largest** phase (it retargets ~18k of codegen + adds closures +
the IR builder + the data-SP ABI). Expect it to sub-phase in practice (2a builder
+ scaffold, 2b control/bindings/procs, 2c closures + type-driven lowering, 2d
strings/structs + bring-up). The runtime (Phase 1) and the de-risked mechanisms
(Phase 0) make each slice testable end-to-end on interp + JIT from the start.
