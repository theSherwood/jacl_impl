/*
 * bigfloat.h -- Arbitrary-precision floating-point arithmetic
 *
 * Header-only module with configurable bit precision and IEEE 754
 * rounding modes. Uses BigInt for the significand. Normalized
 * non-zero values have bit_len(significand) == precision.
 *
 * Representation: value = (-1)^sign * significand * 2^exponent
 * where significand is an unsigned integer with exactly `precision` bits
 * (the leading bit is always 1 for normalized values).
 *
 * Special values are represented via flags: ZERO, INF, NAN.
 *
 * Define BIGINT_MALLOC/BIGINT_FREE before including for custom allocators
 * (bigint.h uses these too).
 */
#ifndef BIGFLOAT_H
#define BIGFLOAT_H

#include "bigint.h"

/* --- Flags for special values --- */

#define BIGFLOAT_NORMAL 0
#define BIGFLOAT_ZERO   1
#define BIGFLOAT_INF    2
#define BIGFLOAT_NAN    3

/* --- Rounding modes --- */

#define BIGFLOAT_ROUND_NEAREST_EVEN  0
#define BIGFLOAT_ROUND_TOWARD_ZERO   1
#define BIGFLOAT_ROUND_TOWARD_POS_INF 2
#define BIGFLOAT_ROUND_TOWARD_NEG_INF 3

/* --- BigFloat context --- */

typedef struct {
    uint32_t precision;   /* minimum 2 bits */
    int      round_mode;
} BigFloatCtx;

/* --- BigFloat type --- */

typedef struct {
    uint8_t  sign;       /* 0 = positive, 1 = negative */
    uint8_t  flags;      /* BIGFLOAT_NORMAL, BIGFLOAT_ZERO, BIGFLOAT_INF, BIGFLOAT_NAN */
    int64_t  exponent;   /* binary exponent */
    BigInt   significand; /* unsigned magnitude; bit_len == precision for normalized values */
    uint32_t precision;  /* bit precision of this value */
} BigFloat;

/* --- Special value constructors --- */

/* Returns positive zero with the given precision. */
static BigFloat bigfloat_zero(uint32_t precision) {
    BigFloat f;
    memset(&f, 0, sizeof(f));
    f.flags = BIGFLOAT_ZERO;
    f.precision = precision;
    f.significand = bigint_zero();
    return f;
}

/* Returns +/- infinity. sign: 0 = positive, 1 = negative. */
static BigFloat bigfloat_inf(uint8_t sign) {
    BigFloat f;
    memset(&f, 0, sizeof(f));
    f.sign = sign ? 1 : 0;
    f.flags = BIGFLOAT_INF;
    f.significand = bigint_zero();
    return f;
}

/* Returns NaN. */
static BigFloat bigfloat_nan(void) {
    BigFloat f;
    memset(&f, 0, sizeof(f));
    f.flags = BIGFLOAT_NAN;
    f.significand = bigint_zero();
    return f;
}

/* --- Internal: normalize significand to exactly `precision` bits --- */

/* Adjusts significand and exponent so that bit_len(significand) == precision.
 * If significand has more bits, shift right and adjust exponent.
 * If significand has fewer bits, shift left and adjust exponent.
 * Rounding is applied when shifting right (losing bits). */
