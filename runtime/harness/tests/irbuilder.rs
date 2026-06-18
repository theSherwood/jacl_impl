//! P2.1 — IR builder round-trip proof.
//!
//! Compiles the JACL-owned IR builder (`codegen/irbuilder.c`) plus a demo emitter
//! (`codegen/tests/emit_demo.c`) natively, runs it to get the svm-text for each
//! module, then parses → verifies → runs on interp + JIT — "Spike-1, but via the
//! builder." Covers literals/arithmetic, an SSA loop with block args (block-local
//! SSA), an intra-module direct call, a load/store memory round-trip, and a
//! `call.import` linked against the real runtime artifact (reusing the P2.0 path).
//!
//! For import-free modules it also asserts the builder's output is already the
//! *canonical* svm-text (`text == print_module(parse_module(text))`).

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use jacl_runtime_harness::translate_runtime;
use svm_interp::Value;
use svm_ir::{link, LinkUnit, Module, ValType};

const CODEGEN_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../codegen");

/// Build the demo emitter once per test process.
fn emitter() -> &'static Path {
    static BIN: OnceLock<PathBuf> = OnceLock::new();
    BIN.get_or_init(|| {
        let out = std::env::temp_dir().join(format!("jacl_emit_demo_{}", std::process::id()));
        let status = Command::new("clang")
            .args(["-O1", "-Wall", "-Wextra", "-Werror", "-o"])
            .arg(&out)
            .arg(format!("{CODEGEN_DIR}/irbuilder.c"))
            .arg(format!("{CODEGEN_DIR}/tests/emit_demo.c"))
            .status()
            .expect("spawn clang (is clang installed?)");
        assert!(status.success(), "clang failed to build the IR-builder demo");
        out
    })
    .as_path()
}

/// Emit one module's svm-text via the builder.
fn emit(module: &str) -> String {
    let out = Command::new(emitter())
        .arg(module)
        .output()
        .expect("run emit_demo");
    assert!(
        out.status.success(),
        "emit_demo {module} failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    String::from_utf8(out.stdout).expect("emit_demo output is UTF-8")
}

/// Run `entry` on interp + JIT, assert both equal `want` (normalized to the entry's
/// result type), and that the two backends agree.
fn check(module: &Module, entry: u32, args: &[Value], want: i64) {
    let rty = module.funcs[entry as usize].results[0];

    let mut fuel = 2_000_000_000u64;
    let interp = svm_interp::run(module, entry, args, &mut fuel).expect("interp run");
    let iv = match interp[0] {
        Value::I32(x) => x as u32 as i64,
        Value::I64(x) => x,
        ref other => panic!("unexpected interp value {other:?}"),
    };

    let slots: Vec<i64> = args
        .iter()
        .map(|v| match v {
            Value::I32(x) => *x as i64,
            Value::I64(x) => *x,
            other => panic!("unsupported arg {other:?}"),
        })
        .collect();
    let raw = match svm_jit::compile_and_run(module, entry, &slots).expect("jit run") {
        svm_jit::JitOutcome::Returned(s) => s[0],
        other => panic!("jit did not return: {other:?}"),
    };
    let jv = match rty {
        ValType::I32 => raw as u32 as i64,
        ValType::I64 => raw,
        other => panic!("unhandled result type {other:?}"),
    };

    let want_norm = match rty {
        ValType::I32 => want as u32 as i64,
        _ => want,
    };
    assert_eq!(iv, want_norm, "interp entry {entry}");
    assert_eq!(jv, want_norm, "jit entry {entry}");
}

/// Parse + verify a self-contained (import-free) module, asserting the builder
/// emitted canonical svm-text.
fn parse_self_contained(module: &str) -> Module {
    let text = emit(module);
    let m = svm_text::parse_module(&text).unwrap_or_else(|e| panic!("parse {module}: {e:?}"));
    assert_eq!(
        text,
        svm_text::print_module(&m),
        "builder output for {module} is not canonical svm-text"
    );
    svm_verify::verify_module(&m).unwrap_or_else(|e| panic!("verify {module}: {e:?}"));
    m
}

#[test]
fn add2_arithmetic() {
    let m = parse_self_contained("add2");
    check(&m, 0, &[Value::I32(40), Value::I32(2)], 42);
    check(&m, 0, &[Value::I32(-5), Value::I32(8)], 3);
}

#[test]
fn sum_to_loop_with_block_args() {
    let m = parse_self_contained("sum_to");
    check(&m, 0, &[Value::I32(5)], 15);
    check(&m, 0, &[Value::I32(10)], 55);
    check(&m, 0, &[Value::I32(100)], 5050);
}

#[test]
fn direct_intra_module_call() {
    let m = parse_self_contained("call_addone");
    // func 1 (call_addone) calls func 0 (addone).
    check(&m, 1, &[Value::I32(41)], 42);
}

#[test]
fn memory_load_store_round_trip() {
    let m = parse_self_contained("mem_roundtrip");
    check(&m, 0, &[Value::I64(64), Value::I64(0x1234_5678)], 0x1234_5678);
}

#[test]
fn closure_via_call_indirect() {
    // A hand-built closure (ref.func + closure object + call_indirect), linked against
    // the runtime: a closure over `+upval(10)` applied to 32 -> 42.
    let program = svm_text::parse_module(&emit("closure")).expect("parse closure");
    let rt = translate_runtime();
    let entry_sp = rt.entry_sp;
    let entry = rt.module.funcs.len() as u32;
    let linked = link(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit { module: program, ..Default::default() },
    ])
    .expect("link closure program");
    assert!(linked.imports.is_empty(), "unresolved: {:?}", linked.imports);
    svm_verify::verify_module(&linked).expect("verify linked closure");

    let i32_val = |x: i32| ((0x02u64 << 56) | (x as u32 as u64)) as i64;
    let args = [Value::I64(entry_sp as i64)];
    let mut fuel = 100_000_000u64;
    let interp = svm_interp::run(&linked, entry, &args, &mut fuel).expect("interp");
    assert_eq!(match interp[0] { Value::I64(x) => x, _ => panic!() }, i32_val(42), "interp closure");
    let jv = match svm_jit::compile_and_run(&linked, entry, &[entry_sp as i64]).expect("jit") {
        svm_jit::JitOutcome::Returned(s) => s[0],
        o => panic!("jit: {o:?}"),
    };
    assert_eq!(jv, i32_val(42), "jit closure");
}

