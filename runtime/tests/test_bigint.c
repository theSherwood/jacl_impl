/* jacl #121 — big-by-big division (Knuth TAOCP 4.3.1 algorithm D).
 *
 * Until this landed, `/` and `%` were the only arithmetic that did not work across the whole
 * tower: a divisor wider than one limb was a domain error. This exercises the new path
 * through the *public* builtins (jacl_div / jacl_mod), so it covers the routing in
 * builtins.c as well as the algorithm.
 *
 * Three kinds of check, because each catches what the others miss:
 *
 *  1. **Add-back vectors.** The `top < 0` branch in jbig_mag_divmod fires for roughly 2 in
 *     2^32 quotient digits, so random testing will never reach it — 400k random pairs hit it
 *     zero times during development. These two are constructed for it: the divisor's *low*
 *     limb is what makes the estimate too large, and the two-limb estimate cannot see it
 *     (v[1] == 0 and the matching dividend limb is 0, so D3's correction loop does nothing).
 *     Verified non-vacuous: with the add-back correction disabled, exactly these two fail
 *     and every other case in this file still passes.
 *  2. **A round-trip property** over varied magnitudes: `q * b + r == a` and `|r| < |b|`.
 *     Normalization bugs show up here and nowhere else, because they produce a quotient that
 *     is merely *wrong* rather than obviously wrong.
 *  3. **The sign matrix.** Truncation toward zero, remainder takes the dividend's sign —
 *     matching the i32 and i64 tiers. Algorithm D works on magnitudes, so every sign
 *     question is settled in jacl_big_divmod, and only a test says it was settled right.
 *
 * Section 5 covers jacl #138, a separate bug this file's operands make easy to reach: a
 * bigint has no int64 form, but `jacl_is_anyint` admits one while `jacl_int_val` does not
 * handle it, so three callers read the JaclBig header as a number.
 *
 * Section 6 covers jacl #119's raw-`u64` crossings, which land on bigint for the same reason:
 * the dynamic tower is signed, so a u64 above `INT64_MAX` has no i64 form. It also **pins
 * today's answers** for the `0x0F` "wide but unsigned" tag, some of which are wrong — that
 * tag is on its way out (step 3), and a test that changes when it goes is the difference
 * between the tag being gone and the tag merely being unused.
 *
 * Returns 819. Any other value names what failed, so a red run says where to look without
 * a rebuild: 101-106 are the six sections below, 201-206 the individual checks inside the
 * property loop.
 */
#include "jaclrt.h"

static JaclVal big(const char *s) {
  uint32_t n = 0;
  while (s[n]) n++;
  return jacl_big_from_decimal(jacl_str_new(s, n));
}

/* Negation as `0 - v`, which is what codegen emits for unary minus and the only form that
 * works at every width — the runtime's jacl_neg is i32-only (see jacl #130). */
static JaclVal negv(JaclVal v) { return jacl_sub(jaclrt_i32(0), v); }

/* |v|, for the |r| < |b| check. */
static JaclVal absv(JaclVal v) {
  return jaclrt_as_bool(jacl_lt(v, jaclrt_i32(0))) ? negv(v) : v;
}

