/*
 * bigint.h -- Arbitrary-precision integer arithmetic
 *
 * Header-only module with arena-backed storage and inline small-number
 * optimization. Values with <= 2 limbs (128 bits) use inline storage
 * (no allocation); larger values use arena-allocated limb arrays.
 *
 * Define BIGINT_MALLOC/BIGINT_FREE before including for custom allocators.
 */
#ifndef BIGINT_H
#define BIGINT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef BIGINT_MALLOC
#include <stdlib.h>
#define BIGINT_MALLOC(sz) malloc(sz)
#define BIGINT_FREE(p)    free(p)
#endif

#ifndef BIGINT_KARATSUBA_THRESHOLD
#define BIGINT_KARATSUBA_THRESHOLD 32
#endif

/* --- Arena setup --- */

#define ARENA_IMPLEMENTATION
#include "../arena/arena.h"

/* --- BigInt type --- */

typedef struct {
    uint8_t  sign;  /* 0 = non-negative, 1 = negative */
    uint32_t len;   /* number of active limbs */
    uint32_t cap;   /* allocated capacity; 0 = inline storage */
    union {
        uint64_t  inline_limbs[2]; /* for len <= 2 */
        uint64_t* ptr;             /* arena-allocated for len > 2 */
    };
} BigInt;

/* --- Limb access --- */

static inline const uint64_t* bigint_limbs(const BigInt* x) {
    return (x->cap == 0) ? x->inline_limbs : x->ptr;
}

static inline uint64_t* _bigint_limbs_mut(BigInt* x) {
    return (x->cap == 0) ? x->inline_limbs : x->ptr;
}

/* --- Internal helpers --- */

/* Allocate a BigInt with n limbs. If n <= 2, uses inline storage. */
static BigInt _bigint_alloc(arena_t* arena, uint32_t n) {
    BigInt x;
    memset(&x, 0, sizeof(x));
    x.len = n;
    if (n <= 2) {
        x.cap = 0;
    } else {
        x.cap = n;
        x.ptr = (uint64_t*)arena_alloc(arena, sizeof(uint64_t) * n);
    }
    return x;
}

/* Strip leading zero limbs, maintaining at least 1 limb.
 * Converts heap -> inline if len shrinks to <= 2. */
static void _bigint_normalize(BigInt* x) {
    const uint64_t* limbs = bigint_limbs(x);
    while (x->len > 1 && limbs[x->len - 1] == 0) {
        x->len--;
    }
    /* Canonical zero is non-negative */
    if (x->len == 1 && limbs[0] == 0) {
        x->sign = 0;
    }
    /* If we shrank to <= 2 limbs and were using ptr, convert to inline */
    if (x->len <= 2 && x->cap > 0) {
        uint64_t lo = x->ptr[0];
        uint64_t hi = (x->len > 1) ? x->ptr[1] : 0;
        x->cap = 0;
        x->inline_limbs[0] = lo;
        x->inline_limbs[1] = hi;
    }
}

/* --- Construction --- */

static BigInt bigint_zero(void) {
    BigInt x;
    memset(&x, 0, sizeof(x));
    x.len = 1;
    /* sign=0, cap=0, inline_limbs[0]=0, inline_limbs[1]=0 from memset */
    return x;
}

static BigInt bigint_from_u64(uint64_t v) {
    BigInt x;
    memset(&x, 0, sizeof(x));
    x.len = 1;
    x.inline_limbs[0] = v;
    return x;
}

static BigInt bigint_from_i64(int64_t v) {
    if (v >= 0) {
        return bigint_from_u64((uint64_t)v);
    }
    BigInt x;
    memset(&x, 0, sizeof(x));
    x.sign = 1;
    x.len = 1;
    /* Handle INT64_MIN: -(INT64_MIN) overflows in signed,
     * but casting to uint64_t gives 2^63 which is the correct magnitude. */
    x.inline_limbs[0] = (uint64_t)(-(v + 1)) + 1;
    return x;
}

static BigInt bigint_from_u128(uint64_t hi, uint64_t lo) {
    BigInt x;
    memset(&x, 0, sizeof(x));
    if (hi == 0) {
        x.len = 1;
        x.inline_limbs[0] = lo;
        /* Normalize: zero stays len=1 */
    } else {
        x.len = 2;
        x.inline_limbs[0] = lo;
        x.inline_limbs[1] = hi;
    }
    return x;
}

static BigInt bigint_clone(arena_t* arena, const BigInt* src) {
    BigInt x;
    memset(&x, 0, sizeof(x));
    x.sign = src->sign;
    x.len = src->len;

    const uint64_t* src_limbs = bigint_limbs(src);

    if (src->len <= 2) {
        x.cap = 0;
        x.inline_limbs[0] = src_limbs[0];
        if (src->len > 1) {
            x.inline_limbs[1] = src_limbs[1];
        }
    } else {
        x.cap = src->len;
        x.ptr = (uint64_t*)arena_alloc(arena, sizeof(uint64_t) * src->len);
        memcpy(x.ptr, src_limbs, sizeof(uint64_t) * src->len);
    }
    return x;
}

/* --- Comparison --- */

