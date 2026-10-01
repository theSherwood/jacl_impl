//! unir-node — one of two machines editing one document, from a terminal (theSherwood/unir#62,
//! docs/UNIR_SERVICES.md):
//!
//! ```text
//! unir_node --listen ADDR  [--dir DIR] [--engine E] [EDITOR.jacl]     # machine 0: waits for machine 1
//! unir_node --connect ADDR [--dir DIR] [--engine E] [EDITOR.jacl]     # machine 1: joins machine 0
//! ```
//!
//! Each runs unir's store vat in peer mode with a JACL editor (`tests/services/ed.jacl` unless
//! another is named) as its client, reading the terminal a line at a time and printing to it as it
//! goes. The two vats replicate ref `doc` over TCP, machine 1 merging what both type at once. Each
//! keeps its store in `DIR` (`unir-node-0` or `unir-node-1` in the current directory unless
//! given), so the document is there when both run again (theSherwood/unir#86). A machine's run
//! ends once both editors have quit; it then prints its report (its head's id and text, its
//! editor's and puller's statuses, its senders'). temen's JIT runs it unless `--engine` names
//! `tree-walk` or `bytecode`: an edit shown takes well under 10 ms there, against ~70 ms on the
//! tree-walker and ~40 ms on the bytecode engine, for ~7 s more compiling at the start.

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
    let mut backend = temen_run::Backend::Jit;
    let mut dir: Option<PathBuf> = None;
    let mut editor = PathBuf::from(ED_JACL);
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--listen" => listen = Some(args.next().unwrap_or_else(|| die("--listen needs ADDR"))),
            "--connect" => connect = Some(args.next().unwrap_or_else(|| die("--connect needs ADDR"))),
            "--dir" => dir = Some(PathBuf::from(args.next().unwrap_or_else(|| die("--dir needs DIR")))),
            "--engine" => {
                backend = match args.next().as_deref() {
                    Some("jit") => temen_run::Backend::Jit,
                    Some("tree-walk") => temen_run::Backend::TreeWalk,
                    Some("bytecode") => temen_run::Backend::Bytecode,
                    _ => die("--engine needs jit, tree-walk or bytecode"),
                }
            }
            "-h" | "--help" => {
                println!("usage: unir_node (--listen ADDR | --connect ADDR) [--dir DIR] [--engine E] [EDITOR.jacl]");
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
    let dir = dir.unwrap_or_else(|| PathBuf::from(format!("unir-node-{}", link.role)));
    std::fs::create_dir_all(&dir).unwrap_or_else(|e| die(&format!("{}: {e}", dir.display())));
    eprintln!(
        "unir-node: connected; the store is in {}. Type a line to add it; :i N, :c N, :d N, :p, :q.",
        dir.display()
    );
    // The terminal is read a line at a time on a thread of its own, so the editor's read never
    // waits on it: with no line yet the source answers "nothing yet" (temen#2019), and the editor
    // goes on showing the other machine's edits as they arrive (#195).
    let (tx, lines) = std::sync::mpsc::channel::<Vec<u8>>();
    std::thread::spawn(move || loop {
        let mut line = String::new();
        let n = std::io::stdin().lock().read_line(&mut line).unwrap_or(0);
        if n == 0 || tx.send(line.into_bytes()).is_err() {
            return;
        }
    });
    let mut lines = Some(lines);
    let mut terminal = |h: &mut temen_interp::Host| {
        h.set_stdout_tee(Box::new(|b| {
            let mut out = std::io::stdout().lock();
            let _ = out.write_all(b);
            let _ = out.flush();
        }));
        let lines = lines.take().expect("one run");
        h.set_stdin_source(Box::new(move || match lines.try_recv() {
            Ok(line) => Some(line),
            Err(std::sync::mpsc::TryRecvError::Empty) => None,
            Err(std::sync::mpsc::TryRecvError::Disconnected) => Some(Vec::new()),
        }));
    };
    // The terminal saw everything as it was printed, the report included.
    let _ = node::run(&images, link, backend, Some(&dir), &mut terminal);
}
