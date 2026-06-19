//! P3.5 — host I/O through the powerbox (Stream.write), differential interp == jit.
use jacl_runtime_harness::run_powerbox;

#[test]
fn print_hello() {
    assert_eq!(run_powerbox("test_print.c"), b"hi\n",
        "write(1,...) reaches stdout via the granted Stream capability on both backends");
}

#[test]
fn jacl_print_string_and_int() {
    assert_eq!(run_powerbox("test_jacl_print.c"), b"hello\n42\n",
        "jacl_print writes value text + newline to stdout via the powerbox (interp == jit)");
}
