# Design — typed / monomorphized closures

Status: **Phase B COMPLETE (2026-06-11): B1+B2 (typed closure VALUES), B3a
(closure params), B3b (closures-returning-closures), B3d (named-closure
values), B3c-vec (`[Vec [Proc …]]`) AND B3c-arr (`[Arr [Proc …]]`) — collections
of closures in both the persistent vec and the mutable arr, all via a registry
`TYPE_SHAPE_PROC`, no VM change.** The B3c follow-ups (vec-get/arr-get element
narrowing + push-of-closure monomorphization) are **also landed (2026-06-11)** —
see the B3c follow-ups block below.

**Phase B3c follow-ups — vec-get/arr-get narrowing + push monomorphization
(2026-06-11):**
- **vec-get/arr-get narrowing:** `[vec-get/arr-get $fns i]` on a `[Vec/Arr
  [Proc …]]` now narrows the result to a typed closure, so `def g [vec-get
  $fns 0]` stamps a typed-closure binding and `[g …]` is a typed (unboxed) call.
  Typer: a `TYPE_CLOSURE` arm on the var-ref-binding decode in HEAD_VEC_GET /
  HEAD_ARR_GET (the arr case gained the var-ref re-resolution block the vec case
  already had), typing the node `TYPE_CLOSURE` + the typer-side proc shape — the
  existing B3b def branch then stamps the binding. Compiler:
  `compiler__call_closure_return_shape` recognizes a vec-get/arr-get head and
  returns the receiver local's element proc shape (new shared helper
  `compiler__coll_receiver_proc_shape`), so the def handler's B3b stamp fires.
- **push monomorphization:** a closure literal pushed via vec-push/arr-push onto
  a `[Vec/Arr [Proc …]]` is now monomorphized to the element signature (was read
  as nil). Compiler: both push builtins compile the element via
  `compiler__compile_closure_to_shape` when the receiver carries a proc-elem
  shape (mirrors the ctor). Typer: an inline closure-literal push arg is
  monomorphized via `typer__monomorphize_proc_literal` (mirrors the B3a arg
  walk). Push receiver must be a var-ref — a nested push
  (`[vec-push [vec-push …] …]`) doesn't monomorphize the outer element (the
  outer receiver is a command, not a var-ref); use intermediate bindings.
- **Cross-registry fix:** `typer__infer_var_ref` suppressed the typer-side shape
  idx on the AST stamp only for `TYPE_VEC`/`TYPE_MAP` — `TYPE_ARR` was missing,
  so an `[Arr [Proc …]]` var-ref leaked the shape idx and arr-get never narrowed
  (wide-i64 returns came back nil). Added `TYPE_ARR` to the suppression.
- **Annotated non-ctor result binding:** `def [Vec [Proc …]] more [vec-push …]`
  now stamps `more`'s element shape from the DECLARED annotation (a new
  `effective == TYPE_VEC` arm), not only from a literal-ctor RHS, so the result
  vec narrows on read-back. Tests: `typed_closure_vec_getpush.jacl`,
  `typed_closure_arr_getpush.jacl` (both wide-i64 unboxed). Suite 91/91 binaries.

**Phase B3c — `[Arr [Proc …]]` (mutable collections of closures) as landed
(2026-06-11):** the mutable-array twin of B3c-vec. Closures are ref-kind so an
`[Arr [Proc …]]` is the PLAIN traced arr rep (OP_ARR) with the element signature
carried statically, exactly like `[Arr str]`. The wiring mirrors the vec path at
every site — they were deliberately kept structurally identical:
- **Declared annotation** (`def [Arr [Proc …]] …`): the def handler's
  nested-compound annotation arm now accepts an `Arr` head (was Vec-only), so it
  resolves `declared_type = TYPE_ARR` + the typer-side element shape instead of
  bailing at the `else return false` (the bug found during this slice — without
  it the def fell to the generic typer and the binding mistyped as a vec).
- **Constructor** (`[[Arr [Proc …]] e0 e1]`): `typer__arr_type` /
  `compiler__arr_type_expr` reject a command element, so both the typer ctor
  recognition (`is_arr_ctor` now flips on an `[Arr [cmd]]` head) and a new
  compiler arr nested-compound branch (mirrors the vec one, emits OP_ARR)
  recognize it; each element is monomorphized / conformance-checked via the same
  `compiler__compile_closure_to_shape` helper.
