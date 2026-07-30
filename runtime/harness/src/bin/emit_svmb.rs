//! emit_svmb — spike tool (docs/SVM_BROWSER_PLAN.md).
//!
//! Runs the JACL frontend+codegen → links against the translated runtime + extern
//! catalog → powerbox `_start` → **encodes** the module to a `.svmb` byte image
//! (instead of running it, as `jacl_svm run` does). The image is what the browser
//! `svm-browser` cdylib's `svm_run_pb` decodes and runs.
//!
//!   emit_svmb <prog.jacl> <out.svmb>
//!
//! The pipeline mirrors `jacl_svm.rs` exactly up to `synth_manifest_start`.

use std::path::PathBuf;
use std::process::Command;

use svm_ir::LinkUnit;

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

fn build_driver() -> PathBuf {
    let out = std::env::temp_dir().join(format!("emit_svmb_driver_{}", std::process::id()));
    let status = Command::new("gcc")
        .args(["-O0", "-w", "-D_DEFAULT_SOURCE"])
        .arg(format!("{ROOT}/codegen/tests/emit_jacl.c"))
        .arg(format!("{ROOT}/codegen/codegen.c"))
        .arg(format!("{ROOT}/codegen/irbuilder.c"))
        .arg("-o")
        .arg(&out)
        .args(["-lm", "-lpthread"])
        .status()
        .expect("spawn gcc");
    assert!(status.success(), "gcc failed to build the codegen driver");
    out
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.len() != 2 {
        eprintln!("usage: emit_svmb <prog.jacl> <out.svmb>");
        std::process::exit(2);
    }
    let (prog, out_path) = (PathBuf::from(&args[0]), PathBuf::from(&args[1]));

    let driver = build_driver();
    let out = Command::new(&driver).arg("--file").arg(&prog).output().expect("run emit driver");
    if !out.status.success() {
        eprint!("{}", String::from_utf8_lossy(&out.stderr));
        std::process::exit(1);
    }
    let program =
        jacl_runtime_harness::decode_emitted(&out.stdout).expect("decode emitted IR");

    eprintln!("emit_svmb: translating runtime + catalog…");
    let rt = jacl_runtime_harness::translate_runtime();
    let cat = jacl_runtime_harness::translate_catalog();

    let linked = svm_ir::link_with_manifest(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit { module: cat.module, exports: cat.exports, ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![("__jacl_entry".to_string(), 0)],
            ..Default::default()
        },
    ])
    .expect("link");
    let entry = linked.resolve_export("__jacl_entry").expect("entry export missing after link");
    let module = svm_ir::synth_manifest_start(linked, entry, false).expect("powerbox");

    let bytes = svm_encode::encode_module(&module);
    std::fs::write(&out_path, &bytes).expect("write .svmb");
    eprintln!("emit_svmb: wrote {} ({} bytes)", out_path.display(), bytes.len());
}
