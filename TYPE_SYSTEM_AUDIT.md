# JACL Static ↔ Dynamic Type System Audit

## 1. The two type worlds

**Runtime (dynamic) — NaN-boxed `JaclVal`** (`src/jacl.h:52-77`, `src/value.c:42-67`).
A 64-bit tagged value, ~5 bits of tag + 3 flag bits (tainted / secret / error). Tag families:
- Inline scalars: `nil`, `bool`, `i32`, `u32`, `f32`
- Boxed numerics: `i64`, `u64`, `f64`
- Strings: `inline_string` (≤7 bytes), `string`, `rope_string`
- Collections: `vector`, `map`, `typed_vector`, `typed_map`
- Functions: `closure`, `native_fn`, `state_machine`
- References: `box`, `cell`, `mutable_ref`
- Other: `struct`, `atom`, `bignum`, `future`, `stream`, `syntax`

**Compile time (static) — `JaclType` enum** (`src/compiler.c:82-100`), 17 kinds:
`TYPE_DYN, TYPE_BOOL, TYPE_NIL, TYPE_I32/I64/U32/U64/F32/F64, TYPE_STR, TYPE_VEC, TYPE_MAP, TYPE_CLOSURE, TYPE_STRUCT, TYPE_STREAM, TYPE_TYPED_VEC, TYPE_TYPED_MAP`.

Properties:
- **Nominal** for structs (matched by `type_idx` in a registry, `compiler.c:247-253`).
- **Erased** for typed collections — `TYPE_TYPED_VEC` / `TYPE_TYPED_MAP` carry no element type at the static level; element types only exist on the heap object at runtime.
- **No** generics, unions, optionals, or aliases. `TYPE_DYN` is the universal escape hatch.

## 2. The check phase

There is no separate type-check pass. Checking is interleaved with codegen in `compiler__compile_node` and friends. The plumbing is `c->expected_type` (top-down hint) and `c->last_expr_type` (bottom-up result), with `compiler__error` accumulating errors (`compiler.c:3034-3051`).

What's enforced statically:
- Inline struct field assignment type (`compiler.c:10134-10148`)
- Function arity for known procs (`compiler.c:10580-10607`)
- Return type vs declared (`compiler.c:3941-3943`)
- Struct-to-struct equality requires same `type_idx` (`compiler.c:4108-4115`)

What's deferred (or just dropped):
- Parameter types on `proc` are advisory — no runtime guard emitted.
- Indirect calls through a value bypass arity/type checks entirely.
- Element types of typed vec/map are not checked on access.
- Loop variables in `for` over typed collections come out as `TYPE_DYN`.

## 3. The boundary opcodes

The handoff between worlds is concrete and lives in a small set of ops:
- `OP_BOX` / `OP_BOX_STRUCT` (`vm.c:4565-4592`) — wrap a typed scalar / inline struct bytes into a `JaclVal` with `type_idx` so the GC can trace it.
- `OP_STRUCT_NEW_INLINE` (`vm.c:6302-6320`) — build a struct as raw bytes across N stack slots, no heap, no tag; the *bytecode* carries the `type_idx`.
- `OP_RESET_INLINE` (`vm.c:5277-5293`) — re-init those bytes in place.
- `OP_GET_STATE_FIELD_WIDE` / `OP_SET_STATE_FIELD_WIDE` (`vm.c:7861-7898`) — the state-machine equivalent: copy N slots between the SM heap object and the stack, preserving width.

For inline-typed field reads, the compiler emits the field type as a one-byte hint after the offset (`compiler.c:10152`) so the VM knows how to box back into a `JaclVal` if needed.

## 4. What survives erasure at runtime

Defensive vs. load-bearing tag checks:
- **Load-bearing**: print/format dispatch (`vm.c:779-845`), arithmetic fast paths that branch on operand tags (`vm.c:672-684`), GC tracing.
- **Trusted**: inline struct field reads/writes — the VM reads bytes at the compiled offset with no tag check. If the compiler emitted the wrong width or offset, you get silent corruption, not a trap.

There are very few "this shouldn't happen" asserts in `vm.c` for type tags. The contract is: the compiler proved it.

