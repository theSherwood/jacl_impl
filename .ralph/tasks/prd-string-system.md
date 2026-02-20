# PRD: Milestone 5 — String System

## Introduction

This milestone completes the string story for JACL. M0-M4 introduced inline strings (0-7 bytes
stored directly in the 64-bit tagged value), and the lexer/parser already handle string
interpolation syntax. M5 adds heap-interned strings for strings longer than 7 bytes, a global
intern table for deduplication and pointer-equality comparison, string builtins (`concat`,
`length`, `slice`, `index`, and comparison operators), and compiles string interpolation
(`"text $var and $[expr]"`) end-to-end.

After this milestone, JACL programs can work with strings of arbitrary length, compare them,
slice them, and build them dynamically via interpolation.

**Key design decision:** Heap-interned strings are **not null-terminated**. They carry an
explicit length field. This enables binary-safe strings and avoids off-by-one/sentinel bugs.

## Goals

- Support strings of arbitrary length (beyond the 7-byte inline limit)
- Intern all heap strings so that identical strings share a single allocation
- Provide pointer-equality fast path for heap-to-heap string comparison
- Provide core string operations: `concat`, `length`, `slice`, `index`
- Support string comparison: `==`, `<`, `>` across all string representations
- Compile string interpolation end-to-end: `"hello $name"` produces a string at runtime
- All string data arena-allocated (matching existing project patterns)
- Rope-backed large strings deferred to a future milestone

## Design Decisions

- **Heap string representation:** A `JaclHeapString` struct containing `{ uint32_t length;
  uint32_t hash; char data[]; }` — a flexible array member. The string data is **not
  null-terminated**; the `length` field is authoritative. The hash is stored alongside the
  data to avoid recomputation during intern table lookups.
- **Intern table:** A global hash table mapping `(data, length)` → `JaclHeapString*`. On
  string creation, the table is consulted first; if a match exists, the existing pointer is
  returned. This guarantees pointer equality for identical heap strings. The table and all
  string data are arena-allocated.
- **Tagged value:** `JACL_TAG_STRING` (0x05, already reserved) with a pointer payload to a
  `JaclHeapString`. The existing `jacl_string_ptr()` constructor and `jacl_as_ptr()` extractor
  are used.
- **Unified string API:** Helper functions that work on any string value regardless of
  representation: `jacl_string_len(v)`, `jacl_string_data(v, buf, buflen)`,
  `jacl_string_eq(a, b)`, `jacl_string_cmp(a, b)`. These dispatch on the tag internally.
- **Equality semantics (representation-aware):** Inline-to-inline compares via bitwise
  equality on the tagged value. Heap-to-heap compares via pointer equality (guaranteed by
  interning). Cross-representation (inline vs heap) compares by content (length + memcmp).
  In practice, strings ≤7 bytes are always inline and strings >7 bytes are always heap, so
  cross-representation comparison only arises if the same short string exists in both forms
  (unlikely but handled correctly).
- **String concatenation:** A new `OP_CONCAT` opcode that pops two string values, produces
  their concatenation (choosing inline or heap based on result length), and pushes the result.
- **Interpolation compilation:** `AST_INTERP_STRING` compiles each segment, then chains
  `OP_CONCAT` calls to build the final string. Non-string segments (e.g., integer variables)
  are coerced to strings via a `OP_TO_STRING` opcode.
- **Arena allocation:** The intern table, its bucket array, and all `JaclHeapString` instances
  are allocated from the existing arena. This keeps M5 consistent with the rest of the project
  and defers GC concerns to M10.

## User Stories

### US-001: Heap string type and intern table

**Description:** As the runtime, I need a heap-allocated interned string type so that strings
longer than 7 bytes can be represented and deduplicated.

**Acceptance Criteria:**
- [ ] `JaclHeapString` struct defined: `{ uint32_t length; uint32_t hash; char data[]; }`
- [ ] String data is NOT null-terminated; `length` is the sole source of truth
- [ ] `JaclInternTable` struct: hash table with open addressing or chaining (arena-allocated)
- [ ] `jacl_intern(arena, table, data, length)` → returns `JaclVal` with `JACL_TAG_STRING`
- [ ] Interning the same bytes twice returns the same pointer (pointer equality guaranteed)
- [ ] Hash function: FNV-1a or similar, operating on `(data, length)` — not relying on null terminator
- [ ] Table resizes (rehashes) when load factor exceeds threshold (e.g., 0.75)
- [ ] All allocations go through the arena
- [ ] `jacl_is_heap_string(v)` predicate works correctly
- [ ] Unit tests: intern a string, intern the same bytes again, verify pointer equality;
      intern different strings, verify different pointers; strings with embedded NUL bytes work
- [ ] Compiles cleanly with `-Wall -Wextra -std=c99`

