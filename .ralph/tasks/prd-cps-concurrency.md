# PRD: M13 — CPS Transform & Concurrency Primitives

## Introduction

Add the CPS (Continuation-Passing Style) compiler transform and user-facing concurrency primitives (`spawn`, `await`, `parallel`, `race`) to JACL, completing the concurrency half of the GC & concurrency co-design from GC_CONCURRENCY_DESIGN.md. M12 built the runtime infrastructure: worker thread pool with Chase-Lev work-stealing deques, concurrent epoch-based tracing GC, hybrid write barriers, and atomic CAS on atoms. M13 builds the language-level features on top.

The CPS transform is the central piece. The compiler identifies procedures that can suspend (those containing `await`, `parallel`, `race`, or calls to other suspending procedures), then rewrites them into continuation-closure chains. At each suspension point, the code after the suspension becomes a continuation closure that captures all live variables. This is what makes GC root enumeration correct without scanning VM stacks (per GC_CONCURRENCY_DESIGN.md Section 3): continuation closures capture all live state, and continuations are tasks on deques — which the GC already scans.

This PRD also includes GC improvements deferred from M12: adaptive threshold tuning and OOM escalation.

## Goals

- Implement full CPS compiler transform that rewrites suspending procedures into continuation-closure chains at suspension points
- Add suspension inference to the compiler: transitively determine which procedures can suspend from `await`/`parallel`/`race` usage
- Add `Future` as a first-class GC-managed value type with pending/resolved/error states and a waiter list
- Implement `spawn` (launch task on worker pool, return future), `await` (suspend until future resolves, resume continuation), `parallel` (fork N tasks, join all results into vector), `race` (fork N tasks, return first result)
- Add `run` as the synchronous-to-asynchronous bridge: blocks calling thread until async closure completes
- Remove VM stack root scanning from concurrent GC (CPS continuations are the roots, per GC_CONCURRENCY_DESIGN.md Section 7)
- Add adaptive GC threshold (survival-rate-based tuning)
- Add 3-tier OOM escalation (emergency GC, heap growth, panic with diagnostics)
- All existing M0-M12 tests pass, new concurrency tests demonstrate correctness under concurrent load

## User Stories

### US-001: Future type and GC integration

**Description:** As a developer, I need a `Future` value type so that `spawn` can return a handle that `await` uses to suspend and resume, and the GC can trace futures and their waiters.

**Acceptance Criteria:**
- [ ] Add `JACL_TAG_FUTURE` to the value tag enum in value.c
- [ ] Define `JaclFuture` struct with fields:
  - `_Atomic uint32_t state` — `FUTURE_PENDING` (0), `FUTURE_RESOLVED` (1), `FUTURE_ERROR` (2)
  - `_Atomic uint64_t result` — the resolved JaclVal (value or error)
  - `FutureWaiter* waiters` — linked list of waiter nodes, each holding a continuation closure
  - Synchronization for waiter registration vs. resolution race: mutex or spinlock per future
- [ ] Define `FutureWaiter` struct: `{ JaclVal continuation; FutureWaiter* next; }`
- [ ] Add `OBJ_FUTURE` to GCObjType enum in gc.c
- [ ] Add `OBJ_FUTURE_WAITER` to GCObjType enum
- [ ] Allocate futures via `gc_alloc(heap, OBJ_FUTURE, sizeof(JaclFuture))`
- [ ] Allocate waiter nodes via `gc_alloc(heap, OBJ_FUTURE_WAITER, sizeof(FutureWaiter))`
- [ ] GC trace strategy for `OBJ_FUTURE`: trace `result` (if resolved/errored) and traverse `waiters` list (trace each continuation closure)
- [ ] GC trace strategy for `OBJ_FUTURE_WAITER`: trace continuation closure + next pointer
- [ ] `jacl_future(ThreadHeap*)` — creates a pending future, returns JaclVal
- [ ] `jacl_future_resolve(JaclFuture*, JaclVal result)` — atomically sets result and state to RESOLVED; returns waiter list (caller schedules them as tasks)
- [ ] `jacl_future_error(JaclFuture*, JaclVal error)` — same as resolve but sets state to ERROR
- [ ] `jacl_future_add_waiter(JaclFuture*, JaclVal continuation, ThreadHeap*)` — if pending, prepends waiter node and returns true; if already resolved/errored, returns false (caller should schedule continuation immediately with the result)
- [ ] `jacl_is_future(JaclVal)` predicate
- [ ] Add `future?` builtin to VM
- [ ] Write barrier on resolution: fire `gc_write_barrier(old=JACL_NIL, new=result)` when storing result into future
- [ ] Unit tests: create pending future, resolve it, verify state/result; add waiters to pending future, resolve, verify waiters returned; add waiter to already-resolved future returns false; error a future; GC traces live future with waiters
- [ ] All existing tests pass (build.sh)

