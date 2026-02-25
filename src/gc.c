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
    uint32_t epoch;        /* allocation epoch for watermark protection */
    uint8_t  mark;         /* mark bit (alternates 0/1 each GC cycle) */
    uint8_t  obj_type;     /* GCObjType — tells GC how to trace */
    uint16_t alloc_total;  /* total aligned allocation size (header + payload + padding) */
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

/* ======================================================================
 * Block-based heap allocator (Immix-style line granularity)
 * ====================================================================== */

/* --- Constants --- */

#define GC_BLOCK_SIZE       65536   /* 64KB per block */
#define GC_LINE_SIZE        128     /* bytes per line */
#define GC_LINES_PER_BLOCK  512     /* GC_BLOCK_SIZE / GC_LINE_SIZE */

#define GC_LINE_FREE     0
#define GC_LINE_OCCUPIED 1

/* --- GCBlock: one 64KB heap block with out-of-band line map --- */

typedef struct GCBlock {
    uint8_t         payload[GC_BLOCK_SIZE];       /* allocation area */
    uint8_t         line_map[GC_LINES_PER_BLOCK]; /* one byte per line */
    struct GCBlock *next;                          /* linked list */
} GCBlock;

/* --- BlockPool: thread-safe pool of free blocks --- */

typedef struct {
    GCBlock         *free_list;
    platform_mutex_t mutex;
} BlockPool;

static void gc_block_pool_init(BlockPool *pool) {
    pool->free_list = NULL;
    MUTEX_INIT(pool->mutex);
}

static GCBlock *gc_block_pool_get(BlockPool *pool) {
    GCBlock *block;
    MUTEX_LOCK(pool->mutex);
    block = pool->free_list;
    if (block) {
        pool->free_list = block->next;
    }
    MUTEX_UNLOCK(pool->mutex);

    if (!block) {
        block = (GCBlock *)malloc(sizeof(GCBlock));
        if (!block) return NULL;
    }
    memset(block->payload, 0, GC_BLOCK_SIZE);
    memset(block->line_map, GC_LINE_FREE, GC_LINES_PER_BLOCK);
    block->next = NULL;
    return block;
}

static void gc_block_pool_return(BlockPool *pool, GCBlock *block) {
    MUTEX_LOCK(pool->mutex);
    block->next = pool->free_list;
    pool->free_list = block;
    MUTEX_UNLOCK(pool->mutex);
}

static void gc_block_pool_destroy(BlockPool *pool) {
    GCBlock *b = pool->free_list;
    while (b) {
        GCBlock *n = b->next;
        free(b);
        b = n;
    }
    pool->free_list = NULL;
    MUTEX_DESTROY(pool->mutex);
}

/* --- GC trigger threshold --- */

#define GC_THRESHOLD (1024 * 1024) /* 1MB default */

/* --- ThreadHeap: per-thread heap state --- */

typedef struct {
    GCBlock   *blocks;         /* linked list of all owned blocks */
    GCBlock   *current_block;  /* block being allocated into */
    uint8_t   *cursor;         /* next allocation position */
    uint8_t   *limit;          /* end of current free-line run */
    size_t     bytes_since_gc; /* allocation counter for GC trigger */
    BlockPool *pool;           /* shared block pool */
    uint8_t    current_mark;   /* alternates 0/1 each GC cycle */
    bool       needs_gc;       /* set by gc_alloc when threshold exceeded */
} ThreadHeap;

static void gc_heap_init(ThreadHeap *heap, BlockPool *pool) {
    heap->blocks = NULL;
    heap->current_block = NULL;
    heap->cursor = NULL;
    heap->limit = NULL;
    heap->bytes_since_gc = 0;
    heap->pool = pool;
    heap->current_mark = 1; /* first GC marks with 1 (initial mark on objects is 0) */
    heap->needs_gc = false;
}

static void gc_heap_destroy(ThreadHeap *heap) {
    GCBlock *b = heap->blocks;
    while (b) {
        GCBlock *n = b->next;
        if (heap->pool) {
            gc_block_pool_return(heap->pool, b);
        } else {
            free(b);
        }
        b = n;
    }
    heap->blocks = NULL;
    heap->current_block = NULL;
    heap->cursor = NULL;
    heap->limit = NULL;
}

/* --- Internal helpers --- */

