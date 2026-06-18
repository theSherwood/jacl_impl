//! P1.6 — stackless stream iterators (pull-based combinators + GC interaction).
use jacl_runtime_harness::run_test;

#[test]
fn stream() {
    assert_eq!(run_test("test_stream.c", 0), 606, "range/vec | map/filter/take | collect");
}

#[test]
fn stream_gc() {
    for n in [1, 8, 80] {
        assert_eq!(run_test("test_stream_gc.c", n), 616,
            "a held stream pipeline keeps its source vec + string elements alive across GC");
    }
}
