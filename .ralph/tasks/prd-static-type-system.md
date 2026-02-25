# PRD: M11 — Static Type System

## Introduction

Add gradual static typing to JACL. Type annotations on `def`, `mut`, and `proc` enable the compiler to emit unboxed, type-specialized opcodes that skip runtime type dispatch. The full numeric tower — i32, i64, u32, u64, f32, f64 — is supported, with unboxed 64-bit arithmetic opcodes for i64, u64, and f64. All existing untyped code continues to work unchanged (`dyn` is the default). Type mismatches are compile-time errors — no runtime type coercion or implicit promotion. A `to` builtin provides explicit conversions between types.

## Goals

- Support type annotations on `def`, `mut`, and `proc` (parameters + return type) — all optional
- Full numeric tower: i32, i64, u32, u64, f32, f64 as first-class typed values
- Emit unboxed arithmetic/comparison opcodes for i64, u64, f64 (skip runtime type dispatch)
- Compile-time type checking — mismatched types are compile errors, not runtime errors
- Infer result types through expressions (`i64 + i64 → i64`)
- Contextual typing — literals adopt the expected type in typed contexts
- Explicit conversions via `[to TYPE expr]` — no implicit promotion
- Full backward compatibility — all existing untyped JACL code works unchanged

## User Stories

### US-001: Value representation for new numeric types
**Description:** As a developer, I need runtime representations for u32, i64, u64, and f64 values so that typed computations have concrete value types to operate on.

**Acceptance Criteria:**
- [ ] Add `JACL_TAG_U32` (0x0D) — unsigned 32-bit integer stored inline in payload (like i32)
- [ ] Add `JACL_TAG_I64` (0x0E) — heap pointer to `int64_t`
- [ ] Add `JACL_TAG_U64` (0x0F) — heap pointer to `uint64_t`
- [ ] Add `JACL_TAG_F64` (0x10) — heap pointer to `double`
- [ ] Constructors: `jacl_u32(uint32_t)`, `jacl_i64(int64_t)`, `jacl_u64(uint64_t)`, `jacl_f64(double)`
- [ ] Accessors: `jacl_as_u32()`, `jacl_as_i64()`, `jacl_as_u64()`, `jacl_as_f64()`
- [ ] Predicates: `jacl_is_u32()`, `jacl_is_i64()`, `jacl_is_u64()`, `jacl_is_f64()`
- [ ] Heap types: arena-allocated structs `JaclHeapI64 { int64_t value; }`, `JaclHeapU64 { uint64_t value; }`, `JaclHeapF64 { double value; }`
- [ ] Arithmetic functions for each new type following existing i32/f32 pattern:
  - `jacl_i64_add/sub/mul/div/mod/neg`, `jacl_i64_lt/gt/le/ge/eq`
  - `jacl_u64_add/sub/mul/div/mod/neg`, `jacl_u64_lt/gt/le/ge/eq`
  - `jacl_u32_add/sub/mul/div/mod/neg`, `jacl_u32_lt/gt/le/ge/eq`
  - `jacl_f64_add/sub/mul/div/mod/neg`, `jacl_f64_lt/gt/le/ge/eq`
- [ ] Division by zero sets the error flag (consistent with existing i32/f32 behavior)
- [ ] Flag propagation via `jacl_propagate_flags()` / `jacl_apply_flags()` for all new arithmetic
- [ ] Add u32 dispatch cases to existing `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV`, `OP_MOD`, `OP_NEG`, `OP_LT`, `OP_GT`, `OP_LE`, `OP_GE`, `OP_EQ` in the VM (so dynamically-typed u32 values work)
- [ ] Typecheck/build passes

### US-002: Typed opcodes and VM execution
**Description:** As a developer, I need specialized opcodes for 64-bit arithmetic so that typed code runs without runtime type dispatch overhead.

