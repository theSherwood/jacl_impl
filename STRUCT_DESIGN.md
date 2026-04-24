# Struct Design — Value Types for JACL

## Goal

Make structs maximally C-compatible: no object header, no GC involvement,
raw C ABI layout. Type metadata lives in a side-table (the struct registry),
not in the value itself. Structs are value types that infect static typing —
every producer and consumer must agree on the concrete type at compile time.

## Current Architecture

Every struct instance is heap-allocated with a `GCHeader` (8 bytes) prepended,
followed by a `JaclStruct` header (`type_idx` + pad, 8 bytes), then the field
data in C ABI layout. Total overhead: 16 bytes per struct. The GC traces
structs by walking fields via the `StructTypeRegistry`.

## Proposed Architecture

### Layout

A struct is just its field bytes. No header, no tag, no `type_idx` inline.
The layout follows C ABI alignment and sizing rules — identical to what a
C compiler would produce for the same field sequence.

```
Current:  [GCHeader 8B][type_idx 4B][pad 4B][field data...]
Proposed: [field data...]
```

### Allowed Field Types

Structs may only contain non-reference types:

| Allowed | Types |
|---------|-------|
| Integers | i8, u8, i16, u16, i32, u32, i64, u64 |
| Floats | f32, f64 |
| Boolean | bool |
| Nested structs | inline (value, not pointer) |

Reference types (str, vec, map, closure, dyn) are **not** allowed as struct
fields. This eliminates GC scanning of structs entirely — no write barriers,
no root registration, no tracing.

### Storage: Wide Locals

Structs are stored inline wherever their containing scope lives:

**VM stack (synchronous functions):**
A struct local occupies `ceil(total_size / 8)` consecutive `JaclVal` stack
slots. The compiler tracks the base slot index and width. Field access
computes a byte offset from the base slot, then reads/writes the raw bytes.

```
stack: [... | slot_base | slot_base+1 | slot_base+2 | ...]
             ^--- struct bytes laid out across slots ---^
```

**State machine fields (async/generator functions):**
Same approach — a struct local reserves N consecutive entries in the state
object's `fields[]` array. The compiler records the base field index and
width in the `StateLayout`. `OP_GET/SET_STATE_FIELD` get struct-aware
variants that operate on N-slot regions.

No copying in/out at suspension points — the struct lives in `fields[]`
permanently, accessed directly via byte offsets.

### Static Typing Infection

Structs opt into static typing at every boundary:

- **Function arguments:** The compiler knows the struct type and width at
  every call site. N slots are pushed for a struct argument.
- **Function returns:** The compiler knows the return type is a specific
  struct. The calling convention reserves N return slots. A function that
  returns a struct must always return that struct type — no polymorphic
  returns.
- **Closure capture:** If a closure captures a struct, the compiler knows
  the type at capture time. The upvalue region is widened to hold the
  struct's bytes.
- **Nested calls:** If function A calls function B which returns a struct,
  A must also statically know the struct type. The typing requirement
  propagates through the call chain.

### Equality and Hashing

Since structs contain only value types (no reference fields), equality is
byte-level comparison:

```c
bool struct_equal(void* a, void* b, uint32_t size) {
    return memcmp(a, b, size) == 0;
}
```

Hashing is the same — hash the raw bytes. No per-field dispatch, no
recursive walk.

### Unified Box Representation

Struct boxing is unified with JACL's existing `box` (mutable container).
A box is always a GC-managed mutable container for any value. The
`JaclMutableRef` representation is extended to carry type information:

```c
typedef struct {
    uint32_t type_idx;    // 0 = dyn JaclVal, N = struct type from registry
    uint32_t total_size;  // byte count of data[]
    uint8_t  data[];      // value bytes
} JaclMutableRef;
```

- **`type_idx == 0`:** Box contains a plain `JaclVal` (8 bytes). GC traces
  the `JaclVal` in `data[]` (it could reference strings, vecs, etc.).
- **`type_idx > 0`:** Box contains a struct. `data[]` holds raw C ABI bytes.
  GC does not trace into `data[]` (no reference fields).

This replaces the old `JaclMutableRef { JaclVal value; }` with a single
unified representation. One allocation path, one GC object type, one set
of operations.

### Boxing Syntax

Boxing is explicit — uses the existing `box` command:

```
def Point p [Point 1 2]
def boxed [box $p]         # box a struct (copies bytes in)
```

All existing box operations work:
- `[deref $boxed]` — copy the struct out (by value)
- `[reset $boxed $new-point]` — replace the contents
- `[swap $boxed $new-point]` — swap contents, returns old value

