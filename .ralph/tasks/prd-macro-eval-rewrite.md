# PRD: Macro Evaluation Rewrite — Staged Compilation + Reentrant Execution Context

## Introduction

JACL's macro system today is **template substitution only**. The expander in `src/syntax.c` (`expand__subst_template`) walks a `syntax-quote` template, splices unquoted arguments, and recognizes a tiny set of hard-coded special forms (currently just `gensym`). There is no interpreter or VM running during expansion — macro bodies cannot call functions, do arithmetic, branch on values, or otherwise execute user code at expansion time. This is a substantial departure from traditional Lisp macros (Scheme, CL, Racket), and every future feature that "should run during expansion" hits the same wall.

This PRD rewrites macro expansion to use **staged compilation**: macro bodies become regular closures compiled to bytecode, invoked on the real VM during expansion via a new reentrant execution-context abstraction (`jacl_context_t`). The same context abstraction is also designed as the substrate for M14's runtime `[interpret $src]` builtin (which is **not** shipped here, but the internal `jacl_ctx_run` API is).

Two prerequisites are also addressed: the **7-byte inline variable name limit** (which artificially constrains gensym prefixes and shows up constantly in realistic code), and a **VM reentrancy audit** (whether `vm_exec` tolerates being called from inside an opcode — needed for both this work and future M14 interpret).

The work is organized in four phases: prerequisites → shared substrate → macro-eval rewrite → migration & cleanup. A **gradual migration strategy** is used: a per-macro flag routes through either the old `expand__subst_template` path or the new staged path, and macros are converted one test file at a time. The old expander is deleted only after every test passes on the new path.

## Goals

- Replace template-substitution macro expansion with real bytecode evaluation: macro bodies are compiled as closures and invoked on the VM during expansion.
- Reframe `syntax-quote` as an **expression that builds a syntax object at runtime**, not a data template that gets walked. Unquotes (`~`, `~@`) become normal subexpressions whose values are spliced into the constructed syntax tree.
- Build a reentrant `jacl_context_t` execution-context abstraction usable by both the expander (now) and M14's `[interpret $src]` builtin (later). Expose internal `jacl_ctx_run(...)` API; do not surface a user-facing `interpret` builtin in this PRD.
- Eliminate the 7-byte inline variable name limit at all 14 enforcement sites in `compiler.c`. Allow names of arbitrary length (capped at 128 bytes — the boundary at which strings become non-pointer-stable ropes). Stop silently truncating names in `jacl_inline_string`.
- Eliminate gensym's special-form hack: `gensym` becomes a real VM builtin that returns a syntax var-ref, callable from any macro body. Allow long, descriptive prefixes (e.g. `gensym "loop_idx"`).
- Preserve the user-observable semantics of all existing macros. The full macro test suite (test_m10–m13, test_syntax.c — ~10,600 LOC) must pass on the new path before old infrastructure is deleted.
- Migrate macros gradually via a per-macro routing flag, with the old expander available as a fallback throughout the migration. Delete old infrastructure only when every test passes on the new path.

## User Stories

### US-001: Hygiene de-risking spike
**Description:** As the implementer, I need to validate the proposed mark-propagation hygiene rule before touching any compilation code, so that two weeks of rewrite don't unravel on a wrong assumption.

**Acceptance Criteria:**
- [ ] Pick 5–8 representative templates from existing macro tests (must include: simple unquote, `~@` splicing, `^`-prefixed identifiers, `gensym` use, nested `syntax-quote`, identifier capture/shadowing, transitive macro composition).
- [ ] For each, hand-lower the `syntax-quote` body into a tree of `make-syntax`/`vec` calls as it would compile under the new model.
- [ ] Run the hand-lowered bodies through the existing VM with a proposed mark-propagation rule: "the currently-executing macro call has a current_mark in its frame; every `make-syntax` called during that frame applies the mark to the output; spliced-in arg syntax objects pass through unchanged with their existing marks."
- [ ] Diff the resulting syntax trees against the output of today's `expand__subst_template` for the same templates. Output must be byte-identical (modulo position metadata).
- [ ] Document any cases where the rule diverges and propose an updated rule. If no rule produces byte-identical output for all 8 templates, **stop and re-evaluate Option A** before continuing this PRD.
- [ ] Spike output is captured in a markdown doc at `.ralph/spikes/macro-eval-hygiene-spike.md` with templates, lowered forms, diff results, and the validated rule.
- [ ] No production code is modified. Spike code lives under `.ralph/spikes/` or is reverted before US-002 begins.

