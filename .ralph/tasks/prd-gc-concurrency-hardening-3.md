# PRD: M16 — GC/Concurrency Hardening Phase 3

## Introduction

A comprehensive audit of the JACL GC and concurrency runtime — following the M14 and M15 hardening rounds — identified one race condition bug, several robustness concerns requiring documentation, significant code duplication in the runtime task submission/execution paths, and gaps in concurrent stress test coverage.

The architecture (Immix-style per-thread heaps, epoch watermarking, CPS-based root tracking, hybrid write barriers) is sound. This milestone focuses on fixing the one real race, documenting subtle safety invariants, reducing code duplication for maintainability, and adding stress tests that exercise the concurrent GC under realistic multi-threaded pressure.

## Goals

- Fix the deferred block recycling race that can corrupt heap block lists under concurrent allocation
- Document subtle memory-ordering safety invariants (grey buffer realloc, line_map memcpy, epoch truncation) with regression tests
- Reduce runtime.c code duplication by extracting common inbox-push, VM-setup, and parallel-completion helpers
- Add concurrent stress tests that exercise GC under multi-threaded allocation and high task churn

## User Stories

### US-001: Fix deferred block recycling race

**Description:** As a runtime developer, I need deferred block recycling to happen safely so that the heap block list is never corrupted by concurrent allocator access.

**Background:** In `gc_concurrent_collect` (runtime.c:739-762), after setting `gc_running = 0`, the GC thread walks each worker's `heap->blocks` linked list to unlink fully-empty deferred blocks. But once `gc_running = 0`, workers resume normal operation and `gc_alloc`'s slow path (`gc__find_fit_in_block`) also iterates the same linked list. This is a data race on the singly-linked list: the GC thread may unlink a block (modifying `prev->next`) while a worker is traversing through that same node.

**Fix approach:** Move block unlinking into `gc_sweep_concurrent` itself. During the sweep walk, we already have `prev` and `block` pointers for the linked list traversal. When a block is identified as fully empty, unlink it inline (set `prev->next = block->next`) and return it to the pool immediately. This eliminates the deferred block list, the `deferred_out`/`deferred_count_out` parameters, and the entire post-sweep unlinking loop in `gc_concurrent_collect`.

