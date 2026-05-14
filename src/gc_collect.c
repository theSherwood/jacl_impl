/*
 * JACL Garbage Collector — Mark, sweep, and collection trigger.
 *
 * This file is included after vm.c in the unity build so it has access
 * to all type definitions: GCHeader/gc_alloc (gc.c), collection node
 * types (collections.c), and the VM struct (vm.c).
 */

#ifndef GC_COLLECT_C
#define GC_COLLECT_C

/* ======================================================================
 * Mark stack: fixed-size array with dynamic overflow for pathological cases
 * ====================================================================== */

#define GC_MARK_STACK_SIZE 4096

typedef struct {
    void  *fixed[GC_MARK_STACK_SIZE];
    int    top;          /* next free slot in fixed array */
    void **overflow;     /* dynamically growing overflow buffer */
    int    ov_count;
    int    ov_cap;
} GCMarkStack;

void gc__ms_init(GCMarkStack *ms) {
    ms->top      = 0;
    ms->overflow = NULL;
    ms->ov_count = 0;
    ms->ov_cap   = 0;
}

void gc__ms_push(GCMarkStack *ms, void *ptr) {
    if (ms->top < GC_MARK_STACK_SIZE) {
        ms->fixed[ms->top++] = ptr;
        return;
    }
    /* Overflow path */
    if (ms->ov_count >= ms->ov_cap) {
        int new_cap = ms->ov_cap == 0 ? 256 : ms->ov_cap * 2;
        void **new_buf = (void **)realloc(ms->overflow,
                                          (size_t)new_cap * sizeof(void *));
        ms->overflow = new_buf;
        ms->ov_cap   = new_cap;
    }
    ms->overflow[ms->ov_count++] = ptr;
}

bool gc__ms_pop(GCMarkStack *ms, void **out) {
    if (ms->top > 0) {
        *out = ms->fixed[--ms->top];
        return true;
    }
    if (ms->ov_count > 0) {
        *out = ms->overflow[--ms->ov_count];
        return true;
    }
    return false;
}

void gc__ms_destroy(GCMarkStack *ms) {
    free(ms->overflow);
    ms->overflow = NULL;
    ms->ov_count = 0;
    ms->ov_cap   = 0;
}

/* Push a JaclVal if it refers to a GC-managed heap object */
void gc__ms_push_val(GCMarkStack *ms, JaclVal v) {
    if (jacl_is_heap_type(v)) {
        gc__ms_push(ms, jacl_as_ptr(v));
    }
}

/* Push a constant pool entry — skip closures (arena-allocated templates) */
void gc__ms_push_const(GCMarkStack *ms, JaclVal v) {
    if (jacl_is_heap_type(v) && !jacl_is_closure(v)) {
        gc__ms_push(ms, jacl_as_ptr(v));
    }
}

/* ======================================================================
 * Object finalization: release external resources before sweep zeroes memory
 * ====================================================================== */

void gc__finalize_dead(GCHeader *hdr) {
    (void)hdr;
}

/* ======================================================================
 * Post-sweep block summary: single-pass scan of a line map, computes the
 * scan hint (first FREE line) and the all-free flag in one walk. Shared
 * by gc_sweep / gc_sweep_minor / gc_sweep_concurrent — they all do this
 * derivation identically after rebuilding the line map. Phase B will
 * extend the same loop with max_free_run_lines.
 * ====================================================================== */
typedef struct {
    int  first_free;   /* first FREE line index, GC_LINES_PER_BLOCK if none */
    bool all_free;     /* entire block is FREE */
} GCBlockSweepSummary;

static GCBlockSweepSummary gc__summarize_block_map(const uint8_t *line_map) {
    GCBlockSweepSummary s;
    s.first_free = GC_LINES_PER_BLOCK;
    s.all_free   = true;
    bool found_first = false;
    for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
        if (line_map[i] == GC_LINE_FREE) {
            if (!found_first) { s.first_free = i; found_first = true; }
        } else {
            s.all_free = false;
        }
    }
    return s;
}

/* ======================================================================
 * Object tracing: push an object's children onto the mark stack
 * ====================================================================== */

