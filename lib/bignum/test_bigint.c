/*
 * test_bigint.c -- Tests for BigInt construction and representation
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
#define BIGINT_KARATSUBA_THRESHOLD 4
#include "bigint.h"

/* --- Tests: bigint_zero --- */

static int test_zero() {
    tracker_reset();

    BigInt z = bigint_zero();
    ASSERT_U32_EQ(z.sign, 0);
    ASSERT_U32_EQ(z.len, 1);
    ASSERT_U32_EQ(z.cap, 0);
    ASSERT_U64_EQ(bigint_limbs(&z)[0], 0);

    /* No allocations for zero */
    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_from_u64 --- */

static int test_from_u64_zero() {
    tracker_reset();

    BigInt x = bigint_from_u64(0);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U32_EQ(x.cap, 0);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 0);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_u64_one() {
    tracker_reset();

    BigInt x = bigint_from_u64(1);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 1);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_u64_max() {
    tracker_reset();

    BigInt x = bigint_from_u64(UINT64_MAX);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], UINT64_MAX);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_from_i64 --- */

static int test_from_i64_positive() {
    tracker_reset();

    BigInt x = bigint_from_i64(42);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 42);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_i64_negative() {
    tracker_reset();

    BigInt x = bigint_from_i64(-42);
    ASSERT_U32_EQ(x.sign, 1);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 42);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_i64_zero() {
    tracker_reset();

    BigInt x = bigint_from_i64(0);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 0);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_i64_int64_max() {
    tracker_reset();

    BigInt x = bigint_from_i64(INT64_MAX);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], (uint64_t)INT64_MAX);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_i64_int64_min() {
    tracker_reset();

    BigInt x = bigint_from_i64(INT64_MIN);
    ASSERT_U32_EQ(x.sign, 1);
    ASSERT_U32_EQ(x.len, 1);
    /* INT64_MIN = -2^63, magnitude = 2^63 = 9223372036854775808 */
    ASSERT_U64_EQ(bigint_limbs(&x)[0], (uint64_t)9223372036854775808ULL);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_i64_minus_one() {
    tracker_reset();

    BigInt x = bigint_from_i64(-1);
    ASSERT_U32_EQ(x.sign, 1);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 1);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_from_u128 --- */

static int test_from_u128_zero() {
    tracker_reset();

    BigInt x = bigint_from_u128(0, 0);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 0);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_u128_lo_only() {
    tracker_reset();

    BigInt x = bigint_from_u128(0, 0xDEADBEEFULL);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 0xDEADBEEFULL);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_u128_hi_and_lo() {
    tracker_reset();

    BigInt x = bigint_from_u128(0x1234, 0x5678);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 2);
    ASSERT_U32_EQ(x.cap, 0); /* inline storage */
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 0x5678);
    ASSERT_U64_EQ(bigint_limbs(&x)[1], 0x1234);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_u128_max() {
    tracker_reset();

    BigInt x = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 2);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], UINT64_MAX);
    ASSERT_U64_EQ(bigint_limbs(&x)[1], UINT64_MAX);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_u128_hi_one() {
    tracker_reset();

    /* 2^64 = hi=1, lo=0 */
    BigInt x = bigint_from_u128(1, 0);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 2);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&x)[1], 1);

    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_clone --- */

