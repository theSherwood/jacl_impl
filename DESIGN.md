# JACL Design Document

Just A Command Lisp — a fusion of a command language and a lisp.
Love child of Tcl and Clojure.

## 1. Execution Model

**Decision: Bytecode VM**

- Parse source to AST, compile to bytecode, execute on a virtual machine
- AOT compilation to bytecode files planned as a future milestone
- No JIT initially (could be added later as an optimization layer)
- Rationale: tree-walking too slow for concurrency/FFI/embedding goals; JIT too complex for initial implementation

## 2. Value Representation

**Decision: Hybrid — unboxed in typed contexts, boxed in dynamic contexts**

Unboxed representation is a day-1 requirement, not a later optimization. The bytecode
compiler must distinguish between statically-typed (unboxed) and dynamically-typed (boxed)
contexts from the start.

### Boxed Values (Dynamic Context)

64-bit tagged values. Top byte is the tag, bottom 56 bits are the payload.

```
 63    56 55                                         0
┌────────┬──────────────────────────────────────────────┐
│  tag   │                  payload (56 bits)           │
└────────┴──────────────────────────────────────────────┘
```

**Tag byte layout (8 bits):**

```
 7   6   5   4   3   2   1   0
┌───┬───┬───┬───┬───┬───┬───┬───┐
│ T │ S │ E │        type (5)   │
└───┴───┴───┴───┴───┴───┴───┴───┘
```

- Bit 7: `tainted` — value came from untrusted input
- Bit 6: `secret` — value contains sensitive data
- Bit 5: `error` — value carries error state
- Bits 4-0: type tag (32 possible types)

**Payload (56 bits):**

- Pointers: 56 bits (sufficient for current x86-64/ARM64 address spaces)
- i32: 32 bits inline (fits in 56-bit payload)
- f32: 32 bits inline (fits in 56-bit payload)
- i64/f64: do NOT fit — require static typing (unboxed) or heap allocation
- Booleans, nil, etc.: encoded directly in payload

### Type Tags (5 bits — up to 32 types)

Allocation TBD. Candidates:

- `NIL`
- `BOOL`
- `I32`
- `F32`
- `STRING` (pointer to heap)
- `VECTOR` (pointer to RRB vec)
- `MAP` (pointer to HAMT)
- `CLOSURE`
- `BIGNUM` (pointer to heap)
- ... (room for more)

### Unboxed Values (Static/Typed Context)

When the type is statically known (e.g. `i64 x = 5`), the value is stored as the
raw native type with no tag overhead. This means:

- `i64`, `f64`, `u64` etc. are available but only in statically typed contexts
- The bytecode compiler emits different instructions for boxed vs unboxed operations
- Unboxed values cannot carry tainted/secret/error flags (these are dynamic properties)

### Design Notes

- The tainted/secret/error flags flow through computations automatically at the
  value representation level, not as a separate error-handling or taint-tracking mechanism
- i32/f32 is a fair tradeoff for dynamic values — users needing full-width numerics use static typing, which also gives them better performance
- This representation is very compact (single 64-bit word) and cache-friendly

## 3. Syntax

**Status: Partially decided — phased approach**

### Phase 1: Core Bracket Syntax

Start with a minimal, unambiguous bracket-delimited command syntax to get the VM and
runtime working. This is the "lisp under the hood" made explicit.

```
# Everything is a command invocation
[print hello world]
[def foo 3]
[def bar [+ 1 2]]

# At top level, brackets may be omitted
print hello world
def foo 3

# Proc definition — macros/builtins can take code blocks {} as final arg
proc add [x y] {
  [+ $x $y]
}

# Control flow
if [> $x 5] {
  print big
} {
  print small
}
```

### Phase 2: Syntactic Sugar (Later)

Once the runtime is stable, add operator syntax, assignment syntax, etc. as a
desugaring layer that compiles to the same AST:

```
foo = 3               # desugars to [def foo 3]
bar : 4               # desugars to [mut bar 4]
bar :: 5              # desugars to [set! bar 5]
i64 baz = 13          # desugars to [def baz 13] with type annotation
result = $x + $y * $z # desugars to [def result [+ $x [* $y $z]]]
```

### Decided

