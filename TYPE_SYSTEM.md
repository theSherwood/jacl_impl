# JACL Type System

JACL is gradually-typed: values default to `dyn` (runtime-tagged) and pick
up static types when bindings, parameters, or returns carry annotations.
The compiler emits unboxed code paths when both operands of an operation
are statically known. The typer is the sole authority on type identity;
the compiler reads its annotations and emits bytecode.

This doc covers the shipping language. For wire-level value layout (NaN
boxing, tag bits, heap object headers) see `DESIGN.md`.

## Pipeline

```
parser   →  AST                 (untyped; LIT_INT/FLOAT/STRING get
                                  parser-set inferred_type defaults)
typer    →  AST + annotations   (every node reachable from the walk
                                  has inferred_type, inferred_struct_idx,
                                  inferred_key_struct_idx populated)
compiler →  bytecode            (reads type identity from AST; tracks
                                  runtime stack representation in
                                  c->last_expr_type for ensure_boxed)
```

The typer runs once between macro expansion and codegen. It is total
over reachable AST shapes — every node it visits ends up with a real
`JaclType` (no sentinel for "didn't visit"). `TYPE_DYN` means
"dynamically-typed value", not "typer gap".

## State ownership

| State | Lives on | Purpose |
|---|---|---|
| Declared type of an expression | `AstNode.inferred_type` | Set by typer; what the value's type *is*. |
| Struct registry index for STRUCT / typed-vec / typed-map / Future / Box / Ptr | `AstNode.inferred_struct_idx` | Set by typer; aligned with compiler's `struct_registry`. For typed collections / futures / boxes / pointers, this is the *element* idx (or a `JACL_SCALAR_TYPE_IDX` sentinel for scalar elements). |
| Map key struct index for typed-map | `AstNode.inferred_key_struct_idx` | Set by typer. |
| Runtime stack representation post-emit | `Compiler.last_expr_type` | Reflects what's *actually on the stack* mid-codegen — distinct from declared type when var-ref loads emit unbox code (e.g. mutable cell loads emit `OP_GET_CELL_LOCAL + OP_TO_DYN`, leaving the stack DYN even when the binding is i64). Read only by `compiler__ensure_boxed`. |
| Inline-struct stack arrangement | `Compiler.inline_repr`, `inline_ref_base`, `inline_ref_offset` | Codegen state for chained struct field access. Not type-system state despite the name. |

## Shared encoding for collection / wrapper element types

`JACL_SCALAR_VEC_BASE` (= `0xFF00`, in `ast.c`) defines the sentinel
range for the element-type slot. Real struct registry entries live
below `0xFF00`; values in `[0xFF00, 0x10000)` encode a scalar
`JaclType` (e.g. `[Vec i64]` carries `JACL_SCALAR_TYPE_IDX(TYPE_I64)`
in its `inferred_struct_idx`). Both typer and compiler use the same
encoding via `JACL_SCALAR_TYPE_IDX` / `JACL_TYPE_IDX_TO_SCALAR` /
`JACL_IS_SCALAR_TYPE_IDX` (defined after the `JaclType` enum in
`ast.c`).

The same encoding serves `TYPE_TYPED_VEC`, `TYPE_TYPED_MAP`,
`TYPE_FUTURE`, `TYPE_BOX`, and `TYPE_PTR` element/pointee tracking.
`TYPE_TYPED_MAP` additionally uses `inferred_key_struct_idx` for the
key-side type.

## Proc / closure types (`[Proc [P…] R]`)

A closure is `TYPE_CLOSURE`; its **signature** (param types + return
type) is interned in the shape registry as a `TYPE_SHAPE_PROC` entry,
and the entry's index rides on `inferred_struct_idx` just like a
struct idx (the slot space is shared — proc-shape idxs and struct
idxs are distinguished by the registry entry's kind, not the value
range). Param/return slots that are scalars use the
`JACL_SCALAR_TYPE_IDX` encoding above; struct params/returns use the
real struct idx. This is what makes `[Proc [Point] i32]` /
`[Proc [i32] Point]` work — struct idxs are portable 1:1 across the
typer and compiler registries. **Nested compounds** inside `[Proc …]`
(`[Proc [[Vec i64]] …]`, nested `[Proc …]`) are deliberately
unsupported: those element idxs are registry-bound and not portable.

