/*
 * rational.h -- Exact rational number arithmetic
 *
 * Header-only module with automatic GCD normalization. Uses BigInt for
 * numerator and denominator. Numerator carries the sign; denominator
 * is always positive (>= 1).
 *
 * Define BIGINT_MALLOC/BIGINT_FREE before including for custom allocators
 * (bigint.h uses these too).
 */
#ifndef RATIONAL_H
#define RATIONAL_H

#include "bigint.h"

/* Forward-declare bigfloat types/functions we need. Callers who want
 * rational_to_bigfloat must include bigfloat.h before rational.h. */

#define RATIONAL_ERR_DIV_ZERO   1
#define RATIONAL_ERR_INVALID_INPUT 2

/* --- Rational type --- */

typedef struct {
    BigInt num;  /* numerator (carries sign) */
    BigInt den;  /* denominator (always positive, >= 1) */
} Rational;

/* --- Internal: normalize rational --- */

/* Divides num and den by their GCD; ensures den is positive
 * (moves sign to num). Zero is represented as 0/1. */
static Rational _rational_normalize(arena_t* arena, BigInt num, BigInt den) {
    Rational r;

    /* Handle zero numerator */
    if (bigint_is_zero(&num)) {
        r.num = bigint_zero();
        r.den = bigint_from_u64(1);
        return r;
    }

    /* Ensure denominator is positive */
    uint8_t result_sign = num.sign ^ den.sign;
    num = bigint_abs(num);
    den = bigint_abs(den);

    /* Divide by GCD */
    BigInt g = bigint_gcd(arena, &num, &den);
    BigInt one = bigint_from_u64(1);
    if (!bigint_is_zero(&g) && bigint_cmp(&g, &one) != 0) {
        num = bigint_div(arena, &num, &g);
        den = bigint_div(arena, &den, &g);
    }

    num.sign = result_sign;
    if (bigint_is_zero(&num)) num.sign = 0;

    r.num = num;
    r.den = den;
    return r;
}

/* --- Construction --- */

/* Construct a rational from two int64_t values. Normalizes by GCD.
 * Returns error if den == 0. */
static int rational_from_ints(arena_t* arena, int64_t num, int64_t den, Rational* out) {
    if (den == 0) return RATIONAL_ERR_DIV_ZERO;
    BigInt n = bigint_from_i64(num);
    BigInt d = bigint_from_i64(den);
    *out = _rational_normalize(arena, n, d);
    return 0;
}

/* Construct a rational from two BigInts. Normalizes by GCD.
 * Returns error if den is zero. */
static int rational_from_bigints(arena_t* arena, const BigInt* num, const BigInt* den, Rational* out) {
    if (bigint_is_zero(den)) return RATIONAL_ERR_DIV_ZERO;
    BigInt n = bigint_clone(arena, num);
    BigInt d = bigint_clone(arena, den);
    *out = _rational_normalize(arena, n, d);
    return 0;
}

/* --- Comparison --- */

/* Compare two rationals via cross-multiplication.
 * Returns -1, 0, or +1. */
static int rational_cmp(arena_t* arena, const Rational* a, const Rational* b) {
    /* a/b vs c/d  =>  a*d vs c*b */
    BigInt ad = bigint_mul(arena, &a->num, &b->den);
    BigInt cb = bigint_mul(arena, &b->num, &a->den);
    return bigint_cmp(&ad, &cb);
}

/* --- Arithmetic --- */

/* Returns a + b in lowest terms.
 * Uses lcm(b, d) as common denominator to reduce intermediate sizes:
 * g = gcd(b, d), num = a*(d/g) + c*(b/g), den = b/g * d */
static Rational rational_add(arena_t* arena, const Rational* x, const Rational* y) {
    BigInt g = bigint_gcd(arena, &x->den, &y->den);
    BigInt yd_g = bigint_div(arena, &y->den, &g);  /* y.den / g */
    BigInt xd_g = bigint_div(arena, &x->den, &g);  /* x.den / g */
    BigInt t1 = bigint_mul(arena, &x->num, &yd_g);  /* x.num * (y.den / g) */
    BigInt t2 = bigint_mul(arena, &y->num, &xd_g);  /* y.num * (x.den / g) */
    BigInt num = bigint_add(arena, &t1, &t2);
    BigInt den = bigint_mul(arena, &xd_g, &y->den);  /* (x.den / g) * y.den = lcm */
    return _rational_normalize(arena, num, den);
}

/* Returns a - b in lowest terms.
 * Uses lcm(b, d) as common denominator to reduce intermediate sizes. */
static Rational rational_sub(arena_t* arena, const Rational* x, const Rational* y) {
    BigInt g = bigint_gcd(arena, &x->den, &y->den);
    BigInt yd_g = bigint_div(arena, &y->den, &g);  /* y.den / g */
    BigInt xd_g = bigint_div(arena, &x->den, &g);  /* x.den / g */
    BigInt t1 = bigint_mul(arena, &x->num, &yd_g);  /* x.num * (y.den / g) */
    BigInt t2 = bigint_mul(arena, &y->num, &xd_g);  /* y.num * (x.den / g) */
    BigInt num = bigint_sub(arena, &t1, &t2);
    BigInt den = bigint_mul(arena, &xd_g, &y->den);  /* (x.den / g) * y.den = lcm */
    return _rational_normalize(arena, num, den);
}

