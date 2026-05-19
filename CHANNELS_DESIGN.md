# Channels — Working Design Notes

> **Status:** exploratory. Started 2026-05-19 (post-`[Stream T]`-Phase-1).
> No code committed yet. This doc captures an in-progress design
> conversation so a future session can pick it up without re-deriving
> the framing. Decisions called out below as "open" remain open.

## The problem

JACL's I/O surface is scattered across mismatched shapes:

| Source | Today's shape | Concerns |
|---|---|---|
| `[lines $path]` | `[Stream str]` | pull-only |
| `[read-file $path]` | `str` | batch — slurps whole file |
| `!cmd` via `spawn` | Job (Future + pid + signal) | batch — captures stdout to tempfile, `await` reads it whole |
| Atom watchers | eager push (callback fires synchronously on `reset`) | scalar, not streamy |
| `Future` | one-shot resolve | not streamy |
| sockets, pipes | don't exist yet | — |

A user wanting to stream the stdout of a long-running process can't do
it today — the Job model waits for exit before exposing output. A user
wanting to react to filesystem readiness has no primitive. The same
event-loop infrastructure (worker idle poll, timer wakeups per
`NOT_IMPLEMENTED.md` §8b) is already present but not exposed.

## The Tcl insight (and where it stops applying)

In Tcl, sockets, files, pipes, processes, and stdin are all "channels"
under one API. The event loop, `fileevent`, and `vwait` cooperate with
coroutines: a coroutine `yield`s waiting for channel readiness, the
event loop wakes it when bytes arrive, the coroutine reads and
continues. The filesystem layer can virtualize new channel kinds
(zip-vfs, http-vfs) without changing user code.

What Tcl gets right: **one API surface for all I/O sources**, integrated
with cooperative scheduling. The "channel" name is about *the resource*,
not flow direction. A Tcl channel is *pull-on-data, push-on-readiness*.

What doesn't transfer: trying to also unify atoms, futures, and
generator streams under "channel" is the Java `InputStream`-as-everything
mistake. Those are genuinely different patterns:

| Pattern | Producer | Consumer | JACL today |
|---|---|---|---|
| Generator stream | drives via `yield` | drives via `stream-next` | `TYPE_STREAM` |
| Atom + watcher | drives via `reset` | reactive callback | atoms |
| One-shot completion | resolves once | multi-waiter | `JaclFuture` |
| Tcl channel | OS/peer pushes readiness | pull data after wake | (none) |
| Go channel | rendezvous send/recv | rendezvous send/recv | (none — debatable need) |

The unification is **scheduling machinery**, not type. One scheduler
(the worker idle loop) drains three wake queues: atom-deps dirty,
channel ready, future resolved. Different observer types share the
wake mechanism.

## Push-pull signals — what they are, and why they're not channels

The user spotted the structural parallel to alien-signals /
Solid-style fine-grained reactivity (push-pull signals). Walking
through the UI flow clarified the divergence:

**Signal model (Solid / Alien Signals / Vue 3):**
1. Setup: an `effect` runs, reading signals; the runtime builds the
   dep graph as a side effect of execution.
2. Write: `signal.set(v)` walks dependents and marks them dirty.
   *Eager push.* Synchronous.
3. Schedule: dirty *effects* are queued (microtask / animationFrame).
4. Flush: scheduler runs each dirty effect. If the effect reads a
   `computed`, that `computed` checks its own dirty bit and recomputes
   only if needed. *Lazy pull*, only at the computed-intermediate
   boundary.

So "push-pull" is **eager at effect leaves, lazy at intermediate
derivations**. The laziness only buys you something when you have
intermediate computeds whose recomputation can be short-circuited.

**Channel model:**
1. fd becomes readable → OS pushes one wake.
2. Parked coroutine resumes, pulls data.

There's no graph, no transitive dirty propagation, no laziness to win.
"Notify then satisfy" is structurally similar, but signals' subtlety
(graph traversal, conditional recomputation) doesn't apply.

**JACL impact:**
- Atoms could evolve toward push-pull *if and when* derived atoms
  ([[derived-atoms]] — not yet designed) ship. Without intermediates,
  eager `watch` is fine; the lazy bit only pays off with derivations.
- I/O channels are their own thing — Tcl-style, not signal-style.
- The shared infrastructure is the wake queue in the worker idle loop.

So: keep these as **two separate threads of work**, not one unification.

## Channels over Jobs — the concrete sketch

Today's Job (`spawn { !cmd }`) is batch: stdout captured to a tempfile,
`await` returns the whole string. With channels:

