# PRD: M12/M13 Hardening — Lexer Overflow, Escape Analysis, Generational GC, E2E Stress Tests

## Introduction

Four hardening tasks for the JACL runtime, targeting correctness gaps identified during audit. The lexer silently truncates large integer literals. The compiler's mutable-cell pinning can be bypassed via closure escaping. The GC lacks generational collection, causing unnecessary re-tracing of long-lived objects. And the concurrent test suite doesn't stress GC-under-load deeply enough.

These are ordered by dependency: the lexer fix and escape analysis are independent and go first. Generational GC follows (larger, self-contained). E2E stress tests come last so they cover everything including generational behavior.

## Goals

- Eliminate silent data corruption from integer literal overflow in the lexer
- Close the mutable-cell escape hole so box references can't leak across threads undetected
- Implement non-moving generational GC (sticky mark-bit) to reduce tracing cost for long-lived objects
- Add targeted stress tests that exercise GC under realistic concurrent load

## User Stories

### US-001: Lexer rejects integer literals outside i32 range

**Description:** As a JACL developer, I want the lexer to produce an error when a bare integer literal exceeds the i32 range, so I don't get silently truncated values.

**Acceptance Criteria:**
- [ ] Hex literals: if parsed value > `INT32_MAX` or < `INT32_MIN`, the lexer emits a `TOKEN_ERROR` with message `"integer literal out of i32 range — use [i64 ...] or [u64 ...]"`
- [ ] Binary literals: same range check and error
- [ ] Decimal literals: same range check and error (both positive and negative overflow)
- [ ] Negative decimals: the lexer doesn't parse negative literals (negation is a separate op), so only check `val > INT32_MAX`
- [ ] Hex values that fit in u32 but not i32 (e.g. `0xFFFFFFFF`) still error — the user must use `[u32 0xFFFFFFFF]`
- [ ] Values that fit in i32 continue to work identically (no behavior change)
- [ ] Add lexer tests for: `0xFFFFFFFF`, `0x80000000`, `2147483648`, `0b` followed by 33 ones, and values at exact boundaries (`2147483647` passes, `2147483648` errors)
- [ ] Existing lexer tests pass
- [ ] `build.sh` passes
- [ ] Compiles without warnings

### US-002: Lexer overflow error messages include the literal text

**Description:** As a JACL developer, I want overflow error messages to show the literal I wrote so I can fix it easily.

**Acceptance Criteria:**
- [ ] Error token for overflow captures the source span (start/end position) so downstream error reporting can show the literal
- [ ] The error message string is descriptive: includes "out of i32 range" and a suggestion to use `[i64 ...]` or `[u64 ...]`
- [ ] Test that the error message contains "i32 range" substring
- [ ] Existing tests pass
- [ ] `build.sh` passes

### US-003: Detect box/cell capture in closures passed to concurrent primitives

**Description:** As a runtime developer, I need the compiler to detect when a closure capturing a `box` or mutable cell is passed as an argument to `spawn`, `parallel`, or `race`, and auto-pin the task to the owning worker thread.

**Acceptance Criteria:**
- [ ] Extend `ast__contains_nonlocal_set_impl` (or add a parallel analysis) to also detect closures that capture variables declared with `mut` or `box` — not just direct `set!` usage
- [ ] When a closure capturing a box/cell is passed to `spawn`/`parallel`/`race`, the compiler marks the generated CPS continuation as pinned (same mechanism as current `set!` pinning)
- [ ] The analysis walks closure argument subtrees: if a `$var` reference in a closure argument resolves to a `mut`/`box` binding from an enclosing scope, that closure is flagged
- [ ] Pinning behavior: pinned tasks go to the owning worker's private deque (existing infrastructure), ensuring box dereferences hit the correct thread-local state
- [ ] Direct `box` usage in concurrent bodies (not via closure argument) continues to work via existing pinning
- [ ] The TODO comment at compiler.c ~line 499 is updated to document what the new analysis covers and what remains out of scope (e.g., box refs stored in collections then retrieved on another thread)
- [ ] Add compiler test: closure capturing a box passed to spawn → verify pinned flag is set on the continuation
- [ ] Add `.jacl` test: closure capturing a box used inside spawn body → verify correct value is read
- [ ] Existing `mut_*.jacl` and `set_concurrent_*.jacl` tests pass
- [ ] `build.sh` passes
- [ ] Compiles without warnings

