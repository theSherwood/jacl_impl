# Pipelines of vats on Unir (`!a | !b`)

**Status:** built through the value codec (2026-09-29: program values, argument values and typed
outputs, theSherwood/unir#43); design revised 2026-09-25 (values and outputs, job control, typed
outputs; see Order of work). For [theSherwood/unir#19](https://github.com/theSherwood/unir/issues/19), the exit
criterion of Unir stage 1b. It builds on `docs/UNIR_CHANNELS.md` (channels on edges within one vat).
The user-facing semantics are `SHELL_API_DESIGN.md`'s, with the Unir amendments it points to here.
This note maps them onto vats and edges, and names the pieces TEMEN and unir still lack.

## What exists today

- `!a | !b` of program stages runs as vats joined by edges, on all three TEMEN engines. Its value is
  the last program's value, and its output goes to the enclosing output (`jacl_pipeline`); into a
  JACL stage, `!a | f` gives `f` the output as a channel read end (`jacl_pipeline_stream`), and
  `collect` reads a read end into a string, or a vector when it carries values. A failed stage
  fails the pipeline either way: the value, or the read that reaches the end, is the pipefail
  error. Inside a stage the enclosing output is the stage's `unir.stdout` (`jacl_out`), which
  nothing tests yet: a stage holds no programs to run.
- Values cross vat boundaries through one codec (see "One codec"): a program's value comes back to
  its spawner, arguments arrive as values, and `emit` makes a program's output typed.
- `|` between JACL values is argument threading: `a | f x` compiles to `f a x`. `!a | f` is the
  same call, with the chain compiled as a read end (`shell_cmd.stream`). JACL values into a program
  stage are not wired yet. The lexer knows `|` and `||`, not `|!`, `|+` or `|&`.
- A lone `!cmd` the vat holds no program for runs a host subprocess through the `exec` capability
  (`jacl_exec_capture`) and returns its stdout as a string.
- `!a | !b &` is `spawn {!a | !b}`: its Future is the Job, and awaiting it gives the pipeline's value.
  `cancel`, `suspend` and `resume` act on any Future, as "Background and job control" describes.

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

- **Its value.** `!prog args` evaluates to the program's value, what its program returns, and a
  pipeline to its last stage's value (`SHELL_API_DESIGN.md` §4). A stage that fails makes the
  value the pipefail error instead: the first failed stage's own error value, which crosses like
  any value, or, for a stage that trapped or was severed and so returned none, an error value
  naming it and carrying the end of its stderr (`SHELL_API_DESIGN.md` §9). A host program run
  through `exec` gives its output as a string, as before. *(Until the codec, a program's value was
  its exit record, `{exit}` or `{exits}`; nothing reads exit statuses as values any more.)*
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
| `await $f` | the pipeline's value (its last program's), or the pipefail error | the task's value |
| `cancel $f` | severs every end the task holds with `cancelled`, kills and reaps the stages, and ends the task; awaiting gives the `cancelled` error | the task ends with the `cancelled` error at its next job-control point |
| `suspend $f` | sets the credit limit on the ends the task reads to what it has already granted (`unir_consumer_suspend`) and holds the task: the stages fill their rings and park, by backpressure, with no signals | the task is held at its next job-control point |
| `resume $f` | lifts the limit; the stages continue, and no byte is lost or repeated | the task continues |

Each returns whether it reached a live task: false for a finished one, or the program's own.

**Job-control points.** Tasks are cooperative, so a request takes effect where the task is not
running: while it is queued, parked on an `await`, or held. A running task acts on it at its next
job-control point: an `await`, each millisecond of a `sleep`, and each poll of a pipeline's output.
At those points its fiber holds nothing another task needs, so a cancelled task's fiber is simply
never resumed (`runtime/sched.c`). A task that computes without reaching one runs on until it does.

**How a stage learns.** A stage sees only its edges (unir §10: kill is `Severed(cancelled)`, with
no signal ladder). Once its stdout has ended, whether its reader cancelled it or the shell severed
it:

- a `write` to it is an error value;
- a `print` to it ends the task that printed, cleanly, as SIGPIPE ends a Unix program. The reader
  went away, which is not the stage's failure, so pipefail still names the stage that did fail;
- a read of its stdin fails (`unir_producer_poll`), so a stage that only reads ends too.

A stage that ends cancels its stdin, which ends the stage before it the same way. A stage that never
touches its stdio (a loop, a long sleep, a read of an idle upstream) learns nothing from its edges,
so `cancel` also kills every stage once it has severed (`unir_kill`, unir decision 73), and then
reaps them.

