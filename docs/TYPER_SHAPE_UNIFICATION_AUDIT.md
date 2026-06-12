# Typer/Compiler Shape-Registry Unification — Audit Touch-List

Prerequisite audit for collapsing the typer's private shape registry
(`TyperCtx.shape_reg`) into the shared registry the compiler already builds
and ships to runtime (`vm->struct_registry`). This is the "Phase 6" the
`TYPE_REGISTRY_REFACTOR.md` work stopped short of: unify the registry
*instance* across passes, not just the encoding within a pass.

Goal of the audit: enumerate every site that discriminates a type index by
**range** (the `TYPER_MAX_STRUCTS` / shape-band boundary) rather than by the
per-entry **`kind`** tag, since a single unified numbering can't preserve the
typer's offset-based bands.

## Headline finding

**The entire migration surface is `src/typer.c`.** The compiler, VM, and GC
already discriminate by kind/presence/sentinel and contain **zero** range-based
struct-vs-shape tests:

| File | `TYPER_MAX_STRUCTS` refs | How it discriminates a type idx |
|---|---|---|
| `src/typer.c` | 32 | **range-based** (`>= TYPER_MAX_STRUCTS && < shape_reg.count`) |
| `src/compiler.c` | 0 | `JACL_IS_SCALAR_TYPE_IDX` + `defs[idx]` presence + `shapes[idx].kind` |
| `src/vm.c` | 0 | same (kind/sentinel) |
| `src/gc_collect.c` | 0 | `defs[idx]` presence + `JACL_IS_SCALAR_TYPE_IDX` + heap `elem_idx` |

So compiler/VM/GC need **no discrimination changes** under a unified registry —
they already tolerate any numbering. The risky reinterpretation surface
(the "silently read a typed-vec as a struct" hazard) is contained to `typer.c`.

## The idx-space bands

- `[0, 256)` = `TYPER_MAX_STRUCTS` — struct-table indices (slot 0 = dyn, slot 1
  = ctx, user structs from 2). Already seeded 1:1 from the compiler's registry,
  already portable, already ride AST stamps.
- `[256, 0xFF00)` — parametric shapes (proc, typed-vec, typed-map, buf, ptr,
  future, box). **Typer-side only**, via `struct_registry__init_at_offset(&tc.shape_reg, 256)`.
  This band is the duplication.
- `[0xFF00, 0x10000)` — scalar sentinels (`JACL_SCALAR_TYPE_IDX`). A fixed
  portable encoding, **not** registry-numbering-dependent. Safe; stays as-is.

Only the `[0,256)` vs `[256,…)` boundary is at issue. Scalar-sentinel checks
(`JACL_IS_SCALAR_TYPE_IDX`) are orthogonal and need no change anywhere.

## ROOT — the mechanism to remove

| Site | What it is |
|---|---|
| `typer.c:175` | `StructTypeRegistry shape_reg;` — the private second instance |
| `typer.c:6395` | `struct_registry__init_at_offset(&tc.shape_reg, TYPER_MAX_STRUCTS)` — the offset trick |
| `typer.c:129` | `TyperStruct structs[TYPER_MAX_STRUCTS];` — the typer's own struct table (candidate to drop in favor of reading the shared registry) |

Removing these is the change; everything below is the fallout.

## Category A — range discrimination → convert to kind-based

Each tests `idx >= TYPER_MAX_STRUCTS && idx < tc->shape_reg.count` (sometimes
`< TYPER_MAX_STRUCTS` for the struct side) to mean "this is a parametric shape."
Under unification these become a kind query — ideally one helper
`typer__is_shape_idx(reg, idx)` (true iff `idx < reg->count && defs[idx]==NULL`,
i.e. a shape slot) routed through the existing decode.

