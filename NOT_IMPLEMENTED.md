# JACL — Not Yet Implemented

A single index of everything **planned, deferred, half-built, or
design-open** in JACL. Each entry is a short status line and a pointer
to the doc that owns the long version.

If you find yourself adding a new "TODO"-class item, add it here *and*
in the owning doc. Keep entries tight: one paragraph max.

Last refreshed: 2026-05-22 (§10 CPS arg-eval bug fixed — SM
compiler's SUSPEND_CALL path now spills/restores the enclosing
operand stack across `OP_AWAIT_SM`, matching the discipline already
used by await/sleep/yield/parallel/race. Regression test:
`test/jacl/suspending_call_inline_args.jacl`. §4 annotation-driven
`[Stream T]` typing landed 2026-05-19; for-loop narrowing + yield-
site inference deferred as a Phase 2 unit. §3b generator-tail check
landed and section removed; see compiler.c
`find_disallowed_generator_tail`).

---

## 1. User-facing language features — not yet implemented

Lifted from `SYNTAX.md` § "Not yet implemented" (the canonical table).
Status snapshot:

| Feature | Complexity | Blocking deps | Notes |
|---|---|---|---|
| **`match` / case** | Large | None | Lexed; `HEAD_MATCH` recognized as special form (`compiler.c:2527`); typer rules deferred; no compile/runtime path. May ship as a macro instead of a compiler-path special form — see `DESIGN_CRITIQUE.md` §3.3. |
| **Callable values** (maps/atoms in `[]` head) | Medium | None | `[$colors red]`, `[$config port]`. +50–100 LOC projected. |
| **`$env`** (atom of map, `with-env`, `$home`/`$pwd`/`$pid`) | Medium | Callable values | Bidirectional OS sync via watchers. Watcher dep met by `watch`/`unwatch` (2026-05-19, `ATOM_WATCH_DESIGN.md`). |
| **Aliases** (`alias ll { !ls -la }`) | Small | None | Compile-time syntactic rewrite. Scoping (file vs session) unresolved — `SYNTAX.md` Q21. |
| **Globbing** (`glob`) | Medium | None | Reads `$ctx.pwd`; returns a stream. Pattern engine + brace expansion. |
| **`par-each`** | Medium | None | Concurrent stream processing. Hard part is backpressure (open question in `DESIGN.md`). |
| **Regular expressions** | Medium | Lib exists (see §2) | Literal syntax (`/regex/`) + capture-group bindings + VM bridge. |
| **Bignum / numeric tower** | Medium | Lib exists (see §2) | `JACL_TAG_BIGNUM` exists; no VM arithmetic dispatch. Plan is bigint+bigfloat with implicit promotion in `dyn`. Rationals dropped. |

---

## 2. Libraries that exist but aren't wired

Self-contained code sitting in `lib/` waiting for VM integration.
From `DESIGN.md` "Project Layout" and `DESIGN_CRITIQUE.md` §3.2, §3.4.

- **`lib/regex/`** (~2,500 LOC) — Thompson NFA. Needs literal syntax,
  VM dispatch, capture-group binding. Projected wiring add: +300–600
  LOC. (`SYNTAX.md` Q14 — literal syntax TBD.)
- **`lib/bignum/`** (~1,860 LOC after rational deletion) — bigint +
  bigfloat. Planned. Will be both `dyn` promotion targets
  (Python-style overflow promotion) and typed slots (`bigint x`,
  `bigfloat y`). Projected wiring add: +500–1,000 LOC. `rational.h`
  and `test_rational.c` deleted 2026-05-16 — rationals were cut from
  the integration plan per `DESIGN_CRITIQUE.md` §3.2 / §4.4.
- **`lib/segment_array/`** (~320 LOC) — segmented growable array.
  Backs JACL's **`arr` / `[Arr T]`** type: a mutable ref type that
  parallels `[Vec T]`'s shape (dyn by default with `[arr ...]` /
  `[$arr]`, explicitly typed via `[Arr T]` and `[[Arr T] ...]`),
  complementing the existing immutable `[Vec T]` (RRB) and
  fixed-size `[Buf N T]`. Needs a GC object kind + tracing for
  ref-element segments, write-barrier integration on mutating ops,
  typer rules mirroring `[Vec T]`'s element-type machinery, and a
  builtin set (push / pop / get / set / len / iter). Projected
  wiring add: not yet scoped.

---

## 3. Compiler/VM infrastructure present but unused

Scaffolding that exists in the runtime but no compiler path emits it.

