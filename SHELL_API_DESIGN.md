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
$r.stdout                   # captured output if capture was on
$r.stderr                   # same
$r.duration                 # how long it ran

def j {!cmd &}              # j is a running Job (its own Future)
$j.pid                      # alive process
def fin {await $j}          # fin is the same Job, now finished
```

To retain stdout on the Job for later inspection, attach a
`capture` stage:

```jacl
def r {!cmd | capture}                 # blocks; $r.stdout is the captured string
def s {!cmd | collect}                 # blocks; s is the string directly (no Job)
```

Without an attached stage, `$r.stdout` is `null` (bytes went to
terminal passthrough). See §7 for the full vocabulary of stages.

### 3. Uniform Job shape — running and finished have the same fields

A Job has the same field set regardless of state. Running fields
have `null` / pending values; finished fields are populated.

```
Job {
  pid:      <int or vec<int>>      # always present; vec for pipeline-Jobs
  pids:     <vec<int>>             # all member PIDs (for pipelines)
  exit:     <int or null>          # null while running, int after done
  exits:    <vec<int> or null>     # per-stage for pipelines (bash $PIPESTATUS)
  stdout:   <channel/buffer/null>  # depends on capture mode chosen at spawn
  stderr:   <channel/buffer/null>  # same
  result:   <any or null>          # see below — for pipelines ending in JACL
  duration: <duration or null>
}
```

Access semantics:

- `$j.exit` returns `null` while running, the exit code after done.
- `$j.stdout` is the **same kind of thing** at all times. If capture
  mode was `stream`, it's a channel (open while running, closed
  after). If `capture`, it's a buffer (growing while running,
  stable after). If `passthrough` or `discard`, it's null.
- `$j.result` carries the pipeline's final JACL value (see §4).

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

### 7. Stream configuration via pipeline stages

Stream destinations and modes are specified by composing pipeline
stages, not by a separate `with stdout=...` syntax. This is more
JACL-idiomatic — users reach for the same composition primitive
they use everywhere else — and avoids inventing a second
configuration surface.

| Goal | Form |
|---|---|
| Capture stdout into Job buffer | `!cmd \| capture` |
| Capture last N bytes (ring buffer) | `!cmd \| tail N` |
| Discard stdout silently | `!cmd \| discard` |
| Write stdout to file | `!cmd \| save-to "path"` |
| Write stdout to file AND pass through | `!cmd \| tee-to "path"` |
| Collect stdout as a single string | `!cmd \| collect` |
| Collect lines as a vec | `!cmd \| lines \| collect` |
| Consume stdout incrementally | `!cmd \| for line { ... }` |
| Capture stderr (only) | `!cmd \|! capture` |
| Discard stderr | `!cmd \|! discard` |
| Merge stderr into stdout, then process | `!cmd \|+ collect` |

**Default (no stage attached)** falls out of position-aware
behavior (§1):

- Statement position: stdout / stderr → terminal passthrough.
- Value position (`def r {!cmd}`): stdout / stderr → terminal
  passthrough. `$r.stdout` is `null`. To retain output on the
  Job, use `!cmd | capture`.
- Pipe position: stdout flows into the next stage; stderr → terminal.
- Backgrounded (`!cmd &`): same defaults as value position.

**No `capture` vs. `stream` mode flag.** The choice is implicit in
which stage you attach: `| capture` buffers into a stable string;
`| for line { ... }` (or `| to-channel $ch`) consumes incrementally.
They're different stages, not different modes of the same one.

**Stages that affect a Job:**

- `capture`, `tail`, `save-to`, `tee-to`, `discard` — terminating
  stdout-side stages that return the Job (with `.stdout` populated
  if `capture` / `tail`; otherwise `null`).
- `|! capture`, `|! discard`, `|! save-to`, etc. — stderr-side
  equivalents, populating `$j.stderr`.

**Stages that consume the Job:**

- `collect`, `for ... { ... }`, `lines | collect` — return a JACL
  value, *not* a Job. The pipeline value (§4) is whatever the stage
  produces.

For unusual configurations not expressible by pipeline stages — e.g.
fan-out to multiple consumers from one stdout, channel sharing
across more than two processes — drop to `proc-spawn` (§10).

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

### 10. Explicit low-level primitive — `proc-spawn`

The `|` macro covers 95% of cases. For hand-wired channel/fd
configurations — fan-out to multiple consumers, channel sharing
across more than two processes, custom shell-like constructs —
there is an explicit form:

```jacl
def {r w} {pipe}
def j1 {proc-spawn "cmd1" args {stdout: $w}}
def j2 {proc-spawn "cmd2" args {stdin: $r}}
```

Or single-call with a record of fd attachments:

```jacl
def j {proc-spawn "cmd" args {
  stdin:  $some-channel
  stdout: $other-channel
  stderr: file "errs.log"
}}
```

Both `|`-the-macro and `&` are implementable in terms of this. End
users typically don't write `proc-spawn`; it exists as the escape
hatch.

## Worked examples

### Direct style

```jacl
# Run and check exit code (just blocks; error short-circuits)
!my-tool --check

