# Running the JACL compiler *on* SVM — feasibility (measured)

> **Status:** spike results, reproducible. Answers the design question raised
> against the browser migration: the playground's live-editing path currently
> compiles edited source with `jacl_emit.wasm` (the C frontend built to wasm by
> **Emscripten**). Could we instead compile the JACL **compiler itself** to SVM IR
> (via the `vendor/svm` LLVM on-ramp) and run it as an SVM guest on
> `svm_browser.wasm` — one engine, no emcc? **Yes.** This note records the numbers.
>
> Tooling: `clang-18` + `llvm-link-18` (C → LLVM IR), `vendor/svm`'s
> `svm-llvm-translate` / `try_translate` (LLVM IR → SVM IR → verify → run). The
> reproducible harness lives in `codegen/selfhost/`.

## TL;DR

The whole JACL frontend compiles to SVM IR, verifies, runs on the SVM engine, and
**compiles JACL end-to-end as an SVM guest** — source in, correct SVM IR out.

| Question | Measured result |
|---|---|
| svm-llvm translates the whole compiler? | **Yes** — 1027 funcs (frontend lib) / 1031 (with driver), verified |
| Runs on the SVM engine? | **Yes** — executes, clean exit |
| Compiles JACL end-to-end on SVM? | **Yes** — `[+ 1 [* 2 3]]` → correct tagged-`7` SVM IR |
| libc bring-up cost? | **~80-line shim**, integer-only, no filesystem |
| Fast tier available? | **55% wasm-JIT tier-up** today (float-limited; a lower bound — see caveats) |

My earlier "this is a big lift" instinct was wrong. This is a **medium-*small*
bring-up**, now demonstrated rather than argued.

## Why this matters

`emcc` only ever produces a *browser* artifact. Compiling the compiler through the
SVM on-ramp makes the **entire JACL compiler a portable SVM module** that runs
anywhere SVM runs, unifies the playground on one engine, and dogfoods the on-ramp.
It also composes with the generic `svm_link_run` we already landed: the
compiler-guest emits IR → `svm_link_run` links it against `jaclrt.svm` and runs it.
`emcc` was the cheaper way to *ship the playground*; this is the better architecture
if "SVM everywhere" is a goal.

## Method

Compile the exact browser-frontend translation units (`codegen/tests/emit_jacl.c`,
a unity include of `src/jacl.c`, plus `codegen/codegen.c` and `codegen/irbuilder.c`)
to LLVM IR, link, and run through the on-ramp:

```sh
CF="-std=c99 -O2 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -D__EMSCRIPTEN__ \
    -Wno-implicit-function-declaration -fno-vectorize -fno-slp-vectorize -emit-llvm -S -I codegen"
clang-18 $CF codegen/tests/emit_jacl.c -o emit_jacl.ll   # unity: full frontend
clang-18 $CF codegen/codegen.c        -o codegen.ll
clang-18 $CF codegen/irbuilder.c      -o irbuilder.ll
llvm-link-18 -S emit_jacl.ll codegen.ll irbuilder.ll -o jacl_frontend.ll
SVM_STUB_EXTERNS=1 try_translate jacl_frontend.ll
```

(`-D__EMSCRIPTEN__` selects the browser build config — no computed-goto, no threads
— and exposes the `jacl_emit_ir(const char*)` entry. A stub `emscripten.h` supplies
`EMSCRIPTEN_KEEPALIVE`.)

## Results

### 1. Translate + verify + run

```
TRANSLATED in 4.36s: 1027 funcs
VERIFIED
RAN in 6.01s: outcome Returned([I32(2)])
--- stdout ---
usage: emit_jacl <case> | emit_jacl --file <path> | emit_jacl --oldvm <case>
```

The whole frontend translated (no gap, no unsupported construct), passed the
escape-freedom **verifier**, and **executed** on the SVM engine — the built-in CLI
`main` ran and printed its usage line (no case arg was passed).

### 2. Undefined-symbol footprint — 59 total, none fatal

`llvm-nm` on the linked module lists 59 undefined externals, split cleanly:

- **Dead runtime surface (~33):** `fork`/`execvp`/`pipe`/`popen`/`opendir`/`open`/
  `read`/`write`/`kill`/`waitpid`/`realpath`/`fopen`… — pulled in only because the
  unity build links `vm.c`/`runtime.c` (the interpreter, via `jacl_vm_new`), which
  the emit path never calls. `--stub-externs` makes them trap-if-called; the run
  never reaches them. An **emit-only build drops most of these** (see Slice 2).
- **Emit-path tail (~26):** `malloc`/`calloc`/`realloc`/`free` (on-ramp
  **synthesized**), plus the reusable-shim set — `memcmp`, `strlen`/`strcmp`/
  `strcpy`/`strrchr`, `snprintf`/`vsnprintf`, `qsort`, `strtol`, `strerror`, `fmod`,
  `__errno_location`.

Measured emit-path libc call counts (codegen + pipeline `.c`, excluding the runtime
TUs): `memcmp` 412, `snprintf` 173, `memcpy` 82, `strlen` 50, `memset` 37, `free`
33, `realloc` 16, `calloc` 13, `malloc` 6, `strrchr` 5, `strcmp` 3, `vsnprintf` 2,
`strcpy` 1, `fmod` 1. The IR-text builder's format specifiers are **integer/string
only** — `%u %s %d %llu %lld %02x` — no `%f`/`%g`.

