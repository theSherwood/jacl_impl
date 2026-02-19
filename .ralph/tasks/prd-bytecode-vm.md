# PRD: Milestone 3 — Bytecode Compiler & VM (Minimal)

## Introduction

This is the milestone where JACL becomes a running language. The goal is to compile AST
(from the existing parser) into bytecode and execute it on a stack-based virtual machine.
The target is executing programs like `def x [+ 1 2]; print $x`.

Three new modules are introduced:
- `compiler/bytecode.h` — instruction encoding, constant pool, bytecode chunk
- `compiler/compiler.h` — AST → bytecode compilation
- `vm/vm.h` — stack-based bytecode interpreter

## Goals

- Compile parsed AST into a compact bytecode representation
- Execute bytecode on a stack-based VM producing correct results
- Support literal values (nil, bool, i32, f32, inline strings ≤7 bytes)
- Support arithmetic (+, -, *, /, %, unary negation) with i32/f32 dynamic dispatch
- Support comparison (==, <, >, <=, >=)
- Support `print` for output
- Support `def` and `$var` for global variable binding and lookup
- Report runtime type errors with source position information
- Full pipeline: source string → lex → parse → compile → execute

## Design Decisions

- **Instruction encoding:** Single-byte opcode + inline operands (uint16_t for constant
  pool indices). Like Lua/CPython.
- **Builtin dispatch:** Compile-time resolution. The compiler recognizes known builtins
  (+, -, print, def, etc.) and emits specific opcodes. Unknown command names are a
  compile error.
- **Strings:** Inline strings only (≤7 bytes via `jacl_inline_string`). Longer strings
  are a compile error in M3. Full string system is M5.
- **Variable names:** Stored as inline string JaclVals in the constant pool. Names >7
  bytes are a compile error in M3.
- **Stack:** Fixed-size operand stack (256 slots). Overflow is a runtime error.
- **Environment:** Simple global-only name→value array with linear scan lookup.
  Pre-populated with `$true`, `$false`, `$nil`.

## User Stories

### US-001: Bytecode chunk and opcode definitions

**Description:** As the compiler and VM, I need a bytecode chunk data structure so that
compiled instructions and constants can be stored and executed.

**Acceptance Criteria:**
- `compiler/bytecode.h` exists following single-header convention
- `OpCode` enum defines all M3 opcodes: `OP_CONST`, `OP_NIL`, `OP_TRUE`, `OP_FALSE`,
  `OP_POP`, `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD`, `OP_NEG`, `OP_EQ`,
  `OP_LT`, `OP_GT`, `OP_LE`, `OP_GE`, `OP_PRINT`, `OP_DEF_GLOBAL`, `OP_GET_GLOBAL`,
  `OP_HALT`
- `BytecodeChunk` struct with fields: `uint8_t* code` (bytecode array),
  `uint32_t code_count`, `JaclVal* constants` (constant pool), `uint32_t const_count`,
  `uint32_t* lines` (source line per bytecode byte for error reporting)
- All storage is arena-allocated
- `chunk_init(BytecodeChunk*, arena_t*)` initializes a chunk
- `chunk_write(BytecodeChunk*, uint8_t byte, uint32_t line)` appends a byte
- `chunk_write_u16(BytecodeChunk*, uint16_t value, uint32_t line)` appends 2 bytes (big-endian)
- `chunk_add_constant(BytecodeChunk*, JaclVal value)` → returns `uint16_t` index
- `OP_CONST` is followed by a `uint16_t` constant pool index
- `OP_DEF_GLOBAL` is followed by a `uint16_t` constant pool index (the variable name as inline string)
- `OP_GET_GLOBAL` is followed by a `uint16_t` constant pool index (the variable name as inline string)
- All other opcodes have no operands (single byte)
- Compiles cleanly with `-Wall -Wextra -std=c99`

---

### US-002: VM state and execution loop

**Description:** As the runtime, I need a VM that can execute bytecode chunks so that
compiled programs produce results.

**Acceptance Criteria:**
- `vm/vm.h` exists following single-header convention
- `VM` struct with fields: operand stack (`JaclVal stack[VM_STACK_MAX]` where
  `VM_STACK_MAX` is 256), `uint32_t stack_top` (index of next free slot),
  `uint8_t* ip` (instruction pointer), `BytecodeChunk* chunk`
