# PRD: String System GC Unification & Thread Safety

## Introduction

The JACL three-tier string system has a split allocation model: inline and interned strings are GC-managed, but rope string internals use a separate reference-counting (RC) allocator. This creates a dual-heap model that is inconsistent with how vec (RRB) and map (HAMT) collections work — those already allocate all internal tree nodes via the GC. Additionally, the intern table lacks thread safety and never evicts dead entries.

This PRD unifies rope allocation under the GC, adds thread-safe interning with eviction, and closes test coverage gaps — bringing the string system to the same level of maturity as the collection types.

## Goals

- Eliminate the RC allocator from rope/sum_tree, replacing all node allocation with `gc_alloc` via the same `GC_MODE` pattern used by RRB and HAMT in `collections.c`
- Make the intern table safe for concurrent access from multiple threads
- Allow the GC to reclaim dead interned strings, preventing unbounded memory growth in long-running programs
- Add `platform_rwlock_t` abstraction to `lib/platform/platform.h`
- Validate all changes with targeted tests: GC tracing of rope trees, intern eviction, concurrent interning, rope slice grapheme safety, and cross-thread string passing

## User Stories

### US-001: Add platform_rwlock_t to platform.h
**Description:** As a runtime developer, I need a cross-platform read-write lock primitive so that the intern table can allow concurrent readers with exclusive writers.

**Acceptance Criteria:**
- [ ] `platform_rwlock_t` type defined with three backends: POSIX (`pthread_rwlock_t`), Windows (`SRWLOCK`), Emscripten (no-op stub)
- [ ] Macros provided: `RWLOCK_INIT(rw)`, `RWLOCK_DESTROY(rw)`, `RWLOCK_RDLOCK(rw)`, `RWLOCK_RDUNLOCK(rw)`, `RWLOCK_WRLOCK(rw)`, `RWLOCK_WRUNLOCK(rw)`
- [ ] Follows the same conditional-compilation pattern as existing `platform_mutex_t`
- [ ] Emscripten stubs compile to no-ops (single-threaded environment)
- [ ] Compiles cleanly on all three platforms (POSIX, Windows, Emscripten)
- [ ] Unit test creates rwlock, acquires read lock from two threads simultaneously, acquires write lock exclusively

### US-002: Add GC_MODE to sum_tree template
**Description:** As a runtime developer, I want the sum_tree template to support GC allocation so that rope nodes are managed by the garbage collector instead of reference counting.

**Acceptance Criteria:**
- [ ] New compile-time flag `STREE_GC_MODE` (empty define, acts as feature flag)
- [ ] When `STREE_GC_MODE` is defined, three additional defines are required: `STREE_GC_ALLOC(obj_type, size)`, `STREE_GC_OBJ_INTERNAL`, `STREE_GC_OBJ_LEAF`
- [ ] When `STREE_GC_MODE` is defined, `ST_MK_LEAF` calls `STREE_GC_ALLOC(STREE_GC_OBJ_LEAF, sizeof(ST_LEAF))` instead of `ST_RC_ALLOC`
- [ ] When `STREE_GC_MODE` is defined, `ST_MK_INTERNAL` calls `STREE_GC_ALLOC(STREE_GC_OBJ_INTERNAL, sizeof(ST_INTERNAL))` instead of `ST_RC_ALLOC`
- [ ] When `STREE_GC_MODE` is defined, all `ST_RC_REF` calls compile to no-ops
- [ ] When `STREE_GC_MODE` is defined, all `ST_RC_UNREF` calls compile to no-ops
- [ ] When `STREE_GC_MODE` is defined, `ST_NODE_DESTROY` is not generated (no RC destructor needed)
- [ ] The `#include "../rc/rc.h"` is skipped when `STREE_GC_MODE` is defined
- [ ] `RC_NAME` and `RC_ALLOCATOR` defines are skipped when `STREE_GC_MODE` is defined
- [ ] All existing sum_tree operations (mk_leaf, mk_internal, split, concat, set, from_array) work correctly under GC_MODE
- [ ] Compiles without warnings in GC_MODE

### US-003: Remove RC from sum_tree and rope
**Description:** As a runtime developer, I want to remove RC as the allocation strategy for sum_tree since it is only used by JACL and JACL uses GC.

