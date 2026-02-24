# PRD: M10 — Mutable State

## Introduction

Add mutable bindings, reassignment, and explicit mutable containers to JACL. `mut` declares a mutable binding (local or global), `set!` reassigns it, and the compiler enforces immutability of `def` bindings at compile time. Mutable locals captured by closures are automatically boxed into heap cells so mutations are shared. `box` and `atom` provide explicit mutable containers — `box` for thread-local state and `atom` for CAS-based shared state (full concurrency semantics deferred to M13).

## Goals

- Introduce `mut` for mutable bindings alongside the existing immutable `def`
- Provide `set!` for reassigning mutable bindings (locals, globals, and upvalues)
- Enforce immutability at compile time — `set!` on a `def` binding is a compile error
- Auto-box `mut` locals into heap cells so closure capture shares mutations
- Add `box` (thread-local container) and `atom` (CAS container) as explicit mutable containers
- Provide `deref`, `reset!`, `swap!` builtins for reading/writing containers

## User Stories

### US-001: Foundation — `JaclCell` type and `is_mutable` tracking
**Description:** As a developer, I need the value-level and compiler-level infrastructure for mutable state so that subsequent stories can build on it.

**Acceptance Criteria:**
- [ ] Add `JACL_TAG_CELL` (0x0A) to value.c — a heap-pointer tag for internal mutable cells
- [ ] Add `JaclMutableRef` struct: `{ JaclVal value; }` — shared by cell, box, and atom
- [ ] Add constructors/accessors: `jacl_cell(JaclMutableRef*)`, `jacl_as_cell(JaclVal)`, `jacl_is_cell(JaclVal)`
- [ ] Add `bool is_mutable` field to the `Local` struct in compiler.c
- [ ] Add `bool is_mutable` field to the `GlobalArity` struct in compiler.c (rename to `GlobalInfo` if desired)
- [ ] Add `bool is_mutable` field to the `Upvalue` struct in compiler.c
- [ ] Typecheck/build passes

### US-002: `mut` — mutable local bindings
**Description:** As a JACL programmer, I want to declare mutable local variables with `mut` so that I can update their values later with `set!`.

**Acceptance Criteria:**
- [ ] `mut x 42` at local scope (inside a proc or block) allocates a `JaclCell` containing `42`, stores the cell in the local slot
- [ ] New opcode `OP_MAKE_CELL`: pops value from stack, allocates `JaclMutableRef` on arena, pushes cell-tagged value
- [ ] New opcode `OP_GET_CELL_LOCAL` (uint8_t slot): reads cell from local slot, dereferences it, pushes the contained value
- [ ] Compiler emits `OP_MAKE_CELL` after the RHS of `mut` at local scope
- [ ] Compiler emits `OP_GET_CELL_LOCAL` (instead of `OP_GET_LOCAL`) when reading a `mut` local via `$x`
- [ ] `[mut x 42]` returns nil (like `def`)
- [ ] `mut` can shadow existing bindings in the same or enclosing scope
- [ ] Arity: exactly 2 arguments (name + value); compile error otherwise
- [ ] Typecheck/build passes

### US-003: `set!` — reassign mutable locals
**Description:** As a JACL programmer, I want to reassign a mutable local variable with `set!` so that I can update state.

**Acceptance Criteria:**
- [ ] `set! x 99` where `x` is a `mut` local writes the new value through the cell
- [ ] New opcode `OP_SET_CELL_LOCAL` (uint8_t slot): pops new value, reads cell from local slot, stores value in cell, pushes nil
- [ ] `set!` is a special form — first argument is a bare name (not `$name`), second is an expression
- [ ] `[set! x 99]` returns nil
- [ ] Compile-time error: `set!` on a name declared with `def` → "cannot mutate immutable binding 'x'"
- [ ] Compile-time error: `set!` on an undeclared name → "undefined variable 'x'"
- [ ] Compile-time error: `set!` on a function parameter → "cannot mutate parameter 'x'"
- [ ] Arity: exactly 2 arguments (name + value); compile error otherwise
- [ ] Typecheck/build passes

