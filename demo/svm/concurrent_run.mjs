// Regression gate for the browser **run** path on a concurrent program (not just compile).
//
// Exercises the exact playground live-edit pipeline on the wasm32 cdylib:
//   1. runSvmb(jacl_compiler.svmb, source) -> IR text   (svm_run_onramp; macro-capable frontend)
//   2. svm_link_run(IR, jaclrt.svm, "__jacl_entry")      -> run the linked program
//
// The program is `# mode: concurrent` and contains `sleep`, which lowers to a timed `memory.wait`
// on the bytecode scheduler. That timed wait used to record a wall-clock deadline via
// `std::time::Instant::now()`, which PANICS on wasm32-unknown-unknown ("SVM run failed:
// unreachable" from `bytecode::drive`). This gate fails if that regression returns — a concurrent
// guest must run to completion on the browser engine, not trap.
//
//   node concurrent_run.mjs <svm_browser.wasm> <jacl_compiler.svmb> <jaclrt.svm>
import { readFileSync } from 'node:fs';

const [wasmPath, compPath, rtPath] = process.argv.slice(2);
if (!wasmPath || !compPath || !rtPath) {
  console.error('usage: node concurrent_run.mjs <svm_browser.wasm> <jacl_compiler.svmb> <jaclrt.svm>');
  process.exit(2);
}

// A concurrent program that sleeps (timed wait) and then prints — the shape that trapped in-browser.
const SRC =
  '# mode: concurrent\n' +
  'proc worker {} {\n  sleep 0.001\n  print "woke"\n}\n' +
  'proc main {} {\n  def f [spawn { worker }]\n  await $f\n}\n' +
  'main\n';

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
const cap = (pf, lf) => {
  const n = Number(lf());
  return n === 0 ? '' : new TextDecoder().decode(new Uint8Array(ex.memory.buffer, Number(pf()), n).slice());
};

const svmb = readFileSync(compPath);
const rt = readFileSync(rtPath);
const src = new TextEncoder().encode(SRC);

// 1. compile the source to IR with the self-hosted compiler-guest.
ex.svm_run_onramp(load(svmb), U(svmb.length), load(src), U(src.length));
let status = ex.svm_status();
const ir = cap(ex.svm_stdout_ptr, ex.svm_stdout_len);
if ((status !== 0 && status !== 5) || ir.startsWith('%%ERROR%%')) {
  console.error(`concurrent_run: COMPILE FAILED status=${status} ${ir.slice(0, 300)}`);
  process.exit(1);
}

// 2. link against the runtime and RUN — this is where a wasm Instant::now() would have trapped.
const prog = new TextEncoder().encode(ir);
const entry = new TextEncoder().encode('__jacl_entry');
ex.svm_link_run(load(prog), U(prog.length), load(rt), U(rt.length), load(entry), U(entry.length), U(0), U(0));
status = ex.svm_status();
const out = cap(ex.svm_stdout_ptr, ex.svm_stdout_len);
const err = cap(ex.svm_stderr_ptr, ex.svm_stderr_len);

console.log(`concurrent_run: status=${status} stdout=${JSON.stringify(out)}${err ? ` stderr=${JSON.stringify(err.slice(0, 300))}` : ''}`);
const ok = (status === 0 || status === 5) && out.includes('woke');
if (!ok) {
  console.error('FAIL: the concurrent sleep program did not run to completion (regression: browser run trap)');
  process.exit(1);
}
console.log('PASS');
