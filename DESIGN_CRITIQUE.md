# JACL Design Critique

A critical, high-level read of JACL as a language and as a program, after
a pass through `DESIGN.md`, `SYNTAX.md`, `TYPE_SYSTEM.md`, `STRUCT_DESIGN.md`,
`AUDIT.md`, `GENERATOR_STATE_MACHINE.md`, `prelude.jacl`, sample fixtures
under `test/jacl/`, and the runtime/VM/typer source. Written 2026-05-15.

This is meant as a discussion artifact — each section is intended to
stand on its own so it can be picked up out of order.

## 1. The central bet

JACL targets **glue and scripting** — shell-style command code with
real C interop. The Lisp ancestry isn't a goal in itself; it's a
substrate choice, taken to escape the "everything is a string" model
Tcl coheres around but never fully escapes the hack-feeling of. The
parser-level lever is the **three-mode delimiter system** — `[]`
juxtaposition, `{}` command mode, `()` infix. Two of those modes are
load-bearing (`[]` and `{}`); `()` is intentionally a narrow
sublanguage modeled on Tcl's `expr`, for the arithmetic and predicates
that glue code inevitably needs but doesn't dominate.

Framed that way, the bet is **stronger than a "three modes to be
three things" reading would suggest**. The `[]`/`{}` split is the
real workhorse — one rule lets `{}` carry params, struct fields,
blocks, and `with-ctx` field maps, while `[]` is unambiguously a
call. `()` doesn't need to compete with C's expression grammar; it
needs to be ergonomic for the cases shell code reaches for it.
Operators have one meaning across all modes (no mode-dependent
semantics), so the cognitive cost of mode-switching is lower than it
first looks.

What survives as real criticism, after that reframing, is narrower
than my first draft suggested:

- **Parser-shape ambiguity leaks into macros.** The binding-operator
  macros (`=`, `:`, `::`) in `prelude.jacl:16–89` each spend ~15
  lines disambiguating shapes the parser hands them — `x = 3`
  arrives as a zero-arg command `[x]`, `[a b c] = $v` as a multi-arg
  command (destructuring), and other shapes pass through. This is
  *not* a mode problem; it's a contract problem between parser and
  macro layer. As written, every macro that has to care about its
  caller's shape pays the same tax.

  Two avenues worth considering, neither committed to:

  - **Parser-side normalization.** Make the parser emit a single
    canonical shape for the binding-operator LHS — e.g. always a
    bare identifier node or a destructuring-pattern node, never
    "zero-arg command that the macro has to unwrap." The macro then
    splices a known shape and the 15 lines collapse to 2. Cost:
    moves complexity from many user-land macros into one privileged
    parser pass, and the parser has to know what's a "binding
    operator LHS" vs. an arbitrary expression. May not generalize
    cleanly to user-defined operators.
  - **Lighter macro-side shape-matching primitive.** Provide
    something like `(syntax-case)` or a `match-shape` so each macro
    declares patterns to match and bindings to extract, rather than
    hand-walking trees with `syntax-kind` / `syntax-head` /
    `syntax-args`. Keeps the parser dumb and the macro layer fully
    programmable, but pushes complexity onto every macro author.
    More Schemey, less Tcl-y.

  These aren't mutually exclusive — the parser could normalize the
  shapes the prelude cares about *and* the macro layer could grow a
  better shape-matching primitive for user macros.

- **The substitution-model count went up, not down.** Tcl gets
  coherence from one substitution model applied uniformly during
  scan: `$var`, `[cmd]`, `\n`. JACL has more user-facing forms —
  `$var`, `$[cmd]`, `$(expr)`, bare-words-as-strings, quoted strings
  with the above three substitutions nested, macro expansion at
  compile time, `interpret` at runtime, `[to T $val]` for typed
  barrier crossings. The Lisp substrate buys a cleaner *internal*
  model (everything is an AST node, not a string being substituted
  again and again), but the user has more forms to learn, not fewer.
  Whether that trade is worth it depends on whether the cleaner
  internal model pays out elsewhere (it largely does — see the macro
  system, the typer's single-authority invariant, the absence of
  Tcl-style `uplevel`/`upvar` hacks). But the surface-level
  reduction Tcl promises and JACL implicitly competes with hasn't
  happened.

  Flagged as a candidate for future revisit. Not clear a more
  ergonomic design exists — the three string-internal forms
  (`$var`, `$[...]`, `$(...)`) each pull their weight for distinct
  cases, and collapsing them to one universal escape (e.g. making
  `$(...)` the only form) would cost paren-noise on the most common
  case (`$var` reads). The bigger question is whether the
  user-facing substitution surface can be *justified* as
  irreducible, or whether some forms are accidental complexity
  picked up along the way. Worth its own deeper pass.

- **No-precedence in `()` is a deliberate Smalltalk-style trade**, not
  a wart. Worth naming as a taste — users coming from C will be
  surprised once, then internalize it — but it shouldn't have been
  in the "weakness" column.

The first draft of this section overstated the cost of three modes
and conflated two separate complaints (mode-dependent operator
semantics, which is rejected by design, with parser-shape ambiguity,
which is real). Section 1 originally read as "the central bet is
half-right"; the corrected read is **the bet is largely right for
the glue/scripting audience it actually targets**, with the residual
costs concentrated in the macro/parser contract and the
substitution-model surface area, not in the three-mode parser
itself.

## 2. Strengths

### 2.1 Direct-style concurrency is the headline