### US-004: `mut`/`set!` for mutable globals
**Description:** As a JACL programmer, I want to declare and reassign mutable global variables so that top-level state can be updated.

**Acceptance Criteria:**
- [ ] `mut x 42` at global scope (`scope_depth == 0`) emits `OP_DEF_GLOBAL` and records the name as mutable in the compiler's global info list
- [ ] `set! x 99` at global scope where `x` was declared with `mut` emits `OP_SET_GLOBAL`
- [ ] New opcode `OP_SET_GLOBAL` (uint16_t name index): pops value, calls `vm__env_set()`, pushes nil
- [ ] Compile-time error: `set!` on a global declared with `def` → "cannot mutate immutable binding 'x'"
- [ ] Global `mut` bindings are tracked in the compiler's `GlobalArity` (or `GlobalInfo`) array alongside arity info
- [ ] Multiple `set!` on the same global works correctly
- [ ] Typecheck/build passes

### US-005: Mutable closure capture — shared mutation via cells
**Description:** As a JACL programmer, I want closures that capture `mut` variables to share mutations with the enclosing scope so that closures can be used for stateful patterns like counters and accumulators.

**Acceptance Criteria:**
- [ ] When a closure captures a `mut` local, the cell pointer is copied into the closure's upvalue array (standard `OP_CLOSURE` capture) — both the enclosing scope and the closure reference the same heap cell
- [ ] New opcode `OP_GET_CELL_UPVALUE` (uint8_t index): reads cell from upvalue array, dereferences, pushes value
- [ ] New opcode `OP_SET_CELL_UPVALUE` (uint8_t index): pops value, reads cell from upvalue array, stores value in cell, pushes nil
- [ ] Compiler emits `OP_GET_CELL_UPVALUE` when reading a `mut` upvalue via `$x`
- [ ] Compiler emits `OP_SET_CELL_UPVALUE` when `set!` targets a `mut` upvalue
- [ ] `Upvalue` struct's `is_mutable` flag is set when capturing a `mut` local, propagated through transitive capture
- [ ] Mutation in closure is visible in enclosing scope:
  ```
  mut x 0
  proc inc { set! x [+ $x 1] }
  [inc]
  [print $x]  ;; => 1
  ```
- [ ] Mutation in enclosing scope is visible in closure:
  ```
  mut x 0
  proc get-x { $x }
  set! x 42
  [print [get-x]]  ;; => 42
  ```
- [ ] Multiple closures sharing the same `mut` local all see each other's mutations
- [ ] Transitive capture works: inner closure captures `mut` from grandparent scope through parent
- [ ] Typecheck/build passes

### US-006: `box` — thread-local mutable container
**Description:** As a JACL programmer, I want an explicit mutable container (`box`) for cases where I need a first-class mutable reference that can be passed around.

**Acceptance Criteria:**
- [ ] `[box 42]` creates a `JaclMutableRef` with tag `JACL_TAG_BOX` (0x0B) containing `42`
- [ ] New opcode `OP_BOX`: pops value, allocates `JaclMutableRef` on arena, pushes box-tagged value
- [ ] `[box? $b]` returns `$true` if `$b` is a box, `$false` otherwise
- [ ] New opcode `OP_IS_BOX`: pops value, pushes bool based on tag check
- [ ] `[print $b]` displays `<box: 42>` (shows contained value)
- [ ] Arity: `box` takes exactly 1 argument, `box?` takes exactly 1 argument
- [ ] Error propagation: `[box [error "x"]]` returns the error (does not create a box)
- [ ] Typecheck/build passes

### US-007: `atom` — CAS mutable container
**Description:** As a JACL programmer, I want a CAS-based mutable container (`atom`) for state that will eventually be safe to share across threads (full concurrency in M13).