#[test]
fn gc_keeps_live_root_across_collect() {
    // Build a live 3-element vector, leak some unreachable junk vectors (in a helper
    // whose frame is gone on return), then jacl_gc_collect. The live root must survive
    // — gc.roots conservatively finds it across the collect, so its nodes aren't swept
    // and length reads 3. Validates GC root discipline for codegen-shaped IR.
    let program = svm_text::parse_module(&emit("gc")).expect("parse gc");
    let rt = translate_runtime();
    let entry_sp = rt.entry_sp;
    let entry = rt.module.funcs.len() as u32;
    let linked = link(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit { module: program, ..Default::default() },
    ])
    .expect("link gc");
    svm_verify::verify_module(&linked).expect("verify gc");

    let want = ((0x02u64 << 56) | 3) as i64; // jaclval_i32(3)
    let args = [Value::I64(entry_sp as i64)];
    let mut fuel = 200_000_000u64;
    let interp = svm_interp::run(&linked, entry, &args, &mut fuel).expect("interp gc");
    assert_eq!(match interp[0] { Value::I64(x) => x, ref o => panic!("{o:?}") }, want, "interp gc");
    let jv = match svm_jit::compile_and_run(&linked, entry, &[entry_sp as i64]).expect("jit gc") {
        svm_jit::JitOutcome::Returned(s) => s[0],
        o => panic!("jit gc: {o:?}"),
    };
    assert_eq!(jv, want, "jit gc");
}

#[test]
fn call_import_linked_against_runtime() {
    // The builder emits a program calling jacl_add via call.import; link it against
    // the separately-translated runtime (P2.0 path) and run.
    let text = emit("call_jacl_add");
    let program = svm_text::parse_module(&text).expect("parse call_jacl_add");

    let rt = translate_runtime();
    let entry_sp = rt.entry_sp;
    let rt_func_count = rt.module.funcs.len() as u32;
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
    .expect("link builder program against runtime");
    assert!(linked.imports.is_empty(), "unresolved: {:?}", linked.imports);
    svm_verify::verify_module(&linked).expect("verify linked");

    // jacl_add(40, 2) == 42, as i32 JaclVals (tag 0x02 in the high byte).
    let i32_val = |x: i32| ((0x02u64 << 56) | (x as u32 as u64)) as i64;
    check(
        &linked,
        rt_func_count,
        &[
            Value::I64(entry_sp as i64),
            Value::I64(i32_val(40)),
            Value::I64(i32_val(2)),
        ],
        i32_val(42),
    );
}
