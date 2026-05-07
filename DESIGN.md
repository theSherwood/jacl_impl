# JACL Design Document

Just A Command Lisp — a fusion of a command language and a lisp. Love child of Tcl and Clojure.

## Core Decisions

| Area        | Decision                                                                                                                                                                                                                                                                 |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Execution   | Bytecode VM (AST → bytecode → stack-based interpreter)                                                                                                                                                                                                                   |
| Values      | 64-bit tagged values (tag byte + 56-bit payload). i32/f32 inline, pointers for heap types. Tainted/secret/error flag bits in tag.                                                                                                                                        |
| Syntax      | Three-mode delimiters: `[]` juxtaposition (commands), `{}` command mode (blocks/params/struct fields), `()` infix mode. Bare words are strings; `$var` reads a variable. Binding operators (`=`, `:`, `::`) sugar `def`/`mut`/`set`. Architecture: `SYNTAX_REDESIGN.md`. |
| Concurrency | Direct-style. CPS transform for await/parallel/race, state-machine transform for generators (`GENERATOR_STATE_MACHINE.md`). NxM work-stealing scheduler. Atoms for shared state, boxes for thread-local.                                                                 |
| GC          | Epoch-based tracing, non-moving, no stop-the-world. Co-design with concurrency: `GC_CONCURRENCY_DESIGN.md`.                                                                                                                                                              |
| Types       | Gradual typing — dynamic by default, optional static types give unboxed values. Sound at commitment sites (typed slots reject implicit dyn flow). Architecture and decisions: `TYPE_SYSTEM.md`.                                                                          |
| Errors      | Error flag on values with implicit propagation (like NaN). `try`/`catch` to handle. Stack traces available via `stack-trace`.                                                                                                                                            |
| Modules     | File-based. Top-level names public by default; underscore prefix marks private (compiler-enforced at import). Sandboxing via `[interpret]` with capability-based restrictions.                                                                                           |
| Macros      | AST-based (homoiconic), hygienic by default. Quasiquoting (`syntax-quote`, `~`, `~@`); `gensym` for fresh names; `^` prefix for intentional anaphoric introduction.                                                                                                      |
| Scoping     | Lexical. Same-scope shadowing is a compile error; nested-scope shadowing is fine. `$ctx` provides typed dynamic scoping for ambient state.                                                                                                                               |
| Strings     | Three-tier: inline (0-7 bytes in payload), heap-interned, rope-backed large.                                                                                                                                                                                             |
| Mutability  | `def` (immutable), `mut`/`set` (mutable cells). `box` (thread-local ref), `atom` (CAS-shared ref).                                                                                                                                                                       |

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
| 14  | Module System             | **COMPLETE** | File modules, both `use` forms, cross-module typing, sandboxed `interpret`                |
| 15  | Macro System              | **COMPLETE** | AST macros via syntax objects, hygienic expansion, quasiquoting, `gensym`                 |
| 16  | Phase 2 Syntax            | **COMPLETE** | Three-mode delimiters, infix in `()`, binding operators (`=`/`:`/`::`)                    |
| 17  | FFI & Embedding           | **COMPLETE** | `embed.c` API, native fns, typed `extern`, `[Ptr T]` for in-process inspection            |

## Where to read more

- `SYNTAX_REDESIGN.md` — full syntax reference + per-feature implementation status
- `TYPE_SYSTEM.md` — type-system architecture, decisions, deferred items
- `STRUCT_DESIGN.md` — struct value-type architecture (no header, C-ABI layout)
- `GENERATOR_STATE_MACHINE.md` — generator SM transform
- `GC_CONCURRENCY_DESIGN.md` — GC and concurrency co-design

## Mode-specific operator overloading (tentative)

The same symbol may need different macro definitions per parsing mode (e.g. `|` = pipe in `{}`, bitwise OR in `()`). One possible approach — mode annotation on `defmacro`:

```
defmacro | :cmd {left, right} { syntax-quote [pipe ~left ~right] }
defmacro | :infix {left, right} { syntax-quote [bit-or ~left ~right] }
```

Whether this is the right mechanism or whether mode dispatch should be handled differently is an open question.

