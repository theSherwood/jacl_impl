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

**What's *not* here yet** (deliberately skipped):

- Stage 4 (separate `TypedAstNode` from `AstNode`) — optional;
  weighed against benefit. Not pursued.
- Stage 1e (typer emits type errors directly) — typer flags
  divergences in `inferred_type=DYN` but doesn't report errors.
  The compiler still owns error reporting (using the shared
  formatters in `src/type_error.c`).
- Stage 1f (`dyn` as a real type with defined-semantics ops) —
  the architecture supports it but the rules aren't written.
- Stage 5 (pointer types `*T` for FFI) — design pending.

---

## End state

After all stages land, the architecture looks like:

```
parser    →  AST                    (untyped, scope_marks stamped)
typer     →  AST + type annotations (every node has a non-sentinel type)
compiler  →  bytecode               (reads only from AST; emits no type questions)
```

**Compiler invariants after migration:**

- The compiler never *infers* a type. It only *consumes* annotations.
- Functions that need a type read it from the AST node, not from
  `Compiler` state.
- Type errors are detected and reported during typing, not codegen.
- `c->expected_type` is gone — the typer pushes context down during
  the walk and stamps each node with its expected/effective type.

**Typer invariants after migration:**

- Every `AstNode` reachable by the walk has `inferred_type` set to a
  real `JaclType`. `TYPE_DYN` *is* a real type ("dynamically-typed
  value"), not "I don't know."
- Where the typer can't decide, it reports a type error rather than
  silently leaving DYN.
- Operations on `dyn` operands have known typed semantics (e.g.,
  `[+ $a $b]` where both are `dyn` types as `dyn` and selects the
  generic add opcode).

**The `dyn` type, properly:**

| Today                                           | After migration                        |
|------------------------------------------------|----------------------------------------|
| `TYPE_DYN` = "typer gap; ask `c->last_expr_type`" | `TYPE_DYN` = "value of unknown static type; runtime-tagged"     |
| Implicit fallback at 22 sites                   | No fallback; `dyn` flows through ops with defined typed semantics |
| Untyped def/mut → DYN binding                  | Same, but DYN binding is a known thing, not a hole |
| Mixed-type if-branches → DYN                    | Same, but DYN is the unified result, not a punt |

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
2. **Stage 1a — control-flow result types.** ✅ PARTIAL.
   Done: `while`, `for`, `yield`, `break`, `continue`, `dot`
   field-set. Not done: `try`, `match`, `with-ctx` (each typer
   says DYN today).
3. **Stage 1b — call returns.** ✅ PARTIAL.
   Done: ~30 entries in `fixed_returns` (predicates, lengths,
   strings, streams, dyn collection ctors, signal/cancel, syntax-*
   introspection, while/for/yield, signal/cancel). HEAD_PIPE result
   inherits rhs. Not done: `await`/`parallel`/`race`/`spawn` (typer
   gives DYN), `vec-get`/`map-get` element types, `take`/`first`/
   `reset`/`swap`/`hash`/`interpret`/FFI cluster. Plus closures and
   imported procs are absent from the typer's proc registry.
4. **Stage 1c — typed-collection element narrowing.** ✅ PARTIAL.
   Done (this session): typed-collection constructor `[[Vec T] ...]` /
   `[[Map K V] ...]` now types as TYPE_TYPED_VEC/MAP with element
   `inferred_struct_idx` populated for struct elements. `[vec-get
   $typed_vec idx]` / `[map-get $typed_map key]` narrow to TYPE_STRUCT
   with the element struct_idx when the receiver carries it. Proc
   params declared `[Vec T]` / `[Map K V]` and bindings declared with
   typed-collection types also propagate the elem struct_idx. Not
   done: scalar-element narrowing (`[Vec i64]` etc.) — still DYN
   because the typer would need the compiler's
   COMPILER_SCALAR_TYPE_IDX sentinel encoding.
5. **Stage 1d — narrowing through every guard.** ✅ PARTIAL.
   Done: `[box? Type $x]`. Not done: typed-collection box guards
   (`box? [Vec T]`), error guards in `try`.
6. **Stage 1e — type errors emitted by the typer.** ❌ NOT STARTED.
   The shared formatters in `src/type_error.c` are ready (Stage 0
   commit `e5a31a8`); only the compiler currently calls them. Stage
   1e would have the typer call the same helpers.
7. **Stage 1f — `dyn` as a real type.** ❌ NOT STARTED.
   The architecture is in place but no rules written yet.
8. **Run the audit-mode build continuously.** ✅ DONE — see
   "Running the audit" subsection below.

**Exit criteria:**

- Audit mode reports zero typer↔compiler disagreements across the
  full test corpus.
- Typer assigns a non-sentinel type to every node in every test.
  (Add an assertion in audit mode.)
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

### Stage 5 — Pointer types for FFI (2-3 weeks)

After the migration architecture is stable, add `*T` to the type
system as a real, user-visible category. This is the FFI enablement
work that motivated the inline-vs-pointer distinction in decision 3.

**Tasks:**

1. **Design the pointer type vocabulary.** `*T` only, or also
   `*mut T`? Should `*T` apply to primitives (`*i32`, `*u8`) or only
   to structs? Auto-deref on field access (`[. $p field]` works on
   both `Point` and `*Point`), or always explicit? Defer the answers
   to a design discussion when we get here.
2. **Extend `JaclType` encoding.** The Stage 1 data-structure work
   already left room (decision 3); fill it in.
3. **Add operations:** `[ptr-deref $p]`, `[ptr-addr-of $val]`, and
   `[ptr-offset $p $n]` at minimum. Plus FFI call signatures using
   `*T` types.
4. **Codegen.** New opcodes for pointer load/store; integration
   with existing inline-struct codegen so a pointer to an inline
   struct field works.
5. **Typer rules.** `*T ↔ T` is a barrier crossing (consistent with
   decision 1's model). Going from `T` to `*T` requires
   `[ptr-addr-of]`; going from `*T` to `T` requires `[ptr-deref]`.
   No implicit coercion.

**Exit criteria:** can write a JACL program that calls a C function
returning `*Point`, deref it, read fields, and the typer rejects
treating the pointer as a value (or vice versa) without explicit
ops.

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

**Decision:** no implicit coercion in either direction.

Crossing the boundary requires `[to T $val]` (or `[to dyn $val]`):

- `def i32 x $dyn_val` → compile error. Use
  `def i32 x [to i32 $dyn_val]`.
- `[typed_proc $dyn_arg]` → compile error. Use
  `[typed_proc [to T $dyn_arg]]`.
- `[dyn_proc $i32_arg]` → compile error. Use
  `[dyn_proc [to dyn $i32_arg]]` (or just don't have a dyn proc
  taking what's clearly a typed value — usually a sign of a
  modeling issue).

This is stricter than TypeScript's `noImplicitAny` (which lets
typed → dyn flow implicitly) but matches Dart sound mode. The
strictness is the point: every barrier crossing is visible in
source, so reasoning about types is local.

**Sub-decision to flag:** does `def dyn x $i32_val` count as the
explicit cast (the `dyn` keyword in the binding is itself the
boundary marker), or is `def dyn x [to dyn $i32_val]` required? My
inclination: the typed binding form (`def dyn x ...`) is itself
explicit enough — it's already saying "I want this slot to be dyn"
— so no additional cast needed. Same for `dyn` proc params and
return types. Loop in if you disagree.

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
| 2 | Implicit `dyn` coercion | None — both directions require explicit `[to T $val]` |
| 3 | `inline_repr` | Stays in compiler; pointer types are Stage 5 |
| 4 | Error formatting | Shared content formatters; per-pass reporting |
| 5 | Migration order | Bottom-up by AST shape, sub-stratified by complexity |

## How to start

**Stage 0 is complete** (commits `86be2bb`, `5b00aa3`, `e30a308`,
`e5a31a8`). The audit, both test harnesses, and the shared
formatters are in place.

**Stage 1 is at plateau** (~12 commits, last is `066e666`). Real
divergences (GAP+MISMATCH) on the test_compiler audit are at 46,
down from 665 — 93% reduction. Remaining work split into
substeps with done/partial/not-started markers above.

**Pick-up entry points** (in increasing depth of work):

- **Stage 2 minimal edit — LANDED.** `compiler__effective_type` now
  reads `n->inferred_type` only; the `c->last_expr_type` fallback
  is gone. All 95/95 tests pass. Audit on test_compiler dropped
  71→66 divergences, but the bigger win is eliminating dual-track
  state for the 22 helper call sites. The Stage 1 closures needed
  to land this:
    1. typed-collection constructor types + elem struct_idx
    2. `vec-get`/`map-get` elem narrowing on typed receivers
    3. `[box? [Vec T]]` / `[box? [Map K V]]` narrowing
    4. `filter` (preserves recv) / `transform` (typed→VEC) rules
    5. Generator detection: yielding proc → TYPE_STREAM
    6. Proc params with compound type heads (`{[Vec T] pts}`)
    7. CapitalCase imported names → placeholder structs (so
       `[Point ...]` from `use {Point}` types as STRUCT)
- **Stage 1e** (type errors emitted by typer) — bigger, but the
  shared formatters are already extracted; mostly call-site moves.
- **Stage 1f** (dyn as a real type) — the bigger architectural
  step; requires writing down all dyn-touching op rules.

Stop at the end of any Stage and the codebase is still better off.

## Running the audit

The audit-mode build prints a `TYPER_AUDIT` line to stderr for
every divergence between `node->inferred_type` and
`c->last_expr_type` after each `compile_node` call.

```sh
# Build with -DJACL_TYPER_AUDIT, run a single test, capture stderr:
bash build.sh --audit --test=compiler 2>err.log

# Triage the bug list:
grep TYPER_AUDIT err.log \
  | grep -v SUMMARY \
  | sed 's/at [0-9]*:[0-9]*//' \
  | sort | uniq -c | sort -rn \
  | head -20

# Just the category counts:
grep TYPER_AUDIT err.log \
  | grep -v SUMMARY \
  | awk '{print $2}' | sort | uniq -c | sort -rn
```

Categories the audit reports:

| Kind | Meaning |
|------|---------|
| `MISMATCH` | both passes non-DYN, types differ — typer or compiler bug |
| `GAP` | typer DYN, compiler non-DYN — typer is missing a rule |
| `EXTRA` | typer non-DYN, compiler DYN — compiler doesn't track this op (Stage 2 absorbs) |
| `STRUCT_IDX` | types match but `inferred_struct_idx` differs — struct propagation bug |

Each compile prints a `TYPER_AUDIT SUMMARY: total=N agree=N
mismatch=N gap=N extra=N struct_idx_diff=N` line at end. Counters
reset between compiles.

The audit only fires when `compile_node` is called — i.e., for
codegen-running tests, not for typer-only tests.

## Stage 0 deliverables (reference)

The four artifacts Stage 0 produced:

- **Audit build** — `bash build.sh --audit` (commit `86be2bb`).
- **Typer test harness** — `test/test_typer.c` (commit `5b00aa3`),
  16 cases asserting on `inferred_type` directly.
- **Type-error corpus** — `test/test_type_errors.c` (commit
  `e30a308`), 11 cases locking in compile-time error messages.
- **Shared formatters** — `src/type_error.c` (commit `e5a31a8`).
  Seven helpers, all callable from typer.c when Stage 1e starts:
  - `jacl_format_assign_mismatch`
  - `jacl_format_assign_dyn_named`
  - `jacl_format_assign_dyn_unnamed`
  - `jacl_format_assign_struct_to_dyn`
  - `jacl_format_field_mismatch`
  - `jacl_format_field_dyn_assign`
  - `jacl_format_proc_return_mismatch`

  Two error variants are intentionally not extracted (struct
  constructor "got dyn" without cast hint, immutable-binding
  mutate); Stage 1e can either extract them or leave them in
  compiler.c as one-offs.

## Progress snapshot

**Stage 0 — complete.** Four commits established the audit and
test infrastructure (`86be2bb`, `5b00aa3`, `e30a308`, `e5a31a8`).

**Stage 1 — in progress.** Audit divergences on the test_compiler
suite track progress:

| Snapshot | Total | GAP | MISMATCH | EXTRA | STRUCT_IDX |
|----------|------:|----:|---------:|------:|-----------:|
| Stage 0 end | 854 | 613 | 52 | 73 | 116 |
| After commit `1f220a4` (def/mut sugar, proc, struct_idx) | 194 | 108 | 21 | 65 | 0 |
| After commit `fb95682` (while/for/dot field-set) | 194 | 70 | 30 | 94 | 0 |
| After commit `7d0ed8b` (compiler box?=BOOL fix) | ~190 | 70 | 22 | 94 | 0 |
| After commit `4447c01` (while/break/continue compiler pins) | ~165 | 70 | 22 | 70 | 0 |
| After commit `f604819` (typer continue/shell rules) | 160 | 66 | 26 | 68 | 0 |
| After commit `05dc74e` (typer ctx-struct pre-pass) | 160 | 66 | 26 | 68 | 0 |
| After commit `e6b0d6b` (set/:: pin nil + make-syntax pin dyn) | 107 | 59 | 15 | 33 | 0 |
| After commit `8b595ea` (HEAD_PIPE result from rhs + atom dyn) | 96 | 48 | 15 | 33 | 0 |
| After commit `a1d713f` (struct== inline → bool) | 91 | 48 | 15 | 28 | 0 |
| After commit `cf5b87e` (HEAD_FOR compiler pin) | 82 | 48 | 9 | 25 | 0 |
| After commit `5d089be` (HEAD_YIELD typer rule) | 82 | 48 | 9 | 25 | 0 |
| After commit `c4b9d65` (per-field struct_idx for chained dot) | 71 | 37 | 9 | 25 | 0 |
| After commit `066e666` (typer 1-arg AST_COMMAND def-sugar generalization) | 71 | 37 | 9 | 25 | 0 |
| **After commit `1501551` (Stage 2 minimal edit: drop helper fallback + 7 typer closures)** | 66 | 34 | 5 | 27 | 0 |
| After commit `981c770` (Stage 2: bin-op + def cluster struct_idx → AST) | 66 | 34 | 5 | 27 | 0 |
| After commit `9eca062` (Stage 2: mut/set/if/destructure clusters) | 66 | 34 | 5 | 27 | 0 |
| After commit `26a558c` (pin def/mut to nil; typer NIL fallback for unhandled set/def shapes) | 61 | 33 | 0 | 28 | 0 |
| After commit `e520bf8` (pin while to nil) | 58 | 33 | 0 | 25 | 0 |
| After commit `df9352a` (typer rule for HEAD_RESET) | 54 | 29 | 0 | 25 | 0 |
| After commit `2fdc066` (pin AST_RETURN to nil; proc body return-type check reads AST tail) | 47 | 27 | 0 | 20 | 0 |
| After commit `a4aef7b` (pin print to nil; typer shell-cmd background→DYN) | 45 | 27 | 0 | 18 | 0 |
| After commit `f81bdeb` (typer rule for unary minus) | 44 | 26 | 0 | 18 | 0 |
| After commit `5d31371` (HEAD_PROC closure fallback) | 44 | 26 | 0 | 18 | 0 |
| After commits `152eabd`, `c3227c5`, `ba1b84e`, `cb97a4f` (Stage 2: print/box/reset/HOF/ctor-elem/vec-concat → AST) | 44 | 26 | 0 | 18 | 0 |
| After commit `9b95e12` (delete unused compiler__get_type) | 44 | 26 | 0 | 18 | 0 |
| After commit `e21b1fe` (typer pipe-binop simulation) | 41 | 23 | 0 | 18 | 0 |
| After commit `7f5acd8` (parser defaults LIT_INT/LIT_FLOAT/LIT_STRING inferred_type) | 35 | 17 | 0 | 18 | 0 |
| After commit `e78f933` (typed-elem-arg helper struct check → AST) | 35 | 17 | 0 | 18 | 0 |
| After commit `698e951` (ctx is a real registered struct; CTX_STRUCT_PENDING deleted) | 35 | 17 | 0 | 18 | 0 |
| After commit `a1dfe64` (AstNode.inferred_key_struct_idx propagated for typed-map) | 35 | 17 | 0 | 18 | 0 |

**Real divergence (GAP+MISMATCH):** 665 → 46. 93% reduction.

**Stage 2 in progress.** Stage 2's "minimal edit" landed at
`1501551` (66 audit divergences). Subsequent commits migrated
`c->last_struct_idx` reads in compile_binary, def, mut, set/reset,
if-branch unification, and destructure paths to read
`args[i]->inferred_struct_idx` instead. Then HEAD_DEF, HEAD_MUT,
HEAD_WHILE got pinned to TYPE_NIL after dispatch; HEAD_RESET got a
typer rule. Tests held at 95/95 throughout.

At 54 audit divergences, the remaining clusters are:

- **6 GAP `AST_LIT_INT`:** scattered top-level int literals where
  the typer didn't reach. Some are `def x = 5` parsed as
  `[= [def x] 5]` (now handled by 066e666), others appear to be
  macro-rewrite artifacts where the post-rewrite tree has new
  AST_LIT_INT nodes the typer never saw.
- **5 EXTRA `AST_VAR_REF` (typer=struct, compiler=dyn):** typer
  knows the binding's struct type but the compiler's local has
  type=DYN. Mostly inline-struct receivers; clears as more
  TypeInfo migrations move from c-state to AST.
- **5 EXTRA `AST_RETURN` (typer=nil, compiler=dyn):** typer pins
  return to NIL; compiler doesn't pin (tried, broke a
  struct-comparison test that relies on a struct-returning proc's
  body leaking the struct type back through last_expr_type — needs
  per-proc analysis to know when the leak is safe to drop).
