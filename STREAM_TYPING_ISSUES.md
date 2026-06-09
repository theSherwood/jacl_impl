# Issues log — typed streams, lambdas, contextual literals

Companion to `LAMBDA_TYPING_PLAN.md`. Catalogs everything that went wrong (and
why) during the lambda-typing → producer-wide → contextual-literal effort, so
the next person doesn't relearn it. Commits referenced: `120fc1f` (layering),
`c338b99` (i64 wide flip), `fb56d70` (gen_elem_idx), `aa81bef` (literals + f64/u64).

---

## A. Bugs found and fixed

1. **Macro-knowledge leak in lambda inference.** `typer__lambda_ret_enc`
   hardcoded the param name `"it"` — private knowledge of the `\` prelude
   macro's expansion. A hand-written inline proc wouldn't get the same
   treatment, and the core stopped treating lambdas as ordinary procs.
   Fix: general `typer__proc_result_enc` reading real params via
   `typer__parse_params` (`120fc1f`).

2. **`gen_elem_idx` never reached runtime closures.** `OP_CLOSURE` copied
   `is_generator`/`is_sm_compiled`/etc. from the closure template but NOT
   `gen_elem_idx`, so every runtime generator stream had `elem_idx=0`. Dormant
   ("foundation in place, not read") until `transform`-over-generator became the
   first reader — at which point it was a **latent bug in the committed i64
   work**: `transform`/`filter` over an i64 *generator* produced garbage (a
   tagged value read as wide), while range-sourced transform worked, so the
   suite (no transform-over-generator test) missed it. Fix: copy `gen_elem_idx`
   in `OP_CLOSURE` + regression test `stream_transform_generator.jacl`
   (`fb56d70`).

3. **`collect [range 1 5]` rep changed i32→i64.** The producer-wide flip made
   range yield wide i64; the box-back to a plain vec used `jacl_i64`, so
   collected elements became i64 and `== [vec 1 2 3 4]` (i32 literals) failed.
   Fix: `vm__stream_to_tagged` tags small integers as i32 (the canonical
   small-int dyn rep, matching pre-flip range/yield).

4. **`yield` literals stayed f32/i32.** `yield 1.5` in `[Stream f64]` carried an
   **f32**-tagged value; `yield 100` in `[Stream i64]` an i32. The typer
   *deliberately* didn't push the stream element type as `expected_type` onto a
   yielded literal (typer.c comment), to dodge a feared wide-yield codegen bug
   that was in fact already bridged by `compiler__ensure_boxed`. So f64 streams
   carried f32 values; the wide flip then re-tagged them f64, breaking predicate
   comparisons against f32 literals. Fix: yield-time `expected_type` propagation
   (`aa81bef`).

5. **Mixed-numeric ordering comparison errored at runtime.** `> tagged-f64
   f32-literal` (and i64-vs-i32) hit the generic `OP_GT` strict-tag check and
   errored "expected matching types". Fix: `vm__numeric_order` promotes mixed
   numeric tags (any float → double, else int64) in GT/LT/GE/LE (`aa81bef`).

6. **The existing i64 `filter` only worked by coincidence.** `filter [range]
   [\ > $it 2]` succeeded only because a small wide-i64 element boxed back to
   **i32-for-small**, which happened to match the i32 literal `2`. For a large
   i64 (or any f64) the coincidence breaks. The runtime promotion (A5) made it
   principled.

---

## B. Process pitfalls (where time was lost)

1. **Stale-binary confusion — the biggest time sink (HANDOFF §5).** Incremental
   `./build.sh` (without `rm -rf .build`) repeatedly produced inconsistent test
   results: a clean-committed f64 filter gave `[vec 5]` but a stale build of the
   same state showed empty, leading to a wrong "this is pre-existing" then "this
   is mine" diagnosis loop. **Rule: `rm -rf .build` before any decisive
   build/test whose result you'll reason from.** Don't trust a single
   incremental run when results surprise you.

2. **Committing to a framing before tracing the runtime representation.** Most
   reversals this effort came from asserting a conclusion, then discovering the
   actual i32/i64/f32/f64 *value rep* contradicted it. Examples:
   - "A transform-local typed-closure step removes the restore-pass" — asserted,
     then disproven empirically (a wide-typed body over a tagged param produced
     garbage: `4323455642275676220`). The restore-pass removal is **intrinsic to
     the wide flip**, not a separable precursor.
   - "Phase 2's unbox is throwaway scaffolding" → "not throwaway" → "actually
     throwaway." It is only throwaway *if* the wide flip lands.
   - f64 failure misdiagnosed as a "binary-op coercion bug at compiler.c:~15721"
     — the real cause was `gen_elem_idx=0` + f32 literal tagging. The wrong
     line was even written into a memory note before being corrected.
   **Rule: run the experiment first, conclude second.** When in doubt about a
   value, `print` it / read the raw bits, don't reason about tags abstractly.

3. **Recommendation reversals to the user.** Pure propagate-types → hybrid was a
   *correct* reversal (the pure path for HOF predicates needs wide values pushed
   into wide-compiled predicate bodies — too coupled), but it came after an
   earlier choice, and there were ≥3 reversals total across the effort. Pattern:
   I picked a direction before fully understanding the rep coupling. Surfacing
   findings *with* the supporting experiment (rather than a confident framing)
   would have avoided the churn.

---

## C. JACL surface gotchas (cost real debugging time writing tests)

1. **No inline `proc` inside `[...]`** — the parser rejects it
   (`parser.c:373`). `\` is the *only* inline-anonymous-proc form, and it always
   names its param `it`. (This is also why the macro leak in A1 was never
   surface-observable.)
2. **`[\ $it]` is NOT an identity lambda.** The `\` macro treats the first token
   as a command head, so `[\ $it]` expands to *calling* `$it`. Use `[\ + $it 0]`
   / `[\ * $it 1]` for identity-ish.
3. **Multiple statements on one line fail.** `{ yield 1  yield 2 }` parses the
   second `yield` as an argument ("yield expects 1 argument but got N"). One
   statement per line (or use separators).
4. **Large integer literals overflow.** `5000000000` (> INT32_MAX) fails literal
   parsing — int literals are stored i32 in the AST. Can't easily write a large
   i64 literal directly.
5. **Commit messages: use `git commit -F <file>`, never `-m "...backticks..."`**
   — backticks trigger shell command substitution (HANDOFF §5).

---

## D. Design truths that weren't obvious up front

1. **The producer-wide flip is atomic.** The for-loop's tagged→wide unbox is
   keyed on the binding's *static* type (`[Stream i64]`), identical whether the
   producer is `range` or a generator. A wide value and a tagged value are
   **bit-pattern-indistinguishable**, so you cannot flip one producer (or the
   for-loop) in isolation — all wide-element producers + the consumer contract
   flip together, validated as one diff.
2. **Restore-pass removal lives inside the wide flip**, not before it (B2, A4).
3. **`OP_STREAM_NEXT` is shared** by the for-loop (wants wide) and the userland
   `stream_next` builtin (returns dyn → wants tagged). Resolved by making it a
   **tagged-normalizing terminal** (pull via `vm__pull_stream_dyn`); wide flows
   through *lazy composition* (range→filter→transform→take, incl. the wide
   mapper body), and all terminals (for-loop, collect, each, spread, stream_next)
   re-tag uniformly. This eliminated the for-loop codegen change, SM-field
   boxing, and userland boxing all at once.
4. **A GC `cached_value` skip would have been a bug.** Generators cache the
   *tagged* `yield_value` in `cached_value`, so it stays GC-safe; skipping it
   when `elem_idx` is wide (as an early plan suggested) would skip a real heap
   pointer. The wide value lives only transiently in `*out_value`.
5. **The contextual-literal mechanism already existed** (`expected_type` →
   literal type for def/mut/return/binop). The only real gap was `yield`. "Make
   literals inferable as f32/f64, i32/i64" was mostly a propagation fix, not new
   machinery.
6. **Phase 1b had no targets.** No `reduce`/`fold` exists; `map` is the hashmap
   constructor (not a HOF); `each` returns nil; `filter` is a bool predicate. So
   `transform` is the only HOF whose output element is the lambda's return type —
   it isn't privileged, it's the sole applicable case.

---

## E. Known remaining / not addressed

- Typed `collect`/spread → `[Vec T]`, consumer-untyped → type error, and
  struct-element streams are still open (non-blocking) — see
  `LAMBDA_TYPING_PLAN.md` and `NOT_IMPLEMENTED.md §4`.
- Dynamic **arithmetic** ops were not given mixed-numeric promotion (only the
  four ordering comparisons). Transform mappers use typed wide ops, so this
  hasn't been needed; revisit if a dyn-context mixed-numeric `+`/`*`/etc. surfaces.
- `OP_EQ` was already cross-type permissive (mismatched tags → not-equal), so it
  was intentionally left unchanged by the promotion work.
