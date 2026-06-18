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

## Progress

- **P1.0 harness ✅** — `runtime/harness` (Rust): compiles a C driver + runtime via
  `clang -O2 → svm-llvm`, runs `run` (func 0) on interp + JIT, asserts agreement.
  `cargo test` is the runner. Whole-program compilation (see Sequencing).
- **P1.1 values ✅** — `runtime/jaclrt.h`: `JaclVal` tag scheme mirroring
  `src/jacl.h`; inline ctors/predicates/extractors; `test_values.c` green.
- **P1.2 allocator + P1.3 GC ✅** — `runtime/heap_gc.c`: size-class free lists +
  bump + per-granule object-start bitmap; conservative non-moving mark-sweep with
  roots via `gc.roots`, **interior-pointer resolution**, and per-class reuse.
  `test_gc.c` (reachable graph incl. a trace-only leaf, n varied-size garbage,
  two cycles with reuse) green on interp + JIT.

  Two findings (both expected for conservative GC, both load-bearing):
  1. **Interior pointers are mandatory.** At `-O2` the program holds a pointer to an
     object's *payload* (`cell + header`), not its start, so the mark phase must
     resolve any candidate to its enclosing object (back-scan the start bitmap), not
     require an exact start.
  2. **The runtime must be opaque to the program optimizer.** Inlined into a
     deterministic test, the allocator's object addresses constant-fold, so roots
     stop being live stack values and `gc.roots` can't see them. The real backend
     compiles the runtime separately (opaque by construction); here `jacl_alloc` is
     `noinline` to reproduce that. **Implication for Phase 2:** keep the
     runtime↔program boundary a real call boundary (separate artifact / no
     cross-boundary inlining of allocation).

- **Backing store:** static 16 MiB region for now; `__vm_map`-grown window pages
  are a follow-up needing the powerbox harness (the algorithm is unchanged).

Next: P1.4 strings, P1.5 collections (wire `lib/rrb_vec` + `lib/hamt` GC hooks to
this allocator + make nodes traceable), P1.6 stream iterators, P1.7 builtins.

- **P1.4 strings ✅** — `runtime/string.c` (+ `jaclrt.c` unity): inline (≤7B, value),
  heap (`JOBJ_STR`), and interned strings over the GC heap. Interning introduces a
  **runtime root set** (the intern table) the collector marks via
  `jacl_mark_runtime_roots` / `jacl_gc_mark` (obligation #3) — strong interning, so
  interned strings survive GC via the table (weak interning is a later refinement).
  `test_strings.c` (inline/heap/intern/eq) and `test_strings_gc.c` (interned string
  survives collection while n non-interned heap strings are reclaimed) green on
  interp + JIT.

  Findings: svm-llvm provides `memcpy`/`memset` but **not** `bcmp`/`memcmp`/`strcmp`
  — hand-roll byte compares in runtime + tests. And clang merges chained i64 tag
  compares into an **i64 `switch`** that svm-llvm can't lower (only i32) — runtime
  **tag dispatch must switch on the i32 type index** (`jaclrt_type_index`), not
  chained `(v & TAG_MASK) == const` tests. (Candidate svm-llvm enhancement: i64
  `switch`/`br_table`.)

- **P1.5 collections (vector) ✅** — `runtime/collections.c`: the real `lib/rrb_vec`
  instantiated for `JaclVal`, with node alloc → `jacl_alloc(JOBJ_NODE)` (traced) and
  size-tables → `JOBJ_BLOB`. A vector value is `JACL_TAG_VECTOR` over the RRB root;
  GC reachability flows value → root → internals → leaves → elements. `test_vec.c`
  (build/get/persistence) and `test_vec_gc.c` (a vector of heap-string elements —
  reachable only via the vector — survives collection intact and is stable across a
  second collect, while intermediate persistent versions + external garbage are
  reclaimed) green on interp + JIT. (Map/HAMT is the remaining P1.5 piece.)

  **Key finding — `gc.roots` vs tagged values (an svm ask).** JACL stack roots are
  **tagged JaclVals** (`VECTOR<<56 | offset`, …) whose full word sits far above the
  raw heap range, so `gc.roots`' range filter on the raw word **drops them** (raw
  `JaclObj*` roots pass; tagged ones don't). Phase-1 workaround: widen the range
  past the max heap-tagged value and mask each candidate in the collector (re-admits
  host addresses — an ASLR smell, harmless here since we mask + range-check). **The
  proper fix is a `gc.roots` payload-mask parameter** so tagged-pointer guests keep
  the tight range and svm returns masked offsets — file as an svm enhancement before
  Phase 2 / production.

  Lesser findings: persistent `push` creates intermediate versions that become
  garbage, so tests assert reachable-survival + stability, not exact reclaim counts;
  `rrb_vec.h`'s `assert()` needs `-DNDEBUG` (added to the harness clang flags).

- **P1.6 stream iterators ✅** — `runtime/stream.c`: stackless, pull-based
  combinators — `range`/`vec` sources, `map`/`filter`/`take`, and `collect` — each a
  heap object (`JOBJ_NODE`, `JACL_TAG_STREAM`) holding its source + state; `next`
  pulls from the source. **No fibers, no SM** (Design §4.3.1). Transforms/predicates
  are C function pointers (funcrefs) — stand-ins for the JACL closures that arrive
  with Phase-2 codegen; stored in the payload as code refs (never in the heap-data
  range, so conservative tracing skips them). `test_stream.c`
  (range|map|filter|take|collect + vec source + empty take) and `test_stream_gc.c`
  (a held pipeline keeps its source vector + string elements alive across a
  collection, then collects to the right result) green on interp + JIT.
  (`enumerate`/`lines` are the remaining combinators.)