## 5. The sharp edges (ranked by how much they'd worry me)

**(a) `TYPE_DYN` is a one-way trapdoor into typed slots — `compiler.c:10139`**
```c
if (field_type != TYPE_DYN && val_type != TYPE_DYN && val_type != field_type) { error }
```
Reads as: if either side is dynamic, accept. That's fine when narrowing dynamic→typed *with a runtime check*, but no runtime check is emitted. Any expression that statically infers as `TYPE_DYN` (call to an untyped proc, lookup in a plain map, indirect call, etc.) can be assigned into an `i64` inline field, and the VM will reinterpret the 64 bits at that offset. With inline structs that's actually unsafe — a `JaclVal` pointer or NaN-boxed payload becomes raw integer bytes. **This is the load-bearing soundness gap.**

**(b) Typed collections lose their element type at the static layer — `compiler.c:98-99`**
`TYPE_TYPED_VEC` is a single kind. The element type lives only in the heap header. So:
- A typed `vec[i64]` flowing into a typed field can't be checked element-by-element.
- Iteration variables fall back to `TYPE_DYN` (item 5d).
- A function declared to return `vec[i64]` is indistinguishable, statically, from `vec[str]`.

**(c) Generator state fields infer types, they don't preserve declarations — `compiler.c:1747-1787`**
`sm__walk_locals` figures out struct widths from explicit `def Type x ...` annotations or constructor names. If a state-captured local was assigned from a dynamic expression, it lands as `struct_type_idx=0` / generic JaclVal slots. Combined with (a), a typed field on a captured struct can be filled with garbage that survives across a `yield`, where the next resume will treat the bytes as the original type.

**(d) `for` loop variable is always dyn**
Even iterating `vec[i64]`, the loop var is `TYPE_DYN`, which immediately triggers (a) when written into a typed slot.

**(e) Struct identity is forgotten through dynamic boxes**
Once an inline struct is boxed and stored in a plain `vec`/`map` and pulled back out, the static type is gone. The runtime box still carries `type_idx`, so a runtime narrowing would be possible — but I didn't find a `[box? Type $val]`-style check that the compiler emits before reusing the bytes as a typed struct.

**(f) Param types on `proc` aren't enforced**
Parsed and stored, but no `OP_TYPE_CHECK` (or similar) is emitted at the prologue. Callers that go through (a) propagate untyped values straight into the function body, where the body's compiled offsets/widths trust the declared type.

## 6. Tests

Direct static-vs-dynamic coverage is thin. Type-named files: `test_typed_vec.c`, `test_typed_map.c`, `test_stream_type.c`, `test_stream_type_enforce.c`. Plus the `test_struct_*` family. What's notably absent:

- A test that proves a `TYPE_DYN`-returning proc *cannot* be assigned into an `i64` inline field (it currently can).
- A test that puts a struct in a plain `vec`, retrieves it, and assigns it back into a typed slot.
- A `for` over `typed_vec[i64]` writing the iter var into an `i64` field.
- Generator suspension across a typed field that received a dyn value before the yield.

All four would likely surface (a)/(b)/(c)/(d) above as observable bugs, not just lints.

## 7. Overall design assessment

The two-tier system (NaN-boxed dynamic + nominal static for structs/scalars) is a reasonable bet for a Lisp-shaped language that wants to be fast without forcing whole-program annotation. The inline-struct + wide state-field path is genuinely good engineering — you're not paying for a heap box on every typed value, and the GC still traces correctly via `type_idx`. **That's the part of the design earning its keep.**

What's *not* paying rent:
- **17 static `JaclType` kinds, no generics.** Just enough type machinery to be load-bearing in codegen but not enough to express what users actually write (`vec[i64]` collapses to one kind). That's the worst spot on the curve — complexity without expressivity.
- **Interleaved checking.** `expected_type` / `last_expr_type` threaded through `compiler__compile_node` is part of why `compiler.c` is **13k lines** — type inference, codegen, and struct layout are all braided together. A separate type pass would probably *shrink* the compiler, not grow it.
- **`TYPE_DYN` as silent passthrough.** This isn't really a design — it's the absence of one. Every place that defers to it is a place the design hasn't decided yet.

