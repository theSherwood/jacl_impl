# PRD: Compiler & VM Refactoring

## Introduction

The JACL codebase (~19,400 lines of C across 14 source files) has accumulated structural duplication in its two largest modules: `compiler.c` (6,129 lines) and `vm.c` (4,441 lines). This refactoring eliminates that duplication before a major syntax redesign lands. The syntax redesign will heavily modify `compiler.c`, so cleaning it up now has compounding returns — every new feature added later benefits from the cleaner structure. The `vm.c` refactors are orthogonal and reduce maintenance burden independently.

All work items are pure refactors. No behavioral changes. Existing tests must continue to pass.

## Goals

- Reduce duplicated code in compiler.c by ~200 lines through shared helpers
- Reduce duplicated code in vm.c and embed.c by ~300 lines through macros, shared functions, and deduplication
- Make compiler.c easier to extend when three-mode delimiter parsing, pipes, destructuring, and other syntax features land
- Fill any obvious testing gaps discovered during refactoring
- Files may be created, moved, or deleted as needed to achieve cleaner organization

## User Stories

### US-001: Consolidate global arity lookups
**Description:** As a developer working on the compiler, I want a single function for looking up global arity information so that I don't have to maintain four near-identical linear scans.

**Acceptance Criteria:**
- [ ] Four functions replaced with a single `compiler__find_global(c, name)` returning `GlobalArity*`
  - `compiler__resolve_global_arity` (lines 1225–1235) — removed
  - `compiler__resolve_global_info` (lines 1237–1248) — removed
  - `compiler__resolve_global_type` (lines 1250–1259) — removed
  - `compiler__find_global_arity` (lines 1261–1270) — renamed/kept as the single entry point
- [ ] All call sites updated to use the single function and access `GlobalArity` fields directly
- [ ] No callers do their own root-walk or linear scan — all go through the single function
- [ ] All existing tests pass
- [ ] Compiler builds cleanly with no warnings

### US-002: Merge CPS argument extraction
**Description:** As a developer working on CPS transforms, I want the shared extraction logic in one place so that adding new CPS-aware constructs doesn't require duplicating 100 lines.

**Acceptance Criteria:**
- [ ] A shared helper extracted that takes an `AstNode**` array of arguments, identifies which ones are suspending, generates synthetic `[def __aN ...]` AST nodes for each, and returns the modified argument array plus the synthetic defs
- [ ] `compiler__compile_cps_extract_args` (lines 2245–2344) becomes a thin wrapper calling the shared helper
- [ ] `compiler__compile_cps_extract_def_value` (lines 2346–2436) becomes a thin wrapper calling the shared helper
- [ ] The shared helper covers: temp name generation, `ast_alloc` sequence, var ref / name node / def head / def command construction
- [ ] All existing tests pass, especially any CPS/suspension-related tests

### US-003: Extract HOF builtin compilation helper
**Description:** As a developer adding new higher-order builtins (like `for`, `par-each`), I want a shared compilation helper so each new builtin is ~5 lines instead of ~40.

**Acceptance Criteria:**
- [ ] A `compiler__compile_hof_builtin` helper extracted with signature like: `(Compiler* c, const char* name, uint32_t name_len, AstNode** args, uint32_t argc, uint8_t opcode, uint32_t line, uint32_t col)`
- [ ] The helper handles: arity check (exactly 2 args), non-suspending callback validation (local slot check, global arity check, block suspension point check), collection compilation, `in_non_suspending_callback` save/set/restore, callback compilation, opcode emission
- [ ] `transform` builtin (lines 4103–4141) rewritten as arity check + one call to helper
- [ ] `each` builtin (lines 4143–4181) rewritten as arity check + one call to helper
- [ ] `filter` builtin (lines 4183–4221) rewritten as arity check + one call to helper
- [ ] All existing tests pass

### US-004: Extract shared struct field marshaling
**Description:** As a developer, I want struct field read/write logic defined once so that changes to type marshaling don't require updating six copy-pasted switch statements.

