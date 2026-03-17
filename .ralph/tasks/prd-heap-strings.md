# PRD: Three-Tier String Architecture (Unicode-Correct Heap Strings)

## Introduction

JACL's M5 string system provides inline strings (0-7 bytes) and flat interned heap strings (8+
bytes) with byte-based operations. This milestone upgrades the string system to a three-tier
architecture with full Unicode correctness: inline strings (0-7 bytes), interned flat heap strings
(8-128 bytes after NFD normalization), and rope-backed strings (129+ bytes) using the existing
sum_tree library. All strings are NFD-normalized, immutable, UTF-8, and expose grapheme-based
length/indexing/slicing to the user. The implementation is transparent — all string operations work
identically across all three tiers.

**Key design decisions:**

- **NFD normalization on creation:** Every string entering the system is canonicalized to NFD form.
  This guarantees that two strings which are canonically equivalent have identical byte sequences,
  enabling reliable byte-level hashing and comparison. The 128-byte interning threshold is applied
  after normalization.
- **Grapheme-based semantics:** User-facing `length`, `index`, and `slice` operate on UAX #29
  extended grapheme clusters. `"cafe\u0301"` and its NFC equivalent both have length 4 (c, a, f, e\u0301).
  Internal byte-length accessors remain available for the runtime.
- **Rope integration for large strings:** Strings exceeding 128 bytes use the sum_tree rope with
  monoidal summaries (bytes, graphemes, lines), enabling O(log n) seeking and efficient
  concat/slice without copying. Rope strings cache an FNV-1a hash for fast equality checks.
- **Grapheme-safe rope splitting:** Rope leaf boundaries never fall inside a grapheme cluster.
  The existing `utf8_find_safe_split` (codepoint-aware) is upgraded to a grapheme-aware variant.
- **Immutability:** All strings at all tiers are immutable. Concat and slice produce new strings.
- **Invalid UTF-8 rejected:** String creation rejects invalid UTF-8 sequences (returns error/nil).

## Goals

- Upgrade string system from two-tier (inline + flat heap) to three-tier (inline + interned flat + rope)
- NFD-normalize all strings on creation for canonical equivalence
- Make all user-facing string operations grapheme-based (length, index, slice)
- Integrate the sum_tree rope library for strings >128 bytes
- Ensure O(log n) grapheme seeking on rope strings via monoidal summaries
- Maintain full operational parity across all three tiers — no operation is tier-restricted
- Cache FNV-1a hash on rope strings for O(1) equality fast-path
- Upgrade rope leaf splitting from codepoint-aware to grapheme-aware
- Reject invalid UTF-8 at string creation boundaries
- Keep interned strings immortal (weak intern table deferred to a future milestone)

## User Stories

### US-001: NFD normalization on string creation

**Description:** As the runtime, I need every string entering the system to be NFD-normalized so
that canonically equivalent strings have identical byte representations.

**Acceptance Criteria:**
- [ ] All string creation paths (literal compilation, intern, concat result, slice result, `to_string` coercion) produce NFD-normalized output
- [ ] `unicode_is_nfd()` quick-check is used to skip normalization when input is already NFD
- [ ] NFC input "e\u0301" (U+00E9, 2 bytes) is decomposed to "e" + U+0301 (3 bytes NFD)
- [ ] Pure ASCII input passes through without allocation overhead (already NFD)
- [ ] The 128-byte interning threshold is applied to the post-normalization byte length
- [ ] A string that is 120 bytes in NFC but 140 bytes in NFD correctly becomes a rope, not a flat interned string
- [ ] Hangul syllable decomposition works correctly (algorithmic decomposition in `unicode__decompose_cp`)
- [ ] Unit tests: NFC input round-trips to NFD; ASCII is unchanged; Hangul decomposes; combining sequences are canonically ordered
- [ ] Compiles cleanly with `-Wall -Wextra`

---

### US-002: Invalid UTF-8 rejection

**Description:** As the runtime, I need to reject invalid UTF-8 input at string creation boundaries
so that all strings in the system are guaranteed valid UTF-8.