### US-002: Fix `jacl_inline_string` silent truncation
**Description:** As the runtime, I want `jacl_inline_string` to refuse strings longer than 7 bytes rather than silently truncating, so that bugs in name handling fail loudly instead of producing corrupted identifiers.

**Acceptance Criteria:**
- [ ] `jacl_inline_string(s, len)` in `src/value.c:293` becomes a precondition: if `len > 7`, hit a `JACL_ASSERT(len <= 7, ...)` (or equivalent) instead of `if (len > 7) len = 7;`.
- [ ] All callers of `jacl_inline_string` audited; any that might pass a length > 7 are updated to either pre-check or route through `jacl_string_new` instead.
- [ ] Existing test suite still passes. The assertion must never fire under normal use after the audit.
- [ ] Build passes, all tests pass.

### US-003: Remove 7-byte name limit at compiler binding sites
**Description:** As a JACL programmer, I want to use variable names longer than 7 bytes (`captured`, `counter`, `current`, `element`) without the compiler rejecting them, so I can write idiomatic code.

**Acceptance Criteria:**
- [ ] All 14 enforcement sites in `src/compiler.c` (lines ~3355, 3366, 3640, 3653, 3730, 4172, 4722, 4859, 5335, 5480, 6099, 6428, 7668, 8034) are modified to no longer reject names > 7 bytes.
- [ ] Each binding site uses `jacl_string_new(heap, intern_table, name, len)` instead of `jacl_inline_string(name, len)` to construct the `JaclVal name` for `Local`/`Upvalue`/`GlobalArity`.
- [ ] Names ≤ 7 bytes still produce inline strings (cheapest representation); names 8–128 bytes produce interned strings (pointer-stable, `==` comparison still works); names > 128 bytes are rejected with a clear error ("variable name exceeds 128-byte limit") because rope strings are not pointer-stable and would break the resolve loops.
- [ ] `compiler__resolve_local`, `compiler__resolve_upvalue`, `compiler__find_global`, and `sm__find_field` continue to use `JaclVal ==` comparison unchanged. (Verify pointer-stability of interned strings via test.)
- [ ] Add integration tests that exercise long variable names in: `def`, `mut`, `set!`, proc params, vec destructure, named destructure, struct spread, `for` binding, `try` binding. Each test uses a name in the 8–30 byte range.
- [ ] Add a test verifying the 128-byte hard cap rejects with the expected error.
- [ ] Add a test verifying that two `def` sites at different scopes with the same long name resolve correctly (proves interned string identity works).
- [ ] Build passes, all tests pass, no regressions.

### US-004: Expand gensym prefix capability
**Description:** As a macro author, I want to give `gensym` a descriptive prefix like `"loop_idx"` instead of being capped at 3 bytes, so generated names are debuggable.

**Acceptance Criteria:**
- [ ] `jacl_gensym_next` in `src/syntax.c:1131` no longer caps the prefix at 3 bytes. Allow prefixes up to a sensible limit (e.g. 64 bytes), with the resulting name `prefix + "__" + decimal_digits(counter)` permitted to use heap-interned storage if it overflows the 7-byte inline budget.
- [ ] The hard-coded "max 7 chars" comment at `src/syntax.c:1126-1127` is removed and replaced with the actual new behavior.
- [ ] Update `expand__is_gensym_call` to accept longer prefixes (it currently parses arg as a lit-string and would need to handle interned strings).
- [ ] Add an integration test: macro that uses `gensym "loop_idx"` and verifies the generated identifier looks like `loop_idx__N` for some N, and is correctly bound in the expansion.
- [ ] All existing gensym tests still pass.
- [ ] Build passes.

### US-005: Define `jacl_context_t` and audit VM reentrancy
**Description:** As the implementer, I need a reentrant execution-context type that owns the arena, GC heap, intern table, macro table, and VM state, so that compile+run cycles can be nested (macro expansion → VM, future `interpret` → VM).

