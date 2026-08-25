# Warm-snapshot + JIT compiler-guest — plan

Closing the **guest-JIT ↔ AOT gap**: make the self-hosted JACL compiler-guest
(`jacl_compiler.svmb`) fast enough in the browser that it approaches the AOT `jacl_emit.wasm`
frontend, by running it over a **pre-sized, non-growing window**. That single property unlocks two
independent wins svm now supports:

1. **Warm-runtime snapshot** — run the compiler's program-independent init **once** at page load,
   snapshot it, and restore it per compile (svm's `svm_warm_open`/`_eval`/`_close`). Removes the
   fixed per-compile init cost.
2. **wasm-JIT tier** — a page-op-free guest is JIT-eligible, so the compiler's hot functions run as
   emitted wasm instead of interpreted bytecode (svm's slice-2 default-on / opt-in tier-up). Removes
   the per-source compile-work cost.

> **Slice 0 measured the ROI (below): the per-source work is ~90 % of a real compile and the fixed
> floor ~10 %. So JIT-tier is the primary win and warm-snapshot is secondary — do Slice 1 (the
> page-op-free window) → measure JIT first; defer the warm-snapshot frontend decomposition.**

Both are gated on the same thing: **the guest must not grow/remap its window via `vm_map`** (no page
ops), because (a) the JIT bakes `mapped = 1<<size_log2` at emit time and gates page-managing guests
to the interpreter (svm #750), and (b) the snapshot restore is a window memcpy that can't reproduce a
grown region's `Mem` page-state (svm #816). Pre-size the window big enough that the heap never grows
→ both problems vanish. svm's own warm-snapshot prototype does exactly this (`WARM_MAPPED_LOG2` =
2²⁶) and measured QuickJS — an on-ramp of the *same shape* as our compiler-guest — going from a >1 s
do-nothing floor to ~2 ms warm.

## Scope honesty (read before starting)

- **This is a self-hosting win, not a live perf need.** The playground is already fast via the AOT
  `jacl_emit.wasm` path (macro-body staging made it macro-capable; tour compiles ~80 ms). The value
  here is *architectural*: one compiler, compiled entirely through SVM, no separate Emscripten
  toolchain, and in-guest macro expansion without the staging bounce. Scope the effort accordingly.
- **Ceiling.** Even done, the JIT'd guest lands *below* `jacl_emit` (the wasm-JIT carries confinement
  masking on every access that Emscripten's native-linear-memory build does not; svm slice-3 elision
  shrinks but doesn't erase it). It won't beat the AOT frontend.
- **Two costs, two levers — don't conflate them.** Warm-snapshot removes the *fixed init floor*;
  JIT-tier removes the *per-source compile work*. Which dominates for the JACL compiler is unknown —
  **measure first (Slice 0)**. If the init floor is small, prioritise JIT-tier and skip/defer the
  frontend decomposition.

## What svm provides (all landed as of pin `63a70532`)

- `svm_warm_open(mod_ptr, mod_len) -> i64` — decode a two-phase driver module, enlarge the mapped
  window to `WARM_MAPPED_LOG2`, run its `warmup` export **once**, keep the live prefix `[0, brk)` as
  the warm image. (`browser/src/lib.rs`.)
- `svm_warm_eval(stdin_ptr, stdin_len) -> i64` — restore the warm image into a fresh window, run
  `eval_run` with stdin seeded, stage stdout into the same capture slots `svm_run_onramp` uses.
- `svm_warm_close()` — free the session.
- Slice-2 default-on JIT + the "warm+JIT" compose (`2b0f65bf`): the warm snapshot's `eval_run` can
  itself run on emitted wasm. Reference driver: `crates/svm-run/demos/quickjs/qjs_snapshot.c`
  (`main`/`warmup`/`eval_run`); native test `crates/svm-llvm/examples/qjs_snapshot.rs`; browser test
  `browser/warm-snapshot-test.mjs`.

## Current shape (what we're changing)

- **Driver** — `codegen/selfhost/emit_driver.c`: a single `main()` that reads source on stdin and
  calls `jacl_emit_ir(src)` (one-shot: init + lex + parse + type + codegen + emit), printing the IR.
- **Frontend entry** — `codegen/tests/emit_jacl.c:457` `jacl_emit_ir(const char *source)`: the
  self-contained compile. No separable init/reset seam today.
- **Heap** — `runtime/heap_gc.c:27`: the GC heap is **already a fixed static region** (no window
  growth). The `vm_map` growth is the guest **libc `malloc`** heap (the driver's source buffer +
  the frontend's non-GC allocations), which is svm #737's 4 KiB-`vm_map` area.

## Slices

### Slice 0 — measure the floor — DONE. Verdict: **JIT-tier first, warm-snapshot deferred.**

Measured `jacl_compiler.svmb` compiling several programs through `svm_run_onramp` (bytecode
interpreter, the live playground path), median of 25 runs, via `demo/svm/bench_guest.mjs`:

| program | src | median | fixed floor | per-source work |
|---|---:|---:|---:|---:|
| trivial (`print "hi"`) | 11 B | 60 ms | 60 | — |
| sm_for_suspend | 1.7 KB | 129 ms | 60 | +69 |
| gc_stress_persistent | 5.4 KB | 243 ms | 60 | +183 |
| **tour.jacl** | 14 KB | **574 ms** | 60 | **+514 (90%)** |

The **fixed floor is ~60 ms** (module decode + instantiate + `_start` + jaclrt init + a minimal
compile) — the ceiling on what a warm-runtime snapshot can remove. The **per-source compile work
scales with source and is ~90 % of a real compile** (tour: 514 ms of 574 ms). For reference the AOT
`jacl_emit.wasm` ceiling on the tour is ~80 ms.

**Conclusion — this reverses the plan's emphasis:**
- **Warm-snapshot removes only ~10 %** of a real compile (the 60 ms floor). It helps tiny edits
  more (a 1.7 KB edit: 129 → ~69 ms, ~half), but not the case that matters.
- **JIT-tier targets the ~90 %** — the interpreted per-source compile work. It is the primary win,
  and it is gated on **Slice 1** (a page-op-free window makes the guest JIT-eligible).
- Therefore: **do Slice 1 → measure JIT (promoted below) first.** Defer the warm-snapshot frontend
  decomposition (Slice 2), which is the most expensive/risky work for the smallest return — and much
  of the 60 ms floor (decode + instantiate) can be reclaimed far more cheaply by a
  decoded/compiled-module cache across compiles (svm's slice-1 browser cache is the JIT-path
  analogue) with no frontend surgery.

**Honest JIT-magnitude caveat (must be measured after Slice 1, not assumed).** The compiler is
allocation- and cap-call-heavy (arenas, HAMT, string interning, GC), and cap-calls bounce back to
the interpreter via `env.call_interp` — so it sits closer to svm's Lua/SQLite JIT regime (~1.3–3×)
than to pure compute (16–112×). It also links the in-guest staging/interpret hooks (Jit cap +
fibers), so **concurrency ops are in the reachable set** → whole-module `compile_module` is out; the
path is **per-function mainline tier-up over the live window** (exactly what svm slice-2 just
validated in Chromium). Realistic guess: 514 ms → ~150–250 ms, i.e. the JIT'd guest lands ~2–3× the
`jacl_emit` ceiling, ~2–3.5× faster than today's guest. Slice 1 + a tier-up-enabled run path turn
this guess into a number.

### Slice 0b — tier-up eligibility measured against svm's pump (#839) — CONFIRMS the blocker

svm has since wired the single-shot **tier-up pump** into the on-ramp cards (#809/#831/#888): the
cdylib now exposes `svm_onramp_tierup_open` + `driveTierupRun` (the playground's `runJitModule` falls
through to it for an `InterpDriven` `_start`). #839 asks to bench the JACL card through it. Measured
(`demo/svm/bench_tierup.mjs`, `svm_browser.wasm` @ `d6f34a49`):

- **The pump refuses the JACL compiler-guest outright** — `svm_onramp_tierup_open` returns
  `-2 (STATUS_UNSUPPORTED)` for both `shared=0` and `shared=1` (so it is *not* a threads-build
  artifact; the cdylib memory isn't shared either). `runJitModule` throws "status 2 (_start not
  emittable)" and the playground falls back to the plain interpreter — a small per-Run *regression*
  (the ~100 ms open attempt is wasted), exactly #839's vacuity case.
- **Why (from `svm_onramp_tierup_open`'s own gate):** it admits fibers and `vm_jit_*` guests, and
  refuses only on `uses_threads()`/`uses_futex()` or when `compile_module_tierup` **emits nothing**.
  The compiler's concurrency is fiber-based (`cont.*`, `JACL_POOL_WORKERS=1`, no `thread.spawn`), so
  the blocker is **page ops**: the guest grows its window via `vm_map` (#737), so
  `compile_module_tierup` emits nothing → refuse.

**Conclusion:** the engine tier-up is ready and validated for eligible guests, but the JACL
compiler-guest is **not eligible yet — for the exact reason Slice 1 targets**. Slice 1 (a pre-sized,
non-`vm_map`-growing window) is the precise, confirmed unlock; until it lands, routing the JACL
compile through the pump is a net loss, so keep it on the plain interpreter.

### Slice 0c — CORRECTION + re-measurement after the `temen` rename (engine @ `85d8cf5d`)

The Slice-0b page-ops diagnosis was **wrong**, corrected by the engine side on #839: grow-via-`vm_map`-
*import* is invisible to the page-op gate (Lua/QuickJS/chibicc all grow and tier up). The real
blocker was the **whole-module `uses_threads()`/`uses_futex()` scan** catching jaclrt's **dead**
concurrency ops (linked but never run at `JACL_POOL_WORKERS=1`). That gate was **removed** (#926
slice 1 / PR #930) — concurrency now matters only if it *executes*. So Slice 1 (pre-sizing the
window) would fix a non-problem; drop it as the priority.

Re-measured against the renamed engine (the pump is now the **coop driver** `temen_coop_*`;
`demo/svm/bench_tierup.mjs`, threads `temen_browser.wasm` @ `85d8cf5d`):

- **Admission fixed ✓** — `temen_coop_open` returns **`0`** (was `-2`) for the compiler-guest, both
  `shared=0` and `shared=1`. The card opens; no JACL-side change needed for admission.
- **But the tier-up run traps** — on a clean no-macro compile (`sm_for_suspend`, 24 KB IR): 2 leaves
  tier up, then a concurrency op **executes** at runtime → the coop driver declines to
  `TIERUP_RUN_TRAP` → interpreter fallback. Net: bytecode 129 ms vs coop path 227 ms (**0.57×, a
  regression** — the open+partial-tier-up work is wasted, then it re-runs on the interpreter).
- So JACL gets **no tier-up win yet**: it executes a real concurrency op (an atomic in the GC/alloc
  path, despite `JACL_POOL_WORKERS=1`), which is **#926 slice 2** territory (servicing concurrency
  events on the cooperative scheduler) — deferred on the engine side.

**Also found — a separate regression to fix on our side:** in-guest macro staging via the Jit cap
broke at `85d8cf5d`: the macro-bearing tour fails with `macro 'unless': jit compile_linked failed
(-22)`. No-macro programs compile fine. (The playground default `jacl_emit` stages macros a different
way, so this hits the *guest*-compiler path.)

**Revised takeaway:** admission is solved by #926; the remaining levers are engine-side #926-slice-2
(service the concurrency trap instead of declining) and #1009 (Ro-rodata makes tier-up fire zero
useful events on real guests anyway) — plus, on our side, the `-22` macro-staging regression. The
consumer window-pre-sizing (old Slice 1) is **not** the lever.

#### Update — re-measured with a freshly-built cdylib (engine still @ `85d8cf5d`, post-migration)

After the `svm→temen` migration merged (jacl_impl PR #73), the shipped `temen_browser.wasm` was found
**stale** — built mid-migration when the submodule pointer had flickered to an older commit, so its
compiled-in `temen_encode` rejected the current v10 card (`temen_run_onramp` → `STATUS_DECODE_ERR`,
status 1) even though `decode_module`/`decode_unit` accept the card natively. Rebuilt the **threads**
cdylib from the pinned checkout (the `pages.yml` `browser-real` recipe: `+atomics,+bulk-memory` +
`--shared-memory --import-memory`, `-Z build-std`) and re-ran `demo/svm/bench_tierup.mjs`. Three
refinements to the above:

- **The `-22` macro-staging regression is RESOLVED** — the v10 encoder fix (`irb_to_encoded`, PR #73:
  version `9→10`, `CALL_SYM` `0x0E→0x7B`) closes it. `tour.jacl` now compiles clean to 191 828 B of
  IR on the plain path, no `-22`.
- **The trap is input-independent** — `tour.jacl` (13 969 B src) and `try_basic.jacl` (93 B src) both
  produce the *identical* event profile: `temen_coop_open` 0/0 (admitted ✓), then `tierups=2,
  jit_invokes=0, bounces=1`, then a clean decline (`temen_coop_run` → 2, `temen_status()==3
  STATUS_TRAP`, **empty** `temen_par_last_panic` — a decline, not a wasm crash). Same 2-tierups /
  1-bounce for a 93 B and a 14 KB compile ⇒ the decline is **structural to the compiler-guest itself**
  (its linked jaclrt), hit early in startup, *not* in the input-dependent compile work.
- **#926 is now fully closed** (slice 2 landed via PR #974 — fiber-scheduler + live-futex tier-up
  servicing — and is in this pin), and the multi-vCPU coop driver *does* service `Spawn`/`Join`/
  `Wait`/`Notify`. Yet JACL still cleanly declines.

#### Traced root cause — it is **not** a concurrency op; it is a `vm_map`-grow-inside-an-emitted-leaf

Instrumented the coop driver (`temen_coop_run`'s `Trapped(_)` arm) and the JS drive loop's swallowed
`catch` to surface the real trap. The decline is:

```
TIERUP f821: RuntimeError: unreachable      (identical for a 93 B and a 14 KB compile)
```

`temen_coop_deliver_trap` defaults to `Trap::Unreachable`, so the coop code reports it generically —
but the JS `catch` shows the ground truth: the **emitted tiered leaf `f821` traps on a wasm
`unreachable` at runtime**. Dumping func 821 from the card (`decode_check <card> 821`) pins it exactly:

- `f821 : (I64) -> I64`, **3 blocks, no `unreachable` terminator in its IR** — so it is *not* a
  `--stub-externs` stub. Its body is `Load … IntCmp → BrIf` (block 0, "is there room?"), then
  `… CallImport<vm_map> … Store → Br` (block 1, the grow-and-write slow path), then `Store, Store,
  Return` (block 2). **Func 821 is the guest's heap-grow allocator slow-path.**
- The emitted leaf calls `vm_map` via a **cross-tier bounce** (matches the observed `bounces=1`) to
  grow the window, then `Store`s into the just-grown region. But the emitted tier's `mapped` bound is
  synced only at event boundaries, so it does **not** reflect the grow that happened *within* this
  leaf — the post-grow `Store`'s confinement bounds-check against the stale `mapped` global fails and
  the emitter's guard fires `unreachable`. The interpreter admits the same `Store` (its page map
  updated on the `vm_map`), so **emitted `f821` diverges from the interpreter oracle**.

So the blocker is precisely the **#816 / #717 / #1009** family, now with a concrete repro. It is
input-independent (f821 is the allocator, hit by every compile) and decisively rules out the earlier
"concurrency op" guess.

**Dug into the engine to attempt a fix — the mechanism is deeper than a stale `mapped` bound.** The
JACL card is already run **paged** (1128 `readonly` data segments ⇒ `temen_coop_open`'s paged
predicate is true), so the emitted tier uses the per-page state table, not a scalar extent. Yet the
store still traps. Instrumenting `temen_coop_call_interp` across f821's `vm_map` bounce:

```
BOUNCE paged  map_ver_now=1 (== pv_before)  scalar_ext=0  cover_after=33554432 (32 MiB)
```

- `mem_map_version()` **does not change** across the `vm_map` grow, so the version-guarded
  `sync_pagestate` skips its rebuild. But forcing an unconditional rebuild on every bounce
  (tried: `sync_pagestate_forced`) **still gives 32 MiB coverage** — so `mem_map_info()` **does not
  reflect the grown pages at all**. The coop window is `JIT_RUN_WIN_LOG2 = 25` (32 MiB) with a small
  committed prefix; the guest's heap pages sit above the prefix and the paged table marks them
  `Unmapped` (default above `mapped`) ⇒ the emitted store traps `unreachable`.
- This matches a limitation the engine flags in-code: `window_scalar_extent`/`mem_map_info` "read the
  shared root window (a confined child's own-window growth is a later refinement)" — i.e. the
  `vm_map`-grown/committed reserved-tail pages the guest writes to are **not represented in the root
  window introspection the paged tier-up depends on**.

So the real engine fix is a **non-trivial memory-model change** (make the grown/committed reserved-tail
state visible to the paged tier-up's page-state table, in the confinement-critical path — needs the
differential + masking fuzz suites), not a one-line refresh. The reverted experiment is captured in
this session; the deepened diagnosis is posted to #816/#1009.

**The consumer-side sidestep is the faster path and is fully in our hands: the old Slice 1** — a
**pre-sized, non-`vm_map`-growing** compiler window (below). It removes the grow entirely, so f821
never calls `vm_map`, the paged table stays valid, and the guest tiers up — and it is exactly what
#816's own workaround recommends. That is the lever to pull for a JACL tier-up win without waiting on
the engine memory-model refinement.

Net for the playground today: **unchanged** — the JACL compile still runs wholly on the (correct,
fast-enough) bytecode interpreter; the coop tier-up attempt declines and falls back. No regression in
the shipped path (the playground catches the decline), no win yet. `decode_check` (harness bin) guards
against shipping a cdylib/card version-skew again.

### Slice 1 — pre-sized, non-growing window (the shared prerequisite)

Make the compiler-guest never call `vm_map`: give the guest libc `malloc` heap a fixed pre-sized
arena (large enough for the largest expected compile) instead of growing window pages. The GC heap
is already static (`heap_gc.c`), so this is the libc-heap side (#737's area). Verify with
`probe_svmb`/`browser_coverage` that the linked compiler module reports **no page ops** and is
JIT-eligible (`analyze` accepts it, `module_uses_page_ops == false`).
- **Test:** the compiler-guest compiles the corpus identically with the fixed arena (native
  `parity`), and `probe_svmb` shows page-op-free + JIT-eligible.

### Slice 2 — frontend decomposition (`warmup` / `compile`) + two-phase driver

Split `jacl_emit_ir` into:
- `jacl_emit_warmup(void)` — build the **program-independent** state into statics: intern/keyword
  tables, the built-in prelude macros, type-registry builtins, and prime the GC/arena allocators to
  a clean "ready to compile, arenas empty" state.
- `jacl_emit_compile(const char *src) -> char *` — compile one source using the warm state, writing
  into per-compile arenas that a snapshot **restore** resets between runs.

Then a two-phase driver (`codegen/selfhost/emit_driver_snapshot.c`) exporting `warmup()` and
`eval_run()` (mirrors `qjs_snapshot.c`). **Fresh-per-Run isolation (INVARIANT #6)** falls out of the
snapshot contract: each `eval_run` restores the identical post-`warmup` image, so per-compile arena
state cannot leak between compiles — *provided* the frontend allocates all persistent state during
`warmup` and all per-compile state after it.
- **Risk / open question:** how separable is the frontend's init? If `jacl_emit_ir` interleaves
  init with compile (or leaves global mutable state a second call would trip on), the decomposition
  needs care. Pin it with a native test **before** wiring the browser.
- **Test:** native harness — `warmup()` once, then `eval_run()` over N sources, asserting each
  output byte-identical to `jacl_emit_ir(src)` (cold≡warm), and that source K+1's output does not
  depend on source K (isolation).

### Slice 3 — playground wiring

- Build `jacl_compiler_snapshot.svmb` (the two-phase driver) in `build_assets.sh`.
- `SvmJaclRunner`: add `warmOpen(moduleBytes)` / `warmEval(source) -> RunResult` over
  `svm_warm_open`/`_eval`/`_close`. Open once in `ensureLive`; `compileWith` a new `warm` mode calls
  `warmEval`. Invalidate + reopen on module change (there is none at runtime, so open-once).
- **Test:** node twin of `warm-snapshot-test.mjs` — warmup once, eval several sources, cold≡warm
  parity + first-vs-warm timing. Then the browser differential.

### Slice 4 — confirm JIT engages (measure)

With Slice 1's page-op-free window, confirm the compiler-guest's hot functions actually tier up
(slice-2 default-on, or opt-in `tierup: true`), and measure the compile-work speedup. Compose with
warm-snapshot (svm's warm+JIT). Report numbers vs. the interpreter and vs. `jacl_emit`.

## Relationship to svm issues

- **#816** — warm-snapshot can't restore a `vm_map`-growing guest (the reason Slice 1 pre-sizes the
  window). Filed from this work.
- **#750** — JIT-eligibility for page-managing guests (the JIT half of the same non-growing-window
  constraint). Slice 1 sidesteps it by not growing.
- **#737** — the compiler-guest's 4 KiB `vm_map` page-size assumption; Slice 1 is adjacent (remove
  the `vm_map` growth entirely).
- **#810** — fuzzing the runtime-aware confinement that would eventually let a *growing* guest be
  JIT'd/snapshotted, removing the need to pre-size. Not required for this plan.

## Not doing (and why)

- **Per-function mainline tier-up over a live growing window** — the original spike
  (`SVM_BROWSER_TIERUP_FINDINGS.md`); superseded here by "pre-size the window so it never grows,"
  which is simpler and unlocks both levers at once.
- **Replacing the AOT `jacl_emit.wasm` path** — it stays the default fast path; this is an
  alternative self-hosted path, not a replacement.
