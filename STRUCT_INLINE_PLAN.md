# Struct Inline-By-Default — Implementation Plan

## Goal

Move JACL structs to "always inline by default". Heap allocation of struct
values is only possible via explicit `[box $val]`, never automatic. The lone
exception is `ctx`, which becomes a builtin "heap record" — a special pointer
shape, not a user-defined struct.

See `STRUCT_DESIGN.md` for the broader design rationale.

## Design Rules (settled)

1. **No reference fields in `defstruct`.** Allowed field types: `i8/u8/i16/u16/i32/u32/i64/u64/f32/f64/bool` and nested structs. `str/vec/map/closure/dyn` are rejected at compile time.
2. **No struct-at-dyn boundaries.** Struct values may only live in statically
   struct-typed slots: typed local, typed param, typed return, typed upvalue,
   typed vec/map element. The compiler rejects struct-at-dyn instead of
   auto-reifying.
3. **`[box $s]` is the only struct → heap path.** Produces a `JaclMutableRef`
   carrying the struct's `type_idx`. No type erasure occurs.
4. **`[unbox $val]` requires a `[box? Type $val]` flow-typing guard.** Outside
   a narrowed branch, `unbox` is a compile error.
5. **Ctx is a builtin `HeapRecord`.** It is not a user `defstruct`. It is
   embedder-definable with arbitrary fields (including reference fields). It is
   accessed via pointer-deref opcodes. The existing `JaclStruct`/`OBJ_STRUCT`
   GC object is renamed to `HeapRecord`/`OBJ_HEAP_RECORD` and survives only to
   support ctx (and any future runtime builtins of the same shape). There is
   no user-visible `Ptr T` syntax.
6. **`is_value_type` flag is removed.** All `defstruct`-created types are
   value-type by construction. Ctx is identified by `reg->ctx_type_idx`, not
   by a flag.

## Phasing

Each phase is independently shippable (passes tests).

### Phase 1 — Reject reference fields in `defstruct`

Reject `str/vec/map/closure/dyn` fields in user `defstruct` with a clear error
pointing users at `[box $val]`. Ctx remains exempt.

- `src/compiler.c:11530-11600` — main `defstruct` field loop.
- `src/compiler.c:611-650` — anonymous inline-struct path (struct-typed fields).
- `src/compiler.c:443-461` — ctx path stays as-is; add a comment that ctx is
  the lone heap-record exception.
- Reuse the existing `is_struct_value_type` predicate (compiler.c:204).

**Implementor must sweep:**
- `demo/`, `test/jacl/`, and any other `.jacl` sources for ref-field structs.
- Migrate any found to `[box $val]` + typed-collection patterns.

**Tests:**
- Flip `test_defstruct_ref_type_accepted` (test/test_compiler.c:4672) to
  `_rejected`. Assert error count > 0 with a useful diagnostic.

### Phase 2 — Carve ctx out as `HeapRecord`

Pure rename: `JaclStruct` → `HeapRecord`, `OBJ_STRUCT` → `OBJ_HEAP_RECORD`,
related accessors and helpers. Ctx is identified by `reg->ctx_type_idx`
(compiler.c:460); no new flag needed. Behavior-identical.

- `src/jacl.h:120` (enum), `:188` (typedef), `:1735-1736`, `:2037-2055`, `:2202-2203`
- `src/gc.c:39, 474, 476, 480`
- `src/gc_collect.c:366-448`
- `src/runtime.c:619`
- `src/vm.c:87-144` (ctx_pool), `:1006-1102` (read/write/reify helpers)
- `src/embed.c` (embed_struct_read/write_field)

**Risk:** ctx pool reuses freed `JaclStruct` headers — free-list walks in
`runtime.c:619` and `gc_collect.c:448` must agree on the renamed object kind.

### Phase 3 — Convert reify call sites to compile errors

Each `compiler__reify_inline_struct` call site needs to become either (a) a
compile error directing users to `box`, (b) a typed-variant op that handles
inline bytes directly, or (c) stay as-is if it's an architectural transient
that other phases will clean up.

**Status as of writing:** 3e landed. The other subphases turned out deeper
than the brief implied — see notes below.

- **3a. Box reifies first** (compiler.c:9123-9127). *Blocked on missing
  opcode.* Box currently consumes a heap HeapRecord pointer. Switching to
  inline bytes requires the var-ref load path (compiler.c:11055) to push
  inline bytes from a struct local, which today emits `OP_STRUCT_MATERIALIZE`
  (heap). Needs a new `OP_LOAD_INLINE_LOCAL` opcode and a sweep of
  consumers that currently expect a materialized heap pointer from var-ref.
