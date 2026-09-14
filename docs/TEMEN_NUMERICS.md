# The TEMEN backend numeric model

How JACL numbers are represented, promoted, compared, and printed by the TEMEN runtime
(`runtime/builtins.c`). This is the behaviour the codegen relies on when it lowers an
arithmetic head to a `jacl_*` call, and the reference for anyone touching the numeric
entry points. Everything here is about the *runtime* tower; the typer's static widths
(`i32`, `u64`, `f64`, …) drive which values flow in, but the runtime tower below is what
actually executes.

## The integer model

One rule decides everything below it:

> **A static type is a promise about representation. `dyn` is a promise about value.**

There are five integer types. Four of them — `i32`, `u32`, `i64`, `u64` — are **C
variables**: untagged, exactly the width the name says, no spare bits, no flags, no
runtime type word. What a program declares is what the machine holds. The fifth, `dyn`, is
**an integer**, conceptually of arbitrary precision; its width is a representation the
compiler picks, re-picks, and never lets the program observe.

### `dyn`: one word, three representations

A `dyn` integer is always a tagged 64-bit word, in one of three forms:

| form | when | tag |
|---|---|---|
| inline `i32` | the value fits `[INT32_MIN, INT32_MAX]` | `0x02` |
| pointer to a boxed, untagged `i64` | it fits 64 bits signed | `0x0E` |
| pointer to a boxed, untagged bigint | anything larger | `0x09` |

