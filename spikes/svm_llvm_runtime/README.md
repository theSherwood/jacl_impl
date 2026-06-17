# Spike-1b — JACL runtime C through the svm-llvm on-ramp

**Status: PASS** (svm `vendor/svm` @ f75509a; needs `clang`, `cc`, and **`llvm-18-dev`**
installed for svm-llvm's libLLVM linkage).

Validates the single biggest effort assumption of the backend rewrite
(`docs/SVM_BACKEND_PLAN.md` §8): that JACL's existing runtime C can be **recompiled**
through svm's LLVM on-ramp rather than re-authored.

## (a) A real JACL collection compiles + runs — `cargo run` here

`src/main.rs` writes a tiny driver that `#include`s JACL's **actual
`lib/rrb_vec/rrb_vec.h`** (the RRB persistent vector, an include-as-template),
instantiated for `int`, with a self-contained bump arena for its allocator/GC hook.
It compiles the driver with stock `clang -O2 -emit-llvm`, translates the bitcode
with `svm_llvm::translate_bc_path`, verifies, and runs `run(n) = sum(0..n-1)` on
**interp + Cranelift JIT**, cross-checked against the closed form and native `cc`:

```
[2] svm-llvm translated it to SVM IR (6 funcs)
    imports: []
[3] verifier accepted the translated module
[4] run (interp == jit), cross-checked vs closed form n*(n-1)/2:
  PASS  run(16) = 120
  PASS  run(100) = 4950
  PASS  run(1000) = 499500
[5] native cc oracle: run(16) exit code = 120  (matches svm)
```

`run(1000)` exercises a multi-level RRB tree with ~1000 copy-on-write `push_back`s
— real pointer-chasing, struct-heavy, COW collection code — translated and run with
**no unresolved imports**. (`malloc`/window-growth was deliberately avoided via the
static arena; that path — `vm_map` / the Memory capability — is separately proven by
svm-llvm's own `vm_memory_map_and_page_size` test.)

## (b) The concurrency/GC intrinsic surface works

The `__vm_*` ops JACL's runtime needs are exercised by svm-llvm's own tests
(run from `vendor/svm/crates/svm-llvm`):

```
cargo test vm_      # 12/12 pass, incl:
  vm_gc_roots_smoke          (__vm_gc_roots -> gc.roots)
  vm_fibers_generator        (cont.new/resume/suspend)
  vm_atomics_single_threaded, vm_futex_wait_notify, vm_threads_atomic_counter
  vm_memory_map_and_page_size, vm_async_io_runtime
```

## Takeaway

The "port + recompile the runtime, don't re-author it" plan holds: real JACL
collection C goes through the on-ramp clean, and the concurrency/GC primitives the
runtime depends on lower correctly. The remaining runtime-port work is substrate
swaps (window allocator, conservative GC, fibers-for-threads), not rewriting the
algorithmic core.

## Reproduce

```
# one-time, for svm-llvm's libLLVM linkage:
apt-get install -y llvm-18-dev      # matches the container's clang/llvm 18
cd spikes/svm_llvm_runtime && cargo run
```