- **Comments:** `# line comment`
- **Variable read:** `$var` (required everywhere, including in commands)
- **Subcommands:** `[cmd arg1 arg2]` — brackets delimit command invocations
- **Top-level:** brackets may be omitted for commands
- **Code blocks:** `{ ... }` — not runtime values, but macros/builtins can accept them as arguments
- **String interpolation:** `"text $var text $[expr]"`
- **Bare words are strings/symbols:** `print hello` passes the string "hello" to print
- **Numbers are literal:** `42`, `3.14` are numeric values, not strings
- **Booleans & nil:** not special syntax — accessed as pre-defined variables `$true`, `$false`, `$nil` (or `$t`, `$f` — TBD)
- **Operators:** composed entirely of non-letter, non-number, non-space, non-control ASCII
  characters. Nothing else is treated as an infix operator.
- **Parentheses `()`:** used for grouping in expression contexts

### Strings

Three-tier representation:

- **Inline (0-7 bytes):** packed directly into the 56-bit payload of a tagged value.
  Zero allocation. Covers most command names, flags, short identifiers.
- **Heap interned (up to threshold):** deduplicated, cheap pointer-equality comparison.
- **Large strings:** likely rope-based (rope module exists).

Implementation detail TBD: how inline strings encode their length.

### Open Questions

- Operator precedence rules
- Data literal syntax (vectors, maps, sets)
- Pattern matching syntax
- Lambda / anonymous function syntax
- Control flow syntax details (if/cond/match/loop)
- Exact set of operator characters
- `$true/$false/$nil` vs `$t/$f/$nil` vs something else

## 4. Concurrency Model

**Decision: Direct-style with compiler-driven CPS transform, NxM work-stealing scheduler**

### Programming Model

Users write direct-style code. The compiler tracks async (or potentially async) expressions
in the type system and performs a CPS transform to split procedures at suspension points
into continuations. The scheduler automatically ensures execution order is respected.

```
# Direct style — compiler handles suspension points
proc fetch-both [url1 url2] {
  parallel {
    a = [http-get $url1]
  } {
    b = [http-get $url2]
  }
  # continuation runs after both complete
  [merge $a $b]
}
```

### Concurrency Primitives

- **`parallel`** — run multiple tasks concurrently; continuation runs after ALL complete
- **`race`** — run multiple tasks concurrently; continuation runs after FIRST completes
- **`spawn`** — send a task to the background, returns a handle
- **`await`** — suspend on the return value of a `spawn` handle

### Thread Safety

The type system tracks tasks that mutate values which are not thread-safe. Such tasks
are placed on a thread's **private queue** so they cannot be stolen by another thread.
Immutable values (persistent data structures) can be shared freely across threads with
zero synchronization.

### Shared Mutable State: Atoms

**Single primitive: atom** — a CAS-based container holding an immutable value.

- Updated via `swap!` which atomically applies a transform function
- Holds dynamic values (any type)
- Since contained values are immutable/persistent, CAS is always safe
- Replaces Clojure's three primitives (atom/ref/agent):
  - Agents unnecessary — `[spawn { [swap! $a $f] }]` covers async mutation
  - STM/Refs unnecessary — coordinated state goes in a single atom holding a map

### Infrastructure (Existing)

- Chase-Lev work-stealing deque for NxM scheduler
- Platform atomics and threading abstractions
- Persistent data structures (RRB vector, HAMT) enable zero-cost sharing

### Open Questions

- How do tainted/secret flags interact with cross-thread communication?
- Channel primitives? (may not be needed given parallel/race/spawn/await)
- Cancellation semantics for `race` losers and spawned tasks
- Backpressure / bounded concurrency

## 5. GC Strategy

**Decision: Epoch-based tracing GC, no RC at value level**

No compaction. No moving. No stop-the-world. RC module (rc.h) remains available as a
C-level implementation tool but is NOT used for JACL value lifetime management.

### Epoch System

- Global epoch counter, monotonically increasing
- Each thread tracks its current epoch (updated when starting a new task)
- All allocations are stamped with the epoch at the time of the task's start
- No synchronization needed for epoch reads — monotonically increasing guarantees safety

### GC Cycle

