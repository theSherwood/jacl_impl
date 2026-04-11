# US-001: Hygiene De-Risking Spike

**Goal:** Validate the proposed mark-propagation hygiene rule for the staged
macro-eval rewrite **before** touching production code, so that two weeks of
implementation don't unravel on a wrong assumption.

**Validated rule (with one carve-out):**

> The currently-executing macro call carries a `current_mark` on its frame.
> Every `make-syntax` invocation that runs *during that frame* applies
> `current_mark` to the freshly-allocated syntax object. Spliced-in
> argument syntax objects (the values produced by `~expr` and `~@expr`)
> pass through unchanged with their existing scope marks. **Carve-out:**
> nodes flagged `is_caret = 1` are constructed with `scope_mark = 0`
> regardless of `current_mark`. Likewise, the gensym builtin (US-010)
> is the **only** macro-frame builtin that allocates a node with mark
> `current_mark` *and* sets `is_gensym = 1`; everything else just
> applies the mark uniformly.

**Verdict:** the rule produces byte-identical syntax trees to today's
`expand__subst_template` + `expand__stamp_syntax` pipeline for **6 of 7**
representative templates. The one divergence (Template 5: nested
syntax-quote) is a pre-existing limitation of today's expander, **not** a
problem with the new rule, and is documented in §6 with a recommended
resolution.

The rest of this document picks the seven templates, hand-lowers each
into the bytecode that the new staged compiler will emit, traces the
scope-mark assignments through both pipelines, and diffs the results.

---

## 1. How to read the trace tables

For each template the trace shows:

- **Source** — the JACL surface syntax of the macro and its caller.
- **Today's pipeline** — the sequence of operations performed by
  `expand__node` → `syntax_from_ast` → `expand__stamp_syntax` →
  `expand__subst_template` (in `src/syntax.c`).
- **Lowered form** — the proposed bytecode the new compiler will emit
  for the same `syntax-quote` body, expressed as a tree of `make-syntax`
  / `vec` calls. (The bytecode itself is `OP_SYNTAX_OP` with subops 7-12
  from US-016, and the existing `vec` builtins.)
- **Mark trace** — for every node in the resulting syntax tree, the
  scope mark assigned by *both* pipelines, side by side. Marks are named
  symbolically: `M_<macro>` for a fresh per-call mark, `0` for the
  default (caller-scope) mark, and `caller(...)` for whatever mark the
  argument carries when handed in.

The "make-syntax cmd" / "make-syntax var-ref" / etc. lines below
correspond directly to existing `OP_SYNTAX_OP` subops:

| Subop | Op                     | Notes                                |
| ----- | ---------------------- | ------------------------------------ |
| 7     | make-syntax lit-int    | one operand: i32                     |
| 8     | make-syntax lit-float  | one operand: f32                     |
| 9     | make-syntax lit-string | one operand: string                  |
| 10    | make-syntax var-ref    | one operand: name string             |
| 11    | make-syntax command    | two operands: head syntax, args vec  |
| 12    | make-syntax block      | one operand: commands vec            |

Three new construction subops are needed to support the staged
compiler's full lowering, and are introduced here only as part of the
spike's bookkeeping (they will be added in US-007/US-009/US-010 with
production tests):

| Subop | Op                       | Notes                                          |
| ----- | ------------------------ | ---------------------------------------------- |
| 15    | make-syntax var-ref ^    | name string + caret bit (mark **always** = 0)  |
| 16    | gensym                   | optional prefix (real builtin per US-010)      |
| 17    | make-syntax syntax-quote | one operand: child syntax (literal nesting)    |

The rule is the same for all of these: the macro frame's `current_mark`
is applied to the new node when it is constructed, **except** for the
caret variant (subop 15), where the mark is forced to 0.

---

## 2. The seven representative templates

The PRD asks for "5–8 representative templates from existing macro
tests" covering: simple unquote, `~@` splicing, `^`-prefixed identifiers,
`gensym`, nested syntax-quote, identifier capture/shadowing, and
transitive macro composition.

| #   | Template                                              | From test                                               | Risk surface                  |
| --- | ----------------------------------------------------- | ------------------------------------------------------- | ----------------------------- |
| 1   | `defmacro double {x} { syntax-quote [+ ~x ~x] }`      | `test_us011_double_macro`                               | simple unquote, repeated ref  |
| 2   | `defmacro wrap {args} { syntax-quote [print ~@args] }`| `test_us010_unquote_splicing_in_args`                   | `~@` splicing                 |
| 3   | `defmacro bindit {v} { syntax-quote [mut ^it ~v] }`   | `test_us013_caret_binds_in_caller_scope`                | `^`-prefix opt-out            |
| 4   | `defmacro m {v} { syntax-quote [mut [gensym] ~v] }`   | `test_us014_gensym_in_macro_mut`                        | gensym hygienic temp          |
| 5   | `defmacro mk {} { syntax-quote {syntax-quote [+ 1 2]} }` | `test_us010_nested_syntax_quote_literal`             | nested `syntax-quote`         |
| 6   | `defmacro m {x} { syntax-quote { mut tmp 1; [+ ~x $tmp] } }` | `test_us012_hygiene_local_shadow`               | identifier capture/shadowing  |
| 7   | `defmacro add_hundred {x} { syntax-quote { mut t 100; [add_ten [+ ~x $t]] } }` (where `add_ten` is itself a macro) | `test_us012_hygiene_nested_macros` | transitive macro composition |

