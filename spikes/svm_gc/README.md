# Spike-2 — conservative non-moving mark-sweep driven by `gc.roots`

**Status: PASS** (svm `vendor/svm` @ f75509a; needs `clang` + `llvm-18-dev`).

The riskiest design point of the backend rewrite (`docs/SVM_GC_CONTRACT.md`,
`docs/SVM_BACKEND_PLAN.md` §2): prove a guest-side **conservative, non-moving
mark-sweep** collector — finding roots via svm's `gc.roots` op — reclaims garbage
and retains the reachable graph (with intact data) across collections.

The collector is plain C compiled through svm-llvm, so it runs the **real
`__vm_gc_roots` op** on interp + Cranelift JIT.

## Run

```
cd spikes/svm_gc && cargo run
```

```
[2] svm-llvm translated it (7 funcs); imports: []
[3] verifier accepted the translated module
[4] run (interp == jit): conservative mark-sweep, two cycles, reuse:
  PASS  run(1) = 230   (1 garbage reclaimed x2, a & c survived intact)
  PASS  run(8) = 230
  PASS  run(100) = 230
  PASS  run(200) = 230 (200 garbage reclaimed x2, a & c survived intact)
```

## What it proves

`run(n)`:
- `a` — a live object held in a **stack local across the collection** → found
  conservatively via `gc.roots` (control stack / registers). Survives.
- `c = a->child` — allocated in a helper, reachable **only through `a`** → survives
  via **tracing** (not direct rooting). Proves the mark phase follows heap edges.
- `n` unreachable objects, allocated in a helper whose frame is then popped → not
  on the live stack → **reclaimed** (`swept == n`).
- **Two collection cycles** with `run(200)` over a 256-slot heap: round 2 only fits
  if round-1's swept slots were **reused** — proving reclamation actually frees
  space. `a` and `c` survive **both** collections with intact data.

`run(n) == 230` iff every invariant held, independent of `n`, identical on interp
and JIT. (No native oracle — `gc.roots` has no native equivalent; interp==jit plus
the exact value is the check.)

## Findings

- **`gc.roots` found the live stack root through the call chain** (`run` →
  `gc_collect` → `__vm_gc_roots`) with no special spilling tricks — the conservative
  root path works in practice here. JACL's codegen still owes obligation #1
  (safepoint spills) for the general case; the register-flush gap (GC.md §7) did not
  bite this scenario.
- **svm-llvm rejects a *constant* `ptrtoint` of a global address** (folding
  `(long)&gc_heap[0]` to a relocation-as-integer constant). Fix: route the cast
  through a `noinline` helper so it's a runtime `ptrtoint` instruction. A concrete
  codegen note for JACL's runtime (and a candidate svm-llvm enhancement).