### US-002: Suspension analysis in compiler

**Description:** As a developer, I need the compiler to determine which procedures can suspend so the CPS transform knows which procs to rewrite and illegal suspension contexts produce compile-time errors.

**Acceptance Criteria:**
- [ ] Add `bool suspends` field to `Local` struct in compiler.c
- [ ] Add `bool suspends` field to `GlobalArity` struct
- [ ] Add `bool suspends` field to `Upvalue` struct (for captured proc-typed variables)
- [ ] Implement pre-compilation suspension analysis pass:
  1. Walk all proc/fn definitions in the AST (including nested definitions)
  2. Mark procs that directly contain `await`, `parallel`, or `race` commands as suspending
  3. Propagate transitively: mark any proc that calls a known-suspending proc as suspending
  4. Iterate until fixpoint (no changes in a full pass)
- [ ] Store suspension map for use during compilation
- [ ] During compilation, when resolving a call, look up callee's suspension status and propagate to the enclosing proc
- [ ] For indirect calls to closures stored in variables where suspension status is unknown: **conservatively assume suspending**. The enclosing proc gets CPS-transformed. This is safe (may over-transform, but never misses a suspension point). Explicit annotations in a future PRD will allow opting out
- [ ] Compile-time error: suspension point (`await`, `parallel`, `race`) appears inside a closure passed to a non-suspending higher-order builtin (`map`, `filter`, `reduce`, `each`) — error message: "cannot suspend inside non-suspending callback"
- [ ] Compile-time error: a suspending closure is passed as argument to a non-suspending higher-order builtin
- [ ] Compile-time error: suspension point inside `try`/`catch` block — "cannot suspend inside try/catch; use error capture on futures instead"
- [ ] `spawn` is NOT a suspension point — it can appear in any proc
- [ ] `run` is NOT a suspension point — it blocks synchronously
- [ ] Unit tests: verify suspension correctly inferred for direct use, transitive calls, nested procs; verify indirect call to unknown closure is conservatively suspending; verify compile errors for illegal contexts
- [ ] All existing tests pass (build.sh) — no existing code uses suspension points, so all procs are inferred non-suspending

### US-003: CPS transform — sequential code

**Description:** As a developer, I need the compiler to transform suspending procedures with suspension points in sequential (straight-line) code into continuation-closure chains, so that `await` can suspend and resume via continuations.

**Acceptance Criteria:**
- [ ] CPS-transformed suspending procs take a hidden continuation parameter `__k` — the "result continuation" called with the proc's final result instead of returning
- [ ] When compiling a suspending proc body, the compiler splits at each suspension point: everything after the point becomes a continuation closure capturing all live variables
- [ ] Single `await` in sequence:
  ```
  # Input:
  proc foo []
    def x [compute]
    def y [await [spawn (fn [] [work])]]
    [+ $x $y]

  # CPS output (conceptual):
  proc foo [__k]
    def x [compute]
    def __f [spawn (fn [] [work])]
    __await(__f, continuation(y) { __k([+ x y]) })
    # continuation captures x, receives y as parameter
  ```
- [ ] Multiple sequential `await`s create chained continuations:
  ```
  # Input:
  proc bar []
    def a [await $f1]
    def b [await $f2]
    [+ $a $b]

  # CPS output:
  proc bar [__k]
    __await(f1, continuation_1(a) {
      __await(f2, continuation_2(b) {  # captures a
        __k([+ a b])
      })
    })
  ```