1. **Waterline:** GC takes the minimum epoch across all threads
2. **Mark:** Roots are currently running and scheduled tasks. Trace all reachable objects.
3. **Sweep:** Collect anything unmarked AND below the waterline epoch

The waterline ensures newly allocated objects are implicitly protected. The GC is
conservative — objects survive longer than strictly necessary, but synchronization
costs are minimal.

### Write Barriers on Mutable Containers

**Required** for atoms and mutable collections. Without write barriers, a concurrent
mark phase can produce use-after-free:

1. GC scans mutable container C (marks it "done"/black)
2. Thread mutates C to point to object X
3. Thread removes X from container D (not yet scanned)
4. GC scans D, never sees X
5. X is still reachable (through C) but unmarked → collected → dangling pointer

The write barrier logs new references added to already-scanned containers so the GC
re-examines them before sweeping. Only needed on mutable containers — immutable values
have fixed reference graphs and are always safe to scan concurrently.

### Safe Points

CPS-transformed suspension points provide natural safe points for root scanning. Each
thread can snapshot its roots at a safe point without stopping other threads.

### Per-Thread Heaps

Each thread manages its own heap. Heaps can contain shared (immutable) data referenced
by other threads. The GC runs as a task on one thread but traces across all heaps.

### Design Notes

- Atomic RC was rejected due to overhead — persistent data structures share structure
  heavily, resulting in excessive atomic inc/dec operations
- Extending the tracing GC to all heap values simplifies the runtime (one system)
- Tradeoff: dead objects linger until next GC cycle instead of dying immediately
- The epoch waterline makes the GC conservative but minimizes synchronization

### Open Questions

- Exact write barrier implementation (card table? remembered set? per-thread buffer?)
- GC scheduling heuristics (when to trigger a cycle)
- Memory pressure / OOM handling
- Finalization (destructors for FFI resources, file handles, etc.)

## 6. Type System

**Decision: Gradual, strict, local inference only**

Once something is statically typed, the type is enforced all the way through until
explicitly converted to dynamic. Conversions between static and dynamic are always explicit.

### Base Types

**Dynamic (boxed, 64-bit tagged):**
`nil`, `bool`, `i32`, `f32`, `string`, `vector`, `map`, `function`, `atom`, `bignum`, `rational`

**Static (unboxed):**
`i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`,
plus user-defined structs

### Type Conversions

Conversions between typed and dynamic contexts are always explicit and fallible:

```
# static → dynamic
i64 x = 42
y = [to dyn $x]         # explicit, can fail (e.g. i64 too large for dynamic i32)

# dynamic → static
z = 42                   # dynamic i32
i64 w = try {to i64 $z} {id 0}   # runtime-checked cast, fallback on error
```

### Type Inference

Local only — within a function body. No bidirectional or global inference.

### Structs

Value types (copied, not referenced). Can be boxed for GC-safe heap storage.
Users can also obtain unsafe pointers to structs. Unboxed structs may NOT be
used in dynamic contexts.

```
struct Point {
  f64 x
  f64 y
}
```

When boxed, the box holds runtime type data about the struct.

### Sum Types (Variants)

```
variant Shape {
  Circle { f64 radius }
  Rect { f64 w, f64 h }
  Point
}
```

### Mutable Containers

- **`[box X]`** — thread-local mutable container holding 1 inline item. Tasks that
  mutate boxes are pinned to their thread (cannot be work-stolen).
- **`[atom X]`** — shared/CAS-based mutable container holding 1 item. Thread-safe,
  updated via `swap!`.

### Generics

Planned. Details TBD.

### Open Questions

- Generic syntax and constraints
- Trait/protocol/interface system?
- Numeric tower details (promotion rules, dynamic i32 ↔ bignum)
- How variant types interact with pattern matching
- What runtime type data a boxed struct carries
- Can variants be generic? (e.g. `variant Option[T] { Some { T val } None }`)
- Exact rules for when `[to dyn $x]` can fail

## 7. Error Handling

**Decision: No exceptions. Error flag on values with implicit propagation.**

### Error Flag

Bit 5 of the tag byte. When set, the 56-bit payload contains the error value
(which can be anything — a string, a map, a boxed struct, etc.).

### Creating Errors

