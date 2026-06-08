# JACL Codebase Audit (active)

> This file tracks **currently-open** audit items. The full historical
> record — all completed phases (A–H), every closed correctness fix,
> profiling baselines, completed-phase methodology, and the per-bug
> deep-dives — is preserved in `AUDIT_HISTORY.md`.
>
> Companion docs:
> - `GC_CONCURRENCY_DESIGN.md` — the *why* (design intent)
> - `SYNCHRONIZATION.md` — the *what protects what*, per-field reference
> - `AUDIT_HISTORY.md` — the audit's worked-through backlog (read-only archive)

## Current state (2026-05-19)

All correctness items from the May 2026 audit campaign are closed:
8 critical GC/concurrency items, 5 soak-surfaced races, the §9 SATB
barrier UAF, 4 TSAN-triage real bugs, the §18 ctx-pool UAF, and the
§D compiler/VM survey items. The runtime is well-balanced after the
§14 Phase-B fix landed — no single non-idle sink above ~10% in the
mixed-CPU profile. §16 (VM stack-overflow surfacing) closed
2026-05-15 — operand-stack vs call-depth overflow now produce
distinct, limit-naming error messages, and worker-completion paths
propagate `vm->error_message` to the awaiter. See `AUDIT_HISTORY.md`
for the per-phase story.

**Test baselines:**
- Normal-mode suite: 87 pass / 0 fail (`./build.sh`) as of 2026-05-19.
  The earlier drift below this baseline has been fully closed:
  `stream_type` in `a8fab24`, `chaos_soak` seed-dependent livelock
  by the worker-loop fix, and `rope_concat::zwj_at_junction` via the
  generalized `rope_concat` junction fix-up (§19, closed below).
