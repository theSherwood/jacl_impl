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
    OBJ_TYPED_HAMT_LEAF,
    OBJ_ATOM_REF,
    OBJ_WATCHER_LIST,
    OBJ_BOX_INLINE,         /* compact untyped box: payload is one JaclVal */
    OBJ_ARR                 /* mutable [Arr T]: JaclArr header (segment_array
                             * inline) + malloc'd segments freed at finalize.
                             * See ARR_DESIGN.md. */
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
    /* Single-bitmask test — see JACL_HEAP_TAG_MASK in jacl.h. The previous
     * 18-way `||` chain dominated gc_remembered_set_barrier in box_churn
     * profiles since every immediate-int new_val walked the full chain. */
    uint64_t tag_idx = (v & JACL_TYPE_MASK) >> JACL_TAG_SHIFT;
    return ((JACL_HEAP_TAG_MASK >> tag_idx) & 1u) != 0;
}

/* --- Box layout dispatch ---
 *
 * Untyped boxes (the `[box x]` form) live in the compact OBJ_BOX_INLINE
 * layout: payload is the single JaclVal directly, no JaclMutableRef header
 * fields — 16 bytes total per box (GCHeader + JaclVal) vs 24 for the
 * full OBJ_MUTABLE_REF layout. Struct-typed boxes (OP_BOX_STRUCT) still
 * use OBJ_MUTABLE_REF since they need type_idx + total_size + raw data.
 *
 * Callers that touch box payloads must dispatch on obj_type via the
 * helpers below. Cells and atoms always use OBJ_MUTABLE_REF /
 * OBJ_ATOM_REF respectively — only TAG_BOX values can carry the inline
 * layout. */

static inline bool jacl_box_is_typed(void* payload) {
    GCHeader* h = gc_header_of(payload);
    if (h->obj_type == OBJ_BOX_INLINE) return false;
    return ((JaclMutableRef*)payload)->type_idx > 0;
}