### 3. wasm-JIT emittability — mixed tier, majority emittable

```
funcs=1027  emittable=565 (55.0%)  bytecode-only=462  emitted_wasm_bytes=602609
compile_module (whole-module): ERR Unsupported("a function is outside the integer subset")
```

`compile_module_tierup` emits 55% of functions to wasm (the rest interpret with
cross-tier calls — the same model the browser threads tier uses). Strict
whole-module emit fails on "the integer subset" — the `fmod` + float-literal path.

### 4. End-to-end: JACL source → SVM IR, on the SVM engine

A driver whose `main` calls `jacl_emit_ir("[+ 1 [* 2 3]]")` and writes the result to
stdout, linked with the frontend + an ~80-line emit-path libc shim, translated and
**ran**:

```
TRANSLATED: 1031 funcs
VERIFIED
RAN: outcome Returned([I32(0)])
--- stdout ---
=== JACL source: [+ 1 [* 2 3]]
=== emitted SVM IR:
func (i64) -> (i64) { block 0 (v0: i64) {
  v1 = i32.const 0
  call.sym "jacl_heap_init"   (i64) -> () v1(v0)
  call.sym "jacl_intern_init" (i64) -> () ...
  v4 = ref.func 1
  v7 = call.sym "jacl_sched_run_main" (i64, i64) -> (i64) ...
  return v7 } }
func (i64, i64) -> (i64) { block 0 (v0: i64, v1: i64) {
  v3 = suspend -1
  v5 = i32.const 2
  v6 = i32.const 3
  v7 = i32.mul v5 v6          # 2*3
  v8 = i32.add v4 v7          # 1+6
  v9 = i64.extend_i32_u v8
  v10 = i64.const 144115188075855872   # 1<<57 small-int tag
  v11 = i64.or v9 v10
  return v11 } }              # → 7, tagged
```

The compiler-guest produced correct SVM IR for `1 + 2*3 = 7` (tagged small-int).

### 5. The libc bring-up cost

Closing the run took **one self-contained ~80-line shim** (`memcmp`, string ops, an
**integer-only** `vsnprintf`/`snprintf`, `strtol`, `strerror`, `qsort`, `fmod`,
`__errno_location`). `malloc`-family and `memcpy`/`memset` needed nothing — the
on-ramp synthesizes them. No filesystem, no floats in the formatter, nothing near
`tcl_shim.c`'s ~70-symbol scale. In a real build, link the existing
`vendor/svm/crates/svm-run/demos/postgres/printf_shim.c` + string shims instead of a
hand-rolled one.

## Caveats

1. **On-ramp version.** Measured against the current `vendor/svm` checkout. A newer
   svm has grown the wasm-JIT subset (e.g. chibicc moved to `jit:true`, i.e. float
   support landed), so the 55% tier-up figure and the whole-module "integer subset"
   failure are a **lower bound** — both should improve on latest.
2. **Dev-lane toolchain.** The on-ramp is Linux + `clang-18`, workspace-excluded,
   off the runtime path. Making it a Pages CI build dependency is heavier and less
   portable than emsdk.
3. **`use`/module resolution.** The one fundamental filesystem dependency in the
   frontend is `use` import resolution (`src/compiler.c` `module__resolve_path` /
   `module__read_file`). Single-file programs never reach it (the browser path
   already rejects top-level `use`). Full support needs an in-memory module provider
   — a small, localized change to those two functions.

## Implementation plan

Slices, smallest-useful first. Each is independently landable.

1. **Self-host build harness** (`codegen/selfhost/`) — a reproducible
   `build_compiler_svmb.sh` (emit TUs + driver + shim → `llvm-link` →
   `svm-llvm-translate` → `jacl_compiler.svmb`), the stdin→IR driver
   (`emit_driver.c`), and the emit-path shim (`emit_shim.c`). Guards on
   `clang-18`/svm-llvm; skips cleanly when absent (like the svm Tcl demo). This
   pins the spike as a repeatable artifact.
2. **Emit-only build** — a frontend TU that does **not** drag in `vm.c`/`runtime.c`
   (no `jacl_vm_new`), so the dead-runtime symbol surface and module size shrink.
3. **Real shims + `use` provider** — replace the hand-rolled shim with the reused
   `printf`/string shims; add an in-memory `name → source` module provider (or keep
   single-file for the first browser cut).
4. **Browser wiring** — ship `jacl_compiler.svmb`; run it on `svm_browser.wasm`,
   feeding edited source via stdin and capturing emitted IR via stdout, then hand
   that IR to the existing `svm_link_run`. Behind a flag; keep `jacl_emit.wasm`
   (emcc) as the fallback until parity is proven.
5. **Tier selection** — measure bytecode vs wasm-JIT on current svm; adopt tier-up
   if the module emits.

## Reproduce

See `codegen/selfhost/README.md`. The build script produces `jacl_compiler.svmb`;
feed a JACL program on stdin to get SVM IR on stdout.
