//! A JACL program in a terminal pane (#203; theSherwood/unir#102, decision 81): unir's store vat
//! (`runtime/unir/unir_store_vat.ll`) run as `shell SOURCE LOG2 ROWS COLS`. The vat is the root; it
//! spawns the terminal vat and the dangling view from its own image (granted as `store-self`), and
//! the program from the client image (granted as `store-client`), granted the store as `store` and
//! the pane as `pane`. Only the terminal vat holds the terminal: the root's stdin after the vat's
//! command, and its stdout. The store lives in `dir/store.log`.

use std::path::Path;

use temen_run::{Backend, RunConfig};

use crate::node::store_images;

/// A pane's size, in character cells.
#[derive(Clone, Copy, Debug)]
pub struct Size {
    pub rows: u16,
    pub cols: u16,
}

/// Runs `client` (a JACL program compiled as a child image, its window `2^log2` bytes) in a pane
/// of `size` on `backend`, and returns everything the terminal was sent, then the vat's report.
/// `setup` sees the root's host before the run, to attach the terminal
/// (`Host::set_stdin_source`, `Host::set_stdout_tee`).
pub fn run(
    client: &temen_ir::Module,
    log2: u8,
    backend: Backend,
    dir: &Path,
    size: Size,
    setup: &mut dyn FnMut(&mut temen_interp::Host),
) -> String {
    let (vat, helper) = store_images();
    let vat_log2 = vat.memory.map(|m| m.size_log2).expect("a window") + 8;
    let inst = temen_run::instantiate(vat).expect("instantiate the store vat");
    let config = RunConfig {
        memory_size_log2: Some(vat_log2),
        stdin: format!("shell 1 {log2} {} {}", size.rows, size.cols).into_bytes(),
        ..Default::default()
    };
    let mut grant = |h: &mut temen_interp::Host| {
        let m = h.grant_module(client);
        h.register_cap_name("store-client", m);
        let m = h.grant_module(&helper);
        h.register_cap_name("store-self", m);
        h.grant_instantiator(0, 1 << log2);
        h.grant_budget(-1, -1, -1);
        setup(h);
    };
    let fs = temen_run::fs::host_fs(dir.to_path_buf());
    match inst.run_with_caps_and_host(backend, &config, &[("fs", fs)], Some(&mut grant)) {
        Ok(run) => String::from_utf8_lossy(&run.stdout).into_owned(),
        Err(e) => format!("run failed: {e}\n"),
    }
}
