# PRD: Hygienic Macro System (M15)

## Introduction

Add a compile-time macro system to Jacl. Macros receive unevaluated AST as syntax objects, transform it, and return new AST that the compiler processes in place of the original call. Expansion is hygienic by default — macro-introduced identifiers don't collide with caller identifiers — with explicit escape hatches for intentional binding introduction.

This is the foundation for user-extensible syntax. Operator-as-macro support and prelude rewriting are future work built on top of this.

## Goals

- Implement `defmacro` as a top-level form that defines compile-time AST transformers
- Implement syntax objects as a compile-time value type carrying kind, datum, source position, and scope marks
- Implement `quote` and `syntax-quote` (with `~` unquote and `~@` unquote-splicing) for constructing syntax
- Implement hygienic expansion via scope marks, with `^` prefix for intentional introduction and `gensym` for unique temporaries
- Implement a macro expansion pass in the compiler (between parsing and bytecode emission)
- Provide introspection and construction APIs for advanced macro authors
- Implement `syntax-error` for explicit compile-time error reporting from macro bodies

## User Stories

### US-001: Add JACL_TAG_SYNTAX value type
**Description:** As the compiler, I need a value type to represent AST nodes as manipulable values during macro expansion.

**Acceptance Criteria:**
- [ ] Add `JACL_TAG_SYNTAX` to the tag enum in `value.c`
- [ ] A syntax object wraps: kind (string — `"command"`, `"var-ref"`, `"lit-int"`, `"lit-float"`, `"lit-string"`, `"block"`, `"interp-string"`, `"spread"`), datum (child syntax objects or raw value), source position (line, col), and a scope mark set (initially empty)
- [ ] Syntax objects are GC-managed (add `OBJ_SYNTAX` to `gc.c`)
- [ ] Add helper functions to create syntax objects from `AstNode*` (converting the entire subtree)
- [ ] Add helper to convert syntax objects back to `AstNode*` for compilation
- [ ] Syntax objects print readably (e.g. `<syntax:command [if ...]>`) for debugging
- [ ] Tests: round-trip AstNode -> syntax object -> AstNode preserves structure

### US-002: Add `quote` special form
**Description:** As a macro author, I want to write `quote [foo $x 3]` to get the literal AST of that expression as a syntax object, without evaluating it.

**Acceptance Criteria:**
- [ ] Lexer recognizes `quote` as a keyword (`TOKEN_QUOTE`)
- [ ] Parser parses `quote <expr>` and produces an `AST_QUOTE` node wrapping the unevaluated child
- [ ] Compiler handles `AST_QUOTE` by converting the child AST to a syntax object value at compile time
- [ ] `quote` does not evaluate any part of its argument — `$x` remains a var-ref syntax node, not a variable lookup
- [ ] Nested quotes work: `quote [foo quote [bar]]`
- [ ] Tests: `quote [+ 1 2]` produces a syntax object with kind "command", head "+" and two literal args

### US-003: Add `syntax-quote` with `~` and `~@`
**Description:** As a macro author, I want quasiquoting to build syntax templates with computed holes.

**Acceptance Criteria:**
- [ ] Lexer recognizes `syntax-quote` as a keyword
- [ ] `~expr` (unquote) inside a `syntax-quote` evaluates `expr` and splices the resulting syntax object into the template
- [ ] `~@expr` (unquote-splicing) evaluates `expr` (must produce a vec of syntax objects) and flattens it into the surrounding command's argument list or block's command list
- [ ] Unquoted expressions can reference macro parameters and local bindings
- [ ] Nested `syntax-quote` / `~` respects depth: `~` only unquotes at the current nesting level
- [ ] Error if `~@` appears outside a command arg list or block command list
- [ ] Tests: `syntax-quote [if [not ~cond] ~body]` with bound `cond` and `body` produces correct compound syntax
- [ ] Tests: `syntax-quote [foo ~@args]` with `args` = vec of two syntax objects produces a 3-arg command

### US-004: Add `defmacro` top-level form
**Description:** As a Jacl programmer, I want to define macros that transform syntax at compile time.

