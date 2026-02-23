# PRD: M8a — Collections Design Fixes

## Introduction

Fix three design issues discovered in the M8 persistent collections implementation. These are correctness and usability fixes to existing code on the `ralph/persistent-collections` branch: (1) the `map` builtin's dual-purpose naming makes 1-pair map construction impossible, (2) `vec-set` silently returns nil on out-of-bounds instead of growing the vector, and (3) collections used as map keys silently break because the hash function uses pointer identity while `eq` uses structural equality.

## Goals

- Split the `map` transform HOF into a dedicated `transform` builtin so `map` is purely a constructor at any arity
- Make `vec-set` grow the vector (filling gaps with nil) when the index is beyond the current length, and error on negative indices
- Add eager node-level structural hashing to the RRB vec and HAMT library data structures so collections can be used as map keys correctly
- Update all affected tests to cover the new behavior

## User Stories

### US-012: Rename map transform to `transform`
**Description:** As a JACL user, I want `map` to always mean "construct a map" so that `[map "name" "alice"]` creates a 1-pair map instead of being misinterpreted as a transform call.

**Acceptance Criteria:**
- [ ] `[map "a" 1]` creates a 1-pair map (previously this was the transform path)
- [ ] `[map]` still creates an empty map
- [ ] `[map "a" 1 "b" 2]` still creates a 2-pair map
- [ ] `[map "a"]` (odd args) still produces a compile-time arity error
- [ ] `[transform $v [proc {x} [+ $x 1]]]` returns a new vector with each element incremented (2 args, replaces old `[map $v $proc]` form)
- [ ] `[transform $m [proc {k v} [vec $k [+ $v 1]]]]` works on maps — proc receives key and value, returns 2-element vector `[key new-value]`
- [ ] `[transform [vec] [proc {x} $x]]` on empty vector returns empty vector
- [ ] Compiler emits `OP_MAP` for all `map` calls (arg count / 2 = pair count), `OP_MAP_TRANSFORM` for all `transform` calls
- [ ] Compile-time arity: `transform` requires exactly 2 args
- [ ] Existing tests updated: all uses of 2-arg `map` changed to `transform`
- [ ] Compiles cleanly with -Wall -Wextra -std=c99 -g
- [ ] All tests pass (build.sh)

### US-013: `vec-set` grows vector on out-of-bounds, errors on negative index
**Description:** As a JACL user, I want `vec-set` to extend the vector when I set an index beyond its current length (filling gaps with nil), and to give me a clear error if I use a negative index.

**Acceptance Criteria:**
- [ ] `[vec-set [vec 1 2 3] 1 "x"]` still returns `[vec 1 "x" 3]` (in-bounds unchanged)
- [ ] `[vec-set [vec 1 2 3] 5 "x"]` returns `[vec 1 2 3 $nil $nil "x"]` — vector grows to length 6, gaps filled with nil
- [ ] `[vec-set [vec] 0 "x"]` returns `[vec "x"]` — works on empty vector
- [ ] `[vec-set [vec] 3 "x"]` returns `[vec $nil $nil $nil "x"]`
- [ ] `[vec-set [vec 1 2 3] -1 "x"]` produces a runtime error (negative index)
- [ ] Original vector unchanged after all operations (persistent semantics)
- [ ] Compiles cleanly with -Wall -Wextra -std=c99 -g
- [ ] All tests pass (build.sh)

### US-014: Eager node-level structural hashing for RRB vector
**Description:** As a library developer, I want every RRB vector node to maintain an eagerly-updated structural hash so that the root hash is always available in O(1) and vectors work correctly as map keys.

