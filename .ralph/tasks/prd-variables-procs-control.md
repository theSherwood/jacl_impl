# PRD: Milestone 4 — Variables, Procedures & Control Flow

## Introduction

This milestone makes JACL a real programming language. M3 gave us a working pipeline with
global variables and builtin arithmetic. M4 adds user-defined procedures (`proc`), lexical
scoping with nested environments, conditional branching (`if`), looping (`while`), closures
that capture bindings by value, and recursion. After this milestone, JACL programs can
express arbitrary computations.

Key language constructs added:
- `[def x 42]` — immutable binding (now works in both global and local scope)
- `[proc name [params] { body }]` — procedure definition
- `[if cond { then } { else }]` — conditional
- `[while cond { body }]` — loop
- User-defined procedure invocation: `[my-proc arg1 arg2]`
- Closures: procedures capture their enclosing bindings by value

## Goals

- Support `proc` definitions that create callable procedures stored as values
- Support calling user-defined procedures with the same syntax as builtins
- Implement lexical scoping: `def` inside a proc/block creates a local binding
- Support `if` with then/else blocks that returns the value of the taken branch
- Support `while` loops for iterative computation
- Closures capture immutable bindings by value at definition time
- Recursive procedure calls work (tail-call optimization deferred)
- All new constructs integrate with the existing pipeline (`jacl_run`)
- Zero memory leaks (arena-allocated, verified by tracked allocator)

## Design Decisions

- **Closure representation:** A new heap type `JACL_TAG_CLOSURE` packing a pointer to an
  arena-allocated `JaclClosure` struct. The struct holds: a pointer to the compiled
  `BytecodeChunk` for the body, parameter count, parameter names, and an array of captured
  upvalues (copied `JaclVal`s — capture by value since all bindings are immutable in M4).
- **Environment model:** Flat frame with compile-time resolved local slots, plus upvalue
  slots for closures. Each call frame has a fixed-size locals array. The compiler resolves
  variable references to local indices or upvalue indices at compile time. Global fallback
  for top-level names.
- **Call frames:** A `CallFrame` struct (function pointer, return IP, stack base) pushed
  onto a call frame stack in the VM. Max depth limit (e.g. 64) to catch runaway recursion.
- **Block compilation:** `{ ... }` blocks in `if`/`while`/`proc` are compiled inline into
  the enclosing chunk, using jump instructions for control flow. Blocks are not first-class
  values in M4.
- **New opcodes:** `OP_GET_LOCAL`, `OP_SET_LOCAL`, `OP_GET_UPVALUE`, `OP_CALL`,
  `OP_RETURN`, `OP_CLOSURE`, `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_LOOP`, `OP_POP_N`.
- **Truthiness:** `false` and `nil` are falsy; everything else (including `0` and `""`)
  is truthy.

## User Stories

### US-001: Local variable slots and compile-time resolution

**Description:** As the compiler, I need to resolve `def` bindings to local slot indices
so that local variables are fast flat-array lookups rather than name-based hash lookups.

