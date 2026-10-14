//! jacl-shell-image — the JACL shell's image, for unir to vendor (theSherwood/unir#106):
//!
//! ```text
//! jacl_shell_image OUT
//! ```
//!
//! Builds `tests/services/shell.jacl` as unir's store vat spawns it, a child with the compiler
//! linked in ([`jacl_runtime_harness::repl::shell_image`]), and writes it to OUT encoded
//! (`temen_encode`). unir's stage 7 session runs it in the shell's seat, with the profile unir
//! commits to the store; it records this repository's commit beside it.

use std::path::Path;

const SHELL_JACL: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/services/shell.jacl");

fn main() {
    let Some(out) = std::env::args().nth(1) else {
        eprintln!("usage: jacl_shell_image OUT");
        std::process::exit(2);
    };
    let (image, log2) = jacl_runtime_harness::repl::shell_image(Path::new(SHELL_JACL), true)
        .unwrap_or_else(|e| {
            eprintln!("jacl_shell_image: {e}");
            std::process::exit(1);
        });
    let bytes = temen_encode::encode_module(&image);
    std::fs::write(&out, &bytes).unwrap_or_else(|e| {
        eprintln!("jacl_shell_image: {out}: {e}");
        std::process::exit(1);
    });
    eprintln!("{out}: {} bytes, a window of 2^{log2}", bytes.len());
}
