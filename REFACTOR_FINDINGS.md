# Compiler Simplification — Session Findings

Working notes from a refactor session focused on reducing duplication and
clarifying dispatch in `src/compiler.c`. Companion to `SIMPLIFICATION.md`
(which is older and broader).

## Headline numbers

```
Baseline (e02020e):   compiler.c 13,463 LOC, typer.c 1,180 LOC
After session:        compiler.c 12,948 LOC, typer.c 1,173 LOC
                      ast.c +384 (HeadId enum + lookup + struct field)
                      jacl.h  +47 (mirror enum + field)

Net source LOC:  ~ -250 across compiler/typer, +431 in headers.
                 (the headers carry the substrate that enables the rest.)

Tests: 93 passing throughout. No behavioral changes.
```

## Commits in this session

| SHA | What | Δ LOC |
|---|---|---|
| `52acae2` | head_id substrate (enum + 11 stamp sites + test) | + |
| `67a88e4` | `compile_command` 102 dispatches → integer compares | ±0 |
| `7781159` | walkers + macro special-form check → integer compares | -118 |
| `6af9414` | typer special-form dispatch | -7 (typer.c) |
| `679ca1f` | unify 9 walker shells via `ast__walk_children` / `ast__any_child` | -167 |
| `7b8f5ed` | table-drive syntax-*, unary, binop; vec-* receiver | -189 |
| `27ce849` | extract destructure helpers (`mark_global_mutable`, `apply_destructure_type`) | -45 |

Plus the substrate-only first commit at `52acae2`.

## What's now structurally different

1. **Every `AST_COMMAND` carries a stamped `head_id`.** Adding a new
   well-known builtin is a 1-line edit to `ast__head_id_for` in `ast.c`.
   No more 102 `memcmp` chains; consumers do `switch (hid)` or
   `if (hid == HEAD_FOO)`.

2. **AST walkers share two helpers.** `ast__walk_children` (void) and
   `ast__any_child` (bool short-circuit) own the
   `BLOCK/INTERP_STRING/BREAK/RETURN/SHELL_CMD` recursion shape. Each
   walker now consists of: a Ctx struct, a `__visit` / `__pred` function
   that handles `AST_COMMAND` (and sometimes `AST_VAR_REF`), and a thin
   wrapper. Adding a new AST node kind updates **one** place
   instead of 11.

3. **Three builtin clusters are table-driven.** `syntax-*` introspection,
   uniform unary-emit builtins, and arithmetic/comparison binops each
   collapsed to a small static table indexed by `HeadId`. Each row is
   `{HeadId, name, opcode, result_type, ...}`.

4. **Destructure compilers share two helpers.** `mark_global_mutable`
   and `apply_destructure_type` replaced 6 + 5 copies of the same
   pattern. The two functions still exist separately — fully fusing
   them would lose clarity (vec uses index access, named uses key
   access; struct fast path; rest semantics differ).

## What was sized but not done

### "Finish the typer" — full week of work, not a quick win

The typer (1,173 LOC) handles ~60% of type queries. The compiler has
**22 fallback sites** of the shape:

```c
JaclType lhs_type = (JaclType)args[0]->inferred_type;
compiler__compile_node(c, args[0]);
if (lhs_type == TYPE_DYN) lhs_type = c->last_expr_type;  // fallback
```

Plus 208 reads of `c->last_expr_type` and 41 of `c->expected_type` — the
dual type-tracking system that exists *because* the typer is partial.

**Three phases** to finish, with very different risk profiles:

| Phase | Scope | Δ LOC | Risk | Days |
|---|---|---|---|---|
| A | Fill typer gaps for vec-*/map-*/atom?/error?/range/HOFs/if/while/for/try | +400 typer, ±0 compiler | low | 1-2 |
| B | Drop the 22 fallback sites | -50 compiler | low | 0.5 |
| C | Delete `c->last_expr_type` field, audit 208 reads | -200 to -400 compiler | medium-high | 3-5 |

**A+B alone net adds LOC.** The conceptual win (typer is the sole
authority on AST node types) doesn't pay for itself on raw LOC. The
real win is Phase C — but that's a real refactor, not a refactor of
state. Each of those 208 reads needs classification: pure type query
vs codegen scratchpad.

### "Move pure builtins to prelude" — turned out to be illusory

Originally estimated at 300-700 LOC saved across compiler+VM. After
reading the code: **almost every builtin maps directly to a single VM
opcode.** There's no library function to extract — it's a 4-line
compile-side dispatch wrapping a 1-line opcode emission.