**Acceptance Criteria:**
- [ ] The `#include "../rc/rc.h"` line is removed from `sum_tree.h`
- [ ] All `ST_RC_ALLOC`, `ST_RC_REF`, `ST_RC_UNREF`, `ST_RC_COUNT` macros are removed from `sum_tree.h`
- [ ] The `RC_NAME` and `RC_ALLOCATOR` defines are removed from `sum_tree.h`
- [ ] `ST_NODE_DESTROY` destructor function is removed from `sum_tree.h`
- [ ] `ST_MK_LEAF` uses `STREE_GC_ALLOC(STREE_GC_OBJ_LEAF, sizeof(ST_LEAF))` directly
- [ ] `ST_MK_INTERNAL` uses `STREE_GC_ALLOC(STREE_GC_OBJ_INTERNAL, sizeof(ST_INTERNAL))` directly
- [ ] All ~20 `ST_RC_REF` call sites across `sum_tree.h` are deleted (structural sharing is now implicit via GC reachability)
- [ ] All `ST_RC_UNREF` call sites across `sum_tree.h` are deleted
- [ ] In `rope.h`: `rope_ref()` becomes a no-op (returns `r` unchanged, no RC increment)
- [ ] In `rope.h`: `rope_unref()` becomes a no-op (no cascade-unref)
- [ ] All `rope_st_ref()` / `rope_st_unref()` calls in `rope.h` operations (concat, split, slice, from_str) are removed
- [ ] No reference to `rc.h` remains in `sum_tree.h` or `rope.h`
- [ ] All existing tests in `test_heap_strings.c`, `test_rope_string.c`, `test_concat_tiers.c` still pass

### US-004: Wire rope into GC allocation via collections.c pattern
**Description:** As a runtime developer, I want rope nodes allocated via `gc_alloc` through the same template pattern that RRB and HAMT use in `collections.c`.

**Acceptance Criteria:**
- [ ] `OBJ_ROPE_LEAF` (value 19) and `OBJ_ROPE_INTERNAL` (value 20) added to `GCObjType` enum in `gc.c`
- [ ] In `collections.c` (or `string.c`, wherever the rope template is instantiated), add defines:
  ```c
  #define STREE_GC_ALLOC(t, sz)      gc_alloc(gc__current_heap, (t), (sz))
  #define STREE_GC_OBJ_INTERNAL      OBJ_ROPE_INTERNAL
  #define STREE_GC_OBJ_LEAF          OBJ_ROPE_LEAF
  ```
- [ ] Rope leaf and internal nodes are allocated from GC blocks (verified by checking `gc_header_of()` returns valid header)
- [ ] GC header overhead is 8 bytes per node (down from 16 bytes with RC)
- [ ] No calls to `malloc` / system allocator for rope node creation

### US-005: Add GC tracing for rope nodes
**Description:** As a runtime developer, I need the GC mark phase to trace rope internal nodes so that structurally-shared rope subtrees remain alive when reachable.

**Acceptance Criteria:**
- [ ] `gc__trace_object` in `gc_collect.c` handles `OBJ_ROPE_LEAF`: no child pointers, just `break` (leaves are atomic blobs)
- [ ] `gc__trace_object` handles `OBJ_ROPE_INTERNAL`: iterates `node->children[0..n_children-1]`, pushes each non-NULL child onto mark stack via `gc__ms_push`
- [ ] `gc__trace_object` for `OBJ_ROPE_STRING` now pushes `rs->r.root.node` onto mark stack (previously was a no-op because rope internals were RC-managed)
- [ ] `gc__finalize_dead` no longer calls `rope_unref()` for `OBJ_ROPE_STRING` (remove the finalization case entirely)
- [ ] Structural sharing verified: two ropes created via `rope_concat` that share subtrees — dropping one rope does not cause the shared subtree to be collected (the other rope keeps it alive via tracing)

### US-006: Intern table read-write lock
**Description:** As a runtime developer, I want the intern table to be safe for concurrent access so that multiple threads can intern and look up strings without data races.

**Acceptance Criteria:**
- [ ] `JaclInternTable` struct gains a `platform_rwlock_t lock` field
- [ ] `jacl_intern_table_init()` calls `RWLOCK_INIT` on the lock
- [ ] `jacl_intern_table_destroy()` calls `RWLOCK_DESTROY` on the lock
- [ ] Intern lookup path (`jacl_intern` when string already exists) acquires read lock, releases after probe completes
- [ ] Intern insert path (`jacl_intern` when string is new) escalates to write lock for insertion
- [ ] Intern table resize (when load factor > 0.75) happens under write lock
- [ ] Multiple threads can concurrently look up already-interned strings without blocking each other
- [ ] A writing thread blocks all readers and other writers for the duration of the insert
- [ ] No deadlocks: lock acquisition order is always rwlock only (no nested locks with other mutexes)

### US-007: Intern table weak-reference eviction
**Description:** As a runtime developer, I want the GC to reclaim interned strings that are no longer referenced by live code, preventing unbounded memory growth in long-running programs.