These cover every code path in `expand__subst_template`, every branch in
`expand__stamp_syntax`, and every observable mark assignment exercised
by the existing US-012/US-013/US-014 hygiene tests.

---

## 3. Trace tables — Templates 1 through 7

### Template 1 — `double` (simple unquote, repeated ref)

**Source:**
```jacl
defmacro double {x} { syntax-quote [+ ~x ~x] }
double 21
```

**Today's pipeline (expand__node + expand__subst_template):**

1. Encounter macro call `double 21`. Allocate fresh mark `M_double`.
2. `syntax_from_ast(template_ast)` produces:
   ```
   COMMAND  (mark 0)
   ├─ head: LIT_STRING "+"  (mark 0)
   ├─ args[0]: UNQUOTE → VAR_REF "x"  (mark 0)
   └─ args[1]: UNQUOTE → VAR_REF "x"  (mark 0)
   ```
3. `expand__stamp_syntax(tree, M_double)` walks the tree and overwrites
   every node's `scope_mark` with `M_double` (no caret here):
   ```
   COMMAND  (mark M_double)
   ├─ head: LIT_STRING "+"  (mark M_double)
   ├─ args[0]: UNQUOTE → VAR_REF "x"  (mark M_double)
   └─ args[1]: UNQUOTE → VAR_REF "x"  (mark M_double)
   ```
4. `expand__subst_template(tree, params=[x], args=[lit-int 21 (mark 0)])`
   walks the COMMAND, allocates a new COMMAND copying `scope_mark = M_double`
   from the template node, replaces each UNQUOTE with `arg_vals[0]` =
   `LIT_INT 21 (mark 0)` (the caller's value, returned by reference,
   *not* re-stamped).

**Resulting tree (today):**
```
COMMAND       mark = M_double
├─ head: LIT_STRING "+"   mark = M_double
├─ args[0]: LIT_INT 21    mark = 0 (caller)
└─ args[1]: LIT_INT 21    mark = 0 (caller)
```

**Lowered form (proposed staged compiler):**

```
make-syntax command
  ├─ head: make-syntax lit-string "+"     ; subop 9
  └─ args: vec
       ├─ ~x         ; load param x → caller's syntax val
       └─ ~x         ; load param x → caller's syntax val
```

In bytecode terms, the body of the closure for `double` runs roughly:

```
GET_LOCAL   x
GET_LOCAL   x                        ; x is on the stack twice
VEC_NEW 2                            ; or whatever vec ctor exists
PUSH_STR    "+"
SYNTAX_OP   9                        ; make-syntax lit-string "+"
SWAP                                 ; head, args
SYNTAX_OP   11                       ; make-syntax command
RETURN
```

**Mark trace under proposed rule (current_mark = M_double):**

| Node                  | Created by         | scope_mark assigned |
| --------------------- | ------------------ | ------------------- |
| LIT_STRING "+"        | subop 9 (in frame) | `M_double`          |
| outer COMMAND         | subop 11 (in frame)| `M_double`          |
| args[0] (LIT_INT 21)  | spliced from `~x`  | `0` (caller)        |
| args[1] (LIT_INT 21)  | spliced from `~x`  | `0` (caller)        |

**Diff vs. today:**

| Node                | Today      | Proposed   | Match? |
| ------------------- | ---------- | ---------- | ------ |
| outer COMMAND       | `M_double` | `M_double` | ✓      |
| head LIT_STRING "+" | `M_double` | `M_double` | ✓      |
| args[0] LIT_INT 21  | `0`        | `0`        | ✓      |
| args[1] LIT_INT 21  | `0`        | `0`        | ✓      |

**Verdict: byte-identical.**

---

### Template 2 — `wrap` (`~@` splicing)

**Source:**
```jacl
defmacro wrap {args} { syntax-quote [print ~@args] }
wrap [vec [quote a] [quote b] [quote c]]
```

**Today's pipeline:**

1. `M_wrap` allocated.
2. Template tree:
   ```
   COMMAND
   ├─ head: LIT_STRING "print"
   └─ args[0]: UNQUOTE_SPLICING → VAR_REF "args"
   ```
