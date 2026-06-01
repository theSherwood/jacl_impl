# Recursive Layout Descriptor Refactor

## Quick orientation (read first)

This doc tracks a planned refactor to eliminate a whole *class* of
buffer/struct nesting gaps by deriving byte layout and GC reference
maps **recursively from the type**, instead of hand-coding them per
shape.

Status: **done** — all four steps landed. The descriptor walker is now
the single source of truth for byte layout, GC ref maps, and literal
init across every depth × leaf-kind combination.

Commits so far (on `main`):
- `73cc2ac` — Step 1: recursive layout descriptor spike (wired nowhere;
  env-guarded self-check confirmed it reproduces existing decisions).
- `bf5a8bf` — Step 2: GC slot classification routed through the
  descriptor; fixed a latent bug where ref-elem bufs nested in a
  sub-struct field were marked raw and collected.
- `2013b9b` — Step 3 (struct leaves): `OP_BUF_STORE_OFF` +
  `compiler__emit_buf_init_walk`; depth-2 and depth-3+ struct-leaf
  literal init now route through the walker. Closes BUFFER_DESIGN
  Tier-1 #1.
- `3760304` — Step 3 (ref-elem leaves): removed the def-site rejection
  and extended the walker-routing branch to fire for ref leaves too.
  Init + chained access + GC all work for `[Buf N [Buf M dyn]]`.
  Closes BUFFER_DESIGN Tier-1 #2.
- *pending commit* — Step 4 (cleanup): rerouted depth-1, depth-2-flat,
  and depth-3+ scalar literal init through the walker. Deleted
  `compiler__emit_buf_nested_literal_init`. Walker now carries an
  optional `BufInitPos` so the old "element I" / "row R column C"
  range-check messages survive. Net –171 lines in compiler.c.

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
| depth-2 `[Buf N [Buf M T]]`| done (flat `row*M+col`) | done (walker) | done (walker) |
| depth-3+                   | done (recursive emitter) | done (walker) | done (walker) |

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

- [x] **Step 1 — descriptor spike (reversible).** Build the recursive
  layout descriptor (Part A). Wire it nowhere. Add a debug assertion
  that it agrees with existing `slot_ref_bitmap` / byte-size decisions
  across the full fixture suite. De-risks the cross-registry trap in
  isolation. **Decision gate:** does cross-registry consistency hold?
  **Done** (commit 73cc2ac): `compiler__encoding_align` +
  `compiler__encoding_ref_map` added after `struct__slot_width`; an
  env-guarded self-check confirmed the descriptor reproduces existing
  `slot_ref_bitmap` / byte-size decisions across the suite with **zero
  divergences** (435/435). Cross-registry consistency held.
- [x] **Step 2 — route GC classification through it** (Part C),
  keeping the flat bitmap as a cache. Unblocks nested ref-elem bufs
  with no new opcodes. Benchmark the mark path.
  **Done.** Struct registration now computes `slot_ref_bitmap` via
  `compiler__encoding_ref_map` (replacing the hand-coded one-level loop
  that `continue`d past `TYPE_STRUCT` fields — a genuine latent GC bug:
  ref-elem bufs nested inside a sub-struct field were marked raw and
  collected). Buf-field layout now uses the descriptor's recursive
  `byte_size`/`align`. The `OBJ_HEAP_RECORD` GC walker is now recursive
  (`gc__push_record_refs`) instead of a flat field loop. Proven via
  stash-and-rebuild: `buf_traced_nested_struct_gc.jacl` crashes without
  the fix, passes with it. Suite 87/87, harness 436/436.
  - **Follow-ups discovered (not blocking Step 3):**
    - `OBJ_MUTABLE_REF` with `type_idx>0` (boxed user struct) still
      traces its bytes as **raw**, not via the descriptor. A ref-buf
      inside a *boxed* struct would not be traced. The
      `OBJ_HEAP_RECORD` path (ctx + heap structs) is hard to reach for
      plain user structs (`[vec $o]` rejects bare structs; boxing routes
      through `MUTABLE_REF`). Generalizing `MUTABLE_REF` struct boxes to
      the recursive walker is the clean follow-up.
    - **256-slot cap asymmetry:** the cached flat `slot_ref_bitmap[32]`
      holds 256 slots; the recursive GC walker is uncapped. For very
      large/deep inline bufs the cache would silently under-cover. The
      walker is correct; the cache needs a "too big to cache → always
      walk" guard before relying on it as the fast path everywhere.