CPS transform for `await`/`parallel`/`race` plus a state-machine
transform for generators, all sitting on an NxM work-stealing scheduler
with epoch-based non-moving GC. The combination is rare; each piece is
not. Two honest caveats to walk back the originally-uncritical praise:

- **"No function coloring" is a syntax claim, not an architectural
  one.** There's an implicit compile-time `SuspensionAnalysis` pass
  (`compiler.c:compiler__analyze_suspensions`) that classifies procs as
  suspension-capable or not and compiles them differently — different
  state allocation, different return path. The user-visible
  `async fn` keyword is gone, which is genuine value, but coloring
  exists; it just lives in the compiler instead of the type system.
  Users still can't always reason locally about call cost without
  knowing whether the callee suspends.
- **The audit was triggered by real bugs, not just paranoia.**
  Chase-Lev contract violation, grey-buffer realloc race, SATB barrier
  UAF, multiple TSAN-surfaced races. The right framing is "the design
  survived a heavy audit and is now solid," not "the design is solid
  as evidenced by the audit." The former is true; the latter is the
  kind of compliment that disappears if you read the audit history.

Also missing from the original praise: the model is excellent for
**task parallelism** (spawn N tasks, collect results), weaker for
**data parallelism** (split a large workload across workers — `par-each`
isn't shipped yet), and offers nothing for **CSP-style channels** (the
docs hedge "may not be needed given atoms + futures," which is a
deferral, not an answer). Glue/scripting may genuinely not need
channels, but data parallelism for streaming workloads is on the wrong
side of the implementation line right now.

### 2.2 Implementation discipline is well above the median

`SYNCHRONIZATION.md` as a per-field W/R/Sync/Order/Invariant reference
is rare even in production runtimes. `AUDIT_HISTORY.md` reads like a
postmortem archive most teams never write. The chaos test inventory +
TSAN baselines + soak harness is the kind of thing that distinguishes
"works on my benchmarks" from "shippable." For a third party evaluating
JACL as a library to embed, that documentation alone is a substantial
trust signal.

**Caveat — this is a project property, not a language property.** The
discipline lives in the current author's working style and in the
docs. If JACL gains contributors or changes hands, the audit pattern,
the SYNCHRONIZATION.md update habit, and the chaos-test-first workflow
have to be re-built from scratch — they don't transfer by virtue of
the language existing. A potential adopter should value this *today*
and separately ask whether the structure will outlive the original
author's involvement. (The docs are written well enough that the
answer plausibly is yes, but it's a real question, not a guarantee.)

### 2.3 Errors-as-values with pipe short-circuit

A clean story for the happy path. NaN-style flag-bit propagation in
every value avoids the Go-tax of explicit `err != nil`, and shell
pipelines compose naturally because failure stops the chain by
default. `catch` as a pipe stage and `try`/`catch` as a block form
cover both code and pipeline contexts.

Three honest costs the original framing glossed over:

- **`parallel` returns a vector with mixed errors and values.** The
  caller has to manually filter (`$results | filter [\ not [error? $it]]`)
  before doing anything with the results. Composition is preserved
  but the ergonomics are worse than the happy-path examples in
  `SYNTAX.md` suggest. This is the price of letting all branches run
  to completion; a "fail-fast on first error" mode would have a
  different ergonomics trade.
- **Constant low-grade runtime tax.** Every value carries an error
  flag bit, which is checked at many operations. Individually
  negligible; collectively it shows up in profiles.
- **Silent-swallow risk.** A forgotten error check leaks the error
  to wherever the value ends up, where it'll propagate again or get
  stringified into a print site or written to a file. JACL chose
  composition over enforcement; Rust-style `Result<T, E>` makes the
  opposite trade and forces handling at the type system. The JACL
  choice is defensible for a scripting language, but it's a *trade*,
  not a free win.

### 2.4 Implicit typed `$ctx`

A real answer to a problem shell scripts perennially get wrong
(threading `pwd`/`env` everywhere). Typed field declarations, copy-on-
write forking at concurrency boundaries, and spawn-snapshot semantics
are genuine wins, and the compile-time field-existence + field-type
enforcement contains the worst dynamic-scoping pitfalls.

Three places where the original framing was too generous, including
one where I was repeating doc claims that the code doesn't back up:

- **`fork-per-proc-call` is doc fiction.** `SYNTAX.md` says "Each
  proc call gets a fork of the caller's `$ctx`," but the
  implementation only calls `ctx_fork` at `spawn` (vm.c:5129),
  `parallel` (5319), `race` (5505), and `with-ctx` (10227). Regular
  `OP_CALL` (vm.c:2767) shares `vm->ctx` with the caller. That's
  actually a sensible implementation choice — per-call forking would
  be a huge tax — but it means dynamic-scoping pitfalls are *more*
  present than the doc suggests, since a callee's `set $ctx.field`
  *will* leak back to the caller. The query-builder example in
  `SYNTAX.md` works precisely because the doc is wrong; if every
  proc call forked, the sibling-mutation pattern wouldn't propagate.
  `SYNTAX.md` should be corrected to "fork happens at concurrency
  boundaries and explicit `with-ctx`, not per-call."
- **"ctx-pure" pruning is future work, not current.** The doc
  describes the compiler "marking procs as ctx-pure to skip the
  fork entirely." Grep finds no such pass — no `ctx_pure`, no
  `CTX_PURE`, no static analysis annotating procs with this
  property. The "zero overhead on most calls" claim in the doc
  rests on this nonexistent pass; in reality, calls don't fork
  anyway, so the optimization is moot for proc calls but real for
  `with-ctx` (currently always pays the fork cost when entered).
