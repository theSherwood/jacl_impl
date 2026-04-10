# JACL Design Document

Just A Command Lisp — a fusion of a command language and a lisp. Love child of Tcl and Clojure.

## Core Decisions

| Area        | Decision                                                                                                                                                                                                                                                                 |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Execution   | Bytecode VM (AST → bytecode → stack-based interpreter)                                                                                                                                                                                                                   |
| Values      | 64-bit tagged values (tag byte + 56-bit payload). i32/f32 inline, pointers for heap types. Tainted/secret/error flag bits in tag.                                                                                                                                        |
| Syntax      | Phase 1: bracket-delimited commands `[cmd arg1 arg2]`. Bare words are strings. `$var` for variable read. `{ ... }` for code blocks. Phase 1.5 (M6): line-based sugar — bare `cmd arg1 arg2` desugars to `[cmd ...]`. Phase 2 (later): operator sugar, assignment syntax. |
| Concurrency | Direct-style with compiler CPS transform, NxM work-stealing scheduler, atoms for shared state                                                                                                                                                                            |
| GC          | Epoch-based tracing (no RC at value level, no compaction, no stop-the-world)                                                                                                                                                                                             |
| Types       | Gradual typing — dynamic by default, optional static types give unboxed values                                                                                                                                                                                           |
| Errors      | Error flag on values with implicit propagation (like NaN). `try` to handle.                                                                                                                                                                                              |
| Modules     | File-based, everything public, sandboxing via module restriction                                                                                                                                                                                                         |
| Macros      | AST-based (homoiconic), hygienic by default                                                                                                                                                                                                                              |
| Scoping     | Lexical. Explicit context objects for dynamic-like behavior.                                                                                                                                                                                                             |
| Strings     | Three-tier: inline (0-7 bytes in payload), heap interned, rope-backed large                                                                                                                                                                                              |
| Mutability  | `def` (immutable), `mut`/`set!` (mutable). `box` (thread-local), `atom` (CAS shared)                                                                                                                                                                                     |

## Milestone Roadmap

| #   | Milestone                 | Status       | Key Deliverable                                                                           |
| --- | ------------------------- | ------------ | ----------------------------------------------------------------------------------------- |
| 0   | Value Representation      | **COMPLETE** | 64-bit tagged values, arithmetic, comparisons                                             |
| 1   | Lexer                     | **COMPLETE** | Token stream (23 token types)                                                             |
| 2   | Parser & AST              | **COMPLETE** | Recursive descent parser, arena-allocated AST                                             |
| 3   | Bytecode Compiler & VM    | **COMPLETE** | Opcodes, compiler, stack VM, `jacl_run` pipeline                                          |
| 4   | Variables, Procs, Control | **COMPLETE** | `proc`, `if`, closures, lexical scoping, `for`/`while` loops, `break`/`continue`/`return` |
| 5   | String System             | **COMPLETE** | Heap interned strings, interpolation end-to-end                                           |
| 6   | Line-Based Syntax Sugar   | **COMPLETE** | Implicit brackets, line/semicolon/comma delimiters                                        |
| 7   | Arity Checking            | **COMPLETE** | Compile-time arity checks, variadic procs                                                 |
| 8   | Persistent Collections    | **COMPLETE** | Vectors (RRB) and maps (HAMT) from JACL                                                   |
| 9   | Error Handling            | **COMPLETE** | Error flag propagation, `try`                                                             |
| 10  | Mutable State             | **COMPLETE** | `mut`, `set!`, `box`, `atom`                                                              |
| 11  | Static Type System        | **COMPLETE** | Typed/unboxed values, compile-time checking                                               |
| 12  | Garbage Collection        | **COMPLETE** | Epoch-based tracing GC, non-moving generational (sticky mark-bit), minor/major scheduling |
| 13  | Concurrency               | **COMPLETE** | NxM scheduler, parallel/spawn/await, escape analysis for mutable capture pinning          |
| 14  | Module System             |              | File modules, sandboxing                                                                  |
| 15  | Macro System              |              | AST macros, hygiene                                                                       |
| 16  | Phase 2 Syntax            |              | Operators, assignment sugar                                                               |
| 17  | FFI & Embedding           |              | C interop, embedding API                                                                  |

## Future Milestone Details

### M14: Module System

- File = module, everything public, `use` for imports
- Module cache (compile/load once), circular import detection
- Sandboxing: `[interpret $src $restriction-map]`

### M15: Macro System

- `defmacro` — receives unevaluated AST, returns transformed AST
- AST quasiquoting, hygienic expansion
- Rewrite builtins (`proc`, `if`, etc.) as macros where possible

#### Syntax Objects

Macros operate on **syntax objects** — a dedicated compile-time value type (`JACL_TAG_SYNTAX`) that wraps:
- **Kind**: AST node type (`"command"`, `"var-ref"`, `"lit-int"`, `"block"`, etc.)
- **Datum**: payload (children for commands/blocks, raw value for literals, name for var-refs)
- **Source position**: line/col/file for error messages pointing back to the original call site
- **Scope marks**: set of marks for hygiene tracking (see below)

Syntax objects exist only during compilation — macro bodies are Jacl code evaluated by the compiler, and syntax objects are the values they manipulate. They never appear in the running program.

**Primary interface — quasiquoting:**
```
defmacro unless {cond, body} {
  syntax-quote [if [not ~cond] ~body]      # ~ splices, ~@ splices and flattens
}
```