- [x] **Step 3 — route literal init through it** (Part B). Add the one
  unchecked byte-offset store the leaf path needs. Closed depth-3+
  struct leaves AND the depth-2 struct-leaf gap, AND nested ref-elem
  bufs (Tier-1 #2).
  - **Struct leaves (commit `2013b9b`):** `OP_BUF_STORE_OFF` opcode
    added (bytecode.c enum + name, vm.c jump-table entry + handler) and
    the `compiler__emit_buf_init_walk` walker added. Depth-2
    `[Buf N [Buf M Struct]]` and depth-3+ `[Buf N1 [Buf N2 [Buf N3
    Struct]]]` literal init now route through the walker instead of
    being rejected. Fixtures: `buf_nested_struct_literal.jacl`,
    `buf_nested_depth3_struct_literal.jacl`.
  - **Ref-elem leaves (slice A, *pending commit*):** removed the
    def-site rejection (`if (elem_is_ref && inner_buf_len > 0)` at
    `compiler.c:~8407`) and extended the walker-routing condition at
    `~8446` from `is_struct_elem` to `(is_struct_elem || elem_is_ref)`,
    computing the inner buf encoding with the scalar sentinel for ref
    leaves. The walker's ref-leaf branch in `OP_BUF_STORE_OFF` already
    fires the GC write barrier (vm.c:3640–3650).
    - **Access path** turned out to be already wired: `$grid->i` at
      `buf_inner_len > 0` emits `OP_BUF_ADDR_LOCAL` (no ref gating); the
      outer `->j` flows through the ptr-arrow path, which routes through
      `OP_PTR_LOAD` / `OP_PTR_STORE` — both already have ref-type
      branches with the SATB write barrier (vm.c:7664, 7734). No
      compiler or VM changes needed.
    - **GC** is correct via Step 2's recursive ref-map; proven by a
      stress fixture mirroring `buf_traced_nested_struct_gc.jacl`.
    - Fixtures: `buf_nested_dyn_literal.jacl`, `buf_nested_dyn_access.jacl`,
      `buf_nested_dyn_gc.jacl`. Suite 87/87, harness 441/441.
  - The working scalar fast paths still use the old per-depth emitters
    (collapsed in Step 4).
  - **Design (implemented):**
    - New opcode `OP_BUF_STORE_OFF base_slot:u8 byte_offset:u16
      leaf_enc:u16`. Addresses by **byte offset** (not element index),
      because struct leaves sit at arbitrary offsets inside a tiled
      struct — the element-stride `OP_BUF_USET_LOCAL` can't express
      that. `leaf_enc` is the descriptor encoding (scalar sentinel or
      registry idx), so one opcode covers all three leaf kinds:
      - scalar-value leaf → pop val, narrow, memcpy width bytes at
        `base_bytes + byte_offset` (same per-type narrowing as USET).
      - ref leaf → pop val, store `JaclVal` at offset **with write
        barrier** (offset is 8-aligned by construction).
      - struct leaf → ctor leaves inline bytes on TOS; memcpy
        `total_size` bytes at offset, clear consumed inline bitmap.
    - New walker `compiler__emit_buf_init_walk(c, enc, byte_base,
      ast_value, pos_ctx)` walks the layout encoding + AST ctor in
      lockstep (buf node → loop+recurse; struct node → loop fields;
      leaf → emit `OP_BUF_STORE_OFF`). Threads position context so the
      "row R column C value X out of range" messages survive.
    - **Sequencing inside Step 3:** first route only the currently
      *rejected* gaps (depth-2 `[Buf N [Buf M Struct]]`, depth-3+
      struct leaves) through the walker + new opcode, with new
      fixtures. Leave the working scalar fast paths on their existing
      opcodes until Step 4 collapses them. Keeps each commit green.
- [x] **Step 4 — cleanup.** Collapsed the per-depth init emitters
  (depth-1 / depth-2-flat / depth-3+) through `compiler__emit_buf_init_walk`.
  Walker took an optional `BufInitPos` (dim chain + leaf type + index
  trail) so the old `"element I"` / `"row R column C"` range-check
  messages are preserved verbatim where fixtures asserted them.
  `compiler__emit_buf_nested_literal_init` deleted. The per-leaf store
  opcodes (`OP_BUF_SET_LOCAL`, `OP_BUF_USET_LOCAL`,
  `OP_BUF_SET_STRUCT_LOCAL`) are still used by the `buf-set` /
  `buf-unchecked-set` builtins and the runtime-index arrow-access path
  (`$buf->$i <- v`), so they stay. Net: –171 lines, suite 87/87,
  harness 441/441.

## What this closes

`BUFFER_DESIGN.md` Tier-1 items this refactor targets:

1. Depth-3+ literal init with struct leaves — **✅ closed** (Step 3,
   `2013b9b`), plus the unlisted depth-2 struct-leaf gap.
2. Nested ref-elem bufs (`[Buf N [Buf M dyn]]`) — **✅ closed** (Step 3
   slice A, *pending commit*). Init via the walker, chained access via
   existing `OP_BUF_ADDR_LOCAL` + `OP_PTR_LOAD/STORE` ref-type branches,
   GC via Step 2's recursive ref-map.

The endgame: any future depth × leaf × kind combination becomes "does
the walker handle this node kind?" — and there are only four node kinds
(buf, struct, scalar leaf, ref leaf).

## Where to look first

Line numbers drift; the symbol names are the durable anchors.

- Descriptor (Step 1): `compiler__encoding_align`,
  `compiler__encoding_ref_map` (`src/compiler.c`, ~609/~637),
  `compiler__encoding_byte_size` (~277).
- Init walker (Step 3): `compiler__emit_buf_init_walk`
  (`src/compiler.c`, ~3904). Old per-depth emitter still in use for
  scalar leaves: `compiler__emit_buf_nested_literal_init` (~4015).
- Init dispatch sites (where the walker is wired in): depth-3+ struct
  branch (`compiler.c`, ~7900, inside the `inner_telt` is-a-Buf block);
  depth-2 struct/ref branch
  `if (inner_buf_len > 0 && (is_struct_elem || elem_is_ref))` (~8440).
- New leaf store opcode: `OP_BUF_STORE_OFF` — decl `src/bytecode.c`
  (~312) + name (~567); handler `CASE(OP_BUF_STORE_OFF)` (`src/vm.c`,
  ~3553); jump-table entry (~2572).
- `slot_ref_bitmap` computation (now descriptor-driven):
  `compiler.c:~15543` (`compiler__encoding_ref_map(reg, this_idx, ...)`).
- GC: `gc__push_record_refs` + `OBJ_HEAP_RECORD` walker
  (`src/gc_collect.c`, ~130 / ~505); inline-slot marking
  `vm__mark_struct_inline_slots` (`src/vm.c`, ~630).
- Shape registry + `StructTypeDef`: `src/shapes.c` + `src/jacl.h`
  (keep in sync).

## What's next (pickup point)

Refactor is complete. The walker (`compiler__emit_buf_init_walk`) +
the layout descriptor (`compiler__encoding_byte_size` /
`compiler__encoding_align` / `compiler__encoding_ref_map`) cover every
depth × leaf-kind combination that's shipped today. Future work in
this area becomes "does the walker handle this node kind?" — there are
only four (buf, struct, scalar leaf, ref leaf).

Open follow-ups noted under Step 2:

- ~~`OBJ_MUTABLE_REF` struct boxes still trace their bytes as raw~~ ✅
  closed (*pending commit*). The `OBJ_MUTABLE_REF` case now mirrors
  `OBJ_HEAP_RECORD`: when `type_idx > 0` it routes the box's bytes
  through `gc__push_record_refs`, so embedded ref-elem fields (e.g.
  `[Buf N dyn]`) get marked. Fixture: `buf_boxed_struct_dyn_gc.jacl`
  (proven via stash-and-rebuild — pre-fix the harness aborts on
  `jacl_vec_get` after the vecs are collected).
- ~~The cached flat `slot_ref_bitmap[32]` (256 slots) silently
  under-covered very large/deep inline bufs~~ ✅ closed (*pending
  commit*). Struct definitions are now rejected at registration time
  if the descriptor would place a ref slot past index 255, so the
  cache is always sufficient. The error message points at `[Vec T]` /
  field-splitting as the alternatives. Fixture:
  `struct_ref_slot_cap.jacl`.

## Related docs

- `BUFFER_DESIGN.md` — the buffer feature status snapshot (source of
  truth for what's shipped).
- `docs/TYPE_REGISTRY_REFACTOR.md` — the registry this descriptor sits
  on; nested-buf access architecture.
