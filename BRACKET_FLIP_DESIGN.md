# Bracket Flip — `[]` Command-Mode Migration

Tracks the migration that makes `[]` the single command/expression delimiter
(was the prefix-expression delimiter; `{}` was command mode). This is the
authoritative spec for what `[]` means in every position.

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
- **Retire `AST_BLOCK` (next, mechanical):** make proc/if/while/else/macro bodies
  parse to an always-`[do …]` command (a `parse_body_seq` = `parse_block` +
  unconditional `make_do`, no single-statement collapse — collapse is value-only).
  Then change the 11 `compile_block_expr` callers → `compile_node` and the
  `infer_block` caller → `infer_node`; `compile_node`/`infer_node` route `[do …]`
  to `compile_seq`/the HEAD_DO infer, which is equivalent (incl. tail
  `[Proc …]` monomorphization — a single-statement `[do [proc …]]` still
  monomorphizes the tail). Finally delete the dead `AST_BLOCK` compile/infer
  cases and the now-dead `as_type_command` peels.

Pending (other):
- 2 multi-line `.jacl` statement brackets; `destructure_spread_all` `{..}`.
- Compound `[Proc ...]` param/return inference (`typed_closure_binding`).
- Surgical `[def]`/keyword-head wrap.
- Unit-test rewrites (parser/compiler/syntax) for the new AST.
- `{}` → `[]` for struct fields, `use`, and named/spread destructure.
