# ds

A collection of generic data structures implemented in C using macro-polymorphism.

## Modules

| Module | Status | Description |
|--------|--------|-------------|
| `arena` | Complete | Arena (bump) allocator |
| `array/realloc_array` | Complete | Growable array via realloc |
| `array/segment_array` | Complete | Segmented array with stable pointers |
| `rc` | Complete | Reference-counted smart pointer |
| `hamt` | Complete | Hash Array Mapped Trie (persistent map) |
| `pvec` | WIP | Persistent vector (relaxed radix balanced tree) |
| `chase_lev` | Complete | Chase-Lev concurrent work-stealing deque |
| `array/immutable_vec` | WIP | Immutable vector |

## Demos

### rdoc terminal editor

A minimal terminal editor exercising the rdoc rich-text document data structure (text editing, marks, blocks, inline blocks, and token cursor rendering).

```
cc -std=c99 -Wall -Wextra -o demo_rdoc sum_tree/demo_rdoc.c
./demo_rdoc
```

Keys: type to insert, arrow keys to move, shift+arrow to select, Ctrl+B bold, Ctrl+K block, Ctrl+I inline, Ctrl+C quit.

## Build & Test

Run all tests:

```
./build.sh
```

Or compile and run a single module's test:

```
gcc -Wall -Wextra -std=c99 -g <module>/test_<name>.c && ./a.out
```

## Macro-Polymorphism Pattern

Modules use header-only, macro-driven generics. Define type/name macros
before including a header to generate a type-specific implementation:

```c
#define REALLOC_ARRAY_TYPE int
#define REALLOC_ARRAY_NAME int_array
#include "array/realloc_array.h"
```

Some modules use `#define <MODULE>_IMPL` to emit the implementation.
