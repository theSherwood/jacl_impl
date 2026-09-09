// Node twin of demo/src/temen-jacl-wasm.ts: run a precompiled JACL `.temen` through the
// temen-browser cdylib's `temen_run_onramp` entry (bytecode engine, wasm32) and print the
// captured stdout. Proves the playground's TEMEN run path headless, so CI can gate it.
//
//   node run_temen.mjs <temen_browser.wasm> <prog.temen> [expected-stdout]
//
// Exit 0 iff temen_status == 0 and (no expected given, or stdout matches).
import { readFileSync } from 'node:fs';
import { engineImports } from '../../vendor/temen/browser/engine-imports.mjs';

const [wasmPath, cardPath, expected] = process.argv.slice(2);
if (!wasmPath || !cardPath) {
  console.error('usage: node run_temen.mjs <temen_browser.wasm> <prog.temen> [expected-stdout]');
  process.exit(2);
}

const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), engineImports());
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

const card = readFileSync(cardPath);
ex.temen_run_onramp(load(card), U(card.length), U(0), U(0));

const status = ex.temen_status();
const stdout = capture(ex.temen_stdout_ptr, ex.temen_stdout_len);
const stderr = capture(ex.temen_stderr_ptr, ex.temen_stderr_len);

console.log(`${cardPath}: status=${status} exit=${ex.temen_exit_code()} stdout=${JSON.stringify(stdout)}${stderr ? ` stderr=${JSON.stringify(stderr)}` : ''}`);

const ok = status === 0 && (expected === undefined || stdout === expected);
if (!ok) { console.error('FAIL'); process.exit(1); }
console.log('PASS');