- [ ] Variable liveness analysis at each suspension point: only variables used after the point are captured by the continuation closure
- [ ] Continuation closures use the existing closure/upvalue compilation mechanism (nested Compiler context, upvalue resolution)
- [ ] Non-suspending code before the first suspension point executes normally (no CPS overhead)
- [ ] The `__k` continuation parameter is passed through: callers of suspending procs pass their own continuation
- [ ] For calls from non-suspending to suspending procs via `run`: `run` provides `__k = resolve_future`
- [ ] Unit tests: compile suspending procs with 1, 2, 3 sequential awaits; verify correct continuation closures generated with correct upvalue counts; verify non-suspending procs are unaffected
- [ ] All existing tests pass (build.sh)

### US-004: CPS transform — control flow

**Description:** As a developer, I need the CPS transform to handle suspension points inside control flow constructs so that `await` can appear in `if`/`cond`/`match` branches.

**Acceptance Criteria:**
- [ ] Suspension inside `if` branches: when any branch of an `if` contains a suspension point, the code after the `if` becomes a join continuation. All branches (suspending and non-suspending) flow into this continuation:
  ```
  # Input:
  proc baz [cond]
    def result (if $cond
      [await [spawn (fn [] [path-a])]]
    else
      42)
    [use $result]

  # CPS output:
  proc baz [__k] [cond]
    def join_k = continuation(result) { __k([use result]) }
    if cond
      __await(spawn(...), continuation(r) { join_k(r) })
    else
      join_k(42)   # non-suspending branch tail-calls join continuation
  ```
- [ ] When only some branches suspend, non-suspending branches call the join continuation directly with their result value (no suspension overhead for non-suspending paths)
- [ ] Nested `if` with suspension: inner if's join continuation feeds into outer if's join continuation
- [ ] `cond` (chained if) with suspension: same pattern — each branch routes to common join continuation
- [ ] `match` with suspension: same pattern as `if` — each arm routes to common join continuation
- [ ] Suspension inside `let` blocks: live variables from outer scope plus let bindings captured in continuation
- [ ] Nested suspension in expressions — arguments containing `await` are evaluated left-to-right, each creating a continuation:
  ```
  # Input:
  [f [await $x] [await $y]]

  # CPS output:
  __await(x, continuation(xval) {
    __await(y, continuation(yval) {
      __k([f xval yval])
    })
  })
  ```
- [ ] Unit tests: suspension in if (both branches suspend, one suspends, neither), nested if, cond, match, nested await in call arguments, let block with suspension
- [ ] Verify compile-time error for suspension inside try/catch
- [ ] All existing tests pass (build.sh)

### US-005: `spawn` and `run` primitives

**Description:** As a developer, I need `spawn` to launch a closure as a concurrent task on the worker pool and return a future, and `run` to bridge synchronous code into the async world.

**Acceptance Criteria:**
- [ ] `spawn` recognized as a builtin command by the compiler
- [ ] `spawn` takes exactly one argument: a zero-arg closure (suspending or non-suspending)
- [ ] `spawn` is NOT a suspension point — can appear in any proc
- [ ] Add `OP_SPAWN` opcode:
  - Pop closure from stack
  - Create a pending `JaclFuture` via `gc_alloc`
  - Create a task wrapper that: executes the closure on a worker VM (if suspending, passes `__k = resolve_future(F)` as the result continuation; if non-suspending, calls closure normally and resolves future with return value), catches errors and calls `jacl_future_error` on failure
  - Push task onto the submitting worker's public deque (or runtime inbox if called from main thread)
  - Push the future onto the VM stack as the result value
- [ ] `run` recognized as a builtin command by the compiler
- [ ] `run` takes exactly one argument: a zero-arg closure (suspending or non-suspending)
- [ ] `run` is NOT a suspension point — blocks the calling thread synchronously
- [ ] Add `OP_RUN` opcode:
  - Pop closure from stack
  - Internally perform `spawn` (create future + submit task)
  - Block calling thread using mutex + condition variable until the future resolves or errors
  - Push resolved result (or error value) onto the stack