```
[error "something went wrong"]
[error [map :code 404 :msg "not found"]]
[error [box $some-struct]]
```

### Implicit Propagation

Error-flagged values propagate automatically through operations. If `$x` carries
the error flag, `[+ $x 1]` immediately returns the error without computing —
analogous to NaN propagation in IEEE 754 floats.

**Procedures that can return an error must be marked as such.** This means all
procedures that can error necessarily return a dynamic value (since the error flag
lives in the tag byte of boxed values).

### Handling Errors

`try` evaluates a block and uses a fallback if the result carries the error flag:

```
result = try {to i64 $z} {id 0}
```

### Design Notes

- Errors are first-class values, not a separate control flow mechanism
- The error flag makes error checking essentially free (just a bit test)
- Implicit propagation means errors can't be silently ignored — they infect everything
  they touch until explicitly handled with `try`
- Procedures that never error can return unboxed static types; procedures that might
  error are forced into dynamic return types by the error flag requirement

### Open Questions

- Structured error matching/switching (matching on error codes/types)
- Stack traces — are they attached automatically? Opt-in?
- Error chaining (wrapping an error with additional context)
- Syntax for marking a procedure as error-capable

## 8. Module / Namespace System

**Decision: File-based implicit modules, everything public, sandboxing via module restriction**

### Modules

Files are implicit modules. Everything in a module is public.

### Imports

```
use math              # import whole module
use math [sin cos]    # import specific items
```

Possible future direction: uniform destructuring syntax
`{sin cos} = import math` — punted for now.

### Sandboxing

Modules are typed. Builtins are implicit module imports. When invoking the interpreter
on untrusted code, you restrict which modules (and which items within modules) are
available:

```
result = interpret $src {math: {sin cos}}
```

If the source tries to `use math`, only `sin` and `cos` are available. Builtins
can be restricted or shadowed the same way.

### Open Questions

- Module search path / resolution rules
- Circular import handling
- Map and vector literal syntax (needed for the sandboxing restriction maps)
- Versioning / dependency management (much later)
- Destructuring syntax and whether it unifies with import

## 9. Macro System

**Decision: AST-based macros (homoiconic). Hygienic by default.**

### "Lisp Under the Hood"

This is literal. ALL surface syntax desugars into bracket-delimited command invocations
before macro expansion:

```
# Surface syntax:
foo = $x + $y * $z

# Desugars to:
[def foo [+ $x [* $y $z]]]
```

The AST is a tree of command invocations — essentially s-expressions with `[]` instead
of `()`. Macros receive unevaluated AST nodes and return transformed AST nodes.
This is JACL's homoiconic representation.

### Hygiene

Hygienic by default (macros automatically avoid name collisions). Escape hatches for
intentional capture may be added later when needed.

### Design Notes

- Start simple — basic AST-transforming macros first
- Compile-time execution (running arbitrary JACL during compilation): TBD
- Limited macro control over parsing: TBD, but some convenient built-in macros
  will handle common patterns (e.g., `proc`, `if`, control flow)
- Macros can take a bare command invocation (no `[]`) as the last argument,
  which is how `proc name [args] { body }` works

### Open Questions

- Macro definition syntax (`defmacro`? `macro`? `syntax`?)
- Compile-time execution scope and capabilities
- How much control macros have over parsing (custom readers?)
- Quasiquoting syntax for AST templates
- Can macros define new operators?
- Macro expansion order and phase separation

## 10. Environments & Scoping

**Decision: Lexical scoping with explicit context objects for dynamic behavior**

### Scoping

Lexical scoping for all user code. No implicit dynamic scoping.

For dynamic-like behavior (e.g., controlling logging, overriding builtins in a sandbox),
an explicit **context object** is passed around. Builtins like `print` may be macros
that implicitly dispatch through the context:

```
# print might expand to something like:
[ctx.print "hello"]

# sandboxed code gets a restricted context
[interpret $src $restricted-context]
```

### Closure Capture

- **Immutable bindings** (`foo = 3`): captured by value (snapshot at closure creation)
- **Mutable bindings** (`bar : 4`): captured by reference (see mutations)

Future consideration: unifying mutable bindings with `box`/`atom` — i.e., a mutable
binding IS a box under the hood. Deferred for now.

