# PRD: M14 — Module System

## Introduction

Add a file-based module system to JACL so that programs can be split across multiple files with explicit imports. Each `.jacl` file is a module. Modules export all public names (names not prefixed with `_`) and import specific names from other modules via `use` declarations with an explicit name list: `use "math.jacl" [add sub mul]`. There is no qualified/namespace access in this initial version — all imports are by explicit name binding only.

Modules are strictly typed: the compiler tracks and enforces type and arity metadata across module boundaries. Mutable module-level bindings are exported as boxes, giving importers a live reference (not a snapshot). Modules are compiled once per program and cached as independent bytecode chunks. Circular imports are a hard compile-time error.

This is the initial module system — no search paths, no versioning, no sandboxing, no qualified access, no import renaming. Paths are relative to the importing file. Privacy is enforced via underscore-prefix convention: `_helper` is private to the defining module and cannot be imported.

## Goals

- Allow JACL programs to span multiple files with explicit, compile-time-resolved imports
- Each file compiles to its own bytecode chunk, cached and executed at most once per program
- Single import form: `use "path.jacl" [name1 name2 ...]` — explicit names only
- Strictly typed modules: arity, parameter types, and return types propagate across imports and are enforced at compile time
- Mutable bindings exported as boxes (live references)
- Enforce underscore-prefix privacy at compile time
- Detect and reject circular imports with a clear error message
- All existing M0–M13 tests continue to pass (single-file programs are valid single-module programs)

## User Stories

### US-001: Lexer — `use` keyword

**Description:** As a developer, I need the lexer to recognize `use` as a keyword so that import declarations can be parsed.

**Acceptance Criteria:**
- [ ] `use` is recognized as a keyword token (`TOKEN_USE`)
- [ ] `use` as a bare word in non-keyword positions (e.g., inside a string or as a variable name) continues to work correctly — only recognized as keyword at statement start
- [ ] Build passes, all existing lexer tests pass

### US-002: Parser — `use` declarations

**Description:** As a developer, I need the parser to recognize `use` declarations at the top of a file so that import intent is captured in the AST.

**Acceptance Criteria:**
- [ ] Parse: `use "math.jacl" [add sub mul]` → `AST_USE` node with path `"math.jacl"` and name list `[add, sub, mul]`
- [ ] `use` declarations must appear before any other statements; error if `use` appears after a `def`, `proc`, or expression
- [ ] Path must be a string literal (not a variable or interpolated string)
- [ ] Name list must contain at least one name; error on empty list `use "math.jacl" []`
- [ ] Error on duplicate `use` of the same path
- [ ] Error on duplicate names across `use` declarations (e.g., two modules both importing `add`)
- [ ] Build passes, all existing parser tests pass

### US-003: Module compilation unit — one chunk per module

**Description:** As a developer, I need each module to compile into its own independent bytecode chunk with its own constant pool and global namespace, so that modules are self-contained compilation units.

**Acceptance Criteria:**
- [ ] New struct `Module`: contains `BytecodeChunk* chunk`, `const char* path` (canonical), `const char* source`, `ExportEntry* exports` (name + type + arity + is_mutable metadata), `uint32_t export_count`, `bool compiled`
- [ ] `ExportEntry` includes: name, arity (for procs), parameter types, return type, `is_mutable` flag
- [ ] New struct `ModuleCache`: stores compiled modules keyed by canonical path, prevents re-compilation
- [ ] Compiler gains a `ModuleCache*` field and a `Module* current_module` field
- [ ] When compiling a `use` declaration, the compiler resolves the path, checks the cache, and compiles the dependency if not already compiled
- [ ] Each module's top-level bytecode chunk includes `OP_HALT` at the end
- [ ] Module compilation populates the `exports` array with all non-underscore-prefixed top-level `def`, `proc`, and `mut` bindings (including their full type signature)
- [ ] Mutable top-level bindings (`x : 0`) are exported as boxes — the export entry has `is_mutable = true` and the runtime value is a `box` containing the current value
- [ ] Build passes