**Acceptance Criteria:**
- [ ] `RV_LEAF` gains a `uint32_t hash` field — the rotate-XOR of its element hashes: `hash ^= rotate_left(element_hash_fn(elem[i]), i % 32)` for each slot `i`
- [ ] `RV_INTERNAL` gains a `uint32_t hash` field — the XOR of its children's hash fields
- [ ] `RV_ROOT` gains a `uint32_t hash` field — XOR of tree root node hash and tail node hash (or just tail hash if tree is NULL)
- [ ] `element_hash_fn` is a function pointer `uint32_t (*)(RRB_VEC_T)` stored via a `RV_SET_HASH_HANDLER(fn)` call, so the library stays generic
- [ ] Hashes are computed eagerly: every `RV_COPY_LEAF`, `RV_COPY_INTERNAL`, and `RV_MK_ROOT` recomputes the hash from its children/elements at construction time
- [ ] Path-copying on `push_back`/`set` naturally propagates hash updates — only the O(log n) copied nodes recompute
- [ ] `RV_HASH(root)` is a simple field read returning `root->hash` in O(1)
- [ ] `rotate_left` is a bitwise left rotation: `(x << n) | (x >> (32 - n))`
- [ ] New tests in `lib/rrb_vec/test_rrb_vec.c` (or a new `test_rrb_vec_hash.c`): verify hash is consistent across equal vectors built different ways, verify hash changes when elements change, verify hash differs for permuted elements (rotate-XOR order-dependence), verify empty vector hash is deterministic
- [ ] If a new test file is created, add it to the TESTS array in `build.sh`
- [ ] `build.sh` gains a `--lib` flag that runs only the `lib/` tests (all entries with src paths starting with `lib/`). Running `build.sh` with no args still runs everything
- [ ] All existing RRB vec tests still pass
- [ ] Template instantiation in `src/collections.c` still compiles cleanly
- [ ] Compiles cleanly with -Wall -Wextra -std=c99 -g

### US-015: Eager node-level structural hashing for HAMT with root wrapper
**Description:** As a library developer, I want every HAMT node to maintain an eagerly-updated structural hash and a root wrapper struct so that the map hash is always available in O(1) and maps work correctly as map keys.

**Acceptance Criteria:**
- [ ] `H_LEAF` gains a `uint32_t node_hash` field (distinct from existing key hash) — computed as `key_hash_fn(key) ^ val_hash_fn(value)`
- [ ] `H_INTERNAL` gains a `uint32_t hash` field — the XOR of its children's hash/node_hash fields
- [ ] `H_COLLISION` gains a `uint32_t node_hash` field — the XOR of its items' node_hash fields
- [ ] XOR combining is order-independent, which is correct for maps since two equal maps may have different internal trie structure
- [ ] `key_hash_fn` and `val_hash_fn` are function pointers stored via `H_SET_HASH_HANDLERS(key_fn, val_fn)` call, so the library stays generic
- [ ] Hashes are computed eagerly: every node allocation/copy recomputes from children at construction time
- [ ] Path-copying on `set`/`unset` naturally propagates hash updates — only the O(log n) copied nodes recompute
- [ ] New `H_ROOT` wrapper struct with fields: `H_NODE* root`, `size_t count`, `uint32_t hash`
- [ ] `H_ROOT` is reference-counted via `rc.h` (same as other HAMT nodes)
- [ ] `H_ROOT` hash field is the root node's hash (or 0 for empty map)
- [ ] New functions `H_ROOT_SET`, `H_ROOT_UNSET`, `H_ROOT_GET`, `H_ROOT_HAS` wrap existing functions — take/return `H_ROOT*` and set hash from new root node
- [ ] `H_ROOT_EMPTY()` creates an empty root with `hash = 0`
- [ ] `H_ROOT_HASH(root)` is a simple field read returning `root->hash` in O(1)
- [ ] `H_ROOT_COUNT(root)` returns the count
- [ ] Existing HAMT iterator (`H_ITER`) works unchanged on `root->root`
- [ ] New tests in `lib/hamt/test_hamt.c` (or a new `test_hamt_hash.c`): verify hash is consistent for maps with same entries inserted in different order, verify hash changes when entries change, verify `H_ROOT_SET`/`H_ROOT_UNSET`/`H_ROOT_GET`/`H_ROOT_HAS` work correctly, verify empty map hash is 0
- [ ] If a new test file is created, add it to the TESTS array in `build.sh`
- [ ] All existing HAMT tests still pass
- [ ] Template instantiation in `src/collections.c` still compiles cleanly
- [ ] Compiles cleanly with -Wall -Wextra -std=c99 -g