# Capture stdout — attach `capture` stage
def r {!ls -la | capture}
$r.stdout                                    # the listing as a string

# Get stdout as a string directly (no surrounding Job)
def listing {!ls -la | collect}

# Pipeline ending in JACL
def files {!find . -name "*.jacl" | lines | collect}
for $files f { print $f }

# Pipeline of externals (OS pipe, no JACL value between)
# Attach `capture` if you want the trailing count on the Job
def r {!grep ERROR access.log | !wc -l | capture}
$r.exit                                       # 0 if both succeeded
$r.stdout                                     # the count as bytes/string
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
def fut {!grep ERROR access.log | !wc -l | capture &}
do-other-work
def r {await $fut}
$r.stdout
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
processes), drop to `proc-spawn` (§10).

### Stderr handling

```jacl
# Capture stderr separately, stream stdout
def j {!cmd | for line { handle-line $line } |! capture &}
await $j
if {!= $j.exit 0} {
  print "failed: $j.stderr"
}

# Merge stderr into stdout pipeline
!cmd |+ for line { ... }

# Stderr to file while stdout flows normally
!cmd |! save-to "err.log" | for line { ... }
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

1. **Pipeline-stage vocabulary and defaults** for the §7 stage set.
   - Default cap for bare `| capture` (e.g. 64 KiB? 256 KiB?
     1 MiB?).
   - Overflow policy when the cap is hit — backpressure (block
     producer) is the leading candidate; lossy variants live in
     other stages (`tail`, or `capture overflow=drop-newest`).
   - Is `collect` (returns the value) a separate command from
     `capture` (returns a Job with `.stdout` populated), or are
     they unified with a context-sensitive return type? Probably
     separate — different return types should have different names.
   - Naming: `save-to` vs. `write-to-file` vs. overload `write`;
     `tee-to` vs. `tee`; etc.
2. **Name of the error-opt-out combinator.** `complete` (Nushell
   echo)? `try-exec`? `as-result`? Position-dependent — pipeline
   tail stage vs. wrapper around an expression.
3. **`cancel` escalation ladder for GC-driven cleanup.** SIGTERM →
   how long → SIGKILL. Whether explicit `cancel $j` uses the same
   ladder or stays "kill immediately."
4. **Per-call environment override syntax.** `with-env { !cmd }`
   block-form is the obvious answer (parallel to existing
   `with-env` / `with-dir`). Is a per-call form needed at all, or
   does block form cover the use cases?
5. **`proc-spawn`'s fd-record shape** — field names, accepted
   value types (channel, file path, file descriptor int,
   `discard` sentinel). Lowest priority; depends on the rest
   being settled.
6. **Stderr defaults for pipeline-Jobs.** Each process's stderr
   defaults to passthrough independently. If the user wants per-stage
   stderr capture in a multi-process pipeline, do we expose
   per-process `.stderrs` (vec) or only the last stage's? Probably
   per-process to preserve attribution.
7. **What `$j.pid` returns for a multi-stage pipeline-Job.** Process
   group leader, last process, or vec? `$j.pids` covers the vec
   case; the scalar `.pid` needs a convention (likely the last
   process, to match bash `$!`).
8. **Migration strategy from current `!cmd` semantics.** Today
   `def files {!ls -la}` binds a string; under the redesign it
   binds a Job (and you'd write `def files {!ls -la | collect}` for
   the string). Pre-1.0 affords a clean break, but existing tests
   and demos need updating.
9. **Interaction with `exec`.** Today's `exec "cmd" args` returns
   the full struct. Under the redesign, `def r {!cmd | capture}`
   returns a Job with all the same information. Does `exec` go
   away, or stay as the "low-level explicit form" parallel to
   `proc-spawn`?

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
fd polling, exposed via a `| capture` stage on a backgrounded
`!cmd`. That tests the channel-on-Job-stdout path end-to-end
without disturbing existing call sites.

Before that spike, the most blocking open question is **#1
(pipeline-stage vocabulary and defaults)** — it shapes both the
runtime data structures (cap size, overflow behavior) and the
public stage names that everything else composes with. "Bounded
with backpressure" remains the leading default; the producer-side
parking infrastructure that requires is also what `for line { ... }`
needs, so it's not extra work for `capture` specifically.
