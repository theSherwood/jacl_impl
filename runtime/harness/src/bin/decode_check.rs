//! decode_check — decode a `.svmb` with the exact `temen_encode` the cdylib uses and print the
//! real error. The cdylib's `temen_run_onramp` maps any decode failure to `STATUS_DECODE_ERR`
//! (status=1); this surfaces the underlying reason (BadVersion / BadOpcode / section mismatch / …).
//!
//!   decode_check <image.svmb> [func_index]
//!
//! With a `func_index`, also dumps that function's shape (signature, block/terminator kinds, and
//! whether it contains an `unreachable` terminator) — used to characterize a tier-up decline
//! (#839: the emitted leaf `f<idx>` traps on `unreachable`).

use temen_ir::Terminator;

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("usage: decode_check <image.svmb> [func_index]");
    let func_idx: Option<usize> = args.next().and_then(|s| s.parse().ok());
    let bytes = std::fs::read(&path).expect("read image");
    println!("{}: {} bytes; magic+ver {:02x?}", path, bytes.len(), &bytes[..bytes.len().min(6)]);
    let m = match temen_encode::decode_module(&bytes) {
        Ok(m) => {
            println!("decode_module: OK ({} funcs)", m.funcs.len());
            m
        }
        Err(e) => {
            println!("decode_module: ERR {e:?}");
            return;
        }
    };
    // #1009 paged predicate: does the coop tier-up run this guest PAGED (page-checked, honors Ro +
    // grow) or non-paged (scalar `mapped` extent)? Mirrors temen_coop_open's decision.
    {
        let scalar = |t: &temen_ir::ValType| {
            matches!(
                t,
                temen_ir::ValType::I32
                    | temen_ir::ValType::I64
                    | temen_ir::ValType::F32
                    | temen_ir::ValType::F64
            )
        };
        let max_slots = (temen_wasm_jit::ENV_CELL_BYTES - 16) / 8;
        let all_shimmable = m.funcs.iter().all(|f| {
            f.params.iter().all(&scalar)
                && f.results.iter().all(&scalar)
                && f.params.len().max(f.results.len()) <= max_slots
        });
        let ro_data = m.data.iter().filter(|d| d.readonly).count();
        let uses_unmap_protect = temen_wasm_jit::module_uses_unmap_protect(&m);
        let paged = all_shimmable && (ro_data > 0 || uses_unmap_protect);
        println!(
            "paged predicate: all_shimmable={all_shimmable} readonly_data_segs={ro_data} uses_unmap_protect={uses_unmap_protect} => PAGED={paged}"
        );
    }
    if let Some(i) = func_idx {
        let Some(f) = m.funcs.get(i) else {
            println!("func {i}: OUT OF RANGE ({} funcs)", m.funcs.len());
            return;
        };
        println!("func {i}: params={:?} results={:?} blocks={}", f.params, f.results, f.blocks.len());
        let mut has_unreachable = false;
        for (bi, b) in f.blocks.iter().enumerate() {
            let term = match &b.term {
                Terminator::Unreachable => {
                    has_unreachable = true;
                    "Unreachable".to_string()
                }
                other => format!("{other:?}").split_whitespace().next().unwrap_or("?").to_string(),
            };
            let kinds: Vec<String> = b
                .insts
                .iter()
                .map(|i| match i {
                    temen_ir::Inst::CallImport { import, .. } => format!(
                        "CallImport<{}>",
                        m.imports.get(*import as usize).map(|im| im.name.as_str()).unwrap_or("?")
                    ),
                    other => format!("{other:?}").split(['(', ' ', '{']).next().unwrap_or("?").to_string(),
                })
                .collect();
            println!("  block {bi}: term={} :: {}", term, kinds.join(", "));
        }
        println!(
            "func {i}: {} — {}",
            if has_unreachable { "HAS unreachable terminator" } else { "no unreachable terminator" },
            if f.blocks.len() == 1 && has_unreachable { "single-block stub (likely --stub-externs)" } else { "multi-path body" }
        );
    }
}
