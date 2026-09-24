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
| `workers_gate.mjs` | CI gate for the **Worker driver** — drives the real page in Chromium (with COOP/COEP): a precompiled pool-starting example and an edited `parallel` program run on Web Workers, a program that starts no pool stays single-threaded. |
| `stage_gate.mjs` | CI gate for the **macro-staging** path — asserts `jacl_emit.wasm` expands a user `defmacro` by staging its body on TEMEN, instead of silently falling back to the slow compiler-guest. |
| `../src/temen-jacl-wasm.ts` | Browser modules: `TemenJaclRunner` (`runTemen` precompiled / `linkRun` live / `linkEncode` link-only / `linkRunRaw` macro-body), `JaclFrontend` (`emitIr`, staging hook), and the `RunResult` shape `playground.ts` renders. |
| `../src/temen-workers.ts` | `WorkerDriver`: a Run on temen's parallel Worker driver — its threads engine + `par.js`/`worker.js` from `demo/temen-web/`, a fresh engine per run, `JACL_WORKERS=N` in the run's environment. |

Generated assets (`wasm/temen_browser.wasm`, `temen/cards/`, `temen-web/`, `coi-serviceworker.js`)
are git-ignored and rebuilt by CI.

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

### Parallel Runs on Web Workers (jacl #152)

A program whose source uses `spawn`, `parallel` or `race` — the constructs that start the
scheduler's worker pool — runs on temen's **parallel Worker driver** when the page can: each pool
worker on its own Web Worker over one shared memory, instead of multiplexed on the page's thread.
The status line then ends in `[N workers]`, N being `navigator.hardwareConcurrency` (at most 16).
The runtime learns N from the run's environment (`JACL_WORKERS=N`, read by `sched_init`); the same
`.temen` runs single-threaded without it, so both a precompiled card and a live-linked program
(`linkEncode`) go to either driver unchanged.

Measured in Chromium on 4 cores (a card built by `emit_temen`, identical output on both drivers):

| program | single-threaded | Workers, N=4 |
|---|--:|--:|
| 4-way `parallel` fold, 50M integers each | 120.7 s | **31.8 s (3.8x)** |
| `print "hi"` (starts no pool) | 5 ms | 55 ms |

The second row is why a program that starts no pool stays single-threaded: the Worker driver's
start-up (a fresh shared memory and JACL's 64 MiB window per run, Worker boot) buys it nothing.
Across the 69 concurrency examples that build with `emit_temen`, both drivers print identical
output; the one that differed (`gc_stress_atom`) exposed `swap` losing updates under real
parallelism, now a compare-and-swap.

It needs **cross-origin isolation** (shared memory). GitHub Pages cannot send the COOP/COEP
headers, so `index.html` loads `coi-serviceworker.js`, which adds them (reloading once on the first
visit). Without isolation, a single core, or a build without the threads engine (`build_assets.sh`
step 1a needs nightly + `rust-src`), every Run stays single-threaded.

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
