# JACL Codebase Audit

Scope: GC, concurrency, runtime; spot checks on the rest. ~112K LoC; this audit
focuses on the novel parts in depth.

## Critical correctness issues

### 1. `runtime__push_pinned` violates Chase-Lev SPSC contract
`src/runtime.c:435–442` calls `rt_deque_deque_push` on **another** worker's
`private_deque` from arbitrary threads. Chase-Lev `_push` is documented
owner-only (`lib/chase_lev/chase_lev.h:204`): it does plain (relaxed) loads/
stores of `bottom`, plain reads of `buffer`, and unsynchronized `dq->retired`
mutation during buffer grow. Two non-owners pushing concurrently → torn
`bottom`, lost task, or use-after-free on retired buffer. Callers at
runtime.c:1105, 1179, 1296, 1400, 1498 (spawn / SM resumption / continuations /
parallel / race) all hit this path. Fix: a real MPSC queue for `private_deque`,
or funnel pinned tasks through the inbox with a per-worker pinned-routing key.

### 2. GC reads work-stealing buffer without thief registration
`gc__scan_deque` (`src/runtime.c:530`) reads `dq->buffer` and
`buf->data[i & buf->mask]` directly. Chase-Lev's buffer reclamation is gated on
registered thieves' epochs (`CL_DEQUE_RECLAIM`, chase_lev.h:176). The GC isn't
registered, so the owner can `_push` → grow → retire → reclaim while the GC
reads. UAF. Fix: register a thief slot per worker for the GC, or extend the
epoch protocol so the scanner participates.

### 3. Grey buffer realloc vs. concurrent reader
`grey_buf_push` does plain `realloc` of `gb->entries` then a release-store on
`count`. The reader in `gc__drain_grey_bufs` does an acquire-load on `count`
and then loads `gb->entries[j]` (`src/runtime.c:692-693`). The first round is
safe — release/acquire publishes the new `entries` pointer. But the writer can
realloc *again* between the acquire and the entry reads, freeing the buffer
the reader is iterating. The long comment in `grey_buf_push`
(`src/gc.c:715-739`) only addresses the single-realloc case. Mitigations:
snapshot `gb->entries` under acquire, freeze realloc while drain is running,
or hand off ownership via two-phase buffer swap.

### 4. Concurrent GC never sweeps the intern table
`gc_collect` calls `gc_sweep_intern_table` (`src/gc_collect.c:667`).
`gc_concurrent_collect` does not, and instead treats every intern-table entry
as a strong root (`src/runtime.c:606-613`). Result: in multi-threaded mode,
interned strings are immortal → growing memory under string churn.
(Single-threaded major GC evicts them; this is a silent behavioral divergence.)
Also: `gc_mark_minor` adds intern entries as strong roots too, so even
single-threaded minor GC never evicts.

### 5. Tag overload: ParallelAgg / RaceAgg share `JACL_TAG_FUTURE`
`parallel_agg_ptr` / `race_agg_ptr` (`src/gc.c:1066, 1103`) reuse
`JACL_TAG_FUTURE`. Any code path that does `jacl_as_future(v)` on a
`JACL_TAG_FUTURE`-tagged value without first checking the underlying
`GCHeader.obj_type` reads garbage as `JaclFuture` (e.g. `OP_RESOLVE_FUTURE`;
future tracing in `gc__trace_object` already reads `fut->state`). Today the
design says "never exposed to user code," but this is one bytecode-emit bug
away from corruption. Give them distinct tags, or an internal-pointer tag.

### 6. Worker-loop GC trigger uses static threshold, not adaptive one
`src/runtime.c:262` compares `bytes_since_gc > GC_THRESHOLD` (the static 1 MB
constant) instead of `heap->gc_threshold` (the adaptive value
`gc__adjust_threshold` tunes). So adaptive scheduling is effectively dead in
multi-threaded mode. `gc_alloc` does the right thing inside the bump path
(`src/gc.c:354`), but only sets `needs_gc`; the worker-loop check then ignores
it.

