# JACL Runtime Synchronization Reference

> **Companion docs**: `GC_CONCURRENCY_DESIGN.md` (the *why*),
> `AUDIT.md` (the *gaps*, with fix narrative + open-bug punch list).
> Any change to runtime/GC shared state should touch all three.

This document names — for every shared mutable field in the runtime + GC —
the writer(s), reader(s), synchronization primitive, memory order, and the
invariant the ordering preserves. It is meant as a reference, not a tutorial.

When you add or change a shared field, **update this document**. If you find
a field in the codebase that's not listed here, that's a bug (in the doc or
the code).

> Status as of 2026-05-13: all GC/concurrency unsoundness items the
> audit found are fixed — see §8 below for the full checklist. No
> correctness items are open. Phase D (compiler/VM survey) is also
> closed: §D.1 `JACL_ASSERT_TAG` pass, §D.2 frame-check reorder + the
> `VM_ERROR` stack-discipline pass, §D.6 spawn-recursion SEGV are all
> landed. §10 (single global inbox contention) is now also fixed —
> from-worker submissions push directly to own `public_deque`, no
> mutex, no CV signal; externals still use the global inbox path.
> See AUDIT.md for the per-commit story; remaining work in the
> punch list is architectural (§11–§17 minus the closed items,
> defer until profiled).

## Notation

- **W** = writer (thread that mutates)
- **R** = reader
- **Sync** = the synchronization primitive
- **Order** = C11 memory order on the publishing store(s)
- **Invariant** = the property the synchronization preserves

For mutex-protected fields, "Order: under-lock" means access is always under
the mutex, and the mutex's acquire/release pair provides the ordering. Some
fields use a hybrid: a fast-path atomic load outside the lock, then a re-read
under the lock.

## 1. Task scheduling

### `Runtime.inbox` (uintptr_t array) — global inbox (external-only)
- **W**: external (non-worker) threads only, under `inbox_mutex`
- **R**: any worker, under `inbox_mutex` (during drain)
- **Sync**: `inbox_mutex`
- **Order**: under-lock
- **Invariant**: contents are consistent while lock is held
- **Note (§10 fix, 2026-05-13)**: `runtime__push_inbox` now has a
  from-worker fast path that pushes to the caller's own `public_deque`
  and returns without touching this array or its mutex. Only external
  threads (`rt__current_worker == NULL` or pointing to a different
  runtime) reach this path.

### `Runtime.inbox_count` (volatile intptr_t)
- **W**: external pushers (under `inbox_mutex`); worker drainers (under lock)
- **R**: writers (under lock); workers (lockless empty-check)
- **Sync**: mutex for the array; atomic ops on the count itself
- **Order**: writers do `ATOMIC_STORE_EXPLICIT(..., MEM_RELEASE)` at end of
  push/drain. Lockless empty-check uses `MEM_RELAXED`.
- **Invariant**: lockless empty-check may miss the most recent push, but
  the next loop iteration always re-checks. The release-store ensures that
  when a reader later acquires `inbox_mutex`, it sees a count consistent
  with the array contents.

### `Runtime.inbox_cap`, `Runtime.inbox` (pointer)
- **W**: any external pusher growing the buffer, under `inbox_mutex`
- **R**: drainers under `inbox_mutex`
- **Sync**: `inbox_mutex`
- **Order**: under-lock
- **Invariant**: any pointer/cap change is visible to the next lock acquirer

### `WorkerThread.pinned_inbox`, `pinned_inbox_count`, `pinned_inbox_cap`, `pinned_inbox_mutex`
- Same pattern as the global inbox. Writer: any thread routing a pinned task
  to this worker. Reader: this worker (drain). Same release/relaxed/under-lock
  invariants apply.

### `WorkerThread.public_deque` — Chase-Lev SPMC
- **W**: owner only (`CL_DEQUE_PUSH`, `CL_DEQUE_TAKE`)
- **R**: owner (`TAKE`); other workers as registered thieves (`STEAL`); GC
  scanner via registered thief slot (`gc__scan_deque`)
- **Sync**: Chase-Lev's bottom (release/acquire), top (CAS), epoch
- **Order**: release on `bottom` post-write; acquire on `top` then `bottom`
  pre-read; CAS on `top` for race zone
- **Invariant**: at most one consumer claims each slot; readers see a value
  iff the corresponding bottom-increment is visible