A stage that exits normally completes its output, and the next stage reads to the end and exits. A
stage that fails severs its output with `io-error`, so the next stage fails too (pipefail). These
are the stages' own behaviour (see the program side), not operations on the Future.

Suspending acts on the ends the task holds. For `!a | !b`, the task reads `!b`'s output, so
suspending stops `!b` once its ring is full, and `!a` stops behind it. That is flow control as unir
§13.1 defines job control.

### Typed outputs (unir stage 2; stage 1b is bytes)

Unir's streams are typed (unir §1, §3), and stage 1b's are all bytes, the floor. Nothing here depends
on the element type, so the design is written for a typed output; stage 1b implemented `T = bytes`,
and #43 adds `T = value`, JACL values (the codec's `Value`):

| | stage 1b | with types (unir stage 2) |
|---|---|---|
| a program's output | `unir.stdout`, bytes | an edge of type `T`, declared by the program and advertised at connect (unir §3) |
| `!a \| !b` | a byte ring | the same ring; the handshake checks that `b` accepts `a`'s `T`, edge by edge, before data flows |
| `!a \| collect` | frames as bytes, giving a string | frames as `T`: fixed-width heads read as JACL structs, zero-copy (unir §13.1); `collect` gives a vector of `T` |
| the enclosing output | bytes copied to the host stream | the pane is a typed sink, rendering by type rather than scraping text |
| `print`, `[stdout]`, `write` | the output edge | the bytes floor: text written to a bytes output |

**How a program's output gets its type (built, #43).** By its first write, not a declaration: the
first `emit V` offers the value schema on stdout (unir §3's in-band handshake: a `Hello` and the
schema on substream 0, answered in the ring header) and every message after it is one value; the
first `print` or `write` makes it bytes, as before. The reader learns which from the first frame:
an offer is accepted when its schema is the value schema (or one it succeeds), anything else is a
bytes edge. After that, `emit` on a bytes output and `write` on a value output are errors, and
`print` on a value output emits the printed text as one value. A declaration at the entry, as first
sketched, would let the shell check a pipeline before it runs; it waits for programs to carry
metadata the shell can read before spawning them.

On a value edge, `read R N` gives the next value (N is unused), `collect` a vector of them, and a
pipeline's output copied to the enclosing output is emitted there: printed, in the shell. Each
message is verified before it is decoded: the stage edges are untrusted, so nothing takes the
cast-in-place path yet. Channels made by `[channel]` stay bytes.

### One codec: JACL values as meta-schema frames

Three things need a JACL value as a frame: a program's **value** coming back to its parent; **argument
values**, including capabilities and moved channel ends, where argv is strings today (unir §13.1:
passing `--gpu=$gpu` passes a capability); and **typed JACL-to-JACL outputs**. They get one codec, and
it is a subset of unir's meta-schema (unir §3) from the start: structs, sums, lists, text, the integer
ladder, floats and capability fields. That makes it the first slice of unir stage 2 rather than a
JACL-private format that stage 2 would have to replace (unir decision 50).

**Built (theSherwood/unir#43).** The schema is `runtime/unir/value.usc`, compiled by unir-schemac to
`runtime/unir/jacl_value.h` (`runtime/unir/gen.sh`): a recursive sum over nil, bool, i32, i64, u32,
u64, f32, f64, text, bytes, vectors, maps (a list of key/value entries) and errors (the payload in a
one-element list: a sum cannot hold itself inline). Integers and floats keep their width, so a value
comes back as the type it left as. `runtime/value.c` encodes a JACL value canonically (unir §3) and
decodes only frames unir's verifier accepted (`unir_schema_verify`, or a typed edge's agreement),
copied first into this vat's memory. What cannot cross is an error value when encoded: closures,
futures, channel ends, atoms, structs and other objects with identity, bigints past 64 bits, and
tainted or secret values (the wire has no bits for the flags). Capability fields, and with them
channel ends passed by move, wait on theSherwood/unir#42.

| what | where it travels |
|---|---|
| a stage's arguments and environment | its spawn payload: the value `[argv env]`, at most 16,224 bytes |
| a program's value | its stderr edge, substream 2, after its text, in frames; at most 16 KiB |
| a typed output's messages | its stdout edge after the handshake, one value per frame (at most 1016 bytes) |

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

