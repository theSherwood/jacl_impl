//! unir-node — one of two machines editing one document, from a terminal (theSherwood/unir#62,
//! docs/UNIR_SERVICES.md):
//!
//! ```text
//! unir_node --listen ADDR  [--jit] [EDITOR.jacl]     # machine 0: waits for machine 1
//! unir_node --connect ADDR [--jit] [EDITOR.jacl]     # machine 1: joins machine 0
//! ```
//!
//! Each runs unir's store vat in peer mode with a JACL editor (`tests/services/ed.jacl` unless
//! another is named) as its client, reading the terminal a line at a time and printing to it as it
//! goes. The two vats replicate ref `doc` over TCP, machine 1 merging what both type at once. A
//! machine's run ends once both editors have quit; it then prints its report (its head's id and
//! text, its editor's and puller's statuses, its senders'). The tree-walker runs it unless `--jit`
//! asks for the JIT: there a child's short timed waits last ~20 ms each (temen#2012), and the
//! JACL runtime's scheduler waits in 1 ms ticks, so an edit takes 110–150 ms against 30–40 ms on
//! the tree-walker. The bytecode engine, which runs every vat on one thread, cannot serve a
//! terminal that waits for a person.

use std::io::{BufRead, Write};
use std::net::TcpListener;
use std::path::PathBuf;
use std::process::Command;

use jacl_runtime_harness::node;
use temen_ir::LinkUnit;

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");
const ED_JACL: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/services/ed.jacl");

fn die(msg: &str) -> ! {
    eprintln!("unir-node: {msg}");
    std::process::exit(1);
}

/// The editor at `path`, compiled as a child image of the store vat (the frontend and codegen's
/// emit driver, built with gcc as `jacl_temen`'s is, linked with the translated runtime), and its
/// window's size, log2.
fn editor_image(path: &PathBuf) -> (temen_ir::Module, u8) {
    let driver = std::env::temp_dir().join(format!("unir_node_emit_{}", std::process::id()));
    let status = Command::new("gcc")
        .args(["-O0", "-w", "-D_DEFAULT_SOURCE"])
        .arg(format!("{ROOT}/codegen/tests/emit_jacl.c"))
        .arg(format!("{ROOT}/codegen/codegen.c"))
        .arg(format!("{ROOT}/codegen/irbuilder.c"))
        .arg("-o")
        .arg(&driver)
        .args(["-lm", "-lpthread"])
        .status()
        .unwrap_or_else(|e| die(&format!("gcc: {e}")));
    if !status.success() {
        die("gcc failed to build the codegen driver");
    }
    let out = Command::new(&driver).arg("--file").arg(path).output();
    let _ = std::fs::remove_file(&driver);
    let out = out.unwrap_or_else(|e| die(&format!("emit driver: {e}")));
    if !out.status.success() {
        die(&format!("{}: {}", path.display(), String::from_utf8_lossy(&out.stderr)));
    }
    let program = jacl_runtime_harness::decode_emitted(&out.stdout)
        .unwrap_or_else(|e| die(&format!("decode emitted IR: {e}")));
    let rt = jacl_runtime_harness::translate_runtime();
    let linked = temen_ir::link_with_manifest(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![("__jacl_entry".to_string(), 0)],
            ..Default::default()
        },
    ])
    .unwrap_or_else(|e| die(&format!("link: {e:?}")));
    let entry = linked.resolve_export("__jacl_entry").unwrap_or_else(|| die("no entry after link"));
    let image = temen_ir::synth_manifest_child_start(linked, entry, false)
        .unwrap_or_else(|e| die(&format!("child entry: {e}")));
    let log2 = image.memory.map(|m| m.size_log2).unwrap_or_else(|| die("the editor has no window"));
    (image, log2)
}

fn main() {
    let mut listen: Option<String> = None;
    let mut connect: Option<String> = None;
    let mut jit = false;
    let mut editor = PathBuf::from(ED_JACL);
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--listen" => listen = Some(args.next().unwrap_or_else(|| die("--listen needs ADDR"))),
            "--connect" => connect = Some(args.next().unwrap_or_else(|| die("--connect needs ADDR"))),
            "--jit" => jit = true,
            "-h" | "--help" => {
                println!("usage: unir_node (--listen ADDR | --connect ADDR) [--jit] [EDITOR.jacl]");
                return;
            }
            other => editor = PathBuf::from(other),
        }
    }
    eprintln!("unir-node: compiling {} and the store vat…", editor.display());
    let (editor, editor_log2) = editor_image(&editor);
    let (vat, helper) = node::store_images();
    let images = node::Images { vat, helper, editor, editor_log2 };
    let link = match (listen, connect) {
        (Some(addr), None) => {
            let listener =
                TcpListener::bind(&addr).unwrap_or_else(|e| die(&format!("listen on {addr}: {e}")));
            let at = listener.local_addr().map_or(addr, |a| a.to_string());
            eprintln!("unir-node: machine 0, listening on {at}; waiting for machine 1…");
            node::accept(&listener).unwrap_or_else(|e| die(&format!("accept: {e}")))
        }
        (None, Some(addr)) => {
            eprintln!("unir-node: machine 1, connecting to {addr}…");
            node::connect(addr.as_str()).unwrap_or_else(|e| die(&format!("connect to {addr}: {e}")))
        }
        _ => die("give exactly one of --listen ADDR and --connect ADDR"),
    };
    eprintln!("unir-node: connected. Type a line to add it; :i N, :c N, :d N, :p, :q.");
    let backend = if jit { temen_run::Backend::Jit } else { temen_run::Backend::TreeWalk };
    let mut terminal = |h: &mut temen_interp::Host| {
        h.set_stdout_tee(Box::new(|b| {
            let mut out = std::io::stdout().lock();
            let _ = out.write_all(b);
            let _ = out.flush();
        }));
        h.set_stdin_source(Box::new(|| {
            let mut line = String::new();
            match std::io::stdin().lock().read_line(&mut line) {
                Ok(_) => line.into_bytes(),
                Err(_) => Vec::new(),
            }
        }));
    };
    // The terminal saw everything as it was printed, the report included.
    let _ = node::run(&images, link, backend, &mut terminal);
}
