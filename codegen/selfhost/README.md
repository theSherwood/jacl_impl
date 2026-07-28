# JACL compiler on SVM (self-host)

Build the JACL **compiler itself** to an SVM module (`jacl_compiler.svmb`) and run it
as an SVM guest — so the playground can compile edited source with **no Emscripten
frontend**, on the same engine that runs the compiled program.

This is the reproducible foundation (Slice 1) of the plan in
[`docs/SVM_SELFHOST_FEASIBILITY.md`](../../docs/SVM_SELFHOST_FEASIBILITY.md), which
records the measured feasibility results.

## Pieces

| File | Role |
|---|---|
| `emit_driver.c` | The compiler-guest entry: reads JACL source from **stdin** (`read(0,…)`, the on-ramp Stream.read cap) and writes emitted **SVM-IR text to stdout**. Calls `jacl_emit_ir` (the frontend in `codegen/tests/emit_jacl.c`). |
| `emit_shim.c` | The emit-path libc surface the on-ramp doesn't synthesize: `memcmp`, string ops, an integer-only `vsnprintf`/`snprintf`, `strtol`, `strerror`, `qsort`, `fmod`, `__errno_location`. Self-contained (no libc headers). |
| `inc/emscripten.h` | Stub so a native/dev build gets `EMSCRIPTEN_KEEPALIVE` (keeps `jacl_emit_ir` exported) without the emsdk. |
| `build_compiler_svmb.sh` | Compile the frontend + driver + shim to LLVM IR → `llvm-link` → `svm-llvm-translate` → `build/jacl_compiler.svmb`. `--selftest` also compiles a fixed program on the SVM engine and checks the emitted IR. |

`build/` is git-ignored.

## Build + verify

```sh
# needs clang-18 + llvm-link-18; builds svm-llvm from vendor/svm if not prebuilt
codegen/selfhost/build_compiler_svmb.sh --selftest
```

`--selftest` compiles `[+ 1 [* 2 3]]` through the compiler-guest on the SVM engine and
asserts the emitted IR contains the `2*3` / `1+6` (→ tagged `7`). Env overrides:
`CLANG`, `LLVM_LINK`, `SVM_LLVM_TRANSLATE`, `TRY_TRANSLATE`.

## Running by hand

`jacl_compiler.svmb` reads source on stdin, writes SVM IR on stdout. In the browser it
runs on `svm_browser.wasm` via `svm_run_onramp` (stdin = the edited source); the
emitted IR then goes to the generic `svm_link_run` (link vs `jaclrt.svm` + run) — the
same entry the current `jacl_emit.wasm` path feeds. Headless, with the demo cdylib:

```sh
# (once the cdylib is built — see demo/svm/) feed source in, get IR out:
printf '[+ 1 [* 2 3]]' | node demo/svm/run_svmb.mjs demo/wasm/svm_browser.wasm \
    codegen/selfhost/build/jacl_compiler.svmb
```

## Status / next

- **Slice 1 (this dir):** reproducible build + on-SVM selftest. **Done.**
- **Slice 2:** an emit-only build that doesn't link `vm.c`/`runtime.c`, shrinking the
  dead-runtime symbol surface and module size.
- **Slice 3:** reuse the `vendor/svm` `printf`/string shims; add an in-memory `use`
  module provider (or stay single-file).
- **Slice 4:** ship `jacl_compiler.svmb` into the playground behind a flag; keep
  `jacl_emit.wasm` (emcc) as fallback until parity is proven.
- **Slice 5:** pick the tier (bytecode vs wasm-JIT tier-up) once measured on latest svm.
