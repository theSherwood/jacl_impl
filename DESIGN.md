# JACL Design Document

Just A Command Lisp — a fusion of a command language and a lisp. Love child of Tcl and Clojure.

## Core Decisions

| Area | Decision |
|------|----------|
| Execution | Bytecode VM (AST → bytecode → stack-based interpreter) |
| Values | 64-bit tagged values (tag byte + 56-bit payload). i32/f32 inline, pointers for heap types. Tainted/secret/error flag bits in tag. |
| Syntax | Phase 1: bracket-delimited commands `[cmd arg1 arg2]`. Bare words are strings. `$var` for variable read. `{ ... }` for code blocks. Phase 1.5 (M6): line-based sugar — bare `cmd arg1 arg2` desugars to `[cmd ...]`. Phase 2 (later): operator sugar, assignment syntax. |
| Concurrency | Direct-style with compiler CPS transform, NxM work-stealing scheduler, atoms for shared state |
| GC | Epoch-based tracing (no RC at value level, no compaction, no stop-the-world) |
| Types | Gradual typing — dynamic by default, optional static types give unboxed values |
| Errors | Error flag on values with implicit propagation (like NaN). `try` to handle. |
| Modules | File-based, everything public, sandboxing via module restriction |
| Macros | AST-based (homoiconic), hygienic by default |
| Scoping | Lexical. Explicit context objects for dynamic-like behavior. |
| Strings | Three-tier: inline (0-7 bytes in payload), heap interned, rope-backed large |
| Mutability | `def` (immutable), `mut`/`set!` (mutable). `box` (thread-local), `atom` (CAS shared) |

## Milestone Roadmap

| #  | Milestone                    | Status       | Key Deliverable |
|----|------------------------------|--------------|-----------------|
| 0  | Value Representation         | **COMPLETE** | 64-bit tagged values, arithmetic, comparisons |
| 1  | Lexer                        | **COMPLETE** | Token stream (23 token types) |
| 2  | Parser & AST                 | **COMPLETE** | Recursive descent parser, arena-allocated AST |
| 3  | Bytecode Compiler & VM       | **COMPLETE** | Opcodes, compiler, stack VM, `jacl_run` pipeline |
| 4  | Variables, Procs, Control    | **COMPLETE** | `proc`, `if`, closures, lexical scoping |
| 5  | String System                | **COMPLETE** | Heap interned strings, interpolation end-to-end |
| 6  | Line-Based Syntax Sugar      | **COMPLETE** | Implicit brackets, line/semicolon/comma delimiters |
| 7  | Arity Checking               | **COMPLETE** | Compile-time arity checks, variadic procs |
| 8  | Persistent Collections       |              | Vectors (RRB) and maps (HAMT) from JACL |
| 9  | Error Handling               |              | Error flag propagation, `try` |
| 10 | Mutable State                |              | `mut`, `set!`, `box`, `atom` |
| 11 | Static Type System           |              | Typed/unboxed values, compile-time checking |
| 12 | Garbage Collection           |              | Epoch-based tracing GC |
| 13 | Concurrency                  |              | NxM scheduler, parallel/spawn/await |
| 14 | Module System                |              | File modules, sandboxing |
| 15 | Macro System                 |              | AST macros, hygiene |
| 16 | Phase 2 Syntax               |              | Operators, assignment sugar |
| 17 | FFI & Embedding              |              | C interop, embedding API |

## Future Milestone Details

### M8: Persistent Collections
- Vector type backed by `rrb_vec`: `vec`, `vec-get`, `vec-set`, `vec-push`, `vec-len`, etc.
- Map type backed by `hamt`: `map`, `map-get`, `map-set`, `map-has`, `map-keys`, etc.
- Iteration via `each` builtin
- Initially use constructor builtins: `[vec 1 2 3]`, `[map key val]`

### M9: Error Handling
- `error` builtin creates error-flagged values with arbitrary payloads
- Implicit propagation: operations on error-flagged values return the error unchanged
- `try` builtin: evaluate block, use fallback if result is error-flagged
- `error?` predicate

### M10: Mutable State
- `mut` (mutable binding), `set!` (reassignment)
- Closure capture of mutable bindings by reference
- `box` — thread-local mutable container
- `atom` — thread-safe CAS container (concurrency semantics in M13)

### M11: Static Type System
- Type annotations: `[def i64 x 42]`
- Compiler emits typed (unboxed) instructions when type is known
- Separate opcode variants for unboxed i64/f64 arithmetic
- `to` builtin for explicit conversions: `[to dyn $x]`, `[to i64 $y]`

### M12: Garbage Collection
- Epoch-based tracing GC with per-thread epoch tracking
- Mark from roots (stack, frames, globals, scheduled tasks), sweep below waterline
- Write barriers for mutable containers (box, atom)
- Safe points at CPS suspension boundaries
- Per-thread heaps

### M13: Concurrency
- NxM thread pool with work-stealing (one chase_lev deque per thread)
- CPS transform in compiler splits procedures at suspension points
- `parallel` (join all), `race` (take first), `spawn`/`await`
- Tasks mutating thread-local state pinned to their thread

### M14: Module System
- File = module, everything public, `use` for imports
- Module cache (compile/load once), circular import detection
- Sandboxing: `[interpret $src $restriction-map]`

### M15: Macro System
- `defmacro` — receives unevaluated AST, returns transformed AST
- AST quasiquoting, hygienic expansion
- Rewrite builtins (`proc`, `if`, etc.) as macros where possible

### M16: Phase 2 Syntax
- Assignment: `foo = 3` → `[def foo 3]`, `bar : 4` → `[mut bar 4]`, `bar :: 5` → `[set! bar 5]`
- Infix operators with precedence: `$x + $y * $z` → `[+ $x [* $y $z]]`
- Parenthesized grouping: `($x + $y) * $z`

### M17: FFI & Embedding
- C embedding API: `jacl_vm_new()`, `jacl_eval()`, `jacl_call()`, `jacl_register_fn()`
- FFI for calling C from JACL with automatic marshaling
- Struct interop, callback support (closures as C function pointers)

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