static BigFloat _bigfloat_normalize(arena_t* arena, const BigFloatCtx* ctx,
                                     BigInt significand, int64_t exponent, uint8_t sign) {
    BigFloat f;
    memset(&f, 0, sizeof(f));
    f.sign = sign;
    f.precision = ctx->precision;

    if (bigint_is_zero(&significand)) {
        f.flags = BIGFLOAT_ZERO;
        f.significand = bigint_zero();
        f.exponent = 0;
        return f;
    }

    uint32_t bit_len = bigint_bit_len(&significand);
    uint32_t prec = ctx->precision;

    if (bit_len == prec) {
        /* Already normalized */
        f.flags = BIGFLOAT_NORMAL;
        f.significand = significand;
        f.exponent = exponent;
        return f;
    }

    if (bit_len < prec) {
        /* Shift left to reach precision */
        uint32_t shift = prec - bit_len;
        f.significand = bigint_shift_left(arena, &significand, shift);
        f.exponent = exponent - (int64_t)shift;
        f.flags = BIGFLOAT_NORMAL;
        return f;
    }

    /* bit_len > prec: need to shift right, rounding */
    uint32_t shift = bit_len - prec;

    /* Extract guard, round, and sticky bits for rounding decision */
    int guard = 0, round_bit = 0, sticky = 0;
    if (shift >= 1) {
        guard = bigint_test_bit(&significand, shift - 1);
    }
    if (shift >= 2) {
        round_bit = bigint_test_bit(&significand, shift - 2);
    }
    /* Sticky: OR of all bits below guard and round */
    if (shift >= 3) {
        for (uint32_t i = 0; i < shift - 2; i++) {
            if (bigint_test_bit(&significand, i)) {
                sticky = 1;
                break;
            }
        }
    }

    BigInt shifted = bigint_shift_right(arena, &significand, shift);

    /* Determine whether to round up */
    int round_up = 0;
    switch (ctx->round_mode) {
        case BIGFLOAT_ROUND_NEAREST_EVEN:
            if (guard) {
                if (round_bit || sticky) {
                    round_up = 1;
                } else {
                    /* Tie: round to even (round up if LSB of result is 1) */
                    round_up = bigint_test_bit(&shifted, 0);
                }
            }
            break;
        case BIGFLOAT_ROUND_TOWARD_ZERO:
            /* Always truncate */
            break;
        case BIGFLOAT_ROUND_TOWARD_POS_INF:
            /* Round up if positive and any bits lost */
            if (!sign && (guard || round_bit || sticky)) {
                round_up = 1;
            }
            break;
        case BIGFLOAT_ROUND_TOWARD_NEG_INF:
            /* Round up (away from zero toward -inf) if negative and any bits lost */
            if (sign && (guard || round_bit || sticky)) {
                round_up = 1;
            }
            break;
    }

    if (round_up) {
        BigInt one = bigint_from_u64(1);
        shifted = bigint_add(arena, &shifted, &one);

        /* Rounding may have increased bit_len by 1 */
        if (bigint_bit_len(&shifted) > prec) {
            shifted = bigint_shift_right(arena, &shifted, 1);
            shift++;
        }
    }

    /* Check if rounding made it zero (shouldn't happen but be safe) */
    if (bigint_is_zero(&shifted)) {
        f.flags = BIGFLOAT_ZERO;
        f.significand = bigint_zero();
        f.exponent = 0;
        return f;
    }

    f.significand = shifted;
    f.exponent = exponent + (int64_t)shift;
    f.flags = BIGFLOAT_NORMAL;
    return f;
}

/* --- Construction from integers --- */

/* Construct BigFloat from a signed 64-bit integer. Normalizes significand to
 * exactly ctx->precision bits. */
static BigFloat bigfloat_from_i64(arena_t* arena, const BigFloatCtx* ctx, int64_t v) {
    if (v == 0) {
        return bigfloat_zero(ctx->precision);
    }

    uint8_t sign = 0;
    BigInt mag;
    if (v < 0) {
        sign = 1;
        mag = bigint_from_i64(v);
        mag = bigint_abs(mag);
    } else {
        mag = bigint_from_u64((uint64_t)v);
    }

    /* exponent = 0: value = significand * 2^0 = significand
     * _bigfloat_normalize will shift to precision bits */
    return _bigfloat_normalize(arena, ctx, mag, 0, sign);
}

/* Construct BigFloat from a BigInt. Rounds to ctx precision using ctx round_mode. */
static BigFloat bigfloat_from_bigint(arena_t* arena, const BigFloatCtx* ctx, const BigInt* v) {
    if (bigint_is_zero(v)) {
        return bigfloat_zero(ctx->precision);
    }

    uint8_t sign = v->sign;
    BigInt mag = bigint_abs(*v);

    return _bigfloat_normalize(arena, ctx, mag, 0, sign);
}

/* --- Predicates --- */

/* Returns 1 if x is zero (positive or negative zero). */
static int bigfloat_is_zero(const BigFloat* x) {
    return x->flags == BIGFLOAT_ZERO;
}

/* Returns 1 if x is +/- infinity. */
static int bigfloat_is_inf(const BigFloat* x) {
    return x->flags == BIGFLOAT_INF;
}

/* Returns 1 if x is NaN. */
static int bigfloat_is_nan(const BigFloat* x) {
    return x->flags == BIGFLOAT_NAN;
}

/* --- Comparison --- */

/* Compare two BigFloats. Returns -1, 0, or +1.
 * Orders: -inf < negative < -0 == +0 < positive < +inf
 * NaN is unordered: callers must check bigfloat_is_nan before comparing. */