3. `expand__stamp_syntax` paints the entire tree with `M_wrap`.
4. `expand__subst_template` allocates a new COMMAND with mark `M_wrap`,
   loops over old args. The single UNQUOTE_SPLICING resolves to the
   caller's vec of three syntax objects (each with mark 0). The loop
   pushes each spliced element into `new_args` directly, **without**
   re-stamping.

**Resulting tree:**
```
COMMAND               mark = M_wrap
├─ head: LIT_STRING "print"  mark = M_wrap
├─ args[0]: VAR_REF "a"      mark = 0 (caller)
├─ args[1]: VAR_REF "b"      mark = 0 (caller)
└─ args[2]: VAR_REF "c"      mark = 0 (caller)
```

**Lowered form (proposed staged compiler):**

```
make-syntax command
  ├─ head: make-syntax lit-string "print"
  └─ args: vec-flatten [
       (vec)                              ; literals from template
       ~args                              ; spliced inline
     ]
```

The `~@args` lowers to a flatten step: take whatever vec of syntax
objects `args` evaluates to, and concatenate it into the parent's args
vec. In bytecode this is a sequence of `vec-concat` / `vec-push` ops; the
exact lowering doesn't matter for hygiene as long as elements are not
re-allocated.

**Mark trace under proposed rule (current_mark = M_wrap):**

| Node                | Created by                  | scope_mark assigned |
| ------------------- | --------------------------- | ------------------- |
| LIT_STRING "print"  | subop 9 (in frame)          | `M_wrap`            |
| outer COMMAND       | subop 11 (in frame)         | `M_wrap`            |
| args[0] VAR_REF "a" | spliced from `~@args`       | `0` (caller)        |
| args[1] VAR_REF "b" | spliced from `~@args`       | `0` (caller)        |
| args[2] VAR_REF "c" | spliced from `~@args`       | `0` (caller)        |

**Diff:** all four nodes match. ✓ **Byte-identical.**

This case is the most subtle one to get right in the implementation
because the splicing op must **not** allocate new wrapper nodes around
the spliced elements. The vec-flatten lowering achieves this by
handing the existing element references directly into the new args vec.

---

### Template 3 — `bindit` (`^`-prefix opt-out)

**Source:**
```jacl
defmacro bindit {v} { syntax-quote [mut ^it ~v] }
proc run { bindit 42 ; $it }
```

**Today's pipeline:**

1. `M_bindit` allocated.
2. Template tree:
   ```
   COMMAND
   ├─ head: LIT_STRING "mut"
   ├─ args[0]: VAR_REF "it"  (is_caret = 1)   ← parser set this
   └─ args[1]: UNQUOTE → VAR_REF "v"
   ```
3. `expand__stamp_syntax` walks the tree. For the VAR_REF "it" node it
   sees `is_caret == 1` and **leaves `scope_mark` alone** (it stays at
   0). Every other node gets `M_bindit`.
4. `expand__subst_template` allocates a new COMMAND copying `M_bindit`,
   recurses into args:
   - args[0] is a VAR_REF, not an UNQUOTE/UNQUOTE_SPLICING, so it falls
     through to "return as-is" — the caret-marked node passes through
     unchanged.
   - args[1] is an UNQUOTE, replaced by the caller's `LIT_INT 42` (mark 0).

**Resulting tree:**
```
COMMAND                          mark = M_bindit
├─ head: LIT_STRING "mut"        mark = M_bindit
├─ args[0]: VAR_REF "it" ^       mark = 0   ← caret skipped stamping
└─ args[1]: LIT_INT 42           mark = 0 (caller)
```

The caller's `$it` reference (mark 0) then resolves to this `mut ^it`
binding because both have mark 0.

**Lowered form (proposed staged compiler):**

```
make-syntax command
  ├─ head: make-syntax lit-string "mut"
  └─ args: vec
       ├─ make-syntax var-ref-caret "it"     ; new subop 15: forces mark = 0
       └─ ~v
```

The compiler must recognize that the AST node carries `is_caret = 1` and
emit subop 15 instead of subop 10.

**Mark trace under proposed rule (current_mark = M_bindit):**

| Node                | Created by                       | scope_mark assigned    |
| ------------------- | -------------------------------- | ---------------------- |
| LIT_STRING "mut"    | subop 9 (in frame)               | `M_bindit`             |
| VAR_REF "it" ^      | subop 15 (in frame, caret)       | `0` (forced by subop)  |
| outer COMMAND       | subop 11 (in frame)              | `M_bindit`             |
| args[1] LIT_INT 42  | spliced from `~v`                | `0` (caller)           |

**Diff:** all four nodes match. ✓ **Byte-identical** — provided we
add the caret carve-out to the rule.

This is the **first refinement** of the rule. The naïve "always apply
`current_mark`" would break this template by stamping `it` with
`M_bindit`, after which the caller's `$it` would no longer resolve.

---

### Template 4 — `m` (gensym hygienic temp)

**Source:**
```jacl
defmacro m {v} { syntax-quote [mut [gensym] ~v] }
proc run { mut t 99 ; m 7 ; $t }
```

