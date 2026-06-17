# Spike-3 — fibers replace the state-machine transform

**Status: PASS** (svm `vendor/svm` @ f75509a; needs `clang`, `cc`, `llvm-18-dev`).

Validates the headline simplification of the backend rewrite (`docs/SVM_BACKEND_DESIGN.md`
§4.3.1): everything JACL's compile-time **state-machine transform** does for
generators, svm **stackful fibers** (`cont.*`, exposed in C as `__vm_fiber_*`) do at
runtime — simpler, and with strictly more capability. Deleting the SM transform
removes JACL's most bug-prone subsystem.

## Run

```
cd spikes/svm_fiber_generator && cargo run
```

```
[2] svm-llvm translated it (6 funcs); imports: []
[3] verifier accepted the translated module
[4] run (interp == jit), vs closed form a+b+c:
  PASS  run(5) = 85
  PASS  run(10) = 520
  PASS  run(20) = 3440
[5] native oracle (no fibers): run(5) = 85  (matches svm)
```

## What it demonstrates

A generator runtime written in plain C on the fiber primitives, compiled
`clang -O2 -emit-llvm` → `svm_llvm` → run on interp + Cranelift JIT:

- **`gen_range`** — a loop generator that `suspend`s each value (the
  `for`-over-a-stream case).
- **`gen_deep`** — the key one: it `suspend`s from inside `emit()`, a
  `noinline` nested call. Stackful fibers switch the whole control stack, so a
  yield works from *any* call depth. The SM transform cannot do this without
  coloring `emit`/`emit_squares` as suspending and rewriting every call site —
  here they are ordinary functions. **Capability the SM never had, for free.**
- **`interleave`** — two `gen_range` fibers resumed alternately, each keeping its
  own stack/state. Independent live coroutines coexist (what the M:N scheduler
  builds on).

`run(n) = sum(0..n-1) + sum(1..n²) + 2·sum(0..n-1)`, checked against the closed
form on both backends and an independent native oracle.

## Takeaway

No compile-time transform, no state-field layout, no resume-point dispatch tables,
no coloring — a generator is just a fiber. The SM transform (the ~3k-line subsystem
slated for deletion in the rewrite) is replaceable by these primitives, which also
do deep yield that the SM couldn't.
