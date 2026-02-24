# PRD: M9 — Error Handling

## Introduction

Add error handling to JACL with flag-based error values, implicit propagation, `try`/catch, and basic stack traces. Errors are created by setting the error flag on any arbitrary value (the value IS the payload). Errors propagate through all operations and automatically return from functions when not caught by `try`. This builds on the existing `JACL_FLAG_ERROR` infrastructure in `value.c`.

## Goals

- Allow creating error-flagged values with arbitrary payloads via `error` builtin
- Provide `error?` predicate and `error-val` extractor for inspecting errors
- Implement `try` special form with error binding: `[try { body } err { handler }]`
- Errors produced outside a `try` block automatically return from the enclosing function
- Propagate error flags through ALL operations (not just arithmetic)
- Capture basic stack traces when errors are created for debugging

## User Stories

### US-001: `error` builtin — create error-flagged values
**Description:** As a JACL programmer, I want to create error values with arbitrary payloads so that I can signal failure conditions with meaningful data.

**Acceptance Criteria:**
- [ ] `[error "something went wrong"]` returns a string with the error flag set
- [ ] `[error 42]` returns an i32 with the error flag set
- [ ] `[error [map :code 404 :msg "not found"]]` returns a map with the error flag set
- [ ] `[error $nil]` returns nil with the error flag set
- [ ] Compiler emits `OP_ERROR` opcode; VM sets the error flag on the top-of-stack value
- [ ] Arity check: exactly 1 argument
- [ ] Typecheck/build passes

### US-002: `error?` predicate
**Description:** As a JACL programmer, I want to check whether a value is an error so that I can branch on error conditions.

**Acceptance Criteria:**
- [ ] `[error? [error "x"]]` returns `$true`
- [ ] `[error? 42]` returns `$false`
- [ ] `[error? $nil]` returns `$false`
- [ ] Works on error-flagged values from any source (user-created, div-by-zero, etc.)
- [ ] Compiler emits `OP_IS_ERROR` opcode; VM pushes bool based on `jacl_is_error()`
- [ ] Arity check: exactly 1 argument
- [ ] Typecheck/build passes

### US-003: `error-val` extractor
**Description:** As a JACL programmer, I want to extract the payload from an error value so that I can inspect what went wrong.

**Acceptance Criteria:**
- [ ] `[error-val [error "oops"]]` returns `"oops"` (string, no error flag)
- [ ] `[error-val [error 42]]` returns `42` (i32, no error flag)
- [ ] `[error-val [error [map :k "v"]]]` returns the map without the error flag
- [ ] `[error-val 42]` returns `42` unchanged (no-op on non-error values)
- [ ] Compiler emits `OP_ERROR_VAL` opcode; VM clears the error flag on top-of-stack
- [ ] Note: `error-val` does NOT trigger automatic error return (it consumes the error)
- [ ] Arity check: exactly 1 argument
- [ ] Typecheck/build passes

### US-004: Error propagation through non-arithmetic operations
**Description:** As a JACL programmer, I want errors to propagate through ALL operations (not just arithmetic) so that error behavior is consistent and predictable.

**Acceptance Criteria:**
- [ ] `[vec-push [error "x"] 1]` returns the error unchanged
- [ ] `[vec-get [error "x"] 0]` returns the error unchanged
- [ ] `[map-get [error "x"] :k]` returns the error unchanged
- [ ] `[map-set [error "x"] :k 1]` returns the error unchanged
- [ ] `[concat [error "x"] "y"]` returns the error unchanged
- [ ] `[length [error "x"]]` returns the error unchanged
- [ ] `[to-string [error "x"]]` returns the error unchanged
- [ ] For multi-arg ops: if ANY argument is error-flagged, return the first error encountered
- [ ] `[vec-push [vec] [error "x"]]` returns the error (error in second arg)
- [ ] Typecheck/build passes

### US-005: Automatic error return from functions
**Description:** As a JACL programmer, I want errors to automatically return from the enclosing function when not inside a `try` block, so that errors propagate up the call stack without boilerplate.

**Acceptance Criteria:**
- [ ] A function producing an error as any statement returns that error immediately:
  ```
  proc foo { [error "fail"]; [print "never"]; 42 }
  [error? [foo]]  ;; => $true, "never" is not printed
  ```
- [ ] Errors from nested function calls propagate up:
  ```
  proc inner { [error "deep"] }
  proc outer { [inner]; [print "never"]; 99 }
  [error? [outer]]  ;; => $true
  ```
