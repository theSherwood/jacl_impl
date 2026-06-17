/* jaclrt.h — JACL runtime, value representation (Phase 1, P1.1)
 *
 * JaclVal is a 64-bit tagged value: an 8-bit tag byte (top) over a 56-bit
 * payload. Heap pointers are window offsets, which fit in 56 bits (the SVM window
 * is ~2^40). The tag/type layout mirrors the existing bytecode VM (src/jacl.h) so
 * ported builtins/collections stay compatible.
 *
 * P1.1 covers the inline scalars (nil/bool/i32/f32) + tag classification; heap
 * types (string/vector/map/i64/f64/...) get constructors once the allocator (P1.2)
 * and GC (P1.3) land.
 */
#ifndef JACLRT_H
#define JACLRT_H

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t JaclVal;

#define JACL_TAG_SHIFT     56
#define JACL_TAG_MASK      ((uint64_t)0xFF << JACL_TAG_SHIFT)
#define JACL_PAYLOAD_MASK  ((((uint64_t)1) << JACL_TAG_SHIFT) - 1)
#define JACL_TYPE_MASK     ((uint64_t)0x1F << JACL_TAG_SHIFT)   /* 5-bit type, ignores flag bits */

/* Type tags (index << shift). Mirrors src/jacl.h. */
#define JACL_TAG_NIL           ((uint64_t)0x00 << JACL_TAG_SHIFT)
#define JACL_TAG_BOOL          ((uint64_t)0x01 << JACL_TAG_SHIFT)
#define JACL_TAG_I32           ((uint64_t)0x02 << JACL_TAG_SHIFT)
#define JACL_TAG_F32           ((uint64_t)0x03 << JACL_TAG_SHIFT)
#define JACL_TAG_INLINE_STRING ((uint64_t)0x04 << JACL_TAG_SHIFT)
#define JACL_TAG_STRING        ((uint64_t)0x05 << JACL_TAG_SHIFT)
#define JACL_TAG_VECTOR        ((uint64_t)0x06 << JACL_TAG_SHIFT)
#define JACL_TAG_MAP           ((uint64_t)0x07 << JACL_TAG_SHIFT)
#define JACL_TAG_CLOSURE       ((uint64_t)0x08 << JACL_TAG_SHIFT)
#define JACL_TAG_BIGNUM        ((uint64_t)0x09 << JACL_TAG_SHIFT)
#define JACL_TAG_CELL          ((uint64_t)0x0A << JACL_TAG_SHIFT)
#define JACL_TAG_BOX           ((uint64_t)0x0B << JACL_TAG_SHIFT)
#define JACL_TAG_ATOM          ((uint64_t)0x0C << JACL_TAG_SHIFT)
#define JACL_TAG_U32           ((uint64_t)0x0D << JACL_TAG_SHIFT)
#define JACL_TAG_I64           ((uint64_t)0x0E << JACL_TAG_SHIFT)
#define JACL_TAG_U64           ((uint64_t)0x0F << JACL_TAG_SHIFT)
#define JACL_TAG_F64           ((uint64_t)0x10 << JACL_TAG_SHIFT)
#define JACL_TAG_STRUCT        ((uint64_t)0x12 << JACL_TAG_SHIFT)
#define JACL_TAG_ROPE_STRING   ((uint64_t)0x14 << JACL_TAG_SHIFT)

/* Bitmask of heap-managed type indices (after >> shift) — O(1) is-heap test.
 * Same constant as src/jacl.h (JACL_HEAP_TAG_MASK). */
#define JACL_HEAP_TAG_MASK     0x07D7DFE0u

/* Flag bits (61..63), above the 5-bit type. */
#define JACL_FLAG_ERROR        ((uint64_t)1 << 61)

#define JACL_NIL    ((JaclVal)JACL_TAG_NIL)
#define JACL_TRUE   ((JaclVal)(JACL_TAG_BOOL | 1))
#define JACL_FALSE  ((JaclVal)(JACL_TAG_BOOL))

/* ----- type queries ----- */
static inline uint32_t jaclrt_type_index(JaclVal v) {
  return (uint32_t)((v >> JACL_TAG_SHIFT) & 0x1F);
}
static inline bool jaclrt_is_nil(JaclVal v)  { return jaclrt_type_index(v) == 0x00; }
static inline bool jaclrt_is_bool(JaclVal v) { return jaclrt_type_index(v) == 0x01; }
static inline bool jaclrt_is_i32(JaclVal v)  { return jaclrt_type_index(v) == 0x02; }
static inline bool jaclrt_is_f32(JaclVal v)  { return jaclrt_type_index(v) == 0x03; }
/* Is this value a heap-managed pointer (the GC must trace/scan its payload)? */
static inline bool jaclrt_is_heap(JaclVal v) {
  return (JACL_HEAP_TAG_MASK >> jaclrt_type_index(v)) & 1u;
}
static inline bool jaclrt_is_error(JaclVal v) { return (v & JACL_FLAG_ERROR) != 0; }

/* ----- constructors (inline scalars) ----- */
static inline JaclVal jaclrt_nil(void)        { return JACL_NIL; }
static inline JaclVal jaclrt_bool(bool b)     { return b ? JACL_TRUE : JACL_FALSE; }
static inline JaclVal jaclrt_i32(int32_t x)   { return JACL_TAG_I32 | (uint64_t)(uint32_t)x; }

/* ----- extractors ----- */
static inline bool    jaclrt_as_bool(JaclVal v) { return (v & 1u) != 0; }
static inline int32_t jaclrt_as_i32(JaclVal v)  { return (int32_t)(uint32_t)(v & 0xFFFFFFFFu); }
/* Heap payload as a window offset (for P1.2+). */
static inline uint64_t jaclrt_payload(JaclVal v) { return v & JACL_PAYLOAD_MASK; }

/* ===================================================================
 * Heap + GC (P1.2 / P1.3) — non-inline; defined in heap_gc.c.
 * Conservative, non-moving mark-sweep over a heap region; roots via the
 * svm `gc.roots` op (__vm_gc_roots). Object pointers are the cell address
 * (header included). See heap_gc.c.
 * =================================================================== */

/* GC object types (expands as real heap types land in P1.4/P1.5). */
enum {
  JOBJ_FREE = 0,   /* a swept/free cell (skipped by tracing) */
  JOBJ_BLOB = 1,   /* opaque payload, no outgoing pointers   */
  JOBJ_NODE = 2,   /* payload may contain heap pointers (conservatively traced) */
};

typedef struct JaclObj {
  uint32_t size;     /* total cell size in bytes (header + payload), granule-aligned */
  uint8_t  obj_type; /* JOBJ_* */
  uint8_t  mark;     /* GC mark bit */
  uint16_t _rsv;
} JaclObj;

void   jacl_heap_init(void);                              /* reset the heap */
void*  jacl_alloc(uint32_t obj_type, uint32_t payload);   /* -> object cell (header+payload), zeroed payload */
long   jacl_gc_collect(void);                             /* mark-sweep; returns #cells reclaimed */
long   jacl_live_count(void);                             /* #live (non-free) cells */
long   jacl_heap_lo(void);                                /* window-offset bounds, for gc.roots */
long   jacl_heap_hi(void);
static inline void*    jacl_obj_payload(JaclObj* o) { return (void*)((uint8_t*)o + sizeof(JaclObj)); }

#endif /* JACLRT_H */