---

### US-002: Unified string access API

**Description:** As the VM and builtins, I need a uniform API to work with any string value
(inline or heap) without caring about representation.

**Acceptance Criteria:**
- [ ] `jacl_string_len(JaclVal v)` → `uint32_t` — returns length for both inline and heap strings
- [ ] `jacl_string_data(JaclVal v, char* buf, size_t buflen)` → copies string bytes into `buf`,
      returns actual length. For inline strings, extracts from payload. For heap strings, copies
      from `JaclHeapString.data`.
- [ ] `jacl_is_string(JaclVal v)` — returns true for both inline and heap strings
- [ ] `jacl_string_eq(JaclVal a, JaclVal b)` — representation-aware equality:
  - Inline vs inline: bitwise comparison of tagged values
  - Heap vs heap: pointer comparison (interning guarantees equality)
  - Cross-representation: length check + `memcmp` of contents
- [ ] `jacl_string_cmp(JaclVal a, JaclVal b)` → `int` — lexicographic comparison for ordering
      (`<`, `>`), works across representations
- [ ] Unit tests verify all combinations: short==short, long==long, short!=long, ordering

---

### US-003: Compiler support for heap string literals

**Description:** As the compiler, I need to emit heap-interned string constants for string
literals longer than 7 bytes, removing the current hard error.

**Acceptance Criteria:**
- [ ] Compiler holds a pointer to a `JaclInternTable` (passed in or owned by the compilation context)
- [ ] `AST_LIT_STRING` with `length <= 7`: still emits inline string constant (unchanged)
- [ ] `AST_LIT_STRING` with `length > 7`: interns the string via the table, emits the resulting
      `JACL_TAG_STRING` value as a constant
- [ ] The 7-byte error message is removed
- [ ] `[print "hello world"]` compiles and runs (previously errored)
- [ ] `[def greeting "hello world"] [print $greeting]` works end-to-end
- [ ] String constants that appear multiple times in source share the same interned pointer
- [ ] Unit tests verify short and long string literals compile correctly

---

### US-004: String equality and comparison in the VM

**Description:** As a JACL programmer, I want `==`, `<`, and `>` to work on strings so that
I can compare them.

**Acceptance Criteria:**
- [ ] `[== "abc" "abc"]` returns `true` (inline, bitwise fast path)
- [ ] `[== "hello world" "hello world"]` returns `true` (heap, pointer fast path)
- [ ] `[== "abc" "def"]` returns `false`
- [ ] `[< "abc" "def"]` returns `true` (lexicographic)
- [ ] `[> "def" "abc"]` returns `true`
- [ ] `[== "abc" 42]` returns `false` (type mismatch is not an error, just false)
- [ ] Comparison operators dispatch to `jacl_string_eq` / `jacl_string_cmp` when operands are strings
- [ ] Existing numeric comparisons continue to work unchanged
- [ ] Tests cover inline-inline, heap-heap, and mixed-type comparisons

---

### US-005: String concatenation (`concat` builtin and `OP_CONCAT`)

**Description:** As a JACL programmer, I want to concatenate strings so that I can build
new strings from parts.

**Acceptance Criteria:**
- [ ] `OP_CONCAT` opcode: pops two string values from the stack, pushes their concatenation
- [ ] Result is inline if total length ≤ 7, otherwise heap-interned
- [ ] `concat` registered as a builtin: `[concat "hello" " world"]` → `"hello world"`
- [ ] `concat` accepts 2+ arguments: `[concat "a" "b" "c"]` → `"abc"` (variadic, chains pairwise)
- [ ] Concatenating two empty strings returns `""` (empty inline string)
- [ ] Concatenating an inline + heap string works correctly
- [ ] The VM needs access to the intern table (passed via VM context or global)
- [ ] Tests verify short+short, short+long, long+long, variadic, empty cases

---

### US-006: String builtins (`length`, `slice`, `index`)

**Description:** As a JACL programmer, I want string operations so that I can inspect and
manipulate strings.

**Acceptance Criteria:**
- [ ] `[length "hello"]` → `5` (returns i32)
- [ ] `[length ""]` → `0`
- [ ] `[length "hello world this is long"]` → correct length for heap string
- [ ] `[index "hello" 1]` → `"e"` (returns single-character inline string, 0-based indexing)
- [ ] `[index "hello" -1]` or out-of-bounds → returns `nil` (not an error)
- [ ] `[slice "hello" 1 3]` → `"el"` (start inclusive, end exclusive)
- [ ] `[slice "hello" 2]` → `"llo"` (one-arg form: from start to end)
- [ ] `[slice "hello world this is long" 6 11]` → `"world"` (works on heap strings)
- [ ] Slice result is inline if ≤7 bytes, otherwise heap-interned
- [ ] All builtins work on both inline and heap strings transparently
- [ ] Tests cover edge cases: empty string, full slice, zero-length slice, boundary indices

