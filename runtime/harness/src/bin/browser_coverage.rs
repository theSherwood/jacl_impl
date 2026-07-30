//! Browser coverage scoreboard — run the `test/jacl/*.jacl` corpus on the **pure bytecode engine**
//! (the browser's only wasm-safe engine), exactly as the `svm-browser` cdylib's `svm_run_onramp`
//! does: link vs the translated runtime → powerbox `_start` with the runtime's manifest imports
//! bound **by name** (`write`→stdout) → `bytecode::compile_and_run_with_host` with **no tree-walker
//! fallback**. A module the engine declines is `STATUS_UNSUPPORTED` in the browser, so it is scored
//! `unsupported` here — the categories mirror what a real page would see.
//!
//! This is the browser twin of `parity.rs` (which scores interp==jit). It measures how much of the
//! corpus runs on the wasm-safe engine now that `vcpu.tls` is lowered and the `gc.roots + thread`
//! veto is gone, and buckets the rest into a prioritized gap list.
//!
//! Usage: cargo run --release --bin browser_coverage [-- --filter SUBSTR] [--fuel OPS] [--no-doc]
//!        Writes docs/SVM_BROWSER_COVERAGE.md unless --no-doc.

use std::collections::BTreeMap;
use std::fmt::Write as _;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::process::Command;

use svm_interp::{bytecode, cap_id, BoundImport, Host, StreamRole};
use svm_ir::{link_with_manifest, LinkUnit, Module};

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

