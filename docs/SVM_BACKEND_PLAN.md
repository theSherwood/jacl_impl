# JACL on SVM — Migration Plan

> Status: **plan, pre-implementation.** Companion to `SVM_BACKEND_DESIGN.md` and
> `SVM_GC_CONTRACT.md`. This is an **all-or-nothing backend swap**, so it lives on
> its own branch (`svm-backend`) and does not disturb `main` until it can pass the
> existing test corpus on the new backend.

## 0. Where things stand

- **svm side: ready.** `vendor/svm` @ `7737c9e` ships `gc.roots` + the `ref`
  `ValType`; verified (`SVM_GC_CONTRACT.md`). The GC interface JACL needs exists.
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

- **Spike-1 — IR emission path (highest leverage).** Can JACL's C toolchain emit
  and assemble SVM IR? Prove: emit a hand-written `gc.roots`/arithmetic module as
  **text IR from C → assemble (`svm-text`) → run**, or via the **binary encoding**.
  If neither is C-reachable, decide **codegen-in-Rust** now. *This answer sets the
  6-vs-12-month shape (Design §4.4).*
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
- Decide & stand up the **runtime-as-guest** build (chibicc-compiled C runtime lib
  vs hand-written IR), per Design §4.5.

## 4. Phase 2 — codegen retarget

- Rewrite `compiler.c` emit sites: AST → SVM IR (keep the type-aware structure).
- Insert `gc_epoch` safepoint polls at back-edges/call sites (one poll, shared
  with svm's epoch check).
- Builtins dispatched as calls into the runtime library.
- **No** state-machine transform.

## 5. Phase 3 — concurrency on fibers

- `yield`/`await`/`parallel`/`race` → fiber `cont.*` switches.
- Re-platform the NxM Chase-Lev scheduler onto fibers + `thread.spawn` + futex.
- Convert blocking builtins to **async-form** capabilities (park the fiber).

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
| Builtin/runtime semantics (from `vm.c`) | ~12k | **re-homed** as guest code |
| Scheduler (`runtime.c`) | ~2.3k | **re-platformed** on fibers |
| GC (`gc.c`/`gc_collect.c`) | ~2.8k | **rewritten** (simpler: STW non-moving) |
| Bytecode dispatch + `bytecode.c` | — | **deleted** |
| SM transform (~3k in `compiler.c`) | ~3k | **deleted** |

Rough shape: **rewrite ~25–30k, preserve ~14k.** One-person, multi-month (≈6+,
front-loaded by the spikes); Spike-1's answer can push it toward 12.

## 9. Risk register

- **R-Toolchain (high):** no C-reachable IR emission → codegen-in-Rust pivot
  (language split, bigger effort). *Mitigation: Spike-1 first.*
- **R-Runtime-in-chibicc (med):** JACL's runtime C may exceed chibicc's subset.
  *Mitigation: identify early; fall back to IR or trim C.*
- **R-Perf (med):** dynamic dispatch + conservative GC may eat the JIT win.
  *Mitigation: measure after parity, not before.*
- **R-Determinism (low/known):** conservative GC ⇒ backend-divergent occupancy
  (R1); out of the differential oracle. Precise GC is the reserved escape hatch.
- **R-Scope creep (med):** it's all-or-nothing — resist partial cutover. Keep it
  on-branch until the corpus is green.

## 10. Open questions

1. IR emission path (Spike-1) — text vs binary vs Rust codegen.
2. Runtime-as-guest packaging — one chibicc-compiled lib vs IR, and how it links
   with codegen output.
3. Fiber stack sizing / quota policy for many lightweight JACL tasks (no auto
   stack growth; stack probes needed for large frames).
4. How JACL's sandboxed `interpret` maps onto SVM domains/capabilities.
5. Whether/when precise GC becomes a goal (determinism, long-uptime defrag).
