//! P1.7 — builtins: arithmetic, comparisons, type ops, len/to_string, errors.
use jacl_runtime_harness::run_test;

#[test]
fn builtins() {
    assert_eq!(run_test("test_builtins.c", 0), 777, "value-op builtins + error propagation");
}
