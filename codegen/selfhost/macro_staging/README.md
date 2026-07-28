# Macro-staging differential gate (Phase 0)

The safety net for migrating compile-time macro evaluation off the legacy bytecode
VM (`src/vm.c`) onto the SVM backend — see `docs/SVM_MACRO_STAGING_PLAN.md`.

The migration's contract is that **nothing observable changes**: a program using
user `defmacro`s must emit byte-identical SVM IR whether the macro ran on `vm.c` or
on SVM. This harness enforces that.

| Path | Role |
|---|---|
| `corpus/*.jacl` | User-`defmacro` programs (control-flow template, arg-splice, nested/recursive expansion; grows to cover hygiene/gensym, variadic, error cases). |
| `golden/*.ir` | Oracle snapshots — the SVM IR each program emits with macros expanded on `vm.c`. Checked in; the diff target. |
| `run_diff.sh` | Build the native frontend, emit each corpus program, diff vs golden. `--update` regenerates golden. `build/` is git-ignored. |

```sh
codegen/selfhost/macro_staging/run_diff.sh            # gate: all must match
codegen/selfhost/macro_staging/run_diff.sh --update   # after an intentional corpus change
```

Later phases run the same gate with `JACL_STAGE_ON_SVM=1` (macros evaluated on SVM);
green means parity with the `vm.c` oracle.

## Phase 1(B) — syntax-value ABI (`syn_wire`)