**Acceptance Criteria:**
- [ ] Intern table entries are treated as **weak roots**: they are NOT scanned during the mark phase's strong-root enumeration
- [ ] A new function `gc_sweep_intern_table(JaclInternTable* table, uint8_t current_mark)` is added, called after mark phase completes but before sweep phase
- [ ] `gc_sweep_intern_table` acquires write lock on the intern table
- [ ] For each non-NULL entry: if `gc_header_of(entry)->mark != current_mark`, the entry is dead — set to a tombstone sentinel and decrement `table->count`
- [ ] Tombstone sentinel is distinguishable from NULL (NULL = never occupied, tombstone = was occupied) so linear probing continues past tombstones during lookup
- [ ] Eviction is **lazy**: `gc_sweep_intern_table` only runs when the intern table's load factor (including tombstones) exceeds 0.75
- [ ] When tombstone ratio exceeds 25% of capacity, a compaction pass removes tombstones by rehashing live entries (under write lock)
- [ ] After eviction: interning the same byte sequence again creates a fresh `JaclHeapString` (it is not found in the table)
- [ ] Long-running test: create 10,000 interned strings, drop all references, trigger multiple GC cycles, verify `table->count` approaches 0

### US-008: Test — GC tracing of rope trees with structural sharing
**Description:** As a runtime developer, I want tests that verify rope nodes are correctly traced and collected by the GC, including structurally-shared subtrees.

**Acceptance Criteria:**
- [ ] Test: create a rope string (>128 bytes), store in a local, trigger full GC — rope and all nodes survive (marked via tracing)
- [ ] Test: create a rope string, set the local to nil, trigger full GC — `JaclRopeString`, all internal nodes, and all leaf nodes are collected (verified via heap object count or allocation stats)
- [ ] Test: create rope A and rope B via `jacl_string_concat` such that they share subtrees. Drop rope A. Trigger GC. Rope B and its subtrees (including shared nodes) survive. The nodes exclusive to rope A are collected.
- [ ] Test: create a large rope (>10KB, many leaves), trigger minor GC, verify young-gen rope nodes are promoted after 2 cycles
- [ ] All tests use the existing test harness (no new framework needed)

### US-009: Test — Intern table eviction
**Description:** As a runtime developer, I want tests that verify dead interned strings are evicted from the intern table after GC.

**Acceptance Criteria:**
- [ ] Test: intern 100 strings (8-128 bytes each), verify `table->count == 100`
- [ ] Test: drop all references to interned strings, trigger full GC with load factor > 0.75, verify `table->count` has decreased
- [ ] Test: after eviction, re-intern a previously evicted string — verify it creates a new `JaclHeapString` (different pointer than original)
- [ ] Test: tombstone compaction — intern strings, evict most, verify compaction removes tombstones and rehashes survivors

### US-010: Test — Concurrent intern table access
**Description:** As a runtime developer, I want tests that verify the intern table handles concurrent access correctly under worst-case conditions.

**Acceptance Criteria:**
- [ ] Test: spawn 8 threads, each interning 1000 unique strings — no crashes, no lost entries, final `table->count == 8000`
- [ ] Test: spawn 8 threads, all interning the **same** 100 strings — no crashes, no duplicates, final `table->count == 100`
- [ ] Test: spawn 4 reader threads doing lookups + 4 writer threads doing inserts concurrently — no crashes, all inserted strings are findable after join
- [ ] Test: concurrent interning during GC eviction — spawn interning threads while triggering GC cycles, verify no corruption

### US-011: Test — Rope slice grapheme safety
**Description:** As a runtime developer, I want a test that verifies rope slicing never produces a partial grapheme cluster.

**Acceptance Criteria:**
- [ ] Test: create a rope containing multi-codepoint graphemes: ZWJ emoji sequences (e.g., family emoji 👨‍👩‍👧‍👦), regional indicator pairs (e.g., 🇯🇵), combining mark sequences (e.g., é as e + U+0301)
- [ ] For each byte offset in [0, rope_byte_len]: call `rope_slice` with offset and length 1 grapheme — verify the result is either empty or a complete grapheme (no orphaned combining marks, no split surrogate-like sequences)
- [ ] Test: slice at every grapheme boundary — verify round-trip: concatenating all grapheme slices reproduces the original rope content

### US-012: Test — Cross-thread string passing
**Description:** As a runtime developer, I want tests that verify string values of all three tiers can be safely passed between threads.

**Acceptance Criteria:**
- [ ] Test: thread A creates an inline string, passes `JaclVal` to thread B via shared variable, thread B reads and verifies content — correct
- [ ] Test: thread A creates an interned string, pins it with `jacl_handle_new`, passes to thread B, thread B reads and verifies — correct, then `jacl_handle_free`
- [ ] Test: thread A creates a rope string, pins with handle, passes to thread B, thread B reads and verifies full content via cursor — correct
- [ ] Test: thread A creates a rope, thread B creates a rope, both pass to thread C which concatenates them — result is correct, no crash
- [ ] All cross-thread tests trigger at least one GC cycle between send and receive to verify epoch watermarking protects in-flight values

