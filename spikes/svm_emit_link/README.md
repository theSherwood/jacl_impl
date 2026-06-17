# Spike-1 — JACL → SVM emit / verify / static-link / run

**Status: PASS** (svm `vendor/svm` @ f75509a).

Retires the riskiest unknowns of the SVM backend's *front door* before any bulk
work (`docs/SVM_BACKEND_PLAN.md` §2):

1. A JACL-shaped program emitted as **SVM text IR** parses and passes the
   **verifier** (text-format adequacy for our codegen output).
2. It runs **correctly on both the interpreter and the Cranelift JIT** (same
   result — the differential oracle).
3. `svm_ir::link` **static-links** a separately-authored *runtime* unit against the
   *program* units, resolving a cross-module call (`rt_add`) by name.

## Run

```
cd spikes/svm_emit_link && cargo run
```

Expected:

```
[2] linked 3 funcs; imports resolved: true
[3] verifier accepted the linked module
[4] run (interp == jit):
  PASS  combine(40, 2) = 42
  PASS  sum_to(100) = 5050
Spike-1 OK — emit → verify → static-link → run proven on interp and JIT.
```

It builds the svm crates from the submodule (Cranelift JIT included), so the first
run takes ~45s.

## What it exercises

- `combine(a,b) = rt_add(a,b)` — a straight-line cross-unit call.
- `sum_to(n) = 1+…+n` — a real SSA **loop** (blocks, `br`/`br_if`, block args) that
  calls the runtime's `rt_add` **inside the loop** (cross-unit call across a
  back-edge).
- The "runtime" unit (`rt_add`) stands in for JACL's LLVM-compiled runtime; here it
  is hand-authored text IR because the thing under test is the **IR + link
  contract**, not the LLVM compile (proven separately — see
  `docs/SVM_LLVM_INTRINSICS_ASK.md`). The `emit_*` functions stand in for JACL's
  codegen emitter (in the real backend, the C emitter).

## Finding that shapes JACL's codegen

**svm uses block-local SSA.** A block may reference only its own block parameters
and values defined within it — **not** values from dominating blocks. Everything
that crosses a block boundary (loop-carried `n`, `acc`, `i`, …) must be threaded
explicitly as a **block argument**. JACL's codegen must follow this discipline
(it's the SSA analogue of "no implicit dominance use"); see the loop in
`src/main.rs` for the pattern. Recorded as a codegen obligation in
`docs/SVM_BACKEND_DESIGN.md`.
