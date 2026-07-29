# Guest-JIT staging (and `interpret`) — scoping

Scopes the final Phase 6 work (`docs/SVM_MACRO_STAGING_PLAN.md` §6.4): letting the
JACL compiler **compiled to SVM** (`jacl_compiler.svmb`) expand macros without the
legacy VM, by codegen'ing each macro body to SVM IR and running it *in-guest* via
SVM's guest-JIT capability — and, per the same mechanism, re-hosting `interpret`.

This is a **scoping doc**, not an implementation. It records the target SVM API, the
concrete gaps on the JACL side, the design forks (especially for `interpret`), and a
work breakdown with the open decisions called out.

## 1. Why

Two things still pin `vm.c` into the toolchain (Phase 6.1–6.3 removed the rest):

- **Macro expansion in the `.svmb`.** The `.svmb` guest has no staging bridge (it
  can't link the native Rust `jacl_svm_stage` staticlib — it *is* guest code), so its
  `jacl_macro_stage_hook` is NULL and macros fall back to the legacy bytecode VM
  (`vm.c`, running confined inside the guest). `docs/SVM_SELFHOST_FEASIBILITY.md` §2
  confirms the emit path reaches `vm.c` *only* through macro expansion.
- **`interpret`** (`runtime/interpcap.c`) ships source to a host capability that runs
  a JACL evaluator on the host — today the legacy VM (via `parity.rs` →
  `emit_jacl --interp` → `jacl_eval`).

Both are the same shape — *compile JACL → SVM IR, then run it* — and guest-JIT is the
tool that lets the guest do the "run it" half itself, inverting `interpcap.c`'s stated
premise that "the guest is svm bytecode with no JACL compiler or VM inside it."

## 2. The target SVM API — the `Jit` capability (§22)

SVM exposes two nesting mechanisms; only one takes **guest-authored IR**:

| Mechanism | cap_id | Runs | Confinement |
|---|---|---|---|
| **`Jit`** (§22) | **11** | **guest-emitted, pre-encoded SVM IR bytes** | **same domain** — same window + powerbox, re-verified, *no* nested sandbox |
| `Instantiator` (§14) | 6 | the parent's own funcs, or a **host-granted** `Module` handle | nested, confined to a power-of-two sub-window |

`Instantiator` cannot accept guest-authored IR, so **`Jit` (cap 11) is the mechanism**
for "compile IR I just generated and run it."

**Guest ABI** (C intrinsics, the 8th powerbox cap; `frontend/chibicc/include/svm.h`):

- `long __vm_jit_compile(void *blob, long len)` → `(JIT, op 0)`: decode + verify +
  precondition-gate the blob, Cranelift-compile it, return a `JIT_CODE` handle (or
  `-errno`). Fail-closed.
- `long __vm_jit_compile_linked(void *ir, long ir_len, void *symtab, long symtab_len)`
  → `(JIT, 5)`: like op 0, but first binds the blob's unresolved §7 imports **by name**
  against a guest-supplied symbol table. **This is the one staging needs** — the macro
  module imports `jacl_vec_empty`, `synrt_read_arg`, `synrt_write_result`, `jacl_*`.
- `long __vm_jit_invoke2(long code, long a, long b)` → `(JIT, 1)`: run entry `funcs[0]`
  with a strict `(i64,i64)->(i64)` raw-slot ABI (arity-checked).
- `long __vm_jit_release(long code)` → `(JIT, 2)`. (`install`/`uninstall`, ops 3/4, add
  a `call_indirect` fast path — not needed initially.)

**Granting.** The `jit` cap is the 8th of the fixed §3e powerbox and is
**default-granted** to any powerbox program (`grant_powerbox_prefix`,
`svm-run/src/lib.rs`). No `RunConfig` toggle, no opt-in — running under the powerbox is
enough. An embedder building its own `Host` grants it explicitly via `grant_jit` /
`grant_jit_with_table` and must install `set_jit_validator` (the decode/verify gate).

**Security posture.** Guest-JIT is **not** a separate confinement — compiled units run
in the *same* window and powerbox as the caller. Safety comes from **re-verification**:
the blob passes the same `decode_module → verify_module` gate every module passes, so an
untrusted producer is safe (escape-freedom is re-checked; the masking lowering confines
its memory to the same window). A trap in JITed code is terminal for the whole domain.
Net (DESIGN.md §22): "adds no escape-TCB surface (authority-TCB only)."

**Reference implementations** to copy: `crates/svm-run/demos/jit/jit_demo.c` (hand-emit
`svm-encode` bytes → compile → invoke) and, closest to us,
`crates/svm-llvm/tests/fixtures/peval_futamura/` (build an `svm_ir::Module` in guest
memory, `svm_encode::encode_module`, `__vm_jit_compile`, `invoke`).

## 3. The gaps on the JACL side

Three are hard blockers; the rest are plumbing.

### 3a. Codegen emits svm-**text**; `Jit` wants svm-**encode binary** (blocker)

`irbuilder` only has `irb_to_text` (`codegen/irbuilder.h`); the header already
anticipates the fix — *"A binary (svm-encode) serializer can later sit behind the same
API."* The native bridge sidesteps this by parsing the text in Rust
(`svm_text::parse_module`). In-guest there is no text parser.

**Work item:** add `irb_to_encoded(const IrModule*, size_t *out_len)` — a C serializer
emitting the `svm-encode` binary format (LEB128 + one-byte opcodes; magic `SVM\0`,
version, memory descriptor, funcs/blocks/insts) that `__vm_jit_compile` decodes. This is
the single most reusable piece — it also unblocks a smaller/faster native path. Validate
it by round-tripping `irb_to_encoded` → `svm_encode::decode_module` == the text-parsed
module, for the whole corpus.

### 3b. `Jit` forbids **data segments**; `compile_synquote` uses them (blocker)

The `Jit` validator rejects any blob with data segments (single-unit, no `.data`). But
string literals in a syntax-quote (`[+ ~x ~x]`'s `+`) lower through
`compile_string_literal` → `irb_data_addr` → a `data ro` segment (the SelfData relocs
Phase 5 wired up). So a staged-macro module *as emitted today cannot be JIT-loaded*.

**Work item:** a data-segment-free lowering for string/data literals under the guest-JIT
path — materialize the bytes at runtime (immediate `i32.store`s into a scratch buffer, or
a const-overlay/region) instead of a `data ro` segment. Scope is small for the corpus
(short symbol names), but it is a real codegen branch. Alternatively, teach the `Jit`
precondition to accept read-only data segments that fit the window — an **SVM-side**
change; note it as an option, but the JACL-side lowering is self-contained and avoids a
vendor change.

### 3c. The staging runtime must live in the guest's domain (blocker for macros)

The macro module calls `jacl_vec_*` / `synrt_*` / `jacl_*`. Under `compile_linked` those
imports resolve **by name against a guest symbol table** — so the functions must already
exist as code+data in the *same window*. Today the native bridge translates the staging
runtime (`translate_runtime_staging`) as a separate Rust-linked module. The `.svmb` does
**not** currently link jaclrt at all (it's the *compiler*, not a runtime).

**Work item:** make the staging runtime (jaclrt + `syn_rt` + `stage_glue`) part of the
compiler guest's image (a companion translated blob mapped into the window, or linked
into the `.svmb`), and build the symtab `{name → guest address}` that `compile_linked`
consumes. This is the in-guest analogue of `link_with_manifest`, and the biggest single
chunk. Its heap (`jacl_heap_init`) coexists with the compiler's memory in one window —
tractable, but needs a memory-layout decision.

### 3d. Memory-match + ABI plumbing (plumbing)

The emitted module's `memory.size_log2` must equal the JACL program's own window (the
`Jit` memory-match precondition). Codegen already emits a memory descriptor; it must be
stamped to the host window (readable at runtime from the powerbox, as `peval_jit.rs`
passes `size_log2` via argv). The entry is `funcs[0]` with the raw i64 ABI —
`svm_codegen_staged_macro` already produces an arity-1 `(sp, resume)` shape; the
`synrt_read_arg`/`synrt_write_result` wire ABI (Phase 4a/5) carries args/results, so the
stdin/stdout wire is reused verbatim (the guest hands the arg wire to the runtime and
reads the result wire — no change to the codec).

## 4. `interpret` — the same mechanism, but an isolation fork

Macro staging runs during **compilation, inside the compiler** (trusted, and the compiler
has codegen). `interpret` runs **untrusted source at runtime, inside an arbitrary
program** — two differences that matter.

### 4a. Placement: arbitrary programs don't have a compiler

Only the compiler `.svmb` links `codegen.c`. A normal program that calls
`[interpret SRC]` does not — so "compile in-guest" would mean linking the whole JACL
compiler into every interpret-using program (large). The existing structure already
avoids this: `interpret` is a **granted capability**. So the natural shape is
**compiler-as-a-capability** — the host grants an `interp` cap whose implementation is
the SVM-native compiler + a runner, replacing today's host-legacy-VM backend. Programs
are unchanged and unbloated.

### 4b. Isolation: `Jit` is same-domain; today's `interpret` is a separate VM

This is the crux, and it is where **guest-JIT may be the wrong tool for `interpret`**.
Today the interpreted source runs in a *separate* host VM — isolated from the calling
program's memory. The `Jit` cap runs re-verified code in the **same window** — so
interpreted code cannot *escape* the sandbox, but it *can* read/write the calling
program's linear memory. For cooperative metacircular eval that is acceptable; for
running untrusted third-party source it is weaker isolation than today.

Three models, with the tradeoff explicit:

- **(A) Host cap, SVM-backed (recommended default).** Keep `interpret` a host cap, but
  swap its backend from the legacy VM to the SVM engine: the host compiles the source
  with the native compiler and runs it on a fresh `svm_run` instance (exactly the
  `jacl_svm` / `jacl_svm_stage` path). **Drops `vm.c` from `interpret`, preserves full
  isolation (separate instance), needs no guest-JIT and no per-program bloat.** The cost:
  it stays a host round-trip (a host dependency), not fully self-contained.
- **(B) Guest-JIT, same-domain.** The `interp` cap (or the program, if it links codegen)
  compiles in-guest and runs via `__vm_jit_compile`/`invoke`. Fully self-contained; but
  interpreted code shares the caller's window (isolation is escape-freedom only, not
  memory separation). Fine for trusted metacircular use.
- **(C) Guest-compile + `Instantiator`-confined.** Compile in-guest, then run the module
  in a confined §14 sub-window. Gives *both* self-contained and isolated — but
  `Instantiator` does **not** accept guest-authored IR today (only self-funcs or
  host-granted modules), so this needs an **SVM feature** (accept a re-verified
  guest-authored module into a carve). The strongest end state; the largest dependency.

**Recommendation:** use **guest-JIT for macro staging** (§3 — trusted, self-contained,
same-domain is fine) and **model (A) for `interpret`** initially (drops `vm.c`, keeps
isolation, smallest change), revisiting (C) if/when isolated self-contained interpret is
a requirement. This still retires `vm.c` from every path; it just doesn't force
interpret through guest-JIT where the isolation tradeoff is real. **Open decision for the
owner** — the user's steer was "interpret should probably use guest-JIT too," which is
model (B); worth confirming against the isolation cost before committing.

## 5. Work breakdown

1. **`irb_to_encoded` (svm-encode binary serializer in C)** — §3a. Standalone, testable
   by round-trip against `svm_encode::decode_module`. Unblocks everything and helps the
   native path too. *Prereq for all guest-JIT work.*
2. **Data-segment-free literal lowering** under a `staged_macro` codegen flag — §3b.
3. **Staging runtime in the guest image + symtab for `compile_linked`** — §3c. The big
   one; the in-guest `link_with_manifest`.
4. **Guest-JIT staging hook** — a `jacl_macro_stage_hook` variant that, instead of the
   native FFI, does: `irb_to_encoded(body module)` → `__vm_jit_compile_linked(blob,
   symtab)` → feed the arg wire / `__vm_jit_invoke2` → read the result wire → `release`.
   Compiled into the `.svmb`. Verify: `run_diff` against the frozen golden with the
   `.svmb` doing the expansion, no `vm.c`.
5. **`interpret` model (A)** — swap the host `interp` cap backend to the SVM engine;
   delete its `jacl_eval` dependency. (Independent of 1–4.)
6. **Drop `vm.c`** — once 4 (staging) and 5 (interpret) land, `src/jacl.c` stops needing
   `vm.c` / `bytecode.c` / the bytecode half of `compiler.c` on the emit path; remove them
   (behind the emit-only build), remove the now-orphaned `expand__compile_staged_body` /
   `jacl_ctx_run_closure` / `OP_SYNTAX_OP`, and verify the `.svmb` builds, shrinks
   (feasibility §2's ~33 dead externals go away), and the corpus still matches the golden.

Items 1–2 are self-contained and low-risk (start here). Item 3 carries the memory-layout
decision. Item 5 is independent and arguably the cleanest single win (isolated,
vm.c-free interpret) — it could land before the guest-JIT work.

## 6. Risks / open questions

- **Data segments (§3b).** Confirm no other codegen path the macro body can reach emits a
  data segment (float literals? interp-strings?). If the surface is larger than string
  literals, the JACL-side lowering grows, or the SVM-side "accept window-fitting ro data"
  option becomes more attractive.
- **Runtime image size in the guest (§3c).** Linking jaclrt into the compiler guest grows
  the `.svmb`; measure against the `vm.c` bytes it removes — net should be a shrink, but
  verify.
- **No per-unit code reclaim.** The `Jit` arena isn't reclaimed per unit; a compiler that
  stages thousands of macros must use whole-module compaction (`JitSession`/`compact`,
  DESIGN.md §22). Bound how many macro modules a single compile run JITs, or reuse one
  installed unit per macro *definition* rather than per *call*.
- **`interpret` isolation (§4b).** The owner decision: is memory isolation from the
  calling program required (→ model A or C), or is escape-freedom enough (→ model B)?
- **`compile_linked` symtab format.** Confirm the exact symtab wire the op expects
  (name-length-prefixed? address width?) against `jit_native_op` op 5 before building §3c.