- [ ] Future resolution signals blocking waiters: when `jacl_future_resolve`/`error` is called and a blocking sync object is attached, signal the condvar
- [ ] `JaclFuture` gains optional `blocking_sync` field (NULL for normal futures, allocated by `run`) — avoids condvar overhead on every future
- [ ] If `run` receives an error result, it pushes the error value (user can check with `error?`)
- [ ] Unit tests: spawn returns a future, spawned closure executes on worker, spawn + manual future poll, run blocks until result, run with non-suspending closure, run with suspending closure (spawn+await inside), run with error
- [ ] All existing tests pass (build.sh)

### US-006: `await` primitive

**Description:** As a developer, I need `await` to suspend the current CPS continuation until a future resolves, then resume the next continuation with the result.

**Acceptance Criteria:**
- [ ] `await` recognized as a suspension point by the compiler (triggers CPS transform of enclosing proc)
- [ ] `await` takes exactly one argument: a future value
- [ ] After CPS transform, `await` compiles to `OP_AWAIT`:
  - Pop continuation closure from stack (generated by CPS transform)
  - Pop future from stack
  - Check future state:
    - `FUTURE_PENDING`: call `jacl_future_add_waiter` to register continuation; return from `vm__run` (current task complete, worker picks up next task)
    - `FUTURE_RESOLVED`: schedule continuation as a new task on current worker's public deque with the resolved value as its argument; return from `vm__run`
    - `FUTURE_ERROR`: schedule continuation as a new task with the error value; return from `vm__run`
  - In all cases, OP_AWAIT causes `vm__run` to return (the current CPS segment is finished)
- [ ] When a future resolves, all registered waiter continuations are scheduled as tasks:
  - `jacl_future_resolve` returns the waiter list
  - The resolving task iterates the list and pushes each continuation as a RuntimeTask onto the current worker's public deque, with the result as argument
- [ ] Error model: `await` on an errored future passes JACL's existing M9 error value to the continuation — the continuation can check `[error? $result]` to distinguish success from error. Reuses the existing `JaclVal` error tag representation (no new types)
- [ ] Runtime error if `await` is called with a non-future value
- [ ] Unit tests: await resolved future (immediate scheduling), await pending future (waiter registered, scheduled on resolve), await errored future (error value passed), multiple awaiters on same future (all scheduled), sequential awaits (chained continuations produce correct final result)
- [ ] All existing tests pass (build.sh)

### US-007: `parallel` primitive

**Description:** As a developer, I need `parallel` to fork N tasks and suspend until all complete, returning a vector of results in input order.

**Acceptance Criteria:**
- [ ] `parallel` recognized as a suspension point by the compiler
- [ ] `parallel` takes 2+ arguments: zero-arg closures to execute concurrently
- [ ] After CPS transform, `parallel` compiles to `OP_PARALLEL`:
  - Pop continuation closure from stack (generated by CPS)
  - Pop N closures from stack
  - Create N individual futures + N task wrappers (each resolves its future)
  - Push all N tasks onto deques
  - Create an aggregate tracking structure (GC-managed):
    - Holds references to all N individual futures
    - Atomic counter of completed tasks (starts at 0)
    - Result slots array (N entries, preserving input order)
    - Reference to the join continuation
  - Each individual task wrapper, on completion, atomically increments the counter and stores its result at the correct index. When counter reaches N, schedule the join continuation with a vector of all results
  - Return from `vm__run` (suspension)
- [ ] Result is a JACL vector `[result_0, result_1, ..., result_N-1]` in input order (not completion order)
- [ ] If any individual task errors, the entire `parallel` errors: the join continuation receives the error value (first error wins — subsequent errors are discarded)
- [ ] GC trace for aggregate structure: trace all individual futures, result slots, and join continuation
- [ ] Compile-time error if `parallel` given fewer than 2 arguments
- [ ] Unit tests: parallel 2 tasks (both succeed, results in order), parallel 5 tasks, parallel with one error (aggregate errors), parallel with heavy allocation per task (GC stress), parallel results preserve input order despite different completion times
- [ ] All existing tests pass (build.sh)

### US-008: `race` primitive

**Description:** As a developer, I need `race` to fork N tasks and suspend until the first completes, returning the winner's result.

