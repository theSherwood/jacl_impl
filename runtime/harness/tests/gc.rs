//! P1.2/P1.3 — heap allocator + conservative non-moving mark-sweep (gc.roots).
use jacl_runtime_harness::run_test;

#[test]
fn gc_mark_sweep() {
    for n in [1, 7, 64, 500] {
        assert_eq!(run_test("test_gc.c", n), 529,
            "gc invariants for n={n} garbage objects (reachable graph survives, n reclaimed x2)");
    }
}

/// jacl #159: a sweep hands a region with no live cell back to the pool, so a heap full of dead
/// cells of one size (the dead boxes of a wide-int loop) has room for another size, and a large
/// cell's regions are reused. Before, both ran out of memory with the heap all but empty.
#[test]
fn gc_reclaims_regions_across_size_classes() {
    assert_eq!(run_test("test_gc_regions.c", 0), 159,
        "a heap of dead 16-byte cells must make room for strings, and 1 MiB blobs must reuse regions");
}

/// jacl #159: when the heap really is full, the allocator stops the guest itself — it used to
/// return NULL, and the caller's write through it faulted at address 8 (or, at a larger offset,
/// landed in live memory).
#[test]
fn gc_out_of_memory_traps_in_the_allocator() {
    let (interp, jit) = jacl_runtime_harness::run_test_outcome("test_gc_oom.c", 0);
    assert_eq!(interp, "Trap(Unreachable)", "a live heap past 16 MiB must stop in jacl_alloc (interp)");
    assert_eq!(jit, "Trap(Unreachable)", "a live heap past 16 MiB must stop in jacl_alloc (JIT)");
}

/// jacl #175: a mark-stack overflow must not lose reachable objects. A node with more children than
/// the mark stack holds used to leave the excess marked but untraced, and what only they reached
/// was swept while live.
#[test]
fn gc_mark_stack_overflow_keeps_everything_reachable() {
    assert_eq!(run_test("test_gc_mark_overflow.c", 0), 175,
        "every leaf behind a fan wider than the mark stack must survive collection intact");
}
