# PRD: CPS Recursive Suspension & Concurrent Mutation Bugs

## Introduction

Two runtime bugs exist in the JACL CPS/concurrency system that were discovered during M16 (gc_high_churn.jacl) and documented as workarounds in `progress.txt`:

1. **Recursive CPS suspension bug:** When a proc recursively calls itself and the body contains suspending operations (`parallel`, `spawn`, `await`), the result is a spurious `<error: error>` instead of the correct value.
2. **Concurrent `set` mutation bug:** `set` (mutable variable assignment) inside spawned/parallel CPS closures running on worker threads has no effect — the mutation only applies to the worker's isolated VM environment and never propagates back.

Both bugs were explicitly listed as non-goals in the prior `prd-cps-bugfixes.md` (lines 103-104). This PRD addresses them.

## Goals

- Diagnose and fix the recursive CPS suspension error propagation bug
- Diagnose and fix (or explicitly restrict) `set` mutation in concurrent CPS closures
- Add regression tests for the previously-broken patterns
- Simplify `gc_high_churn.jacl` to use the now-fixed patterns where applicable

## User Stories

### US-001: Diagnose recursive CPS suspension root cause
**Description:** As a runtime developer, I need to identify the exact code path that produces `<error: error>` when a recursive proc body contains suspending operations, so that the fix is targeted and correct.

**Acceptance Criteria:**
- [ ] Create a minimal `.jacl` reproducer that demonstrates the bug (e.g., recursive `fib` using `spawn`/`await` in the body)
- [ ] Trace the execution through `compiler__compile_suspending_call_cps()` for the recursive call — document whether the issue is in tail-call continuation passing (lines 1189-1194) or upvalue capture in `compiler__emit_continuation()` (lines 894-933)
- [ ] Trace the runtime execution through `runtime__spawn_task_exec` / `runtime__continuation_task_exec` — identify where the `jacl_set_error(jacl_inline_string("error", 5))` at lines 1094 or 1177 of `runtime.c` gets triggered
- [ ] Document findings in a comment at the top of the fix commit
- [ ] No code changes in this story — diagnosis only

### US-002: Fix recursive CPS suspension error
**Description:** As a JACL developer, I want recursive procs with suspending operations in their body to return correct results instead of `<error: error>`, so that recursion and concurrency can be composed naturally.

**Acceptance Criteria:**
- [ ] The following pattern produces correct output instead of `<error: error>`:
  ```jacl
  # mode: concurrent
  proc work [n] {
    def f [spawn { [+ $n 1] }]
    await $f
  }
  proc loop [n] {
    if [<= $n 0]
      0
    else {
      def r [work $n]
      def rest [loop [- $n 1]]
      [+ $r $rest]
    }
  }
  print [loop 5]
  ```
- [ ] Fix is in `compiler.c` and/or `runtime.c` (wherever the root cause from US-001 lies)
- [ ] Existing CPS tests all pass (`cps_*.jacl`, `spawn_*.jacl`, `parallel_*.jacl`)
- [ ] `build.sh` passes (all test suites green)
- [ ] Compiles without warnings

### US-003: Add recursive CPS suspension regression tests
**Description:** As a runtime developer, I need regression tests for recursive procs with suspending bodies so the bug doesn't regress.

**Acceptance Criteria:**
- [ ] New test `test/jacl/cps_recursive_spawn.jacl`: recursive proc that uses `spawn`/`await` in its body, verifies correct result (e.g., recursive sum or factorial with async steps)
- [ ] Test uses `# mode: concurrent` header
- [ ] Test has `# expect:` headers with correct expected output
- [ ] Test exercises at least 3 levels of recursion depth
- [ ] All existing tests pass
- [ ] `build.sh` passes

### US-004: Diagnose `set` mutation in concurrent CPS closures
**Description:** As a runtime developer, I need to confirm whether `set` in concurrent CPS closures is fundamentally broken (per-worker VM isolation) or has a simpler fix, so we can decide the right approach.

**Acceptance Criteria:**
- [ ] Create a minimal `.jacl` reproducer that demonstrates the bug (e.g., `mut x 0`, spawn that does `set! x 42`, await, print `$x` — still shows 0)
- [ ] Confirm whether the issue is per-worker `vm->env` isolation in `runtime__init_worker_vm()` (runtime.c lines 98-140)
- [ ] Confirm whether `OP_SET_GLOBAL` in `vm.c` (lines 334-349) modifies only the local worker's `env`
- [ ] Determine if the fix requires shared mutable state (complex, needs synchronization) or if a compile-time error is safer
- [ ] Document findings in a comment at the top of the fix commit
- [ ] No code changes in this story — diagnosis only

### US-005: Handle `set` in concurrent mode
**Description:** As a JACL developer, I need `set` in concurrent CPS closures to either work correctly or produce a clear compile-time error, so that silent data loss doesn't occur.

**Acceptance Criteria:**
- [ ] Based on US-004 findings, implement ONE of:
  - **Option A (compile-time error):** `compiler.c` emits a compile error when `set` targets a global/outer variable inside a CPS-transformed body that will run on a worker thread. Error message: `"cannot use 'set' on '%s' inside concurrent task (use 'def' instead)"`
  - **Option B (make it work):** Add shared mutable state mechanism so `set` inside worker closures propagates back to the parent scope after `await`
