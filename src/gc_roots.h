/*
 * gc_roots.h — mark stack + root-enumeration callback, shared between the collector
 * (gc_collect.c) and the root providers (vm.c's single-threaded path, runtime.c's
 * concurrent path).
 *
 * Hoisted here (and the struct given a tag) so a root provider compiled BEFORE the
 * collector in the unity build (vm.c precedes gc_collect.c) can still name `GCMarkStack`
 * and push onto it. The point is that the collector no longer needs to know what a root
 * *is*: gc_mark/gc_collect take a `GcRootEnumerator` that pushes the live roots, so
 * gc_collect.c has no dependency on the bytecode `VM` type. The VM supplies its roots via
 * vm.c's enumerators; an emit-only build that never runs a VM supplies none and never calls
 * the collector at all.
 *
 * Include after gc.c (needs ThreadHeap) and value.c (needs JaclVal).
 */
#ifndef GC_ROOTS_H
#define GC_ROOTS_H

#define GC_MARK_STACK_SIZE 4096

/* Named struct (not an anonymous typedef) so it can be referenced before its methods are
 * defined further down the unity. Layout is owned here; gc_collect.c defines the methods. */
typedef struct GCMarkStack {
    void  *fixed[GC_MARK_STACK_SIZE];
    int    top;          /* next free slot in fixed array */
    void **overflow;     /* dynamically growing overflow buffer */
    int    ov_count;
    int    ov_cap;
} GCMarkStack;

/* A root provider pushes its live roots onto `ms`. `heap` is the heap being collected (for
 * the in-heap check on stack-allocated closures); `ctx` is provider-specific (the `VM*`). */
typedef void (*GcRootEnumerator)(ThreadHeap *heap, GCMarkStack *ms, void *ctx);

/* Mark-stack push helpers + the in-heap predicate (defined in gc_collect.c). Declared here so
 * a root provider (vm.c) compiled before gc_collect.c can call them within the same unity. */
void gc__ms_push(GCMarkStack *ms, void *ptr);
void gc__ms_push_val(GCMarkStack *ms, JaclVal v);
void gc__ms_push_const(GCMarkStack *ms, JaclVal v);
bool gc__ptr_in_heap(ThreadHeap *heap, void *ptr);

#endif /* GC_ROOTS_H */
