# PRD: [interpret] Sandboxed Evaluation — Phases 1.5 through 3

## Introduction

Phase 1 of `[interpret $src]` shipped (commit `104c61e`) with a design compromise: interpreted code runs in an isolated `jacl_context_t` with its own heap, and results get materialized back to the parent heap. Complex values (vecs, maps, closures) can't cross the boundary. This PRD completes the feature by replacing the isolated-context approach with in-place compilation into the caller's heap, adds a map-based capability prelude so callers can supply (and restrict) which names interpreted code can resolve, and gates non-core builtins behind env lookup so the prelude actually controls capability.

The result: `[interpret $prelude $src]` compiles the source in the parent process using the parent's staged compiler (macros today, static types when they land), with name resolution scoped to the prelude map. Compile failures (unresolved names, macro expansion errors, future type errors) surface synchronously to the caller as compile-error values. Runtime failures during interpreted execution surface as runtime-error values.

## Goals

- Eliminate the Phase 1 context-isolation approach in favor of in-place compilation (Phase 1.5)
- Give callers a first-class, composable way to scope which builtins interpreted code can reach (Phase 2)
- Make the prelude mechanism actually enforce capability by downgrading non-core builtins from opcodes to env lookups (Phase 3)
- Preserve the existing `[interpret $src]` one-arg form as a permissive default that uses the full default prelude
- Keep error semantics as error-value returns (not runtime errors), consistent with Phase 1
- Position the design to layer static type checking of interpreted code onto the same machinery when types ship

## User Stories

### US-001: `source_to_closure_in_place` compile helper
**Description:** As the VM, I want a helper that lex/parse/macro-expands/compiles a source string into a `JaclClosure` allocated on a caller-supplied heap, so that `[interpret]` can produce a runnable closure without spinning up a new context.

**Acceptance Criteria:**
- [ ] New function in `src/vm.c` (or near the compiler driver): `JaclVal source_to_closure_in_place(const char *src, size_t len, arena_t *arena, ThreadHeap *heap, JaclInternTable *intern_table, ExpandState *expand, JaclError *err_out)` — returns a closure value or `JACL_NIL` with `err_out` populated
- [ ] Internally calls `lexer_lex` → `parser_parse` → `compiler_compile` using the supplied arena/heap/intern_table/expand; does NOT create a `jacl_context_t`
- [ ] The produced closure's bytecode and captured state live on the supplied heap (so the parent VM's GC sees them as live via normal root scanning)
- [ ] Parse errors populate `err_out->kind = JACL_ERROR_COMPILE` with the first `AST_ERROR` message
- [ ] Compile errors populate `err_out->kind = JACL_ERROR_COMPILE` with `cr.error_message`
- [ ] On error, no partial state leaks into the caller's heap/intern table beyond what normal compilation would (arena-scoped work, interned name entries)
- [ ] Unit test in `test/test_integration.c`: valid source compiles to a closure, calling it produces the expected value
- [ ] Unit test: parse error returns error kind with message
- [ ] Unit test: compile error returns error kind with message
- [ ] `build.sh` passes

### US-002: Rewrite OP_INTERPRET using in-place compile
**Description:** As the maintainer, I want `OP_INTERPRET` to use `source_to_closure_in_place` and call the resulting closure on the parent VM, so that we can delete the isolated-context, result-materialization, and complex-value-rejection code.

