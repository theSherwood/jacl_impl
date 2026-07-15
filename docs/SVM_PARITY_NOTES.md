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

This slice alone moved `err-pass` from 8 → 117.

Note a **structural limit** of this oracle: it only sees *compile-time* rejections. An
`expect-error` program whose error is a *runtime* trap (calling a non-callable, a computed
negative index, a runtime bounds fault) compiles cleanly, so the driver exits 0 and the
case scores `err-no-fail`. Those cases can only be won by (a) const-folding the offending
value so the static compiler rejects it, or (b) a harness that scores an `expect-error`
program's *run*, not just its compile — neither is wired today.

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
- **Overflow-promoting i32 arithmetic.** `+`/`-`/`*` widen an i32 result to i64 **only
  when the op actually overflows** (via `__builtin_{add,sub,mul}_overflow`). Non-overflowing
  arithmetic stays unboxed i32 with zero extra allocation, so GC-stress hot loops don't trip
  the single-threaded interpreter's collector — the livelock that sank an earlier
  *unconditional*-widen attempt. Fixed the wide-i64 truncation across the typed-closure and
  typed-stream clusters (`* n 1000000000` → `6000000000`, not `1705032704`).
- **Unary minus `[- x]`** lowered as `0 - x`, so it inherits the same promotion.
- **Early `return`.** A top-level `return` / bracket `[return]` statement now emits the
  real return and drops the unreachable tail; in a generator this ends the fiber, so a
  `return` before a later `yield` exhausts the stream. `[return V]` inside a generator is
  the old VM's compile error. (Early returns nested inside a loop/if remain unsupported.)
- **Closure-valued expression heads.** `[[vec-get $fns i] x]`, `[[pick-fn] x]` — a command
  head that evaluates to a closure — call through the closure ABI like a `[$f x]` var head,
  guarded so typed collection constructors (`[[Vec T] …]`) keep their own path.
- **`buf-unchecked-get`/`-set`** map to the lenient array get/set.
- **Struct-name type prefixes** (`Point p` params, `def Point p V`) — a Capitalized
  identifier is now recognized as a type token, not a param/binding name.
- **By-value `[Buf N T]` proc params** — copied once in the callee prologue
  (`jacl_arr_copy`, deep over nested buffers), so callee mutations don't escape.

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
  typed vectors from dynamic ones — a *distinct* runtime type. The clean way is a new 5-bit
  tag aliased to the vector representation, but the primary stack-root scanner
  (`__vm_gc_roots`, an opaque svm op) can't be verified to mask an unfamiliar tag byte off a
  candidate root, so a novel tag risks an *unscanned* live root → use-after-free. Under the
  no-memory-corruption rule this is left alone until the root-scan contract is pinned down;
  the ~3 affected cases (`range_*`, `stream_struct_collect`) aren't worth the risk.
- **Nested compound proc params.** `proc apply {[Proc [Point] i32] f, Point p}` — a
  `[Proc …]` param with a *nested* bracketed type (`[Point]`) — miscounts params in the
  proc-param extractor, so the call reads as an arity mismatch. Flattening the nested
  brackets in `extract_param_names` is the fix.

## Difficulty map (where the remaining points are)

Rough guide to what each open cluster costs, to help pick the next slice. "codegen"
means breadth work in `codegen/codegen.c` (+ maybe a fail-closed `jacl_*` entry point);
"runtime" means new C machinery; "infra" means harness / powerbox / scheduler work.

| cluster | size | kind | notes |
|---|---|---|---|
| concurrency hangs | ~72 | runtime/infra | scheduler + fibers must quiesce under interp==jit; biggest single bucket |
| buffers + pointers | ~11 | runtime | needs linear-memory-backed `[Buf N T]` so `addr`/`ptr-deref`/`ptr-offset` read raw bytes; fat-pointer arrow-index + `Ptr<T>(0x…)` print need a pointer runtime type. (By-value buf params + `buf-unchecked-*` are done.) |
| typed-collection print | ~3 | runtime (GC) | distinguish typed `[Vec T]` from `[vec …]`; blocked on the root-scan tag contract (see above) |
| pointer print format | 2 | runtime | `Ptr<i32>(0x…)` needs a first-class pointer type carrying the pointee type + address |
| stack traces | 2 | runtime | capture the call chain + line numbers at error creation |
| atom watchers (`watch`) | 5 | runtime | trampoline to call a guest closure from runtime C on reset/swap |
| file I/O (`read/write-file`) | ~5 | infra | grant a Filesystem powerbox cap through the harness |
| module-global proc capture | ~3 | codegen | a top-level `proc` reading a top-level `def` needs module-global slots (procs are standalone functions today) |
| nested compound proc params | ~2 | codegen | flatten deeper `[Proc [Point] i32]`-style nested brackets (the scalar `Point p` prefix is now handled) |
| runtime-checked `expect-error` | ~6 | driver | const-fold computed indices, or score an expect-error program's *run* |

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
