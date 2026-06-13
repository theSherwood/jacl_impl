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
  just the library. **Current baseline: full suite 85 passed / 6 failed groups;
  jacl_harness 4 failing scenarios.** Start by reproducing this.
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

**Full suite: 85 passed / 6 failed groups** (was 65 passing). Remaining failing:
- `parser`, `compiler`, `syntax` — unit tests that assert the *old* prefix AST;
  need rewriting for the flip (substantial, separate).
- `integration` — one edge: `[def]` (a keyword zero-arg head) returns the bare
  word `"def"` instead of a command (no arity error). Keyword heads don't wrap;
  wrapping all keywords regressed 6 other scenarios (assert/neq/timeout), so a
  surgical fix is deferred.
- `destructure_spread_all` — uses old `{..}` spread-all named destructure.
- `jacl_harness` — 4: `tour`, `typed_closure_binding` (compound `[Proc …]`, Phase
  B3), + 2.

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
     - REMAINING (the hard part): the `args[N] == AST_BLOCK` guards in the
       control-flow handlers (`for`/`try`/`with-ctx`: compiler 2213/2221/2233/12545/
       12676/12680/12686, typer 4048/4050/4078/4079/6226). These are entangled —
       some args are **non-body braces** (C-style for control `{init;cond;step}`,
       `with-ctx` overrides) which are value-position `{…}` (step 4), so steps 3 &
       4 must be coordinated here, not separated. Destructure guards (def `args[0]`:
       2084/8824/10368, typer 1676) stay until step 5.
  2. **Migrate the body guards to the accessors (neutral, bodies still AST_BLOCK):**
     replace `X->type == AST_BLOCK` → `ast__is_seq(X)`, `!= AST_BLOCK` →
     `!ast__is_seq(X)`, `X->data.block.count` → `ast__seq_count(X)`,
     `X->data.block.commands[i]` → `ast__seq_stmt(X,i)`. **Only the BODY guards** —
     leave destructure guards (def `args[0]` etc.) until step 5.
     - Body guards (compiler): `1487 1491 2598 2609 2610 2620 2753 2831 2849 5642
       11376 12109 12341 12345 12469 12545 12676 12680 12686 14128 14136 14211
       14215 16538` (plus the typer's body guards). Destructure (leave):
       `2084 2213 2221 2222 2233 2234 8824 10368`. Peel: `102`.
     - Note `trailing_semi`: a `[do]` materializes a trailing `nil` element
       instead of a flag; sites reading `block.trailing_semi` need the do
       equivalent (last arg is nil), or rely on compile_seq's last-element rule.
  3. **Flip bodies → `[do]`:** re-add `parse_body_seq` (= `parse_block` + always
     `make_do`, no single-statement collapse) and point the 5 proc/if/while/else
     parser body callers at it (1824/1866/1928/1950/2008, pre-accessor line nums).
     Now guards accept it; verify green. Then macro body too.
  4. **Flip value-`{…}`** (parse_expr `TOKEN_LBRACE`, ~685/703) to `block_to_value`
     like `[…]` so non-body braces stop producing `AST_BLOCK`.
  5. **Destructure off `AST_BLOCK`:** make `{a,b,c}`/`[a,b,c]` patterns parse to the
     existing `AST_DESTRUCTURE_VEC`/`AST_DESTRUCTURE_NAMED` nodes (the dedicated
     `parse_destructure_*` parsers exist but are currently unused); update the
     destructure recognizers off the block form. This also fixes
     `destructure_spread_all` (spread-all is currently broken).
  6. **Delete `AST_BLOCK`:** remove the enum value, `compile_block_expr`,
     `infer_block`, the `AST_BLOCK` dispatch/analysis cases, and the dead
     `as_type_command` peels. The accessors' `AST_BLOCK` branch becomes dead too.

Pending (other) — all bigger/deeper than "clean slices" (verified this pass):
- **`destructure_spread_all`** — NOT just a `{..}`→`[..]` rename: spread-all
  destructure is *functionally broken* under the flip — `{..}`/`[..]` don't bind
  the struct fields ("undefined variable"), and `[x, ..]` errors "elements must be
  names". Entangled with destructure-pattern recognition (the dedicated
  `parse_destructure_named_pattern` and the `AST_BLOCK` destructure path).
- **`tour.jacl`** — behavioral: an `[assert $== $hit 5]` fails (a value is computed
  wrong somewhere in the tour); needs debugging, not a syntax fix.
- **`typed_closure_binding`** — compound `[Proc …]` param/return inference (Phase
  B3 "not yet supported"); deep type-system.
- Surgical `[def]`/keyword-head wrap (wrapping all keywords regressed 6 scenarios).
- Unit-test rewrites (parser/compiler/syntax) for the new AST — substantial.
- 2 multi-line `.jacl` statement brackets (tiny).
- `{}` → `[]` for struct fields, `use`, and named/spread destructure.
