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
 * Returns 812. Any other value names what failed, so a red run says where to look without
 * a rebuild: 101-105 are the five sections below, 201-206 the individual checks inside the
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
    ok &= jaclrt_is_error(jacl_to_cast(b,  jacl_str_new("u64", 3)));
    ok &= jaclrt_is_error(jacl_to_cast(nb, jacl_str_new("i64", 3)));
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
     * for any limb count, so the one case the guard is about was the one it let through. */
    JaclVal u64k = jaclrt_i32(0x0F), i64k = jaclrt_i32(0x0E);
    ok &= jaclrt_is_error(jacl_widen_to(nb, u64k));
    ok &= jaclrt_is_error(jacl_widen_to(b,  u64k));            /* > INT64_MAX: no 64-bit cell */
    ok &= jaclrt_is_error(jacl_widen_to(b,  i64k));
    ok &= jaclrt_is_error(jacl_widen_to(jaclrt_i32(-5), u64k));  /* the original guard holds */
    /* ...and widening a value that does fit still widens */
    ok &= !jaclrt_is_error(jacl_widen_to(jaclrt_i32(5), u64k));
    ok &= !jaclrt_is_error(jacl_widen_to(i64max, i64k));
    if (!ok) return 105;
  }

  return ok ? 812 : 106;
}

#include "jaclrt.c"
