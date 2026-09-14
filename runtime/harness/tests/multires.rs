//! The VM accepts multi-result functions — parse, verify, interpret, JIT.
//!
//! #94 slice 2b wants a typed proc to return `(raw_value, error_flag)`: a raw word has no bit
//! for the error flag (INVARIANTS.md V6), so a second result is the natural error channel.
//! Whether that shape is even available is a property of the *VM*, not of JACL, and nothing
//! else in the tree exercises it — so this gate pins it. It currently passes while JACL still
//! emits single-result calls: `codegen/irbuilder.c`'s `irb_call` clamps `nresults` to
//! `callee->nresults == 1 ? 1 : 0`, which is the actual blocker for the slice.
use temen_interp::Value;

const SRC: &str = r#"
func 0 (i64, i64) -> (i64, i64) {
  block 0 (v0: i64, v1: i64) {
    v2 = i64.add v0 v1
    v3 = i64.const 7
    return v2, v3
  }
}
func 1 (i64, i64) -> (i64) {
  block 0 (v0: i64, v1: i64) {
    v2, v3 = call 0(v0, v1)
    v4 = i64.mul v2 v3
    return v4
  }
}
"#;

#[test]
fn vm_accepts_multi_result_functions() {
    let m = match temen_text::parse_module(SRC) {
        Ok(m) => m,
        Err(e) => panic!("PARSE REFUSED multi-result: {e:?}"),
    };
    if let Err(e) = temen_verify::verify_module(&m) {
        panic!("VERIFY REFUSED multi-result: {e:?}");
    }
    let mut fuel = 1_000_000u64;
    let interp = temen_interp::run(&m, 1, &[Value::I64(3), Value::I64(4)], &mut fuel)
        .expect("interp run");
    assert_eq!(interp[0], Value::I64(49), "interp: (3+4)*7");

    match temen_jit::compile_and_run(&m, 1, &[3i64, 4i64]) {
        Ok(temen_jit::JitOutcome::Returned(s)) => assert_eq!(s[0], 49, "jit: (3+4)*7"),
        Ok(other) => panic!("JIT did not return: {other:?}"),
        Err(e) => panic!("JIT REFUSED multi-result: {e:?}"),
    }
}
