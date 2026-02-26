# PRD: M12 — Epoch-Based Tracing GC

## Introduction

Replace JACL's current memory management (arena allocation for runtime objects, reference counting for persistent collections) with a concurrent epoch-based tracing garbage collector. This is the foundational half of the GC & concurrency co-design: it introduces per-thread heaps, a worker thread pool with work-stealing, concurrent mark-sweep collection, and migrates all heap types to GC management — eliminating reference counting entirely. The GC runs concurrently with mutator threads (non-stop-the-world) using epoch-based watermarking, hybrid write barriers, and Immix-style line-granularity sweep.

This PRD covers the GC infrastructure and minimal threading runtime. User-facing concurrency primitives (`spawn`, `await`, `parallel`, `race`) and the full CPS compiler transform are deferred to a follow-up M13 PRD. Generational collection (sticky mark-bit) is also deferred — the base non-generational concurrent GC ships first.

## Goals

- Replace arena allocation for runtime heap objects with a per-thread block-based GC heap
- Replace reference counting on HAMT/RRB collection nodes with GC-managed tracing
- Implement concurrent (non-stop-the-world) mark-sweep collection with epoch-based watermarking
- Introduce a fixed-size worker thread pool with Chase-Lev work-stealing deques
- All existing M0–M11 tests pass with GC instead of arena/RC, with no memory leaks under stress
- Provide the threading runtime foundation that the M13 concurrency primitives will build on

## User Stories

### US-001: GCHeader and object type system

**Description:** As a developer, I need the GC object header and type enumeration so that the collector can identify, trace, and manage every heap-allocated object uniformly.

**Acceptance Criteria:**
- [ ] Define `GCHeader` struct (8 bytes): `{ uint32_t epoch; uint8_t mark; uint8_t obj_type; uint8_t gen; uint8_t _pad; }`
- [ ] Define `GCObjType` enum with entries: `OBJ_CLOSURE`, `OBJ_STRING`, `OBJ_HEAP_I64`, `OBJ_HEAP_U64`, `OBJ_HEAP_F64`, `OBJ_MUTABLE_REF`, `OBJ_BIGNUM`, `OBJ_HAMT_INTERNAL`, `OBJ_HAMT_LEAF`, `OBJ_HAMT_COLLISION`, `OBJ_RRB_INTERNAL`, `OBJ_RRB_LEAF`, `OBJ_RRB_ROOT`
- [ ] Helper: `gc_header_of(void* payload)` — returns pointer to the `GCHeader` prepended before the payload
- [ ] Helper: `jacl_is_heap_type(JaclVal v)` — returns true if the JaclVal tag indicates a heap-allocated (GC-managed) pointer type (string, vector, map, closure, bignum, cell, box, atom, i64, u64, f64)
- [ ] All leaf types (`OBJ_NUMERIC`, `OBJ_STRING`, `OBJ_BIGNUM`, `OBJ_HEAP_I64/U64/F64`) have no outgoing references — GC skips tracing them
- [ ] Build passes

### US-002: Block-based heap allocator

**Description:** As a developer, I need a per-thread block-based heap allocator with Immix-style line granularity so that each worker thread can bump-allocate without contention and the GC can reclaim memory at line granularity.

**Acceptance Criteria:**
- [ ] Define `GCBlock` — 64KB payload divided into 512 lines of 128 bytes each
- [ ] Define per-block line map — 512 bytes (one byte per line), stored out-of-band from block payload. Values: `LINE_FREE` and `LINE_OCCUPIED`
- [ ] Define `ThreadHeap` struct: linked list of blocks, pointer to current block, bump cursor within current free-line run
- [ ] Define global `BlockPool` — thread-safe pool of free blocks (mutex-protected or lock-free). New blocks allocated from OS when pool is empty
- [ ] Implement `gc_alloc(ThreadHeap* heap, uint8_t obj_type, size_t payload_size)`:
  - Bumps cursor within current free-line run
  - When current run is exhausted, scans line map for next free run in current block
  - When no free runs remain, tries next block in heap's list
  - When no blocks have free runs, acquires a new block from the global pool
  - Stamps `GCHeader` with: `epoch = current_epoch`, `mark = 0`, `obj_type = obj_type`, `gen = 0`
  - Returns pointer to payload (past the header)
- [ ] Objects spanning multiple lines: all lines touched by the object are tracked (anchor line = line containing header)
- [ ] `gc_alloc` is NOT thread-safe — only the owning thread calls it (no contention by design)
- [ ] Unit tests: allocate objects of various sizes, verify header fields, verify line map updates, verify block exhaustion triggers new block acquisition
- [ ] Build passes

