//! P1.2/P1.3 — heap allocator + conservative non-moving mark-sweep (gc.roots).
use jacl_runtime_harness::run_test;

#[test]
fn gc_mark_sweep() {
    for n in [1, 7, 64, 500] {
        assert_eq!(run_test("test_gc.c", n), 529,
            "gc invariants for n={n} garbage objects (reachable graph survives, n reclaimed x2)");
    }
}