Because the typer and compiler keep *separate* shape registries, a
proc shape that must survive the typer→AST→compiler handoff is also
interned **portably** and stamped on the AST node as
`inferred_proc_shape_idx` (a parallel field to `inferred_struct_idx`,
used only for proc shapes). The compiler decodes that portable stamp
rather than trusting a typer-side registry index. See
`docs/TYPER_SHAPE_UNIFICATION_AUDIT.md` for the full shape-registry
story.

Closures are first-class values: a `[Proc …]`-annotated binding,
param, return, or collection element carries a concrete signature and
is called through the typed unboxed convention; conformance is exact
structural match (invariant). A bare **global proc name** (`$dbl`)
narrows to a typed closure value via its registered signature, and a
**forward reference** (proc used as a value before its textual
definition) conforms via the portable stamp from the typer's
whole-program pre-pass — which is why higher-order mutual recursion is
expressible. The authoritative status and the few remaining gaps live
in `TYPED_CLOSURES_DESIGN.md` §"Remaining work".

## Struct registry slot reservations

Both passes share the same slot layout so an `inferred_struct_idx`
in the typer's annotations means the same struct in the compiler's
registry:

- Slot 0: dyn placeholder (`defs[0] = NULL`).
- Slot 1: ctx struct (always populated; pre-reserved at registry init).
- Slot 2+: user-declared structs in source order. Anonymous inline
  struct types (`struct{x:i32,y:i32}`) are registered on demand by
  both passes during the defstruct walk; same source order → same
  slot indices.

For the runtime shape of the registry (`StructTypeDef`, `is_value_type`
flag, ctx as the lone HeapRecord) see `STRUCT_DESIGN.md`.

## Key invariants

1. **The typer is the sole authority on type identity.** It writes
   `inferred_type` / `inferred_struct_idx` / `inferred_key_struct_idx`
   on AST nodes. The compiler reads them; never writes them. Code
   that needs to know "what type is this expression?" reads from
   the AST node, not from `Compiler` state.
2. **`compiler__ensure_boxed` is the only consumer of `c->last_expr_type`.**
   That field tracks post-emit stack representation, which can
   diverge from the AST type (e.g. when a var-ref load emits an
   unbox). New code that needs the AST type should read
   `args[i]->inferred_type` directly.
3. **Compile-time AST rewrites must preserve typer annotations.**
   Either keep outer-node identity so the typer's annotation on
   the outer survives, or be rare enough that the typer's
   bare-name-receiver fallback handles them. Today only the
   `HEAD_SET` arrow desugar exists, and the typer covers it.

## Where to find things

| What | File:Function |
|---|---|
| Typer entry point | `typer.c:typer_infer` |
| Typer pre-passes (structs, ctx, procs, imports) | `typer.c:typer__register_structs / __register_ctx_struct / __register_procs` |
| Typer anonymous-struct registration | `typer.c:typer__register_inline_struct` |
| Typer command type rules | `typer.c:typer__infer_command_inner` |
| Typer's HEAD_DOT field-resolve | `typer.c:typer__infer_command_inner` (search "HEAD_DOT") |
| Typer's module-binding `$mod->fn` lookup | `typer.c:typer__find_bound_export` |
| Compiler's import collector | `compiler.c:compiler__collect_typer_imports` |
| ctx struct slot reservation | `compiler.c:struct_registry__init`, `typer.c:typer_infer` |
| Shared scalar-elem encoding | `ast.c` (the `JACL_SCALAR_*` macros, after `JaclType` enum) |
| Proc-shape interning (typer / portable stamp) | `typer.c:typer__intern_global_proc_shape / __portable_proc_shape`; `compiler.c:compiler__decode_proc_shape_ex` |
| Closure conformance (global / forward-ref) | `compiler.c:compiler__global_closure_conforms / __stamp_closure_conforms` |
| Compiler's runtime-state tracker | `compiler.c:compiler__ensure_boxed` (only reader of `c->last_expr_type`) |
| Shared error formatters | `src/type_error.c` (`jacl_format_*`) |

## Design decisions

### 1. Strictness — gradual sound

- **All operands `dyn`** → permissive. The operation compiles; the
  runtime decides. Result is `dyn`.