**Acceptance Criteria:**
- [ ] `[atom 0]` creates a `JaclMutableRef` with tag `JACL_TAG_ATOM` (0x0C) containing `0`
- [ ] New opcode `OP_ATOM`: pops value, allocates `JaclMutableRef` on arena, pushes atom-tagged value
- [ ] `[atom? $a]` returns `$true` if `$a` is an atom, `$false` otherwise
- [ ] New opcode `OP_IS_ATOM`: pops value, pushes bool based on tag check
- [ ] `[print $a]` displays `<atom: 0>` (shows contained value)
- [ ] Arity: `atom` takes exactly 1 argument, `atom?` takes exactly 1 argument
- [ ] Error propagation: `[atom [error "x"]]` returns the error (does not create an atom)
- [ ] In M10, atom behaves identically to box at runtime — CAS semantics added in M13
- [ ] Typecheck/build passes

### US-008: `deref`, `reset!`, `swap!` builtins
**Description:** As a JACL programmer, I want builtins to read and update the values inside `box` and `atom` containers.

**Acceptance Criteria:**
- [ ] `[deref $b]` — reads the value inside a box or atom
  - New opcode `OP_DEREF`: pops container, reads `value` field from `JaclMutableRef`, pushes it
  - Runtime error if argument is not a box or atom: `[error "deref: expected box or atom"]`
  - Arity: exactly 1 argument
- [ ] `[reset! $b 99]` — sets the value inside a box or atom, returns the new value
  - New opcode `OP_RESET`: pops new-value + container, stores new-value in `JaclMutableRef`, pushes new-value
  - Runtime error if first argument is not a box or atom: `[error "reset!: expected box or atom"]`
  - Arity: exactly 2 arguments
- [ ] `[swap! $a inc]` — applies a function to the atom's current value, stores the result, returns the new value
  - New opcode `OP_SWAP`: pops closure + container, calls closure with current value as argument, stores result in `JaclMutableRef`, pushes result
  - Runtime error if first argument is not a box or atom
  - Runtime error if second argument is not a closure
  - Arity: exactly 2 arguments
  - In M10 (single-threaded), `swap!` is: `val = deref(container); new = f(val); reset!(container, new); return new`
  - In M13, `swap!` will use CAS loop for atoms
- [ ] Error propagation: if any argument is error-flagged, return the first error unchanged
- [ ] `[swap! $a [proc {x} { [+ $x 1] }]]` increments atom value by 1
- [ ] Typecheck/build passes

### US-009: Update `print` for new types
**Description:** As a JACL programmer, I want `print` to display cell, box, and atom values with meaningful formatting for debugging.

**Acceptance Criteria:**
- [ ] `[print $b]` where `$b` is a box displays `<box: VALUE>` with the contained value formatted
- [ ] `[print $a]` where `$a` is an atom displays `<atom: VALUE>` with the contained value formatted
- [ ] Cells are transparent — `[print $x]` where `x` is a `mut` local prints the contained value directly (not `<cell: VALUE>`)
- [ ] Nested containers: `[print [box [vec 1 2 3]]]` → `<box: [1 2 3]>`
- [ ] No regression on existing print behavior
- [ ] Typecheck/build passes

### US-010: Integration tests for M10
**Description:** As a developer, I want comprehensive tests covering all mutable state features to ensure correctness and prevent regressions.

**Acceptance Criteria:**
- [ ] Unit tests in `test/test_m10.c` covering:
  - `OP_MAKE_CELL`, `OP_GET_CELL_LOCAL`, `OP_SET_CELL_LOCAL` — basic mut/set! on locals
  - `OP_SET_GLOBAL` — set! on mutable globals
  - Compile-time error for `set!` on `def` binding
  - Compile-time error for `set!` on function parameter
  - `OP_GET_CELL_UPVALUE`, `OP_SET_CELL_UPVALUE` — mutable closure capture
  - Shared mutation between closures and enclosing scope
  - Multiple closures sharing a `mut` local
  - Transitive upvalue capture of mutable bindings
  - `OP_BOX`, `OP_ATOM`, `OP_DEREF`, `OP_RESET`, `OP_SWAP` — container operations
  - `OP_IS_BOX`, `OP_IS_ATOM` — type predicates
  - Error propagation through all new operations
  - Runtime errors for `deref`/`reset!`/`swap!` on wrong types
