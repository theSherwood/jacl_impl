# PRD: For Loop Completion & Control Flow

## Introduction

The JACL `for` loop currently supports three forms (collection + implicit `$it`, collection + explicit binding, and HOF callback). The language spec (SYNTAX_REDESIGN.md) defines an additional C-style form and control flow keywords (`break`, `continue`) that are not yet implemented. Additionally, for-loop block bodies are currently closure-wrapped, which breaks the spec requirement that `return` in a block body exits the enclosing proc. This PRD covers adding C-style `for`, `break`/`continue`, fixing `return` semantics, and adding comprehensive e2e tests.

## Goals

- Implement C-style `for {init; cond; step} { body }` loops
- Implement `break` and `continue` as compiled control flow (not string literals)
- Support `break $value` to return a value from the `for`/`while` expression
- Inline block-form `for` bodies so `return` exits the enclosing proc (matching `while` behavior)
- Add e2e tests covering all `for` forms, `break`, `continue`, and `return` semantics
- Maintain backward compatibility with existing `for` usage

## User Stories

### US-001: Compile `break` as control flow
**Description:** As a JACL developer, I want `break` to exit the nearest enclosing `for` or `while` loop so that I can terminate iteration early.

**Acceptance Criteria:**
- [ ] Parser produces an `AST_BREAK` node (not `AST_LIT_STRING`) when encountering `break`
- [ ] `break` with no argument exits the loop, loop expression evaluates to nil
- [ ] `break $value` exits the loop, loop expression evaluates to `$value`
- [ ] Compiler emits a jump instruction that targets the instruction after the loop
- [ ] VM executes the jump correctly
- [ ] `break` outside any loop is a compile-time error
- [ ] `break` works in both `for` and `while` loops
- [ ] Nested loops: `break` exits only the innermost loop
- [ ] E2e test: `for` with early `break` produces correct output
- [ ] E2e test: `while` with `break` produces correct output
- [ ] E2e test: nested loop `break` exits only inner loop
- [ ] E2e test: `break $value` returns the value from the loop expression

### US-002: Compile `continue` as control flow
**Description:** As a JACL developer, I want `continue` to skip to the next iteration of the nearest enclosing loop so that I can skip specific elements during iteration.

**Acceptance Criteria:**
- [ ] Parser produces an `AST_CONTINUE` node (not `AST_LIT_STRING`) when encountering `continue`
- [ ] Compiler emits a jump instruction that targets the loop's increment/next-iteration point
- [ ] VM executes the jump correctly
- [ ] `continue` outside any loop is a compile-time error
- [ ] `continue` works in both `for` and `while` loops
- [ ] Nested loops: `continue` affects only the innermost loop
- [ ] E2e test: `for` with `continue` skips correct elements
- [ ] E2e test: `while` with `continue` skips to next iteration
- [ ] E2e test: nested loop `continue` affects only inner loop

### US-003: Inline block-form `for` bodies (fix `return` semantics)
**Description:** As a JACL developer, I want `return` inside a `for` block body to exit the enclosing proc, not just the loop body, matching the spec and `while` loop behavior.

**Acceptance Criteria:**
- [ ] Block-form `for $items { body }` inlines the body into the enclosing function's bytecode (not wrapped in a closure)
- [ ] Block-form `for $items name { body }` also inlines the body
- [ ] `return` inside an inlined for-body exits the enclosing `proc`
- [ ] `return $value` inside an inlined for-body returns the value from the enclosing `proc`
- [ ] Lambda-form `for $items [\\ body]` remains closure-based — `return` exits the lambda
- [ ] HOF-form `for $items $callback` remains closure-based — `return` exits the callback proc
- [ ] Existing for-loop tests continue to pass (implicit `$it`, explicit binding, HOF, empty vec, upvalue capture)
- [ ] E2e test: `return` from inside a `for` block exits the enclosing proc
- [ ] E2e test: `return $value` from inside a `for` block returns the value from the proc
- [ ] E2e test: `return` from lambda-form `for` only exits the lambda

### US-004: C-style `for` loop
**Description:** As a JACL developer, I want to write C-style for loops with init, condition, and step expressions so that I can do counted/index-based iteration.

