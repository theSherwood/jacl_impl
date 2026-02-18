# PRD: JACL Parser & AST (Milestone 2)

## Introduction

Implement a recursive descent parser and AST representation for JACL's Phase 1 bracket syntax. The parser consumes the token array produced by the lexer (Milestone 1) and produces an arena-allocated tree of command invocations — the "lisp under the hood" that all later compilation stages operate on. Every syntactic construct in JACL desugars to command invocations with arguments, making the AST a tree of commands containing literals, variable references, code blocks, and nested commands.

The parser uses panic mode error recovery to report multiple errors per parse, skipping to synchronization points (closing brackets, newlines) when it encounters unexpected tokens. Every AST node carries source positions for error reporting in later compiler stages.

This is Milestone 2 of the JACL language, depending on Milestone 1 (Lexer) and the arena/platform infrastructure modules.

## Goals

- Parse all Phase 1 syntax into a tree of command invocation AST nodes
- Support top-level implicit brackets (bare commands delimited by newlines or semicolons)
- Support nested explicit brackets (`[cmd arg1 [inner] arg2]`)
- Parse code blocks (`{ ... }`) as sequences of commands passed as arguments
- Parse string interpolation into structured AST nodes
- Track source positions (start and end) on every AST node
- Recover from parse errors using panic mode — skip to synchronization points, collect multiple errors
- Provide an AST pretty-printer that produces equivalent bracket-form source (round-trip verification)
- Use arena allocation for all AST nodes (zero manual free, bulk lifetime)

## User Stories

### US-001: Define AST node types
**Description:** As the compiler, I need a well-defined set of AST node types so that I can pattern-match on nodes during bytecode compilation.

**Acceptance Criteria:**
- [ ] AST node type enum covers: command invocation, literal (nil, bool, int, float, string), variable reference, code block, interpolated string, error node
- [ ] Each node type has a corresponding struct with appropriate fields (e.g., command has name + argument list, literal has value + kind, variable has name, code block has list of commands)
- [ ] All AST node structs include start position (line, column, offset) and end position (line, column, offset)
- [ ] Nodes are arena-allocated — no individual free required
- [ ] All tests pass and no memory leaks detected by tracked allocator

### US-002: Parse literal values
**Description:** As the compiler, I need literal AST nodes so that constant values can be loaded.

**Acceptance Criteria:**
- [ ] Integer tokens (`42`, `0xFF`, `0b1010`) produce int literal nodes carrying the `int32_t` value
- [ ] Float tokens (`3.14`) produce float literal nodes carrying the `float` value
- [ ] Bare words produce string literal nodes (bare words are strings in JACL)
- [ ] `$true`, `$false`, `$nil` produce variable reference nodes (booleans and nil are variables, not syntax)
- [ ] Source positions are correct on every literal node
- [ ] All tests pass and no memory leaks

### US-003: Parse variable references
**Description:** As the compiler, I need variable reference AST nodes so that `$var` can compile to variable lookups.

**Acceptance Criteria:**
- [ ] `$identifier` produces a variable reference node with the identifier name (without `$`)
- [ ] Variable references work as command arguments: `[print $x]` produces a command with a var-ref argument
- [ ] Variable references work at top level: `print $x` produces the same command structure
- [ ] Source positions include the `$` prefix in the span
- [ ] All tests pass and no memory leaks

### US-004: Parse command invocations (bracketed)
**Description:** As the compiler, I need command invocation AST nodes so that `[cmd arg1 arg2]` can be compiled to call sequences.

**Acceptance Criteria:**
- [ ] `[cmd arg1 arg2]` produces a command node with "cmd" as the command name and two string literal arguments
- [ ] `[+ 1 2]` produces a command node with "+" as the command (operator) and two int literal arguments
- [ ] Nested commands `[print [+ 1 2]]` produce a command node whose argument is another command node
- [ ] Deeply nested commands (`[a [b [c 1]]]`) produce correct tree structure
- [ ] Empty brackets `[]` produce a parse error
- [ ] Mismatched brackets (`[cmd arg`) produce a parse error with position info
- [ ] Source positions span from opening `[` to closing `]`
- [ ] All tests pass and no memory leaks

