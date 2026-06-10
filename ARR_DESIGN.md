# JACL Mutable Array Type Design

`[Arr T]` is a heap-allocated, growable, **mutable reference** array of
`T`, backed by `lib/segment_array/`. It completes the array family:

| Type | Mutability | Backing | Identity | Use |
|---|---|---|---|---|
| `[Buf N T]` | mutable, fixed N | inline / ABI-flat | value (inline) | FFI, fixed blocks |
| `[Vec T]` | immutable, growable | RRB tree (GC) | value (persistent) | functional data |
| **`[Arr T]`** | **mutable, growable** | **segment array (GC)** | **reference (aliased)** | imperative accumulation, in-place mutation |

Where `[Vec T]` returns a *new* root on every `push`/`set`, `[Arr T]`
mutates in place and is shared by reference — `def b $a` aliases the
same object, and a proc receives the same object the caller holds.

Cross-references: `TYPE_SYSTEM.md` (typer pipeline & invariants),
`BUFFER_DESIGN.md` (the most recent mutable-collection precedent — its
ref-element write-barrier path is the template here),
`GC_CONCURRENCY_DESIGN.md` (SATB + generational barriers, finalizer
seam), `NOT_IMPLEMENTED.md` §2 (the entry this doc scopes — point it
here once M1 lands), `lib/segment_array/segment_array.h` (the backing
store).

---

## Status snapshot (for session handoff)

**Pickup for the next session (as of 2026-06-10):** `[Arr T]` is
functionally complete — **M0–M5 done + M6 for-loop done**, on `main` and
green. Dyn (`[arr ...]`), flat typed-scalar (`[Arr i32]`…), and flat
typed-struct (`[Arr Point]`) arrays all work: construct / get / set /
push / pop / len, plus print / to-string / identity-eq / nesting /
`for`-loop iteration. Scalar get/pop narrowing is complete (all scalars,
incl. wide i64/u64/f64 — Phase 1 carve-out removed). The `[[Arr T] …]`
constructor + push/set now box wide float/large-int literals (pre-existing
zero-store bug, fixed Phase 1). **Ref-kind mirror landed (2026-06-10):**
`arr` ≡ `[Arr dyn]`, and `[Arr str]` / `[Arr dyn]` are legal — ref
elements use the DYN arr rep (tagged traced slots; no flat-bytes rep win)
with the element type carried statically as TYPE_ARR + stamp (the
`[Vec str]` erasure scheme): ctor element checks, `def [Arr str]`
annotations (the compiler def path now recognizes `[Arr …]` as a compound
annotation — it errored "def type must be a keyword" before), def-binding
propagation, arr-get/arr-pop narrowing, push/set expected-type + stamp
persistence, for-binding narrowing. **No-autobox rule:** storing an
unboxed wide scalar (i64/u64/f64) in a dyn arr (or any dyn collection
sink) is a COMPILE ERROR — explicit `[box …]` is the only dyn form of a
wide scalar; declared typed slots ([[Arr i64]] stores) keep their
internal box-before-byte-pack bridging. Tests: `arr_ref_elems.jacl`,
`arr_ref_elem_error.jacl`, `arr_wide_dyn_error.jacl`.

- **Verify state:** `timeout 240 ./build.sh` → 91/91 binaries; full jacl
  suite 474/474. TSAN (`./build.sh --tsan`) race-clean in arr paths; only
  the pre-existing known-safe `chase_lev` test-helper race remains (not
  arr-related).
- **Tests:** `test/jacl/arr_*.jacl` (basic, alias, oob, print,
  to_string, nested, typed_scalar, typed_i64, typed_i64_narrow,
  typed_f64, typed_oob, typed_mismatch, typed_struct, for_scalar,
  for_struct, for_wide) + `lib/segment_array/test_segment_array{,_var}.c`
  + the `[Arr T]` block in `tour.jacl`.
- **Start here:** **M6 arrow** — `$a->i` / `set $a->i x` heap-deref
  indexing (the for-loop half of M6 is done). Also optional: `each`/
  `transform`/`filter` over arr, and the deferred wide for-binding
  narrowing (needs the wide-cell + SM state-field work, §4).