### 7. Stale `current_block` snapshot in concurrent sweep
`gc_sweep_concurrent`'s `skip_block` is captured at sweep start
(`src/runtime.c:785`). If a worker exhausts that block and switches
`heap->current_block` during the sweep, the new active block isn't skipped and
is concurrently written by the allocator and walked by Phase 1. Watermark
protection saves correctness for the *object payload* of new allocations
(epoch ≥ watermark ⇒ live), but Phase 1 walks via `hdr->alloc_total`, which the
allocator writes non-atomically — torn read possible. Worth either pinning
`current_block` for the duration of a sweep or making Phase 1 robust to
in-flight headers.

### 8a. Lockless `inbox_count` read is a plain-int data race
M14 amendment §17.4 ("lockless empty check") reads `rt->inbox_count > 0` in
the worker loop (`src/runtime.c:198`) without a lock, while
`runtime__push_inbox` (`src/runtime.c:423`) does `rt->inbox[rt->inbox_count++]
= ...` under the mutex. The recheck-under-lock pattern is fine in principle,
but `inbox_count` is a plain `volatile intptr_t` — the lockless read alongside
the under-lock RMW is a data race under C11 (and TSAN confirms it on the first
push). Make it `_Atomic intptr_t` and use `ATOMIC_LOAD_EXPLICIT(...,
MEM_RELAXED)` / `ATOMIC_FETCH_ADD_EXPLICIT` (or store under lock with release
ordering).

### 8. Bit-field `mark` updated concurrently with allocator on neighboring object
The mark/gen/survive_count byte is updated by GC during mark/sweep and by
`gc_alloc` (which writes the entire 8-byte header). Within one object, single
writer at a time. Across the bit-field byte boundary, fine because each object
has its own byte. **But** `hdr->mark = mark` in the mark loop and
`hdr->survive_count = sc + 1` / `hdr->gen = 1` in sweep are read-modify-write
on the byte. If the GC is *resuming a previously-allocated object* while the
owning worker has freshly written that header (in newly recycled lines), the
RMW could clobber the worker's `epoch`/`obj_type`/`alloc_total` writes that
landed before the mark RMW finished its load. The skip_block invariant should
prevent this in the steady state, but combined with #7 it's not airtight.

## Architectural concerns

### 9. `gc_enumerate_roots` doesn't scan VM stacks; `gc_collect` does
The design (Section 3) claims CPS captures all live state into task closures,
so concurrent GC doesn't need to scan VM stacks
(`src/runtime.c:597-599`). Single-threaded `gc_mark` does scan the VM stack.
The invariant is load-bearing: any CPS segment that loads a value out of a
closure, computes a derived heap pointer, and holds it on the VM stack across
a `gc_alloc` call is at risk. There's no static enforcement that bytecode
emitted by the CPS transform never widens a heap reference between safepoints
in a way that loses reachability. Worth a property test (instrument GC at
random safepoints, verify no dangling stack pointers) plus an explicit list of
"forbidden patterns" the compiler must avoid.

### 10. Single global inbox is a contention point
`runtime__push_inbox` (`src/runtime.c:415`) is one mutex behind every external
submission, plus all pinned-on-fallback, plus most spawn / SM resumption. The
design's M14 amendment removed contention for the empty-check side; the push
side is still serialized. For high spawn-rate workloads, this caps throughput
at single-core mutex acquisition. Either per-worker MPSC inboxes, or a sharded
LIFO with worker hashing.

### 11. Worker idle backoff is `SLEEP_MILLISECONDS(backoff)` up to 10 ms
`src/runtime.c:281`. Latency for a task arriving when all workers are idle is
up to 10 ms (and 1 ms for `runtime__emergency_gc`'s polling at
`runtime.c:166`). Condition variables / `WaitOnAddress` / futex would let the
inbox pusher wake exactly one worker.

