# Generator State Machine Transform — Design Notes

## Context

As of 2026-03-19, generators use CPS (continuation-passing style) for yield.
Each yield allocates a continuation closure on the GC heap. This works correctly
but costs ~3 heap allocations per yield in a loop (terminal_k, loopback closure,
yield continuation). This document captures the analysis for a potential state
machine transform that would eliminate per-yield allocations entirely.

## Current Architecture (CPS)

- Generator bodies are CPS-compiled: yield is a suspension point like await
- `OP_YIELD` pops (continuation_closure, value) from stack
- `OP_STREAM_NEXT` calls generator/continuation closures and interprets VM_YIELD or VM_OK
- Continuation closures capture state via upvalues (cells for mut vars, values for def vars)
- While loops with yield use recursive CPS: self-referencing cell + loopback closure + __k shadowing

### Per-yield allocation cost

| Allocation | Purpose |
|---|---|
| terminal_k closure | Created by stream_next; signals exhaustion when called |
| loopback closure | Created each loop iteration; re-invokes loop function |
| yield continuation | Captures remaining body after yield point |

All three are short-lived (only the latest continuation survives). The GC
handles them efficiently, but it's still real allocation pressure for
generators that yield millions of values.

### Quick win (not yet implemented)

Cache terminal_k on the VM struct — it's always identical. This eliminates
one allocation per stream_next and may also eliminate the loopback closure
re-creation (since the captured __k value would be the same object each time,
though OP_CLOSURE still allocates regardless).

## Proposed Architecture (State Machine)

### Core idea

Allocate **one** state object when the stream is created. Compile the generator
body into a static function that takes the state object and a resume value.
Yield writes a resume point index into the state and returns. Resume dispatches
to the right point via a jump table.

Zero per-yield allocation. The continuations become code positions, not heap objects.

### Conceptual example

Source:
```
proc counter {n} {
  mut i 0
  while (< $i $n) {
    yield $i
    i :: (+ $i 1)
  }
}
```

Compiles to (pseudocode):
```
State: { n, i_cell, resume_point }

function counter_sm(state, resume_value):
  switch state.resume_point:
    case 0: goto start
    case 1: goto after_yield

  start:
    state.i_cell = make_cell(0)
  loop_top:
    if not (get_cell(state.i_cell) < state.n): goto done
    // yield get_cell(state.i_cell)
    state.resume_point = 1
    return YIELD(get_cell(state.i_cell))
  after_yield:
    set_cell(state.i_cell, get_cell(state.i_cell) + 1)
    goto loop_top
  done:
    return EXHAUSTED
```

## Impact Analysis

### CPS / await / parallel / race

State machine transform is only needed for generators (multi-shot yield).
The existing CPS approach is fine for await/parallel/race — they suspend once
and resume once, so the one-closure-per-suspension cost is negligible.

Hybrid approach: keep CPS for concurrency primitives, use state machines for
generators only. This is what Kotlin and Rust effectively do.

If a generator body also contains `await`, both mechanisms must coexist in
one function. The state machine handles yield points; CPS handles await
points. Or: unify everything as a state machine where both yield and await
are suspension points with different dispatch behavior. Full unification is
architecturally cleaner but a much larger compiler change.

### Shared memory multithreading

Still works. Mutable state sharing is through cells (`JaclMutableRef`),
which are heap-allocated and shared by pointer. The state object holds cell
pointers just like closures capture cell upvalues today.

Parallel bodies would still compile as separate functions (they run on
separate threads). Each parallel body that itself suspends would need its
own state machine or closure.

The scheduler interface changes from "call this continuation closure" to
"call this static function with this state object and resume value." Not a
fundamental problem, but it touches the scheduler API.

### GC

Strictly better:
- One state object per generator vs. N closures per yield cycle
- Fewer allocations, fewer GC cycles triggered
- Simpler root set: stream -> state object (no closure chains)
- Less heap fragmentation

One tradeoff: the state object keeps ALL locals alive simultaneously, even
fields that are dead at the current yield point. Closures only capture
what's live at creation. In practice this rarely matters. Production
implementations null out dead state fields after yield points if needed
(requires liveness information).

### Compilation cost

Two passes instead of one, still O(n) total:

1. Walk AST, identify suspension points, classify locals. O(n).
2. Emit bytecode with state-field access + dispatch table. O(n).

Liveness analysis (which locals cross yield boundaries) can be skipped by
conservatively putting ALL locals in state. Larger state object, but zero
analysis cost. Fine for an interpreted language.

## Implementation Requirements

### New bytecode

Locals inside a state machine can't use `OP_GET_LOCAL` (stack-relative).
They need `OP_GET_STATE_FIELD` / `OP_SET_STATE_FIELD` or similar indirection
through a state pointer held in a known stack slot. This means the generator
path emits fundamentally different bytecode from normal function bodies.

### State struct layout

The compiler must compute a per-generator struct: field count, types/sizes,
offsets. Similar to the existing struct type system but auto-generated.

### Dispatch table

Function entry needs a jump table:
```
switch (state->resume_point) {
  case 0: goto start;
  case 1: goto after_yield_1;
  case 2: goto after_yield_2;
  ...
}
```

The compiler tracks yield points during the analysis pass, then emits the
dispatch before the body. Requires either buffering body bytecode or
backpatching the jump targets.

### Control flow flattening

Yield inside `if` inside `while` means the resume point is deep in nested
control flow. The state machine must jump directly into that nesting level.
CPS handles this naturally (closures compose). State machines must explicitly
reconstruct the control flow position — essentially serializing the control
flow state into an integer index.

This is the hardest part of the implementation. Each yield point inside
nested control flow needs a unique resume label, and the dispatch must
reconstruct the right scope/loop context on resume.

### Estimated scope

The current compiler is ~7000 lines. This change would likely add 600-900
lines and require the generator compilation path to become multi-pass.
The main pieces:

- Suspension point identification + local classification: ~150-200 lines
- State struct layout generation: ~100-150 lines
- Dispatch table emission: ~100-150 lines
- State-field bytecode emission (replacing local access): ~150-200 lines
- VM changes (state object type, new opcodes): ~100-150 lines

## Recommendation

**Short term:** Cache terminal_k on the VM to eliminate the easiest
allocation. Consider the hybrid approach only if generator performance
becomes a measured bottleneck.

**Medium term:** Implement state machine transform for generators only.
Keep CPS for await/parallel/race. This gets the biggest performance win
(0 allocations per yield) with a contained compiler change that doesn't
touch the concurrency path.

**Long term:** If the language evolves toward heavy use of generators and
async iteration, consider unifying all suspension under state machines.
This is a larger rewrite but results in one mechanism, zero allocations
everywhere, and a simpler mental model.