**Acceptance Criteria:**
- [ ] Define `jacl_context_t` struct in a new header (e.g. `src/context.h` or in `src/jacl.h`) that owns: `arena_t`, `ThreadHeap`, `JaclInternTable`, `MacroTable`, `VM`, and a `restriction_set` placeholder field (uint64 bitmask, all-permissive default).
- [ ] Provide `jacl_ctx_new(parent_ctx)` (parent may be NULL for root) and `jacl_ctx_destroy(ctx)` lifecycle functions. Child contexts may share parent's intern table but have their own arena/heap.
- [ ] Audit `vm_exec` and all functions it transitively calls for state that assumes a single in-flight execution: file-static caches, instruction-pointer registers held across calls, signal handler globals, etc. Document findings in a comment block at the top of `src/vm.c` or a `.ralph/notes/vm-reentrancy.md`.
- [ ] Fix any reentrancy hazards found. Common fixes: move file-static state into `VM` struct fields, replace global counters with context-scoped ones, ensure no opcode handler holds state across recursive `vm_exec` invocations.
- [ ] Add a test that calls `vm_exec` recursively (top-level invokes a builtin that itself calls `vm_exec` on a fresh closure) and verifies both invocations complete correctly without state corruption.
- [ ] Refactor `jacl_run` (`src/vm.c:6381`) to construct a `jacl_context_t` internally rather than assembling pieces ad-hoc. Public API unchanged.
- [ ] Build passes, all tests pass.

### US-006: Internal `jacl_ctx_run` API
**Description:** As the macro expander (and future `interpret` builtin), I need a stable internal API to compile and run JACL source or AST inside an existing context and receive a result value.

**Acceptance Criteria:**
- [ ] Provide internal API: `JaclVal jacl_ctx_run_source(jacl_context_t *ctx, const char *src, size_t len, uint64_t restriction_set, JaclError *err_out);` — lex, parse, compile, exec, return result.
- [ ] Provide internal API: `JaclVal jacl_ctx_run_closure(jacl_context_t *ctx, JaclClosure *closure, JaclVal *args, uint32_t arg_count, JaclError *err_out);` — invoke a pre-compiled closure with arguments and return result.
- [ ] Errors propagate via `JaclError` out-param: kind (compile-error / runtime-error), message, source position. The macro expander reframes these with caller context; future `interpret` returns them as JACL error values.
- [ ] `restriction_set` parameter is plumbed but not enforced beyond a no-op pass-through in this story (will be wired in a later PRD when sandboxing matters).
- [ ] Add tests: invoke `jacl_ctx_run_source` with valid source, invalid source (compile error), source that throws (runtime error). Verify each path returns the expected value/error.
- [ ] Add a test that nests `jacl_ctx_run_source` calls (an inner script that itself calls into the API via some test hook) — confirms reentrancy survives the public boundary.
- [ ] No user-facing `interpret` builtin is added — this is internal API only.
- [ ] Build passes, all tests pass.

### US-007: Compile `syntax-quote` as an expression
**Description:** As the compiler, I need to lower `syntax-quote` bodies into bytecode that constructs a syntax object at runtime via `make-syntax` calls, instead of representing them as static template AST.

