# PRD: M14 Module System

## Introduction

Add a file-based module system to JACL where modules are first-class map values. `[use "path"]` compiles and loads a file, returning a map of public names to values. Module members are accessed via the existing `->` syntax (extended to work on maps) or destructuring.

## Goals

- Load external JACL files as modules via `[use "./path.jacl"]`
- Modules are map values — composable with existing map/destructuring operations
- Leading `_` makes a name private (not exported)
- Module cache prevents redundant compilation/execution
- Circular imports detected at compile time with clear error
- Extend `[. $val field]` / `->` to work on maps (not just structs)

## User Stories

### US-001: Extend `[.]` to support maps
**Description:** As a developer, I want `[. $map key]` to work on maps (not just structs) so that I can use `->` syntax for map field access.

**Acceptance Criteria:**
- [ ] `[. $map "key"]` returns `[map-get $map "key"]` when `$map` is a map
- [ ] `$map->key` desugars to `[. $map key]` (already works via parser)
- [ ] Chaining works: `$map->foo->bar` when foo's value is also a map
- [ ] Type error if value is not a struct or map
- [ ] `[. $map "key" value]` (3-arg set form) returns `[map-set $map "key" value]` for maps
- [ ] Existing struct behavior unchanged
- [ ] build.sh passes
- [ ] Tests pass

### US-002: Basic `[use "path"]` returns module map
**Description:** As a developer, I want `[use "./foo.jacl"]` to compile and execute a file, returning a map of its public top-level bindings.

**Acceptance Criteria:**
- [ ] `[use "./foo.jacl"]` compiles the file relative to the current file's directory
- [ ] Executes the module's top-level code once
- [ ] Returns a map: `{"name1": <value>, "name2": <closure>, ...}`
- [ ] Top-level `def` and `proc` bindings are included
- [ ] Top-level `mut` bindings are NOT included (mutable state is module-private)
- [ ] Macros defined in the module are NOT included (compile-time only)
- [ ] Structs defined in the module are registered globally (available to importer)
- [ ] File not found produces compile error with clear message
- [ ] Parse/compile errors in module surface to importer with file:line
- [ ] build.sh passes
- [ ] Tests pass

### US-003: Private names with `_` prefix
**Description:** As a module author, I want names starting with `_` to be private so that I can hide implementation details.

**Acceptance Criteria:**
- [ ] Top-level `def _helper ...` is NOT included in the returned map
- [ ] Top-level `proc _internal ...` is NOT included in the returned map
- [ ] Private names are still usable within the module
- [ ] Single `_` as a name is invalid (already handled by destructuring)
- [ ] build.sh passes
- [ ] Tests pass

### US-004: Module cache
**Description:** As the runtime, I want to cache compiled modules so that `[use "./foo.jacl"]` called multiple times returns the same map instance.

**Acceptance Criteria:**
- [ ] First `[use "./foo.jacl"]` compiles, executes, caches result
- [ ] Subsequent `[use "./foo.jacl"]` returns cached map (no re-execution)
- [ ] Cache key is canonical absolute path (resolve symlinks, normalize `..`)
- [ ] Different relative paths to same file hit the cache: `[use "./foo.jacl"]` and `[use "../dir/foo.jacl"]`
- [ ] Module's side effects (print, etc.) only happen once
- [ ] build.sh passes
- [ ] Tests pass

### US-005: Circular import detection
**Description:** As the compiler, I want to detect circular imports and produce a clear error so that developers don't hit infinite loops.

**Acceptance Criteria:**
- [ ] `a.jacl` uses `b.jacl`, `b.jacl` uses `a.jacl` → compile error
- [ ] Error message shows the cycle: "circular import: a.jacl -> b.jacl -> a.jacl"
- [ ] Detection happens at compile time, not runtime
- [ ] Self-import `[use "./self.jacl"]` is detected
- [ ] Transitive cycles (a -> b -> c -> a) are detected
- [ ] build.sh passes
- [ ] Tests pass

### US-006: Module-relative path resolution
**Description:** As a developer, I want paths in `[use]` to resolve relative to the containing file, not the current working directory.

