# JACL on SVM — Backend Rewrite Design

> Status: **design, pre-implementation.** Tracks the architecture for retargeting
> JACL's backend from its bytecode VM to **SVM** (`vendor/svm`, register-SSA
> sandbox VM with a Cranelift JIT). Companion docs: `SVM_BACKEND_PLAN.md` (phased
> plan, risks, spikes) and `SVM_GC_CONTRACT.md` (the GC interface, already landed
> on the svm side).

## 1. Why

SVM is our own VM (`github.com/theSherwood/vm`), the platform-independent layer
beneath a future plan9-like OS. Building JACL on it:

- **One VM to maintain, not two.** JACL's bytecode VM + epoch GC + state-machine
  concurrency are replaced by SVM's interpreter/JIT + fibers; we delete most of
  JACL's hardest runtime.
- **JACL as a concrete consumer** that hardens SVM (it already drove the
  `gc.roots` + `ref` work).
- **JACL as the eventual shell** for the SVM/plan9 layer — gradual typing is a
  good fit for gluing typed programs.
- **Bonus:** Cranelift JIT (potential perf win over the bytecode interpreter),
  capability sandboxing (aligns with JACL's `interpret` sandbox), and stackful
  fibers (kill the state-machine transform).

## 2. What SVM is (the target)

- **Register-based SSA IR over a CFG**; reference interpreter + Cranelift JIT,
  differential-tested.
- **Linear window memory** (~2⁴⁰ reserved), pointers are window offsets, grown via
  the `Memory` capability. **No GC, no managed object model** — guests bring their
  own.
- **Capabilities / powerbox** for the host boundary; `cap.call` is the host ABI.
- **Stackful fibers** (`cont.new`/`cont.resume`/`suspend`, ~ns switch, uncapped,
  quota-metered) + 1:1 OS threads + C11 atomics + futex. **No built-in scheduler**
  — M:N scheduling is guest policy.
- Targeted today via **text IR** (chibicc C frontend proves it) and a binary
  encoding; the IR-building API is the Rust `svm-ir` crate. (See Plan §Spike-1 —
  how *JACL's C compiler* emits IR is the #1 open question.)

## 3. What is preserved, retargeted, deleted

Grounded in the backend audit (project ~59.8k LOC; backend ~42.7k, frontend
~14.1k).

### Preserved (~14k, frontend) — unchanged
`lexer.c` (1.8k), `parser.c` (2.5k), `ast.c` (1.0k), **`typer.c` (7.2k)**,
`syntax.c`/macros (1.4k). The typer is a read-only AST pass with **no backend
callbacks**, so it survives intact — the single biggest gift.

### Preserved conceptually — value representation
`value.c` (~0.9k). JaclVal is an 8-bit tag + 56-bit payload. SVM pointers are
offsets into a ~2⁴⁰ window → **fit in 56 bits with room to spare**, so heap refs
port directly; no boundary translation. The heap-tag bitmask trick carries over.

### Retargeted (~18k) — codegen
`compiler.c` minus the SM transform. Same structure (type-aware AST walk), but
every emit site changes from **bytecode → SVM IR**, plus it must:
- insert **safepoint polls** (`gc_epoch`) at back-edges/calls (GC + STW), and
- model suspension as **fiber switches** instead of SM state spills.

### Re-homed (~12k) — builtin & runtime semantics
The semantics of JACL's ~239 opcodes (vec/map/string/arith/struct/box/atom/…) do
**not** vanish with the dispatch loop — they move into **guest runtime functions**
(C compiled to IR via chibicc, or hand-written IR), running inside the sandbox.
Plus the collections (RRB vec, HAMT map) and string interning, re-homed over the
linear-window heap.

### Re-platformed (~2.3k) — scheduler
`runtime.c`'s NxM Chase-Lev work-stealing design ports onto SVM fibers +
`thread.spawn` + futex. Structure reusable; primitives change.

### Deleted (the wins)
- **Bytecode VM dispatch + `bytecode.c`** (the dispatch loop; SVM executes now).
- **State-machine transform (~3k in `compiler.c`)** → replaced by fibers.
- **Epoch GC concurrent machinery** — grey buffers, concurrent sweep, the **40
  write-barrier sites in `vm.c`**, generational minor GC, Immix concurrency.
  A simple STW non-moving mark-sweep needs none of it.

## 4. Key design decisions

### 4.1 Values & heap
- JaclVal tagging unchanged; heap pointers = window offsets (56-bit payload).
- The guest owns a heap sub-range `[heap_lo, heap_hi)` of the window, grown via
  `Memory.map`. JACL keeps Immix-style block/line maps for object-start +
  interior-pointer resolution.

### 4.2 GC — conservative, non-moving, STW (see `SVM_GC_CONTRACT.md`)
- **Conservative roots** (JACL exposes raw C pointers, so values aren't
  tag-filterable). Root-finding is an svm service: `gc.roots(heap_lo, heap_hi, …)`
  returns deduped in-window candidate words across all fibers' stacks/registers.
- **No shadow stack, no write barriers, no safepoint root-spilling in codegen** —
  the cost the rewrite was meant to avoid is gone; codegen only emits the
  `gc_epoch` safepoint poll.
- **STW is cooperative & guest-coordinated** (Go-style epoch flag + per-vCPU park
  + futex barrier); the GC is the scheduler.
- Tradeoffs accepted: lose no-STW (was epoch concurrent), keep non-moving; heap
  occupancy is backend-divergent and out of the differential oracle (R1).

### 4.3 Concurrency — fibers replace the state-machine transform
- `yield`/`await`/`parallel`/`race` become **fiber suspends/switches** (`cont.*`),
  not compile-time SM state spills. This deletes JACL's most bug-prone subsystem.
- M:N scheduler (Chase-Lev work-stealing) runs over SVM fibers + OS threads +
  futex; fiber migration across vCPUs is supported.
- Obligation: blocking host ops must use **async-form capabilities** (park the
  fiber), never block a vCPU mid-`cap.call` during STW.

### 4.4 Toolchain — how JACL emits SVM IR (THE open question)
JACL's compiler is C; SVM's IR-builder is Rust (`svm-ir`). Candidate paths
(resolve in Plan Spike-1):
- **(a) Emit text IR from C**, assemble via `svm-text` (if available as a
  CLI/C-callable lib). Most portable; re-parsing cost.
- **(b) Emit the binary encoding from C** (`svm-encode` format) directly.
- **(c) Rewrite codegen in Rust** against `svm-ir`. Most native, biggest pivot.
This single answer largely sets the effort (6-month vs 12-month shape).

### 4.5 Runtime-as-guest
Builtins/GC/value-ops run inside the sandbox: compiled from C via **chibicc**
(subset C — JACL's runtime C must fit) or written as IR. Likely hybrid: codegen
emits IR for program logic; the runtime library is compiled once and linked.

### 4.6 Forward-compat
The `ref` `ValType` is reserved in SVM now (lowers as `i64`). If precise GC is
ever wanted (deterministic heap state for the model checker; evacuation/defrag for
long-lived shell processes), add Cranelift stack maps then — frame pointers are
already preserved, so only the maps are missing.

## 5. Non-goals
- No SVM-side GC or object model (mechanism-not-policy stays).
- No moving/compaction now (non-moving; revisit only with precise GC).
- No preemptive any-PC stop-the-world (cooperative only — would add escape-TCB).
