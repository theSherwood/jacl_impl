# Recursive Layout Descriptor Refactor

## Quick orientation (read first)

This doc tracks a planned refactor to eliminate a whole *class* of
buffer/struct nesting gaps by deriving byte layout and GC reference
maps **recursively from the type**, instead of hand-coding them per
shape.

Status: **in progress** — Steps 1–2 done; Step 3 partially done
(struct leaves landed, ref-elem leaves pending). Steps remaining: the
ref-elem slice of Step 3 and the Step 4 cleanup. See **What's next**
at the bottom for the precise pickup point.

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
- [~] **Step 3 — route literal init through it** (Part B). Add the one
  unchecked byte-offset store the leaf path needs. Closes depth-3+
  struct leaves AND the depth-2 struct-leaf gap for free.
  - **Done so far (struct leaves):** `OP_BUF_STORE_OFF` opcode added
    (bytecode.c enum + name, vm.c jump-table entry + handler) and the
    `compiler__emit_buf_init_walk` walker added. Depth-2
    `[Buf N [Buf M Struct]]` and depth-3+ `[Buf N1 [Buf N2 [Buf N3
    Struct]]]` literal init now route through the walker instead of
    being rejected ("scalar leaf types only") or hitting an unsupported
    USET element type. Fixtures: `buf_nested_struct_literal.jacl`,
    `buf_nested_depth3_struct_literal.jacl`. Suite 87/87, harness
    438/438. The working scalar fast paths still use the old per-depth
    emitters (collapsed in Step 4).
  - **Still pending in Step 3:** nested **ref-elem** bufs
    (`[Buf N [Buf M dyn]]`, Tier-1 #2). The walker + opcode already
    handle ref leaves, but the def site still rejects them
    (`compiler.c`, "reference element T ... not yet supported") and the
    *access* path (chained read/write of a ref leaf at depth) isn't
    wired — so closing #2 is more than init and is its own slice.
    **Step-by-step pickup in "What's next" → A.**
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
- [ ] **Step 4 — cleanup.** Collapse the now-redundant per-depth
  emitters and per-leaf-kind store opcodes. Net line-count should drop.
  **Step-by-step pickup in "What's next" → B** (note the
  error-message-text trap).

## What this closes

`BUFFER_DESIGN.md` Tier-1 items this refactor targets:

1. Depth-3+ literal init with struct leaves — **✅ closed** (Step 3,
   `2013b9b`), plus the unlisted depth-2 struct-leaf gap.
2. Nested ref-elem bufs (`[Buf N [Buf M dyn]]`) — **still open.** Init
   is ready (the walker + `OP_BUF_STORE_OFF` already handle ref leaves,
   and Step 2 made the GC ref-map recursive), but the def site still
   rejects them and the chained ref-leaf *access* path isn't wired.

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
  depth-2 struct branch `if (inner_buf_len > 0 && is_struct_elem)`
  (~8446).
- Ref-elem rejection (the def-site block to remove for Tier-1 #2):
  `if (elem_is_ref && inner_buf_len > 0)` (`compiler.c`, ~8407).
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

Two slices remain. They are independent — do them in either order.

### A. Nested ref-elem bufs — `[Buf N [Buf M dyn]]` (Tier-1 #2)

The init half is already built; this slice is mostly the **access**
path plus removing one rejection.

1. **Remove the def-site rejection** at `compiler.c:~8407`
   (`if (elem_is_ref && inner_buf_len > 0)`). With it gone, the depth-2
   path falls through to the init dispatch; route the ref-leaf case to
   `compiler__emit_buf_init_walk` exactly like the struct-leaf branch
   just above it (intern `[Buf M dyn]` via `type_shape_intern_buf`, call
   the walker). The walker + `OP_BUF_STORE_OFF` already emit the
   barriered ref store, so init should "just work."
2. **Wire the chained ref-leaf read/write access path.** This is the
   real work: `$m->i->j` where the leaf is a ref must load/store a
   tagged `JaclVal` slot (with write barrier on store), not raw bytes.
   Find the depth-2 struct access path (`OP_BUF_GET_STRUCT_LOCAL` /
   the arrow-chain compile in `HEAD_DOT`) and add the ref-leaf analogue.
3. **GC is already correct** for the *stack-inline* case (Step 2 made
   the ref-map recursive; see `buf_traced_nested_struct_gc.jacl` for
   the pattern). Add a GC-stress fixture for a nested ref-elem buf to
   prove it, mirroring that fixture.
4. **Watch the follow-ups from Step 2** (recorded under Step 2 above):
   `OBJ_MUTABLE_REF` struct boxes still trace bytes as raw, and the
   256-slot flat `slot_ref_bitmap` cache can under-cover a very large
   inline buf. Neither blocks a small fixture, but a boxed nested
   ref-elem buf would expose the first.

### B. Step 4 cleanup — collapse the redundant emitters

Now that the walker exists, the per-depth/per-leaf machinery is
redundant. Goal: net line-count drop.

1. **Reroute the working scalar fast paths** (depth-1
   `OP_BUF_SET_LOCAL`, depth-2 flat `OP_BUF_USET_LOCAL`, depth-3+
   `compiler__emit_buf_nested_literal_init`) through
   `compiler__emit_buf_init_walk`.
2. **Then delete** `compiler__emit_buf_nested_literal_init` and any
   per-leaf store opcodes left unused (`OP_BUF_SET_LOCAL` /
   `OP_BUF_SET_STRUCT_LOCAL` / `OP_BUF_USET_LOCAL` — grep for remaining
   emit sites first; the access/read paths may still use the GET
   variants).
3. **TRAP — error-message text.** The walker's range-check message is
   `"...value X out of range at depth D index I"`. The old emitters say
   `"row R column C value X out of range"` and `"element I value X out
   of range for TYPE"`. Some fixtures assert on the old text. Before
   rerouting, grep `test/jacl/*.jacl` for `out of range` expectations
   and either (a) thread richer position context (row/col, type name)
   into the walker to preserve the messages, or (b) update the fixtures
   deliberately. Option (a) is preferred — the doc's Part B always
   intended to "thread position context through the recursion."
4. Benchmark nothing special here, but keep the full suite + harness
   green at each reroute (one leaf kind at a time).

## Related docs

- `BUFFER_DESIGN.md` — the buffer feature status snapshot (source of
  truth for what's shipped).
- `docs/TYPE_REGISTRY_REFACTOR.md` — the registry this descriptor sits
  on; nested-buf access architecture.
