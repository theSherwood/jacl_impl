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

Each `compiler__reify_inline_struct` call site becomes a typed compile error
telling the user to `box`. One subphase per commit.

- **3a. Box reifies first** (compiler.c:9123-9127). Internal cleanup; box
  accepts inline structs directly via `OP_BOX_STRUCT`.
- **3b. Untyped vec leaf** (compiler.c:5044, 5285, 5332, 5391, 5404). Error:
  `struct value cannot be stored in untyped vec — declare 'vec<TypeName>' or use [box $val]`.
- **3c. Untyped map** (compiler.c:8416, 8489, 8523, 8629, 8662, 8709, 8717, 8749).
  Mirror of 3b for map values.
- **3d. Destructure-from-dyn** (compiler.c:4560, 4002). Error:
  `cannot destructure struct from a dyn slot — narrow with [box? Type $val] then [unbox $val] first`.
  Also flip the typed-source path's `OP_STRUCT_GET` (compiler.c:4692) to
  `OP_STRUCT_GET_INLINE`.
- **3e. Dyn parameter passing** (compiler.c:10607-10609, 10384, 6402-6530, 7299/7366).
  Error: `struct value cannot be passed to a dyn parameter — wrap with [box $val] or change the parameter type`.
  **Highest-volume site; expect 10+ test migrations.**
- **3f. Post-return-wide field access** (compiler.c:10189-10231). Use
  `OP_STRUCT_GET_INLINE` against the wide-return slot when typing is known.
  Error only if genuinely lost.
- **3g. Ctx field default-init** (compiler.c:12119-12125). Rewrite to use the
  renamed `OP_HEAP_RECORD_SET` directly without inline-then-reify.

### Phase 4 — Delete dead opcodes & flag

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
