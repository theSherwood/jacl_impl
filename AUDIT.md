# JACL Codebase Audit

> **Companion docs**: `GC_CONCURRENCY_DESIGN.md` (design intent),
> `SYNCHRONIZATION.md` (per-field reference for shared runtime/GC
> state). Open-bug punch list lives in this file's
> "Start-here for next session" section below.

## If you're picking this up later

This audit was written over several sessions in May 2026. All eight
GC/concurrency correctness items in "Critical correctness issues" are
fixed, plus five soak-surfaced fixes, plus the §9 SATB-barrier UAF.
Phase D (compiler/VM) surveyed three categories and landed a
frame-check ordering fix, but **opened one new correctness bug
(§D.6)** that is now the only open correctness item.

### Status at end of phase D (2026-05-12)

| Phase | Scope | Outcome |
|-------|-------|---------|
| A | GC/concurrency design audit | 6 P0 fixes, 60s TSAN-clean soak |
| B | Soak-surfaced races | 5 additional fixes, TSAN-validated |
| C | Remaining P1s (§4, §6) | Intern sweep + adaptive threshold fixed |
| C+ | §9 deep-dive | Real UAF in SATB barrier; fixed unconditionally |
| D | Compiler/VM audit | 3 surveys; frame-check ordering fix landed; **§D.6 opened** (spawn + deep recursion SEGV — still open) |

Eight P0/P1 items in "Critical correctness issues" fixed; five
soak-surfaced issues fixed; §9 SATB-barrier UAF fixed; §D.1/D.2/D.3
surveyed; §D.6 opened during the §D.3 test work. See git log for the
per-commit story.

### Start-here for next session

**Open correctness bug: §D.6** — *the one thing to fix next.* SEGV
from valid user code:

```jacl
proc burn {n} {
  if [== $n 0] { 1 } else {
    def g [vec 1 2 3 4 5 6 7 8 9 10]
    burn [- $n 1]
  }
}
proc main {
  def f [spawn { burn 70 }]
  def _ [await $f]
}
main
```

Without `spawn`: clean `stack overflow` at depth 64 (`VM_FRAMES_MAX`).
Inside `spawn`: SEGV at depth ≈58 before the boundary check fires.
ASAN points to `src/vm.c:8190` in `OP_SET_STATE_FIELD`:

```c
JaclVal state_val = vm->stack[frame->stack_base + 0];
JaclStateMachine *sm = jacl_as_state_machine(state_val);  // unchecked
sm->fields[field_index] = value;  // SEGV — sm is garbage
```

Full analysis + ASAN trace + investigation hypotheses in `§D.6` below.
Suggested first move: dump the bytecode for both the spawn body and
`burn`'s body, compare which opcode is at the SEGV PC, and trace it
back to its emit site in `src/compiler.c`. A `--dump-bc` flag may or
may not exist; if not, a 20-LoC dump helper added temporarily to the
test harness is the fastest path.

### Punch list (in priority)

1. **§D.6 SEGV** — only open correctness bug. Above.
2. **§D.1 `JACL_ASSERT_TAG` macro** — ~32 unchecked `jacl_as_*`
   extraction sites in `vm.c`. §D.6 proves these aren't safely
   "trust the compiler"; debug-only assert would have caught §D.6 at
   the call site instead of as a SEGV. Mechanical, ~50 LoC. Likely
   surfaces other latent issues during the rebuild — worth doing
   right after §D.6.
3. **§D.2 remaining frame-check reorderings** — `OP_PARALLEL`,
   `OP_RACE`, `OP_SPREAD`. Same pattern as the fix that landed for
   `OP_EACH` / `OP_TRANSFORM` / `OP_FILTER` (commit `c39ee12`).
   ~10 LoC.
4. **§D.2 broader stack-discipline cleanup** — ~60 sites where a
   `return VM_RUNTIME_ERROR` leaves the operand stack unbalanced.
   Macro-based refactor. Pure cleanup; defends against DoS-style
   slot drift.
