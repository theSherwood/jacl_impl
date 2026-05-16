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

## Hybrid: SM for generators, CPS for await/parallel/race

await / parallel / race suspend once and resume once. Per-suspension
allocation is negligible there, and the CPS implementation composes
across nested concurrency primitives more naturally than a state
machine would. Keeping CPS for those keeps the scheduler interface
unchanged.

If a generator body also contains `await`, the two mechanisms
coexist in one function: the SM handles yield points; CPS handles
await. The compiler tracks both kinds of suspension separately
(`SuspensionAnalysis` in compiler.c).

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

See **`NOT_IMPLEMENTED.md` §8** — yield-liveness pass for SM-stored
locals.