- **3 GAP `head=|`:** pipe rhs is a function call to a proc
  whose return type the typer doesn't track (closures, imported
  procs aren't in the typer's proc registry).
- **2 each reset/mut/def/::/.: macro-rewrite artifacts** — the
  compiler's `:: → set` / sugar-form rewrites produce new
  AST nodes after the typer has already run. Out of scope for
  Stage 2 (would need typer to also run on rewritten trees, or
  rewrite to copy inferred_type).

The ctx-struct pre-pass commit is setup for follow-on: future code
referencing `$ctx.field` inside a `with-ctx` block will type the
field correctly. The remaining 11 `head=.` GAPs in the audit turned
out to be field access on intra-proc struct locals where the
typer's struct registry indexing diverges from the compiler's at
some specific shape. Each remaining GAP cluster from this point is
its own focused investigation.

**Remaining clusters as of `a1d713f`:**

| Cluster | Count | Shape |
|---------|------:|-------|
| GAP `head=.` | 0 | RESOLVED in commit `c4b9d65` — chained dot now propagates struct_idx |
| GAP `AST_LIT_INT` | 7 | NOT macro-expansion: parser produces `[= [def x] 5]` for the surface `def x = 5`, and the typer's def-sugar handler bails because args[0] is a 1-arg AST_COMMAND with non-keyword head 'def'. Fix: extend handle_def_or_mut to unwrap that shape (similar to compiler__rewrite_binding_op's destructure-pattern branch). |
| MISMATCH `head=for` | 6 | for body's last expr leaks (load-bearing in some path; can't pin) |
| MISMATCH `head=def` | 5 | def-with-struct leaks for inline-vs-heap codegen (load-bearing; can't pin) |
| EXTRA `AST_VAR_REF` (struct/dyn) | 5 | typer knows struct binding type, compiler tracks dyn |
| EXTRA `AST_RETURN` | 5 | typer says nil; compiler default dyn (pin breaks struct return) |
| GAP `head=\|` (i32) | 3 | typer rhs returns DYN because proc's return type isn't tracked |
| GAP `head=yield/reset/mut/def` | 2-2 each | scattered; need targeted typer rules |
| ... long tail | ~30 | individual cases |

The 13 commits across Stages 0+1 in this session are recorded above.
At this rate of progress per commit, reaching zero divergences would
take approximately another 6-10 sessions of similar work.

**What Stage 2 would absorb:** EXTRAs (28) plus the load-bearing
MISMATCHes (~11 from def/for/return). That's ~39 of the remaining
91 — clears once `c->last_expr_type` consumers migrate. So Stage 1
reaches its exit criterion ("zero divergences") through a
combination of Stage 1 typer rules AND Stage 2 consumer migration,
not Stage 1 alone.

**Remaining work to drive Stage 1 to zero:**

- ~66 GAPs spread across `HEAD_DOT` (struct field access where typer
  doesn't have receiver's struct_idx), macro-expanded `AST_LIT_INT`
  (typer doesn't run on expanded subtrees), `HEAD_PIPE`,
  `HEAD_MAKE_SYNTAX` (varies by kind), `HEAD_IF` with mismatched
  branch types, etc.
- ~26 MISMATCHes from compiler-side leaks at HEAD_SET, HEAD_FOR,
  HEAD_DEF (struct binding paths). Tried pinning these in commit
  `4447c01` but reverted because struct-flow tests rely on the
  leaked types for inline-vs-heap codegen decisions.
- ~68 EXTRAs are "typer is more correct than compiler tracks" —
  they all clear when Stage 2 drops `c->last_expr_type` reads.
  Not blocking.

**Audit replay:** `bash build.sh --audit --test=compiler 2>err.log`,
then `grep TYPER_AUDIT err.log | sed 's/at [0-9]*:[0-9]*//' |
sort | uniq -c | sort -rn | head` for the bug list.
