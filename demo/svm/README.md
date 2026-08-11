# SVM-backend playground path

Wires the JACL playground's **run** step onto the SVM backend's browser-viable
**bytecode interpreter** (via the `svm-browser` cdylib on `wasm32`). This is now the
playground's **sole** engine — the old Emscripten bytecode VM (`jacl.{js,wasm}`) has been
removed. This is the run half of `docs/SVM_BROWSER_PLAN.md`; the engine blockers it depended
on are closed (see `docs/SVM_BROWSER_SPIKE_FINDINGS.md` — `vcpu.tls` lowered, the
`gc.roots + thread` veto dropped in the vendored svm).

## Pieces

| File | Role |
|---|---|
| `build_assets.sh` | Build `svm_browser.wasm` (cdylib → wasm32) + `jacl_emit.wasm` (frontend) into `demo/wasm/`, ship `jaclrt.svm`, and link+encode example programs to `svm/svmb/*.svmb` (+ `manifest.json`). |
| `build_emit_wasm.sh` | Build just `jacl_emit.wasm` — the LLVM-free frontend+codegen (Emscripten), exposing `jacl_emit_ir(source) → SVM IR text`. Needs emsdk on PATH. |
| `run_svmb.mjs` | Headless Node driver — runs a `.svmb` through `svm_run_onramp` and prints captured stdout. CI gate for the precompiled run path. |
| `concurrent_run.mjs` | CI gate for the browser **run** path on a `# mode: concurrent` `sleep` program — guards the wasm timed-wait against a wall-clock `Instant::now()` trap. |
| `stage_gate.mjs` | CI gate for the **macro-staging** path — asserts `jacl_emit.wasm` expands a user `defmacro` by staging its body on SVM, instead of silently falling back to the slow compiler-guest. |
| `../src/svm-jacl-wasm.ts` | Browser modules: `SvmJaclRunner` (`runSvmb` precompiled / `linkRun` live / `linkRunRaw` macro-body), `JaclFrontend` (`emitIr`, staging hook), and the `RunResult` shape `playground.ts` renders. |

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

## In the editor

`playground.ts` runs every program through the SVM backend (`runOnSvm`):

1. **Precompiled example (unedited):** fetch its `.svmb` from `manifest.json` and run it
   through `SvmJaclRunner.runSvmb` — fast, no compile.
2. **Edited source (live):** `JaclFrontend.emitIr(source)` compiles it to self-contained SVM
   IR text in the browser (`jacl_emit.wasm`); a compile error is shown as-is; otherwise
   `SvmJaclRunner.linkRun(ir, jaclrt.svm)` links it against the runtime and runs it. `linkRun`
   passes the program IR, the `jaclrt.svm` library, and the `__jacl_entry` entry name to the
   cdylib's **generic** `svm_link_run` (nothing JACL-specific lives in the cdylib). Own-data
   addresses are inline `data.self` instructions the linker resolves (wire v9), so there is no
   relocation buffer to pass. Both return the existing `RunResult`, so `displayResult` is
   unchanged.

The whole live pipeline (source → IR → link → run → stdout) is verified headless — see the
Node checks: `jacl_emit_ir("print \"hi\"")` → IR, then `svm_link_run` → `"hi\n"`; a
generator → `"1\n2\n"`; malformed input is rejected with the frontend's own diagnostic.

**Limitations.** Single-file only — a module program (top-level `use`) needs filesystem
import resolution and is rejected by the browser frontend for now. If the SVM assets aren't
built/shipped, the editor shows an "assets missing" note instead of running.

## Pages CI

The Pages workflow is in `.github/workflows_src/pages.yml` (the repo's unprivileged
source of truth for workflows — see that dir's `README.md`). It does submodule checkout,
Rust + `wasm32-unknown-unknown`, clang/LLVM-18, a `Build SVM assets` step
(`demo/svm/build_assets.sh`), and copies `svm_browser.wasm` / `jacl_emit.{js,wasm}` /
`jaclrt.svm` (+ the precompiled `.svmb`) into `_site`. Promote edits into
`.github/workflows/` with the copy step documented in `.github/workflows_src/README.md`.
