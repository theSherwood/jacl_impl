# JACL Buffer Type Design

`[Buf N T]` is a compile-time-sized, contiguous, ABI-flat array of `T`.
It fills the gap between `[Ptr T]` (single element, raw) and `[Vec T]`
(GC-owned, header-bearing, growable) — the "block of N T's, laid out
like C's `T[N]`" case that FFI needs.

Cross-references: `TYPE_SYSTEM.md` (typer pipeline & invariants),
`STRUCT_DESIGN.md` (struct field layout buffers extend),
`NOT_IMPLEMENTED.md` §4 (typer deferred work — remove buffer items
when M4 lands).

## Surface

```
[Buf N T]                              ; type annotation; N is literal int, T is a type
def [Buf 256 u8] scratch               ; zero-init local
def [Buf 4 u8] magic [[Buf 4 u8] 0x7f 0x45 0x4c 0x46]   ; literal init
[buf-get $scratch $i]                  ; indexed access (i must be i32)
[buf-set $scratch $i $byte]            ; indexed mutation
[buf-len $scratch]                     ; compile-time constant => 256
[addr $scratch.[0]]                    ; returns [Ptr u8]                  (M3)
[buf-unchecked-get $scratch $i]        ; skips bounds check                 (M5)
[buf-unchecked-set $scratch $i $byte]  ; skips bounds check                 (M5)
extern i32 read {i32 [Ptr u8] u64}
[read 0 $scratch 256]                  ; implicit decay [Buf 256 u8] → [Ptr u8] (M3)
```

### Why `[Buf N T]` and not `[Buf T N]`

`N` is a value (integer literal), not a type, so the asymmetry from
`[Vec T]` / `[Map K V]` is honest. Multi-dim ordering matches C's
row-major declaration:

```
C:                     int arr[3][4];
JACL:                  [Buf 3 [Buf 4 i32]]
```

Reads naturally ("buffer of 3 buffers of 4 i32s"). The `[Buf T N]`
ordering would force inside-out reading.

## Type encoding

A buf type carries three pieces of state on its AST node:

| Field | Role |
|---|---|
| `inferred_type` | `TYPE_BUF` |
| `inferred_struct_idx` | element type — scalar via `JACL_SCALAR_TYPE_IDX`, real struct registry idx for value-struct elements, or another buf's encoded slot for nested buffers |
| `inferred_buf_len` | `N` — new `uint32_t` field added to `AstNode` for this feature |

Choice rationale: explicit length field (Option A from the design
discussion) keeps the existing two-slot annotation scheme clean and
keeps the typer's "key" field semantically honest. Costs 4 bytes per
AST node.

## Element-type rules

Any **fixed-width** type may be an element:

- Scalars (`i8`/`i16`/`i32`/`i64`/`u8`/`u16`/`u32`/`u64`/`f32`/`f64`/`bool`)
- `[Ptr T]` (8 bytes, raw machine pointer, not GC-traced)
- Value-type structs (`is_value_type` true; inline byte layout)
- Nested `[Buf M U]`
- GC-traced reference types: `dyn`, `[Vec T]`, `[Map K V]`, closures,
  strings — all 8-byte tagged values

### Small int types (u8/i8/u16/i16)

These are **static-only** types with no NaN-box representation. They
are valid as `[Buf N T]` elements and struct fields; at any boundary
that would cross into `dyn`, the value widens to `i32` (signed) /
`u32` (unsigned). Standalone-local form (`def u8 x 5`) is reserved
for a later milestone — the typer currently routes it through the
generic typed-binding path and the compiler will reject it.

Disallowed: nothing currently — every JACL value is fixed-width on the
stack/heap-cell. The distinction that *does* matter is **ABI
compatibility** (see decay rules).

## Decay rules

`[Buf N T]` decays to `[Ptr T]` implicitly at:
- extern call sites where the parameter is `[Ptr T]`
- JACL proc call sites where the parameter is declared `[Ptr T]`
- `[addr $buf.[i]]` (always; produces a pointer to element `i`)

Decay is gated on `T` being **C-ABI compatible**:
- Allowed: scalars, `[Ptr U]`, value-struct
- Rejected at the typer: `dyn`, `[Vec T]`, `[Map K V]`, closures,
  strings — these are JACL-tagged 8-byte values, not anything C
  understands. Error message points at the explicit `[addr]` form for
  runtime-internal use (e.g. passing tagged-value buffers to JACL's
  own C runtime).

The reverse direction (`[Ptr T]` → `[Buf N T]`) requires
`[ptr-cast [Buf N T] $p]`, matching today's pointer cast story.

