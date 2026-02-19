/*
 * test_bigfloat.c -- Tests for BigFloat representation and construction
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

#include "../../test/test_helpers.h"

/* Wire up tracked allocator */
#define BIGINT_MALLOC(sz) tracked_malloc(sz)
#define BIGINT_FREE(p)    tracked_free(p)
#include "bigfloat.h"

/* --- Tests: bigfloat_zero --- */

static int test_bf_zero() {
    tracker_reset();

    BigFloat z = bigfloat_zero(53);
    ASSERT_U32_EQ(z.sign, 0);
    ASSERT_U32_EQ(z.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(z.precision, 53);
    ASSERT(bigint_is_zero(&z.significand));

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_zero_various_precisions() {
    tracker_reset();

    BigFloat z2 = bigfloat_zero(2);
    ASSERT_U32_EQ(z2.precision, 2);
    ASSERT_U32_EQ(z2.flags, BIGFLOAT_ZERO);

    BigFloat z256 = bigfloat_zero(256);
    ASSERT_U32_EQ(z256.precision, 256);
    ASSERT_U32_EQ(z256.flags, BIGFLOAT_ZERO);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_inf --- */

static int test_bf_inf_positive() {
    tracker_reset();

    BigFloat f = bigfloat_inf(0);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_INF);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_inf_negative() {
    tracker_reset();

    BigFloat f = bigfloat_inf(1);
    ASSERT_U32_EQ(f.sign, 1);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_INF);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_nan --- */

static int test_bf_nan() {
    tracker_reset();

    BigFloat f = bigfloat_nan();
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NAN);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_from_i64 --- */

static int test_bf_from_i64_zero() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 0);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT_U32_EQ(f.precision, 53);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_i64_one() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 1);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT_U32_EQ(f.precision, 53);

    /* 1 normalized to 53 bits: significand has bit_len == 53 */
    /* significand = 1 << 52 = 2^52, exponent = -52 */
    /* value = 2^52 * 2^(-52) = 1 */
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);
    ASSERT_I64_EQ(f.exponent, -52);

    /* Verify: the significand should be 2^52 */
    ASSERT(bigint_test_bit(&f.significand, 52));  /* bit 52 is set (leading bit) */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_i64_negative() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, -1);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 1);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);
    ASSERT_I64_EQ(f.exponent, -52);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_i64_power_of_two() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 256 = 2^8, bit_len = 9 */
    BigFloat f = bigfloat_from_i64(&arena, &ctx, 256);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);

    /* 256 = 2^8. Normalized: significand = 2^52, exponent = 8 - 52 = -44 */
    /* value = 2^52 * 2^(-44) = 2^8 = 256 */
    ASSERT_I64_EQ(f.exponent, -44);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_i64_large() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* INT64_MAX = 2^63 - 1, bit_len = 63 */
    BigFloat f = bigfloat_from_i64(&arena, &ctx, INT64_MAX);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);

    /* 63 bits -> 53 bits: shift right by 10. But all lower 10 bits are 1,
     * so rounding (nearest even) rounds up. The shifted value (all 53 bits set)
     * + 1 = 2^53, which overflows to 54 bits, causing another right shift.
     * Final exponent = 0 + 10 + 1 = 11. */
    ASSERT_I64_EQ(f.exponent, 11);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_i64_int64_min() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* INT64_MIN = -2^63 */
    BigFloat f = bigfloat_from_i64(&arena, &ctx, INT64_MIN);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 1);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);

    /* |INT64_MIN| = 2^63, bit_len = 64. Shift right 11 to get 53 bits.
     * But 2^63 has exactly 1 bit set, so shifting right by 11 gives 2^52.
     * exponent = 0 + 11 = 11 */
    ASSERT_I64_EQ(f.exponent, 11);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_i64_small_precision() {
    tracker_reset();

    /* Test with precision = 8 and value = 255 (bit_len = 8, exact fit) */
    BigFloatCtx ctx = { .precision = 8, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 255);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 8);
    ASSERT_I64_EQ(f.exponent, 0);  /* 255 has 8 bits, precision = 8, no shift needed */

    /* Verify significand is exactly 255 */
    const uint64_t* limbs = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(limbs[0], 255);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_i64_precision_2() {
    tracker_reset();

    /* Minimum precision = 2 */
    BigFloatCtx ctx = { .precision = 2, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 3 = 0b11, bit_len = 2, exact fit at precision 2 */
    BigFloat f = bigfloat_from_i64(&arena, &ctx, 3);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 2);
    ASSERT_I64_EQ(f.exponent, 0);

    /* 5 = 0b101, bit_len = 3, needs rounding to 2 bits */
    BigFloat g = bigfloat_from_i64(&arena, &ctx, 5);
    ASSERT_U32_EQ(g.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&g.significand), 2);

    /* 5 = 101. Shift right 1: guard=1, round=0, sticky=0. Tie -> even.
     * shifted = 10 (2). LSB=0, so no round up. Result = 10, exponent = 1.
     * value = 2 * 2^1 = 4 */
    ASSERT_I64_EQ(g.exponent, 1);
    const uint64_t* gl = bigint_limbs(&g.significand);
    ASSERT_U64_EQ(gl[0], 2);  /* 0b10 */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_from_bigint --- */

static int test_bf_from_bigint_zero() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigInt z = bigint_zero();
    BigFloat f = bigfloat_from_bigint(&arena, &ctx, &z);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(f.sign, 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_bigint_small() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigInt v = bigint_from_i64(42);
    BigFloat f = bigfloat_from_bigint(&arena, &ctx, &v);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);

    /* 42 = 101010, bit_len = 6. Shift left 47 to get 53 bits. exponent = -47. */
    ASSERT_I64_EQ(f.exponent, -47);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_bigint_negative() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigInt v = bigint_from_i64(-100);
    BigFloat f = bigfloat_from_bigint(&arena, &ctx, &v);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 1);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_bigint_large() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* Build a 3-limb value (> 128 bits) and convert to BigFloat */
    BigInt v = _bigint_alloc(&arena, 3);
    uint64_t* vl = _bigint_limbs_mut(&v);
    vl[0] = 0xFFFFFFFFFFFFFFFFULL;
    vl[1] = 0xFFFFFFFFFFFFFFFFULL;
    vl[2] = 1;

    BigFloat f = bigfloat_from_bigint(&arena, &ctx, &v);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);

    /* 3 limbs, top limb = 1, so bit_len = 129. Shift right by 76.
     * But the top 53 bits are all 1s, so rounding up causes overflow
     * to 54 bits, adding another right shift. exponent = 0 + 76 + 1 = 77 */
    ASSERT_I64_EQ(f.exponent, 77);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_bigint_exact_precision() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 64, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* UINT64_MAX has bit_len = 64, which matches precision exactly */
    BigInt v = bigint_from_u64(UINT64_MAX);
    BigFloat f = bigfloat_from_bigint(&arena, &ctx, &v);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 64);
    ASSERT_I64_EQ(f.exponent, 0);

    /* Significand should be exactly UINT64_MAX */
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], UINT64_MAX);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: normalization --- */

static int test_bf_normalized_bit_len() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* Test various values all produce precision-bit significands */
    int64_t values[] = {1, 2, 3, 7, 100, 1000, 65536, INT64_MAX, -1, -42, -INT64_MAX};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat f = bigfloat_from_i64(&arena, &ctx, values[i]);
        ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
        ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_value_reconstruction() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* For small values that fit in precision bits, we should be able to
     * reconstruct the original value from significand * 2^exponent */

    /* Test: 42 has bit_len = 6 <= 53, so it fits exactly */
    BigFloat f = bigfloat_from_i64(&arena, &ctx, 42);

    /* significand * 2^exponent should equal 42 */
    /* Since exponent is negative (shifted left to pad), shift right to recover */
    ASSERT(f.exponent < 0);
    BigInt recovered = bigint_shift_right(&arena, &f.significand, (uint32_t)(-f.exponent));
    const uint64_t* rl = bigint_limbs(&recovered);
    ASSERT_U64_EQ(rl[0], 42);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_value_reconstruction_256() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 256);
    ASSERT(f.exponent < 0);
    BigInt recovered = bigint_shift_right(&arena, &f.significand, (uint32_t)(-f.exponent));
    const uint64_t* rl = bigint_limbs(&recovered);
    ASSERT_U64_EQ(rl[0], 256);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: rounding --- */