**Acceptance Criteria:**
- [ ] `OP_INTERPRET` handler in `src/vm.c` no longer calls `jacl_ctx_save`/`jacl_ctx_new`/`jacl_ctx_run_source`/`jacl_ctx_restore`/`jacl_ctx_destroy`
- [ ] Handler pops the source string, extracts bytes, calls `source_to_closure_in_place` with the parent VM's arena/heap/intern_table
- [ ] On `JaclError.kind == JACL_ERROR_COMPILE`: pushes `jacl_set_error(JACL_NIL)`, stores the message in `vm->error_message` (caller semantics preserved from Phase 1)
- [ ] On success: invokes the returned closure via the existing closure-call path (sets up frame, runs `vm__run`), result lands on the parent stack naturally
- [ ] On runtime error during interpreted execution: pushes `jacl_set_error(JACL_NIL)` with the message in `vm->error_message`
- [ ] The Phase 1 result-materialization block (the if/else ladder for i64/u64/f64/strings and complex-value error) is deleted entirely
- [ ] All 7 existing `test_interpret_*` tests in `test/test_integration.c` still pass
- [ ] New test: `[interpret]` returning a vector works (previously returned the complex-value error)
- [ ] New test: `[interpret]` returning a map works
- [ ] `build.sh` passes (all 86+ tests green)

### US-003: Compiler accepts prelude map as compile-time env seed
**Description:** As the compiler, I want to accept an optional JACL map whose keys populate the compile-time environment before I walk the AST, so that interpreted code resolves names against caller-supplied capabilities instead of the accumulated-from-source env.

**Acceptance Criteria:**
- [ ] `compiler_compile` in `src/compiler.c` gains an optional parameter `JaclVal prelude_map` (or `NULL`-sentinel via a distinguishable value like `JACL_NIL`)
- [ ] When `prelude_map` is a non-nil map: before compiling the AST, iterate the map's keys, register each as a global-scope name bound to the map's corresponding value in the compile-time environment
- [ ] Reserved keys (e.g., `:core`) are NOT registered as names — they are config flags reserved for future use (documented but not enforced in this story; default is core-always-on)
- [ ] When `prelude_map` is nil: existing behavior unchanged (env starts empty, names accumulate from source as they do today)
- [ ] Unresolved name references in the AST produce compile errors via the existing error path (`cr.error_count > 0`, message like `"undefined name 'foo'"`)
- [ ] Unit test in `test/test_compiler.c` or `test/test_integration.c`: compiling `[log 42]` with a prelude map `{"log": $my-log}` succeeds; the closure's `log` reference resolves to `$my-log`
- [ ] Unit test: compiling `[log 42]` with prelude map `{}` returns a compile error for unresolved `log`
- [ ] Unit test: prelude map entries are reachable via normal env lookup during interpreted execution (value stored under the key is the value called)
- [ ] All existing compiler and integration tests still pass
- [ ] `build.sh` passes

### US-004: `[interpret-prelude]` builtin
**Description:** As a caller of `[interpret]`, I want a builtin that returns the default permissive prelude map (including `interpret` itself), so that I can derive restricted preludes by removing keys with `map-remove` rather than enumerating every capability I want.

**Acceptance Criteria:**
- [ ] New opcode-based builtin `[interpret-prelude]` (0 args) in `src/compiler.c` — compiles to a new `OP_INTERPRET_PRELUDE` opcode
- [ ] Opcode added to `OP_INTERPRET_PRELUDE` in `src/jacl.h` and `src/bytecode.c` (enum + name mapping); `test/test_bytecode.c` updated with the new opcode position and `OP_HALT` shifted
- [ ] VM handler constructs a map containing an entry for every non-core builtin name currently recognized by the compiler, with values being either native-fn references or a sentinel callable that routes to the opcode
- [ ] `interpret` itself is included in the default map (supports nested interpret out of the box)
- [ ] Core language forms (arithmetic, comparison, control flow, binding, destructuring, immutable data construction, vec/map/set ops, string ops, syntax-quote/unquote) are NOT in the map — they remain opcode-only regardless of prelude
- [ ] Side-effecting/capability-sensitive builtins ARE in the map: `print`, `read-line`, file I/O, `spawn`, `await`, concurrency primitives, `interpret`, `make-syntax`, mutation ops (`set!`, `swap!`), module meta
- [ ] Unit test: `[interpret-prelude]` returns a map with `interpret` as a key
- [ ] Unit test: the returned map does NOT contain core names like `+`, `if`, `def`
- [ ] Unit test: `[map-contains? [interpret-prelude] "print"]` is true
- [ ] Document the exact default key list in a comment near the VM handler (for auditability)
- [ ] `build.sh` passes

