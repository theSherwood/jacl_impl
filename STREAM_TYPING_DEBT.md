# Technical debt — typed streams / lambdas / contextual literals

Actionable list of compromises made during the lambda-typing → producer-wide →
contextual-literal effort (`120fc1f` … `aa81bef`). Companion to
`LAMBDA_TYPING_PLAN.md` (design) and `STREAM_TYPING_ISSUES.md` (retrospective).
Each item: where, why it exists (forced vs pragmatic), impact, remediation,
rough effort. Nothing here is a correctness bug — the suite is 482/482 and TSAN
is clean; these are cleanliness/consistency compromises.

> **Design for the unifying fix (items 1, 3, 4, 5-by-deletion): `TYPED_CLOSURES_DESIGN.md`.**

## Root cause (most items collapse into this)

**Lambdas have `dyn` params/returns at runtime — JACL has no generics.** So every
value crossing a lambda boundary (mapper, predicate) boxes, and the type-tag
friction that causes was papered over with runtime promotion + boundary
box/unbox rather than monomorphizing. The unifying fix for items 1, 3, and 4 is
**typed / monomorphized closures** (specialize a HOF lambda to its element
type, with a typed param and typed return). That's a large project; until then
the items below are the localized compromises.

---

## 1. Runtime numeric promotion is dynamic coercion (biggest)

- **Where:** `vm__numeric_order` in `src/vm.c`, wired into `OP_GT/LT/GE/LE`.
- **Why (pragmatic):** the strict-static path needed wide values pushed into
  wide-compiled HOF *predicate* bodies (tightly coupled to the wide flip and the
  dyn predicate param). We chose runtime promotion of mixed numeric tags
  instead. Localized (only fires when operands are already dyn-tagged), but it
  is dynamic coercion — counter to the strict-typed-stream direction.
- **Impact:** `filter [fs] [\ > $it 3.5]` works, but via runtime promotion, not
  static typing. A genuinely strict design wouldn't need it.
- **Remediation:** typed/monomorphized closures → predicate params become
  statically typed → the literal narrows at compile time → the promotion path
  becomes dead code and can be deleted.
- **Effort:** large (the closure-monomorphization project). Until then, keep.

## 2. `i32-for-small` box-back conflates i64/i32

- **Where:** `vm__stream_to_tagged` in `src/vm.c` (the `TYPE_I64`/`TYPE_U64`
  arms emit `jacl_i32` for values that fit i32).
- **Why (forced, behavior-preserving):** keeps `collect [range 1 5] ==
  [vec 1 2 3 4]` working (the literals are i32). Without it, collected i64
  elements would be i64 and the equality (and other tests) would fail.
- **Impact:** the tagged rep of a wide-stream element is i32 when small, i64
  when large — not a clean single type. Conflates i64 and i32 at the boundary.
- **Remediation:** decide the intended semantics of `collect [Stream i64]` — if
  it should produce a typed `[Vec i64]` (see the open "typed collect → [Vec T]"
  work in `NOT_IMPLEMENTED.md §4`), the box-back goes away (elements stored
  wide in a typed vec). Couples with that feature.
- **Effort:** medium; do it with typed-collect.

## 3. Per-element box/unbox round-trips at HOF boundaries

- **Status:** ✅ **transform half resolved** (TYPED_CLOSURES_DESIGN.md Phase A).
  An inline `transform` mapper over a wide (i64/u64/f64) stream now compiles
  with a typed return, so `emit_return` no longer boxes the wide tail and the
  transform pull no longer unboxes it — round-trip gone for that path. The
  **filter half remains** (still boxes the wide element to call the dyn
  predicate); resolved when `filter` input-param monomorphization lands.
- **Where:**
  - `transform` branch in `src/vm.c`: the mapper (dyn return) boxes its wide
    result via `compiler__emit_return`, then `transform` immediately unboxes it
    back to wide (the `vm__elem_idx_is_wide` unbox after the mapper call).
  - `filter` branch in `src/vm.c`: boxes the wide element to tagged to call the
    dyn predicate, then yields the wide original.
- **Why (semi-forced):** lambda params/returns are dyn, so values must be boxed
  to cross the boundary; the surrounding pipeline wants wide.
- **Impact:** a box + an unbox per element at each HOF stage — avoidable
  overhead on hot numeric pipelines.
- **Remediation:** typed closures — a mapper with a typed return wouldn't box
  (so no unbox needed); a predicate with a typed param wouldn't need the
  box-for-call.
- **Effort:** large (same closure project as item 1).

## 4. Restore-pass only partially removed

- **Where:** `typer__proc_result_enc` in `src/typer.c` — `body_wide` gates the
  restore-pass drop to i64/u64/f64 element types only.
- **Why:** the wide-body codegen is correct only when the value arrives wide
  (i64/u64/f64). For i32/struct/str-element mappers the restore-to-`it:dyn`
  double-walk is kept, so those mapper bodies are typed `it:dyn` (lose the
  element type in codegen).
- **Impact:** the "kill the double-walk wart" goal is partial; i32-element (and
  struct/str) transform mapper bodies still run dyn. Also two cheap missed
  opportunities: (a) an i32-element body could safely drop the restore (tagged
  i32 body reads a tagged i32 value fine — no garbage), tightening it; (b) the
  double-walk's nested-pipeline cost remains for the non-wide cases.
- **Remediation (cheap part):** allow `body_wide` (or a sibling flag) to also
  drop the restore for i32/u32/f32/bool element types where the body rep
  already matches the tagged value. **Remediation (full):** typed closures
  remove the restore-pass entirely.
- **Effort:** small for the i32 tightening; large for full removal.

## 5. Comparison/arith promotion asymmetry

- **Where:** `src/vm.c` — promotion added to `OP_GT/LT/GE/LE` only; `OP_EQ`
  left cross-type-permissive (mismatched tags → not-equal); dynamic arithmetic
  ops (`OP_ADD/SUB/MUL/DIV/MOD`) not given promotion.
- **Why:** scoped to exactly what unblocked filter predicates; EQ already didn't
  error, and transform mappers use typed wide arithmetic (so dyn arith wasn't
  hit).
- **Impact:** inconsistent numeric semantics — `3.0 > 2` promotes and works, but
  `3 == 3.0` is likely false (no promotion) and a dyn-context `+ f64
  f32-literal` could still error. Surprising.
- **Remediation:** decide the intended cross-width numeric semantics, then
  extend `vm__numeric_order` (or an equivalent) to `OP_EQ` and the arithmetic
  ops for consistency — OR explicitly document that only ordering promotes and
  why. Watch: making `==` promote changes `3 == 3.0` from false to true, which
  may affect existing cross-type-equality expectations — check the suite.
- **Effort:** small-to-medium (mechanical, but the EQ semantics change needs a
  suite check + a decision).

---

## Suggested sequencing

1. **Item 5 (consistency)** and **item 4's cheap i32 tightening** — small, remove
   surprising asymmetries, no big dependencies. Do first.
2. **Item 2** — fold into the typed `collect`/spread → `[Vec T]` work when that's
   scheduled.
3. **Items 1, 3, 4-full** — collapse into **typed/monomorphized closures**, the
   one project that makes the whole thing genuinely strict (deletes the runtime
   promotion and the boundary box/unbox). Largest; do when ready to invest in
   the closure model. This is the line between "coherent with named debt" and
   "no compromises."
