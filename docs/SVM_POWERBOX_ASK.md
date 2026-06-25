# Ask for svm — a frontend-independent powerbox instantiation layer

**Context.** svm already has the wasm-like pieces: named capability **imports**
(`resolve_imports(module, resolver)` → `cap.call`), named **exports** + cross-module
binding (`svm_ir::link`), and host grants (`Host::grant_stream`/`grant_host_fn`). svm-llvm
even delivers wasm-level ease to the **C** frontend — you write `write(...)` and the
plumbing is hidden.

**Problem.** The one piece that ease depends on — the **powerbox bootstrap** (the
synthesized `_start` that stashes granted handles at window offset 0, with the writable
page-0 stash, globals at `STACK_PAGE`, RO/writable page isolation, and the Memory mapped
region) — is **baked into svm-llvm's C→IR translation** (`synth_start`). A frontend that
emits SVM-IR directly and links it itself (e.g. JACL's codegen) can't reach it, and
reproducing the layout by hand faults (`MemoryFault`) because those layout invariants are
internal, not a public contract.

**Ask.** Expose the bootstrap **independently of the C frontend**, ideally both:

1. **Generalized `synth_start`** — a public step
   `(linked Module, entry funcidx, capability set) → Module` that prepends the powerbox
   `_start` and lays out the writable handle-stash / globals / memory exactly as the C path
   does. So any linked module gets the powerbox entry, not just clang output.

2. **A thin instantiation wrapper** — `instantiate(module, host) → instance` +
   `instance.call(export_name, args)` over the existing `resolve_imports` + grants + `link`
   + `run_with_host`/`compile_and_run_with_host`. (This is essentially what `run_c_full`
   already does — just decoupled from chibicc.) Keep the handle/object-capability model as
   the escape hatch; the wrapper is the easy default.

**Acceptance.** A **hand-written IR module** (no C, no svm-llvm) that imports `write` and
exports an entry can be instantiated with a granted stdout and run via the wrapper,
producing the expected bytes on stdout, **interp == jit** — i.e. the `run_c_full`
experience without a C frontend. Existing C-path behavior unchanged.
