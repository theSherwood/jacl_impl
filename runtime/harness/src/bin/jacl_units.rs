//! `jacl_units <prog.jacl> <outdir>` writes the link units an application links a JACL program into
//! its own temen program with (#290). The application's linker, `temen-link`, links them as
//! libraries beside its own units, and its code calls `__jacl_entry` to run the program:
//!
//! - `<outdir>/jaclrt.temeno`: the runtime with its unir layer, as a link unit
//!   ([`jacl_runtime_harness::translate_runtime`]). Its calls into unir's C ABI stay symbols.
//! - `<outdir>/<prog>.temeno`: the program's object. It exports `__jacl_entry`, which runs the
//!   program's top level on the runtime's scheduler and returns its value.
//! - `<outdir>/unir_cabi.temeno`: unir's C ABI, the unit the runtime was built against
//!   (`runtime/unir/UNIR_REV`). The application links this one and no other copy, so one unit and
//!   one heap serve both.
//!
//! `examples/app/build.sh` is an application's build with them.
use std::path::Path;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let [prog, out] = args.as_slice() else {
        eprintln!("usage: jacl_units <prog.jacl> <outdir>");
        std::process::exit(2)
    };
    let (prog, out) = (Path::new(prog), Path::new(out));
    let stem = prog.file_stem().and_then(|s| s.to_str()).unwrap_or("prog");
    std::fs::create_dir_all(out).unwrap_or_else(|e| panic!("{}: {e}", out.display()));

    let program = jacl_runtime_harness::emit_program(prog).unwrap_or_else(|e| {
        eprintln!("jacl_units: {e}");
        std::process::exit(1)
    });
    write(&out.join(format!("{stem}.temeno")), &temen_encode::encode_unit(&program));
    let rt = jacl_runtime_harness::translate_runtime();
    write(&out.join("jaclrt.temeno"), &temen_encode::encode_unit(&rt.module));
    let unit = concat!(env!("CARGO_MANIFEST_DIR"), "/../unir/unir_cabi.temeno");
    let bytes = std::fs::read(unit).unwrap_or_else(|e| panic!("{unit}: {e}"));
    write(&out.join("unir_cabi.temeno"), &bytes);
}

fn write(path: &Path, bytes: &[u8]) {
    std::fs::write(path, bytes).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
}
