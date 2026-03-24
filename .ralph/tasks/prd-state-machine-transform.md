# PRD: Unified State Machine Transform (CPS Replacement)

## Introduction

Replace the CPS (continuation-passing style) suspension system with a unified state machine architecture across all four suspension primitives: yield, await, parallel, and race. Currently, every suspension point allocates a continuation closure on the GC heap that eagerly captures all live locals as upvalues — costing ~3 heap allocations per yield in a generator loop and 1+ per await/parallel/race. The state machine transform allocates a single state object when a suspending function begins execution. Suspension points become resume-point indices in a jump table, eliminating per-suspension allocations entirely.

This is a multi-step migration. During steps 2-4, both CPS and state machine paths coexist behind a compiler flag, allowing incremental validation. Step 5 removes CPS entirely.

## Goals

- Zero per-suspension heap allocations for all four primitives (yield, await, parallel, race)
- Unified compilation model — one suspension mechanism instead of four special cases in CPS
- Incremental migration with CPS fallback available at each step
- All existing tests pass at every step
- Compilation cost remains O(n) — two linear passes instead of one
- Simpler GC root set: stream/future → state object (no closure chains)
- Reduced heap fragmentation from fewer short-lived closure allocations

## User Stories

---

### US-001: Suspension Point Analysis Pass

**Description:** As the compiler, I need to walk the AST of any function whose body (or nested bodies) contains yield/await/parallel/race, identify all suspension points, and number them sequentially, so that the state machine code generator knows how many resume points exist and where they are.

**Acceptance Criteria:**
- [ ] New function `compiler__analyze_suspensions` walks AST recursively
- [ ] Detects yield, await, parallel, and race nodes at any nesting depth (inside if/while/for/try)
- [ ] Assigns sequential integer IDs (0, 1, 2, ...) to each suspension point
- [ ] Records the AST node, suspension type (yield/await/parallel/race), and source location for each point
- [ ] Nested closures/lambdas are NOT analyzed as part of the parent — they get their own analysis if they suspend
- [ ] Functions with zero suspension points are not flagged for state machine compilation
- [ ] Result stored in a `SuspensionAnalysis` struct accessible during code generation
- [ ] Unit test: function with yield in while-loop identifies correct number of suspension points
- [ ] Unit test: function with await in both branches of if-else identifies both points
- [ ] Unit test: function with parallel containing 3 bodies — only the join point is a suspension point on the parent, body suspension points belong to the body closures

---

### US-002: Local Classification for State Object

**Description:** As the compiler, I need to determine which locals must live in the state object, so the code generator can emit state-field access instead of stack-relative access for those variables.

**Acceptance Criteria:**
- [ ] Conservative strategy: ALL locals in a suspending function are placed in the state object (no liveness analysis)
- [ ] Parameters are included (they must survive across suspension points)
- [ ] Each local gets a sequential field index in the state object
- [ ] Mutable locals (`mut`) are tracked — their state-object slot holds a cell reference, same as current upvalue capture semantics
- [ ] Result is a `StateLayout` struct: field count, field names (for debug), field-to-local mapping
- [ ] `StateLayout` is produced by `compiler__analyze_suspensions` alongside suspension point data
- [ ] Unit test: function with 3 locals produces StateLayout with 3 fields
- [ ] Unit test: function with mut local marks that field as mutable in layout

---

### US-003: OBJ_STATE_MACHINE GC Object Type

**Description:** As the runtime, I need a new GC-managed object type that holds a state machine's local fields, resume point, error handler, and debug info, so state machines have a heap-allocated home for their persistent state.

**Acceptance Criteria:**
- [ ] New object kind `OBJ_STATE_MACHINE` added to GC object type enum in gc.c
- [ ] Struct layout: `{ uint32_t resume_point, uint32_t field_count, JaclVal error_k, JaclVal sm_closure, SourceLoc* resume_locs, JaclVal fields[] }` (trailing array for local fields)
- [ ] `sm_closure` field holds the compiled state machine function (a JaclClosure with the bytecode)
- [ ] `resume_locs` is an array of source locations indexed by resume point (for stack traces)
- [ ] Allocation function: `gc_alloc_state_machine(heap, field_count, sm_closure, resume_locs)` — allocates object + trailing field array in one allocation
- [ ] All fields initialized to JACL_NIL on allocation
- [ ] GC tracing: `error_k`, `sm_closure`, and all `fields[0..field_count-1]` are traced as roots
- [ ] `resume_point` and `resume_locs` are not GC values (integer and pointer to arena-allocated array)
- [ ] Helper macros: `jacl_is_state_machine(val)`, `jacl_as_state_machine(val)`, `jacl_state_machine(ptr)`
- [ ] Unit test: allocate state machine, set fields, trigger GC, verify fields survive
- [ ] Unit test: state machine with closure field — GC traces through to closure's constants

