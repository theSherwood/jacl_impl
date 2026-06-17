# JACL on SVM — Migration Plan

> Status: **plan, pre-implementation.** Companion to `SVM_BACKEND_DESIGN.md` and
> `SVM_GC_CONTRACT.md`. This is an **all-or-nothing backend swap**, so it lives on
> its own branch (`svm-backend`) and does not disturb `main` until it can pass the
> existing test corpus on the new backend.

## 0. Where things stand

- **svm side: ready.** `vendor/svm` @ `7737c9e` ships `gc.roots` + the `ref`
  `ValType`; verified (`SVM_GC_CONTRACT.md`). The GC interface JACL needs exists.
- **Runtime path: LLVM, not chibicc.** The runtime is C compiled via **`svm-llvm`**
  (clang `-O2` → LLVM bitcode → SVM IR), which is production-mature (8 real libs
  compile byte-identical to native clang, incl. clay). This lets us **reuse JACL's
  existing runtime C** (value ops, `lib/` collections, strings, bignum, builtins)
  instead of re-authoring it, with `-O2` optimization. Linked against
  JACL-emitted program IR via svm static linking (`svm_ir::link`, `DYNLINK.md`).
  (See Design §4.4/§4.5.) chibicc is dropped (spec/oracle only).
- **The one concrete svm-side ask: SATISFIED** (svm `f75509a`, slices AC/AD/AF).
  `svm-llvm` now lowers the full `<svm.h>` `__vm_*` surface — **35 builtins** incl.
  `__vm_gc_roots` → `gc.roots`, the fiber ops → `cont.*`, atomics/futex/threads,
  and the P2 memory/async-I/O/caps. Verified by inspection of the lowering bodies +
  the dedicated tests (`vm_gc_roots_smoke`, `vm_fibers_generator`,
  `vm_atomics_*`, `vm_threads_atomic_counter`); the `__vm_gc_roots` signature
  matches the ask exactly. (Tests run in svm CI; not runnable in this sandbox —
  missing LLVM-18 dev headers.) Our submodule is pinned at this version.
- **JACL side: not started.** Frontend (lexer/parser/typer/macros) and the value
  representation are the assets we carry forward; everything below the AST is
  rewritten.

## 1. Guiding constraints

- **Preserve the frontend** (~14k LOC) and the JaclVal representation; rewrite
  codegen + runtime + GC + scheduler.
- **Behavior parity is the bar:** success = the existing JACL test corpus passes
  on the SVM backend (modulo the explicitly-allowed differences, e.g. GC
  occupancy per R1).
- **De-risk before committing bulk effort.** Three cheap spikes gate the project.

## 2. Phase 0 — spikes (gate the whole effort; ~1–2 weeks)

Do these *first*; a bad answer here changes the project's size or viability.

- **Spike-1 — program IR emission (highest leverage).** Prove JACL's compiler can
  emit SVM IR directly: a hand-built module as **text IR → assemble (`svm-text`) →
  run through the verifier + interp**, then **static-link** (`svm_ir::link`) a
  program module against a separately-compiled runtime module. Confirms the emit +
  link path our codegen depends on. (Binary `svm-encode` emission is a later speed
  swap behind the same builder.) *Sets the project shape (Design §4.4).*
- **Spike-1b — runtime via `svm-llvm` + the intrinsic surface.** (a) Compile a
  non-trivial slice of existing JACL runtime C (e.g. an `lib/` collection) through
  `svm-llvm` and run it. (b) Land the svm-llvm intrinsic surface for
  `__vm_gc_roots` + `cont.*` and prove a C runtime fn can call `gc.roots` and
  resume a fiber. Gates Spike-2/Spike-3. *(Depends on the svm handoff ask.)*