**Acceptance Criteria:**
- [ ] `race` recognized as a suspension point by the compiler
- [ ] `race` takes 2+ arguments: zero-arg closures to race
- [ ] After CPS transform, `race` compiles to `OP_RACE`:
  - Pop continuation closure from stack
  - Pop N closures from stack
  - Create N individual futures + N task wrappers
  - Push all N tasks onto deques
  - Create a race tracking structure (GC-managed):
    - `_Atomic uint32_t settled` — false initially
    - Reference to the race continuation
    - References to all N individual futures
  - Each individual task wrapper, on completion: CAS `settled` from false to true. If CAS succeeds (this task is the winner), schedule the race continuation with this task's result. If CAS fails (not first), result is discarded
  - Return from `vm__run` (suspension)
- [ ] Losing tasks run to completion — their results are silently discarded (no cancellation in M13)
- [ ] If the winning task errors, the race continuation receives the error value
- [ ] GC trace for race structure: trace all individual futures and race continuation
- [ ] Compile-time error if `race` given fewer than 2 arguments
- [ ] Unit tests: race 2 tasks (faster wins), race returns winner's value, losing tasks complete without crash, race with winning error (error propagated), race with concurrent GC
- [ ] All existing tests pass (build.sh)

### US-009: Remove VM stack root scanning from concurrent GC

**Description:** As a developer, I need to remove VM stack root scanning from the concurrent GC path, since CPS continuations capture all live state and VM stack scanning is not thread-safe (per GC_CONCURRENCY_DESIGN.md Section 7).

**Acceptance Criteria:**
- [ ] Remove VM stack scanning (`stack[0..stack_top]`) from `gc_enumerate_roots` in runtime.c
- [ ] Remove call frame closure scanning from `gc_enumerate_roots`
- [ ] Remove call frame chunk constant scanning from `gc_enumerate_roots`
- [ ] Concurrent GC roots are now exactly per GC_CONCURRENCY_DESIGN.md Section 7:
  1. All tasks on all deques (public + private) — via deque snapshot
  2. Each worker's `currently_executing` slot (with BUSY sentinel protocol)
  3. Global environment values
  4. Intern table entries (Phase 1: immortal strings)
  5. Inbox tasks
- [ ] Single-threaded GC path (`gc_mark` in gc_collect.c used by `gc_collect` for non-runtime mode) retains VM stack scanning for backward compatibility with M0-M12 tests that don't use the runtime
- [ ] All concurrency tests pass (CPS continuations capture all live state as task roots)
- [ ] All M0-M12 tests pass (single-threaded path unchanged)
- [ ] All existing tests pass (build.sh)

### US-010: Adaptive GC threshold

**Description:** As a developer, I need the GC threshold to adapt based on survival rate so the collector runs less often when most objects are live and more often when there's lots of garbage.

**Acceptance Criteria:**
- [ ] Track `bytes_survived` during each GC sweep phase (sum of `alloc_total` for all live objects)
- [ ] Track `bytes_allocated` since last GC (already tracked as `bytes_since_gc`)
- [ ] Compute survival rate after each GC cycle: `survival_rate = bytes_survived / bytes_allocated`
- [ ] Adaptive threshold adjustment after each GC cycle:
  - Survival rate > 80%: increase threshold by 50% (GC is mostly re-tracing live objects)
  - Survival rate < 20%: decrease threshold by 25% (lots of garbage, collect sooner)
  - Otherwise: no change
- [ ] Configurable bounds: `GC_THRESHOLD_MIN` (default 256KB), `GC_THRESHOLD_MAX` (default 16MB)
- [ ] Threshold clamped to [min, max] after each adjustment
- [ ] `ThreadHeap` stores `gc_threshold` (starts at `GC_THRESHOLD` default 1MB)
- [ ] `gc_alloc` compares `bytes_since_gc` against `heap->gc_threshold` instead of fixed `GC_THRESHOLD`
- [ ] Works for both single-threaded (`gc_collect`) and concurrent (`gc_concurrent_collect`) paths
- [ ] Unit tests: verify threshold increases after high-survival GC, decreases after low-survival GC, respects min/max bounds, threshold persists across cycles
- [ ] All existing tests pass (build.sh)

### US-011: OOM escalation

