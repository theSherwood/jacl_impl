# PRD: Operators as Macros

## Introduction

Refactor JACL's operator handling so that all operators (`=`, `:`, `::`, `|`, `+`, `-`, `*`, `/`, etc.) are implemented as macros rather than having special-case handling in the parser. Currently the parser has hardcoded logic for binding operators, pipe, and infix arithmetic. The design doc (SYNTAX_REDESIGN.md) specifies that "symbolic operators in `{}` command mode and `()` infix mode are macros — they receive unevaluated AST and produce transformed AST. The parser treats all operators uniformly; semantics are defined by the macro system."

This aligns with Lisp philosophy: minimize special forms in the parser, maximize what can be expressed as macros. The `\` lambda shorthand is already implemented this way — operators should follow the same pattern.

## Goals

- Remove special-case operator handling from the parser
- Parser recognizes operator tokens and produces `AST_COMMAND` nodes with the operator as head
- Implement all operators as prelude macros defined in pure JACL
- Support user-defined operators using the same mechanism as builtins
- Maintain backward compatibility — existing code continues to work
- Macros are mode-agnostic; they transform AST uniformly regardless of parse context

## User Stories

### US-001: Parser emits uniform AST for operators in command mode
**Description:** As the compiler, I need the parser to emit `AST_COMMAND` nodes with operators as heads in `{}` command mode, so that macro expansion can handle them uniformly.

**Acceptance Criteria:**
- [ ] `foo a | bar b` parses to `[| [foo a] [bar b]]` (AST_COMMAND with head `|`, two command children)
- [ ] `x = 3` parses to `[= x 3]` (AST_COMMAND with head `=`, var-ref and literal children)
- [ ] `i64 x = 3` parses to `[= [i64 x] 3]` (type annotation groups with the pattern)
- [ ] `x : 0` parses to `[: x 0]`
- [ ] `x :: ($x + 1)` parses to `[:: x ...]` (right side is whatever expression follows)
- [ ] `a | b | c` parses left-to-right: `[| [| a b] c]`
- [ ] Existing special-case handling for `=`, `:`, `::`, `|` in command mode is removed
- [ ] `build.sh` passes
- [ ] Test: parse `x = 3`, verify AST has head `=`, two children
- [ ] Test: parse `i64 x = 3`, verify AST has head `=`, first child is `[i64 x]`
- [ ] Test: parse `a | b | c`, verify left-associative structure
- [ ] Test: parse `x : 0` and `y :: 1`, verify heads are `:` and `::`

### US-002: Parser emits uniform AST for operators in infix mode
**Description:** As the compiler, I need the parser to emit `AST_COMMAND` nodes with operators as heads in `()` infix mode, so that macro expansion can handle them uniformly.

**Acceptance Criteria:**
- [ ] `(1 + 2)` parses to `[+ 1 2]`
- [ ] `($x + $y * $z)` parses left-to-right to `[* [+ $x $y] $z]` (no precedence, Smalltalk-style)
- [ ] `(1 + 2 + 3)` parses to `[+ [+ 1 2] 3]`
- [ ] Unary prefix operators work: `(- $x)` parses to `[- $x]`, `(not $a)` parses to `[not $a]`
- [ ] Unary binds tighter: `(- $x + $y)` parses to `[+ [- $x] $y]`
- [ ] Parenthesized grouping works: `(1 + (2 * 3))` parses to `[+ 1 [* 2 3]]`
- [ ] The hardcoded `parser__parse_infix` operator handling is replaced with uniform operator-as-head parsing
- [ ] `build.sh` passes
- [ ] Test: parse `(1 + 2)`, verify AST is `[+ 1 2]`
- [ ] Test: parse `(1 + 2 * 3)`, verify left-associative `[* [+ 1 2] 3]`
- [ ] Test: parse `(- $x)`, verify unary `[- $x]`
- [ ] Test: parse `(- $x + $y)`, verify unary binds tighter `[+ [- $x] $y]`
- [ ] Test: parse `(1 + (2 * 3))`, verify explicit grouping `[+ 1 [* 2 3]]`
- [ ] Test: parse `($a == $b)`, verify `[== $a $b]`

### US-003: Create JACL prelude file infrastructure
**Description:** As the runtime, I need to load a prelude file containing macro definitions at startup, so that operator macros are available before user code runs.

**Acceptance Criteria:**
- [ ] New file `src/prelude.jacl` with operator macro definitions
- [ ] Build step generates `src/prelude_source.h` containing prelude as C string literal
- [ ] Prelude is parsed and macro-expanded before any user code
- [ ] Macros defined in prelude are available to all subsequent compilation
- [ ] Prelude loading errors produce clear error messages with file:line
- [ ] The existing C-defined `\` macro in `syntax.c` is moved to `prelude.jacl`
- [ ] `build.sh` passes
- [ ] Test: define a simple macro in prelude, verify it expands in user code
- [ ] Test: prelude parse error produces error message mentioning "prelude"
- [ ] Test: prelude macro can use `syntax-quote`, `~`, `gensym`
- [ ] Test: user code cannot redefine prelude macros (or can? — decide)

### US-004: Implement binding operator `=` as macro
**Description:** As a JACL programmer, I want `x = 3` to be sugar for `[def x 3]`, implemented as a macro.

**Acceptance Criteria:**
- [ ] `prelude.jacl` defines: `defmacro = {lhs, rhs} { syntax-quote [def ~lhs ~rhs] }`
- [ ] `x = 3` expands to `[def x 3]` and works
- [ ] `i64 x = 3` expands to `[def i64 x 3]` (type flows through)
- [ ] `[a, b] = $pair` expands to `[def [a, b] $pair]` (destructuring works)
- [ ] `{x, y} = $point` expands to `[def {x, y} $point]` (named destructuring works)
- [ ] Hygiene: macro-internal names don't leak
- [ ] `build.sh` passes
- [ ] Test: `x = 42` then `$x` returns 42
- [ ] Test: `i64 x = 42` then `$x` returns 42 with correct type
- [ ] Test: `[a, b] = [vec 1 2]` then `$a` is 1, `$b` is 2
- [ ] Test: `{x, y} = $point` destructures struct fields
- [ ] Test: `x = 1; x = 2` is compile error (redefinition in same scope)

### US-005: Implement binding operator `:` as macro
**Description:** As a JACL programmer, I want `x : 0` to be sugar for `[mut x 0]`, implemented as a macro.

**Acceptance Criteria:**
- [ ] `prelude.jacl` defines: `defmacro : {lhs, rhs} { syntax-quote [mut ~lhs ~rhs] }`
- [ ] `x : 0` expands to `[mut x 0]` and works
- [ ] `i64 x : 0` expands to `[mut i64 x 0]` (type flows through)
- [ ] `build.sh` passes
- [ ] Test: `x : 0` then `$x` returns 0
- [ ] Test: `x : 0; x :: 5` then `$x` returns 5 (mutable, can reassign)
- [ ] Test: `i64 x : 0` then `$x` has correct type
- [ ] Test: `x : 0; x : 1` is compile error (redefinition)

### US-006: Implement reassignment operator `::` as macro
**Description:** As a JACL programmer, I want `x :: 5` to be sugar for `[set x 5]`, implemented as a macro.

**Acceptance Criteria:**
- [ ] `prelude.jacl` defines: `defmacro :: {lhs, rhs} { syntax-quote [set ~lhs ~rhs] }`
- [ ] `x :: 5` expands to `[set x 5]` and works
- [ ] `x :: ($x + 1)` works (compound expression on right side)
- [ ] `build.sh` passes
- [ ] Test: `x : 0; x :: 5` then `$x` returns 5
- [ ] Test: `x : 0; x :: ($x + 1)` then `$x` returns 1
- [ ] Test: `x = 0; x :: 5` is compile error (immutable binding)
- [ ] Test: `y :: 5` where `y` undefined is compile error

### US-007: Implement pipe operator `|` as macro
**Description:** As a JACL programmer, I want `foo a | bar b` to thread the result of `foo a` as the first argument to `bar b`, implemented as a macro.

**Acceptance Criteria:**
- [ ] `prelude.jacl` defines pipe macro that transforms `[| left right]` → `[right-head left right-args...]`
- [ ] `foo a | bar b` expands to `[bar [foo a] b]`
- [ ] `a | b | c` chains correctly: `[c [b [a]]]` (left-to-right associativity)
- [ ] Pipe with no args on right: `foo | bar` expands to `[bar [foo]]`
- [ ] Macro produces clear error if right operand is not a command (e.g., `foo | 3`)
- [ ] `build.sh` passes
- [ ] Test: `[+ 1 2] | [* 2]` returns 6 (3 * 2)
- [ ] Test: `[vec 1 2 3] | vec-len` returns 3
- [ ] Test: 3-stage pipe `a | b | c` threads correctly
- [ ] Test: `[+ 1 2] | 3` produces compile error with clear message
- [ ] Test: pipe works with procs: `def f [proc {x} {* $x 2}]; 5 | $f` returns 10

### US-008: Implement arithmetic operators as macros
**Description:** As a JACL programmer, I want `($x + $y)` to work via macro expansion, not parser special-casing.

**Acceptance Criteria:**
- [ ] `prelude.jacl` defines macros for `+`, `-`, `*`, `/`, `%`
- [ ] Operators expand to internal builtins: `defmacro + {a, b} { syntax-quote [__add ~a ~b] }`
- [ ] Internal builtins `__add`, `__sub`, `__mul`, `__div`, `__mod` are recognized by compiler
- [ ] `(1 + 2)` evaluates to `3`
- [ ] `($x + $y * $z)` evaluates correctly (left-to-right, no precedence)
- [ ] Unary `-`: `(- $x)` negates (expands to `[__neg ~a]`)
- [ ] `build.sh` passes
- [ ] Test: `(1 + 2)` returns 3
- [ ] Test: `(10 - 3)` returns 7
- [ ] Test: `(4 * 5)` returns 20
- [ ] Test: `(10 / 2)` returns 5
- [ ] Test: `(10 % 3)` returns 1
- [ ] Test: `(1 + 2 * 3)` returns 9 (left-to-right: (1+2)*3, not 1+(2*3))
- [ ] Test: `(1 + (2 * 3))` returns 7 (explicit grouping)
- [ ] Test: `(- 5)` returns -5 (unary negation)
- [ ] Test: `(- 3 + 5)` returns 2 (unary binds tighter: (-3)+5)

### US-009: Implement comparison operators as macros
**Description:** As a JACL programmer, I want comparison operators to work via macro expansion.

**Acceptance Criteria:**
- [ ] `prelude.jacl` defines macros for `==`, `!=`, `<`, `>`, `<=`, `>=`
- [ ] `($x == $y)` expands to `[__eq ~x ~y]`
- [ ] `($x != $y)` expands to `[__ne ~x ~y]`
- [ ] `($x < $y)` expands to `[__lt ~x ~y]`
- [ ] `($x > $y)` expands to `[__gt ~x ~y]`
- [ ] `($x <= $y)` expands to `[__le ~x ~y]`
- [ ] `($x >= $y)` expands to `[__ge ~x ~y]`
- [ ] All comparisons return `$true` or `$false`
- [ ] `build.sh` passes
- [ ] Test: `(1 == 1)` returns `$true`
- [ ] Test: `(1 == 2)` returns `$false`
- [ ] Test: `(1 != 2)` returns `$true`
- [ ] Test: `(1 < 2)` returns `$true`
- [ ] Test: `(2 > 1)` returns `$true`
- [ ] Test: `(1 <= 1)` returns `$true`
- [ ] Test: `(1 >= 2)` returns `$false`
- [ ] Test: `("a" == "a")` returns `$true` (string comparison)
- [ ] Test: comparison in `if`: `if (1 < 2) { "yes" }` returns "yes"

### US-010: Implement logical commands with short-circuit behavior
**Description:** As a JACL programmer, I want `and`, `or`, `not` to be commands (not operators) with proper short-circuit semantics.

**Acceptance Criteria:**
- [ ] `and`, `or`, `not` are regular commands, not operators (no symbol characters)
- [ ] `[and $a $b]` short-circuits correctly (doesn't evaluate `$b` if `$a` is false)
- [ ] `[or $a $b]` short-circuits correctly (doesn't evaluate `$b` if `$a` is true)
- [ ] `[not $a]` negates
- [ ] These are implemented as macros for short-circuit: `and` → `[if ~a ~b $false]`
- [ ] Work in infix mode too: `($a and $b)` since bare words are valid in `()`
- [ ] `build.sh` passes
- [ ] Test: `[and $true $true]` returns `$true`
- [ ] Test: `[and $false $true]` returns `$false`
- [ ] Test: `[or $false $true]` returns `$true`
- [ ] Test: `[or $true $false]` returns `$true`
- [ ] Test: `[not $true]` returns `$false`
- [ ] Test: `[not $false]` returns `$true`
- [ ] Test short-circuit: `[and $false [error "should not eval"]]` returns `$false` (no error)
- [ ] Test short-circuit: `[or $true [error "should not eval"]]` returns `$true` (no error)
- [ ] Test infix: `($true and $false)` returns `$false`

### US-011: Migrate `\` lambda macro to prelude.jacl
**Description:** As the maintainer, I want the existing `\` lambda shorthand to be defined in `prelude.jacl` alongside other operator macros, not hardcoded in C.

**Acceptance Criteria:**
- [ ] The `\` macro definition is moved from `syntax.c` to `prelude.jacl`
- [ ] `[\ + $it 1]` continues to work, expanding to `proc {it} { + $it 1 }`
- [ ] The C code that injects the `\` macro is removed
- [ ] `build.sh` passes
- [ ] All existing tests using `\` pass
- [ ] Test: `[\ + $it 1]` applied to 5 returns 6
- [ ] Test: `[vec 1 2 3] | filter [\ > $it 1]` returns `[vec 2 3]`
- [ ] Test: `[\ * $it $it]` applied to 4 returns 16
- [ ] Test: nested lambdas work (if supported)

### US-012: User-defined operators
**Description:** As a JACL programmer, I want to define my own operators using the same `defmacro` mechanism as the builtins.

**Acceptance Criteria:**
- [ ] Users can define `defmacro <> {a, b} { ... }` for a custom operator
- [ ] Custom operators work in both `{}` and `()` contexts (parser recognizes them as operators)
- [ ] Custom operators compose with pipes and other syntax
- [ ] Document which characters/sequences are valid operator names
- [ ] `build.sh` passes
- [ ] Test: define `defmacro <> {a, b} { syntax-quote [and [!= ~a ~b] ...] }` (not-equal with extra behavior)
- [ ] Test: custom operator `<>` works in infix: `(1 <> 2)` returns expected result
- [ ] Test: define `defmacro |> {a, b} { ... }` as alternative pipe, verify it works
- [ ] Test: define `defmacro ?? {a, b} { ... }` as null-coalescing, verify it works
- [ ] Test: operator with max length (8 chars) works
- [ ] Test: operator with invalid chars (e.g., `~=`) produces clear error at definition time

### US-013: Remove legacy operator handling from parser
**Description:** As the maintainer, I want all special-case operator code removed from the parser once macros handle everything.

**Acceptance Criteria:**
- [ ] No operator-specific logic remains in `parser.c` beyond tokenization
- [ ] `parser__parse_infix` is simplified to just parse operands and operators uniformly
- [ ] Any operator desugaring logic (e.g., `=` → `def`) is deleted from parser
- [ ] Comments document that operators are handled by macro expansion
- [ ] `build.sh` passes
- [ ] All existing tests pass (full backward compatibility)
- [ ] Test: grep parser.c for `TOKEN_EQUALS` shows only tokenization, no semantic handling
- [ ] Test: grep parser.c for `def`, `mut`, `set` shows no desugaring from operators
- [ ] Code review: parser line count decreased vs before this PRD

### US-014: Integration tests for operator macros
**Description:** As the maintainer, I want comprehensive tests proving operators-as-macros work correctly.

**Acceptance Criteria:**
- [ ] Test file `test/test_operator_macros.c` (or section in existing test file)
- [ ] `build.sh` passes
- [ ] Test: binding operators with all pattern types (simple, typed, destructuring)
  - [ ] `x = 1`, `i64 x = 1`, `[a, b] = $v`, `{x, y} = $p`
- [ ] Test: mutable binding and reassignment
  - [ ] `x : 0; x :: 1; x :: ($x + 1)`
- [ ] Test: pipe chains of 3+ stages
  - [ ] `1 | [+ 2] | [* 3]` returns 9
- [ ] Test: arithmetic in infix mode with left-to-right associativity
  - [ ] `(1 + 2 * 3)` returns 9, `(1 + (2 * 3))` returns 7
- [ ] Test: all comparison operators return correct boolean
- [ ] Test: logical operators with short-circuit verification
  - [ ] `[and $false [error "boom"]]` returns `$false` without error
- [ ] Test: mixed operators in complex expressions
  - [ ] `x = (1 + 2); y = ($x * 3); [and ($y > 5) ($y < 20)]`
- [ ] Test: user-defined operator end-to-end
- [ ] Test: error messages when operators receive invalid operands
  - [ ] `1 | 2` produces "pipe expects command" error
- [ ] Test: operators work inside proc bodies, if branches, loops
- [ ] Test: operators work with macros (operator in macro output)

## Functional Requirements

- FR-1: Parser produces `AST_COMMAND` nodes with operator symbols as heads, not special AST node types
- FR-2: Parser does not perform any operator-specific desugaring; all desugaring happens in macro expansion
- FR-3: Operators associate left-to-right with no precedence in infix mode (Smalltalk-style)
- FR-4: Unary prefix operators bind tighter than binary operators
- FR-5: Prelude file `prelude.jacl` is loaded and macro-expanded before any user code
- FR-6: All standard operators (`=`, `:`, `::`, `|`, `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`) are defined as macros in prelude; `and`, `or`, `not` are command macros (not operators)
- FR-7: Macros are mode-agnostic; they transform AST uniformly regardless of whether the operator appeared in `{}` or `()` context
- FR-8: User-defined operators use the same mechanism as builtin operators
- FR-9: Operator macros can use `syntax-kind`, `syntax-head`, `syntax-args` to introspect operands and produce clear error messages

## Non-Goals

- **Operator precedence:** No precedence levels. Use explicit parentheses or prefix notation.
- **Mode-specific operator overloading:** Same operator symbol always means the same thing (no `|` as pipe in `{}` but bitwise-or in `()`).
- **Infix operator declaration syntax:** No `infixl 6 +` style declarations. Operators are just macros.
- **Custom operator precedence:** Users cannot define precedence for their operators.
- **Right-associative operators:** All operators are left-associative. Use explicit grouping for right-associativity.

## Technical Considerations

### Operator vs builtin name collision

Operators expand to double-underscore internal builtins to avoid infinite recursion:

```
defmacro + {a, b} { syntax-quote [__add ~a ~b] }
defmacro - {a, b} { syntax-quote [__sub ~a ~b] }
defmacro * {a, b} { syntax-quote [__mul ~a ~b] }
; etc.
```

The compiler recognizes `__add`, `__sub`, `__mul`, `__div`, `__mod`, `__neg`, `__eq`, `__ne`, `__lt`, `__gt`, `__le`, `__ge` as primitives that emit direct opcodes. User code cannot define macros with `__` prefix (reserved by convention).

### Type annotations with binding operators

`i64 x = 3` needs to parse as `[= [i64 x] 3]` so the macro receives the type+name as a single unit. This requires the parser to recognize `type name =` patterns and group appropriately, OR the parser treats `i64` as a unary prefix operator/command that wraps `x`.

Simplest: parser sees `i64 x = 3` and parses as `[= [i64 x] 3]`:
- `i64` starts a type annotation
- `x` is the name being annotated
- `=` is the operator
- `3` is the value

The `=` macro then expands `[= [i64 x] 3]` → `[def [i64 x] 3]` → existing `def` handles typed binding.

### Prelude loading

The prelude is embedded in the binary for simplicity and to ensure version consistency:
1. Prelude source lives in `src/prelude.jacl` (editable JACL file)
2. Build step converts it to a C string literal in `src/prelude_source.h`
3. At startup, the embedded string is parsed and macro-expanded before any user code
4. No external file dependencies, single-binary deployment

### Error messages

Operator macros should produce helpful errors. For example, the pipe macro should check that its right operand is a command:

```
defmacro | {left, right} {
  if [!= [syntax-kind $right] "command"] {
    syntax-error "pipe expects a command on the right, got " [syntax-kind $right] $right
  }
  ; ... actual expansion
}
```

### Valid operator characters

Operators are pure runs of symbol characters (no letters):
- Valid: `+ - * / % < > = ! & | ? @ : . \`
- Reserved: `$` `#` `,` `;` `( ) [ ] { }` `"` `'` `~` `^`
- `~` and `^` are reserved for macro quoting syntax
- Maximum length: 8 characters

