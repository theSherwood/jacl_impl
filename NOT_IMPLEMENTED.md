# JACL — Not Yet Implemented

A single index of everything **planned, deferred, half-built, or
design-open** in JACL. Each entry is a short status line and a pointer
to the doc that owns the long version.

If you find yourself adding a new "TODO"-class item, add it here *and*
in the owning doc. Keep entries tight: one paragraph max.

> **Active work-in-progress:** see `TYPED_CLOSURES_DESIGN.md` §"Remaining work"
> — authoritative for current status. **The typed-closures arc is
> functionally complete (2026-06-11):** `[Proc [P…] R]` syntax, closure
> VALUES/PARAMS/RETURNS, user HOFs, closures-returning-closures, named closures
> as values with conformance, collections (`[Vec/Arr [Proc …]]`), closure-valued
> expression results, runtime proc-signature introspection, **struct params &
> returns in `[Proc …]`**, dyn-inferred struct params, direct-calls of inline
> struct literals, **global procs as first-class values**, and **forward
> references as values** (higher-order mutual recursion) — all via a registry
> `TYPE_SHAPE_PROC` + portable proc-shape stamp, no VM change. **Compound types
> in `[Proc …]` work end-to-end incl. invariant param conformance (2026-06-12:
> the compiler unified its per-param conformance encoding on the FULL compound
> shape idx — closes the `[Map K V]` KEY soundness edge and a latent
> def-binding-vs-arg encoding mismatch).** **The typed-closure arc + the
> shape-registry unification (2a–2d) are now fully CLOSED** (2026-06-12): compound
> RETURNS (`[Map K V]`/`[Arr T]`, plain + closure-param + def-binding), verbatim-
> return compound conformance, and the 2c/2d nested-collection narrowing cleanup
> (consumers consolidated onto one helper; portable stamp un-suppressed) all
> landed. Alternative next slices, all indexed below: typed spread (§4.1b),
> map iteration / `for` over maps (§4), the punted dyn-return no-autobox
> consistency call (§4), C-style-for suspension and [Buf]/[Ptr] defs
> across suspensions (§8d/§8e).

Last refreshed: 2026-06-10 (big session — summary, newest first; each
item's full record lives in §4/§8: typed-closures Phase A closed with
struct-returning transform mappers — multi-slot HOF output, `9251467`;
SM wide-rep boundary family fixed — state-field captures,
spawn/parallel wide tails, compound-annotated defs in SM, wide
args/returns across suspending calls, `64e8d6c`; suspending for-loop
BODIES landed incl. the call-only residue (`9f4d5c9`, §8d) and SM-body
struct for-bindings (`00632f9`); collection-type unification finished —
vec/map/arr ≡ [.. dyn], ref-kind erasure stamps, [Map V] removed
(BREAKING, `2fe6788`), no-autobox rule at dyn sinks (BREAKING,
`4fdfc8f`), hard stamped checks, param annotations, nested
shape-carried narrowing, nested compound map values; pre-existing
segfault/corruption fixes: OP_TYPED_MAP_EQ scalar sentinels, boxed-wide
hash/eq identity, [box $wide]/[atom $wide], wide values into plain
vec/map slots). 2026-06-08 (§1b added — the shell / external-command
API redesign in `SHELL_API_DESIGN.md` is now indexed here as a
planned-but-unimplemented target; current behavior is still the
tempfile-based `OP_EXEC` path). 2026-06-02 (§12 `()` infix mode removed — parser's
infix surface deleted (`parser__parse_infix*`, `parser__sync_paren`,
the `$(...)` interp branch), `TOKEN_LPAREN` now fires a "use `[]`"
diagnostic, `TOKEN_DOLLAR_PAREN` fires a "use `$[...]`" diagnostic,
and the `HEAD_DOTDOT_LT` / `HEAD_DOTDOT_EQ` heads were pulled from
`ast.c` / `jacl.h` / `typer.c` / `compiler.c` now that nothing
reaches them. Migration of in-repo `()` sites landed as PR 1
(commit `be1696b`). Tests: `parens_removed_error.jacl` and
`dollar_paren_removed_error.jacl` lock down the error messages.
§8c SM state-field shadowing fix — `sm__add_state_field` now records
storage-shape collisions and `compiler__analyze_suspensions` emits a
compile error pointing at the offending re-declaration, mirroring
non-SM same-scope shadowing. Tests: `sm_field_shadow_repro.jacl`
(expect-error) and `sm_field_same_shape_rebind.jacl` (positive
control). §10 CPS arg-eval bug fixed 2026-05-22 — SM compiler's
SUSPEND_CALL path now spills/restores the enclosing operand stack
across `OP_AWAIT_SM`, matching the discipline already used by
await/sleep/yield/parallel/race. Regression test:
`test/jacl/suspending_call_inline_args.jacl`. §4 annotation-driven
`[Stream T]` typing landed 2026-05-19; for-loop narrowing + yield-
site inference deferred as a Phase 2 unit. §3b generator-tail check
landed and section removed; see compiler.c
`find_disallowed_generator_tail`).

---

## 1. User-facing language features — not yet implemented

Lifted from `SYNTAX.md` § "Not yet implemented" (the canonical table).
Status snapshot:

| Feature | Complexity | Blocking deps | Notes |
|---|---|---|---|
| **`match` / case** | Large | None | Lexed; `HEAD_MATCH` recognized as special form (`compiler.c:2527`); typer rules deferred; no compile/runtime path. May ship as a macro instead of a compiler-path special form — see `DESIGN_CRITIQUE.md` §3.3. |
| **Callable values** (maps/atoms in `[]` head) | Medium | None | `[$colors red]`, `[$config port]`. +50–100 LOC projected. |
| **`$env`** (atom of map, `with-env`, `$home`/`$pwd`/`$pid`) | Medium | Callable values | Bidirectional OS sync via watchers. Watcher dep met by `watch`/`unwatch` (2026-05-19, `ATOM_WATCH_DESIGN.md`). |
| **Aliases** (`alias ll { !ls -la }`) | Small | None | Compile-time syntactic rewrite. Scoping (file vs session) unresolved — `SYNTAX.md` Q21. |
| **Globbing** (`glob`) | Medium | None | Reads `$ctx.pwd`; returns a stream. Pattern engine + brace expansion. |
| **`par-each`** | Medium | None | Concurrent stream processing. Hard part is backpressure (open question in `DESIGN.md`). |
| **Regular expressions** | Medium | Lib exists (see §2) | Literal syntax (`/regex/`) + capture-group bindings + VM bridge. |
| **Bignum / numeric tower** | Medium | Lib exists (see §2) | `JACL_TAG_BIGNUM` exists; no VM arithmetic dispatch. Plan is bigint+bigfloat with implicit promotion in `dyn`. Rationals dropped. |

---

## 1b. Shell / external-command API redesign

