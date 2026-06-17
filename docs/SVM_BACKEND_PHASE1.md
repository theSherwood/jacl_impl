# JACL on SVM — Phase 1 scope: the runtime substrate

> Status: **scoping, pre-implementation.** Branch: `svm-backend-phase1`. Companion
> to `SVM_BACKEND_DESIGN.md`, `SVM_BACKEND_PLAN.md` (§3 Phase 1), and
> `SVM_GC_CONTRACT.md`. Phase 0 (all four spikes) is **complete and merged** — the
> emit/verify/link path, reuse-existing-C, fibers-replace-SM, and the conservative
> non-moving GC model are validated on interp + JIT. Phase 1 turns the proven
> mechanisms into the real **runtime library** that JACL programs will link against.

## Goal

Produce a **JACL runtime IR artifact** — values, heap, GC, strings, collections,
stream iterators, and the pure-computation builtins — written in C, compiled
`clang -O2 → svm-llvm → SVM IR`, with a test suite green on interp + JIT. When
Phase 1 is done, Phase 2 (JACL program codegen) has a substrate to emit calls
against.

**Definition of done:** `svm_ir::link`-ing a small program module against the
runtime artifact and running it on interp + JIT exercises: allocate values/objects;
trigger a GC that reclaims garbage and keeps the reachable graph; build/read
persistent vec + map; intern strings; iterate a stackless stream pipeline; and call
the arithmetic/comparison/collection builtins — all passing, with the runtime test
suite green.

## Runtime layout

New top-level `runtime/` tree (the old `src/` bytecode VM stays intact until the
Phase 5 cutover):

```
runtime/
  jaclrt.h            # JaclVal, tags, object headers, public runtime API
  value.c             # tag/untag, predicates (ported from src/value.c)
  heap.c              # window allocator + block/line maps (uses __vm_map)
  gc.c                # conservative non-moving mark-sweep (gc.roots-driven)
  string.c            # inline / interned / rope strings + intern table
  collections.c       # rrb_vec + hamt + arr instantiations over the GC heap
  stream.c            # stackless iterator combinators
  builtins.c          # arith/compare/type/vec/map/string value-op semantics
  build.sh            # runtime C -> clang -O2 -emit-llvm -> svm-llvm -> artifact
  tests/              # C-driver "programs" linked against the runtime, run on interp+JIT
```

`<svm.h>` (from the submodule) supplies the `__vm_*` builtins (`__vm_map`,
`__vm_gc_roots`, atomics, …). Reuses `lib/rrb_vec`, `lib/hamt`, etc. as headers.

## Sequencing principle

- **Single-vCPU GC now.** Spike-2's model (conservative roots via `gc.roots` from
  the collector's call chain) works single-threaded. The **multi-vCPU cooperative
  STW barrier** (`gc_epoch` + futex across N vCPUs) is deferred to **Phase 3**,
  where the scheduler/fibers land — the GC's `gc_epoch` poll is stubbed as a no-op
  until then. (svm already proved the cross-vCPU `gc.roots` scan separately.)
- **No JACL program codegen yet.** Phase 1 tests use **C-driver "programs"**
  (compiled via svm-llvm, like the spikes) standing in for Phase 2's emitter.
- **Reuse, don't re-author.** Per Spike-1b, the collections and value ops are
  ported C; new code is the allocator, GC, and the substrate glue.

## Concrete steps

### P1.0 — Runtime build + link/test harness
- **Goal:** a repeatable build that turns `runtime/*.c` into one IR artifact, and a
  test harness that `svm_ir::link`s a C-driver program against it and runs on
  interp + JIT.
- **Deliverable:** `runtime/build.sh` (→ artifact) + a Rust test harness promoted
  from the spikes (`spikes/svm_*` are the prototypes).
- **Validates:** the whole pipeline as a real target (not ad-hoc spikes).
- **Depends on:** nothing (mechanisms proven in Phase 0).

### P1.1 — Value representation
- **Goal:** `JaclVal` for the svm world — 8-bit tag + 56-bit payload; heap pointers
  are window offsets (fit 56 bits; Design §4.1). Port `value.c` tag macros,
  predicates, the heap-tag bitmask.
- **Deliverable:** `jaclrt.h` + `value.c`; unit tests for tag/untag, type
  predicates, flag bits.