Chosen approach (option B): represent syntax objects as plain data and pass them
across the host↔staged-macro boundary by **serialization**, not shared heap objects
(the two heaps use native pointers vs SVM window offsets, so objects aren't portable).

| File | Role |
|---|---|
| `syn_wire.{h,c}` | Compiler side of the ABI: `AstNode ↔ bytes`. Self-describing, preserves hygiene fields (`scope_mark`/`is_caret`/`is_gensym`). Corpus-scoped node kinds (command/block/literal/var-ref); unsupported kinds return NULL, grow as needed. |
| `syn_wire_test.c` + `run_wire_test.sh` | Round-trip test: parse → encode → decode → assert pretty-print equal **and** re-encode byte-identical. |

```sh
codegen/selfhost/macro_staging/run_wire_test.sh   # syn_wire round-trips
```

## Phase 1(B) — macro side of the ABI (`syn_rt`)

The mirror of `syn_wire` for code running **inside** a staged macro on the SVM
runtime: it converts the same wire bytes to/from the **plain-data** syntax
representation — a jaclrt **vector** `[kind, scope_mark, flags, …payload]` (option B:
no special GC type). Compiled to SVM as runtime code for real macros; built natively
(with an `__vm_*` shim) for the test.

| File | Role |
|---|---|
| `syn_rt.{h,c}` | `syn_wire bytes ↔ jaclrt plain-data vec`. Wire format identical to `syn_wire.c`. |
| `rt_native_shim.c` | Native single-threaded stubs for the SVM `__vm_*` intrinsics, so the runtime value/string/collection subset runs off-SVM for tests. |
| `gen_wire.c` / `rt_roundtrip.c` + `run_rt_codec_test.sh` | Cross-codec test: `AST → wire` (compiler side) then `wire → vec → wire` (runtime side), asserting **byte-identical** output. Proves the two codecs share one ABI. |

```sh
codegen/selfhost/macro_staging/run_rt_codec_test.sh   # syn_wire ≡ syn_rt on the wire
```

## Phase 1(B) — codegen lowering of `syntax-quote`

`codegen/codegen.c` now lowers `AST_SYNTAX_QUOTE` (via `compile_synquote`) into runtime
construction of a plain-data syntax vec — the exact `[kind, scope_mark, flags, …payload]`
schema `syn_rt` uses. A `~unquote` hole evaluates its child (already a syntax value) and
splices it in. So a macro body compiled by the SVM codegen *builds* the same plain-data
a staged macro reads/writes.

| File | Role |
|---|---|
| `run_synquote_test.sh` | Structural test: `syntax-quote <template>` must emit one `jacl_vec_empty` per syntax node (a correct quasiquote fingerprint). All shapes pass; the differential gate confirms no regression to existing output. |

```sh
codegen/selfhost/macro_staging/run_synquote_test.sh   # syntax-quote -> plain-data vec
```

Hygiene note: `scope_mark` is currently baked from the template node (correct for a
top-level `syntax-quote`, scope_mark 0). A staged macro needs the *runtime* expansion
mark instead — the Phase 4 hygiene ABI.

## Phase 3 — macro-body module entry (codegen half)

`svm_codegen_macro_body(names, lens, nparams, body, …)` (in `codegen/codegen.c`) compiles
one macro body to a module with a single `__jacl_macro(sp, args…) → syntax-vec` function:
the params bind the macro's parameters, and the body lowers normally — `syntax-quote`
becomes vec construction, `~unquote` holes read the params (incoming syntax values). No
entry/scheduler; a host wrapper will provide `_start`, and `__jacl_macro` resolves by name
at link time (codegen output carries no named exports in its IR text — the func index is
returned for the link step).

| File | Role |
|---|---|
| `macro_body_test.c` + `run_macro_body_test.sh` | Compiles `twice {x} = [+ ~x ~x]` and checks the structure: exactly 2 `jacl_vec_empty` (the constant nodes command + `"+"`), while the two `~x` holes splice param `x` rather than build constant vecs. |

```sh
codegen/selfhost/macro_staging/run_macro_body_test.sh   # macro body -> __jacl_macro
```

## Phase 3 — end-to-end on real SVM

`run_svm_e2e.sh` compiles a runtime `syntax-quote` program with the SVM codegen and
**runs it on SVM** (`jacl_svm --interp`), asserting it builds the expected plain-data
syntax vec. Proven on the real engine:

```
print syntax-quote [+ 1 2]
  ->  [vec 0 0 0 1 [vec 3 0 0 "+"] [vec 1 0 0 1] [vec 1 0 0 2]]

print syntax-quote [if [== 1 2] {} { foo }]
  ->  [vec 0 0 0 26 [vec 3 0 0 "if"] [vec 0 0 0 10 [vec 3 0 0 "=="] …] [vec 5 0 0 0] [vec 5 0 0 0 …]]
```

Exactly the `syn_rt` schema — commands, nested commands, string heads, empty/non-empty
blocks (`5` = BLOCK), literals. So the codegen `syntax-quote` lowering + jaclrt vec
construction execute correctly on the real engine. (`run_svm_e2e.sh` is heavy — builds the
Rust harness + translates the runtime — and skips cleanly without cargo/gcc/clang.)

```sh
codegen/selfhost/macro_staging/run_svm_e2e.sh   # syntax-quote runs on real SVM
```

## Phase 3 — staged-macro wrapper + data flow

`macro_main.c` is the staged-macro module entry (arity-1): read the argument syntax value
on `stdin` (`synrt_decode`), call `__jacl_macro`, write the encoded result on `stdout`
(`synrt_encode`). `read(0)`/`write(1)` are POSIX natively and the on-ramp Stream caps on
SVM, so the one source serves both. `__jacl_macro` resolves by name at link time
(`svm_codegen_macro_body`'s export).

`run_wrapper_test.sh` exercises the whole marshalling round-trip natively with the real
codecs, for a `twice {x} = [+ ~x ~x]`-shaped macro:

```
gen_wire ARG (AST→wire) | macro_main (decode→__jacl_macro→encode) | wire_to_ast (wire→AST)
  twice 21       ->  [+ 21 21]
  twice [* 3 4]  ->  [+ [* 3 4] [* 3 4]]     (compound arg spliced twice)
  twice foo      ->  [+ [foo] [foo]]
```

Only `__jacl_macro` is a hand-written stand-in (`fake_twice.c`) mirroring the codegen'd
body — which is itself proven by `run_macro_body_test.sh` (IR) and `run_svm_e2e.sh` (real
SVM). So both halves of Phase 3 are validated; what remains is the **final link**: join
`macro_main` + a real codegen'd `__jacl_macro` + `jaclrt` + `syn_rt` into one SVM module
and run it on the engine (a Rust link driver, like `jacl_svm`, with an explicit
`__jacl_macro` export). Then: arity>1 (args frame), `~@` splicing, the hygiene ABI (Phase
4), the staging pipeline behind `JACL_STAGE_ON_SVM` (Phase 5), and dropping the legacy VM
(Phase 6).