Full target design: `SHELL_API_DESIGN.md` (working notes from the
2026-05-20 conversation). It re-shapes `SYNTAX.md` §"Shell interop"
and §"Jobs" — those describe *today's* behavior; the doc describes
the *target* and supersedes the Job/`await` decisions sketched in
`CHANNELS_DESIGN.md`.

Motivation: today's `!cmd` API has eight concrete holes audited
against `vm.c`/`bytecode.c` — no live stdin to a running process,
background stdout dead until exit, second-class stderr, no exit-code
without throwing, no tee/fan-out, no per-stream redirection at the
call site, unspecified pipeline exit-code propagation, and
fake stdin-from-stream streaming (buffers to tempfile first).

The model is settled in broad strokes (Job as the universal
foreground value; uniform running/finished Job shape; `&` as postfix
sugar for `spawn`; pipe-operator family; GC-driven lifetime with a
detached opt-out; non-zero-exit propagation with a `complete`-style
inspection opt-out; an explicit `create-process` low-level
primitive). **Not implemented** — current behavior is still the
tempfile-based `OP_EXEC` path (`vm.c` ~10204–10610).

Gating open question before any spike: **#4, `create-process`'s
fd-record shape** (field names + accepted value types — channel /
path / fd int / `discard`), since it's the primitive everything else
lowers to. Other open questions (cancel/SIGTERM→SIGKILL escalation
ladder, error-opt-out combinator name, per-call env override,
per-stage stderr attribution, `.pid` convention for pipeline Jobs,
migration from string-returning `!cmd`, `exec` vs `create-process`)
shape ergonomics but don't gate the core path. The doc's suggested
first spike: replace `EXEC_FLAG_BG`'s tempfile redirection
(`vm.c:10359-10384`) with `pipe2(O_NONBLOCK)` + worker-idle-loop fd
polling (the §8b idle loop is the natural home), exposed via a
minimal `create-process` that attaches a channel to stdout.
Projected wiring: not yet scoped.

---

## 2. Libraries that exist but aren't wired

Self-contained code sitting in `lib/` waiting for VM integration.
From `DESIGN.md` "Project Layout" and `DESIGN_CRITIQUE.md` §3.2, §3.4.

- **`lib/regex/`** (~2,500 LOC) — Thompson NFA. Needs literal syntax,
  VM dispatch, capture-group binding. Projected wiring add: +300–600
  LOC. (`SYNTAX.md` Q14 — literal syntax TBD.)
- **`lib/bignum/`** (~1,860 LOC after rational deletion) — bigint +
  bigfloat. Planned. Will be both `dyn` promotion targets
  (Python-style overflow promotion) and typed slots (`bigint x`,
  `bigfloat y`). Projected wiring add: +500–1,000 LOC. `rational.h`
  and `test_rational.c` deleted 2026-05-16 — rationals were cut from
  the integration plan per `DESIGN_CRITIQUE.md` §3.2 / §4.4.
- **`lib/segment_array/`** — backs JACL's **`arr` / `[Arr T]`** mutable
  reference array (dyn `[arr ...]`, flat typed `[Arr i32]` / `[Arr Point]`).
  **Mostly implemented** as of 2026-06-08 — M0–M5 shipped (full builtin
  set get/set/push/pop/len, `OBJ_ARR` GC kind + trace + finalizer, SATB +
  generational barriers, identity eq, print/to-string, typer narrowing,
  runtime-stride `sa_var` backing). Scalar get/pop narrowing is now
  complete — i64/u64/f64 narrow to the unboxed wide scalar (Phase 1,
  2026-06-08; the former carve-out is gone), and the scalar constructor /
  `arr-push` / `arr-set` box wide float/large-int literals before the
  byte-packed store (pre-existing zero-store bug, fixed same change). See
  `ARR_DESIGN.md` (the source of truth + handoff). `for [a] x { }`
  iteration shipped (2026-06-08) — dyn/scalar/struct elements, binding
  narrowing for ALL scalars incl. wide i64/u64/f64 (both plain and
  suspending procs, via the general wide-cell + SM fix; see §4).
  **Remaining (M6, ergonomic):** `$a->i` / `set $a->i x` arrow indexing;
  plus one carve-out (struct arrays are pure-value so GC recursion is a
  no-op). Remove this item when arrow lands.

---

## 3. Compiler/VM infrastructure present but unused

Scaffolding that exists in the runtime but no compiler path emits it.

- **`OP_TAIL_CALL`** — handler at `vm.c:2901–2935`; compiler never
  emits it. Compiler-side tail-position analysis pass not written.
  The short-term mitigation (4× bump of `VM_STACK_MAX` / `VM_FRAMES_MAX`
  to 1024 / 256) landed 2026-05-16, so practical recursion depths are
  now comfortable; the durable fix is still the compiler pass. Full
  discussion: `DESIGN_CRITIQUE.md` §3.5.
- **`JACL_FLAG_TAINTED` / `JACL_FLAG_SECRET`** — full bit-flag API
  (`value.c:45-46, 392-400`) preserved across GC (`JACL_FLAGS_MASK`,
  `gc.c:723, 780, 836`). Zero producers, zero consumers. Open design
  question: runtime-only flag vs typed property (`tainted str` as a
  distinct type). Projected wiring: +200–400 (runtime) or +500–800
  (typed). Full discussion: `DESIGN_CRITIQUE.md` §3.4.
- **`match` reservations** — `TOKEN_MATCH`, `HEAD_MATCH`, special-form
  recognition at `compiler.c:2527`. Reserves the word; no
  implementation. See §1 above.
- **Pragma block (`#{...}`)** — removed (was a parsed-and-ignored lexer
  stub with no consumer). `#` is a line comment only.

## Planned

- **Drop the binding operators (`=` / `:` / `::`)** — the `x = 3` (def),
  `x : 3` (mut), `x :: 3` (set) infix sugar is slated for removal in favor
  of the prefix `def`/`mut`/`set` forms only. `ctx` will take its default by
  juxtaposition (`ctx i32 x 0`) rather than `=`. Validated on a scratch pass
  (the `.jacl` corpus migrates cleanly; proc-valued bindings become named
  procs); remaining work is the C-test embedded-JACL migration (~25 files),
  removing the operators from the lexer/parser/compiler, and rewriting the
  binding/ctx parser+typer assert tests. A coherent all-or-nothing change —
  do it in one dedicated pass.

---

## 4. Type system — deferred items

From `TYPE_SYSTEM.md` § "Deferred items". No concrete workload
pulling on them; revisit when one shows up.

