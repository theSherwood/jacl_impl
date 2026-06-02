# JACL Syntax Reference

JACL syntax is built around a two-mode delimiter system. This document is the full syntax reference and per-feature implementation status. For language architecture (VM, GC, types, modules), see `DESIGN.md`.

**Why two modes:** the two surviving delimiters split along a real semantic axis — expression vs. command/pipeline — rather than offering multiple flavors of the same operation. An earlier `()` infix mode was cut; see `SYNTAX_REDESIGN_2026_06.md` for the rationale.

## Two parsing modes (delimiter-determined)

- `[]` — **Prefix expression mode**: items separated by whitespace, no implied relationship. First item determines semantics. Used for procedure calls `[foo 1 2]`, value-producing expressions, and nesting `[foo [bar 3] 2]`. Pure value computation.
- `{}` / top-level — **Command mode**: bare commands with separators (`;` `,` newline). Symbolic connectors live here: `|` pipes between commands, with `&&`/`||` and redirections in the same family. Used for code blocks, param lists, struct field declarations, and shell-pipeline composition (same construct, differentiated by context).

## Key principle: print/parse symmetry

Every value prints as a form the reader accepts. `[read [print v]] == v` for everything except first-class procs (whose printed form is deliberately non-readable, e.g. `#<proc add>`).

Consequences:

- Maps print as `[map a 1 b 2]`.
- Vectors print as `[vec 1 2 3]`.
- Errors print as `[error "msg"]`.
- Structs print as `[Pt x 30 y 15]` — same form as the constructor (see Structs below).

This principle constrains constructor design: every collection / record type has a single read-and-print form, no asymmetric "init one way, print another way" surface.

## Key syntax decisions

- `proc foo {x, y} {+ $x $y}` — braces for both params and body
- `proc add {i32 x, i32 y} i32 {+ $x $y}` — typed return goes after params (optional bare type token between the params block and the body block)
- `struct Point {i32 x, i32 y}` — `defstruct` shortened to `struct`
- `[Point x 30 y 15]` — struct constructor mirrors `[map …]`, named-only, no positional form
- Type-before-name in params and fields: `{i64 a, i64 b}`, `struct Pt {i32 x, i32 y}`
- `:type` prefix in struct fields replaced with type-before-name
- Zero-arg procs: `proc greet {} {print "hello"}`
- Block bodies use bare command syntax: `{* $n 2}` not `{[* $n 2]}`

## String interpolation

Interpolation inside `"…"` strings uses one non-trivial form for embedded expressions:

```
"hello $name"                  # variable interpolation
"result: $[+ 1 2]"            # embedded expression
```

- `$name` — variable reference
- `$[...]` — evaluates a `[]` expression and splices its string form
- `\$` — escaped literal `$`
- Forms nest: `"value: $[+ $x [vec-len $v]]"`

## Pipes

- `|` is intrinsic to command mode (not bolted on)
- First-arg threading: `foo a | bar b` → `[bar [foo a] b]`
- Line-level feature, not inside `[]`

## Lambda shorthand

- `[\ + $it 2]` as macro expanding to `proc {it} {+ $it 2}`
- `$it` is implicit single-arg parameter
- Multi-arg lambdas use explicit `proc`

## Destructuring

- `[]` for positional (vectors): `def [a b c] [vec 1 2 3]`
- `{}` for named (structs/maps): `def {x, y} $point`
- `..rest` for rest patterns: `def [head ..rest] $v`
- `..` means "the rest" everywhere:
  - `{..}` — destructure all fields into scope (structs, modules)
  - `{x, ..}` — explicit x, plus everything else
  - `{x, ..rest}` — explicit x, remaining fields collected into rest
  - `[..]` — all positions
  - Only works on fully-typed containers (compiler must know names)
- `_` for wildcards
- `def {..} $point` — binds x and y from Point into current scope
- `use "shapes.jacl" {..}` — import all public bindings
- Destructuring in proc params deferred — do it in the body instead

## Match/case

- `match $val { pattern { body } ... }`
- Literals match, bare identifiers bind, `$var` pins against variable value
- Type patterns: `Point {x, y} { ... }`
- Guards use `[]` predicates: `n [> $n 0] { ... }`
- Composes with pipes: `get-shape | match { ... }`

## Shell interop

- `!cmd args...` — always means external command, in all contexts
- `|` between external commands is a real OS pipe (stdout→stdin)
- `|` from external to JACL converts stdout to string value
- External command result includes stdout + exit code; non-zero exit → JACL error value
- **REPL/terminal:** bare unknown commands fall back to `$PATH` (on by default)
- **Scripts:** bare unknown commands are compile errors (strict by default)
- **Pragma `#! path-fallback`:** opt-in for scripts to enable `$PATH` fallback (file-level)

### `!cmd` — simple form (95% of use cases)

Returns stdout as a string value on success, error value on failure.

**On success (exit 0):**

