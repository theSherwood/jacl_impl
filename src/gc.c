/*
 * JACL Garbage Collector — Header, type system, and allocator.
 *
 * GCHeader (8 bytes) is prepended before every GC-managed object's payload,
 * replacing the 16-byte RCHeader used by collections. The gc_header_of()
 * helper recovers the header from a payload pointer.
 */

#ifndef GC_C
#define GC_C

/* --- Bitmap helpers for inline struct slot tracking (US-014) --- */
#ifndef BITMAP_SET
#define BITMAP_SET(bm, idx) ((bm)[(idx) / 8] |=  (uint8_t)(1u << ((idx) % 8)))
#define BITMAP_CLR(bm, idx) ((bm)[(idx) / 8] &= (uint8_t)~(1u << ((idx) % 8)))
#define BITMAP_GET(bm, idx) (((bm)[(idx) / 8] >> ((idx) % 8)) & 1u)
#endif

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
    OBJ_HEAP_RECORD,
    OBJ_ROPE_STRING,
    OBJ_ROPE_LEAF,
    OBJ_ROPE_INTERNAL,
    OBJ_STREAM,
    OBJ_STATE_MACHINE,
    OBJ_SYNTAX,
    OBJ_TYPED_RRB_LEAF,
    OBJ_TYPED_HAMT_LEAF
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

GCHeader *gc_header_of(void *payload) {
    return (GCHeader *)payload - 1;
}

/* --- Heap type predicate --- */