static int bigfloat_cmp(const BigFloat* a, const BigFloat* b) {
    /* Both zero: -0 == +0 */
    if (a->flags == BIGFLOAT_ZERO && b->flags == BIGFLOAT_ZERO) return 0;

    /* Both infinity */
    if (a->flags == BIGFLOAT_INF && b->flags == BIGFLOAT_INF) {
        if (a->sign == b->sign) return 0;
        return a->sign ? -1 : 1;
    }

    /* One infinity */
    if (a->flags == BIGFLOAT_INF) return a->sign ? -1 : 1;
    if (b->flags == BIGFLOAT_INF) return b->sign ? 1 : -1;

    /* One or both zero (both-zero already handled above) */
    if (a->flags == BIGFLOAT_ZERO) return b->sign ? 1 : -1;
    if (b->flags == BIGFLOAT_ZERO) return a->sign ? -1 : 1;

    /* Both normal, different signs */
    if (a->sign != b->sign) return a->sign ? -1 : 1;

    /* Both normal, same sign */
    int sign = a->sign ? -1 : 1;

    /* Compare exponents: larger exponent = larger magnitude */
    if (a->exponent > b->exponent) return sign;
    if (a->exponent < b->exponent) return -sign;

    /* Same exponent: compare significands (both unsigned) */
    int cmp = bigint_cmp(&a->significand, &b->significand);
    return cmp * sign;
}

/* --- Sign operations --- */

/* Returns -x. Flips sign; neg of positive zero returns negative zero;
 * neg of NaN returns NaN. Takes by value, no allocation. */
static BigFloat bigfloat_neg(BigFloat x) {
    if (x.flags == BIGFLOAT_NAN) return x;
    x.sign = x.sign ? 0 : 1;
    return x;
}

/* Returns |x|. Clears sign; abs of negative infinity returns positive infinity.
 * Takes by value, no allocation. */
static BigFloat bigfloat_abs(BigFloat x) {
    x.sign = 0;
    return x;
}

/* --- Addition and subtraction --- */

/* Returns x + y rounded to ctx precision. */
static BigFloat bigfloat_add(arena_t* arena, const BigFloatCtx* ctx,
                              const BigFloat* x, const BigFloat* y) {
    /* NaN propagation */
    if (x->flags == BIGFLOAT_NAN || y->flags == BIGFLOAT_NAN)
        return bigfloat_nan();

    /* Infinity cases */
    if (x->flags == BIGFLOAT_INF && y->flags == BIGFLOAT_INF) {
        if (x->sign == y->sign) return bigfloat_inf(x->sign);
        return bigfloat_nan(); /* inf + (-inf) = NaN */
    }
    if (x->flags == BIGFLOAT_INF) return bigfloat_inf(x->sign);
    if (y->flags == BIGFLOAT_INF) return bigfloat_inf(y->sign);

    /* Zero cases */
    if (x->flags == BIGFLOAT_ZERO && y->flags == BIGFLOAT_ZERO) {
        BigFloat z = bigfloat_zero(ctx->precision);
        if (x->sign == y->sign) z.sign = x->sign;
        else z.sign = (ctx->round_mode == BIGFLOAT_ROUND_TOWARD_NEG_INF) ? 1 : 0;
        return z;
    }
    if (x->flags == BIGFLOAT_ZERO)
        return _bigfloat_normalize(arena, ctx, y->significand, y->exponent, y->sign);
    if (y->flags == BIGFLOAT_ZERO)
        return _bigfloat_normalize(arena, ctx, x->significand, x->exponent, x->sign);

    /* Both normal: arrange so a has larger or equal exponent */
    const BigFloat* a = x;
    const BigFloat* b = y;
    if (b->exponent > a->exponent) { a = y; b = x; }

    int64_t d = a->exponent - b->exponent; /* >= 0 */

    /* Threshold for shortcut: when b is negligible compared to a */
    uint32_t pa = bigint_bit_len(&a->significand);
    uint32_t pb = bigint_bit_len(&b->significand);
    uint32_t max_prec = ctx->precision;
    if (pa > max_prec) max_prec = pa;
    if (pb > max_prec) max_prec = pb;

    if (a->sign == b->sign) {
        /* Same sign: add magnitudes */
        if (d > (int64_t)max_prec + 2) {
            /* b is negligible: encode a with sticky bit = 1 */
            uint32_t es = (ctx->precision > pa) ? (ctx->precision - pa + 3) : 3;
            BigInt enc = bigint_shift_left(arena, &a->significand, es);
            BigInt one = bigint_from_u64(1);
            enc = bigint_add(arena, &enc, &one);
            return _bigfloat_normalize(arena, ctx, enc,
                                        a->exponent - (int64_t)es, a->sign);
        }
        /* Exact computation: shift a left by d, add b */
        BigInt a_sh = bigint_shift_left(arena, &a->significand, (uint32_t)d);
        BigInt sum = bigint_add(arena, &a_sh, &b->significand);
        return _bigfloat_normalize(arena, ctx, sum, b->exponent, a->sign);
    } else {
        /* Different signs: subtract magnitudes */
        if (d > (int64_t)max_prec + 2) {
            /* b is negligible: result ≈ a, slightly less in magnitude */
            /* Encode: a_sig << es - 1 gives guard=1, round=1, sticky from bits */
            uint32_t es = (ctx->precision > pa) ? (ctx->precision - pa + 3) : 3;
            BigInt enc = bigint_shift_left(arena, &a->significand, es);
            BigInt one = bigint_from_u64(1);
            enc = bigint_sub(arena, &enc, &one);
            return _bigfloat_normalize(arena, ctx, enc,
                                        a->exponent - (int64_t)es, a->sign);
        }
        /* Exact computation: shift a left by d, subtract b */
        BigInt a_sh = bigint_shift_left(arena, &a->significand, (uint32_t)d);
        BigInt diff = bigint_sub(arena, &a_sh, &b->significand);

        if (bigint_is_zero(&diff)) {
            BigFloat z = bigfloat_zero(ctx->precision);
            z.sign = (ctx->round_mode == BIGFLOAT_ROUND_TOWARD_NEG_INF) ? 1 : 0;
            return z;
        }

        uint8_t rsign;
        if (diff.sign) {
            /* |b| > |a|: only possible when d == 0 */
            diff = bigint_abs(diff);
            rsign = b->sign;
        } else {
            rsign = a->sign;
        }
        return _bigfloat_normalize(arena, ctx, diff, b->exponent, rsign);
    }
}