- `VMResult` enum: `VM_OK`, `VM_RUNTIME_ERROR`, `VM_STACK_OVERFLOW`
- `vm_init(VM*)` zeroes the VM state
- `vm_exec(VM*, BytecodeChunk*)` executes the chunk and returns `VMResult`
- VM supports a configurable print function: `void (*print_fn)(const char* text, uint32_t len, void* ctx)` with a `void* print_ctx` field on the VM struct, defaulting to stdout
- Dispatch loop handles: `OP_CONST` (push constant), `OP_NIL`/`OP_TRUE`/`OP_FALSE`
  (push literal), `OP_POP` (discard top), `OP_HALT` (stop and return VM_OK)
- Stack overflow (push when `stack_top >= VM_STACK_MAX`) returns `VM_RUNTIME_ERROR`
- Stack underflow (pop when `stack_top == 0`) returns `VM_RUNTIME_ERROR`
- Tests use hand-assembled bytecode chunks to verify stack operations
- `vm/test_vm.c` exists with test scaffolding using `test/test_helpers.h` and `tracked_allocator`
- Test entry added to `build.sh` TESTS array
- All tests pass and no memory leaks

---

### US-003: Compiler skeleton and literal compilation

**Description:** As the pipeline, I need a compiler that translates AST into bytecode
so that parsed programs can be executed.

**Acceptance Criteria:**
- `compiler/compiler.h` exists following single-header convention
- `CompileResult` struct with fields: `BytecodeChunk chunk`, `uint32_t error_count`
- `compiler_compile(ParseResult, arena_t*)` returns `CompileResult`
- `AST_LIT_INT` compiles to `OP_CONST` with `jacl_i32(value)` in constant pool
- `AST_LIT_FLOAT` compiles to `OP_CONST` with `jacl_f32(value)` in constant pool
- `AST_LIT_STRING` (≤7 bytes) compiles to `OP_CONST` with `jacl_inline_string(value, len)`
- `AST_LIT_STRING` (>7 bytes) produces a compile error
- Multi-statement programs: each top-level node compiles, `OP_POP` emitted between
  statements to keep the stack clean (no pop after last statement)
- `OP_HALT` emitted at the end of the chunk
- `compiler/test_compiler.c` exists with test scaffolding
- Test entry added to `build.sh` TESTS array
- Tests verify constant pool contents and emitted bytecode for literal nodes
- All tests pass and no memory leaks

---

### US-004: Arithmetic builtins

**Description:** As a user, I need arithmetic operations so that I can compute with
numbers.

**Acceptance Criteria:**
- Compiler recognizes command heads `+`, `-`, `*`, `/`, `%` and emits corresponding opcodes
- `[+ 1 2]` compiles to: OP_CONST(1), OP_CONST(2), OP_ADD
- `[- 10 3]` compiles to: OP_CONST(10), OP_CONST(3), OP_SUB
- `[* 4 5]`, `[/ 10 2]`, `[% 7 3]` compile similarly
- Unary negation: `[- $x]` (one argument) compiles to OP_NEG instead of OP_SUB
- VM `OP_ADD`: pops two values, checks both are same numeric type (i32 or f32),
  dispatches to `jacl_add_i32` or `jacl_add_f32`, pushes result
- Same dynamic type dispatch for SUB, MUL, DIV, MOD, NEG
- `OP_MOD` with f32 operands produces a runtime error (modulo is i32-only)
- Division by zero produces an error-flagged value (from `jacl_div_i32`/`jacl_div_f32`)
- Nested arithmetic works: `[+ 1 [* 2 3]]` → OP_CONST(1), OP_CONST(2), OP_CONST(3), OP_MUL, OP_ADD → result 7
- Tests verify each operation with i32 and f32 values
- Tests verify nested arithmetic produces correct results
- All tests pass and no memory leaks

---

### US-005: Comparison builtins

**Description:** As a user, I need comparison operations so that I can compare values.

**Acceptance Criteria:**
- Compiler recognizes command heads `==`, `<`, `>`, `<=`, `>=`
- `[== 1 1]` → `JACL_TRUE`, `[== 1 2]` → `JACL_FALSE`
- `[< 1 2]` → `JACL_TRUE`, `[< 2 1]` → `JACL_FALSE`
- `[> 5 3]` → `JACL_TRUE`, `[<= 3 3]` → `JACL_TRUE`, `[>= 2 5]` → `JACL_FALSE`
- VM dispatches: both i32 → `jacl_lt_i32` etc., both f32 → `jacl_lt_f32` etc.
- `OP_EQ` uses `jacl_eq` (works across types — different types are not equal)
- Comparisons between mismatched numeric types (i32 vs f32) produce a runtime error
  (except `OP_EQ` which returns false for different types)
