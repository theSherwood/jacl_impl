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
| **M4.4 ext5** Nested-buf depth lift (`[Buf N1 [Buf N2 [Buf N3 T]]]`) | ✅ declaration / `[buf-len]` / `[addr]` work at any depth | `0ea5bff` |
| **M4.4 ext6** Depth-3+ arrow chains (`$cube->i->j->k`, read + set) | ✅ done | this session |
| **M4.4 ext7** Decomposed chains (`def p $cube->1; $p->2->3`) | ✅ done | this session |
| **M4.4 ext8** Dynamic arrow indices (`$cube->$i->$j->$k`) | ✅ done | this session |
| **M4.4 ext9** Slice passing via `[Ptr [Buf N T]]` proc params | ✅ done | this session |
| **M4.4 ext10** Depth-3+ literal init | ✅ done (scalar leaf) | this session |
| **M4.4 ext11** Runtime bounds checks for dynamic arrow indices | ✅ done (`OP_PTR_OFFSET_CHECKED`) | this session |
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

## Session pickup — open work

Cross-reference: `docs/TYPE_REGISTRY_REFACTOR.md` carries the full
type-shape-registry history; this section lists what's still **open**
for the buffer feature.

**Tier 1 — finishing the nested-buf depth lift (Phase 5b follow-up).**
The depth cap was lifted at *declaration* time (commit `0ea5bff`).
This session closed out the major access-side items: depth-3+ arrow
chains (read + set), decomposed chains, dynamic indices, and slice
passing. See `docs/TYPE_REGISTRY_REFACTOR.md` for the architecture
notes and the fixture list.

Remaining:

1. **Ref-elem bufs as struct fields** (`struct Bag {i32 size, [Buf 4
   dyn] items}`). Today rejected at `compiler.c` with a clear error.
   The struct-tracing walker (`gc_collect.c`) needs to descend into
   the embedded N-tagged-slot range. Two implementation choices
   sketched in `docs/TYPE_REGISTRY_REFACTOR.md` — extend
   `StructTypeField.type` with a kind discriminator, or attach a per-
   field shape pointer. Either way the field's GC trace becomes
   "scan N tagged slots starting at field offset".

2. **Depth-3+ literal init with struct leaves**. Scalar leaves work
   (`[[Buf 2 [Buf 3 [Buf 4 i32]]] ...]`); a struct leaf path would
   need a flat-index OP_BUF_USET_STRUCT_LOCAL-style store. Rejected
   at compile time today with a clear error.

**Tier 2 — nice-to-haves, not blocking.**

- **`[Box T]` annotation surface**. `TYPE_BOX` exists at runtime
  (created via `[box $val]`) but `[Box T]` isn't a recognized
  annotation — no parser entry. Adding it would let `[Buf N [Box T]]`
  ride the existing Phase 5 `TYPE_SHAPE_BOX` kind via a one-line
  recognizer extension. Mechanical.
- **`[buf-get $h->field $i]` builtin-head forms** for buf-typed struct
  fields (currently arrow syntax only). M5d only shipped the arrow
  path.
- **`[Buf N T]` proc parameter syntax**. Bufs decay to `[Ptr T]` at
  call boundaries; declaring a param as `[Buf N T]` directly is
  rejected today. The new `[Ptr [Buf N T]]` slice-pass annotation
  covers the common "pass a buf row to a proc" use case (see
  `docs/TYPE_REGISTRY_REFACTOR.md`). Direct `[Buf N T]` params (with
  whole-buf semantics, no decay) are still open if needed.
- **Test harness native-fn registration** — unblocks a real C extern
  fixture (M3.6) and stress-tests the decay path with an actual
  syscall.

**Tier 3 — orthogonal cleanups.**

- **Language-wide LHS type inference** for typed-ctor RHS already
  landed (M5b). No remaining buffer-specific work here.
- **Receiver-shape generalization** — substantively shipped. M5c / M5d
  covered struct-field-derived `[Ptr T]` chains. M4.4 ext6..ext9 cover
  the nested-buf chain receiver matrix (depth-3+ chains, decomposed
  chains, dynamic indices, slice passing) -- see the chain helper
  architecture documented in `docs/TYPE_REGISTRY_REFACTOR.md`.
  Builtin-head forms (`[buf-get $h->field $i]`) and direct
  whole-buf-value `[Buf N T]` proc-param syntax remain in Tier 2.

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