The interesting question is: does the `[gensym]` call in the template
produce a var-ref with mark `M_m`, mark 0, or some other value? Today's
behavior fixes this; the new rule must reproduce it.

**Today's pipeline:**

1. `M_m` allocated.
2. Template tree:
   ```
   COMMAND
   ├─ head: LIT_STRING "mut"
   ├─ args[0]: COMMAND  (head LIT_STRING "gensym", args [])
   └─ args[1]: UNQUOTE → VAR_REF "v"
   ```
3. `expand__stamp_syntax` paints everything `M_m`. The
   `[gensym]` COMMAND node now carries mark `M_m`.
4. `expand__subst_template` walks. When recursing into args[0], the
   first thing it does is call `expand__is_gensym_call`, which
   recognizes the `[gensym]` shape and returns true. The substituter
   then calls `jacl_gensym_next(prefix="g", ..., scope_mark = syn->scope_mark)`
   — that is, the gensym var-ref inherits the *template node's* mark
   (`M_m`).
5. The new COMMAND wrapping it gets mark `M_m`. The `~v` arg is
   replaced by `LIT_INT 7` with mark 0.

**Resulting tree:**
```
COMMAND                              mark = M_m
├─ head: LIT_STRING "mut"            mark = M_m
├─ args[0]: VAR_REF "g__N" gensym    mark = M_m
└─ args[1]: LIT_INT 7                mark = 0 (caller)
```

Because the gensym var-ref has mark `M_m`, the caller's `$t` (mark 0)
does not collide with it — and indeed both `mut t 99` and `mut g__N 7`
coexist in the same lexical scope without name conflict.

**Lowered form (proposed staged compiler, with gensym as a real builtin
per US-010):**

```
make-syntax command
  ├─ head: make-syntax lit-string "mut"
  └─ args: vec
       ├─ gensym                          ; subop 16, real builtin
       └─ ~v
```

