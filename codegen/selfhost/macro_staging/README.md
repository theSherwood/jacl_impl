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