/* Mark all lines touched by an allocation at [offset, offset+size) */
static void gc__mark_lines(GCBlock *block, size_t offset, size_t size) {
    int first_line = (int)(offset / GC_LINE_SIZE);
    int last_line  = (int)((offset + size - 1) / GC_LINE_SIZE);
    int i;
    for (i = first_line; i <= last_line; i++) {
        block->line_map[i] = GC_LINE_OCCUPIED;
    }
}

/* Find next run of free lines starting from line index `from`.
 * Returns true if found, writing start index and length to out params. */
static bool gc__find_free_run(GCBlock *block, int from,
                              int *out_start, int *out_len) {
    int i = from;
    while (i < GC_LINES_PER_BLOCK && block->line_map[i] != GC_LINE_FREE)
        i++;
    if (i >= GC_LINES_PER_BLOCK) return false;

    *out_start = i;
    while (i < GC_LINES_PER_BLOCK && block->line_map[i] == GC_LINE_FREE)
        i++;
    *out_len = i - *out_start;
    return true;
}

/* Search a block (from line `from`) for a free run >= needed_lines.
 * On success, sets heap cursor/limit/current_block and returns true. */
static bool gc__find_fit_in_block(ThreadHeap *heap, GCBlock *block,
                                  int needed_lines, int from) {
    int run_start, run_len;
    int scan = from;
    while (gc__find_free_run(block, scan, &run_start, &run_len)) {
        if (run_len >= needed_lines) {
            heap->current_block = block;
            heap->cursor = block->payload
                         + ((size_t)run_start * GC_LINE_SIZE);
            heap->limit  = block->payload
                         + ((size_t)(run_start + run_len) * GC_LINE_SIZE);
            return true;
        }
        scan = run_start + run_len;
    }
    return false;
}

/* Bump-allocate at the current cursor position.
 * Caller must ensure cursor + total <= limit. */
static void *gc__bump_alloc(ThreadHeap *heap, size_t total, uint8_t obj_type) {
    uint8_t  *ptr = heap->cursor;
    GCHeader *hdr;

    heap->cursor += total;
    heap->bytes_since_gc += total;

    /* Flag GC if threshold exceeded (checked by VM at next safepoint) */
    if (heap->bytes_since_gc > GC_THRESHOLD) {
        heap->needs_gc = true;
    }

    gc__mark_lines(heap->current_block,
                   (size_t)(ptr - heap->current_block->payload), total);

    hdr = (GCHeader *)ptr;
    hdr->epoch       = 0;
    hdr->mark        = 0;
    hdr->obj_type    = obj_type;
    hdr->alloc_total = (uint16_t)total;

    return hdr + 1; /* payload pointer */
}

/* --- gc_alloc: bump-allocate with Immix-style line scanning --- */

static void *gc_alloc(ThreadHeap *heap, uint8_t obj_type, size_t payload_size) {
    size_t   total = sizeof(GCHeader) + payload_size;
    int      needed_lines;
    GCBlock *b;
    GCBlock *new_block;

    /* Align total to 8 bytes so next header stays aligned */
    total = (total + 7) & ~(size_t)7;

    if (total > GC_BLOCK_SIZE) return NULL;

    /* Fast path: bump within current free-line run */
    if (heap->cursor && heap->cursor + total <= heap->limit) {
        return gc__bump_alloc(heap, total, obj_type);
    }

    /* Slow path: scan all blocks from line 0 for a large-enough free run */
    needed_lines = (int)((total + GC_LINE_SIZE - 1) / GC_LINE_SIZE);
    b = heap->blocks;
    while (b) {
        if (gc__find_fit_in_block(heap, b, needed_lines, 0)) {
            return gc__bump_alloc(heap, total, obj_type);
        }
        b = b->next;
    }

    /* No room in any existing block — acquire a new one from the pool */
    new_block = gc_block_pool_get(heap->pool);
    if (!new_block) return NULL;

    new_block->next = heap->blocks;
    heap->blocks = new_block;
    heap->current_block = new_block;
    heap->cursor = new_block->payload;
    heap->limit  = new_block->payload + GC_BLOCK_SIZE;

    return gc__bump_alloc(heap, total, obj_type);
}

/* ======================================================================
 * Heap-allocating value constructors (moved from value.c — need ThreadHeap)
 * ====================================================================== */

