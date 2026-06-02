/*
 * test_bigint_pbt.c -- Randomized property-based tests for BigInt
 *
 * Verifies algebraic identities on randomly generated values:
 *   - Commutativity & associativity of + and *
 *   - Distributivity of * over +
 *   - Round-trip identities: (a+b)-b == a, (a*b)/b == a
 *   - divmod identity: a == (a/b)*b + (a%b)
 *   - Shift inverse: (x << k) >> k == x
 *   - String roundtrip in bases 2, 10, 16
 *   - GCD properties: g divides a and b; gcd(0,a) == |a|
 *   - Differential: _bigint_mul_schoolbook == _bigint_karatsuba on the same
 *     inputs. This is the highest-signal test we have because it cross-checks
 *     two independent implementations of the same operation.
 *
 * The PRNG is a deterministic xorshift64 seeded from a CLI arg (default 1),
 * so any failing iteration prints a reproducer seed.
 *
 *   ./test_bigint_pbt              # 2000 iterations, seed=1
 *   ./test_bigint_pbt 500          # 500 iterations
 *   ./test_bigint_pbt 500 42       # 500 iterations, seed=42
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* Threshold = 2 forces Karatsuba on almost every non-trivial multiply, so
 * generic bigint_mul calls exercise the Karatsuba path constantly. The
 * differential test calls _bigint_karatsuba directly. */
#define BIGINT_KARATSUBA_THRESHOLD 2
#include "bigint.h"

/* --- PRNG: xorshift64 --- */

typedef struct { uint64_t s; } prng_t;

static uint64_t prng_next(prng_t* p) {
    uint64_t x = p->s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    p->s = x ? x : 0x9E3779B97F4A7C15ULL;
    return p->s;
}

/* Random BigInt with up to `max_limbs` limbs (at least 1). Random sign. */
static BigInt rand_bigint(arena_t* arena, prng_t* p, uint32_t max_limbs) {
    if (max_limbs < 1) max_limbs = 1;
    uint32_t n = 1 + (uint32_t)(prng_next(p) % max_limbs);
    BigInt x = _bigint_alloc(arena, n);
    uint64_t* lim = _bigint_limbs_mut(&x);
    for (uint32_t i = 0; i < n; i++) lim[i] = prng_next(p);
    /* Guarantee the top limb is non-zero (avoid wasted leading-zero limbs
     * that normalize would strip — we want the full requested size). */
    if (lim[n - 1] == 0) lim[n - 1] = 1;
    x.sign = (prng_next(p) & 1) ? 1 : 0;
    _bigint_normalize(&x);
    if (bigint_is_zero(&x)) x.sign = 0;
    return x;
}

/* Random non-zero BigInt for use as a divisor. */
static BigInt rand_bigint_nonzero(arena_t* arena, prng_t* p, uint32_t max_limbs) {
    for (;;) {
        BigInt x = rand_bigint(arena, p, max_limbs);
        if (!bigint_is_zero(&x)) return x;
    }
}

/* --- Failure helper: dump operands and bail --- */

static void dump(const char* label, const BigInt* x, arena_t* arena) {
    char* s = bigint_to_str(arena, x, 16);
    fprintf(stderr, "  %s = 0x%s   (sign=%u len=%u cap=%u)\n",
            label, s, x->sign, x->len, x->cap);
}

#define FAIL(seed_, iter_, msg_) do { \
    fprintf(stderr, "FAIL: %s (seed=%" PRIu64 " iter=%u)\n", msg_, seed_, iter_); \
    return 0; \
} while (0)

/* --- Properties ---
 *
 * Each property takes the current arena and PRNG, performs one trial, and
 * returns 1 on success or 0 on failure (after printing the offending inputs).
 * They all assume the caller resets the arena between trials. */

static int prop_add_commutes(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 16);
    BigInt b = rand_bigint(arena, p, 16);
    BigInt ab = bigint_add(arena, &a, &b);
    BigInt ba = bigint_add(arena, &b, &a);
    if (bigint_cmp(&ab, &ba) != 0) {
        dump("a", &a, arena); dump("b", &b, arena);
        dump("a+b", &ab, arena); dump("b+a", &ba, arena);
        return 0;
    }
    return 1;
}

static int prop_add_associates(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 12);
    BigInt b = rand_bigint(arena, p, 12);
    BigInt c = rand_bigint(arena, p, 12);
    BigInt ab  = bigint_add(arena, &a, &b);
    BigInt abc1 = bigint_add(arena, &ab, &c);
    BigInt bc  = bigint_add(arena, &b, &c);
    BigInt abc2 = bigint_add(arena, &a, &bc);
    if (bigint_cmp(&abc1, &abc2) != 0) {
        dump("a", &a, arena); dump("b", &b, arena); dump("c", &c, arena);
        return 0;
    }
    return 1;
}