## Functional Requirements

- FR-1: `platform_rwlock_t` must support three backends (POSIX, Windows, Emscripten) following the same pattern as `platform_mutex_t`
- FR-2: `sum_tree.h` must allocate nodes exclusively via `STREE_GC_ALLOC` — no RC template, no `rc.h` include
- FR-3: `rope_ref()` and `rope_unref()` must be no-ops — rope lifetime is managed entirely by GC reachability
- FR-4: `gc__trace_object` must trace `OBJ_ROPE_INTERNAL` children and `OBJ_ROPE_STRING` root node
- FR-5: `gc__finalize_dead` must NOT call `rope_unref()` for `OBJ_ROPE_STRING`
- FR-6: Intern table lookups must acquire read lock; inserts must acquire write lock
- FR-7: Intern table eviction must only run when load factor (including tombstones) exceeds 0.75
- FR-8: Intern table must use tombstone sentinels (not NULL) for evicted entries so linear probing is not broken
- FR-9: Tombstone compaction must trigger when tombstone ratio exceeds 25% of table capacity
- FR-10: All existing string tests (`test_heap_strings.c`, `test_rope_string.c`, `test_concat_tiers.c`, `test_string_eq_cmp.c`, `test_compiler_vm_integration.c`) must continue to pass after all changes

## Non-Goals

- No changes to inline string representation (stays in NaN-boxed payload)
- No changes to the interned string byte-length threshold (stays at 128 bytes)
- No changes to the rope leaf size (stays at 256 bytes)
- No lock-free or striped-lock intern table — rwlock is sufficient for now; can be revisited if profiling shows contention
- No changes to the GC handle API itself — it already exists and works
- No changes to write barrier semantics — ropes are immutable, so no write barriers are needed for rope operations
- No preservation of the RC allocator in sum_tree — RC is being removed entirely (sum_tree is only used by JACL)

## Technical Considerations

- **Template pattern precedent:** `collections.c` already demonstrates the `GC_MODE` pattern for RRB and HAMT. The rope wiring should follow this exactly, using `gc__current_heap` thread-local for allocation.
- **GCObjType enum:** Currently has 19 values (0-18). `OBJ_ROPE_LEAF` = 19, `OBJ_ROPE_INTERNAL` = 20. The `obj_type` field in `GCHeader` is `uint8_t`, supporting up to 256 types.
- **Header savings:** RC header is 16 bytes (`intptr_t ref_count` + `void (*destructor)(void*)`). GC header is 8 bytes. For a rope with N nodes, this saves 8N bytes.
- **Cascade-unref elimination:** Currently, when a large rope dies, `rope_unref` triggers a depth-first cascade through the entire tree, touching every node. Under GC, dead nodes are simply zeroed during the linear sweep — no tree traversal needed.
- **Cursor safety:** `rope_cursor` holds raw pointers to tree nodes without incrementing any refcount. This is already the contract: the caller must keep the rope alive for the cursor's lifetime. Under GC, this means the `JaclVal` holding the rope must be reachable from a root. This is naturally true since cursors are used within C functions that have the `JaclVal` on the stack or in a local.
- **Intern table tombstone design:** Use a sentinel pointer value (e.g., `(JaclHeapString*)1`) as tombstone. This is never a valid heap pointer due to alignment. Lookup probes past tombstones; insert can reuse tombstone slots.
- **Eviction during concurrent access:** `gc_sweep_intern_table` acquires write lock, so it is serialized with all readers and writers. The GC stop-the-world phase already pauses mutator threads, so in practice the write lock is uncontended during eviction.
- **platform_rwlock_t on Emscripten:** Stubs compile to no-ops since Emscripten is single-threaded. This matches the existing `platform_mutex_t` pattern.

## Success Metrics

- All existing string tests pass without modification (except removing RC-specific assertions if any exist)
- Zero `rope_unref` cascade storms — GC sweep handles dead rope cleanup in linear time
- Intern table `count` decreases after GC when strings are unreferenced (verified by eviction test)
- No data races detected under ThreadSanitizer when running concurrent intern tests
- 8-byte-per-node memory savings confirmed by comparing `sizeof(GCHeader)` vs `sizeof(RCHeader)` in rope node allocation

## Open Questions

None — all resolved:
- **Compaction timing:** Deferred. Compaction runs at the next insert that observes tombstone ratio > 25%, not during GC sweep. This keeps GC pauses predictable and amortizes rehash cost into mutator time.
- **Eviction scope:** Full GC only. `gc_sweep_intern_table` does not run during minor GC. Interned strings tend to be long-lived, and minor GC must stay fast.