### US-004: Path resolution and circular import detection

**Description:** As a developer, I need the compiler to resolve relative import paths and detect circular dependencies so that imports are deterministic and infinite loops are impossible.

**Acceptance Criteria:**
- [ ] Paths are resolved relative to the directory of the importing file: if `src/main.jacl` says `use "lib/math.jacl"`, it resolves to `src/lib/math.jacl`
- [ ] Paths are canonicalized (resolve `..`, `.`, symlinks) before cache lookup
- [ ] The compiler maintains an "in-progress" set of module paths during compilation
- [ ] If a module being compiled is already in the in-progress set, emit error: `circular import detected: A -> B -> A`
- [ ] The error message includes the full import chain for debugging
- [ ] File-not-found produces a clear error: `module not found: "path/to/missing.jacl"`
- [ ] Build passes

### US-005: Underscore-prefix privacy enforcement

**Description:** As a developer, I want names prefixed with `_` to be private to their defining module so that internal helpers cannot leak across module boundaries.

**Acceptance Criteria:**
- [ ] Top-level definitions starting with `_` (e.g., `proc _helper`, `def _internal`) are excluded from the module's export list
- [ ] Attempting to import a private name via `use "mod.jacl" [_helper]` produces compile error: `'_helper' is private to module 'mod.jacl'`
- [ ] Private names remain accessible within their own module (no restrictions on internal use)
- [ ] Build passes

### US-006: Import name resolution with strict typing

**Description:** As a developer, I need `use "math.jacl" [add sub]` to make `add` and `sub` available as direct names in the importing module with full type information so that cross-module calls are type-safe.

**Acceptance Criteria:**
- [ ] After `use "math.jacl" [add sub]`, calling `[add 1 2]` resolves to `math.jacl`'s `add` proc
- [ ] After `use "math.jacl" [add sub]`, reading `$add` resolves to `math.jacl`'s `add` value
- [ ] Importing a name that doesn't exist in the target module produces compile error: `'foo' is not exported by 'math.jacl'`
- [ ] Importing a name that conflicts with a local definition produces compile error: `'add' is already defined`
- [ ] Arity checking works across module boundaries: `[add 1]` (wrong arity) produces the standard arity error
- [ ] Type metadata fully propagates: if `math.jacl` defines `i64 add [i64 x i64 y]`, the importing module sees the typed signature and enforces it
- [ ] Calling a typed imported proc with wrong argument types produces a compile-time type error
- [ ] Importing a `def` with a known type (e.g., `def x 42` → `i32`) propagates that type to the importer
- [ ] Build passes

### US-007: Mutable imports as boxes

**Description:** As a developer, I need mutable module-level bindings to be exported as boxes so that importers get a live reference to the value, not a snapshot.

**Acceptance Criteria:**
- [ ] A module defining `counter : 0` exports `counter` as a box
- [ ] Importing `counter` via `use "state.jacl" [counter]` gives a box value
- [ ] The importer can read the current value with `[deref $counter]`
- [ ] The importer can update the value with `[reset $counter 5]` or `[swap $counter inc]`
- [ ] Changes made by the importer are visible to the defining module and other importers (shared mutable state)
- [ ] The `is_mutable` flag in the export metadata lets the compiler know this is a box, not a plain value
- [ ] Build passes

### US-008: Module initialization and runtime execution

**Description:** As a developer, I need modules to be initialized (their top-level code executed) exactly once, in dependency order, so that `def` and `proc` bindings are available before they're imported.

**Acceptance Criteria:**
- [ ] When a program starts, modules are initialized in topological order (dependencies before dependents)
- [ ] Each module's top-level chunk is executed once; subsequent imports reuse the cached results
- [ ] Module initialization executes in the VM's environment — each module gets its own namespace prefix internally (e.g., `math.jacl::add`) to avoid collisions in the flat global environment
- [ ] Side effects in module top-level code (e.g., `print "loaded"`) execute exactly once regardless of how many files import the module
- [ ] If module initialization fails (runtime error), the error propagates with context: `error in module 'math.jacl': <message>`
- [ ] Build passes

