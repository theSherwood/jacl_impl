# SVM runtime builtins — the codegen↔runtime ABI

Every JACL head the SVM codegen (`codegen/codegen.c`) can't lower inline becomes a
`call.import` to a `jacl_*` C entry point in `runtime/` (translated to SVM IR by
`svm-llvm` and linked into the program module). The uniform ABI is:

> `jacl_fn(i64 sp, i64 a0, i64 a1, …) -> i64`

The leading `sp` is the data-stack pointer (§3d ABI); every argument and result is a
`JaclVal` (a tagged 64-bit word — see `runtime/jaclrt.h`). Entry points **fail closed**:
on a type mismatch or bad input they return an error-flagged value rather than misread a
foreign pointer, so a head can be wired before its full static check exists without
risking memory unsafety.

This reference groups the entry points the codegen dispatches to. It is a map for the
next contributor picking a head off the `docs/SVM_PARITY.md` worklist — not exhaustive
runtime internals.

## Arithmetic & comparison (`runtime/builtins.c`)

| head | entry point | notes |
|---|---|---|
| `+ - * / %` | `jacl_add` / `sub` / `mul` / `div` / `mod` | i32 fast path; promotes to i64/u64/f64 when an operand is wide; error-propagating |
| `< <= > >= == !=` | `jacl_lt` / `le` / `gt` / `ge` / `eq` / `ne` | structural `==` for strings/vecs/maps/structs; **identity** for reference arrays |
| `~` / `not` | `jacl_not` | boolean negation |

## Collections

| head | entry point |
|---|---|
| `vec` literal / `vec-push` | `jacl_vec_empty` + `jacl_vec_push` / `jacl_vec_push_v` |
| `vec-get` / `vec-set` / `vec-len` | `jacl_vec_get_at` / `jacl_vec_set_at` / `jacl_len` |
| `vec-slice` / `vec-concat` | `jacl_vec_slice` / `jacl_vec_concat` |
| `arr` / `arr-push` / `arr-get` / `arr-set` / `arr-pop` | `jacl_arr_new` / `jacl_arr_push_v` / `jacl_arr_get_at` / `jacl_arr_set_at` / `jacl_arr_pop` |
| by-value `[Buf N T]` param copy | `jacl_arr_copy` (deep over nested buffers; no-op on non-arrays) |
| `map` literal / `map-get` / `map-set` / `map-has` / `map-remove` / `map-keys` / `map-vals` | `jacl_map_*` |
| `index` / `slice` | `jacl_index_op` / `jacl_slice_op` (string vs vec/arr) |
| `first` / `count` / `length` | `jacl_first` / `jacl_len` |
| `range` / `range-inclusive` | `jacl_range_vec` / `jacl_range_inclusive` |
| `for`-each element / key | `jacl_iter_val_at` / `jacl_iter_key_at` (vec/arr index, map entry) |
| `..$v` operator spread | `jacl_vec_reduce` (folds an op over a vec/arr, or drains a generator) |

Array bounds are lenient (vec-style): OOB get → nil, OOB set grows with fill, pop-empty → nil.

## Strings

`jacl_str_new`, `jacl_str_concat` (error-propagating), `jacl_str_index`, `jacl_str_slice`,
`jacl_to_string`, `jacl_lines` (CRLF-aware). Interpolation folds `jacl_str_concat` over
`jacl_to_string` of each segment.

## Records, boxes, errors, ctx

| head | entry point |
|---|---|
| struct ctor / `->` field | `jacl_struct_new` + `jacl_struct_init_field` / `jacl_field_get` / `jacl_struct_put` |
| `?.` / dynamic `->` | `jacl_qdot` / `jacl_dot_dyn` / `jacl_dot_dyn_set` |
| destructure name-or-index | `jacl_field_or_index` |
| `box` / `atom` / `deref` / `reset` / `swap` | `jacl_box_new` / `jacl_atom_new` / `jacl_box_get` / `jacl_box_set` |
| `error` / `error?` / `error-val` | `jacl_error_new` / `jacl_is_error_v` / `jacl_error_val` |
| `ctx` / `with-ctx` | `jacl_ctx_get` / `jacl_ctx_set_field` / `jacl_ctx_swap` |
| `to` cast / typed-def widen | `jacl_to_cast` / `jacl_widen_to` |

## Buffers & pointers

`[Buf N T]` is an array (see `docs/SVM_BUFFERS.md`). `addr $buf->i` → `jacl_addr_of`
(a `{base, offset}` fat pointer); `ptr-deref` → `jacl_ptr_deref`; `ptr-offset` →
`jacl_ptr_offset`. `ptr-cast` / `ptr-null` / `ptr-addr` treat pointers as raw addresses
(identity / 0).

A `[Buf N T]` passed to a proc expecting `[Ptr T]` **decays** to the bare array (C-style,
the pointer is `&buf[0]`): `jacl_ptr_deref` reads element 0 of a bare array, and
`jacl_ptr_offset` treats it as `{buf, 0}` before advancing, so a decayed buffer walks
element-by-element exactly like a fat pointer. A `[Buf N T]` param proper (not `[Ptr T]`)
is copied by value in the callee prologue (`jacl_arr_copy`), so callee writes don't escape.

## Control, streams, concurrency, host

- Streams are **eager**: `collect` / `transform` / `filter` / `take` materialise a vector
  immediately (the corpus collects lazily-built pipelines right away, so this is
  output-equivalent). Generators are fibers (`jacl_gen_new` / `jacl_gen_next` /
  `jacl_gen_done`).
- `spawn` / `await` / `parallel` / `race` map to `jacl_spawn` / `jacl_await` /
  `jacl_parallel` / `jacl_race` over the M:N worker pool (`runtime/sched.c`). These run on
  the JIT but hang under the single-threaded interpreter, so the interp==jit oracle can't
  score them yet.
- `print` → `jacl_print` → the powerbox `Stream.write` on the stashed stdout handle. Error
  values render as `<error: PAYLOAD>`; boxes/atoms as `<box: V>` / `<atom: V>`.
- Runtime init (`jacl_heap_init` / `jacl_intern_init` / `jacl_map_init`) runs in the entry
  before the program body executes as the root task on a fiber.
