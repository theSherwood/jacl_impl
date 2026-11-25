//! unir's C ABI linked as its committed unit (jacl_impl#267): a call into it that goes through the
//! unit's static function pointers runs right on every engine.

use jacl_runtime_harness::{link, translate_c_unit, translate_runtime};
use temen_interp::Value;
use temen_ir::LinkUnit;
use temen_run::{Backend, HostCap, Imports, Outcome, RunConfig};

#[test]
fn a_call_through_the_units_function_pointers_runs_on_every_engine() {
    // `unir_sync_print` formats with core::fmt, which calls through vtables in the unit's data. A
    // whole-module translation entered with no `main` called the wrong function through each of
    // them (theSherwood/temen#2308); a link unit records them.
    let rt = translate_runtime();
    let test = translate_c_unit(concat!(env!("CARGO_MANIFEST_DIR"), "/../tests/unir_sync_print.c"));
    let linked = link(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit { module: test.module, exports: test.exports, ..Default::default() },
    ])
    .expect("link the test with the runtime and unir's C ABI");
    let entry = linked.resolve_export("unir_sync_print_test").expect("the test's entry");
    let image = temen_ir::synth_manifest_start(linked, entry, false).expect("entry");
    temen_verify::verify_module(&image).expect("verify");
    assert!(
        temen_interp::bytecode::SharedProgram::compile(&image).is_some(),
        "the image is outside the bytecode engine's subset, so its bytecode run would be a tree-walk"
    );
    // The runtime's manifest, as every harness grants it.
    let imports = Imports::new()
        .provide("write", HostCap::stdout())
        .provide("read", HostCap::stdin())
        .provide("exit", HostCap::exit())
        .provide("stdin", HostCap::stdin())
        .provide("vm_region_create", HostCap::memory(5));
    let inst = temen_run::instantiate_with_imports(image, imports).expect("instantiate");
    for backend in [Backend::TreeWalk, Backend::Bytecode, Backend::Jit] {
        let run = inst
            .run_with_caps(backend, &RunConfig::default(), &[])
            .unwrap_or_else(|e| panic!("{backend:?}: {e}"));
        let text = String::from_utf8_lossy(&run.stdout);
        assert!(
            matches!(run.outcome, Outcome::Returned(ref v) if v[..] == [Value::I64(0)]),
            "{backend:?}: {:?}, printing {text:?}",
            run.outcome
        );
        assert!(
            text.starts_with("Sync { m: Pull(Puller { name: \"doc\""),
            "{backend:?}: {text:?}"
        );
    }
}