**Acceptance Criteria:**
- [ ] `[use "./util.jacl"]` in `/project/lib/main.jacl` looks for `/project/lib/util.jacl`
- [ ] `[use "../common.jacl"]` in `/project/lib/main.jacl` looks for `/project/common.jacl`
- [ ] Nested imports resolve correctly: if `a.jacl` uses `./b.jacl` and `b.jacl` uses `./c.jacl`, c is found relative to b
- [ ] Absolute paths work: `[use "/usr/lib/jacl/std.jacl"]`
- [ ] Missing `.jacl` extension is NOT auto-added (explicit paths only)
- [ ] build.sh passes
- [ ] Tests pass

### US-007: Destructuring import pattern
**Description:** As a developer, I want to destructure the module map inline to import specific names directly.

**Acceptance Criteria:**
- [ ] `def {foo, bar} [use "./mod.jacl"]` binds foo and bar in current scope
- [ ] `def {foo, ..} [use "./mod.jacl"]` binds foo plus all other public names (if struct-like spread is supported for maps)
- [ ] `def {foo, ..rest} [use "./mod.jacl"]` binds foo, remaining names go into `rest` map
- [ ] Importing a non-existent name produces error: "module has no export 'baz'"
- [ ] Works with existing named destructuring implementation
- [ ] build.sh passes
- [ ] Tests pass

### US-008: Integration test — realistic module usage
**Description:** As the maintainer, I want end-to-end tests showing realistic module patterns.

**Acceptance Criteria:**
- [ ] Test: utility module with helper procs, imported and called
- [ ] Test: module with private `_helper` proc not visible to importer
- [ ] Test: module defines struct, importer constructs instances
- [ ] Test: diamond import (a uses b and c, both b and c use d) — d loaded once
- [ ] Test: module with `mut` state, importer cannot access it directly
- [ ] Test: chained `->` access into nested module maps
- [ ] build.sh passes
- [ ] Tests pass

## Functional Requirements

- FR-1: `[. $map "key"]` performs `map-get` when value is a map; `[. $map "key" val]` performs `map-set`
- FR-2: `[use "path"]` is a compile-time builtin that loads, compiles, executes, and caches a module
- FR-3: Module map contains all top-level `def`/`proc` bindings except those starting with `_`
- FR-4: `mut` bindings and `defmacro` definitions are module-private (not exported)
- FR-5: Structs defined in modules are registered globally in the importer's struct registry
- FR-6: Module cache uses canonical absolute paths as keys
- FR-7: Circular imports produce compile errors before any code executes
- FR-8: Path resolution is relative to the file containing the `[use]`, not cwd

## Non-Goals

- No search paths or `$JACL_PATH` — relative and absolute paths only
- No explicit `export` declarations — everything public unless `_` prefixed
- No versioning or dependency management
- No dynamic/runtime `use` (path must be a literal string at compile time)
- No interaction with `[interpret]` sandboxing — modules are orthogonal
- No standard library paths — that's future work
- No package manager integration

## Technical Considerations

- **Compiler state:** `[use]` needs access to file I/O during compilation. May need a "module loader" callback in compile context.
- **Struct registry:** Module-defined structs must merge into importer's registry. Check for name collisions.
- **Path canonicalization:** Use `realpath()` or equivalent to normalize paths for cache keys.
- **Import stack:** Track current import chain for cycle detection and relative path resolution.
- **Map extension:** `OP_STRUCT_GET_DYN` / `OP_STRUCT_SET_DYN` need to check for map tag and dispatch to `map-get`/`map-set`.

## Success Metrics

- Modules load correctly with `[use]` and return expected map
- `->` syntax works uniformly on structs and maps
- Circular imports caught at compile time with clear error
- Module code executes exactly once regardless of import count
- Destructuring provides ergonomic selective imports

## Open Questions

1. Should `struct` definitions in a module be namespaced or global? (Current proposal: global)
2. Should we support re-exporting? e.g., `def {foo} [use "./other.jacl"]; def foo $foo` — works naturally but verbose
3. What happens if two modules define the same struct name? (Probably: error on conflict)