### US-005: Parse top-level bare commands
**Description:** As the compiler, I need top-level bare commands parsed with implicit brackets so that users can write `print hello` instead of `[print hello]`.

**Acceptance Criteria:**
- [ ] `print hello world` (at top level) produces a command node equivalent to `[print hello world]`
- [ ] Newlines delimit top-level commands: `print a\nprint b` produces two command nodes
- [ ] Semicolons delimit top-level commands: `print a; print b` produces two command nodes
- [ ] Blank lines (consecutive newlines) are skipped without producing empty commands
- [ ] Nested brackets within bare commands work: `print [+ 1 2]` produces `[print [+ 1 2]]`
- [ ] Variable references within bare commands work: `print $x` produces `[print $x]`
- [ ] A bare word alone (e.g., `foo`) produces a command node with no arguments
- [ ] Source positions span the full bare command (first token to last token before newline/semicolon/EOF)
- [ ] All tests pass and no memory leaks

### US-006: Parse code blocks
**Description:** As the compiler, I need code block AST nodes so that `{ ... }` arguments to builtins like `proc` and `if` can be compiled.

**Acceptance Criteria:**
- [ ] `{ print hello }` produces a code block node containing one command
- [ ] Multi-line code blocks produce a block with multiple commands (newline-delimited)
- [ ] Semicolons delimit commands within code blocks: `{ print a; print b }` produces a block with two commands
- [ ] Code blocks can be arguments to commands: `[if $cond { print yes } { print no }]`
- [ ] Nested code blocks: `{ if $x { inner } }` parses correctly
- [ ] Empty code blocks `{}` produce a code block node with zero commands
- [ ] Mismatched braces produce a parse error with position info
- [ ] Source positions span from `{` to `}`
- [ ] All tests pass and no memory leaks

### US-007: Parse string interpolation
**Description:** As the compiler, I need interpolated string AST nodes so that `"hello $name"` can be compiled to string concatenation.

**Acceptance Criteria:**
- [ ] Non-interpolated strings (`"hello"`) produce a plain string literal node
- [ ] `"hello $name"` produces an interpolated string node containing a string segment ("hello ") and a variable reference (name)
- [ ] `"result: $[+ 1 2]"` produces an interpolated string node with a string segment and a nested command invocation
- [ ] Multiple interpolations: `"$a and $b"` produces correct segment sequence
- [ ] Adjacent interpolations: `"$a$b"` produces correct structure (no empty string segments between them, or empty segments are acceptable)
- [ ] Source positions cover the full string including quotes
- [ ] All tests pass and no memory leaks

### US-008: Parse programs (multiple top-level forms)
**Description:** As the compiler, I need to parse complete multi-line JACL programs into a list of top-level AST nodes.

**Acceptance Criteria:**
- [ ] A multi-line program produces a list of top-level command nodes
- [ ] Mixed syntax works: bare commands, bracketed commands, and commands with code block arguments all coexist
- [ ] A realistic program (10+ lines mixing `def`, `proc`, `if`, arithmetic) parses into the correct AST
- [ ] Empty input produces an empty command list (no error)
- [ ] Comment-only input produces an empty command list (no error)
- [ ] Source positions are correct across many lines
- [ ] All tests pass and no memory leaks

### US-009: Parse error recovery
**Description:** As a developer, I need the parser to recover from errors and continue parsing so that multiple issues are reported at once.

**Acceptance Criteria:**
- [ ] A missing closing bracket `[print hello` produces an error and parsing continues to the next top-level command
- [ ] A missing closing brace `{ print hello` produces an error and parsing continues
- [ ] An unexpected token (e.g., `]` at top level) produces an error and parsing continues
- [ ] Multiple errors in one program all produce error nodes with source positions and descriptive messages
- [ ] The parse result includes both valid AST nodes and an error count (or error list)
- [ ] Panic mode: on error, the parser skips tokens until it reaches a synchronization point (matching `]`/`}`, newline, or EOF)
- [ ] All tests pass and no memory leaks

