/*
 * JACL Garbage Collector — Mark and sweep phases.
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

static void gc__ms_init(GCMarkStack *ms) {
    ms->top      = 0;
    ms->overflow = NULL;
    ms->ov_count = 0;
    ms->ov_cap   = 0;
}

static void gc__ms_push(GCMarkStack *ms, void *ptr) {
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

static bool gc__ms_pop(GCMarkStack *ms, void **out) {
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

static void gc__ms_destroy(GCMarkStack *ms) {
    free(ms->overflow);
    ms->overflow = NULL;
    ms->ov_count = 0;
    ms->ov_cap   = 0;
}

/* Push a JaclVal if it refers to a GC-managed heap object */
static inline void gc__ms_push_val(GCMarkStack *ms, JaclVal v) {
    if (jacl_is_heap_type(v)) {
        gc__ms_push(ms, jacl_as_ptr(v));
    }
}

/* ======================================================================
 * Object tracing: push an object's children onto the mark stack
 * ====================================================================== */

static void gc__trace_object(void *payload, GCMarkStack *ms) {
    GCHeader *hdr = gc_header_of(payload);

    switch (hdr->obj_type) {

    /* --- Leaf types: no outgoing references --- */
    case OBJ_STRING:
    case OBJ_HEAP_I64:
    case OBJ_HEAP_U64:
    case OBJ_HEAP_F64:
    case OBJ_BIGNUM:
        break;

    /* --- Closure: trace captured upvalues --- */
    case OBJ_CLOSURE: {
        JaclClosure *cl = (JaclClosure *)payload;
        for (uint8_t i = 0; i < cl->upvalue_count; i++) {
            gc__ms_push_val(ms, cl->upvalues[i]);
        }
        break;
    }

    /* --- Mutable ref (cell/box/atom): trace contained value --- */
    case OBJ_MUTABLE_REF: {
        JaclMutableRef *ref = (JaclMutableRef *)payload;
        gc__ms_push_val(ms, ref->value);
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

    /* --- HAMT leaf: trace key and value --- */
    case OBJ_HAMT_LEAF: {
        jacl_map_leaf *leaf = (jacl_map_leaf *)payload;
        gc__ms_push_val(ms, leaf->key);
        gc__ms_push_val(ms, leaf->value);
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
        for (uint32_t i = 0; i < leaf->count; i++) {
            gc__ms_push_val(ms, leaf->elements[i]);
        }
        break;
    }

    /* --- RRB root: trace tree root and tail --- */
    case OBJ_RRB_ROOT: {
        jacl_vec_root *root = (jacl_vec_root *)payload;
        if (root->root) gc__ms_push(ms, root->root);
        if (root->tail) gc__ms_push(ms, root->tail);
        break;
    }

    default:
        break;
    }
}

/* ======================================================================
 * gc_mark: trace from GC roots through the object graph, marking live objects
 * ====================================================================== */

static void gc_mark(ThreadHeap *heap, VM *vm) {
    GCMarkStack ms;
    gc__ms_init(&ms);

    uint8_t mark = heap->current_mark;

    /* --- Root enumeration --- */

    /* 1. VM stack values */
    for (uint32_t i = 0; i < vm->stack_top; i++) {
        gc__ms_push_val(&ms, vm->stack[i]);
    }

    /* 2. Call frame closures */
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        if (vm->frames[i].closure) {
            gc__ms_push(&ms, vm->frames[i].closure);
        }
    }

    /* 3. Global environment values */
    for (uint32_t i = 0; i < vm->env.count; i++) {
        gc__ms_push_val(&ms, vm->env.values[i]);
    }

    /* --- Mark loop --- */
    void *ptr;
    while (gc__ms_pop(&ms, &ptr)) {
        GCHeader *hdr = gc_header_of(ptr);
        if (hdr->mark == mark) continue; /* already marked this cycle */
        hdr->mark = mark;
        gc__trace_object(ptr, &ms);
    }

    gc__ms_destroy(&ms);
}

#endif /* GC_COLLECT_C */
