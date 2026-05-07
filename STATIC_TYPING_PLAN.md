# Static Typing Migration Plan

Goal: convert the typer (`src/typer.c`) into a real static type checker
that is the sole authority on AST node types. Retire the compiler's
dual type-tracking state (`c->last_expr_type`, `c->last_struct_idx`,
`c->last_key_struct_idx`, `c->expected_type`, plus `c->inline_repr`
where it serves type queries). Make `dyn` a first-class member type
rather than a sentinel for "the typer doesn't know."

This is a multi-week refactor, not a rewrite. The compiler stays. The
typer grows. The seam between them gets cleaner.

## Companion documents

- `REFACTOR_FINDINGS.md` — session notes on what's been tried; this
  plan supersedes its "finish the typer" section.
- `SIMPLIFICATION.md` — older, broader simplification notes.

---

## Where the type system lives now (post-Stage-3)

Stages 0–3 are complete. This section captures the final architecture
so you don't have to read the migration history to understand the
present-day code.

**Pipeline:**

```
parser   →  AST                    (untyped; LIT_INT/FLOAT/STRING get
                                    parser-set inferred_type defaults)
typer    →  AST + annotations      (every node reachable from the walk
                                    has inferred_type, inferred_struct_idx,
                                    inferred_key_struct_idx populated)
compiler →  bytecode               (reads type identity from AST; tracks
                                    runtime stack representation in
                                    c->last_expr_type for ensure_boxed)
```

**Where each piece of state lives:**

| State | Lives on | Purpose |
|---|---|---|
| Declared type of an expression | `AstNode.inferred_type` | Set by typer; what the value's type *is*. |
| Struct registry index for STRUCT/typed-vec/typed-map | `AstNode.inferred_struct_idx` | Set by typer; aligned with compiler's `struct_registry`. For typed collections, this is the *element* idx (or a `JACL_SCALAR_TYPE_IDX` sentinel for scalar elements). |
| Map key struct index for typed-map | `AstNode.inferred_key_struct_idx` | Set by typer. |
| Runtime stack representation post-emit | `Compiler.last_expr_type` | Reflects what's *actually on the stack* mid-codegen — distinct from declared type when var-ref loads emit unbox code (e.g. mutable cell loads emit `OP_GET_CELL_LOCAL + OP_TO_DYN`, leaving the stack DYN even when the binding is i64). Read only by `compiler__ensure_boxed`. |
| Inline-struct stack arrangement | `Compiler.inline_repr`, `inline_ref_base`, `inline_ref_offset` | Codegen state for chained struct field access. Not type-system state despite the name. |

**Shared encoding:**

`JACL_SCALAR_VEC_BASE` (= 0xFF00, in `ast.c`) defines the sentinel
range for typed-collection element indices. Real struct registry
entries live below 0xFF00; values in `[0xFF00, 0x10000)` encode a
scalar `JaclType` (e.g. `[Vec i64]` carries `JACL_SCALAR_TYPE_IDX(TYPE_I64)`
in its `inferred_struct_idx`). Both typer and compiler use the same
encoding via the `JACL_SCALAR_TYPE_IDX` / `JACL_TYPE_IDX_TO_SCALAR` /
`JACL_IS_SCALAR_TYPE_IDX` macros.

