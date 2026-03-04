# PRD: M15 — GC/Concurrency Hardening Phase 2

## Introduction

A deep review of the JACL GC and concurrency subsystems — following the M14 hardening round — identified one concrete bug, two soundness/memory concerns, two minor races, and significant test coverage gaps. The overall architecture (Immix-style per-thread heaps, epoch watermarking, CPS-based root elimination, hybrid write barriers) remains sound, but these issues affect correctness at edge cases, memory usage in long-running programs, and formal C11 compliance.

This milestone addresses all findings in priority order: a correctness bug first, then soundness fixes, then memory management improvements, and finally expanded test coverage.

## Goals

- Fix the `alloc_total` uint16_t overflow bug that makes block-sized objects invisible to GC
- Close the grey buffer final drain gap that can miss write barrier entries
- Enable concurrent block recycling so long-running programs don't leak memory per-worker
- Eliminate the TOCTOU race in block pool limit enforcement
- Document the benign line map memcpy data race for TSan suppression
- Add targeted tests for all fixed issues plus fragmentation and memory boundedness

## User Stories

### US-001: Fix alloc_total uint16_t overflow at block boundary

**Description:** As a runtime developer, I need the GC allocator to reject allocations that would overflow the `alloc_total` field so that all allocated objects are visible to the sweep phase.

**Background:** In `gc.c:317`, the guard `total > GC_BLOCK_SIZE` allows `total == GC_BLOCK_SIZE` (65536) to pass. When `hdr->alloc_total = (uint16_t)65536` is stored, it wraps to 0. The sweep interprets `alloc_total == 0` as "no object here" and skips to the next line boundary — the object becomes invisible to GC. It is never marked, never swept, and its lines are never freed. This occurs when `payload_size == 65528` (65536 - 8 byte header, after 8-byte alignment).

**Acceptance Criteria:**
- [ ] `gc.c` line 317: change `total > GC_BLOCK_SIZE` to `total >= GC_BLOCK_SIZE`
- [ ] New unit test in `test_gc.c`: attempt `gc_alloc(heap, OBJ_STRING, 65528)` — verify it returns `NULL`
- [ ] New unit test: attempt `gc_alloc(heap, OBJ_STRING, 65520)` (max valid size after alignment) — verify it succeeds and `gc_header_of(result)->alloc_total > 0`
- [ ] All existing tests pass
- [ ] Compiles without warnings

### US-002: Close grey buffer final drain gap

**Description:** As a runtime developer, I need the concurrent GC mark phase to fully drain all grey buffer entries before disabling write barriers so that no write barrier entries are lost.

**Background:** In `gc_concurrent_collect` (runtime.c:665-678), the final grey buffer drain happens at line 665, followed by processing remaining mark stack entries (668-673). During this processing, workers can still push to grey buffers (gc_active is still true, disabled at line 678). Any entries pushed between the final drain and `gc_active=false` are never processed. Under high mutation rates (many concurrent `swap!`/`reset!` operations), this could cause the SATB deletion barrier to miss an old value, leading to premature collection of objects reachable only through evicted container references.

**Approach:** Replace the single final drain + mark with a convergence loop: repeatedly drain grey buffers and process the mark stack until both are empty, then set `gc_active = false`. Add a maximum iteration cap (e.g., 8 rounds) with a final forced drain to prevent unbounded looping under pathological mutation rates.

**Acceptance Criteria:**
- [ ] `gc_concurrent_collect` final drain (after line 665) replaced with convergence loop:
  ```
  int rounds = 0;
  do {
      gc__drain_grey_bufs(rt, &ms, drained);
      while (gc__ms_pop(&ms, &ptr)) {
          GCHeader *hdr = gc_header_of(ptr);
          if (hdr->mark == mark) continue;
          hdr->mark = mark;
          gc__trace_object(ptr, &ms);
      }
      rounds++;
  } while (has_new_entries && rounds < GC_FINAL_DRAIN_MAX_ROUNDS);
  ```
- [ ] `GC_FINAL_DRAIN_MAX_ROUNDS` defined as 8 (configurable)
- [ ] `gc__drain_grey_bufs` returns a boolean or count indicating whether new entries were found
- [ ] After the convergence loop, `gc_active` set to false
- [ ] All existing tests pass
- [ ] Compiles without warnings

### US-003: Add deferred block recycling to concurrent sweep

**Description:** As a runtime developer, I need concurrent GC to return fully-empty blocks to the shared pool so that long-running programs don't accumulate unused memory per worker.

