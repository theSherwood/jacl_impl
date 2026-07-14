# Asks for svm — Phase 3 prerequisite (per-vCPU heap identity)

> **STATUS: RESOLVED** (svm `main` ≥ `7a82f64`, PR #79 "per-vCPU TLS register").
> svm shipped exactly Ask 1 — and a touch better than option 1. See the
> "Resolution" note at the bottom; the rest of this doc is kept as the original
> ask for the record.

> For the svm side. **One** change unblocks JACL's Phase 3 P3.4 (the M:N
> work-stealing scheduler with real OS-thread workers): a way for guest code to
> learn **which worker/vCPU it is running on**. The runtime and codegen are
> otherwise ready; this is the single missing primitive for a high-performance,
> per-worker allocator.
>
> Context: P3.1–P3.3 (generators, `spawn`/`await`, `parallel`/`race`) and the
> single-thread GC quiesce (P3.6) are done cooperatively on one OS thread. P3.4
> turns the workers into real `__vm_thread_spawn` OS threads, which needs a
> **thread-safe allocator**. We agreed on JACL's existing design — **per-worker
> (per-vCPU) heaps with a lock-free bump fast path** — not a per-allocation lock.
> (See `SVM_BACKEND_PHASE3.md` §P3.4.)

## Ask 1 — a guest-visible thread/vCPU self-id (the blocker)

**Problem.** The per-vCPU allocator needs each worker to index its own heap:
`per_worker[id].bump`. That requires the running code to know its own `id`
**cheaply, anywhere** (the allocator fast path runs on every `jacl_alloc`). svm
today exposes **no** way to get that:

- **No C thread-locals.** svm-llvm does not lower TLS (`__thread` /
  `thread_local`), so the classic `__thread ThreadHeap *current_heap` the old VM
  uses (`src/gc.c`) cannot be expressed.
- **No self-id intrinsic.** The `__vm_*` surface has atomics
  (`__vm_atomic_add`, `__vm_atomic_cas32`), a futex (`__vm_wait32` /
  `__vm_notify`), and `__vm_thread_spawn` / `__vm_thread_join`, but nothing that
  returns "which vCPU/OS-thread am I." `__vm_thread_spawn(funcidx, sp, arg)`
  returns the handle **to the spawner**, not an identity to the spawnee.
- **No frame-address / stack-base intrinsic** either, so a worker can't even
  derive an identity from its own stack region.

We can thread a **worker id as the spawn `arg`** and pass it explicitly down the
data-SP ABI — and the current P3.4 design does exactly that as the fallback — but
it means **every** function on a worker carries an extra `id` parameter purely to
reach the allocator, which is invasive and easy to get wrong across fiber
resume/suspend boundaries (a fiber may migrate workers under work-stealing, so a
captured-at-spawn id is *stale* after a steal). A cheap "ask the VM where I am
**now**" removes both problems.

**The change.** Add one of (in preference order):

1. **A self-id intrinsic** — `long __vm_thread_self_id(void)` (or
   `__vm_vcpu_id`), returning a small dense index `0..N` for the current OS
   worker/vCPU, stable for the duration of a `thread.spawn` body and **re-read
   after any fiber resume** (so a stolen fiber sees its new home). This is the
   minimal, fastest, migration-correct option.

   ```c
   long __vm_thread_self_id(void);  // 0..num_workers-1 for the running vCPU
   ```

2. **TLS lowering** — make svm-llvm lower `__thread` / `thread_local` to a
   per-OS-thread slot. More general (lets the allocator stash a whole
   `ThreadHeap*`, matching the old VM verbatim), but a larger change and still
   needs care so a migrated fiber re-reads the slot on its new thread.

Option 1 is sufficient and is what we'd build on: index `per_worker[id]` and
re-read `id` at each allocation safe point.

