//! `link_runtime <out.temen>` writes the runtime a host links programs against as one library
//! (`runtime/build.sh`'s `jaclrt.temen`, which the playground ships): jaclrt translated as a link
//! unit and linked with unir's C ABI, each C ABI function the runtime cannot reach a trap
//! ([`jacl_runtime_harness::link`], #267).
fn main() {
    let out = std::env::args()
        .nth(1)
        .unwrap_or_else(|| {
            eprintln!("usage: link_runtime <out.temen>");
            std::process::exit(2)
        });
    let rt = jacl_runtime_harness::translate_runtime();
    let linked = jacl_runtime_harness::link(&[temen_ir::LinkUnit {
        module: rt.module,
        exports: rt.exports,
        ..Default::default()
    }])
    .unwrap_or_else(|e| panic!("link the runtime with unir's C ABI: {e:?}"));
    std::fs::write(&out, temen_encode::encode_module(&linked))
        .unwrap_or_else(|e| panic!("{out}: {e}"));
}
