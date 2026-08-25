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

use temen_ir::LinkUnit;

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
    // Two input shapes:
    //   emit_svmb <prog.jacl> <out.svmb>   — compile via the emit-only C driver (no macros), or
    //   emit_svmb --ir <prog.ir> <out.svmb> — link + encode SVM-IR produced elsewhere. The `--ir`
    // form is how the macro-bearing tour is baked: the self-hosted guest (which CAN stage macros)
    // emits the IR, and this path links it against the runtime + powerbox exactly like the C path.
    let (program, out_path) = match args.as_slice() {
        // `--ir` takes SVM-IR **text** (what the self-hosted guest emits on stdout — the macro-capable
        // path), parsed the same way `svm_link_run` parses the playground's live compile.
        [flag, ir_path, out] if flag == "--ir" => {
            let text = std::fs::read_to_string(ir_path).expect("read --ir file");
            let m = temen_text::parse_module(&text)
                .unwrap_or_else(|e| panic!("parse --ir text: {e:?}"));
            (m, PathBuf::from(out))
        }
        // The default path runs the emit-only C driver (binary object → `decode_emitted`); no macros.
        [prog, out] => {
            let driver = build_driver();
            let res = Command::new(&driver)
                .arg("--file")
                .arg(PathBuf::from(prog))
                .output()
                .expect("run emit driver");
            if !res.status.success() {
                eprint!("{}", String::from_utf8_lossy(&res.stderr));
                std::process::exit(1);
            }
            (
                jacl_runtime_harness::decode_emitted(&res.stdout).expect("decode emitted IR"),
                PathBuf::from(out),
            )
        }
        _ => {
            eprintln!("usage: emit_svmb <prog.jacl> <out.svmb>  |  emit_svmb --ir <prog.ir> <out.svmb>");
            std::process::exit(2);
        }
    };

    eprintln!("emit_svmb: translating runtime + catalog…");
    let rt = jacl_runtime_harness::translate_runtime();
    let cat = jacl_runtime_harness::translate_catalog();

    let linked = temen_ir::link_with_manifest(&[
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
    let module = temen_ir::synth_manifest_start(linked, entry, false).expect("powerbox");

    let bytes = temen_encode::encode_module(&module);
    std::fs::write(&out_path, &bytes).expect("write .svmb");
    eprintln!("emit_svmb: wrote {} ({} bytes)", out_path.display(), bytes.len());
}