- **`OP_TAIL_CALL`** — handler at `vm.c:2901–2935`; compiler never
  emits it. Compiler-side tail-position analysis pass not written.
  The short-term mitigation (4× bump of `VM_STACK_MAX` / `VM_FRAMES_MAX`
  to 1024 / 256) landed 2026-05-16, so practical recursion depths are
  now comfortable; the durable fix is still the compiler pass. Full
  discussion: `DESIGN_CRITIQUE.md` §3.5.
- **`JACL_FLAG_TAINTED` / `JACL_FLAG_SECRET`** — full bit-flag API
  (`value.c:45-46, 392-400`) preserved across GC (`JACL_FLAGS_MASK`,
  `gc.c:723, 780, 836`). Zero producers, zero consumers. Open design
  question: runtime-only flag vs typed property (`tainted str` as a
  distinct type). Projected wiring: +200–400 (runtime) or +500–800
  (typed). Full discussion: `DESIGN_CRITIQUE.md` §3.4.
- **`match` reservations** — `TOKEN_MATCH`, `HEAD_MATCH`, special-form
  recognition at `compiler.c:2527`. Reserves the word; no
  implementation. See §1 above.
- **Pragma block (`#{...}`)** — lexer stub recognizes it (~30 LOC).
  No consumer.

---

## 4. Type system — deferred items

From `TYPE_SYSTEM.md` § "Deferred items". No concrete workload
pulling on them; revisit when one shows up.

- **Closure literal call signatures.** Anonymous closures get
  `TYPE_CLOSURE` but the typer doesn't carry their param/return
  signature, so calls to a captured anon closure stay `dyn`. +150–300
  in the typer.