**Acceptance Criteria:**
- [ ] Add a new compilation rule in `src/compiler.c` for `AST_SYNTAX_QUOTE` that emits bytecode constructing the templated syntax tree at runtime via `OP_SYNTAX_OP` (existing `make-syntax` subops 7–12 from US-016).
- [ ] Unquoted subexpressions (`~expr`) are compiled as normal expressions; their resulting `JaclVal` (which must be a syntax object at runtime, or an auto-promoted literal) is spliced into the parent `make-syntax` call's args.
- [ ] Unquote-splicing (`~@expr`) lowers to a vector-flatten step in the parent's args construction. The expression's value must be a vector of syntax objects at runtime.
- [ ] Nested `syntax-quote` (e.g. `syntax-quote {syntax-quote ~x}`) compiles correctly: the outer one treats the inner as a literal-ish subtree, but unquotes inside the inner are still subexpressions of the outer level. (This is the same as Scheme's quasiquote nesting.)
- [ ] The new compilation rule is **only used by US-008**'s staged path. Existing macros routed through the old expander are unaffected. Add a feature flag or compilation context bit that selects the new rule.
- [ ] Add tests: a hand-written defmacro whose body is a `syntax-quote` expression compiles, runs as a closure, and produces a syntax object whose structure matches expectations. Cover: literal substitution, `~`, `~@`, nested quote.
- [ ] Build passes, all tests pass.

### US-008: Staged macro expansion path with per-macro routing flag
**Description:** As the macro expander, I need a parallel staged-evaluation expansion path that compiles macro bodies as closures and invokes them via `jacl_ctx_run_closure`, gated by a per-macro flag so that existing macros continue using the template substituter.

**Acceptance Criteria:**
- [ ] Add a `bool use_staged_eval` field to `MacroEntry` in `src/compiler.c:2189` / `src/jacl.h:843`. Default false.
- [ ] Modify `expand__node` in `src/syntax.c:1416` to check `entry->use_staged_eval` per macro call. If false: existing template-substitution path. If true: new staged path.
- [ ] Implement the new staged path: at expansion time, ensure `entry->closure` is compiled (compile body via the new `syntax-quote`-as-expression rule from US-007), convert caller args to syntax-object `JaclVal`s, invoke `jacl_ctx_run_closure(ctx, entry->closure, args, arg_count, &err)`, expect a syntax-object result, convert back to AST via existing `syntax_to_ast`, replace the call site, and re-expand.
- [ ] Macro body compilation now happens **interleaved with expansion**, not in a later compile pass. When a `defmacro` with `use_staged_eval=true` is encountered, compile its body immediately (using the in-progress `jacl_context_t`) before continuing AST traversal.
- [ ] Forward references (a staged macro that calls another staged macro defined later in the same file) are handled by a two-pass approach: pre-register all defmacro names with stub entries, then compile bodies in order, then expand calls.
- [ ] Add a test-only mechanism to set `use_staged_eval=true` on a defmacro (e.g. a `defmacro :staged` parser variant or a magic comment). Real syntax-level toggle is not needed — flag exists only for migration.
- [ ] Add at least one test macro that uses the staged path end-to-end and produces correct output. Compare against the same macro on the old path; outputs must match.
- [ ] Hygiene is **not yet** implemented in the staged path in this story (US-009 follows). Test macros in this story must be hygiene-trivial (no shadowing).
- [ ] Build passes, all existing tests pass (none use the staged path yet).

### US-009: Mark-propagation hygiene at make-syntax call time
**Description:** As the staged expansion path, I need to apply scope marks to syntax objects constructed during a macro invocation, so that hygiene works the same as the template-substitution path.

**Acceptance Criteria:**
- [ ] Implement the mark-propagation rule validated in US-001's spike: the executing macro call has a `current_mark` field in its `CallFrame` (or in the `jacl_context_t`); every `make-syntax` invocation during that frame applies the mark to the output; spliced-in arg syntax objects pass through unchanged with their existing (caller's) marks.
- [ ] `^`-prefixed identifiers in source (set as `is_caret=1` on the AST/syntax) opt out of mark application (the current mark is **not** applied to caret-marked nodes).
- [ ] Update all `OP_SYNTAX_OP` make-syntax subops (`src/vm.c:5979-6107`) to consult the current frame's mark and apply it.
- [ ] When `jacl_ctx_run_closure` is invoked from the expander, it receives a `current_mark` parameter (allocated fresh per macro call by `expand__scope_counter` or its successor); this mark is set on the outermost frame.
- [ ] Migrate **all** `test_m10` hygiene tests to `use_staged_eval=true` and verify byte-identical output against the template-substitution path.
- [ ] Add a regression test for any divergence found in the spike (US-001) that the new rule was updated to handle.
- [ ] Build passes, test_m10 passes on the staged path, all other tests still pass on the old path.

### US-010: gensym becomes a real VM builtin
**Description:** As a macro author, I want `gensym` to be a normal callable function from any macro body, instead of a hard-coded special form detected by the template substituter.

**Acceptance Criteria:**
- [ ] Add a new `OP_SYNTAX_OP` subop (e.g. subop 15) for `gensym` that takes a prefix string and returns a fresh syntax var-ref with the macro's current scope mark applied.
- [ ] Move the gensym counter from the file-static `syntax__gensym_counter` (`src/syntax.c:1132`) into `jacl_context_t` so it's owned by the execution context.
- [ ] The new builtin uses the same name-generation logic as `jacl_gensym_next` (now allowed longer prefixes thanks to US-004): `prefix + "__" + decimal_digits(counter)`.
- [ ] Add compiler emission for `gensym` as a builtin call (not as an AST_COMMAND going through the special-form detection path).
- [ ] Add at least one macro on the staged path that calls `gensym` directly via the builtin and verifies it works (the resulting var-ref is correctly bound and shadow-free).
- [ ] The old `expand__is_gensym_call` (`src/syntax.c:1181`) and template-level gensym detection still exist for the template-substitution path (deleted in US-013).
- [ ] Build passes, all tests pass.