- **`$ctx.env` vs `$env` is "TBD"** in the built-in fields table.
  That's a not-fully-baked design corner, not a finished strength.
  Which one does `!cmd` inherit from? What happens when you
  `with-ctx {env ...}` *and* `swap $env`? Open.

The reframed strength: `$ctx` is a good design *for the scope where
forking is explicit* (`with-ctx`, `spawn`, `parallel`, `race`). The
implicit-dynamic-scoping property within a proc-call chain is less
contained than the doc claims, and users writing
mutate-`$ctx`-in-a-proc patterns need to know that mutations propagate
back to the caller's chain.

### 2.5 C-ABI struct layout with no value header

The most under-appreciated piece. JACL structs can be passed to C
without translation, which collapses a whole class of FFI friction. For
the "embedded scripting in C app" use case, this is a real
differentiator vs. Lua or Wren.

One caveat worth naming: the win is **asymmetric**. Stack-passed
structs and inline field-access patterns get the full benefit (no
GCHeader, no HeapRecord, no registry walk on mark). Structs stored in
collections, captured by closures, or held in `mut`-bound globals still
pay the `HeapRecord` indirection per STRUCT_DESIGN.md. The
"Auto-heap-allocation when a struct value crosses a typed boundary"
elimination is real for the stack-only case and approximate for the
collection case. Worth knowing when designing data layout in user
code: prefer struct-by-value-on-stack, fall back to boxing only when
you need to share.

## 3. Weaknesses

### 3.1 The surface-language story is in flight

The runtime — concurrency, GC, FFI, embedding — is further along than
the shell-side surface features that distinguish a glue/scripting
language. Still unimplemented per the `SYNTAX.md` status table:
`par-each`, `timeout`, `$env`, `watch`, `glob`, `read-file` /
`write-file` / `append-file`, regex. None of these are corner cases
for the stated audience; all are scheduled work.

This is a status observation, not a critique of the design. Worth
flagging because anyone reading the pitch and the runtime story
might over-estimate how close the surface language is to matching
them.

**The ordering is the unusual part, and it's deliberate.** Most new
languages ship a surface and then harden the runtime, often
discovering soundness problems years in. JACL went the other way:
the hardest engineering — non-moving epoch GC, NxM work-stealing,
direct-style CPS+SM concurrency, the May 2026 audit campaign — is
the work that's done, because the author had concerns about the
soundness of the model and chose to prove it out before building
surface area on top. The surface features that remain are mostly
straightforward builtins composing with primitives that already
exist (`par-each` on top of `spawn` + streams, `glob` reading
`$ctx.pwd`, `read-file` returning a stream). That's the easy half
of the work, and now that the soundness questions are mostly
settled, the gaps can be plugged. The current state — runtime
ahead of surface — is "right order, partway through," not "easy
parts forgotten."

### 3.2 The numeric story is unfinished

`JACL_TAG_BIGNUM` exists in the value layout (`value.c:65`) and
`lib/bignum/` is 2,100 LOC of self-contained bigint/bigfloat/rational
code. The VM has no arithmetic dispatch for the tag. An earlier draft
of this critique called the current state "the worst of both worlds";
that overstated it — the tag reservation is one packed bit, the lib
doesn't drag on the main runtime, and the wiring is the work the
schedule reflects.

**The plan is bigint + bigfloat, not the full tower.** Arbitrary-
precision integers and floats will be available both as automatic
promotion targets in the dynamic type (so `dyn` integer arithmetic
that overflows `i64` promotes rather than wrapping or trapping —
Python-style semantics) and as typed slots (`bigint x = ...`,
`bigfloat y = ...`) for users who want explicit precision. Rationals
get cut from the integration plan even though `lib/bignum/rational.h`
exists — the lib stays self-contained, the wiring won't include it.

What's left as a real concern: until the wiring lands, scripts that
silently assume Python-style integer semantics will get the
typed-`i64` failure mode (overflow / trap) on inputs they didn't
anticipate. The places this surfaces in glue/scripting are narrower
than I first claimed — file sizes and network byte counts fit
comfortably in `i64`/`u64` on every modern system — but real:
cryptographic glue (keys, signatures, large hashes as integers),
JSON interop where the source has unbounded numbers, and the
default-`dyn` arithmetic users assume Just Works. "Unfinished
rather than wasteful," and the cap on which scripts JACL can host
narrows once promotion lands.

### 3.3 `match` may not ship at all — and that's defensible

`match` is lexed, `HEAD_MATCH` is recognized as a special form
(`compiler.c:2527`), the typer-side rules are deferred, the runtime
compile path doesn't exist. The earlier critique called this
"load-bearing" and ranked it "highest-leverage single feature." Both
overclaimed. Bash and Tcl ship without pattern matching; the
glue/scripting audience can use `if`/`elif`/`error?` ladders for the
shape-dispatch cases that pattern matching would otherwise clean up.
The `SYNTAX.md` sketch (literals, bindings, type patterns, guards,
pipe composition) is ambitious; the cost is `Large`; the benefit is
ergonomic improvement on existing patterns rather than unlocking new
ones. "May not be worth the cost" is a reasonable read.

