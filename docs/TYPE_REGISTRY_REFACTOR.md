# Type-Shape Registry Refactor

## Why this exists

Today's type-idx encoding is a two-headed beast:

- A **struct registry** (`StructTypeRegistry` in `src/compiler.c`) holds
  named struct layouts. Entries are `StructTypeDef`s with field lists
  and C-ABI offsets.
- A **scalar sentinel range** carved out of the same 32-bit idx space
  (`0xFF00..0xFFFF` via `JACL_SCALAR_TYPE_IDX`) lets the same field
  encode "this is a primitive JaclType" without growing the registry.

Parametric types (`[Vec T]`, `[Map K V]`, `[Buf N T]`, `[Ptr T]`,
futures, boxes) are **not registered**. Their parameters ride in
parallel aux fields on AST nodes / bindings / locals:
`inferred_struct_idx` for the value/element type, `inferred_key_struct_idx`
for typed-map keys, the buf-binding's `buf_inner_len` for nested length,
and (since M4.4 ext) the buf-binding's `key_struct_idx` repurposed for
typed-vec inner T.

This was fine when typed collections only appeared one level deep at
each use site — every reader decodes `(sentinel, aux)` in place. It
falls over the moment we try to nest: `[Buf N [Map K V]]` needs to
remember both K and V on the buf binding, and there's no second aux
slot. M4.4 ext used the only one for typed-vec.

The aux-field shortcut buys one feature and pushes the same encoding
problem one nesting level further out. This refactor closes the door
on the class.

Cross-references: `BUFFER_DESIGN.md` (M4.4 ext follow-ups),
`TYPE_SYSTEM.md` (typer pipeline + invariants).

## Goal

Replace the two-pronged encoding with a single **TypeShape registry**.
Every parametric or named type becomes a registry entry; every code
site that needs to talk about a type stores one 32-bit idx pointing
into it. Aux fields disappear from AST nodes, bindings, and locals.

## Non-goals

- Not changing the runtime layout of any value. Typed-vec / typed-map
  stay heap headers with their existing GC tracing.
- Not changing the user surface — `[Vec i64]`, `[Map str Point]`,
  `[Buf N T]` still parse the same.
- Not unifying every parametric type at once. `[Vec T]` and `[Map K V]`
  are the immediate targets; `[Buf N T]`, `[Ptr T]`, future, box are
  Phase 5 (optional).

## Target shape

```c
typedef enum {
  TYPE_SHAPE_STRUCT,        // user-declared struct (current StructTypeDef)
  TYPE_SHAPE_CTX,           // reserved slot 1 (current)
  TYPE_SHAPE_TYPED_VEC,     // [Vec T] -- one inner idx
  TYPE_SHAPE_TYPED_MAP,     // [Map K V] -- two inner idxes
  // Phase 5 candidates: BUF, PTR, FUTURE, BOX
} TypeShapeKind;

typedef struct {
  TypeShapeKind kind;
  union {
    StructTypeDef* struct_def;          // STRUCT, CTX
    struct { uint32_t elem_idx; } tvec; // TYPED_VEC
    struct { uint32_t key_idx;
             uint32_t value_idx; } tmap; // TYPED_MAP
  };
} TypeShape;

typedef struct {
  TypeShape* shapes;
  uint32_t   count, capacity;
  arena_t*   arena;
  // Intern hash-set keyed on (kind, args) so identical parametric
  // shapes share an idx. Without interning, every textual occurrence
  // of [Vec i64] becomes a new entry and equality checks regress to
  // structural compare.
  ...intern table...
} TypeShapeRegistry;
```

The idx encoding stays compatible:

| Range | Meaning |
|---|---|
| `0..0xFEFF` | TypeShape registry idx (any kind) |
| `0xFF00..0xFFFF` | Scalar sentinel for primitive / plain-ref keyword |

What changes is the *meaning* of an idx in the registry range. Today
it's always a struct shape; after the refactor it can be any kind, and
readers consult `type_shape_kind(reg, idx)` before interpreting.