## 8. On the 41k LOC

It's a lot, but the type system isn't the main culprit. Rough proportions:
- `compiler.c` 13k + `vm.c` 11k = **59% of the codebase in two files.**
- `parser.c` 3k, `gc.c` + `gc_collect.c` 2.4k, `lexer.c` 1.7k, `runtime.c` 1.5k.

The compiler being 13k is a smell — most language implementations of this scope land at 4–6k for the front end. Some is the type/codegen interleaving above, but a bigger chunk is likely opcode-specialization sprawl (`STRUCT_NEW_INLINE`, `RESET_INLINE`, `GET_STATE_FIELD_WIDE`, plus per-type arithmetic) where every new typed op needs a compile site, an emit site, a VM handler, and inference glue. Data-oriented design done right at the VM layer leaking complexity upward into the compiler.

`vm.c` at 11k is the natural cost of a fat opcode set + NaN boxing + GC barriers + state machines. Less alarming.

## 9. Recommendations (rough order of payoff vs. disruption)

1. **Fix `TYPE_DYN` passthrough.** Small, removes a soundness footgun, possibly forces 1–2 honest design decisions.
2. **Parameterize `TYPE_TYPED_VEC` / `TYPED_MAP` with element type.** Medium, unlocks real checking and probably *deletes* some runtime checks.
3. **Extract a type-inference pass** that runs before/with codegen and stores results on AST nodes. Large, but likely net-negative LOC in `compiler.c`.
4. Only after the above: consider whether the typed-op explosion can be table-driven instead of hand-written per type.

Don't touch the inline-struct / state-machine machinery — that's the part that justifies having a static type system at all.

## TL;DR

The architecture is sound where the compiler can fully type a program: nominal struct inline layouts plus typed-aware boxing/state ops give real performance with real safety. The weak seam is uniform: anywhere a `TYPE_DYN` value can reach a typed slot, the static check abstains and no runtime check fills in. The single-line bypass at `compiler.c:10139` is the place to start — either emit a runtime narrow when one side is `TYPE_DYN`, or refuse the assignment and require an explicit cast/check at the source. Closing that one would convert (d), (e), (f) from "silently wrong" into "rejected at compile or trapped at runtime."

---

# Plan: 1 → 3 → 2

Sequenced for compounding wins. (1) is cheap insurance and produces tests both later phases must keep passing. (3) untangles the codegen/inference braid before (2) widens the type representation — doing (2) first would mean editing the same braided code (3) plans to delete.

## Phase 1 — Close the `TYPE_DYN` passthrough (~1–2 days)

**Goal:** make `compiler.c:10139` no longer a soundness gap.

**Approach:** runtime narrow. Cheapest and least source-breaking. Adds one opcode; no new syntax.

Steps:
1. Add `OP_CHECK_TYPE` to `src/bytecode.c` (operand: 1-byte `JaclType`). Trap on tag mismatch.
2. Implement handler in `src/vm.c`. Behavior on mismatch: structured runtime error with line/col, not silent corruption.
3. At `compiler.c:10139`, when `field_type != TYPE_DYN && val_type == TYPE_DYN`, emit `OP_CHECK_TYPE field_type` before the `SET_*_INLINE`. The reverse direction (`field_type == TYPE_DYN && val_type != TYPE_DYN`) stays free — widening to dyn is always safe.
4. Audit every `c->last_expr_type = TYPE_DYN` site in `compiler.c` to confirm no false-positive narrow gets inserted. Look for literals, already-checked values, GC barriers.
5. Add tests for the four cases in §6 (dyn proc → typed field; vec retrieve → typed field; for loop var → typed field; suspended dyn → typed resume).

**Checkpoint:** all four §6 cases now fail-fast at runtime instead of corrupting memory. Existing test suite still passes.

**Risk:** if many programs rely on the passthrough, runtime errors will surface them. That's the point — but expect a round of fixing demos/prelude.

## Phase 2 — Extract a type-inference pass (~3–6 weeks)

**Goal:** AST nodes carry types. `compiler.c` shrinks.

