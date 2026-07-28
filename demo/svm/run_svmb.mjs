// Node twin of demo/src/svm-jacl-wasm.ts: run a precompiled JACL `.svmb` through the
// svm-browser cdylib's `svm_run_onramp` entry (bytecode engine, wasm32) and print the
// captured stdout. Proves the playground's SVM run path headless, so CI can gate it.
//
//   node run_svmb.mjs <svm_browser.wasm> <prog.svmb> [expected-stdout]
//
// Exit 0 iff svm_status == 0 and (no expected given, or stdout matches).
import { readFileSync } from 'node:fs';

const [wasmPath, svmbPath, expected] = process.argv.slice(2);
if (!wasmPath || !svmbPath) {
  console.error('usage: node run_svmb.mjs <svm_browser.wasm> <prog.svmb> [expected-stdout]');
  process.exit(2);
}

const imports = { svm_host: { webgpu_op: () => -1n } };
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), imports);
const ex = instance.exports;

const is64 = ex.svm_abi_is64() === 1;
const U = (x) => (is64 ? BigInt(x) : x);

const load = (bytes) => {
  const ptr = ex.svm_alloc(U(bytes.length));
  new Uint8Array(ex.memory.buffer).set(bytes, Number(ptr)); // re-fetch (alloc may grow memory)
  return Number(ptr);
};
const capture = (ptrFn, lenFn) => {
  const n = Number(lenFn());
  return n === 0 ? '' : new TextDecoder().decode(new Uint8Array(ex.memory.buffer, Number(ptrFn()), n).slice());
};

const svmb = readFileSync(svmbPath);
ex.svm_run_onramp(load(svmb), U(svmb.length), U(0), U(0));

const status = ex.svm_status();
const stdout = capture(ex.svm_stdout_ptr, ex.svm_stdout_len);
const stderr = capture(ex.svm_stderr_ptr, ex.svm_stderr_len);

console.log(`${svmbPath}: status=${status} exit=${ex.svm_exit_code()} stdout=${JSON.stringify(stdout)}${stderr ? ` stderr=${JSON.stringify(stderr)}` : ''}`);

const ok = status === 0 && (expected === undefined || stdout === expected);
if (!ok) { console.error('FAIL'); process.exit(1); }
console.log('PASS');