- Return value: stdout as string
- Stderr: prints to terminal (warnings, progress — visible but doesn't enter the pipeline)

**On failure (exit non-zero):**

- Return value: JACL error value with stderr as the error message
- Stdout: discarded

```
# stdout becomes a string value
def files [!ls -la]

# pipes work naturally — stdout flows through
!ls -la | split "\n" | for [\ print $it]

# failure short-circuits the pipeline
!curl $bad_url | json-parse
# curl exits non-zero → error value (message = stderr)
# json-parse never runs

# catch failure inline
!curl $url | catch {err} {
  print "curl failed: $err"
  "{}"
} | json-parse
```

### `|` between external commands

When `!cmd` pipes into another `!cmd`, it's a real OS pipe (stdout→stdin byte stream):

```
!ls -la | !grep ".jacl"    # real OS pipe, no conversion
!ls -la | split "\n"        # OS stdout → JACL string → split
```

### `exec` — full control form (rare)

Returns a struct with all streams and exit code. Use when you need stderr on success, or fine-grained exit code inspection.

```
def result [exec "ls" "-la"]
# result is a struct: {str stdout, str stderr, i32 exit}

. $result stdout | split "\n"
. $result stderr | print
if [!= $result->exit 0] { print "failed" }
```

### Design rationale

This avoids Nushell's structured-vs-raw impedance mismatch. Nushell tried to make external commands feel native in structured pipelines, but stderr bypasses the pipeline by default, `try`/`catch` doesn't reliably catch external failures, and the workarounds are inconsistent. JACL avoids this by:

- Keeping the boundary explicit (`!` prefix)
- Giving stderr a clear destination in both outcomes (terminal on success, error message on failure)
- Providing `exec` as an escape hatch for full control, rather than trying to make the simple form handle every case

### I/O redirection

JACL uses commands for I/O redirection, not operators. No `>`, `>>`, `<` syntax — file I/O composes with pipes like everything else.

#### String/stream → external command (stdin)

When a JACL string or stream pipes into `!cmd`, it feeds stdin:

```
read-file "input.txt" | !sort           # file contents → stdin of sort
!cat huge.log | lines | !grep "error"   # stream → stdin
"hello world" | !wc -w                  # literal string → stdin
```

Between two `!cmd`s, `|` remains a real OS pipe (no conversion).

#### Output to files

`write-file` and `append-file` are pipe-friendly commands:

```
!ls -la | write-file "files.txt"        # write stdout to file
!ls -la | append-file "log.txt"         # append stdout to file
!curl $url | json-parse | to-json | write-file "data.json"
```

#### Stderr

The simple `!cmd` form already handles stderr:

- On success: stderr prints to terminal
- On failure: stderr becomes the error message

For full control, use `exec`:

```
def r [exec "cmd" "args"]
. $r stderr | write-file "errors.log"
. $r stdout | write-file "output.txt"
```

#### Design rationale

Operators like `>` and `<` would be feasible as command-mode macros (mode-specific semantics already exist for `|`). But commands compose better with JACL's pipe model and avoid importing shell-specific syntax. `write-file "path"` in a pipeline is just as concise and more explicit.

### Globbing

`glob` is a command that returns a stream of paths. It reads `$ctx.pwd` for its base directory (see Implicit context).

```
glob "*.txt"                    # relative to $ctx.pwd
glob "src/**/*.jacl"            # recursive, path in pattern
glob "*.{txt,md}"               # brace expansion
```

`!cmd` does **not** expand globs — arguments are always passed literally:

```
!ls *.txt                       # passes literal "*.txt" to ls — not what you want
!ls ..[glob "*.txt"]            # splat glob results as separate args
```

Use `with-dir` to change the glob context:

```
with-dir "/var/log" {
  glob "*.log" | for f { !gzip $f }
}
```

Glob + pipes for file processing:

```
glob "**/*.log"
  | filter [\ > [file-size $it] 1000000]
  | for f { print "large: $f" }
```

#### Design rationale

No implicit glob expansion anywhere — not in `!cmd` args, not in the REPL. Globbing is always explicit via the `glob` command. This avoids the bash problem where `*.txt` silently expands (or doesn't, if no matches) and makes the data flow visible. The `glob` command composes naturally with streams, pipes, and `$ctx.pwd`.

## Streams

JACL has a stream type representing a lazy sequence — elements are produced one at a time rather than materializing all at once. Streams unify the behavior of internal and external command pipelines.

### Where streams come from

- `!cmd` — external command stdout is a stream of bytes
- `lines` — converts a byte/string stream or string into a stream of line strings
- Ranges `(1..1000000)` — produce streams of values
- Any builtin or proc that yields sequential data

### Stream-aware commands

`for`, `filter`, `transform` accept both streams and vectors:

```
# stream in, stream out (lazy, O(1) memory):
!cat hugefile | lines | filter [\ match-re /error/ $it] | for [\ print $it]

# vector in, vector out (eager):
def v [vec 1 2 3 4 5]
$v | filter [\ > $it 2] | for [\ print $it]
```

### Explicit collection — no auto-collect

Streams do not silently materialize into vectors. Commands that require random access (like `vec-get`, `vec-set`, `vec-slice`) do not accept streams — you must explicitly `collect` first. This makes memory costs visible.

```
# Streaming — O(1) memory:
!cat hugefile | lines | count

# Explicit collection — O(n) memory, user sees the cost:
!cat hugefile | lines | collect | vec-len

# Error — vec-get requires a vector, not a stream:
!cat hugefile | lines | vec-get 0    # type error
!cat hugefile | lines | collect | vec-get 0    # works
```

### Sequence operations (work on both streams and vectors)

These commands accept either a stream or a vector. When given a stream, they consume lazily. When given a vector, they work eagerly.

| Command     | Input         | Output         | Description                              |
| ----------- | ------------- | -------------- | ---------------------------------------- |
| `for`       | stream or vec | (side effects) | Apply function to each element           |
| `filter`    | stream or vec | stream or vec  | Keep elements matching predicate         |
| `transform` | stream or vec | stream or vec  | Apply function, collect results          |
| `count`     | stream or vec | scalar         | Count elements (consumes stream)         |
| `take N`    | stream or vec | stream or vec  | First N elements                         |
| `first`     | stream or vec | scalar         | First element (consumes one from stream) |
| `collect`   | stream        | vec            | Materialize stream into vector           |
| `lines`     | stream or str | stream         | Split into line stream                   |

### Vector-only operations (require `collect` for streams)

| Command      | Description                      |
| ------------ | -------------------------------- |
| `vec-get`    | Random access by index           |
| `vec-set`    | Set element by index             |
| `vec-slice`  | Slice by index range             |
| `vec-len`    | Length (use `count` for streams) |
| `vec-push`   | Append element                   |
| `vec-concat` | Concatenate two vectors          |

### Pipes remain value-passing

Streams don't change the pipe model. `|` still threads a single value (first-arg). A stream is a value like any other — it just hasn't computed all its elements yet.

```
# scalar value through pipe — no streaming:
+ 1 2 | * 3 | print

# stream value through pipe — lazy processing:
!find . -name "*.txt" | lines | par-each [\ process-file $it]
```

### Concurrency and streams

`par-each` pulls elements from a stream and dispatches them to workers concurrently, without loading all elements into memory first:

```
!find . -name "*.txt" | lines | par-each [\ process-file $it]
```

## Concurrency

### Primitives

| Command                | Takes                  | Returns             | Description                          |
| ---------------------- | ---------------------- | ------------------- | ------------------------------------ |
| `spawn { expr }`       | block                  | future              | Start async task                     |
| `await $f`             | future                 | resolved value      | Wait for future (suspends)           |
| `parallel { } { } ...` | N blocks               | vector of N results | Run blocks concurrently, collect all |
| `race { } { } ...`     | N blocks               | single result       | Run blocks, return first to complete |
| `par-each`             | collection/stream + fn | vector of results   | Process elements concurrently        |
| `timeout N { }`        | duration + block       | result or error     | Sugar for race + sleep               |

All compose with pipes — they're commands that return values:

```
# parallel returns a vector — pipe to each
parallel {fetch $url1} {fetch $url2} | for [\ json-parse $it]

# parallel + destructuring
def [users posts] [parallel {fetch "/users"} {fetch "/posts"}]

# race returns the winner — pipe to next stage
race {fetch $primary} {fetch $mirror} | json-parse

# timeout
timeout 5 {fetch $url} | json-parse

# par-each on a collection
$urls | par-each [\ fetch $it] | for [\ json-parse $it]

# spawn/await for manual control
def future [spawn { expensive-work $data }]
# ... do other stuff ...
await $future | process-result
```

### Error semantics

- **`parallel`:** All branches run to completion. Result vector may contain a mix of values and errors. Caller decides how to handle.
- **`race`:** First to complete wins, error or not. If the winner errored, the error propagates through the pipe.
- **`par-each`:** All elements processed. Results may contain errors (same as `parallel`).
- All errors compose with pipe short-circuiting and `catch`.

```
# parallel — handle mixed results
def results [parallel {fetch $url1} {fetch $url2} {fetch $url3}]
$results | filter [\ not [error? $it]] | for [\ json-parse $it]

# race with fallback on error
race {fetch $primary} {fetch $mirror} | catch {"default"} | json-parse
```

### Jobs

When `spawn` runs a block containing an external command, the returned future is a **Job** — a future that also wraps an OS process.

#### Future vs Job

A Future is a JACL async task. A Job is a Future with process-level capabilities:

|          | Future         | Job                                  |
| -------- | -------------- | ------------------------------------ |
| `await`  | result value   | `{str stdout, str stderr, i32 exit}` |
| `cancel` | stops the task | kills the process                    |
| `pid`    | n/a            | OS process ID                        |
| `signal` | n/a            | send OS signal                       |

```
# Future — pure JACL computation
def f [spawn { expensive-work $data }]
await $f              # → result value
cancel $f             # stops the task

# Job — wraps an OS process
def j [spawn { !python -m http.server }]
. $j pid              # → 12345
signal $j SIGTERM     # send signal
cancel $j             # kill the process
await $j              # → {stdout, stderr, exit}
```

#### `&` suffix as spawn sugar

`&` at the end of a `!cmd` is sugar for `spawn { !cmd }`, returning a Job:

```
def server [!python -m http.server &]
# equivalent to:
def server [spawn { !python -m http.server }]
```

#### Composability

Jobs compose with everything futures already compose with:

```
def [api db] [parallel { !start-api } { !start-db }]
. $api pid                    # both are Jobs
signal $db SIGTERM            # signal one
timeout 30 { await $job }    # timeout works
```

## Conditionals

```
if $cond { body }
if $cond { body } else { body }
if $cond { body } elif $cond2 { body } else { body }
```

`if` is an expression — it returns the value of the taken branch:

```
def x [if [> $n 0] { "positive" } else { "non-positive" }]
```

Composes with pipes:

```
get-status | if [== $it "ok"] { proceed } else { abort }
```

## Iteration and control flow

### `for` — unified iteration

`for` replaces `each` as the single iteration construct. Multiple forms distinguished by argument shape:

```
# C-style (mutable loop variable)
for {mut i 0; < $i 10; incr i} {
  log $i
}

# Collection + implicit binding ($it)
for $items { log $it }

# Collection + explicit binding — collection first, name second
for $items item { log $item }

# Collection + callback (HOF form)
for $items $callback
```

The explicit form is **collection-first** so that pipe threading works naturally: `$items | for item { … }` threads the piped value as the first arg, which is the collection slot. A name-first form would put the threaded value in the name slot and break the pipe idiom — a real ergonomic loss in a shell/glue language. See `SYNTAX_REDESIGN_2026_06.md` §5 for the full discussion.

Compiler distinguishes forms by argument shape:

- First arg is `{}` block → C-style loop
- First arg is a value, next is `{}` block → implicit binding (`$it`)
- First arg is a value, next is bare word, next is `{}` block → explicit binding
- First arg is a value, next is a proc/variable → HOF callback

Works on both streams and vectors. `filter` and `transform` remain separate — they produce new collections, not side effects.

`while` stays as a separate construct: `while $cond { body }`.

### `return`, `break`, `continue`

**Key principle:** `return` always exits the nearest `proc`. Blocks are not procs — they are inlined into the enclosing proc's scope.

- **`return`** — exits the nearest enclosing `proc`
- **`break`** — exits the nearest enclosing `for`/`while` loop
- **`continue`** — skips to the next iteration of the nearest `for`/`while`

```
proc find-first {items, pred} {
  for $items item {
    if [$pred $item] { return $item }    # exits find-first
  }
  $nil
}

proc process {items} {
  for $items item {
    if [skip? $item] { continue }        # next iteration
    if [done? $item] { break }           # exit for loop
    print $item
  }
}
```

### Block form vs lambda form

The distinction between blocks and lambdas determines how `return`/`break`/`continue` behave:

- **Block form** `for $items item { body }` — block is inlined, `return` exits enclosing proc, `break`/`continue` work
- **Lambda form** `for $items [\ do-thing $it]` — lambda is a separate proc, `return` exits the lambda, `break`/`continue` are errors

Both forms coexist. Use blocks when you need control flow, lambdas for concise pipelines:

```
# Block form — full control flow
for $items item {
  if [> $item 10] { return $item }
}

# Lambda form — concise in pipelines
$items | filter [\ > $it 2] | for [\ print $it]
```

## Splat / spread (`..`)

`..` is a builtin symbol (like `$` and `!`) with special handling by the parser. It works in two directions:

### Collecting (rest patterns)

In destructuring and param lists, `..` collects remaining items:

```
def [head ..rest] $v                     # destructuring
proc log {level, ..msgs} { ... }         # variadic params
```

### Spreading

In `[]` juxtaposition and `!cmd` args, `..` spreads a collection into separate arguments:

```
def args [vec "-la" "/tmp"]
!ls ..$args                              # → !ls -la /tmp

def nums [vec 1 2 3]
[+ ..$nums]                             # → [+ 1 2 3]

# Glob + splat for passing matched files to external commands
!ls ..[glob "*.txt"]                     # → !ls file1.txt file2.txt ...

# Forwarding variadic args
proc wrapper {..args} {
  !git status ..$args
}
```

`..` is not a user-definable operator — it's a parser-level construct, same category as `$` (variable reference) and `!` (external command).

## Variadic procs

`..` in param lists collects remaining arguments, consistent with destructuring syntax:

```
proc log {level, ..msgs} {
  for $msgs msg { print "[$level] $msg" }
}

log "INFO" "server started" "listening on 8080"
```

## Ranges

Ranges are named-prefix builtins:

```
[range 1 10]            # exclusive: 1, 2, 3, ..., 9
[range-inclusive 1 10]  # inclusive: 1, 2, 3, ..., 10
```

Ranges produce streams. Work with `for`, `filter`, `transform`, etc:

```
for i [range 0 10] { print $i }
[range-inclusive 1 100] | filter [\ == 0 [% $it 2]] | collect
```

Operator-prefix forms (`[..< 0 10]`) were considered but rejected for the same readability reasons that motivated the named-prefix decision: an operator token in head position reads awkwardly.

## Line continuation

Newline, `,`, and `;` are command separators in `{}` command mode and at top level. To continue a command across lines, use `\` at end of line:

```
!curl -X POST \
  -H "Content-Type: application/json" \
  -d $payload \
  $url

glob "**/*.jacl" \
  | filter [\ match-re /test/ $it] \
  | for f { run-test $f }
```

Inside `{}` blocks, `()`, and `[]`, newlines are already handled by the delimiters — no `\` needed:

```
def results [parallel
  {fetch $url1}
  {fetch $url2}
  {fetch $url3}
]
```

## Pragmas

`#{ ... }` syntax for pragmas (avoids collision with `#!/usr/bin/env jacl` shebang):

```
#!/usr/bin/env jacl
#{ path-fallback }

# script content...
!ls -la | lines | for line { print $line }
```

## Scoping

- `def` is block-scoped
- Shadowing in the same scope is a compile-time error
- Shadowing in a nested scope is allowed (new `def` in inner block)

```
def x 1
def x 2          # error: x already defined in this scope

{
  def x 2        # ok: nested scope, shadows outer x
}
```

## Optional chaining

Nil-safe field access via the `?.` builtin (prefix form):

```
[?. $val field]                  # nil if $val is nil, otherwise field access
[?. [?. $user address] city]     # chains via nesting
```

The prefix form is verbose and is the current holding pattern. Mirroring `->` as a glued suffix (`$user?.address?.city`) is the eventual target; see `SYNTAX_REDESIGN_2026_06.md` §3.4.

## Module visibility

Everything top-level is public by default. Underscore prefix marks private names — the compiler enforces this at import boundaries.

```
# shapes.jacl
struct Point {i32 x, i32 y}
proc distance {Point a, Point b} { ... }
proc _helper {x} { ... }                # private — underscore prefix

# main.jacl
use "shapes.jacl" {Point, distance}      # import specific names
use "shapes.jacl" {..}                   # import all public names
use "shapes.jacl" {_helper}              # error: _helper is private
```

No `pub`/`priv` keywords needed. The convention is the mechanism. If encapsulation needs grow later, explicit keywords can be added without breaking existing code.

## Multi-line strings

Triple-quoted strings for multi-line content:

```
def sql """
  SELECT * FROM users
  WHERE active = true
"""
```

- Interpolation works inside triple-quoted strings: `$var`, `$[expr]`
- Leading whitespace stripped based on indentation of closing `"""` (Kotlin-style)
- No delimiter name needed (unlike heredocs)

```
def query """
  SELECT * FROM $table
  WHERE id = $[+ $id 1]
"""
```

## Comments

- `#` — single-line comment (unchanged)
- `##` — doc comment convention (extractable by tooling)
- No multi-line comment syntax — stack `#` lines instead

```
# Regular comment

## Calculates the distance between two points.
## Returns f64.
proc distance {Point a, Point b} {
  ...
}
```

## Callable values

Maps and atoms are callable in head position of `[]`, extending JACL's dispatch rule: if the first item in a juxtaposition is callable, call it.

### Maps as callable (key lookup)

Like Clojure, placing a map in head position does a key lookup:

```
def colors [map "red" "#ff0000" "blue" "#0000ff"]
[$colors red]       # → "#ff0000"
[$colors blue]      # → "#0000ff"
```

### Atoms as callable (deref + delegate)

Calling an atom derefs it and delegates to the contained value. If the atom holds a map, calling the atom does deref → map → key lookup:

```
def config [atom [map "debug" $false "port" 8080]]
[$config port]      # → 8080 (deref atom → map, lookup "port")
```

## Environment variables

`$env` is an atom containing a `[map string string]` that syncs bidirectionally with the OS environment via atom listeners.

### Reading

```
[$env HOME]           # atom deref → map → key lookup → "/Users/adam"
[$env PATH]           # → "/usr/bin:..."
```

### Writing

```
# swap atomically applies a function to the atom's value
swap $env [\ map-set $it NODE_ENV "production"]
# listener fires → setenv("NODE_ENV", "production")
```

### Scoped changes

`with-env` temporarily modifies `$env`, runs a block, then restores the original. The listener syncs to the OS at each step:

```
with-env {DEBUG "1", NODE_ENV "production"} {
  !npm run build    # child process sees DEBUG=1 and NODE_ENV=production
  do-stuff          # JACL code sees them via [$env DEBUG]
}
# restored — DEBUG and NODE_ENV back to original values
```

### Built-in aliases

A small set of read-only convenience variables that stay in sync with `$env` via listeners:

| Variable | Synced from       | Description               |
| -------- | ----------------- | ------------------------- |
| `$home`  | `[$env HOME]`     | User home directory       |
| `$pwd`   | Working directory | Current working directory |
| `$pid`   | Process ID        | Current process ID        |

These update automatically. For example, `cd` updates the working directory, swaps `PWD` in `$env`, and the listener updates `$pwd`:

```
print $pwd         # /Users/adam/code
cd "src"
print $pwd         # /Users/adam/code/src
```

### Atom listeners (general-purpose mechanism)

The env sync is built on a general-purpose listener mechanism for atoms. Any atom can have watchers:

```
def counter [atom 0]
logger = proc {old, new} { print "changed: $old → $new" }

watch $counter "log" $logger
swap $counter [\ + $it 1]
# prints: changed: 0 → 1

unwatch $counter "log"
# subsequent swaps no longer fire the watcher
```

Watchers fire synchronously in the thread that committed the change, after every `reset` / `swap` mutation, with `(old, new)`. Each watcher is keyed; re-registering the same key replaces the previous closure, and `unwatch` removes it. Multiple watchers fire in registration order. The full surface (storage, threading, re-entrancy, GC) is in `ATOM_WATCH_DESIGN.md`.

The `$env` atom has a built-in listener that:

- Calls `setenv()`/`unsetenv()` for each changed key
- Updates `$home` if `HOME` changed
- Updates `$pwd` if `PWD` changed

No special-casing of `$env` in the language — it's an atom with a listener, same as any user-created atom.

## Implicit context (`$ctx`)

`$ctx` is a typed, dynamically-scoped context implicitly available in every proc. It carries ambient state like working directory and environment, and is user-extensible via module-level field declarations.

### Core semantics

- Every proc receives `$ctx` implicitly — no explicit parameter needed
- `$ctx` is **shared across regular proc calls** within a single call stack. A callee's `set $ctx.field` mutation *is* visible to the caller after the callee returns, and to any sibling callees later in the same scope. This is true dynamic scoping with no implicit scope boundary at proc-call sites.
- A **fresh fork of `$ctx` is created only at explicit sites**: `with-ctx { ... }`, `spawn { ... }`, `parallel { ... } { ... }`, `race { ... } { ... }`. Mutations inside one of those scopes do not leak out when the scope ends.
- `with-ctx` is the primitive for scoped mutations. Scoped commands like `cd` are expected to be macros that desugar to `with-ctx`, so state changes that should be local become syntactically visible (`cd "build" { ... }`) rather than relying on a hidden per-call fork.
- An earlier design forked at every proc call. It was abandoned for performance. May be revisited.

### Built-in fields

| Field | Type            | Mutable | Description                                               |
| ----- | --------------- | ------- | --------------------------------------------------------- |
| `pwd` | `str`           | yes     | Working directory                                         |
| `env` | `[map str str]` | yes     | Environment variables (relationship with `$env` atom TBD) |

### Dynamic scoping

Mutations within a scope are visible to all callees and subsequent
code in that scope, including across proc-call boundaries:

```
proc setup-paths {} {
  set $ctx.log-level 2      # mutates the shared $ctx
}
setup-paths
print $ctx.log-level         # → 2 — change persists after return
```

To scope a mutation, use `with-ctx` (or a block-scoped macro like
`cd "build" { ... }` that desugars to `with-ctx`):

```
with-ctx {pwd "build"} {
  !make                # runs in build/, sees the scoped pwd
}
# $ctx.pwd restored — the with-ctx fork was discarded on block exit
```

This is a deliberate trade-off. Fork-on-every-call would scope
mutations automatically but pays a fork cost at every call site;
fork-at-explicit-sites makes the scoping syntactically visible and
keeps proc calls cheap. Users who want the auto-scoping discipline
should write `cd`-style block macros for state changes they want
contained.

### Forking context

`with-ctx` creates a fork with changed fields. The fork is discarded when the block exits:

```
with-ctx {pwd [path-join $ctx.pwd "src"]} {
  glob "*.txt"        # globs in src/
  !make               # runs in src/
}
# $ctx.pwd restored
```

`with-dir` and `with-env` are sugar for common `with-ctx` patterns:

```
with-dir "src" { ... }
# desugars to:
with-ctx {pwd [path-join $ctx.pwd "src"]} { ... }

with-env {DEBUG "1"} { ... }
# desugars to:
with-ctx {env [map-set $ctx.env DEBUG "1"]} { ... }
```

### User-extensible fields

Modules can declare additional typed fields on `$ctx` at the top level. Only declared fields can be read or written — the compiler enforces this. Mutability is per-field (`mut` opt-in, immutable by default).

```
ctx mut Connection db-conn              # mutable, no default — runtime error if read while unset
ctx mut i32 log-level = 0               # mutable, has default — always safe to read
ctx str app-name                        # immutable, set via with-ctx only
```

Field declarations follow the existing type-before-name pattern.

### Field conflicts

Two imported modules declaring the same `$ctx` field name with different types is a compile error — same as name conflicts with `use {..}`. Resolve by wrapping one module's usage behind a differently-named field.

### Example: database connection

```
# db.jacl
ctx mut Connection db-conn

proc with-db {Connection conn, block} {
  with-ctx {db-conn $conn} $block
}

proc query {str sql} {
  _execute $ctx.db-conn $sql    # reads from ctx
}
```

```
use "db.jacl" {with-db, query}

with-db [connect "postgres://..."] {
  query "SELECT * FROM users"
}
```

### Example: scoped query builder

`$ctx` enables implicit scoped state without threading values through every call:

```
# qb.jacl
ctx mut QueryBuilder qb

proc with-query {block} {
  with-ctx {qb [QueryBuilder]} $block
}

proc select {..cols} {
  set $ctx.qb [qb-add-select $ctx.qb $cols]
}

proc where {str clause} {
  set $ctx.qb [qb-add-where $ctx.qb $clause]
}

proc build {} { qb-to-sql $ctx.qb }
```

```
use "qb.jacl" {..}

with-query {
  select "name" "age"
  where "age > 21"
  build
}
# → "SELECT name, age WHERE age > 21"
```

### Closures and `$ctx`

Lambdas and proc calls resolve `$ctx` at **call time** (true dynamic scoping). A stored callback sees the caller's context, not the context from when it was defined:

```
def cb [\ query "SELECT 1"]
with-db $other-conn {
  [$cb]    # uses $other-conn — the current scope's db-conn
}
```

Concurrency primitives (`spawn`, `parallel`, `race`) snapshot `$ctx` at the spawn site, since the spawned task runs independently of the caller's stack:

```
with-ctx {pwd "src"} {
  def f [spawn {
    # sees $ctx.pwd = ".../src" (snapshot taken at spawn site)
    with-ctx {pwd "lib"} {
      !make             # runs in lib/, only inside this task's scope
    }
    # spawned task's outer $ctx.pwd = ".../src" again
  }]
  # ... parent $ctx.pwd here = ".../src"
}
# back to original pwd — with-ctx scope ended
```

### Performance

Regular proc calls have **zero `$ctx` overhead** — caller and callee share the same `$ctx` record, so there's no allocation or copy at call sites.

The explicit fork sites (`with-ctx`, `spawn`, `parallel`, `race`) each pay one fork cost on entry:

- A pooled `HeapRecord` slot is taken from the per-VM `ctx_pool` (O(1)).
- The parent's `$ctx` payload is eager-`memcpy`'d into the fresh slot (one struct-sized copy — the ctx type's `total_size`).
- On scope exit, the slot is returned to the pool.

Pool sizing keeps `with-ctx` and concurrency forks cheap in practice. There is currently no copy-on-write or static "ctx-pure" analysis — both have been considered but neither is implemented. The current model is simpler to reason about and the fork sites are infrequent enough that the eager-copy cost hasn't surfaced as a bottleneck.

## Error handling

- Errors are values (existing JACL design)
- **Pipes short-circuit on error by default** — if a stage produces an error, subsequent stages don't run
- **`catch` as a pipe stage** for inline recovery:
  ```
  read-file $path | catch {"fallback"} | split "\n"
  read-file $path | catch {err} { print "warning: $err"; "fallback" } | split "\n"
  ```
- **`try`/`catch` as block-level form** for non-pipeline code:
  ```
  try { risky-operation } err { print "failed: $err" }
  ```
- External commands: non-zero exit code → error value (see Shell interop above)

## Operators as macros

Symbolic operators live in `{}` command mode and connect commands in a shell-pipeline style. They are macros — they receive unevaluated AST and produce transformed AST. The parser treats all operators uniformly; semantics are defined by the macro system.

Arithmetic, comparison, and logical operations are written in `[]` prefix form (`[+ 1 2]`, `[== $a 7]`, `[&& $p $q]`). There is no infix mode.

### Command-mode operators (`{}`)

In `{}`, symbolic operators work between commands (not between individual values):

```
foo a | bar b           # pipe: [bar [foo a] b]
foo a && bar b          # and-then: run bar only if foo succeeded
foo a || bar b          # or-else: run bar only if foo failed
```

No operator precedence in `{}` — left to right.

### User-definable operators

Operators are a kind of macro. Users can define new operators using the same mechanism the built-in operators use.

- **AST representation**: macros receive and return **syntax objects** — a compile-time value type wrapping kind + datum + source position + scope marks. Quasiquoting (`syntax-quote`, `~`, `~@`) is the primary interface; introspection/construction APIs are available for advanced macros. See DESIGN.md M15 for full details.
- **Hygiene**: hygienic by default — scope marks prevent macro-introduced names from colliding with caller names. Anaphoric macros use `^` prefix to intentionally introduce names into the caller's scope. `gensym` available for guaranteed-unique temporaries.

### Prelude

A prelude module is implicitly imported and pre-defines the core operators:

- `|`, `&&`, `||` — command-mode control flow
- `\` — lambda shorthand
- Arithmetic, comparison, and logical operators as prefix builtins (`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `not`)
- `assert` — prelude macro covering both `assert <expr>` (truthy check) and `[assert <op> arg1 arg2 …]` (predicate form) — both expand to `[if <pred> {} { panic "<source>" }]`
- `panic` — builtin: halt unconditionally with an optional string message; underlying primitive for `assert` on failure

## Aliases

Aliases are macros that rewrite call sites. The alias body is spliced in at the call site, and any trailing arguments from the caller are appended.

### External command aliases

```
alias ll { !ls -la }
alias gs { !git status }
alias k { !kubectl }

# Usage        → Expands to
ll -h /tmp       !ls -la -h /tmp
gs -s            !git status -s
k get pods       !kubectl get pods
```

### JACL command aliases

Aliases work for JACL commands too — they're syntactic rewrites, not shell-specific:

```
alias pj { json-parse }

fetch $url | pj       # → fetch $url | json-parse
```

### Aliases vs procs

Aliases are compile-time rewrites. Procs are runtime values. Use aliases for simple abbreviations; use procs when you need logic, bindings, or control flow:

```
# Alias — simple rewrite, args appended
alias gs { !git status }

# Proc — logic, conditional behavior
proc deploy {env, ..flags} {
  !docker build -t myapp .
  !kubectl apply -f "deploy/$env.yaml"
}
```

## Typed collections

Concrete type parameterization for collections using `[]` in type position. Not full generics — no type variables, no inference, no constraints. Just "this vec holds Points."

### Syntax

Extends the existing typed-def pattern (`def TYPE name value`):

```
# Typed vectors
def [vec Point] points [vec [Point 1 2] [Point 3 4]]
def [vec i64] numbers [vec 1 2 3 4 5]

# Typed maps
def [map str Point] lookup [map "origin" [Point 0 0]]
def [map str [vec Point]] groups [map "a" $points1 "b" $points2]

# Nested
def [vec [vec i64]] matrix [vec [vec 1 2] [vec 3 4]]
```

`[vec Point]` in type position is type parameterization — `[]` juxtaposition where the compiler reads items as types. Same principle as destructuring: position determines meaning.

### In struct fields

```
struct Polygon {[vec Point] vertices, str name}
struct Graph {[map str [vec str]] edges}
```

### In proc signatures

```
proc [vec Point] get-points {} {
  vec [Point 1 2] [Point 3 4]
}

proc closest {[vec Point] points, Point target} { ... }
```

### Type checking

```
def [vec i64] nums [vec 1 2 3]
vec-push $nums "hello"     # compile error: expected i64, got str
vec-push $nums 42           # ok
```

### Gradual typing still holds

```
# Typed: compiler checks element types
def [vec i64] nums [vec 1 2 3]

# Untyped: holds anything, no checking
def stuff [vec 1 "hello" $true]

# Generic proc: just don't annotate
proc first {v} { vec-get $v 0 }    # works on any vec
```

### Implementation path

- Phase 1: Compile-time type checking only (elements stored as `dyn` at runtime)
- Phase 2: Unboxed storage for typed collections (contiguous memory layout, no tag overhead)

The syntax is the same for both phases. Full parametric generics (type variables, constraints) remain a future possibility if needed.

## Fixed-size C-ABI buffers (`[Buf N T]`)

Compile-time-sized contiguous array of `T` — the FFI / "I need an
`int[10]`" case. Sits between `[Ptr T]` (one element) and `[Vec T]`
(GC-owned, growable). Full reference: `BUFFER_DESIGN.md`.

```
def [Buf 256 u8] scratch                                ; zero-init local
def [Buf 4 u8] magic [[Buf 4 u8] 0x7f 0x45 0x4c 0x46]   ; literal init
print $magic->0                                          ; 127 (arrow indexing)
print [buf-get $magic 1]                                 ; 69
[buf-set $magic 0 0xff]                                  ; bounds-checked write
print [buf-len $magic]                                   ; 4 (compile-time fold)

[buf-unchecked-get $magic $i]                            ; escape hatch — no bounds check
[buf-unchecked-set $magic $i $v]

struct ElfHeader { i32 version, [Buf 4 u8] magic }       ; buf as struct field
extern i32 read {i32 fd [Ptr u8] dst u64 len}
[read 0 $scratch 256]                                    ; implicit [Buf]→[Ptr] decay
```

`N` is a positive integer literal; `T` is any scalar
(`i8`/`u8`/`i16`/`u16`/`i32`/`u32`/`i64`/`u64`/`f32`/`f64`/`bool`),
`[Ptr U]`, or value-type struct. `[Buf N T]` decays to `[Ptr T]` at
extern and JACL proc call sites when the pointee matches.

## Open design questions

### Resolved in this document

- Two-mode delimiter system — `[]` prefix expressions, `{}` command mode (shell-pipeline). The earlier `()` infix mode was cut; see `SYNTAX_REDESIGN_2026_06.md`.
- Pipe threading (first-arg)
- Destructuring with `{..}`
- Match/case with guards
- Generics (skipped)
- Shell interop — `!cmd` simple form, `exec` full control, `$PATH` fallback modes
- Error handling — short-circuit pipes, `catch` pipe stage, `try`/`catch` block form
- Streams — lazy sequence type, explicit `collect`, sequence ops work on both streams and vectors
- Concurrency — `spawn`/`await`, `parallel`, `race`, `par-each`, `timeout`; all compose with pipes
- Callable values — maps (key lookup) and atoms (deref + delegate) are callable in `[]` head position
- Environment variables — `$env` is an atom of a map, synced to OS via listeners; `[$env HOME]` for access; `with-env` for scoped changes; `$home`/`$pwd`/`$pid` as synced aliases
- Atom listeners — `watch` adds watchers to any atom; env sync is one application
- I/O redirection — commands (`write-file`, `append-file`), not operators; string/stream piped to `!cmd` feeds stdin
- Aliases — compile-time macros that rewrite call sites with arg appending
- Implicit context (`$ctx`) — typed, dynamically-scoped, user-extensible; `with-ctx` / `spawn` / `parallel` / `race` are the only fork sites; regular proc calls share `$ctx` with the caller and a callee's `set $ctx.field` propagates back
- `$ctx` user-extensible — modules declare typed fields at top level; compiler enforces field existence and type
- `$ctx` field conflicts — compile error, same as name conflicts with `use {..}`
- `$ctx` default values — three forms: no default (runtime error if unset), default value, optional type (nil if unset)
- `$ctx` closures — resolve at call time (true dynamic scoping); `spawn` snapshots at spawn time
- `$ctx` performance — regular proc calls have zero ctx overhead (no fork); explicit fork sites eager-`memcpy` a pooled `HeapRecord` slot on entry. Copy-on-write and a static "ctx-pure" pass were considered but not implemented.
- Globbing — explicit `glob` command, reads `$ctx.pwd`, returns stream; no implicit expansion anywhere
- Conditionals — `if $cond {} elif $cond2 {} else {}`, is an expression, composes with pipes
- `for` replaces `each` — `for` is the single iteration construct, `each` removed
- Splat/spread (`..`) — builtin parser symbol, spreads collections into args in `[]` and `!cmd`; not a user-definable operator
- Line continuation — `\` at end of line; newline/`,`/`;` are command separators

### Needs design work

Open design questions live in **`NOT_IMPLEMENTED.md` §6** (syntax-level
open questions). Question numbers below are stable — they're
referenced from `NOT_IMPLEMENTED.md` and elsewhere — so resolved items
stay listed as historical record.

1. ~~**String interpolation**~~ — resolved, see String interpolation section.
2. **Boolean/logical operators** — see `NOT_IMPLEMENTED.md` §6.
3. ~~**Early return**~~ — resolved: `return` exits nearest `proc`, blocks are inlined.
4. ~~**Loops**~~ — resolved: `for` replaces `each`, `while` stays, `break`/`continue` work in block forms.
5. ~~**Module visibility**~~ — resolved: everything top-level is public, underscore prefix is private (compiler-enforced).
6. ~~**Multi-line strings**~~ — resolved: triple-quoted strings with interpolation, Kotlin-style whitespace stripping.
7. ~~**Comments**~~ — resolved: `#` single-line, `##` doc comments, no multi-line syntax.
8. ~~**Variadic procs**~~ — resolved: `..` in param lists, consistent with destructuring.
9. ~~**Pragmas**~~ — resolved: `#{ ... }` syntax, avoids shebang collision.
10. ~~**Ranges**~~ — resolved: `[range 1 10]` exclusive, `[range-inclusive 1 10]` inclusive, produce streams. (Originally infix `(1 ..< 10)`; revised June 2026.)
11. ~~**Scoping**~~ — resolved: same-scope shadowing is compile error, nested scope shadowing is fine.
12. ~~**Optional chaining**~~ — resolved (holding pattern): `[?. $val field]` prefix form. Glued-suffix mirror of `->` is the eventual target; see `SYNTAX_REDESIGN_2026_06.md` §3.4.
13. ~~**Field access syntax**~~ — resolved: `->` only. `$user->name`, chained `$a->b->c`. `.` was rejected because bare words are strings (filenames/paths use `.` heavily). Implemented in lexer/parser; desugars to `[. expr field]`.
14. **Regular expressions** — see `NOT_IMPLEMENTED.md` §1, §2, §6.
15. **Operator overloading** — see `NOT_IMPLEMENTED.md` §6.
16. **Boolean/logical operators** — see `NOT_IMPLEMENTED.md` §6 (duplicate of Q2).
17. **Standard library surface** — see `NOT_IMPLEMENTED.md` §6.
18. ~~**Job detection**~~ — resolved: static. The compiler decides at the `!cmd` call site whether to emit `OP_EXEC` with `EXEC_FLAG_BG` (Job) or basic mode (foreground stream).
19. **`cancel` semantics** — see `NOT_IMPLEMENTED.md` §6. Current impl: `[cancel $job]` desugars to `[signal $job SIGTERM]` — SIGTERM only, no escalation; cooperative cancel on plain Future is open.
20. ~~**`&` syntax**~~ — resolved: `&` is parser-recognized only on `!cmd` heads (`shell_cmd.background` AST flag), compiles to `OP_EXEC | EXEC_FLAG_BG`. Does **not** apply to arbitrary JACL procs — `expensive-work &` is not a valid background form; use `[spawn { ... }]` for JACL-side concurrency.
21. **Alias scoping** — see `NOT_IMPLEMENTED.md` §6.
22. ~~**Splat into `!cmd`**~~ — resolved: `..` is a builtin parser symbol, `!cmd ..$args` spreads into separate args.
23. ~~**`signal` on plain Future**~~ — resolved: runtime error. `OP_SIGNAL` requires the operand to be a Job map (checks `_is_job` marker); anything else returns `"signal requires a Job map"`.
24. **`$ctx` vs `$env` relationship** — see `NOT_IMPLEMENTED.md` §6.

## Implementation status

Features from this document compared against the current codebase. Last updated: 2026-06-02.

The June 2026 syntax redesign (see `SYNTAX_REDESIGN_2026_06.md`) revised several decisions. The implementation has not yet caught up to the revised spec for those items; they are marked **(spec ahead of impl)** below.

### Implemented

| Feature | Notes |
|---------|-------|
| Three-mode delimiter system (`[]`, `{}`, `()`) | **(spec ahead of impl)** spec is now two modes: `[]` prefix + `{}` command. `()` infix is currently implemented and pending removal. |
| Infix mode — no precedence, left-to-right | **(spec ahead of impl)** pending removal. |
| Pipe threading (`\|` first-arg in command mode) | yes |
| Binding operators (`=`, `:`, `::`) | **(spec ahead of impl)** removed from spec; prelude macros currently still in place. |
| Arrow field access (`->`) | chained |
| `proc` syntax (`{params} {body}`) | yes |
| `proc` typed-return position — after params: `proc f {…} T {…}` | **(spec ahead of impl)** current form is `proc T f {…} {…}`. |
| `struct` declaration (`struct Name {type field, ...}`) | C-ABI layout |
| Struct constructor — named-only (`[Pt x 30 y 15]`) | **(spec ahead of impl)** current constructor is positional (`[Pt 30 15]`); printer uses a third form. |
| `if`/`elif`/`else` | expression-valued |
| `while` loops | yes |
| `for` — all 4 forms | implicit `for COLL {body}`, explicit `for COLL NAME {body}` (collection-first; see `SYNTAX_REDESIGN_2026_06.md` §5 for why this didn't flip to name-first), C-style `for {init;cond;step} {body}`, HOF `for COLL $cb`. |
| `break`, `continue`, `return` | block-inlined; lambda-separate |
| Lambda shorthand (`[\  ]` with `$it`) | prelude macro |
| `incr name` — sugar for `set name [+ $name 1]` | prelude macro |
| String interpolation (`$var`, `$[...]`) | **(spec ahead of impl)** `$(...)` form still accepted by the implementation; pending removal alongside `()`. |
| Line continuation (`\` at end of line) | yes |
| Comments (`#`, `##`) | yes |
| Error handling (error values, pipe short-circuit, `try`/`catch`) | yes |
| `panic` builtin | `OP_PANIC`: pops a string message, halts with that message. Zero-arg form emits the default literal `"panic"`. |
| `assert` prelude macro | Two forms: `assert <expr>` (truthy check) and `[assert <op> arg1 arg2 …]` (predicate form). Both expand to `[if <pred> {} { panic "<source>" }]`. Bare command-mode `assert <op> arg1 arg2 …` is currently blocked by the parser treating operators as command-arg terminators; use the bracketed form. |
| Persistent collections (vectors, maps) | RRB-tree, HAMT |
| Mutable state (`box`, `atom`, `swap`, `reset`) | thread-local boxes, CAS atoms |
| Concurrency (`spawn`, `await`, `parallel`, `race`) | CPS + SM transform; NxM scheduler |
| Static type system (gradual typing) | see `TYPE_SYSTEM.md` |
| GC | epoch-based tracing |
| `filter`, `transform` (HOF) | eager on vectors |
| Destructuring (`[pos]`, `{named}`, `{..}`, `..rest`, `_`) | yes |
| Splat/spread (`..`) | destructure + variadic macros + spread |
| Streams (lazy sequences) | `lines`, `stream_next`, `collect` |
| Macro system (`defmacro`) | AST quasiquote, hygienic, `gensym` |
| Operators as macros | in prelude.jacl |
| Variadic macros | `..rest` in macro params |
| Module system | both `use` forms; cross-module typing |
| `interpret` (sandboxed eval) | capability-based |
| Implicit context (`$ctx`) | typed, dynamically-scoped, user-extensible; `with-ctx` |
| Typed collections (`[Vec T]`, `[Map K V]`) | see `TYPE_SYSTEM.md` |
| Fixed-size C-ABI buffers (`[Buf N T]`) | see `BUFFER_DESIGN.md`; nested bufs + GC-traced elements deferred |
| Variadic procs (`proc log {level, ..msgs}`) | `..rest` in proc params |
| Ranges — `[range a b]`, `[range-inclusive a b]` | both prefix forms emit `OP_RANGE`. The infix `(a ..< b)` / `(a ..= b)` forms still compile to the same opcode for now; they go away when `()` is removed. |
| Multi-line strings (`"""..."""`) | with interpolation, Kotlin-style indent |
| Optional chaining (`?.`) | **(spec ahead of impl)** spec is `[?. $val field]` prefix (holding pattern); current implementation is `($val ?. field)` in `()` mode. |
| Negative numeric literals (`-1`) | **(spec ahead of impl)** lexer currently requires `(-1)` workaround in argument position; spec is to accept `-1` directly. |
| Module visibility (`_` prefix = private) | compiler-enforced at import |
| Pragmas (`#{ ... }`) | yes |
| Same-scope shadowing error | compile-time |
| Shell interop (`!cmd`, `exec`) | `OP_EXEC` with FULL/STDIN/BG/PIPE flags; OS pipes, stdin/stdout |
| Jobs (Future + OS process) | `exec` BG mode + `signal`/`cancel`; map carries `pid`. `&` sugar + cancel semantics still design-open |

### Not yet implemented

See **`NOT_IMPLEMENTED.md` §1** for the canonical table.