static inline JaclVal jacl_i64(ThreadHeap *heap, int64_t n) {
    JaclHeapI64 *h = (JaclHeapI64 *)gc_alloc(heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    h->value = n;
    return JACL_TAG_I64 | ((uint64_t)(uintptr_t)h & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_u64(ThreadHeap *heap, uint64_t n) {
    JaclHeapU64 *h = (JaclHeapU64 *)gc_alloc(heap, OBJ_HEAP_U64, sizeof(JaclHeapU64));
    h->value = n;
    return JACL_TAG_U64 | ((uint64_t)(uintptr_t)h & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_f64(ThreadHeap *heap, double d) {
    JaclHeapF64 *h = (JaclHeapF64 *)gc_alloc(heap, OBJ_HEAP_F64, sizeof(JaclHeapF64));
    h->value = d;
    return JACL_TAG_F64 | ((uint64_t)(uintptr_t)h & JACL_PAYLOAD_MASK);
}

/* --- i64 arithmetic --- */

static inline JaclVal jacl_i64_add(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i64(heap, jacl_as_i64(a) + jacl_as_i64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_i64_sub(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i64(heap, jacl_as_i64(a) - jacl_as_i64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_i64_mul(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i64(heap, jacl_as_i64(a) * jacl_as_i64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_i64_div(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    int64_t divisor = jacl_as_i64(b);
    if (divisor == 0) {
        return jacl_apply_flags(jacl_set_error(jacl_i64(heap, 0)), flags);
    }
    int64_t dividend = jacl_as_i64(a);
    int64_t quot = (dividend == INT64_MIN && divisor == -1)
        ? INT64_MIN : (dividend / divisor);
    JaclVal result = jacl_i64(heap, quot);
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_i64_mod(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    int64_t divisor = jacl_as_i64(b);
    if (divisor == 0) {
        return jacl_apply_flags(jacl_set_error(jacl_i64(heap, 0)), flags);
    }
    int64_t dividend = jacl_as_i64(a);
    int64_t rem = (dividend == INT64_MIN && divisor == -1)
        ? 0 : (dividend % divisor);
    JaclVal result = jacl_i64(heap, rem);
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_i64_neg(ThreadHeap *heap, JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    JaclVal result = jacl_i64(heap, -jacl_as_i64(a));
    return jacl_apply_flags(result, flags);
}

/* --- u64 arithmetic --- */

static inline JaclVal jacl_u64_add(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) + jacl_as_u64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_u64_sub(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) - jacl_as_u64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_u64_mul(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) * jacl_as_u64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_u64_div(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    uint64_t divisor = jacl_as_u64(b);
    if (divisor == 0) {
        return jacl_apply_flags(jacl_set_error(jacl_u64(heap, 0)), flags);
    }
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) / divisor);
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_u64_mod(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    uint64_t divisor = jacl_as_u64(b);
    if (divisor == 0) {
        return jacl_apply_flags(jacl_set_error(jacl_u64(heap, 0)), flags);
    }
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) % divisor);
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_u64_neg(ThreadHeap *heap, JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    return jacl_apply_flags(jacl_set_error(jacl_u64(heap, 0)), flags);
}

/* --- f64 arithmetic --- */

static inline JaclVal jacl_f64_add(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f64(heap, jacl_as_f64(a) + jacl_as_f64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_f64_sub(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f64(heap, jacl_as_f64(a) - jacl_as_f64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_f64_mul(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f64(heap, jacl_as_f64(a) * jacl_as_f64(b));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_f64_div(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    double divisor = jacl_as_f64(b);
    if (divisor == 0.0) {
        return jacl_apply_flags(jacl_set_error(jacl_f64(heap, 0.0)), flags);
    }
    JaclVal result = jacl_f64(heap, jacl_as_f64(a) / divisor);
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_f64_mod(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    double divisor = jacl_as_f64(b);
    if (divisor == 0.0) {
        return jacl_apply_flags(jacl_set_error(jacl_f64(heap, 0.0)), flags);
    }
    JaclVal result = jacl_f64(heap, fmod(jacl_as_f64(a), divisor));
    return jacl_apply_flags(result, flags);
}

static inline JaclVal jacl_f64_neg(ThreadHeap *heap, JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    JaclVal result = jacl_f64(heap, -jacl_as_f64(a));
    return jacl_apply_flags(result, flags);
}

/* --- Thread-local heap pointer for collection templates (HAMT, RRB) ---
 * Set this before calling any collection mutation (set, unset, etc.)
 * so the template allocator can reach the current thread's GC heap.
 * Single static pointer for now; becomes __thread in US-008. */

static ThreadHeap *gc__current_heap = NULL;

#endif /* GC_C */
