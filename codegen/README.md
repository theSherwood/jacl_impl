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

- `codegen.h` / `codegen.c` — **the codegen walk.** `svm_codegen_program` walks the
  JACL AST (from `src/jacl.h`) and emits an SVM-IR module via the builder, with a
  single entry `func (i64 sp) -> (i64)` returning the last top-level form's JaclVal.
  - **P2.2 (literals + arithmetic):** integer literals (→ i32 JaclVal constants) and
    `+ - * / %` (→ runtime `jacl_add`/`sub`/`mul`/`div`/`mod` via `call.import`,
    left-folded for >2 args), threading `sp` per the §3d ABI.
  - **P2.3 (bindings & scope):** a compile-time lexical environment (scope chain of
    name→SSA-value). `def`/`mut` bind, `set` updates the most-recent binding in place
    (errors on undefined / immutable), `$x` reads it, `{ … }` blocks open a nested
    scope. Same-scope immutable shadowing is an error.
  - **P2.4 (control flow):** the walk tracks a *current block* and threads a frame
    (`sp` + all live locals) through block params on every control-flow edge
    (block-local SSA). `if`/`elif`/`else` → an SSA diamond joining on the frame + the
    if's result; `while` → header/body/exit with a back-edge; `for NAME in RANGE`
    (integer ranges) desugars onto the loop machinery. Conditions reduce to an i32
    truth (`(c != false) & (c != nil)`); comparisons lower to `jacl_lt/le/gt/ge/eq/ne`.

  - **P2.5 (procs & calls):** top-level `proc`s become SVM functions
    (`func (i64 sp, i64 a0…) -> i64`); codegen is two-pass (register all procs, then
    compile bodies) so recursion resolves. Calls resolve by name against the proc
    table (with an arity check) and thread `sp` unchanged. Proc bodies compile in
    *tail position*: a tail call becomes `return_call` and a tail `if` returns from
    each branch, so tail recursion is constant-stack.

  - **P2.6 (closures):** lambda `[\ {params} {body}]` and anonymous
    `[proc {params} {body}]` become a runtime closure object (`ref.func` funcref +
    captured upvalues, built with the `runtime/closure.c` helpers); a call `[$f a]`
    dispatches via `call_indirect`. Free vars are found by a capture scan (transitive
    through nested closures); closure bodies compile on a deferred worklist. A captured
    `mut` is boxed in a heap **cell** (`jacl_cell_*`) so mutation is shared between the
    defining scope and the closure (a per-function pre-pass decides which `mut`s to box).

  - **P2.7 (type-driven lowering):** the driver runs the typer (`typer_infer`), and
    where it proved an arithmetic expression is `i32` (`+ - *`) the codegen emits
    native `i32.add`/`sub`/`mul` (operands unboxed, result boxed once) instead of a
    dynamic `jacl_*` call; `dyn` falls back to the dynamic path. Unboxing is confined
    to typed arithmetic trees — locals/frame stay all-i64 boxed JaclVals.

  Later slices extend the walk (`for` over collections, break/continue, more unboxed
  types/ops) behind the same entry point.

- `tests/emit_demo.c` — builds sample modules through the builder and prints their
  svm-text (selected by argv), the C side of the IR-builder round-trip test.

- `tests/emit_jacl.c` — parses a JACL source snippet through the real frontend
  (`src/jacl.c`: lexer + parser + `typer_infer`) and runs `svm_codegen_program`,
  printing the program module's svm-text. Built with **gcc** (the frontend's toolchain).

## Testing

`runtime/harness/tests/irbuilder.rs` compiles the builder + demo natively, emits each
module, then parses → verifies → runs it on interp + JIT (and checks import-free
output is canonical svm-text). `runtime/harness/tests/codegen.rs` does the same from
real JACL source through `svm_codegen_program`, linking each program against the
runtime artifact. Run from `runtime/harness/`:

```sh
cargo test --test irbuilder
cargo test --test codegen
```