**Description:** As a developer, I need a 3-tier escalation when block allocation fails so the runtime can recover from transient memory pressure and fail gracefully under true OOM.

**Acceptance Criteria:**
- [ ] When `gc_block_pool_get` fails to allocate a new block from the OS:
  - **Tier 1 — Emergency synchronous GC**:
    - For single-threaded mode: call `gc_collect` synchronously
    - For concurrent mode: if `gc_running` is false, perform a synchronous full GC on the current thread (CAS `gc_running`, run `gc_concurrent_collect` inline, release `gc_running`). If another GC is already running, wait for it to complete
    - Retry block allocation after emergency GC
  - **Tier 2 — Heap growth**: if emergency GC didn't free enough blocks, attempt to allocate from OS up to `GC_MAX_HEAP_BLOCKS` (default 4096 blocks = 256MB)
    - If under limit, allocate and return new block
  - **Tier 3 — Panic**: if OS refuses allocation or max heap limit reached:
    - Print diagnostic to stderr: current heap size (blocks x block size), failed allocation request size, survival rate from last GC, number of workers
    - Call `abort()` (unrecoverable)
- [ ] `GC_MAX_HEAP_BLOCKS` configurable as compile-time constant
- [ ] `BlockPool` tracks `total_blocks_allocated` for limit enforcement
- [ ] Emergency GC should be rare — only fires under extreme memory pressure (normal concurrent GC prevents this)
- [ ] Unit tests: simulate OOM by setting very small `GC_MAX_HEAP_BLOCKS`, verify emergency GC fires, verify tier 3 panic message (capture stderr)
- [ ] All existing tests pass (build.sh)

### US-012: Integration tests and stress testing

**Description:** As a developer, I need comprehensive integration and stress tests validating the CPS transform, concurrency primitives, and GC under concurrent load.

**Acceptance Criteria:**
- [ ] Create `test/test_m13.c` with integration tests grouped by feature
- [ ] **CPS transform correctness** (compile and run via `run`):
  - Single await: `def x [compute]; def y [await [spawn ...]]; [+ $x $y]`
  - Multiple sequential awaits: chain of 3 awaits, all captured variables correct
  - Await in if branches: suspending branch + non-suspending branch produce correct result
  - Await in nested expressions: `[f [await $x] [await $y]]` evaluates correctly
  - Deep continuation chain: 5+ sequential awaits with different captured variables
  - Variable capture correctness: verify continuation captures the right values (not stale)
- [ ] **spawn + await integration**:
  - spawn + await returns correct value
  - Multiple spawn + sequential await
  - Spawn closure that allocates heavily (triggers GC during task execution)
  - Await already-resolved future (immediate resume, no deadlock)
  - Await errored future (error value received, checkable with `error?`)
  - Multiple awaiters on same future (all receive the value)
- [ ] **parallel integration**:
  - parallel 2 tasks: results in correct order
  - parallel 5 tasks: all results collected into vector
  - parallel with one error: error propagated
  - parallel with heavy allocation per task (GC under load)
- [ ] **race integration**:
  - race 2 tasks: winner's value returned
  - Losers complete without crash or leak
- [ ] **run integration**:
  - run with spawn+await: blocks main thread, returns result
  - run with parallel: blocks until all complete
  - run with error: returns error value
- [ ] **Multi-threaded stress**:
  - 4+ workers, many spawned tasks that spawn more tasks (recursive spawn)
  - Heavy allocation during spawn/await with concurrent GC — no crashes, no use-after-free
  - parallel of 20+ tasks with concurrent GC
  - race of 10+ tasks with concurrent GC
  - Mixed spawn/await/parallel/race workload under sustained load
- [ ] **Memory bounded**: sustained spawn/await workload triggering many GC cycles — heap block count stabilizes, doesn't grow without bound
- [ ] **Regression**: all M0-M12 C unit tests pass, all M0-M12 `.jacl` harness tests pass
- [ ] Add `test/test_m13.c` to build.sh TESTS array with `-lpthread`
- [ ] All existing tests pass (build.sh)

## Functional Requirements

