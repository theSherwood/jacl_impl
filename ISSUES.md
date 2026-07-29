# Known issues / flaky or broken CI

Track flaky and broken test gates here (per AGENTS.md) so they stay visible.

## `runtime/harness/tests/codegen.rs` — stale vs the manifest-import link model (broken)

**Status:** broken, pre-existing (predates the macro-staging work).

The codegen differential suite (`cargo test -p jacl-runtime-harness --test codegen`) does not
build/run against the current `svm` API:

- It calls `svm_ir::synth_powerbox_start(linked, entry, 3, false)`, which no longer exists —
  it was renamed to `synth_manifest_start(module, entry, seed_heap)` (the `3` param is gone).
- After fixing that name, it still fails: it links with `svm_ir::link` and asserts
  `linked.imports.is_empty()`, but the translated runtime now keeps its `write` capability as a
  **manifest import** (resolved by `link_with_manifest` + bound at `instantiate_with_imports`,
  not by plain `link`). So every `run_case` panics at that assert with an unresolved `write`.

**Fix (not yet done):** migrate `run_case_full` to the manifest model the shipping binaries
already use (`runtime/harness/src/bin/jacl_svm.rs`): `link_with_manifest` →
`synth_manifest_start` → `instantiate_with_imports` (bind `write`/`read`/`exit`), and run on
interp + JIT preserving the differential (`run_diff`) check. Left as a follow-up because it is
a test-harness migration unrelated to the macro-staging slices.

Meanwhile the codegen path is covered differentially by
`codegen/selfhost/macro_staging/run_diff.sh` (legacy oracle) and `--svm` (staged), and by the
`jacl_svm` binary on the `test/jacl` corpus.
