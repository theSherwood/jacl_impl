# PRD: M14 — GC/Concurrency Hardening

## Introduction

An audit of the JACL GC and concurrency subsystems identified 3 soundness bugs, 1 data race (undefined behavior under C11), performance bottlenecks, and significant test coverage gaps. This milestone hardens the GC/concurrency intersection with targeted fixes and expanded test coverage to ensure correctness under real concurrent workloads.

The audit found that the overall architecture (Immix-style per-thread heaps, epoch watermarking, CPS-based root elimination, hybrid write barriers) is sound, but several implementation details create scenarios where GC-managed objects can be prematurely collected or where concurrent memory accesses are technically undefined behavior.

## Goals

- Fix all identified soundness bugs where GC can prematurely collect live objects
- Eliminate undefined behavior in grey buffer concurrent access
- Reduce inbox mutex contention for better multi-worker throughput
- Eliminate OS resource leaks in GC-managed futures
- Expand test coverage to exercise real concurrent GC scenarios, not just single-threaded stubs

## User Stories

### US-001: Multi-root GC scanning for RuntimeTask

**Description:** As a runtime developer, I need RuntimeTask to root multiple GC values so that all task-referenced objects survive garbage collection.

**Background:** `RuntimeTask.gc_root` holds a single `JaclVal`, but several task types require two roots:

| Submission site | gc_root (rooted) | Missing root |
|---|---|---|
| `runtime__schedule_continuation` | continuation closure | result value |
| `runtime__submit_spawn_task` | future | spawn closure |
| `runtime__submit_parallel_task` | parallel agg | task body closure |
| `runtime__submit_race_task` | race agg | task body closure |

If GC runs between task submission and execution, the unrooted value can be collected if no other reference exists.

**Approach:** Add a second root slot `gc_root2` to `RuntimeTask`. Update all submission sites to populate both slots. Update `gc_enumerate_roots` (and `gc__scan_deque`) to scan both `gc_root` and `gc_root2`.

**Acceptance Criteria:**
- [ ] `RuntimeTask` struct has `gc_root` and `gc_root2` fields
- [ ] `runtime__schedule_continuation` sets `gc_root = continuation`, `gc_root2 = result`
- [ ] `runtime__submit_spawn_task` sets `gc_root = future_val`, `gc_root2 = closure_tagged`
- [ ] `runtime__submit_parallel_task` sets `gc_root = agg_val`, `gc_root2 = closure_tagged`
- [ ] `runtime__submit_race_task` sets `gc_root = agg_val`, `gc_root2 = closure_tagged`
- [ ] `gc_enumerate_roots` scans both `gc_root` and `gc_root2` for `currently_executing`
- [ ] `gc__scan_deque` scans both `gc_root` and `gc_root2` for deque tasks
- [ ] Inbox scanning in `gc_enumerate_roots` scans both roots
- [ ] `runtime_submit` and `runtime_submit_task` set `gc_root2 = JACL_NIL`
- [ ] New unit test: submit a task with gc_root2, force GC, verify object survives
- [ ] All existing tests pass
- [ ] Compiles without warnings

### US-002: Root the parallel join result vector

**Description:** As a runtime developer, I need the parallel join result vector to be GC-rooted between construction and continuation execution so it is not prematurely collected.

**Background:** In `runtime__parallel_task_exec`, when the last parallel task completes, it builds a result vector from `agg->results[]` and passes it to `runtime__schedule_continuation`. The result vector lives only in malloc'd `ContinuationTaskData` — not GC-rooted. If all parallel tasks have completed (their RuntimeTasks freed, removing `gc_root = agg_val`) and GC runs before the continuation task executes, the result vector can be collected.

**Approach:** With US-001's `gc_root2` in place, `runtime__schedule_continuation` already sets `gc_root2 = result`. This roots the result vector through the continuation task. Verify this is sufficient and add a targeted test.

**Acceptance Criteria:**
- [ ] Confirm `runtime__schedule_continuation` sets `gc_root2 = cont_arg` (result vector) after US-001
- [ ] New test: parallel with N tasks, force GC after all tasks complete but before join continuation runs, verify result vector is intact
- [ ] All existing parallel tests pass in concurrent mode
- [ ] Compiles without warnings

