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
introspection (`print` shows `<proc name(types) -> ret>`), and — newest —
**struct PARAMS inside `[Proc …]`** (`[Proc [Point] i32]`). All green across
single-file + interpret + module compiles.

Encoding contract (key mental model): a `TYPE_SHAPE_PROC` shape's param/return
idx (in `proc_param_pool` / `return_idx`) is a **portable** encoding — a scalar
sentinel (`JACL_SCALAR_TYPE_IDX(t)`) for a scalar, or a **struct idx** for a
struct (struct idxs are 1:1-aligned across the typer/compiler registries).
Nested compound (vec/map/nested-proc) idxs are registry-bound → NOT portable →
stay unsupported.

## Next slice (recommended): struct RETURNS in `[Proc …]`

`[Proc [i32] Point]` — a closure returning a struct. **Most of the pipeline
already handles it** (the struct-param slice plumbed both params and returns
through encode/decode/stamp; only the parser was gated to params). Concretely:
- `intern_proc_shape` (typer + compiler) already encodes a struct **return** idx.
- the `*_ex` decoders already surface the return struct idx (`out_rstruct_idx`).
- `compiler__stamp_closure_param_local` / `compiler__stamp_closure_local` already
  set `Local.return_struct_idx` from the shape.
- the typed-call return path **already** materializes a struct return inline —
  `compiler.c` ~17386: `if (call_return_type == TYPE_STRUCT && call_return_struct_idx != UINT32_MAX)`
  sets `INLINE_STACK` + `last_expr_type = TYPE_STRUCT`. A closure call whose
  `Local.return_type == TYPE_STRUCT` flows through this unchanged.

What's actually left:
1. **Remove the two rejections** (grep `struct RETURN in [Proc …] is a later
   slice`): `typer__proc_type` (`src/typer.c`) and `compiler__proc_annotation`
   (`src/compiler.c`). That alone accepts the annotation and (via the above)
   should make `def [Proc [i32] Point] g …; [g 3]` return an inline Point.
2. **Typer call-result typing:** make a closure call whose binding returns a
   struct type the call node `TYPE_STRUCT` + the return struct idx (so
   `[[g 3] ->x]` / a proc returning `[g 3]` type-checks). Look at where the
   TyperBinding's `proc_return_type` / `proc_return_struct_idx` drives the
   body-call narrowing (the bound_proxy path — grep `proc_return_struct_idx` /
   `bound_proxy` in `src/typer.c`). Scalar returns already narrow; extend to
   `TYPE_STRUCT` (set `node->inferred_struct_idx`).
3. **`typer__portable_proc_shape`** (`src/typer.c`) currently declines a
   non-scalar param/return (returns UINT32_MAX), so the 2b stamp doesn't fire for
   struct-param/return closures (they fall back to re-derivation — works, but no
   stamp). Optional: allow struct idxs through (they're portable — a struct idx
   is `< tc->struct_count`; decline only nested-shape idxs) so direct-call /
   expression-arg of a struct-param/return closure also rides the fast path.
4. **Tests:** add a positive `typed_closure_struct_return.jacl` (`[Proc [i32]
   Point]`, call + field access) and flip `typed_closure_struct_return_error.jacl`
   from expect-error to the working case (or repurpose it for a non-struct
   mismatch). Mirror the struct-param tests.

Risk: low-ish — the inline-struct-return machinery is reused. Watch the typer
call-result struct typing (item 2) and a struct return through a *closure*
(B3b closures-returning-closures interaction).

## Smaller remaining items (lower value)

- **Dyn-inferred struct param** — `def [Proc [Point] i32] h [proc {q} i32 { $q->x }]`
  (literal param `q` is dyn, annotation says Point). The **compiler** infers it
  (`src/compiler.c`, the param-vs-annotation loop ~11401 sets `param_struct_idxs`
  from `annot_proc_param_struct_idxs`), but the **typer** doesn't:
  `typer__monomorphize_proc_literal` (`src/typer.c`) binds an overridden dyn
  param with `struct_idx = UINT32_MAX`, so `$q->x` types dyn → "body returns dyn"
  error. Fix: thread the shape's param struct idxs into
  `typer__monomorphize_proc_literal` (use `typer__decode_proc_shape_ex` at its 3
  call sites) and use them when overriding a dyn param. Currently a clean error
  (declare the struct type on the literal).
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
- Tests: `test/jacl/typed_closure_struct_param{,_error}.jacl`,
  `typed_closure_struct_return_error.jacl`, and the broader
  `typed_closure_*.jacl` family.
