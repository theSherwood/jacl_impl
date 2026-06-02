#!/bin/bash
#
# Build the JACL playground demo assets:
#   1. Symlinks the WASM artifacts produced by build_wasm.sh.
#   2. Generates examples.json from test/jacl/*.jacl + the demo tours.
#   3. Bundles the TypeScript editor (src/playground.ts) into
#      dist/playground.js via esbuild, pulling CodeMirror 6 + the vim
#      plugin in from node_modules. Runs `npm ci` first if
#      node_modules is missing, so a fresh clone can build with
#      `bash build_demo.sh` alone.
#
# Usage: cd demo && bash build_demo.sh
#

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"

# --- Symlink WASM artifacts ---
mkdir -p "$DIR/wasm"
ln -sfn "../../build_wasm/jacl.js"   "$DIR/wasm/jacl.js"
ln -sfn "../../build_wasm/jacl.wasm" "$DIR/wasm/jacl.wasm"
echo "Linked wasm/ -> build_wasm/"

# --- Editor bundle (CodeMirror 6 + vim + JACL mode) ---
cd "$DIR"
if [ ! -d node_modules ]; then
  echo "Installing editor deps (one-time)…"
  npm ci --no-audit --no-fund >/dev/null 2>&1 || npm install --no-audit --no-fund
fi
echo "Bundling editor (esbuild)…"
npm run build --silent
echo "Bundled -> dist/playground.js ($(wc -c < dist/playground.js) bytes)"
cd "$ROOT"

# --- Generate examples.json ---
node -e '
const fs = require("fs");
const path = require("path");

const testDir = path.join(process.argv[1], "test", "jacl");
const outFile = path.join(process.argv[2], "examples.json");

const files = fs.readdirSync(testDir)
  .filter(f => f.endsWith(".jacl") && !fs.statSync(path.join(testDir, f)).isDirectory())
  .sort();

const examples = files.map(f => {
  const code = fs.readFileSync(path.join(testDir, f), "utf8");
  const name = f.replace(".jacl", "");

  // Category: prefix before first underscore, or full name if no underscore
  const underIdx = name.indexOf("_");
  const category = underIdx > 0 ? name.substring(0, underIdx) : name;

  // Detect concurrent features
  const concurrent = /\b(spawn|parallel|race|await)\b/.test(code) ||
                     code.includes("# mode: concurrent");

  // Extract first descriptive comment (skip expect/mode lines)
  let description = "";
  for (const line of code.split("\n")) {
    if (/^#\s*(expect|mode):/.test(line)) continue;
    if (line.startsWith("# ")) {
      description = line.slice(2).trim();
      break;
    }
    if (line.trim() && !line.startsWith("#")) break;
  }

  return { name, category, code, concurrent, description };
});

fs.writeFileSync(outFile, JSON.stringify(examples, null, 2));
console.log("Generated examples.json with " + examples.length + " examples");
' "$ROOT" "$DIR"

echo ""
echo "Done! To serve the demo:"
echo "  cd demo && python3 -m http.server 8080"
echo "  Open http://localhost:8080"