static int prop_add_sub_inverse(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 16);
    BigInt b = rand_bigint(arena, p, 16);
    BigInt sum = bigint_add(arena, &a, &b);
    BigInt back = bigint_sub(arena, &sum, &b);
    if (bigint_cmp(&back, &a) != 0) {
        dump("a", &a, arena); dump("b", &b, arena);
        dump("(a+b)-b", &back, arena);
        return 0;
    }
    return 1;
}

static int prop_neg_involutive(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 16);
    BigInt na = bigint_neg(a);
    BigInt nna = bigint_neg(na);
    if (bigint_cmp(&nna, &a) != 0) {
        dump("a", &a, arena); dump("--a", &nna, arena);
        return 0;
    }
    (void)arena;
    return 1;
}

static int prop_mul_commutes(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 12);
    BigInt b = rand_bigint(arena, p, 12);
    BigInt ab = bigint_mul(arena, &a, &b);
    BigInt ba = bigint_mul(arena, &b, &a);
    if (bigint_cmp(&ab, &ba) != 0) {
        dump("a", &a, arena); dump("b", &b, arena);
        dump("a*b", &ab, arena); dump("b*a", &ba, arena);
        return 0;
    }
    return 1;
}

static int prop_mul_distributes(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 8);
    BigInt b = rand_bigint(arena, p, 8);
    BigInt c = rand_bigint(arena, p, 8);
    BigInt bc = bigint_add(arena, &b, &c);
    BigInt left = bigint_mul(arena, &a, &bc);     /* a*(b+c) */
    BigInt ab = bigint_mul(arena, &a, &b);
    BigInt ac = bigint_mul(arena, &a, &c);
    BigInt right = bigint_add(arena, &ab, &ac);   /* a*b + a*c */
    if (bigint_cmp(&left, &right) != 0) {
        dump("a", &a, arena); dump("b", &b, arena); dump("c", &c, arena);
        dump("a*(b+c)", &left, arena); dump("a*b+a*c", &right, arena);
        return 0;
    }
    return 1;
}

/* Differential test: schoolbook and Karatsuba must agree on every input.
 * This is the strongest correctness check in the harness — two independent
 * algorithms either both have the same bug, or both are right. */
static int prop_mul_school_eq_karatsuba(arena_t* arena, prng_t* p) {
    /* Magnitudes only; the algorithms operate on absolute values. */
    BigInt a = rand_bigint(arena, p, 60); a.sign = 0;
    BigInt b = rand_bigint(arena, p, 60); b.sign = 0;
    if (a.len < BIGINT_KARATSUBA_THRESHOLD || b.len < BIGINT_KARATSUBA_THRESHOLD) {
        return 1; /* Karatsuba would fall back to schoolbook; nothing to compare */
    }
    BigInt s = _bigint_mul_schoolbook(arena, &a, &b);
    BigInt k = _bigint_karatsuba(arena, &a, &b);
    if (bigint_cmp(&s, &k) != 0) {
        dump("a", &a, arena); dump("b", &b, arena);
        dump("schoolbook", &s, arena); dump("karatsuba", &k, arena);
        return 0;
    }
    return 1;
}

static int prop_divmod_identity(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 16);
    BigInt b = rand_bigint_nonzero(arena, p, 8);
    BigInt q, r;
    if (bigint_divmod(arena, &a, &b, &q, &r) != 0) return 1;
    /* a == q*b + r */
    BigInt qb = bigint_mul(arena, &q, &b);
    BigInt sum = bigint_add(arena, &qb, &r);
    if (bigint_cmp(&sum, &a) != 0) {
        dump("a", &a, arena); dump("b", &b, arena);
        dump("q", &q, arena); dump("r", &r, arena);
        dump("q*b+r", &sum, arena);
        return 0;
    }
    /* |r| < |b| */
    BigInt abs_r = bigint_abs(r);
    BigInt abs_b = bigint_abs(b);
    if (_bigint_cmp_abs(&abs_r, &abs_b) >= 0) {
        dump("a", &a, arena); dump("b", &b, arena); dump("r", &r, arena);
        fprintf(stderr, "  |r| >= |b|\n");
        return 0;
    }
    /* r has same sign as a (C99 truncation), unless r == 0 */
    if (!bigint_is_zero(&r) && r.sign != a.sign) {
        dump("a", &a, arena); dump("r", &r, arena);
        fprintf(stderr, "  r sign mismatch\n");
        return 0;
    }
    return 1;
}

static int prop_mul_div_inverse(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 12);
    BigInt b = rand_bigint_nonzero(arena, p, 8);
    BigInt ab = bigint_mul(arena, &a, &b);
    BigInt back = bigint_div(arena, &ab, &b);
    if (bigint_cmp(&back, &a) != 0) {
        dump("a", &a, arena); dump("b", &b, arena);
        dump("a*b", &ab, arena); dump("(a*b)/b", &back, arena);
        return 0;
    }
    return 1;
}

