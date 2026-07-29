//! stage_macro — run a JACL macro's body **on SVM** with a real argument (the Phase 3
//! final link of `docs/SVM_MACRO_STAGING_PLAN.md`, corrected design). Given a `.jacl` file
//! with a `defmacro`, this codegens a complete staged-macro program (entry reads the
//! argument syntax value on stdin, runs the body, writes the result on stdout), links it
//! against the staging-extended runtime, and runs it. The argument/result use the
//! `syn_wire` format — feed one with `gen_wire`, read one with `wire_to_ast`:
//!
//!   gen_wire 21 | stage_macro twice.jacl | wire_to_ast   ->  [+ 21 21]

use std::io::{Read, Write};
use std::path::PathBuf;
use std::process::Command;

use svm_ir::LinkUnit;

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

fn die(msg: &str) -> ! {
    eprintln!("stage_macro: {msg}");
    std::process::exit(1);
}

/// Build the `emit_macro_body` tool (frontend + codegen, gcc — same recipe as jacl_svm).
fn build_emit_macro_body() -> PathBuf {
    let out = std::env::temp_dir().join(format!("emit_macro_body_{}", std::process::id()));
    let ms = format!("{ROOT}/codegen/selfhost/macro_staging");
    let status = Command::new("gcc")
        .args(["-O1", "-w", "-D_GNU_SOURCE"])
        .arg("-I").arg(format!("{ROOT}/codegen"))
        .arg(format!("{ms}/emit_macro_body.c"))
        .arg(format!("{ROOT}/codegen/codegen.c"))
        .arg(format!("{ROOT}/codegen/irbuilder.c"))
        .arg("-o").arg(&out)
        .args(["-lm", "-lpthread"])
        .status()
        .expect("spawn gcc");
    assert!(status.success(), "gcc failed to build emit_macro_body");
    out
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let macro_file = args
        .first()
        .unwrap_or_else(|| die("usage: stage_macro <macro.jacl>   (argument wire on stdin)"));

    // 1. codegen the complete staged-macro program.
    let tool = build_emit_macro_body();
    let out = Command::new(&tool)
        .arg("--staged")
        .arg(macro_file)
        .output()
        .expect("run emit_macro_body");
    if !out.status.success() {
        eprint!("{}", String::from_utf8_lossy(&out.stderr));
        die("emit_macro_body failed");
    }
    let raw = String::from_utf8(out.stdout).unwrap_or_else(|_| die("emitted IR not UTF-8"));
    // Split the module text from its SelfData relocations (%%RELOCS%% wire format) — string/data
    // literals need these applied at link, else they resolve to empty. (Same as jacl_svm.)
    let (text, relocs) = match raw.find("%%RELOCS%%") {
        None => (raw.as_str(), Vec::new()),
        Some(i) => {
            let r = raw[i..]
                .lines()
                .skip(1)
                .filter(|l| !l.trim().is_empty())
                .filter_map(|l| {
                    let n: Vec<u32> = l.split_whitespace().filter_map(|t| t.parse().ok()).collect();
                    (n.len() == 3).then(|| svm_ir::DataReloc {
                        func: n[0], block: n[1], inst: n[2], kind: svm_ir::RelocKind::SelfData,
                    })
                })
                .collect::<Vec<_>>();
            (&raw[..i], r)
        }
    };
    let program = svm_text::parse_module(text).unwrap_or_else(|e| die(&format!("parse: {e:?}")));

    // 2. translate the staging-extended runtime, link, wrap the entry in the powerbox _start.
    eprintln!("stage_macro: translating staging runtime…");
    let rt = jacl_runtime_harness::translate_runtime_staging();
    let linked = svm_ir::link_with_manifest(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![("__jacl_entry".to_string(), 0)],
            relocations: relocs,
            ..Default::default()
        },
    ])
    .unwrap_or_else(|e| die(&format!("link: {e:?}")));
    let entry = linked
        .resolve_export("__jacl_entry")
        .unwrap_or_else(|| die("entry export missing after link"));
    let module = svm_ir::synth_manifest_start(linked, entry, false)
        .unwrap_or_else(|e| die(&format!("powerbox: {e}")));

    let imports = svm_run::Imports::new()
        .provide("write", svm_run::HostCap::stdout())
        .provide("exit", svm_run::HostCap::exit())
        .provide("read", svm_run::HostCap::stdin());   // synrt_read_arg's read(0) -> seeded stdin
    let inst = svm_run::instantiate_with_imports(module, imports)
        .unwrap_or_else(|e| die(&format!("instantiate: {e}")));

    // 3. feed the argument wire on stdin, run, emit the result wire on stdout.
    let mut stdin_bytes = Vec::new();
    std::io::stdin().read_to_end(&mut stdin_bytes).ok();
    let run = inst
        .call_with_stdin(&stdin_bytes)
        .unwrap_or_else(|e| die(&format!("run: {e}")));
    eprintln!("stage_macro: outcome {:?}, {} stdout byte(s)", run.outcome, run.stdout.len());
    std::io::stdout().write_all(&run.stdout).ok();
    std::io::stderr().write_all(&run.stderr).ok();
}
