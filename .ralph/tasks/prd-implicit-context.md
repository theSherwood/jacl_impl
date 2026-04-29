# PRD: Implicit Context ($ctx)

## Introduction

Add an implicit, dynamically-scoped context (`$ctx`) to JACL. `$ctx` is a pool-allocated struct that is implicitly available in every proc. It carries ambient state (working directory, user-declared fields) and propagates through calls without explicit parameter passing. Forking is always explicit via `with-ctx`.

`$ctx` is a real `StructTypeDef` in the struct registry — not a special type. It happens to be pool-allocated (free-list of GC-managed `OBJ_STRUCT` objects) and implicitly available. The compiler knows its type at compile time and resolves field access to byte offsets. Per-field mutability is enforced at compile time.

## Goals

- Introduce per-field mutability (`is_mutable`) on `StructTypeField`, enforced at compile time
- Add `ctx` declaration syntax for declaring context fields (per-field, from any module)
- Assemble all `ctx` declarations into a single `StructTypeDef` during compilation
- Provide a pool of GC-managed ctx struct objects with lock-free free-list for fast fork/free
- Make `$ctx` implicitly available via `vm->ctx` register (sync) and SM state field (async)
- Implement `with-ctx` as the explicit forking primitive
- Integrate with `spawn` (fork ctx at spawn time)
- Require all ctx fields to have compile-time constant defaults
- Pre-declare built-in `pwd` field (str, mutable, default = process CWD)

## User Stories

### US-001: Per-field mutability on StructTypeField

**Description:** As a developer, I need the struct type system to track per-field mutability so that the compiler can reject writes to immutable fields.

**Acceptance Criteria:**
- [ ] Add `bool is_mutable` (or a flags field containing mutability) to `StructTypeField`
- [ ] All existing struct fields default to `is_mutable = false` (no behavior change for existing code)
- [ ] Compiler emits an error when `set $s->field value` targets a field with `is_mutable = false`
- [ ] Error message is clear: "cannot mutate immutable field 'name' on struct 'TypeName'"
- [ ] Fields marked `is_mutable = true` can be written via `OP_STRUCT_SET` as usual
- [ ] Mutability check applies to both heap structs and inline structs
- [ ] Existing struct tests pass unchanged (all fields are immutable by default)
- [ ] New tests verify: immutable field write rejected, mutable field write accepted
- [ ] Build passes

### US-002: `ctx` declaration — AST and parser

**Description:** As a developer, I need the parser to recognize `ctx` field declarations at the top level so that modules can contribute fields to the implicit context.

**Acceptance Criteria:**
- [ ] New AST node type `AST_CTX_DECL` with fields: `is_mutable` (bool), `type_name` (string), `field_name` (string), `default_expr` (AST node, required)
- [ ] Parser recognizes top-level `ctx [mut] Type name = default_expr` syntax
- [ ] `mut` keyword is optional — present means mutable, absent means immutable
- [ ] Default expression is required (compile error if missing)
- [ ] Default expression must be a compile-time constant: literal (int, float, bool, string), or struct constructor with all-literal args
- [ ] Non-constant default expression produces compile error: "ctx field default must be a compile-time constant"
- [ ] Multiple `ctx` declarations in one file are allowed (each adds a field)
- [ ] `ctx` declarations are only valid at module top level (error inside proc/block)
- [ ] Parser tests verify: valid declarations parse correctly, invalid syntax rejected
- [ ] Build passes

### US-003: Ctx field collection and struct registration

**Description:** As a developer, I need the compiler to collect all `ctx` declarations across modules and register a single ctx `StructTypeDef` so that field access can be resolved at compile time.

**Acceptance Criteria:**
- [ ] Compiler maintains a `CtxFieldList` (or similar) that accumulates fields during compilation
- [ ] When processing `AST_CTX_DECL`, compiler adds the field to the list (name, type, mutability, default value)
- [ ] Duplicate field name (from any module, even same type) produces compile error: "ctx field 'name' already declared in module 'X'"
- [ ] After all modules are compiled, a single `StructTypeDef` is registered in the `StructTypeRegistry` with all collected fields
- [ ] Built-in fields (`pwd`: mut str, default = process CWD) are pre-populated before user code is compiled
- [ ] The ctx type_idx is stored in a well-known location accessible to the compiler and VM
- [ ] Field offsets follow C ABI layout rules (same as existing struct offset computation)
- [ ] `is_value_type` on the ctx StructTypeDef is `false` (it contains reference fields like str)
- [ ] Tests verify: single field, multiple fields from one module, fields from multiple modules, duplicate detection
- [ ] Build passes