### US-003: Grey buffer memory ordering

**Description:** As a runtime developer, I need grey buffer writes and reads to use proper memory ordering so that the GC always sees consistent entries on all architectures.

**Background:** `grey_buf_push` (worker thread) writes `entries[count]` then increments `count` as plain stores. `gc__drain_grey_bufs` (GC thread) reads `count` then reads `entries[j]` as plain loads. Under C11, concurrent non-atomic access where at least one is a write is undefined behavior. On ARM/POWER (weakly-ordered), the GC could observe incremented `count` but stale `entries[]` data, missing a write barrier entry and potentially collecting a live object.

**Approach:** Change `GreyBuffer.count` to use atomic operations:
- In `grey_buf_push`: write `entries[count]` first (plain store, thread-local), then `ATOMIC_STORE_EXPLICIT(&gb->count, gb->count + 1, MEM_RELEASE)` to ensure the entry is visible before the count increment.
- In `gc__drain_grey_bufs`: read `current = ATOMIC_LOAD_EXPLICIT(&gb->count, MEM_ACQUIRE)` to ensure all entries up to `current` are visible.
- The `entries` array writes remain plain stores (only the owning thread writes them), but the release on count publication ensures they are visible to the GC thread after acquire.

**Acceptance Criteria:**
- [ ] `GreyBuffer.count` is declared as `volatile uint32_t` (or use `ATOMIC_STORE/LOAD` wrappers)
- [ ] `grey_buf_push` uses a release store for the count increment
- [ ] `gc__drain_grey_bufs` uses an acquire load for the count read
- [ ] `grey_buf.count = 0` reset in `gc_concurrent_collect` uses a release store (or plain store after `gc_active = 0` which provides the needed ordering)
- [ ] Comment in `grey_buf_push` and `gc__drain_grey_bufs` documenting the ordering contract
- [ ] All existing GC and write barrier tests pass
- [ ] Compiles without warnings on both x86 and ARM (if cross-compile available)

### US-004: Batched inbox drain with lockless empty check

**Description:** As a runtime developer, I want to reduce inbox mutex contention so that workers spend less time blocked on lock acquisition when the inbox is empty.

**Background:** The current worker loop acquires `inbox_mutex` on every iteration to check for tasks. The inbox is empty ~99% of the time. With N workers polling at ~100Hz+ each, this creates unnecessary contention. Additionally, when tasks are present, each worker pops one task per lock acquisition rather than draining in batch.

**Approach:** Two changes to `runtime__worker_loop`:
1. **Lockless empty check:** Read `rt->inbox_count` (already `volatile intptr_t`) without the lock. Only acquire the mutex if `inbox_count > 0`. This eliminates lock acquisition for the empty-inbox common case.
2. **Batched drain:** When the inbox is non-empty, lock once and drain ALL pending tasks into the worker's private deque, then unlock. Other workers process remaining work from their own deques or by stealing.

**Acceptance Criteria:**
- [ ] Worker loop checks `rt->inbox_count > 0` before acquiring `inbox_mutex`
- [ ] When inbox is non-empty, all pending tasks are drained in one lock acquisition
- [ ] Drained tasks are pushed to the worker's private deque (not executed inline)
- [ ] `runtime_submit` and `runtime_submit_task` continue to use mutex-protected push (no change to producer side)
- [ ] All existing concurrent tests pass
- [ ] Stress test (`test_concurrent_gc_stress` or equivalent) shows no regression
- [ ] Compiles without warnings

### US-005: Replace future mutex with spinlock

**Description:** As a runtime developer, I want futures to use a spinlock instead of a platform mutex so there is no OS resource to leak when futures are garbage collected.

**Background:** `JaclFuture` contains a `platform_mutex_t` (pthread_mutex on POSIX, CRITICAL_SECTION on Windows). When a future is garbage-collected, `MUTEX_DESTROY` is never called — there is no finalizer mechanism. On POSIX this is benign, but on Windows each future leaks a kernel object. The lock is only held for ~10 instructions during the resolve-vs-add_waiter race window, making a spinlock appropriate.