/* Returns a * b in lowest terms.
 * Cross-cancels before multiplying to reduce intermediate sizes:
 * g1 = gcd(|x.num|, y.den), g2 = gcd(|y.num|, x.den)
 * num = (x.num/g1) * (y.num/g2), den = (x.den/g2) * (y.den/g1) */
static Rational rational_mul(arena_t* arena, const Rational* x, const Rational* y) {
    BigInt g1 = bigint_gcd(arena, &x->num, &y->den);
    BigInt g2 = bigint_gcd(arena, &y->num, &x->den);

    BigInt xn = bigint_div(arena, &x->num, &g1);
    BigInt yn = bigint_div(arena, &y->num, &g2);
    BigInt xd = bigint_div(arena, &x->den, &g2);
    BigInt yd = bigint_div(arena, &y->den, &g1);

    BigInt num = bigint_mul(arena, &xn, &yn);
    BigInt den = bigint_mul(arena, &xd, &yd);
    return _rational_normalize(arena, num, den);
}

/* Returns a / b in lowest terms.
 * (a/b) / (c/d) = (a*d) / (b*c)
 * Cross-cancels after flipping y: g1 = gcd(|x.num|, |y.num|), g2 = gcd(y.den, x.den)
 * num = (x.num/g1) * (y.den/g2), den = (x.den/g2) * (y.num/g1)
 * Returns error if y is zero. */
static int rational_div(arena_t* arena, const Rational* x, const Rational* y, Rational* out) {
    if (bigint_is_zero(&y->num)) return RATIONAL_ERR_DIV_ZERO;

    BigInt g1 = bigint_gcd(arena, &x->num, &y->num);
    BigInt g2 = bigint_gcd(arena, &y->den, &x->den);

    BigInt xn = bigint_div(arena, &x->num, &g1);
    BigInt yd = bigint_div(arena, &y->den, &g2);
    BigInt xd = bigint_div(arena, &x->den, &g2);
    BigInt yn = bigint_div(arena, &y->num, &g1);

    BigInt num = bigint_mul(arena, &xn, &yd);
    BigInt den = bigint_mul(arena, &xd, &yn);
    *out = _rational_normalize(arena, num, den);
    return 0;
}

/* --- String conversion --- */

/* Convert rational to string: "num/den" or "num" if den is 1.
 * Returns arena-allocated null-terminated string. */
static char* rational_to_str(arena_t* arena, const Rational* x) {
    char* num_str = bigint_to_str(arena, &x->num, 10);

    /* If denominator is 1, just return the numerator string */
    BigInt one = bigint_from_u64(1);
    if (bigint_cmp(&x->den, &one) == 0) {
        return num_str;
    }

    char* den_str = bigint_to_str(arena, &x->den, 10);

    uint32_t num_len = (uint32_t)strlen(num_str);
    uint32_t den_len = (uint32_t)strlen(den_str);
    uint32_t total = num_len + 1 + den_len + 1; /* num + '/' + den + NUL */
    char* s = (char*)arena_alloc(arena, total);
    memcpy(s, num_str, num_len);
    s[num_len] = '/';
    memcpy(s + num_len + 1, den_str, den_len);
    s[num_len + 1 + den_len] = '\0';
    return s;
}

/* Parse a string "num/den" or "num" into a Rational.
 * Returns 0 on success, nonzero on malformed input. */
static int rational_from_str(arena_t* arena, const char* str, Rational* out) {
    if (str == NULL || str[0] == '\0') return RATIONAL_ERR_INVALID_INPUT;

    /* Find '/' separator */
    const char* slash = strchr(str, '/');

    if (slash == NULL) {
        /* Just a numerator, denominator is 1 */
        BigInt num;
        if (bigint_from_str(arena, str, 10, &num)) return RATIONAL_ERR_INVALID_INPUT;
        out->num = num;
        out->den = bigint_from_u64(1);
        return 0;
    }

    /* Split at slash */
    uint32_t num_len = (uint32_t)(slash - str);
    if (num_len == 0) return RATIONAL_ERR_INVALID_INPUT;

    char* num_buf = (char*)arena_alloc(arena, num_len + 1);
    memcpy(num_buf, str, num_len);
    num_buf[num_len] = '\0';

    const char* den_start = slash + 1;
    if (*den_start == '\0') return RATIONAL_ERR_INVALID_INPUT;

    BigInt num, den;
    if (bigint_from_str(arena, num_buf, 10, &num)) return RATIONAL_ERR_INVALID_INPUT;
    if (bigint_from_str(arena, den_start, 10, &den)) return RATIONAL_ERR_INVALID_INPUT;
    if (bigint_is_zero(&den)) return RATIONAL_ERR_DIV_ZERO;

    *out = _rational_normalize(arena, num, den);
    return 0;
}

/* --- Conversion to BigFloat --- */

/* Convert rational to BigFloat by dividing numerator by denominator
 * at the given precision. Requires bigfloat.h to be included before rational.h. */
#ifdef BIGFLOAT_H
static BigFloat rational_to_bigfloat(arena_t* arena, const BigFloatCtx* ctx, const Rational* x) {
    BigFloat num_f = bigfloat_from_bigint(arena, ctx, &x->num);
    BigFloat den_f = bigfloat_from_bigint(arena, ctx, &x->den);
    return bigfloat_div(arena, ctx, &num_f, &den_f);
}
#endif

#endif /* RATIONAL_H */