/* Returns x - y rounded to ctx precision. */
static BigFloat bigfloat_sub(arena_t* arena, const BigFloatCtx* ctx,
                              const BigFloat* x, const BigFloat* y) {
    BigFloat neg_y = bigfloat_neg(*y);
    return bigfloat_add(arena, ctx, x, &neg_y);
}

/* --- Multiplication --- */

/* Returns x * y rounded to ctx precision. */
static BigFloat bigfloat_mul(arena_t* arena, const BigFloatCtx* ctx,
                              const BigFloat* x, const BigFloat* y) {
    /* NaN propagation */
    if (x->flags == BIGFLOAT_NAN || y->flags == BIGFLOAT_NAN)
        return bigfloat_nan();

    /* Sign of result */
    uint8_t rsign = x->sign ^ y->sign;

    /* Infinity cases */
    if (x->flags == BIGFLOAT_INF || y->flags == BIGFLOAT_INF) {
        /* 0 * inf = NaN */
        if (x->flags == BIGFLOAT_ZERO || y->flags == BIGFLOAT_ZERO)
            return bigfloat_nan();
        /* inf * inf = inf, inf * finite = inf */
        return bigfloat_inf(rsign);
    }

    /* Zero cases */
    if (x->flags == BIGFLOAT_ZERO || y->flags == BIGFLOAT_ZERO) {
        BigFloat z = bigfloat_zero(ctx->precision);
        z.sign = rsign;
        return z;
    }

    /* Both normal: multiply significands, add exponents */
    BigInt product = bigint_mul(arena, &x->significand, &y->significand);
    int64_t result_exp = x->exponent + y->exponent;

    return _bigfloat_normalize(arena, ctx, product, result_exp, rsign);
}

/* --- Division --- */