In US-010 the gensym subop allocates a fresh var-ref with `is_gensym = 1`
and `scope_mark = current_mark`. (See PRD §US-010 acceptance criterion:
"returns a fresh syntax var-ref with the macro's current scope mark
applied".)

**Mark trace under proposed rule (current_mark = M_m):**

| Node                  | Created by                       | scope_mark assigned   |
| --------------------- | -------------------------------- | --------------------- |
| LIT_STRING "mut"      | subop 9 (in frame)               | `M_m`                 |
| VAR_REF "g__N" gensym | subop 16 (in frame)              | `M_m`                 |
| outer COMMAND         | subop 11 (in frame)              | `M_m`                 |
| args[1] LIT_INT 7     | spliced from `~v`                | `0` (caller)          |

**Diff:** all four nodes match. ✓ **Byte-identical.**

Note: the gensym builtin sits at a slight tension with the general
rule. It needs to allocate a node that is *both* tagged `is_gensym = 1`
**and** stamped with `current_mark`. This is fine — the rule says
"every make-syntax invocation applies `current_mark`," and gensym is
a make-syntax invocation that happens to also set the gensym tag. No
special-case logic at the mark level is needed.

---

### Template 5 — `mk` (nested syntax-quote)

**Source:**
```jacl
defmacro mk {} { syntax-quote {syntax-quote [+ 1 2]} }
mk
```

This is the **one case where today's behavior diverges from what a
Scheme programmer would expect**, and it deserves careful handling.

**Today's pipeline:**

1. `M_mk` allocated.
2. Template tree:
   ```
   SYNTAX_QUOTE
   └─ child: COMMAND
              ├─ head: LIT_STRING "+"
              ├─ args[0]: LIT_INT 1
              └─ args[1]: LIT_INT 2
   ```
3. `expand__stamp_syntax` paints everything (including the inner
   COMMAND, head, and lit-ints) with `M_mk`.
4. `expand__subst_template` recurses into the SYNTAX_QUOTE branch. The
   `if (syn->kind == SYNTAX_SYNTAX_QUOTE) return tmpl;` line at
   `src/syntax.c:1372` returns the **entire** stamped subtree as-is —
   it does **not** allocate a new SYNTAX_QUOTE wrapper, and any
   unquotes inside the inner are left as literal `SYNTAX_UNQUOTE`
   nodes (intentional — see comment in `syntax__splice_template`).

**Resulting tree:**
```
SYNTAX_QUOTE                          mark = M_mk
└─ child: COMMAND                     mark = M_mk
           ├─ head: LIT_STRING "+"    mark = M_mk
           ├─ args[0]: LIT_INT 1      mark = M_mk
           └─ args[1]: LIT_INT 2      mark = M_mk
```

**Lowered form (proposed staged compiler):**

The naïve PRD-as-written reading is "lower nested syntax-quote like
Scheme quasiquote: outer treats inner as a literal-ish subtree, but
unquotes inside the inner are still subexpressions of the outer level."
This would produce a tree shape like:

```
make-syntax syntax-quote                    ; subop 17
  └─ make-syntax command
       ├─ head: make-syntax lit-string "+"
       └─ args: vec
            ├─ make-syntax lit-int 1
            └─ make-syntax lit-int 2
```

**Mark trace (proposed rule, current_mark = M_mk):**

| Node                | Created by                       | scope_mark assigned |
| ------------------- | -------------------------------- | ------------------- |
| LIT_STRING "+"      | subop 9 (in frame)               | `M_mk`              |
| LIT_INT 1           | subop 7 (in frame)               | `M_mk`              |
| LIT_INT 2           | subop 7 (in frame)               | `M_mk`              |
| inner COMMAND       | subop 11 (in frame)              | `M_mk`              |
| outer SYNTAX_QUOTE  | subop 17 (in frame)              | `M_mk`              |

**Diff:** all five nodes match. ✓ **Byte-identical.**

(I assert this with one hedge: today's `expand__stamp_syntax` walks
into the SYNTAX_SYNTAX_QUOTE branch and recurses into its child, so
the inner subtree DOES get stamped with `M_mk`. Since the proposed
lowering also constructs every inner node inside the macro frame, both
pipelines arrive at the same result.)

**The genuine divergence — unquotes inside nested syntax-quote.** If
someone writes:

```jacl
defmacro mk {x} { syntax-quote {syntax-quote [+ ~x ~x]} }
```

then today the inner `~x` is **never** substituted — the entire
SYNTAX_SYNTAX_QUOTE subtree is returned literal, including the
SYNTAX_UNQUOTE markers. A naïve PRD-faithful lowering of nested
syntax-quote would substitute it (the unquote is at outer level 1, the
inner syntax-quote bumps to level 2, and a `~x` at level 2 would not
fire — but if there were a `~~x` it would). **There is no test case in
the existing suite that exercises a `~` inside a nested
`syntax-quote`,** so this divergence is unobserved by the migration
matrix.

The single test (`test_us010_nested_syntax_quote_literal`) uses the
fully-literal `[foo 1]` form and produces a result whose structure
(SYNTAX_COMMAND with args length 1) is identical under both rules.

**Recommendation for the migration:** preserve today's "treat nested
syntax-quote as completely literal" behavior in the new lowering. That
is, for `AST_SYNTAX_QUOTE` whose body contains a nested
`AST_SYNTAX_QUOTE`, emit the inner as a single compile-time constant
(via `syntax_from_ast`) wrapped by `make-syntax syntax-quote`. This
matches today's behavior bit-for-bit and defers the harder question of
multi-level quasiquote semantics to a future PRD that would need its
own test cases.

The PRD's US-007 acceptance criterion ("nested syntax-quote compiles
correctly: outer treats inner as a literal-ish subtree, but unquotes
inside the inner are still subexpressions of the outer level") will
need a small revision: it should say that **today's behavior is
preserved** rather than "Scheme-style nesting is implemented." This is
captured in §6 below.

---

### Template 6 — `m` (identifier capture / shadowing)

**Source:**
```jacl
defmacro m {x} {
  syntax-quote {
    mut tmp 1
    [+ ~x $tmp]
  }
}
proc run {} {
  mut tmp 99
  m $tmp
}
```

**Today's pipeline:**

1. `M_m` allocated.
2. The body is a SYNTAX_BLOCK containing:
   - SYNTAX_COMMAND `mut tmp 1`
     - head: LIT_STRING "mut"
     - args[0]: VAR_REF "tmp"
     - args[1]: LIT_INT 1
   - SYNTAX_COMMAND `[+ ~x $tmp]`
     - head: LIT_STRING "+"
     - args[0]: UNQUOTE → VAR_REF "x"
     - args[1]: VAR_REF "tmp"
3. `expand__stamp_syntax` paints every node with `M_m`. Note: the
   VAR_REF "tmp" nodes (both occurrences) are now `M_m`-marked.
4. `expand__subst_template`: the `~x` UNQUOTE is replaced with the
   caller's argument, which is `VAR_REF "tmp"` with mark 0 (the
   caller's `$tmp` reference, allocated when the caller's source was
   parsed and `syntax_from_ast`-ed at expansion time).

**Resulting tree:**
```
BLOCK                                    mark = M_m
├─ COMMAND "mut"                         mark = M_m
│   ├─ head: LIT_STRING "mut"            mark = M_m
│   ├─ args[0]: VAR_REF "tmp"            mark = M_m   ← template's tmp
│   └─ args[1]: LIT_INT 1                mark = M_m
└─ COMMAND "+"                           mark = M_m
    ├─ head: LIT_STRING "+"              mark = M_m
    ├─ args[0]: VAR_REF "tmp"            mark = 0     ← caller's $tmp
    └─ args[1]: VAR_REF "tmp"            mark = M_m   ← template's $tmp
```