| Site | Context |
|---|---|
| `typer.c:599` | `typer__buf_elem_decode` — **the central decode**; range gate before it reads `shapes[idx].kind`. Migrating this one + routing callers through it collapses most of the rest. |
| `typer.c:803`, `:813` | nested-buf chain walk (`typer__nested_buf_chain_result`) |
| `typer.c:1893`, `:1898` | def-handler `def row [vec-get $nested i]` nested-vec re-derivation |
| `typer.c:3626`, `:3639` | `HEAD_FOR` loop-binding element decode |
| `typer.c:4157` | `vec-push`/`arr-push` closure-literal monomorphization (B3c follow-up) |
| `typer.c:4453`, `:4558` | `[[Vec/Arr […]] …]` constructor element decode |
| `typer.c:5027`, `:5037` | `HEAD_VEC_GET` element narrowing |
| `typer.c:5103` | `HEAD_ARR_GET` element narrowing (B3c follow-up) |
| `typer.c:5205`, `:5215` | `HEAD_MAP_GET` element narrowing |
| `typer.c:5687` | buf-chain shape walk |
| `typer.c:5944` | closure/return-shape decode (B3b call-result propagation) |

Note: many of these sites *also* call `typer__buf_elem_decode` right after the
range gate. If the decode becomes the single kind-based chokepoint and callers
drop their inline `>= TYPER_MAX_STRUCTS` pre-guards, the diff shrinks
substantially.

## Category C — the high-value deletion

| Site | Context |
|---|---|
| `typer.c:6193` | `typer__infer_var_ref` — suppresses the shape idx on the AST stamp (the cross-registry rule). Under unification, **delete the suppression**: portable indices mean the stamp is valid for the compiler to read directly. This is the change that retires the "re-walk from the binding" tax. |

Deleting this also makes obsolete the compiler-side re-derivation helpers that
exist only because the stamp was suppressed (e.g. `compiler__coll_receiver_proc_shape`,
the nested-buf chain re-walk) — those become follow-up cleanups once stamps are
trusted.

## Category B — struct-table capacity / seeding (not discrimination)

These concern the fixed-size `tc.structs[256]` array, a separate axis. If the
typer drops its own struct table and reads structs from the shared registry,
they disappear; otherwise they change form.

| Site | Context |
|---|---|
| `typer.c:2844`, `:2915` | struct-registration capacity guards (`struct_count >= TYPER_MAX_STRUCTS`) |
| `typer.c:6362`, `:6364`, `:6367`, `:6411` | `typer_infer` seeding loop bounds |

## Doc/comment-only

`typer.c:583`, `:585`, `:1056`, `:6389`, `:6392`; `shapes.c:188`, `:189` —
describe the offset trick / band layout; update once the bands collapse.

## Recommended migration shape

1. **✅ DONE.** Kind-based `typer__is_shape_idx(tc, idx)` landed
   (`idx < shape_reg.count && shapes[idx].kind >= TYPE_SHAPE_TYPED_VEC`), and
   every Category A discrimination site + `typer__buf_elem_decode`'s gate now
   route through it. Provably equivalent to the old range test in today's
   offset layout (`!scalar(iv) && iv>=256 && iv<count` ≡ `iv<count &&
   kind>=TYPED_VEC`, since the reserved prefix is `TYPE_SHAPE_NONE` and shapes
   live at >= 256), and correct under a unified layout because it reads the
   kind tag. The offset trick (`init_at_offset`, the `structs[256]` table,
   Category B capacity bounds) is untouched. Suite 91/0; TSAN 89/2 (known-safe).
2. Repoint `tc.shape_reg` interning to the shared registry; flip ownership so
   the registry lives on the compile context (created before `typer_infer`,
   surviving to runtime). Dedup in every `type_shape_intern_*` (verified) means
   the compiler's existing intern calls converge on the typer's entries — no
   duplicate entries, minimal compiler change. **See the blocker below — this
   step is NOT a clean repoint; it requires moving struct registration first.**
3. Delete the Category C suppression; let the compiler read AST stamps.
4. Remove the now-dead re-derivation helpers as cleanup.

### Step 2 blocker — struct-registration ordering (discovered 2026-06-11)

`compiler_compile` calls `typer_infer` (line ~19105) **before** the compiler
registers the current program's structs into `c.struct_registry` — that happens
during codegen, in the `AST_DEFSTRUCT` handler (~17994). So at typer time the
shared registry holds only *seeded* (prior-eval) structs. Today, struct-index
alignment between the two passes is achieved by **independent registration in
the same source order** (typer fills `tc.structs[]`, compiler fills
`c.struct_registry`, both from `AST_DEFSTRUCT` order starting at idx 2) — **not**
by sharing.

