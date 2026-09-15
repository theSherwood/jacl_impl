# Invariants

The design rules that answer *"is this change allowed?"*. A change that breaks one is wrong
until the invariant itself is deliberately renegotiated with the owner.

This file is **seeded, not complete.** It currently covers the rules established by the
integer-model work (jacl #106 and its slices); other areas of the system have invariants that
are still only written down beside the code. Add them here as they are settled, and keep each
entry to the shape below: the rule, then *why it cannot simply be relaxed*.

---

## V1. A static type is a promise about representation; `dyn` is a promise about value

`i32`, `u32`, `i64`, `u64` are C variables — untagged, exactly that width at runtime. `dyn` is
an integer of conceptually arbitrary precision whose representation (inline i32, boxed i64,
boxed bigint) the program cannot observe.

Everything in `docs/TEMEN_NUMERICS.md` § "The integer model" follows from this one sentence.
Relaxing it collapses the distinction the whole typed fast path is built on: if a static type
were only a hint, an annotation could change a program's answers, and adding one would stop
being a speed decision.

## V2. A dynamic integer is always in its narrowest representation

Every construction of an integer goes through a canonicalizing constructor — `jacl_int_result`
for the machine tiers, `jbig_canon` for bigint. A bigint whose value fits an i64 must not
exist; a boxed i64 whose value fits an i32 must not exist.

**This is not a size optimization.** `jacl_val_equal` settles integers by value while
`jmap_key_hash` mixes the raw bits — a pointer, for a heap value — so two representations of
one number are *distinguishable* unless exactly one is canonical. A wide-computed `37` hashing
to a different bucket than the inline `37` it compares equal to was a real, silent map-lookup
miss (jacl #107). Any new path that produces an integer must canonicalize, or that bug returns.

## V3. A statically typed value never carries taint or secret

Coercing a flagged `dyn` to **any** static type is an **error**, not a flag drop (jacl #95).

A raw, untagged word has no bits to carry a flag, so the alternative to refusing is laundering:
a tainted value would cross into the typed world and come back out as a plain number. That is
the exact failure a taint system exists to prevent, so the crossing has to refuse rather than
silently succeed. The refusal keeps the flags, so the fact is not lost along with the cast.

Enforced at every `dyn` → static crossing: `jacl_to_cast`, `jacl_widen_to`, `jacl_i64_unbox`,
and codegen's `emit_has_any_flag` before a raw-word crossing. The codegen test costs exactly
what the error-only test it replaced cost — a wider mask constant, the same two instructions.

**Status:** taint and secret have no source-level producer yet (`NOT_IMPLEMENTED.md`), so
today the rule is enforced ahead of the feature rather than exercised by it. That is
deliberate: the crossings are the only place it *can* be enforced, and retrofitting them after
a producer lands means auditing every one of them under time pressure.

## V4. Narrowing never truncates

`dyn` → a static width is explicit (`[to T V]`) and range-checked; a value the target cannot
hold is a domain error (jacl #116). Static → `dyn` is implicit, cannot fail, and canonicalizes.

The asymmetry is the point. A `dyn`'s contract is that its width is not observable, so it must
not become a *different number* by being stored — that is the one place the model could leak.
Dropping a float's *fractional* part is a different operation and is allowed; refusing is about
magnitude.

**The leak is easier to open than it looks.** `[to "i64" 18446744073709551615]` answered
`8589934593` for a while (jacl #138) — not because the range check was missing, but because
reading the value gave a number that passed it. `jacl_is_anyint` admits a bigint and
`jacl_int_val` does not handle one, so the check ran against the `JaclBig` header. Anything
enforcing this invariant has to be sure it is reading the *value*, and by V2 a bigint has no
int64 to read: its magnitude necessarily exceeds what an i64 holds, so every static integer
width refuses it by construction.

## V5. A declared width errors on overflow; `dyn` promotes; `+% -% *%` wrap

Three behaviours, one per spelling (jacl #102, #103, #106). A declared `i32` cannot promote —
the promoted value is not an `i32`, so an `i32`-annotated proc would return something its own
signature forbids — and silently wrapping would mean adding an annotation changes a program's
answers. `dyn` promotes through the whole tower and therefore **cannot fail on magnitude**;
only domain errors remain. Wrapping is available, but only when the program asks for it by
name.

## V6. A raw-word representation needs a separate channel for the error

A tagged result can carry an error flag; a raw untagged word cannot. So where a value is a raw
word, the error travels beside it rather than inside it. Two shapes, both in use:

- **Control flow.** Inside a function body, an overflowing operation *branches* — through the
  enclosing `[try …]` or the function's error return — rather than producing an
  error-flagged value.
- **A second result.** A proc whose declared return is a raw width returns
  `(value, error)`: the raw entry is a two-result function, the error word is 0 on the normal
  path, and the value word is 0 whenever the error word is not. The decision of what to do
  with the error lands at the call site, which is the only place that knows whether the value
  is wanted raw or boxed.

The observable rule (V5) is unchanged; the mechanism is forced by the representation. Any new
raw representation inherits this, and a new one that returns a **sentinel** — an in-band value
that means "failed" — is wrong, because there is no raw bit pattern a caller can distinguish
from a legitimate result.

## V7. Where the typer is unsure, the representation stays tagged

The raw, untagged representations are taken **only** where the typer proved the type. The
fallback is correct-but-slower, never fast-but-wrong.

The failure mode is what makes this absolute: narrowing a tagged value to a raw i32 is a bare
truncation, so on a string binding it is the low half of a *pointer*, re-tagged as a perfectly
plausible integer. Silent garbage is the one outcome these representations must not have, and
it is a worse failure than the stale operand of jacl #100, which at least the verifier caught.
