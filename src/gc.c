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
    OBJ_RRB_ROOT,
    OBJ_FUTURE,
    OBJ_FUTURE_WAITER,
    OBJ_PARALLEL_AGG,
    OBJ_RACE_AGG,
    OBJ_STRUCT,
    OBJ_ROPE_STRING,
    OBJ_ROPE_LEAF,
    OBJ_ROPE_INTERNAL,
    OBJ_STREAM
} GCObjType;

/* --- GC object header (8 bytes, prepended before payload) ---
 *
 * Bit layout of the mark/gen byte (offset 4):
 *   bit 0:    mark          — alternates 0/1 each GC cycle
 *   bit 1:    gen           — generation: 0 = young, 1 = old
 *   bits 2-3: survive_count — number of GC cycles survived (0-3); promoted at 2
 *   bits 4-7: reserved      — available for future flags
 */

typedef struct {
    /* Allocation epoch for watermark protection.
     *
     * Intentionally uint32_t (not uint64_t) for header compactness — keeps
     * GCHeader at 8 bytes total.  Truncated from the uint64_t global_epoch
     * via the path: global_epoch (u64) → thread_epoch (u64) →
     * gc__thread_epoch (u32) → hdr->epoch (u32).
     *
     * Wraps after ~4 billion GC cycles.  On wraparound the watermark
     * comparison (epoch >= watermark) may produce false positives —
     * protecting objects that could be swept — but never false negatives,
     * so correctness is preserved (conservative, not dangerous). */
    uint32_t epoch;
    uint8_t  mark          : 1; /* mark bit (alternates 0/1 each GC cycle) */
    uint8_t  gen           : 1; /* generation: 0 = young, 1 = old */
    uint8_t  survive_count : 2; /* GC cycles survived (0-3); promoted at 2 */
    uint8_t  _reserved     : 4; /* reserved for future flags */
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
        || tag == JACL_TAG_F64
        || tag == JACL_TAG_FUTURE
        || tag == JACL_TAG_STRUCT
        || tag == JACL_TAG_ROPE_STRING;
}

/* ======================================================================
 * Block-based heap allocator (Immix-style line granularity)
 * ====================================================================== */

/* --- Constants --- */

#define GC_BLOCK_SIZE       65536   /* 64KB per block */
#define GC_LINE_SIZE        128     /* bytes per line */
#define GC_LINES_PER_BLOCK  512     /* GC_BLOCK_SIZE / GC_LINE_SIZE */

/* --- OOM escalation: maximum heap blocks (compile-time configurable) --- */

#ifndef GC_MAX_HEAP_BLOCKS
#define GC_MAX_HEAP_BLOCKS 4096   /* 4096 * 64KB = 256MB */
#endif

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
    uint32_t         total_blocks_allocated;  /* blocks malloc'd from OS */
    uint32_t         max_blocks;              /* limit (default GC_MAX_HEAP_BLOCKS) */
} BlockPool;

static void gc_block_pool_init(BlockPool *pool) {
    pool->free_list = NULL;
    MUTEX_INIT(pool->mutex);
    pool->total_blocks_allocated = 0;
    pool->max_blocks = GC_MAX_HEAP_BLOCKS;
}

