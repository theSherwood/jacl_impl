/*
 * ---------------------------------------------------------------------------
 * JACL Value Representation
 * ---------------------------------------------------------------------------
 * 64-bit tagged value system with inline types, pointer payloads,
 * inline strings, flag propagation, arithmetic, and comparisons.
 *
 * Layout of a JaclVal (uint64_t):
 *   Bits 63-56: Tag byte
 *     Bit 7 (63): Tainted flag
 *     Bit 6 (62): Secret flag
 *     Bit 5 (61): Error flag
 *     Bits 4-0 (60-56): Type tag (0-31)
 *   Bits 55-0: Payload (56 bits)
 *
 * Single-header convention:
 *   #ifndef VALUE_H / #define VALUE_H for declarations
 *   #ifdef VALUE_IMPLEMENTATION for implementation
 */

#ifndef VALUE_H
#define VALUE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* --- Core type --- */

typedef uint64_t JaclVal;

/* --- Tag byte layout --- */

#define JACL_TAG_SHIFT      56
#define JACL_TAG_MASK       ((uint64_t)0xFF << JACL_TAG_SHIFT)
#define JACL_PAYLOAD_MASK   ((UINT64_C(1) << JACL_TAG_SHIFT) - 1)

/* --- Flag bits (within the tag byte) --- */

#define JACL_FLAG_TAINTED   ((uint64_t)1 << 63)  /* Bit 7 of tag byte */
#define JACL_FLAG_SECRET    ((uint64_t)1 << 62)   /* Bit 6 of tag byte */
#define JACL_FLAG_ERROR     ((uint64_t)1 << 61)   /* Bit 5 of tag byte */
#define JACL_FLAGS_MASK     (JACL_FLAG_TAINTED | JACL_FLAG_SECRET | JACL_FLAG_ERROR)

/* --- Type tag mask (low 5 bits of the tag byte) --- */

#define JACL_TYPE_MASK      ((uint64_t)0x1F << JACL_TAG_SHIFT)

/* --- Type tag constants (5-bit values, pre-shifted to tag position) --- */

#define JACL_TAG_NIL            ((uint64_t)0x00 << JACL_TAG_SHIFT)
#define JACL_TAG_BOOL           ((uint64_t)0x01 << JACL_TAG_SHIFT)
#define JACL_TAG_I32            ((uint64_t)0x02 << JACL_TAG_SHIFT)
#define JACL_TAG_F32            ((uint64_t)0x03 << JACL_TAG_SHIFT)
#define JACL_TAG_INLINE_STRING  ((uint64_t)0x04 << JACL_TAG_SHIFT)
#define JACL_TAG_STRING         ((uint64_t)0x05 << JACL_TAG_SHIFT)
#define JACL_TAG_VECTOR         ((uint64_t)0x06 << JACL_TAG_SHIFT)
#define JACL_TAG_MAP            ((uint64_t)0x07 << JACL_TAG_SHIFT)
#define JACL_TAG_CLOSURE        ((uint64_t)0x08 << JACL_TAG_SHIFT)
#define JACL_TAG_BIGNUM         ((uint64_t)0x09 << JACL_TAG_SHIFT)

/* --- Nil and boolean constants --- */

#define JACL_NIL    ((JaclVal)JACL_TAG_NIL)
#define JACL_TRUE   ((JaclVal)(JACL_TAG_BOOL | UINT64_C(1)))
#define JACL_FALSE  ((JaclVal)(JACL_TAG_BOOL))

/* --- Constructors --- */

static inline JaclVal jacl_bool(bool b) {
    return JACL_TAG_BOOL | (b ? UINT64_C(1) : UINT64_C(0));
}