## Migration phases

Each phase is independently shippable and leaves the suite green.

### Phase 0 -- Audit (no code change)

Grep every reader of these fields and classify:

- `struct_type_idx` (compiler `Local`)
- `struct_idx` (typer `TyperBinding`)
- `inferred_struct_idx`, `inferred_key_struct_idx` (AST `AstNode`)
- `key_struct_idx` (typer + compiler local)

Each reader falls into one of:
- **(a) treats idx as struct shape** -- needs a kind check post-refactor.
- **(b) treats idx as opaque** -- no-op.
- **(c) treats idx as scalar sentinel** -- decodes via
  `JACL_IS_SCALAR_TYPE_IDX`; unaffected.

Output: a checklist file (working notes, not committed) confirming the
Phase 1-4 sizing below. If the count is far off the estimate, revise
the plan before any code changes.

### Phase 1 -- Rename + kind tag (non-breaking)

- Rename `StructTypeRegistry` -> `TypeShapeRegistry`, `StructTypeDef`
  becomes the payload of `kind = STRUCT` entries. Existing entries get
  `kind = STRUCT` automatically.
- Add `type_shape_kind(reg, idx)` helper. Keep `struct_def_is_user` as
  a thin wrapper that returns `kind == STRUCT && idx != ctx_type_idx`.
- Update the registry alloc / grow / lookup paths to traffic in
  `TypeShape` instead of `StructTypeDef*`.

**Touches**: `src/compiler.c` registry definition + ~40 call sites
that read `reg->defs[idx]`. Mostly mechanical search-and-replace plus
an unwrap of `.struct_def`.

**Exit criterion**: full suite green (87/87), no behavior change.

### Phase 2 -- Intern typed-vec shapes

- Every site that creates a typed-vec annotation interns it via a new
  `type_shape_intern_typed_vec(reg, elem_idx)` helper. Repeat
  annotations of `[Vec i64]` share one idx.
- Typed-vec readers (vec-get narrowing, vec-set type check, M4.4 ext's
  `[Buf N [Vec T]]` path) consult the TypeShape entry for T instead of
  `inferred_key_struct_idx` / `binding.key_struct_idx`.
- After readers migrate, drop typed-vec usage of `key_struct_idx`. It
  stays alive for typed-map keys until Phase 3.

**Touches**:
- `typer.c`: AST_VAR_REF resolution; every HEAD_* case that narrows
  from a TYPE_TYPED_VEC.
- `compiler.c`: same set + the M4.4 ext buf-elem path.
- `vm.c`: typed-vec ops -- decode T via the registry rather than the
  inline u16 the ops carry today.

**Risk hotspot**: bytecode operands. `OP_TYPED_VEC_PUSH` and friends
carry the element-type idx inline (u16). Under the new scheme that idx
points into the TypeShape registry. Byte layout stays the same; only
the meaning of the bits changes.

**Adversarial test**: a bytecode disassembly of a representative test
should byte-equal between the pre-Phase-2 and post-Phase-2 build.

**Exit criterion**: full suite green; typed-vec narrowing flows through
the registry; `key_struct_idx`'s typed-vec usage is gone.

### Phase 3 -- Intern typed-map shapes

- Mirror Phase 2 for `[Map K V]`.
- Drop `inferred_key_struct_idx` entirely. Both K and V are read from
  the TypeShape entry.

This is the phase that unlocks `[Buf N [Map K V]]`: the buf binding
stores one registry idx (the typed-map shape), K and V fall out of it.

**Touches**: same files as Phase 2, plus the `inferred_key_struct_idx`
removal cascade in any header that exports it.

### Phase 4 -- Wire `[Buf N [Map K V]]`

Now trivial. `typer__buf_type_full` recognizes `[Map K V]` AST_COMMAND,
interns it, returns the registry idx. Compiler's HEAD_DEF buf path
stores it on the local's `struct_type_idx`. VM's existing TYPE_TYPED_MAP
load/store cases consult the registry. Add fixtures.

