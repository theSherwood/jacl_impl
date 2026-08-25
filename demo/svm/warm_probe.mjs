import { readFileSync } from 'node:fs';
import { engineImports } from '../../vendor/svm/browser/engine-imports.mjs';
const [wasmPath, coldCard, warmCard, prog] = process.argv.slice(2);
const memory = new WebAssembly.Memory({ initial: 2048, maximum: 16384, shared: true });
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), engineImports(memory));
const ex = instance.exports;
const dec = new TextDecoder(), enc = new TextEncoder();
const u8 = () => new Uint8Array(memory.buffer);
const load = (b) => { const p = Number(ex.temen_alloc(b.length)); u8().set(b, p); return p; };
const readOut = () => dec.decode(u8().slice(Number(ex.temen_stdout_ptr()), Number(ex.temen_stdout_ptr()) + Number(ex.temen_stdout_len())));
const src = enc.encode(readFileSync(prog, 'utf8'));
const cold = new Uint8Array(readFileSync(coldCard)), warm = new Uint8Array(readFileSync(warmCard));
const best = (f, n) => { let b = 1e9, out; for (let i=0;i<n;i++){ const t=performance.now(); out=f(); const d=performance.now()-t; if(d<b)b=d; } return {ms:b,out}; };

// COLD: temen_run_onramp with the _start card, full run each time (prelude init paid per compile).
function coldRun() { const mp=load(cold), sp=load(src); ex.temen_run_onramp(mp,cold.length,sp,src.length); const o=readOut(); ex.temen_dealloc(mp,cold.length); ex.temen_dealloc(sp,src.length); return o; }
const c = best(coldRun, 3);
console.log(`cold (temen_run_onramp): ${c.ms.toFixed(0)}ms  (ir ${c.out.length} B)`);

// WARM: open the snapshot card once (runs warmup + snapshots), then eval per compile.
const mp = load(warm);
const opened = ex.temen_warm_open(mp, warm.length);
console.log(`temen_warm_open -> ${opened} (status ${ex.temen_status()})`);
if (opened !== -1n) {
  function warmEval() { const sp=load(src); ex.temen_warm_eval(sp,src.length); const o=readOut(); ex.temen_dealloc(sp,src.length); return o; }
  const w = best(warmEval, 4);
  console.log(`warm (temen_warm_eval): ${w.ms.toFixed(0)}ms  (ir ${w.out.length} B)`);
  console.log(`parity: ${w.out === c.out ? 'OK' : 'MISMATCH'}   speedup: ${(c.ms/w.ms).toFixed(2)}x   saved: ${(c.ms-w.ms).toFixed(0)}ms/compile`);
  ex.temen_warm_close();
}