- **Programs as fixtures** (`runtime/harness/tests/pipelines/`): small JACL programs compiled to
  child images and granted as `bin.<name>`: `gen` (prints its arguments fifty times), `upcase`
  (copies stdin to stdout, uppercased), `fail` (returns an error without reading), `forever` (writes
  the alphabet until a write fails), `sink` (reads stdin to its end), `envdump` (prints what it sees
  of `$ctx` and `$env`), `value` (returns a value of every kind, its own arguments, an error, or
  one that cannot cross), `nums` (emits integers, or maps, as values) and `double` (reads values and
  emits them doubled). Still to come with channel ends passed by move: `tee` (copies stdin to
  stdout and to a channel passed as its argument).
- **End to end on TEMEN, on the tree-walker, bytecode and Cranelift:**
  - `!gen 100 | !upcase | collect`: every line, uppercased (Complete through two vats into a JACL
    stage).
  - `!gen 100 | !upcase` in value position: `upcase`'s value, 0; the output went to the enclosing
    output. In statement position the same output passes through.
  - `!gen 100 | !fail | !upcase`: the pipeline's value is `fail`'s own error, and `upcase` saw a
    sever (Severed).
  - `!value all`: a map holding an i32, an i64, a negative, an f64, a long string, a bool, nil, a
    nested vector and a map comes back equal; `!value err` an error carrying a map; `!value "atom"`
    the error saying the atom cannot cross; `!value "args" 42 $xs [map …]` its arguments, as the
    values passed.
  - `!nums 5 | collect` and `!nums 4 maps | collect`: vectors of the emitted values;
    `!nums 300 | !double | collect`: 300 doubled integers through a typed stage;
    `!nums 10 | sumvals`, a JACL stage reading values with `read`; `!nums 3 | !double` in value
    position: `double`'s value, with the values printed to the enclosing output.
  - `!gen 10 | !tee $w | !upcase | collect`, with `$r` read in this vat: both outputs are complete,
    and `$w` is closed in this vat after the call (the end moved).
  - `!gen bg | !upcase &`: awaiting the Future gives the pipeline's value, and the output reaches
    the enclosing output.
  - `!forever | !sink &`, then `cancel`, where `forever` writes until a write fails and `sink` only
    reads: both stages end and are reaped, and awaiting gives the `cancelled` error.
  - `!spin | !sink &`, then `cancel`, where `spin` loops without touching its stdio and `sink`
    waits on it: both are killed and reaped, and awaiting gives the `cancelled` error.
  - `spawn {!forever | !upcase | tally $n}`, then `suspend` and `resume`, where `tally` is a JACL
    stage that checks every byte is the next letter and keeps the count in the atom `$n`: while
    suspended, `$n` stays fixed; after resuming it grows, with nothing lost or repeated.
  - `cancel`, `suspend` and `resume` on a task with no pipeline act at its job-control points.
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
   stage); the exit record as a program's value; `&` as `spawn`; `cancel`, `suspend` and `resume`
   on Futures, carried out on edges for a task running a pipeline, with a kill for stages that
   never touch their edges (theSherwood/unir#35). **Done**, but for `duration`. The pipeline tests
   read the output with `| collect` and a JACL stage, check what reaches the enclosing output, and
   cancel, suspend and resume running pipelines.
5. Then: channel ends passed by move; JACL values feeding a first stage's stdin; `$bin` as a map
   value, which needs TEMEN to list a vat's capabilities by name. Until then `!name` resolves the
   capability `bin.<name>` in the vat's endowment, and a lone `!name` the vat does not hold runs
   through `exec` as before.
6. The value codec over unir's meta-schema (unir stage 2's first slice): programs' values, argument
   values, typed JACL-to-JACL outputs. **Done** (theSherwood/unir#43), but for capability fields
   (theSherwood/unir#42), a message larger than one frame, and a value larger than 16 KiB.

## Out of scope for #19

- Blocks as vat stages (needs the value codec, and a closure's captures in it).
- The `|!`, `|+` and `|&` operators (the stderr edges exist; only the syntax and wiring choices wait).
- `create-process`'s explicit fd records.
- `&`/detached lifetime and GC-driven kill. Likewise a pipeline's read end dropped before its end:
  its stages are not reaped until something reads it to the end or closes it.
- A pipeline's `duration`, until the C frontend can reach TEMEN's `Clock`.
- Output types other than bytes and JACL values; a typed message larger than one frame, and a
  program value larger than 16 KiB; the cast-in-place read of a trusted edge; reading a nil message
  apart from the end of a value stream (`read` gives nil for both; `collect` keeps them).