**Acceptance Criteria:**
- [ ] `jacl_string_create()` (or equivalent entry point) validates UTF-8 before normalization
- [ ] Invalid UTF-8 sequences (overlong encodings, surrogate halves, truncated sequences, invalid continuation bytes) cause string creation to return an error value (nil or error-tagged value)
- [ ] Valid UTF-8 with unusual but legal sequences (e.g., U+0000 encoded as 1 byte) is accepted
- [ ] The compiler validates string literals at compile time and reports an error for invalid UTF-8
- [ ] Unit tests: each category of invalid UTF-8 is tested; valid edge cases pass

---

### US-003: Grapheme-aware `JaclHeapString` struct

**Description:** As the runtime, I need the flat heap string struct to cache grapheme count
alongside byte length so that grapheme-based `length` is O(1) for interned strings.

**Acceptance Criteria:**
- [ ] `JaclHeapString` struct updated to: `{ uint32_t byte_len; uint32_t grapheme_len; uint32_t hash; char data[]; }`
- [ ] `grapheme_len` is computed once at creation time using `unicode_grapheme_count()`
- [ ] All existing code that reads `length` is updated to use `byte_len`
- [ ] `jacl_string_len()` returns `grapheme_len` for flat heap strings
- [ ] `jacl_string_byte_len()` returns `byte_len` for internal use
- [ ] Inline strings (0-7 bytes): grapheme count is computed on-the-fly (bounded, fast)
- [ ] Unit tests: grapheme count matches expected values for ASCII, multi-byte, combining sequences, emoji with ZWJ

---

### US-004: `JACL_TAG_ROPE_STRING` and rope value type

**Description:** As the runtime, I need a new tagged value type for rope-backed strings so that
the VM can dispatch string operations efficiently across all three tiers.

**Acceptance Criteria:**
- [ ] `JACL_TAG_ROPE_STRING` defined in the tag space (e.g., 0x06 or next available slot)
- [ ] `JaclRopeString` wrapper struct: `{ uint32_t hash; rope r; }` — stores cached hash and the sum_tree rope
- [ ] `JaclRopeString` allocated on GC heap via `gc_alloc()` with a new `OBJ_ROPE_STRING` object kind
- [ ] `jacl_is_rope_string(v)` predicate
- [ ] `jacl_as_rope_string(v)` extractor
- [ ] `jacl_is_string(v)` updated to return true for all three tiers (inline, heap, rope)
- [ ] `jacl_rope_string_ptr(JaclRopeString*)` constructs a tagged value
- [ ] FNV-1a hash computed over all rope leaves at creation time, cached in `hash` field
- [ ] Unit tests: create a rope string from >128 byte input, verify tag, verify hash consistency

---

### US-005: Grapheme-safe rope invariant

**Description:** As the rope implementation, I need all leaf boundaries to always fall on grapheme
cluster boundaries — both at construction time (`rope_from_str`) and after structural operations
(`rope_concat`). This guarantees that `rope_summary_combine` (simple summation of grapheme counts)
is always correct, and O(log n) grapheme seeking via summaries is always accurate.

**Acceptance Criteria:**
- [ ] New function `grapheme_find_safe_split(const uint8_t* src, size_t len, size_t pos)` — walks backward from `pos` to find a grapheme cluster boundary
- [ ] `rope_from_str()` uses `grapheme_find_safe_split` instead of `utf8_find_safe_split` when constructing leaves
- [ ] `rope_concat()` enforces the grapheme boundary invariant at the junction: if the right tree's leftmost leaf starts with Extend/ZWJ/SpacingMark codepoints, those bytes are moved to the left tree's rightmost leaf, and affected leaf summaries are recomputed. This is O(log n) (2 leaves touched + ancestors in the persistent tree).
- [ ] A combining character sequence (e.g., base + multiple combining marks) is never split across leaves
- [ ] Emoji ZWJ sequences (e.g., family emoji) are never split across leaves
- [ ] Regional indicator pairs (flag emoji) are never split across leaves
- [ ] The existing `rope_summary_summarize()` produces correct grapheme counts per leaf (no cross-leaf grapheme accounting needed because the invariant guarantees clean boundaries)
- [ ] Unit tests: construct rope from string with combining sequences at leaf boundaries; verify grapheme count matches expected; verify no leaf starts with an extending/ZWJ codepoint
- [ ] Unit tests: `rope_concat` where right side starts with combining marks — verify bytes are moved to left leaf and grapheme count is correct
- [ ] Unit tests: repeated concat operations maintain the invariant (no accumulated drift)

