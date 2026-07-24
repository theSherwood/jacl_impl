//! Phase-4 parity scoreboard — run the old backend's `.jacl` corpus on the svm backend.
//!
//! Drives every `test/jacl/*.jacl` through the real pipeline (frontend → svm codegen →
//! link against the translated runtime → powerbox → interp==jit via `run_diff`) and
//! scores it against the corpus's own inline oracle:
//!   `# expect: <line>`        — expected stdout lines, in order
//!   `# expect-error: <text>`  — compilation must fail and mention <text>
//!
//! Every case is classified by the stage it reaches, so the scoreboard doubles as a
//! prioritized worklist for the codegen (Phase 4 bring-up):
//!   pass / err-pass          — output (or expected compile error) matches
//!   emit-fail                — frontend or codegen rejected it (the breadth gap)
//!   err-wrong-msg            — expected a compile error, got a different one
//!   err-no-fail              — expected a compile error, but it compiled + ran
//!   text/link/run-fail       — pipeline stages after codegen
//!   wrong-output             — ran, but stdout != expects
//!   hang                     — exceeded the per-case timeout
//!
//! Usage: cargo run --release --bin parity [-- --filter SUBSTR] [--timeout SECS]
//!        Writes docs/SVM_PARITY.md (repo-relative) unless --no-doc.
//! Run with --release: svm-jit's gc.roots scan trips a debug-only UB precondition
//! check (pre-existing svm debug assertion, benign in release).

use std::collections::BTreeMap;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::process::Command;
use std::sync::mpsc;
use std::time::Duration;

use svm_ir::{link_with_manifest, LinkUnit};

/// The program-module export the harness pins its entry to, so `resolve_export` recovers the
/// entry funcidx after linking reshuffles function indices (the program's entry is its func 0).
const ENTRY_SYM: &str = "__jacl_entry";

/// The fixed powerbox names a jacl program's runtime imports (`write` from `jacl_print`; `exit`
/// when a program halts), bound by name at instantiation. `stdin` is provided defensively — a
/// name the module doesn't import is simply unused (`instantiate_with_imports` only requires that
/// every *declared* import is covered).
fn powerbox_imports() -> svm_run::Imports {
    svm_run::Imports::new()
        .provide("write", svm_run::HostCap::stdout())
        .provide("exit", svm_run::HostCap::exit())
        .provide("stdin", svm_run::HostCap::stdin())
}

/// Link `[runtime, catalog, program]` retaining the runtime's manifest capability imports
/// (`link_with_manifest`), wrap the program's entry (its func 0, pinned as [`ENTRY_SYM`]) in the
/// paramless powerbox `_start` (`synth_manifest_start`), and instantiate with the powerbox names
/// bound. The post-IMPORTS.md-phase-4 embedding path (docs/SVM_IMPORT_MIGRATION_ASK.md; svm's
/// `link_manifest.rs` is the reference). Returns a runnable manifest module instance.
fn link_instantiate(
    rt: &svm_llvm::Translated,
    cat: &svm_llvm::Translated,
    text: &str,
    relocs: Vec<svm_ir::DataReloc>,
) -> Result<svm_run::Instance, String> {
    let program = svm_text::parse_module(text).map_err(|e| format!("{e:?}"))?;
    let linked = link_with_manifest(&[
        LinkUnit { module: rt.module.clone(), exports: rt.exports.clone(), ..Default::default() },
        LinkUnit { module: cat.module.clone(), exports: cat.exports.clone(), ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![(ENTRY_SYM.to_string(), 0)],
            relocations: relocs,
            ..Default::default()
        },
    ])
    .map_err(|e| format!("{e:?}"))?;
    let entry = linked
        .resolve_export(ENTRY_SYM)
        .ok_or_else(|| "entry export missing after link".to_string())?;
    let module = svm_ir::synth_manifest_start(linked, entry, false).map_err(|e| format!("powerbox: {e}"))?;
    svm_run::instantiate_with_imports(module, powerbox_imports()).map_err(|e| format!("instantiate: {e}"))
}

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

