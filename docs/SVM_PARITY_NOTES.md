# SVM backend parity — bring-up notes

Working notes for grinding `docs/SVM_PARITY.md` (the generated scoreboard) toward
full corpus parity. This is the *how* and *why* behind the slices; the scoreboard
itself is the *what's-left*.

## The pipeline under test

`runtime/harness/src/bin/parity.rs` drives every `test/jacl/*.jacl` through the real
backend and scores it against the file's inline oracle:

```
frontend (lex → parse → typer)          # src/jacl.c, via codegen/tests/emit_jacl.c
  → SVM codegen (AST → SVM-IR)           # codegen/codegen.c
  → link against the translated runtime  # runtime/jaclrt.c, svm-llvm → svm_ir::link
  → powerbox + instantiate
  → run_diff (interp == jit)             # svm-run
  → compare stdout to `# expect:` lines
```

Each case is bucketed by the furthest stage it reaches, so the scoreboard doubles as a
prioritized worklist:

| stage | meaning |
|---|---|
| `pass` / `err-pass` | output (or the expected compile error) matches |
| `emit-fail` | the frontend or codegen rejected it (the breadth gap) |
| `err-no-fail` | expected a compile error, but it compiled + ran |
| `err-wrong-msg` | expected a compile error, got a *different* message |
| `wrong-output` | ran, but stdout ≠ expects |
| `run-fail` / `text/link-fail` | a pipeline stage after codegen failed |
| `hang` | exceeded the per-case timeout |

Regenerate with `cargo run --release --bin parity` from `runtime/harness/` (needs
`--release`: svm-jit's `gc.roots` scan trips a debug-only precondition otherwise).

## The static-error oracle (the biggest lever)

The `expect-error` corpus (~128 cases) checks the old VM's *compile-time* rejections:
typed def/set/arith/proc-arg/convert mismatches, struct/ctx/buf bounds, generator-tail
and typed-closure conformance, and so on. Re-deriving all of those in the SVM codegen
would be a large, error-prone duplication.

Instead the driver (`codegen/tests/emit_jacl.c`) runs the old VM's own
`compiler_compile` purely for its diagnostics. Every corpus program is *written for the
old VM*, so:

- a passing program compiles cleanly → the SVM codegen runs and we score its output;
- an `expect-error` program is rejected with its canonical message → we surface that and
  stop.

This mirrors the old VM's accept/reject decision exactly, which is the definition of
parity. Two subtleties matter:

1. **Isolate the ASTs.** Both the compiler's conformance pass and the SVM codegen mutate
   their AST in place (inferred types, synthesized sugar, macro expansion). The compiler
   check therefore runs on its *own* re-lexed/re-parsed tree; codegen keeps the original.
   Sharing one tree cross-contaminates both (it regressed ~24 codegen cases one way and
   ~2 conformance cases the other).
2. **Compiler first, on the separate parse.** Running the check before codegen lets the
   compiler's canonical message win for the `err-wrong-msg` cases, while codegen still
   sees pristine nodes — so there are *zero* regressions on passing programs.

This slice alone moved `err-pass` from 8 → 115.

## Completed slices (Phase 4)

- **Runtime error semantics.** `print`/`to-string` render error-flagged values as
  `<error: PAYLOAD>`; the error flag propagates through map/string/vector ops.
- **Command heads.** `?.`, `not`, `~`, `vec-slice`, `range-inclusive`, `index`/`slice`
  (string + collection), `first`/`count`, `take`, 3-arg dot mutation `[. e k v]`,
  argument spread `..$v`, `ptr-null`/`ptr-cast`/`ptr-addr` (u64 round-trip), a
  `stack-trace` stub.
- **Typed `Map` constructor** `[[Map K V] …]`.
- **`for`-over-map** (one-name value, two-name key+value) via uniform iter accessors.
- **Variadic proc params** `..rest` (pack trailing call args into a vector).
- **`assert` predicate form** `[assert OP a b]`.
- **Old-VM-shaped arity messages** (correct singular/plural).

## Remaining clusters (and why they're hard)

- **Concurrency hangs (~69).** `spawn`/`await`/`parallel`/`race`/`cps`/`gc`/`sleep`
  stress. The M:N scheduler + fiber machinery needs to terminate cleanly under the
  differential (interp==jit) oracle; this is scheduler/runtime work, not codegen breadth.
- **Buffers + pointers (`addr`, `ptr-deref`, `ptr-offset`, `buf-unchecked-set`).** These
  need real linear-memory-backed buffers and address arithmetic; today `[Buf N T]` lowers
  to a JaclVal array, so `addr`/`ptr-deref` can't read raw element bytes. Round-trip-only
  pointer ops (`ptr-null`/`ptr-cast`/`ptr-addr`) are done; memory-walking ones are not.
- **Atom watchers (`watch`).** Fire a JACL closure synchronously from inside a runtime C
  `reset`/`swap`. Closures dispatch via `call_indirect` in generated IR, so calling back
  into guest code from the runtime needs a trampoline that doesn't exist yet.
- **File I/O (`read-file`/`write-file`).** Needs Filesystem powerbox capabilities wired
  through the harness; only the stdout Stream cap is granted today.
- **Typed-collection printing.** The old VM prints a typed `[Vec T]` as `[1, 2, 3]` and an
  untyped `[vec …]` as `[vec 1 2 3]`. Matching this needs the runtime to distinguish
  typed vectors, which the current single vector representation does not.
- **Early `return` in generators.** Mid-body `[return]` that exhausts a stream needs the
  generator state machine to model an early-exit edge.

## Difficulty map (where the remaining points are)

Rough guide to what each open cluster costs, to help pick the next slice. "codegen"
means breadth work in `codegen/codegen.c` (+ maybe a fail-closed `jacl_*` entry point);
"runtime" means new C machinery; "infra" means harness / powerbox / scheduler work.

| cluster | size | kind | notes |
|---|---|---|---|
| concurrency hangs | ~69 | runtime/infra | scheduler + fibers must quiesce under interp==jit; biggest single bucket |
| buffers + pointers | ~15 | runtime | needs linear-memory-backed `[Buf N T]` so `addr`/`ptr-deref`/`ptr-offset` read raw bytes |
| typed-collection wrong-output | ~10 | runtime | distinguish typed `[Vec T]` (prints `[a, b]`) from untyped `[vec …]` |
| atom watchers (`watch`) | 5 | runtime | trampoline to call a guest closure from runtime C on reset/swap |
| file I/O (`read/write-file`) | ~6 | infra | grant a Filesystem powerbox cap through the harness |
| generator early `return` | ~4 | codegen | model an early-exit edge in the generator state machine |
| runtime-checked `expect-error` | ~6 | driver | const-fold computed indices so a negative/OOB index rejects at compile time |

The compiler-check oracle already covers the *statically* rejectable `expect-error`
corpus, so most remaining error cases are runtime-checked (they need either const-folding
in the compile path or a harness that scores an expect-error program's *run*, not just its
compile).

## Adding a slice

1. Pick a cluster from the scoreboard (prefer the largest `emit-fail` bucket or a
   category with a low pass/total ratio).
2. If it is a new head, wire it in `codegen/codegen.c` (usually the `BI[]` table for a
   fixed-arity builtin, or a dedicated block for control-flow / sugar forms) and add the
   `jacl_*` entry point in `runtime/` if one is missing.
3. Runtime entry points must **fail closed** — return an error value on a type mismatch
   rather than misread a foreign pointer — so a head can be wired before its full static
   check exists without risking memory unsafety.
4. Rebuild the driver (`gcc` — the frontend's toolchain) and re-run the parity harness
   with `--filter <cluster>` for a fast check, then a full run to confirm no regressions.
