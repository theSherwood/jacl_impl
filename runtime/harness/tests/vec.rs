//! P1.5 — persistent vector over the GC heap (build/get/persistence + GC tracing).
use jacl_runtime_harness::run_test;

#[test]
fn vec() {
    for n in [1, 5, 33, 100] {
        assert_eq!(run_test("test_vec.c", n), 333, "vector build/get/persistence n={n}");
    }
}

#[test]
fn vec_gc() {
    for n in [1, 4, 40, 200] {
        assert_eq!(run_test("test_vec_gc.c", n), 444,
            "vector of heap strings survives GC intact; {n} garbage reclaimed");
    }
}