**Introspection (advanced macros):**
```
syntax-kind $s          # → "command", "lit-int", "var-ref", "block", ...
syntax-datum $s         # → raw value (for literals)
syntax-head $s          # → head syntax object (for commands)
syntax-args $s          # → vec of child syntax objects (for commands)
syntax-commands $s      # → vec of child syntax objects (for blocks)
syntax-pos $s           # → map {line: N, col: M}
syntax->string $s       # → pretty-printed source text
```

**Construction (computed AST):**
```
make-syntax "command" $head $args
make-syntax "lit-int" 42
make-syntax "var-ref" "x"
```

#### Hygiene

Hygienic by default. Each macro expansion gets a fresh scope mark. Identifiers in a `syntax-quote` template get the macro's scope mark; identifiers spliced via `~` retain their original (caller's) scope mark.

```
defmacro swap {a, b} {
  syntax-quote {
    tmp = ~a        # 'tmp' gets macro's scope — invisible to caller
    ~a :: ~b
    ~b :: $tmp
  }
}
```

**Binding operators need no special treatment.** In `x = 3`, `x` comes from the user's code, is passed as an argument to the `=` macro, and spliced back via `~` — it retains the caller's scope naturally.

**`^` prefix for intentional introduction (anaphoric macros):**
```
defmacro aif {cond, body} {
  syntax-quote {
    ^it = ~cond       # ^it → "it" in the caller's scope
    if $^it ~body      # body can reference $it
  }
}
```

**`gensym` for guaranteed-unique temporaries:**
```
defmacro or-else {a, b} {
  tmp = [gensym "or_tmp"]
  syntax-quote {
    ~tmp = ~a
    if (~tmp != nil) { $~tmp } { ~b }
  }
}
```

#### Mode-Specific Operator Overloading (tentative)

Same symbol may need different macro definitions per parsing mode (e.g. `|` = pipe in `{}`, bitwise OR in `()`). One possible approach — mode annotation on `defmacro`:

```
defmacro | :cmd {left, right} { syntax-quote [pipe ~left ~right] }
defmacro | :infix {left, right} { syntax-quote [bit-or ~left ~right] }
```

Whether this is the right mechanism or whether mode dispatch should be handled differently is an open question.

### M16: Phase 2 Syntax

- Assignment: `foo = 3` → `[def foo 3]`, `bar : 4` → `[mut bar 4]`, `bar :: 5` → `[set! bar 5]`
- Infix operators with precedence: `$x + $y * $z` → `[+ $x [* $y $z]]`
- Parenthesized grouping: `($x + $y) * $z`

### M17: FFI & Embedding

- C embedding API: `jacl_vm_new()`, `jacl_eval()`, `jacl_call()`, `jacl_register_fn()`
- FFI for calling C from JACL with automatic marshaling
- Struct interop, callback support (closures as C function pointers)

## Known Limitations

**Escape analysis: boxes in collections are not tracked.** The escape analysis (compiler.c) detects direct mutable captures and transitive captures through closures, but does not detect box/cell refs stored in collections. If a box is stashed into a vector and that vector is passed to `spawn`, the task will not be pinned. Fixing this would require tracking taint through collection operations (vec-push, map-set, etc.).

## Open Questions

**Syntax:** Operator precedence rules. Data literal syntax (vectors, maps, sets). Pattern matching syntax. Lambda/anonymous function syntax. Control flow details (cond/match/loop). `$true/$false/$nil` vs shorter forms.

**Type System:** Generic syntax and constraints. Trait/protocol/interface system. Numeric tower (promotion rules, i32 ↔ bignum). Variant interaction with pattern matching. Rules for when `[to dyn $x]` can fail.

**Concurrency:** Tainted/secret flag interaction with cross-thread communication. Channel primitives (may not be needed). Cancellation semantics for `race` losers. Backpressure/bounded concurrency.

**GC:** Write barrier implementation (card table vs remembered set vs per-thread buffer). GC scheduling heuristics. OOM handling. Finalization for FFI resources.

**Errors:** Structured error matching. Stack traces (automatic vs opt-in). Error chaining/wrapping. Syntax for marking procedures as error-capable.

**Modules:** Search path/resolution rules. Circular import handling. Versioning/dependency management.

## Project Layout

```
src/           JACL pipeline (plain .c files, unity build via jacl.c)
  jacl.c       Unity build entry point — #includes all src/*.c + lib headers
  value.c      64-bit tagged values
  lexer.c      Tokenizer
  ast.c        AST node types
  parser.c     Recursive descent parser
  bytecode.c   Opcodes and bytecode chunks
  compiler.c   AST → bytecode compiler
  vm.c         Stack-based bytecode interpreter
lib/           Infrastructure (single-header libraries)
  arena/       Bulk allocation (arena.h)
  platform/    Atomics, threading (platform.h)
  rc/          Reference counting (rc.h)
  hamt/        Persistent hashmap (hamt.h)
  rrb_vec/     Persistent vector (rrb_vec.h)
  sum_tree/    B-tree + rope (sum_tree.h, rope.h)
  chase_lev/   Work-stealing deque (chase_lev.h)
  bignum/      Bigint, bigfloat, rational (bigint.h)
  regex/       Thompson NFA (nfa.h)
  segment_array/ Segment array (segment_array.h)
test/          JACL test files + test_helpers.h
```

Each test file includes `../src/jacl.c` as a single translation unit — no `#define X_IMPLEMENTATION` needed.
