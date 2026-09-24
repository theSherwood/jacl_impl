//! Phase 1 runtime test harness.
//!
//! Compiles a C driver from `runtime/tests/` — which `#include`s the JACL runtime
//! (`runtime/jaclrt.h` and, once it exists, the unity `jaclrt.c`) — with
//! `clang -O2 -emit-llvm`, translates the bitcode via `temen-llvm`, verifies, and runs
//! the driver's `run(int)` (module function 0) on **both** the interpreter and the
//! Cranelift JIT, asserting they agree. Returns the i32 result.
//!
//! Whole-program compilation (driver + runtime in one TU) is used deliberately for
//! Phase 1: it sidesteps the separate-artifact symbol-linking question (temen-llvm
//! does not yet expose a name→index export map — a Phase-2 prerequisite) while
//! still building and testing the real runtime code. Separate-artifact linking is
//! proven independently by Spike-1 (`spikes/temen_emit_link`).

use std::path::PathBuf;
use std::process::Command;
use temen_interp::Value;
use temen_ir::ValType;
use temen_jit::JitOutcome;

/// C-ABI bridge (`jacl_temen_stage`) that runs a codegen'd staged-macro module on TEMEN
/// in-process, for the native frontend's `JACL_STAGE_ON_TEMEN` path.
pub mod stage_ffi;

const RUNTIME_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/..");

/// Decode the emit driver's default output — a v9 temen-encode **object** (`irb_to_encoded`).
/// The object carries its own `data.self` link-form data addresses and unresolved `call.sym`s,
/// which `link` resolves, so there is no separate relocation table: the returned `Module` is a
/// pre-link unit, fed to `link`/`link_with_manifest` as a `LinkUnit`. Objects decode via
/// `decode_unit` (`decode_module` rejects them). (`emit_jacl --text` still emits temen-text for
/// goldens; that path is consumed by shell diffs, not this decoder.)
pub fn decode_emitted(bytes: &[u8]) -> Result<temen_ir::Module, String> {
    temen_encode::decode_unit(bytes).map_err(|e| format!("decode object: {e:?}"))
}

fn compile_driver(driver_abs: &str) -> PathBuf {
    compile_driver_defs(driver_abs, &[])
}

/// Like `compile_driver`, but with extra `-D` defines (e.g. a smaller `JACL_HEAP_BYTES`
/// so a test exercises the collect-on-pressure path without filling 16 MiB).
fn compile_driver_defs(driver_abs: &str, defines: &[&str]) -> PathBuf {
    let dir = std::env::temp_dir();
    let stem = std::path::Path::new(driver_abs)
        .file_stem().and_then(|s| s.to_str()).unwrap_or("driver");
    let bc = dir.join(format!("jaclrt_{}_{}_{}.bc", stem, defines.len(), std::process::id()));
    let mut cmd = Command::new("clang");
    cmd.args(["-O2", "-emit-llvm", "-c", "-fno-vectorize", "-fno-slp-vectorize", "-DNDEBUG"]);
    for d in defines {
        cmd.arg(format!("-D{d}"));
    }
    let status = cmd
        .arg("-I").arg(RUNTIME_DIR)
        .arg(driver_abs)
        .arg("-o").arg(&bc)
        .status()
        .expect("spawn clang (is clang installed?)");
    assert!(status.success(), "clang failed to compile {driver_abs}");
    bc
}

/// The runtime unity TU built as `runtime/build.sh` builds it: with `JACL_UNIR`, so channels
/// run on Unir edges, and llvm-linked with the vendored unir unit (`runtime/unir/unir_cabi.ll`,
/// docs/UNIR_CHANNELS.md). The unit is rustc's LLVM 21 IR, so this needs `llvm-link` from LLVM 21
/// or later (`LLVM_LINK`, default `llvm-link-21`). Returns the linked textual IR.
fn compile_runtime_with_unir() -> PathBuf {
    let dir = std::env::temp_dir();
    let tag = format!("{}_{:?}", std::process::id(), std::thread::current().id());
    let tag: String = tag.chars().filter(|c| c.is_ascii_alphanumeric() || *c == '_').collect();
    let c_ll = dir.join(format!("jaclrt_unir_c_{tag}.ll"));
    let linked = dir.join(format!("jaclrt_unir_{tag}.ll"));
    let status = Command::new("clang")
        .args(["-O2", "-S", "-emit-llvm", "-fno-vectorize", "-fno-slp-vectorize", "-DNDEBUG"])
        .args(["-DJACL_UNIR", "--target=x86_64-unknown-linux-gnu"])
        .arg("-I").arg(RUNTIME_DIR)
        .arg(format!("{RUNTIME_DIR}/jaclrt.c"))
        .arg("-o").arg(&c_ll)
        .status()
        .expect("spawn clang (is clang installed?)");
    assert!(status.success(), "clang failed to compile the runtime");
    let llvm_link = std::env::var("LLVM_LINK").unwrap_or_else(|_| "llvm-link-21".into());
    let status = Command::new(&llvm_link)
        .arg("-S")
        .arg(&c_ll)
        .arg(format!("{RUNTIME_DIR}/unir/unir_cabi.ll"))
        .arg("-o").arg(&linked)
        .status()
        .unwrap_or_else(|e| panic!("spawn {llvm_link} (LLVM 21+ llvm-link; set LLVM_LINK): {e}"));
    assert!(status.success(), "{llvm_link} failed to link the unir unit");
    linked
}

