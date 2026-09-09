# TEMEN in the browser — playground migration plan

> **Status:** plan + de-risking spike. The playground (`demo/`, deployed to GitHub
> Pages by `.github/workflows/pages.yml`) still runs the **old bytecode VM**
> (`src/jacl.c` → Emscripten). This doc scopes moving it onto the **TEMEN backend**,
> and records the spike that validates the one load-bearing risk.
>
> Companion docs: `TEMEN_BACKEND_DESIGN.md`, `TEMEN_BACKEND_PHASE2.md` (runtime
> translation + linking), `vendor/temen/BROWSER.md` (the temen-side browser story).

## TL;DR

The hard part is already built in `vendor/temen`. Two browser execution tiers exist
and are validated in real Chromium — an **TEMEN-IR → WebAssembly emitter**
(`temen-wasm-jit`, near-native speed) and a **bytecode interpreter that compiles to
`wasm32`** (the fail-closed fallback). A `browser/` cdylib (`temen-browser`) is the
wasm entry point, with a JS linker. **So this migration is integration, not
invention** — no new TEMEN machinery.

The work is JACL-specific glue: an in-browser frontend→IR step, shipping the
build-time-translated runtime as an IR blob, a JACL-shaped entry into the cdylib,
routing `print` to JS, and the Pages build wiring.

**The spike (below) changed the risk picture.** The `wasm32`-vs-`u64` concern is
retired — `wasm32` runs real allocating guests here (187/187). The actual blocker
is **op coverage**: the browser's only wasm-safe engine (the bytecode interpreter)
declines the `yielder`/`instantiator`/`cap.self` ops the JACL runtime uses, and
lowers all-or-nothing, so every JACL module is refused today. See **Spike results**.

## Why this is non-trivial

The playground is a **static site** — GitHub Pages, no server. So the *entire*
compile-and-run pipeline must run **client-side**. The old backend satisfies this
because `src/jacl.c` is self-contained C (frontend + bytecode VM) that Emscripten
turns into one `.wasm`. The TEMEN pipeline is three stages that do **not** reach the
browser equally:

| Stage | What it is | Browser story |
|---|---|---|
| Frontend + codegen | `src/jacl.c` + `codegen/codegen.c` + `codegen/irbuilder.c` (C, **LLVM-free** — only libc/libm) → TEMEN IR text | ✅ Emscripten-portable |
| Runtime translation | `temen-llvm` turns the C runtime (`runtime/*.c`) into TEMEN IR — **links libLLVM** | ⛔ native-only, but it's a **one-time build step**; its output ships as data |
| Execution | Rust: link IR + run via **interp** (tree-walk / bytecode) or **JIT** (Cranelift) | ⚠️ Cranelift emits *native* code — impossible in a browser sandbox. See tiers below |

## What already exists in `vendor/temen` (the enabler)

Confirmed by reading the crates and `BROWSER.md`:

- **`crates/temen-wasm-jit`** — `compile_module(&temen_ir::Module) -> Result<Vec<u8>, Error>`
  emits a native WebAssembly module from verified TEMEN IR. Hand-rolled encoder, zero
  deps, **fail-closed**: anything outside its subset returns `Error::Unsupported`
  and the caller keeps that function on the interpreter tier. Measured ~16–112×
  over interp-in-wasm, "at or below native Cranelift." The window-confinement mask
  (`eff & MASK`, trap on overrun) is baked into the emitted wasm — it is in the
  escape-TCB. Refused ops: fibers/`suspend`, `gc.roots`, inline host `cap.call`
  (the latter is hoisted out by `outline_cap_calls` so surrounding compute still
  emits).
- **Bytecode interpreter (`crates/temen-interp`, `Backend::Bytecode`)** — a
  cooperative single-OS-thread engine with **no platform surface** (no
  `std::thread`, `Instant`, or `page_size`). Compiles clean to
  `wasm32-unknown-unknown` (flag-free, Node-runnable) and `wasm64` (via `-Z
  build-std`). This is the fallback tier and the correctness floor.