**Background:** `gc_sweep_concurrent` explicitly avoids block recycling to prevent racing with `gc_alloc`'s slow-path block list traversal. This means blocks allocated to a worker's heap are never returned to the shared `BlockPool` during concurrent GC. For long-running programs where workload shifts between workers (or where a worker's objects become mostly short-lived), memory grows monotonically per worker.

**Approach:** During `gc_sweep_concurrent`, collect pointers to fully-empty blocks into a thread-local list (without unlinking them from the heap's block list). After `gc_running` is set to false (step 10 in `gc_concurrent_collect`), iterate the deferred list: unlink each block from the owning heap's block list and return it to the shared pool. This is safe because `gc_running == false` means no concurrent GC is modifying block lists, and each heap is only allocated from by its owning thread (which is not in gc_alloc's slow path at this point — between tasks).

**Acceptance Criteria:**
- [ ] New struct or array in `gc_concurrent_collect` scope: `GCBlock *deferred_free[MAX_DEFERRED]` per worker (or dynamically allocated)
- [ ] `gc_sweep_concurrent` gains an output parameter: list/count of fully-empty blocks (identified when all lines in `new_map` are `GC_LINE_FREE`)
- [ ] After step 10 (`gc_running = false`) in `gc_concurrent_collect`, iterate deferred blocks: unlink from heap's block list, call `gc_block_pool_return`
- [ ] Ensure `current_block` is never deferred (already skipped by sweep)
- [ ] New unit test: allocate many objects on a worker, let them all die, trigger concurrent GC, verify blocks returned to pool (pool free list grows)
- [ ] New `.jacl` integration test: long-running loop that allocates heavily in phases, verify memory stays bounded (not monotonically growing)
- [ ] All existing tests pass
- [ ] Compiles without warnings

### US-004: Fix TOCTOU in gc_block_pool_get

**Description:** As a runtime developer, I need the block pool limit check and allocation to be atomic so that concurrent workers cannot exceed `max_blocks`.

**Background:** In `gc_block_pool_get` (gc.c:120-131), the limit check reads `total_blocks_allocated` under the lock, releases the lock, calls `malloc`, then re-acquires the lock to increment the counter. Two threads can both pass the check simultaneously, each allocating a block and both incrementing, exceeding `max_blocks` by up to `num_workers - 1`.

**Acceptance Criteria:**
- [ ] `gc_block_pool_get` restructured: lock held from limit check through counter increment
  - Read `total_blocks_allocated` under lock
  - If at limit: unlock, return NULL
  - Otherwise: increment counter, unlock, then malloc
  - If malloc fails: re-lock, decrement counter, unlock, return NULL
- [ ] The `malloc` call itself remains outside the lock (to avoid blocking other threads on a potentially slow system call)
- [ ] New unit test: verify that with `max_blocks = 2`, exactly 2 blocks can be allocated (not 3+)
- [ ] All existing tests pass
- [ ] Compiles without warnings

### US-005: Document benign line map memcpy data race

**Description:** As a runtime developer, I need the concurrent sweep's line map update to be clearly documented as a benign data race so that future maintainers understand the safety argument and TSan suppressions are justified.

**Background:** In `gc_sweep_concurrent` (gc_collect.c:487), `memcpy(block->line_map, new_map, GC_LINES_PER_BLOCK)` races with `gc_alloc`'s slow path on the owning worker, which may be scanning `line_map` entries in `gc__find_free_run`. This is technically undefined behavior under C11. In practice it is benign: the sweep only transitions lines from OCCUPIED to FREE (Phase 1 already zeroed dead objects), so gc_alloc either sees the old map (misses some free space, harmless) or the new map (correct). No incorrect allocation can result.

**Acceptance Criteria:**
- [ ] Replace the comment `/* Phase 3: atomically swap line map */` with a detailed safety argument:
  ```
  /* Phase 3: update line map.
   *
   * NOTE: This memcpy races with gc_alloc's slow path (gc__find_free_run)
   * on the owning worker thread reading line_map entries. This is technically
   * a data race under C11, but is benign in practice:
   *
   * - Sweep only transitions lines OCCUPIED -> FREE (never FREE -> OCCUPIED)
   * - Phase 1 already zeroed dead object memory before line map changes
   * - gc_alloc seeing a partially-updated map either:
   *   (a) sees OCCUPIED for a now-free line -> misses free space (harmless)
   *   (b) sees FREE for a freed line -> allocates into zeroed memory (correct)
   * - No incorrect allocation or use-after-free can result
   *
   * If TSan reports this, suppress with an annotation or __attribute__((no_sanitize("thread"))).
   */
  ```
- [ ] All existing tests pass
- [ ] Compiles without warnings

### US-006: Add unit tests for edge cases

**Description:** As a runtime developer, I need unit tests covering the fixed edge cases so that regressions are caught immediately.

**Background:** The following scenarios lack test coverage and correspond to issues fixed or documented in US-001 through US-005.

**Acceptance Criteria:**
- [ ] **Fragmentation test**: allocate objects of mixed sizes (small, medium, multi-line), free alternating objects (via GC), allocate again — verify new allocations succeed using freed line runs (no unnecessary block growth)
- [ ] **Multiple GC cycles with mixed lifetimes**: run 10+ GC cycles where each cycle some objects survive and others die — verify heap doesn't grow unboundedly and mark-bit alternation works correctly across all cycles
- [ ] **alloc_total boundary values**: test allocations at payload sizes 65512, 65520, 65528, 65536 — verify correct accept/reject behavior and that `alloc_total` is never 0 for accepted allocations
- [ ] **Block pool limit enforcement**: with `max_blocks = N`, allocate N+1 blocks — verify the (N+1)th returns NULL
- [ ] All new tests in `test_gc.c`
- [ ] All tests pass

### US-007: Add integration tests for concurrent GC scenarios

**Description:** As a runtime developer, I need `.jacl` integration tests that exercise GC under realistic concurrent workloads so that the grey buffer convergence, block recycling, and write barrier correctness are validated end-to-end.

**Background:** Current tests cover basic spawn/await and parallel/race semantics, but lack stress tests for high-mutation scenarios during active GC.

**Acceptance Criteria:**
- [ ] **Write barrier stress test** (`barrier_stress.jacl`): spawn N workers that concurrently `swap!` atoms in a tight loop while other workers allocate heavily (forcing GC). Verify all final atom values are consistent (no use-after-free or corruption). Use `# mode: concurrent`.
- [ ] **Memory boundedness test** (`gc_memory_bounded.jacl`): run a loop of 100+ iterations where each iteration spawns tasks that allocate and discard large vectors. Verify program completes without OOM (tests that block recycling keeps memory bounded). Use `# mode: concurrent`.
- [ ] **Multi-cycle GC correctness** (`gc_multi_cycle.jacl`): chain 10+ sequential spawn/await calls with intermediate heap allocations, forcing multiple GC cycles. Verify all intermediate values survive correctly across cycles. Use `# mode: concurrent`.
- [ ] All new test files have `# mode: concurrent` and expected output
- [ ] All tests pass via the test harness

## Functional Requirements

- FR-1: `gc_alloc` must reject allocations where the total size (header + payload + alignment) would overflow `uint16_t` (i.e., `total >= 65536`)
- FR-2: The concurrent GC mark phase must drain all grey buffer entries before disabling write barriers, using a convergence loop with a bounded iteration cap
- FR-3: Concurrent GC sweep must collect fully-empty blocks and return them to the shared pool after the GC cycle completes (after `gc_running` is reset)
- FR-4: Block pool limit enforcement must be atomic — no TOCTOU window between checking and incrementing `total_blocks_allocated`
- FR-5: The concurrent sweep line map update must be documented with a clear safety argument explaining why the data race is benign
- FR-6: All fixes must have corresponding unit tests in `test_gc.c`
- FR-7: Concurrent GC scenarios must have `.jacl` integration tests running in concurrent mode

## Non-Goals

- No changes to the GC algorithm itself (mark-sweep with Immix line granularity)
- No compaction or defragmentation — fragmentation is mitigated by line-granular reclamation but not eliminated
- No generational collection or nursery
- No formal verification of memory ordering (would require tools like GenMC or CDSChecker)
- No cancellation for `race` losers (deferred to future work)
- No per-worker inbox optimization (inbox cache-line bouncing is a minor concern at current scale)

## Technical Considerations

- **Unity build**: All `.c` files are included in a single compilation unit. Changes to `gc.c`, `gc_collect.c`, and `runtime.c` must respect include order: `gc.c` -> `gc_collect.c` -> `vm.c` -> `runtime.c`.
- **Platform atomics**: Use `ATOMIC_LOAD_EXPLICIT`, `ATOMIC_STORE_EXPLICIT`, `ATOMIC_CAS` from `platform.h`. Avoid bare `volatile` accesses for new code.
- **Grey buffer drain return value**: `gc__drain_grey_bufs` currently returns void. US-002 requires it to indicate whether new entries were found (return bool or uint32_t count).
- **Deferred block list**: US-003's deferred free list should be stack-allocated with a reasonable cap (e.g., 64 blocks per worker) to avoid malloc during GC. If exceeded, remaining empty blocks are left in-place (collected next cycle).
- **Test harness**: `.jacl` integration tests use `# mode: concurrent` header and `# expect:` comments. See existing tests in `tests/concurrent/` for the pattern.

## Success Metrics

- The `alloc_total` overflow is impossible (rejected by allocator)
- Grey buffer entries are fully drained before mark phase ends (convergence loop)
- Long-running programs' memory usage stabilizes (blocks recycled to shared pool)
- Block pool limit is never exceeded by more than 0 blocks (atomic enforcement)
- All new tests pass under both single-threaded and concurrent modes
- No regressions in existing test suite

## Open Questions

- Should the convergence loop cap (US-002) trigger a warning/diagnostic if hit? High mutation rates hitting the cap could indicate a pathological workload.
- Should deferred block recycling (US-003) have a minimum block retention policy (e.g., always keep at least 2 blocks per worker to avoid frequent pool contention)?
- Is the current `GC_MAX_HEAP_BLOCKS` default of 4096 (256MB) appropriate, or should it be configurable at runtime?
