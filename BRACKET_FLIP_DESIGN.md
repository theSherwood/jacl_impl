# Bracket Flip — `[]` Command-Mode Migration

Tracks the migration that makes `[]` the single command/expression delimiter
(was the prefix-expression delimiter; `{}` was command mode). This is the
authoritative spec for what `[]` means in every position.

## Handoff (start here)

- **Branch:** `claude/flip-bracket-mode` (off `main`, which has Stage A/B). Pushed
  and in sync with `origin`. The core flip is **done and working**; what remains
  is cleanup/migration (see "Migration status").
- **Build / verify:** `./build.sh` runs the whole suite; `./build.sh --test=NAME`
  runs one group (e.g. `--test=jacl_harness`, `--test=typed_vec`); `--lib` builds
  just the library. **Current baseline: full suite 90 passed / 1 failed —
  jacl_harness 586/587, `typed_closure_binding` only (Phase B3 compound
  `[Proc …]` inference, separate feature work).** Start by reproducing this.
  `--tsan`: same + `chase_lev_stress` (a known-safe tsan failure).
  `--wasm`: skipped in the cloud sandbox (no emcc).
- **Key code entry points:**
  - Value `[…]` lowering: `parser__block_to_value` + `parser__make_do` (src/parser.c).
  - Value-position application/call wrap: the `in_value_bracket` flag (Parser
    struct, src/parser.c) — set in `parse_expr`'s `TOKEN_LBRACKET` case, read-and-
    cleared at the top of `parser__parse_cmd_operand`, used in its zero-arg branch.
  - `[do …]`: `HEAD_DO` head-id (src/ast.c), `compiler__compile_seq` + the
    `HEAD_DO` dispatch in `compiler__compile_command`, and the `HEAD_DO` case in
    `typer__infer_command_inner`.
  - Sequence/scope: `compiler__compile_block_expr` and `typer__infer_block` both
    delegate to `compile_seq` / always-scope.
  - Dead transitional code to delete in the retire: `compiler__as_type_command` /
    `typer__as_type_command` peels (no-ops now); destructure still on `AST_BLOCK`
    at `compiler.c:2006/2084/2213`.
- **Migration helpers:** corpus debrace + `$op` operator escaping were applied via
  one-off python scripts (see commits `e6fad6a`, `6dcd0a0`, the C-lambda commits);
  reuse the same shape for remaining migrations.
- **Note:** a few early commits (`a96e1e6`, `ec2cd44`, …) predate the git
  committer-email being set to `noreply@anthropic.com`, so GitHub shows them
  "Unverified". Cosmetic; rebase-reset-author if you want them clean.

## Core model

- **A line that does not start with `[` is a command application** (Tcl-style):
  `print 7` = apply `print` to `(7)`. The first element is the head.
- **`[...]` in value position is an application that yields a value**:
  `[+ 1 2]` = apply `+` to `(1,2)` = `3`. The first element is the head, the
  rest are arguments.
- **There is no separate `AST_BLOCK` node.** A sequence is the `do` form,
  `[do s0 s1 ...]` — an ordinary command. (See "The `do` form".)
- **`[]` is fundamentally a list; its interpretation is decided by position**
  (value / type / binding / body), exactly like `()` in Lisp. We are *not*
  pure-Lisp, though: separators carry meaning and binding positions parse
  their own item lists (see "Separators").

## Separators inside `[]`

| Separator | Meaning |
|---|---|
| space | arguments of an application — `[f a b]` = apply `f` to `(a,b)` |
| comma `,` | item separator. In **binding** position → list items (params, fields, destructure names). In **value** position → statement separator (→ `do`). |
| `;` / newline | statement separator (→ `do`) |

So `[i64 x, f64 y]` (binding) is a list of two items, each a space-group
(`i64 x`); `[a; b]` (value) is `[do a b]`.

## `[]` by position

### 1. Value / expression — `[]` is an **application**