Consequence: the typer cannot simply intern shapes into the shared registry,
because current-program struct idxs aren't assigned there yet. A typer-interned
shape would take an idx that the compiler later wants for a struct → idx-space
collision, breaking the alignment everything depends on.

So step 2 has a prerequisite: **struct registration (with the idx assignment)
must happen before `typer_infer`.** Two further wrinkles:

- **Layout vs. type info.** Struct *layout* (byte offsets, sizes, inline
  nesting via `compiler__register_inline_struct`) is computed in codegen. The
  typer only needs names + field types + idxs, not offsets. So either the
  layout computation moves before the typer, or registration is two-phase:
  reserve the idx + names/field-types pre-typer, fill layout in codegen.
- **Dual representation.** The typer caches structs as `TyperStruct` (uint8
  field types, no offsets); the registry uses `StructTypeDef` (full layout).
  Unifying the struct table means reconciling these or keeping `tc.structs[]`
  as a typed-from-the-registry cache.

Revised step-2 decomposition:

- **2a.** ✅ **DONE (2026-06-11).** Struct registration hoisted to a pre-pass
  (`compiler__prepass_register_structs`) before `typer_infer` in
  `compiler_compile`: the full `compiler__register_struct_def` (incl. layout)
  runs in the pre-pass, so all struct idxs are fixed before the typer. The
  typer gained a `seed_struct_count` param bounding its seeding loop to the
  pre-existing (prior-eval) structs (snapshot taken before the pre-pass), so
  `tc.structs[]` is byte-identical — the current program's structs are still
  AST-registered by `typer__register_structs`. Codegen `AST_DEFSTRUCT` skips
  re-registration when the root's `structs_preregistered` flag is set (the
  pre-pass owns registration + duplicate detection); falls back to registering
  otherwise. **Scope:** initially `compiler_compile` (single-file + interpret)
  only; **now also the module paths** (`compiler__compile_module`,
  `jacl_compile_program`) — each snapshots the struct count after dependency
  modules registered theirs (during import collection), pre-registers its own
  structs, and bounds the typer's seeding to the snapshot, so closure
  proc-shape stamping is collision-safe in module compiles too. Only the
  `syntax.c` typer calls remain pass-the-full-count + no pre-pass (the prelude/
  macro-body paths; one passes a NULL registry, so no stamping there anyway).
  Landed in two commits: a pure extraction of the registration body into
  `compiler__register_struct_def`, then the reorder (and a module extension).
  Suite 91/0; TSAN 89/2 (known-safe). NB: the `Compiler` struct is mirrored in
  `jacl.h` — the new `structs_preregistered` field had to be added there too
  (the `struct_sizes` test catches the sizeof drift).
- **2b.** ✅ **DONE (2026-06-12, commit `b61342a`).** The typer now interns AND
  decodes all shapes in the shared registry (`tc.shared_reg`): every
  `type_shape_intern_*(&tc.shape_reg, …)` and `tc.shape_reg` read was repointed
  to `tc->shared_reg`, `typer__is_shape_idx` reads it, and
  `typer__portable_proc_shape` collapsed to identity. The embedded `tc.shape_reg`
  remains ONLY as a fallback for the `syntax.c` prelude/macro paths (which pass a
  NULL seed registry) — so the offset trick isn't fully dropped (gated on 2c/2d /
  giving those paths a shared registry). Shape idxs are now portable across
  passes by construction; this is what enabled compound types in `[Proc …]`
  (`TYPED_CLOSURES_DESIGN.md` item 4). Build 91/0, --tsan 89/2.
- **2c.** Un-suppress AST shape stamps (Category C — `typer__infer_var_ref`).
  ✅ **DONE (2026-06-12).** Two steps: (1) the four nested-collection narrowing
  consumers (vec-get / arr-get / map-get / for) were consolidated onto one helper
  `typer__receiver_coll_shape(recv)` — reads the stamp first, falls back to the
  binding for a var-ref — so the scattered "re-resolve the binding" convention
  now lives in ONE place (behavior-preserving; suite 580/580). (2) The
  suppression was then GATED to the non-portable registry only
  (`tc->shared_reg == &tc->shape_reg`, i.e. the `syntax.c` prelude/macro pass's
  embedded `tc.shape_reg` fallback). The prelude uses no nested-collection types,
  so in practice the stamp is now always portable and reaches the AST; the gate
  guards a future prelude regression rather than a live path. With the consumers
  reading through the helper (stamp-first), the un-suppress passed 580/580 — the
  earlier blanket attempt failed 10 because the consumers branched on
  `== UINT32_MAX`; the helper removed that fragility.
