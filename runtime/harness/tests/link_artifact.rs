//! P2.0 separate-artifact link proof.
//!
//! Spike-1 (`spikes/svm_emit_link`) proved emit → verify → link → run with a
//! *hand-authored* runtime unit. This proves the same path against the **real,
//! separately-compiled runtime**: translate `jaclrt.c` on its own, take its
//! `exports` (the svm-llvm name→index map, Phase-2 Ask 1), and link a JACL-shaped
//! program module that calls a `jacl_*` function via `call.import` — the foundation
//! Phase-2 codegen builds on (compile the runtime once, link many programs).

use jacl_runtime_harness::translate_runtime;
use svm_interp::Value;
use svm_ir::{link, LinkUnit};

/// JaclVal encoding of an i32 (tag `0x02` in the high byte, value in the low 32),
/// matching `jaclrt_i32` in `runtime/jaclrt.h`.
fn i32_val(x: i32) -> i64 {
    ((0x02u64 << 56) | (x as u32 as u64)) as i64
}

/// A program function that forwards `(sp, a, b)` to the runtime's `jacl_add`. svm-llvm
/// threads the data-stack pointer as every function's leading parameter (the §3d ABI),
/// so the runtime `jacl_add(JaclVal,JaclVal)` is `(i64 sp, i64 a, i64 b) -> i64`; the
/// program passes its own `sp` straight through. The `call.import` handle (`i32.const 0`)
/// is the text form Spike-1 used; the linker rewrites the named import into a direct call.
const PROGRAM_IR: &str = "\
func (i64, i64, i64) -> (i64) {
block0(v0: i64, v1: i64, v2: i64):
  v3 = i32.const 0
  v4 = call.import \"jacl_add\" (i64, i64, i64) -> (i64) v3 (v0, v1, v2)
  return v4
}
";

#[test]
fn program_links_against_runtime_artifact_and_calls_jacl_add() {
    // 1. The separately-compiled runtime + its export map (Ask 1).
    let rt = translate_runtime();
    assert!(
        rt.exports.iter().any(|(n, _)| n == "jacl_add"),
        "runtime must export jacl_add; got {} exports",
        rt.exports.len()
    );
    let entry_sp = rt.entry_sp;
    let rt_func_count = rt.module.funcs.len() as u32;

    // 2. Link a JACL-shaped program unit against the runtime unit (runtime first, so
    //    its indices are unchanged and the program function lands at rt_func_count).
    let program = svm_text::parse_module(PROGRAM_IR).expect("parse program module");
    let linked = link(&[
        LinkUnit {
            module: rt.module,
            exports: rt.exports,
            ..Default::default()
        },
        LinkUnit {
            module: program,
            ..Default::default()
        },
    ])
    .expect("link program against runtime");
    assert!(
        linked.imports.is_empty(),
        "all imports must resolve; unresolved: {:?}",
        linked.imports
    );

    // 3. The linked module re-verifies.
    svm_verify::verify_module(&linked).expect("verify linked module");

    // 4. Run the program entry on interp + JIT: jacl_add(40, 2) == 42 (as i32 JaclVals).
    let entry = rt_func_count;
    let (a, b) = (i32_val(40), i32_val(2));
    let want = i32_val(42);

    let mut fuel = 100_000_000u64;
    let interp = svm_interp::run(
        &linked,
        entry,
        &[Value::I64(entry_sp as i64), Value::I64(a), Value::I64(b)],
        &mut fuel,
    )
    .expect("interp run");
    let iv = match interp[0] {
        Value::I64(x) => x,
        other => panic!("unexpected interp value {other:?}"),
    };
    assert_eq!(iv, want, "interp jacl_add(40,2)");

    let jv = match svm_jit::compile_and_run(&linked, entry, &[entry_sp as i64, a, b])
        .expect("jit compile_and_run")
    {
        svm_jit::JitOutcome::Returned(s) => s[0],
        other => panic!("jit did not return: {other:?}"),
    };
    assert_eq!(jv, want, "jit jacl_add(40,2)");
}