| Use | Example | Sep | Result |
|---|---|---|---|
| Call | `[f a b]`, `[+ 1 2]`, `[$f x]` | space | apply `f` to args |
| Zero-arg call | `[f]`, `[$f]` | — | apply to `()` → call (words **and vars**) |
| Literal | `[5]`, `["s"]` | — | the value (literals are **not** called) |
| Sequence | `[a; b]`, multi-line | `;`/nl | `[do a b]` |
| Empty | `[]` | — | nil (an empty `[do]`) |
| Populated constructor | `[[Vec T] e0 e1]` | space | construct `Vec T` with elements |
| Empty constructor | `[[Vec T]]` | — | apply the type to `()` → **empty** `Vec T` |

### 2. Type — `[]` is a **type**

| Use | Example |
|---|---|
| Compound | `[Vec T]`, `[Map K V]`, `[Arr T]`, `[Box T]`, `[Ptr T]`, `[Future T]`, `[Stream T]` |
| Sized buffer | `[Buf N T]` |
| Proc type | `[Proc [i64, f64] R]` — outer space, **comma** in the param-type list |

### 3. Binding / structural — `[]` is a **comma-list of items** (not an application)

Each parsed by a dedicated, position-aware parser; the `do` lowering does
**not** apply here.

| Use | Example |
|---|---|
| Param list | `[x, y]`, `[i64 x, f64 y]` |
| Positional destructure | `[a, b, c]`, `[h, ..t]` |
| Named (struct) destructure | `[x, y]` |
| Struct fields | `struct P [i32 x, i32 y]` |
| `use` imports | `use [a, b]` |

### 4. Body / block — `[]` is a **sequence** (lowers to `do`)

`proc f [x] [body]`, `if c [body]`, `while c [body]`, `else [body]`,
`for $coll x [body]`.

## Key decisions

