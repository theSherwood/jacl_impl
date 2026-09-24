#!/bin/sh
# Produce the JACL runtime artifacts for the TEMEN backend:
#
#   1. jaclrt.ll   — the clang stage (runtime C -> clang -O2 -emit-llvm), built with JACL_UNIR so
#                    channels run on Unir edges (docs/UNIR_CHANNELS.md), then llvm-linked with the
#                    vendored unir unit (unir/unir_cabi.ll).
#   2. jaclrt.temen  — the TEMEN-IR module (temen-llvm-translate of the linked IR), the
#                    reusable, separately-compiled runtime that programs link against.
#                    Exports ride in-band in the module now — temen-llvm retired the
#                    `.syms` export sidecar — so a program module resolves its
#                    `call.import "jacl_*"` through temen_ir::link over the module's own
#                    export table (temen_link_run reads `module.exports` directly).
#
# This is the separate-artifact path (TEMEN_BACKEND_PHASE2.md P2.0): compile the
# runtime once here, link many JACL-emitted program modules against it. Requires
# clang (LLVM 18, to match the temen submodule) and libLLVM-18 dev (for temen-llvm), plus
# llvm-link from LLVM 21 or later (LLVM_LINK): the unir unit is rustc's LLVM 21 IR, which
# LLVM 18 cannot read.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${DIR}/build"
TEMEN_LLVM="${DIR}/../vendor/temen/crates/temen-llvm"
mkdir -p "$OUT"

# The LLVM -> TEMEN on-ramp, built **release**. This used to be `cargo run` with no profile,
# i.e. debug, which cost twice over: the translator itself runs ~6x slower (measured on the
# self-hosted compiler card, 6131ms -> 1030ms, byte-identical output), and it meant CI compiled
# the LLVM-bindings crate in *two* profiles, because codegen/selfhost/build_compiler_temen.sh
# has always used release. One profile, one compile, same bytes.
TRANSLATE="${TEMEN_LLVM_TRANSLATE:-$TEMEN_LLVM/target/release/temen-llvm-translate}"
if [ ! -x "$TRANSLATE" ]; then
  echo "=== building temen-llvm-translate (release) from vendor/temen ==="
  ( cd "$TEMEN_LLVM" && cargo build --release --bin temen-llvm-translate )
fi

# 1. runtime C -> LLVM IR, linked with the unir unit. -fno-*vectorize keeps the IR in temen-llvm's
#    scalar subset (the backend ingests auto-vec selectively; the runtime stays scalar).
LLVM_LINK="${LLVM_LINK:-llvm-link-21}"
clang -O2 -S -emit-llvm -DNDEBUG -DJACL_UNIR -fno-vectorize -fno-slp-vectorize \
  --target=x86_64-unknown-linux-gnu -I "$DIR" "$DIR/jaclrt.c" -o "$OUT/jaclrt_c.ll"
"$LLVM_LINK" -S "$OUT/jaclrt_c.ll" "$DIR/unir/unir_cabi.ll" -o "$OUT/jaclrt.ll"
echo "runtime IR:       $OUT/jaclrt.ll (with unir $(cat "$DIR/unir/UNIR_REV"))"

# 2. linked IR -> TEMEN-IR module (exports in-band), via the standalone CLI.
"$TRANSLATE" "$OUT/jaclrt.ll" -o "$OUT/jaclrt.temen" --powerbox-layout
echo "runtime module:   $OUT/jaclrt.temen"

# 3. staging runtime (jaclrt + the syn_rt macro-I/O glue) as one module: the library jacl_emit.wasm's
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
