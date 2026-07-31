# GitHub workflow sources

This directory holds the **source of truth** for the project's GitHub
Actions workflows. GitHub only runs workflows that live in
`.github/workflows/`, so the files here have **no effect** until they are
copied into that directory.

## Why the indirection?

Some agents/contributors lack permission to commit directly to
`.github/workflows/` (GitHub treats workflow files as privileged). To let
those agents propose CI/Pages changes anyway, we edit them here — under a
plain, unprivileged path — and gate activation behind a deliberate,
user-driven copy step. Nothing here runs until a human (with the right
permissions) promotes it.

## Activating / updating the workflows

From the repository root, copy the sources into place and commit:

```sh
mkdir -p .github/workflows
cp .github/workflows_src/ci.yml    .github/workflows/ci.yml
cp .github/workflows_src/pages.yml .github/workflows/pages.yml
git add .github/workflows/ci.yml .github/workflows/pages.yml
git commit -m "Sync GitHub workflows from workflows_src"
```

Or, to copy everything except this README in one shot:

```sh
mkdir -p .github/workflows
find .github/workflows_src -maxdepth 1 -name '*.yml' -exec cp {} .github/workflows/ \;
```

After the copy lands on `main`, the workflows are live. Re-run the copy
whenever you change a file here.

### One-time setup for Pages

Before the first deploy, enable Pages in the repo:
**Settings → Pages → Build and deployment → Source = "GitHub Actions".**
The site then publishes to `https://theSherwood.github.io/jacl_impl/`.

## The workflows

### `ci.yml` — build & test

- Triggers on pushes to `main`, on pull requests, and manually
  (`workflow_dispatch`).
- Installs `gcc` + `libffi` and runs `./build.sh --parallel`, which
  compiles and runs the full C/JACL test suite. The run is wrapped in
  `timeout` because the chaos suite can livelock on rare seeds; a hang
  surfaces as a failed job.

The `ci.yml` job **`workflow-sync`** guards against drift: it fails if any
`*.yml` here differs from its promoted copy in `.github/workflows/` (or is
missing there). Because CI runs from the *promoted* copy, this is the
signal that a `workflows_src` edit is still awaiting promotion — when it
goes red, re-run the copy above and commit.

### `pages.yml` — deploy the playground

- Triggers on pushes to `main` and manually (`workflow_dispatch`).
- Builds the **SVM backend** — the playground's sole engine (the old Emscripten
  compiler + VM is no longer shipped to the playground):
  1. `demo/svm/build_assets.sh` builds the wasm-safe bytecode engine cdylib
     (`svm_browser.wasm`), the in-browser frontend (`jacl_emit.wasm`,
     `src/jacl_emit.c` + `codegen/*` → SVM IR), and the runtime blob
     (`jaclrt.svm`). Needs submodule checkout (`vendor/svm`), Rust +
     `wasm32-unknown-unknown`, and clang/LLVM-18 (svm-llvm bakes the runtime).
     The playground runs edited source entirely client-side: `jacl_emit.wasm` →
     `svm_link_run` (generic link vs `jaclrt.svm` + run), and runs unedited
     examples from precompiled `.svmb` blobs. See `demo/svm/README.md`.
  2. `demo/build_demo.sh` bundles the CodeMirror 6 editor with esbuild
     and regenerates `examples.json` from `test/jacl/*.jacl`.
  3. The real files are assembled into `_site/` and deployed to GitHub Pages.
