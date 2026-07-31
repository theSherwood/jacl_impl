# Known issues / flaky or broken CI

Track flaky and broken test gates here (per AGENTS.md) so they stay visible.

## `runtime/harness/tests/codegen.rs` — restored, with 6 pre-existing failures `#[ignore]`d

**Status:** the suite builds and runs again (was completely dark — it didn't compile against
the current `svm` API). As of the item-6 migration it runs on the emit-only driver:
**96 pass, 2 `#[ignore]`d** (see the driver note below).

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

**Driver is now emit-only (item 6, slice 3a).** `codegen.rs`'s `driver()` builds
`emit_jacl.c` with `-DJACL_EMIT_ONLY` — the LLVM-free frontend the product ships
(`jacl_emit.wasm`), with **no** legacy `compiler_compile` static-error oracle. Accept/reject is
now exactly the SVM backend's own behavior (typer + codegen). This is a prerequisite for
deleting `compiler.c`/`vm.c` (slice 3c): the oracle path links against them. Effect on the
previously-ignored cases: `arithmetic_variadic_fold` is **un-ignored** (SVM codegen folds `+`
variadically; only the old binary-`+` oracle rejected it).

**2 remain `#[ignore]`d** — deliberate current-semantics gaps, not regressions:

- `for_over_dotdot_range` — the `A..B` dotdot form is not a valid **for-collection** in the
  current syntax: `[for 0..5 i { … }]` parses `0`, `..`, `5` as separate args and fails. Use
  `[for [range A B] binding { … }]` (as the other `for` tests now do). Reconcile if the
  dotdot-as-collection sugar is meant to return.
- `binding_same_scope_shadow_is_an_error` — SVM codegen deliberately permits top-level
  `def x 1; def x 2` as reassignment (`env_define`'s `module_scope` carve-out in `codegen.c`),
  matching the old VM's top-level semantics. With no oracle in the emit-only driver, this stays
  ignored unless top-level redef detection is added as a deliberate semantic change.

The `for`-iteration cases were reconciled: the loop syntax changed from `for NAME in COLLECTION`
to **`for COLLECTION binding { body }`** (collection first, no `in`), and `[range A B]` yields
i64, so the accumulator is typed `i64`. (Note: `codegen/codegen.c`'s `compile_for` still carries
a legacy `for NAME in [range …]` branch the reference compiler no longer accepts — dead via the
normal `emit_jacl` path, which validates through the reference first; left in place since
removing it would also orphan the `compile_for_range` integer fast-path.)

Un-ignore each remaining case as its gap is fixed. Meanwhile the codegen path also stays covered
by `codegen/selfhost/macro_staging/run_diff.sh` (legacy oracle) and `--svm` (staged), and by the
`jacl_svm` binary on the `test/jacl` corpus.

## irbuilder.rs — 3 link-model tests ignored (2026-07-29)

While landing the binary IR serializer (`irb_to_encoded`, guest-JIT staging item 1), the
`irbuilder.rs` suite was found **entirely red on the base commit** — an untracked pre-existing
break, not a regression. Two independent causes:

1. *svm-text canonical drift.* `irb_to_text` emitted `func (…)` with unindented blocks, but the
   vendored `svm_text::print_module` now emits `func <idx> (…)` with 2-space block / 4-space body
   indentation. Fixed `irb_to_text` to match; the 4 canonical-text tests (`add2`, `sum_to`,
   `call_addone`, `mem_roundtrip`) are green again.

2. *link-model drift.* The 3 tests that link against the translated runtime (`closure`, `gc`,
   `call_import_linked_against_runtime`) fail with `Unresolved("write")`: the runtime now carries
   a `write` **manifest capability import** that the old `svm_ir::link` + direct `interp/jit` path
   can't bind, and their entries take an explicit `sp` (and, for `call_import`, `a`/`b`) that the
   paramless powerbox `_start` can't supply. Reaching green needs the same migration `codegen.rs`
   already took — `link_with_manifest` → `synth_manifest_start` → `instantiate_with_imports` →
   `run_diff`. Marked `#[ignore]` with that pointer; this is runtime-on-SVM work (staging items
   5/6), not serialization. Un-ignore each as it moves to the powerbox model.

The new binary round-trip test (`encoded_binary_matches_text_module`) covers all 12 instruction
kinds and every terminator, gated at the `Module` level against the text path.

`runtime/harness/tests/link_artifact.rs::program_links_against_runtime_artifact_and_calls_jacl_add`
is the same drift family and was also found red on the base commit: its hardcoded `PROGRAM_IR`
uses the retired `block0(…):` label syntax, and the old `svm_ir::link` + direct interp/jit can't
bind the runtime's manifest `write` import. Marked `#[ignore]` with the same pointer; un-ignore
once it moves to canonical text + the powerbox/`run_diff` model.