### US-009: Compiler API — multi-file entry point

**Description:** As a developer, I need a top-level compilation API that takes a root file path and produces a fully linked program (all modules compiled and dependency-ordered) so that the VM can execute multi-file programs.

**Acceptance Criteria:**
- [ ] New function: `CompileResult jacl_compile_program(const char* root_path, arena_t* arena, JaclInternTable* intern, ThreadHeap* heap)` — reads root file, recursively compiles all dependencies, returns compiled program
- [ ] `CompileResult` contains: `Module** modules` (in topological order), `uint32_t module_count`, error info
- [ ] The root file's module is always last in the topological order (executed last)
- [ ] Existing `jacl_compile()` single-source API continues to work for single-file programs
- [ ] The test harness can use the new API for multi-file test programs
- [ ] Build passes

### US-010: VM execution of multi-module programs

**Description:** As a developer, I need the VM to execute a multi-module program by initializing modules in order and making imported bindings available.

**Acceptance Criteria:**
- [ ] New function: `VMResult jacl_exec_program(CompileResult* program, VM* vm)` — initializes modules in topological order, then executes root module
- [ ] Each module's globals are stored with namespace-prefixed keys in the VM environment
- [ ] Imported names resolve to the correct namespace-prefixed globals via bytecode (the compiler emits `OP_GET_GLOBAL` with the prefixed key)
- [ ] Mutable imports resolve to the box object in the source module's namespace (shared reference)
- [ ] Concurrent mode works: modules containing `spawn`/`await`/`parallel`/`race` undergo CPS transform as before
- [ ] Existing single-file `vm_exec()` continues to work unchanged
- [ ] Build passes

### US-011: End-to-end integration tests

**Description:** As a developer, I need comprehensive integration tests covering the full module system so that correctness is verified across the pipeline.

**Acceptance Criteria:**
- [ ] Test: basic import — `use "lib.jacl" [add]` then `[add 1 2]` → 3
- [ ] Test: multiple imports from one module — `use "lib.jacl" [add sub mul]`
- [ ] Test: imports from multiple modules — `use "math.jacl" [add]` and `use "str.jacl" [concat]`
- [ ] Test: transitive imports — A imports B, B imports C; A can use B's exports (not C's directly)
- [ ] Test: diamond imports — A imports B and C, both B and C import D; D initialized once
- [ ] Test: privacy enforcement — importing `_private` name produces compile error
- [ ] Test: circular import detection — A imports B, B imports A → compile error with chain
- [ ] Test: module not found — `use "nonexistent.jacl"` → clear error
- [ ] Test: name conflict — importing a name that's already defined → compile error
- [ ] Test: unknown export — `use "lib.jacl" [nonexistent]` → compile error
- [ ] Test: arity checking across modules — wrong arg count on imported proc → compile error
- [ ] Test: typed imports — `i64` proc imported and called with wrong types → compile error
- [ ] Test: mutable import — module defines `counter : 0`, importer does `[deref $counter]` and `[reset $counter 5]`, changes visible
- [ ] Test: side effects run once — module prints on load, imported by two files via diamond, output appears once
- [ ] Test: module with concurrent code — module uses `spawn`/`await`, importing module awaits result
- [ ] All existing single-file `.jacl` tests continue to pass
- [ ] Build passes

### US-012: Test harness support for multi-file tests

**Description:** As a developer, I need the test harness to support multi-file test programs so that module tests can be written as `.jacl` files with the standard `# expect:` format.

**Acceptance Criteria:**
- [ ] New directory: `test/jacl/modules/` for multi-file test programs
- [ ] Convention: each test is a subdirectory containing `main.jacl` (entry point) and dependency files
- [ ] The test harness detects subdirectories in `test/jacl/modules/` and runs `main.jacl` as the root
- [ ] `# expect:` comments in `main.jacl` define expected output (same format as single-file tests)
- [ ] At least 10 multi-file test programs covering the scenarios in US-011
- [ ] Build passes