- [ ] Whichever option is chosen, document the rationale in a code comment
- [ ] Existing CPS and mutation tests pass
- [ ] `build.sh` passes
- [ ] Compiles without warnings

### US-006: Add `set` concurrent mode regression tests
**Description:** As a runtime developer, I need regression tests for `set` behavior in concurrent mode.

**Acceptance Criteria:**
- [ ] If Option A (compile-time error): new test `test/jacl/set_concurrent_error.jacl` with `# expect_error:` header verifying the compile error message
- [ ] If Option B (make it work): new test `test/jacl/set_concurrent_propagate.jacl` with `# mode: concurrent` verifying mutation is visible after `await`
- [ ] Test covers `set` inside `spawn { ... }` body
- [ ] Test covers `set` inside `parallel { ... }` body
- [ ] All existing tests pass
- [ ] `build.sh` passes

### US-007: Simplify gc_high_churn.jacl to use fixed patterns
**Description:** As a runtime developer, I want to update the gc_high_churn.jacl test to use the now-fixed patterns (recursive CPS, etc.) where applicable, proving the fixes work under GC pressure.

**Acceptance Criteria:**
- [ ] Review `test/jacl/gc_high_churn.jacl` for workarounds that were needed due to these bugs
- [ ] Replace flat sequential workarounds with recursive or natural patterns where the fix enables it
- [ ] Test still achieves 500+ task executions
- [ ] Test still uses `# mode: concurrent` header
- [ ] All `# expect:` values remain correct
- [ ] Test runs in under 10 seconds
- [ ] All existing tests pass
- [ ] `build.sh` passes

### US-008: Add C-level CPS transform unit tests
**Description:** As a runtime developer, I need C-level unit tests that verify CPS transformation correctness for recursive suspending procs, to complement the .jacl integration tests.

**Acceptance Criteria:**
- [ ] New test in `test/test_m13.c` (or appropriate test file): compile a recursive proc with suspending body, verify the generated bytecode has correct continuation passing for the recursive call
- [ ] Test verifies upvalue capture includes necessary variables for nested suspension closures in the recursive body
- [ ] Test verifies the tail-call path in `compiler__compile_suspending_call_cps()` correctly passes `__k`
- [ ] All existing tests pass
- [ ] `build.sh` passes
- [ ] Compiles without warnings

## Functional Requirements

- FR-1: Recursive procs whose bodies contain `spawn`, `await`, `parallel`, or `race` must produce correct results when called recursively, not `<error: error>`
- FR-2: The continuation chain in recursive CPS calls must correctly propagate return values back through all recursion levels
- FR-3: `set` mutation inside concurrent CPS closures must either work correctly (propagating changes) or produce a clear compile-time error — silent data loss is not acceptable
- FR-4: All fixes must maintain backward compatibility with existing M0-M16 tests
- FR-5: The `gc_high_churn.jacl` workarounds should be simplified where fixes enable it

## Non-Goals

- No changes to the CPS transformation architecture itself (continuation-passing style remains the approach)
- No new concurrency primitives (channels, shared memory, etc.)
- No changes to the GC or block pool
- No performance optimization of CPS closures (correctness first)
- No support for mutation across `parallel` branches (independent parallel branches should remain isolated)

## Technical Considerations

- **Compiler investigation (Bug 1):** The likely culprit is `compiler__compile_suspending_call_cps()` in `compiler.c` (lines 1141-1199). When `remaining_count == 0` (tail call), the compiler passes the parent's `__k` directly. For recursive calls where the body itself creates intermediate continuations, this chain may not properly thread results back. The `<error: error>` is generated at `runtime.c` lines 1094 and 1177 when `vm__run()` doesn't return `VM_OK` or when the continuation wasn't properly called.
- **Per-worker isolation (Bug 2):** Each worker thread has its own `VM` with isolated `env` (runtime.c lines 98-140). `OP_SET_GLOBAL` modifies only the local worker's `vm->env`. There is no shared global environment — proc definitions are hoisted before CPS execution (compiler.c lines 4118-4135), but mutable variables are not. A compile-time error (Option A) is likely simpler and safer than shared mutable state.
- **Unity build order:** `src/jacl.c` includes files in dependency order. Changes to `compiler.c` and `runtime.c` must respect this.
- **Inline string limit:** Proc names ≤ 7 bytes. Test procs must use short names.
- **Existing workaround documentation:** Progress.txt Codebase Patterns section (lines 8-9) documents both bugs. Update these patterns after fixing.

## Success Metrics

- Recursive procs with suspending bodies produce correct results (not `<error: error>`)
- `set` in concurrent mode either works or fails loudly at compile time
- `gc_high_churn.jacl` simplified to use natural patterns
- Zero regressions: all existing test suites pass
- Both bugs have .jacl integration tests and C-level unit tests

## Open Questions

- **Bug 2 approach:** Should `set` in concurrent mode produce a compile-time error (Option A, simpler) or actually work via shared state (Option B, more capable but complex)? US-004 diagnosis will inform this decision.
- **Recursion depth limits:** Should there be a CPS recursion depth limit to prevent stack overflow in deeply recursive suspending procs? (Likely out of scope — address separately if needed.)
