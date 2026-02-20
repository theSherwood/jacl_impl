# PRD: Milestone 6 — Line-Based Syntax Sugar

## Introduction

This milestone adds line-based syntax sugar to JACL, allowing users to omit the `[]` brackets
around top-level and block-level commands. A bare line like `puts hello` desugars to
`[puts hello]`, and `exit` alone desugars to `[exit]`. This makes JACL feel more like a
command/scripting language while preserving the bracket syntax as the canonical form.

The existing parser already supports much of this — `parser__parse_bare_command` handles
multi-word lines, and the lexer already emits `TOKEN_NEWLINE` and `TOKEN_SEMICOLON`. The main
gaps are: (1) comma as a command separator, (2) backslash line continuation, and (3) bare
single-word lines (and `$var` lines) should be treated as zero-arg command calls rather than
literal values.

After this milestone, JACL programs can be written in a natural line-oriented style:
```
def greeting "hello"
puts $greeting
def x 1; def y 2
```

## Goals

- A bare word alone on a line desugars to a zero-arg call: `exit` → `[exit]`
- A `$var` at the start of a line desugars to a call: `$fn arg` → `[$fn arg]`
- Comma (`,`) works as a command separator alongside newlines and semicolons
- Backslash (`\`) at end of line continues the command on the next line
- Unclosed `[` or `{` automatically continues across lines (already works)
- Explicit `[cmd ...]` still works everywhere — sugar, not replacement
- Works at top-level and inside `{ }` blocks identically
- All existing bracket-based code continues to parse and produce identical AST
- REPL is acknowledged as a future goal but not part of this milestone

## Design Decisions

- **Bare word = zero-arg call:** Currently `parser__parse_bare_command` returns a bare word as
  `AST_LIT_STRING` when there are no arguments. After this change, a bare word at statement
  position (top-level or inside a block) is always wrapped in `AST_COMMAND` with zero args.
  This means `exit` on its own line produces the same AST as `[exit]`. Inside brackets (as an
  argument), bare words remain string literals — only statement-position words become calls.

- **`$var` as command head:** A line starting with `$var` treats the variable value as the
  command to call. `$fn 1 2` desugars to `[$fn 1 2]`. This already works for multi-arg cases
  via `parser__parse_bare_command`; the change is that `$var` alone on a line also becomes a
  zero-arg call `[$var]`.

- **Comma as separator:** Add `TOKEN_COMMA` to the lexer. The parser treats commas exactly like
  semicolons — they terminate the current command and start a new one. Commas are command
  separators only, not argument separators. `puts 1, puts 2` is two commands; `puts 1, 2` is
  `[puts 1]` followed by `[2]` (a call to the string "2").

- **Backslash continuation:** When the lexer encounters `\` immediately followed by a newline
  (`\n` or `\r\n`), it consumes both and emits neither — the next line is logically part of the
  current line. No token is emitted for the backslash-newline sequence.

- **Unclosed delimiters:** Already handled. When inside `[...]`, the parser reads across
  newlines until `]`. When inside `{...}`, the parser reads bare commands delimited by
  newlines/semicolons until `}`. No changes needed.

- **Statement position vs argument position:** The key distinction is where
  `parser__parse_bare_command` is called (statement position) vs where `parser__parse_expr` is
  called (argument position). Only statement-position parsing wraps single expressions as
  commands. This preserves the existing behavior where `[puts hello]` treats `hello` as a
  string argument, not a command call.

## User Stories

### US-001: Comma token in lexer

**Description:** As the lexer, I need to emit `TOKEN_COMMA` when encountering `,` so the
parser can use it as a command separator.

**Acceptance Criteria:**
- [ ] Add `TOKEN_COMMA` to the `TokenType` enum
- [ ] Lexer emits `TOKEN_COMMA` for `,` characters (same pattern as `TOKEN_SEMICOLON`)
- [ ] Comma inside strings is not tokenized (already handled — strings consume their content)
- [ ] Unit test: `"def x 1, def y 2"` lexes to `[WORD WORD INT COMMA WORD WORD INT EOF]`
- [ ] Unit test: `","` inside a string is `TOKEN_STRING`, not `TOKEN_COMMA`
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`

### US-002: Backslash line continuation in lexer

**Description:** As the lexer, I need to consume `\` followed by a newline as whitespace
so that long commands can span multiple lines.

**Acceptance Criteria:**
- [ ] When `\` is immediately followed by `\n` (or `\r\n`), the lexer consumes both characters
      and emits no token — it continues lexing the next line as part of the same logical line
- [ ] `\` followed by something other than a newline continues to be lexed as an operator
      character (existing behavior — `\` is in `lexer__is_operator_char`)
- [ ] Line counter (`lex->line`) is incremented; column resets to 1
- [ ] Unit test: `"puts \\\nhello"` (backslash-newline) lexes to `[WORD WORD EOF]` with
      `hello` on line 2
- [ ] Unit test: `"puts \\\n  hello"` — leading whitespace on continuation line is skipped
- [ ] Unit test: `"puts \\\r\nhello"` — CRLF continuation works
- [ ] Backslash continuation inside strings is unaffected (strings have their own escape handling)
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`

### US-003: Comma as command separator in parser

**Description:** As the parser, I need to treat `TOKEN_COMMA` as a command terminator (like
newlines and semicolons) so users can write multiple commands on one line separated by commas.

**Acceptance Criteria:**
- [ ] `parser__is_command_end` returns true for `TOKEN_COMMA`
- [ ] Top-level parsing loop skips `TOKEN_COMMA` between commands (alongside newlines and
      semicolons)
- [ ] Block parsing loop skips `TOKEN_COMMA` between commands
- [ ] Integration test: `"def x 1, def y 2, [+ $x $y]"` evaluates to `3`
- [ ] Integration test: `"{ def a 10, def b 20, [+ $a $b] }"` in a block evaluates to `30`
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`

### US-004: Bare words as zero-arg command calls

**Description:** As the parser, I need to treat a bare word alone at statement position as a
zero-arg command call, so that `exit` on its own line desugars to `[exit]`.

**Acceptance Criteria:**
- [ ] In `parser__parse_bare_command`, when the head is a single `AST_LIT_STRING` (bare word)
      with zero arguments, wrap it in an `AST_COMMAND` node with `arg_count = 0`
- [ ] `"exit"` parses to `AST_COMMAND { head: "exit", args: [] }` — same as `"[exit]"`
- [ ] `"puts hello"` still parses to `AST_COMMAND { head: "puts", args: ["hello"] }` — unchanged
- [ ] Bare words inside brackets remain string literals: `[puts exit]` — `exit` is a string arg
- [ ] A bare integer alone on a line (`42`) should NOT become a call — only words and `$var`
- [ ] A bare float alone on a line (`3.14`) should NOT become a call
- [ ] A bare string literal alone (`"hello"`) should NOT become a call
- [ ] A bracketed command alone (`[foo]`) is returned directly — not double-wrapped
- [ ] A block alone (`{ ... }`) is returned directly — not wrapped
- [ ] Unit tests for each of the above cases via AST inspection
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`

### US-005: `$var` lines as command calls

**Description:** As the parser, I need to treat `$var` alone at statement position as a
zero-arg call, so that `$fn` on its own line desugars to `[$fn]`.

**Acceptance Criteria:**
- [ ] In `parser__parse_bare_command`, when the head is `AST_VAR_REF` with zero arguments,
      wrap it in an `AST_COMMAND` node with `arg_count = 0`
- [ ] `"$fn"` parses to `AST_COMMAND { head: AST_VAR_REF("fn"), args: [] }`
- [ ] `"$fn 1 2"` parses to `AST_COMMAND { head: AST_VAR_REF("fn"), args: [1, 2] }` (already works)
- [ ] `$var` inside brackets remains a variable reference: `[puts $x]` is unchanged
- [ ] Integration test: `"proc greet [] { 42 }\ndef f greet\n$f"` evaluates to `42`
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`

### US-006: Update existing tests for new bare-word semantics

**Description:** As a developer, I need to verify that existing tests still pass with the new
bare-word-as-call semantics and update any tests that relied on bare words being string literals
at statement position.

**Acceptance Criteria:**
- [ ] Run the full existing test suite (`test_lexer`, `test_parser`, `test_compiler`, `test_vm`,
      `test_m4`, `test_m5`, `test_integration`)
- [ ] Identify any tests that fail due to the bare-word-as-call change
- [ ] Update failing tests to match the new semantics (bare words at statement position are now
      commands, not literals)
- [ ] All existing tests pass after updates
- [ ] No regressions in bracket-based code behavior

### US-007: End-to-end integration tests for line-based syntax

**Description:** As a developer, I need comprehensive integration tests that verify the full
pipeline (lex → parse → compile → run) for line-based syntax.

**Acceptance Criteria:**
- [ ] Test file: `test/test_m6.c`
- [ ] Test: bare command with args — `"puts hello"` compiles and runs like `"[puts hello]"`
- [ ] Test: bare command no args — `"exit"` produces `AST_COMMAND` (verified via parse)
- [ ] Test: `$var` command — `"proc f [] { 42 }\ndef g f\n$g"` → `42`
- [ ] Test: semicolon separator — `"def x 1; def y 2; [+ $x $y]"` → `3`
- [ ] Test: comma separator — `"def x 10, def y 20, [+ $x $y]"` → `30`
- [ ] Test: mixed separators — `"def a 1; def b 2, def c 3\n[+ [+ $a $b] $c]"` → `6`
- [ ] Test: backslash continuation — `"def x \\\n42\n$x"` → `42`
- [ ] Test: continuation in block — `"{ def x 1, def y 2, [+ $x $y] }"` → `3`
- [ ] Test: nested brackets in line mode — `"puts [+ 1 [* 2 3]]"` works correctly
- [ ] Test: explicit brackets still work — `"[def x 42]\n[+ $x 1]"` → `43`
- [ ] Test: proc definition line-based — `"proc add [a b] { [+ $a $b] }\nadd 3 4"` → `7`
- [ ] Test: multi-line block — `"proc f [] {\n  def x 10\n  def y 20\n  [+ $x $y]\n}\nf"` → `30`
- [ ] All tests pass
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`

## Functional Requirements

- FR-1: The lexer must emit `TOKEN_COMMA` for `,` characters outside of strings
- FR-2: The lexer must consume `\` followed by a newline as whitespace (no token emitted),
  incrementing the line counter
- FR-3: The parser must treat `TOKEN_COMMA` as a command terminator in `parser__is_command_end`
- FR-4: The parser must skip `TOKEN_COMMA` between commands at top-level and in blocks
- FR-5: A bare word at statement position with no arguments must produce `AST_COMMAND` with
  zero args (same as `[word]`)
- FR-6: A `$var` at statement position with no arguments must produce `AST_COMMAND` with
  zero args (same as `[$var]`)
- FR-7: Bare literals (integers, floats, quoted strings) at statement position remain literal
  values — they are NOT wrapped as calls
- FR-8: Inside brackets (argument position), bare words remain `AST_LIT_STRING` — no change
- FR-9: Explicit `[cmd ...]` syntax continues to work identically in all positions
- FR-10: Unclosed `[` or `{` continues across lines without any changes (already works)

## Non-Goals

- REPL / interactive mode (future milestone)
- Argument-position commas (commas are command separators only, not argument separators)
- Any changes to the compiler or VM — this milestone is purely lexer + parser
- Parenthesized grouping for nested calls — brackets remain the only nesting syntax
- Operator syntax or assignment sugar (M15)
- Tab-based or indentation-based scoping

## Technical Considerations

- **Files modified:** `src/lexer.c` (TOKEN_COMMA, backslash continuation), `src/parser.c`
  (comma handling, bare-word-as-call logic). Possibly minor updates to existing test files.
- **New file:** `test/test_m6.c` for integration tests.
- **Backward compatibility:** The only behavioral change is that bare words and `$var` at
  statement position become calls instead of literals. Any existing code that relied on a bare
  word at top level being a string value (unlikely in practice) would change behavior. All
  bracket-based code is unaffected.
- **Parser change is minimal:** The core change in `parser__parse_bare_command` is ~10 lines:
  when `args.count == 0` and head is `AST_LIT_STRING` or `AST_VAR_REF`, wrap in `AST_COMMAND`.
- **Lexer backslash handling:** Must be careful not to break `\` as an operator character. Only
  `\` immediately followed by `\n` or `\r\n` triggers continuation. `\` followed by any other
  character is lexed as part of an operator token (existing behavior).
- **Token count in lexer header comment:** The lexer header mentions "23 token types" — update
  this to reflect the addition of `TOKEN_COMMA`.

## Success Metrics

- All new integration tests in `test_m6.c` pass
- All existing tests pass (with any necessary updates for bare-word semantics)
- Line-based programs produce identical bytecode to their bracket-based equivalents
- Zero compiler or VM changes required

## Open Questions

- Should bare operators at statement position (e.g., `+` alone on a line) be calls or errors?
  Current design: they would become `AST_COMMAND` calls to "+". Likely harmless but unusual.
- Should the `parser__sync_bracket` error recovery be updated to also sync on `TOKEN_COMMA`?
- If a future REPL is added, should it evaluate on every newline or require a blank line / special key?