- **Closure literal call signatures.** Anonymous closures get
  `TYPE_CLOSURE` but the typer doesn't carry their param/return
  signature, so calls to a captured anon closure stay `dyn`. +150–300
  in the typer. **PARTIALLY ADDRESSED (Phase B B1+B2, 2026-06-10):** an
  inline literal bound under a `[Proc [P…] R]` annotation
  (`def [Proc [i64] i64] f [proc {x} {…}]`) now carries a concrete
  signature (on `TyperBinding`/`Local`/`GlobalArity`) and is called through
  the typed unboxed convention. **Closure PARAMS too (B3a, 2026-06-10):** a
  `[Proc …]` proc param interns a registry `TYPE_SHAPE_PROC`; an inline
  closure-literal arg is monomorphized to the param's signature and the body
  call `[f …]` is typed-unboxed — user-defined HOFs work. **Closures-RETURNING-
  closures too (B3b, 2026-06-10):** a `[Proc …]` return type monomorphizes the
  body-tail closure literal and the returned signature propagates to bindings
  (`def add5 [make-adder 5]` → `[add5 3]` typed). **Named closures as VALUES too
  (B3d, 2026-06-10):** `[apply $g 5]` and `def [Proc…] f $g` conformance-check a
  named typed-closure's signature (resolved from its local) and pass/bind it.
  **Collections of closures too (B3c-vec + B3c-arr, 2026-06-11):** both
  `[Vec [Proc …]]` and `[Arr [Proc …]]` construct (literal elements
  monomorphized, named elements conformance-checked) and a for-loop narrows the
  element to a typed closure (typed unboxed call). **B3c follow-ups done:**
  vec-get/arr-get element narrowing and closure-literal push monomorphization.
  **Beyond B (2026-06-11):** struct params/returns in `[Proc …]`, dyn-inferred
  struct params, direct-calls of inline struct literals, **global procs as
  first-class values** (`$dbl` narrows), and **forward-ref-as-value** (proc used
  before its definition; higher-order mutual recursion). **Compound types inside
  `[Proc …]` work** — `[Vec/Arr/Map …]` and nested `[Proc …]` params with
  invariant conformance (the registry-unification foundation made the shape idxs
  portable), and as of 2026-06-12 the compiler carries the FULL compound shape
  idx as its per-param conformance encoding, so a map's KEY is conformance-checked
  (`[Map str i64]` ≠ `[Map i64 i64]`) and `def [Proc [[Vec/Map …]] …] f $proc`
  binds correctly. **Compound RETURNS now narrow too (2026-06-12):** `[Proc [..]
  [Arr T]]` and `[Proc [..] [Map K V]]` PARAMS narrow the closure call result
  (key carried for maps), on the same footing as the `[Vec T]` return. Still
  `dyn` / unsupported (now a SHORT list — see `TYPED_CLOSURES_DESIGN.md`
  §"Remaining work" for the authoritative version): a closure with NO `[Proc …]`
  annotation. (The 2c/2d nested-collection narrowing cleanup landed 2026-06-12 —
  consumers consolidated onto one helper; portable stamp un-suppressed.)