Steps:
1. Add a `JaclType type` field to AST nodes in `src/ast.c`. Default `TYPE_DYN`.
2. New file `src/typer.c` (or section in `compiler.c`): `typer__infer(Ast*)` walks the AST, populates `node->type`, accumulates errors via the same `compiler__error` pathway.
3. Mirror existing inference logic from `compiler__compile_node` cases — literals, struct construction, field access, calls (with arity), arithmetic, returns. This is mechanical translation, not new design.
4. Handle forward references: two-pass for top-level decls (collect signatures, then bodies). Recursive procs work because the signature is known by body time.
5. Generator state field inference (`sm__walk_locals` at `compiler.c:1747-1787`) reads AST types instead of reconstructing — typed-state-across-suspension becomes structurally clean.
6. Codegen reads `node->type` instead of `c->last_expr_type`. Delete the `expected_type` / `last_expr_type` plumbing from `compiler.c` once codegen no longer references it.

**Checkpoint:** `compiler.c` line count drops (target: -2k to -4k). All Phase 1 tests still pass. Type errors now reported before any bytecode is emitted.

**Risk:** codegen sites that "needed" the inline inference (e.g. struct layout decisions, inline-vs-heap, box-vs-raw) — these now have to read from the AST. If any decision depends on info inference doesn't currently track, add it to inference rather than threading state. Budget a week of "I thought this was done."

## Phase 3 — Parameterize typed collections (~1 week, post-(3) → ~2 weeks pre-(3))

**Goal:** `vec[i64]` and `vec[str]` are statically distinguishable. Element types flow through assignments, params, returns, and `for` loops.

Steps:
1. Replace flat `TYPE_TYPED_VEC` / `TYPE_TYPED_MAP` enum values with structured types. With Phase 2 done, this means extending the AST `Type` representation (small struct or tagged union with element type). Without Phase 2, this means widening `last_expr_type` everywhere — much bigger patch.
2. Update `src/parser.c` to parse `vec[T]` / `map[K V]` annotations into structured type nodes, not flat tokens. Check current state — if already structured, free; if flat, ~200 lines.
3. Inference pass populates element types on collection literals, indexing, iteration.
4. `for` loop binds iterator variable to the element type (fixes audit §5d). Iter-var-into-typed-field now type-checks at compile time.
5. Subtyping decision: does `vec[i64]` flow into `vec` (untyped)? Suggest yes, with widening to dyn — same rule as scalar `i64 → dyn`. Reverse needs a runtime narrow (reuse `OP_CHECK_TYPE` infrastructure from Phase 1, extended to element-wise).
6. Decide whether typed-vec assignment is invariant or covariant. Recommend invariant for mutable, covariant for read-only views. If JACL doesn't distinguish, pick invariant — simpler, less surprising.

**Checkpoint:** typed collections check element types at compile time. Several runtime checks in `runtime.c` / `vm.c` for typed vec/map access become dead and can be deleted.

**Risk:** subtyping is the design call to get right; the rest is mechanical.

---

## Cross-phase notes

- **Tests are the contract.** Every phase must keep prior phase's tests green. Phase 1 tests in particular are non-negotiable — they encode the soundness fix.
- **Don't touch inline-struct / state-machine machinery.** That's the load-bearing part of the design that makes the static system worth having. If a refactor seems to require changing it, stop and re-plan.
- **`compiler.c` line count is a useful proxy.** If Phase 2 doesn't shrink it materially, the inference pass isn't replacing enough of the inline logic — likely means codegen is still doing inference work.

---

## Phase 1 — completion notes

Done. 92/92 tests pass; 4 new soundness tests added in `test_m11.c`.

**Six loose sites tightened**, not one (audit undercounted): `mut` init, `def` init, ctx fork override, inline struct field set, ctx struct field set, heap struct field set. All now reject `dyn → typed` and suggest `[to TYPE $val]`.

**Bonus fix:** `compile_binary` now preserves operand type for tagged scalar arithmetic (`i32+i32 → i32`, etc.) instead of collapsing to `TYPE_DYN`. The runtime value was always correctly tagged; only the static tracker had lost it. This was needed to keep legitimate `[+ $i32_field 1]` from being rejected by the new strict policy.

### New gaps discovered