- **Note**: `buf->data[i]` accesses go through `CL_DATA_LOAD`/`CL_DATA_STORE`.
  When `CHASE_LEV_ATOMIC_DATA` is defined (the runtime's instantiation), these
  are relaxed atomic; otherwise plain. Synchronization is via top/bottom.
- **Note (§10 fix, 2026-05-13)**: owner-side `_push` is now reached
  from two call sites: the worker loop's inbox/pinned-inbox drains
  (existing) and `runtime__push_inbox`'s from-worker fast path
  (added). Both run on the owner's thread, so the SPSC owner-push
  contract is preserved.

### `WorkerThread.private_deque` — Chase-Lev SPSC (no thieves)
- **W**: owner only
- **R**: owner only (other workers never touch it); GC scanner via registered
  thief slot
- **Pinned cross-worker submissions land in `pinned_inbox`, not `private_deque`**
  (audit §1 fix). The owner drains pinned_inbox → private_deque each loop.

### `WorkerThread.currently_executing` (volatile uintptr_t)
- **W**: owner only — sets BUSY, then task_ptr, then IDLE
- **R**: GC during `gc_enumerate_roots`
- **Sync**: atomic load/store
- **Order**: owner stores with `MEM_RELEASE`; GC reads with `MEM_ACQUIRE`
- **Invariant**: BUSY sentinel covers the gap between deque-pop and
  task-publish. GC spin-waits on BUSY (~ns) so it never misses an in-transit
  task. See GC_CONCURRENCY_DESIGN.md §7.

### `Runtime.gc_running` (volatile uint32_t)
- **W**: anyone trying to start a GC, via CAS 0→1; GC owner CAS 1→0
- **R**: any thread doing the start-CAS
- **Sync**: `ATOMIC_CAS`
- **Order**: `MEM_ACQ_REL` on success, `MEM_RELAXED` on failure
- **Invariant**: at most one concurrent GC cycle in flight

### `Runtime.gc_active` (volatile uint32_t)
- **W**: GC thread — set to 1 at mark start, 0 at sweep start
- **R**: every mutator on every write-barrier call (`gc_write_barrier`)
- **Sync**: atomic load/store
- **Order**: writer uses `MEM_RELEASE`; readers use `MEM_RELAXED` (fast path)
- **Invariant**: when 1, barrier pushes both old and new values to the
  thread-local grey buffer. Relaxed read on the fast path is safe: a stale
  "off" reading means a missed barrier for a value that won't be reclaimed
  this cycle anyway (next cycle will trace it).

### `Runtime.shutdown` (volatile int)
- **W**: main thread in `runtime_destroy`
- **R**: every worker, every loop iteration
- **Sync**: atomic load/store
- **Order**: writer `MEM_RELEASE`; readers `MEM_ACQUIRE`
- **Invariant**: workers see shutdown after `runtime_destroy` publishes it

## 2. GC orchestration

### `Runtime.global_epoch` (volatile uint64_t)
- **W**: GC thread at start of cycle (increment)
- **R**: workers when picking up a task (snapshot into `thread_epoch`)
- **Sync**: atomic
- **Order**: writer `MEM_RELEASE`; reader `MEM_ACQUIRE`
- **Invariant**: after GC publishes epoch N, every task picked up by any
  worker sees `thread_epoch >= N`

### `WorkerThread.thread_epoch` (volatile uint64_t)
- **W**: owner — at start of each task pickup
- **R**: GC computing the watermark; owner via `gc__thread_epoch` (TLS) for
  allocation stamping
- **Sync**: atomic
- **Order**: writer `MEM_RELEASE`; reader `MEM_ACQUIRE`
- **Invariant**: GC sees `thread_epoch ≥ N` ⇒ that thread has executed at
  least one task whose lifetime began at epoch N or later. Watermark =
  min(all thread_epochs) bounds which objects are safe to reclaim.

### `gc__thread_epoch` (JACL_THREAD_LOCAL uint32_t)
- **W**: owner thread, set at task pickup from `thread_epoch`
- **R**: owner thread only, inside `gc_alloc` to stamp `GCHeader.epoch`
- **Sync**: thread-local, no sharing
- **Invariant**: stamps new allocations with the owner's current epoch.
  Truncates u64 → u32; comment in `gc.c` documents wraparound safety
  (conservative — false positives possible after 2³² cycles, never false
  negatives).

### `WorkerThread.grey_buf.entries`, `.count`, `.cap`
- **W**: owner only (`grey_buf_push` from mutator code), under `gb->mutex`
- **R**: GC during mark phase (`gc__drain_grey_bufs`), under `gb->mutex`;
  GC also resets count to 0 at end of cycle, under the same mutex
- **Sync**: `gb->mutex`
- **Order**: under-lock. `count` retains its release-store for potential
  lockless empty-check readers, but there are none today.
- **Invariant**: reader and writer never overlap on `entries` (so realloc
  can't free the buffer the reader is iterating). Push is rare (only when
  `gc_active && jacl_is_heap_type(val)`), so mutex contention is bounded.

### `WorkerThread.remembered_set.entries`, `.count`, `.cap`
- **W**: owner only (`remembered_set_push` from generational write barrier)
- **R**: GC only during minor collection
- **Sync**: not concurrent in the current design — generational minor GC
  is single-threaded (only the concurrent major collector runs while
  workers are active)
- **Order**: n/a
- **Invariant**: minor GC and worker writes don't overlap
- **Note**: if concurrent minor GC is ever implemented, this needs the
  same atomic count + acquire/release treatment as `grey_buf`.

### `Runtime.gc_thief_public_ids`, `gc_thief_private_ids`
- **W**: main thread at init (write); destroy (read for unregister)
- **R**: GC during `gc__scan_deque` to address its thief slot
- **Sync**: not concurrent — set before threads start, freed after threads
  joined
- **Invariant**: arrays are stable for the runtime's lifetime

### Chase-Lev `dq->thieves[i].epoch` (volatile uint64_t)
- **W**: thief at enter (set to observed epoch); at exit (set to INACTIVE)
- **R**: owner inside `CL_DEQUE_RECLAIM` to compute min thief epoch
- **Sync**: atomic
- **Order**: thief stores with `MEM_RELEASE`; owner reads with `MEM_ACQUIRE`
- **Invariant**: if owner sees thief slot ≤ E, any retired buffer with
  retire_epoch ≤ E is pinned. This was upgraded from RELAXED→RELEASE in
  AUDIT.md §2 fix — RELAXED+ACQUIRE doesn't establish happens-before under
  C11.

## 3. Allocation

### `ThreadHeap.blocks` (singly-linked list)
- **W**: owner head-insert in `gc_alloc` slow path, under
  `heap->blocks_mutex`; GC unlinking empty blocks in `gc_sweep_concurrent`,
  under the same mutex
- **R**: owner list traversal in `gc_alloc` slow path (under lock); GC
  iteration during sweep (under lock)
- **Sync**: `heap->blocks_mutex`
- **Order**: under-lock
- **Invariant**: the hot allocation path (bump within current free-line
  run) does NOT touch `blocks` and does NOT acquire the lock. The slow
  path serializes with sweep, so the owner and GC never read/write the list
  concurrently.

### `ThreadHeap.current_block`, `cursor`, `limit`
- **W**: owner only, under `blocks_mutex` (slow path sets `current_block`,
  `cursor`, `limit` when switching to a new block); also owner-only
  unsynchronized `cursor` advance in the hot path
- **R**: owner only (hot path checks `cursor` against `limit`);
  `gc_sweep_concurrent` reads `current_block` under `blocks_mutex` to
  determine which block to skip
- **Sync**: `blocks_mutex` for the slow-path writes and the GC's snapshot
- **Order**: under-lock for the writes/snapshot; plain for the hot-path
  cursor advance (owner is the only writer)
- **Invariant**: `current_block` is consistent under the lock. GC's snapshot
  is fresh enough that the block it skips is exactly the one the allocator
  is currently bumping into.

### `ThreadHeap.bytes_since_gc`, `gc_threshold`, `needs_gc`
- **W**: owner (allocator) and GC worker
- **R**: owner (allocator, VM safepoint) and GC worker
- **Sync**: relaxed atomic ops on all accesses
- **Order**: `MEM_RELAXED` everywhere — these are heuristics, the value
  being slightly stale is acceptable, but the access itself must be
  atomic so TSAN can track it.
- **Note**: AUDIT.md §6 (the worker_loop's GC-trigger check used the
  static `GC_THRESHOLD` constant) is **fixed**: the trigger now reads
  `heap->gc_threshold` via `ATOMIC_LOAD_EXPLICIT(..., MEM_RELAXED)`, so
  adaptive scheduling is live in multi-threaded mode.

### `WorkerThread.retired_tasks`, `retired_epochs`, `retired_count`
- **W**: owner only — `runtime__retire_task` appends after a task runs;
  `runtime__drain_retired` compacts at the top of each loop iteration
- **R**: owner only
- **Sync**: none required (single owner)
- **Invariant**: a task is freed only when `global_epoch > retire_epoch`,
  which means a new GC cycle has started since the task was retired. The
  `gc_running` CAS guarantees no overlap, so by the time `global_epoch`
  advances, the previous GC's scan (which may have observed the task via
  `currently_executing`) has completed. Without this, freeing a task
  while the GC is dereferencing `task->gc_root*` is a UAF (surfaced by
  `test_chaos_soak` under TSAN).