/* Returns x / y rounded to ctx precision. */
static BigFloat bigfloat_div(arena_t* arena, const BigFloatCtx* ctx,
                              const BigFloat* x, const BigFloat* y) {
    /* NaN propagation */
    if (x->flags == BIGFLOAT_NAN || y->flags == BIGFLOAT_NAN)
        return bigfloat_nan();

    /* Sign of result */
    uint8_t rsign = x->sign ^ y->sign;

    /* inf / inf = NaN */
    if (x->flags == BIGFLOAT_INF && y->flags == BIGFLOAT_INF)
        return bigfloat_nan();

    /* inf / finite = inf, inf / 0 = inf */
    if (x->flags == BIGFLOAT_INF)
        return bigfloat_inf(rsign);

    /* finite / inf = 0 */
    if (y->flags == BIGFLOAT_INF) {
        BigFloat z = bigfloat_zero(ctx->precision);
        z.sign = rsign;
        return z;
    }

    /* 0 / 0 = NaN */
    if (x->flags == BIGFLOAT_ZERO && y->flags == BIGFLOAT_ZERO)
        return bigfloat_nan();

    /* x / 0 = inf (correct sign) */
    if (y->flags == BIGFLOAT_ZERO)
        return bigfloat_inf(rsign);

    /* 0 / y = 0 */
    if (x->flags == BIGFLOAT_ZERO) {
        BigFloat z = bigfloat_zero(ctx->precision);
        z.sign = rsign;
        return z;
    }

    /* Both normal: divide significands.
     * Shift x_sig left by (precision + 3) bits so the integer quotient
     * has enough bits for guard/round/sticky rounding. */
    uint32_t extra = ctx->precision + 3;
    BigInt x_shifted = bigint_shift_left(arena, &x->significand, extra);

    BigInt q, rem;
    bigint_divmod(arena, &x_shifted, &y->significand, &q, &rem);

    /* If remainder is nonzero, set LSB of quotient (sticky bit)
     * so _bigfloat_normalize knows the division was inexact */
    if (!bigint_is_zero(&rem)) {
        uint64_t* ql = _bigint_limbs_mut(&q);
        ql[0] |= 1;
    }

    /* result = q * 2^(x_exp - y_exp - extra) */
    int64_t result_exp = x->exponent - y->exponent - (int64_t)extra;

    return _bigfloat_normalize(arena, ctx, q, result_exp, rsign);
}

/* --- Internal: compute 5^n via repeated squaring --- */

static BigInt _bigfloat_pow5(arena_t* arena, uint32_t n) {
    BigInt result = bigint_from_u64(1);
    BigInt base = bigint_from_u64(5);
    uint32_t e = n;
    while (e > 0) {
        if (e & 1) {
            result = bigint_mul(arena, &result, &base);
        }
        e >>= 1;
        if (e > 0) {
            base = bigint_mul(arena, &base, &base);
        }
    }
    return result;
}

/* --- String conversion --- */

#define BIGFLOAT_ERR_INVALID_INPUT 2

/* Convert BigFloat to decimal string in scientific notation: [-]d.ddd...e[+|-]ddd
 * with `digits` significant decimal digits. Returns arena-allocated string.
 * Zero returns "0.0e+0"; infinity returns "inf"/"-inf"; NaN returns "nan". */
