# PRD: Struct Value Types — Headerless Stack-Allocated Structs

## Introduction

Redesign JACL structs from heap-allocated GC-managed objects to headerless, stack-allocated value types with no GC involvement. Struct data uses raw C ABI layout with no object header — type metadata lives in an arena-based side-table instead of on the instance. Structs are stored inline as "wide locals" spanning multiple consecutive slots in the VM stack and state machine `fields[]` array. This makes structs maximally C-compatible and eliminates GC overhead for structured data.

Structs may only contain non-reference types (integers, floats, bools, nested structs). This removes the need for GC tracing, write barriers, and root registration. Structs infect static typing — every producer and consumer must agree on the concrete type at compile time. Boxing into a `dyn` wrapper is the escape hatch for type erasure.

The existing heap-allocated struct path is retained temporarily during migration. New code uses the value-type path; old code continues to work until fully migrated.

## Goals

- Remove `GCHeader` and `JaclStruct` header from struct instances (16 bytes saved per struct)
- Store structs inline on the VM stack and in state machine fields as wide locals
- Restrict struct fields to non-reference types only (no GC scanning required)
- Replace the fixed-size `StructTypeRegistry` with an arena-based registry supporting variable-width type descriptors
- Unify box representation: `JaclMutableRef` carries `type_idx` (0 = dyn, N = struct) + `total_size` + `data[]`
- Support explicit boxing of structs via existing `[box $val]` command
- Support type-checked unboxing via `[box? Type $val]` flow typing + `[unbox $val]`
- Provide generic operations on boxed structs: equality, hashing, stringify, type name
- Use byte-level equality and hashing (no per-field dispatch)
- Preserve existing syntax: `struct Name {type field, ...}`, `Name val1 val2`, `$s->field`

## User Stories

### US-001: Arena-based struct type registry

**Description:** As a developer, I need the struct type registry to use an arena allocator so that it can grow without a fixed type limit and support variable-width type descriptors.

**Acceptance Criteria:**
- [ ] Replace `StructTypeRegistry` (fixed 32-slot array of `StructTypeDef`) with an arena-backed registry
- [ ] Each `StructTypeDef` is allocated in the arena; lookups return a `StructTypeDef*` pointer into arena memory
- [ ] Variable field counts per struct — no `STRUCT_MAX_FIELDS` limit (arena accommodates the actual field list)
- [ ] Registry grows by acquiring new arena pages — no reallocation of existing entries (pointers remain stable)
- [ ] Name-to-pointer lookup (hash map or linear scan with interned names) returns `StructTypeDef*`
- [ ] Separate index array maps sequential `type_idx` values to arena pointers for box type checks
- [ ] `type_idx == 0` is reserved for plain dyn `JaclVal` boxes; struct types start at `type_idx == 1`
- [ ] Existing struct tests pass with the new registry backend
- [ ] Build passes

### US-002: Restrict struct fields to non-reference types

**Description:** As a developer, I need the compiler to reject struct field declarations with reference types so that structs are guaranteed to be GC-free.