- [ ] `.jacl` harness tests in `test/jacl/` covering:
  - `mut_basics.jacl` — `mut`, `set!`, scoping
  - `mut_closures.jacl` — shared mutable state across closures
  - `box_atom.jacl` — `box`, `atom`, `deref`, `reset!`, `swap!`
- [ ] All existing M1–M9 tests pass without regression
- [ ] Typecheck/build passes

## Functional Requirements

- FR-1: `[mut <name> <value>]` declares a mutable binding. At local scope, allocates a `JaclCell` containing the value and stores the cell in the local slot. At global scope, stores the value via `OP_DEF_GLOBAL` and records the name as mutable.
- FR-2: `[set! <name> <value>]` reassigns a mutable binding. Resolves `<name>` as local → upvalue → global. Compile-time error if the binding was declared with `def` or is a function parameter.
- FR-3: `set!` returns nil.
- FR-4: Reading a `mut` local via `$x` emits `OP_GET_CELL_LOCAL` which dereferences the cell transparently. The user never interacts with cells directly.
- FR-5: When a closure captures a `mut` local, the cell pointer is copied into the upvalue array. Both the enclosing scope and all closures sharing the capture reference the same heap cell, so mutations are bidirectionally visible.
- FR-6: `[box <value>]` creates a `JACL_TAG_BOX` container. `[atom <value>]` creates a `JACL_TAG_ATOM` container. Both hold a single `JaclVal` in a `JaclMutableRef` struct.
- FR-7: `[deref <container>]` reads the value inside a box or atom. Runtime error if argument is not a box or atom.
- FR-8: `[reset! <container> <value>]` sets the value inside a box or atom and returns the new value. Runtime error if first argument is not a box or atom.
- FR-9: `[swap! <container> <fn>]` applies `<fn>` to the current value of a box or atom, stores the result, and returns the new value. Runtime error if types don't match.
- FR-10: `[box? <value>]` and `[atom? <value>]` return bool predicates for container types.
- FR-11: All new operations propagate error flags — if any argument is error-flagged, return the first error unchanged.
- FR-12: `print` displays boxes as `<box: VALUE>` and atoms as `<atom: VALUE>`. Cells are transparent (print shows the contained value directly).

## Non-Goals (Out of Scope)

- No CAS/atomic semantics for `atom` in M10 — deferred to M13 (Concurrency)
- No thread-pinning for `box` — deferred to M13
- No `compare-and-swap!` or `add-watch` for atoms
- No validators on atoms (e.g., `[atom 0 :validator pos?]`)
- No mutable function parameters — parameters are always immutable
- No `volatile` or `transient` collection variants
- No optimization of uncaptured `mut` locals (always cell-boxed for simplicity; optimization is future work)
- No `ref` or general-purpose mutable reference type beyond box/atom

## Technical Considerations

