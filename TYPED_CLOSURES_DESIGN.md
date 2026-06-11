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

## Remaining work (authoritative, as of 2026-06-11)

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
   **Still open (same class):** a closure value flowing through an UNTYPED
   binding (`def f $dbl` / `def f [proc …]`) or a get (`def g [vec-get …]`)
   collapses to `dyn` when later passed to a `[Proc …]` param — the untyped-def
   effective-type logic doesn't inherit the closure shape.
3. **Non-literal closure-returning tail** — a proc returning a closure *param or
   value verbatim* (not an inline literal) isn't conformance-checked against its
   declared `[Proc …]` return.

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

