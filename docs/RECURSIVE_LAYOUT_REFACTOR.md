# Recursive Layout Descriptor Refactor

## Quick orientation (read first)

This doc tracks a planned refactor to eliminate a whole *class* of
buffer/struct nesting gaps by deriving byte layout and GC reference
maps **recursively from the type**, instead of hand-coding them per
shape.

Status: **not started** — design agreed, sequencing planned. No code
written yet. This is the pickup doc for that work.

## The root cause (why this refactor exists)

Three open gaps in the `[Buf N T]` feature are the same bug wearing
different hats: **layout knowledge is hand-coded per shape instead of
derived recursively from the type.** It surfaces in three places:

1. **Store opcodes bake the leaf kind in.** `OP_BUF_SET_LOCAL`
   (scalar), `OP_BUF_SET_STRUCT_LOCAL` (struct), `OP_BUF_USET_LOCAL`
   (unchecked scalar) each know what they store, so every new leaf
   kind needs a new opcode.
2. **Literal-init emitters are hand-written per depth.** Depth-1
   (`compiler.c:~8306`), depth-2 (`~8225`), depth-3+ recursive
   (`compiler__emit_buf_nested_literal_init`, `~3793`). Each covers
   only some leaf kinds — the unwritten combinations are the gaps.
3. **GC classification is a flat, one-level bitmap.** `inline_slot_bitmap`
   (per stack slot) and `slot_ref_bitmap[32]` (per struct) are computed
   by walking *one* level of fields. `slot_ref_bitmap` (`compiler.c:~15290`)
   only decodes scalar-sentinel element types — it cannot see into a
   nested buf's own ref map. This is why nested ref-elem bufs are blocked.

### The gap table (current state)

Literal init splits on nesting depth × leaf kind:

|              | scalar leaf | struct leaf | ref-elem leaf |
|--------------|-------------|-------------|---------------|
| depth-1 `[Buf N T]`        | done (`OP_BUF_SET_LOCAL`) | done (`OP_BUF_SET_STRUCT_LOCAL`) | done (flat local) |
| depth-2 `[Buf N [Buf M T]]`| done (flat `row*M+col`) | **gap** | **gap** |
| depth-3+                   | done (recursive emitter) | **gap** (rejected `compiler.c:~7638`) | **gap** (rejected `compiler.c:~8180`) |

The empty cells are not hard problems — they are code nobody wrote,
because each cell is a bespoke emitter.

## The clean fix

A **recursive layout descriptor** in the shape registry, plus two
descriptor-driven mechanisms that replace the hand-written ones.

### Part A — recursive layout descriptor

Every `TypeShape` gains a canonical layout `{ size, align, ref_map }`,
computed bottom-up at type-definition time and cached:

- scalar `T` → `{ sizeof(T), align(T), no refs }`
- ref scalar (dyn/str/vec/map/closure/stream) → `{ 8, 8, one traced slot }`
- `[Buf N elem]` → `{ N * elem.size, elem.align, elem.ref_map tiled N times }`
- struct → union of field layouts at their offsets

This makes the GC reference map **correct by construction** for
arbitrary nesting — there is no "extend the bitmap from per-struct to
per-layout" step because the layout *is* the recursion.

### Part B — one recursive literal-init walker

A single function walks the descriptor + the AST constructor in
lockstep:

- buf node → validate `[Buf N ...]` head, loop elements, recurse
- struct node → validate ctor, loop fields, recurse
- scalar leaf → byte-store at computed offset + range check
- ref leaf → compile expr, store-with-barrier

This subsumes the entire 3×3 table. `compiler__emit_buf_nested_literal_init`
is already half of it (recurses on buf nesting, dead-ends at scalar
leaves).

### Part C — descriptor-driven GC walker

"Is this slot traced?" becomes "walk the descriptor" instead of
reading a flat bitmap. Unifies `inline_slot_bitmap` and
`slot_ref_bitmap` into one mechanism. Keep the flat bitmap only as a
cache for the small/flat common case.