5. **Architectural items §10–§17** — throughput, latency, ergonomics.
   Not correctness. Defer until profiled.

§D.3 is closed: `test/jacl/cps_inner_closure_capture.jacl` verifies
that inner closures defined before a suspension still see their
captured values after. Passes.

### How earlier phases were structured

**The loop each fix went through:**

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

# Run a single .jacl test directly through the harness:
.build/jacl_harness test/jacl/<name>.jacl

# Soak (longer durations more likely to surface rare races):
SOAK_DURATION_SEC=60 \
  TSAN_OPTIONS="halt_on_error=1 exitcode=66" \
  ./.build-tsan/chaos_soak

# Reproduce a specific soak failure:
SOAK_SEED=<seed>  SOAK_DURATION_SEC=<sec>  ./.build-tsan/chaos_soak

# Ad-hoc ASAN build for crash investigation (used to trace §D.6):
cc -fsanitize=address -fno-omit-frame-pointer -g -O0 -D_DEFAULT_SOURCE \
   -o /tmp/jacl_asan src/jacl.c test/test_jacl_harness.c -lpthread -lm
/tmp/jacl_asan path/to/repro.jacl
```

**Expected normal-mode suite result:** 87 passed, 14 pre-existing
failures (all `hamt*` / `rrb_vec*`, blocked by the removed `lib/rc`
include path — unrelated to the audit).

**Chaos test inventory (all `--tsan`-clean as of end of phase D):**
| Chaos test | Reproduces |
|------------|-----------|
| `chaos_soak` | randomized 4×4 worker/driver load — catches general regressions |
| `chaos_pinned_deque` | §1 cross-worker MPSC violation on `private_deque` |
| `chaos_gc_deque_scan` | §2 GC scanning a Chase-Lev buffer without thief registration |
| `chaos_grey_buf` | §3 grey-buffer realloc vs. drain race |
| `chaos_gc_alloc_sweep` | §7 / §15 stale `current_block` and block-recycle races |
| `chaos_concurrent_intern` | §4 intern table sweep under concurrent GC |
| `chaos_satb_deref` | §9 SATB barrier deref-mutate-GC stress |

**Three documents that should evolve together:**

- `GC_CONCURRENCY_DESIGN.md` — the *why* (design intent)
- `SYNCHRONIZATION.md` — the *what protects what*, per-field
- `AUDIT.md` (this file) — the *gaps between the two*, with fix status

Any change to runtime/GC shared state should update all three; struct
drift between `src/runtime.c`/`src/gc.c` and `src/jacl.h` is caught at
build time by `test/test_struct_sizes.c`.



Scope: GC, concurrency, runtime; phase D extended into compiler + VM.
Total non-test code is ~80K LoC (~62K hand-written after subtracting
the 16.7K generated `lib/unicode/unicode_tables.h`). This audit
focuses on the novel parts in depth — the GC/concurrency runtime
got full coverage; compiler/VM got three targeted surveys (see §D).

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

### 9. `gc_enumerate_roots` doesn't scan VM stacks; `gc_collect` does — **FIXED** (deletion barrier)
The design (Section 3) claims CPS captures all live state into task closures,
so concurrent GC doesn't need to scan VM stacks
(`src/runtime.c:822-829`). Single-threaded `gc_mark` does scan the VM stack
(`src/gc_collect.c:411-415`). The invariant is load-bearing.

**The audit (May 2026) traced this through the bytecode and found a real
soundness gap.** Initial framing was "no static enforcement"; the gap is
actually runtime, not compiler. Empirical reproduction is now
deterministic — see `test/test_runtime.c:test_write_barrier_satb_protects_held_value`
which exhibits the UAF pre-fix (V's GCHeader reads zeroed after sweep)
and passes post-fix. The full analysis follows.

**Fix landed**: `gc_write_barrier` (`src/gc.c`) now fires the SATB
deletion-side grey-buffer push **unconditionally** on any heap-valued
overwrite, regardless of `gc_active`. The insertion-side push remains
gated on `gc_active` (a new value stored before GC scanned the
container is already visible through the container). Cost: one grey-
buffer push per heap-typed atom mutation. The grey buffer resets at
each cycle boundary, so growth between cycles is bounded by mutation
rate within one cycle. Validated by:
- `test/test_runtime.c:test_write_barrier_satb_unconditional` — direct
  invariant: heap `old_val` always pushes, immediate `old_val` never,
  `new_val` only pushes when `gc_active=true`.
- `test/test_runtime.c:test_write_barrier_satb_protects_held_value` —
  end-to-end UAF reproducer running `gc_concurrent_collect` synchronously.
- `test/test_chaos_satb_deref.c` — TSAN stress test of the pattern under
  concurrent load.
- Full `--tsan` sweep (`runtime`, `m13`, `chaos_soak` 60s, all chaos tests)
  remains clean.

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

## Compiler / VM audit (Phase D, May 2026)

After all P0/P1 GC/concurrency items closed, the next-largest unaudited
surface is the bytecode compiler (`src/compiler.c`, 13.5K LoC) and
bytecode VM (`src/vm.c`, 11.7K LoC) — 25K of code, never audited at the
depth GC got. Phase D scope: enumerate hazards in three categories
(tag-check completeness on opcode handlers, stack discipline across
error paths, CPS capture correctness). One concrete fix landed; the
rest is documented for follow-up.

### D.0 The dispatch-loop convention

`vm__run` (`src/vm.c:2000`) is a `for (;;) { switch (instruction) }`
loop. Opcode handlers signal errors via:

- `result = vm__push(...); if (result != VM_OK) return result;` — bubble
  a `VM_STACK_OVERFLOW` etc. up.
- `vm__set_error(vm, ...); return VM_RUNTIME_ERROR;` — direct return for
  type/semantic errors.

Neither path resets `vm->stack_top` or frame state. The error
propagates to the caller of `vm__run` (typically `vm_exec`), which
reports the error and tears down. **For any opcode that pushes
intermediate values then errors, those values stay on the operand
stack.** This is the foundation of the §D.2 findings.

### D.1 Tag-check completeness — ~32 unchecked extractions

Survey of every `jacl_as_*` call in opcode handlers asks: was the
source tag verified (`jacl_is_*`) first? Result: ~32 unchecked vs ~8
checked, concentrated in three areas. None is reachable from
well-typed user code — the compiler/typer is supposed to guarantee
type-correctness before emitting the opcode. The risk is **layered
trust**: a compiler/typer bug compounds into a wild-pointer read in
the VM.

| Area | Opcodes | Source | Status |
|------|---------|--------|--------|
| State-machine fields | `OP_GET_STATE_FIELD{,_CELL,_WIDE}`, `OP_SET_STATE_FIELD{,_CELL,_WIDE}`, `OP_GET_RESUME_POINT`, `OP_SET_RESUME_POINT` (`vm.c:8165-8265`) | `frame->stack_base + 0` — compiler invariant: slot 0 of a state-machine function holds the SM | Trust the compiler. Add `assert(jacl_is_state_machine(...))` in debug builds. |
| Struct field stores | `OP_HEAP_RECORD_NEW`, `OP_STRUCT_NEW_INLINE`, `OP_STRUCT_SET_INLINE`, `OP_STRUCT_SET_UPVALUE` (`vm.c:6564-6910`) | Popped from stack; bytecode operand declares the type | Trust the typer. The opcode trusts the field-type operand, not the value's actual tag. Misemit → corruption. |
| Typed collections | `OP_TYPED_VEC_*`, `OP_TYPED_MAP_*` (`vm.c:10015-10920`) | Popped from stack | Trust the typer. Add `assert(jacl_is_typed_*(...))` in debug builds. |
| Stream / SM closure | `OP_STREAM_NEXT` reading `stream->state_machine` and `sm->sm_closure` (`vm.c:7493-7494`) | Field of a heap object | Trust struct invariants; field is populated only by the runtime. |

**Recommendation**: add a single `JACL_ASSERT_TAG(v, jacl_is_*)` macro
that compiles to a debug-build check (`assert`) and a release-build
no-op. Sprinkle at every unchecked extraction site. ~32 lines of
mechanical change. Catches compiler/typer regressions early without
slowing release builds. No correctness change on its own.

### D.2 Stack discipline — ~60 severity-(ii) and ~8 severity-(iii) leaks

Survey of every `return VM_RUNTIME_ERROR` (or equivalent) asks: is the
operand stack in a balanced state at that point? Result: most error
paths leak 1–3 slots; a handful in iterating opcodes can leak more.

**Severity classification**:
- **(i) Cosmetic**: error before any stack work; VM terminates anyway.
- **(ii) Slot leak per error**: pops without re-pushing, or partial
  pushes before erroring. Each error shrinks effective stack capacity.
  Bounded by 256-slot cap (`VM_STACK_MAX`, see §16); enough accumulated
  errors and even normal code starts hitting `VM_STACK_OVERFLOW`.
- **(iii) Mid-iteration leak**: an iterating opcode pushes args, then
  the *next* check fails (frame overflow, type error). Per-call leak
  is bounded by per-iteration push count (2–3 slots for `OP_TRANSFORM`,
  variable for `OP_SPREAD`).

**Categories** (representative, not exhaustive):

| Pattern | Sites | Per-error leak |
|---------|-------|----------------|
| Binary arith/compare pops both, then type-checks: `OP_MOD`, `OP_LT`, `OP_GT`, `OP_LE`, `OP_GE`, `OP_NEG` | `vm.c:2080-2231` | 1–2 slots |
| `OP_CALL` pre-frame-push arity / not-callable checks | `vm.c:2443-2575` | `arg_count + 1` slots |
| Destructuring pops source, then errors: `OP_DESTRUCTURE_VEC*`, `OP_DESTRUCTURE_NAMED*` | `vm.c:2902-3145` | 1 + partial slots |
| String ops pop, then type-check: `OP_CONCAT`, `OP_STR_LEN`, `OP_STR_INDEX`, `OP_STR_SLICE` | `vm.c:3146-3390` | 1–3 slots |
| Collection ops pop, then type-check: `OP_VEC_GET/SET/CONCAT/SLICE`, `OP_MAP_*` | `vm.c:3500-3840` | 1–3 slots |
| Mutation ops pop, then type-check: `OP_DEREF`, `OP_RESET`, `OP_SWAP` | `vm.c:5231-5400+` | 1–3 slots |
| Conversion ops pop, then type-check (`to-i32`, `to-i64`, …) | `vm.c:5765-5926` | 1 slot |
| **Iterating opcodes push args then frame-check**: `OP_EACH`, `OP_TRANSFORM`, `OP_FILTER`, `OP_PARALLEL`, `OP_RACE`, `OP_SPREAD` | `vm.c:4040-5191`, `7180-7300` | (iii): 2–3 slots per failed call |

**Recommendation**: two complementary fixes.

1. *Move "would-fail" checks before any pushes/pops.* For iterating
   opcodes, move the `frame_count >= VM_FRAMES_MAX` check above the arg
   pushes. Eliminates the (iii) category. Mechanical — one fix per
   iterating opcode. **One concrete fix landed in this audit pass**
   (`OP_TRANSFORM`, both vec and map paths) as a pattern demonstration.

2. *Either restructure error returns to reset stack_top, or add a
   `VM_ERROR(...)` macro that does so.* Eliminates the (ii) category.
   Touches ~60 sites; pure refactor, no semantics change. The macro
   approach:
   ```c
   #define VM_ERROR(vm, ...) do { \
       vm->stack_top = saved_stack_top; \
       vm__set_error(vm, __VA_ARGS__); \
       return VM_RUNTIME_ERROR; \
   } while (0)
   ```
   plus `uint32_t saved_stack_top = vm->stack_top;` at the top of each
   opcode case. Tedious, mechanical, low-risk.

Neither fix changes correctness for **well-behaved** programs that
never hit a runtime error. The class of bugs they close: cumulative
slot drift in programs that recover from errors (currently impossible
to do cleanly), and the obscure "your stack is corrupted, here's a
nonsensical type error from 50 ops downstream" symptom.

### D.3 CPS capture correctness

The CPS transform (`src/compiler.c:2209-3700`) is **closure + state-
machine based**, not pure CPS. Each suspending procedure compiles to a
state machine: a single closure with two synthetic params (`__sm`,
`__rv`), an upfront dispatch table that jumps to resume labels by ID,
and a state object containing every let-binding/parameter live across
suspensions. Variables are read/written via `OP_GET_STATE_FIELD` /
`OP_SET_STATE_FIELD` inline as assignments execute.

**The capture set is collected by `sm__walk_locals`
(`src/compiler.c:1577`)**, which recursively walks the body AST and
emits a state field for every `def`/`mut`/destructuring binding it
encounters. It handles destructured vectors, named destructuring, rest
patterns, typed bindings with struct widths, and recurses into all
expression positions.

**Liveness pruning** (`sm__optimize_state_layout`,
`src/compiler.c:2124`) — would remove variables whose `first_write <
last_read` doesn't cross a suspension — **is only enabled for
yield-only generators** (`src/compiler.c:2131-2136`). For
`await`/`parallel`/`race`, all bindings are kept conservatively:
> "Await/parallel/race have a diamond control flow (inline resolution
> vs resume path) that makes stack-local slot numbering inconsistent
> across the two paths."

This is the right tradeoff for soundness. The cost is a bigger state
object; the win is a much simpler invariant to verify.

**Potential hazard the survey flagged**: inner closures defined inside
a suspending function may capture outer locals into their own
upvalues. The capture happens at closure-creation time and the closure
holds the captured value (or a cell, for mutable bindings) directly.
After a suspension, if the inner closure is invoked, it accesses its
upvalues — not the state object. So the suspending function's state
object doesn't need to back the inner closure's upvalues *as long as
the inner closure's upvalues are direct value copies, not pointers
into the state object*. Worth a focused property test: define an inner
closure, suspend, run a GC, invoke the closure, verify it sees the
right values. None of the existing CPS tests in `test/jacl/cps_*.jacl`
hit this exact pattern.

**Recommendation**: add `test/jacl/cps_inner_closure_capture.jacl`
that exercises the inner-closure-survives-suspension case under GC
pressure. If it passes, document the invariant and close the
investigation. If it fails, dig deeper.

### D.4 What landed in this audit pass

Frame-check ordering fixed across all three iterating opcodes that had
the same vec/map iteration shape (`OP_EACH`, `OP_TRANSFORM`,
`OP_FILTER` — 6 sites total). The `vm->frame_count >= VM_FRAMES_MAX`
check now runs **before** the args-push, eliminating the
severity-(iii) 2–3-slot leak per failed call. Full normal-mode suite
unchanged (87 pass, 14 pre-existing HAMT/RRB failures unrelated).

`OP_PARALLEL`, `OP_RACE`, and `OP_SPREAD` have similar but
shape-different sites (more state to track per iteration); same fix
applies and is queued as follow-up.

The §D.3 test work also surfaced a new correctness bug — §D.6 below.

### D.6 New finding from §D.3 test work — `spawn` + deep recursion SEGV

While constructing the inner-closure-capture test, a separate crash
surfaced:

```jacl
proc burn {n} {
  if [== $n 0] { 1 } else {
    def g [vec 1 2 3 4 5 6 7 8 9 10]
    burn [- $n 1]
  }
}
proc main {} {
  def f [spawn { burn 70 }]
  def _ [await $f]
}
main
```

This SEGVs (single-threaded mode, default harness). Without `spawn`,
`burn 70` returns a clean `stack overflow` error at the
`VM_FRAMES_MAX=64` boundary. Inside `spawn`, the same depth crashes
before the boundary check fires.

ASAN trace (built with `-fsanitize=address`):

```
SEGV on unknown address 0x000000000043 — WRITE memory access.
#0 vm__run src/vm.c:8190  →  sm->fields[field_index] = value;
#1 vm__run src/vm.c:8559
#2 jacl_run src/vm.c:11704
```

`src/vm.c:8190` is inside `OP_SET_STATE_FIELD`:

```c
JaclVal state_val = vm->stack[frame->stack_base + 0];
JaclStateMachine *sm = jacl_as_state_machine(state_val);  // unchecked!
JaclVal value;
result = vm__pop(vm, &value);
if (result != VM_OK) return result;
sm->fields[field_index] = value;  // SEGV here
```

`state_val` is whatever's at `frame->stack_base + 0`. The unchecked
`jacl_as_state_machine` extraction (one of the 32 unchecked sites in
§D.1) masks the value to a pointer; if the value isn't a heap
state-machine, the resulting pointer is garbage. The SEGV address
`0x43` suggests an immediate-tagged JaclVal whose payload bits, masked
and dereferenced as a struct pointer, indexed past `fields[0]` to a
near-null address.

**Crash threshold**: bisects to N≈58 (without `spawn`, N=64). The
4-frame gap suggests the `spawn` body wrapper itself consumes ~4
frames before `burn` starts recursing.

**Bisection summary**:
| Setup | Threshold | Failure mode |
|-------|-----------|--------------|
| `burn N` at top level | N=64 | Clean `stack overflow` runtime error |
| `spawn { burn N }` | N≈58 | **SEGV** at `vm.c:8190` |

**Why this matters**: the §D.1 audit framed the unchecked tag
extractions as "trust the compiler — not reachable from well-typed user
code." This crash demonstrates the trust *is* breakable from valid
user code. Depth-N recursion is well-typed JACL. The class of bugs
just moved from "fragile but unreachable" to "reachable DoS/crash."

**Likely root cause**: the spawn body is compiled as an SM-wrapped
closure (since `spawn` is a suspension point in `main`). When `burn`
is called from inside it, `burn` is *not* SM-compiled (no
await/parallel/race/yield), but its call frame inherits some context
from the SM-wrapped caller. Somewhere along the recursive call chain,
a frame's `stack_base + 0` no longer points to a state machine, but
`OP_SET_STATE_FIELD` is still emitted (or executed by way of the SM
dispatch table at the top of the SM closure).

Two hypotheses to test:
1. The state-machine dispatch prelude at the head of an SM closure
   (the resume-point jump table) might re-execute on every recursive
   re-entry, reading slot 0 expecting an SM that isn't there at that
   depth.
2. `OP_SET_STATE_FIELD` is being emitted for variables defined in the
   spawn body, and when those variables happen to fall in a deeply-
   recursive frame's slot 0, the SM pointer is wrong.

**Severity**: high. Crashes from valid user code = bad. But probably
straightforward to fix once root-caused (add the `assert` from §D.1's
recommendation, find which compilation path emits the wrong opcode).

**Mitigation now**: don't `spawn` a body that recurses past ~50 deep
without a tail-call. (JACL has tail calls; the test recursion isn't
tail-recursive because `burn`'s `else` branch wraps the recursion in
`def g [...] burn ...`, which makes the `burn` call non-tail.)

**Next step**: dump the bytecode for the spawn body and burn body
(JACL probably has a `--dump-bc` or similar), confirm which opcode is
executing at SEGV time, find the originating compile site.

**Repro test**: `test/jacl/cps_inner_closure_capture.jacl` keeps `burn`
at depth 30 — well below the threshold — so the existing CPS
invariant test still passes. A dedicated reproducer for §D.6 would
either crash or assert; punt that until we have a clear fix.

### D.5 Next steps

Superseded by the "Punch list" at the top of this file — see
"Start-here for next session". §D.6 is item #1 (the open correctness
bug); the mechanical hardening items are #2–#4. Kept here as a
pointer so search-for-§D.5 reaches the right place.

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
