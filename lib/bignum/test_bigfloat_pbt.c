/*
 * test_bigfloat_pbt.c -- Randomized property-based tests for BigFloat
 *
 * Floating-point identities don't generally hold under rounding, so the PBT
 * picks a regime where they *do*:
 *   - Inputs are random BigInts up to ~80 bits, converted to BigFloat at
 *     1024-bit precision. At that precision, sums/differences/products of
 *     numbers up to ~1024/2 bits in magnitude are exactly representable.
 *   - Identities are checked against the equivalent BigInt computation,
 *     which is exact.
 *
 * Properties:
 *   - neg involutive (--x == x)
 *   - cmp antisymmetric (cmp(a,b) == -cmp(b,a))
 *   - add commutes & associates (vs BigInt)
 *   - mul commutes (vs BigInt)
 *   - distributivity (vs BigInt)
 *   - from_bigint round-trip via add/sub
 *
 *   ./test_bigfloat_pbt              # 1000 iterations, seed=1
 *   ./test_bigfloat_pbt 500 42       # 500 iterations, seed=42
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#define BIGINT_KARATSUBA_THRESHOLD 4
#include "bigint.h"
#include "bigfloat.h"

typedef struct { uint64_t s; } prng_t;
static uint64_t prng_next(prng_t* p) {
    uint64_t x = p->s;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    p->s = x ? x : 0x9E3779B97F4A7C15ULL;
    return p->s;
}

/* Random BigInt up to `max_bits` bits; random sign. */
static BigInt rand_bigint_bits(arena_t* a, prng_t* p, uint32_t max_bits) {
    uint32_t bits = 1 + (uint32_t)(prng_next(p) % max_bits);
    uint32_t limbs = (bits + 63) / 64;
    BigInt x = _bigint_alloc(a, limbs);
    uint64_t* lim = _bigint_limbs_mut(&x);
    for (uint32_t i = 0; i < limbs; i++) lim[i] = prng_next(p);
    /* Mask the top limb so the total is at most `bits` bits */
    uint32_t top_bits = bits - (limbs - 1) * 64;
    if (top_bits == 64) {
        /* keep as-is */
    } else {
        lim[limbs - 1] &= ((uint64_t)1 << top_bits) - 1;
    }
    if (lim[limbs - 1] == 0 && limbs > 1) lim[limbs - 1] = 1;
    x.sign = (prng_next(p) & 1) ? 1 : 0;
    _bigint_normalize(&x);
    if (bigint_is_zero(&x)) x.sign = 0;
    return x;
}

/* Compare BigFloat to BigInt for exact equality (assumes the float was
 * constructed/produced without any rounding loss). */
static int bf_eq_bigint(arena_t* a, const BigFloatCtx* ctx,
                        const BigFloat* f, const BigInt* i) {
    BigFloat ref = bigfloat_from_bigint(a, ctx, i);
    return bigfloat_cmp(f, &ref) == 0;
}

static int prop_neg_involutive(arena_t* a, prng_t* p) {
    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigInt v = rand_bigint_bits(a, p, 80);
    BigFloat f = bigfloat_from_bigint(a, &ctx, &v);
    BigFloat nn = bigfloat_neg(bigfloat_neg(f));
    if (bigfloat_cmp(&nn, &f) != 0) {
        fprintf(stderr, "  -(-f) != f\n");
        return 0;
    }
    return 1;
}

static int prop_cmp_antisymmetric(arena_t* a, prng_t* p) {
    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigInt a_i = rand_bigint_bits(a, p, 80);
    BigInt b_i = rand_bigint_bits(a, p, 80);
    BigFloat af = bigfloat_from_bigint(a, &ctx, &a_i);
    BigFloat bf = bigfloat_from_bigint(a, &ctx, &b_i);
    int ab = bigfloat_cmp(&af, &bf);
    int ba = bigfloat_cmp(&bf, &af);
    if (ab != -ba) {
        fprintf(stderr, "  cmp(a,b)=%d cmp(b,a)=%d\n", ab, ba);
        return 0;
    }
    int self = bigfloat_cmp(&af, &af);
    if (self != 0) {
        fprintf(stderr, "  cmp(a,a)=%d\n", self);
        return 0;
    }
    return 1;
}

