# JACL Buffer Type Design

`[Buf N T]` is a compile-time-sized, contiguous, ABI-flat array of `T`.
It fills the gap between `[Ptr T]` (single element, raw) and `[Vec T]`
(GC-owned, header-bearing, growable) — the "block of N T's, laid out
like C's `T[N]`" case that FFI needs.

Cross-references: `TYPE_SYSTEM.md` (typer pipeline & invariants),
`STRUCT_DESIGN.md` (struct field layout buffers extend),
`NOT_IMPLEMENTED.md` §4 (typer deferred work — remove buffer items
when M4 lands).

## Status snapshot (for session handoff)

| Milestone | Status | Commits |
|---|---|---|
| **M1** Type plumbing | ✅ done | `a9b92fc`, `c2802eb` (small ints) |
| **M2** Scalar buffer MVP | ✅ done | `caff29f`, `79959e6`, `73e6b5e` |
| **M3** C-ABI interop (extern decay, addr) | ✅ done | `00e7f18`, `35fabcf`, `aa91b0b`, `aac8f23` |
| **M3.7** Pointer-arrow indexing (`$p->N`) | ✅ done | `fcd68eb` |
| **M4.1** Value-struct buf elements | ✅ done | `c0059af`, `175b372`, `d7298f6` |
| **M4.2** Nested buffers `[Buf 3 [Buf 4 i32]]` | ✅ done (read+write+literal-init+struct-inner; deeper nesting deferred) | `ddc7163` + `fcd4c48` + this |
| **M4.3** Buf-typed struct fields | ✅ done | `56d8a4d`, `c22ecde` |
| **M4.4** GC-traced element types | ✅ done (flat locals; struct-field + nested ref-elem deferred) | `26fa6b8` |
| **M4.4 ext** Typed-collection elements `[Buf N [Vec T]]` | ✅ done | `0cf93f5` |
| **M4.4 ext2** `[Buf N [Map K V]]` via shared type-shape registry | ✅ done | `c1e9390` |
| **M4.4 ext3** `[Buf N [Ptr T]]` via Phase 5 registry | ✅ done | `a783668` |
| **M4.4 ext4** `[Buf N [Future T]]` | ✅ done | `eed8107` |
| **M4.4 ext5** Nested-buf depth lift (`[Buf N1 [Buf N2 [Buf N3 T]]]`) | ✅ declaration / `[buf-len]` / `[addr]` work at any depth; arrow chain still depth-2 only | this |
| **M5** Polish (unchecked ops, print, overflow check, docs) | ✅ done | `da5a953` |
| **M5b** LHS type inference for typed-ctor RHS | ✅ done | `0e4ba05`, `3dca5cb` |
| **M5c** Arrow indexing on non-var-ref TYPE_PTR receivers | ✅ done | `a2c81c9` |
| **M5d** `[addr]` on chained-arrow buf-field leaf | ✅ done | `9992ae7` |

What works end-to-end today:

```jacl
struct ElfHeader { i32 version, [Buf 4 u8] magic }
proc main {} {
  def h [ElfHeader 1]              ; buf field zero-init implicitly
  def [Ptr u8] m $h->magic         ; field access returns [Ptr T]
  set $m->0 0x7f
  set $m->1 0x45
  print $m->0                      ; 127
  print $h->magic->1               ; chained-arrow: 0x45 = 69
  print $h->version                ; 1

  def [Buf 256 u8] scratch                              ; zero-init local
  def [Buf 4 u8] magic [[Buf 4 u8] 0x7f 0x45 0x4c 0x46] ; literal init
  def hdr [[Buf 8 i32] 1024 2048]                       ; LHS inferred from RHS
  [buf-set $scratch 0 0xff]                             ; bounds-checked set
  [buf-unchecked-set $scratch 1 0xfe]                   ; bounds-check elided
  print [buf-len $scratch]                              ; 256

  extern i32 sys_read {i32 fd [Ptr u8] dst u64 len}
  [sys_read 0 $scratch 256]                             ; implicit [Buf]→[Ptr] decay

  ; Nested bufs (M4.2): 2D matrix, scalar + struct inner element.
  def [Buf 3 [Buf 4 i32]] grid
  set $grid->1->2 42
  print $grid->1->2                                     ; 42

  struct Point {i32 x, i32 y}
  def [Buf 3 [Buf 4 Point]] points
  set $points->0->0 [Point 7 11]
  print $points->0->0->x                                ; 7

  ; Ref-element bufs (M4.4): tagged JaclVal slots traced by the GC.
  ; Supported element keywords: dyn, str, vec, map, stream.
  def [Buf 4 dyn] tagged
  [buf-set $tagged 0 "hello"]
  [buf-set $tagged 1 [vec 1 2 3]]
  print [buf-get $tagged 0]                             ; hello
  def [Buf 3 dyn] mixed [[Buf 3 dyn] "a" 42 nil]        ; literal init OK
  print [buf-get $mixed 1]                              ; 42
}
```