**Acceptance Criteria:**
- [ ] Syntax: `for {init; cond; step} { body }` where init, cond, step are separated by `;`
- [ ] Compiler distinguishes this form: first arg is a `{}` block → C-style loop
- [ ] Init expression runs once before the loop
- [ ] Condition is checked before each iteration; loop exits when condition is falsy
- [ ] Step expression runs after each iteration body
- [ ] Loop variable declared in init is scoped to the loop (not visible after loop ends)
- [ ] `break` works inside C-style for
- [ ] `continue` skips to the step expression, then re-checks condition
- [ ] `return` exits the enclosing proc
- [ ] E2e test: basic counting `for {mut i 0; ($i < 5); set i ($i + 1)} { print $i }` → "0\n1\n2\n3\n4\n"
- [ ] E2e test: C-style for with `break`
- [ ] E2e test: C-style for with `continue`
- [ ] E2e test: nested C-style for loops

### US-005: Comprehensive e2e test suite for all `for` forms
**Description:** As a maintainer, I want a complete test suite covering all `for` forms and control flow interactions to prevent regressions.

**Acceptance Criteria:**
- [ ] Tests in `test/test_compiler.c` (compile-and-run style checking stdout output)
- [ ] Test: collection for + implicit `$it` (existing — verify still passes)
- [ ] Test: collection for + explicit binding (existing — verify still passes)
- [ ] Test: collection for + HOF callback (existing — verify still passes)
- [ ] Test: for over empty collection (existing — verify still passes)
- [ ] Test: upvalue capture in for body (existing — verify still passes)
- [ ] Test: C-style for basic counting
- [ ] Test: C-style for with break
- [ ] Test: C-style for with continue
- [ ] Test: C-style for nested
- [ ] Test: break exits for loop (value is nil)
- [ ] Test: break $value returns value from for expression
- [ ] Test: break exits while loop
- [ ] Test: break in nested loops exits only innermost
- [ ] Test: continue in for skips element
- [ ] Test: continue in while skips to next check
- [ ] Test: continue in nested loops affects only innermost
- [ ] Test: return from for block exits enclosing proc
- [ ] Test: return from lambda-form for exits only the lambda
- [ ] Test: break outside loop is compile error
- [ ] Test: continue outside loop is compile error

## Functional Requirements

- FR-1: `break` must be parsed as a dedicated AST node, not a string literal
- FR-2: `continue` must be parsed as a dedicated AST node, not a string literal
- FR-3: `break` with no argument evaluates the loop expression to nil
- FR-4: `break $value` evaluates the loop expression to the given value
- FR-5: `continue` jumps to the loop's next-iteration point (step in C-style, next element in collection/range)
- FR-6: `break` and `continue` outside any loop context must be compile-time errors
- FR-7: Block-form for bodies (`for $items { body }`, `for $items name { body }`) must be inlined into the enclosing function's bytecode, not closure-wrapped
- FR-8: Lambda-form for (`for $items [\\ ...]`) remains closure-based — `return` exits the lambda, which is a proc
- FR-9: C-style for must parse first arg as a block with three `;`-separated sections: init, condition, step
- FR-10: C-style for loop variable is scoped to the loop
- FR-11: All existing for-loop tests must continue to pass without modification
- FR-14: The compiler must track a loop context stack to resolve `break`/`continue` jump targets and to detect usage outside loops

## Non-Goals

- No `break`/`continue` with labels (e.g., `break :outer`) — only innermost loop
- No infinite `for {}` loop syntax — use `while true { }` instead
- No range operators (`..<`, `..=`) or range-based iteration — deferred to a future PRD
- No `for` over maps with key-value destructuring (existing map iteration via HOF is sufficient)
- No changes to `while` loop implementation (it already works correctly with inlined bodies)

## Technical Considerations

- **Compiler loop context stack:** The compiler needs a stack of loop contexts to track where `break` should jump to and where `continue` should jump to. Each `for`/`while` pushes a context; `break`/`continue` reference the top. This is a common pattern in bytecode compilers.
- **Jump patching:** `break` emits a forward jump whose target isn't known at emit time. Use placeholder bytes and patch them when the loop end is known (same technique as `if`/`else`).
- **Inlining for-bodies:** The current implementation wraps block bodies in closures and calls the `for` builtin. Inlining means the compiler must emit the iteration loop directly (like `while`), binding `$it` or the named variable as a local in the enclosing scope.

## Success Metrics

- All e2e tests in US-005 pass
- All existing for-loop tests pass unchanged
- `break`, `continue`, and `return` behave identically in `for` and `while` block bodies
- No new memory leaks (test with existing GC/leak detection tooling)

## Open Questions

None — all questions resolved.