static inline JaclVal jacl_i32(int32_t n) {
    return JACL_TAG_I32 | ((uint64_t)(uint32_t)n & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_f32(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return JACL_TAG_F32 | ((uint64_t)bits & JACL_PAYLOAD_MASK);
}

/* --- Type tag extractor --- */

static inline uint64_t jacl_type_tag(JaclVal v) {
    return v & JACL_TYPE_MASK;
}

/* --- Predicates --- */

static inline bool jacl_is_nil(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_NIL;
}

static inline bool jacl_is_bool(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_BOOL;
}

static inline bool jacl_is_i32(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_I32;
}

static inline bool jacl_is_f32(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_F32;
}

/* --- Compile-time pointer size check --- */

/* User-space addresses on x86-64 use 48 bits, ARM64 uses up to 52 bits.
 * Both fit comfortably in our 56-bit payload. This assert catches any
 * platform with pointers wider than 64 bits. */
typedef char jacl_assert_ptr_fits_payload_[(sizeof(void *) <= sizeof(uint64_t)) ? 1 : -1];

/* --- Pointer-payload constructors --- */

static inline JaclVal jacl_string_ptr(void *p) {
    return JACL_TAG_STRING | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_vector_ptr(void *p) {
    return JACL_TAG_VECTOR | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_map_ptr(void *p) {
    return JACL_TAG_MAP | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_closure_ptr(void *p) {
    return JACL_TAG_CLOSURE | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_bignum_ptr(void *p) {
    return JACL_TAG_BIGNUM | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

/* --- Pointer extractor --- */

static inline void *jacl_as_ptr(JaclVal v) {
    return (void *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* --- Pointer type predicates --- */

static inline bool jacl_is_string(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_STRING;
}

static inline bool jacl_is_vector(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_VECTOR;
}

static inline bool jacl_is_map(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_MAP;
}

static inline bool jacl_is_closure(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_CLOSURE;
}

static inline bool jacl_is_bignum(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_BIGNUM;
}

/* --- Inline string constructor --- */

static inline JaclVal jacl_inline_string(const char *s, size_t len) {
    uint64_t payload = 0;
    if (len > 7) len = 7;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\0') break;
        payload |= ((uint64_t)(unsigned char)s[i]) << (i * 8);
    }
    return JACL_TAG_INLINE_STRING | payload;
}

/* --- Inline string predicate --- */

static inline bool jacl_is_inline_string(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_INLINE_STRING;
}

/* --- Inline string extractors --- */

static inline size_t jacl_inline_string_len(JaclVal v) {
    uint64_t payload = v & JACL_PAYLOAD_MASK;
    size_t len = 0;
    for (size_t i = 0; i < 7; i++) {
        if (((payload >> (i * 8)) & 0xFF) == 0) break;
        len++;
    }
    return len;
}

static inline void jacl_inline_string_get(JaclVal v, char *buf, size_t buflen) {
    uint64_t payload = v & JACL_PAYLOAD_MASK;
    size_t i;
    for (i = 0; i < 7 && i + 1 < buflen; i++) {
        unsigned char c = (unsigned char)((payload >> (i * 8)) & 0xFF);
        if (c == '\0') break;
        buf[i] = (char)c;
    }
    if (buflen > 0) buf[i] = '\0';
}

/* --- Flag manipulation --- */

static inline bool jacl_is_tainted(JaclVal v) {
    return (v & JACL_FLAG_TAINTED) != 0;
}

static inline JaclVal jacl_set_tainted(JaclVal v) {
    return v | JACL_FLAG_TAINTED;
}

static inline JaclVal jacl_clear_tainted(JaclVal v) {
    return v & ~JACL_FLAG_TAINTED;
}

static inline bool jacl_is_secret(JaclVal v) {
    return (v & JACL_FLAG_SECRET) != 0;
}

static inline JaclVal jacl_set_secret(JaclVal v) {
    return v | JACL_FLAG_SECRET;
}

static inline JaclVal jacl_clear_secret(JaclVal v) {
    return v & ~JACL_FLAG_SECRET;
}

static inline bool jacl_is_error(JaclVal v) {
    return (v & JACL_FLAG_ERROR) != 0;
}

static inline JaclVal jacl_set_error(JaclVal v) {
    return v | JACL_FLAG_ERROR;
}

static inline JaclVal jacl_clear_error(JaclVal v) {
    return v & ~JACL_FLAG_ERROR;
}

/* --- Flag propagation --- */

static inline uint64_t jacl_propagate_flags(JaclVal a, JaclVal b) {
    return (a | b) & JACL_FLAGS_MASK;
}

static inline JaclVal jacl_apply_flags(JaclVal result, uint64_t flags) {
    return result | (flags & JACL_FLAGS_MASK);
}

/* --- Extractors --- */

static inline bool jacl_as_bool(JaclVal v) {
    return (v & JACL_PAYLOAD_MASK) != 0;
}

static inline int32_t jacl_as_i32(JaclVal v) {
    return (int32_t)(uint32_t)(v & JACL_PAYLOAD_MASK);
}

static inline float jacl_as_f32(JaclVal v) {
    uint32_t bits = (uint32_t)(v & JACL_PAYLOAD_MASK);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* --- i32 arithmetic --- */

static inline JaclVal jacl_add_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i32(jacl_as_i32(a) + jacl_as_i32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_sub_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i32(jacl_as_i32(a) - jacl_as_i32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_mul_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i32(jacl_as_i32(a) * jacl_as_i32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_div_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    int32_t dividend = jacl_as_i32(a);
    int32_t divisor = jacl_as_i32(b);
    if (divisor == 0) {
        return jacl_apply_flags(jacl_set_error(jacl_i32(0)), flags);
    }
    /* Avoid UB: INT32_MIN / -1 overflows; wrap via unsigned cast */
    int32_t quot = (dividend == INT32_MIN && divisor == -1)
        ? INT32_MIN : (dividend / divisor);
    return jacl_apply_flags(jacl_i32(quot), flags);
}

static inline JaclVal jacl_mod_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    int32_t dividend = jacl_as_i32(a);
    int32_t divisor = jacl_as_i32(b);
    if (divisor == 0) {
        return jacl_apply_flags(jacl_set_error(jacl_i32(0)), flags);
    }
    /* Avoid UB: INT32_MIN % -1 is 0 (mathematically exact) */
    int32_t rem = (dividend == INT32_MIN && divisor == -1)
        ? 0 : (dividend % divisor);
    return jacl_apply_flags(jacl_i32(rem), flags);
}

static inline JaclVal jacl_neg_i32(JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    JaclVal result = jacl_i32(-jacl_as_i32(a));
    return jacl_apply_flags(result, flags);
}

/* --- f32 arithmetic --- */

static inline JaclVal jacl_add_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f32(jacl_as_f32(a) + jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_sub_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f32(jacl_as_f32(a) - jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_mul_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f32(jacl_as_f32(a) * jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_div_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f32(jacl_as_f32(a) / jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_neg_f32(JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    JaclVal result = jacl_f32(-jacl_as_f32(a));
    return jacl_apply_flags(result, flags);
}

/* --- Comparison operations --- */

static inline JaclVal jacl_eq(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    /* Strip flags: compare type tag + payload only */
    uint64_t mask = JACL_TYPE_MASK | JACL_PAYLOAD_MASK;
    JaclVal result = jacl_bool((a & mask) == (b & mask));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_lt_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_i32(a) < jacl_as_i32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_gt_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_i32(a) > jacl_as_i32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_le_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_i32(a) <= jacl_as_i32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_ge_i32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_i32(a) >= jacl_as_i32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_lt_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_f32(a) < jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_gt_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_f32(a) > jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_le_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_f32(a) <= jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_ge_f32(JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_bool(jacl_as_f32(a) >= jacl_as_f32(b));
    return jacl_apply_flags(result, flags);
}

#endif /* VALUE_H */

/* =========================================================================
 * Implementation Section
 * Define VALUE_IMPLEMENTATION before including to generate function bodies.
 * ========================================================================= */

#ifdef VALUE_IMPLEMENTATION
#ifndef VALUE_IMPL_GUARD_
#define VALUE_IMPL_GUARD_

/* Implementation will go here in future stories */

#endif /* VALUE_IMPL_GUARD_ */
#endif /* VALUE_IMPLEMENTATION */
