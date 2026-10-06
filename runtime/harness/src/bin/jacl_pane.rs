//! jacl-pane — a JACL program in a terminal pane, on this terminal (#203; theSherwood/unir#102):
//!
//! ```text
//! jacl_pane [--dir DIR] [--engine E] [PROGRAM.jacl]
//! ```
//!
//! Runs unir's store vat in its `shell` mode with the program (`tests/services/pane.jacl` unless
//! another is named) as the client of a pane over this terminal. The terminal is put in raw mode
//! for the run and restored after; its bytes go to temen as they are typed, and only unir's
//! terminal vat reads them and writes to it. The store lives in `DIR` (`jacl-pane` in the current
//! directory unless given). temen's JIT runs it unless `--engine` names `tree-walk` or
//! `bytecode`.

use std::io::{Read, Write};
use std::path::PathBuf;
use std::process::{Command, Stdio};

use jacl_runtime_harness::pane;

const PANE_JACL: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/services/pane.jacl");

fn die(msg: &str) -> ! {
    eprintln!("jacl-pane: {msg}");
    std::process::exit(1);
}

/// `stty ARGS` on this terminal; its output.
fn stty(args: &[&str]) -> Option<String> {
    let out = Command::new("stty")
        .args(args)
        .stdin(Stdio::inherit())
        .output()
        .ok()?;
    out.status
        .success()
        .then(|| String::from_utf8_lossy(&out.stdout).trim().to_string())
}

fn main() {
    let mut dir = PathBuf::from("jacl-pane");
    let mut backend = temen_run::Backend::Jit;
    let mut program = PathBuf::from(PANE_JACL);
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--dir" => {
                dir = PathBuf::from(args.next().unwrap_or_else(|| die("--dir needs a path")))
            }
            "--engine" => {
                backend = match args.next().as_deref() {
                    Some("jit") => temen_run::Backend::Jit,
                    Some("tree-walk") => temen_run::Backend::TreeWalk,
                    Some("bytecode") => temen_run::Backend::Bytecode,
                    _ => die("--engine is jit, tree-walk or bytecode"),
                }
            }
            p => program = PathBuf::from(p),
        }
    }
    std::fs::create_dir_all(&dir).unwrap_or_else(|e| die(&format!("{}: {e}", dir.display())));
    eprintln!("jacl-pane: compiling {}", program.display());
    let (client, log2) = jacl_runtime_harness::child_image(&program).unwrap_or_else(|e| die(&e));
    let size = stty(&["size"])
        .and_then(|s| {
            let mut it = s.split_whitespace().map(|n| n.parse::<u16>().ok());
            Some(pane::Size {
                rows: it.next()??,
                cols: it.next()??,
            })
        })
        .unwrap_or(pane::Size { rows: 24, cols: 80 });
    let saved = stty(&["-g"]).unwrap_or_else(|| die("stdin is not a terminal"));
    stty(&["raw", "-echo"]).unwrap_or_else(|| die("could not put the terminal in raw mode"));
    // The terminal is read on a thread of its own, so a read never waits on it: with nothing typed
    // the source answers "not yet" (temen#2019), and the terminal vat polls.
    let (tx, bytes) = std::sync::mpsc::channel::<Vec<u8>>();
    std::thread::spawn(move || {
        let mut buf = [0u8; 256];
        loop {
            let n = std::io::stdin().lock().read(&mut buf).unwrap_or(0);
            if n == 0 || tx.send(buf[..n].to_vec()).is_err() {
                return;
            }
        }
    });
    let mut bytes = Some(bytes);
    let mut terminal = |h: &mut temen_interp::Host| {
        h.set_stdout_tee(Box::new(|b| {
            let mut out = std::io::stdout().lock();
            let _ = out.write_all(b);
            let _ = out.flush();
        }));
        let bytes = bytes.take().expect("one run");
        h.set_stdin_source(Box::new(move || match bytes.try_recv() {
            Ok(b) => Some(b),
            Err(std::sync::mpsc::TryRecvError::Empty) => None,
            Err(std::sync::mpsc::TryRecvError::Disconnected) => Some(Vec::new()),
        }));
    };
    let _ = pane::run(&client, log2, backend, &dir, size, &mut terminal);
    stty(&[&saved]);
    println!();
}