/* Compare absolute values. Returns -1, 0, or +1. */
static int _bigint_cmp_abs(const BigInt* a, const BigInt* b) {
    if (a->len != b->len) {
        return (a->len > b->len) ? 1 : -1;
    }
    const uint64_t* al = bigint_limbs(a);
    const uint64_t* bl = bigint_limbs(b);
    for (uint32_t i = a->len; i > 0; i--) {
        if (al[i - 1] != bl[i - 1]) {
            return (al[i - 1] > bl[i - 1]) ? 1 : -1;
        }
    }
    return 0;
}

/* Compare two BigInts. Returns -1 if a < b, 0 if a == b, +1 if a > b. */
static int bigint_cmp(const BigInt* a, const BigInt* b) {
    int a_zero = (a->len == 1 && bigint_limbs(a)[0] == 0);
    int b_zero = (b->len == 1 && bigint_limbs(b)[0] == 0);

    if (a_zero && b_zero) return 0;
    if (a->sign && !b->sign) return -1;
    if (!a->sign && b->sign) return 1;

    /* Same sign */
    int cmp = _bigint_cmp_abs(a, b);
    return a->sign ? -cmp : cmp;
}

static int bigint_is_zero(const BigInt* x) {
    return (x->len == 1 && bigint_limbs(x)[0] == 0);
}

/* Returns -1, 0, or +1 */
static int bigint_sign(const BigInt* x) {
    if (bigint_is_zero(x)) return 0;
    return x->sign ? -1 : 1;
}

/* --- Bit operations --- */

/* Returns position of highest set bit + 1.
 * 0 for zero, 1 for 1, 8 for 255, 9 for 256. */
static uint32_t bigint_bit_len(const BigInt* x) {
    if (bigint_is_zero(x)) return 0;
    const uint64_t* limbs = bigint_limbs(x);
    uint64_t top = limbs[x->len - 1];
    /* Count leading zeros then derive bit position */
    uint32_t bits = 64;
    if (top == 0) return 0; /* shouldn't happen after normalize */
    /* Use a portable CLZ: shift-based */
    uint32_t clz = 0;
    uint64_t tmp = top;
    if (tmp <= 0x00000000FFFFFFFFULL) { clz += 32; tmp <<= 32; }
    if (tmp <= 0x0000FFFFFFFFFFFFULL) { clz += 16; tmp <<= 16; }
    if (tmp <= 0x00FFFFFFFFFFFFFFULL) { clz +=  8; tmp <<=  8; }
    if (tmp <= 0x0FFFFFFFFFFFFFFFULL) { clz +=  4; tmp <<=  4; }
    if (tmp <= 0x3FFFFFFFFFFFFFFFULL) { clz +=  2; tmp <<=  2; }
    if (tmp <= 0x7FFFFFFFFFFFFFFFULL) { clz +=  1; }
    bits = 64 - clz;
    return (uint32_t)(x->len - 1) * 64 + bits;
}

/* Test if bit at given position (0-indexed from LSB) is set. */
static int bigint_test_bit(const BigInt* x, uint32_t bit) {
    uint32_t limb_idx = bit / 64;
    uint32_t bit_idx  = bit % 64;
    if (limb_idx >= x->len) return 0;
    return (bigint_limbs(x)[limb_idx] >> bit_idx) & 1;
}

/* Shift left by `bits` positions. Returns new BigInt. */
static BigInt bigint_shift_left(arena_t* arena, const BigInt* x, uint32_t bits) {
    if (bigint_is_zero(x) || bits == 0) {
        return bigint_clone(arena, x);
    }

    uint32_t limb_shift = bits / 64;
    uint32_t bit_shift  = bits % 64;

    uint32_t new_len = x->len + limb_shift + (bit_shift > 0 ? 1 : 0);
    BigInt result = _bigint_alloc(arena, new_len);
    result.sign = x->sign;

    uint64_t* rl = _bigint_limbs_mut(&result);
    const uint64_t* xl = bigint_limbs(x);

    /* Zero out the low limbs */
    for (uint32_t i = 0; i < limb_shift; i++) {
        rl[i] = 0;
    }

    /* Shift the existing limbs.
     * When bit_shift == 0, (val >> 64) and (val << 64) would be UB,
     * so we use an explicit if to avoid evaluating those expressions. */
    uint64_t carry = 0;
    for (uint32_t i = 0; i < x->len; i++) {
        uint64_t val = xl[i];
        if (bit_shift > 0) {
            rl[i + limb_shift] = (val << bit_shift) | carry;
            carry = val >> (64 - bit_shift);
        } else {
            rl[i + limb_shift] = val;
        }
    }
    if (carry > 0) {
        rl[x->len + limb_shift] = carry;
    }

    _bigint_normalize(&result);
    return result;
}

/* Shift right by `bits` positions. Rounds toward zero for negative values.
 * Shift larger than value returns zero. */
