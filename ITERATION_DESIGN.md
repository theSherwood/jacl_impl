# JACL Iteration Design — `for` over every collection

`for` is JACL's single iteration construct (it replaced `each`). This doc covers
the **unified binding rule** and the **lowering strategy** that lets one surface
work across streams, vectors, arrays, and maps — including the design principles
that generalize beyond `for`.

Surface syntax + status live in `SYNTAX.md` §"`for` — unified iteration" and
`NOT_IMPLEMENTED.md` §4; this is the *why*.

## The unified binding rule

> The **last** name is always the value/element. An optional **first** name is
> the *enumerator* — the thing that locates the element in that collection.

| collection | `for $c v` | `for $c e v` (enumerator + value) |
|---|---|---|
| vec / arr | element | `i32` **index** + element |
| map (typed or dyn) | **value** | **key** + value |
| stream | element | `i32` **0-based counter** + element |

So `for $vec v` / `for $vec i v` is `enumerate`, and `for $map v` / `for $map k v`
is `values` / `items` — the *same* surface, the enumerator's meaning chosen by
the collection. Zero names (`for $c { … }`) binds the implicit `$it`.

### Two deliberate, non-obvious choices

1. **A single name over a map binds the VALUE, not the key** — the opposite of
   Python's `for x in dict`. It follows the rule (last name = value) and keeps
   the form uniform; reach for the key with the two-name form. Flagged loudly in
   `SYNTAX.md` because it surprises people.
2. **Map iteration order is unspecified** (hash/HAMT order, matching
   `map-keys`/`map-vals`) — JACL maps are HAMTs, not insertion-ordered. An
   ordered-map variant would be a separate type, not a `for` feature.

## Lowering strategy

### Statically-typed maps — reuse, don't add opcodes

A `for` over a `TYPE_MAP` / `TYPE_TYPED_MAP` collection lowers to: evaluate the
map **once** into `__col`, materialize its values (`map-vals`, single name) or
its keys **and** values (`map-keys` + `map-vals`, two names), then iterate the
resulting vec(s) on the **existing vec-loop** scaffolding (`__idx < __len`,
`vec-get`). `keys[i]` ↔ `vals[i]` correspond because HAMT traversal is
deterministic for a fixed map. **No new VM opcodes** — and typed-element
narrowing + (eventually) SM/suspension support come for free from the vec path.

Cost: two materialized vecs (O(n) memory, snapshot semantics — clean, since
HAMTs are persistent). A lazy HAMT-iterator lowering would avoid them but drags
the depth-8 iterator struct into SM state; deferred until a workload needs it.

### Dyn collections — runtime tag dispatch, not type promotion

A statically-`dyn` `$coll` (e.g. a bare `def m [map a 1 b 2]`, which stays
`TYPE_DYN` like a bare dyn vec) doesn't reveal at compile time whether it's a map
or a sequence. Two ways to handle that were considered:

- **Promote bare map bindings to `TYPE_MAP`** (a typer change) — rejected: broad
  blast radius, and it papers over the fact that the kind genuinely isn't known
  statically.
- **Branch on the runtime tag** (chosen) — the `for` loop emits `OP_IS_MAP` on
  `__col`; if a map, replace `__col` with its `map-vals` and stash `map-keys`,
  then iterate as a dyn vec. The enumerator is resolved **per iteration** by the
  same runtime flag: the key (map) or the `i32` index (vec). All bindings are
  `dyn`.

`OP_IS_MAP` is the *one* new opcode in the whole arc — a runtime tag check
genuinely needs VM support, unlike the static-map lowering which needed none.

### The enumerator, mechanically

For vec/arr the enumerator is a **separate** local copied from the loop's
existing `__idx` each iteration (so body writes to it don't perturb the counter).
Streams carry no index, so a hidden 0-based `i32` counter is maintained and
incremented at the loop tail — **after** the continue target, so `continue`
advances it.

## What's deferred (all clean compile errors today, never silent)

- **Struct keys/values in a map for-loop** — would need inline-slot binding like
  the struct-element vec loop.
- **Suspending map-loop bodies + the enumerator form in a suspending body** —
  the SM (generator state-machine) lowering would need the materialized vecs /
  counter / second binding in state fields. Guarded in the SM path.
- **Lazy HAMT-iterator map iteration** — see above.

## Principles worth carrying forward

These generalize past `for`:

1. **Lower to existing primitives before adding a VM opcode.** The entire static
   map for-loop reused `map-keys`/`map-vals` + the vec loop — zero opcodes. Only
   the dyn runtime dispatch, which *fundamentally* needs a runtime tag check,
   earned a new opcode (`OP_IS_MAP`). Prefer desugaring/composition; spend
   opcodes only on things that can't be expressed with the ones you have.
2. **For `dyn`, dispatch at runtime — don't over-promote the static type.** When
   a value's concrete kind is only known at runtime, branch on the tag rather
   than forcing a static type that lies in the other cases. Keeps `dyn` honest
   and avoids wide-blast-radius typer changes.
3. **One rule, many collections.** The "last name = value, first = enumerator"
   rule made vec/arr/map/stream a single surface to learn and a single shape to
   implement (each branch just decides what the enumerator *is*). Find the rule
   that unifies before adding per-collection syntax.
4. **Snapshot semantics from persistent data.** Materializing `map-vals`/`keys`
   to iterate is safe precisely because HAMT maps are immutable — mutation during
   the loop produces a new map, never corrupts the snapshot. Persistent backing
   buys clean iteration for free.
