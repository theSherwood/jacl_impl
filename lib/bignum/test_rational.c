/*
 * test_rational.c -- Tests for Rational representation and arithmetic
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
#include "rational.h"

/* --- Tests: construction and normalization --- */

static int test_rat_from_ints_basic() {
    tracker_reset();
    arena_t arena = {0};

    Rational r;
    ASSERT(rational_from_ints(&arena, 1, 2, &r) == 0);
    /* 1/2 should be in lowest terms */
    char* s = rational_to_str(&arena, &r);
    ASSERT_STR_EQ(s, "1/2");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_from_ints_normalize() {
    tracker_reset();
    arena_t arena = {0};

    /* 2/4 normalizes to 1/2 */
    Rational r;
    ASSERT(rational_from_ints(&arena, 2, 4, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "1/2");

    /* -3/-6 normalizes to 1/2 */
    ASSERT(rational_from_ints(&arena, -3, -6, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "1/2");

    /* 6/4 normalizes to 3/2 */
    ASSERT(rational_from_ints(&arena, 6, 4, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "3/2");

    /* -3/6 normalizes to -1/2 */
    ASSERT(rational_from_ints(&arena, -3, 6, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "-1/2");

    /* 3/-6 normalizes to -1/2 */
    ASSERT(rational_from_ints(&arena, 3, -6, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "-1/2");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_from_ints_zero() {
    tracker_reset();
    arena_t arena = {0};

    /* 0/5 = 0/1 = 0 */
    Rational r;
    ASSERT(rational_from_ints(&arena, 0, 5, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "0");
    ASSERT(bigint_is_zero(&r.num));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_from_ints_whole() {
    tracker_reset();
    arena_t arena = {0};

    /* 6/3 = 2 (den = 1, printed as just "2") */
    Rational r;
    ASSERT(rational_from_ints(&arena, 6, 3, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "2");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_from_ints_div_zero() {
    tracker_reset();
    arena_t arena = {0};

    Rational r;
    ASSERT(rational_from_ints(&arena, 1, 0, &r) == RATIONAL_ERR_DIV_ZERO);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_from_bigints() {
    tracker_reset();
    arena_t arena = {0};

    BigInt num = bigint_from_i64(10);
    BigInt den = bigint_from_i64(4);
    Rational r;
    ASSERT(rational_from_bigints(&arena, &num, &den, &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "5/2");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: comparison --- */

static int test_rat_cmp() {
    tracker_reset();
    arena_t arena = {0};

    Rational a, b, c, d;
    rational_from_ints(&arena, -1, 2, &a);  /* -1/2 */
    rational_from_ints(&arena, 0, 1, &b);   /* 0 */
    rational_from_ints(&arena, 1, 3, &c);   /* 1/3 */
    rational_from_ints(&arena, 1, 2, &d);   /* 1/2 */

    /* -1/2 < 0 */
    ASSERT(rational_cmp(&arena, &a, &b) < 0);
    /* 0 < 1/3 */
    ASSERT(rational_cmp(&arena, &b, &c) < 0);
    /* 1/3 < 1/2 */
    ASSERT(rational_cmp(&arena, &c, &d) < 0);
    /* -1/2 < 1/2 */
    ASSERT(rational_cmp(&arena, &a, &d) < 0);

    /* Equal */
    Rational e;
    rational_from_ints(&arena, 2, 4, &e);  /* 2/4 = 1/2 */
    ASSERT(rational_cmp(&arena, &d, &e) == 0);

    /* Greater */
    ASSERT(rational_cmp(&arena, &d, &c) > 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: addition --- */

static int test_rat_add_basic() {
    tracker_reset();
    arena_t arena = {0};

    /* 1/3 + 1/3 + 1/3 = 1 (exact) */
    Rational third;
    rational_from_ints(&arena, 1, 3, &third);
    Rational sum = rational_add(&arena, &third, &third);
    sum = rational_add(&arena, &sum, &third);
    ASSERT_STR_EQ(rational_to_str(&arena, &sum), "1");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_add_different_den() {
    tracker_reset();
    arena_t arena = {0};

    /* 1/2 + 1/3 = 5/6 */
    Rational a, b;
    rational_from_ints(&arena, 1, 2, &a);
    rational_from_ints(&arena, 1, 3, &b);
    Rational sum = rational_add(&arena, &a, &b);
    ASSERT_STR_EQ(rational_to_str(&arena, &sum), "5/6");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_add_negative() {
    tracker_reset();
    arena_t arena = {0};

    /* 1/2 + (-1/3) = 1/6 */
    Rational a, b;
    rational_from_ints(&arena, 1, 2, &a);
    rational_from_ints(&arena, -1, 3, &b);
    Rational sum = rational_add(&arena, &a, &b);
    ASSERT_STR_EQ(rational_to_str(&arena, &sum), "1/6");

    /* -1/2 + (-1/3) = -5/6 */
    Rational c;
    rational_from_ints(&arena, -1, 2, &c);
    sum = rational_add(&arena, &c, &b);
    ASSERT_STR_EQ(rational_to_str(&arena, &sum), "-5/6");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_add_zero() {
    tracker_reset();
    arena_t arena = {0};

    /* 3/4 + 0 = 3/4 */
    Rational a, z;
    rational_from_ints(&arena, 3, 4, &a);
    rational_from_ints(&arena, 0, 1, &z);
    Rational sum = rational_add(&arena, &a, &z);
    ASSERT_STR_EQ(rational_to_str(&arena, &sum), "3/4");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: subtraction --- */

static int test_rat_sub_basic() {
    tracker_reset();
    arena_t arena = {0};

    /* 1/2 - 1/3 = 1/6 */
    Rational a, b;
    rational_from_ints(&arena, 1, 2, &a);
    rational_from_ints(&arena, 1, 3, &b);
    Rational diff = rational_sub(&arena, &a, &b);
    ASSERT_STR_EQ(rational_to_str(&arena, &diff), "1/6");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_sub_self() {
    tracker_reset();
    arena_t arena = {0};

    /* 3/7 - 3/7 = 0 */
    Rational a;
    rational_from_ints(&arena, 3, 7, &a);
    Rational diff = rational_sub(&arena, &a, &a);
    ASSERT_STR_EQ(rational_to_str(&arena, &diff), "0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: multiplication --- */

static int test_rat_mul_basic() {
    tracker_reset();
    arena_t arena = {0};

    /* 1/3 * 3 = 1 (exact) */
    Rational third, three;
    rational_from_ints(&arena, 1, 3, &third);
    rational_from_ints(&arena, 3, 1, &three);
    Rational prod = rational_mul(&arena, &third, &three);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "1");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_mul_fractions() {
    tracker_reset();
    arena_t arena = {0};

    /* 2/3 * 3/4 = 1/2 */
    Rational a, b;
    rational_from_ints(&arena, 2, 3, &a);
    rational_from_ints(&arena, 3, 4, &b);
    Rational prod = rational_mul(&arena, &a, &b);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "1/2");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_mul_zero() {
    tracker_reset();
    arena_t arena = {0};

    /* 5/7 * 0 = 0 */
    Rational a, z;
    rational_from_ints(&arena, 5, 7, &a);
    rational_from_ints(&arena, 0, 1, &z);
    Rational prod = rational_mul(&arena, &a, &z);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_mul_negative() {
    tracker_reset();
    arena_t arena = {0};

    /* -2/3 * 3/5 = -2/5 */
    Rational a, b;
    rational_from_ints(&arena, -2, 3, &a);
    rational_from_ints(&arena, 3, 5, &b);
    Rational prod = rational_mul(&arena, &a, &b);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "-2/5");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: division --- */

static int test_rat_div_basic() {
    tracker_reset();
    arena_t arena = {0};

    /* (1/2) / (3/4) = (1/2) * (4/3) = 2/3 */
    Rational a, b, result;
    rational_from_ints(&arena, 1, 2, &a);
    rational_from_ints(&arena, 3, 4, &b);
    ASSERT(rational_div(&arena, &a, &b, &result) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &result), "2/3");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_div_by_zero() {
    tracker_reset();
    arena_t arena = {0};

    Rational a, z, result;
    rational_from_ints(&arena, 1, 2, &a);
    rational_from_ints(&arena, 0, 1, &z);
    ASSERT(rational_div(&arena, &a, &z, &result) == RATIONAL_ERR_DIV_ZERO);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_div_self() {
    tracker_reset();
    arena_t arena = {0};

    /* (3/7) / (3/7) = 1 */
    Rational a, result;
    rational_from_ints(&arena, 3, 7, &a);
    ASSERT(rational_div(&arena, &a, &a, &result) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &result), "1");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: string conversion --- */

static int test_rat_to_str() {
    tracker_reset();
    arena_t arena = {0};

    Rational r;

    /* Integer */
    rational_from_ints(&arena, 5, 1, &r);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "5");

    /* Fraction */
    rational_from_ints(&arena, 3, 7, &r);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "3/7");

    /* Negative */
    rational_from_ints(&arena, -3, 7, &r);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "-3/7");

    /* Zero */
    rational_from_ints(&arena, 0, 5, &r);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_from_str() {
    tracker_reset();
    arena_t arena = {0};

    Rational r;

    /* Simple fraction */
    ASSERT(rational_from_str(&arena, "3/7", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "3/7");

    /* Fraction that normalizes */
    ASSERT(rational_from_str(&arena, "6/4", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "3/2");

    /* Integer */
    ASSERT(rational_from_str(&arena, "42", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "42");

    /* Negative */
    ASSERT(rational_from_str(&arena, "-5/3", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "-5/3");

    /* Zero */
    ASSERT(rational_from_str(&arena, "0", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_from_str_invalid() {
    tracker_reset();
    arena_t arena = {0};

    Rational r;

    /* Empty string */
    ASSERT(rational_from_str(&arena, "", &r) == RATIONAL_ERR_INVALID_INPUT);

    /* Just a slash */
    ASSERT(rational_from_str(&arena, "/3", &r) == RATIONAL_ERR_INVALID_INPUT);

    /* Trailing slash */
    ASSERT(rational_from_str(&arena, "3/", &r) == RATIONAL_ERR_INVALID_INPUT);

    /* Zero denominator */
    ASSERT(rational_from_str(&arena, "3/0", &r) == RATIONAL_ERR_DIV_ZERO);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_roundtrip() {
    tracker_reset();
    arena_t arena = {0};

    /* Round-trip: from_str(to_str(x)) == x */
    Rational r, r2;
    rational_from_ints(&arena, 22, 7, &r);
    char* s = rational_to_str(&arena, &r);
    ASSERT(rational_from_str(&arena, s, &r2) == 0);
    ASSERT(rational_cmp(&arena, &r, &r2) == 0);

    /* Negative round-trip */
    rational_from_ints(&arena, -355, 113, &r);
    s = rational_to_str(&arena, &r);
    ASSERT(rational_from_str(&arena, s, &r2) == 0);
    ASSERT(rational_cmp(&arena, &r, &r2) == 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: conversion to BigFloat --- */

static int test_rat_to_bigfloat() {
    tracker_reset();
    arena_t arena = {0};

    BigFloatCtx ctx = { .precision = 53, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };

    /* 1/2 = 0.5 exactly */
    Rational half;
    rational_from_ints(&arena, 1, 2, &half);
    BigFloat f = rational_to_bigfloat(&arena, &ctx, &half);
    ASSERT(!bigfloat_is_zero(&f));
    ASSERT(!bigfloat_is_nan(&f));
    /* Compare with bigfloat_from_i64: 0.5 = 1 * 2^(-1) */
    /* Check via string conversion */
    char* s = bigfloat_to_str(&arena, &f, 5);
    ASSERT_STR_EQ(s, "5.0000e-1");

    /* 1/3 should be close to 0.333... */
    Rational third;
    rational_from_ints(&arena, 1, 3, &third);
    f = rational_to_bigfloat(&arena, &ctx, &third);
    s = bigfloat_to_str(&arena, &f, 10);
    /* 1/3 at 53-bit precision should be 3.333333333e-1 */
    ASSERT(s[0] == '3');
    ASSERT(s[1] == '.');
    ASSERT(s[2] == '3');

    /* -7/4 = -1.75 */
    Rational neg_frac;
    rational_from_ints(&arena, -7, 4, &neg_frac);
    f = rational_to_bigfloat(&arena, &ctx, &neg_frac);
    s = bigfloat_to_str(&arena, &f, 5);
    ASSERT_STR_EQ(s, "-1.7500e+0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: multi-limb values --- */

static int test_rat_multi_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* Use factorial-like values: 20!/10! as numerator, verify GCD normalization */
    /* Build 20! and 10! */
    BigInt fact20 = bigint_from_u64(1);
    for (int i = 2; i <= 20; i++) {
        fact20 = bigint_mul_u64(&arena, &fact20, (uint64_t)i);
    }
    BigInt fact10 = bigint_from_u64(1);
    for (int i = 2; i <= 10; i++) {
        fact10 = bigint_mul_u64(&arena, &fact10, (uint64_t)i);
    }

    /* 20!/10! should simplify by GCD */
    Rational r;
    ASSERT(rational_from_bigints(&arena, &fact20, &fact10, &r) == 0);

    /* 20!/10! = 11*12*...*20 = 670442572800 */
    /* In fraction form: after GCD normalization, den should be 1 */
    BigInt one = bigint_from_u64(1);
    ASSERT(bigint_cmp(&r.den, &one) == 0);

    /* Verify the numerator value */
    char* s = bigint_to_str(&arena, &r.num, 10);
    ASSERT_STR_EQ(s, "670442572800");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: arithmetic identities --- */

static int test_rat_identities() {
    tracker_reset();
    arena_t arena = {0};

    Rational a, b;
    rational_from_ints(&arena, 7, 11, &a);
    rational_from_ints(&arena, 5, 13, &b);

    /* a + b == b + a (commutativity) */
    Rational ab = rational_add(&arena, &a, &b);
    Rational ba = rational_add(&arena, &b, &a);
    ASSERT(rational_cmp(&arena, &ab, &ba) == 0);

    /* a * b == b * a (commutativity) */
    Rational ab_mul = rational_mul(&arena, &a, &b);
    Rational ba_mul = rational_mul(&arena, &b, &a);
    ASSERT(rational_cmp(&arena, &ab_mul, &ba_mul) == 0);

    /* a * (1/a) == 1 (inverse) */
    Rational inv_a;
    ASSERT(rational_div(&arena, &(Rational){bigint_from_u64(1), bigint_from_u64(1)}, &a, &inv_a) == 0);
    Rational prod = rational_mul(&arena, &a, &inv_a);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "1");

    /* (a + b) - b == a */
    Rational diff = rational_sub(&arena, &ab, &b);
    ASSERT(rational_cmp(&arena, &diff, &a) == 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: stress tests with large shared factors (US-022) --- */

static int test_rat_stress_large_lcm() {
    tracker_reset();
    arena_t arena = {0};

    /* Build LCM = 2^64 * 3 * 5 * 7 */
    BigInt one_bi = bigint_from_u64(1);
    BigInt two_64 = bigint_shift_left(&arena, &one_bi, 64); /* 2^64 */
    BigInt lcm = bigint_mul_u64(&arena, &two_64, 3);
    lcm = bigint_mul_u64(&arena, &lcm, 5);
    lcm = bigint_mul_u64(&arena, &lcm, 7);  /* 2^64 * 105 */

    /* a/lcm + b/lcm where a=1, b=1 => 2/lcm => 1/(2^63 * 105) */
    BigInt a_num = bigint_from_u64(1);
    BigInt b_num = bigint_from_u64(1);
    Rational ra, rb;
    ASSERT(rational_from_bigints(&arena, &a_num, &lcm, &ra) == 0);
    ASSERT(rational_from_bigints(&arena, &b_num, &lcm, &rb) == 0);

    Rational sum = rational_add(&arena, &ra, &rb);

    /* Result should be 2/lcm = 1/(2^63 * 105) */
    /* Verify numerator is 1 */
    BigInt one_check = bigint_from_u64(1);
    ASSERT(bigint_cmp(&sum.num, &one_check) == 0);

    /* Verify denominator = 2^63 * 105 */
    BigInt two_63 = bigint_shift_left(&arena, &one_bi, 63);
    BigInt expected_den = bigint_mul_u64(&arena, &two_63, 105);
    ASSERT(bigint_cmp(&sum.den, &expected_den) == 0);

    /* Verify result is in lowest terms: gcd(1, den) = 1 */
    BigInt g = bigint_gcd(&arena, &sum.num, &sum.den);
    ASSERT(bigint_cmp(&g, &one_check) == 0);

    /* Now try a=11, b=13 (both coprime to lcm) => 24/lcm */
    /* 24 = 2^3 * 3, lcm has 2^64 and 3, gcd(24, lcm) = 24 */
    /* Result = 1/(2^61 * 5 * 7) = 1/(2^61 * 35) */
    BigInt a11 = bigint_from_u64(11);
    BigInt b13 = bigint_from_u64(13);
    ASSERT(rational_from_bigints(&arena, &a11, &lcm, &ra) == 0);
    ASSERT(rational_from_bigints(&arena, &b13, &lcm, &rb) == 0);

    sum = rational_add(&arena, &ra, &rb);

    /* Verify numerator is 1 */
    ASSERT(bigint_cmp(&sum.num, &one_check) == 0);

    /* Verify denominator = 2^61 * 35 */
    BigInt two_61 = bigint_shift_left(&arena, &one_bi, 61);
    expected_den = bigint_mul_u64(&arena, &two_61, 35);
    ASSERT(bigint_cmp(&sum.den, &expected_den) == 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_stress_harmonic_20() {
    tracker_reset();
    arena_t arena = {0};

    /* Compute H_20 = sum_{k=1}^{20} 1/k using rational arithmetic */
    Rational sum;
    rational_from_ints(&arena, 0, 1, &sum);

    for (int k = 1; k <= 20; k++) {
        Rational term;
        rational_from_ints(&arena, 1, k, &term);
        sum = rational_add(&arena, &sum, &term);
    }

    /* H_20 = 55835135/15519504 (exact, in lowest terms) */
    ASSERT_STR_EQ(rational_to_str(&arena, &sum), "55835135/15519504");

    /* Cross-check: sum * 15519504 should equal 55835135 */
    Rational den_rat;
    rational_from_ints(&arena, 15519504, 1, &den_rat);
    Rational prod = rational_mul(&arena, &sum, &den_rat);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "55835135");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_stress_shared_denominator() {
    tracker_reset();
    arena_t arena = {0};

    /* Build a large denominator: 2^128 * 17 * 19 * 23 */
    BigInt one_bi = bigint_from_u64(1);
    BigInt big_den = bigint_shift_left(&arena, &one_bi, 128);
    big_den = bigint_mul_u64(&arena, &big_den, 17);
    big_den = bigint_mul_u64(&arena, &big_den, 19);
    big_den = bigint_mul_u64(&arena, &big_den, 23);

    /* (1 / big_den) + (1 / big_den) = 2 / big_den */
    /* Since big_den has 2^128, this simplifies to 1 / (big_den / 2) */
    Rational ra, rb;
    ASSERT(rational_from_bigints(&arena, &one_bi, &big_den, &ra) == 0);
    ASSERT(rational_from_bigints(&arena, &one_bi, &big_den, &rb) == 0);

    Rational sum = rational_add(&arena, &ra, &rb);

    /* Verify denominator is NOT big_den^2 — should be big_den / 2 = 2^127 * 17 * 19 * 23 */
    BigInt two_127 = bigint_shift_left(&arena, &one_bi, 127);
    BigInt expected_den = bigint_mul_u64(&arena, &two_127, 17);
    expected_den = bigint_mul_u64(&arena, &expected_den, 19);
    expected_den = bigint_mul_u64(&arena, &expected_den, 23);
    ASSERT(bigint_cmp(&sum.den, &expected_den) == 0);

    /* Verify numerator is 1 */
    ASSERT(bigint_cmp(&sum.num, &one_bi) == 0);

    /* Result denominator bit_len should be close to 127 + small bits, NOT ~256 */
    uint32_t den_bits = bigint_bit_len(&sum.den);
    ASSERT(den_bits < 145);  /* 127 + log2(17*19*23) ≈ 127 + 13 = 140 */

    /* Test with coprime numerators: 7/big_den + 11/big_den = 18/big_den */
    /* 18 = 2 * 3^2, big_den has 2^128, gcd = 2, result = 9 / (big_den/2) */
    BigInt seven = bigint_from_u64(7);
    BigInt eleven = bigint_from_u64(11);
    ASSERT(rational_from_bigints(&arena, &seven, &big_den, &ra) == 0);
    ASSERT(rational_from_bigints(&arena, &eleven, &big_den, &rb) == 0);

    sum = rational_add(&arena, &ra, &rb);

    BigInt nine = bigint_from_u64(9);
    ASSERT(bigint_cmp(&sum.num, &nine) == 0);
    ASSERT(bigint_cmp(&sum.den, &expected_den) == 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: from_str with negative denominator (US-020) --- */

static int test_rat_from_str_negative_den() {
    tracker_reset();
    arena_t arena = {0};

    Rational r;

    /* "3/-4" should produce -3/4 */
    ASSERT(rational_from_str(&arena, "3/-4", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "-3/4");

    /* "-3/-4" should produce 3/4 */
    ASSERT(rational_from_str(&arena, "-3/-4", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "3/4");

    /* "0/-5" should produce 0 */
    ASSERT(rational_from_str(&arena, "0/-5", &r) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &r), "0");
    ASSERT(bigint_is_zero(&r.num));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: cross-cancellation in mul/div (US-024) --- */

static int test_rat_mul_cross_cancel_primes() {
    tracker_reset();
    arena_t arena = {0};

    /* Build two large primes:
     * large_prime_a = 2^127 - 1 (Mersenne prime M127, 2 limbs)
     * large_prime_b = 2^89 - 1  (Mersenne prime M89, 2 limbs) */
    BigInt one_bi = bigint_from_u64(1);
    BigInt two_127 = bigint_shift_left(&arena, &one_bi, 127);
    BigInt large_a = bigint_sub(&arena, &two_127, &one_bi);

    BigInt two_89 = bigint_shift_left(&arena, &one_bi, 89);
    BigInt large_b = bigint_sub(&arena, &two_89, &one_bi);

    /* r1 = large_a / large_b, r2 = large_b / large_a */
    Rational r1, r2;
    ASSERT(rational_from_bigints(&arena, &large_a, &large_b, &r1) == 0);
    ASSERT(rational_from_bigints(&arena, &large_b, &large_a, &r2) == 0);

    /* r1 * r2 should be exactly 1 */
    Rational prod = rational_mul(&arena, &r1, &r2);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "1");

    /* Also verify via rational_div: r1 / r1 == 1 */
    Rational div_result;
    ASSERT(rational_div(&arena, &r1, &r1, &div_result) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &div_result), "1");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_rat_mul_cross_cancel_factorial() {
    tracker_reset();
    arena_t arena = {0};

    /* Build 40! and 20! */
    BigInt fact40 = bigint_from_u64(1);
    for (int i = 2; i <= 40; i++) {
        fact40 = bigint_mul_u64(&arena, &fact40, (uint64_t)i);
    }
    BigInt fact20 = bigint_from_u64(1);
    for (int i = 2; i <= 20; i++) {
        fact20 = bigint_mul_u64(&arena, &fact20, (uint64_t)i);
    }

    /* r1 = 40! / 20!, r2 = 20! / 40! */
    Rational r1, r2;
    ASSERT(rational_from_bigints(&arena, &fact40, &fact20, &r1) == 0);
    ASSERT(rational_from_bigints(&arena, &fact20, &fact40, &r2) == 0);

    /* r1 * r2 == 1 — exercises cross-cancellation with many shared factors */
    Rational prod = rational_mul(&arena, &r1, &r2);
    ASSERT_STR_EQ(rational_to_str(&arena, &prod), "1");

    /* Also verify r1 is an integer (20! divides 40!) */
    BigInt one_bi = bigint_from_u64(1);
    ASSERT(bigint_cmp(&r1.den, &one_bi) == 0);

    /* r1 = 40!/20! = 21*22*...*40 — verify via known value */
    /* 40!/20! = 335367096786357081410764800000 */
    char* s = bigint_to_str(&arena, &r1.num, 10);
    ASSERT_STR_EQ(s, "335367096786357081410764800000");

    /* Test rational_div cross-cancellation: r1 / r1 == 1 */
    Rational div_result;
    ASSERT(rational_div(&arena, &r1, &r1, &div_result) == 0);
    ASSERT_STR_EQ(rational_to_str(&arena, &div_result), "1");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);
typedef struct { const char* name; test_fn fn; } test_entry;

int main(void) {
    test_entry tests[] = {
        {"rat_from_ints_basic",      test_rat_from_ints_basic},
        {"rat_from_ints_normalize",  test_rat_from_ints_normalize},
        {"rat_from_ints_zero",       test_rat_from_ints_zero},
        {"rat_from_ints_whole",      test_rat_from_ints_whole},
        {"rat_from_ints_div_zero",   test_rat_from_ints_div_zero},
        {"rat_from_bigints",         test_rat_from_bigints},
        {"rat_cmp",                  test_rat_cmp},
        {"rat_add_basic",            test_rat_add_basic},
        {"rat_add_different_den",    test_rat_add_different_den},
        {"rat_add_negative",         test_rat_add_negative},
        {"rat_add_zero",             test_rat_add_zero},
        {"rat_sub_basic",            test_rat_sub_basic},
        {"rat_sub_self",             test_rat_sub_self},
        {"rat_mul_basic",            test_rat_mul_basic},
        {"rat_mul_fractions",        test_rat_mul_fractions},
        {"rat_mul_zero",             test_rat_mul_zero},
        {"rat_mul_negative",         test_rat_mul_negative},
        {"rat_div_basic",            test_rat_div_basic},
        {"rat_div_by_zero",          test_rat_div_by_zero},
        {"rat_div_self",             test_rat_div_self},
        {"rat_to_str",               test_rat_to_str},
        {"rat_from_str",             test_rat_from_str},
        {"rat_from_str_invalid",     test_rat_from_str_invalid},
        {"rat_from_str_negative_den", test_rat_from_str_negative_den},
        {"rat_roundtrip",            test_rat_roundtrip},
        {"rat_to_bigfloat",          test_rat_to_bigfloat},
        {"rat_multi_limb",           test_rat_multi_limb},
        {"rat_identities",           test_rat_identities},
        {"rat_stress_large_lcm",     test_rat_stress_large_lcm},
        {"rat_stress_harmonic_20",   test_rat_stress_harmonic_20},
        {"rat_stress_shared_den",    test_rat_stress_shared_denominator},
        {"rat_mul_cross_cancel_primes", test_rat_mul_cross_cancel_primes},
        {"rat_mul_cross_cancel_fact",   test_rat_mul_cross_cancel_factorial},
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int pass = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        printf("  %-35s", tests[i].name);
        if (tests[i].fn()) {
            pass++;
        } else {
            printf("FAIL\n");
            fail++;
        }
    }

    printf("\n  %d/%d tests passed\n", pass, pass + fail);
    return fail ? 1 : 0;
}