- [ ] Only statements (not the final expression) trigger early return — the final expression's error flag is returned normally as the function's result
- [ ] Compiler emits `OP_CHECK_ERROR` after each non-final statement in function bodies and top-level
- [ ] `OP_CHECK_ERROR`: peeks at top of stack; if error-flagged, triggers return from current frame; otherwise pops and continues
- [ ] Arithmetic error-flag propagation within a single expression still works as before (NaN-style)
- [ ] Typecheck/build passes

### US-006: `try` special form
**Description:** As a JACL programmer, I want to catch errors with `try` and handle them with access to the error value, so that I can recover from failures.

**Acceptance Criteria:**
- [ ] Basic catch: `[try { [error "oops"] } e { [concat "caught: " [error-val $e]] }]` returns `"caught: oops"`
- [ ] No error: `[try { 42 } e { 0 }]` returns `42` (handler not invoked)
- [ ] Error binding: inside the handler, `$e` is the full error value (flag set), `[error-val $e]` extracts payload
- [ ] Multi-statement body: `[try { [error "x"]; [print "skip"]; 1 } e { $e }]` — the error from first statement is caught (print does not execute)
- [ ] Nested try: inner `try` catches before outer
- [ ] `try` suppresses `OP_CHECK_ERROR` early-return behavior within its body — errors produced inside the body are caught by the handler instead of returning from the enclosing function
- [ ] Compiler: emits body with error-jump to handler; handler block has `e` bound as a local
- [ ] Arity check: exactly 4 tokens (body-block, name, handler-block); compile error otherwise
- [ ] Typecheck/build passes

### US-007: Stack trace capture
**Description:** As a JACL programmer, I want errors to automatically capture a stack trace when created so that I can debug where errors originated.

**Acceptance Criteria:**
- [ ] When `error` builtin executes, capture current call frame chain (function name + source line for each frame)
- [ ] VM stores the most recent error's stack trace (single active trace, overwritten on new error)
- [ ] Arithmetic errors (div-by-zero, etc.) also capture a stack trace
- [ ] Maximum trace depth capped (e.g., 32 frames) to bound memory
- [ ] Stack trace struct stores array of `{ function_name, line_number }` entries
- [ ] Typecheck/build passes

### US-008: `stack-trace` builtin
**Description:** As a JACL programmer, I want to access the stack trace of the most recent error so that I can display diagnostic information.

**Acceptance Criteria:**
- [ ] `[stack-trace]` returns a string representation of the most recent error's stack trace
- [ ] Format: one line per frame, e.g., `"  at foo (line 5)\n  at bar (line 12)\n  at <main> (line 20)"`
- [ ] Returns empty string if no error has been created yet
- [ ] Accessible inside `try` handler blocks to get trace of the caught error
- [ ] Arity check: 0 arguments
- [ ] Typecheck/build passes

### US-009: Update `OP_PRINT` for error display
**Description:** As a JACL programmer, I want `print` to display error values with their payload and flag so that debugging is easier.

**Acceptance Criteria:**
- [ ] `[print [error "oops"]]` outputs `<error: oops>` (or similar format showing the payload)
- [ ] `[print [error 42]]` outputs `<error: 42>`
- [ ] `[print [error [vec 1 2 3]]]` outputs `<error: [1 2 3]>` (nested value formatted)
- [ ] Non-error values print unchanged (no regression)
- [ ] Typecheck/build passes

### US-010: Integration tests for M9
**Description:** As a developer, I want comprehensive tests covering all error handling features to ensure correctness and prevent regressions.

**Acceptance Criteria:**
- [ ] `.jacl` harness tests in `test/jacl/` covering:
  - `error_creation.jacl` — `error`, `error?`, `error-val` basics
  - `error_propagation.jacl` — propagation through arithmetic, collections, and function calls
  - `error_try.jacl` — `try` with catch, nested try, multi-statement bodies
  - `error_stack_trace.jacl` — `stack-trace` output in handlers
- [ ] Unit tests in `test/test_m9.c` covering:
  - `OP_ERROR`, `OP_IS_ERROR`, `OP_ERROR_VAL` opcodes
  - `OP_CHECK_ERROR` early-return behavior
  - Error propagation through every collection op
  - `try` compilation and execution
  - Stack trace capture and formatting
- [ ] All existing tests still pass (no regressions)
- [ ] Typecheck/build passes

## Functional Requirements

