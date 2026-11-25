#!/bin/sh
# Produce the JACL runtime artifacts for the TEMEN backend:
#
#   1. jaclrt.temen  — the TEMEN-IR module programs link against: the runtime C, built with
#                    JACL_UNIR so channels run on Unir edges (docs/UNIR_CHANNELS.md), translated
#                    as a link unit and linked with unir's C ABI, the vendored unit
#                    (unir/unir_cabi.temeno), by the harness's `link_runtime`. Each function of the
#                    C ABI the runtime cannot reach links as a trap. Exports ride in-band in the
#                    module, so a program module resolves its `call.import "jacl_*"` through
#                    temen_ir::link over the module's own export table (temen_link_run reads
#                    `module.exports` directly).
#
# This is the separate-artifact path (TEMEN_BACKEND_PHASE2.md P2.0): compile the
# runtime once here, link many JACL-emitted program modules against it. Requires
# clang (LLVM 18, to match the temen submodule) and libLLVM-18 dev (for temen-llvm).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${DIR}/build"
TEMEN_LLVM="${DIR}/../vendor/temen/crates/temen-llvm"
mkdir -p "$OUT"

# The LLVM -> TEMEN on-ramp, built **release**. This used to be `cargo run` with no profile,
# i.e. debug, which cost twice over: the translator itself runs ~6x slower (measured on the
# self-hosted compiler card, 6131ms -> 1030ms, byte-identical output), and it meant CI compiled
# the LLVM-bindings crate in *two* profiles, because codegen/selfhost/build_compiler_temen.sh
# has always used release. One profile, one compile, same bytes. Built on every run unless
# TEMEN_LLVM_TRANSLATE names one: a binary left from before vendor/temen moved translates for the old
# pin (one rejected `__vm_kill`), and cargo makes an up-to-date build a no-op.
TRANSLATE="${TEMEN_LLVM_TRANSLATE:-$TEMEN_LLVM/target/release/temen-llvm-translate}"
if [ -z "${TEMEN_LLVM_TRANSLATE:-}" ]; then
  echo "=== building temen-llvm-translate (release) from vendor/temen ==="
  ( cd "$TEMEN_LLVM" && cargo build --release --bin temen-llvm-translate )
fi

# 1. runtime C -> a link unit -> linked with unir's C ABI, through the harness's one route
#    (runtime/harness/src/lib.rs: `translate_runtime`, `link`), which the harness's tests and
#    precompiled cards take too. The debug build is the one demo/temen/build_assets.sh's
#    `emit_temen` uses.
( cd "$DIR/harness" && cargo run --quiet --bin link_runtime -- "$OUT/jaclrt.temen" )
echo "runtime module:   $OUT/jaclrt.temen (with unir $(cat "$DIR/unir/UNIR_REV"))"

# 2. staging runtime (jaclrt + the syn_rt macro-I/O glue) as one module: the library jacl_emit.wasm's
#    macro staging links each codegen'd macro body against, so `synrt_read_arg`/`synrt_write_result`
#    (and the jacl_* runtime) resolve by name. Same scalar subset as jaclrt.temen.
STAGING="${DIR}/../codegen/selfhost/macro_staging"
clang -O2 -emit-llvm -c -DNDEBUG -fno-vectorize -fno-slp-vectorize \
  -I "$DIR" -I "$STAGING" "$STAGING/jaclrt_staging.c" -o "$OUT/jaclrt_staging.bc"
# Emit the **binary** object (.temeno, ~200 KB) rather than text (~1.3 MB): the browser re-decodes this
# runtime on every macro body, and decoding the binary is ~4x faster than parsing the text — the
# dominant per-macro cost (tour macro staging ~90ms→~20ms per body).
"$TRANSLATE" "$OUT/jaclrt_staging.bc" -o "$OUT/jaclrt_staging.temeno"
echo "staging runtime:  $OUT/jaclrt_staging.temeno"
