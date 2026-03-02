# PRD: E2E Test Coverage for Concurrency & Type System

## Introduction

JACL's M13 concurrency primitives (spawn, await, parallel, race) and M11 static type system have C-level integration tests but zero `.jacl` end-to-end harness tests. The existing `test/test_jacl_harness.c` runs `.jacl` files from `test/jacl/` through `jacl_run()` with `# expect:` / `# expect-error:` header assertions, providing true end-to-end coverage from source text to output. This PRD adds comprehensive `.jacl` e2e tests for M13 concurrency and fills gaps in M11 type system coverage. It also extends the harness to support multi-threaded execution via `rt_run_to_completion`.

## Goals

- Add `.jacl` e2e tests for all M13 concurrency primitives: spawn, await, parallel, race
- Add `.jacl` e2e tests for CPS transform correctness (variable capture, suspension propagation, control flow)
- Add `.jacl` e2e tests for error handling through async code
- Extend the harness to support multi-threaded execution via a `# mode: concurrent` directive
- Fill M11 static type system e2e gaps: comprehensive numeric types, typed closures, type conversion edge cases
- All new `.jacl` files follow existing conventions: `# expect:` / `# expect-error:` headers, alphabetical naming

## User Stories

### US-013: Extend harness for multi-threaded execution
**Description:** As a developer, I need the `.jacl` test harness to support running tests with the runtime worker pool so that concurrency tests exercise real multi-threaded execution via `rt_run_to_completion`.

**Acceptance Criteria:**
- Add `# mode: concurrent` header directive to `.jacl` file format — when present, the harness uses `runtime_init` + `rt_run_to_completion` instead of plain `jacl_run()`
- Without `# mode: concurrent`, behavior is unchanged (uses `jacl_run()` for single-threaded execution)
- The concurrent mode must: initialize a `Runtime` with worker threads, compile the source to get a `CompileResult`, handle top-level CPS (suspending code produces a closure), submit via `rt_run_to_completion`, capture print output via the same `PrintCapture` mechanism, and clean up with `runtime_destroy`
- Print capture must work from worker threads — worker VMs must have `print_fn` / `print_ctx` set to the shared `PrintCapture` (with mutex protection if needed, since output ordering from concurrent tasks is non-deterministic)
- The `# mode: concurrent` directive is parsed alongside existing `# expect:` / `# expect-error:` headers
- Update `build.sh` harness entry to include `-lpthread` flag
- All existing `.jacl` tests continue to pass unchanged
- All tests pass (`build.sh`)

**Notes:** `rt_run_to_completion` takes `(Runtime*, JaclClosure*, arena_t*)` and blocks until the program completion future resolves. The harness needs to compile the source, detect if the top-level is suspending (via `CompileResult.suspending`), wrap in the CPS closure pattern, and submit. Look at how `jacl_run` in `vm.c` handles `cr.suspending` for the pattern to replicate. Worker VM initialization uses `runtime__init_worker_vm` which is internal to the runtime — the harness should use the public `runtime_init` / `runtime_destroy` API and let workers self-initialize.

### US-014: Spawn and await e2e tests
**Description:** As a developer, I need `.jacl` e2e tests for spawn and await so that the full pipeline from JACL source to concurrent execution is validated.

**Acceptance Criteria:**
- `spawn_basic.jacl` — spawn a simple computation, await it, print result (`# mode: concurrent`)
- `spawn_value.jacl` — spawn returns a future, await extracts the value
- `spawn_multi.jacl` — spawn multiple tasks, await each sequentially, print all results in order
- `spawn_nested.jacl` — spawn a task that itself spawns and awaits an inner task
- `spawn_heavy.jacl` — spawn a task with heavy allocation (vector/map construction) to exercise GC under concurrency (`# mode: concurrent`)
- `await_resolved.jacl` — await a future that has already resolved (no deadlock)
- `await_error.jacl` — spawn a task that errors, await it, check with `error?` predicate
- All files in `test/jacl/`, all use `# expect:` headers, all pass via harness
- All tests pass (`build.sh`)