- **2d.** Delete the dead re-derivation helpers. ✅ **EFFECTIVELY DONE** via the
  2c consolidation: the per-site inline `scope_resolve` re-resolutions are gone
  (folded into the helper). The re-derivation that REMAINS is load-bearing, not
  dead: (a) the helper's binding fallback (the gated/expression path), (b) the
  def-handler + HEAD_FOR ctor-head shape computation for a *literal* receiver
  (`def n [[Vec [Vec i64]] …]` — no binding to read; ctors still stamp
  UINT32_MAX), and (c) the compiler's `compiler__coll_receiver_proc_shape`, which
  recovers a `[Vec/Arr [Proc …]]` element shape from the compiler's OWN Local
  (a parallel, independent mechanism, not a suppression workaround) to
  monomorphize a pushed closure. Converting (c) to read the typer stamp is a
  separate lateral change with its own risk and no behavioral payoff — left as-is.

**Empirical confirmation that 2c and 2d must land together (2026-06-12).**
Tried the *gated* 2c in isolation — keep suppressing only when
`tc->shared_reg == &tc->shape_reg` (the non-portable embedded fallback), let the
portable shared-registry stamp through otherwise. Build stayed clean but **10
jacl tests failed** (`map_nested_vals`, `vec_nested_get`,
`typed_closure_{arr_getpush,direct_get,mut,nested_push,struct_param_dyn,untyped_def,vec_getpush}`,
`typed_closure_module`), all with `assert-type failed: expected <elem>, got dyn`.
Cause: the compiler's nested-collection narrowing sites still RE-DERIVE from the
binding and branch on `inferred_struct_idx == UINT32_MAX` to detect the nested
case; making the stamp present flips those branches and the element collapses to
`dyn`. So 2c cannot ship without simultaneously converting every such compiler
reader to consume the stamp (the 2d helper-removal). This is the "wide blast
radius" — a coordinated typer+compiler change across ~10 narrowing sites for
**zero** user-facing or soundness benefit (the closure arc + nested narrowing are
already functionally complete). Reverted; left deferred as lowest priority.

"Proc-shapes first" still applies to 2c–2d, but 2a/2b are the groundwork shared
by all kinds. The instance flip is a multi-step sub-project, not a single
repoint.

### Cleanup audit after 2b slices 1–3 (2026-06-11)

What landed: the portable proc-shape stamp (`inferred_proc_shape_idx`) now feeds
the def-binding (slice 1), direct-call-of-expression (slice 2), and
closure-expression-arg (slice 3) sites. Question: can any re-derivation helper be
retired (the 2d goal)?

**Finding: nothing is safely removable yet — the stamp is purely additive.**
- `compiler__coll_receiver_proc_shape` is still used by both push paths
  (vec-push / arr-push monomorphization), independent of the get-narrowing
  fast-path.
- The get/local/global fallback branches in
  `compiler__call_closure_return_shape` are NOT dead: they cover (a) local
  closure-returning calls whose result the typer doesn't stamp (the 4288 stamp
  fires only when `typer__find_proc` resolves the callee), and (b) **any typer
  path where `tc->shared_reg` is NULL** — `syntax.c:1240` (prelude pre-pass)
  passes a NULL seed registry, so `typer__portable_proc_shape` can't stamp
  there; the fallback re-derivation is the only path.

So 2d (deleting re-derivation) is gated on completing the plumbing everywhere —
every typer path needs a surviving shared registry, and all shape kinds (not
just proc) need stamping. The 2b slices delivered the *capability* wins
(expression results carry proc shapes; the motivating B3c/B3d debts are closed);
the *code-removal* win is a larger, later effort whose marginal value is low now
that the motivating problems are solved.

### Tail closeout (2026-06-11)