### `ThreadHeap.current_mark`
- **W**: GC at end of cycle (toggles 0↔1)
- **R**: GC during next mark and sweep; allocator never reads
- **Sync**: none (GC is single-threaded across cycles via `gc_running`)
- **Invariant**: stable during a cycle; toggled only between cycles

### `ThreadHeap.old_gen_bytes`, `last_major_old_gen_bytes`, `gc_cycle_count`
- **W**: GC only
- **R**: GC only
- **Sync**: none (GC is single-threaded)

### `GCBlock.payload[]` (object payloads)
- **W**: owner during `bump_alloc`; mutators during normal use
- **R**: any worker (immutable values are shared freely); GC during mark
- **Sync**: depends on object type:
  - immutable objects (closures, HAMT/RRB nodes, strings): written once
    before any cross-thread reference exists. The cross-thread reference
    itself is published with release/acquire (e.g. through an atom).
  - mutable refs (atom): atomic ops on the value slot
  - mutable refs (box): owner-only by escape analysis; no cross-thread
    access (pinned task routing enforces this)
- **Invariant**: GC sees consistent payloads for marked objects because
  marking happens-after publication via the task root snapshot

### `GCBlock.line_map[]` (byte per line, 0=free, 1=occupied)
- **W**: owner during alloc (mark new line as occupied); GC during sweep
  (rebuild from live objects)
