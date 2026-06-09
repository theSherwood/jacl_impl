# Session Handoff — scalar narrowing & typed streams

Last updated: 2026-06-09. Branch `main`, working tree clean. Build: `timeout
240 ./build.sh` → 91/91 binaries, jacl suite **480/480**. TSAN
(`./build.sh --tsan`): only the two **pre-existing known-safe** races
(`chase_lev` test helper `tracked_free`; `OP_GET_GLOBAL` inline-cache patch at
`vm.c:3104-3109`/read at `vm__read_byte`). Nothing this session is race-related.

This doc is the pickup point. **Start with §1 — there is an open design
decision the user explicitly wants rethought before more code lands.**

---

## 1. RESOLVED — `transform` lambda-return inference → see `LAMBDA_TYPING_PLAN.md`

**Status: RESOLVED (2026-06-09). `LAMBDA_TYPING_PLAN.md` is the authoritative
doc for this thread.** Outcome: the macro layering leak was killed
(`typer__lambda_ret_enc` → general `typer__proc_result_enc` reading the proc's
real params; `\` is pure sugar again — `120fc1f`), and the i64 producer-wide
rep flip landed (`c338b99`), so the restore-pass is dropped for i64 mappers and
i64 stream values flow wide through lazy composition. Open follow-up: u64/f64
wide (scoped out — a wide-f64 mapper param mis-coerces in the binary-op
compiler; details in `LAMBDA_TYPING_PLAN.md`). The historical analysis below is
kept for context.

---

### Historical (pre-resolution) framing

What `79c78b5` does: `transform` over a typed stream is typed as a stream of
the lambda's return type (`transform [range 1 4] [\ * $it 10]` → `[Stream
i64]`; `[\ > $it 2]` → `[Stream bool]`; composes). Implemented as
`typer__lambda_ret_enc` in `src/typer.c`, called from **only** the
`HEAD_TRANSFORM` case (`typer.c` ~3745).

**Why it's contentious (agreed it's unprincipled):**
- One-off: only `transform` gets it; `each`/`filter`/`reduce`/user HOFs/direct
  `[$f $x]` calls get nothing.
- The implementation has a **restore-pass smell**: it types the lambda body
  with `it:<elem>` to read the return type, then re-types it with `it:dyn` so
  codegen isn't corrupted. That double pass exists because the lambda's param
  is genuinely `dyn` at runtime — the `\` prelude macro (`src/prelude.jacl:5`,
  `defmacro \`) expands `[\ body]` to `proc {^it} { body }`, an **untyped**
  param. Static inference and runtime representation disagree, and the
  restore papers over it.

**Root cause:** a lambda is *polymorphic in its parameter* — its return type
is a function of the arg type, known only at the application site — and JACL
has **no generics** (deliberately skipped). So a lambda has no standalone
signature; its type only exists relative to a call site.

**The three options on the table (user leaning unresolved):**
1. **Generalize** `typer__lambda_ret_enc` into a reusable "infer lambda result
   given arg types" facility, called from every lambda-application site
   (transform/each/filter/reduce/direct calls). Kills the "only transform"
   objection; keeps the dyn-runtime-param model, so the restore-pass (or
   equivalent) stays.
2. **Typed closures / monomorphized HOF lambdas** — specialize the closure to
   the source element type (`(i64) -> R`, compiled with an i64 param). Removes
   the restore-pass wart entirely (static & runtime agree, no boxing
   round-trip). Architecturally clean; substantial; dovetails with the
   producer-wide rep work (§2, option A) since both want "values flow typed,
   not boxed."
3. **Revert `79c78b5` and defer** — `transform` → `[Stream dyn]` for now; do
   lambda return typing only as part of a proper general closure-signature
   design (the `NOT_IMPLEMENTED §4` "closure literal call signatures" item).

**Claude's recommendation (not yet accepted):** **3 now → 2 later** — revert
the one-off rather than entrench it, and do lambda typing properly via typed
closures when tackling the producer-wide rep, since they share the same goal.
Option **1** is the "consistent-enough today without the deep work" fallback.

**Note:** `take`/`filter` element propagation (`5309fe9`) is NOT contentious —
those genuinely preserve the source element type (no lambda return involved).
Only the `transform` lambda-return inference is in question.

---

## 2. Remaining typed-stream work ("do all 3" — user wants the full set)

Tracked against `NOT_IMPLEMENTED.md §4`. Done vs remaining:

| Piece | Status |
|---|---|
| for-loop binding narrowing — range/lines/`[Stream T]` generators | ✅ `a35d596` |
| `take`/`filter` derived-stream narrowing | ✅ `5309fe9` |
| `transform` → lambda-return-type | ⚠️ `79c78b5` — **see §1, may revert** |
| Typed `collect`/spread → `[Vec T]` | ⬜ remaining |
| Consumer-untyped → type error (can't use `[Stream i64]` as `[Stream dyn]`) | ⬜ remaining |
| Struct-element streams (`[Stream Point]`) | ⬜ remaining |
| **Producer-wide rep flip (option A, #1)** | ⬜ remaining (internal) |

### Key constraint discovered: the producer rep is shared/atomic
Today `range`/`lines`/`yield` all emit **tagged** values, and the suite leans
on it (`collect [range 1 5]` == `[vec 1 2 3 4]`, `collect [transform …]`, etc.
— 29 files use `collect`, 10 `transform`, 8 `filter`, 4 `take`). Flipping the
producer to **wide** (option A) breaks every consumer at once — there is no
"for-loop only" slice of a producer-rep change. That's why the for-loop work
shipped as the **option-1 mechanism**: producer stays tagged; the for-loop
**unboxes the pulled value to wide based on the static type** (`compiler.c`
stream for-loop branch). The runtime `JaclStream.elem_idx` /
`JaclClosure.gen_elem_idx` foundation (commit folded into `a35d596`) is in
place but **not yet read** by the for-loop — it's there for the eventual
producer-wide flip.

### Consumer landscape (for the producer-wide flip)
`vm__pull_stream_one` (`vm.c:1502`) has ~11 callers: filter/transform/take
composition (1519/1578/1688), exec-stdin (2157), each/collect/spread/
OP_STREAM_NEXT (5759, 9477, 9751, 10135, 10271, 10383). A producer-wide flip
must update all: for-loop binds wide (drop the unbox), filter/take pass
through (transparent if `cached_value` GC-skips wide), transform/each box at
the fn boundary (closure params are dyn), `collect`/spread → typed vec or
box-back. GC: `OBJ_STREAM` trace (`gc_collect.c:449`) must **skip
`cached_value`** when `elem_idx` is a wide scalar (mirror the SM state-field
inline bitmap). `vm->yield_value` is **NOT a GC root** (VM-stack scanning was
removed — CPS captures roots as task roots), so wide bits are safe in the
yield→pull handoff; only `cached_value` (on the heap stream object) needs the
skip.

---

## 3. What landed this session (commits, newest first)

- `79c78b5` streams: `transform` → lambda-return-type (**see §1**).
- `5309fe9` streams: `take` propagates element type (was dropping it; `filter`
  already did). `for x in [take [range 1 10] 3]` narrows.
- `a35d596` streams: strict for-loop binding narrowing for typed streams
  (range→i64, lines→str, `[Stream T]` generators→T) via option-1 (unbox at the
  bind). Typer `HEAD_FOR` extended to `TYPE_STREAM`. Folds in the
  `JaclStream.elem_idx` / `JaclClosure.gen_elem_idx` foundation. **Replaced a
  broken intermediate commit** — see §5.
- `bb40193` vec: fix scalar `[Vec i64]` for-loop **segfault** (the loop treated
  every `[Vec T]` as struct-element → `struct__slot_width` on a scalar
  sentinel) + narrow the binding like the arr branch.
- `fdaeaab` arr/sm: **wide-scalar (i64/u64/f64) narrowing across SM + proc
  returns.** SM state-field reads unbox wide scalars (keyed on the var-ref's
  `node->inferred_type`); SM stores (`def`/`mut`/`set`) box wide before
  `SET_STATE_FIELD` (fixed the `mut i64`-across-suspension `nil` bug); arr
  for-loop binding narrows all scalars incl. wide (SM stores boxed, non-SM
  stores wide); dyn-return procs box a wide tail (`compiler__emit_return` +
  `compile_sm_stmts`). Fixed the original wide-for-binding-in-SM segfault.
- `533ceb2` audit: record **§22** `OBJ_FUTURE` trace data race (pre-existing,
  schedule-dependent; `gc_collect.c:415` reads `fut->result`/`waiters` during
  concurrent resolution; not observed as a wrong-result/crash). See
  `AUDIT.md §22`.
- `117f9f3` arr: scalar binding narrowing — `arr-get`/`arr-pop` narrow ALL
  scalars incl. wide (removed the dyn carve-out; `vm__arr_scalar_load` pushes
  raw wide bits like typed-vec); **fixed a pre-existing `[Arr f64]`/`[Arr f32]`
  construct + push/set zero-store bug** (wide float/large-int literals fell
  through `vm__arr_scalar_store`'s tagged-only branches → stored 0; now boxed
  before the byte-packed store). + the `for [Arr T]` loop branch.

Tests added across the session: `arr_typed_i64_narrow`, `arr_typed_f64`,
`arr_for_scalar`, `arr_for_struct`, `arr_for_wide`, `sm_wide_scalar`,
`wide_proc_return`, `vec_for_scalar`, `stream_for_scalar`,
`stream_derived_for`, `stream_transform_typed`.

---

## 4. Other open followups (not stream-specific)

- **arr arrow indexing** `$a->i` / `set $a->i x` — the remaining M6 ergonomic
  piece (new heap-deref lowering; `ARR_DESIGN.md`). Builtins are the surface today.
- **`each`/`transform`/`filter` over arr** (`vm.c` ~5623) — collection HOFs not
  wired for arr.
- **AUDIT §22 `OBJ_FUTURE` race** — documented, not fixed (acquire-load the
  `waiters` head; confirm waiter `next` links / `result` are release-published).

---

## 5. Gotchas / learnings (read before editing)

- **Don't `tail`/`head` build output — grep for `error:`.** I committed a
  **non-compiling** Phase A this session (the elem_idx foundation) because a
  `tail -6` on grep output hid the compile errors; the test suite then ran the
  *stale* binary and looked green. Caught on the next build, fixed, and
  collapsed the broken commit out of history via `git reset --soft`. Always
  `grep -E "error:"` the full build log and confirm `Results:`.
- **Dual-defined structs.** `JaclStream` (jacl.h + gc.c), `JaclClosure`
  (jacl.h + bytecode.c), `StateField`/`JaclType`/`OpCode`/`HeadId` etc. Edit
  BOTH. The broken commit above was exactly a half-updated `JaclClosure`.
- **gc.c sees no jacl.h macros/enums.** It's included very early in the unity
  build; `TYPE_DYN`/`JACL_SCALAR_VEC_BASE` are NOT visible there. The
  `jacl_stream()` dyn-sentinel init uses the literal `0xFF00u` with a comment.
- **`\` lambdas are a prelude macro** (`src/prelude.jacl:5`), expanding to an
  anonymous `proc {^it} { body }` *before* the typer runs. So a lambda reaches
  the typer/compiler as a `HEAD_PROC` node with empty name and a dyn `it`
  param — not a `"\"` command. (This surprised me mid-implementation.)
- **Commit messages via `-F <file>`, not `-m` with backticks.** A `-m` message
  with backticks triggered shell command substitution (ran `set`, dumped env
  vars into the message); had to `--amend`. Write the message with the Write
  tool and `git commit -F`.
- **Scalar representation model** (central to all this work): i32/u32/f32/bool
  are always tagged NaN-boxed `JaclVal`s — narrowing them is representation-
  free. i64/u64/f64 have two forms: **boxed** (tagged heap-cell pointer, the
  dyn form) and **unboxed wide** (raw 64 bits in a slot). `OP_TO_DYN` boxes
  wide→tagged; `OP_TO_I64/U64/F64` (src=DYN) unboxes tagged→wide.
  `compiler__ensure_boxed` bridges wide→dyn at dyn sinks. The whole
  scalar-narrowing effort is about getting wide values to flow with the right
  rep across boundaries (locals, SM state fields, proc returns, stream pulls).

---

## 6. Build / verify

```
timeout 240 ./build.sh                 # 91/91 binaries; jacl suite via jacl_harness
.build/jacl_harness                    # full jacl suite (480/480)
.build/jacl_harness test/jacl/X.jacl   # one test
timeout 480 ./build.sh --tsan          # race check (.build-tsan); expect only the 2 known races
```
Memory note: always wrap test runs in `timeout` (chaos suite can livelock on
rare seeds).
