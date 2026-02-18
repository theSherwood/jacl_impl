# PRD: JACL Milestone 0 — Value Representation

## Introduction

Implement the foundational 64-bit tagged value system for JACL (Just A Command Lisp). Every value in JACL's dynamic runtime is represented as a single 64-bit word (`JaclVal`) where the top byte encodes type and metadata flags, and the bottom 56 bits hold the payload. This is the foundation that all subsequent milestones build upon — the lexer, parser, compiler, VM, GC, and concurrency system all depend on this representation.

This milestone also includes basic arithmetic and comparison operations on `JaclVal`, with proper flag propagation, so that the value system is immediately usable when the VM arrives in Milestone 3.

## Goals

- Define the canonical `JaclVal` 64-bit tagged value type used throughout the entire JACL runtime
- Support all inline value types: nil, bool, i32, f32
- Support pointer-payload constructors for heap-allocated types (string, vector, map, closure, bignum, etc.)
- Pack short strings (0–7 bytes) directly into the 56-bit payload with zero heap allocation
- Propagate tainted/secret/error metadata flags through operations automatically
- Provide arithmetic and comparison operations on boxed numeric types
- Deliver a comprehensive test suite proving correctness at boundary conditions

## User Stories

### US-001: Define JaclVal Type and Tag Layout
**Description:** As a runtime developer, I need the core `JaclVal` typedef and tag-byte constants so that all modules share a single value representation.

**Acceptance Criteria:**
- [ ] `JaclVal` is a 64-bit type (e.g., `uint64_t` typedef)
- [ ] Tag byte occupies bits 63–56 of the 64-bit word
- [ ] Tag byte layout: bit 7 = tainted, bit 6 = secret, bit 5 = error, bits 4–0 = type tag
- [ ] Type tag constants defined for at minimum: NIL, BOOL, I32, F32, STRING (inline), STRING (heap pointer), VECTOR, MAP, CLOSURE, BIGNUM
- [ ] Payload occupies bits 55–0 (56 bits)
- [ ] All constants and the typedef are defined in `value/value.h`
- [ ] Compiles cleanly with no warnings under `-Wall -Wextra`

### US-002: Nil and Boolean Constructors/Extractors
**Description:** As a runtime developer, I need to create and inspect nil and boolean values so that control flow and absence-of-value can be represented.

**Acceptance Criteria:**
- [ ] `JACL_NIL` constant or `jacl_nil()` constructor produces a nil-tagged value with zero payload
- [ ] `jacl_bool(bool b)` constructor produces a bool-tagged value with 0 or 1 payload
- [ ] `jacl_is_nil(JaclVal v)` returns true only for nil-tagged values
- [ ] `jacl_is_bool(JaclVal v)` returns true only for bool-tagged values
- [ ] `jacl_as_bool(JaclVal v)` extracts the boolean payload
- [ ] `JACL_TRUE` and `JACL_FALSE` convenience constants
- [ ] Round-trip: `jacl_as_bool(jacl_bool(x)) == x` for both true and false
- [ ] Tests verify nil is not bool, bool is not nil

### US-003: i32 Constructors/Extractors
**Description:** As a runtime developer, I need to store and retrieve 32-bit signed integers inline in a JaclVal so that integer arithmetic works without heap allocation.

**Acceptance Criteria:**
- [ ] `jacl_i32(int32_t n)` constructor packs n into the 56-bit payload
- [ ] `jacl_is_i32(JaclVal v)` returns true only for i32-tagged values
- [ ] `jacl_as_i32(JaclVal v)` extracts the int32_t payload
- [ ] Round-trip: `jacl_as_i32(jacl_i32(n)) == n` for all int32_t values
- [ ] Tests cover: 0, 1, -1, INT32_MAX, INT32_MIN
- [ ] Sign extension is handled correctly (negative values survive round-trip)

### US-004: f32 Constructors/Extractors
**Description:** As a runtime developer, I need to store and retrieve 32-bit floats inline in a JaclVal so that floating-point arithmetic works without heap allocation.