## Functional Requirements

- FR-1: `use "path.jacl" [name1 name2 ...]` imports specific names from a module into the current scope
- FR-2: Import paths are relative to the importing file's directory
- FR-3: Each module is compiled exactly once and cached by canonical path
- FR-4: Circular imports produce a compile-time error with the full import chain
- FR-5: Names prefixed with `_` are private and cannot be imported
- FR-6: Modules are initialized in topological (dependency-first) order at runtime
- FR-7: Module top-level side effects execute exactly once
- FR-8: Arity, parameter types, and return types propagate across module boundaries and are enforced at compile time
- FR-9: Name conflicts between imports and local definitions are compile-time errors
- FR-10: Importing a non-existent name from a module is a compile-time error
- FR-11: Mutable module-level bindings are exported as boxes (live shared references)
- FR-12: Single-file programs continue to work with no changes

## Non-Goals

- No qualified/namespace access (`math/add` syntax) — deferred to a future milestone
- No import renaming (`use "math.jacl" [add as plus]`) — deferred
- No destructuring syntax in imports — deferred (will share syntax with general destructuring)
- No search path / `JACL_PATH` environment variable (paths are purely relative for now)
- No wildcard imports (`use "math.jacl" [*]`)
- No re-exports (a module cannot re-export names it imported)
- No runtime/dynamic imports (all imports resolved at compile time)
- No module versioning or dependency management
- No sandboxing or restriction maps (deferred per DESIGN.md)
- No explicit `export` declarations (everything public unless `_`-prefixed)
- No circular import relaxation (hard error, no forward declarations)

## Technical Considerations

- **Compilation unit boundary:** Each module compiles to its own `BytecodeChunk` via its own `Compiler` instance. The `Compiler` struct gains `ModuleCache*` and `Module*` fields. The existing `global_arities[]` array is per-module; imported arities are copied in during `use` compilation.
- **Strict type propagation:** `ExportEntry` carries full type signatures (return type, parameter count, parameter types). When the importing compiler processes `use`, it registers each imported name in its `global_arities[]` with the full type info from the export entry. This makes cross-module type checking identical to intra-module type checking.
- **Mutable bindings as boxes:** When a module defines `counter : 0`, the compiler wraps it in a `box` at the module level. The export entry marks it `is_mutable = true`. The importing module's compiler knows to treat $counter as a box value, so `[deref $counter]` / `[reset $counter val]` work naturally. No new opcodes needed — the existing box/deref/reset infrastructure handles it.
- **Global environment namespacing:** The VM's flat `Environment` stores module globals with prefixed keys (e.g., `math.jacl::add`). Imported names in the importing module's bytecode emit `OP_GET_GLOBAL` with the prefixed constant. This avoids any structural changes to the environment.
- **File I/O:** The compiler needs to read source files from disk. Add a minimal `jacl_read_file(path)` utility. The compiler currently receives source as a string; the new `jacl_compile_program()` API handles file reading internally.
- **Path canonicalization:** Use `realpath()` (POSIX) for canonical path resolution. This handles `..`, `.`, and symlinks.
- **Topological sort:** Use a simple DFS-based topological sort on the module dependency graph. The "in-progress" set for cycle detection doubles as the DFS visited set.
- **CPS interaction:** Modules containing concurrent code undergo CPS transform independently. The CPS `__main` closure is per-module. Module initialization in concurrent mode may need to run each module's `__main` through the runtime scheduler.
- **Memory:** Module chunks and export metadata are arena-allocated (compile-time data). Module cache lifetime matches program lifetime.

## Success Metrics

- All 193+ existing single-file tests pass without modification
- At least 10 multi-file integration tests covering imports, privacy, cycles, diamonds, typed imports, mutable box imports, and concurrent modules
- No measurable regression in single-file compilation or execution performance
- Module compilation is cached: importing the same module from N files compiles it once
- Cross-module type errors caught at compile time (no runtime type surprises from imports)

## Open Questions

None — all design decisions resolved.