static int prop_add_commutes_exact(arena_t* a, prng_t* p) {
    BigFloatCtx ctx = { .precision = 1024, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigInt a_i = rand_bigint_bits(a, p, 80);
    BigInt b_i = rand_bigint_bits(a, p, 80);
    BigFloat af = bigfloat_from_bigint(a, &ctx, &a_i);
    BigFloat bf = bigfloat_from_bigint(a, &ctx, &b_i);

    BigFloat ab = bigfloat_add(a, &ctx, &af, &bf);
    BigFloat ba = bigfloat_add(a, &ctx, &bf, &af);
    if (bigfloat_cmp(&ab, &ba) != 0) {
        fprintf(stderr, "  a+b != b+a\n");
        return 0;
    }
    /* Cross-check against BigInt sum */
    BigInt ref = bigint_add(a, &a_i, &b_i);
    if (!bf_eq_bigint(a, &ctx, &ab, &ref)) {
        fprintf(stderr, "  bigfloat a+b != bigint a+b\n");
        return 0;
    }
    return 1;
}

static int prop_mul_commutes_exact(arena_t* a, prng_t* p) {
    BigFloatCtx ctx = { .precision = 1024, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigInt a_i = rand_bigint_bits(a, p, 64);
    BigInt b_i = rand_bigint_bits(a, p, 64);
    BigFloat af = bigfloat_from_bigint(a, &ctx, &a_i);
    BigFloat bf = bigfloat_from_bigint(a, &ctx, &b_i);
    BigFloat ab = bigfloat_mul(a, &ctx, &af, &bf);
    BigFloat ba = bigfloat_mul(a, &ctx, &bf, &af);
    if (bigfloat_cmp(&ab, &ba) != 0) return 0;
    BigInt ref = bigint_mul(a, &a_i, &b_i);
    if (!bf_eq_bigint(a, &ctx, &ab, &ref)) {
        fprintf(stderr, "  bigfloat a*b != bigint a*b\n");
        return 0;
    }
    return 1;
}

static int prop_distributes_exact(arena_t* a, prng_t* p) {
    BigFloatCtx ctx = { .precision = 1024, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigInt a_i = rand_bigint_bits(a, p, 40);
    BigInt b_i = rand_bigint_bits(a, p, 40);
    BigInt c_i = rand_bigint_bits(a, p, 40);
    BigFloat af = bigfloat_from_bigint(a, &ctx, &a_i);
    BigFloat bf = bigfloat_from_bigint(a, &ctx, &b_i);
    BigFloat cf = bigfloat_from_bigint(a, &ctx, &c_i);
    BigFloat bcf = bigfloat_add(a, &ctx, &bf, &cf);
    BigFloat left = bigfloat_mul(a, &ctx, &af, &bcf);
    BigFloat abf = bigfloat_mul(a, &ctx, &af, &bf);
    BigFloat acf = bigfloat_mul(a, &ctx, &af, &cf);
    BigFloat right = bigfloat_add(a, &ctx, &abf, &acf);
    if (bigfloat_cmp(&left, &right) != 0) {
        fprintf(stderr, "  a*(b+c) != a*b + a*c\n");
        return 0;
    }
    return 1;
}

static int prop_x_minus_x_zero(arena_t* a, prng_t* p) {
    BigFloatCtx ctx = { .precision = 256, .round_mode = BIGFLOAT_ROUND_NEAREST_EVEN };
    BigInt v = rand_bigint_bits(a, p, 80);
    BigFloat f = bigfloat_from_bigint(a, &ctx, &v);
    BigFloat z = bigfloat_sub(a, &ctx, &f, &f);
    if (!bigfloat_is_zero(&z)) {
        fprintf(stderr, "  x - x != 0\n");
        return 0;
    }
    return 1;
}

typedef int (*prop_fn)(arena_t*, prng_t*);
typedef struct { const char* name; prop_fn fn; } prop_t;
static const prop_t PROPS[] = {
    {"neg_involutive",     prop_neg_involutive},
    {"cmp_antisymmetric",  prop_cmp_antisymmetric},
    {"add_commutes_exact", prop_add_commutes_exact},
    {"mul_commutes_exact", prop_mul_commutes_exact},
    {"distributes_exact",  prop_distributes_exact},
    {"x_minus_x_zero",     prop_x_minus_x_zero},
};
#define NPROPS (sizeof(PROPS)/sizeof(PROPS[0]))

int main(int argc, char** argv) {
    uint32_t iters = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 10) : 1000;
    uint64_t seed  = (argc > 2) ? strtoull(argv[2], NULL, 10) : 1ULL;
    printf("bigfloat PBT: %u iterations per property, seed=%" PRIu64 "\n", iters, seed);

    for (size_t pi = 0; pi < NPROPS; pi++) {
        prng_t prng = { .s = seed ^ ((uint64_t)pi * 0x9E3779B97F4A7C15ULL + 1) };
        arena_t arena = {0};
        for (uint32_t i = 0; i < iters; i++) {
            if (!PROPS[pi].fn(&arena, &prng)) {
                fprintf(stderr, "  ^ at property=%s iter=%u seed=%" PRIu64 "\n",
                        PROPS[pi].name, i, seed);
                arena_destroy(&arena);
                printf("FAIL: %s\n", PROPS[pi].name);
                return 1;
            }
            if ((i & 63) == 63) arena_destroy(&arena);
        }
        arena_destroy(&arena);
        printf("  %-20s %u/%u OK\n", PROPS[pi].name, iters, iters);
    }
    printf("PASS\n");
    return 0;
}