---

### US-006: Unified string creation with tier selection

**Description:** As the runtime, I need a single entry point for string creation that validates,
normalizes, and routes to the correct tier automatically.

**Acceptance Criteria:**
- [ ] `jacl_string_new(ThreadHeap* heap, JaclInternTable* table, const char* data, uint32_t length)` — unified constructor
- [ ] Step 1: UTF-8 validation — reject invalid input
- [ ] Step 2: NFD normalization (skip if `unicode_is_nfd()` returns true)
- [ ] Step 3: Tier selection based on post-NFD byte length:
  - 0-7 bytes → inline string (`JACL_TAG_INLINE_STRING`)
  - 8-128 bytes → interned flat heap string (`JACL_TAG_STRING`)
  - 129+ bytes → rope string (`JACL_TAG_ROPE_STRING`)
- [ ] For inline/flat tiers, the existing `jacl_inline_string()` / `jacl_intern()` are used internally
- [ ] For rope tier, `rope_from_str()` is called and wrapped in `JaclRopeString`
- [ ] Compiler updated to use `jacl_string_new()` for all string literal compilation
- [ ] `OP_CONCAT` result uses `jacl_string_new()` (or optimized path for known-tier inputs)
- [ ] Unit tests: strings at each tier boundary (7, 8, 128, 129 bytes) route correctly

---

### US-007: Grapheme-based `length` across all tiers

**Description:** As a JACL programmer, I want `[length s]` to return the number of grapheme
clusters so that string length matches what I see on screen.

**Acceptance Criteria:**
- [ ] `[length "hello"]` → `5` (ASCII, graphemes = bytes)
- [ ] `[length "cafe\u0301"]` → `4` (NFD: c, a, f, e+combining acute — 4 graphemes regardless of input form)
- [ ] `[length ""]` → `0`
- [ ] `[length s]` where `s` is a rope string returns the correct grapheme count from the rope summary
- [ ] For inline strings: grapheme count computed on the fly via `unicode_grapheme_count()`
- [ ] For flat heap strings: returns cached `grapheme_len` field (O(1))
- [ ] For rope strings: returns `rope_summary(r).graphemes` (O(1) from monoidal summary)
- [ ] A new `byte-length` builtin is available for when byte length is needed
- [ ] Unit tests: ASCII, combining marks, emoji, ZWJ sequences, empty string, large rope string

---

### US-008: Grapheme-based `index` across all tiers

**Description:** As a JACL programmer, I want `[index s i]` to return the i-th grapheme cluster
so that indexing aligns with visible characters.

**Acceptance Criteria:**
- [ ] `[index "hello" 0]` → `"h"`
- [ ] `[index "cafe\u0301" 3]` → `"e\u0301"` (the grapheme cluster, which may be multi-codepoint/multi-byte)
- [ ] `[index s i]` where `i` is negative or >= grapheme length → `nil`
- [ ] For inline/flat strings: linear scan through grapheme clusters (O(n), bounded at 128 bytes)
- [ ] For rope strings: use rope cursor to seek to grapheme offset `i` via summary (O(log n)), then extract the cluster
- [ ] The returned value is itself a valid string (goes through `jacl_string_new`, may be inline or flat)
- [ ] Unit tests: ASCII index, combining mark index, emoji index, out-of-bounds, rope string index

---

### US-009: Grapheme-based `slice` across all tiers

**Description:** As a JACL programmer, I want `[slice s start end?]` to operate on grapheme
indices so that slicing aligns with visible characters.

**Acceptance Criteria:**
- [ ] `[slice "hello" 1 3]` → `"el"` (graphemes 1 and 2)
- [ ] `[slice "cafe\u0301" 2 4]` → `"fe\u0301"` (graphemes 2 and 3, where grapheme 3 is multi-byte)
- [ ] `[slice s start]` (one-arg form) → from `start` to end
- [ ] For inline/flat strings: compute byte offsets by scanning graphemes, then extract
- [ ] For rope strings: use `rope_slice()` with byte offsets derived from grapheme cursor seeking
- [ ] Result goes through `jacl_string_new()` for proper tier selection
- [ ] Empty slice returns `""` (empty inline string)
- [ ] Out-of-range indices are clamped (not errors)
- [ ] Unit tests: ASCII slice, combining mark boundaries, rope slice across leaves, edge cases

