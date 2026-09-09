# Asks for temen — Phase 2 prerequisites (separate-artifact linking + tagged-root GC)

> For the temen side. Three changes that unblock JACL's Phase 2 (program codegen). The
> **runtime is unchanged**; these are about linking a separately-compiled runtime
> artifact to JACL-emitted program IR, and about finding JACL's tagged roots. None
> blocks JACL-side codegen from *starting* (the whole-program harness compiles
> program + runtime in one TU), but all three gate the real separate-artifact path
> and clean GC. Priority order below.
>
> Context: JACL compiles the runtime as C → `temen-llvm` → an TEMEN IR module that
> exports `jacl_*`; programs are emitted as TEMEN IR that calls those via
> `call.import`; the two are joined with `temen_ir::link`. (See
> `TEMEN_BACKEND_PHASE2.md`.)

## Ask 1 — `temen-llvm`: expose a name→index export map (small)

**Problem.** `temen_ir::Func` carries no name, and `temen_llvm::Translated` is just
`{ module, entry_sp }`. So after translating the runtime, JACL can't tell which
function index is `jacl_add` etc. — and `temen_ir::link`'s `LinkUnit.exports` is
`Vec<(String, FuncIdx)>`, which needs exactly that mapping.

**The change.** `temen-llvm` already computes `defined_names: HashMap<String, u32>`
during translation (≈`crates/temen-llvm/src/lib.rs:217`). Surface it:

```rust
pub struct Translated {
    pub module: Module,
    pub entry_sp: u64,
    pub exports: Vec<(String, u32)>,   // NEW: defined function symbol -> index
}
```

**Acceptance.** Translate a 2-function module; `exports` contains both names with
their module indices; feeding them as `LinkUnit { module, exports, .. }` to
`temen_ir::link` resolves a `call.import` of those names. Zero codegen/runtime effect.

## Ask 2 — an `temen-llvm-translate` CLI (small/medium)

**Problem.** Translation only runs in-process (the JACL test harness). To produce a
real, reusable runtime artifact, JACL needs a command-line step: bitcode → a
serialized TEMEN IR module + its export map.

**The change.** A thin bin target (e.g., `temen-llvm-translate`) that:
`clang`-produced `.bc` → `temen_llvm::translate_bc_path` → write the module as **text
IR** (`temen-text`) and/or **binary** (`temen-encode`), plus the export map (Ask 1) as a
sidecar (`name idx` per line, or JSON).

```
temen-llvm-translate runtime.bc -o runtime.temen --emit-syms runtime.syms
```

**Acceptance.** `runtime/build.sh` runs `clang … -emit-llvm` then
`temen-llvm-translate` to produce `runtime.temen` + `runtime.syms`; JACL loads the
module (`temen-text`/`temen-encode` decode) + syms, builds a `LinkUnit`, and
`temen_ir::link`s a program module against it — the separate-artifact analogue of
Spike-1 (which used hand-written text units).

## Ask 3 — `gc.roots`: a payload-mask parameter (medium; the important one)

**Problem.** `gc.roots(heap_lo, heap_hi, buf, cap)` range-filters on the **raw**
stack word. But JACL's stack roots are **tagged JaclVals** — the type tag lives in
the high byte, so a heap value is `(TAG << 56) | offset`, whose full word is far
above `heap_hi` and gets dropped. Raw pointers pass; tagged values (i.e., *all*
real JACL roots) don't. JACL's Phase-1 workaround widens the range past the
max-tagged value and masks each candidate itself — which **re-admits host
addresses** to the returned set (an ASLR smell), since the raw words now cross the
boundary.

**The change.** Add a guest-supplied **mask**, applied before the range check, and
return the **masked** value:

```
gc.roots(heap_lo, heap_hi, mask, buf, cap) -> count
//   for each control-stack/register word w:
//     m = w & mask
//     if heap_lo <= m < heap_hi: emit m   (deduped, ascending)
```

JACL passes `mask = 0x00FF_FFFF_FFFF_FFFF` (clear the tag byte). Then:
- a **tagged** JaclVal → `m = offset` → in range → returned;
- a **raw** heap pointer (tag byte 0) → `m = w` → returned (covers C-frame roots);
- a **host address** → masked low-56 bits are (essentially never) in the guest heap
  sub-range → excluded; and crucially **only the masked, in-window value crosses
  the boundary** — the raw host address never does, so **no ASLR leak**. This
  restores the security property the tight range gave while finding tagged roots.

**Where it touches:** the `Inst::GcRoots` operand set in `temen-ir`, its
`temen-text`/`temen-encode`/`temen-verify` handling, the interp lowering
(`temen-interp` ≈ `lib.rs:4621`), the JIT thunk
(`temen-fiber/src/fiber_rt.rs::gc_roots` ≈ `:512`) + the JIT emit, the
`__vm_gc_roots` lowering in `temen-llvm` (≈ `lib.rs:2776`), and the `<temen.h>`
declaration. Mechanical but spans the stack — keep the **mask optional / a new
opcode** if back-compat with the current `gc.roots` tests matters.

**Acceptance.** A guest holding only tagged JaclVal roots (no raw pointers on the
stack) collects correctly with `mask = PAYLOAD_MASK` and the **tight** heap range;
the existing `gc_roots` tests still pass (old form, or `mask = ~0`); host addresses
are not present in the returned set.

## Summary

| # | Ask | Size | Gates |
|---|---|---|---|
| 1 | `Translated.exports` (name→index) | small | separate-artifact linking |
| 2 | `temen-llvm-translate` CLI | small–med | the runtime `.temen` artifact |
| 3 | `gc.roots` payload-mask | medium | clean tagged-root GC (drops the widen+mask workaround) |

1 and 2 together make `runtime/build.sh` emit a linkable artifact and let JACL link
programs against it. 3 removes the only standing GC workaround. JACL-side codegen
(the IR builder + scaffold) proceeds in parallel on the whole-program harness
meanwhile.