- FR-1: `Future` is a first-class GC-managed value type (`JACL_TAG_FUTURE`, `OBJ_FUTURE`) with states pending/resolved/error, a result slot, and a waiter list of continuation closures
- FR-2: The compiler infers suspension status for all procedures transitively: a proc suspends if it contains `await`/`parallel`/`race` or calls a proc that suspends
- FR-3: Suspending procedures are CPS-transformed at compilation: a hidden continuation parameter `__k` is added, the body is split at suspension points into continuation closures, and the proc calls `__k` with its final result instead of returning
- FR-4: Continuation closures capture exactly the live variables at each suspension point using the existing upvalue mechanism
- FR-5: `spawn` takes a zero-arg closure, creates a pending future, pushes a task wrapper onto a worker deque, and returns the future. `spawn` does not suspend
- FR-6: `await` suspends the current CPS task until the given future resolves. The continuation (generated by CPS) is registered as a waiter on the future. When the future resolves, the continuation is scheduled as a task with the result value
- FR-7: `parallel` takes N zero-arg closures, spawns all as tasks, and suspends until all complete. The continuation receives a vector of results in input order
- FR-8: `race` takes N zero-arg closures, spawns all as tasks, and suspends until the first completes. The continuation receives the winner's result. Losers run to completion (no cancellation)
- FR-9: `run` takes a zero-arg closure (suspending or non-suspending), submits it as a task to the worker pool, and blocks the calling thread until the task completes. Returns the result. This is the synchronous-to-asynchronous bridge
- FR-10: Concurrent GC roots are: tasks on all deques, each worker's `currently_executing` slot, global environment values, intern table entries, inbox tasks. No VM stack scanning in the concurrent path
- FR-11: GC threshold adapts based on survival rate: high survival increases threshold (less frequent GC), low survival decreases it (more frequent GC), clamped to configurable min/max bounds
- FR-12: Block allocation failure triggers 3-tier escalation: emergency synchronous GC, heap growth up to configured max, panic with diagnostics
- FR-13: Suspension points cannot appear inside `try`/`catch` blocks (compile-time error). Error handling for concurrent tasks uses the error capture model: errors are stored in futures, `await` passes the error value to the continuation
- FR-14: Suspension points cannot appear inside closures passed to non-suspending higher-order builtins (`map`, `filter`, `reduce`, `each`) — compile-time error
- FR-15: `await` on an errored future passes the M9 error value (existing `JaclVal` error tag) to the continuation. The continuation can distinguish success from error using the existing `error?` predicate. No new error types are introduced
- FR-16: Write barriers fire on future resolution (storing result into the future is a mutable container mutation during potentially active GC)
- FR-17: Indirect calls to closures with unknown suspension status are conservatively treated as suspending — the enclosing proc is CPS-transformed. This is safe (over-transformation is harmless) and avoids missed suspension points
- FR-18: `parallel` with fewer than 2 arguments is a compile-time error
- FR-19: `race` with fewer than 2 arguments is a compile-time error

## Non-Goals (Out of Scope)

- No channels or message passing — deferred to a future PRD
- No `select` multiplexing — deferred (depends on channels)
- No cancellation or cooperative cancellation — `race` losers run to completion; cancellation deferred to a future PRD
- No explicit suspension annotations — suspension is inferred for M13; explicit annotations (e.g., for library boundaries) deferred
- No suspension inside `try`/`catch` — error handling for concurrent tasks uses the future error capture model
- No suspending closures passed to higher-order builtins — `parallel` is the primitive for concurrent iteration
- No generational GC (sticky mark-bit, remembered set) — deferred to a future PRD
- No weak-reference string interning (Phase 2) — deferred
- No async generators or async iteration
- No `with-open` / `defer` for deterministic resource cleanup — deferred
- No timeout mechanism for `await`
- No priority scheduling for tasks

## Technical Considerations

