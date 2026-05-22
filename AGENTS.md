# When writing code

- Use functions/procedures instead of objects wherever possible
  - No OOP
- Prefer simple, straight-line code to complex abstractions
- Run tests with a timeout to avoid hanging tests during development.
- Avoid altering tests unless you know the test is testing the wrong thing.
- Three pre-merge baselines for any change to `src/` or `include/`:
  `./build.sh` (87/0), `./build.sh --tsan` (86/2 known-and-safe),
  `./build.sh --wasm` (Emscripten compile check; skipped if `emcc`
  isn't installed). See `AUDIT.md` for rationale.
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
