//! jacl #121 — big-by-big division (Knuth algorithm D) through the public builtins.
use jacl_runtime_harness::run_test;

#[test]
fn bigint_divmod() {
    assert_eq!(run_test("test_bigint.c", 0), 819, "bigint division: add-back, q*b+r==a, signs");
}