What's deferred (the next session can pick from these):

1. **M4.4 follow-ups** — the flat-local form is done; remaining slices:

   - **Ref-elem bufs as struct fields** (rejected with a clear error
     today): `struct Bag {i32 size, [Buf 4 dyn] items}` needs the
     struct walker to descend into embedded tagged-slot ranges. Plumb
     either a per-field shape descriptor (offset/N/elem) or extend the
     struct's `field_inline_bitmap` to distinguish "raw bytes" from
     "N tagged slots". `compiler.c:13718` (struct field parse) is where
     the rejection lives; lift it once the walker handles the new shape.
   - **Nested ref-elem bufs** `[Buf N [Buf M dyn]]` — currently
     rejected in `compiler.c:HEAD_DEF` buf branch. Same shape problem
     scaled up; defer until there's a concrete need.
   - ~~`[Buf N [Map K V]]`~~ — landed in TYPE_REGISTRY_REFACTOR.md
     Phase 3+4 via a shared typed-shape registry (commit `c1e9390`).
     Both K and V ride on one shape entry; no aux fields needed.
   - ~~Strict typed-vec value check on `buf-set`~~ — closed via the
     typer's new `typer__check_buf_set_value` helper. `[buf-set]` /
     `[buf-unchecked-set]` / `set $b->i v` all reject mismatched
     value types at compile time. See fixtures
     `buf_set_value_type_mismatch.jacl`,
     `buf_arrow_set_value_type_mismatch.jacl`,
     `buf_set_typed_vec_mismatch.jacl`.

   What landed in M4.4 (flat-local form):

   - **Typer**: `typer__buf_type_full` already encoded ref keywords
     (`dyn`/`str`/`vec`/`map`/`stream`) via `JACL_SCALAR_TYPE_IDX`;
     the existing buf-get / arrow-int narrowing returns the original
     keyword type. Implicit `[Buf N T]` → `[Ptr T]` decay at call
     sites is rejected for ref T with a message pointing at the
     explicit `[addr]` escape hatch (see `src/typer.c` ~2440).
   - **Compiler** (`src/compiler.c` HEAD_DEF buf branch): for ref
     elements, skip `OP_BUF_ZERO_LOCAL`. The prior OP_NIL pushes
     leave each slot at `JACL_NIL` (the all-zeros tag) and leaving
     the `inline_slot_bitmap` clear means the existing stack walker
     scans the slots as ordinary tagged JaclVals -- no new GC
     machinery. Stride is `sizeof(JaclVal) = 8` via
     `struct__type_size`.
   - **VM** (`src/vm.c` OP_BUF_GET/SET_LOCAL + unchecked variants):
     added a tagged-ref case set that loads/stores `vm->stack[base +
     idx]` directly. The store path calls `gc_write_barrier` on the
     old→new value so the concurrent marker observes both halves of
     the swap.
   - **GC**: no new walker code. Ref-elem slots are normal stack
     slots that the existing `gc_mark` loop already scans.
   - **Test fixtures**: `test/jacl/buf_traced_dyn.jacl`,
     `buf_traced_vec.jacl`, `buf_traced_literal_init.jacl`,
     `buf_traced_dyn_gc.jacl` (GC churn), `buf_traced_decay_reject.jacl`
     (extern decay error), `buf_traced_struct_field_reject.jacl`
     (struct-field error). Full chaos suite (`chaos_grey_buf`,
     `chaos_satb_deref`, `chaos_soak`) still passes.

   Original pickup notes (kept for the follow-up slices):

   - **Typer** (`src/typer.c`, `typer__buf_type_full` ~393): drop the
     scalar-only check on the inner element when the element type
     keyword resolves to a tagged-ref type (`dyn`, `str`, `vec`,
     `map`, `closure`) or to a typed collection.
   - **Compiler** (`src/compiler.c` HEAD_DEF buf branch ~7040):
     element-size for tagged types is `sizeof(JaclVal)` = 8 bytes.
     Stride math already works; the new piece is **NIL zero-init**
     (the OP_BUF_ZERO_LOCAL memset of zero bytes happens to produce
     `JACL_NIL` for the current tagged encoding — verify this still
     holds, since `JACL_NIL` is a specific NaN-boxing bit pattern
     and changing the encoding would break this).
   - **VM** (`src/vm.c` OP_BUF_GET_LOCAL/SET_LOCAL ~3024 + ~3112):
     add cases for the tagged-ref element types — load/store a
     full `JaclVal` slot. The store path must call the write
     barrier (`gc_write_barrier(...)`) so the concurrent collector
     sees the new reference.
   - **GC** (`src/gc.c` / `src/gc_collect.c`): extend the stack-
     frame walker to recognise buf locals whose element type is a
     tagged ref, then mark each of the N slots. Two options:
     (a) **Shape descriptor**: record `(base_slot, N, element_type)`
         somewhere the marker can find it (likely a new table on
         the call frame, populated by the compiler at def time).
     (b) **Bitmap extension**: extend `vm->inline_slot_bitmap` to
         distinguish "raw inline bytes" (current usage for struct
         locals) from "N tagged slots" (new for ref-elem bufs).
     Pick one and stick to it; (a) is more honest but more
     plumbing, (b) reuses infrastructure but conflates concepts.
   - **Struct-field buf** (M4.3 path, same files): the struct
     registry's `StructTypeField.type` for a buf field needs to
     carry the element type too, so the struct-tracing walker can
     descend into ref-elem buf fields.
   - **Test fixtures**:
     - `test/jacl/buf_traced_dyn.jacl` — `[Buf 4 dyn]` storing
       strings and vectors; force a GC mid-walk and verify the
       elements are still reachable.
     - `test/jacl/buf_traced_vec.jacl` — `[Buf 3 [Vec i64]]`
       holding typed vecs.
     - A chaos-style stress test under `test/test_chaos_*.c` that
       allocates many ref-elem bufs, mutates them while GC runs,
       and verifies the heap stays consistent.

   **Why this is the highest-risk slice**: the concurrent collector
   has invariants around what's traced and how write barriers are
   emitted. Indexed stores into a tagged-elem buf cross a barrier
   the existing struct-store machinery never has to (struct fields
   are statically known offsets; buf elements are runtime indices).
   Land in small, tested commits — typer + compiler first (NIL
   zero-init and store with barrier), then the GC walker, then the
   stress fixtures. Run `test/test_chaos_*.c` aggressively between
   each commit.