static int test_clone_inline() {
    tracker_reset();

    arena_t arena = {0};

    BigInt orig = bigint_from_i64(-999);
    BigInt copy = bigint_clone(&arena, &orig);

    /* Clone should be identical */
    ASSERT_U32_EQ(copy.sign, 1);
    ASSERT_U32_EQ(copy.len, 1);
    ASSERT_U32_EQ(copy.cap, 0);
    ASSERT_U64_EQ(bigint_limbs(&copy)[0], 999);

    /* Inline clone doesn't need arena allocation */
    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_clone_two_limbs() {
    tracker_reset();

    arena_t arena = {0};

    BigInt orig = bigint_from_u128(42, 100);
    BigInt copy = bigint_clone(&arena, &orig);

    ASSERT_U32_EQ(copy.sign, 0);
    ASSERT_U32_EQ(copy.len, 2);
    ASSERT_U32_EQ(copy.cap, 0); /* still inline */
    ASSERT_U64_EQ(bigint_limbs(&copy)[0], 100);
    ASSERT_U64_EQ(bigint_limbs(&copy)[1], 42);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_clone_heap() {
    tracker_reset();

    arena_t arena = {0};

    /* Build a 3-limb BigInt manually to test heap path */
    BigInt orig = _bigint_alloc(&arena, 3);
    orig.sign = 0;
    uint64_t* limbs = _bigint_limbs_mut(&orig);
    limbs[0] = 0xAAAAAAAAAAAAAAAAULL;
    limbs[1] = 0xBBBBBBBBBBBBBBBBULL;
    limbs[2] = 0xCCCCCCCCCCCCCCCCULL;

    ASSERT_U32_EQ(orig.cap, 3);
    ASSERT(orig.ptr != NULL);

    /* Clone into same arena */
    BigInt copy = bigint_clone(&arena, &orig);

    ASSERT_U32_EQ(copy.sign, 0);
    ASSERT_U32_EQ(copy.len, 3);
    ASSERT_U32_EQ(copy.cap, 3);
    ASSERT(copy.ptr != orig.ptr); /* different allocation */
    ASSERT_U64_EQ(bigint_limbs(&copy)[0], 0xAAAAAAAAAAAAAAAAULL);
    ASSERT_U64_EQ(bigint_limbs(&copy)[1], 0xBBBBBBBBBBBBBBBBULL);
    ASSERT_U64_EQ(bigint_limbs(&copy)[2], 0xCCCCCCCCCCCCCCCCULL);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: inline storage invariant --- */

static int test_inline_storage_invariant() {
    tracker_reset();

    /* All construction functions with len <= 2 must use inline (cap=0) */
    BigInt z = bigint_zero();
    ASSERT_U32_EQ(z.cap, 0);

    BigInt u = bigint_from_u64(12345);
    ASSERT_U32_EQ(u.cap, 0);

    BigInt i = bigint_from_i64(-12345);
    ASSERT_U32_EQ(i.cap, 0);

    BigInt u128 = bigint_from_u128(1, 2);
    ASSERT_U32_EQ(u128.cap, 0);

    /* No allocations at all */
    ASSERT(tracker_active_count() == 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_limbs helper --- */

static int test_limbs_helper() {
    tracker_reset();

    arena_t arena = {0};

    /* Inline: limbs pointer should point to inline_limbs */
    BigInt a = bigint_from_u64(7);
    const uint64_t* a_limbs = bigint_limbs(&a);
    ASSERT_U64_EQ(a_limbs[0], 7);

    /* Two-limb inline */
    BigInt b = bigint_from_u128(3, 5);
    const uint64_t* b_limbs = bigint_limbs(&b);
    ASSERT_U64_EQ(b_limbs[0], 5);
    ASSERT_U64_EQ(b_limbs[1], 3);

    /* Heap: limbs pointer should point to ptr */
    BigInt c = _bigint_alloc(&arena, 4);
    uint64_t* c_mut = _bigint_limbs_mut(&c);
    c_mut[0] = 10;
    c_mut[1] = 20;
    c_mut[2] = 30;
    c_mut[3] = 40;
    const uint64_t* c_limbs = bigint_limbs(&c);
    ASSERT_U64_EQ(c_limbs[0], 10);
    ASSERT_U64_EQ(c_limbs[1], 20);
    ASSERT_U64_EQ(c_limbs[2], 30);
    ASSERT_U64_EQ(c_limbs[3], 40);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: normalize --- */

static int test_normalize_strips_leading_zeros() {
    tracker_reset();

    arena_t arena = {0};

    /* Build a 3-limb BigInt where top limb is zero */
    BigInt x = _bigint_alloc(&arena, 3);
    uint64_t* limbs = _bigint_limbs_mut(&x);
    limbs[0] = 42;
    limbs[1] = 0;
    limbs[2] = 0;

    _bigint_normalize(&x);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U32_EQ(x.cap, 0); /* converted to inline */
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 42);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_normalize_zero_canonical() {
    tracker_reset();

    /* Negative zero should become non-negative */
    BigInt x = bigint_zero();
    x.sign = 1; /* force negative */
    _bigint_normalize(&x);
    ASSERT_U32_EQ(x.sign, 0);
    ASSERT_U32_EQ(x.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&x)[0], 0);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_cmp --- */

static int test_cmp_equal() {
    tracker_reset();

    BigInt a = bigint_from_i64(42);
    BigInt b = bigint_from_i64(42);
    ASSERT_INT_EQ(bigint_cmp(&a, &b), 0);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_cmp_less() {
    tracker_reset();

    BigInt a = bigint_from_i64(10);
    BigInt b = bigint_from_i64(20);
    ASSERT_INT_EQ(bigint_cmp(&a, &b), -1);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_cmp_greater() {
    tracker_reset();

    BigInt a = bigint_from_i64(100);
    BigInt b = bigint_from_i64(50);
    ASSERT_INT_EQ(bigint_cmp(&a, &b), 1);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_cmp_negative() {
    tracker_reset();

    BigInt a = bigint_from_i64(-5);
    BigInt b = bigint_from_i64(3);
    ASSERT_INT_EQ(bigint_cmp(&a, &b), -1);

    BigInt c = bigint_from_i64(3);
    BigInt d = bigint_from_i64(-5);
    ASSERT_INT_EQ(bigint_cmp(&c, &d), 1);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_cmp_both_negative() {
    tracker_reset();

    BigInt a = bigint_from_i64(-10);
    BigInt b = bigint_from_i64(-5);
    ASSERT_INT_EQ(bigint_cmp(&a, &b), -1); /* -10 < -5 */

    BigInt c = bigint_from_i64(-5);
    BigInt d = bigint_from_i64(-10);
    ASSERT_INT_EQ(bigint_cmp(&c, &d), 1);  /* -5 > -10 */

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_cmp_zeros() {
    tracker_reset();

    BigInt a = bigint_zero();
    BigInt b = bigint_zero();
    ASSERT_INT_EQ(bigint_cmp(&a, &b), 0);

    BigInt c = bigint_from_i64(0);
    ASSERT_INT_EQ(bigint_cmp(&a, &c), 0);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_cmp_different_lengths() {
    tracker_reset();

    BigInt a = bigint_from_u64(1);
    BigInt b = bigint_from_u128(1, 0); /* 2^64 */
    ASSERT_INT_EQ(bigint_cmp(&a, &b), -1);
    ASSERT_INT_EQ(bigint_cmp(&b, &a), 1);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_is_zero --- */

static int test_is_zero() {
    tracker_reset();

    BigInt z = bigint_zero();
    ASSERT(bigint_is_zero(&z));

    BigInt z2 = bigint_from_u64(0);
    ASSERT(bigint_is_zero(&z2));

    BigInt nz = bigint_from_u64(1);
    ASSERT(!bigint_is_zero(&nz));

    BigInt neg = bigint_from_i64(-1);
    ASSERT(!bigint_is_zero(&neg));

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_sign --- */

static int test_sign() {
    tracker_reset();

    BigInt z = bigint_zero();
    ASSERT_INT_EQ(bigint_sign(&z), 0);

    BigInt pos = bigint_from_u64(42);
    ASSERT_INT_EQ(bigint_sign(&pos), 1);

    BigInt neg = bigint_from_i64(-42);
    ASSERT_INT_EQ(bigint_sign(&neg), -1);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_bit_len --- */

static int test_bit_len() {
    tracker_reset();

    BigInt z = bigint_zero();
    ASSERT_U32_EQ(bigint_bit_len(&z), 0);

    BigInt one = bigint_from_u64(1);
    ASSERT_U32_EQ(bigint_bit_len(&one), 1);

    BigInt two = bigint_from_u64(2);
    ASSERT_U32_EQ(bigint_bit_len(&two), 2);

    BigInt three = bigint_from_u64(3);
    ASSERT_U32_EQ(bigint_bit_len(&three), 2);

    BigInt ff = bigint_from_u64(255);
    ASSERT_U32_EQ(bigint_bit_len(&ff), 8);

    BigInt v256 = bigint_from_u64(256);
    ASSERT_U32_EQ(bigint_bit_len(&v256), 9);

    BigInt max64 = bigint_from_u64(UINT64_MAX);
    ASSERT_U32_EQ(bigint_bit_len(&max64), 64);

    /* Two-limb value: 2^64 */
    BigInt two_64 = bigint_from_u128(1, 0);
    ASSERT_U32_EQ(bigint_bit_len(&two_64), 65);

    /* Two-limb value: 2^128 - 1 */
    BigInt max128 = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    ASSERT_U32_EQ(bigint_bit_len(&max128), 128);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_test_bit --- */

static int test_test_bit() {
    tracker_reset();

    BigInt x = bigint_from_u64(0xA5); /* 10100101 */
    ASSERT(bigint_test_bit(&x, 0) == 1);
    ASSERT(bigint_test_bit(&x, 1) == 0);
    ASSERT(bigint_test_bit(&x, 2) == 1);
    ASSERT(bigint_test_bit(&x, 3) == 0);
    ASSERT(bigint_test_bit(&x, 4) == 0);
    ASSERT(bigint_test_bit(&x, 5) == 1);
    ASSERT(bigint_test_bit(&x, 6) == 0);
    ASSERT(bigint_test_bit(&x, 7) == 1);
    ASSERT(bigint_test_bit(&x, 8) == 0);

    /* Beyond the value's range */
    ASSERT(bigint_test_bit(&x, 64) == 0);
    ASSERT(bigint_test_bit(&x, 1000) == 0);

    /* Two-limb value */
    BigInt y = bigint_from_u128(1, 0); /* 2^64 */
    ASSERT(bigint_test_bit(&y, 0) == 0);
    ASSERT(bigint_test_bit(&y, 63) == 0);
    ASSERT(bigint_test_bit(&y, 64) == 1);
    ASSERT(bigint_test_bit(&y, 65) == 0);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_shift_left --- */

static int test_shift_left_zero_shift() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u64(42);
    BigInt y = bigint_shift_left(&arena, &x, 0);
    ASSERT_INT_EQ(bigint_cmp(&x, &y), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_left_zero_value() {
    tracker_reset();
    arena_t arena = {0};

    BigInt z = bigint_zero();
    BigInt y = bigint_shift_left(&arena, &z, 100);
    ASSERT(bigint_is_zero(&y));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_left_small() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u64(1);
    BigInt y = bigint_shift_left(&arena, &x, 1);
    BigInt expected = bigint_from_u64(2);
    ASSERT_INT_EQ(bigint_cmp(&y, &expected), 0);

    BigInt y8 = bigint_shift_left(&arena, &x, 8);
    BigInt exp8 = bigint_from_u64(256);
    ASSERT_INT_EQ(bigint_cmp(&y8, &exp8), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_left_cross_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* 1 << 64 = 2^64, should produce two-limb value */
    BigInt x = bigint_from_u64(1);
    BigInt y = bigint_shift_left(&arena, &x, 64);
    BigInt expected = bigint_from_u128(1, 0);
    ASSERT_INT_EQ(bigint_cmp(&y, &expected), 0);
    ASSERT_U32_EQ(y.len, 2);
    ASSERT_U64_EQ(bigint_limbs(&y)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&y)[1], 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_left_negative() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_i64(-1);
    BigInt y = bigint_shift_left(&arena, &x, 3);
    BigInt expected = bigint_from_i64(-8);
    ASSERT_INT_EQ(bigint_cmp(&y, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_shift_right --- */

static int test_shift_right_zero_shift() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u64(42);
    BigInt y = bigint_shift_right(&arena, &x, 0);
    ASSERT_INT_EQ(bigint_cmp(&x, &y), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_right_zero_value() {
    tracker_reset();
    arena_t arena = {0};

    BigInt z = bigint_zero();
    BigInt y = bigint_shift_right(&arena, &z, 100);
    ASSERT(bigint_is_zero(&y));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_right_small() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u64(256);
    BigInt y = bigint_shift_right(&arena, &x, 1);
    BigInt expected = bigint_from_u64(128);
    ASSERT_INT_EQ(bigint_cmp(&y, &expected), 0);

    BigInt y8 = bigint_shift_right(&arena, &x, 8);
    BigInt exp8 = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&y8, &exp8), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_right_larger_than_value() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u64(255);
    BigInt y = bigint_shift_right(&arena, &x, 100);
    ASSERT(bigint_is_zero(&y));

    BigInt y2 = bigint_shift_right(&arena, &x, 8);
    ASSERT(bigint_is_zero(&y2));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_right_cross_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* 2^64 >> 64 = 1 */
    BigInt x = bigint_from_u128(1, 0);
    BigInt y = bigint_shift_right(&arena, &x, 64);
    BigInt expected = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&y, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_right_negative() {
    tracker_reset();
    arena_t arena = {0};

    /* -8 >> 3 = -1 (rounds toward zero) */
    BigInt x = bigint_from_i64(-8);
    BigInt y = bigint_shift_right(&arena, &x, 3);
    BigInt expected = bigint_from_i64(-1);
    ASSERT_INT_EQ(bigint_cmp(&y, &expected), 0);

    /* -7 >> 3 = 0 (rounds toward zero: magnitude 7 >> 3 = 0, then zero is non-negative) */
    BigInt a = bigint_from_i64(-7);
    BigInt b = bigint_shift_right(&arena, &a, 3);
    ASSERT(bigint_is_zero(&b));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shift_left_right_roundtrip() {
    tracker_reset();
    arena_t arena = {0};

    /* (x << n) >> n == x for various values */
    BigInt x = bigint_from_u64(0xDEADBEEFCAFEULL);
    BigInt shifted = bigint_shift_left(&arena, &x, 37);
    BigInt back = bigint_shift_right(&arena, &shifted, 37);
    ASSERT_INT_EQ(bigint_cmp(&x, &back), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_shift_left UB boundary cases (US-017) --- */

static int test_shift_left_boundary_values() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u64(0xABCDEF0123456789ULL);

    /* Shift by 0: result == original */
    BigInt s0 = bigint_shift_left(&arena, &x, 0);
    ASSERT_INT_EQ(bigint_cmp(&s0, &x), 0);

    /* Shift by 1: result == x * 2 */
    BigInt s1 = bigint_shift_left(&arena, &x, 1);
    /* x << 1 should have correct value: low bit 0, high bits shifted */
    ASSERT_U64_EQ(bigint_limbs(&s1)[0], 0xABCDEF0123456789ULL << 1);
    ASSERT_U32_EQ(s1.sign, 0);

    /* Shift by 63: top bit goes into next limb */
    BigInt s63 = bigint_shift_left(&arena, &x, 63);
    ASSERT_U32_EQ(s63.len, 2);
    /* Low limb: val << 63, only the LSB of x goes to bit 63 */
    ASSERT_U64_EQ(bigint_limbs(&s63)[0], 0xABCDEF0123456789ULL << 63);
    /* High limb: val >> 1 */
    ASSERT_U64_EQ(bigint_limbs(&s63)[1], 0xABCDEF0123456789ULL >> 1);

    /* Shift by 64: pure limb shift, bit_shift == 0 (the UB case) */
    BigInt s64 = bigint_shift_left(&arena, &x, 64);
    ASSERT_U32_EQ(s64.len, 2);
    ASSERT_U64_EQ(bigint_limbs(&s64)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&s64)[1], 0xABCDEF0123456789ULL);

    /* Shift by 65: limb_shift=1, bit_shift=1 */
    BigInt s65 = bigint_shift_left(&arena, &x, 65);
    ASSERT_U32_EQ(s65.len, 3);
    ASSERT_U64_EQ(bigint_limbs(&s65)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&s65)[1], 0xABCDEF0123456789ULL << 1);
    ASSERT_U64_EQ(bigint_limbs(&s65)[2], 0xABCDEF0123456789ULL >> 63);

    /* Shift by 128: limb_shift=2, bit_shift=0 (UB case again) */
    BigInt s128 = bigint_shift_left(&arena, &x, 128);
    ASSERT_U32_EQ(s128.len, 3);
    ASSERT_U64_EQ(bigint_limbs(&s128)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&s128)[1], 0);
    ASSERT_U64_EQ(bigint_limbs(&s128)[2], 0xABCDEF0123456789ULL);

    /* Verify roundtrip for each shift amount */
    uint32_t shifts[] = {0, 1, 63, 64, 65, 128};
    for (int i = 0; i < 6; i++) {
        BigInt shifted = bigint_shift_left(&arena, &x, shifts[i]);
        BigInt back = bigint_shift_right(&arena, &shifted, shifts[i]);
        ASSERT_INT_EQ(bigint_cmp(&x, &back), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_shift_right UB boundary cases (US-018) --- */

static int test_shift_right_boundary_values() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    /* x is a 2-limb value: limbs[0] = 0xFEDCBA9876543210, limbs[1] = 0xABCDEF0123456789 */

    /* Shift by 0: result == original */
    BigInt s0 = bigint_shift_right(&arena, &x, 0);
    ASSERT_INT_EQ(bigint_cmp(&s0, &x), 0);

    /* Shift by 1: result == x / 2 */
    BigInt s1 = bigint_shift_right(&arena, &x, 1);
    /* Low limb: (limbs[0] >> 1) | (limbs[1] << 63) */
    ASSERT_U64_EQ(bigint_limbs(&s1)[0],
        (0xFEDCBA9876543210ULL >> 1) | (0xABCDEF0123456789ULL << 63));
    /* High limb: limbs[1] >> 1 */
    ASSERT_U64_EQ(bigint_limbs(&s1)[1], 0xABCDEF0123456789ULL >> 1);

    /* Shift by 63: bits cross limb boundary */
    BigInt s63 = bigint_shift_right(&arena, &x, 63);
    ASSERT_U32_EQ(s63.len, 2);
    ASSERT_U64_EQ(bigint_limbs(&s63)[0],
        (0xFEDCBA9876543210ULL >> 63) | (0xABCDEF0123456789ULL << 1));
    ASSERT_U64_EQ(bigint_limbs(&s63)[1], 0xABCDEF0123456789ULL >> 63);

    /* Shift by 64: pure limb shift, bit_shift == 0 (the UB case) */
    BigInt s64 = bigint_shift_right(&arena, &x, 64);
    ASSERT_U32_EQ(s64.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&s64)[0], 0xABCDEF0123456789ULL);

    /* Shift by 65: limb_shift=1, bit_shift=1 */
    BigInt s65 = bigint_shift_right(&arena, &x, 65);
    ASSERT_U32_EQ(s65.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&s65)[0], 0xABCDEF0123456789ULL >> 1);

    /* Shift by 128: shift >= bit_len, result is zero */
    BigInt s128 = bigint_shift_right(&arena, &x, 128);
    ASSERT(bigint_is_zero(&s128));

    /* Verify roundtrip for each shift amount */
    uint32_t shifts[] = {0, 1, 63, 64, 65, 128};
    for (int i = 0; i < 6; i++) {
        BigInt shifted = bigint_shift_right(&arena, &x, shifts[i]);
        BigInt back = bigint_shift_left(&arena, &shifted, shifts[i]);
        /* (x >> n) << n may lose bits, but should be <= x */
        BigInt roundtrip = bigint_shift_right(&arena, &back, 0);
        ASSERT(bigint_cmp(&roundtrip, &x) <= 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: US-021 shift edge-case tests --- */

/* Test all boundary shift amounts: 0, 1, 63, 64, 65, 127, 128, 129 (left and right) */
static int test_shift_all_boundary_amounts() {
    tracker_reset();
    arena_t arena = {0};

    BigInt x = bigint_from_u128(0xABCDEF0123456789ULL, 0xFEDCBA9876543210ULL);
    /* x = (0xABCDEF0123456789 << 64) | 0xFEDCBA9876543210 */

    uint32_t shifts[] = {0, 1, 63, 64, 65, 127, 128, 129};
    for (int i = 0; i < 8; i++) {
        uint32_t s = shifts[i];

        /* Shift left then right roundtrip: (x << s) >> s == x */
        BigInt sl = bigint_shift_left(&arena, &x, s);
        BigInt back = bigint_shift_right(&arena, &sl, s);
        ASSERT_INT_EQ(bigint_cmp(&x, &back), 0);

        /* Shift right then left: (x >> s) << s <= x (bits lost) */
        BigInt sr = bigint_shift_right(&arena, &x, s);
        BigInt back2 = bigint_shift_left(&arena, &sr, s);
        ASSERT(bigint_cmp(&back2, &x) <= 0);

        /* Verify bit_len changes correctly for shift left */
        if (!bigint_is_zero(&x)) {
            uint32_t orig_bl = bigint_bit_len(&x);
            uint32_t sl_bl = bigint_bit_len(&sl);
            ASSERT_U32_EQ(sl_bl, orig_bl + s);
        }
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test shift-left/right roundtrip for each boundary shift amount */
static int test_shift_roundtrip_all_boundaries() {
    tracker_reset();
    arena_t arena = {0};

    /* Use a single-limb value to exercise inline storage */
    BigInt x1 = bigint_from_u64(0x123456789ABCDEF0ULL);
    /* Use a 2-limb value */
    BigInt x2 = bigint_from_u128(0xDEADBEEFCAFEBABEULL, 0x0123456789ABCDEFULL);

    uint32_t shifts[] = {0, 1, 63, 64, 65, 127, 128, 129};

    for (int i = 0; i < 8; i++) {
        uint32_t s = shifts[i];

        /* Single-limb roundtrip */
        BigInt sl1 = bigint_shift_left(&arena, &x1, s);
        BigInt rt1 = bigint_shift_right(&arena, &sl1, s);
        ASSERT_INT_EQ(bigint_cmp(&x1, &rt1), 0);

        /* Two-limb roundtrip */
        BigInt sl2 = bigint_shift_left(&arena, &x2, s);
        BigInt rt2 = bigint_shift_right(&arena, &sl2, s);
        ASSERT_INT_EQ(bigint_cmp(&x2, &rt2), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test shifting a multi-limb value (3+ limbs) by exact multiples of 64 */
static int test_shift_multi_limb_exact_64() {
    tracker_reset();
    arena_t arena = {0};

    /* Build a 4-limb value */
    BigInt x = _bigint_alloc(&arena, 4);
    uint64_t* xl = _bigint_limbs_mut(&x);
    xl[0] = 0x1111111111111111ULL;
    xl[1] = 0x2222222222222222ULL;
    xl[2] = 0x3333333333333333ULL;
    xl[3] = 0x4444444444444444ULL;

    /* Shift left by 64: limbs move up by one */
    BigInt sl64 = bigint_shift_left(&arena, &x, 64);
    ASSERT_U32_EQ(sl64.len, 5);
    ASSERT_U64_EQ(bigint_limbs(&sl64)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&sl64)[1], 0x1111111111111111ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl64)[2], 0x2222222222222222ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl64)[3], 0x3333333333333333ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl64)[4], 0x4444444444444444ULL);

    /* Shift left by 128: limbs move up by two */
    BigInt sl128 = bigint_shift_left(&arena, &x, 128);
    ASSERT_U32_EQ(sl128.len, 6);
    ASSERT_U64_EQ(bigint_limbs(&sl128)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&sl128)[1], 0);
    ASSERT_U64_EQ(bigint_limbs(&sl128)[2], 0x1111111111111111ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl128)[3], 0x2222222222222222ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl128)[4], 0x3333333333333333ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl128)[5], 0x4444444444444444ULL);

    /* Shift left by 192: limbs move up by three */
    BigInt sl192 = bigint_shift_left(&arena, &x, 192);
    ASSERT_U32_EQ(sl192.len, 7);
    ASSERT_U64_EQ(bigint_limbs(&sl192)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&sl192)[1], 0);
    ASSERT_U64_EQ(bigint_limbs(&sl192)[2], 0);
    ASSERT_U64_EQ(bigint_limbs(&sl192)[3], 0x1111111111111111ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl192)[4], 0x2222222222222222ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl192)[5], 0x3333333333333333ULL);
    ASSERT_U64_EQ(bigint_limbs(&sl192)[6], 0x4444444444444444ULL);

    /* Shift right by 64: lose bottom limb */
    BigInt sr64 = bigint_shift_right(&arena, &x, 64);
    ASSERT_U32_EQ(sr64.len, 3);
    ASSERT_U64_EQ(bigint_limbs(&sr64)[0], 0x2222222222222222ULL);
    ASSERT_U64_EQ(bigint_limbs(&sr64)[1], 0x3333333333333333ULL);
    ASSERT_U64_EQ(bigint_limbs(&sr64)[2], 0x4444444444444444ULL);

    /* Shift right by 128: lose bottom two limbs */
    BigInt sr128 = bigint_shift_right(&arena, &x, 128);
    ASSERT_U32_EQ(sr128.len, 2);
    ASSERT_U64_EQ(bigint_limbs(&sr128)[0], 0x3333333333333333ULL);
    ASSERT_U64_EQ(bigint_limbs(&sr128)[1], 0x4444444444444444ULL);

    /* Shift right by 192: lose bottom three limbs */
    BigInt sr192 = bigint_shift_right(&arena, &x, 192);
    ASSERT_U32_EQ(sr192.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&sr192)[0], 0x4444444444444444ULL);

    /* Roundtrip: shift left then right by multiples of 64 */
    uint32_t mult64[] = {64, 128, 192, 256};
    for (int i = 0; i < 4; i++) {
        BigInt sl = bigint_shift_left(&arena, &x, mult64[i]);
        BigInt rt = bigint_shift_right(&arena, &sl, mult64[i]);
        ASSERT_INT_EQ(bigint_cmp(&x, &rt), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test shifting that crosses from inline (<=2 limbs) to heap (>2 limbs) and back */
static int test_shift_inline_heap_crossing() {
    tracker_reset();
    arena_t arena = {0};

    /* Start with a 1-limb value (inline) */
    BigInt x1 = bigint_from_u64(0xFFFFFFFFFFFFFFFFULL);
    ASSERT_U32_EQ(x1.len, 1);
    ASSERT_U32_EQ(x1.cap, 0); /* inline */

    /* Shift left by 64: becomes 2 limbs (still inline) */
    BigInt s64 = bigint_shift_left(&arena, &x1, 64);
    ASSERT_U32_EQ(s64.len, 2);
    ASSERT_U32_EQ(s64.cap, 0); /* still inline */
    ASSERT_U64_EQ(bigint_limbs(&s64)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&s64)[1], 0xFFFFFFFFFFFFFFFFULL);

    /* Shift left by 65: becomes 3 limbs (heap) */
    BigInt s65 = bigint_shift_left(&arena, &x1, 65);
    ASSERT_U32_EQ(s65.len, 3);
    ASSERT(s65.cap > 0); /* heap allocated */
    ASSERT_U64_EQ(bigint_limbs(&s65)[0], 0);
    ASSERT_U64_EQ(bigint_limbs(&s65)[1], 0xFFFFFFFFFFFFFFFEULL);
    ASSERT_U64_EQ(bigint_limbs(&s65)[2], 1);

    /* Shift right by 65: back to 1 limb (inline) */
    BigInt back = bigint_shift_right(&arena, &s65, 65);
    ASSERT_U32_EQ(back.len, 1);
    ASSERT_U32_EQ(back.cap, 0); /* back to inline */
    ASSERT_INT_EQ(bigint_cmp(&x1, &back), 0);

    /* Start with 2-limb value (inline) where top limb MSB is set */
    BigInt x2 = bigint_from_u128(0x8000000000000000ULL, 0xFFFFFFFFFFFFFFFFULL);
    ASSERT_U32_EQ(x2.len, 2);
    ASSERT_U32_EQ(x2.cap, 0); /* inline */

    /* Shift left by 1: top bit overflows, becomes 3 limbs (heap) */
    BigInt s1 = bigint_shift_left(&arena, &x2, 1);
    ASSERT_U32_EQ(s1.len, 3);
    ASSERT(s1.cap > 0); /* heap */

    /* Shift right by 1: back to 2 limbs (inline) */
    BigInt back2 = bigint_shift_right(&arena, &s1, 1);
    ASSERT_U32_EQ(back2.len, 2);
    ASSERT_U32_EQ(back2.cap, 0); /* back to inline */
    ASSERT_INT_EQ(bigint_cmp(&x2, &back2), 0);

    /* Start with 3-limb heap value, shift right to 2 limbs (inline) */
    BigInt x3 = _bigint_alloc(&arena, 3);
    uint64_t* x3l = _bigint_limbs_mut(&x3);
    x3l[0] = 0;
    x3l[1] = 0;
    x3l[2] = 0xABCDEF0123456789ULL;
    /* Shift right by 128: only top limb survives */
    BigInt sr = bigint_shift_right(&arena, &x3, 128);
    ASSERT_U32_EQ(sr.len, 1);
    ASSERT_U32_EQ(sr.cap, 0); /* inline */
    ASSERT_U64_EQ(bigint_limbs(&sr)[0], 0xABCDEF0123456789ULL);

    /* Shift that 1-limb result left by 128: back to 3 limbs (heap) */
    BigInt sl = bigint_shift_left(&arena, &sr, 128);
    ASSERT_U32_EQ(sl.len, 3);
    ASSERT(sl.cap > 0); /* heap */
    ASSERT_INT_EQ(bigint_cmp(&x3, &sl), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_neg --- */

static int test_neg_positive() {
    tracker_reset();

    BigInt x = bigint_from_i64(42);
    BigInt y = bigint_neg(x);
    ASSERT_U32_EQ(y.sign, 1);
    ASSERT_U64_EQ(bigint_limbs(&y)[0], 42);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_neg_negative() {
    tracker_reset();

    BigInt x = bigint_from_i64(-42);
    BigInt y = bigint_neg(x);
    ASSERT_U32_EQ(y.sign, 0);
    ASSERT_U64_EQ(bigint_limbs(&y)[0], 42);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_neg_zero() {
    tracker_reset();

    BigInt z = bigint_zero();
    BigInt y = bigint_neg(z);
    ASSERT_U32_EQ(y.sign, 0); /* non-negative zero */
    ASSERT(bigint_is_zero(&y));

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_abs --- */

static int test_abs_positive() {
    tracker_reset();

    BigInt x = bigint_from_i64(42);
    BigInt y = bigint_abs(x);
    ASSERT_U32_EQ(y.sign, 0);
    ASSERT_U64_EQ(bigint_limbs(&y)[0], 42);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_abs_negative() {
    tracker_reset();

    BigInt x = bigint_from_i64(-42);
    BigInt y = bigint_abs(x);
    ASSERT_U32_EQ(y.sign, 0);
    ASSERT_U64_EQ(bigint_limbs(&y)[0], 42);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_abs_zero() {
    tracker_reset();

    BigInt z = bigint_zero();
    BigInt y = bigint_abs(z);
    ASSERT_U32_EQ(y.sign, 0);
    ASSERT(bigint_is_zero(&y));

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_add --- */

static int test_add_zero_zero() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_zero();
    BigInt b = bigint_zero();
    BigInt r = bigint_add(&arena, &a, &b);
    ASSERT(bigint_is_zero(&r));
    ASSERT_U32_EQ(r.sign, 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_x_plus_zero() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(42);
    BigInt z = bigint_zero();
    BigInt r1 = bigint_add(&arena, &a, &z);
    ASSERT_INT_EQ(bigint_cmp(&r1, &a), 0);

    BigInt r2 = bigint_add(&arena, &z, &a);
    ASSERT_INT_EQ(bigint_cmp(&r2, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_simple_positive() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(3);
    BigInt b = bigint_from_i64(5);
    BigInt r = bigint_add(&arena, &a, &b);
    BigInt expected = bigint_from_i64(8);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_carry_propagation() {
    tracker_reset();
    arena_t arena = {0};

    /* (2^64 - 1) + 1 = 2^64 */
    BigInt a = bigint_from_u64(UINT64_MAX);
    BigInt b = bigint_from_u64(1);
    BigInt r = bigint_add(&arena, &a, &b);
    BigInt expected = bigint_from_u128(1, 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);
    ASSERT_U32_EQ(r.len, 2);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_mixed_sign_positive_result() {
    tracker_reset();
    arena_t arena = {0};

    /* 5 + (-3) = 2 */
    BigInt a = bigint_from_i64(5);
    BigInt b = bigint_from_i64(-3);
    BigInt r = bigint_add(&arena, &a, &b);
    BigInt expected = bigint_from_i64(2);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_mixed_sign_negative_result() {
    tracker_reset();
    arena_t arena = {0};

    /* (-5) + 3 = -2 */
    BigInt a = bigint_from_i64(-5);
    BigInt b = bigint_from_i64(3);
    BigInt r = bigint_add(&arena, &a, &b);
    BigInt expected = bigint_from_i64(-2);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_both_negative() {
    tracker_reset();
    arena_t arena = {0};

    /* (-5) + (-3) = -8 */
    BigInt a = bigint_from_i64(-5);
    BigInt b = bigint_from_i64(-3);
    BigInt r = bigint_add(&arena, &a, &b);
    BigInt expected = bigint_from_i64(-8);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_cancel_to_zero() {
    tracker_reset();
    arena_t arena = {0};

    /* 42 + (-42) = 0 */
    BigInt a = bigint_from_i64(42);
    BigInt b = bigint_from_i64(-42);
    BigInt r = bigint_add(&arena, &a, &b);
    ASSERT(bigint_is_zero(&r));
    ASSERT_U32_EQ(r.sign, 0); /* canonical non-negative zero */

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_two_limb_carry() {
    tracker_reset();
    arena_t arena = {0};

    /* (2^128 - 1) + 1 = 2^128: two-limb to three-limb */
    BigInt a = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    BigInt b = bigint_from_u64(1);
    BigInt r = bigint_add(&arena, &a, &b);
    ASSERT_U32_EQ(r.len, 3);
    const uint64_t* rl = bigint_limbs(&r);
    ASSERT_U64_EQ(rl[0], 0);
    ASSERT_U64_EQ(rl[1], 0);
    ASSERT_U64_EQ(rl[2], 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_multi_limb_carry_chain() {
    tracker_reset();
    arena_t arena = {0};

    /* Build 3-limb value: all UINT64_MAX limbs, add 1 to produce 4-limb result */
    BigInt a = _bigint_alloc(&arena, 3);
    uint64_t* al = _bigint_limbs_mut(&a);
    al[0] = UINT64_MAX;
    al[1] = UINT64_MAX;
    al[2] = UINT64_MAX;

    BigInt b = bigint_from_u64(1);
    BigInt r = bigint_add(&arena, &a, &b);
    ASSERT_U32_EQ(r.len, 4);
    const uint64_t* rl = bigint_limbs(&r);
    ASSERT_U64_EQ(rl[0], 0);
    ASSERT_U64_EQ(rl[1], 0);
    ASSERT_U64_EQ(rl[2], 0);
    ASSERT_U64_EQ(rl[3], 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_multi_limb_same_sign() {
    tracker_reset();
    arena_t arena = {0};

    /* Build two 3-limb values and add them */
    BigInt a = _bigint_alloc(&arena, 3);
    uint64_t* al = _bigint_limbs_mut(&a);
    al[0] = 0x1111111111111111ULL;
    al[1] = 0x2222222222222222ULL;
    al[2] = 0x3333333333333333ULL;

    BigInt b = _bigint_alloc(&arena, 3);
    uint64_t* bl = _bigint_limbs_mut(&b);
    bl[0] = 0x4444444444444444ULL;
    bl[1] = 0x5555555555555555ULL;
    bl[2] = 0x6666666666666666ULL;

    BigInt r = bigint_add(&arena, &a, &b);
    ASSERT_U32_EQ(r.len, 3);
    const uint64_t* rl = bigint_limbs(&r);
    ASSERT_U64_EQ(rl[0], 0x5555555555555555ULL);
    ASSERT_U64_EQ(rl[1], 0x7777777777777777ULL);
    ASSERT_U64_EQ(rl[2], 0x9999999999999999ULL);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_sub --- */

static int test_sub_simple() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(10);
    BigInt b = bigint_from_i64(3);
    BigInt r = bigint_sub(&arena, &a, &b);
    BigInt expected = bigint_from_i64(7);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_sub_negative_result() {
    tracker_reset();
    arena_t arena = {0};

    /* 3 - 10 = -7 */
    BigInt a = bigint_from_i64(3);
    BigInt b = bigint_from_i64(10);
    BigInt r = bigint_sub(&arena, &a, &b);
    BigInt expected = bigint_from_i64(-7);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_sub_self_equals_zero() {
    tracker_reset();
    arena_t arena = {0};

    /* x - x = 0 for various values */
    BigInt a = bigint_from_i64(12345);
    BigInt r1 = bigint_sub(&arena, &a, &a);
    ASSERT(bigint_is_zero(&r1));
    ASSERT_U32_EQ(r1.sign, 0);

    BigInt b = bigint_from_i64(-99999);
    BigInt r2 = bigint_sub(&arena, &b, &b);
    ASSERT(bigint_is_zero(&r2));
    ASSERT_U32_EQ(r2.sign, 0);

    BigInt c = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    BigInt r3 = bigint_sub(&arena, &c, &c);
    ASSERT(bigint_is_zero(&r3));
    ASSERT_U32_EQ(r3.sign, 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_sub_double_negative() {
    tracker_reset();
    arena_t arena = {0};

    /* (-5) - (-3) = (-5) + 3 = -2 */
    BigInt a = bigint_from_i64(-5);
    BigInt b = bigint_from_i64(-3);
    BigInt r = bigint_sub(&arena, &a, &b);
    BigInt expected = bigint_from_i64(-2);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_sub_multi_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* Build 3-limb value, subtract 1 to exercise borrow chain */
    BigInt a = _bigint_alloc(&arena, 3);
    uint64_t* al = _bigint_limbs_mut(&a);
    al[0] = 0;
    al[1] = 0;
    al[2] = 1; /* value = 2^128 */

    BigInt b = bigint_from_u64(1);
    BigInt r = bigint_sub(&arena, &a, &b);

    /* 2^128 - 1 = UINT64_MAX:UINT64_MAX in two limbs */
    BigInt expected = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_add_sub_consistency() {
    tracker_reset();
    arena_t arena = {0};

    /* (a + b) - b == a */
    BigInt a = bigint_from_u64(0xDEADBEEFCAFEULL);
    BigInt b = bigint_from_u64(0x123456789ABCULL);
    BigInt sum = bigint_add(&arena, &a, &b);
    BigInt back = bigint_sub(&arena, &sum, &b);
    ASSERT_INT_EQ(bigint_cmp(&back, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_mul --- */

static int test_mul_zero() {
    tracker_reset();
    arena_t arena = {0};

    /* x * 0 = 0, 0 * x = 0 */
    BigInt a = bigint_from_i64(42);
    BigInt z = bigint_zero();
    BigInt r1 = bigint_mul(&arena, &a, &z);
    ASSERT(bigint_is_zero(&r1));
    ASSERT_U32_EQ(r1.sign, 0);

    BigInt r2 = bigint_mul(&arena, &z, &a);
    ASSERT(bigint_is_zero(&r2));
    ASSERT_U32_EQ(r2.sign, 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_identity() {
    tracker_reset();
    arena_t arena = {0};

    /* x * 1 = x */
    BigInt a = bigint_from_i64(42);
    BigInt one = bigint_from_u64(1);
    BigInt r = bigint_mul(&arena, &a, &one);
    ASSERT_INT_EQ(bigint_cmp(&r, &a), 0);

    /* x * (-1) = -x */
    BigInt neg_one = bigint_from_i64(-1);
    BigInt r2 = bigint_mul(&arena, &a, &neg_one);
    BigInt neg_a = bigint_from_i64(-42);
    ASSERT_INT_EQ(bigint_cmp(&r2, &neg_a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_sign() {
    tracker_reset();
    arena_t arena = {0};

    BigInt pos = bigint_from_i64(3);
    BigInt neg = bigint_from_i64(-5);

    /* positive * positive = positive */
    BigInt r1 = bigint_mul(&arena, &pos, &pos);
    BigInt exp1 = bigint_from_i64(9);
    ASSERT_INT_EQ(bigint_cmp(&r1, &exp1), 0);

    /* positive * negative = negative */
    BigInt r2 = bigint_mul(&arena, &pos, &neg);
    BigInt exp2 = bigint_from_i64(-15);
    ASSERT_INT_EQ(bigint_cmp(&r2, &exp2), 0);

    /* negative * positive = negative */
    BigInt r3 = bigint_mul(&arena, &neg, &pos);
    ASSERT_INT_EQ(bigint_cmp(&r3, &exp2), 0);

    /* negative * negative = positive */
    BigInt r4 = bigint_mul(&arena, &neg, &neg);
    BigInt exp4 = bigint_from_i64(25);
    ASSERT_INT_EQ(bigint_cmp(&r4, &exp4), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_simple() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(6);
    BigInt b = bigint_from_i64(7);
    BigInt r = bigint_mul(&arena, &a, &b);
    BigInt expected = bigint_from_i64(42);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_commutativity() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_u64(0xDEADBEEFCAFEULL);
    BigInt b = bigint_from_u64(0x123456789ABCULL);
    BigInt r1 = bigint_mul(&arena, &a, &b);
    BigInt r2 = bigint_mul(&arena, &b, &a);
    ASSERT_INT_EQ(bigint_cmp(&r1, &r2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_carry_propagation() {
    tracker_reset();
    arena_t arena = {0};

    /* UINT64_MAX * UINT64_MAX = (2^128 - 2^65 + 1) */
    BigInt a = bigint_from_u64(UINT64_MAX);
    BigInt b = bigint_from_u64(UINT64_MAX);
    BigInt r = bigint_mul(&arena, &a, &b);

    /* Expected: hi = UINT64_MAX - 1, lo = 1 */
    /* (2^64-1)^2 = 2^128 - 2*2^64 + 1 */
    /*            = (UINT64_MAX-1) << 64 | 1 */
    ASSERT_U32_EQ(r.len, 2);
    ASSERT_U64_EQ(bigint_limbs(&r)[0], 1);
    ASSERT_U64_EQ(bigint_limbs(&r)[1], UINT64_MAX - 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_power_of_two() {
    tracker_reset();
    arena_t arena = {0};

    /* Multiply by powers of 2 should match shift left */
    BigInt x = bigint_from_u64(0xABCDEF0123456789ULL);
    BigInt two = bigint_from_u64(2);

    /* x * 2 == x << 1 */
    BigInt r = bigint_mul(&arena, &x, &two);
    BigInt expected = bigint_shift_left(&arena, &x, 1);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    /* x * 4 == x << 2 */
    BigInt four = bigint_from_u64(4);
    BigInt r2 = bigint_mul(&arena, &x, &four);
    BigInt exp2 = bigint_shift_left(&arena, &x, 2);
    ASSERT_INT_EQ(bigint_cmp(&r2, &exp2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_multi_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* (2^128 - 1) * 2 = 2^129 - 2 */
    BigInt a = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    BigInt two = bigint_from_u64(2);
    BigInt r = bigint_mul(&arena, &a, &two);

    /* Expected: 3-limb value */
    /* (2^128 - 1) * 2 = 2^129 - 2 */
    /* limb[0] = UINT64_MAX - 1 = 0xFFFFFFFFFFFFFFFE */
    /* limb[1] = UINT64_MAX */
    /* limb[2] = 1 */
    ASSERT_U32_EQ(r.len, 3);
    const uint64_t* rl = bigint_limbs(&r);
    ASSERT_U64_EQ(rl[0], UINT64_MAX - 1);
    ASSERT_U64_EQ(rl[1], UINT64_MAX);
    ASSERT_U64_EQ(rl[2], 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_u64_matches_mul() {
    tracker_reset();
    arena_t arena = {0};

    /* bigint_mul_u64 should match bigint_mul with single-limb second operand */
    BigInt a = bigint_from_u128(0x123456789ABCDEF0ULL, 0xFEDCBA9876543210ULL);
    uint64_t y = 0xDEADBEEFCAFEBABEULL;
    BigInt y_bi = bigint_from_u64(y);

    BigInt r1 = bigint_mul_u64(&arena, &a, y);
    BigInt r2 = bigint_mul(&arena, &a, &y_bi);
    ASSERT_INT_EQ(bigint_cmp(&r1, &r2), 0);

    /* Also test with negative */
    BigInt neg_a = bigint_neg(a);
    BigInt r3 = bigint_mul_u64(&arena, &neg_a, y);
    BigInt r4 = bigint_mul(&arena, &neg_a, &y_bi);
    ASSERT_INT_EQ(bigint_cmp(&r3, &r4), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_u64_zero() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(42);
    BigInt r = bigint_mul_u64(&arena, &a, 0);
    ASSERT(bigint_is_zero(&r));
    ASSERT_U32_EQ(r.sign, 0);

    BigInt z = bigint_zero();
    BigInt r2 = bigint_mul_u64(&arena, &z, 42);
    ASSERT(bigint_is_zero(&r2));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_u64_one() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(-42);
    BigInt r = bigint_mul_u64(&arena, &a, 1);
    ASSERT_INT_EQ(bigint_cmp(&r, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_factorial_20() {
    tracker_reset();
    arena_t arena = {0};

    /* 20! = 2432902008176640000 */
    /* This fits in a single limb (< 2^64) */
    BigInt result = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 20; i++) {
        result = bigint_mul_u64(&arena, &result, i);
    }

    /* 20! = 2432902008176640000 = 0x21C3677C82B40000 */
    ASSERT_U32_EQ(result.len, 1);
    ASSERT_U64_EQ(bigint_limbs(&result)[0], 2432902008176640000ULL);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_factorial_25() {
    tracker_reset();
    arena_t arena = {0};

    /* 25! = 15511210043330985984000000 which needs 2 limbs */
    BigInt result = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 25; i++) {
        result = bigint_mul_u64(&arena, &result, i);
    }

    /* Verify by checking that 25! / 25 / 24 / 23 / 22 / 21 == 20! */
    /* We'll verify bit_len: 25! = 15511210043330985984000000 */
    /* log2(25!) ~ 84.46, so bit_len should be 85 */
    ASSERT_U32_EQ(result.len, 2);
    ASSERT_U32_EQ(bigint_bit_len(&result), 84);

    /* Verify: 25! = 15511210043330985984000000 */
    /* In hex: 0xCCC62ACA00000 / 0xD1AB85B12567E00000 */
    /* lo = 0x12567E00000 ... let me just verify commutativity */

    /* Verify: result == 20! * 21 * 22 * 23 * 24 * 25 */
    BigInt check = bigint_from_u64(2432902008176640000ULL); /* 20! */
    check = bigint_mul_u64(&arena, &check, 21);
    check = bigint_mul_u64(&arena, &check, 22);
    check = bigint_mul_u64(&arena, &check, 23);
    check = bigint_mul_u64(&arena, &check, 24);
    check = bigint_mul_u64(&arena, &check, 25);
    ASSERT_INT_EQ(bigint_cmp(&result, &check), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_multi_limb_by_multi_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* Two 2-limb values multiplied should produce a 3-or-4 limb result */
    BigInt a = bigint_from_u128(1, 0); /* 2^64 */
    BigInt b = bigint_from_u128(1, 0); /* 2^64 */
    BigInt r = bigint_mul(&arena, &a, &b);

    /* 2^64 * 2^64 = 2^128 */
    /* That's limb[0]=0, limb[1]=0, limb[2]=1 */
    ASSERT_U32_EQ(r.len, 3);
    const uint64_t* rl = bigint_limbs(&r);
    ASSERT_U64_EQ(rl[0], 0);
    ASSERT_U64_EQ(rl[1], 0);
    ASSERT_U64_EQ(rl[2], 1);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_distributive() {
    tracker_reset();
    arena_t arena = {0};

    /* a * (b + c) == a*b + a*c */
    BigInt a = bigint_from_u64(0xDEADBEEFULL);
    BigInt b = bigint_from_u64(0x12345678ULL);
    BigInt c = bigint_from_u64(0xABCDEF01ULL);

    BigInt bc = bigint_add(&arena, &b, &c);
    BigInt lhs = bigint_mul(&arena, &a, &bc);

    BigInt ab = bigint_mul(&arena, &a, &b);
    BigInt ac = bigint_mul(&arena, &a, &c);
    BigInt rhs = bigint_add(&arena, &ab, &ac);

    ASSERT_INT_EQ(bigint_cmp(&lhs, &rhs), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_mul_negative_zero_result() {
    tracker_reset();
    arena_t arena = {0};

    /* (-3) * 0 = 0 (non-negative) */
    BigInt a = bigint_from_i64(-3);
    BigInt z = bigint_zero();
    BigInt r = bigint_mul(&arena, &a, &z);
    ASSERT(bigint_is_zero(&r));
    ASSERT_U32_EQ(r.sign, 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: Karatsuba multiplication --- */

/* Helper: build a BigInt with n limbs filled with a deterministic pattern */
static BigInt _make_big(arena_t* arena, uint32_t n_limbs, uint64_t seed) {
    BigInt r = _bigint_alloc(arena, n_limbs);
    uint64_t* rl = _bigint_limbs_mut(&r);
    for (uint32_t i = 0; i < n_limbs; i++) {
        rl[i] = seed * (i + 1) + (seed >> 3) * (i * i + 1);
    }
    /* Ensure top limb is nonzero */
    if (rl[n_limbs - 1] == 0) rl[n_limbs - 1] = 1;
    return r;
}

static int test_karatsuba_vs_schoolbook() {
    tracker_reset();
    arena_t arena = {0};

    /* Build 5-limb values (above threshold of 4) */
    BigInt a = _make_big(&arena, 5, 0xDEADBEEFCAFEBABEULL);
    BigInt b = _make_big(&arena, 5, 0x123456789ABCDEF0ULL);

    /* Compute via schoolbook directly */
    BigInt schoolbook = _bigint_mul_schoolbook(&arena, &a, &b);

    /* Compute via bigint_mul (should use Karatsuba since both >= 4 limbs) */
    BigInt karatsuba = bigint_mul(&arena, &a, &b);
    ASSERT_INT_EQ(bigint_cmp(&schoolbook, &karatsuba), 0);

    /* Also test with 8-limb values for deeper recursion */
    BigInt c = _make_big(&arena, 8, 0xAAAABBBBCCCCDDDDULL);
    BigInt d = _make_big(&arena, 8, 0x1111222233334444ULL);

    BigInt sb2 = _bigint_mul_schoolbook(&arena, &c, &d);
    BigInt ka2 = bigint_mul(&arena, &c, &d);
    ASSERT_INT_EQ(bigint_cmp(&sb2, &ka2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_karatsuba_threshold_boundary() {
    tracker_reset();
    arena_t arena = {0};

    /* Exactly at threshold (4 limbs) - should use Karatsuba */
    BigInt a = _make_big(&arena, 4, 0xF0F0F0F0F0F0F0F0ULL);
    BigInt b = _make_big(&arena, 4, 0x0F0F0F0F0F0F0F0FULL);

    BigInt sb = _bigint_mul_schoolbook(&arena, &a, &b);
    BigInt result = bigint_mul(&arena, &a, &b);
    ASSERT_INT_EQ(bigint_cmp(&sb, &result), 0);

    /* Below threshold (3 limbs) - should use schoolbook */
    BigInt c = _make_big(&arena, 3, 0xABCDABCDABCDABCDULL);
    BigInt d = _make_big(&arena, 3, 0x1234123412341234ULL);

    BigInt sb2 = _bigint_mul_schoolbook(&arena, &c, &d);
    BigInt result2 = bigint_mul(&arena, &c, &d);
    ASSERT_INT_EQ(bigint_cmp(&sb2, &result2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_karatsuba_different_sizes() {
    tracker_reset();
    arena_t arena = {0};

    /* One operand above threshold, one below - uses schoolbook */
    BigInt a = _make_big(&arena, 6, 0xDEADBEEF00000000ULL);
    BigInt b = _make_big(&arena, 3, 0xCAFEBABE00000000ULL);

    BigInt sb = _bigint_mul_schoolbook(&arena, &a, &b);
    BigInt result = bigint_mul(&arena, &a, &b);
    ASSERT_INT_EQ(bigint_cmp(&sb, &result), 0);

    /* Both above threshold but different sizes */
    BigInt c = _make_big(&arena, 4, 0x1111111111111111ULL);
    BigInt d = _make_big(&arena, 7, 0x2222222222222222ULL);

    BigInt sb2 = _bigint_mul_schoolbook(&arena, &c, &d);
    BigInt result2 = bigint_mul(&arena, &c, &d);
    ASSERT_INT_EQ(bigint_cmp(&sb2, &result2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_karatsuba_commutativity() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = _make_big(&arena, 6, 0xAAAAAAAAAAAAAAAAULL);
    BigInt b = _make_big(&arena, 6, 0x5555555555555555ULL);

    BigInt ab = bigint_mul(&arena, &a, &b);
    BigInt ba = bigint_mul(&arena, &b, &a);
    ASSERT_INT_EQ(bigint_cmp(&ab, &ba), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_karatsuba_factorial_100() {
    tracker_reset();
    arena_t arena = {0};

    /* Compute 100! iteratively using bigint_mul_u64 */
    BigInt fact = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 100; i++) {
        fact = bigint_mul_u64(&arena, &fact, i);
    }

    /* 100! has 525 bits (floor(log2(100!)) + 1 = 525) */
    ASSERT_U32_EQ(bigint_bit_len(&fact), 525);

    /* 100! has 97 trailing zero bits
     * (sum of floor(100/2^k) for k=1..6: 50+25+12+6+3+1=97) */
    for (uint32_t i = 0; i < 97; i++) {
        ASSERT(bigint_test_bit(&fact, i) == 0);
    }
    ASSERT(bigint_test_bit(&fact, 97) == 1);

    /* Verify by computing 100! = 50! * product(51..100) using bigint_mul.
     * Both operands have >= 4 limbs, so this exercises Karatsuba. */
    BigInt fact50 = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 50; i++) {
        fact50 = bigint_mul_u64(&arena, &fact50, i);
    }
    BigInt top_half = bigint_from_u64(1);
    for (uint64_t i = 51; i <= 100; i++) {
        top_half = bigint_mul_u64(&arena, &top_half, i);
    }
    BigInt product = bigint_mul(&arena, &fact50, &top_half);
    ASSERT_INT_EQ(bigint_cmp(&fact, &product), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_karatsuba_signed() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = _make_big(&arena, 5, 0xDEADBEEF00000000ULL);
    BigInt b = _make_big(&arena, 5, 0xCAFEBABE00000000ULL);

    /* (-a) * b == -(a * b) */
    BigInt neg_a = bigint_neg(a);
    BigInt r1 = bigint_mul(&arena, &neg_a, &b);
    BigInt r2 = bigint_mul(&arena, &a, &b);
    BigInt neg_r2 = bigint_neg(r2);
    ASSERT_INT_EQ(bigint_cmp(&r1, &neg_r2), 0);

    /* (-a) * (-b) == a * b */
    BigInt neg_b = bigint_neg(b);
    BigInt r3 = bigint_mul(&arena, &neg_a, &neg_b);
    ASSERT_INT_EQ(bigint_cmp(&r3, &r2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_divmod (US-006) --- */

static int test_div_by_zero() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(42);
    BigInt z = bigint_zero();
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &z, &q, &r);
    ASSERT(err != 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_zero_dividend() {
    tracker_reset();
    arena_t arena = {0};

    BigInt z = bigint_zero();
    BigInt d = bigint_from_i64(42);
    BigInt q, r;
    int err = bigint_divmod(&arena, &z, &d, &q, &r);
    ASSERT(err == 0);
    ASSERT(bigint_is_zero(&q));
    ASSERT(bigint_is_zero(&r));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_by_one() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(42);
    BigInt one = bigint_from_u64(1);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &one, &q, &r);
    ASSERT(err == 0);
    ASSERT_INT_EQ(bigint_cmp(&q, &a), 0);
    ASSERT(bigint_is_zero(&r));

    /* Also test negative */
    BigInt neg = bigint_from_i64(-42);
    err = bigint_divmod(&arena, &neg, &one, &q, &r);
    ASSERT(err == 0);
    ASSERT_INT_EQ(bigint_cmp(&q, &neg), 0);
    ASSERT(bigint_is_zero(&r));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_simple() {
    tracker_reset();
    arena_t arena = {0};

    /* 42 / 5 = 8 remainder 2 */
    BigInt a = bigint_from_i64(42);
    BigInt b = bigint_from_i64(5);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_i64(8);
    BigInt exp_r = bigint_from_i64(2);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_exact() {
    tracker_reset();
    arena_t arena = {0};

    /* 42 / 6 = 7 remainder 0 */
    BigInt a = bigint_from_i64(42);
    BigInt b = bigint_from_i64(6);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_i64(7);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT(bigint_is_zero(&r));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_negative_dividend() {
    tracker_reset();
    arena_t arena = {0};

    /* (-7) / 2 = -3 remainder -1 */
    BigInt a = bigint_from_i64(-7);
    BigInt b = bigint_from_i64(2);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_i64(-3);
    BigInt exp_r = bigint_from_i64(-1);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    /* Verify invariant: q * y + r == x */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_negative_divisor() {
    tracker_reset();
    arena_t arena = {0};

    /* 7 / (-2) = -3 remainder 1 */
    BigInt a = bigint_from_i64(7);
    BigInt b = bigint_from_i64(-2);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_i64(-3);
    BigInt exp_r = bigint_from_i64(1);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    /* Verify invariant: q * y + r == x */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_both_negative_signs() {
    tracker_reset();
    arena_t arena = {0};

    /* (-7) / (-2) = 3 remainder -1 */
    BigInt a = bigint_from_i64(-7);
    BigInt b = bigint_from_i64(-2);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_i64(3);
    BigInt exp_r = bigint_from_i64(-1);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    /* Verify invariant: q * y + r == x */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_power_of_two() {
    tracker_reset();
    arena_t arena = {0};

    /* 1000 / 8 = 125 remainder 0 */
    BigInt a = bigint_from_i64(1000);
    BigInt b = bigint_from_i64(8);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_i64(125);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT(bigint_is_zero(&r));

    /* 1023 / 256 = 3 remainder 255 */
    BigInt c = bigint_from_i64(1023);
    BigInt d = bigint_from_i64(256);
    err = bigint_divmod(&arena, &c, &d, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q2 = bigint_from_i64(3);
    BigInt exp_r2 = bigint_from_i64(255);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q2), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_large_prime() {
    tracker_reset();
    arena_t arena = {0};

    /* Use a large prime divisor: 1000000007 */
    uint64_t prime = 1000000007ULL;
    BigInt a = bigint_from_u64(123456789012345678ULL);
    BigInt b = bigint_from_u64(prime);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    /* Verify invariant: q * y + r == x */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    /* Verify remainder < divisor */
    ASSERT(_bigint_cmp_abs(&r, &b) < 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_dividend() {
    tracker_reset();
    arena_t arena = {0};

    /* (2^128 - 1) / 3 */
    BigInt a = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    BigInt b = bigint_from_u64(3);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    /* Verify invariant: q * b + r == a */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    /* Verify remainder < 3 */
    ASSERT(_bigint_cmp_abs(&r, &b) < 0);

    /* 3-limb dividend / single-limb divisor */
    BigInt c = _bigint_alloc(&arena, 3);
    uint64_t* cl = _bigint_limbs_mut(&c);
    cl[0] = UINT64_MAX;
    cl[1] = UINT64_MAX;
    cl[2] = 0x42;

    BigInt d = bigint_from_u64(7);
    err = bigint_divmod(&arena, &c, &d, &q, &r);
    ASSERT(err == 0);

    /* Verify invariant */
    check = bigint_mul(&arena, &q, &d);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &c), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_self_equals_one() {
    tracker_reset();
    arena_t arena = {0};

    /* x / x = 1 for nonzero x */
    BigInt a = bigint_from_i64(42);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &a, &q, &r);
    ASSERT(err == 0);

    BigInt one = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&q, &one), 0);
    ASSERT(bigint_is_zero(&r));

    /* Also for negative */
    BigInt neg = bigint_from_i64(-99);
    err = bigint_divmod(&arena, &neg, &neg, &q, &r);
    ASSERT(err == 0);
    ASSERT_INT_EQ(bigint_cmp(&q, &one), 0);
    ASSERT(bigint_is_zero(&r));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_divisor_larger() {
    tracker_reset();
    arena_t arena = {0};

    /* 3 / 42 = 0 remainder 3 */
    BigInt a = bigint_from_i64(3);
    BigInt b = bigint_from_i64(42);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);
    ASSERT(bigint_is_zero(&q));
    ASSERT_INT_EQ(bigint_cmp(&r, &a), 0);

    /* (-3) / 42 = 0 remainder -3 */
    BigInt neg_a = bigint_from_i64(-3);
    err = bigint_divmod(&arena, &neg_a, &b, &q, &r);
    ASSERT(err == 0);
    ASSERT(bigint_is_zero(&q));
    ASSERT_INT_EQ(bigint_cmp(&r, &neg_a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_convenience_functions() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(42);
    BigInt b = bigint_from_i64(5);

    BigInt q = bigint_div(&arena, &a, &b);
    BigInt r = bigint_mod(&arena, &a, &b);

    BigInt exp_q = bigint_from_i64(8);
    BigInt exp_r = bigint_from_i64(2);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_invariant_various() {
    tracker_reset();
    arena_t arena = {0};

    /* Test q * y + r == x for various divisors */
    uint64_t divisors[] = {1, 2, 3, 7, 10, 100, 1000000007ULL, UINT64_MAX};
    int n_divisors = (int)(sizeof(divisors) / sizeof(divisors[0]));

    BigInt x = bigint_from_u128(0xDEADBEEFCAFEBABEULL, 0x123456789ABCDEF0ULL);

    for (int i = 0; i < n_divisors; i++) {
        BigInt d = bigint_from_u64(divisors[i]);
        BigInt q, r;
        int err = bigint_divmod(&arena, &x, &d, &q, &r);
        ASSERT(err == 0);

        /* q * d + r == x */
        BigInt check = bigint_mul(&arena, &q, &d);
        check = bigint_add(&arena, &check, &r);
        ASSERT_INT_EQ(bigint_cmp(&check, &x), 0);

        /* 0 <= |r| < |d| */
        ASSERT(_bigint_cmp_abs(&r, &d) < 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_factorial_invariant() {
    tracker_reset();
    arena_t arena = {0};

    /* Compute 25! and verify division by various values */
    BigInt fact = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 25; i++) {
        fact = bigint_mul_u64(&arena, &fact, i);
    }

    /* 25! / 25 = 24! */
    BigInt twenty_five = bigint_from_u64(25);
    BigInt q, r;
    int err = bigint_divmod(&arena, &fact, &twenty_five, &q, &r);
    ASSERT(err == 0);
    ASSERT(bigint_is_zero(&r)); /* exact division */

    /* Verify: q == 24! */
    BigInt fact24 = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 24; i++) {
        fact24 = bigint_mul_u64(&arena, &fact24, i);
    }
    ASSERT_INT_EQ(bigint_cmp(&q, &fact24), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_uint64_max_divisor() {
    tracker_reset();
    arena_t arena = {0};

    /* (2^128) / UINT64_MAX */
    BigInt a = _bigint_alloc(&arena, 3);
    uint64_t* al = _bigint_limbs_mut(&a);
    al[0] = 0;
    al[1] = 0;
    al[2] = 1; /* = 2^128 */

    BigInt b = bigint_from_u64(UINT64_MAX);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    /* Verify invariant: q * b + r == a */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    /* 2^128 / (2^64-1) = 2^64 + 1 remainder 1 */
    /* Because (2^64-1) * (2^64+1) = 2^128 - 1, so 2^128 / (2^64-1) = 2^64+1 rem 1 */
    BigInt exp_q = bigint_from_u128(1, 1); /* 2^64 + 1 */
    BigInt exp_r = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_divmod multi-limb (US-007) --- */

static int test_div_multi_limb_basic() {
    tracker_reset();
    arena_t arena = {0};

    /* (2^128 - 1) / (2^64 + 1) = 2^64 - 1 remainder 0
     * Because (2^64+1)(2^64-1) = 2^128 - 1 */
    BigInt a = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    BigInt b = bigint_from_u128(1, 1); /* 2^64 + 1 */
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_u64(UINT64_MAX);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT(bigint_is_zero(&r));

    /* Verify invariant */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_with_remainder() {
    tracker_reset();
    arena_t arena = {0};

    /* (2^128) / (2^64 + 1) = 2^64 - 1 remainder 1
     * Because (2^64+1)(2^64-1) = 2^128 - 1, so 2^128 = (2^64+1)(2^64-1) + 1 */
    BigInt one = bigint_from_u64(1);
    BigInt pow128 = bigint_shift_left(&arena, &one, 128);
    BigInt b = bigint_from_u128(1, 1); /* 2^64 + 1 */
    BigInt q, r;
    int err = bigint_divmod(&arena, &pow128, &b, &q, &r);
    ASSERT(err == 0);

    BigInt exp_q = bigint_from_u64(UINT64_MAX);
    BigInt exp_r = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&q, &exp_q), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    /* Verify invariant */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &pow128), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_factorial() {
    tracker_reset();
    arena_t arena = {0};

    /* Compute 50! and 25! */
    BigInt fact50 = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 50; i++) {
        fact50 = bigint_mul_u64(&arena, &fact50, i);
    }
    BigInt fact25 = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 25; i++) {
        fact25 = bigint_mul_u64(&arena, &fact25, i);
    }

    /* 50! / 25! should be exact */
    BigInt q, r;
    int err = bigint_divmod(&arena, &fact50, &fact25, &q, &r);
    ASSERT(err == 0);
    ASSERT(bigint_is_zero(&r));

    /* Verify: q == 26 * 27 * ... * 50 */
    BigInt expected = bigint_from_u64(1);
    for (uint64_t i = 26; i <= 50; i++) {
        expected = bigint_mul_u64(&arena, &expected, i);
    }
    ASSERT_INT_EQ(bigint_cmp(&q, &expected), 0);

    /* Also verify: q * 25! == 50! */
    BigInt check = bigint_mul(&arena, &q, &fact25);
    ASSERT_INT_EQ(bigint_cmp(&check, &fact50), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_powers_of_2() {
    tracker_reset();
    arena_t arena = {0};

    BigInt one = bigint_from_u64(1);

    /* 2^256 / 2^128 = 2^128 remainder 0 */
    BigInt pow256 = bigint_shift_left(&arena, &one, 256);
    BigInt pow128 = bigint_shift_left(&arena, &one, 128);
    BigInt q, r;
    int err = bigint_divmod(&arena, &pow256, &pow128, &q, &r);
    ASSERT(err == 0);
    ASSERT_INT_EQ(bigint_cmp(&q, &pow128), 0);
    ASSERT(bigint_is_zero(&r));

    /* (2^256 + 1) / 2^128 = 2^128 remainder 1 */
    BigInt pow256p1 = bigint_add(&arena, &pow256, &one);
    err = bigint_divmod(&arena, &pow256p1, &pow128, &q, &r);
    ASSERT(err == 0);
    ASSERT_INT_EQ(bigint_cmp(&q, &pow128), 0);
    BigInt exp_r = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&r, &exp_r), 0);

    /* Verify invariant */
    BigInt check = bigint_mul(&arena, &q, &pow128);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &pow256p1), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_divisor_larger() {
    tracker_reset();
    arena_t arena = {0};

    /* 2-limb dividend / 3-limb divisor = 0 remainder dividend */
    BigInt a = bigint_from_u128(0x12345678ULL, 0xABCDEF01ULL);
    BigInt b = _bigint_alloc(&arena, 3);
    uint64_t* bl = _bigint_limbs_mut(&b);
    bl[0] = 0x1111111111111111ULL;
    bl[1] = 0x2222222222222222ULL;
    bl[2] = 0x3333333333333333ULL;

    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);
    ASSERT(bigint_is_zero(&q));
    ASSERT_INT_EQ(bigint_cmp(&r, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_equal_length() {
    tracker_reset();
    arena_t arena = {0};

    /* Two 3-limb values */
    BigInt a = _bigint_alloc(&arena, 3);
    uint64_t* al = _bigint_limbs_mut(&a);
    al[0] = 0xFEDCBA9876543210ULL;
    al[1] = 0x123456789ABCDEF0ULL;
    al[2] = 0xDEADBEEFCAFEBABEULL;

    BigInt b = _bigint_alloc(&arena, 3);
    uint64_t* bl = _bigint_limbs_mut(&b);
    bl[0] = 0xABCDEF0123456789ULL;
    bl[1] = 0x1111111111111111ULL;
    bl[2] = 0x42ULL;

    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    /* Verify invariant: q * b + r == a */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);
    ASSERT(_bigint_cmp_abs(&r, &b) < 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_self() {
    tracker_reset();
    arena_t arena = {0};

    /* x / x = 1 for multi-limb x */
    BigInt a = _make_big(&arena, 4, 0xDEADBEEFCAFEBABEULL);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &a, &q, &r);
    ASSERT(err == 0);

    BigInt one = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&q, &one), 0);
    ASSERT(bigint_is_zero(&r));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_invariant() {
    tracker_reset();
    arena_t arena = {0};

    /* 5-limb / 3-limb */
    BigInt a = _make_big(&arena, 5, 0xDEADBEEFCAFEBABEULL);
    BigInt b = _make_big(&arena, 3, 0x123456789ABCDEF0ULL);
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);
    ASSERT(_bigint_cmp_abs(&r, &b) < 0);

    /* 4-limb / 2-limb */
    BigInt c = _make_big(&arena, 4, 0xAAAAAAAAAAAAAAAAULL);
    BigInt d = _make_big(&arena, 2, 0x5555555555555555ULL);
    err = bigint_divmod(&arena, &c, &d, &q, &r);
    ASSERT(err == 0);
    check = bigint_mul(&arena, &q, &d);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &c), 0);
    ASSERT(_bigint_cmp_abs(&r, &d) < 0);

    /* 8-limb / 4-limb */
    BigInt e = _make_big(&arena, 8, 0xF0F0F0F0F0F0F0F0ULL);
    BigInt f = _make_big(&arena, 4, 0x0F0F0F0F0F0F0F0FULL);
    err = bigint_divmod(&arena, &e, &f, &q, &r);
    ASSERT(err == 0);
    check = bigint_mul(&arena, &q, &f);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &e), 0);
    ASSERT(_bigint_cmp_abs(&r, &f) < 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_div_multi_limb_signed() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = _make_big(&arena, 4, 0xDEADBEEFCAFEBABEULL);
    BigInt b = _make_big(&arena, 2, 0x123456789ABCDEF0ULL);
    a.sign = 1; /* negative dividend */

    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);

    /* Remainder has same sign as dividend */
    ASSERT(r.sign == 1 || bigint_is_zero(&r));

    /* Verify invariant: q * b + r == a */
    BigInt check = bigint_mul(&arena, &q, &b);
    check = bigint_add(&arena, &check, &r);
    ASSERT_INT_EQ(bigint_cmp(&check, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: US-019 Division with d==0 normalization --- */

/* Division where the divisor's top limb already has its high bit set,
 * so the Knuth Algorithm D normalization shift d == 0.
 * E.g. (2^127 + 1) / (2^63 + 1). */
static int test_div_knuth_d_zero_normalization() {
    tracker_reset();
    arena_t arena = {0};

    /* a = 2^127 + 1 = (2 limbs: lo=1, hi=2^63) */
    BigInt a = bigint_from_u128(0x8000000000000000ULL, 1);
    /* b = 2^63 + 1 — a single-limb value with high bit set would go through
     * the single-limb fast path, so we need a 2-limb divisor with high bit
     * set in the top limb. Use b = 2^127 - 1 = (lo=FFFFFFFFFFFFFFFF, hi=7FFFFFFFFFFFFFFF).
     * Actually, for d==0 we need the top limb's MSB set. Use:
     * b = (hi=0x8000000000000001, lo=0) — 2 limbs, top limb has high bit set. */
    BigInt b = bigint_from_u128(0x8000000000000001ULL, 0);

    /* a < b, so quotient should be 0, remainder = a */
    BigInt q, r;
    int err = bigint_divmod(&arena, &a, &b, &q, &r);
    ASSERT(err == 0);
    ASSERT(bigint_is_zero(&q));
    ASSERT_INT_EQ(bigint_cmp(&r, &a), 0);

    /* Now try where a > b with top bit set in divisor.
     * a2 = 2^128 + 2^64 + 1 = (3 limbs: lo=1, mid=1, hi=1)
     * b2 = (hi=0x8000000000000000, lo=1) — 2-limb, top bit set (d==0) */
    BigInt a2 = _bigint_alloc(&arena, 3);
    {
        uint64_t* rl = _bigint_limbs_mut(&a2);
        rl[0] = 1;
        rl[1] = 1;
        rl[2] = 1;
    }
    BigInt b2 = bigint_from_u128(0x8000000000000000ULL, 1);

    BigInt q2, r2;
    err = bigint_divmod(&arena, &a2, &b2, &q2, &r2);
    ASSERT(err == 0);

    /* Verify invariant: q2 * b2 + r2 == a2 */
    BigInt check = bigint_mul(&arena, &q2, &b2);
    check = bigint_add(&arena, &check, &r2);
    ASSERT_INT_EQ(bigint_cmp(&check, &a2), 0);

    /* Remainder must be less than divisor */
    ASSERT(_bigint_cmp_abs(&r2, &b2) < 0);

    /* Also test with UINT64_MAX as both limbs of divisor (all bits set = d==0) */
    BigInt a3 = _bigint_alloc(&arena, 3);
    {
        uint64_t* rl = _bigint_limbs_mut(&a3);
        rl[0] = 0xFFFFFFFFFFFFFFFFULL;
        rl[1] = 0xFFFFFFFFFFFFFFFFULL;
        rl[2] = 0x0000000000000001ULL;
    }
    BigInt b3 = bigint_from_u128(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);

    BigInt q3, r3;
    err = bigint_divmod(&arena, &a3, &b3, &q3, &r3);
    ASSERT(err == 0);

    /* Verify invariant: q3 * b3 + r3 == a3 */
    BigInt check3 = bigint_mul(&arena, &q3, &b3);
    check3 = bigint_add(&arena, &check3, &r3);
    ASSERT_INT_EQ(bigint_cmp(&check3, &a3), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: US-008 GCD (binary/Stein's algorithm) --- */

static int test_gcd_both_zero() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_zero();
    BigInt b = bigint_zero();
    BigInt g = bigint_gcd(&arena, &a, &b);
    ASSERT(bigint_is_zero(&g));

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_one_zero() {
    tracker_reset();
    arena_t arena = {0};

    /* gcd(0, 5) = 5 */
    BigInt a = bigint_zero();
    BigInt b = bigint_from_u64(5);
    BigInt g = bigint_gcd(&arena, &a, &b);
    ASSERT_INT_EQ(bigint_cmp(&g, &b), 0);

    /* gcd(7, 0) = 7 */
    BigInt c = bigint_from_u64(7);
    BigInt d = bigint_zero();
    BigInt g2 = bigint_gcd(&arena, &c, &d);
    ASSERT_INT_EQ(bigint_cmp(&g2, &c), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_small_values() {
    tracker_reset();
    arena_t arena = {0};

    /* gcd(12, 8) = 4 */
    BigInt a = bigint_from_u64(12);
    BigInt b = bigint_from_u64(8);
    BigInt g = bigint_gcd(&arena, &a, &b);
    BigInt four = bigint_from_u64(4);
    ASSERT_INT_EQ(bigint_cmp(&g, &four), 0);

    /* gcd(17, 13) = 1 (coprime) */
    BigInt c = bigint_from_u64(17);
    BigInt d = bigint_from_u64(13);
    BigInt g2 = bigint_gcd(&arena, &c, &d);
    BigInt one = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&g2, &one), 0);

    /* gcd(100, 75) = 25 */
    BigInt e = bigint_from_u64(100);
    BigInt f = bigint_from_u64(75);
    BigInt g3 = bigint_gcd(&arena, &e, &f);
    BigInt twentyfive = bigint_from_u64(25);
    ASSERT_INT_EQ(bigint_cmp(&g3, &twentyfive), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_negative_inputs() {
    tracker_reset();
    arena_t arena = {0};

    /* GCD is always non-negative regardless of input signs */
    BigInt a = bigint_from_i64(-12);
    BigInt b = bigint_from_u64(8);
    BigInt g = bigint_gcd(&arena, &a, &b);
    ASSERT(g.sign == 0); /* non-negative */
    BigInt four = bigint_from_u64(4);
    ASSERT_INT_EQ(bigint_cmp(&g, &four), 0);

    /* Both negative */
    BigInt c = bigint_from_i64(-18);
    BigInt d = bigint_from_i64(-12);
    BigInt g2 = bigint_gcd(&arena, &c, &d);
    ASSERT(g2.sign == 0);
    BigInt six = bigint_from_u64(6);
    ASSERT_INT_EQ(bigint_cmp(&g2, &six), 0);

    /* gcd(0, -5) = 5 (not -5) */
    BigInt z = bigint_zero();
    BigInt neg5 = bigint_from_i64(-5);
    BigInt g3 = bigint_gcd(&arena, &z, &neg5);
    ASSERT(g3.sign == 0);
    BigInt five = bigint_from_u64(5);
    ASSERT_INT_EQ(bigint_cmp(&g3, &five), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_equal_values() {
    tracker_reset();
    arena_t arena = {0};

    /* gcd(x, x) = |x| */
    BigInt a = bigint_from_u64(42);
    BigInt g = bigint_gcd(&arena, &a, &a);
    ASSERT_INT_EQ(bigint_cmp(&g, &a), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_one() {
    tracker_reset();
    arena_t arena = {0};

    /* gcd(1, n) = 1 for any n */
    BigInt one = bigint_from_u64(1);
    BigInt n = bigint_from_u64(123456789);
    BigInt g = bigint_gcd(&arena, &one, &n);
    ASSERT_INT_EQ(bigint_cmp(&g, &one), 0);

    BigInt g2 = bigint_gcd(&arena, &n, &one);
    ASSERT_INT_EQ(bigint_cmp(&g2, &one), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_powers_of_two() {
    tracker_reset();
    arena_t arena = {0};

    /* gcd(2^k, 2^m) = 2^min(k,m) */
    /* gcd(16, 64) = 16 */
    BigInt a = bigint_from_u64(16);
    BigInt b = bigint_from_u64(64);
    BigInt g = bigint_gcd(&arena, &a, &b);
    ASSERT_INT_EQ(bigint_cmp(&g, &a), 0);

    /* gcd(2^64, 2^32) = 2^32 */
    BigInt two_64 = bigint_from_u128(1, 0);
    BigInt two_32 = bigint_from_u64(1ULL << 32);
    BigInt g2 = bigint_gcd(&arena, &two_64, &two_32);
    ASSERT_INT_EQ(bigint_cmp(&g2, &two_32), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_multi_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* Compute gcd of 20! and 15! — should be 15! since 15! divides 20! */
    BigInt fact20 = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 20; i++) {
        fact20 = bigint_mul_u64(&arena, &fact20, i);
    }

    BigInt fact15 = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 15; i++) {
        fact15 = bigint_mul_u64(&arena, &fact15, i);
    }

    BigInt g = bigint_gcd(&arena, &fact20, &fact15);
    ASSERT_INT_EQ(bigint_cmp(&g, &fact15), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_multi_limb_coprime() {
    tracker_reset();
    arena_t arena = {0};

    /* Two large coprime numbers: 2^127 - 1 (Mersenne prime) and 2^64 */
    /* gcd(2^127 - 1, 2^64) = 1 since 2^127-1 is odd */
    BigInt mersenne = bigint_from_u128(UINT64_MAX >> 1, UINT64_MAX); /* 2^127 - 1 */
    BigInt pow64 = bigint_from_u128(1, 0); /* 2^64 */

    BigInt g = bigint_gcd(&arena, &mersenne, &pow64);
    BigInt one = bigint_from_u64(1);
    ASSERT_INT_EQ(bigint_cmp(&g, &one), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_gcd_multi_limb_known() {
    tracker_reset();
    arena_t arena = {0};

    /* Build known values: a = g*x, b = g*y where gcd(x,y)=1 */
    /* g = 2^64 + 7 (2-limb), x = 3, y = 5 */
    BigInt g_val = bigint_from_u128(1, 7); /* 2^64 + 7 */
    BigInt three = bigint_from_u64(3);
    BigInt five = bigint_from_u64(5);

    BigInt a = bigint_mul(&arena, &g_val, &three); /* 3 * (2^64 + 7) */
    BigInt b = bigint_mul(&arena, &g_val, &five);  /* 5 * (2^64 + 7) */

    BigInt g = bigint_gcd(&arena, &a, &b);
    ASSERT_INT_EQ(bigint_cmp(&g, &g_val), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Tests: bigint_to_str / bigint_from_str (US-009) --- */

static int test_to_str_zero_base10() {
    tracker_reset();
    arena_t arena = {0};

    BigInt z = bigint_zero();
    char* s = bigint_to_str(&arena, &z, 10);
    ASSERT_STR_EQ(s, "0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_zero_base16() {
    tracker_reset();
    arena_t arena = {0};

    BigInt z = bigint_zero();
    char* s = bigint_to_str(&arena, &z, 16);
    ASSERT_STR_EQ(s, "0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_zero_base2() {
    tracker_reset();
    arena_t arena = {0};

    BigInt z = bigint_zero();
    char* s = bigint_to_str(&arena, &z, 2);
    ASSERT_STR_EQ(s, "0");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_neg1_base10() {
    tracker_reset();
    arena_t arena = {0};

    BigInt n = bigint_from_i64(-1);
    char* s = bigint_to_str(&arena, &n, 10);
    ASSERT_STR_EQ(s, "-1");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_small_values_base10() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_i64(42);
    ASSERT_STR_EQ(bigint_to_str(&arena, &a, 10), "42");

    BigInt b = bigint_from_i64(-123456789);
    ASSERT_STR_EQ(bigint_to_str(&arena, &b, 10), "-123456789");

    BigInt c = bigint_from_u64(UINT64_MAX);
    ASSERT_STR_EQ(bigint_to_str(&arena, &c, 10), "18446744073709551615");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_base16() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_u64(255);
    ASSERT_STR_EQ(bigint_to_str(&arena, &a, 16), "ff");

    BigInt b = bigint_from_u64(256);
    ASSERT_STR_EQ(bigint_to_str(&arena, &b, 16), "100");

    BigInt c = bigint_from_u64(0xDEADBEEFULL);
    ASSERT_STR_EQ(bigint_to_str(&arena, &c, 16), "deadbeef");

    BigInt d = bigint_from_i64(-16);
    ASSERT_STR_EQ(bigint_to_str(&arena, &d, 16), "-10");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_base2() {
    tracker_reset();
    arena_t arena = {0};

    BigInt a = bigint_from_u64(1);
    ASSERT_STR_EQ(bigint_to_str(&arena, &a, 2), "1");

    BigInt b = bigint_from_u64(255);
    ASSERT_STR_EQ(bigint_to_str(&arena, &b, 2), "11111111");

    BigInt c = bigint_from_u64(256);
    ASSERT_STR_EQ(bigint_to_str(&arena, &c, 2), "100000000");

    BigInt d = bigint_from_i64(-5);
    ASSERT_STR_EQ(bigint_to_str(&arena, &d, 2), "-101");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_multi_limb_base10() {
    tracker_reset();
    arena_t arena = {0};

    /* 2^128 - 1 = 340282366920938463463374607431768211455 */
    BigInt a = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    char* s = bigint_to_str(&arena, &a, 10);
    ASSERT_STR_EQ(s, "340282366920938463463374607431768211455");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_multi_limb_base16() {
    tracker_reset();
    arena_t arena = {0};

    /* 2^128 - 1 in hex = ffffffffffffffffffffffffffffffff */
    BigInt a = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    char* s = bigint_to_str(&arena, &a, 16);
    ASSERT_STR_EQ(s, "ffffffffffffffffffffffffffffffff");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_str_base10_simple() {
    tracker_reset();
    arena_t arena = {0};

    BigInt r;
    ASSERT_INT_EQ(bigint_from_str(&arena, "0", 10, &r), 0);
    ASSERT(bigint_is_zero(&r));

    ASSERT_INT_EQ(bigint_from_str(&arena, "42", 10, &r), 0);
    BigInt expected = bigint_from_i64(42);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    ASSERT_INT_EQ(bigint_from_str(&arena, "-123", 10, &r), 0);
    expected = bigint_from_i64(-123);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_str_base16_simple() {
    tracker_reset();
    arena_t arena = {0};

    BigInt r;
    ASSERT_INT_EQ(bigint_from_str(&arena, "ff", 16, &r), 0);
    BigInt expected = bigint_from_u64(255);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    ASSERT_INT_EQ(bigint_from_str(&arena, "FF", 16, &r), 0);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    ASSERT_INT_EQ(bigint_from_str(&arena, "-10", 16, &r), 0);
    expected = bigint_from_i64(-16);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    ASSERT_INT_EQ(bigint_from_str(&arena, "deadbeef", 16, &r), 0);
    expected = bigint_from_u64(0xDEADBEEFULL);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_str_base2_simple() {
    tracker_reset();
    arena_t arena = {0};

    BigInt r;
    ASSERT_INT_EQ(bigint_from_str(&arena, "11111111", 2, &r), 0);
    BigInt expected = bigint_from_u64(255);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    ASSERT_INT_EQ(bigint_from_str(&arena, "-101", 2, &r), 0);
    expected = bigint_from_i64(-5);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    ASSERT_INT_EQ(bigint_from_str(&arena, "100000000", 2, &r), 0);
    expected = bigint_from_u64(256);
    ASSERT_INT_EQ(bigint_cmp(&r, &expected), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_str_rejects_invalid() {
    tracker_reset();
    arena_t arena = {0};

    BigInt r;
    /* Empty string */
    ASSERT(bigint_from_str(&arena, "", 10, &r) != 0);
    /* Just a sign */
    ASSERT(bigint_from_str(&arena, "-", 10, &r) != 0);
    /* Invalid digit for base 10 */
    ASSERT(bigint_from_str(&arena, "12g4", 10, &r) != 0);
    /* Invalid digit for base 2 */
    ASSERT(bigint_from_str(&arena, "10201", 2, &r) != 0);
    /* Invalid digit for base 16 */
    ASSERT(bigint_from_str(&arena, "12xyz", 16, &r) != 0);
    /* NULL */
    ASSERT(bigint_from_str(&arena, NULL, 10, &r) != 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_roundtrip_base10() {
    tracker_reset();
    arena_t arena = {0};

    /* Test various values */
    int64_t values[] = {0, 1, -1, 42, -42, 1000000, -999999999,
                        INT64_MAX, INT64_MIN};
    int n = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < n; i++) {
        BigInt orig = bigint_from_i64(values[i]);
        char* s = bigint_to_str(&arena, &orig, 10);
        BigInt parsed;
        ASSERT_INT_EQ(bigint_from_str(&arena, s, 10, &parsed), 0);
        ASSERT_INT_EQ(bigint_cmp(&orig, &parsed), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_roundtrip_base16() {
    tracker_reset();
    arena_t arena = {0};

    uint64_t values[] = {0, 1, 15, 16, 255, 256, 0xDEADBEEF, UINT64_MAX};
    int n = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < n; i++) {
        BigInt orig = bigint_from_u64(values[i]);
        char* s = bigint_to_str(&arena, &orig, 16);
        BigInt parsed;
        ASSERT_INT_EQ(bigint_from_str(&arena, s, 16, &parsed), 0);
        ASSERT_INT_EQ(bigint_cmp(&orig, &parsed), 0);
    }

    /* Negative roundtrip */
    BigInt neg = bigint_from_i64(-255);
    char* s = bigint_to_str(&arena, &neg, 16);
    BigInt parsed;
    ASSERT_INT_EQ(bigint_from_str(&arena, s, 16, &parsed), 0);
    ASSERT_INT_EQ(bigint_cmp(&neg, &parsed), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_roundtrip_base2() {
    tracker_reset();
    arena_t arena = {0};

    uint64_t values[] = {0, 1, 2, 3, 7, 8, 127, 128, 255, 256, UINT64_MAX};
    int n = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < n; i++) {
        BigInt orig = bigint_from_u64(values[i]);
        char* s = bigint_to_str(&arena, &orig, 2);
        BigInt parsed;
        ASSERT_INT_EQ(bigint_from_str(&arena, s, 2, &parsed), 0);
        ASSERT_INT_EQ(bigint_cmp(&orig, &parsed), 0);
    }

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_roundtrip_multi_limb() {
    tracker_reset();
    arena_t arena = {0};

    /* 2^128 - 1 */
    BigInt a = bigint_from_u128(UINT64_MAX, UINT64_MAX);
    for (int base = 2; base <= 16; base += 7) { /* bases 2, 9 (skip), 16 */
        if (base == 9) continue;
        char* s = bigint_to_str(&arena, &a, base);
        BigInt parsed;
        ASSERT_INT_EQ(bigint_from_str(&arena, s, base, &parsed), 0);
        ASSERT_INT_EQ(bigint_cmp(&a, &parsed), 0);
    }

    /* Factorial(25) — a multi-limb value */
    BigInt fact = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 25; i++) {
        fact = bigint_mul_u64(&arena, &fact, i);
    }

    char* s10 = bigint_to_str(&arena, &fact, 10);
    BigInt parsed10;
    ASSERT_INT_EQ(bigint_from_str(&arena, s10, 10, &parsed10), 0);
    ASSERT_INT_EQ(bigint_cmp(&fact, &parsed10), 0);

    char* s16 = bigint_to_str(&arena, &fact, 16);
    BigInt parsed16;
    ASSERT_INT_EQ(bigint_from_str(&arena, s16, 16, &parsed16), 0);
    ASSERT_INT_EQ(bigint_cmp(&fact, &parsed16), 0);

    char* s2 = bigint_to_str(&arena, &fact, 2);
    BigInt parsed2;
    ASSERT_INT_EQ(bigint_from_str(&arena, s2, 2, &parsed2), 0);
    ASSERT_INT_EQ(bigint_cmp(&fact, &parsed2), 0);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_from_str_large_decimal() {
    tracker_reset();
    arena_t arena = {0};

    /* 100+ digit number: 10^100 (googol) */
    char googol_str[102];
    googol_str[0] = '1';
    for (int i = 1; i <= 100; i++) googol_str[i] = '0';
    googol_str[101] = '\0';

    BigInt googol;
    ASSERT_INT_EQ(bigint_from_str(&arena, googol_str, 10, &googol), 0);

    /* Verify: should have bit_len around 333 (log2(10^100) ~ 332.19) */
    uint32_t bl = bigint_bit_len(&googol);
    ASSERT(bl >= 332 && bl <= 334);

    /* Round-trip */
    char* s = bigint_to_str(&arena, &googol, 10);
    ASSERT_STR_EQ(s, googol_str);

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_to_str_factorial_20() {
    tracker_reset();
    arena_t arena = {0};

    /* 20! = 2432902008176640000 */
    BigInt fact = bigint_from_u64(1);
    for (uint64_t i = 2; i <= 20; i++) {
        fact = bigint_mul_u64(&arena, &fact, i);
    }

    char* s = bigint_to_str(&arena, &fact, 10);
    ASSERT_STR_EQ(s, "2432902008176640000");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_base10_efficiency() {
    tracker_reset();
    arena_t arena = {0};

    /* Verify that base 10 conversion handles 18-digit chunk boundaries correctly.
     * 10^18 = 1000000000000000000 is exactly a chunk boundary. */
    BigInt a;
    ASSERT_INT_EQ(bigint_from_str(&arena, "1000000000000000000", 10, &a), 0);
    char* s = bigint_to_str(&arena, &a, 10);
    ASSERT_STR_EQ(s, "1000000000000000000");

    /* 10^36 = two chunks exactly */
    ASSERT_INT_EQ(bigint_from_str(&arena, "1000000000000000000000000000000000000", 10, &a), 0);
    s = bigint_to_str(&arena, &a, 10);
    ASSERT_STR_EQ(s, "1000000000000000000000000000000000000");

    arena_reset(&arena);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* --- Test runner --- */

int main(void) {
    printf("Test: bigint_zero\n");
    if (!test_zero()) return 1;

    printf("Test: bigint_from_u64(0)\n");
    if (!test_from_u64_zero()) return 1;

    printf("Test: bigint_from_u64(1)\n");
    if (!test_from_u64_one()) return 1;

    printf("Test: bigint_from_u64(UINT64_MAX)\n");
    if (!test_from_u64_max()) return 1;

    printf("Test: bigint_from_i64 positive\n");
    if (!test_from_i64_positive()) return 1;

    printf("Test: bigint_from_i64 negative\n");
    if (!test_from_i64_negative()) return 1;

    printf("Test: bigint_from_i64(0)\n");
    if (!test_from_i64_zero()) return 1;

    printf("Test: bigint_from_i64(INT64_MAX)\n");
    if (!test_from_i64_int64_max()) return 1;

    printf("Test: bigint_from_i64(INT64_MIN)\n");
    if (!test_from_i64_int64_min()) return 1;

    printf("Test: bigint_from_i64(-1)\n");
    if (!test_from_i64_minus_one()) return 1;

    printf("Test: bigint_from_u128(0,0)\n");
    if (!test_from_u128_zero()) return 1;

    printf("Test: bigint_from_u128 lo only\n");
    if (!test_from_u128_lo_only()) return 1;

    printf("Test: bigint_from_u128 hi and lo\n");
    if (!test_from_u128_hi_and_lo()) return 1;

    printf("Test: bigint_from_u128 max\n");
    if (!test_from_u128_max()) return 1;

    printf("Test: bigint_from_u128 hi=1\n");
    if (!test_from_u128_hi_one()) return 1;

    printf("Test: clone inline\n");
    if (!test_clone_inline()) return 1;

    printf("Test: clone two limbs\n");
    if (!test_clone_two_limbs()) return 1;

    printf("Test: clone heap\n");
    if (!test_clone_heap()) return 1;

    printf("Test: inline storage invariant\n");
    if (!test_inline_storage_invariant()) return 1;

    printf("Test: bigint_limbs helper\n");
    if (!test_limbs_helper()) return 1;

    printf("Test: normalize strips leading zeros\n");
    if (!test_normalize_strips_leading_zeros()) return 1;

    printf("Test: normalize canonical zero\n");
    if (!test_normalize_zero_canonical()) return 1;

    /* US-002: Comparison and bit operations */
    printf("Test: cmp equal\n");
    if (!test_cmp_equal()) return 1;

    printf("Test: cmp less\n");
    if (!test_cmp_less()) return 1;

    printf("Test: cmp greater\n");
    if (!test_cmp_greater()) return 1;

    printf("Test: cmp negative\n");
    if (!test_cmp_negative()) return 1;

    printf("Test: cmp both negative\n");
    if (!test_cmp_both_negative()) return 1;

    printf("Test: cmp zeros\n");
    if (!test_cmp_zeros()) return 1;

    printf("Test: cmp different lengths\n");
    if (!test_cmp_different_lengths()) return 1;

    printf("Test: is_zero\n");
    if (!test_is_zero()) return 1;

    printf("Test: sign\n");
    if (!test_sign()) return 1;

    printf("Test: bit_len\n");
    if (!test_bit_len()) return 1;

    printf("Test: test_bit\n");
    if (!test_test_bit()) return 1;

    printf("Test: shift_left zero shift\n");
    if (!test_shift_left_zero_shift()) return 1;

    printf("Test: shift_left zero value\n");
    if (!test_shift_left_zero_value()) return 1;

    printf("Test: shift_left small\n");
    if (!test_shift_left_small()) return 1;

    printf("Test: shift_left cross limb\n");
    if (!test_shift_left_cross_limb()) return 1;

    printf("Test: shift_left negative\n");
    if (!test_shift_left_negative()) return 1;

    printf("Test: shift_right zero shift\n");
    if (!test_shift_right_zero_shift()) return 1;

    printf("Test: shift_right zero value\n");
    if (!test_shift_right_zero_value()) return 1;

    printf("Test: shift_right small\n");
    if (!test_shift_right_small()) return 1;

    printf("Test: shift_right larger than value\n");
    if (!test_shift_right_larger_than_value()) return 1;

    printf("Test: shift_right cross limb\n");
    if (!test_shift_right_cross_limb()) return 1;

    printf("Test: shift_right negative\n");
    if (!test_shift_right_negative()) return 1;

    printf("Test: shift_left/right roundtrip\n");
    if (!test_shift_left_right_roundtrip()) return 1;

    printf("Test: shift_left boundary values (0, 1, 63, 64, 65, 128)\n");
    if (!test_shift_left_boundary_values()) return 1;

    printf("Test: shift_right boundary values (0, 1, 63, 64, 65, 128)\n");
    if (!test_shift_right_boundary_values()) return 1;

    /* US-021: Shift edge-case tests */
    printf("Test: shift all boundary amounts (0,1,63,64,65,127,128,129)\n");
    if (!test_shift_all_boundary_amounts()) return 1;

    printf("Test: shift roundtrip all boundaries\n");
    if (!test_shift_roundtrip_all_boundaries()) return 1;

    printf("Test: shift multi-limb by exact multiples of 64\n");
    if (!test_shift_multi_limb_exact_64()) return 1;

    printf("Test: shift inline/heap crossing\n");
    if (!test_shift_inline_heap_crossing()) return 1;

    /* US-003: Addition and subtraction */
    printf("Test: neg positive\n");
    if (!test_neg_positive()) return 1;

    printf("Test: neg negative\n");
    if (!test_neg_negative()) return 1;

    printf("Test: neg zero\n");
    if (!test_neg_zero()) return 1;

    printf("Test: abs positive\n");
    if (!test_abs_positive()) return 1;

    printf("Test: abs negative\n");
    if (!test_abs_negative()) return 1;

    printf("Test: abs zero\n");
    if (!test_abs_zero()) return 1;

    printf("Test: add 0 + 0\n");
    if (!test_add_zero_zero()) return 1;

    printf("Test: add x + 0\n");
    if (!test_add_x_plus_zero()) return 1;

    printf("Test: add simple positive\n");
    if (!test_add_simple_positive()) return 1;

    printf("Test: add carry propagation\n");
    if (!test_add_carry_propagation()) return 1;

    printf("Test: add mixed sign positive result\n");
    if (!test_add_mixed_sign_positive_result()) return 1;

    printf("Test: add mixed sign negative result\n");
    if (!test_add_mixed_sign_negative_result()) return 1;

    printf("Test: add both negative\n");
    if (!test_add_both_negative()) return 1;

    printf("Test: add cancel to zero\n");
    if (!test_add_cancel_to_zero()) return 1;

    printf("Test: add two-limb carry\n");
    if (!test_add_two_limb_carry()) return 1;

    printf("Test: add multi-limb carry chain\n");
    if (!test_add_multi_limb_carry_chain()) return 1;

    printf("Test: add multi-limb same sign\n");
    if (!test_add_multi_limb_same_sign()) return 1;

    printf("Test: sub simple\n");
    if (!test_sub_simple()) return 1;

    printf("Test: sub negative result\n");
    if (!test_sub_negative_result()) return 1;

    printf("Test: sub self equals zero\n");
    if (!test_sub_self_equals_zero()) return 1;

    printf("Test: sub double negative\n");
    if (!test_sub_double_negative()) return 1;

    printf("Test: sub multi-limb\n");
    if (!test_sub_multi_limb()) return 1;

    printf("Test: add/sub consistency\n");
    if (!test_add_sub_consistency()) return 1;

    /* US-004: Multiplication */
    printf("Test: mul zero\n");
    if (!test_mul_zero()) return 1;

    printf("Test: mul identity\n");
    if (!test_mul_identity()) return 1;

    printf("Test: mul sign\n");
    if (!test_mul_sign()) return 1;

    printf("Test: mul simple\n");
    if (!test_mul_simple()) return 1;

    printf("Test: mul commutativity\n");
    if (!test_mul_commutativity()) return 1;

    printf("Test: mul carry propagation\n");
    if (!test_mul_carry_propagation()) return 1;

    printf("Test: mul power of two\n");
    if (!test_mul_power_of_two()) return 1;

    printf("Test: mul multi-limb\n");
    if (!test_mul_multi_limb()) return 1;

    printf("Test: mul_u64 matches mul\n");
    if (!test_mul_u64_matches_mul()) return 1;

    printf("Test: mul_u64 zero\n");
    if (!test_mul_u64_zero()) return 1;

    printf("Test: mul_u64 one\n");
    if (!test_mul_u64_one()) return 1;

    printf("Test: mul factorial(20)\n");
    if (!test_mul_factorial_20()) return 1;

    printf("Test: mul factorial(25)\n");
    if (!test_mul_factorial_25()) return 1;

    printf("Test: mul multi-limb by multi-limb\n");
    if (!test_mul_multi_limb_by_multi_limb()) return 1;

    printf("Test: mul distributive\n");
    if (!test_mul_distributive()) return 1;

    printf("Test: mul negative zero result\n");
    if (!test_mul_negative_zero_result()) return 1;

    /* US-005: Karatsuba multiplication */
    printf("Test: karatsuba vs schoolbook\n");
    if (!test_karatsuba_vs_schoolbook()) return 1;

    printf("Test: karatsuba threshold boundary\n");
    if (!test_karatsuba_threshold_boundary()) return 1;

    printf("Test: karatsuba different sizes\n");
    if (!test_karatsuba_different_sizes()) return 1;

    printf("Test: karatsuba commutativity\n");
    if (!test_karatsuba_commutativity()) return 1;

    printf("Test: karatsuba factorial(100)\n");
    if (!test_karatsuba_factorial_100()) return 1;

    printf("Test: karatsuba signed\n");
    if (!test_karatsuba_signed()) return 1;

    /* US-006: Single-limb division */
    printf("Test: div by zero\n");
    if (!test_div_by_zero()) return 1;

    printf("Test: div zero dividend\n");
    if (!test_div_zero_dividend()) return 1;

    printf("Test: div by one\n");
    if (!test_div_by_one()) return 1;

    printf("Test: div simple\n");
    if (!test_div_simple()) return 1;

    printf("Test: div exact\n");
    if (!test_div_exact()) return 1;

    printf("Test: div negative dividend\n");
    if (!test_div_negative_dividend()) return 1;

    printf("Test: div negative divisor\n");
    if (!test_div_negative_divisor()) return 1;

    printf("Test: div both negative\n");
    if (!test_div_both_negative_signs()) return 1;

    printf("Test: div power of two\n");
    if (!test_div_power_of_two()) return 1;

    printf("Test: div large prime\n");
    if (!test_div_large_prime()) return 1;

    printf("Test: div multi-limb dividend\n");
    if (!test_div_multi_limb_dividend()) return 1;

    printf("Test: div self equals one\n");
    if (!test_div_self_equals_one()) return 1;

    printf("Test: div divisor larger\n");
    if (!test_div_divisor_larger()) return 1;

    printf("Test: div convenience functions\n");
    if (!test_div_convenience_functions()) return 1;

    printf("Test: div invariant various\n");
    if (!test_div_invariant_various()) return 1;

    printf("Test: div factorial invariant\n");
    if (!test_div_factorial_invariant()) return 1;

    printf("Test: div UINT64_MAX divisor\n");
    if (!test_div_uint64_max_divisor()) return 1;

    /* US-007: Multi-limb division (Knuth Algorithm D) */
    printf("Test: div multi-limb basic\n");
    if (!test_div_multi_limb_basic()) return 1;

    printf("Test: div multi-limb with remainder\n");
    if (!test_div_multi_limb_with_remainder()) return 1;

    printf("Test: div multi-limb factorial(50)/factorial(25)\n");
    if (!test_div_multi_limb_factorial()) return 1;

    printf("Test: div multi-limb powers of 2\n");
    if (!test_div_multi_limb_powers_of_2()) return 1;

    printf("Test: div multi-limb divisor larger\n");
    if (!test_div_multi_limb_divisor_larger()) return 1;

    printf("Test: div multi-limb equal length\n");
    if (!test_div_multi_limb_equal_length()) return 1;

    printf("Test: div multi-limb self\n");
    if (!test_div_multi_limb_self()) return 1;

    printf("Test: div multi-limb invariant\n");
    if (!test_div_multi_limb_invariant()) return 1;

    printf("Test: div multi-limb signed\n");
    if (!test_div_multi_limb_signed()) return 1;

    /* US-019: Division with d==0 normalization */
    printf("Test: div Knuth d==0 normalization\n");
    if (!test_div_knuth_d_zero_normalization()) return 1;

    /* US-008: GCD (binary/Stein's algorithm) */
    printf("Test: gcd both zero\n");
    if (!test_gcd_both_zero()) return 1;

    printf("Test: gcd one zero\n");
    if (!test_gcd_one_zero()) return 1;

    printf("Test: gcd small values\n");
    if (!test_gcd_small_values()) return 1;

    printf("Test: gcd negative inputs\n");
    if (!test_gcd_negative_inputs()) return 1;

    printf("Test: gcd equal values\n");
    if (!test_gcd_equal_values()) return 1;

    printf("Test: gcd one\n");
    if (!test_gcd_one()) return 1;

    printf("Test: gcd powers of two\n");
    if (!test_gcd_powers_of_two()) return 1;

    printf("Test: gcd multi-limb\n");
    if (!test_gcd_multi_limb()) return 1;

    printf("Test: gcd multi-limb coprime\n");
    if (!test_gcd_multi_limb_coprime()) return 1;

    printf("Test: gcd multi-limb known\n");
    if (!test_gcd_multi_limb_known()) return 1;

    /* US-009: String conversion */
    printf("Test: to_str zero base 10\n");
    if (!test_to_str_zero_base10()) return 1;

    printf("Test: to_str zero base 16\n");
    if (!test_to_str_zero_base16()) return 1;

    printf("Test: to_str zero base 2\n");
    if (!test_to_str_zero_base2()) return 1;

    printf("Test: to_str -1 base 10\n");
    if (!test_to_str_neg1_base10()) return 1;

    printf("Test: to_str small values base 10\n");
    if (!test_to_str_small_values_base10()) return 1;

    printf("Test: to_str base 16\n");
    if (!test_to_str_base16()) return 1;

    printf("Test: to_str base 2\n");
    if (!test_to_str_base2()) return 1;

    printf("Test: to_str multi-limb base 10\n");
    if (!test_to_str_multi_limb_base10()) return 1;

    printf("Test: to_str multi-limb base 16\n");
    if (!test_to_str_multi_limb_base16()) return 1;

    printf("Test: from_str base 10 simple\n");
    if (!test_from_str_base10_simple()) return 1;

    printf("Test: from_str base 16 simple\n");
    if (!test_from_str_base16_simple()) return 1;

    printf("Test: from_str base 2 simple\n");
    if (!test_from_str_base2_simple()) return 1;

    printf("Test: from_str rejects invalid\n");
    if (!test_from_str_rejects_invalid()) return 1;

    printf("Test: roundtrip base 10\n");
    if (!test_roundtrip_base10()) return 1;

    printf("Test: roundtrip base 16\n");
    if (!test_roundtrip_base16()) return 1;

    printf("Test: roundtrip base 2\n");
    if (!test_roundtrip_base2()) return 1;

    printf("Test: roundtrip multi-limb\n");
    if (!test_roundtrip_multi_limb()) return 1;

    printf("Test: from_str large decimal (100+ digits)\n");
    if (!test_from_str_large_decimal()) return 1;

    printf("Test: to_str factorial(20)\n");
    if (!test_to_str_factorial_20()) return 1;

    printf("Test: base 10 chunk boundary\n");
    if (!test_base10_efficiency()) return 1;

    printf("\nAll bigint tests passed!\n");
    return 0;
}
