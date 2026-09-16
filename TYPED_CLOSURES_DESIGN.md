# Design — typed / monomorphized closures

Make a closure used in a typed context carry a real, statically-known signature,
so its body compiles against the actual types (no `dyn` boxing across the lambda
boundary). JACL has no generics; the technique is **monomorphization** — see §1.

## Status — complete (2026-06-12)

The typed-closure arc is **functionally complete and sound**. Everything below
landed; the per-phase handoff log was trimmed (git history + tests have the
detail). §2/§3 are the enduring spec.

**Capabilities** — `[Proc [P…] R]` type syntax; closure VALUES / PARAMS /
RETURNS; user-defined HOFs; closures-returning-closures; named-closure values;
collections (`[Vec/Arr [Proc …]]`) with get/push narrowing; struct params &
returns; **compound params & returns** (`[Vec T]`/`[Arr T]`/`[Map K V]`/nested
`[Proc …]`); global procs as first-class values; forward-ref-as-value
(higher-order mutual recursion); def-binding of a compound-returning closure;
untyped-binding/get narrowing. All via a shared-registry `TYPE_SHAPE_PROC` +
portable proc-shape stamp — **no VM change**.

**Conformance is invariant and exact, by construction.** A closure conforms to
`[Proc [T…] R]` iff arity, each param, and the return match *exactly* (no
subtyping/variance — see §2.3). The implementation leans on the shared type-shape
registry: every shape (including nested compounds) is interned once and deduped,
so a structurally-equal signature has the *same* portable idx. Conformance then
reduces to comparing those idxs — `typer__proc_shapes_conform` compares the
proc shapes' param-pool entries + return idx directly, which captures a vec/arr
element, a map's key AND value, and a nested `[Proc …]` signature without
decoding. This is the durable lesson: **dedup shapes into one registry and
conformance becomes idx equality.**

Residual non-bugs / by-design limits: a closure with NO `[Proc …]` annotation
stays `dyn` (its type comes only from annotations or a propagated one); the
`[Proc [P]]` no-return form yields `dyn` at the tail (discarded), not nil.

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

**Status (jacl #117, the stream half).** The HOF-over-a-typed-stream case above
now actually holds; for a long while the doc was right and the implementation
was not, and the reason is worth recording because nothing about it is visible
from the JACL side.

`typer__proc_result_enc` is the call-site probe that binds a mapper's params to
the element type and reads its body tail. It accepted only `HEAD_PROC`. But in
the **emit-only** build — which is the TEMEN codegen path — prelude macros are
deliberately *not* expanded (`jacl_expand_skip_prelude` in `emit_jacl.c`);
codegen name-handles them instead. So `[\ * $it 2]` reaches the typer
**unexpanded**, as a command whose head is the bare word `\` and whose
`head_id` is `HEAD_NONE`. The probe bailed on its first line, every mapper
looked like an unknown command, and the output element type fell back to `dyn`.

The visible symptom was a cast that should not have been needed: `for
[transform [range 1 4] [\ * $it 10]] x { set s [+ $s [to "i64" $x]] }` over a
declared-`i64` `mut`. Several corpus cases carried one, with a comment saying
so. The fix normalizes all three callable spellings — `[proc …]`,
`[\ {params} {body}]` (params wrapped in a BLOCK, matching codegen's
`in_block=1`), and `[\ <head> <args>…]` (implicit `it`) — so the typer and
`compile_closure` agree on what a lambda is.

One boundary is deliberate and pinned by `test_transform_mixed_width_mapper_stays_dyn`:
a mapper that *mixes* widths, e.g. `[\ * $it 1.5]` over an `i64` source, stays
`dyn`. That is #115's strict-mixed rule doing its job — the probe surfaces the
error, rolls back, and the element falls to the lenient dyn path — not a
remnant of this bug.

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