/// Build the codegen driver (same recipe as tests/codegen.rs: gcc, the unity frontend).
fn build_driver() -> PathBuf {
    let out = std::env::temp_dir().join(format!("jacl_parity_emit_{}", std::process::id()));
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

#[derive(Clone)]
struct Case {
    name: String,
    path: PathBuf,
    expects: Vec<String>,
    expect_error: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
enum Stage {
    Pass,
    ErrPass,
    WrongOutput(String),
    ErrNoFail,
    ErrWrongMsg(String),
    EmitFail(String),
    TextParseFail(String),
    LinkFail(String),
    RunFail(String),
    Hang,
}

impl Stage {
    fn key(&self) -> &'static str {
        match self {
            Stage::Pass => "pass",
            Stage::ErrPass => "err-pass",
            Stage::WrongOutput(_) => "wrong-output",
            Stage::ErrNoFail => "err-no-fail",
            Stage::ErrWrongMsg(_) => "err-wrong-msg",
            Stage::EmitFail(_) => "emit-fail",
            Stage::TextParseFail(_) => "text-parse-fail",
            Stage::LinkFail(_) => "link-fail",
            Stage::RunFail(_) => "run-fail",
            Stage::Hang => "hang",
        }
    }
    fn is_pass(&self) -> bool {
        matches!(self, Stage::Pass | Stage::ErrPass)
    }
    fn detail(&self) -> Option<&str> {
        match self {
            Stage::WrongOutput(d)
            | Stage::ErrWrongMsg(d)
            | Stage::EmitFail(d)
            | Stage::TextParseFail(d)
            | Stage::LinkFail(d)
            | Stage::RunFail(d) => Some(d),
            _ => None,
        }
    }
}

/// First line of a message, truncated — enough to bucket failures without noise.
fn brief(s: &str) -> String {
    let line = s.lines().find(|l| !l.trim().is_empty()).unwrap_or("").trim();
    let mut out: String = line.chars().take(120).collect();
    if line.chars().count() > 120 {
        out.push('…');
    }
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

/// Split the driver's output into module text + SelfData relocs (matches tests/codegen.rs).
fn split_relocs(raw: &str) -> (&str, Vec<svm_ir::DataReloc>) {
    match raw.find("%%RELOCS%%") {
        None => (raw, Vec::new()),
        Some(i) => {
            let relocs = raw[i..]
                .lines()
                .skip(1)
                .filter(|l| !l.trim().is_empty())
                .map(|l| {
                    let n: Vec<u32> = l.split_whitespace().map(|t| t.parse().unwrap()).collect();
                    svm_ir::DataReloc { func: n[0], block: n[1], inst: n[2], kind: svm_ir::RelocKind::SelfData }
                })
                .collect();
            (&raw[..i], relocs)
        }
    }
}

/// The fs-granted analogue of `run_diff`: run the powerbox entry on the tree-walker AND the
/// JIT with a named `"fs"` capability granted — a `mem_fs` seeded with `dirs=["tmp"]` (the
/// directory model the guest VFS exposes) — and require the two legs to agree. Each grant
/// clones the seed fresh (the `HostCap` `make` builder), so the legs can't leak filesystem
/// state into each other. `run_diff` itself doesn't take extra caps (yet), hence the
/// hand-rolled two-leg diff via `run_with_caps`.
fn run_fs_granted(inst: &svm_run::Instance) -> Result<svm_run::Run, String> {
    let cfg = svm_run::RunConfig::default();
    let cap = || svm_run::fs::mem_fs_seeded(Vec::new(), vec!["tmp".to_string()]);
    let i = inst
        .run_with_caps(svm_run::Backend::TreeWalk, &cfg, &[("fs", cap())])
        .map_err(|e| format!("{e}"))?;
    let j = inst
        .run_with_caps(svm_run::Backend::Jit, &cfg, &[("fs", cap())])
        .map_err(|e| format!("{e}"))?;
    if i.outcome != j.outcome {
        return Err("interp/JIT outcome diverge (fs-granted)".into());
    }
    if i.stdout != j.stdout {
        return Err("interp/JIT stdout diverge (fs-granted)".into());
    }
    Ok(i)
}

/// Build (link + powerbox) and run an emitted program, returning its stdout lines on a
/// clean run or a failure description (link error / trap / interp≠jit divergence) — the
/// latter is what a runtime `expect-error` case matches against. `fs_granted` picks the
/// plain `run_diff` or the [`run_fs_granted`] two-leg.
fn build_and_run(rt: &svm_llvm::Translated, cat: &svm_llvm::Translated, driver_stdout: &[u8], fs_granted: bool) -> Result<Vec<String>, String> {
    let raw = String::from_utf8(driver_stdout.to_vec()).map_err(|_| "driver output not UTF-8".to_string())?;
    let (text, relocs) = split_relocs(&raw);
    let inst = link_instantiate(rt, cat, text, relocs)?;
    let run = if fs_granted {
        run_fs_granted(&inst)?
    } else {
        inst.run_diff(&svm_run::RunConfig::default()).map_err(|e| format!("{e}"))?
    };
    Ok(String::from_utf8_lossy(&run.stdout).lines().map(|l| l.trim_end().to_string()).collect())
}

fn run_case(driver: &PathBuf, rt: &svm_llvm::Translated, cat: &svm_llvm::Translated, case: &Case) -> Stage {
    let out = Command::new(driver).arg("--file").arg(&case.path).output().expect("run emit driver");
    let stderr = String::from_utf8_lossy(&out.stderr);

    if let Some(want_err) = &case.expect_error {
        // A compile-time error: the emit driver fails and prints the message to stderr.
        if !out.status.success() {
            return if stderr.contains(want_err.as_str()) { Stage::ErrPass } else { Stage::ErrWrongMsg(brief(&stderr)) };
        }
        // Otherwise the program must surface the error at RUNTIME: run it and look for the
        // expected message in its output (an uncaught error prints `<error: …>`) or in a trap.
        let score = |fs_granted: bool| match build_and_run(rt, cat, &out.stdout, fs_granted) {
            Ok(lines) => {
                if lines.iter().any(|l| l.contains(want_err.as_str())) { Stage::ErrPass } else { Stage::ErrNoFail }
            }
            Err(e) => {
                if e.contains(want_err.as_str()) { Stage::ErrPass } else { Stage::ErrWrongMsg(brief(&e)) }
            }
        };
        let ungranted = score(false);
        // Dual-mode gate (SVM_FS_DESIGN.md phase 2): an io_* case must score identically
        // with and without the "fs" capability granted — the adapter may not change
        // observable semantics, only the backing store.
        if case.name.starts_with("io_") {
            let granted = score(true);
            if granted.key() != ungranted.key() {
                return Stage::ErrWrongMsg(brief(&format!(
                    "fs-granted mode diverged: ungranted={} granted={}",
                    ungranted.key(),
                    granted.key()
                )));
            }
        }
        return ungranted;
    }
    if !out.status.success() {
        return Stage::EmitFail(brief(&stderr));
    }

    let raw = match String::from_utf8(out.stdout) {
        Ok(s) => s,
        Err(_) => return Stage::TextParseFail("driver output not UTF-8".into()),
    };
    let (text, relocs) = split_relocs(&raw);

    let inst = match link_instantiate(rt, cat, text, relocs) {
        Ok(i) => i,
        Err(e) if e.starts_with("instantiate") || e.starts_with("powerbox") => {
            return Stage::RunFail(brief(&e))
        }
        Err(e) => return Stage::LinkFail(brief(&e)),
    };
    let run = match inst.run_diff(&svm_run::RunConfig::default()) {
        Ok(r) => r,
        Err(e) => return Stage::RunFail(brief(&format!("{e}"))),
    };

    let got: Vec<String> =
        String::from_utf8_lossy(&run.stdout).lines().map(|l| l.trim_end().to_string()).collect();
    // Dual-mode gate (SVM_FS_DESIGN.md phase 2): an io_* case must produce the same output
    // with the "fs" capability granted (seeded mem_fs) as without it (guest VFS).
    if case.name.starts_with("io_") {
        match build_and_run(rt, cat, raw.as_bytes(), /*fs_granted=*/ true) {
            Ok(glines) if glines == got => {}
            Ok(glines) => {
                return Stage::WrongOutput(brief(&format!(
                    "fs-granted mode diverged: ungranted [{}…], granted [{}…]",
                    got.first().map(String::as_str).unwrap_or(""),
                    glines.first().map(String::as_str).unwrap_or("")
                )));
            }
            Err(e) => return Stage::RunFail(brief(&format!("fs-granted mode: {e}"))),
        }
    }
    if got == case.expects {
        Stage::Pass
    } else {
        let d = format!(
            "want {} line(s) [{}…], got {} [{}…]",
            case.expects.len(),
            case.expects.first().map(String::as_str).unwrap_or(""),
            got.len(),
            got.first().map(String::as_str).unwrap_or("")
        );
        Stage::WrongOutput(brief(&d))
    }
}

fn main() {
    let mut filter: Option<String> = None;
    let mut timeout = 10u64;
    let mut write_doc = true;
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--filter" => filter = args.next(),
            "--timeout" => timeout = args.next().and_then(|s| s.parse().ok()).unwrap_or(10),
            "--no-doc" => write_doc = false,
            other => panic!("unknown arg {other}"),
        }
    }

    eprintln!("[parity] building codegen driver…");
    let driver = build_driver();
    eprintln!("[parity] translating runtime…");
    let rt = jacl_runtime_harness::translate_runtime();
    eprintln!("[parity] translating extern catalog…");
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
    eprintln!("[parity] {} cases, timeout {timeout}s each", cases.len());

    let mut results: Vec<(Case, Stage)> = Vec::with_capacity(cases.len());
    for (i, case) in cases.iter().enumerate() {
        let (tx, rx) = mpsc::channel();
        let c = case.clone();
        let d = driver.clone();
        // Per-case clone of the translated runtime + catalog so a hung case can't poison the shared one.
        let rt_mod = rt.module.clone();
        let rt_exp = rt.exports.clone();
        let cat_mod = cat.module.clone();
        let cat_exp = cat.exports.clone();
        std::thread::spawn(move || {
            let stage = catch_unwind(AssertUnwindSafe(|| {
                let t = svm_llvm::Translated { module: rt_mod, entry_sp: 0, exports: rt_exp };
                let ct = svm_llvm::Translated { module: cat_mod, entry_sp: 0, exports: cat_exp };
                run_case(&d, &t, &ct, &c)
            }))
            .unwrap_or_else(|p| {
                let msg = p
                    .downcast_ref::<String>()
                    .cloned()
                    .or_else(|| p.downcast_ref::<&str>().map(|s| s.to_string()))
                    .unwrap_or_else(|| "panic".into());
                Stage::RunFail(brief(&format!("panic: {msg}")))
            });
            let _ = tx.send(stage);
        });
        let stage = rx.recv_timeout(Duration::from_secs(timeout)).unwrap_or(Stage::Hang);
        if !stage.is_pass() {
            eprintln!("  [{}/{}] {:<40} {}  {}", i + 1, cases.len(), case.name, stage.key(), stage.detail().unwrap_or(""));
        }
        results.push((case.clone(), stage));
    }

    // ---- aggregate ----
    let total = results.len();
    let passed = results.iter().filter(|(_, s)| s.is_pass()).count();
    let mut by_stage: BTreeMap<&'static str, usize> = BTreeMap::new();
    for (_, s) in &results {
        *by_stage.entry(s.key()).or_default() += 1;
    }
    let mut by_cat: BTreeMap<String, (usize, usize)> = BTreeMap::new();
    for (c, s) in &results {
        let cat = c.name.split('_').next().unwrap_or(&c.name).to_string();
        let e = by_cat.entry(cat).or_default();
        e.0 += 1;
        if s.is_pass() {
            e.1 += 1;
        }
    }
    // Bucket emit failures by message — the codegen worklist.
    let mut emit_buckets: BTreeMap<String, usize> = BTreeMap::new();
    for (_, s) in &results {
        if let Stage::EmitFail(m) | Stage::ErrWrongMsg(m) = s {
            *emit_buckets.entry(m.clone()).or_default() += 1;
        }
    }
    let mut emit_sorted: Vec<_> = emit_buckets.into_iter().collect();
    emit_sorted.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(&b.0)));

    println!("\n== SVM backend parity: {passed}/{total} ==");
    for (k, n) in &by_stage {
        println!("  {k:<16} {n}");
    }

    if write_doc {
        use std::fmt::Write as _;
        let mut md = String::new();
        let _ = writeln!(md, "# SVM backend parity scoreboard\n");
        let _ = writeln!(md, "> Generated by `cargo run --release --bin parity` (runtime/harness). Do not edit by hand.");
        let _ = writeln!(md, "> Corpus: `test/jacl/*.jacl` with inline `# expect:` / `# expect-error:` oracles.\n");
        let _ = writeln!(md, "## Total: **{passed} / {total}**\n");
        let _ = writeln!(md, "| stage | count |\n|---|---|");
        for (k, n) in &by_stage {
            let _ = writeln!(md, "| {k} | {n} |");
        }
        let _ = writeln!(md, "\n## By category (pass/total)\n");
        let _ = writeln!(md, "| category | pass | total |\n|---|---|---|");
        for (cat, (tot, pass)) in &by_cat {
            let _ = writeln!(md, "| {cat} | {pass} | {tot} |");
        }
        let _ = writeln!(md, "\n## Codegen worklist (emit failures by message)\n");
        let _ = writeln!(md, "| count | first error line |\n|---|---|");
        for (m, n) in emit_sorted.iter().take(40) {
            let _ = writeln!(md, "| {n} | `{}` |", m.replace('|', "\\|"));
        }
        let _ = writeln!(md, "\n## Non-emit failures\n");
        for (c, s) in &results {
            if !s.is_pass() && !matches!(s, Stage::EmitFail(_) | Stage::ErrWrongMsg(_)) {
                let _ = writeln!(md, "- `{}` — {}: {}", c.name, s.key(), s.detail().unwrap_or("-"));
            }
        }
        let path = format!("{ROOT}/docs/SVM_PARITY.md");
        std::fs::write(&path, md).expect("write docs/SVM_PARITY.md");
        eprintln!("[parity] wrote {path}");
    }
}