void gc__trace_object(void *payload, GCMarkStack *ms) {
    GCHeader *hdr = gc_header_of(payload);

    switch (hdr->obj_type) {

    /* --- Leaf types: no outgoing references --- */
    case OBJ_STRING:
    case OBJ_HEAP_I64:
    case OBJ_HEAP_U64:
    case OBJ_HEAP_F64:
    case OBJ_BIGNUM:
    case OBJ_ROPE_LEAF:
        break;

    /* --- Rope string: trace root node of the rope tree --- */
    case OBJ_ROPE_STRING: {
        JaclRopeString *rs = (JaclRopeString *)payload;
        if (rs->r.root.node) {
            gc__ms_push(ms, rs->r.root.node);
        }
        break;
    }

    /* --- Rope internal: trace children array --- */
    case OBJ_ROPE_INTERNAL: {
        rope_st_internal *node = (rope_st_internal *)payload;
        for (size_t i = 0; i < node->n_children; i++) {
            if (node->children[i]) {
                gc__ms_push(ms, node->children[i]);
            }
        }
        break;
    }

    /* --- Closure: trace captured upvalues + chunk constants --- */
    case OBJ_CLOSURE: {
        JaclClosure *cl = (JaclClosure *)payload;
        /* US-014: iterate upvalue_total_slots (not upvalue_count) to cover
           wide struct upvalues. Skip slots marked as raw struct bytes. */
        uint16_t uv_total = cl->upvalue_total_slots
            ? cl->upvalue_total_slots : cl->upvalue_count;
        if (cl->upvalues) {
            for (uint16_t i = 0; i < uv_total; i++) {
                if (!BITMAP_GET(cl->upvalue_inline_bitmap, i)) {
                    gc__ms_push_val(ms, cl->upvalues[i]);
                }
            }
        }
        /* Trace chunk constants to keep heap literals alive.
         * Skip closures (arena-allocated templates, not GC objects). */
        for (uint32_t i = 0; i < cl->chunk.const_count; i++) {
            gc__ms_push_const(ms, cl->chunk.constants[i]);
        }
        break;
    }

    /* --- Mutable ref (cell/box/atom): trace contained value --- */
    case OBJ_MUTABLE_REF: {
        JaclMutableRef *ref = (JaclMutableRef *)payload;
        /* Only trace for plain JaclVal boxes (type_idx==0).
           Struct boxes (type_idx>0) contain raw bytes — no GC references.
           ACQUIRE load: pairs with the RELEASE store atom mutations use.
           Without this pairing, the GC can read a fresh pointer but miss
           the writes the writer made before the release (e.g. the
           GCHeader fields of the newly-allocated value). */
        if (ref->type_idx == 0) {
            JaclVal v = (JaclVal)ATOMIC_LOAD_EXPLICIT(
                (volatile uint64_t *)&MREF_VAL(ref), MEM_ACQUIRE);
            gc__ms_push_val(ms, v);
        }
        break;
    }

    /* --- HAMT internal: trace children array --- */
    case OBJ_HAMT_INTERNAL: {
        jacl_map_internal *node = (jacl_map_internal *)payload;
        int count = get_popcount(node->bitmap);
        for (int i = 0; i < count; i++) {
            gc__ms_push(ms, node->children[i]);
        }
        break;
    }

    /* --- HAMT leaf: trace all key and value slots --- */
    case OBJ_HAMT_LEAF: {
        jacl_map_leaf *leaf = (jacl_map_leaf *)payload;
        uint32_t total = leaf->key_stride + leaf->val_stride;
        for (uint32_t i = 0; i < total; i++) {
            gc__ms_push_val(ms, leaf->slots[i]);
        }
        break;
    }

    /* --- HAMT collision: trace items array (leaf pointers) --- */
    case OBJ_HAMT_COLLISION: {
        jacl_map_collision *col = (jacl_map_collision *)payload;
        for (uint32_t i = 0; i < col->count; i++) {
            gc__ms_push(ms, col->items[i]);
        }
        break;
    }

    /* --- RRB internal: trace children array --- */
    case OBJ_RRB_INTERNAL: {
        jacl_vec_internal *node = (jacl_vec_internal *)payload;
        for (uint32_t i = 0; i < node->child_count; i++) {
            gc__ms_push(ms, node->children[i]);
        }
        break;
    }

    /* --- RRB leaf: trace element values --- */
    case OBJ_RRB_LEAF: {
        jacl_vec_leaf *leaf = (jacl_vec_leaf *)payload;
        uint32_t total_slots = leaf->count * leaf->stride;
        for (uint32_t i = 0; i < total_slots; i++) {
            gc__ms_push_val(ms, leaf->elements[i]);
        }
        break;
    }

    /* --- Typed RRB leaf: all slots are raw struct bytes, no GC tracing --- */
    case OBJ_TYPED_RRB_LEAF:
        break;

    /* --- Typed HAMT leaf: trace dyn keys only, skip struct bytes --- */
    case OBJ_TYPED_HAMT_LEAF: {
        jacl_typed_map_leaf *tleaf = (jacl_typed_map_leaf *)payload;
        /* key_stride == 1: single JaclVal key (dyn), trace it.
           key_stride > 1: multi-slot struct key (raw bytes), no tracing.
           val_stride slots are always raw struct bytes, never traced. */
        if (tleaf->key_stride == 1) {
            gc__ms_push_val(ms, tleaf->slots[0]);
        }
        /* key_stride > 1: all slots are raw struct bytes, skip. */
        break;
    }

    /* --- RRB root: trace tree root and tail --- */
    case OBJ_RRB_ROOT: {
        jacl_vec_root *root = (jacl_vec_root *)payload;
        if (root->root) gc__ms_push(ms, root->root);
        if (root->tail) gc__ms_push(ms, root->tail);
        break;
    }

    /* --- Future: trace result (if resolved/errored) and waiter list --- */
    case OBJ_FUTURE: {
        JaclFuture *fut = (JaclFuture *)payload;
        uint32_t state = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);
        if (state == FUTURE_RESOLVED || state == FUTURE_ERROR) {
            gc__ms_push_val(ms, (JaclVal)fut->result);
        }
        FutureWaiter *w = fut->waiters;
        while (w) {
            gc__ms_push(ms, w); /* trace the waiter node itself */
            w = w->next;
        }
        break;
    }

    /* --- FutureWaiter: trace continuation closure + next pointer --- */
    case OBJ_FUTURE_WAITER: {
        FutureWaiter *fw = (FutureWaiter *)payload;
        gc__ms_push_val(ms, fw->continuation);
        if (fw->next) {
            gc__ms_push(ms, fw->next);
        }
        break;
    }

    /* --- ParallelAgg: trace state machine, error value, and all result slots --- */
    case OBJ_PARALLEL_AGG: {
        ParallelAgg *agg = (ParallelAgg *)payload;
        gc__ms_push_val(ms, agg->state_machine);
        uint64_t ev = ATOMIC_LOAD_EXPLICIT(&agg->error_val, MEM_ACQUIRE);
        gc__ms_push_val(ms, (JaclVal)ev);
        for (uint32_t i = 0; i < agg->count; i++) {
            gc__ms_push_val(ms, agg->results[i]);
        }
        break;
    }

    case OBJ_RACE_AGG: {
        RaceAgg *agg = (RaceAgg *)payload;
        gc__ms_push_val(ms, agg->state_machine);
        break;
    }

    /* --- Stream: trace next_fn, cached_value, and deferred args --- */
    case OBJ_STREAM: {
        JaclStream *stream = (JaclStream *)payload;
        gc__ms_push_val(ms, stream->next_fn);
        gc__ms_push_val(ms, stream->cached_value);
        gc__ms_push_val(ms, stream->state_machine);
        for (uint8_t i = 0; i < stream->arg_count; i++) {
            gc__ms_push_val(ms, stream->args[i]);
        }
        break;
    }

    /* --- State machine: trace error_k, sm_closure, and non-struct field slots --- */
    case OBJ_STATE_MACHINE: {
        JaclStateMachine *sm = (JaclStateMachine *)payload;
        gc__ms_push_val(ms, sm->error_k);
        gc__ms_push_val(ms, sm->sm_closure);
        /* US-014: skip field slots that hold raw inline struct bytes */
        for (uint32_t i = 0; i < sm->field_count; i++) {
            if (!BITMAP_GET(sm->field_inline_bitmap, i)) {
                gc__ms_push_val(ms, sm->fields[i]);
            }
        }
        break;
    }

    /* --- Syntax object: trace JaclVal fields based on kind --- */
    case OBJ_SYNTAX: {
        JaclSyntax *syn = (JaclSyntax *)payload;
        switch (syn->kind) {
        case SYNTAX_COMMAND:
            gc__ms_push_val(ms, syn->data.command.head);
            gc__ms_push_val(ms, syn->data.command.args);
            break;
        case SYNTAX_LIT_STRING:
            gc__ms_push_val(ms, syn->data.lit_string.value);
            break;
        case SYNTAX_VAR_REF:
            gc__ms_push_val(ms, syn->data.var_ref.name);
            break;
        case SYNTAX_BLOCK:
            gc__ms_push_val(ms, syn->data.block.commands);
            break;
        case SYNTAX_INTERP_STRING:
            gc__ms_push_val(ms, syn->data.interp_string.segments);
            break;
        case SYNTAX_SPREAD:
            gc__ms_push_val(ms, syn->data.spread.child);
            break;
        case SYNTAX_USE:
            gc__ms_push_val(ms, syn->data.use_decl.child);
            break;
        case SYNTAX_DEFSTRUCT:
            gc__ms_push_val(ms, syn->data.defstruct.child);
            break;
        case SYNTAX_DEFMACRO:
            gc__ms_push_val(ms, syn->data.defmacro.child);
            break;
        case SYNTAX_QUOTE:
            gc__ms_push_val(ms, syn->data.quote.child);
            break;
        case SYNTAX_SYNTAX_QUOTE:
            gc__ms_push_val(ms, syn->data.syntax_quote.child);
            break;
        case SYNTAX_UNQUOTE:
            gc__ms_push_val(ms, syn->data.unquote.child);
            break;
        case SYNTAX_UNQUOTE_SPLICING:
            gc__ms_push_val(ms, syn->data.unquote_splicing.child);
            break;
        case SYNTAX_BREAK:
            gc__ms_push_val(ms, syn->data.break_stmt.value);
            break;
        case SYNTAX_RETURN:
            gc__ms_push_val(ms, syn->data.return_stmt.value);
            break;
        case SYNTAX_DESTRUCTURE_VEC:
            gc__ms_push_val(ms, syn->data.destructure_vec.names);
            break;
        case SYNTAX_DESTRUCTURE_NAMED:
            gc__ms_push_val(ms, syn->data.destructure_named.names);
            break;
        default:
            break; /* SYNTAX_LIT_INT, SYNTAX_LIT_FLOAT, SYNTAX_CONTINUE — no refs */
        }
        break;
    }

    /* --- HeapRecord: trace reference fields. The only HeapRecord type
       in user-reachable code is ctx (the lone builtin). Struct-typed
       fields store inline bytes (value-type only since ref fields are
       rejected in defstruct), so they never need GC tracing. --- */
    case OBJ_HEAP_RECORD: {
        HeapRecord *s = (HeapRecord *)payload;
        if (gc__struct_registry) {
            StructTypeRegistry *sreg = (StructTypeRegistry *)gc__struct_registry;
            StructTypeDef *sdef = sreg->defs[s->type_idx];
            for (uint32_t i = 0; i < sdef->field_count; i++) {
                JaclType ft = sdef->fields[i].type;
                if (ft == TYPE_STR || ft == TYPE_VEC || ft == TYPE_MAP ||
                    ft == TYPE_CLOSURE || ft == TYPE_DYN) {
                    /* ACQUIRE pairs with any release-store into a
                     * heap-record field (struct field assignments,
                     * etc.). Ensures the trace sees fully-published
                     * pointer values rather than torn writes.
                     * (Originally added for ctx_pool_free's NIL writes;
                     * the pool itself was removed in §18 but the
                     * pairing remains valid for other paths.) */
                    JaclVal val = (JaclVal)ATOMIC_LOAD_EXPLICIT(
                        (volatile uint64_t*)(s->data + sdef->fields[i].offset),
                        MEM_ACQUIRE);
                    gc__ms_push_val(ms, val);
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

/* Forward declaration — defined below; used by the closure-root push to
 * skip stack-allocated synthesized closures (no GCHeader). */
bool gc__ptr_in_heap(ThreadHeap *heap, void *ptr);

/* ======================================================================
 * gc_mark: trace from GC roots through the object graph, marking live objects
 * ====================================================================== */

void gc_mark(ThreadHeap *heap, VM *vm) {
    GCMarkStack ms;
    gc__ms_init(&ms);

    uint8_t mark = heap->current_mark;

    /* --- Root enumeration --- */

    /* 1. VM stack values — skip slots containing raw inline struct bytes */
    for (uint32_t i = 0; i < vm->stack_top; i++) {
        if (!BITMAP_GET(vm->inline_slot_bitmap, i)) {
            gc__ms_push_val(&ms, vm->stack[i]);
        }
    }

    /* 2. Call frame closures. Skip stack-allocated synthesized closures
     * (vm_exec's top_closure, embed.c's top_closure_wrapper) — they
     * have no GCHeader, so gc_header_of(cl) would read 4 bytes before
     * a stack variable. Their chunk constants are pushed below (step 5)
     * and they carry no upvalues, so skipping the closure itself is
     * safe — nothing reachable only through them is lost. */
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        JaclClosure *cl = vm->frames[i].closure;
        if (cl && gc__ptr_in_heap(heap, cl)) {
            gc__ms_push(&ms, cl);
        }
    }

    /* 3. Global environment values */
    for (uint32_t i = 0; i < vm->env.count; i++) {
        gc__ms_push_val(&ms, vm->env.values[i]);
    }

    /* 4. Intern table entries — treated as weak roots (not scanned).
     * Dead entries are evicted by gc_sweep_intern_table after mark phase. */

    /* 5. Call frame chunk constants (heap i64/u64/f64 literals) */
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        BytecodeChunk *ch = vm->frames[i].chunk;
        if (ch) {
            for (uint32_t j = 0; j < ch->const_count; j++) {
                gc__ms_push_const(&ms, ch->constants[j]);
            }
        }
    }

    /* 6. External GC handle slots (embedding API) */
    if (vm->gc_handle_slots) {
        for (uint32_t i = 0; i < vm->gc_handle_count; i++) {
            gc__ms_push_val(&ms, vm->gc_handle_slots[i]);
        }
    }

    /* 7. (formerly: walked ctx_pool->free_list_head). Pool removed
     * in AUDIT.md §18 — see runtime.c gc_enumerate_roots comment. */

    /* 8. Current ctx register (implicit context struct) */
    gc__ms_push_val(&ms, vm->ctx);

    /* 9. Saved ctx stack (with-ctx nesting) */
    for (uint8_t sci = 0; sci < vm->saved_ctx_count; sci++) {
        gc__ms_push_val(&ms, vm->saved_ctx[sci]);
    }

    /* --- Mark loop --- */
    void *ptr;
    while (gc__ms_pop(&ms, &ptr)) {
        GCHeader *hdr = gc_header_of(ptr);
        /* See gc_concurrent_collect for the alloc_total==0 guard. */
        if (hdr->alloc_total == 0) continue;
        if (hdr->mark == mark) continue; /* already marked this cycle */
        hdr->mark = mark;
        gc__trace_object(ptr, &ms);
    }

    gc__ms_destroy(&ms);
}

/* ======================================================================
 * gc_sweep: reclaim dead objects at line granularity
 *
 * Algorithm:
 * 1. For each block, clear all line marks to LINE_FREE
 * 2. Walk objects linearly (using alloc_total to advance)
 * 3. Live objects (mark == current_mark): re-mark their lines as OCCUPIED
 * 4. Dead objects: zero their memory (enables safe walking on future cycles)
 * 5. All-free blocks returned to global pool
 * ====================================================================== */

size_t gc_sweep(ThreadHeap *heap) {
    uint8_t  current_mark = heap->current_mark;
    GCBlock *block = heap->blocks;
    GCBlock *prev  = NULL;
    size_t   bytes_survived = 0;
    size_t   old_gen_bytes  = 0; /* recount old gen during major sweep */

    while (block) {
        GCBlock *next = block->next;

        /* Phase 1: clear all line marks to FREE */
        memset(block->line_map, GC_LINE_FREE, GC_LINES_PER_BLOCK);

        /* Phase 2: walk all objects, re-mark live objects' lines */
        uint8_t *ptr = block->payload;
        uint8_t *end = block->payload + GC_BLOCK_SIZE;

        while (ptr < end) {
            GCHeader *hdr   = (GCHeader *)ptr;
            uint16_t  total = hdr->alloc_total;

            if (total == 0) {
                /* Zero region — advance by 8 (alignment). See
                 * gc_sweep_concurrent for why line-skip is unsafe. */
                ptr += 8;
                continue;
            }

            if (hdr->mark == current_mark) {
                /* Live object — mark all its lines as OCCUPIED */
                size_t offset = (size_t)(ptr - block->payload);
                int first_line = (int)(offset / GC_LINE_SIZE);
                int last_line  = (int)((offset + total - 1) / GC_LINE_SIZE);
                for (int i = first_line; i <= last_line; i++) {
                    block->line_map[i] = GC_LINE_OCCUPIED;
                }
                bytes_survived += total;

                /* Promotion: young objects that survive 2 GC cycles become old */
                if (hdr->gen == 0) {
                    uint8_t sc = hdr->survive_count;
                    if (sc >= 1) {
                        hdr->gen = 1; /* promote to old generation */
                        old_gen_bytes += total;
                    } else {
                        hdr->survive_count = sc + 1;
                    }
                } else {
                    /* Already old — count towards old gen total */
                    old_gen_bytes += total;
                }
            } else {
                /* Dead object — finalize external resources, then zero */
                gc__finalize_dead(hdr);
                memset(ptr, 0, total);
            }

            ptr += total;
        }

        /* Phase 3: derive first-free hint and all-free flag from the
         * rebuilt line map. §14 tier-2: first_free_line lets the next
         * slow-path search skip the OCCUPIED prefix. */
        GCBlockSweepSummary sum = gc__summarize_block_map(block->line_map);
        if (sum.all_free) {
            /* Remove from heap's block list and return to pool */
            if (prev) prev->next = next;
            else      heap->blocks = next;
            gc_block_pool_return(heap->pool, block);
        } else {
            block->first_free_line = (uint16_t)sum.first_free;
            prev = block;
        }

        block = next;
    }

    /* Invalidate cursor — gc_alloc will rescan for free runs */
    heap->cursor        = NULL;
    heap->limit         = NULL;
    heap->current_block = NULL;
    /* §14: reset slow-path resume cursor (see comment above). */
    heap->search_block  = NULL;

    /* Update old gen tracking — major GC recounts everything */
    heap->old_gen_bytes = old_gen_bytes;

    return bytes_survived;
}

/* ======================================================================
 * gc_sweep_intern_table: evict dead interned strings as tombstones.
 *
 * Called after mark phase but before sweep phase. Dead entries (mark
 * mismatch and not epoch-protected) are always replaced with tombstone
 * sentinels — the underlying JaclHeapString is then freed by gc_sweep.
 * Tombstones are compacted away on the next jacl_intern insert that
 * crosses the tombstone-ratio threshold (string.c).
 *
 * Earlier versions only tombstoned when load factor was already > 0.75
 * and otherwise retroactively marked dead entries live. That made
 * interned strings effectively immortal under sustained churn: the
 * power-of-two resize keeps load near 0.5, so the high-load branch
 * almost never fired, and unrooted entries were carried forward cycle
 * after cycle (AUDIT.md §4).
 * ====================================================================== */

/* Check if a pointer falls within any of this heap's blocks. */
bool gc__ptr_in_heap(ThreadHeap *heap, void *ptr) {
    uint8_t *p = (uint8_t *)ptr;
    for (GCBlock *b = heap->blocks; b; b = b->next) {
        if (p >= b->payload && p < b->payload + GC_BLOCK_SIZE)
            return true;
    }
    return false;
}

void gc_sweep_intern_table(JaclInternTable *table,
                                   ThreadHeap *heap,
                                   uint32_t watermark) {
    /* Skip sweep if we're inside jacl_intern — the allocation that
     * triggered this GC is about to insert into the table, and
     * evicting entries now would create spurious duplicates.
     * (Only relevant to the single-threaded path: the concurrent GC
     * runs on its own thread where gc__interning is never set.) */
    if (gc__interning) return;

    uint8_t current_mark = heap->current_mark;
    MUTEX_LOCK(table->lock);

    /* gc__ptr_in_heap walks heap->blocks. Under concurrent GC the owning
     * worker may be head-inserting a fresh block from gc_alloc's slow
     * path. Hold blocks_mutex for the walk to serialize with that
     * insert; same lock the concurrent block-sweep takes (AUDIT.md §7,
     * §15). Lock order matches jacl_intern: gc_alloc takes blocks_mutex
     * outside table->lock, never the reverse, so acquiring blocks_mutex
     * while holding table->lock here is safe.
     *
     * Always lock — using watermark==0 as the discriminator for "single-
     * threaded" was wrong: in concurrent mode the watermark can be 0 on
     * the first cycle (fresh workers at thread_epoch=0), and the gate
     * skipped exactly when the lock was needed. The uncontended single-
     * threaded acquire is free. */
    MUTEX_LOCK(heap->blocks_mutex);

    for (uint32_t i = 0; i < table->cap; i++) {
        JaclHeapString *entry = table->entries[i];
        if (entry == NULL || entry == INTERN_TOMBSTONE) continue;

        /* Only evict entries allocated on this heap.
         * Other heaps' entries have independent mark bits. */
        if (!gc__ptr_in_heap(heap, entry)) continue;

        GCHeader *hdr = gc_header_of(entry);
        if (hdr->mark == current_mark) continue;

        /* Concurrent-GC epoch protection: an entry interned after the
         * marker enumerated roots (which no longer includes the intern
         * table — see gc_enumerate_roots) is not marked but must survive.
         * Fresh allocations carry hdr->epoch >= watermark, the same
         * invariant gc_sweep_concurrent relies on. Single-threaded
         * callers pass watermark=0, disabling this branch. */
        if (watermark != 0 && hdr->epoch >= watermark) continue;

        /* Dead. Tombstone the slot so probes still find later entries;
         * the underlying JaclHeapString is freed by the following
         * gc_sweep (its mark didn't get bumped this cycle). */
        table->entries[i] = INTERN_TOMBSTONE;
        table->count--;
        table->tombstone_count++;
    }

    MUTEX_UNLOCK(heap->blocks_mutex);
    MUTEX_UNLOCK(table->lock);
}

/* ======================================================================
 * gc_collect: full single-threaded mark-sweep cycle
 * ====================================================================== */

/* Adjust gc_threshold based on survival rate after a GC cycle.
 * High survival (>80%) → increase threshold by 50% (too much live data).
 * Low survival (<20%) → decrease threshold by 25% (lots of garbage).
 * Clamped to [GC_THRESHOLD_MIN, GC_THRESHOLD_MAX]. */
void gc__adjust_threshold(ThreadHeap *heap, size_t bytes_survived) {
    size_t allocated = ATOMIC_LOAD_EXPLICIT(&heap->bytes_since_gc,
                                             MEM_RELAXED);
    if (allocated == 0) return;

    /* survival_rate = bytes_survived / bytes_allocated (percentage * 100) */
    size_t rate_pct = (bytes_survived * 100) / allocated;

    /* gc_threshold is read by the allocator (gc__bump_alloc) from another
     * thread. Tag accesses as relaxed atomic — it's a heuristic, precision
     * isn't required, but the access must be atomic for TSAN. */
    size_t th = ATOMIC_LOAD_EXPLICIT(&heap->gc_threshold, MEM_RELAXED);
    if (rate_pct > 80) {
        th = th + th / 2;       /* High survival — back off */
    } else if (rate_pct < 20) {
        th = th - th / 4;       /* Low survival — collect more often */
    }
    if (th < GC_THRESHOLD_MIN) th = GC_THRESHOLD_MIN;
    if (th > GC_THRESHOLD_MAX) th = GC_THRESHOLD_MAX;
    ATOMIC_STORE_EXPLICIT(&heap->gc_threshold, th, MEM_RELAXED);
}

void gc_collect(ThreadHeap *heap, VM *vm) {
    gc__struct_registry = vm ? vm->struct_registry : NULL;
    gc_mark(heap, vm);

    /* Evict dead intern table entries before sweep zeroes their memory.
     * watermark=0: single-threaded path, no concurrent allocations to
     * protect — mark bit alone determines liveness. */
    if (vm && vm->intern_table) {
        gc_sweep_intern_table(vm->intern_table, heap, 0);
    }

    size_t bytes_survived = gc_sweep(heap);
    gc__adjust_threshold(heap, bytes_survived);
    heap->current_mark  = 1 - heap->current_mark;
    heap->bytes_since_gc = 0;
    heap->needs_gc       = false;
    heap->gc_cycle_count++;
    /* Snapshot old gen size after major GC for scheduling heuristics */
    heap->last_major_old_gen_bytes = heap->old_gen_bytes;
}

/* ======================================================================
 * gc_mark_minor: trace from roots but STOP at old-gen objects.
 *
 * Minor GC only collects young objects. Old-gen objects are treated as
 * opaque roots — marked but not traced (their children are assumed live).
 * Exception: remembered set entries are old-gen containers that store
 * young-gen values; their children ARE traced to find young objects
 * reachable only through old-gen containers.
 * ====================================================================== */

void gc_mark_minor(ThreadHeap *heap, VM *vm,
                           RememberedSet *remembered_set) {
    GCMarkStack ms;
    gc__ms_init(&ms);

    uint8_t mark = heap->current_mark;

    /* --- Root enumeration (same as full GC) --- */

    /* 1. VM stack values — skip slots containing raw inline struct bytes */
    for (uint32_t i = 0; i < vm->stack_top; i++) {
        if (!BITMAP_GET(vm->inline_slot_bitmap, i)) {
            gc__ms_push_val(&ms, vm->stack[i]);
        }
    }

    /* 2. Call frame closures. See gc_mark for the stack-closure skip rationale. */
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        JaclClosure *cl = vm->frames[i].closure;
        if (cl && gc__ptr_in_heap(heap, cl)) {
            gc__ms_push(&ms, cl);
        }
    }

    /* 3. Global environment values */
    for (uint32_t i = 0; i < vm->env.count; i++) {
        gc__ms_push_val(&ms, vm->env.values[i]);
    }

    /* 4. Intern table entries */
    if (vm->intern_table) {
        for (uint32_t i = 0; i < vm->intern_table->cap; i++) {
            if (vm->intern_table->entries[i]) {
                gc__ms_push(&ms, vm->intern_table->entries[i]);
            }
        }
    }

    /* 5. Call frame chunk constants */
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        BytecodeChunk *ch = vm->frames[i].chunk;
        if (ch) {
            for (uint32_t j = 0; j < ch->const_count; j++) {
                gc__ms_push_const(&ms, ch->constants[j]);
            }
        }
    }

    /* 6. External GC handle slots (embedding API) */
    if (vm->gc_handle_slots) {
        for (uint32_t i = 0; i < vm->gc_handle_count; i++) {
            gc__ms_push_val(&ms, vm->gc_handle_slots[i]);
        }
    }

    /* 7. Remembered set: trace old-gen containers' children.
     * These are old-gen mutable refs that point to young-gen values.
     * We trace them so their young-gen children are marked. */
    if (remembered_set) {
        for (uint32_t i = 0; i < remembered_set->count; i++) {
            JaclVal container = remembered_set->entries[i];
            if (jacl_is_heap_type(container)) {
                void *ptr = jacl_as_ptr(container);
                GCHeader *hdr = gc_header_of(ptr);
                /* Mark the container itself */
                if (hdr->mark != mark) {
                    hdr->mark = mark;
                }
                /* Trace its children (the young values it points to) */
                gc__trace_object(ptr, &ms);
            }
        }
    }

    /* --- Minor mark loop: stop tracing at old-gen objects --- */
    void *ptr;
    while (gc__ms_pop(&ms, &ptr)) {
        GCHeader *hdr = gc_header_of(ptr);
        /* See gc_concurrent_collect for the alloc_total==0 guard. */
        if (hdr->alloc_total == 0) continue;
        if (hdr->mark == mark) continue; /* already marked this cycle */
        hdr->mark = mark;

        /* Old-gen objects: mark but DON'T trace children.
         * Their children are all old or will be caught by remembered set. */
        if (hdr->gen == 1) continue;

        /* Young-gen objects: trace normally */
        gc__trace_object(ptr, &ms);
    }

    gc__ms_destroy(&ms);
}

/* ======================================================================
 * gc_sweep_minor: only sweep young-generation objects (gen == 0).
 *
 * Old-gen objects and their lines are untouched. Dead young objects are
 * zeroed. Surviving young objects that meet the promotion threshold are
 * promoted to old gen.
 * ====================================================================== */

size_t gc_sweep_minor(ThreadHeap *heap) {
    uint8_t  current_mark = heap->current_mark;
    GCBlock *block = heap->blocks;
    size_t   bytes_survived = 0;
    size_t   promoted_bytes = 0; /* bytes promoted to old gen this cycle */

    while (block) {
        GCBlock *next = block->next;

        /* Phase 1: clear lines that ONLY contain young objects to FREE.
         * Lines containing any old-gen object must stay OCCUPIED.
         * We do this in two passes: first determine which lines have old
         * objects, then process young objects. */

        /* Track which lines contain old-gen objects (must not be freed) */
        bool old_on_line[GC_LINES_PER_BLOCK];
        memset(old_on_line, 0, sizeof(old_on_line));

        /* First pass: identify lines with old-gen or live young objects */
        uint8_t *ptr = block->payload;
        uint8_t *end = block->payload + GC_BLOCK_SIZE;

        while (ptr < end) {
            GCHeader *hdr   = (GCHeader *)ptr;
            uint16_t  total = hdr->alloc_total;

            if (total == 0) {
                /* Zero region — advance by alignment. See
                 * gc_sweep_concurrent for why line-skip is unsafe. */
                ptr += 8;
                continue;
            }

            if (hdr->gen == 1) {
                /* Old-gen object — protect its lines */
                size_t offset = (size_t)(ptr - block->payload);
                int first_line = (int)(offset / GC_LINE_SIZE);
                int last_line  = (int)((offset + total - 1) / GC_LINE_SIZE);
                for (int i = first_line; i <= last_line; i++) {
                    old_on_line[i] = true;
                }
            }

            ptr += total;
        }

        /* Phase 2: clear line marks for lines without old objects */
        for (int i = 0; i < GC_LINES_PER_BLOCK; i++) {
            if (!old_on_line[i]) {
                block->line_map[i] = GC_LINE_FREE;
            }
        }

        /* Phase 3: walk objects — handle young objects only */
        ptr = block->payload;
        while (ptr < end) {
            GCHeader *hdr   = (GCHeader *)ptr;
            uint16_t  total = hdr->alloc_total;

            if (total == 0) {
                /* Zero region — advance by alignment. See
                 * gc_sweep_concurrent for why line-skip is unsafe. */
                ptr += 8;
                continue;
            }

            if (hdr->gen == 1) {
                /* Old-gen: untouched, count as survived */
                bytes_survived += total;
            } else if (hdr->mark == current_mark) {
                /* Live young object — re-mark lines as OCCUPIED */
                size_t offset = (size_t)(ptr - block->payload);
                int first_line = (int)(offset / GC_LINE_SIZE);
                int last_line  = (int)((offset + total - 1) / GC_LINE_SIZE);
                for (int i = first_line; i <= last_line; i++) {
                    block->line_map[i] = GC_LINE_OCCUPIED;
                }
                bytes_survived += total;

                /* Promotion: young objects that survive 2 GC cycles */
                uint8_t sc = hdr->survive_count;
                if (sc >= 1) {
                    hdr->gen = 1; /* promote to old generation */
                    promoted_bytes += total;
                } else {
                    hdr->survive_count = sc + 1;
                }
            } else {
                /* Dead young object — finalize external resources, then zero */
                gc__finalize_dead(hdr);
                memset(ptr, 0, total);
            }

            ptr += total;
        }

        /* §14 tier-2: refresh per-block first-free hint from the final
         * line_map. Minor sweep doesn't unlink fully-empty blocks (old
         * objects keep blocks pinned), so the all_free flag is ignored. */
        {
            GCBlockSweepSummary sum = gc__summarize_block_map(block->line_map);
            block->first_free_line = (uint16_t)sum.first_free;
        }

        block = next;
    }

    /* Invalidate cursor — gc_alloc will rescan for free runs */
    heap->cursor        = NULL;
    heap->limit         = NULL;
    heap->current_block = NULL;
    /* §14: reset slow-path resume cursor — sweep may have freed runs
     * before search_block, and re-walking from heap->blocks head will
     * find them. */
    heap->search_block  = NULL;

    /* Update old gen tracking — add newly promoted bytes */
    heap->old_gen_bytes += promoted_bytes;

    return bytes_survived;
}

/* ======================================================================
 * gc_collect_minor: single-threaded minor GC cycle.
 *
 * Only traces and sweeps young-generation objects. Uses the remembered
 * set to find young objects reachable through old-gen containers.
 * Clears the remembered set after collection.
 * ====================================================================== */

void gc_collect_minor(ThreadHeap *heap, VM *vm,
                              RememberedSet *remembered_set) {
    gc__struct_registry = vm ? vm->struct_registry : NULL;
    gc_mark_minor(heap, vm, remembered_set);
    size_t bytes_survived = gc_sweep_minor(heap);
    gc__adjust_threshold(heap, bytes_survived);
    heap->current_mark  = 1 - heap->current_mark;
    heap->bytes_since_gc = 0;
    heap->needs_gc       = false;
    heap->gc_cycle_count++;

    /* Clear remembered set — entries were processed during mark */
    if (remembered_set) {
        remembered_set->count = 0;
    }
}

/* ======================================================================
 * gc_should_major: determine if a major GC is needed instead of minor.
 *
 * Returns true if:
 *   - This is the first GC cycle (no old gen exists yet)
 *   - Old generation has grown >50% since last major GC
 * ====================================================================== */

bool gc_should_major(ThreadHeap *heap) {
    /* First GC cycle is always major */
    if (heap->gc_cycle_count == 0) return true;

    /* Major GC if old gen grew >50% since last major */
    size_t threshold = heap->last_major_old_gen_bytes
                     + heap->last_major_old_gen_bytes / 2;
    if (heap->old_gen_bytes > threshold) return true;

    return false;
}

/* ======================================================================
 * gc_sweep_concurrent: concurrent-safe sweep with epoch watermark
 *
 * Unlike gc_sweep (single-threaded), this function:
 * - Skips `skip_block` (the owning thread's active allocation block)
 * - Respects the epoch watermark: objects with epoch >= watermark are
 *   immune to collection regardless of mark state (too new to judge)
 * - Uses a two-phase per-block approach to avoid exposing intermediate
 *   line map state to concurrent allocators:
 *     Phase 1: Zero dead objects (keeping old line map intact)
 *     Phase 2: Build new line map from surviving objects, then memcpy
 * - Collects fully-empty blocks into a deferred list (caller recycles
 *   them after gc_running is set to false)
 * - Does NOT invalidate cursor/limit (owning worker may be allocating)
 * ====================================================================== */

size_t gc_sweep_concurrent(ThreadHeap *heap, GCBlock *skip_block,
                                   uint32_t watermark, uint8_t current_mark,
                                   BlockPool *pool) {
    /* Serialize blocks-list mutation with the allocator. The lock is held
     * for the entire sweep of this heap. Slow-path allocations on this
     * worker will stall briefly, but the bump-allocation fast path is
     * unaffected (it doesn't touch heap->blocks). The sweep itself runs
     * in tens of microseconds for typical heaps. */
    MUTEX_LOCK(heap->blocks_mutex);

    /* Re-snapshot current_block under the lock to avoid §7's stale-skip
     * race: the value passed in by the caller may have been computed before
     * the allocator switched current_block. */
    skip_block = heap->current_block;

    GCBlock **pp = &heap->blocks;
    size_t    bytes_survived = 0;
    size_t    old_gen_bytes  = 0; /* recount old gen during concurrent sweep */

    while (*pp) {
        GCBlock *block = *pp;

        if (block == skip_block) {
            pp = &block->next;
            continue;
        }

        uint8_t *ptr = block->payload;
        uint8_t *end = block->payload + GC_BLOCK_SIZE;

        /* Phase 1: zero dead objects (old line map still intact).
         *
         * Walker invariant: ptr always points at an object header OR at
         * a zeroed region. On total==0 we advance by 8 (the GC alignment)
         * rather than jumping to the next line — a previously-dead-and-
         * zeroed object inside an OCCUPIED line can sit immediately
         * before a live object that starts mid-line and spans into the
         * next line. Line-skipping would jump OVER that live object's
         * header and land in its payload, where bytes look like a
         * "header" with garbage alloc_total. */
        while (ptr < end) {
            GCHeader *hdr   = (GCHeader *)ptr;
            uint16_t  total = hdr->alloc_total;

            if (total == 0) {
                ptr += 8;
                continue;
            }

            bool is_live = (hdr->mark == current_mark) ||
                           (hdr->epoch >= watermark);

            if (!is_live) {
                gc__finalize_dead(hdr);
                memset(ptr, 0, total);
            }

            ptr += total;
        }

        /* Phase 2: build new line map from surviving objects */
        uint8_t new_map[GC_LINES_PER_BLOCK];
        memset(new_map, GC_LINE_FREE, GC_LINES_PER_BLOCK);

        ptr = block->payload;
        while (ptr < end) {
            GCHeader *hdr   = (GCHeader *)ptr;
            uint16_t  total = hdr->alloc_total;

            if (total == 0) {
                /* Zero region — advance by alignment (see Phase 1 comment). */
                ptr += 8;
                continue;
            }

            /* Live object — mark its lines in the new map */
            size_t offset     = (size_t)(ptr - block->payload);
            int    first_line = (int)(offset / GC_LINE_SIZE);
            int    last_line  = (int)((offset + total - 1) / GC_LINE_SIZE);
            if (last_line >= GC_LINES_PER_BLOCK)
                last_line = GC_LINES_PER_BLOCK - 1;
            for (int i = first_line; i <= last_line; i++) {
                new_map[i] = GC_LINE_OCCUPIED;
            }
            bytes_survived += total;

            /* Promotion: young objects that survive 2 GC cycles become old */
            if (hdr->gen == 0) {
                uint8_t sc = hdr->survive_count;
                if (sc >= 1) {
                    hdr->gen = 1; /* promote to old generation */
                    old_gen_bytes += total;
                } else {
                    hdr->survive_count = sc + 1;
                }
            } else {
                /* Already old — count towards old gen total */
                old_gen_bytes += total;
            }

            ptr += total;
        }

        /* Phase 3: update line map.
         *
         * NOTE: This memcpy races with gc_alloc's slow path (gc__find_free_run)
         * on the owning worker thread reading line_map entries. This is technically
         * a data race under C11, but is benign in practice:
         *
         * One-directional invariant:
         * - Sweep only transitions lines OCCUPIED (0x01) -> FREE (0x00), never
         *   the reverse. gc_alloc marks lines OCCUPIED only *after* choosing a
         *   free run and bump-allocating into it, which cannot conflict with
         *   sweep (sweep skips the owning thread's skip_block / active block).
         *
         * Phase 1 zeroing guarantee:
         * - Phase 1 (above) zeroes all dead object memory *before* we update
         *   the line map here. So by the time any line transitions to FREE,
         *   the underlying payload is already zeroed. An allocator that sees
         *   FREE will bump-allocate into safely zeroed memory.
         *
         * Partially-updated map outcomes:
         * - gc_alloc seeing a partially-updated map either:
         *   (a) sees OCCUPIED for a now-free line -> misses free space (harmless)
         *   (b) sees FREE for a freed line -> allocates into zeroed memory (correct)
         * - No incorrect allocation or use-after-free can result.
         *
         * ARM/RISC-V (weakly-ordered) analysis:
         * - On x86, memcpy typically uses wide stores (rep movsb, AVX) that
         *   are byte-granular and TSO-ordered. On ARM/RISC-V, memcpy may use
         *   wide stores (e.g., stp for 16-byte pairs on AArch64, or SIMD
         *   stores) that write multiple line_map bytes in a single store.
         * - A concurrent reader (gc__find_free_run) doing byte-sized loads
         *   may observe a torn read from a partially-written wide store.
         *   However, line_map values are only 0x00 (FREE) or 0x01 (OCCUPIED).
         *   A torn byte from a wide store that writes 0x00 over a former 0x01
         *   can only produce intermediate values with some bits set (non-zero),
         *   which gc__find_free_run treats as OCCUPIED (it checks != 0x00).
         *   This is equivalent to outcome (a) above: missing free space, which
         *   is harmless — the allocator will find it on the next scan.
         * - The one-directional invariant (0x01 -> 0x00 only) ensures no torn
         *   read can produce 0x00 from a byte that should be 0x01, because
         *   the store is writing 0x00 (all bits clear) — partial completion
         *   of clearing bits can only leave some bits still set (non-zero).
         *
         * TSan suppression:
         * - TSan will report this memcpy as a data race since there is no
         *   synchronization between the GC thread's write and the worker
         *   thread's read. This is a known benign race. Suppress with
         *   __attribute__((no_sanitize("thread"))) on gc_sweep_concurrent,
         *   or add a file-level suppression in the TSan suppressions file.
         */
        memcpy(block->line_map, new_map, GC_LINES_PER_BLOCK);

        /* Derive scan hint + all-free flag from new_map (cache-hot,
         * equivalent to reading block->line_map post-memcpy). The hint
         * is set AFTER the memcpy above so a benign concurrent reader on
         * line_map can't see a hint pointing past where the old map has
         * FREE lines. Concurrent slow-path readers serialize on
         * heap->blocks_mutex (held for the whole sweep), so the actual
         * worry is only the bump-alloc fast path on skip_block — and
         * skip_block isn't swept here. */
        GCBlockSweepSummary sum = gc__summarize_block_map(new_map);
        if (sum.all_free && pool) {
            *pp = block->next;
            block->next = NULL;
            gc_block_pool_return(pool, block);
        } else {
            block->first_free_line = (uint16_t)sum.first_free;
            pp = &block->next;
        }
    }

    /* Update old gen tracking — concurrent sweep recounts everything */
    heap->old_gen_bytes = old_gen_bytes;

    /* §14: reset slow-path resume cursor so the post-sweep allocator
     * re-discovers freed runs near the list head. Either search_block
     * has been unlinked (UAF if we left it set) or it still points at
     * an existing block but earlier blocks now have free runs the
     * resume cursor would skip. Held under blocks_mutex; the gc_alloc
     * slow path takes the same mutex before reading these fields. */
    heap->search_block = NULL;

    MUTEX_UNLOCK(heap->blocks_mutex);
    return bytes_survived;
}

#endif /* GC_COLLECT_C */
