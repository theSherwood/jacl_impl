# Pipelines of vats on Unir (`!a | !b`)

**Status:** partly built; design revised 2026-09-25 (values and outputs, job control, typed outputs; see
Order of work). For [theSherwood/unir#19](https://github.com/theSherwood/unir/issues/19), the exit
criterion of Unir stage 1b. It builds on `docs/UNIR_CHANNELS.md` (channels on edges within one vat).
The user-facing semantics are `SHELL_API_DESIGN.md`'s, with the Unir amendments it points to here.
This note maps them onto vats and edges, and names the pieces TEMEN and unir still lack.

## What exists today

- `!a | !b` of program stages runs as vats joined by edges, on all three TEMEN engines. Its value is
  the exit record, `{exit}` or `{exits}`, and its output goes to the enclosing output
  (`jacl_pipeline`); into a JACL stage, `!a | f` gives `f` the output as a channel read end
  (`jacl_pipeline_stream`), and `collect` reads a read end into a string. A failed stage fails the
  pipeline either way: the value, or the read that reaches the end, is the pipefail error. Inside a
  stage the enclosing output is the stage's `unir.stdout` (`jacl_out`), which nothing tests yet:
  a stage holds no programs to run.
- The exit record has no `duration` yet: TEMEN has a `Clock` capability, but the C frontend has no
  way to call it (`__vm_host_call` reaches only embedder `HostProc` capabilities).
- `|` between JACL values is argument threading: `a | f x` compiles to `f a x`. `!a | f` is the
  same call, with the chain compiled as a read end (`shell_cmd.stream`). JACL values into a program
  stage are not wired yet. The lexer knows `|` and `||`, not `|!`, `|+` or `|&`.
- A lone `!cmd` the vat holds no program for runs a host subprocess through the `exec` capability
  (`jacl_exec_capture`) and returns its stdout as a string.
- The parser marks `!cmd &` as background (`shell_cmd.background`); codegen ignores the mark.
- JACL tasks have no cancel, suspend or resume.
- Nothing carries a JACL value across a vat boundary: there is no value codec.

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

A program vat is endowed with three edges, `unir.stdin`, `unir.stdout` and `unir.stderr` (stdin is
absent for the first stage), plus the parent's host Stream as `stdout`, so its `write` import still
binds. These are conventional names in its endowment, not special cases, and they are not the bare
`stdin`/`stdout`, which TEMEN's stdio convention binds the `read`/`write` imports to. A vat endowed
with `unir.stdout` is a stage.

- `[stdin]`, `[stdout]` and `[stderr]` return those edges as channel ends (nil outside a stage), so
  `read [stdin] N` and `write [stdout] $bytes` work. `print` writes to `unir.stdout` when the vat has
  it, and to the host stream otherwise, so a program that prints is a pipeline stage with no changes.
  `[args]` is the argv it was spawned with (op 15's payload), a vector of strings.
- At exit, the runtime completes `stdout` if the program returned normally, and severs it with
  `io-error` if it returned an error value, so the error travels down the pipeline as a sever (unir
  §10, default reaction Fail). It writes that error's message to `stderr` and completes it either way:
  a sever would drop the message. It cancels `stdin`, so the stage before it stops.
- The exit status is the join status. `SHELL_API_DESIGN.md`'s pipefail applies: a stage that severs,
  traps or returns an error makes the pipeline's value an error value naming that stage.

**stderr** gets its own edge per stage, not a substream of stdout. Credit is per edge, so a shared edge
would let a flood of diagnostics stall the data. By default the shell reads every stage's stderr,
forwards it to the pane labelled by stage, and keeps a bounded tail per stage, which is what pipefail
reports. `|!` (stderr into the next stage) and `|+` (both, merged) are then wiring choices on the same
edges. A merge is the consumer reading several rings (unir §12.8).

**Channels are values too** (decided 2026-09-24). A channel end from `[channel]` can be passed to a
program as an argument, `!tee $w2`, and the child receives it as a channel value in its argument list,
with its region granted at spawn. An end has exactly one owner (unir invariant 5), so **passing an end
moves it**: the parent's copy is closed, and using it is an error value. Until temen#1707 is fixed, a
moved end's mapping stays in the parent's map area (see Later in `UNIR_CHANNELS.md`).

### A program has a value and an output (decided 2026-09-25)

A program stage has two results, and they are kept apart:

- **Its value.** `!prog args` evaluates to the program's value, and a pipeline to its last stage's
  value (`SHELL_API_DESIGN.md` §4). Until a value can cross a vat boundary (see "One codec"), a
  program's value is its **exit record**: `{exit, duration}` for one program, `{exits, duration}` for
  a pipeline, one exit per stage. A stage that fails, traps or severs makes the value the pipefail
  error instead: an error value naming that stage and carrying the end of its stderr
  (`SHELL_API_DESIGN.md` §9). Once
  values cross, a JACL program's value is what its `main` returns; a host program run through `exec`
  keeps the exit record.
- **Its output.** An edge of type `T` (see "Typed outputs"). It flows to the next program stage; into
  a JACL stage, which reads it as a channel (`!gen 3 | collect` is the output's elements, a string
  when `T` is text); or, with nothing after it, into the **enclosing output**. In the shell that is
  the host stream (later the pane); inside a stage it is the stage's own `unir.stdout`, copied
  there, because an edge has one writer and cannot be handed to the child.

Stage 1b's first cut returned the output as the value because nothing else could come back. That
fixed text as the result type of every pipeline, which is what the typed-stream design exists to
avoid, so the value and the output are separated before anything else builds on the stopgap.

### Who wires the output: the fiber that evaluates the pipeline

Evaluating a pipeline spawns its stages, joins them with edges, and then **wires the last output**:
into a JACL stage's channel, or copied into the enclosing output. The fiber doing that is whichever
one evaluates the expression, with no special drainer. So:

- `spawn {!a | !b}` wires the output inside the spawned task, and the pipeline runs to completion
  whether or not anyone awaits it.
- `!a | !b &` is **`spawn {!a | !b}`** (`SHELL_API_DESIGN.md` §5): the parser's background mark
  lowers to a spawn of the chain.

### Background and job control: the Future is the Job

The Future a spawn returns is the handle to the running pipeline. There is no separate Job object.

| operation | on a task running a pipeline | on any other task |
|---|---|---|
| `await $f` | the pipeline's value (the exit record), or the pipefail error | the task's value |
| `cancel $f` | severs every end the task holds with `cancelled`; each stage's next edge operation severs, and awaiting gives the `cancelled` error | the task ends with a `cancelled` error at its next safepoint |
| `suspend $f` | sets the credit limit on the ends the task reads to what it has already granted (`unir_consumer_suspend`): the stages fill their rings and park, by backpressure, with no signals | the task parks at its next safepoint |
| `resume $f` | lifts the limit; the stages continue, and no byte is lost or repeated | the task continues |

A stage that exits normally completes its output, and the next stage reads to the end and exits. A
stage that fails severs its output with `io-error`, so the next stage fails too (pipefail). These
are the stages' own behaviour (see the program side), not operations on the Future.

Suspending acts on the ends the task holds. For `!a | !b`, the task reads `!b`'s output, so
suspending stops `!b` once its ring is full, and `!a` stops behind it. That is flow control as unir
§13.1 defines job control.

### Typed outputs (unir stage 2; stage 1b is bytes)

Unir's streams are typed (unir §1, §3), and stage 1b's are all bytes, the floor. Nothing here depends
on the element type, so the design is written for a typed output and stage 1b implements `T = bytes`:

| | stage 1b | with types (unir stage 2) |
|---|---|---|
| a program's output | `unir.stdout`, bytes | an edge of type `T`, declared by the program and advertised at connect (unir §3) |
| `!a \| !b` | a byte ring | the same ring; the handshake checks that `b` accepts `a`'s `T`, edge by edge, before data flows |
| `!a \| collect` | frames as bytes, giving a string | frames as `T`: fixed-width heads read as JACL structs, zero-copy (unir §13.1); `collect` gives a vector of `T` |
| the enclosing output | bytes copied to the host stream | the pane is a typed sink, rendering by type rather than scraping text |
| `print`, `[stdout]`, `write` | the output edge | the bytes floor: text written to a bytes output |

So a program declares its output type (default bytes) where it declares its entry; stage 1b records
the slot and always says bytes. Channels stay frame-based (`docs/UNIR_CHANNELS.md`), so a typed frame
changes the payload, not the API, and `collect` is defined over `T` from the start.

### One codec: JACL values as meta-schema frames

Three things need a JACL value as a frame: a program's **value** coming back to its parent; **argument
values**, including capabilities and moved channel ends, where argv is strings today (unir §13.1:
passing `--gpu=$gpu` passes a capability); and **typed JACL-to-JACL outputs**. They get one codec, and
it is a subset of unir's meta-schema (unir §3) from the start: structs, sums, lists, text, the integer
ladder, floats and capability fields. That makes it the first slice of unir stage 2 rather than a
JACL-private format that stage 2 would have to replace (unir decision 50).

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
- **End to end on TEMEN, on the tree-walker, bytecode and Cranelift:**
  - `!gen 100 | !upcase | collect`: every line, uppercased (Complete through two vats into a JACL
    stage).
  - `!gen 100 | !upcase` in value position: the exit record, with both exits 0; the output went to
    the enclosing output. In statement position the same output passes through.
  - `!gen 100 | !fail | !upcase`: the pipeline's value is an error naming `fail` and carrying its
    stderr, and `upcase` saw a sever (Severed).
  - `!gen 10 | !tee $w | !upcase | collect`, with `$r` read in this vat: both outputs are complete,
    and `$w` is closed in this vat after the call (the end moved).
  - `!gen 1000000 | !slow &`, then `cancel`: both stages end `Severed(cancelled)`, and awaiting the
    Future gives the `cancelled` error.
  - `!gen 10000 | !upcase | tally $n &`, then `suspend` and `resume`, where `tally` is a JACL stage
    that adds the bytes it reads to the cell `$n`: while suspended, `$n` stays fixed; after resuming,
    the output is complete and in order.
  - `cancel`, `suspend` and `resume` on a task with no pipeline act at its safepoints.
- The #18 channel tests, and every existing baseline, stay green.

## Order of work

1. TEMEN (theSherwood/temen#1776, #1777): `synth_manifest_child_start`, `vm_region_create` in
   `CHILD_BINDABLE`, `__vm_instantiate_detached`, and an entry-less unit's globals kept clear of the
   powerbox argument area, where op 15's payload (a stage's argv) lands. **Done.**
2. unir: detached spawn (op 15) with programs as module capabilities, and the C ABI for it
   (`unir_vat_child`, `unir_vat_args`, `unir_endowed`, `unir_spawn` with grants,
   `unir_consumer_suspend`/`resume`), tested from a C root. **Done.**
3. `jacl_impl`: stage programs and `jacl_pipeline` with pipefail (`runtime/pipe_unir.c`, codegen for
   `!cmd` and `|` chains of them), tested by `codegen.rs::pipelines_run_on_temen` on the tree-walker,
   bytecode and Cranelift. Bytecode needed theSherwood/temen#1789 (per-domain fiber registries, so a
   module may both spawn and use fibers). Cranelift needed temen#1469 (a child task runs its own
   fibers and `thread.spawn` vCPUs); the test asserts its stages are JIT-compiled. **Done.**