static int test_bf_rounding_nearest_even() {
    tracker_reset();

    /* precision = 4. Value = 0b11001 = 25, bit_len = 5.
     * Shift right by 1: guard = 1, round_bit = 0, sticky = 0.
     * Tie -> round to even. shifted = 0b1100 = 12 (even), no round up.
     * Result: significand = 12, exponent = 1, value = 24 */
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 25);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 4);
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], 12);  /* 0b1100 */
    ASSERT_I64_EQ(f.exponent, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_rounding_nearest_even_round_up() {
    tracker_reset();

    /* precision = 4. Value = 0b11011 = 27, bit_len = 5.
     * Shift right by 1: guard = 1, round_bit = 0, sticky = 1 (bit 0 = 1).
     * guard=1, sticky=1 -> round up. shifted = 0b1101 + 1 = 0b1110 = 14.
     * Result: significand = 14, exponent = 1, value = 28 */
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 27);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 4);
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], 14);  /* 0b1110 */
    ASSERT_I64_EQ(f.exponent, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_rounding_toward_zero() {
    tracker_reset();

    /* precision = 4. Value = 0b11011 = 27, bit_len = 5.
     * Shift right by 1: guard = 1. Toward zero always truncates.
     * shifted = 0b1101 = 13.
     * Result: significand = 13, exponent = 1, value = 26 */
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_TOWARD_ZERO };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 27);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 4);
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], 13);  /* 0b1101 */
    ASSERT_I64_EQ(f.exponent, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_rounding_toward_pos_inf() {
    tracker_reset();

    /* precision = 4. Value = 27 (positive), bit_len = 5.
     * Shift right by 1: guard = 1. Toward +inf rounds up for positive.
     * shifted = 0b1101 + 1 = 0b1110 = 14. exponent = 1, value = 28 */
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_TOWARD_POS_INF };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 27);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 4);
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], 14);  /* 0b1110 */

    /* Negative -27: toward +inf truncates (rounds toward zero for negative) */
    BigFloat g = bigfloat_from_i64(&arena, &ctx, -27);
    ASSERT_U32_EQ(g.sign, 1);
    ASSERT_U32_EQ(bigint_bit_len(&g.significand), 4);
    const uint64_t* gl = bigint_limbs(&g.significand);
    ASSERT_U64_EQ(gl[0], 13);  /* truncated: 0b1101 */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_rounding_toward_neg_inf() {
    tracker_reset();

    /* precision = 4. Value = 27 (positive): toward -inf truncates for positive */
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_TOWARD_NEG_INF };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 27);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 4);
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], 13);  /* truncated: 0b1101 */

    /* Negative -27: toward -inf rounds away from zero for negative */
    BigFloat g = bigfloat_from_i64(&arena, &ctx, -27);
    ASSERT_U32_EQ(g.sign, 1);
    ASSERT_U32_EQ(bigint_bit_len(&g.significand), 4);
    const uint64_t* gl = bigint_limbs(&g.significand);
    ASSERT_U64_EQ(gl[0], 14);  /* rounded up: 0b1110 */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_from_bigint with rounding --- */

static int test_bf_from_bigint_rounding() {
    tracker_reset();

    /* Use a large bigint that requires rounding to precision */
    BigFloatCtx ctx = { .precision = 10, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 2047 = 0b11111111111, bit_len = 11, round to 10 bits */
    /* Shift right 1: guard = 1, round/sticky = 0. Tie -> even.
     * shifted = 0b1111111111 = 1023. LSB = 1 -> round up.
     * 1023 + 1 = 1024 = 0b10000000000 (11 bits). That's > precision!
     * So shift right 1 more: 512 = 0b1000000000 (10 bits). exponent = 2.
     * value = 512 * 4 = 2048 */
    BigInt v = bigint_from_u64(2047);
    BigFloat f = bigfloat_from_bigint(&arena, &ctx, &v);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 10);
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], 512);  /* 0b1000000000 */
    ASSERT_I64_EQ(f.exponent, 2);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: from_i64 with precision=53 (IEEE double) bit_len checks --- */

static int test_bf_from_i64_precision_53_one() {
    tracker_reset();

    /* From acceptance criteria: from_i64(ctx, 1) with precision=53 has bit_len=53 */
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 1);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 53);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: from_i64(ctx, 0) produces canonical zero --- */

static int test_bf_from_i64_canonical_zero() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 0);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(f.sign, 0);
    ASSERT(bigint_is_zero(&f.significand));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: large precision --- */

static int test_bf_from_i64_large_precision() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 42);
    ASSERT_U32_EQ(f.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 256);

    /* 42 bit_len = 6. Shift left by 250 to get 256 bits. exponent = -250. */
    ASSERT_I64_EQ(f.exponent, -250);

    /* Recover original value */
    BigInt recovered = bigint_shift_right(&arena, &f.significand, 250);
    const uint64_t* rl = bigint_limbs(&recovered);
    ASSERT_U64_EQ(rl[0], 42);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: rounding carry overflow --- */

static int test_bf_rounding_carry_overflow() {
    tracker_reset();

    /* precision = 3. Value = 0b1111 = 15, bit_len = 4.
     * Shift right 1: guard = 1, round/sticky = 0 (bit 0 = 1 -> sticky=0 since shift-2 < 0).
     * Wait: shift = 4 - 3 = 1. guard = bit(0) = 1. shift >= 2 is false. round_bit = 0. sticky = 0.
     * Tie -> even. shifted = 0b111 = 7. LSB = 1 -> round up. 7 + 1 = 8 = 0b1000 (4 bits > prec 3).
     * Re-shift: 8 >> 1 = 4 = 0b100 (3 bits). exponent = 0 + 1 + 1 = 2.
     * value = 4 * 4 = 16 */
    BigFloatCtx ctx = { .precision = 3, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f = bigfloat_from_i64(&arena, &ctx, 15);
    ASSERT_U32_EQ(bigint_bit_len(&f.significand), 3);
    const uint64_t* fl = bigint_limbs(&f.significand);
    ASSERT_U64_EQ(fl[0], 4);  /* 0b100 */
    ASSERT_I64_EQ(f.exponent, 2);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_is_zero / bigfloat_is_inf / bigfloat_is_nan --- */

static int test_bf_predicates() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat z = bigfloat_zero(53);
    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);
    BigFloat nan_val = bigfloat_nan();
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);

    /* is_zero */
    ASSERT(bigfloat_is_zero(&z));
    ASSERT(!bigfloat_is_zero(&pi));
    ASSERT(!bigfloat_is_zero(&ni));
    ASSERT(!bigfloat_is_zero(&nan_val));
    ASSERT(!bigfloat_is_zero(&one));

    /* is_inf */
    ASSERT(!bigfloat_is_inf(&z));
    ASSERT(bigfloat_is_inf(&pi));
    ASSERT(bigfloat_is_inf(&ni));
    ASSERT(!bigfloat_is_inf(&nan_val));
    ASSERT(!bigfloat_is_inf(&one));

    /* is_nan */
    ASSERT(!bigfloat_is_nan(&z));
    ASSERT(!bigfloat_is_nan(&pi));
    ASSERT(!bigfloat_is_nan(&ni));
    ASSERT(bigfloat_is_nan(&nan_val));
    ASSERT(!bigfloat_is_nan(&one));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_cmp --- */