- Tests verify each comparison with i32 and f32 values
- All tests pass and no memory leaks

---

### US-006: Print builtin

**Description:** As a user, I need `print` so that I can see program output.

**Acceptance Criteria:**
- Compiler recognizes command head `print` and emits `OP_PRINT`
- `print 42` compiles to: OP_CONST(42), OP_PRINT
- `print` with no arguments is a compile error
- `print` with multiple arguments prints each separated by spaces (compile N args,
  emit OP_PRINT for each — or emit a multi-arg print opcode; simplest: single arg only,
  `[print 42]` prints one value)
- VM `OP_PRINT`: pops value, formats and outputs via the VM's `print_fn`:
  - `JACL_NIL` → `"nil"`
  - `JACL_TRUE` → `"true"`, `JACL_FALSE` → `"false"`
  - i32 → decimal string (e.g., `"42"`, `"-7"`)
  - f32 → decimal string with `%g` format
  - inline_string → the string content
  - error-flagged → `"<error>"`
- Each `OP_PRINT` outputs a trailing newline
- `OP_PRINT` pushes `JACL_NIL` onto the stack as its return value (print returns nil)
- Tests capture output via custom `print_fn` and verify formatted strings
- Tests verify `[print [+ 1 2]]` outputs `"3\n"`
- All tests pass and no memory leaks

---

### US-007: Global environment, def, and variable references

**Description:** As a user, I need `def` to bind variables and `$var` to read them
so that I can name and reuse values.

**Acceptance Criteria:**
- VM has an `Environment` (or equivalent) struct: a growable array of (name, value) pairs
- `vm_init` pre-populates the environment with: `true` → `JACL_TRUE`,
  `false` → `JACL_FALSE`, `nil` → `JACL_NIL`
- Compiler recognizes command head `def`: `def x 42` compiles to
  OP_CONST(42), OP_DEF_GLOBAL(name_index) where name_index is the constant pool index
  of the inline string `"x"`
- `def` with wrong argument count (not exactly 2 args: name + value) is a compile error
- `def`'s first argument must be `AST_LIT_STRING` (the variable name) — otherwise compile error
- `AST_VAR_REF` compiles to `OP_GET_GLOBAL(name_index)` where name_index is the
  constant pool index of the inline string variable name
- VM `OP_DEF_GLOBAL`: pops value, stores name→value in environment. Redefining
  an existing name overwrites silently
- `OP_DEF_GLOBAL` pushes `JACL_NIL` as its return value (def returns nil)
- VM `OP_GET_GLOBAL`: looks up name in environment, pushes value. If not found,
  pushes an error-flagged value
- `$true`, `$false`, `$nil` resolve from pre-populated environment
- Tests verify: `def x 42; print $x` → output `"42\n"`, `def x [+ 1 2]; print $x` → `"3\n"`
- Tests verify: `$true` → `JACL_TRUE`, undefined variable → error
- All tests pass and no memory leaks

---

### US-008: Nested subcommands and multi-statement programs

**Description:** As a user, I need nested commands and multi-line programs so that I can
write real computations.

**Acceptance Criteria:**
- `[print [+ 1 [* 2 3]]]` executes correctly: prints `"7\n"`
- `[+ [* 2 3] [- 10 4]]` evaluates to i32(12)
- Deeply nested: `[+ 1 [+ 2 [+ 3 4]]]` evaluates to i32(10)
- Multi-statement with def: `def x 10\ndef y 20\nprint [+ $x $y]` → output `"30\n"`
- Multi-statement with semicolons: `def a 1; def b 2; print [+ $a $b]` → output `"3\n"`
- Intermediate statement results are popped (stack is clean between statements)
- Final statement result is available as the execution result
- Tests verify all above cases
- All tests pass and no memory leaks

---

### US-009: Runtime error reporting

**Description:** As a developer, I need clear error messages so that I can debug type
mismatches and other runtime errors.

**Acceptance Criteria:**
- Type mismatch in arithmetic: `[+ $true 1]` produces a runtime error with message
  indicating the expected type and the actual type
- Type mismatch in comparison: `[< $true 1]` produces a runtime error
- Undefined variable: `print $undefined` produces a runtime error naming the variable
- Stack overflow produces a runtime error
- All runtime errors include the source line number from the bytecode chunk's line table
- `vm_exec` returns `VM_RUNTIME_ERROR` when an error occurs
- The VM stores the last error message (arena-allocated string) accessible after execution
- Compile errors: unknown command name (e.g., `foo 42`) produces a compile error with
  source position