int run(int n) {
  (void)n;
  jacl_heap_init();
  jacl_intern_init();
  jacl_map_init();
  int ok = 1;

  /* ---- 1. the add-back branch ---- */
  {
    /* u = 0x80000000_00000000_FFFFFFFE, v = the same with a low limb one larger. */
    JaclVal u = big("39614081257132168801066942462");
    JaclVal v = big("39614081257132168801066942463");
    ok &= jacl_val_equal(jacl_div(u, v), jaclrt_i32(0));
    ok &= jacl_val_equal(jacl_mod(u, v), u);

    /* The same pattern one limb up, so the digit that needs the add-back is not the last. */
    JaclVal u2 = big("170141183460469231750134047781003722752");
    ok &= jacl_val_equal(jacl_div(u2, v), big("4294967295"));
    ok &= jacl_val_equal(jacl_mod(u2, v), big("39614081257132168796771975167"));
  }

  if (!ok) return 101;

  /* ---- 2. q * b + r == a, and |r| < |b| ---- */
  {
    /* Operands are built by multiplying through the tower rather than parsed, so the values
     * under test are the ones ordinary arithmetic produces. xorshift, so the set is fixed
     * across runs — a flaky property test is worse than none. */
    uint64_t st = 0x243F6A8885A308D3ull;
    for (int i = 0; i < 4000; i++) {
      st ^= st << 13; st ^= st >> 7; st ^= st << 17;
      int wa = 2 + (int)(st % 4);
      st ^= st << 13; st ^= st >> 7; st ^= st << 17;
      int wb = 1 + (int)(st % 3);
      JaclVal a = jaclrt_i32(1), b = jaclrt_i32(1);
      for (int k = 0; k < wa; k++) {
        st ^= st << 13; st ^= st >> 7; st ^= st << 17;
        a = jacl_mul(a, jaclrt_i32((int32_t)(st | 1u)));
      }
      for (int k = 0; k < wb; k++) {
        st ^= st << 13; st ^= st >> 7; st ^= st << 17;
        b = jacl_mul(b, jaclrt_i32((int32_t)(st | 1u)));
      }
      /* Every third divisor is scaled down so its top limb has leading zeros. Without this
       * the divisor is almost always already normalized, and the whole D1/D8 shift is dead
       * code the property never touches — checked by disabling normalization and watching
       * this loop keep passing, which is how the first version of it was wrong. */
      if (i % 3 == 0) {
        st ^= st << 13; st ^= st >> 7; st ^= st << 17;
        b = jacl_div(b, jaclrt_i32((int32_t)(1 + (st % 0xFFFFFu))));
      }
      st ^= st << 13; st ^= st >> 7; st ^= st << 17;
      if (st & 1) a = negv(a);
      if (st & 2) b = negv(b);
      if (jacl_val_equal(b, jaclrt_i32(0))) continue;
      if (jaclrt_is_error(a) || jaclrt_is_error(b)) { return 201; }

      JaclVal q = jacl_div(a, b);
      JaclVal r = jacl_mod(a, b);
      if (jaclrt_is_error(q)) { return 202; }
      if (jaclrt_is_error(r)) { return 203; }
      if (!jacl_val_equal(jacl_add(jacl_mul(q, b), r), a)) { return 204; }
      if (!jaclrt_as_bool(jacl_lt(absv(r), absv(b)))) { return 205; }
      /* The remainder takes the dividend's sign (or is zero). */
      if (!jacl_val_equal(r, jaclrt_i32(0))) {
        int rneg = jaclrt_as_bool(jacl_lt(r, jaclrt_i32(0)));
        int aneg = jaclrt_as_bool(jacl_lt(a, jaclrt_i32(0)));
        if (rneg != aneg) { return 206; }
      }
    }
  }

  if (!ok) return 102;

  /* ---- 3. the sign matrix, on one multi-limb pair ---- */
  {
    JaclVal a = big("170141183460469231731687303715884105727");   /* 2^127 - 1 */
    JaclVal b = big("39614081257132168801066942463");
    JaclVal na = negv(a), nb = negv(b);
    JaclVal q = jacl_div(a, b), r = jacl_mod(a, b);
    ok &= jacl_val_equal(jacl_div(na, b), negv(q));
    ok &= jacl_val_equal(jacl_div(a, nb), negv(q));
    ok &= jacl_val_equal(jacl_div(na, nb), q);
    ok &= jacl_val_equal(jacl_mod(na, b), negv(r));   /* dividend's sign */
    ok &= jacl_val_equal(jacl_mod(a, nb), r);
    ok &= jacl_val_equal(jacl_mod(na, nb), negv(r));
    /* and the identity still holds in every quadrant */
    ok &= jacl_val_equal(jacl_add(jacl_mul(jacl_div(na, b), b), jacl_mod(na, b)), na);
    ok &= jacl_val_equal(jacl_add(jacl_mul(jacl_div(a, nb), nb), jacl_mod(a, nb)), a);
    ok &= jacl_val_equal(jacl_add(jacl_mul(jacl_div(na, nb), nb), jacl_mod(na, nb)), na);
  }

  if (!ok) return 103;

  /* ---- 4. the edges the general path has to keep ---- */
  {
    JaclVal b = big("39614081257132168801066942463");
    /* |a| < |b|: quotient 0, remainder a — algorithm D's precondition does not hold here. */
    ok &= jacl_val_equal(jacl_div(jaclrt_i32(5), b), jaclrt_i32(0));
    ok &= jacl_val_equal(jacl_mod(jaclrt_i32(5), b), jaclrt_i32(5));
    /* exact division canonicalizes back down out of the bigint tier */
    ok &= jacl_val_equal(jacl_div(jacl_mul(b, jaclrt_i32(7)), b), jaclrt_i32(7));
    ok &= jacl_val_equal(jacl_mod(jacl_mul(b, jaclrt_i32(7)), b), jaclrt_i32(0));
    ok &= jacl_val_equal(jacl_div(b, b), jaclrt_i32(1));
    /* the single-limb path is untouched */
    JaclVal big2 = jacl_mul(big("9223372036854775807"), jaclrt_i32(2));
    ok &= jacl_val_equal(jacl_div(big2, jaclrt_i32(2)), big("9223372036854775807"));
    /* divide by zero is still a domain error, at every width */
    ok &= jaclrt_is_error(jacl_div(b, jaclrt_i32(0)));
    ok &= jaclrt_is_error(jacl_mod(b, jaclrt_i32(0)));
  }

  if (!ok) return 104;

  /* ---- 5. a bigint has no int64 form, and nothing may pretend otherwise (jacl #138) ---- */
  {
    /* `jacl_is_anyint` admits a bigint but `jacl_int_val` does not handle one, so three
     * single-operand callers read the JaclBig header (`sign`, `n`) as an int64 and got a
     * plausible number: a 2-limb positive bigint reads as 8589934593. The binary arithmetic
     * ops were never affected - they route bigints to the bigint path first. */
    JaclVal b  = big("18446744073709551615");                 /* > INT64_MAX: fits no width */
    JaclVal nb = jacl_sub(jaclrt_i32(0), b);                  /* and its negation */
    JaclVal i64max = big("9223372036854775807");
    ok &= jacl_is_bigint(b) && jacl_is_bigint(nb);
    ok &= !jacl_is_bigint(i64max);                            /* canonical: this is an i64 */

    /* (a) the cast. `[to "i64" b]` answered 8589934593 rather than refusing - exactly the
     * truncation jacl #116 exists to forbid. `i32` refused, but only because the garbage
     * happened to exceed INT32_MAX: right answer, wrong reason. */
    ok &= jaclrt_is_error(jacl_to_cast(b,  jacl_str_new("i64", 3)));
    ok &= jaclrt_is_error(jacl_to_cast(b,  jacl_str_new("i32", 3)));
    ok &= jaclrt_is_error(jacl_to_cast(nb, jacl_str_new("i64", 3)));
    /* `u64` is the one width that *can* hold this one now — its range is the full
     * [0, UINT64_MAX] (jacl #119 steps 4-5), and a bigint in (INT64_MAX, UINT64_MAX] is
     * already its own canonical dynamic form, so the cast is a check rather than a
     * conversion. This is the refusal #138 had to leave in place; it is gone. */
    ok &= jacl_val_equal(jacl_to_cast(b, jacl_str_new("u64", 3)), b);
    ok &= jaclrt_is_error(jacl_to_cast(nb, jacl_str_new("u64", 3)));     /* negative: no u64 */
    ok &= jaclrt_is_error(jacl_to_cast(big("18446744073709551616"),      /* one past the end */
                                       jacl_str_new("u64", 3)));
    /* ...and the in-range path still converts, so the refusal is not blanket */
    ok &= jacl_val_equal(jacl_to_cast(i64max, jacl_str_new("i64", 3)), i64max);
    ok &= jacl_val_equal(jacl_to_cast(jaclrt_i32(7), jacl_str_new("i32", 3)), jaclrt_i32(7));
    ok &= jaclrt_is_error(jacl_to_cast(i64max, jacl_str_new("i32", 3)));   /* genuinely too big */

    /* (b) the wrapping ops. Wrapping modulo 2^64 is not defined for a bigint; it used to
     * return the header arithmetic (`[+% b 1]` = 8589934594). */
    ok &= jaclrt_is_error(jacl_wrap_add(b, jaclrt_i32(1)));
    ok &= jaclrt_is_error(jacl_wrap_sub(b, jaclrt_i32(1)));
    ok &= jaclrt_is_error(jacl_wrap_mul(b, jaclrt_i32(2)));
    ok &= jaclrt_is_error(jacl_wrap_add(jaclrt_i32(1), b));   /* either operand */
    /* ...and wrapping still wraps where it is defined */
    ok &= jacl_val_equal(jacl_wrap_add(jaclrt_i32(2000000000), jaclrt_i32(2000000000)),
                         jaclrt_i32(-294967296));
    ok &= jacl_val_equal(jacl_wrap_add(i64max, jaclrt_i32(1)),
                         big("-9223372036854775808"));         /* wide pair still wraps */

    /* (c) the u64 widening guard. `negative -> u64` is a domain error, and a negative *bigint*
     * used to slip past it: the old test read `sign = -1` and `n` together, which is positive
     * for any limb count, so the one case the guard is about was the one it let through.
     *
     * A `u64` widening is now *only* a range check — u64 has no representation of its own
     * (jacl #119 step 3), so there is nothing left for it to widen *into*. The bigint half of
     * its range passes through unchanged. */
    JaclVal u64k = jaclrt_i32(0x0F), i64k = jaclrt_i32(0x0E);
    ok &= jaclrt_is_error(jacl_widen_to(nb, u64k));
    ok &= jacl_val_equal(jacl_widen_to(b, u64k), b);           /* in range, already canonical */
    ok &= jaclrt_is_error(jacl_widen_to(big("18446744073709551616"), u64k));   /* one past */
    ok &= jaclrt_is_error(jacl_widen_to(b,  i64k));            /* > INT64_MAX: no i64 cell */
    ok &= jaclrt_is_error(jacl_widen_to(jaclrt_i32(-5), u64k));  /* the original guard holds */
    /* ...and widening a value that does fit still widens */
    ok &= !jaclrt_is_error(jacl_widen_to(jaclrt_i32(5), u64k));
    ok &= !jaclrt_is_error(jacl_widen_to(i64max, i64k));
    if (!ok) return 105;
  }

  /* ---- 6. jacl #119: a raw u64 crossing into the signed tower ---- */
  {
    int ok = 1;
    JaclVal i64max = big("9223372036854775807");
    /* (a) `jacl_big_from_u64` covers the whole [0, UINT64_MAX] range and canonicalizes: below
     * INT64_MAX it demotes, so a u64 and an i64 spelling of the same number are the same
     * JaclVal (#107), and above it there is a bigint because there is nothing else. */
    ok &= jacl_val_equal(jacl_big_from_u64(0u), jaclrt_i32(0));
    ok &= jacl_val_equal(jacl_big_from_u64(37u), jaclrt_i32(37));
    ok &= !jacl_is_bigint(jacl_big_from_u64(37u));           /* demoted, not a 1-limb bigint */
    ok &= jacl_val_equal(jacl_big_from_u64((uint64_t)INT64_MAX), i64max);
    ok &= !jacl_is_bigint(jacl_big_from_u64((uint64_t)INT64_MAX));
    ok &= jacl_val_equal(jacl_big_from_u64((uint64_t)INT64_MAX + 1u),
                         big("9223372036854775808"));
    ok &= jacl_is_bigint(jacl_big_from_u64((uint64_t)INT64_MAX + 1u));
    ok &= jacl_val_equal(jacl_big_from_u64(UINT64_MAX), big("18446744073709551615"));
    if (!ok) return 107;

    /* (b) the round trip, and the two shapes that have no u64 form. */
    uint64_t out = 0;
    ok &= jacl_big_to_u64(big("18446744073709551615"), &out) && out == UINT64_MAX;
    ok &= jacl_big_to_u64(big("9223372036854775808"), &out) &&
          out == (uint64_t)INT64_MAX + 1u;
    ok &= !jacl_big_to_u64(big("-9223372036854775809"), &out);   /* negative */
    ok &= !jacl_big_to_u64(big("18446744073709551616"), &out);   /* one past UINT64_MAX */
    ok &= !jacl_big_to_u64(jaclrt_i32(5), &out);                 /* not a bigint at all */
    if (!ok) return 108;

    /* (c) `jacl_u64_box` / `jacl_u64_unbox`, the pair codegen emits at the raw boundary. */
    ok &= jacl_val_equal(jacl_u64_box(37), jaclrt_i32(37));
    ok &= jacl_val_equal(jacl_u64_box(INT64_MAX), i64max);
    ok &= jacl_val_equal(jacl_u64_box((int64_t)((uint64_t)INT64_MAX + 1u)),
                         big("9223372036854775808"));
    ok &= jacl_val_equal(jacl_u64_box(-1), big("18446744073709551615"));  /* read as unsigned */
    ok &= jacl_u64_unbox(jaclrt_i32(37)) == 37;
    ok &= jacl_u64_unbox(jaclrt_i32(-1)) == 0;                   /* no u64 form */
    ok &= jacl_u64_unbox(i64max) == INT64_MAX;
    ok &= (uint64_t)jacl_u64_unbox(big("18446744073709551615")) == UINT64_MAX;
    ok &= jacl_u64_unbox(big("-9223372036854775809")) == 0;      /* no u64 form */
    /* A tainted or secret value has nowhere to put its flag in an untagged word, so it is
     * refused rather than laundered into a plain number (jacl #95) — the same guard
     * `jacl_i64_unbox` has, and for the same reason. (The *error* flag is not tested here:
     * codegen branches on all three before it calls this at all, so this is the second line
     * of defence for the two flags a value can carry without being an error.) */
    ok &= jacl_u64_unbox(jaclrt_i32(5) | JACL_FLAG_TAINTED) == 0;
    ok &= jacl_u64_unbox(jaclrt_i32(5) | JACL_FLAG_SECRET) == 0;
    /* and it is the inverse of the box across the whole range */
    { uint64_t vs[] = {0u, 1u, 37u, (uint64_t)INT32_MAX, (uint64_t)INT32_MAX + 1u,
                       (uint64_t)INT64_MAX, (uint64_t)INT64_MAX + 1u, UINT64_MAX};
      for (unsigned i = 0; i < sizeof vs / sizeof vs[0]; i++)
        ok &= (uint64_t)jacl_u64_unbox(jacl_u64_box((int64_t)vs[i])) == vs[i]; }
    if (!ok) return 109;

    /* (d) the carry-out check the emitted code cannot do inline. */
    ok &= jacl_u64_mul_ovf(0, 0) == 0;
    ok &= jacl_u64_mul_ovf((int64_t)UINT32_MAX, (int64_t)UINT32_MAX) == 0;   /* fits */
    ok &= jacl_u64_mul_ovf(INT64_MAX, 2) == 0;                               /* fits unsigned */
    ok &= jacl_u64_mul_ovf(-1, 2) == 1;                                      /* UINT64_MAX * 2 */
    ok &= jacl_u64_mul_ovf((int64_t)1 << 32, (int64_t)1 << 32) == 1;
    if (!ok) return 110;

    /* (e) **the `0x0F` tag is gone** (jacl #119 step 3). This block used to pin its wrong
     * answers: the tag recorded that a value was meant to be unsigned and no operation
     * honoured it — every read went through `jacl_int_val` into an `int64_t` and used the
     * signed op — so `/ % < >` answered wrongly above `INT64_MAX` while the representation
     * and the printer were right across the whole range. Those values were unreachable from
     * JACL source (the range caps stopped at `INT64_MAX`) but reachable from `flatbuf.c`,
     * which minted one for a raw machine address.
     *
     * With signedness moved entirely to the static side, the tower is purely signed and the
     * same values are a **bigint**, which computes correctly. The pin is now the other way
     * round: it asserts the answers rather than the bug, and that no wide cell carries the
     * old tag. */
    JaclVal umax = jacl_u64_box((int64_t)UINT64_MAX);
    JaclVal nb   = jacl_sub(jaclrt_i32(0), umax);              /* its negation */
    JaclVal ten  = jaclrt_i32(10);
    ok &= jaclrt_type_index(umax) != 0x0F;                     /* the tag, gone */
    ok &= jacl_is_bigint(umax);                                /* a signed tower has one form */
    ok &= jacl_val_equal(umax, big("18446744073709551615"));
    ok &= jacl_val_equal(jacl_add(umax, jaclrt_i32(0)), umax);
    ok &= jacl_val_equal(jacl_div(umax, ten), big("1844674407370955161"));
    ok &= jacl_val_equal(jacl_mod(umax, ten), jaclrt_i32(5));
    ok &= jacl_val_equal(jacl_lt(umax, ten), jaclrt_bool(0));
    ok &= jacl_val_equal(jacl_gt(umax, ten), jaclrt_bool(1));
    /* nothing in the tower produces a 0x0F any more, including the ops that used to
     * propagate it */
    ok &= jaclrt_type_index(jacl_add(umax, jaclrt_i32(0))) != 0x0F;
    ok &= jaclrt_type_index(jacl_widen_to(jaclrt_i32(5), jaclrt_i32(0x0F))) != 0x0F;
    ok &= jaclrt_type_index(jacl_to_cast(jaclrt_i32(5), jacl_str_new("u64", 3))) != 0x0F;
    /* `+% -% *%` promote now rather than erroring: `u64` was the tower's one overflow
     * exception, and it was the tag that forced it. */
    ok &= jacl_val_equal(jacl_add(i64max, i64max), big("18446744073709551614"));
    /* and `assert-type u64` is a range question, the only one a signed tower can answer:
     * it passes the value through, or errors. */
    ok &= jacl_val_equal(jacl_assert_type(umax, jacl_str_new("u64", 3)), umax);
    ok &= jacl_val_equal(jacl_assert_type(i64max, jacl_str_new("u64", 3)), i64max);
    ok &= jaclrt_is_error(jacl_assert_type(jaclrt_i32(-1), jacl_str_new("u64", 3)));
    ok &= jaclrt_is_error(jacl_assert_type(nb, jacl_str_new("u64", 3)));
    if (!ok) return 111;
  }

  /* ---- 7. jacl #94 item 1: `INT_MIN / -1` promotes, at both widths ---- */
  {
    int ok = 1;
    /* The i32 branch of `jacl_div` used to return a wrapped `INT32_MIN` here, with a comment
     * saying it was avoiding the C undefined behaviour — which it was, but by wrapping, which
     * the integer model forbids for `dyn`: these operands' width is not declared, so the rule
     * is promote, never wrap. `jacl_mul` on the same numbers already promoted, so the two
     * operators disagreed about one number. */
    JaclVal i32min = jaclrt_i32(INT32_MIN), mone = jaclrt_i32(-1);
    ok &= jacl_val_equal(jacl_div(i32min, mone), big("2147483648"));
    ok &= jacl_val_equal(jacl_div(i32min, mone), jacl_mul(i32min, mone));   /* they agree now */
    ok &= jacl_val_equal(jacl_mod(i32min, mone), jaclrt_i32(0));            /* exact; unchanged */
    /* the answer canonicalizes back down when it fits again, so it is a *value*, not a tier */
    ok &= jacl_val_equal(jacl_div(jacl_div(i32min, mone), jaclrt_i32(2)), jaclrt_i32(1073741824));
    /* the 64-bit sibling was already right (#104) — pinned here so the pair stays together */
    JaclVal i64min = big("-9223372036854775808");
    ok &= jacl_val_equal(jacl_div(i64min, mone), big("9223372036854775808"));
    ok &= jacl_val_equal(jacl_mod(i64min, mone), jaclrt_i32(0));
    /* a zero divisor stays a domain error at every width */
    ok &= jaclrt_is_error(jacl_div(i32min, jaclrt_i32(0)));
    ok &= jaclrt_is_error(jacl_mod(i64min, jaclrt_i32(0)));
    if (!ok) return 112;
  }

  return ok ? 820 : 106;
}

#include "jaclrt.c"
