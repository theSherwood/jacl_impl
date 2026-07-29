# Known issues / flaky or broken CI

Track flaky and broken test gates here (per AGENTS.md) so they stay visible.

## `runtime/harness/tests/codegen.rs` — restored, with 6 pre-existing failures `#[ignore]`d

**Status:** the suite builds and runs again (was completely dark — it didn't compile against
the current `svm` API). **55 pass, 6 `#[ignore]`d.**

**What was fixed.** The harness used the pre-`IMPORTS.md`-phase-4 link/run model
(`svm_ir::link` + `synth_powerbox_start(linked, entry, 3, false)` + `instantiate`), which no
longer exists / no longer works: the translated runtime now keeps its `write` capability as a
**manifest import**. `run_case_full` was migrated to the model the shipping binaries use
(`bin/jacl_svm.rs`): `link_with_manifest` → export + `resolve_export` the program entry →
`synth_manifest_start` → `instantiate_with_imports` (bind `write`/`exit`/`stdin`) → `run_diff`
(preserving the interp == jit differential). Several stale test-case *sources* in
`codegen/tests/emit_jacl.c` that used removed syntax were also updated to current syntax:
the `else` keyword (`[if C {t} else {e}]` → `[if C {t} {e}]`, and `elif` → nested `if`) and a
proc named `count` that now collides with a builtin (renamed to `cnt`).

**The 6 still `#[ignore]`d are pre-existing codegen gaps/drift** (they fail identically on
`main` — the dark suite just never surfaced them), not regressions from the macro-staging work:

- `for_over_range_sum` / `for_over_range_command` / `for_over_dotdot_range` /
  `for_with_nested_if` — the `for NAME in COLL { body }` loop form no longer codegens
  ("for requires: …"); the `in`-keyword form drifted from what `compile_for` accepts. Needs a
  for-loop parser/codegen reconciliation.
- `arithmetic_variadic_fold` — `[+ 1 2 3 4]`; `+` is binary-only in this codegen (no variadic
  fold).
- `binding_same_scope_shadow_is_an_error` — top-level `def x 1; def x 2` no longer errors
  ("already defined in this scope") in this codegen.

Un-ignore each as its underlying gap is fixed. Meanwhile the codegen path also stays covered
by `codegen/selfhost/macro_staging/run_diff.sh` (legacy oracle) and `--svm` (staged), and by
the `jacl_svm` binary on the `test/jacl` corpus.
