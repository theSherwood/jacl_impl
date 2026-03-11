# PRD: Struct Types

## Introduction

Add user-defined struct types to JACL as a precursor to FFI (M17). Structs are named, compound value types with typed fields. They are mutable by default, heap-allocated, and can be boxed into `dyn` for use in collections like `vec` and `map`. Struct definitions support full nesting — fields can be other named structs or inline anonymous struct types. Structs are pure data containers with no methods; functions operate on structs by taking them as arguments.

For FFI readiness, struct memory layout is deterministic and follows C ABI field ordering (declaration order, natural alignment, trailing padding). This milestone does not implement FFI itself — it establishes the struct type infrastructure that FFI will build on. Inline/anonymous struct types are scoped to FFI signatures only in this milestone.

## Goals

- Support named struct type declarations via `defstruct`
- Struct instantiation, field access, and field mutation
- Integrate struct types into the compiler's type system (`JaclType`)
- Support structs as field types (nesting), including inline anonymous struct definitions
- Heap-allocated structs that can be boxed into `dyn` for use in `vec`, `map`, and other dynamic contexts
- Deterministic C-ABI-compatible memory layout for future FFI marshaling
- Structs exportable/importable across modules via `use`

## User Stories

### US-001: `defstruct` keyword and parser support

**Description:** As a developer, I need the lexer and parser to recognize struct definitions so that struct types can be declared in JACL source code.

**Acceptance Criteria:**
- [ ] `defstruct` is recognized as a keyword token (`TOKEN_DEFSTRUCT`)
- [ ] Parse: `defstruct Point [x :i32] [y :i32]` → `AST_DEFSTRUCT` node with name `Point` and field list
- [ ] Each field is a `[name :type]` pair
- [ ] Field types can be any existing type (`:i32`, `:i64`, `:u32`, `:u64`, `:f32`, `:f64`, `:str`, `:bool`, `:dyn`) or another named struct type
- [ ] Error on duplicate field names within a struct
- [ ] Error on zero fields (structs must have at least one field)
- [ ] `defstruct` must appear at top level (not inside a proc or block)
- [ ] Build passes, all existing tests pass

### US-002: Struct type registration in the compiler

**Description:** As a developer, I need the compiler to register struct definitions as named types so that struct names can be used as type annotations throughout the program.

**Acceptance Criteria:**
- [ ] Add `TYPE_STRUCT` to `JaclType` enum
- [ ] Compiler maintains a struct type registry (name → field list with types and offsets)
- [ ] Struct names are valid type annotations: `def p :Point [Point 1 2]`
- [ ] Error on duplicate struct name definitions
- [ ] Error on struct fields referencing undefined struct types (forward references not allowed)
- [ ] Struct types are available for use after their `defstruct` declaration (declaration order matters)
- [ ] Build passes, all existing tests pass

### US-003: Struct value representation and instantiation

**Description:** As a developer, I need to create struct instances at runtime so that JACL programs can work with structured data.

**Acceptance Criteria:**
- [ ] Add `JACL_TAG_STRUCT` tag for struct values
- [ ] Struct instances are heap-allocated with fields laid out in declaration order using C-ABI alignment rules (natural alignment per field type, trailing padding to struct alignment)
- [ ] Instantiation syntax: `[Point 1 2]` — struct name as command, positional arguments for fields in declaration order
- [ ] Compile-time arity check: error if wrong number of arguments
- [ ] Compile-time type check: each argument must match the declared field type
- [ ] Add `OP_STRUCT_NEW` opcode: pops N field values from the stack, allocates struct, pushes struct value
- [ ] Struct values carry a pointer to their type metadata (for field access and GC)
- [ ] Build passes, all existing tests pass

### US-004: Field access

**Description:** As a developer, I need to read individual fields from a struct so that I can use struct data in expressions.

**Acceptance Criteria:**
- [ ] Field access syntax: `[. $point x]` — dot command, struct value, field name
- [ ] Add `OP_STRUCT_GET` opcode with field offset operand
- [ ] Compiler resolves field name to offset at compile time — error on unknown field name
- [ ] Compiler knows the result type of field access (the declared field type)
- [ ] Works with nested structs: `[. [. $line start] x]` to access `line.start.x`
- [ ] Error when used on non-struct values
- [ ] Build passes, all existing tests pass