- **Unity build integration**: Future type + GC tracing in gc.c (before string.c). New opcodes (OP_SPAWN, OP_AWAIT, OP_PARALLEL, OP_RACE, OP_RUN) in bytecode.c/vm.c. Suspension analysis and CPS transform in compiler.c. Task scheduling in runtime.c
- **CPS transform mechanism**: The compiler creates a nested Compiler context for each continuation closure (same mechanism used for `fn`/`proc` bodies). The continuation captures live variables as upvalues using the existing upvalue resolution mechanism. This means CPS doesn't require new runtime concepts — it produces ordinary closures
- **Hidden `__k` parameter**: CPS-transformed procs take `__k` (the result continuation) as a hidden first parameter. Non-suspending callers never see this. `spawn` provides `__k = resolve_future(F)`. `await` provides the next continuation in the CPS chain. At the bytecode level, `__k` is a regular local variable (slot 0 or 1)
- **OP_AWAIT semantics**: OP_AWAIT pops future + continuation from stack, registers/schedules continuation, then causes `vm__run` to return (like OP_RETURN from the outermost frame). The worker thread's task loop then picks up the next task. This is the "suspension" — the VM stack is abandoned (continuation captured all live state)
- **Task scheduling for continuations**: When a future resolves, waiter continuations become RuntimeTasks pushed onto the resolving worker's public deque. Each task calls the continuation closure with the result as argument. This integrates naturally with the existing Chase-Lev work-stealing infrastructure
- **`run` blocking mechanism**: `run` attaches a `BlockingSync` (mutex + condvar) to the future. When the future resolves, if `blocking_sync` is non-NULL, signal the condvar. The main thread blocks on the condvar. Normal futures have `blocking_sync = NULL` (no overhead)
- **Parallel aggregate tracking**: The aggregate structure for `parallel` needs atomic counter + result array + continuation reference. This is a GC-managed object (new `OBJ_PARALLEL_AGG` type) so it's properly traced. Each sub-task wrapper holds a reference to the aggregate and its index
- **Race tracking**: Similar to parallel but simpler — just an atomic `settled` flag + continuation reference. New `OBJ_RACE_AGG` type
- **Suspension analysis performance**: The fixpoint analysis iterates over all proc definitions. In practice, very few procs will suspend (most code is synchronous), so the fixpoint converges quickly. Worst case is O(N^2) for N proc definitions, which is acceptable for typical program sizes
- **Backward compatibility**: Existing M0-M12 tests use `vm_exec` directly (single-threaded mode). This path is unchanged — single-threaded `gc_collect` still scans VM stacks. The concurrent path (via Runtime) uses CPS roots only
- **Existing platform.h atomics**: Use `ATOMIC_CAS`, `ATOMIC_LOAD`, `ATOMIC_STORE` from platform.h for future state, aggregate counters, race settled flag
- **Test infrastructure**: Concurrency tests use `run` to block the test thread while async work completes. This provides deterministic test output despite concurrent execution

## Success Metrics

- All existing M0-M12 tests pass unchanged (no regressions)
- CPS transform produces correct results for all control flow patterns (sequential, branching, nested)
- `spawn` + `await` correctly produces values across worker threads
- `parallel` returns correctly ordered results from N concurrent tasks
- `race` returns the first result, losers complete without crash
- Multi-threaded stress tests: no crashes, no use-after-free, no data races over sustained concurrent workloads with GC active
- Memory bounded: sustained async workloads with GC — heap stabilizes, doesn't grow without bound
- Adaptive GC threshold: threshold adjusts up/down based on measured survival rate

## Resolved Design Decisions

- **`parallel` error handling**: First error fails the whole `parallel` — the join continuation receives the error value, remaining task results are discarded. This matches Promise.all semantics
- **Error value representation**: Reuse JACL's existing M9 error value representation (`JaclVal` with error tag). `error?` predicate already works. No new types needed for error propagation through futures
- **`parallel` argument count**: Compile-time error for fewer than 2 arguments. 0 or 1 args is not a meaningful parallel operation
- **Indirect calls to unknown closures**: Conservatively assume suspending. The enclosing proc gets CPS-transformed. Safe (may over-transform, never misses a suspension). Explicit annotations in a future PRD will allow the user to mark closures as non-suspending

## Open Questions

- Should `run` warn or error when called from a worker thread? It blocks a worker, reducing parallelism. For M13, allow it (useful for tests), consider a warning in the future
- Should the CPS transform optimize the case where `await` is called on an already-resolved future (avoid creating a continuation task, just inline the result)? Deferred optimization — always schedule for uniformity first
