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

// ---- P2.4 control flow ----

#[test]
fn if_then_taken() {
    run_case("if_true", i32_val(10)); // [if [> 5 3] { 10 } else { 20 }]
}

#[test]
fn if_else_taken() {
    run_case("if_false", i32_val(20)); // [if [> 1 3] { 10 } else { 20 }]
}

#[test]
fn if_without_else_taken() {
    run_case("if_noelse_true", i32_val(7)); // [if [> 5 3] { 7 }]
}

#[test]
fn if_without_else_falls_to_nil() {
    run_case("if_noelse_false", 0); // [if [> 1 3] { 7 }] => nil (JACL_NIL == 0)
}

#[test]
fn if_branch_mutates_outer_binding() {
    run_case("if_sets_outer", i32_val(100)); // mut x 1; [if [> 5 3] { set x 100 }]; [+ $x 0]
}

#[test]
fn if_used_as_expression() {
    run_case("if_expr_bind", i32_val(9)); // def x [if [> 5 3] { 9 } else { 0 }]; [+ $x 0]
}

#[test]
fn elif_chain_middle_clause() {
    run_case("elif_chain", i32_val(22)); // [if false { 1 } elif true { 22 } else { 33 }]
}

#[test]
fn while_accumulating_loop() {
    run_case("while_sum", i32_val(15)); // sum 1..=5
}

#[test]
fn while_countdown_loop() {
    run_case("while_countdown", i32_val(30)); // 3 iterations × 10
}

#[test]
fn for_over_range_command() {
    run_case("for_range_cmd", i32_val(6)); // for i in [range 1 4]: 1+2+3
}

#[test]
fn for_over_range_sum() {
    run_case("for_sum", i32_val(10)); // for i in [range 0 5]: 0+1+2+3+4
}

#[test]
fn for_over_dotdot_range() {
    run_case("for_dotdot", i32_val(10)); // for i in 0..5: 0+1+2+3+4
}

#[test]
fn for_with_nested_if() {
    run_case("for_with_if", i32_val(7)); // sum i in 0..5 where i > 2: 3+4
}

// ---- P2.5 procs & calls ----

#[test]
fn proc_simple_call() {
    run_case("proc_add", i32_val(42)); // proc add {x,y}; [add 40 2]
}

#[test]
fn proc_typed_params_and_return() {
    run_case("proc_typed", i32_val(42)); // proc add {i32 x, i32 y} i32
}

#[test]
fn proc_zero_args() {
    run_case("proc_zero_arg", i32_val(42)); // [answer]
}

#[test]
fn proc_calls_another_proc() {
    run_case("proc_calls_proc", i32_val(42)); // [dbl [inc 20]]
}

#[test]
fn proc_recursive_factorial() {
    run_case("proc_recursion", i32_val(120)); // [fac 5]
}

#[test]
fn proc_explicit_return() {
    run_case("proc_return", i32_val(42)); // proc f {n} {return [+ $n 1]}; [f 41]
}

#[test]
fn proc_with_local_loop() {
    run_case("proc_local_loop", i32_val(15)); // proc sumto {n} { while … }; [sumto 5]
}

#[test]
fn proc_arity_mismatch_is_an_error() {
    let err = emit_expecting_error("proc_arity_err"); // [add 1] for a 2-arg proc
    assert!(err.contains("arity"), "unexpected error: {err}");
}

#[test]
fn proc_tail_recursion() {
    run_case("proc_tail_rec", i32_val(5)); // count 5 0 -> 5
}

#[test]
fn proc_tail_recursion_is_constant_stack() {
    // 200k tail calls: must not overflow the native stack — proves return_call is a
    // real tail call on both backends.
    run_case("proc_tail_deep", i32_val(200_000));
}

// ---- P2.6 closures ----

#[test]
fn closure_basic_lambda() {
    run_case("closure_basic", i32_val(42)); // def f [\ {x} {[+ $x 1]}]; [$f 41]
}

#[test]
fn closure_captures_outer_binding() {
    run_case("closure_capture", i32_val(42)); // captures y=10; [$f 32]
}

#[test]
fn closure_anonymous_proc() {
    run_case("closure_anon_proc", i32_val(42)); // def f [proc {x} {[* $x 2]}]; [$f 21]
}

#[test]
fn closure_multiple_captures() {
    run_case("closure_multi_capture", i32_val(122)); // (100+20)+2
}

#[test]
fn closure_returned_from_proc_captures_param() {
    run_case("closure_returned_from_proc", i32_val(42)); // adder(5) then add5(37)
}

#[test]
fn closure_nested_transitive_capture() {
    run_case("closure_nested", i32_val(42)); // (1+10)+31, inner captures a and x
}
