//! bench_svm — execution-time benchmark for the SVM backend.
//!
//! The counterpart to `test/test_perf.c` (which times the *old bytecode VM*'s
//! runtime). This drives the same `test/jacl/bench/*.jacl` scenarios through the
//! real SVM pipeline and times **execution only**, hoisting the one-time costs
//! (svm-llvm runtime translation, codegen driver build, link, JIT compile) out of
//! the timed region so the numbers are comparable to the VM harness's
//! compile-once / run-N-times medians.
//!
//! Pipeline per scenario (mirrors `parity.rs`):
//!   emit driver (--file) → parse IR → link(rt, catalog, program)
//!   → synth_powerbox_start → instantiate  ...  [done once, untimed]
//!   → run_with_caps(backend) × N           ...  [timed]
//!
//! `run_with_caps(Jit)` recompiles the module each call (~ms of Cranelift), so the
//! JIT numbers carry a per-run compile tax. We quantify it with an `_baseline`
//! near-empty program (`print 0`): its per-run time ≈ compile + VM startup, so
//! `scenario_min - baseline_min` estimates pure guest execution. Both raw and
//! baseline-subtracted figures are reported.
//!
//! Output: JSONL on stdout (one line per scenario) + a human summary on stderr.
//! Env: BENCH_ITERS (default 25), BENCH_WARMUP (default 3),
//!      BENCH_INTERP_ITERS (default 8; the tree-walker is slow),
//!      BENCH_JSON_OUT (path; default stdout), BENCH_NO_INTERP=1 to skip interp.
//! Args: one or more `.jacl` paths to bench (order preserved). Put `_baseline`
//!       first (or name a file `_baseline.jacl`) to establish the compile tax.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

use svm_ir::{link, LinkUnit};
use svm_run::{Backend, Instance, RunConfig};

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

/// Build the codegen driver (same recipe as parity.rs / jacl_svm.rs).
fn build_driver() -> PathBuf {
    let out = std::env::temp_dir().join(format!("bench_svm_emit_{}", std::process::id()));
    let status = Command::new("gcc")
        .args(["-O2", "-w", "-D_DEFAULT_SOURCE"])
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

/// Split the driver's stdout into module text + SelfData relocs (matches parity.rs).
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
                    svm_ir::DataReloc {
                        func: n[0],
                        block: n[1],
                        inst: n[2],
                        kind: svm_ir::RelocKind::SelfData,
                    }
                })
                .collect();
            (&raw[..i], relocs)
        }
    }
}

/// Compile a scenario `.jacl` all the way to an instantiated `Instance` (untimed).
fn build_instance(
    driver: &Path,
    rt: &svm_llvm::Translated,
    cat: &svm_llvm::Translated,
    scenario: &Path,
) -> Result<Instance, String> {
    let out = Command::new(driver)
        .arg("--file")
        .arg(scenario)
        .output()
        .expect("run emit driver");
    if !out.status.success() {
        return Err(format!(
            "emit driver failed: {}",
            String::from_utf8_lossy(&out.stderr)
        ));
    }
    let raw = String::from_utf8(out.stdout).map_err(|_| "emit output not UTF-8".to_string())?;
    let (text, relocs) = split_relocs(&raw);
    let program = svm_text::parse_module(text).map_err(|e| format!("parse IR: {e:?}"))?;

    let entry = (rt.module.funcs.len() + cat.module.funcs.len()) as u32;
    let linked = link(&[
        LinkUnit {
            module: rt.module.clone(),
            exports: rt.exports.clone(),
            ..Default::default()
        },
        LinkUnit {
            module: cat.module.clone(),
            exports: cat.exports.clone(),
            ..Default::default()
        },
        LinkUnit {
            module: program,
            relocations: relocs,
            ..Default::default()
        },
    ])
    .map_err(|e| format!("link: {e:?}"))?;
    if !linked.imports.is_empty() {
        return Err(format!("unresolved imports {:?}", linked.imports));
    }
    let pb = svm_ir::synth_powerbox_start(linked, entry, 3, false)
        .map_err(|e| format!("powerbox: {e}"))?;
    svm_run::instantiate(pb).map_err(|e| format!("instantiate: {e}"))
}

fn run_once(inst: &Instance, backend: Backend, cfg: &RunConfig) -> Result<Vec<u8>, String> {
    inst.run_with_caps(backend, cfg, &[]).map(|r| r.stdout)
}

/// Time `iters` runs, returning (min_ns, median_ns) over the samples.
fn time_runs(inst: &Instance, backend: Backend, cfg: &RunConfig, warmup: u32, iters: u32) -> (u64, u64) {
    for _ in 0..warmup {
        let _ = inst.run_with_caps(backend, cfg, &[]);
    }
    let mut samples: Vec<u64> = Vec::with_capacity(iters as usize);
    for _ in 0..iters {
        let t0 = Instant::now();
        let _ = inst.run_with_caps(backend, cfg, &[]);
        samples.push(t0.elapsed().as_nanos() as u64);
    }
    samples.sort_unstable();
    let min = samples[0];
    let median = samples[samples.len() / 2];
    (min, median)
}

fn env_u32(key: &str, default: u32) -> u32 {
    std::env::var(key).ok().and_then(|v| v.parse().ok()).unwrap_or(default)
}