### Environment Reification

- **Compile time:** environments are reified (macros can inspect them)
- **Runtime:** not reified in general. Some specific things may be reified:
  - RTTI for boxed struct types
  - Shell-like command aliases
  - Module restriction maps for sandboxing
- This remains an open design area

### Open Questions

- Context object structure and API
- Which builtins dispatch through context vs are true primitives
- Whether mutable bindings should unify with box/atom
- What RTTI is available at runtime for boxed values
- Command alias mechanism

---

## Incremental Implementation Plan

Each milestone produces a testable, working increment. Later milestones build on earlier
ones but each is a self-contained deliverable.

### Milestone 0: Value Representation (`value/`)

The foundation everything else sits on. Implement the 64-bit tagged value system from
Section 2.

**Delivers:**
- `value.h` — the `JaclVal` type (64-bit tagged word)
- Tag byte manipulation: type tag (5 bits), error/tainted/secret flags
- Constructors and extractors for all inline types: nil, bool, i32, f32
- Pointer-payload constructors for heap types (string, vector, map, closure, etc.)
- Inline string packing (0–7 bytes in the 56-bit payload)
- Flag propagation helpers (e.g., "combine flags from two operands")
- Type predicate macros (`IS_NIL`, `IS_I32`, `IS_ERROR`, etc.)

**Tests:** construct/extract round-trips for every type, flag propagation, inline string
pack/unpack, edge cases (max inline string length, i32 min/max, pointer tagging).

**No dependencies on other milestones.**

---

### Milestone 1: Lexer (`lexer/`)

Tokenize Phase 1 bracket syntax. Operates on byte buffers (later: rope input).

**Delivers:**
- `lexer.h` — streaming tokenizer
- Token types: `[`, `]`, `{`, `}`, `(`, `)`, bare word, number (integer and float
  literals), string (double-quoted, with `$var` and `$[expr]` interpolation markers),
  `$identifier`, `#` comment (skipped), newline, EOF
- Position tracking (line, column, byte offset) for error reporting
- Arena-backed allocation for token storage

**Tests:** tokenize individual constructs, full multi-line programs, edge cases (nested
interpolation, escape sequences, unterminated strings, UTF-8 in bare words).

**Depends on:** Milestone 0 (for arena; value.h not strictly needed but co-developed).

---

### Milestone 2: Parser & AST (`parser/`)

Parse token stream into a tree of command invocations — the "lisp under the hood."

**Delivers:**
- `ast.h` — AST node types: command invocation, literal (number, string, bool, nil),
  variable reference (`$var`), code block (`{ ... }`), interpolated string
- `parser.h` — recursive descent parser producing AST from token stream
- Top-level: implicit brackets (bare commands); nested: explicit `[cmd ...]`
- Code blocks `{ ... }` parsed as a sequence of commands
- Error recovery: meaningful parse error messages with source positions

**Tests:** parse and verify AST structure for all Phase 1 syntax constructs, error
cases (mismatched brackets, unexpected tokens), round-trip pretty-printing.

**Depends on:** Milestone 1.

---

### Milestone 3: Bytecode Compiler & VM — Minimal (`compiler/`, `vm/`)

The point where JACL becomes a running language. Enough to execute `[print [+ 1 2]]`.

**Delivers:**
- `bytecode.h` — instruction encoding (opcode + operands), constant pool, bytecode chunk
- `compiler.h` — AST → bytecode compilation for:
  - Literal loading (nil, bool, i32, f32, strings)
  - Builtin command dispatch (`+`, `-`, `*`, `/`, `print`, comparison ops)
  - Nested subcommand evaluation (`[cmd [cmd ...]]`)
  - Variable reference (`$var`) — read from environment
- `vm.h` — stack-based bytecode interpreter:
  - Operand stack, call frames
  - Instruction dispatch loop
  - An environment structure for name → value bindings
  - A small set of builtins registered by name

**Tests:** compile and execute arithmetic expressions, nested commands, print output,
variable lookup, type errors at runtime (e.g., `[+ "a" 1]`).

**Depends on:** Milestones 0, 2.

---

### Milestone 4: Variables, Procedures & Control Flow

JACL becomes a real programming language.