- **Imported struct exports field-typing.** Imported struct types go
  through the CapitalCase placeholder pre-pass with empty fields.
  Field access on imported structs stays `dyn` at the typer level
  (codegen still works via the compiler's shared struct registry).
  +200–400 LOC; needs cross-module struct-idx alignment.
- **`[swap $box fn]` struct-element narrowing.** Scalar-element swap
  narrows; struct-element swap stays `dyn`. Needs `OP_SWAP_INLINE`
  parallel to `OP_DEREF_INLINE`. +50–100 LOC.
- **`match` arm-walk.** Typer rules for `match` patterns. Dead code
  until the feature itself lands.
- **Parameterized stream element type — for-loop narrowing & yield
  inference (Phase 2).** Annotation-driven typing landed 2026-05-19
  (`compiler__stream_type_expr`, `typer__stream_type`,
  `compiler.c:7891` generator-return + parser
  `parser__parse_proc_form`). `proc [Stream T] gen {} { [yield X] }`
  now parses, the typer carries the element idx via
  `proc->return_struct_idx`, and yield arg-mismatch fires for
  typed→typed and dyn→typed cases (typer.c yield-element check).
  Range / `..=` / `lines` builtins stamp i64 / str on
  `inferred_struct_idx`. Two pieces of the original §4 stream item
  remain deferred:

  1. **For-loop binding narrowing.** `for $it in [gen]` where the
     stream is `[Stream i64]` would naively narrow `$it` to i64, but
     the stream stores tagged `JaclVal`s (yield emits `jacl_i32` /
     `jacl_str` / ...) while typed-i64 locals expect the unboxed
     wide representation used by `OP_CONST_I64` / typed-vec slots.
     Narrowing without an unboxing step on `OP_STREAM_NEXT` produces
     `144115188075855873` (the NaN-boxing tag bits leaking out) where
     the user expects `1`. Fix needs a per-element unbox op on stream
     pull, parallel to typed-vec's inline-load path. Estimated
     +150-250 LOC. Same wide-cell concern blocks yielding an
     already-typed-i64 value (`def i64 x 42; yield $x` produces nil
     because `OP_CONST_I64`-style values can't survive
     `OP_YIELD_SM`'s `vm__pop` — currently masked by the typer's
     literal-flex exemption, which keeps literals at their default
     scalar tag).
  2. **Yield-site inference.** Today an unannotated generator stays
     `[Stream dyn]`; the typer doesn't walk yield expressions to
     unify their types. Annotation is the only narrowing path. Adding
     inference needs a unification policy for mixed yields
     (error vs widen-to-dyn) and treatment of conditional yields,
     which is real design work — see the parent design conversation
     before implementing.

  Cross-module element-type alignment for streams exported from
  another file also stays deferred (mirrors the imported-struct
  field-typing carve-out in this same section).

---

## 5. Architecture-level open design questions

From `DESIGN.md` § "Open Questions". Not implementation TODOs —
decisions that need a call before code can be written.

- **Type system:** Numeric tower / bignum integration (see §2).
  Variant types.
- **Concurrency:** Tainted/secret flag interaction with cross-thread
  communication. Channel primitives (may not be needed given atoms +
  futures). Backpressure / bounded concurrency for `par-each`.
  Job/Future cancel semantics — see `SYNTAX.md` Q19 below.
- **GC:** Scheduling heuristics (current: adaptive allocation-byte
  threshold; tuning is open).
- **Errors:** Error chaining / wrapping. Whether to mark procs as
  error-capable explicitly or trust propagation. Whether stack
  overflow should be catchable via `try`/`catch` like other errors —
  see §10 for the current inconsistency.
- **Modules:** Search path / resolution rules beyond
  relative-to-importer. Versioning / dependency management. Visibility
  beyond underscore convention (`pub`/`priv` keywords if encapsulation
  needs grow).
- **Macros:** Whether all builtins should be rewritten as macros where
  possible.

---

## 6. Syntax-level open design questions

From `SYNTAX.md` § "Needs design work". Only unresolved items
listed (resolved ones are in the source doc).

- **Q2 / Q16. Boolean/logical operators.** `and`/`or`/`not`:
  word-form, symbol-form, or both? Precedence in `()` infix mode?
  Connects to operator overloading.
- **Q14. Regex literal syntax.** See §1 / §2.
- **Q15. Operator overloading.** Desired but not designed. Operators
  should be user-definable.
- **Q17. Standard library surface.** What's builtin vs module?
- **Q19. `cancel` semantics.** Current impl: `[cancel $job]` →
  `[signal $job SIGTERM]`. No grace-then-SIGKILL fallback. What
  `cancel` should mean on a plain JACL Future (cooperative
  cancellation) is open.
- **Q21. Alias scoping.** File-scoped like `def`? Importable from
  modules? Or session/config-level like `.bashrc`?
- **Q24. `$ctx` vs `$env` relationship.** Does `$ctx.env` subsume
  `$env`? Which does `!cmd` inherit?

---

## 7. Struct value-type — open

From `STRUCT_DESIGN.md` § "Open Questions".

- **Fixed-size arrays in structs** (e.g. `[u8; 16]`). Layout system
  can accommodate; not yet wired. Useful for byte buffers without
  strings.
- **Wide cells / globals.** Mut bindings and globals store `JaclVal`
  slots; structs must go through `[box]`. A wide-cell architecture
  could allow inline mut struct bindings; not pursued.

---

## 8. Generator state machine — future work

From `GENERATOR_STATE_MACHINE.md` § "Future work".

What already exists: a **layout-time** liveness pass
(`sm__optimize_state_layout`, `compiler.c:2249-2325`) that filters out
locals whose write-and-last-read sit in the same inter-suspension
segment — those become normal stack locals instead of SM state
fields. Two restrictions on this pass leave real work on the table:

- **Yield-only.** The pass bails out the moment any `await` /
  `parallel` / `race` appears (`compiler.c:2256-2261`) because their
  diamond control flow (inline-already-resolved vs real-suspend-then-
  resume) breaks the linear segment numbering the current algorithm
  assumes. So async procs carry every local in the SM, whether it
  survives a suspension or not. Lifting this needs a CFG-based
  liveness pass rather than the existing linear segment walk.
- **Per-yield-point clearing, deferred.** A field that crosses *some*
  yield (so layout keeps it) can still be dead after a *specific*
  yield in the middle of the body — e.g., a generator that loads
  buffer A, yields N times from A, loads B, yields M times from B.
  Today A's state field stays live for the whole stream's lifetime;
  GC can't reclaim what A pointed to until the SM itself dies. A
  per-yield-point pass would clear dead fields at each yield site,
  shrinking retained memory and reducing major-GC tracing work.
  Needs a new `OP_CLEAR_STATE_FIELD` opcode (`bytecode.c:187-198`
  has get/set variants, no clear).

Best done together: a CFG-based liveness pass replaces the existing
linear one and emits per-yield-point clears in the same lowering,
killing both restrictions at once. Estimated +300-600 LOC. Not
measured as a bottleneck; no workload today retains large heap
values across phase-distinct SM lifetimes long enough to matter.

---

## 8b. Sleep timers — known limits

Shipped 2026-05-18 (initially as a dedicated OS thread per `Runtime`);
the dedicated thread was eliminated 2026-05-18 in favor of polling from
the worker idle loop. `runtime__poll_timers` runs once per worker idle
iteration, splicing expired entries off the deadline-sorted list and
dispatching SM resumptions via the existing `schedule_sm_resumption`
path. Firing latency is bounded by the worker idle-park (currently 1ms
via `COND_WAIT_FOR_MS`; see `AUDIT.md` §11). Remaining corners:

- **O(n) insertion.** Timer entries are kept in a sorted singly-linked
  list. Fine for small concurrent-sleep counts; replace with a binary
  heap if a workload pushes into the hundreds.
- **No cancellation.** `[timeout n body]`'s losing branch keeps running
  after the race settles — its eventual result is discarded but it
  still occupies a worker. Connected to `SYNTAX.md` Q19 (`cancel`
  semantics, see §6).
- **Wake granularity.** Sub-millisecond deadlines round up to the
  worker idle-park granularity (1ms today).
- **Coupling to audit §11.** Firing latency now equals the worker idle-
  park timeout. When §11 is tackled (lengthen or eliminate the 1ms
  tick), the new park timeout MUST be capped by
  `min(idle_park, timer_head->deadline_ns - now)` or sleeps will
  silently take however long the next non-timer wakeup happens to
  arrive. Flagged inline in `runtime.c` at the timer block.
- **Emscripten / single-threaded builds.** Single-threaded mode has no
  runtime, so `OP_SLEEP_BLOCK` (toplevel `nanosleep`) is the only
  path. If a single-threaded runtime ever ships, sleep wakeups would
  fire on whatever single-worker idle path it uses — no longer a
  silent no-op since there's no dedicated thread to be a no-op.

---

## 9. GC / concurrency — deferred (already indexed)

`AUDIT.md` is the canonical home for runtime open items. Pointers only:

- **§11. Worker idle CV-park timeout.** Workers park with a 1 ms
  timeout for wake paths the signaling code can't reach. 81% of CPU
  on `spawn_chain`. Real fix needs serial-vs-parallel workload
  distinction at the submit site. Attempted 2026-05-15 and reverted
  (5× regression on `spawn_chain`). See `AUDIT.md` §11 for the lesson
  and candidate strategies. **Extra constraint as of 2026-05-18:**
  the timer thread was deleted in favor of `runtime__poll_timers`
  running on the worker idle path, so the 1ms tick is now load-
  bearing for `sleep` latency. Any fix must compute its park timeout
  as `min(idle_park, timer_head->deadline_ns - now)` — see §8b.
- **§12. `JaclFuture` waiters list spinlock.** Not observed under
  contention. Consider parking after N spins if it ever surfaces.
- **§17. Inconsistent thread-local convention.** Scattered globals
  (`gc__current_heap`, `gc__thread_epoch`, etc.) instead of a threaded
  `JaclCtx*`. Larger refactor; not a sandbox prerequisite.
- **Performance smells (deferred):** see `AUDIT.md` § "Performance
  smells" — HAMT/RRB TLS-heap-read, steal-from-every-worker,
  `gc__find_fit_in_block` residual, `gc_sweep_concurrent` rebuild,
  sweep/mark main-body dedup. None concentrated enough to chase
  under current workloads.  (`jacl_is_heap_type` tag chain was
  resolved 2026-05-22 — see commit `405932f`.)
- **§9 read-side operand-stack rooting hole.** Theoretical UAF;
  no concrete bug observed. Convention (CPS-only concurrency)
  prevents it today. See `AUDIT.md` § "Known theoretical hole".

---

## 10. Known limitations (will-not-fix-soon)

From `DESIGN.md` § "Known Limitations".

- **Escape analysis: boxes in collections are not tracked.** The
  escape pass detects direct mutable captures and transitive captures
  through closures, but does *not* track box/cell refs stored in
  vectors/maps. A box stashed into a vector that's then passed to
  `spawn` will not pin the task. Fixing requires taint tracking
  through collection ops (`vec-push`, `map-set`, ...).
- **"ctx-pure" pruning is future work.** The compiler does not mark
  procs as ctx-pure to skip the fork. `with-ctx` always pays the fork
  cost when entered. Future analysis pass.
- **Stack overflow is not catchable at toplevel.** JACL's error model
  is "errors are values that propagate" (`try`/`catch` catches anything
  with the error flag), but stack overflow bypasses it. Two different
  behaviors today for the same condition:
  - *Inside `spawn`/`parallel`/`race`:* overflow becomes a regular
    error value on the future; `await` surfaces it; downstream
    `try`/`catch` catches it. This is the §16 path
    (`runtime__make_completion_error`) and matches the design intent.
  - *At toplevel / inside `try`:* overflow returns `VM_RUNTIME_ERROR`
    (or `VM_STACK_OVERFLOW`) directly from `vm__run` to the C caller,
    bypassing the in-VM error-value machinery. Verified empirically:
    `try { rec 0 } e { ... }` with infinite-recursion `rec` does not
    catch — the program exits with the message and any post-`try`
    code never fires.

  Making overflow uniformly catchable requires reserving headroom (a
  few stack slots + frames) so the error-value push and unwind can
  run, plus growing every overflow check site to route through the
  in-VM error machinery instead of returning to C. The hard-fatal
  floor stays as defense-in-depth (overflow-during-`catch`-body
  itself can't be catchable — that's where you have to give up). The
  smaller-effort alternative is to declare overflow fatal-by-design
  (machine-limit, not user-recoverable) and document that `try`
  won't catch it — bash-flavored honesty, but it forecloses the
  "retry with a different strategy" pattern.

  Tests covering current behavior: `test_vm.c:227` (operand stack),
  `test_vm.c:1633` (error_line), `test_vm.c:2190` (call frames),
  `test_compiler.c:2760` (compiler-emitted recursion), and the
  end-to-end fixtures `test/jacl/overflow_call_depth.jacl` (toplevel)
  + `test/jacl/cps_spawn_deep_recursion.jacl` (across a spawn
  boundary). No test today asserts `try`/`catch` semantics for
  overflow — add one when the design call is made.

- **`ctx` field declarations don't survive across `jacl_eval` calls.**
  `typer__register_ctx_struct` (`src/typer.c:1979`) rebuilds the ctx
  struct each compile from the current source's `AST_CTX_DECL` nodes.
  A `ctx mut T name = v` in one `jacl_eval`, then `$ctx->name` in a
  second, fails the typer with "no field 'name' on ctx". The runtime
  side (lazy `ctx__init_vm` in `embed.c`) is fixed — the persistent
  ctx struct and pool both survive — so the field is *there*, just
  invisible to the typer on subsequent compiles. Single-script
  invocations (playground, `jacl_eval` of a whole file) are
  unaffected; the gap shows up only when an embedder splits a ctx
  declaration and its consumer into separate `jacl_eval` calls. Fix
  needs the typer's ctx pre-pass to read from the persistent struct
  registry too, not just the current AST. Covering test (currently
  exercises the single-call path):
  `test/test_e2e_embed_basics.c::test_e2e_ctx_declaration`.

---

## 11. Build-flag / scope cuts considered, not committed

From `DESIGN_CRITIQUE.md` §3.6 and §9.5. These are *scope-reduction*
options, not features to add — listed here so the design space stays
visible.

- **`JACL_THREADED=0`** — ~3,000–4,000 LOC out (workers, chase_lev,
  concurrent GC paths, SATB, epoch). Loses CPU parallelism; single-
  threaded coroutines for spawn/await/parallel/race remain. Largest
  single binary-size lever.
- **Drop generational GC (single-threaded tier-2 cut).** ~350–500
  LOC out. Removes working code, not stubs.
- **Drop sum_tree rope tier.** ~5,000 lib + ~150 bridge. Inline +
  interned string tiers remain.
- **Drop typed collections.** ~1,300 LOC. Falls back to dyn
  collections.
- **Drop `$ctx`.** ~600 + cascading simplifications.
- **Drop FFI / embedding.** ~1,340 (embed.c) + ~800 (Ptr/extern/
  trampolines).
- **Consolidate dual struct definitions** (`runtime.c`/`gc.c` vs
  `jacl.h`). Doesn't shed size; removes drift-bug class. Options:
  thread via `jacl.h`, route tests through unity build, or opaque
  pointers + accessors. **Mitigated 2026-05-18**: every field of
  `Runtime` + `WorkerThread` is now offset-checked via the X-macro
  list in `src/struct_drift_fields.h` (driving both `embed.c` getters
  and `test_struct_sizes.c` checks); a missing field in either
  definition fails to compile, an offset mismatch fails the test at
  runtime. The duplication itself remains.
- **`vm.c` split.** 12,200 lines in one TU. Splitting by category
  (keeping the dispatch table central) could halve it. No runtime
  cost change; cognitive-load only.

---

## How to use this file

- **Adding an item:** add it both here (one paragraph) and in the
  owning doc (full discussion). This file is an index, not the
  source of truth.
- **Closing an item:** remove from here when the feature ships or
  the design call is made; update the owning doc per its own rules.
- **Cross-cutting changes** (GC, concurrency, synchronization) belong
  in `AUDIT.md` / `SYNCHRONIZATION.md` / `GC_CONCURRENCY_DESIGN.md`
  rather than here.
