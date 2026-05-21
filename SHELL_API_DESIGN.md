# Shell API — External Command Redesign

> **Status:** working design notes from 2026-05-20 conversation.
> Supersedes the Job/`await` semantics open questions in
> `CHANNELS_DESIGN.md`. Re-shapes parts of `SYNTAX.md` §"Shell interop"
> and §"Jobs" — those sections describe today's behavior; this doc
> describes the target.
>
> The model is settled in broad strokes; specific sub-decisions
> remain open and are flagged below.

## Motivation

The current external-command API has eight concrete holes (audited
against `vm.c`/`bytecode.c` and `SYNTAX.md` on 2026-05-20):

1. **No live stdin to a running process.** `string | !cmd` writes
   stdin to a tempfile first, then runs `cmd <tempfile`. Interactive
   processes (REPLs, `ffmpeg`, anything wanting line-by-line input)
   are unreachable.
2. **Background stdout is dead until exit.** `!cmd &` populates
   `_stdout_path` / `_stderr_path` tempfiles only at process
   termination. `!tail -f &` is useless.
3. **Stderr is second-class.** On success it goes to terminal; on
   failure it becomes the error message. Can't be *captured* as a
   stream while stdout flows in the pipeline.
4. **Foreground `!cmd` can't surface exit code without throwing.**
   Returns string on success, error on non-zero. `exec` gets both
   but is batch-only.
5. **No fan-out / tee** in pipelines.
6. **No per-stream redirection at call site.** Bash-equivalent of
   `cmd 2>file` requires dropping to `exec` and losing streaming.
7. **Pipeline exit-code propagation is unspecified** for multi-stage
   external pipes.
8. **Documented-but-fake streaming for stdin from streams**
   (`vm__exec_collect_stdin` buffers to tempfile first).

The redesign solves 1, 2, 3, 5, 6 directly; specifies 4 and 7;
fixes 8.

## Requirements

Established in discussion, in priority order:

1. **Direct-style `!cmd`** that blocks the calling coroutine until
   complete. Async work is written linearly; concurrency is opt-in
   via `&` / `spawn`.
2. **Shell-like passthrough by default** — `!cmd`'s stdout flows to
   the JACL process's stdout when nothing captures it. Visibility
   without ceremony.
3. **Capture and redirect to arbitrary destinations** — files,
   channels, JACL values.
4. **No tempfiles.** Implementation goal underwriting the deeper
   requirement: I/O must be incremental, not batch.
