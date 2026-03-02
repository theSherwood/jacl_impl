# PRD: CPS Edge Case Bugfixes

## Introduction

Three bugs were discovered during US-018 (Async error handling e2e tests) in the JACL compiler and runtime. These bugs cause segfaults, incorrect compilation, and silent error swallowing when CPS-transformed code interacts with spawn bodies, parallel body closures, and error propagation. Fixing them removes dangerous workarounds and makes the async/concurrency system robust.

## Goals

- Fix segfault caused by pre-await variable capture in parallel body closures (Bug #2)
- Fix incorrect compilation of spawn bodies calling named suspending procs (Bug #1)
- Fix silent error swallowing in parallel CPS completion path (Bug #3)
- Add defensive VM guards so future upvalue bugs produce clear errors instead of segfaults
- Update existing US-018 e2e tests to use the previously-broken patterns, proving fixes work end-to-end

## User Stories

### US-001: Eager upvalue capture in CPS continuations
**Description:** As a JACL developer, I want variables defined before an `await` to be accessible inside `parallel`/`race` body closures after that `await`, so that CPS-transformed code behaves like regular lexical scoping.

**Acceptance Criteria:**
- [ ] `compiler__emit_continuation()` in `src/compiler.c` proactively captures all live locals from the parent scope into the continuation closure's upvalue array
- [ ] Variables defined before a suspension point (e.g., `def x [await $f]`) are available as upvalues in nested closures (parallel bodies, race bodies, spawn bodies) compiled inside the continuation
- [ ] The following pattern no longer segfaults:
  ```jacl
  proc main [] {
    def f [spawn { 100 }]
    def x [await $f]
    def r [parallel { $x } { error "fail" }]
    print [error? $r]
  }
  ```
- [ ] Upvalue indices in nested closures correctly reference the continuation's upvalue array (transitive capture chain is valid)
- [ ] Update `test/jacl/async_error_parallel.jacl` to use `$x` (pre-await variable) inside a parallel body, replacing the workaround pattern
- [ ] Add unit test in `test/test_m13.c`: continuation captures all live locals (verify upvalue count includes unreferenced-by-continuation but referenced-by-nested-closure variables)
- [ ] Add e2e test: `test/jacl/cps_capture_parallel.jacl` — pre-await variable used in parallel body after await
- [ ] Existing CPS tests still pass (cps_variable_capture, cps_closure_capture, cps_propagation, parallel_with_await, etc.)
- [ ] `build.sh` passes (all test suites green)

### US-002: VM upvalue bounds checking
**Description:** As a JACL developer, I want the VM to produce a clear error message instead of a segfault when the compiler emits an invalid upvalue index, so that future compiler bugs are diagnosable.

**Acceptance Criteria:**
- [ ] `OP_CLOSURE` handler in `src/vm.c` validates `uv_index` against `frame->closure->upvalue_count` when `is_local == 0` (upvalue-from-parent path)
- [ ] `OP_CLOSURE` handler validates `uv_index` against `frame->stack_base + local_count` when `is_local == 1` (local capture path)
- [ ] Invalid index produces a `VM_RUNTIME_ERROR` with message like `"OP_CLOSURE: upvalue index %d out of bounds (parent has %d upvalues)"` instead of a segfault
- [ ] Add unit test in `test/test_m13.c`: crafted bytecode with out-of-bounds upvalue index triggers error (not crash)
- [ ] No performance impact on valid bytecode (checks are simple integer comparisons)
- [ ] `build.sh` passes

### US-003: Suspension detection for named proc calls
**Description:** As a JACL developer, I want `spawn { suspending_proc }` to correctly compile the spawn body as CPS when `suspending_proc` is a known suspending procedure, so that spawn bodies calling suspending procs work correctly.

**Acceptance Criteria:**
- [ ] `ast__contains_suspension()` in `src/compiler.c` accepts a `SuspensionMap*` parameter
- [ ] When the head of an `AST_COMMAND` is a string that matches a suspending proc in the map (via `suspension_map_lookup()`), the function returns `true`
- [ ] All call sites updated to pass `c->suspension_map` (approximately 4 sites: spawn body check, parallel body check, callback validation in transform/each/filter, top-level suspension check)
- [ ] The following pattern no longer errors:
  ```jacl
  proc deep [] {
    def f [spawn { error "deep-err" }]
    await $f
  }
  proc mid [] {
    def f [spawn { deep }]
    await $f
  }
  ```
- [ ] Update `test/jacl/async_error_propagation.jacl` to use named suspending proc calls from spawn bodies, replacing the inline nested spawn workaround
- [ ] Add e2e test: `test/jacl/spawn_susp_proc.jacl` — spawn body calling a named suspending proc
- [ ] Callback validation (transform/each/filter) still correctly rejects suspending callbacks
- [ ] `build.sh` passes

### US-004: Parallel CPS error propagation
**Description:** As a JACL developer, I want error values returned through `await` inside parallel bodies to be detected as errors by the parallel aggregation, so that `error?` returns `true` on the parallel result.

**Acceptance Criteria:**
- [ ] In `runtime__parallel_task_exec()` in `src/runtime.c`, the CPS path checks `jacl_is_error(task_result)` after retrieving `fut->result` in the `FUTURE_RESOLVED` case
- [ ] If the result is an error value, `task_errored` is set to `true` (triggering the existing first-error-wins aggregation)
- [ ] The following pattern now returns an error:
  ```jacl
  def r [parallel { 30 } { def f [spawn { error "par-err" }]; await $f }]
  print [error? $r]    # true (was: false)
  print [error-val $r]  # par-err (was: [vec 30 "par-err"])
  ```
- [ ] Update `test/jacl/async_error_parallel.jacl` to use a parallel body with spawn+await that errors, replacing the workaround
- [ ] Add e2e test: `test/jacl/parallel_await_error.jacl` — parallel body with spawn+await+error, verifies error? and error-val
- [ ] Non-CPS parallel error behavior unchanged (parallel_error.jacl still passes)
- [ ] `build.sh` passes

## Functional Requirements

- FR-1: `compiler__emit_continuation()` must capture all live locals (locals with `depth >= 1` in the parent compiler) as upvalues in the continuation closure, not just those directly referenced by the continuation body
- FR-2: Upvalue descriptors emitted for continuation closures must include entries for all live locals so that nested closures (parallel/race/spawn bodies) can resolve transitive upvalue references correctly
- FR-3: `ast__contains_suspension()` must accept a `SuspensionMap*` and check it for named proc calls in addition to literal `await`/`parallel`/`race` keywords
- FR-4: `ast__contains_suspension()` must skip proc names longer than 7 bytes (inline string limit — cannot be in the suspension map)
- FR-5: The `OP_CLOSURE` handler must validate upvalue indices before dereferencing and produce a runtime error for out-of-bounds access
- FR-6: `runtime__parallel_task_exec()` CPS path must check `jacl_is_error()` on `FUTURE_RESOLVED` results and set the error flag on the `ParallelAgg`
- FR-7: All modified code must maintain backward compatibility with existing M0-M13 tests and US-012 through US-018 e2e tests

## Non-Goals

- No changes to the CPS transform architecture itself (continuation-passing style remains the approach)
- No fix for recursive CPS tail call return value propagation (known limitation, separate issue)
- No fix for bare-word calls to suspending procs at tail position returning nil (known limitation, separate issue)
- No changes to the single-threaded GC path or concurrent GC
- No new opcodes or bytecode format changes
- No performance optimization of upvalue capture (eager capture trades memory for correctness)

## Technical Considerations

- **Unity build order:** `src/jacl.c` includes files in dependency order. `compiler.c` has access to `SuspensionMap` (defined within it). `runtime.c` has access to `jacl_is_error()` from `value.c`.
- **Upvalue memory cost:** Eager capture means continuation closures will have more upvalues than strictly necessary (every live local, not just referenced ones). For typical JACL procs with 3-5 locals, this adds ~40 bytes per continuation. Acceptable for correctness.
- **Inline string limit:** Proc names > 7 bytes cannot be represented as inline strings and are not in the suspension map. `ast__contains_suspension` must handle this gracefully (skip lookup for long names).
- **Test ordering:** The harness runs .jacl files alphabetically. New test file names should sort near related existing tests.
- **Thread safety:** The `runtime__parallel_task_exec` fix touches code that runs on worker threads. The `jacl_is_error()` check is a pure read on an atomic-safe tagged value — no synchronization needed.

## Success Metrics

- All three crash/error patterns from US-018 debugging are fixed and have passing e2e tests
- The US-018 workaround tests are updated to use the direct (previously-broken) patterns
- Zero regressions: all existing 157+ harness tests and 48 test suites pass
- Invalid upvalue indices produce clear error messages instead of segfaults

## Open Questions

None — all resolved:

- **Eager capture and dead locals:** Capture everything, including A-normalization temporaries. The extra ~8 bytes per dead temporary is negligible. Optimize later only if profiling shows it matters.
- **Upvalue-captured suspending procs in `ast__contains_suspension`:** No — `SuspensionMap` lookup covers all statically-known suspending procs. Indirect/dynamic calls (e.g., suspending procs passed as parameters) are already handled conservatively by Rule 2 in the suspension analysis. Threading Compiler state into `ast__contains_suspension` is not worth the complexity for this rare edge case.