### US-003: Migrate runtime heap types to GC heap

**Description:** As a developer, I need all runtime heap-allocated types (closures, strings, numerics, mutable refs) to be allocated via `gc_alloc` with prepended `GCHeader` so the GC can manage their lifecycle.

**Acceptance Criteria:**
- [ ] `JaclClosure` allocated via `gc_alloc(heap, OBJ_CLOSURE, sizeof(JaclClosure))` instead of `arena_alloc(vm->arena, ...)`
  - Upvalue arrays allocated via `gc_alloc` (with a suitable obj_type or inline in closure payload)
  - `OP_CLOSURE` handler updated to use GC heap
- [ ] `JaclHeapString` allocated via `gc_alloc(heap, OBJ_STRING, sizeof(JaclHeapString) + length)` instead of `arena_alloc`
  - String intern table updated: intern table itself remains arena-backed, but interned strings live on GC heap (Phase 1: immortal — GC never collects interned strings)
- [ ] `JaclHeapI64`, `JaclHeapU64`, `JaclHeapF64` allocated via `gc_alloc(heap, OBJ_HEAP_I64/U64/F64, sizeof(...))`
- [ ] `JaclMutableRef` (cell, box, atom) allocated via `gc_alloc(heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef))`
- [ ] BigNum heap data allocated via `gc_alloc(heap, OBJ_BIGNUM, ...)`
- [ ] Arena still used for compile-time objects: AST nodes, bytecode chunks, constant pools, compiler temporaries, param_names arrays
- [ ] VM struct gains a `ThreadHeap*` field (replaces arena for runtime allocations)
- [ ] All constructors (`jacl_closure()`, `jacl_heap_string()`, `jacl_i64()`, etc.) updated to take `ThreadHeap*` instead of `arena_t*`
- [ ] All existing M0–M11 tests compile and pass (GC not yet active — objects allocated but never collected)
- [ ] Build passes

### US-004: Migrate HAMT/RRB from RC to GC heap

**Description:** As a developer, I need all HAMT and RRB persistent collection nodes allocated via `gc_alloc` with `GCHeader` instead of `rc_alloc` with `RCHeader`, so the GC manages their lifecycle and cascading refcount decrements are eliminated.

**Acceptance Criteria:**
- [ ] HAMT nodes (`jacl_map_internal`, `jacl_map_leaf`, `jacl_map_collision`) allocated via `gc_alloc` with appropriate `OBJ_HAMT_*` type
  - `GCHeader` replaces `RCHeader` — same position (prepended to payload), 8 bytes instead of 16
  - All `jacl_map_rc_ref()` calls removed from COW operations
  - All `jacl_map_rc_unref()` calls removed — old nodes become garbage when unreachable
  - Destructors removed from all HAMT node types
- [ ] RRB nodes (`jacl_vec_internal`, `jacl_vec_leaf`, `jacl_vec_root`) allocated via `gc_alloc` with appropriate `OBJ_RRB_*` type
  - Same pattern: `GCHeader` replaces `RCHeader`, all `jacl_vec_rc_ref/unref` calls removed
  - Size tables for relaxed radix nodes allocated via `gc_alloc` or inline
- [ ] The `HAMT_ALLOCATOR` and `RRB_VEC_ALLOCATOR` macros updated to route through `gc_alloc` instead of `libc_allocator`
  - This requires threading a `ThreadHeap*` through all collection operations — may need a thread-local or context parameter
- [ ] COW operations (assoc, dissoc, conj, update, etc.) simplified: just create new nodes, no ref management
- [ ] The `rc.h` library is no longer included by `collections.c`
- [ ] All existing collection tests (M8 tests, `test_hamt*.c`, `test_rrb_vec*.c`) pass
- [ ] Build passes

### US-005: Single-threaded mark-sweep GC

**Description:** As a developer, I need a working single-threaded mark-sweep GC so that I can validate the GCHeader/tracing/sweep logic in isolation before adding concurrency. This is a stepping stone — the concurrent GC (US-010) replaces it.