5. **Sane error handling** — non-zero exit becomes an error value
   that propagates through pipes (today's model preserved).
6. **Streaming, not just non-tempfile.** Long-running commands'
   output observable while they run. Both stdout and stdin.
7. **Backpressure / no unbounded buffering.** Slow readers slow
   producers; nothing in the middle silently buffers.
8. **Cleanup of orphaned processes.** Process lifetime tied to Job
   reachability via GC, with explicit `detached` opt-out for
   daemons.
9. **Per-call environment / cwd** — possible, not yet specified.
10. **Composability with `parallel` / `race` / `timeout`** — Jobs
    remain first-class Futures.

## Decisions

### 1. Position-aware default behavior (Option A)

A `!cmd` invocation's user-visible result depends on its syntactic
position. The compiler already classifies call sites
(`OP_EXEC` FULL / non-FULL / PIPE / BG flags); the redesign keeps
that machinery and chooses different defaults:

| Position | Behavior |
|---|---|
| Statement (return value discarded) | stdout, stderr → terminal (passthrough) |
| Value (`def r {!cmd}`, function arg) | blocks; returns a **finished Job** |
| Pipe (`!cmd \| ...`) | stdout streams via OS pipe / channel into next stage |
| Backgrounded (`!cmd &`) | returns a **running Job** (Future) |

This is bash-faithful for the statement / passthrough case. Today's
foreground `!cmd` in statement position silently captures-and-discards;
the redesign changes that to terminal passthrough.

### 2. The Job is the universal foreground value

`def r {!cmd}` returns a Job, **not** a string. This unifies
foreground and background under a single value type:

```jacl
def r {!cmd}                # blocks; r is a finished Job
$r.exit                     # exit code
$r.duration                 # how long it ran
$r.stdout                   # null unless a channel was attached via create-process (§10)

def j {!cmd &}              # j is a running Job (its own Future)
$j.pid                      # alive process
def fin {await $j}          # fin is the same Job, now finished
```

To get stdout as a value, terminate a pipeline with a JACL stage:

```jacl
def s   {!cmd | collect}              # blocks; s is the string directly (no Job)
def lns {!cmd | lines | collect}      # blocks; lns is a vec<string>
```

To hold stdout as a channel readable from another coroutine,
attach the channel via `create-process` (§10). See §7.

### 3. Uniform Job shape — running and finished have the same fields

A Job has the same field set regardless of state. Running fields
have `null` / pending values; finished fields are populated.

```
Job {
  pid:      <int or vec<int>>      # always present; vec for pipeline-Jobs
  pids:     <vec<int>>             # all member PIDs (for pipelines)
  exit:     <int or null>          # null while running, int after done
  exits:    <vec<int> or null>     # per-stage for pipelines (bash $PIPESTATUS)
  stdout:   <channel or null>      # the channel attached via create-process, if any
  stderr:   <channel or null>      # same
  duration: <duration or null>     # null while running
}
```

Access semantics:

- `$j.exit` returns `null` while running, the exit code after done.
- `$j.stdout` / `$j.stderr` reference the channel attached at spawn
  time via `create-process` (§10). They are `null` for Jobs created
  by pipeline syntax — pipeline-syntax invocations route stdout
  through the pipeline itself, not onto Job fields. A `null` `.stdout`
  doesn't mean "no output"; it means "no channel was attached to
  read it from after the fact."

### 4. Pipeline value = last stage's value

A pipeline expression evaluates to whatever its last stage produces.
It is **not** always a Job.

```jacl
def r   {!cmd}                       # Job (finished)
def lns {!cmd | lines | collect}     # vec<string>
def n   {!cmd | wc-l}                # int
def r   {!cmd | !grep "x"}           # Job (finished, for !grep)
```

For backgrounded pipelines, the Future resolves to that same
last-stage value:

```jacl
def f {!cmd | collect &}             # Future<vec<string>>
def v {await $f}                     # vec<string>
```

### 5. `&` is postfix sugar for `spawn { ... }`

`&` does not have a special "only on `!cmd` heads" rule. It is
postfix sugar: `expr &` ≡ `spawn { expr }`.

Under JACL's same-precedence left-associative operator rules
(`SYNTAX.md` §"Three syntax modes"), this means `&` attaches to
the whole preceding pipeline:

```jacl
!a | !b &       ≡   spawn {!a | !b}
!cmd | collect & ≡   spawn {!cmd | collect}
expensive $x &   ≡   spawn {expensive $x}
```

Both forms remain valid — `&` for compactness at the tail of an
expression, `spawn { ... }` when the expression is multi-line or
the user wants the form visible.

### 6. Pipe operator family

Four pipe-shaped operators:

| Op | Threads | Notes |
|---|---|---|
| `\|` | stdout bytes | Default. Between two `!cmd`s, uses an OS pipe directly. |
| `\|!` | stderr bytes | Stderr-only pipe. |
| `\|+` | merged stdout + stderr | Bash's `\|&` equivalent. |
| `\|&` | the Job value itself | For passing a Job through process-aware JACL commands. |

`|` is a **macro / smart operator** whose runtime mechanics depend
on what's on each side:

| Left | Right | `\|` mechanic |
|---|---|---|
| `!cmd` | `!cmd` | OS pipe between fds; no JACL value materializes between |
| `!cmd` | JACL stage | Channel from `!cmd` stdout into the JACL stage |
| JACL stage | `!cmd` | Channel from JACL stage into `!cmd` stdin |
| JACL stage | JACL stage | Regular value-pipe (today's behavior) |

This is a compile-time decision based on syntactic shape, not a
runtime dispatch.

### 7. Stream configuration is library / user code

Stream destinations are not part of the language design. Once `|`
delivers a stream of bytes (or strings, after `lines`, etc.) to the
right-hand side, the right-hand side is just a JACL command
consuming a stream. Anything users want to do with that stream —
materialize it (`collect`), write it to a file (`save-to`), discard
it, ring-buffer the last N bytes, fan it out — is a regular JACL
command, not a special pipeline-stage construct.

The default disposition for an unconsumed `!cmd`'s stdout / stderr
is position-aware (see §1): terminal passthrough in statement
position, channel-into-next-stage in pipe position. Anything else
(routing to a specific destination, retaining the bytes for later
inspection) is achieved by composing with whichever JACL command
the user reaches for, or by dropping to `create-process` (§10)
when a custom fd attachment is needed.

A Job's `.stdout` / `.stderr` fields are `null` unless a channel
was explicitly attached via `create-process` — pipeline-syntax
runs never populate them. To inspect output from a pipeline-syntax
invocation, terminate the pipeline with a stage that returns the
value you want (e.g. `| collect`), or attach a channel via
`create-process`.

### 8. Resource lifetime — GC + detached opt-out

Process lifetime is tied to Job reachability. When a Job becomes
unreachable, the runtime sends SIGTERM → grace period → SIGKILL,
then `waitpid`s to reap.

```jacl
def j {!cmd &}              # default: process dies when Job is GC'd
def j {!cmd & detached}     # opt-out: process survives Job collection (daemon case)
```

**Two non-negotiables** to avoid the standard GC-finalizer footguns:

1. **VM-shutdown sweep.** On normal or panic VM exit, walk all live
   Jobs and run cleanup. Handles "script ended, helpers running"
   without depending on GC timing.
2. **Idle-loop sweep.** The worker idle loop checks for collectible
   Jobs periodically (≤1s lag in the common case).

**Multi-threading caveat:** JACL is multi-threaded (NxM scheduler).
The cleanup path must:
- Have a single owner for `waitpid` (or use SIGCHLD-driven central
  reaper) — two threads racing on the same pid → `ECHILD`.
- Use mark-then-EOF for pipe-fd shutdown, so readers on other
  threads see clean EOF rather than syscall failure mid-read.
- Run idle-loop sweep on a designated thread or use atomic
  refcounts.

### 9. Error handling — non-zero exit propagates

Default: a non-zero exit raises an error value that propagates
through pipes and short-circuits the surrounding context.

```jacl
def r {!cmd}                # if !cmd exits non-zero, errors before binding
def lns {!cmd | collect}    # same — errors propagate
def fin {await $j}          # same — await of a non-zero Job errors
```

**Opt-out for inspection:** a `complete` form (name TBD) yields the
Job regardless of exit code:

```jacl
def r {!cmd | complete}             # never errors; check $r.exit
def fin {await $j | complete}        # same for backgrounded
```

**Pipeline error semantics:** a non-zero exit anywhere in the chain
short-circuits the whole pipeline. The pipeline's error value carries
the failing stage's stderr. (Bash `pipefail` equivalent, applied
always.)

### 10. Explicit low-level primitive — `create-process`

The `|` macro covers 95% of cases. For hand-wired channel/fd
configurations — fan-out to multiple consumers, channel sharing
across more than two processes, custom shell-like constructs —
there is an explicit form:

```jacl
def {r w} {pipe}
def j1 {create-process "cmd1" args {stdout: $w}}
def j2 {create-process "cmd2" args {stdin: $r}}
```

Or single-call with a record of fd attachments:

```jacl
def j {create-process "cmd" args {
  stdin:  $some-channel
  stdout: $other-channel
  stderr: file "errs.log"
}}
```

Both `|`-the-macro and `&` are implementable in terms of this. End
users typically don't write `create-process`; it exists as the escape
hatch.

## Worked examples

### Direct style

```jacl
# Run and check exit code (just blocks; error short-circuits)
!my-tool --check

# Capture stdout as a string (pipeline terminates in a JACL stage)
def listing {!ls -la | collect}

# Pipeline ending in JACL
def files {!find . -name "*.jacl" | lines | collect}
for $files f { print $f }

# Pipeline of externals (OS pipe, no JACL value between)
def count {!grep ERROR access.log | !wc -l | collect}
```

### Streaming and backgrounding

```jacl
# Stream a long-running process's stdout — consume it in the pipeline
!tail -f /var/log/app.log | for line {
  if {contains $line "ERROR"} { print $line }
}
# (blocks the coroutine; to background it, append &)

# Same, but backgrounded so other work can proceed
def fut {!tail -f /var/log/app.log | for line { handle $line } &}
do-other-work
cancel $fut                                   # tear down

# Backgrounded pipeline producing a value
def fut {!grep ERROR access.log | !wc -l | collect &}
do-other-work
def count {await $fut}
```

### Two-way I/O

```jacl
# Feed stdin from a JACL-side producer; read stdout in the pipeline.
# A channel is the bridge between the writer coroutine and the !cmd's stdin.
def {ch-w ch-r} {channel}

spawn {
  for $words w { write $ch-w $w }
  close $ch-w
}

# ch-r piped into !sort's stdin; stdout consumed by for-each
$ch-r | !sort -u | for unique { print $unique }
```

For configurations the pipeline form can't express (multiple
consumers of one stdout, shared channels across more than two
processes), drop to `create-process` (§10).