- **R**: owner during fit-finding
- **Sync**: one-directional transitions:
  - Allocator: 0→1 (mark occupied)
  - Sweep: 1→0 (mark free, after the dead object was zeroed)
- **Order**: no atomic ops; relies on the one-directional invariant + the
  fact that line_map values are 0x00 or 0x01 only
- **Invariant**: gc_collect.c:1052-1098 has a long comment justifying why
  partial reads / torn wide stores are benign here. Summarized: a partial
  read can produce either "occupied (looks non-zero)" or "free (zero, but
  payload was already zeroed in Phase 1)" — both safe. **TSan-flagged but
  benign**; would need suppression if running this path under TSAN.

### `GCHeader.epoch` (uint32_t)
- **W**: allocator at allocation
- **R**: GC during concurrent sweep (watermark comparison)
- **Sync**: none (allocator and concurrent sweep operate on disjoint blocks
  via skip_block, modulo audit §7)
- **Invariant**: object is collected only if `epoch < watermark` AND
  `mark != current_mark`

### `GCHeader.mark` (1 bit), `gen` (1 bit), `survive_count` (2 bits)
- **W**: GC mark phase (`mark`); GC sweep phase (`gen`, `survive_count`)
- **R**: GC mark phase
- **Sync**: none — these are bitfields in a byte
- **Invariant**: single-writer (always the GC thread), single-reader. Mutual
  exclusion via `gc_running`. Bitfield RMW on the byte is safe because no
  other thread writes that byte during a GC cycle.

### `GCHeader.obj_type`, `alloc_total`
- **W**: allocator at allocation
- **R**: GC sweep walking objects
- **Sync**: non-atomic, bound by skip_block (allocator writes only to
  current_block; sweep skips current_block)
- AUDIT.md §7 is fixed: `heap->blocks_mutex` now serializes
  `gc_alloc`'s slow-path list traversal + head-insert against
  `gc_sweep_concurrent`'s walk, and the sweep re-snapshots
  `current_block` under that lock. The owner-switches-current_block-
  mid-sweep race no longer exists.