**Acceptance Criteria:**
- [ ] Compiler emits an error if a struct field type is `str`, `vec`, `map`, `closure`, or `dyn`
- [ ] Allowed field types: `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `f32`, `f64`, `bool`, and named struct types
- [ ] Nested struct fields (another value-type struct) are allowed and inline
- [ ] Error message is clear: "struct fields must be value types; 'str' is a reference type"
- [ ] Existing tests with reference-type struct fields are updated or removed
- [ ] Build passes

### US-003: Wide locals — VM stack storage

**Description:** As a developer, I need structs to be stored inline on the VM stack as consecutive slots so that struct locals don't require heap allocation.

**Acceptance Criteria:**
- [ ] Compiler computes slot width for a struct local: `ceil(total_size / 8)` consecutive `JaclVal` stack slots
- [ ] Compiler tracks each struct local's base slot index, width, and `StructTypeDef*` in the local variable table
- [ ] Struct construction (`OP_STRUCT_NEW` replacement) writes raw field bytes across the reserved stack slots
- [ ] Stack slot reservation accounts for struct width in frame size calculations
- [ ] No heap allocation occurs for struct locals in synchronous functions
- [ ] Existing `OP_STRUCT_NEW` path remains for the legacy heap path during migration
- [ ] Build passes, struct creation and field access work for stack-allocated structs

### US-004: Wide locals — state machine field storage

**Description:** As a developer, I need structs in async/generator functions to occupy consecutive slots in the state machine's `fields[]` array so that they persist across suspension points without heap allocation.

**Acceptance Criteria:**
- [ ] `StateField` gains a `width` field (number of slots occupied, default 1 for non-struct locals)
- [ ] `sm__add_state_field` accepts a width parameter; struct locals reserve N consecutive slots
- [ ] `sm__find_field` returns the base index; compiler emits width from `StructTypeDef`
- [ ] State machine allocation sizes the `fields[]` array to account for multi-slot struct locals
- [ ] Liveness optimization (`sm__optimize_state_layout`) treats multi-slot struct fields as atomic units — never partially evicts
- [ ] Struct field read/write in state machine context uses byte-offset addressing from base field index
- [ ] Build passes, structs work correctly in async/generator functions across suspension points

### US-005: Struct field access — byte-offset addressing

**Description:** As a developer, I need struct field reads and writes to use byte-offset addressing from a base slot so that field access works on inline stack/state-machine data without pointer dereference.

**Acceptance Criteria:**
- [ ] New opcode `OP_STRUCT_GET_INLINE`: reads a field from a stack-resident struct using base slot + byte offset + field type
- [ ] New opcode `OP_STRUCT_SET_INLINE`: writes a field to a stack-resident struct using base slot + byte offset + field type
- [ ] Compiler emits byte offset (from `StructTypeDef`) and field type as opcode operands
- [ ] Field access on nested structs chains offsets: `$line->start->x` computes combined offset at compile time
- [ ] Existing `$s->field` syntax works unchanged — compiler selects inline vs legacy opcode based on storage
- [ ] Build passes, field access tests pass for stack-resident structs

### US-006: Struct arguments and returns — pass by value

**Description:** As a developer, I need structs to be passed to and returned from functions by value (copied) so that value semantics are preserved.

**Acceptance Criteria:**
- [ ] Calling a function with a struct argument copies N slots from caller's frame to callee's parameter slots
- [ ] Compiler emits width-aware copy for struct arguments at call sites
- [ ] Returning a struct from a function copies N slots to the caller's designated return slots
- [ ] Calling convention is statically resolved — both caller and callee know the struct type and width
- [ ] Functions that return a struct must always return that specific struct type (no polymorphic returns)
- [ ] Recursive functions with struct args/returns work correctly
- [ ] Build passes, tests verify value copy semantics (mutation of copy doesn't affect original)

### US-007: Closure capture of structs

**Description:** As a developer, I need closures to correctly capture struct locals by value so that closures work with the new wide-local representation.

**Acceptance Criteria:**
- [ ] When a closure captures a struct local, the compiler widens the upvalue region to hold N slots
- [ ] Capture is by value — the struct bytes are copied into the closure at capture time
- [ ] The compiler tracks which upvalue ranges are struct regions (base + width + type)
- [ ] Struct field access on a captured struct uses upvalue-relative byte-offset addressing
- [ ] Mutations to the captured struct inside the closure do not affect the original
- [ ] Build passes, closure capture tests pass with struct locals

### US-008: Unified box representation

**Description:** As a developer, I need the `JaclMutableRef` representation to be unified so that boxes for plain dyn values and boxed structs use the same layout and operations.

**Acceptance Criteria:**
- [ ] Replace `JaclMutableRef { JaclVal value; }` with `JaclMutableRef { uint32_t type_idx; uint32_t total_size; uint8_t data[]; }`
- [ ] `type_idx == 0`: box contains a plain `JaclVal` (8 bytes in `data[]`). GC traces the `JaclVal`.
- [ ] `type_idx > 0`: box contains a struct. `data[]` holds raw C ABI bytes. GC does not trace into `data[]`.
- [ ] Existing `[box value]` creates a box with `type_idx = 0` for non-struct values
- [ ] `[box $struct_val]` creates a box with `type_idx = N` for struct values — compiler selects based on argument type
- [ ] `[deref $boxed]` reads value out (by value for structs — copies bytes to stack slots)
- [ ] `[reset $boxed $new_val]` replaces contents (by value for structs — copies bytes in)
- [ ] `[swap $boxed $new_val]` swaps contents, returns old value
- [ ] All existing box/atom tests pass with the new representation
- [ ] Build passes

### US-009: Box type checking and flow typing

**Description:** As a developer, I need `box?` to support type-narrowing checks so that unboxing structs is safe and the compiler can infer types.

**Acceptance Criteria:**
- [ ] `[box? $val]` (one argument): checks if a dyn value is any box; implicitly requires `$val` to be dynamic
- [ ] `[box? Point $val]` (two arguments): checks if `$val` is a box containing a `Point` (compares `type_idx`)
- [ ] `[box? dyn $val]` is the explicit form of `[box? $val]` — checks for a box with `type_idx == 0`
- [ ] Two-argument `box?` enables flow typing: inside a guarded branch, the compiler narrows the type
- [ ] `[unbox $val]` extracts the struct from a box — compiler infers type and width from the narrowed type
- [ ] `[unbox $val]` outside a `box?`-narrowed scope is a compile error
- [ ] Flow typing example: `if [box? Point $b] { def Point p [unbox $b] }`
- [ ] Build passes, boxing round-trip tests pass

### US-010: Generic operations on boxed structs

**Description:** As a developer, I need equality, hashing, stringify, and type-name operations to work on boxed structs so that they are usable in generic contexts.

**Acceptance Criteria:**
- [ ] **Equality:** Two boxes are equal iff `type_idx` matches AND `memcmp(data, data, total_size) == 0`
- [ ] **Hashing:** Hash is computed over `type_idx` + raw `data[]` bytes
- [ ] **Stringify:** Registry lookup by `type_idx` to get struct name and field names; format each field by type (e.g., `Vec3{x: 1.5, y: 2.5, z: 3.5}`)
- [ ] **Type name:** Registry lookup by `type_idx`, return struct name string
- [ ] Equality and hashing work without registry access (pure byte operations except for `type_idx` integer comparison)
- [ ] Stringify is the only operation that requires runtime registry lookup
- [ ] Boxed structs work as `map` keys (equality + hashing)
- [ ] Build passes, generic operation tests pass

### US-011: Remove GC tracing for value-type structs

**Description:** As a developer, I need the GC to stop tracing value-type structs so that the GC overhead is eliminated for the new struct path.

**Acceptance Criteria:**
- [ ] GC mark phase skips struct data in stack-resident and state-machine-resident structs (no reference fields to trace)
- [ ] Unified `JaclMutableRef` is traced as a GC object: if `type_idx == 0`, trace the `JaclVal` in `data[]`; if `type_idx > 0`, skip `data[]`
- [ ] Remove the `OBJ_STRUCT` case from GC tracing that walks fields via registry for value-type structs
- [ ] Legacy heap-allocated structs (with reference fields) retain their existing GC tracing during migration
- [ ] No memory leaks or use-after-free — verify with stress tests
- [ ] Build passes, GC tests pass

### US-012: Byte-level equality and hashing for unboxed structs

**Description:** As a developer, I need struct equality and hashing to use raw byte comparison so that these operations are fast and simple.

**Acceptance Criteria:**
- [ ] Equality of two stack-resident structs of the same type: `memcmp` across their slot ranges
- [ ] Hashing of a stack-resident struct: hash raw bytes across slot range
- [ ] Compiler verifies both operands are the same struct type at compile time (static typing infection)
- [ ] Padding bytes are zeroed at struct construction time so `memcmp` is deterministic
- [ ] Build passes, equality and hashing tests pass

### US-013: Migration scaffolding — dual path

**Description:** As a developer, I need both the old heap-allocated struct path and the new value-type path to coexist so that migration is incremental.

**Acceptance Criteria:**
- [ ] Compiler can distinguish value-type structs (non-reference fields only) from legacy structs (has reference fields)
- [ ] Value-type structs use the new inline storage path; legacy structs use the existing heap path
- [ ] Both paths produce correct results — no behavioral regressions
- [ ] Clear internal naming convention distinguishes old vs new paths (e.g., `OP_STRUCT_NEW` vs `OP_STRUCT_NEW_INLINE`)
- [ ] Build passes, all existing struct tests pass, new value-type tests pass

## Functional Requirements

- FR-1: Struct instances have no `GCHeader` or `JaclStruct` header — raw C ABI field bytes only
- FR-2: Struct fields are restricted to non-reference types: integers, floats, bools, nested value-type structs
- FR-3: Structs are stored inline as wide locals in VM stack slots and state machine `fields[]` entries
- FR-4: The struct type registry uses an arena allocator — growable, variable-width, pointer-based lookup; `type_idx == 0` reserved for plain dyn
- FR-5: Struct arguments and return values are passed by value (copied)
- FR-6: Closures capture structs by value with widened upvalue regions
- FR-7: `JaclMutableRef` is unified: `{ type_idx, total_size, data[] }` — same representation for plain dyn boxes and struct boxes
- FR-8: `[box $val]` creates a box; compiler selects `type_idx = 0` (dyn) or `type_idx = N` (struct) based on argument type
- FR-9: `[box? $val]` checks if a dyn value is any box; `[box? Type $val]` checks for a box containing a specific struct type
- FR-10: `[unbox $val]` extracts a struct from a box; type inferred from flow typing; compile error if not in a `box?`-narrowed scope
- FR-11: `[deref $box]`, `[reset $box $val]`, `[swap $box $val]` work for both dyn and struct boxes
- FR-12: Equality is `memcmp` over raw bytes; for boxes, `type_idx` is compared first
- FR-13: Hashing is over raw bytes (plus `type_idx` for boxes)
- FR-14: Stringify uses registry lookup for field names and types
- FR-15: Padding bytes are zeroed at construction time for deterministic `memcmp`/hashing
- FR-16: Existing struct syntax is preserved: `struct Name {type field, ...}`, `Name val1 val2`, `$s->field`
- FR-17: The old heap-allocated struct path is retained during migration

## Non-Goals

- No fixed-size arrays in structs (deferred to a follow-up PRD)
- No reference-type fields in value-type structs (use boxing or collections separately)
- No trait/interface system for structs
- No methods or associated functions on structs
- No generic/parameterized struct types
- No struct inheritance or composition operators
- No packed/repr attributes for controlling layout
- No implicit boxing — `[box $val]` is always explicit
- No flow typing beyond `box?` guards (general flow typing is out of scope)
- No struct-in-atom — atoms remain dyn-only (`type_idx == 0`)

## Technical Considerations

- **Arena registry:** The arena allocator for `StructTypeDef` entries should use page-based growth (e.g., 4KB pages). Pointers into the arena are stable — no reallocation. A separate index (array of `StructTypeDef*`) maps sequential `type_idx` values to arena pointers. `type_idx == 0` is reserved for plain dyn `JaclVal` boxes; struct types start at 1.
- **Unified box representation:** `JaclMutableRef` changes from `{ JaclVal value; }` to `{ uint32_t type_idx; uint32_t total_size; uint8_t data[]; }`. All existing box/cell/atom code paths must be updated. GC tracing branches on `type_idx`: 0 means trace the `JaclVal` in `data[]`, >0 means skip (struct, no references).
- **Padding zeroing:** `memset(0)` the full slot range before writing field values during struct construction. This ensures padding bytes between fields and trailing padding are zero, making `memcmp` and hashing deterministic.
- **Stack pressure:** A struct with 10 × 8-byte fields consumes 10 stack slots out of 256. Deep call stacks with large structs could exhaust the stack. Monitor this — may need to increase `VM_STACK_MAX` or add a stack overflow diagnostic that identifies struct-heavy frames.
- **State machine field width:** The `JaclStateMachine.field_count` and allocation size must account for multi-slot struct locals. The state object may be significantly larger for functions with many struct locals.
- **Embedding API:** The C embedding API (`jacl_struct_new_val`, `jacl_struct_get_val`, `jacl_struct_set_val`) currently operates on heap-allocated structs via `JaclVal` pointers. These functions need updated variants for value-type structs, or the API can box/unbox transparently.
- **Flow typing scope:** The compiler must track type narrowing within `if [box? Type $val]` branches. This is a limited form of flow typing — only `box?` with a type argument creates a narrowed scope. The narrowed type is associated with the specific variable tested and is valid only within the true branch.

## Success Metrics

- Struct creation has zero GC allocations for value-type structs
- Struct field access has no pointer dereference (direct byte-offset from stack base)
- `memcmp` equality and byte hashing work correctly with no false positives from padding
- All existing struct and box tests pass (dual-path migration, unified representation)
- Box round-trip works: `[box $struct]` → `[box? Type $b]` → `[unbox $b]` recovers original bytes
- New value-type struct tests cover: stack storage, state machine storage, argument passing, return values, closure capture, boxing round-trip, generic operations, flow typing

## Resolved Questions

- **Boxing syntax:** Explicit via existing `[box $val]` command. Compiler selects `type_idx` based on argument type.
- **Unboxing syntax:** `[unbox $val]` inside a `[box? Type $val]` flow-typed branch. Type inferred from narrowing. Compile error outside narrowed scope.
- **Box representation:** Unified `JaclMutableRef { type_idx, total_size, data[] }`. `type_idx == 0` for plain dyn, `type_idx > 0` for structs.
- **`box?` predicate:** Extended to 1 or 2 arguments. `[box? $val]` checks for any box (implicitly checks dyn). `[box? Type $val]` checks for a box containing a specific type. `[box? dyn $val]` is the explicit form of the 1-argument version.
- **Dyn-in-box:** Boxing a dyn value is valid — creates a mutable container for a dynamic value. Useful for mutable references to dynamically-typed data.

## Resolved Questions (continued)

- **Atom interaction:** Atoms remain dyn-only (`type_idx == 0`). Putting a struct in an atom is a compile error. CAS semantics on wide payloads would require a lock, which is a separate design problem.
- **Inline anonymous structs:** Yes, they become value types. Same non-reference-field restriction applies. Falls out naturally from the registry and layout changes.
- **Stack size:** Deferred. 256 slots is sufficient for now. If struct-heavy code exhausts the stack, `VM_STACK_MAX` is a one-line constant change.