- **Spike-2 — GC model.** Prototype a non-moving mark-sweep over a linear-window
  heap arena + block/line maps, using `gc.roots` for roots and the cooperative
  STW handshake. Tiny heap, a couple of fibers. Confirms the riskiest design point
  end-to-end (and that obligation #1 safepoints suffice).
- **Spike-3 — fibers replace SM.** Port exactly one suspending primitive
  (`yield`) onto a fiber and confirm it does what the SM transform did, simpler.
  Validates the core simplification thesis.

**Exit criteria:** all three green → proceed. Spike-1 or Spike-2 fighting back →
reassess scope (or stay on the bytecode VM).

## 3. Phase 1 — runtime substrate (guest-side)

- Heap allocator over the `Memory` window + Immix block/line maps.
- Non-moving conservative mark-sweep wired to `gc.roots` + cooperative STW.
- JaclVal value ops re-homed; string interning + RRB-vec/HAMT-map over the new
  heap.
- Built-in stream combinators (`map`/`filter`/`take`/`lines`/`collect`/…) as
  **stackless iterator objects** — no fibers, no SM (Design §4.3.1).
- Stand up the **runtime build**: existing/ported runtime C → clang `-O2` →
  `svm-llvm` → IR artifact; static-linked against program IR (Design §4.5).

## 4. Phase 2 — codegen retarget

- Rewrite `compiler.c` emit sites: AST → SVM IR (keep the type-aware structure).
- Insert `gc_epoch` safepoint polls at back-edges/call sites (one poll, shared
  with svm's epoch check).
- Builtins dispatched as calls into the runtime library.
- **No** state-machine transform.

## 5. Phase 3 — concurrency on fibers

- `yield`/`await`/`parallel`/`race` → fiber `cont.*` switches; user `yield`
  generators run on fibers (Design §4.3.1). **No SM transform.**
- Re-platform the NxM Chase-Lev scheduler onto fibers + `thread.spawn` + futex.
- Convert blocking builtins to **async-form** capabilities (park the fiber).
- (Deferred, only on a named trigger: a narrow stackless-generator lowering —
  Design §4.3.1.)

## 6. Phase 4 — bring-up & parity

- Run the existing C test corpus + `test/jacl/*.jacl` against the SVM backend.
- Triage divergences; remember GC-occupancy/`check_no_leaks`-style assertions are
  **not** backend-portable (R1 / obligation #4) and must be reframed to
  reachability + program output.
- Perf pass (Cranelift JIT) once correct.

## 7. Phase 5 — cutover

- Only when the corpus is green on SVM: make SVM the default backend, retire the
  bytecode VM (`vm.c` dispatch, `bytecode.c`), the SM transform, and the epoch GC.
- Merge `svm-backend` → `main`.

## 8. Effort & what changes (from the audit)

| Bucket | LOC | Disposition |
|---|---:|---|
| Frontend (lexer/parser/ast/typer/macros) | ~14k | **preserved** |
| Value representation (`value.c`) | ~0.9k | preserved conceptually |
| Codegen (`compiler.c` minus SM) | ~18k | **retargeted** to SVM IR |
| Builtin/runtime semantics (from `vm.c`) + `lib/` collections | ~12k+ | **ported + recompiled** via `svm-llvm` (substrate swaps, not re-authored) |
| Scheduler (`runtime.c`) | ~2.3k | **re-platformed** on fibers |
| GC (`gc.c`/`gc_collect.c`) | ~2.8k | **rewritten** (simpler: STW non-moving) |
| Bytecode dispatch + `bytecode.c` | — | **deleted** |
| SM transform (~3k in `compiler.c`) | ~3k | **deleted** |

Rough shape: **rewrite ~22–26k, preserve ~14k, port-recompile ~12k.** The LLVM
path moves the bulk of the builtin/collection semantics from "re-author" to
"port + recompile," which is the biggest single effort reduction. One-person,
multi-month, front-loaded by the spikes.

## 9. Risk register

- **R-svm-llvm-intrinsics — RESOLVED** (svm `f75509a`): the on-ramp now lowers the
  full `__vm_*` surface incl. `__vm_gc_roots`/`cont.*`/atomics/futex/threads. No
  longer blocking.
- **R-Runtime-port (med):** existing runtime C needs substrate swaps (window
  allocator, conservative GC, fibers-for-threads) before it recompiles cleanly;
  some C may hit svm-llvm gaps (e.g. varargs `printf`, not yet supported).
  *Mitigation: port the substrate-touching layer first; most pure-compute C is
  proven to compile.*
- **R-LLVM-builddep (low):** clang/LLVM is a build-time dep for the runtime
  artifact. *Mitigation: prebuilt/checked-in artifact; end users don't need LLVM.*
- **R-Perf (med):** dynamic dispatch + conservative GC may eat the JIT win.
  *Mitigation: measure after parity, not before.*
- **R-Determinism (low/known):** conservative GC ⇒ backend-divergent occupancy
  (R1); out of the differential oracle. Precise GC is the reserved escape hatch.
- **R-Scope creep (med):** it's all-or-nothing — resist partial cutover. Keep it
  on-branch until the corpus is green.

## 10. Open questions

1. Program IR emission (Spike-1) — text-then-binary behind one builder; confirm
   `svm-text` reachable and `svm_ir::link` merges program + runtime modules.
2. Runtime packaging — granularity of the `svm-llvm` artifact(s) and the
   symbol/ABI convention linking JACL-emitted program IR to runtime functions.
3. Fiber stack sizing / quota policy for many lightweight JACL tasks (no auto
   stack growth; stack probes needed for large frames).
4. How JACL's sandboxed `interpret` maps onto SVM domains/capabilities.
5. Whether/when precise GC becomes a goal (determinism, long-uptime defrag).
