# When writing code

- Use functions/procedures instead of objects wherever possible
  - No OOP
- Prefer simple, straight-line code to complex abstractions
- Run tests with a timeout to avoid hanging tests during development.
- Avoid altering tests unless you know the test is testing the wrong thing.
- Pre-merge baselines for any change to `src/`: `./build.sh` and
  `./build.sh --tsan`. See `AUDIT.md` for rationale. (The legacy bytecode
  VM and its Emscripten `--wasm` check were removed in item 6 — SVM is the
  sole backend; the browser build now lives in `demo/svm/build_assets.sh`.)
- Prefer snake_case for values and functions. Prefer PascalCase for types.
- If there is a DESIGN.md, upon completion of an entire prd.json, update DESIGN.md to show what was completed. Be very concise about things already implemented.

## When writing JACL source

- Inside `{}` command-mode blocks (proc bodies, for/while/if bodies,
  top-level), write bare commands: `yield 10`, not `[yield 10]`;
  `print $x`, not `[print $x]`. `{}` is command mode — items separated
  by `;` / `,` / newline are each parsed as a head + args command.
- Use `[...]` only when nesting one command as a value inside another
  expression: `print [collect [gen]]` — `collect` is an arg to `print`,
  so it needs `[]`; `gen` is an arg to `collect`, same.
- This applies to test sources (`.c` strings, `test/jacl/*.jacl`) and
  any sample code in docs.
- For a single-file survey of working JACL syntax, see
  `test/jacl/tour.jacl` — values, bindings, the three modes,
  interpolation, procs, lambdas, if/while/for, streams,
  destructuring, structs, maps, mutable state, errors, and
  spawn/await/parallel/race. The test harness runs it, so it stays in
  sync with the implementation. `SYNTAX.md` is the full reference.

## When benchmarking

- `./build.sh --release` builds with `-O2` in `.build-release/`. The
  default debug build (no `-O`) is fine for correctness tests but a
  poor baseline for any runtime measurement.
- `./build.sh --test=perf` runs the in-process bench harness
  (`test/test_perf.c`); set `BENCH_TIMED_ITERS=N` for sharper medians
  and `BENCH_JSON_OUT=/path.jsonl` to capture results.
- `tools/bench-python.py` runs the Python ports of the same scenarios
  (`test/python/bench/*.py`), and `tools/bench-compare.sh A B` prints
  a side-by-side ratio table between any two JSONL outputs (either
  runtime).
- Cross-runtime baselines and per-step diff snapshots live under
  `docs/profiles/2026-05-22_*` — read those first before chasing a
  perf regression.

## When working on the playground

- The WASM playground lives in `demo/`. It's a TypeScript app
  (CodeMirror 6 + the `@replit/codemirror-vim` plugin + a JACL
  StreamLanguage syntax mode) bundled by esbuild. There is no
  frontend framework yet — Solid is the preferred default if
  reactivity warrants one later.
- `cd demo && bash build_demo.sh` does the whole pipeline: symlinks
  `wasm/`, regenerates `examples.json` from `test/jacl/*.jacl`, runs
  `npm ci` (one-time) on a fresh clone, then `npm run build` to
  produce `dist/playground.js`. The npm registry is reachable from
  the sandbox; the browser running the playground on the host is
  not, so the editor must be bundled to a local file the dev server
  serves directly.
- Source layout: `src/playground.ts` (entry — editor wiring, run
  pipeline, example picker, splitter, vim toggle), `src/jacl-mode.ts`
  (StreamLanguage tokenizer), `src/jacl-wasm.ts` (typed wrapper over
  `jacl_eval`), `src/splitter.ts` (drag-to-resize divider, persisted
  to localStorage). The bundle (`dist/playground.js`, ~425 KB
  minified) is committed so the playground works from a clone
  without a Node toolchain.
- `./node_modules/.bin/tsc --noEmit` typechecks; `npm run build` only
  runs esbuild and ignores types, so always typecheck separately when
  changing TS surfaces.
- Each Run click discards the previous JaclVM and spins up a fresh
  one. The cross-eval persistence work in `embed.c` (struct registry,
  ctx pool, GlobalArity table) is for embedders sharing one VM
  across calls — the playground intentionally doesn't want that, so
  Run is hermetic.
- Highlighting in `jacl-mode.ts` is position-driven, not just
  keyword-based:
    - first identifier after `[` → call head
    - first identifier after `|` → call head (pipe stage)
    - column-0 line that starts with a bareword → first ident is head
    - lines inside a multi-line `{..}` whose first non-whitespace
      char is a bareword → first ident is head
    - single-line `{..}` is treated as a list (params /
      destructuring / struct fields), heads inside stay plain
  See the demo commit history for the reasoning behind each case.

## When writing C

- Prefer arenas for memory management. This require organizing allocations by lifetime.
- Use data-oriented design for fast code with good cache locality.
- Follow the philosophy of Mike Acton, Casey Muratori, and Ryan Fleury.
- Prefer simple, direct code and flat data.
- Avoid the standard library.
- If the code is segfaulting or seeing memory corruption, it is not acceptable.
- If test code finds a memory handling issue in the implementation, that should be fixed in the implementation.
- Use typedefs to avoid unnecessary use of the `struct` keyword.
- Tests should make use of the memory tracking helpers in `test/test_helpers.h`.
  - This means that implementations either need to use the Allocator from `platform/platform.h` or they need to use macro-based polymorphism to allow the allocator to be set statically.
- Use helpers from `platform/platform.h` where appropriate.

### Dual-definition drift (gotchas)

Some definitions are **duplicated across files and can silently drift** — the
unity build (`src/jacl.c` `#include`s the `.c` files) means only one copy is
canonical per build:

- **`OpCode` enum.** Canonical copy is in `src/bytecode.c` (the VM dispatch
  table and the compiler's `OP_*` emits resolve against it via the unity build).
  `src/jacl.h` has a **second, drifted** `OpCode` enum that is *not* what the VM
  uses — adding an opcode there alone fails to link. **Add a new opcode to
  `src/bytecode.c`** (the enum + the `opcode_name` switch); the VM handler goes
  in `src/vm.c` (`CASE(OP_X)` label + the `dispatch_table[OP_X]` designated
  initializer — add at the enum's end so existing values don't renumber).
- **`Compiler` / `Runtime` / `WorkerThread` structs** are mirrored in `jacl.h`
  vs `compiler.c`/`runtime.c`. Offsets are offset-checked via
  `src/struct_drift_fields.h` (the `struct_sizes` test fails on drift); a new
  field must be added to *both* copies.
