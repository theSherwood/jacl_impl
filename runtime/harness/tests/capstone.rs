//! Phase 1 DoD capstone — the whole runtime substrate exercised in one program.
use jacl_runtime_harness::run_test;

#[test]
fn capstone() {
    for n in [0, 3, 50, 250] {
        assert_eq!(run_test("test_capstone.c", n), 1234,
            "DoD: heap+GC + vec + map + intern + stream + builtins compose (garbage n={n})");
    }
}
