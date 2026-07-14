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

use svm_ir::{link, LinkUnit};

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

fn run_case(driver: &PathBuf, rt: &svm_llvm::Translated, case: &Case) -> Stage {
    let out = Command::new(driver).arg("--file").arg(&case.path).output().expect("run emit driver");
    let stderr = String::from_utf8_lossy(&out.stderr);

    if let Some(want_err) = &case.expect_error {
        return if out.status.success() {
            Stage::ErrNoFail
        } else if stderr.contains(want_err.as_str()) {
            Stage::ErrPass
        } else {
            Stage::ErrWrongMsg(brief(&stderr))
        };
    }
    if !out.status.success() {
        return Stage::EmitFail(brief(&stderr));
    }

    let raw = match String::from_utf8(out.stdout) {
        Ok(s) => s,
        Err(_) => return Stage::TextParseFail("driver output not UTF-8".into()),
    };
    let (text, relocs) = split_relocs(&raw);
    let program = match svm_text::parse_module(text) {
        Ok(m) => m,
        Err(e) => return Stage::TextParseFail(brief(&format!("{e:?}"))),
    };

    let entry = rt.module.funcs.len() as u32;
    let linked = match link(&[
        LinkUnit { module: rt.module.clone(), exports: rt.exports.clone(), ..Default::default() },
        LinkUnit { module: program, relocations: relocs, ..Default::default() },
    ]) {
        Ok(l) => l,
        Err(e) => return Stage::LinkFail(brief(&format!("{e:?}"))),
    };
    if !linked.imports.is_empty() {
        return Stage::LinkFail(brief(&format!("unresolved imports {:?}", linked.imports)));
    }

    let pb = match svm_ir::synth_powerbox_start(linked, entry, 3, false) {
        Ok(m) => m,
        Err(e) => return Stage::LinkFail(brief(&format!("powerbox: {e}"))),
    };
    let inst = match svm_run::instantiate(pb) {
        Ok(i) => i,
        Err(e) => return Stage::RunFail(brief(&format!("instantiate: {e}"))),
    };
    let run = match inst.run_diff(&svm_run::RunConfig::default()) {
        Ok(r) => r,
        Err(e) => return Stage::RunFail(brief(&format!("{e}"))),
    };

    let got: Vec<String> =
        String::from_utf8_lossy(&run.stdout).lines().map(|l| l.trim_end().to_string()).collect();
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
        // Per-case clone of the translated runtime so a hung case can't poison the shared one.
        let rt_mod = rt.module.clone();
        let rt_exp = rt.exports.clone();
        std::thread::spawn(move || {
            let stage = catch_unwind(AssertUnwindSafe(|| {
                let t = svm_llvm::Translated { module: rt_mod, entry_sp: 0, exports: rt_exp };
                run_case(&d, &t, &c)
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
