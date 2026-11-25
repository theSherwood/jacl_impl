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

/// Bells a host rings when something arrives for a vat, so the vat waits rather than polls
/// (theSherwood/unir#225, #259).
pub mod bell;

/// One of two machines editing one document over TCP: unir's store vat in peer mode, its editor a
/// JACL program (`unir_node`).
pub mod node;

/// A JACL program in a terminal pane: unir's store vat in its `shell` mode (`jacl_pane`).
pub mod pane;

/// The JACL shell's REPL: the runtime with the compiler linked in (jacl_impl#204).
pub mod repl;

const RUNTIME_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/..");

/// A live terminal's tee: `tee` sees each write to the guest's stdout as it is made, and none of
/// stderr's (`Host::set_stream_tee` hands it both).
pub fn stdout_tee(mut tee: impl FnMut(&[u8]) + Send + 'static) -> temen_interp::StreamTee {
    Box::new(move |role, bytes| {
        if matches!(role, temen_interp::StreamRole::Out) {
            tee(bytes)
        }
    })
}

/// Decode the emit driver's default output — a v9 temen-encode **object** (`irb_to_encoded`).
/// The object carries its own `data.self` link-form data addresses and unresolved `call.sym`s,
/// which `link` resolves, so there is no separate relocation table: the returned `Module` is a
/// pre-link unit, fed to `link`/`link_with_manifest` as a `LinkUnit`. Objects decode via
/// `decode_unit` (`decode_module` rejects them). (`emit_jacl --text` still emits temen-text for
/// people and shell diffs; this decoder does not read it.)
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

/// The C at `src` compiled to textual IR as the runtime is compiled: with `JACL_UNIR`, so channels
/// run on Unir edges (docs/UNIR_CHANNELS.md). Returns the IR's path.
fn compile_unir_c(src: &str) -> PathBuf {
    let tag = format!("{}_{:?}", std::process::id(), std::thread::current().id());
    let tag: String = tag.chars().filter(|c| c.is_ascii_alphanumeric() || *c == '_').collect();
    let stem = std::path::Path::new(src).file_stem().and_then(|s| s.to_str()).unwrap_or("unit");
    let ll = std::env::temp_dir().join(format!("jaclrt_unir_{stem}_{tag}.ll"));
    let status = Command::new("clang")
        .args(["-O2", "-S", "-emit-llvm", "-fno-vectorize", "-fno-slp-vectorize", "-DNDEBUG"])
        .args(["-DJACL_UNIR", "--target=x86_64-unknown-linux-gnu"])
        .arg("-I").arg(RUNTIME_DIR)
        .arg(src)
        .arg("-o").arg(&ll)
        .status()
        .expect("spawn clang (is clang installed?)");
    assert!(status.success(), "clang failed to compile {src}");
    ll
}

/// The IR at `ll` translated as a link unit (theSherwood/temen#1746). Its calls into unir's C ABI
/// stay symbols, which [`link`] binds to the C ABI's unit, and the function pointers in its data
/// are recorded, so a call through one reaches the function it names (theSherwood/temen#2308).
pub fn translate_unit(ll: &std::path::Path) -> temen_llvm::Translated {
    let opts = temen_llvm::TranslateOptions { link_unit: true, ..Default::default() };
    temen_llvm::translate_ll_path_with_options(ll, opts)
        .unwrap_or_else(|e| panic!("temen-llvm: translate {} as a link unit: {e:?}", ll.display()))
}

/// unir's C ABI as unir builds it, `runtime/unir/unir_cabi.temeno` (theSherwood/unir decision
/// #126.1), laid out once as the base programs link against (`temen_ir::link_base`,
/// theSherwood/temen#2168), with its exports by name.
struct Base {
    module: temen_ir::Module,
    exports: Vec<(String, temen_ir::FuncIdx)>,
}

fn unir_base() -> &'static Base {
    static BASE: std::sync::OnceLock<Base> = std::sync::OnceLock::new();
    BASE.get_or_init(|| {
        let path = format!("{RUNTIME_DIR}/unir/unir_cabi.temeno");
        let bytes = std::fs::read(&path).unwrap_or_else(|e| panic!("{path}: {e}"));
        let module = temen_encode::decode_unit(&bytes)
            .unwrap_or_else(|e| panic!("decode {path}: {e:?}"));
        let unit = temen_ir::LinkUnit {
            exports: module.exports.iter().map(|e| (e.name.clone(), e.func)).collect(),
            data_exports: module.data_exports.clone(),
            module,
        };
        let module = temen_ir::link_base(&[temen_ir::LinkUnitRef::from(&unit)])
            .expect("unir's C ABI lays out as a base");
        let exports = module.exports.iter().map(|e| (e.name.clone(), e.func)).collect();
        Base { module, exports }
    })
}

