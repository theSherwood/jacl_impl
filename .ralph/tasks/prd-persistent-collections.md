# PRD: M8 — Persistent Collections

## Introduction

Add persistent vectors and maps to JACL, backed by the existing RRB vector and HAMT libraries. Vectors are ordered, indexed collections; maps are key-value associative collections. Both are immutable — all "mutation" operations return new values with structural sharing. This milestone makes JACL a practical language by providing core data structures beyond scalars and strings.

## Goals

- Provide persistent vector and map types usable from JACL code
- Wire existing `lib/rrb_vec` and `lib/hamt` C libraries into the JACL value/compiler/VM pipeline
- Support construction, access, update, and querying for both types
- Provide iteration via `each`, `map`, and `filter` builtins that work on both vectors and maps
- Structural (deep) equality for collections via the existing `eq` builtin
- Readable `print` output using constructor syntax (e.g., `[vec 1 2 3]`)

## User Stories

### US-001: Instantiate RRB vector and HAMT templates with JaclVal types
**Description:** As a developer, I need the RRB vector and HAMT template libraries instantiated with JACL value types so the VM can create and manipulate persistent collections at runtime.

**Acceptance Criteria:**
- [ ] RRB vector template instantiated with `RRB_VEC_T = JaclVal` and `RRB_VEC_NAME = jacl_vec`
- [ ] HAMT template instantiated with `HAMT_KEY_T = JaclVal`, `HAMT_VAL_T = JaclVal`, `HAMT_NAME = jacl_map`
- [ ] Generic `JaclVal` hash function implemented (dispatches on type tag: i32, f32, bool, nil, inline string, heap string)
- [ ] Generic `JaclVal` equality function implemented for HAMT key comparison
- [ ] Template instantiation compiles cleanly within the unity build (`jacl.c`)
- [ ] Typecheck/compile passes with no warnings

### US-002: Vector constructor and basic accessors
**Description:** As a JACL user, I want to create vectors and access their elements so I can work with ordered collections.

**Acceptance Criteria:**
- [ ] `[vec]` creates an empty vector
- [ ] `[vec 1 2 3]` creates a vector with elements 1, 2, 3 (variadic, 0+ args)
- [ ] `[vec-get $v 0]` returns the element at index 0
- [ ] `[vec-get $v 99]` on a 3-element vector returns nil (out-of-bounds returns nil, not error)
- [ ] `[vec-len $v]` returns the number of elements as an i32
- [ ] Compiler emits appropriate opcodes for each builtin
- [ ] Compile-time arity checking enforced: `vec-get` requires exactly 2 args, `vec-len` requires exactly 1 arg
- [ ] Tests verify all of the above

### US-003: Vector update operations
**Description:** As a JACL user, I want to add and update elements in a vector, receiving a new vector back (persistent/immutable semantics).

**Acceptance Criteria:**
- [ ] `[vec-push $v 4]` returns a new vector with 4 appended (2 args)
- [ ] `[vec-set $v 1 "x"]` returns a new vector with index 1 replaced by "x" (3 args)
- [ ] `[vec-set $v 99 "x"]` on a 3-element vector returns nil (out-of-bounds returns nil)
- [ ] Original vector is unchanged after push/set (persistent semantics verified)
- [ ] `[vec-concat $v1 $v2]` returns a new vector that is the concatenation of two vectors (2 args)
- [ ] `[vec-slice $v 1 3]` returns a new vector from index 1 (inclusive) to 3 (exclusive) (3 args)
- [ ] Compile-time arity checking enforced for all operations
- [ ] Tests verify all of the above, including persistence

### US-004: Map constructor and basic accessors
**Description:** As a JACL user, I want to create maps and look up values by key so I can work with associative data.

**Acceptance Criteria:**
- [ ] `[map]` creates an empty map
- [ ] `[map "a" 1 "b" 2]` creates a map with two key-value pairs (variadic, must be even number of args)
- [ ] `[map "a" 1 "b"]` (odd args) produces a compile-time arity error
- [ ] `[map-get $m "a"]` returns the value associated with key "a"
- [ ] `[map-get $m "missing"]` returns nil for absent keys
- [ ] `[map-has $m "a"]` returns `$true` if key exists, `$false` otherwise (2 args)
- [ ] `[map-len $m]` returns the number of key-value pairs as an i32 (1 arg)
- [ ] Non-string keys work: `[map 1 "one" $true "yes"]` (integer and boolean keys)
- [ ] Compile-time arity checking enforced for `map-get` (2 args), `map-has` (2 args), `map-len` (1 arg)
- [ ] Tests verify all of the above

### US-005: Map update operations
**Description:** As a JACL user, I want to add, update, and remove map entries, receiving a new map back.