## Storage

| Where | Layout |
|---|---|
| Local | `N * sizeof(T)` contiguous bytes in the frame; new opcodes resolve `base + i * sizeof(T)`. |
| Struct field | `N * sizeof(T)` inline bytes in the struct body; extends `STRUCT_DESIGN.md`'s field layout. |
| Heap | Not first-class. Use `[Ptr [Buf N T]]` (pointer to heap-allocated region) or explicit boxing. |

Zero-init on declaration:
- Scalars: all-zero bytes
- `[Ptr T]`: null
- Value-struct: per-field zero-init (recursive)
- Nested buf: recursive zero-init
- GC-traced ref types: `NIL`

## Bounds checks

On by default. Constant index → fold at compile time (out-of-range is
a type error). Runtime index → compile to a compare-and-trap.

Escape hatch: `[buf-unchecked-get $buf $i]` / `[buf-unchecked-set $buf $i $v]`.
Named to make audit grep-friendly.

## Literal initialization

`def [Buf N T] x [[Buf N T] v0 v1 ... vk]` initializes a buffer with
a literal sequence of values. Matches the surface of typed-vec
constructors (`[[Vec i64] 10 20 30]`).

- The constructor type `[[Buf N T] ...]` must match the LHS
  annotation exactly (same `N`, same `T`).
- **Partial fill is allowed**: if `k < N`, the supplied values land
  at indices `0..k-1` and the remaining slots stay zero-init. Useful
  for "magic bytes at the start of a header" patterns.
- `k > N` is a compile-time error.
- Each value is checked against `T` at compile time; small ints are
  narrowed via C-cast semantics (truncate without overflow trap).
  **Deferred (M5)**: constant-overflow check on literal values, e.g.
  `[[Buf 4 u8] 256]` should warn or error at compile time since 256
  doesn't fit in u8.

## GC integration

When `T` is a GC-traced reference type, the buffer's inline slots must
be walked by the collector. The collector learns one new shape
descriptor: **"N contiguous tagged slots at offset X, stride S"** —
applicable in stack frames and struct field tables. Indexed stores
into traced-element bufs go through the same write barrier as struct
field stores.

No new GC subsystem; just an extension to the existing struct/frame
walker.

## Errors

Surfaced via new `jacl_format_buf_*` formatters in `src/type_error.c`.
Categories:

- Malformed annotation (`[Buf 0 T]`, non-literal `N`, unresolved `T`)
- Literal-init length mismatch (`[buf u8 [1 2 3]]` for a `[Buf 4 u8]`)
- Out-of-range constant index
- Element-type mismatch on indexed store
- Extern decay rejection (non-ABI-compatible element type)
- `[Buf N T]` ↔ `[Buf M T]` mismatch (different lengths are different
  types)
- Cross-element-type mismatch (`[Buf N i32]` vs `[Buf N u32]`)

## Implementation order

Each milestone is independently shippable and testable.

### M1 — Type plumbing (no runtime behavior)

- `ast.c`: add `TYPE_BUF` to `JaclType`; add `uint32_t inferred_buf_len` to `AstNode`.
- `parser.c`: recognize `[Buf N T]` in type-annotation positions
  (`def` / `mut` / `set` / proc params / returns / struct fields).
- `typer.c`: type-resolver case. Validate `N` positive literal, `T`
  resolves; populate the three annotation fields. Restrict `T` to
  scalars initially (broaden in M4).
- `type_error.c`: `jacl_format_buf_*` skeletons.
- Tests: `test/test_typer.c` for annotation shapes;
  `test/test_type_errors.c` for malformed cases.

**Exit:** `def [Buf 256 u8] x` parses and types but does not compile.

### M2 — Scalar buffer MVP (locals only)

- `bytecode.c`: opcodes for typed buffer get/set and `OP_BUF_LEN`.
- `compiler.c`: local-frame layout extension reserving
  `N * sizeof(T)` per buffer local. Bounds-check codegen
  (compile-time fold for literals, runtime compare-and-trap
  otherwise).
- Zero-init on declaration.
- Literal init: `[buf T [vals...]]` with typer length-check.
- `[buf-len $x]` compile-time fold.
- Tests: `test/jacl/buffers/` golden-path fixtures.

**Exit:** can declare, init, index, mutate, and read length of a
scalar buffer local.

### M3 — C ABI interop  ✅

- `[addr $buf->N]` → `[Ptr T]` via OP_BUF_ADDR_LOCAL (constant index;
  runtime-index addr-of deferred).
