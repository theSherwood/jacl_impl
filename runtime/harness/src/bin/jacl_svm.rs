//! jacl-svm — the SVM-backend CLI (phase 3 of `docs/SVM_FS_DESIGN.md`).
//!
//! Compile-and-run a `.jacl` program with explicit capability grants, and build the
//! shippable fs data images those grants can mount:
//!
//! ```text
//! jacl_svm run <prog.jacl> [--fs-root DIR | --fs-image FILE | --no-fs] [--interp]
//! jacl_svm bundle <assets-dir> -o <out.fsimg>
//! ```
//!
//! Grants are **explicit and default-deny** (SVM_FS_DESIGN.md, design rule 1): with no
//! flag (or `--no-fs`) the program gets no filesystem authority and the runtime's
//! pure-guest VFS serves. `--fs-root DIR` grants the real filesystem **confined to DIR**
//! (`host_fs` — the program's `/` is DIR; it cannot name anything outside it; `DIR/tmp`
//! is created to mirror the language's baseline directory model). `--fs-image FILE`
//! mounts a `bundle`-built data image as an in-memory fs — reads see the bundled assets,
//! writes are discarded when the run ends (the portable / browser-shaped grant).
//!
//! The pipeline is the parity harness's: frontend+codegen emit driver → svm-ir link
//! against the translated runtime (+ extern catalog) → powerbox → run. Default backend
//! is the JIT; `--interp` runs the tree-walking interpreter instead.

use std::path::PathBuf;
use std::process::Command;

use svm_ir::LinkUnit;

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

/// Build the codegen driver (same recipe as parity.rs: gcc, the unity frontend).
fn build_driver() -> PathBuf {
    let out = std::env::temp_dir().join(format!("jacl_svm_emit_{}", std::process::id()));
    let status = Command::new("gcc")
        .args(["-O0", "-w", "-D_DEFAULT_SOURCE"])
        .arg(format!("{ROOT}/codegen/tests/emit_jacl.c"))
        .arg(format!("{ROOT}/codegen/codegen.c"))
        .arg(format!("{ROOT}/codegen/irbuilder.c"))
        .arg("-o")
        .arg(&out)
        .args(["-lm", "-lpthread"])
        .status()
        .expect("spawn gcc (is gcc installed?)");
    assert!(status.success(), "gcc failed to build the codegen driver");
    out
}

fn die(msg: &str) -> ! {
    eprintln!("jacl-svm: {msg}");
    std::process::exit(1);
}

/// `bundle <assets-dir> -o <out.fsimg>` — walk a host directory into an fs seed and
/// serialize it as a data image (`svm-fs` `read_host_dir` + `encode_image`). The image is
/// what `run --fs-image` (or any embedder via `mem_fs_from_archive`) mounts.
fn cmd_bundle(args: &[String]) {
    let mut dir: Option<PathBuf> = None;
    let mut out: Option<PathBuf> = None;
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "-o" => out = it.next().map(PathBuf::from),
            other if dir.is_none() => dir = Some(PathBuf::from(other)),
            other => die(&format!("bundle: unexpected argument '{other}'")),
        }
    }
    let dir = dir.unwrap_or_else(|| die("bundle: missing <assets-dir>"));
    let out = out.unwrap_or_else(|| die("bundle: missing -o <out.fsimg>"));
    let (files, dirs) = svm_run::fs::read_host_dir(&dir)
        .unwrap_or_else(|e| die(&format!("bundle: read {}: {e}", dir.display())));
    let bytes: usize = files.iter().map(|(_, b)| b.len()).sum();
    let img = svm_run::fs::encode_image(&files, &dirs);
    std::fs::write(&out, &img)
        .unwrap_or_else(|e| die(&format!("bundle: write {}: {e}", out.display())));
    eprintln!(
        "jacl-svm: bundled {} file(s), {} dir(s), {} content byte(s) -> {} ({} bytes)",
        files.len(),
        dirs.len(),
        bytes,
        out.display(),
        img.len()
    );
}

enum FsGrant {
    None,
    Root(PathBuf),
    Image(PathBuf),
}

