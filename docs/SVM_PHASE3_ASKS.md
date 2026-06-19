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