### US-011: Migrate test_m10 hygiene tests to staged path
**Description:** As the migration, I need every test in test_m10 (the hygiene suite) to pass on the staged path, since hygiene is the highest-risk area.

**Acceptance Criteria:**
- [ ] Every defmacro in `test/test_m10.c` is annotated with `use_staged_eval=true` (via the test-only mechanism from US-008).
- [ ] All test_m10 tests pass without modification to the assertions.
- [ ] If any test reveals a hygiene divergence: fix the mark-propagation rule in US-009's implementation (not the test). Document the fix.
- [ ] Both paths (old and new) coexist; test_m11/m12/m13/test_syntax.c still use the old path.
- [ ] Build passes, full test suite passes.

### US-012: Migrate remaining macro tests to staged path
**Description:** As the migration, I need every macro across test_m11, test_m12, test_m13, and test_syntax.c to pass on the staged path, so that the old expander becomes deletable.

**Acceptance Criteria:**
- [ ] Every defmacro across `test/test_m11.c`, `test/test_m12.c`, `test/test_m13.c`, and `test/test_syntax.c` is annotated with `use_staged_eval=true`.
- [ ] All tests pass without modification to assertions.
- [ ] Any divergence from the old path is treated as a bug in the new implementation, not the test. Document each fix.
- [ ] At the end of this story, **zero** macros are routed through the old `expand__subst_template` path.
- [ ] Build passes, full test suite passes.

### US-013: Delete old expansion infrastructure
**Description:** As the implementer, I want to delete `expand__subst_template` and all dead infrastructure once every test passes on the staged path, so that the codebase has one expansion model, not two.

**Acceptance Criteria:**
- [ ] Delete `expand__subst_template` (`src/syntax.c:1232-1376`, ~145 LOC).
- [ ] Delete `expand__is_gensym_call` (`src/syntax.c:1181-1220`, ~40 LOC) and all callers.
- [ ] Delete `expand__stamp_syntax` (`src/syntax.c:1016-1058`) and the file-static `expand__scope_counter` (replaced by per-context counter from US-010).
- [ ] Delete the `use_staged_eval` field from `MacroEntry` and the per-macro routing branch in `expand__node` — there's only one path now.
- [ ] Delete the old gensym path: `jacl_gensym_next` (or repurpose if shared with the new builtin).
- [ ] Delete any test-only flag mechanism added in US-008 (the `defmacro :staged` parser variant or magic comment).
- [ ] Update `DESIGN.md`'s "Known Limitations" section: remove the "Macros cannot run interpreted code at expansion time" entry. Update any related text in the M15 milestone description.
- [ ] Update `DESIGN.md`'s "Known Limitations": remove the 7-byte name limit entry as well (resolved by US-002/003).
- [ ] Build passes, full test suite passes, no compiler warnings about dead code.

## Functional Requirements

- **FR-1:** Macro bodies are compiled as bytecode closures and invoked on the VM during expansion. No template substitution in the final state.
- **FR-2:** `syntax-quote` is compiled as an expression that constructs a syntax object at runtime via `make-syntax` calls. `~` and `~@` are normal subexpression evaluation and splicing.
- **FR-3:** A `jacl_context_t` struct owns all state needed for one compile+run cycle: arena, GC heap, intern table, macro table, VM state, restriction set. Contexts can be nested (parent + child).
- **FR-4:** Internal API `jacl_ctx_run_source` and `jacl_ctx_run_closure` execute source or pre-compiled closures inside a context and return values. Errors propagate via out-param. The expander uses `jacl_ctx_run_closure` to invoke macro bodies.
- **FR-5:** Variable names of arbitrary length up to 128 bytes are accepted at all binding sites (`def`, `mut`, `set!`, proc params, destructure, `for`, `try`, etc.). Names > 128 bytes are rejected with a clear error.
- **FR-6:** `Local`, `Upvalue`, and `GlobalArity` continue to store names as `JaclVal` and compare with `==`. Pointer stability of interned strings makes this work without struct layout changes.
- **FR-7:** `jacl_inline_string` is a precondition-checked function: passing `len > 7` is a programmer error, not silent truncation.
- **FR-8:** `gensym` is a real VM builtin (`OP_SYNTAX_OP` subop) that takes a prefix string and returns a fresh syntax var-ref with the current macro call's scope mark. The gensym counter lives on `jacl_context_t`, not as a file-static.
- **FR-9:** Hygiene is implemented by per-macro-call `current_mark` propagated through the executing frame, applied by `make-syntax` to constructed syntax objects, and **not** applied to spliced-in args (they retain caller marks) or `^`-prefixed identifiers (intentional introduction).
- **FR-10:** Macros are migrated from old to new path one test file at a time via a per-macro routing flag. Old infrastructure is deleted only after every macro passes on the new path.
- **FR-11:** `vm_exec` is reentrancy-safe: invoking it from within an opcode handler is supported and tested.
- **FR-12:** `expand__subst_template`, `expand__is_gensym_call`, `expand__stamp_syntax`, the file-static gensym counter, and the per-macro routing flag are deleted at the end of the migration. Single expansion path, no dead code.