**Approach:** Replace `platform_mutex_t lock` in `JaclFuture` with `volatile uint32_t lock` and use CAS-based spin-lock/unlock:
```c
static inline void future_lock(JaclFuture *f) {
    uint32_t expected = 0;
    while (!ATOMIC_CAS(&f->lock, &expected, 1, MEM_ACQUIRE, MEM_RELAXED)) {
        expected = 0;
    }
}
static inline void future_unlock(JaclFuture *f) {
    ATOMIC_STORE_EXPLICIT(&f->lock, 0, MEM_RELEASE);
}
```

**Acceptance Criteria:**
- [ ] `JaclFuture.lock` changed from `platform_mutex_t` to `volatile uint32_t`
- [ ] `MUTEX_INIT(f->lock)` replaced with `f->lock = 0` in `jacl_future()`
- [ ] `MUTEX_LOCK/UNLOCK` calls in `jacl_future_resolve`, `jacl_future_error`, `jacl_future_add_waiter` replaced with `future_lock`/`future_unlock`
- [ ] No `MUTEX_DESTROY` needed (nothing to finalize)
- [ ] All existing future and spawn/await tests pass
- [ ] Concurrent stress tests pass (spinlock correctness under contention)
- [ ] Compiles without warnings

### US-006: Enable concurrent mode for existing .jacl tests

**Description:** As a developer, I want more .jacl integration tests to run in true concurrent mode so they exercise real worker threads, work-stealing, and GC interaction.

**Background:** Only 4 of ~20 concurrent .jacl test files use `# mode: concurrent` (spawn_basic, spawn_heavy, parallel_concurrent, race_concurrent). The rest run in single-threaded stub mode, testing CPS compiler output but NOT real concurrent execution. Error propagation paths are especially undertested in concurrent mode.

**Approach:** Add `# mode: concurrent` to the following test files. Fix any failures that surface (these would be real bugs exposed by concurrent execution):

| Test file | What it exercises |
|---|---|
| `spawn_multi.jacl` | Multiple sequential spawns |
| `spawn_nested.jacl` | Spawn within spawn |
| `parallel_error.jacl` | Error propagation in parallel |
| `parallel_with_await.jacl` | Parallel with nested await |
| `parallel_await_error.jacl` | Await error inside parallel |
| `race_basic.jacl` | Basic race semantics |
| `race_error.jacl` | Error in race branch |
| `async_error_spawn.jacl` | Error from spawned task |
| `async_error_propagation.jacl` | Error propagation through CPS |
| `async_error_parallel.jacl` | Error in parallel branch |

**Acceptance Criteria:**
- [ ] All listed test files have `# mode: concurrent` added
- [ ] All listed tests pass in concurrent mode (fix any real bugs discovered)
- [ ] No regressions in existing tests
- [ ] Document any bugs found and fixed during this process

### US-007: Add targeted GC+concurrency intersection tests

**Description:** As a developer, I need tests that exercise the specific GC+concurrency failure scenarios identified in the audit to prevent regressions.

**Background:** The current test suite has good unit coverage for GC and concurrency separately, and some stress tests for their intersection. But specific interleaving scenarios from the design document are untested.

**Approach:** Add the following C-level tests (in `test_runtime.c` or a new `test_gc_concurrent.c`):

1. **Mid-CPS-chain GC:** Create a chain of 3+ CPS continuations where intermediate continuation upvalues are the ONLY reference to certain objects. Trigger GC mid-chain. Verify all objects survive.

2. **Grey buffer processing verification:** Set up a scenario where a write barrier fires during active GC (atom reset while gc_active=1). Verify that the grey buffer entry is actually processed during the mark phase (the old value survives collection).

3. **Unrooted result regression test:** (After US-001 fix) Create a future, resolve it with a heap-allocated result, drop all references to the future, trigger GC, then execute the awaiting continuation. Verify the result is intact.

