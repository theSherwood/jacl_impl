//! #290's acceptance: an application builds a JACL program into its own temen program with
//! `examples/app/build.sh`, beside a Nim unit and a C unit of its own, and the program runs on the
//! tree-walker, the bytecode engine and the JIT. Its `extern`s call the Nim and the C with 64-bit
//! integers and pointers into JACL buffers, and its channel runs on unir's edges.
//!
//! The build needs nimony, on `PATH` or in `NIMONY_BIN` and `NIM_BIN` (vendor/temen's
//! `scripts/ci/provision-nimony.sh` builds it). Without it the test prints SKIP and passes, as
//! temen's own Nim end-to-end tests do, unless `JACL_REQUIRE_NIMONY` is set. CI's job for this test
//! sets it, so there a missing toolchain fails.
use std::process::Command;

/// The `PATH` `build.sh` runs nimony under, if nimony is on it.
fn nimony_path() -> Option<String> {
    let mut path = std::env::var("PATH").unwrap_or_default();
    for var in ["NIM_BIN", "NIMONY_BIN"] {
        if let Ok(dir) = std::env::var(var) {
            path = format!("{dir}:{path}");
        }
    }
    let found = path.split(':').any(|d| !d.is_empty() && std::path::Path::new(d).join("nimony").is_file());
    found.then_some(path)
}

#[test]
fn an_application_links_a_jacl_program_beside_its_nim_and_c() {
    let Some(path) = nimony_path() else {
        assert!(std::env::var_os("JACL_REQUIRE_NIMONY").is_none(), "JACL_REQUIRE_NIMONY: no nimony on PATH");
        eprintln!("SKIP: nimony not found (set NIMONY_BIN and NIM_BIN, or put nimony on PATH)");
        return;
    };
    let out = std::env::temp_dir().join(format!("jacl_app_{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&out);
    let script = concat!(env!("CARGO_MANIFEST_DIR"), "/../../examples/app/build.sh");
    let status = Command::new("bash")
        .arg(script)
        .arg(&out)
        .env("PATH", &path)
        // The bin this test was built with, so the script runs no cargo inside the test's own.
        .env("JACL_UNITS", env!("CARGO_BIN_EXE_jacl_units"))
        .status()
        .expect("run examples/app/build.sh");
    assert!(status.success(), "examples/app/build.sh failed");

    let bytes = std::fs::read(out.join("app.temen")).expect("read app.temen");
    let _ = std::fs::remove_dir_all(&out);
    let app = temen_encode::decode_module(&bytes).expect("decode app.temen");
    temen_verify::verify_module(&app).expect("verify app.temen");
    // `Backend::Bytecode` falls back to the tree-walker for a module outside its subset.
    assert!(
        temen_interp::bytecode::SharedProgram::compile(&app).is_some(),
        "app.temen is outside the bytecode engine's subset, so its bytecode run would be a tree-walk"
    );
    // What the program asks its host for: Nim's streams, exit and heap on the capability edge, the
    // runtime's `read` and `write`, and the region unir's channel edges are made in.
    let imports = temen_run::Imports::new()
        .provide("stream_write", temen_run::HostCap::stdout())
        .provide("stream_read", temen_run::HostCap::stdin())
        .provide("stderr", temen_run::HostCap::stdout())
        .provide("exit", temen_run::HostCap::exit())
        .provide("vm_map", temen_run::HostCap::memory(0))
        .provide("vm_page_size", temen_run::HostCap::memory(3))
        .provide("write", temen_run::HostCap::stdout())
        .provide("read", temen_run::HostCap::stdin())
        .provide("vm_region_create", temen_run::HostCap::memory(5));
    let inst = temen_run::instantiate_with_imports(app, imports).expect("instantiate app.temen");
    for backend in [temen_run::Backend::TreeWalk, temen_run::Backend::Bytecode, temen_run::Backend::Jit] {
        let run = inst
            .run(backend, &temen_run::RunConfig::default())
            .unwrap_or_else(|e| panic!("run app.temen on {backend:?}: {e}"));
        assert_eq!(
            String::from_utf8_lossy(&run.stdout),
            "10000000000000\n11000000007\non unir\n",
            "{backend:?}"
        );
        assert!(
            matches!(run.outcome, temen_run::Outcome::Returned(ref v) if v[..] == [temen_run::Value::I32(0)]),
            "{backend:?}: {:?}",
            run.outcome
        );
    }
}