## Non-Goals

- **No user-facing `[interpret $src]` builtin.** The internal `jacl_ctx_run` API is built and tested, but no JACL-level builtin is exposed. That ships in a future M14 PRD.
- **No real sandboxing or capability system.** The `restriction_set` parameter is plumbed as an opaque uint64 with no enforcement beyond a no-op pass-through. Real restriction logic ships when `interpret` does.
- **No performance optimization beyond status quo.** Macro expansion runs the VM, which is heavier than tree-rewriting, but macros are small and rare. If a performance regression shows up in the test suite (>2× slowdown on `make test`), it's a bug to fix; otherwise no tuning.
- **No cross-context GC handles.** Long-lived references that survive child-context teardown are not supported in this PRD. The macro expansion model is "create context, run closure, extract result, destroy context."
- **No new macro features.** This PRD is about making the existing macro semantics work via real evaluation. Pattern matching, syntax-rules, conditional expansion, procedural macros, etc. are unlocked by this work but not implemented here.
- **No rope support for variable names.** Names are restricted to inline (≤7) + interned (8–128). Rope strings (>128) are not pointer-stable and would break the resolve-loop comparison contract. 128 bytes is enough for any sane identifier.
- **No changes to bytecode operand encoding.** `OP_GET_GLOBAL` and friends already use a 16-bit name pool index, not inline strings; that representation is unchanged.

## Technical Considerations

**Sequencing constraints (must be respected):**

1. US-001 (hygiene spike) blocks everything else. If the spike fails to find a working mark-propagation rule, the PRD is re-evaluated before any code changes.
2. US-002, US-003, US-004 (the name-limit prerequisites) must land before US-009 (hygiene implementation), because the new mark-propagation code will want longer internal names.
3. US-005, US-006 (jacl_context_t + jacl_ctx_run) must land before US-008 (staged path) — the staged path is implemented in terms of these APIs.
4. US-007 (syntax-quote as expression) must land before US-008 (staged path) — the staged path needs the new compilation rule.
5. US-009 (hygiene) must land before US-011 (migrate test_m10) — hygiene tests are the first thing migrated.
6. US-013 (delete old infrastructure) is the last story; everything else is blocking.

**Shared substrate decision (see `project_jacl_context.md`):**
The `jacl_context_t` abstraction is deliberately designed for two consumers: the macro expander (now) and M14's `interpret` builtin (later). Resist shortcuts that couple the type to macro-specific concerns. Scope marks, gensym counters, and macro tables are macro-layer concerns and should live **on top of** the context, not inside the context struct itself. (Exception: gensym counter is moved into the context per US-010 because it's shared state that needs to outlive any single macro call. Acceptable on the grounds that even runtime `interpret` could legitimately want a fresh-symbol generator.)

**Relevant existing code (do not duplicate or rewrite):**
- `MacroEntry` already has a `closure` field (`src/compiler.c:2189`) that's compiled but never invoked — the staged path **uses** this existing field rather than adding a new one.
- All `make-syntax` and introspection opcodes (`OP_SYNTAX_OP` subops 0–14) already exist from US-015/US-016/US-017 in `src/vm.c:5888-6224`. The new compilation rule from US-007 emits these existing opcodes.
- `syntax_from_ast` and `syntax_to_ast` already exist (`src/syntax.c:27-310`) for converting between AST and syntax-object representations. The expansion boundary uses these unchanged.
- Three-tier string routing (`jacl_string_new` in `src/string.c:614`) already routes strings to inline / interned / rope based on length. The compiler's binding sites use this existing function for names.