### Phase 5 -- More kinds (foundation done)

`TYPE_SHAPE_BUF`, `TYPE_SHAPE_PTR`, `TYPE_SHAPE_FUTURE`,
`TYPE_SHAPE_BOX` are all added to the registry as kinds with their
natural params:

- BUF: `(len, elem_idx)` -- two params
- PTR: `(pointee_idx)` -- one param
- FUTURE: `(resolves_to_idx)` -- one param
- BOX: `(boxes_idx)` -- one param

Each has an `intern` helper and a decoder case. Composition is now
*free at the registry level*: any inner `*_idx` can point at any
other registry shape (struct, typed-vec, typed-map, buf, ptr, future,
box) or a scalar sentinel.

What's foundation-only: the *recognizers* haven't been extended to
emit these for every annotation that could use them. Specifically:

- `typer__buf_type_full` recognizes `[Vec T]` and `[Map K V]` as buf
  elements; extending it to also recognize `[Buf M T]`, `[Ptr T]`,
  `[Future T]`, `[Box T]` is a separate (small) PR per use case.
- `[Vec T]` and `[Map K V]` annotation parsers accept scalar/struct
  T today; allowing parametric T (`[Vec [Buf N U]]`) is similarly a
  per-use-case extension.

Each such extension is now a one-line `type_shape_intern_*` call --
no schema growth, no aux fields, no encoding-out-of-slots problem.
That's the architectural payoff; specific compose cases get wired up
on demand.

### Phase 5a/5b -- Recognizer wiring (in progress)

- ✅ **`[Buf N [Ptr T]]`** -- `a783668`.
- ✅ **`[Buf N [Future T]]`** -- `eed8107`.
- ⚠ **`[Buf N [Box T]]`** -- shape kind ready, but `[Box T]` isn't a
  parser-recognized annotation today. Adding `[Box T]` would unblock
  this with a one-line recognizer extension.
- ✅ **Nested-buf depth lift via recursive `TYPE_SHAPE_BUF` interning**
  -- `0ea5bff`. **Partial**: declaration / `[buf-len]` / `[addr]` work
  at any depth; arrow chains beyond depth-2 still need codegen work
  (see "Pickup for next session" below).
- ❌ **`[Vec [Buf N T]]`, `[Map K [Buf N V]]`** -- typed-vec/map
  annotation parsers need to accept parametric T at element positions.
  Mechanical extension once a use case demands it.

## Pickup for next session

**Goal: close out the nested-buf depth lift.** Three deliverables, in
order of payoff:

### 1. Depth-3+ arrow chains (`$cube->i->j->k`)

The hard part. Current state: typer's HEAD_DOT for TYPE_BUF with a
nested inner returns `[Ptr T_leaf]` for depth-2, and `TYPE_DYN` for
depth-3+ (silent fallthrough, see `src/typer.c` HEAD_DOT). The
codegen for `$cube->i` decays via `OP_BUF_ADDR_LOCAL` with byte offset
`i * inner_byte_size`, but the result type's pointee is `T_leaf`, so
the next arrow `->j` does `j * sizeof(T_leaf)` -- wrong for any depth.

**Fix shape:** introduce an intermediate "pointer to remaining buf
shape" type. Concretely:

- Typer's HEAD_DOT arrow-int on a `[Buf N1 [Buf N2 [Buf N3 T]]]`
  receiver: when the buf-elem decoder returns `TYPE_BUF` (inner is a
  shape, depth-2+), yield `[Ptr <inner_shape_idx>]` rather than
  `[Ptr T_leaf]`. The pointee is the inner buf shape; chaining `->j`
  on this works the same way for the next dimension.
- Typer's HEAD_DOT arrow-int on a `[Ptr <shape>]` receiver where the
  pointee is a `TYPE_SHAPE_BUF`: peel one dimension. If the resulting
  inner is another `TYPE_SHAPE_BUF`, yield `[Ptr <inner-inner shape>]`;
  if scalar/struct, yield `[Ptr T_leaf]`.