**If `match` does ship, the interesting design path is implementing
it as a macro rather than a special form.** Most languages bake match
into the compiler because pattern compilation (decision tree
generation, exhaustiveness analysis, binding scope) is non-trivial.
Doing it as a user-space macro would require the macro system to
support real compile-time computation — building decision trees,
emitting binding sites with correct scope, attributing pattern errors
back to source locations through generated AST. JACL's macro system
runs bodies on a real VM at expansion time
(`jacl_ctx_run_closure`-driven, per the macro-eval-rewrite PRD), so
the machinery is plausibly there. If `match`-as-macro lands cleanly,
it's strong evidence the macro system is powerful enough for serious
DSL work; if it doesn't, it surfaces the limits.

Two friction points to expect on the macro path:

- **Typer integration.** The `TYPE_SYSTEM.md` deferred "match arm-walk"
  item assumes the typer needs special rules for pattern binding
  scope. A macro can emit destructure + type-check code that the
  existing typer already handles, but only if the macro is careful
  enough to produce a shape the typer recognizes. The typer doesn't
  natively know "this `def` is a pattern binding scoped to this arm."
- **Error attribution.** A pattern-compile error inside a macro is
  harder to point at the source location than an error in a
  compiler-path handler. JACL's syntax objects already carry source
  position, but the macro has to thread it through every generated
  node — easy to drop on a wrong-shape pattern.

So: `match` is the right feature to *evaluate* the macro system
against, even if it doesn't make the cut into the language. Worth
keeping in scope as a litmus test even while it's not committed as a
shipping feature.

### 3.4 Half-built features — every one is on a planned path

Originally framed as "namespace squats." After two rounds of
clarification, the framing doesn't survive: every reserved-but-
unwired feature in the codebase has an intended landing. What
differs is the *carrying cost* and the *design certainty*. The
section reads more honestly as a status of half-built work than as
an indictment of unused scaffolding.

- **Tainted/secret tag bits — foresighted, with the typed-vs-runtime
  question still open.** `value.c:45–46` defines `JACL_FLAG_TAINTED`
  and `JACL_FLAG_SECRET` with a full helper API (`jacl_is_tainted`,
  `jacl_set_tainted`, `jacl_clear_tainted`, plus the secret-flag
  versions in `value.c:392–400`), and they're preserved across GC
  via `JACL_FLAGS_MASK` (`gc.c:723, 780, 836`). Grep returns zero
  producers and zero consumers elsewhere in the codebase today — but
  the feature is planned, not abandoned: taint/secret tracking
  complements the `interpret` capability sandbox by letting an
  embedder mark untrusted inputs and reject them at sensitive sinks.
  The open design question is whether taint becomes a runtime-only
  property (the current bit-flag infrastructure already supports
  this) or a type-system property (`tainted str` as a distinct type,
  with sinks rejecting tainted inputs at compile time). Static
  information-flow systems are powerful but historically hard to
  make ergonomic; runtime taint is simpler and well-matched to the
  scaffolding that exists. The bits are foresighted prerequisites
  for whichever flavor lands.

- **Bignum / regex — libs ahead of integration.** `lib/bignum/` is
  2.1k LOC, `lib/regex/` is 2.3k LOC. Both are self-contained and
  don't drag on the main runtime; `JACL_TAG_BIGNUM` exists but isn't
  dispatched. The wiring is the scheduled work. One sub-detail
  surfaced from 3.2: per the planned bigint + bigfloat integration
  (rationals dropped), `lib/bignum/rational.h` (~241 lines) is dead
  code that won't be wired even when the rest of bignum lands. The
  smallest unambiguous "won't ever be used" piece in the tree, but
  also genuinely small — a non-issue unless someone goes hunting
  for it.

- **`match` — reserving the word, not prerequisites.** Per 3.3,
  `match` may not ship at all, and if it does, it's likely a macro
  rather than a compiler-path special form. The existing scaffolding
  (TOKEN_MATCH in the lexer, HEAD_MATCH in `ast.c`, the entry in
  `macro__is_special_form` at `compiler.c:2527`) functions as
  "reserve the word, prevent user macros from shadowing it" rather
  than "load-bearing prerequisites for the implementation." Cost is
  tiny — one lexer keyword, one HEAD enum entry, one special-form
  recognition line — and the reservation makes sense even if the
  feature doesn't land.

The net pattern: there's no truly unjustified architecture-without-
implementation in the codebase. The original "namespace squat"
framing was hunting for a kind of dead-weight that isn't actually
there. The accurate critique is narrower: some planned features have
open design questions (taint typed-or-not, match-or-no-match), and
those uncertainties carry small forward costs (tag bits, scaffolding,
dormant libs) that are easy to defer. That's a normal state for an
in-flight language, not a weakness.

### 3.5 Stack limits are tighter than they look because TCO isn't wired

`VM_STACK_MAX = 256` and `VM_FRAMES_MAX = 64`. §16 (2026-05-15) made
the failure mode legible by distinguishing operand-stack vs call-
frame overflow and propagating the result to the awaiter. So far so
good.

The buried issue: **`OP_TAIL_CALL` exists in the VM** (declared
`bytecode.c:43`, handler at `vm.c:2901–2935`) and reuses the current
frame. **The compiler never emits it** — grep for
`OP_TAIL_CALL`/`emit_tail`/`tail_position`/`is_tail` in `compiler.c`
returns nothing. So every recursive call consumes a fresh frame. The
infrastructure is in place; the compiler-side analysis pass that
identifies tail-position call sites just hasn't been written. Per
the author, the gap is unimplemented rather than deliberate.

