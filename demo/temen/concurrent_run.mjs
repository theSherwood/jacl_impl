// Regression gate for the browser **run** path on a concurrent program (not just compile).
//
// Exercises the exact playground live-edit pipeline on the wasm32 cdylib:
//   1. runTemen(jacl_compiler.temen, source) -> IR text   (temen_run_onramp; macro-capable frontend)
//   2. temen_link_run(IR, jaclrt.temen, "__jacl_entry")      -> run the linked program
//
// The program is `# mode: concurrent` and contains `sleep`, which lowers to a timed `memory.wait`
// on the bytecode scheduler. That timed wait used to record a wall-clock deadline via
// `std::time::Instant::now()`, which PANICS on wasm32-unknown-unknown ("TEMEN run failed:
// unreachable" from `bytecode::drive`). This gate fails if that regression returns — a concurrent
// guest must run to completion on the browser engine, not trap.
//
//   node concurrent_run.mjs <temen_browser.wasm> <jacl_compiler.temen> <jaclrt.temen>
import { readFileSync } from 'node:fs';
import { engineImports } from '../../vendor/temen/browser/engine-imports.mjs';

const [wasmPath, compPath, rtPath] = process.argv.slice(2);
if (!wasmPath || !compPath || !rtPath) {
  console.error('usage: node concurrent_run.mjs <temen_browser.wasm> <jacl_compiler.temen> <jaclrt.temen>');
  process.exit(2);
}

// A concurrent program that sleeps (timed wait) and then prints — the shape that trapped in-browser.
const SRC =
  '# mode: concurrent\n' +
  'proc worker {} {\n  sleep 0.001\n  print "woke"\n}\n' +
  'proc main {} {\n  def f [spawn { worker }]\n  await $f\n}\n' +
  'main\n';

const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), engineImports());
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
  return n === 0 ? '' : new TextDecoder().decode(new Uint8Array(ex.memory.buffer, Number(pf()), n).slice());
};
// The compiler card emits a BINARY TEMEN object, so its bytes must reach temen_link_run
// untouched. Decoding them as UTF-8 and re-encoding does not round-trip: every byte that is
// not valid UTF-8 becomes U+FFFD (three bytes), which both corrupts and lengthens the object.
// Measured on a real one: 178,896 bytes in, 228,017 out, first divergence at offset 20.
const capRaw = (pf, lf) => {
  const n = Number(lf());
  return n === 0 ? new Uint8Array(0) : new Uint8Array(ex.memory.buffer, Number(pf()), n).slice();
};

const card = readFileSync(compPath);
const rt = readFileSync(rtPath);
const src = new TextEncoder().encode(SRC);

// 1. compile the source with the self-hosted compiler-guest -> a binary TEMEN object.
ex.temen_run_onramp(load(card), U(card.length), load(src), U(src.length));
let status = ex.temen_status();
const prog = capRaw(ex.temen_stdout_ptr, ex.temen_stdout_len);
const head = new TextDecoder().decode(prog.slice(0, 9));   // enough for the %%ERROR%% marker
if ((status !== 0 && status !== 5) || head.startsWith('%%ERROR%%') || prog.length === 0) {
  const text = new TextDecoder().decode(prog).slice(0, 300);
  console.error(`concurrent_run: COMPILE FAILED status=${status} ${text}`);
  process.exit(1);
}

// 2. link against the runtime and RUN — this is where a wasm Instant::now() would have trapped.
const entry = new TextEncoder().encode('__jacl_entry');
ex.temen_link_run(load(prog), U(prog.length), load(rt), U(rt.length), load(entry), U(entry.length), U(0), U(0));
status = ex.temen_status();
const out = cap(ex.temen_stdout_ptr, ex.temen_stdout_len);
const err = cap(ex.temen_stderr_ptr, ex.temen_stderr_len);

console.log(`concurrent_run: status=${status} stdout=${JSON.stringify(out.slice(-120))}${err ? ` stderr=${JSON.stringify(err.slice(0, 300))}` : ''}`);
// `endsWith`, not `includes`: the compiled object has the string literal "woke" in its data
// section, so an `includes` check passes on a stdout buffer that is really just the leftover
// object from step 1 — which is exactly what it was doing while this gate was broken.
const ok = (status === 0 || status === 5) && out.trimEnd().endsWith('woke');
if (!ok) {
  console.error('FAIL: the concurrent sleep program did not run to completion (regression: browser run trap)');
  process.exit(1);
}
console.log('PASS');