### `BlockPool.free_list`, `total_blocks_allocated`
- **W**: any worker via `gc_block_pool_get`/`gc_block_pool_return`, under
  `pool->mutex`
- **R**: under same mutex
- **Sync**: `pool->mutex`
- **Order**: under-lock

## 4. String interning

### `JaclInternTable.entries`, `count`, `tombstone_count`, `cap`
- **W**: any thread interning a new string, under `table->lock`
- **R**: any thread looking up a string, under `table->lock`
- **Sync**: `table->lock` (mutex)
- **Order**: under-lock
- **Invariant**: consistent under lock
- **Special**: `INTERN_TOMBSTONE` is the address `(JaclHeapString*)1`,
  never a valid pointer (8-byte alignment).

### `gc__interning` (JACL_THREAD_LOCAL bool)
- **W/R**: owner only — set during `jacl_intern` to suppress GC eviction
  of entries during the same intern call
- **Sync**: thread-local, no sharing

### Intern eviction during concurrent GC
- AUDIT.md §4 is **fixed**:
  - `gc_enumerate_roots` no longer pushes intern entries (weak roots,
    matching single-threaded `gc_mark`).
  - `gc_concurrent_collect` calls `gc_sweep_intern_table` per worker
    between mark convergence (step 7) and block sweep (step 8).
  - `gc_sweep_intern_table` takes an epoch `watermark` argument:
    `hdr->epoch >= watermark` ⇒ live, so a string interned after the
    marker drained but before the sweep ran is protected. Single-threaded
    callers pass `watermark=0` to disable the branch and preserve
    historical behavior.
  - Sweep also takes `heap->blocks_mutex` for the entry walk (`gc__ptr_in_heap`
    enumerates `heap->blocks`); same lock the block sweep holds.
- The original "retroactively mark live" branch in `gc_sweep_intern_table`
  was removed: dead entries (mark mismatch, not epoch-protected) are
  always tombstoned. Power-of-two resize keeps the load near 0.5, so the
  old "load > 0.75 ⇒ evict, else keep alive" rule pinned unrooted strings
  cycle after cycle. `jacl_intern`'s existing tombstone compaction
  reclaims slots on insert.
- Validated by `test/test_chaos_concurrent_intern.c` (TSAN-clean over a
  30s run, ~85% eviction rate on ~2M churned strings).

## 5. Concurrency primitives

### `JaclFuture.state` (volatile uint32_t)
- **W**: resolver via `jacl_future_resolve` / `jacl_future_error`, under
  `f->lock`
- **R**: any awaiter
- **Sync**: spinlock + atomic
- **Order**: writer `MEM_RELEASE` on `state`; readers `MEM_ACQUIRE`
- **Invariant**: when state ≥ RESOLVED, `result` is valid for read

### `JaclFuture.result` (volatile uint64_t)
- Written under `f->lock` before `state` is published. Readers see it after
  the state acquire.

### `JaclFuture.waiters` (linked list)
- **W**: awaiter prepending; resolver clearing — both under `f->lock`
- **R**: under `f->lock`
- **Sync**: spinlock

### `JaclFuture.lock` (volatile uint32_t)
- CAS-based spinlock (M14 §17.3 — replaced `platform_mutex_t` to avoid the
  finalizer leak). 0 = unlocked, 1 = locked.

### `ParallelAgg.completed`, `errored`, `error_val`, `results[]`
- **completed**: atomic counter, `MEM_ACQ_REL` CAS to increment
- **errored**: CAS 0→1 (first-error-wins)
- **error_val**: written by the CAS winner with `MEM_RELEASE`, read by the
  joiner with `MEM_ACQUIRE`
- **results[]**: each slot written by a single task by index; no contention
- **state_machine**: written at construction, immutable thereafter

### `RaceAgg.settled`
- CAS 0→1, winner determines result. Same first-wins pattern as
  `ParallelAgg.errored`.

### `JaclMutableRef` value slot
- **Atom (atomic ref)**: atomic load/store/CAS on the value slot. Writes
  fire `gc_write_barrier`.
- **Box (thread-local ref)**: plain access, escape-analysis-pinned so no
  cross-thread reads happen.
- **Cell (set!-able mutable)**: similar to box, owner-only.

### `JaclStream.state`, `next_fn`, `cached_value`, `state_machine`, `args[]`
- Single-consumer streams: only one thread pulls at a time. No cross-thread
  contention in current usage.