**Acceptance Criteria:**
- [ ] Compiler tracks a `Local` array: `{ name, depth }` for each local in scope
- [ ] `[def x expr]` inside a procedure body compiles to: evaluate expr, store in local slot
- [ ] `$x` referencing a local compiles to `OP_GET_LOCAL <slot>` (uint8_t operand)
- [ ] `[def x expr]` at top-level still compiles to `OP_DEF_GLOBAL` / `OP_GET_GLOBAL`
- [ ] Scope depth tracked: entering a block increments depth, exiting decrements and pops locals
- [ ] Redefining a name in the same scope shadows (new slot), does not error
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`
- [ ] Unit tests verify local slot assignment for simple and nested scopes

---

### US-002: New opcodes for locals, jumps, calls, and closures

**Description:** As the VM, I need new opcodes to support local variable access, control
flow jumps, procedure calls, and closure creation.

**Acceptance Criteria:**
- [ ] `OP_GET_LOCAL <slot>` — push `stack[frame_base + slot]` onto stack
- [ ] `OP_SET_LOCAL <slot>` — write top-of-stack to `stack[frame_base + slot]` (for internal use)
- [ ] `OP_GET_UPVALUE <index>` — push upvalue from current closure's upvalue array
- [ ] `OP_JUMP <offset_u16>` — unconditional forward jump
- [ ] `OP_JUMP_IF_FALSE <offset_u16>` — pop condition, jump if falsy
- [ ] `OP_LOOP <offset_u16>` — unconditional backward jump (for while loops)
- [ ] `OP_CALL <arg_count>` — call a closure: push new CallFrame, set up stack window
- [ ] `OP_RETURN` — pop CallFrame, push return value onto caller's stack
- [ ] `OP_CLOSURE <const_idx>` — create closure from prototype + capture upvalues
- [ ] `OP_POP_N <count>` — discard N values from stack (for scope cleanup)
- [ ] All new opcodes added to the `OpCode` enum in `bytecode.c`
- [ ] Unit tests exercise each opcode in isolation

---

### US-003: Call frames and procedure invocation

**Description:** As the VM, I need a call frame stack so that procedure calls save/restore
execution context correctly.

**Acceptance Criteria:**
- [ ] `CallFrame` struct: `{ JaclClosure* closure, uint8_t* return_ip, uint32_t stack_base }`
- [ ] VM has a `CallFrame frames[VM_FRAMES_MAX]` array (VM_FRAMES_MAX = 64)
- [ ] `OP_CALL` validates argument count matches parameter count, errors if mismatch
- [ ] `OP_CALL` pushes a new frame: sets `ip` to closure's bytecode, `stack_base` to first arg slot
- [ ] `OP_RETURN` pops the frame, restores `ip`, pushes return value at caller's stack position
- [ ] Exceeding VM_FRAMES_MAX produces a "stack overflow" runtime error (catches infinite recursion)
- [ ] Calling a non-closure value produces a runtime error: "cannot call <type>"
- [ ] Tests verify basic call/return, nested calls, and recursion depth limit

---

### US-004: Procedure definition (`proc`)

**Description:** As a JACL programmer, I want to define named procedures so that I can
create reusable functions.

**Acceptance Criteria:**
- [ ] `[proc name [params] { body }]` syntax parsed and compiled
- [ ] `proc` compiles the body into a separate `BytecodeChunk` (or sub-region), creates a
      closure prototype, and emits `OP_CLOSURE` + `OP_DEF_GLOBAL` (or local slot) to bind the name
- [ ] Parameters are local slot 0..N-1 in the procedure's frame
- [ ] The body's last expression is the implicit return value
- [ ] `[proc add [a b] { [+ $a $b] }]` followed by `[add 1 2]` returns `3`
- [ ] `[proc greet [name] { [print $name] }]` followed by `[greet hello]` prints `hello`
- [ ] Zero-parameter procedures work: `[proc answer [] { 42 }]` then `[answer]` returns `42`
- [ ] Redefining a proc name overwrites the binding (same as `def`)
- [ ] Tests verify definition, invocation, parameter passing, and return values

---

### US-005: Conditional (`if`)

**Description:** As a JACL programmer, I want conditional branching so that I can make
decisions in my programs.

**Acceptance Criteria:**
- [ ] `[if cond { then-body }]` — evaluates then-body if cond is truthy, returns nil otherwise
- [ ] `[if cond { then-body } { else-body }]` — evaluates one branch, returns its value
- [ ] Truthiness: `false` and `nil` are falsy; all other values (0, "", etc.) are truthy
- [ ] Compiler emits: compile cond, `OP_JUMP_IF_FALSE` over then-body, compile then-body,
      `OP_JUMP` over else-body, compile else-body
- [ ] `if` is an expression — it returns the value of the taken branch
- [ ] `[if [> 5 3] { 1 } { 2 }]` returns `1`
- [ ] `[if false { 1 }]` returns `nil`
- [ ] Nested `if` works: `[if true { [if false { 1 } { 2 }] }]` returns `2`
- [ ] Wrong argument count produces a compile error
- [ ] Tests verify truthy/falsy values, both branches, nesting, expression return

---

### US-006: While loop

**Description:** As a JACL programmer, I want a while loop so that I can perform iterative
computation without relying solely on recursion.

**Acceptance Criteria:**
- [ ] `[while cond { body }]` — repeatedly evaluates body while cond is truthy
- [ ] Compiler emits: loop-start label, compile cond, `OP_JUMP_IF_FALSE` to exit,
      compile body, `OP_POP` (discard body result), `OP_LOOP` back to loop-start
- [ ] `while` returns `nil` when the loop exits
- [ ] While loop with counter: a program using `def`, `while`, `if`, and arithmetic
      can compute a factorial or sum
- [ ] Zero-iteration loop works: `[while false { 1 }]` returns `nil`
- [ ] Tests verify iteration count, loop exit, and zero-iteration case
- [ ] Note: `while` needs mutable state to be useful on its own — test with global `def`
      overwrite for now (since `mut`/`set!` is M8, allow `def` to rebind in same scope for
      loop counters in M4)

---

### US-007: Closures and upvalue capture

**Description:** As a JACL programmer, I want procedures to capture variables from their
enclosing scope so that I can create closures.

**Acceptance Criteria:**
- [ ] When a procedure body references a variable from an enclosing scope, the compiler
      marks it as an upvalue
- [ ] `OP_CLOSURE` captures the current values of upvalue slots at closure creation time
      (capture by value — since all M4 bindings are immutable)
- [ ] `OP_GET_UPVALUE` retrieves the captured value at runtime
- [ ] `JaclClosure` struct: `{ BytecodeChunk* chunk, uint8_t param_count, JaclVal* params,
      JaclVal* upvalues, uint8_t upvalue_count }`
- [ ] Closure value uses `JACL_TAG_CLOSURE` with pointer payload to `JaclClosure`
- [ ] `[proc make-adder [x] { [proc inner [y] { [+ $x $y] }] }]` — `[make-adder 10]`
      returns a closure; calling it with `5` returns `15`
- [ ] Multiple closures over same scope capture independently
- [ ] Tests verify capture, independent captures, nested closures

---

### US-008: Recursion

**Description:** As a JACL programmer, I want to write recursive procedures so that I can
express algorithms like factorial and fibonacci.

**Acceptance Criteria:**
- [ ] A procedure can call itself by name: `[proc fact [n] { [if [== $n 0] { 1 } { [* $n [fact [- $n 1]]] }] }]`
- [ ] `[fact 10]` returns `3628800`
- [ ] Mutually recursive procedures work (proc A calls proc B which calls proc A)
- [ ] Deep recursion (e.g., `[fact 50]` — may overflow i32 but should not crash the VM;
      the frame limit catches truly runaway recursion)
- [ ] Tests verify factorial, fibonacci, and recursion depth limit error message

---

### US-009: End-to-end integration tests

**Description:** As a developer, I need comprehensive integration tests that exercise the
full pipeline with M4 features.

**Acceptance Criteria:**
- [ ] C-level tests in `test/test_m4.c` using `jacl_run()` and PrintCapture pattern
- [ ] Test: `[def x 10] [proc double [n] { [* $n 2] }] [print [double $x]]` prints `20`
- [ ] Test: Factorial via recursion returns correct result
- [ ] Test: Fibonacci via recursion returns correct result
- [ ] Test: Closure captures variable from enclosing scope
- [ ] Test: `if`/`while` with various truthy/falsy values
- [ ] Test: Nested procedure definitions and calls
- [ ] Test: Error cases — wrong arg count, calling non-function, undefined variable in proc
- [ ] All existing M0-M3 tests continue to pass
- [ ] Zero memory leaks detected by tracked allocator in all tests
- [ ] Test file added to `build.sh` TESTS array

---

### US-010: Mark M4 complete in DESIGN.md

**Description:** As a project maintainer, I want M4 marked as complete in DESIGN.md once
all stories are implemented, so the roadmap reflects actual progress.

**Acceptance Criteria:**
- [ ] All US-001 through US-009 are implemented and passing
- [ ] M4 row in DESIGN.md milestone table updated from empty to `**COMPLETE**`
- [ ] Commit message references M4 completion

## Functional Requirements

- FR-1: `[def name expr]` creates an immutable binding — global at top-level, local inside
  procedures/blocks
- FR-2: `[proc name [params] { body }]` creates a closure value and binds it to `name`
- FR-3: `[name arg1 arg2]` invokes a user-defined procedure when `name` resolves to a closure
- FR-4: Procedures return the value of their last expression
- FR-5: `[if cond { then }]` and `[if cond { then } { else }]` evaluate the appropriate
  branch and return its value
- FR-6: `[while cond { body }]` repeatedly evaluates body while cond is truthy, returns nil
- FR-7: Lexical scoping: inner `def` shadows outer bindings; procedure bodies create new scopes
- FR-8: Closures capture enclosing immutable bindings by value at definition time
- FR-9: Recursive calls work up to the frame depth limit
- FR-10: Argument count mismatch at call site produces a runtime error
- FR-11: Calling a non-closure value produces a runtime error with type information
- FR-12: Truthiness: `false` and `nil` are falsy; everything else is truthy
- FR-13: All values are arena-allocated; no manual malloc/free
- FR-14: Source line numbers propagate through new opcodes for error reporting

## Non-Goals (Out of Scope)

- No `mut` / `set!` mutable bindings — that's M8
- No tail-call optimization — can be added later
- No anonymous functions / lambdas — future syntax addition
- No variadic parameters — future addition
- No default parameter values
- No pattern matching on parameters
- No `{ ... }` blocks as first-class values (they're compiled inline for control flow)
- No `and` / `or` / `not` boolean operators — can be added as builtins later
- No `cond` / `match` / `case` — future control flow
- No heap-allocated strings (>7 bytes) — that's M5
- No persistent collections — that's M6
- No error handling (`try`/`error`) — that's M7

## Technical Considerations

- **Depends on:** M0 (value.c for JaclVal, JACL_TAG_CLOSURE), M1 (lexer.c),
  M2 (parser.c, ast.c for AST_BLOCK), M3 (bytecode.c, compiler.c, vm.c)
- **Files modified:** `src/value.c` (closure helpers), `src/bytecode.c` (new opcodes),
  `src/compiler.c` (local resolution, proc/if/while compilation, upvalue tracking),
  `src/vm.c` (call frames, new opcode handlers), `src/jacl.c` (includes unchanged)
- **New file:** `test/test_m4.c` for integration tests
- **Unity build:** All new code goes in existing `src/*.c` files, included via `src/jacl.c`
- **Arena allocation:** Closures, upvalue arrays, parameter name arrays — all arena-allocated
- **Test pattern:** Same as M3 — `test_helpers.h`, `tracker_reset()`, `check_no_leaks()`,
  PrintCapture for output verification
- **Compiler flags:** `-Wall -Wextra -std=c99 -g`
- **Naming:** `compiler__resolve_local`, `compiler__add_upvalue`, `vm__call_closure`, etc.

## Success Metrics

- `[proc double [x] { [* $x 2] }] [print [double 21]]` prints `42`
- `[proc fact [n] { [if [== $n 0] { 1 } { [* $n [fact [- $n 1]]] }] }] [print [fact 10]]`
  prints `3628800`
- Closure example: `make-adder` pattern works and returns correct results
- `[while ...]` can compute iterative sums
- All M0-M3 tests continue to pass
- Zero memory leaks in all tests
- Frame overflow produces a clear error message

## Open Questions

- Should `def` inside a while loop body be allowed to rebind the same name on each
  iteration (effectively creating a mutable loop counter)? Recommendation: yes for M4
  pragmatism, since `mut`/`set!` doesn't exist until M8. The compiler can allow same-scope
  rebinding via `def`.
- Should there be a `return` keyword for early return from procedures? Recommendation:
  defer to a later milestone. Implicit last-expression return is sufficient for M4.
- Max local variable count per scope? Recommendation: 256 (uint8_t slot index), matching
  the Lua/Lox convention. Error if exceeded.
- Should closures be printable? Recommendation: `print` on a closure outputs
  `<proc name>` or `<closure>`. Implementation detail, not a blocker.