static int test_bf_cmp_zeros() {
    tracker_reset();

    BigFloat pz = bigfloat_zero(53);
    BigFloat nz = bigfloat_neg(bigfloat_zero(53));

    /* -0 == +0 */
    ASSERT_INT_EQ(bigfloat_cmp(&pz, &pz), 0);
    ASSERT_INT_EQ(bigfloat_cmp(&nz, &nz), 0);
    ASSERT_INT_EQ(bigfloat_cmp(&pz, &nz), 0);
    ASSERT_INT_EQ(bigfloat_cmp(&nz, &pz), 0);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_infinities() {
    tracker_reset();

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);

    ASSERT_INT_EQ(bigfloat_cmp(&pi, &pi), 0);   /* +inf == +inf */
    ASSERT_INT_EQ(bigfloat_cmp(&ni, &ni), 0);   /* -inf == -inf */
    ASSERT_INT_EQ(bigfloat_cmp(&ni, &pi), -1);  /* -inf < +inf */
    ASSERT_INT_EQ(bigfloat_cmp(&pi, &ni), 1);   /* +inf > -inf */

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_inf_vs_normal() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);
    BigFloat pos = bigfloat_from_i64(&arena, &ctx, 1000);
    BigFloat neg = bigfloat_from_i64(&arena, &ctx, -1000);

    /* -inf < negative < positive < +inf */
    ASSERT_INT_EQ(bigfloat_cmp(&ni, &neg), -1);
    ASSERT_INT_EQ(bigfloat_cmp(&ni, &pos), -1);
    ASSERT_INT_EQ(bigfloat_cmp(&neg, &pi), -1);
    ASSERT_INT_EQ(bigfloat_cmp(&pos, &pi), -1);
    ASSERT_INT_EQ(bigfloat_cmp(&pi, &pos), 1);
    ASSERT_INT_EQ(bigfloat_cmp(&pi, &neg), 1);
    ASSERT_INT_EQ(bigfloat_cmp(&neg, &ni), 1);
    ASSERT_INT_EQ(bigfloat_cmp(&pos, &ni), 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_zero_vs_normal() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat z = bigfloat_zero(53);
    BigFloat pos = bigfloat_from_i64(&arena, &ctx, 42);
    BigFloat neg = bigfloat_from_i64(&arena, &ctx, -42);

    /* negative < 0 < positive */
    ASSERT_INT_EQ(bigfloat_cmp(&z, &pos), -1);
    ASSERT_INT_EQ(bigfloat_cmp(&pos, &z), 1);
    ASSERT_INT_EQ(bigfloat_cmp(&z, &neg), 1);
    ASSERT_INT_EQ(bigfloat_cmp(&neg, &z), -1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_same_sign_different_exp() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 1 and 256: both positive, different exponents */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 256);

    ASSERT_INT_EQ(bigfloat_cmp(&a, &b), -1);  /* 1 < 256 */
    ASSERT_INT_EQ(bigfloat_cmp(&b, &a), 1);   /* 256 > 1 */

    /* -1 and -256: both negative, different exponents */
    BigFloat c = bigfloat_from_i64(&arena, &ctx, -1);
    BigFloat d = bigfloat_from_i64(&arena, &ctx, -256);

    ASSERT_INT_EQ(bigfloat_cmp(&c, &d), 1);   /* -1 > -256 */
    ASSERT_INT_EQ(bigfloat_cmp(&d, &c), -1);  /* -256 < -1 */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_same_sign_same_exp() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 5 and 7: both have bit_len = 3, so same exponent after normalization */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 5);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 7);

    ASSERT_I64_EQ(a.exponent, b.exponent);  /* same exponent */
    ASSERT_INT_EQ(bigfloat_cmp(&a, &b), -1);  /* 5 < 7 */
    ASSERT_INT_EQ(bigfloat_cmp(&b, &a), 1);   /* 7 > 5 */

    /* Same value */
    BigFloat c = bigfloat_from_i64(&arena, &ctx, 42);
    BigFloat d = bigfloat_from_i64(&arena, &ctx, 42);
    ASSERT_INT_EQ(bigfloat_cmp(&c, &d), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_negative_normals() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat a = bigfloat_from_i64(&arena, &ctx, -5);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, -7);

    ASSERT_INT_EQ(bigfloat_cmp(&a, &b), 1);   /* -5 > -7 */
    ASSERT_INT_EQ(bigfloat_cmp(&b, &a), -1);  /* -7 < -5 */

    /* Same negative value */
    BigFloat c = bigfloat_from_i64(&arena, &ctx, -42);
    BigFloat d = bigfloat_from_i64(&arena, &ctx, -42);
    ASSERT_INT_EQ(bigfloat_cmp(&c, &d), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_different_signs() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pos = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat neg = bigfloat_from_i64(&arena, &ctx, -1);

    ASSERT_INT_EQ(bigfloat_cmp(&neg, &pos), -1);
    ASSERT_INT_EQ(bigfloat_cmp(&pos, &neg), 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_cmp_full_ordering() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* Verify full ordering: -inf < -100 < -1 < 0 < 1 < 100 < +inf */
    BigFloat values[7];
    values[0] = bigfloat_inf(1);                         /* -inf */
    values[1] = bigfloat_from_i64(&arena, &ctx, -100);   /* -100 */
    values[2] = bigfloat_from_i64(&arena, &ctx, -1);     /* -1 */
    values[3] = bigfloat_zero(53);                        /* 0 */
    values[4] = bigfloat_from_i64(&arena, &ctx, 1);      /* 1 */
    values[5] = bigfloat_from_i64(&arena, &ctx, 100);    /* 100 */
    values[6] = bigfloat_inf(0);                          /* +inf */

    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 7; j++) {
            int cmp = bigfloat_cmp(&values[i], &values[j]);
            if (i < j) ASSERT_INT_EQ(cmp, -1);
            else if (i > j) ASSERT_INT_EQ(cmp, 1);
            else ASSERT_INT_EQ(cmp, 0);
        }
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_neg --- */

static int test_bf_neg_normal() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pos = bigfloat_from_i64(&arena, &ctx, 42);
    BigFloat neg = bigfloat_neg(pos);
    ASSERT_U32_EQ(neg.sign, 1);
    ASSERT_U32_EQ(neg.flags, BIGFLOAT_NORMAL);
    ASSERT_I64_EQ(neg.exponent, pos.exponent);

    /* Neg of neg is positive */
    BigFloat pos2 = bigfloat_neg(neg);
    ASSERT_U32_EQ(pos2.sign, 0);
    ASSERT_INT_EQ(bigfloat_cmp(&pos, &pos2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_neg_zero() {
    tracker_reset();

    /* neg of positive zero returns negative zero */
    BigFloat pz = bigfloat_zero(53);
    BigFloat nz = bigfloat_neg(pz);
    ASSERT_U32_EQ(nz.sign, 1);
    ASSERT_U32_EQ(nz.flags, BIGFLOAT_ZERO);

    /* neg of negative zero returns positive zero */
    BigFloat pz2 = bigfloat_neg(nz);
    ASSERT_U32_EQ(pz2.sign, 0);
    ASSERT_U32_EQ(pz2.flags, BIGFLOAT_ZERO);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_neg_inf() {
    tracker_reset();

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_neg(pi);
    ASSERT_U32_EQ(ni.sign, 1);
    ASSERT_U32_EQ(ni.flags, BIGFLOAT_INF);

    BigFloat pi2 = bigfloat_neg(ni);
    ASSERT_U32_EQ(pi2.sign, 0);
    ASSERT_U32_EQ(pi2.flags, BIGFLOAT_INF);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_neg_nan() {
    tracker_reset();

    BigFloat nan_val = bigfloat_nan();
    BigFloat neg_nan = bigfloat_neg(nan_val);
    ASSERT_U32_EQ(neg_nan.flags, BIGFLOAT_NAN);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigfloat_abs --- */

static int test_bf_abs_positive() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pos = bigfloat_from_i64(&arena, &ctx, 42);
    BigFloat a = bigfloat_abs(pos);
    ASSERT_U32_EQ(a.sign, 0);
    ASSERT_INT_EQ(bigfloat_cmp(&pos, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_abs_negative() {
    tracker_reset();

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat neg = bigfloat_from_i64(&arena, &ctx, -42);
    BigFloat a = bigfloat_abs(neg);
    ASSERT_U32_EQ(a.sign, 0);
    ASSERT_U32_EQ(a.flags, BIGFLOAT_NORMAL);

    /* abs(-42) should equal 42 */
    BigFloat pos = bigfloat_from_i64(&arena, &ctx, 42);
    ASSERT_INT_EQ(bigfloat_cmp(&a, &pos), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_abs_zero() {
    tracker_reset();

    /* abs of -0 is +0 */
    BigFloat nz = bigfloat_neg(bigfloat_zero(53));
    ASSERT_U32_EQ(nz.sign, 1);  /* confirm it's -0 */

    BigFloat a = bigfloat_abs(nz);
    ASSERT_U32_EQ(a.sign, 0);
    ASSERT_U32_EQ(a.flags, BIGFLOAT_ZERO);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_abs_inf() {
    tracker_reset();

    /* abs of -inf returns +inf */
    BigFloat ni = bigfloat_inf(1);
    BigFloat a = bigfloat_abs(ni);
    ASSERT_U32_EQ(a.sign, 0);
    ASSERT_U32_EQ(a.flags, BIGFLOAT_INF);

    /* abs of +inf returns +inf */
    BigFloat pi = bigfloat_inf(0);
    BigFloat b = bigfloat_abs(pi);
    ASSERT_U32_EQ(b.sign, 0);
    ASSERT_U32_EQ(b.flags, BIGFLOAT_INF);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- US-012: BigFloat addition and subtraction --- */

static int test_bf_add_basic() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 1 + 1 = 2 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 2);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);

    /* 3 + 4 = 7 */
    a = bigfloat_from_i64(&arena, &ctx, 3);
    b = bigfloat_from_i64(&arena, &ctx, 4);
    r = bigfloat_add(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 7);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 100 + 200 = 300 */
    a = bigfloat_from_i64(&arena, &ctx, 100);
    b = bigfloat_from_i64(&arena, &ctx, 200);
    r = bigfloat_add(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 300);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_different_exponents() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 1 + 256 = 257 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 256);
    BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 257);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 256 + 1 = 257 (commutative) */
    r = bigfloat_add(&arena, &ctx, &b, &a);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 1 + 65536 = 65537 */
    a = bigfloat_from_i64(&arena, &ctx, 1);
    b = bigfloat_from_i64(&arena, &ctx, 65536);
    r = bigfloat_add(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 65537);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_same_sign_negative() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* (-3) + (-5) = -8 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, -3);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, -5);
    BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, -8);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_mixed_sign() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 5 + (-3) = 2 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 5);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, -3);
    BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 2);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(r.sign, 0);

    /* (-5) + 3 = -2 */
    a = bigfloat_from_i64(&arena, &ctx, -5);
    b = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_add(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, -2);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(r.sign, 1);

    /* 3 + (-5) = -2 */
    a = bigfloat_from_i64(&arena, &ctx, 3);
    b = bigfloat_from_i64(&arena, &ctx, -5);
    r = bigfloat_add(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, -2);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_exact_cancellation() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* x + (-x) = 0 for various x */
    int64_t values[] = {1, 42, 256, 1000000, -7, -100};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat a = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat b = bigfloat_from_i64(&arena, &ctx, -values[i]);
        BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
        ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
        ASSERT_U32_EQ(r.sign, 0); /* +0 for nearest-even */
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_nan() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat nan_val = bigfloat_nan();
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat z = bigfloat_zero(53);
    BigFloat pi = bigfloat_inf(0);

    /* NaN + x = NaN */
    ASSERT_U32_EQ(bigfloat_add(&arena, &ctx, &nan_val, &one).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_add(&arena, &ctx, &one, &nan_val).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_add(&arena, &ctx, &nan_val, &z).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_add(&arena, &ctx, &nan_val, &pi).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_add(&arena, &ctx, &nan_val, &nan_val).flags, BIGFLOAT_NAN);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_inf_same_sign() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);

    /* inf + inf = inf */
    BigFloat r = bigfloat_add(&arena, &ctx, &pi, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    /* (-inf) + (-inf) = -inf */
    r = bigfloat_add(&arena, &ctx, &ni, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_inf_opposite_sign() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);

    /* inf + (-inf) = NaN */
    BigFloat r = bigfloat_add(&arena, &ctx, &pi, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    /* (-inf) + inf = NaN */
    r = bigfloat_add(&arena, &ctx, &ni, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_inf_finite() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);

    /* inf + 1 = inf */
    BigFloat r = bigfloat_add(&arena, &ctx, &pi, &one);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    /* 1 + inf = inf */
    r = bigfloat_add(&arena, &ctx, &one, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    /* (-inf) + 1 = -inf */
    r = bigfloat_add(&arena, &ctx, &ni, &one);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_zero_identity() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat z = bigfloat_zero(53);

    /* x + 0 = x, 0 + x = x */
    int64_t values[] = {1, -1, 42, -42, 1000};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat r1 = bigfloat_add(&arena, &ctx, &x, &z);
        BigFloat r2 = bigfloat_add(&arena, &ctx, &z, &x);
        ASSERT_INT_EQ(bigfloat_cmp(&r1, &x), 0);
        ASSERT_INT_EQ(bigfloat_cmp(&r2, &x), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_zero_zero() {
    tracker_reset();
    arena_t arena = {0};

    BigFloat pz = bigfloat_zero(53);
    BigFloat nz = bigfloat_neg(bigfloat_zero(53));

    /* +0 + +0 = +0 */
    BigFloatCtx ctx_ne = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigFloat r = bigfloat_add(&arena, &ctx_ne, &pz, &pz);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 0);

    /* -0 + -0 = -0 */
    r = bigfloat_add(&arena, &ctx_ne, &nz, &nz);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 1);

    /* +0 + -0 = +0 (nearest even) */
    r = bigfloat_add(&arena, &ctx_ne, &pz, &nz);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 0);

    /* +0 + -0 = -0 (toward -inf) */
    BigFloatCtx ctx_ni = { .precision = 53, .round_mode = BIGFLOAT_ROUND_TOWARD_NEG_INF };
    r = bigfloat_add(&arena, &ctx_ni, &pz, &nz);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_epsilon_all_modes() {
    tracker_reset();
    arena_t arena = {0};

    /* precision 4: 1.0 = sig 8, exp -3. ULP = 2^(-3) = 0.125. ULP/2 = 0.0625.
     * epsilon = 1/32 = 0.03125 < ULP/2 */

    /* Construct epsilon: 1.0 with exponent shifted down by 5 → value = 2^(-5) = 1/32 */
    BigFloatCtx ctx;
    BigFloat one, eps, r;
    const uint64_t* rl;

    /* NEAREST_EVEN: 1.0 + eps = 1.0 (rounds down, epsilon < ULP/2) */
    ctx = (BigFloatCtx){ .precision = 4, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    one = bigfloat_from_i64(&arena, &ctx, 1);
    eps = bigfloat_from_i64(&arena, &ctx, 1);
    eps.exponent -= 5; /* value = 2^(-5) = 1/32 */
    r = bigfloat_add(&arena, &ctx, &one, &eps);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 4);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 8); /* sig = 8 = 0b1000 */
    ASSERT_I64_EQ(r.exponent, -3); /* value = 1.0 */

    /* TOWARD_ZERO: 1.0 + eps = 1.0 (truncate) */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_ZERO;
    r = bigfloat_add(&arena, &ctx, &one, &eps);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 8);
    ASSERT_I64_EQ(r.exponent, -3);

    /* TOWARD_POS_INF: 1.0 + eps = 1.125 (rounds up for positive) */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_POS_INF;
    one = bigfloat_from_i64(&arena, &ctx, 1);
    eps = bigfloat_from_i64(&arena, &ctx, 1);
    eps.exponent -= 5;
    r = bigfloat_add(&arena, &ctx, &one, &eps);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 9); /* sig = 9 = 0b1001, value = 9/8 = 1.125 */
    ASSERT_I64_EQ(r.exponent, -3);

    /* TOWARD_NEG_INF: 1.0 + eps = 1.0 (truncate for positive) */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_NEG_INF;
    one = bigfloat_from_i64(&arena, &ctx, 1);
    eps = bigfloat_from_i64(&arena, &ctx, 1);
    eps.exponent -= 5;
    r = bigfloat_add(&arena, &ctx, &one, &eps);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 8);
    ASSERT_I64_EQ(r.exponent, -3);

    /* Negative: -1.0 + (-eps) with TOWARD_NEG_INF: rounds away → -1.125 */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_NEG_INF;
    BigFloat neg_one = bigfloat_from_i64(&arena, &ctx, -1);
    BigFloat neg_eps = bigfloat_from_i64(&arena, &ctx, -1);
    neg_eps.exponent -= 5;
    r = bigfloat_add(&arena, &ctx, &neg_one, &neg_eps);
    ASSERT_U32_EQ(r.sign, 1);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 9); /* |sig| = 9, value = -1.125 */
    ASSERT_I64_EQ(r.exponent, -3);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_carry() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 15 + 1 = 16. At precision 4: 15 = 0b1111 (exact), 1 = sig 8, exp -3.
     * 15 has exp = 0, 1 has exp = -3. d = 3.
     * a_shifted = 15 << 3 = 120. sum = 120 + 8 = 128 = 0b10000000 (8 bits).
     * Normalize: shift right 4. 128>>4 = 8. guard=0. Result: sig=8, exp=1.
     * Value = 8 * 2 = 16. */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 15);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 16);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 4);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_massive_cancellation() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 8, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 255 + (-254) = 1. Both have 8 bits (exact at precision 8).
     * Result 1 needs shift left by 7 to normalize to 8 bits. */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 255);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, -254);
    BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 1);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 8);
    ASSERT_U32_EQ(r.sign, 0);

    /* At precision 53: 1000001 + (-1000000) = 1 */
    BigFloatCtx ctx53 = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    a = bigfloat_from_i64(&arena, &ctx53, 1000001);
    b = bigfloat_from_i64(&arena, &ctx53, -1000000);
    r = bigfloat_add(&arena, &ctx53, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx53, 1);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_result_precision() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* Various additions should all produce results with bit_len == precision */
    int64_t pairs[][2] = {
        {1, 1}, {1, 2}, {100, 200}, {-3, -5}, {5, -3}, {-5, 3},
        {1, 65536}, {12345, 67890}, {-1, 1000000}
    };
    int count = sizeof(pairs) / sizeof(pairs[0]);

    for (int i = 0; i < count; i++) {
        BigFloat a = bigfloat_from_i64(&arena, &ctx, pairs[i][0]);
        BigFloat b = bigfloat_from_i64(&arena, &ctx, pairs[i][1]);
        BigFloat r = bigfloat_add(&arena, &ctx, &a, &b);
        if (r.flags == BIGFLOAT_NORMAL) {
            ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);
        }
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_sub_basic() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 3 - 1 = 2 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 3);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat r = bigfloat_sub(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 2);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 10 - 3 = 7 */
    a = bigfloat_from_i64(&arena, &ctx, 10);
    b = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_sub(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 7);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 1 - 3 = -2 */
    a = bigfloat_from_i64(&arena, &ctx, 1);
    b = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_sub(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, -2);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_sub_to_zero() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* x - x = 0 for various x */
    int64_t values[] = {1, 42, 256, -7, -100, 1000000};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat a = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat r = bigfloat_sub(&arena, &ctx, &a, &a);
        ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
        ASSERT_U32_EQ(r.sign, 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_sub_inf() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);

    /* inf - inf = NaN */
    BigFloat r = bigfloat_sub(&arena, &ctx, &pi, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    /* inf - (-inf) = inf */
    r = bigfloat_sub(&arena, &ctx, &pi, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    /* (-inf) - inf = -inf */
    r = bigfloat_sub(&arena, &ctx, &ni, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    /* (-inf) - (-inf) = NaN */
    r = bigfloat_sub(&arena, &ctx, &ni, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    /* inf - 1 = inf */
    r = bigfloat_sub(&arena, &ctx, &pi, &one);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_sub_massive_cancellation() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* Subtract nearly equal values: result should be correctly normalized */
    /* 1048577 - 1048576 = 1 (2^20 + 1 - 2^20 = 1) */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1048577);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 1048576);
    BigFloat r = bigfloat_sub(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 1);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_add_rounding_tie() {
    tracker_reset();
    arena_t arena = {0};

    /* precision 4: 15 + 2 = 17. 17 = 0b10001 (5 bits).
     * Shift right 1: guard=1, round=0, sticky=0.
     * 8 (shifted) + tie → nearest even: LSB of 8 = 0 → no round. sig=8, exp=1. val=16.
     * But with TOWARD_POS_INF: round up → sig=9, exp=1. val=18. */
    BigFloatCtx ctx_ne = { .precision = 4, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigFloat a = bigfloat_from_i64(&arena, &ctx_ne, 15);
    BigFloat b = bigfloat_from_i64(&arena, &ctx_ne, 2);
    BigFloat r = bigfloat_add(&arena, &ctx_ne, &a, &b);
    const uint64_t* rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 8); /* 16, not 18 (tie to even) */
    ASSERT_I64_EQ(r.exponent, 1);

    /* With TOWARD_POS_INF: round up */
    BigFloatCtx ctx_pi = { .precision = 4, .round_mode = BIGFLOAT_ROUND_TOWARD_POS_INF };
    a = bigfloat_from_i64(&arena, &ctx_pi, 15);
    b = bigfloat_from_i64(&arena, &ctx_pi, 2);
    r = bigfloat_add(&arena, &ctx_pi, &a, &b);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 9); /* 18 */
    ASSERT_I64_EQ(r.exponent, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- US-013: BigFloat multiplication --- */

static int test_bf_mul_basic() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 3 * 4 = 12 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 3);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 4);
    BigFloat r = bigfloat_mul(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 12);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);

    /* 7 * 13 = 91 */
    a = bigfloat_from_i64(&arena, &ctx, 7);
    b = bigfloat_from_i64(&arena, &ctx, 13);
    r = bigfloat_mul(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 91);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 100 * 200 = 20000 */
    a = bigfloat_from_i64(&arena, &ctx, 100);
    b = bigfloat_from_i64(&arena, &ctx, 200);
    r = bigfloat_mul(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 20000);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_identity() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* x * 1 = x for various x */
    int64_t values[] = {1, -1, 42, -42, 256, 1000000};
    int count = sizeof(values) / sizeof(values[0]);
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat r = bigfloat_mul(&arena, &ctx, &x, &one);
        ASSERT_INT_EQ(bigfloat_cmp(&r, &x), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_zero() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat z = bigfloat_zero(53);
    BigFloat nz = bigfloat_neg(bigfloat_zero(53));

    /* x * 0 = 0, 0 * x = 0 */
    int64_t values[] = {1, -1, 42, -42, 1000000};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat r1 = bigfloat_mul(&arena, &ctx, &x, &z);
        BigFloat r2 = bigfloat_mul(&arena, &ctx, &z, &x);
        ASSERT_U32_EQ(r1.flags, BIGFLOAT_ZERO);
        ASSERT_U32_EQ(r2.flags, BIGFLOAT_ZERO);
    }

    /* Sign of zero: positive * +0 = +0, negative * +0 = -0 */
    BigFloat pos = bigfloat_from_i64(&arena, &ctx, 5);
    BigFloat neg = bigfloat_from_i64(&arena, &ctx, -5);
    BigFloat r = bigfloat_mul(&arena, &ctx, &pos, &z);
    ASSERT_U32_EQ(r.sign, 0);
    r = bigfloat_mul(&arena, &ctx, &neg, &z);
    ASSERT_U32_EQ(r.sign, 1);
    r = bigfloat_mul(&arena, &ctx, &neg, &nz);
    ASSERT_U32_EQ(r.sign, 0); /* neg * neg_zero = pos_zero */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_neg_one() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* x * (-1) = -x */
    BigFloat neg_one = bigfloat_from_i64(&arena, &ctx, -1);
    int64_t values[] = {1, 42, -42, 256, -256};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat r = bigfloat_mul(&arena, &ctx, &x, &neg_one);
        BigFloat expected = bigfloat_neg(x);
        ASSERT_INT_EQ(bigfloat_cmp(&r, &expected), 0);
        ASSERT_U32_EQ(r.sign, expected.sign);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_sign() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pos3 = bigfloat_from_i64(&arena, &ctx, 3);
    BigFloat neg5 = bigfloat_from_i64(&arena, &ctx, -5);
    BigFloat neg7 = bigfloat_from_i64(&arena, &ctx, -7);

    /* pos * neg = neg */
    BigFloat r = bigfloat_mul(&arena, &ctx, &pos3, &neg5);
    ASSERT_U32_EQ(r.sign, 1);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, -15);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* neg * neg = pos */
    r = bigfloat_mul(&arena, &ctx, &neg5, &neg7);
    ASSERT_U32_EQ(r.sign, 0);
    e = bigfloat_from_i64(&arena, &ctx, 35);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_nan() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat nan_val = bigfloat_nan();
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat z = bigfloat_zero(53);
    BigFloat pi = bigfloat_inf(0);

    /* x * NaN = NaN */
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &nan_val, &one).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &one, &nan_val).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &nan_val, &z).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &nan_val, &pi).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &nan_val, &nan_val).flags, BIGFLOAT_NAN);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_inf() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);
    BigFloat two = bigfloat_from_i64(&arena, &ctx, 2);
    BigFloat neg3 = bigfloat_from_i64(&arena, &ctx, -3);

    /* inf * inf = inf */
    BigFloat r = bigfloat_mul(&arena, &ctx, &pi, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    /* inf * (-inf) = -inf */
    r = bigfloat_mul(&arena, &ctx, &pi, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    /* (-inf) * (-inf) = +inf */
    r = bigfloat_mul(&arena, &ctx, &ni, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    /* inf * finite = inf */
    r = bigfloat_mul(&arena, &ctx, &pi, &two);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    /* inf * negative = -inf */
    r = bigfloat_mul(&arena, &ctx, &pi, &neg3);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_zero_inf() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat z = bigfloat_zero(53);
    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);

    /* 0 * inf = NaN */
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &z, &pi).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &pi, &z).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &z, &ni).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_mul(&arena, &ctx, &ni, &z).flags, BIGFLOAT_NAN);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_commutativity() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* x * y == y * x for various pairs */
    int64_t pairs[][2] = {
        {3, 7}, {-5, 13}, {100, -200}, {-42, -17}, {1, 999}, {256, 65537}
    };
    int count = sizeof(pairs) / sizeof(pairs[0]);

    for (int i = 0; i < count; i++) {
        BigFloat a = bigfloat_from_i64(&arena, &ctx, pairs[i][0]);
        BigFloat b = bigfloat_from_i64(&arena, &ctx, pairs[i][1]);
        BigFloat r1 = bigfloat_mul(&arena, &ctx, &a, &b);
        BigFloat r2 = bigfloat_mul(&arena, &ctx, &b, &a);
        ASSERT_INT_EQ(bigfloat_cmp(&r1, &r2), 0);
        ASSERT_U32_EQ(r1.sign, r2.sign);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_precision_result() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* All products should have bit_len == precision for normal results */
    int64_t pairs[][2] = {
        {1, 1}, {3, 4}, {100, 200}, {-5, -7}, {13, -17},
        {65536, 65536}, {12345, 67890}, {-1, 1000000}
    };
    int count = sizeof(pairs) / sizeof(pairs[0]);

    for (int i = 0; i < count; i++) {
        BigFloat a = bigfloat_from_i64(&arena, &ctx, pairs[i][0]);
        BigFloat b = bigfloat_from_i64(&arena, &ctx, pairs[i][1]);
        BigFloat r = bigfloat_mul(&arena, &ctx, &a, &b);
        if (r.flags == BIGFLOAT_NORMAL) {
            ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);
        }
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_powers_of_two() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 2^10 * 2^10 = 2^20 = 1048576 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1024);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 1024);
    BigFloat r = bigfloat_mul(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 1048576);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 2^16 * 2^16 = 2^32 */
    a = bigfloat_from_i64(&arena, &ctx, 65536);
    b = bigfloat_from_i64(&arena, &ctx, 65536);
    r = bigfloat_mul(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, (int64_t)4294967296LL);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_small_precision_rounding() {
    tracker_reset();
    arena_t arena = {0};

    /* precision 4: 15 * 15 = 225.
     * 15 at precision 4 = sig 15 (0b1111), exp 0.
     * product sig = 15 * 15 = 225 = 0b11100001 (8 bits).
     * Normalize to 4 bits: shift right 4.
     * 225 >> 4 = 14. guard=0, round=0, sticky=0+1=1.
     * NEAREST_EVEN: guard=0, no round. sig=14, exp=4. val = 14*16 = 224. */
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 15);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 15);
    BigFloat r = bigfloat_mul(&arena, &ctx, &a, &b);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 4);
    const uint64_t* rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 14); /* 0b1110 */
    ASSERT_I64_EQ(r.exponent, 4); /* 14 * 2^4 = 224, nearest to 225 */

    /* With TOWARD_POS_INF: should round up to 15 * 16 = 240 */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_POS_INF;
    a = bigfloat_from_i64(&arena, &ctx, 15);
    b = bigfloat_from_i64(&arena, &ctx, 15);
    r = bigfloat_mul(&arena, &ctx, &a, &b);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 15); /* rounds up */
    ASSERT_I64_EQ(r.exponent, 4); /* 15 * 2^4 = 240 */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_mul_large_precision() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* Multiply large values at high precision and verify bit_len */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1000000007);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 999999937);
    BigFloat r = bigfloat_mul(&arena, &ctx, &a, &b);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 256);

    /* Verify via addition: (a*b)/a should recover b approximately
     * We just check the result is non-zero and normalized */
    ASSERT_U32_EQ(r.sign, 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- US-014: BigFloat division --- */

static int test_bf_div_basic() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 12 / 3 = 4 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 12);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 3);
    BigFloat r = bigfloat_div(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 4);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);

    /* 100 / 10 = 10 */
    a = bigfloat_from_i64(&arena, &ctx, 100);
    b = bigfloat_from_i64(&arena, &ctx, 10);
    r = bigfloat_div(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 10);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 256 / 16 = 16 */
    a = bigfloat_from_i64(&arena, &ctx, 256);
    b = bigfloat_from_i64(&arena, &ctx, 16);
    r = bigfloat_div(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 16);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_identity() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* x / 1 = x for various x */
    int64_t values[] = {1, -1, 42, -42, 256, 1000000};
    int count = sizeof(values) / sizeof(values[0]);
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat r = bigfloat_div(&arena, &ctx, &x, &one);
        ASSERT_INT_EQ(bigfloat_cmp(&r, &x), 0);
        ASSERT_U32_EQ(r.sign, x.sign);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_inverse() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 1/x for small integers: verify (1/x) * x == 1 for powers of 2 (exact) */
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);

    /* 1/2 * 2 = 1 (exact) */
    BigFloat two = bigfloat_from_i64(&arena, &ctx, 2);
    BigFloat half = bigfloat_div(&arena, &ctx, &one, &two);
    BigFloat prod = bigfloat_mul(&arena, &ctx, &half, &two);
    ASSERT_INT_EQ(bigfloat_cmp(&prod, &one), 0);

    /* 1/4 * 4 = 1 (exact) */
    BigFloat four = bigfloat_from_i64(&arena, &ctx, 4);
    BigFloat quarter = bigfloat_div(&arena, &ctx, &one, &four);
    prod = bigfloat_mul(&arena, &ctx, &quarter, &four);
    ASSERT_INT_EQ(bigfloat_cmp(&prod, &one), 0);

    /* 1/8 * 8 = 1 (exact) */
    BigFloat eight = bigfloat_from_i64(&arena, &ctx, 8);
    BigFloat eighth = bigfloat_div(&arena, &ctx, &one, &eight);
    prod = bigfloat_mul(&arena, &ctx, &eighth, &eight);
    ASSERT_INT_EQ(bigfloat_cmp(&prod, &one), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_sign() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pos12 = bigfloat_from_i64(&arena, &ctx, 12);
    BigFloat neg3 = bigfloat_from_i64(&arena, &ctx, -3);
    BigFloat neg12 = bigfloat_from_i64(&arena, &ctx, -12);
    BigFloat pos3 = bigfloat_from_i64(&arena, &ctx, 3);

    /* pos / neg = neg */
    BigFloat r = bigfloat_div(&arena, &ctx, &pos12, &neg3);
    ASSERT_U32_EQ(r.sign, 1);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, -4);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* neg / pos = neg */
    r = bigfloat_div(&arena, &ctx, &neg12, &pos3);
    ASSERT_U32_EQ(r.sign, 1);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* neg / neg = pos */
    r = bigfloat_div(&arena, &ctx, &neg12, &neg3);
    ASSERT_U32_EQ(r.sign, 0);
    e = bigfloat_from_i64(&arena, &ctx, 4);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* pos / pos = pos */
    r = bigfloat_div(&arena, &ctx, &pos12, &pos3);
    ASSERT_U32_EQ(r.sign, 0);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_nan() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat nan_val = bigfloat_nan();
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat z = bigfloat_zero(53);
    BigFloat pi = bigfloat_inf(0);

    /* x / NaN = NaN, NaN / x = NaN */
    ASSERT_U32_EQ(bigfloat_div(&arena, &ctx, &nan_val, &one).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_div(&arena, &ctx, &one, &nan_val).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_div(&arena, &ctx, &nan_val, &z).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_div(&arena, &ctx, &nan_val, &pi).flags, BIGFLOAT_NAN);
    ASSERT_U32_EQ(bigfloat_div(&arena, &ctx, &nan_val, &nan_val).flags, BIGFLOAT_NAN);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_inf() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    BigFloat ni = bigfloat_inf(1);
    BigFloat two = bigfloat_from_i64(&arena, &ctx, 2);
    BigFloat neg3 = bigfloat_from_i64(&arena, &ctx, -3);

    /* inf / inf = NaN */
    BigFloat r = bigfloat_div(&arena, &ctx, &pi, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    r = bigfloat_div(&arena, &ctx, &pi, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    r = bigfloat_div(&arena, &ctx, &ni, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    /* inf / finite = inf (correct sign) */
    r = bigfloat_div(&arena, &ctx, &pi, &two);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    r = bigfloat_div(&arena, &ctx, &pi, &neg3);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    r = bigfloat_div(&arena, &ctx, &ni, &two);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    /* finite / inf = 0 (correct sign) */
    r = bigfloat_div(&arena, &ctx, &two, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 0);

    r = bigfloat_div(&arena, &ctx, &neg3, &pi);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 1);

    r = bigfloat_div(&arena, &ctx, &two, &ni);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_zero() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat z = bigfloat_zero(53);
    BigFloat nz = bigfloat_neg(bigfloat_zero(53));
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat neg5 = bigfloat_from_i64(&arena, &ctx, -5);

    /* 0 / 0 = NaN */
    BigFloat r = bigfloat_div(&arena, &ctx, &z, &z);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    r = bigfloat_div(&arena, &ctx, &nz, &z);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NAN);

    /* x / 0 = inf (correct sign) */
    r = bigfloat_div(&arena, &ctx, &one, &z);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 0);

    r = bigfloat_div(&arena, &ctx, &neg5, &z);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(r.sign, 1);

    /* 0 / y = 0 (correct sign) */
    r = bigfloat_div(&arena, &ctx, &z, &one);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 0);

    r = bigfloat_div(&arena, &ctx, &z, &neg5);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 1);

    r = bigfloat_div(&arena, &ctx, &nz, &one);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(r.sign, 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_rounding_modes() {
    tracker_reset();
    arena_t arena = {0};

    /* precision 4: 1 / 3 is a repeating binary fraction 0.010101...
     * At precision 4 the significand is 4 bits.
     * 1 / 3 ≈ 0.3333... In binary: 0.0101010101...
     * Normalized: 1.01010101... * 2^(-2)
     * At 4 bits: significand choices are 10 (0b1010 = 10) or 11 (0b1011 = 11)
     * Exact value at 4 bits: 1.0101... → guard=0, round=1, sticky=1
     * For sig=10 (0b1010): value = 10 * 2^(-5) = 10/32 = 5/16 = 0.3125
     * For sig=11 (0b1011): value = 11 * 2^(-5) = 11/32 = 0.34375
     * True value = 1/3 ≈ 0.33333
     */

    BigFloat one, three, r;
    const uint64_t* rl;

    /* NEAREST_EVEN: 1/3 → the midpoint between 10/32 and 11/32 is 21/64=0.328125.
     * 1/3 = 0.3333 > 0.328125 → round up → sig=11 */
    BigFloatCtx ctx = { .precision = 4, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    one = bigfloat_from_i64(&arena, &ctx, 1);
    three = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_div(&arena, &ctx, &one, &three);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 4);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 11); /* 0b1011 */

    /* TOWARD_ZERO: truncate → sig=10 */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_ZERO;
    one = bigfloat_from_i64(&arena, &ctx, 1);
    three = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_div(&arena, &ctx, &one, &three);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 10); /* 0b1010 */

    /* TOWARD_POS_INF: positive → round up → sig=11 */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_POS_INF;
    one = bigfloat_from_i64(&arena, &ctx, 1);
    three = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_div(&arena, &ctx, &one, &three);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 11); /* 0b1011 */

    /* TOWARD_NEG_INF: positive → truncate → sig=10 */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_NEG_INF;
    one = bigfloat_from_i64(&arena, &ctx, 1);
    three = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_div(&arena, &ctx, &one, &three);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 10); /* 0b1010 */

    /* Negative: -1 / 3 with TOWARD_NEG_INF → rounds away from zero → sig=11, sign=1 */
    ctx.round_mode = BIGFLOAT_ROUND_TOWARD_NEG_INF;
    BigFloat neg_one = bigfloat_from_i64(&arena, &ctx, -1);
    three = bigfloat_from_i64(&arena, &ctx, 3);
    r = bigfloat_div(&arena, &ctx, &neg_one, &three);
    ASSERT_U32_EQ(r.sign, 1);
    rl = bigint_limbs(&r.significand);
    ASSERT_U64_EQ(rl[0], 11); /* magnitude rounds up for negative toward -inf */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_mul_recovery() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* (a * b) / b should recover a within 1 ULP */
    int64_t pairs[][2] = {
        {7, 3}, {13, 5}, {100, 7}, {256, 17}, {1000000, 13},
        {-42, 7}, {99, -11}, {-100, -3}
    };
    int count = sizeof(pairs) / sizeof(pairs[0]);

    for (int i = 0; i < count; i++) {
        BigFloat a = bigfloat_from_i64(&arena, &ctx, pairs[i][0]);
        BigFloat b = bigfloat_from_i64(&arena, &ctx, pairs[i][1]);
        BigFloat prod = bigfloat_mul(&arena, &ctx, &a, &b);
        BigFloat recovered = bigfloat_div(&arena, &ctx, &prod, &b);

        /* Check recovered == a (should be exact for integer inputs at p=53) */
        ASSERT_INT_EQ(bigfloat_cmp(&recovered, &a), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_precision_result() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* All divisions should produce results with bit_len == precision for normal results */
    int64_t pairs[][2] = {
        {1, 1}, {12, 3}, {100, 7}, {-5, 3}, {13, -17},
        {65536, 13}, {12345, 67890}, {-1, 1000000}
    };
    int count = sizeof(pairs) / sizeof(pairs[0]);

    for (int i = 0; i < count; i++) {
        BigFloat a = bigfloat_from_i64(&arena, &ctx, pairs[i][0]);
        BigFloat b = bigfloat_from_i64(&arena, &ctx, pairs[i][1]);
        BigFloat r = bigfloat_div(&arena, &ctx, &a, &b);
        if (r.flags == BIGFLOAT_NORMAL) {
            ASSERT_U32_EQ(bigint_bit_len(&r.significand), 53);
        }
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_powers_of_two() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 2^20 / 2^10 = 2^10 = 1024 */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1048576);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 1024);
    BigFloat r = bigfloat_div(&arena, &ctx, &a, &b);
    BigFloat e = bigfloat_from_i64(&arena, &ctx, 1024);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    /* 2^32 / 2^16 = 2^16 = 65536 */
    a = bigfloat_from_i64(&arena, &ctx, (int64_t)4294967296LL);
    b = bigfloat_from_i64(&arena, &ctx, 65536);
    r = bigfloat_div(&arena, &ctx, &a, &b);
    e = bigfloat_from_i64(&arena, &ctx, 65536);
    ASSERT_INT_EQ(bigfloat_cmp(&r, &e), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_self() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* x / x = 1 for various x */
    int64_t values[] = {1, 2, 42, -42, 256, 1000000, -7};
    int count = sizeof(values) / sizeof(values[0]);
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        BigFloat r = bigfloat_div(&arena, &ctx, &x, &x);
        ASSERT_INT_EQ(bigfloat_cmp(&r, &one), 0);
        ASSERT_U32_EQ(r.sign, 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_div_large_precision() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* Divide large values at high precision and verify bit_len */
    BigFloat a = bigfloat_from_i64(&arena, &ctx, 1000000007);
    BigFloat b = bigfloat_from_i64(&arena, &ctx, 999999937);
    BigFloat r = bigfloat_div(&arena, &ctx, &a, &b);
    ASSERT_U32_EQ(r.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(bigint_bit_len(&r.significand), 256);

    /* (a/b) * b should recover a */
    BigFloat recovered = bigfloat_mul(&arena, &ctx, &r, &b);
    /* At 256-bit precision, should be exact for these small integers */
    ASSERT_INT_EQ(bigfloat_cmp(&recovered, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- US-015: BigFloat string conversion --- */

static int test_bf_to_str_zero() {
    tracker_reset();
    arena_t arena = {0};

    BigFloat z = bigfloat_zero(53);
    char* s = bigfloat_to_str(&arena, &z, 2);
    ASSERT_STR_EQ(s, "0.0e+0");

    /* Negative zero */
    BigFloat nz = bigfloat_neg(z);
    s = bigfloat_to_str(&arena, &nz, 2);
    ASSERT_STR_EQ(s, "-0.0e+0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_to_str_inf() {
    tracker_reset();
    arena_t arena = {0};

    BigFloat pi = bigfloat_inf(0);
    char* s = bigfloat_to_str(&arena, &pi, 2);
    ASSERT_STR_EQ(s, "inf");

    BigFloat ni = bigfloat_inf(1);
    s = bigfloat_to_str(&arena, &ni, 2);
    ASSERT_STR_EQ(s, "-inf");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_to_str_nan() {
    tracker_reset();
    arena_t arena = {0};

    BigFloat nan_val = bigfloat_nan();
    char* s = bigfloat_to_str(&arena, &nan_val, 2);
    ASSERT_STR_EQ(s, "nan");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_to_str_integers() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat f;
    char* s;

    /* 1 → "1.00e+0" (3 digits) */
    f = bigfloat_from_i64(&arena, &ctx, 1);
    s = bigfloat_to_str(&arena, &f, 3);
    ASSERT_STR_EQ(s, "1.00e+0");

    /* 42 → "4.20e+1" (3 digits) */
    f = bigfloat_from_i64(&arena, &ctx, 42);
    s = bigfloat_to_str(&arena, &f, 3);
    ASSERT_STR_EQ(s, "4.20e+1");

    /* 100 → "1.00e+2" (3 digits) */
    f = bigfloat_from_i64(&arena, &ctx, 100);
    s = bigfloat_to_str(&arena, &f, 3);
    ASSERT_STR_EQ(s, "1.00e+2");

    /* 10 → "1.0e+1" (2 digits) */
    f = bigfloat_from_i64(&arena, &ctx, 10);
    s = bigfloat_to_str(&arena, &f, 2);
    ASSERT_STR_EQ(s, "1.0e+1");

    /* 1 → single digit: "1e+0" */
    f = bigfloat_from_i64(&arena, &ctx, 1);
    s = bigfloat_to_str(&arena, &f, 1);
    ASSERT_STR_EQ(s, "1e+0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_to_str_negative() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* -42 → "-4.20e+1" */
    BigFloat f = bigfloat_from_i64(&arena, &ctx, -42);
    char* s = bigfloat_to_str(&arena, &f, 3);
    ASSERT_STR_EQ(s, "-4.20e+1");

    /* -1 → "-1.0e+0" */
    f = bigfloat_from_i64(&arena, &ctx, -1);
    s = bigfloat_to_str(&arena, &f, 2);
    ASSERT_STR_EQ(s, "-1.0e+0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_to_str_fractions() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat two = bigfloat_from_i64(&arena, &ctx, 2);
    BigFloat four = bigfloat_from_i64(&arena, &ctx, 4);

    /* 1/2 = 0.5 → "5.0e-1" */
    BigFloat half = bigfloat_div(&arena, &ctx, &one, &two);
    char* s = bigfloat_to_str(&arena, &half, 2);
    ASSERT_STR_EQ(s, "5.0e-1");

    /* 1/4 = 0.25 → "2.5e-1" */
    BigFloat quarter = bigfloat_div(&arena, &ctx, &one, &four);
    s = bigfloat_to_str(&arena, &quarter, 2);
    ASSERT_STR_EQ(s, "2.5e-1");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_str_special() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};
    BigFloat out;

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "inf", &out), 0);
    ASSERT_U32_EQ(out.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(out.sign, 0);

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "-inf", &out), 0);
    ASSERT_U32_EQ(out.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(out.sign, 1);

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "+inf", &out), 0);
    ASSERT_U32_EQ(out.flags, BIGFLOAT_INF);
    ASSERT_U32_EQ(out.sign, 0);

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "nan", &out), 0);
    ASSERT_U32_EQ(out.flags, BIGFLOAT_NAN);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_str_integers() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};
    BigFloat out, expected;

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "42", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, 42);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "-100", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, -100);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "0", &out), 0);
    ASSERT_U32_EQ(out.flags, BIGFLOAT_ZERO);
    ASSERT_U32_EQ(out.sign, 0);

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "1000000", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, 1000000);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_str_decimal() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};
    BigFloat out, expected;

    /* "1.5" → 3/2 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "1.5", &out), 0);
    BigFloat three = bigfloat_from_i64(&arena, &ctx, 3);
    BigFloat two = bigfloat_from_i64(&arena, &ctx, 2);
    expected = bigfloat_div(&arena, &ctx, &three, &two);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    /* "0.25" → 1/4 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "0.25", &out), 0);
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat four = bigfloat_from_i64(&arena, &ctx, 4);
    expected = bigfloat_div(&arena, &ctx, &one, &four);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    /* ".5" → 1/2 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, ".5", &out), 0);
    expected = bigfloat_div(&arena, &ctx, &one, &two);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    /* "5." → 5 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "5.", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, 5);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_str_scientific() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};
    BigFloat out, expected;

    /* "1.5e3" → 1500 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "1.5e3", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, 1500);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    /* "1e10" → 10000000000 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "1e10", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, 10000000000LL);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    /* "2.5E-1" → 0.25 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "2.5E-1", &out), 0);
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat four = bigfloat_from_i64(&arena, &ctx, 4);
    expected = bigfloat_div(&arena, &ctx, &one, &four);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    /* "1e0" → 1 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "1e0", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, 1);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    /* "+3e+2" → 300 */
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "+3e+2", &out), 0);
    expected = bigfloat_from_i64(&arena, &ctx, 300);
    ASSERT_INT_EQ(bigfloat_cmp(&out, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_str_pi() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};
    BigFloat out;

    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, "3.14159", &out), 0);
    ASSERT_U32_EQ(out.flags, BIGFLOAT_NORMAL);
    ASSERT_U32_EQ(out.sign, 0);
    ASSERT_U32_EQ(bigint_bit_len(&out.significand), 256);

    /* Convert back and verify first 6 digits match */
    char* s = bigfloat_to_str(&arena, &out, 6);
    ASSERT_STR_EQ(s, "3.14159e+0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_from_str_invalid() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};
    BigFloat out;

    ASSERT(bigfloat_from_str(&arena, &ctx, "", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, "1.2.3", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, "abc", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, "1.2x", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, "1e", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, "1e+", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, ".", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, "-", &out) != 0);
    ASSERT(bigfloat_from_str(&arena, &ctx, NULL, &out) != 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_roundtrip() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* 17 decimal digits enough for 53-bit exact round-trip */
    uint32_t enough = 17;
    int64_t values[] = {1, -1, 42, -42, 256, 1000000, -999999, 12345678};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        char* s = bigfloat_to_str(&arena, &x, enough);
        BigFloat y;
        int err = bigfloat_from_str(&arena, &ctx, s, &y);
        ASSERT_INT_EQ(err, 0);
        ASSERT_INT_EQ(bigfloat_cmp(&x, &y), 0);
    }

    /* Also test 1/3 (inexact value) */
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat three = bigfloat_from_i64(&arena, &ctx, 3);
    BigFloat third = bigfloat_div(&arena, &ctx, &one, &three);
    char* s = bigfloat_to_str(&arena, &third, enough);
    BigFloat y;
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, s, &y), 0);
    ASSERT_INT_EQ(bigfloat_cmp(&third, &y), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bf_roundtrip_256() {
    tracker_reset();
    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    arena_t arena = {0};

    /* ~79 digits for 256-bit exact round-trip */
    uint32_t enough = 79;
    int64_t values[] = {1, -1, 42, 1000000007, -999999937};
    int count = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < count; i++) {
        BigFloat x = bigfloat_from_i64(&arena, &ctx, values[i]);
        char* s = bigfloat_to_str(&arena, &x, enough);
        BigFloat y;
        int err = bigfloat_from_str(&arena, &ctx, s, &y);
        ASSERT_INT_EQ(err, 0);
        ASSERT_INT_EQ(bigfloat_cmp(&x, &y), 0);
    }

    /* Also test 1/3 at high precision */
    BigFloat one = bigfloat_from_i64(&arena, &ctx, 1);
    BigFloat three = bigfloat_from_i64(&arena, &ctx, 3);
    BigFloat third = bigfloat_div(&arena, &ctx, &one, &three);
    char* s = bigfloat_to_str(&arena, &third, enough);
    BigFloat y;
    ASSERT_INT_EQ(bigfloat_from_str(&arena, &ctx, s, &y), 0);
    ASSERT_INT_EQ(bigfloat_cmp(&third, &y), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- main --- */

int main(void) {
    printf("Test: bf_zero\n");
    if (!test_bf_zero()) return 1;

    printf("Test: bf_zero various precisions\n");
    if (!test_bf_zero_various_precisions()) return 1;

    printf("Test: bf_inf positive\n");
    if (!test_bf_inf_positive()) return 1;

    printf("Test: bf_inf negative\n");
    if (!test_bf_inf_negative()) return 1;

    printf("Test: bf_nan\n");
    if (!test_bf_nan()) return 1;

    printf("Test: bf_from_i64 zero\n");
    if (!test_bf_from_i64_zero()) return 1;

    printf("Test: bf_from_i64 one\n");
    if (!test_bf_from_i64_one()) return 1;

    printf("Test: bf_from_i64 negative\n");
    if (!test_bf_from_i64_negative()) return 1;

    printf("Test: bf_from_i64 power of two\n");
    if (!test_bf_from_i64_power_of_two()) return 1;

    printf("Test: bf_from_i64 large\n");
    if (!test_bf_from_i64_large()) return 1;

    printf("Test: bf_from_i64 INT64_MIN\n");
    if (!test_bf_from_i64_int64_min()) return 1;

    printf("Test: bf_from_i64 small precision\n");
    if (!test_bf_from_i64_small_precision()) return 1;

    printf("Test: bf_from_i64 precision 2\n");
    if (!test_bf_from_i64_precision_2()) return 1;

    printf("Test: bf_from_bigint zero\n");
    if (!test_bf_from_bigint_zero()) return 1;

    printf("Test: bf_from_bigint small\n");
    if (!test_bf_from_bigint_small()) return 1;

    printf("Test: bf_from_bigint negative\n");
    if (!test_bf_from_bigint_negative()) return 1;

    printf("Test: bf_from_bigint large\n");
    if (!test_bf_from_bigint_large()) return 1;

    printf("Test: bf_from_bigint exact precision\n");
    if (!test_bf_from_bigint_exact_precision()) return 1;

    printf("Test: bf_normalized bit_len\n");
    if (!test_bf_normalized_bit_len()) return 1;

    printf("Test: bf_value reconstruction\n");
    if (!test_bf_value_reconstruction()) return 1;

    printf("Test: bf_value reconstruction 256\n");
    if (!test_bf_value_reconstruction_256()) return 1;

    printf("Test: bf_rounding nearest even\n");
    if (!test_bf_rounding_nearest_even()) return 1;

    printf("Test: bf_rounding nearest even round up\n");
    if (!test_bf_rounding_nearest_even_round_up()) return 1;

    printf("Test: bf_rounding toward zero\n");
    if (!test_bf_rounding_toward_zero()) return 1;

    printf("Test: bf_rounding toward +inf\n");
    if (!test_bf_rounding_toward_pos_inf()) return 1;

    printf("Test: bf_rounding toward -inf\n");
    if (!test_bf_rounding_toward_neg_inf()) return 1;

    printf("Test: bf_from_bigint rounding\n");
    if (!test_bf_from_bigint_rounding()) return 1;

    printf("Test: bf_from_i64 precision 53 one\n");
    if (!test_bf_from_i64_precision_53_one()) return 1;

    printf("Test: bf_from_i64 canonical zero\n");
    if (!test_bf_from_i64_canonical_zero()) return 1;

    printf("Test: bf_from_i64 large precision\n");
    if (!test_bf_from_i64_large_precision()) return 1;

    printf("Test: bf_rounding carry overflow\n");
    if (!test_bf_rounding_carry_overflow()) return 1;

    /* US-011: Comparison and sign operations */
    printf("Test: bf_predicates\n");
    if (!test_bf_predicates()) return 1;

    printf("Test: bf_cmp zeros\n");
    if (!test_bf_cmp_zeros()) return 1;

    printf("Test: bf_cmp infinities\n");
    if (!test_bf_cmp_infinities()) return 1;

    printf("Test: bf_cmp inf vs normal\n");
    if (!test_bf_cmp_inf_vs_normal()) return 1;

    printf("Test: bf_cmp zero vs normal\n");
    if (!test_bf_cmp_zero_vs_normal()) return 1;

    printf("Test: bf_cmp same sign different exponent\n");
    if (!test_bf_cmp_same_sign_different_exp()) return 1;

    printf("Test: bf_cmp same sign same exponent\n");
    if (!test_bf_cmp_same_sign_same_exp()) return 1;

    printf("Test: bf_cmp negative normals\n");
    if (!test_bf_cmp_negative_normals()) return 1;

    printf("Test: bf_cmp different signs\n");
    if (!test_bf_cmp_different_signs()) return 1;

    printf("Test: bf_cmp full ordering\n");
    if (!test_bf_cmp_full_ordering()) return 1;

    printf("Test: bf_neg normal\n");
    if (!test_bf_neg_normal()) return 1;

    printf("Test: bf_neg zero\n");
    if (!test_bf_neg_zero()) return 1;

    printf("Test: bf_neg inf\n");
    if (!test_bf_neg_inf()) return 1;

    printf("Test: bf_neg nan\n");
    if (!test_bf_neg_nan()) return 1;

    printf("Test: bf_abs positive\n");
    if (!test_bf_abs_positive()) return 1;

    printf("Test: bf_abs negative\n");
    if (!test_bf_abs_negative()) return 1;

    printf("Test: bf_abs zero\n");
    if (!test_bf_abs_zero()) return 1;

    printf("Test: bf_abs inf\n");
    if (!test_bf_abs_inf()) return 1;

    /* US-012: Addition and subtraction */
    printf("Test: bf_add basic\n");
    if (!test_bf_add_basic()) return 1;

    printf("Test: bf_add different exponents\n");
    if (!test_bf_add_different_exponents()) return 1;

    printf("Test: bf_add same sign negative\n");
    if (!test_bf_add_same_sign_negative()) return 1;

    printf("Test: bf_add mixed sign\n");
    if (!test_bf_add_mixed_sign()) return 1;

    printf("Test: bf_add exact cancellation\n");
    if (!test_bf_add_exact_cancellation()) return 1;

    printf("Test: bf_add NaN\n");
    if (!test_bf_add_nan()) return 1;

    printf("Test: bf_add inf same sign\n");
    if (!test_bf_add_inf_same_sign()) return 1;

    printf("Test: bf_add inf opposite sign\n");
    if (!test_bf_add_inf_opposite_sign()) return 1;

    printf("Test: bf_add inf finite\n");
    if (!test_bf_add_inf_finite()) return 1;

    printf("Test: bf_add zero identity\n");
    if (!test_bf_add_zero_identity()) return 1;

    printf("Test: bf_add zero + zero\n");
    if (!test_bf_add_zero_zero()) return 1;

    printf("Test: bf_add epsilon all modes\n");
    if (!test_bf_add_epsilon_all_modes()) return 1;

    printf("Test: bf_add carry\n");
    if (!test_bf_add_carry()) return 1;

    printf("Test: bf_add massive cancellation\n");
    if (!test_bf_add_massive_cancellation()) return 1;

    printf("Test: bf_add result precision\n");
    if (!test_bf_add_result_precision()) return 1;

    printf("Test: bf_sub basic\n");
    if (!test_bf_sub_basic()) return 1;

    printf("Test: bf_sub to zero\n");
    if (!test_bf_sub_to_zero()) return 1;

    printf("Test: bf_sub inf\n");
    if (!test_bf_sub_inf()) return 1;

    printf("Test: bf_sub massive cancellation\n");
    if (!test_bf_sub_massive_cancellation()) return 1;

    printf("Test: bf_add rounding tie\n");
    if (!test_bf_add_rounding_tie()) return 1;

    /* US-013: Multiplication */
    printf("Test: bf_mul basic\n");
    if (!test_bf_mul_basic()) return 1;

    printf("Test: bf_mul identity\n");
    if (!test_bf_mul_identity()) return 1;

    printf("Test: bf_mul zero\n");
    if (!test_bf_mul_zero()) return 1;

    printf("Test: bf_mul neg one\n");
    if (!test_bf_mul_neg_one()) return 1;

    printf("Test: bf_mul sign\n");
    if (!test_bf_mul_sign()) return 1;

    printf("Test: bf_mul NaN\n");
    if (!test_bf_mul_nan()) return 1;

    printf("Test: bf_mul inf\n");
    if (!test_bf_mul_inf()) return 1;

    printf("Test: bf_mul zero * inf\n");
    if (!test_bf_mul_zero_inf()) return 1;

    printf("Test: bf_mul commutativity\n");
    if (!test_bf_mul_commutativity()) return 1;

    printf("Test: bf_mul precision result\n");
    if (!test_bf_mul_precision_result()) return 1;

    printf("Test: bf_mul powers of two\n");
    if (!test_bf_mul_powers_of_two()) return 1;

    printf("Test: bf_mul small precision rounding\n");
    if (!test_bf_mul_small_precision_rounding()) return 1;

    printf("Test: bf_mul large precision\n");
    if (!test_bf_mul_large_precision()) return 1;

    /* US-014: Division */
    printf("Test: bf_div basic\n");
    if (!test_bf_div_basic()) return 1;

    printf("Test: bf_div identity\n");
    if (!test_bf_div_identity()) return 1;

    printf("Test: bf_div inverse\n");
    if (!test_bf_div_inverse()) return 1;

    printf("Test: bf_div sign\n");
    if (!test_bf_div_sign()) return 1;

    printf("Test: bf_div NaN\n");
    if (!test_bf_div_nan()) return 1;

    printf("Test: bf_div inf\n");
    if (!test_bf_div_inf()) return 1;

    printf("Test: bf_div zero\n");
    if (!test_bf_div_zero()) return 1;

    printf("Test: bf_div rounding modes\n");
    if (!test_bf_div_rounding_modes()) return 1;

    printf("Test: bf_div mul recovery\n");
    if (!test_bf_div_mul_recovery()) return 1;

    printf("Test: bf_div precision result\n");
    if (!test_bf_div_precision_result()) return 1;

    printf("Test: bf_div powers of two\n");
    if (!test_bf_div_powers_of_two()) return 1;

    printf("Test: bf_div self\n");
    if (!test_bf_div_self()) return 1;

    printf("Test: bf_div large precision\n");
    if (!test_bf_div_large_precision()) return 1;

    /* US-015: BigFloat string conversion */
    printf("Test: bf_to_str zero\n");
    if (!test_bf_to_str_zero()) return 1;

    printf("Test: bf_to_str inf\n");
    if (!test_bf_to_str_inf()) return 1;

    printf("Test: bf_to_str nan\n");
    if (!test_bf_to_str_nan()) return 1;

    printf("Test: bf_to_str integers\n");
    if (!test_bf_to_str_integers()) return 1;

    printf("Test: bf_to_str negative\n");
    if (!test_bf_to_str_negative()) return 1;

    printf("Test: bf_to_str fractions\n");
    if (!test_bf_to_str_fractions()) return 1;

    printf("Test: bf_from_str special\n");
    if (!test_bf_from_str_special()) return 1;

    printf("Test: bf_from_str integers\n");
    if (!test_bf_from_str_integers()) return 1;

    printf("Test: bf_from_str decimal\n");
    if (!test_bf_from_str_decimal()) return 1;

    printf("Test: bf_from_str scientific\n");
    if (!test_bf_from_str_scientific()) return 1;

    printf("Test: bf_from_str pi\n");
    if (!test_bf_from_str_pi()) return 1;

    printf("Test: bf_from_str invalid\n");
    if (!test_bf_from_str_invalid()) return 1;

    printf("Test: bf_roundtrip\n");
    if (!test_bf_roundtrip()) return 1;

    printf("Test: bf_roundtrip 256\n");
    if (!test_bf_roundtrip_256()) return 1;

    printf("\nAll bigfloat tests passed!\n");
    return 0;
}
