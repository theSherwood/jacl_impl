# GC & Concurrency Co-Design

> **Companion docs**: `SYNCHRONIZATION.md` (per-field W/R/Sync/Order/
> Invariant reference for everything described here), `AUDIT.md`
> (gap analysis, fix narrative, open-bug punch list). When this design
> doc changes, the other two should too.

## 1. Why Co-Design?

The GC must be non-stop-the-world, and immutable values are freely shared across threads.
These requirements make GC and concurrency inseparable:

- **CPS continuations** (concurrency) are what make roots scannable without stopping threads (GC)
- **Per-thread heaps** (GC) depend on the thread model (concurrency)
- **Epoch tracking** (GC) is maintained per-thread and per-task (concurrency)
- **Write barriers** (GC) apply to mutable containers whose thread-safety rules come from the concurrency model

This is a single unified design. Not M12-then-M13 or vice versa.

## 2. Thread Model

### Worker Pool

Fixed-size thread pool (one worker per core). Each worker thread owns:

| Component | Purpose |
|-----------|---------|
| Public deque | Chase-lev work-stealing deque for tasks touching only immutable/threadsafe values |
| Private deque | FIFO for tasks that touch thread-local mutable state (box, cell) |
| Heap | Bump allocator in blocks — only the owning thread allocates |
| Grey buffer | Thread-local append-only buffer for write barrier entries during GC |
| `currently_executing` | Atomic pointer to the continuation currently being executed |
| `thread_epoch` | Atomic uint64 — set to global epoch before each task |

### Tasks

Each task is a **CPS continuation closure**. All tasks are GC roots.

- **Public deque tasks**: can be stolen by any worker (touch only immutables)
- **Private deque tasks**: pinned to owning thread (touch thread-local mutables like `box`)

### Task Execution Loop

```
loop:
  1. Set currently_executing = BUSY (sentinel)
  2. Pop from private deque (priority) or public deque, or steal from another worker
  3. Set currently_executing = task
  4. Set thread_epoch = atomic_load(global_epoch)
  5. Execute the CPS segment (bytecode on local VM stack)
  6. Segment may enqueue new tasks (spawn, parallel branches)
  7. Set currently_executing = NULL
  8. goto loop
```

## 3. CPS Transform

The compiler splits procedures at **suspension points** — `spawn`, `await`, `parallel`, `race`, channel operations. Each CPS segment is a closure capturing all live variables at that point.

Between suspension points, a CPS segment executes as normal bytecode on the thread's local VM stack. This is the foundation of GC correctness:

- The continuation closure captures **all live state** from the previous suspension
- Any old object accessed between suspensions was reachable through the continuation (which GC scans)
- New allocations between suspensions get the current epoch (protected)
- Therefore: every live object is either **reachable from a scannable root** or **epoch-protected**

Without CPS, the VM stack is opaque to GC and this invariant breaks.

## 4. Epoch-Based Tracing GC

### 4.1 Epoch Tracking