- **Gotchas to know before editing:**
  - `JaclType` / `OpCode` / `HeadId` are each **dual-defined**
    (jacl.h + ast.c / bytecode.c). Edit both; append new opcodes at the
    enum END (`test_bytecode` pins opcode numbers). See the memory note
    `project_jacltype_multidef`.
  - **Cross-registry idx hazard:** struct element idx for opcode
    operands comes from the *compiler* registry (`struct_registry__find`),
    not the typer's.
  - **One intentional carve-out** (don't "fix" without reading why):
    struct arrays are pure value types (JACL structs can't hold ref
    fields) so the GC recursion / ref-store barriers are
    kept-for-correctness no-ops.
  - **Scalar narrowing now complete (2026-06-08, Phase 1).** The former
    i64/u64/f64 "return dyn" carve-out is **removed** — `arr-get`/`arr-pop`
    on `[Arr i64/u64/f64]` now narrow to the unboxed **wide** scalar,
    matching `vec-get`. `vm__arr_scalar_load` pushes raw 8-byte bits for
    i64/u64/f64 (was heap-boxing); the typer/compiler narrow all scalars
    and rely on the existing `ensure_boxed` (`OP_TO_DYN`) bridges at dyn
    sinks. Same change fixed a **pre-existing construct/push/set bug**:
    unboxed wide float literals (`OP_CONST_F64`) and large i64/u64
    literals fell through `vm__arr_scalar_store`'s tagged-only branches
    and stored `0`; the scalar `[[Arr T] …]` constructor + `arr-push`/
    `arr-set` now `ensure_boxed` each element before the byte-packed
    store. Tests: `arr_typed_i64_narrow.jacl`, `arr_typed_f64.jacl`.

| Milestone | Status | Notes |
|---|---|---|
| **M0** Design (this doc) | ✅ done | Four gating decisions settled 2026-06-08 (see Decisions) |
| **M1** Library gaps | ✅ done | `SA_SET` (in-place index write) + order-preserving `SA_POP` added to `segment_array.h` (25/25 lib tests). |
| **M2** Type plumbing | ✅ done | `TYPE_ARR` / `TYPE_TYPED_ARR`, `TYPE_SHAPE_TYPED_ARR` + `tarr` union, `type_shape_intern_typed_arr`. |
| **M3** Dyn `[arr ...]` MVP | ✅ done | Construct / get / set / push / pop / len for tagged-JaclVal elements; `OBJ_ARR` GC object kind + trace + finalizer; SATB + generational write barriers; identity eq; print. Main build 90/90, TSAN race-clean (only the known chase_lev_stress flake). |
| **M4** Typed `[[Arr T] ...]` — **flat bytes** | ✅ done | Decision 2026-06-08: typed arrays store **raw element bytes** (not boxed), matching `[Buf N T]` / typed-vec. Delivered across M4a–M4e below. |
| ⮑ **M4a** Runtime-stride segment array | ✅ done | `sa_var` (`lib/segment_array/segment_array_var.h`) — runtime `elem_size`; 5/5 lib tests. |
| ⮑ **M4b** Unify `JaclArr` on `sa_var` | ✅ done | Dyn path refactored onto the byte backing (elem_size = 8, JaclVal slots); M3 tests intact. |
| ⮑ **M4c** Type-position + typer | ✅ done | `typer__arr_type`; `def [Arr T]` → `TYPE_TYPED_ARR` + elem_idx; `[[Arr T] ...]` constructor; get narrows, push/set type-check; element-literal flex. |
| ⮑ **M4d** Typed byte ops | 🟡 scalar done | `OP_TYPED_ARR` construct + get/set/push/pop/len byte path for scalar elements (i32/i64/u32/u64/f32/f64/bool) via `vm__arr_scalar_{store,load}`. **Struct elements (`[Arr Point]`) deferred (M4d-2)** — constructor/ops error clearly. |
| ⮑ **M4e** GC trace + tests | 🟡 scalar done | Trace branches on elem_idx: dyn → push JaclVals, scalar → skip (no refs). Struct recursion deferred (M4e-2). Tests: typed_scalar/i64/oob/mismatch. TSAN race-clean in arr paths. |
| ⮑ **M4d-2/e-2** Struct-element typed arrays | ✅ done | `[Arr Point]`: flat `width*8`-byte slots; `OP_TYPED_ARR_PUSH/SET`; inline-struct get/pop/construct; GC ref-bitmap recursion. JACL structs are pure value types (no ref fields), so the recursion/barriers are effectively no-ops but kept for correctness. Build 91/91, TSAN race-clean in arr paths. **Update (Phase 1, 2026-06-08):** the wide-cell caveat is **resolved** — `arr-get`/`arr-pop` on `[Arr i64/u64/f64]` now narrow to the unboxed wide scalar (see the "Scalar narrowing now complete" note in the Pickup block). |

### Struct-element typed arrays (M4d-2 design)

Store each struct element as `vm__struct_width(sdef) * sizeof(JaclVal)`
bytes (the wide inline form, matching typed-vec). `elem_size` is set
accordingly; `elem_idx` is the **compiler/runtime struct registry idx**
(`struct_registry__find`, not the typer's — sidesteps the cross-registry
hazard, exactly as the typed-vec struct constructor does).

Op split (the key constraint): a struct element occupies `width` stack
slots *above* the receiver, so `push`/`set` must know `width` **before**
popping — which the unified `OP_ARR_PUSH`/`OP_ARR_SET` can't (they pop a
single `elem` slot first). So struct arrays need **`OP_TYPED_ARR_PUSH`
/ `OP_TYPED_ARR_SET`** carrying `u16 type_idx` (mirroring
`OP_TYPED_VEC_PUSH`). `get` / `pop` / construct (`OP_TYPED_ARR`) / `len`
stay unified — they pop the receiver first, then derive `width` from the
object's `elem_idx`:
- construct: `vm__pop_struct` each element into a scratch buffer, then
  `sa_var_push` the `width*8` bytes.
