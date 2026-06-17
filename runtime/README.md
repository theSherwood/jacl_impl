# JACL runtime (on SVM) — Phase 1

The JACL runtime, written in C and compiled to SVM IR via `svm-llvm`. Scope and
step plan: `docs/SVM_BACKEND_PHASE1.md`. Architecture: `docs/SVM_BACKEND_DESIGN.md`.

## Layout

- `jaclrt.h` — `JaclVal` + tag/type scheme (mirrors `src/jacl.h`), value
  constructors/predicates/extractors. (P1.1)
- `tests/*.c` — C "driver" programs (each defines `int run(int)`) that exercise the
  runtime; stand in for Phase-2 codegen.
- `harness/` — Rust test harness: compiles a driver (+ runtime) with
  `clang -O2 -emit-llvm`, translates via `svm-llvm`, verifies, and runs `run` on
  **interp + Cranelift JIT**, asserting they agree.

Heap allocator (`heap.c`, P1.2), GC (`gc.c`, P1.3), strings, collections, stream
iterators, and builtins land in subsequent steps, with a unity `jaclrt.c` +
`build.sh` once there is non-inline runtime code.

## Run the tests

```
# one-time, for svm-llvm's libLLVM linkage:
apt-get install -y llvm-18-dev
cd runtime/harness && cargo test
```

Whole-program compilation (driver + runtime in one TU) is used in Phase 1;
separate runtime-artifact linking (which needs an svm-llvm name→index export map)
is a Phase-2 prerequisite, proven independently by `spikes/svm_emit_link`.
