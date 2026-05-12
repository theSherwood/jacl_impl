# JACL Codebase Audit

## If you're picking this up later

This audit was written over a few sessions in May 2026. All eight P0/P1
correctness items in the "Critical correctness issues" section are now
fixed, along with five additional issues the soak test surfaced.

**The loop that fixed each issue, for future use:**

1. Read the audit item.
2. Write a focused reproducer in `test/test_chaos_*.c` that pushes the exact
   pattern. Register it in `build.sh`. Keep it small enough that TSAN catches
   the race in seconds.
3. Run under `./build.sh --tsan --test=chaos_<name>`. Confirm the report
   matches the audit's prediction.
4. Implement the fix in the runtime/GC/chase-lev. Re-run the chaos test
   under both normal mode and `--tsan` until clean.
5. Run `./build.sh --tsan --test=runtime` and `--test=m13` to check for
   regressions. Run the full suite with `./build.sh` for normal-mode
   regression.
6. Update `SYNCHRONIZATION.md`: mark the unsoundness fixed in §8, update
   the per-field row, link the chaos test.
7. Update this file (`AUDIT.md`): strike the issue, link the chaos test,
   note any related issues found.

**Useful invocations:**

```sh
./build.sh                              # full normal-mode test sweep
./build.sh --tsan                       # full TSAN-mode sweep
./build.sh --test=<name>                # one test, normal mode
./build.sh --tsan --test=<name>         # one test, TSAN

# Soak (longer durations more likely to surface rare races):
SOAK_DURATION_SEC=60 \
  TSAN_OPTIONS="halt_on_error=1 exitcode=66" \
  ./.build-tsan/chaos_soak

# Reproduce a specific soak failure:
SOAK_SEED=<seed>  SOAK_DURATION_SEC=<sec>  ./.build-tsan/chaos_soak
```

**Three documents that should evolve together:**

- `GC_CONCURRENCY_DESIGN.md` — the *why* (design intent)
- `SYNCHRONIZATION.md` — the *what protects what*, per-field
- `AUDIT.md` (this file) — the *gaps between the two*, with fix status

Any change to runtime/GC shared state should update all three; struct
drift between `src/runtime.c`/`src/gc.c` and `src/jacl.h` is caught at
build time by `test/test_struct_sizes.c`.



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

### 4. Concurrent GC never sweeps the intern table — **FIXED**
~~`gc_collect` calls `gc_sweep_intern_table` (`src/gc_collect.c:667`).~~
~~`gc_concurrent_collect` does not, and instead treats every intern-table entry~~
~~as a strong root (`src/runtime.c:606-613`). Result: in multi-threaded mode,~~
~~interned strings are immortal → growing memory under string churn.~~

Three-part fix:
1. **Drop intern entries from the concurrent root set** (`gc_enumerate_roots`
   now leaves them out, matching single-threaded `gc_mark`).
2. **Sweep the intern table from `gc_concurrent_collect`** between the mark
   convergence loop and `gc_sweep_concurrent`, once per worker.
3. **Add epoch-watermark protection inside `gc_sweep_intern_table`** so a
   string interned after the marker drained but before sweep_intern_table
   ran (`hdr->epoch >= watermark`) survives, mirroring how the block sweep
   protects fresh allocations. Single-threaded callers pass `watermark=0`
   to disable the branch.

Two related issues fell out of the fix:

- **`gc__ptr_in_heap` walks `heap->blocks` without locking.** The
  concurrent intern sweep is the first caller hitting it from the GC
  thread while a worker may be head-inserting a block. Fixed by taking
  `heap->blocks_mutex` for the duration of the table walk (same lock
  the block sweep already takes — see §7/§15).
- **`gc_sweep_intern_table`'s "retroactively mark live" path was making
  unrooted strings effectively immortal.** Under power-of-two resize, the
  load factor sits around 0.5, so the high-load tombstone branch almost
  never fired and dead entries got pinned cycle after cycle. Removed the
  branch: dead entries (mark mismatch, not epoch-protected) are always
  tombstoned, and `jacl_intern`'s existing compaction reclaims them.

Note on `gc_mark_minor` (also pushes intern entries as strong roots):
left as-is — minor GC doesn't sweep the intern table either way, and
the next major GC evicts. Diverging this would buy nothing.

Validated by `test/test_chaos_concurrent_intern.c` — 4 workers churning
~2M unique unrooted strings over 3s under TSAN: post-fix, ~85% get
evicted into tombstones; pre-fix, every entry stayed live forever.

### 5. Tag overload: ParallelAgg / RaceAgg share `JACL_TAG_FUTURE`
`parallel_agg_ptr` / `race_agg_ptr` (`src/gc.c:1066, 1103`) reuse
`JACL_TAG_FUTURE`. Any code path that does `jacl_as_future(v)` on a
`JACL_TAG_FUTURE`-tagged value without first checking the underlying
`GCHeader.obj_type` reads garbage as `JaclFuture` (e.g. `OP_RESOLVE_FUTURE`;
future tracing in `gc__trace_object` already reads `fut->state`). Today the
design says "never exposed to user code," but this is one bytecode-emit bug
away from corruption. Give them distinct tags, or an internal-pointer tag.

