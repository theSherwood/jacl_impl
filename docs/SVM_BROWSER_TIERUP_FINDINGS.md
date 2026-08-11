# Browser per-function JIT tier-up — spike findings

**Status: RESOLVED on the svm side (defect diagnosed + fixed); adopted as svm's WASM_AOT.md
"slice 0".** The confirmed defect is fixed in svm `main` and is present as of our submodule pin
`bf75dd72` (svm PR: "slice 0: gate page ops in compile_module_tierup", commit `17e06a4e`). The
sections below are the original postmortem, preserved for context; the resolution is summarized
next.

## 0. Resolution (svm side)

svm reproduced the defect natively and fixed it. Which of this doc's ranked hypotheses held:

- **Hypothesis 1 (data/window materialization) — ruled out** for the native path. A new harness
  (`svm-wasm-jit/tests/tierup_live_window.rs`) drives the real loop — a `bytecode::VcpuReactor`
  over a caller-owned window, `_start` materializing `m.data`, then a mainline `Call` tiered onto
  emitted wasm mirrored over that live window — and it matches the interpreter oracle exactly
  (data-segment read + write-then-read round-trip) **when the window size matches**. The
  shared-`win` contract is sound.
- **Hypothesis 2/3 (baked `size_log2` vs a page-managed window) — confirmed as the real defect.**
  `compile_module_tierup` (a *public* entry) did **not** apply the `module_uses_page_ops` gate that
  `compile_jit`/`compile_nested` apply. A guest that maps/unmaps/protects its own window — exactly
  the JACL compiler's growing heap — got hot leaves emitted with `mapped = 1 << size_log2` **baked
  at emit time**; an emitted mask-only access then ignores the live (grown/remapped) page state and
  diverges from the interpreter → the `MemoryFault → unreachable` mid-body, before any
  `env.call_interp`, that this spike saw. The spike called `compile_module_tierup` directly,
  bypassing `compile_jit`'s gate.

**Fix (landed):** `compile_module_tierup_caps` now self-applies the page-op gate — a page-managing
module emits nothing (all-`false` bitmap, valid imports-only wasm), identical to `compile_jit`.
The gate holds regardless of which public entry a host calls.