static BigInt bigint_shift_right(arena_t* arena, const BigInt* x, uint32_t bits) {
    if (bigint_is_zero(x) || bits == 0) {
        return bigint_clone(arena, x);
    }

    uint32_t total_bits = bigint_bit_len(x);
    if (bits >= total_bits) {
        return bigint_zero();
    }

    uint32_t limb_shift = bits / 64;
    uint32_t bit_shift  = bits % 64;

    uint32_t src_len = x->len;
    uint32_t new_len = src_len - limb_shift;
    BigInt result = _bigint_alloc(arena, new_len);
    result.sign = x->sign;

    uint64_t* rl = _bigint_limbs_mut(&result);
    const uint64_t* xl = bigint_limbs(x);

    /* Shift the existing limbs.
     * When bit_shift == 0, (val << 64) would be UB,
     * so we use an explicit if to avoid evaluating that expression. */
    for (uint32_t i = 0; i < new_len; i++) {
        uint64_t val = xl[i + limb_shift];
        if (bit_shift > 0) {
            rl[i] = val >> bit_shift;
            if ((i + limb_shift + 1) < src_len) {
                rl[i] |= xl[i + limb_shift + 1] << (64 - bit_shift);
            }
        } else {
            rl[i] = val;
        }
    }

    _bigint_normalize(&result);
    return result;
}

/* --- Sign operations --- */

/* Returns -x (flips sign, no allocation). Neg of zero returns non-negative zero. */
static BigInt bigint_neg(BigInt x) {
    if (bigint_is_zero(&x)) {
        x.sign = 0;
    } else {
        x.sign = x.sign ? 0 : 1;
    }
    return x;
}

/* Returns |x| (clears sign, no allocation). */
static BigInt bigint_abs(BigInt x) {
    x.sign = 0;
    return x;
}

/* --- Addition and subtraction --- */

/* Add magnitudes |a| + |b|. Result is always non-negative.
 * Caller is responsible for setting the sign. */
static BigInt _bigint_add_abs(arena_t* arena, const BigInt* a, const BigInt* b) {
    uint32_t alen = a->len;
    uint32_t blen = b->len;
    uint32_t maxlen = (alen > blen) ? alen : blen;
    uint32_t new_len = maxlen + 1; /* room for carry */

    BigInt result = _bigint_alloc(arena, new_len);

    const uint64_t* al = bigint_limbs(a);
    const uint64_t* bl = bigint_limbs(b);
    uint64_t* rl = _bigint_limbs_mut(&result);

    uint64_t carry = 0;
    for (uint32_t i = 0; i < maxlen; i++) {
        uint64_t av = (i < alen) ? al[i] : 0;
        uint64_t bv = (i < blen) ? bl[i] : 0;
        uint64_t sum = av + bv + carry;
        /* Carry if sum wrapped around */
        carry = (sum < av || (carry && sum == av)) ? 1 : 0;
        rl[i] = sum;
    }
    rl[maxlen] = carry;

    _bigint_normalize(&result);
    return result;
}

/* Subtract magnitudes |a| - |b|. Requires |a| >= |b|.
 * Result is always non-negative. Caller sets sign. */
static BigInt _bigint_sub_abs(arena_t* arena, const BigInt* a, const BigInt* b) {
    uint32_t alen = a->len;
    uint32_t blen = b->len;

    BigInt result = _bigint_alloc(arena, alen);

    const uint64_t* al = bigint_limbs(a);
    const uint64_t* bl = bigint_limbs(b);
    uint64_t* rl = _bigint_limbs_mut(&result);

    uint64_t borrow = 0;
    for (uint32_t i = 0; i < alen; i++) {
        uint64_t av = al[i];
        uint64_t bv = (i < blen) ? bl[i] : 0;
        uint64_t diff = av - bv - borrow;
        /* Borrow if av < bv + borrow */
        borrow = (av < bv || (borrow && av == bv)) ? 1 : 0;
        rl[i] = diff;
    }
    /* borrow should be 0 since |a| >= |b| */

    _bigint_normalize(&result);
    return result;
}

/* Returns x + y with carry propagation across limbs. */
static BigInt bigint_add(arena_t* arena, const BigInt* x, const BigInt* y) {
    /* Same sign: add magnitudes, keep sign */
    if (x->sign == y->sign) {
        BigInt result = _bigint_add_abs(arena, x, y);
        result.sign = x->sign;
        /* Normalize handles canonical zero sign */
        if (bigint_is_zero(&result)) result.sign = 0;
        return result;
    }

    /* Different signs: subtract smaller magnitude from larger */
    int cmp = _bigint_cmp_abs(x, y);
    if (cmp == 0) {
        return bigint_zero(); /* x + (-x) = 0 */
    }

    if (cmp > 0) {
        /* |x| > |y|, result sign = sign(x) */
        BigInt result = _bigint_sub_abs(arena, x, y);
        result.sign = x->sign;
        if (bigint_is_zero(&result)) result.sign = 0;
        return result;
    } else {
        /* |y| > |x|, result sign = sign(y) */
        BigInt result = _bigint_sub_abs(arena, y, x);
        result.sign = y->sign;
        if (bigint_is_zero(&result)) result.sign = 0;
        return result;
    }
}

/* Returns x - y. Implemented as x + (-y). */
static BigInt bigint_sub(arena_t* arena, const BigInt* x, const BigInt* y) {
    BigInt neg_y = bigint_neg(*y);
    return bigint_add(arena, x, &neg_y);
}

/* --- Multiplication --- */

/* Forward declaration for Karatsuba mutual recursion */
static BigInt _bigint_mul_mag(arena_t* arena, const BigInt* x, const BigInt* y);