- **Binding shape:** the def handler re-derives the element proc shape and keeps
  the binding `TYPE_ARR` (the existing `coll_proc_elem_shape` compiler arm and
  the local stamps already handled both VEC and ARR).
- **for-loop narrowing:** the typer for-loop fallback + the compiler for-loop
  closure-narrowing block now fire for `TYPE_ARR` too, stamping the loop var as a
  typed closure. Tests: `typed_closure_arr.jacl` (literal + named elements,
  wide-i64 unboxed — verbatim twin of `typed_closure_vec.jacl`),
  `typed_closure_arr_error` (element conformance). Suite 537/537, 91/91 binaries.

**Phase B3c — `[Vec [Proc …]]` (collections of closures) as landed (2026-06-10):**
Closures are ref-kind (tagged heap pointers) → a `[Vec [Proc …]]` is the PLAIN
traced vec rep with the element signature carried statically (TYPE_VEC + an
element `TYPE_SHAPE_PROC` idx), exactly like `[Vec str]`/`[Vec [Vec i64]]`.
- **Element type:** `typer__nested_elem_shape` gained a `[Proc …]` arm (interns
  the shape; handled up front since the param list is an AST_COMMAND the generic
  inner loop would misparse).
- **Constructor** (`[[Vec [Proc …]] e0 e1]`): typer + compiler nested-vec ctor
  recognize a `[Proc …]` element; each element is compiled via a shared helper
  `compiler__compile_closure_to_shape` (monomorphize an inline literal /
  conformance-check a named closure / error) — the same invariant as args.
- **Binding shape (cross-registry):** the def handler re-derives the element
  proc shape COMPILER-side (`coll_proc_elem_shape`) and stamps it on the Local's
  `struct_type_idx`; a `[Vec/Arr [Proc …]]` binding keeps its `TYPE_VEC` type
  (plain-rep ref vecs otherwise fall to dyn) so the for-loop can read it.
- **for-loop narrowing:** the typer decodes the element proc shape onto the loop
  binding (is_typed_closure); the compiler resolves the receiver's ORIGINAL
  Local, reads its proc-shape `struct_type_idx`, and stamps the loop var via
  `compiler__stamp_closure_param_local` — so `[f …]` on an element is a typed
  unboxed call. Tests: `typed_closure_vec.jacl` (literal + named elements,
  wide-i64 unboxed), `typed_closure_vec_error` (element conformance). Suite
  599/599, 91/91.

**B3c scope / debt:** `[Arr [Proc …]]` (mutable) is wired (B3c-arr, above), and
the vec-get/arr-get narrowing + push-of-closure monomorphization follow-ups are
landed (see the B3c follow-ups block near the top). A closure-collection element
read as an EXPRESSION and called directly — `[[vec-get $fns i] x]` /
`[[arr-get …] x]` — is now also covered, via the step-2b portable proc-shape
stamp (`inferred_proc_shape_idx`): the typer stamps the get result and the
compiler drives the typed call from it, with no binding to re-derive from. Test:
`typed_closure_direct_get.jacl`. Remaining edge: a nested push
(`[vec-push [vec-push …] …]`) only monomorphizes the inner element (the outer
receiver is a command, not a var-ref) — that one needs the collection's element
shape on the expression result, a different stamp than the closure-signature
stamp.

**Phase B3d — named closures as values as landed (2026-06-10):**
- **`[apply $g 5]`** — a NAMED typed-closure value passed to a `[Proc …]` param.
  The compiler closure-arg path resolves g's local, conformance-checks its
  signature against the param's shape (`compiler__decode_proc_shape` +
  `compiler__local_closure_conforms`, invariant: arity + param types + return),
  and pushes the closure value. The typer already typed a `[Proc …]` binding as
  `TYPE_CLOSURE`, so it accepts the arg leniently; the compiler enforces.
- **`def [Proc [i64] i64] f $g`** — a named typed-closure RHS under a `[Proc …]`
  annotation. A `proc_named` path conformance-checks g vs the declared signature
  and stamps f from the annotation (so `[f …]` is typed and f composes:
  `def h $g` then `[h …]`). The typer records f's declared signature for any
  RHS (inline literal OR named) when the binding is `TYPE_CLOSURE`.