**Acceptance.** A guest spawns `N` workers via `__vm_thread_spawn`; inside each,
`__vm_thread_self_id()` returns a distinct value in `0..N-1`; the value is stable
across calls within a worker and, for a fiber resumed on a different worker after
a steal, reflects the **current** worker. A small test: N workers each bump-
allocate into `per_worker[self_id()]` concurrently with no atomics on the fast
path and no cross-worker corruption (the per-vCPU allocator's core invariant).

## What JACL builds on top (no further svm asks)

Everything else P3.4 needs already exists in svm and is referenced in
`SVM_BACKEND_PHASE3.md`:

- **Real OS-thread workers:** `__vm_thread_spawn` / `__vm_thread_join`
  (`Inst::ThreadSpawn`, supported on interp + JIT).
- **Shared region-pool refill + free-lists:** `__vm_atomic_cas32` /
  `__vm_atomic_add` (coarse mutex now, lock-free Treiber list later).
- **Multi-vCPU STW quiesce:** the guest futex barrier pattern from svm's
  `crates/svm/tests/gc_quiesce.rs` ("quiescing the N threads quiesces the M
  fibers") — no new svm primitive.
- **Conservative roots over every parked fiber:** `gc.roots` already scans all
  suspended fibers' control + data stacks (svm's
  `gc_roots_scans_suspended_fiber_stack` / `gc_roots_enumerates_every_parked_fiber`).

## Summary

| # | Ask | Size | Gates |
|---|---|---|---|
| 1 | `__vm_thread_self_id()` (or TLS lowering) | small–med | per-vCPU lock-free allocator (P3.4) |

With Ask 1, JACL implements per-vCPU TLAB allocation (lock-free bump, shared
region pool, cooperative STW quiesce) directly — the agreed best-perf design —
with no throwaway global-allocator step. Until it lands, P3.4 implementation is
paused; P3.1–P3.3 + P3.6 (cooperative concurrency) are done and unaffected.

## Resolution (svm `7a82f64`, PR #79)

svm added an **ambient per-vCPU TLS register**, exposed to C exactly as the ask's
preferred option (and slightly stronger):

```c
long __vm_vcpu_tls_get(void);   // current vCPU's i64 TLS word
void __vm_vcpu_tls_set(long x); // overwrite it
```

- **Read at the execution point**, so a fiber that migrated to another vCPU reads
  the *current* vCPU's word — the migration-correctness property we specifically
  called out for work-stealing (svm's `vcpu_tls_tracks_current_vcpu_across_migration`
  test covers exactly this).
- **Seeded to a dense id** at vCPU creation (root 0, children sequential), so a
  bare `get` before any `set` doubles as a `vcpu.id` — what we'd index
  `per_worker[id]` with.
- **Or a full per-CPU pointer:** the guest may `set` it to a pointer to its own
  per-CPU block, giving `__thread`-style TLS (lets us stash a whole `ThreadHeap*`
  the way the old VM does, without C TLS).

Wired through ir / encode (`0xEB`/`0xEC`) / text (`vcpu.tls.get|set`) / verify /
interp / jit / svm-llvm (`__vm_vcpu_tls_get` / `__vm_vcpu_tls_set`) / peval.
Verified on the JACL side: the C intrinsics translate (svm-llvm
`vm_vcpu_tls_round_trip`) and the svm migration/seed tests pass at the pinned
commit. **P3.4 is unblocked.**

> Note: the same bump (svm `9b0d163`) also **widened fiber handles i32 → i64**
> (`cont.new` yields i64, `cont.resume` takes an i64 handle, status stays i32; the
> C on-ramp is now `long __vm_fiber_new` / `long __vm_fiber_resume(long, …)`).
> `runtime/fiber.c` was updated to store i64 handles accordingly — required for the
> module to verify against the new pin.

---

## Ask 2 — persistent M:N pool — **WITHDRAWN: no svm change needed**

> **STATUS: WITHDRAWN.** Two earlier drafts of this ask were both wrong. (a) "`gc.roots`
> coverage bug" — wrong: `gc.roots` is sound (the transient `jacl_sched_run_batch` uses it and
> is rock-solid, `mt.rs::gc_sched`). (b) "need fiber migration" — wrong: **svm already provides
> cross-vCPU fiber migration, and it is tested *with* GC.** So the persistent M:N pool (the
> P3.4d keystone) is **not blocked on svm at all** — the remaining work is entirely JACL-side.
> Kept here, withdrawn, for the record. Full root-cause + reproducer: `spikes/svm_pool_gc/`.

**Why the persistent pool corrupted (root cause — all JACL-side).** Three causes, found by
isolation in `spikes/svm_pool_gc/`:
1. *main-side roots* — `jacl_parallel` held in-flight roots (the closures vector, freshly
   allocated futures, the result vector, register transients) on **main's** fiber stack while
   pool workers collected; `gc.roots` doesn't cover the main thread's stack for a collection
   elected on another vCPU. **Fixed** by global root buffers + main-allocates-only-when-sole-mutator.
2. *result-handoff ordering* — a plain i64 result store wasn't ordered before the atomic DONE
   flag under svm-llvm + real JIT threads. **Fixed** by atomic-halves publish/read.
3. *main's program-fiber in the multi-worker quiesce set* — unlike the transient pool (main
   unregisters and parks on its bare thread, never a running fiber during a collection), the
   persistent pool runs main as a program fiber that **cycles suspend/resume** while awaiting,
   leaving rare windows the strict `gc.roots` trips (`FiberFault`, ~1/40 on real-thread JIT).

**Why #3 needs no svm change.** The clean fix for #3 is a **continuation scheduler**: a wait
suspends the program/task fiber back to a bare scheduler loop (so it is `RUNNABLE`, *off* the
workers — never `RUNNING` while a worker collects), and a ready fiber is resumed by whatever
worker grabs it. That needs cross-vCPU fiber **migration** (resume a fiber on a different vCPU
than suspended it), which **svm already implements and tests on both backends**:

- `cont.resume` claims a fiber from a run-shared registry, "**possibly one suspended on another
  vCPU (D57 migration)**" — interp `svm-interp/src/bytecode.rs`; JIT `fiber_rt::fiber_resume`:
  "**any vCPU may resume** a fresh (`OWNED`) or suspended (`RUNNABLE`) fiber … even when another
  OS thread suspended it (the migration edge)."