That makes 64 frames noticeably tighter than it would otherwise be:

- **With TCO**: 64 frames covers all practical recursion depths because
  tail-recursive walkers, traversers, and accumulators don't consume
  frames at all.
- **Without TCO**: a 64-deep directory tree, a 64-deep JSON document, a
  64-deep AST visitor each runs out of frames. These come up in
  glue/scripting; they aren't "math library" use cases.

Two paths forward, both planned:

- **Short-term: bump the constants.** `CallFrame` is ~32 bytes
  (4 pointers + one `uint32_t`, `vm.c:80–86`). Going from 64 frames
  to 256 costs ~6KB more per VM; from 256 operand slots to 1024
  costs ~6KB more. ~12KB total for a 4× increase across both limits
  — negligible per VM, and the author has said it should happen.
- **Durable fix: compiler-emitted `OP_TAIL_CALL`.** The runtime side
  is done. Compiler-side analysis is small: identify proc bodies
  whose last expression is a call, in tail position (not inside
  `try`, not the source of a binding, not followed by a pipe stage
  that consumes the return). One nuance — suspending procs are
  CPS-transformed, so the "tail call" in a suspending proc becomes a
  continuation construction. That's not impossible (the SM-compile
  path could emit a continuation-tail variant) but it's the part
  worth thinking through before the analysis pass lands.

Growable stacks are a further option (heap-allocated arrays, GC-
traced, inline-bitmap shadow arrays scaled to stack size), but
probably not needed if TCO + 4× constants are in. The audience
isn't writing parsers in JACL itself; the planned mitigations
should cover the recursive-traversal patterns that do come up.

### 3.6 Necessary complexity for the full build, but the embed cost is real

The runtime architecture — NxM work-stealing + non-moving generational
GC + lockless Chase-Lev deques + direct-style CPS+SM suspension — is
*load-bearing* for the capabilities it provides. You cannot have those
goals and a simpler runtime story; the per-field synchronization
invariants in `SYNCHRONIZATION.md` aren't unnecessary architecture, they
are necessary architecture documented at a level most production
runtimes skip. That's a strength.

The honest follow-up: the audience JACL targets (glue/scripting, with
embedding as the path to widening) doesn't strictly need all of those
capabilities. The architecture has been built with capability headroom
in mind — the full runtime is more powerful than glue/scripting demands
— and the cost of that headroom is showing up in embed scenarios
where binary size and per-VM footprint matter. That's the version of
"too much complexity" worth taking seriously: not "the design is over-
engineered for what it does," but "the full capability set costs more
than embedders want to pay when they don't need all of it."

#### Highest-leverage cut: build-time-conditional multi-threaded runtime

The single largest source of complexity is the multi-threaded runtime
layer:

- `runtime.c` — workers, Chase-Lev deques (public + private per
  worker), grey buffer, thief registration, idle CV park, work-stealing
  scheduler.
- `gc.c` / `gc_collect.c` concurrent paths — concurrent sweep, SATB
  write barrier, remembered set, epoch protection, watermark logic.
- All of `lib/chase_lev/`.
- Most of `SYNCHRONIZATION.md`.

**The single-threaded paths already exist.** `vm.c:5120–5125` shows
the spawn dispatch as `if (vm->runtime) { runtime__submit_spawn_task(...) }
else { /* synchronous single-threaded path */ }`, repeated for
parallel and race. So `spawn`/`await`/`parallel`/`race` semantics are
already implemented twice — once for the threaded runtime and once
as cooperative single-thread coroutines.

Gating the threaded runtime behind a build flag (e.g. `JACL_THREADED`)
would let embed users opt for the single-threaded build and get:

- Plausibly 30–50% smaller binary (everything in `runtime.c`, the
  concurrent GC paths, Chase-Lev, the thread machinery).
- Half of `SYNCHRONIZATION.md`'s surface gone (most of it is about
  cross-thread state).
- No atomics in hot paths — alloc fastpath stops reading TLS, GC mark
  loop stops doing epoch-protected reads.
- WASM-friendly. The WASM build is hampered by thread machinery it
  can't use anyway; the single-threaded build would be the natural
  WASM default.

CPS + SM cooperative concurrency stays. The surface language stays.
FFI stays. Structs, macros, gradual typing all stay. What's lost is
CPU parallelism — exactly the capability glue/scripting embed users
mostly don't need.

This is the highest-leverage lever. It fits the "capability headroom"
goal because the full threaded runtime stays available as the default
build; users who care about embed size opt out.

#### Tier-2 lever: simpler GC for the single-threaded build

If you have the threaded-or-not flag, you can also pick a simpler GC
for the single-threaded build. Caveat: the generational collector is
**fully wired**, not partial — `hdr->gen` bit, `survive_count`-based
promotion, `gc_mark_minor`/`gc_sweep_minor`, `RememberedSet` with a
working write barrier, `gc_should_major` dispatch logic at the trigger
site. So "drop generations for embed" would be removing working code,
not unwiring a stub.

The trade is: lose a generational collector that already pays off on
long-running scripts (old-gen objects skipped on minor cycles), in
exchange for a smaller binary and simpler reasoning. Whether this is
worth it depends on the target embed workload — a script that runs
once and exits doesn't benefit much from generations; a long-lived
embedded plugin does. Worth measuring before committing.