- **Design choice:** conformance resolves the named binding DIRECTLY at each
  sink (compares flat signatures) rather than changing the closure binding's
  value-read rep — lower-risk, no VM/struct change. Tests:
  `typed_closure_named.jacl` (pass to HOF, wide-i64 unboxed, bind-and-compose) +
  `typed_closure_param_named_arg_error` (now a conformance-mismatch error).
  Suite 597/597, 91/91 binaries.

**B3d scope / debt:** a closure value that is an EXPRESSION (not a var-ref or
inline literal) passed to a closure param — e.g. `[apply [pick-fn] 5]` — is now
handled (step 2b, slice 3): the typer stamps the expression's portable
proc-shape idx (`inferred_proc_shape_idx`) and the compiler conformance-checks
it at the closure-param sink, then pushes the value. A non-conforming signature
is a clear error. Tests: `typed_closure_expr_arg.jacl` (+ direct call of the
expr) / `typed_closure_expr_arg_error.jacl`. Remaining: named closures still
resolve their signature only via locals (global typed-closure bindings as values
are a follow-up).

**Phase B3b — closures-returning-closures as landed (2026-06-10):**
- **`[Proc …]` RETURN type** (`typer__resolve_return_type` + the compiler
  proc-return resolution) → `TYPE_CLOSURE` + interned shape on
  `TyperProc`/`GlobalArity.return_struct_idx`.
- **Return monomorphization** of the body-tail closure literal against the
  declared return signature, on both sides. Compiler: a new
  `pending_return_proc_shape` field is live ONLY in tail position
  (`compile_block_expr` clears it around non-tail statements, keeps it for the
  tail and propagates into nested tail blocks — `if`/`match` returning a
  closure), so the tail literal activates `annot_proc` from the shape; mis-firing
  on an intermediate closure-valued block-expr is avoided. Typer: `handle_proc`
  monomorphizes the tail literal (captures resolve from the enclosing scope).
- **Call-result propagation**: `def f [make-adder 5]` stamps f's binding with the
  returned closure's signature so `[f …]` is a typed call. The compiler
  re-derives the COMPILER-side shape from the callee's `GlobalArity.return_struct_idx`
  (`compiler__call_closure_return_shape`) — NOT the typer's stamp on the call node
  (cross-registry rule); the typer stamps from its own call-node shape. Works for
  `def`-bound and directly-called (`[[make-adder 5] 3]`) returned closures, with
  captured upvalues. Tests: `typed_closure_returning.jacl` (capture, wide-i64
  unboxed via a strict consumer, str) + `_return_{arity,struct}_error`. Suite
  593/593, 91/91 binaries.

**B3b scope / debt → B3c–d:** call-result propagation resolves the return shape
only via `GlobalArity` (global closure-returning procs); struct/compound
`[Proc …]` return types still error (need nested-shape encoding); a non-literal
tail (returning a closure param/value verbatim) isn't conformance-checked.

**Phase B3a — closure params as landed (2026-06-10):**
- **`TYPE_SHAPE_PROC` registry encoding** (shapes.c + jacl.h mirror): a proc
  signature interned as a shape (param idxs in a registry-owned side
  `proc_param_pool`, + return idx) so a proc type can be referenced by a single
  idx *inside another type* — the encoding B1+B2 deferred. `type_shape_intern_proc`
  dedups; decode → `TYPE_CLOSURE` (size/align/refmap treat it as one tagged
  traced slot).
- **`[Proc …]` closure params** (`typer__parse_params` + the compiler param
  loop): the param binds as a `TYPE_CLOSURE` local carrying the shape; its
  signature is decoded onto the body local (`compiler__stamp_closure_param_local`
  / the typer `TyperBinding` stamp) so a body call `[f …]` reuses the B1+B2
  typed-call path. `GlobalArity` gained `param_struct_idxs[]` (dual-def synced)
  so a call site can recover a closure param's shape.
- **Caller monomorphization + conformance**: at `[apply <lit> …]`, an inline
  closure-literal arg is monomorphized to the param's declared signature — on
  BOTH sides: the compiler activates the `annot_proc` context from the shape
  (`compiler__activate_annot_proc_from_shape`), and the typer runs a tail-typed
  body walk (`typer__monomorphize_proc_literal`, mirroring the def-site walk).
  **Both are required** — without the typer walk the body stays dyn (literal
  narrowing + param reads tagged) and the typed call reads a tagged param as
  nil (the bug found + fixed during B3a). Arity/param conformance reuses the
  HEAD_PROC checks. Tests: `typed_closure_param.jacl` (wide-i64 unboxed proof,
  multi closure params, str), `typed_closure_param_{arity,named_arg}_error`.
  Suite 587/587, 91/91 binaries.

