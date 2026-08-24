// ROI probe for the warm-snapshot / JIT compiler-guest plan (docs/SVM_WARM_COMPILER.md, Slice 0).
// Times the self-hosted compiler-guest (jacl_compiler.svmb) compiling several programs through the
// svm-browser cdylib's svm_run_onramp — the exact playground live-compile path, on the bytecode
// interpreter. Reports, per program, the wall time to compile.
//
// Decomposition: time(trivial) ≈ the fixed FLOOR (module decode + instantiate + _start + jaclrt
// init + minimal compile) — roughly what a warm-runtime snapshot removes. time(prog) − time(trivial)
// ≈ the MARGINAL per-source compile work — roughly what the JIT tier speeds up. The split sizes the
// ROI of warm-snapshot vs JIT-tier.
//
//   node bench_guest.mjs <svm_browser.wasm> <jacl_compiler.svmb> <prog1.jacl> [prog2.jacl ...]
import { readFileSync } from 'node:fs';

const [wasmPath, compPath, ...progs] = process.argv.slice(2);
if (!wasmPath || !compPath || progs.length === 0) {
  process.stderr.write('usage: node bench_guest.mjs <svm_browser.wasm> <jacl_compiler.svmb> <prog.jacl>...\n');
  process.exit(2);
}

const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), { svm_host: { webgpu_op: () => -1n } });
const ex = instance.exports;
const is64 = ex.svm_abi_is64() === 1;
const U = (x) => (is64 ? BigInt(x) : x);
const load = (b) => { const p = ex.svm_alloc(U(b.length)); new Uint8Array(ex.memory.buffer).set(b, Number(p)); return Number(p); };
const capStr = (pf, lf) => { const n = Number(lf()); return n === 0 ? '' : new TextDecoder().decode(new Uint8Array(ex.memory.buffer, Number(pf()), n).slice()); };

const svmb = readFileSync(compPath);
// Load the compiler module once into guest memory (its bytes don't change between compiles).
const svmbPtr = load(svmb);
const svmbLen = U(svmb.length);

function compileOnce(srcBytes) {
  const sp = load(srcBytes);
  ex.svm_run_onramp(svmbPtr, svmbLen, sp, U(srcBytes.length));
  const status = ex.svm_status();
  const ir = capStr(ex.svm_stdout_ptr, ex.svm_stdout_len);
  return { status, irLen: ir.length, isErr: (status !== 0 && status !== 5) || ir.startsWith('%%ERROR%%'), ir };
}

function timeProg(path, iters) {
  const srcBytes = new TextEncoder().encode(readFileSync(path, 'utf8'));
  const first = compileOnce(srcBytes);
  if (first.isErr) return { path, err: first.ir.slice(0, 200), srcLen: srcBytes.length };
  // median of `iters` timed runs
  const ts = [];
  for (let i = 0; i < iters; i++) {
    const t0 = performance.now();
    compileOnce(srcBytes);
    ts.push(performance.now() - t0);
  }
  ts.sort((a, b) => a - b);
  return { path, srcLen: srcBytes.length, irLen: first.irLen, min: ts[0], median: ts[(ts.length / 2) | 0], iters };
}

const TRIVIAL = 'print "hi"\n';
// synth a trivial program file inline (write via a data: source, not a file)
function timeInline(label, source, iters) {
  const srcBytes = new TextEncoder().encode(source);
  const first = compileOnce(srcBytes);
  if (first.isErr) return { path: label, err: first.ir.slice(0, 200), srcLen: srcBytes.length };
  const ts = [];
  for (let i = 0; i < iters; i++) { const t0 = performance.now(); compileOnce(srcBytes); ts.push(performance.now() - t0); }
  ts.sort((a, b) => a - b);
  return { path: label, srcLen: srcBytes.length, irLen: first.irLen, min: ts[0], median: ts[(ts.length / 2) | 0], iters };
}

const ITERS = 25;
const rows = [];
rows.push(timeInline('<trivial>', TRIVIAL, ITERS));
for (const p of progs) rows.push(timeProg(p, ITERS));

const floor = rows[0].median;
process.stdout.write(`\ncompiler-guest compile timings (bytecode interp, median of ${ITERS})\n`);
process.stdout.write(`  ${'program'.padEnd(28)} ${'src'.padStart(6)} ${'ir'.padStart(7)} ${'median'.padStart(9)} ${'min'.padStart(9)}   marginal(=med-floor)\n`);
for (const r of rows) {
  if (r.err) { process.stdout.write(`  ${String(r.path).padEnd(28)} ERROR: ${r.err}\n`); continue; }
  const marg = (r.median - floor);
  process.stdout.write(`  ${String(r.path).padEnd(28)} ${String(r.srcLen).padStart(6)} ${String(r.irLen).padStart(7)} ${r.median.toFixed(1).padStart(7)}ms ${r.min.toFixed(1).padStart(7)}ms   ${marg >= 0 ? '+' : ''}${marg.toFixed(1)}ms\n`);
}
process.stdout.write(`\n  FLOOR (trivial compile) = ${floor.toFixed(1)} ms  ≈ warm-snapshot's removable ceiling\n`);
const heavy = rows.find((r) => /tour/.test(String(r.path)) && !r.err);
if (heavy) {
  const W = heavy.median - floor;
  process.stdout.write(`  tour: floor ${floor.toFixed(1)}ms + per-source work ${W.toFixed(1)}ms = ${heavy.median.toFixed(1)}ms\n`);
  process.stdout.write(`  → warm-snapshot ROI ≈ ${(100 * floor / heavy.median).toFixed(0)}% of tour time; JIT-tier targets the other ${(100 * W / heavy.median).toFixed(0)}% (× its speedup).\n`);
}