**Acceptance Criteria:**
- [ ] Root enumeration (single-threaded): VM stack (`stack[0..stack_top]`), call frame closures, global environment values
- [ ] Mark phase using explicit mark stack (not recursion):
  - Push all root JaclVals onto mark stack
  - Pop value, skip if inline type or already marked
  - Set `header->mark = current_mark`
  - Based on `obj_type`, push children onto mark stack (using trace strategies from GC_CONCURRENCY_DESIGN.md Section 5 table)
  - Mark bit alternation: `current_mark` toggles 0/1 each GC cycle (avoids clearing all marks)
- [ ] Sweep phase with line granularity:
  - For each block in the heap, clear all line marks to `LINE_FREE`
  - For each object in block: if `header->mark == current_mark`, mark all lines it touches as `LINE_OCCUPIED`
  - Blocks with all lines free returned to global block pool
  - Blocks with some free lines kept in heap for future allocation into free runs
- [ ] GC trigger: allocation byte threshold (configurable, default 1MB). When exceeded, run GC synchronously
- [ ] All existing M0–M11 tests pass with GC active (objects are allocated AND collected)
- [ ] No use-after-free or double-free under any existing test
- [ ] Build passes

### US-006: Worker thread pool and task execution loop

**Description:** As a developer, I need a fixed-size worker thread pool with Chase-Lev work-stealing deques so that multiple threads can execute tasks concurrently, forming the runtime foundation for concurrent GC and (later) user-facing concurrency primitives.

**Acceptance Criteria:**
- [ ] Define `WorkerThread` struct:
  - `chase_lev_deque public_deque` — for tasks touching only immutables (stealable)
  - `chase_lev_deque private_deque` — for tasks touching thread-local mutables (not stealable)
  - `ThreadHeap heap` — per-thread GC heap (only this thread allocates from it)
  - `GreyBuffer grey_buf` — thread-local append-only buffer for write barrier entries
  - `atomic_ptr currently_executing` — pointer to the task currently being executed (or BUSY sentinel, or NULL)
  - `atomic_uint64 thread_epoch` — set to global epoch before each task
  - `VM vm` — per-thread VM instance (own stack, own frames, shared globals)
- [ ] Define `Runtime` struct (global singleton):
  - `WorkerThread workers[N]` — fixed-size array, N = number of cores (configurable)
  - `atomic_uint64 global_epoch` — incremented at each GC start
  - `atomic_bool gc_running` — mutual exclusion for GC (only one GC at a time)
  - `atomic_bool gc_active` — true during mark phase (write barriers check this)
  - `BlockPool block_pool` — global pool of free blocks
  - `Environment globals` — shared global environment (read by all threads)
