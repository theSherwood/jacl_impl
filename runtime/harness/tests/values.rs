//! P1.1 — value representation. Mirrors src/jacl.h's tag scheme.
use jacl_runtime_harness::run_test;

#[test]
fn values() {
    assert_eq!(run_test("test_values.c", 0), 4242, "value representation checks");
}