If you take the cut, the candidates are: drop generations (remove
`gc_mark_minor`/`gc_sweep_minor`/`RememberedSet`/`gc_should_major`),
drop the concurrent sweep (redundant with single-threaded), drop the
SATB barrier (only matters for concurrent collection), keep the
block/line free-run allocator (it's a good non-moving design and
earns its keep). Estimated: another 15–25% off the embed binary on
top of the threaded-or-not cut.

#### Smaller cuts, independent of the build-flag work

- **Consolidate dual struct definitions.** `WorkerThread`, `Runtime`,
  `RuntimeTask`, `ThreadHeap`, `GreyBuffer`, `RememberedSet` are
  defined twice — in `runtime.c`/`gc.c` (unity build) and in
  `src/jacl.h` (separately-compiled tests), with
  `test/test_struct_sizes.c` as a sizeof + offsetof drift guard. The
  guard catches size changes but only spot-checks individual field
  offsets, so silent reorderings that preserve total size can slip
  through to a future failure mode. Options: thread everything via
  `jacl.h` (forces full rebuilds on internal-field changes), make
  tests go through the unity build (couples test compilation to the
  runtime), or use opaque pointers + accessor functions in `jacl.h`
  (cleanest, costs a few accessor functions). Doesn't shed runtime
  size, but removes a class of drift bug and a chunk of maintenance
  overhead.

- **`vm.c` split.** Currently 12,200 lines in one TU. Direct-threaded
  dispatch wants opcodes co-located, but splitting by category
  (arithmetic / control flow / collections / GC ops / shell / SM
  resume) while keeping the dispatch table in one place could
  plausibly halve the file size. Doesn't cut runtime cost; reduces
  cognitive load and may make small fixes (the `OBJ_CLOSURE = 0`
  enum hazard is the canonical case) actually get made instead of
  deferred.

- **Drop `lib/bignum/rational.h`** (per 3.4 update). 241 LOC that
  won't be wired. Pure cleanup, negligible win, but worth doing if
  any reorganization happens nearby.

- **Check the `ctx_pool`.** `$ctx` forks are rare (only at
  `with-ctx` / `spawn` / `parallel` / `race`) and short-lived. The
  pool may not be earning its keep over regular `gc_alloc`. Worth
  profiling before cutting; could be one less data structure.

#### What to leave alone

- **Struct value-type / inline opcodes.** Big complexity, big payoff
  for FFI. Stays.
- **Direct-style CPS + SM concurrency.** The differentiator. Stays.
- **`interpret` capability sandbox.** Core for embed use cases. Stays.
- **Macro system.** Core to language identity. Stays.
- **C-ABI struct layout.** FFI win. Stays.

#### Recommended order

1. **Prototype `JACL_THREADED` as a build flag.** Even before any
   cuts, get the codebase to compile with the threaded runtime
   disabled and verify the single-threaded paths actually work
   standalone. Where the runtime layer leaks into the single-threaded
   path will itself be useful audit signal.

2. **Measure.** Binary size and per-VM footprint for both flavors.
   Quantify the win before deciding whether to also pursue the
   simpler-GC path.

3. **Decide on the GC swap based on measurements.** If the build-flag
   alone hits the embed size budget, leave the GC alone. If you need
   more, take the simpler-GC cut and accept the regression on
   long-running workloads.

4. **In parallel**: the smaller cuts (struct consolidation, `vm.c`
   split, rationals removal, ctx-pool check) can happen any time and
   are independent of the build-flag work.

The reconciliation between "capability headroom" (1) and "intellectual
interest" (3) and "embed cost" pressures: the full capability stays
the default; opt-out flags handle the embed case. That's the cleanest
seam I can see.

#### Residual concerns independent of cuts

- **Onboarding friction.** A new contributor has to read
  `GC_CONCURRENCY_DESIGN.md`, `SYNCHRONIZATION.md`, and `AUDIT.md`
  before touching `runtime.c` or `gc.c` without risking a regression.
  That's the cost of the complexity (which is load-bearing for the
  full build), not the cost of the discipline (which is what makes
  the cost survivable). The build-flag work narrows this — embed
  contributors who only build single-threaded face a much smaller
  surface — but doesn't eliminate it for full-build maintainers.

### 3.7 `vm.c` is large; the cost is token economy, not cognitive load