/// How the runtime is translated: a library that a program link makes a powerbox program, so its
/// globals must clear the argument area its host seeds, where a pipeline stage's arguments arrive
/// (docs/UNIR_PIPELINES.md). runtime/build.sh passes the same `--powerbox-layout`.
fn runtime_options() -> temen_llvm::TranslateOptions {
    temen_llvm::TranslateOptions { powerbox_layout: true, ..Default::default() }
}

/// Translate the runtime **unity TU** (`runtime/jaclrt.c`, no test driver) on its
/// own — the separately-compiled runtime artifact. The returned `exports` name each
/// `jacl_*` function with its module index, so a program module can resolve a
/// `call.import "jacl_*"` against it through `temen_ir::link` (the separate-artifact
/// path: compile the runtime once, link many programs against it). This is the
/// in-process analogue of `runtime/build.sh`'s `clang … | temen-llvm-translate`.
pub fn translate_runtime() -> temen_llvm::Translated {
    let ll = compile_runtime_with_unir();
    temen_llvm::translate_ll_path_with_options(&ll, runtime_options())
        .expect("temen-llvm: translate runtime")
    // The runtime keeps its `write` (jacl_print) capability import as a manifest slot: it links
    // through `temen_ir::link_with_manifest` (which retains an import no unit exports), and the host
    // binds `write` at `instantiate_with_imports` time. (Pre-refresh this was lowered to a cap.call
    // via the now-deleted `resolve_capability_imports`; the manifest path is the phase-4 model.)
}

/// Translate the runtime **plus the staged-macro I/O glue** (`syn_rt` + `synrt_read_arg` /
/// `synrt_write_result`) as one module, so a codegen'd staged-macro program resolves those
/// (and `jacl_*`) by name at link. The glue's `jacl_vec_*` calls are in-unit; only
/// `read`/`write` cross the boundary (the recognized Stream caps). See
/// `docs/TEMEN_MACRO_STAGING_PLAN.md` "final link".
pub fn translate_runtime_staging() -> temen_llvm::Translated {
    let unity = format!("{RUNTIME_DIR}/../codegen/selfhost/macro_staging/jaclrt_staging.c");
    let bc = compile_driver(&unity);
    temen_llvm::translate_bc_path(&bc).expect("temen-llvm: translate staging runtime")
}

/// Translate the test-only native `extern` catalog (`extern_catalog.c`) as its own
/// module. Its exports name `t_sumi` / `t_fill` / `t_xor` so a program module's
/// `call.import "t_*"` (emitted for an `extern` call) resolves against it through
/// `temen_ir::link`. The catalog is pure (no capability imports); it takes raw
/// linear-memory addresses and dereferences them with C semantics — the fidelity
/// point of the flat-buffer decay path (see docs/TEMEN_BUFFERS.md).
pub fn translate_catalog() -> temen_llvm::Translated {
    let bc = compile_driver(&format!("{}/extern_catalog.c", env!("CARGO_MANIFEST_DIR")));
    temen_llvm::translate_bc_path(&bc).expect("temen-llvm: translate extern catalog")
}

/// Translate the runtime with a custom `JACL_HEAP_BYTES` (a small heap makes the
/// collect-on-pressure path fire after a modest number of allocations).
pub fn translate_runtime_heap(heap_bytes: u32) -> temen_llvm::Translated {
    let def = format!("JACL_HEAP_BYTES={heap_bytes}u");
    let bc = compile_driver_defs(&format!("{RUNTIME_DIR}/jaclrt.c"), &[&def]);
    temen_llvm::translate_bc_path(&bc).expect("temen-llvm: translate runtime (small heap)")
}