---

### US-007: `OP_TO_STRING` — value-to-string coercion

**Description:** As the runtime, I need to convert non-string values to their string
representation so that string interpolation can include numbers, booleans, and nil.

**Acceptance Criteria:**
- [ ] `OP_TO_STRING` opcode: pops a value, pushes its string representation
- [ ] Integer → decimal string: `42` → `"42"`, `-7` → `"-7"`
- [ ] Float → decimal string: `3.14` → `"3.14"` (reasonable formatting, no trailing zeros)
- [ ] Boolean → `"true"` / `"false"`
- [ ] Nil → `"nil"`
- [ ] String → no-op (already a string, push unchanged)
- [ ] Closure → `"<proc name>"` or `"<closure>"` (matches existing print behavior)
- [ ] Result is inline if ≤7 bytes, otherwise heap-interned
- [ ] Tests verify each type conversion

---

### US-008: String interpolation compilation

**Description:** As a JACL programmer, I want `"hello $name, you are $[+ $age 1]"` to
produce a concatenated string at runtime.

**Acceptance Criteria:**
- [ ] `AST_INTERP_STRING` compilation no longer errors — the stub is replaced with real code
- [ ] Each literal segment compiles to a string constant (inline or heap)
- [ ] Each `AST_VAR_REF` segment compiles to: get variable, `OP_TO_STRING`
- [ ] Each `AST_COMMAND` segment compiles to: compile the command, `OP_TO_STRING`
- [ ] Segments are concatenated left-to-right using `OP_CONCAT`
- [ ] `[def name "world"] [print "hello $name"]` prints `hello world`
- [ ] `[def x 42] [print "x is $x"]` prints `x is 42`
- [ ] `[print "sum is $[+ 1 2]"]` prints `sum is 3`
- [ ] `[print "$[+ 1 2] plus $[+ 3 4]"]` prints `3 plus 7`
- [ ] Empty interpolation segments handled correctly
- [ ] Tests verify variable interpolation, expression interpolation, mixed segments,
      adjacent interpolations

---

### US-009: Print support for heap strings

**Description:** As a JACL programmer, I want `print` to display heap strings correctly.

**Acceptance Criteria:**
- [ ] `[print "hello world this is a long string"]` prints the full string
- [ ] `[print "short"]` continues to work (inline strings unchanged)
- [ ] Print extracts string data via the unified API (`jacl_string_data` or direct pointer)
- [ ] No trailing garbage or null-terminator issues (heap strings are not null-terminated)
- [ ] Tests verify printing short strings, long strings, and concatenated results

---

### US-010: End-to-end integration tests

**Description:** As a developer, I need comprehensive integration tests exercising the full
string system through `jacl_run()`.

**Acceptance Criteria:**
- [ ] C-level tests in `test/test_m5.c` using `jacl_run()` and PrintCapture pattern
- [ ] Test: `[print "hello world"]` prints `hello world` (basic heap string)
- [ ] Test: `[def s "hello"] [print [concat $s " world"]]` prints `hello world`
- [ ] Test: `[print [length "hello"]]` prints `5`
- [ ] Test: `[print [index "hello" 0]]` prints `h`
- [ ] Test: `[print [slice "hello world" 6 11]]` prints `world`
- [ ] Test: `[def name "JACL"] [print "hello $name"]` prints `hello JACL`
- [ ] Test: `[print "1 + 2 = $[+ 1 2]"]` prints `1 + 2 = 3`
- [ ] Test: `[== "hello world" "hello world"]` returns true (interned pointer equality)
- [ ] Test: `[< "abc" "xyz"]` returns true
- [ ] Test: String used as proc argument and return value works
- [ ] Test: Closure captures a heap string
- [ ] All existing M0-M4 tests continue to pass
- [ ] Zero memory leaks detected by tracked allocator in all tests
- [ ] Test file added to `build.sh` TESTS array

---

### US-011: Mark M5 complete in DESIGN.md

**Description:** As a project maintainer, I want M5 marked as complete in DESIGN.md once
all stories are implemented, so the roadmap reflects actual progress.

**Acceptance Criteria:**
- [ ] All US-001 through US-010 are implemented and passing
- [ ] M5 row in DESIGN.md milestone table updated from empty to `**COMPLETE**`
- [ ] Commit message references M5 completion

## Functional Requirements

