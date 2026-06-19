//! Phase 1 runtime test harness.
//!
//! Compiles a C driver from `runtime/tests/` — which `#include`s the JACL runtime
//! (`runtime/jaclrt.h` and, once it exists, the unity `jaclrt.c`) — with
//! `clang -O2 -emit-llvm`, translates the bitcode via `svm-llvm`, verifies, and runs
//! the driver's `run(int)` (module function 0) on **both** the interpreter and the
//! Cranelift JIT, asserting they agree. Returns the i32 result.
//!
//! Whole-program compilation (driver + runtime in one TU) is used deliberately for
//! Phase 1: it sidesteps the separate-artifact symbol-linking question (svm-llvm
//! does not yet expose a name→index export map — a Phase-2 prerequisite) while
//! still building and testing the real runtime code. Separate-artifact linking is
//! proven independently by Spike-1 (`spikes/svm_emit_link`).

use std::path::PathBuf;
use std::process::Command;
use svm_interp::Value;
use svm_ir::ValType;
use svm_jit::JitOutcome;

const RUNTIME_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/..");

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

/// Translate the runtime **unity TU** (`runtime/jaclrt.c`, no test driver) on its
/// own — the separately-compiled runtime artifact. The returned `exports` name each
/// `jacl_*` function with its module index, so a program module can resolve a
/// `call.import "jacl_*"` against it through `svm_ir::link` (the separate-artifact
/// path: compile the runtime once, link many programs against it). This is the
/// in-process analogue of `runtime/build.sh`'s `clang … | svm-llvm-translate`.
pub fn translate_runtime() -> svm_llvm::Translated {
    let bc = compile_driver(&format!("{RUNTIME_DIR}/jaclrt.c"));
    let mut t = svm_llvm::translate_bc_path(&bc).expect("svm-llvm: translate runtime");
    // The runtime references `write` (jacl_print) → a §7 capability import. Lower it to a
    // cap.call here so the linked program module verifies (the runtime defines all jacl_*, so
    // its only import is the cap; resolving leaves the jacl_* exports intact). A program that
    // never calls print never executes it; one that does is run via `run_powerbox`.
    t.module = svm_run::resolve_capability_imports(t.module).expect("resolve runtime cap imports");
    t
}

/// Translate the runtime with a custom `JACL_HEAP_BYTES` (a small heap makes the
/// collect-on-pressure path fire after a modest number of allocations).
pub fn translate_runtime_heap(heap_bytes: u32) -> svm_llvm::Translated {
    let def = format!("JACL_HEAP_BYTES={heap_bytes}u");
    let bc = compile_driver_defs(&format!("{RUNTIME_DIR}/jaclrt.c"), &[&def]);
    let mut t = svm_llvm::translate_bc_path(&bc).expect("svm-llvm: translate runtime (small heap)");
    t.module = svm_run::resolve_capability_imports(t.module).expect("resolve runtime cap imports");
    t
}

/// Compile `runtime/tests/<driver>`, translate via svm-llvm, verify, and run
/// `run(n)` (func 0) on interp + JIT. Asserts the two backends agree; returns the
/// i32 result.
pub fn run_test(driver: &str, n: i32) -> i32 {
    let driver_abs = format!("{RUNTIME_DIR}/tests/{driver}");
    let bc = compile_driver(&driver_abs);

    let t = svm_llvm::translate_bc_path(&bc).expect("svm-llvm: translate bitcode");
    // The runtime now references `write` (jacl_print), so even non-printing programs carry a
    // §7 capability import; lower it to a (here dead) cap.call before verify. A program that
    // never calls print never executes it. Programs that DO print are powerbox programs — run
    // them via `run_powerbox`, not this path.
    let module = svm_run::resolve_capability_imports(t.module).expect("resolve capability imports");
    svm_verify::verify_module(&module).expect("verify translated IR");

    let results = module.funcs[0].results.clone();
    let full = vec![Value::I64(t.entry_sp as i64), Value::I32(n)];

    let mut fuel = 2_000_000_000u64;
    let interp = svm_interp::run(&module, 0, &full, &mut fuel).expect("interp run");
    let iv = match interp[0] {
        Value::I32(x) => x,
        other => panic!("unexpected interp value {other:?}"),
    };

    let slots: Vec<i64> = full.iter().map(|v| match v {
        Value::I64(x) => *x,
        Value::I32(x) => *x as i64,
        other => panic!("unsupported arg {other:?}"),
    }).collect();
    let jv = match svm_jit::compile_and_run(&module, 0, &slots).expect("jit run") {
        JitOutcome::Returned(s) => match results[0] {
            ValType::I32 => s[0] as i32,
            other => panic!("unexpected result type {other:?}"),
        },
        other => panic!("jit did not return: {other:?}"),
    };

    assert_eq!(iv, jv, "interp ({iv}) != jit ({jv}) for {driver} run({n})");
    iv
}