- **`browser/` cdylib (`temen-browser`, `crate-type = ["cdylib","rlib"]`)** — the wasm
  entry point. It depends on `temen-interp` + `temen-wasm-jit` but **not** `temen-jit`
  (Cranelift), so no native-codegen deps enter the graph. Relevant exports:
  - `temen_run_pb(mod_ptr, mod_len, stdin_ptr, stdin_len) -> i64` — decode an encoded
    (`.temen`) module via `temen_encode::decode_module`, run function 0 under a
    **powerbox** (streams / clock / exit), and stash stdout/stderr.
  - `temen_alloc` / `temen_dealloc` — marshal bytes in.
  - `temen_stdout_ptr`/`temen_stdout_len`, `temen_stderr_ptr`/`temen_stderr_len`,
    `temen_exit_code`, `temen_status` — read results back.
  - `temen_abi_is64` — report the address ABI.
  - JS linker at `browser/web/wasmjit.js`; Node twins `browser/*.mjs`.
- The `temen-ir-wasm-comparison` branch you may have seen is **already merged** — it
  *is* this wasm-JIT tier.

## Target architecture

Everything client-side. Two wasm modules + one IR data blob + JS glue:

```
JACL source (editor)
   │  ① C frontend + codegen  ── Emscripten → jacl_emit.wasm
   ▼      jacl_emit_ir(source) → TEMEN IR text (+ SelfData relocs)
TEMEN IR text
   │  ② link vs runtime IR, encode to .temen            (see note)
   ▼
   │  ③ temen-browser cdylib  ── Rust → wasm32
   │      temen_run_pb(bytes) → run: bytecode-interp + wasm-JIT-lifted hot fns
   ▼
stdout / stderr / exit / status  ── read via accessor exports → JS
                                    (print routed through the powerbox)

   ④ jaclrt.temen(+.syms)  ── runtime-as-TEMEN-IR, baked ONCE at build time (temen-llvm)
                            shipped as a static data asset; linked in at ②
```

**Clean boundary:** module ① hands ② an IR *string*, so there is no shared-linear-
memory coupling between the Emscripten module and the Rust cdylib.

**Linking note (②).** `temen_run_pb` wants one self-contained encoded module (function
0 = entry). JACL program IR imports `jacl_*` runtime symbols, so program IR must be
**linked** against `jaclrt.temen` and encoded before it's handed in. Options, cheapest
first:
- **(a)** Link + encode natively at build time for fixed examples (MVP).
- **(b)** Do the link in the browser: add an entry to the cdylib that decodes
  `jaclrt.temen`, parses the program IR (`temen-text`), `temen_ir::link`s them, and runs —
  the machinery all exists in `temen-ir`/`temen-encode`; this is the piece to add for
  live editing.

## Execution tiers (the honest perf model)

Mixed-tier, which is exactly right for a playground:

- **Hot compute** → `temen-wasm-jit` lifts it to a native wasm module (near
  native-Cranelift).
- **Host-I/O / fibers / `gc.roots`** → stays on the bytecode interpreter-in-wasm
  (slower, but correct). `print`-bearing functions keep their compute on the fast
  tier via `outline_cap_calls`.

## The risk we expected: `wasm32` vs `u64` addresses (spike verdict: retired)

> **Update:** the spike **retired** this risk — `wasm32` ran temen's full 187/187
> differential corpus (incl. a 2 MiB alloc) in this container. The real blocker
> turned out to be bytecode-engine **op coverage** (see **Spike results**). This
> section is kept for the reasoning.


TEMEN addresses are `u64` end-to-end (`temen-mask` confines into `[0, 1<<reserved_log2)`;
every `temen-mem` offset is a `u64`). `wasm64` (memory64) is the *semantic* production
target. But **V8 does not support 64-bit wasm tables yet**, so the browser path is
**`wasm32` today**, which forces a `u64`→32-bit narrowing at every guest access.
`BROWSER.md` positions `wasm32` as the "compute-only, address-width-immaterial"
target.