- Compiler mirrors: each arrow emits `OP_PTR_ADD_OFFSET` with byte
  offset `idx * compiler__encoding_byte_size(reg, remaining_shape)`.
  Helper already exists in `src/compiler.c`.
- VM: no change. `OP_PTR_ADD_OFFSET` already does what we need.

**Hazard from this session's dev**: when the typer propagates a
binding's `struct_idx` (which is a typer-side shape idx for nested
bufs) directly as the AST node's `inferred_struct_idx`, the compiler
reads it as a *compiler*-side idx and crashes on a NULL `defs[]`
entry. This bit me in the `[addr]` path and was fixed by walking the
shape chain to the leaf encoding (a scalar sentinel or struct idx --
those align between typer and compiler registries). Watch for the same
trap when extending HEAD_DOT for depth-3+: any node-propagated idx
must be a "common encoding" (scalar sentinel / struct idx), not a
typer-shape idx. The decoder helpers already do this correctly; just
don't bypass them.

**Fixtures to add**: `buf_nested_depth3_chain.jacl` --
`$cube->i->j->k` returns the correctly-strided element. Maybe also a
`set $cube->i->j->k v` form to exercise the store path.

### 2. Depth-3+ literal init

Today errors with "literal init not yet supported for nesting depth >
2" at `src/compiler.c` HEAD_DEF buf branch (the Phase 5b depth-3+
path). Extend the literal-init emission to recurse into nested
constructors. The existing depth-2 literal init at `compiler.c:7517`
(approx, search for `inner_buf_len > 0` in the literal-init block) is
the template -- one more level of recursion plus a per-row constructor
parse.

### 3. Ref-elem bufs as struct fields

`struct Bag {i32 size, [Buf 4 dyn] items}` is rejected at
`compiler.c:13718` with a clear error. The struct walker
(`src/gc_collect.c`) needs to descend into the embedded N-tagged-slot
range. Two options:

- **Per-field shape**: extend `StructTypeField` with a `TypeShape*` (or
  shape idx) when the field is a buf, and have the struct walker
  consult it to scan N tagged slots at the field offset. Honest, more
  plumbing.
- **Bitmap extension**: extend the struct's `field_inline_bitmap` to
  distinguish "raw bytes" from "N tagged slots" for buf fields. Reuses
  infrastructure but conflates concepts.

Recommend option 1. Touches typer (allow ref-elem field at decl),
compiler (parse + register), `src/gc_collect.c` (walker descent),
`src/gc.c` write barrier on indexed store (already wired through the
existing M4.4 path -- no change needed).

## Implementation notes (for future-me)

- **Typer shape registry** lives on `TyperCtx.shape_reg`. Initialized
  via `struct_registry__init_at_offset(&tc.shape_reg, TYPER_MAX_STRUCTS)`
  so its indices start at 256 -- disjoint from struct indices in
  `tc.structs[]` (which fit in `[0, TYPER_MAX_STRUCTS)`). Decode via
  `typer__buf_elem_decode`. Freed in `typer_infer` after the pass.