**Risks to watch:**
- **Hygiene correctness** is the single biggest risk. US-001's spike is the mitigation; if it doesn't produce a working rule, do not proceed.
- **Phase ordering for forward-referenced staged macros.** If `defmacro A` calls `defmacro B` and B is defined later in the file, the two-pass approach in US-008 must handle this. Add a test that exercises forward references.
- **Test reconciliation effort** is unpredictable. Budget 2–5 days for migration debugging across US-011 + US-012, even though each story is "small" by line count.
- **VM reentrancy might surface latent bugs** under recursive calls. The fix-as-you-find-them approach in US-005 may take longer than the story implies if there's significant global state. If the audit reveals a tangle, scope a follow-up PRD rather than ballooning US-005.

**Files most affected (rough estimate):**
- `src/compiler.c` — name limit fixes (~14 sites), syntax-quote compilation rule (~200–400 LOC added), per-macro routing field
- `src/syntax.c` — staged expansion path implementation, deletion of template substituter at end (~600 LOC delta)
- `src/vm.c` — `jacl_ctx_run_*` API, gensym builtin opcode, mark-propagation in make-syntax subops, reentrancy fixes
- `src/value.c` — `jacl_inline_string` precondition
- `src/jacl.h` — `jacl_context_t` definition, MacroEntry field addition/removal
- New: `src/context.h` (or similar) — context lifecycle API
- New: `.ralph/spikes/macro-eval-hygiene-spike.md` — US-001 output
- `test/test_m10.c`, `test_m11.c`, `test_m12.c`, `test_m13.c`, `test_syntax.c` — annotation only (mechanical), no test rewrites
- `DESIGN.md` — strike two "Known Limitations" entries at end

**Total estimated diff:** ~1500–2000 LOC across ~10 files, with ~600 LOC of deletions in US-013.

## Success Metrics

- Every test in test_m10, test_m11, test_m12, test_m13, and test_syntax.c passes on the new staged path with no test assertion changes.
- `expand__subst_template`, `expand__is_gensym_call`, `expand__stamp_syntax`, and the file-static gensym counter are deleted from the codebase.
- Variable names of length 8–128 bytes work at every binding site.
- `gensym "long_descriptive_prefix"` produces an identifier like `long_descriptive_prefix__N` and binds correctly inside macro expansions.
- A new test demonstrates a macro body that does compile-time arithmetic, calls a helper function, and branches on a value — none of which were possible under template substitution. (e.g. `defmacro power-of-two? {n} { if [is-pow2? ~n] { syntax-quote $true } { syntax-quote $false } }`.)
- `vm_exec` is verifiably reentrant: a test invokes it recursively from within an opcode handler and both calls complete correctly.
- `DESIGN.md`'s "Known Limitations" section has both the macro-eval and 7-byte-name entries removed.
- The internal `jacl_ctx_run_source` API is callable, tested, and ready for M14's `interpret` builtin to wrap with no further substrate changes.

## Open Questions

- **Migration unit of granularity.** The plan migrates one test file at a time (US-011, US-012). Should we go finer — one macro at a time within a file — to bisect more precisely? Probably not (overhead of toggling per macro is annoying), but worth keeping in mind if a file's worth of failures becomes hard to debug.
- **What does `defmacro :staged` syntactically look like?** US-008 calls for a test-only mechanism to set `use_staged_eval=true`. Options: a `:staged` keyword on `defmacro`, a magic comment, a global compiler flag with allowlist, or programmatic setting from C in the test harness. The cleanest is probably a programmatic test-harness hook (no parser changes), but this is decided in US-008.
- **Forward references across files.** US-008 handles forward refs within a file via two-pass. Forward refs across files (file A's macro calls file B's macro defined in a later imported file) are blocked on M14 (modules). For now, all macros must be defined before use, in single-file programs.
- **What's the right name for the gensym counter on `jacl_context_t`?** Naming bikeshed; defer to whoever writes US-010.
- **Does the spike (US-001) need its own retro/decision before US-002 begins?** If the spike succeeds cleanly: no, just proceed. If it surfaces unexpected complexity: yes, take a beat to reconsider scope. Trust the implementer's judgment.
- **Should we add a `JACL_DEBUG_MACRO_TRACE` env var that prints which path each macro takes during the migration?** Probably yes, makes US-011/US-012 debugging much faster. Not blocking; add if useful.