/// `units` linked as `temen_ir::link_with_manifest` links them, after unir's C ABI when any of them
/// calls into it: the C ABI's unit is laid out first as a base ([`unir_base`]), and each of its
/// functions none of `units` can reach links as a trap ([`base_live`]), so an image carries what it
/// uses of the C ABI, not all of it (#267, theSherwood/unir decision #409.1). An import no unit
/// exports stays in the module's manifest, for the host to bind.
pub fn link(units: &[temen_ir::LinkUnit]) -> Result<temen_ir::Module, temen_ir::LinkError> {
    let base = unir_base();
    let live = base_live(base, units);
    let base = live.as_deref().map(|live| temen_ir::LinkUnitRef {
        module: &base.module,
        exports: &base.exports,
        data_exports: &base.module.data_exports,
        live: Some(live),
    });
    let refs: Vec<_> = base.into_iter().chain(units.iter().map(temen_ir::LinkUnitRef::from)).collect();
    temen_ir::link_with_manifest_ref(&refs)
}

/// Which of the base's functions `units` can reach, or `None` when they name none of its exports:
/// those they import, or name in their code or data, those the base's own data names (vtables) or
/// offers, and all those call. temen's `base_live` (`browser/src/lib.rs`) over several units, as
/// theSherwood/unir#409 copies it, until theSherwood/temen#2751 moves it into `temen_ir`.
fn base_live(base: &Base, units: &[temen_ir::LinkUnit]) -> Option<Vec<bool>> {
    let export = |name: &str| base.exports.iter().find(|(n, _)| n == name).map(|&(_, f)| f);
    let mut named = Vec::new();
    for unit in units {
        let m = &unit.module;
        let ref_syms = m.funcs.iter().flat_map(|f| &f.blocks).flat_map(|b| &b.insts).filter_map(
            |i| match i {
                temen_ir::Inst::RefSym { name } => std::str::from_utf8(name).ok(),
                _ => None,
            },
        );
        let names = m.imports.iter().map(|i| i.name.as_str())
            .chain(m.data_funcrefs.iter().map(|r| r.name.as_str()))
            .chain(ref_syms);
        named.extend(names.filter_map(export));
    }
    if named.is_empty() {
        return None;
    }
    let roots = temen_ir::data_funcref_targets(&base.module)
        .into_iter()
        .flatten()
        .chain(base.module.impl_exports.iter().flat_map(|e| e.ops.iter().copied()))
        .chain(named);
    Some(
        temen_ir::reachable_funcs(&base.module, roots)
            .unwrap_or_else(|_| vec![true; base.module.funcs.len()]),
    )
}

/// The JACL program at `path`, compiled as a child image a vat spawns (the frontend and codegen's
/// emit driver, built with gcc as `jacl_temen`'s is, linked with the translated runtime), and its
/// window's size, log2.
pub fn child_image(path: &std::path::Path) -> Result<(temen_ir::Module, u8), String> {
    link_image(emit_program(path)?, translate_runtime(), true)
}

/// The JACL program at `path`, compiled by the frontend and codegen's emit driver (built with gcc,
/// as `jacl_temen`'s is), unlinked.
pub fn emit_program(path: &std::path::Path) -> Result<temen_ir::Module, String> {
    let root = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");
    // One driver per thread: tests run in parallel in one process.
    let tag = format!("{}_{:?}", std::process::id(), std::thread::current().id());
    let tag: String = tag.chars().filter(|c| c.is_ascii_alphanumeric() || *c == '_').collect();
    let driver = std::env::temp_dir().join(format!("jacl_child_emit_{tag}"));
    let status = Command::new("gcc")
        .args(["-O0", "-w", "-D_DEFAULT_SOURCE"])
        .arg(format!("{root}/codegen/tests/emit_jacl.c"))
        .arg(format!("{root}/codegen/codegen.c"))
        .arg(format!("{root}/codegen/irbuilder.c"))
        .arg("-o")
        .arg(&driver)
        .args(["-lm", "-lpthread"])
        .status()
        .map_err(|e| format!("gcc: {e}"))?;
    if !status.success() {
        return Err("gcc failed to build the codegen driver".into());
    }
    let out = Command::new(&driver).arg("--file").arg(path).output();
    let _ = std::fs::remove_file(&driver);
    let out = out.map_err(|e| format!("emit driver: {e}"))?;
    if !out.status.success() {
        return Err(format!(
            "{}: {}",
            path.display(),
            String::from_utf8_lossy(&out.stderr)
        ));
    }
    decode_emitted(&out.stdout).map_err(|e| format!("decode emitted IR: {e}"))
}

