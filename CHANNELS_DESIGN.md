# Channels — Infrastructure Layer Notes

> **Status:** working notes on the infrastructure that underpins
> shell I/O, file I/O, sockets, and other resource-backed
> streaming. Started 2026-05-19; reframed 2026-05-20 once the
> user-facing shell API decisions migrated to
> `SHELL_API_DESIGN.md`.
>
> **Layer:** below the user-facing operator surface. This doc
> covers what a *channel* is as a JACL value, how channel
> readiness integrates with the scheduler, and how it shares
> infrastructure with the existing timer/poll work. The user-
> visible shell API (`!cmd`, pipe operators, Job shape, error
> propagation) is specified in `SHELL_API_DESIGN.md` and is
> **not** redescribed here.
>
> What was "open decision #1" (await semantics) and "#2"
> (unconsumed output policy) — and most of the "channels over
> Jobs" sketch — have been resolved by SHELL_API_DESIGN.md
> decisions §1–§10. The remaining open infrastructure questions
> are flagged at the end.

## The problem

JACL has scheduling infrastructure (worker idle loop, timer
wakeups per `NOT_IMPLEMENTED.md` §8b) but no first-class concept
of a *resource that becomes ready over time*. Several user-
visible features need that primitive:

| Source | Today | Needs |
|---|---|---|
| `!cmd` background stdout/stderr | tempfiles, only populated at exit | live readable resource |
| `!cmd` stdin (post-spawn) | nonexistent — stdin batched at spawn via tempfile | live writable resource |
| File watchers / FS events | nonexistent | readiness-driven wake |
| Sockets, pipes | nonexistent | same |
| `stdin` of the JACL process | nonexistent (line-read only) | live readable resource |
| Async file reads | `[read-file]` slurps whole file | incremental reads with backpressure |

All of these want the same shape: *something pushes readiness;
a parked coroutine pulls bytes after wake.* That shape doesn't
exist in JACL yet. Building it once and reusing it for all of
these is the win.

## The Tcl insight (and where it stops)

In Tcl, sockets, files, pipes, processes, and stdin are all
"channels" under one API. The event loop, `fileevent`, and
`vwait` cooperate with coroutines: a coroutine `yield`s waiting
for channel readiness, the event loop wakes it when bytes
arrive, the coroutine reads and continues. The filesystem layer
can virtualize new channel kinds (zip-vfs, http-vfs) without
changing user code.

What Tcl gets right: **one API surface for all I/O sources**,
integrated with cooperative scheduling. The "channel" name is
about *the resource*, not flow direction. A Tcl channel is
*pull-on-data, push-on-readiness*.

What doesn't transfer: trying to also unify atoms, futures, and
generator streams under "channel" is the Java
`InputStream`-as-everything mistake. Those are genuinely
different patterns:

| Pattern | Producer | Consumer | JACL today |
|---|---|---|---|
| Generator stream | drives via `yield` | drives via `stream-next` | `TYPE_STREAM` |
| Atom + watcher | drives via `reset` | reactive callback | atoms |
| One-shot completion | resolves once | multi-waiter | `JaclFuture` |
| Tcl channel | OS / peer pushes readiness | pull data after wake | (none) |
| Go channel | rendezvous send / recv | rendezvous send / recv | (none — debatable need) |

The unification is **scheduling machinery**, not type. One
scheduler (the worker idle loop) drains three wake queues:
atom-deps dirty, channel ready, future resolved. Different
observer types share the wake mechanism.

## Push-pull signals — separate concern, not channels

The structural parallel to alien-signals / Solid-style
fine-grained reactivity is tempting. Walking through the UI
flow clarifies the divergence:

**Signal model (Solid / Alien Signals / Vue 3):**
1. Setup: an `effect` runs, reading signals; the runtime builds
   the dep graph as a side effect of execution.
2. Write: `signal.set(v)` walks dependents and marks them
   dirty. *Eager push.* Synchronous.
3. Schedule: dirty *effects* are queued (microtask /
   animationFrame).
4. Flush: scheduler runs each dirty effect. If the effect reads
   a `computed`, that `computed` checks its own dirty bit and
   recomputes only if needed. *Lazy pull*, only at the
   computed-intermediate boundary.

So "push-pull" is **eager at effect leaves, lazy at intermediate
derivations**. The laziness only buys you something when you
have intermediate computeds whose recomputation can be
short-circuited.

