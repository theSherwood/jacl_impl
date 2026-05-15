# JACL Codebase Audit — Historical Record

> **This file is the read-only archive of the May 2026 audit campaign.**
> For currently-open items see `AUDIT.md`. This file preserves the
> full worked-through backlog: every closed correctness fix, the
> phase-by-phase status table, profiling baselines, methodology notes,
> and the deep-dive write-ups that informed the design decisions.
>
> Snapshot taken 2026-05-14 when the campaign's open queue collapsed
> to non-correctness code-health items. Do not modify entries here —
> if a closed item gets reopened, copy the relevant section back into
> `AUDIT.md` as an active issue.
>
> **Companion docs**: `GC_CONCURRENCY_DESIGN.md` (design intent),
> `SYNCHRONIZATION.md` (per-field reference for shared runtime/GC
> state).

## If you're picking this up later

This audit was written over several sessions in May 2026. All eight
GC/concurrency correctness items in "Critical correctness issues" are
fixed, plus five soak-surfaced fixes, plus the §9 SATB-barrier UAF.
Phase D (compiler/VM) surveyed three categories, landed a frame-check
ordering fix, opened §D.6 (spawn + deep recursion SEGV), and §D.6 is
now fixed too. **2026-05-13 perf-bench surfaced three new bugs in
GC/concurrency, all SEGV-class — all FIXED: stack-closure underflow
in single-thread mark (b521b1d), sweep-walker overshoot in all five
walkers (b521b1d), and `OBJ_CLOSURE=0` garbage dispatch in the
concurrent mark loop (bc74a10). The N≥500 hang (#0c) is also now
FIXED: completion future held only by a raw C pointer was racing
with concurrent sweep; new `runtime_pin_value`/`runtime_unpin_value`
API roots external JaclVals across GC cycles. The §10/§11 CV change
is also FIXED, along with the two GC races that blocked it: race #1
(unbarriered `f->waiters = NULL` detach in `jacl_future_resolve` —
chain head's continuations lost reachability), and race #2
(unbarriered `w->continuation` write in `jacl_future_add_waiter` —
fresh waiter watermark-protected but its OLD continuation has no
mark path). Both have deterministic regression tests. CV change
applied (signal-one on push_inbox/push_pinned, 1ms-timeout park in
worker idle path). gc_stress_atom × 30 went from 27/30 crashes to
0/30 once race #2 was closed.**

**2026-05-13 TSAN-baseline triage pass surfaced four more real
bugs** that had been blanket-labeled "pre-existing TSAN failures"
without root-causing. All FIXED:

- **`realpath` buffer too small (compiler)** — 11 sites across
  `src/compiler.c`, `test/test_compiler.c`, `test/test_jacl_harness.c`
  passed a `char[1024]` destination to `realpath`. POSIX requires
  `PATH_MAX` (4096 on Linux). glibc fortify (active under `-O1` with
  Ubuntu's default `_FORTIFY_SOURCE=2`, which TSAN mode enables but
  normal `-O0` mode does not) aborts unconditionally on the
  compile-time-known size mismatch. Latent in production: real
  paths fit in 1024, but any path between 1024 and PATH_MAX would
  have silently overflowed. Commit `c7dea0d`.

- **`syntax_from_ast` leaked vec data into doomed macro_ctx heap
  (syntax)** — the function takes an explicit `heap` parameter but
  its body uses `jacl_vec_empty` / `jacl_vec_push_back` / etc.,
  which allocate via `gc__current_heap` TLS. When `jacl_eval`
  created a transient `macro_ctx` via `jacl_ctx_new` — whose
  `vm_init` side-effects TLS to the new heap — quote-constant vec
  data landed in macro_ctx's heap. `jacl_ctx_destroy(macro_ctx)`
  freed those blocks, leaving the compiled chunk with a constant
  pointing into freed memory. Normally invisible (freed bits still
  readable); TSAN catches the free→read sequence exactly. Fix:
  public wrapper pins TLS to the explicit `heap` arg for its scope.
  Commit `491ad0d`.

- **`jacl_run`'s `JaclInternTable` was a stack-local (integration)** —
  declared on the stack, initialized (including a mutex), pointer
  stored in `vm->intern_table`, then `intern_table_destroy` (which
  `MUTEX_DESTROY`s the mutex) called at return. Callers that read
  `vm.intern_table` post-return (e.g., to intern a key for a map
  the run produced — exactly what `test_job_returns_map_with_pid`
  does) dereferenced a doubly-dangling pointer. Silent in normal
  mode (stack memory still readable, glibc's pthread_mutex_lock
  doesn't validate destruction). Fix: allocate intern_table in the
  arena; drop the redundant destroy. Commit `7469a98`.

- **Cluster of GC/worker races on ctx state (jacl_harness, partial)**
  — five distinct shapes, all real C11 races:
  - `gc_sweep_intern_table` walked `heap->blocks` without
    `blocks_mutex` when `watermark==0`, on the false assumption
    "watermark==0 ⇒ single-threaded." In concurrent mode the first
    GC cycle has watermark 0 (workers at thread_epoch=0), and the
    gate skipped the lock exactly when a worker might be
    head-inserting a block.
  - `vm.ctx` (current implicit-context register), `vm.saved_ctx[]`
    (with-ctx nesting stack), and `ctx_pool_free`'s NIL-out of
    freed-ctx reference fields were all plain stores racing with
    plain reads in `gc_enumerate_roots` / `gc__trace_object`.
    RELEASE-store + ACQUIRE-load pairing applied to each.

  Commit `2465a10`. `jacl_harness` still has remaining TSAN races
  on heap-pointer slot writes paired with `gc_write_barrier` (e.g.
  `sm->fields[i] = value` at vm.c:8305) — these are correct under
  the audit's SATB+watermark barrier discipline, but TSAN's
  per-location vector clocks don't see barrier-mediated happens-
  before (same shape AUDIT §S5 flagged for Chase-Lev). Out of
  scope for the triage pass; would need a systematic
  atomic-store/load conversion of every heap-pointer slot access
  across the runtime.

### Status at end of phase D (2026-05-12) + TSAN triage (2026-05-13)

| Phase | Scope | Outcome |
|-------|-------|---------|
| A | GC/concurrency design audit | 6 P0 fixes, 60s TSAN-clean soak |
| B | Soak-surfaced races | 5 additional fixes, TSAN-validated |
| C | Remaining P1s (§4, §6) | Intern sweep + adaptive threshold fixed |
| C+ | §9 deep-dive | Real UAF in SATB barrier; fixed unconditionally |
| D | Compiler/VM audit | 3 surveys; frame-check ordering fix (8 iterating opcodes); §D.6 opened and fixed (spawn + deep recursion SEGV); recursive `vm__run` follow-up audit clean (0 §D.6 siblings); §D.1 `JACL_ASSERT_TAG` pass (56 asserts) caught a typer bug in `map-keys`; §D.2 `VM_ERROR` stack-discipline pass (72 error returns across 39 opcodes) |
| E | TSAN-baseline triage | Audit had dismissed 8 TSAN failures as "pre-existing flakes" without triaging; 4 turned out to be real bugs (PATH_MAX, syntax TLS, jacl_run stack-local intern_table, ctx-state race cluster) — see commits `c7dea0d` / `491ad0d` / `7469a98` / `2465a10`. TSAN baseline now 83 pass / 5 fail. |
| E+ | TSAN-baseline cleanup | Closed 2 of the 5 remaining failures (`rwlock`, `cross_thread_string`) and reclassified `perf` as a `jacl_harness` sibling once the WorkerStats counter race got fixed. Surfaced + fixed a real bug in `sum_tree.h` (per-instantiation handler state needed `JACL_THREAD_LOCAL`). TSAN baseline now 85 pass / 3 fail. |
| E++ | ctx-pool elimination (§18) | Surfaced via `perf`: the ctx pool's lockless free-list reuse can recycle slots that SM state fields still reference across worker boundaries. Real UAF; not a §S5 missing-atomics issue. Eliminated the pool entirely (option 1) — `ctx_pool_alloc` → direct `gc_alloc`, `ctx_pool_free` → no-op. Closes the race; bench is *faster* (collection_churn -20%, spawn_chain -13%). Closed `perf` together with the §S5 slice 1 (`vm->env`) and a `GcStats` cycle-counter atomic fix. TSAN baseline now 86 pass / 2 fail. |
| E+++ | TSAN baseline triage (2026-05-14) | Triaged the 2 remaining failures (`jacl_harness`, `chase_lev_stress`) rather than fixing. Walked the actual TSAN output for both. `jacl_harness`: **1 distinct site** — `sm->fields[i] = value` at `vm.c:8300` paired with the GC trace at `gc_collect.c:303`. The `gc_write_barrier` two lines before the write maintains SATB correctness; TSAN can't see the happens-before because it runs through grey-buffer count atomics, not the slot itself. `chase_lev_stress`: **2 distinct sites** — `chase_lev.h:268` (fence-mediated data store, the test doesn't opt into `CHASE_LEV_ATOMIC_DATA`) and `test_helpers.h:86` (epoch-protected `free`; TSAN can't track happens-before through the epoch wait loop). Both documented as known-and-safe. Coverage of the runtime's actual chase_lev usage is `chaos_pinned_deque`/`chaos_gc_deque_scan` (both TSAN-clean); coverage of the heap-pointer-slot write discipline is the unified-barrier regression tests + 250-run gc_stress soak. The §S5 atomic-conversion pass remains available but is declined: only 1 of the named sites surfaces under current workloads, and the conversion would introduce an ongoing maintenance footgun (every new heap-pointer slot write must route through the atomic primitive or TSAN cleanliness silently regresses). TSAN baseline 86 pass / 2 fail, both known-and-safe. |
| F | Rope-summary incremental combine (2026-05-14) | The post-tier-2 profile put `unicode_grapheme_next` at 19% of `string_concat` CPU — biggest non-GC sink, unmasked by tier-2's alloc-path wins. Root cause: `sum_tree.h`'s `ST_MK_LEAF` re-runs `summarize(elems, count)` over the full merged byte buffer every time `ST_CONCAT` merges two leaves, even though both child summaries are already cached on the leaves and the rope's invariants (codepoint-aligned splits, junctions made grapheme-safe by `rope_concat`) make summary monoidal across the merge. Added `ST_MK_LEAF_SUMMARIZED(elems, count, summary)` that accepts a precomputed summary; the three leaf-merge sites in `ST_CONCAT` (same-height, height-left>right, height-right>left) now pass `combine(ll->summary, rl->summary)` instead of forcing a full re-scan. Wins (BENCH_TIMED_ITERS=200, single run): `string_concat` wall_median **-42.3%**, `collection_churn` **-41.1%**, `spawn_chain` **-33.3%**, `box_churn` +2% (noise). Profile confirms: `unicode_grapheme_next` 19% → **2.5%**, `rope_summary_summarize` out of the top 18 entirely. Full normal-mode suite 88/88, 5×30 gc_stress soak clean, 30s TSAN chaos_soak clean (153K tasks), TSAN baseline unchanged. Commit `f43d67f`. |
| G | FNV-1a hash extend in OP_CONCAT (2026-05-14) | Once phase F removed the rope-summary cost, `rope_string__compute_hash` surfaced as the next sink (8% of `string_concat`). Same shape of fix: FNV-1a is a left-fold over bytes, so `hash(a ++ b) == continue_FNV(hash(a), b's bytes)`. Every string-tagged variant (inline / heap / rope) already exposes `hash(a)` via `jacl_val_hash`. Added `rope_string__hash_extend(prev_hash, r)` (`compute_hash` now delegates with `prev_hash = FNV_INIT`); `OP_CONCAT`'s large-concat path uses `hash_extend(jacl_val_hash(a), rb)` instead of `compute_hash(rc)`. For append-loop workloads (a grows, b is tiny) per-concat hash work goes from O(\|a\|+\|b\|) to O(\|b\|). Magnitude: smaller than phase F — the post-rope-fix profile of `string_concat` had `rope_string__compute_hash` at 8% of CPU (~0.55s of a 6.5s run), and gprofng on these short scenarios has very high run-to-run variance (saw 3.5s → 12s spread across 5 samples at the same SHA, same args). `rope_string__compute_hash` drops out of the top 15 post-fix; bench wall_median diff at 200 iters showed -13% on `string_concat` (within single-run noise but directionally right). The other `compute_hash` callers (`jacl_rope_string_create`, the slice path at `vm.c:3433`) don't have a prefix hash to extend from and still hash from scratch. Suite 88/88, gc_stress 5×30 clean, 30s TSAN chaos_soak clean (72K tasks), TSAN baseline unchanged. Commit `ea77e52`. |
| H | §14 max_free_run_lines + sweep dedup (2026-05-14) | The tier-3 redesign mentioned in punch list #1. Instrumented gc_alloc's slow path first (Phase A counters: `blocks_scanned` + `lines_scanned` + later `blocks_rejected_fast` on `ThreadHeap` and `JaclPerfSnapshot`, BENCH_TIMED_ITERS=2000) before designing. Data showed each slow-path call visits **230–370 blocks**, scanning **35–180 lines/block** post-tier-2. The outer block-walk dominates. Phase D first: dedupe the post-sweep block summary (find first_free + verify all_free) into one helper `gc__summarize_block_map`, replacing three near-identical epilogues in `gc_sweep` / `gc_sweep_minor` / `gc_sweep_concurrent`. -12 LoC net, zero behavior change, sets up Phase B to land the new field in one place. Phase B: add `uint16_t max_free_run_lines` to `GCBlock` (next to `first_free_line`). Sweep writes authoritatively in the existing summary pass; `gc_alloc`'s outer walk does `if (block->max_free_run_lines < needed_lines) continue;` as O(1) reject before calling `gc__find_fit_in_block`; on a miss inside the block, the run walker tightens the cached bound to the authoritative max seen. Wins on BENCH_TIMED_ITERS=2000: lines_scanned **-98.8%** on collection_churn (694.7M → 8.5M), **-94.4%** on string_concat (758.7M → 42.5M), **-98.7%** on box_churn (66.6M → 0.8M), **-94.6%** on spawn_chain. Wall_median: collection_churn -25%, string_concat **-59%**, box_churn -16% (single-run, directional). Outer-walk visits are unchanged by design; ~97-99% of visits are now O(1)-rejected. The remaining ~500 lines/admitted-block (distance from `first_free_line` to the chosen run on admitted blocks) is the next lever if more is needed — Phase C territory. Full suite 88/88, 30x gc_stress_atom clean, TSAN baseline unchanged (86/2), 30s TSAN chaos_soak clean (73883 tasks). Commits `51c9c3a` / `4d0858b` / `4059961`. |

Eight P0/P1 items in "Critical correctness issues" fixed; five
soak-surfaced issues fixed; §9 SATB-barrier UAF fixed; §D.1/D.2/D.3
surveyed; §D.6 opened during the §D.3 test work and now fixed. Four
additional real bugs found by phase-E TSAN triage. **§14 now FIXED
end-to-end via phase H** (per-block `max_free_run_lines` O(1) reject,
2026-05-14) — replaces the reverted tier-3-as-intrusive-list attempt
and closes the perf item that was driving the punch list. See git log
for the per-commit story.

### Start-here for next session

No open correctness bugs. The §10/§11 CV change is landed along
with the two GC races that blocked it AND the five sibling
§9-class holes (direct heap-ptr writes on GC-managed objects).
The phase-E TSAN triage closed four additional real bugs that had
been masked as "pre-existing flakes." gc_stress_atom × 50 is clean
with CV applied. The write-barrier discipline is uniform: any
heap-pointer slot write on any GC-managed object goes through
`gc_write_barrier`, unconditional on both deletion and insertion
sides.

**TSAN baseline (2 remaining failures, both documented as known-and-safe)**:

A second pass (2026-05-14) closed two of the five originally-listed
failures and clarified that two of the remaining three are the same
underlying issue as `jacl_harness`. A third pass (2026-05-14, later)
triaged the two stragglers — `jacl_harness` and `chase_lev_stress` —
and documented both as known-and-safe rather than open. Both are
TSAN-blindness on barrier/fence/epoch-mediated synchronization, not
missed barriers; see the table below for the per-test triage.
Surprises from the second pass:

- `cross_thread_string` was framed as a missing release/acquire on the
  `SharedSlot` publication signal. Fixing that exposed a deeper race
  on `rope_st_handler_state` — a `static` global in `sum_tree.h` that
  every rope op writes (with idempotent handler values) at the start
  of each call. Fix: mark it `JACL_THREAD_LOCAL`. The global was a
  same-class bug as the §17 thread-local-convention discussion. Both
  layers had to land for the test to go green.

- `perf` was framed as "perf counters are intentionally
  unsynchronized." That was true of the `WorkerStats` fields the
  audit named at runtime.c:1299/385, and atomizing them was a clean
  ~15-line fix (new `stats_add_u64` helper + atomic loads in
  `jacl_perf_snapshot`). But once that race went away, TSAN tripped
  on `gc_enumerate_roots` reading `vm->env`'s `vm__env_set`-written
  slot — the exact `jacl_harness` shape. So `perf` is no longer its
  own thing; it's a `jacl_harness` sibling.

| Test | Status | Why it fails (after 2026-05-14 pass) | Action |
|------|--------|--------------------------------------|--------|
| ~~`rwlock`~~ | **FIXED** (2026-05-14) | `g_state.max_concurrent` updated under read lock by two readers — read locks don't serialize mutations. | CAS-loop atomic peak update. |
| ~~`cross_thread_string`~~ | **FIXED** (2026-05-14) | (a) `SharedSlot.ready` was the publication signal but plain-read/written. (b) `sum_tree.h` per-instantiation `ST_HANDLER_STATE` was a non-TLS global written by every rope op. | (a) Atomic load/store explicit with release/acquire. (b) `JACL_THREAD_LOCAL` on the handler-state declaration. |
| `chase_lev_stress` | known, triaged 2026-05-14 | 38 warnings across 2 distinct sites, both fence/epoch-mediated. (a) `chase_lev.h:268` — plain data store, synchronized via the release-store on `bottom` two lines later; the test doesn't `#define CHASE_LEV_ATOMIC_DATA` (the TSAN-friendly mode `src/runtime.c` opts into). (b) `test_helpers.h:86` — `free()` of a retired buffer that a thief read earlier, safe by the epoch protocol; TSAN can't see happens-before through the epoch wait loop. | Documented as known-and-safe. Real coverage of the runtime's actual usage is `chaos_pinned_deque` + `chaos_gc_deque_scan` (both TSAN-clean). Even setting `CHASE_LEV_ATOMIC_DATA` in the test would only close site (a); site (b) is fundamentally TSAN-invisible. Porting the runtime's wrapper into the standalone test would duplicate runtime logic in tests — declined. |
| ~~`perf`~~ | **FIXED** (2026-05-14) | (a) `WorkerStats` counter race — atomic load/store. (b) `gc_enumerate_roots` reads `vm->env` slots — §S5 slice 1. (c) ctx-pool-vs-SM-state UAF — pool eliminated entirely (§18 option 1). (d) `GcStats` cycle-counter race — atomic load/store, same pattern as WorkerStats. |
| `jacl_harness` | known, triaged 2026-05-14 | Triage run (`TSAN_OPTIONS=halt_on_error=0`) surfaced **1 distinct race site** across the entire suite: write at `vm.c:8300` (`sm->fields[field_index] = value` in `OP_SET_STATE_FIELD`) vs read at `gc_collect.c:303` (the OBJ_STATE_MACHINE trace). Two lines before the write (`vm.c:8298`), `gc_write_barrier` already fires the SATB deletion push, so the GC always sees either the old value (via grey buffer) or the new value (via the slot). The happens-before runs through the grey-buffer count atomics, not through `sm->fields[i]` directly — TSAN's per-location vector clocks can't observe it. Other audit-named sites (`cl->upvalues`, stream fields, `vm->env`) share the shape but didn't surface in this workload. No missed §9 sibling. | Documented as known-and-safe. The §S5 atomic-conversion pass (convert every heap-pointer slot write/read to `ATOMIC_STORE_EXPLICIT`/`ATOMIC_LOAD_EXPLICIT` with RELEASE/ACQUIRE) would silence it but is declined for now: only 1 of the named sites actually surfaces, the conversion has near-zero runtime impact but introduces an ongoing maintenance footgun (every new heap-pointer slot write must use the atomic primitive or TSAN cleanliness silently regresses). Revisit if a future Phase-E-style triage finds bugs masked here, or if a new site does surface. |

Both remaining failures are **TSAN-blindness on barrier/fence/epoch-
mediated synchronization**, not missed barriers. Neither blocks
shipping. The 2026-05-14 triage walked the actual TSAN output for
both — see commit history for the run details. The `jacl_harness`
report came from a single bytecode site whose two-line barrier
context is in the source; the `chase_lev_stress` reports are split
between the well-known fence-store pattern and the epoch-protected
reclamation pattern, both intentionally TSAN-invisible in the
primitive.

**Known theoretical hole, not surfacing as a bug today**: §9
recommendation #2 (capture mutable-load values into a per-worker
"stack root set" scanned by `gc_enumerate_roots`) is **still
open** — see §9e below. The write-side races are all closed, but
the *read-side* operand-stack-rooting gap remains: a worker can
hold a derived heap value on the operand stack across a GC moment
with no other root, relying on epoch protection or the value's
container still pointing to it. JACL's CPS compiler de facto
avoids this pattern (heap values live across suspensions only via
SM state fields, which the GC scans), but it's a convention, not
a runtime invariant. No concrete UAF has been observed; revisit
if one ever does, or if JACL grows beyond CPS-only concurrency.

(#0c fixed 2026-05-13: external roots API.)
The §D.6 SEGV from `spawn { burn 70 }` is
fixed by making `OP_SPAWN`'s single-threaded path reset
`vm->frame_count` and `vm->stack_top` after the inner `vm__run`
returns, mirroring what `OP_PARALLEL` already does. `OP_RACE` had
the same bug pattern and was fixed in the same pass. Regression test:
`test/jacl/cps_spawn_deep_recursion.jacl`.

The §D.6-cluster follow-up audit (formerly punch-list item #4) is
also closed: every recursive `vm__run` call site was classified.
Of 28 sites, 19 propagate errors immediately (SAFE), 9 continue
past error and explicitly reset both `frame_count` and `stack_top`
(FIXED, including the §D.6 patch for `OP_SPAWN`/`OP_RACE`), and 0
exhibit the §D.6 shape. No siblings exist.

The §D.2 frame-check reorderings (formerly item #2) are also
closed for `OP_PARALLEL`, `OP_RACE`, and `OP_SPREAD` (5 sites in
total — SM + non-SM branches for parallel/race, plus the stream-
generator path in spread). Same pattern as `c39ee12`. Suite
unchanged: 87 pass, 14 pre-existing HAMT/RRB. TSAN unchanged
relative to baseline (the §D.2 fix is pure single-thread ordering).

§D.1 `JACL_ASSERT_TAG` macro is also landed. 56 asserts cover all
four categories: 8 SM-field opcodes, 16 struct-field-store primitive
extractions, 26 typed-collection pointer extractions, 6 stream/SM
closure dereferences. The macro compiles to `assert(pred(v))` in
debug builds, no-op under `-DNDEBUG`. The pass surfaced two real
bugs and one piece of brittle test infrastructure:

- **Typer bug (now fixed)**: `(HEAD_MAP_KEYS, TYPE_TYPED_MAP)` was
  always inferred as `TYPE_TYPED_VEC`, even when the map's keys
  were dyn (`inferred_key_struct_idx == UINT32_MAX`). The runtime
  emit/consume in `OP_TYPED_MAP_KEYS` had always switched on
  `key_type_idx == 0xFFFF` correctly, so the inconsistency had been
  silent — `vec-len` against the dyn-keys output worked only because
  `jacl_typed_vec_count` happens to share its count-field offset
  with `jacl_vec_count`. Fixed in `typer.c` to emit `TYPE_VEC` for
  the dyn-keys branch; assert now passes.
- **Test bugs (now fixed)**: `test_struct_new_inline_opcode`,
  `test_struct_new_inline_wide`, and `test_struct_new_inline_padding_zeroed`
  in `test/test_compiler.c` hand-emitted bytecode using
  `type_idx=1` (the reserved ctx slot) where they meant the first
  user struct (`type_idx=2`), and pushed `jacl_i32(1)` into a
  bool field. Both worked pre-assert because the runtime defaults
  to an 8-byte raw memcpy for non-primitive fields and
  `jacl_as_bool` returns payload bit 0. Fixed.

The §D.2 broader stack-discipline cleanup is also landed. A
`VM_ERROR(vm, ...)` macro now resets `vm->stack_top` to a per-opcode
`saved_stack_top` snapshot, sets the error message, and returns
`VM_RUNTIME_ERROR` in one shot. 72 error returns across 39 opcodes
were converted (Categories §D.2: binary arith/compare, destructuring,
string ops, vector/map ops, mutation ops, conversion ops). The pass
also picked up sibling sites the original audit didn't list
(`OP_VEC_LEN`/`OP_VEC_PUSH`/`OP_MAP_HAS`/`OP_MAP_LEN`/`OP_MAP_REMOVE`/
`OP_MAP_KEYS`/`OP_MAP_VALS`/`OP_STR_BYTE_LEN`). Cumulative slot drift
across error returns is now closed for these opcodes; if/when error
recovery lands, the operand stack will be balanced.

The remaining work is non-correctness, performance/architecture:

### Punch list (in priority)

0a. ~~SEGV under sustained allocation pressure via jacl_harness (single-thread path)~~
   **FIXED** in `b521b1d`. Root causes were two real bugs in
   `src/gc_collect.c`:

   - `gc_mark` / `gc_mark_minor` pushed every `vm->frames[i].closure`
     to the mark stack unconditionally, but `vm_exec` and `embed.c`
     synthesize stack-allocated frame closures for top-level chunks.
     `gc_header_of(cl)` then read 4 bytes BEFORE the stack variable
     under any alloc-pressure GC. Fix: skip closures that aren't in
     the heap (their chunk constants are already enumerated separately
     and they carry no upvalues).
   - All five sweep walkers (`gc_sweep`, both `gc_mark_minor` walks,
     and both phases of `gc_sweep_concurrent`) used line-skip on
     `total==0`. That breaks when a live object straddles a line
     boundary with a zeroed dead object immediately before it — the
     walker leaps past the live object's header into its payload,
     reads garbage `alloc_total`, and `memset`s past the block in
     `gc_sweep_concurrent` Phase 1. Fix: advance ptr by 8 (the
     alignment) on zero, not by line.

0b. ~~SEGV in `gc__trace_object` on garbage `OBJ_CLOSURE` dispatch~~
   **FIXED** in `bc74a10`. The mark loop now skips any header whose
   `alloc_total == 0`. Root cause: `OBJ_CLOSURE = 0` in the
   `GCObjType` enum, so any zero-byte at the obj_type offset
   dispatches to the closure case. A header reads zero either from
   a mid-init publication race (gc__bump_alloc writes `alloc_total`
   LAST) or a post-sweep stale reference. Either way, dispatching as
   closure and reading `cl->upvalues[i]` from garbage SEGVs. The
   guard is correct because a valid live object always has
   `alloc_total > 0`.

   Earlier theory ("non-CPS loses VM-stack roots") was wrong: epoch
   protection DOES cover allocations during a task — the worker's
   `gc__thread_epoch` is set at task pickup and stays constant for
   the task, and the GC watermark is `min(thread_epochs)` so new
   allocs always satisfy `epoch >= watermark`. So intermediate
   values during HAMT path-copy etc. are correctly kept alive even
   without being on the mark stack.

   Stress on test_perf at N=500: 14/20 SEGV → 0/50 SEGV. Combined
   with #0a's walker fixes.

0c. ~~Rare hang under sustained allocation pressure at N≥500.~~
   **FIXED**. Root cause: the completion future allocated by
   `rt_run_to_completion` / `test_perf` lives on the GC heap but is
   held by a raw C pointer that the GC does not scan. While the spawn
   task is in flight, the future is rooted via `task->gc_root`. Once
   the task resolves the future and retires, the future is unreachable
   from any GC root. A concurrent GC after retire then sweeps the
   future block and `memset`s its memory — `state` reads back as
   `FUTURE_PENDING = 0`, `result = NIL`, `waiters = NULL`. The polling
   thread sees PENDING forever and the process hangs.

   Reproduction was deterministic with `PERF_DEBUG_HANG=1`: the
   future's `GCHeader` came back all-zero (`epoch=0 mark=0 gen=0
   obj_type=0 alloc_total=0`) — the block had been swept under us.

   Fix: added a `runtime_pin_value` / `runtime_unpin_value` API on
   `Runtime`. A pinned `JaclVal` is held in a mutex-protected array
   and scanned by `gc_enumerate_roots`. `rt_run_to_completion` and
   `test_perf.c` now pin the completion future across the polling
   window. Validated: 0/50 hangs at N=500 (was 12% pre-fix), full
   normal-mode suite 88 pass, TSAN regression set unchanged (9 failures
   pre-existing in non-chaos tests, no new races). The same API will
   be the right answer for any future C-side embedder that needs to
   observe a JaclVal across GC cycles.

1. **Architectural items §10–§17** — throughput, latency, ergonomics.
   Not correctness. Profile baseline captured 2026-05-13; see
   "Profiling baseline" section below for which items the data
   actually pushes to the top of the queue. **§10 (single global
   inbox) is now FIXED (2026-05-13)** — from-worker submissions
   bypass the global mutex by pushing directly to own `public_deque`,
   eliminating 98% of inbox-mutex traffic on `spawn_chain` and
   activating work-stealing (0 → 1873 steal_successes). Wall delta
   on the bench is modest (~10%) because `spawn_chain` is half pinned
   tasks (already off the global mutex) and the remaining ceiling is
   the 1ms CV wake-up timeout. **§14 tier-1 + tier-2** are now both
   landed (tier-2 in 2026-05-14: per-block `first_free_line` hint —
   wall_median -16.9% to -59.1% on alloc-heavy scenarios, stacked on
   top of tier-1's wins). **§14 tier-3 attempted and reverted
   (2026-05-14)** — intrusive free-run list designed exactly as the
   audit's tier-3 sketch (head pointer on ThreadHeap, nodes written
   into the start of each free run, sweep rebuilds the list each
   cycle, allocator slow path pops first-fit before the line-map
   scan). Bench regression of +21% to +650% wall_median, driven by
   three interactions the audit's sketch didn't surface:
   (1) **Walker invariant**: sweep's Phase 1 walker reads
       `alloc_total` from every 8-byte boundary in dead-zone bytes,
       expecting zero so it can advance 8. Intrusive `GCFreeRun`
       nodes put non-zero bytes there; `alloc_total` at byte offsets
       6/14/22 only reads zero by canonical-pointer luck on x86_64,
       and 4 bytes of struct padding aren't zeroed at all. Walker
       mis-advances into garbage and rescan-loops, which inflated
       `gc_total_ns` 8x on string_concat.
   (2) **search_block locality**: tier-1's `search_block` anchor
       keeps a worker allocating from one block until exhausted
       (maximizing in-block locality); pop_fit picks an arbitrary
       run from any block (LIFO from sweep order), forcing block-
       bouncing. `current_heap_blocks` grew 14–23% on alloc-heavy
       scenarios — more blocks, more sweep work.
   (3) **List growth**: pop_fit is O(runs) under `blocks_mutex`,
       and sweep pushed every free run with no size threshold. On
       fragmented heaps (~1500 blocks in string_concat) the list
       walk dominated slow-path latency.
   Conclusion: tier-3-as-intrusive-list is more invasive than the audit
   framed. **Reverted**; pivoted to rope-summary perf (phase F row),
   FNV-1a hash extend (phase G row), and then to a different tier-3-
   class fix: **per-block `max_free_run_lines` O(1) reject (phase H row,
   2026-05-14)**. Same problem, no intrusive nodes, no heap-level list.
   Result: `lines_scanned` -94 to -99% across alloc-heavy scenarios,
   ~97-99% of slow-path block visits now O(1)-rejected. §14 is now
   marked FIXED. The intrusive-list sketch in this item is preserved
   for posterity — it documents why the obvious tier-3 design didn't
   pan out and informed Phase B's design choices. §13 (computed-goto)
   remains the next lever for the dispatch-bound regime — `vm__run` is
   **61% of `box_churn`** post-F (up from 52% pre-F as a share of a
   smaller scenario total); WASM portability story (`&&label` is
   GCC/Clang-only) is the main scope question.

### Housekeeping (2026-05-13)

Two test-hygiene items, neither affects runtime correctness:

- **Dead RC-mode paths removed from `lib/hamt/hamt.h` and
  `lib/rrb_vec/rrb_vec.h`.** Both headers had a long-stale
  `#ifndef *_GC_MODE` branch that still `#include "../rc/rc.h"`, but
  `lib/rc/` was deleted back in `1ed778a` ("never wired in"). Every
  production caller (`src/collections.c`, `src/jacl.h`) defines
  `*_GC_MODE`, so the RC fallback only existed for 14 standalone
  template tests that hadn't compiled since the rc removal. Deleted
  the RC branches (the transient API + RC node destructors) and the
  14 orphan tests; headers now hard-require `*_GC_ALLOC` via `#error`.
  ~11k LOC removed, no production change. Commit `8dc407f`.
- **`test_chaos_gc_deque_scan` deflaked.** Two stacked races caused
  ~7/30 failures under `--parallel`, ~1/30 in isolation: (i) the
  scanner loop checked `owner_done` at the top, so a fast owner that
  finished before the scanner got CPU caused the scanner to exit
  before its first observation; (ii) even after fixing that, the
  scanner's 5000-round atomic loop completes in microseconds while
  the owner is still alloc/grow-bound, so all rounds could see
  `b=t=0` and `sum` stayed zero. Fix: drop the unused `owner_done`
  flag, always run `SCAN_ITERS` rounds, and add a second sync point
  — wait for `dq->bottom > 0` before starting the scan loop.
  Validated 0/100 isolated, 0/30 `--parallel`. Commit `d39314b`.

§D.3 is closed: `test/jacl/cps_inner_closure_capture.jacl` verifies
that inner closures defined before a suspension still see their
captured values after. Passes.

### Profiling baseline (2026-05-13)

`test/test_perf.c` now runs four scenarios (`collection_churn`,
`spawn_chain`, `box_churn`, `string_concat`). The bench harness
accepts `BENCH_TIMED_ITERS` / `BENCH_WARMUP_ITERS` / `BENCH_SCENARIOS`
env vars to amplify runtime for profilers. Raw text reports are in
`docs/profiles/2026-05-13_*.txt`; this section is the summary.

Captured via `gprofng collect app -p hi` on an `-O2 -g
-fno-omit-frame-pointer` build (no TSAN). 2 workers. Numbers are
exclusive CPU time as % of scenario total.

| Function | mixed | collection_churn | box_churn | spawn_chain | Audit item |
|----------|------:|-----------------:|----------:|------------:|------------|
| `gc__find_fit_in_block` | **51%** | **58%** | **21%** | <1% | **§14** |
| `runtime__worker_loop` (excl) | 13% | — | — | **97%** | **§11 / §10** |
| `unicode_grapheme_next` | 8% | — | — | — | (string-specific) |
| `gc_sweep_concurrent` | 7% | 15% | 8% | <1% | (sweep cost) |
| `vm__run` | 5% | 10% | **47%** | <1% | (dispatch) |
| `rope_string__compute_hash` | 2% | — | — | — | (string-specific) |

**Headline.** The audit's top suspicion was right: **§14 is the
single biggest CPU sink** in any allocation-heavy workload. On
`collection_churn`, `gc__find_fit_in_block` alone is 58% of CPU —
more than the rest of GC + interpreter combined. A per-block
"first free line" hint or heap-level free-run list converting it
from O(blocks × 512) to O(1) amortized would be the biggest single
perf win available.

**Spawn-heavy workloads look completely different.** `spawn_chain`
spends 97% of CPU in the worker loop's spin-and-backoff path, not in
GC at all. This is `§10` (single-mutex inbox) plus `§11` (10ms
progressive backoff, no condition variable). Both audit items will
need to land for spawn throughput to move. The 4.6s of `idle_sleep_ns`
in the bench output confirms `§11` directly — that's wall-clock spent
in `SLEEP_MILLISECONDS(backoff)` while the inbox sits with no real
work ready.

**Box-churn (small ephemerals) is dispatch-bound.** Once allocation
is cheap (a free run is usually available), `vm__run` becomes the
hot loop at 47% — the switch-on-opcode dispatch itself, including
the per-iteration safepoint check. A computed-goto / direct-threaded
rewrite of the dispatch loop would help this profile. Not on the
audit punch list explicitly but implied by `§13` ("tag dispatch in
`jacl_is_heap_type`" — same pattern).

**Surprise observation.** `unicode_grapheme_next` accounts for ~8%
of mixed CPU, driven entirely by string-concat's rope summary
computation. This isn't on the audit punch list at all — it's a
specific consequence of the rope/string design that should be
investigated separately (cache or lazy-evaluate the rope summary?).

### Profiling baseline (2026-05-14, post phases F + G)

Re-captured after the rope-summary fix (phase F) and FNV-1a hash
extend (phase G).  Raw artifacts in
`docs/profiles/2026-05-14_post_tier2_profile.txt` (pre-F),
`docs/profiles/2026-05-14_rope_summarized_diff.txt` (after F), and
`docs/profiles/2026-05-14_hash_extend_after.jsonl` (after G).  Same
build settings as the 2026-05-13 baseline (`-O2 -g
-fno-omit-frame-pointer`, no TSAN, 2 workers, BENCH_TIMED_ITERS=3000
for the gprofng captures).

| Function | mixed | collection_churn | box_churn | spawn_chain | string_concat | Note |
|----------|------:|-----------------:|----------:|------------:|--------------:|------|
| `runtime__worker_loop` (excl) | **44%** | — | — | **81%** | — | unchanged (CV-idle dominated) |
| `gc__find_fit_in_block` | 14% | **36%** | 7% | <1% | **36%** | down from 51% mixed, still the alloc-heavy ceiling |
| `vm__run` | 9% | 15% | **61%** | 2% | 5% | up from 47%→61% on box_churn (share grew as F/G shrank total) |
| `gc_sweep_concurrent` | 5% | 12% | 2% | <1% | 15% | unchanged proportionally |
| `pthread_cond_signal` | 4% | 1% | 5% | 5% | 1% | the §11 CV change |
| `rope_string__compute_hash` | 1% | — | — | — | (out of top 15) | was 8% pre-G |
| `unicode_grapheme_next` | <1% | — | — | — | 3% | was 19% pre-F |

**Headline shift.** With F + G in, the *mixed* profile has **no
single sink above ~14%** (excluding `runtime__worker_loop`'s
intentional spin/CV-idle time). The runtime is well-balanced; the
remaining concentrated levers are scenario-specific:

- `box_churn` is now overwhelmingly **dispatch-bound** (`vm__run`
  61%) — §13 (computed-goto / direct-threaded rewrite) is the
  biggest unclaimed lever.
- `collection_churn` and `string_concat` are still **alloc-bound**
  (`gc__find_fit_in_block` 36% + `gc_sweep_concurrent` 12–15%) —
  §14 tier-3 territory, but the naïve intrusive-list design
  failed (see Punch list item #1); needs a redesign.
- `spawn_chain` is unchanged in shape: 81% in worker idle path.
  Driving it down further means per-worker targeted CV signaling,
  significant architectural work.

**Recommended order of attack.**

1. ~~**§14** — universal win, large magnitude, well-understood fix.~~
   **Tier-1 + tier-2 landed** (2026-05-13 + 2026-05-14). Tier-3
   **attempted and reverted (2026-05-14)** — see the "Punch list"
   item #1 above for the design that didn't pan out and the three
   interactions that broke it (walker invariant, search_block
   locality, list growth). Open to a redesign but no longer a quick
   win.
2. ~~**§11 + §10** — required for spawn-heavy throughput; the two
   are coupled (a real condition variable needs a per-worker
   inbox or sharded queue to wake the right worker).~~
   **Both landed.** §11 CV change in 2026-05-13. §10 from-worker
   fast path in 2026-05-13 (this pass). The "coupled" framing
   turned out to be partly wrong: the fast path pushes to own
   `public_deque` (mutex-free, no CV signal), and the existing
   global CV with 1ms timeout handles externals fine.
3. ~~**Rope summary perf** — promoted to top of remaining queue by
   the 2026-05-14 post-tier-2 profile. `unicode_grapheme_next` is
   19% of `string_concat`, with `rope_summary_summarize` (5%) and
   `rope_string__compute_hash` (6%) right behind — ~30% of the
   scenario in the rope hot path. Tier-2's alloc-path wins
   unmasked this (it was 8% of mixed before; now it's the largest
   single non-GC sink). Likely fixable with a cached/lazy rope
   summary.~~ **Landed (2026-05-14)** — see phase F row above.
   `ST_MK_LEAF_SUMMARIZED` short-circuit in `ST_CONCAT`'s three
   leaf-merge sites cut `unicode_grapheme_next` 19% → 2.5% on
   `string_concat`; wall_median -42% on `string_concat`. Adjacent
   wins on `collection_churn` (-41%) and `spawn_chain` (-33%) too
   — those scenarios don't use strings, so the deltas are likely
   single-run noise, but worth re-measuring on a multi-run setup
   if anyone investigates.
4. **VM dispatch (§13 family)** — computed-goto rewrite of
   `vm__run` for the dispatch-bound regime. `vm__run` is **61% of
   `box_churn` post-rope-fixes** (was 52% pre-fix; share grew as
   everything else shrank). Bigger single-scenario delta than #3
   but biggest scope (whole dispatch loop) and a portability
   story (`&&label` GCC/Clang-only — needs MSVC fallback).
5. ~~**`rope_string__compute_hash`** — surfaced as the new top non-
   GC sink on `string_concat` post-rope-summary fix (12%, up from
   6% in absolute share because the total time shrank).~~
   **Landed (2026-05-14) as phase G** — see the status row above.
   Incremental FNV-1a extend over the right side of the concat
   (`hash(a ++ b) = continue_FNV(hash(a), b's bytes)`); win is
   smaller and noisier than phase F's was.

**Status after phases F + G + H**: there is no single sink above 14% in
the **mixed** profile (excluding `runtime__worker_loop`'s spin/CV
idle time, which is intentional). After phase H (per-block
`max_free_run_lines` reject), `gc__find_fit_in_block`'s in-block scan
work dropped 94–99%; remaining cost is the outer block-walk pointer-
chase (unchanged by H, by design) plus the in-block scan on the
admitted block. Remaining concentrated opportunities:

- §13 dispatch — 61% of `box_churn`, biggest single-scenario lever.
  WASM portability is the main design question (`&&label` is
  GCC/Clang-only; could `#ifdef` it and let WASM fall through to the
  switch, or invest in WASM tail-call dispatch for parity).
- `gc__find_fit_in_block` residual — ~500 lines per admitted block
  on alloc-heavy scenarios (distance from `first_free_line` to the
  chosen run). Phase C territory: in-block index structure (per-
  block free-run array or skiplist). Re-profile after H lands;
  likely no longer a top sink.
- `gc_sweep_concurrent` — 12–15% in alloc-heavy pre-H. Re-profile
  to see if H's reduced mutator alloc work also lowered the sweep
  share, or if it's still meaningfully concentrated.

Diminishing returns territory for further perf work. Defensible
stopping points: capture a fresh profile of a *real* JACL workload
(rather than these microbenchmarks) and let that profile drive
the next pass — the synthetic benches have served their purpose
on the easy wins.

A regression bench is now available: `./build.sh --test=perf`
prints JSONL stats for all four scenarios, and `tools/bench-diff.sh`
compares two JSONL snapshots. Capture a baseline before each
attempt and diff after.

**Caveat on bench measurements (learned 2026-05-14)**: at
`BENCH_TIMED_ITERS=200` the per-iter `wall_ns_median` can swing
±50% between single runs, and `wall_ns_max` regularly wraps
(reported as `18446744073.71G`) suggesting clock-source quirks.
At 500–3000 iters the median locks to ~2ms steps (clock
granularity, not the runtime's wall-clock floor). For changes
smaller than ~15% wall_median delta, *gprofng* CPU samples are
more reliable than the bench's own wall numbers — but even
gprofng on short scenarios has wide run-to-run variance (seen
3.5s → 12s across 5 samples at the same SHA). Quick rule of
thumb: trust the *direction* of a profile-shift observation
(e.g. "function X disappears from the top sinks") more than the
exact magnitude unless you have ≥5 runs to average.

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

**Expected normal-mode suite result:** 87 passed, 0 failed. The 14
`hamt*` / `rrb_vec*` standalone tests that had been blocked by the
removed `lib/rc` include path were retired together with the dead
RC-mode branches of those headers — see the 2026-05-13 housekeeping
note below.

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

1. **Make the SATB barrier unconditional on the deletion side.** —
   **DONE** (§9 fix + §10/§11 race #1 + sibling-holes pass extended
   this to insertion side too — see below). Both sides of
   `gc_write_barrier` now fire unconditionally, and every heap-pointer
   slot write on every GC-managed object is routed through it
   (mutable refs, FutureWaiter chains, sm->fields, sm->sm_closure,
   sm->error_k, cl->upvalues, stream->cached_value/next_fn/state_machine,
   etc.).

2. **Capture mutable-load values into a thread-local "stack root set."**
   — **STILL OPEN** (2026-05-13). The barrier discipline closes
   write-side races, but the *read-side* operand-stack-rooting gap
   that motivated this recommendation remains: a worker that does
   `OP_DEREF` on an atom (or `OP_GET_CELL_*`, or `OP_GET_STATE_FIELD`
   producing a heap value) pushes the value onto the operand stack,
   which is **not** a GC root in concurrent mode. The value is safe
   only because of one of the following coincidences:

   - Another rooted path also holds the value (e.g., the atom still
     points to it because nobody has reset the atom yet — the §9 SATB
     fix covers the "someone resets the atom" case but not the case
     where the value's only ref happens to vanish via some unrelated
     code path).
   - The value's epoch protects it (allocated this cycle).
   - The worker consumes the value before the next safepoint where a
     concurrent GC could observe it as unrooted.

   None of these are formally guaranteed. The class of programs
   that's safe today is "CPS-segments that don't hold mutable-loaded
   heap values across allocations" — which the JACL compiler does
   currently enforce *de facto* by virtue of how CPS lowering
   structures locals (state fields cross suspensions; pure stack
   slots don't). But this is a runtime-correctness property held by
   compiler convention rather than a runtime invariant, and the §D
   audits made it clear there's no static check enforcing it.

   **Why we left it open**: every concrete UAF the audit identified
   so far has been on the *write side* (mutation overwriting the
   value's only ref), not the *read side* (consumer holds value
   across a GC moment with no other ref). The chaos tests + 250 GC-
   stress runs under CV show no symptoms of read-side races. So
   while §9-#2 is theoretically necessary for fully-defended
   semantics, it's not surfacing as a bug today. Implementation
   would touch every `OP_DEREF`/`OP_GET_CELL_*`/`OP_GET_STATE_FIELD`/
   `OP_GET_UPVALUE` opcode and every site that produces a derived
   heap value from a mutable load, plus add per-worker "handle
   table" infrastructure scanned by `gc_enumerate_roots`. JVM-style.
   Significant work; revisit if a read-side UAF ever shows up under
   stress, or if JACL grows beyond CPS-only concurrency (e.g.,
   preemptive task switching mid-segment).

3. **Scan the VM operand stack in `gc_enumerate_roots` with safepoint
   synchronization.** — Alternative to #2; same problem space, just a
   different mechanism (pause-and-snapshot vs. eager handle-table).
   Also still open. Likely the cleanest long-term answer if JACL
   ever needs preemptive concurrency, but heavier than #2 today.

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

### 10/§11 — **FIXED** (2026-05-13): both races closed, CV change landed

A first attempt at the §10+§11 fix — replacing the worker-loop's
progressive sleep backoff with a bounded short-spin then a CV wait,
signaled by `push_inbox` / `push_pinned` — exposed a latent GC race
that is **not new** but only manifests under the higher concurrency
the CV change produces. Pre-CV baseline `gc_stress_atom.jacl` crashed
~3/30 (the audit's earlier "~1/30" was the low end of variance — a
30-run reproducer measured 3/30 in normal mode). Post-CV: ~24/30.

**Race #1 — FIXED**: `jacl_future_resolve` (and `jacl_future_error`)
did `waiters = f->waiters; f->waiters = NULL;` — a direct C
assignment that overwrote a heap-pointer slot on a published GC
object **without a write barrier**. The chain of `FutureWaiter` nodes
(which transitively roots every awaiting state machine via
`w->continuation`, per `OBJ_FUTURE_WAITER` tracing in
`gc_collect.c:256-263`) is reachable only through `fut → waiters` at
trace time. After the NULL store, a concurrent GC tracing the future
re-reads `fut->waiters == NULL`, misses the chain, and the sweep
reclaims the chain and its continuations. `runtime__schedule_waiters`
then iterates and reads zeroed memory.

The §9 unconditional-SATB-deletion fix closed exactly this class of
bug, **but only for mutations that go through `gc_write_barrier`**.
`f->waiters = NULL` is raw C.

**Why the prior fix attempt failed.** The earlier attempt published
the chain head into a per-worker `scheduling_waiters` slot before
detach, scanned by `gc_enumerate_roots`. The SMs still came back
zeroed because **roots are snapshot once at step 4 of
`gc_concurrent_collect`** (`runtime.c:1098`), not re-scanned during
the mark/convergence loop. Only **grey-buffer pushes** are drained
mid-cycle. A slot publication that happened after the root snapshot
but before the chain was consumed was invisible to the marker.

**Fix for race #1**: don't detach. `jacl_future_resolve` /
`jacl_future_error` now leave `f->waiters` pointing at the chain
across resolve. The chain stays reachable through `OBJ_FUTURE`
tracing for as long as the future itself is rooted. After settle,
`jacl_future_add_waiter` rejects new waiters (state-check under
`future_lock`), so the chain is bounded and write-once. ~10 lines in
`src/gc.c`. Validated by a deterministic regression test
(`test_future_resolve_waiter_chain_survives_gc`): pre-fix it fails
at the `returned == fut->waiters` invariant; post-fix it passes.

**Race #2 — FIXED**: `jacl_future_add_waiter` wrote `w->continuation
= continuation` directly, with no barrier. The fresh waiter `w` is
watermark-protected (epoch ≥ current), so the sweep keeps it without
needing to trace it via the mark path. But `w->continuation` is an
OLD (pre-watermark) SM whose only reachability during this cycle goes
through `w` — and if `w` isn't on any mark-stack path (e.g., the
future itself isn't externally rooted, or the mark phase has already
drained when add_waiter runs), the SM never gets marked. Sweep then
reclaims it. The chain head looks alive (watermark-protected), but
its `continuation` points to a swept SM. Next resolve+schedule reads
that pointer → garbage `sm_closure` → SEGV in
`runtime__schedule_sm_resumption`.

This is the symmetric counterpart to race #1: #1 was a missing SATB
barrier on the *deletion* side (waiter chain detached without
barrier); #2 is a missing barrier on the *insertion* side (heap
value written into a fresh container without barrier). Both fit the
§9 unsoundness pattern that §9's `gc_write_barrier` fix closed for
container mutations going through that API — but neither future
operation went through it.

**Fix for race #2**: unconditional grey-buffer push on
`continuation` inside `jacl_future_add_waiter`. Matches §9's
unconditional-deletion-side discipline: bounded cost (one push per
add_waiter; grey buffer resets each cycle), guaranteed coverage
regardless of `gc_active` timing or whether the fresh waiter is on
any mark path. ~5 lines in `src/gc.c`. Validated by:

- `test/test_runtime.c:test_future_add_waiter_grey_push` —
  deterministic UAF reproducer. Allocates V at low epoch, advances
  watermark past V, then add_waiter on a fresh (unrooted) future.
  Without the fix, V is swept (header zeroed); with the fix, V is
  grey-buffered → marked → survives. Confirmed catches the
  regression by temporarily disabling the push.

**Validation of the combined fix + CV change**:
- Full normal-mode suite: 88 pass, 0 fail.
- TSAN sweep: 80 pass, 8 fail — pre-existing baseline (`runtime`
  test moved from FAIL → PASS, suggesting the prior `runtime` TSAN
  failure was a flake masked by the race).
- `gc_stress_atom.jacl` × 30 with CV applied: 0/30 (was 27/30
  pre-fix, 3/30 pre-CV).
- Other `gc_stress_*` × 30 each: 0/120 (one outlier in
  `gc_stress_deep_cps` initial run that didn't reproduce in
  follow-up 100-run soak — noise floor).
- 60s `chaos_soak` under TSAN: 159K tasks, 0 heap problems.
- All 6 chaos tests under TSAN: clean.

**CV change details**: signal-one on `runtime__push_inbox` and
`runtime__push_pinned` (the latter takes `rt->inbox_mutex` briefly
to signal — non-targeted, but pinned-task latency is bounded by
the 1ms timeout); 1ms-timeout park in worker idle path; broadcast
under `inbox_mutex` from `runtime__stop_threads`. Worker re-checks
inbox + pinned + shutdown under the same mutex before parking, so
lost wakeups can't happen. The 1ms timeout caps steal-from-others
latency since other workers' deque pushes don't signal.

Perf delta on `spawn_chain` microbench is in the noise (median ~7ms
with or without CV at 200 iterations). §11 alone doesn't unlock the
big spawn-chain win the audit predicted; §10 (per-worker MPSC
inbox, removes the single-mutex bottleneck) is the real lever and
is independent follow-up work.

### Sibling unbarriered heap-ptr writes — **FIXED** (2026-05-13)

The `f->waiters = NULL` audit surfaced a pattern: direct C writes
that overwrite heap-typed pointer slots on already-published GC
objects, bypassing `gc_write_barrier`. Five sibling sites had the
same shape:

| Site | Field overwritten |
|------|-------------------|
| `vm.c` `OP_SET_STATE_FIELD` | `sm->fields[i]` (heap slot) |
| `vm.c` `OP_SET_STATE_FIELD_WIDE` | `sm->fields[base+i]` (inline-struct raw bytes — **no barrier needed**, marked via `field_inline_bitmap`) |
| `vm.c` `OP_CLOSURE` (3 capture branches) | `cl->upvalues[i]` (fresh closure) |
| `vm.c` stream pull (multiple paths) | `stream->cached_value` |
| `vm.c` stream create | `stream->next_fn`, `stream->state_machine` |
| `vm.c` SM construction (adjacent — not flagged in original audit but same shape) | `sm->sm_closure`, `sm->error_k`, `sm->fields[i]` during construction |

**Foundation fix**: `gc_write_barrier`'s insertion side is now
unconditional (matching the §9 deletion-side discipline). The
§9 gating assumption — "a new value stored after GC scanned the
container is already visible through the container" — holds only
for already-marked containers. Fresh containers (watermark-protected
but never traced) lose inserted values regardless of `gc_active`
state, which was race #2's mechanism. Unconditional insertion-side
push closes that gap globally.

**Per-site fixes**: each of the listed sites now calls
`gc_write_barrier` (or the `vm__slot_set` helper that wraps it) on
heap-pointer slot writes. The raw-bytes path
(`OP_SET_STATE_FIELD_WIDE`) is explicitly excluded because its slots
are not heap pointers and must not be grey-pushed.

Three deterministic synchronous-GC regression tests
(`test_sm_field_store_barrier`, `test_closure_upvalue_store_barrier`,
plus the prior `test_future_add_waiter_grey_push`) validate the
unified-barrier semantics across the §10/§11 race shapes. Validation:

- Full normal-mode suite: 88/88.
- `gc_stress_*` × 50 each (atom, spawn, race, deep_cps, persistent)
  with CV applied: 0/250.
- TSAN: 9 baseline failures (pre-existing non-chaos test issues),
  unchanged.
- 60s `chaos_soak` under TSAN: 187K tasks, 0 heap problems.

The §9 hardening recommendation #2 ("capture mutable-load values
into a per-worker stack-root set") remains a possible global
alternative for the broader operand-stack-rooting question, but
isn't needed for the heap-pointer-slot write class. With these
fixes plus the §9 deletion-side and §10/§11 races #1/#2, the
write-barrier discipline is uniform: any heap-pointer slot write
on any GC-managed object goes through `gc_write_barrier`,
unconditionally on both sides.

### 10. Single global inbox is a contention point — **FIXED** (2026-05-13)
~~`runtime__push_inbox` (`src/runtime.c:415`) is one mutex behind every external
submission, plus all pinned-on-fallback, plus most spawn / SM resumption. The
design's M14 amendment removed contention for the empty-check side; the push
side is still serialized. For high spawn-rate workloads, this caps throughput
at single-core mutex acquisition. Either per-worker MPSC inboxes, or a sharded
LIFO with worker hashing.~~

**Fix landed**: `runtime__push_inbox` now has a from-worker fast path. If
`rt__current_worker` is set and points to a worker in the same runtime,
the task is pushed directly to that worker's `public_deque` — Chase-Lev
SPSC on the owner-push side, no mutex, no CV signal. Other workers
steal from the public deque, preserving load-balancing. Externals (no
worker context) still take the global inbox + CV signal path.

Why public deque rather than per-worker MPSC inbox: the SPSC owner-push
to `public_deque` is the canonical Chase-Lev pattern and was already
covered by the §1/§2 fixes (private/public split + GC thief registration
+ epoch-protected buffer reclamation). No new data structures needed.
Cross-worker pinned routing remains on `pinned_inbox` (per-worker MPSC,
unchanged). The fast path skips the CV signal because the submitter is
itself active (will drain its own deque on the next loop iteration); a
parked worker sees the new task via the steal path within the 1ms CV
timeout. This bounded latency is the same one the §11 fix already
accepted.

**Validation (2026-05-13)**:
- `spawn_chain` bench (N=200, 8 runs): median 7.3ms → 6.5ms (~10%).
- `inbox_pops` on `spawn_chain`: 11407 → 220 (98% drop — only external
  submissions hit the global inbox now).
- `steal_successes` on `spawn_chain`: 0 → 1873 (work-stealing is now
  actively exercised; previously every non-pinned submission funneled
  through the global inbox and was drained back into the submitter's
  own private deque).
- Full normal-mode suite: 88/88.
- TSAN suite: 80/8 — matches pre-fix baseline exactly (8 pre-existing
  perf-counter races + chase_lev_stress/rwlock/compiler/integration/
  jacl_harness/cross_thread_string/syntax flakes, all pre-existing).
- All 7 chaos tests under TSAN: clean.
- `gc_stress_*` × 50 each (atom, spawn, race, deep_cps, persistent):
  0/250.
- 60s `chaos_soak` under TSAN: 216K tasks, 0 heap problems.

The wall delta is smaller than the profile predicted because
`spawn_chain` is ~50% pinned tasks (which already use per-worker
`pinned_inbox` and didn't go through the global mutex). The §10 fix
removed the mutex from the *other* 50% and unlocked actual
work-stealing on `public_deque`. The remaining ceiling on `spawn_chain`
is the 1ms CV wake-up timeout (§11 path); driving that down would
require per-worker CV with targeted signaling, which doesn't help
the dominant in-worker submission case anyway.

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

### 14. `gc__find_free_run` slow path is O(blocks × 512) — **FIXED** (tier 1 + tier 2 + Phase B)
~~When the current free run is exhausted, `gc_alloc` scans every block in the
heap from line 0 (`src/gc.c:393-401`). Each block scan is
O(GC_LINES_PER_BLOCK=512). Heaps grow to thousands of blocks under load.~~

**Tier-1 fix landed**: added `heap->search_block`, an anchor pointing to the
block where the last successful `gc__find_fit_in_block` returned. The slow
path now resumes the block walk from there instead of restarting at
`heap->blocks` head every call. Lines within each block are still scanned
from line 0, so smaller leftover free runs (skipped because they didn't
fit a larger allocation) remain reachable for subsequent smaller
allocations. Sweep clears `search_block` at the end of each GC cycle so
freshly-freed runs near the list head are picked up on the next pass.

**Tier-2 fix landed (2026-05-14)**: added `GCBlock.first_free_line` —
per-block scan hint. Invariant: lines `[0, first_free_line)` are all
OCCUPIED. Written by sweep (post line-map rebuild, computed in the same
pass that already scans for all-empty blocks) and by
`gc__find_fit_in_block` on successful fit (advance to `run_start` —
the find_free_run walk just confirmed `[hint, run_start)` is OCCUPIED).
Both writers run under `heap->blocks_mutex`; the bump-alloc fast path
doesn't touch it. Cost: 2 bytes per block, one extra compare/store per
slow-path scan call. Win: aged blocks with a heavy OCCUPIED prefix no
longer rescan that prefix on every slow-path call — the canonical
remaining cost that tier-1 didn't address.

Tier-1 benchmark deltas (BENCH_TIMED_ITERS=500, geomean of 8 runs)
remain as documented above. Tier-2 stacked on top
(BENCH_TIMED_ITERS=200, single clean run after warmup; see
`docs/profiles/2026-05-14_tier2_*`):

| Scenario | wall_median | gc_total_ns |
|----------|------------:|------------:|
| `collection_churn` | **-16.9%** | -11.8% |
| `string_concat`    | **-59.1%** | +14.7% (cycles +1) |
| `box_churn`        | **-39.5%** |  -3.7% |
| `spawn_chain`      | (noise, -0.0%) | -24.1% |

The string_concat win is dominant because that scenario's heap grows to
~1500 blocks during the run — the OCCUPIED prefix on aged blocks was
exactly the cost tier-2 targets. `spawn_chain` shows no wall delta
(expected: not alloc-bound; profile pinned 97% in worker idle path).
GC totals nudged in mixed directions because the trigger-vs-survivor
ratio shifted slightly — `bytes_allocated` is unchanged across all
scenarios, so total mutator work is the same.

Validation: 88/88 normal-mode suite, TSAN baseline unchanged (5
pre-existing fails — perf, chase_lev_stress, rwlock,
cross_thread_string, jacl_harness), all 7 chaos tests TSAN-clean, 60s
TSAN `chaos_soak` clean (201K tasks, 0 heap problems).

Tier-3 (heap-level intrusive free-run list — the audit's original
recommendation) remains available but is now a secondary lever: after
tier-1 + tier-2, profile re-capture should show a different top sink.
Defer until a real-workload profile says it's still needed.

**Phase B fix landed (2026-05-14)**: instead of the heap-level intrusive
free-run list (the original tier-3 sketch that was attempted and
reverted — see Punch list item #1), the same problem yielded to a
per-block `max_free_run_lines` upper-bound hint. Added `uint16_t
max_free_run_lines` to `GCBlock` (next to `first_free_line`). Sweep
writes it authoritatively in the same pass that derives
`first_free_line` (one-pass scan via the deduped
`gc__summarize_block_map` helper that Phase D introduced). `gc_alloc`'s
outer block walk does `if (block->max_free_run_lines < needed_lines)
continue;` as an O(1) reject before calling `gc__find_fit_in_block`.
On a slow-path miss inside the block, the run walker tightens the
cached bound to the authoritative max seen during the walk; the bound
is conservative (never under-states the actual max) between sweeps so
a false admit costs only one in-block scan.

Why this works where the intrusive-list tier-3 didn't, mapped to the
three failure modes called out in Punch list item #1:

1. **Walker invariant intact** — no metadata stored in free payload.
   `max_free_run_lines` lives in the block header struct (out-of-band
   from `payload[]`), so sweep's "advance by 8 over zero bytes"
   convention is preserved.
2. **Block-level locality preserved** — the `search_block` anchor and
   the outer block-walk order are unchanged. Phase B only narrows
   *which* blocks the walk pays the line scan on.
3. **No heap-level list growth** — no list, no `blocks_mutex`-held
   sweep, no size-bucketing required.

Phase A instrumentation (`blocks_scanned` + `lines_scanned` +
`blocks_rejected_fast` counters on `ThreadHeap` and `JaclPerfSnapshot`)
drove the design decision. At BENCH_TIMED_ITERS=2000, each slow-path
call visited 230–370 blocks scanning 35–180 lines/block before Phase
B; afterwards 97–99% of those visits are O(1)-rejected:

| Scenario | lines_scanned pre | post | drop | wall_median |
|----------|------------------:|-----:|-----:|------------:|
| `collection_churn` |   694.7M |   8.5M | **-98.8%** | 8.0→6.0 ms |
| `string_concat`    |   758.7M |  42.5M | **-94.4%** | 14.6→6.0 ms |
| `box_churn`        |    66.6M |   0.8M | **-98.7%** | 7.4→6.2 ms |
| `spawn_chain`      |     5.4M |   0.3M | -94.6%      | (not-bound) |

(wall_median single-run; per the F/G caveat the directional signal is
the line-scan reduction.) The remaining cost is ~500 lines per
admitted block — distance from `first_free_line` to the chosen run on
blocks that pass the reject. That's Phase C territory (in-block index
structure, e.g. per-block free-run array) if a future profile still
shows `gc__find_fit_in_block` hot. With Phase B in, it should fall
out of the top sinks; re-profile to confirm before designing further.

Validation: 88/88 normal-mode suite, 30x gc_stress_atom clean, TSAN
baseline unchanged (86/2 — `jacl_harness` + `chase_lev_stress`, both
documented known-and-safe), 30s TSAN chaos_soak clean (73883 tasks).
Commits `51c9c3a` (instrumentation), `4d0858b` (Phase D dedup),
`4059961` (Phase B).

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

### 18. ctx-pool vs SM-state captured-pointer UAF — **FIXED** (2026-05-14)

**Fix landed (option 1): the ctx pool was eliminated.** `ctx_pool_alloc`
is now a direct `gc_alloc(OBJ_HEAP_RECORD)`; `ctx_pool_free` is a no-op.
The `JaclCtxPool` struct stays (callers still hold `struct_size` /
`type_idx` / `sdef` for the alloc), but `free_list_head` is unused —
freed ctxs become unreachable when `vm.ctx` is restored at `ctx_unfork`
and are reclaimed by the GC on the next cycle. The free-list-walks in
`gc_enumerate_roots` (`runtime.c`) and `gc_mark` (`gc_collect.c`) are
gone (free list is always empty). The 3 obsolete `test_ctx_pool_*`
tests in `test_gc.c` were retired.

Bench result (median-of-5, BENCH_TIMED_ITERS=200): **-20.1%** on
`collection_churn`, **-13.4%** on `spawn_chain`, **-5.0%** on
`string_concat`, **-1.7%** (noise) on `box_churn`. So pool removal is
a perf *win* in addition to closing the race. The CAS pop + `memset`
the pool was doing per ctx alloc was more expensive than the
post-`§14` `gc_alloc` bump path.

Two adjacent races fell out of the same investigation, both fixed:
the §S5 slice 1 `vm->env` race (separate commit `83d2736`), and a
`GcStats` cycle-counter race (writer in `gc_concurrent_collect` doing
plain `+=` while `jacl_perf_snapshot` struct-copied the field;
converted to atomic load/store, same pattern as `WorkerStats`).

Original analysis follows for posterity.

---


Surfaced while investigating `perf`'s remaining TSAN failure after the
§S5 slice 1 (`vm->env` atomic) closed the originally-listed race.
TSAN now flags:

- Worker T_a (parent worker): mid-spawn, `ctx_fork` does
  `memcpy(dst->data, src->data, total_size)` reading `parent_ctx->data`
- Worker T_b (different worker): `ctx_unfork` → `ctx_pool_free` doing
  NIL-writes (release-stored) and free-list-next-pointer-write (plain)
  on `s->data` at the same address

For two different workers to touch the same byte, T_a's `parent_ctx`
must point at the same `HeapRecord` that T_b is freeing. T_a got it
from `vm->ctx` at the `OP_SPAWN` site. T_b owns the underlying pool
slot. So `vm->ctx` on T_a is a pointer into T_b's ctx pool.

How that happens: the SM compiler at `compiler.c:3468–3471` emits
`OP_GET_STATE_FIELD ctx_field_idx; OP_SET_CTX` as part of the
resume-dispatch prelude. The corresponding suspension path saved
`vm->ctx` (via `OP_GET_CTX`) into the SM's state field. So an SM that
suspends on one worker and resumes on another carries a pool-backed
`HeapRecord*` across the boundary in its state field, and the resume
path stores it back into the new worker's `vm->ctx`.

Once on the new worker:

1. The new worker reads from `vm->ctx` (for example, the snapshot/
   memcpy at `OP_SPAWN`, or any `OP_GET_CTX` consumer).
2. The old worker — whose pool still owns the slot — eventually does
   `ctx_unfork`, which runs `ctx_pool_free`, NIL-ing reference fields
   and pushing the slot's first 8 bytes onto the pool's free list.
3. The old worker can then `ctx_pool_alloc` and re-issue the same slot
   for an unrelated future ctx, overwriting all its bytes.

The `gc_enumerate_roots` walk at `runtime.c:1033–1040` scans each
worker's `ctx_pool->free_list_head` so the *memory* stays GC-alive,
but the pool's free-list reuse path is independent of GC reachability —
nothing prevents it from re-issuing a slot that an SM state field
still references.

**Severity.** Reachable in the runtime today; observed by TSAN in
`test_perf` (`spawn_chain` scenario). The bench passes in normal mode
because the timing is rare (parent worker finishes `ctx_unfork`
between the SM's resume and the resume's first `vm->ctx`-using
opcode) and the read often gets stale-but-plausible data. Worst-case
manifestations:

- Resumed SM reads ctx struct fields → gets unrelated ctx's data
  (silent semantic bug: "function called with X where Y expected").
- Resumed SM is mid-memcpy when the slot is re-issued → torn read of
  mixed-source bytes.
- Resumed SM reads at `data[0..7]` exactly when `ctx_pool_free`'s
  free-list-next-pointer-write executes → reads a pool free-list
  pointer where a ctx field should be.

Not a §S5 missing-atomics issue — making the access atomic doesn't
fix it. The slot is genuinely freed and reused.

**Fix options (in order of complexity):**

1. **Eliminate the ctx pool.** Use `gc_alloc` on every fork; let GC
   reclaim. Simplest. Loses the pool's allocation-overhead
   optimization, but §14 tier-1+tier-2 made `gc_alloc` significantly
   cheaper. Worth measuring first.

2. **Snapshot at suspension / capture sites.** Make `OP_GET_CTX`
   allocate a fresh non-pooled `HeapRecord` and copy bytes into it,
   so the captured pointer is GC-managed (no pool reuse). Surgical
   — touches `OP_GET_CTX` and any other site that lets a pool-backed
   ctx pointer escape into long-lived storage. Adds one gc_alloc per
   ctx-capturing suspension.

3. **Defer `ctx_pool_free` to GC sweep.** Pool-free becomes "mark for
   reclaim"; the GC sweep does the actual free-list push only for
   slots that GC has determined are unreachable. Keeps the fast
   alloc path; complicates the sweep. Solves the issue universally
   because GC roots include SM state fields.

4. **Refcount ctx slots.** Each escape-into-state-field path bumps a
   refcount; `ctx_pool_free` only puts on free list at zero.
   Pervasive change.

(2) is the most surgical; (1) is the simplest and lets `gc_alloc`'s
recent perf work absorb the cost; (3) is the most architecturally
clean. The right pick depends on what fraction of total CPU
`ctx_pool_alloc` / `_free` actually save. The audit's profiling
baseline didn't isolate the pool — that should be the first move.

**Partial fix evaluated and reverted (2026-05-14):** I tried
snapshotting `parent_ctx` at spawn submit time (`runtime__submit_spawn_task`
allocates a fresh non-pooled `HeapRecord`, memcpys parent's ctx into
it, child uses the snapshot). That closes the *spawn-time* race
between submit and child's `ctx_fork`, but the SM-state-saved-ctx
race fires earlier: by the time submit runs on the parent worker,
`vm->ctx` was already set by an `OP_SET_CTX` from a saved state
field, pointing into another worker's pool. So the spawn-side
snapshot doesn't help and just adds a gc_alloc per spawn. Reverted.

**Resolution:** picked option 1. See header at top of §18.

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

## §16 VM stack overflow surfacing (2026-05-15)

The audit (§16 in the open-items list) framed VM_STACK_MAX = 256 as
"either grow on demand or document the limit and emit a clear error
message" — the cheap fix here is the latter. Two distinct overflows
were collapsed under one error string and one of them silently lost
its message on completion paths.

**What changed:**

- Two helpers in `vm.c` (`vm__set_frame_overflow`,
  `vm__set_operand_overflow`) replace every bare
  `vm__set_error(vm, "stack overflow")`. The frame variant says
  `"call depth exceeded (max 64 frames) — too much nested recursion"`;
  the operand variant says `"operand stack overflow (max 256 slots)
  [at <context>] — expression too deeply nested"`. The pre-existing
  contextual variants (`"stack overflow (swap inline)"` etc.) now
  carry the same prefix + limit number but keep their location tag.
- Worker SM/spawn/parallel/race completion in `runtime.c` previously
  fell back to `jacl_set_error(jacl_inline_string("error", 5))` on
  any VM error from the body, silently dropping `vm->error_message`.
  A new static helper `runtime__make_completion_error(vm)` carries
  the real message through. Worker VMs run with `vm->intern_table =
  NULL`, which `jacl_string_new`'s intern tier would deref — the
  helper detects this and truncates to inline-string rather than
  crashing.
- `src/jacl.h` now documents both caps where they're defined.

**Tests:**

- New `test/jacl/overflow_call_depth.jacl` — `# expect-error: call
  depth exceeded`, triggered by unbounded direct recursion.
- Two existing C tests updated to the new error substring:
  `test_call_frame_overflow` in `test/test_vm.c` and the recursion
  test in `test/test_compiler.c`.

**Out of scope:**

- Growing the stack on demand — explicitly rejected by the user.
- Investigating WHY worker VMs run tasks after `runtime_destroy`
  with intern_table = NULL (a latent issue this work surfaced via
  the new helper's allocation attempt). The helper now degrades
  gracefully; the underlying lifecycle gap is its own item if it
  surfaces in another way.

**Validation.** Normal-mode sweep: 87/1 — the single failure is
`chaos_concurrent_intern`, a pre-existing flake unrelated to this
change (confirmed by reverting `src/runtime.c` and observing the
same 1-in-5 to 2-in-10 failure rate on the test under both
configurations). TSAN sweep: 86/2 — `chase_lev_stress` (documented
known-and-safe per AUDIT.md) plus `rope_concat`'s
`test_rope_concat_zwj_at_junction` (pre-existing under TSAN;
confirmed by reverting both `src/runtime.c` and `src/vm.c` and
seeing the same failure). WASM compile-only: clean.

## §13 VM dispatch (2026-05-14)

The audit measured `vm__run` at 57 % of `box_churn` CPU under the
central-switch dispatch and asked for computed-goto; option 1 from the
audit (ifdef computed-goto, switch fallback for WASM) landed.

**Shape.** A 222-entry `static void* const dispatch_table[]` lives at
function scope inside `vm__run`. Each `case OP_X:` became `CASE(OP_X):`
where `CASE(op)` expands to `case op` (switch mode) or `L_##op` (goto
mode). Each case-ending `break;` became `DISPATCH();` — including the
~150 *internal* early-exit breaks scattered through opcode bodies that
relied on the outer switch to "go to next instruction" (not just the
~210 terminal ones). 348 break replacements across the body. The
dispatch macros are `#undef`ed at the end of `vm__run` so they don't
leak.

The feature gate is `#if (defined(__GNUC__) || defined(__clang__)) &&
!defined(__EMSCRIPTEN__)` — Emscripten defines `__GNUC__` (it's clang)
but lowers `&&label` to a single WASM `br_table`, losing the
per-opcode prediction benefit, so the switch path is what runs there.

**Caveats.**
- The dispatch_table mirrors the OpCode enum from `bytecode.c`
  (which the unity build sees), not `jacl.h` (which test files see —
  one entry shorter as of 2026-05-14, pre-existing enum drift). Adding
  an opcode requires touching both the table and adding a `CASE(op):`
  handler in the same pass.
- `VM_PRELUDE()` is a macro, not a function call, on purpose: in goto
  mode the per-opcode dispatch sequence (GC safepoint check + line
  tracking + opcode fetch + indirect jump) is inlined at the tail of
  every handler, which is what gives the branch predictor a per-site
  prediction context. Function-call overhead would defeat the
  optimization.

**Perf.** Microbench `box_churn` at -O2 (500 timed iters, average of
3 runs): pre-§13 median ≈ 1,162 µs, post-§13 median ≈ 1,132 µs —
roughly 2–3 % faster. Smaller than the 15 %+ this transform sometimes
delivers on other interpreters. Likely reasons: (a) GCC at -O2 already
recognizes the dense `case 0..221:` and emits a near-optimal jump
table for the switch, and (b) other parts of the loop (allocation, GC
safepoint, push/pop helpers) dominate dispatch overhead. The bigger
value here is having the structure in place for future opcode work
where the per-handler tail-call shape matters more.

**Validation.** Normal-mode 88/0; TSAN baseline 86/2 preserved
(known-and-safe `chase_lev_stress` + `jacl_harness`); WASM compile
clean via `./build.sh --wasm`.

## Smaller things

- `gc_sweep` and `gc_sweep_concurrent` duplicate ~80% of their loop bodies;
  same for `gc_mark` and `gc_mark_minor` (root enumeration is copied twice). A
  `for_each_root` helper and a `sweep_block` with strategy flags would cut
  ~400 LoC.
- `gc__oom_panic_default` calls `abort()` without giving callers a chance to
  clean up. For embedded use, this should be a callback returning an error
  code. **FIXED** (2026-05-14): public `jacl_set_oom_handler(handler,
  user_data)` added to `include/jacl.h` + `embed.c`. The underlying
  `gc__oom_handler` global already supported non-aborting return (alloc
  returns NULL); the new setter exposes it through a stable embed API
  without leaking `ThreadHeap` to embedders.
- `volatile` is used on shared fields **in addition to** atomic load/store
  macros. The atomics are sufficient; the volatile is noise and on some
  compilers inhibits optimization. **FIXED** (2026-05-14): swept all
  `volatile` qualifiers from struct field declarations (`JaclFuture`,
  `ParallelAgg`, `RaceAgg`, `WorkerThread`, `Runtime`, `JaclCtxPool`, VM
  `gc_active_ptr`) and from the `(volatile uint64_t*)` casts at
  `ATOMIC_*_EXPLICIT` call sites — the macros re-cast to `_Atomic` (C11) /
  `volatile` (MSVC) internally so the caller-side qualifier is redundant.
  Both jacl.h mirrors and the .c definitions updated so `test_struct_sizes`
  stays in sync. TSAN suite preserved at the 86/2 baseline; chaos_soak
  20-s smoke clean.
- `parallel_agg_ptr`, `race_agg_ptr`, `jacl_future_ptr` all build the value
  with `(uint64_t)(uintptr_t)p & JACL_PAYLOAD_MASK`. The mask is unnecessary
  if pointer alignment is enforced — and if it *is* necessary, you're silently
  truncating high bits. Either way, the mask hides bugs. **FIXED**
  (2026-05-14): introduced `JACL_PACK_PTR(tag, p)` in jacl.h / value.c.
  The macro asserts in debug builds that the pointer fits in the 56-bit
  payload (so a future LA57-host with wider VAs fails loudly instead of
  silently truncating into the tag) and keeps the mask in release for
  belt-and-suspenders defense. Replaced the ~25 callsites across value.c,
  gc.c, runtime.c, string.c that used the raw pattern.
- **Stray RCHeader references / offset assumptions**: a sweep was warranted
  because the design once had `GCHeader` (8B) replace a 16B `RCHeader` on
  HAMT/RRB nodes. **CONFIRMED CLEAN** (2026-05-14): zero stray references
  in `src/`; the only mention is one historical-context comment in
  `gc.c:5` ("replacing the 16-byte RCHeader used by collections") that
  bakes in no offset assumption. No fix needed.
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

### D.1 Tag-check completeness — **FIXED** (56 asserts, 1 typer bug found)

Survey of every `jacl_as_*` call in opcode handlers asks: was the
source tag verified (`jacl_is_*`) first? Original estimate: ~32
unchecked vs ~8 checked, concentrated in four areas. Real count
turned out larger after enumerating every typed-collection opcode
and per-primitive struct-field-store case: 56 sites total.

| Area | Opcodes | Source | Status |
|------|---------|--------|--------|
| State-machine fields | `OP_GET_STATE_FIELD{,_CELL,_WIDE}`, `OP_SET_STATE_FIELD{,_CELL,_WIDE}`, `OP_GET_RESUME_POINT`, `OP_SET_RESUME_POINT` | `frame->stack_base + 0` — compiler invariant: slot 0 of a state-machine function holds the SM | **8 asserts** |
| Struct field stores | `OP_HEAP_RECORD_NEW`, `OP_STRUCT_NEW_INLINE`, `OP_STRUCT_SET_INLINE`, `OP_STRUCT_SET_UPVALUE` | Popped from stack; bytecode operand declares the type | **16 asserts** (4 primitive types × 4 opcodes) |
| Typed collections | `OP_TYPED_VEC_*`, `OP_TYPED_MAP_*` (length / push / set / concat / slice / each / transform / filter / keys / vals / has / remove / print / eq) | Popped from stack | **26 asserts** |
| Stream / SM closure | `OP_SPREAD`, `OP_STREAM_NEXT`, `OP_COLLECT` reading `stream->state_machine` and `sm->sm_closure` | Field of a heap object | **6 asserts** (3 SM + 3 closure) |

Macro: `JACL_ASSERT_TAG(v, pred)` expands to `assert(pred(v))`. Compiles
to no-op under `-DNDEBUG`.

**Bug surfaced — typer mis-inferred `map-keys` over dyn-keyed maps**:
The typer at `src/typer.c` unconditionally inferred
`(map-keys x : typed-map K V) → typed-vec` even when K was dyn
(`inferred_key_struct_idx == UINT32_MAX`). The runtime emit and consume
in `OP_TYPED_MAP_KEYS` had always switched on `key_type_idx == 0xFFFF`
correctly — the dyn branch pushed a plain `jacl_vector_ptr`, never a
typed one. The inconsistency had been silent because `vec-len`
downstream worked anyway: `jacl_typed_vec_count` shares its count-field
offset with `jacl_vec_count`. The assert surfaced this immediately
(`test_typed_map_keys` triggered `Assertion 'jacl_is_typed_vector(tvec_val)'`).
Fixed: typer now branches on `recv->inferred_key_struct_idx == UINT32_MAX`
and emits `TYPE_VEC` for the dyn-keys case.

**Test brittleness surfaced — `test_struct_new_inline_*`**: three sub-
tests in `test/test_compiler.c` hand-emit bytecode using `type_idx=1`
(the reserved ctx slot) where they meant the first user struct
(`type_idx=2`), and pushed `jacl_i32(1)` into a bool field. Pre-assert,
these "worked" because the runtime defaults to an 8-byte raw memcpy
for non-primitive fields, and `jacl_as_bool` returns payload bit 0.
Fixed.

### D.2 Stack discipline — **FIXED** (72 sites via `VM_ERROR` macro + 11 iterating-opcode reorders)

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
   iterating opcode. **Now landed across all 11 sites**: `OP_EACH`,
   `OP_TRANSFORM`, `OP_FILTER` (6 sites, commit `c39ee12`), then
   `OP_PARALLEL`, `OP_RACE`, `OP_SPREAD` (5 sites, follow-up). The
   (iii) category is closed.

2. *Either restructure error returns to reset stack_top, or add a
   `VM_ERROR(...)` macro that does so.* **Now landed.** Defined
   `VM_ERROR(vm_, ...)` at the top of `src/vm.c`:
   ```c
   #define VM_ERROR(vm_, ...) do { \
       (vm_)->stack_top = saved_stack_top; \
       vm__set_error((vm_), __VA_ARGS__); \
       return VM_RUNTIME_ERROR; \
   } while (0)
   ```
   Each affected opcode case declares
   `uint32_t saved_stack_top = vm->stack_top;` at the top and uses
   `VM_ERROR(vm, ...)` for any error return after a pop/push. 72
   error returns converted across 39 opcodes. Categories covered:
   binary arith/compare, destructuring (vec + named, including rest
   variants), string ops (concat / len / byte-len / index / slice),
   vector ops (get/len/push/set/concat/slice), map ops
   (get/has/len/set/remove/keys/vals), mutation ops (deref / reset / swap),
   conversion ops (to-i32/i64/u32/u64/f32/f64).

Neither fix changes correctness for **well-behaved** programs that
never hit a runtime error. The class of bugs they close: cumulative
slot drift in programs that recover from errors (currently impossible
to do cleanly), and the obscure "your stack is corrupted, here's a
nonsensical type error from 50 ops downstream" symptom. The (ii) and
(iii) categories are both closed.

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

Follow-up landed in a later pass: `OP_PARALLEL`, `OP_RACE`, and
`OP_SPREAD` (stream-generator path) got the same reorder — 5 sites
total (SM + non-SM branches for parallel/race, single path for
spread). Same shape, same pattern; commit message references this
section. Suite unchanged; TSAN unchanged relative to baseline.

The §D.3 test work also surfaced a new correctness bug — §D.6 below.

A follow-up audit of every recursive `vm__run` call site (28 total)
confirmed no other §D.6 shape exists: 19 propagate errors
immediately, 9 continue past error and explicitly reset both
`frame_count` and `stack_top` (including the §D.6 fix in
`OP_SPAWN`/`OP_RACE`), 0 AT RISK.

### D.6 `spawn` + deep recursion SEGV — **FIXED**

**Fix landed**: `OP_SPAWN`'s single-threaded path (`src/vm.c`) now
captures `saved_stack_top` alongside `saved_frame_count` and
**resets both** after the inner `vm__run` returns, regardless of
whether the sub-run succeeded.  This mirrors the existing pattern in
`OP_PARALLEL`.  `OP_RACE` had the same bug and was fixed in the same
pass.  Regression test:
`test/jacl/cps_spawn_deep_recursion.jacl` — `spawn { burn 70 }`
where `burn` recurses past `VM_FRAMES_MAX` now completes cleanly
(the stack-overflow becomes a future error surfaced by `await`)
instead of SEGV'ing.

Root cause, in one sentence: when the inner `vm__run` for the spawn
body returned `VM_RUNTIME_ERROR` (because `burn`'s deep recursion hit
the `vm->frame_count >= VM_FRAMES_MAX` check in `OP_CALL`), the
outer `OP_SPAWN` restored `vm->ip` and `vm->chunk` to main's chunk
but left `vm->frame_count` at 64 — so `frame = &vm->frames[63]` and
the next opcode (main's `OP_SET_STATE_FIELD`) read
`vm->stack[frame->stack_base + 0]` from burn's deepest stack slot
(which held the `i32` value of `n`), cast it as a `JaclStateMachine*`,
and dereferenced garbage.

Validated by:
- `test/jacl/cps_spawn_deep_recursion.jacl` (the original repro)
- Full normal-mode suite: 87 passed / 14 pre-existing fails — baseline
- Full TSAN sweep clean on the chaos test inventory
- 60-second `chaos_soak` TSAN run: 195K tasks, 0 heap problems

Original finding follows.

#### Original finding from §D.3 test work — `spawn` + deep recursion SEGV

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