/// The frontend and codegen's emit driver built with the macro-staging bridge, so a `defmacro`'s body
/// runs on TEMEN (run it with `JACL_STAGE_ON_TEMEN=1`). It links this crate's staticlib from
/// `profile_dir` (`target/<profile>`), which building a test or a bin does not produce, so cargo is
/// asked for it first (a no-op when it is current: checking only that the file exists once linked
/// a stale one whose decoder rejected the wire the C encoder emitted). The driver is built beside
/// the staticlib, so a run replaces the last one's rather than leaving 23 MB in the temp directory
/// each time (#198), under a name of its own and renamed into place, since several processes may
/// build it at once.
pub fn staged_driver(profile_dir: &std::path::Path) -> Result<PathBuf, String> {
    let root = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");
    let mut cargo = Command::new(env!("CARGO"));
    cargo.args(["build", "--lib"]).current_dir(env!("CARGO_MANIFEST_DIR"));
    if profile_dir.file_name().and_then(|s| s.to_str()) == Some("release") {
        cargo.arg("--release");
    }
    let st = cargo.status().map_err(|e| format!("cargo build --lib: {e}"))?;
    let staticlib = profile_dir.join("libjacl_runtime_harness.a");
    if !st.success() || !staticlib.exists() {
        return Err(format!("could not build {} for the staged driver", staticlib.display()));
    }
    let out = profile_dir.join("jacl_emit_staged");
    let tmp = profile_dir.join(format!("jacl_emit_staged.{}", std::process::id()));
    let status = Command::new("gcc")
        .args(["-DJACL_STAGE_ON_TEMEN_BUILD", "-std=gnu11", "-O1", "-w", "-D_GNU_SOURCE"])
        .arg("-I")
        .arg(format!("{root}/codegen"))
        .arg(format!("{root}/codegen/tests/emit_jacl.c"))
        .arg(format!("{root}/codegen/codegen.c"))
        .arg(format!("{root}/codegen/irbuilder.c"))
        .arg(format!("{root}/codegen/selfhost/macro_staging/stage_bridge.c"))
        .arg("-L")
        .arg(profile_dir)
        // The Rust staticlib pulls in libstd's system deps.
        .args([
            "-ljacl_runtime_harness", "-lgcc_s", "-lutil", "-lrt", "-lpthread", "-lm", "-ldl", "-lc",
        ])
        .arg("-o")
        .arg(&tmp)
        .status()
        .map_err(|e| format!("gcc: {e}"))?;
    if !status.success() {
        return Err("gcc failed to build the TEMEN-staged codegen driver".into());
    }
    std::fs::rename(&tmp, &out).map_err(|e| format!("move the staged driver into place: {e}"))?;
    Ok(out)
}

/// `program` linked with the runtime `rt` and given its entry: a child's a vat spawns (`child`), or
/// a root's; and its window's size, log2.
pub fn link_image(
    program: temen_ir::Module,
    rt: temen_llvm::Translated,
    child: bool,
) -> Result<(temen_ir::Module, u8), String> {
    let linked = link(&[
        temen_ir::LinkUnit {
            module: rt.module,
            exports: rt.exports,
            ..Default::default()
        },
        temen_ir::LinkUnit {
            module: program,
            exports: vec![("__jacl_entry".to_string(), 0)],
            ..Default::default()
        },
    ])
    .map_err(|e| format!("link: {e:?}"))?;
    let entry = linked
        .resolve_export("__jacl_entry")
        .ok_or("no entry after link")?;
    let image = if child {
        temen_ir::synth_manifest_child_start(linked, entry, false)
    } else {
        temen_ir::synth_manifest_start(linked, entry, false)
    }
    .map_err(|e| format!("entry: {e}"))?;
    let log2 = image
        .memory
        .map(|m| m.size_log2)
        .ok_or("the program has no window")?;
    Ok((image, log2))
}

/// Translate the runtime **unity TU** (`runtime/jaclrt.c`, no test driver) on its
/// own, as a link unit — the separately-compiled runtime artifact. The returned `exports` name
/// each `jacl_*` function with its module index, so a program module can resolve a
/// `call.import "jacl_*"` against it through [`link`] (the separate-artifact path: compile the
/// runtime once, link many programs against it), which also links in unir's C ABI. The runtime's
/// capability imports (`write`, …) stay in the linked module's manifest, and the host binds them
/// at `instantiate_with_imports` time.
pub fn translate_runtime() -> temen_llvm::Translated {
    translate_c_unit(&format!("{RUNTIME_DIR}/jaclrt.c"))
}

/// The C at `src` compiled as the runtime is ([`compile_unir_c`]) and translated as a link unit
/// ([`translate_unit`]).
pub fn translate_c_unit(src: &str) -> temen_llvm::Translated {
    let ll = compile_unir_c(src);
    let t = translate_unit(&ll);
    // Each is megabytes, written per call: left behind, a test suite's runs fill the disk.
    let _ = std::fs::remove_file(&ll);
    t
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