- `$buf->N` arrow indexing as a value read (mirrors `[buf-get $buf N]`).
- Typer: extern decay rule, gated on pointee match. Compiler emits
  OP_BUF_ADDR_LOCAL for bare var-ref-to-buf args when param is `[Ptr T]`.
- `[Ptr u8]` / small-int pointer types now work end-to-end through
  OP_PTR_LOAD / OP_PTR_STORE (u8/i8/u16/i16 cases added).
- **Deferred to M4 / later** (out of scope for FFI motivation):
  - `[ptr-cast [Buf N T] $p]` — bufs aren't first-class values, no
    natural rvalue destination. Casts go through `[Ptr T]` instead.
  - End-to-end fixture calling a real C extern — requires test
    harness to register a native fn. The toolkit is fully exercised
    via `buf_ptr_round_trip.jacl` using `[addr]`/`[ptr-offset]`/
    `[ptr-deref]`, which are the same primitives an extern would use.

**Exit:** the buf+pointer toolkit a C function would use round-trips
cleanly (verified by `test/jacl/buf_ptr_round_trip.jacl`).

### M4 — Element-type expansion

- Value-struct elements (real struct registry idx, struct-byte-size
  stride).
- Nested buffers (multi-dim indexing).
- Buffer-typed struct fields (extends `STRUCT_DESIGN.md`).
- GC-traced element types: new shape descriptor, write-barrier
  wiring on indexed stores, `NIL` zero-init for ref-typed slots.
- Tests: per element-kind; one GC stress fixture retaining references
  only through buffers.

**Exit:** all element types work in locals and struct fields.

### M5 — Polish

- `[buf-unchecked-get]` / `[buf-unchecked-set]` escape hatches.
- Print formatting (e.g. `Buf<u8, 256>`).
- **Compile-time literal-value overflow check**: reject literal
  initializers whose constant values don't fit in `T` (e.g.
  `[[Buf 4 u8] 256]`). Today the VM truncates via C-cast
  semantics; users only learn about the overflow at runtime when
  the stored byte differs from what they wrote.
- Documentation: update `TYPE_SYSTEM.md`, `SYNTAX.md`,
  `STRUCT_DESIGN.md`; remove buffer items from `NOT_IMPLEMENTED.md`.

## Risk concentration

- **M4 GC walker extension**: touches concurrent collector code paths;
  land as a focused commit with stress fixtures.
- **M4 struct field layout**: ripples into struct field-offset
  computation; check carefully.
- M1–M3 are mechanical and low-risk.

## Open questions / deferred

- First-class heap buffers (`new [Buf N T]` returning
  `[Ptr [Buf N T]]`) — deferred; current escape is manual via
  `malloc` + `[ptr-cast]`.
- Slicing (`[buf-slice $b 4 12]` → `[Buf 8 T]`) — deferred until
  there's a concrete need.
- Constant-`N` polymorphic procs (`proc f [[Buf N u8]] {...}` where
  `N` is bound) — deferred; for now `N` must be a literal at each
  use site.

### LHS type inference for typed RHS constructors (language-wide)

Today `def [Buf 8 i32] hdr [[Buf 8 i32] 1024 2048]` repeats
`[Buf 8 i32]` on both sides. The RHS constructor already carries
the full type — the LHS annotation is pure noise when the RHS is
syntactically a typed constructor.

Reduce to:

```
def hdr [[Buf 8 i32] 1024 2048]   ; type inferred from RHS
```

This is **not buffer-specific** — the same redundancy exists for
every statically-typed RHS form already in the language:

```
def xs [[Vec i64] 1 2 3]           ; today repeats [Vec i64]
def m  [[Map str i32] "a" 1 "b" 2] ; today repeats [Map str i32]
def f  [spawn { …i32… }]           ; could infer [Future i32]
def p  [ptr-null [Ptr Point]]      ; could infer [Ptr Point]
def b  [box-i32 0]                 ; could infer [Box i32]
```

Recommended approach when this lands: a typer pass that, when `def
NAME RHS` is encountered without an LHS annotation, takes the
RHS's inferred_type / inferred_struct_idx / inferred_buf_len /
inferred_key_struct_idx and adopts them onto the binding —
provided the RHS is a *statically known* typed form (a recognized
typed-constructor head, an extern call with a typed return, a
typed proc call, etc.). The dyn-binding form `def dyn x …` stays
the explicit "I want a dyn slot" marker (per `TYPE_SYSTEM.md`
decision 2).

This is one of those "do once, applies everywhere" wins — worth
landing as a separate, focused milestone after M3/M4 so it's not
entangled with the buffer feature delivery.