```jacl
def $j [spawn { !grep "pattern" }]

# Stream stdout line-by-line as it appears, not after exit
for line in [lines $j.stdout] {
  print $line
}

# Feed stdin from another source
for chunk in [lines "input.txt"] {
  write $j.stdin $chunk
}
close $j.stdin                # signals EOF to grep

def $code [await $j]          # await returns exit code, not stdout
```

**Mechanics:**
- Replace tempfile capture in `runtime.c` with `pipe2(O_NONBLOCK)` per
  fd. The channel handle wraps the read or write end.
- Worker idle loop already polls timers (§8b). Extend to poll registered
  fds via `kqueue` (BSD/mac) / `epoll` (Linux). On readability, wake
  the coroutine parked on `read $ch`.
- `[lines $ch]` and `[bytes $ch]` are generic adapters returning
  `[Stream str]` / `[Stream u8]`. The stream's `next_fn` reads from
  the channel and parks on channel-wake instead of pure-iteration.
- Backpressure falls out of OS semantics: writes block when the pipe
  fills, reads block when it drains. Block = coroutine suspend.

**Pipeline desugaring:**

```jacl
!cmd1 | !cmd2
```

rewrites to two `spawn`s with `cmd1.stdout` piped into `cmd2.stdin` —
no tempfiles, no waiting for cmd1 to finish, no intermediate buffering.
This is the real ergonomic prize.

## Open decisions (none made)

1. **`await` semantics change.** Today: `[await $j]` returns
   stdout-as-string. With channels: exit code only makes sense, but
   every existing call site breaks. Options:
   - Keep `await` = exit code; add `[capture $j]` for the
     "drain stdout to string, then await" idiom.
   - Both — overloaded by whether stdout was consumed elsewhere.
   - Hard migration (clean break, fix call sites).

2. **Unconsumed-output policy.** What if a Job's stdout has no reader?
   - Shell convention: silent drain to `/dev/null`.
   - Principled answer: "you must consume or detach" — runtime error
     if process blocks on a full pipe with no reader.
   - Today's batch model masks this entirely.

3. **Channel typing default.** `$j.stdout` is bytes underneath.
   User-facing default:
   - `[Channel u8]` raw, `[lines $ch]` as a typed adapter.
   - `[Channel str]` line-buffered with implicit UTF-8 decode.
   The bytes-as-default option is more honest about the wire format.

4. **`cancel` escalation** (cross-references `SYNTAX.md` Q19 and
   `NOT_IMPLEMENTED.md` §8b). Cooperative cancel could be
   close-stdin-then-grace-then-SIGTERM-then-SIGKILL. Decide the
   escalation ladder explicitly rather than today's
   "cancel == SIGTERM, no grace."

5. **Scope of channel primitive.** Tcl-style I/O channels only, or
   *also* Go-style queue channels for coroutine coordination? These
   are different problems (I/O integration vs. value handoff). JACL
   already has streams for pull and atoms for shared state; Go-style
   channels would be largely sugar over atoms+futures. Recommend:
   I/O channels only; revisit Go-style if a workload actually wants
   them.

6. **Migration strategy.** This is a big surface change touching
   `spawn`, `await`, `!cmd`, pipelines, and the worker idle loop
   simultaneously. Options:
   - Introduce `[spawn-streaming { !cmd }]` returning the new shape;
     migrate; eventually delete batch `spawn`.
   - Clean break — pre-1.0 affords this.

## Cross-cutting items

- **Atom watchers eager-vs-lazy** is the prerequisite design call
  *before* derived atoms can ship. Currently watchers are eager push
  ([[ATOM_WATCH_DESIGN]]); push-pull lazy would change that. Doesn't
  block channel work, but worth resolving before adding derivations.
- **Worker idle loop** (`NOT_IMPLEMENTED.md` §8b + AUDIT.md §11) is
  already the shared scheduling point. Channel-fd polling is one more
  wake source on the same loop. Park-timeout calculation already needs
  to account for next timer deadline; channel fds would join that
  calculation as a fixed `kqueue` / `epoll` wait.
- **Cross-thread channel access** is open (see §5 in `DESIGN.md`'s
  open questions). Channels are per-runtime by default; sharing across
  threads needs synchronization or moves.

## What to do next session

Pick the prerequisite question to resolve first — probably
**`await` migration strategy** (decision #1). That choice constrains
the rest: if `await` stays compatible via `capture`, the batch model
coexists during migration; if clean-break, the channel work can
restructure spawn entirely.

After that, a single concrete spike — a non-blocking pipe wired into
the worker idle loop for `spawn { !cmd }` stdout — would test whether
the infrastructure works before committing to the full surface.
