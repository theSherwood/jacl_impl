// Compile a JACL source file to SVM IR by running the self-hosted compiler-guest (jacl_compiler.svmb)
// through the svm-browser cdylib's `svm_run_onramp` — the macro-capable path (the emit-only C driver
// can't stage `defmacro`s). Writes the emitted IR to stdout; used by build_assets.sh to pre-bake the
// tour into a runnable .svmb (via `emit_svmb --ir`) so the unedited tour loads instantly.
//
//   node guest_compile.mjs <svm_browser.wasm> <jacl_compiler.svmb> <prog.jacl>  > out.ir
import { readFileSync } from 'node:fs';

const [wasmPath, compPath, progPath] = process.argv.slice(2);
if (!wasmPath || !compPath || !progPath) {
  process.stderr.write('usage: node guest_compile.mjs <svm_browser.wasm> <jacl_compiler.svmb> <prog.jacl>\n');
  process.exit(2);
}

const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), { svm_host: { webgpu_op: () => -1n } });
const ex = instance.exports;
const is64 = ex.svm_abi_is64() === 1;
const U = (x) => (is64 ? BigInt(x) : x);
const load = (b) => { const p = ex.svm_alloc(U(b.length)); new Uint8Array(ex.memory.buffer).set(b, Number(p)); return Number(p); };
const cap = (pf, lf) => { const n = Number(lf()); return n === 0 ? '' : new TextDecoder().decode(new Uint8Array(ex.memory.buffer, Number(pf()), n).slice()); };

const svmb = readFileSync(compPath);
const src = new TextEncoder().encode(readFileSync(progPath, 'utf8'));
ex.svm_run_onramp(load(svmb), U(svmb.length), load(src), U(src.length));

const status = ex.svm_status();
const ir = cap(ex.svm_stdout_ptr, ex.svm_stdout_len);
const err = cap(ex.svm_stderr_ptr, ex.svm_stderr_len);
if ((status !== 0 && status !== 5) || ir.startsWith('%%ERROR%%')) {
  process.stderr.write(`guest_compile: FAILED status=${status} ${err || ir.slice(0, 300)}\n`);
  process.exit(1);
}
process.stdout.write(ir);
