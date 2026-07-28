#!/bin/bash
#
# Build the SVM-backend playground assets (docs/SVM_BROWSER_PLAN.md, MVP option (a)):
#
#   1. svm_browser.wasm  — the svm-browser cdylib compiled to wasm32 (the run engine:
#                          decode a .svmb + run function 0 on the bytecode interpreter,
#                          capture stdout). Copied to demo/wasm/svm_browser.wasm.
#   2. <example>.svmb    — each example program linked against the translated JACL runtime
#                          and encoded at build time (the "link natively for fixed examples"
#                          MVP; the in-browser frontend for live editing is a later slice).
#                          Written to demo/svm/svmb/.
#
# The runtime IR (jaclrt.svm) is baked by runtime/build.sh (clang-18 + svm-llvm) — libLLVM
# stays out of the browser; only its .svmb output ships. Requires the wasm32 target
# (`rustup target add wasm32-unknown-unknown`) and clang-18.
#
# Usage: cd demo && bash svm/build_assets.sh [prog1.jacl prog2.jacl ...]
#        (default set: a couple of smoke programs)
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"          # demo/svm
DEMO="$(cd "$DIR/.." && pwd)"                 # demo
ROOT="$(cd "$DEMO/.." && pwd)"                # repo root
SVM="$ROOT/vendor/svm"

# --- 1. cdylib -> wasm32 --------------------------------------------------------------------------
echo "Building svm-browser cdylib for wasm32…"
( cd "$SVM/browser" && cargo build --release --lib --target wasm32-unknown-unknown )
mkdir -p "$DEMO/wasm"
cp "$SVM/browser/target/wasm32-unknown-unknown/release/svm_browser.wasm" "$DEMO/wasm/svm_browser.wasm"
echo "  -> demo/wasm/svm_browser.wasm ($(wc -c < "$DEMO/wasm/svm_browser.wasm") bytes)"

# --- 1b. runtime IR (jaclrt.svm): baked once, shipped for in-browser live linking -----------------
if [ ! -f "$ROOT/runtime/build/jaclrt.svm" ]; then
  echo "Baking runtime IR (runtime/build.sh)…"
  bash "$ROOT/runtime/build.sh"
fi
cp "$ROOT/runtime/build/jaclrt.svm" "$DEMO/svm/jaclrt.svm"
echo "  -> demo/svm/jaclrt.svm ($(wc -c < "$DEMO/svm/jaclrt.svm") bytes; the live-editing link target)"

# --- 1c. frontend (jacl_emit.wasm): the LLVM-free lexer+parser+codegen, for live editing ----------
# Requires the Emscripten SDK on PATH. Skipped (fail-soft) if emcc is absent — the playground then
# runs precompiled examples only, and the SVM live path falls back to the Classic VM.
if command -v emcc >/dev/null 2>&1; then
  bash "$DIR/build_emit_wasm.sh"
else
  echo "  (emcc not found — skipping jacl_emit.wasm; live editing on SVM needs it. Precompiled examples still run.)"
fi

OUT="$DIR/svmb"
mkdir -p "$OUT"

# Default smoke set if none given: write two tiny programs.
if [ "$#" -eq 0 ]; then
  printf 'print "hi"\n' > "$OUT/hi.jacl"
  printf 'proc g {} [Stream i64] {\n  yield 1\n  yield 2\n}\nproc main {} {\n  for [g] n { print $n }\n}\nmain\n' > "$OUT/gen.jacl"
  set -- "$OUT/hi.jacl" "$OUT/gen.jacl"
fi

names=()
for prog in "$@"; do
  name="$(basename "$prog" .jacl)"
  echo "Linking + encoding $name → $name.svmb…"
  ( cd "$ROOT/runtime/harness" && cargo run --quiet --bin emit_svmb -- "$prog" "$OUT/$name.svmb" )
  echo "  -> demo/svm/svmb/$name.svmb ($(wc -c < "$OUT/$name.svmb") bytes)"
  names+=("$name")
done

# Manifest: example name → .svmb file, so the playground can list + fetch precompiled programs.
printf '%s\n' "${names[@]}" | node -e '
  const fs = require("fs");
  const names = require("fs").readFileSync(0, "utf8").split("\n").filter(Boolean);
  const manifest = names.map((n) => ({ name: n, svmb: `svmb/${n}.svmb` }));
  fs.writeFileSync(process.argv[1], JSON.stringify(manifest, null, 2) + "\n");
' "$OUT/manifest.json"
echo "  -> demo/svm/svmb/manifest.json ($(wc -c < "$OUT/manifest.json") bytes)"

echo ""
echo "Smoke-test the run path headless:"
echo "  node demo/svm/run_svmb.mjs demo/wasm/svm_browser.wasm demo/svm/svmb/hi.svmb \$'hi\\n'"