### US-005: `[interpret $prelude $src]` two-arg form
**Description:** As a caller, I want to pass a prelude map alongside the source string so that interpreted code resolves names against my custom capability set.

**Acceptance Criteria:**
- [ ] `compiler__compile_command` in `src/compiler.c` accepts `interpret` with either 1 or 2 args
- [ ] 1-arg form: existing behavior (full permissive prelude is supplied implicitly — the handler calls `[interpret-prelude]` internally, or the compiler emits it)
- [ ] 2-arg form: first arg is the prelude map, second is the source string; compiler emits both, then `OP_INTERPRET` (possibly with an arity byte distinguishing 1-arg vs 2-arg, or a separate `OP_INTERPRET2` opcode — implementer's choice, document rationale in commit)
- [ ] VM handler type-checks: prelude must be a map (or nil → use default permissive), source must be a string; type mismatches produce runtime error
- [ ] The prelude map is passed through `source_to_closure_in_place` to `compiler_compile` as the env seed (uses US-003)
- [ ] Unit test: `[interpret {"x": 42} "[+ $x 1]"]` returns 43 (prelude exposes `x`, core `+` works)
- [ ] Unit test: `[interpret {} "[+ 1 2]"]` returns 3 (core always available, no prelude names needed)
- [ ] Unit test: `[interpret {} "[print \"hi\"]"]` returns a compile-error value (print not in empty prelude)
- [ ] Unit test: `[interpret [map-remove [interpret-prelude] "interpret"] "[interpret \"[+ 1 2]\"]"]` returns compile-error (nested interpret blocked)
- [ ] Unit test: `[interpret [map-set {} "log" $my-closure] "[log 42]"]` calls `$my-closure` with 42 — interpreted code uses a parent-supplied closure
- [ ] Unit test: closure in prelude sees parent's mutable state correctly (proves shared heap / shared VM semantics work)
- [ ] `build.sh` passes

### US-006: Non-core builtin opcode-vs-env downgrade in sandbox mode
**Description:** As the compiler, when compiling under a caller-supplied prelude, I want to downgrade non-core builtin references from direct opcode emission to env lookups, so that the prelude map actually controls whether `print`, `spawn`, etc. are callable.

**Acceptance Criteria:**
- [ ] Compiler gains a sandbox-mode flag, set when `prelude_map` is non-nil in `compiler_compile`
- [ ] In sandbox mode, when compiling a command whose head is a non-core builtin name, the compiler:
  - Checks the name against the prelude map's keys (compile-time)
  - If present: emits `OP_GET_GLOBAL`/env lookup + `OP_CALL` against the map entry's value
  - If absent: emits a compile error "undefined name 'foo'"
- [ ] In sandbox mode, core builtins (arithmetic, comparison, control flow, binding, destructuring, immutable data ops) emit their normal opcodes regardless of prelude contents
- [ ] In non-sandbox mode (no prelude supplied): existing behavior unchanged (all recognized builtins emit their opcodes)
- [ ] The list of core vs non-core builtins is captured in a single helper `bool compiler__is_core_builtin(const char *name, uint32_t len)` for auditability
- [ ] Unit test: `[interpret {"print": $my-print} "[print \"hi\"]"]` calls `$my-print` instead of the built-in print opcode
- [ ] Unit test: `[interpret {} "[spawn { 42 }]"]` is a compile error (spawn not in prelude)
- [ ] Unit test: `[interpret {} "[+ 1 [* 2 3]]"]` returns 7 (core arithmetic always works)
- [ ] Unit test: a macro defined inside interpreted code that references a non-core builtin gets the sandbox treatment (macro-expanded output still goes through sandbox compile)
- [ ] `build.sh` passes

### US-007: End-to-end sandboxing integration tests
**Description:** As the maintainer, I want scenario-level tests that exercise realistic sandboxing use cases, so that we have confidence the pieces work together and catch regressions.

**Acceptance Criteria:**
- [ ] New test file `test/test_interpret_sandbox.c` (or expanded section in `test_integration.c`) registered in the test runner
- [ ] Scenario test: untrusted expression evaluator — caller passes a prelude with only arithmetic callables (no I/O), runs `[interpret $safe "[+ 1 [* 2 3]]"]`, asserts result is 7 and no side effects
- [ ] Scenario test: audit-logged I/O — caller wraps `print` with a logging closure, injects it into prelude, runs interpreted code that prints, asserts both the log fires and the print output is captured
- [ ] Scenario test: nested interpret with restriction — parent interprets code that itself interprets with an even-more-restricted prelude; proves capability narrowing works transitively
- [ ] Scenario test: prelude derivation — `[map-remove [interpret-prelude] "spawn"]` produces a prelude that blocks spawn in interpreted code, and the resulting error value is detectable via `jacl_is_error`
- [ ] Scenario test: parent closure captures — prelude value is a closure that closes over parent-local state; interpreting code that calls it observes the parent's state correctly
- [ ] Scenario test: compile-time macro in interpreted code — interpreted source defines its own macro and uses it, everything resolved at the parent's staged-compilation phase
- [ ] All scenarios pass
- [ ] `build.sh` passes

### US-008: Remove or deprecate Phase 1 isolated-context path
**Description:** As the maintainer, I want the Phase 1 jacl_context-based implementation path removed once US-002 lands and all tests pass, so that there is one implementation of `[interpret]`, not two.

**Acceptance Criteria:**
- [ ] No code in `src/vm.c` or elsewhere calls `jacl_ctx_new`/`jacl_ctx_run_source`/`jacl_ctx_destroy` in service of `OP_INTERPRET`
- [ ] `jacl_context_t` APIs remain available for legitimate uses (macro-eval sibling contexts during compilation) but are no longer on the `[interpret]` hot path
- [ ] The Phase 1 integration tests (`test_interpret_basic`, `test_interpret_string_result`, `test_interpret_parse_error`, `test_interpret_runtime_error`, `test_interpret_with_print`, `test_interpret_type_error`, `test_interpret_shared_heap`) still pass unchanged
- [ ] `build.sh` passes
- [ ] No compiler warnings about dead code paths

## Functional Requirements

- FR-1: `[interpret $src]` (1 arg) evaluates source using the full default prelude (returned by `[interpret-prelude]`); backward-compatible with Phase 1 behavior from the caller's perspective except that complex values (vecs, maps, closures) now return successfully
- FR-2: `[interpret $prelude $src]` (2 args) evaluates source using the caller-supplied prelude map as the compile-time environment
- FR-3: `[interpret-prelude]` returns a map containing every non-core builtin name mapped to a callable, including `interpret` itself
- FR-4: Interpreted code runs on the parent's VM and heap; results are first-class values on the parent heap with no materialization step
- FR-5: Parse errors, macro expansion errors, and unresolved-name compile errors in interpreted source are returned to the caller as error values (`jacl_set_error(JACL_NIL)` with `vm->error_message` set); the caller's VM does not abort
- FR-6: Runtime errors during interpreted execution are returned to the caller as error values with the same convention
- FR-7: In sandbox mode (prelude provided), non-core builtins are resolved via the prelude map; core language forms (arithmetic, comparison, control flow, binding, destructuring, immutable data ops) are always available regardless of prelude
- FR-8: The compiler's sandbox-mode env seeding uses `compiler_compile`'s existing name resolution — no separate lookup path; a name in the prelude map is indistinguishable from a top-level `def` from the compiler's perspective
- FR-9: Interpreted code that uses the staged macro system works: macros defined in the interpreted source are expanded by the parent's staged compiler during OP_INTERPRET, errors surface as compile errors to the caller

## Non-Goals

- **Closure return from `[interpret]`:** interpreted code that produces a closure as its result is out of scope for this PRD; the closure may be returned, but we do not guarantee it is safely callable across module boundaries or after the interpret call site returns — to be revisited
- **Macros in the prelude:** the prelude map values are runtime callables only; exposing parent macros through the prelude (so interpreted code can invoke them at expansion time) is deferred
- **Structured error values:** this PRD keeps the Phase 1 `jacl_set_error(JACL_NIL)` + `vm->error_message` pattern; returning structured error maps (with kind/line/col fields) is out of scope
- **Static type checking of interpreted code:** the design is positioned to accept this when types land, but no type-checking code is added here
- **`:core` toggle or restricting core language ops:** core opcodes are always available; further restricting them is out of scope
- **Fine-grained per-opcode capabilities:** capabilities are at the builtin-name granularity; per-call-site capability checks are not added
- **Runtime-level sandboxing (time/memory limits):** CPU, memory, or wall-clock bounds on interpreted execution are out of scope
- **Deprecating or removing `jacl_context_t`:** it remains in use for macro-eval sibling contexts; only the `[interpret]` code path stops using it

## Technical Considerations

- The `source_to_closure_in_place` helper must respect reentrancy: `compiler_compile` already takes `arena_t*` / `ThreadHeap*` / `JaclInternTable*` / `ExpandState*`, so all required state is parameter-threaded; the helper must thread its `ExpandState` such that a `ctx` is available for staged macro eval (either reuse the parent's `ExpandState.ctx`, or spin a temporary macro-eval context per the existing pattern in `jacl_run`)
- The prelude map is a regular JACL map — we do not introduce a new data structure; `map-set`, `map-remove`, `map-contains?`, `map-keys` all work on it
- The default prelude constructed by `[interpret-prelude]` should be cached (built once, returned by reference) since it is immutable and always the same shape; construction cost matters if interpret is called in a hot loop
- The opcode-vs-env downgrade in US-006 likely requires a small lookup: given a head name, is it core? This should be a keyword-style prefix-match or a static table — cheap at compile time
- Nested interpret is intended to work by default: the default prelude includes `interpret`, and the compile path is reentrant (a compiler invocation inside another compiler invocation shares no mutable state beyond the supplied args)
- Error messages for unresolved names should include the position info from the AST node (line/col) so the caller can surface them meaningfully even though we return only the string message
- Interned names for prelude keys: building the prelude map at VM startup (or lazily on first `[interpret-prelude]`) means each key is interned once; compile-time env seeding can compare by `==` on interned pointers, not byte comparison

## Success Metrics

- All 7 existing Phase 1 interpret tests pass without modification after US-002 lands
- `[interpret]` can return vec/map values that Phase 1 could not (regression gain)
- The prelude mechanism demonstrably blocks a capability: a negative test shows `[interpret {} "[print ...]"]` produces a compile error
- The prelude mechanism demonstrably grants a capability: injecting a closure into a prelude lets interpreted code call it and observe parent state
- Zero new `jacl_context_t` instantiations on the `[interpret]` hot path (verifiable by grep after US-008)
- `build.sh` green throughout the PRD — each story lands on a green build before the next begins

## Open Questions

- How should `[interpret-prelude]` handle newly added builtins going forward? Should there be a single registry the builtin consults, or does each new builtin ship a patch to the prelude constructor? (Registry preferred for maintainability; worth a short note in the implementation comment.)
- When interpreted code defines a `def` or `defmacro`, does that leak into the parent's env/macro-table, or is it scoped to the interpret call? (Expected: scoped to the interpret call — the compile is a fresh env seeded from the prelude. Verify with a test.)
- Should there be a limit on prelude map depth/size to prevent pathological preludes? (Probably not — it's the caller's problem, document as a caveat.)
- Future compatibility: if we later add `[interpret-async $prelude $src]` that returns a future, what changes? (Should fit cleanly — the spawn machinery is already in place — but not in this PRD.)
