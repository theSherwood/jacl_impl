# Buffers & pointers on the SVM backend

How `[Buf N T]`, `[Ptr T]`, `addr`, and the `ptr-*` operators are lowered by the JACL→SVM
codegen (`codegen/codegen.c`) and its runtime (`runtime/`). This is a *model* of the old
VM's memory-backed buffers, not a byte-for-byte reimplementation — the differences are
called out below so the parity gaps are legible.

## The representation gap

The old bytecode VM lays a `[Buf N T]` out as **`N * sizeof(T)` contiguous bytes** of real
memory. A `[Ptr T]` is a machine address into that memory; `addr`, `ptr-deref`,
`ptr-offset`, and `ptr-diff` are pointer arithmetic over raw bytes, and a struct with a
buffer field embeds the bytes inline in the struct's payload.

The SVM backend has no exposed guest heap for JACL values to point into — every JACL value
is a tagged 64-bit `JaclVal`, and aggregates are GC objects. So a buffer is modelled as a
**`JaclVal` array** (the mutable-array runtime type, tag `0x1A`): `N` slots, each holding
one element's `JaclVal`. This keeps the GC sound (every element is a traced slot) at the
cost of the raw-address semantics.

What follows from that choice:

- **Element access works.** `$buf->i`, `set $buf->i v`, `[buf-get $b i]`, `[buf-set $b i v]`
  all lower to array index get/set. Bounds are lenient (vec-style): an out-of-range read is
  `nil`, an out-of-range write grows with nil/zero fill.
- **Zero-init is type-aware and recursive** (`emit_type_default` / `emit_field_default`).
  A declared `[Buf N T]` fills each slot with `T`'s zero: a numeric `0`, a `bool` `false`,
  a `str` `""`, a **struct** zeroed recursively (so `$pts->0` reads `[Point x 0 y 0]`), and
  a **nested `[Buf M U]`** as a fresh zero inner buffer (so `[Buf N [Buf M T]]` lays out
  real inner rows and `$m->i->j` chains through index access). A struct field of buffer type
  is stored by the parser as the canonical string `Buf{N,T}`, which `emit_field_default`
  parses back to build the inner buffer.

## Pointers

Two pointer flavours appear in the corpus, and they don't share one representation here:

1. **Raw address pointers** — `ptr-cast [Ptr T] $addr`, `ptr-null [Ptr T]`, `ptr-addr $p`.
   The old VM round-trips a real `u64` address through these. The backend treats them as the
   underlying integer: `ptr-cast`/`ptr-addr` are identity on the address value, `ptr-null` is
   `0`. Round-trips (`ptr-cast` then `ptr-addr`) match; the hex `Ptr<T>(0x…)` *print* form
   does **not** (there is no real address to render, and the pointee type is erased).

2. **Buffer pointers** — `addr $buf->i` yields a *fat pointer* `{ base, offset }`
   (`jacl_addr_of`, internally a 2-slot traced object over the buffer array). `ptr-deref`
   reads `base[offset]`; `ptr-offset $p n` advances the offset. This is enough to walk a
   buffer element-by-element the way a C extern would — `[ptr-deref [ptr-offset $p k]]`.

The two flavours are distinguished only by *which operator produced the value*, so mixing
them (e.g. `ptr-cast`-ing a buffer pointer, or arrow-indexing `$p->N` on a fat pointer) is
not modelled. Arrow-indexing on a pointer needs the runtime to tell a fat pointer from a
plain array at the access site — that requires a distinct pointer runtime type, which the
current tag scheme doesn't carry.

## What's still missing

- **Raw memory semantics.** `addr` of a scalar, cross-buffer `ptr-diff`, reading a
  multi-byte word through a `u8` pointer, and `ptr-cast` between element widths all assume
  a byte-addressable buffer. These need a linear-memory-backed buffer (an svm `memory`
  region + a bump allocator) rather than the `JaclVal`-array model.
- **`$p->N` arrow indexing on fat pointers** and the `Ptr<T>(0xADDR)` print form — both need
  a first-class pointer runtime type (a new heap tag), so `jacl_index_get` and the printer
  can recognise a pointer and act on it.
- **By-value buffer proc params** (`[Buf N T]` passed by value, mutations not escaping) —
  the array model is a reference, so a copy would have to be inserted at the call site from
  the param's declared type.

These are the buffer/pointer entries in `docs/SVM_PARITY_NOTES.md`'s difficulty map; the
element-access and zero-init half of the surface is done, the raw-memory half is not.
