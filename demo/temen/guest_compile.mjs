// Compile a JACL source file to TEMEN IR by running the self-hosted compiler-guest (jacl_compiler.temen)
// through the temen-browser cdylib's `temen_run_onramp` — the macro-capable path (the emit-only C driver
// can't stage `defmacro`s). Writes the emitted IR to stdout; used by build_assets.sh to pre-bake the
// tour into a runnable .temen (via `emit_temen --ir`) so the unedited tour loads instantly.
//
//   node guest_compile.mjs <temen_browser.wasm> <jacl_compiler.temen> <prog.jacl>  > out.ir
import { engineImports } from '../../vendor/temen/browser/engine-imports.mjs';
import { readFileSync } from 'node:fs';

const [wasmPath, compPath, progPath] = process.argv.slice(2);
if (!wasmPath || !compPath || !progPath) {
  process.stderr.write('usage: node guest_compile.mjs <temen_browser.wasm> <jacl_compiler.temen> <prog.jacl>\n');
  process.exit(2);
}

const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), engineImports());
const ex = instance.exports;
const is64 = ex.temen_abi_is64() === 1;
const U = (x) => (is64 ? BigInt(x) : x);
const load = (b) => { const p = ex.temen_alloc(U(b.length)); new Uint8Array(ex.memory.buffer).set(b, Number(p)); return Number(p); };
const cap = (pf, lf) => { const n = Number(lf()); return n === 0 ? '' : new TextDecoder().decode(new Uint8Array(ex.memory.buffer, Number(pf()), n).slice()); };

const card = readFileSync(compPath);
const src = new TextEncoder().encode(readFileSync(progPath, 'utf8'));
ex.temen_run_onramp(load(card), U(card.length), load(src), U(src.length));

const status = ex.temen_status();
const ir = cap(ex.temen_stdout_ptr, ex.temen_stdout_len);
const err = cap(ex.temen_stderr_ptr, ex.temen_stderr_len);
if ((status !== 0 && status !== 5) || ir.startsWith('%%ERROR%%')) {
  process.stderr.write(`guest_compile: FAILED status=${status} ${err || ir.slice(0, 300)}\n`);
  process.exit(1);
}
process.stdout.write(ir);
