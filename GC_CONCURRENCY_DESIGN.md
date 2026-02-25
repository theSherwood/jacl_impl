# GC & Concurrency Co-Design

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

GC is itself a task on a deque. Any worker can execute it.

```c
// Trigger: allocation threshold exceeded
if (thread->bytes_since_gc > GC_THRESHOLD) {
    if (atomic_cas(&gc_running, false, true)) {
        enqueue_gc_task(thread->public_deque);
    }
    thread->bytes_since_gc = 0;
}
```

Mutual exclusion via `gc_running` atomic flag. Only one GC at a time.

## 5. Object Headers

Every GC-managed heap object gets a header prepended:

```c
typedef struct {
    uint32_t epoch;      // allocation epoch
    uint8_t  mark;       // mark bit (alternates 0/1 each cycle to avoid clearing)
    uint8_t  obj_type;   // tells GC how to trace this object's references
    uint16_t _pad;       // alignment
} GCHeader;
```

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

Each thread owns a heap composed of fixed-size blocks:

```
Thread Heap:
  [Block 0: 64KB] → [Block 1: 64KB] → [Block 2: 64KB] → ...
                                              ↑ current (allocation target)
```

### Bump Allocation

Within a block, allocate by bumping a pointer. When full, allocate a new block. Fast path is a pointer bump + epoch stamp — no locks, no contention.

```c
static inline void* gc_alloc(WorkerThread* t, uint8_t obj_type, size_t payload_size) {
    size_t total = sizeof(GCHeader) + payload_size;
    // ... bump allocator, get new block if needed ...
    GCHeader* hdr = (GCHeader*)ptr;
    hdr->epoch    = t->thread_epoch;
    hdr->mark     = 0;
    hdr->obj_type = obj_type;
    return hdr + 1;  // return pointer past header (the payload)
}
```

### Cross-Thread Access

- **Allocate**: only into own heap (no contention)
- **Read**: any thread can read any heap (immutables are shared freely)
- **GC sweep**: GC reads all heaps; only sweeps completed blocks (not the active allocation block of any thread)

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
Failure scenario:

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

### Solution: Grey Buffer

When storing value V into a mutable container during an active GC cycle, push V onto the current thread's grey buffer. The GC drains grey buffers during the mark phase, tracing V and all objects reachable from V.

```c
static inline void gc_write_barrier(WorkerThread* t, JaclVal new_val) {
    if (!atomic_load_relaxed(&gc_active)) return;   // fast path: no GC in progress
    if (!jacl_is_heap_type(new_val)) return;         // inline values need no barrier
    grey_buffer_push(&t->grey_buf, new_val);
}
```

**Properties:**
- Thread-local writes → no contention
- Only fires on box/atom mutation during GC → rare in mostly-functional code
- Fast path (no GC active) is a single relaxed atomic load
- GC drains all grey buffers periodically during mark phase + one final drain after mark completes

### What About Atoms Specifically?

`swap!` applies a function and CAS-es the result. The write barrier fires only on **successful** CAS. Failed CAS attempts don't change the container, so no barrier needed.

For `swap!`, the old value was either already traced by GC (if GC scanned the atom) or will be traced (if GC hasn't scanned it yet). The new value may not be traced → write barrier handles it.

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

For each thread's heap, iterate completed blocks:

```
for each block in thread->heap->completed_blocks:
    for each object in block:
        if (obj->mark == current_mark) → live, do nothing
        else if (obj->epoch >= watermark) → live (too new), do nothing
        else → dead, add to free list / reclaim
```

**Concurrency**: GC sweeps completed blocks only. The active allocation block (where the owning thread is bump-allocating) is not swept — all objects in it are from the current epoch and are protected by the watermark.

## 11. Collection Migration (HAMT, RRB)

### Current: Reference Counting

HAMT and RRB nodes use `RCHeader` (refcount + destructor). COW operations `RC_REF` shared nodes and `RC_UNREF` replaced nodes, triggering cascading frees.

### Future: GC-Managed

- Replace `RCHeader` with `GCHeader` on all collection nodes
- Route allocation through per-thread GC heap (`gc_alloc`)
- Remove all `RC_REF` / `RC_UNREF` calls from collection operations
- COW operations just create new nodes; old nodes become garbage when unreachable

**Benefits:**
- No cascading decrements (latency spikes eliminated)
- No atomic refcount operations
- Simpler collection code

## 12. Arena Migration

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

Currently strings are interned forever in an arena-backed table. With GC:

- **Phase 1**: Keep immortal interning (simple, strings are usually long-lived)
- **Phase 2** (if needed): Weak-reference intern table — GC can reclaim unused strings

## 13. Concurrency Primitives

Built on top of the task/deque infrastructure:

| Primitive | Semantics | Implementation |
|-----------|-----------|----------------|
| `spawn` | Launch task, get future | Push continuation onto public deque, return future handle |
| `await` | Block until future resolves | CPS suspend — current continuation waits on future |
| `parallel` | Fork N tasks, join all | Push N continuations, parent waits for all results |
| `race` | Fork N tasks, take first | Push N continuations, first result cancels others |

Each of these is a **CPS suspension point** — the compiler splits the code here, creating a continuation closure for "what happens after."

## 14. Open Questions

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

## 15. Implementation Ordering

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
