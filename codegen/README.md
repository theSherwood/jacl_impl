# codegen — JACL → SVM-IR (Phase 2)

JACL-owned code generation for the SVM backend. Phase 2 retargets JACL's codegen to
**AST → SVM IR**, emitting calls into the Phase-1 runtime, linked into one module and
run on interp + JIT (see `docs/SVM_BACKEND_PHASE2.md`).

## Contents

- `irbuilder.h` / `irbuilder.c` — **P2.1: the IR builder.** An in-memory SVM-IR
  module/func/block/inst representation mirroring `svm-ir`'s shape, with a text
  serializer (`irb_to_text`) emitting the svm-text form that `svm_text::parse_module`
  accepts. The target of the codegen walk (P2.2+); a binary (`svm-encode`) serializer
  can later sit behind the same API.

  Instruction surface: `i32/i64.const`, integer binops + comparisons, `load`/`store`
  (with a `memory` declaration), direct `call`, name-inline `call.import`, and the
  `br` / `br_if` / `return` terminators.

  **Discipline:** SVM is *block-local SSA* — an instruction references only its own
  block's params (`v0..v{k-1}`) and earlier results; values needed elsewhere cross as
  branch arguments and are received as block params. The builder numbers values per
  block and returns each new id; the caller threads cross-block values explicitly.

- `tests/emit_demo.c` — builds sample modules through the builder and prints their
  svm-text (selected by argv), the C side of the round-trip test.

## Testing

`runtime/harness/tests/irbuilder.rs` compiles the builder + demo natively, emits each
module, then parses → verifies → runs it on interp + JIT (and checks import-free
output is canonical svm-text). Run from `runtime/harness/`:

```sh
cargo test --test irbuilder
```