### US-004: Ctx pool — GC-managed free-list

**Description:** As a developer, I need a pool of pre-allocated ctx struct objects with a lock-free free-list so that `with-ctx` forking is fast and GC-safe.

**Acceptance Criteria:**
- [ ] Ctx structs are allocated as `OBJ_STRUCT` GC objects (have `GCHeader`, participate in normal GC tracing)
- [ ] `JaclCtxPool` struct holds: atomic free-list head pointer, struct_size, type_idx, pre-allocation count
- [ ] `ctx_pool_init(pool, type_idx, struct_size, pre_alloc_count)` allocates N ctx structs via `gc_alloc` and links them in the free-list
- [ ] `ctx_pool_alloc(pool)` pops from free-list (CAS on head); if empty, allocates a fresh GC object via `gc_alloc`
- [ ] `ctx_pool_free(pool, ctx)` clears reference fields (prevents stale references from keeping objects alive) then pushes back to free-list (CAS)
- [ ] Free-list link uses a `next` pointer stored at a fixed offset in the struct's `data[]` (overlapping with field data — only valid when in free-list)
- [ ] Pool is thread-safe: lock-free CAS operations for alloc/free
- [ ] GC traces pool entries normally (they're regular GC objects) — entries on the free-list are kept alive by the pool's head pointer (pool head is a GC root)
- [ ] Tests verify: alloc/free round-trip, concurrent alloc/free from multiple threads, pool growth on exhaustion
- [ ] Build passes

### US-005: `vm->ctx` register and GC root integration

**Description:** As a developer, I need the VM to carry the current ctx pointer as a register so that procs can access it efficiently, and the GC must trace through it.

**Acceptance Criteria:**
- [ ] Add `JaclVal ctx` field to the VM struct (holds tagged pointer to ctx struct)
- [ ] `vm->ctx` is included in GC root enumeration: added to `gc_enumerate_roots()` for concurrent GC and to `gc_mark()` for single-threaded GC
- [ ] On program startup, runtime allocates the initial ctx from the pool, populates defaults (pwd = process CWD via `getcwd()`), and sets `vm->ctx`
- [ ] For multi-worker runtime: each worker's VM gets its own `vm->ctx` initialized from the initial ctx (pointer copy — shared until first fork)
- [ ] `vm->ctx` is never NULL during program execution
- [ ] Tests verify: vm->ctx is set at startup, GC doesn't collect it, GC doesn't collect strings referenced by ctx fields
- [ ] Tests verify: after GC cycle, ctx fields (including string pwd) remain valid
- [ ] Build passes

### US-006: `$ctx` variable resolution

**Description:** As a developer, I need the compiler to resolve `$ctx` as a special variable that reads from `vm->ctx` so that user code can access the implicit context.

**Acceptance Criteria:**
- [ ] `$ctx` is recognized by the compiler as a reserved name (not declarable via `def`/`mut`)
- [ ] Attempting to shadow `$ctx` with `def ctx ...` or `mut ctx ...` produces compile error: "'ctx' is reserved"
- [ ] Reading `$ctx` emits a new opcode `OP_GET_CTX` that pushes `vm->ctx` onto the stack
- [ ] `$ctx` has a known type (the ctx struct type_idx) — compiler tracks this for field access resolution
- [ ] `$ctx` works in all contexts: top-level, inside procs, inside lambdas, inside blocks
- [ ] Tests verify: `$ctx` is accessible, has correct type, field access resolves, shadowing rejected
- [ ] Build passes

### US-007: `$ctx->field` read access

**Description:** As a developer, I need `$ctx->field` to resolve field offset at compile time so that reading ctx fields is a single struct field read.

**Acceptance Criteria:**
- [ ] `$ctx->field` compiles to: `OP_GET_CTX` + `OP_STRUCT_GET` with compile-time byte offset from the ctx StructTypeDef
- [ ] If field name doesn't exist on ctx, compile error: "no field 'name' on ctx"
- [ ] Chained access (`$ctx->field->subfield`) works if the field is itself a struct type
- [ ] The result type is known at compile time (from StructTypeField.type)
- [ ] Tests verify: read built-in pwd field, read user-declared fields, read nested struct field, unknown field rejected
- [ ] Build passes

### US-008: `set $ctx->field value` — mutable field writes

**Description:** As a developer, I need to write to mutable ctx fields so that procs can update ambient state like pwd.

**Acceptance Criteria:**
- [ ] `set $ctx->field value` compiles to: `OP_GET_CTX` + compile value + `OP_STRUCT_SET` with byte offset
- [ ] Compiler checks `is_mutable` on the target field; rejects writes to immutable fields
- [ ] Write barrier is fired when updating a reference-type field (str, etc.) during active GC — push old value to grey buffer
- [ ] Type checking: value must match the field's declared type
- [ ] Tests verify: write to mut field succeeds, write to immutable field rejected, type mismatch rejected, write barrier fires during GC
- [ ] Build passes

### US-009: `with-ctx` — explicit forking

**Description:** As a developer, I need `with-ctx` to create a forked copy of the current ctx with field overrides so that scoped context changes are possible.

**Acceptance Criteria:**
- [ ] Syntax: `with-ctx {field1 expr1, field2 expr2} { body }` — first arg is command-mode block of field-value pairs, second arg is body block
- [ ] Semantics: allocate new ctx from pool, `memcpy` current ctx data into it, evaluate and apply field overrides, swap `vm->ctx` to new ctx, execute body, restore old `vm->ctx`, free new ctx back to pool
- [ ] Field overrides can target both mutable and immutable fields (immutability prevents in-place mutation, but fork creates a new instance)
- [ ] Invalid field name in overrides produces compile error
- [ ] Type mismatch in override value produces compile error
- [ ] SATB write barrier is fired when saving old ctx (push old ctx value to grey buffer) to protect from concurrent GC
- [ ] `with-ctx` is an expression — returns the value of the body block
- [ ] Nested `with-ctx` blocks work (stack of saves/restores)
- [ ] `with-ctx` with zero overrides (just `with-ctx {} { body }`) is valid — creates a fork with no changes (isolation)
- [ ] Tests verify: field override visible inside body, original restored after body, nested forks, expression return value, GC safety during with-ctx
- [ ] Build passes

### US-010: SM integration — ctx across suspensions

**Description:** As a developer, I need the ctx pointer to be preserved across state machine suspension points so that async procs maintain their ctx.

**Acceptance Criteria:**
- [ ] Compiler adds an implicit `__ctx` field to every SM-compiled proc's `StateLayout` (width=1, it's a JaclVal pointer)
- [ ] On SM entry (resume_point == 0): emit `OP_GET_CTX` + `OP_SET_STATE_FIELD` to save vm->ctx into the __ctx state field
- [ ] On SM resume (resume_point > 0): emit `OP_GET_STATE_FIELD` + `OP_SET_CTX` to restore vm->ctx from the state field
- [ ] `OP_SET_CTX` is a new opcode: pops a value from the stack and stores it in vm->ctx
- [ ] `$ctx->field` access inside SM-compiled procs works correctly (reads current vm->ctx, which was loaded from state field)
- [ ] `with-ctx` inside an SM-compiled proc works correctly across suspension points (fork is saved/restored via normal state field or local save logic)
- [ ] GC traces the __ctx state field normally (it's a JaclVal in SM fields[], already traced by OBJ_STATE_MACHINE handler)
- [ ] Tests verify: ctx access before/after await, ctx access before/after yield, with-ctx spanning await, parallel bodies see correct ctx
- [ ] Build passes

### US-011: Spawn integration — ctx fork at spawn time

**Description:** As a developer, I need `spawn` to automatically fork the current ctx so that spawned tasks get their own independent copy.

**Acceptance Criteria:**
- [ ] When `OP_SPAWN` executes, the runtime allocates a new ctx from the pool and copies the current vm->ctx data into it
- [ ] The spawned task's SM __ctx field is initialized with the forked ctx
- [ ] Mutations to ctx in the spawned task do not affect the spawning task's ctx
- [ ] Mutations to ctx in the spawning task (after spawn) do not affect the spawned task's ctx
- [ ] When the spawned task completes, its forked ctx is freed back to the pool
- [ ] `parallel` and `race` bodies also fork ctx (each body gets an independent copy)
- [ ] Tests verify: spawned task sees snapshot of ctx at spawn time, mutations in spawned task isolated, mutations in parent isolated, ctx freed on task completion
- [ ] Build passes

### US-012: Built-in `pwd` field and initial ctx

**Description:** As a developer, I need the built-in `pwd` field pre-declared and the initial ctx populated at program startup so that the context system is immediately usable.

**Acceptance Criteria:**
- [ ] `pwd` is pre-declared as a built-in ctx field: type `str`, mutable, default = process CWD
- [ ] Built-in fields are added to the ctx field list before any user module is compiled
- [ ] The initial ctx (allocated at startup) has `pwd` set to the result of `getcwd()`
- [ ] `$ctx->pwd` returns the current working directory as a string
- [ ] `set $ctx->pwd "/some/path"` updates the pwd field (mutable)
- [ ] `with-ctx {pwd "/other/path"} { ... }` forks with a different pwd
- [ ] User code cannot re-declare `pwd` as a ctx field (duplicate detection catches it)
- [ ] Tests verify: pwd is accessible at startup, pwd matches actual CWD, pwd is writable, with-ctx overrides pwd, duplicate pwd declaration rejected
- [ ] Build passes

### US-013: End-to-end integration tests

**Description:** As a developer, I need comprehensive integration tests that verify the full ctx system works together, including interactions between features.

**Acceptance Criteria:**
- [ ] Test: user-declared ctx field + with-ctx + proc calls (verify propagation and isolation)
- [ ] Test: multiple ctx fields from different modules (verify all accessible, no conflicts)
- [ ] Test: with-ctx in async proc spanning await (verify ctx preserved across suspension)
- [ ] Test: spawn + with-ctx interaction (spawned task inherits ctx from before with-ctx if spawned before, from inside with-ctx if spawned inside)
- [ ] Test: nested with-ctx (3+ levels deep) with different field overrides at each level
- [ ] Test: ctx field that holds a struct value (not just primitives/strings)
- [ ] Test: GC stress test — allocate many ctx forks in a loop, verify no memory leaks, no use-after-free, strings stay alive
- [ ] Test: concurrent stress — multiple workers spawning tasks with ctx forks, verify no corruption
- [ ] Test: with-ctx as expression (return value propagates correctly)
- [ ] Test: error propagation through with-ctx (error in body restores old ctx correctly)
- [ ] Build passes, all tests pass under both single-threaded and concurrent runtime modes

## Functional Requirements

- FR-1: `StructTypeField` has `is_mutable` flag; compiler rejects writes to immutable fields
- FR-2: `ctx [mut] Type name = default` syntax declares a context field at module top level
- FR-3: All ctx declarations are assembled into one `StructTypeDef` registered in the `StructTypeRegistry`
- FR-4: Duplicate ctx field names (even same type, even same module) produce a compile error
- FR-5: All ctx fields must have compile-time constant default values
- FR-6: Ctx structs are `OBJ_STRUCT` GC objects managed in a lock-free free-list pool
- FR-7: `vm->ctx` holds the current context; it is a GC root traced by both concurrent and single-threaded GC
- FR-8: `$ctx` is a reserved name that resolves to `vm->ctx`
- FR-9: `$ctx->field` compiles to `OP_GET_CTX` + `OP_STRUCT_GET` with compile-time byte offset
- FR-10: `set $ctx->field value` checks `is_mutable`; fires write barrier for reference fields during active GC
- FR-11: `with-ctx {overrides} { body }` allocates from pool, copies data, applies overrides, runs body, restores, frees to pool
- FR-12: `with-ctx` fires SATB write barrier when saving old ctx to protect from concurrent GC
- FR-13: SM-compiled procs store/restore vm->ctx in an implicit `__ctx` state field across suspensions
- FR-14: `spawn`, `parallel`, and `race` fork ctx (pool alloc + memcpy) for each body
- FR-15: Forked ctx is freed to pool when the fork scope exits (with-ctx block end, task completion)
- FR-16: Built-in `pwd` field is type `str`, mutable, default = `getcwd()` at startup
- FR-17: `with-ctx` is an expression that returns the value of the body block

## Non-Goals

- `cd` command (deferred — later sugar over ctx mutation)
- `with-dir` / `with-env` convenience macros (deferred)
- `$env` atom and OS environment sync (deferred)
- Wiring `$ctx->pwd` to `!cmd` child process CWD (deferred)
- Generalizing per-field mutability declaration syntax to user-declared structs (deferred — this PRD only adds the `is_mutable` flag internally and enforces it for ctx)
- Atom listeners (separate feature)
- Callable values (separate feature)
- User-defined struct constructors with mutable fields (future PRD)
- `$ctx` in `interpret` sandboxed eval (future — needs design for capability restriction)

## Technical Considerations

- **GC safety of pool:** Pool entries are real GC objects (`OBJ_STRUCT` with `GCHeader`). The pool's free-list head pointer must be a GC root so that pooled-but-unused entries are kept alive. When returning a ctx to the pool, clear all reference-type fields (set to JACL_NIL) to avoid keeping stale strings/collections alive unnecessarily.
- **Write barrier on ctx swap:** When `with-ctx` saves the old `vm->ctx` (to restore later), the old value must be pushed to the grey buffer (SATB barrier). Without this, if concurrent GC has already scanned `vm->ctx` and finds the new ctx there, the old ctx (now only on the VM stack) could be collected.
- **Free-list link placement:** The `next` pointer for the free-list can be stored at byte 0 of the struct's `data[]` region (overlapping with field data). This is safe because the link is only valid when the ctx is in the free-list (not in use). When allocated, the data is overwritten with memcpy of the source ctx.
- **SM __ctx field:** The implicit `__ctx` state field is added at a fixed index (e.g., always the last field in the StateLayout). The compiler hard-codes this index for OP_GET/SET_STATE_FIELD emission. This avoids name collision with user state fields.
- **Module compilation order:** Ctx field declarations are processed during module compilation. Since JACL uses depth-first `use`-based compilation, a module's ctx declarations are registered before any module that imports it is compiled. This ensures field offsets are stable when accessed by downstream modules.
- **Pool pre-allocation:** Start with 8 pre-allocated ctx objects. This covers common nesting depths (with-ctx rarely goes deeper than 4-5 levels, and a few concurrent tasks). On exhaustion, fall back to `gc_alloc`.
- **Initial ctx ownership:** The initial ctx is allocated from the pool at program startup. It is never freed back to the pool — it lives for the duration of the program. Its reference fields (pwd string) are traced normally via `vm->ctx` root.
- **Concurrent GC root enumeration:** Add one line per worker in `gc_enumerate_roots()` to push `worker->vm.ctx` into the root set. This is O(num_workers) additional work per GC cycle — negligible.

## Success Metrics

- `$ctx->pwd` accessible from any proc without explicit parameter passing
- `with-ctx` fork/free adds at most one `gc_alloc` call per fork (pool hit = zero allocation)
- Per-field mutability enforced at compile time with clear error messages
- Ctx survives GC cycles — no dangling pointers to collected strings
- Spawn correctly isolates ctx — mutations in spawned task invisible to parent
- SM-compiled procs correctly preserve ctx across yield/await suspension points
- All tests pass under concurrent runtime with GC stress

## Open Questions

- **Pool sizing heuristic:** Should the pool grow permanently (never shrink) or can it reclaim entries when load decreases? Initial implementation: grow only (simpler). Revisit if memory pressure becomes an issue.
- **`$ctx` in `interpret` (sandboxed eval):** Should sandboxed code see the caller's ctx? Probably not — sandbox should get a restricted ctx. Deferred to a future PRD.
- **Field ordering:** Should built-in fields always be at fixed offsets (pwd at offset 0) regardless of user declarations? Yes for now — built-in fields first, user fields after. This allows the runtime to access pwd without consulting the registry.