2. **M4.2 deeper nesting** (`[Buf 2 [Buf 3 [Buf 4 i32]]]`): needs
   the encoding to grow from a single `inner_buf_len` to a variable-
   length dimension list (or a recursive synthetic-struct encoding).
   Touches typer, compiler, error rendering, arrow chain. The scalar
   and struct-inner forms at depth 2 work today; depth-N support is
   a separate design slice.
3. **Language-wide LHS type inference** (see "Open questions / deferred"
   below) — independent of bufs; reduces
   `def [Buf 8 i32] hdr [[Buf 8 i32] 1024 2048]` to
   `def hdr [[Buf 8 i32] 1024 2048]` and similar across typed vecs,
   maps, futures, pointers, boxes.
4. **Receiver-shape generalization** — partially shipped (M5c / M5d).
   `$h->magic->0` (and the `set` form), plus `[addr $h->magic->0]`,
   now work when the first arrow returns a `[Ptr T]` from a struct-
   field access — the typer + compiler accept any TYPE_PTR-typed
   receiver (not just bare local var-refs) for arrow-int indexing
   and for [addr] leaves. Codegen dispatches through OP_PTR_LOAD /
   OP_PTR_STORE / OP_PTR_ADD_OFFSET on the typer-recovered
   un-widened pointee. Still deferred: `[buf-get $h->field $i]` /
   `[buf-set $h->field $i $v]` builtin-head forms (use the arrow
   syntax instead) and `[Buf N T]` proc parameter syntax.
5. **Test harness native-fn registration** — would unblock a real C
   extern fixture (M3.6) and stress-test the decay path with an actual
   syscall.

The design decisions that landed (and *why*) — useful when reading the
code:

- **C-matching pointer-decay semantics** at call boundaries (both
  extern and JACL-to-JACL). The user explicitly confirmed this matches
  C arrays' actual behavior (`void foo(int arr[10])` is `int*`). See
  the discussion before `aac8f23`.
- **Small ints (u8/i8/u16/i16) are static-only** — no NaN-box rep.
  Valid only as buf elements and struct fields. Widen to i32 at any
  dyn boundary. See `c2802eb`.
- **`[Buf N T]` (count first, then type)** — chosen over `[Buf T N]`
  for multi-dim readability (`[Buf 3 [Buf 4 i32]]` matches C's
  row-major `int arr[3][4]`).
- **Struct constructors skip buf fields** (auto-zero-init). The user
  passes args only for non-buf fields. M4.3a / `56d8a4d`.
- **Buf fields produce `[Ptr T]` on access** (`$h->magic` →
  `[Ptr u8]`) rather than buf-typed handles. Composes cleanly with
  existing ptr-arrow machinery.

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

### M1 — Type plumbing (no runtime behavior)  ✅

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