**Acceptance Criteria:**
- [ ] Two shared functions defined: `struct_field_read(data, offset, type, heap)` and `struct_field_write(data, offset, type, val)` (or equivalent signatures)
- [ ] Functions placed wherever makes sense for the codebase (value.c, a shared header, etc.)
- [ ] `OP_STRUCT_GET` (vm.c lines 3924–3987) calls the shared read function
- [ ] `OP_STRUCT_SET` (vm.c lines 3989–4054) calls the shared write function
- [ ] `OP_STRUCT_GET_DYN` (vm.c lines 4056–4105) calls the shared read function
- [ ] `OP_STRUCT_SET_DYN` (vm.c lines 4107–4160) calls the shared write function
- [ ] `embed__struct_read_field` (embed.c lines 836–873) promoted to the shared read function (moved to shared location, embed.c calls it)
- [ ] `embed__struct_write_field` (embed.c lines 876–919) promoted to the shared write function (moved to shared location, embed.c calls it)
- [ ] Static vs dynamic opcode variants differ only in how they resolve field offset and type — marshaling is shared
- [ ] All existing tests pass, including any embed API tests

### US-005: Macro-ify typed arithmetic opcodes
**Description:** As a developer, I want the 20 typed arithmetic/comparison opcode handlers generated by macros so the pattern is obvious and mechanical changes don't risk introducing bugs in one of 20 copies.

**Acceptance Criteria:**
- [ ] `VM__I64_BINOP(op)` macro defined that handles: pop two values, cast to `int64_t`, apply operator, push result, break
- [ ] `VM__F64_BINOP(op)` macro defined that handles: pop two values, `memcpy` to double, apply operator, `memcpy` back or box as bool, push, break
- [ ] Extended variants for div/mod that include zero-check before the operation
- [ ] Unary neg variants handled (either separate macro or parameterized)
- [ ] i64 opcodes (lines 3338–3463) replaced with macro invocations: ADD, SUB, MUL, DIV, MOD, NEG, LT, GT, LE, GE, EQ
- [ ] f64 opcodes (lines 3465–3627) replaced with macro invocations: ADD, SUB, MUL, DIV, MOD, NEG, LT, GT, LE, GE
- [ ] Macros are `#define`d near their usage in vm.c (not in a shared header — they're vm-internal)
- [ ] All existing tests pass

## Functional Requirements

- FR-1: All refactored code must produce identical bytecode for any given input program
- FR-2: The `GlobalArity` struct (compiler.c lines 569–579) is the single source of truth for global arity/type info — no parallel data structures
- FR-3: The shared CPS extraction helper must handle both the "command arguments" case and the "def value expression" case via parameterization, not duplication
- FR-4: The HOF builtin helper must be parameterizable by builtin name (for error messages) and opcode — adding a new HOF builtin should require only a name, opcode, and one call
- FR-5: Shared struct field marshaling must handle all existing field types including DYN, and must work for both the "known offset/type at compile time" (static opcodes) and "resolved at runtime" (dynamic opcodes) paths
- FR-6: Arithmetic macros must not change evaluation order (pop b first, then a) or error behavior (div/mod zero checks)
- FR-7: Files may be created, moved, or deleted to achieve cleaner organization. If shared code is placed in a new file, it must be included in the unity build (`src/jacl.c`)
- FR-8: Any obvious testing gaps discovered during refactoring should be filled with new tests

## Non-Goals

- No behavioral changes — these are pure structural refactors
- No new language features or opcodes
- No changes to the public embed API signatures (internal implementation can change)
- No changes to bytecode format or encoding
- No performance optimization work (though eliminating function call overhead via `static inline` is fine where appropriate)
- No removal of libraries from `lib/` — they are kept for future use
- No work on the generational GC infrastructure
- No parser changes

## Technical Considerations

- **Unity build:** The project uses a unity build via `src/jacl.c`. Any new files must be `#include`d there.
- **Static functions:** Most internal functions are `static`. Shared helpers that cross file boundaries need appropriate visibility (non-static, or in a shared header as `static inline`).
- **Performance:** The struct field marshaling and arithmetic opcodes are in the VM hot path. Shared helpers should be `static inline` or otherwise ensure the compiler can inline them. Measure if concerned, but the current code already has function call overhead in the embed.c path.
- **Macro hygiene:** Arithmetic macros should use `do { ... } while(0)` or statement-expression patterns to avoid surprises at call sites.
- **The `in_non_suspending_callback` flag** in the HOF helper needs careful save/restore — the current code does this and the helper must preserve the pattern.
- **All stories are independent** — they can be done in any order and interleaved freely. No story depends on another.

## Success Metrics

- compiler.c reduced by ~200 lines (from 6,129)
- vm.c + embed.c reduced by ~300 lines combined
- Adding a new HOF builtin (like `for` or `par-each`) requires ≤10 lines in compiler.c
- No test regressions

## Open Questions

None — all resolved.
