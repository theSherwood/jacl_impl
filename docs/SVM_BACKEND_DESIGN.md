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

### 4.3.1 Generators & streams — fibers, not the SM transform

JACL's current state-machine transform exists to give generators **one heap
allocation per stream, zero allocations per yield** (it replaced a CPS path that
allocated closures per yield). **Fibers preserve that property** — a `yield` is a
`suspend`/`resume` stack switch, allocation-free — *and* add deep-yield (yield
through a helper call) for free, which the SM transform never supported. So the
original motivation for the SM does not argue for keeping it over fibers.

Decision:

- **Built-in stream combinators are stackless iterator objects, not coroutines.**
  `map`/`filter`/`take`/`enumerate`/`lines`/`collect` are small pull-based `next`
  steppers (Rust-iterator / transducer style) — no fiber, no SM. A pipeline like
  `!cmd | lines | filter | take 10` is a chain of `next`-stepping heap objects;
  it's the fastest option (the `next` inlines) and pays no per-stream stack cost.
  This is where most "stream" usage lives, so it removes most footprint pressure.
- **User-defined `yield` generators run on fibers.** Simple, deep-yield-capable,
  allocation-free per yield; fine for the realistic workload (a few live pipelines,
  or one generator producing many values).
- **Delete the general SM transform** — the most bug-prone subsystem (state-field
  layout, wide-rep boundaries, inline-struct bitmaps, transitive coloring).
- **Deferred optimization, not now: a *narrow* stackless-generator lowering.**
  Only if profiling shows a real problem, add a restricted lowering for the
  **flat** generator shape (yields at its own lexical level, no deep yield) into a
  small heap state object; deep-yielding generators fall back to fibers. It is a
  fraction of today's complexity *because fibers handle the general case*
  (await/parallel/race/deep-yield), so it never deals with coloring, wide reps, or
  the concurrency primitives. **Named triggers to revisit:** (1) many
  simultaneously-live user generators / high stream churn where per-fiber stacks
  dominate memory; (2) extremely hot trivial-body generator loops (two stack
  switches per element vs. on-stack dispatch); (3) a need to **persist or migrate
  suspended generators** (svm `durable`/`snapshot`) — a heap state object
  serializes; a fiber stack does not.

### 4.4 Toolchain — how JACL emits SVM IR

JACL's compiler is C; SVM's IR-builder is Rust (`svm-ir`) — but the **text IR**
is the decoupling interface, and SVM's own C frontend (**chibicc**) already emits
it. Two distinct producers:

- **JACL runtime library** (GC, scheduler, builtins, value ops, fiber glue):
  written in C, compiled by **chibicc → text IR**. See §4.5 — chibicc already
  lowers the svm ops we need as `__vm_*` builtins.
- **JACL program codegen** (JACL source → IR): JACL's compiler emits **text IR**
  (path (a), what chibicc itself emits) or, later, the **binary encoding**
  (path (b)) for speed. Either is a pure serialization concern — **no C-callable
  binding to svm's Rust is required**; the two producers' output is assembled and
  linked into one module. (Rewriting codegen in Rust against `svm-ir`, path (c),
  stays the fallback if the text path proves inadequate — but the chibicc
  precedent makes that unlikely.)

Spike-1 still confirms the text assembler (`svm-text`) is reachable from our
toolchain (CLI or lib) and that JACL-program-codegen + runtime modules link.

### 4.5 Runtime-as-guest — C via chibicc (already wired)

Builtins/GC/value-ops run inside the sandbox as guest code, written in **C and
compiled by chibicc**. Crucially, chibicc **already exposes the svm ops JACL needs
as C builtins** that lower straight to IR — verified in `vendor/svm`
(`frontend/chibicc/codegen_ir.c`):

- **Fibers:** `__vm_fiber_new` / `__vm_fiber_resume` / `__vm_fiber_suspend`
  (→ `cont.new`/`cont.resume`/`suspend`) — the generator + suspension substrate.
- **Threads:** `__vm_thread_spawn` / `__vm_thread_join` — the M:N executor.
- **Atomics + futex:** `__vm_atomic_*` (incl. `cas32`) and `__vm_wait32` /
  `__vm_notify` — the lock-free scheduler + the cooperative-STW barrier.
- **Memory:** `__vm_map` / `__vm_unmap` / `__vm_protect` / `__vm_page_size` — the
  heap allocator's window growth.
- **Async I/O + caps:** `__vm_io_submit_async` / `__vm_io_reap` /
  `__vm_blocking_handle` (async-form blocking ops, JACL obligation #2) and
  `__vm_cap*` (the host boundary).

**The one gap:** there is no `__vm_gc_roots` builtin yet (the `gc.roots` *op*
exists in the IR/interp/JIT; chibicc just doesn't surface it). Adding it follows
the existing `gen_builtin_*` pattern and is the single concrete svm-side C-interface
ask (see `SVM_BACKEND_PLAN.md`). With that, the GC, scheduler, and fiber-based
generators are all writable in plain C against chibicc builtins — **no new
Rust↔C bindings required.**

### 4.6 Forward-compat
The `ref` `ValType` is reserved in SVM now (lowers as `i64`). If precise GC is
ever wanted (deterministic heap state for the model checker; evacuation/defrag for
long-lived shell processes), add Cranelift stack maps then — frame pointers are
already preserved, so only the maps are missing.

## 5. Non-goals
- No SVM-side GC or object model (mechanism-not-policy stays).
- No moving/compaction now (non-moving; revisit only with precise GC).
- No preemptive any-PC stop-the-world (cooperative only — would add escape-TCB).