Investigated whether the remaining tail (full `tc.shape_reg` retirement; the
`syntax.c` paths) is worth doing. **Conclusion: no — the arc is complete.**
- Probed non-closure expression results that would have the "lacks its shape"
  problem if it were general: nested-vec get-of-get (`[vec-get [vec-get $n 0] 1]`)
  and vec-get on a call result (`[vec-get [make-vec] 1]`) — **both already
  narrow correctly** via existing re-derivation. The problem was closure-
  specific (proc shapes are registry-bound; scalar collection elements ride the
  AST stamp natively as portable sentinels). So the instance flip adds **no new
  capability**.
- The `syntax.c` paths (prelude pre-pass / macro bodies) have **no failing
  case** — those contexts don't use typed-closure expressions.

**Introspection delivered instead** (the one high-value item the arc enabled):
a typed proc's signature is interned as a `TYPE_SHAPE_PROC` at compile time and
carried on the closure (`JaclClosure.proc_shape_idx`, copied at `OP_CLOSURE`),
so printing a closure shows `<proc name(types) -> ret>` (struct params by name),
`<proc name>` for untyped, `<closure(…)>` for anonymous. This uses the unified
registry directly — proc shapes survive to runtime in `vm->struct_registry`.
Closures also now route through `vm__fmt_value` in `OP_PRINT` (they previously
printed `<unknown>` at top level). Test: `proc_introspect.jacl`.

### Open decisions (resolve before step 2)

- **Dead-shape accumulation** (DECIDED: **lazy interning**): typer-only shapes
  (interned to check a type that compiles away) would otherwise survive to
  runtime and accumulate across `jacl_eval`s in the persistent registry. Step 2
  will intern a shape only when it's about to be stamped on an AST node /
  emitted, not merely to type-check.
- **Growth stability** (✅ RESOLVED — pointer-stability audit, below).
- **`interpret` isolation** (verified safe): `source_to_closure_in_place` calls
  `compiler_compile(..., seed_registry=NULL, …)` → a fresh registry, so a
  runtime typer pass never grows the live VM's GC-scanned registry. No new
  typer-vs-GC race.

### Pointer-stability audit (✅ resolved)

`struct_registry__grow` reallocs `reg->defs` / `reg->shapes`; the intern fns
separately realloc `proc_param_pool`. So the rule is: **entry indices are
stable across an intern; raw `TypeShape*` / `StructTypeDef*` / pool pointers are
not.** Audited both passes for any pointer held across an intern (= grow):

- **shapes.c intern fns** — all **grow-then-index**: `struct_registry__grow`
  (and the pool realloc) run first, then `reg->shapes[idx]` / `reg->proc_param_pool[off+k]`
  are written by index. Dedup scans are index-based. Safe.
- **typer.c holds** — all safe:
  - `:616` (`buf_elem_decode`), `:2214` (`decode_proc_shape`), `:5938`
    (nested-buf format) — read-only decode/format, no intern in the live range.
  - `:2792` (ctx-field seed) and `:6352` (struct seed loop) — setup phase: read
    the seed registry, write the fixed `tc.structs[]`; no intern in scope
    (`tc.shape_reg` isn't even initialized until after the seed loop).
  - `proc_param_pool` is always read as `reg->proc_param_pool[off+k]` (re-based
    each access), never a cached base pointer across an intern.
- **compiler.c holds** — the decode/format paths (`:259/:314/:657/:696/:4599/
  :4674/:4750`) are read-only with no intern in range; these are pre-existing
  and unchanged by unification (the compiler grows its registry during its own
  pass, after the typer pass has finished).

No interleaving across passes: `typer_infer` runs to completion before codegen,
per file/module/eval, so the typer never grows the registry while the compiler
holds a pointer into it. Invariant documented at `struct_registry__grow`
(`src/shapes.c`). Conclusion: growing the shared registry from the typer in
step 2 is pointer-safe, given lazy interning keeps to the same grow-then-index
discipline.

### Test strategy

Behavior-preserving: gate each step on full-suite-green plus a bytecode diff
(same source → identical chunk) to catch silent reinterpretation the suite
might miss. The 91-binary suite + the typed-closure / nested-buf fixtures are
the safety net.
