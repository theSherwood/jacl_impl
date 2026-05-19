# Generator State Machine Transform — Design Notes

JACL generators compile to a state machine: one heap allocation per
stream, zero allocations per yield. This doc captures the design and
the tradeoffs that informed it. For the broader concurrency / GC
co-design see `GC_CONCURRENCY_DESIGN.md`.

## Why a state machine

Earlier the same generator path used CPS (continuation-passing style):
each yield allocated a continuation closure. Three closures per yield
in a loop (terminal_k from `stream_next`, the loopback closure for
the loop body, the yield continuation itself), all short-lived. The
GC handled it efficiently, but generators yielding millions of values
allocated millions of throwaway closures.

The SM transform replaces those closures with a single per-stream
state object. Continuations become *code positions* (resume-point
indices) instead of heap objects. Yielding writes a resume index;
resuming dispatches to the right code position.

## Architecture

For every generator proc (`is_sm_compiled = true` on the closure),
the compiler computes:

- A state-field layout: which locals cross yield boundaries, what
  width they need (single slot for scalars / boxed values, multiple
  slots for inline structs).
- A resume-point index per yield site.
- A dispatch table at the proc's entry: switch on the SM's
  resume_point, jump to the right body label.

`gc_alloc_state_machine(heap, field_count)` (gc.c) allocates one
`JaclStateMachine` when the stream is created. `JaclStateMachine.fields[]`
holds the cross-yield locals; `JaclStateMachine.field_inline_bitmap`
records which fields are raw struct bytes (so the GC knows not to
trace them as `JaclVal`s). A bytecode-level state-pointer slot is
maintained in a known stack position so `OP_GET_STATE_FIELD` /
`OP_SET_STATE_FIELD_*` opcodes can address fields by index.

Yield writes resume_point + the yielded value, returns. The runtime
sees `VM_YIELD` and pauses. `OP_STREAM_NEXT` re-enters the proc with
the same state object, which dispatches via the entry switch.

## Suspension uses SM everywhere

Historical note: an earlier design kept CPS for `await` / `parallel` /
`race` on the grounds that those suspend once per call and the
per-suspension allocation was negligible. That split has been removed.
**All suspension points — `yield`, `await`, `parallel`, `race`, and
calls to suspending procs — now go through the state-machine path.**
The CPS `OP_AWAIT` handler at `vm.c:5307` is a stub that errors out
("CPS await not supported"); the live opcodes are `OP_AWAIT_SM`
(`vm.c:9154`), `OP_PARALLEL` (`vm.c:5472`), `OP_RACE` (`vm.c:5659`),
and `OP_CALL_SUSPEND` (`vm.c:9396`), each of which requires a
state-machine continuation.

The compiler's `sm__walk_suspensions__visit` (`compiler.c:1458`) counts
all five suspension kinds together; if any are present, the enclosing
function is SM-compiled (`compiler.c:3724`) and each suspension site
gets a unique resume-point index. A generator body that also `await`s
is just an SM with both yield-resume and await-resume points in the
same dispatch table.

The remaining CPS-shaped allocation is `resolve_k` in `runtime.c`,
created once per `spawn` to publish the spawned closure's result back
into its future. That's one allocation per `spawn`, not per suspension.

### What still allocates per suspension

`OP_AWAIT_SM` registers the SM on the awaited future's waiter list by
calling `jacl_future_add_waiter` (`gc.c:1182`), which allocates a
single `FutureWaiter` node (~48 bytes) per call (`gc.c:1189`). No
closure, no captured environment. A loop of N sequential `await`s
allocates N `FutureWaiter` nodes plus the one SM that was allocated
when the proc was entered. `parallel` / `race` over N child futures
allocate N waiters (one per child future) per call.

The obvious "embed one waiter slot in the SM struct and reuse it
across awaits" optimization is **not viable as a local change.** The
waiter chain is write-once after settle (`gc.c:1133-1143`) and that
invariant is load-bearing for AUDIT §9b / §10-11 race #2 (the fresh-
waiter-reclaimed-under-a-worker case the unconditional grey-buf push
in `gc_write_barrier` closed). An embedded slot linked into future
A's chain cannot be relinked into future B's chain without mutating
A's frozen chain. So an embedded slot would only ever help the
*first* await an SM does, not subsequent iterations of a loop. Making
it actually amortize requires detaching `f->waiters` at resolve time
with a SATB push of the old chain head into the grey buffer — a real
GC change, not a small one, and a re-audit of §9b / §10-11. Skipped
until a profile points at `FutureWaiter` allocation as a hot spot;
none does today (AUDIT §11 says `spawn_chain` is dominated by the
worker idle-park, not allocation).

## Tradeoffs that informed the design

**State object keeps all locals alive across yields.** A closure
captures only what's live at creation; the state object holds all
cross-yield locals. In practice this rarely matters for short-lived
generators. A liveness pass could null out dead state fields after
yield points, but for an interpreted language the analysis cost
isn't worth the memory savings.

**Two-pass compilation for SM bodies.** The first pass walks the AST
to identify suspension points and classify locals (which cross yield
boundaries → state field, which don't → stack local). The second
pass emits bytecode using state-field opcodes for cross-yield
locals. Both passes are O(n).

**Control flow flattening.** A yield inside `if` inside `while`
needs a unique resume label that can re-enter the right nesting
level. Each yield point gets a label; the entry dispatch jumps
directly to it. The compiler buffers body bytecode and backpatches
jump targets after the body is laid out.

**Cell-shared state between threads still works.** Mutable state
sharing is through cells (`JaclMutableRef`), which are heap-allocated
and shared by pointer. The state object holds cell pointers just
like closures captured cell upvalues before. Parallel bodies still
compile as separate functions and run on separate threads — each
parallel body that itself suspends has its own SM or closure.

## Where to find things

| What | File:Function |
|---|---|
| State machine value | `JaclStateMachine` (jacl.h, gc.c) |
| Allocation | `gc_alloc_state_machine` (gc.c) |
| State-field opcodes | `OP_GET_STATE_FIELD*` / `OP_SET_STATE_FIELD*` (vm.c, bytecode.c) |
| SM-aware compilation | `compiler.c` (`is_sm_compiled = true` paths; `SuspensionAnalysis`) |
| Inline struct field bitmap | `JaclStateMachine.field_inline_bitmap` (vm.c) |
| Suspension analysis | `compiler__analyze_suspensions` (compiler.c) |

## Future work

See **`NOT_IMPLEMENTED.md` §8.** Two related items, ideally done
together: lift the existing layout-time liveness pass's yield-only
restriction (CFG-based instead of linear segment walk, so it also
applies to `await`/`parallel`/`race`), and add per-yield-point
clearing of dead SM fields so retained heap values from a finished
generator phase become collectable before the SM itself dies.
