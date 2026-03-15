# PRD: Three-Mode Syntax Redesign

## Introduction

Redesign JACL's syntax from a single-mode `[]`-only system to a three-mode delimiter system: `[]` (juxtaposition), `{}` (command mode), and `()` (infix mode). This is a clean break — the old syntax stops working and all tests are rewritten. The goal is to make JACL feel like a pragmatic scripting language comfortable in a shell, not a Lisp with brackets.

All three modes resolve to a uniform `[]`-shaped AST internally, but maintain distinct surface syntax with different parsing rules. This PRD covers making the **existing feature set** work with the new syntax — no new runtime features (streams, `$ctx`, shell interop, etc.).

**Prerequisite:** Merge the `ralph/compiler-vm-refactor` branch to `main` before starting this work.

## Goals

- Replace the `[]`-only syntax with three distinct parsing modes
- `{}` command mode becomes the primary way to write code (top-level and block bodies)
- `()` infix mode for arithmetic and comparisons (no precedence, left-to-right)
- `[]` juxtaposition mode for nested calls and explicit grouping
- Rename builtins to match SYNTAX_REDESIGN.md (`each`→`for`, `defstruct`→`struct`, `set!`→`set`, `reset!`→`reset`, `swap!`→`swap`)
- Implement pipe threading (`|` in command mode = first-arg threading)
- Implement binding operators (`=`, `:`, `::` as sugar for `def`, `mut`, `set`)
- Implement `->` field access syntax (`$var->field`)
- Implement logical operators `&&`, `||`, `~` in `()` infix mode
- All existing tests rewritten for new syntax and passing
- Maintain gradual typing — untyped code still works

## User Stories

### US-001: New Token Types

**Description:** As a compiler developer, I need the lexer to emit new token types for the three-mode syntax so that the parser can distinguish between modes.

