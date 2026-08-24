# jacl

Just A Command Lisp. A fusion of a command language and a lisp — the
love child of Tcl and Clojure. Gradually typed, with inline operators,
unboxed typed values, an NxM work-stealing runtime, a non-moving
tracing GC, and top-tier C FFI for embedding. Be as safe or as unsafe
as you like; the language supports both.

> **⚠️ Status: very early, very WIP.** Both the language and its tooling
> are under active development and change often — expect rough edges,
> missing features, and churn (see [Status](#status) and
> `NOT_IMPLEMENTED.md`).

**Try it in your browser →
[thesherwood.github.io/jacl_impl](https://thesherwood.github.io/jacl_impl/)**
— a live playground that compiles and runs JACL entirely client-side on
the SVM backend. Nothing to install (see [Playground](#playground)).

## Goals

- **Safety by default**, with absolute freedom to be unsafe (poke at
  raw memory addresses via `[Ptr T]` / `[Buf N T]` when you want to).
- **Great shell language** — bare-command syntax, pipelines.
- **Great comptime/macros** — a lisp under the hood; AST macros,
  hygienic, with quasiquoting.
- **Top-tier FFI** — usable as an embedded command language in C apps.
- **Effortless, safe concurrency** — direct-style `spawn`/`await`/
  `parallel`/`race`, no async coloring at call sites.
- **Gradual yet strict typing** — dynamic by default; opt into static
  types for unboxed structs/scalars with no boxing or dyn dispatch.
- **Sandbox untrusted code** — restricted preludes via `[interpret]`,
  no performance compromise for trusted code.

## Status

**This is an experimental, work-in-progress language.** Syntax, APIs, and
internals move frequently and there is no stability guarantee yet.

The core language is implemented and exercised by a large test corpus
(the `*.jacl` programs under `test/jacl/`, the SVM runtime/codegen
suites under `runtime/`, and the frontend C units under `test/`). The
sole backend is the **SVM** (`codegen/*.c` → SVM IR, linked against the
`runtime/` library and run on the SVM engine); the legacy in-tree
bytecode compiler + VM was removed in the SVM-backend migration
(item 6). Implemented: value
representation, lexer/parser, procs and control flow, strings,
persistent collections, error handling, mutable state, the static type
system, the tracing GC, the NxM concurrency runtime, modules, macros,
and the three-mode syntax.

The GC and concurrency paths were hardened by a multi-session audit
campaign (see `DESIGN.md` "M12/M13 hardening note", `AUDIT.md`, and
`AUDIT_HISTORY.md`).

**Not yet implemented** (tracked in `NOT_IMPLEMENTED.md`): `match`/case,
callable values (`[$map key]`), `$env` / `with-env`, aliases, globbing,
regular-expression literals (the NFA engine exists in `lib/regex` but
isn't wired in), and the bignum numeric tower (`lib/bignum` exists,
not yet dispatched). The infix `()` mode was removed — use `[]`.

The single best up-to-date picture of working syntax is
`test/jacl/tour.jacl`: the harness runs it, so it cannot drift from the
implementation. `SYNTAX.md` is the full reference. The examples below
are drawn from that tour.

## A taste

```
proc fib {i64 n} i64 {
  if [< $n 2] { $n } { + [fib [- $n 1]] [fib [- $n 2]] }
}

proc main {} {
  def nums [vec 1 2 3 4 5]
  def doubled [collect [transform $nums [\ * $it 2]]]
  print "doubled: $doubled"          # doubled: [vec 2 4 6 8 10]
  print "fib 10 = $[fib 10]"         # fib 10 = 55
}
main
```

## Syntax

**Comments.** `#` to end of line; `##` is a doc comment.

```
# a comment
## a doc comment (extractable by tooling)
```

**Bindings.** `def` is immutable, `mut` is a mutable cell, `set`
reassigns. Bindings are name-first prefix commands — there is no infix
binding operator. Read a variable with `$name`.

```
def imm 7            # immutable
mut counter 0        # mutable cell
set counter 2        # reassign
```

**Typed bindings.** Type before name. Typed locals stay unboxed and
arithmetic narrows accordingly — no boxing, no dyn dispatch.

```
def i64 x 10
def i64 y 20
def i64 sum [+ $x $y]
def i64 wide [to i64 $small]   # explicit conversion via [to TYPE val]
```

**Two parsing modes.** `[]` is juxtaposition (head + args — a value
computation). `{}` is command mode (bare commands / pipelines, and also
the form for param lists, struct fields, and destructuring patterns).
Bare words are strings.

```
def total [+ 1 [* 2 [+ 1 3]]]   # nested value computation → 9
```

**String interpolation.** `$var` and `$[...]` (juxtaposition) nest
inside `"..."`. Triple-quoted strings strip leading indentation.

```
def msg "name=$who age=$yrs sum=$[+ 2 3]"
```

**Procs.** Params and body are both `{}` blocks; types go before names.
The return type sits after the params block. Untyped params are `dyn`.

```
proc add {a, b} { + $a $b }                 # untyped
proc typed-add {i64 a, i64 b} i64 { + $a $b }
proc sum-all {..nums} {                      # variadic: collects trailing args
  mut total 0
  for $nums n { set total [+ $total $n] }
  $total
}
sum-all ..$xs                                # `..$vec` splats at a call site
```

**Lambdas.** `[\ body]` is a one-arg proc with `$it` as the parameter.

```
def square [\ * $it $it]
collect [transform [vec 1 2 3] [\ * $it 2]]   # → [vec 2 4 6]
```

**Control flow.** `if` is an expression. `while [cond] {body}`. `for`
comes in implicit-`$it`, explicit-binding (`for COLL NAME {...}`), and
C-style (`for {init; cond; step} {...}`) forms, over vectors, arrays,
ranges, and streams.

```
if [> $n 0] { "positive" } elif [== $n 0] { "zero" } else { "negative" }

for [vec 1 2 3] { print $it }
for [range 0 3] n { ... }
for {mut i 1; <= $i 4; incr i} { ... }
```

**Collections.** `[vec ...]` (persistent RRB vector, immutable) and
`[map ...]` (persistent HAMT) are dyn by default; `[[Vec T]]` /
`[[Map K V]]` are typed and check element types at push/set. `[arr ...]`
(`[Arr T]`) is a mutable, segment-array-backed *reference* array.

```
def m [map a 1 b 2]
map-get $m a                       # → 1
def [Vec i32] ns [[Vec i32] 10 20 30]
def nums [arr 1 2 3]               # mutable; arr-push / arr-set / arr-get / arr-pop
```

**Structs.** Defined at module level, type before name; constructed
`[Name field val ...]`; fields read with `->` and mutated with the
3-arg `. obj field val` form (field must be `mut`). Structs are flat,
header-less, C-ABI-laid-out value types and may carry any numeric type.

```
struct Pt {i32 x, i32 y}
def p [Pt x 3 y 4]
print $p->x                        # → 3
```

**Destructuring.** Positional `[]` for vectors, named `{}` for
structs/maps, `_` wildcard, `..rest` for the remainder.

```
def [a b c] [vec 1 2 3]
def {x, y} $pt
def [head ..tail] [vec first second third]
```

**Streams and generators.** Any proc containing `yield` is a generator;
each call returns a fresh lazy stream that suspends at each yield.
`range` / `range-inclusive` / `lines` are stream sources; `transform` /
`filter` / `take` are lazy; `collect` materializes (a typed stream
collects to a typed vec).

```
proc count-up {start, stop} {
  mut i $start
  while [<= $i $stop] { yield $i; set i [+ $i 1] }
}
collect [take [count-up 1 100] 3]   # → [vec 1 2 3]
```

**Pipes.** `|` is a command-mode operator that threads the result into
the next stage's first argument.

```
range-inclusive 1 10 \
  | filter [\ == 0 [% $it 2]] \
  | transform [\ * $it $it] \
  | collect \
  | print
```

**Mutable state.** `box` is a thread-local cell; `atom` is a
CAS-shared cell. `reset` stores, `swap` applies a fn, `deref` reads.
`watch` registers a keyed listener firing on each commit with
`(old, new)`.

```
def cell [box 0]
reset $cell 1
swap $cell [\ + $it 5]             # deref → 6
watch $counter "log" [\ ...]       # atom listener
```

**Errors as values.** `error` produces an error *value* that propagates
through pipes; catch it with `try {body} name {handler}`. `assert`,
by contrast, halts the VM.

```
try { error bad } e { concat "caught: " [error-val $e] }   # → "caught: bad"
error? [error boom]                                        # → true
```

**Dynamic context (`$ctx`).** Fields declared with `ctx` at module
level are visible everywhere. `with-ctx {field val} {body}` overrides
for the dynamic extent of the body and restores on exit (including on
error).

```
ctx mut i32 verbosity 0
with-ctx {verbosity 3} { ... }     # $ctx->verbosity is 3 inside, 0 after
```

**Macros.** `defmacro` registers a compile-time rewrite; `syntax-quote`
templates AST and `~name` splices a captured arg.

```
defmacro unless {cond body} { syntax-quote [if ~cond {} ~body] }
```

**Concurrency.** `spawn {block}` returns a future; `await` blocks until
it resolves. `parallel { } { } ...` runs blocks concurrently and
returns results in argument order; `race` returns the first to
complete. Procs that internally suspend (`await`, `sleep`) are
suspending too — but the CPS state-machine transform makes that
invisible at the call site (direct style, no async coloring).

```
def fut [spawn { + 20 22 }]
await $fut                         # → 42
parallel { 10 } { 20 } { 30 }      # → [vec 10 20 30]
```

**Sandboxing.** `[interpret SRC]` runs SRC in a child VM with the
default prelude; `[interpret PRELUDE SRC]` restricts to the entries in
PRELUDE (an empty map yields a no-I/O sandbox). Blocked primitives
surface as error values rather than aborting.

```
interpret "[+ 1 [* 2 3]]"          # → 7
interpret [map] "[+ 10 20]"        # → 30 (restricted prelude)
```

## Build & Test

Three pre-merge baselines:

```
./build.sh                  # full normal-mode test sweep
./build.sh --tsan           # ThreadSanitizer sweep (two known-and-safe
                            #   failures: TSAN blindness on barrier/fence/
                            #   epoch-mediated synchronization, not missed
                            #   barriers — see DESIGN.md)
```

The SVM backend's own tests live under `runtime/` (`runtime/harness` — the
`cargo test` suites — and the `runtime/tests/*.c` GC/scheduler tests). The
browser playground is built by `demo/svm/build_assets.sh`.

Each run prints `=== Results: N passed, M failed ===`. Filter to one
test:

```
./build.sh --test=<name>          # one test, normal mode
./build.sh --tsan --test=<name>   # one test, TSAN
./build.sh --release              # -O2 build in .build-release/ (for benchmarks)
```

Run a single `.jacl` source through the SVM CLI (`jacl_svm`, built from
the runtime harness crate):

```
cd runtime/harness
cargo run --bin jacl_svm -- run ../../test/jacl/<name>.jacl
cargo run --bin jacl_svm -- run ../../test/jacl/tour.jacl   # the syntax tour
```

The execution engine is the SVM, vendored at `vendor/svm` (from
[`theSherwood/vm`](https://github.com/theSherwood/vm)): the C runtime
under `runtime/` is compiled to SVM IR via `svm-llvm` and executed on
the SVM's tree-walking interpreter and Cranelift JIT (the runtime
harness runs both and asserts they agree). See `AUDIT.md` for the
per-baseline rationale and `AUDIT_HISTORY.md` for the audit campaign.

## Playground

A live, hosted playground runs in the browser at
**<https://thesherwood.github.io/jacl_impl/>** (deployed from `main` by
`.github/workflows/pages.yml`). Edit JACL and run it with no install — the
whole pipeline (source → SVM IR → link → run) executes client-side.

It runs on the **SVM backend** — the same `vm` engine the rest of the
project uses, vendored at `vendor/svm` from
[`theSherwood/vm`](https://github.com/theSherwood/vm) and compiled to
WebAssembly. Editing recompiles the source to SVM IR in the browser
(`jacl_emit.wasm`, the LLVM-free frontend), then links and runs it against
the runtime blob (`jaclrt.svm`) on the `svm-browser` cdylib; unedited
examples run from precompiled `.svmb` blobs.

The source lives in `demo/` — a CodeMirror 6 editor with a position-aware
JACL syntax mode, optional vim keybindings, and a draggable panel divider.
To build and serve it locally:

```
cd demo && bash svm/build_assets.sh   # svm_browser.wasm + jacl_emit.wasm + jaclrt.svm + example .svmb
bash build_demo.sh                    # regenerates examples.json, bundles src/ → dist/playground.js
python3 -m http.server 8080           # or any static server
# open http://localhost:8080
```

The bundle (`demo/dist/playground.js`, ~430 KB minified) is committed
so the playground works from a clone without a Node toolchain;
`build_demo.sh` only re-bundles when you edit `demo/src/`.

## Where to read more

- `DESIGN.md` — core decisions, milestone roadmap, project layout
- `SYNTAX.md` — full syntax reference + per-feature status
- `TYPE_SYSTEM.md` — gradual typing architecture
- `STRUCT_DESIGN.md` — flat, header-less struct value types
- `GENERATOR_STATE_MACHINE.md` — the CPS/SM transform for suspension
- `GC_CONCURRENCY_DESIGN.md` — GC and concurrency co-design
- `NOT_IMPLEMENTED.md` — index of planned / deferred / open items
- `test/jacl/tour.jacl` — the canonical, harness-verified syntax tour