**Notes:** JACL concurrency works single-threaded via `jacl_run()` (recursive vm__run for spawn) and multi-threaded via runtime. Tests without `# mode: concurrent` exercise single-threaded path; tests with it exercise the worker pool. Include a mix. Remember: suspending code at top level must be wrapped in `proc main [] { ... }; main` pattern. Use `to-string` for typed numeric output. Helper procs must be defined at top level (not inside CPS-transformed proc bodies).

### US-015: Parallel primitive e2e tests
**Description:** As a developer, I need `.jacl` e2e tests for the parallel primitive so that fork-join semantics are validated end-to-end.

**Acceptance Criteria:**
- `parallel_two.jacl` — parallel with 2 bodies, print result vector
- `parallel_order.jacl` — parallel preserves input order in result vector (not completion order)
- `parallel_error.jacl` — one parallel body errors, result is error (first-error-wins), check with `error?`
- `parallel_with_await.jacl` — parallel bodies that internally use spawn+await
- `parallel_concurrent.jacl` — parallel with `# mode: concurrent` to verify real worker execution
- All files in `test/jacl/`, all use `# expect:` or `# expect-error:` headers, all pass
- All tests pass (`build.sh`)

**Notes:** Parallel returns a JACL vector of results. `[parallel { expr1 } { expr2 }]` is a single-line command form — JACL parser treats newlines inside `[...]` as statement separators, so multi-line parallel may cause parse errors. Use single-line format or the `proc main` wrapper pattern.

### US-016: Race primitive e2e tests
**Description:** As a developer, I need `.jacl` e2e tests for the race primitive so that first-wins semantics are validated end-to-end.

**Acceptance Criteria:**
- `race_basic.jacl` — race two bodies, print the winner's result
- `race_error.jacl` — race where the winning body errors, result is error
- `race_concurrent.jacl` — race with `# mode: concurrent` for real parallel execution
- All files in `test/jacl/`, all use `# expect:` headers, all pass
- All tests pass (`build.sh`)

**Notes:** In single-threaded mode, the first body always wins (sequential execution). In concurrent mode, the actual faster body wins — tests should account for non-deterministic winner by either making one body trivially faster or checking that the result is one of the expected values. The `# expect:` header is line-exact, so non-deterministic output needs careful test design.

### US-017: CPS transform correctness e2e tests
**Description:** As a developer, I need `.jacl` e2e tests for CPS transform edge cases so that variable capture, suspension propagation, and control flow through continuations are validated end-to-end.

**Acceptance Criteria:**
- `cps_variable_capture.jacl` — variables defined before await are accessible after await
- `cps_sequential.jacl` — multiple sequential awaits produce correct intermediate results
- `cps_if_suspend.jacl` — await inside if branch, code after if uses result
- `cps_deep_chain.jacl` — 5+ chained awaits (deep continuation chain)
- `cps_propagation.jacl` — proc A calls suspending proc B which calls suspending proc C (transitive CPS)
- `cps_closure_capture.jacl` — closures capturing variables across suspension points
- `cps_nested_expr.jacl` — await nested inside expression arguments: `[+ [await $f1] [await $f2]]`
- All files in `test/jacl/`, all use `# expect:` headers, all pass
- All tests pass (`build.sh`)

**Notes:** These tests validate the compiler's CPS transform correctness rather than the runtime. All should work in single-threaded mode via `jacl_run()`. Use the `proc main [] { ... }; main` wrapper for top-level suspending code. Remember: JACL `true`/`false` are bare words (strings, both truthy) — use comparison operators for boolean conditions.

### US-018: Async error handling e2e tests
**Description:** As a developer, I need `.jacl` e2e tests for error handling through async code paths so that error propagation via futures and CPS continuations is validated.

**Acceptance Criteria:**
- `async_error_spawn.jacl` — spawn a task that calls `[error "msg"]`, await it, verify `error?` is true and `error-val` returns the message
- `async_error_propagation.jacl` — error in a deeply nested async call chain propagates to the top-level awaiter
- `async_error_parallel.jacl` — one body in parallel errors, entire parallel result is error
- `async_try_before_spawn.jacl` — try/catch wrapping non-async code works alongside spawn/await (try/catch cannot contain suspension points, but can wrap sync code in an async program)
- All files in `test/jacl/`, all use `# expect:` headers, all pass
- All tests pass (`build.sh`)