### US-010: AST pretty-printer (round-trip)
**Description:** As a developer, I need an AST pretty-printer so that I can verify parse correctness via round-trip testing.

**Acceptance Criteria:**
- [ ] A function takes an AST node and writes equivalent JACL bracket-form source to a buffer
- [ ] All nodes are printed in explicit bracket form: `[cmd arg1 arg2]` (no implicit top-level syntax)
- [ ] Literals print as their source representation: `42`, `3.14`, `hello`
- [ ] Variable references print as `$name`
- [ ] Code blocks print as `{ cmd1; cmd2 }` or `{ cmd1\ncmd2 }`
- [ ] Interpolated strings print as `"segment $var segment $[cmd] segment"`
- [ ] Parsing the pretty-printed output produces an equivalent AST (round-trip property)
- [ ] All tests pass and no memory leaks

### US-011: Build integration
**Description:** As a developer, I need the parser module to compile and test within the existing build system.

**Acceptance Criteria:**
- [ ] `parser/ast.h` is a single-header defining AST node types (with `AST_IMPLEMENTATION` guard)
- [ ] `parser/parser.h` is a single-header implementing the recursive descent parser (with `PARSER_IMPLEMENTATION` guard)
- [ ] `parser/test_parser.c` compiles and runs via `build.sh`
- [ ] Test file uses `test/test_helpers.h` for assertions and memory tracking
- [ ] Tests use table-driven main() pattern (matching lexer test style)
- [ ] Build entry added to `build.sh` with appropriate dependencies
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`
- [ ] All tests pass with zero memory leaks under tracked allocator

## Functional Requirements

- FR-1: The parser must accept a `LexResult` (token array from the lexer) and an `arena_t*`, and return a `ParseResult` containing an arena-allocated array of top-level AST nodes, a count, and an error count
- FR-2: AST node types must include: `AST_COMMAND` (command invocation), `AST_LIT_INT` (integer literal), `AST_LIT_FLOAT` (float literal), `AST_LIT_STRING` (string literal), `AST_VAR_REF` (variable reference), `AST_BLOCK` (code block — sequence of commands), `AST_INTERP_STRING` (interpolated string — list of segments), `AST_ERROR` (parse error placeholder)
- FR-3: Each AST node must carry source positions: start (line, column, byte offset) and end (line, column, byte offset), derived from the tokens that produced it
- FR-4: `AST_COMMAND` nodes must contain: the command name (first element), and an array of argument nodes (each can be any AST node type)
- FR-5: `AST_BLOCK` nodes must contain an array of `AST_COMMAND` nodes representing the commands within the block
- FR-6: `AST_INTERP_STRING` nodes must contain an ordered array of segments, where each segment is either a string literal, a variable reference, or a command invocation (from `$[expr]`)
- FR-7: At top level, the parser must treat newlines and semicolons as command delimiters — a bare word followed by arguments up to newline/semicolon/EOF is wrapped in an implicit command invocation
- FR-8: Nested `[cmd ...]` must be parsed as explicit command invocations, usable as arguments to other commands
- FR-9: Code blocks `{ ... }` must be parsed as a sequence of newline/semicolon-delimited commands, and can appear as arguments to commands
- FR-10: On parse error, the parser must record an error (with source position and message), skip tokens to the next synchronization point (matching close bracket/brace, newline, or EOF), and continue parsing
- FR-11: The parser must provide an `ast_pretty_print` function that writes an AST node as bracket-form JACL source to a caller-provided buffer
- FR-12: All memory allocations must go through the provided arena — no direct malloc/free calls in the parser
- FR-13: Operator tokens (e.g., `+`, `>=`, `->`) must be treated as command names when they appear in the command position (first element of a `[...]` invocation)

## Non-Goals

- Phase 2 syntactic sugar — no infix operator parsing, no `=`/`:`/`::` assignment syntax
- Macro expansion — the parser produces raw AST; macro processing is a later pass
- Type annotations — not part of Phase 1 syntax
- Semantic analysis or name resolution — the parser only produces structure
- Operator precedence — all operations are explicit bracket-delimited commands in Phase 1
- Desugaring of any kind — the AST is a direct representation of the source syntax
- Parenthesized expression grouping — deferred to Phase 2 syntax

## Technical Considerations

- **Single-header style:** Follow the project convention with `#ifdef AST_IMPLEMENTATION` and `#ifdef PARSER_IMPLEMENTATION` guards, matching `arena.h` and `lexer.h`
- **Arena allocation:** All AST nodes allocated from the caller-provided arena. Node arrays (command arguments, block bodies, interp segments) are also arena-allocated. The arena's bump allocator makes this efficient — no individual node deallocation
- **Node representation:** Tagged union — a `NodeType` enum discriminant plus a union of type-specific structs. Arena-allocated so the caller manages lifetime in bulk
- **Argument lists:** Since argument count is not known ahead of time, use a two-pass approach (count then allocate) or a temporary growable buffer. Arena chaining handles overflow naturally. Alternatively, arena-allocate in a contiguous block with a count field
- **Token consumption:** The parser holds a cursor (index) into the token array and advances through it. Helper functions: `peek()`, `advance()`, `expect(type)`, `match(type)`
- **Synchronization points for error recovery:** On error inside `[...]`, skip to matching `]` (tracking bracket depth). On error inside `{...}`, skip to matching `}`. On error at top level, skip to next newline or EOF
- **Source positions on nodes:** Start position comes from the first token of the construct; end position from the last token. For command invocations, start = `[` token position (or first word for bare commands), end = `]` token position (or last argument for bare commands)
- **Existing dependencies:** `lexer/lexer.h` (token types and `LexResult`), `arena/arena.h` (allocation), `platform/platform.h` (basic types). The parser does NOT depend on `value/value.h`
- **Test harness:** Use `test/test_helpers.h` with `tracked_allocator` plugged into the arena. Verify zero leaks after each test. Use table-driven test main() matching the lexer test pattern
- **Pretty-printer:** Writes to a dynamically-sized buffer (arena-allocated). This enables round-trip tests: parse source → pretty-print → parse again → compare AST structures

