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

/* --- Predicates --- */

static inline bool jacl_is_nil(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_NIL;
}

static inline bool jacl_is_bool(JaclVal v) {
    return (v & JACL_TYPE_MASK) == JACL_TAG_BOOL;
}

/* --- Extractors --- */

static inline bool jacl_as_bool(JaclVal v) {
    return (v & JACL_PAYLOAD_MASK) != 0;
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
