# Type-Shape Registry Refactor

## Quick orientation (read first)

If you're resuming this work, the current frontier is **nested
buffers (`[Buf N T]`)**. The type-registry refactor that motivated
this doc is largely complete; Phases 1-5 shipped, and Phase 5b is
extending the *recognizer wiring* and *codegen* for nested buf access
on top of the registry.

What works today (post most-recent session):
- Declaration, `[buf-len]`, `[addr]` at any nesting depth.
- Arrow-chain read/set at any depth with literal or dynamic indices:
  `$cube->i->j->k`, `$cube->$i->$j->$k`, mixed (`$cube->1->$j->3`).
- Decomposed chains: `def p $cube->1` then `$p->2->3` (and further
  decomposition: `def q $p->2` then `$q->3`). Both literal and
  dynamic indices.
- Slice passing across proc boundaries via `[Ptr [Buf N T]]`
  param annotations.

What's still open: see ["Pickup for next session"](#pickup-for-next-session)
below. The current top items are depth-3+ literal init and runtime
bounds checks for dynamic indices.

**Where to look first:**
- Chain helpers: `typer__nested_buf_chain_result` in `src/typer.c`,
  `compiler__try_compile_nested_buf_chain` in `src/compiler.c`.
- Slice-passing param annotation: `typer__ptr_type` (typer) +
  `compiler__ptr_type_expr` (compiler), each accepts a `[Buf N T]`
  pointee and interns the shape.
- Dynamic-index parser: `parser__maybe_arrow_access` in
  `src/parser.c`.