- If streams are ever shared, this needs revisiting.

## 6. VM-local state (per-worker, not shared)

These are owner-only and require no synchronization. Listed for completeness:

- `VM.stack[]`, `stack_top`, `frames[]`, `frame_count`, `ip`, `chunk`
- `VM.env.values[]`, `env.names[]`, `env.count`, `env.cap`
- `VM.ctx`, `saved_ctx[]`, `saved_ctx_count`
- `VM.inline_slot_bitmap[]`
- `VM.error_message`, `error_line`, `stack_trace`
- `VM.gc_handle_slots[]` (embedding API — external code must not touch
  these from another thread; that's a contract on the embedder)

## 7. Cross-cutting: the BUSY-sentinel protocol

The trickiest single piece of synchronization in the design. Documented in
detail in `GC_CONCURRENCY_DESIGN.md §7`. Summary:

```
worker_loop iteration:
  store BUSY → currently_executing  (RELEASE)
  drain inbox / deques / steal
  if found:
    store task_ptr → currently_executing  (RELEASE)
    snapshot thread_epoch ← global_epoch  (RELEASE)
    run task
    store IDLE → currently_executing  (RELEASE)
```

GC during root scan:
```
load currently_executing  (ACQUIRE)
if BUSY: spin-wait up to N times, then yield/sleep
if task_ptr: scan task->gc_root{1,2,3}
```

The BUSY sentinel covers the ~3 instruction window between popping from a
deque and publishing the task pointer. Spin resolution is ns-scale because
the window is so tiny.

## 8. Known unsoundness checklist

| ID | Field/path | Status |
|----|------------|--------|
| §3 | grey_buf entries pointer during realloc | fixed 2026-05-12 (mutex) |
| §4 | intern table sweep in concurrent GC | fixed 2026-05-12 (weak roots + per-cycle sweep + epoch watermark) |
| §6 | adaptive GC threshold ignored in worker loop | fixed 2026-05-12 (atomic-relaxed load) |
| §7 | stale current_block in concurrent sweep | fixed 2026-05-12 (blocks_mutex) |
| §9 | SATB barrier gated on gc_active leaves a UAF window when a deref'd value is on a thread's stack and the source atom is mutated before gc_active=true | fixed 2026-05-12 (deletion-side push unconditional; insertion-side still gated) |
| §15 | block recycle race with owner heap walk | fixed 2026-05-12 (blocks_mutex) |
| line_map | benign torn-read race during concurrent sweep | known-benign, doc-only |

When you fix one of these, move the line to "fixed (date)" and remove the
caveat in the relevant section above.

## 9. How to update this document

- Adding a shared field: add a row in the appropriate section. Be specific
  about W, R, Sync, Order, Invariant. If any of those five is "n/a" or
  "none", justify why.
- Changing memory orders: update the row; consider whether existing readers
  need adjustment.
- Adding a new sync primitive (e.g. condvar, rwlock): add a section, and
  cross-reference from any field that uses it.
- Promoting a field from owner-only to cross-thread: this is the most
  common source of bugs. Audit every access site for the new access pattern,
  add the appropriate atomics/mutex, and write the row here before merging.

## 10. Outstanding design questions

Things this document doesn't yet pin down:

- **CPS continuations and VM stack reachability** — partially addressed.
  AUDIT.md §9 closed the worst hole: the SATB barrier now fires
  unconditionally on heap-typed overwrites, so a value loaded from a
  mutable container and held across a GC moment is protected even if
  its source is mutated before `gc_active=true`. AUDIT.md §D.3
  (`test/jacl/cps_inner_closure_capture.jacl`) verifies that inner
  closures defined before a suspension still observe their captures
  after. **Not yet built**: the property test the original design
  asked for (instrument GC at random safepoints, check VM stack for
  dangling references). Worth doing as belt-and-braces.
- **Finalization**: the design defers FFI resource cleanup to explicit
  `with-open` / `defer`. There's no current GC hook for finalization.
  If one is added, it needs to specify ordering w.r.t. sweep and write
  barriers.
- **OOM during concurrent GC**: `runtime__emergency_gc` recurses into
  `gc_concurrent_collect`. The behavior when an allocator hits OOM mid-GC
  is not pinned down in this document — it just spins on `gc_running`.
