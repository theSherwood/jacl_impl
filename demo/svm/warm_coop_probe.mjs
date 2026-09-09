// Measure the JACL compiler-guest on the WARM-COOP tier: open the warm-snapshot card once
// (temen_warm_open runs warmup + snapshots), then per program compare
//   1. warm-interp : temen_warm_eval                    (restore image + eval on the bytecode interp)
//   2. warm-coop   : runWarmCoop -> driveCoopTierupRun   (restore image + tier up eval_run)
// Reports best-of-N warm times, tier-up/bounce counts, stdout parity, and interp/coop speedup —
// so we can see whether warm-snapshot (no startup floor) COMPOSES with tier-up (fast compile).
//
//   node demo/svm/warm_coop_probe.mjs <cdylib.wasm> <snapshot.temen> <prog.jacl>...
import { readFileSync } from 'node:fs';
import { performance } from 'node:perf_hooks';
import { runWarmCoop } from '../../vendor/svm/browser/web/wasmjit-module.js';
import { engineImports } from '../../vendor/svm/browser/engine-imports.mjs';

const [wasmPath, snapCard, ...progs] = process.argv.slice(2);
const memory = new WebAssembly.Memory({ initial: 2048, maximum: 16384, shared: true });
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), engineImports(memory));
const ex = instance.exports;
const enc = new TextEncoder(), dec = new TextDecoder();
const u8 = () => new Uint8Array(memory.buffer);
const readOut = () => dec.decode(u8().slice(Number(ex.temen_stdout_ptr()), Number(ex.temen_stdout_ptr()) + Number(ex.temen_stdout_len())));

// Cross-tier event counters (same Proxy trick as bench_tierup / bench_warm_coop).
const counts = { tierups: 0, bounces: 0 };
const reset = () => { counts.tierups = 0; counts.bounces = 0; };
const exCounted = Object.fromEntries(Object.entries(Object.getOwnPropertyDescriptors(ex)).map(([k, d]) => {
  const v = d.value;
  if (typeof v !== 'function') return [k, v];
  if (k === 'temen_coop_func') return [k, (...a) => { counts.tierups++; return v(...a); }];
  if (k === 'temen_coop_call_interp') return [k, (...a) => { counts.bounces++; return v(...a); }];
  return [k, v];
}));

const bestSync = (f, n) => { let b = 1e9, o; for (let i = 0; i < n; i++) { const t = performance.now(); o = f(); const d = performance.now() - t; if (d < b) b = d; } return { ms: b, out: o }; };
const bestAsync = async (f, n) => { let b = 1e9, r; for (let i = 0; i < n; i++) { const t = performance.now(); const out = await f(); const d = performance.now() - t; if (d < b) { b = d; r = { ms: d, out, ...counts }; } } return r; };

const N = 4;
const snap = new Uint8Array(readFileSync(snapCard));

console.log(`cdylib: ${wasmPath}`);
console.log(`snapshot card: ${snapCard}  (warm session opened once per program; best of ${N})\n`);

for (const prog of progs) {
  const src = enc.encode(readFileSync(prog, 'utf8'));
  // Fresh warm session per program (the image is program-independent, but re-open keeps each row clean).
  const mp = Number(ex.temen_alloc(snap.length)); u8().set(snap, mp);
  const opened = ex.temen_warm_open(mp, snap.length);
  ex.temen_dealloc(mp, snap.length);
  if (opened === -1n || ex.temen_status() !== 0) { console.log(`${prog}: temen_warm_open FAILED (status ${ex.temen_status()})`); continue; }

  // warm-interp
  const wi = bestSync(() => { const sp = Number(ex.temen_alloc(src.length)); u8().set(src, sp); ex.temen_warm_eval(sp, src.length); const o = readOut(); ex.temen_dealloc(sp, src.length); return o; }, N);

  // warm-coop
  let wc = null, err = null;
  try {
    // cold run first (emits leaves + WebAssembly.compile), then best-of-N warm
    reset(); await runWarmCoop(exCounted, memory, src, `wc#${prog}`);
    wc = await bestAsync(async () => { reset(); await runWarmCoop(exCounted, memory, src, `wc#${prog}`); return readOut(); }, N);
  } catch (e) { err = e.message; }

  const sz = readFileSync(prog).length;
  console.log(`${prog}  (${sz} B)`);
  console.log(`  warm-interp : ${wi.ms.toFixed(0)}ms  (ir ${wi.out.length} B)`);
  if (wc) {
    const parity = wc.out === wi.out ? 'OK' : 'MISMATCH';
    console.log(`  warm-coop   : ${wc.ms.toFixed(0)}ms  (${(wi.ms / wc.ms).toFixed(2)}x vs warm-interp)  tierups=${wc.tierups} bounces=${wc.bounces}  parity=${parity}`);
  } else {
    console.log(`  warm-coop   : DECLINED/failed (${err})`);
  }
  console.log('');
  ex.temen_warm_close();
}
