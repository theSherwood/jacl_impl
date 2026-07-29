# Macro-staging migration: run compile-time macros on SVM

> **Status:** plan. Slice 2 of `SVM_SELFHOST_FEASIBILITY.md`. Goal: evaluate
> compile-time macros on the **SVM backend** instead of JACL's legacy bytecode VM
> (`src/vm.c`), so the compiler has **one** execution engine and the legacy VM can be
> dropped from the toolchain.

## The problem, precisely

JACL macros are compile-time functions: a `defmacro` body runs at compile time,
taking the call's argument AST as **syntax objects** and returning a syntax object
that replaces the call. Today that evaluation runs on JACL's **original** stack-based
bytecode interpreter:

```
emit_jacl.c: expand_macros_inplace
  → ast_expand_macros                    (src/syntax.c:1159)
    → expand__compile_staged_body        (src/syntax.c:1091)  AST → BytecodeChunk
        via compiler__init / compiler__compile_block_expr     (src/compiler.c)
    → jacl_ctx_run_closure               (src/vm.c:14629)     runs on…
        → vm__run                        (src/vm.c:2730)      the legacy bytecode VM
```

The SVM migration moved the **runtime** to SVM but left **compile-time** evaluation
on `vm.c`. So `jacl_compiler.svmb` bundles the whole legacy interpreter and, at
compile time, runs macros on *(vm.c compiled to SVM)* — an interpreter on an
interpreter. This plan re-hosts macro evaluation on SVM so `vm.c`, `bytecode.c`, and
the bytecode half of `compiler.c` fall out.