**B3a scope / debt → B3b–d:**
- **Named/bound-closure ARG** (`[apply $g …]`) is a clear compile error
  (Phase B3d) — supporting it needs conformance-checking a stored closure's
  signature AND the closure-value-read rep (a `[Proc …]` binding currently reads
  as `dyn` when used as a value, only as typed when *called*). Not the cheap
  add it first looked — it's its own slice.
- Closure params only resolve their shape via `GlobalArity` (global procs); a
  closure param on a *local* proc binding has no shape at the call site yet.
- Struct/compound param/return inside `[Proc …]` still B3b (clear error). Resolved the lambda-boundary technical
debt in `STREAM_TYPING_DEBT.md` (items 1, 3, 4, and 5-by-deletion); pairs with
typed `collect` (item 2). Builds directly on the existing i64 wide-mapper path
(`c338b99`, `aa81bef`), which is already a partial monomorphization.

**Phase B — B1+B2 as landed (2026-06-10):**
- **`[Proc [P…] R]` syntax (§3) at the `def`/`mut` annotation site.** Parses as
  an ordinary `[...]` command (no parser change); resolved by `typer__proc_type`
  / `compiler__proc_annotation` (mirroring `typer__future_type`). Param list is
  the bracketed first arg (`[]`, `[i64]`, `[i64 i64]`); return is the optional
  second arg. Scalar param/return types **and `dyn`** are supported; struct &
  compound (`[Vec T]`, nested `[Proc …]`) param/return types are a **clear
  compile error** (Phase B3) — never a silent rep-mismatched bind.
- **Typed closure VALUE rep is the ordinary `JaclClosure`; NO VM change.**
  `OP_CALL` already uses a caller-prepares-rep convention (args left in native
  rep, callee reads by its param types; wide returns flow back unboxed for a
  regular call). So a "typed closure value call" is a **typer + compiler
  threading job** that reuses the existing local-typed-call path
  (`compiler.c:16189`, `call_param_types = c->locals[slot].param_types`).
- **Monomorphization of an inline literal RHS.** `def [Proc [i64] i64] f
  [proc {x} {…}]`: the def handler stashes the resolved signature in
  `c->annot_proc_*` just around compiling the literal; `HEAD_PROC` adopts the
  param types for untyped params and the return type when undeclared
  (generalizes `hof_param_enc`/`hof_mapper_ret_enc` from HOF context to an
  explicit annotation), so the body compiles with typed params + typed return
  (no box). The binding's `Local`/`GlobalArity` records the signature
  (`compiler__stamp_closure_local`) → call sites use the typed unboxed
  convention. The typer mirrors this: a strict tail-typed monomorphization walk
  in `handle_def_or_mut`, the signature stored on `TyperBinding`
  (`is_typed_closure`), and a `bound_proxy` at the call dispatch so `[f args]`
  narrows to the declared return + checks args (like a named proc).
- **Conformance (§2.2/2.3, invariant):** the literal's **arity** must equal the
  declared one, and an **explicitly-typed literal param** must match the
  declared type (untyped params infer) — both are compile errors. Proven unboxed
  by wide i64 results past INT32_MAX flowing into strict-`i64` consumers
  (`test/jacl/typed_closure_binding.jacl`); error tests
  `typed_closure_{arity,param_type,struct_param}_error.jacl`. Suite 584/584.

**Deliberate B1+B2 scope / debt (→ B3):**
- **Named/bound-closure RHS** (`def [Proc …] f $g`) is **not** yet a typed bind
  — a plain closure var reads as `dyn`, so this is a safe "cannot assign dyn to
  closure binding" error today (no corruption). B3 adds conformance-checking of
  a concrete-signature closure value.
- **`[Proc …]` in param / return / array-element positions**, higher-order
  procs (case 5.3), closures-returning-closures (case 5.1) — all B3. The
  encoding currently lives ON the binding (`TyperBinding` / `Local` /
  `GlobalArity`), NOT as a registry `TYPE_SHAPE_PROC` — that shape is only
  needed when a proc type is referenced by a single idx *inside another type*,
  which is exactly what B3 needs; add it then.
