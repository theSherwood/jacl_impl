//! unir-node — one of two machines editing one document, from a terminal (theSherwood/unir#62,
//! docs/UNIR_SERVICES.md):
//!
//! ```text
//! unir_node --pair DIR0 DIR1 [--engine E]                                           # once: both machines' keys
//! unir_node --listen ADDR  [--dir DIR] [--engine E] [--allow-remote] [EDITOR.jacl]  # machine 0: waits for machine 1
//! unir_node --connect ADDR [--dir DIR] [--engine E] [--allow-remote] [EDITOR.jacl]  # machine 1: joins machine 0
//! ```
//!
//! `--pair` makes two machines' keys, each in its directory as `keys`: a machine's keystore,
//! holding the other's delegation of its store, and the other machine's key (theSherwood/unir#71).
//! Copy machine 1's to wherever it runs. The machines authenticate each other with them and
//! encrypt what crosses. `ADDR` must be loopback unless `--allow-remote` is given, so that
//! reaching another host is an explicit choice (theSherwood/unir#82).
//!
//! Each runs unir's store vat in peer mode with a JACL editor (`tests/services/ed.jacl` unless
//! another is named) as its client, reading the terminal a line at a time and printing to it as it
//! goes. The two vats replicate ref `doc` over TCP, machine 1 merging what both type at once. Each
//! keeps its store and its keys in `DIR` (`unir-node-0` or `unir-node-1` in the current directory
//! unless given), so the document is there when both run again (theSherwood/unir#86). A machine's run
//! ends once both editors have quit; it then prints its report (its head's id and text, its
//! editor's and puller's statuses, its senders'). temen's JIT runs it unless `--engine` names
//! `tree-walk` or `bytecode`: an edit shown takes well under 10 ms there, against ~70 ms on the
//! tree-walker and ~40 ms on the bytecode engine, for ~7 s more compiling at the start.

use std::io::{BufRead, Write};
use std::net::TcpListener;
use std::path::PathBuf;

use jacl_runtime_harness::node;

const ED_JACL: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/services/ed.jacl");

fn die(msg: &str) -> ! {
    eprintln!("unir-node: {msg}");
    std::process::exit(1);
}

/// Makes two machines' keys and keeps each in its directory.
fn pair(dirs: [PathBuf; 2], backend: temen_run::Backend) {
    let (vat, _) = node::store_images();
    let keys = node::pair(&vat, backend).unwrap_or_else(|e| die(&e));
    for (role, (dir, k)) in dirs.iter().zip(&keys).enumerate() {
        std::fs::create_dir_all(dir).unwrap_or_else(|e| die(&format!("{}: {e}", dir.display())));
        node::keep(dir, role as u8, k).unwrap_or_else(|e| die(&e));
    }
    eprintln!(
        "unir-node: paired; machine 0's keys are in {}, machine 1's in {}.",
        dirs[0].display(),
        dirs[1].display()
    );
}

fn main() {
    let mut paired: Option<[PathBuf; 2]> = None;
    let mut listen: Option<String> = None;
    let mut connect: Option<String> = None;
    let mut backend = temen_run::Backend::Jit;
    let mut dir: Option<PathBuf> = None;
    let mut editor = PathBuf::from(ED_JACL);
    let mut allow_remote = false;
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--pair" => {
                let mut dir = || PathBuf::from(args.next().unwrap_or_else(|| die("--pair needs DIR0 DIR1")));
                paired = Some([dir(), dir()]);
            }
            "--listen" => listen = Some(args.next().unwrap_or_else(|| die("--listen needs ADDR"))),
            "--connect" => connect = Some(args.next().unwrap_or_else(|| die("--connect needs ADDR"))),
            "--allow-remote" => allow_remote = true,
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
                println!(
                    "usage: unir_node --pair DIR0 DIR1 [--engine E]\n       unir_node (--listen ADDR | --connect ADDR) [--dir DIR] [--engine E] [--allow-remote] [EDITOR.jacl]"
                );
                return;
            }
            other => editor = PathBuf::from(other),
        }
    }
    if let Some(dirs) = paired {
        return pair(dirs, backend);
    }
    for addr in listen.iter().chain(&connect) {
        node::permitted(addr, allow_remote).unwrap_or_else(|e| die(&e));
    }
    let role = match (&listen, &connect) {
        (Some(_), None) => 0,
        (None, Some(_)) => 1,
        _ => die("give exactly one of --listen ADDR and --connect ADDR"),
    };
    let dir = dir.unwrap_or_else(|| PathBuf::from(format!("unir-node-{role}")));
    let keys = node::kept(&dir, role)
        .unwrap_or_else(|e| die(&format!("{e}; make both machines' keys first, with --pair DIR0 DIR1")));
    eprintln!("unir-node: compiling {} and the store vat…", editor.display());
    let (editor, editor_log2) = jacl_runtime_harness::child_image(&editor).unwrap_or_else(|e| die(&e));
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
        _ => unreachable!("one of them, checked above"),
    };
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
        h.set_stream_tee(jacl_runtime_harness::stdout_tee(|b| {
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
    let _ = node::run(&images, link, keys, backend, Some(&dir), &mut terminal);
}