/// `run <prog.jacl> [--fs-root DIR | --fs-image FILE | --no-fs] [--interp]`.
fn cmd_run(args: &[String]) -> ! {
    let mut prog: Option<PathBuf> = None;
    let mut grant = FsGrant::None;
    let mut interp = false;
    let mut bytecode = false;
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--fs-root" => {
                grant = FsGrant::Root(it.next().map(PathBuf::from).unwrap_or_else(|| die("--fs-root needs DIR")))
            }
            "--fs-image" => {
                grant = FsGrant::Image(it.next().map(PathBuf::from).unwrap_or_else(|| die("--fs-image needs FILE")))
            }
            "--no-fs" => grant = FsGrant::None,
            "--interp" => interp = true,
            "--bytecode" => bytecode = true,
            other if prog.is_none() => prog = Some(PathBuf::from(other)),
            other => die(&format!("run: unexpected argument '{other}'")),
        }
    }
    let prog = prog.unwrap_or_else(|| die("run: missing <prog.jacl>"));

    // Compile: frontend + SVM codegen via the emit driver.
    let driver = build_driver();
    let out = Command::new(&driver).arg("--file").arg(&prog).output().expect("run emit driver");
    if !out.status.success() {
        eprint!("{}", String::from_utf8_lossy(&out.stderr));
        std::process::exit(1);
    }
    let (program, relocs) = jacl_runtime_harness::decode_emitted(&out.stdout)
        .unwrap_or_else(|e| die(&format!("decode emitted IR: {e}")));

    // Link against the translated runtime + extern catalog; powerbox; instantiate.
    eprintln!("jacl-svm: translating runtime…");
    let rt = jacl_runtime_harness::translate_runtime();
    let cat = jacl_runtime_harness::translate_catalog();
    // Retain the runtime's manifest capability imports through link (post-IMPORTS.md-phase-4;
    // docs/SVM_IMPORT_MIGRATION_ASK.md), wrap the program entry (its func 0) in the paramless
    // powerbox `_start`, and bind the powerbox names.
    let linked = svm_ir::link_with_manifest(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit { module: cat.module, exports: cat.exports, ..Default::default() },
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
        .provide("stdin", svm_run::HostCap::stdin());
    let inst = svm_run::instantiate_with_imports(module, imports)
        .unwrap_or_else(|e| die(&format!("instantiate: {e}")));

    // The grant (SVM_FS_DESIGN.md host-policy ladder). Explicit, default-deny.
    let caps: Vec<(&str, svm_run::HostCap)> = match &grant {
        FsGrant::None => Vec::new(),
        FsGrant::Root(dir) => {
            // The program's `/` is DIR; create it (and DIR/tmp, the language's baseline
            // directory model — the guest VFS models `/` and `/tmp` as existing).
            std::fs::create_dir_all(dir.join("tmp"))
                .unwrap_or_else(|e| die(&format!("--fs-root {}: {e}", dir.display())));
            vec![("fs", svm_run::fs::host_fs(dir.clone()))]
        }
        FsGrant::Image(file) => {
            let bytes = std::fs::read(file)
                .unwrap_or_else(|e| die(&format!("--fs-image {}: {e}", file.display())));
            let cap = svm_run::fs::mem_fs_from_archive(&bytes)
                .unwrap_or_else(|e| die(&format!("--fs-image {}: {e}", file.display())));
            vec![("fs", cap)]
        }
    };

    // `--bytecode` selects the wasm-safe bytecode engine (the browser tier). For JACL modules it
    // currently falls back to the tree-walker at compile time — the `gc.roots + thread` seam veto
    // (docs/SVM_BROWSER_SPIKE_FINDINGS.md) declines the linked runtime — so output matches
    // `--interp`; the flag exists to exercise that path directly as the browser migration proceeds.
    let backend = match (bytecode, interp) {
        (true, _) => svm_run::Backend::Bytecode,
        (false, true) => svm_run::Backend::TreeWalk,
        (false, false) => svm_run::Backend::Jit,
    };
    let run = inst
        .run_with_caps(backend, &svm_run::RunConfig::default(), &caps)
        .unwrap_or_else(|e| die(&format!("run: {e}")));
    use std::io::Write;
    std::io::stdout().write_all(&run.stdout).ok();
    std::io::stderr().write_all(&run.stderr).ok();
    match run.outcome {
        svm_run::Outcome::Exited(c) => std::process::exit(c),
        svm_run::Outcome::Returned(_) => std::process::exit(0),
    }
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match args.first().map(String::as_str) {
        Some("run") => cmd_run(&args[1..]),
        Some("bundle") => cmd_bundle(&args[1..]),
        _ => die("usage: jacl_svm run <prog.jacl> [--fs-root DIR | --fs-image FILE | --no-fs] [--interp]\n       jacl_svm bundle <assets-dir> -o <out.fsimg>"),
    }
}