fn main() {
    let files: Vec<PathBuf> = std::env::args().skip(1).map(PathBuf::from).collect();
    if files.is_empty() {
        eprintln!("usage: bench_svm <scenario.jacl>...");
        std::process::exit(2);
    }
    let iters = env_u32("BENCH_ITERS", 25);
    let warmup = env_u32("BENCH_WARMUP", 3);
    let interp_iters = env_u32("BENCH_INTERP_ITERS", 8);
    let no_interp = std::env::var("BENCH_NO_INTERP").is_ok();

    eprintln!("bench_svm: building codegen driver…");
    let driver = build_driver();
    eprintln!("bench_svm: translating runtime (svm-llvm)…");
    let rt = jacl_runtime_harness::translate_runtime();
    let cat = jacl_runtime_harness::translate_catalog();
    eprintln!(
        "bench_svm: runtime translated ({} funcs). iters={iters} warmup={warmup} interp_iters={} interp={}",
        rt.module.funcs.len(),
        interp_iters,
        !no_interp,
    );

    let cfg = RunConfig::default();
    let mut out_lines: Vec<String> = Vec::new();
    // Baseline (compile tax) is captured from the first scenario whose stem starts with "_baseline".
    let mut jit_baseline_min: Option<u64> = None;

    eprintln!(
        "\n{:<22} {:>12} {:>12} {:>12} {:>12}",
        "scenario", "jit_min_us", "jit_med_us", "interp_min_us", "output"
    );
    eprintln!("{}", "-".repeat(74));

    for f in &files {
        let name = f.file_stem().and_then(|s| s.to_str()).unwrap_or("?").to_string();
        let inst = match build_instance(&driver, &rt, &cat, f) {
            Ok(i) => i,
            Err(e) => {
                eprintln!("{:<22} BUILD-FAIL: {}", name, e);
                out_lines.push(format!("{{\"scenario\":\"{name}\",\"status\":\"build-fail\",\"error\":{:?}}}", e));
                continue;
            }
        };

        // Warm + validate output on the JIT.
        let output = match run_once(&inst, Backend::Jit, &cfg) {
            Ok(o) => String::from_utf8_lossy(&o).trim().to_string(),
            Err(e) => {
                eprintln!("{:<22} RUN-FAIL: {}", name, e);
                out_lines.push(format!("{{\"scenario\":\"{name}\",\"status\":\"run-fail\",\"error\":{:?}}}", e));
                continue;
            }
        };

        let (jit_min, jit_med) = time_runs(&inst, Backend::Jit, &cfg, warmup, iters);
        if name.starts_with("_baseline") && jit_baseline_min.is_none() {
            jit_baseline_min = Some(jit_min);
        }

        let (interp_min, interp_med) = if no_interp {
            (0, 0)
        } else {
            match run_once(&inst, Backend::TreeWalk, &cfg) {
                Ok(_) => time_runs(&inst, Backend::TreeWalk, &cfg, 1, interp_iters),
                Err(_) => (0, 0),
            }
        };

        eprintln!(
            "{:<22} {:>12.1} {:>12.1} {:>12.1} {:>12}",
            name,
            jit_min as f64 / 1000.0,
            jit_med as f64 / 1000.0,
            interp_min as f64 / 1000.0,
            output.chars().take(12).collect::<String>(),
        );

        out_lines.push(format!(
            "{{\"scenario\":\"{name}\",\"status\":\"ok\",\"output\":{:?},\
             \"jit_ns_min\":{jit_min},\"jit_ns_median\":{jit_med},\
             \"interp_ns_min\":{interp_min},\"interp_ns_median\":{interp_med},\
             \"jit_iters\":{iters},\"interp_iters\":{}}}",
            output,
            if no_interp { 0 } else { interp_iters },
        ));
    }

    // Baseline-subtracted execution estimate.
    if let Some(base) = jit_baseline_min {
        eprintln!(
            "\nJIT per-run compile+startup tax (from _baseline): {:.1} us",
            base as f64 / 1000.0
        );
        eprintln!("Estimated pure guest execution (jit_min - baseline):");
        eprintln!("{:<22} {:>16}", "scenario", "exec_est_us");
        eprintln!("{}", "-".repeat(40));
        for line in &out_lines {
            // cheap parse: pull scenario + jit_ns_min back out
            if let (Some(sn), Some(jm)) = (json_str(line, "scenario"), json_num(line, "jit_ns_min")) {
                if sn.starts_with("_baseline") {
                    continue;
                }
                let est = (jm as i64 - base as i64).max(0) as f64 / 1000.0;
                eprintln!("{:<22} {:>16.1}", sn, est);
            }
        }
    }

    let json = out_lines.join("\n");
    match std::env::var("BENCH_JSON_OUT") {
        Ok(path) => {
            std::fs::write(&path, format!("{json}\n")).expect("write BENCH_JSON_OUT");
            eprintln!("\nbench_svm: wrote {} scenarios to {path}", out_lines.len());
        }
        Err(_) => println!("{json}"),
    }
}

// Minimal JSON field extractors (our own well-formed output only).
fn json_str<'a>(line: &'a str, key: &str) -> Option<&'a str> {
    let needle = format!("\"{key}\":\"");
    let i = line.find(&needle)? + needle.len();
    let rest = &line[i..];
    let j = rest.find('"')?;
    Some(&rest[..j])
}
fn json_num(line: &str, key: &str) -> Option<u64> {
    let needle = format!("\"{key}\":");
    let i = line.find(&needle)? + needle.len();
    let rest = &line[i..];
    let j = rest.find(|c: char| !c.is_ascii_digit()).unwrap_or(rest.len());
    rest[..j].parse().ok()
}
