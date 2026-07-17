# JACL capabilities design — filesystem first

How JACL programs get real capabilities (starting with the filesystem) while staying
portable across hosts that grant less — or nothing. The design goal, stated by the
project: **allow full capability when the host has it, and make it easy to bundle a
program to run on different platforms or with limited access to the fs or other
capabilities.** The mechanism generalizes beyond `fs`; the fs is the first worked
instance because the corpus already exercises it (`read-file` / `write-file` /
`append-file`) and the svm side is ready.

## What exists today (verified against the tree)

**JACL side.** `read-file`/`write-file`/`append-file` are served by a pure-guest
in-memory VFS (`runtime/io.c`: `jacl_vfs`, a JaclVal map `path → content`, GC-rooted,
fresh per run). Parent-directory existence is modelled (`/` and `/tmp` exist; a write
into any other directory errors like `open()`'s ENOENT). This is why the `io_*` corpus
passes with **zero** filesystem authority — the "filesystem" is guest memory.

**svm side** (as of the refreshed submodule) already implements the layered capability
story we need, so this design is mostly *adoption*, not invention:

- **`crates/svm-fs`** — the fs-capability **wire protocol** (ops 0–19: open/read/write/
  seek/close/remove/rename/truncate/sync, stat/mkdir/rmdir/opendir/readdir/closedir,
  mmap family) plus the deterministic **in-memory backend** (`mem_fs`), which is
  wasm-safe and can be **seeded from a shippable data image**
  (`read_host_dir` → `encode_image` → `decode_image` → `mem_fs_seeded`).
- **`crates/svm-run/src/fs.rs`** — the **real-filesystem backend** (`host_fs(root)`),
  *attenuated to a root directory*: the capability **is** the rooted directory.
  Both backends enforce the same path vetting (relative paths only; absolute, `..`,
  and empty are refused with `-EACCES`), so a program behaves identically on either.
- **Granting** — `Instance::run_with_caps(backend, config, &[("fs", cap)])` grants a
  named cap for one run and registers it in the §7 name directory. **No filesystem
  authority exists unless the embedder injects it.** Each grant invokes the backend's
  `make` builder, which **clones the seed fresh** — a run's writes never leak into the
  next run (this is exactly what the interp==jit differential needs).
- **Guest access from C** — `__vm_cap_resolve("fs", 2)` → handle (negative when not
  granted), `__vm_host_call(handle, op, a, b, c, d)` → i64. Verified to lower through
  our harness's exact clang flags to `cap.self.resolve` + `cap.call`; the pattern
  (probe once, cache, fall back on not-granted) is the same one svm's own
  `fs_probe.c` fixture and the sqlite/lmdb/postgres demos use.

**What svm-posix is — and why it is *not* our tool.** `crates/svm-posix` is a *POSIX
personality*: libc-as-a-capability for porting foreign C programs (BusyBox, Bash) that
expect `open`/`malloc`/`getcwd`/environment semantics. JACL owns its runtime; it does
not need libc emulation, it needs **narrow named caps**. Granting a whole POSIX surface
where only `fs` is wanted would widen the authority for nothing. We use `svm-fs`
directly and leave `svm-posix` for the day someone wants to run a foreign C tool
*inside* a JACL deployment.

## The design: three layers

```
  JACL program            read-file / write-file / append-file / (later: list-dir, …)
        │                 — one language surface, no platform or capability conditionals
        ▼
  runtime adapter         io.c: probe __vm_cap_resolve("fs") once at init
        │                   granted  → route builtins through cap ops (svm-fs protocol)
        │                   absent   → today's pure-guest VFS map (zero authority)
        ▼
  host policy             the embedder decides what "fs" means for this run:
                            nothing · mem_fs · mem_fs seeded from a data image ·
                            host_fs(some directory)
```

**Layer 1 — language surface (unchanged).** Programs are written once against the
builtins. Whether they run against a real disk, a bundled image, or guest memory is
*not visible in source*. This is the portability property: capability decides
authority, code never does.

**Layer 2 — runtime adapter (`runtime/io.c`).** At `jacl_heap_init` time (or lazily on
first file op), probe `__vm_cap_resolve("fs", 2)` and cache the handle:

- **Granted:** `read-file` → `open(O_READ)` + read-loop + `close`; `write-file` →
  `open(O_WRITE|O_CREATE|O_TRUNC)` + write + `close`; `append-file` →
  `open(O_WRITE|O_APPEND|O_CREATE)`. A negative return (−errno) maps to a JACL error
  value — the same try/catch-able error the VFS path produces today, so `# expect-error`
  oracles keep matching. No new language-visible failure modes.
- **Absent:** exactly today's code path. The guest VFS is not a stopgap to delete —
  it is the **zero-authority backend**, and it is what keeps a fully-denied or
  not-yet-wired platform working.

**Layer 3 — host policy.** The capability ladder the embedder picks from, per run:

| grant | authority | typical use |
|---|---|---|
| *(none)* | zero — guest VFS serves | locked-down deploys; platforms with no fs; today's behaviour |
| `mem_fs()` | in-memory, fresh per run | tests, differential runs (hermetic, parallel-safe) |
| `mem_fs_seeded(image)` | bundled assets, writes discarded per run | shipping an app with data files; browser/wasm |
| `host_fs(root)` | real disk, **confined to `root`** | dev CLI, servers — full capability, still can't name anything outside the granted directory |

The two ends of the ladder are the user's two requirements: `host_fs` is "full
capabilities"; `mem_fs_seeded` + the data image is "bundle up to run on different
platforms"; and the guest-VFS fallback is "limited access" degrading to none.

## The path model: JACL's `/` is the granted root

The corpus (and JACL users) write absolute paths — `/tmp/jacl_io_append.txt`. The
svm-fs protocol refuses absolute paths by design: the capability is the root, so paths
are relative to it. The adapter reconciles the two:

- JACL's path namespace is **rooted at the grant**: the adapter strips the leading
  `/` (and collapses duplicate separators) before the cap call, so JACL's
  `/tmp/x.txt` names `tmp/x.txt` *inside whatever root the host granted*.
- Consequence: a JACL program's `/` never means the host's `/`. On a dev box with
  `host_fs("/home/me/proj/data")`, the program's `/tmp/x.txt` is
  `/home/me/proj/data/tmp/x.txt`. Confinement comes free; programs stay portable
  because they never see host-specific prefixes.
- `..` and empty components are refused by *both* svm-fs backends (`-EACCES`); the
  adapter surfaces that as a JACL error rather than pre-vetting — one vetting
  implementation, host-side, backend-independent.
- Parent-directory semantics: the current VFS models "only `/` and `/tmp` exist".
  Under a cap grant the backend's own directory model governs (real ENOENT from
  `host_fs`; `mem_fs`'s prefix/mkdir model). For parity runs the harness seeds
  `dirs = ["tmp"]`, reproducing today's observable behaviour exactly.

## The differential oracle stays sound

The parity harness scores `run_diff` (interp == jit), which runs the program **twice**.
A filesystem is state; state leaking between legs would break the oracle. Two facts
keep it sound:

1. Every grant **clones the backend's seed fresh** (the `make` builder), so each leg
   sees a pristine filesystem — documented and implemented in svm-fs.
2. `run_diff` does not currently take extra named caps (`run_with_caps` does). Until
   upstream grows `run_diff_with_caps`, the harness can run its own two-leg
   differential — `run_with_caps(TreeWalk, …)` then `run_with_caps(Jit, …)`, comparing
   outcome + stdout — with identical semantics. (Worth a small upstream ask; not a
   blocker.)

## Bundling

A shippable JACL bundle is:

```
program module (SVM IR, linked against the runtime)
+ fs data image        (optional — encode_image of an asset tree; decode + mem_fs_seeded to mount)
+ capability manifest  (optional, informative — the names the program probes: "fs", later "exec", …)
```

The manifest is *advisory*, not authority: it lets a launcher show/ask what a program
wants, but the security model never depends on it — a program probing an ungranted name
just gets the fallback. Grants are per-platform launcher policy:

| platform | grant |
|---|---|
| dev CLI (`jacl run`) | `host_fs(--fs-root DIR)`, defaulting to a per-project dir; `--no-fs` for hermetic runs |
| test / parity harness | `mem_fs` seeded with `dirs=["tmp"]` (and, dual-mode, no grant at all — both must pass) |
| browser / wasm | `mem_fs_seeded` from the bundle's data image (svm-fs builds for wasm; no real fs exists) |
| locked-down deploy | nothing — guest VFS serves; program still runs |

## Generalizing to other capabilities

The pattern — **probe a name once, cache the handle, route the builtin through
`cap.call`, fall back to a zero-authority guest implementation (or a clean JACL error)
when absent** — is capability-agnostic:

- **`exec`** (would unblock `io_pipe_to_file` and `tour`'s shell section): a named cap
  whose ops are spawn/wait/read-output. The fallback is a JACL error ("shell-out not
  granted"), which is honest on platforms with no subprocess notion (browser). This
  needs an upstream powerbox capability design first — see the deliberate-divergences
  note in `SVM_PARITY_NOTES.md`.
- **`clock`, `net`, …**: same shape. `svm-run` already exposes `HostCap::clock()`.

Design rules that keep this coherent as caps accumulate:

1. **Default deny.** The fixed powerbox stays minimal (stdout/stdin/exit). Everything
   else is a named grant the embedder opts into per run.
2. **One probe point per cap** in the runtime, cached; builtins never probe per-call.
3. **Fallback must be semantically honest** — a working zero-authority implementation
   (fs → guest VFS) or a catchable error (exec) — never a silent no-op, so a program
   can detect and adapt (`try`/`catch`) if it cares.
4. **Vetting lives host-side** in the shared backend code, never duplicated per
   frontend — both backends speak identical semantics so differentials stay valid.

## Phasing

1. **Adapter** (`runtime/io.c` + a small `runtime/fscap.c`): probe + cap-routed
   read/write/append behind the granted handle; guest VFS untouched as fallback.
   Corpus-neutral by construction (harness grants nothing yet).
2. **Harness dual-mode**: grant seeded `mem_fs` in the parity harness's own two-leg
   differential; run the `io_*` cluster both granted and ungranted — both must score
   identically. This is the regression gate for the adapter.
3. **Bundle tooling**: `jacl bundle` builds program + `encode_image`'d asset dir;
   `jacl run` grows `--fs-root DIR` / `--fs-image FILE` / `--no-fs`.
4. **Surface growth** (as needed, each corpus-driven): `list-dir`/`stat`/`delete-file`
   builtins over ops 5/14/17–19; then the `exec` capability ask upstream.

## Non-goals

- Reproducing host-`/` semantics inside JACL (the rooted namespace is the point).
- svm-posix adoption (libc personality; wrong granularity for a runtime we own).
- mmap-backed buffers over files (`FS_MMAP`/`FS_MAP_REGION`) — powerful (zero-copy
  file-backed `[Buf]`s would compose with the flat-buffer work), but nothing in the
  corpus needs it yet; revisit when a use case lands.