### Stderr handling

```jacl
# Stderr to file while stdout flows normally
!cmd |! save-to "err.log" | for line { ... }

# Merge stderr into stdout pipeline
!cmd |+ for line { ... }

# Stream stderr to a channel for inspection from another coroutine
def ch {channel}
def j {create-process "cmd" args {stderr: $ch}}
spawn {
  for $ch line { if {is-fatal $line} { signal $j SIGTERM } }
}
await $j
```

### Inspecting failure without erroring

```jacl
def r {!cmd-that-might-fail | complete}
if {!= $r.exit 0} {
  log "command failed with exit $r.exit: $r.stderr"
} else {
  process $r.stdout
}
```

### Composing with concurrency primitives

```jacl
# parallel returns a vec of N results; jobs/values mix freely
def {api db} {parallel {!start-api &} {!start-db &}}
$api.pid                                     # both are running Jobs
signal $db SIGTERM
timeout 30 { await $api }
```

## Open questions

1. **Name of the error-opt-out combinator.** `complete` (Nushell
   echo)? `try-exec`? `as-result`? Position-dependent — pipeline
   tail stage vs. wrapper around an expression.
2. **`cancel` escalation ladder for GC-driven cleanup.** SIGTERM →
   how long → SIGKILL. Whether explicit `cancel $j` uses the same
   ladder or stays "kill immediately."