static inline JaclVal* jacl_box_untyped_val(void* payload) {
    GCHeader* h = gc_header_of(payload);
    if (h->obj_type == OBJ_BOX_INLINE) {
        return (JaclVal*)payload;
    }
    return (JaclVal*)((JaclMutableRef*)payload)->data;
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
    uint16_t        first_free_line;               /* §14 tier-2 scan hint */
    /* §14 Phase-B: largest contiguous FREE run in line_map, in lines.
     * Written authoritatively by sweep; updated conservatively by
     * gc__find_fit_in_block on a slow-path fit (to the largest non-
     * chosen run seen during the scan). The bump-alloc fast path
     * doesn't touch it. Lets gc_alloc's outer block walk O(1)-reject
     * blocks with insufficient room before entering the line scan.
     * Conservative upper bound: may overestimate between sweeps (since
     * bump-alloc shrinks the chosen run lazily) but never underestimate
     * the actual largest run, so a "false admit" only costs one line
     * scan that fails to find a fit. */
    uint16_t        max_free_run_lines;
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
    block->first_free_line = 0;
    block->max_free_run_lines = GC_LINES_PER_BLOCK; /* whole block FREE */
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
    /* §14 slow-path resume anchor — see jacl.h for the rationale. Cleared
     * by sweep at end of each GC cycle. */
    GCBlock         *search_block;
    /* Cumulative perf counters. Sole writer is the heap's owning worker;
     * read across workers by jacl_perf_snapshot post-quiesce. MUST stay in
     * sync with the definition in jacl.h. */
    uint64_t         total_bytes_allocated;
    uint64_t         total_allocs;
    uint64_t         slow_path_allocs;
    /* §14 instrumentation. Pre-Phase-B: blocks_scanned counted every block
     * visited by the outer walk. Post-Phase-B: blocks_scanned counts only
     * blocks that PASSED the max_free_run_lines reject (i.e. entered the
     * in-block line scan); blocks_rejected_fast counts the cheap O(1)
     * rejects. blocks_scanned + blocks_rejected_fast = total visited.
     * lines_scanned tallies line_map bytes inspected inside the inner
     * scan (which now only runs on admitted blocks). */
    uint64_t         blocks_scanned;
    uint64_t         lines_scanned;
    uint64_t         blocks_rejected_fast;
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
    heap->search_block = NULL;
    heap->total_bytes_allocated = 0;
    heap->total_allocs          = 0;
    heap->slow_path_allocs      = 0;
    heap->blocks_scanned        = 0;
    heap->lines_scanned         = 0;
    heap->blocks_rejected_fast  = 0;
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
    heap->search_block = NULL;
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
 * On success, sets heap cursor/limit/current_block and returns true.
 *
 * §14 tier-2: scan starts at max(from, block->first_free_line) — lines
 * [0, first_free_line) are known OCCUPIED. On success, advance
 * first_free_line to run_start (we just walked past any OCCUPIED prefix
 * from the previous hint to run_start, so [0, run_start) is OCCUPIED).
 * The whole chosen run [run_start, run_start+run_len) will be consumed
 * by bump-alloc before the next slow-path call (cursor exhaustion is
 * what triggers slow-path), so any leftover free space here is
 * transient and the next slow-path lookup will see the run as fully
 * marked. Caller (gc_alloc) holds heap->blocks_mutex. */
bool gc__find_fit_in_block(ThreadHeap *heap, GCBlock *block,
                                  int needed_lines, int from) {
    int run_start, run_len;
    int hint = (int)block->first_free_line;
    int scan = from > hint ? from : hint;
    int scan_origin = scan;
    int max_run_seen = 0;     /* §14 Phase-B: tightens bound on miss */
    bool found = false;
    /* §14 Phase-A: every call corresponds to one block visited by the
     * slow-path block-walk. Single relaxed RMW. */
    {
        uint64_t v = ATOMIC_LOAD_EXPLICIT(&heap->blocks_scanned, MEM_RELAXED);
        ATOMIC_STORE_EXPLICIT(&heap->blocks_scanned, v + 1, MEM_RELAXED);
    }
    while (gc__find_free_run(block, scan, &run_start, &run_len)) {
        if (run_len > max_run_seen) max_run_seen = run_len;
        if (run_len >= needed_lines) {
            heap->current_block = block;
            heap->cursor = block->payload
                         + ((size_t)run_start * GC_LINE_SIZE);
            heap->limit  = block->payload
                         + ((size_t)(run_start + run_len) * GC_LINE_SIZE);
            if (run_start > hint) {
                block->first_free_line = (uint16_t)run_start;
            }
            found = true;
            scan = run_start + run_len;
            break;
        }
        scan = run_start + run_len;
    }
    /* §14 Phase-B: on miss we walked every run from hint to end-of-block,
     * so max_run_seen is the authoritative max free run. Tighten the
     * cached bound. On hit we stopped early at the chosen run; we don't
     * know about runs after it, so leave the bound unchanged (the
     * chosen run is about to be consumed by bump-alloc, and any larger
     * remaining run would still be admitted by a higher cached value). */
    if (!found) {
        block->max_free_run_lines = (uint16_t)max_run_seen;
    }
    /* §14 Phase-A: tally lines actually inspected (from hint forward, up
     * to either the chosen run's end or end-of-block on miss). Single
     * RMW at exit keeps the hot loop tight. */
    {
        int reached = found ? scan : GC_LINES_PER_BLOCK;
        int walked  = reached - scan_origin;
        if (walked > 0) {
            uint64_t v = ATOMIC_LOAD_EXPLICIT(&heap->lines_scanned, MEM_RELAXED);
            ATOMIC_STORE_EXPLICIT(&heap->lines_scanned,
                                  v + (uint64_t)walked, MEM_RELAXED);
        }
    }
    return found;
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

    /* Cumulative perf counters — relaxed RMW. Same single-writer pattern. */
    {
        uint64_t a = ATOMIC_LOAD_EXPLICIT(&heap->total_bytes_allocated, MEM_RELAXED);
        ATOMIC_STORE_EXPLICIT(&heap->total_bytes_allocated, a + total, MEM_RELAXED);
        uint64_t n = ATOMIC_LOAD_EXPLICIT(&heap->total_allocs, MEM_RELAXED);
        ATOMIC_STORE_EXPLICIT(&heap->total_allocs, n + 1, MEM_RELAXED);
    }

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

    /* Falling through to the slow path. Count it; this is the §14 hot-path
     * proxy. Single-writer relaxed RMW, same pattern as bump_alloc. */
    {
        uint64_t s = ATOMIC_LOAD_EXPLICIT(&heap->slow_path_allocs, MEM_RELAXED);
        ATOMIC_STORE_EXPLICIT(&heap->slow_path_allocs, s + 1, MEM_RELAXED);
    }

    /* Slow path: scan blocks list. Lock to serialize with concurrent sweep
     * which may be unlinking empty blocks. The lock is held through
     * head-insert if we acquire a new block. The lock is released before
     * tier-1 emergency GC (which re-acquires it for its own work).
     *
     * §14: resume the block walk from heap->search_block (set on the
     * previous successful fit). Lines within each block are still scanned
     * from 0 so smaller free runs left over from a larger find-fit call
     * remain reachable for subsequent smaller allocations. Sweep clears
     * search_block at end of each GC cycle so freed runs near the list
     * head are picked up on the next pass. */
    needed_lines = (int)((total + GC_LINE_SIZE - 1) / GC_LINE_SIZE);

    MUTEX_LOCK(heap->blocks_mutex);
    b = heap->search_block ? heap->search_block : heap->blocks;
    while (b) {
        /* §14 Phase-B: O(1) reject for blocks that can't possibly fit.
         * max_free_run_lines is a conservative upper bound between
         * sweeps, so admitting a block here that ultimately doesn't fit
         * costs one find_fit_in_block scan (which then tightens the
         * bound to the authoritative max). */
        if ((int)b->max_free_run_lines < needed_lines) {
            uint64_t r = ATOMIC_LOAD_EXPLICIT(&heap->blocks_rejected_fast, MEM_RELAXED);
            ATOMIC_STORE_EXPLICIT(&heap->blocks_rejected_fast, r + 1, MEM_RELAXED);
        } else if (gc__find_fit_in_block(heap, b, needed_lines, 0)) {
            heap->search_block = b;
            MUTEX_UNLOCK(heap->blocks_mutex);
            return gc__bump_alloc(heap, total, obj_type);
        }
        b = b->next;
    }
    /* No fit found through end of list — reset anchor so the next
     * allocation (after pool_get below) starts fresh at heap->blocks. */
    heap->search_block = NULL;
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

    /* After emergency GC: rescan existing blocks (GC may have freed runs).
     * Sweep cleared search_block, so this naturally restarts at the head. */
    MUTEX_LOCK(heap->blocks_mutex);
    b = heap->search_block ? heap->search_block : heap->blocks;
    while (b) {
        if ((int)b->max_free_run_lines < needed_lines) {
            uint64_t r = ATOMIC_LOAD_EXPLICIT(&heap->blocks_rejected_fast, MEM_RELAXED);
            ATOMIC_STORE_EXPLICIT(&heap->blocks_rejected_fast, r + 1, MEM_RELAXED);
        } else if (gc__find_fit_in_block(heap, b, needed_lines, 0)) {
            heap->search_block = b;
            MUTEX_UNLOCK(heap->blocks_mutex);
            return gc__bump_alloc(heap, total, obj_type);
        }
        b = b->next;
    }
    heap->search_block = NULL;
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
                new_block->first_free_line = 0;
                new_block->max_free_run_lines = GC_LINES_PER_BLOCK;
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
    return JACL_PACK_PTR(JACL_TAG_STRUCT, s);
}

/* ======================================================================
 * Heap-allocating value constructors (moved from value.c — need ThreadHeap)
 * ====================================================================== */

JaclVal jacl_i64(ThreadHeap *heap, int64_t n) {
    JaclHeapI64 *h = (JaclHeapI64 *)gc_alloc(heap, OBJ_HEAP_I64, sizeof(JaclHeapI64));
    h->value = n;
    return JACL_PACK_PTR(JACL_TAG_I64, h);
}

JaclVal jacl_u64(ThreadHeap *heap, uint64_t n) {
    JaclHeapU64 *h = (JaclHeapU64 *)gc_alloc(heap, OBJ_HEAP_U64, sizeof(JaclHeapU64));
    h->value = n;
    return JACL_PACK_PTR(JACL_TAG_U64, h);
}

JaclVal jacl_f64(ThreadHeap *heap, double d) {
    JaclHeapF64 *h = (JaclHeapF64 *)gc_alloc(heap, OBJ_HEAP_F64, sizeof(JaclHeapF64));
    h->value = d;
    return JACL_PACK_PTR(JACL_TAG_F64, h);
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
 * The deletion side of gc_write_barrier fires unconditionally on any
 * heap-valued container overwrite (see AUDIT.md §9 for why the
 * previous gc_active gate was unsound). Entries pushed between GC
 * cycles are conservative marks for the next cycle — at the end of
 * each cycle gb->count is reset to 0, so growth is bounded by the
 * mutation rate within one cycle.
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
 * Fires on any heap-pointer mutation: mutable container reset!/swap!/
 * set-cell!, but also state-machine field stores, closure upvalue
 * stores, stream field stores, future-waiter chain mutations, and any
 * other site that overwrites a heap-typed slot on a GC-managed object.
 *
 * BOTH SIDES UNCONDITIONAL.
 *
 * Deletion side (SATB) — closes AUDIT.md §9. If the gate gated this
 * push on gc_active, a worker that read V_old from the container onto
 * its operand stack and then ran into a concurrent mutation while
 * gc_active was still false would lose V_old's reachability — V_old's
 * only container ref is gone, the operand stack isn't a GC root in
 * concurrent mode, and if V_old's epoch is below the next watermark
 * the sweep would reclaim it under the worker.
 *
 * Insertion side — closes AUDIT.md §10/§11 race #2. The original
 * design gated the insertion push on gc_active, assuming "a new value
 * stored after GC scanned the container is already visible through
 * the container." That assumption holds only for containers that are
 * ALREADY MARKED (pre-existing, in some root path). For FRESH
 * containers — watermark-protected but never traced — insertions are
 * lost: the container survives sweep via the watermark check without
 * the mark phase ever visiting its slots, so a heap value inserted
 * into a fresh container has no protection unless the value is
 * independently rooted. Race #2 was the future-waiter case: a fresh
 * FutureWaiter w whose continuation pointed to an OLD state machine
 * — w survived via watermark, the SM had no other mark path, sweep
 * reclaimed it. Making the insertion push unconditional means every
 * heap value placed into any heap-managed slot is conservatively
 * marked for the next cycle, regardless of container freshness.
 *
 * The grey buffer is reset every GC cycle; entries pushed between
 * cycles cost one atomic store per heap-valued mutation and serve as
 * conservative marks for the next cycle. Bounded by mutation rate
 * within one cycle.
 *
 * Single-threaded mode (NULL gc_active_ptr) bypasses both: single
 * tracing GC scans the VM stack, so neither V_old nor V_new can be
 * lost there.
 * ====================================================================== */

void gc_write_barrier(GreyBuffer *gb,
                                     uint32_t *gc_active_ptr,
                                     JaclVal old_val, JaclVal new_val) {
    /* Single-threaded mode: no gc_active flag → no barrier needed */
    if (!gc_active_ptr) return;

    /* SATB deletion side — unconditional. */
    if (jacl_is_heap_type(old_val))
        grey_buf_push(gb, old_val);

    /* Insertion side — also unconditional. */
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
    uint32_t            state;       /* FUTURE_PENDING / RESOLVED / ERROR (atomic) */
    uint64_t            result;      /* JaclVal result, valid when settled (atomic) */
    FutureWaiter       *waiters;     /* linked list of waiting continuations */
    uint32_t            lock;        /* CAS spinlock: 0=unlocked, 1=locked */
} JaclFuture;

/* --- Pointer tag/untag for futures --- */

JaclVal jacl_future_ptr(JaclFuture *p) {
    return JACL_PACK_PTR(JACL_TAG_FUTURE, p);
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
 *     Fires write barrier (old=NIL, new=result) for concurrent GC.
 *
 *     The waiter chain is NOT detached from f->waiters — see AUDIT.md
 *     §9b/§10-11. Detaching would overwrite a heap pointer slot without
 *     a write barrier, and a concurrent GC tracing this future after
 *     the NULL store would miss the chain (and the awaiting SMs it
 *     transitively roots via OBJ_FUTURE_WAITER.continuation). The
 *     chain stays attached so OBJ_FUTURE tracing keeps it reachable
 *     while runtime__schedule_waiters iterates. After settle,
 *     jacl_future_add_waiter no longer mutates the chain (state check
 *     under future_lock), so the chain is bounded and write-once.
 *     It will be reclaimed naturally once the future itself becomes
 *     unreachable from any root. --- */

FutureWaiter *jacl_future_resolve(JaclFuture *f, JaclVal result,
                                          GreyBuffer *gb,
                                          uint32_t *gc_active_ptr) {
    FutureWaiter *waiters;
    gc_write_barrier(gb, gc_active_ptr, JACL_NIL, result);
    future_lock(f);
    f->result = (uint64_t)result;
    ATOMIC_STORE_EXPLICIT(&f->state, FUTURE_RESOLVED, MEM_RELEASE);
    /* Atomic load: f->waiters is also read by gc__trace_object without
     * future_lock; once any access to the field is atomic, all of them
     * must be (otherwise mixed atomic/plain is UB per the C memory model). */
    waiters = ATOMIC_LOAD_EXPLICIT(&f->waiters, MEM_RELAXED);
    future_unlock(f);
    return waiters;
}

/* --- Error: set error result + ERROR state, return waiter list.
 *     Fires write barrier (old=NIL, new=error) for concurrent GC.
 *     Chain detach skipped for the same reason as jacl_future_resolve. --- */

FutureWaiter *jacl_future_error(JaclFuture *f, JaclVal error,
                                        GreyBuffer *gb,
                                        uint32_t *gc_active_ptr) {
    FutureWaiter *waiters;
    gc_write_barrier(gb, gc_active_ptr, JACL_NIL, error);
    future_lock(f);
    f->result = (uint64_t)error;
    ATOMIC_STORE_EXPLICIT(&f->state, FUTURE_ERROR, MEM_RELEASE);
    /* See jacl_future_resolve for why this load is atomic. */
    waiters = ATOMIC_LOAD_EXPLICIT(&f->waiters, MEM_RELAXED);
    future_unlock(f);
    return waiters;
}

/* --- Add waiter: if pending, prepend waiter and return true.
 *     If already resolved/errored, return false (caller schedules immediately). --- */

bool jacl_future_add_waiter(JaclFuture *f, JaclVal continuation,
                                    ThreadHeap *heap,
                                    GreyBuffer *gb,
                                    uint32_t *gc_active_ptr) {
    bool added = false;
    future_lock(f);
    if (ATOMIC_LOAD_EXPLICIT(&f->state, MEM_RELAXED) == FUTURE_PENDING) {
        FutureWaiter *w = (FutureWaiter *)gc_alloc(heap, OBJ_FUTURE_WAITER,
                                                     sizeof(FutureWaiter));
        w->continuation = continuation;
        /* Read+write f->waiters atomically: the head pointer is also read by
         * gc__trace_object outside future_lock during concurrent marking,
         * so all accesses must be atomic. w->next is read-only after this
         * point (waiter chain is write-once; see jacl_future_resolve), so
         * traversing it from the GC trace doesn't need atomic loads. */
        w->next = ATOMIC_LOAD_EXPLICIT(&f->waiters, MEM_RELAXED);
        ATOMIC_STORE_EXPLICIT(&f->waiters, w, MEM_RELAXED);
        /* AUDIT.md §10/§11 race #2: w is a freshly-allocated container,
         * watermark-protected without being traced. The insertion side
         * of gc_write_barrier (now unconditional) greyifies continuation
         * so the next mark phase keeps it alive. */
        gc_write_barrier(gb, gc_active_ptr, JACL_NIL, continuation);
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
    uint32_t          completed;     /* atomic completion counter */
    uint32_t          errored;       /* 0 or 1 (first-error-wins CAS) */
    uint64_t          error_val;     /* first error value (JaclVal, atomic) */
    uint32_t          count;         /* total N */
    JaclVal           state_machine; /* state machine object for join resumption */
    JaclVal           results[];     /* trailing array of N result slots */
} ParallelAgg;

/* Tag/untag helpers — reuses JACL_TAG_FUTURE for pointer tagging since
 * ParallelAgg is an internal GC object (OBJ_PARALLEL_AGG obj_type in
 * GCHeader ensures correct tracing). Never exposed to user code. */

JaclVal parallel_agg_ptr(ParallelAgg *p) {
    return JACL_PACK_PTR(JACL_TAG_FUTURE, p);
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
    uint32_t          settled;       /* 0 = not settled, 1 = winner determined (CAS) */
    JaclVal           state_machine; /* state machine object for join resumption */
} RaceAgg;

/* Tag/untag helpers — same tagging scheme as ParallelAgg */

JaclVal race_agg_ptr(RaceAgg *r) {
    return JACL_PACK_PTR(JACL_TAG_FUTURE, r);
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