- **No-return form `[Proc [P]]` defaults to `dyn`**, not nil (the strict
  "defaults nil" of §3 needs a nil-return rep that discards the body tail).
- **Struct/compound param & return types** — B3 (currently a clear error).

**Landed so far (Phase A):**
1. **Parser** — inline anonymous `proc` literals now parse inside `[...]`
   (`[proc {x} {…}]`, `[proc {x} type {body}]`); the old `parser.c:373`
   rejection is lifted. `\` is now demonstrably pure sugar for this shape.
   Test: `test/jacl/inline_proc_literal.jacl`.
2. **Transform typed return** — an inline `transform` mapper over a wide
   (i64/u64/f64) stream is compiled with a **typed return**, not a dyn return:
   `compiler__compile_hof_builtin` stashes the typer-inferred wide return enc in
   `c->hof_mapper_ret_enc` just around compiling the mapper; the `HEAD_PROC`
   path adopts it as `proc_return_type` when no return is declared. So
   `compiler__emit_return` no longer boxes the wide tail (its box branch is
   gated on `return_type == TYPE_DYN`), and the matching unbox in the
   `STREAM_KIND_TRANSFORM` pull (`vm.c`) is removed in lockstep. This kills the
   per-element box→unbox round-trip (**debt item 3, transform half**) for the
   dominant numeric idiom. Tests: `stream_transform_wide_return.jacl` (values
   past INT32_MAX), plus the existing `stream_transform_typed` /
   `stream_wide_f64_u64` stay green. Suite 484/484, 91/91 binaries.

3. **Filter predicate input monomorphization** — an inline `filter` predicate
   over a wide stream is typed to read its param wide (typer `HEAD_FILTER` runs
   `typer__proc_result_enc` for the body-typing side effect), and `OP_FILTER`
   carries a `pred_elem_idx` operand → `JaclStream.pred_elem_idx` so the pull
   passes the element **wide with no box-for-call** (kills **debt item 3, filter
   half**). Named/dyn predicates (and the vec/map paths) keep the box. Filter
   keeps **truthiness** (§5). Crucially, `typer__proc_result_enc` now
   **trial-types with rollback**: if binding the param wide surfaces a
   strict-numeric error the dyn body wouldn't (e.g. `== i32-literal i64-expr`),
   it rolls the error back and falls to the dyn boxed path — so existing
   permissive predicates (e.g. `[\ == 0 [% $it 2]]`) keep working. Tests:
   `stream_filter_typed.jacl`, `stream_filter_named_pred.jacl`.

4. **`each` callback input monomorphization** — `each` is the HOF callback form
   of `for` (`for $stream [\ body]` → `OP_EACH`, return discarded). Same pattern
   as filter: the typer (`HEAD_FOR` callback branch) types the inline callback
   body wide and stamps it; `OP_EACH` carries the same param-rep operand; the VM
   pulls **wide** (`vm__pull_stream_one`) instead of the tagged-normalizing
   `vm__pull_stream_dyn` when the callback was monomorphized, passing the element
   wide with no box. Named/dyn callbacks keep the tagged pull. Tests:
   `stream_each_typed.jacl`, `stream_each_named_cb.jacl`.

**Restore-pass removal — DONE for all element types (debt 4 resolved,
2026-06-10):**
- **i32/u32/f32/bool/str (tagged-fit)** — the keep gate generalized to
  `keep_typed = body_bound && !probe_errored`; tagged on both sides, so the
  bytecode is unchanged (no typed-i32 opcodes exist — verified), but
  body-internal static typing now works and the double-walk is gone. The
  generic arg pre-walk + for-callback pre-infer SKIP inline HOF callbacks
  over typed streams, so the body is typed exactly ONCE (the original
  "Startup" goal); the restore pass survives only as the mixed-type
  rollback. `assert-type $x i32` inside a mapper body works (used to be a
  sticky compile error from the unbound pre-walk). Test:
  `stream_mono_tagged_fit.jacl` (i32/str/f32).
- **struct — DONE for inputs (2026-06-10): channel + for-loop + HOF
  monomorphization.** The channel: `OP_YIELD_SM_WIDE` parks the struct's raw
  value bytes in `vm->yield_wide` (no GC tracing — user structs are pure
  value bytes; box-at-yield rejected per STRUCT_DESIGN); the generator pull
  copies to the caller's buffer; the for-loop pulls via
  `OP_STREAM_NEXT_INLINE` and binds an N-slot inline local. **Struct HOF
  monomorphization:** an inline transform/filter/each callback over a
  `[Stream <Struct>]` is compiled with a **by-value struct param** — the
  typer keeps the struct-typed body (`keep_typed` gate in
  `typer__proc_result_enc`) and stamps the callback node; the compiler
  adopts the element type for the untyped param via `c->hof_param_enc`
  (HEAD_PROC), and the existing typed-param machinery (`param_total_slots`,
  `has_inline_params`, padding locals) does the rest; the lazy pulls push N
  bitmap-marked inline slots and set `stack_base = stack_top -
  param_total_slots`, mirroring the eager OP_TYPED_TRANSFORM loop. `take` is
  a rep-agnostic passthrough. Named/dyn callbacks over struct streams are a
  compile error (+ `has_inline_params` runtime defense for dyn flow) — no
  legal boxed path exists. Tests: `stream_struct_hof.jacl` (each/transform/
  filter/chained, incl. a 3-slot element), `stream_struct_hof_named_error`.
  **Struct-RETURNING transform mappers LANDED (2026-06-10, multi-slot HOF
  OUTPUT channel):** a struct `out_elem_enc` (only producible by a cleanly
  monomorphized inline mapper — `typer__proc_result_enc` returns UINT32_MAX
  otherwise) rides `c->hof_mapper_ret_enc` into HEAD_PROC, which compiles
  the mapper with a TYPE_STRUCT return (`emit_return` ships N inline slots /
  expands a heap tail via OP_RETURN_WIDE); the transform pull pops the
  multi-slot result via `vm__pop_struct` into the caller's buffer, and the
  stream's elem_idx becomes the struct idx, so downstream consumers
  (for-loop, chained HOFs, typed collect) ride the existing struct channel.
  Works for struct AND scalar input elements ([range …] → struct stream).
  En route: def-bound typed streams now keep their element stamp (the def
  handler had no TYPE_STREAM propagation arm, so `def s [transform …]` +
  `for [filter $s …]` compiled the dyn consumer path and tripped the
  runtime guard). Test: `stream_struct_ret.jacl` (identity, field-modifying,
  and scalar→struct mappers; chained filter; typed collect).
  **SM-body struct for-bindings LANDED (2026-06-10):** the loop binding
  rides a WIDE SM state field (walk registers width + struct idx from the
  collection stamp; OP_SET/GET_STATE_FIELD_WIDE; GC skips raw slots) for
  stream / [Vec T] / [Arr T] loops in suspending procs — the vec/arr
  struct loops previously misbehaved silently there (local-vs-state
  mismatch). Note for-loop BODIES still can't suspend at all
  (NOT_IMPLEMENTED.md §8d). Test: `sm_struct_for.jacl`.
  Typed `collect` → `[Vec T]` LANDED (debt 2;
  struct streams collect to [Vec Struct], wide stored flat, str stays plain
  — typed-vec storage is GC-opaque). The §4.1c
  generator-body struct bugs found en route are FIXED — see
  `NOT_IMPLEMENTED.md` §4.1b/§4.1c.

The unifying gate, now satisfied for every element type: drop the
restore-pass exactly when *the rep the VM hands the body equals the rep a
typed-body read expects*. Wide scalars needed the producer-wide flip; struct
needed the multi-slot channel + by-value param convention; tagged-fit
scalars and str satisfied it trivially. All landed — the restore pass exists
only as the failed-probe rollback.

**Known limitation (pre-existing):** `set` of a captured mutable upvalue inside
a HOF callback (e.g. `for $s [\ set acc [+ $acc $it]]`) type-errors — the
callback body doesn't resolve the mutable upvalue's declared type for the `set`
target (upvalue *reads* do resolve; see case 5). Independent of this work; the
callback bodies were typed `it:dyn` before too.

---

## 1. Goal

Make a closure used in a typed context have a real, concrete, statically-known
signature — so its body compiles against the actual element types (no `dyn`
boxing across the lambda boundary, no restore-pass, no runtime numeric
promotion to paper over tag mismatches). JACL has no generics; the technique is
**monomorphization** (specialize per concrete type at the use site), which is
sound because the relevant type is always statically known at that site.

---

## 2. Core rules (settled)

### 2.1 Where a closure's type comes from

A closure gets a concrete signature one of two ways; there is no third:

- **Inline proc literal** (`[\ … ]` or `[proc {x} {…}]` — anonymous, single-use
  by construction) → **monomorphized to the expected closure type in context.**
- **Named / bound closure** (a var-ref to a `def`/`proc`) → must **already have
  a concrete signature**; it is conformance-checked, never inferred.

`\` is pure sugar for an anonymous `proc {^it} { … }`; the core never special-
cases it. The compiler detects "inline" structurally: the HOF/return/element arg
is a proc-literal AST node, not an `AST_VAR_REF`.

### 2.2 Inline: untyped params infer, typed params check

For an inline proc literal monomorphized against expected type `[Proc [T…] R]`:

- **Untyped params** (`{x}`, or `\`'s `it`) → bind each param to the
  corresponding `T`; type the body; **infer** the return (checked against `R`
  if the context supplies one — see 2.4).
- **Typed params** (`{i64 x}`) → the signature is fixed; **conformance-check**
  it against `[Proc [T…] R]`. Mismatch (e.g. `{i64 x}` at an `[Stream f64]` HOF)
  is an error — an explicit annotation is never silently re-typed.

### 2.3 Conformance is invariant (exact structural match)

A closure conforms to `[Proc [T1 … Tn] R]` iff: same arity, each param type
matches exactly, return type matches exactly. **No subtyping, no variance.**

This is what *enforces* strictness, not a bolted-on check: proper function
subtyping is contravariant in params, which would make `[Proc [dyn] …]` usable
where `[Proc [i64] …]` is expected (the "dyn accepts anything" fallback we
reject). Invariance blocks that. Variance is moot regardless — JACL has no
subtype relation among concrete types (i32 is not a subtype of i64).

### 2.4 The monomorphization *context* (generalizes "HOF element type")

An inline literal is monomorphized against whatever expected closure type its
position supplies:

- **HOF over a typed collection/stream** → params constrained to the element
  type(s); **return inferred** (the HOF doesn't constrain it). `transform
  [Stream i64] [\ * $it 2]` → mapper `[Proc [i64] <inferred i64>]`.
- **Explicit `[Proc …]` context** (return position, typed param, typed array
  element) → params **and** return constrained; return is checked.

Same mechanism; the only difference is whether the context pins the return.

### 2.5 `dyn` is an ordinary type

`dyn` is allowed anywhere a type can appear, including inside `[Proc …]`
(`[Proc [dyn] dyn]`, `[Proc [i64] dyn]`). No special carve-out. A dyn-param
closure simply fails invariant conformance against a concrete-param requirement,
so **a dyn closure in a typed context is an error** — as a consequence of 2.3,
not a separate rule. (A dyn *return* is legitimate: a heterogeneous-body mapper
has type `[Proc [i64] dyn]` and produces a `[Stream dyn]`.)

---

## 3. Type syntax: `[Proc [params] ret]` (settled)

Parallel to `vec`/`[Vec T]`, `arr`/`[Arr T]`, `proc`/`[Proc …]`.

```
[Proc [i64 i64] i64]      # (i64,i64) -> i64
[Proc [i64] i64]          # (i64) -> i64
[Proc [] i64]             # () -> i64
[Proc [i64]]              # (i64) -> nil   (return optional, defaults nil)
[Proc []]                 # () -> nil      (minimal form)
[Arr [Proc [i64] i64]]    # array of (i64)->i64
[Proc [[Proc [i64] i64]] i64]   # higher-order: takes a (i64)->i64, returns i64
```

- **Types only, no names** — calls are positional, so param names have no
  meaning in a type. `[Proc [i64] i64]` and `[Proc [i64] i64]` are the only
  spelling; stray names are rejected.
- **`[]` not `{}`** — a param list is a *list of types*, not a statement block;
  `{}` is the block delimiter and would be a category error here.
- **Whitespace-separated**, exactly like `[Map K V]` / `[vec 1 2 3]`. No commas.
- **Param list mandatory** (even `[]`); **return optional**, defaults nil.
- **`dyn` permitted** inside (2.5).

---

## 4. Implementation sketch (where it touches)

The current i64 wide-mapper path already does ~40% of this for `transform` (param
bound to i64, body compiled wide, HOF pushes wide). Completing it:

- **Typer:** for an inline HOF arg, bind each param to the element type(s) (have
  it for `transform`; add `filter`/`each`); **record the inferred return type on
  the proc node** (new — needed so codegen emits a typed return). For a named
  closure arg in a typed context, conformance-check its signature; dyn → error.
- **Compiler:** compile the inline body with typed params (generalize the i64
  wide path to all element types) **and a typed return** so `compiler__emit_return`
  stops boxing the wide tail (kills the box→unbox round-trip, debt 3). Drop the
  restore-pass for these (debt 4).
- **VM / HOF invocation:** pass the element in its typed (wide) rep and **don't
  box for the call / don't unbox the result** — the closure is now typed in and
  typed out. (`transform` already pushes wide; remove the post-call unbox once
  the mapper returns typed; `filter` stops boxing for the predicate.)
- **Parser:** allow an inline anonymous `proc` inside `[...]` (lift the
  `parser.c:373` rejection) so `[proc {x} {…}]` works and `\` is demonstrably
  just sugar.

---

## 5. Pressure tests (cases worked through)

1. **Proc returning a closure** — `proc make-adder {i64 n} [Proc [i64] i64]
   { proc {x} { + $x $n } }`. The inner literal is monomorphized against the
   declared return `[Proc [i64] i64]` (param x→i64, body typed, captures i64
   `$n`). The *result* is a typed closure **value** invoked later via a typed
   calling convention → **needs Phase B** (the `[Proc …]` return-type syntax and
   a typed-closure-value rep). In Phase A (no syntax) a returned closure stays
   dyn. Noted limitation.
2. **`each` with no return** — `each [Stream i64] [\ print $it]`. Monomorphized
   param i64; body returns nil; `each` discards the return. Fine; return type
   unused.
3. **Nested `[Proc …]` param** — `[Proc [[Proc [i64] i64]] i64]` parses and
   conforms structurally (the param must be a closure conforming to
   `[Proc [i64] i64]`). Phase B (higher-order, needs the syntax + typed closure
   value passed in). Verbose but consistent.
4. **Heterogeneous body / un-inferable return** — `transform [Stream i64]
   [\ if [> $it 0] { $it } { "neg" }]`. Return infers to `dyn` (mixed) → mapper
   `[Proc [i64] dyn]` → output `[Stream dyn]`. Allowed (2.5); no error.
5. **Capturing upvalues** — `mut i64 acc 0; transform [Stream i64]
   [\ + $it $acc]`. Param $it:i64, upvalue $acc:i64 (existing typed-upvalue
   mechanism), body typed. Works.
6. **Arity mismatch** — `transform [Stream i64] [\ … two params …]` → the HOF's
   calling convention fixes arity (transform/filter/each = 1); a 2-param literal
   is an arity error via conformance.

### Resolved sub-question

- **`filter` predicate return type:** **DECIDED — keep truthiness.** The
  predicate stays `[Proc [T] dyn]` with the current runtime falsy check; a
  non-bool predicate is *not* an error. (Strict `[Proc [T] bool]` was
  considered and rejected as too breaking for now.) `filter` monomorphization
  will still type the *input* param (dropping the box-for-call), independent of
  this return-side policy.

---

## 6. Phasing

- **Phase A — inline monomorphization, no new syntax.** Rules 2.1/2.2/2.4
  (HOF/param-only context) + 2.3/2.5 for inline literals; typed param + typed
  body + typed return + typed HOF invocation; dyn-named-closure-in-typed-HOF =
  error; parser allows inline anonymous procs. Resolves debt 3 & 4 for the
  dominant idiom; makes debt 1's HOF-predicate consumer dead. **No `[Proc …]`
  syntax needed.**
- **Phase B — `[Proc …]` types.** §3 syntax → typed named closures, typed
  closure params, typed closure arrays, closures-returning-closures (case 5.1),
  higher-order procs (case 5.3). Requires a typed-closure-**value** rep + typed
  calling convention for stored/passed closures.

### Independent (neither A nor B, coordinate separately)

- **Typed `collect`/spread → `[Vec T]`** (debt 2) — terminal materialization.
- **Delete the runtime numeric promotion** (debt 1/5) — a strict-vs-lenient
  **policy** decision, made *after* A (which renders it dead for inline HOF
  predicates). It still serves general dyn-context comparisons (`if [> $dynvar
  3.5]`); deleting it makes those error / require a cast. Decoupled from A —
  typed ops don't route through the promotion path, so they coexist until the
  decision is made.