### US-016: Wire eager hashes into `jacl_val_hash`
**Description:** As a JACL developer, I want the `jacl_val_hash` function to use the eager structural hashes from vectors and maps so that collections work correctly as map keys.

**Acceptance Criteria:**
- [ ] `jacl_val_hash` for `JACL_TAG_VECTOR` calls `jacl_vec_hash(root)` — O(1) field read from RRB root
- [ ] `jacl_val_hash` for `JACL_TAG_MAP` calls `jacl_map_root_hash(root)` — O(1) field read from HAMT root
- [ ] `collections__init()` calls `RV_SET_HASH_HANDLER(jacl_val_hash)` and `H_SET_HASH_HANDLERS(jacl_val_hash, jacl_val_hash)` to wire in the element/key/value hash functions
- [ ] JACL VM code updated to use `H_ROOT*` (via `jacl_map_root`) instead of raw `H_NODE*` for all map operations
- [ ] `[map-get [map [vec 1] "a"] [vec 1]]` returns `"a"` — two structurally equal vectors as keys find the same entry
- [ ] `[map-get [map [map "x" 1] "nested"] [map "x" 1]]` returns `"nested"` — maps as keys work
- [ ] `[eq]` still uses structural equality (no change needed, just verify it still works)
- [ ] Compiles cleanly with -Wall -Wextra -std=c99 -g
- [ ] All tests pass (build.sh)

### US-017: Update test suite for all fixes
**Description:** As a developer, I want the test suite updated to cover all the new behaviors introduced by these fixes.

**Acceptance Criteria:**
- [ ] Tests for `[map "a" 1]` as 1-pair constructor
- [ ] Tests for `transform` on vectors (replaces old 2-arg `map` tests)
- [ ] Tests for `transform` on maps
- [ ] Tests for `vec-set` growing vector with nil fill
- [ ] Tests for `vec-set` negative index producing runtime error
- [ ] Tests for vectors as map keys (structural hash correctness)
- [ ] Tests for maps as map keys (structural hash correctness)
- [ ] Tests for nested collections as map keys
- [ ] All existing tests still pass (no regressions)
- [ ] Compiles cleanly with -Wall -Wextra -std=c99 -g
- [ ] All tests pass (build.sh)

## Functional Requirements

- FR-1: Remove the 2-arg special case from the `map` compiler path — `map` always emits `OP_MAP` with pair count = argc / 2
- FR-2: Add `transform` as a new builtin recognized by the compiler — always emits `OP_MAP_TRANSFORM` (renamed to `OP_TRANSFORM` for clarity), requires exactly 2 args
- FR-3: Rename `OP_MAP_TRANSFORM` to `OP_TRANSFORM` in bytecode.c for clarity
- FR-4: `vec-set` VM implementation: when index >= length, push nils until length == index, then push the value. When index < 0, emit a runtime error
- FR-5: Add `uint32_t hash` to `RV_LEAF`, `RV_INTERNAL`, and `RV_ROOT` in `lib/rrb_vec/rrb_vec.h`
- FR-6: Leaf hash uses rotate-XOR: `hash ^= rotate_left(element_hash(elem[i]), i % 32)` — order-dependent
- FR-7: Internal/root hash uses plain XOR of children hashes — order is already encoded in the tree structure
- FR-8: All node construction/copy functions eagerly compute hash from children. `RV_HASH(root)` is an O(1) field read
- FR-9: Add `uint32_t node_hash` to `H_LEAF` and `H_COLLISION`, `uint32_t hash` to `H_INTERNAL` in `lib/hamt/hamt.h`
- FR-10: HAMT node hashes use XOR combining (order-independent) since iteration order is not canonical
- FR-11: All HAMT node construction/copy functions eagerly compute hash from children
- FR-12: Add `H_ROOT` wrapper struct to `lib/hamt/hamt.h` with `H_NODE* root`, `size_t count`, `uint32_t hash`
- FR-13: Add `H_ROOT_SET`, `H_ROOT_UNSET`, `H_ROOT_GET`, `H_ROOT_HAS`, `H_ROOT_EMPTY`, `H_ROOT_COUNT`, `H_ROOT_HASH` functions
- FR-14: Update `jacl_val_hash` to call `RV_HASH` for vectors and `H_ROOT_HASH` for maps — both O(1) field reads
- FR-15: Update all VM map operations to use `H_ROOT*` wrapper instead of raw `H_NODE*`
- FR-16: Update `src/collections.c` template instantiation for any new API surface
- FR-17: `collections__init()` calls `RV_SET_HASH_HANDLER(jacl_val_hash)` and `H_SET_HASH_HANDLERS(jacl_val_hash, jacl_val_hash)`