static char* bigfloat_to_str(arena_t* arena, const BigFloat* x, uint32_t digits) {
    /* Special values */
    if (x->flags == BIGFLOAT_NAN) {
        char* s = (char*)arena_alloc(arena, 4);
        memcpy(s, "nan", 4);
        return s;
    }
    if (x->flags == BIGFLOAT_INF) {
        if (x->sign) {
            char* s = (char*)arena_alloc(arena, 5);
            memcpy(s, "-inf", 5);
            return s;
        }
        char* s = (char*)arena_alloc(arena, 4);
        memcpy(s, "inf", 4);
        return s;
    }
    if (x->flags == BIGFLOAT_ZERO) {
        uint32_t total = (x->sign ? 1 : 0) + 7;
        char* s = (char*)arena_alloc(arena, total);
        uint32_t pos = 0;
        if (x->sign) s[pos++] = '-';
        memcpy(s + pos, "0.0e+0", 7);
        return s;
    }

    if (digits < 1) digits = 1;

    /* Estimate decimal exponent:
     * log10(value) ≈ (bit_len - 1 + exp) * log10(2) ≈ bit_value * 1233/4096 */
    uint32_t bl = bigint_bit_len(&x->significand);
    int64_t bit_value = (int64_t)bl - 1 + x->exponent;
    int64_t dec_exp;
    if (bit_value >= 0) {
        dec_exp = bit_value * 1233 / 4096;
    } else {
        /* Floor division for negative: (a - b + 1) / b */
        dec_exp = (bit_value * 1233 - 4095) / 4096;
    }

    /* Compute M = round(sig * 2^exp * 10^P) where P = digits-1-dec_exp.
     * Factor: 10^P = 2^P * 5^P, so M = round(sig * 2^(exp+P) * 5^P).
     * Split into numerator and denominator based on signs. */
    char* m_str = NULL;
    for (int attempt = 0; attempt < 3; attempt++) {
        int64_t P = (int64_t)digits - 1 - dec_exp;
        int64_t p2 = x->exponent + P;
        int64_t p5 = P;

        BigInt num = x->significand;
        BigInt den = bigint_from_u64(1);
        int has_den = 0;

        if (p5 > 0) {
            BigInt pow5 = _bigfloat_pow5(arena, (uint32_t)p5);
            num = bigint_mul(arena, &num, &pow5);
        } else if (p5 < 0) {
            den = _bigfloat_pow5(arena, (uint32_t)(-p5));
            has_den = 1;
        }

        if (p2 > 0) {
            num = bigint_shift_left(arena, &num, (uint32_t)p2);
        } else if (p2 < 0) {
            den = bigint_shift_left(arena, &den, (uint32_t)(-p2));
            has_den = 1;
        }

        BigInt q;
        if (!has_den) {
            q = num;
        } else {
            BigInt rem;
            bigint_divmod(arena, &num, &den, &q, &rem);
            /* Round half-to-even */
            BigInt two_rem = bigint_shift_left(arena, &rem, 1);
            int cmp = bigint_cmp(&two_rem, &den);
            if (cmp > 0 || (cmp == 0 && bigint_test_bit(&q, 0))) {
                BigInt one_val = bigint_from_u64(1);
                q = bigint_add(arena, &q, &one_val);
            }
        }

        m_str = bigint_to_str(arena, &q, 10);
        uint32_t m_len = (uint32_t)strlen(m_str);
        if (m_len == digits) break;
        if (m_len > digits)
            dec_exp += (int64_t)(m_len - digits);
        else
            dec_exp -= (int64_t)(digits - m_len);
    }

    /* Format exponent */
    char exp_buf[24];
    int64_t e_abs = dec_exp < 0 ? -dec_exp : dec_exp;
    uint8_t exp_neg = dec_exp < 0 ? 1 : 0;
    int exp_len = 0;
    if (e_abs == 0) {
        exp_buf[0] = '0';
        exp_len = 1;
    } else {
        int64_t tmp = e_abs;
        while (tmp > 0) {
            exp_buf[exp_len++] = '0' + (char)(tmp % 10);
            tmp /= 10;
        }
        for (int i = 0; i < exp_len / 2; i++) {
            char t = exp_buf[i];
            exp_buf[i] = exp_buf[exp_len - 1 - i];
            exp_buf[exp_len - 1 - i] = t;
        }
    }

    /* Build: [-]d[.ddd...]e[+|-]ddd */
    uint32_t has_dot = (digits > 1) ? 1 : 0;
    uint32_t total = (x->sign ? 1 : 0) + digits + has_dot + 1 + 1 + (uint32_t)exp_len + 1;
    char* s = (char*)arena_alloc(arena, total);
    uint32_t pos = 0;
    if (x->sign) s[pos++] = '-';
    s[pos++] = m_str[0];
    if (has_dot) {
        s[pos++] = '.';
        for (uint32_t i = 1; i < digits; i++) s[pos++] = m_str[i];
    }
    s[pos++] = 'e';
    s[pos++] = exp_neg ? '-' : '+';
    {
        int i;
        for (i = 0; i < exp_len; i++) s[pos++] = exp_buf[i];
    }
    s[pos] = '\0';
    return s;
}

/* Parse a decimal string into a BigFloat.
 * Format: [-|+]d[.ddd...][e[+|-]ddd], or "inf", "-inf", "+inf", "nan".
 * Returns 0 on success, BIGFLOAT_ERR_INVALID_INPUT on malformed input. */