- FR-1: `[error <value>]` sets `JACL_FLAG_ERROR` on any value and returns it. The payload is the value itself.
- FR-2: `[error? <value>]` returns `$true` if `JACL_FLAG_ERROR` is set, `$false` otherwise.
- FR-3: `[error-val <value>]` clears `JACL_FLAG_ERROR` and returns the underlying value. No-op on non-error values.
- FR-4: All VM operations (arithmetic, comparison, collection, string, higher-order) check inputs for error flags. If any input is error-flagged, the operation returns the first error-flagged input unchanged.
- FR-5: `OP_CHECK_ERROR` is emitted after each non-final statement in function bodies and at top-level. It peeks at the stack top — if error-flagged, it triggers an early return from the current call frame with that value. Otherwise it pops and continues.
- FR-6: `[try { <body> } <name> { <handler> }]` evaluates body. If the body produces an error (via expression result or `OP_CHECK_ERROR`), the error value is bound to `<name>` and the handler block is evaluated. If no error, the body result is returned and the handler is skipped.
- FR-7: Inside a `try` body, `OP_CHECK_ERROR` jumps to the handler instead of returning from the enclosing function.
- FR-8: When `error` or an arithmetic error (div-by-zero, etc.) occurs, the VM captures the current call frame chain into a stack trace struct (max 32 frames).
- FR-9: `[stack-trace]` returns a formatted string of the most recent error's stack trace.
- FR-10: `[print <error-value>]` displays `<error: <formatted-payload>>` instead of the current bare `<error>`.

## Non-Goals (Out of Scope)

- No structured error matching or pattern matching on error types (future milestone)
- No error chaining/wrapping (`cause` field or `wrap-error` builtin)
- No error codes or error type tags beyond the arbitrary payload
- No syntax for marking procedures as error-capable (effect typing)
- No `finally` or cleanup semantics
- No user-defined error hierarchies or classes
- No integration with future concurrency error handling (M13)

## Technical Considerations

- **Existing infrastructure:** `JACL_FLAG_ERROR`, `jacl_is_error()`, `jacl_set_error()`, `jacl_clear_error()`, `jacl_propagate_flags()` are all already implemented in `value.c`. Arithmetic ops already short-circuit on error inputs.
- **New opcodes needed:** `OP_ERROR`, `OP_IS_ERROR`, `OP_ERROR_VAL`, `OP_CHECK_ERROR`, `OP_JUMP_IF_ERROR` (for try), `OP_STACK_TRACE`
- **Compiler changes:** Add `error`, `error?`, `error-val`, `stack-trace` to the builtin `if/else if` chain in `compiler__compile_command()`. Add `try` as a special form with jump-patching similar to `if`.
- **VM changes:** Add error-flag checks at the top of every non-arithmetic opcode handler. Add stack trace capture in `OP_ERROR` and in div-by-zero paths. Add `OP_CHECK_ERROR` handler with frame-return logic.
- **Stack trace storage:** Single `StackTrace` struct on the VM with a fixed-size array of `{ const char* name, uint32_t line }` entries. Overwritten each time a new error is created. No heap allocation needed.
- **`try` compilation strategy:**
  ```
  compile_block(body)         ; body leaves result on stack
  OP_JUMP_IF_ERROR <handler>  ; if error, jump to handler
  OP_JUMP <end>               ; skip handler
  handler:
    ; error value is on stack top, bind to local
    OP_SET_LOCAL <name_slot>
    compile_block(handler)
  end:
  ```
- **`OP_CHECK_ERROR` inside `try`:** The compiler needs to track whether it's currently inside a `try` body. If so, `OP_CHECK_ERROR` jumps to the handler instead of returning from the function. This can be achieved by making `OP_CHECK_ERROR` take an operand (jump offset to handler or 0 for function-return).

## Success Metrics

- All new builtins (`error`, `error?`, `error-val`, `try`, `stack-trace`) work as specified
- Errors propagate consistently through ALL operations without manual checking
- Functions automatically return errors when not inside `try`
- Stack traces correctly identify the originating function and line number
- All existing M1-M8 tests pass without regression
- Error handling adds negligible overhead to the non-error fast path

## Open Questions

- Should `OP_CHECK_ERROR` also be emitted at top-level (outside any function)? If so, what does "return" mean at top-level — halt execution with the error?
- Should `error-val` on a non-error value be a no-op (return the value) or itself an error?
- Should `print` of an error value also print the stack trace, or only show the payload?
- What format should `stack-trace` use? Simple text, or a structured value (vector of maps)?
- Should the VM's `error_message` (fatal runtime error) mechanism be unified with value-level errors, or kept separate?