## Why this is bounded

The runtime primitives are **already mostly offset-based**:
`OP_PTR_STORE` fires barriers, `OP_INLINE_COPY_LOCAL` memcpys bytes,
addressing is byte-offset. The bespoke-ness is concentrated in the
compiler's init emitters and the GC bitmaps — not the whole VM. So
this is "build one descriptor and route two things through it," not a
type-system rewrite.

## Risks / traps

- **Cross-registry hazard.** The typer (`tc->shape_reg`) and compiler
  (`c->struct_registry`) each have their own shape registry; idxs are
  NOT interchangeable. The descriptor must be computed identically in
  both, or in one canonical place both reach. Biggest correctness trap.
- **Shared `StructTypeDef` duplication.** Defined byte-identical in
  `src/shapes.c` and `src/jacl.h` (checked by `test_struct_sizes.c`).
  Any new layout fields must sync both.
- **The flat bitmap can't just grow.** `slot_ref_bitmap[32]` is 256
  bits, but buf bytes cap at 65535 (~8191 slots). Do NOT materialize an
  8191-bit map per shape. Keep the GC walker compositional/recursive
  (tile the element's small ref-map; don't flatten); flat bitmap is a
  cache only. This touches the hot GC mark path — needs benchmarking.
- **Error-message fidelity.** Today: "row 3 column 2 value X out of
  range." A generic walker must thread position context through the
  recursion to preserve these.

## Plan (sequenced, not big-bang)

- [ ] **Step 1 — descriptor spike (reversible).** Build the recursive
  layout descriptor (Part A). Wire it nowhere. Add a debug assertion
  that it agrees with existing `slot_ref_bitmap` / byte-size decisions
  across the full fixture suite. De-risks the cross-registry trap in
  isolation. **Decision gate:** does cross-registry consistency hold?
- [ ] **Step 2 — route GC classification through it** (Part C),
  keeping the flat bitmap as a cache. Unblocks nested ref-elem bufs
  with no new opcodes. Benchmark the mark path.
- [ ] **Step 3 — route literal init through it** (Part B). Add the one
  unchecked byte-offset store the leaf path needs. Closes depth-3+
  struct leaves AND the depth-2 struct-leaf gap for free.
- [ ] **Step 4 — cleanup.** Collapse the now-redundant per-depth
  emitters and per-leaf-kind store opcodes. Net line-count should drop.

## What this closes

When Step 3 lands, these `BUFFER_DESIGN.md` Tier-1 items are gone:

1. Depth-3+ literal init with struct leaves
2. Nested ref-elem bufs (`[Buf N [Buf M dyn]]`)

…plus the unlisted depth-2 struct-leaf gap, and any future
depth × leaf × kind combination becomes "does the walker handle this
node kind?" — and there are only four node kinds (buf, struct, scalar
leaf, ref leaf).

## Where to look first

- Per-depth init emitters: `compiler__emit_buf_nested_literal_init`
  (`src/compiler.c`, ~3793); depth-1/2 inline paths (~8225, ~8306).
- Struct-leaf / ref-elem rejections: `compiler.c:~7638`, `~8180`.
- `slot_ref_bitmap` computation: `compiler.c:~15290`.
- GC inline-slot marking: `vm__mark_struct_inline_slots` (`src/vm.c`,
  ~630); `OBJ_HEAP_RECORD` walker (`src/gc_collect.c`, ~457).
- Shape registry + `StructTypeDef`: `src/shapes.c` + `src/jacl.h`
  (keep in sync).

## Related docs

- `BUFFER_DESIGN.md` — the buffer feature status snapshot (source of
  truth for what's shipped).
- `docs/TYPE_REGISTRY_REFACTOR.md` — the registry this descriptor sits
  on; nested-buf access architecture.
