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

- **Status:** ✅ **resolved for collect** (2026-06-10, typed collect).
  `collect` over a typed stream now produces a **typed vec `[Vec T]`** —
  wide elements (i64/u64/f64) are stored as raw flat bits straight from the
  wide pull (no box-back at all), struct elements as flat inline bytes,
  tagged-fit scalars as their tagged values. The decision is keyed off the
  stream's runtime `elem_idx` (dyn-flow and typed-flow agree); the typer
  stamps the static `[Vec T]` so `==` against typed-vec literals compiles
  (OP_TYPED_VEC_EQ gained a scalar-element arm — it previously assumed
  struct elements and crashed on a scalar sentinel). **Scope notes:**
  (a) `str`-element streams (`lines`) still collect to a PLAIN vec — typed-
  vec storage is GC-opaque raw bytes, so heap pointers must stay in a traced
  vec (same rule as the `[Vec T]` constructor, which rejects `[Vec str]`).
  (b) `vm__stream_to_tagged`'s i32-for-small arm still exists for the
  remaining dyn-normalizing consumers (spread / each-dyn / userland
  stream_next); retire it if/when those go typed. This was a BREAKING
  semantic change (user-approved): `== [collect …] [vec …]` is now a compile
  error → migrate to `[[Vec i64] …]` literals; print format is `[1, 2, 3]`.
  Tests migrated: range_builtins/exclusive/inclusive, tour, wide_f64_u64,
  struct_hof; new positive `stream_struct_collect.jacl`.

## 3. Per-element box/unbox round-trips at HOF boundaries

- **Status:** ✅ **resolved** (TYPED_CLOSURES_DESIGN.md Phase A).
  - *transform half:* an inline mapper over a wide (i64/u64/f64) stream compiles
    with a typed return, so `emit_return` no longer boxes the wide tail and the
    transform pull no longer unboxes it.
  - *filter half:* an inline predicate over a wide stream is monomorphized to
    read its param wide, so the filter pull passes the element wide with no
    box-for-call (a per-stream `pred_elem_idx` signal on `OP_FILTER`
    distinguishes it from named/dyn predicates, which still box). Predicates
    that can't type wide (e.g. mixed `== i32-literal i64-expr`) fall back to the
    dyn boxed path — see the rollback in `typer__proc_result_enc`.
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

- **Status:** ✅ **resolved** (2026-06-10, with the rest of TYPED_CLOSURES
  Phase A). The keep gate in `typer__proc_result_enc` is now
  `keep_typed = body_bound && !probe_errored` — EVERY bindable element type
  (wide i64/u64/f64, struct, and tagged-fit i32/u32/f32/bool/str) keeps its
  element-typed body, because in each case the rep the runtime delivers
  matches what the typed body reads (wide via the producer flip, struct via
  the multi-slot channel + by-value params, tagged-fit trivially). The
  restore pass survives ONLY as the rollback for a failed probe (mixed-type
  bodies degrade to the lenient dyn path). The double-walk is also truly
  gone: the generic arg pre-walk and the for-callback pre-infer now SKIP an
  inline HOF callback over a typed stream (the skip condition mirrors the
  HOF handler's exactly), so the callback body is typed exactly once — and
  body-internal static typing works (`assert-type $x i32` inside a mapper
  used to be a sticky compile error from the unbound pre-walk). Test:
  `stream_mono_tagged_fit.jacl`.

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