**The untested-for-JACL question:** does the **full JACL GC runtime** — heap, boxes,
vectors, maps, strings, real allocation churn — run correctly and fit inside
`wasm32`'s 4 GiB space, or only compute kernels? This is the spike below. If `wasm32`
can't host the full runtime, the browser story waits on V8 memory64 tables (or a
narrower address configuration of `temen-mem`/`temen-mask`).

## Work breakdown

| # | Item | Size | Notes |
|---|---|---|---|
| 1 | Browser emit driver (C) | S | An `emit_jacl.c` variant taking arbitrary source → IR text (+ relocs); Emscripten-compile the **frontend+codegen only** |
| 2 | Bake `jaclrt.temen` at build time | S | `runtime/build.sh` already emits it; ship as a static asset. libLLVM stays out of the browser |
| 3 | JACL entry into the cdylib | M | Link program IR vs `jaclrt.temen` + encode (option (b) above), or reuse `temen_run_pb` on pre-linked bytes (option (a)) |
| 4 | `print`/powerbox → JS | S–M | Route the powerbox stream capture to the editor output pane |
| 5 | JS glue (`demo/src/jacl-wasm.ts`) | M | Orchestrate the two modules + runtime blob; keep the `RunResult` shape so `playground.ts` is ~untouched |
| 6 | Build + Pages CI | M | Build both wasm artifacts + bake runtime IR; retarget `pages.yml` (its own note invites this). Adds Rust + a wasm target to the job |

## Phased plan

0. **Spike (de-risk).** ✅ **Done** — see **Spike results**. `wasm32` is viable
   (187/187); the blocker is bytecode-engine op coverage for the JACL runtime.
1. **Unblock op coverage** *(new critical path)*. Prototype inter-function
   tree-shaking over the linked module (option A above) and re-run `emit_temen` +
   `run_pb`. Decision gate: does a pruned simple program fit the bytecode subset? If
   not, escalate to extending the bytecode engine (option B, upstream `vendor/temen`).
2. **MVP.** Once *some* class of programs runs, precompile the example programs'
   linked IR at build time; ship the cdylib + runtime IR; run examples in-browser
   (no live compile). Proves the end-to-end run path on Pages. Gate examples to the
   supported subset; surface a clear "unsupported feature in the browser" message
   otherwise.
3. **Full.** Add the Emscripten frontend→IR module for live editing; enable in-
   browser link (option (b)) and the `temen-wasm-jit` tier for hot compute; retarget
   `pages.yml`. Full concurrency/generator support in-browser depends on option B.

## Spike results

Two experiments, run in this repo's dev container (Node 22, `wasm32-unknown-unknown`,
clang-18). Tooling: `runtime/harness/src/bin/emit_temen.rs` (new — frontend→link→
powerbox→**encode**), temen's `browser/` cdylib, and temen's `run.mjs`/`corpus.mjs`.

### A. Address-width risk (`wasm32` vs `u64`) — **RETIRED**

- `temen-browser` cdylib **builds clean for `wasm32`** here (17 s) and runs under Node.
- temen's own **differential corpus passes 187/187 on `wasm32`** — 14 feature families
  including memory regions, address-space carving, shared regions, sub-guests,
  coroutines, durability freeze/thaw, and a **2 MiB `temen_alloc` echo**. The `u64`→32
  narrowing does not break the memory model for temen's feature set.

**Verdict:** `wasm32` is a viable host for real (allocating, GC-ed) guests today.
The address width is not the blocker.

### B. Full JACL runtime on the browser engine — **BLOCKED, precisely**

Linked a real JACL program (allocation + GC churn + a recursive proc + strings +
`print`; native output `10050` / `hello from temen-in-wasm`) into a 180 KB `.temen`
and ran it through the cdylib's `temen_run_pb` on `wasm32`:

```
temen_run_pb -> ret=0 status=2 exit=0     # status 2 = STATUS_UNSUPPORTED, no output
```

Even a trivial `print "hi"` returns `status=2`. Root cause, traced end to end:

