#!/bin/sh
# Produce the JACL runtime artifacts for the SVM backend:
#
#   1. jaclrt.bc   — the clang stage (runtime C -> clang -O2 -emit-llvm).
#   2. jaclrt.svm  — the SVM-IR module (temen-llvm-translate of the bitcode), the
#                    reusable, separately-compiled runtime that programs link against.
#                    Exports ride in-band in the module now — temen-llvm retired the
#                    `.syms` export sidecar — so a program module resolves its
#                    `call.import "jacl_*"` through svm_ir::link over the module's own
#                    export table (svm_link_run reads `module.exports` directly).
#
# This is the separate-artifact path (SVM_BACKEND_PHASE2.md P2.0): compile the
# runtime once here, link many JACL-emitted program modules against it. Requires
# clang (LLVM 18, to match the svm submodule) and libLLVM-18 dev (for temen-llvm).
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${DIR}/build"
SVM_LLVM="${DIR}/../vendor/svm/crates/temen-llvm"
mkdir -p "$OUT"

# 1. runtime C -> LLVM bitcode. -fno-*vectorize keeps the IR in temen-llvm's scalar
#    subset (the backend ingests auto-vec selectively; the runtime stays scalar).
clang -O2 -emit-llvm -c -DNDEBUG -fno-vectorize -fno-slp-vectorize \
  -I "$DIR" "$DIR/jaclrt.c" -o "$OUT/jaclrt.bc"
echo "runtime bitcode:  $OUT/jaclrt.bc"

# 2. bitcode -> SVM-IR module (exports in-band), via the standalone CLI.
cargo run --quiet --manifest-path "$SVM_LLVM/Cargo.toml" --bin temen-llvm-translate -- \
  "$OUT/jaclrt.bc" -o "$OUT/jaclrt.svm"
echo "runtime module:   $OUT/jaclrt.svm"

# 3. staging runtime (jaclrt + the syn_rt macro-I/O glue) as one module: the library jacl_emit.wasm's
#    macro staging links each codegen'd macro body against, so `synrt_read_arg`/`synrt_write_result`
#    (and the jacl_* runtime) resolve by name. Same scalar subset as jaclrt.svm.
STAGING="${DIR}/../codegen/selfhost/macro_staging"
clang -O2 -emit-llvm -c -DNDEBUG -fno-vectorize -fno-slp-vectorize \
  -I "$DIR" -I "$STAGING" "$STAGING/jaclrt_staging.c" -o "$OUT/jaclrt_staging.bc"
# Emit the **binary** object (.svmo, ~200 KB) rather than text (~1.3 MB): the browser re-decodes this
# runtime on every macro body, and decoding the binary is ~4x faster than parsing the text — the
# dominant per-macro cost (tour macro staging ~90ms→~20ms per body).
cargo run --quiet --manifest-path "$SVM_LLVM/Cargo.toml" --bin temen-llvm-translate -- \
  "$OUT/jaclrt_staging.bc" -o "$OUT/jaclrt_staging.svmo"
echo "staging runtime:  $OUT/jaclrt_staging.svmo"