- [ ] Task execution loop (per worker):
  1. Set `currently_executing = BUSY` (sentinel)
  2. Pop from private deque (priority) or public deque, or steal from another worker's public deque
  3. Set `currently_executing = task`
  4. Set `thread_epoch = atomic_load(global_epoch)`
  5. Execute the task (run the closure on this worker's VM)
  6. Set `currently_executing = NULL`
  7. Repeat
- [ ] `runtime_submit_task(Runtime*, JaclClosure*, bool thread_local)` — push task onto a worker's public or private deque
- [ ] Worker threads are created at `runtime_init()` and joined at `runtime_destroy()`
- [ ] Use existing `chase_lev.h` library for deques and `platform.h` for threads/atomics
- [ ] Unit tests: submit multiple independent closures, verify all execute, verify work-stealing distributes load
- [ ] Build passes

### US-007: Root enumeration protocol

**Description:** As a developer, I need a correct root enumeration protocol that finds all live GC roots across all worker threads without stopping the world, handling the pop-in-transit race condition.

**Acceptance Criteria:**
- [ ] GC enumerates roots from all sources:
  1. All tasks on all deques (public + private) — snapshot each deque's `[top, bottom)` range and read all elements
  2. Each worker's `currently_executing` slot — the task currently being executed
  3. Each worker's VM stack (`stack[0..stack_top]`) — values on the stack during execution
  4. Global environment values
- [ ] BUSY sentinel protocol for pop-in-transit race:
  - Worker sets `currently_executing = BUSY` BEFORE popping from deque
  - GC, upon seeing `BUSY`, spins briefly (busy-wait, nanoseconds) until value resolves to a real task or NULL
  - This closes the window where a task is popped but not yet published
- [ ] Deque snapshot is conservative:
  - Elements stolen between reading indices and reading data → visible on another thread's deque or `currently_executing`
  - Elements added after reading `bottom` → new, epoch-protected
  - May include already-completed tasks → harmless (traces dead objects, they survive one extra cycle)
- [ ] Unit tests: simulate pop-in-transit race with BUSY sentinel, verify GC sees all tasks
- [ ] Build passes

### US-008: Write barriers (hybrid SATB + insertion)

**Description:** As a developer, I need hybrid write barriers on mutable container mutations so that the concurrent GC correctly handles both old values evicted from containers and new values stored into containers during an active GC cycle.

**Acceptance Criteria:**
- [ ] Define `GreyBuffer` — per-thread append-only buffer of `JaclVal` entries
  - Thread-local writes only (no contention)
  - GC reads entries up to a snapshot of the count; new entries after snapshot caught by final drain
- [ ] Implement `gc_write_barrier(WorkerThread* t, JaclVal old_val, JaclVal new_val)`:
  - Fast path: if `!atomic_load_relaxed(&gc_active)`, return immediately (single relaxed atomic load)
  - SATB deletion barrier: if `old_val` is a heap type, push onto `t->grey_buf`
  - Insertion barrier: if `new_val` is a heap type, push onto `t->grey_buf`
- [ ] Write barrier fires on:
  - `OP_RESET` (reset! on box/atom) — old value is the current container value, new value is the replacement
  - `OP_SWAP` (swap! on box/atom) — old value is the pre-swap value, new value is the post-swap value
  - `OP_SET_CELL_LOCAL` and `OP_SET_CELL_UPVALUE` (set! on mut locals/upvalues) — old value is the cell's current value, new value is the replacement
  - Successful CAS on atoms (when CAS semantics are added in M13)
- [ ] Write barrier does NOT fire on:
  - Failed CAS attempts (container unchanged)
  - Reads (`deref`, `OP_GET_CELL_LOCAL`, etc.)
  - Immutable value operations
- [ ] Unit tests: verify grey buffer receives entries during active GC, verify fast path skips when GC not active
- [ ] Build passes

### US-009: Concurrent mark-sweep GC

**Description:** As a developer, I need the full concurrent mark-sweep GC that runs as a task on a worker thread while other threads continue executing, using epoch-based watermarking to protect recently-allocated objects.

**Acceptance Criteria:**
- [ ] GC is itself a task submitted to a worker's public deque — any worker can execute it
- [ ] GC trigger: when `thread->bytes_since_gc > GC_THRESHOLD`, atomically CAS `gc_running` from false to true. If CAS succeeds, enqueue GC task. Reset byte counter regardless
- [ ] GC algorithm:
  1. Increment `global_epoch`
  2. Set `gc_active = true`
  3. Compute `watermark = min(all thread_epochs)`
  4. Root enumeration (US-007 protocol)
  5. **Mark phase**: trace from roots using mark stack
     - Pop value from mark stack
     - Skip inline types and already-marked objects
     - Set `header->mark = current_mark`
     - Push children based on `obj_type` (trace strategies per US-001)
     - Periodically drain all threads' grey buffers → push entries onto mark stack
  6. Final grey buffer drain → process any remaining entries
  7. Set `gc_active = false`
  8. **Sweep phase**: for each worker's heap, sweep completed blocks with line granularity
     - `marked` → live
     - Not marked, `epoch >= watermark` → live (too new to judge)
     - Not marked, `epoch < watermark` → reclaim (update line map to FREE)
  9. Block recycling: all-free blocks returned to global pool, partially-free blocks stay in owning thread's heap
  10. Toggle `current_mark` (0↔1)
  11. Set `gc_running = false`
- [ ] Sweep only touches completed blocks — never the active allocation block of any thread
- [ ] Mark stack overflow: fixed-size stack (4096 entries) with dynamically-growing overflow list for pathological cases
- [ ] Mutual exclusion: only one GC at a time via `gc_running` atomic flag
- [ ] All existing M0–M11 tests pass under concurrent GC
- [ ] Stress test: multiple workers allocating and discarding objects simultaneously, GC runs repeatedly, no use-after-free or memory leak
- [ ] Build passes

### US-010: Atom CAS semantics

**Description:** As a developer, I need `swap!` on atoms to use a proper CAS loop so that concurrent modifications to atoms are safe, replacing the single-threaded read-modify-write from M10.

**Acceptance Criteria:**
- [ ] `OP_SWAP` on atoms uses CAS loop: read current value, call closure, CAS result back. If CAS fails (another thread modified the atom), retry
- [ ] Write barrier fires on successful CAS only (not failed attempts)
- [ ] `OP_RESET` on atoms uses atomic store (not CAS — unconditional replace)
- [ ] `OP_DEREF` on atoms uses atomic load
- [ ] Box operations remain non-atomic (box is thread-local, private deque only)
- [ ] Unit tests: concurrent `swap!` from multiple threads on shared atom, verify final value is consistent
- [ ] Build passes

### US-011: Integration tests and stress testing

**Description:** As a developer, I need comprehensive integration and stress tests validating the entire GC system under concurrent load.

**Acceptance Criteria:**
- [ ] GC correctness tests in `test/test_m12.c`:
  - Allocate objects across multiple types, trigger GC, verify live objects survive and dead objects are collected
  - Verify tracing through closures (upvalue references kept alive)
  - Verify tracing through HAMT nodes (map values kept alive)
  - Verify tracing through RRB nodes (vector elements kept alive)
  - Verify write barrier correctness: old value survives after being swapped out of an atom during GC
  - Verify write barrier correctness: new value stored into atom after GC scans it survives
  - Verify epoch watermark: recently-allocated objects survive even if unreachable
  - Verify line-granularity reclamation: single live object in a block doesn't prevent reclaiming other lines
  - Verify block recycling: fully-dead blocks returned to global pool
- [ ] Multi-threaded stress tests:
  - N workers allocating short-lived closures in a loop, GC running concurrently — no crashes, no leaks
  - N workers performing COW map operations (assoc/dissoc) on shared immutable maps — no crashes, correct values
  - N workers performing `swap!` on shared atoms — final values consistent
  - Allocation-heavy workload that triggers many GC cycles — memory usage bounded (doesn't grow without bound)
- [ ] Regression: all existing M0–M11 `.jacl` harness tests pass
- [ ] Regression: all existing M0–M11 C unit tests pass
- [ ] No memory leaks (verified by running tests under sanitizer or manual heap inspection)
- [ ] Build passes

## Functional Requirements

- FR-1: Every GC-managed heap object is prepended with an 8-byte `GCHeader` containing epoch, mark bit, object type, and generation
- FR-2: Each worker thread owns a private heap composed of 64KB blocks, each divided into 512 lines of 128 bytes. Allocation bumps through contiguous free lines with no locking
- FR-3: All runtime heap types (closures, strings, heap numerics, mutable refs, bignums, HAMT nodes, RRB nodes) are allocated via `gc_alloc()` and managed by the tracing GC
- FR-4: Compile-time objects (AST nodes, bytecode chunks, constant pools) remain on the existing arena allocator
- FR-5: The GC runs as a concurrent task on a worker thread. It does not stop the world. Mutator threads continue executing during mark and sweep phases
- FR-6: The GC uses epoch-based watermarking: objects allocated at or after the watermark (= min of all thread epochs at GC start) are immune to collection regardless of reachability
- FR-7: Root enumeration covers: all tasks on all deques, each worker's `currently_executing` slot, each worker's VM stack, and global variables
- FR-8: The BUSY sentinel protocol prevents the pop-in-transit race: workers publish `BUSY` before popping, GC spins on `BUSY` until resolved
- FR-9: Mutable container mutations (reset!, swap!, set! on cells) fire a hybrid write barrier during active GC: both the old value (SATB/deletion) and the new value (insertion) are pushed to the worker's thread-local grey buffer
- FR-10: The mark phase uses an explicit mark stack with mark-bit alternation (0/1 per cycle). Grey buffers are drained periodically during marking and once at the end
- FR-11: The sweep phase operates at line granularity: lines containing no live objects are marked free; lines with any live object are marked occupied. Blocks with all lines free are returned to the global pool
- FR-12: `swap!` on atoms uses a CAS loop for thread-safe concurrent modifications. `reset!` uses atomic store. `deref` uses atomic load
- FR-13: Reference counting (`RCHeader`, `rc_ref`, `rc_unref`) is completely removed from HAMT and RRB collection code. No destructors, no cascading decrements
- FR-14: String interning remains immortal (Phase 1) — interned strings are never collected by GC
- FR-15: A fixed-size worker thread pool (one worker per core) executes tasks from Chase-Lev work-stealing deques. Each worker has its own VM, heap, and grey buffer

## Non-Goals (Out of Scope)

- No user-facing concurrency primitives (`spawn`, `await`, `parallel`, `race`) — deferred to M13
- No CPS compiler transform — deferred to M13 (when suspension points are introduced)
- No generational collection (sticky mark-bit, remembered set, minor/major GC scheduling) — deferred to a future optimization PRD
- No weak-reference string intern table (Phase 2 of string interning) — deferred
- No finalization or destructor callbacks on `GCHeader` — JACL uses explicit `close` discipline for resource cleanup
- No object compaction or movement — objects are never relocated after allocation
- No channel-based concurrency or message passing
- No JACL-level thread API or thread-local storage syntax

## Technical Considerations

- **Unity build model**: All source files are `#include`d through `jacl.c`. New GC source (`gc.c`) and thread pool source (`runtime.c`) must be integrated into this chain
- **Chase-Lev deque**: Already implemented at `lib/chase_lev/chase_lev.h` with stress tests. Use directly for worker deques
- **Platform atomics**: Already implemented at `lib/platform/platform.h` — `ATOMIC_CAS`, `ATOMIC_LOAD`, `ATOMIC_STORE`, `ATOMIC_INC/DEC`, memory ordering fences. Use for `gc_running`, `gc_active`, `global_epoch`, `thread_epoch`, `currently_executing`, atom CAS
- **HAMT/RRB template pattern**: Both use `#define`-based include-as-template. The allocator is currently a function pointer macro (`HAMT_ALLOCATOR`, `RRB_VEC_ALLOCATOR`). Replacing with `gc_alloc` requires threading a `ThreadHeap*` through all operations — likely via a thread-local or a context parameter added to every collection function signature
- **String intern table**: Currently arena-backed with `JaclHeapString*` pointers into the arena. When strings move to GC heap, intern table entries become GC roots (or strings are marked immortal). Phase 1: mark all interned strings as immortal (never sweep them). This is simple and strings are usually long-lived anyway
- **VM per worker**: Each `WorkerThread` owns a `VM` instance. Global environment (`Environment`) is shared (read-mostly, writes are rare and occur during initialization). Global env access needs synchronization if globals can be mutated at runtime (`set!` on global `mut`)
- **`gc_alloc` threading**: In the current single-threaded VM, `vm->arena` is passed everywhere. Replacing with `ThreadHeap*` requires touching all allocation callsites. Consider a thread-local `current_heap` for convenience, or pass explicitly through the VM struct
- **Block pool sizing**: Start with a reasonable initial allocation (e.g., 4 blocks per worker = 256KB per worker). Grow on demand from the OS. Consider `mmap` for large block allocations on macOS/Linux
- **Mark stack sizing**: 4096 entries handles all realistic workloads (HAMT depth ~7, RRB depth ~7, closure nesting bounded by `VM_FRAMES_MAX` = 64). Overflow list for pathological cases (deeply nested user-constructed linked lists)
- **GC threshold tuning**: Start with 1MB of new allocations since last GC. Adaptive: increase threshold when survival rate is high (most objects live), decrease when low (lots of garbage)
- **Testing under concurrency**: Use thread sanitizer (`-fsanitize=thread`) during development to catch data races. Use address sanitizer (`-fsanitize=address`) to catch use-after-free

## Success Metrics

- All existing M0–M11 tests pass with GC-managed memory instead of arena/RC
- No memory leaks: under sustained allocation, memory usage stabilizes (doesn't grow without bound)
- No use-after-free: verified under address sanitizer for all test suites
- No data races: verified under thread sanitizer for multi-threaded tests
- Multi-threaded allocation stress test: N workers allocating/discarding concurrently with GC, no crashes over 10+ GC cycles
- Latency: no GC pauses observable at the test level (non-stop-the-world verified by concurrent execution during GC)

## Open Questions

- Should the GC thread be a dedicated thread or a regular worker that picks up the GC task? The design doc says "any worker," which is simpler and avoids dedicating a core to GC. But a dedicated GC thread avoids latency impact on worker tasks during collection. Start with "any worker" per the design doc.
- How should global `mut` variables be synchronized? Currently `Environment` is a flat linear-scan array. With multiple workers, `set!` on a global `mut` needs synchronization. Options: mutex per global, copy-on-write environment, or restrict global mutation to initialization phase. Defer decision until M13 concurrency design.
- Should the GC scan each worker's call frame closures as roots in addition to the VM stack? Call frame closures point to the currently-executing closure at each frame depth. They're already reachable via the stack, but scanning them explicitly is more robust. Start with scanning both VM stack and call frame closures.
- What happens when a worker thread is idle (no tasks on any deque)? It should spin-wait or park. Spinning wastes CPU; parking adds wake-up latency. Start with a simple spin-wait with backoff, optimize later if CPU usage is a concern.