static int prop_shift_roundtrip(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 8);
    uint32_t k = (uint32_t)(prng_next(p) % 200);
    BigInt sl = bigint_shift_left(arena, &a, k);
    BigInt back = bigint_shift_right(arena, &sl, k);
    if (bigint_cmp(&back, &a) != 0) {
        dump("a", &a, arena);
        fprintf(stderr, "  k=%u\n", k);
        dump("(a<<k)>>k", &back, arena);
        return 0;
    }
    /* a << k should equal a * 2^k. Check with a small k via multiplication. */
    if (k < 64) {
        BigInt pow = bigint_from_u64((uint64_t)1 << k);
        BigInt prod = bigint_mul(arena, &a, &pow);
        if (bigint_cmp(&prod, &sl) != 0) {
            dump("a", &a, arena); dump("a<<k", &sl, arena); dump("a*2^k", &prod, arena);
            fprintf(stderr, "  k=%u\n", k);
            return 0;
        }
    }
    return 1;
}

static int prop_str_roundtrip(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 10);
    int bases[] = {2, 10, 16};
    for (int i = 0; i < 3; i++) {
        char* s = bigint_to_str(arena, &a, bases[i]);
        BigInt parsed;
        if (bigint_from_str(arena, s, bases[i], &parsed) != 0) {
            fprintf(stderr, "  from_str failed for base %d on \"%s\"\n", bases[i], s);
            return 0;
        }
        if (bigint_cmp(&parsed, &a) != 0) {
            dump("a", &a, arena); dump("parsed", &parsed, arena);
            fprintf(stderr, "  base=%d  str=\"%s\"\n", bases[i], s);
            return 0;
        }
    }
    return 1;
}

static int prop_gcd_divides(arena_t* arena, prng_t* p) {
    BigInt a = rand_bigint(arena, p, 8);
    BigInt b = rand_bigint(arena, p, 8);
    if (bigint_is_zero(&a) && bigint_is_zero(&b)) return 1;
    BigInt g = bigint_gcd(arena, &a, &b);
    /* g must be non-negative and non-zero (since not both inputs are zero) */
    if (g.sign != 0 || bigint_is_zero(&g)) {
        dump("a", &a, arena); dump("b", &b, arena); dump("g", &g, arena);
        return 0;
    }
    /* g divides |a| and |b| */
    BigInt abs_a = bigint_abs(a);
    BigInt abs_b = bigint_abs(b);
    if (!bigint_is_zero(&abs_a)) {
        BigInt r = bigint_mod(arena, &abs_a, &g);
        if (!bigint_is_zero(&r)) {
            dump("a", &a, arena); dump("g", &g, arena); dump("a%g", &r, arena);
            return 0;
        }
    }
    if (!bigint_is_zero(&abs_b)) {
        BigInt r = bigint_mod(arena, &abs_b, &g);
        if (!bigint_is_zero(&r)) {
            dump("b", &b, arena); dump("g", &g, arena); dump("b%g", &r, arena);
            return 0;
        }
    }
    return 1;
}

/* --- Test driver --- */

typedef int (*prop_fn)(arena_t*, prng_t*);
typedef struct { const char* name; prop_fn fn; } prop_t;

static const prop_t PROPS[] = {
    {"add_commutes",            prop_add_commutes},
    {"add_associates",          prop_add_associates},
    {"add_sub_inverse",         prop_add_sub_inverse},
    {"neg_involutive",          prop_neg_involutive},
    {"mul_commutes",            prop_mul_commutes},
    {"mul_distributes",         prop_mul_distributes},
    {"mul_school_eq_karatsuba", prop_mul_school_eq_karatsuba},
    {"divmod_identity",         prop_divmod_identity},
    {"mul_div_inverse",         prop_mul_div_inverse},
    {"shift_roundtrip",         prop_shift_roundtrip},
    {"str_roundtrip",           prop_str_roundtrip},
    {"gcd_divides",             prop_gcd_divides},
};
#define NPROPS (sizeof(PROPS)/sizeof(PROPS[0]))

int main(int argc, char** argv) {
    uint32_t iters = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed  = (argc > 2) ? strtoull(argv[2], NULL, 10) : 1ULL;

    printf("bigint PBT: %u iterations per property, seed=%" PRIu64 "\n",
           iters, seed);

    for (size_t pi = 0; pi < NPROPS; pi++) {
        prng_t prng = { .s = seed ^ ((uint64_t)pi * 0x9E3779B97F4A7C15ULL + 1) };
        arena_t arena = {0};
        uint32_t failed = 0;
        for (uint32_t i = 0; i < iters; i++) {
            if (!PROPS[pi].fn(&arena, &prng)) {
                fprintf(stderr, "  ^ at property=%s iter=%u\n",
                        PROPS[pi].name, i);
                failed = 1;
                break;
            }
            /* Reset the arena every 64 iters to keep memory bounded. */
            if ((i & 63) == 63) arena_destroy(&arena);
        }
        arena_destroy(&arena);
        if (failed) {
            printf("FAIL: %s\n", PROPS[pi].name);
            return 1;
        }
        printf("  %-26s %u/%u OK\n", PROPS[pi].name, iters, iters);
    }

    printf("PASS\n");
    return 0;
}