### 12. `JaclFuture` waiters list is mutated through a spinlock
M14 traded `platform_mutex_t` for a CAS spinlock to avoid the finalizer leak.
Reasonable. But under heavy contention (many waiters racing with a resolver),
spinning blocks the OS scheduler from migrating a worker that's holding the
lock. The hold window is small, but if a worker is preempted while holding
`f->lock`, every other waiter on that future spins. Consider parking after N
spins.

### 13. Tag dispatch in `jacl_is_heap_type`
`src/gc.c:88` is a long chain of comparisons against tag values on every
barrier and root push. The tags appear to be sparse bits at the top of the
value; a single bit test (or a 256-entry tag-byte lookup table) would cut
this. Hot path: every `grey_buf_push`, every `gc__ms_push_val`, every
`gc_write_barrier`.

### 14. `gc__find_free_run` slow path is O(blocks × 512)
When the current free run is exhausted, `gc_alloc` scans every block in the
heap from line 0 (`src/gc.c:393-401`). Each block scan is
O(GC_LINES_PER_BLOCK=512). Heaps grow to thousands of blocks under load. A
free-list (per-block "first free line" or a heap-level intrusive free-run
list) would convert this from O(n²) to O(1) amortized.

### 15. Block recycle in concurrent sweep races with owner traversal
`gc_sweep_concurrent` (`src/gc_collect.c:1109-1112`) unlinks fully-empty blocks
and returns them to the pool inline. If the owning worker's
`gc__find_fit_in_block` is scanning the heap's block list concurrently, the
block can be unlinked under it (the list is a plain `next` pointer, no atomics
on update or traversal). Walking a singly-linked list that's being mutated is
UB. The owning thread is the only mutator of `heap->blocks` except during
sweep — that's the race.

### 16. VM stack hard-capped at 256
`VM_STACK_MAX = 256` (`src/jacl.h:1394`) is tight: every call frame consumes
locals + temporaries, and deeply nested expression evaluation hits it. Either
grow on demand or document the limit and emit a clear error. Today an overflow
returns `VM_STACK_OVERFLOW` but most callers don't surface it gracefully.

### 17. Inconsistent thread-local convention
`gc__current_heap`, `gc__thread_epoch`, `gc__emergency_gc_fn`, `gc__interning`,
`rt__worker_id`, `rt__current_worker` are scattered globals. Worker context
already lives on `WorkerThread`. The thread-locals exist mostly for HAMT / RRB
template code that doesn't take a heap parameter. Threading a `JaclCtx*` (or
equivalent) through the templates would remove a class of hard-to-diagnose
"wrong heap" bugs and make sub-VM / sandbox embedding cleaner.

## Performance smells

- **HAMT / RRB template metaprogramming via thread-local heap pointer**: every
  collection mutation reads `gc__current_heap` from TLS rather than receiving
  it as a parameter. TLS reads are cheap but inhibit inlining of the alloc
  fastpath into a single function.