/* Multiply BigInt x by a single uint64_t value y. */
static BigInt bigint_mul_u64(arena_t* arena, const BigInt* x, uint64_t y) {
    if (y == 0 || bigint_is_zero(x)) {
        return bigint_zero();
    }

    uint32_t xlen = x->len;
    uint32_t new_len = xlen + 1; /* room for final carry */
    BigInt result = _bigint_alloc(arena, new_len);
    result.sign = x->sign;

    const uint64_t* xl = bigint_limbs(x);
    uint64_t* rl = _bigint_limbs_mut(&result);

    uint64_t carry = 0;
    for (uint32_t i = 0; i < xlen; i++) {
        __uint128_t prod = (__uint128_t)xl[i] * y + carry;
        rl[i] = (uint64_t)prod;
        carry = (uint64_t)(prod >> 64);
    }
    rl[xlen] = carry;

    _bigint_normalize(&result);
    return result;
}

/* Internal: build BigInt from limb array (result is non-negative). */
static BigInt _bigint_from_limbs(arena_t* arena, const uint64_t* limbs, uint32_t count) {
    if (count == 0) return bigint_zero();
    while (count > 1 && limbs[count - 1] == 0) count--;
    BigInt r = _bigint_alloc(arena, count);
    memcpy(_bigint_limbs_mut(&r), limbs, count * sizeof(uint64_t));
    _bigint_normalize(&r);
    return r;
}