4. **Emergency GC / OOM escalation:** Set `GC_MAX_HEAP_BLOCKS` to a very low value (e.g., 4). Allocate until OOM escalation triggers. Verify Tier 1 (emergency GC) fires and reclaims memory. Verify Tier 2 (heap growth) works. Verify Tier 3 (panic) fires with the custom OOM handler.

5. **Parallel join under GC pressure:** Run a parallel with N tasks that allocate heavily. Trigger GC right after all tasks complete but before the join continuation executes. Verify the result vector and all individual results survive.

**Acceptance Criteria:**
- [ ] Test for mid-CPS-chain GC survival added and passing
- [ ] Test for grey buffer entry processing added and passing
- [ ] Test for unrooted result (regression test for US-001/US-002) added and passing
- [ ] Test for OOM Tier 1 (emergency GC) added and passing
- [ ] Test for OOM Tier 2 (heap growth) added and passing
- [ ] Test for OOM Tier 3 (panic handler) added and passing
- [ ] Test for parallel join under GC pressure added and passing
- [ ] All new tests run as part of the standard test suite

## Functional Requirements

- FR-1: `RuntimeTask` must support scanning two GC root values during root enumeration
- FR-2: All task submission sites must populate both root slots with appropriate values
- FR-3: `gc_enumerate_roots`, `gc__scan_deque`, and inbox scanning must scan both root slots
- FR-4: `GreyBuffer.count` must use release/acquire atomic ordering between writer and reader threads
- FR-5: Worker loop must not acquire inbox mutex when inbox is empty
- FR-6: Worker loop must batch-drain all inbox tasks in a single lock acquisition
- FR-7: `JaclFuture` must not hold OS resources that require finalization
- FR-8: Future lock/unlock must use CAS-based spinlock with acquire/release ordering
- FR-9: At least 14 concurrent .jacl test files must run with `# mode: concurrent`
- FR-10: Test suite must cover mid-CPS GC, grey buffer processing, OOM escalation, and parallel-join-under-GC scenarios

## Non-Goals

- No generational GC in this milestone (deferred per design doc Section 11)
- No channels or message passing (deferred to future milestone)
- No cooperative cancellation for race losers (deferred)
- No parallel marking or parallel sweeping (single GC thread is retained)
- No MPSC per-worker mailboxes (batched drain is sufficient; escalate if profiling shows need)
- No changes to the CPS compiler or suspension analysis
- No changes to the Chase-Lev deque implementation

## Technical Considerations

- **Unity build order matters.** `gc.c` defines `GreyBuffer` before `vm.c` and `gc_collect.c`. Atomic changes to `GreyBuffer.count` must use `platform.h` macros that are already available at that point in the build.
- **Spinlock fairness.** The future spinlock is held for ~10 instructions. No fairness concern at this hold time. If future contention grows (e.g., many waiters racing to register), consider a ticket lock, but this is unlikely given the mutex-protected state machine.
- **Test determinism.** Concurrent tests are inherently non-deterministic. The new intersection tests (US-007) should use allocation pressure and GC triggers rather than timing to create the desired interleavings. Set `GC_THRESHOLD` low to force frequent GC.
- **Cross-platform atomics.** The `platform.h` abstraction supports C11, GCC builtins, and MSVC intrinsics. All atomic changes should use the `ATOMIC_LOAD_EXPLICIT` / `ATOMIC_STORE_EXPLICIT` / `ATOMIC_CAS` macros, not raw C11 atomics.

## Success Metrics

- Zero known soundness bugs in GC/concurrency intersection
- Zero undefined behavior under C11 memory model (verifiable by running tests under ThreadSanitizer if available)
- 14+ .jacl concurrent tests running in real concurrent mode (up from 4)
- 7+ new targeted GC+concurrency intersection tests
- No performance regression in existing stress tests (memory_bounded, concurrent_gc_stress, concurrent_gc_multi_cycle)

## Open Questions

- Should we run the test suite under ThreadSanitizer (TSan) as part of CI? TSan would catch data races automatically but requires clang and may have false positives with custom atomics.
- Should the `gc_root2` approach be documented in `GC_CONCURRENCY_DESIGN.md` as a design amendment, or is the PRD sufficient?
