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
- The runtime reaches edges through **unir's C ABI** (`unir.h`), unir's temen link unit, which each
  image links after laying it out as a base (Build, below).
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
runtime/unir/         vendored from unir: the C ABI's unit and headers, the store vat's images, UNIR_REV
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
- **Wake latency.** A notify from the peer does not wake an *idle* worker: it sleeps for
  `JACL_WAIT_NS` (1 ms) between re-polls, on `jacl_pool_event` or on the vat's notification when one is
  open (docs/UNIR_SERVICES.md, "Waiting"). So an idle reader sees new data within about 1 ms; a busy
  worker sees it at its next re-poll. That is fine for #18 and #19. The fix, if a measurement needs one,
  is an edge whose peer signals the vat's notification, as a service's does once armed.
- **The unit's heap.** The runtime has no `malloc`, and calling `jacl_alloc` from inside the unit would
  hit GC safepoints mid-call. `chan_unir.c` defines `unir_host_alloc`/`unir_host_free` over a static
  4 MiB pool outside `jacl_heap_mem`: power-of-two size classes from 16 B to 256 KiB with free lists,
  and a `cas32` spin lock that is never held across a park. The unit allocates a few hundred bytes per
  edge end and per spawn, a service's whole schema at once, and a legacy job's screen (up to 100 KiB)
  as one read. A request the pool cannot serve fails the unit's allocation, which traps. The pool's
  lock is separate from the vat's, because the unit allocates inside its calls.
- **The vat, one call at a time.** Of `unir.h`'s calls only an edge's end operations and a
  notification's wait and signal take no vat; the others read and grow the vat's tables (its posts,
  mapped notifications and children), and this runtime's fibers run on several workers. So every call
  that takes the vat holds a parking lock (`vat_take`/`vat_give`): a fiber waits its turn parked, never
  spinning against one parked inside a call. A call that waits for a peer (a service's reply, a child
  that starts or ends) holds the vat meanwhile; a wait that may be long does not. A service's next
  event is waited for on the vat's notification when the service was opened with it, and polled for
  otherwise (docs/UNIR_SERVICES.md, "Waiting"); a legacy job's next screen is waited for the same
  way, its report armed to signal the notification (`unir_job_arm`, #242), and its end signalling
  it too, reported or not (unir#432), so the wait has no bound (#270).

## Build

- **What unir ships.** unir's `e2e/temen` builds its C ABI as a temen link unit
  (`cargo run --release -- cabi-unit <dir>`, which writes `unir_cabi.temeno` and copies `unir.h` and
  `unir_store.h`; theSherwood/unir decision #126.1). `jacl_impl` vendors them under `runtime/unir/`,
  with the unir commit in `UNIR_REV`, the way the playground bundle is committed. A runtime build
  then needs no Rust toolchain of unir's and no LLVM but clang's.
- **The unit is unir's whole C ABI**: `unir.h`'s vat, edge, job, service and screen calls, including
  `unir_spawn` for pipelines (`docs/UNIR_PIPELINES.md`), and the store's (`unir_store.h`, which
  `unir.h` includes, and `unir.h`'s `unir_home_*`). The runtime calls none of the store's.
- **Linking** (jacl_impl#267). The runtime's C is compiled with clang 18 and translated as a link unit
  (`temen-llvm`'s `link_unit`). Its calls into the C ABI stay symbols, and the function pointers in
  its data are recorded. `runtime/harness/src/lib.rs`'s `link` is the one route every image takes:
  it decodes the unit once, lays it out as a base (`temen_ir::link_base`), and links the runtime and
  the program after it. Each of the unit's functions none of them can reach links as a trap
  (`base_live`, a copy of temen's until theSherwood/temen#2751 moves it into `temen_ir`), so an image
  carries what the runtime uses of the C ABI, not all of it (theSherwood/unir decision #409.1).
  - The unit's heap, `unir_host_alloc` and `unir_host_free`, is the runtime's (`chan_unir.c`).
  - A call through a function pointer in the unit's data, such as `core::fmt`'s and every Rust
    `dyn` call's, reaches the function it names (theSherwood/temen#2308). `tests/unir_unit.rs` calls
    `unir_sync_print`, which formats with `core::fmt`, on every engine.
  - `runtime/build.sh` writes `jaclrt.temen`, the one library the playground links programs
    against, with the harness's `link_runtime`: the runtime linked with the unit this way.
  - An application links the runtime as a link unit beside its own code, with temen-link
    (`docs/APPLICATIONS.md`, #290).
  - `codegen.rs::the_runtime_compiles_to_bytecode` guards that the linked runtime stays inside the
    bytecode engine's subset, which a module that both spawns and uses fibers is since
    theSherwood/temen#1789.
- **One unir commit.** `UNIR_REV` names the commit the unit, its headers and the store vat's images
  (`docs/UNIR_SERVICES.md`) all come from, and a re-vendor moves them together: a program built on
  one commit's C ABI talks to the store vat of the same commit.
- **The TEMEN pins.** The unit and the images were translated by unir's `vendor/temen`, and this
  repo's temen decodes and runs them, so the two must share temen's wire format and IR. They may
  differ otherwise: unir 065b95d's pin is temen 4aea8e0, this repo's is f99ad5c, and neither
  `temen-encode` nor temen's instruction set changed between them. A re-vendor checks this before
  it moves `UNIR_REV`.
- **Later:** the runtime itself can join the base, laid out once with the unit. That gives #250's
  pruning and #83's link-once lever, by theSherwood/temen#2168's route.

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