- **Imported struct exports field-typing.** Imported struct types go
  through the CapitalCase placeholder pre-pass with empty fields.
  Field access on imported structs stays `dyn` at the typer level
  (codegen still works via the compiler's shared struct registry).
  +200–400 LOC; needs cross-module struct-idx alignment.
- **`[swap $box fn]` struct-element narrowing.** Scalar-element swap
  narrows; struct-element swap stays `dyn`. Needs `OP_SWAP_INLINE`
  parallel to `OP_DEREF_INLINE`. +50–100 LOC.
- **`match` arm-walk.** Typer rules for `match` patterns. Dead code
  until the feature itself lands.
- **Vector type unification — `vec` ≡ `[Vec dyn]` (decided 2026-06-10;
  foundation landed).** The `[Vec T]` family is the fundamental concept and
  plain `vec` is shorthand for `[Vec dyn]`. The element type is static; the
  REP is an implementation detail chosen per element kind: value types
  (numeric scalars/bool/structs) keep the strided GC-opaque rep (the rep
  win); ref kinds (str, dyn, and eventually nested collections) use the
  plain traced vec rep with the element type carried statically (TYPE_VEC +
  elem stamp — the same scheme typed streams use). Ref-element vecs widen
  to [Vec dyn] through dyn slots (like scalars); value-element vecs stay
  opaque typed-vector values through dyn (inherent to the rep split).
  **Landed:** `[[Vec str] …]` / `[[Vec dyn] …]` constructors (plain OP_VEC +
  compile-time element checks), def-binding stamp propagation, for-binding
  narrowing over stamped vecs, `collect [Stream str]` stamped `[Vec str]`
  (runtime rep unchanged — lines tests untouched). Tests:
  `vec_ref_elems.jacl`, `vec_ref_elem_error.jacl`. **Nested compound
  element types LANDED (same day):** `[Vec [Vec T]]` / `[Vec [Map K V]]`
  constructors (plain OP_VEC; element checks via portable stamps), def
  bindings carry the TYPER-SIDE element shape idx (interned via
  `typer__nested_elem_shape`; per the cross-registry rule shape idxs live in
  bindings only — AST stamps stay UINT32_MAX and `typer__infer_var_ref`
  suppresses them), and HEAD_FOR narrowing resolves the shape from the
  binding (var-ref receiver) or re-derives from the ctor head (literal
  receiver), decoding to PORTABLE inner encodings — so nested for-loops
  narrow all the way down ([Vec [Vec i64]] → row is [Vec i64] → x is i64;
  inner ref-kinds like [Vec [Vec str]] bind rows as plain-rep'd [Vec str]).
  Test: `vec_nested_elems.jacl`. **Tail landed (same day):**
  depth-N element type expressions (typer__nested_elem_shape recurses;
  shape idxs nest inside the typer's registry and HEAD_FOR's decode binds an
  inner-shape element as a ref-element TYPE_VEC carrying the inner shape —
  the recursion point for depth-N narrowing), explicit `def [Vec str/dyn/
  [Vec T]] v` annotations (both typer annotation resolvers patched — the
  def-handler site at ~typer.c:1190 AND typer__resolve_return_type),
  vec-get narrowing on stamped vecs, vec-push/vec-set element expected-type
  on stamped vecs. Test: `vec_ref_tail.jacl`. **[Map K V] ref-kind
  generalization LANDED (2026-06-10, foundation):** str (and explicit dyn)
  KEYS are first-class on the TYPED map rep — keys are one tagged GC-traced
  slot, so the lift was static-only (`[Map str i64]`, key checks, map-keys →
  plain `[Vec str]` both reps — VM OP_TYPED_MAP_KEYS routes str-sentinel
  keys to the traced vec). Ref-kind VALUES (`[Map K str]` / `[Map K dyn]`)
  select the PLAIN traced map rep with key/value stamps
  (TYPE_MAP + inferred_key_struct_idx/inferred_struct_idx — the [Vec str]
  erasure scheme): ctor checks both reps, def propagation + `def [Map K
  str]` annotations, map-get value narrowing, set/remove stamp persistence,
  keys/vals `[Vec str]` stamps, key/value expected-type narrowing. **The
  one-arg `[Map V]` form was REMOVED (same day, design decision):** maps
  are always `[Map K V]`, `map` is shorthand for `[Map dyn dyn]`, and dyn
  keys are spelled `[Map dyn V]`. Every resolution surface (ctor, def
  annotation, params, return types, buf elements, nested element shapes)
  rejects or no longer recognizes the one-arg form; ctor/def/params/return
  emit a targeted "[Map V] was removed" migration diagnostic (test:
  `map_v_removed_error.jacl`, `test_typed_map_v_form_removed`). Internally
  the dyn-key machinery (key idx UINT32_MAX / VM 0xFFFF) is unchanged —
  `[Map dyn V]` normalizes the `dyn` keyword to it at every resolver.
  Numeric plain-rep keys box consistently (i64 literal keys at ctor and
  lookup sites); en route two latent runtime bugs were fixed:
  OP_TYPED_MAP_EQ scalar-sentinel OOB (segfault on `== [Map dyn i64] ...`,
  prior commit) and
  jacl_val_hash/jacl_val_eq comparing boxed wide scalars (i64/u64/f64) by
  POINTER — they now hash/compare by value, making boxed wide ints work as
  dyn-map keys everywhere. Tests: `map_ref_kinds.jacl`,
  `map_ref_kind_error.jacl`, `typed_map_scalar_eq.jacl`. **Map tail still
  open:** struct keys with ref values (no tagged rep for bare structs —
  compile error), nested compound VALUES (`[Map str [Vec i64]]` — needs the
  typer-side shape scheme HEAD_FOR uses for vecs), proc-param/extern `[Map
  str ...]` annotations + typer__resolve_return_type, for-loop narrowing
  over stamped maps, and cross-width numeric key identity (an i32-tagged
  dyn key never matches a stored boxed-i64 key — same-tag identity only;
  belongs to the numeric-tower work). **[Arr T] ref-kind mirror LANDED
  (2026-06-10):** `arr` ≡ `[Arr dyn]`; `[Arr str]` uses the DYN arr rep
  (tagged traced slots) with the element type static as TYPE_ARR + stamp:
  ctor checks, `def [Arr str]` annotations (the compiler def path now
  recognizes `[Arr …]` as a compound annotation at all — it errored
  "def type must be a keyword" before), def propagation, arr-get/arr-pop
  narrowing, push/set expected-type + stamp persistence, for-binding
  narrowing. Tests: `arr_ref_elems.jacl`, `arr_ref_elem_error.jacl`.
  **No-autobox rule (2026-06-10, design decision):** dyn slots hold tagged
  values; a wide scalar (i64/u64/f64) has no tagged form, so storing one
  in ANY dyn collection sink (plain vec/map/arr ctors + mutators, dyn-key
  map slots) is a COMPILE ERROR — explicit `[box …]` is the only dyn form
  of a wide scalar (`compiler__reject_wide_dyn`). This replaced both the
  short-lived auto-boxing AND pre-existing silent corruption (plain
  vec-push/map ctor stored raw wide bits into tagged slots). Declared
  typed slots ([[Arr i64]] stores, [Map i64 str] keys) keep their internal
  box/byte-pack bridging — annotation-driven, static type preserved.
  `[box $wide]` / `[atom $wide]` themselves now bridge correctly (they
  stored raw wide bits before — deref read back garbage). Tests:
  `{arr,vec,map}_wide_dyn_error.jacl`. **Open consistency question
  (punted 2026-06-10, user call):** a dyn-RETURN proc still implicitly
  boxes a wide tail (`wide_proc_return.jacl`) while yield in an
  unannotated generator errors — the no-autobox principle suggests the
  return boundary (and dyn proc args) should error too.
  **Collection-typing tail CLOSED (2026-06-10):** hard element/key checks
  on stamped plain-rep mutators (`compiler__check_stamped_elem`; tests
  `{vec,arr}_push_stamped_error`, `map_set_stamped_error`); proc-param
  collection annotations — ref kinds → plain-rep param types with body
  narrowing, numeric scalars → typed rep via sentinel, `[Arr T]` params
  recognized at all reps (test `coll_ref_params.jacl`); vec-get narrowing
  through nested shape-carried bindings incl. depth-3 def re-derivation
  (test `vec_nested_get.jacl`; also fixed: inner maps with ref VALUES
  were decoded TYPE_TYPED_MAP at the nested ctor check / HEAD_FOR —
  plain-rep elements would have run typed-map opcodes; new
  `typer__decode_map_shape` applies the rep rule at every decode site);
  nested compound map VALUES (`[Map str [Vec i64]]` ctor both layers,
  key stamps + binding-carried value shape, map-get portable decode,
  test `map_nested_vals.jacl`). **Remaining (this area):** for-loop
  iteration over maps — DESIGN SETTLED, IN PROGRESS (2026-06; see the §"for over
  maps" entry below and `SYNTAX.md` §"`for` — unified iteration"); narrowing over
  stamped maps rides it;
  proc RETURN-type collection annotations LANDED for `[Arr T]` and `[Map K V]`
  (2026-06-12): `typer__resolve_return_type` now resolves `[Arr T]` returns
  (value + ref kind; the compiler's 4-arg proc-form recognizes the `[Arr …]`
  head) and threads a `[Map K V]` return's KEY (new out-channel +
  `TyperProc.return_key_struct_idx`), so a plain `proc f {} [Map i64 i64] {…}`
  types `map-get` on its result and a `[Proc [..] [Arr/Map …]]` PARAM narrows
  the closure call result fully (tests `proc_return_collection`,
  `typed_closure_compound_return_{arr,map}`). Def-BINDING a compound-returning
  closure (`def [Proc [..] [Vec/Map ..]] f $proc`) works too (2026-06-12 — was a
  segfault; the def-binding path decodes the return shape to the element/value
  idx + map key, matching the param convention; test
  `typed_closure_compound_return_def`). STILL OPEN: cross-module element-type
  alignment (existing carve-out).
  **Still open (vec/arr):** hard vec-push element CHECKS (currently
  expected-type narrowing only), proc-param [Vec str]/[Arr str]
  annotations, and vec-get narrowing through nested (shape-carried)
  bindings.
- **`for` over maps + the enumerator form — LANDED 2026-06 (phases a+b+c+dyn).**
  One binding rule across all collections: the LAST name is
  the value/element; an optional FIRST name is the *enumerator* — the `i32` index
  for vec/arr, the key `K` for maps. So `for $vec v` / `for $vec i v` is
  `enumerate`, `for $map v` / `for $map k v` is `values` / `items`. A single name
  over a map binds the VALUE (not the key — opposite of Python; deliberate,
  follows the rule). Order is hash/HAMT (unspecified), matching
  `map-keys`/`map-vals`. Full surface spec: `SYNTAX.md` §"`for` — unified
  iteration". **What landed:** (a) parser/typer two-name form + map narrowing;
  (b) compiler map iteration for STATICALLY-typed maps (TYPE_MAP/TYPE_TYPED_MAP)
  — materialize `map-vals` (single) / `map-keys`+`map-vals` (two names) from the
  map at __col, iterate the resulting vec(s) on the vec-loop scaffolding,
  `keys[i]`↔`vals[i]` by HAMT determinism; **runtime DYN dispatch** — a
  statically-dyn `$coll` is checked with `OP_IS_MAP` and iterated as a dyn map
  (replace __col with map-vals, stash map-keys) or a dyn vec, the enumerator
  resolved per-iteration (key vs i32 index); (c) STATICALLY-typed vec/arr
  two-name form — the enumerator binds the loop's `__idx` under the user's name
  as `i32` (a separate per-iteration copy, so body writes don't perturb the
  counter); STREAM enumerate — `for $stream n v` binds `n` to a 0-based `i32`
  counter maintained at the loop tail (the pull carries no index), incremented
  after the continue target so `continue` advances it. Tests: `for_map`,
  `for_map_continue`, `for_map_struct_err`, `for_dyn_dispatch`, `for_enumerate`,
  `for_stream_enum`. **Open / deferred (clean errors today):** struct keys/values
  in a map for-loop (inline-slot binding); suspending map-loop bodies + the
  enumerator form in a suspending body (SM lowering); a lazy HAMT-iterator
  lowering (avoids the two materialized vecs).
- **Parameterized stream element type — for-loop narrowing & yield
  inference (Phase 2).** Annotation-driven typing landed 2026-05-19
  (`compiler__stream_type_expr`, `typer__stream_type`,
  `compiler.c:7891` generator-return + parser
  `parser__parse_proc_form`). `proc [Stream T] gen {} { [yield X] }`
  now parses, the typer carries the element idx via
  `proc->return_struct_idx`, and yield arg-mismatch fires for
  typed→typed and dyn→typed cases (typer.c yield-element check).
  Range / `..=` / `lines` builtins stamp i64 / str on
  `inferred_struct_idx`. Two pieces of the original §4 stream item
  remain deferred:

  1. **For-loop binding narrowing.** Landed for `arr` AND `vec`, all
     scalars (2026-06-08): `for [a] x { }` over `[Arr T]` / `[Vec T]`
     narrows the binding to the element scalar (typer `HEAD_FOR` case +
     compiler arr/vec branches), incl. wide `i64/u64/f64`; struct-element
     arrs/vecs narrow to the struct. (The vec scalar path also fixed a
     pre-existing **segfault**: scalar `[Vec i64]` for-loops ran
     `struct__slot_width` on a scalar sentinel — `vec_for_scalar.jacl`.)
     Wide scalars work in **both** plain and
     suspending (SM) procs: non-SM loops store the wide binding in a typed
     local; SM loops store it **boxed** in the GC-traced state field and
     the var-ref read unboxes via `node->inferred_type`. This was part of
     a general **wide-cell + SM fix** (same session): `def`/`mut` wide
     locals now keep their narrowed type across a suspension (boxed on
     store via `ensure_boxed` in the def/mut/set SM paths; unboxed on read
     at the state-field var-ref site), the `mut i64`-across-suspension
     `nil` bug is fixed, and wide scalars returned from a dyn-return proc
     are boxed at the return boundary (`compiler__emit_return` +
     `compile_sm_stmts` tail). Tests: `arr_for_wide`, `sm_wide_scalar`,
     `wide_proc_return`, `vec_for_scalar`.

     **Stream for-bindings now narrow too (2026-06-09).** `for x in [gen]` /
     `[range …]` / `[lines …]` narrows the binding to the element type via
     the **option-1 mechanism**: the producer still yields tagged values, and
     the for-loop **unboxes the pulled value to the wide rep based on the
     static type** (no per-element unbox *op* needed — it reuses
     `OP_TO_I64/U64/F64`). `take`/`filter` propagate the element type so
     derived-stream for-loops narrow; `transform` infers the output element
     from the lambda's return type. **Caveat:** the producer-rep stays tagged
     (the full "option A" producer-wide flip is still pending), and the
     `transform` lambda-return inference is a one-off special case the user
     has flagged for a rethink. **The active design decision + the remaining
     typed-stream work (typed `collect`, consumer-untyped errors,
     struct-element streams) lives in `TYPED_CLOSURES_DESIGN.md` — start there.
     (Producer-wide rep for i64/u64/f64 has since landed.)**
  1b. **Struct-element streams — channel + for-loop + HOF monomorphization
     LANDED (2026-06-10); typed collect still open.** What shipped:
     `OP_YIELD_SM_WIDE` (yield pops the struct — inline N slots or heap ptr,
     via `vm__pop_struct` — into `vm->yield_wide`, raw value bytes, never
     GC-traced), the generator pull copies it to the consumer's buffer, and
     the for-loop pulls via `OP_STREAM_NEXT_INLINE` + binds an N-slot inline
     local (`OP_INLINE_TO_LOCAL`, same shape as the arr/vec struct loops);
     typer narrows stream struct bindings (the old "streams stay dyn" carve-
     out is gone). Box-at-yield was rejected (would violate STRUCT_DESIGN's
     no-auto-box rule); `yield_value` stays nil for struct yields. **HOF
     monomorphization (same day):** inline transform/filter/each callbacks
     over struct streams compile with a by-value struct param (typer
     `keep_typed` + compiler `hof_param_enc` adoption → the existing
     `param_total_slots` machinery); the lazy pulls push N bitmap-marked
     inline slots with `stack_base = stack_top - param_total_slots`,
     mirroring the eager OP_TYPED_TRANSFORM loop; `take` is a rep-agnostic
     passthrough. **Guarded (error instead of the old silent nil):**
     collect/spread/userland `stream_next` (runtime guards in
     `vm__pull_stream_dyn` + the inline SM loops), NAMED/dyn HOF callbacks
     over struct streams (compile error + `has_inline_params` runtime
     defense — no legal boxed path exists), struct-RETURNING transform
     mappers (multi-slot HOF *output* channel not yet wired), struct yield into an unannotated generator (typer + compiler errors).
     Typed `collect` → `[Vec T]` LANDED (same day; debt 2 — wide elements
     stored flat, struct streams collect to [Vec Struct]; str stays plain
     vec since typed-vec storage is GC-opaque). **Struct-RETURNING
     transform mappers LANDED (2026-06-10, multi-slot HOF OUTPUT
     channel):** the mapper compiles with a TYPE_STRUCT return via
     `c->hof_mapper_ret_enc` (HEAD_PROC adoption), the transform pull pops
     the N-slot result via vm__pop_struct, and the stream's elem_idx
     becomes the struct idx — downstream for/chained-HOF/typed-collect
     ride the existing struct channel; works for struct AND scalar inputs.
     Also fixed en route: def-bound typed streams kept NO element stamp
     (no TYPE_STREAM def-propagation arm) — `def s [transform …]` then
     `for [filter $s …]` hit the dyn-consumer runtime guard. Test:
     `stream_struct_ret.jacl`. **SM-body struct for-bindings LANDED
     (2026-06-10):** in a suspending proc (await/parallel/race make the
     liveness pass keep every local in SM state) a struct-element
     for-loop binding now lives in a WIDE state field — sm__walk_locals
     registers width + struct idx from the collection stamp, the pull
     stores N raw slots via OP_SET_STATE_FIELD_WIDE (which now also
     clears the vacated stack inline bits — latent stale-bitmap hazard),
     var-refs read via OP_GET_STATE_FIELD_WIDE, GC skips the slots. All
     three collection kinds (stream / [Vec T] / [Arr T]); the vec/arr
     struct loops previously MISBEHAVED silently in SM procs (stores hit
     the inline local, reads hit the 1-slot state field → nil). Test:
     `sm_struct_for.jacl`. Still open: typed spread; for-loop BODIES
     that suspend (see §8d — a separate, broader limitation). Tests: `stream_struct_for.jacl`,
     `stream_struct_hof.jacl` (each/transform/filter/chained, 3-slot
     elements), `stream_struct_hof_named_error`,
     `stream_struct_{collect,yield_dyn}_error`.
  1d. **For-loop bodies cannot suspend at all (§8d).** Indexed below.
  1c. **✅ FIXED (2026-06-10) — SM-generator rep-mismatch bug family.** All
     were one flaw in different clothes: SM compile paths for liveness-culled
     locals ignored the value's representation, while typed consumers
     (typed arithmetic, struct byte-pack, field access) trusted the static
     type. Fixes, all in compiler.c: (a) the def-SM fall-through now gives
     culled locals the same typed treatment as non-SM locals — wide scalars
     stay RAW in a typed local (no ensure_boxed), inline structs get
     width/is_inline/padding + `OP_STRUCT_STORE_INLINE` for heap RHS (was:
     everything boxed into ONE untyped dyn slot → tagged bytes byte-packed
     into struct fields; N-slot struct misregistered → slot misread as heap
     pointer → segfault); (b) the mut-SM fall-through now saves TypeInfo
     like the non-SM mut path (cell reads unbox wide via the static type);
     (c) the SM state layout now infers a struct field's width from the
     TYPER STAMP on the RHS (covers `def v [proc-call]` — annotation and
     constructor-head inference both missed it, so raw `OP_RETURN_WIDE`
     bytes landed in a 1-slot GC-traced field); (d) the WIDE state-field
     store expands an INLINE_NONE (heap) RHS via `OP_STRUCT_EXPAND`,
     mirroring emit_return. Regression tests: `sm_gen_struct_local`,
     `sm_gen_wide_ctor_fields`, `sm_gen_struct_from_call`, `sm_gen_mut_wide`,
     `stream_struct_wide_fields` (3-slot stream element with computed wide
     fields, end-to-end). Related cheap follow-up: dropping the typer
     restore-pass for tagged-fit scalars (i32/u32/f32/bool).
  2. **Yield-site inference.** Today an unannotated generator stays
     `[Stream dyn]`; the typer doesn't walk yield expressions to
     unify their types. Annotation is the only narrowing path. Adding
     inference needs a unification policy for mixed yields
     (error vs widen-to-dyn) and treatment of conditional yields,
     which is real design work — see the parent design conversation
     before implementing.

  Cross-module element-type alignment for streams exported from
  another file also stays deferred (mirrors the imported-struct
  field-typing carve-out in this same section).

---

## 5. Architecture-level open design questions

From `DESIGN.md` § "Open Questions". Not implementation TODOs —
decisions that need a call before code can be written.

- **Type system:** Numeric tower / bignum integration (see §2).
  Variant types.
- **Concurrency:** Tainted/secret flag interaction with cross-thread
  communication. Channel primitives (may not be needed given atoms +
  futures). Backpressure / bounded concurrency for `par-each`.
  Job/Future cancel semantics — see `SYNTAX.md` Q19 below.
- **GC:** Scheduling heuristics (current: adaptive allocation-byte
  threshold; tuning is open).
- **Errors:** Error chaining / wrapping. Whether to mark procs as
  error-capable explicitly or trust propagation. Whether stack
  overflow should be catchable via `try`/`catch` like other errors —
  see §10 for the current inconsistency.
- **Modules:** Search path / resolution rules beyond
  relative-to-importer. Versioning / dependency management. Visibility
  beyond underscore convention (`pub`/`priv` keywords if encapsulation
  needs grow).
- **Macros:** Whether all builtins should be rewritten as macros where
  possible.

---

## 6. Syntax-level open design questions

From `SYNTAX.md` § "Needs design work". Only unresolved items
listed (resolved ones are in the source doc).

- **Q2 / Q16. Boolean/logical operators.** `and`/`or`/`not`:
  word-form, symbol-form, or both? Precedence in `()` infix mode?
  Connects to operator overloading.
- **Q14. Regex literal syntax.** See §1 / §2.
- **Q15. Operator overloading.** Desired but not designed. Operators
  should be user-definable.
- **Q17. Standard library surface.** What's builtin vs module?
- **Q19. `cancel` semantics.** Current impl: `[cancel $job]` →
  `[signal $job SIGTERM]`. No grace-then-SIGKILL fallback. What
  `cancel` should mean on a plain JACL Future (cooperative
  cancellation) is open.
- **Q21. Alias scoping.** File-scoped like `def`? Importable from
  modules? Or session/config-level like `.bashrc`?
- **Q24. `$ctx` vs `$env` relationship.** Does `$ctx.env` subsume
  `$env`? Which does `!cmd` inherit?

---

## 7. Struct value-type — open

From `STRUCT_DESIGN.md` § "Open Questions".

- **Wide cells / globals.** Mut bindings and globals store `JaclVal`
  slots; structs must go through `[box]`. A wide-cell architecture
  could allow inline mut struct bindings; not pursued.

---

## 8. Generator state machine — future work

From `GENERATOR_STATE_MACHINE.md` § "Future work".

What already exists: a **layout-time** liveness pass
(`sm__optimize_state_layout`, `compiler.c:2249-2325`) that filters out
locals whose write-and-last-read sit in the same inter-suspension
segment — those become normal stack locals instead of SM state
fields. Two restrictions on this pass leave real work on the table:

- **Yield-only.** The pass bails out the moment any `await` /
  `parallel` / `race` appears (`compiler.c:2256-2261`) because their
  diamond control flow (inline-already-resolved vs real-suspend-then-
  resume) breaks the linear segment numbering the current algorithm
  assumes. So async procs carry every local in the SM, whether it
  survives a suspension or not. Lifting this needs a CFG-based
  liveness pass rather than the existing linear segment walk.
- **Per-yield-point clearing, deferred.** A field that crosses *some*
  yield (so layout keeps it) can still be dead after a *specific*
  yield in the middle of the body — e.g., a generator that loads
  buffer A, yields N times from A, loads B, yields M times from B.
  Today A's state field stays live for the whole stream's lifetime;
  GC can't reclaim what A pointed to until the SM itself dies. A
  per-yield-point pass would clear dead fields at each yield site,
  shrinking retained memory and reducing major-GC tracing work.
  Needs a new `OP_CLEAR_STATE_FIELD` opcode (`bytecode.c:187-198`
  has get/set variants, no clear).

Best done together: a CFG-based liveness pass replaces the existing
linear one and emits per-yield-point clears in the same lowering,
killing both restrictions at once. Estimated +300-600 LOC. Not
measured as a bottleneck; no workload today retains large heap
values across phase-distinct SM lifetimes long enough to matter.

---

## 8d. For-loop bodies CAN now suspend (lifted 2026-06-10) — residue

`for $coll x { yield … / await … }` works: when the body directly
contains a suspension (`sm__loop_body_suspends`), the loop compiles in
SM style — ALL iteration state (collection / index / length / binding)
lives in synthetic SM state fields (`__f{c,i,l}_<line>_<col>`, minted
identically by the locals walk, the liveness walk, and the compile
path), and the body's suspensions ride the normal resume-dispatch
machinery (the entry dispatch jumps straight into the loop; the
backward OP_LOOP re-reads state fields — stack locals die at every
suspension, so none are used). Streams pull via OP_STREAM_NEXT(_INLINE);
vec/arr/typed collections index via state-field __idx/__len. Struct
elements ride the WIDE field channel; break/continue work, incl. across
suspensions. Test: `sm_for_suspend.jacl`. **Residue lifted (2026-06-10):**
bodies that suspend only via a CALL to a suspending proc now take the SM
lowering too — the suspension map is threaded into `sm__walk_locals`,
so the walk and the compile gate agree. Still out: the C-style
`for {init; cond; step}` form; the HOF callback form (`for $coll $cb` —
callbacks can't suspend by design).

---

## 8e. SM wide-rep boundary bugs — FIXED (2026-06-10)

The "closures can't capture SM state fields" report decomposed into a
family of wide-rep boundary bugs, all fixed in one pass (the capture
machinery itself — `is_local=2`, copy-at-creation — was already
correct):

- **Captured wide-typed state fields** (`def i64 x [await $f]` →
  `[spawn { $x }]`): the field stores the value TAGGED, but the closure
  body's typed read consumed the box as raw wide bits. Upvalue reads now
  unbox when the upvalue is dyn-stored and the binding is statically
  wide (mirrors cell/global/state-field reads).
- **Spawn/parallel body wide TAILS**: `[spawn { [* $x 2] }]` with a wide
  tail resolved the future with raw bits (no boundary boxing — futures
  hold traced JaclVals). Both body compiles now box wide tails.
- **Compound-annotated defs invisible to the SM walk**: `def [Future
  i64] f …` (and [Vec T]/[Map K V]/[Arr T]/[Box T]/[Stream T]) was never
  registered as a state field — the binding stayed a stack LOCAL, dead
  after any suspension and stale across loop iterations. Registered now
  (1 tagged slot). [Buf N T] / [Ptr T] keep their pre-existing local
  treatment (multi-slot stack alloc / wide rep — separate work).
- **Wide ARGS to suspending calls**: `[f $wideI64]` landed raw wide bits
  in the callee SM's GC-traced param fields (trace hazard + unreadable).
  Suspending-call args now box; param reads already unboxed.
- **Wide RETURNS from suspending procs**: a declared-i64-return SM proc
  completes through a future (tagged channel); explicit returns now box
  regardless of declared type, and the typed call site unboxes after its
  await.
- The SM for-loop got the per-iteration inner body scope it was missing
  (culled locals no longer accumulate stack slots across iterations),
  and `call_suspend` no longer overwrites the inner error message.

Tests: `sm_capture_and_calls.jacl`; the m13 suspending-callback test
flipped to assert the now-working call-suspending for body.

---

## 8b. Sleep timers — known limits

Shipped 2026-05-18 (initially as a dedicated OS thread per `Runtime`);
the dedicated thread was eliminated 2026-05-18 in favor of polling from
the worker idle loop. `runtime__poll_timers` runs once per worker idle
iteration, splicing expired entries off the deadline-sorted list and
dispatching SM resumptions via the existing `schedule_sm_resumption`
path. Firing latency is bounded by the worker idle-park (currently 1ms
via `COND_WAIT_FOR_MS`; see `AUDIT.md` §11). Remaining corners:

- **O(n) insertion.** Timer entries are kept in a sorted singly-linked
  list. Fine for small concurrent-sleep counts; replace with a binary
  heap if a workload pushes into the hundreds.
- **No cancellation.** `[timeout n body]`'s losing branch keeps running
  after the race settles — its eventual result is discarded but it
  still occupies a worker. Connected to `SYNTAX.md` Q19 (`cancel`
  semantics, see §6).
- **Wake granularity.** Sub-millisecond deadlines round up to the
  worker idle-park granularity (1ms today).
- **Coupling to audit §11.** Firing latency now equals the worker idle-
  park timeout. When §11 is tackled (lengthen or eliminate the 1ms
  tick), the new park timeout MUST be capped by
  `min(idle_park, timer_head->deadline_ns - now)` or sleeps will
  silently take however long the next non-timer wakeup happens to
  arrive. Flagged inline in `runtime.c` at the timer block.
- **Emscripten / single-threaded builds.** Single-threaded mode has no
  runtime, so `OP_SLEEP_BLOCK` (toplevel `nanosleep`) is the only
  path. If a single-threaded runtime ever ships, sleep wakeups would
  fire on whatever single-worker idle path it uses — no longer a
  silent no-op since there's no dedicated thread to be a no-op.

---

## 9. Runtime audit items — pointers into AUDIT.md

`AUDIT.md` is the canonical home for runtime open items. Pointers only:

- **§11. Worker idle CV-park timeout.** Workers park with a 1 ms
  timeout for wake paths the signaling code can't reach. 81% of CPU
  on `spawn_chain`. Real fix needs serial-vs-parallel workload
  distinction at the submit site. Attempted 2026-05-15 and reverted
  (5× regression on `spawn_chain`). See `AUDIT.md` §11 for the lesson
  and candidate strategies. **Extra constraint as of 2026-05-18:**
  the timer thread was deleted in favor of `runtime__poll_timers`
  running on the worker idle path, so the 1ms tick is now load-
  bearing for `sleep` latency. Any fix must compute its park timeout
  as `min(idle_park, timer_head->deadline_ns - now)` — see §8b.
- **§12. `JaclFuture` waiters list spinlock.** Not observed under
  contention. Consider parking after N spins if it ever surfaces.
- **§17. Inconsistent thread-local convention.** Scattered globals
  (`gc__current_heap`, `gc__thread_epoch`, etc.) instead of a threaded
  `JaclCtx*`. Larger refactor; not a sandbox prerequisite.
- **Performance smells (deferred):** see `AUDIT.md` § "Performance
  smells" — HAMT/RRB TLS-heap-read, steal-from-every-worker,
  `gc__find_fit_in_block` residual, `gc_sweep_concurrent` rebuild,
  sweep/mark main-body dedup. None concentrated enough to chase
  under current workloads.  (`jacl_is_heap_type` tag chain was
  resolved 2026-05-22 — see commit `405932f`.)
- **§9 read-side operand-stack rooting hole.** Theoretical UAF;
  no concrete bug observed. Convention (CPS-only concurrency)
  prevents it today. See `AUDIT.md` § "Known theoretical hole".

---

## 10. Known limitations (will-not-fix-soon)

From `DESIGN.md` § "Known Limitations".

- **Escape analysis: boxes in collections are not tracked.** The
  escape pass detects direct mutable captures and transitive captures
  through closures, but does *not* track box/cell refs stored in
  vectors/maps. A box stashed into a vector that's then passed to
  `spawn` will not pin the task. Fixing requires taint tracking
  through collection ops (`vec-push`, `map-set`, ...).
- **"ctx-pure" pruning is future work.** The compiler does not mark
  procs as ctx-pure to skip the fork. `with-ctx` always pays the fork
  cost when entered. Future analysis pass.
- **Stack overflow is not catchable at toplevel.** JACL's error model
  is "errors are values that propagate" (`try`/`catch` catches anything
  with the error flag), but stack overflow bypasses it. Two different
  behaviors today for the same condition:
  - *Inside `spawn`/`parallel`/`race`:* overflow becomes a regular
    error value on the future; `await` surfaces it; downstream
    `try`/`catch` catches it. This is the §16 path
    (`runtime__make_completion_error`) and matches the design intent.
  - *At toplevel / inside `try`:* overflow returns `VM_RUNTIME_ERROR`
    (or `VM_STACK_OVERFLOW`) directly from `vm__run` to the C caller,
    bypassing the in-VM error-value machinery. Verified empirically:
    `try { rec 0 } e { ... }` with infinite-recursion `rec` does not
    catch — the program exits with the message and any post-`try`
    code never fires.

  Making overflow uniformly catchable requires reserving headroom (a
  few stack slots + frames) so the error-value push and unwind can
  run, plus growing every overflow check site to route through the
  in-VM error machinery instead of returning to C. The hard-fatal
  floor stays as defense-in-depth (overflow-during-`catch`-body
  itself can't be catchable — that's where you have to give up). The
  smaller-effort alternative is to declare overflow fatal-by-design
  (machine-limit, not user-recoverable) and document that `try`
  won't catch it — bash-flavored honesty, but it forecloses the
  "retry with a different strategy" pattern.

  Tests covering current behavior: `test_vm.c:227` (operand stack),
  `test_vm.c:1633` (error_line), `test_vm.c:2190` (call frames),
  `test_compiler.c:2760` (compiler-emitted recursion), and the
  end-to-end fixtures `test/jacl/overflow_call_depth.jacl` (toplevel)
  + `test/jacl/cps_spawn_deep_recursion.jacl` (across a spawn
  boundary). No test today asserts `try`/`catch` semantics for
  overflow — add one when the design call is made.

- **Multi-eval embedder state — fully wired.** The struct registry,
  ctx field map, typer struct table, and `GlobalArity` proc
  signature table all persist across `jacl_eval` calls
  (`JaclVM_s::persistent_*` in `src/embed.c`, threaded through
  `compiler_compile`'s persistent-state params and `typer_infer`'s
  `seed_registry`). Cross-eval `struct Pt {...}` → typed proc →
  typed call type-checks and runs. The `GlobalArity` table is now
  arena-backed and grows by doubling — no fixed cap (was 64 until
  this session). Covering tests in `test/test_e2e_embed_basics.c`:
  `test_e2e_struct_name_persists`,
  `test_e2e_ctx_cross_eval`,
  `test_e2e_proc_signature_cross_eval`,
  `test_e2e_proc_signature_growth` (declares 100 typed procs across
  100 evals to exercise the growth path).

---

## 11. Build-flag / scope cuts considered, not committed

From `DESIGN_CRITIQUE.md` §3.6 and §9.5. These are *scope-reduction*
options, not features to add — listed here so the design space stays
visible.

- **`JACL_THREADED=0`** — ~3,000–4,000 LOC out (workers, chase_lev,
  concurrent GC paths, SATB, epoch). Loses CPU parallelism; single-
  threaded coroutines for spawn/await/parallel/race remain. Largest
  single binary-size lever.
- **Drop generational GC (single-threaded tier-2 cut).** ~350–500
  LOC out. Removes working code, not stubs.
- **Drop sum_tree rope tier.** ~5,000 lib + ~150 bridge. Inline +
  interned string tiers remain.
- **Drop typed collections.** ~1,300 LOC. Falls back to dyn
  collections.
- **Drop `$ctx`.** ~600 + cascading simplifications.
- **Drop FFI / embedding.** ~1,340 (embed.c) + ~800 (Ptr/extern/
  trampolines).
- **Consolidate dual struct definitions** (`runtime.c`/`gc.c` vs
  `jacl.h`). Doesn't shed size; removes drift-bug class. Options:
  thread via `jacl.h`, route tests through unity build, or opaque
  pointers + accessors. **Mitigated 2026-05-18**: every field of
  `Runtime` + `WorkerThread` is now offset-checked via the X-macro
  list in `src/struct_drift_fields.h` (driving both `embed.c` getters
  and `test_struct_sizes.c` checks); a missing field in either
  definition fails to compile, an offset mismatch fails the test at
  runtime. The duplication itself remains.
- **`vm.c` split.** 12,200 lines in one TU. Splitting by category
  (keeping the dispatch table central) could halve it. No runtime
  cost change; cognitive-load only.

---

## How to use this file

- **Adding an item:** add it both here (one paragraph) and in the
  owning doc (full discussion). This file is an index, not the
  source of truth.
- **Closing an item:** remove from here when the feature ships or
  the design call is made; update the owning doc per its own rules.
- **Cross-cutting changes** (GC, concurrency, synchronization) belong
  in `AUDIT.md` / `SYNCHRONIZATION.md` / `GC_CONCURRENCY_DESIGN.md`
  rather than here.