- get: bounds-**checked** (OOB errors — a missing struct can't be `nil`
  inline, unlike scalar/dyn get; matches `OP_TYPED_VEC_GET_INLINE`);
  push `width` inline slots (`memset` + `memcpy total_size` + set
  `inline_slot_bitmap`).
- pop: `sa_var_pop` into a temp, push `width` inline slots.

GC (M4e-2): `OBJ_ARR` trace's struct branch recurses
`gc__push_record_refs(ms, base + i*elem_size, sreg, elem_idx)` per live
element (the buf struct-element walker). Ref-store barriers fire only
when the struct contains ref fields.
| **M5** Identity & polish | ✅ done | `to-string` (wired), tour.jacl `[Arr T]` section, nested-arr + to-string tests (identity eq + print landed in M3). |
| **M6** Iteration + arrow | 🟡 for-loop done | `for [a] x { }` iteration shipped (dyn/scalar/struct elements, binding narrowing for ALL scalars incl. wide i64/u64/f64, in plain + suspending procs — see below). `$a->i` / `set $a->i x` arrow still todo. |

### M6 status (iteration + arrow ergonomics)

**for-loop — done (2026-06-08).** `for [a] x { }` / `for [a] { ... $it }`
is a dedicated branch in the inlined for-loop (`compiler.c`, right before
the "Vector-based" branch) using `OP_ARR_LEN` / `OP_ARR_GET` (both read
element size/ref-ness off the object's `elem_idx`, so no per-kind opcode).
Three element kinds, two binding shapes:
- **dyn `[arr ...]`** → dyn binding, `OP_ARR_GET` + `OP_SET_LOCAL`.
- **tagged scalar `[Arr i32/u32/f32/bool]`** → binding **narrows** to the
  scalar (typer + compiler), enabling typed body math / `assert-type`.
- **wide scalar `[Arr i64/u64/f64]`** → binding **narrows** too, in both
  plain and suspending procs. Non-SM loops store the wide binding in a
  typed local (raw bits); SM loops store it **boxed** in the GC-traced
  state field and the var-ref read unboxes via `node->inferred_type`.
  This rides on a general **wide-cell + SM fix** (same session): `def`/
  `mut` wide locals keep their narrowed type across a suspension (boxed on
  store, unboxed on read), the `mut i64`-across-suspension `nil` bug is
  fixed, and wide scalars returned from a dyn-return proc are boxed at the
  return boundary. See `NOT_IMPLEMENTED §4`.
- **struct `[Arr Point]`** → inline-width binding (`OP_INLINE_TO_LOCAL`,
  mirroring `is_typed_vec_loop`); `$p->field` works in the body.

Break/continue/break-value and suspending (SM) for-loops all work; the
typer narrows the binding via a `HEAD_FOR` case (all scalars + struct).
Tests: `arr_for_scalar.jacl`, `arr_for_struct.jacl`, `arr_for_wide.jacl`,
`sm_wide_scalar.jacl`, `wide_proc_return.jacl`, plus a tour.jacl line.
`each`/`transform`/`filter` over arr (vm.c ~5623+) remain a parallel
follow-up.

**arrow — todo.** `$a->i` / `set $a->i x`: buf's arrow lowering assumes an
inline base-slot + fixed stride; arr needs a new heap-deref path that
lowers to `arr-get` / `arr-set`. Builtins are the supported surface for now.

### Flat-bytes typed-array storage (M4 design)

`segment_array.h` fixes element size at instantiation via `sizeof(T)`.
Typed arrays need a stride known only at runtime (`[Arr i32]` → 4,
`[Arr Point]` → `sizeof(Point)`), so M4a adds a **runtime-stride
segment array** (`sa_var`): identical index math, but segments are
sized in *elements of `elem_size` bytes* (segment k holds
`2^(6+k)` elements of `elem_size` each), so an element never straddles
a segment boundary. get/set/push/pop `memcpy` `elem_size` bytes.

**Unified backing (M4b).** `JaclArr` switches to a single `sa_var`
with `elem_size` set per array: dyn arrays use `elem_size = 8` storing
tagged `JaclVal`s (so M3 behavior is preserved); `[Arr i32]` uses 4;
`[Arr Point]` uses `sizeof(Point)`. `elem_idx` (the existing field)
distinguishes them and drives ops + GC.

**Ops (M4d).** Mirroring typed-vec / buf element access:
- get: dyn → read the `JaclVal`; scalar → read raw bytes, rebuild the
  tagged scalar; struct → push inline struct bytes (`INLINE_STACK`).
- set/push: dyn → store `JaclVal` (+ barriers); scalar → write raw
  bytes from the popped value; struct → `memcpy` inline struct bytes.
- Bounds stay lenient; OOB grow zero-fills (typed scalars/structs get
  zero bytes, not `nil`).

**GC (M4e).** The `OBJ_ARR` trace branches on `elem_idx`: dyn → push
every 8-byte slot as a `JaclVal` (M3 behavior); typed scalar → no
tracing (raw bytes); typed struct → walk each element at `elem_size`
stride via `gc__push_record_refs` (the buf struct-element walker).
Ref-store barriers fire only on the dyn / ref-containing-struct paths.

What should work end-to-end when M5 lands:

```jacl
proc main {} {
  def a [arr 1 2 3]        ; dyn mutable array
  def b $a                 ; b ALIASES a (reference type)
  arr-push $b 4
  print [arr-len $a]       ; 4   — mutation visible through a
  arr-set $a 0 99
  print [arr-get $b 0]     ; 99  — shared storage
  print [eq $a $b]         ; true (identity)
  print $a                 ; [arr 99 2 3 4]
  print [arr-pop $a]       ; 4   — order-preserving tail remove

  def [Arr Point] pts      ; typed mutable array
  arr-push $pts [Point x 1 y 2]
  print [arr-get $pts 0]   ; [Point x 1 y 2]
}
```

---

## Decisions (settled 2026-06-08)

These four were the gating questions before implementation. Recorded
here so they are not re-litigated.

### 1. Identity / equality — reference type, identity `eq`

`[Arr T]` is a true mutable reference, modeled on `atom`/`box` rather
than on the value-typed collections:

- **Aliasing.** `def b $a` makes `$b` and `$a` the same object;
  mutating through one is visible through the other.
- **Pass by reference.** A proc that takes an `[Arr T]` receives the
  caller's object, not a copy. Mutations persist after return.
- **`eq` is identity.** `[eq $a $b]` is true iff they are the same
  object. `[eq $a [arr 1 2 3]]` is **false** even with equal contents.
  (Contrast `[Vec T]`, which is structural.)

Consequences (decided, not re-asked):

- **Not a structural map key.** Unlike `[Vec T]`, an `[Arr T]` is not
  usable as a structural map key — a mutable object whose hash could
  change underfoot is a footgun. If keying is ever wanted it would be
  identity-keyed; out of scope for now. `arr` does not get a structural
  hash registered in `collections.c`.
- **Print shows contents.** `print` / `to-string` still render the
  elements (`[arr 1 2 3]`) even though `eq` is identity — identity
  governs comparison, not display.

### 2. Indexing surface — builtins first, arrow later (both)

- **Phase 1 (M3–M5):** builtins, mirroring vec —
  `[arr-get $a $i]`, `arr-set $a $i $x`, `arr-push $a $x`,
  `arr-pop $a`, `[arr-len $a]`. Lowest risk; maps directly onto the
  heap segment-array layout.
- **Phase 2 (M6):** arrow sugar — `$a->i` (read) and `set $a->i x`
  (write). Buf's arrow lowering assumes an inline base-slot + fixed
  stride; `[Arr T]` needs a **new arrow-lowering path** that derefs the
  heap object and calls the same get/set machinery. Deferred so the
  core ships first.

### 3. Concurrency — single-owner, unsynchronized

`segment_array` is not data-race-safe and we do not add internal
locking.

- The GC write barriers make a shared `[Arr T]` **GC-correct** (no
  use-after-free / lost-mark), but **concurrent mutation from two
  coroutines is a documented user error**, not a supported pattern.
- To share a mutable array across coroutines, wrap it in an `atom`
  (`def shared [atom [arr]]`) and go through atom's synchronized
  swap/reset, exactly as for any other mutable value.
- This keeps the mutating hot path a plain memory write — no per-op
  lock.

### 4. Bounds & growth — lenient, mirror vec

- `arr-get` OOB → `nil` (forgiving read).
- `arr-set` OOB → **grow** (fill intervening slots with `nil` for dyn;
  for typed arrays the gap is zero-filled, mirroring whatever
  `vec-set`'s typed path does — see Element-type rules).
- `arr-pop` on empty → `nil`.
- Negative index → error (mirrors `vec-set`'s negative-index error).

---

## Surface

```
[arr]                 ; empty dyn array
[arr 1 2 3]           ; dyn array literal
[$a]                  ; dyn array from spread (mirrors [$v] for vec)
[Arr T]               ; typed array, declaration / type position
[[Arr T] 1 2 3]       ; typed array literal

[arr-get $a $i]       ; read element i           -> T | nil (OOB)
arr-set $a $i $x      ; write element i in place  (grows if OOB)
arr-push $a $x        ; append                    -> see return convention
arr-pop  $a           ; remove + return last      -> T | nil (empty)
[arr-len $a]          ; element count             -> i32
```

**Return conventions for mutating ops** (mutating ops, unlike vec's
functional ops, do not return a new collection):

- `arr-push` returns the new length (`i32`) — useful and cheap; avoids
  implying it returns a fresh array.
- `arr-set` returns the array (`$a`) to allow chaining, OR `nil`.
  **Pick `$a`** for chaining symmetry; revisit if it confuses the typer.
- `arr-pop` returns the removed element (or `nil` if empty).

---

## Type encoding

Mirror the `[Vec T]` two-tag split exactly:

- `TYPE_ARR` — dyn array; elements are tagged `JaclVal`s.
- `TYPE_TYPED_ARR` — typed array; elements are raw bytes (scalar or
  struct), element type carried out-of-band.

Element type flows through the **shared shape registry** (the same one
buf and typed-vec use — see `project_dual_struct_definitions` /
`project_cross_registry_idx_hazard` cautions):

- Add `TYPE_SHAPE_TYPED_ARR` to `TypeShape` (`shapes.c` + `jacl.h`,
  kept in sync), holding `{ uint32_t elem_idx; }` exactly like
  `TYPE_SHAPE_TYPED_VEC`.
- `elem_idx` uses the standard encoding: scalar sentinel
  (`JACL_SCALAR_TYPE_IDX`) for i32/u8/…, registry idx for struct /
  nested collection element types.
- The heap object stores `elem_idx` so the GC trace and the
  get/set paths know element size and ref-ness at runtime.

Typer rules mirror typed-vec (`typer.c`): construction preserves
`TYPE_TYPED_ARR` + `elem_idx`; `arr-get` narrows to the element type
for typed arrays and `TYPE_DYN` for dyn arrays; `arr-push`/`arr-set`
type-check the element against `elem_idx`.

---

## Storage & GC integration

**One GC object per array.** The heap object is the `SA_T` header
(count, segment pointers, cached cursor) plus an `elem_idx` field and
the standard `GCHeader`. New object kind: `OBJ_SEGMENT_ARRAY` (one kind;
the dyn-vs-typed distinction is read from `elem_idx`, avoiding a second
kind — but split into `OBJ_ARR` / `OBJ_TYPED_ARR` if the trace fn gets
cleaner that way, mirroring `OBJ_RRB_LEAF` / `OBJ_TYPED_RRB_LEAF`).

**Segments are malloc-backed, freed via the finalizer seam.**
`segment_array` allocates segments through its `Allocator*`. The
segments are *not* individual GC objects; they are external resources
owned by the one `SA_T` GC object. `gc__finalize_dead`
(`gc_collect.c:86`, currently a no-op stub) is the seam: extend it to
`SA_FREE` the segments when an `OBJ_SEGMENT_ARRAY` is swept. This is
exactly the "release external resources before sweep zeroes memory"
case the hook was built for.

**Tracing.** The trace fn walks `segments[]` for the live `count`
elements:

- Dyn (`TYPE_ARR`): each element slot is a tagged `JaclVal` → push it
  via the mark stack (mirrors `OBJ_RRB_LEAF`).
- Typed scalar (`[Arr i32]`): raw bytes, no refs → no tracing
  (mirrors `OBJ_TYPED_RRB_LEAF`).
- Typed struct (`[Arr Point]`): recurse per element at the struct's
  byte stride via `gc__push_record_refs` (the same walker buf uses for
  struct-element bufs).

Reads of element slots use ACQUIRE atomics, matching buf's GC walker,
so the concurrent collector sees consistent values.

**Write barrier.** On every ref-storing mutation (`arr-set` /
`arr-push` where the element is a heap type, and the dyn case where any
element may be), call `gc_write_barrier(grey_buf, gc_active_ptr, old,
new)` **before** the store — copy buf's `OP_BUF_SET_LOCAL` ref-element
path (`vm.c` ~3244). Because the array is a heap container that can be
old-gen, also fire `gc_remembered_set_barrier` on ref stores so a
young element reachable only through an old-gen array survives a minor
GC.

**Concurrency note.** Single-owner mutation races the concurrent
marker only in benign ways: segment pointers are stable (never moved —
`segment_array`'s core guarantee), `push` only appends, and the SATB
insertion barrier pushes any newly stored ref. A marker that reads a
`count` one element stale therefore cannot miss a live reference. No
extra synchronization is required for *GC* correctness; cross-coroutine
*data* races remain the documented user error (Decision 3).

---

## Element-type rules

Reuse buf / typed-vec rules verbatim:

- Small int element types (`u8`/`i8`/`u16`/`i16`) store narrowed; see
  `BUFFER_DESIGN.md` "Small int types".
- `arr-set` OOB growth on a typed array zero-fills the gap (no `nil`
  for a typed scalar slot); for a dyn array the gap is `nil`.
- Element type-check on `push`/`set` against `elem_idx`, producing the
  same mismatch error shape as typed-vec push.

---

## Implementation order

### M1 — Library gaps (`lib/segment_array/segment_array.h`)
Add two operations the builtin set needs but the library lacks:
- `SA_SET(sa, index, value)` — in-place write; `*SA_GET(sa, index) =
  value`. (Growth path handled in the VM, not the library.)
- `SA_POP(sa)` — order-preserving tail remove + return; decrements
  `count` and maintains the cached cursor. (`SA_SWAP_REMOVE` already
  exists for unordered removal but reorders — not what `arr-pop` wants.)
Add unit coverage in `test_segment_array.c`.

### M2 — Type plumbing (no runtime behavior)
- `TYPE_ARR`, `TYPE_TYPED_ARR` (`jacl.h`); `TYPE_SHAPE_TYPED_ARR`
  (`shapes.c` + `jacl.h`, synced); intern helper paralleling
  `type_shape_intern` for typed-vec.
- Heads: `HEAD_ARR`, `HEAD_ARR_GET`, `HEAD_ARR_SET`, `HEAD_ARR_PUSH`,
  `HEAD_ARR_POP`, `HEAD_ARR_LEN` (`ast.c`); type-keyword `Arr`.
- Parser: `[Arr T]` in type position (mirror `[Vec T]`, `parser.c`).
- Typer: declaration → `TYPE_TYPED_ARR` + elem_idx; narrowing table
  entries. No codegen yet.

### M3 — Dyn `[arr ...]` MVP
- `JACL_TAG_ARR` value tag; `OBJ_SEGMENT_ARRAY` GC kind; `jacl_arr_ptr`
  / `jacl_is_arr` (`value.c`).
- `collections.c`: instantiate `segment_array` template for
  `JaclVal` elements (GC allocator).
- Ops `OP_ARR`, `OP_ARR_GET`, `OP_ARR_SET`, `OP_ARR_PUSH`,
  `OP_ARR_POP`, `OP_ARR_LEN` — compiler emit + VM handlers.
- GC: trace (dyn leaf) + finalizer free in `gc__finalize_dead`.
- Write barrier on ref stores.
- Bounds policy (Decision 4).

### M4 — Typed `[[Arr T] ...]`
- `OP_TYPED_ARR*` variants carrying `elem_idx` (or fold into the dyn
  ops reading the object's `elem_idx` — prefer the latter if the VM
  handler can branch on the object, fewer opcodes).
- Raw-byte scalar leaves (no trace) and struct leaves (recurse).
- Typer element-type narrowing + push/set type-check.

### M5 — Identity & polish
- Identity `eq` (pointer compare); ensure `arr` is *not* added to the
  structural-hash / structural-eq paths.
- `print` / `to-string` rendering (`[arr …]`).
- Error messages (OOB negative, type mismatch).
- `tour.jacl` section + `test/jacl/arr_*.jacl` suite paralleling the
  `vec_*` tests (construct, get, set, push, pop, len, oob, alias/
  identity, typed-struct element, type-mismatch, arity).

### M6 — Arrow ergonomics (Phase 2)
- `$a->i` read + `set $a->i x` write via a heap-deref arrow lowering
  (new path; buf's assumes inline base+stride).
- for-loop / `iter` integration (stream bridge or dedicated iterate op).

---

## Open questions / deferred

- **`arr-set` return value.** Tentatively returns `$a` for chaining;
  if that complicates typer flow, switch to `nil`. Decide during M3.
- **Single vs split typed opcodes (M4).** Prefer one opcode family that
  reads `elem_idx` off the object over a parallel `OP_TYPED_ARR*` set;
  confirm the VM handler can branch cleanly before committing.
- **`arr-insert` / `arr-remove(i)` / `arr-clear`.** Not in the initial
  builtin set. `swap_remove` exists in the library (unordered); an
  ordered `remove(i)` is O(n). Add post-M5 if a workload pulls.
- **Cross-module element-type alignment** for typed arrays exported
  from another file — same carve-out as imported-struct field-typing
  and exported streams (`NOT_IMPLEMENTED.md` §4); deferred.
