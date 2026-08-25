// Regression gate for the browser **macro-staging** path — the pipeline that makes the fast AOT
// frontend (jacl_emit.wasm) macro-capable without falling back to the slow self-hosted guest.
//
// The playground compiles edited source with jacl_emit.wasm (native wasm speed). It is an emit-only
// build with no SVM runtime, so on its own it *cannot* expand a `defmacro` — it returns an
// "emit-only"/staging error, and playground.ts then silently falls back to the ~20x-slower
// compiler-guest (compileWith, demo/src/playground.ts). The staging hook is what avoids that: the
// frontend codegens each macro body to an SVM object and bounces it to the cdylib
// (jacl_svm_stage → Module.svmStageRun → temen_link_run vs jaclrt_staging.svmo), so only tiny macro
// bodies touch SVM and the tour compiles in ~80ms instead of ~1.8s.
//
// This gate wires that exact pipeline headlessly and asserts a user `defmacro` compiles **through
// staging** — i.e. jacl_emit.wasm produces IR (no staging error) AND the stage hook actually ran.
// If the seam regresses (hook not installed, jaclrt_staging.svmo missing/stale, wire format drift,
// cdylib entry rename), emitIr errors here and the gate fails — instead of the browser quietly
// degrading to the slow guest with output still correct (invisible to correctness tests).
//
//   node stage_gate.mjs <jacl_emit.js> <temen_browser.wasm> <jaclrt_staging.svmo> <jaclrt.svm>
import { readFileSync, writeFileSync, rmSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import path from 'node:path';

const [emitJsPath, wasmPath, stagingRtPath, jaclrtPath] = process.argv.slice(2);
if (!emitJsPath || !wasmPath || !stagingRtPath || !jaclrtPath) {
  console.error('usage: node stage_gate.mjs <jacl_emit.js> <temen_browser.wasm> <jaclrt_staging.svmo> <jaclrt.svm>');
  process.exit(2);
}

// A program with a user `defmacro` (the tour's `unless`, verbatim). jacl_emit.wasm can only compile
// this by staging the macro body on SVM; without the hook it errors "emit-only build".
const SRC =
  'defmacro unless {cond body} {\n' +
  '  syntax-quote [if ~cond {} ~body]\n' +
  '}\n' +
  'proc main {} {\n' +
  '  mut hit 0\n' +
  '  unless [== 1 2] { set hit 5 }\n' +
  '  print $hit\n' +
  '}\n' +
  'main\n';

// --- svm-browser cdylib: runs the staged macro-body module, and the final linked program. ---------
const imports = { temen_host: { webgpu_op: () => -1n } };
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), imports);
const ex = instance.exports;
const is64 = ex.temen_abi_is64() === 1;
const U = (x) => (is64 ? BigInt(x) : x);
const load = (bytes) => {
  const ptr = ex.temen_alloc(U(bytes.length));
  new Uint8Array(ex.memory.buffer).set(bytes, Number(ptr)); // re-fetch (alloc may grow memory)
  return Number(ptr);
};
const cap = (pf, lf) => {
  const n = Number(lf());
  return n === 0 ? new Uint8Array(0) : new Uint8Array(ex.memory.buffer, Number(pf()), n).slice();
};

const stagingRt = readFileSync(stagingRtPath);
const jaclrt = readFileSync(jaclrtPath);
const ENTRY = new TextEncoder().encode('__jacl_entry');

// linkRunRaw twin (demo/src/svm-jacl-wasm.ts): link `moduleBytes` at __jacl_entry against `runtime`,
// feed `stdin`, return raw stdout bytes (null on a non-OK run). Byte-in/byte-out — no marshalling.
function linkRunRaw(moduleBytes, runtime, stdin) {
  const progPtr = load(moduleBytes);
  const rtPtr = load(runtime);
  const entryPtr = load(ENTRY);
  let inPtr = U(0), inLen = U(0);
  if (stdin.length) { inPtr = load(stdin); inLen = U(stdin.length); }
  ex.temen_link_run(progPtr, U(moduleBytes.length), rtPtr, U(runtime.length), entryPtr, U(ENTRY.length), inPtr, inLen);
  if (ex.temen_status() !== 0) return null;
  return cap(ex.temen_stdout_ptr, ex.temen_stdout_len);
}

// --- jacl_emit.wasm frontend, wired with the staging hook (mirrors JaclFrontend.create). ----------
// Emscripten's MODULARIZE output is CommonJS; demo/package.json is "type":"module", so require of the
// .js is refused. Copy it to a .cjs and require that; locateFile points the loader back at the .wasm.
const cjsPath = path.join(tmpdir(), `jacl_emit_gate_${process.pid}.cjs`);
writeFileSync(cjsPath, readFileSync(emitJsPath));
const createJaclEmit = createRequire(import.meta.url)(cjsPath);
const wasmDir = path.dirname(emitJsPath);

let stageCalls = 0;
const mod = await createJaclEmit({ locateFile: (p) => path.join(wasmDir, p) });
rmSync(cjsPath, { force: true });
mod.svmStageRun = (moduleBytes, argWire) => { stageCalls++; return linkRunRaw(moduleBytes, stagingRt, argWire); };
if (typeof mod._jacl_install_svm_stage_hook !== 'function') {
  console.error('stage_gate: FAIL — jacl_emit.wasm has no jacl_install_svm_stage_hook export; the staging seam was not built in.');
  process.exit(1);
}
mod.ccall('jacl_install_svm_stage_hook', null, [], []); // arm the hook

// --- Compile the macro program through the staged frontend. ---------------------------------------
const irPtr = mod.cwrap('jacl_emit_ir', 'number', ['string'])(SRC);
const emitted = mod.UTF8ToString(irPtr);
mod._free(irPtr);

const MARK = '%%ERROR%%\n';
if (emitted.startsWith(MARK)) {
  console.error(`stage_gate: emit FAILED (macro did not stage — browser would silently fall back to the slow guest):`);
  console.error(`  ${emitted.slice(MARK.length).trim().slice(0, 400)}`);
  process.exit(1);
}
if (stageCalls === 0) {
  console.error('stage_gate: FAIL — jacl_emit produced IR but the SVM stage hook never ran; the `unless` macro was not staged.');
  process.exit(1);
}

// --- Run the emitted IR to confirm the staged expansion is correct end-to-end. --------------------
const run = linkRunRaw(new TextEncoder().encode(emitted), jaclrt, new Uint8Array(0));
// linkRunRaw here reuses the same helper; the emitted IR is text, which temen_link_run sniffs by magic.
const out = run ? new TextDecoder().decode(run) : null;
const stderr = new TextDecoder().decode(cap(ex.temen_stderr_ptr, ex.temen_stderr_len));

console.log(`stage_gate: stageCalls=${stageCalls} ir=${emitted.length}B stdout=${JSON.stringify(out)}${stderr ? ` stderr=${JSON.stringify(stderr.slice(0, 200))}` : ''}`);

if (out === null || !out.includes('5')) {
  console.error('stage_gate: FAIL — the staged `unless` expansion did not run to the expected result (5).');
  process.exit(1);
}
console.log('PASS');