**Scope note (only user macros hit the VM).** On the emit path the driver sets
`jacl_expand_skip_prelude = 1` (`emit_jacl.c:295`); the 7 prelude macros (`\`, `and`,
`or`, `not`, `incr`, `timeout`, `assert`) are lowered **by name** in `codegen/` and
never touch the VM. Only **user `defmacro`s** are staged-evaluated. So the migration's
observable surface is user-defined macros.

## Why this is bigger than "re-point the evaluator"

The blocker is that the **SVM runtime has no syntax support at all**:

- The value ABIs match in *format* — both are 64-bit tagged, same tag indices
  (`src/jacl.h:42-91` ≡ `runtime/jaclrt.h:20-46`) — **but pointer semantics differ**:
  the legacy heap packs a **native pointer** in the payload (`JACL_PACK_PTR`), the SVM
  runtime packs a **window offset**. A legacy `JaclSyntax*` value is meaningless inside
  the SVM window. Heap objects are **not** bit-portable; they need reconstruction.
- `runtime/jaclrt.h` **omits** `JACL_TAG_SYNTAX` (0x17); `runtime/heap_gc.c` has **no**
  `OBJ_SYNTAX`; `codegen/codegen.c` has **zero** syntax code. The entire 20-kind
  `JaclSyntax` model and the ~450-line `OP_SYNTAX_OP` surface (make-syntax,
  syntax-quote/datum/str/kind, syntax-error, gensym — `src/vm.c:11532-11978`) live
  **only** in the legacy VM.
- `svm_codegen_program` (`codegen/codegen.c:4310`) is **whole-program, fixed-entry**:
  func 0 = runtime init → `jacl_sched_run_main`, func 1 = `__jacl_main` as a fiber root
  task. It has no "compile this closure body to a callable `f(syntax…) → syntax`" path.
- **Hygiene/gensym** (`vm.macro_scope_mark`, `vm.gensym_counter_ptr`) is host-side
  mutable state read *inside* the running macro; on SVM it must be passed in via ABI.

So re-hosting macros means porting JACL's syntax runtime + a macro-body codegen entry
+ cross-heap marshalling to the SVM backend. Substantial, but bounded and testable.

## Key seams (from the subsystem map)

| Seam | Signature / location |
|---|---|
| compile body → closure | `expand__compile_staged_body(MacroEntry*, ThreadHeap*, JaclInternTable*, arena_t*)` — `src/syntax.c:1091` |
| run closure (the swap point) | `JaclVal jacl_ctx_run_closure(jacl_context_t*, JaclClosure*, JaclVal*, u32, JaclError*)` — `src/vm.c:14629` |
| AST → syntax value | `JaclVal syntax_from_ast(AstNode*, ThreadHeap*, JaclInternTable*)` — `src/syntax.c:39` |
| syntax value → AST | `AstNode *syntax_to_ast(JaclVal, arena_t*)` — `src/syntax.c:354` |
| syntax object | `JaclSyntax` (tag 0x17, 20 `SyntaxKind`s, hygiene fields) — `src/jacl.h:400-429` |
| SVM codegen (whole-program) | `IrModule *svm_codegen_program(AstNode**, u32, int module_mode, char*, size_t)` — `codegen/codegen.c:4310` |
| syntax ops (legacy-only) | `OP_SYNTAX_OP` handler — `src/vm.c:11532-11978` |

## Differential oracle

Every phase is gated by a **byte-for-byte IR diff**. A macro-using program compiled
through the current (`vm.c`) path yields golden IR; the SVM-staged path must produce
**identical** IR. Confirmed working: `unless` (from `test/jacl/tour.jacl`) expands and
emits stable IR today. The corpus + harness live in `codegen/selfhost/macro_staging/`
(Phase 0).

## Phases

Each phase is independently landable and testable. `JACL_STAGE_ON_SVM` gates the new
path so the `vm.c` path stays the default (and the oracle) until parity is proven.

- **Phase 0 — Differential harness + corpus.** A set of user-`defmacro` programs
  (arithmetic templates, hygiene/gensym, nested/recursive macros, variadic, error
  cases) and a harness that snapshots each program's emitted IR via the current path.
  The gate for every later phase. *No runtime changes.* **(Started.)**

- **Phase 1 — Syntax object model on `jaclrt`.** Add `JACL_TAG_SYNTAX` + `OBJ_SYNTAX`,
  the `JaclSyntax` layout, `gc_alloc_syntax`, and GC tracing to `runtime/heap_gc.c`.
  Unit-test alloc + trace + field access in the SVM runtime. *Decision to make here:*
  port the special GC object as-is, **or** represent syntax as ordinary JACL data
  (records/vecs) so codegen handles macros as normal code and no new GC type is needed
  — evaluate the trade before committing (the data-rep option could collapse Phases
  1–2, at the cost of a syntax-model redesign).

- **Phase 2 — Syntax builtins on SVM.** Re-express the `OP_SYNTAX_OP` surface
  (make-syntax subops, `syntax-kind/datum/str`, `syntax-error`, `gensym`) as `jaclrt`
  C builtins, and lower the `make-syntax`/`syntax-*` forms in `codegen/` to calls on
  them. Differential-test each op against the `vm.c` implementation.

- **Phase 3 — Macro-body codegen entry.** A codegen path that compiles a single
  closure body to an SVM module whose entry is `f(syntax-args…) → syntax-result`
  (a new entry shape, or a synthesized wrapper program that the scheduler entry calls).

- **Phase 4 — Marshalling + hygiene ABI.** Reimplement `syntax_from_ast`/`syntax_to_ast`
  against the SVM heap (offset, not pointer); pass `macro_scope_mark` + a gensym
  counter into the module and read them from the SVM-side builtins; keep SVM-heap roots
  alive across the call.

- **Phase 5 — Staged pipeline.** Behind `JACL_STAGE_ON_SVM`, replace
  `jacl_ctx_run_closure` in `expand__node` with: compile the body (Phase 3) → run on an
  SVM runtime instance (Phase 1/2) → marshal args/result (Phase 4). Green when the whole
  Phase 0 corpus emits IR identical to the oracle.

- **Phase 6 — Drop the legacy VM.** Remove the macro-path dependencies
  (`jacl_ctx_run_closure`, `vm__run`, `OP_SYNTAX_OP`, `bytecode.c`, the bytecode half of
  `compiler.c`) and cut the emit driver's two other legacy-VM users — the static-error
  oracle (`compiler_compile`) and the `--interp` host round-trip — so `jacl_compiler.svmb`
  no longer links `vm.c`. Verify the module shrinks and the corpus still matches.

## Hardest parts (watch list)

1. No syntax model in `jaclrt` — Phase 1 is a real runtime addition (or a redesign).
2. Re-implementing ~450 lines of syntax ops as SVM builtins (Phase 2).
3. Two heaps: pointer-vs-offset marshalling across a heap boundary (Phase 4).
4. Hygiene + gensym plumbing as an ABI, read inside the running macro (Phase 4).
5. `svm_codegen_program` is whole-program/fixed-entry — needs a macro-body entry (Phase 3).
6. Bootstrapping order: a macro body may reference other procs/macros; staging is a
   mini pipeline that compiles+runs each body before the main program codegen (Phase 5).

## Phase 3 final link — blocker found, design corrected

The first attempt made the staged-macro entry a **C wrapper** (`stage_entry.c`) that
imports the codegen'd `__jacl_macro` and calls `jaclrt`/`syn_rt`. That does **not** link:
`svm-llvm` only emits SVM imports for a **known capability / `__vm_*` family**
(`read`/`write`/`exit`, memory, io, jit, region — `collect_cap_imports` &c.); a call to any
*other* undefined external (`jacl_vec_empty`, `__jacl_macro`, `synrt_*`) is rejected —
`svm-llvm/src/lib.rs:477`: "a call to one needs import support (a later slice)". So
**C-via-svm-llvm code cannot import another unit**; only **codegen** output emits arbitrary
`call.sym` imports (that's how every JACL program already calls `jaclrt`).

**Corrected design — the entry is codegen'd, the glue is in the runtime:**

- Extend the runtime (`jaclrt` translate) with `syn_rt` + two self-contained helpers:
  `synrt_read_arg()` (read stdin → `synrt_decode` → syntax vec) and
  `synrt_write_result(vec)` (`synrt_encode` → write stdout). These live in a C unit
  translated *with* `jaclrt`, so their `jacl_vec_*` calls are in-unit and `read`/`write`
  are the recognized `Stream` caps — no cross-unit import.
- A new codegen entry `svm_codegen_staged_macro(names, lens, n, body)` emits a whole
  module: func 0 = `{ init; x = call.sym synrt_read_arg(); res = __jacl_macro(x);
  call.sym synrt_write_result(res) }`, func 1 = `__jacl_macro(sp, x)` (the body). Both are
  codegen'd, so the `synrt_*` + `jacl_*` calls are `call.sym` imports and `__jacl_macro` is
  a direct in-module call — no funcref, no cross-unit C import.
- The Rust driver then reduces to `jacl_svm`'s exact path: link the codegen'd module
  against the (staging-extended) runtime, `synth_manifest_start`, run with the arg wire on
  stdin.

The `emit_macro_body` tool (codegen a body → `__jacl_macro` svm-text) is reusable as-is;
the rest of the first attempt (`stage_entry.c` wrapper + `stage_macro.rs` importing across
units) is superseded by the above and not kept.

## Phase 3 final link — done; the arity-1 corpus runs end-to-end on SVM

The corrected design is built and **the whole pipeline compiles, links, runs on the SVM
engine, and reproduces the legacy VM's expansions**:

- **Runtime glue** — `codegen/selfhost/macro_staging/{stage_glue.c,jaclrt_staging.c}`:
  `syn_rt` + `synrt_read_arg()` / `synrt_write_result(vec)`, translated *with* jaclrt
  (`translate_runtime_staging()` in the harness). Its `jacl_vec_*` are in-unit; only
  `read`/`write` cross the boundary.
- **Codegen entry** — `svm_codegen_staged_macro` (`codegen/codegen.c`): emits func 0 =
  `{init; x=synrt_read_arg(); res=<body(x)>; synrt_write_result(res)}` + func 1 = the body.
  `emit_macro_body --staged` drives it.
- **Driver** — `runtime/harness/src/bin/stage_macro.rs`: link vs the staging runtime,
  `synth_manifest_start`, run with the arg wire on stdin (`call_with_stdin`), emit the
  result wire on stdout. Reduces to `jacl_svm`'s link path.

**Verified on SVM** (`run_stage_test.sh` — the committed e2e gate): `k → 42`,
`s → [+ 1 2]`, `t → 21`, `twice 21 → [+ 21 21]`, `dq 7 → [+ 7 1]`, `idd 21 → 21`, and a
compound argument `twice [* 3 4] → [+ [* 3 4] [* 3 4]]`. Each expansion matches the legacy
oracle (`run_diff.sh`), so the migration is observationally transparent for this corpus.

**The bug that was blocking it** (previously mis-diagnosed as a `malloc`/`realloc` fault) was
two real defects, both now fixed:
1. **Missing SelfData relocations.** String/data literals lower to data-segment references
   whose `i64.const <offset>` must be relocated to the segment's linked address. `emit_macro_body`
   dropped the reloc table and `stage_macro.rs` never applied it, so every string resolved to
   empty (`syntax-quote [+ 1 2]` → `["" 1 2]`). Fix: emit the relocs after a `%%RELOCS%%`
   sentinel (as the frontend's `--file` path does) and pass them in the program `LinkUnit`
   (`relocations:`), exactly like `jacl_svm`'s `split_relocs`.
2. **Unquote of a bare word didn't resolve the parameter.** `~x` parses as
   `AST_UNQUOTE(AST_LIT_STRING "x")` (bare words in command position are strings), so
   `compile_synquote` compiled the *string* "x" instead of a reference to the macro parameter
   `x` — the arg came out as the inline string "x" (vec-count 0), and `synrt_encode` rejected
   it. Fix: when an unquote child is a bare `AST_LIT_STRING`, synthesize an `AST_VAR_REF` and
   compile that — mirroring the legacy VM's `syntax__compile_unquote_child` (`src/compiler.c`).

The alignment fix noted earlier (guest bump-allocator `g_arena` now `aligned(16)`, so its
`*(size_t*)base` size-header write doesn't fault) stands and is still required.

## Multi-arity macros — done

The staged path now handles macros of any arity, so the whole Phase 0 corpus (including
`unless {cond body}`) stages on SVM. The design keeps the wire flat: the argument wire is a
single version byte followed by the N parameter syntax values **back-to-back** (arity-1 is
exactly the original format). `synrt_read_arg` is stateful — the first call slurps stdin and
skips the version byte, and each call decodes the next value (`synrt_decode_node`); the
codegen entry (`svm_codegen_staged_macro`) calls it once per parameter, in order, and binds
each. No args frame or per-arg func parameters — the SVM func signature is unchanged; the
parameters arrive over the wire. `run_stage_test.sh` covers the arity-2 case
`unless [== 1 2] { set hit 5 } → [if [== 1 2] {} { set hit 5 }]`.

## Phase 5 — the compiler stages macros on SVM (behind `JACL_STAGE_ON_SVM`) — done

The frontend's macro expander now runs macro bodies on the **SVM engine** instead of the
legacy bytecode VM, and the whole Phase 0 corpus (`twice`, `nested` — a macro calling a
macro, `unless` — arity-2 with blocks) emits IR **byte-identical to the oracle**. That is the
Phase 5 acceptance criterion: nothing observable changes except which engine runs the macro.

The seam is a hook, so `src/` gains no codegen/SVM dependency:

- **`src/syntax.c`** declares `jacl_macro_stage_hook` (NULL by default). In `expand__node`,
  when the hook is installed *and* `JACL_STAGE_ON_SVM` is set, a macro call is expanded through
  it — producing the expanded AST directly — instead of `jacl_ctx_run_closure`. The splice +
  re-expand tail is shared, so iterative expansion (a macro whose output calls another macro)
  works unchanged. Hook unset or env unset ⇒ the legacy path, bit-for-bit.
- **`runtime/harness`** now also builds a `staticlib` exposing a C ABI, `jacl_svm_stage`
  (`src/stage_ffi.rs`): link a codegen'd staged-macro module against the staging runtime
  (translated **once**, cached), run it, return the result wire. This is the in-process
  embedding of "run on an SVM runtime instance."
- **`codegen/selfhost/macro_staging/stage_bridge.c`** is the hook body (codegen the body →
  `synw_encode` the argument ASTs → `jacl_svm_stage` → `synw_decode`), installed by the driver
  (`emit_jacl`, built with `-DJACL_STAGE_ON_SVM_BUILD`).
- **`run_diff.sh --svm`** is the committed gate: it builds that driver and diffs the
  `JACL_STAGE_ON_SVM=1` output against the same `golden/` the legacy path matches.

**Still out of scope** (Phase 4 correctness-completeness, pulled in as a future corpus needs
it): `~@` unquote-splicing and hygiene / scope-mark + gensym propagation across the boundary.
And Phase 6: drop the legacy VM from the macro path once staging is the only evaluator (the
`JACL_STAGE_ON_SVM` gate flips to always-on, then `jacl_ctx_run_closure` / `vm.c` come out).

## Sequencing note

Chosen over "ship the self-hosted compiler first (legacy VM bundled), purify later."
That means the browser wiring (Slice 4) waits until this lands, so the compiler that
ships is already single-backend. This is a multi-phase runtime port, not a quick slice.