**Acceptance Criteria:**
- [ ] `gc_sweep_concurrent` in `gc_collect.c`: when a block is fully empty, unlink it from the heap's block list inline and call `gc_block_pool_return` directly
- [ ] Remove the `deferred_out` and `deferred_count_out` parameters from `gc_sweep_concurrent`
- [ ] Remove the `MAX_DEFERRED_BLOCKS` constant and the deferred block arrays in `gc_concurrent_collect`
- [ ] Remove the post-sweep deferred block unlinking loop (runtime.c lines 741-765)
- [ ] `gc_sweep_concurrent` must still skip `skip_block` (the owning worker's active allocation block)
- [ ] `gc_sweep_concurrent` receives a `BlockPool *` parameter (to return blocks to the pool)
- [ ] All existing GC tests pass (`test_gc.c` and all `.jacl` integration tests)

### US-002: Document grey buffer realloc safety with regression test

**Description:** As a runtime developer, I need the grey buffer's realloc-during-concurrent-read safety to be explicitly documented and tested so that future changes don't accidentally break the invariant.

**Background:** `grey_buf_push` (gc.c:635) may call `realloc` on `gb->entries` while the GC thread reads from the same array in `gc__drain_grey_bufs`. This is currently safe because:
1. The GC reads `count` with `MEM_ACQUIRE` before accessing `entries[]`
2. The worker stores to `gb->entries` (after realloc) and to `gb->entries[c]` before the `MEM_RELEASE` store to `count`
3. The release/acquire pair on `count` establishes a happens-before relationship that makes both the updated `entries` pointer and the new entry visible to the GC

This reasoning is correct but subtle and fragile. It must be documented inline.

**Acceptance Criteria:**
- [ ] Add a detailed comment block above `grey_buf_push` in `gc.c` explaining: (a) the race scenario (GC reads entries while worker reallocs), (b) why it's safe (release on count happens after realloc + entry write; acquire on count in `gc__drain_grey_bufs` sees both), (c) the critical invariant: the `gb->entries` pointer store and the `gb->entries[c]` store must both happen-before the release store to `count`
- [ ] Add a matching comment in `gc__drain_grey_bufs` (runtime.c) referencing the invariant
- [ ] Add a C unit test in `test_gc.c` that exercises grey buffer growth: push `GREY_BUF_INIT_CAP + 100` entries, verify all entries are readable and correct after the realloc
- [ ] All existing tests pass

### US-003: Document line_map memcpy safety on weakly-ordered architectures

**Description:** As a runtime developer, I need the line_map concurrent memcpy reasoning documented for ARM/RISC-V so that the benign race is understood and TSan annotations are justified.

**Background:** In `gc_sweep_concurrent` (gc_collect.c:509), `memcpy(block->line_map, new_map, GC_LINES_PER_BLOCK)` races with `gc__find_free_run` on the owning worker thread. On x86, byte stores are atomic, so torn reads cannot occur. On ARM, a wide-store memcpy implementation could produce a torn byte read yielding a value that is neither `FREE(0)` nor `OCCUPIED(1)`. However, `gc__find_free_run` only checks `== GC_LINE_FREE` and `!= GC_LINE_FREE`, so any non-zero torn value is treated as OCCUPIED — the allocator skips the line (harmless, just misses free space temporarily).

Additionally, the sweep only transitions lines from OCCUPIED to FREE (never the reverse), and dead object memory is zeroed in Phase 1 before the line map changes. So even if the allocator "sees" a line transition to FREE mid-memcpy, the underlying memory is already zeroed and safe to allocate into.

**Acceptance Criteria:**
- [ ] Expand the existing comment block at `gc_collect.c:495-508` to include: (a) ARM/RISC-V analysis: torn byte reads produce non-0 values treated as OCCUPIED, (b) the one-directional invariant (OCCUPIED→FREE only during sweep), (c) Phase 1 zeroing ensures freed memory is safe regardless of line map visibility timing
- [ ] Add a note about TSan suppression if this code path is annotated
- [ ] All existing tests pass

### US-004: Document epoch truncation from uint64_t to uint32_t

**Description:** As a runtime developer, I need the epoch field width mismatch documented so that the truncation is intentional and understood.

**Background:** `global_epoch` in `Runtime` is `uint64_t`, but `GCHeader.epoch` is `uint32_t` (to keep the 8-byte header compact). The truncation at `gc__bump_alloc` (gc.c:305) via `hdr->epoch = gc__thread_epoch` (which is also `uint32_t`, set from the `uint64_t thread_epoch`) means watermark comparison wraps after 2^32 GC cycles. At realistic GC rates (even 1000 cycles/sec), this is ~50 days of continuous execution. The watermark comparison `hdr->epoch >= watermark` would give false positives (protecting objects that should be collectible) rather than false negatives, so wraparound is conservative, not dangerous.

**Acceptance Criteria:**
- [ ] Add a comment on the `GCHeader.epoch` field in `gc.c` explaining: the field is intentionally `uint32_t` for header compactness, truncated from `uint64_t global_epoch`, wraps after ~4B cycles (conservative on wraparound)
- [ ] Add a comment at `gc__bump_alloc` where `hdr->epoch = gc__thread_epoch` explaining the truncation
- [ ] All existing tests pass

### US-005: Extract runtime inbox push helper

**Description:** As a runtime developer, I want a single `runtime__push_inbox` helper so that the inbox push pattern is defined once and all 5 call sites are simplified.

**Background:** The following functions all contain the identical pattern: `malloc(RuntimeTask)`, set fields, `MUTEX_LOCK(inbox_mutex)`, grow inbox if full, push task, `MUTEX_UNLOCK`:
- `runtime_submit` (runtime.c:405-421)
- `runtime_submit_task` (runtime.c:455-475)
- `runtime__submit_spawn_task` (runtime.c:1189-1214)
- `runtime__submit_parallel_task` (runtime.c:1357-1386)
- `runtime__submit_race_task` (runtime.c:1478-1505)
- `runtime__schedule_continuation` (runtime.c:1065-1090)

**Acceptance Criteria:**
- [ ] New static function `runtime__push_inbox(Runtime *rt, RuntimeTask *task)` that handles: lock inbox mutex, grow if `inbox_count >= inbox_cap`, store task, increment count, unlock
- [ ] All 6 call sites refactored to: create `RuntimeTask`, set its fields, call `runtime__push_inbox`
- [ ] Net reduction of ~60 lines of duplicated code
- [ ] All existing tests pass

### US-006: Extract VM reset + closure frame setup helper

**Description:** As a runtime developer, I want a single helper for "reset VM state and set up a closure call frame" so that the 5 task execution functions are shorter and less error-prone.

**Background:** These functions all duplicate the same ~15-line VM reset + frame setup sequence:
- `runtime__exec_closure` (runtime.c:427-453)
- `runtime__continuation_task_exec` (runtime.c:1018-1063)
- `runtime__spawn_task_exec` (runtime.c:1112-1187)
- `runtime__parallel_task_exec` (runtime.c:1231-1355)
- `runtime__race_task_exec` (runtime.c:1402-1476)

The pattern is: zero `stack_top`, `frame_count`, error fields, stack_trace; push closure and optional args to stack; set `frames[0]` fields; set `ip`, `chunk`, `top_chunk`.

**Acceptance Criteria:**
- [ ] New static function `runtime__setup_call(VM *vm, JaclClosure *cl, int argc, JaclVal *argv)` that: resets VM state, pushes closure + args to stack, sets up `frames[0]`, sets `ip`/`chunk`/`top_chunk`
- [ ] `argc` is the number of arguments (0 for no-arg calls, 1 for single-arg like continuation/CPS calls)
- [ ] `argv` is a pointer to the argument values (NULL if argc==0)
- [ ] All 5 task execution functions refactored to use the helper
- [ ] Net reduction of ~60 lines of duplicated code
- [ ] All existing tests pass

### US-007: Deduplicate parallel completion logic

**Description:** As a runtime developer, I want the non-CPS parallel completion path to call `runtime__complete_parallel_slot` so the logic exists in exactly one place.

**Background:** The non-CPS path in `runtime__parallel_task_exec` (runtime.c lines 1304-1351) duplicates `runtime__complete_parallel_slot` (runtime.c lines 885-941) almost verbatim: store result, CAS error, atomic increment completed, check if last, build vector, schedule continuation. The non-CPS path should simply call `runtime__complete_parallel_slot` after computing `task_result`.

**Acceptance Criteria:**
- [ ] Non-CPS branch of `runtime__parallel_task_exec` calls `runtime__complete_parallel_slot(self->runtime, vm, ptd->agg_val, ptd->index, task_result)` instead of duplicating the logic inline
- [ ] Remove the duplicated ~45 lines of parallel completion code from the non-CPS branch
- [ ] All existing parallel tests pass (both CPS and non-CPS paths)

### US-008: Add concurrent allocation + GC stress test

**Description:** As a runtime developer, I need a C stress test that exercises concurrent allocation across multiple threads while a GC cycle runs, to verify that per-thread heap isolation and block pool access are correct under contention.

**Background:** Current GC tests are single-threaded. The concurrent GC has never been stress-tested at the C level with multiple threads allocating simultaneously during a collection cycle.

**Acceptance Criteria:**
- [ ] New test function `gc_concurrent_alloc_stress` in `test_gc.c`
- [ ] Test creates 4 pthreads, each with its own `ThreadHeap` sharing a single `BlockPool`
- [ ] Each thread performs 12,500 allocations (50,000 total) of varying sizes (16–2048 bytes), writing a known pattern to each allocation
- [ ] After all allocations, a single-threaded sweep is run on each heap
- [ ] Verify: no corruption (known patterns intact for live objects), no double-free, block pool accounting is consistent (`total_blocks_allocated` matches actual blocks in use + free list)
- [ ] Test completes without TSan/ASan errors when compiled with sanitizers
- [ ] Test runs in < 5 seconds

### US-009: Add grey buffer growth + drain integration test

**Description:** As a runtime developer, I need a test that forces grey buffer realloc and verifies that the GC correctly drains all entries, including those added after the buffer was reallocated.

**Acceptance Criteria:**
- [ ] New test function `gc_grey_buffer_drain_after_realloc` in `test_gc.c`
- [ ] Test creates a `GreyBuffer`, pushes `GREY_BUF_INIT_CAP * 3` entries (forces at least one realloc)
- [ ] Simulates GC drain: reads `count` with acquire, iterates `entries[0..count)`, verifies all values match what was pushed
- [ ] Verify the buffer's `cap` grew (at least one realloc occurred)
- [ ] All pushed values are heap-type `JaclVal`s pointing to real GC-allocated objects (not just sentinel values)
- [ ] Test passes under ASan

### US-010: Add high task churn .jacl integration test

**Description:** As a runtime developer, I need a .jacl test that spawns 500+ concurrent tasks under GC pressure to stress timing-sensitive race windows in the concurrent GC.

**Acceptance Criteria:**
- [ ] New file `test/jacl/gc_high_churn.jacl`
- [ ] Test spawns 500 tasks, each performing a small computation that allocates (e.g., building a small vector or map, calling fib(8))
- [ ] All 500 results are collected and verified for correctness
- [ ] Test runs in concurrent mode (the test harness's concurrent execution path)
- [ ] Test completes without hanging, crashing, or producing incorrect results
- [ ] Test runs in < 10 seconds
- [ ] Add the test to the test harness expected-output list

### US-011: Add OOM escalation test

**Description:** As a runtime developer, I need tests for all 3 tiers of OOM escalation to verify that emergency GC, heap growth, and panic handling work correctly.

**Acceptance Criteria:**
- [ ] New test function `gc_oom_tier1_emergency_gc` in `test_gc.c`: set `max_blocks = 2`, fill both blocks, install a mock emergency GC callback that frees objects in one block, verify next allocation succeeds by reclaiming freed space
- [ ] New test function `gc_oom_tier2_heap_growth` in `test_gc.c`: set `max_blocks = 2`, fill both blocks, no emergency GC callback, verify allocation succeeds by growing to block 3 (increase `max_blocks` in the emergency callback or verify the tier-2 growth path is taken)
- [ ] New test function `gc_oom_tier3_panic` in `test_gc.c`: set `max_blocks = 1`, fill the block, install custom `gc__oom_handler` that sets a flag instead of aborting, verify the handler is called with correct arguments
- [ ] All existing tests still pass

## Functional Requirements

- FR-1: Deferred block recycling must happen during the sweep walk, not in a post-sweep phase after `gc_running` is cleared
- FR-2: `gc_sweep_concurrent` must unlink fully-empty blocks inline and return them to the shared block pool
- FR-3: Grey buffer realloc safety invariant must be documented at both the writer (`grey_buf_push`) and reader (`gc__drain_grey_bufs`) sites
- FR-4: Line map concurrent memcpy must be documented with ARM/RISC-V torn-read analysis
- FR-5: Epoch field truncation must be documented at the type definition and the assignment site
- FR-6: All inbox push operations must go through a single `runtime__push_inbox` helper
- FR-7: All task execution VM setup must go through a single `runtime__setup_call` helper
- FR-8: Non-CPS parallel completion must call `runtime__complete_parallel_slot`, not duplicate it
- FR-9: Stress tests must run under ASan/TSan without errors
- FR-10: All changes must maintain backward compatibility with single-threaded GC path (`gc_collect`)

## Non-Goals

- No changes to the GC algorithm itself (mark-sweep, epoch watermarking, hybrid barriers)
- No changes to the write barrier implementation
- No changes to the BUSY sentinel protocol or root enumeration
- No changes to the single-threaded GC path (`gc_mark`/`gc_sweep`/`gc_collect`)
- No performance benchmarking or optimization (this is a correctness/maintainability milestone)
- No changes to the Chase-Lev deque, HAMT, or RRB implementations

## Technical Considerations

- **Unity build**: All files are `#include`d into a single translation unit. Static functions are shared across files. New helpers in `runtime.c` are automatically visible to all call sites.
- **Thread safety of inline block recycling**: When `gc_sweep_concurrent` unlinks a block, the owning worker thread may be allocating into a *different* block (the `skip_block`). Since `skip_block` is excluded from sweep, and the allocator only walks `heap->blocks` in the slow path (not during bump allocation), unlinking non-active blocks during sweep is safe — the allocator's slow path and the sweep are not concurrent on the same heap (sweep happens on the GC thread while the worker is executing a task, and allocation only happens on the worker thread).
- **Grey buffer realloc**: The current design is safe but the safety depends on the compiler not reordering the `entries` pointer store after the `count` release store. The `realloc` return value has a data dependency on the store, so this is guaranteed in practice, but the comment should note this.
- **Test infrastructure**: C tests use the existing `test_gc.c` framework with `ASSERT_*` macros. `.jacl` tests use the file-based test harness with expected output matching.

## Success Metrics

- Zero TSan/ASan warnings when running the full test suite with sanitizers
- All existing tests continue to pass (no regressions)
- `runtime.c` shrinks by ~120 lines from deduplication (US-005 + US-006 + US-007)
- New stress tests catch the deferred block race if the fix is reverted

## Open Questions

- Should we add a TSan suppression file for the (documented, benign) line_map memcpy race, or leave it to developers to suppress locally?
- Should the `runtime__setup_call` helper also handle the `vm__run` call, or just setup? (Recommendation: just setup — callers need different post-run logic.)