**Delivers:**
- `def` — immutable variable binding
- `proc` — procedure definition (name, param list, body block)
- Procedure invocation (user-defined procedures dispatch like builtins)
- Lexical scoping: nested environments, procedure bodies create new scopes
- `if` — conditional (takes condition, then-block, optional else-block)
- Closures: capture immutable bindings by value
- Recursion and tail-call optimization (optional — can defer TCO)

**Tests:** define and call procedures, closures capturing variables, recursive
fibonacci/factorial, nested scopes, if/else branching, higher-order procedures
(passing procs as arguments).

**Depends on:** Milestone 3.

---

### Milestone 5: String System

Critical for a command language. Implement the three-tier representation from Section 3.

**Delivers:**
- Inline strings (0–7 bytes packed in value payload) — from Milestone 0, now fully wired
- Heap-interned strings: intern table (hash-consed), pointer-equality comparison
- Large strings: backed by the existing rope module (`sum_tree/rope.h`)
- String operations: `concat`, `length`, `slice`, `index`, comparison
- String interpolation: `"text $var and $[expr] here"` fully working end-to-end
  (lexer marks interpolation boundaries, parser builds interpolated-string AST nodes,
  compiler emits concat sequences)

**Tests:** small/medium/large string creation, interpolation with variables and
subcommands, string operations, intern deduplication, comparison (pointer-eq fast path
for interned, structural for others).

**Depends on:** Milestone 4 (needs working variable lookup and subcommand evaluation
for interpolation).

---

### Milestone 6: Persistent Collections

Wire up the existing HAMT and RRB vector modules as JACL's map and vector types.

**Delivers:**
- Vector type: backed by `rrb_vec`. Builtins: `vec`, `vec-get`, `vec-set`, `vec-push`,
  `vec-pop`, `vec-len`, `vec-concat`, `vec-slice`, `vec-map`, `vec-filter`, `vec-reduce`
- Map type: backed by `hamt`. Builtins: `map`, `map-get`, `map-set`, `map-unset`,
  `map-has`, `map-keys`, `map-vals`, `map-merge`
- Iteration: `each` builtin for both vectors and maps
- Literal syntax TBD — initially use constructor builtins: `[vec 1 2 3]`,
  `[map :key1 val1 :key2 val2]`

**Tests:** create, query, update, iterate over vectors and maps from JACL code;
nested structures; structural sharing (update returns new value, original unchanged).

**Depends on:** Milestone 4 (needs procedures and control flow for `each`/`map`/etc.).

---

### Milestone 7: Error Handling

Implement the error-flag system from Section 7.

**Delivers:**
- `error` builtin — create an error-flagged value
- Implicit error propagation: any operation on an error-flagged value returns the error
  unchanged (implemented in the VM dispatch: check error flag before executing opcodes)
- `try` builtin — evaluate block, use fallback if result is error-flagged
- `error?` predicate — test if a value carries the error flag
- Error values carry arbitrary payloads (strings, maps, etc.)

**Tests:** error creation, propagation through arithmetic/string ops/procedure calls,
try/catch, nested try, error payload inspection.

**Depends on:** Milestone 4. Benefits from Milestone 6 (structured error payloads
using maps).

---

### Milestone 8: Mutable State

Add mutable bindings and container types.

**Delivers:**
- Mutable bindings: `mut` (creates mutable binding), `set!` (reassigns)
  - Phase 1 syntax: `[mut bar 4]`, `[set! bar 5]`
- Closure capture of mutable bindings by reference
- `box` — thread-local mutable container. Builtins: `box`, `unbox`, `box-set!`
- `atom` — thread-safe CAS container. Builtins: `atom`, `deref`, `swap!`, `reset!`
  (concurrency semantics come in Milestone 11, but the data structure is introduced here)

**Tests:** mutable variables, reassignment, closures over mutable bindings, box
operations, atom CAS (single-threaded for now).

**Depends on:** Milestone 4.

---

### Milestone 9: Static Type System — Basics

Begin the gradual typing system from Section 6. This is a compiler change — the VM
already handles unboxed values via Milestone 0.

