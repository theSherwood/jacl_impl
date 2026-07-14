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

  - **P2.8 (strings + vectors):** string literals build a `data` segment + a relocated
    address const and call `jacl_str_new`; interpolation / `[concat …]` fold
    `jacl_str_concat` (with `jacl_to_string` of expression segments). `[vec …]` →
    `jacl_vec_empty` + `jacl_vec_push`; `[length X]` → `jacl_len`. The IR builder gained
    data segments + `SelfData` relocations (`irb_add_data`/`irb_data_addr`/
    `irb_relocs_text`); the driver emits relocs after a `%%RELOCS%%` sentinel and the
    harness passes them to `svm_ir::link`.

  - **P2.9 (GC):** the codegen entry initializes the runtime (`jacl_heap_init` /
    `jacl_intern_init` / `jacl_map_init`) before the program runs. GC is stop-the-world,
    conservative, non-moving — `jacl_alloc` collects on pressure, and because the
    collector is conservative the allocation point is the safe point (no `gc_epoch`
    polling). Live roots in codegen-shaped IR are found by `gc.roots` across a collect.
  - **P2.10 (bring-up):** combined programs across the subset are diffed against the
    old bytecode VM (the driver's `--oldvm` mode runs `jacl_eval` in-process); the test
    `bringup_matches_old_vm` asserts new-codegen interp == JIT == old VM. This surfaced
    non-canonical forms the codegen accepts (`for NAME in …`, `[\ {params}{body}]`, flat
    `elif`/`else`, vector `length`) to reconcile in Phase 4.

  Later slices (Phase 4) reconcile the canonical surface forms (with macro expansion),
  add maps + element access and structs, and drive the full corpus.

- `tests/emit_demo.c` — builds sample modules through the builder and prints their
  svm-text (selected by argv), the C side of the IR-builder round-trip test.

- `tests/emit_jacl.c` — parses a JACL source snippet through the real frontend
  (`src/jacl.c`: lexer + parser + `typer_infer`) and runs `svm_codegen_program`,
  printing the program module's svm-text. Built with **gcc** (the frontend's toolchain).

## Phase 4 — corpus bring-up (parity scoreboard)

`runtime/harness/src/bin/parity.rs` drives the whole `test/jacl/*.jacl` corpus through
the real pipeline (frontend → svm codegen → link → interp==jit) and scores each case
against its inline `# expect:` / `# expect-error:` oracle, writing `docs/SVM_PARITY.md`.
Phase 4 works that scoreboard cluster by cluster. Slices landed so far:

- **Static-error oracle (the driver).** `tests/emit_jacl.c` runs the old VM's full
  `compiler_compile` purely for its compile-time diagnostics, on its OWN re-lexed/
  re-parsed AST (so the compiler's conformance pass and the SVM codegen never mutate a
  shared tree). Every corpus program is written for the old VM, so the compiler accepts
  each passing case and rejects each `# expect-error` case with its canonical message —
  exactly the accept/reject the SVM backend must mirror. This resolves the bulk of the
  typed/struct/ctx/buf/generator/typed-closure `expect-error` corpus without duplicating
  those checks in the codegen. On rejection the emitted module is discarded.

- **New command heads.** `?.` (optional chaining), `not` / `~`, `vec-slice`,
  `range-inclusive`, `index` / `slice` (string + collection), `first` / `count`
  (draining a generator source), `take` (bounded stream drain), the 3-arg dot mutation
  `[. EXPR key VALUE]`, typed pointers as raw u64 addresses (`ptr-null` / `ptr-cast` /
  `ptr-addr`), and a `stack-trace` stub (empty string).

- **Typed `Map` constructor** `[[Map K V] k0 v0 …]`, alongside the existing typed
  `Vec`/`Arr`/`Buf` constructors.

- **`for`-over-map.** One name binds the value; two names bind key + value. Iteration
  goes through uniform `jacl_iter_val_at` / `jacl_iter_key_at` accessors (vec/arr index
  → element, map → i-th key/value in deterministic HAMT order), so the two-name form
  also works over vectors and arrays (index + element).

- **`assert` predicate form** `[assert OP a b …]` (evaluate `[OP a b …]`, assert truth).

- **Old-VM-shaped arity messages** for builtins and operators (correct singular/plural).

- **Runtime error semantics.** `print` / `to-string` render error-flagged values as
  `<error: PAYLOAD>`; error propagation flows through `map-get`/`set`/`remove`,
  `str-concat`, and `vec-push`.

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

The full corpus scoreboard is regenerated with:

```sh
cargo run --release --bin parity      # writes docs/SVM_PARITY.md
```