**Acceptance Criteria:**
- [ ] Lexer recognizes `defmacro` as a keyword (`TOKEN_DEFMACRO`)
- [ ] Parser parses `defmacro name {param1, param2, ...} { body }` and produces an `AST_DEFMACRO` node
- [ ] `defmacro` is top-level only — error if it appears inside a block, proc, or other nested context
- [ ] Macro parameters receive syntax objects (the unevaluated AST of each argument at the call site)
- [ ] The macro body is Jacl code that runs at compile time and must return a syntax object
- [ ] The returned syntax object replaces the macro call in the AST before bytecode compilation
- [ ] Macros are registered in a compile-time macro table keyed by name
- [ ] Definition must precede use — error if a macro is called before its `defmacro`
- [ ] Error if a macro name shadows an existing special form (`if`, `proc`, `def`, `mut`, `set`, etc.)
- [ ] Tests: `defmacro unless {cond, body} { syntax-quote [if [not ~cond] ~body] }` then `unless [eq 1 2] { print "ok" }` prints "ok"
- [ ] Tests: error when `defmacro` appears inside a proc body

### US-005: Implement macro expansion pass
**Description:** As the compiler, I need a pass that expands all macro calls before bytecode emission.

**Acceptance Criteria:**
- [ ] Add an expansion pass between parsing and the existing compilation pass
- [ ] The pass walks the AST top-to-bottom; when it encounters a command whose head matches a registered macro, it invokes the macro
- [ ] Macro invocation: evaluate the macro body with params bound to the call-site argument syntax objects, collect the returned syntax object, convert back to AST, and replace the call node
- [ ] Expansion is iterative — the result of a macro expansion is re-walked for further macro calls
- [ ] Recursion depth limit (default 256) with a clear error message on exceeded depth
- [ ] Non-macro commands and all other AST node types pass through unchanged
- [ ] Tests: macro that expands to another macro call (two levels) works correctly
- [ ] Tests: recursive macro expansion hits depth limit and produces error

### US-006: Hygienic expansion with scope marks
**Description:** As a macro author, I want macro-introduced identifiers to be invisible to the caller by default, preventing accidental name capture.