**Delivers:**
- Type annotations on variable definitions: `[def i64 x 42]` (or surface syntax TBD)
- Compiler emits typed (unboxed) instructions when type is statically known
- Separate opcode variants for unboxed i64/f64/u64 arithmetic
- Type checking at compile time for statically typed contexts
- `to` builtin for explicit conversions: `[to dyn $x]`, `[to i64 $y]`
- Type errors at compile time (e.g., passing i64 where f64 expected)

**Tests:** typed arithmetic (i64, f64), type errors caught at compile time, dyn↔static
conversions, mixed typed/untyped code in same program.

**Depends on:** Milestone 4. Benefits from Milestone 7 (fallible conversions return errors).

---

### Milestone 10: Garbage Collection (`gc/`)

Replace manual/arena memory management with the epoch-based tracing GC from Section 5.

**Delivers:**
- `gc.h` — epoch-based tracing garbage collector
- Global epoch counter, per-thread epoch tracking
- Mark phase: trace from roots (operand stack, call frames, globals, scheduled tasks)
- Sweep phase: collect unreachable objects below the epoch waterline
- Write barriers for mutable containers (box, atom)
- Safe points at procedure call/return boundaries
- Per-thread heap allocation
- Integration: all heap-allocated values (strings, vectors, maps, closures) managed by GC

**Tests:** allocation and collection cycles, reachability (live objects survive, dead
objects collected), write barrier correctness, stress tests with many allocations.

