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

## Current state (2026-05-15)

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
- Normal-mode suite: 88 pass / 0 fail (`./build.sh`).
- TSAN baseline: 86 pass / 2 fail (`./build.sh --tsan`).
  `jacl_harness` and `chase_lev_stress` are the two **documented as
  known-and-safe** — TSAN blindness on barrier/fence/epoch-mediated
  synchronization, not missed barriers.
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

- **`jacl_is_heap_type` tag chain (§13 sibling)** — 0.10% of mixed CPU.
  Either already inlined or genuinely cheap. The originally-suggested
  256-entry tag-byte LUT isn't worth it.
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

## Closed items

Pointers only — full bodies live in `AUDIT_HISTORY.md`.

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
protection or the value's container still pointing to it. JACL's CPS
compiler de facto avoids this pattern (heap values live across
suspensions only via SM state fields, which the GC scans), but it's a
convention, not a runtime invariant. No concrete UAF has been
observed; revisit if one ever does, or if JACL grows beyond
CPS-only concurrency.

## Useful invocations

```sh
./build.sh                              # full normal-mode test sweep
./build.sh --tsan                       # full TSAN-mode sweep
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