### US-004: Escape analysis covers transitive capture

**Description:** As a runtime developer, I need the escape analysis to handle transitive capture — a closure A captures a box, closure B captures A, and B is passed to spawn.

**Acceptance Criteria:**
- [ ] If a closure captures another closure that itself captures a mutable cell, and the outer closure is passed to a concurrent primitive, the task is pinned
- [ ] Analysis is conservative: if unsure whether a captured variable transitively holds a mutable ref, pin (safe default)
- [ ] Add test: transitive capture scenario with a box → verify pinned
- [ ] Add `.jacl` test: nested closure with box used in spawn → correct result
- [ ] Existing tests pass
- [ ] `build.sh` passes

### US-005: Add `gen` field to GCHeader

**Description:** As a GC developer, I need a generation bit in the object header to distinguish young from old objects.

**Acceptance Criteria:**
- [ ] Reclaim 1 bit from `alloc_total` for a `gen` field (e.g., reduce `alloc_total` to 15 bits via bitfield, or use a bit from `mark` since mark only needs 1 bit). The header must remain 8 bytes total.
- [ ] `gen` is initialized to 0 (young) in `gc_alloc`
- [ ] `gc_header_of` and all existing header access patterns continue to work
- [ ] Document the bit layout in a comment on the struct
- [ ] All existing GC tests pass — no behavioral change yet
- [ ] `build.sh` passes
- [ ] Compiles without warnings

### US-006: Implement promotion logic in sweep

**Description:** As a GC developer, I need the sweep phase to promote objects that survive 2 minor collections.

**Acceptance Criteria:**
- [ ] Add a `survive_count` mechanism: either a separate counter (if bits available) or use "survived 2 consecutive cycles" heuristic by checking mark status across cycles
- [ ] Simplest approach: an object is promoted (`gen = 1`) if it is live (marked or epoch-protected) after 2 consecutive GC cycles. Track this via the mark bit: if an object was marked in the previous cycle AND is marked again in the current cycle, promote it
- [ ] Promotion is in-place: only the `gen` metadata changes. No object movement, no pointer updates, no copying
- [ ] Add C-level test: allocate objects, run 2 GC cycles, verify promoted objects have `gen == 1`
- [ ] Add C-level test: allocate short-lived objects, run 1 GC cycle, verify they remain `gen == 0`
- [ ] Existing GC tests pass
- [ ] `build.sh` passes

### US-007: Implement per-thread remembered set

**Description:** As a GC developer, I need a per-thread remembered set that tracks old→young pointers created by box/atom mutation.

**Acceptance Criteria:**
- [ ] Add a `RememberedSet` structure to `WorkerThread`: append-only buffer of `JaclVal` container references, similar to `GreyBuffer`
- [ ] Extend the hybrid write barrier in `gc_write_barrier` to record a remembered set entry when an old-gen container (`gen == 1`) stores a young-gen value (`gen == 0`)
- [ ] The generational check runs unconditionally (not gated on `gc_active`), because the remembered set must be maintained between GC cycles
- [ ] Remembered set is per-thread, no contention
- [ ] Add `remembered_set_init`, `remembered_set_push`, `remembered_set_drain`, `remembered_set_destroy` functions
- [ ] Drain deduplicates entries (a container mutated multiple times between GCs only appears once)
- [ ] Add C-level test: mutate an old-gen atom to point to a young-gen value → verify remembered set contains the atom
- [ ] Add C-level test: mutate a young-gen atom → verify remembered set is NOT populated
- [ ] Existing tests pass
- [ ] `build.sh` passes

### US-008: Implement minor GC mode

**Description:** As a GC developer, I need a minor GC mode that only traces and sweeps young-generation objects, using the remembered set for old→young references.