---

### US-010: String concatenation with tier-aware result

**Description:** As a JACL programmer, I want `[concat s1 s2 ...]` to produce the smallest tier
that fits the result.

**Acceptance Criteria:**
- [ ] Two inline strings whose concat fits in 7 bytes → inline result
- [ ] Two 80-byte flat strings → 160 bytes → rope result
- [ ] One rope + one inline → rope result (if total >128 bytes)
- [ ] Concat of two ropes uses `rope_concat()` for O(log n) structural sharing
- [ ] Concat of flat + flat where result ≤128 bytes → flat interned result
- [ ] `OP_CONCAT` updated to handle all 9 cross-tier combinations efficiently
- [ ] Hash of the result is computed appropriately for the result tier
- [ ] NFD invariant maintained: since both inputs are already NFD, concat of NFD strings is NFD (no re-normalization needed for simple concatenation)
- [ ] **Grapheme boundary fix-up at concat junction:** when the right operand's first codepoint has Extend/ZWJ/SpacingMark grapheme break property, the combining bytes are moved from the right side's leftmost leaf to the left side's rightmost leaf (for rope results), or the grapheme count is recomputed (for flat results). This check is O(1) — decode one codepoint and look up its grapheme break property; the fix-up touches at most 2 leaves.
- [ ] Concat where right operand does NOT start with a combining character: grapheme count is simply summed (no fix-up needed)
- [ ] Unit tests: all tier combinations, size boundary transitions, variadic concat, concat at grapheme boundary (base + combining mark junction)

---

### US-011: String equality with hash fast-path for ropes

**Description:** As the runtime, I need string equality to work across all three tiers with
efficient fast paths.

**Acceptance Criteria:**
- [ ] Inline vs inline: bitwise comparison (unchanged)
- [ ] Flat heap vs flat heap: pointer comparison via interning (unchanged)
- [ ] Rope vs rope: compare cached hashes first; if equal, compare content leaf-by-leaf
- [ ] Rope vs flat/inline: compare hash of rope against computed hash of flat/inline bytes; if equal, compare content
- [ ] Cross-tier comparisons that differ in hash return `false` immediately (O(1))
- [ ] `jacl_string_eq()` updated to handle all 6 cross-tier pair combinations
- [ ] Unit tests: same content across different tiers compares equal; different content compares unequal; hash collision case (same hash, different content) handled correctly

---

### US-012: String comparison (ordering) across all tiers

**Description:** As the runtime, I need lexicographic comparison (`<`, `>`, `<=`, `>=`) to work
across all three tiers.

**Acceptance Criteria:**
- [ ] Comparison is byte-level lexicographic on the NFD-normalized content (since all strings are NFD, this gives consistent Unicode ordering)
- [ ] Inline vs inline: extract bytes, memcmp (existing logic)
- [ ] Flat vs flat: direct memcmp on `data` pointers
- [ ] Rope vs rope: leaf-by-leaf comparison via cursors
- [ ] Cross-tier: materialize the smaller side if needed, or use cursor-based comparison
- [ ] `jacl_string_cmp()` updated for all tier combinations
- [ ] Unit tests: ordering across tiers, strings with combining marks, emoji ordering

---

### US-013: Compiler and VM integration

**Description:** As a JACL programmer, I want all existing string features (literals, interpolation,
builtins) to work seamlessly with the new three-tier system.

**Acceptance Criteria:**
- [ ] Compiler uses `jacl_string_new()` for all string literal compilation
- [ ] String interpolation produces correct results when segments span tiers
- [ ] `OP_CONCAT` handles all tier combinations
- [ ] `OP_TO_STRING` produces NFD-normalized output
- [ ] `print` works correctly for rope strings (materializes to buffer or streams leaf-by-leaf)
- [ ] All existing string tests from M5 continue to pass (possibly with updated expectations for grapheme-based length)
- [ ] GC updated to trace `OBJ_ROPE_STRING` objects and their internal rope nodes
- [ ] Unit tests: end-to-end programs using string interpolation with large strings, mixed tiers

---

### US-014: Comprehensive test suite

