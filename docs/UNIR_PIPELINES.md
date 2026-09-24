# Pipelines of vats on Unir (`!a | !b`)

**Status:** design, 2026-09-24, for [theSherwood/unir#19](https://github.com/theSherwood/unir/issues/19),
the exit criterion of Unir stage 1b. It builds on `docs/UNIR_CHANNELS.md` (channels on edges within
one vat). The user-facing semantics are `SHELL_API_DESIGN.md`'s. This note maps them onto vats and
edges, and names the pieces TEMEN and unir still lack.

## What exists today

- `|` is argument threading only: `a | f x` compiles to `f a x` (`codegen.c` `compile_cmd_control_forms`).
  The lexer knows `|` and `||`, not `|!`, `|+` or `|&`.
- `!cmd` blocks: `jacl_exec_capture` runs a host subprocess through the `exec` capability and returns
  its stdout as a string.
- Channels run on Unir edges, but only within one vat.
- Nothing resolves a name to "spawn this vat", nothing lets a JACL program start as an op-13 child,
  and JACL closures cannot cross a vat boundary. Captures are heap pointers, and there is no value codec.

## Design

### A stage is a program value, not a closure

**Programs are capabilities, held as values** (decided 2026-09-24). A program is a child image, and
a vat holds program capabilities the way it holds any other. `!name args` is sugar for running the
program bound to `name` in the vat's program map, `$bin` (§13.1: "command lookup is a resolver
capability"). Attenuating is building a smaller map, and passing a program to a child is passing a
value, so nothing reaches a child except through its endowment. The top-level map comes from the host
(or the parent) at spawn. A resolver, when one exists, is only what fills the map, lazily or remotely,
and pinned to versions (unir §14); the surface does not change when it arrives. Path-shaped names
(`$bin` as a tree, for discoverability) are a later refinement of the same map.

`!name` falls back to the `exec` capability (a host subprocess) when `$bin` has no `name`, so existing
programs keep working.

Closures stay in-vat: `spawn { … }` keeps meaning a job in this vat. Running a *block* as a vat needs a
value codec for its captures; that is deferred, and nothing in #19 needs it.

The stage-pair rules are `SHELL_API_DESIGN.md`'s, with an edge as the pipe:

| left → right | wiring |
|---|---|
| `!a` → `!b` | one edge between the two child vats; the shell holds neither end |
| `!a` → JACL | an edge from the child into this vat, read as a channel |
| JACL → `!b` | a channel in this vat, feeding the child's stdin edge |
| JACL → JACL | value threading, as today |

### The program side: stdin, stdout, stderr, and channels passed as values

A program vat is endowed with three edges, `stdin`, `stdout` and `stderr` (each may be absent), plus the
parent's `stdout` Stream so `write` still binds. These are conventional names in its endowment, not
special cases.

- `[stdin]` returns the `stdin` read end as a channel (nil when there is none), so `read [stdin] N`
  works. `print` writes to `stdout` when the vat has it, and to the host `stdout` otherwise, so a
  program that prints is a pipeline stage with no changes. Errors and diagnostics go to `stderr`.
- At exit, the runtime completes `stdout` and `stderr` if the program returned normally. If it
  returned an error value, the runtime severs them with `io-error`, and the error travels down the
  pipeline as a sever (unir §10, default reaction Fail).
- The exit status is the join status. `SHELL_API_DESIGN.md`'s pipefail applies: a stage that severs,
  traps or returns an error makes the pipeline's value an error value naming that stage.

**stderr** gets its own edge per stage, not a substream of stdout. Credit is per edge, so a shared edge
would let a flood of diagnostics stall the data. By default the shell reads every stage's stderr,
forwards it to the pane labelled by stage, and keeps a bounded tail in the Job, which is what pipefail
reports. `|!` (stderr into the next stage) and `|+` (both, merged) are then wiring choices on the same
edges. A merge is the consumer reading several rings (unir §12.8).

**Channels are values too** (decided 2026-09-24). A channel end from `[channel]` can be passed to a
program as an argument, `!tee $w2`, and the child receives it as a channel value in its argument list,
with its region granted at spawn. An end has exactly one owner (unir invariant 5), so **passing an end
moves it**: the parent's copy is closed, and using it is an error value. Until temen#1707 is fixed, a
moved end's mapping stays in the parent's map area (see Later in `UNIR_CHANNELS.md`).

### The shell side: `jacl_pipeline`

Codegen lowers a `|` chain that contains a `!cmd` to one runtime call,
`jacl_pipeline(stages, first_input, want_value)`, where each stage is an argv vector. The runtime:

1. creates the edge regions (one per link, plus one stderr edge per stage), since the parent creates
   every region (see TEMEN below);
2. spawns each program vat with its argv, its edges, and any channel ends passed as arguments;
3. wires JACL ends as channels in this vat;
4. returns a **Job** (`SHELL_API_DESIGN.md` §Job: `exits`, `stdout` when the last stage is a program
   and the pipeline sits in value position, `duration`). Stage vats are joined when the Job is awaited.

### Terminals and job control

| shell action | on the edges | what the stages see |
|---|---|---|
| a stage exits normally | its `unir.stdout` completes | the next stage reads to end of stream and exits |
| a stage fails | its `unir.stdout` severs (`io-error`) | the next stage's read severs, so it fails too (pipefail) |
| `cancel $job` (kill) | the shell severs every end it holds with `cancelled` | each stage's next edge operation severs |
| `suspend $job` | the shell sets the credit limit to what it has already granted on the ends it reads (`set_credit_limit`) | the upstream stages fill their rings and park: backpressure, no signals |
| `resume $job` | the credit limit is lifted | they continue; no byte is lost or repeated |

Suspend and resume act on the ends the shell holds. For `!a | !b`, the shell reads `!b`'s output, so
suspending stops `!b` once its ring is full, and `!a` stops behind it. That is flow control as §13.1
defines job control. It needs `unir_consumer_set_credit_limit` in the C ABI.

## What TEMEN and unir need first

| gap | where | fix |
|---|---|---|
| A JACL image cannot be an op-13 child: `synth_manifest_start` only makes a paramless `_start`, and op 13 needs `(i64 starter) -> i64` | temen-ir | a child-entry flag on `synth_manifest_start`, mirroring temen-llvm's `TranslateOptions::child_entry` (one ignored i64, returns the entry's i64) |
| A child cannot bind `vm_region_create` (it is not `CHILD_BINDABLE`), and every JACL image imports it, so the spawn fails closed | temen-interp | add `vm_region_create` to `CHILD_BINDABLE`, bound to the child's own AddressSpace. Stages still never create regions (the parent does); the import only has to bind |
| No child-side vat, no named endowments, one grant per spawn, one child module | unir C ABI and `unir-temen` | `unir_vat_child(window)`; `unir_endowed(vat, name)`; `unir_spawn` taking a module name and a grant list (`TemenProgram` gains the module name); `unir_consumer_set_credit_limit` |
| A JACL image declares 64 MiB (`memory 26`), so each stage carve is at least 64 MiB of the parent's window | JACL runtime | for #19, the harness runs the shell with room for N carves above its image. Shrinking the image (and lifting the fixed 16 MiB heap) is jacl_impl#161 |
| Closed edges are never unmapped | unir binding | blocked on temen#1707 (a JIT unmap zeroes the region, which the peer still maps). Until then, grow the map area and map a channel's region once for both of its ends |