---

### US-004: OP_GET_STATE_FIELD / OP_SET_STATE_FIELD Opcodes

**Description:** As the VM, I need opcodes to read and write fields on the current state machine object, so compiled state machine bodies can access their locals without stack-relative addressing.

**Acceptance Criteria:**
- [ ] `OP_GET_STATE_FIELD` added to OpCode enum — operand: uint8_t field index
- [ ] `OP_SET_STATE_FIELD` added to OpCode enum — operand: uint8_t field index
- [ ] VM handler for `OP_GET_STATE_FIELD`: reads field from state machine object in a known stack slot (slot 0 of the frame, i.e., the first argument), pushes onto stack
- [ ] VM handler for `OP_SET_STATE_FIELD`: pops value from stack, writes to state machine field
- [ ] The state machine object is passed as the first argument to the SM function — it lives at `frame->stack_base + 0`
- [ ] Opcode names registered in debug/disassembly tables
- [ ] Bytecode disassembler updated to print field index
- [ ] Unit test: hand-assembled bytecode that creates state machine, calls SM function, gets/sets fields

---

### US-005: Compiler Flag for State Machine vs CPS

**Description:** As a developer, I need a compiler flag to choose between state machine and CPS compilation for suspending functions, so both paths can coexist during the migration and I can test/compare them.

**Acceptance Criteria:**
- [ ] New field `bool use_state_machines` on the Compiler struct (default: false initially)
- [ ] When true, suspending functions use the state machine path; when false, existing CPS path
- [ ] Flag propagated to nested compilers (continuation compilers, parallel body compilers)
- [ ] Exposed via a runtime/CLI option (e.g., `--state-machines` or similar) for test runs
- [ ] Both paths produce correct results for the same test suite
- [ ] Test harness can run the full test suite with either flag

---

### US-006: State Machine Code Generation for Generators (Yield)

**Description:** As the compiler, I need to compile generator bodies into state machine bytecode instead of CPS when the state machine flag is enabled, so generators allocate one state object instead of N continuation closures.

**Acceptance Criteria:**
- [ ] Generator detected via existing `is_generator` flag on closure or via suspension analysis finding yield points
- [ ] Compilation emits a state machine function (JaclClosure) whose first parameter is the state machine object and second parameter is the resume value
- [ ] Function entry emits dispatch table: load `state->resume_point`, emit `OP_JUMP_TABLE` or chained conditional jumps to reach the correct resume label
- [ ] All local variable access in the generator body uses `OP_GET_STATE_FIELD` / `OP_SET_STATE_FIELD` instead of `OP_GET_LOCAL` / `OP_SET_LOCAL`
- [ ] Each yield point: (a) writes next resume_point index to state, (b) pushes yielded value, (c) emits `OP_RETURN` with a VM_YIELD signal
- [ ] After-yield resume labels: code after each yield is reachable via the dispatch table
- [ ] While-loops with yield: no loopback closure needed — loop body compiles with backward jump, yield inside uses resume point that jumps back into the loop
- [ ] If-statements with yield: each branch's yield gets its own resume point, dispatch jumps into the correct branch
- [ ] Nested control flow (yield inside if inside while): dispatch reconstructs control position via the resume point index
- [ ] `error_k` field on state object set to the caller's error handler at generator creation time
- [ ] Existing CPS while-loop pattern (`compiler__compile_cps_while` with self-referencing cell + loopback closure) is bypassed when state machine flag is on

---

### US-007: State Machine Generator Runtime (VM + Stream)

**Description:** As the VM, I need to create state machine objects for generators and drive them via OP_STREAM_NEXT, so the runtime can produce values from state-machine-compiled generators.