**Description:** As a developer, I need thorough tests covering all tier interactions, Unicode
edge cases, and regression scenarios.

**Acceptance Criteria:**
- [ ] Test: NFD normalization of NFC input, already-NFD input, ASCII input, Hangul input
- [ ] Test: Invalid UTF-8 rejection for each error category
- [ ] Test: Tier routing at boundaries: 7-byte, 8-byte, 128-byte, 129-byte strings
- [ ] Test: Grapheme length for ASCII, Latin + combining marks, Devanagari conjuncts, emoji, ZWJ sequences, regional indicators
- [ ] Test: Grapheme index and slice at combining mark boundaries
- [ ] Test: Concat producing each tier transition (inline→flat, flat→rope, etc.)
- [ ] Test: Concat at grapheme boundary (string A ends with base char, string B starts with combining mark)
- [ ] Test: Equality across all 6 tier-pair combinations
- [ ] Test: Comparison (ordering) across tiers
- [ ] Test: Rope leaf splitting never breaks a grapheme cluster
- [ ] Test: Rope hash consistency (same content → same hash regardless of tree structure)
- [ ] Test: Large rope operations (concat, slice, index) on strings >10KB
- [ ] Test: All existing M5 string tests pass (with grapheme-based length adjustments)
- [ ] Test: GC correctly traces and collects rope string objects
- [ ] Zero memory leaks in all tests
- [ ] All tests added to `build.sh` TESTS array

## Functional Requirements