**Acceptance Criteria:**
- [ ] Add a `gc_collect_minor` function (or a mode flag on the existing collection)
- [ ] Minor GC mark phase: trace from all roots (same as full GC), but STOP tracing at old-gen objects (don't trace their children) — except for objects in the remembered set
- [ ] Minor GC also traces from remembered set entries: for each old-gen container in the set, trace its current value (which may be young)
- [ ] Minor GC sweep: only sweep young objects (`gen == 0`). Old-gen objects are untouched
- [ ] After sweep, promote surviving young objects that meet the promotion threshold (US-006)
- [ ] Clear the remembered set after minor GC completes
- [ ] Major GC (`gc_collect`) continues to work as before — traces everything, sweeps everything
- [ ] Add C-level test: allocate young and old objects, run minor GC, verify only dead young objects are reclaimed and old objects are untouched
- [ ] Add C-level test: old atom pointing to young value, run minor GC, verify young value survives (reachable via remembered set)
- [ ] Existing GC tests pass
- [ ] `build.sh` passes

### US-009: GC scheduling heuristics for minor vs major

**Description:** As a GC developer, I need the GC scheduler to choose between minor and major collection based on allocation patterns.

**Acceptance Criteria:**
- [ ] Default trigger: young allocation threshold exceeded (configurable, default ~1MB of new allocations) → minor GC
- [ ] Major GC trigger: old generation has grown by >50% since last major GC
- [ ] Emergency fallback: OOM during allocation → synchronous full (major) GC, same as existing 3-tier escalation
- [ ] The `bytes_since_gc` counter on `WorkerThread` drives minor GC scheduling (existing infrastructure)
- [ ] Add a `old_gen_bytes` counter that tracks total old-gen size, updated during promotion and major sweep
- [ ] First GC cycle is always a major GC (no old gen exists yet)
- [ ] Add C-level test: verify minor GC triggers after allocation threshold
- [ ] Add C-level test: verify major GC triggers after old gen growth threshold
- [ ] Existing tests pass
- [ ] `build.sh` passes

### US-010: E2E stress test — GC under spawn/await pressure

**Description:** As a runtime developer, I need a stress test that spawns many tasks producing garbage while GC runs concurrently.

**Acceptance Criteria:**
- [ ] New `test/jacl/gc_stress_spawn.jacl` with `# mode: concurrent` header
- [ ] Spawns 100+ tasks, each allocating vectors/maps/strings that become garbage
- [ ] Uses `await` to collect results, verifying correctness
- [ ] Forces at least 2-3 GC cycles during execution (allocate enough to exceed threshold)
- [ ] Correct results despite concurrent GC — verified by `# expect:` headers
- [ ] Test runs in under 10 seconds
- [ ] `build.sh` passes

### US-011: E2E stress test — parallel with large persistent data structures

**Description:** As a runtime developer, I need a test that shares large persistent vectors/maps across parallel tasks while GC runs.

**Acceptance Criteria:**
- [ ] New `test/jacl/gc_stress_persistent.jacl` with `# mode: concurrent` header
- [ ] Creates a large vector (1000+ elements) and a large map (100+ entries)
- [ ] Passes these to `parallel` branches that read and derive new collections from them
- [ ] GC runs during the parallel execution (allocation pressure from derived collections)
- [ ] Verifies all branches see correct, uncorrupted data
- [ ] Tests that the original collections are unchanged (persistence)
- [ ] `build.sh` passes

### US-012: E2E stress test — atom contention under GC

**Description:** As a runtime developer, I need a test that hammers atoms with concurrent CAS operations while GC is active.

**Acceptance Criteria:**
- [ ] New `test/jacl/gc_stress_atom.jacl` with `# mode: concurrent` header
- [ ] Creates an atom holding a counter (or vector)
- [ ] Spawns 50+ tasks that each `swap!` the atom (increment counter or append to vector)
- [ ] GC runs during the swap operations
- [ ] Final atom value is correct (all swaps applied, no lost updates)
- [ ] Write barrier fires correctly — no use-after-free (test doesn't crash)
- [ ] `build.sh` passes

### US-013: E2E stress test — deep CPS recursion with concurrent suspension

**Description:** As a runtime developer, I need a test that exercises deep recursive CPS chains with interleaved spawn/await to stress continuation management under GC.

**Acceptance Criteria:**
- [ ] New `test/jacl/gc_stress_deep_cps.jacl` with `# mode: concurrent` header
- [ ] Recursive proc that spawns a subtask at each level, awaits it, and recurses (at least 20 levels deep)
- [ ] Each level allocates some garbage (strings, vectors) to trigger GC
- [ ] Final result is correct (sum or product of recursive results)
- [ ] Tests that CPS continuations are properly rooted through all GC cycles
- [ ] `build.sh` passes

### US-014: E2E stress test — race with GC and write barriers

**Description:** As a runtime developer, I need a test that uses `race` with multiple branches producing garbage, verifying the winner's result survives GC and losers' garbage is eventually collected.

**Acceptance Criteria:**
- [ ] New `test/jacl/gc_stress_race.jacl` with `# mode: concurrent` header
- [ ] Races 5+ branches, each allocating different amounts of garbage
- [ ] Winner's result is correct
- [ ] Test runs multiple races in sequence to exercise GC across multiple race completions
- [ ] `build.sh` passes

## Functional Requirements

- FR-1: The lexer must reject integer literals with values outside `[-2^31, 2^31 - 1]` with a descriptive error
- FR-2: The compiler must detect closures capturing mutable cells (box, mut) that escape to concurrent primitives, and auto-pin the resulting task
- FR-3: Escape analysis must handle transitive capture (closure capturing a closure that captures a box)
- FR-4: GCHeader must include a generation bit without increasing header size beyond 8 bytes
- FR-5: Objects surviving 2 minor GC cycles must be promoted to old generation in-place (no movement)
- FR-6: A per-thread remembered set must track old→young pointers from box/atom mutations
- FR-7: Minor GC must trace only young objects + remembered set roots, sweep only young objects
- FR-8: GC scheduler must default to minor GC on allocation threshold, major GC on old-gen growth
- FR-9: The GC is non-moving. No compaction, no semi-spaces, no object relocation, no forwarding pointers. Promotion is purely a metadata flag change.
- FR-10: 5 new `.jacl` stress tests must exercise GC under concurrent load with correct results

## Non-Goals

- No changes to the value representation or tag encoding
- No new syntax — the `[i64 ...]` / `[u64 ...]` constructors already exist
- No compaction or object movement of any kind
- No copying nursery or semi-space collector
- No weak references or finalizers (future work)
- No changes to the module system, macro system, or FFI (M14-M17)
- No benchmarking framework (focus is correctness, not performance measurement)

## Technical Considerations

- **GCHeader bit layout**: Currently `epoch(32) + mark(8) + obj_type(8) + alloc_total(16) = 64 bits`. The `gen` bit can be carved from `mark` (only needs 1 bit; use 1 bit for mark, 1 for gen, 6 unused) or from `alloc_total` (15 bits = max 32KB allocation, well within the 64KB block size)
- **Remembered set sizing**: In mostly-functional JACL code, box/atom mutations are rare. The remembered set will typically be empty or near-empty. A simple dynamic array (like GreyBuffer) is sufficient
- **Promotion threshold of 2**: Conservative enough to avoid premature promotion of burst allocations, but not so high that the old gen never fills. Tunable later
- **Escape analysis scope**: The analysis is syntactic and conservative. A box stored in a collection and retrieved on another thread is NOT caught — that requires runtime checks or a more sophisticated type-based analysis (documented as out of scope)
- **Test timing**: All `.jacl` stress tests must complete in under 10 seconds to avoid CI flakiness. Use allocation count rather than wall-clock time to trigger GC pressure

## Success Metrics

- `build.sh` passes with all new tests (0 failures)
- Lexer correctly rejects all overlarge literals with descriptive errors
- No mutable cell can silently escape to another thread via closure passing
- Minor GC skips old-gen objects (verified by C-level test counting traced objects)
- Stress tests run 100+ concurrent tasks with GC pressure without crashes or data corruption

## Open Questions

- Should the promotion threshold (2 survivals) be a compile-time constant or a runtime-tunable parameter? (Start with compile-time constant, tune later.)
- Should the remembered set use a hash set for O(1) dedup, or is linear scan sufficient given expected small size? (Start with linear scan.)
- What's the right allocation threshold for minor GC trigger? (Start with 1MB, tune based on stress test behavior.)