**What this means for JACL.** The trap/divergence is gone: driving a page-managing guest through
tier-up is now *safe* (it silently routes those functions to the interpreter). But a page-managing
guest — our compiler — is therefore **not accelerated** by this path; it runs on the interpreter as
before. Actually *speeding up* a page-managing guest needs the follow-on bounds-proof elision work
svm has started (`svm_ir::bounds` + "wasm-JIT elides the bounds-trap on a provable in-window
proof", commits `6c492579` / `4d5f811d`). So: **fixed = no longer a bug; still not a speedup for the
compiler-guest.** The shipped AOT-frontend + macro-body-staging fast path remains the playground's
real performance story, and this stays a latent option, not a live need. One residue svm flags as
unverifiable without a node+cdylib browser harness: whether the `svm_par_run`/`PAR_TIERUP` lane's
`win`/`size_log2` provenance matches `svm_run_onramp` for a guest that *grows* its window mid-run —
moot in practice now that the gate routes such guests to the interpreter.

---

References below are to the `vendor/svm` engine (`crates/svm-wasm-jit`, `browser/src`). Line
numbers are approximate — read against current svm `main`; the symbol names and module-doc
sections are the stable anchors.

## 1. Motivation

The playground's macro-capable live path can run the self-hosted JACL compiler **as an SVM
guest** (`jacl_compiler.svmb`) on the browser's bytecode interpreter. It is correct but slow —
compiling the tour took ~1.8s, dominated by interpreting the compiler. (The shipped fast path
sidesteps this entirely: the AOT `jacl_emit.wasm` frontend compiles at native speed and stages
only tiny macro *bodies* on SVM — see `docs/SVM_MACRO_STAGING_PLAN.md` and
`demo/svm/README.md`. This spike was the *other* direction: make the guest itself fast.)

The goal was to JIT the compiler-guest's hot functions to wasm so mainline compilation runs on
the JIT tier instead of the interpreter.

## 2. What svm offers (baseline)

`svm-wasm-jit` has two JIT entry points:

- **`compile_module`** — whole-guest wasm JIT. Gated by `compile_tier_eligibility`
  (`crates/svm-wasm-jit/src/lib.rs`, the `TierEligibility` struct, ~L819): every *reachable*
  function must be in the v1 subset or an interp leaf, func 0 in-subset, and **nothing
  reachable may use concurrency / §12 capability ops**. Any reachable indirect call
  conservatively forces *all* reachable functions to be in-subset.
- **`compile_module_tierup`** — per-function tier-up. Emits `f{i}` for eligible hot functions;
  every other call routes through the `env.call_interp` import back to the bytecode interpreter
  **over the same shared window**. This is the path built for `thread.spawn` bodies: the main
  vCPU raises `VcpuEvent::TierUp { func, argv }` (`browser/src/lib.rs` ~L1433), the host
  marshals `argv`, a Worker runs `f{func}`, and `svm_par_deliver_tierup` returns the results.
  The whole thing is driven by the resumable `svm_par_run` event loop.

### The load-bearing invariant

Every emitted memory access replicates trap-confinement masking **with constants baked at emit
time** (`svm-wasm-jit/src/lib.rs` module docs, ~L22–33 and the `compile_module_tierup` body,
~L1297–1305, L1444–1449):

```
mask   = (1 << DEFAULT_RESERVED_LOG2) - 1     // MASK const, L76
mapped = 1 << m.memory.size_log2               // baked from the module's declared memory
eff = addr + offset
if eff + width > mapped { env.trap(MemoryFault); unreachable }   // unmasked bounds check
access linear memory at  win + (eff & mask)                       // clamp is a no-op past the check
```

Two consequences matter here:

1. **`mapped` is frozen at emit time** from `m.memory.size_log2`. If the running window is a
   different size than what was baked, a legitimate in-bounds access above `mapped` faults.
2. **The host must materialize `m.data` into the shared window before the run** (explicit note
   at ~L1444: *"the emitted code only loads/stores, so the host must materialize the module's
   data into the window before the run … an unwritten segment simply reads as zero"*). The
   emitter assumes the window is already populated exactly as the interpreter's window-init
   leaves it, and that emitted code runs over that **same live `win`**.

The design intent is explicitly *"the hot path emits while its cold range-check / I/O helpers
stay on the interpreter over the **same** shared window"* (~L1297). The shared, already-populated
window is the contract.

## 3. What was attempted

The compiler-guest is `InterpDriven` and its reachable set uses §12 concurrency / capability ops
(fibers + atomics pulled in by `jaclrt`, plus the `vm_jit_*` Jit cap), so `compile_module`
rejects it outright at eligibility time. The spike therefore used **per-function tier-up**: emit
the hot compile function via `compile_module_tierup`, leave everything else on the interpreter
via `env.call_interp`, and drive it through `svm_par_run` + `VcpuEvent::TierUp`.

## 4. Symptom (ground truth)

The emitted `f{func}` **trapped `Unreachable` internally** — it hit the `env.trap → unreachable`
sequence *inside its own body*, **before reaching any `env.call_interp`** (the cross-tier call
count was 0). So this was:

- **not** an eligibility rejection (emission succeeded), and
- **not** a cross-tier arg/result marshalling bug (no `call_interp` ran), but
- a fault **inside the emitted body**, almost certainly the masked-bounds `MemoryFault` →
  `unreachable`.

## 5. Root-cause leads (ranked)

1. **Window / data-materialization mismatch (most likely).** The `thread.spawn` tier that
   `compile_module_tierup` was built for gets a fresh activation with `argv` marshalled in.
   Driving **mainline** compiler computation through `TierUp` instead means the emitted function
   must run over the *live, mid-computation* window — with `m.data` already written and the same
   `win` base the interpreter has been using. If the `svm_par_run`/`TierUp` setup hands the
   emitted function a fresh or differently-based window, or doesn't pre-materialize `m.data`,
   then a legitimate access computes `eff > mapped` (or chases a pointer into unpopulated
   memory) → `MemoryFault` → `unreachable`. The compiler is intensely data-segment-driven
   (keyword tables, interned strings, AST arenas), which is exactly where this bites first.
   **Check:** does the onramp/`svm_par_run` tier-up path write `m.data` into the shared window
   and pass the *live* `win` + `size_log2`, the way `svm_run_onramp`'s interpreter init does?

2. **`size_log2` / heap-growth mismatch.** `mapped` is baked from `m.memory.size_log2` at emit
   time. If the onramp guest's window is instantiated at a different size, or grows past that
   during the run, in-bounds accesses above the baked `mapped` fault. **Check:** the `size_log2`
   the emitter reads vs. the window the onramp path actually allocates (and whether the guest's
   heap can grow beyond it).

3. **Subset-classification hole.** The emitted hot function might contain an op that *should*
   have been routed to an interp leaf via `call_interp` but was emitted inline (or an
   indirect-call trampoline that lands wrong). Less likely — the trap precedes any `call_interp`
   — but worth confirming the `interp_leaf` / `in_subset` classification for the specific
   function against `compile_tier_eligibility`.

## 6. Diagnostics for the svm follow-up

- **Instrument `env.trap`**: log the trap **code** plus `eff` / `mapped` / `win` at the faulting
  access. `MemoryFault` with `eff` just over `mapped` → hypothesis 1/2; a different code → look
  elsewhere.
- **Minimal repro isolating the shape**: a small `InterpDriven` guest that (a) uses a `data`
  segment and (b) is forced through per-function `TierUp` (*not* `thread.spawn`). If it faults,
  the problem is "mainline `TierUp` over a live window", independent of the compiler's size.
- **Diff the window setup** between `svm_run_onramp` (interp) and the `svm_par_run` / `TierUp`
  path — specifically data materialization and the provenance of `win` / `size_log2`.

## 7. Strategic note

The existing tier-up is purpose-built for `thread.spawn` bodies (fresh activation, `argv`
marshalled, results delivered once). Using it to accelerate the *mainline* computation of a
large `InterpDriven` guest is off-design; the shared-live-window invariant (§2) is the load-
bearing assumption most likely being violated. A fix probably means either (a) a tier-up entry
that runs an emitted function over the interpreter's *current* live window/state rather than a
spawn activation, or (b) confirming and enforcing that the onramp path materializes data + shares
the exact window the interpreter holds. Whether that's worth it depends on whether the AOT-
frontend + macro-staging fast path (already shipped) makes guest-JIT compile speed moot for the
playground's purposes.
