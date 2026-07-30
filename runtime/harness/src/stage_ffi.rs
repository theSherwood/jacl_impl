//! C-ABI bridge that runs a codegen'd staged-macro module on the SVM engine, in-process,
//! for the native JACL frontend (Phase 5 of `docs/SVM_MACRO_STAGING_PLAN.md`). This is the
//! embedding half of "run on an SVM runtime instance": `expand__node`, behind
//! `JACL_STAGE_ON_SVM`, codegens a macro body into a staged-macro module (a v9 svm-encode
//! object), encodes the argument syntax values to a `syn_wire` buffer, calls `jacl_svm_stage`,
//! and decodes the returned result wire — replacing the legacy VM's `jacl_ctx_run_closure`.
//!
//! The path mirrors `bin/stage_macro.rs` exactly (decode_unit → link vs the staging runtime →
//! `synth_manifest_start` → run with the arg wire on stdin), except the staging runtime is
//! translated **once** and cached: translation dominates the cost, and it is identical for
//! every macro.

use std::os::raw::c_int;
use std::sync::OnceLock;

use svm_ir::{LinkUnit, Module};

/// The staging-extended runtime, translated once (translation is the expensive step and is
/// the same for every macro). `link_with_manifest` consumes the module, so each call links a
/// clone. `None` if translation ever fails (reported per-call).
fn staging_runtime() -> &'static Option<(Module, Vec<(String, u32)>)> {
    static RT: OnceLock<Option<(Module, Vec<(String, u32)>)>> = OnceLock::new();
    RT.get_or_init(|| {
        // translate_runtime_staging panics on a build failure; treat that as "unavailable".
        std::panic::catch_unwind(|| {
            let t = crate::translate_runtime_staging();
            (t.module, t.exports)
        })
        .ok()
    })
}

/// Run a staged-macro module on SVM and return the result `syn_wire` bytes.
///
/// * `module_bytes` / `module_len` — the svm-encode **object** (v9) from
///   `svm_codegen_staged_macro` + `irb_to_encoded`. Own-data addresses are inline `data.self`
///   instructions that `link` resolves — no separate relocation table.
/// * `arg_wire` / `arg_len` — the argument wire (one version byte, then the N parameter
///   syntax values back-to-back), fed to the module on stdin.
/// * `out_ptr` / `out_len` — on success, receive a heap buffer of result-wire bytes; free it
///   with `jacl_svm_stage_free`.
///
/// Returns 0 on success, non-zero on error (nothing is written to the out-params on error).
///
/// # Safety
/// All pointers must be valid for their stated lengths.
#[no_mangle]
pub unsafe extern "C" fn jacl_svm_stage(
    module_bytes: *const u8,
    module_len: usize,
    arg_wire: *const u8,
    arg_len: usize,
    out_ptr: *mut *mut u8,
    out_len: *mut usize,
) -> c_int {
    if module_bytes.is_null() || module_len == 0 || out_ptr.is_null() || out_len.is_null() {
        return 1;
    }
    let module_bytes = std::slice::from_raw_parts(module_bytes, module_len);
    let stdin_bytes: &[u8] = if arg_len == 0 || arg_wire.is_null() {
        &[]
    } else {
        std::slice::from_raw_parts(arg_wire, arg_len)
    };

    match stage_run(module_bytes, stdin_bytes) {
        Ok(bytes) => {
            let mut boxed = bytes.into_boxed_slice();
            *out_len = boxed.len();
            *out_ptr = boxed.as_mut_ptr();
            std::mem::forget(boxed); // ownership transfers to C; reclaimed via jacl_svm_stage_free
            0
        }
        Err(_) => 3,
    }
}

/// Free a buffer returned by `jacl_svm_stage`.
///
/// # Safety
/// `ptr`/`len` must be exactly what a prior `jacl_svm_stage` success wrote, or null/0.
#[no_mangle]
pub unsafe extern "C" fn jacl_svm_stage_free(ptr: *mut u8, len: usize) {
    if !ptr.is_null() && len != 0 {
        drop(Box::from_raw(std::slice::from_raw_parts_mut(ptr, len)));
    }
}

fn stage_run(module_bytes: &[u8], stdin_bytes: &[u8]) -> Result<Vec<u8>, String> {
    let program = svm_encode::decode_unit(module_bytes).map_err(|e| format!("decode: {e:?}"))?;
    let (rt_module, rt_exports) = staging_runtime()
        .as_ref()
        .ok_or_else(|| "staging runtime translation unavailable".to_string())?;

    let linked = svm_ir::link_with_manifest(&[
        LinkUnit { module: rt_module.clone(), exports: rt_exports.clone(), ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![("__jacl_entry".to_string(), 0)],
            ..Default::default()
        },
    ])
    .map_err(|e| format!("link: {e:?}"))?;
    let entry = linked
        .resolve_export("__jacl_entry")
        .ok_or_else(|| "entry export missing after link".to_string())?;
    let module = svm_ir::synth_manifest_start(linked, entry, false)
        .map_err(|e| format!("powerbox: {e}"))?;

    let imports = svm_run::Imports::new()
        .provide("write", svm_run::HostCap::stdout())
        .provide("exit", svm_run::HostCap::exit())
        .provide("read", svm_run::HostCap::stdin());
    let inst = svm_run::instantiate_with_imports(module, imports)
        .map_err(|e| format!("instantiate: {e}"))?;
    let run = inst.call_with_stdin(stdin_bytes).map_err(|e| format!("run: {e}"))?;
    Ok(run.stdout)
}