**Slot reservations in both registries (compiler's `struct_registry`
and typer's `tc->structs`):**

- Slot 0: reserved for "dyn placeholder" (`defs[0] = NULL`).
- Slot 1: reserved for the ctx struct. Always populated (built-in
  `pwd: str` field, plus any user `ctx Type field = default`
  declarations). Both passes pre-reserve at registry-init time so the
  slot indices align across registries without runtime coordination.
- Slot 2 and up: user-declared structs in source order. Anonymous
  inline struct types (`struct{x:i32,y:i32}`) are registered on
  demand by both passes during the defstruct walk; same source order
  → same slot indices.

**Key invariants:**

1. The typer is the sole authority on `inferred_type` /
   `inferred_struct_idx` / `inferred_key_struct_idx`. The compiler
   reads them; never writes them.
2. Compile-time AST rewrites (e.g. `HEAD_SET`'s arrow desugar) must
   either preserve outer-node identity (so the typer's annotation
   on the outer survives) or be rare enough that the typer's
   bare-name-receiver fallback handles them. Currently only the
   set-rewrite exists, and it's covered by the typer's fallback.
3. `compiler__ensure_boxed` is the only place that reads
   `c->last_expr_type`. New code that needs to know "what type was
   just compiled" should read `args[i]->inferred_type` from the AST,
   *unless* it specifically needs the post-emit stack representation
   (which is rare — the only reason ensure_boxed needs it is the
   var-ref-load-of-mutable-cell case where the load emits an unbox
   that the AST type doesn't reflect).

**Where to find things in the source:**

| What | File:Function |
|---|---|
| Typer entry point | `typer.c:typer_infer` |
| Typer struct/proc/ctx pre-passes | `typer.c:typer__register_structs / __register_procs / __register_ctx_struct` |
| Typer anonymous-struct registration | `typer.c:typer__register_inline_struct` |
| Typer command type rules | `typer.c:typer__infer_command_inner` |
| Typer's HEAD_DOT field-resolve | `typer.c:typer__infer_command_inner` (search "HEAD_DOT") |
| Compiler reads type from AST | `compiler.c:compiler__effective_type` (one-line wrapper around `n->inferred_type`) |
| ctx struct slot reservation | `compiler.c:struct_registry__init`, `typer.c:typer_infer` |
| Shared scalar-elem encoding | `ast.c` (the `JACL_SCALAR_*` macros, after `JaclType` enum) |
| Compiler's runtime-state tracker | `compiler.c:compiler__ensure_boxed` (only reader of `c->last_expr_type`) |

**What's *not* here yet:**

- Stage 1e — typer emits type errors directly. ✅ COMPLETE.
  Eight error sites fire from the typer using the shared formatters
  in `src/type_error.c`. Includes the `set $recv->field val` arrow
  form (covers ctx-field-set and arbitrary chained struct field-set
  on the pre-rewrite tree).
- Stage 1f — `dyn` as a real type with defined-semantics ops.
  ✅ COMPLETE. Four rules landed: proc-call dyn-into-typed-param,
  binary-op arithmetic concrete-mismatch, binary-op unboxed
  comparison, dyn-into-typed-return. The previously-open "mixed
  dyn/typed binary" case was resolved by the revised decision 2
  framing: expression-level mixing is permissive, the barrier
  check fires at commitment sites only.
- Stage 4 (separate `TypedAstNode` from `AstNode`) — optional;
  weighed against benefit. Not pursued.
- Stage 5 (pointer types `*T` for FFI) — design pending.

---

## End state

The architecture today (after Stages 0–3 and the partial 1e/1f
work):

```
parser    →  AST                    (untyped, scope_marks stamped;
                                      LIT_INT/FLOAT/STRING get
                                      parser-set inferred_type defaults)
typer     →  AST + type annotations (every node reachable from the
                                      walk has inferred_type)
compiler  →  bytecode               (reads type identity from AST;
                                      tracks runtime stack rep in
                                      c->last_expr_type for ensure_boxed)
```

**Compiler invariants, achieved:**

- ✅ The compiler never *infers* a type for declared identity. It
  only *consumes* AST annotations. The surviving `last_expr_type`
  field tracks post-emit stack representation, not type identity.
- ✅ Functions that need a type read it from the AST node, not
  from `Compiler` state.
- ✅ `c->expected_type`, `c->last_struct_idx`, `c->last_key_struct_idx`
  are gone — the typer pushes context down during its walk and
  stamps each node with the narrowed type.
- 🟡 Type errors are detected and reported during typing for six
  sites (Stage 1e); the compiler still detects + reports for the
  rest (typed-collection element ctor, plus the deferred Stage 1f
  rules). Both passes use the same shared formatters when a
  formatter exists.

**Typer invariants, achieved:**

- ✅ Every `AstNode` reachable by the walk has `inferred_type` set
  to a real `JaclType`.
- 🟡 `TYPE_DYN` is mostly treated as a real type ("dynamically-typed
  value") rather than "I don't know" — Stage 1f's three landed
  rules treat dyn as a first-class boundary marker. The two
  deferred rules still treat dyn permissively in mixed contexts.
- ✅ Operations on dyn operands have defined typed semantics:
  `[+ $a $b]` types as dyn when both operands are dyn, types as
  the operand's type when both are the same concrete type, errors
  on concrete-mismatch arithmetic.

**The `dyn` type, properly:**

| Today (before migration)                        | Now                                    |
|------------------------------------------------|----------------------------------------|
| `TYPE_DYN` = "typer gap; ask `c->last_expr_type`" | `TYPE_DYN` = "value of unknown static type; runtime-tagged"     |
| Implicit fallback at 22 sites                   | No fallback (`compiler__effective_type` was deleted) |
| Untyped def/mut → DYN binding                  | Same; DYN binding is the explicit boundary marker (decision 2) |
| Mixed-type if-branches → DYN                    | Same; DYN is the unified result |

---

## Stages

Each stage ships independently and leaves the codebase passing tests.
Stop at any stage and the work to that point is still useful.

### Stage 0 — Test infrastructure and decisions (2-3 days)

Before any architectural change, set up the rails to detect regressions
that the current 93 tests would miss.

**Tasks:**

1. **Add a typer-only test harness.** Run the typer on a corpus of
   `.jacl` programs and assert annotations on specific AST nodes.
   Today the only way to test the typer is end-to-end through codegen,
   so silent typer bugs are invisible.
2. **Add a typer↔compiler disagreement check.** During development,
   after the typer runs, walk the AST again during compile and assert
   `args[i]->inferred_type == c->last_expr_type` after each
   `compile_node` call. Behind a `JACL_TYPER_AUDIT=1` build flag.
   Surfaces every place the two passes diverge — the bug list for
   Stage 1.
3. **Expand the type-error test corpus.** Today most type errors are
   reported by the compiler (e.g., "type error: cannot assign str to
   i32 binding 'x'"). Add tests for: typed-collection mismatches in
   destructure, struct field type mismatches, ctx field type
   mismatches, narrowing escape from `box?` branches. Each becomes a
   typer responsibility in Stage 1.
4. **Decide the open questions below.** Write decisions into this doc.

**Exit criteria:**

- New test harness exists and is run by `build.sh`.
- The audit-mode build runs against the existing 93 tests with all
  divergences logged (zero is unlikely; the goal is to know the list).
- Open decisions resolved.

### Stage 1 — Make the typer total (2-3 weeks)

Walk every AST shape; assign every node a real type. The typer becomes
correct standalone, but the compiler still uses its own tracking. No
behavior change.

**Tasks (status as of last commit in this branch):**

1. **Add the missing AST annotation slots.** ⏳ DEFERRED.
   - `inferred_key_struct_idx` was scoped here, but turns out the
     typer already handles typed-map keys via `inferred_struct_idx`
     for the value-type idx (compiler tracks key idx separately).
     Stage 2 doesn't actually need a new field unless we want the
     typer to *enforce* typed-map key types — currently a Stage 1c
     follow-on, not blocking.
   - `inline_repr` — confirmed in decision 3 that this stays in
     compiler as codegen state. No AST field needed.
2. **Stage 1a — control-flow result types.** ✅ MOSTLY DONE.
   Done: `if` (then/else unify), `while`, `for`, `yield`, `break`,
   `continue`, `dot` field-set, `try` (body/handler unify),
   `with-ctx` (body's tail type). Not done: `match` (each typer
   says DYN today — needs arm-walk + unification when match is
   used in code; not load-bearing in current corpus).
3. **Stage 1b — call returns.** ✅ PARTIAL.
   Done: ~30 entries in `fixed_returns` (predicates, lengths,
   strings, streams, dyn collection ctors, signal/cancel, syntax-*
   introspection, while/for/yield, signal/cancel). HEAD_PIPE result
   inherits rhs. parallel→TYPE_VEC. spawn→TYPE_FUTURE with element
   type from body's tail; await of TYPE_FUTURE narrows to the
   element type. race narrows to the unified body tail type when
   all bodies produce the same concrete type, dyn otherwise. New
   `TYPE_FUTURE` enum value (jacl.h, ast.c). `[Future T]` /
   `[Vec T]` / `[Map T]` / `[Map K V]` surface annotations are now
   accepted by the compiler in def position (routed through the
   untyped-def path so the binding inherits the RHS's effective
   type — the typer remains the source of truth for the static
   type identity). hash → i32. take preserves the receiver's
   collection kind (typed_vec → typed_vec, vec → vec, stream →
   stream). reset on a struct-box → TYPE_STRUCT, else NIL. Not
   done: `first`, `swap`, `interpret`, FFI cluster — these type
   to dyn today. Plus closures and imported procs are absent from
   the typer's proc registry.
4. **Stage 1c — typed-collection element narrowing.** ✅ DONE.
   Typed-collection constructor `[[Vec T] ...]` / `[[Map K V] ...]`
   types as TYPE_TYPED_VEC/MAP with element `inferred_struct_idx`
   populated. `[vec-get $typed_vec idx]` / `[map-get $typed_map key]`
   narrow to the element type — TYPE_STRUCT for struct elements,
   the scalar JaclType for scalar elements (via the shared
   `JACL_SCALAR_TYPE_IDX` sentinel encoding). Proc params declared
   `[Vec T]` / `[Map K V]` and bindings declared with typed-
   collection types also propagate the elem struct_idx. Both
   scalar and struct cases are covered.
5. **Stage 1d — narrowing through every guard.** ✅ PARTIAL.
   Done: `[box? Type $x]`. Not done: typed-collection box guards
   (`box? [Vec T]`), error guards in `try`.
6. **Stage 1e — type errors emitted by the typer.** ✅ SUBSTANTIALLY
   COMPLETE. Typer error infrastructure (`TyperResult`,
   `typer__error`) plumbed through `typer_infer` to
   `compiler_compile`'s reporting machinery (commit `77709de`). Six
   error sites converted to fire from the typer:
   - typed def/mut mismatch (`jacl_format_assign_mismatch` /
     `jacl_format_assign_dyn_unnamed`) — commit `640ae7d`
   - typed set mismatch (`jacl_format_assign_mismatch` /
     `jacl_format_assign_dyn_named`) — commit `f1a6af2`
   - proc declared-return vs body tail
     (`jacl_format_proc_return_mismatch`) — commit `f1a6af2`
   - struct field-set mismatch (`jacl_format_field_mismatch` /
     `jacl_format_field_dyn_assign`) — commit `402fd47`
   - struct constructor positional-arg field-type mismatch
     (`jacl_format_field_mismatch` + bespoke wording for the
     "got dyn" case) — commit `16db519`
   - proc call positional-arg param-type mismatch (compiler-matched
     bespoke wording — no shared `argument N of NAME` formatter
     yet) — commit `16db519`

   The compiler's parallel checks at compiler.c:5983-5995, 6204-6221,
   6714-6726, 7272-7293, 9722-9738, 10094-10113, 10359-10378 still
   exist as a backstop; the typer fires first and its message reaches
   the user via `compiler__error` re-injection at the `typer_infer`
   call site. Struct-to-struct narrowing stays compiler-owned (the
   typer's same-struct-idx tracking isn't fully aligned with the
   compiler's across module boundaries — would need a follow-on).

   **Remaining:** none. All Stage 1e error sites fire from the typer.

   **Landed since first writing of this plan:**
   - Typed-collection element ctor mismatches (`[[Vec T] e1 e2 ...]`,
     `[[Map T] k v ...]`, `[[Map K V] k v ...]`). Three new shared
     formatters in `src/type_error.c`: `jacl_format_typed_vec_elem`,
     `jacl_format_typed_map_value`, `jacl_format_typed_map_kv`. Both
     typer and compiler call them; typer fires first when both
     element and key/value types are knowable from the typer's
     struct registry. Compiler still backstops for unknown structs
     and unsupported scalars.
   - Arrow-form field-set (`set $recv->field val`). The typer now
     recognizes the pre-rewrite shape (SET with arg[0] a 2-arg dot
     command) directly in `typer__handle_set` and applies the same
     field-type check as the 3-arg dot handler. Covers ctx-field-set
     plus arbitrary chained struct field-set without requiring the
     compiler's HEAD_SET rewrite to run first.
7. **Stage 1f — `dyn` as a real type.** ✅ PARTIAL.

   **The rules** (per decisions 1 and 2):

   | Form | Status today | Stage 1f intent |
   |------|--------------|-----------------|
   | `def i32 x $i32_val` | typer + compiler accept | accept |
   | `def i32 x $dyn_val` | typer errors (1e ✅) | error |
   | `def dyn x $i32_val` | accept | accept (dyn binding is the boundary marker) |
   | `[typed_proc $i32_arg]` against `i32` param | accept | accept |
   | `[typed_proc $dyn_arg]` against typed param | typer errors (1f ✅, commit `e25d9d5`) | error |
   | `[. $struct field $dyn_val]` typed field | typer errors (1e ✅) | error |
   | `[. $struct field $i32_val]` against `f32` field | typer errors (1e ✅) | error |
   | `[+ $i32 $i32]` | accept; result `i32` | accept |
   | `[+ $i64 $i64]` | accept; result `i64` | accept |
   | `[+ $i32 $i64]` | typer errors (1f ✅, commit `aa702cd`) | error |
   | `[+ $i32 $f32]` | typer errors (1f ✅, commit `aa702cd`) | error |
   | `[== $i32 $f32]` | accept; result `bool` (cross-type comparison meaningful) | accept |
   | `[== $i64 $i32]` | typer errors (1f ✅, mirrors compiler — unboxed can't dispatch) | error |
   | `[+ $i32 $dyn]` | accept; result `dyn` | accept (decision 2 — expression-level mixing is permitted; result types `dyn`) |
   | `[+ $dyn $dyn]` | accept; result `dyn` | accept |
   | proc returns `$dyn_val` to typed return | typer errors (1f ✅, this commit) | error |

   **Done in Stage 1f:**
   - Proc-call dyn-into-typed-param check fires from typer with cast
     hint (`(use [to T $val])`). Mirrors compiler.c:10370-10378.
     Commit `e25d9d5`.
   - Binary-op arithmetic concrete-mismatch (any combination of
     concrete numeric types). Tightens `[+ $i32 $f32]` from
     accept-via-dispatch to compile-error per decision 1.
     Commit `aa702cd`.
   - Binary-op comparison concrete-mismatch when either operand is
     unboxed (mirrors compiler.c:3859-3868). Cross-type tagged
     equality remains permitted because tests rely on it
     (`[== [vec 1] 1] → false`).
   - Dyn-into-typed-return: typed proc whose body tail is dyn now
     errors with cast-hint wording. Uses
     `jacl_format_proc_return_dyn` in `src/type_error.c`. The
     existing concrete-mismatch path
     (`jacl_format_proc_return_mismatch`) is preserved.

   **Resolved (no longer Stage 1f work):**

   - **Mixed dyn/typed binary** (`[+ $i32 $dyn]`): per the revised
     decision 2, expression-level mixing is permitted and types as
     `dyn`. Current behavior is correct; no rule needed. The
     barrier check fires at whatever commitment site the result
     flows into.
8. **Run the audit-mode build continuously.** ✅ DONE during the
   migration; the audit machinery was deleted in commit `0aa722f`
   once it had served its purpose.

**Exit criteria (historical — Stage 1 hit them through the
combination of typer rules in Stage 1 + consumer migration in
Stage 2):**

- Audit mode reports zero typer↔compiler disagreements across the
  full test corpus.
- Typer assigns a non-sentinel type to every node in every test.
- New typer-only tests pass.
- Compiler unchanged behaviorally; no `c->last_expr_type`
  consumers migrated yet.

### Stage 2 — Migrate compiler consumers (1-2 weeks)

For each cluster of `c->last_expr_type` / `c->last_struct_idx` reads
in `compiler.c`, switch to reading from the AST. Each cluster is one
commit, fully tested, individually revertable.

**Shortcut path enabled by Stage 0 commit `0860e56`:** all 22
`c->last_expr_type` reads were already collapsed into a single
helper `compiler__effective_type(c, n)` which reads
`n->inferred_type` and falls back to `c->last_expr_type` when the
typer left it DYN. Stage 2's true minimal edit is to **delete the
fallback branch from that helper** — one edit, all 22 sites
correct in one shot. Per-cluster framing below is then about
verifying each cluster's tests still pass, not about the
mechanical edits themselves.

For `c->last_struct_idx` (read at ~80 sites unconditionally, not
through a helper), the per-cluster migration is real work — each
site needs to switch to `args[i]->inferred_struct_idx`. The typer
now propagates struct_idx for bindings, struct field access, and
typed-collection cases (Stage 1 commits `1f220a4` and `c4b9d65`).

**Cluster groupings (approximate):**

| Cluster | Sites | Notes |
|---------|-------|-------|
| Binary ops | 2 | `compile_binary` LHS/RHS |
| def/mut/set typed bindings | 6 | Includes upvalue and global paths |
| Struct constructor args | 1 | Variadic loop |
| Struct field set | 3 | Three different LHS shapes |
| Ctx field set | 2 | Decl and override paths |
| `if` branch unification | 2 | then/else |
| `for` collection / typed-vec loop | 1 | Plus take/first |
| `to` cast validation | 1 | Source type read |
| Unary minus | 1 | Type check before negate |
| Function call argument typing | 2 | Param type matching |

Total: ~21 sites (matches the 22 we have, minus 1 that may already be
inside the binary-op cluster).

**Per-cluster recipe:**

1. Identify all reads of `c->last_expr_type` / `c->last_struct_idx`
   in the cluster. (`c->last_key_struct_idx` reads exist but the
   typer has no `inferred_key_struct_idx` field yet — either add
   one as a prerequisite or leave key-idx reads as compiler-side
   for now; affects only typed-map sites.)
2. For `c->last_expr_type`: prefer `compiler__effective_type(c,
   args[i])`, which already does the right thing. If the helper's
   fallback branch is gone (single global edit done first), the
   reads naturally read from `inferred_type` only.
3. For `c->last_struct_idx`: replace with
   `args[i]->inferred_struct_idx`.
4. Remove the corresponding writes if they exist only to feed this
   cluster.
5. Run the full test suite.
6. Run audit mode; remaining writes are still consumed somewhere.
7. Commit.

After all clusters migrate, `c->last_expr_type` writes still happen
but are no longer read.

**Exit criteria:**

- Zero reads of `c->last_expr_type`, `c->last_struct_idx`,
  `c->last_key_struct_idx` in `compiler.c`.
- Tests pass.

### Stage 3 — Delete dead state (2-3 days)

Mechanical cleanup. The fields are dead by Stage 2's exit; remove
them.

**Status (Stage 2/3 progress):** `c->last_struct_idx` reads in
compiler.c are down to **2 sites** (2025-05 / commit `a1dfe64`):

- compiler.c — audit-mode comparison code (audit reads both
  sides by definition; not load-bearing for codegen).

`c->last_key_struct_idx` reads in compiler.c are down to **0
sites** (commit `a1dfe64`). The two consumers (typed-map equality,
typed-map print) now read `args[i]->inferred_key_struct_idx` from
the AST. Writes still exist throughout the compiler (they're paired
with last_struct_idx writes via compiler__set_type and direct
assignments) — these become deletable when last_struct_idx and
last_expr_type go away as a unit.

The CTX_STRUCT_PENDING sentinel was deleted in commit `698e951`.
ctx now occupies pre-reserved struct_registry slot 1 in both the
compiler and the typer; the four sentinel uses became
`reg->ctx_type_idx` reads. This also lets the typer fully type
`$ctx.field` accesses via its existing HEAD_DOT rule (the ctx
struct's field types are now in tc->structs[1]).

**Why c->last_expr_type can't yet be deleted:** it's still load-
bearing for compile-time dispatch decisions inside compile_command
branches (`if (c->last_expr_type == TYPE_TYPED_VEC) { ...emit
typed_op... }`). Migrating those dispatches to read
`args[i]->inferred_type` is the missing piece — mostly mechanical,
but spread across ~50 sites in compiler.c. Once done, the field
declaration can be removed.

**Update (commit `6ddee97`):** all 24 dispatch reads of
`c->last_expr_type` migrated to `args[i]->inferred_type`. Zero
load-bearing reads outside audit code remain.

**Update (commit `ed1f4c3`):** `c->last_key_struct_idx` field
deleted (its consumers were migrated to
`args[i]->inferred_key_struct_idx`).

**Update (commit `fa3ff9c`):** audit's struct_idx_diff
comparison removed — `struct_idx_diff` was 0 across all compiles
since the registries aligned, and keeping it forced
`c->last_struct_idx` alive. Two of the four remaining read sites
also migrated.

**The blocker for deleting `c->last_struct_idx`:** HEAD_SET's
arrow-desugar handler at compiler.c:6155 rewrites the AST at
compile time — it walks an expression like `[set [. [. ln start] x] 77]`,
finds the bare-name `ln`, replaces it with a fresh AST_VAR_REF
node, then calls `compile_node` on the rewritten dot expression.
The typer never visited the rewritten tree, so the inner dot's
`inferred_struct_idx` reflects the pre-rewrite state (DYN, since
`ln` is a bare LIT_STRING in the pre-rewrite tree, not a typed
var-ref). Two chained-dot read sites (`inline_sidx` at
compiler.c:9752, `struct_idx` at compiler.c:9863) read
`c->last_struct_idx` for this reason — it's the only correct
source post-rewrite.

**Path 2 attempted (commit `1723747`):** the typer-side companion
landed — `HEAD_DOT` rule now resolves bare-name LIT_STRING
receivers via `typer__scope_resolve`, so a `set ln->start->x`
expression types correctly even though the parser produces a bare
"ln" string at the bottom of the chain. The set rewrite is no
longer load-bearing for the typer's annotation correctness —
inner-dot `inferred_struct_idx` is set on the OUTER node before
the rewrite touches the inner LIT_STRING.

**Update (commit `61ea693`):** anonymous-struct gap closed.
`typer__register_inline_struct` parses canonical strings
(`struct{x:i32,y:i32}`) and registers entries indexed by the
canonical name. `typer__register_ctx_struct` always pre-populates
ctx with the built-in `pwd: str` field. With those, the chained-
dot reads migrate to the AST and `c->last_struct_idx` is deleted
from compiler.c and jacl.h.

**Update (commit `0aa722f`):** typer-audit machinery deleted —
the audit served its purpose as a Stage-1/2 development bridge.
Removed: `TyperAuditStats`, `compiler__audit_*` functions, the
`JACL_TYPER_AUDIT` compile flag, build.sh's `--audit` option.

**Final state of Stage 3:**

`c->last_expr_type` survives. Its only reader is
`compiler__ensure_boxed`, which has runtime-state semantics: it
reflects the post-emit stack representation, not the declared
AST type. Var-ref loads of mutable cells emit
`OP_GET_CELL_LOCAL + OP_TO_DYN` as a pair (compiler.c ~10973),
leaving last_expr_type=DYN even when the binding's declared
type is e.g. i64. Reading `args[i]->inferred_type` instead would
re-emit OP_TO_DYN on a value that's already boxed, corrupting
the stack. Documented in commit `2297a56`.

`c->last_struct_idx` and `c->last_key_struct_idx` are deleted.
`compiler__get_type` (TypeInfo getter) is deleted.
The typer-audit machinery is deleted.

The compiler's type tracking is now narrowly scoped to:
"what's on the stack right now, for ensure_boxed's auto-boxing."
Type identity (declared types of bindings, struct registry
indices, typed-collection element types) lives entirely on
AstNode (`inferred_type`, `inferred_struct_idx`,
`inferred_key_struct_idx`).

**Tasks (all complete):**

1. ✅ `c->last_struct_idx` and `c->last_key_struct_idx` deleted
   (commits `61ea693`, `ed1f4c3`). `c->last_expr_type` retained
   intentionally — its only reader is `compiler__ensure_boxed`,
   tracking post-emit stack representation rather than declared
   type. Documented in commit `2297a56`.
2. ✅ `c->expected_type` deleted. The two literal-narrowing reads
   at AST_LIT_INT / AST_LIT_FLOAT now read `node->inferred_type`,
   which the typer narrows via its own `tc->expected_type`
   plumbing. ~30 writes deleted.
3. ✅ `compiler__effective_type` inlined and deleted. Its ~25 call
   sites read `(JaclType)n->inferred_type` directly.
4. ✅ `c->inline_repr` audited — purely codegen state for chained
   struct field access. No type-shadow uses to remove.
5. ✅ `compiler__set_type` deleted. Was a one-line wrapper around
   `c->last_expr_type = ti.type;` after struct_idx stopped being
   tracked; call sites now assign directly.
6. ✅ Re-flow done as part of (2) — the unused variable comments
   and the `compile_binary` LHS push that did nothing got cleaned.

**Exit criteria:**

- ✅ `Compiler` struct no longer has dead type-tracking fields.
  Surviving fields (`last_expr_type`, locals[i].type, upvalues[i].type,
  global_arities[i].type, return_type) all drive codegen decisions.
- compiler.c LOC: net -120 across Stage 3 cleanup commits.

### Stage 4 — Optional: separate the typed AST (1 week)

At Stage 3's exit the typer is the source of truth, but it still
mutates `AstNode` in place. The cleaner architecture is a separate
`TypedAstNode` produced by the typer and consumed by the compiler.

**Why optional:** in-place annotation works fine. The benefit of
separation is *interface clarity* — the compiler's input type becomes
`TypedAstNode*` and you can't accidentally compile an un-typed AST.
Worth doing if/when the compiler grows further; not critical for
correctness.

### Stage 5 — Pointer types for in-process debugger scripting

After the migration architecture is stable, add typed pointers to the
language so JACL can drive in-process memory inspection of C structs
(the use case: gdb/lldb-style scripts that walk `[Ptr Point]`,
read/write fields, scan arrays).

**Surface syntax:** `[Ptr T]` — bracketed compound type annotation
matching the existing `[Vec T]`, `[Map K V]`, `[Future T]` idiom. The
arrow form `$p->x` reuses the existing `->` field-access syntax
(already lexed and routed through the dot-handler today).

**Design decisions (resolved before implementation):**

1. **Mutability:** `[Ptr T]` only; reads and writes both legal.
   `[Ptr mut T]` distinction deferred — adds parse complexity for low
   payoff in an inspection workflow.
2. **Deref ergonomics:** auto-deref on field access. `$p->x` reading
   through `[Ptr Point]` matches debugger user expectations. Single
   deref only — no implicit `[Ptr [Ptr T]]` chains.
3. **Operations:** `[ptr-cast [Ptr T] $u64]` (raw addr → typed
   pointer), `[ptr-addr $p]` (back to u64), `[ptr-deref $p]` (load
   pointee), `[ptr-offset $p $n]` (typed: adds `n * sizeof(T)`),
   `[ptr-diff]`, `[ptr-eq]`, `[ptr-lt]`. Field auto-deref handles the
   common case; `ptr-deref` is for scalar-pointer loads (`[Ptr i32]`).
4. **Typed externs:** new top-level `extern <ret-type> <name>
   <params>` mirrors `proc` syntax minus the body. Lets the typer
   record native-fn signatures so calls type-check without per-site
   casts.
5. **Generic over pointee:** one `TYPE_PTR` enum value carries the
   pointee parameter via the existing `inferred_struct_idx` slot
   (struct registry idx for `[Ptr Point]`; `JACL_SCALAR_TYPE_IDX`
   sentinel for `[Ptr i32]`). Same encoding pattern as typed
   collections and `[Future T]`.

**Stage 5a — pointer type + cast (foundation, ~1 week) — IN
PROGRESS.** Done in this commit:

- `TYPE_PTR` added to `JaclType` enum (`ast.c` canonical, `jacl.h`
  mirror).
- `[Ptr T]` recognized by the typer in def/mut/set and proc-param
  positions; `typer__ptr_type` helper mirrors `typer__future_type`.
- Compiler accepts `[Ptr T]` annotations: in def positions
  `declared_type` becomes `TYPE_U64` (the runtime storage rep), with
  a `ptr_u64_compat` exception in the type-check so `def [Ptr T] p
  [ptr-cast …]` compiles. In proc-param positions the parameter is
  registered as `TYPE_PTR` with the pointee idx.
- `HEAD_PTR_CAST` and `HEAD_PTR_ADDR` head IDs added (ast.c lookup
  table + jacl.h mirror).
- `[ptr-cast [Ptr T] $u64]` builtin: typer marks the AST node as
  `TYPE_PTR` + pointee idx; the compiler emits no opcode beyond the
  value expression. Errors on non-`[Ptr T]` first arg or non-u64
  value.
- `[ptr-addr $p]` builtin: typer narrows the result to `TYPE_U64`,
  errors on non-pointer operand.
- Typer rules:
  - Pointer arithmetic via `+ - * / %` → compile error (use
    `ptr-offset`).
  - Comparing two pointers with different pointees → compile error.
  - Assigning a pointer of the wrong pointee to a typed binding →
    compile error.
- Tests: 4 new typer-only cases (`test_ptr_cast_returns_typed_ptr`,
  `test_ptr_cast_struct_pointee`, `test_ptr_addr_returns_u64`,
  `test_ptr_t_def_annotation`); 6 new type-error cases
  (`ptr_cast_bad_first_arg`, `ptr_cast_value_not_u64`,
  `ptr_addr_not_ptr`, `ptr_pointee_mismatch_assign`,
  `ptr_arithmetic_plus`, `ptr_compare_different_pointees`); 2 new
  `.jacl` corpus files (`ptr_cast_roundtrip.jacl`,
  `ptr_struct_pointee.jacl`).

**Stage 5a — typed externs ✅ COMPLETE**

- New `extern [ret-type] name {params}` declaration form. Typer
  registers the signature in its proc registry; calls return the
  declared type without per-site casts. Runtime dispatch flows
  through the existing native-fn registry as today
  (`embed__register_native` is still where the C function lives) —
  the extern statement itself produces nil at runtime.
- Compound return types (`[Ptr T]`, `[Future T]`, `[Vec T]`,
  `[Map K V]`) accepted in both `proc` and `extern` return
  positions via the new `typer__resolve_return_type` helper.
- New TOKEN_EXTERN + HEAD_EXTERN; dedicated parser path
  `parser__parse_extern_form` mirrors `parser__parse_proc_form`
  minus the body block.
- Tests: 4 new typer-only cases (`test_proc_ptr_return`,
  `test_proc_future_return_compound`, `test_extern_ptr_return_call`,
  `test_extern_with_params`), 2 new type-error cases
  (`extern_call_wrong_arg_type`,
  `extern_ptr_result_pointee_mismatch`), 2 new embed integration
  tests (`test_extern_typed_ptr_native`, `test_extern_u64_return`).

**Stage 5b — deref + field auto-deref ✅ COMPLETE**

- New opcodes `OP_PTR_LOAD` and `OP_PTR_STORE` (u16 byte_offset, u8
  field_type). Both pop a tagged-u64 pointer JaclVal, cast its bits
  to `void*`, and read/write the requested type at `*ptr+offset`.
  Trap on null and on non-u64 operands.
- `$p->x` and `[. $p field]` auto-deref when receiver is `[Ptr
  Struct]`. The compiler reads the field's offset from the struct
  registry (`StructTypeField.offset`) and emits `OP_PTR_LOAD` /
  `OP_PTR_STORE`. Set form leaves nil at the language level (the
  store opcode pushes the pointer for chaining; the compiler pops
  it and emits `OP_NIL`).
- `set $p->x val` arrow form auto-derefs through both
  `typer__handle_set` (pre-rewrite shape) and the 3-arg HEAD_DOT
  branch.
- `HEAD_PTR_DEREF` + `[ptr-deref $p]` builtin for whole-value
  scalar loads (`[Ptr i32]`, `[Ptr u64]`). Struct pointees error
  ("use $p->field for field access"); the build deliberately doesn't
  support whole-struct loads through pointers in this stage.
- Tests: 2 new typer-only (`test_ptr_arrow_field_narrows`,
  `test_ptr_deref_scalar_narrows`); 3 new type-error
  (`ptr_deref_on_struct_pointer`, `ptr_deref_non_pointer`,
  `ptr_field_set_wrong_type`); 3 new embed integration tests with
  real C struct memory (`test_ptr_field_read`, `test_ptr_field_write`,
  `test_ptr_deref_scalar`). The write test confirms the C side
  observes JACL's mutation.

**Stage 5b extension — nested struct fields through pointers ✅
COMPLETE.** Resolved the previously deferred case:

- Three new opcodes (appended at end of OpCode enum):
  - `OP_PTR_ADD_OFFSET (u16)` — pop u64 ptr, push ptr+offset.
  - `OP_PTR_LOAD_INLINE (u16 byte_offset, u16 sub_type_idx)` —
    pop u64 ptr, push N inline JaclVal slots from `*ptr+offset`.
  - `OP_PTR_STORE_INLINE` — same, but writing.
- New compiler helper `compiler__resolve_ptr_chain_step` walks
  arbitrarily-deep `[. recv field]` chains, accumulating field
  byte-offsets through embedded struct fields. The HEAD_DOT
  compile path (both 2-arg read and 3-arg set) collapses the
  whole chain into ONE opcode at the terminal field — never
  materializes intermediate struct values.
- Surface model (matches user expectation): `->` always produces a
  value. Scalar leaves load via OP_PTR_LOAD; struct leaves load via
  OP_PTR_LOAD_INLINE (whole-struct copy). `set $chain val` writes
  through with the combined offset. The intermediate `$o->inner`
  standalone is typed as `Inner` (struct value) — when used inside
  a longer chain, the chain accumulator skips materialization.
- New `[addr $expr]` builtin (HEAD_ADDR) — instead of loading the
  chain leaf, emits the base pointer + OP_PTR_ADD_OFFSET with
  cumulative offset. Result types as `[Ptr <field-type>]` (struct
  pointee for struct field, scalar pointee for scalar leaf). Lets
  the user grab a typed pointer to any nested struct or scalar.
- Tests: 3 new typer-only (nested chain narrows to scalar, struct
  field returns Inner, addr returns [Ptr T]), 2 new type-error
  (addr on non-chain, addr without [Ptr T] base), 4 new embed
  integration with real C nested structs (read, write, addr of
  scalar field, addr of embedded struct).

**Stage 5c — pointer arithmetic for array walking ✅ COMPLETE**

- New opcodes `OP_PTR_OFFSET` (u16 elem_size; pop n, pop p, push
  `p + n*elem_size`) and `OP_PTR_DIFF` (u16 elem_size; pop b, pop a,
  push `(i64)(a-b)/elem_size`).
- `[ptr-offset $p $n]` — typer preserves the operand's pointee
  on the result; compiler bakes `elem_size` from the struct registry
  (or scalar size) into the operand.
- `[ptr-diff $a $b]` — typer requires same pointee on both sides;
  result narrows to `i64`.
- Equality / ordering: `==`, `<`, `>`, etc. already work on
  pointers via the existing binary-op rules (Stage 5a's typer
  rule rejects mixed-pointee comparison; same-pointee falls
  through to standard tagged-u64 equality). No dedicated
  `[ptr-eq]` / `[ptr-lt]` builtins added — would be redundant.
- Tests: 2 new typer-only (`test_ptr_offset_preserves_pointee`,
  `test_ptr_diff_returns_i64`); 2 new type-error
  (`ptr_diff_different_pointees`, `ptr_offset_non_numeric_offset`);
  2 new embed integration tests walking a real C array
  (`test_ptr_offset_walk_array`, `test_ptr_diff_array_elements`).

**Exit criteria (Stage 5 overall) — MET:** the embed test
`test_ptr_offset_walk_array` does exactly this — takes a raw u64
address from a registered native fn, casts to `[Ptr CPoint]`,
walks an array via `[ptr-offset]`, reads `$p->x` from each element,
and sums them. The typer rejects pointee mismatches, pointer
arithmetic via `+`, and non-numeric offsets at compile time.

### Stage 5 — known gaps & next directions

The core debugger-scripting workflow is complete and tested. These
items are not blockers but are worth tracking:

**Polish / ergonomics:**

- **`print` formatting for `[Ptr T]` values.** Today a typed pointer
  prints as a raw u64 (e.g., `4096` or however `print` formats u64
  values), losing the pointee identity that the typer tracks. A
  Stage-5-aware print would format as `Ptr<Point>(0x1000)` or
  similar. Requires the print path to consult `inferred_type` /
  `inferred_struct_idx` on the AST node, or carry a runtime tag
  for pointers — neither exists today.
- **Null pointer literal.** No way to express `null` / a null
  `[Ptr T]` in JACL source today. `[ptr-cast [Ptr T] 0]` works as
  a workaround. A `[null [Ptr T]]` builtin or `null` keyword would
  be cleaner; runtime traps already fire on null deref via
  `OP_PTR_LOAD` and friends.
- **Better error wording.** Several typer messages embed bespoke
  `snprintf` patterns rather than going through `src/type_error.c`.
  A small extraction pass would put pointer messages in the same
  shared formatter file as the rest of Stage 1e/1f.

**Language design (deferred decisions):**

- **`*mut T` distinction.** Per Stage 5's design decisions, mutability
  was deliberately not split into `[Ptr T]` vs `[Ptr mut T]` for the
  first cut. Adding it later is mostly typer work — declare a binding
  as `[Ptr T]` (read-only), and reject writes (`set $p->x val` and
  `[ptr-store ...]`). No runtime change needed.
- **Bounds-checked array variants.** `[ptr-offset]` accepts any
  signed offset and trusts the user. A `[ptr-array T n]` type that
  carries a length and rejects out-of-bounds offsets at compile time
  would be a useful safer surface for the debugger-scripting use
  case (where reading past an array bound is a common bug).
- **`memcpy` / bulk read.** No way to read a sized slice (`[u8]`,
  raw bytes) through a pointer for printing buffers or hex dumps.
  Could be added as `[ptr-read-bytes $p $n]` returning a `vec` of
  u8 — useful for debugger inspection.

**Runtime / FFI integration (out of scope for the typer):**

- **Layout validation.** The user must hand-mirror the C struct
  layout (field types, order) in their JACL `struct` declaration.
  Mismatches cause silent misreads. A future `extern struct CPoint
  {...}` form could pull layout from a header / DWARF, but that's
  a separate FFI tooling project, not typer work.
- **Out-of-process inspection.** Stage 5 chose in-process semantics
  per design decision. An out-of-process variant (ptrace /
  `process_vm_readv`) would replace the runtime read-byte primitive
  in `OP_PTR_LOAD*` with a function pointer; the typer / compiler
  would not change. Worth a follow-on stage when the workload
  appears.

**Stage 5 long-tail items deferred until a real debugger workload
drives them.** Picking any one is unblocked but speculative without
a concrete script that needs it.

---

## Open decisions (resolve before Stage 0)

These shape the rest of the work. Each needs an answer before the
test infrastructure can be designed.

### 1. How strict is the type checker? — RESOLVED

**Decision:** gradual sound typing.

- **All operands `dyn`** → permissive. The operation compiles; the
  runtime decides. Result is `dyn`.
- **Any operand non-`dyn`** → sound. As soon as one operand has a
  known type, the operation is type-checked. Mismatched concrete
  types are compile-time errors.
- **Barrier crossing** (decision 2) is itself part of the sound
  rules: a `dyn` value can only enter a typed context, or vice
  versa, via explicit `[to T $val]`.

**Consequences:**

| Expression | Status |
|------------|--------|
| `[+ $dyn $dyn]` | compiles; result `dyn` |
| `[+ $i32 $i32]` | compiles; result `i32` |
| `[+ $i32 $f32]` | compile error (typed mismatch) |
| `[+ $i32 $dyn]` | compile error (mixed; cast required) |
| `[+ $i32 [to i32 $dyn]]` | compiles; result `i32` |

**Migration cost:** existing programs that rely on implicit
`dyn ↔ typed` flow at operators or function boundaries will need
explicit casts. Stage 0's audit mode surfaces every instance
before the architectural change ships.

**Why this and not "permissive default + opt-in strict":** the
quarantine model is what makes the typer's job tractable. Once a
value is typed, every operation it touches has known typed semantics
— no exception cases for "this typed value happens to be flowing
through dyn code." The typer's rules become a small, total set
rather than a permissive layer with carve-outs.

### 2. Implicit `dyn` coercion: where? — RESOLVED

**Decision:** no implicit coercion *at commitment sites*. Expression-
level mixing flows through as `dyn`; commits get checked.

A "commitment site" is a place a value is stored into (or returned
from) a slot whose type is declared. Crossing the dyn boundary at
those sites requires `[to T $val]`:

- `def i32 x $dyn_val` → compile error. Use
  `def i32 x [to i32 $dyn_val]`.
- `mut i32 x $dyn_val` / `set typed_var $dyn_val` → same shape.
- `[typed_proc $dyn_arg]` (param is a commitment slot) → compile
  error. Use `[typed_proc [to T $dyn_arg]]`.
- `[. $struct typed_field $dyn_val]` (struct/ctx field-set) →
  compile error.
- `proc i32 f {} { $dyn_val }` (declared return is a commitment to
  the declared type) → compile error. Use `[to i32 $dyn_val]` at
  the tail.
- `dyn → dyn` (typed value flowing to a dyn slot) → permitted; the
  typed binding form itself is the explicit "I want this dyn"
  marker, so no cast needed (e.g. `def dyn x $i32_val` is fine).

Expression-level mixing — binary ops, calls returning into a `def
dyn`, vec-push into untyped vec, etc. — flows through implicitly
and the result types as `dyn`. Examples:

- `[+ $i64 $dyn]` → result types `dyn`. Runtime traps if the
  dyn's tag isn't a numeric compatible with i64.
- `def dyn r [+ $i64 $dyn_x]` → ok (dyn slot accepts dyn result).
- `def i64 r [+ $i64 $dyn_x]` → compile error at the `def`,
  because the `[+ ...]` result is dyn and the binding is i64.

**Why this and not strict-everywhere:** mixing `dyn` and typed
inside an expression doesn't introduce new failure modes — the
runtime check happens at the operator either way. The user-
facing benefit of forcing a cast at every operator site is small
(documentation), and the migration cost is large (every read
from an untyped source feeding typed arithmetic grows a `[to T
…]`). Pushing the boundary check to commitment sites preserves
soundness where it matters (a typed slot only ever holds a
checked value) without paying the syntactic cost at every
internal arithmetic step.

This is closer in spirit to TypeScript's `noImplicitAny` (typed
→ any flows implicitly; any → typed requires cast) than to Dart's
fully-sound mode. The `[to T …]` cast at commitment sites is the
single, well-named place a tag check happens — easier to reason
about than runtime traps inside operators.

### 3. `inline_repr` — type or codegen detail? — RESOLVED

**Decision:** `inline_repr` stays in the compiler as codegen state.
Pointer types are a separate, planned type-system feature owned by
the typer (see Stage 5 below).

**The two distinctions, separated:**

- **`inline_repr` (codegen):** `INLINE_NONE` / `INLINE_STACK` /
  `INLINE_REF` flags on `Compiler`. These describe how struct bytes
  are *currently* arranged on the value stack mid-expression.
  `INLINE_REF` in particular is a stack offset used during chained
  field access (`[. [. $a b] c]`) — purely transient, never persists
  past a single expression, invisible to the user. Not a type-level
  concept. Stays in the compiler.

- **Pointer types (`*T`, future):** user-visible type distinction
  for FFI. `*Point` (a C pointer) vs `Point` (a value) are different
  types with different operations: `[ptr-deref $p]`,
  `[ptr-addr-of $val]`, `[ptr-offset $p $n]`. The typer must track
  these so FFI signatures type-check and pointer ops can't
  accidentally apply to values (or vice versa). This is a real
  type-system feature, scheduled as Stage 5.

**Implication for Stages 1-3:** the typer's `JaclType` representation
must be designed *extensibly*. Specifically, the encoding should
allow adding `TYPE_PTR_TO(t)` (or however we encode it) in Stage 5
without restructuring `inferred_type` storage. A simple way: reserve
high bits in the type field for pointer-depth, or use a small union.
Decide the exact encoding when we get to Stage 1's data-structure
design pass.

**Sub-decision deferred:** the exact pointer type vocabulary and
operations (`*T` vs `*mut T`, auto-deref or always-explicit, FFI
call syntax). That's Stage 5's design work, not blocking on the
migration.

### 4. Where do type errors get formatted? — RESOLVED

**Decision:** shared message formatters; per-pass reporting
machinery.

**Concretely:**

- Extract type-mismatch message formatters into a shared location
  (likely `jacl_type_error.c` or helpers in `ast.c`). Functions like
  `jacl_format_type_mismatch(lhs_t, rhs_t, context)` and
  `jacl_format_assignment_error(target_type, value_type, name)`
  produce the *content* string.
- Each pass keeps its own reporting machinery: `typer__error(tc,
  line, col, msg)` mirrors `compiler__error(c, line, col, msg)`,
  but both call into the same content formatters.
- The pass that catches the error first reports it; the other
  treats the affected AST node as poisoned (its `inferred_type`
  becomes `TYPE_DYN` so downstream walks don't cascade-error) and
  skips.
- Codegen-only errors (e.g., "cannot expand inline struct here",
  "suspension inside non-suspending callback") stay fully in
  `compiler.c` — they're not type-mismatch errors.

**Why shared:** message drift between passes erodes user-facing UX
quietly; Stage 3 wants to delete the compiler's type-check sites
cleanly, which is easier when they were calls-to-shared rather than
embedded `snprintf`s. Extraction is small and one-time.

### 5. Migration order: top-down or bottom-up? — RESOLVED

**Decision:** bottom-up by AST shape, sub-stratified by complexity
within each shape.

Each commit picks one `AstNodeType` (or a sub-case of one), makes
the typer total for that shape, migrates every `compile_node`
consumer that produces a result of that shape from `c->last_*` to
AST reads, runs tests plus audit mode, and ships.

**Recommended sequence:**

1. Literals (`AST_LIT_INT` / `LIT_FLOAT` / `LIT_STRING`) — already
   done by Phase A; just verify under audit mode.
2. `AST_VAR_REF` — already done; verify.
3. `AST_INTERP_STRING`, `AST_QUOTE`, `AST_SYNTAX_QUOTE`,
   `AST_UNQUOTE`, `AST_UNQUOTE_SPLICING`, `AST_SPREAD`,
   `AST_SHELL_CMD` — small, mostly DYN today.
4. `AST_BLOCK` — already mostly done.
5. `AST_COMMAND`, simple heads — the `fixed_returns` cluster,
   predicates, lengths, strings, syntax-* introspection.
6. `AST_COMMAND`, binary ops — `compile_binary`'s consumers.
7. `AST_COMMAND`, control-flow heads — `if`, `while`, `for`,
   `try`, `match`, `with-ctx`.
8. `AST_COMMAND`, def/mut/set — typed bindings (the cluster of
   six fallback sites in compiler.c).
9. `AST_COMMAND`, proc/struct constructor dispatch — call-return
   tracking, including imported modules and nested procs.
10. `AST_COMMAND`, vec-* / map-* with typed-collection narrowing —
    builds on the work already done this session.
11. `AST_DESTRUCTURE_VEC`, `AST_DESTRUCTURE_NAMED`.
12. `AST_DEFSTRUCT`, `AST_DEFMACRO`, `AST_USE`, `AST_CTX_DECL`,
    `AST_BREAK`, `AST_RETURN`, `AST_CONTINUE`, `AST_ERROR`.

By step 5 we're past anything currently done. The sequence is
checklist-shaped: at any point, the migration's status is "shapes
1-N done; shape N+1 in progress; shapes N+2 onward pending." Easy
to triage, easy to ship in pieces.

---

## Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Silent miscompile from typer/compiler disagreement | high | severe | Audit mode (Stage 0) runs continuously; merges blocked on zero divergences |
| Migration takes longer than 4-6 weeks | medium | medium | Stages are independently shippable; partial progress is still useful |
| Typer rejects programs compiler accepts | medium | medium | Strict mode is opt-in; default is permissive (decision 1) |
| Test corpus too small to catch regressions | high | severe | Stage 0 expands corpus before any architectural change |
| `inline_repr` interactions break | medium | medium | Keep `inline_repr` in compiler (decision 3); typer doesn't touch it |
| Macro-expanded code has surprising shapes | medium | medium | Run audit mode against `syntax.c`'s test corpus specifically |
| One stage gets blocked, can't ship | low | medium | Each stage's exit criteria are mechanical; stuck = bug, not architecture |

---

## Effort estimate

| Stage | Calendar | Effort | Risk |
|-------|----------|--------|------|
| 0 — test infra + decisions | 2-3 days | 2-3 days | low |
| 1 — typer total + authoritative | 2-3 weeks | 2-3 weeks | medium |
| 2 — migrate consumers | 1-2 weeks | 1-2 weeks | medium |
| 3 — delete dead state | 2-3 days | 2-3 days | low |
| 4 — separate TypedAstNode (optional) | 1 week | 1 week | low |
| 5 — pointer types for FFI | 2-3 weeks | 2-3 weeks | medium |
| **Total (1-3)** | **4-6 weeks** | | |
| **Total (1-3 + 5)** | **6-9 weeks** | | |

LOC outcome estimates (post-Stage 3):

- compiler.c: −1500 to −2500 (−12% to −19%)
- typer.c: +800 to +1200 (+70% to +100%)
- Net: −700 to −1300

---

## Non-goals

To keep scope tight, the following are *not* part of this work:

1. **New type-system features beyond pointer types (Stage 5).** No
   type variables, no inheritance/subtyping beyond what exists, no
   type aliases, no refinement types. Stages 1-4 are pure
   architectural migration; Stage 5 adds `*T` for FFI and that's the
   only deliberate language addition.
2. **Performance work.** The typer is an extra pass; if it's too slow
   we deal with that after correctness lands. No premature
   optimization.
3. **Touching `vm.c`, `runtime.c`, `parser.c`, `lexer.c`,
   `syntax.c`.** Type tracking lives in compiler.c and typer.c.
   Stage 5 may need parser/lexer touches for `*T` syntax — that's
   acknowledged scope for that stage only.
4. **Changing the surface language for Stages 1-4.** Per decisions 1
   and 2, existing programs that mix `dyn` and typed values
   implicitly *will* need explicit casts to compile. Stage 0's audit
   surfaces every instance. That's acknowledged migration cost,
   not language redesign.
5. **Adding a separate type-check command.** Type errors keep firing
   during normal compile; we don't add `jacl typecheck` as a
   standalone tool.

---

## Decision summary

All five Stage 0 decisions resolved (see "Open decisions" above):

| # | Decision | Resolution |
|---|----------|------------|
| 1 | Strictness | Gradual sound: all-`dyn` permissive, any-non-`dyn` sound, barrier explicit |
| 2 | Implicit `dyn` coercion | None at commitment sites (def/mut/set/param/return/field-set); expression-level mixing flows as `dyn` |
| 3 | `inline_repr` | Stays in compiler; pointer types are Stage 5 |
| 4 | Error formatting | Shared content formatters; per-pass reporting |
| 5 | Migration order | Bottom-up by AST shape, sub-stratified by complexity |

## Status summary

| Stage | Status |
|---|---|
| 0 — test infra + decisions | ✅ complete |
| 1 — typer total + authoritative | ✅ substantially complete (1a–1d done modulo structural items below) |
| 1e — typer emits errors | ✅ complete (8/8 sites — all type-mismatch errors fire from the typer) |
| 1f — dyn as a real type | ✅ complete (4 rules landed; expression-level mixing resolved as permissive) |
| 2 — migrate compiler consumers | ✅ complete |
| 3 — delete dead state | ✅ complete |
| 4 — separate `TypedAstNode` | ⏭ skipped (optional, not pursued) |
| 5a — pointer type + cast (foundation) | ✅ complete (`TYPE_PTR` + `[Ptr T]` annotation + `[ptr-cast]` / `[ptr-addr]` + typer misuse rules) |
| 5a — typed externs | ✅ complete (`extern [type] name {params}` + compound return types for proc/extern) |
| 5b — deref + field auto-deref | ✅ complete (`$p->x`, `[ptr-deref]`) |
| 5b ext — nested struct fields through ptr | ✅ complete (`$p->inner->x`, `[addr ...]`, chain accumulator) |
| 5c — pointer arithmetic | ✅ complete (`[ptr-offset]`, `[ptr-diff]` + opcodes; eq/lt reuse standard binary-ops) |

Tests: 95/95 corpus + 35/35 typer-only + 16/16 type-error. The audit
machinery from Stages 0–2 was deleted in commit `0aa722f` after it
served its purpose.

Cross-module proc typing (destructuring form) landed this session:
two new module fixtures (`typed_import_narrows`,
`err_typed_import_into_typed`). Module-binding form
(`use "path" name` → `[$name->fn ...]`) and imported struct fields
remain deferred (see "Imports design" section).

## Stage 1 long-tail — what's left

Stage 1's leaf rules are largely complete. What remains is
structural rather than per-builtin:

- **Match arm-walk** (Stage 1a): blocked on the language. `match`
  is lexed and parsed (TOKEN_MATCH, HEAD_MATCH) but has no compiler
  / runtime implementation — `match $x { ... }` falls through to
  "undefined variable '$match'". Adding typer rules would be dead
  code until the feature itself is implemented. Deferred until
  match becomes a working language construct.
- **Imported procs** (Stage 1b) ✅ COMPLETE for the destructuring
  form (`use "path" {names}`). Pre-pass in
  `compiler__collect_typer_imports` triggers dependency compilation
  before the outer typer runs (so `dep_mod->exports` is populated)
  and packs each imported proc's signature into a flat
  `TyperImportProc[]` that's handed to `typer_infer`. The typer
  registers each entry in its proc table during the pre-pass, so
  cross-module calls narrow to the declared return type and
  arg-type checks fire from the typer like local-proc calls.
  Struct constructors (filtered out — covered by the existing
  CapitalCase placeholder pre-pass) and non-callable values
  (`def`/`mut` imports, `arity == -1`) are skipped. The
  module-binding form (`use "path" name` → `$mod->fn` calls) is
  deferred — calls through dot-receivers stay dyn.
- **Nested proc registration** (Stage 1b) ✅ COMPLETE.
  `typer__register_procs` now recurses into proc bodies, picking
  up inner `proc` declarations. A nested proc called from a sibling
  position narrows to its declared return type instead of dyn.
  Externs have no body so the recursion skips them. The typer's
  proc registry stays flat, matching runtime lexical resolution
  semantics (a nested proc with a name colliding with a top-level
  one shadows it).
- **Closure literals as call results** (Stage 1b): anonymous procs
  get TYPE_CLOSURE via the same path as named procs (no remaining
  gap in typing the closure value itself), but the typer doesn't
  know an anonymous closure's signature, so calling it falls
  through to DYN.
- **Box-element narrowing for `[deref $box]` / `[swap $box fn]`**
  ✅ COMPLETE for scalar elements. New `TYPE_BOX` JaclType mirrors
  `TYPE_FUTURE` — element idx in `inferred_struct_idx` (scalar
  sentinel for `[box 42]`, would be a struct registry idx for
  `[box [Point 1 2]]`). `[box $val]` types as `TYPE_BOX` with the
  value's element type; `[deref $box]` / `[swap $box $fn]` narrow
  to the scalar element type. Untyped `def b [box ...]` now
  inherits TYPE_BOX from the RHS the same way TYPE_FUTURE /
  TYPE_PTR do.

  **Known gap (deferred):** struct-element narrowing through deref
  was tried but reverted. The typer marking `[deref $box_of_Point]`
  as `TYPE_STRUCT` triggers downstream compiler rules ("proc
  returning a struct must declare return type") that the runtime
  path doesn't actually need — the box's contents are already
  boxed JaclVals at runtime, not inline struct slots. Closing
  this needs a new `OP_BOX_DEREF_INLINE` opcode that materializes
  inline struct bytes from the box's contents (parallel to
  `OP_TYPED_VEC_GET_INLINE`). Tracked as future work; for now
  struct-element boxes still narrow to dyn through deref.

These are tracked as future work but not blockers.

### Imports design — destructuring form landed; module-binding deferred

Cross-module typing for the destructuring form
(`use "path" {names}`) is now implemented. The module-binding form
(`use "path" name` → `[$name->fn args]`) is still deferred — calls
through a dot-receiver stay dyn. Architecture below is preserved
for the deferred work and for future extensions (struct exports,
arity from imports).

**Problem:** the typer runs as a pre-pass on the importer's AST
(compiler.c:12558) BEFORE the compiler walks AST_USE nodes. When
the typer sees `[$mod->fn args]` or a named-import call, it can't
resolve the imported proc's signature because the imported module
hasn't been compiled yet — `dep_mod->exports` doesn't exist at
typer time. So all cross-module calls fall through to dyn.

The compiler does the work eventually: `compiler__compile_module`
(compiler.c:12848) recursively compiles imported modules with their
own typer pass, populating `Module.exports[]` with full signatures
(name + return_type + param_types + arity). That work is just
ordered after the outer typer.

**Two viable architectures:**

1. **Pre-compile imports before outer typer** (preferred,
   architecturally clean):
   - In `compiler_compile`, scan `parse.nodes` for AST_USE before
     line 12558.
   - For each, call `compiler__compile_module` (existing function)
     to load the dependency into the cache. Already handles cycles
     via `import_stack`, so re-using its machinery is safe.
   - Pass the populated `module_cache` and `module_prefix` to
     `typer_infer` as additional context.
   - Inside the typer, on each AST_USE, look up the canonical path
     in the cache and read `dep_mod->exports[]` — every entry
     becomes a TyperProc registration, optionally namespaced by
     the import binding.

2. **Typer self-walk imports** (pragmatic, duplicates work):
   - Give the typer access to the arena, `module__read_file`, and
     `module__resolve_path`.
   - For each AST_USE, lex + parse + recursively call `typer_infer`
     on the imported source.
   - Capture the inner typer's proc registry; copy entries into
     the outer typer's tc->procs prefixed by the binding name.
   - The compiler later re-types the same module via its own walk;
     extra cost is linear in module count.

**Resolution form mapping:**

| Source form | Effect on outer typer registry |
|---|---|
| `use "mod" name { name1 name2 }` | Register `name1` and `name2` as plain typer procs (typer doesn't track namespacing currently) |
| `use "mod" name` (module-binding form) | Register exports under `name->proc` lookup — needs new typer machinery to resolve `[$name->proc args]` calls through a dot-receiver to the proc registry |
| `use "mod" {name1 name2}` (no binding) | Register `name1`, `name2` as plain typer procs |

**Edge cases:**

- **Cycles:** option 1 reuses `import_stack` directly. Option 2
  needs its own cycle tracker.
- **Compile errors in dependency:** option 1 surfaces them via the
  existing compiler__error path. Option 2 must thread errors
  through TyperResult.
- **Struct exports:** modules can export struct types
  (`module->StructName` is reusable for instantiation). Imported
  struct registration would need to merge into the typer's
  `tc->structs[]`. Currently the compiler shares the struct
  registry across imports (compiler.c:12903-12907) — option 1 can
  piggyback on that; option 2 would need to copy.
- **`prelude_is_native_fn` flag:** native fns flow through prelude
  injection, separate from imports. Not affected.

**Recommendation:** option 1. The pre-scan is ~30 lines in
`compiler_compile`. The typer changes are ~80 lines (signature
update, AST_USE handler, dot-call resolution). Tests: 2-3
typer-only (basic imported call narrows; wrong-arg-type errors;
missing-export error). Total scope: half a day in a focused
session.

**What landed (destructuring form):** the implementation followed
option 1's shape. A new helper `compiler__collect_typer_imports`
walks `parse.nodes` for AST_USE before the outer `typer_infer` call,
triggers `compiler__compile_module` for each uncached dependency,
then reads `dep_mod->exports[]` and packs callable procs into a
`TyperImportProc[]` array (filtered to skip structs and non-callable
values). `typer_infer`'s signature was extended to take
`(imports, import_count)`; the typer's pre-pass registers each
entry in `tc.procs` so calls dispatch through the same
`typer__find_proc` path as local procs. Two new fixture tests
(`modules/typed_import_narrows/`, `modules/err_typed_import_into_typed/`)
plus the pre-existing `modules/err_typed/` cover the narrowing,
wrong-return-into-typed-binding, and arg-mismatch cases.

**Open follow-ons** (deferred from this batch):

1. **Module-binding form** (`use "path" name` → `[$name->fn args]`).
   Requires a new typer registry indexed by binding name → namespaced
   procs, plus a dot-receiver resolution rule in the call dispatch.
   Calls through `$mod->fn` currently fall through to dyn.
2. **Struct exports.** Imported struct types still go through the
   CapitalCase placeholder pre-pass with empty fields. Field access
   on imported structs stays dyn at the typer level (the compiler's
   shared registry has the real fields, so codegen still works).
   Closing this needs the typer's `tc.structs[]` to merge real field
   info from imports — would need typer↔compiler struct-idx alignment
   across modules.
3. **Imported `def`/`mut` values.** Skipped (`arity == -1`). To type
   them, the typer's binding registry would need to register
   imported names as TyperBindings with their declared type. Cheap
   to add but no current workload demands it.

## Pickup points

In rough order from "smallest, lowest-risk increment" to "freshest
architectural piece". Each is independently shippable.

### Sharp edge: `await` on a non-future operand — RESOLVED

✅ Resolved via option (2). The m13 `await 42` placeholders were
rewritten to `await [spawn { 42 }]` (the spawn body still resolves
to the same scalar; the surrounding suspension/structural-error
diagnostics still fire). The typer's HEAD_AWAIT branch now errors
when the operand has a known concrete non-future type:

> `type error: await expects a future, got <T>`

Dyn operands remain permitted — the runtime tag check still catches
non-future values dynamically. The runtime-error test
(`test_await_non_future_error` in `test/test_m13.c`) was rewritten
to use a `def dyn x 42` binding so its operand types as `dyn`,
preserving runtime coverage of the non-future trap. Two new cases
in `test/test_type_errors.c` lock in the compile-time wording
(`await_int_literal`, `await_typed_int_binding`). New shared
formatter `jacl_format_await_non_future` lives in `src/type_error.c`.

### Stage 5 — pointer types for FFI

A fresh architectural piece, separate from the migration work. The
existing Stage 5 task list (above, in the "Stages" section) is the
starting point. Open sub-decisions: `*T` only or also `*mut T`;
auto-deref vs. always explicit; FFI call syntax. Resolve those
before starting work.

### Stage 1 long-tail structural items

Three of the original five long-tail items have landed:
nested-proc registration ✅, box-element narrowing ✅ (scalar-only),
and the await sharp edge ✅. The remaining items:

- **Imported procs**: design captured above; pick up when
  cross-module typing matters for a workload.
- **Closure literal call signatures**: trickiest of the long-tail.
  Anonymous closures get TYPE_CLOSURE today, but the typer doesn't
  carry their signature, so calls to a captured anon closure stay
  dyn. Closing this needs the typer to record signatures on
  binding metadata when `def x { proc {x} { ... } }` and propagate
  through var-refs.
- **Match arm-walk**: blocked on the match feature itself
  existing in the compiler/runtime. See entry above.

## Test infrastructure

- **`test/test_typer.c`** — 35 typer-only cases asserting on
  `inferred_type` directly. Doesn't go through codegen, so silent
  typer bugs are visible here.
- **`test/test_type_errors.c`** — 16 cases locking in compile-time
  error messages via substring matching. Tolerates message
  rewordings within reason; lock in the wording you want by
  asserting on the load-bearing tokens (type names, "type error",
  proc/struct names) rather than the full sentence.
- **`src/type_error.c`** — shared formatters. Add new ones here
  whenever a previously bespoke `snprintf` site moves to the typer.

Run the suite with `bash build.sh`. Type-error binary:
`/jacl_impl/.build/type_errors`. Typer-only binary:
`/jacl_impl/.build/typer`.

## Pickup-ready summary (last session)

Stage 1 is essentially closed. The typer now handles all
load-bearing rules in the current corpus. The next session can
choose between:

1. **Stage 5 — pointer types `*T` for FFI.** Fresh architectural
   piece. Sub-decisions to resolve: `*T` only vs. also `*mut T`,
   auto-deref vs. always explicit, FFI call syntax.
2. **Tackle a structural Stage 1 item** when a real workload
   needs it (match, imports, nested procs, closure-call typing,
   box-element narrowing).

(The await sharp edge was resolved — see the section above.)

Concrete state of the codebase post-session:

- `TYPE_FUTURE` is a real type with element-type tracking via
  the existing `JACL_SCALAR_TYPE_IDX` sentinel encoding (mirrors
  `TYPE_TYPED_VEC`/`TYPE_TYPED_MAP`). spawn produces typed
  futures from body-tail types; await unwraps to the element
  type; race narrows to the unified body tail when homogeneous.
- All five commitment-site rules from decision 2 are enforced
  (def/mut/set/proc-param/proc-return/struct-or-ctx field-set).
  Expression-level dyn mixing is permissive — `[+ $i32 $dyn]`
  types as dyn and the barrier check fires at the next commit.
- Compiler accepts `[Future T]` / `[Vec T]` / `[Map T]` /
  `[Map K V]` as compound type annotations in def position
  (routed through the untyped-def path; the typer narrows the
  static type from the annotation).
- Receiver-preserving builtins: filter, transform, take, vec-push,
  vec-set, vec-concat, vec-slice, map-set, map-remove, map-keys,
  map-vals all preserve typed-collection element types where
  applicable.
- vec-get, map-get, first narrow to typed-vec/typed-map element
  types (struct or scalar via the shared sentinel).