- FR-1: `JaclHeapString` stores `{ length, hash, data[] }` — data is not null-terminated
- FR-2: `JaclInternTable` deduplicates heap strings; identical bytes yield identical pointers
- FR-3: `JACL_TAG_STRING` (0x05) holds a pointer to a `JaclHeapString`
- FR-4: String literals >7 bytes compile to heap-interned constants via the intern table
- FR-5: String literals ≤7 bytes continue to use `JACL_TAG_INLINE_STRING` (unchanged)
- FR-6: `==` on two heap strings uses pointer equality (O(1) via interning)
- FR-7: `==` on two inline strings uses bitwise comparison (unchanged)
- FR-8: `==` across representations compares by content (length + memcmp)
- FR-9: `<` and `>` on strings use lexicographic byte comparison
- FR-10: `==` between a string and a non-string returns `false` (not an error)
- FR-11: `[concat s1 s2 ...]` produces a new string from 2+ arguments
- FR-12: `[length s]` returns the byte length as an i32
- FR-13: `[index s i]` returns a single-byte string at position `i`, or `nil` if out of bounds
- FR-14: `[slice s start end?]` returns a substring; result interned if >7 bytes
- FR-15: `OP_TO_STRING` converts any value to its string representation
- FR-16: `OP_CONCAT` concatenates two string values on the stack
- FR-17: String interpolation `"text $var $[expr]"` compiles to segment evaluation + coercion + concatenation
- FR-18: `print` handles both inline and heap strings
- FR-19: All string data and intern table entries are arena-allocated
- FR-20: Source line numbers propagate through new opcodes for error reporting

## Non-Goals (Out of Scope)

- No rope-backed large strings — deferred to a future milestone
- No regular expression matching
- No Unicode-aware operations (length/index/slice are byte-based for now)
- No `mut` / `set!` for string variables — that's M8
- No string formatting / printf-style operations
- No `split`, `join`, `trim`, `upper`, `lower`, `starts-with`, `ends-with`, `contains`
- No string-to-number parsing (`to` builtin deferred to M9)
- No GC of interned strings — arena freed in bulk (GC is M10)
- No mutable string buffers

## Technical Considerations

- **Depends on:** M0 (value.c, `JACL_TAG_STRING` already reserved), M1 (lexer — string
  tokenization complete), M2 (parser — `AST_INTERP_STRING` complete), M3 (bytecode/compiler/VM),
  M4 (closures, procs)
- **Files modified:** `src/value.c` (heap string type, intern table, unified API),
  `src/bytecode.c` (new opcodes `OP_CONCAT`, `OP_TO_STRING`), `src/compiler.c` (heap string
  literals, interpolation compilation), `src/vm.c` (opcode handlers, comparison dispatch,
  print update, intern table access)
- **New file:** `test/test_m5.c` for integration tests
- **Unity build:** All new code goes in existing `src/*.c` files, included via `src/jacl.c`
- **Arena allocation:** Intern table buckets, `JaclHeapString` instances — all arena-allocated
- **Intern table threading:** Single-threaded for now. Concurrency (M11) may require
  synchronization — not a concern for M5
- **Test pattern:** Same as M3/M4 — `test_helpers.h`, `tracker_reset()`, `check_no_leaks()`,
  PrintCapture for output verification
- **Compiler flags:** `-Wall -Wextra -std=c99 -g`
- **Naming:** `jacl_intern`, `jacl_string_len`, `jacl_string_eq`, `jacl_string_cmp`,
  `compiler__compile_interp_string`, `vm__op_concat`, `vm__op_to_string`, etc.
- **Hash function:** FNV-1a recommended — simple, fast, good distribution for string keys.
  Must operate on `(data, length)` pair, not rely on null terminator.
- **Intern table sizing:** Start at 64 buckets, double on resize. Load factor threshold 0.75.

## Success Metrics

- `[print "hello world, this string is longer than seven bytes"]` prints correctly
- `[def name "JACL"] [print "hello $name"]` prints `hello JACL`
- `[print "1 + 2 = $[+ 1 2]"]` prints `1 + 2 = 3`
- `[== "long string here" "long string here"]` returns `true` via pointer equality
- `[print [concat "hello" " " "world"]]` prints `hello world`
- `[print [slice "hello world" 0 5]]` prints `hello`
- All M0-M4 tests continue to pass
- Zero memory leaks in all tests

## Open Questions

- Should `index` return a single-character string or an integer codepoint? Recommendation:
  single-character string (consistent with string-in, string-out philosophy). Integer
  codepoint access can be added later as a separate builtin.
- Should `length` count bytes or Unicode codepoints? Recommendation: bytes for M5 (simple,
  fast). A `char-length` or Unicode-aware `length` can be added when rope integration lands.
- Maximum intern table size / OOM behavior? Recommendation: let the arena handle OOM. If
  the arena cannot allocate, it's a fatal error (same as today for all allocations).
- Should `concat` with zero arguments return `""`? Recommendation: require at least 2
  arguments for now. A variadic `str` builder can come later.