- **Upvalue flow typing.** `[box? Type $b]` in an outer scope does not propagate the narrowing into a nested `proc inner {}` that captures `$b`. Two existing closure-capture tests had to be rewritten to pass typed values via parameters instead. Real limitation in the current flow-typer — cheap to fix in Phase 3 once types live on AST nodes.
- **Top-level struct values.** `def Point p [Point ...]` at top level is rejected (pre-existing constraint, unrelated to Phase 1) — must be wrapped in a proc. Worth revisiting whether this is intentional.

---

## Phase 3a — foundation laid

Done. 92/92 tests still pass. Commit `b0f9580`.

**What landed:**
- `AstNode` gains `inferred_type` (uint8_t / JaclType) and `inferred_struct_idx` (uint32_t, UINT32_MAX = none) fields.
- New `src/typer.c` with `typer_infer(AstNode**, count)` walking the AST. Currently handles literals (`I32`/`F32`/`STR`/`NIL`), blocks (last-expression type), and structural recursion. Everything else defaults to `TYPE_DYN`.
- `JaclType` enum + helpers (`is_type_keyword`, `type_from_keyword`, `type_name`, `is_numeric_type`, `is_unboxed_type`, `is_typed_collection`) relocated from `compiler.c` to `ast.c` so the typer (earlier in unity build) can use them. Public copy in `jacl.h` left alone.
- Typer runs after macro expansion in `compiler_compile`. Dual-track mode: codegen does not yet read `node->inferred_type`; the existing `expected_type`/`last_expr_type` plumbing remains the source of truth.

### What this unlocks

The AST now carries type annotations; further subphases mostly fill them in correctly and switch consumers.

### What's next (Phase 3b — scope-aware inference)

The biggest remaining chunk before consumers can switch. Needs:
- **Scope tracker** in the typer: locals (with type), upvalues, globals (proc return types, mut types), struct registry. ~300–400 LOC mirroring `compiler__resolve_local` etc.
- **Var-ref inference**: read the binding's declared type.
- **Command dispatch**: at minimum `def`/`mut`/`set` (to push expected types into RHS), `proc`/proc-call (to track return types), arithmetic builtins (to mirror `compile_binary`), struct constructor + field access.
- **Forward references**: two-pass for top-level decls (collect signatures, then bodies). Recursive procs need this.

Recommended sequencing:
1. Skeleton scope tracker + var-ref + simple `def` (no command dispatch yet).
2. Add `proc` registration + proc-call return type.
3. Add arithmetic + comparison rules (mirror `compile_binary`).
4. Add struct construction + field access.
5. Add a dev-build assertion harness that compares typer output to `last_expr_type` at compile sites — catches inference bugs before consumers depend on them.

Each step is its own commit. Total scope: ~2–4 weeks of focused work; should land `compiler.c` 1–2k lines lighter once consumers switch (3c).

---

## Phase 3b — scope-aware inference (in progress)

92/92 tests still pass throughout. Six commits land 3b's first arc:

1. `71e9e6c` — scope tracker (push/pop/add/resolve), var-ref inference, def/mut/set/proc command handlers, dual-track check (`-DJACL_TYPER_DUAL_TRACK`).
2. `741ffe6` — literal expected_type propagation (def/mut push declared_type to RHS literals). Dual-track on m11: 123 → 35.
3. `8a5cf72` — proc signature pre-pass + call-arg expected_type propagation. m11: 35 → 26, harness: 10 → 6.
4. `c62b48d` — binary op LHS-to-RHS expected_type propagation (mirrors `compile_binary`). m11: 26 → 9.
5. (this commit) — nested proc registration so calls in same scope as a nested proc def find its signature. m11: 9 → 7.

### Status of recommended sequencing

- ✅ (1) Scope tracker, var-ref, simple def
- ✅ (2) Proc registration + return type at calls
- ✅ (3) Arithmetic + comparison rules
- ⏳ (4) Struct construction + field access — not yet; would need struct registry threading in the typer.
- ✅ (5) Dual-track assertion harness (gated by `-DJACL_TYPER_DUAL_TRACK`).

### Remaining mismatches

