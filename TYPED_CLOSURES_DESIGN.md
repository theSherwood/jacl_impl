# Design — typed / monomorphized closures

Status: the typed-closure arc is **functionally complete** (2026-06-11) —
`[Proc …]` type syntax, closure VALUES/PARAMS/RETURNS, user HOFs,
closures-returning-closures, named-closure values, collections
(`[Vec/Arr [Proc …]]`) with get/push narrowing, struct params/returns, global
procs as first-class values, and forward-ref-as-value (higher-order mutual
recursion) — all via a registry `TYPE_SHAPE_PROC` + portable proc-shape stamp,
no VM change. See **"Remaining work"** below for the few open items; **§2/§3**
are the settled core rules and type syntax (the enduring spec). The historical
implementation plan and per-phase blocks have been trimmed (git history).

## Remaining work (authoritative, as of 2026-06-12)

> **NEXT SESSION — start here.** This section is the live handoff. The
> typed-closure arc + the registry unification (2b) are done; compound types in
> `[Proc …]` mostly work (item 4). The open items, in rough priority:
> 1. **Compound RETURNS beyond `[Vec T]`** (`[Map K V]`/`[Arr T]`) — item 4.
>    NOTE this is the broader "proc RETURN-type collection annotations" gap:
>    `typer__resolve_return_type` doesn't resolve `[Arr T]` returns at all and
>    DROPS a `[Map K V]` return's KEY (no key output channel), so even a plain
>    `proc f {} [Map i64 i64] {…}` mis-types `map-get` on its result. Fixing
>    closure-return narrowing for maps/arrs rides on closing that first.
> 2. **Non-literal closure-returning tail** — item 3 (mostly done; re-check).
> 3. **Unification cleanup 2c/2d** — `docs/TYPER_SHAPE_UNIFICATION_AUDIT.md`
>    (low value, entangled with the `syntax.c` NULL-registry paths).
> **DONE 2026-06-12 — Map KEY conformance + the compiler-side representation
> migration for PARAMS** (was items 1 + "compiler-side representation
> migration"): the compiler now carries the FULL compound shape idx as the
> per-param conformance encoding on BOTH sides — `compiler__decode_proc_shape_ex`
> emits `pidx` (not the element idx) for a declared `[Proc …]` compound param, and
> the stored signature side (`GlobalArity`/`Local`, via the new
> `compiler__param_conf_idx` re-intern from a param's value+key) matches it. This
> closed the map-key soundness edge (`[Map str i64]` no longer conforms to a
> `[Map i64 i64]` sink) AND fixed a latent inconsistency bug where the
> def-binding path (`def [Proc [[Vec/Map …]] …] f $proc`) used a different
> encoding than the arg-passing path and wrongly REJECTED a valid bind. Tests:
> `typed_closure_compound_def`, `typed_closure_compound_map_key_err`,
> `typed_closure_compound_map_key_def_err`.
> Full per-item detail (what works, what's open, file/function pointers) is
> below in items 1–4.

The arc (B1–B3d + the B3c follow-ups + the 2b portable-stamp slices) is
functionally complete: closure values, params, returns, collections, and
closure-valued *expression* results (direct calls of get/call results,
expression args, nested push), across single-file + interpret + module compiles,
plus runtime proc-signature introspection (`print` shows `<proc name(types) ->
ret>`). Verified-remaining gaps:

1. **Struct types inside `[Proc …]`** — struct PARAMS *and* RETURNS are now
   supported (the struct-param + struct-return slices): `[Proc [Point] i32]` and
   `[Proc [i32] Point]` both work — a closure with a struct param/return is
   passed/returned inline + monomorphized (inline literal) / conformance-checked
   (named, by struct idx). A struct-returning closure call materializes the
   struct inline at the call site (`[g 3]->x`); a proc returning the call result
   narrows to `TYPE_STRUCT`; the literal's declared return (or omitted, adopting
   the annotation's) is conformance-checked against the `[Proc …]` return by
   struct idx (a mismatched struct is a clean error, def-site *and* call-site
   arg). Tests: `typed_closure_struct_param.jacl`,
   `typed_closure_struct_return.jacl` (+ each `_error` for a wrong-struct
   mismatch). The struct-in-`[Proc …]` capabilities below are all in; the only
   remaining limit is nested compounds (unsupported by design):
   - **Dyn-inferred struct param** (`[proc {q} …]` where the annotation says
     `Point`): now narrows on BOTH sides — `typer__monomorphize_proc_literal`
     threads the shape's param struct idxs onto an overridden dyn literal param,
     the def-site does the monomorphizing walk INSTEAD of a generic walk, and the
     typed vec/arr-ctor arg-walk SKIPS the generic walk for a closure-literal
     element (the ctor branch monomorphizes it). All three used to raise a
     premature "body returns dyn" before the param was bound to the struct. Test:
     `typed_closure_struct_param_dyn.jacl` (def-binding + inline-arg +
     `[Vec [Proc …]]` element forms).
   - **Direct-call of a bare inline struct-param/return literal**
     (`[[proc {Point q} i32 {…}] $p]`, `[[proc {i32 a} Point {…}] 3]->x`): now
     rides the typed (inline) convention. The typer interns a DIRECT-call
     proc-literal head's signature (`typer__intern_proc_literal_shape`) and
     stamps it (typer-side idx → `head_is_closure_call` arg narrowing + struct
     result typing; portable idx → the compiler's direct-call path
     `_ex`-decodes the param/return struct idxs). `typer__portable_proc_shape`
     now passes struct idxs through (declines only nested-compound shape idxs).
     Also enables a struct closure read out of a `[Vec [Proc …]]` and called
     directly. Tests in `typed_closure_struct_param.jacl` /
     `typed_closure_struct_return.jacl`.
   - **Nested compound** inside `[Proc …]` (`[Proc [[Vec i64]] …]`, nested
     `[Proc …]`): registry-bound idxs aren't portable — stays unsupported.
2. **A global proc used as a first-class value** — DONE: `$procname` now narrows
   to a typed closure value (`typer__infer_var_ref` looks it up in the proc
   registry; `typer__intern_global_proc_shape` interns its portable shape), so
   `def [Proc …] g $dbl`, `[apply $dbl 5]`, struct-signature procs, and
   `[Vec [Proc …]]` elements all work, and wrong-signature procs are rejected.
   The compiler conformance sinks gained `compiler__global_closure_conforms` (a
   transient-`Local` view over the proc's `GlobalArity`). Test:
   `typed_closure_global_proc.jacl`. **Forward refs supported:** a proc used as a
   value before its textual definition conforms via the typer's portable shape
   stamp (`compiler__stamp_closure_conforms`) — the compiler has no compiled
   signature yet, but the typer's pre-pass stamped one. This makes higher-order
   mutual recursion expressible. The fallback is gated to immutable procs only:
   the typer stamps `$name` solely for registered procs, so a forward ref to a
   `mut`/`def` binding or an undefined name stays `dyn` and is a hard compile
   error (`typed_closure_global_proc_fwd_{mut,undef}_err.jacl`); a wrong-signature
   proc is still rejected (`…_wrong_sig_err.jacl`). (Procs with a non-portable
   compound param/return, an inferred return, or a generator stay dyn-valued —
   SYMMETRICALLY, in either definition order, so no forward-specific sharp edge;
   and a generator can't present a portable scalar signature to the pre-pass —
   `proc g {} i32 { yield 1 }` is rejected at its own definition — so the stamp
   can never conform-but-mismatch a runtime generator value. No unsoundness.)
   **Untyped binding / get narrowing — DONE.** A closure value with a
   statically-known signature, bound via an untyped `def` *or* `mut` — a named
   proc (`$dbl`), a call/get result (`[make 2]`, `[vec-get $fns 0]`), or an
   annotated inline literal — now inherits the signature instead of collapsing
   to `dyn`, so the binding is passable to a `[Proc …]` sink, not just callable.
   Per-slot rule: a partly-typed literal (`[proc {i32 x, y} i32 {…}]`) keeps a
   static `[Proc [i32 dyn] i32]` (unannotated slots are `dyn`); a FULLY-
   unannotated literal (`[proc {x} {…}]`) carries no annotation and stays `dyn`
   (the bridge that keeps untyped proc bindings). `mut` additionally enforces
   signature conformance on REASSIGNMENT (`set f $g`): a wrong-signature closure
   is rejected, a conforming one (or a monomorphizing inline literal) is allowed.
   Tests: `typed_closure_untyped_def.jacl`, `typed_closure_mut.jacl`
   (+ `_unannotated_err`, `_mut_reassign_err`). The rule: a proc/closure's type
   comes from its annotations (per-slot) or a propagated annotation; no known
   signature → `dyn` — applied uniformly to `def` and `mut`.
3. **Non-literal closure-returning tail — DONE.** A proc declaring a `[Proc …]`
   return that returns a closure VALUE verbatim — a param (`$f`), a named proc
   (`$dbl`), or a closure binding (`$g`) — is now conformance-checked against the
   declared return signature (`typer__proc_shapes_conform`: arity + per-slot
   param/return type & struct-idx). Previously only `TYPE_CLOSURE == TYPE_CLOSURE`
   was checked, so a wrong-signature verbatim return was silently accepted (a
   latent unsoundness). An inline-literal tail is monomorphized to the declared
   shape (so it conforms); lenient when the tail has no known shape. An inherited
   untyped closure binding now also carries its proc-shape idx so a `$g` read
   keeps the signature for this check. Tests:
   `typed_closure_return_verbatim.jacl` (+ `_err`).

4. **Nested compounds inside `[Proc …]`** — was "by design unsupported" because
   nested-compound shape idxs weren't portable across the typer/compiler
   registries. The **registry unification** (docs/TYPER_SHAPE_UNIFICATION_AUDIT.md
   step 2b) removed that blocker — the typer now interns/decodes all shapes in
   the shared registry. On that foundation, **most compound forms now work end to
   end, with invariant conformance:**
   - **`[Vec T]` PARAMS** (incl. `[Vec Struct]`) — `typed_closure_compound_param`.
   - **`[Vec T]` RETURNS** — `[f 5]` on `[Proc [i64] [Vec i64]]` narrows to
     `[Vec i64]` (element idx preserved) — `typed_closure_compound_return`.
   - **`[Arr T]` PARAMS** — `typed_closure_compound_arr`.
   - **Nested `[Proc …]` PARAMS** — `[Proc [[Proc [i64] i64]] i64]`, with
     invariant inner-signature conformance — `typed_closure_nested_proc_param`.
   - **`[Map K V]` PARAMS** — a value-kind typed-map param works end to end:
     `[Proc [[Map i64 i64]] i64]`. The typer half of the **representation
     migration** landed: `TyperProc.param_shape_idxs[]` carries the full encoded
     idx per param (computed in `parse_params` via `nested_elem_shape`, capturing
     a map's key+value), and `intern_global_proc_shape` uses it. This also fixed
     a pre-existing bug — a value-kind typed-map proc param lost its KEY, so a
     body `[map-get $m 1]` typed the key literal as i32 and hash-missed at
     runtime; `handle_proc` now patches the binding's key from the shape.
     `typed_closure_compound_map`.

   Element / inner-signature conformance is enforced throughout (`[Vec i32]` ≠
   `[Vec i64]`, `[Proc [str] str]` ≠ `[Proc [i64] i64]`, map VALUE `[Map i64 i32]`
   ≠ `[Map i64 i64]`), on both the `GlobalArity` and forward-ref stamp paths.

   **Map KEY conformance — DONE (2026-06-12), via the compiler-side
   representation migration for PARAMS.** The compiler used to compare the
   per-param VALUE/element idx (`inner`), losing a map's key — so `[Map str i64]`
   leniently conformed to a `[Map i64 i64]` sink (a runtime hash-miss). It also
   carried TWO encodings: the `annot`/`proc_sig_elem` path stored the FULL
   compound shape idx while `decode`/`GlobalArity` stored the element idx, so the
   def-binding path and the arg-passing path disagreed and the def path wrongly
   REJECTED a valid `def [Proc [[Vec/Map …]] …] f $proc`. Both are now fixed by
   carrying the FULL compound shape idx as the per-param conformance encoding
   everywhere: `compiler__decode_proc_shape_ex` emits `pidx` for a compound param,
   and the stored side re-interns it from a param's (type, value, key) via
   `compiler__param_conf_idx` (the proc-compile path keeps its element/key idxs
   for body narrowing — those are untouched). The shared registry dedups, so the
   existing invariant `lsi != dsi` compare now captures key+value+element with no
   change to the conformance fns. `call_param_struct_idxs` is consumed only for
   `TYPE_CLOSURE` params, so widening the compound encoding is inert at call
   sites. Tests: `typed_closure_compound_def` (positive vec/map/arr bind),
   `typed_closure_compound_map_key_err` (arg path), `…_map_key_def_err` (def
   path).

   **Still open:**
   - **Compound RETURNS beyond `[Vec T]`** — `[Map K V]`/`[Arr T]` returns
     (call-result narrowing) on the same footing as the vec return. Rides on the
     broader return-type gap: `typer__resolve_return_type` doesn't resolve
     `[Arr T]` returns and drops a `[Map K V]` return's KEY (no key out channel),
     so even a plain `proc f {} [Map i64 i64] {…}` mis-types `map-get` on its
     result. Add an arr arm + a key output (mirroring the param-side
     `typer__nested_elem_shape`) and thread the key onto the call-result binding.
   - **Unification cleanup tail (2c/2d)** — un-suppress AST stamps + retire
     re-derivation helpers. Entangled: the `syntax.c` prelude paths pass a NULL
     registry (shapes land in the embedded fallback, not portable), so a blanket
     un-suppress would misfire there; full 2c/2d needs those paths on a shared
     registry first. Low user-facing value (internal tidiness, wide blast
     radius), so lowest priority — the unification is *functionally* complete
     without it.

Non-bug nuance: the no-return form `[Proc [P]]` yields `dyn` (discarded at the
tail), not nil.

## 1. Goal

Make a closure used in a typed context have a real, concrete, statically-known
signature — so its body compiles against the actual element types (no `dyn`
boxing across the lambda boundary, no restore-pass, no runtime numeric
promotion to paper over tag mismatches). JACL has no generics; the technique is
**monomorphization** (specialize per concrete type at the use site), which is
sound because the relevant type is always statically known at that site.

---

## 2. Core rules (settled)

### 2.1 Where a closure's type comes from

A closure gets a concrete signature one of two ways; there is no third:

- **Inline proc literal** (`[\ … ]` or `[proc {x} {…}]` — anonymous, single-use
  by construction) → **monomorphized to the expected closure type in context.**
- **Named / bound closure** (a var-ref to a `def`/`proc`) → must **already have
  a concrete signature**; it is conformance-checked, never inferred.

`\` is pure sugar for an anonymous `proc {^it} { … }`; the core never special-
cases it. The compiler detects "inline" structurally: the HOF/return/element arg
is a proc-literal AST node, not an `AST_VAR_REF`.

### 2.2 Inline: untyped params infer, typed params check

For an inline proc literal monomorphized against expected type `[Proc [T…] R]`:

- **Untyped params** (`{x}`, or `\`'s `it`) → bind each param to the
  corresponding `T`; type the body; **infer** the return (checked against `R`
  if the context supplies one — see 2.4).
- **Typed params** (`{i64 x}`) → the signature is fixed; **conformance-check**
  it against `[Proc [T…] R]`. Mismatch (e.g. `{i64 x}` at an `[Stream f64]` HOF)
  is an error — an explicit annotation is never silently re-typed.

### 2.3 Conformance is invariant (exact structural match)

A closure conforms to `[Proc [T1 … Tn] R]` iff: same arity, each param type
matches exactly, return type matches exactly. **No subtyping, no variance.**

This is what *enforces* strictness, not a bolted-on check: proper function
subtyping is contravariant in params, which would make `[Proc [dyn] …]` usable
where `[Proc [i64] …]` is expected (the "dyn accepts anything" fallback we
reject). Invariance blocks that. Variance is moot regardless — JACL has no
subtype relation among concrete types (i32 is not a subtype of i64).

### 2.4 The monomorphization *context* (generalizes "HOF element type")

An inline literal is monomorphized against whatever expected closure type its
position supplies:

- **HOF over a typed collection/stream** → params constrained to the element
  type(s); **return inferred** (the HOF doesn't constrain it). `transform
  [Stream i64] [\ * $it 2]` → mapper `[Proc [i64] <inferred i64>]`.
- **Explicit `[Proc …]` context** (return position, typed param, typed array
  element) → params **and** return constrained; return is checked.

Same mechanism; the only difference is whether the context pins the return.

### 2.5 `dyn` is an ordinary type

`dyn` is allowed anywhere a type can appear, including inside `[Proc …]`
(`[Proc [dyn] dyn]`, `[Proc [i64] dyn]`). No special carve-out. A dyn-param
closure simply fails invariant conformance against a concrete-param requirement,
so **a dyn closure in a typed context is an error** — as a consequence of 2.3,
not a separate rule. (A dyn *return* is legitimate: a heterogeneous-body mapper
has type `[Proc [i64] dyn]` and produces a `[Stream dyn]`.)

---

## 3. Type syntax: `[Proc [params] ret]` (settled)

Parallel to `vec`/`[Vec T]`, `arr`/`[Arr T]`, `proc`/`[Proc …]`.

```
[Proc [i64 i64] i64]      # (i64,i64) -> i64
[Proc [i64] i64]          # (i64) -> i64
[Proc [] i64]             # () -> i64
[Proc [i64]]              # (i64) -> nil   (return optional, defaults nil)
[Proc []]                 # () -> nil      (minimal form)
[Arr [Proc [i64] i64]]    # array of (i64)->i64
[Proc [[Proc [i64] i64]] i64]   # higher-order: takes a (i64)->i64, returns i64
```

- **Types only, no names** — calls are positional, so param names have no
  meaning in a type. `[Proc [i64] i64]` and `[Proc [i64] i64]` are the only
  spelling; stray names are rejected.
- **`[]` not `{}`** — a param list is a *list of types*, not a statement block;
  `{}` is the block delimiter and would be a category error here.
- **Whitespace-separated**, exactly like `[Map K V]` / `[vec 1 2 3]`. No commas.
- **Param list mandatory** (even `[]`); **return optional**, defaults nil.
- **`dyn` permitted** inside (2.5).