- Architecture summary:
  ["How the nested-buf access path works"](#how-the-nested-buf-access-path-works-current-architecture)
  below.

**Fixtures (in `test/jacl/`)**:
- `buf_nested.jacl` -- depth-2 literal chain (read + set).
- `buf_nested_literal.jacl` -- depth-2 literal init.
- `buf_nested_struct.jacl` -- depth-2 with struct elements.
- `buf_nested_oob.jacl` -- compile-time bounds errors.
- `buf_nested_depth3.jacl` -- depth-3 declaration + `[addr]` + manual
  pointer arithmetic.
- `buf_nested_depth3_chain.jacl` -- depth-3 arrow chains
  (`$cube->i->j->k`).
- `buf_nested_depth3_decomp.jacl` -- decomposed chains via `def`.
- `buf_dynamic_chain.jacl` -- dynamic and mixed indices.
- `buf_slice_pass.jacl` -- `[Ptr [Buf N T]]` proc params.

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

### Phase 5a/5b -- Recognizer wiring

Completed:

- ✅ **`[Buf N [Ptr T]]`** -- `a783668`.
- ✅ **`[Buf N [Future T]]`** -- `eed8107`.
- ✅ **Nested-buf depth lift via recursive `TYPE_SHAPE_BUF` interning**
  -- `0ea5bff`. Declaration / `[buf-len]` / `[addr]` at any depth.
- ✅ **Depth-3+ arrow chains (`$cube->i->j->k`, read + set)** -- this
  commit. Two helpers (`typer__nested_buf_chain_result` /
  `compiler__try_compile_nested_buf_chain`) walk the chain AST
  inside-out and emit a single `OP_BUF_ADDR_LOCAL` plus leaf load/store.
  Fixture `buf_nested_depth3_chain.jacl`.
- ✅ **Decomposed nested-buf chains (`def p $cube->1; $p->2->3`)** --
  this commit. The chain walker also accepts a TYPE_PTR base whose
  pointee is a `TYPE_SHAPE_BUF`. Both sides patch the local's
  remaining-shape idx at def-time by re-walking the RHS AST. Slice
  aliases mutate the source buf as expected. Fixture
  `buf_nested_depth3_decomp.jacl`.
- ✅ **Dynamic arrow indices (`$cube->$i->$j->$k`)** -- this commit.
  Parser accepts `TOKEN_VAR` (and any non-LIT_STRING expr) after `->`.
  Chain helper folds prefix-literal arrows into the base op's offset,
  emits `OP_PTR_OFFSET` per dynamic arrow, folds trailing literals into
  the leaf op's u16 offset. Depth-2 chains with any dynamic index
  route through this path; all-literal depth-2 stays on the existing
  per-arrow codegen. Fixture `buf_dynamic_chain.jacl`.
- ✅ **Slice passing via `[Ptr [Buf N T]]` proc params** -- this
  commit. `typer__ptr_type` and `compiler__ptr_type_expr` accept a
  `[Buf N T]` AST as the pointee and intern the whole shape as a
  `TYPE_SHAPE_BUF`. Proc-param resolution stores the shape idx on the
  param. Inside the body, `$param->...` re-enters the chain helper via
  the TYPE_PTR-with-buf-shape branch. Fixture `buf_slice_pass.jacl`.

Open:

- ⚠ **`[Buf N [Box T]]`** -- shape kind ready, but `[Box T]` isn't a
  parser-recognized annotation today. Adding `[Box T]` would unblock
  this with a one-line recognizer extension. See pickup #5.
- ❌ **`[Vec [Buf N T]]`, `[Map K [Buf N V]]`** -- typed-vec/map
  annotation parsers need to accept parametric T at element positions.
  See pickup #4.

## How the nested-buf access path works (current architecture)

This section documents the working pieces so the next session can
extend them without re-deriving the design.

### Parser: arrow RHS forms (`src/parser.c parser__maybe_arrow_access`)

After `->` the parser accepts:

- `TOKEN_WORD` -- name field access (`$obj->field`), produces an
  `AST_LIT_STRING` arg. Routes to the existing field-access codepath.
- `TOKEN_INT` -- literal buf index (`$buf->3`), produces an
  `AST_LIT_INT`. Routes to the chain helper.
- `TOKEN_VAR` -- dynamic buf index (`$slice->$i`), produces an
  `AST_VAR_REF`. Routes to the chain helper's dynamic-arrow path.
- Any other token -- parse error.

To extend: `$expr->[expr]` or `$expr->$(expr)` aren't supported yet.
If wanted, add the corresponding `TOKEN_DOLLAR_BRACKET` /
`TOKEN_DOLLAR_PAREN` branches with a recursive expression parse.

### Chain helper (typer + compiler)

The chain helper is the heart of nested-buf access. Both sides have a
mirror implementation that walks the AST chain inside-out:

- **Typer** (`src/typer.c typer__nested_buf_chain_result`): yields the
  chain's result type. For intermediate steps, also returns the
  remaining typer-side `TYPE_SHAPE_BUF` idx so the def handler can
  stash it on a binding when the user decomposes (`def p $cube->1`).
- **Compiler** (`src/compiler.c compiler__try_compile_nested_buf_chain`):
  emits the actual opcodes. Identical AST walk, identical shape chain
  decode, but pulling strides from the compiler-side registry. Emits
  one base op (`OP_BUF_ADDR_LOCAL` for TYPE_BUF base or `OP_GET_LOCAL`
  for TYPE_PTR base) with prefix-literal offset folded in, then per
  dynamic arrow emits `OP_PTR_OFFSET` (stride), folding trailing
  literals into the leaf load/store's u16 byte offset.

Bases supported:
- `TYPE_BUF` local, depth-3+ (shape-registry encoding via
  `local.struct_type_idx` chain).
- `TYPE_BUF` local, depth-2 (legacy `buf_inner_len` encoding) -- only
  when at least one index is dynamic. All-literal depth-2 still routes
  through the older per-arrow code (line ~11875 in `compiler.c`) to
  preserve well-tested emission for that common case.
- `TYPE_PTR` (or `TYPE_U64`) local whose `struct_type_idx` points at a
  `TYPE_SHAPE_BUF`. Covers decomposed slices (`def p $cube->1`) and
  slice-passing params (`[Ptr [Buf N T]]`).

### Decomposed-chain capture (def-time shape patching)

When a chain expression is bound to a new name and lands at an
intermediate level (`TYPE_PTR` with `UINT32_MAX` struct idx), each side
re-walks the RHS AST at def-time and patches the local/binding with
its own remaining buf-shape idx:

- **Typer**: inside `typer__handle_def_or_mut`, the
  `effective == TYPE_PTR && struct_idx == UINT32_MAX` branch calls
  `typer__nested_buf_chain_result` with `out_remaining_shape_idx` and
  stores it on the new binding's `struct_idx`.
- **Compiler**: after `TYPEINFO_SAVE` in `HEAD_DEF`'s local-binding
  path, the same condition triggers an inline AST walk that peels the
  source binding's compiler-side shape chain by `chain_depth` levels
  (TYPE_BUF base peels `chain_depth - 1`; TYPE_PTR base peels
  `chain_depth`) and patches `local.struct_type_idx`.

### Slice-passing annotation (`[Ptr [Buf N T]]`)

Both `typer__ptr_type` and `compiler__ptr_type_expr` accept a
`[Buf N T]` AST node as the pointee. Inner buf is interned as a
`TYPE_SHAPE_BUF` (including the outer N) on the respective registry.
The proc-param resolver then stores that shape idx on the param's
binding/local just like a decomposed local. Inside the proc body,
`$param->...` re-enters the chain helper via the TYPE_PTR-with-buf-shape
branch -- no new code path.

### The cross-registry rule (why the chain helper re-walks)

Typer and compiler each own a `StructTypeRegistry` instance; shape
indices are NOT interchangeable. Per the rule in "Implementation
notes" below, the typer never propagates a typer-shape-idx through an
AST node's `inferred_struct_idx`. So the compiler's chain helper
can't trust any non-scalar/non-struct-idx number on the AST; it
re-walks the chain inside-out from the AST and looks up shape info
through the *compiler's* local table and registry. The typer's
binding table separately carries typer-shape-idxes for its own
re-decoding on subsequent expressions.

## Pickup for next session

In rough order of payoff, the open items:

### 1. Depth-3+ literal init

Today errors with "literal init not yet supported for nesting depth >
2" at `src/compiler.c` HEAD_DEF buf branch (the Phase 5b depth-3+
path). Extend the literal-init emission to recurse into nested
constructors. The existing depth-2 literal init in
`compiler.c` (search for `inner_buf_len > 0` in the HEAD_DEF buf
block) is the template -- one more level of recursion plus a per-row
constructor parse.