**Acceptance Criteria:**
- [ ] Generator creation (when calling a generator proc): allocates `OBJ_STATE_MACHINE` with field count from `StateLayout`, stores SM closure, copies arguments into state fields
- [ ] `Stream` struct: when backed by a state machine generator, `next_fn` is nil and a new `state_machine` field holds the state object reference
- [ ] `OP_STREAM_NEXT` updated: if stream has a state machine, calls `sm_closure(state_obj, resume_value)` instead of calling a continuation closure
- [ ] First call passes JACL_NIL as resume value (no prior yield to resume from)
- [ ] VM_YIELD return: stream caches yielded value, stream state = STREAM_PENDING (has value)
- [ ] Normal return (no yield): stream state = STREAM_EXHAUSTED
- [ ] `terminal_k` creation is skipped for state machine generators (no continuation closures involved)
- [ ] GC: Stream traces its `state_machine` field if present
- [ ] All existing generator tests pass with state machine flag enabled
- [ ] Performance test: generator yielding 1M values — measure allocation count is O(1) not O(N)

---

### US-008: Yield in Nested Control Flow (Control Flow Flattening)

**Description:** As the compiler, I need to handle yield inside arbitrarily nested if/while/for blocks by flattening control flow into a dispatch table, so state machine generators work correctly regardless of where yield appears.

**Acceptance Criteria:**
- [ ] Yield inside if-then branch: resume point jumps directly into the then-branch code after the yield
- [ ] Yield inside if-else branch: separate resume points for then and else yields
- [ ] Yield inside while body: resume point jumps to the code after yield within the loop; loop condition is re-evaluated on next iteration
- [ ] Yield inside nested while-in-if: resume point reconstructs both the if-branch and loop context
- [ ] Yield inside for-loop: resume point handles iterator state correctly (for-loop desugars to while + stream_next, state machine must preserve iterator)
- [ ] Multiple yields in sequence (no control flow): each gets sequential resume points, dispatch is straightforward
- [ ] Test: generator with `if (cond) { yield 1; yield 2 } else { yield 3 }` produces [1,2] or [3] correctly
- [ ] Test: generator with `while (cond) { yield i; if (other) { yield (i * 10) } }` produces correct sequence
- [ ] Test: generator with yield in 3 levels of nesting produces correct sequence

---

### US-009: Convert Await to State Machine Suspension

**Description:** As the compiler, I need to compile await as a state machine suspension point (not CPS), so async functions that await futures allocate one state object instead of a chain of continuation closures.

**Acceptance Criteria:**
- [ ] Await detected by suspension analysis alongside yield points
- [ ] Each await compiles as: (a) evaluate future expression, push onto stack, (b) write next resume_point to state, (c) emit `OP_AWAIT_SM` (new opcode) which pops future and state object
- [ ] `OP_AWAIT_SM` handler: if future resolved, calls SM function inline with (state, result); if future pending, registers state object as waiter; returns VM_OK to suspend current task
- [ ] Resume value for an await point is the future's resolved value
- [ ] Sequential awaits in one function: each gets its own resume point, second await resumes at its point with the second future's result
- [ ] `def x [await expr]` pattern: resume value is stored into the appropriate state field for local `x`
- [ ] State machine flag must be enabled; CPS await still works as fallback when flag is off
- [ ] Existing await tests pass with state machine flag enabled
- [ ] Test: function with 3 sequential awaits — only 1 allocation (state object), not 3 continuation closures

---

### US-010: Scheduler Support for State Machine Resumption

**Description:** As the runtime scheduler, I need to resume state machine functions when futures resolve, so the work-stealing scheduler can drive async state machines the same way it drives CPS continuations.

**Acceptance Criteria:**
- [ ] `FutureWaiter` struct extended: can hold either a continuation closure (existing) or a state machine object (new)
- [ ] `runtime__schedule_continuation` updated: accepts either a closure or state machine; dispatches to appropriate task executor
- [ ] New task executor `runtime__state_machine_task_exec`: calls `sm_closure(state_obj, resume_value)` on the worker's VM
- [ ] Error handling: if SM function returns error, reads `error_k` from state object and schedules it with the error (mirrors current `__k` upvalue[0] convention)
- [ ] `runtime__schedule_waiters` works with both closure and state machine waiters
- [ ] Worker pinning (`pinned`, `pin_worker_id`): state machine tasks respect pinning via the SM closure's pin fields
- [ ] GC roots: state machine object registered as GC root in task data (like closure is today)
- [ ] Test: concurrent test with multiple workers, async function with state machine await, verify correct results

---

### US-011: Error Propagation via error_k on State Object