**Acceptance Criteria:**
- [ ] `[map-set $m "c" 3]` returns a new map with key "c" set to 3 (3 args)
- [ ] `[map-set $m "a" 99]` overwrites existing key "a" with 99
- [ ] `[map-remove $m "a"]` returns a new map without key "a" (2 args)
- [ ] `[map-remove $m "missing"]` on an absent key returns the map unchanged
- [ ] Original map is unchanged after set/remove (persistent semantics verified)
- [ ] `[map-keys $m]` returns a vector of all keys (1 arg)
- [ ] `[map-vals $m]` returns a vector of all values (1 arg)
- [ ] Compile-time arity checking enforced for all operations
- [ ] Tests verify all of the above, including persistence

### US-006: Structural equality for collections
**Description:** As a JACL user, I want `eq` to compare vectors and maps by value so I can test collection equality.

**Acceptance Criteria:**
- [ ] `[eq [vec 1 2 3] [vec 1 2 3]]` returns `$true`
- [ ] `[eq [vec 1 2] [vec 1 2 3]]` returns `$false`
- [ ] `[eq [map "a" 1] [map "a" 1]]` returns `$true`
- [ ] `[eq [map "a" 1 "b" 2] [map "b" 2 "a" 1]]` returns `$true` (insertion order irrelevant)
- [ ] `[eq [vec [vec 1] [vec 2]] [vec [vec 1] [vec 2]]]` returns `$true` (nested/recursive)
- [ ] `[eq [vec 1] [map "a" 1]]` returns `$false` (different types)
- [ ] `[eq [vec 1] 1]` returns `$false` (collection vs scalar)
- [ ] Tests verify all of the above

### US-007: Print representation for collections
**Description:** As a JACL user, I want `print` to display vectors and maps in readable constructor syntax so I can debug my programs.

**Acceptance Criteria:**
- [ ] `[print [vec 1 2 3]]` outputs `[vec 1 2 3]`
- [ ] `[print [vec]]` outputs `[vec]`
- [ ] `[print [map "a" 1 "b" 2]]` outputs `[map "a" 1 "b" 2]` (string keys quoted)
- [ ] `[print [map]]` outputs `[map]`
- [ ] `[print [vec [vec 1] [map "x" 2]]]` outputs nested constructor syntax
- [ ] `[to-string [vec 1 2]]` returns the string `[vec 1 2]`
- [ ] Tests verify all of the above

### US-008: `each` builtin for iteration
**Description:** As a JACL user, I want to iterate over collections with `each` so I can perform side effects on each element.

**Acceptance Criteria:**
- [ ] `[each $v [proc {x} [print $x]]]` calls the proc for each element of vector `$v` (2 args)
- [ ] `[each $m [proc {k v} [print $k $v]]]` calls the proc for each key-value pair of map `$m` (2 args)
- [ ] `each` returns nil
- [ ] Proc receives 1 arg for vectors, 2 args for maps (runtime arity check)
- [ ] Iterating an empty collection calls the proc zero times
- [ ] Compile-time arity checking: `each` requires exactly 2 args
- [ ] Tests verify all of the above

### US-009: `map` (transform) builtin
**Description:** As a JACL user, I want to transform each element of a collection with `map` (the higher-order function), receiving a new collection back.

**Acceptance Criteria:**
- [ ] `[map $v [proc {x} [+ $x 1]]]` returns a new vector with each element incremented (2 args)
- [ ] Note: `map` with 2 args is the transform function; `map` with even args is the constructor. The compiler distinguishes by argument count: 2 args = transform, even args >= 0 (except 2) = constructor. Specifically: 0 args = empty map constructor, 2 args = transform, 4/6/8/... args = map constructor with key-value pairs. Odd args (1, 3, 5, ...) = arity error.
- [ ] Transforming an empty vector returns an empty vector
- [ ] When applied to a map: `[map $m [proc {k v} [vec $k [+ $v 1]]]]` — proc receives key and value, returns a 2-element vector `[key, new-value]`, result is a new map
- [ ] Tests verify all of the above

### US-010: `filter` builtin
**Description:** As a JACL user, I want to select elements from a collection based on a predicate, receiving a new collection back.

**Acceptance Criteria:**
- [ ] `[filter $v [proc {x} [> $x 2]]]` returns a new vector containing only elements greater than 2 (2 args)
- [ ] Filtering an empty vector returns an empty vector
- [ ] When applied to a map: `[filter $m [proc {k v} [> $v 0]]]` — proc receives key and value, returns truthy/falsy, result is a new map with only matching entries
- [ ] Compile-time arity checking: `filter` requires exactly 2 args
- [ ] Tests verify all of the above

### US-011: Integration test suite
**Description:** As a developer, I want a comprehensive test file that exercises all collection operations end-to-end through the full JACL pipeline.