**Acceptance Criteria:**
- [ ] `jacl_f32(float f)` constructor packs f into the 56-bit payload (bit-cast, not conversion)
- [ ] `jacl_is_f32(JaclVal v)` returns true only for f32-tagged values
- [ ] `jacl_as_f32(JaclVal v)` extracts the float payload via bit-cast
- [ ] Round-trip: `jacl_as_f32(jacl_f32(f))` is bitwise identical to f
- [ ] Tests cover: 0.0f, -0.0f, 1.0f, -1.0f, FLT_MAX, FLT_MIN, FLT_EPSILON, INFINITY, -INFINITY, NAN
- [ ] NaN round-trips correctly (bitwise, not just `isnan()`)

### US-005: Pointer-Payload Constructors/Extractors
**Description:** As a runtime developer, I need to pack heap pointers into JaclVal for types that don't fit inline (strings, vectors, maps, closures, etc.) so that heap objects participate in the tagged value system.

**Acceptance Criteria:**
- [ ] Constructor for each pointer-carrying type tag: e.g., `jacl_string_ptr(void *p)`, `jacl_vector_ptr(void *p)`, `jacl_map_ptr(void *p)`, `jacl_closure_ptr(void *p)`, `jacl_bignum_ptr(void *p)`
- [ ] Each constructor stores the pointer in the low 56 bits with the appropriate type tag in the high byte
- [ ] Corresponding `jacl_as_ptr(JaclVal v)` extracts the pointer (with tag byte masked off)
- [ ] Type predicates: `jacl_is_string(v)`, `jacl_is_vector(v)`, `jacl_is_map(v)`, `jacl_is_closure(v)`, `jacl_is_bignum(v)`
- [ ] Round-trip: pointer survives pack/unpack unchanged
- [ ] Tests use aligned heap-allocated objects to verify pointer integrity
- [ ] This milestone does NOT define heap object layouts — only the JaclVal packing

### US-006: Inline String Packing
**Description:** As a runtime developer, I need to store short strings (0–7 bytes) directly in the 56-bit payload so that common short strings (command names, flags, short identifiers) require zero heap allocation.

**Acceptance Criteria:**
- [ ] `jacl_inline_string(const char *s, size_t len)` packs up to 7 bytes into the payload
- [ ] Length is determined by null-termination within the 7-byte payload area; if all 7 bytes are non-null, length is 7
- [ ] Inline strings must NOT contain null bytes (null is the terminator, not data)
- [ ] `jacl_is_inline_string(JaclVal v)` distinguishes inline strings from heap string pointers
- [ ] `jacl_inline_string_len(JaclVal v)` returns the length (0–7)
- [ ] `jacl_inline_string_get(JaclVal v, char *buf, size_t buflen)` copies the string into a buffer
- [ ] Round-trip: pack then unpack yields identical bytes and length
- [ ] Tests cover: empty string (len 0), single char, 6 bytes, 7 bytes (max), strings with various byte values (excluding null)
- [ ] Test that inline string type tag is distinct from heap string pointer type tag

### US-007: Tainted/Secret/Error Flag Manipulation
**Description:** As a runtime developer, I need to get, set, and clear the metadata flags (tainted, secret, error) on any JaclVal so that security and error metadata flow through the system.