The two TEMEN changes are generic (Unir's invariant 1: nothing Unir-specific goes into TEMEN), so
they are an upstream PR, filed from this note.

## Tests

- **Programs as fixtures:** small JACL programs compiled to child images and granted in `$bin` as `gen`
  (prints N numbered lines), `upcase` (copies stdin to stdout, uppercased), `fail` (writes to stderr,
  then returns an error), `slow` (echoes stdin to stdout), and `tee` (copies stdin to stdout and to a
  channel passed as its argument).
- **End to end on TEMEN, on the interpreter and the JIT:**
  - `!gen 100 | !upcase` in value position: the output is every line, uppercased (Complete through
    two vats).
  - `!gen 100 | !fail | !upcase`: the pipeline is an error naming `fail` and carrying its stderr, and
    `upcase` saw a sever (Severed).
  - `!gen 10 | !tee $w | !upcase`, with `$r` read in this vat: both outputs are complete, and `$w` is
    closed in this vat after the call (the end moved).
  - `cancel` on a running `!gen 1000000 | !slow`: both stages end `Severed(cancelled)`, and the Job's
    exits say so.
  - `suspend` then `resume` on `!gen 10000 | !upcase`: while suspended, the byte count the shell has
    read stays fixed. After resuming, the output is complete and in order.
- The #18 channel tests, and every existing baseline, stay green.

## Order of work

1. TEMEN PR: child-entry `synth_manifest_start`, and `vm_region_create` in `CHILD_BINDABLE`. Then
   bump both repos' pins.
2. unir PR: the C ABI additions and `TemenProgram` with a module name, with tests in `e2e/temen`
   (a C root spawning two named programs).
3. `jacl_impl` PR: stage programs (`[stdin]`, `print` to `unir.stdout`, completion at exit), then
   `jacl_pipeline` and codegen for `|` chains with `!cmd`, then Job, suspend/resume/cancel, and the
   tests above.

## Out of scope for #19

- Blocks as vat stages (needs a value codec).
- The `|!`, `|+` and `|&` operators (the stderr edges exist; only the syntax and wiring choices wait).
- `create-process`'s explicit fd records.
- `&`/detached lifetime and GC-driven kill.
- Typed edges (unir stage 2).
