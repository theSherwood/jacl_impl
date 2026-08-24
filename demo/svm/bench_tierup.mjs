// #839 re-measurement (post-#926/#1026, temen): does the JACL compiler-guest now tier up?
// Mirrors temen's browser/bench_tierup_cards.mjs conventions (temen_browser.wasm + engineImports +
// the coop driver's event counters) but for the JACL compiler card: it runs the compiler-guest over
// a JACL source fed on stdin, on (1) the plain bytecode path (temen_run_onramp) and (2) the playground's
// automatic runJitModule path (whole-program emit → falls through to temen_coop_open). Reports timing,
// which path was taken, tier-up/JIT_INVOKE/bounce event counts, stdout parity, and — the #926 question —
// whether temen_coop_open now ADMITS the compiler-guest (returns 0, was -2).
//
//   node demo/svm/bench_tierup.mjs <temen_browser.wasm> <jacl_compiler.svmb> <prog.jacl>...
import { readFileSync } from 'node:fs';
import { runJitModule } from '../../vendor/svm/browser/web/wasmjit-module.js';
import { engineImports } from '../../vendor/svm/browser/engine-imports.mjs';

const [wasmPath, compPath, ...progs] = process.argv.slice(2);
if (!wasmPath || !compPath || progs.length === 0) {
  process.stderr.write('usage: node bench_tierup.mjs <temen_browser.wasm> <jacl_compiler.svmb> <prog.jacl>...\n');
  process.exit(2);
}

const memory = new WebAssembly.Memory({ initial: 2048, maximum: 16384, shared: true });
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), engineImports(memory));
const ex = instance.exports;
const enc = new TextEncoder(), dec = new TextDecoder();
const u8 = () => new Uint8Array(memory.buffer);
const readStdout = () => dec.decode(u8().slice(Number(ex.temen_stdout_ptr()), Number(ex.temen_stdout_ptr()) + Number(ex.temen_stdout_len())));

// Event counters observed from outside (same hooks as browser/bench_tierup_cards.mjs).
const counts = { tierups: 0, invokes: 0, bounces: 0, path: 'interp' };
const reset = () => { counts.tierups = 0; counts.invokes = 0; counts.bounces = 0; counts.path = 'interp'; };
const exCounted = Object.fromEntries(Object.entries(Object.getOwnPropertyDescriptors(ex)).map(([k, d]) => {
  const v = d.value;
  if (typeof v !== 'function') return [k, v];
  if (k === 'temen_coop_func') return [k, (...a) => { counts.tierups++; return v(...a); }];
  if (k === 'temen_coop_jit_wasm_ptr') return [k, (...a) => { counts.invokes++; return v(...a); }];
  if (k === 'temen_coop_call_interp') return [k, (...a) => { counts.bounces++; return v(...a); }];
  if (k === 'temen_onramp_jit_run_open') return [k, (...a) => { const r = v(...a); if (r === 0) counts.path = 'whole-program'; return r; }];
  if (k === 'temen_coop_open') return [k, (...a) => { const r = v(...a); if (r === 0) counts.path = 'coop'; return r; }];
  return [k, v];
}));

const comp = new Uint8Array(readFileSync(compPath));
const load = (b) => { const p = Number(ex.temen_alloc(b.length)); u8().set(b, p); return p; };

function bytecode(srcBytes) {
  const mp = load(comp), sp = load(srcBytes);
  const t0 = performance.now();
  ex.temen_run_onramp(mp, comp.length, sp, srcBytes.length);
  const ms = performance.now() - t0;
  const out = readStdout(), st = ex.temen_status();
  ex.temen_dealloc(mp, comp.length); ex.temen_dealloc(sp, srcBytes.length);
  return { ms, out, st };
}
async function pump(srcBytes, key) {
  reset();
  const t0 = performance.now();
  let threw = null;
  try { await runJitModule(exCounted, memory, comp, srcBytes, key); } catch (e) { threw = e.message; }
  return { ms: performance.now() - t0, out: readStdout(), st: ex.temen_status(), threw, ...counts };
}
// Direct #926 eligibility probe: does temen_coop_open now admit the compiler-guest?
function coopOpenProbe(srcBytes, shared) {
  const mp = load(comp), sp = load(srcBytes);
  const r = ex.temen_coop_open(mp, comp.length, sp, srcBytes.length, shared);
  ex.temen_coop_close?.();
  ex.temen_dealloc(mp, comp.length); ex.temen_dealloc(sp, srcBytes.length);
  return r;
}
const N = 3, best = (xs) => Math.min(...xs);

console.log(`cdylib: ${wasmPath}\n`);
for (const p of progs) {
  const src = enc.encode(readFileSync(p, 'utf8'));
  const b0 = bytecode(src);
  if (b0.st !== 0 && b0.st !== 5) { console.log(`${p}: bytecode status=${b0.st}`); continue; }
  const bMs = best(Array.from({ length: N }, () => bytecode(src).ms));

  const openShared0 = coopOpenProbe(src, 0), openShared1 = coopOpenProbe(src, 1);
  const cold = await pump(src, `jacl#${p}`);
  let warm = cold, warmMs = cold.ms;
  if (!cold.threw) { for (let i = 0; i < N; i++) { const r = await pump(src, `jacl#${p}`); if (r.ms < warmMs) { warmMs = r.ms; warm = r; } } }
  const parity = warm.out === b0.out && warm.st === b0.st ? 'OK' : 'MISMATCH';

  console.log(`${p}  (src ${src.length} B, ir ${b0.out.length} B)`);
  console.log(`  temen_coop_open: shared=0 → ${openShared0}   shared=1 → ${openShared1}   ${openShared0 === 0 ? '(ADMITTED ✓)' : '(refused)'}`);
  if (cold.threw) { console.log(`  pump: THREW ${cold.threw}`); }
  console.log(`  bytecode ${bMs.toFixed(0)}ms | auto path=${warm.path} cold ${cold.ms.toFixed(0)}ms warm ${warmMs.toFixed(0)}ms (${(bMs / warmMs).toFixed(2)}x)`);
  console.log(`  events: tierups=${warm.tierups} jit_invokes=${warm.invokes} bounces=${warm.bounces} | parity=${parity}\n`);
}
