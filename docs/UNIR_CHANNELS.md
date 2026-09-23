# Channels on Unir edges (the TEMEN channel backend)

**Status:** design, 2026-09-23. Implements [theSherwood/unir#18](https://github.com/theSherwood/unir/issues/18),
the channel half of stage 1b. [unir#19](https://github.com/theSherwood/unir/issues/19) (`a | b` across
vats) builds on it. The user-facing semantics are the ones `SHELL_API_DESIGN.md` and
`CHANNELS_DESIGN.md` already settled; this note is the infrastructure under them on TEMEN. It
supersedes `CHANNELS_DESIGN.md`'s implementation plan (epoll/kqueue in the idle loop, `pipe2`, `vm.c`),
which targeted the retired native VM.

## Summary

- A **channel is a Unir edge**: a bounded SPSC ring in a TEMEN shared region, with credit, marks and
  terminals (unir spec §1a, §8). JACL does not implement a ring of its own.
- The runtime reaches edges through **unir's C ABI** (`unir.h`). The Rust edge unit is `llvm-link`ed
  into `jaclrt` before its one translation, so a JACL program still links one runtime module.
- **Blocking is fiber parking.** An edge read or write that must wait calls `__vm_wait32` inside the
  calling fiber. TEMEN parks the fiber, not the worker (the §3.6 5a contract `sleep` already relies on),
  and `worker_loop`'s `repoll_blocked` resumes it. No new scheduler machinery.
- The channel layer is **platform-neutral**; `runtime/chan_unir.c` is the only file that knows about
  Unir. It implements five `chan_be_*` functions, and a Unix or Windows backend would implement the
  same five. It is compiled only with `JACL_UNIR`, which `runtime/build.sh` and the harness's
  `translate_runtime` set because they link the unit. Every other build of `jaclrt.c` (the self-hosted
  compiler card, C-driver tests, the staging runtime) gets stubs, and `channel` returns an error value.

## Surface (from `SHELL_API_DESIGN.md`)

| JACL | meaning | on the edge |
|---|---|---|
| `def {w r} {channel}`, `[channel CAP]` | a bounded channel; returns its write and read ends | a fresh edge region, both ends attached in this vat; CAP frames (default 16) |
| `write $w $bytes` | write every byte, parking while full; `nil`, or an error value if the channel ended | frames of at most `max_payload` bytes |
| `read $r N` | up to N bytes, parking while empty; `nil` at end of stream; an error value if severed | reads a frame, keeps any unread tail for the next `read` |
| `close $w` | half-close: the reader sees end of stream after the buffered bytes | Complete |
| `close $r` | the reader is done; the writer's next `write` gets an error value | Cancel |

- **Bytes by default.** `read` returns a flat `[Buf n u8]` (`runtime/flatbuf.c`). `write` accepts a
  `str` (its bytes) or a `u8` flat buffer. Decoders such as `lines $r` are adapters on top, later.
- **Errors are values** (unir §10). A sever surfaces as a JACL error value naming the cause
  (`io-error`, `peer-reset`, `cancelled`, …), never as a trap. Using an end after `close` is an error
  value too.
- **Framing is invisible.** A channel is a byte stream: a `write` larger than one frame is split,
  and a `read` of N returns at most N bytes of the current frame, keeping the rest.

## Runtime layer

```
runtime/chan.c        platform-neutral: the JaclChan object, read/write/close, framing, the busy flag
runtime/chan_unir.c   the Unir backend over unir.h, plus the unit's heap (unir_host_alloc/free)
runtime/unir/         vendored from unir: unir.h, unir_cabi.ll, UNIR_REV
```

- **The object.** A channel end is one GC object (`JOBJ_BLOB`, so no traced pointers). It holds the
  end kind (read or write), the backend kind, the backend handle (a `unir_producer*` or
  `unir_consumer*`, which lives in the unit's own heap, not the GC heap), a busy flag, and for a read
  end the unread tail of the current frame (at most `max_payload` bytes).
- **One operation per end at a time.** An edge end has one writer by construction (unir invariant 5),
  and a fiber can park in the middle of an edge call. So each end carries a busy flag (`cas32`). A
  second fiber that enters an end already in use gets an error value (`channel busy`) instead of
  racing it. Sharing one end among several fibers (Go-style fan-in and fan-out) is a later addition:
  a waiter list on the end, unparked the way `complete_job` unparks awaiters.
- **No runtime lock across an edge call.** An edge call may park the fiber, and holding `slock` or a
  GC lock across a park livelocks the pool (jacl #142). The busy flag is the only state held across
  the call, and it is per end.
- **GC.** Buffers passed into the unit stay reachable from the calling fiber's frame while it is
  parked. The collector is non-moving, so their addresses stay valid. An unreachable end is not yet
  finalized: its edge stays open and its handle stays allocated until the vat exits (see Later).

## The Unir backend

- **The vat.** The runtime creates its `unir_vat` on first use with `unir_vat_root(...)`. Regions map
  over a static, 64 KiB-aligned 8 MiB array in the runtime (`jacl_unir_map`) that holds nothing else.
  That needs no window configuration from the host, and it is zero-initialized, so it adds nothing to
  the `.temen` file. Each channel maps its region twice, one 64 KiB page per end at the default
  capacity, so the area holds about 60 channels until the binding learns to unmap. Child carves arrive
  with #19.
- **Authority.** Creating a region is AddressSpace op 5, the runtime's one new import
  (`vm_region_create`); mapping and waiting go through handles the runtime already holds. An embedder
  that instantiates the runtime grants it (`Imports::provide("vm_region_create", HostCap::memory(5))`),
  as the harness, `jacl_temen` and `bench_temen` now do.
- **A channel within one vat** is one edge region mapped twice, once per end. It exercises the full
  edge protocol (credit, wake, terminals) with no second vat, which is what makes #18 testable before
  #19 wires stages across vats.
- **Waiting.** `unir_producer_write` and `unir_consumer_read` are called with no timeout. Inside a fiber
  their waits park the fiber. The unir binding issues an unbounded wait as 1 s timed waits
  (temen#1711), which only bounds each park.
- **Wake latency.** A notify from the peer does not wake an *idle* worker: it sleeps on
  `jacl_pool_event` for `JACL_WAIT_NS` (1 ms) between re-polls. So an idle reader sees new data within
  about 1 ms; a busy worker sees it at its next re-poll. That is fine for #18 and #19. The fix, if a
  measurement needs one, is TEMEN's blocking-resume variant (`TEMEN_FIBER_TIMED_WAIT_FOLLOWUP.md` §2),
  or waking on the edge's bell directly when a worker's only blocked fiber is on one edge.
- **The unit's heap.** The runtime has no `malloc`, and calling `jacl_alloc` from inside the unit would
  hit GC safepoints mid-call. `chan_unir.c` defines `unir_host_alloc`/`unir_host_free` over a static
  256 KiB pool outside `jacl_heap_mem`: power-of-two size classes from 16 B to 4 KiB with free lists,
  and a `cas32` spin lock that is never held across a park. The unit allocates a few hundred bytes per
  edge end and per spawn. The pool's lock is separate from the lock held while a channel opens,
  because the unit allocates while it opens.

## Build

- **What unir ships.** unir's `e2e/temen` emits the edge unit as internalized LLVM IR
  (`cargo run --release -- cabi-ll <dir>`, which writes `unir_cabi.ll` and copies `unir.h`).
  `jacl_impl` vendors both files under `runtime/unir/`, with the unir commit in `UNIR_REV`, the way the
  playground bundle is committed. A runtime build then needs no Rust toolchain.
- **Linking.** `runtime/build.sh` compiles `jaclrt.c` to `.ll` with clang 18 as today, then runs
  `llvm-link jaclrt.ll runtime/unir/unir_cabi.ll` and translates the result once. The unit's IR comes
  from rustc's LLVM 21, and LLVM 18's `llvm-link` cannot read it, so **this step needs `llvm-link`
  from LLVM 21 or later** (`LLVM_LINK`, defaulting to `llvm-link-21`). CI installs it next to clang 18.
  The harness (`translate_runtime`) links the same way.
- **The TEMEN pin** must match unir's: the unit's `__vm_*` builtins are lowered by this repo's
  `temen-llvm`. `UNIR_REV` records the unir commit, whose `vendor/temen` pin must equal ours.
- **Later:** once temen#1746 lets the on-ramp import across translated units, unir can ship a
  translated `.temen` unit and `jaclrt` links it through `temen_ir::link`, which drops the LLVM 21 step.

## Tests

`runtime/harness/tests/channels.jacl`, run by `codegen.rs::channels_run_on_temen` on the interpreter
and the JIT (`run_diff` requires they agree), is self-checking like `tour.jacl`:

- round trips, and reads smaller than a frame that keep the rest;
- a write larger than a frame, split and reassembled in order;
- end of stream after `close $w`, with the buffered bytes still delivered;
- a writer and a reader in two jobs over a two-frame ring, each parking in turn, with every byte
  arriving once and in order;
- `channel busy` for a second job entering an end that is in use;
- `close $r` making the writer's next write an error value, and bad arguments as error values.

It lives beside the harness, not in `test/jacl/`, because that corpus also feeds the playground,
whose runtime build does not grant `vm_region_create` yet. The C-driver harness (`run_test`) cannot
host these tests: it runs without a powerbox, so there is no AddressSpace to create regions with.

## Later (not #18)

- Channels across vats: stage spawn, endowment and the child-side attach (#19).
- Several fibers sharing one end (a waiter list).
- Finalizing an unreachable end: Cancel on a read end, Sever(`cancelled`) on a write end.
- `lines`/`chars` decoders, and typed channels (unir stage 2).
- Freeing a closed edge's mapping: the unir binding bump-allocates its map range and never unmaps, so
  a vat that opens thousands of channels runs out of map space and gets an error value. This needs an
  unmap in the unir binding.
