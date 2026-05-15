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

### 3.1 The shell-language pitch is undercut by what's still missing

`par-each`, `timeout`, `$env`, `watch`, `glob`, `read-file` /
`write-file`, regex, and bignum are still unimplemented. Those aren't
corner cases — they are core shell-language and scripting expectations.
The runtime/concurrency foundation is excellent; the surface-level shell
features lag it. A user reaching for JACL "to replace big bash scripts"
today will hit the I/O command gap immediately.

### 3.2 The numeric story is the most surprising gap

`JACL_TAG_BIGNUM` exists in the value layout and `lib/bignum/` is 2,100
LOC of bigint/bigfloat/rational. None of it is wired into the VM. Either
commit to the numeric tower (and pay the integer-overflow-promotes-to-
bignum cost) or remove the dead reservation. The current state — tag
bit reserved, lib present, zero dispatch sites — is the worst of both:
maintenance burden with no user value.

### 3.3 `match` not being implemented is bigger than the "Large" tag suggests

It's lexed, `HEAD_MATCH` is a special form, but there's no compile path.
For a language that explicitly cites Clojure as a parent and has done
the work to ship structs, futures, and macros, no pattern matching is a
strange prioritization. The typer rules for match arms (the deferred
TYPE_SYSTEM item) are dead-code until then, but the *runtime* match
impl is the load-bearing piece — users will reach for it constantly in
error handling, parsing, and shape dispatch.

### 3.4 Some declared features look like namespace squats

Tainted/secret tag flag bits in the value layout (DESIGN.md:10) — where
are the consumers? The flags exist but there's no propagation analysis
or sink-side check. Either there's a pass I missed, or those bits are
reservations awaiting a feature that hasn't landed. Same shape as
bignum and regex: laying tracks without trains.

### 3.5 Stack limits are tight

`VM_STACK_MAX = 256` and `VM_FRAMES_MAX = 64` are fine for shell scripts
and embedding glue, but substantial JACL code (recursive descent parsers,
tree algorithms, math libraries) will hit the 64-frame ceiling. §16
made the failure mode legible, which is the right first step, but the
limit itself will be a recurring complaint as soon as anyone writes a
real program. Worth a "what's the path to growable stacks" answer
before declaring done.

### 3.6 The audit discipline that's a strength is also a tell about complexity

You need a `SYNCHRONIZATION.md` because the system has more shared state
than one person can hold in their head. Not a problem yet (one author,
recent codebase) but every new contributor will pay an onboarding tax
to read three docs before touching `runtime.c` or `gc.c`. The fact that
`WorkerThread`/`Runtime`/`ThreadHeap` are duplicated between
`runtime.c`/`gc.c` and `jacl.h` (with `test_struct_sizes.c` as the
drift guard) is the kind of mitigation that survives one author and
dies on the second.

### 3.7 `vm.c` is 12,200 lines

Direct-threaded dispatch makes that somewhat unavoidable (the giant
CASE table is structural), but 12k lines in one TU is a place where
bugs hide. The `OBJ_CLOSURE = 0` enum hazard is the canonical example:
a defense-in-depth fix is a 2-line enum reorder, but no one made it
because the cost-to-walk-the-blast-radius is high. That's the smell of
a TU that's too big.

## 4. Pulling-their-weight scorecard

### 4.1 Earning their keep

- **CPS + SM concurrency transform.** The differentiator.
- **Errors as tag-bit flag + pipe short-circuit.** Clean and composable.
- **Epoch non-moving GC.** Right for embedding scenarios where you can't
  stop-the-world; possibly over-engineered for pure scripting use, but
  the audit hardening is done.
- **Three-mode parser, `[]` / `{}` split.** Yes.
- **C-ABI struct layout.** Big FFI win.
- **`interpret` capability sandbox sharing parent heap.** Clever; the
  trade-off (no per-thread VM isolation) is documented in §17 and is
  the right call for embed scenarios.
- **Typed `$ctx` with COW forking.** Solves a real problem most
  languages punt on.
- **Macro system with syntax objects + hygiene.** Core to language
  identity; the cost is real but Lisp-without-macros isn't Lisp.

### 4.2 Not earning their keep (yet)

- **`()` infix mode as a third parser mode.** Worst cost/benefit of the
  three modes. Either drop it and have operators be `{}`-mode macros,
  or commit to real precedence so users aren't paying a tax for a
  half-measure.
- **Tainted/secret tag bits.** Namespace squat without consumers.
- **Bignum tag + lib.** Namespace squat without dispatch.
- **Regex lib.** Namespace squat without dispatch.
- **`match` enum entry / special-form recognition without compile path.**
  Minor, but the same shape.
- **Prelude binding-operator macros.** The parser-shape-pattern-matching
  in `=` / `:` / `::` is verbose enough to suggest the parser should
  be emitting a cleaner shape.

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