- **Any operand non-`dyn`** → sound. As soon as one operand has a
  known type, the operation is type-checked. Mismatched concrete
  types are compile-time errors.
- **Barrier crossing** (decision 2) is part of the sound rules: a
  `dyn` value can only enter a typed slot via explicit `[to T $val]`.

| Expression | Status |
|------------|--------|
| `[+ $dyn $dyn]` | compiles; result `dyn` |
| `[+ $i32 $i32]` | compiles; result `i32` |
| `[+ $i32 $f32]` | compile error (typed mismatch) |
| `[+ $i32 $dyn]` | compiles; result `dyn` (mixing flows through; barrier fires at next commitment) |
| `def i32 r [+ $i32 $dyn]` | compile error at the `def` (dyn-result into typed slot) |
| `[+ $i32 [to i32 $dyn]]` | compiles; result `i32` |

The quarantine model makes the typer's rules tractable: once a
value is typed, every operation it touches has known typed semantics,
without exception cases for "this typed value happens to be flowing
through dyn code."

### 2. Implicit dyn coercion — none at commitment sites

A "commitment site" is a place a value is stored into (or returned
from) a slot whose type is declared. Crossing the dyn boundary at
those sites requires `[to T $val]`:

- `def i32 x $dyn_val` → compile error
- `mut i32 x $dyn_val` / `set typed_var $dyn_val` → same
- `[typed_proc $dyn_arg]` (param is a typed slot) → compile error
- `[. $struct typed_field $dyn_val]` (struct/ctx field-set) → compile error
- `proc i32 f {} { $dyn_val }` (declared return) → compile error
- `dyn → dyn` (typed value flowing to a dyn slot) → permitted; the
  `dyn` binding form itself is the explicit "I want this dyn" marker
  (e.g. `def dyn x $i32_val` is fine).

Expression-level mixing — binary ops, calls returning into a
`def dyn`, vec-push into untyped vec — flows through implicitly and
the result types as `dyn`. The barrier check fires at whichever
commitment site the `dyn` result flows into next, not at every
internal step.

This is closer in spirit to TypeScript's `noImplicitAny` (typed →
any flows implicitly; any → typed requires cast) than to Dart's
fully-sound mode. The `[to T …]` cast at commitment sites is the
single, well-named place a tag check happens.

### 3. `inline_repr` stays in the compiler

`Compiler.inline_repr` (`INLINE_NONE` / `INLINE_STACK` / `INLINE_REF`)
describes how struct bytes are currently arranged on the value
stack mid-expression. It's purely transient codegen state — never
persists past a single expression, invisible to the user. Not a
type-level concept. Pointer types (`[Ptr T]`) are a separate,
typer-owned feature.

### 4. Type errors — shared formatters, per-pass reporting

Type-mismatch message content lives in `src/type_error.c`
(`jacl_format_*`). Both the typer and the compiler call them; each
pass keeps its own reporting machinery (`typer__error` /
`compiler__error`). The pass that catches an error first reports
it; the other treats the affected AST node as poisoned
(`inferred_type → TYPE_DYN`) so downstream walks don't cascade.