- Compile errors: string >7 bytes produces a compile error
- `CompileResult.error_count > 0` when compile errors occur
- Tests verify each error case produces the expected error type and a non-empty message
- All tests pass and no memory leaks

---

### US-010: End-to-end pipeline integration

**Description:** As a developer, I need a complete source-to-execution pipeline so that
I can verify the entire system works together.

**Acceptance Criteria:**
- A helper function `jacl_run(const char* source, VM* vm, arena_t* arena)` → `VMResult`
  that chains: `lexer_lex` → `parser_parse` → `compiler_compile` → `vm_exec`
- This helper lives in `vm/vm.h` (or a separate convenience header)
- Parse errors (from lexer/parser) are reported, execution does not proceed
- Compile errors are reported, execution does not proceed
- Runtime errors are reported with line numbers
- Integration test: a 10+ line program mixing def, arithmetic, comparisons, print,
  nested subcommands — all output verified
- Integration test: program with deliberate errors verifies error reporting
- Test with the motivating example: `[print [+ 1 2]]` → output `"3\n"`
- Tests verify no memory leaks (all allocations through arena + tracked allocator)
- All tests pass, full build suite (`build.sh`) green

## Functional Requirements

- FR-1: Bytecode uses single-byte opcodes with optional uint16_t inline operands
- FR-2: Constant pool stores JaclVal values (literals and variable names as inline strings)
- FR-3: VM uses a fixed-size operand stack (256 JaclVal slots)
- FR-4: Arithmetic opcodes dynamically dispatch based on operand types (i32 or f32)
- FR-5: Compiler resolves known builtins at compile time — no runtime command lookup
- FR-6: Unknown command names produce compile errors
- FR-7: The VM's print function is configurable (function pointer + context) for testability
- FR-8: All memory is arena-allocated; no manual malloc/free
- FR-9: Source line numbers are tracked per bytecode byte for error reporting
- FR-10: Global environment is pre-populated with $true, $false, $nil

## Non-Goals (Out of Scope)

- No user-defined procedures (`proc`) — that's M4
- No `if`/control flow — that's M4
- No closures or lexical scoping — that's M4
- No heap-allocated strings (>7 bytes) — that's M5
- No string interpolation — that's M5
- No persistent collections (vector, map) — that's M6
- No error handling (`try`/`error`) — that's M7
- No mutable variables (`mut`/`set!`) — that's M8
- No static types — that's M9
- No garbage collection — that's M10
- No code blocks `{ ... }` as values — blocks are not compiled in M3
- No bytecode serialization/AOT — future milestone
- No optimization passes

## Technical Considerations

- **Depends on:** Milestone 0 (`value/value.h` for JaclVal, arithmetic, comparisons),
  Milestone 2 (`parser/parser.h` + `parser/ast.h` for AST input)
- **Single-header convention:** Each `.h` file uses `#ifndef X_H` / `#define X_H` for
  declarations and `#ifdef X_IMPLEMENTATION` for implementation
- **Arena allocation:** All bytecode chunks, constant pools, and string data allocated
  from arena. Tests use `tracked_allocator` to verify no leaks.
- **Test pattern:** Table-driven `main()` with `{name, fn}` entries, `setup()`/`teardown()`
  with `check_no_leaks()`, matching existing test style
- **Build system:** Each test file added as `"path/test.c|name|"` to `build.sh` TESTS array
- **Compiler flags:** `-Wall -Wextra -std=c99 -g`
- **Naming conventions:** Public types PascalCase, public functions `module_verb_noun`,
  internal functions `module__function_name` (double underscore), constants ALL_CAPS
- **Existing modules used:** `arena/arena.h`, `lexer/lexer.h`, `parser/parser.h`,
  `parser/ast.h`, `value/value.h`, `test/test_helpers.h`

## Success Metrics

- `[print [+ 1 2]]` compiles and prints `3`
- `def x 42; def y 8; print [+ $x $y]` compiles and prints `50`
- `[+ "hello" 1]` produces a meaningful runtime type error
- All existing tests continue to pass (29+ test suites in `build.sh`)
- Zero memory leaks detected by tracked allocator

## Open Questions

- Should `print` accept multiple arguments (print each separated by spaces) or
  strictly one argument? (Recommendation: one argument for M3 simplicity. Multi-arg
  can be added as sugar later.)
- Should the `OP_PRINT` return value be nil or the printed value? (Recommendation: nil,
  matching command-language convention where print is a side effect.)
- Should `def` of an already-defined name be an error or a silent overwrite?
  (Recommendation: silent overwrite for M3. Immutability enforcement can be added later.)