The template's `mut tmp` (mark `M_m`) and the template's `$tmp` (mark
`M_m`) both resolve to each other — a hygienic local with the macro
scope mark. The caller's `$tmp` reference (mark 0), spliced in via
`~x`, resolves to the caller's own `mut tmp 99` (mark 0). Hygiene
upheld.

**Lowered form (proposed staged compiler):**

```
make-syntax block (vec
  make-syntax command (
    make-syntax lit-string "mut",
    vec (
      make-syntax var-ref "tmp",
      make-syntax lit-int 1
    )
  ),
  make-syntax command (
    make-syntax lit-string "+",
    vec (
      ~x,                                ; caller's $tmp, mark 0
      make-syntax var-ref "tmp"          ; template's $tmp
    )
  )
)
```

**Mark trace (proposed rule, current_mark = M_m):**

| Node                            | Created by                | scope_mark |
| ------------------------------- | ------------------------- | ---------- |
| outer BLOCK                     | subop 12 (in frame)       | `M_m`      |
| `mut` COMMAND                   | subop 11 (in frame)       | `M_m`      |
| LIT_STRING "mut"                | subop 9 (in frame)        | `M_m`      |
| VAR_REF "tmp" (binding)         | subop 10 (in frame)       | `M_m`      |
| LIT_INT 1                       | subop 7 (in frame)        | `M_m`      |
| `+` COMMAND                     | subop 11 (in frame)       | `M_m`      |
| LIT_STRING "+"                  | subop 9 (in frame)        | `M_m`      |
| VAR_REF "tmp" (caller's, ~x)    | spliced                   | `0`        |
| VAR_REF "tmp" (template's $tmp) | subop 10 (in frame)       | `M_m`      |

**Diff:** all nine nodes match. ✓ **Byte-identical.**

This is the workhorse hygiene test. The proposed rule handles it
correctly *only* because the caller's `$tmp` is spliced in via `~x`
rather than constructed by the macro — so it carries its caller-side
mark of 0, which keeps it disjoint from the macro's `M_m`-marked
`tmp` references. This is the central safety property the staged
rewrite must preserve.

---

### Template 7 — `add_hundred` calling `add_ten` (transitive composition)

**Source:**
```jacl
defmacro add_ten {x} {
  syntax-quote {
    mut t 10
    [+ ~x $t]
  }
}
defmacro add_hundred {x} {
  syntax-quote {
    mut t 100
    [add_ten [+ ~x $t]]
  }
}
proc run { mut t 0 ; add_hundred $t }
```

**Today's pipeline:**

1. `add_hundred $t` is encountered. `M_h` allocated.
2. Template stamped with `M_h`. Tree:
   ```
   BLOCK (M_h)
   ├─ COMMAND `mut t 100` (M_h)
   └─ COMMAND `add_ten [+ ~x $t]` (M_h)
       ├─ head: LIT_STRING "add_ten" (M_h)
       └─ args[0]: COMMAND `+ ~x $t` (M_h)
                    ├─ head: LIT_STRING "+" (M_h)
                    ├─ args[0]: UNQUOTE → VAR_REF "x" (M_h)
                    └─ args[1]: VAR_REF "t" (M_h)
   ```
3. `expand__subst_template` substitutes `~x` with the caller's
   `VAR_REF "t"` (mark 0). The subst returns a new BLOCK with mark `M_h`,
   containing two new COMMAND nodes (each with mark `M_h`); the new
   `+` COMMAND has args `[caller-t (mark 0), template-t (mark M_h)]`.
4. `expand__node` re-expands the result. It encounters the
   `[add_ten ...]` call. `M_a` allocated.
5. `add_ten`'s template is stamped with `M_a`. Substitution: `~x` is
   replaced by the **already-substituted** `+` command from step 3,
   i.e. `COMMAND(+, args=[caller-t mark 0, mh-t mark M_h])`. The
   spliced subtree retains its existing marks.

**Resulting tree (after both expansions):**
```
BLOCK (M_h)                                 ; from add_hundred
├─ COMMAND "mut t 100" (M_h)
│   ├─ LIT_STRING "mut" (M_h)
│   ├─ VAR_REF "t" (M_h)
│   └─ LIT_INT 100 (M_h)
└─ BLOCK (M_a)                              ; from add_ten's expansion
    ├─ COMMAND "mut t 10" (M_a)
    │   ├─ LIT_STRING "mut" (M_a)
    │   ├─ VAR_REF "t" (M_a)
    │   └─ LIT_INT 10 (M_a)
    └─ COMMAND "+" (M_a)
        ├─ LIT_STRING "+" (M_a)
        ├─ COMMAND "+" (M_h)              ← spliced from add_hundred
        │   ├─ LIT_STRING "+" (M_h)
        │   ├─ VAR_REF "t" (0)            ← caller's $t, original
        │   └─ VAR_REF "t" (M_h)
        └─ VAR_REF "t" (M_a)
```

Three distinct `t` bindings: caller's (mark 0), `add_hundred`'s (`M_h`),
and `add_ten`'s (`M_a`). Each `$t` reference resolves to the matching
binding. Sum: `0 + 100 + 10 = 110` (matches the
`test_us012_hygiene_nested_macros` assertion).

**Lowered form (proposed staged compiler):**

Two closures, each with a body lowered the same way as Template 6.
When `add_hundred` runs as a closure with current_mark `M_h`, it
constructs nodes stamped `M_h`. Its `~x` is the caller's `$t` (mark 0),
spliced in. When the result is re-expanded and `add_ten` is invoked,
its closure runs with current_mark `M_a`; `~x` for that call is the
already-built `[+ ~x $t]` subtree from `add_hundred`, which retains its
`M_h` marks for the constructed parts and `0` for the caller's spliced
piece.

**Mark trace under proposed rule:**

| Node                                 | Created in frame | scope_mark    |
| ------------------------------------ | ---------------- | ------------- |
| add_hundred outer BLOCK              | M_h              | `M_h`         |
| `mut t 100` COMMAND + children       | M_h              | `M_h`         |
| add_ten BLOCK (after re-expansion)   | M_a              | `M_a`         |
| `mut t 10` COMMAND + children        | M_a              | `M_a`         |
| add_ten's outer `+` COMMAND          | M_a              | `M_a`         |
| add_hundred's inner `+` COMMAND      | M_h (preserved)  | `M_h`         |
| caller's `t` (deepest)               | 0 (preserved)    | `0`           |
| add_hundred's template `t`           | M_h (preserved)  | `M_h`         |
| add_ten's template `t`               | M_a              | `M_a`         |

**Diff:** every mark matches. ✓ **Byte-identical** — and
critically, the iterative re-expansion mechanism (a macro result being
passed as an arg to another macro) works without any extra
mark-tracking machinery, because the spliced subtree simply carries its
marks along.

---

## 4. Summary table

| #   | Template                          | Match? | Notes                                                                                  |
| --- | --------------------------------- | ------ | -------------------------------------------------------------------------------------- |
| 1   | `double` simple unquote           | ✓      | trivial; both nodes get `M_double`, args spliced                                       |
| 2   | `wrap` `~@` splicing              | ✓      | requires vec-flatten lowering, no per-element re-allocation                            |
| 3   | `bindit` `^`-prefix               | ✓      | requires caret carve-out: subop 15 forces mark = 0                                     |
| 4   | `m` `[gensym]`                    | ✓      | gensym builtin allocates with current_mark; matches today's behavior                   |
| 5   | `mk` nested syntax-quote (literal)| ✓ *    | matches **for the literal case**; unquotes-inside-nested are unobserved by today's tests |
| 6   | `m` identifier shadowing          | ✓      | the central hygiene case; works because spliced args retain caller marks               |
| 7   | `add_hundred` ∘ `add_ten`         | ✓      | re-expansion is transparent; spliced subtrees carry their own marks                    |

\* see §6 for the recommended treatment of nested syntax-quote.

---

## 5. Validated rule (final wording, ready to implement in US-009)

```
Mark-propagation hygiene rule (staged macro evaluation):

  When a macro M is invoked at expansion time, the expander allocates
  a fresh integer scope mark and pushes a CallFrame for M's closure
  with that frame's current_mark set to it. While that frame is on the
  call stack, every OP_SYNTAX_OP construction subop (subops 7..N)
  allocates a JaclSyntax with scope_mark = current_mark, EXCEPT for
  the caret variant (subop 15) which always allocates with
  scope_mark = 0 regardless of current_mark.

  Spliced-in argument values (the syntax objects that flow out of an
  unquote `~expr` or unquote-splicing `~@expr`) are passed through
  unchanged: their existing scope_mark (whatever the caller's pipeline
  assigned) is preserved when they appear in a parent make-syntax
  args/commands vec.

  When a macro call's result is re-expanded and contains another
  macro call, that inner expansion runs in its own frame with its own
  fresh mark. The inner frame's mark applies only to nodes the inner
  closure newly constructs; the previously-built outer-call subtree
  carries its existing marks through unchanged.
```

Three concrete implications for the implementation:

1. **CallFrame must gain a `current_mark` field.** It is set when the
   expander invokes `jacl_ctx_run_closure(ctx, closure, args, argc,
   mark, &err)` and read by `OP_SYNTAX_OP` construction subops. Frames
   for non-macro calls (e.g. helper procs called from inside a macro
   body, once that becomes possible) inherit the parent frame's mark.

2. **`make-syntax var-ref` needs a caret variant.** Either as a new
   subop (15, used here) or as a flag bit on the existing subop 10 with
   an extra immediate operand. The choice doesn't affect the spike's
   conclusion.

3. **gensym** is just a regular construction subop (16) that happens
   to allocate with `is_gensym = 1`. It uses `current_mark` like
   everything else.

The carve-out for caret is the **only** exception to the rule. The
rule is otherwise uniform.

---

## 6. Open issue: nested `syntax-quote` semantics

Today's `expand__subst_template` treats `SYNTAX_SYNTAX_QUOTE` as a
fully literal subtree: the line at `src/syntax.c:1372` returns the
template as-is, without recursing into it for unquote substitution.
This means an `~x` inside a nested `syntax-quote` is never
substituted; it appears as a literal `SYNTAX_UNQUOTE` node in the
output.

Two facts about this:

1. **No existing test exercises this case.** The single nested
   `syntax-quote` test (`test_us010_nested_syntax_quote_literal`) uses
   a fully literal `[foo 1]` body and asserts only the resulting
   COMMAND's args length.

2. **The PRD's US-007 acceptance criterion as written would break this
   test if interpreted strictly.** It says "outer treats inner as a
   literal-ish subtree, but unquotes inside the inner are still
   subexpressions of the outer level (this is the same as Scheme's
   quasiquote nesting)." A literal Scheme reading would make `~x`
   inside a nested `syntax-quote` resolve at the OUTER level, which is
   *not* what today's implementation does.

**Recommendation:** in US-007, lower a nested `AST_SYNTAX_QUOTE` as
a single compile-time constant wrapped in `make-syntax syntax-quote`.
That is, when the compiler walking the OUTER syntax-quote body
encounters an INNER `AST_SYNTAX_QUOTE`, it does NOT recurse — it
calls `syntax_from_ast` on the entire inner subtree, emits the
result as a constant, and wraps it with `make-syntax syntax-quote`
(subop 17, which applies the current_mark to the wrapper but does
nothing to the embedded constant's marks). At runtime the wrapper
gets `current_mark` and the embedded subtree carries the marks
that `syntax_from_ast` produced (mark 0 throughout).

That matches today's behavior bit-for-bit for the literal case **and**
preserves the "ignore unquotes inside nested" semantics — the
embedded `SYNTAX_UNQUOTE` nodes are part of a constant, not active
splicing instructions.

The Scheme-style multi-level nesting can be revisited in a future PRD
if and when someone needs it; the current spike does not require
deciding it now.

**Action item for US-007:** update the acceptance criterion's wording
to "nested `syntax-quote` is preserved as a literal subtree (the
embedded constant is wrapped in `make-syntax syntax-quote` and not
recursed into for unquote substitution); this matches today's
behavior bit-for-bit."

---

## 7. Sequencing implications for the rest of the PRD

The spike confirms that the rule works without further architecture
changes. Specifically:

- **US-005 (`jacl_context_t`)** does NOT need to carry the
  `current_mark`. The mark belongs on the CallFrame, not the
  context. This keeps the context substrate clean for M14's eventual
  `interpret` builtin (which has no notion of macro hygiene).
- **US-007 (compile syntax-quote as expression)** can proceed with
  the lowering shown in §3 templates. The caret carve-out and the
  nested-syntax-quote handling are notes for the implementer, not
  blockers.
- **US-008 (staged path)** can use the existing closure machinery
  largely unchanged. The only addition is plumbing `current_mark` from
  the expander into the closure's outermost frame.
- **US-009 (mark propagation in make-syntax subops)** has a tight
  scope: read `current_mark` from the active frame, write it into
  every newly-allocated `JaclSyntax`, EXCEPT for the caret subop.
- **US-010 (gensym builtin)** allocates with `current_mark` and
  `is_gensym = 1`; no additional bookkeeping.
- **US-011/US-012 (test migration)** can proceed file-by-file with
  confidence that any divergence is implementation bug, not rule bug.

---

## 8. What this spike did NOT validate

- **Real bytecode execution.** The spike is a hand-trace; no
  production code was modified. The corresponding executable
  validation will land in US-007/US-008/US-009 as their tests run
  the new lowering through the actual VM.
- **Performance.** The new path executes the VM during expansion;
  this is unavoidably slower than tree-rewriting. The PRD's
  Non-Goals call out that >2× slowdown on `make test` would be a
  bug, but this spike does not measure it.
- **Forward references between staged macros in the same file.** US-008
  introduces a two-pass strategy for this. Not validated here.
- **The compile-time-constant inner template emitted by the
  recommendation in §6.** Need to check that the GC keeps the
  constant alive across re-runs of the closure (closures already do
  this via their constant pool, so it should work — but worth a
  test).

---

## Decision

✅ **Proceed with the rest of the PRD.** The mark-propagation rule
produces byte-identical output to today's expander for every observable
hygiene case in the existing test suite. The single carve-out (caret
opt-out) is documented and small. The single open question (nested
syntax-quote semantics) has a clean recommendation that preserves
today's behavior. No re-evaluation of Option A is required.
