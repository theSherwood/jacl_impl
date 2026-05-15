# Struct Design — Value Types for JACL

## Goal

Make structs maximally C-compatible: no object header, no GC involvement,
raw C ABI layout. Type metadata lives in a side-table (the struct registry),
not in the value itself. Structs are value types that infect static typing —
every producer and consumer must agree on the concrete type at compile time.

## Architecture

A user-defined struct is just its field bytes — no header, no tag, no
`type_idx` inline. The layout follows C ABI alignment and sizing rules,
identical to what a C compiler would produce for the same field sequence.

```
struct Point {i32 x, i32 y}    →    [x:4][y:4]   (8 bytes total)
```

Type metadata (field names, offsets, types) lives in the
`StructTypeRegistry`, keyed by a sequential `type_idx`. A struct value
carries no runtime tag — the static type system tracks it.

## Allowed Field Types

Struct fields must be value types:

| Allowed | Types |
|---------|-------|
| Integers | i32, u32, i64, u64 |
| Floats | f32, f64 |
| Boolean | bool |
| Nested structs | inline (value, not pointer) |

Reference types (`str`, `vec`, `map`, `closure`, `dyn`) are **rejected at
compile time** in `defstruct`. The error directs the user to `[box $val]`
to hold a reference. This eliminates GC scanning of struct instances —
no write barriers, no tracing.

## Storage

### VM stack (synchronous functions)

A struct local occupies `ceil(total_size / 8)` consecutive `JaclVal` stack
slots. The compiler marks the local as `is_inline` with `width = N`. The
slots are tracked in `vm->inline_slot_bitmap` so the GC knows not to
trace them as `JaclVal`s.

```
stack: [... | slot_base | slot_base+1 | slot_base+2 | ...]
             ^--- struct bytes laid out across slots ---^
```

Field access uses `OP_STRUCT_GET_INLINE base_slot byte_offset field_type`
(or `OP_STRUCT_SET_INLINE`) — direct byte arithmetic, no pointer
dereference. Closure-captured structs use the `_UPVALUE` variants.

### Transient values

A struct value left on TOS by an expression (a constructor, proc return,
typed-vec-get, etc.) is a sequence of `width` inline slots, also marked in
the bitmap.

- `OP_LOAD_INLINE_LOCAL base_slot type_idx` copies an inline local's bytes
  to TOS.
- `OP_LOAD_INLINE_UPVALUE base_uv_slot type_idx` does the same for a
  closure upvalue.
- `OP_STRUCT_GET_INLINE_TOS type_idx byte_offset field_type` pops the
  inline bytes and pushes a single field — used for transient field
  access like `$proc-result->x`.
- `OP_STRUCT_EQ_TOS type_idx` pops two structs (each may be inline or
  heap, dispatched via `vm__pop_struct`'s bitmap check) and pushes
  `memcmp == 0`.
- `OP_STRUCT_EXPAND type_idx` converts a heap pointer on TOS into inline
  slots (used at boundaries where a heap struct meets the inline path).

### State machine fields (async/generator functions)

Same approach. `JaclStateMachine.field_inline_bitmap` records which
`fields[]` slots are raw struct bytes. `OP_GET/SET_STATE_FIELD_WIDE`
operate on N-slot regions.

## Static Typing Boundaries

Structs cannot live in a `dyn` slot. This is the struct-specific
expression of `TYPE_SYSTEM.md`'s decision 2 (no implicit dyn coercion
at commitment sites): for scalars the cast `[to T $dyn]` papers over
the boundary, but structs have no runtime tag, so the only way across
the boundary is the explicit `[box]` / `[box? T]` / `[unbox]` triad.

The following are compile-time rejections that direct the user to
`[box $val]`:

- `def dyn d [Point ...]` — drop the `dyn` annotation or use `[box]`.
- `mut p [Point ...]` — must wrap with `[box]` so the cell holds a box
  reference (cells store `JaclVal`, not inline bytes).
- `set` on a dyn binding with a struct value — same rule.
- Passing a struct to a dyn parameter — same.
- Storing a struct in an untyped vec/map — must use `[box]` or a typed
  collection.
- Comparing a struct to a non-struct (or to a struct of unknown type) —
  must narrow with `[box? Type $val]` first.
- Mutating a transient inline struct (e.g. proc return) — must assign to
  a typed local first.

A struct value can only appear in:
- a typed local / parameter / return / upvalue (inline bytes across stack slots),
- a typed vec/map element (stored inline in the leaf),
- inside a `[box]` (sanctioned heap path via `JaclMutableRef`).

**Structs cannot live at top level.** A script like `def Point p [Point 1 2]`
at top level is a compile error directing the user to either wrap the body
in a proc (so `p` becomes a wide local) or use `[box]` explicitly. Globals
are single-slot `JaclVal` storage and would force auto-allocation otherwise.
Ctx is exempt — it's the lone HeapRecord builtin and is registered through
its own embedder-side path.

## Box Representation