- **`gc__ms_push_val` always calls `jacl_is_heap_type`**, which is the long
  chain in (#13). Inlining hint or a tag-byte LUT in a header.
- **Worker loop tries inbox → private → public → steal from EVERY worker**
  every iteration with no jitter — `O(num_workers)` failed steal attempts when
  there's no work. Randomized victim selection is standard for work-stealing.
- **All collection layouts now assume 8-byte `GCHeader` prefix**, but the
  design originally had GCHeader replace a 16-byte RCHeader. Worth a sweep for
  stray `RCHeader` references / offset assumptions.

## Smaller things

- `gc_sweep` and `gc_sweep_concurrent` duplicate ~80% of their loop bodies;
  same for `gc_mark` and `gc_mark_minor` (root enumeration is copied twice). A
  `for_each_root` helper and a `sweep_block` with strategy flags would cut
  ~400 LoC.
- `gc__oom_panic_default` calls `abort()` without giving callers a chance to
  clean up. For embedded use, this should be a callback returning an error
  code.
- `volatile` is used on shared fields **in addition to** atomic load/store
  macros. The atomics are sufficient; the volatile is noise and on some
  compilers inhibits optimization.
- `parallel_agg_ptr`, `race_agg_ptr`, `jacl_future_ptr` all build the value
  with `(uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK`. The mask is unnecessary
  if pointer alignment is enforced — and if it *is* necessary, you're silently
  truncating high bits. Either way, the mask hides bugs.
- `DESIGN.md` lists M13 (concurrency) as **COMPLETE**, but several of the
  issues above are real correctness bugs in concurrency. Worth a "known
  issues" section so this isn't claimed as production-ready.

## Recommended next steps

In rough priority:

1. ~~Fix `runtime__push_pinned` (#1)~~ — **FIXED.** Per-worker pinned inbox
   (mutex-protected MPSC) routes cross-worker tasks, leaving `private_deque`
   pure SPSC. Owner drains its pinned inbox into private_deque at the top of
   each worker-loop iteration. Validated by `test/test_chaos_pinned_deque.c`
   passing in both normal mode and under `--tsan`.
2. ~~Make `inbox_count` atomic (#8a)~~ — **FIXED** along with #1. The
   under-lock RMW now goes through `ATOMIC_STORE_EXPLICIT(MEM_RELEASE)` and
   the lockless empty check uses `MEM_RELAXED`. Same pattern applied to the
   new `pinned_inbox_count`. `./build.sh --tsan --test=runtime` now gets
   through all 37 tests cleanly.
3. **A struct-layout drift hazard surfaced while fixing #1**: `WorkerThread`
   and `RuntimeTask` were defined in *both* `src/runtime.c` and `src/jacl.h`,
   and they had drifted (`RuntimeTask.gc_root3` was missing from jacl.h).
   Test code reads the jacl.h definition while `libjacl.a` ships the runtime.c
   definition, so any test that touches these fields reads the wrong bytes.
   Synced both. Long-term: deduplicate by having one definition and a forward
   declaration in the other file, or move the definition to a shared header.
4. ~~Fix the GC's deque scan to register as a thief (#2)~~ — **FIXED.** Each
   deque gets a persistent GC thief slot registered in `runtime_init`;
   `gc__scan_deque` brackets its read with epoch enter (RELEASE) / exit
   (RELEASE), pairing with the owner's ACQUIRE load in `CL_DEQUE_RECLAIM`.
   Chase-Lev also gained an opt-in `CHASE_LEV_ATOMIC_DATA` mode that uses
   atomic-relaxed accesses on `buf->data[i]` so TSAN can track the
   per-address synchronization. Validated by
   `test/test_chaos_gc_deque_scan.c` (passes under both normal and `--tsan`).
5. ~~Snapshot `gb->entries` after the acquire-load on count in
   `gc__drain_grey_bufs` (#3)~~ — **FIXED.** Added a mutex to `GreyBuffer`;
   both `grey_buf_push` (which reallocs) and `gc__drain_grey_bufs` (which
   reads) hold it. Pushes are already rare (only during `gc_active`), so
   contention is bounded. Also locked the grey-buffer reset at the end of
   the concurrent GC cycle. Validated by `test/test_chaos_grey_buf.c`
   passing under `--tsan` (was a clear UAF report without the fix).
6. ~~Pin `current_block` for the lifetime of a sweep, or have sweep re-check
   before touching each block (#7, #15)~~ — **FIXED.** Added
   `ThreadHeap.blocks_mutex`; `gc_alloc`'s slow path (list traversal +
   head-insert) and `gc_sweep_concurrent`'s entire traversal hold it.
   `gc_sweep_concurrent` re-snapshots `heap->current_block` under the lock,
   eliminating the stale-skip race (§7) and the unlink-while-walking race
   (§15) in one change. The hot allocation path (bump within current free
   run) is unaffected. Validated by `test/test_chaos_gc_alloc_sweep.c`
   under `--tsan`.
7. Wire `gc_sweep_intern_table` into `gc_concurrent_collect` (#4).
8. Make `runtime.c:262` use `heap->gc_threshold` (#6) — one-line fix.

All P0s addressed. Remaining items 7–8 are P1 and quick.

## Additional issues found by the soak test (Phase C)

The soak test (`test/test_chaos_soak.c`) — 4 driver threads submitting a
randomized mix of allocation / atom-write-barrier / pinned-task ops to 4
workers — surfaced several more issues that only show up under sustained
concurrent load. All fixed:

### S1. Task envelope freed before GC scan completes
`runtime__worker_loop` was calling `free(task)` after running the task,
before `currently_executing` was reset to IDLE. A concurrent
`gc_enumerate_roots` could observe the task pointer via
`currently_executing` and dereference `task->gc_root*` after the free.
Fix: defer task free until a new GC epoch starts (per-worker
`retired_tasks` list, drained when `global_epoch` advances past
`retire_epoch`). The GC scan that observed the task pointer must have
completed by the time we cross into a new epoch (gc_running CAS
guarantees no overlap).

### S2. `heap->bytes_since_gc` and `heap->gc_threshold` races
Both written by the allocator (worker) and read by `gc__adjust_threshold`
(GC worker). Fix: relaxed atomic accesses everywhere. These are
heuristics so relaxed is sufficient — the value being slightly stale is
fine, but the access itself must be atomic.

### S3. `heap->needs_gc` race
Written by `gc_alloc` (worker), `gc_concurrent_collect` end-of-cycle
(GC worker), and the VM safepoint. Fix: atomic-relaxed accesses
everywhere.

### S4. `gc__trace_object`'s mutable-ref trace was a plain load
Tracing `OBJ_MUTABLE_REF` read `MREF_VAL(ref)` as a plain load. Atom
mutations atomic-store this slot from worker threads. Race. Worse: a
plain load doesn't pair with the release-store, so the GC could see a
fresh pointer but miss writes to the pointed-to object's GCHeader.
Fix: atomic ACQUIRE load — pairs with the worker's RELEASE store.

### S5. Chase-Lev fence + relaxed-store opaque to TSAN
The Chase-Lev paper's pattern (relaxed-store-on-data, release-fence,
relaxed-store-on-bottom) is correct under C11 but TSAN's per-location
vector clocks don't track happens-before through a free-standing fence.
Fix: replace with a direct release-store on `bottom`. Same C11
guarantee, cleaner for TSAN. Also strengthened `CL_DATA_LOAD`/
`CL_DATA_STORE` (the new opt-in atomic data mode) from relaxed to
acquire/release, and matched the consumer side in `gc__scan_deque`.

### Phase C testing infrastructure

- `test/test_chaos_soak.c` — randomized concurrent workload, time-bounded,
  seed-reproducible. Currently configured for 4 drivers × 4 workers
  default 5s, override via `SOAK_DURATION_SEC` env var.
- Validated TSAN-clean at 5s, 30s, and 60s durations (433K tasks at 60s).
- Drivers self-throttle when the queue runs ahead of execution so the
  test terminates in bounded time even under TSAN's 20x slowdown.

## Testing infrastructure

A TSAN build mode is now available: `./build.sh --tsan [--test=NAME]`. It
uses a separate build cache (`.build-tsan/`), compiles everything with
`-fsanitize=thread -fno-omit-frame-pointer -O1`, and sets
`TSAN_OPTIONS=halt_on_error=1 exitcode=66` so the first race fails the test
loudly instead of cascading into UAFs.

`test/test_chaos_pinned_deque.c` is the first focused reproducer — a multi-
producer / single-consumer hammer on a Chase-Lev deque (the same template
instantiation as `runtime.c`'s `private_deque`). It fails in normal mode
(double-free in glibc) and under TSAN (data race on `bottom`, UAF on
retired buffers).

Further chaos work that would pay off: targeted reproducers for findings
#2, #3, #7/#15; randomized operation soak under TSAN; a CI matrix that runs
the full suite under both modes.