fn build_driver() -> PathBuf {
    let out = std::env::temp_dir().join(format!("jacl_browser_cov_{}", std::process::id()));
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

fn parse_expects(src: &str) -> (Vec<String>, Option<String>) {
    let mut expects = Vec::new();
    let mut expect_error = None;
    for l in src.lines() {
        if let Some(rest) = l.strip_prefix("# expect-error:") {
            expect_error.get_or_insert_with(|| rest.trim().to_string());
        } else if let Some(rest) = l.strip_prefix("# expect:") {
            expects.push(rest.strip_prefix(' ').unwrap_or(rest).trim_end().to_string());
        }
    }
    (expects, expect_error)
}

/// Build the powerbox `Host` the on-ramp uses: grant a stdout/stdin stream + exit, and bind the
/// module's manifest imports to them **by name** (`write`→stdout write, `read`/`stdin`→stdin read,
/// `exit`→exit) — mirroring `svm-browser`'s `grant_onramp_caps` / `onramp_cap_resolver`.
fn onramp_host(m: &Module) -> Host {
    let mut host = Host::new();
    let out = host.grant_stream(StreamRole::Out);
    let inp = host.grant_stream(StreamRole::In);
    let exit = host.grant_exit();
    let bindings: Vec<BoundImport> = m
        .imports
        .iter()
        .map(|im| match im.name.as_str() {
            "write" => BoundImport::required(cap_id::STREAM, 1, out),
            "read" | "stdin" => BoundImport::required(cap_id::STREAM, 0, inp),
            "exit" => BoundImport::required(cap_id::EXIT, 0, exit),
            _ => BoundImport::rebindable(0, 0, None), // unbound → CapFault if actually used
        })
        .collect();
    host.set_import_bindings(bindings);
    host
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
enum Stage {
    Pass,
    ErrPass,           // expected a compile error and got one (browser-irrelevant, but scored)
    Unsupported,       // decoded+linked but bytecode::compile_module declined (STATUS_UNSUPPORTED)
    Trap(String),      // ran on bytecode, trapped (STATUS_TRAP)
    WrongOutput,       // ran, stdout != expects
    EmitFail,          // frontend/codegen rejected the source
    TextParseFail,     // emitted IR didn't parse
    LinkFail,          // link/powerbox failed
    ErrNoFail,         // expected a compile error, but it compiled
    Hang,              // exhausted the fuel budget
}

impl Stage {
    fn key(&self) -> &'static str {
        match self {
            Stage::Pass => "pass",
            Stage::ErrPass => "err-pass",
            Stage::Unsupported => "unsupported",
            Stage::Trap(_) => "trap",
            Stage::WrongOutput => "wrong-output",
            Stage::EmitFail => "emit-fail",
            Stage::TextParseFail => "text-parse-fail",
            Stage::LinkFail => "link-fail",
            Stage::ErrNoFail => "err-no-fail",
            Stage::Hang => "hang",
        }
    }
    /// "Browser can run it and gets the right answer" — the number that matters.
    fn is_browser_pass(&self) -> bool {
        matches!(self, Stage::Pass)
    }
}

struct Case {
    name: String,
    path: PathBuf,
    expects: Vec<String>,
    expect_error: Option<String>,
}

fn run_case(driver: &PathBuf, rt: &svm_llvm::Translated, cat: &svm_llvm::Translated, case: &Case, fuel_cap: u64) -> Stage {
    // 1. frontend + codegen
    let out = Command::new(driver).arg("--file").arg(&case.path).output().expect("run driver");
    if !out.status.success() {
        return if case.expect_error.is_some() { Stage::ErrPass } else { Stage::EmitFail };
    }
    if case.expect_error.is_some() {
        return Stage::ErrNoFail; // emit succeeded but a compile error was expected
    }
    let program = match jacl_runtime_harness::decode_emitted(&out.stdout) {
        Ok(pr) => pr,
        Err(_) => return Stage::TextParseFail, // decode of the binary emit container failed
    };

    // 2. link vs runtime + catalog, powerbox `_start`
    let linked = match link_with_manifest(&[
        LinkUnit { module: rt.module.clone(), exports: rt.exports.clone(), ..Default::default() },
        LinkUnit { module: cat.module.clone(), exports: cat.exports.clone(), ..Default::default() },
        LinkUnit { module: program, exports: vec![("__jacl_entry".to_string(), 0)], ..Default::default() },
    ]) {
        Ok(m) => m,
        Err(_) => return Stage::LinkFail,
    };
    let entry = match linked.resolve_export("__jacl_entry") {
        Some(e) => e,
        None => return Stage::LinkFail,
    };
    let module = match svm_ir::synth_manifest_start(linked, entry, false) {
        Ok(m) => m,
        Err(_) => return Stage::LinkFail,
    };

    // 3. the browser's engine: pure bytecode, no tree-walker fallback.
    if bytecode::compile_module(&module.funcs).is_none() {
        return Stage::Unsupported;
    }
    let mut host = onramp_host(&module);
    let mut fuel = fuel_cap;
    match bytecode::compile_and_run_with_host(&module, 0, &[], &mut fuel, &mut host) {
        None => Stage::Unsupported, // shouldn't happen (compile_module accepted), but classify safely
        Some(Err(svm_interp::Trap::Exit(_))) | Some(Ok(_)) => {
            if fuel == 0 {
                return Stage::Hang;
            }
            let stdout = String::from_utf8_lossy(&host.stdout);
            let got: Vec<String> = stdout.lines().map(str::to_string).collect();
            if got == case.expects { Stage::Pass } else { Stage::WrongOutput }
        }
        Some(Err(t)) => Stage::Trap(format!("{t:?}")),
    }
}

fn main() {
    let mut filter: Option<String> = None;
    let mut fuel_cap = 500_000_000u64;
    let mut write_doc = true;
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--filter" => filter = args.next(),
            "--fuel" => fuel_cap = args.next().and_then(|s| s.parse().ok()).unwrap_or(fuel_cap),
            "--no-doc" => write_doc = false,
            other => panic!("unknown arg {other}"),
        }
    }

    eprintln!("[coverage] building codegen driver…");
    let driver = build_driver();
    eprintln!("[coverage] translating runtime + catalog…");
    let rt = jacl_runtime_harness::translate_runtime();
    let cat = jacl_runtime_harness::translate_catalog();

    let dir = format!("{ROOT}/test/jacl");
    let mut cases: Vec<Case> = std::fs::read_dir(&dir)
        .expect("read test/jacl")
        .filter_map(|e| {
            let p = e.ok()?.path();
            if p.extension()?.to_str()? != "jacl" {
                return None;
            }
            let name = p.file_stem()?.to_str()?.to_string();
            if let Some(f) = &filter {
                if !name.contains(f.as_str()) {
                    return None;
                }
            }
            let src = std::fs::read_to_string(&p).ok()?;
            let (expects, expect_error) = parse_expects(&src);
            Some(Case { name, path: p, expects, expect_error })
        })
        .collect();
    cases.sort_by(|a, b| a.name.cmp(&b.name));

    let total = cases.len();
    eprintln!("[coverage] running {total} cases on the pure bytecode engine…");

    let mut results: Vec<(String, Stage)> = Vec::with_capacity(total);
    let mut counts: BTreeMap<&'static str, usize> = BTreeMap::new();
    for (i, case) in cases.iter().enumerate() {
        let stage = catch_unwind(AssertUnwindSafe(|| run_case(&driver, &rt, &cat, case, fuel_cap)))
            .unwrap_or_else(|_| Stage::Trap("panic".into()));
        *counts.entry(stage.key()).or_default() += 1;
        if (i + 1) % 50 == 0 || i + 1 == total {
            eprintln!("  {}/{}", i + 1, total);
        }
        results.push((case.name.clone(), stage));
    }

    let browser_pass = results.iter().filter(|(_, s)| s.is_browser_pass()).count();
    // Runnable programs (exclude the expect-error cases, which aren't a browser concern).
    let runnable = results.iter().filter(|(_, s)| !matches!(s, Stage::ErrPass | Stage::ErrNoFail)).count();
    eprintln!("\n[coverage] browser-pass {browser_pass}/{runnable} runnable ({total} total)");
    for (k, v) in &counts {
        eprintln!("  {k:<16} {v}");
    }

    if write_doc {
        let mut md = String::new();
        let _ = writeln!(md, "# SVM browser coverage — corpus on the wasm-safe bytecode engine\n");
        let _ = writeln!(md, "> Generated by `runtime/harness/src/bin/browser_coverage.rs`. Each `test/jacl/*.jacl`");
        let _ = writeln!(md, "> is linked vs the translated runtime and run on the **pure bytecode engine** with a");
        let _ = writeln!(md, "> name-bound powerbox — the exact path the `svm-browser` cdylib's `svm_run_onramp`");
        let _ = writeln!(md, "> takes (no tree-walker fallback). `unsupported` = the engine declined the module");
        let _ = writeln!(md, "> (`STATUS_UNSUPPORTED` in the browser); `trap` = it ran but faulted.\n");
        let _ = writeln!(md, "**Browser-pass: {browser_pass}/{runnable} runnable programs** ({total} corpus files).\n");
        let _ = writeln!(md, "> The powerbox grants only `stdout`/`stdin`/`exit` (a browser sandbox has no");
        let _ = writeln!(md, "> shell or filesystem), so a program that shells out (`!cmd`) or reads/writes files");
        let _ = writeln!(md, "> (`read-file`/`write-file`) is a **capability** miss, not an engine gap. Zero");
        let _ = writeln!(md, "> `unsupported`/`trap` means the wasm-safe bytecode engine itself runs everything else.\n");
        let _ = writeln!(md, "| category | count |\n|---|---|");
        for (k, v) in &counts {
            let _ = writeln!(md, "| {k} | {v} |");
        }
        // The prioritized gap list: the programs the browser can't yet run.
        for cat in ["unsupported", "trap", "wrong-output"] {
            let names: Vec<&str> = results.iter().filter(|(_, s)| s.key() == cat).map(|(n, _)| n.as_str()).collect();
            if names.is_empty() {
                continue;
            }
            let _ = writeln!(md, "\n## {cat} ({})\n", names.len());
            for chunk in names.chunks(8) {
                let _ = writeln!(md, "{}", chunk.join(", "));
            }
        }
        let path = format!("{ROOT}/docs/SVM_BROWSER_COVERAGE.md");
        std::fs::write(&path, md).expect("write doc");
        eprintln!("[coverage] wrote {path}");
    }
}