- **3b/3c. Untyped vec/map literal** (compiler.c:5290, 5337, 5396, 5409,
  8494, 8528, 8634, 8667, 8714, 8722, 8754). These reify sites are inside
  TYPED collection construction (`[[Vec Point] ...]`, `[[Map Point] ...]`)
  and the typed `*_PUSH/*_SET/*_GET/*_HAS/*_REMOVE` ops. Today the typed
  ops take heap struct pointers; tomorrow they should take inline bytes.
  This is an *optimization*, not an error path — the user-facing behavior
  is correct; we're just allocating transient HeapRecords. UNTYPED vec/map
  storage of structs is already rejected via existing `reject_bare_typed`
  calls (compiler.c:5439, 8419, 8502, 8537, 8610, 8731).
- **3d. Destructure-from-dyn** (compiler.c:4566). Today destructure stores
  the source in a temp local and emits `OP_STRUCT_GET` (heap field access).
  Converting to error-on-dyn + inline-on-typed requires rewriting the
  destructure code to use a wide local + `OP_STRUCT_GET_INLINE`. Non-trivial.
- **3e. Dyn parameter passing** (compiler.c:10612). **DONE.** Errors with
  "cannot pass struct value to dyn parameter — wrap with [box $val]".
- **3f. Post-return-wide field access** (compiler.c:10195). The reify here
  fires when the compiler can't statically resolve the struct type at the
  field-access site after a wide return. Should be rare in well-typed code;
  improving static type tracking eliminates most cases.
- **3g. Ctx default-init** (compiler.c:12144). Ctx-only path; stays as-is
  (it's the ctx HeapRecord, not user struct).
- **Cell storage** (compiler.c:6407, 6455). `set` on a `mut`-bound struct
  variable currently reifies because cells store JaclVals (8 bytes). To
  keep mut-bindings of struct type without heap, would need wide cells —
  an architectural change beyond Phase 3 scope.
- **Global storage** (compiler.c:6535, 6986). Same as cell storage: globals
  are JaclVal-shaped.
- **Reset** (compiler.c:6505, 9302). Box reset currently goes
  inline → heap → box. Improvable along with 3a.

**Recommended next steps for full elimination:**
1. Add `OP_LOAD_INLINE_LOCAL` + `OP_LOAD_INLINE_UPVALUE` ops; rewrite
   var-ref of inline locals to use them. This unblocks 3a, 3d, and the
   reset cleanup.
2. Add inline-aware variants for typed vec/map ops to remove the transient
   heap allocations (3b/3c).
3. Decide on architecture for mut-struct bindings (wide cells vs. forced box).
4. Convert remaining sites to errors once the alternatives exist.

The current state — Phase 1, Phase 2, and Phase 3e — already eliminates
the *unintended* heap allocations for ref-bearing structs, keeps ctx as
the lone HeapRecord, and prevents the most common type-erasure path
(struct-to-dyn-parameter). Subsequent passes can chip away at the
remaining transient heap allocations without changing user-visible
behavior except where required by the design (e.g. `def dyn d [Point ...]`
becoming an error once a typed alternative is in place).

### Phase 4 — Delete dead opcodes & flag (deferred)

Blocked until Phase 3 fully lands: `OP_STRUCT_REIFY` and friends still
have callers from the unconverted reify sites.

Delete now-unreachable opcodes in `src/jacl.h:752-846` and their VM handlers:
- `OP_STRUCT_REIFY`
- `OP_STRUCT_NEW`, `OP_STRUCT_GET`, `OP_STRUCT_SET` (provided ctx now uses renamed `OP_HEAP_RECORD_*`)
- `OP_STRUCT_MATERIALIZE`, `OP_STRUCT_MATERIALIZE_UPVALUE`
- `OP_STRUCT_EXPAND`
- `OP_STRUCT_STORE_INLINE`
- Any `_DYN` struct variants

Rename ctx-only survivors to `OP_HEAP_RECORD_NEW/GET/SET`. Update VM dispatch
and compiler emitters.

Delete `is_value_type` (compiler.c:243). Collapse 16 use sites:
- 3184, 6852, 6883, 7299, 7366, 9262, 10351, 10594, 10693 → unconditional
  inline (always true post-cleanup)
- Ctx-specific check sites → `idx == reg->ctx_type_idx`
- Delete value-type computation loops at 644-650 and 11589-11595
- Delete the `is_value_type=false` ctx assignment at 455-456

Possibly removable helpers:
- `compiler__reify_inline_struct` (compiler.c:3985) — once all callers are gone
- `vm__reify_nested_structs` (vm.c:1052-1063)
- `vm__struct_to_slots` (vm.c:575)

### Phase 5 — Documentation

- Rewrite `STRUCT_DESIGN.md`'s "Proposed Architecture" framing in descriptive
  present-tense. Document the no-ref-field rule with the exact diagnostic.
  Document `[box]`/`[box? T]`/`[unbox]` as the only dyn boundary. Document
  ctx as the lone builtin heap record.
- Update `DESIGN.md` references to `OBJ_STRUCT`/`JaclStruct`/value-vs-heap split.
- Cross-check `SYNTAX_REDESIGN.md` and `SIMPLIFICATION.md` for stale references.