### 6. Worker-loop GC trigger uses static threshold, not adaptive one — **FIXED**
~~`src/runtime.c:262` compares `bytes_since_gc > GC_THRESHOLD` (the static 1 MB~~
~~constant) instead of `heap->gc_threshold` (the adaptive value~~
~~`gc__adjust_threshold` tunes). So adaptive scheduling is effectively dead in~~
~~multi-threaded mode.~~

One-line fix: worker-loop trigger now uses an `ATOMIC_LOAD_EXPLICIT` of
`heap->gc_threshold` (relaxed) instead of the `GC_THRESHOLD` constant.
Now matches `gc_alloc`'s own check at `src/gc.c:368`, so adaptive
scheduling is live in multi-threaded mode.

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
(`src/runtime.c:822-829`). Single-threaded `gc_mark` does scan the VM stack
(`src/gc_collect.c:411-415`). The invariant is load-bearing.

**The audit (May 2026) traced this through the bytecode and found a real
soundness gap.** Empirical reproduction is hard (the race window is tight
and requires precise cross-thread alignment), but the gap is reachable in
principle. The full analysis follows.

#### 9a. The intended model

- Roots: each worker's `currently_executing` task (its `gc_root{,2,3}`
  fields hold the CPS closure + ctx), the public/private deques, the env,
  ctx pool, saved ctx stack. **Not** the VM operand stack.
- Closures trace `upvalues[]` *and* their chunk's constant pool
  (`src/gc_collect.c:128-148`), so heap-typed constants are reached
  transitively via the rooted closure.
- Within a CPS segment, a value pushed onto the operand stack is "safe"
  only if it is reachable from a real root *at every potential GC moment*.
  GC moments are safepoints — `vm.c:2004`, checked at the start of every
  opcode dispatch — and on this path concurrent mode submits
  `gc__concurrent_task` to be picked up by some worker, which then runs
  `gc_concurrent_collect`.
- Mutable containers (atoms, boxes, cells) are mutated via `OP_RESET` /
  `OP_SWAP` / `OP_BOX_SET_*`. The hybrid SATB + insertion barrier in
  `gc_write_barrier` is the design's defense against a "V removed from
  container but still on a thread's stack" race.

#### 9b. The gap: SATB barrier is gated on `gc_active`

`src/gc.c:865`:
```c
if (!ATOMIC_LOAD_EXPLICIT(gc_active_ptr, MEM_RELAXED)) return;
```

If `gc_active = false` at the moment of mutation, no barrier fires.
`gc_active` is only set to `true` at step 2 of `gc_concurrent_collect`
(`src/runtime.c:948`), which runs after a worker has picked up the
`gc__concurrent_task` from the inbox. The window between "trigger
submitted" and "gc_active = true" is non-zero (one inbox round-trip plus
task dispatch).

Concretely, the following sequence is unsound:

```
Thread T1 (mid-CPS-segment)          Thread T2 (different segment)         GC worker
─────────────────────────            ─────────────────────────             ─────────────
OP_DEREF atom A → V_old              ...                                   idle
   (V_old pushed on T1.stack)        ...                                   idle
   V_old.epoch = E_old               OP_RESET A V_new                      idle
                                        write barrier: gc_active=false
                                          → fast-path return, V_old NOT
                                            grey-buffered
                                        ATOMIC_STORE A ← V_new
                                        (V_old now reachable from no root)
OP_<allocating> X                                                          idle
   needs_gc = true
   safepoint: gc_concurrent_trigger                                        picks up task
                                                                           step 1: bump epoch
                                                                           step 2: gc_active=true
                                                                           step 3: watermark =
                                                                                   min(thread_epoch)
                                                                           step 4: enumerate roots
                                                                              A → V_new only
                                                                              T1.stack NOT scanned
                                                                           mark / sweep
                                                                              V_old.epoch < watermark
                                                                              → V_old reclaimed
OP_<use> V_old                                                             — — —
   UAF on V_old
```

For `V_old.epoch < watermark` to hold, V_old must have been allocated long
enough ago that every live thread's `thread_epoch` exceeds it. That's
exactly the profile of "old-gen heap value in a shared atom", which is
the dominant use of atoms in idiomatic JACL (long-lived state, infrequent
mutation, persistent-collection values).

The bytecode-level entry points into this race are the mutable-load
opcodes — `OP_DEREF`, `OP_GET_CELL_LOCAL`, `OP_GET_CELL_UPVALUE`,
`OP_GET_STATE_FIELD_CELL`. All push the contained value verbatim with no
grey-buffering. Loads from immutable composites (HAMT/RRB element access,
upvalue reads, struct field reads) are safe — the source structure stays
rooted, and the value is reachable through it.

#### 9c. Why CPS doesn't save us