Codegen-only errors ("cannot expand inline struct here", "suspension
inside non-suspending callback") stay fully in `compiler.c` — they're
not type-mismatch errors.

### 5. Migration order — bottom-up by AST shape

The migration that built today's typer walked AST shapes from
literals → var-refs → blocks → commands → declarations, making
each shape's typing total before moving to the next. Recorded for
the same shape of work if a future change re-opens the migration.

## Cross-module typing

Pre-pass in `compiler__collect_typer_imports` triggers dependency
compilation before the outer typer runs (so `dep_mod->exports` is
populated) and packs each imported export's signature into a flat
`TyperImportProc[]` handed to `typer_infer`. The struct's `arity`
field disambiguates callable procs (≥ 0) from def/mut value imports
(< 0); `binding` distinguishes destructuring (NULL) from
module-binding form.

- **Destructuring** (`use "path" {names}`): callable entries become
  TyperProcs; value entries become top-level TyperBindings.
- **Module-binding** (`use "path" m`): entries stay in `tc.imports`
  and are looked up at use sites by `typer__find_bound_export`.
  Procs drive `[$m->fn args]` call dispatch via a stack-allocated
  TyperProc proxy. Bare `$m->field` value access narrows in the
  HEAD_DOT 2-arg typer rule (procs → TYPE_CLOSURE, values →
  declared type).

Resolution falls through to dyn if a local binding shadows the
module name (matches runtime lexical resolution).

## Pointer types (Stage 5)

`TYPE_PTR` is a real type with pointee tracking via the same
sentinel encoding. Surface:

- `[Ptr T]` annotations in def/mut/set/proc-param/return positions.
- `[ptr-cast [Ptr T] $u64]` / `[ptr-addr $p]` round-trip.
- `[ptr-null [Ptr T]]` typed null literal.
- `$p->field` auto-deref through `[Ptr Struct]` (single deref;
  nested chains accumulate offsets at compile time using the inline
  byte layout described in `STRUCT_DESIGN.md`).
- `[ptr-deref $p]` for whole-value scalar loads.
- `[addr $p->field]` returns `[Ptr <field-type>]`.
- `[ptr-offset $p $n]` / `[ptr-diff $a $b]` typed pointer
  arithmetic.
- `extern <ret-type> name {params}` for typed native function
  declarations.
- Print: typed pointers format as `Ptr<Name>(0xADDR)` via
  `OP_PRINT_PTR` (compiler emits when pointee is known; falls
  back to raw u64 print otherwise).

Pointer misuse (cross-pointee assignment, mixed-pointee
comparison, arithmetic via `+`, ptr-diff with mismatched pointees)
errors at compile time via `jacl_format_ptr_*` formatters in
`src/type_error.c`.

## Buffer types (Stage 6)

`TYPE_BUF` is a compile-time-sized, contiguous, ABI-flat array of `T` —
fills the gap between `[Ptr T]` (single element) and `[Vec T]`
(GC-owned, growable). The canonical reference is `BUFFER_DESIGN.md`.
Surface highlights:

- `[Buf N T]` annotation in def/mut/struct-field positions; `N` is a
  positive literal, `T` is a scalar or value-struct.
- Zero-init on declaration; literal init via `[[Buf N T] v0 v1 ...]`
  (partial fill allowed, `k > N` rejected at compile time, literal
  values out of `T`'s range rejected at compile time).
- `[buf-get $b $i]` / `[buf-set $b $i $v]` — bounds-checked indexed
  access. `[buf-unchecked-get]` / `[buf-unchecked-set]` skip the
  runtime bounds check.
- `[buf-len $b]` — compile-time constant fold.
- `$b->N` — arrow indexing at a constant `N`; bounds-checked at the
  typer. `[addr $b->N]` returns `[Ptr T]`.
- `[Buf N T]` → `[Ptr T]` implicit decay at extern and JACL proc call
  sites (pointee must match exactly).
- Error messages render the type in source syntax — `[Buf 4 u8]`
  rather than `buf` — for easy copy-paste back into code.

Buf-related compile-time errors are formatted via the
`jacl_format_buf_*` helpers in `src/type_error.c`.

## Deferred items

See **`NOT_IMPLEMENTED.md` §4** for the canonical list of typer-side
deferred work (imported struct field-typing, `swap` struct-element
narrowing, match arm-walk). **Closure literal signatures are no longer
deferred** — the typed-closures arc (incl. `[Proc …]` syntax, closure
values/params/returns, compound params & returns, global procs as values,
and forward refs) is **complete**; see `TYPED_CLOSURES_DESIGN.md`
(§"Status" for capabilities, §2/§3 for the settled spec).

## Test coverage

- `test/test_typer.c` — typer-only assertions on `inferred_type` /
  `inferred_struct_idx`. Doesn't go through codegen, so silent
  typer bugs are visible here.
- `test/test_type_errors.c` — substring match on compile-time
  error messages. Tolerates re-wording; locks in load-bearing
  tokens (type names, `type error`, proc / struct names).
- `test/jacl/` — end-to-end fixtures via `test_jacl_harness`.
  Modules use `test/jacl/modules/<name>/main.jacl` with
  `# expect:` / `# expect-error:` headers.

Run the suite with `bash build.sh`. Per-binary: typer-only at
`/jacl_impl/.build/typer`, type-error at `/jacl_impl/.build/type_errors`.
