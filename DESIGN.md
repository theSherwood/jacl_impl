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
- `SYMBOL` / `KEYWORD`
- `VECTOR` (pointer to RRB vec)
- `MAP` (pointer to HAMT)
- `FUNCTION` / `CLOSURE`
- `ERROR` (pointer to error info)
- `BIGINT` (pointer to heap)
- `RATIONAL` (pointer to heap)
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
- i32/f32 is a fair tradeoff for dynamic values — users needing full-width numerics
  use static typing, which also gives them better performance
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

## Implementation Modules (Existing)

| Module | Purpose | Key File |
|--------|---------|----------|
| platform | Cross-platform atomics, threading, allocators | `platform/platform.h` |
| arena | Bulk memory allocation by lifetime | `arena/arena.h` |
| rc | Thread-safe reference counting | `rc/rc.h` |
| rrb_vec | Persistent vector (immutable, structural sharing) | `rrb_vec/rrb_vec.h` |
| hamt | Persistent hashmap | `hamt/hamt.h` |
| segment_array | Resizable array with stable pointers | `segment_array/segment_array.h` |
| sum_tree | Generic B-tree with monoidal summaries | `sum_tree/sum_tree.h` |
| rope | Text rope (specialization of sum_tree) | `sum_tree/rope.h` |
| chase_lev | Lock-free work-stealing deque | `chase_lev/chase_lev.h` |
| bigint | Arbitrary-precision integers | `bignum/bigint.h` |
| bigfloat | Arbitrary-precision floats | `bignum/bigfloat.h` |
| rational | Rational arithmetic | `bignum/rational.h` |
| regex/nfa | Thompson NFA regex engine | `regex/nfa.h` |
| test | Memory-tracking test harness | `test/test_helpers.h` |
