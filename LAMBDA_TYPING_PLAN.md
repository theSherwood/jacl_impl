# Plan — clean lambda result-type inference

> ## START HERE — reading order for this thread
> **Phase A is COMPLETE** (2026-06-10): inline-callback monomorphization
> for every element type (wide scalars, tagged-fit, str, STRUCT — input
> AND output channels), typed collect, restore-pass deletion, plus the
> collection-type unification (vec/map/arr ≡ [.. dyn]; ref-kind erasure
> stamps), the no-autobox rule, suspending for-loop bodies, and the SM
> wide-rep boundary fixes. See the git log around `64e8d6c` and
> `NOT_IMPLEMENTED.md` §4/§8d/§8e for exactly what landed.
>
> **Phase B is COMPLETE for `[Vec …]`** (2026-06-10): `[Proc [P…] R]` type
> syntax + typed closure VALUES (B1+B2), closure PARAMS / user HOFs (B3a),
> closures-returning-closures (B3b), named closures as VALUES with conformance
> (B3d), AND collections of closures `[Vec [Proc …]]` (B3c-vec) — all via a
> registry `TYPE_SHAPE_PROC`, no VM change. **The only remaining follow-up is
> `[Arr [Proc …]]`** (mutable-array variant — same mechanism, needs its own
> constructor + for-loop branch wired; the ctor errors today) and vec-get
> narrowing on a `[Vec [Proc …]]`. See the `TYPED_CLOSURES_DESIGN.md` Phase B3*
> status blocks.
> 1. **`TYPED_CLOSURES_DESIGN.md`** — the typed-closure arc is essentially
>    complete; remaining: `[Arr [Proc …]]` + vec-get narrowing (B3c debt).
> 2. **This file** (`LAMBDA_TYPING_PLAN.md`) — background: the lambda
>    inference design + i64/u64/f64 producer-wide flip + contextual
>    literals (all DONE).
> 3. **`STREAM_TYPING_DEBT.md`** — the 5 compromises (all resolved by
>    Phase A except where noted in the doc).
> 4. **`STREAM_TYPING_ISSUES.md`** — retrospective / pitfalls (read before
>    debugging: stale-binary discipline, etc.).
>
> Other open threads to pick instead of Phase B, all indexed in
> `NOT_IMPLEMENTED.md`: typed spread (§4.1b); map iteration (`for` over
> maps doesn't exist — §4); the punted dyn-return no-autobox consistency
> call (§4, after the no-autobox paragraph); C-style for suspension +
> [Buf]/[Ptr] defs across suspensions (§8d/§8e residue).
>
> The auto-loaded memory `project_typed_streams_handoff` mirrors this.

Last updated: 2026-06-10 (end of session; Phase A closed).

## The problem

A `\` lambda is a **prelude macro** (`src/prelude.jacl:5`) that expands to an
anonymous `proc {^it} { body }` with an **untyped (dyn) param**. It therefore
has no standalone signature — its return type is a function of the argument
type, known only at the call site, and JACL has no generics.

`79c78b5` made `transform` over a typed stream infer the lambda's return type
via `typer__lambda_ret_enc` (typer.c:2435). Two problems:

1. **Layering leak (the real objection).** The facility hardcodes the param
   name `"it"` (typer.c:2458) — private knowledge of the `\` macro's
   expansion. The core stopped treating a lambda as an ordinary proc and
   started pattern-matching its desugared shape. A hand-written inline
   `proc {^x} { * $x 10 }` passed to `transform` gets nothing. So "lambdas are
   just a macro the core never sees" is broken.
2. **Double-walk wart.** It types the body with `it:<elem>` to read the return
   type, then re-types with `it:dyn` so codegen (which expects a boxed dyn
   param) isn't corrupted. The restore pass is the smell; it compounds on
   nested pipelines.

## The decision

The clean end state is **call-site-directed typed-closure compilation**: push
the receiver's element type into the proc body and compile that body **once**
against it, using the proc's **real** params. One mechanism fixes all axes:

- **Layering** — nothing lambda-specific; the rule is "compile a proc body
  with its params bound to the known arg types." Lambdas benefit because they
  *are* procs. `\` goes back to being pure sugar. No `"it"`.
- **Startup** (the typer is a compile-time pass run once per program run —
  `jacl_run`: lex→parse→`compiler_compile`(typer inside)→`vm_exec`, no
  bytecode cache) — type the body **once**, same passes as the dyn baseline.
  No double-walk, no nested-pipeline compounding.
- **Runtime** — the body operates on the real element type, so the per-element
  box/unbox of the param disappears on hot numeric streams.

Rejected alternatives: generics/function types (deliberately out of scope, and
they don't escape the box-vs-monomorphize runtime choice anyway); annotations
(kills piping ergonomics); revert-and-stay-dyn (loses the ergonomics we want).

## Phased path (no ergonomic-regression window)

- **Phase 1 (#1)** — Replace `typer__lambda_ret_enc` with a general
  `typer__proc_result_enc(proc_node, arg_encs[], argc)` using real params
  (`typer__parse_params`). **Keeps** the restore pass for now (codegen
  unchanged) — interim, documented. Fixes the layering leak immediately.
- **Phase 1b (#2)** — ✅ RESOLVED, not applicable. Current JACL has **no**
  reduce/fold; `map` is the hashmap constructor (not a HOF); `each` → nil;
  `filter` → bool predicate (element unchanged); direct `[$f $x]` is a var-ref
  to a named closure (needs deferred closure-signature work); typed-vec/map
  `transform` produces a **plain boxed** vec/map at runtime, so the typer can't
  claim `[Vec R]` without rep changes. ⇒ `transform` over a typed **stream**
  is the only safe lambda-return consumer today (done in Phase 1). The general
  facility is ready to wire into those sites if/when they gain runtime support.
  No "only transform" arbitrariness remains — transform is the sole applicable
  case, not a privileged one.
- **Phase 2 (#3)** — Type-once + unbox-at-param: drive body compilation from
  the call site with the param typed, **drop the restore pass**, insert one
  unbox at the param prologue, memoize the node result. Removes the wart with
  the producer still tagged.
- **Phase 3 (#4)** — Producer-wide rep flip: producers
  emit wide, all `vm__pull_stream_one` consumers updated, GC skips wide
  `cached_value`. Drops the Phase 2 unbox. Static rep, runtime rep, and value
  flow all agree. Shared with the rest of the remaining typed-stream work.

## Finding (Phase 1): `\` is the only inline-anonymous-proc surface form

Inside `[...]` the parser **rejects** `proc` (parser.c:373-377 — "proc must use
command syntax"); anonymous `proc {params} {body}` parses only at statement
level (parser.c:1648). The `\` macro sidesteps this by emitting `make-syntax`
nodes directly, and it always names the param `it`. So for `transform` with an
inline lambda, the old hardcoded `"it"` and the new real-params path are
**behaviorally identical** — the generalization is correct-by-construction and
the foundation for 1b/Phase 2, but it is not separately surface-testable here.
The de-leak is exercised for real in **Phase 1b** (reduce / direct calls bind
multiple real params: `acc`, element). Phase 1 is verified by
`stream_transform_typed` staying green (no regression on the `\` path).

## Verified: restore-pass removal is intrinsic to B, not a standalone step

Tried dropping the restore-pass alone (typed mapper body, tagged pipeline):
`collect [transform [range 0 3] [\ * $it 10]]` → `4323455642275676220`
(tagged param read as wide bits). A wide body needs a wide param; in the
tagged pipeline that means unbox-at-boundary + box-back-result, and the input
type isn't reliably available at runtime (filter/take/transform don't set
`elem_idx`). All such scaffolding is deleted by B. ⇒ the restore-pass removal
falls out *for free* once the pipeline is wide (B); there is no clean low-risk
precursor. Restore-pass kept until B (documented in `typer__proc_result_enc`).

## Phase 1 status: ✅ done

`typer__lambda_ret_enc` → `typer__proc_result_enc(proc, arg_encs[], argc)`
(typer.c). Reads real params via `typer__parse_params`; binds untyped (dyn)
params to call-site arg encodings; interim restore-pass kept and documented.
`transform` rewired. Suite 480/480.

## B implementation recipe (validated, resumable)

Contract: `vm__pull_stream_one` yields **wide** raw bits iff the stream's
`elem_idx` decodes to a wide scalar (i64/u64/f64); else tagged. Wide rep = raw
64-bit value in the `JaclVal` slot (as `OP_TO_I64` produces: `vm__push(vm,
(uint64_t)i)`). Validated facts: `range` elem_idx=i64 (vm.c:12253); generators
set elem_idx=`gen_elem_idx` (vm.c:3883) and yield **tagged** via
`vm->yield_value` (proven by stream_for_scalar summing a [Stream i64] generator
to 300 through the existing tagged→wide unbox); lines=str, exec=str/bytes.

Strategy = normalization wrapper to minimize wide-aware consumers:
- Add `vm__pull_stream_dyn(vm, s, out)` = `pull_stream_one` then box wide→tagged
  if `elem_idx` is wide-scalar. Point **tagged-wanting** consumers at it with
  ZERO logic change: collect (vm.c ~9477), spread, each (~5759), exec-stdin
  (~2157), and userland `stream_next` (see below). They keep seeing tagged.
- **range** (vm.c:1967): emit wide `(JaclVal)(uint64_t)current` (drop the
  i32/boxed-i64 split) — gated on `elem_idx` wide (always i64 here).
- **generator pull** (vm.c:2031, `*out_value = vm->yield_value`): if elem_idx
  wide-scalar, unbox yield_value (tagged) → wide before returning.
- **filter** (vm.c:1512): source already yields per-contract; BOX elem→tagged
  before pushing to the predicate (dyn param); yield the elem in source rep
  (wide passthrough). Set fs->elem_idx = source elem_idx at construction (6167).
- **take** (vm.c:1677): pure passthrough (already correct); set elem_idx =
  source elem_idx at construction (10333).
- **transform** (vm.c:1572): source yields wide (if wide-elem); push wide to
  the mapper whose body is now compiled wide (DROP the typer restore-pass in
  `typer__proc_result_enc`); mapper returns wide; yield wide. Set ts->elem_idx
  at construction (5992) from a compile-baked u16 operand on OP_TRANSFORM (the
  transform node's output elem enc; dyn for the vec path). For non-wide elems
  (i32/bool/struct/str) nothing changes — tagged stays.
- **for-loop** (compiler.c:10863): DROP the OP_TO_I64/U64/F64 unbox — the value
  arrives wide. SM-field path already stores+reads via inferred_type; leave it.
- **OP_STREAM_NEXT** (vm.c:9730): pushes whatever pull returns (wide for
  wide-elem). For-loop reads wide (unbox dropped). **userland `stream_next`**
  (compiler.c:7266, returns dyn): after the opcode, emit a box when the arg's
  static elem is wide — or route it through a dyn variant. Must not leak wide
  to a dyn sink.
- **GC** (gc_collect.c:452): in OBJ_STREAM trace, skip `cached_value` when
  `elem_idx` is a wide scalar (a wide value must not be traced as a pointer).
  gc.c sees no jacl.h macros — use the literal sentinel as elsewhere.
- **typer** (`typer__proc_result_enc`): drop the restore-pass (body stays typed
  it:<elem>; the wide pipeline makes it correct). This is where restore removal
  finally lands.

Validate: full 480 suite, all-or-nothing. No green intermediate exists (the
for-loop unbox is keyed on shared static type), so build once at the end and
debug from there. grep -E "error:" the FULL build log (see STREAM_TYPING_ISSUES.md).

Risks to watch: u64/f64 wide variants (range is i64-only, but generators can
yield u64/f64 — handle all three in the generator-pull unbox); the OP_TRANSFORM
operand wiring (vec path must emit a dyn sentinel so it still works); structs/
str streams must be untouched (only i64/u64/f64 flip).

## B as landed (differs from the recipe above — read this)

Landed green (480/480, 91/91 binaries). Two deliberate design choices vs the
original recipe:

1. **`OP_STREAM_NEXT` is a tagged-normalizing terminal**, not a wide bind.
   It pulls via `vm__pull_stream_dyn` (boxes wide→tagged), and the for-loop
   keeps its existing tagged→wide unbox. So the wide rep flows through the
   *lazy composition* (range→filter→transform→take, incl. the wide mapper
   body), and ALL terminal consumers (for-loop, collect, each, spread, userland
   `stream_next`) normalize to tagged uniformly. This removed the need to touch
   the for-loop codegen, the SM-field boxing, the userland `stream_next` box,
   AND the GC (cached_value stays tagged — generators cache `yield_value` which
   is tagged, so no OBJ_STREAM skip is needed; adding one would've been a bug).

2. **Scoped to i64.** `vm__elem_idx_is_wide` returns true only for i64 (the
   dominant, fully-tested integer stream element — `range`, numeric
   generators). u64/f64 streams stay tagged end-to-end (status quo). The
   restore-pass in `typer__proc_result_enc` is dropped ONLY when a mapper param
   is bound to i64 (`body_wide`); for every other element type it is kept, so
   the body stays it:dyn over the tagged pipeline. The conversion helpers keep
   u64/f64 arms, so widening is a small change once the follow-up below lands.

**Fixed since: `gen_elem_idx` not reaching runtime closures.** `OP_CLOSURE`
(vm.c) copied `is_generator`/`is_sm_compiled`/… from the closure template but
NOT `gen_elem_idx`, so every runtime generator stream had `elem_idx=0`. This
was dormant ("foundation in place, not read") until `transform`-over-generator
became the first reader — and it was a latent bug in the *committed i64* work:
`transform`/`filter` over an i64 GENERATOR produced garbage (a tagged value
read as wide), while range-sourced transform worked, so the suite missed it.
Fixed by copying `gen_elem_idx` in OP_CLOSURE; regression test
`stream_transform_generator.jacl`.

**u64/f64 wide — DONE (contextual literals + runtime promotion).** Resolved the
tagging blocker two ways: (a) `yield X` now narrows a numeric literal to the
generator's `[Stream T]` element type (yield-time `expected_type`), so a
`[Stream f64]` genuinely carries f64 (typer.c, the yield branch of the
mutator-arg expected_type loop); (b) the four ordering opcodes (GT/LT/GE/LE)
**promote mixed numeric tags** at runtime (`vm__numeric_order`: any float →
double, else int64) instead of erroring — so a dyn-context predicate like
`filter [fs] [\ > $it 3.5]` (tagged-f64 element vs f32 literal) just works, and
the previously-coincidental i64-vs-i32 filter becomes principled. With those,
`vm__elem_idx_is_wide` + the `body_wide` check now include u64/f64. Tests:
`stream_wide_f64_u64.jacl`; unit test `op_lt_mixed_numeric_promotes` (was
`op_lt_mixed_type_error`). The pure compile-time predicate-narrowing path was
rejected as too coupled (it needs wide values pushed into wide-compiled
predicate bodies). NOTE: `OP_EQ` was already permissive (cross-type allowed);
arithmetic dyn ops weren't touched (transform mappers use typed wide ops).

**(historical) Earlier f64/u64 blocker write-up:**
Enabling f64/u64 in `vm__elem_idx_is_wide` + `body_wide` makes the transform
mapper body work (verified: `collect [transform [fs] [\ * $it 2.0]]` →
`[vec 3 10]`), BUT breaks `filter [fs] [\ > $it 3.5]` with a *runtime* type
error "f64 and f32". Root cause: **float literals are f32**, so a `[Stream f64]`
that does `yield 1.5` actually carries an f32-tagged value. Tagged, that f32
flows to the predicate and compares f32-vs-f32 (the f32 literal) — fine. Under
the wide flip, the value is widened to f64 and `vm__stream_to_tagged` re-tags
the box-back as **f64** (the original f32 tag is unrecoverable from raw bits),
so the predicate compares f64-vs-f32-literal → mismatch. u64 has an analogous
large-value box-back tag risk (small u64 tags as i32, large as u64). Unblocking
needs the f32/f64 (and large-u64) literal/element tagging settled first — a
float-typing concern separate from the stream rep flip. The conversion helpers
keep u64/f64 arms; widening is then a one-line flip of `vm__elem_idx_is_wide`
+ `body_wide`. No `[Stream f64]`/`[Stream u64]` tests exist — add with the fix.

## Net result

`\` is pure sugar (Phase 1); lambda result inference is general; for i64
streams the value flows wide through lazy composition, the transform mapper
body compiles and runs wide, and the restore-pass is gone — the clean end
state for i64. Terminals re-tag uniformly. u64/f64 wide is a scoped follow-up.

## Deliberately out of scope

Named lambdas **reused across different element types** stay `dyn` (would need
per-type monomorphization — more startup + bytecode; rare). Inline `[\ … ]`
has one call site, so its single typed compilation is free and optimal.

## Verify

```
timeout 240 ./build.sh            # 91/91 binaries; jacl suite 480/480
.build/jacl_harness               # full suite
grep -E "error:" the full build log — never tail/head it (STREAM_TYPING_ISSUES.md)
```
