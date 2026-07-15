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

## Builtin entry points (Phase-4 additions)

The codegen calls into these `jacl_*` C entry points (all take/return `JaclVal`,
threading the data-stack pointer where relevant). Recent parity slices added:

- **Collections/strings.** `jacl_vec_slice` (half-open sub-vector), `jacl_str_index`
  / `jacl_str_slice` (1-char access / substring), `jacl_index_op` / `jacl_slice_op`
  (dispatch `[index]` / `[slice]` over string vs vector/array), `jacl_first`,
  `jacl_range_inclusive`, and `jacl_vec_reduce` (fold an arithmetic operator over a
  collection — the runtime side of `[+ ..$v]`).
- **Iteration.** `jacl_iter_val_at` / `jacl_iter_key_at` give the i-th value/key of any
  iterable (vector/array index → element, map → i-th HAMT entry), so a single
  `for`-each lowering serves vectors, arrays, and maps (one- and two-name forms).
- **Optional chaining.** `jacl_qdot` (`?.`): nil short-circuits, a map reads its entry,
  and a struct is rejected (use `->`).
- **Error semantics.** Error-flagged values print as `<error: PAYLOAD>`; the error flag
  propagates through `jacl_map_get`/`set`/`remove`, `jacl_str_concat`, and the
  `[vec-push]` / `[vec-slice]` / `[range-inclusive]` builtins.

All entry points fail closed (return an error value) on a type mismatch rather than
misreading a foreign pointer, so a codegen slice can wire up a head before the full
static check exists without risking memory unsafety.
