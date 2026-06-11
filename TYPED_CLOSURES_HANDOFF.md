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
(`[Proc [i32] Point]`), **dyn-inferred struct params** (an untyped `{q}` literal
param adopts the annotation's `Point`), and — newest — **direct-calls of bare
inline struct-param/return literals** (`[[proc {Point q} i32 …] $p]`,
`[[proc {i32 a} Point …] 3]->x`). All green across single-file + interpret +
module compiles. With the direct-call slice in, the struct-in-`[Proc …]` story
has no remaining gaps.

### Direct-call of struct-param/return literal slice (just landed)

`[[proc {Point q} i32 { $q->x }] $p]` and `[[proc {i32 a} Point …] 3]->x` now
ride the typed (inline) convention — the struct param passes inline and the
struct return materializes inline. `handle_proc` doesn't intern a shape for a
proc literal, so the typer now (in `typer__infer_command_inner`, at the head
inference) interns a DIRECT-call proc-literal head's signature
(`typer__intern_proc_literal_shape`) and stamps it: the typer-side idx on
`head->inferred_struct_idx` (drives `head_is_closure_call` → arg narrowing +
result typing; a struct return sets the call node's `inferred_struct_idx` so
`[…]->field` resolves) and the portable idx on `head->inferred_proc_shape_idx`
(the compiler's direct-call path `_ex`-decodes it for the struct param/return
idxs). `typer__portable_proc_shape` now allows struct idxs through (declines only
nested-compound shape idxs, `≥ struct_count`). The compiler direct-call branch
(`compiler.c` ~17117) switched from `compiler__decode_proc_shape` to `_ex` and
sets `call_param_struct_idxs` + `call_return_struct_idx`. This also enables a
struct-param/return closure read out of a `[Vec [Proc …]]` and called directly
(`[[vec-get $fns 0] $p]`). Tests: the direct-call cases added to
`typed_closure_struct_param.jacl` / `typed_closure_struct_return.jacl`.

### Dyn-inferred struct param slice (prior)

`def [Proc [Point] i32] g [proc {q} i32 { $q->x }]` — a closure literal can leave
a param untyped and the typer infers its struct type from the `[Proc …]`
annotation (`q : Point`), so `$q->field` resolves without restating the type.
Works on the def-binding form, the inline closure-arg form, AND a
`[Vec/Arr [Proc …]]` element literal.
`typer__monomorphize_proc_literal` gained a `pstruct_idxs` arg (its 4 call sites
`_ex`-decode the shape), `typer__handle_def_or_mut` does the monomorphizing walk
INSTEAD of the generic walk for a `[Proc …]` proc-literal RHS, and the typed
vec/arr-ctor arg-walk now SKIPS the generic walk for a closure-literal element
(the ctor branch monomorphizes it) — all three used to raise a premature "body
returns dyn" before the param was bound to the struct. Test:
`typed_closure_struct_param_dyn.jacl`.

(Unrelated pre-existing gap surfaced while testing: a `[vec-get …]` result bound
to a `def` and then passed to a `[Proc …]` closure PARAM types `dyn` — true for
scalar element vecs too. Binding + a *direct* call on the element works; passing
the bound value to a closure param is the gap. This is the same class as the
"vec-get result narrowing" debt, not specific to struct params.)

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

## Next slice (recommended): global proc / closure as a first-class value

The struct-in-`[Proc …]` story is now complete (params, returns, dyn-inferred
params, direct-calls — all in). The remaining items are narrowing gaps where the
runtime value IS a closure but the typer reads `dyn`. The highest-value one:

A **global proc name** (`$dbl`) or a **global `[Proc …]` binding** types `dyn`,
so `def [Proc …] g $dbl` errors ("cannot assign dyn to closure binding") and the
value can't be passed to a closure param. Local typed-closure bindings, inline
literals, and closure-valued expressions all narrow; only the *global-name-as-
value* read stays dyn. The runtime value is a real closure (`print $add` shows
its signature). Fix: where the typer resolves a global var-ref / global proc
ref, narrow a global proc (or a global `[Proc …]` binding) to `TYPE_CLOSURE` +
its proc-shape idx, so the def/assign and the closure-param sink accept it. Watch
the cross-registry rule (global proc shapes must intern portably for any AST
stamp the compiler reads).

A related, lower-value gap (same class): the **vec-get-result-to-closure-param**
narrowing — a `[vec-get $fns 0]` bound via `def g …` then passed to a `[Proc …]`
param types `dyn` (scalar element vecs too). A direct call on the bound element
works; passing the bound value to a param doesn't.

Risk: medium — touches global-ref typing, used everywhere. Re-run the full
`typed_closure_*` family.

## Smaller remaining items (lower value)

- **Dyn-inferred struct param** — `def [Proc [Point] i32] h [proc {q} i32 { $q->x }]`
  (literal param `q` is dyn, annotation says Point). **DONE**: the typer infers
  `q : Point` on the def-binding form, the inline closure-arg form, AND a
  `[Vec/Arr [Proc …]]` element literal.
  `typer__monomorphize_proc_literal` gained a `pstruct_idxs` arg and threads the
  shape's param struct idxs onto an overridden dyn param; its 4 call sites decode
  via `typer__decode_proc_shape_ex`. The def-site (`typer__handle_def_or_mut`)
  and the typed vec/arr-ctor arg-walk (`typer__infer_command_inner`) both run the
  monomorphizing walk INSTEAD of (resp. skip) the generic walk for a `[Proc …]`
  proc-literal (the generic walk raised a premature "body returns dyn" before `q`
  was bound to the struct). Test: `typed_closure_struct_param_dyn.jacl`.
- **vec-get result → closure param narrowing** (pre-existing, scalar too) — a
  `[vec-get $fns 0]` element of a `[Vec [Proc …]]`, bound via `def g …` and then
  passed to a `[Proc …]` closure PARAM, types `dyn` ("expected closure, got
  dyn"). A *direct* call on the bound element works (the element narrows for a
  call but not as a passed value). Same class as the global-ref narrowing gap.
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