- FR-1: Three string tiers: inline (0-7 bytes), interned flat (8-128 bytes post-NFD), rope (129+ bytes post-NFD)
- FR-2: All strings are NFD-normalized on creation; `unicode_is_nfd()` quick-check skips normalization for already-NFD input
- FR-3: All strings are valid UTF-8; invalid UTF-8 input is rejected at creation
- FR-4: `jacl_string_len()` returns grapheme cluster count (UAX #29) for all tiers
- FR-5: `jacl_string_byte_len()` returns byte length for internal runtime use
- FR-6: `byte-length` builtin exposed to JACL programs for when byte length is needed
- FR-7: `JaclHeapString` struct: `{ uint32_t byte_len; uint32_t grapheme_len; uint32_t hash; char data[]; }`
- FR-8: `JACL_TAG_ROPE_STRING` tag for rope-backed strings; `JaclRopeString` struct: `{ uint32_t hash; rope r; }`
- FR-9: `OBJ_ROPE_STRING` GC object kind; GC traces rope nodes
- FR-10: Rope summary stores bytes, graphemes, and lines (existing `rope_summary` fields)
- FR-11: Rope leaf boundaries are always grapheme cluster boundaries — enforced at construction (`rope_from_str`) and after concat (`rope_concat` junction fix-up)
- FR-12: `grapheme_find_safe_split()` walks backward from a position to the nearest grapheme cluster boundary
- FR-13: `jacl_string_new()` is the unified string creation entry point: validate → normalize → tier-select
- FR-14: Interned flat strings use pointer equality for O(1) comparison (unchanged)
- FR-15: Rope strings cache FNV-1a hash; equality checks compare hash first, content on collision
- FR-16: `[index s i]` returns the i-th grapheme cluster as a string value
- FR-17: `[slice s start end?]` operates on grapheme indices
- FR-18: `[concat s1 s2 ...]` produces the smallest tier that fits the result
- FR-19: Concat of two NFD strings is NFD without re-normalization; grapheme count at the junction is handled by the rope's grapheme boundary invariant (for rope results) or recomputed (for flat results)
- FR-20: `OP_TO_STRING` produces NFD-normalized output
- FR-21: `print` supports rope strings (streams or materializes)
- FR-22: Interned strings remain immortal (weak intern table deferred)
- FR-23: All string operations are transparent across tiers — no operation is tier-restricted

## Non-Goals (Out of Scope)

- Weak/GC-integrated intern table (deferred to a future milestone)
- NFC normalization or user-selectable normalization forms
- Regular expression matching
- `split`, `join`, `trim`, `upper`, `lower`, `replace`, `starts-with`, `ends-with`, `contains` (separate milestone)
- String-to-number parsing
- Mutable string buffers or StringBuilder
- String formatting / printf-style operations
- Locale-aware collation (comparison is NFD byte-order, which gives consistent Unicode ordering but not locale-specific sorting)
- NFKD/NFKC compatibility decomposition

## Technical Considerations

- **Depends on:** M5 (existing string system), lib/sum_tree/rope.h (rope library), lib/unicode/unicode.h (NFD, grapheme breaks)
- **Files modified:**
  - `src/string.c` — `JaclHeapString` struct change, `jacl_string_new()`, unified API updates, rope integration
  - `src/value.c` — `JACL_TAG_ROPE_STRING`, `jacl_is_string()` update, predicates
  - `src/vm.c` — `OP_CONCAT` cross-tier handling, `length`/`index`/`slice` grapheme semantics, `print` for ropes
  - `src/compiler.c` — use `jacl_string_new()` for literals
  - `src/gc.c` — `OBJ_ROPE_STRING` tracing
  - `lib/sum_tree/rope.h` — `grapheme_find_safe_split`, leaf construction update
  - `lib/unicode/unicode.h` — `grapheme_find_safe_split()` function
- **New file:** `test/test_heap_strings.c`
- **Unity build:** All code in existing `src/*.c` files via `src/jacl.c`
- **NFD expansion budget:** NFD can expand a string by up to ~3x in pathological cases (Hangul), but typical Latin text expands by <10%. The 128-byte threshold accommodates this.
- **Concat grapheme boundary handling:** The rope library enforces a structural invariant: all leaf boundaries are grapheme cluster boundaries. `rope_from_str` achieves this via `grapheme_find_safe_split`. `rope_concat` maintains it by checking if the right tree's leftmost leaf starts with Extend/ZWJ/SpacingMark codepoints; if so, those bytes are moved to the left tree's rightmost leaf and affected summaries are recomputed. This costs O(log n) in the persistent tree (2 leaves + ancestors) and only triggers when combining marks cross the junction. For flat concat results (≤128 bytes), the grapheme count is simply recomputed from scratch (O(n) where n ≤ 128, trivially fast). With this invariant, `rope_summary_combine` (simple summation) is always correct, and O(log n) grapheme seeking via summaries is always accurate.
- **Rope hash computation:** Iterate over all leaves in order, feeding each leaf's bytes into the FNV-1a state. This gives the same hash as if the string were flat. Cache in `JaclRopeString.hash`.
- **Performance characteristics:**
  - Inline strings: all operations O(1) or O(n) where n ≤ 7
  - Flat interned strings: length O(1), index/slice O(n) where n ≤ 128, equality O(1) via pointer
  - Rope strings: length O(1) via summary, index O(log n) via cursor, slice O(log n), equality O(1) hash check + O(n) worst case, concat O(log n) via structural sharing

## Success Metrics

- `[length "cafe\u0301"]` returns `4` (grapheme-correct)
- `[index "cafe\u0301" 3]` returns the e-with-accent grapheme cluster
- All three tiers produce identical results for the same operations
- Rope strings handle 100KB+ content without performance degradation
- No operation panics or produces incorrect results on any tier combination
- All existing M5 tests pass (with grapheme-length adjustments)
- Zero memory leaks in all tests

## Resolved Design Decisions

- **Concat grapheme boundary re-evaluation:** Check only when the right operand's first codepoint has Extend/ZWJ/SpacingMark grapheme break property. This is O(1) — decode one codepoint and look up its property. When the property is non-combining, grapheme counts are simply summed with no fix-up.
- **Rope summary accuracy at concat boundaries:** Enforced in the rope library itself. `rope_concat` maintains the structural invariant that all leaf boundaries are grapheme cluster boundaries by moving combining bytes from the right tree's leftmost leaf to the left tree's rightmost leaf when needed. This keeps `rope_summary_combine` (simple summation) always correct and enables accurate O(log n) grapheme seeking. The rope library already knows about graphemes (via `rope_summary_summarize` calling `unicode_grapheme_count`), so this is consistent with existing design.
- **Maximum string size:** No explicit cap. The JACL string API uses `size_t` for lengths (matching the rope library's internal `size_t`). Flat heap strings use `uint32_t byte_len` (capped at 128 bytes, so `uint32_t` is more than sufficient). Memory is the practical limit.
- **`byte-length` builtin name:** `byte-length` — consistent with `length` and reads naturally: `[byte-length s]`.