- **Compiler shape registry** is `c->struct_registry` (same struct
  type as the typer's, but a separate instance). Indices start at 2
  (slot 0 = dyn placeholder, slot 1 = ctx). Decode via
  `compiler__buf_elem_decode`. Survives compilation; the VM reads
  through `vm->struct_registry` at runtime.
- **Cross-registry rule**: indices in the **typer's** registry are
  never propagated to runtime / bytecode. The typer translates to a
  "common encoding" (scalar sentinel `JACL_SCALAR_TYPE_IDX(t)` or
  struct idx aligned with the compiler's registry) when stamping AST
  `inferred_struct_idx`. The compiler stamps its own shape idx on
  `Local.struct_type_idx` for its runtime use. Don't mix.
- **`StructTypeRegistry`, `StructTypeDef`, `StructTypeField`,
  `TypeShape`, `TypeShapeKind`** are defined in **both** `src/shapes.c`
  (unity-build internal) and `src/jacl.h` (external consumer view).
  Any field change must sync both or external test consumers grab
  wrong offsets silently. The duplication is intentional (unity build
  doesn't include `jacl.h`); see `feedback_dual_struct_definitions`
  memory.

## Risks & mitigations

- **Silent type-confusion bugs.** A reader that assumes "idx in
  registry range -> struct shape" silently misinterprets a typed-vec
  shape as a struct. *Mitigation*: Phase 0 audit produces an explicit
  touch list; every Phase 1 wrapper that surfaces "is this a struct"
  asserts the kind in debug builds.
- **Bytecode operand reinterpretation.** *Mitigation*: byte widths
  unchanged. Adversarial bytecode-diff test between phases.
- **Concurrent registry access.** The GC marker reads the registry
  concurrently. *Mitigation*: registry growth stays at compile time
  (no runtime interning during VM execution); same lifetime story as
  today.
- **Migration drift across phases.** *Mitigation*: phase exits are
  full-suite-green gates; never merge a half-migrated reader.

## Status (live)

| Phase | State |
|---|---|
| 0 (audit) | done |
| 1 (parallel TypeShape array) | shipped `3400ac5` |
| 2 (typed-vec intern; compiler-side migrate) | shipped `519fdd2` |
| 3 (shared registry above typer.c; typed-map intern; typer + compiler migrate) | shipped (this commit) |
| 4 (`[Buf N [Map K V]]` wire + test) | shipped (this commit) |
| 5 (more kinds: BUF / PTR / FUTURE / BOX) | shipped `5cf6a7a` — foundation only |
| 5a (wire PTR compose: `[Buf N [Ptr T]]`) | shipped `a783668` |
| 5a (wire FUTURE compose: `[Buf N [Future T]]`) | shipped `eed8107` |
| 5b (lift depth-2 nested-buf cap; declaration / [buf-len] / [addr] at any depth) | shipped (this commit) |

Mid-Phase-3 surprise that wasn't in the Phase 0 audit: the original
shape registry lived in `src/compiler.c`, which is included **after**
`typer.c` in the unity build, so the typer couldn't reach the intern
helpers. Resolved by extracting the registry types + helpers into a
new `src/shapes.c` included between `ast.c` and `typer.c`. Both phases
now hold their own `StructTypeRegistry` (typer-side bounded to one
call to `typer_infer`; compiler-side travels to runtime as
`vm->struct_registry`). Each registry interns shapes for the shapes
it sees, independent of the other.

Typer-side encoding uses an offset trick to keep shape indices disjoint
from struct indices in `tc->structs[]`: shape registry counts from
`TYPER_MAX_STRUCTS` (= 256), so a `binding.struct_idx < TYPER_MAX_STRUCTS`
is a struct table idx, `>= TYPER_MAX_STRUCTS && < JACL_SCALAR_VEC_BASE`
is a shape registry idx, `>= JACL_SCALAR_VEC_BASE` is a scalar
sentinel. One field, three meanings, no aux clutter on the binding.

## Sizing

| Phase | Estimate |
|---|---|
| Phase 0 (audit) | 0.5 day |
| Phase 1 (rename + kind tag) | 0.5 day |
| Phase 2 (typed-vec intern) | 1-1.5 days |
| Phase 3 (typed-map intern) | 0.5-1 day |
| Phase 4 (`[Buf N [Map K V]]` + tests) | 0.5 day |
| Phase 5 (more kinds) | deferred |

**Phases 1-4 total: ~3 days of focused work**, spread across 4-5
commits each independently green.

The aux-field shortcut for just `[Map K V]` is half a day -- buys the
one feature and pushes the same encoding problem one nesting level
further out. Refactor is 6x the work and closes the class.