### 2. Runtime bounds checks for dynamic indices

Dynamic arrow chains (`$cube->$i->$j->$k`) emit `OP_PTR_OFFSET`
without bounds checking -- the runtime trusts the index is in range.
Literal indices are still compile-time validated. To close the gap:

- Add `OP_PTR_OFFSET_CHECKED` with `u16 elem_size, u16 dim_size`: pops
  i32 idx, traps if `idx < 0 || idx >= dim_size`, otherwise multiplies
  and adds.
- Compiler dynamic-arrow loop swaps `OP_PTR_OFFSET(stride)` for
  `OP_PTR_OFFSET_CHECKED(stride, dims[k])`.

Cost: one new opcode, mechanical compiler change. ~half a day.

### 3. Ref-elem bufs as struct fields

`struct Bag {i32 size, [Buf 4 dyn] items}` is rejected in
`compiler.c` (search for the M4.4-related error) with a clear error.
The struct walker (`src/gc_collect.c`) needs to descend into the
embedded N-tagged-slot range. Two options:

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

### 4. `[Vec [Buf N T]]` and `[Map K [Buf N V]]`

The Phase 5 foundation supports composing `TYPE_SHAPE_BUF` under any
other shape kind, but the typed-vec / typed-map annotation parsers
still only accept scalar/struct element types. Extending them is a
mechanical extension once a use case demands it:

- `compiler.c`: in the `[Vec T]` and `[Map K V]` annotation parsers,
  accept `AST_COMMAND` with "Buf" head and call
  `compiler__intern_buf_shape_chain`.
- `typer.c`: symmetric change to the typed-collection annotation
  resolvers.

### 5. `[Box T]` annotation

`[Box T]` isn't a parser-recognized annotation, so `[Buf N [Box T]]`
can't be expressed. The shape kind (`TYPE_SHAPE_BOX`) is ready. Adding
the annotation would be a one-line recognizer extension on each side
plus the wiring at the use sites.

### 6. Decomposing across loops (already works, document)

The current architecture lets users write loops like:

```jacl
proc each_row {[Ptr [Buf 3 [Buf 4 i32]]] p} {
  mut i 0
  while [< $i 3] {
    set $p->$i->0 [+ $p->$i->0 1]
    set i [+ $i 1]
  }
}
```

This works today (verified in `buf_slice_pass.jacl`). If a future
session is asked for "loops over nested bufs," the answer is: it
already works, just point at the fixture.

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
| 5b (lift depth-2 nested-buf cap; declaration / [buf-len] / [addr] at any depth) | shipped `0ea5bff` |
| 5b (depth-3+ arrow chains `$cube->i->j->k`, read + set) | shipped (this commit) |
| 5b (decomposed chains `def p $cube->1; $p->2->3`) | shipped (this commit) |
| 5b (dynamic arrow indices `$cube->$i->$j->$k`) | shipped (this commit) |
| 5b (slice passing via `[Ptr [Buf N T]]` proc params) | shipped (this commit) |

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