- TSAN baseline: 86 pass / 1 fail (`./build.sh --tsan`).
  `chase_lev_stress` is the lone TSAN-mode failure and is
  **known-and-safe** — TSAN blindness on barrier/fence/epoch-mediated
  synchronization, not missed barriers. It fails with all of
  `src/runtime.c` + `src/vm.c` reverted, confirming it's not owned by
  JACL runtime code. `rope_concat`'s prior TSAN-mode failure shared
  its cause with the normal-mode failure and is closed alongside §19.
  - `chase_lev_stress`: 11/11 functional sub-tests PASS; ~20 race
    warnings from `test_stress_epoch_thief_rotation` cause the
    non-zero exit. Two distinct sites — `chase_lev.h:268` (plain
    data store synchronized by the release-store on `bottom` two
    lines later; the standalone test doesn't `#define
    CHASE_LEV_ATOMIC_DATA`, the TSAN-friendly mode `src/runtime.c`
    opts into) and `test_helpers.h:86` (epoch-protected `free` of a
    retired buffer; TSAN can't track happens-before through the
    epoch wait loop). Coverage of the runtime's actual chase_lev
    use lives in `chaos_pinned_deque` + `chaos_gc_deque_scan`, both
    TSAN-clean.

  AUDIT_HISTORY.md §"TSAN baseline triage (2026-05-14)" carries the
  full chase_lev triage walk-through.
- Soak flake (`chaos_soak`) — **fixed 2026-05-19**, see Closed
  items §20. The seed-dependent emergency-GC livelock was caused by
  an async GC-submit pattern in the worker loop; collapsed to the
  same synchronous trigger `runtime__emergency_gc` uses. Five
  consecutive `timeout 20 ./.build/chaos_soak` invocations with
  distinct seeds now pass in 5–10 s each.
- WASM compile check: clean (`./build.sh --wasm` — gated on `emcc`
  on PATH; skips with a notice if absent). Run before merging any
  change to `src/` or `include/` so the embedded/WASM target doesn't
  silently regress.

**Three docs that should evolve together:**

- `GC_CONCURRENCY_DESIGN.md` — design intent
- `SYNCHRONIZATION.md` — per-field protections
- `AUDIT.md` (this file) — gaps between the two, with fix status

Any change to runtime/GC shared state should update all three; struct
drift between `src/runtime.c`/`src/gc.c` and `src/jacl.h` is caught at
build time by `test/test_struct_sizes.c`.

## Open items

These are non-correctness — code health, ergonomics, deferred perf
levers. None block shipping. Roughly ordered by **remaining**
leverage; closed items live below the open ones.

### §17. Inconsistent thread-local convention — *low priority, no feature blocked*

`gc__current_heap`, `gc__thread_epoch`, `gc__emergency_gc_fn`,
`gc__interning`, `rt__worker_id`, `rt__current_worker` are scattered
globals. Worker context already lives on `WorkerThread`. The thread-
locals exist mostly for HAMT/RRB template code that doesn't take a
heap parameter. Threading a `JaclCtx*` (or equivalent) through the
templates would remove a class of hard-to-diagnose "wrong heap" bugs
and shave the TLS read off the alloc fastpath (also listed under
performance smells). Larger refactor.

**Not a sandbox prerequisite.** Sandboxing is already implemented by
the `interpret` primitive (`src/vm.c:9441`), which compiles and runs
untrusted source *in-place* on the parent VM's heap, with a fresh
top-level env populated from a capability prelude map. Same heap,
same arena, same intern table, same GC cycle, same workers — only
the env is reset. Per-OS-thread VM isolation (the only thing §17
would unlock) is not a use case JACL has. See
`test/test_interpret_sandbox.c` for the seven end-to-end scenarios
the sandbox API supports today.

### §12. `JaclFuture` waiters list spinlock — *not observed*

`f->lock` is a CAS spinlock (M14 traded `platform_mutex_t` for it to
avoid a finalizer leak). Under heavy waiter contention, spinning blocks
the OS scheduler from migrating a worker that's holding the lock —
preempted holder leads to all other waiters spinning until reschedule.
Not observed in any chaos test. Consider parking after N spins.

### §22. `gc__trace_object` OBJ_FUTURE trace race — *TSAN-flagged, schedule-dependent, not observed as a bug*

A `--tsan` run on 2026-06-08 flagged a data race at `gc_collect.c:415`
in the `OBJ_FUTURE` case of `gc__trace_object` — the concurrent marker
walks the future's waiter list (`w = w->next`, line 415) and reads
`fut->result` (line 410) while the resolving thread writes the result
and mutates the waiter list. Schedule-dependent: it did **not** appear
in an earlier `--tsan` run on the same tree, only surfacing under a
different thread interleaving. The `fut->state` load is `MEM_ACQUIRE`
(line 408) but the `fut->waiters` load is `MEM_RELAXED` (line 412) and
the subsequent `w->next` traversal is a plain read, so the list walk is
not ordered against concurrent waiter insertion/removal. **Not observed
as a wrong-result or crash** — the marker conservatively pushes whatever
it reads, and a stale waiter pointer is still a live GC object. Likely
fix: `MEM_ACQUIRE` the `waiters` head and confirm waiter-node `next`
links are published with release ordering by the inserting side (and
that `result` is release-ordered before `state`). Low priority;
investigate alongside §12 (both are `JaclFuture` concurrency). Unrelated
to the `[Arr T]` work that surfaced it (arr paths touch no future code).

### §11. Worker idle CV-park timeout — *architectural, deferred*

Phase 10/11 landed a real CV-based wake-up path. Residual: workers park
with a **1 ms timeout** to bound max latency for cases the signaling
path can't reach (notably the from-worker fast-path push to own
public_deque, `runtime.c:737` — deliberately mutex-free, no signal).
On `spawn_chain`, `runtime__worker_loop` is 81% of CPU — almost
entirely the timed park. Driving this down further would mean targeted
per-worker signaling for every wake path. Architectural work;
substantial scope.

**Attempted 2026-05-15 and reverted.** The straightforward design —
per-worker (idle_mutex, idle_cv, wake_pending) + an O(num_workers)
idle-scan that signals one idle worker on every push (including the
from-worker fast path) — passed all tests and was TSAN-clean, but
regressed `spawn_chain` wall-time 5× (2.6 ms → 13 ms median at default
iters). Cause: `spawn_chain` is a *serial* chain — each iteration's
continuation lands on the submitter's own public_deque and the
submitter would have popped it on its next loop iteration. Waking an
idle peer forces a cross-worker steal + cache-line bounce for no
benefit. A minimal version (per-worker CVs, targeted pinned wake, no
from-worker signal) was perf-neutral but doesn't address the 81% CPU
symptom (workers still poll on the 1 ms timeout). See
`AUDIT_HISTORY.md` § "§11 attempt and revert (2026-05-15)" for the
diff and bench numbers.

**Lesson for the next attempt.** A real fix has to distinguish
spawn-chain serial workloads (don't wake peers — submitter handles
the continuation) from parallel workloads (wake peers — there's
genuine work to share). The submit site can't tell which it is.
Candidate strategies:
- Wake only when own deque depth exceeds a threshold (~2 items).
- Lazy wake: defer signaling by N ns, cancel if the submitter pops
  the work itself in the meantime.
- Per-worker "recently-active" hint: if my own deque was non-empty
  on my previous loop iteration, keep my new pushes local; only
  signal peers when I'm actively idling-down.

Each is its own multi-day project with bench validation. Defer until
a real workload (not `spawn_chain` microbench) actually pins this as
the bottleneck.

### Performance smells (deferred)

Profile after Phase H showed none of these are concentrated enough to
chase under current workloads. Listed for completeness; revisit only
if a real-workload profile pins them.

- ~~**`jacl_is_heap_type` tag chain (§13 sibling)**~~ — resolved
  2026-05-22 (commit `405932f`). Replaced the 18-way `||` chain with a
  single 32-bit bitmask test (`JACL_HEAP_TAG_MASK >> tag_idx & 1`).
  Box-churn bench wall-median improved 13.7%; string_concat improved
  40% (compounded with the rope ASCII fast path that landed the same
  day). Predicate now O(1) instead of O(18).
- **HAMT/RRB template metaprogramming via TLS heap pointer** — every
  collection mutation reads `gc__current_heap` from TLS. TLS reads are
  cheap but inhibit inlining of the alloc fast path. Tied to §17.
- **Worker loop steal-from-EVERY-worker on miss** — `O(num_workers)`
  failed steal attempts per idle iteration with no jitter. Randomized
  victim selection is standard for work-stealing; not surfaced at 2-4
  workers but would matter at higher counts.
- **`gc__find_fit_in_block` residual** — ~500 lines per admitted block
  on alloc-heavy scenarios (distance from `first_free_line` to the
  chosen run). Phase C territory (per-block free-run array). After H,
  this is no longer in the top of the profile.
- **`gc_sweep_concurrent` is 32% on `collection_churn`** — biggest
  remaining GC sink, but microbench artifact. A single-pass rebuild
  (combining Phase 1 zero + Phase 2 line-map rebuild, with stack-local
  live-line bitmap as `gc_sweep_minor` already does) could land
  ~30–40% off sweep CPU. Don't pursue without a real-workload profile.
- **`gc_sweep` / `gc_sweep_concurrent` main-body dedup** — ~80%
  duplicated; Phase D got the post-sweep summary epilogue but the main
  walker bodies stayed separate. Pure code health, no perf win, ~400
  LoC removable. Same for `gc_mark` / `gc_mark_minor`.

### Next-session perf candidates (2026-05-22 cross-runtime bench)

Pick-up hand-off. After the env inline-cache landed (see Closed items)
the standing vs CPython 3.x at 200 timed iters, 8 scenarios
(`docs/profiles/2026-05-22_jacl_ic_vs_python_compare.txt`, min metric):
JACL faster — spawn_chain ~16× · collection_churn ~1.7× ·
parallel_map_reduce ~1.9×. JACL slower — box_churn 1.74× ·
map_lookup_hot 1.56× · fib_recursive 1.62× · sieve_primes 2.0× ·
string_concat 12×.

The IC closed a notable chunk of the pure-arithmetic and HAMT-read
gaps (spawn_chain pulled away by another factor of 4 as a byproduct
of cheaper env access in the spawn machinery). The follow-up pass
that lowers top-level mut/def to depth-0 locals in chunks with a
hot loop and no closures (see Closed items below) shaved another
4 – 19 % off the env-bound scenarios:
collection_churn -18.7 %, box_churn -13.5 %, string_concat -6.4 %,
sieve_primes -4.1 %, map_lookup_hot ≈ tie.

A Clojure column was added on 2026-05-22 — see
`docs/profiles/2026-05-22_3way_summary.md` for the writeup and
`docs/profiles/2026-05-22_jacl_3way_compare.txt` for the raw table.
Highlights at 200 timed iters, median:

  - **collection_churn**: JACL ties Clojure (1.05×) and beats Python
    by 2.5×. Persistent-collection workloads are a real JACL strength
    now, not a Python-baseline artefact.
  - **parallel_map_reduce**: JACL ties Python; Clojure wins by 7× via
    JIT + JVM agent pool.
  - **spawn_chain**: JACL beats both — 3.6× over Python, 1.2× over
    Clojure futures. Lightweight spawn primitive paying off.
  - **fib_recursive / sieve_primes / map_lookup_hot**: Clojure wins
    20× / 33× / 3.6× respectively — pure JIT advantage on inner
    loops. No JIT → JACL pays full bytecode dispatch.
  - **string_concat**: still the biggest closeable gap (Python 18×,
    Clojure 5.6×) — candidate #1 below.
  - **box_churn**: Clojure 14× faster is suspicious — likely JIT
    escape-analysis eliding the atom; not a fair allocator comparison.

Ranked by leverage:

1. **string_concat algorithmic gap** — 18× slower than Python because
   CPython's BINARY_ADD has a refcount==1 in-place realloc fast path
   that turns the loop into amortized O(N) while JACL is honest O(N²).
   To close most of this gap: add an "owner-1 in-place mutation" path
   to JACL's concat — when the GC can cheaply prove the LHS isn't
   shared (e.g. it just came off a local slot with no other
   reachability), grow its buffer in place rather than allocating a
   fresh rope. Invasive (needs uniqueness info from the GC), large
   payoff.

2. **box_churn remaining 1.5× gap** — `gc_alloc` fast path is already
   at 0.6% self-time; the cost is downstream GC cycles from 385 MB
   cumulative bytes_allocated and per-cycle mark/sweep. A per-thread
   nursery / TLAB with bump-only allocation and cheap minor GC would
   eliminate most of those cycles entirely (most boxes die before
   they'd be collected anyway). Architectural — touches allocator,
   GC, root scanning, write barriers. Large work, large payoff for
   any alloc-heavy workload.

## Closed items

Pointers only — full bodies live in `AUDIT_HISTORY.md`.

- **OP_BOX error-check elision** — landed 2026-05-22. Added
  `OP_BOX_UNCHECKED` (skips the `jacl_is_error` propagation branch
  in OP_BOX's dispatch) and a `compiler__expr_is_error_free`
  predicate that returns true for `AST_LIT_INT` / `AST_LIT_FLOAT`
  / `AST_LIT_STRING` and for `HEAD_PLUS` / `HEAD_MINUS` / `HEAD_STAR`
  whose every arg is recursively error-free. `HEAD_BOX` emits
  the unchecked variant when the predicate fires; otherwise the
  checked OP_BOX. New `box_literal_churn.jacl` bench scenario added
  to exercise the path (boxes `[* 2 5]` per iter). Perf delta is
  under the run-to-run noise floor on this 4×200-iter bench setup
  (the elided branch is 1–2 cycles per box vs ~50 ns total cost of
  the `gc_alloc` + push), which matches the original audit notes
  ("Tiny per-op win; primarily an optimizer practice exercise").
  Value is the typer-driven optimization scaffolding for future
  use — e.g. if we ever extend the typer to track per-value error-
  freeness, more sites become eligible automatically. The opcode
  is kept at the end of `OpCode` (after `OP_ASSERT`) so existing
  numeric-stability asserts in `test_bytecode` stay valid.
- **Top-level mut/def → depth-0 local lowering** — landed 2026-05-22.
  A one-pass AST scan in `compiler_compile` sets two flags per chunk:
  `has_closure` (any proc, defmacro, spawn, parallel, race, await,
  yield, interpret, use anywhere) and `has_loop` (any while or for at
  any depth). When `!has_closure && has_loop`, the compiler's
  top-level `mut`/`def` branch routes through the existing
  scope-depth-greater-than-zero local path: the value stays on the
  stack as a depth-0 local, no `OP_DEF_GLOBAL` is emitted, and the
  name lives in `c->locals`. Subsequent `$name` reads and `set name`
  writes naturally resolve via `compiler__resolve_local` and emit
  `OP_GET_LOCAL` / `OP_SET_LOCAL` — single u8 read, single load, no
  env scan, no IC validation. The compiler then emits `OP_CLOSE_LOOP
  <N>` before the trailing `OP_HALT` (N = number of depth-0 locals)
  so direct `vm_exec` callers (tests, `jacl_eval`) still see a clean
  stack with just the chunk's final value at slot 0. The loop
  precondition keeps the embed/REPL `jacl_eval` flow on the env path
  so a follow-up `$x` chunk can still read a prior `def x 42`. The
  closure precondition keeps proc bodies, spawn continuations, and
  interpret callees observing top-level state through env as before.
  Min-of-min over 4×200-iter runs:
  collection_churn -18.7 %, box_churn -13.5 %, string_concat -6.4 %,
  sieve_primes -4.1 %, map_lookup_hot ≈ tie; fib_recursive,
  parallel_map_reduce, spawn_chain all skipped (closures present)
  and sit within machine-load variance of pre-lowering. Compounded
  with the IC, sieve_primes is now ~19 % faster than the pre-IC
  baseline. See
  `docs/profiles/2026-05-22_jacl_lowered_vs_ic_compare.txt`.
- **Inline cache for OP_GET_GLOBAL / OP_SET_GLOBAL** — landed 2026-05-22.
  Each `OP_GET_GLOBAL` / `OP_SET_GLOBAL` site now carries a 2-byte
  inline-cache slot (initial value 0xFFFF). On first dispatch the VM
  does the full env linear scan and patches the resolved env-slot index
  back into the bytecode; subsequent dispatches read the slot, validate
  `env.names[slot] == name` (catches cross-VM cases where workers
  running the same chunk have differently-laid envs), and skip the
  scan. `env_set` never reorders existing entries, so the (name, slot)
  pair is stable for the env's lifetime. IC hit rate ~99.99% across
  all eight bench scenarios. Min-of-min over 4×200-iter runs:
  spawn_chain -37.6 %, parallel_map_reduce -18.1 %, map_lookup_hot
  -15.6 %, fib_recursive -15.3 %, sieve_primes -10.5 %,
  string_concat -9.8 %, box_churn -2.2 %, collection_churn -1.7 %.
  See `docs/profiles/2026-05-22_jacl_env_ic_vs_baseline_compare.txt`.
  An earlier per-VM open-addressing cache attempt regressed all
  scenarios 23–39 % — the per-site cache won by being monomorphic
  (single dispatch site sees one name, hits 100 %) and by patching
  the slot into the bytecode (no hash, no probe, single u16 read).
- **C-style `for` vs `while` dispatch overhead** — investigated and
  closed 2026-05-22 as **not reproducing**. The prior "40% slower"
  claim pre-dated `be44681`'s inner-scope cleanup; matched-pair benches
  added under `test/jacl/bench/{box,tight}_{for,while}_bench.jacl`
  (reverted after the experiment) show `for` consistently within 1–3 %
  of `while`, with `for` very slightly *faster* across `box`-shaped
  (8 k inner, 500 timed iters: for 1205–1257 µs vs while 1229–1277 µs
  wall-min) and arithmetic-only (100 k inner, 200 timed: for 11.46 ms
  vs while 11.71 ms) workloads. Per-iter the C-style for emits the
  same opcode sequence as the while except for one extra
  `OP_CHECK_ERROR` after the step expression (compiler.c:8361) — ~2–3 ns,
  consistent with what we see. Eliding that check_error when the step
  is provably non-erroring would be a tiny micro-win; not worth a fix
  on its own.
- **§19 `rope_concat` cross-seam grapheme cluster join** — closed
  2026-05-19. The old `rope_concat` fix-up scanned only for
  Extend/ZWJ/SpacingMark at the start of right and migrated that
  prefix into left's last leaf, which dropped GB11
  (Extended_Pictographic ZWJ Extended_Pictographic) and
  GB12/GB13 (Regional_Indicator pairs) when the join crossed the
  seam. Replaced with a general detector: take the last grapheme
  of left's rightmost leaf, splice it with a 128-byte window from
  right's start, and run `unicode_grapheme_next` from offset 0 to
  learn how far left's last cluster actually extends. If the
  returned offset exceeds `left_tail_len`, the excess is the
  right-side prefix that needs to land in the junction. Covers
  GB9 / GB9a / GB11 / GB12 / GB13 / CR×LF / Hangul through the
  single UAX #29 forward walker. Regression coverage:
  `test_rope_concat_zwj_at_junction` (existing),
  `test_rope_concat_emoji_vs_zwj_at_junction` and
  `test_rope_concat_ri_pair_at_junction` (added), plus
  `test_rope_concat_two_flags_no_join` as a guard against an
  over-eager fix-up. Normal-mode baseline returns to 87/0, TSAN
  baseline to 86/1 (only `chase_lev_stress` remains, known-safe).
- **§21 `gc_concurrent_trigger` async-submit residual risk** —
  closed 2026-05-19 in `395aba1`. Collapsed the last remaining
  async-submit GC trigger to the synchronous shape used by the
  other two acquirers (`runtime__emergency_gc` and the worker-loop
  self-trigger). With all three sites identical, the pattern
  asymmetry that allowed §20 is gone. `gc__concurrent_task` had no
  callers and was deleted. `test_concurrent_gc_mutual_exclusion`
  rewritten to observe the mutex via `gc_running` + `global_epoch`
  rather than the now-removed `inbox_count` effect. See
  `AUDIT_HISTORY.md` § "§21 gc_concurrent_trigger async-submit
  residual (2026-05-19)".
- **§20 `chaos_soak` emergency-GC livelock** — closed 2026-05-19
  in `ef79087`. The worker-loop GC trigger at `runtime.c:498` used
  an async-submit pattern (CAS → `runtime_submit gc__concurrent_task`)
  that didn't match `runtime__emergency_gc`'s synchronous shape;
  on seeds where every worker fell into the alloc-failure spin
  before the queued task popped off any deque, the task stayed
  enqueued and `gc_running` stranded at 1. Fix: replace the
  submit with direct `gc_concurrent_collect(rt)` after the CAS.
  Verified across five distinct-seed `timeout 20 chaos_soak` runs
  (5–10 s each) where some seeds previously hung indefinitely.
  See `AUDIT_HISTORY.md` § "§20 chaos_soak emergency-GC livelock
  (2026-05-19)".
- **§18 `stream_type::stream_print` regression** — closed 2026-05-19
  in `a8fab24`. `VMFormatBuf` and `vm__fmt_init` had diverged
  between `src/jacl.h` (24-byte struct / 2-arg init) and `src/vm.c`
  (32-byte struct / 3-arg init). External callers stack-allocated
  the smaller version; the impl's 8-byte overflow corrupted the
  adjacent local. Fix: sync the public header to the impl. See
  `AUDIT_HISTORY.md` § "§18 stream_type::stream_print regression
  (2026-05-19)".
- **§16 VM stack overflow surfacing** — closed 2026-05-15 (cheap fix).
  Kept `VM_STACK_MAX = 256` / `VM_FRAMES_MAX = 64`. The ~44 bare
  `"stack overflow"` error strings in `vm.c` now resolve through one of
  two helpers — `vm__set_operand_overflow` (operand stack, with location
  tag) or `vm__set_frame_overflow` (call-frame depth) — each naming the
  limit numerically. SM/spawn/parallel/race completion in `runtime.c`
  now propagates `vm->error_message` to the awaiter via
  `runtime__make_completion_error` (with NULL-intern-table fallback for
  worker VMs). See `AUDIT_HISTORY.md` § "§16 VM stack overflow surfacing
  (2026-05-15)".
- **§13 VM dispatch (computed-goto / direct-threaded)** —
  closed 2026-05-14 (option 1: ifdef computed-goto, switch fallback
  for WASM). 222-entry direct-threaded dispatch table, ~2-3 % perf
  delta on `box_churn` at -O2; main value is the architectural lever.
  See `AUDIT_HISTORY.md` § "§13 VM dispatch (2026-05-14)".
- **Smaller items batch** — closed 2026-05-14: oom-panic public
  hook, redundant-`volatile` strip, `JACL_PACK_PTR` debug-asserting
  helper, stray `RCHeader` sweep. See `AUDIT_HISTORY.md` §
  "Smaller things" for the per-item resolutions.

### Known theoretical hole, not surfacing as a bug today

§9 recommendation #2 (capture mutable-load values into a per-worker
"stack root set" scanned by `gc_enumerate_roots`) is **still open**.
The write-side races are all closed; the *read-side* operand-stack-
rooting gap remains: a worker can hold a derived heap value on the
operand stack across a GC moment with no other root, relying on epoch
protection or the value's container still pointing to it. The SM
lowering de facto avoids this pattern (heap values live across
suspensions only via SM state fields, which the GC scans), but it's a
convention, not a runtime invariant. No concrete UAF has been
observed; revisit if one ever does, or if JACL grows beyond
suspension-only cross-task concurrency (e.g., if a worker ever runs
multi-step uninterruptible code that loads a heap value, mutates the
container, then re-touches the loaded value with no other root).

## Useful invocations

**Always wrap test invocations in a timeout.** The chaos suite — and
some `.jacl` scripts that drive workers — can occasionally hang on a
real deadlock or livelock; without a hard timeout, you'll lose
context noticing. `timeout 240 ./build.sh` is a reasonable ceiling
for the full normal-mode sweep on a current laptop; bump it under
TSAN. Per-test wrappers (`timeout 30 ./.build/<test>`) are good when
narrowing down a hang.

```sh
timeout 240 ./build.sh                  # full normal-mode test sweep
timeout 480 ./build.sh --tsan           # full TSAN-mode sweep
./build.sh --wasm                       # WASM compile-only check (emcc)
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

# Perf bench (regression + counters; see test_perf.c for scenarios):
./build.sh --test=perf
BENCH_TIMED_ITERS=2000 BENCH_WARMUP_ITERS=200 ./build.sh --test=perf

# Ad-hoc ASAN build for crash investigation:
cc -fsanitize=address -fno-omit-frame-pointer -g -O0 -D_DEFAULT_SOURCE \
   -o /tmp/jacl_asan src/jacl.c test/test_jacl_harness.c -lpthread -lm
/tmp/jacl_asan path/to/repro.jacl
```

## Chaos test inventory

All TSAN-clean as of end of audit campaign:

| Chaos test | Reproduces |
|------------|-----------|
| `chaos_soak` | Randomized 4×4 worker/driver load — catches general regressions |
| `chaos_pinned_deque` | §1 cross-worker MPSC violation on `private_deque` |
| `chaos_gc_deque_scan` | §2 GC scanning a Chase-Lev buffer without thief registration |
| `chaos_grey_buf` | §3 grey-buffer realloc vs. drain race |
| `chaos_gc_alloc_sweep` | §7 / §15 stale `current_block` and block-recycle races |
| `chaos_concurrent_intern` | §4 intern table sweep under concurrent GC |
| `chaos_satb_deref` | §9 SATB barrier deref-mutate-GC stress |

## How to use this file going forward

When working on an open item or filing a new one:

1. Cross-reference `SYNCHRONIZATION.md` for any shared-state implications.
2. Write a focused reproducer (`test/test_chaos_*.c` or a `.jacl` stress
   file) before fixing. Register it under `--tsan` if applicable.
3. After landing the fix: move the section's body to `AUDIT_HISTORY.md`
   as a closed entry, and either delete or "FIXED"-flag the entry here.
4. Update `SYNCHRONIZATION.md` §8 (Known unsoundness checklist) if the
   fix closes a synchronization gap.

The campaign methodology that produced the current state is preserved
in `AUDIT_HISTORY.md` under "How earlier phases were structured."