**Acceptance Criteria:**
- [ ] Add `TOKEN_PIPE` for `|` (currently lumped into `TOKEN_OPERATOR`)
- [ ] Add `TOKEN_ARROW` for `->` field access
- [ ] Add `TOKEN_BACKSLASH` for `\` (lambda shorthand and line continuation)
- [ ] Add `TOKEN_BANG` for `!` (shell interop prefix — lexed now, used later)
- [ ] Add `TOKEN_DOTDOT` for `..` (splat/spread — lexed now, used later)
- [ ] Add `TOKEN_AMP` for `&` (background job sugar — lexed now, used later)
- [ ] Add `TOKEN_AND` for `&&`, `TOKEN_OR` for `||`, `TOKEN_NOT` for `~`
- [ ] Add `TOKEN_EQUALS` for `=`, `TOKEN_COLON` for `:`, `TOKEN_DOUBLE_COLON` for `::`
- [ ] Add `TOKEN_STRUCT` keyword (replacing `TOKEN_DEFSTRUCT`)
- [ ] Add `TOKEN_PROC`, `TOKEN_IF`, `TOKEN_ELIF`, `TOKEN_ELSE`, `TOKEN_WHILE`, `TOKEN_FOR`, `TOKEN_DEF`, `TOKEN_MUT`, `TOKEN_SET`, `TOKEN_MATCH`, `TOKEN_RETURN`, `TOKEN_BREAK`, `TOKEN_CONTINUE`, `TOKEN_TRY` as keyword tokens
- [ ] Remove `TOKEN_DEFSTRUCT`
- [ ] `$[expr]` string interpolation continues to work
- [ ] Add `TOKEN_DOLLAR_PAREN` for `$(expr)` string interpolation in infix mode
- [ ] Existing non-keyword tokens (`TOKEN_INT`, `TOKEN_FLOAT`, `TOKEN_STRING`, `TOKEN_VAR`, `TOKEN_WORD`, etc.) unchanged
- [ ] All new tokens have correct source position tracking (line, column, byte offset)
- [ ] Lexer test file rewritten and passing

### US-002: Three-Mode Parser — Juxtaposition (`[]`)

**Description:** As a compiler developer, I need the parser to handle `[]` as juxtaposition mode — whitespace-separated items where the first item determines semantics.

**Acceptance Criteria:**
- [ ] `[foo 1 2]` parses as `AST_COMMAND` with head `foo`, args `1`, `2` (unchanged from today)
- [ ] `[foo [bar 3] 2]` nesting works (unchanged from today)
- [ ] `[+ 1 2]` works for prefix arithmetic (unchanged from today)
- [ ] `$[expr]` inside strings works for juxtaposition subexpressions (unchanged from today)
- [ ] `[]` does NOT support `|`, `;`, `,`, or newline as separators — those are `{}` features
- [ ] Parser tests for juxtaposition mode passing

### US-003: Three-Mode Parser — Command Mode (`{}` and Top-Level)

**Description:** As a compiler developer, I need the parser to handle `{}` blocks and top-level code as command mode — bare commands with separators and pipes.

**Acceptance Criteria:**
- [ ] Top-level code parses in command mode (no surrounding `[]` needed)
- [ ] `{}` blocks parse in command mode
- [ ] Commands separated by newline: `print "a"\nprint "b"` → two commands
- [ ] Commands separated by semicolon: `print "a"; print "b"` → two commands
- [ ] Commands separated by comma: `print "a", print "b"` → two commands
- [ ] Bare command syntax: `+ $x $y` in command mode means `[+ $x $y]` (head is first word/operator)
- [ ] Block bodies use bare command syntax: `{* $n 2}` not `{[* $n 2]}`
- [ ] Nested `[]` for subexpressions within command mode: `print [+ 1 2]`
- [ ] Nested `()` for infix within command mode: `print ($x + 1)`
- [ ] Nested `{}` for sub-blocks within command mode: `if $cond { body }`
- [ ] Empty block `{}` is valid (produces nil)
- [ ] Parser tests for command mode passing

### US-004: Three-Mode Parser — Infix Mode (`()`)

**Description:** As a compiler developer, I need the parser to handle `()` as infix mode — operators parsed infix with no precedence, left-to-right.

**Acceptance Criteria:**
- [ ] `($x + 3)` parses as `[+ $x 3]`
- [ ] `($x + 3 * 2)` parses as `[* [+ $x 3] 2]` (no precedence, left-to-right)
- [ ] `(1 + (2 * 3))` parses as `[+ 1 [* 2 3]]` (explicit grouping with nested parens)
- [ ] Unary prefix operators bind tighter: `(- $x + $y)` → `[+ [neg $x] $y]`
- [ ] All arithmetic operators work: `+`, `-`, `*`, `/`, `%`
- [ ] All comparison operators work: `==`, `<`, `>`, `<=`, `>=`
- [ ] Logical operators: `($a && $b)` → `[and $a $b]`, `($a || $b)` → `[or $a $b]`, `(~ $a)` → `[not $a]`
- [ ] `|` in `()` mode is bitwise OR (not pipe) — lexed but compilation deferred until bitwise ops exist
- [ ] `$[expr]` and `$(expr)` in strings: `"total: $($price * $qty)"` works
- [ ] Variable references work in infix: `($x + $y)`
- [ ] Nested `[]` calls in infix: `($x + [vec-len $v])`
- [ ] Parser tests for infix mode passing

### US-005: Pipe Threading

**Description:** As a compiler developer, I need `|` in command mode to thread the result of the left command as the first argument to the right command.

**Acceptance Criteria:**
- [ ] `foo $a | bar $b` desugars to `[bar [foo $a] $b]` (first-arg threading)
- [ ] Multi-stage pipes: `a | b | c` desugars to `[c [b [a]]]`
- [ ] Pipes with arguments: `+ 1 2 | * 3` desugars to `[* [+ 1 2] 3]`
- [ ] Pipes across line continuation: `foo $a \\\n  | bar $b` works
- [ ] Pipes compose with existing builtins: `vec 1 2 3 | vec-len` → `[vec-len [vec 1 2 3]]`
- [ ] Pipe short-circuits on error values (existing error propagation handles this via `OP_CHECK_ERROR`)
- [ ] Compiler tests for pipe threading passing

### US-006: Binding Operators (`=`, `:`, `::`)

**Description:** As a compiler developer, I need the binding operators to work as sugar for `def`, `mut`, and `set` in command mode.

**Acceptance Criteria:**
- [ ] `x = 5` desugars to `[def x 5]`
- [ ] `i64 x = 5` desugars to `[def i64 x 5]` (typed binding)
- [ ] `x : 0` desugars to `[mut x 0]`
- [ ] `i64 x : 0` desugars to `[mut i64 x 0]` (typed mutable binding)
- [ ] `x :: ($x + 1)` desugars to `[set x ($x + 1)]`
- [ ] The command forms `def x 5`, `mut x 0`, `set x ($x + 1)` still work
- [ ] Binding operators only valid in command mode (`{}` and top-level), not in `[]` or `()`
- [ ] Compiler tests for binding operators passing

### US-007: Arrow Field Access (`->`)

**Description:** As a compiler developer, I need `->` to work as field access on variables, replacing the current `.` command syntax.

**Acceptance Criteria:**
- [ ] `$point->x` parses as field access and compiles to `OP_STRUCT_GET`
- [ ] Works in all three modes: `$point->x` in `{}`, `($point->x + 1)` in `()`, `[foo $point->x]` in `[]`
- [ ] Chained access: `$user->address->city` compiles to nested `OP_STRUCT_GET`
- [ ] Field access on expression results: `[get-point]->x` — access field on return value of `[get-point]`
- [ ] The existing `.` command (`[. $struct field]`) is removed
- [ ] Compiler tests for field access passing

### US-008: Proc Syntax Update

**Description:** As a compiler developer, I need `proc` to use the new `{params} {body}` syntax with type-before-name parameter declarations.

**Acceptance Criteria:**
- [ ] `proc foo {x, y} {+ $x $y}` — braces for both params and body
- [ ] `proc i64 add {i64 a, i64 b} {+ $a $b}` — return type before name, typed params
- [ ] `proc greet {} {print "hello"}` — zero-arg procs
- [ ] `proc add {a, b} {+ $a $b}` — untyped params (gradual typing)
- [ ] Params separated by commas within `{}`
- [ ] Type-before-name in params: `{i64 a, str b}` not `{a :i64, b :str}`
- [ ] Closures/upvalue capture still works with new syntax
- [ ] Tail-call optimization still applies
- [ ] Old proc syntax `[proc [x y] [+ $x $y]]` no longer parses
- [ ] Compiler tests for proc syntax passing

### US-009: Struct Syntax Update

**Description:** As a compiler developer, I need `struct` (replacing `defstruct`) to use the new type-before-name field syntax.

**Acceptance Criteria:**
- [ ] `struct Point {i32 x, i32 y}` — keyword is `struct` not `defstruct`
- [ ] Fields use type-before-name: `{i32 x, i32 y}` not `[x :i32, y :i32]`
- [ ] Struct construction: `[Point 1 2]` (unchanged — type name in head position)
- [ ] Field access via `->`: `$point->x` (see US-007)
- [ ] All existing struct runtime behavior preserved (C-ABI layout, typed fields, GC integration)
- [ ] Old `defstruct` keyword no longer recognized
- [ ] Compiler tests for struct syntax passing

### US-010: Control Flow Syntax Update

**Description:** As a compiler developer, I need `if`, `while`, and the new `for` to use the redesigned syntax.

**Acceptance Criteria:**
- [ ] `if $cond { body }` — condition then block
- [ ] `if $cond { body } else { body }` — else clause
- [ ] `if $cond { body } elif $cond2 { body } else { body }` — elif chains
- [ ] `if` with infix condition: `if ($n > 0) { "positive" } else { "non-positive" }`
- [ ] `if` is an expression — returns value of taken branch
- [ ] `while $cond { body }` — while loop with block
- [ ] `while ($i < 10) { body }` — while with infix condition
- [ ] `for $items { print $it }` — iteration with implicit `$it` binding
- [ ] `for $items item { print $item }` — iteration with explicit binding
- [ ] `for $items [\\ print $it]` — iteration with lambda callback (HOF form)
- [ ] `each` keyword no longer recognized — fully replaced by `for`
- [ ] `try { risky } err { handle $err }` — try/catch block form
- [ ] Old `[if cond then else]` syntax no longer parses
- [ ] Compiler tests for control flow passing

### US-011: Builtin Renames

**Description:** As a compiler developer, I need to rename builtins to match SYNTAX_REDESIGN.md conventions.

**Acceptance Criteria:**
- [ ] `each` → `for` (iteration — see US-010)
- [ ] `defstruct` → `struct` (struct definition — see US-009)
- [ ] `set!` → `set` (variable reassignment — drop the bang)
- [ ] `reset!` → `reset` (atom/box reset — drop the bang)
- [ ] `swap!` → `swap` (atom/box swap — drop the bang)
- [ ] Old names (`each`, `defstruct`, `set!`, `reset!`, `swap!`) produce compile errors
- [ ] All opcodes remain unchanged — this is a compiler-level rename only
- [ ] Compiler tests for renamed builtins passing

### US-012: Lambda Shorthand

**Description:** As a compiler developer, I need the `\` lambda shorthand to work in the new syntax.

**Acceptance Criteria:**
- [ ] `[\ + $it 2]` parses as `[proc {it} {+ $it 2}]` — lambda with implicit `$it` param
- [ ] Works in pipe chains: `$items | filter [\ > $it 2]`
- [ ] Works as callback arg: `for $items [\ print $it]`
- [ ] `$it` is automatically bound as the parameter name
- [ ] `\` at end of line in command mode is line continuation (not lambda) — context-dependent
- [ ] Compiler tests for lambda shorthand passing

### US-013: Line Continuation and Separators

**Description:** As a compiler developer, I need newline, `,`, `;` as command separators in `{}` and top-level, and `\` for line continuation.

**Acceptance Criteria:**
- [ ] Newline separates commands at top-level and in `{}`
- [ ] `;` separates commands at top-level and in `{}`
- [ ] `,` separates commands at top-level and in `{}`
- [ ] `\` at end of line continues the command on the next line
- [ ] Inside `[]`, `()`, and `{}`, newlines within the delimiters do NOT terminate — balanced delimiters handle multi-line naturally
- [ ] Blank lines (multiple newlines) don't produce empty commands
- [ ] Compiler tests for separators and continuation passing

### US-014: Rewrite All Tests

**Description:** As a compiler developer, I need all existing tests rewritten to use the new syntax so the test suite validates the redesigned language.

**Acceptance Criteria:**
- [ ] `test_lexer.c` — rewritten for new token types
- [ ] `test_parser.c` — rewritten for three-mode parsing
- [ ] `test_compiler.c` — rewritten for new syntax (proc, struct, if, for, pipes, binding ops)
- [ ] `test_vm.c` — rewritten for new syntax
- [ ] `test_collections.c` — rewritten (`each`→`for`, pipe syntax)
- [ ] `test_string.c` — rewritten (add `$(...)` interpolation tests)
- [ ] `test_m4.c` through `test_m13.c` — rewritten for new syntax
- [ ] `test_embed.c` — rewritten where syntax appears in test programs
- [ ] `test_e2e_*.c` — rewritten for new syntax
- [ ] `test_integration.c` — rewritten for new syntax
- [ ] All ~1,200 tests passing
- [ ] No old-syntax test programs remain

## Functional Requirements

- FR-1: The lexer must emit distinct token types for all new syntax elements (`|`, `->`, `\\`, `=`, `:`, `::`, `&&`, `||`, `~`, and all keywords)
- FR-2: The parser must operate in three modes determined by the enclosing delimiter: `[]` juxtaposition, `{}` command, `()` infix
- FR-3: Top-level code must parse in command mode (no enclosing brackets needed)
- FR-4: `()` infix mode must evaluate all binary operators left-to-right with no precedence
- FR-5: Unary prefix operators in `()` must bind tighter than binary operators
- FR-6: `|` in command mode must thread the left result as the first argument to the right command
- FR-7: `=`, `:`, `::` must desugar to `def`, `mut`, `set` respectively at the parser or compiler level
- FR-8: `$var->field` must compile to struct field access opcodes
- FR-9: `proc` must accept `{params} {body}` with type-before-name params separated by commas
- FR-10: `struct` must accept `{type name, type name}` field syntax
- FR-11: `if`/`elif`/`else` must work with block bodies and be expression-valued
- FR-12: `for` must replace `each` for all iteration patterns
- FR-13: `\` in `[]` must create a lambda with implicit `$it` parameter
- FR-14: All three delimiter modes must nest freely: `{}` can contain `[]` and `()`, etc.
- FR-15: Error recovery in the parser must work across all three modes (sync to matching delimiter)
- FR-16: `&&`, `||`, `~` must work as logical operators in `()` infix mode

## Non-Goals

- No new runtime features (streams, `$ctx`, shell interop `!cmd`, concurrency syntax changes)
- No new opcodes — all new syntax compiles to existing opcodes
- No pattern matching / destructuring (future PRD)
- No `match`/`case` (future PRD)
- No splat/spread `..` semantics (tokens lexed but not compiled)
- No alias system
- No macro system
- No operator overloading / user-definable operators
- No regular expression literals
- No `break`/`continue`/`return` control flow (requires new opcodes — future PRD)
- No bitwise operators (future PRD — `|` in `()` is reserved but not implemented)
- No changes to the VM, GC, value representation, or runtime

## Technical Considerations

- The compiler-vm-refactor branch must be merged to main before starting
- Unity build: any new source files must be `#include`d in `src/jacl.c`
- All AST nodes are arena-allocated — new node types follow the same pattern
- The compiler dispatches on command name strings — renames only change the string comparisons
- Pipes are a parser/compiler feature — they desugar to nested `AST_COMMAND` nodes before the compiler sees them (or the compiler handles the pipe AST node directly)
- `->` field access can be handled in the lexer (as part of `$var` token parsing) or as a post-parse transformation
- Infix mode needs a new parser function that handles the left-to-right binary operator loop
- The `\` character serves double duty: lambda shorthand in `[]` and line continuation in `{}` — the lexer/parser must disambiguate by context (end of line vs. inside brackets)
- Existing arity checking must be updated for new syntax forms (e.g., `for` has different arities than `each`)

## Success Metrics

- All ~1,200 existing tests pass with new syntax
- No regressions in any milestone (M0-M13) functionality
- Parser error messages are clear and actionable for all three modes
- Programs written in new syntax are visually cleaner and shorter than equivalent old syntax

## Open Questions

- Should `->` be parsed as part of the `$var` token in the lexer, or as a separate binary operator in the parser?
- Should pipes desugar in the parser (producing nested `AST_COMMAND`) or in the compiler (new `AST_PIPE` node)?
- Should binding operators desugar in the parser or compiler?
- What is the exact `for` arity/dispatch for C-style `for {init; cond; step} { body }` — is that in scope or deferred?