To genuinely move a builtin to the prelude you'd need one of:

1. **Expose opcodes to prelude code** via a new `(builtin OP_NAME ...)`
   primitive. That's a language feature addition, not a refactor.
2. **Express builtins via other builtins** (e.g., `count` for vecs is
   `vec-len`; `first` is `take 1`). Possible for a few but it's an API
   decision, not refactoring.
3. **Wrap each opcode in a prelude proc.** Compiles `[length $x]` to a
   function call instead of a single opcode emit. Net negative on
   performance, no LOC win.

**Recommendation: drop this from the refactor list.** It's a
language-design conversation, not a code-cleanup one.

### Other levers from the original assessment

| Lever | Realistic LOC | Risk | Notes |
|---|---|---|---|
| Drop SM liveness pass | ~600 | medium | Constant memory regression per active generator. Unless benchmarked. |
| Remove HOF builtins (`transform`/`filter`) via stream-construction primitive | ~70 | medium | Language API change |
| Drop `count` / `first` (composable from `vec-len` / `take`) | ~40 | low | API change |
| Move `to-string` / `hash` formatting to JACL | ~100 | medium | Opcodes stay; just shift formatting |

## Recommendations for the next session

The cheap, mechanical wins are mostly consumed. The remaining options
split into three buckets, each with a different shape of work:

### Bucket A — finish what's started (low-medium risk)

1. **Phase C of typer finish (audit `c->last_expr_type`).** The biggest
   single LOC reduction left, but slow and silent-miscompile-prone.
   Prerequisite: Phase A to fill typer gaps.
2. **Look at `vm.c` (11,435 LOC).** Hasn't been audited yet; likely has
   its own duplication patterns. Big potential surface.

### Bucket B — bounded language-API decisions (need user direction)

1. Remove `count` / `first` as builtins; require composition.
2. Remove `transform` / `filter` builtins; require explicit stream
   construction.
3. Decide if SM liveness optimization pays for itself. If not, delete
   ~600 LOC.

### Bucket C — broader audit

1. **`syntax.c` macro expansion** (1,381 LOC) — likely shares walk
   patterns with the now-unified compiler walkers.
2. **`runtime.c`** (1,533 LOC) — task scheduling, futures, channels.
   Might have similar dispatch duplication.
3. **The three "compile statement list" functions**
   (`compile_block_expr`, `AST_BLOCK` case in `compile_node`,
   `compile_sm_stmts`) — probably foldable to one helper with two
   flags. ~80-120 LOC.
4. **`compile_command` is still 5,800 LOC.** Now uniform-dispatching,
   but each branch is still its own logic. Consider whether the
   `vec-*`/`map-*` clusters should split into separate files
   (organizational, not LOC-reducing).

## Open questions for the next session

1. Is the compiler's runtime perf a constraint? Several refactors
   trade clarity for an extra function call (e.g. wrapping opcodes in
   prelude procs).
2. Are there benchmarks that justify the SM liveness pass? If not, it's
   ~600 LOC for memory we may not need.
3. Is there appetite for language-API changes (removing `count`,
   `first`, etc.)? That unlocks more reduction but changes user-facing
   surface.
4. Would consolidating destructure-vec and destructure-named into a
   single function with an `indexer` callback be acceptable, or is
   keeping them separate the right call? (My read: keep them separate
   — the differences are real.)

## Concrete starting points for next session

If picking up cold without a specific direction:

- **`/loop` candidate:** Audit `vm.c`. Apply the same recipe used here:
  enumerate dispatch sites, find shared shells, extract helpers. The
  walker pattern won't apply (no AST), but other duplications likely
  will.
- **Or:** Phase A of the typer finish. Mechanical, low-risk, sets up
  Phase C as a future option without committing to it.
- **Avoid:** Starting Phase C of the typer cold. Too much state to
  reason about without setup work in Phase A first.

## Reference: the head_id substrate

The single most useful structural change. Every `AST_COMMAND` now has
`node->data.command.head_id` set at construction time:

- Defined in `ast.c` (canonical) and mirrored in `jacl.h`
- Lookup function: `ast__head_id_for(const char* s, uint32_t len)`
- Stamping helper: `ast__compute_head_id(AstNode* head)`
- Stamped at: 9 parser sites, 1 macro-expansion site
  (`syntax_to_ast`), 2 compiler-rewrite sites
  (`rewrite_binding_op`, `compile_pipe_op`)
- Validated by: `test/test_head_id_stamp.c`

Adding a new well-known head is a 1-line edit in `ast__head_id_for`'s
length-dispatched switch.
