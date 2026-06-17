# SVM GC Support Contract (conservative, non-moving)

> **Status: CONVERGED — svm side landed & verified.**
> svm implemented this contract as of `vendor/svm` @ `7737c9e`
> (`GC.md` on the svm side is the mirror). Verified here on 2026-06-15:
> `cargo test -p svm --test gc_roots` → **8/8 pass**, including suspended-fiber
> scan on interp *and* JIT, the cross-vCPU stop-the-world scan, "enumerates every
> parked fiber," and a multi-fiber soundness sweep. The `ref` `ValType` is present
> and lowers as `i64` (zero codegen/perf delta).
>
> This document is JACL's copy of the interface. svm's `GC.md` is authoritative for
> svm-side semantics; keep the two in sync.

## Model

JACL runs a guest-side **non-moving, conservative mark-sweep** GC over a heap it
owns in the linear window (grown via the existing `Memory` cap). JACL keeps its
own block/line maps for object-start and interior-pointer resolution.
**svm implements no GC and no object model** — it provides only fiber **root
enumeration**; world-stop is guest-coordinated. Roots are conservative because
JACL exposes raw C pointers (no tag filtering possible).

## Division of labor

- **JACL owns:** heap + allocator, mark/sweep, object/line maps, the M:N
  scheduler, world-stop coordination, and scanning of its own **in-window data
  stack** + heap + GC structures (all guest-addressable).
- **svm provides:** enumeration of roots living on **control stacks +
  saved-register blocks** (JIT) / **frame value vectors** (interp) — the only
  place JACL cannot reach (out-of-band, unnameable by guest masking).

## Stop-the-world: cooperative, guest-coordinated (no async preemption)

The GC *is* the scheduler. "Stop the world" = stop resuming user fibers and let
running ones reach existing safepoints and `suspend`. Coordinate the **N vCPUs**,
not the M fibers: a vCPU re-enters its scheduler (where it parks) only *after*
its current fiber suspended/returned, at which point that fiber's roots are
flushed to its control stack (`suspend` is call-clobbering). So "all N vCPUs
parked" ⟹ "no fiber running" ⟹ every fiber scannable.

Handshake is **pure guest code** over svm's window-atomics + `memory.wait/notify`
(Go-style: bump `gc_epoch`; mutator fibers poll at safepoints and `suspend`; each
vCPU parks instead of resuming; collector waits for `parked == N-1`, scans,
signals done). **No new svm world-stop primitive required.**

## The svm primitive: range-filtered root enumeration

```
gc.roots(heap_lo, heap_hi, buf, cap) -> count   // ambient IR op (opcode 0xEA)
//   writes min(count, cap) distinct in-window candidate words (u64, ascending)
//   to guest buffer `buf`; count > cap ⇒ retry with a larger buffer
```

svm walks every fiber **not actively executing guest mutator code** (all parked
fibers + the `gc.roots` caller; under STW that is every fiber) and returns the
deduped words inside `[heap_lo, heap_hi)`.

**Required properties (all implemented):**
1. **Range-filtered** — only in-window candidates cross the boundary; host return
   addresses / frame pointers / host pointers are filtered *inside* svm. No host
   ASLR/layout leak.
2. **Value-only, deduplicated** — non-moving needs reachability not positions;
   never exposes a raw control-stack view.
3. **Coverage = every non-mutating fiber, including the caller** (`gc.roots` is
   call-clobbering, so the collector's own live roots are spilled at the call
   site and scanned like any parked fiber).
4. **Mechanism, not policy** — svm does the window range-check; JACL does
   object-start validation, interior-pointer resolution, and marking.
5. **Representation** — `heap_lo`/`heap_hi` and candidates are **window offsets**
   (JACL's raw-pointer representation).

### Agreed semantics (R1–R5, from the contract negotiation)

- **R1 — Over-approximation is sound but backend-divergent.** Interp scans typed
  `Value` frames; JIT scans raw native-stack words (more false positives). Sound
  for non-moving (a falsely-retained object is never freed and never moves).
  **Consequence: GC heap occupancy is NOT part of the interp↔JIT differential
  oracle — only program output is.**
- **R2 — svm scans the `gc.roots` caller too** (the call is the collector's own
  safepoint; svm reads the caller's out-of-band control stack on its behalf).
- **R3 — Quiescence is a caller precondition** svm can't cheaply enforce; sound
  only under STW. Optional svm assert: refuse if another vCPU holds a RUNNING
  fiber.
- **R4 — Defined semantically, realized per-backend.**
- **R5 — Window-offset representation throughout.**

## JACL-side obligations

1. **Safepoint polls** at loop back-edges + call sites — no un-polled tight loops
   (else STW stalls; svm has no async escape hatch by design). Piggyback the
   `gc_epoch` poll on the same sites used for svm's epoch check.
2. **Blocking host ops use async-form caps** (which park the fiber → scannable);
   never sit in a long synchronous host `cap.call` during STW; no reentrant guest
   execution while stopped.
3. JACL scans its own **data stack + heap + GC structures**; it relies on svm
   only for control-stack / saved-reg roots.
4. **Do not assert exact GC occupancy across backends** (mirror of R1). JACL GC
   tests may assert program output and liveness of *reachable* objects, but not
   that a given garbage object was reclaimed under both interp and JIT.

### JIT "spilled-only" contract (svm GC.md §7)

The JIT walker scans regions whose roots are already flushed to memory — parked
fibers (spilled at `suspend`), resume-chain ancestors, and the `gc.roots` caller
(forced to spill by the call boundary). Roots held *only* in unspilled
callee-saved registers of a non-spilled frame are out of scope (a register-flush
shim is a noted svm follow-up). **Fully covered under cooperative STW** — every
scanned fiber is either `suspend`-parked or the calling collector — provided JACL
honors obligation #1 (reach a safepoint/spill before collection).

## Forward-compat: precise GC later

- **Reserved now:** the `ref` `ValType` (opaque 64-bit, lowers as `i64`) — already
  in svm. Prevents a format break when precise stack maps arrive.
- **Deferred until precise GC is committed:** Cranelift stack maps + per-PC
  value-location metadata. Drivers to revisit: deterministic heap state for the
  model checker, or evacuation/defrag for long-lived heaps.