0. **OPEN ISSUE — call vs. block is no longer structurally distinguishable.**
   Surfaced by the body-position work: `[…]` is *one* spelling for two
   different things — an application (`[print $it]` = call print now) and a
   statement sequence (`[print $it]` as a for-body = run per element). Nothing
   in the syntax distinguishes them; **the head decides**, per argument
   position (`parser__head_body_arg`: for/try/spawn/with-ctx/parallel/race).
   This is Tcl-like (each command interprets its own args) but has real
   consequences:
   - `for $coll [\ print $it]` (HOF callback = a *value* arg) vs
     `for $coll [print $it]` (body) is disambiguated only by peeking for the
     `\` token inside the bracket — a special case, not a rule.
   - User-defined procs/HOFs **cannot** take a literal block argument: a
     `[…]` arg to a non-special head is always an application. The only
     explicit "sequence as a value" spelling in such positions is `[do …]`.
   - Macros that take bodies work because args arrive as unevaluated syntax —
     but the same `[…]` arg parses differently under a macro head (whatever
     the template does with it) than under `for`.
   - Bitten in practice by the corpus debrace: `timeout 0.02 [ slow ]` and
     `unless $c [ set hit 5 ]` (macro heads) collapse the single-statement
     arg to a call. NOTE there is no good explicit spelling either: a typed
     `[do slow]` parses `slow` as an *atom arg*, not a statement (do's args
     are only statement-parsed when produced by `;`/newline lowering) —
     you'd need `[do [slow]]`. Those corpus call sites keep `{…}` for now
     (tour.jacl macro section, timeout_fires/timeout_success).
   Candidate resolutions to evaluate before `{}` is dropped: (a) accept the
   per-head table as the language rule and document each builtin's body
   positions as part of its signature; (b) require explicit `[do …]` for all
   block args and remove the per-head table (uniform but noisy); (c) keep a
   dedicated block-literal spelling. Until decided, the per-head table is
   the behavior.

1. **`[Vec T]` ≠ `[[Vec T]]`.** `[Vec T]` is a *parameterized type* and is only
   valid in type position — **there is no bare-type-as-value**. `[[Vec T]]` is a
   *form with `[Vec T]` as the head*, i.e. it constructs an **empty** `[Vec T]`.
   `[[Vec T] e0 e1]` constructs a populated one. None of these collapse: the
   outer bracket is a real application of the type.
2. **Comma is position-dependent:** statement separator (→ `do`) in value
   position; item separator in binding position.
3. **Named vs positional destructure** share the spelling `[a, b, c]` and are
   disambiguated by the **RHS type** (vec → positional, struct → named), which
   the typer already infers.
4. **`{}` → `[]` everywhere**, including the last holdouts: struct fields
   (`struct P [i32 x, i32 y]`) and `use` imports (`use [a, b]`).
5. **`[$f]` calls** the closure in `$f`; the *value* of a var is the bare `$f`.
   Migration: any `[$x]` that meant "the value of x" becomes `$x`.
   - **A bare `$x` statement is a *value*** (substitution), not a call — *for
     now*. NOTE: this may change; a stricter "every statement line is a command
     application" reading would make a bare `$x` line a call. Revisit before
     finalizing.
   - **Implementation consequence:** the zero-arg "apply a callable head"
     wrap (so `[$f]` calls, `[[Vec T]]` constructs) must fire **only inside a
     value-position bracket**, *not* when parsing a statement/body. A leading
     word at statement position is still a command (`foo` → call `foo`); a
     leading `$var` or `[...]` at statement position is a value. So the parse
     needs a value-position vs statement-position distinction (e.g. a flag
     threaded into the operand parser), since `[$x]` as an *argument* is a call
     but `[$x]` as a *body* (its single statement `$x`) is the value of x.
6. **`[5]` / `["s"]`** (a bare literal head) returns the value — literals are not
   called.
7. **`[]`** is nil (an empty `[do]`).
8. **`match`** is out of scope (not supported yet).

## The `do` form

`[do s0 s1 ...]` is the single sequence/scope primitive (replaces `AST_BLOCK`):
opens a scope, runs each statement, value = the last element. An empty `[do]`
is nil. A **trailing separator** materializes a trailing `nil` element, so
"value = last element" needs no `trailing_semi` flag:
`[a b; c d;]` → `[do [a b] [c d] nil]` → nil. `do` is user-callable.

`HeadId` is `HEAD_DO`; compiled by `compiler__compile_seq`, typed by the
`HEAD_DO` case in `typer__infer_command_inner`.

## Migration status

Done (branch `claude/flip-bracket-mode`):
- Stage A/B operator-as-value, type-driven & juxtaposition positional (on `main`).
- `[do]` form added (additive).
- Value-position `[...]` lowers to bare-command / `[do ...]` / nil.
  Type-confusion segfaults (`def [Vec T]`, `def [Map K V]`,
  `typed_closure_compound_map`) resolved at the source.

- Corpus **debraced**: ~197 single-line command statements (word/var/operator
  headed) → bare `cmd ...` across the `.jacl` files. Behavior-preserving
  against the current parser (jacl_harness stays 20); pre-positions the corpus
  so the strict-application wrap has no lone-bracket *statements* to
  double-apply.

- **Strict-application wrap DONE** (`in_value_bracket` flag, read-and-cleared at
  operand entry): value-position `[...]` applies a zero-arg callable head —
  `[$f]` calls, `[[Vec T]]` constructs an empty `Vec T` (no collapse), `[foo]`
  calls; bare `$x`/`[…]` at statement/body position stay values; literal → value.
  **jacl_harness 20 → 4** (≈2 real: `tour`, `typed_closure_binding`);
  `typed_vec`/`typed_map` now pass (empty `[[Vec T]]`); no regressions, no
  segfaults.

- **C-embedded operator-lambda migration DONE**: `[\ > $it 3]` → `[\ $> $it 3]`
  in `test/*.c` (whitespace-flexible). Greened stream_filter, stream_transform,
  yield, lines, stream_seq_ops, stream_type_enforce. typer now passes too.

**Full suite: 90 passed / 1 failed** (was 65 passing). All previously failing
groups are green:
- `parser`, `compiler`, `syntax` — unit tests REWRITTEN for the flip AST (seq
  accessors; flip-semantics expectations: `[x = 5]` binds, `[foo | bar]`
  pipes, `[. $p x]` is the desugared arrow, `[]` is the empty `[do]`,
  unclosed-bracket recovery swallows to EOF). The pretty-printer renders
  `[do …]` in brace form, lambda head `\` bare, operator args `$`-escaped —
  roundtrips hold.
- `integration` — `[def]`-style keyword heads in value brackets now wrap to
  zero-arg commands → real arity errors. Statement separators are enforced in
  parse_block (leftover tokens after a statement are a parse error), and the
  compiler surfaces parser messages from AST_ERROR nodes.
- `destructure_spread_all` — FIXED by step 4 (seq-unified destructure path).
- `jacl_harness` 586/587 — only `typed_closure_binding` (compound `[Proc …]`,
  Phase B3 type-system feature, separate work).

**Structural cleanup (in progress):**
- DONE: `compile_block_expr` and `typer__infer_block` now delegate to the single
  `compile_seq` / always-scope sequence implementation; the value-position
  scope-hacks are removed (value brackets lower to commands and no longer reach
  the block path). Neutral (jacl_harness 4, all groups green).
- **Retire `AST_BLOCK` — LARGER than first estimated (revised after attempt):**
  - DONE (step A, neutral, committed): the 10 explicit `compile_block_expr` body
    call sites now call `compile_node` (the `AST_BLOCK` dispatch at
    `compiler.c:~19147` still routes there, so it's a no-op until bodies change).
    The typer already infers bodies via `infer_node` → the `AST_BLOCK` case, so it
    needs no caller change.
  - **NOT mechanical:** converting proc/if/while/else bodies to `[do …]` commands
    (tried: `parse_body_seq` + 5 callers) **broke 376 scenarios.** Cause: there
    are **~53 `type == AST_BLOCK` guard checks** across compiler (34) + typer (19)
    that gate body handling (e.g. `compiler.c:1487/1491`) — a `[do]` command isn't
    `AST_BLOCK`, so every guard rejects it. Full conversion must update all of
    those (or make them accept `HEAD_DO`), not just the 11 callers. Reverted to
    green for now.
  - **Entanglement:** `AST_BLOCK` is *also* the destructure-pattern representation
    (`{a,b,c}`/`[a,b,c]`; `compiler.c:2006/2084/2213`), so deleting the node means
    converting bodies AND destructure together. This is a multi-step focused pass,
    not a single slice. (Sequence/scope unification + step A are the safe prereqs.)

  **DECISION (user): delete `AST_BLOCK` — it's two-mode-split debt. Execution plan
  (each step neutral-verifiable, commit green):**
  1. DONE: seq accessors (ast.c, jacl.h) — `ast__is_seq`, `seq_count`, `seq_stmt`,
     `seq_stmts` (array), `seq_trailing_semi` ([do] trailing-nil = nil). Uniform
     over `AST_BLOCK` or a `HEAD_DO` command.
     - DONE (neutral, committed): migrated all **body validations**
       (`!= AST_BLOCK → error`, 14 sites → `!ast__is_seq`) and all **body-variable
       content accesses** (`body`/`body_blk`/`body_block`/`then_block`/`else_block`
       + `args[body_idx]` detection → `is_seq`/`seq_*`).
  2. DONE (neutral, committed): ALL remaining guards migrated to the accessors —
     the control-flow `args[N] == AST_BLOCK` guards (`for`/`try`/`with-ctx`,
     C-style for control reads, with-ctx override reads, while body compile)
     in compiler + typer.
  3. DONE (committed, green): **bodies → `[do]`**. `parser__parse_body_seq`
     (= `parse_block` + always `make_do`, no single-statement collapse; trailing
     separator materializes a trailing nil) feeds the proc / if-then / else /
     trailing-else / while / **macro** body callers; the elif desugar synthesizes
     a `[do]` wrapper. Fallout fixed in the same slice:
     - `compiler__find_disallowed_generator_tail` needed a seq case (a `[do]`
       body was itself reported as a bad generator tail).
     - `sm__head_uses_operand_stack_for_args`: `HEAD_DO` belongs in the
       special-form `false` list (statements compile at parent depth) — without
       it, suspensions inside if/while branches got bogus operand depth and
       cps_if_suspend asserted in vm__run.
     - `expand__compile_staged_body` (syntax.c) called `compile_block_expr`
       directly on the macro body → segfault on the command form; routed through
       `compile_node`.
  4. DONE (committed, green): **value-`{…}` → `[do …]`** (parse_expr
     `TOKEN_LBRACE` → `parse_body_seq`; always-do, NOT `block_to_value`, to keep
     the scoped-sequence semantics and keep for-bodies/single-statement patterns
     as seqs). Destructure recognizers were entangled and migrated in the same
     slice: def/mut named-destructure paths, `block_as_positional_pattern`, SM
     locals/liveness pattern walks, typer named-destructure — all via
     `is_seq`/`seq_*`, with `HEAD_DO` excluded from the bracket-positional
     command form (a `[do x y]` pattern is an AST_COMMAND; seq checks must come
     FIRST at every recognizer). **This fixed `destructure_spread_all`** (85→86
     groups) — step 5's dedicated-node conversion is now optional cleanup, not a
     prerequisite.
     **The parser no longer emits AST_BLOCK at all** (every `parse_block` caller
     wraps; only parser-internal/transient + 2 synthetic `fake_block` sites in
     compiler_compile remain).
  5. (now optional) **Destructure onto dedicated nodes:** `{a,b,c}`/`[a,b,c]`
     patterns → `AST_DESTRUCTURE_VEC`/`AST_DESTRUCTURE_NAMED` (the dedicated
     `parse_destructure_*` parsers exist, unused). Patterns currently ride the
     `[do]` form through the seq accessors and work, including spread-all.
  6. **Delete `AST_BLOCK`:** remove the enum value, `compile_block_expr`,
     `infer_block`, the `AST_BLOCK` dispatch/analysis cases (compiler 1262/1293/
     3466/18482/19164, typer 3518/6942, syntax.c 99/415/911/1072, ast.c 783/837,
     vm.c 14551), the dead `as_type_command` peels (compiler 102, typer 295),
     and the 2 synthetic `fake_block`s (compiler ~20414/21101 — convert to a
     stack-built HEAD_DO command). `parse_block` internals must stop building
     the node (return stmts+count+trailing instead). **Blocked on the unit-test
     rewrite:** test_parser/test_compiler/test_syntax/test_head_id_stamp still
     reference AST_BLOCK and would stop compiling — rewrite them for the new
     AST first (they're already failing groups).

Pending (other) — all bigger/deeper than "clean slices" (verified this pass):
- **`tour.jacl`** — FIXED: `[if ~cond {…} ~body]` templates dropped `~body` out
  of the if (parse_if_form's else-detection), so `unless` ran its body
  unconditionally. A same-line `~x` at syntax-quote depth 1 now parses as the
  else branch.
- **`typed_closure_binding`** — compound `[Proc …]` param/return inference (Phase
  B3 "not yet supported"); deep type-system.
- DONE: surgical `[def]`/keyword-head wrap (value-bracket keyword heads only).
- DONE: unit-test rewrites (parser/compiler/syntax) for the new AST.
- DONE: **every position now accepts `[]`** — `{}` is a fully redundant
  spelling:
  - Body-position `[…]` for the generic-command heads (`parser__head_body_arg`
    in parse_cmd_operand): `for $coll [body]`, C-style `for [i = 0; …] [body]`,
    `try [body] e [handler]`, `spawn [body]`, `parallel`/`race` (all args),
    `with-ctx [overrides] [body]`. Parsed via parse_body_seq (always-do, no
    collapse); a `[\ …]` lambda arg stays a value so for's HOF callback form
    is unchanged.
  - `struct Name [i32 x, i32 y]` and `use "path" [a, b]` (delimiter
    auto-detect, like proc params).
  - Covered by `test/jacl/bracket_bodies.jacl` + `modules/use_bracket`.
  Remaining for ACTUAL `{}` removal: scripted corpus debrace (~396 .jacl files
  + embedded C sources), SYNTAX.md/AGENTS.md/playground-highlighter updates,
  then drop `{}` from the value/body grammar (and step 6 rides along).
- Step 6 (delete `AST_BLOCK`) is now unblocked: no test file asserts the node
  type anymore except via the accessors; remaining references are the parser
  internals, dispatch cases, syntax converters, and 2 synthetic fake_blocks
  (see step 6 list above).
