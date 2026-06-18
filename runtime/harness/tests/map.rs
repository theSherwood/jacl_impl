//! P1.5 — persistent map (HAMT) over the GC heap.
use jacl_runtime_harness::run_test;

#[test]
fn map() {
    assert_eq!(run_test("test_map.c", 0), 555, "i32+string keys, persistence, overwrite, remove");
}

#[test]
fn map_gc() {
    for n in [1, 6, 60] {
        assert_eq!(run_test("test_map_gc.c", n), 565,
            "map of heap key->value strings survives GC intact; {n} garbage reclaimed");
    }
}