12,200 lines in one TU. Direct-threaded dispatch (222 opcodes per
AUDIT.md §13) wants the handlers reachable from one dispatch loop
for the goto-label addressing trick, but the handlers themselves
don't have to be inline in the same source file — a split via
`#include`-d handler files (CPython's pattern) could co-locate the
dispatch loop while distributing handler bodies across category
files. A practical split (arithmetic / control flow / collections /
GC ops / shell / SM resume) could plausibly take the core `vm.c`
down to 1–2k lines plus a directory of handler includes.

**The cost metric is different than it would be for human dev.** JACL's
development is primarily agent-driven, so the cost of 12k lines is
not "humans get lost in it" but "each cross-cutting read burns more
tokens than it needs to." Evidence the file size isn't blocking work:
the §D.1 `JACL_ASSERT_TAG` pass (56 sites) and §D.2 `VM_ERROR`
cleanup (72 sites across 39 opcodes) both landed, so motivated work
gets done. The cost is in the *steady-state token economy* — every
exploratory pass, every cross-handler refactor, every "understand
the dispatch" reading session pays the full-file load cost. A split
would shift that toward "pay for what you load," cheaper on
category-localized changes (the common case) and roughly neutral on
genuinely cross-cutting ones.

So: worth doing for ongoing token cost, not urgent because no
specific edit has been blocked by the size.

**Aside on the `OBJ_CLOSURE = 0` enum.** An earlier draft of this
critique cited this as a "cost-of-edit too high" signal — the fix
not happening because walking the blast radius across 12k lines was
too expensive. After confirming with the author, the actual reason
is closer to "small wart, working mitigation in place, never
escalated to a priority item." Briefly, since this surfaces a
different kind of issue worth recording:

- `GCObjType` is the heap-object-kind tag stored in every GC header.
  `OBJ_CLOSURE` is the first enum value (`gc.c:22`), so it equals
  zero.
- Risk: a GC header read mid-init (before `obj_type` is written) or
  post-sweep (stale ref into a zeroed slot) would have a zero
  `obj_type` and dispatch to the closure handler, reading upvalue
  pointers on garbage.
- Did surface: the May 2026 perf-bench bug, fixed in `bc74a10` by
  having the mark loop skip any object with `alloc_total == 0`
  (which a live object never has). That mitigation is reliable —
  the SEGV class is closed.
- Defense-in-depth proposal: introduce `OBJ_INVALID = 0`, shift the
  real types up by one. Then a zero `obj_type` dispatches to "no
  real case" regardless of `alloc_total`. Two-line enum change,
  modest blast radius across `GCObjType` switch sites.

The reason this surfaces in a critique like this one and not in
day-to-day work: design warts known to a previous agent session
don't necessarily persist in the author's working memory, and there
isn't a durable issue-tracker surface for "small defense-in-depth
fixes worth doing eventually." That's a meta-issue separate from
the file-size concern — worth its own note: critical reviews catch
this kind of thing precisely because they re-derive everything from
the code, but the findings need somewhere durable to live or they
decay between sessions.

## 4. Pulling-their-weight scorecard

After the section 1–3 revisions, the simple earning/not-earning split
the original draft used no longer fits the picture. Four buckets read
honestly. The bottom line up front: **the residual genuinely-wasted
complexity is small.** Most of what looked like waste on first pass
turned out to be either earning its keep, foresighted for a planned
feature, or a candidate for build-flag-conditional inclusion rather
than removal.

### 4.1 Earning their keep (full build)

These pay off for the language as currently built. Trade-offs are
documented elsewhere; the net is positive.

- **CPS + SM concurrency transform.** The differentiator. Caveat per
  §2.1: "no function coloring" is a syntax claim; `SuspensionAnalysis`
  is implicit compile-time coloring.
- **Errors as tag-bit flag + pipe short-circuit.** Clean composition;
  silent-swallow and `parallel`-mixed-results trades documented in §2.3.
- **Epoch non-moving generational GC.** Fully wired generations
  (verified in §3.6: `hdr->gen`, promotion via `survive_count`,
  `gc_mark_minor`/`gc_sweep_minor`, `RememberedSet` + barrier,
  `gc_should_major` dispatch). Pays off on long-running scripts.
- **Three-mode parser as a system.** `[]`/`{}` is the workhorse; `()`
  is a deliberately-narrow `expr`-style sublanguage with mode-
  independent operator semantics (§1).
- **C-ABI struct layout.** Big FFI win. Asymmetric per §2.5 —
  stack-passed structs get the full benefit; collection/closure-capture
  patterns still box.
- **`interpret` capability sandbox sharing parent heap.** Trade-off
  (no per-thread VM isolation) documented in AUDIT §17; right call
  for embed scenarios.
- **Typed `$ctx` (in its corrected scope).** Earns its keep in the
  scope where fork is explicit: `with-ctx`, `spawn`, `parallel`,
  `race`. Regular proc calls share `$ctx` per §2.4 / the SYNTAX.md
  correction.
- **Macro system with syntax objects + hygiene.** Core to language
  identity. Macro/parser contract issue (binding-operator shape
  ambiguity) is real but a refinement, not a "doesn't earn."
- **Operators-as-macros, one meaning per operator.** Clean unified
  model after the §1 walk-back.
- **Stream type + sequence ops over streams and vectors.** Composes
  with `for`/`filter`/`transform`; lets `!cmd` pipelines stay lazy.
- **Bytecode VM + direct-threaded dispatch.** Foundational. The
  direct-threaded path (AUDIT §13) earns ~2–3% on its own but the
  architectural option matters more than the delta.
- **Gradual type system.** Pays off at FFI/static slots; the deferred
  items (closure signatures, imported struct fields, swap struct-elem
  narrowing) are refinements rather than gaps in the core.

### 4.2 Build-flag candidates (full earning, but high embed cost)

Earning their keep for the full build; costing more than embed users
want to pay. Per §3.6, the cleanest answer is conditional compilation
rather than removal.

- **Multi-threaded runtime** (workers, Chase-Lev deques, grey buffer,
  idle CV park, thief registration). Highest-leverage cut. The
  single-threaded paths already exist in `vm.c`.
- **Concurrent sweep + SATB write barrier + epoch protection.** Tied
  to the threaded runtime — only matters when collection runs
  concurrently with mutators.
- **Generational GC (tier-2 cut).** Working code, not a stub. Trade-
  off discussed in §3.6: lose long-running-script pay-off, gain
  binary size and reasoning simplicity. Measure before committing.

### 4.3 Waiting on integration (will earn once wired)

Reserved or partially-built. Carrying costs are small; landing the
wiring unlocks the payoff.

- **Bigint + bigfloat dispatch.** `JACL_TAG_BIGNUM` reserved;
  `lib/bignum/` ready; planned with implicit promotion in `dyn` per §3.2.
- **Regex.** `lib/regex/` (Thompson NFA) self-contained; integration
  scheduled.
- **Shell-language surface features.** `par-each`, `timeout`, `$env` +
  built-in aliases, `watch`, `glob`, `read-file`/`write-file`/
  `append-file`. The runtime primitives they compose on top of are
  already in place.
- **`match` (uncertain).** May not ship; if it does, macro path is
  a litmus test for the macro system (§3.3).
- **TCO emission.** `OP_TAIL_CALL` exists in the VM; compiler-side
  tail-position analysis pass needed (§3.5).
- **Taint/secret enforcement.** Flag bits and helper API present;
  producers, consumers, and the typed-vs-runtime design choice still
  open (§3.4).

### 4.4 Genuinely not earning (the small residual)

After the walk-backs, this bucket is short.

- **`lib/bignum/rational.h`** (~241 LOC). Won't be wired per the
  bigint + bigfloat integration plan (§3.2). Pure cleanup; worth
  dropping if any nearby reorganization happens.
- **`OBJ_CLOSURE = 0` enum wart** (§3.7). Working mitigation in place
  (`alloc_total != 0` skip in the mark loop); defense-in-depth fix
  (introduce `OBJ_INVALID = 0`, shift the rest) is two lines and
  unticketed.
- **`ctx_pool` (maybe).** Forks happen only at four explicit sites and
  are short-lived; the pool may not earn over regular `gc_alloc`. Per
  §3.6, profile before deciding.

That's it. Everything else either pays its way today, is a build-flag
question rather than a removal question, or is on a planned integration
path. The original draft's longer "not earning" list collapsed to three
small items once the framings settled.

## 5. Innovations and whether they're worth it

1. **Three-mode delimiter parsing.** Novel as a coherent system. Worth
   it for `[]` / `{}`; cut or rethink `()`.
2. **Direct-style CPS+SM concurrency on a tagged-value VM with
   non-moving epoch GC.** The combination is unusual; each piece exists
   elsewhere. Worth it — the technically distinguishing feature.
3. **Implicit typed `$ctx` with compile-time field declarations + COW
   forking + spawn-snapshot semantics.** A genuinely new take on
   dynamic scoping. Worth it.
4. **Error-flag-bit in tagged values + implicit propagation through
   pipes.** Not entirely new (NaN-boxing has been around) but the
   pipe-short-circuit composition is a fresh combination. Worth it.
5. **Capability-restricted `interpret` reusing parent heap.** Pragmatic,
   not a research contribution, but the trade-off analysis in §17
   (sandboxing without per-VM isolation) is the right one for the embed
   scenario.
6. **Methodological:** the `SYNCHRONIZATION.md` per-field reference and
   the multi-phase audit pattern. Not a language feature but a project
   artifact worth keeping.

## 6. Domains

### 6.1 Will be good at

- Embedded scripting in C/C++ apps (real concurrency + capability
  sandboxing + FFI struct passing — this is where it's strongest)
- DevOps/build tooling that outgrew bash but doesn't want to be a
  Python project
- Sysadmin glue where you need real concurrency for spawning +
  supervising external processes (Jobs + `&` + `signal`/`cancel` is the
  right shape)
- Test harnesses and orchestration tools embedded in larger systems

### 6.2 Will be bad at

- Numeric / scientific code (no numeric tower, no SIMD, gradual-typing
  overhead, RRB vectors aren't unboxed numeric arrays)
- High-performance systems work (256-slot operand stack, non-moving GC,
  no value-type unboxed arrays)
- Mainstream application development (no LSP, no package manager, no
  ecosystem — same chicken-and-egg every new language faces)
- Anything where pattern matching is the bread and butter (parsers,
  compilers, protocol decoders) — until match ships

## 7. As a program

About 28k LOC of C plus ~50k of single-header libs. Unity build is
sensible at this scale. The codebase reads like one person with strong
taste who has stayed disciplined across a long project — consistent
naming, real documentation, real tests, real audit. The hot paths
(`runtime.c`, `gc.c`, `vm.c`, `compiler.c`) are reasonably well-factored;
the struct-drift hazard and `vm.c` size are the two visible scaling
tensions. Concurrency correctness is the part to most trust — the chaos
tests + TSAN soak + per-field synchronization doc is more rigor than
most production runtimes get.

The thing to worry about if evaluating this for a real bet: it's been
built by a single voice with a strong vision, and the vision is
*opinionated*. Some of those opinions (three-mode parsing, no
precedence, bare-words-are-strings, errors-as-tag-bits) will be
load-bearing for whoever decides to use it. They are not
majority-comfortable defaults. Whether that's a moat or a ceiling
depends entirely on whether the target audience self-selects into those
tastes.

## 8. One concrete suggestion

The single highest-leverage thing to ship next is **`match`**. It
unblocks a typer item, it's table-stakes for the Clojure-inspired
marketing, and once it exists every code sample becomes more idiomatic.
The shell features (`par-each`, `glob`, `$env`, file I/O) matter for
the "scrappy shell language" pitch and would come second. The
numeric-tower wiring matters for credibility but is third-priority
because most embed/shell use cases don't need bignums.
