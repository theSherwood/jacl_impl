# Ask for svm: expose the concurrency/GC ops in the LLVM on-ramp

> **STATUS: SATISFIED — svm `f75509a` (slices AC = P0+P1+Memory, AD = P2, AF =
> SharedRegion).** `svm-llvm` now lowers the full `<svm.h>` `__vm_*` surface
> (35 builtins) incl. `__vm_gc_roots` → `gc.roots`, `__vm_fiber_*` → `cont.*`,
> atomics/futex/threads, memory, async-I/O, caps — each to its existing IR op, with
> dedicated tests (`vm_gc_roots_smoke`, `vm_fibers_generator`, `vm_atomics_*`,
> `vm_threads_atomic_counter`). The original request is preserved below for record.

> For the svm side. Small, bounded request. Context: JACL is building its backend
> on svm and will compile its **runtime** (allocator, GC, scheduler, value ops,
> collections) as C → `clang -O2` → LLVM bitcode → **`svm-llvm`** → SVM IR, then
> static-link (`svm_ir::link`) against JACL-emitted program IR. This replaces the
> chibicc path (chibicc stays as spec/oracle only).

## The gap

`svm-llvm` is production-mature for ordinary C (the 8-lib corpus runs
byte-identical to native clang). But it currently lowers **only ordinary C** — a
grep of `crates/svm-llvm/src/lib.rs` shows **zero** handling for `cont.*` (fibers),
atomics, futex (`wait`/`notify`), threads, or `gc.roots`. JACL's runtime needs all
of these. The corresponding **IR ops already exist** in `svm-ir`/interp/JIT (and
`gc.roots` landed recently, opcode `0xEA`); they're just not reachable from the
LLVM frontend.

## The request

Teach `svm-llvm` to recognize a fixed set of **external symbols** (declared, never
defined — same idea as the chibicc `__vm_*` builtins) and lower a call to each into
the corresponding SVM IR op, instead of an ordinary `call`.

- **Mechanism precedent:** this is the same shape `svm-llvm` already uses to bind
  `write`/`read`/`exit` to §7 named imports and to synthesize `__svm_memset`/
  `__svm_memcpy` helpers — just matching on call target and emitting an op.
- **Authoritative signatures:** mirror chibicc's `__vm_*` table verbatim —
  `frontend/chibicc/codegen_ir.c`, the `strcmp(fname, "__vm_…")` dispatch
  (~lines 1431–1495) and the `gen_builtin_*` bodies. Same names, same C
  signatures, same target ops. (LLVM intrinsics instead of magic symbols are fine
  if you prefer; JACL only needs *some* stable surface.)

## Symbols, by priority

**P0 — gates JACL Spikes 1b/2/3 (GC + fibers):**

- **`__vm_gc_roots`** — *the one with no chibicc precedent; define it here.*
  Suggested C: `long __vm_gc_roots(long heap_lo, long heap_hi, void *buf, long cap)`
  → lower to the `gc.roots` op (`heap_lo, heap_hi, buf, cap -> count`). Semantics
  per JACL's `SVM_GC_CONTRACT.md` / svm `GC.md` §3.
- **`__vm_fiber_new` / `__vm_fiber_resume` / `__vm_fiber_suspend`**
  → `cont.new` / `cont.resume` / `suspend` (signatures per chibicc).

**P1 — scheduler (M:N executor, lock-free deques, cooperative-STW barrier):**

- **`__vm_atomic_*`** — `add`/`load`/`store` (64-bit) + `load32`/`store32`/
  `add32`/`cas32` → the atomic ops.
- **`__vm_wait32` / `__vm_notify`** → futex wait/notify.
- **`__vm_thread_spawn` / `__vm_thread_join`** → `thread.spawn` / `thread.join`.

**P2 — memory + host boundary (allocator growth, I/O builtins):**

- **`__vm_map` / `__vm_unmap` / `__vm_protect` / `__vm_page_size`** → the `Memory`
  ops. (Note: `svm-llvm`'s `malloc` already grows the window; the GC's own arena
  allocator + guard/RO pages need the raw `map`/`protect` surface.)
- **`__vm_io_submit_async` / `__vm_io_reap` / `__vm_blocking_handle`** and
  **`__vm_cap` / `__vm_cap_count` / `__vm_cap_at`** → async-form blocking + the
  capability boundary (JACL's I/O builtins; the async forms are how a fiber parks
  without blocking its vCPU during STW).

## Acceptance

- A C function calling each symbol lowers to the intended op (not a `call`),
  passes the verifier, and runs **interp == JIT == expected**, plus text/binary
  round-trip — the existing `svm-llvm` test convention.
- End-to-end smoke: a C runtime fn that (a) creates and resumes a fiber, and
  (b) calls `__vm_gc_roots` over a small window range and reads back candidates.

## Out of scope

Varargs `printf` formatting and other general-C breadth — JACL's runtime doesn't
need them. No new IR ops, no TCB/backend semantics change: this is purely a
frontend lowering surface over ops that already exist.