### US-005: Field mutation

**Description:** As a developer, I need to modify struct fields in place so that structs work as mutable data containers.

**Acceptance Criteria:**
- [ ] Field mutation syntax: `[.set! $point x 42]` — dot-set command, struct value, field name, new value
- [ ] Add `OP_STRUCT_SET` opcode with field offset operand
- [ ] Compiler resolves field name to offset at compile time — error on unknown field name
- [ ] Compile-time type check: new value must match the declared field type
- [ ] Returns the struct value (for chaining)
- [ ] Error when used on non-struct values
- [ ] Build passes, all existing tests pass

### US-006: Struct boxing and `dyn` interop

**Description:** As a developer, I need to store structs in dynamically-typed collections so that structs integrate with JACL's existing `vec` and `map` types.

**Acceptance Criteria:**
- [ ] Struct values can be assigned to `:dyn` variables: `def d :dyn [Point 1 2]`
- [ ] Structs can be stored in `vec`: `[vec [Point 1 2] [Point 3 4]]`
- [ ] Structs can be stored as `map` values: `[map "origin" [Point 0 0]]`
- [ ] Field access on a `:dyn` value holding a struct works with runtime type checking
- [ ] `OP_TO_DYN` handles struct values (boxes them with their tag)
- [ ] Build passes, all existing tests pass

### US-007: Nested struct types

**Description:** As a developer, I need struct fields that are themselves structs so that I can model complex data.

**Acceptance Criteria:**
- [ ] Struct fields can reference other named struct types: `defstruct Line [start :Point] [end :Point]`
- [ ] Instantiation works with nested structs: `[Line [Point 0 0] [Point 10 10]]`
- [ ] Field access chains work: `[. [. $line start] x]`
- [ ] Field mutation works on nested struct fields: `[.set! [. $line start] x 5]`
- [ ] Compile-time type checking applies at all nesting levels
- [ ] Build passes, all existing tests pass

### US-008: Inline anonymous struct types (FFI prep)

**Description:** As a developer, I need to define anonymous struct types inline in FFI function signatures so that C struct parameters don't all need top-level `defstruct` declarations.

**Acceptance Criteria:**
- [ ] Inline struct type syntax in type annotation position: `:struct{x:i32,y:i32}`
- [ ] Inline structs can be used as field types in `defstruct`: `defstruct Wrapper [pos :struct{x:i32,y:i32}]`
- [ ] Inline structs can nest: `:struct{start:struct{x:i32,y:i32},end:struct{x:i32,y:i32}}`
- [ ] Inline struct types follow the same layout rules as named structs (C-ABI compatible)
- [ ] Two inline structs with the same fields and types in the same order are structurally equivalent
- [ ] Compiler generates internal type metadata for inline structs (no user-visible name)
- [ ] Build passes, all existing tests pass

### US-009: Module export/import of struct types

**Description:** As a developer, I need struct type definitions to be available across module boundaries so that modules can share structured data.

**Acceptance Criteria:**
- [ ] Struct types defined in a module are exported by default (following the existing convention — unless prefixed with `_`)
- [ ] `use "shapes.jacl" [Point Line]` imports both struct type definitions and their constructors
- [ ] Imported struct types can be used as type annotations in the importing module
- [ ] Field access and mutation work on imported struct instances
- [ ] Type checking across module boundaries works correctly (a `Point` from module A is the same type as `Point` imported from module A)
- [ ] Private structs (`_Point`) cannot be imported — compile-time error
- [ ] Build passes, all existing tests pass

### US-010: GC integration for struct values

**Description:** As a developer, I need the garbage collector to correctly trace and collect struct values so there are no memory leaks or use-after-free bugs.

**Acceptance Criteria:**
- [ ] GC traces struct fields that contain heap-allocated values (strings, other structs, closures, collections)
- [ ] Struct instances are collected when unreachable
- [ ] No use-after-free when structs are nested (inner struct outlives outer)
- [ ] Stress test: allocate many structs in a loop, verify memory is reclaimed
- [ ] Build passes, all existing tests pass

### US-011: E2E tests — struct basics

**Description:** As a developer, I need end-to-end tests covering struct definition, instantiation, field access, and mutation.

