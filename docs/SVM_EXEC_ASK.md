# Ask for svm — an `exec` (subprocess) capability

**Context.** The named-capability pattern is proven end to end for the filesystem:
`svm-fs` defines a wire protocol two interchangeable backends speak (deterministic
in-memory `mem_fs`, real `host_fs(root)`), the embedder injects it per run
(`run_with_caps(…, [("fs", cap)])`), a C-compiled runtime reaches it with
`__vm_cap_resolve("fs")` + `__vm_host_call`, and no authority exists un-granted. JACL's
runtime now consumes it (`docs/SVM_FS_DESIGN.md`) with a zero-authority guest fallback.
`svm-posix` deliberately stops short of `fork`/`exec` (POSIX.md §6, "still to come").

**Problem.** Shell-out has no capability at all. JACL's `!cmd args` / `[exec …]` needs to
spawn a host process, feed it stdin, and collect `{stdout, stderr, exit}` — the last two
reference-VM corpus cases the SVM backend cannot reach (`io_pipe_to_file`, `tour`'s shell
section) both reduce to exactly this. There is nothing to attenuate or emulate guest-side:
a subprocess is pure host authority, so without a grant the honest behaviour is a
catchable error — but *with* one there must be something to grant.

**Ask.** An `exec` capability in the svm-fs mold — a small op protocol plus two backends:

1. **Op protocol** (one-shot, matching the shape shell-out actually needs):
   `run(argv_ptr, argv_len, stdin_ptr, stdin_len) → handle` (argv as NUL-separated bytes),
   then `read_out(handle, buf, cap)` / `read_err(handle, buf, cap)` (chunked, 0 = EOF),
   `status(handle) → exit code`, `close(handle)`. Blocking one-shot semantics are enough —
   pipelines, incremental stdin, and signals can come later without breaking this surface.
2. **`host_exec(allowlist)`** — the real backend: spawn via the host, **attenuated by an
   explicit program allowlist** (the capability names *which* binaries may run, the way
   `host_fs`'s capability *is* the rooted directory; an empty list means "any" is a choice
   the embedder must spell out).
3. **`scripted_exec(table)`** — the deterministic backend: a `(argv-prefix → {stdout,
   stderr, exit})` table, no host processes. The `mem_fs` analog — what differential
   (interp == jit) tests and wasm/browser embedders grant, so a program using shell-out
   stays testable and portable.

**Acceptance.** A guest that resolves `"exec"` and runs `echo hi` sees `hi\n` + exit 0
under `host_exec(["echo"])`, byte-identical output under a `scripted_exec` seeded with the
same entry, **interp == jit** on both; un-granted, `__vm_cap_resolve` stays negative and
the guest's fallback runs (JACL: a catchable error value). A binary outside the allowlist
is a refused op (negative errno-style return), not a trap.