- **Validates:** values round-trip; predicates correct on interp + JIT.
- **Depends on:** P1.0.

### P1.2 — Window heap allocator
- **Goal:** a real allocator over the Memory window (grow via `__vm_map`), object
  headers, and **block/line maps** (Immix-style) for object-start + interior-pointer
  resolution and sweep. Resolves the `vm_map` import sidestepped in Spike-2.
- **Deliverable:** `heap.c` — alloc, heap-walk, object-start query; tests
  (allocation, growth across `__vm_map`, header integrity).
- **Validates:** alloc/grow correct; object-start query exact (no false starts).
- **Depends on:** P1.1.
- **Note:** Spike-2 found svm-llvm rejects a *constant* `ptrtoint` of a global —
  route heap-base→int through a `noinline` helper (or revisit as an svm-llvm ask).

### P1.3 — Conservative non-moving GC
- **Goal:** the real mark-sweep — conservative roots via `gc.roots`, **precise heap
  tracing** via object headers/type layout (Design §4.2), sweep over the block/line
  maps. Scale Spike-2 to real headers, real object types, real allocator.
- **Deliverable:** `gc.c` — `gc_collect`, mark stack/worklist, sweep; the
  `gc_epoch` safepoint poll (no-op stub until Phase 3). Stress tests: large graphs,
  cycles, churn, allocate-during-collect ordering.
- **Validates:** reclaims garbage, retains reachable graph intact across many
  cycles; interp == JIT (occupancy itself is out of the oracle per R1 — assert
  reachability + program output, not exact occupancy).
- **Depends on:** P1.2.

### P1.4 — Strings
- **Goal:** three-tier strings (inline / heap-interned / rope) + intern table over
  the GC heap. Port `src/string.c`.
- **Deliverable:** `string.c`; tests (intern identity, concat, rope, GC interaction).
- **Depends on:** P1.3.

### P1.5 — Collections
- **Goal:** port `lib/rrb_vec` (Spike-1b proved it compiles), `lib/hamt`, `arr`, and
  the typed variants onto the **GC heap** — wire their `*_GC_ALLOC` hooks to the real
  allocator and make their node layouts **traceable** by the GC.
- **Deliverable:** `collections.c`; tests for persistence + collecting mid-structure
  (a vec/map survives and stays intact across a GC; dropped versions are reclaimed).
- **Depends on:** P1.3.

### P1.6 — Stream combinators (stackless iterators)
- **Goal:** `map`/`filter`/`take`/`enumerate`/`lines`/`collect` as pull-based
  `next`-stepping iterator objects — **no fibers, no SM** (Design §4.3.1).
- **Deliverable:** `stream.c`; tests (composed pipelines, early termination).
- **Depends on:** P1.5.

### P1.7 — Pure-computation builtins
- **Goal:** port the value-op semantics from `src/vm.c`'s opcode handlers — arith,
  comparisons, type ops, vec/map/string ops — into callable runtime functions.
- **Deliverable:** `builtins.c`; tests per family. (The bulk; may spill into Phase 2
  as codegen reveals the exact call surface.)
- **Depends on:** P1.1–P1.6.

## Carried constraints (from Phase 0 / the contract)

- **Block-local SSA** (Spike-1) is a *Phase 2* codegen obligation; irrelevant to the
  C runtime (svm-llvm handles it).
- **Spilled roots** (obligation #1): the multi-vCPU safepoint poll is a Phase 3 item;
  Phase 1's single-vCPU GC scans the `gc.roots` caller chain (worked in Spike-2).
- **`ptrtoint` of globals**: use `noinline` cast helpers in the runtime (P1.2).
- **`llvm-18-dev`** is required in the build env for `svm-llvm` (libLLVM linkage).

## Deferred (not Phase 1)

- JACL program codegen + the JACL-owned IR builder (text→binary) → **Phase 2**.
- Fibers, generators, the M:N scheduler, and the multi-vCPU cooperative STW barrier
  → **Phase 3**.
- Cutover / deleting the old VM → **Phase 5**.

## Exit criteria

The DoD scenario passes on interp + JIT, the `runtime/tests` suite is green, and the
runtime artifact builds reproducibly via `runtime/build.sh`. Then Phase 2 begins.
