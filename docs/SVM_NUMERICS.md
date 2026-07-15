# The SVM backend numeric model

How JACL numbers are represented, promoted, compared, and printed by the SVM runtime
(`runtime/builtins.c`). This is the behaviour the codegen relies on when it lowers an
arithmetic head to a `jacl_*` call, and the reference for anyone touching the numeric
entry points. Everything here is about the *runtime* tower; the typer's static widths
(`i32`, `u64`, `f64`, …) drive which values flow in, but the runtime tower below is what
actually executes.

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

## Comparison and equality

`jacl_lt` / `le` / `gt` / `ge` compare numerically across widths: two ints compare as
64-bit integers (`jacl_int_val`), anything involving a float compares as f64
(`jacl_num_f64`). So `[< 5 6000000000]` is `true` even though the two sides have different
representations.

`jacl_val_equal` (behind `==` / `!=`) is structural and width-agnostic for numbers: an
i32 `5` equals an i64 `5`. For non-numbers it is type + payload bitwise, except strings
(by content) and vectors / maps / structs (deep, recursive). Reference **arrays** compare
by *identity*, not elementwise — a deliberate divergence noted in
`docs/SVM_RUNTIME_BUILTINS.md`.

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
`docs/SVM_PARITY_NOTES.md` for the slice history.
