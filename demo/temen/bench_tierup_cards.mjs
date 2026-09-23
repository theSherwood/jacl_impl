// JACL **program** cards on the cooperative tier-up driver (temen #1627).
//
// `bench_tierup.mjs` runs the compiler-guest; this runs what the compiler's output becomes — a JACL
// program linked against jaclrt, which carries the collector (`jacl_gc_collect_stw` holds
// `gc.roots`). Until temen #1627 the wasm tier vetoed such a module outright (#1546), so every JACL
// program ran wholly on the interpreter; from #1664 the coop driver emits it in spill mode.
//
// Per card: the plain bytecode run (`temen_run_onramp`) is the oracle; the playground path
// (`runJitModule`) must match its stdout and status byte-for-byte. Reports which path ran, the
// driver's events, the spill words handed over at each bounce, and best-of-N wall time for both.
//
//   node bench_tierup_cards.mjs <temen_browser.wasm> [--floor N] [--runs N] <card.temen>...
//
// `--floor` sets the driver's tier-up size floor (default 4096, the production value); a huge floor
// keeps the coop path but tiers nothing up, which separates the coop run's own cost from tier-up's.
//
// The cdylib is the **threads** build (the playground's): it imports a shared `env.memory`.
// Cards come from `runtime/harness`'s `emit_temen <prog.jacl> <out.temen>`.
import { readFileSync } from 'node:fs';
import { basename } from 'node:path';
import { runJitModule } from '../../vendor/temen/browser/web/wasmjit-module.js';
import { engineImports } from '../../vendor/temen/browser/engine-imports.mjs';

const args = process.argv.slice(2);
const wasmPath = args.shift();
let floor = 4096; // the driver's production default, stated rather than inherited
let N = 3; // best of N, per path
for (;;) {
  if (args[0] === '--floor') { args.shift(); floor = Number(args.shift()); } else if (args[0] === '--runs') { args.shift(); N = Number(args.shift()); } else break;
}
if (!wasmPath || args.length === 0) {
  process.stderr.write('usage: node bench_tierup_cards.mjs <temen_browser.wasm> [--floor N] [--runs N] <card.temen>...\n');
  process.exit(2);
}

const memory = new WebAssembly.Memory({ initial: 2048, maximum: 16384, shared: true });
const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), engineImports(memory));
const ex = instance.exports;
const dec = new TextDecoder();
const u8 = () => new Uint8Array(memory.buffer);
const readStdout = () => dec.decode(u8().slice(
  Number(ex.temen_stdout_ptr()), Number(ex.temen_stdout_ptr()) + Number(ex.temen_stdout_len())));

// Event counters over the exports (the `bench_tierup.mjs` Proxy trick; zero engine changes). The
// third `temen_coop_call_interp` argument is the spilled-word count the emitted frames pushed.
const counts = {};
const reset = () => Object.assign(counts,
  { path: 'interp', tierups: 0, invokes: 0, bounces: 0, spilled: 0, spillMax: 0, spillBounces: 0 });
const exCounted = Object.fromEntries(Object.entries(Object.getOwnPropertyDescriptors(ex)).map(([k, d]) => {
  const v = d.value;
  if (typeof v !== 'function') return [k, v];
  if (k === 'temen_coop_func') return [k, (...a) => { counts.tierups++; return v(...a); }];
  if (k === 'temen_coop_jit_wasm_ptr') return [k, (...a) => { counts.invokes++; return v(...a); }];
  if (k === 'temen_coop_call_interp') {
    return [k, (...a) => {
      const n = Number(a[2] ?? 0);
      counts.bounces++;
      counts.spilled += n;
      if (n > 0) counts.spillBounces++;
      if (n > counts.spillMax) counts.spillMax = n;
      return v(...a);
    }];
  }
  if (k === 'temen_onramp_jit_run_open') return [k, (...a) => { const r = v(...a); if (r === 0) counts.path = 'whole-program'; return r; }];
  if (k === 'temen_coop_open') return [k, (...a) => { const r = v(...a); if (r === 0) counts.path = 'coop'; return r; }];
  return [k, v];
}));
ex.temen_coop_set_tierup_floor(floor);

const load = (b) => { const p = Number(ex.temen_alloc(b.length)); u8().set(b, p); return p; };
function bytecode(card) {
  const mp = load(card);
  const t0 = performance.now();
  ex.temen_run_onramp(mp, card.length, 0, 0);
  const ms = performance.now() - t0;
  const r = { ms, out: readStdout(), st: ex.temen_status() };
  ex.temen_dealloc(mp, card.length);
  return r;
}
async function coop(card, key) {
  reset();
  const t0 = performance.now();
  let threw = null;
  try { await runJitModule(exCounted, memory, card, new Uint8Array(0), key); } catch (e) { threw = e.message; }
  return { ms: performance.now() - t0, out: readStdout(), st: ex.temen_status(), threw, ...counts };
}

let bad = 0;
console.log(`cdylib: ${wasmPath}   tier-up floor: ${floor}\n`);
for (const p of args) {
  const card = new Uint8Array(readFileSync(p));
  const name = basename(p, '.temen');
  const oracle = bytecode(card);
  let bMs = oracle.ms;
  for (let i = 1; i < N; i++) bMs = Math.min(bMs, bytecode(card).ms);

  let best = await coop(card, `card#${name}`);
  if (!best.threw) {
    for (let i = 1; i < N; i++) { const r = await coop(card, `card#${name}`); if (r.ms < best.ms) best = r; }
  }
  const parity = !best.threw && best.out === oracle.out && best.st === oracle.st;
  if (!parity) bad++;
  console.log(`${name}  (card ${card.length} B, stdout ${oracle.out.length} B, status ${oracle.st})`);
  if (best.threw) console.log(`  coop: THREW ${best.threw}`);
  console.log(`  bytecode ${bMs.toFixed(1)}ms | path=${best.path} ${best.ms.toFixed(1)}ms `
    + `(${(bMs / best.ms).toFixed(2)}x) | parity=${parity ? 'OK' : 'MISMATCH'}`);
  console.log(`  events: tierups=${best.tierups} jit_invokes=${best.invokes} bounces=${best.bounces} `
    + `| spill: ${best.spillBounces} bounce(s) carried words, ${best.spilled} total, max ${best.spillMax}\n`);
}
process.exit(bad ? 1 : 0);