- Tests: `svm/tests/fiber_migrate.rs::fiber_suspended_on_root_resumes_on_spawned_vcpu`,
  `foreign_vcpu_claim_succeeds_without_a_race`, `racing_resumes_have_exactly_one_winner`;
  `fiber_fuzz.rs::generated_migration_schedules_agree_on_interp_and_jit`; and `gc_roots.rs` /
  `gc_quiesce.rs` exercise a stop-the-world `gc.roots` scan **across spawned vCPUs** (with the
  §3.3 "refuse if another vCPU holds a *running* fiber" check verified *not* to fire when fibers
  are properly suspended — exactly the continuation scheduler's invariant).

**Remaining work (JACL-side, no ask):** build the continuation scheduler on svm's existing
migration — `await`/wait suspends the awaiting fiber to a bare per-worker loop instead of
busy-cycling; workers resume ready fibers (migrating them); keep #1's global-buffer rooting and
#2's atomic handoff. Then `parallel`/`race`/`spawn`/`await` move onto the persistent pool and
the async/parking builtins (`sleep`/channels) follow. Tracked in `SVM_BACKEND_PHASE3.md` P3.4d.

---

## Ask 3 — `gc.roots` coverage contract for guest values on stacks/registers

> **STATUS: RESOLVED — by contract, not a VM change.** svm's answer: **a vCPU top (its bare
> native / `thread.spawn`-entry stack, e.g. our worker loop) must hold no heap roots at a
> safepoint — push them into a fiber or the window before parking.** `gc.roots` scans suspended
> fibers + the window (marked guest memory); it does **not** scan vCPU-top native frames or
> unspilled registers, and won't (no register-flush shim). This is the existing model, now
> written down as a contract. That's exactly the "give JACL a contract to target" option of this
> ask, so it's answered.
>
> **JACL already complies, and is sound.** The `jacl_all_jobs` registry + ready queues +
> `running_job` are the "window": every job root lives in marked guest memory, never relied upon
> from a bare vCPU stack. `mt::par_gc` passes 10/10 on the JIT. (The earlier experiment that
> *relied* on the implicit stack scan for jobs held only in a runtime C local — `parallel`'s
> `futs[]` — hung, which is precisely the contract's point: don't hold roots there.)
>
> **Remaining is JACL-side, not svm:** the registry's cap/leak is a *reclamation* problem (a
> DONE job may be a held, re-awaitable future) — solved by refcounting jobs or moving result
> ownership out of the job object, both independent of svm. Original ask kept below for the
> record.

---

### Original ask (answered above by the vCPU-top contract)

**Context.** JACL runs an M:N scheduler on `cont.*`: a persistent pool of `thread.spawn` vCPU
workers, each running a bare worker loop that resumes job fibers. The guest GC finds heap roots
via `gc.roots` at a stop-the-world safepoint.

**Problem.** We cannot rely on `gc.roots` to find guest heap pointers that live on stacks — they
get swept mid-collection. Confirmed with a reproducer: when scheduler "job" objects are rooted
only via their natural references (a worker-loop local, a `parallel` block's `futs[]` array on
the awaiting fiber, the program root reachable through a waiter chain), live jobs — including the
program root — get collected, hanging/corrupting the run. Marking the obvious guest globals
(ready queues, per-worker current-job, root-job pointer) is **not** enough; only copying *every*
job pointer into a guest-global array and marking it by hand works. That array can't be safely
reclaimed (a DONE job may be a held, re-awaitable future whose only reference is a stack slot we
can't prove dead), so it leaks / is capped.

`gc.roots`' own doc already flags one gap: *"live roots a caller holds only in unspilled
callee-saved registers … are out of scope (a register-flush shim is a future follow-up)."*

**Please confirm coverage at a STW `gc.roots` (driven by one vCPU) for each:**
1. Values live only in **unspilled callee-saved registers** — of the calling frame, of suspended
   fibers, and of resume-chain ancestors.
2. A **non-collector spawned vCPU's bare native-thread (root-computation) stack** — i.e. its
   `thread.spawn` entry frames (our worker loop), where a job pointer sits between dequeuing it
   and resuming its fiber. Is `root_entry_sp` recorded and that region scanned for spawned vCPUs,
   not just the main one?
3. A **suspended fiber's full saved C-frame chain** (e.g. a `parallel` local array on a fiber
   parked in `await`).

**Ask.** Document the exact `gc.roots` coverage contract (which locations × which vCPUs/fibers are
guaranteed scanned at STW), and close the gaps — in particular the already-flagged register-flush
shim, and (2) if spawned-worker native stacks are not covered. Then a guest scheduler can keep
roots in normal locals instead of a hand-marked, un-reclaimable global table.

**Repro (JACL, this repo).** `spikes/svm_pool_gc/` (analysis) + `runtime/tests/test_par_gc.c` via
`cargo test --release --test mt par_gc` (JIT). With the explicit job registry it passes 10/10;
relying on the stack scan (registry removed, queues+running+root marked) it hangs 0/8 —
`runtime/sched.c` `jacl_sched_mark_roots` / `jacl_all_jobs`.