/// Run a **powerbox** program (a host-I/O program whose entry is the svm-llvm synthesized
/// `_start`, function 0, taking the granted capability handles) on interp + JIT under an
/// identical granted powerbox, assert they agree on captured stdout, and return it. The
/// granted handle prefix is sized to `_start`'s arity in the standard VM_CAP order
/// (stdout, stdin, exit, memory, addrspace, ioring, blocking, jit). This is the analogue
/// of svm's own `run_c_full` for JACL host-I/O programs.
pub fn run_powerbox(driver: &str) -> Vec<u8> {
    use svm_interp::{run_with_host, Host, StreamRole};
    let driver_abs = format!("{RUNTIME_DIR}/tests/{driver}");
    let bc = compile_driver(&driver_abs);
    let t = svm_llvm::translate_bc_path(&bc).expect("svm-llvm: translate bitcode");
    // Lower the §7 named capability imports (write/read/exit/…) to concrete cap.calls under the
    // reference host policy, before verify — the verifier rejects unresolved imports (fail-closed).
    let module = svm_run::resolve_capability_imports(t.module).expect("resolve capability imports");
    svm_verify::verify_module(&module).expect("verify translated IR");

    let n = module.funcs[0].params.len(); // _start handle count (sized to the caps used)
    let win = module.memory.map_or(0, |mc| 1u64 << mc.size_log2);
    let grant = |h: &mut Host| -> Vec<i64> {
        if n > 3 {
            h.set_region_factory(svm_run::new_shared_region);
            h.set_jit_validator(svm_run::jit_blob_validator);
        }
        (0..n)
            .map(|i| {
                let handle = match i {
                    0 => h.grant_stream(StreamRole::Out),
                    1 => h.grant_stream(StreamRole::In),
                    2 => h.grant_exit(),
                    3 => h.grant_memory(),
                    4 => h.grant_address_space(0, win),
                    5 => h.grant_io_ring(),
                    6 => h.grant_blocking(std::time::Duration::ZERO, None),
                    7 => h.grant_jit((win != 0).then(|| win.trailing_zeros() as u8)),
                    _ => panic!("powerbox: unexpected handle count {n}"),
                };
                handle as i64
            })
            .collect()
    };

    // interp
    let mut hi = Host::new();
    let iargs: Vec<Value> = grant(&mut hi).into_iter().map(|h| Value::I32(h as i32)).collect();
    let mut fuel = 2_000_000_000u64;
    match run_with_host(&module, 0, &iargs, &mut fuel, &mut hi) {
        Ok(_) | Err(svm_interp::Trap::Exit(_)) => {}
        Err(e) => panic!("interp run_with_host trapped: {e:?}"),
    }

    // jit
    let mut hj = Host::new();
    let jargs = grant(&mut hj);
    match svm_jit::compile_and_run_with_host(
        &module,
        0,
        &jargs,
        svm_run::cap_thunk,
        &mut hj as *mut Host as *mut core::ffi::c_void,
    ) {
        Ok(JitOutcome::Returned(_)) | Ok(JitOutcome::Exited(_)) => {}
        other => panic!("jit compile_and_run_with_host: {other:?}"),
    }

    assert_eq!(hi.stdout, hj.stdout, "interp/jit stdout differ for {driver}");
    hi.stdout
}