The CPS transform splits at user-visible suspension points (`spawn`,
`await`, `parallel`, `race`, channel ops) and captures locals live across
each suspension into the next continuation closure. **Within** a segment,
locals/operands live only on the VM stack. Segments are long-running by
design (each instruction including allocations is fair game between
suspensions), so any mutable-load → use sequence inside a single segment
crosses many potential GC moments.

CPS handles the "value crosses a suspension" case — that value is in the
next closure. It does not handle "value loaded mid-segment, source
mutated by another thread, GC runs before consumption."

#### 9d. Severity

- **Reachable**: yes, in principle. Requires a long-lived value in a
  shared atom, a thread holding it on its stack across an allocation,
  and another thread (or the same thread) mutating the atom in the
  window before GC starts.
- **Empirically observed**: not yet. The current chaos tests don't
  exercise the exact alignment. `test/test_chaos_soak.c` performs
  cross-worker atom writes but doesn't hold deref'd values across
  allocations in the way that triggers the race.
- **Practical exposure**: depends on idiom. JACL programs that
  read-then-mutate the same atom from multiple workers under
  GC-triggering allocation pressure can hit it. Read-mostly atoms
  (the common case) avoid it.

#### 9e. Recommended hardening (ranked)

1. **Make the SATB barrier unconditional on the deletion side.** Drop
   the `gc_active` gate for the `old_val` grey-buffer push; always fire
   it when a heap value is overwritten in a mutable container. The
   insertion-barrier push can stay gated (insertion only matters during
   an active cycle). The grey buffer is reset at the end of every GC
   cycle, so a barrier push between cycles is a bounded waste of memory
   and one atomic store per mutation. Eliminates the race cleanly.
   Estimated work: hours, plus a regression run.

2. **Capture mutable-load values into a thread-local "stack root set."**
   On every `OP_DEREF` / `OP_GET_CELL_*` that produces a heap value,
   stash it into a per-worker root set that `gc_enumerate_roots` scans.
   Clear the slot when the value is consumed. More compiler involvement;
   matches the JVM "handle table" model. Heavier than (1).

3. **Scan the VM operand stack in `gc_enumerate_roots` with safepoint
   synchronization.** Add a BUSY-sentinel-style protocol so the GC can
   pause T at an opcode boundary, snapshot its stack, then continue.
   Standard tracing-GC approach; biggest change. Probably the cleanest
   long-term answer if JACL grows beyond CPS-only concurrency.

(1) is the right first move: small, contained, undoes the gap. A
follow-up chaos test should drive a thread to OP_DEREF an atom, force a
mutation from another thread, and force a GC trigger in the window —
ideally surfacing the UAF on TSAN/ASAN pre-fix so we have a regression
guard.

#### 9f. Adjacent, less critical observations from the survey

- `OP_DEREF` on a struct box allocates a fresh `HeapRecord`
  (`src/vm.c:5252`). The newly allocated `HeapRecord` is pushed onto the
  stack and consumed by the next opcode — fine, since fresh allocations
  are epoch-protected.
- `OP_CALL` / `OP_TAIL_CALL` on a generator allocates a state machine
  (`src/vm.c:2484-2521`) after arguments are already staged on the
  operand stack. The arguments themselves are on the stack across this
  allocation. If any argument is a derived heap value loaded from a
  mutable source in the same segment, it has the same risk profile as
  (9b). The generator path is uncommon enough that this is a secondary
  concern — but worth a chaos test once (1) is in.
- Locals are stored in `vm->stack[frame->stack_base + slot]`, i.e., on
  the operand stack itself. There is no separate "locals area." So a
  local holding a mutable-loaded value carries the same risk for the
  entire frame's lifetime.

There is no compiler-level static check that prevents the unsound
pattern. With (1) in place, no static check is needed: the runtime
barrier covers it. Without (1), trying to encode a "no allocation
between mutable load and consume" check in the compiler is fragile —
loads can be transitive (load atom → load field → load field), and
allocation can be hidden inside helper functions.

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
7. ~~Wire `gc_sweep_intern_table` into `gc_concurrent_collect` (#4)~~ —
   **FIXED.** Concurrent collect drops intern entries from the root set,
   then calls `gc_sweep_intern_table` per worker between mark convergence
   and block sweep. New epoch-watermark parameter protects strings
   interned after the marker drained. Surfaced and fixed two adjacent
   bugs: `gc__ptr_in_heap` was walking `heap->blocks` lock-free
   (now serialized via `blocks_mutex`), and the "retroactively mark
   live" path in the original sweep was pinning unrooted strings
   indefinitely under power-of-two resize (removed; always tombstone
   dead entries). Validated by
   `test/test_chaos_concurrent_intern.c` under both normal and `--tsan`
   (~85% eviction rate on ~2M churned strings).
8. ~~Make `runtime.c:262` use `heap->gc_threshold` (#6)~~ — **FIXED**,
   one-line as predicted: relaxed-atomic load of `heap->gc_threshold`
   replaces the static `GC_THRESHOLD` constant. Adaptive scheduling is
   now live in multi-threaded mode.

All P0/P1 correctness items are addressed.

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
