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
> **SVM migration note:** this builds the playground on the **old
> backend** — `build_wasm.sh` compiles `src/jacl.c` (`embed.c` →
> `jacl_eval`, the in-tree compiler + VM). The new SVM backend
> (`codegen/` + `runtime/` + `vendor/svm`) has no wasm embedding yet, so
> the playground can't run on it. Retarget the wasm build here once an
> SVM→wasm path exists.

- Builds the WASM playground under `demo/`:
  1. `build_wasm.sh` compiles `src/jacl.c` to `build_wasm/jacl.{js,wasm}`
     via Emscripten.
  2. `demo/build_demo.sh` bundles the CodeMirror 6 editor with esbuild
     and regenerates `examples.json` from `test/jacl/*.jacl`.
  3. The real files are assembled into `_site/` (the `demo/wasm/*`
     symlinks don't survive artifact upload) and deployed to GitHub
     Pages.
