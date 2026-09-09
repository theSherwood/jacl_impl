# TEMEN-backend playground path

Wires the JACL playground's **run** step onto the TEMEN backend's browser-viable
**bytecode interpreter** (via the `temen-browser` cdylib on `wasm32`). This is now the
playground's **sole** engine — the old Emscripten bytecode VM (`jacl.{js,wasm}`) has been
removed. This is the run half of `docs/TEMEN_BROWSER_PLAN.md`; the engine blockers it depended
on are closed (see `docs/TEMEN_BROWSER_SPIKE_FINDINGS.md` — `vcpu.tls` lowered, the
`gc.roots + thread` veto dropped in the vendored temen).

## Pieces

| File | Role |
|---|---|
| `build_assets.sh` | Build `temen_browser.wasm` (cdylib → wasm32) + `jacl_emit.wasm` (frontend) into `demo/wasm/`, ship `jaclrt.temen`, and link+encode example programs to `temen/cards/*.temen` (+ `manifest.json`). |
| `build_emit_wasm.sh` | Build just `jacl_emit.wasm` — the LLVM-free frontend+codegen (Emscripten), exposing `jacl_emit_ir(source) → TEMEN IR text`. Needs emsdk on PATH. |
| `run_temen.mjs` | Headless Node driver — runs a `.temen` through `temen_run_onramp` and prints captured stdout. CI gate for the precompiled run path. |
| `concurrent_run.mjs` | CI gate for the browser **run** path on a `# mode: concurrent` `sleep` program — guards the wasm timed-wait against a wall-clock `Instant::now()` trap. |
| `stage_gate.mjs` | CI gate for the **macro-staging** path — asserts `jacl_emit.wasm` expands a user `defmacro` by staging its body on TEMEN, instead of silently falling back to the slow compiler-guest. |
| `../src/temen-jacl-wasm.ts` | Browser modules: `TemenJaclRunner` (`runTemen` precompiled / `linkRun` live / `linkRunRaw` macro-body), `JaclFrontend` (`emitIr`, staging hook), and the `RunResult` shape `playground.ts` renders. |

Generated assets (`wasm/temen_browser.wasm`, `temen/cards/`) are git-ignored and rebuilt by CI.

## Build + verify

```sh
rustup target add wasm32-unknown-unknown     # once
cd demo && bash temen/build_assets.sh          # → demo/wasm/temen_browser.wasm + demo/temen/cards/*.temen
node temen/run_temen.mjs wasm/temen_browser.wasm temen/cards/hi.temen  $'hi\n'    # PASS
node temen/run_temen.mjs wasm/temen_browser.wasm temen/cards/gen.temen $'1\n2\n'  # PASS
```

Both print through the cdylib on `wasm32`: `temen_status=0`, correct stdout.

## Why `temen_run_onramp` (not `temen_run_pb`)

A `.temen` off `emit_temen` (frontend → link vs `jaclrt.temen` → `synth_manifest_start` →
encode) reaches `print` through a **manifest import** `write` (`call.import "write"`).
`temen_run_onramp` binds manifest imports **by name** (`write` → the stdout stream), which
is the ABI that module expects. `temen_run_pb`'s arity-slot powerbox leaves `write` unbound,
so the guest traps (`STATUS_TRAP`).

## In the editor

`playground.ts` runs every program through the TEMEN backend (`runOnTemen`):

1. **Precompiled example (unedited):** fetch its `.temen` from `manifest.json` and run it
   through `TemenJaclRunner.runTemen` — fast, no compile.
2. **Edited source (live):** `JaclFrontend.emitIr(source)` compiles it to self-contained TEMEN
   IR text in the browser (`jacl_emit.wasm`); a compile error is shown as-is; otherwise
   `TemenJaclRunner.linkRun(ir, jaclrt.temen)` links it against the runtime and runs it. `linkRun`
   passes the program IR, the `jaclrt.temen` library, and the `__jacl_entry` entry name to the
   cdylib's **generic** `temen_link_run` (nothing JACL-specific lives in the cdylib). Own-data
   addresses are inline `data.self` instructions the linker resolves (wire v9), so there is no
   relocation buffer to pass. Both return the existing `RunResult`, so `displayResult` is
   unchanged.

The whole live pipeline (source → IR → link → run → stdout) is verified headless — see the
Node checks: `jacl_emit_ir("print \"hi\"")` → IR, then `temen_link_run` → `"hi\n"`; a
generator → `"1\n2\n"`; malformed input is rejected with the frontend's own diagnostic.

**Limitations.** Single-file only — a module program (top-level `use`) needs filesystem
import resolution and is rejected by the browser frontend for now. If the TEMEN assets aren't
built/shipped, the editor shows an "assets missing" note instead of running.

## Pages CI

The Pages workflow is in `.github/workflows_src/pages.yml` (the repo's unprivileged
source of truth for workflows — see that dir's `README.md`). It does submodule checkout,
Rust + `wasm32-unknown-unknown`, clang/LLVM-18, a `Build TEMEN assets` step
(`demo/temen/build_assets.sh`), and copies `temen_browser.wasm` / `jacl_emit.{js,wasm}` /
`jaclrt.temen` (+ the precompiled `.temen`) into `_site`. Promote edits into
`.github/workflows/` with the copy step documented in `.github/workflows_src/README.md`.