The form is chosen by **magnitude alone** — never by the path that computed the value.
That is what `jacl_int_result` enforces, and it is not a nicety: `jacl_val_equal` settles
integers by value while `jmap_key_hash` mixes the raw bits, so two representations of one
number are *distinguishable* unless exactly one is canonical. A wide-computed `37` hashing
to a different bucket than the inline `37` it compares equal to was a real, quiet map-lookup
miss (jacl #107).

Canonicalization is therefore the load-bearing part of "width is an implementation detail",
and it runs in both directions: a value that grows past i32 boxes, and a value that shrinks
back into i32 range un-boxes.

### What follows from the rule

**`dyn` arithmetic cannot fail on magnitude.** It promotes: inline i32 → boxed i64 →
bigint. The only failures left are domain errors (division by zero, `%` by zero). This is
the property that makes `dyn` worth having, and it is why the bigint tier is not optional
garnish — see "The bigint tier" below.

**A declared width errors on overflow.** The promoted value is not an `i32`, so an
`i32`-annotated proc returning one would break its own signature. Wrapping silently is
worse still: it would mean *adding an annotation changes a program's answers*, which would
make widening the typer's reach a semantic change rather than a speed one. `+% -% *%` wrap,
because there the program asked.

**A declared width errors on a literal it cannot hold.** `def i32 x 5000000000` is a type
error, for the same reason and at compile time. A *constant expression* counts as a literal
here — `[* 2000000 1500]` under a declared `i64` multiplies at 64 bits — but adopting a
width never changes the operation: `def f64 x [/ 1 2]` is still integer division, giving
`0.0` exactly as C does. See `TYPE_SYSTEM.md` § 6.

**No implicit conversions between static types — C widths, not C conversions.**
`[+ $u64 $i64]` is a type error; write the `to` you mean. C's integer-promotion and
usual-arithmetic-conversion ranking is a famous footgun (the signed operand converts to
unsigned, and a negative number becomes enormous), and there is no reason to inherit it
along with the widths.

**`dyn` → static is explicit and checked; static → `dyn` is implicit and canonicalizes.**
`[to "i32" $d]` is a **domain error** if `$d` does not fit — the class `[/ 1 0]` produces,
never a truncation. This is the one place the model could leak: a `dyn`, whose whole
contract is that its width is not observable, must not become a *different number* by being
stored. (`[to "i32" 5000000000]` used to answer `705032704`.) The rule covers the declared
widening too, where the only lossy case is sign: `def u64 x [- 0 5]` is an error rather than
`18446744073709551611`.

Dropping a float's **fractional** part is a different operation and stays — `[to "i32" 3.9]`
is `3`, as a float-to-int conversion means everywhere. What is refused is a float whose
*magnitude* the target cannot hold, and a non-finite one; both are undefined behaviour to
convert in C rather than merely lossy, so the check runs in double arithmetic before the
cast. It is written as `d >= (double)lo && d < (double)hi + 1.0` because `(double)INT64_MAX`
rounds *up* to 2^63, so the inclusive form would admit a value one past the end — and
because NaN has to fail, which a positively-phrased test gives for free.

The other direction needs no syntax and cannot fail: the value is re-canonicalized on the
way out, which is what makes a typed `i64` holding `37` the same map key as the literal
`37`.

**Signedness is a static property only.** There is no such thing as a `dyn u64`: `dyn` is
"integer", signed, unbounded. A static `u64` above `INT64_MAX` therefore needs the bigint
tier to reach `dyn` at all — it cannot borrow a tag to remember it was unsigned, because
under this model no tag carries that meaning.

**A static value cannot carry the taint/secret flags** (jacl #95, `INVARIANTS.md` V3).
Coercing a flagged `dyn` to any static type is an **error**, not a flag drop: an untagged word
has no bits to carry the flag, so the alternative to refusing is laundering a tainted value
into a plain number. The refusal keeps the flags, so the fact is not lost with the cast.

### The bigint tier

`runtime/bigint.c`. Sign-magnitude, base 2^32, little-endian limbs, in a `JOBJ_BLOB` cell:
no outgoing pointers, so the collector needs no new tracing and a bigint can never hold a
reference alive. Base 2^32 rather than 2^64 so a limb product fits a `uint64_t` — a 2^64
base needs a 128-bit intermediate, which is not portable C and not something to assume of
the VM target.

**Canonical form is an invariant, not a convention**: a bigint whose value fits an i64 must
not exist. Every construction goes through one function, `jbig_canon`, which demotes. Two
things depend on it — `==` and map-key hashing agree across the whole tower (#107), and a
bigint compared against an i32 or i64 can be settled by magnitude alone.

Hashing is **by value**, over the limbs. A pointer hash would put two equal numbers in
different buckets, which is precisely the bug #107 was.

Two bounds worth knowing, both deliberate and both loud rather than silent:

- **~1233 decimal digits** (128 limbs). "Arbitrary precision" is the *model*; the
  implementation states a limit and exceeds it with a domain error rather than wrapping or
  smashing a stack buffer. Raising it is one constant plus moving the multiply scratch off
  the stack — the scratch is what the bound really protects.
- **Division needs a single-limb divisor.** Big-by-big needs Knuth algorithm D (jacl #121);
  until then it is a domain error, never a wrong answer.

`u64` is the one type that still errors on overflow rather than promoting, and the tag is
what forces it: promoting drops the record that the value was meant to be unsigned, and
`dyn` has no unsigned form to promote *into* — the model puts signedness on the static side
only.

## Values: inline scalars vs heap wides

A `JaclVal` is a 64-bit tagged word — an 8-bit tag over a 56-bit payload (see
`runtime/jaclrt.h`). Two numeric widths live **inline** in the payload, so ordinary
arithmetic never allocates:

| tag | type | payload |
|---|---|---|
| `0x02` | `i32` | sign-extended 32-bit integer |
| `0x03` | `f32` | 32-bit float |

The wider numeric types don't fit an inline 56-bit payload with full range, so they are
**heap-boxed** — a `JOBJ_BLOB` cell holding 8 payload bytes, referenced by a tagged
pointer:

| tag | type | boxed payload |
|---|---|---|
| `0x0E` | `i64` | 64-bit signed |
| `0x0F` | `u64` | 64-bit unsigned |
| `0x10` | `f64` | IEEE-754 double (bit-cast to `int64`) |

`jacl_wide_new(tidx, bits)` allocates the box; `jacl_wide_bits(v)` reads it back. Because
a wide is a heap object, **producing one costs an allocation** — a fact that shapes the
arithmetic rules below.

## The GC-livelock constraint

The single-threaded interpreter runs a conservative, non-moving collector
(`runtime/heap_gc.c`). A hot loop that allocates on *every* iteration can drive the
collector into a livelock where it never makes forward progress against the allocation
rate — surfacing in the parity harness as a spurious `hang`. This is why the numeric
rules below are written to **stay inline whenever they can**: an i32 loop counter, an
index, an accumulator that never leaves 32-bit range must not silently box itself into an
i64 every time it is touched. "Allocate only when the value genuinely needs the width" is
the governing principle.

## Integer literals

There is no size at which writing a `dyn` integer stops working. A **decimal** literal past
`INT64_MAX` becomes a bigint: the token carries its digit span instead of a value, and
codegen builds the number at run time through `jacl_big_from_decimal` (there is no constant
form to fold it into — not inline, not a 64-bit cell). `-9223372036854775808` therefore
works now, having been unreachable twice over: the digits are lexed before the leading `-`
folds in, so the *magnitude* is what has to lex, and it is one past the i64 ceiling.

`0x` and `0b` keep the `INT64_MAX` ceiling and error past it. A hex or binary literal that
wide is far likelier a typo than an intent, and the digit-span path is decimal-only.

A literal that overflows is never a silently truncated number — it used to be (jacl #105).

A literal that fits i32 lowers to the inline i32 constant the rest of the compiler expects.
A wider one has no inline form — a dynamic wide int lives on the heap — so codegen builds it
at run time through `jacl_i64_box`, the same canonicalizing box a typed i64 uses when it
crosses to `dyn`. That is what lets a wide literal be a map key: it hashes like every other
spelling of the same number.

The typer types an out-of-i32 literal `i64` — including under an i32 *expectation*, because
an i32 expectation is not always a user annotation: a binop unifies its operands by
expecting the first one's type, which is what `[- 0 9223372036854775807]` is. A declared
width is not enforced against a literal initializer either way; `def i32 x 5000000000`
binds the i64, exactly as `def i8 x 300` already bound 300. That gap predates i64 literals
and is not narrowed here.

Two places still carry the literal as an i32 and now refuse a wider one instead of
narrowing it silently: the staged-syntax plain-data form (`syn_rt.c` / `syn_wire.c`, i.e. a
wide literal inside a quoted macro body) and a `[Buf N T]` length, which no buffer could
reach anyway.

## Arithmetic

`jacl_add` / `jacl_sub` / `jacl_mul` / `jacl_div` / `jacl_mod` take two `JaclVal`s and
return one, propagating the error flag (see below). The dispatch, in order:

1. **Both i32 → stay i32, promote only on overflow.** `+`, `-`, `*` compute the i32 result
   with `__builtin_{add,sub,mul}_overflow`. If it does not overflow, the result is an
   inline i32 — no allocation. If it does, the operation is redone in 64-bit and the result
   is boxed as an `i64`. So `[+ 2 3]` is inline `5`, but `[* 100000 100000]` is a boxed
   `10_000_000_000` rather than a wrapped `1410065408`. This overflow-only rule is what
   keeps GC-stress loops allocation-free while still giving wide results their true value.
2. **Either side f64 → f64.** If one operand is an f64 (and the other is any int or float),
   the op runs in double precision and boxes an f64. `f32` participates through the shared
   `jacl_num_f64` view.
3. **Either side a wide int → wide int.** If either operand is already an `i64`/`u64` (so
   the value is wide regardless of the other side), the op runs in 64-bit and boxes with
   `jacl_iwide_tag` — `u64` if either side is `u64`, else `i64`.
4. **Otherwise fall back to f32** for mixed small-numeric operands, or return an
   error-flagged value if an operand is not numeric at all.

`jacl_div` guards divide-by-zero (returns an error-flagged `0`) and the
`INT32_MIN / -1` overflow (returns `INT32_MIN`, avoiding UB). `jacl_mod` is i32-only and
guards the same two edge cases. Unary minus `[- x]` is lowered by the codegen as `0 - x`,
so it flows through `jacl_sub` and inherits every promotion above (including
`- INT32_MIN → 2147483648` as a boxed i64).

### Typed i32: a raw, untagged word

A binding the typer proved is `i32` holds an untagged, **sign-extended** 32-bit word in its
frame slot (jacl #114) — the same change slice 1a made for `i64`. The frame is all-i64, so
the invariant is worth stating once because every store depends on it:

> A `REP_I32` slot holds the **sign-extended** value.

Sign- rather than zero-extension so the slot reads as the same number at 64 bits, which is
what makes a stray 64-bit compare on it right instead of subtly wrong. (Pins stay
zero-extended; a pin round-trips bits, not a value.)

This is a smaller change than it sounds, because typed i32 *arithmetic* was already raw:
`compile_i32` returns native values and `box_i32_checked` boxed once at the root of the
tree. It was the **binding** that carried a tag. What actually changes:

- Reading an i32 binding for arithmetic is a narrow instead of an untag, and crossing back to
  `dyn` is a narrow and an OR — no runtime call in either direction, unlike `i64`, whose
  crossing may allocate. An i32 is already the narrowest representation of its value, so
  there is nothing for the crossing to canonicalize.
- **Overflow becomes control flow.** The boxed tree folds its sticky overflow bit into the
  result's error flag, which works only because a tagged i32 has a spare bit to put it in. A
  raw word does not, so the same bit becomes a branch through the enclosing `[try …]` or the
  function's error return — identical trade to `i64`, forced by the same fact.

One guard is load-bearing: the raw representation is taken **only where the typer proved the
value is `i32`**. Unlike `i64`, whose fallback crossing goes through `jacl_i64_unbox` and
type-checks, narrowing a tagged value to i32 is a bare truncation — on a string binding that
is the low half of a pointer, re-tagged as a perfectly plausible integer. Where the typer is
unsure, the binding stays tagged and correct-but-slower, the same safe default slice 1a set.

`u32` and `u64` are **not** raw yet, and the reason is not effort. A static `u64` above
`INT64_MAX` has no `dyn` representation to cross into — the bigint tier is signed, and
`u32`/`u64` have no native typed arithmetic (the IR has `div_u`/`rem_u`/`lt_u`, but nothing
emits them), so giving them raw slots today would add a box on every operation and make them
*slower*. Both wait on their own slice.

### Typed i64: a raw, untagged word

A binding the typer proved is `i64` holds an **untagged 64-bit word** — a C variable, no tag
and no spare bits (jacl #106). `codegen.c` tracks that on the binding (`Binding.rep`), and
reads go through `env_value`, which boxes a raw binding back into a tagged JaclVal. That
default is deliberate: a site that has not been taught about raw words gets a correct (if
slower) value rather than reading a raw word as a tag, and a missed coercion would be silent
garbage. Only the typed-arithmetic path opts into reading raw.

Boxing on the way out canonicalizes, so a typed i64 holding `37` reaches dynamic code as the
inline `37` that every other spelling produces — the same map key, the same `==`.

Because the word has no spare bits, an overflowing typed op cannot return an error *value*
the way an i32 tree's root does. It branches instead, through the enclosing `[try …]`
handler or the function's error return, so the observable rule is the same ("a declared width
errors on overflow, catchably") by a different mechanism. Add and subtract detect it inline
from the operand and result signs; multiply asks `jacl_i64_mul_ovf`, because there is no sign
trick for it and the obvious "divide back and compare" traps the host on `INT64_MIN / -1`.

Slice 1a covers an immutable local inside a proc. A top-level binding is mirrored into the
global map and a captured `mut` lives in a heap cell — both store a JaclVal, so an i64 bound
that way stays tagged until those paths learn about raw words, as do `i64` proc parameters
and returns (slice 1b) and `u64` (slice 1c).

One wart inherited from the type system: `typer__infer_command` clears the expected type at
command boundaries, so a declared type does not reach a binop over *literals* —
`def i64 d [* 2000000 1500]` multiplies at i32 and reports the overflow rather than
multiplying at 64 bits. `def i64 d [* $k 1500]` (one typed operand) does use the declared
width. That matches C, where the same initializer also computes in `int`, and it now fails
loudly rather than silently; widening it is a typer change with its own blast radius
(jacl #112 — literals should be `dyn`, and a `dyn` product should promote).

i64 literals sharpen the asymmetry without resolving it: `[* 5000000000 2]` promotes,
because one operand is already too wide for i32 and the whole node types `i64`, while
`[* 100000 100000]` still errors — same product, different spelling.

### Canonical dynamic integers

A **dynamic** integer takes the narrowest representation that holds it: its representation
is a function of the magnitude, never of the path that computed it. Every dynamic wide
result is built through `jacl_int_result`, which hands back an inline i32 when the value
fits.

This is what makes the representation unobservable, and it has to be, because two spellings
of one number are otherwise distinguishable: `jacl_val_equal` settles integers by value
while `jmap_key_hash` mixes the raw bits — a *pointer*, for a heap wide int. A wide-computed
`37` therefore hashed to a different bucket than the inline `37` it compares equal to, and a
map lookup silently missed (jacl #107). It only surfaced once a map outgrew the small-map
linear scan, which is what kept it quiet.

Two things deliberately keep their wide form:

- **`u64`** — the tag is the only record that the value is meant to be unsigned, and
  dropping it would change which tag later arithmetic picks.
- **Explicit widening** (`def i64 x 37`, `[to "i64" 37]`) — that is the *typed* side of the
  rule, where a declared type should mean exactly that representation at runtime. Honoring
  it end to end needs the native i64 lowering (jacl #106 slice 1), which is also what gives
  the typed → dyn crossing a place to canonicalize; until then such a value stays wide and
  is not interchangeable with an inline i32 as a map key.

### Wide (i64/u64) overflow

An overflowing wide op **promotes to a bigint**: `jacl_add`/`sub`/`mul` ask
`__builtin_*_overflow` before building the result, and hand an overflow to the bigint tier.
Signed 64-bit overflow is UB in C, so the original unguarded
`jacl_int_val(a) + jacl_int_val(b)` was not merely a silent wrap — it was undefined
(jacl #104). It errored for a while after that, which was loud but still a violation of the
model; it now promotes, which is the rule. `u64` is the exception, for the reason given
above.

### Overflow: three behaviors, one per spelling

The promotion rule above applies to a **dynamic** value, whose width is not declared. A
value the typer has *proved* is `i32` cannot use it: the promoted result would be an i64,
which is not an i32, so an `i32`-annotated proc would return something its own signature
forbids. Silently wrapping instead is worse — it would mean adding an annotation changes a
program's answers, and it would make widening the typer's reach a *semantic* change rather
than a speed one. So overflow in a typed i32 computation is an **error**:

| spelling | on overflow |
|---|---|
| `+ - *` on dynamic values | promote to a wide int (`[* 100000 100000]` → `10_000_000_000`) |
| `+ - *` on values proved `i32` | error-flagged result — catchable with `try` / `error?` |
| `+% -% *%`, anywhere | wrap modulo the operand width |

```jacl
proc dbl {i32 n} i32 { + $n $n }
print [error? [dbl 2000000000]]     # true  — declared i32, cannot promote
def a 2000000000
print [+ $a $a]                     # 4000000000 — dynamic, promotes
print [+% $a $a]                    # -294967296 — asked for a wrap
```

`/` and `%` join `+ - *` on the typed path (jacl #94), and comparisons get a native op of
their own. Two inputs must never reach a machine divide, because both trap: a zero divisor,
and `INT32_MIN / -1`, whose true quotient is not an i32. Neither can be computed and checked
afterwards, so both are steered around by substituting a divisor of 1 — which gives the right
answer for the second case for free (`INT32_MIN / 1` is `INT32_MIN`; `INT32_MIN % 1` is 0,
which is what the runtime returns for each). The zero case then needs only its payload masked
to 0 and the sticky failure bit set. No branch, so it composes inside a larger tree.

A typed comparison folds that same sticky bit into the bool's error flag, so
`[< [* $a $b] $c]` with an overflowing multiply is an error rather than a comparison against
a wrapped value. The #98 monomorphic guard reaches the same instructions on the *dynamic*
path, but pays a runtime tag test and a diamond to find out it may; a proved operand needs
neither.

The typed lowering computes each op in 64 bits on the sign-extended operands, narrows back
to i32, and ORs "the result left i32 range" into a sticky bit that becomes the error flag
when the tree re-boxes (branchless — an error-flagged i32 is an ordinary JaclVal, the same
shape `jacl_div` returns for a zero divisor). `+% -% *%` skip the check and emit the bare
`i32.add` / `i32.sub` / `i32.mul`; on dynamic operands they call `jacl_wrap_add` & co.,
which wrap two i32s modulo 2^32, two wide ints modulo 2^64, and return a type error for
anything else — coercing a float into a wrapping op would defeat the point of asking.

At the `extern` (C ABI) boundary a typed scalar argument is *not* checked: it is handed to
C as a machine i32, so C's wrapping is the contract there, the same reason a pointer
argument decays to a raw address.

History: the typed path wrapped silently until jacl #102 — see that issue for the
alternatives considered (documenting the wrap, versus checking it).

## Rendering

`repr_fmt_fp` formats both f32 and f64: six fractional digits, trailing zeros stripped, an
integral value printed without a decimal point. One fixed-point scale cannot span the range,
so there are three magnitude bands — below 1e13 the value is scaled by 1e6 (integral and
fractional digits, exact in a `u64`); below 1.8e19 the integral digits are printed alone (a
float that large has no fractional precision left); above that, exponent form (`1e+20`),
since an f32 carries only ~7 significant digits and printing 39 of them would be noise.

`inf` means an actual infinity. It used to be printed for anything above `INT32_MAX`, so a
correct `1e10` rendered as `inf` (jacl #108) — and an f64 was narrowed through f32 before
formatting, losing both range and precision.

## Comparison and equality

`jacl_lt` / `le` / `gt` / `ge` compare numerically across widths: two ints compare as
64-bit integers (`jacl_int_val`), anything involving a float compares as f64
(`jacl_num_f64`). So `[< 5 6000000000]` is `true` even though the two sides have different
representations.

`jacl_val_equal` (behind `==` / `!=`) is structural and width-agnostic for numbers: an
i32 `5` equals an i64 `5`. For non-numbers it is type + payload bitwise, except strings
(by content) and vectors / maps / structs (deep, recursive). Reference **arrays** compare
by *identity*, not elementwise — a deliberate divergence noted in
`docs/TEMEN_RUNTIME_BUILTINS.md`.

## The error flag

Numbers (like all `JaclVal`s) carry three flag bits above the 5-bit type (bits 61–63):
`ERROR`, `SECRET`, `TAINTED`. Arithmetic is **error-propagating**: `ERR_IF_ERR` short-
circuits at the top of each op, so an error-flagged operand returns immediately, and
`prop_flags` carries `SECRET`/`TAINTED` from the operands onto the result. This is the
NaN-like propagation model — a bad value poisons the computations that consume it rather
than trapping, and surfaces at `print` as `<error: PAYLOAD>`.

## Printing

`repr_val` (behind `print` / `to-string`) formats each width directly:

- i32 via a small itoa; wide i64/u64 via a 64-bit itoa over `jacl_wide_bits`.
- f32 and f64 share one formatter (`repr_f32` on the f64 value), so both render with the
  same fixed-precision style; `nan` / `inf` are spelled out.
- The wide path prints the true 64-bit magnitude, which is what makes the overflow-
  promoted results (`6000000000`, not `1705032704`) match the old VM's output.

## Where this bites in parity

The overflow-promotion rule is the single change that fixed the wide-i64 truncation across
the typed-closure and typed-stream clusters — those tests multiply a small i32 by a large
constant and expect the full 64-bit product. The GC-livelock constraint is why the earlier
*unconditional*-widen attempt (box every i32 arithmetic result as i64) had to be reverted:
it regressed several GC-stress tests to hangs. The current "promote only on overflow" rule
threads both needles — correct wide results, zero extra allocation on the common path. See
`docs/TEMEN_PARITY_NOTES.md` for the slice history.