**Acceptance Criteria:**
- [ ] Test: define struct, instantiate, access fields, verify values
- [ ] Test: field mutation with `[.set!]`, verify updated value
- [ ] Test: struct as proc parameter and return value
- [ ] Test: struct with all supported field types (i32, i64, u32, u64, f32, f64, str, bool, dyn)
- [ ] Test: nested structs — access and mutate nested fields
- [ ] Test: structs in collections (vec, map)
- [ ] All tests pass

### US-012: E2E tests — error cases

**Description:** As a developer, I need tests verifying that struct-related errors are caught at compile time.

**Acceptance Criteria:**
- [ ] Test: wrong number of fields in constructor → compile error
- [ ] Test: type mismatch in constructor argument → compile error
- [ ] Test: access nonexistent field → compile error
- [ ] Test: mutate field with wrong type → compile error
- [ ] Test: duplicate struct definition → compile error
- [ ] Test: duplicate field name in struct → compile error
- [ ] Test: undefined struct type in field → compile error
- [ ] All tests pass

## Functional Requirements

- FR-1: `defstruct Name [field1 :type1] [field2 :type2] ...` declares a named struct type at the top level
- FR-2: `[Name val1 val2 ...]` instantiates a struct with positional field values
- FR-3: `[. $struct field]` reads a field value
- FR-4: `[.set! $struct field value]` mutates a field in place, returns the struct
- FR-5: Struct memory layout follows C ABI rules: fields in declaration order, natural alignment, trailing padding
- FR-6: Struct values use a new `JACL_TAG_STRUCT` tag and are heap-allocated
- FR-7: Struct values can be boxed into `dyn` and stored in `vec` and `map`
- FR-8: The compiler tracks struct types and enforces field types at compile time
- FR-9: Struct types are exportable/importable across modules (following existing `use` + name list convention)
- FR-10: Inline anonymous struct types are supported in type annotation positions: `:struct{field:type,...}`
- FR-11: The GC traces struct fields for heap-allocated values

## Non-Goals

- No methods or associated functions on structs (structs are pure data)
- No generic/parameterized struct types
- No pattern matching or destructuring on structs
- No default field values
- No struct inheritance or composition operators
- No FFI implementation (this milestone only prepares the struct infrastructure)
- No packed/repr attributes for controlling layout (C ABI only)
- No `impl` blocks or trait implementations for structs

## Technical Considerations

- **Value representation:** Struct values will need a new tag (`JACL_TAG_STRUCT`). The heap object should store a pointer to type metadata (field count, field types, field offsets) plus the field data itself.
- **Type metadata:** A global struct type registry in the compiler, keyed by name. Each entry stores field names, types, byte offsets, and total size. Inline anonymous structs get compiler-generated internal names.
- **C ABI layout:** Field offsets computed at struct registration time using natural alignment rules (field aligned to its own size, struct padded to largest field alignment). This matches what a C compiler would produce for the equivalent `struct`.
- **NaN-boxed fields:** Struct fields that are `dyn`-typed store full 64-bit `JaclVal` values. Typed fields (e.g., `:i32`) store unboxed values at their natural size. This means field offsets are not uniform — they depend on field types.
- **GC integration:** The struct type metadata must include enough information for the GC to know which fields are heap pointers that need tracing. A simple bitmap (one bit per field: "is heap-allocated type") would work.
- **Module integration:** Struct type metadata needs to be serialized into the module's export table so that importing modules can reconstruct the type registry entry. This extends the existing `ExportEntry` system.

## Success Metrics

- All struct operations (create, access, mutate) work correctly with the type system
- Struct layout matches equivalent C struct layout for all supported field types
- Structs work seamlessly with existing `vec`, `map`, and `dyn` infrastructure
- No GC-related memory bugs with struct values
- All existing M0–M14 tests continue to pass

## Resolved Questions

- **`is-struct?` predicate:** Yes, will be needed for runtime type checking of boxed structs in `dyn` collections. Deferred to a follow-up story — not required for the core struct infrastructure.
- **Deep access shorthand (`[.. $line start x]`):** Deferred. Chained `[.]` calls work fine for now; sugar can come later or via the macro system (M15).
- **`repr` annotation vs always-C-layout:** All structs always use C ABI layout. One layout engine, simpler implementation, and the whole point is FFI prep. A `repr` annotation can be revisited if profiling shows C padding is wasteful for JACL-only structs.
