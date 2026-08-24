// Headless gate for the **self-hosted compiler path**: run `jacl_compiler.svmb` through the
// svm-browser cdylib's `temen_run_onramp` (wasm32) with a macro-bearing JACL program on stdin, and
// assert the emitted SVM-IR shows the macro expanded. This exercises the real deployed path the
// playground's live-edit uses (`SvmJaclRunner.emitIrViaCompiler`) and gates two things the wasm32
// cdylib must get right for a large C guest that grows its heap:
//   * the interpreter's 4 KiB software page size (a coarse page faults the guest mid-heap-grow), and
//   * import-bound execution of the whole compile on the bytecode engine.
//
//   node compile_guest.mjs <temen_browser.wasm> <jacl_compiler.svmb>
//
// Exit 0 iff the compile succeeds (temen_status == 0/exit) and the emitted IR contains a `br_if`
// (the tour's `unless` macro lowers `if` → `br_if`) with no `%%ERROR%%` diagnostic.
import { readFileSync } from 'node:fs';

const [wasmPath, svmbPath] = process.argv.slice(2);
if (!wasmPath || !svmbPath) {
  console.error('usage: node compile_guest.mjs <temen_browser.wasm> <jacl_compiler.svmb>');
  process.exit(2);
}

// The tour's macro program: `unless C {B}` expands to `[if C {} {B}]`, which lowers to a `br_if`.
const SRC =
  'defmacro unless {cond body} { syntax-quote [if ~cond {} ~body] }\n' +
  'mut hit 0\nunless [== 1 2] { set hit 5 }\nhit\n';

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
const capture = (ptrFn, lenFn) => {
  const n = Number(lenFn());
  return n === 0 ? '' : new TextDecoder().decode(new Uint8Array(ex.memory.buffer, Number(ptrFn()), n).slice());
};

const svmb = readFileSync(svmbPath);
const src = new TextEncoder().encode(SRC);
ex.temen_run_onramp(load(svmb), U(svmb.length), load(src), U(src.length));

const status = ex.temen_status();
const ir = capture(ex.temen_stdout_ptr, ex.temen_stdout_len);
const stderr = capture(ex.temen_stderr_ptr, ex.temen_stderr_len);

console.log(`compile_guest: status=${status} ir=${ir.length}B br_if=${ir.includes('br_if')}${stderr ? ` stderr=${JSON.stringify(stderr)}` : ''}`);

const ok = (status === 0 || status === 5) && ir.includes('br_if') && !ir.includes('%%ERROR%%');
if (!ok) {
  console.error('FAIL: the compiler-guest did not stage the macro (see status/IR above)');
  process.exit(1);
}
console.log('PASS');