**Channel model:**
1. fd becomes readable → OS pushes one wake.
2. Parked coroutine resumes, pulls data.

There's no graph, no transitive dirty propagation, no laziness
to win. "Notify then satisfy" is structurally similar, but
signals' subtlety (graph traversal, conditional recomputation)
doesn't apply.

**JACL impact:**
- Atoms could evolve toward push-pull *if and when* derived
  atoms ship (not yet designed; see `ATOM_WATCH_DESIGN.md`).
  Without intermediates, eager `watch` is fine; the lazy bit
  only pays off with derivations.
- I/O channels are their own thing — Tcl-style, not
  signal-style.
- The shared infrastructure is the wake queue in the worker
  idle loop.

Keep these as **two separate threads of work**, not one
unification.

## What a channel is as a JACL value

A channel is a JACL-level handle to a resource that produces
or consumes bytes (or items) asynchronously. Concrete shape
(working sketch — exact fields TBD as part of the
implementation spike):

```
Channel {
  kind:         <read | write | bidirectional>
  state:        <open | half-closed | closed>
  fd:           <int or null>          # underlying OS fd, if any
  buffer:       <bounded ring>         # bytes in flight
  parked:       <coroutine or null>    # who's blocked on readiness
  ...
}
```

Operations:
- `read $ch N` — read up to N bytes; parks the coroutine if
  none available; returns bytes (possibly fewer than N) or EOF
  sentinel.
- `write $ch bytes` — write bytes; parks if buffer is full.
- `close $ch` — half-close (write-side) or full close.
- `[lines $ch]`, `[bytes $ch]` — adapters returning `[Stream str]`
  / `[Stream u8]` whose `next_fn` reads from the channel and
  parks on channel-wake.

**Default typing: bytes.** A channel sourced from an fd or
process pipe carries `u8`. String streams are an explicit
decode via `lines` / `chars` / `read-utf8` / whatever the
adapter is called. Don't bake UTF-8 into the primitive — many
fds (binary file I/O, sockets carrying non-text protocols) are
honestly bytes.

**Bounded buffering with backpressure.** Channels have a
bounded internal buffer. When full, writes park the producer;
when empty, reads park the consumer. The buffer cap is a
construction parameter, not a global default — different
channel kinds have different natural caps (e.g. a Linux pipe
fd is already capped at 64 KiB by the kernel; an in-memory
JACL-to-JACL channel might default to a smaller number).

**No memory-only buffering by default.** A channel that's *not*
backed by an fd (e.g. a pure JACL-to-JACL coordination channel)
still uses bounded buffering with backpressure — same shape.
That said, whether JACL needs pure JACL-to-JACL channels at
all is open (see below).

## Worker idle loop integration

The worker idle loop (`NOT_IMPLEMENTED.md` §8b, `AUDIT.md` §11)
is already the shared scheduling point. Channel-fd polling is
one more wake source on the same loop:

- Park-timeout calculation already needs to account for the
  next timer deadline.
- Channel fds join that calculation as a `kqueue` / `epoll`
  wait with the same deadline.
- On readability, wake the coroutine parked on `read $ch` (or
  on `write $ch` if waiting on space).
- Same loop also drives the `create-process` cleanup-sweep
  cadence (see SHELL_API_DESIGN.md §8) — one scheduler, multiple
  wake sources.

Implementation order matches priority:
1. Add fd registration + kqueue/epoll wait to the idle loop.
2. Channel primitive + `read` / `write` / `close` ops.
3. `create-process` to bind channels to OS fds (the user-facing
   surface; specified in SHELL_API_DESIGN.md §10).
4. Pipe operator macro emits channel-attached `create-process`
   calls for the `!cmd | jacl-stage` and `jacl-stage | !cmd`
   cases.

## Cross-thread channel access

JACL runs on a multi-threaded NxM scheduler. A channel created
on one worker may be read or written from a coroutine that ends
up scheduled on another worker. Practical consequences for the
implementation:

- **fd ownership and `epoll` registration** — must be
  thread-safe or single-owner; can't have two workers race on
  the same fd-registration table.
- **Park / wake** across threads — a wake from the kqueue/epoll
  thread has to enqueue the coroutine onto whatever worker
  scheduler picks it up. Same problem as future-resolves-on-
  thread-A-coroutine-was-parked-from-thread-B; presumably the
  existing scheduler already handles this for futures.
