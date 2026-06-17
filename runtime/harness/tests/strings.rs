//! P1.4 — strings: inline/heap/interning, and string/GC interaction.
use jacl_runtime_harness::run_test;

#[test]
fn strings() {
    assert_eq!(run_test("test_strings.c", 0), 1414, "inline/heap/intern/eq");
}

#[test]
fn strings_gc() {
    for n in [1, 5, 50, 300] {
        assert_eq!(run_test("test_strings_gc.c", n), 707,
            "interned string survives GC via the table; {n} non-interned reclaimed");
    }
}
