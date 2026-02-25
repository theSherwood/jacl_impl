/*
 * JACL Garbage Collector — Header, type system, and allocator.
 *
 * GCHeader (8 bytes) is prepended before every GC-managed object's payload,
 * replacing the 16-byte RCHeader used by collections. The gc_header_of()
 * helper recovers the header from a payload pointer.
 */

#ifndef GC_C
#define GC_C

/* --- GC object type enumeration --- */

typedef enum {
    OBJ_CLOSURE,
    OBJ_STRING,
    OBJ_HEAP_I64,
    OBJ_HEAP_U64,
    OBJ_HEAP_F64,
    OBJ_MUTABLE_REF,
    OBJ_BIGNUM,
    OBJ_HAMT_INTERNAL,
    OBJ_HAMT_LEAF,
    OBJ_HAMT_COLLISION,
    OBJ_RRB_INTERNAL,
    OBJ_RRB_LEAF,
    OBJ_RRB_ROOT
} GCObjType;

/* --- GC object header (8 bytes, prepended before payload) --- */

typedef struct {
    uint32_t epoch;     /* allocation epoch for watermark protection */
    uint8_t  mark;      /* mark bit (alternates 0/1 each GC cycle) */
    uint8_t  obj_type;  /* GCObjType — tells GC how to trace */
    uint8_t  gen;       /* generation (0 = young, 1 = old) */
    uint8_t  _pad;      /* alignment padding */
} GCHeader;

/* --- Header access --- */

static inline GCHeader *gc_header_of(void *payload) {
    return (GCHeader *)payload - 1;
}

/* --- Heap type predicate --- */

static inline bool jacl_is_heap_type(JaclVal v) {
    uint64_t tag = v & JACL_TYPE_MASK;
    return tag == JACL_TAG_STRING
        || tag == JACL_TAG_VECTOR
        || tag == JACL_TAG_MAP
        || tag == JACL_TAG_CLOSURE
        || tag == JACL_TAG_BIGNUM
        || tag == JACL_TAG_CELL
        || tag == JACL_TAG_BOX
        || tag == JACL_TAG_ATOM
        || tag == JACL_TAG_I64
        || tag == JACL_TAG_U64
        || tag == JACL_TAG_F64;
}

#endif /* GC_C */
