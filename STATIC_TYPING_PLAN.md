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
4. **Stage 1c — typed-collection element narrowing.** ❌ NOT STARTED.
   `[vec-get $typed_vec idx]` and `[map-get $typed_map key]` still
   return DYN in the typer.
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

**Tasks:**

1. Delete `c->last_expr_type`, `c->last_struct_idx`,
   `c->last_key_struct_idx` field declarations and every write.
2. Delete `c->expected_type` if its only consumers were literal
   narrowing (which the typer now handles by stamping `inferred_type`
   on the literal directly).
3. Delete `compiler__effective_type` (the helper added in this
   session); call sites become direct AST reads.
4. Audit `c->inline_repr` — much of it is consumed by codegen for
   actual representation choices, not type queries. Keep the codegen
   uses; remove anything that was a type-tracking shadow.
5. Delete dead helpers (e.g., `compiler__set_type` if it was only
   feeding `last_*` fields).
6. Re-flow `compile_binary`, `compile_node`, etc., to remove the
   variables that only existed to thread `last_expr_type` through.

**Exit criteria:**

- `Compiler` struct no longer has type-tracking fields beyond what
  codegen genuinely needs (e.g., `locals[i].type` for codegen
  decisions on inline structs).
- compiler.c LOC drops by 200-400 from this stage alone.

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

- **Stage 2 minimal edit (1 line, ~30 audit divergences cleared):**
  delete the `c->last_expr_type` fallback from
  `compiler__effective_type` in compiler.c. Run tests. Then audit
  mode shows which other clusters need follow-up.
- **Finish Stage 1c** (typed-collection elem narrowing for
  `vec-get` / `map-get`) — small, targeted, ~13 audit GAPs gone.
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

**Real divergence (GAP+MISMATCH):** 665 → 46. 93% reduction.

**Stage 1 plateau.** At 71 total / 46 real divergences, the remaining
clusters are:

- **5 MISMATCHes (`head=def` typer=nil compiler=struct):** load-
  bearing leak — `def Point p [Ctor ...]` paths read the leaked
  struct type to drive inline-vs-heap codegen. Pinning breaks
  struct-flow tests. Stage 2 absorbs naturally when the consumer
  reads `args[1]->inferred_type` instead of `c->last_expr_type`.
- **5 EXTRAs each (AST_VAR_REF, AST_RETURN, head=`==`):** typer
  is more correct than compiler tracks. Stage 2 absorbs.
- **3 EXTRAs each (head=`while`, head=`for`):** compiler-side
  pin opportunities exist (not yet attempted; risk profile similar
  to the `def` MISMATCHes — would need to verify no downstream
  reader relies on the leak).
- **7 AST_LIT_INT GAPs + 2 head=def GAPs:** at top-level positions
  the typer's handle_def_or_mut bails for some shape we haven't
  pinned down; needs targeted reproducer + handler extension.
- **3 head=`|` GAPs:** pipe rhs is a function call to a proc whose
  return type the typer doesn't know (typer-only proc registry
  doesn't include closures or imported procs).
- **2 each across reset/yield/mut/etc.:** small clusters; each is
  a focused 1-2 line typer rule.

**Recommendation:** at this point, further Stage 1 investment
returns less per commit than starting Stage 2 (which absorbs ~30
of the remaining 71 just by migrating consumers off
`c->last_expr_type`). The typer is now correct for ~95% of AST
shapes encountered in the test_compiler corpus.

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
