//! P3.5 — host I/O through the powerbox (Stream.write), differential interp == jit.
use jacl_runtime_harness::run_powerbox;

#[test]
fn print_hello() {
    assert_eq!(run_powerbox("test_print.c"), b"hi\n",
        "write(1,...) reaches stdout via the granted Stream capability on both backends");
}