**Acceptance Criteria:**
- [ ] i64 opcodes (11): `OP_ADD_I64`, `OP_SUB_I64`, `OP_MUL_I64`, `OP_DIV_I64`, `OP_MOD_I64`, `OP_NEG_I64`, `OP_LT_I64`, `OP_GT_I64`, `OP_LE_I64`, `OP_GE_I64`, `OP_EQ_I64`
- [ ] u64 opcodes (6 unique + reuse i64 for add/sub/mul/neg/eq): `OP_DIV_U64`, `OP_MOD_U64`, `OP_LT_U64`, `OP_GT_U64`, `OP_LE_U64`, `OP_GE_U64`
- [ ] f64 opcodes (11): `OP_ADD_F64`, `OP_SUB_F64`, `OP_MUL_F64`, `OP_DIV_F64`, `OP_MOD_F64`, `OP_NEG_F64`, `OP_LT_F64`, `OP_GT_F64`, `OP_LE_F64`, `OP_GE_F64`, `OP_EQ_F64`
- [ ] Conversion opcodes (7): `OP_TO_I32`, `OP_TO_I64`, `OP_TO_U32`, `OP_TO_U64`, `OP_TO_F32`, `OP_TO_F64`, `OP_TO_DYN`
- [ ] Typed constant opcodes (3): `OP_CONST_I64`, `OP_CONST_U64`, `OP_CONST_F64` — load raw 64-bit value from constant pool
- [ ] All typed arithmetic opcodes operate on raw (unboxed) 64-bit stack values — no tag checking, no dispatch
- [ ] u64 add/sub/mul/neg/eq reuse the i64 opcodes (identical bit patterns in two's complement)
- [ ] VM handlers for all new opcodes in `vm__run()`
- [ ] `OP_TO_DYN` boxes a raw stack value into the appropriate tagged `JaclVal` (using the source type tracked by the compiler, encoded as an operand byte)
- [ ] Typed constants stored as raw 64-bit values in the constant pool; `OP_CONST_I64`/`OP_CONST_U64`/`OP_CONST_F64` push them to the stack unboxed
- [ ] Typecheck/build passes

### US-003: Type system compiler infrastructure
**Description:** As a developer, I need compile-time type tracking so the compiler can propagate types through expressions, check for mismatches, and emit typed opcodes.

**Acceptance Criteria:**
- [ ] Define `JaclType` enum: `TYPE_DYN` (0, default), `TYPE_BOOL`, `TYPE_NIL`, `TYPE_I32`, `TYPE_I64`, `TYPE_U32`, `TYPE_U64`, `TYPE_F32`, `TYPE_F64`, `TYPE_STR`, `TYPE_VEC`, `TYPE_MAP`, `TYPE_CLOSURE`
- [ ] Add `JaclType type` field to `Local` struct (default `TYPE_DYN`)
- [ ] Add `JaclType type` field to `Upvalue` struct (default `TYPE_DYN`)
- [ ] Add `JaclType type` field to global info tracking (default `TYPE_DYN`)
- [ ] Add helper: `bool is_type_keyword(const char* word)` — returns true for "i32", "i64", "u32", "u64", "f32", "f64", "bool", "str", "dyn"
- [ ] Add helper: `JaclType type_from_keyword(const char* word)` — maps keyword string to `JaclType`
- [ ] Add helper: `const char* type_name(JaclType t)` — maps `JaclType` to display string for error messages
- [ ] Add helper: `bool is_numeric_type(JaclType t)` — true for i32/i64/u32/u64/f32/f64
- [ ] Add helper: `bool is_unboxed_type(JaclType t)` — true for i64/u64/f64 (types that live unboxed on stack)
- [ ] Add compile error helper: `compiler__type_error(Compiler*, const char* fmt, ...)` — reports type mismatch with source location
- [ ] Typecheck/build passes

### US-004: Typed `def` — declare typed immutable bindings
**Description:** As a JACL programmer, I want to declare typed variables with `[def TYPE name value]` so the compiler can emit efficient unboxed code and catch type errors at compile time.

**Acceptance Criteria:**
- [ ] `[def i64 x 42]` declares `x` as type `i64`, compiles `42` as a raw i64 constant
- [ ] `[def f64 pi 3.14]` declares `pi` as type `f64`, compiles `3.14` as a raw f64 constant
- [ ] `[def u32 flags 0]` declares `flags` as type `u32`
- [ ] `[def u64 big 999]` declares `big` as type `u64`
- [ ] `[def i32 n 42]` declares `n` as type `i32` (uses existing tagged i32 representation)
- [ ] `[def f32 v 1.5]` declares `v` as type `f32` (uses existing tagged f32 representation)
- [ ] Untyped `[def x 42]` continues to work unchanged (type is `TYPE_DYN`)
- [ ] Disambiguation: `def` with 3 args where first arg is a type keyword → typed def. With 2 args → untyped def. With 3 args where first is NOT a type keyword → compile error.
- [ ] Compiler stores the declared type in the `Local` entry (local scope) or global info (global scope)
- [ ] Reading a typed local via `$x` pushes the correctly-typed value (unboxed for i64/u64/f64)
- [ ] Typed globals: `[def i64 x 42]` at global scope stores a boxed `JACL_TAG_I64` in the environment (globals are always boxed); reading emits `OP_GET_GLOBAL` + unbox conversion if the declared type is unboxed
- [ ] Compile error if the RHS expression's type conflicts with the declared type (e.g., `[def i64 x "hello"]` → "type error: expected i64, got str")
- [ ] Typecheck/build passes

### US-005: Typed `mut` and type-checked `set!`
**Description:** As a JACL programmer, I want to declare typed mutable variables and have `set!` enforce the type so type safety is maintained through mutations.

**Acceptance Criteria:**
- [ ] `[mut i64 counter 0]` declares a mutable `i64` binding
- [ ] `[mut f64 total 0.0]` declares a mutable `f64` binding
- [ ] Untyped `[mut x 0]` continues to work unchanged (type is `TYPE_DYN`)
- [ ] Disambiguation: same as `def` — 3 args with type keyword → typed mut
- [ ] `[set! counter 10]` succeeds — literal `10` adopts type `i64` from context
- [ ] `[set! counter "hello"]` → compile error: "type error: cannot assign str to i64 binding 'counter'"
- [ ] `[set! counter $y]` where `y` is `dyn` → compile error: "type error: cannot assign dyn to i64 binding 'counter'; use [to i64 $y]"
- [ ] `[set! counter $y]` where `y` is `i64` → OK, emits typed set
- [ ] Mutable typed locals use cells (from M10) — the cell contains an unboxed value for i64/u64/f64 types
- [ ] Type info propagates through closure capture — a `mut i64` captured as upvalue retains its `i64` type
- [ ] Typecheck/build passes

### US-006: Typed arithmetic — emit typed opcodes for typed operands
**Description:** As a JACL programmer, I want arithmetic on typed values to compile to fast, specialized opcodes without runtime type dispatch.

**Acceptance Criteria:**
- [ ] `[+ $a $b]` where both `a` and `b` are `i64` → emits `OP_ADD_I64` (not `OP_ADD`)
- [ ] `[* $a $b]` where both are `f64` → emits `OP_MUL_F64`
- [ ] `[/ $a $b]` where both are `u64` → emits `OP_DIV_U64` (unsigned division)
- [ ] `[< $a $b]` where both are `u64` → emits `OP_LT_U64` (unsigned comparison)
- [ ] `[- $a]` (unary negate) where `a` is `i64` → emits `OP_NEG_I64`
- [ ] `[+ $a $b]` where both are `i32` → emits existing `OP_ADD` (i32 uses dynamic dispatch, still efficient)
- [ ] `[+ $a $b]` where both are `u32` → emits existing `OP_ADD` (u32 added to dynamic dispatch in US-001)
- [ ] `[+ $a $b]` where both are `dyn` → emits existing `OP_ADD` (fully backward compatible)
- [ ] `[+ $a $b]` where `a` is `i64` and `b` is `f64` → compile error: "type error: cannot add i64 and f64; use [to] to convert"
- [ ] `[+ $a $b]` where `a` is `i64` and `b` is `dyn` → compile error: "type error: cannot add i64 and dyn; use [to i64 $b] to convert"
- [ ] `[+ $a 1]` where `a` is `i64` and `1` is a literal → contextual typing: `1` compiled as raw i64 constant, emits `OP_ADD_I64`
- [ ] `[+ $a 1.5]` where `a` is `f64` → `1.5` compiled as raw f64, emits `OP_ADD_F64`
- [ ] Result type propagation: `[def y [+ $a $b]]` where `a`, `b` are `i64` → `y` inferred as `i64` (when `def` has no explicit type annotation, infer from RHS)
- [ ] Nested expressions: `[+ [* $a $b] $c]` where all are `i64` → emits `OP_MUL_I64` then `OP_ADD_I64`, result is `i64`
- [ ] Typecheck/build passes

### US-007: `to` builtin for explicit type conversions
**Description:** As a JACL programmer, I want to explicitly convert between types using `[to TYPE expr]` so I can bridge typed and dynamic code.

**Acceptance Criteria:**
- [ ] Syntax: `[to TYPE expr]` where TYPE is a type keyword (i32, i64, u32, u64, f32, f64, dyn)
- [ ] Arity: exactly 2 arguments (type + expression); compile error otherwise
- [ ] `[to i64 $x]` where `x` is `i32` → widen i32 to i64 (sign-extend), result type is `i64`
- [ ] `[to i64 $x]` where `x` is `u32` → widen u32 to i64 (zero-extend)
- [ ] `[to i64 $x]` where `x` is `f64` → truncate to integer
- [ ] `[to i64 $x]` where `x` is `dyn` → runtime unbox: extract numeric value, convert to i64, push raw. Runtime error if value is not numeric.
- [ ] `[to f64 $x]` where `x` is `i64` → convert integer to double
- [ ] `[to f64 $x]` where `x` is `i32` → widen to double
- [ ] `[to dyn $x]` where `x` is `i64` → box into `JACL_TAG_I64` tagged value
- [ ] `[to dyn $x]` where `x` is `f64` → box into `JACL_TAG_F64` tagged value
- [ ] `[to dyn $x]` where `x` is `i32` → box into `JACL_TAG_I32` (already tagged, no-op)
- [ ] `[to dyn $x]` where `x` is `dyn` → no-op
- [ ] `[to i32 $x]` where `x` is `i64` → narrow (truncate to 32 bits)
- [ ] `[to u32 $x]` where `x` is `u64` → narrow (truncate to 32 bits)
- [ ] Compiler resolves the conversion statically when both source and target types are known → emits the appropriate `OP_TO_*` opcode
- [ ] `[to i64 "hello"]` → compile error: "type error: cannot convert str to i64"
- [ ] Result type of `[to TYPE expr]` is always `TYPE`
- [ ] Error propagation: if `expr` is error-flagged, return the error unchanged
- [ ] Typecheck/build passes

### US-008: Typed `proc` — optional return type and parameter types
**Description:** As a JACL programmer, I want to annotate proc parameters and return types so that the compiler checks call sites and body expressions for type correctness.

**Acceptance Criteria:**
- [ ] Return type syntax: `[proc TYPE name params body]` — 4 args, first is type keyword
  - `[proc i64 add [a b] { [+ $a $b] }]` — return type i64, params untyped
  - Disambiguation: if `proc` has 4 args and first arg is a type keyword → has return type. Otherwise 3 args → no return type (existing behavior).
- [ ] Parameter type syntax: type keywords interleaved in param list
  - `[proc add [i64 a i64 b] { [+ $a $b] }]` — params `a: i64`, `b: i64`, no return type
  - `[proc i64 add [i64 a i64 b] { [+ $a $b] }]` — return type i64, both params i64
  - `[proc fmt [i64 n str label] { ... }]` — param `n: i64`, param `label: str`
  - `[proc add [a b] { ... }]` — fully untyped (existing behavior, all params `dyn`)
- [ ] Param list parsing: walk the flat list of elements; if current element is a type keyword and next element exists, next element is the parameter name with that type. Otherwise the current element is an untyped parameter name.
- [ ] Variadic params: `[proc f [i64 a & rest] { ... }]` — `rest` is always `dyn` (variadic rest cannot be typed)
- [ ] Compiler stores parameter types and return type alongside arity info
- [ ] Call-site checking: `[add 1 2]` where `add` expects `[i64 a i64 b]` → literals `1`, `2` adopt type `i64` via contextual typing
- [ ] Call-site error: `[add "x" 2]` → compile error: "type error: argument 1 of 'add' expected i64, got str"
- [ ] Call-site error: `[add $x 2]` where `x` is `dyn` → compile error: "type error: argument 1 of 'add' expected i64, got dyn; use [to i64 $x]"
- [ ] Return type checking: if return type is declared, the body's result type must match
  - `[proc i64 bad [] { "hello" }]` → compile error: "type error: proc 'bad' declared return type i64, but body returns str"
- [ ] Return type inference at call site: `[def y [add 1 2]]` → `y` inferred as `i64` (from `add`'s return type)
- [ ] Untyped procs continue to work unchanged — all params and return are `dyn`
- [ ] Typecheck/build passes

### US-009: Update `print` and type predicates for new types
**Description:** As a JACL programmer, I want `print` to correctly display values of all numeric types, and I want type predicates for the new types.

**Acceptance Criteria:**
- [ ] `[print $x]` where `x` is typed `i64` → prints the integer (e.g., `42`). Compiler emits a typed-print sequence: box to dyn, then existing `OP_PRINT`.
- [ ] `[print $x]` where `x` is typed `f64` → prints the float (e.g., `3.14`)
- [ ] `[print $x]` where `x` is typed `u32` → prints unsigned integer (e.g., `255`)
- [ ] `[print $x]` where `x` is typed `u64` → prints unsigned integer
- [ ] Boxed `JACL_TAG_I64` prints correctly when used in dynamic context (e.g., in a vector)
- [ ] Boxed `JACL_TAG_U32`, `JACL_TAG_U64`, `JACL_TAG_F64` print correctly
- [ ] `[to-string $x]` works for all new types (used by string interpolation)
- [ ] No regression on existing print behavior for i32, f32, strings, etc.
- [ ] Typecheck/build passes

### US-010: Comprehensive M11 integration tests
**Description:** As a developer, I want comprehensive tests covering all type system features to ensure correctness and prevent regressions.

**Acceptance Criteria:**
- [ ] Unit tests in `test/test_m11.c` covering:
  - Value representation: u32, i64, u64, f64 constructors, accessors, predicates
  - Arithmetic functions: all new type arithmetic with edge cases (overflow, division by zero, max/min values)
  - Typed opcodes: direct bytecode emission and VM execution for all i64/u64/f64 opcodes
  - Conversion opcodes: all TO_* variants with valid and edge-case inputs
  - Typed def: i32, i64, u32, u64, f32, f64 bindings with correct values
  - Typed mut/set!: mutable typed bindings, type-checked reassignment
  - Typed proc: return type, param types, mixed typed/untyped params
  - Type checking: compile errors for mismatched types, undeclared type names
  - Contextual typing: literals adopting expected types
  - Type inference: result types from expressions, inferred def types
  - `to` builtin: all valid conversions, compile errors for invalid ones
  - Cross-call checking: typed arguments, return type propagation
  - Typed closures: typed params/return in closures, typed upvalue capture
- [ ] `.jacl` harness tests in `test/jacl/` covering:
  - `typed_def.jacl` — typed `def` for all numeric types
  - `typed_mut.jacl` — typed `mut`/`set!` with type checking
  - `typed_proc.jacl` — typed procs with param and return types
  - `typed_arithmetic.jacl` — typed arithmetic and comparisons
  - `type_conversions.jacl` — `to` builtin for all conversion paths
  - `type_errors.jacl` — compile-time type error cases
- [ ] All existing M1–M10 tests pass without regression
- [ ] Typecheck/build passes

## Functional Requirements

- FR-1: Type annotations are optional. Omitting a type annotation defaults to `dyn` (dynamic). All existing untyped JACL code works unchanged.
- FR-2: `[def TYPE name value]` declares an immutable binding with the given type. `[def name value]` (no type) remains `dyn`. 3 args with first arg being a type keyword = typed def.
- FR-3: `[mut TYPE name value]` declares a mutable binding with the given type. `[set! name value]` checks the value's type against the binding's declared type at compile time.
- FR-4: `[proc TYPE name params body]` declares a proc with return type `TYPE`. Param types are interleaved in the param list: `[TYPE name TYPE name ...]`. Both return type and param types are optional.
- FR-5: Typed values of i64, u64, f64 live on the VM stack as raw (unboxed) 64-bit values. The compiler tracks their type — no runtime tag is present.
- FR-6: Typed values of i32, u32, f32 use the existing tagged representation on the stack. The compiler tracks their type for compile-time checking.
- FR-7: The compiler emits typed opcodes (`OP_ADD_I64`, `OP_MUL_F64`, etc.) when both operands have the same unboxed type. For i32/u32/f32, it emits the existing dynamic opcodes.
- FR-8: Type mismatches are compile-time errors. No implicit numeric promotion or coercion. `[+ $a $b]` where `a: i64` and `b: f64` → compile error.
- FR-9: Mixing typed and dynamic values requires explicit `[to TYPE expr]` conversion. `[+ $a $b]` where `a: i64` and `b: dyn` → compile error.
- FR-10: `[to TYPE expr]` converts between types explicitly. Valid conversions: any numeric type to any other numeric type, any type to `dyn`, `dyn` to any numeric type (runtime unbox). `str` ↔ numeric is a compile error.
- FR-11: Contextual typing: integer literals in a typed-integer context adopt that type. Float literals in a typed-float context adopt that type. This applies in `def`/`mut` RHS, `set!` RHS, proc arguments, and binary operator operands.
- FR-12: Type inference: the result type of `[+ $a $b]` where both are `i64` is `i64`. When `def` has no type annotation, the RHS expression type is inferred. `[def x [+ $a $b]]` where both `a`, `b` are `i64` → `x` is `i64`.
- FR-13: Call-site checking: when calling a proc with typed parameters, argument types must match. Literals use contextual typing. Non-matching arguments → compile error.
- FR-14: Return type checking: when a proc has a declared return type, the body's last expression type must match. Mismatch → compile error.
- FR-15: `[to dyn $x]` boxes an unboxed value into the corresponding tagged representation (`JACL_TAG_I64`, `JACL_TAG_U64`, `JACL_TAG_F64` for 64-bit types; existing tags for 32-bit types).
- FR-16: Error propagation: `to` propagates error flags. Typed arithmetic on error-flagged values propagates the error.

## Non-Goals (Out of Scope)

- No generic/parameterized types (e.g., `vec<i64>`) — deferred to future milestone
- No trait/protocol/interface system
- No pattern matching on types
- No typed collection elements — vectors and maps remain dynamically typed
- No typed closures/lambdas (no lambda syntax exists yet)
- No implicit numeric promotion (i32 → i64 auto-widening) — use explicit `to`
- No runtime type errors for typed code — all type errors are compile-time
- No type annotations on `box`/`atom` containers
- No bignum integration with the type system
- No optimization of i32/f32 to skip runtime dispatch (typed i32/f32 still use dynamic opcodes)

## Technical Considerations

- **Unboxed stack values:** The VM stack is `JaclVal` (`uint64_t`). Unboxed i64 values occupy a stack slot as raw `int64_t`. Unboxed f64 values are bit-cast `double`. The compiler tracks which slots are unboxed — the VM itself doesn't know (typed opcodes assume correct types).
- **New value tags (4):** `JACL_TAG_U32` (0x0D, inline), `JACL_TAG_I64` (0x0E, heap), `JACL_TAG_U64` (0x0F, heap), `JACL_TAG_F64` (0x10, heap). Heap types are arena-allocated structs holding a single 64-bit value. These tags are used only for boxed representations (dynamic context, collections, globals).
- **New opcodes (~38):** 11 i64 arithmetic/comparison, 6 u64 (div/mod/comparisons — add/sub/mul/neg/eq reuse i64), 11 f64, 7 conversion, 3 typed constants. All within the uint8_t opcode space (current count ~89, new total ~127 of 256).
- **Typed globals:** Global variables are stored in the Environment hash table as `JaclVal` (always boxed). Typed globals store boxed values (`JACL_TAG_I64` etc.). On read, the compiler emits `OP_GET_GLOBAL` followed by an unbox conversion if the variable's declared type is unboxed. On write (`def`/`set!`), the compiler emits a box conversion before `OP_DEF_GLOBAL`/`OP_SET_GLOBAL`.
- **Typed mutable locals:** `mut i64 x 0` creates a cell (from M10) whose contained value is a boxed `JACL_TAG_I64`. Reading emits `OP_GET_CELL_LOCAL` + unbox. Writing emits box + `OP_SET_CELL_LOCAL`. This preserves M10's cell-based mutation model.
- **Constant pool for typed constants:** Raw 64-bit values stored in the constant pool alongside existing `JaclVal` constants. `OP_CONST_I64 idx` reads 8 bytes from the constant pool at offset `idx` and pushes them as a raw stack value.
- **OP_TO_DYN operand:** Since the VM doesn't know the source type of an unboxed value, `OP_TO_DYN` takes a 1-byte type operand indicating the source type (e.g., TYPE_I64, TYPE_F64). The VM uses this to construct the correct tagged value.
- **Compiler type tracking:** Each expression compilation returns a `JaclType` indicating the result type pushed to the stack. The compiler threads this through binary ops, `if` branches (both branches must agree), proc calls (from return type), etc.
- **Param list parsing:** The compiler walks the param list (AST_COMMAND with head + children as a flat list). For each element: if it matches a type keyword and a next element exists, consume both as (type, name). Otherwise consume as untyped (dyn) name. The `&` for variadic is handled as before — rest params are always `dyn`.
- **Interaction with `if`:** `[if $cond { typed-expr } { typed-expr }]` — both branches must yield the same type. If they differ → compile error. If one is `dyn` and the other is typed → result is `dyn` (conservative).
- **Interaction with error handling:** `[try { typed-expr }]` — result type is `dyn` (may be an error value). Typed code inside `try` works normally; the `try` wrapper produces a dynamic result.
- **Interaction with collections:** `[vec $x $y]` where `x`, `y` are typed → auto-box to `dyn`. Collections store `JaclVal` (tagged), so typed values are boxed on insertion and unboxed on extraction (if the extraction context expects a type).

## Success Metrics

- All 6 numeric types (i32, i64, u32, u64, f32, f64) usable as type annotations on `def`, `mut`, and `proc`
- Typed i64/f64 arithmetic compiles to specialized opcodes with no runtime type dispatch
- Type mismatches caught at compile time with clear error messages
- Explicit `[to TYPE expr]` handles all numeric conversions
- All existing M1–M10 tests pass without regression — zero breakage of untyped code
- Typed procs enforce parameter and return types at call sites

## Open Questions

- Should `if`/`try` with mismatched branch types be an error or silently degrade to `dyn`? This PRD specifies: mismatched → error, one typed + one dyn → `dyn`.
- Should there be a `type?` or `typeof` builtin that returns the runtime type of a dynamic value? Could be useful for bridging typed and dynamic code.
- Should typed proc signatures be stored in the value representation (so higher-order functions can be type-checked)? For now, type checking only works for direct calls where the compiler knows the callee.
- How should type annotations interact with recursive procs? The compiler needs the return type before compiling the body — declared return types handle this naturally, but inferred return types of recursive procs may need special handling.
- Should integer overflow in typed arithmetic be an error (error flag) or wrap (two's complement)? Current i32 arithmetic wraps. This PRD does not specify — follow existing i32 behavior (wrap) for consistency.