**Acceptance Criteria:**
- [ ] Each macro expansion assigns a fresh scope mark (monotonically increasing integer)
- [ ] Identifiers in a `syntax-quote` template get the current macro's scope mark
- [ ] Identifiers spliced via `~` retain their original scope marks (the caller's)
- [ ] During name resolution, two identifiers with the same name but different scope marks are treated as distinct variables
- [ ] A macro using `tmp` internally does not shadow a caller's `tmp`
- [ ] Tests: `defmacro m {x} { syntax-quote { tmp = 1; [+ ~x $tmp] } }` then `tmp = 99; m $tmp` returns 100 (caller's `tmp` = 99, macro's `tmp` = 1, `~x` sees caller's `tmp`)
- [ ] Tests: two nested macro expansions each introducing `tmp` don't collide with each other

### US-007: `^` prefix for intentional binding introduction
**Description:** As a macro author writing anaphoric macros, I want `^name` in a `syntax-quote` to introduce a name in the caller's scope.

**Acceptance Criteria:**
- [ ] Inside `syntax-quote`, `^identifier` creates an identifier with the caller's scope mark instead of the macro's
- [ ] `^` only valid inside `syntax-quote` — error elsewhere
- [ ] The introduced name is accessible in subsequent code at the call site
- [ ] Tests: `defmacro aif {cond, body} { syntax-quote { ^it = ~cond; if $^it ~body } }` then `aif 42 { print $it }` prints 42
- [ ] Tests: `^` used outside `syntax-quote` produces a compile error

### US-008: `gensym` builtin
**Description:** As a macro author, I want `gensym` to generate guaranteed-unique symbol names for temporaries.

**Acceptance Criteria:**
- [ ] `gensym` is a builtin available during macro expansion that takes an optional string prefix
- [ ] Returns a syntax object (var-ref) with a name guaranteed to never collide with any user-written or macro-introduced identifier
- [ ] Generated names include the prefix for readability (e.g. `or_tmp__g42`)
- [ ] Each call to `gensym` produces a distinct name
- [ ] Tests: `gensym "tmp"` produces unique names across multiple calls
- [ ] Tests: macro using gensym for a temporary doesn't collide with caller names

### US-009: Introspection API
**Description:** As an advanced macro author, I want to inspect the structure of syntax objects passed to my macro.

**Acceptance Criteria:**
- [ ] `syntax-kind $s` returns the kind string (`"command"`, `"var-ref"`, `"lit-int"`, `"lit-float"`, `"lit-string"`, `"block"`, `"interp-string"`, `"spread"`)
- [ ] `syntax-datum $s` returns the raw value for literals (integer for lit-int, float for lit-float, string for lit-string), the name string for var-ref
- [ ] `syntax-head $s` returns the head syntax object for commands; error for non-commands
- [ ] `syntax-args $s` returns a vec of child syntax objects for commands; error for non-commands
- [ ] `syntax-commands $s` returns a vec of child syntax objects for blocks; error for non-blocks
- [ ] `syntax-pos $s` returns a map `{line: N, col: M}`
- [ ] `syntax->string $s` returns a pretty-printed string representation of the syntax
- [ ] All introspection builtins error clearly when given a non-syntax value
- [ ] Tests: round-trip inspection of `quote [+ 1 2]` — kind is "command", head datum is "+", args are lit-int 1 and lit-int 2
- [ ] Tests: `syntax-pos` returns correct line/col from source
- [ ] Tests: `syntax->string` of `quote [if $x 3]` produces readable output

### US-010: Construction API
**Description:** As an advanced macro author, I want to build syntax objects programmatically when quasiquoting isn't sufficient.

**Acceptance Criteria:**
- [ ] `make-syntax "command" $head $args-vec` creates a command syntax object
- [ ] `make-syntax "lit-int" $value` creates an integer literal syntax object
- [ ] `make-syntax "lit-float" $value` creates a float literal syntax object
- [ ] `make-syntax "lit-string" $value` creates a string literal syntax object
- [ ] `make-syntax "var-ref" $name` creates a variable reference syntax object
- [ ] `make-syntax "block" $commands-vec` creates a block syntax object
- [ ] Constructed syntax objects get the current expansion's scope mark
- [ ] Error on invalid kind string or wrong argument types
- [ ] Tests: `make-syntax "command" [make-syntax "lit-string" "+"] [vec [make-syntax "lit-int" 1] [make-syntax "lit-int" 2]]` produces equivalent syntax to `quote [+ 1 2]`
- [ ] Tests: macro that conditionally constructs different AST based on argument inspection

### US-011: `syntax-error` for explicit compile-time errors
**Description:** As a macro author, I want to signal a compile error with a custom message when the macro is used incorrectly.

**Acceptance Criteria:**
- [ ] `syntax-error $message` halts compilation with the given message
- [ ] `syntax-error $message $syntax-obj` halts with the message and points to the source position of the given syntax object
- [ ] The error output includes: the custom message, the macro call site location, and (when provided) the specific syntax object's source location
- [ ] Tests: `defmacro only-ints {x} { if [!= [syntax-kind $x] "lit-int"] { syntax-error "expected integer literal" $x }; ~x }` — calling with a string literal produces the custom error
- [ ] Tests: error output includes correct source positions

### US-012: Compile-time error reporting for macro failures
**Description:** As a Jacl programmer, when a macro body fails during expansion I want error messages pointing to both the call site and the failure location inside the macro.

**Acceptance Criteria:**
- [ ] When a macro body throws an error during compile-time evaluation, the error message includes: (1) the macro call site (file, line, col), and (2) the location inside the macro body where the error occurred
- [ ] Stack-trace-style output: "in expansion of macro `name` at file:line:col" followed by the internal error
- [ ] Errors from nested macro expansion (macro A calls macro B which fails) show the full expansion chain
- [ ] Tests: macro body that divides by zero at compile time produces error pointing to both the call site and the division
- [ ] Tests: nested macro failure shows both expansion levels

## Functional Requirements

- FR-1: Add `JACL_TAG_SYNTAX` value tag and `OBJ_SYNTAX` GC object type for syntax objects
- FR-2: Syntax objects carry kind, datum, source position, and a scope mark set
- FR-3: Add `TOKEN_QUOTE`, `TOKEN_SYNTAX_QUOTE`, `TOKEN_DEFMACRO` keywords to the lexer
- FR-4: Add `AST_QUOTE`, `AST_SYNTAX_QUOTE`, `AST_UNQUOTE`, `AST_UNQUOTE_SPLICING`, `AST_DEFMACRO` node types to the parser
- FR-5: `defmacro` is top-level only; definition must precede use in source order
- FR-6: Implement a macro expansion pass between parsing and bytecode compilation that iteratively expands until no macro calls remain, with a depth limit of 256
- FR-7: Macro bodies execute as Jacl code at compile time using the existing VM; they receive syntax objects and must return a syntax object
- FR-8: `syntax-quote` assigns the current macro's scope mark to template identifiers; `~` preserves the original scope marks of spliced syntax
- FR-9: `^identifier` in `syntax-quote` assigns the caller's scope mark to that identifier
- FR-10: `gensym` produces unique symbol names that cannot collide with any other identifier
- FR-11: Introspection builtins (`syntax-kind`, `syntax-datum`, `syntax-head`, `syntax-args`, `syntax-commands`, `syntax-pos`, `syntax->string`) available during macro expansion
- FR-12: Construction builtin `make-syntax` available during macro expansion
- FR-13: `syntax-error` halts compilation with a custom message and optional source position
- FR-14: Macro expansion errors report both call site and internal failure location
- FR-15: Convert between `AstNode*` (C struct) and syntax objects (`JACL_TAG_SYNTAX`) in both directions

## Non-Goals

- Mode-specific operator overloading (`defmacro | :cmd` vs `:infix`)
- Prelude module defining operators as macros
- Rewriting existing builtins (`proc`, `if`, `def`, etc.) as macros
- Aliases as macro sugar
- `defmacro` inside blocks or procs (local macros)
- Macro export/import across modules (depends on M14)
- Reader macros or token-level macros

## Technical Considerations

- **Compile-time VM execution**: macro bodies need to run Jacl code at compile time. This likely means compiling the macro body to bytecode eagerly when `defmacro` is encountered, then invoking the VM to execute it during the expansion pass. The existing `jacl_eval()` in `embed.c` may be a starting point.
- **AstNode <-> Syntax conversion**: the expansion pass needs to convert between C-level `AstNode*` (arena-allocated, used by parser/compiler) and `JaclVal` syntax objects (GC-managed, used by macro bodies). This is a tree walk in both directions. Source positions must survive the round trip.
- **Scope mark representation**: scope marks can be a simple set of integers attached to each identifier syntax node. During name resolution in the compiler, two identifiers match only if they have the same name AND compatible scope mark sets. The exact compatibility rule follows the "sets of scopes" model (an identifier is visible if its scope set is a subset of the reference's scope set).
- **GC interaction**: syntax objects are transient (exist only during compilation) but must be GC-rooted while macro bodies execute, since the VM's GC may run during expansion.
- **Depth limit**: track expansion depth with a simple counter incremented on each macro call and decremented on return. Exceeding 256 is a compile error.

## Success Metrics

- All existing tests continue to pass (macros are additive, no regressions)
- A `defmacro unless` that desugars to `if/not` works end-to-end
- Hygiene is demonstrable: a macro-internal `tmp` doesn't collide with a caller's `tmp`
- `^` introduction is demonstrable: an anaphoric macro can introduce a name the caller uses
- Nested macro expansion (macro A expands to a call to macro B) works correctly
- Error messages from failed macro expansion point to both call site and internal failure

## Resolved Design Decisions

- **Depth limit**: fixed constant (256), not configurable. If hit, the macro is broken.
- **`~` inside strings**: not supported. `~` only works in structural positions (command args, block commands). Use `make-syntax "interp-string" ...` for dynamic interpolated string construction.
- **Kind strings for all AST types**: lowercase kebab-case, dropping the `AST_` prefix. Complete mapping: `"command"`, `"lit-int"`, `"lit-float"`, `"lit-string"`, `"var-ref"`, `"block"`, `"interp-string"`, `"use"`, `"defstruct"`, `"break"`, `"continue"`, `"return"`, `"destructure-vec"`, `"destructure-named"`, `"spread"`. `AST_ERROR` nodes are not convertible to syntax objects.