4. Values and outputs, in this order: `!cmd → JACL` wiring (the last output read as a channel) and
   `collect`; the enclosing output (statement and value position, a stage's own output inside a
   stage); the exit record as a program's value. **Done**, but for `duration`. Then `&` as `spawn`;
   `cancel`, `suspend` and `resume` on Futures, carried out on edges for a task running a
   pipeline. The pipeline tests read the output with `| collect` and a JACL stage, and check what
   reaches the enclosing output.
5. Then: channel ends passed by move; JACL values feeding a first stage's stdin; `$bin` as a map
   value, which needs TEMEN to list a vat's capabilities by name. Until then `!name` resolves the
   capability `bin.<name>` in the vat's endowment, and a lone `!name` the vat does not hold runs
   through `exec` as before.
6. The value codec over unir's meta-schema (unir stage 2's first slice): programs' values, argument
   values, typed JACL-to-JACL outputs.

## Out of scope for #19

- Blocks as vat stages (needs the value codec, and a closure's captures in it).
- The `|!`, `|+` and `|&` operators (the stderr edges exist; only the syntax and wiring choices wait).
- `create-process`'s explicit fd records.
- `&`/detached lifetime and GC-driven kill. Likewise a pipeline's read end dropped before its end:
  its stages are not reaped until something reads it to the end or closes it.
- A pipeline's `duration`, until the C frontend can reach TEMEN's `Clock`.
- Output types other than bytes, and the value codec (unir stage 2; see "Typed outputs" and "One
  codec").
