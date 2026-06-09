# Plan — clean lambda result-type inference

Last updated: 2026-06-09. Supersedes HANDOFF.md §1 (the `transform`
lambda-return probe). Tracked as tasks #1–#4.

## The problem

A `\` lambda is a **prelude macro** (`src/prelude.jacl:5`) that expands to an
anonymous `proc {^it} { body }` with an **untyped (dyn) param**. It therefore
has no standalone signature — its return type is a function of the argument
type, known only at the call site, and JACL has no generics.

`79c78b5` made `transform` over a typed stream infer the lambda's return type
via `typer__lambda_ret_enc` (typer.c:2435). Two problems:

1. **Layering leak (the real objection).** The facility hardcodes the param
   name `"it"` (typer.c:2458) — private knowledge of the `\` macro's
   expansion. The core stopped treating a lambda as an ordinary proc and
   started pattern-matching its desugared shape. A hand-written inline
   `proc {^x} { * $x 10 }` passed to `transform` gets nothing. So "lambdas are
   just a macro the core never sees" is broken.
2. **Double-walk wart.** It types the body with `it:<elem>` to read the return
   type, then re-types with `it:dyn` so codegen (which expects a boxed dyn
   param) isn't corrupted. The restore pass is the smell; it compounds on
   nested pipelines.

## The decision

The clean end state is **call-site-directed typed-closure compilation**: push
the receiver's element type into the proc body and compile that body **once**
against it, using the proc's **real** params. One mechanism fixes all axes:

- **Layering** — nothing lambda-specific; the rule is "compile a proc body
  with its params bound to the known arg types." Lambdas benefit because they
  *are* procs. `\` goes back to being pure sugar. No `"it"`.
- **Startup** (the typer is a compile-time pass run once per program run —
  `jacl_run`: lex→parse→`compiler_compile`(typer inside)→`vm_exec`, no
  bytecode cache) — type the body **once**, same passes as the dyn baseline.
  No double-walk, no nested-pipeline compounding.
- **Runtime** — the body operates on the real element type, so the per-element
  box/unbox of the param disappears on hot numeric streams.

Rejected alternatives: generics/function types (deliberately out of scope, and
they don't escape the box-vs-monomorphize runtime choice anyway); annotations
(kills piping ergonomics); revert-and-stay-dyn (loses the ergonomics we want).

## Phased path (no ergonomic-regression window)

- **Phase 1 (#1)** — Replace `typer__lambda_ret_enc` with a general
  `typer__proc_result_enc(proc_node, arg_encs[], argc)` using real params
  (`typer__parse_params`). **Keeps** the restore pass for now (codegen
  unchanged) — interim, documented. Fixes the layering leak immediately.
- **Phase 1b (#2)** — ✅ RESOLVED, not applicable. Current JACL has **no**
  reduce/fold; `map` is the hashmap constructor (not a HOF); `each` → nil;
  `filter` → bool predicate (element unchanged); direct `[$f $x]` is a var-ref
  to a named closure (needs deferred closure-signature work); typed-vec/map
  `transform` produces a **plain boxed** vec/map at runtime, so the typer can't
  claim `[Vec R]` without rep changes. ⇒ `transform` over a typed **stream**
  is the only safe lambda-return consumer today (done in Phase 1). The general
  facility is ready to wire into those sites if/when they gain runtime support.
  No "only transform" arbitrariness remains — transform is the sole applicable
  case, not a privileged one.
- **Phase 2 (#3)** — Type-once + unbox-at-param: drive body compilation from
  the call site with the param typed, **drop the restore pass**, insert one
  unbox at the param prologue, memoize the node result. Removes the wart with
  the producer still tagged.
- **Phase 3 (#4)** — Producer-wide rep flip (HANDOFF §2 option A): producers
  emit wide, all `vm__pull_stream_one` consumers updated, GC skips wide
  `cached_value`. Drops the Phase 2 unbox. Static rep, runtime rep, and value
  flow all agree. Shared with the rest of the remaining typed-stream work.

## Finding (Phase 1): `\` is the only inline-anonymous-proc surface form

Inside `[...]` the parser **rejects** `proc` (parser.c:373-377 — "proc must use
command syntax"); anonymous `proc {params} {body}` parses only at statement
level (parser.c:1648). The `\` macro sidesteps this by emitting `make-syntax`
nodes directly, and it always names the param `it`. So for `transform` with an
inline lambda, the old hardcoded `"it"` and the new real-params path are
**behaviorally identical** — the generalization is correct-by-construction and
the foundation for 1b/Phase 2, but it is not separately surface-testable here.
The de-leak is exercised for real in **Phase 1b** (reduce / direct calls bind
multiple real params: `acc`, element). Phase 1 is verified by
`stream_transform_typed` staying green (no regression on the `\` path).

## Phase 1 status: ✅ done

`typer__lambda_ret_enc` → `typer__proc_result_enc(proc, arg_encs[], argc)`
(typer.c). Reads real params via `typer__parse_params`; binds untyped (dyn)
params to call-site arg encodings; interim restore-pass kept and documented.
`transform` rewired. Suite 480/480.

## Deliberately out of scope

Named lambdas **reused across different element types** stay `dyn` (would need
per-type monomorphization — more startup + bytecode; rare). Inline `[\ … ]`
has one call site, so its single typed compilation is free and optimal.

## Verify

```
timeout 240 ./build.sh            # 91/91 binaries; jacl suite 480/480
.build/jacl_harness               # full suite
grep -E "error:" the full build log — never tail/head it (HANDOFF §5)
```