m11: 7. harness: 5. All same shape: `AST_LIT_INT` → `i32` vs `i64`/`u32`/`u64`/`f64`. Hard-to-pinpoint sites (mostly in interpolated strings, struct constructors, or context-narrowing forms not yet covered). Diminishing-returns territory for this phase.

### What this unlocks

The typer is now correct enough to drive codegen for the cases it claims (literals, var-refs, simple binary ops, proc calls, def/mut/set). Phase 3c (switching consumers in `compiler.c` to read `node->inferred_type`) is the next logical step — and it's incremental: pick one site, switch it, run tests, repeat.

### Phase 3c planning

The cleanest first switch is the `compile_binary` LHS-to-RHS narrowing at `compiler.c:4097-4100`. That code already does what the typer now mirrors; the compiler can read `lhs->inferred_type` instead of computing it via re-walking. After a few such conversions, the dual-track harness flips polarity: instead of checking the typer against the compiler, it'll check the compiler's residual computations against the typer.

---

## Phase 3b — completed

92/92 tests pass throughout. Twelve commits drove the typer to provable equivalence with the compiler on every pattern in the test suite:

| Commit | What |
|---|---|
| `71e9e6c` | Scope tracker, var-ref, def/mut/set/proc handlers, dual-track harness |
| `741ffe6` | Literal expected_type propagation. m11: 123→35 mismatches |
| `8a5cf72` | Proc signature pre-pass + call-arg propagation. m11: 35→26, harness: 10→6 |
| `c62b48d` | Binary-op LHS-to-RHS narrowing. m11: 26→9 |
| `6091abd` | Nested-proc registration. m11: 9→7 |
| `50064bd` | `::` sugar, command boundary reset, proc-body return_type push. m11: 7→0 |
| `bde0179` | Struct registry + constructor field narrowing. harness: 4→0 mismatches |
| `3d6628d` | Dual-track distinguishes mismatch (typer bug) vs gap (typer conservative) |
| `71b1ffd` | `=`/`:` sugar forms, completing all binding shapes |

Final dual-track: **m11 0 mismatches / 0 gaps. harness 0 mismatches / 201 gaps.** All harness gaps are typer omissions (LIT_INT inside CTX_DECL, BLOCK result-type tracking through some struct cases, ERROR-node sites). Long tail; doesn't block Phase 3c.

## Phase 3c — first consumer switches

Two compile sites now read types from the typer's AST annotations instead of `c->last_expr_type`:

| Commit | Site | What changed |
|---|---|---|
| `2e0de1e` | `compile_binary` | LHS and RHS types come from `args[i]->inferred_type` (with `c->last_expr_type` fallback for typer gaps) |
| `71b1ffd` | def/mut value RHS type read | Same pattern at the typed-binding sites in `compile_command` |

Each switch is behaviorally identical; the dual-track invariants are unchanged. The pattern works.

### Next switches (mechanical, by frequency)