**Depends on:** Milestones 0–8 (GC must trace all value types). Milestone 9 optional
(unboxed values don't need GC, but boxed wrappers do).

---

### Milestone 11: Concurrency (`scheduler/`)

Implement the NxM work-stealing scheduler from Section 4, using the existing chase_lev
deque.

**Delivers:**
- `scheduler.h` — NxM thread pool with work-stealing (one chase_lev deque per thread)
- Task representation: a closure + continuation
- CPS transform in compiler: split procedures at suspension points into continuations
- `parallel` — run blocks concurrently, join on all
- `race` — run blocks concurrently, take first result
- `spawn` — launch background task, return handle
- `await` — suspend current task on a handle
- Thread-safety: tasks mutating thread-local state (boxes) pinned to their thread;
  immutable values shared freely; atoms use CAS

**Tests:** parallel execution, race semantics, spawn/await, shared immutable data across
threads, atom contention, scheduler fairness under load, no data races (TSAN).

**Depends on:** Milestones 4, 8, 10 (GC must be thread-aware).

---

### Milestone 12: Module System

Implement file-based modules from Section 8.

**Delivers:**
- File = module. Module name derived from file path
- `use` builtin: `[use math]`, `[use math [sin cos]]`
- Module compilation: each module compiled independently, exports all top-level bindings
- Module cache: each module compiled/loaded once
- Circular import detection (error)
- Sandboxing: `[interpret $src $restriction-map]` — restrict available modules and builtins

**Tests:** import whole module, selective import, circular import error, sandboxed
interpretation with restricted builtins, module caching.

**Depends on:** Milestone 4. Benefits from Milestone 6 (maps for restriction specs).

---

### Milestone 13: Macro System

Implement AST-based macros from Section 9.

**Delivers:**
- `defmacro` — define a macro that receives unevaluated AST and returns transformed AST
- AST quasiquoting for building templates
- Hygienic expansion (automatic gensym for introduced bindings)
- Macro expansion pass: runs after parsing, before compilation
- Built-in macros: rewrite `proc`, `if`, `use` etc. as macros where possible
  (simplifies the compiler — fewer special forms)

**Tests:** basic macro expansion, hygiene (no accidental capture), nested macro
expansion, macros that generate macros, error cases.

**Depends on:** Milestone 2 (AST), Milestone 4 (needs working language for macro bodies).

---

### Milestone 14: Phase 2 Syntax — Syntactic Sugar

Add the operator and assignment syntax from Section 3 Phase 2.

**Delivers:**
- Assignment syntax: `foo = 3` → `[def foo 3]`, `bar : 4` → `[mut bar 4]`,
  `bar :: 5` → `[set! bar 5]`
- Typed assignment: `i64 baz = 13` → `[def i64 baz 13]`
- Infix operators: `$x + $y * $z` → `[+ $x [* $y $z]]` with precedence
- Operator precedence and associativity rules
- Parenthesized grouping in expression contexts: `($x + $y) * $z`
- Desugaring pass: runs before macro expansion, rewrites surface syntax to bracket form
- All sugar is purely syntactic — no new semantics

**Tests:** all sugar forms desugar correctly, operator precedence, mixed bracket and
sugar syntax, error cases (ambiguous expressions).

**Depends on:** Milestone 13 (macros, so sugar and macros compose correctly).

---

### Milestone 15: FFI & Embedding

Top-tier FFI and C embedding — a core language goal.

**Delivers:**
- C embedding API: `jacl_vm_new()`, `jacl_eval()`, `jacl_call()`, `jacl_register_fn()`,
  value conversion helpers (C ↔ JaclVal)
- FFI for calling C from JACL: declare external functions with type signatures,
  automatic marshaling between JACL values and C types
- Struct interop: JACL structs with known layout can be passed to/from C
- Callback support: pass JACL closures as C function pointers (trampoline)

**Tests:** embed JACL in a C test harness, register C functions callable from JACL,
call C from JACL, pass structs across boundary, callback round-trips.

**Depends on:** Milestones 9 (type system for FFI signatures), 10 (GC-safe handles
for C-held references).

---

### Milestone Summary

| #  | Milestone                    | Key Deliverable                        | Enables                          |
|----|------------------------------|----------------------------------------|----------------------------------|
| 0  | Value Representation         | 64-bit tagged values                   | Everything                       |
| 1  | Lexer                        | Token stream                           | Parsing                          |
| 2  | Parser & AST                 | Command-invocation tree                | Compilation, macros              |
| 3  | Bytecode Compiler & VM       | Running `[print [+ 1 2]]`             | Real programs                    |
| 4  | Variables, Procs, Control    | `def`, `proc`, `if`, closures          | Turing-complete language         |
| 5  | String System                | Three-tier strings, interpolation      | Command-language UX              |
| 6  | Persistent Collections       | Vectors and maps from JACL             | Data manipulation                |
| 7  | Error Handling               | Error flag propagation, `try`          | Robust programs                  |
| 8  | Mutable State                | `mut`, `set!`, `box`, `atom`           | Stateful programs                |
| 9  | Static Type System           | Typed/unboxed values, type checking    | Performance, safety              |
| 10 | Garbage Collection           | Epoch-based tracing GC                 | Long-running programs            |
| 11 | Concurrency                  | NxM scheduler, parallel/spawn/await    | Multithreaded programs           |
| 12 | Module System                | File modules, sandboxing               | Code organization, security      |
| 13 | Macro System                 | AST macros, hygiene                    | Language extensibility           |
| 14 | Phase 2 Syntax               | Operators, assignment sugar            | User-friendly surface syntax     |
| 15 | FFI & Embedding              | C interop, embedding API               | Real-world integration           |

---

## Implementation Modules (Existing)

| Module        | Purpose                                           | Key File                        |
| ------------- | ------------------------------------------------- | ------------------------------- |
| platform      | Cross-platform atomics, threading, allocators     | `platform/platform.h`           |
| arena         | Bulk memory allocation by lifetime                | `arena/arena.h`                 |
| rc            | Thread-safe reference counting                    | `rc/rc.h`                       |
| rrb_vec       | Persistent vector (immutable, structural sharing) | `rrb_vec/rrb_vec.h`             |
| hamt          | Persistent hashmap                                | `hamt/hamt.h`                   |
| segment_array | Resizable array with stable pointers              | `segment_array/segment_array.h` |
| sum_tree      | Generic B-tree with monoidal summaries            | `sum_tree/sum_tree.h`           |
| rope          | Text rope (specialization of sum_tree)            | `sum_tree/rope.h`               |
| chase_lev     | Lock-free work-stealing deque                     | `chase_lev/chase_lev.h`         |
| bigint        | Arbitrary-precision integers                      | `bignum/bigint.h`               |
| bigfloat      | Arbitrary-precision floats                        | `bignum/bigfloat.h`             |
| rational      | Rational arithmetic                               | `bignum/rational.h`             |
| regex/nfa     | Thompson NFA regex engine                         | `regex/nfa.h`                   |
| test          | Memory-tracking test harness                      | `test/test_helpers.h`           |
