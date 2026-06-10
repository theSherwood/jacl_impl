# Design — typed / monomorphized closures

Status: **Phase A in progress.** Resolves the lambda-boundary technical
debt in `STREAM_TYPING_DEBT.md` (items 1, 3, 4, and 5-by-deletion); pairs with
typed `collect` (item 2). Builds directly on the existing i64 wide-mapper path
(`c338b99`, `aa81bef`), which is already a partial monomorphization.

**Landed so far (Phase A):**
1. **Parser** — inline anonymous `proc` literals now parse inside `[...]`
   (`[proc {x} {…}]`, `[proc {x} type {body}]`); the old `parser.c:373`
   rejection is lifted. `\` is now demonstrably pure sugar for this shape.
   Test: `test/jacl/inline_proc_literal.jacl`.
2. **Transform typed return** — an inline `transform` mapper over a wide
   (i64/u64/f64) stream is compiled with a **typed return**, not a dyn return:
   `compiler__compile_hof_builtin` stashes the typer-inferred wide return enc in
   `c->hof_mapper_ret_enc` just around compiling the mapper; the `HEAD_PROC`
   path adopts it as `proc_return_type` when no return is declared. So
   `compiler__emit_return` no longer boxes the wide tail (its box branch is
   gated on `return_type == TYPE_DYN`), and the matching unbox in the
   `STREAM_KIND_TRANSFORM` pull (`vm.c`) is removed in lockstep. This kills the
   per-element box→unbox round-trip (**debt item 3, transform half**) for the
   dominant numeric idiom. Tests: `stream_transform_wide_return.jacl` (values
   past INT32_MAX), plus the existing `stream_transform_typed` /
   `stream_wide_f64_u64` stay green. Suite 484/484, 91/91 binaries.

**Still open in Phase A:** `filter`/`each` monomorphization (filter keeps
**truthiness** semantics — §5 decided), and full restore-pass removal (debt 4)
for non-wide element bodies (i32/struct/str); the wide path already drops it.

---

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

---

## 4. Implementation sketch (where it touches)

The current i64 wide-mapper path already does ~40% of this for `transform` (param
bound to i64, body compiled wide, HOF pushes wide). Completing it:

- **Typer:** for an inline HOF arg, bind each param to the element type(s) (have
  it for `transform`; add `filter`/`each`); **record the inferred return type on
  the proc node** (new — needed so codegen emits a typed return). For a named
  closure arg in a typed context, conformance-check its signature; dyn → error.
- **Compiler:** compile the inline body with typed params (generalize the i64
  wide path to all element types) **and a typed return** so `compiler__emit_return`
  stops boxing the wide tail (kills the box→unbox round-trip, debt 3). Drop the
  restore-pass for these (debt 4).
- **VM / HOF invocation:** pass the element in its typed (wide) rep and **don't
  box for the call / don't unbox the result** — the closure is now typed in and
  typed out. (`transform` already pushes wide; remove the post-call unbox once
  the mapper returns typed; `filter` stops boxing for the predicate.)
- **Parser:** allow an inline anonymous `proc` inside `[...]` (lift the
  `parser.c:373` rejection) so `[proc {x} {…}]` works and `\` is demonstrably
  just sugar.

---

## 5. Pressure tests (cases worked through)

1. **Proc returning a closure** — `proc make-adder {i64 n} [Proc [i64] i64]
   { proc {x} { + $x $n } }`. The inner literal is monomorphized against the
   declared return `[Proc [i64] i64]` (param x→i64, body typed, captures i64
   `$n`). The *result* is a typed closure **value** invoked later via a typed
   calling convention → **needs Phase B** (the `[Proc …]` return-type syntax and
   a typed-closure-value rep). In Phase A (no syntax) a returned closure stays
   dyn. Noted limitation.
2. **`each` with no return** — `each [Stream i64] [\ print $it]`. Monomorphized
   param i64; body returns nil; `each` discards the return. Fine; return type
   unused.
3. **Nested `[Proc …]` param** — `[Proc [[Proc [i64] i64]] i64]` parses and
   conforms structurally (the param must be a closure conforming to
   `[Proc [i64] i64]`). Phase B (higher-order, needs the syntax + typed closure
   value passed in). Verbose but consistent.
4. **Heterogeneous body / un-inferable return** — `transform [Stream i64]
   [\ if [> $it 0] { $it } { "neg" }]`. Return infers to `dyn` (mixed) → mapper
   `[Proc [i64] dyn]` → output `[Stream dyn]`. Allowed (2.5); no error.
5. **Capturing upvalues** — `mut i64 acc 0; transform [Stream i64]
   [\ + $it $acc]`. Param $it:i64, upvalue $acc:i64 (existing typed-upvalue
   mechanism), body typed. Works.
6. **Arity mismatch** — `transform [Stream i64] [\ … two params …]` → the HOF's
   calling convention fixes arity (transform/filter/each = 1); a 2-param literal
   is an arity error via conformance.

### Resolved sub-question

- **`filter` predicate return type:** **DECIDED — keep truthiness.** The
  predicate stays `[Proc [T] dyn]` with the current runtime falsy check; a
  non-bool predicate is *not* an error. (Strict `[Proc [T] bool]` was
  considered and rejected as too breaking for now.) `filter` monomorphization
  will still type the *input* param (dropping the box-for-call), independent of
  this return-side policy.

---

## 6. Phasing

- **Phase A — inline monomorphization, no new syntax.** Rules 2.1/2.2/2.4
  (HOF/param-only context) + 2.3/2.5 for inline literals; typed param + typed
  body + typed return + typed HOF invocation; dyn-named-closure-in-typed-HOF =
  error; parser allows inline anonymous procs. Resolves debt 3 & 4 for the
  dominant idiom; makes debt 1's HOF-predicate consumer dead. **No `[Proc …]`
  syntax needed.**
- **Phase B — `[Proc …]` types.** §3 syntax → typed named closures, typed
  closure params, typed closure arrays, closures-returning-closures (case 5.1),
  higher-order procs (case 5.3). Requires a typed-closure-**value** rep + typed
  calling convention for stored/passed closures.

### Independent (neither A nor B, coordinate separately)

- **Typed `collect`/spread → `[Vec T]`** (debt 2) — terminal materialization.
- **Delete the runtime numeric promotion** (debt 1/5) — a strict-vs-lenient
  **policy** decision, made *after* A (which renders it dead for inline HOF
  predicates). It still serves general dyn-context comparisons (`if [> $dynvar
  3.5]`); deleting it makes those error / require a cast. Decoupled from A —
  typed ops don't route through the promotion path, so they coexist until the
  decision is made.