**Description:** As the runtime, I need state machine error propagation to work as reliably as the current `__k` upvalue[0] CPS convention, so errors in async state machines don't cause futures to hang unresolved.

**Acceptance Criteria:**
- [ ] `error_k` field on `OBJ_STATE_MACHINE` is a JaclVal (closure or nil)
- [ ] Set at state machine creation time: caller provides the error handler closure (equivalent to `__k` in CPS)
- [ ] For generators: `error_k` is the stream's exhaustion/error handler
- [ ] For async functions: `error_k` is the completion future's resolver
- [ ] `runtime__state_machine_task_exec`: on error, checks `state->error_k`, schedules it with error value
- [ ] If `error_k` is nil (shouldn't happen in well-formed code), error is reported via VM error mechanism
- [ ] GC traces `error_k` field
- [ ] Test: async function that throws after an await — error propagates to caller's future
- [ ] Test: generator that throws after a yield — error propagates correctly

---

### US-012: Convert Parallel to State Machine Join

**Description:** As the compiler and runtime, I need parallel's join continuation to use state machine resumption instead of a CPS continuation closure, so parallel blocks in async functions don't allocate extra closures.

**Acceptance Criteria:**
- [ ] Parallel bodies still compiled as separate closures (they execute on different threads — unchanged)
- [ ] Join continuation is now a state machine resume point instead of a CPS closure
- [ ] `ParallelAgg` struct: new field for state machine object reference (replaces `continuation` closure field)
- [ ] `ParallelAgg` stores the resume_point index to resume at after all parallel tasks complete
- [ ] `runtime__complete_parallel_slot`: last completing task (atomic counter == count) calls SM function with (state_obj, results_vec) instead of scheduling a continuation closure
- [ ] Results vector assembled the same way as today — passed as the resume value
- [ ] Error handling: if any parallel body errors, `error_k` from state object is scheduled (same semantics as current `__k` forwarding)
- [ ] Thread safety: unchanged — atomic `completed` counter ensures only one thread resumes the parent SM
- [ ] GC: `ParallelAgg` traces state machine object field
- [ ] State machine flag must be enabled; CPS parallel still works as fallback
- [ ] All existing parallel tests pass with state machine flag enabled
- [ ] Test: parallel with 4 bodies, parent has locals that survive across parallel — locals correct after join

---

### US-013: Convert Race to State Machine Join

**Description:** As the compiler and runtime, I need race's join continuation to use state machine resumption instead of a CPS continuation closure, so race blocks don't allocate extra closures.

**Acceptance Criteria:**
- [ ] Race bodies still compiled as separate closures (unchanged)
- [ ] Join continuation is a state machine resume point
- [ ] `RaceAgg` struct: new field for state machine object reference (replaces `continuation` closure field)
- [ ] `RaceAgg` stores the resume_point index to resume at after the winning task completes
- [ ] `runtime__complete_race_slot`: winning task (CAS 0→1) calls SM function with (state_obj, winner_result) instead of scheduling a continuation closure
- [ ] Losing tasks silently discard (unchanged behavior)
- [ ] Error handling: if winning task errors, `error_k` from state object is scheduled
- [ ] Thread safety: unchanged — CAS-based winner selection ensures only one thread resumes parent SM
- [ ] GC: `RaceAgg` traces state machine object field
- [ ] State machine flag must be enabled; CPS race still works as fallback
- [ ] All existing race tests pass with state machine flag enabled

---

### US-014: Mixed Suspension (Yield + Await in Same Function)

**Description:** As the compiler, I need to handle generator functions that also await futures (async generators), so both yield and await are suspension points in the same state machine.

**Acceptance Criteria:**
- [ ] Suspension analysis identifies both yield and await points in the same function, assigns them sequential resume point IDs
- [ ] Yield points dispatch as generator yields (VM_YIELD return)
- [ ] Await points dispatch as future waits (OP_AWAIT_SM)
- [ ] The state machine function handles both resume-from-yield (called by OP_STREAM_NEXT) and resume-from-await (called by scheduler) via the same dispatch table
- [ ] Stream struct must handle the async case: stream_next may trigger an await that suspends the calling task
- [ ] Test: async generator that fetches data and yields results — correct sequence produced
- [ ] Test: async generator with yield in a loop that awaits inside the loop body

---

### US-015: Remove CPS Compilation Path

**Description:** As a maintainer, I want to remove the CPS compilation path entirely once state machines handle all suspension, so the compiler has one mechanism instead of two.

**Acceptance Criteria:**
- [ ] State machine flag defaults to true (or is removed — state machines are the only path)
- [ ] `compiler__emit_continuation` deleted (~100 lines in compiler.c:1767-1873)
- [ ] `compiler__compile_cps_stmts` and related CPS dispatch deleted (~250 lines in compiler.c:2649-2900)
- [ ] `compiler__compile_cps_while` (self-referencing cell + loopback closure pattern) deleted
- [ ] `is_cps` flag removed from Compiler struct
- [ ] `__k` convention removed: no upvalue[0] error forwarding, no `compiler__emit_get_k`
- [ ] `vm__make_terminal_k` deleted (vm.c:657-673) — no terminal continuation closures needed
- [ ] CPS-only test helpers (if any) updated or removed
- [ ] `suspension_map` usage reviewed — keep if still used for suspension detection, remove if only used for CPS
- [ ] Compiler flag `use_state_machines` removed (no longer optional)
- [ ] All tests pass
- [ ] Net code reduction: ~300 lines removed from compiler, ~30 lines removed from VM

---

### US-016: Remove CPS Runtime Support

**Description:** As a maintainer, I want to clean up runtime code that only exists to support CPS continuation closures for suspension, so the scheduler and aggregation structs are simpler.

**Acceptance Criteria:**
- [ ] `FutureWaiter` struct simplified: only state machine path, closure waiter path removed
- [ ] `ParallelAgg.continuation` field removed (replaced by state machine field in US-012)
- [ ] `RaceAgg.continuation` field removed (replaced by state machine field in US-013)
- [ ] `runtime__continuation_task_exec` simplified or removed if all resumptions go through SM executor
- [ ] `ContinuationTaskData` struct removed if no longer used
- [ ] Error recovery code in task executor simplified (no `__k` upvalue[0] check, just `error_k` field)
- [ ] All tests pass
- [ ] No dead code remaining related to CPS continuation scheduling

---

### US-017: Liveness Optimization for State Object Fields (Future)

**Description:** As the compiler, I want to optionally reduce state object size by only including locals that are live across at least one suspension point, so generators/async functions with many temporaries don't waste memory.

**Acceptance Criteria:**
- [ ] New analysis: for each local, determine if it is live (defined before, used after) across any suspension point
- [ ] Locals that are only used between two suspension points (never cross a boundary) stay on the stack as normal `OP_GET_LOCAL` / `OP_SET_LOCAL`
- [ ] State object only contains cross-suspension locals + parameters
- [ ] Analysis is O(n) — single pass with local liveness tracking
- [ ] Compiler flag to enable/disable (conservative mode remains available as fallback)
- [ ] Test: function with 10 locals but only 2 cross a yield — state object has 2 fields (+ params)
- [ ] All existing tests pass with optimization enabled

---

## Functional Requirements

- FR-1: The compiler must perform a suspension analysis pass on any function whose body or nested scope contains yield, await, parallel, or race, producing a numbered list of suspension points and a state object field layout.
- FR-2: The compiler must emit `OP_GET_STATE_FIELD` / `OP_SET_STATE_FIELD` for all local variable access within state-machine-compiled functions, using the state object (first argument) as the base.
- FR-3: The compiler must emit a dispatch table at state machine function entry that jumps to the correct resume point based on `state->resume_point`.
- FR-4: The compiler must flatten control flow such that yield/await inside nested if/while/for blocks can be resumed by jumping directly into the correct nesting level.
- FR-5: The VM must support `OBJ_STATE_MACHINE` as a first-class GC object with field access, resume point tracking, error handler, and source location debug info.
- FR-6: `OP_YIELD` in state machine mode must write the resume point index to the state object and return VM_YIELD with the yielded value — no continuation closure allocated.
- FR-7: `OP_STREAM_NEXT` must drive state-machine generators by calling the SM function with (state_obj, resume_value).
- FR-8: `OP_AWAIT_SM` must suspend the current state machine by registering the state object as a future waiter (or calling inline if already resolved).
- FR-9: The scheduler must resume state machines by calling the SM function with (state_obj, resume_value), using the same work-stealing dispatch as continuation tasks.
- FR-10: `ParallelAgg` and `RaceAgg` must hold state machine references for join continuation, resuming the parent SM when tasks complete.
- FR-11: Error propagation must use the `error_k` field on the state object, replacing the `__k` upvalue[0] CPS convention.
- FR-12: A compiler flag must allow selecting CPS vs state machine compilation during the migration period (steps 2-4).
- FR-13: The CPS path (compiler__emit_continuation, compiler__compile_cps_stmts, __k convention, terminal_k) must be fully removed after state machines handle all suspension types.

## Non-Goals

- **JIT compilation:** This PRD covers the bytecode interpreter only — no native code generation.
- **Stackful coroutines:** State machines are stackless (like Kotlin/Rust). No OS thread or stack switching.
- **Automatic parallelism:** parallel/race bodies are still explicitly written by the user.
- **Async I/O runtime:** This changes suspension mechanics, not the I/O model.
- **Liveness-optimized state objects in initial implementation:** US-017 is explicitly deferred; conservative "all locals in state" is the initial approach.
- **Changes to the language syntax:** No new keywords or syntax. Yield, await, parallel, race behave identically from the user's perspective.
- **Optimization of parallel/race body compilation:** Bodies still compile as closures. Only the join continuation changes.

## Technical Considerations

- **Key files:** compiler.c (~8400 lines), vm.c (~6200 lines), runtime.c (~1400 lines), gc.c (~1100 lines), bytecode.c (opcode enum)
- **CPS path to replace:** compiler.c:1767-1873 (continuation emitter), compiler.c:2649-2900 (CPS statement compiler), compiler.c:2787-2849 (CPS while-loop)
- **VM handlers to modify:** OP_YIELD (vm.c:5297), OP_STREAM_NEXT (vm.c:5311), OP_AWAIT (vm.c:3596), OP_PARALLEL (vm.c:3833), OP_RACE (vm.c:4030)
- **Scheduler to modify:** runtime__schedule_continuation (runtime.c:1078), runtime__continuation_task_exec (runtime.c:1045), FutureWaiter (gc.c:889)
- **GC integration:** New OBJ_STATE_MACHINE type needs allocation, tracing, and sweeping support in gc.c
- **Dispatch table implementation:** Either a jump table opcode (`OP_JUMP_TABLE` with offset array) or chained conditional jumps. Jump table is faster for >3 resume points; chained jumps are simpler to implement. Recommend jump table.
- **Backpatching:** The dispatch table at function entry references labels that are emitted later during body compilation. The compiler must either buffer bytecode or backpatch jump targets (existing backpatching infrastructure can be reused from if/while compilation).
- **State object lifetime:** Alive as long as the stream (for generators) or the completion future (for async functions) is alive. GC traces from stream/future → state object → fields.
- **Thread safety:** State machine object is only accessed by one thread at a time. Parallel/race use atomic counters to ensure only the final completing task resumes the parent SM. No mutex needed on the state object itself.

## Success Metrics

- Zero per-suspension heap allocations: generator yielding N values allocates exactly 1 state object, not O(N) closures
- Async function with K sequential awaits allocates 1 state object, not K continuation closures
- All existing tests pass at every step of the migration
- Compiler code is net smaller after CPS removal (~300 lines removed, ~700 lines added for state machines = net ~400 line increase, but one mechanism instead of two)
- GC pressure measurably reduced in generator-heavy benchmarks (fewer GC cycles triggered)
- No regression in compilation speed (two O(n) passes vs one O(n) pass — negligible difference)

## Open Questions

1. **Jump table opcode vs chained jumps:** Should we add a dedicated `OP_JUMP_TABLE` opcode with an inline offset array, or use chained `OP_JUMP_IF_EQ` comparisons for the dispatch? Jump table is O(1) dispatch but requires a new opcode format. Chained jumps work with existing opcodes but are O(k) where k = number of resume points.
2. **State object field access width:** `OP_GET_STATE_FIELD` with uint8_t operand limits to 255 fields. Is this sufficient, or should we use uint16_t for future-proofing? (255 locals in a single function seems like plenty.)
3. **Async generator interaction with stream consumers:** When an async generator awaits inside its body, the stream_next caller is also suspended. Should stream_next return a future in this case, or should the caller implicitly await? This affects the user-facing API.
4. **Pinned state machines:** Currently closures can be pinned to specific workers. State machine objects need the same — should pinning live on the SM closure or the state object itself?
5. **Debug/profiler integration:** The current profiler/debugger may rely on closure-based stack traces through `__k` chains. State machines change the call stack shape. How much debugger work is needed?