- **Buffer access** — concurrent read/write on the bounded
  buffer requires synchronization. Channels are *not* atoms;
  they don't need transactional semantics, but they do need
  basic mutual exclusion on the buffer.
- **Sharing the same channel-value across threads** is allowed
  by default. (Channels are not coroutine-local; sending one to
  another coroutine just shares the reference.) The `DESIGN.md`
  open question about cross-thread value sharing in general
  applies here too.

## Open decisions (infrastructure layer)

1. **Pure JACL-to-JACL coordination channels (Go-style) — in
   scope or not?**
   I/O channels (backed by an fd or process pipe) are
   load-bearing. Channels for coroutine-to-coroutine value
   handoff *without* an fd are different — they overlap with
   streams (pull) and atoms (shared state). Recommendation:
   ship I/O channels first; revisit Go-style coordination
   channels only if a real workload wants them. The bidirectional
   I/O case in `SHELL_API_DESIGN.md` §worked-examples already
   uses `(channel)` as a JACL-side bridge — so some form of
   pure JACL channel may end up needed anyway. Status: open.

2. **Cancel escalation ladder.** Same question as
   `SHELL_API_DESIGN.md` open #2 — SIGTERM → grace period →
   SIGKILL. The infrastructure question is what the close-fd
   side looks like when cleanup is escalating: do we
   half-close-stdin first to signal EOF, wait for graceful
   exit, then escalate to signals? Or is fd close orthogonal
   from signal escalation? Status: open.

3. **Cross-thread channel access details** — the §"Cross-thread
   channel access" sketch above isn't a complete design. The
   exact synchronization primitives (lock-per-channel vs.
   lock-free ring) need a closer look. Status: open.

4. **Channel typing.** Bytes-as-default is the leaning, with
   `lines` etc. as explicit decode adapters. SHELL_API_DESIGN.md
   doesn't address this directly; worth confirming the same
   choice in both layers. Status: weakly settled (bytes), worth
   explicit confirmation.

## Cross-cutting items (forward-looking)

- **Atom watchers eager-vs-lazy** is a prerequisite design call
  *before* derived atoms can ship. Currently watchers are
  eager push (`ATOM_WATCH_DESIGN.md`); push-pull lazy would
  change that. Doesn't block channel work, but worth resolving
  before adding derivations.
- **Filesystem virtualization** (zip-vfs, http-vfs as channel
  sources) is a Tcl-style win this primitive enables but
  doesn't require. Out of scope for the initial implementation.

## Cross-references

- `SHELL_API_DESIGN.md` — user-facing shell interop API,
  built *on top of* the primitives here. Authoritative for
  `!cmd` semantics, Job shape, pipe operators, error
  propagation, and `create-process`. Cross-reference here when
  in doubt about user-visible behavior.
- `NOT_IMPLEMENTED.md` §8b — worker idle loop, the natural
  home for channel-fd polling and the cleanup sweep.
- `AUDIT.md` §11 — current state of the idle loop.
- `ATOM_WATCH_DESIGN.md` — atom watcher semantics; relevant
  for the "atoms and channels share the wake queue but are
  semantically separate" claim.
- `DESIGN.md` open questions §5 — cross-thread value sharing
  in general.

## What to do next session

1. **Spike: channel primitive + idle-loop fd registration.**
   Build the minimum viable channel type with `read` / `write` /
   `close`. Wire fd registration into the idle loop's
   kqueue/epoll wait. Test by hand-wiring a non-blocking pipe
   between two coroutines without going through `!cmd`.

2. **Spike: `create-process` with one channel attached.** Once
   the channel primitive exists, the next step is replacing
   `EXEC_FLAG_BG`'s tempfile redirection (`vm.c:10359-10384`)
   with a `pipe2(O_NONBLOCK)` whose read end is wrapped as a
   channel and exposed via `$j.stdout`. This is the smallest
   slice that proves the channels-on-Jobs path end-to-end.

3. **Decide on JACL-to-JACL coordination channels.** The
   bidirectional-I/O example in SHELL_API_DESIGN.md uses
   `(channel)` to bridge a JACL writer coroutine to a `!cmd`'s
   stdin. That implies pure JACL channels exist (at least for
   the bridge case). Confirm scope before the spike — is a
   "channel" always fd-backed, or can it be JACL-backed too?