static int bigfloat_from_str(arena_t* arena, const BigFloatCtx* ctx,
                              const char* str, BigFloat* out) {
    if (str == NULL || str[0] == '\0') return BIGFLOAT_ERR_INVALID_INPUT;

    uint32_t pos = 0;
    uint8_t sign = 0;
    if (str[0] == '-') { sign = 1; pos = 1; }
    else if (str[0] == '+') { pos = 1; }

    /* Special strings */
    if (strcmp(str + pos, "inf") == 0) { *out = bigfloat_inf(sign); return 0; }
    if (strcmp(str + pos, "nan") == 0) { *out = bigfloat_nan(); return 0; }

    /* Parse mantissa: digits, optional '.', digits */
    uint32_t start = pos;
    uint32_t dot_found = 0;
    uint32_t digit_count = 0;
    uint32_t frac_digits = 0;
    uint32_t i = pos;
    while (str[i] != '\0' && str[i] != 'e' && str[i] != 'E') {
        if (str[i] == '.') {
            if (dot_found) return BIGFLOAT_ERR_INVALID_INPUT;
            dot_found = 1;
        } else if (str[i] >= '0' && str[i] <= '9') {
            digit_count++;
            if (dot_found) frac_digits++;
        } else {
            return BIGFLOAT_ERR_INVALID_INPUT;
        }
        i++;
    }
    if (digit_count == 0) return BIGFLOAT_ERR_INVALID_INPUT;

    /* Parse exponent */
    int64_t exp_val = 0;
    if (str[i] == 'e' || str[i] == 'E') {
        i++;
        if (str[i] == '\0') return BIGFLOAT_ERR_INVALID_INPUT;
        uint8_t esign = 0;
        if (str[i] == '-') { esign = 1; i++; }
        else if (str[i] == '+') { i++; }
        if (str[i] < '0' || str[i] > '9') return BIGFLOAT_ERR_INVALID_INPUT;
        while (str[i] >= '0' && str[i] <= '9') {
            exp_val = exp_val * 10 + (str[i] - '0');
            i++;
        }
        if (esign) exp_val = -exp_val;
    }
    if (str[i] != '\0') return BIGFLOAT_ERR_INVALID_INPUT;

    /* Build digit string (without dot) and parse as BigInt */
    char* dbuf = (char*)arena_alloc(arena, digit_count + 1);
    {
        uint32_t d = 0;
        uint32_t j;
        for (j = start; str[j] != '\0' && str[j] != 'e' && str[j] != 'E'; j++) {
            if (str[j] != '.') dbuf[d++] = str[j];
        }
        dbuf[d] = '\0';
    }

    BigInt sig_int;
    if (bigint_from_str(arena, dbuf, 10, &sig_int)) return BIGFLOAT_ERR_INVALID_INPUT;

    if (bigint_is_zero(&sig_int)) {
        *out = bigfloat_zero(ctx->precision);
        out->sign = sign;
        return 0;
    }

    /* value = sig_int * 10^dec_exp where dec_exp = exp_val - frac_digits
     * Factor: 10^dec_exp = 2^dec_exp * 5^dec_exp */
    int64_t dec_exp = exp_val - (int64_t)frac_digits;

    if (dec_exp >= 0) {
        /* value = sig_int * 5^dec_exp * 2^dec_exp (pure integer) */
        BigInt val = sig_int;
        if (dec_exp > 0) {
            BigInt pow5 = _bigfloat_pow5(arena, (uint32_t)dec_exp);
            val = bigint_mul(arena, &val, &pow5);
        }
        *out = _bigfloat_normalize(arena, ctx, val, dec_exp, sign);
    } else {
        /* value = sig_int * 2^dec_exp / 5^(-dec_exp)
         * Shift sig_int left so quotient has enough bits for rounding */
        uint32_t neg_dec = (uint32_t)(-dec_exp);
        BigInt pow5 = _bigfloat_pow5(arena, neg_dec);
        uint32_t pow5_bits = bigint_bit_len(&pow5);
        uint32_t sig_bits = bigint_bit_len(&sig_int);
        uint32_t need = ctx->precision + 4;
        uint32_t shift = (pow5_bits + need > sig_bits) ?
                          (pow5_bits + need - sig_bits) : 0;

        BigInt shifted = (shift > 0) ?
            bigint_shift_left(arena, &sig_int, shift) : sig_int;
        BigInt q, rem;
        bigint_divmod(arena, &shifted, &pow5, &q, &rem);

        /* Set sticky bit if remainder nonzero */
        if (!bigint_is_zero(&rem)) {
            uint64_t* ql = _bigint_limbs_mut(&q);
            ql[0] |= 1;
        }

        *out = _bigfloat_normalize(arena, ctx, q, dec_exp - (int64_t)shift, sign);
    }

    return 0;
}

#endif /* BIGFLOAT_H */