### M2 — Scalar buffer MVP (locals only)  ✅

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

### M4 — Element-type expansion  (partial: M4.1 + M4.3 ✅, M4.2 + M4.4 deferred)

- Value-struct elements (real struct registry idx, struct-byte-size
  stride).
- Nested buffers (multi-dim indexing).
- Buffer-typed struct fields (extends `STRUCT_DESIGN.md`).
- GC-traced element types: new shape descriptor, write-barrier
  wiring on indexed stores, `NIL` zero-init for ref-typed slots.
- Tests: per element-kind; one GC stress fixture retaining references
  only through buffers.

**Exit:** all element types work in locals and struct fields.

### M5 — Polish  ✅

- `[buf-unchecked-get]` / `[buf-unchecked-set]` escape hatches
  (new `OP_BUF_UGET_LOCAL` / `OP_BUF_USET_LOCAL` opcodes; bounds
  check elided).
- **Error-message rendering**: bufs print in source syntax
  (`[Buf 4 u8]`, `[Buf 10 Point]`) rather than the bare type name
  `buf`, so the diagnostic copy-pastes back into code. Applies to
  both compile-time bounds errors (typer + compiler) and runtime
  OOB messages (VM). Helper: `jacl_format_buf_type` in
  `src/type_error.c`.
- **Compile-time literal-value overflow check**: rejects literal
  initializers whose constant values don't fit in `T` (e.g.
  `[[Buf 4 u8] 256]` now errors with
  `"element 2 value 256 out of range for u8"` at compile time
  instead of silently truncating to `0`). Handles direct
  `AST_LIT_INT` and unary-minus shape `(- LIT_INT)`. Fixtures:
  `test/jacl/buf_literal_value_overflow.jacl`,
  `buf_literal_value_negative.jacl`.
- Documentation: `TYPE_SYSTEM.md` (new "Buffer types" section),
  `SYNTAX.md` (new `[Buf N T]` section + status row),
  `STRUCT_DESIGN.md` (buf as struct field type, small-int field
  note).

**Exit:** the buffer feature has parity with the other typed
collections for error messaging and constant-time safety; escape
hatches are available for performance-critical paths.

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

### LHS type inference for typed RHS constructors (shipped)

`def NAME RHS` now infers the LHS type from a typed RHS constructor
for every typed-binding shape the language carries. The LHS
annotation is dropped because the RHS already carries the full type
(commits `0e4ba05` bufs, `<this>` rest).

```
def hdr   [[Buf 8 i32] 1024 2048]   ; was: def [Buf 8 i32] hdr ...
def xs    [[Vec i64] 1 2 3]         ; was: def [Vec i64] xs ...
def m     [[Map i64 i64] 1 100]     ; was: def [Map i64 i64] m ...
def p     [ptr-null [Ptr Point]]    ; was: def [Ptr Point] p ...
def b     [box 0]                   ; binding inherits TYPE_BOX
def f     [spawn {+ 40 2}]          ; binding inherits TYPE_FUTURE
```

Implementation:

- **Typer** (`handle_def_or_mut`): TYPE_BUF and TYPE_PTR added to the
  inherit-from-RHS list (alongside the pre-existing unboxed-scalar /
  struct / stream / typed-collection / future / box cases). TYPE_BUF
  also inherits `inferred_buf_len` so downstream `buf-len` and
  bounds-checking see the correct N. Typed-map already inherited
  `inferred_key_struct_idx`.
- **Compiler** (`HEAD_DEF`):
  - *Bufs*: a pre-process step rewrites `def NAME [[Buf N T] ...]`
    into the canonical 3-arg `def [Buf N T] NAME RHS` shape using a
    stack-local AstNode array, so the existing multi-slot allocation
    / zero-init / per-element store path handles it unchanged.
  - *Other typed forms*: the generic def-handler's `effective_type`
    fallback already inherited unboxed scalars / struct / stream /
    typed collections from `rhs_type`; extended to also inherit
    TYPE_PTR / TYPE_BOX / TYPE_FUTURE so the local's tracked type
    matches what the typer assigned.
- Fixtures: `test/jacl/buf_lhs_inference.jacl` (buf scalar + struct
  elem + partial fill), `def_lhs_inference.jacl` (vec / map / ptr /
  box / future), `def_lhs_vec_narrowing.jacl` (typed-vec element-type
  enforcement on the inferred binding).

The dyn-binding form `def dyn x …` remains the explicit "I want a
dyn slot" marker (per `TYPE_SYSTEM.md` decision 2). Tagged scalars
(i32 / u32 / f32 / bool) intentionally still collapse to DYN when
inferred from a bare literal — `def x 5` keeps its historical dyn-
by-default behavior. Only structurally typed RHS forms participate
in the inference.