## Non-Goals

- No changes to `vec-get` behavior (nil on OOB is fine for reads)
- No changes to `vec-push`, `vec-concat`, `vec-slice` behavior
- No changes to `map-get`, `map-remove` behavior
- No new builtins beyond `transform`
- No changes to `each` or `filter`
- No changes to the transient (batch builder) paths for hashing — transients compute the hash when persisted

## Technical Considerations

- **Eager hashing fits path-copying perfectly.** Both RRB vec and HAMT already allocate new nodes along the mutation path. Computing the hash at construction time adds O(fan-out) work per node on that path — negligible compared to the allocation and copy costs already paid. No extra traversal is needed.
- **RRB vec uses rotate-XOR for leaves** to preserve element ordering: `hash ^= rotate_left(elem_hash, slot % 32)`. Internal and root nodes use plain XOR of children hashes since the tree structure already encodes order. The root hash combines tree root hash and tail hash.
- **HAMT uses plain XOR at all levels** since maps are unordered. `H_LEAF.node_hash = hash(key) ^ hash(val)`. Internal nodes XOR their children. This is order-independent, which is correct since two equal maps may have different trie structure.
- **HAMT has no root struct today** — the API takes/returns `H_NODE*` directly. The new `H_ROOT` wrapper must be reference-counted and integrated into the existing `rc.h` lifecycle. All JACL VM code that currently stores `H_NODE*` for maps must switch to `H_ROOT*`.
- **Hash handler registration:** Both libraries need a `SET_HASH_HANDLER(s)` mechanism (similar to HAMT's existing `SET_KEY_HANDLERS`) so the hash function for elements/keys/values is configurable. Called once during `collections__init()`.
- **`vec-set` growth**: use `jacl_vec_push_back` in a loop to append nils, then set the final index. This produces O(gap_size) intermediate vectors. For a more efficient approach, use a transient builder to batch the appends. Given that extreme growth is unlikely in practice, the simple loop is acceptable for now.
- **Opcode rename**: `OP_MAP_TRANSFORM` → `OP_TRANSFORM` requires updating bytecode.c, compiler.c, vm.c, and the disassembler (if any). Purely mechanical.

## Success Metrics

- `[map "a" 1]` creates a 1-pair map (the original footgun is gone)
- `[vec-set [vec] 5 "x"]` produces a 6-element vector (no silent nil return)
- `[map-get [map [vec 1 2] "found"] [vec 1 2]]` returns `"found"` (collections as keys work)
- All existing M8 tests pass after updating `map` 2-arg calls to `transform`
- No regressions in any other test suite

## Open Questions

- Should `vec-set` growth have a maximum gap size to prevent accidental `[vec-set [vec] 1000000 "x"]` creating a million-element vector? A reasonable cap (e.g., 1024 or 65536) could prevent accidental memory blowup while still being useful.
- For the HAMT `H_ROOT` wrapper, should existing non-JACL users of the HAMT library also be migrated to `H_ROOT`, or should it be a JACL-specific addition layered on top?