**Acceptance Criteria:**
- [ ] Test file `test/test_collections.c` exists
- [ ] Tests cover: vec construction, vec-get, vec-set, vec-push, vec-len, vec-concat, vec-slice
- [ ] Tests cover: map construction, map-get, map-set, map-remove, map-has, map-len, map-keys, map-vals
- [ ] Tests cover: structural equality for vectors, maps, nested collections
- [ ] Tests cover: print/to-string output for both types
- [ ] Tests cover: each, map (transform), filter on both vectors and maps
- [ ] Tests cover: error cases (wrong arity, type errors like `[vec-get 42 0]`)
- [ ] Tests cover: persistence (original unchanged after operations)
- [ ] All tests pass with no memory leaks (arena + tracker pattern)
- [ ] Tests compile and run cleanly

## Functional Requirements

- FR-1: Instantiate `lib/rrb_vec/rrb_vec.h` with `RRB_VEC_T = JaclVal` to produce `jacl_vec_*` functions
- FR-2: Instantiate `lib/hamt/hamt.h` with `HAMT_KEY_T = JaclVal`, `HAMT_VAL_T = JaclVal` to produce `jacl_map_*` functions
- FR-3: Implement a generic `JaclVal` hash function that dispatches on type tag (i32, f32, bool, nil, inline string, heap string, vector, map)
- FR-4: Implement a generic `JaclVal` equality function for HAMT key comparison
- FR-5: Add new opcodes to `src/bytecode.c` for all collection operations
- FR-6: Add compiler support in `src/compiler.c` for recognizing `vec`, `vec-get`, `vec-set`, `vec-push`, `vec-len`, `vec-concat`, `vec-slice`, `map` (constructor), `map-get`, `map-set`, `map-remove`, `map-has`, `map-len`, `map-keys`, `map-vals`, `each`, `filter` builtins
- FR-7: `map` with 2 args is the transform function; 0 or even >= 4 args is the map constructor; odd args (1, 3, 5, ...) produce a compile-time arity error
- FR-8: Add VM execution in `src/vm.c` for all new opcodes
- FR-9: Extend `OP_EQ` in the VM to perform recursive structural equality for vectors and maps
- FR-10: Extend `OP_PRINT` and `OP_TO_STRING` to render vectors as `[vec 1 2 3]` and maps as `[map "a" 1 "b" 2]`
- FR-11: Out-of-bounds `vec-get` and `vec-set` return nil (no error/crash)
- FR-12: Missing key `map-get` returns nil
- FR-13: `map-remove` on absent key returns the map unchanged
- FR-14: Type errors at runtime (e.g., `[vec-get 42 0]`) produce a clear error message
- FR-15: All collection operations that return collections return new values (persistent semantics — original values are never mutated)

## Non-Goals

- No literal syntax for vectors or maps (no `[1, 2, 3]` or `{"a": 1}` — use constructor builtins)
- No `reduce`, `any?`, `every?`, `find`, or other higher-order functions beyond `each`, `map`, `filter`
- No sorted maps or sets
- No lazy sequences or generators
- No destructuring bind or pattern matching on collections
- No nested access syntax (e.g., `get-in`) — users chain `map-get`/`vec-get` calls
- No automatic memory management for collections (GC is M12) — collections use reference counting from the underlying libraries
- No mutable vectors or maps (`mut`/`set!` is M10)

## Technical Considerations

- The value system already has `JACL_TAG_VECTOR` (0x06) and `JACL_TAG_MAP` (0x07) with constructor/accessor functions in `src/value.c`. No changes to the value representation are needed.
- RRB vector and HAMT libraries use reference counting internally (`lib/rc/rc.h`). The VM must call `RV_REF`/`RV_UNREF` and `H_REF`/`H_UNREF` appropriately to avoid leaks. Since GC is not yet implemented (M12), collections created during execution will currently leak if not explicitly managed — this is acceptable for M8.
- The HAMT library requires `H_SET_KEY_HANDLERS(hash_fn, eq_fn)` to be called once before use. This should happen during VM initialization.
- `map` serves double duty as both the constructor (0 or even >= 4 args) and the transform function (2 args). The compiler must distinguish based on argument count.
- Iteration builtins (`each`, `map` transform, `filter`) receive closures. They invoke `OP_CALL` on the closure for each element. This means they need to be implemented as multi-step VM operations, not simple single-opcode builtins. Consider implementing them as complex opcodes that contain an internal loop with call/return handling.
- String keys in maps: the hash function for strings should use the precomputed `hash` field from `JaclHeapString` for heap strings, and compute FNV-1a inline for inline strings.

## Success Metrics

- All 11 user stories pass their acceptance criteria
- `test/test_collections.c` passes with zero memory leaks
- Collections integrate naturally with existing JACL features (closures, variables, print, eq)
- No regressions in existing test suites

## Open Questions

- Should `map-keys` and `map-vals` guarantee a consistent ordering across calls? (Likely no — HAMT iteration order is deterministic but not sorted.)
- For `map` transform on maps, should the proc return a 2-element vector `[key value]`? Or should it return just the new value, keeping the key unchanged? (PRD assumes 2-element vector for maximum flexibility.)
- Memory management: since GC (M12) is not yet implemented, should the VM track allocated collections for cleanup at VM destruction, or accept that they leak for now?
