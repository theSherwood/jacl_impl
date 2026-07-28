# SVM-backend playground path

Wires the JACL playground's **run** step onto the SVM backend's browser-viable
**bytecode interpreter** (via the `svm-browser` cdylib on `wasm32`), instead of the old
Emscripten bytecode VM (`demo/wasm/jacl.{js,wasm}`). This is the run half of
`docs/SVM_BROWSER_PLAN.md`; the engine blockers it depended on are closed (see
`docs/SVM_BROWSER_SPIKE_FINDINGS.md` — `vcpu.tls` lowered, the `gc.roots + thread` veto
dropped in the vendored svm).

## Pieces

| File | Role |
|---|---|
| `build_assets.sh` | Build `svm_browser.wasm` (cdylib → wasm32) into `demo/wasm/`, and link+encode example programs to `svm/svmb/*.svmb` at build time (+ `manifest.json`). |
| `run_svmb.mjs` | Headless Node driver — runs a `.svmb` through `svm_run_onramp` and prints captured stdout. CI gate for the run path. |
| `../src/svm-jacl-wasm.ts` | Browser module: `SvmJaclRunner.create(wasmUrl)` + `runSvmb(bytes) → RunResult` — the same `RunResult` shape as `jacl-wasm.ts`, so `playground.ts` stays backend-agnostic. |

Generated assets (`wasm/svm_browser.wasm`, `svm/svmb/`) are git-ignored and rebuilt by CI.

## Build + verify

```sh
rustup target add wasm32-unknown-unknown     # once
cd demo && bash svm/build_assets.sh          # → demo/wasm/svm_browser.wasm + demo/svm/svmb/*.svmb
node svm/run_svmb.mjs wasm/svm_browser.wasm svm/svmb/hi.svmb  $'hi\n'    # PASS
node svm/run_svmb.mjs wasm/svm_browser.wasm svm/svmb/gen.svmb $'1\n2\n'  # PASS
```

Both print through the cdylib on `wasm32`: `svm_status=0`, correct stdout.

## Why `svm_run_onramp` (not `svm_run_pb`)

A `.svmb` off `emit_svmb` (frontend → link vs `jaclrt.svm` → `synth_manifest_start` →
encode) reaches `print` through a **manifest import** `write` (`call.import "write"`).
`svm_run_onramp` binds manifest imports **by name** (`write` → the stdout stream), which
is the ABI that module expects. `svm_run_pb`'s arity-slot powerbox leaves `write` unbound,
so the guest traps (`STATUS_TRAP`).

## Wiring into the editor UI (the remaining slice)

The run path is proven; the last mile is the editor. Two steps:

1. **Now (precompiled examples).** `playground.ts` can run built-in examples on the SVM
   backend today: load the runner once, fetch the example's `.svmb` from `manifest.json`,
   and hand its bytes to `runSvmb` — the returned `RunResult` flows into the existing
   `displayResult` unchanged:

   ```ts
   import { SvmJaclRunner } from "./svm-jacl-wasm";
   const svm = await SvmJaclRunner.create("wasm/svm_browser.wasm");
   const svmb = new Uint8Array(await (await fetch(`svm/${entry.svmb}`)).arrayBuffer());
   displayResult(svm.runSvmb(svmb), elapsedMs);
   ```

2. **Live editing (needs the in-browser frontend).** Running *edited* source requires the
   LLVM-free frontend+codegen (`src/jacl.c` + `codegen/*.c`) compiled to `jacl_emit.wasm`
   via Emscripten, exposing `jacl_emit_ir(source) → SVM IR text`, then linked against
   `jaclrt.svm` and encoded in the browser (`SVM_BROWSER_PLAN.md` option (b)). That module
   does **not** exist yet — it is the next slice. Until it lands, free-form editing stays on
   the old backend and the SVM backend serves the precompiled example set.