- **New value tags:** `JACL_TAG_CELL` (0x0A), `JACL_TAG_BOX` (0x0B), `JACL_TAG_ATOM` (0x0C). All point to the same `JaclMutableRef` struct: `{ JaclVal value; }`.
- **Always-box strategy:** All `mut` locals are boxed into cells at creation time, even if never captured. This simplifies the single-pass compiler (no need to predict future captures). The overhead is one arena allocation per `mut` local. Optimization to only box captured locals can be done later with a two-pass approach.
- **New opcodes (13 total):**
  - `OP_MAKE_CELL` — pop value, alloc cell, push cell
  - `OP_GET_CELL_LOCAL` (uint8_t slot) — read cell from local, deref, push value
  - `OP_SET_CELL_LOCAL` (uint8_t slot) — pop value, write through cell in local, push nil
  - `OP_GET_CELL_UPVALUE` (uint8_t index) — read cell from upvalue, deref, push value
  - `OP_SET_CELL_UPVALUE` (uint8_t index) — pop value, write through cell in upvalue, push nil
  - `OP_SET_GLOBAL` (uint16_t name) — pop value, update global, push nil
  - `OP_BOX` — pop value, alloc box, push box
  - `OP_ATOM` — pop value, alloc atom, push atom
  - `OP_DEREF` — pop container, push contained value
  - `OP_RESET` — pop value + container, store, push new value
  - `OP_SWAP` — pop closure + container, call closure with current value, store result, push result
  - `OP_IS_BOX` — pop value, push bool
  - `OP_IS_ATOM` — pop value, push bool
- **Compiler changes:**
  - `mut` added as a special form alongside `def` in `compiler__compile_command()`
  - `set!` added as a special form — resolves name through local → upvalue → global, checks mutability, emits appropriate `OP_SET_CELL_LOCAL`/`OP_SET_CELL_UPVALUE`/`OP_SET_GLOBAL`
  - `box`, `atom`, `deref`, `reset!`, `swap!`, `box?`, `atom?` added to the builtin chain
  - `compiler__resolve_local()` and `compiler__resolve_upvalue()` propagate `is_mutable` info so the compiler knows which get/set opcodes to emit
  - Variable read in `AST_VAR_REF`: check `is_mutable` flag to choose between `OP_GET_LOCAL` vs `OP_GET_CELL_LOCAL`, `OP_GET_UPVALUE` vs `OP_GET_CELL_UPVALUE`
- **VM changes:** All new opcode handlers in `vm__run()`. `OP_SWAP` needs to set up a call frame to invoke the closure, which adds complexity — consider implementing it as: push current value, call closure, read result, store in container.
- **Interaction with error handling:** `OP_CHECK_ERROR` (from M9) interacts with `set!` — if the RHS of `set!` produces an error, the error propagates up via `OP_CHECK_ERROR` as usual. The cell is NOT updated with the error value.
- **Existing `OP_SET_LOCAL`:** Currently used only internally for block cleanup. Remains unchanged — it writes raw values to stack slots. The new `OP_SET_CELL_LOCAL` writes through the cell indirection.
- **`OP_SWAP` implementation detail:** Since `swap!` calls a closure, the VM needs to: (1) read current value from container, (2) push as argument, (3) call the closure via the existing `OP_CALL` mechanism, (4) when the call returns, store the result back. This may be simplest to implement by emitting multiple instructions from the compiler rather than a single monolithic opcode. Alternative: `OP_SWAP` can internally invoke the VM's call machinery.

## Success Metrics

- `mut` + `set!` work for locals, globals, and upvalues with shared mutation through closures
- Compile-time errors for `set!` on `def` bindings, parameters, and undeclared names
- `box` and `atom` containers work with `deref`, `reset!`, `swap!`
- Error propagation through all new operations is consistent with M9 behavior
- All existing M1–M9 tests pass without regression
- No memory leaks from cell/box/atom allocations (arena-allocated, freed on arena reset)

## Open Questions

- Should `set!` return nil (like `def`) or the new value? This PRD specifies nil for consistency with `def`, but returning the new value enables chaining patterns like `[print [set! x 99]]`.
- Should `swap!` work on `box` as well as `atom`, or only `atom`? This PRD allows both for convenience, but restricting `swap!` to atoms would enforce the semantic distinction.
- Should there be a `mut?` predicate to check if a value is a cell? Probably not — cells are meant to be transparent. But it could be useful for debugging.
- How should `deref` interact with cells? If a user somehow gets a cell value (e.g., by returning a `mut` local from a function before it's dereferenced), should `deref` work on it? This PRD keeps cells fully transparent — users should never see them.
