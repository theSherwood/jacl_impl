# PRD: JACL Lexer (Milestone 1)

## Introduction

Implement a streaming tokenizer for JACL's Phase 1 bracket syntax. The lexer converts raw byte buffers into an arena-allocated array of tokens, handling all Phase 1 constructs: brackets, braces, parentheses, bare words, numbers, strings with interpolation, variable references, comments, and newlines. It provides full error recovery — invalid input produces error tokens with source positions while lexing continues through the rest of the input.

The lexer is the second milestone of the JACL language (a bytecode-compiled fusion of Tcl and Clojure), sitting between raw source text and the parser. It depends only on the arena allocator and platform modules — it does NOT depend on Milestone 0 (Value Representation).

## Goals

- Tokenize all JACL Phase 1 syntax constructs into a flat array of tokens
- Track source positions (line, column, byte offset) on every token for error reporting
- Recover from lexical errors gracefully — emit error tokens and continue lexing
- Use arena allocation for all token storage (zero manual free, bulk lifetime)
- Operate on byte buffers (null-terminated `const char*` input)
- Handle UTF-8 in bare words and string content (pass through, don't decode beyond validating)
- Support full C-style and Unicode escape sequences in strings
- Keep the implementation simple and flat — no objects, no unnecessary abstractions

## User Stories

### US-001: Lex bracket delimiters
**Description:** As the parser, I need bracket tokens (`[`, `]`, `{`, `}`, `(`, `)`) so that I can build the command invocation tree.

**Acceptance Criteria:**
- [ ] Each of `[`, `]`, `{`, `}`, `(`, `)` produces a distinct token type
- [ ] Each token carries correct line, column, and byte offset
- [ ] Adjacent delimiters with no whitespace produce separate tokens
- [ ] All tests pass and no memory leaks detected by tracked allocator

### US-002: Lex bare words
**Description:** As the parser, I need bare word tokens so that I can identify command names, arguments, and symbols.

**Acceptance Criteria:**
- [ ] Sequences of non-delimiter, non-whitespace, non-special characters produce WORD tokens
- [ ] Bare words can contain UTF-8 multibyte characters
- [ ] Bare words are terminated by whitespace, delimiters (`[]{}()`), `$`, `"`, `#`, or EOF
- [ ] Operator tokens (sequences of non-letter, non-digit, non-space, non-control ASCII like `+`, `->`, `>=`, `*`) produce OPERATOR tokens, distinct from WORD
- [ ] Keywords (`:` followed by word characters, e.g. `:key`) produce KEYWORD tokens
- [ ] Token payload includes pointer to start and length (into original source buffer)
- [ ] All tests pass and no memory leaks

### US-003: Lex numeric literals
**Description:** As the parser, I need integer and float tokens so that numeric values can be compiled.

**Acceptance Criteria:**
- [ ] Integer literals (`42`, `0`, `-17`) produce INT tokens
- [ ] Float literals (`3.14`, `0.5`, `-2.0`) produce FLOAT tokens
- [ ] Leading negative sign is part of the number only when unambiguous (after `[`, whitespace, or at start of input); otherwise it is an OPERATOR
- [ ] Numbers followed immediately by a word character (e.g. `42abc`) produce an error token
- [ ] Hex literals (`0xFF`) and binary literals (`0b1010`) produce INT tokens
- [ ] All tests pass and no memory leaks

### US-004: Lex strings with interpolation
**Description:** As the parser, I need string tokens with interpolation markers so that the parser can build interpolated string AST nodes.

**Acceptance Criteria:**
- [ ] Double-quoted strings (`"hello"`) produce STRING tokens
- [ ] Strings may span multiple lines (embedded newlines are part of the string)
- [ ] `$var` inside a string produces a sequence: STRING_BEGIN, ... INTERP_VAR, ... STRING_END (or equivalent representation that marks interpolation boundaries)
- [ ] `$[expr]` inside a string marks an expression interpolation boundary
- [ ] Escape sequences supported: `\\`, `\"`, `\n`, `\t`, `\r`, `\0`, `\xNN` (hex byte), `\uNNNN` (Unicode), `\UNNNNNNNN` (Unicode extended)
- [ ] Invalid escape sequences produce an error token with position info, lexing continues
- [ ] Unterminated strings produce an error token at EOF
- [ ] All tests pass and no memory leaks

### US-005: Lex variable references
**Description:** As the parser, I need `$identifier` tokens so that variable reads can be compiled.

**Acceptance Criteria:**
- [ ] `$` followed by identifier characters produces a VAR token
- [ ] The token payload includes the identifier name (without the `$` prefix)
- [ ] `$` followed by `[` is the start of a subcommand expression — produces a DOLLAR_BRACKET token (or equivalent)
- [ ] `$` followed by non-identifier, non-`[` produces an error token
- [ ] All tests pass and no memory leaks

### US-006: Lex comments and whitespace
**Description:** As the parser, I need comments skipped and newlines preserved as tokens so that top-level command boundaries can be determined.

**Acceptance Criteria:**
- [ ] `#` starts a line comment — everything from `#` to end of line is skipped (no token emitted)
- [ ] Spaces and tabs are skipped (no token emitted)
- [ ] Newline characters (`\n`, `\r\n`) produce NEWLINE tokens
- [ ] Consecutive newlines may produce consecutive NEWLINE tokens (parser decides significance)
- [ ] A comment before a newline still produces the NEWLINE token
- [ ] All tests pass and no memory leaks

### US-007: Lex full programs
**Description:** As the parser, I need to lex multi-line JACL programs end-to-end and get a complete token array terminated by EOF.

**Acceptance Criteria:**
- [ ] Lexing a full multi-line program produces the correct sequence of tokens
- [ ] The token array is terminated by an EOF token
- [ ] All tokens have correct source positions even across many lines
- [ ] Nested constructs (`[cmd [inner] "str $[nested]"]`) tokenize correctly
- [ ] All tests pass and no memory leaks

### US-008: Error recovery
**Description:** As a developer, I need the lexer to recover from errors so that the parser can report multiple issues at once.

**Acceptance Criteria:**
- [ ] Invalid characters (e.g. stray `\x01` control chars) produce ERROR tokens with a message
- [ ] After an error token, lexing continues from the next recoverable position
- [ ] Unterminated strings produce an error token; remaining input (if any) continues to lex
- [ ] Multiple errors in one input all produce error tokens with correct positions
- [ ] Error tokens carry a human-readable message string (arena-allocated)
- [ ] All tests pass and no memory leaks

### US-009: Build integration
**Description:** As a developer, I need the lexer to compile and test within the existing build system.

**Acceptance Criteria:**
- [ ] `lexer/lexer.h` is a single-header implementation (matching project convention: header with `#ifdef LEXER_IMPLEMENTATION` guard)
- [ ] `lexer/test_lexer.c` compiles and runs via `build.sh`
- [ ] Test file uses `test/test_helpers.h` for memory tracking
- [ ] Lexer uses `Allocator` from `platform/platform.h` via arena
- [ ] All tests pass with zero memory leaks under tracked allocator

## Functional Requirements

- FR-1: The lexer must accept a `const char*` input buffer and an `arena_t*`, and return a pointer to an arena-allocated array of `Token` structs plus a count
- FR-2: Each `Token` must contain: token type (enum), source position (line, column, byte offset), span length, and a payload (pointer into source buffer for words/strings, numeric value for numbers, error message for errors)
- FR-3: The token type enum must include at minimum: LBRACKET, RBRACKET, LBRACE, RBRACE, LPAREN, RPAREN, WORD, OPERATOR, KEYWORD, INT, FLOAT, STRING, STRING_BEGIN, STRING_PART, STRING_END, INTERP_VAR, INTERP_EXPR_START, INTERP_EXPR_END, VAR, DOLLAR_BRACKET, NEWLINE, ERROR, EOF_TOKEN
- FR-4: The lexer must skip `#` line comments (no token emitted) and horizontal whitespace (spaces, tabs)
- FR-5: Newline characters (`\n` and `\r\n`) must produce NEWLINE tokens
- FR-6: The lexer must handle all C-style escape sequences in strings: `\\`, `\"`, `\n`, `\t`, `\r`, `\0`, `\xNN`, `\uNNNN`, `\UNNNNNNNN`
- FR-7: String interpolation `$var` and `$[expr]` must produce structured token sequences that the parser can distinguish from literal string content
- FR-8: On encountering invalid input, the lexer must emit an ERROR token (with position and message) and advance past the problematic byte(s) to continue lexing
- FR-9: The token array must always be terminated by an EOF_TOKEN
- FR-10: All memory allocations must go through the provided arena — no direct malloc/free calls in the lexer

## Non-Goals

- Phase 2 syntactic sugar (operators as infix, assignment syntax) — not lexed specially here
- Rope-based input — the lexer operates on flat byte buffers; rope support is a future enhancement
- Unicode normalization or grapheme cluster handling — UTF-8 bytes are passed through as-is
- Incremental/partial re-lexing — lex the entire input each time
- Value representation (Milestone 0) — the lexer is independent of `JaclVal`
- Parser or AST construction — the lexer only produces tokens

## Technical Considerations

- **Single-header style:** Follow the project convention (`#ifdef LEXER_IMPLEMENTATION` pattern) as seen in `arena/arena.h`
- **Arena allocation:** All token storage allocated from the caller-provided arena. The lexer may do an initial pass or use a growable buffer within the arena. Consider pre-allocating a reasonable token array and growing if needed (arena regions chain automatically)
- **Source buffer lifetime:** Tokens reference into the original source buffer (zero-copy for word/string text). The caller must keep the source buffer alive as long as tokens are in use
- **String interpolation tokenization:** Interpolated strings like `"hello $name, $[expr] end"` must decompose into a token sequence that preserves structure. Recommended approach: STRING_BEGIN `"hello "` / INTERP_VAR `name` / STRING_PART `", "` / INTERP_EXPR_START / ... tokens for expr ... / INTERP_EXPR_END / STRING_PART `" end"` / STRING_END. Non-interpolated strings produce a single STRING token
- **Existing dependencies:** `arena/arena.h` (allocation), `platform/platform.h` (Allocator type, basic types). Nothing else
- **Test harness:** Use `test/test_helpers.h` with `tracked_allocator` plugged into the arena. Verify zero leaks after each test
- **Build integration:** Add entry to `TESTS` array in `build.sh`

## Success Metrics

- All token types from FR-3 are correctly produced for valid input
- Error recovery: a program with 5 intentional errors produces 5 error tokens and correct tokens for everything else
- Position tracking: line/column/offset correct on every token in a 100+ line program
- Zero memory leaks across all tests under tracked allocator
- Compiles cleanly with `-Wall -Wextra -std=c99`

## Open Questions

- Should the leading `-` on negative numbers be handled in the lexer (as part of the number token) or left to the parser (as a unary operator)? Current decision: lexer handles it when unambiguous, otherwise emits OPERATOR. This may need revisiting during parser implementation.
- Exact set of operator characters — DESIGN.md says "composed entirely of non-letter, non-number, non-space, non-control ASCII characters." Need to finalize which characters (e.g., `$`, `#`, `"` are special, not operators). Proposed operator chars: ``!%&*+-./<=>?@\^|~``
- Should string interpolation `$[expr]` lex the inner expression recursively (producing full tokens between INTERP_EXPR_START/END), or should it produce a raw span for the parser to re-lex? Recommended: recursive lexing (the lexer tracks bracket depth within interpolation expressions).
- Maximum token array size / input size limits? Currently unbounded (arena grows as needed).