**Notes:** JACL try/catch syntax is `[try { body } e { handler }]`. Suspension inside try/catch is a compile-time error — tests should NOT put await inside try. Error propagation through futures: `jacl_future_error` stores the error, `await` on an errored future passes the error value to the continuation.

### US-019: M11 type system e2e gap coverage
**Description:** As a developer, I need additional `.jacl` e2e tests for the M11 static type system to cover numeric types, typed closures, and type conversion edge cases not covered by existing tests.

**Acceptance Criteria:**
- `typed_i32.jacl` — i32 arithmetic, overflow behavior, type checking
- `typed_u64.jacl` — u64 arithmetic, type checking, large values
- `typed_f64.jacl` — f64 arithmetic, decimal output, type checking
- `typed_f32.jacl` — f32 arithmetic, type checking
- `typed_conversion_chain.jacl` — chained type conversions: i32 → i64 → f64
- `typed_conversion_error.jacl` — invalid type conversion produces error
- `typed_closure.jacl` — closure capturing typed values, return type checking
- `typed_collection.jacl` — typed values stored in and retrieved from vectors/maps
- All files in `test/jacl/`, all use `# expect:` or `# expect-error:` headers, all pass
- All tests pass (`build.sh`)

**Notes:** Existing M11 `.jacl` tests cover: `typed_def_basic` (i64 def), `typed_def_error` (type mismatch), `typed_mut_basic` (i64 mut), `typed_proc` (typed proc), `typed_proc_error` (arg type mismatch), `typed_arithmetic` (i64 addition), `typed_arith_error` (i64+f64 error), `typed_set_error` (set! type mismatch), `type_conversions` (i32 to i64). Gaps: no f64/f32/u64 coverage, no typed closures, no type conversion errors, no chained conversions, no typed values in collections.

## Functional Requirements

- FR-1: The `# mode: concurrent` directive is parsed from `.jacl` file headers alongside `# expect:` / `# expect-error:` directives
- FR-2: When `# mode: concurrent` is present, the harness initializes a `Runtime`, compiles source, and executes via `rt_run_to_completion`
- FR-3: Worker thread print output is captured via `PrintCapture` with appropriate synchronization
- FR-4: All new `.jacl` test files follow naming convention: `feature_variant.jacl` (snake_case, descriptive)
- FR-5: All `.jacl` files are self-contained — no dependencies between test files
- FR-6: Tests requiring deterministic output from concurrent execution either use single-threaded mode or design around non-deterministic ordering
- FR-7: The `build.sh` harness entry is updated to include `-lpthread` for runtime support

## Non-Goals

- No new language features — tests exercise existing M13 and M11 functionality only
- No cancellation testing (M13 has no cancellation support)
- No stress/performance testing in `.jacl` files — that stays in `test_m13.c` C tests
- No changes to the JACL language semantics or compiler
- No `.jacl` tests for internal GC behavior (not observable from language level)

## Technical Considerations

- The harness currently uses `jacl_run()` which handles single-threaded CPS internally — this already works for basic concurrency tests
- For `# mode: concurrent`, the harness must replicate what `jacl_run` does for suspending code but route through the runtime: compile → check `cr.suspending` → create completion future + resolve_k → call `rt_run_to_completion`
- Print output from worker threads needs synchronization — either a mutex around `capture_print` or accept that line ordering may vary
- Non-deterministic output ordering in concurrent tests: either test only for specific known-order outputs, or use `# expect-unordered:` (if adding) or stick to tests where output order is deterministic (single print at end)
- The `build.sh` harness entry currently has no extra flags (`"test/test_jacl_harness.c|jacl_harness|"`) — needs `-lpthread` appended

## Success Metrics

- All new `.jacl` files pass through the harness with zero failures
- M13 concurrency features have >20 `.jacl` e2e test files covering spawn, await, parallel, race, CPS, and error handling
- M11 type system has >15 total `.jacl` test files (8 existing + 8 new) covering all numeric types
- `build.sh` reports all suites passing with the new harness configuration
- No regressions in existing 131+ `.jacl` test files

## Open Questions

- Should we add a `# expect-unordered:` directive for tests where output lines can appear in any order (useful for concurrent tests)? This could simplify testing of truly parallel output.
- Should worker count for `# mode: concurrent` be configurable per test, or use a default (e.g., 4 workers)?