Boxing is the **only** way for a struct to cross a `dyn` boundary. The
`JaclMutableRef` is unified across all box uses:

```c
typedef struct {
    uint32_t type_idx;    /* 0 = dyn JaclVal box, >0 = struct type index */
    uint32_t total_size;  /* byte size of data[] */
    uint8_t  data[];      /* JaclVal for type_idx==0, raw struct bytes otherwise */
} JaclMutableRef;
```

`OP_BOX_STRUCT type_idx` allocates a `JaclMutableRef` and copies struct
bytes into `data[]`. It dispatches on the inline bitmap at TOS:
- inline → `memcpy width slots` directly into `data[]`,
- heap → pop the `HeapRecord*` and copy `s->data`.

### Boxing syntax

```jacl
def Point p [Point 1 2]
def boxed [box $p]              # JaclMutableRef{type_idx=Point, data=bytes}
```

Box operations:
- `[deref $boxed]` — copies the struct out (heap path); for an inline
  destination, `OP_DEREF_INLINE type_idx` pushes inline slots.
- `[reset $boxed $new-point]` — overwrites box contents.
- `[swap $boxed $new-point]` — replace + return old.

### Unboxing and flow typing

`box?` narrows; `unbox` extracts.

```jacl
if [box? Point $val] {
  def Point p [unbox $val]    # compiler infers Point from the narrowing
}
```

`unbox` outside a `box?`-narrowed branch is a compile error. The two-arg
form `[box? Point $val]` is the only safe extraction path. `box?`
implicitly verifies the argument is a dynamic `JaclVal` — in statically
typed code the compiler already knows.

## Generic Operations on Boxed Structs

| Operation | Implementation |
|-----------|---------------|
| **Equality** | Compare `type_idx`; if equal, `memcmp` `data[]` |
| **Hashing** | Hash `type_idx` + raw `data[]` bytes |
| **Stringify** | `vm__fmt_struct_bytes` walks the registry, formats from raw bytes |
| **Type name** | Registry lookup by `type_idx` |

`vm__fmt_struct_bytes` is the shared formatter — used by `OP_PRINT_STRUCT`
(typed inline path) and the legacy heap-struct print case (now only
reached for ctx).

## ctx — the lone HeapRecord

`ctx` is **not** a user `defstruct`. It is a builtin embedder-defined
record that uses the heap representation:

```c
typedef struct {
    uint32_t type_idx;
    uint32_t total_size;
    uint8_t  data[];
} HeapRecord;
```

`OBJ_HEAP_RECORD` is the GC object kind. Ctx may contain reference fields
(e.g. `str pwd`) — the GC traces `data[]` through the registry. Field
access uses the legacy `OP_STRUCT_GET` / `OP_STRUCT_SET` opcodes against
a heap pointer.

This is the only sanctioned non-inline struct shape in the runtime.
Future runtime builtins of similar shape can reuse `HeapRecord`, but
there is no user-visible `Ptr T` syntax.

## Registry

The `StructTypeRegistry` is arena-backed. Each `StructTypeDef` is
allocated with its actual field count. Lookups return stable pointers; a
separate index array maps `type_idx` → `StructTypeDef*`. `reg->ctx_type_idx`
records ctx's slot.

The `is_value_type` flag on `StructTypeDef` distinguishes user structs
(true) from ctx (false). Since ref fields are now rejected, all user
defstructs are value-type by construction; the flag could be replaced
with a `idx == reg->ctx_type_idx` check.

Slot reservations (slot 0 = dyn placeholder, slot 1 = ctx, slot 2+ = user
structs in source order) are pre-arranged so the typer's `tc->structs[]`
and the compiler's `reg->defs[]` agree on indices without runtime
coordination — see `TYPE_SYSTEM.md` for the typer-side perspective.

## What This Eliminates

- Per-instance `GCHeader` (8 bytes) on user struct values.
- The `HeapRecord` header (8 bytes) on user struct values.
- GC tracing of user struct fields — no registry walk during mark.
- Write barriers for struct field writes.
- Auto-heap-allocation when a struct value crosses a typed boundary.
- Transient `HeapRecord` allocations in typed vec/map ops, print, eq,
  field access, destructure, and box.

## Open Questions

- **Fixed-size arrays in structs** (e.g., `[u8; 16]`): useful for byte
  buffers without strings. Layout system can accommodate; not yet wired.
- **Stack size:** `VM_STACK_MAX = 256` slots, `VM_FRAMES_MAX = 64`. Wide
  structs in deep call stacks could exhaust the operand stack. §16
  (2026-05-15) added `vm__set_operand_overflow` and
  `vm__set_frame_overflow` so the two limits surface as distinct,
  numerically-named error messages and propagate to awaiters via
  `runtime__make_completion_error`, but the limits themselves are
  unchanged.
- **Wide cells / globals:** mut bindings and globals currently store
  `JaclVal` slots; structs in those slots must go through `[box]`. A
  wide-cell architecture could allow inline mut struct bindings, but
  is not pursued here.