3. **Per-call environment override syntax.** `with-env { !cmd }`
   block-form is the obvious answer (parallel to existing
   `with-env` / `with-dir`). Is a per-call form needed at all, or
   does block form cover the use cases?
4. **`create-process`'s fd-record shape** — field names, accepted
   value types (channel, file path, file descriptor int,
   `discard` sentinel). The actual primitive everything else
   lowers to, so this needs to be specified precisely.
5. **Stderr defaults for pipeline-Jobs.** Each process's stderr
   defaults to passthrough independently. If the user wants per-stage
   stderr capture in a multi-process pipeline, do we expose
   per-process `.stderrs` (vec) or only the last stage's? Probably
   per-process to preserve attribution.
6. **What `$j.pid` returns for a multi-stage pipeline-Job.** Process
   group leader, last process, or vec? `$j.pids` covers the vec
   case; the scalar `.pid` needs a convention (likely the last
   process, to match bash `$!`).
7. **Migration strategy from current `!cmd` semantics.** Today
   `def files {!ls -la}` binds a string; under the redesign it
   binds a Job (and you'd write `def files {!ls -la | collect}` for
   the string). Pre-1.0 affords a clean break, but existing tests
   and demos need updating.
8. **Interaction with `exec`.** Today's `exec "cmd" args` returns
   the full struct. Under the redesign, `def r {!cmd | complete}`
   covers the same case. Does `exec` go away, or stay as the
   "low-level explicit form" parallel to `create-process`?

## Cross-references

- `CHANNELS_DESIGN.md` — earlier sketch focused on the channels
  scheduling-machinery framing. This doc supersedes its Job/`await`
  decisions; the wake-queue / event-loop integration described
  there remains relevant to the implementation.
- `SYNTAX.md` §"Shell interop", §"Concurrency / Jobs", §"Three
  syntax modes" — defines current behavior and operator rules
  (left-assoc, same precedence).
- `NOT_IMPLEMENTED.md` §8b — worker idle loop, the natural home
  for the idle GC sweep and fd-readiness polling.
- `vm.c` `OP_EXEC` handling (lines ~10204–10610) — current
  tempfile-based BG implementation; the target for the
  pipe/channel rewrite.

## What to do next session

The model is settled enough that implementation could start on a
narrow spike: replace `EXEC_FLAG_BG`'s tempfile redirection
(`vm.c:10359-10384`) with `pipe2(O_NONBLOCK)` + worker-idle-loop
fd polling, exposed initially via a minimal `create-process` form
that attaches a channel to stdout. That tests the channel-attached-
to-Job-fd path end-to-end without disturbing existing pipeline
call sites.

The most blocking remaining open question is **#4
(`create-process`'s fd-record shape)** — it's the primitive that
everything else lowers to, so its surface needs to be settled
before the spike. The others (cancel ladder, error-opt-out name,
per-call env) shape ergonomics but don't gate the implementation
of the core path.