**Acceptance Criteria:**
- [ ] `jacl_is_tainted(JaclVal v)` / `jacl_set_tainted(JaclVal v)` / `jacl_clear_tainted(JaclVal v)`
- [ ] `jacl_is_secret(JaclVal v)` / `jacl_set_secret(JaclVal v)` / `jacl_clear_secret(JaclVal v)`
- [ ] `jacl_is_error(JaclVal v)` / `jacl_set_error(JaclVal v)` / `jacl_clear_error(JaclVal v)`
- [ ] Setting a flag preserves the type tag and payload
- [ ] Clearing a flag preserves the type tag and payload
- [ ] Multiple flags can be set simultaneously (e.g., tainted + error)
- [ ] Tests verify flag independence (setting one doesn't affect others)
- [ ] Tests verify flags survive with all value types (nil, bool, i32, f32, pointer, inline string)

### US-008: Flag Propagation Helpers
**Description:** As a runtime developer, I need helpers that combine flags from operands so that operations automatically propagate tainted/secret/error metadata without manual flag management.

**Acceptance Criteria:**
- [ ] `jacl_propagate_flags(JaclVal a, JaclVal b)` returns the OR of both values' flag bits (tainted, secret, error)
- [ ] A helper or macro to apply propagated flags to a result value
- [ ] If either operand is error-flagged, the result carries the error flag
- [ ] If either operand is tainted, the result carries the tainted flag
- [ ] If either operand is secret, the result carries the secret flag
- [ ] Tests verify all combinations: no flags, one flag each, mixed flags, all flags

### US-009: Type Predicate Macros
**Description:** As a runtime developer, I need a complete set of type-checking macros/functions so that any code handling JaclVal can quickly determine the value's type.

**Acceptance Criteria:**
- [ ] Predicates for every defined type tag: `jacl_is_nil`, `jacl_is_bool`, `jacl_is_i32`, `jacl_is_f32`, `jacl_is_inline_string`, `jacl_is_string` (heap), `jacl_is_vector`, `jacl_is_map`, `jacl_is_closure`, `jacl_is_bignum`
- [ ] `jacl_type_tag(JaclVal v)` extracts the 5-bit type tag (ignoring flag bits)
- [ ] Predicates ignore flag bits — an error-flagged i32 still returns true for `jacl_is_i32`
- [ ] Each predicate returns false for all other types (no false positives)
- [ ] Tests verify each predicate against every other type

### US-010: i32 Arithmetic Operations
**Description:** As a runtime developer, I need arithmetic operations on i32 JaclVal operands so that the VM can execute integer math with proper flag propagation.

**Acceptance Criteria:**
- [ ] `jacl_add_i32(JaclVal a, JaclVal b)` — returns i32 sum with propagated flags
- [ ] `jacl_sub_i32(JaclVal a, JaclVal b)` — returns i32 difference with propagated flags
- [ ] `jacl_mul_i32(JaclVal a, JaclVal b)` — returns i32 product with propagated flags
- [ ] `jacl_div_i32(JaclVal a, JaclVal b)` — returns i32 quotient with propagated flags; division by zero returns an error-flagged value
- [ ] `jacl_mod_i32(JaclVal a, JaclVal b)` — returns i32 remainder with propagated flags; mod by zero returns an error-flagged value
- [ ] `jacl_neg_i32(JaclVal a)` — returns negated i32 with preserved flags
- [ ] If either operand is error-flagged, the error is returned immediately (no computation)
- [ ] Tests cover: basic operations, flag propagation, division by zero, INT32_MIN edge cases, error propagation

### US-011: f32 Arithmetic Operations
**Description:** As a runtime developer, I need arithmetic operations on f32 JaclVal operands so that the VM can execute floating-point math with proper flag propagation.

**Acceptance Criteria:**
- [ ] `jacl_add_f32(JaclVal a, JaclVal b)` — returns f32 sum with propagated flags
- [ ] `jacl_sub_f32(JaclVal a, JaclVal b)` — returns f32 difference with propagated flags
- [ ] `jacl_mul_f32(JaclVal a, JaclVal b)` — returns f32 product with propagated flags
- [ ] `jacl_div_f32(JaclVal a, JaclVal b)` — returns f32 quotient with propagated flags (follows IEEE 754 for division by zero — returns inf, not error)
- [ ] `jacl_neg_f32(JaclVal a)` — returns negated f32 with preserved flags
- [ ] If either operand is error-flagged, the error is returned immediately
- [ ] Tests cover: basic operations, flag propagation, inf/nan behavior, error propagation

### US-012: Comparison Operations
**Description:** As a runtime developer, I need comparison operations that work across value types so that the VM can evaluate conditionals.

**Acceptance Criteria:**
- [ ] `jacl_eq(JaclVal a, JaclVal b)` — returns JaclVal bool; two values are equal if same type tag and same payload
- [ ] `jacl_lt_i32(JaclVal a, JaclVal b)` — returns JaclVal bool for i32 less-than
- [ ] `jacl_gt_i32(JaclVal a, JaclVal b)` — returns JaclVal bool for i32 greater-than
- [ ] `jacl_le_i32(JaclVal a, JaclVal b)` — returns JaclVal bool for i32 less-or-equal
- [ ] `jacl_ge_i32(JaclVal a, JaclVal b)` — returns JaclVal bool for i32 greater-or-equal
- [ ] `jacl_lt_f32` / `jacl_gt_f32` / `jacl_le_f32` / `jacl_ge_f32` — same for f32
- [ ] All comparisons propagate flags to the boolean result
- [ ] If either operand is error-flagged, comparisons return the error (not a bool)
- [ ] `jacl_eq` handles nil (nil equals nil), bools, inline strings (bytewise), and pointer types (pointer equality)
- [ ] Tests cover: same-type equality, cross-type inequality, nil comparisons, flag propagation, error short-circuit

## Functional Requirements

- FR-1: `JaclVal` must be exactly 64 bits wide, passable by value, and storable in arrays/stacks without indirection
- FR-2: The tag byte must occupy the most-significant byte (bits 63–56) so that pointer payloads in the low 56 bits are usable on current 64-bit architectures
- FR-3: Flag bits (tainted, secret, error) must be orthogonal to type tags — any type can carry any combination of flags
- FR-4: Inline strings use null-termination within the 7-byte payload to determine length; inline strings must not contain null bytes
- FR-5: Pointer payloads must preserve the original pointer value through pack/unpack (mask off tag byte, no sign extension)
- FR-6: Error-flagged operands must short-circuit arithmetic and comparison operations (return the error immediately, no computation)
- FR-7: Flag propagation must OR the flags of both operands onto the result for all binary operations
- FR-8: f32 division by zero must follow IEEE 754 (produce infinity), not return an error-flagged value
- FR-9: i32 division by zero must return an error-flagged value
- FR-10: All public API must be defined in `value/value.h` (single header for the value system)
- FR-11: Implementation may use macros or static inline functions; performance-critical paths (constructors, extractors, predicates) should be inlineable

## Non-Goals (Out of Scope)

- Heap object layouts (string headers, vector nodes, map nodes, closures) — deferred to later milestones
- Heap allocation or memory management — no GC, no arena integration in this milestone
- Unboxed/static-typed value representation — that's Milestone 9
- String interning or rope-backed strings — that's Milestone 5
- Dynamic dispatch or runtime type errors (e.g., "cannot add string and int") — that's the VM in Milestone 3
- Any parsing, compilation, or execution infrastructure

## Technical Considerations

- **Platform dependency:** Uses `uint64_t` from `<stdint.h>`. May use compiler intrinsics for bit manipulation if available via `platform/platform.h`
- **Endianness:** Tag-in-high-byte layout assumes little-endian or uses explicit shifting. Verify on target platforms
- **Pointer width:** 56 bits covers current x86-64 and ARM64 address spaces (both use at most 48–52 bits for user-space addresses). This should be asserted at compile time
- **Existing test harness:** Use `test/test_helpers.h` for the test suite
- **Naming convention:** Follow existing project conventions (check other `.h` files for style)
- **Build system:** Must integrate with existing build (check Makefile/CMake)

## Success Metrics

- All construct/extract round-trips pass for every type
- Flag propagation is correct for all flag combinations (2^3 = 8 combinations per operand, 64 per pair)
- Inline string pack/unpack works for all lengths 0–7
- Arithmetic edge cases (INT32_MIN, division by zero, NaN, inf) are handled correctly
- Zero heap allocations for nil, bool, i32, f32, and inline string operations
- All tests pass under address sanitizer (no UB in bit manipulation)

## Open Questions

- Should `jacl_eq` for pointer types do pointer equality only, or should it dereference and compare structurally? (Pointer-only is simpler and correct for this milestone; structural comparison can be added later.)
- Should arithmetic operations assert that operands are the correct type, or return an error-flagged value on type mismatch? (Assert is simpler for now; type checking moves to the VM layer.)
- Exact naming convention: `jacl_` prefix vs `JACL_` for macros vs something else — follow existing project conventions.
