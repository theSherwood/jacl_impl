# Build Refactor Plan: Shared Library for Test Compilation

## Problem

Cold test builds take ~5m54s. Of that, ~5m25s is compilation and only ~30s is
test execution. The root cause: all 56 test files in `test/` do
`#include "../src/jacl.c"`, and `jacl.c` is a unity build that pulls in 12
source files. Every test binary recompiles the entire language implementation
from scratch. When any `.c` or `.h` file in `src/` or `lib/` changes, all 56
tests are invalidated and recompiled.

The 35 library tests (`lib/*/test_*.c`) are unaffected -- they compile against
standalone headers and are already fast.

## Goal

Compile `jacl.c` once into a static archive (`libjacl.a`). Each test links
against it instead of re-including the source. Expected cold-build improvement:
**~5m30s -> ~40-60s** (1 full compile + 56 lightweight link-only compiles).

## Current Architecture

```
test/test_foo.c
  #include "test_helpers.h"
  #include "../src/jacl.c"    <-- unity build: recompiles everything
```

`src/jacl.c` includes (in order):
```
value.c -> gc.c -> [rope.h with GC macros] -> string.c -> lexer.c ->
ast.c -> parser.c -> bytecode.c -> collections.c -> compiler.c ->
vm.c -> gc_collect.c -> runtime.c
```

Key constraints:
- Include order matters (later files depend on types from earlier files)
- 509 static functions across 12 source files (tests call many of them)
- 7 thread-local variables (e.g., `gc__current_heap`) accessed by tests
- Rope library (`sum_tree/rope.h`) is integrated via GC macros defined
  between `gc.c` and `string.c`

## Approach

### Phase 1: Generate `src/jacl.h` (the public header)

Create a single header that exposes all types and function declarations tests
need. This replaces the implicit visibility that `#include "jacl.c"` provides.

**1a. Extract type definitions into `jacl.h`**

Collect all `typedef`, `struct`, `enum`, and `#define` declarations that tests
reference. These are currently scattered across the 12 source files. The header
must preserve the dependency order from the unity build:

```c
/* jacl.h — Public declarations for the JACL pipeline */
#ifndef JACL_H
#define JACL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../lib/arena/arena.h"
#include "../lib/platform/platform.h"
#include "../lib/unicode/unicode.h"

/* Forward declarations and GC macros needed for rope.h */
/* ... GC object type enum, gc_alloc declaration ... */

#define STREE_GC_MODE
#define STREE_GC_ALLOC(t, sz)    gc_alloc(gc__current_heap, (t), (sz))
#define STREE_GC_OBJ_INTERNAL    OBJ_ROPE_INTERNAL
#define STREE_GC_OBJ_LEAF        OBJ_ROPE_LEAF
#include "../lib/sum_tree/rope.h"

/* Type definitions from value.c, gc.c, string.c, ... runtime.c */
/* (in unity-build order) */

/* Function declarations (generated — see Phase 2) */

/* Thread-local variable declarations */
extern JACL_THREAD_LOCAL ThreadHeap *gc__current_heap;
extern JACL_THREAD_LOCAL uint64_t    gc__thread_epoch;
/* ... etc ... */

#endif /* JACL_H */
```

Approximate size: ~400-600 lines (type defs + function declarations).

**1b. Separate type definitions from function bodies in each source file**

Currently, structs are defined inline near the functions that use them. For
`jacl.h` to work, type definitions must be extractable. Two approaches:

- **Option A (recommended):** Write a script that parses each `.c` file and
  extracts `typedef`/`struct`/`enum` blocks, then emits them in order into
  `jacl.h`. Manually review and fix any issues.
- **Option B:** Manually move type definitions into `jacl.h` by hand, leaving
  only function bodies in the `.c` files.

Option A is faster given the scale (40+ types across 12 files).

### Phase 2: Remove `static` from public functions

All 509 `static` functions become non-static so they are visible to the linker.
Not all 509 need declarations in `jacl.h` — only those called by tests. But
removing `static` from all of them is simpler and avoids needing to audit each
one.

**Approach:** A `sed`/script pass over all `src/*.c` files:

```bash
# Remove 'static' qualifier from function definitions
sed -i 's/^static \(inline \)\?/\1/' src/*.c
```

Then generate declarations for `jacl.h`:

```bash
# Extract function signatures from the .c files and emit as extern declarations
# (The existing tools/add_decl_guards.py may be adaptable for this)
```

**Risk — name collisions:** With 509 functions losing `static`, there's a chance
of name collisions across files. Mitigated by the existing naming convention
(`compiler__`, `gc__`, `vm__`, `sm__`, etc.).

**Risk — unused-function warnings:** Some helper functions may only be used
within their file. These will generate warnings if not called elsewhere. Mark
with `__attribute__((unused))` or keep `static` for truly-internal helpers
that no test calls.

### Phase 3: Update `build.sh`

**New Phase 0: Build `libjacl.a`**

```bash
# Compile the unity build once
$CC $CFLAGS -c src/jacl.c -o $BUILD_DIR/jacl.o -lm
ar rcs $BUILD_DIR/libjacl.a $BUILD_DIR/jacl.o

# Cache invalidation: rebuild only if any src/ or lib/ file changed
if [ "$BUILD_DIR/jacl.o" -ot "$NEWEST_SRC" ]; then
    # recompile
fi
```

**Modified Phase 1: Compile tests (link-only)**

```bash
# Before (recompiles everything):
$CC $flags "$src" -o "$BUILD_DIR/$name" -lm

# After (links against prebuilt library):
$CC $flags "$src" -o "$BUILD_DIR/$name" $BUILD_DIR/libjacl.a -lm
```

**Cache invalidation change:** Test binaries in `test/` now depend on:
1. The test source file itself
2. `libjacl.a` (NOT every individual source file)

Library tests (`lib/*/test_*.c`) remain unchanged — they don't use jacl.c.

### Phase 4: Update test files

Each of the 56 test files changes one line:

```c
// Before:
#include "../src/jacl.c"

// After:
#include "../src/jacl.h"
```

This is a simple `sed` one-liner:

```bash
sed -i 's|#include "../src/jacl.c"|#include "../src/jacl.h"|' test/test_*.c
```

### Phase 5: Validate

1. `bash build.sh --clean && bash build.sh` — full suite passes
2. Touch one `src/*.c` file, rebuild — only `jacl.o` + affected test recompiles
3. Touch one `test/*.c` file, rebuild — only that test recompiles (not `jacl.o`)
4. Time comparison: cold build before vs after

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Missing type in `jacl.h` | Compile error in some test | Iterative: fix errors until all 56 tests compile |
| Name collision after removing `static` | Linker error (duplicate symbol) | Rename or keep `static` on colliding helpers |
| Macro-dependent types (e.g., rope GC macros) | Wrong rope behavior | Ensure `jacl.h` defines GC macros before including `rope.h` |
| Thread-local visibility | Link error or wrong TLS model | Declare as `extern JACL_THREAD_LOCAL` in header |
| `embed.c` (separate from unity build) | May need its own handling | Check if `test_embed.c` uses a different include pattern |

## Effort Breakdown

| Phase | Effort | Automation |
|-------|--------|------------|
| 1: Generate `jacl.h` | Medium | Script-assisted extraction + manual review |
| 2: Remove `static` | Low | Single sed pass + generate declarations |
| 3: Update `build.sh` | Low | ~15 lines changed |
| 4: Update test files | Low | Single sed pass |
| 5: Fix compile errors | Medium | Iterative (expect 1-2 hours of chasing missing types/declarations) |

**Total estimate: a focused day of work.** The mechanical parts (Phases 2-4) take
minutes. The bulk of the effort is Phase 1 (getting `jacl.h` right) and Phase 5
(fixing the inevitable compile errors from implicit dependencies).

## Alternative Considered: Precompiled Header (PCH)

GCC/Clang support precompiled headers, which could cache the parsed result of
`jacl.c`. This would require zero code changes but:
- PCH support varies across compilers and is fragile
- Doesn't help with linking (each test still produces a full binary)
- Marginal improvement vs the library approach

The static archive approach is more robust and standard.

## File Changes Summary

| File | Change |
|------|--------|
| `src/jacl.h` | **New** — type definitions + function declarations |
| `src/*.c` (12 files) | Remove `static` from ~509 functions |
| `test/test_*.c` (56 files) | Change `#include "jacl.c"` to `#include "jacl.h"` |
| `build.sh` | Add library build phase; update test compilation to link |