1. The browser cdylib's powerbox runs on the **bytecode engine**
   (`bytecode::compile_and_run_with_host`). It **cannot** use the tree-walker — that
   one is scheduled on real OS threads/timers and is "hostile to wasm."
2. The bytecode engine's `compile_module` is **all-or-nothing across functions**: it
   lowers *every* function and returns `None` (→ `STATUS_UNSUPPORTED`) if **any one**
   uses an op outside its subset. Declined ops include the `INSTANTIATOR` and
   `YIELDER` capabilities and `cap.self` ops 9/10 (`temen-interp/src/bytecode.rs:1346,
   1366`).
3. Linking pulls in the **whole JACL runtime** (`jaclrt.temen`, 232 funcs), which uses
   `cap.self` (16 sites) and `cap.call` (23 sites) — the machinery behind generators,
   `spawn`/`await`, and context introspection. So **one runtime function using a
   declined op disqualifies the entire module**, regardless of what the program does.
4. There is **no inter-function dead-code pass** to prune the unused runtime
   functions before compile — `temen-opt`'s DCE is intra-function (dead blocks/defs)
   only.

The native TEMEN path works only because it runs on the **tree-walker or Cranelift
JIT** (fuller op coverage) — neither of which is browser-viable.

### What the spike changes

The browser blocker is **not** wasm feasibility (proven) — it is **op coverage on
the browser's only wasm-safe engine**. That re-prioritizes the work:

- **(A) Inter-function tree-shaking** *(likely cheapest, JACL-side or a new temen
  pass)* — prune the linked module to functions reachable from `_start`. A program
  that uses no generators/`spawn` would then drop the `yielder`/`instantiator`
  runtime functions and could fit the bytecode subset. **Open question to verify
  next:** are the functions *reachable* from a simple `print` program all within the
  subset, or do core paths (the `print`/GC path itself) use a declined `cap.self` op?
  If the latter, tree-shaking alone isn't enough.
- **(B) Extend the bytecode engine** to lower `yielder`/`instantiator`/`cap.self`
  9-10 *(upstream `vendor/temen`, larger)* — the only path that gets JACL's full
  concurrency/generator feature set running in-browser.
- **(C) `temen-wasm-jit` tier** helps compute speed but **not** this coverage gap: it
  also fail-closes on `cap.call`/fibers and falls back to the same bytecode engine.

**Recommended next step:** prototype (A) — a reachable-function prune over the linked
module before `encode_module` — and re-run `emit_temen` + `run_pb`. If a pruned
`print "hi"` returns `status=0` with output, tree-shaking is the unlock and the MVP
becomes "playground runs any program whose reachable runtime slice is within the
bytecode subset," with (B) tracked for full-feature parity.

### Reproduction

```sh
bash runtime/build.sh                                   # → runtime/build/jaclrt.temen (clang-18 + temen-llvm)
cd vendor/temen/browser
cargo build --release --lib --target wasm32-unknown-unknown
cargo run --bin gencorpus >/dev/null && node corpus.mjs \
  target/wasm32-unknown-unknown/release/temen_browser.wasm     # A: 187/187 on wasm32
cd ../../.. && cd runtime/harness
cargo run --bin emit_temen -- prog.jacl /tmp/prog.temen        # B: link+encode a JACL program
# then drive temen_run_pb on the wasm32 cdylib from Node and read temen_stdout_ptr/len
```

## Load-bearing references

- `vendor/temen/BROWSER.md` — the temen-side browser story (tiers, `wasm32`/`wasm64`).
- `vendor/temen/crates/temen-wasm-jit/src/lib.rs` — the IR→wasm emitter.
- `vendor/temen/browser/src/lib.rs` — the cdylib entry (`temen_run_pb`, accessors).
- `vendor/temen/browser/web/wasmjit.js` — the JS linker pattern.
- `runtime/build.sh` — how `jaclrt.temen` is produced (clang → `temen-llvm-translate`).
- `codegen/tests/emit_jacl.c` — the frontend→IR driver to adapt for the browser.
- `demo/src/jacl-wasm.ts`, `.github/workflows/pages.yml` — the integration points.
