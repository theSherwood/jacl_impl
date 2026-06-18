//! P2.2 — codegen scaffold proof (real JACL source → SVM-IR → run).
//!
//! Builds the JACL frontend + the P2.2 codegen (`codegen/codegen.c`) into a driver
//! (`codegen/tests/emit_jacl.c`, compiled with gcc — the frontend's toolchain),
//! which parses a JACL snippet and emits the program module's svm-text. Each program
//! is linked against the separately-translated runtime (the P2.0 path) and run on
//! interp + JIT; the result (an i32 JaclVal) is checked against the expected value.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use jacl_runtime_harness::translate_runtime;
use svm_interp::Value;
use svm_ir::{link, LinkUnit};

const CODEGEN_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../codegen");

/// Build the codegen driver once per test process (gcc: the unity frontend uses
/// pre-declaration ordering clang rejects under -std=c99).
fn driver() -> &'static Path {
    static BIN: OnceLock<PathBuf> = OnceLock::new();
    BIN.get_or_init(|| {
        let out = std::env::temp_dir().join(format!("jacl_emit_jacl_{}", std::process::id()));
        let status = Command::new("gcc")
            .args(["-O0", "-w", "-D_DEFAULT_SOURCE"])
            .arg(format!("{CODEGEN_DIR}/tests/emit_jacl.c"))
            .arg(format!("{CODEGEN_DIR}/codegen.c"))
            .arg(format!("{CODEGEN_DIR}/irbuilder.c"))
            .arg("-o")
            .arg(&out)
            .args(["-lm", "-lpthread"])
            .status()
            .expect("spawn gcc (is gcc installed?)");
        assert!(status.success(), "gcc failed to build the codegen driver");
        out
    })
    .as_path()
}

/// Emit one case's program svm-text via the codegen.
fn emit(case: &str) -> String {
    let out = Command::new(driver()).arg(case).output().expect("run emit_jacl");
    assert!(
        out.status.success(),
        "emit_jacl {case} failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    String::from_utf8(out.stdout).expect("emit_jacl output is UTF-8")
}

/// Run a case expected to fail codegen; returns the emitted error (stderr).
fn emit_expecting_error(case: &str) -> String {
    let out = Command::new(driver()).arg(case).output().expect("run emit_jacl");
    assert!(!out.status.success(), "{case}: expected codegen to fail but it succeeded");
    String::from_utf8_lossy(&out.stderr).into_owned()
}

/// JaclVal encoding of an i32 (tag 0x02 high byte), matching the runtime + codegen.
fn i32_val(x: i32) -> i64 {
    ((0x02u64 << 56) | (x as u32 as u64)) as i64
}

/// Compile `case` from JACL source, link it against the runtime, run on interp + JIT,
/// and assert the returned JaclVal equals `want`.
fn run_case(case: &str, want: i64) {
    let program = svm_text::parse_module(&emit(case)).unwrap_or_else(|e| panic!("parse {case}: {e:?}"));

    let rt = translate_runtime();
    let entry_sp = rt.entry_sp;
    let entry = rt.module.funcs.len() as u32; // the program function lands after the runtime
    let linked = link(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit { module: program, ..Default::default() },
    ])
    .unwrap_or_else(|e| panic!("link {case}: {e:?}"));
    assert!(linked.imports.is_empty(), "{case}: unresolved imports {:?}", linked.imports);
    svm_verify::verify_module(&linked).unwrap_or_else(|e| panic!("verify {case}: {e:?}"));

    let args = [Value::I64(entry_sp as i64)];
    let mut fuel = 2_000_000_000u64;
    let interp = svm_interp::run(&linked, entry, &args, &mut fuel).expect("interp run");
    let iv = match interp[0] {
        Value::I64(x) => x,
        ref other => panic!("{case}: unexpected interp value {other:?}"),
    };
    assert_eq!(iv, want, "interp {case}");

    let jv = match svm_jit::compile_and_run(&linked, entry, &[entry_sp as i64]).expect("jit run") {
        svm_jit::JitOutcome::Returned(s) => s[0],
        other => panic!("{case}: jit did not return: {other:?}"),
    };
    assert_eq!(jv, want, "jit {case}");
}

#[test]
fn arithmetic_add() {
    run_case("add", i32_val(3)); // [+ 1 2]
}

#[test]
fn arithmetic_nested() {
    run_case("nested", i32_val(7)); // [+ 1 [* 2 3]]
}

#[test]
fn arithmetic_sub_of_mul() {
    run_case("submul", i32_val(17)); // [- [* 4 5] 3]
}

#[test]
fn arithmetic_variadic_fold() {
    run_case("variadic", i32_val(10)); // [+ 1 2 3 4]
}

#[test]
fn arithmetic_div_and_mod() {
    run_case("div_mod", i32_val(5)); // [+ [/ 20 6] [% 20 6]] = 3 + 2
}

#[test]
fn binding_def_and_read() {
    run_case("bind_read", i32_val(15)); // def x 5; [+ $x 10]
}

#[test]
fn binding_mutate_with_set() {
    run_case("mutate", i32_val(42)); // mut c 1; set c 42; [+ $c 0]
}

#[test]
fn binding_set_to_expression() {
    run_case("reassign_expr", i32_val(42)); // mut c 1; set c [+ $c 41]; [+ $c 0]
}

#[test]
fn binding_block_scope() {
    run_case("scoped_block", i32_val(11)); // def x 1; { def y 10; [+ $x $y] }
}

#[test]
fn binding_same_scope_shadow_is_an_error() {
    let err = emit_expecting_error("shadow_err"); // def x 1; def x 2
    assert!(err.contains("already defined in this scope"), "unexpected error: {err}");
}

#[test]
fn binding_set_undefined_is_an_error() {
    let err = emit_expecting_error("set_undef_err"); // set nope 1
    assert!(err.contains("undefined variable"), "unexpected error: {err}");
}

#[test]
fn binding_set_immutable_is_an_error() {
    let err = emit_expecting_error("set_immut_err"); // def x 1; set x 2
    assert!(err.contains("immutable"), "unexpected error: {err}");
}
