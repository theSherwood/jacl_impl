#!/bin/bash
#
# Build jacl_emit.wasm — the LLVM-free JACL frontend+codegen compiled to wasm via Emscripten,
# exposing `jacl_emit_ir(source) -> SVM IR text` (docs/SVM_BROWSER_PLAN.md, option (b), the live-
# editing half of the SVM playground). The JS host feeds edited source in, gets IR text back, then
# links it vs jaclrt.svm + encodes it in the browser and runs it through the svm-browser cdylib.
#
# Mirrors build_wasm.sh (the old backend's Emscripten build) but compiles the **frontend only**:
# the emit TU (codegen/tests/emit_jacl.c, a unity include of src/jacl.c) + codegen.c + irbuilder.c,
# exporting `_jacl_emit_ir` instead of the whole VM. Output → demo/wasm/jacl_emit.{js,wasm}.
#
# Requires the Emscripten SDK on PATH (`source /path/to/emsdk/emsdk_env.sh`).
set -e

EMCC="${EMCC:-emcc}"
command -v "$EMCC" >/dev/null || { echo "error: emcc not found. Install/activate the Emscripten SDK (emsdk)."; exit 1; }

DIR="$(cd "$(dirname "$0")" && pwd)"   # demo/svm
DEMO="$(cd "$DIR/.." && pwd)"          # demo
ROOT="$(cd "$DEMO/.." && pwd)"         # repo root
OUT="$DEMO/wasm"
mkdir -p "$OUT"

echo "=== Building jacl_emit.wasm (frontend → SVM IR) ==="
$EMCC \
  -std=c99 \
  -O2 \
  -D_DEFAULT_SOURCE \
  -D_POSIX_C_SOURCE=200809L \
  -Wno-implicit-function-declaration \
  -I "$ROOT/codegen" \
  "$ROOT/codegen/tests/emit_jacl.c" \
  "$ROOT/codegen/codegen.c" \
  "$ROOT/codegen/irbuilder.c" \
  -o "$OUT/jacl_emit.js" \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="createJaclEmit" \
  -s EXPORTED_FUNCTIONS="['_jacl_emit_ir','_malloc','_free']" \
  -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']" \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s STACK_SIZE=1048576 \
  -s NO_EXIT_RUNTIME=1 \
  -s WASM_BIGINT=1

echo "=== Done ==="
echo "  $OUT/jacl_emit.js  ($(wc -c < "$OUT/jacl_emit.js") bytes)"
echo "  $OUT/jacl_emit.wasm ($(wc -c < "$OUT/jacl_emit.wasm") bytes)"