- **Global epoch**: atomic uint64, incremented at each GC start
- **Thread epoch**: set to global epoch before each task; represents "this thread is up-to-date as of epoch N"
- **Object epoch**: stamped on each object at allocation time (allocating thread's current epoch)

### 4.2 Algorithm Overview

```
1. Increment global_epoch
2. watermark = min(all thread_epochs)
3. Root enumeration: all tasks (deques + currently_executing slots) + globals
4. Mark: trace from roots, mark reachable objects
   - Periodically drain grey buffers (write barrier entries)
5. Final grey buffer drain
6. Sweep each thread's heap:
   - marked → live
   - not marked, epoch ≥ watermark → live (too new to judge)
   - not marked, epoch < watermark → reclaim
```

### 4.3 Watermark

The watermark = `min(all thread_epochs)` at GC start. Any object allocated at or after the watermark is immune — it's too recent for GC to have complete information about.

**Tradeoff**: A slow thread (long-running CPS segment) holds back the watermark, delaying collection. This is conservative but correct. In practice, CPS segments should be short (suspension points are frequent).

### 4.4 GC Scheduling

GC runs *synchronously on the triggering thread* once it wins the
`gc_running` CAS. The CAS provides mutual exclusion; the
synchronous shape provides a forward-progress contract for any
peer that's spin-waiting on the flag.

```c
// Trigger: allocation threshold exceeded (worker-loop self-trigger,
// runtime__emergency_gc, and gc_concurrent_trigger all follow this
// shape)
if (thread->bytes_since_gc > GC_THRESHOLD) {
    if (atomic_cas(&gc_running, false, true)) {
        gc_concurrent_collect(rt);   // in-thread; resets gc_running = 0 on exit
    }
    thread->bytes_since_gc = 0;
}
```

Mutual exclusion via `gc_running` atomic flag. Only one GC at a
time. The original design had GC scheduled as a task on a deque
(any worker can execute it); that pattern was reverted after a
seed-dependent `chaos_soak` livelock (AUDIT_HISTORY.md §§20–21) —
if every worker fell into `emergency_gc`'s spin-wait before any
worker popped the queued GC task, the task stayed enqueued and
`gc_running` stranded at 1 forever. Symmetric synchronous triggers
across all three acquirer sites close that window by construction:
the only thread that can set the flag to 1 also runs the only
function that resets it to 0.

## 5. Object Headers

Every GC-managed heap object gets a header prepended:

```c
typedef struct {
    uint32_t epoch;        // allocation epoch
    uint8_t  mark;         // mark bit (alternates 0/1 each cycle to avoid clearing)
    uint8_t  obj_type;     // tells GC how to trace this object's references
    uint16_t alloc_total;  // total aligned allocation size (header + payload + padding)
} GCHeader;
```

Note: The `gen` and `_pad` fields from the original design were replaced by `alloc_total` during implementation. The sweep phase uses `alloc_total` to walk objects linearly through blocks without maintaining a separate object list. Generational collection (Section 11) will reclaim 2 bytes from `alloc_total` (switching to a line count) or expand the header when implemented.

### Object Types for Tracing

| obj_type | References | Trace strategy |
|----------|-----------|----------------|
| OBJ_NUMERIC | none | skip |
| OBJ_STRING | none | skip |
| OBJ_CLOSURE | upvalues[] array | trace each upvalue |
| OBJ_MUTABLE_REF | single JaclVal | trace the value |
| OBJ_HAMT_INTERNAL | children[] array | trace each child pointer |
| OBJ_HAMT_LEAF | key + value | trace both JaclVals |
| OBJ_HAMT_COLLISION | items[] array | trace each leaf |
| OBJ_RRB_INTERNAL | children[] array | trace each child pointer |
| OBJ_RRB_LEAF | elements[] array | trace each JaclVal element |
| OBJ_RRB_ROOT | root + tail pointers | trace both |
| OBJ_BIGNUM | none (numeric data) | skip |

### Replacing RCHeader

Currently HAMT/RRB nodes use `RCHeader { intptr_t ref_count; void (*destructor)(void*); }` (16 bytes). The `GCHeader` (8 bytes) replaces this — same position (prepended to payload), smaller, no destructor needed (GC handles lifecycle).

## 6. Per-Thread Heaps

### Structure

Each thread owns a heap composed of fixed-size blocks. Each block is subdivided into **lines** (Immix-style):

```
Thread Heap:
  [Block 0: 64KB] → [Block 1: 64KB] → [Block 2: 64KB] → ...
                                              ↑ current (allocation target)

Block layout (64KB = 512 lines × 128 bytes):
  [line 0][line 1][line 2]...[line 511]
  Each line: 128 bytes of object data

Block metadata (out-of-band, one byte per line):
  [F][F][O][O][O][F][F][F][O][F]...   F = free, O = occupied
```

The line map is stored separately from the block payload (one byte per line = 512 bytes per block). During allocation, the allocator bumps through contiguous sequences of free lines. During sweep, lines containing any live object are marked occupied; lines with no live objects are marked free.

**Why lines instead of whole-block reclamation**: With pure bump allocation into blocks, a single long-lived object pins an entire 64KB block. Line marking allows partial reclamation — if 400 of 512 lines are free, those lines are available for new allocations. This dramatically reduces fragmentation without requiring compaction or object movement.

### Bump Allocation

Allocation bumps a pointer through contiguous free lines. When the current run of free lines is exhausted, the allocator scans the line map for the next free run. When no free runs remain in any block, allocate a new block (all lines free).

```c
static inline void* gc_alloc(WorkerThread* t, uint8_t obj_type, size_t payload_size) {
    size_t total = sizeof(GCHeader) + payload_size;
    // Bump within current free-line run; find next run or new block if exhausted
    GCHeader* hdr = (GCHeader*)ptr;
    hdr->epoch    = t->thread_epoch;
    hdr->mark     = 0;
    hdr->obj_type = obj_type;
    hdr->alloc_total = total;
    return hdr + 1;  // return pointer past header (the payload)
}
```

Objects may span multiple lines. An object's "anchor line" is the line containing its header. All lines touched by an object are marked occupied if the object is live.

### Cross-Thread Access

- **Allocate**: only into own heap (no contention)
- **Read**: any thread can read any heap (immutables are shared freely)
- **GC sweep**: GC reads all heaps; only sweeps completed blocks (not the active allocation block of any thread)
- **Block recycling**: A block with all lines free is returned to a global block pool. Blocks with some free lines stay in the owning thread's heap for bump allocation through the free runs.

## 7. Root Enumeration

Roots are:
1. All tasks on all deques (public + private)
2. Each thread's `currently_executing` continuation
3. Global variables

### The Deque Snapshot Problem

Chase-lev deques support push/pop/steal but not "enumerate all elements." GC needs to see all tasks.

**Approach**: GC reads each deque's `top` and `bottom` indices, then reads all elements in `[top, bottom)`.

- Elements stolen between reading indices and reading data → those tasks are on another thread's deque or in `currently_executing` → GC sees them there
- Elements added after reading `bottom` → new tasks, epoch-protected
- May redundantly include already-completed tasks → conservative, harmless

### The Pop-in-Transit Race

Between a thread popping a task and publishing it to `currently_executing`, the task is invisible:

```
Thread:                          GC:
1. pop task T from deque
                                 2. snapshot deque (T gone)
                                 3. read currently_executing (still old value)
4. set currently_executing = T
                                 ... GC never sees T
```

**Fix**: The `BUSY` sentinel. Thread sets `currently_executing = BUSY` before popping (step 1 in the task execution loop). GC, upon seeing `BUSY`, spins briefly (nanoseconds) until it resolves to a real task or NULL.

## 8. Write Barriers

### The Question: Can We Just Bump the Epoch?

When storing a value into a mutable container (box/atom), update the stored value's epoch to the current thread's epoch. This makes the value immune to collection.

### Why It Fails for Composite Values

The epoch bump protects the directly stored value but **not its transitive references**.

```
Failure scenario (new value lost):

1. GC scans atom A → sees nil → nothing to trace
2. Thread T executes continuation C (captures map M, epoch 5, with HAMT children at epoch 3-5)
3. T does [swap! A (fn [_] $M)] → stores M into atom A
4. Epoch bump: M.epoch = 10 (T's epoch) → M is protected
5. C is consumed. T picks up next task C2. Publishes C2.
6. GC scans C2 → M not there
7. Sweep:
   - M: epoch 10 ≥ watermark → SURVIVES ✓
   - M's HAMT children: epoch 3-5, NOT marked, epoch < watermark → COLLECTED ✗
8. M has dangling pointers → use-after-free
```

The bump saves M but not the object graph hanging off it. For leaf values (numbers, strings) it's fine. For closures, vectors, maps — anything with outgoing references — it breaks.

### Why an Insertion-Only Barrier Is Also Insufficient

An insertion barrier (grey-buffer the new value on store) protects the new value and its transitive references. But it does **not** protect the old value being replaced. The old value can be lost if GC scans the container after the mutation:

```
Failure scenario (old value lost):

Thread T                         GC (on another worker)
─────────────────────────────    ─────────────────────────────
pop continuation C from deque
  C captures {atom A, fn F}
  (C does NOT capture V_old)
                                   ... scanning other roots ...
read atom A → V_old on VM stack
compute V_new = [F $V_old]
swap! A V_new
  → insertion barrier: grey-buffer V_new ✓
  → V_old is no longer in A
                                 scan atom A → see V_new → already grey-buffered ✓
                                 V_old: NOT marked, NOT grey-buffered
                                 V_old.epoch < watermark
                                 sweep: V_old is RECLAIMED
T still has V_old on VM stack → use-after-free
```

The root cause: between CPS suspension points, the VM stack is **not** a GC root — only continuation closures are. A value read from a mutable container onto the VM stack, then evicted from the container by another thread's mutation, becomes invisible to the collector.

A deletion-only barrier (SATB) would protect V_old here. But a deletion-only barrier does NOT protect the new value in the first scenario (the old value was `nil`, so grey-buffering `nil` is useless). JACL needs **both** barriers.

### Solution: Hybrid SATB + Insertion Barrier

When overwriting a mutable container during an active GC cycle, grey-buffer **both** the old value (SATB / deletion barrier) and the new value (insertion barrier):

```c
static inline void gc_write_barrier(WorkerThread* t, JaclVal old_val, JaclVal new_val) {
    if (!atomic_load_relaxed(&gc_active)) return;   // fast path: no GC in progress

    // SATB deletion barrier: protect the old value and its transitive references.
    // Prevents use-after-free when a thread holds V_old on its VM stack
    // and another thread overwrites the container.
    if (jacl_is_heap_type(old_val)) {
        grey_buffer_push(&t->grey_buf, old_val);
    }

    // Insertion barrier: protect the new value and its transitive references.
    // Prevents collection of a value stored into a container after GC has
    // already scanned that container, and the source continuation is consumed.
    if (jacl_is_heap_type(new_val)) {
        grey_buffer_push(&t->grey_buf, new_val);
    }
}
```

**Why both are needed:**

| Barrier | Protects against | Example |
|---------|-----------------|---------|
| Deletion (SATB) | Old value evicted from container, still held on a VM stack | Thread reads atom, another thread swaps it — old value is invisible to GC |
| Insertion | New value stored into container after its source continuation is consumed | Continuation stores a map into an atom, continuation is consumed, GC scans atom but never saw the continuation |

Neither barrier alone covers both cases. The hybrid covers both.

**Properties:**
- Thread-local writes → no contention
- Only fires on box/atom mutation during GC → rare in mostly-functional code
- Fast path (no GC active) is a single relaxed atomic load
- Two conditional pushes per mutation instead of one — negligible cost given mutation rarity
- GC drains all grey buffers periodically during mark phase + one final drain after mark completes

### What About Atoms Specifically?

`swap!` applies a function and CAS-es the result. The write barrier fires only on **successful** CAS. Failed CAS attempts don't change the container, so no barrier needed.

On successful CAS, the barrier receives both the old value (the value that was replaced) and the new value (the value that was stored). Both are grey-buffered if GC is active. This ensures:
- The old value (which other threads may have read before the swap) stays live
- The new value (which may have come from a now-consumed continuation) stays live

## 9. Mark Phase Details

### Concurrent Marking

The mark phase runs on the GC worker thread while other threads continue executing. It uses a **mark stack** (explicit worklist) to avoid deep recursion:

```
1. Push all root values onto mark stack
2. While mark stack is not empty:
   a. Pop value V
   b. If V is inline type → skip
   c. If V is heap type → get GCHeader
   d. If already marked (mark == current_mark) → skip
   e. Set mark = current_mark
   f. Based on obj_type, push V's children onto mark stack
3. Periodically: drain all threads' grey buffers → push entries onto mark stack
4. After main loop: final drain of grey buffers → process any new entries
```

### Mark Bit Alternation

Instead of clearing all mark bits before each GC (which would require touching every object), alternate the "current mark" value between 0 and 1 each cycle. An object is marked if `header->mark == current_mark`.

### Grey Buffer Draining

GC reads each thread's grey buffer. Since grey buffers are thread-local (only the owning thread appends), GC can safely read entries up to a snapshot of the count. New entries added after the snapshot are caught by the final drain.

## 10. Sweep Phase Details

### Line-Granularity Sweep

For each thread's heap, iterate completed blocks. Instead of maintaining per-object free lists, sweep updates each block's **line map**:

```
for each block in thread->heap->completed_blocks:
    clear all line marks to FREE
    for each object in block:
        alive = (obj->mark == current_mark) || (obj->epoch >= watermark)
        if alive:
            mark all lines touched by this object as OCCUPIED
    // Lines with no live objects are now FREE — available for bump allocation
```

After sweep, each block's line map reflects which 128-byte regions are available. The allocator bumps through contiguous runs of free lines, preserving the fast-path allocation speed.

### Block-Level Outcomes

| Outcome | Action |
|---------|--------|
| All lines free | Return block to global pool |
| Some lines free | Keep in thread's heap; allocator uses free runs |
| All lines occupied | Skip during allocation (no space) |

**Why this works**: Objects are never moved — live objects remain at their original addresses. Only the line map metadata changes. Worker threads reading live objects see no disruption because the payload bytes of live objects are untouched by the sweep.

**Concurrency**: GC sweeps completed blocks only. The active allocation block (where the owning thread is bump-allocating) is not swept — all objects in it are from the current epoch and are protected by the watermark.

## 11. Generational Collection (Sticky Mark-Bit)

### Why Generational?

Large persistent data structures (e.g., a 10-million-entry HAMT with ~1.5M internal nodes) are expensive to re-trace every GC cycle if they're long-lived and rarely modified. Generational collection avoids this: promote long-lived objects to an old generation, trace them less often.

### Why No Compaction Is Needed

Classic generational GCs use a copying nursery — young objects are allocated in a semi-space and survivors are copied to the old generation. This requires compaction (moving objects, updating pointers). The **sticky-mark-bit** technique avoids this entirely:

- Objects are promoted **in-place**. "Generation" is a metadata flag (`gen` field in `GCHeader`), not a physical location.
- Minor collections skip old-generation objects during sweep. No copying, no pointer updates, no forwarding addresses.
- Major collections trace and sweep everything, same as today's algorithm.

This is the approach used by Immix in generational mode. It pairs naturally with line-based sweep: old objects occupy their lines just like young objects do.

### Algorithm

**Promotion**: An object that survives N minor collections (initially N=1) is promoted: set `header->gen = 1`. The promotion threshold can be tuned — higher values keep objects young longer (better for short-lived bursts), lower values promote faster (less re-tracing).

**Minor GC** (frequent):
```
1. Increment global_epoch
2. Root enumeration (same as full GC)
3. Mark: trace from roots, but STOP at old-generation objects (don't trace their children)
   - Also trace from remembered set entries (old→young pointers)
4. Sweep: only sweep young objects (gen == 0)
   - marked → live; promote if survival count exceeded
   - not marked, epoch ≥ watermark → live (too new)
   - not marked, epoch < watermark → dead, update line map
5. Update line maps for affected blocks
```

**Major GC** (infrequent — triggered by old-generation growth threshold):
```
Same as current full GC algorithm. Trace everything, sweep everything.
All objects reset to gen = 0 if they die; survivors keep gen = 1.
```

### Remembered Set

The critical invariant: minor GC must know about all pointers from old-generation objects to young-generation objects (old→young pointers). Without this, minor GC would miss young objects only reachable through old objects.

**For JACL, the remembered set is naturally tiny:**

- **Immutable values** (the vast majority): Once promoted, an immutable object's references are fixed. All its referents were alive when it was promoted, so they'll be promoted too (same or earlier epoch). An old immutable **can never point to a young object** — it would have had to be mutated, which can't happen. These never need remembered set entries.

- **Mutable containers** (`box`, `atom`): These can store new (young) values into old containers. The existing hybrid write barrier (Section 8) already fires on mutation — extend it to also record a remembered set entry when an old container stores a young value:

```c
static inline void gc_write_barrier(WorkerThread* t, JaclVal container,
                                    JaclVal old_val, JaclVal new_val) {
    // Generational barrier: old container → young value (always active)
    if (jacl_is_heap_type(new_val)) {
        GCHeader* container_hdr = gc_header_of(container);
        if (container_hdr->gen == 1) {
            GCHeader* val_hdr = gc_header_of(new_val);
            if (val_hdr->gen == 0) {
                remembered_set_add(&t->remembered_set, container);
            }
        }
    }

    // Concurrent GC hybrid barrier (SATB + insertion, from Section 8)
    if (!atomic_load_relaxed(&gc_active)) return;
    if (jacl_is_heap_type(old_val)) {
        grey_buffer_push(&t->grey_buf, old_val);
    }
    if (jacl_is_heap_type(new_val)) {
        grey_buffer_push(&t->grey_buf, new_val);
    }
}
```

The remembered set is per-thread (no contention) and only grows when mutable containers are mutated — which is rare in mostly-functional JACL code. The generational check runs unconditionally (not gated on `gc_active`) because the remembered set must be maintained between GC cycles for the next minor collection.

### Scheduling

| Trigger | Collection Type |
|---------|----------------|
| Young allocation threshold exceeded (e.g., 1MB) | Minor GC |
| Old generation grown by >50% since last major | Major GC |
| Emergency (OOM) | Full STW major GC |

Minor GC is fast because it skips tracing old objects and only sweeps young lines. Major GC is the same as today's full collection. The ratio of minor-to-major collections depends on allocation patterns — mostly-functional code with short-lived intermediates will rarely trigger major GC.

### Implementation Ordering

Generational collection is an optimization on top of the base GC. Implement the base (non-generational) concurrent GC first, then add:

1. `gen` field to `GCHeader` (already included above)
2. Promotion logic in sweep phase
3. Remembered set data structure (per-thread append buffer, deduplicated on drain)
4. Minor GC mode that skips old objects
5. Scheduling heuristics for minor vs. major

## 12. Collection Migration (HAMT, RRB) — done

HAMT and RRB nodes are GC-managed. Each node type has its own GC
object tag (`OBJ_HAMT_INTERNAL` / `OBJ_HAMT_LEAF` / `OBJ_HAMT_COLLISION`,
`OBJ_RRB_INTERNAL` / `OBJ_RRB_LEAF` / `OBJ_RRB_ROOT`) and is traced from
`gc_collect.c` (`gc_collect.c:201–266`). The single-header libs still
expose `H_RC_REF` / `H_RC_UNREF` / equivalents as compatibility
macros, but they are zero-cost no-ops (`lib/hamt/hamt.h:132–133`). COW
operations create new nodes; old nodes become garbage when unreachable.

## 13. Arena Migration

### Keep on Arena (compile-time only)
- AST nodes
- Bytecode chunks and constant pools
- Compiler temporaries

### Move to GC Heap (runtime)
- Closures (`JaclClosure`)
- Heap strings (`JaclHeapString`)
- Heap numerics (`JaclHeapI64/U64/F64`)
- Mutable references (`JaclMutableRef`)
- BigNums (heap-allocated limbs)
- All collection nodes (HAMT, RRB)

### String Interning

The intern table is GC-aware: `gc_sweep_intern_table` (called from
`gc_collect`, `gc_collect.c:747`) evicts dead entries based on mark
bits before sweep zeroes their memory. Phase-2 weak-reference
interning is in place — the historical "Phase 1 immortal" plan was
superseded.

Historical phasing notes (kept for context):

- **Phase 1**: Keep immortal interning (simple, strings are usually long-lived)
- **Phase 2** (if needed): Weak-reference intern table — GC can reclaim unused strings

## 14. Concurrency Primitives

Built on top of the task/deque infrastructure:

| Primitive | Semantics | Implementation |
|-----------|-----------|----------------|
| `spawn` | Launch task, get future | Push continuation onto public deque, return future handle |
| `await` | Block until future resolves | CPS suspend — current continuation waits on future |
| `parallel` | Fork N tasks, join all | Push N continuations, parent waits for all results |
| `race` | Fork N tasks, take first | Push N continuations, first result cancels others |

Each of these is a **CPS suspension point** — the compiler splits the code here, creating a continuation closure for "what happens after."

## 15. Open Questions

### Root Enumeration
- Is the `BUSY` sentinel + spin sufficient, or do we need a more robust protocol?
- Should we add a separate task registry (append-only log) as a secondary root source?

**Answer**: BUSY sentinel + spin is sufficient. The vulnerability window is ~3 instructions (pop from deque, store to `currently_executing`). The spin resolves in nanoseconds. A separate task registry would add overhead to every enqueue/dequeue operation for no practical gain — the deque snapshot already provides conservative coverage, and `currently_executing` with the BUSY protocol closes the only gap.

### Sweep Concurrency
- Can GC sweep a thread's completed blocks while that thread reads from them? (Objects are only freed, not moved — reads of live objects remain valid.)
- Should sweep be distributed (each thread sweeps its own heap at a safe point)?

**Answer**: Yes, concurrent sweep of completed blocks is safe. The GC sweep writes only to object metadata (mark bits, free lists); worker threads read object payloads. Since objects are never moved, reads of live objects remain valid throughout the sweep. Distributed sweep (each thread sweeps its own heap) adds synchronization complexity for little benefit — a single GC worker sweeping all completed blocks is simpler and sufficient.

### GC Scheduling Heuristics
- Allocation-byte threshold? Heap growth rate? Fixed interval?
- Should GC be higher priority than regular tasks?

**Answer**: Allocation-byte threshold as the primary trigger, adaptive based on survival rate. High survival rate (most objects live) → increase the threshold (GC is doing wasted work). Low survival rate (lots of garbage) → decrease the threshold (collect more often). Start with a reasonable default (e.g., 1MB of new allocations since last GC) and tune from there. GC should run at normal task priority — not elevated. If GC falls behind allocation rate, the watermark mechanism naturally bounds memory growth: new objects are epoch-protected and can't be collected until threads advance, so the system self-regulates without priority escalation.

### Mark Stack Overflow
- Deep object graphs (long linked lists, deep trees) could overflow the mark stack
- Iterative deepening? Overflow buffer?

**Answer**: Overflow buffer. In practice, object graphs are shallow: HAMT depth caps at ~7 levels (5-bit hashing, 32-bit keys), RRB depth caps at ~7 levels (32-way branching), and closure nesting is bounded by `VM_FRAMES_MAX` (64). A fixed-size mark stack of 4096 entries handles all realistic workloads. For pathological cases (deeply nested user-constructed linked lists), use a dynamically-growing overflow list: when the mark stack is full, spill entries to the overflow list; after draining the main stack, process the overflow list. Simple and bounded.

### Finalization / FFI Resources
- File handles, network sockets, etc. need deterministic cleanup
- Weak references? Destructor callbacks on GCHeader? Explicit `close` discipline?

**Answer**: Explicit `close` discipline as the primary mechanism. GC finalization is fundamentally non-deterministic — relying on it for resource cleanup leads to leaks under pressure and unpredictable behavior. JACL should provide a `with-open` or `defer` construct that guarantees deterministic cleanup at scope exit, even on error paths. Destructor callbacks on `GCHeader` may be added later as a safety net (log a warning if a resource is GC'd without being closed), but never as the primary cleanup path. No weak references in phase 1 — add them only if string interning or caching demonstrates a concrete need.

### Deque Enumeration
- The snapshot approach (read top/bottom, read all elements) is conservative. Is that acceptable, or do we need exact enumeration?

**Answer**: Conservative snapshot is acceptable. The worst case is that GC traces a task that has already completed (still visible in the deque snapshot between index reads and element reads). This keeps some garbage alive for one extra GC cycle — harmless. Exact enumeration would require pausing all workers or adding per-element metadata, neither of which is worth the cost for a marginal reduction in floating garbage.

### Cancellation Semantics
- When `race` picks a winner, how are losing tasks cancelled?
- Can a cancelled task leave mutable state in an inconsistent state?

**Answer**: Cooperative cancellation. Each task has an `is_cancelled` atomic flag. When `race` picks a winner, it sets `is_cancelled` on all losing tasks. A running task checks this flag only at CPS suspension points (the same points where `spawn`, `await`, `parallel`, `race`, and channel operations occur). Between suspension points, a CPS segment executes as uninterruptible bytecode. This means:

- A task that hasn't started yet (still on a deque) is simply discarded when popped and found cancelled.
- A task that is mid-execution runs to its next suspension point, then is discarded.
- A CPS segment is therefore atomic with respect to cancellation — mutable state cannot be left inconsistent, because the segment either completes fully or never started.

For atoms: `swap!` is CAS-based and completes within a single CPS segment, so cancellation cannot interrupt it mid-operation.

### OOM Handling
- Thread can't allocate a new block: trigger emergency GC? Grow heap limit? Panic?

**Answer**: Three-tier escalation:

1. **Emergency synchronous GC**: Stop-the-world, full collection. Acceptable because this only fires under extreme memory pressure — the normal concurrent GC should prevent this in practice.
2. **Heap growth**: If emergency GC doesn't reclaim enough, request more blocks from the OS up to a configurable maximum heap size.
3. **Panic**: If the OS refuses allocation (or max heap size is reached), raise an unrecoverable OOM error with a diagnostic message (current heap size, allocation request size, survival rate).

## 16. Implementation Ordering

These must be interleaved, not sequential:

1. **Object headers** — add `GCHeader` to all heap types, modify constructors
2. **Per-thread heap allocator** — bump allocator with block lists
3. **CPS transform** — compiler splits at suspension points, creates continuation closures
4. **Thread pool + deques** — worker threads, work-stealing, task execution loop
5. **Root enumeration** — deque snapshot, `currently_executing` protocol
6. **Mark phase** — concurrent tracing from roots
7. **Write barriers** — grey buffer on box/atom mutation
8. **Sweep phase** — per-thread heap sweeping
9. **Collection migration** — remove RC from HAMT/RRB, use GC heap
10. **Concurrency primitives** — `spawn`, `await`, `parallel`, `race`

Steps 1-2 can be prototyped single-threaded. Step 3 is a compiler change (independent). Steps 4-8 bring up the concurrent GC. Step 9 removes the old RC system. Step 10 adds the user-facing API.

## 17. M14 Amendments (Post-Audit)

An audit of the M12/M13 implementation identified several issues requiring design amendments.

### 17.1 Multi-Root Task Scanning

**Problem**: `RuntimeTask.gc_root` holds a single `JaclVal`, but several task types need two roots:

| Task type | gc_root (rooted) | Unrooted value |
|-----------|-----------------|----------------|
| Continuation scheduling | continuation closure | result value |
| Spawn submission | future | spawn closure |
| Parallel task | parallel agg | task body closure |
| Race task | race agg | task body closure |

If GC runs between task submission and execution, the unrooted value can be collected when no other reference keeps it alive. Epoch watermarking and eager CPS capture mitigate this in practice, but the design is unsound.

**Amendment**: Add `gc_root2` to `RuntimeTask`. All task submission sites populate both slots. Root enumeration (`gc_enumerate_roots`, `gc__scan_deque`, inbox scanning) scans both roots. This is a minimal change that covers all current cases without over-engineering.

### 17.2 Grey Buffer Memory Ordering

**Problem**: Section 9 states "GC can safely read entries up to a snapshot of the count." This is true on x86 (TSO), but under C11 and on weakly-ordered architectures (ARM, POWER), the plain store to `entries[count]` and plain increment of `count` in `grey_buf_push` can be reordered relative to the plain loads in `gc__drain_grey_bufs`. This is undefined behavior.

**Amendment**: `GreyBuffer.count` must use release semantics on write (ensuring the entry store is visible before the count increment) and acquire semantics on read (ensuring the GC sees all entries up to the read count). The `entries[]` array itself remains plain stores (single-writer, the owning thread), but the release on `count` provides the necessary ordering guarantee.

### 17.3 Future Locking

**Problem**: `JaclFuture` contains a `platform_mutex_t` that is never destroyed when the future is garbage collected. No finalizer mechanism exists.

**Amendment**: Replace the mutex with a CAS-based spinlock (`volatile uint32_t lock`). The lock is held for ~10 instructions during the resolve-vs-add_waiter race window, making spinning appropriate. A spinlock has no OS resource to finalize, eliminating the leak. This also slightly reduces `JaclFuture`'s size (mutex → uint32).

### 17.4 Inbox Contention

**Problem**: Every worker acquires `inbox_mutex` on every loop iteration to check for tasks. The inbox is empty ~99% of the time, creating unnecessary contention.

**Amendment**: Two changes:
1. **Lockless empty check**: Read `inbox_count` (volatile) without the lock. Only acquire the mutex if non-zero.
2. **Batched drain**: When non-empty, drain all pending tasks in one lock acquisition, pushing them to the worker's private deque.

This eliminates lock acquisition for the common case (empty inbox) and reduces per-drain cost from O(tasks) lock acquisitions to O(1).

### 17.5 Push-Side Inbox Contention (AUDIT §10, 2026-05-13)

**Problem**: 17.4 fixed the empty-check side. The *push* side was still serialized by `inbox_mutex`: every `runtime__push_inbox` call (which covers `runtime_submit`, all non-pinned `runtime__schedule_continuation`, all non-pinned `runtime__schedule_sm_resumption`, and all spawn/parallel/race fallbacks when the closure isn't pinned) acquired the mutex. On `spawn_chain`, this means every continuation submitted from a worker funnels through one global mutex — and the worker is also the one that will most likely run it next, so the mutex round-trip is pure overhead.

**Amendment**: `runtime__push_inbox` fast-paths on `rt__current_worker`. If the caller is a worker thread in this runtime, push directly to `self->public_deque` and return — Chase-Lev SPSC on the owner-push side, no mutex, no CV signal. Other workers steal from `public_deque` via the existing steal path, preserving load balancing. Externals (no worker context) keep the global inbox + CV signal path.

Why `public_deque` rather than per-worker MPSC inbox:
- The owner-side `_push` is the canonical Chase-Lev pattern. Already exercised by the worker loop's inbox/pinned-inbox drains.
- The §1/§2 fixes already cover safety: SPSC owner-push, GC thief slots registered at init, epoch-protected buffer reclamation.
- No new data structures; ~17 lines in `runtime.c`.

Why no CV signal on the fast path:
- The submitter is itself active (it's mid-loop). It will pop its own deque on the next iteration.
- Parked workers find the task via the steal path within the 1ms CV timeout (the §11 bounded latency we already accepted).
- Signaling would require briefly acquiring `inbox_mutex`, defeating the point of the fast path.

Validation (2026-05-13): `spawn_chain` N=200 — `inbox_pops` 11407 → 220 (98% drop), `steal_successes` 0 → 1873, wall median ~7.3ms → ~6.5ms. Full normal-mode 88/88; TSAN baseline unchanged; all 7 chaos tests TSAN-clean; gc_stress × 250 = 0 fails; 60s `chaos_soak` TSAN = 216K tasks, 0 heap problems.
