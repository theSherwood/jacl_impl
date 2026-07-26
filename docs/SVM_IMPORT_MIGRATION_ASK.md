# Ask for svm — a supported embedding path for a separate-artifact frontend (jacl)

**Audience:** the svm agents (`theSherwood/vm`). **Consumer:** jacl
(`theSherwood/jacl_impl`) — the shell-like language already pinned as a §7/§3.6
consumer (`IMPORTS.md §3.6`, "Consumer pinning — jacl, 2026-07-22").

**TL;DR.** The IMPORTS.md phase-4 rewrite (`bd0e2b8b`, "delete the legacy import
machinery") + the by-name embedding unification (`322527ce`) removed the exact API jacl
depended on to go from *separately-compiled artifacts* to a *runnable powerbox module*,
and the surviving `link` is built for the C model (whole-program translation, no separate
runtime carrying live capability imports). jacl cannot refresh past svm `29b865dd` without
a supported replacement. This doc states jacl's shape precisely and asks for one public
entry point that serves it. **All references below are against svm `4937780f` (current
`main`).**

## Why jacl is different from the C on-ramp

The C frontend hands svm-llvm one translation unit — program *and* its runtime together —
so svm-llvm synthesizes the `_start`, sees every `write()` as a manifest import, and the
result is a single self-contained module. No linking of separate artifacts, ever.

jacl compiles **two artifacts that must be linked at the IR level**:

1. **The runtime** — `runtime/jaclrt.c`, compiled by clang and translated by
   `svm_llvm::translate_bc_path`. Its shape today (verified):
   ```
   memory 25
   import 0 func "write" 0            # a live §7 capability import (jacl_print → write)
   export 0 func "jacl_heap_init" 0
   export 1 func "jacl_heap_lo" 1
   … ~200 more `export … func "jacl_*"` …
   ```
   It is a **library with one capability import** (`write`; `exit` appears when a program
   halts). It has no `main`, no `_start`.

2. **The program** — jacl's own codegen (`codegen/codegen.c` → svm-IR text) emits the
   user program as functions that call the runtime **by symbol name**, e.g.
   ```
   func (i64) -> (i64) {
   block0(v0: i64):
     call.import "jacl_heap_init" (i64) -> () v1(v0)
     …
     v7 = call.import "jacl_sched_run_main" (i64, i64) -> (i64) v6(v0, v5)
     return v7
   }
   ```

The two are joined with `svm_ir::link([runtime, catalog, program])`, then the linked
module gets a powerbox `_start` and runs on interp+jit under the differential oracle. This
is jacl's whole backend. **This is the shape svm needs to keep supporting.**

## The four concrete breakages (svm `4937780f`)

### 1. `resolve_capability_imports` deleted — *has a clean replacement (non-blocking)*

`svm_run::resolve_capability_imports` (lower the runtime's `write` import to a `cap.call`)
was removed in `bd0e2b8b`. **Resolved on jacl's side:** `svm_ir::resolve_imports_with(&m,
|n| svm_run::default_cap_resolver(n).map(Into::into))` reproduces it (a
`Resolved: From<ResolvedCap>` composes them). Recorded here only so the inventory is
complete — no action needed.

### 2. `synth_powerbox_start` family deleted — *no replacement for a non-C frontend* ⛔

`svm_ir::synth_powerbox_start` / `_with_names` / `_for_imports` + `build_powerbox_start`
were the **frontend-neutral** powerbox bootstrap — `(linked Module, entry funcidx) →
Module with a paramless _start`. They were the deliberate answer to
`docs/SVM_POWERBOX_ASK.md` ("expose the bootstrap independently of the C frontend"), and
phase-4 deleted them. svm-llvm still synthesizes `_start` **internally** (`synth_start`,
`crates/svm-llvm/src/lib.rs`), but that is private to the C path. A frontend that emits IR
directly and links it now has **no public way to obtain a `_start`**.

Note the *good* news that makes a replacement cheap: the new `_start` is trivial — paramless,
**no handle stash, no cap-resolve prologue** (IMPORTS.md phase 3: capabilities are manifest
slots the host binds before entry). For jacl it is literally *"set `sp = entry_sp`; call the
entry; return"* (jacl does not seed the svm heap — the old call site passed
`seed_heap=false`). Every input it needs is already public: `powerbox_entry_sp(&linked)`
(`svm-ir` L2804), `POWERBOX_STACK_RESERVE` (L2746), `POWERBOX_HEAP_BRK/TOP`. The only thing
missing is a public function that assembles them onto a linked module.

### 3. `link` discards the linked module's import table ⛔ — *the load-bearing blocker*

`svm_ir::link` (`crates/svm-ir/src/lib.rs` L3494) resolves each unit's **`CallSym`** imports
by name against the merged exports (L3583, `resolve_imports_with(&m, |name|
funcs_tab.get(name)…Resolved::Func)`) and then returns a module with

```rust
// crates/svm-ir/src/lib.rs:3642
imports: Vec::new(),
```

So the runtime's `import 0 func "write"` — a genuine §7 capability import that **no unit
exports and that `link` must carry forward** — is **silently dropped**, while the runtime's
`call.import 0` that dispatches it survives (the comment at ~L3670 says `call.import`
indices are "untouched — resolved separately"). The result references import slot 0 with an
empty import table: a dangling capability call.

`link` currently assumes *every* cross-unit reference is a `CallSym` that resolves to a
concrete `Func` — i.e. it assumes **no surviving manifest capability import**. That is true
for the C model (one TU, no separate runtime) but false for any frontend whose runtime is a
separate artifact with live capability imports. **This is the change that makes jacl's link
model unrepresentable.**

### 4. `_start`-must-be-func-0 collides with import-type reindexing ⛔

`svm_run::is_named_powerbox_entry` (L2693) requires the entry to be **function 0** (paramless
+ `export "_start" → 0`). To place a synthesized `_start` at func 0 of the *linked* module,
either the program links first (then the runtime's `call.import`/import **type-section
indices** are reindexed by `link`, and a hand-reattached import table from breakage #3 must
track that reindexing), or `_start` is prepended and every funcidx shifts (needs the
now-private `offset_func_indices`). Both routes force a frontend to reverse-engineer `link`'s
internal func/type reindexing — exactly the "internal, not a public contract" layout the
project keeps private. There is no public seam that lets a frontend put a `_start` at func 0
*and* keep a reindexed manifest import valid.

## What jacl asks for

**One public entry point that turns a set of linked artifacts + a fixed powerbox into a
runnable manifest module**, preserving capability imports. Any of these shapes works; they
are ordered by how much they'd simplify jacl:

1. **Best — `link` preserves unresolved manifest imports.** Make `svm_ir::link` carry each
   unit's declared imports into the merged module (merged import table, with the `call.import`
   index / type-section reindexing done *inside* `link` so it stays consistent), instead of
   `imports: Vec::new()`. Then jacl links `[runtime, catalog, program]`, gets a module that
   still imports `write`/`exit`, and instantiates it — no hand-reattachment, no index math.
   This fixes breakage #3 directly and is the single highest-leverage change.

2. **Re-expose a frontend-neutral `_start` synthesizer** — the public successor to the
   deleted `synth_powerbox_start`: `(module, entry_funcidx, cap_names) → module` that
   prepends the paramless `_start` at func 0, shifts funcidxs, sizes the window from
   `powerbox_entry_sp`, and adds the `export "_start" → 0`. This fixes #2 and #4 together and
   restores the `SVM_POWERBOX_ASK.md` guarantee for non-C frontends. Combined with (1) it is
   the complete path.

3. **A one-call embedding wrapper** — `link_and_powerbox(units, entry_symbol,
   cap_names) → Instance` (or `→ Module` feeding `instantiate`) that does link +
   import-preservation + `_start` synthesis + window sizing behind one function, so a frontend
   never touches funcidx/type reindexing at all. This is the "wasm-level ease for a
   separate-artifact frontend" analog of what svm-llvm gives C.

Whatever the shape, the **invariant jacl needs** is: *a separately-compiled runtime that
carries live §7 capability imports can be linked to a program and made into a powerbox
manifest module through public API, without a frontend reconstructing `link`'s internal func
or type reindexing.*

## Acceptance (jacl's actual smoke test)

Given the real `runtime/jaclrt.c` translation (`import "write"` + `jacl_*` exports) linked
with a jacl-codegen'd program for `[print [+ 40 2]]`:

- the linked module still declares `import … "write"` (and `"exit"`), with its `call.import`
  slot/type indices consistent;
- it exports a paramless `_start` at func 0 that sets `sp = powerbox_entry_sp` and calls the
  entry;
- `instantiate` + a fixed-powerbox run prints `42` on **interp == jit**, un-granted `fs`
  falls back (jacl's existing behavior);
- no jacl-side code reads a non-public svm layout constant or reimplements func/type
  reindexing.

That is exactly the pipeline in `runtime/harness/src/bin/parity.rs` (558 corpus cases),
which is how jacl will validate the refresh once the path exists.

## Cross-references

- svm: `IMPORTS.md §3.6` (jacl consumer pinning), `bd0e2b8b` (phase-4 deletion),
  `322527ce` (by-name unification), `crates/svm-ir/src/lib.rs` L3494/L3583/L3642 (`link`),
  L2693/L2804 (`is_named_powerbox_entry`/`powerbox_entry_sp`), `crates/svm-llvm/src/lib.rs`
  `synth_start` (the private C-path bootstrap this asks to re-expose).
- jacl: `docs/SVM_POWERBOX_ASK.md` (the original frontend-neutral-bootstrap ask this
  regression reopens), `docs/SVM_EXEC_ASK.md` (the exec cap, already delivered — jacl wants
  it, and this migration is the gate to consuming it), `runtime/harness/src/bin/parity.rs`
  (the link → powerbox → interp==jit pipeline), `codegen/codegen.c` (the program-module
  emitter), `runtime/jaclrt.c` (the runtime artifact with the `write` import).