## Known Limitations

**Escape analysis: boxes in collections are not tracked.** The escape analysis (compiler.c) detects direct mutable captures and transitive captures through closures, but does not detect box/cell refs stored in collections. If a box is stashed into a vector and that vector is passed to `spawn`, the task will not be pinned. Fixing this would require tracking taint through collection operations (vec-push, map-set, etc.).

~~**Macros cannot run interpreted code at expansion time.**~~ Resolved: macro bodies are now compiled into closures and executed on a real VM at expansion time via `jacl_ctx_run_closure`. Macros can call arbitrary functions, use `gensym` as a real builtin, and perform any computation that returns a syntax object. Implemented in the macro-eval-rewrite PRD (US-005 through US-013).

~~**7-byte inline name limit.**~~ Resolved: variable names longer than 7 bytes now use heap-interned strings (pointer-stable, `==` comparison works). Names up to 128 bytes are supported. The `compiler__name_val` helper routes names ≤7 bytes to inline strings and 8–128 bytes to interned strings. Gensym prefixes are allowed up to 64 bytes. Implemented in US-002, US-003, and US-004.

## Open Questions

These are *design*-level questions, not implementation TODOs. For per-feature implementation status see `SYNTAX_REDESIGN.md`.

**Syntax:** No-precedence infix mode is settled, but ergonomics in practice still need real-world use to validate. Field-access symbol after `$` (open: backtick / tick / `->`-only — see SYNTAX_REDESIGN open question 13).

**Type system:** Generics (type variables, constraints) — currently typed collections are concrete-only. Trait/protocol/interface system. Numeric tower / bignum integration (the `bignum/` lib exists but isn't wired into the VM). Variant types (and their interaction with pattern matching, when `match` lands).

**Concurrency:** Tainted/secret flag interaction with cross-thread communication. Channel primitives (may not be needed given atoms + futures). Backpressure / bounded concurrency for `par-each`. Whether `cancel` on a Job sends SIGTERM-then-SIGKILL or only SIGKILL.

**GC:** Scheduling heuristics. OOM handling. Finalization for FFI resources.

**Errors:** Structured error matching (depends on `match`). Error chaining / wrapping. Whether to mark procs as error-capable explicitly or trust propagation.

**Modules:** Search path / resolution rules beyond relative-to-importer. Versioning / dependency management. Visibility beyond underscore convention (`pub`/`priv` keywords if encapsulation needs grow).

**Macros:** Mode-specific operator overloading mechanism (see section above). Whether all builtins should be rewritten as macros where possible.

## Project Layout

```
src/           JACL pipeline (plain .c files, unity build via jacl.c)
  jacl.c       Unity build entry point — #includes all src/*.c + lib headers
  jacl.h       Public API for tests and embedders
  value.c      64-bit tagged values
  gc.c, gc_collect.c   Heap, allocation, tracing
  string.c     Three-tier strings (inline, interned, rope)
  lexer.c, parser.c, ast.c   Parse → AST
  bytecode.c   Opcodes and bytecode chunks
  type_error.c Shared type-error formatters (see TYPE_SYSTEM.md)
  typer.c      Type inference pass
  compiler.c   AST → bytecode compiler
  syntax.c     Macro expansion
  collections.c, runtime.c   Builtins + scheduler
  vm.c         Stack-based bytecode interpreter
  embed.c      C embedding API
lib/           Infrastructure (single-header libraries)
  arena/       Bulk allocation
  platform/    Atomics, threading
  unicode/     UTF-8 / NFD helpers
  hamt/        Persistent hashmap
  rrb_vec/     Persistent vector
  sum_tree/    B-tree + rope
  chase_lev/   Work-stealing deque
  bignum/      Bigint, bigfloat, rational (declared in value tags; not currently wired into the VM)
  regex/       Thompson NFA (not currently wired in)
  segment_array/   Segment array
test/          JACL test files + test_helpers.h
```

Tests link against `libjacl.a` (built once from `jacl.c`) and include `../src/jacl.h` — historically tests `#include`d `jacl.c` directly, but the unity-include path survives only in two legacy tests.