## Success Metrics

- All Phase 1 syntax constructs parse into correct AST structures
- Error recovery: a program with 5 intentional syntax errors produces 5 error nodes and correct AST for the valid portions
- Position tracking: start/end line/column/offset correct on every node in a 50+ line program
- Round-trip: for all valid test inputs, parsing the pretty-printed output produces a structurally equivalent AST
- Zero memory leaks across all tests under tracked allocator
- Compiles cleanly with `-Wall -Wextra -std=c99`

## Open Questions

- Should `AST_COMMAND` store the command name as a child node (allowing computed commands like `[$fn arg]`) or as a direct string field? Storing as a child node is more general and handles `$[expr]` in command position, but adds complexity. Recommendation: store as a child node — the first argument is the command, remaining are arguments. This mirrors Lisp's `(f x y)` where `f` is evaluated.
- Should the parser handle `$[expr]` outside of strings (the `TOKEN_DOLLAR_BRACKET` token)? DESIGN.md mentions subcommands via `[cmd ...]` — clarify whether `$[cmd]` is an alias for `[cmd]` outside strings. Recommendation: yes, treat `$[cmd]` as equivalent to `[cmd]` outside of strings.
- Should code blocks allow nested bare commands, or require all inner commands to be bracketed? DESIGN.md examples show bare commands inside blocks (e.g., `proc add [x y] { [+ $x $y] }` uses brackets inside the block). Recommendation: allow both bare and bracketed commands inside blocks, matching top-level behavior.
- How to represent the parse result for pretty-printing round-trip: should the pretty-printer normalize the AST (e.g., always use bracket form), or attempt to preserve the original surface syntax? Recommendation: always normalize to explicit bracket form — this tests structural equivalence rather than textual identity.
