# Typed Closures — Handoff (fresh-session pickup)

Last updated: 2026-06-11. Branch: `main` (push directly here). Build/verify:
`./build.sh` (expect **91/0**), `./build.sh --tsan` (expect **89/2** — the 2 are
the known-safe `chase_lev_stress` / `jacl_harness` race-detector reports),
`--wasm` skipped if no `emcc`.

## START HERE — reading order

1. **This file** — current state + the next slice's recipe.
2. **`TYPED_CLOSURES_DESIGN.md`** § "Remaining work (authoritative …)" — the
   authoritative remaining list (the older per-phase "scope / debt" blocks below
   it are partly STALE; trust the Remaining section).
3. **`docs/TYPER_SHAPE_UNIFICATION_AUDIT.md`** — the typer/compiler shape-registry
   arc (kind-based discrimination, struct-registration-before-typer (2a), the
   portable proc-shape stamp (2b), the cleanup audit, the tail closeout). Useful
   background for how proc shapes flow typer→AST→compiler→runtime.

## Where things stand

The typed-closure arc is functionally complete: closure values, params, returns
(of closures), collections (`[Vec/Arr [Proc …]]`), closure-valued *expression*
results (direct calls, expression args, nested push), runtime proc-signature
introspection (`print` shows `<proc name(types) -> ret>`), **struct PARAMS inside
`[Proc …]`** (`[Proc [Point] i32]`), **struct RETURNS inside `[Proc …]`**
(`[Proc [i32] Point]`), and — newest — **dyn-inferred struct params** (an
untyped `{q}` literal param adopts the annotation's `Point`). All green across
single-file + interpret + module compiles.

### Dyn-inferred struct param slice (just landed)

`def [Proc [Point] i32] g [proc {q} i32 { $q->x }]` — a closure literal can leave
a param untyped and the typer infers its struct type from the `[Proc …]`
annotation (`q : Point`), so `$q->field` resolves without restating the type.
Works on the def-binding and inline closure-arg forms.
`typer__monomorphize_proc_literal` gained a `pstruct_idxs` arg (its 4 call sites
`_ex`-decode the shape), and `typer__handle_def_or_mut` does the monomorphizing
walk INSTEAD of the generic walk for a `[Proc …]` proc-literal RHS (the generic
walk used to raise a premature "body returns dyn"). Test:
`typed_closure_struct_param_dyn.jacl`. One sub-item remains (a `[Vec [Proc …]]`
element literal that both omits its struct param type and declares a return —
see the smaller-items list).

### Struct RETURNS slice (prior)

`[Proc [i32] Point]` works: a struct-returning closure call materializes the
struct inline at the call site (`[g 3]->x`, `def Point p [g 3]`), a proc whose
body returns the call result narrows to `TYPE_STRUCT` (`proc apply {[Proc [i32]
Point] f, i32 n} Point { [f $n] }`), and the literal's return — declared
(`{i32 a} Point`) or omitted (adopts the annotation's) — is **conformance-checked
by struct idx** against the `[Proc …]` return. A mismatched struct is a clean
error at BOTH the def-site and the call-site arg (not silently re-typed: a
different struct's width would type-confuse the inline-materialized result — the
"compare idxs, not just kinds" invariant). The conformance check lives in the
proc-compilation path at `compiler.c` ~10932 (right where a dyn-return literal
adopts the annotation's return). Tests: `typed_closure_struct_return.jacl` +
`typed_closure_struct_return_error.jacl` (mismatched-struct error).

Encoding contract (key mental model): a `TYPE_SHAPE_PROC` shape's param/return
idx (in `proc_param_pool` / `return_idx`) is a **portable** encoding — a scalar
sentinel (`JACL_SCALAR_TYPE_IDX(t)`) for a scalar, or a **struct idx** for a
struct (struct idxs are 1:1-aligned across the typer/compiler registries).
Nested compound (vec/map/nested-proc) idxs are registry-bound → NOT portable →
stay unsupported.

## Next slice (recommended): direct-call of a struct-param/return inline literal

After the struct-param + struct-return slices, the ONE remaining struct gap is
symmetric across both: a **direct-call of a bare inline literal** with a struct
param or return doesn't ride the typed path.
- `[[proc {Point q} i32 { $q->x }] $p]` → "cannot pass struct value to dyn
  parameter".
- `[[proc {i32 a} Point { … }] 3]->x` → "field access requires struct or map".

Both are clean errors (no miscompile); named bindings and proc-param
pass-through work. The cause is `typer__portable_proc_shape` (`src/typer.c`
~2283): it declines any non-scalar param/return idx (`return UINT32_MAX`), so
the 2b portable proc-shape stamp never lands on the direct-call head node, and
the compiler's direct-call path (`compiler.c` ~17092) has nothing to decode.

What's left:
1. **`typer__portable_proc_shape`:** allow **struct** idxs through (they're
   portable — a struct idx is `< tc->struct_count`; keep declining nested-shape
   idxs). `type_shape_intern_proc` already stores them raw. For a struct
   *return*, also set the call node's `inferred_struct_idx` so `[…]->field`
   resolves on the result.
2. **Compiler direct-call path** (`compiler.c` ~17092, the `head->type ==
   AST_COMMAND && head->inferred_proc_shape_idx != UINT32_MAX` branch): it
   currently uses `compiler__decode_proc_shape` (scalars only). Switch to
   `compiler__decode_proc_shape_ex` and set `call_param_struct_idxs` +
   `call_return_struct_idx` so a struct param passes inline and a struct return
   materializes inline (the ~17386 `call_return_type == TYPE_STRUCT` block fires
   unchanged).
3. **Tests:** extend `typed_closure_struct_param.jacl` /
   `typed_closure_struct_return.jacl` with the direct-call forms (currently
   noted as the documented gap in each file's header comment).

Risk: medium — touches the shared portable-stamp path used by ALL closure
expressions (collections, expression args). Guard struct idxs precisely
(decline nested-shape idxs) and re-run the full `typed_closure_*` family +
`closure_collection_*` to confirm no scalar-path regression. This also enables a
struct-param/return closure read out of a `[Vec [Proc …]]` and called directly.

(A lowest-effort alternative is the leftover **dyn-inferred struct param**
sub-item — the `[Vec [Proc …]]` element-literal case in the collection-ctor walk
— described in the smaller-items list below.)

## Smaller remaining items (lower value)

- **Dyn-inferred struct param** — `def [Proc [Point] i32] h [proc {q} i32 { $q->x }]`
  (literal param `q` is dyn, annotation says Point). **DONE** (this slice): the
  typer now infers `q : Point` on both the def-binding and inline-arg forms.
  `typer__monomorphize_proc_literal` gained a `pstruct_idxs` arg and threads the
  shape's param struct idxs onto an overridden dyn param; its 4 call sites decode
  via `typer__decode_proc_shape_ex`. The def-site (`typer__handle_def_or_mut`)
  now runs the monomorphizing walk INSTEAD of the generic walk for a `[Proc …]`
  proc-literal RHS (the generic walk raised a premature "body returns dyn" before
  `q` was bound to the struct). Test: `typed_closure_struct_param_dyn.jacl`.
  **One sub-item remains:** a `[Vec [Proc …]]` element literal that BOTH omits
  its struct param type AND declares a return still errors — the collection-ctor
  handler walks the element generically before its monomorphize loop (same
  premature-walk shape as the old def path, in `typer__infer_command_inner`'s
  typed-vec ctor branch ~4734). Declare the struct param on such an element.
- **Global proc / closure as a first-class value** — `$procname` and a global
  `[Proc …]` binding type as `dyn`, so `def [Proc …] g $dbl` errors ("cannot
  assign dyn to closure binding"). The runtime value IS a closure (`print $add`
  shows its signature); the typer just doesn't narrow a global ref. Local
  bindings / inline literals / closure-valued expressions all work.
- **Non-literal closure-returning tail** — a proc returning a closure
  param/value *verbatim* isn't conformance-checked against its declared
  `[Proc …]` return.
- **Nested compound inside `[Proc …]`** — unsupported by design (non-portable
  idxs). Clear error.

## Invariants / gotchas (learned this arc)

- **Dual-defined structs.** `JaclVM_s`, `Compiler`, `AstNode`, `JaclClosure`,
  `Local` are defined in BOTH a `src/*.c` (or `bytecode.c`) and the `src/jacl.h`
  mirror — adding a field needs BOTH. The `struct_sizes` test guards the layout
  (it caught this 3× this session). If `struct_sizes` fails with a sizeof
  mismatch, you forgot the jacl.h mirror.
- **Cross-registry rule.** Typer-side shape idxs are NOT interchangeable with the
  compiler's; don't stamp them on AST nodes. Struct idxs and scalar sentinels
  ARE portable (aligned / fixed encoding). The proc-shape stamp
  (`AstNode.inferred_proc_shape_idx`) is a SHARED-registry (= compiler) idx,
  interned via `typer__portable_proc_shape`.
- **Struct conformance must compare idxs, not just kinds.** `TYPE_STRUCT ==
  TYPE_STRUCT` is not enough — different structs have different widths; an inline
  call would be type-confused. `compiler__local_closure_conforms` + the
  inline-literal param-vs-annotation check both compare struct idxs now.
- **all-or-nothing flips.** Accepting a new type in `proc_sig_elem` WITHOUT the
  full plumbing produces a closure that compiles but mis-calls. Keep the parser
  flip last; gate dormant plumbing behind it.

## Key files / functions

- `src/typer.c`: `typer__proc_sig_elem`, `typer__proc_type`,
  `typer__intern_proc_shape`, `typer__decode_proc_shape{,_ex}`,
  `typer__monomorphize_proc_literal`, `typer__portable_proc_shape`.
- `src/compiler.c`: `compiler__proc_sig_elem`, `compiler__proc_annotation`,
  `compiler__intern_proc_shape`, `compiler__decode_proc_shape{,_ex}`,
  `compiler__stamp_closure_{local,param_local}`,
  `compiler__activate_annot_proc_from_shape`, `compiler__local_closure_conforms`;
  the proc-compilation param-vs-annotation loop (~11401) and the typed-call site
  (~16951 / ~17062, struct-return materialization ~17386).
- `Local.param_struct_idxs` (src/compiler.c + src/jacl.h mirror).
- Tests: `test/jacl/typed_closure_struct_param{,_dyn,_error}.jacl`,
  `typed_closure_struct_return{,_error}.jacl`, and the broader
  `typed_closure_*.jacl` family.