bool jacl_is_heap_type(JaclVal v) {
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
        || tag == JACL_TAG_ROPE_STRING
        || tag == JACL_TAG_STATE_MACHINE
        || tag == JACL_TAG_SYNTAX
        || tag == JACL_TAG_TYPED_VECTOR
        || tag == JACL_TAG_TYPED_MAP;
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

void gc_block_pool_init(BlockPool *pool) {
    pool->free_list = NULL;
    MUTEX_INIT(pool->mutex);
    pool->total_blocks_allocated = 0;
    pool->max_blocks = GC_MAX_HEAP_BLOCKS;
}

GCBlock *gc_block_pool_get(BlockPool *pool) {
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

void gc_block_pool_return(BlockPool *pool, GCBlock *block) {
    MUTEX_LOCK(pool->mutex);
    block->next = pool->free_list;
    pool->free_list = block;
    MUTEX_UNLOCK(pool->mutex);
}

void gc_block_pool_destroy(BlockPool *pool) {
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
JACL_THREAD_LOCAL uint32_t gc__thread_epoch = 0;

/* --- ThreadHeap: per-thread heap state --- */

typedef struct {
    GCBlock         *blocks;         /* linked list of all owned blocks */
    GCBlock         *current_block;  /* block being allocated into */
    uint8_t         *cursor;         /* next allocation position */
    uint8_t         *limit;          /* end of current free-line run */
    size_t           bytes_since_gc; /* allocation counter for GC trigger */
    size_t           gc_threshold;   /* adaptive threshold (starts at GC_THRESHOLD) */
    BlockPool       *pool;           /* shared block pool */
    uint8_t          current_mark;   /* alternates 0/1 each GC cycle */
    bool             needs_gc;       /* set by gc_alloc when threshold exceeded */
    /* Generational GC scheduling */
    size_t           old_gen_bytes;           /* total bytes in old generation */
    size_t           last_major_old_gen_bytes; /* old_gen_bytes at end of last major GC */
    uint32_t         gc_cycle_count;          /* total GC cycles run (first cycle is major) */
    /* Mutex serializing blocks-list mutation (head-insert in gc_alloc slow
     * path) with gc_sweep_concurrent's traversal + unlink. Hot allocation
     * path (bump within current free-line run) does NOT acquire this. */
    platform_mutex_t blocks_mutex;
} ThreadHeap;

void gc_heap_init(ThreadHeap *heap, BlockPool *pool) {
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
    MUTEX_INIT(heap->blocks_mutex);
}

/* Forward-declare thread-local (defined later in this file) */
JACL_THREAD_LOCAL ThreadHeap *gc__current_heap;

void gc_heap_destroy(ThreadHeap *heap) {
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
    MUTEX_DESTROY(heap->blocks_mutex);
    /* Clear thread-local if it points to this heap, to prevent use-after-free */
    if (gc__current_heap == heap) {
        gc__current_heap = NULL;
    }
}

/* --- Emergency GC callback (set by vm.c or runtime.c) --- */

JACL_THREAD_LOCAL void (*gc__emergency_gc_fn)(void *ctx) = NULL;
JACL_THREAD_LOCAL void *gc__emergency_gc_ctx = NULL;

/* --- OOM panic handler --- */

void gc__oom_panic_default(ThreadHeap *heap, size_t request_size) {
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
void (*gc__oom_handler)(ThreadHeap *, size_t) = gc__oom_panic_default;

/* --- Internal helpers --- */

/* Mark all lines touched by an allocation at [offset, offset+size) */
void gc__mark_lines(GCBlock *block, size_t offset, size_t size) {
    int first_line = (int)(offset / GC_LINE_SIZE);
    int last_line  = (int)((offset + size - 1) / GC_LINE_SIZE);
    int i;
    for (i = first_line; i <= last_line; i++) {
        block->line_map[i] = GC_LINE_OCCUPIED;
    }
}

/* Find next run of free lines starting from line index `from`.
 * Returns true if found, writing start index and length to out params. */
bool gc__find_free_run(GCBlock *block, int from,
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
bool gc__find_fit_in_block(ThreadHeap *heap, GCBlock *block,
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
void *gc__bump_alloc(ThreadHeap *heap, size_t total, uint8_t obj_type) {
    uint8_t  *ptr = heap->cursor;
    GCHeader *hdr;

    heap->cursor += total;
    /* bytes_since_gc is read by the concurrent GC (gc__adjust_threshold) from
     * another thread. Tag accesses as relaxed atomic so TSAN can see the
     * synchronization. Owner is sole writer; relaxed RMW is sufficient
     * because precise values aren't required (this is a GC heuristic). */
    size_t bsg = ATOMIC_LOAD_EXPLICIT(&heap->bytes_since_gc, MEM_RELAXED);
    bsg += total;
    ATOMIC_STORE_EXPLICIT(&heap->bytes_since_gc, bsg, MEM_RELAXED);

    /* Flag GC if adaptive threshold exceeded (checked by VM at next safepoint).
     * Relaxed atomic load — threshold may be concurrently adjusted by GC. */
    size_t thresh = ATOMIC_LOAD_EXPLICIT(&heap->gc_threshold, MEM_RELAXED);
    if (bsg > thresh) {
        ATOMIC_STORE_EXPLICIT(&heap->needs_gc, true, MEM_RELAXED);
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

void *gc_alloc(ThreadHeap *heap, uint8_t obj_type, size_t payload_size) {
    size_t   total = sizeof(GCHeader) + payload_size;
    int      needed_lines;
    GCBlock *b;
    GCBlock *new_block;

    /* Align total to 8 bytes so next header stays aligned */
    total = (total + 7) & ~(size_t)7;

    if (total >= GC_BLOCK_SIZE) return NULL;

    /* Fast path: bump within current free-line run.
     * Does NOT touch heap->blocks, so no lock needed. */
    if (heap->cursor && heap->cursor + total <= heap->limit) {
        return gc__bump_alloc(heap, total, obj_type);
    }

    /* Slow path: scan blocks list. Lock to serialize with concurrent sweep
     * which may be unlinking empty blocks. The lock is held through
     * head-insert if we acquire a new block. The lock is released before
     * tier-1 emergency GC (which re-acquires it for its own work). */
    needed_lines = (int)((total + GC_LINE_SIZE - 1) / GC_LINE_SIZE);

    MUTEX_LOCK(heap->blocks_mutex);
    b = heap->blocks;
    while (b) {
        if (gc__find_fit_in_block(heap, b, needed_lines, 0)) {
            MUTEX_UNLOCK(heap->blocks_mutex);
            return gc__bump_alloc(heap, total, obj_type);
        }
        b = b->next;
    }
    MUTEX_UNLOCK(heap->blocks_mutex);

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
    MUTEX_LOCK(heap->blocks_mutex);
    b = heap->blocks;
    while (b) {
        if (gc__find_fit_in_block(heap, b, needed_lines, 0)) {
            MUTEX_UNLOCK(heap->blocks_mutex);
            return gc__bump_alloc(heap, total, obj_type);
        }
        b = b->next;
    }
    MUTEX_UNLOCK(heap->blocks_mutex);

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
    /* Head-insert under the blocks mutex so we don't race with sweep's
     * traversal or unlink. */
    MUTEX_LOCK(heap->blocks_mutex);
    new_block->next = heap->blocks;
    heap->blocks = new_block;
    heap->current_block = new_block;
    heap->cursor = new_block->payload;
    heap->limit  = new_block->payload + GC_BLOCK_SIZE;
    MUTEX_UNLOCK(heap->blocks_mutex);

    return gc__bump_alloc(heap, total, obj_type);
}

/* --- Struct heap type --- */

typedef struct {
    uint32_t type_idx;      /* index into StructTypeRegistry */
    uint32_t total_size;    /* byte size of data[] (from StructTypeDef->total_size) */
    uint8_t  data[];        /* C-ABI laid out field data (total_size bytes) */
} HeapRecord;

HeapRecord* jacl_as_heap_record_ptr(JaclVal v) {
    return (HeapRecord*)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

JaclVal jacl_heap_record_val(HeapRecord* s) {
    return JACL_TAG_STRUCT | ((uint64_t)(uintptr_t)s & JACL_PAYLOAD_MASK);
}

/* ======================================================================
 * Heap-allocating value constructors (moved from value.c — need ThreadHeap)
 * ====================================================================== */

JaclVal jacl_i64(ThreadHeap *heap, int64_t n) {
    JaclHeapI64 *h = (JaclHeapI64 *)gc_alloc(heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    h->value = n;
    return JACL_TAG_I64 | ((uint64_t)(uintptr_t)h & JACL_PAYLOAD_MASK);
}

JaclVal jacl_u64(ThreadHeap *heap, uint64_t n) {
    JaclHeapU64 *h = (JaclHeapU64 *)gc_alloc(heap, OBJ_HEAP_U64, sizeof(JaclHeapU64));
    h->value = n;
    return JACL_TAG_U64 | ((uint64_t)(uintptr_t)h & JACL_PAYLOAD_MASK);
}

JaclVal jacl_f64(ThreadHeap *heap, double d) {
    JaclHeapF64 *h = (JaclHeapF64 *)gc_alloc(heap, OBJ_HEAP_F64, sizeof(JaclHeapF64));
    h->value = d;
    return JACL_TAG_F64 | ((uint64_t)(uintptr_t)h & JACL_PAYLOAD_MASK);
}

/* --- i64 arithmetic --- */

JaclVal jacl_i64_add(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i64(heap, jacl_as_i64(a) + jacl_as_i64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_i64_sub(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i64(heap, jacl_as_i64(a) - jacl_as_i64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_i64_mul(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_i64(heap, jacl_as_i64(a) * jacl_as_i64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_i64_div(ThreadHeap *heap, JaclVal a, JaclVal b) {
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

JaclVal jacl_i64_mod(ThreadHeap *heap, JaclVal a, JaclVal b) {
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

JaclVal jacl_i64_neg(ThreadHeap *heap, JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    JaclVal result = jacl_i64(heap, -jacl_as_i64(a));
    return jacl_apply_flags(result, flags);
}

/* --- u64 arithmetic --- */

JaclVal jacl_u64_add(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) + jacl_as_u64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_u64_sub(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) - jacl_as_u64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_u64_mul(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_u64(heap, jacl_as_u64(a) * jacl_as_u64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_u64_div(ThreadHeap *heap, JaclVal a, JaclVal b) {
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

JaclVal jacl_u64_mod(ThreadHeap *heap, JaclVal a, JaclVal b) {
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

JaclVal jacl_u64_neg(ThreadHeap *heap, JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    return jacl_apply_flags(jacl_set_error(jacl_u64(heap, 0)), flags);
}

/* --- f64 arithmetic --- */

JaclVal jacl_f64_add(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f64(heap, jacl_as_f64(a) + jacl_as_f64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_f64_sub(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f64(heap, jacl_as_f64(a) - jacl_as_f64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_f64_mul(ThreadHeap *heap, JaclVal a, JaclVal b) {
    if (jacl_is_error(a)) return a;
    if (jacl_is_error(b)) return b;
    uint64_t flags = jacl_propagate_flags(a, b);
    JaclVal result = jacl_f64(heap, jacl_as_f64(a) * jacl_as_f64(b));
    return jacl_apply_flags(result, flags);
}

JaclVal jacl_f64_div(ThreadHeap *heap, JaclVal a, JaclVal b) {
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

JaclVal jacl_f64_mod(ThreadHeap *heap, JaclVal a, JaclVal b) {
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

JaclVal jacl_f64_neg(ThreadHeap *heap, JaclVal a) {
    if (jacl_is_error(a)) return a;
    uint64_t flags = a & JACL_FLAGS_MASK;
    JaclVal result = jacl_f64(heap, -jacl_as_f64(a));
    return jacl_apply_flags(result, flags);
}

/* --- Thread-local heap pointer for collection templates (HAMT, RRB) ---
 * Set this before calling any collection mutation (set, unset, etc.)
 * so the template allocator can reach the current thread's GC heap.
 * Each worker thread sets its own copy in the task execution loop. */

JACL_THREAD_LOCAL ThreadHeap *gc__current_heap = NULL;

/* --- Global struct registry pointer for GC tracing ---
 * Type is void* because StructTypeRegistry is defined in compiler.c,
 * which is included after gc.c in the unity build. Cast to
 * StructTypeRegistry* in gc_collect.c (included after compiler.c). */
void *gc__struct_registry = NULL;

/* ======================================================================
 * GreyBuffer: per-thread append-only buffer for write barrier entries.
 *
 * Writer (owner) and reader (GC drain) are serialized by `gb->mutex`.
 * Push is rare in practice — only fires during gc_active && on mutable-
 * container mutation && when the stored value is a heap type — so mutex
 * contention is bounded.
 *
 * Earlier design (lock-free, release-store on count) had an unsoundness:
 * grey_buf_push's realloc could free the old entries buffer while the
 * GC reader was iterating it. See AUDIT.md §3. The mutex serializes
 * realloc with the read loop, eliminating the UAF.
 * ====================================================================== */

#define GREY_BUF_INIT_CAP 256

typedef struct {
    JaclVal         *entries;
    uint32_t         count;
    uint32_t         cap;
    platform_mutex_t mutex;
} GreyBuffer;

void grey_buf_init(GreyBuffer *gb) {
    gb->entries = (JaclVal *)malloc(GREY_BUF_INIT_CAP * sizeof(JaclVal));
    gb->count   = 0;
    gb->cap     = GREY_BUF_INIT_CAP;
    MUTEX_INIT(gb->mutex);
}

/* grey_buf_push — append v under the buffer's mutex.
 *
 * The mutex serializes us with gc__drain_grey_bufs, so the GC reader never
 * observes a half-realloc'd entries pointer or a freed old buffer. */
void grey_buf_push(GreyBuffer *gb, JaclVal v) {
    MUTEX_LOCK(gb->mutex);
    uint32_t c = gb->count;
    if (c >= gb->cap) {
        uint32_t new_cap = gb->cap * 2;
        gb->entries = (JaclVal *)realloc(gb->entries,
                                          (size_t)new_cap * sizeof(JaclVal));
        gb->cap = new_cap;
    }
    gb->entries[c] = v;
    /* Release on count: preserved for any readers that may legitimately use
     * the count outside the mutex (e.g. an empty-check fast path). */
    ATOMIC_STORE_EXPLICIT(&gb->count, c + 1, MEM_RELEASE);
    MUTEX_UNLOCK(gb->mutex);
}

void grey_buf_destroy(GreyBuffer *gb) {
    free(gb->entries);
    gb->entries = NULL;
    gb->count   = 0;
    gb->cap     = 0;
    MUTEX_DESTROY(gb->mutex);
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

void remembered_set_init(RememberedSet *rs) {
    rs->entries = (JaclVal *)malloc(REMEMBERED_SET_INIT_CAP * sizeof(JaclVal));
    rs->count   = 0;
    rs->cap     = REMEMBERED_SET_INIT_CAP;
}

void remembered_set_push(RememberedSet *rs, JaclVal container) {
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
uint32_t remembered_set_drain(RememberedSet *rs,
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

void remembered_set_destroy(RememberedSet *rs) {
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

void gc_write_barrier(GreyBuffer *gb,
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

void gc_remembered_set_barrier(RememberedSet *rs,
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

JaclVal jacl_future_ptr(JaclFuture *p) {
    return JACL_TAG_FUTURE | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

JaclFuture *jacl_as_future(JaclVal v) {
    return (JaclFuture *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* --- Spinlock for futures: CAS-based, no OS resource to leak on GC --- */

void future_lock(JaclFuture *f) {
    uint32_t expected = 0;
    while (!ATOMIC_CAS(&f->lock, &expected, 1, MEM_ACQUIRE, MEM_RELAXED))
        expected = 0;
}

void future_unlock(JaclFuture *f) {
    ATOMIC_STORE_EXPLICIT(&f->lock, 0, MEM_RELEASE);
}

/* --- Constructor: create a pending future --- */

JaclVal jacl_future(ThreadHeap *heap) {
    JaclFuture *f = (JaclFuture *)gc_alloc(heap, OBJ_FUTURE, sizeof(JaclFuture));
    f->state   = FUTURE_PENDING;
    f->result  = (uint64_t)JACL_NIL;
    f->waiters = NULL;
    f->lock = 0;
    return jacl_future_ptr(f);
}

/* --- Resolve: set result + RESOLVED state, return waiter list.
 *     Fires write barrier (old=NIL, new=result) for concurrent GC. --- */

FutureWaiter *jacl_future_resolve(JaclFuture *f, JaclVal result,
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

FutureWaiter *jacl_future_error(JaclFuture *f, JaclVal error,
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

bool jacl_future_add_waiter(JaclFuture *f, JaclVal continuation,
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
#define STREAM_KIND_EXEC       5
#define STREAM_KIND_EXEC_BUFFER 6
#define STREAM_KIND_EXEC_PIPE  7
#define STREAM_KIND_RANGE      8

#define STREAM_MAX_ARGS          8

typedef struct {
    uint32_t  state;         /* STREAM_PENDING / CONSUMED / EXHAUSTED / ERROR */
    uint32_t  kind;          /* STREAM_KIND_GENERATOR / FILTER */
    JaclVal   next_fn;       /* CPS continuation closure (nil when exhausted/error) */
    JaclVal   cached_value;  /* cached current element */
    JaclVal   state_machine; /* SM generator: state machine object (nil for CPS) */
    /* Deferred first-call arguments (saved at generator creation, used on first pull) */
    JaclVal   args[STREAM_MAX_ARGS];
    uint8_t   arg_count;
} JaclStream;

JaclStream *jacl_as_stream(JaclVal v) {
    return (JaclStream *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

JaclVal jacl_stream(ThreadHeap *heap) {
    JaclStream *s = (JaclStream *)gc_alloc(heap, OBJ_STREAM, sizeof(JaclStream));
    s->state         = STREAM_PENDING;
    s->kind          = STREAM_KIND_GENERATOR;
    s->next_fn       = JACL_NIL;
    s->cached_value  = JACL_NIL;
    s->state_machine = JACL_NIL;
    s->arg_count     = 0;
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
    JaclVal           state_machine; /* state machine object for join resumption */
    JaclVal           results[];     /* trailing array of N result slots */
} ParallelAgg;

/* Tag/untag helpers — reuses JACL_TAG_FUTURE for pointer tagging since
 * ParallelAgg is an internal GC object (OBJ_PARALLEL_AGG obj_type in
 * GCHeader ensures correct tracing). Never exposed to user code. */

JaclVal parallel_agg_ptr(ParallelAgg *p) {
    return JACL_TAG_FUTURE | ((uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK);
}

ParallelAgg *as_parallel_agg(JaclVal v) {
    return (ParallelAgg *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* Constructor: allocate a pending parallel aggregate with N result slots */
JaclVal jacl_parallel_agg(ThreadHeap *heap, uint32_t count,
                                  JaclVal state_machine) {
    size_t sz = sizeof(ParallelAgg) + count * sizeof(JaclVal);
    ParallelAgg *agg = (ParallelAgg *)gc_alloc(heap, OBJ_PARALLEL_AGG, sz);
    agg->completed     = 0;
    agg->errored       = 0;
    agg->error_val     = (uint64_t)JACL_NIL;
    agg->count         = count;
    agg->state_machine = state_machine;
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
    JaclVal           state_machine; /* state machine object for join resumption */
} RaceAgg;

/* Tag/untag helpers — same tagging scheme as ParallelAgg */

JaclVal race_agg_ptr(RaceAgg *r) {
    return JACL_TAG_FUTURE | ((uint64_t)(uintptr_t)r & JACL_PAYLOAD_MASK);
}

RaceAgg *as_race_agg(JaclVal v) {
    return (RaceAgg *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* Constructor: allocate a pending race aggregate */
JaclVal jacl_race_agg(ThreadHeap *heap, JaclVal state_machine) {
    RaceAgg *agg = (RaceAgg *)gc_alloc(heap, OBJ_RACE_AGG, sizeof(RaceAgg));
    agg->settled       = 0;
    agg->state_machine = state_machine;
    return race_agg_ptr(agg);
}

/* ======================================================================
 * JaclStateMachine: heap-allocated state for state machine coroutines
 *
 * Holds the resume point, error handler, reference to the compiled SM
 * closure, and a trailing array of local/parameter fields that persist
 * across suspension points.
 * ====================================================================== */

typedef struct {
    uint32_t resume_point;   /* next dispatch index (0 = initial entry) */
    uint32_t field_count;    /* number of trailing field slots */
    JaclVal  error_k;        /* completion callback (closure or JACL_NIL) */
    JaclVal  sm_closure;     /* compiled state machine closure */
    uint8_t  field_inline_bitmap[32]; /* US-014: marks which field slots hold raw struct bytes */
    JaclVal  fields[];       /* trailing array: fields[0..field_count-1] */
} JaclStateMachine;

/* --- Pointer accessor --- */

JaclStateMachine *jacl_as_state_machine(JaclVal v) {
    return (JaclStateMachine *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* --- Constructor: allocate a state machine with field_count trailing slots --- */

JaclVal gc_alloc_state_machine(ThreadHeap *heap, uint32_t field_count) {
    size_t sz = sizeof(JaclStateMachine) + field_count * sizeof(JaclVal);
    JaclStateMachine *sm = (JaclStateMachine *)gc_alloc(heap, OBJ_STATE_MACHINE, sz);
    sm->resume_point = 0;
    sm->field_count  = field_count;
    sm->error_k      = JACL_NIL;
    sm->sm_closure   = JACL_NIL;
    memset(sm->field_inline_bitmap, 0, sizeof(sm->field_inline_bitmap));
    for (uint32_t i = 0; i < field_count; i++) {
        sm->fields[i] = JACL_NIL;
    }
    return jacl_state_machine_ptr(sm);
}

/* ======================================================================
 * JaclSyntax: GC-allocated syntax objects for macro expansion
 *
 * Represents AST nodes as manipulable values during compile-time macro
 * expansion. Each syntax object carries its node kind, source position,
 * a scope mark for hygienic naming, and per-kind data in a tagged union.
 * ====================================================================== */

typedef enum {
    SYNTAX_COMMAND,
    SYNTAX_LIT_INT,
    SYNTAX_LIT_FLOAT,
    SYNTAX_LIT_STRING,
    SYNTAX_VAR_REF,
    SYNTAX_BLOCK,
    SYNTAX_INTERP_STRING,
    SYNTAX_SPREAD,
    SYNTAX_USE,
    SYNTAX_DEFSTRUCT,
    SYNTAX_DEFMACRO,
    SYNTAX_QUOTE,
    SYNTAX_SYNTAX_QUOTE,
    SYNTAX_UNQUOTE,
    SYNTAX_UNQUOTE_SPLICING,
    SYNTAX_BREAK,
    SYNTAX_CONTINUE,
    SYNTAX_RETURN,
    SYNTAX_DESTRUCTURE_VEC,
    SYNTAX_DESTRUCTURE_NAMED
} SyntaxKind;

typedef struct {
    uint8_t   kind;        /* SyntaxKind */
    uint8_t   is_caret;    /* US-013: ^name — skip scope-mark stamping */
    uint8_t   is_gensym;   /* US-014: var-ref produced by gensym */
    uint32_t  pos_line;    /* source line */
    uint32_t  pos_col;     /* source column */
    uint32_t  pos_offset;  /* source offset */
    uint32_t  scope_mark;  /* hygiene: 0 = no macro context */
    union {
        struct { JaclVal head; JaclVal args; }  command;       /* head: syntax, args: vec of syntax */
        struct { int32_t value; }               lit_int;
        struct { float   value; }               lit_float;
        struct { JaclVal value; }               lit_string;    /* JaclVal string */
        struct { JaclVal name; }                var_ref;       /* JaclVal string */
        struct { JaclVal commands; }            block;         /* vec of syntax */
        struct { JaclVal segments; }            interp_string; /* vec of syntax */
        struct { JaclVal child; }               spread;        /* syntax */
        struct { JaclVal child; }               use_decl;      /* syntax (simplified) */
        struct { JaclVal child; }               defstruct;     /* syntax (simplified) */
        struct { JaclVal child; }               defmacro;      /* syntax (simplified) */
        struct { JaclVal child; }               quote;         /* syntax (quoted child) */
        struct { JaclVal child; }               syntax_quote;  /* syntax (template child) */
        struct { JaclVal child; }               unquote;       /* syntax (unquoted expr) */
        struct { JaclVal child; }               unquote_splicing; /* syntax (spliced expr) */
        struct { JaclVal value; }               break_stmt;    /* syntax or nil */
        struct { JaclVal value; }               return_stmt;   /* syntax or nil */
        struct { JaclVal names; }               destructure_vec;   /* vec of strings */
        struct { JaclVal names; }               destructure_named; /* vec of strings */
    } data;
} JaclSyntax;

/* --- Pointer accessor --- */

JaclSyntax *jacl_as_syntax(JaclVal v) {
    return (JaclSyntax *)(uintptr_t)(v & JACL_PAYLOAD_MASK);
}

/* --- Constructor: allocate a zeroed syntax object --- */

JaclVal gc_alloc_syntax(ThreadHeap *heap) {
    JaclSyntax *syn = (JaclSyntax *)gc_alloc(heap, OBJ_SYNTAX, sizeof(JaclSyntax));
    memset(syn, 0, sizeof(JaclSyntax));
    return jacl_syntax_ptr(syn);
}

#endif /* GC_C */