### Unboxing and Flow Typing

Unboxing uses `box?` for type narrowing and `unbox` for extraction. The
`box?` predicate is extended to accept an optional type argument:

```
# Check if a dyn value is any box
[box? $val]

# Check if a dyn value is a box containing a Point
[box? Point $val]

# [box? dyn $val] is the explicit form of [box? $val]
[box? dyn $val]
```

The two-argument form enables **flow typing**: inside a branch guarded by
`[box? Point $val]`, the compiler knows `$val` holds a boxed `Point`.
The `unbox` command extracts the struct without needing a type argument —
the compiler infers it from the narrowed type:

```
if [box? Point $val] {
  def Point p [unbox $val]    # compiler knows it's a Point
}
```

**`unbox` outside a narrowed scope is a compile error.** The compiler
requires a `box?` type guard before allowing `unbox`. This ensures unboxing
is always safe — the type check happens once in the guard, and the
compiler statically verifies the extraction.

`box?` also implicitly checks that the argument is a dynamic `JaclVal` —
in statically typed code the compiler already knows whether something is
a box at compile time.

### Generic Operations on Boxed Values

Four operations work on boxed structs without unboxing:

| Operation | Implementation |
|-----------|---------------|
| **Equality** | Compare `type_idx`; if equal, `memcmp` the `data[]` bytes |
| **Hashing** | Hash `type_idx` + raw `data[]` bytes |
| **Stringify** | Registry lookup by `type_idx` to get field names, walk fields by type for formatting |
| **Type name** | Registry lookup by `type_idx`, return the struct's name |

Stringify is the only one that needs the registry at runtime. The others are
pure byte operations.

### Registry Changes

The `StructTypeRegistry` currently caps at 32 types (`STRUCT_REGISTRY_MAX`).
This is replaced with an arena-based registry. Each `StructTypeDef` is
allocated in the arena with its actual field count (no `STRUCT_MAX_FIELDS`
limit). Lookups return a `StructTypeDef*` pointer into arena memory. A
separate index array maps sequential `type_idx` values to arena pointers
for box type checks. The arena grows by acquiring new pages — existing
pointers remain stable.

Note: `type_idx == 0` is reserved for plain `dyn JaclVal` boxes. Struct
types start at `type_idx == 1`.

### Compiler Changes

**StateLayout:**
- `StateField` gains a `width` field (number of slots, default 1).
- `sm__add_state_field` gets a width parameter for struct locals.
- `sm__find_field` returns the base index; the compiler emits width from
  the `StructTypeDef.total_size`.
- Liveness optimization treats multi-slot struct fields as atomic units.

**Stack frame:**
- The compiler tracks which local ranges are struct regions (base slot +
  width + type index).
- Field access emits byte-offset arithmetic from the base slot rather
  than pointer dereference.

**New opcodes (or opcode variants):**
- Struct field read/write by byte offset from a base slot.
- Struct copy (N slots) for argument passing and return.
- Box value (allocate unified `JaclMutableRef`, memcpy bytes in).
- Unbox struct from box (memcpy bytes out, type inferred from flow typing).

### What This Eliminates

- `GCHeader` on struct instances (8 bytes saved per struct)
- `JaclStruct` header (8 bytes saved per struct)
- GC tracing of struct fields (no registry lookup during mark phase)
- Write barriers for struct field writes
- Heap allocation for short-lived structs (stack-allocated instead)
- Per-allocation GC pressure from struct creation

### Resolved Questions

- **Boxing syntax:** Explicit via existing `box` command: `[box $point]`.
  Boxing is unified — `box` always creates a `JaclMutableRef` with type info.
- **Unboxing syntax:** `[unbox $val]` inside a `[box? Type $val]` flow-typed
  branch. Compiler infers the struct type from narrowing. `unbox` outside a
  narrowed scope is a compile error.
- **Box representation:** Unified `JaclMutableRef` with `type_idx` (0 = dyn
  JaclVal, N = struct type) + `total_size` + `data[]`. Replaces the old
  `{ JaclVal value; }` representation.
- **Struct creation syntax:** Preserved from existing implementation:
  `struct Name {type field, ...}` for definition, `Name val1 val2` for
  construction, `$s->field` for access.

### Open Questions

- **Fixed-size arrays in structs** (e.g., `[u8; 16]`): Useful since strings
  aren't allowed. Not required for the initial implementation but the layout
  system should accommodate them. Deferred to a follow-up PRD.
- **Stack size:** `VM_STACK_MAX` is 256 slots. Struct-heavy code with deep
  call stacks could exhaust this. May need to increase or add diagnostics.