/* Internal: schoolbook O(n*m) multiplication on magnitudes (both non-negative). */
static BigInt _bigint_mul_schoolbook(arena_t* arena, const BigInt* x, const BigInt* y) {
    uint32_t xlen = x->len;
    uint32_t ylen = y->len;
    uint32_t rlen = xlen + ylen;

    BigInt result = _bigint_alloc(arena, rlen);
    uint64_t* rl = _bigint_limbs_mut(&result);
    memset(rl, 0, sizeof(uint64_t) * rlen);

    const uint64_t* xl = bigint_limbs(x);
    const uint64_t* yl = bigint_limbs(y);

    for (uint32_t i = 0; i < xlen; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < ylen; j++) {
            __uint128_t prod = (__uint128_t)xl[i] * yl[j] + rl[i + j] + carry;
            rl[i + j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        rl[i + ylen] += carry;
    }

    _bigint_normalize(&result);
    return result;
}

/* Internal: Karatsuba multiplication on magnitudes (both non-negative).
 * Splits operands at midpoint, performs three half-size multiplications. */
static BigInt _bigint_karatsuba(arena_t* arena, const BigInt* x, const BigInt* y) {
    uint32_t xlen = x->len;
    uint32_t ylen = y->len;

    /* Base case: fall back to schoolbook */
    if (xlen < BIGINT_KARATSUBA_THRESHOLD || ylen < BIGINT_KARATSUBA_THRESHOLD) {
        return _bigint_mul_schoolbook(arena, x, y);
    }

    /* Split at midpoint of the smaller operand */
    uint32_t m = (xlen < ylen ? xlen : ylen) / 2;

    const uint64_t* xl = bigint_limbs(x);
    const uint64_t* yl = bigint_limbs(y);

    /* x = x1 * B^m + x0,  y = y1 * B^m + y0 */
    BigInt x0 = _bigint_from_limbs(arena, xl, m);
    BigInt x1 = _bigint_from_limbs(arena, xl + m, xlen - m);
    BigInt y0 = _bigint_from_limbs(arena, yl, m);
    BigInt y1 = _bigint_from_limbs(arena, yl + m, ylen - m);

    /* Three half-size multiplications */
    BigInt z0 = _bigint_mul_mag(arena, &x0, &y0);
    BigInt z2 = _bigint_mul_mag(arena, &x1, &y1);

    BigInt x_sum = bigint_add(arena, &x0, &x1);
    BigInt y_sum = bigint_add(arena, &y0, &y1);
    BigInt z1_full = _bigint_mul_mag(arena, &x_sum, &y_sum);
    BigInt z1_temp = bigint_sub(arena, &z1_full, &z0);
    BigInt z1 = bigint_sub(arena, &z1_temp, &z2);

    /* Combine: result = z2 * B^(2m) + z1 * B^m + z0 */
    BigInt z2_shifted = bigint_shift_left(arena, &z2, 2 * m * 64);
    BigInt z1_shifted = bigint_shift_left(arena, &z1, m * 64);
    BigInt result = bigint_add(arena, &z2_shifted, &z1_shifted);
    result = bigint_add(arena, &result, &z0);

    return result;
}

/* Internal: magnitude multiplication dispatch (Karatsuba or schoolbook). */
static BigInt _bigint_mul_mag(arena_t* arena, const BigInt* x, const BigInt* y) {
    if (x->len >= BIGINT_KARATSUBA_THRESHOLD && y->len >= BIGINT_KARATSUBA_THRESHOLD) {
        return _bigint_karatsuba(arena, x, y);
    }
    return _bigint_mul_schoolbook(arena, x, y);
}

/* Multiply x * y. Uses schoolbook O(n*m) for small operands, Karatsuba
 * when both operands have >= BIGINT_KARATSUBA_THRESHOLD limbs. */
static BigInt bigint_mul(arena_t* arena, const BigInt* x, const BigInt* y) {
    if (bigint_is_zero(x) || bigint_is_zero(y)) {
        return bigint_zero();
    }

    uint8_t result_sign = x->sign ^ y->sign;

    BigInt abs_x = bigint_abs(*x);
    BigInt abs_y = bigint_abs(*y);

    BigInt result = _bigint_mul_mag(arena, &abs_x, &abs_y);
    result.sign = result_sign;
    if (bigint_is_zero(&result)) result.sign = 0;
    return result;
}

/* --- Division --- */

#define BIGINT_ERR_DIV_ZERO 1

/* Internal: divide |x| by single-limb d (d > 0). Sets *q = |x|/d, returns |x| mod d.
 * Quotient sign is left at 0; caller must set it. */
static uint64_t _bigint_divmod_u64(arena_t* arena, const BigInt* x, uint64_t d, BigInt* q) {
    uint32_t xlen = x->len;
    BigInt result = _bigint_alloc(arena, xlen);
    uint64_t* rl = _bigint_limbs_mut(&result);
    const uint64_t* xl = bigint_limbs(x);

    __uint128_t carry = 0;
    for (uint32_t i = xlen; i > 0; i--) {
        __uint128_t cur = (carry << 64) | xl[i - 1];
        rl[i - 1] = (uint64_t)(cur / d);
        carry = cur % d;
    }

    _bigint_normalize(&result);
    *q = result;
    return (uint64_t)carry;
}

/* Internal: Knuth Algorithm D for multi-limb division.
 * Computes |x| / |y| where |y| has >= 2 limbs and |x| >= |y|.
 * Sets *q_out and *r_out (both non-negative). */
static void _bigint_divmod_knuth(arena_t* arena, const BigInt* x, const BigInt* y,
                                  BigInt* q_out, BigInt* r_out) {
    uint32_t n = y->len;  /* divisor limb count (>= 2) */
    uint32_t m = x->len - n;  /* quotient has m+1 digits */

    const uint64_t* vl = bigint_limbs(y);
    const uint64_t* ul = bigint_limbs(x);

    /* D1: Normalize — shift so high bit of v[n-1] is set */
    uint32_t d = 0;
    {
        uint64_t tmp = vl[n - 1];
        if (tmp <= 0x00000000FFFFFFFFULL) { d += 32; tmp <<= 32; }
        if (tmp <= 0x0000FFFFFFFFFFFFULL) { d += 16; tmp <<= 16; }
        if (tmp <= 0x00FFFFFFFFFFFFFFULL) { d +=  8; tmp <<=  8; }
        if (tmp <= 0x0FFFFFFFFFFFFFFFULL) { d +=  4; tmp <<=  4; }
        if (tmp <= 0x3FFFFFFFFFFFFFFFULL) { d +=  2; tmp <<=  2; }
        if (tmp <= 0x7FFFFFFFFFFFFFFFULL) { d +=  1; }
    }

    /* Normalized divisor v[0..n-1].
     * Guard with d > 0: when d == 0 the divisor's top limb already has its
     * high bit set, so no shift is needed and vl[i] >> (64 - 0) would be UB
     * (shift count == type width). */
    uint64_t* v = (uint64_t*)arena_alloc(arena, sizeof(uint64_t) * n);
    if (d > 0) {
        uint64_t carry = 0;
        for (uint32_t i = 0; i < n; i++) {
            v[i] = (vl[i] << d) | carry;
            carry = vl[i] >> (64 - d);
        }
    } else {
        memcpy(v, vl, sizeof(uint64_t) * n);
    }

    /* Normalized dividend u[0..m+n].
     * Same d > 0 guard as above: ul[i] >> (64 - 0) is UB when d == 0. */
    uint32_t ulen = m + n + 1;
    uint64_t* u = (uint64_t*)arena_alloc(arena, sizeof(uint64_t) * ulen);
    memset(u, 0, sizeof(uint64_t) * ulen);
    if (d > 0) {
        uint64_t carry = 0;
        for (uint32_t i = 0; i < x->len; i++) {
            u[i] = (ul[i] << d) | carry;
            carry = ul[i] >> (64 - d);
        }
        u[x->len] = carry;
    } else {
        memcpy(u, ul, sizeof(uint64_t) * x->len);
    }

    /* Quotient q[0..m] */
    uint64_t* ql = (uint64_t*)arena_alloc(arena, sizeof(uint64_t) * (m + 1));
    memset(ql, 0, sizeof(uint64_t) * (m + 1));

    /* D2-D7: Main loop */
    for (int32_t j = (int32_t)m; j >= 0; j--) {
        /* D3: Trial quotient */
        __uint128_t uu = ((__uint128_t)u[j + n] << 64) | u[j + n - 1];
        __uint128_t qhat = uu / v[n - 1];
        __uint128_t rhat = uu % v[n - 1];

        /* D4: Refine estimate — loop runs at most twice */
        for (;;) {
            if (qhat < ((__uint128_t)1 << 64) &&
                qhat * v[n - 2] <= ((rhat << 64) | u[j + n - 2])) {
                break;
            }
            qhat--;
            rhat += v[n - 1];
            if (rhat >= ((__uint128_t)1 << 64)) break;
        }

        /* D5: Multiply and subtract u[j..j+n] -= qhat * v[0..n-1] */
        uint64_t qd = (uint64_t)qhat;
        uint64_t carry = 0;
        for (uint32_t i = 0; i < n; i++) {
            __uint128_t prod = (__uint128_t)qd * v[i] + carry;
            uint64_t prod_lo = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
            if (u[j + i] < prod_lo) carry++;
            u[j + i] -= prod_lo;
        }
        int overflowed = (u[j + n] < carry);
        u[j + n] -= carry;

        /* D6: Add back if subtracted too much (rare) */
        if (overflowed) {
            qd--;
            uint64_t c = 0;
            for (uint32_t i = 0; i < n; i++) {
                __uint128_t sum = (__uint128_t)u[j + i] + v[i] + c;
                u[j + i] = (uint64_t)sum;
                c = (uint64_t)(sum >> 64);
            }
            u[j + n] += c;
        }

        ql[j] = qd;
    }

    /* Build quotient */
    *q_out = _bigint_from_limbs(arena, ql, m + 1);

    /* D8: Unnormalize remainder — shift u[0..n-1] right by d bits.
     * Guard with d > 0: u[i+1] << (64 - 0) is UB when d == 0, and no
     * unnormalization is needed since the divisor was never shifted. */
    if (d > 0) {
        for (uint32_t i = 0; i < n; i++) {
            u[i] = u[i] >> d;
            if (i + 1 < n) {
                u[i] |= u[i + 1] << (64 - d);
            }
        }
    }
    *r_out = _bigint_from_limbs(arena, u, n);
}

/* Compute quotient and remainder: x = q*y + r.
 * Remainder has same sign as dividend (truncated division, C99 semantics).
 * Returns 0 on success, nonzero on division by zero. */
static int bigint_divmod(arena_t* arena, const BigInt* x, const BigInt* y,
                         BigInt* q, BigInt* r) {
    if (bigint_is_zero(y)) {
        return BIGINT_ERR_DIV_ZERO;
    }

    if (bigint_is_zero(x)) {
        *q = bigint_zero();
        *r = bigint_zero();
        return 0;
    }

    uint8_t q_sign = x->sign ^ y->sign;
    uint8_t r_sign = x->sign;

    BigInt abs_x = bigint_abs(*x);
    BigInt abs_y = bigint_abs(*y);

    /* Divisor larger than dividend: quotient = 0, remainder = dividend */
    if (_bigint_cmp_abs(&abs_x, &abs_y) < 0) {
        *q = bigint_zero();
        *r = bigint_clone(arena, &abs_x);
        r->sign = r_sign;
        if (bigint_is_zero(r)) r->sign = 0;
        return 0;
    }

    /* Single-limb divisor fast path */
    if (abs_y.len == 1) {
        uint64_t d = bigint_limbs(&abs_y)[0];
        BigInt quotient;
        uint64_t remainder = _bigint_divmod_u64(arena, &abs_x, d, &quotient);

        quotient.sign = q_sign;
        if (bigint_is_zero(&quotient)) quotient.sign = 0;
        *q = quotient;

        *r = bigint_from_u64(remainder);
        r->sign = r_sign;
        if (bigint_is_zero(r)) r->sign = 0;
        return 0;
    }

    /* Multi-limb divisor: Knuth Algorithm D */
    BigInt quotient, remainder;
    _bigint_divmod_knuth(arena, &abs_x, &abs_y, &quotient, &remainder);

    quotient.sign = q_sign;
    if (bigint_is_zero(&quotient)) quotient.sign = 0;
    *q = quotient;

    remainder.sign = r_sign;
    if (bigint_is_zero(&remainder)) remainder.sign = 0;
    *r = remainder;
    return 0;
}

/* Returns quotient of x / y. Returns zero on division by zero. */
static BigInt bigint_div(arena_t* arena, const BigInt* x, const BigInt* y) {
    BigInt q, r;
    bigint_divmod(arena, x, y, &q, &r);
    return q;
}

/* Returns remainder of x % y. Returns zero on division by zero. */
static BigInt bigint_mod(arena_t* arena, const BigInt* x, const BigInt* y) {
    BigInt q, r;
    bigint_divmod(arena, x, y, &q, &r);
    return r;
}

/* --- GCD (binary / Stein's algorithm) --- */

/* Returns the number of trailing zero bits in |x|. Returns 0 for zero. */
static uint32_t _bigint_ctz(const BigInt* x) {
    if (bigint_is_zero(x)) return 0;
    const uint64_t* limbs = bigint_limbs(x);
    uint32_t count = 0;
    for (uint32_t i = 0; i < x->len; i++) {
        if (limbs[i] != 0) {
            /* Count trailing zeros in this limb */
            uint64_t v = limbs[i];
            uint32_t tz = 0;
            if ((v & 0xFFFFFFFF) == 0) { tz += 32; v >>= 32; }
            if ((v & 0x0000FFFF) == 0) { tz += 16; v >>= 16; }
            if ((v & 0x000000FF) == 0) { tz +=  8; v >>=  8; }
            if ((v & 0x0000000F) == 0) { tz +=  4; v >>=  4; }
            if ((v & 0x00000003) == 0) { tz +=  2; v >>=  2; }
            if ((v & 0x00000001) == 0) { tz +=  1; }
            return count + tz;
        }
        count += 64;
    }
    return count; /* all zero limbs — shouldn't happen after is_zero check */
}

/* GCD using binary/Stein's algorithm — uses only subtraction and shifts.
 * Result is always non-negative regardless of input signs. */
static BigInt bigint_gcd(arena_t* arena, const BigInt* x, const BigInt* y) {
    /* gcd(0, v) = |v|, gcd(u, 0) = |u|, gcd(0, 0) = 0 */
    if (bigint_is_zero(x)) {
        BigInt r = bigint_clone(arena, y);
        r.sign = 0;
        return r;
    }
    if (bigint_is_zero(y)) {
        BigInt r = bigint_clone(arena, x);
        r.sign = 0;
        return r;
    }

    /* Work with absolute values */
    BigInt u = bigint_abs(*x);
    BigInt v = bigint_abs(*y);

    /* Factor out common powers of 2 */
    uint32_t u_tz = _bigint_ctz(&u);
    uint32_t v_tz = _bigint_ctz(&v);
    uint32_t common_shift = (u_tz < v_tz) ? u_tz : v_tz;

    u = bigint_shift_right(arena, &u, u_tz);
    v = bigint_shift_right(arena, &v, v_tz);

    /* Now u is odd */
    for (;;) {
        /* Both u and v are odd here */
        int cmp = _bigint_cmp_abs(&u, &v);
        if (cmp == 0) break;

        /* Ensure u >= v */
        if (cmp < 0) {
            BigInt tmp = u; u = v; v = tmp;
        }

        /* u = u - v (both odd, so result is even and > 0) */
        u = _bigint_sub_abs(arena, &u, &v);

        /* Remove factors of 2 from u */
        uint32_t tz = _bigint_ctz(&u);
        u = bigint_shift_right(arena, &u, tz);
    }

    /* Restore common factors of 2 */
    BigInt result = bigint_shift_left(arena, &u, common_shift);
    result.sign = 0;
    return result;
}

/* --- String conversion --- */

#define BIGINT_ERR_INVALID_INPUT 2

/* Convert BigInt to null-terminated string in the given base (2, 10, or 16).
 * Negative numbers are prefixed with '-'. Base 16 uses lowercase hex digits.
 * Returns arena-allocated string. */
static char* bigint_to_str(arena_t* arena, const BigInt* x, int base) {
    if (bigint_is_zero(x)) {
        char* s = (char*)arena_alloc(arena, 2);
        s[0] = '0';
        s[1] = '\0';
        return s;
    }

    if (base == 16) {
        /* Direct nibble extraction from limbs */
        uint32_t bit_len = bigint_bit_len(x);
        uint32_t hex_digits = (bit_len + 3) / 4;
        uint32_t total = hex_digits + x->sign + 1; /* sign + digits + NUL */
        char* s = (char*)arena_alloc(arena, total);
        uint32_t pos = 0;
        if (x->sign) s[pos++] = '-';
        const uint64_t* limbs = bigint_limbs(x);
        for (uint32_t i = hex_digits; i > 0; i--) {
            uint32_t nibble_idx = i - 1;
            uint32_t limb_idx = nibble_idx / 16;
            uint32_t shift = (nibble_idx % 16) * 4;
            uint8_t nibble = (uint8_t)((limbs[limb_idx] >> shift) & 0xF);
            s[pos++] = "0123456789abcdef"[nibble];
        }
        s[pos] = '\0';
        return s;
    }

    if (base == 2) {
        /* Direct bit extraction */
        uint32_t bit_len = bigint_bit_len(x);
        uint32_t total = bit_len + x->sign + 1;
        char* s = (char*)arena_alloc(arena, total);
        uint32_t pos = 0;
        if (x->sign) s[pos++] = '-';
        const uint64_t* limbs = bigint_limbs(x);
        for (uint32_t i = bit_len; i > 0; i--) {
            uint32_t bit_idx = i - 1;
            uint32_t limb_idx = bit_idx / 64;
            uint32_t shift = bit_idx % 64;
            s[pos++] = ((limbs[limb_idx] >> shift) & 1) ? '1' : '0';
        }
        s[pos] = '\0';
        return s;
    }

    /* Base 10: repeated divmod by 10^18 (18 digits per division) */
    /* 10^18 = 1000000000000000000 — fits in uint64_t */
    const uint64_t chunk = 1000000000000000000ULL; /* 10^18 */

    /* Estimate max digits: ~0.302 digits per bit + sign + NUL */
    uint32_t bit_len = bigint_bit_len(x);
    uint32_t max_digits = (uint32_t)((uint64_t)bit_len * 3 / 10) + 3 + x->sign;
    char* buf = (char*)arena_alloc(arena, max_digits);

    /* Work with absolute value, extracting 18-digit chunks */
    BigInt val = bigint_abs(*x);
    uint32_t pos = 0;

    while (!bigint_is_zero(&val)) {
        BigInt q;
        uint64_t rem = _bigint_divmod_u64(arena, &val, chunk, &q);
        /* Extract up to 18 digits from remainder */
        for (int d = 0; d < 18; d++) {
            buf[pos++] = '0' + (char)(rem % 10);
            rem /= 10;
            if (bigint_is_zero(&q) && rem == 0) break;
        }
        val = q;
    }

    /* Add sign if negative */
    if (x->sign) buf[pos++] = '-';

    /* Reverse in place */
    for (uint32_t i = 0; i < pos / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[pos - 1 - i];
        buf[pos - 1 - i] = tmp;
    }
    buf[pos] = '\0';
    return buf;
}

/* Parse a null-terminated string in the given base (2, 10, or 16) into a BigInt.
 * Optional leading '-' for negative numbers. Base 16 accepts both upper and lowercase.
 * Returns 0 on success, nonzero on malformed input. Result is stored in *out. */
static int bigint_from_str(arena_t* arena, const char* str, int base, BigInt* out) {
    if (str == NULL || str[0] == '\0') return BIGINT_ERR_INVALID_INPUT;

    uint32_t pos = 0;
    uint8_t sign = 0;
    if (str[0] == '-') {
        sign = 1;
        pos = 1;
    }

    /* Must have at least one digit after optional sign */
    if (str[pos] == '\0') return BIGINT_ERR_INVALID_INPUT;

    if (base == 16) {
        /* Count hex digits */
        uint32_t start = pos;
        uint32_t len = 0;
        for (uint32_t i = pos; str[i] != '\0'; i++) {
            char c = str[i];
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                len++;
            } else {
                return BIGINT_ERR_INVALID_INPUT;
            }
        }
        if (len == 0) return BIGINT_ERR_INVALID_INPUT;

        /* Each hex digit = 4 bits, each limb = 64 bits = 16 hex digits */
        uint32_t n_limbs = (len + 15) / 16;
        BigInt result = _bigint_alloc(arena, n_limbs);
        uint64_t* rl = _bigint_limbs_mut(&result);
        memset(rl, 0, sizeof(uint64_t) * n_limbs);

        /* Parse from least significant (rightmost) to most significant */
        uint32_t end = start + len;
        for (uint32_t i = 0; i < len; i++) {
            char c = str[end - 1 - i];
            uint8_t digit;
            if (c >= '0' && c <= '9') digit = (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = (uint8_t)(c - 'a' + 10);
            else digit = (uint8_t)(c - 'A' + 10);
            rl[i / 16] |= (uint64_t)digit << ((i % 16) * 4);
        }

        result.sign = sign;
        _bigint_normalize(&result);
        if (bigint_is_zero(&result)) result.sign = 0;
        *out = result;
        return 0;
    }

    if (base == 2) {
        /* Count binary digits */
        uint32_t start = pos;
        uint32_t len = 0;
        for (uint32_t i = pos; str[i] != '\0'; i++) {
            if (str[i] == '0' || str[i] == '1') {
                len++;
            } else {
                return BIGINT_ERR_INVALID_INPUT;
            }
        }
        if (len == 0) return BIGINT_ERR_INVALID_INPUT;

        uint32_t n_limbs = (len + 63) / 64;
        BigInt result = _bigint_alloc(arena, n_limbs);
        uint64_t* rl = _bigint_limbs_mut(&result);
        memset(rl, 0, sizeof(uint64_t) * n_limbs);

        uint32_t end = start + len;
        for (uint32_t i = 0; i < len; i++) {
            if (str[end - 1 - i] == '1') {
                rl[i / 64] |= (uint64_t)1 << (i % 64);
            }
        }

        result.sign = sign;
        _bigint_normalize(&result);
        if (bigint_is_zero(&result)) result.sign = 0;
        *out = result;
        return 0;
    }

    /* Base 10: parse in chunks of 18 digits using multiply-and-add */
    /* Validate all digits first */
    uint32_t start = pos;
    uint32_t len = 0;
    for (uint32_t i = pos; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') return BIGINT_ERR_INVALID_INPUT;
        len++;
    }
    if (len == 0) return BIGINT_ERR_INVALID_INPUT;

    BigInt result = bigint_zero();

    /* Process in chunks of 18 digits from the left */
    uint32_t i = start;
    while (i < start + len) {
        /* Determine chunk size: first chunk may be shorter */
        uint32_t remaining = start + len - i;
        uint32_t chunk_len = remaining % 18;
        if (chunk_len == 0) chunk_len = 18;

        /* Parse this chunk as a uint64_t */
        uint64_t chunk_val = 0;
        uint64_t multiplier = 1;
        for (uint32_t d = 0; d < chunk_len; d++) {
            multiplier *= 10;
        }
        for (uint32_t d = 0; d < chunk_len; d++) {
            chunk_val = chunk_val * 10 + (uint64_t)(str[i + d] - '0');
        }

        /* result = result * multiplier + chunk_val */
        result = bigint_mul_u64(arena, &result, multiplier);
        BigInt cv = bigint_from_u64(chunk_val);
        result = bigint_add(arena, &result, &cv);

        i += chunk_len;

        /* Subsequent chunks are always 18 digits */
        /* (The first chunk handles the remainder; rest are full 18-digit chunks) */
    }

    result.sign = sign;
    if (bigint_is_zero(&result)) result.sign = 0;
    *out = result;
    return 0;
}

#endif /* BIGINT_H */