1. The remaining 4 set! sites (compiler.c around 6366, 6420, 6478, 6510) read `rhs_type` after `compile_node`. Same pattern; same fallback.
2. Struct field constructor and SET sites (Phase 1's six sites). `val_type` reads can move to AST.
3. Return-type checking at proc body completion. Reads body's last expression's type.
4. Eventually: every `JaclType x = c->last_expr_type;` after a `compile_node` call becomes `JaclType x = node->inferred_type;` (with fallback while gaps exist).

### Long-tail typer expansions to close the 201 harness gaps

- AST_CTX_DECL recursion (literal args inside ctx field defaults).
- AST_BLOCK result type when last command is a struct constructor / typed call (some path leaves it DYN).
- AST_USE / AST_DEFSTRUCT (typically don't matter for codegen-time decisions).

Each is a small focused commit. Closing them shrinks the fallback path until consumers can drop `c->last_expr_type` reads entirely (at which point the writes can be deleted, and `compiler.c` shrinks materially).

---

## Phase 3c — substantive read-switch complete

| Commit | What |
|---|---|
| `be65d9a` | Four set! sites (local mutable, upvalue, two global mutable variants) |
| `d290639` | Struct field SET (inline + heap + ctx) and ctx fork override |
| `09b9a3a` | Unary minus, struct constructor field args, proc call args |
| `e6cadf3` | set! source struct path, for-loop col_type, to-cast src_type, take col_type, dot-access struct_type |
| `beb9fc0` | if-expression then/else branch types via block AST nodes |

**The only remaining read of `c->last_expr_type` at codegen time is the dual-track invariant check itself (compiler.c:12382).** Every substantive type-decision read now drives off the typer's AST annotations.

Dual-track invariants preserved across every switch: m11 0/0, harness 0 mismatches / 201 gaps (unchanged from Phase 3b finish).

### What this means

The typer pass is now the *primary* type oracle for codegen. Compiler-side `last_expr_type` writes are still needed because of the 201 gap fallbacks, but those writes are no longer load-bearing for the typer-covered paths.

### Path to compiler.c shrinking (Phase 3d?)

The compiler.c shrink the audit predicted requires deleting the inference logic that *writes* to `c->last_expr_type`. To do that safely:

1. **Close the remaining 201 typer gaps.** Each gap means at least one consumer site falls back to the compiler's tracker. Close all → fallbacks become dead code.
2. **Remove the `if (x == TYPE_DYN) x = c->last_expr_type` fallbacks** from the ~15 switched consumer sites. After this, no one reads `c->last_expr_type`.
3. **Delete the writes.** The ~50+ `c->last_expr_type = X` lines plus the propagation through return type checks, struct equality, etc. can be removed.
4. **Delete the `last_expr_type` / `expected_type` / `last_struct_idx` fields** on `Compiler`. The corresponding type-tracking glue (~500 LOC by my earlier estimate) becomes deletable.

The 201 gaps are concentrated: most are a few specific patterns (CTX_DECL recursion, certain block result types). A few focused commits should close them. After that, steps 2–4 are mechanical.

---

## Phase 3d gap closure — substantive progress

Seven commits drove the gap count from 201 → 30 (85% reduction):

| Commit | What | Gap delta |
|---|---|---|
| `e859175` | Typer recurses into AST_CTX_DECL default expressions, propagating declared type as expected_type | 201 → 137 |
| `c4275d0` | Register `$ctx` as builtin TYPE_STRUCT binding at typer init | 137 → 63 |
| `ca5cb09` | syntax_to_ast sets inferred_type on literal clones; expand__compile_staged_body runs typer_infer on macro bodies before compile; AST_DEFMACRO recursion | 137 → 51 |
| `9accbdc` | compiler__compile_module invokes typer_infer on imported AST | 51 → 30 |
| `4533b28` | Typer handles `def DESTRUCTURE_VEC` and `def DESTRUCTURE_NAMED` forms (foundation; type-aware destructured-binding lookup is a future extension) | 30 (no change; new bindings still TYPE_DYN) |

92/92 tests pass throughout. 0 mismatches throughout.

### Remaining 30 gaps

Breakdown:
- 23 AST_LIT_INT (typer DYN → compiler i32) — likely from destructured struct fields where the typer doesn't yet propagate field types into bindings.
- 4 AST_VAR_REF — same root cause: destructure or special-form bindings without typed lookup.
- 2 AST_LIT_STRING — minor.
- 1 AST_BLOCK — block-result inference edge case.

All are harmless — consumers that read from `inferred_type` fall back to `c->last_expr_type` for these cases. Closing them needs:
1. Storing field names alongside field types in TyperStruct (so destructure_named can map `{x, y}` → field types via the source struct's index).
2. A few targeted typer expansions for stream/special-form patterns.

### Steps 2–4 (delete the fallback + writes + fields)

Even with 30 gaps, much of the cleanup can proceed:

- The fallback `if (x == TYPE_DYN) x = c->last_expr_type;` lines remain load-bearing for the 30 gap cases. Removing them requires gap=0.
- Many `c->last_expr_type = X` writes are now read only by the dual-track check and the fallback. Once gaps close, these become unreachable.
- The `expected_type` field on `Compiler` is still load-bearing because consumer-side compile-node still sets it before calling compile_node (e.g., for literal context-narrowing inside an old code path). Removing it requires every literal compile site to read `expected_type` from the typer's expectation, not the compiler's.

The path is clear; it's mechanical work that compounds.
