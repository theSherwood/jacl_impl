# Syntax Redesign — June 2026

Working notes from a design review of `test/jacl/tour.jacl`. Settled
decisions are listed first; the SM-compiler bugs found during the
review are at the end.

The driving goal is JACL as a more powerful **shell and glue
language** — bare words stay, but redundant or aesthetically-motivated
forms get cut.

---

## 1. Principle: print/parse symmetry

**Every value prints as a form the reader accepts.** Once this holds,
`[read [print v]] == v` for everything except first-class procs.

Consequences:

- Maps already print as `[map a 1 b 2]` — keeps.
- Vectors print as `[vec 1 2 3]` — keeps.
- Errors print as `[error "msg"]`.
- Structs become `[Pt x 30 y 15]` (see §2).
- Procs/lambdas cannot round-trip; their printed form is deliberately
  non-readable (e.g. `#<proc add>`) so it can never be mistaken for a
  reader form.

---

## 2. Struct redesign

Current: `[Pt 30 15]` constructor (positional), prints as
`Pt{x: 30, y: 15}`. Asymmetric.

New:

```
[Pt x 30 y 15]      # init and printed form
```

- **No positional shorthand.** Single form, mirrors `[map …]`.
- **Partial init permitted**: `[Pt x 30]` leaves `y` at its default.
- **Defaults**: `0` for typed fields, `Nil` for `dyn` fields.

---

## 3. Drop `()` infix mode

`()` goes away entirely. The two surviving modes now split along a
real semantic axis, not three flavors of the same thing:

- **`[]` — prefix expression mode.** Pure value computation.
- **`{}` — command mode.** Shell-pipeline territory: bare commands,
  symbolic connectors (`|` already supported here; `&&`/`||`/redirections
  in scope as command-mode tokens), statement sequencing.

### What `()` was doing, and the replacement

| Use | Replacement |
|-----|-------------|
| `(1 + 2 + 4)` infix arithmetic | `[+ 1 2 4]` prefix |
| `assert ($a == 7)` | `[assert-eq $a 7]` (see below) |
| `(($a == 1) && ($b == 2))` | `[&& [== $a 1] [== $b 2]]` (variadic) |
| `$(2 + 3)` string interpolation | `$[+ 2 3]` |
| `(0 ..< 3)` range | `[range 0 3]` (option C, named) |
| `(1 ..= 5)` inclusive range | `[range-inclusive 1 5]` |
| `(-1)` negative literal | `-1` (lexer fix) |
| `($m ?. name)` safe nav | `[?. $m name]` (prefix, **revisit** — see §3.4) |
| `if (cond)` / `while (cond)` / `elif (cond)` | `if [cond]` etc. (5 sites in the codebase) |

### 3.1 `assert-eq` family as built-ins

To keep test code readable after `()` is gone, add to the builtin set:

```
assert-eq  assert-ne  assert-lt  assert-le  assert-gt  assert-ge
```

`assert <expr>` stays for non-comparison predicates.

### 3.2 Lexer fix for `-1`

Today, `-1` cannot be written as a literal in argument position — the
two existing workarounds are `(-1)` (in `buf_literal_value_negative.jacl`)
and `(- 1)` (in `buf_dynamic_chain_oob_neg.jacl`). With `()` removed,
the lexer must accept `-1` as a number literal directly. Rule: a leading
`-` glued to a digit with no whitespace is part of the numeric token.

### 3.3 Pipe + symbolic connectors stay in `{}`

`|`, and by extension other shell-style symbolic command connectors,
remain valid in `{}` command mode. They are not affected by the `()`
removal. This is the natural home for shell-pipeline syntax.

### 3.4 `?.` becomes prefix — needs more design

`[?. $m name]` is the migration target, but the resulting form is
heavier than the original `($m ?. name)`. Options for the eventual
redesign:

- Mirror `->` as a glued suffix: `$m?.name` (no spaces). Most
  ergonomic; would require parser support for the suffix-operator
  pair.
- Stay prefix: `[?. $m name]`. Regular but verbose.

Defer. Prefix is the holding pattern.

### 3.5 Ranges as named prefix (option C)

Considered:
- **A.** Primary parser form: `0..<3` (Rust-style).
- **B.** Operator-prefix: `[..< 0 3]`.
- **C.** Named prefix: `[range 0 3]` / `[range-inclusive 1 5]`. ← chosen.

Chosen for **regularity** — no operator-as-head weirdness, no special
parser case.

---

## 4. Proc return type position

Current: `proc i64 typed-add {i64 a, i64 b} { + $a $b }`. The proc name
is sandwiched between the return type and the params block.

New:

```
proc typed-add {i64 a, i64 b} i64 { + $a $b }
```

Parsing rule: an optional bare type token between the params block and
the body block. Two `{}` blocks → typed return. One `{}` block →
untyped return. Unambiguous.

`->` was considered for the separator; rejected as not adding clarity.

---

## 5. For-loop syntax — collection-first (kept)

Three forms, all keep:

```
for $xs { … use $it … }                  # implicit
for $xs n { … use $n … }                 # explicit (collection-first)
for {set i 0; < $i 10; incr i} { … }     # C-style (Tcl-shaped)
```

### Decision: keep collection-first

Originally proposed reversing to `for n $xs { … }` to match the
binding-target convention (`def name value`, `mut name value`,
`set name value`). The swap was tried (commit `a8ef956`) and reverted
(commit `4cd3d57`) because it breaks pipe threading.

Pipes thread the piped value as the **first** argument:
`coll | foo a b` → `[foo coll a b]`. With name-first explicit-for, the
threaded collection would land in the *name* slot:

```
$xs | for n { … }     # threads as [for $xs n { … }]
```

That's still collection-first form. The only way name-first survives
is to forbid that idiom, which is a real ergonomic loss in a
shell/glue language where piping into a loop is everyday work.

Collection-first preserves the natural pipe shape and is what the
current implementation already does. The tour now documents the
rationale at the for-loop section so future readers don't re-litigate.

### Parsing (unchanged)

- 2 args, first is `{}` block → C-style header.
- 2 args, first is value/`$`-ref → implicit.
- 3 args → explicit (collection, name, body).

### Tour update

The C-style form is currently undocumented in `tour.jacl`. Add a
third example to the for-loop section:

```jacl
mut product 1
for {mut i 1; <= $i 4; incr i} { set product [* $product $i] }
assert [assert-eq $product 24]
```

---

## 6. Box vs atom — keep both

Measured under `test/test_perf.c` via newly added scenarios
(`atom_churn`, `box_reset_hot`, `atom_reset_hot`; commit `b84acb9`).

Results (medians, release build, single machine):

| Scenario | Op | Median | Per-op | Mem |
|----------|----|--------|--------|-----|
| `box_churn` | alloc + deref ×2000 | 430 µs | ~215 ns/box | 16 B/box |
| `atom_churn` | alloc + deref ×2000 | 458 µs | ~229 ns/atom | 32 B/atom |
| `box_reset_hot` | reset ×1M | 155 ms | ~155 ns/reset | — |
| `atom_reset_hot` | reset ×1M | 201 ms | ~201 ns/reset | — |

- **Atom reset is ~30% slower than box reset** in a tight loop on a
  long-lived cell.
- **Atom objects are 2× the size of box objects** (32 B vs 16 B per
  object — atom always uses `OBJ_ATOM_REF` with watcher state and
  atomic-aligned fields; box can be inline for small payloads).
- **Atom alloc is ~6% slower than box alloc** (small; both share the
  GC bump path).

The cost matches the implementation: atom reset adds an acquire-load
of the prior value, a release-store of the new value, and a watcher
dispatch (a no-op when no watchers are registered, but still a
function call). Box reset is a plain store plus barriers.

**Decision: keep both as-is.** The 30% per-op cost and 2× memory cost
are real, and atom earns its keep where shared mutability is wanted.
Box stays as the thread-local fast path.

Caveats: harness variance is high (the wall-time includes
scheduler/worker noise from the runtime). Medians stable across runs;
exact ratios should be taken to no more than two significant figures.

---

## 7. SM-compiler bugs still live

The apology comment at `test/jacl/tour.jacl:285–290` is **accurate
and current** as of HEAD. Two bugs in the state-machine transform are
unfixed:

### Bug 1 — `for COLL VAR` inside SM-compiled proc

Repro:

```jacl
# mode: concurrent
proc inner {} {
  def fut [spawn { + 1 2 }]
  mut total 0
  for [vec 1 2 3] n { set total [+ $total $n] }
  + $total [await $fut]
}
print [inner]                # expect 6, gets: runtime error: (unknown)
```

Without the `spawn`/`await` the same `for` passes — the bug is in
the SM transform's handling of locals declared by the explicit-binding
form crossing suspension boundaries.

### Bug 2 — positional destructure of suspending-primitive result inside SM-compiled proc

Repro:

```jacl
# mode: concurrent
proc inner {} {
  def [a b c] [parallel { 10 } { 20 } { 30 }]
  + $a [+ $b $c]
}
print [inner]
# expect 60, gets: type error in '+': expected matching numeric types,
# got nil and nil
```

All three destructure positions bind to nil. Without the suspension
(`def [a b c] [vec 10 20 30]` instead) the destructure passes. The SM
transform is not spilling/restoring the vector result across the
suspension in a form the destructure can read.

### Not the same bug as the recent fixes

Three SM/operand-stack fixes landed in May:
- `8669f5d` — initial operand-stack-loss fix (await/sleep).
- `4f885bb` — extended spill to yield/parallel/race.
- `30d4bdd` — audit + 6 more spill special-form entries.
- `4c329a0` — **inline-suspending-siblings fix**: `+ [susp 10] [susp 16]`
  no longer corrupts. This is the most prominent recent SM fix and
  reads superficially close to the apology, but it's a different bug
  class.

The apology bugs are about **specific syntactic forms** (`for COLL VAR`,
positional vector destructure) crossing the SM boundary, not about
operand-stack spilling for inline expressions.

### Implication for the redesign

- The for-loop reversal (§5) **does not dodge Bug 1**. Whatever shape
  the explicit form takes, the SM transform needs the same fix.
- Bug 2 is the more painful one. With the struct/print-symmetry
  changes (§1, §2) destructuring becomes more important than ever, and
  `def [a b c] [parallel …]` is a core concurrent idiom. Worth
  prioritizing.

---

## Implementation order (suggested)

Nothing is committed beyond the bench files. Order roughly by
"decides downstream stuff first":

1. SYNTAX.md updates capturing §1–§5.
2. Lexer fix for `-1` literal (§3.2).
3. `assert-eq` family builtins (§3.1).
4. `[range a b]` / `[range-inclusive a b]` builtins (§3.5).
5. Drop `()` parser support; migrate the ~5 control-flow sites and
   the `$(...)` interpolation form (§3).
6. Struct constructor + printer changes (§2).
7. For-loop explicit-form argument-order swap (§5).
8. Proc return-type position swap (§4).
9. C-style for + new explicit form examples added to `tour.jacl` (§5).
10. SM transform fixes for Bug 1 and Bug 2 (§7) — independent track,
    can run in parallel with the syntax work.

Box/atom (§6) requires no code changes.
