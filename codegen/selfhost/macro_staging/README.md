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

Next bricks: codegen lowering of `syntax-quote`/`~splice` into plain-data construction,
then the staging pipeline behind `JACL_STAGE_ON_SVM`.