/// Compile `runtime/tests/<driver>`, translate via temen-llvm, verify, and run
/// `run(n)` (func 0) on interp + JIT. Asserts the two backends agree; returns the
/// i32 result.
pub fn run_test(driver: &str, n: i32) -> i32 {
    let driver_abs = format!("{RUNTIME_DIR}/tests/{driver}");
    let bc = compile_driver(&driver_abs);

    let t = temen_llvm::translate_bc_path(&bc).expect("temen-llvm: translate bitcode");
    // A non-printing runtime test carries the `write` capability import but never calls it, so the
    // module runs func 0 directly with the import declared-but-unbound (dead). Programs that DO
    // print are powerbox programs — run them via `run_powerbox`.
    let module = t.module;
    temen_verify::verify_module(&module).expect("verify translated IR");

    let results = module.funcs[0].results.clone();
    let full = vec![Value::I64(t.entry_sp as i64), Value::I32(n)];

    let mut fuel = 2_000_000_000u64;
    let interp = temen_interp::run(&module, 0, &full, &mut fuel).expect("interp run");
    let iv = match interp[0] {
        Value::I32(x) => x,
        other => panic!("unexpected interp value {other:?}"),
    };

    let slots: Vec<i64> = full.iter().map(|v| match v {
        Value::I64(x) => *x,
        Value::I32(x) => *x as i64,
        other => panic!("unsupported arg {other:?}"),
    }).collect();
    let jv = match temen_jit::compile_and_run(&module, 0, &slots).expect("jit run") {
        JitOutcome::Returned(s) => match results[0] {
            ValType::I32 => s[0] as i32,
            other => panic!("unexpected result type {other:?}"),
        },
        other => panic!("jit did not return: {other:?}"),
    };

    assert_eq!(iv, jv, "interp ({iv}) != jit ({jv}) for {driver} run({n})");
    iv
}

/// Like `run_test`, but for a driver whose **point is that it traps**: returns each backend's
/// outcome as a short string, `("Returned(n)" | "Trap(T)", …)` for (interp, JIT). A trap terminates
/// the run and cannot be observed from inside the guest, so this is the only place a "this must
/// fault" rule can be asserted (jacl #141).
pub fn run_test_outcome(driver: &str, n: i32) -> (String, String) {
    let driver_abs = format!("{RUNTIME_DIR}/tests/{driver}");
    let bc = compile_driver(&driver_abs);
    let t = temen_llvm::translate_bc_path(&bc).expect("temen-llvm: translate bitcode");
    let module = t.module;
    temen_verify::verify_module(&module).expect("verify translated IR");
    let full = vec![Value::I64(t.entry_sp as i64), Value::I32(n)];

    let mut fuel = 2_000_000_000u64;
    let interp = match temen_interp::run(&module, 0, &full, &mut fuel) {
        Ok(v) => match v.first() {
            Some(Value::I32(x)) => format!("Returned({x})"),
            other => format!("Returned({other:?})"),
        },
        Err(t) => format!("Trap({t:?})"),
    };

    let slots = vec![t.entry_sp as i64, n as i64];
    let jit = match temen_jit::compile_and_run(&module, 0, &slots).expect("jit compile") {
        JitOutcome::Returned(s) => format!("Returned({})", s[0] as i32),
        JitOutcome::Trapped(t) => format!("Trap({t:?})"),
        other => format!("{other:?}"),
    };
    (interp, jit)
}


/// Run a **powerbox** program (a host-I/O program whose entry is the synthesized `_start`)
/// through temen's frontend-independent embedding API (`instantiate` + `Instance::call`),
/// which resolves the §7 capability imports, grants the fixed powerbox, runs the `_start`
/// entry on interp **and** JIT (enforcing they agree), and captures stdout. Returns the
/// captured stdout bytes.
pub fn run_powerbox(driver: &str) -> Vec<u8> {
    let driver_abs = format!("{RUNTIME_DIR}/tests/{driver}");
    let bc = compile_driver(&driver_abs);
    let t = temen_llvm::translate_bc_path(&bc).expect("temen-llvm: translate bitcode");
    let inst = temen_run::instantiate(t.module).expect("instantiate powerbox module");
    // run_diff runs the powerbox entry (func 0) on interp AND jit, enforcing they agree, and
    // grants the fixed §3e powerbox. (call("_start") would need the entry exported by name;
    // temen-llvm's C output reaches it as the implicit func-0 entry instead.)
    let run = inst.run_diff(&temen_run::RunConfig::default()).expect("run powerbox entry (interp==jit)");
    run.stdout
}