static GCBlock *gc_block_pool_get(BlockPool *pool) {
    GCBlock *block;
    bool need_malloc = false;

    MUTEX_LOCK(pool->mutex);
    block = pool->free_list;
    if (block) {
        pool->free_list = block->next;
    } else {
        /* No free block — check limit and reserve a slot atomically */
        if (pool->total_blocks_allocated >= pool->max_blocks) {
            MUTEX_UNLOCK(pool->mutex);
            return NULL;
        }
        pool->total_blocks_allocated++;
        need_malloc = true;
    }
    MUTEX_UNLOCK(pool->mutex);

    if (need_malloc) {
        block = (GCBlock *)malloc(sizeof(GCBlock));
        if (!block) {
            /* malloc failed — release the reserved slot */
            MUTEX_LOCK(pool->mutex);
            pool->total_blocks_allocated--;
            MUTEX_UNLOCK(pool->mutex);
            return NULL;
        }
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

#define GC_THRESHOLD     (1024 * 1024)        /* 1MB default */
#define GC_THRESHOLD_MIN (256 * 1024)         /* 256KB minimum */
#define GC_THRESHOLD_MAX (16 * 1024 * 1024)   /* 16MB maximum */

/* Thread-local epoch for gc_alloc stamping. Workers set this from their
 * thread_epoch at the start of each task. Main thread leaves it at 0
 * (single-threaded mode doesn't use epoch watermarking). */
static JACL_THREAD_LOCAL uint32_t gc__thread_epoch = 0;

/* --- ThreadHeap: per-thread heap state --- */

typedef struct {
    GCBlock   *blocks;         /* linked list of all owned blocks */
    GCBlock   *current_block;  /* block being allocated into */
    uint8_t   *cursor;         /* next allocation position */
    uint8_t   *limit;          /* end of current free-line run */
    size_t     bytes_since_gc; /* allocation counter for GC trigger */
    size_t     gc_threshold;   /* adaptive threshold (starts at GC_THRESHOLD) */
    BlockPool *pool;           /* shared block pool */
    uint8_t    current_mark;   /* alternates 0/1 each GC cycle */
    bool       needs_gc;       /* set by gc_alloc when threshold exceeded */
    /* Generational GC scheduling */
    size_t     old_gen_bytes;           /* total bytes in old generation */
    size_t     last_major_old_gen_bytes; /* old_gen_bytes at end of last major GC */
    uint32_t   gc_cycle_count;          /* total GC cycles run (first cycle is major) */
} ThreadHeap;

static void gc_heap_init(ThreadHeap *heap, BlockPool *pool) {
    heap->blocks = NULL;
    heap->current_block = NULL;
    heap->cursor = NULL;
    heap->limit = NULL;
    heap->bytes_since_gc = 0;
    heap->gc_threshold   = GC_THRESHOLD;
    heap->pool = pool;
    heap->current_mark = 1; /* first GC marks with 1 (initial mark on objects is 0) */
    heap->needs_gc = false;
    heap->old_gen_bytes           = 0;
    heap->last_major_old_gen_bytes = 0;
    heap->gc_cycle_count          = 0;
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

/* --- Emergency GC callback (set by vm.c or runtime.c) --- */

static JACL_THREAD_LOCAL void (*gc__emergency_gc_fn)(void *ctx) = NULL;
static JACL_THREAD_LOCAL void *gc__emergency_gc_ctx = NULL;

/* --- OOM panic handler --- */

static void gc__oom_panic_default(ThreadHeap *heap, size_t request_size) {
    fprintf(stderr, "JACL OOM PANIC:\n");
    fprintf(stderr, "  Heap blocks: %u / %u\n",
            heap->pool->total_blocks_allocated, heap->pool->max_blocks);
    fprintf(stderr, "  Heap size: %zuKB\n",
            (size_t)heap->pool->total_blocks_allocated * (GC_BLOCK_SIZE / 1024));
    fprintf(stderr, "  Allocation request: %zu bytes\n", request_size);
    fprintf(stderr, "  GC threshold: %zu bytes\n", heap->gc_threshold);
    abort();
}

/* Override for testing (set to non-NULL to intercept panic) */
static void (*gc__oom_handler)(ThreadHeap *, size_t) = gc__oom_panic_default;

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

    /* Flag GC if adaptive threshold exceeded (checked by VM at next safepoint) */
    if (heap->bytes_since_gc > heap->gc_threshold) {
        heap->needs_gc = true;
    }

    gc__mark_lines(heap->current_block,
                   (size_t)(ptr - heap->current_block->payload), total);

    hdr = (GCHeader *)ptr;
    /* Epoch truncation path: global_epoch (u64) → worker.thread_epoch (u64)
     * → gc__thread_epoch (u32, set at task start in runtime.c) → hdr->epoch
     * (u32).  See GCHeader comment for wraparound safety analysis. */
    hdr->epoch       = gc__thread_epoch;
    hdr->mark          = 0;
    hdr->gen           = 0; /* young generation */
    hdr->survive_count = 0;
    hdr->obj_type      = obj_type;
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

    if (total >= GC_BLOCK_SIZE) return NULL;

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
    if (new_block) goto got_block;

    /* ============================================================
     * OOM Escalation (3-tier: GC_CONCURRENCY_DESIGN.md Section 15)
     * ============================================================ */

    /* Tier 1: Emergency synchronous GC */
    if (gc__emergency_gc_fn) {
        gc__emergency_gc_fn(gc__emergency_gc_ctx);
    }

    /* After emergency GC: rescan existing blocks (GC may have freed runs) */
    b = heap->blocks;
    while (b) {
        if (gc__find_fit_in_block(heap, b, needed_lines, 0)) {
            return gc__bump_alloc(heap, total, obj_type);
        }
        b = b->next;
    }

    /* After emergency GC: retry pool (GC may have recycled blocks) */
    new_block = gc_block_pool_get(heap->pool);
    if (new_block) goto got_block;

    /* Tier 2: Heap growth up to limit (atomic check + reserve) */
    {
        bool reserved = false;
        MUTEX_LOCK(heap->pool->mutex);
        if (heap->pool->total_blocks_allocated < heap->pool->max_blocks) {
            heap->pool->total_blocks_allocated++;
            reserved = true;
        }
        MUTEX_UNLOCK(heap->pool->mutex);

        if (reserved) {
            new_block = (GCBlock *)malloc(sizeof(GCBlock));
            if (new_block) {
                memset(new_block->payload, 0, GC_BLOCK_SIZE);
                memset(new_block->line_map, GC_LINE_FREE, GC_LINES_PER_BLOCK);
                new_block->next = NULL;
                goto got_block;
            }
            /* malloc failed — release the reserved slot */
            MUTEX_LOCK(heap->pool->mutex);
            heap->pool->total_blocks_allocated--;
            MUTEX_UNLOCK(heap->pool->mutex);
        }
    }

    /* Tier 3: Panic */
    gc__oom_handler(heap, total);
    return NULL; /* unreachable (handler aborts) */

got_block:
    new_block->next = heap->blocks;
    heap->blocks = new_block;
    heap->current_block = new_block;
    heap->cursor = new_block->payload;
    heap->limit  = new_block->payload + GC_BLOCK_SIZE;

    return gc__bump_alloc(heap, total, obj_type);
}

/* --- Struct heap type --- */

typedef struct {
    uint32_t type_idx;    /* index into StructTypeRegistry */
    uint32_t _pad;        /* padding to 8-byte alignment */
    uint8_t  data[];      /* C-ABI laid out field data (total_size bytes) */
} JaclStruct;

static inline JaclStruct* jacl_as_struct_ptr(JaclVal v) {
    return (JaclStruct*)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

static inline JaclVal jacl_struct_val(JaclStruct* s) {
    return JACL_TAG_STRUCT | ((uint64_t)(uintptr_t)s & JACL_PAYLOAD_MASK);
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
 * Each worker thread sets its own copy in the task execution loop. */

static JACL_THREAD_LOCAL ThreadHeap *gc__current_heap = NULL;

/* --- Global struct registry pointer for GC tracing ---
 * Type is void* because StructTypeRegistry is defined in compiler.c,
 * which is included after gc.c in the unity build. Cast to
 * StructTypeRegistry* in gc_collect.c (included after compiler.c). */
static void *gc__struct_registry = NULL;

/* ======================================================================
 * GreyBuffer: per-thread append-only buffer for write barrier entries.
 * Thread-local writes only (no contention). GC reads a snapshot of count
 * during mark phase draining.
 * ====================================================================== */

#define GREY_BUF_INIT_CAP 256

typedef struct {
    JaclVal  *entries;
    uint32_t  count;
    uint32_t  cap;
} GreyBuffer;

static void grey_buf_init(GreyBuffer *gb) {
    gb->entries = (JaclVal *)malloc(GREY_BUF_INIT_CAP * sizeof(JaclVal));
    gb->count   = 0;
    gb->cap     = GREY_BUF_INIT_CAP;
}

/* grey_buf_push — Thread-local push with concurrent GC reader safety.
 *
 * Race scenario: A worker thread calls grey_buf_push (which may realloc the
 * entries array) while the GC thread concurrently reads entries via
 * gc__drain_grey_bufs.
 *
 * Why this is safe:
 *   1. realloc() copies all existing entries to the new buffer before
 *      returning, so entries[0..c-1] are valid in the new allocation.
 *   2. gb->entries[c] = v writes the new entry into the (possibly new) buffer.
 *   3. The release store to gb->count (below) happens-after both the realloc
 *      (which updated gb->entries) and the entry write (gb->entries[c] = v).
 *   4. In gc__drain_grey_bufs, the acquire load of gb->count sees the new
 *      count only after the release store, which transitively makes both the
 *      new gb->entries pointer and all entries[0..count-1] visible.
 *
 * Critical invariant: the gb->entries pointer store (from realloc) and the
 * gb->entries[c] store MUST both happen-before the release store to count.
 * This is guaranteed because they are all plain stores in the same thread,
 * and the release fence on count prevents reordering past them.
 *
 * Note: the GC thread only reads entries[drained[i]..count-1], never the
 * entries pointer directly through a load — it reads gb->entries once per
 * drain call, and the acquire on count ensures it sees the post-realloc
 * pointer if count reflects post-realloc entries. */
static void grey_buf_push(GreyBuffer *gb, JaclVal v) {
    uint32_t c = gb->count;
    if (c >= gb->cap) {
        uint32_t new_cap = gb->cap * 2;
        gb->entries = (JaclVal *)realloc(gb->entries,
                                          (size_t)new_cap * sizeof(JaclVal));
        gb->cap = new_cap;
    }
    gb->entries[c] = v;
    /* Release on count ensures entries[0..count-1] are visible to GC thread
     * after acquire load of count */
    ATOMIC_STORE_EXPLICIT(&gb->count, c + 1, MEM_RELEASE);
}

static void grey_buf_destroy(GreyBuffer *gb) {
    free(gb->entries);
    gb->entries = NULL;
    gb->count   = 0;
    gb->cap     = 0;
}

/* ======================================================================
 * RememberedSet: per-thread append-only buffer tracking old-to-young
 * pointers created by box/atom/cell mutations. Structurally identical
 * to GreyBuffer but maintained between GC cycles (not gated on gc_active).
 *
 * Stores container JaclVal references (the old-gen mutable ref whose
 * value now points to a young-gen object).
 * ====================================================================== */

#define REMEMBERED_SET_INIT_CAP 256

typedef struct {
    JaclVal  *entries;
    uint32_t  count;
    uint32_t  cap;
} RememberedSet;

static void remembered_set_init(RememberedSet *rs) {
    rs->entries = (JaclVal *)malloc(REMEMBERED_SET_INIT_CAP * sizeof(JaclVal));
    rs->count   = 0;
    rs->cap     = REMEMBERED_SET_INIT_CAP;
}

static void remembered_set_push(RememberedSet *rs, JaclVal container) {
    uint32_t c = rs->count;
    if (c >= rs->cap) {
        uint32_t new_cap = rs->cap * 2;
        rs->entries = (JaclVal *)realloc(rs->entries,
                                          (size_t)new_cap * sizeof(JaclVal));
        rs->cap = new_cap;
    }
    rs->entries[c] = container;
    rs->count = c + 1;
}

/* Drain the remembered set, returning deduplicated entries.
 * Writes unique container values to out_entries (caller-allocated),
 * returns the count of unique entries, and resets the set to empty. */
static uint32_t remembered_set_drain(RememberedSet *rs,
                                      JaclVal *out_entries,
                                      uint32_t out_cap) {
    uint32_t unique = 0;
    for (uint32_t i = 0; i < rs->count && unique < out_cap; i++) {
        JaclVal v = rs->entries[i];
        /* Deduplicate: linear scan (set is typically small) */
        bool dup = false;
        for (uint32_t j = 0; j < unique; j++) {
            if (out_entries[j] == v) { dup = true; break; }
        }
        if (!dup) {
            out_entries[unique++] = v;
        }
    }
    rs->count = 0;
    return unique;
}

static void remembered_set_destroy(RememberedSet *rs) {
    free(rs->entries);
    rs->entries = NULL;
    rs->count   = 0;
    rs->cap     = 0;
}

/* ======================================================================
 * Write barrier: hybrid SATB (deletion) + insertion barrier.
 *
 * Fires on mutable container mutations (reset!, swap!, set! on cells)
 * during an active concurrent GC cycle. Both the old value (evicted from
 * the container) and the new value (stored into the container) are pushed
 * to the thread-local grey buffer for the GC to process during marking.
 *
 * Fast path: single relaxed atomic load of gc_active flag — no overhead
 * when GC is not running or when running in single-threaded mode (NULL).
 * ====================================================================== */

static inline void gc_write_barrier(GreyBuffer *gb,
                                     volatile uint32_t *gc_active_ptr,
                                     JaclVal old_val, JaclVal new_val) {
    /* Single-threaded mode: no gc_active flag → no barrier needed */
    if (!gc_active_ptr) return;

    /* Fast path: GC not running → skip */
    if (!ATOMIC_LOAD_EXPLICIT(gc_active_ptr, MEM_RELAXED)) return;

    /* SATB deletion barrier: protect old value still on some VM stack */
    if (jacl_is_heap_type(old_val))
        grey_buf_push(gb, old_val);

    /* Insertion barrier: protect new value stored after GC scanned container */
    if (jacl_is_heap_type(new_val))
        grey_buf_push(gb, new_val);
}

/* ======================================================================
 * Generational write barrier: records old-to-young pointer creation.
 *
 * Fires unconditionally (not gated on gc_active) whenever a mutable
 * container (box/atom/cell) is mutated. If the container is old-gen
 * (gen == 1) and the new value is young-gen (gen == 0), the container
 * is recorded in the per-thread remembered set.
 *
 * This enables minor GC to find young objects reachable only through
 * old-gen containers without scanning the entire old generation.
 * ====================================================================== */

static inline void gc_remembered_set_barrier(RememberedSet *rs,
                                              JaclVal container,
                                              JaclVal new_val) {
    /* No remembered set in single-threaded mode */
    if (!rs) return;

    /* Both container and new value must be heap objects */
    if (!jacl_is_heap_type(container) || !jacl_is_heap_type(new_val)) return;

    GCHeader *container_hdr = gc_header_of(jacl_as_ptr(container));
    GCHeader *new_hdr       = gc_header_of(jacl_as_ptr(new_val));

    /* Record when old-gen container stores a young-gen value */
    if (container_hdr->gen == 1 && new_hdr->gen == 0) {
        remembered_set_push(rs, container);
    }
}

/* ======================================================================
 * Future type: concurrent task handle for spawn/await
 *
 * JaclFuture is a mutable container like atom — uses atomic state/result.
 * Waiter list is GC-managed (no malloc). The spinlock only contends
 * during the resolve-vs-add_waiter race window.
 * ====================================================================== */

/* Future states */
#define FUTURE_PENDING  0
#define FUTURE_RESOLVED 1
#define FUTURE_ERROR    2

/* FutureWaiter: singly-linked list node for continuations waiting on a future */
typedef struct FutureWaiter {
    JaclVal               continuation; /* closure to call with result */
    struct FutureWaiter  *next;
} FutureWaiter;

/* JaclFuture: the future value itself */
typedef struct {
    volatile uint32_t   state;       /* FUTURE_PENDING / RESOLVED / ERROR */
    volatile uint64_t   result;      /* JaclVal result (valid when resolved/errored) */
    FutureWaiter       *waiters;     /* linked list of waiting continuations */
    volatile uint32_t   lock;        /* CAS spinlock: 0=unlocked, 1=locked */
} JaclFuture;

/* --- Pointer tag/untag for futures --- */

static inline JaclVal jacl_future_ptr(JaclFuture *p) {
    return JACL_TAG_FUTURE | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

static inline JaclFuture *jacl_as_future(JaclVal v) {
    return (JaclFuture *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* --- Spinlock for futures: CAS-based, no OS resource to leak on GC --- */

static inline void future_lock(JaclFuture *f) {
    uint32_t expected = 0;
    while (!ATOMIC_CAS(&f->lock, &expected, 1, MEM_ACQUIRE, MEM_RELAXED))
        expected = 0;
}

static inline void future_unlock(JaclFuture *f) {
    ATOMIC_STORE_EXPLICIT(&f->lock, 0, MEM_RELEASE);
}

/* --- Constructor: create a pending future --- */

static JaclVal jacl_future(ThreadHeap *heap) {
    JaclFuture *f = (JaclFuture *)gc_alloc(heap, OBJ_FUTURE, sizeof(JaclFuture));
    f->state   = FUTURE_PENDING;
    f->result  = (uint64_t)JACL_NIL;
    f->waiters = NULL;
    f->lock = 0;
    return jacl_future_ptr(f);
}

/* --- Resolve: set result + RESOLVED state, return waiter list.
 *     Fires write barrier (old=NIL, new=result) for concurrent GC. --- */

static FutureWaiter *jacl_future_resolve(JaclFuture *f, JaclVal result,
                                          GreyBuffer *gb,
                                          volatile uint32_t *gc_active_ptr) {
    FutureWaiter *waiters;
    gc_write_barrier(gb, gc_active_ptr, JACL_NIL, result);
    future_lock(f);
    f->result = (uint64_t)result;
    ATOMIC_STORE_EXPLICIT(&f->state, FUTURE_RESOLVED, MEM_RELEASE);
    waiters = f->waiters;
    f->waiters = NULL;
    future_unlock(f);
    return waiters;
}

/* --- Error: set error result + ERROR state, return waiter list.
 *     Fires write barrier (old=NIL, new=error) for concurrent GC. --- */

static FutureWaiter *jacl_future_error(JaclFuture *f, JaclVal error,
                                        GreyBuffer *gb,
                                        volatile uint32_t *gc_active_ptr) {
    FutureWaiter *waiters;
    gc_write_barrier(gb, gc_active_ptr, JACL_NIL, error);
    future_lock(f);
    f->result = (uint64_t)error;
    ATOMIC_STORE_EXPLICIT(&f->state, FUTURE_ERROR, MEM_RELEASE);
    waiters = f->waiters;
    f->waiters = NULL;
    future_unlock(f);
    return waiters;
}

/* --- Add waiter: if pending, prepend waiter and return true.
 *     If already resolved/errored, return false (caller schedules immediately). --- */

static bool jacl_future_add_waiter(JaclFuture *f, JaclVal continuation,
                                    ThreadHeap *heap) {
    bool added = false;
    future_lock(f);
    if (ATOMIC_LOAD_EXPLICIT(&f->state, MEM_RELAXED) == FUTURE_PENDING) {
        FutureWaiter *w = (FutureWaiter *)gc_alloc(heap, OBJ_FUTURE_WAITER,
                                                     sizeof(FutureWaiter));
        w->continuation = continuation;
        w->next = f->waiters;
        f->waiters = w;
        added = true;
    }
    future_unlock(f);
    return added;
}

/* ======================================================================
 * JaclStream: lazy sequence value type
 * ====================================================================== */

#define STREAM_PENDING   0
#define STREAM_CONSUMED  1
#define STREAM_EXHAUSTED 2
#define STREAM_ERROR     3

#define STREAM_KIND_GENERATOR  0
#define STREAM_KIND_FILTER     1
#define STREAM_KIND_TRANSFORM  2
#define STREAM_KIND_TAKE       3
#define STREAM_KIND_LINES      4

#define STREAM_MAX_ARGS          8

typedef struct {
    uint32_t  state;         /* STREAM_PENDING / CONSUMED / EXHAUSTED / ERROR */
    uint32_t  kind;          /* STREAM_KIND_GENERATOR / FILTER */
    JaclVal   next_fn;       /* CPS continuation closure (nil when exhausted/error) */
    JaclVal   cached_value;  /* cached current element */
    /* Deferred first-call arguments (saved at generator creation, used on first pull) */
    JaclVal   args[STREAM_MAX_ARGS];
    uint8_t   arg_count;
} JaclStream;

static inline JaclStream *jacl_as_stream(JaclVal v) {
    return (JaclStream *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

static JaclVal jacl_stream(ThreadHeap *heap) {
    JaclStream *s = (JaclStream *)gc_alloc(heap, OBJ_STREAM, sizeof(JaclStream));
    s->state        = STREAM_PENDING;
    s->kind         = STREAM_KIND_GENERATOR;
    s->next_fn      = JACL_NIL;
    s->cached_value = JACL_NIL;
    s->arg_count    = 0;
    return jacl_stream_ptr(s);
}

/* ======================================================================
 * ParallelAgg: aggregate tracker for [parallel] primitive
 *
 * Tracks N concurrent tasks, collects results in input order, and
 * schedules the join continuation when all tasks complete.
 * Uses atomic counter for lock-free completion tracking.
 * ====================================================================== */

typedef struct {
    volatile uint32_t completed;     /* atomic completion counter */
    volatile uint32_t errored;       /* 0 or 1 (first-error-wins CAS) */
    volatile uint64_t error_val;     /* first error value (JaclVal) */
    uint32_t          count;         /* total N */
    JaclVal           continuation;  /* join continuation closure */
    JaclVal           results[];     /* trailing array of N result slots */
} ParallelAgg;

/* Tag/untag helpers — reuses JACL_TAG_FUTURE for pointer tagging since
 * ParallelAgg is an internal GC object (OBJ_PARALLEL_AGG obj_type in
 * GCHeader ensures correct tracing). Never exposed to user code. */

static inline JaclVal parallel_agg_ptr(ParallelAgg *p) {
    return JACL_TAG_FUTURE | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

static inline ParallelAgg *as_parallel_agg(JaclVal v) {
    return (ParallelAgg *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* Constructor: allocate a pending parallel aggregate with N result slots */
static JaclVal jacl_parallel_agg(ThreadHeap *heap, uint32_t count,
                                  JaclVal continuation) {
    size_t sz = sizeof(ParallelAgg) + count * sizeof(JaclVal);
    ParallelAgg *agg = (ParallelAgg *)gc_alloc(heap, OBJ_PARALLEL_AGG, sz);
    agg->completed    = 0;
    agg->errored      = 0;
    agg->error_val    = (uint64_t)JACL_NIL;
    agg->count        = count;
    agg->continuation = continuation;
    for (uint32_t i = 0; i < count; i++) {
        agg->results[i] = JACL_NIL;
    }
    return parallel_agg_ptr(agg);
}

/* ===================================================================
 *  RaceAgg — GC-managed race tracking structure
 * =================================================================== */

/* RaceAgg tracks a first-to-complete race among N tasks.
 * Uses CAS on settled flag to determine the single winner. */
typedef struct {
    volatile uint32_t settled;       /* 0 = not settled, 1 = winner determined */
    JaclVal           continuation;  /* race continuation closure */
} RaceAgg;

/* Tag/untag helpers — same tagging scheme as ParallelAgg */

static inline JaclVal race_agg_ptr(RaceAgg *r) {
    return JACL_TAG_FUTURE | ((uint64_t)(uintptr_t)r & JACL_PAYLOAD_MASK);
}

static inline RaceAgg *as_race_agg(JaclVal v) {
    return (RaceAgg *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* Constructor: allocate a pending race aggregate */
static JaclVal jacl_race_agg(ThreadHeap *heap, JaclVal continuation) {
    RaceAgg *agg = (RaceAgg *)gc_alloc(heap, OBJ_RACE_AGG, sizeof(RaceAgg));
    agg->settled      = 0;
    agg->continuation = continuation;
    return race_agg_ptr(agg);
}

#endif /* GC_C */
