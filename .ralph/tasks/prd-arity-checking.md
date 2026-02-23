# PRD: Arity Checking (M7)

## Introduction

Add compile-time arity checking to JACL so that calling a procedure with the wrong number of arguments is caught before execution. Currently, arity mismatches are only detected at runtime when `OP_CALL` compares `arg_count` against `closure->param_count`. This milestone moves that validation into the compiler for statically-known calls and extends it with variable tracking so that procs assigned to locals are also checked.

## Goals

- Catch wrong-argument-count errors at compile time for all statically resolvable calls
- Track arity through variable assignments so `[def f foo] [f 1 2 3]` is checked
- Enforce consistent arity rules for all builtins
- Extend `JaclClosure` with arity metadata to support future runtime and variadic use
- Lay groundwork for the full static type system (M11)

## User Stories

### US-001: Compile-time arity error for direct proc calls
**Description:** As a developer, I want the compiler to reject `[foo 1 2 3]` when `foo` is defined as `[proc foo [a b] { ... }]` so that I catch mistakes before running my program.

**Acceptance Criteria:**
- [ ] Compiler emits an error when a command head resolves to a known proc and the argument count does not match `param_count`
- [ ] Error message includes line, column, proc name, expected count, and actual count (e.g., `line 5, col 1: proc 'foo' expects 2 arguments but got 3`)
- [ ] Compilation fails (non-zero `error_count`) and no bytecode is executed
- [ ] Existing valid programs continue to compile and run unchanged

### US-002: Arity tracking through variable assignments
**Description:** As a developer, I want arity checking to work even when I bind a proc to a variable, so that `[def f add3] [f 1 2]` is caught at compile time.

**Acceptance Criteria:**
- [ ] When `def` binds a local to a value whose arity is known (a proc literal or another local with known arity), the compiler records that arity in the local table
- [ ] Calls through a local with known arity are checked at compile time
- [ ] When the bound value's arity is unknown (e.g., a runtime expression), no compile-time check is emitted and the call compiles normally
- [ ] Arity info propagates through simple alias chains: `[def g f]` inherits `f`'s arity

### US-003: Builtin arity enforcement
**Description:** As a developer, I want consistent arity errors for builtins like `if`, `def`, and `proc` so that typos and missing arguments are caught early.

**Acceptance Criteria:**
- [ ] All existing builtins have documented arity constraints enforced in the compiler
- [ ] Builtins with fixed arity (`def`: 2, `proc`: 3, `print`: 1, `+`: 2, `-`: 2, `*`: 2, `/`: 2, `=`: 2, `<`: 2, `>`: 2) produce compile-time errors on mismatch
- [ ] Builtins with flexible arity (`if`: 2–3, `slice`: 2–3, `concat`: 2+) produce errors when outside their valid range
- [ ] Error messages follow the same format as user proc arity errors

### US-004: Extend JaclClosure with arity metadata
**Description:** As a developer of the JACL runtime, I want `JaclClosure` to carry arity metadata so it is available for future runtime checks, reflection, and variadic support.

**Acceptance Criteria:**
- [ ] `JaclClosure` struct gains a `flags` or `arity_info` field encoding: fixed param count (already exists as `param_count`), a `variadic` flag (reserved, set to `false` for now), and minimum required args (equal to `param_count` for fixed-arity procs)
- [ ] Compiler populates these fields when creating closures
- [ ] Existing runtime arity check in `OP_CALL` (vm.c) continues to function as a safety net using the same metadata
- [ ] No change to the `JaclVal` tagged-value encoding; metadata lives on the heap `JaclClosure` struct

### US-005: Compiler local table arity tracking
**Description:** As a developer of the JACL compiler, I need the `Local` struct to optionally carry arity information so the compiler can check calls through variables.

**Acceptance Criteria:**
- [ ] `Local` struct in compiler.c gains an `arity` field (e.g., `int16_t known_arity; bool arity_valid;`) or equivalent sentinel-based encoding
- [ ] When a local is initialized from a proc definition or another local with known arity, `known_arity` is set
- [ ] When a local's source arity is unknown, `arity_valid` is `false` and no compile-time check occurs
- [ ] Arity information is scoped correctly — a local's arity does not leak across scope boundaries

### US-006: Test suite for arity checking
**Description:** As a developer, I want a comprehensive test suite that validates arity checking behavior so that regressions are caught.

**Acceptance Criteria:**
- [ ] Test: direct call with too few args → compile error
- [ ] Test: direct call with too many args → compile error
- [ ] Test: direct call with correct args → compiles and runs
- [ ] Test: call through variable alias with wrong arity → compile error
- [ ] Test: call through variable alias with correct arity → compiles and runs
- [ ] Test: call through unknown-arity variable → compiles (no error, deferred to runtime)
- [ ] Test: builtin with wrong arity → compile error (at least `if`, `def`, `+`)
- [ ] Test: builtin with correct arity → compiles and runs
- [ ] Test: nested proc definitions with separate arity → each checked independently
- [ ] All tests pass; existing test suite remains green

## Functional Requirements

- FR-1: The compiler must resolve the arity of a command head before emitting `OP_CALL`. If arity is known and the argument count does not match, emit a compile error instead of bytecode.
- FR-2: The compiler must track arity metadata in its `Local` table. When `def` binds a local to a proc or to another local with known arity, the new local inherits that arity.
- FR-3: All builtins must have their arity constraints validated in the compiler. This is largely already done but must be audited for completeness and consistency.
- FR-4: `JaclClosure` must be extended with a `variadic` flag (initially `false`) and a `min_args` field (initially equal to `param_count`) to prepare for rest-args in M8.
- FR-5: Compile errors must include: source line, source column, proc/builtin name, expected argument count (or range), and actual argument count.
- FR-6: When arity is unknown at compile time (dynamic call, unresolvable head), the compiler must emit `OP_CALL` as before with no error — the existing runtime check in vm.c remains the fallback.

## Non-Goals

- No variadic/rest-args syntax (`& rest`) — deferred to M8 when collections are available
- No runtime `CHECK_ARITY` instruction — compile-time only
- No type checking beyond argument count (no type annotations, no typed opcodes)
- No arity checking across module boundaries (modules are M14)
- No changes to the `JaclVal` 64-bit tagged encoding

## Technical Considerations

- `JaclClosure` already has `param_count` (uint8_t) and `name` — extending with `variadic` (bool) and `min_args` (uint8_t) is straightforward and doesn't change the allocation pattern
- Builtins are currently special-cased with inline arity checks in `compiler__compile_command()` — this pattern should be preserved rather than introducing a builtin registry (keep it simple)
- The `Local` struct currently has `name`, `depth`, `is_captured` — adding `known_arity` (int16_t, -1 = unknown) is a minimal change
- Upvalue arity tracking is out of scope for now; closures captured from enclosing scopes won't carry arity info through `Upvalue` entries

## Success Metrics

- All existing tests continue to pass (no regressions)
- Arity mismatches for direct proc calls are caught at compile time with clear messages
- Arity mismatches through simple variable aliases are caught at compile time
- New test suite covers at least 10 arity-checking scenarios

## Open Questions

- Should arity tracking propagate through upvalues (closures referencing procs from enclosing scopes)? Currently scoped to locals only.
- When M8 adds rest-args, should `min_args` be the only check, or should there also be a `max_args` field?