Examples of valid user-defined operators: `<>`, `|>`, `<|`, `??`, `::=`, `<=>`, `...`

Word-like operations (`and`, `or`, `not`, `in`, `is`) are regular commands, not operators. They work in infix mode because `()` allows bare words, but they're not in the operator character set.

## Success Metrics

- All existing tests pass with no changes to test code (full backward compatibility)
- Parser line count decreases (removed special-case logic)
- `prelude.jacl` is readable and serves as documentation for operator semantics
- User can define a custom operator in 3 lines of macro code
- No performance regression in compilation (macro expansion is fast)

## Resolved Design Decisions

1. **Prelude location:** Embedded in binary. Prelude source lives in `src/prelude.jacl`, build step converts to C string literal in `src/prelude_source.h`. Ensures prelude and compiler versions stay in sync.

2. **Internal builtin names:** Double-underscore prefix convention: `__add`, `__sub`, `__mul`, `__div`, `__mod`, `__eq`, `__lt`, `__gt`, etc. Clear "internal" signal, matches C/Python convention.

3. **Operator character set:**
   - Valid operator characters: `+ - * / % < > = ! & | ? @ : . \`
   - Reserved (cannot be in operators): `$` `#` `,` `;` `( ) [ ] { }` `"` `'` `~` `^`
   - `~` and `^` reserved for macro quoting (unquote and intentional introduction)
   - Operators are pure symbol runs only — no letters allowed
   - Word-like operations (`and`, `or`, `not`) are regular commands, not operators
   - Maximum operator length: 8 characters

4. **Compound assignment:** Deferred to future work. Not in this PRD. Users write `x :: ($x + 1)` explicitly. Can add `+=` etc. later as prelude macros.

5. **Boolean/nil literals:** Keep current behavior. All named values use `$` sigil: `$true`, `$false`, `$nil`. Only numbers and strings are bare literals. Consistent with variable reference syntax.
