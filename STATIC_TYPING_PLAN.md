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

**Tasks:**

1. **Add the missing AST annotation slots.**
   - `inferred_key_struct_idx` (for typed-map keys; mirrors
     `c->last_key_struct_idx`).
   - `inline_repr` if needed — investigate whether this can be derived
     from `inferred_type` + `inferred_struct_idx` alone, or whether it
     needs its own field.
2. **Stage 1a — control-flow result types.** `if`, `while`, `for`,
   `try`, `match`, `with-ctx`, `break`-with-value, `return`. Today
   most of these are TYPE_DYN. Decide unification rules (e.g., `if`
   with mismatched branches → `dyn`; `while`/`for` → `nil` unless
   `break` carries a value; `try` → unify body and handler).
3. **Stage 1b — call returns.** Cover every well-known builtin
   (currently 29 in `fixed_returns`; needs ~50 more — `await`,
   `parallel`, `race`, `spawn`, `yield`, `vec-get`, `map-get`, `take`,
   `first`, `deref`, `error`, `error-val`, `stream-next`, `hash`,
   `index`, `interpret`, `make-syntax`, `exec`, `reset`, `swap`, plus
   the FFI/trampoline cluster). Plus generic proc-return tracking
   that follows imported modules and nested procs.
4. **Stage 1c — typed-collection element narrowing.** `[vec-get
   $typed_vec idx]` returns the element type, not DYN. Same for
   `map-get`. Requires the typer to track elem type idx through call
   chains.
5. **Stage 1d — narrowing through every guard.** Today only
   `[box? Type $x]` flows narrowings. Add the guard forms the
   compiler currently special-cases (typed-collection box guards,
   error guards in `try`).
6. **Stage 1e — type errors emitted by the typer.** Move type-check
   logic from `compiler.c` into the typer for: typed def/mut/set
   bindings, struct field access, ctx field set, struct constructor
   args, proc args, typed-collection element matches. The compiler
   keeps emitting them as a backstop until Stage 2 lands.
7. **Stage 1f — `dyn` as a real type.** Define typed semantics for
   every operation when one or both operands are `dyn`:
   - `[+ $dyn $dyn]` → `dyn` via `OP_ADD_GENERIC`
   - `[+ $i32 $dyn]` → `i32` via narrow-then-add (typer assertion)
   - Etc. Some of these are already in `compiler.c`; just write down
     the rules and have the typer track them.
8. **Run the audit-mode build continuously.** Every divergence
   surfaced in Stage 0 must reduce to zero.

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

1. Identify all reads of `c->last_expr_type` / `c->last_struct_idx` /
   `c->last_key_struct_idx` in the cluster.
2. Replace each with `args[i]->inferred_type` /
   `args[i]->inferred_struct_idx` / `args[i]->inferred_key_struct_idx`.
3. Remove the corresponding writes if they exist only to feed this
   cluster.
4. Run the full test suite.
5. Run audit mode; remaining writes are still consumed somewhere.
6. Commit.

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

---

## Open decisions (resolve before Stage 0)

These shape the rest of the work. Each needs an answer before the
test infrastructure can be designed.

### 1. How strict is the type checker?

Two modes, or pick one:

- **Permissive:** any program the compiler currently accepts must
  still compile. The typer infers types, never rejects. Type errors
  only at boundaries the compiler already checks today (typed
  bindings, struct fields, etc.).
- **Sound:** the typer can reject programs that "happen to work" at
  runtime if their static types don't match. E.g., `[+ "hello" 5]`
  would become a compile-time error even if the runtime would have
  errored too.

**Recommendation:** start permissive. Soundness is a follow-on once
the architecture is in place.

### 2. Implicit `dyn` coercion: where?

When can a typed value be used where `dyn` is expected, and vice
versa? Three options:

- **Both directions implicit.** Most permissive; matches today.
- **Typed → dyn implicit, dyn → typed via explicit `[to T $val]`.**
  Sound; matches Dart sound mode and TypeScript strict.
- **Both explicit.** Most ceremony; probably wrong for a dynamic-by-
  default language.

**Recommendation:** option 2. Keeps `dyn` ergonomic for incremental
typing while preventing silent type holes from opening into typed
code.

### 3. `inline_repr` — type or codegen detail?

The compiler tracks whether a struct value is currently inline-on-
stack vs heap-pointer. This is a *representation* choice, not a type.
But it's currently bundled with type tracking.

- **Option A:** keep `inline_repr` in the compiler (as codegen
  state). Typer ignores it.
- **Option B:** move it to the AST as `inferred_repr` so the typer
  can predict it.

**Recommendation:** option A. It's a codegen concern, not a type
concern. Keep it in the compiler; the typer just says "this is a
struct" and the compiler picks the rep.

### 4. Where do type errors get formatted?

Today error messages live in `compiler.c` with detailed context. If
the typer detects errors first, do those messages move?

- **Option A:** typer emits its own errors via a `typer__error`
  helper; format separately.
- **Option B:** shared error machinery between typer and compiler.

**Recommendation:** option B. The error formatting is mostly correct;
just have the typer call into the same helpers, parameterized by
which pass detected the error.

### 5. Migration order: top-down or bottom-up?

- **Top-down:** start with `compile_module`'s entry, walk down,
  migrate each AST shape's typer logic + consumer in tandem.
- **Bottom-up:** migrate leaves first (literals, var-refs), then
  expressions, then statements, then blocks.

**Recommendation:** bottom-up by AST shape. Each AST node type is
self-contained and can be migrated end-to-end (typer rule + compiler
consumer) before moving on.

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
| 4 — separate TypedAstNode | 1 week | 1 week | low |
| **Total (1-3)** | **4-6 weeks** | | |
| **Total (1-4)** | **5-7 weeks** | | |

LOC outcome estimates (post-Stage 3):

- compiler.c: −1500 to −2500 (−12% to −19%)
- typer.c: +800 to +1200 (+70% to +100%)
- Net: −700 to −1300

---

## Non-goals

To keep scope tight, the following are *not* part of this work:

1. **Adding new type-system features.** No type variables, no
   inheritance/subtyping beyond what exists, no type aliases, no
   refinement types. We're moving the existing system to a clean
   architecture, not extending it.
2. **Performance work.** The typer is an extra pass; if it's too slow
   we deal with that after correctness lands. No premature
   optimization.
3. **Touching `vm.c`, `runtime.c`, `parser.c`, `lexer.c`,
   `syntax.c`.** Type tracking lives in compiler.c and typer.c. Other
   files are out of scope.
4. **Changing the surface language.** Every program that compiles
   today (with permissive mode, decision 1) must still compile.
5. **Adding a separate type-check command.** Type errors keep firing
   during normal compile; we don't add `jacl typecheck` as a
   standalone tool.

---

## How to start

If this plan is approved, the first three commits are:

1. `typer: add audit-mode comparison build` — Stage 0, task 2.
2. `tests: add typer-only test harness` — Stage 0, task 1.
3. `docs: STATIC_TYPING_PLAN — decisions resolved` — write the
   decisions back into this doc once chosen.

After that, Stage 1 begins with the smallest AST shape (literals) and
walks outward.
