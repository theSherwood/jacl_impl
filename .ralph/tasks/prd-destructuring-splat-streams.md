# PRD: Destructuring, Splat/Spread, Variadic Procs, and Streams

## Introduction

JACL's syntax redesign specifies destructuring, splat/spread (`..`), variadic procs, and streams as core language features. These form a dependency chain — each builds on the previous — and together they unlock the next major milestone: making JACL a practical scripting language with lazy evaluation and the foundation for shell interop.

This PRD covers four features in priority order:

1. **Destructuring** — positional `[]`, named `{}`, rest `..rest`, wildcard `_`, spread-all `{..}`
2. **Splat/spread** (`..`) — spreading collections into arguments in `[]` and (later) `!cmd`
3. **Variadic procs** — `..` in param lists to collect remaining arguments
4. **Streams** — lazy sequence type with CPS-based yield, reusing existing suspension infrastructure

## Goals

- Implement full destructuring (positional, named, rest, wildcard, spread-all) for `def` bindings
- Compile `TOKEN_DOTDOT` (already lexed) for both collecting and spreading semantics
- Support variadic parameters in proc definitions via `..rest` syntax
- Introduce a lazy stream value type built on the existing CPS transform and scheduler
- Make `for`, `filter`, and `transform` polymorphic over vectors and streams
- Enforce stream vs vector distinction at compile time (type system)
- Maintain zero overhead for non-stream code paths

## User Stories

### US-001: Positional vector destructuring
**Description:** As a JACL programmer, I want to destructure vectors with `def [a b c] $v` so that I can bind multiple values from a vector in one statement.

**Acceptance Criteria:**
- [ ] `def [a b c] [vec 1 2 3]` binds `a=1`, `b=2`, `c=3` as local variables
- [ ] Works with any vector expression on the RHS, not just literals
- [ ] Runtime error if vector length doesn't match pattern length (too few or too many elements)
- [ ] Works at any scope depth (top-level and nested blocks)
- [ ] Typed destructuring: `def [i64 a, i64 b] $v` checks element types at compile time
- [ ] Compiles to efficient bytecode — extract by index, no intermediate allocations
- [ ] Existing tests pass; new test file covers all cases

### US-002: Named struct/map destructuring
**Description:** As a JACL programmer, I want to destructure structs with `def {x, y} $point` so that I can extract named fields into scope.

**Acceptance Criteria:**
- [ ] `def {x, y} $point` binds `x` and `y` from struct fields of a Point value
- [ ] Works with maps: `def {name, age} $person_map` extracts by string key
- [ ] Compile-time error if struct type is known and field name doesn't exist
- [ ] Runtime error for maps if key is missing
- [ ] Field order in the pattern doesn't matter: `def {y, x} $point` works identically
- [ ] Typed field destructuring: `def {i32 x, i32 y} $point` type-checks fields
- [ ] Existing tests pass; new test file covers all cases

### US-003: Wildcard pattern (`_`)
**Description:** As a JACL programmer, I want to use `_` in destructuring patterns to ignore values I don't need.

**Acceptance Criteria:**
- [ ] `def [_ b _] [vec 1 2 3]` binds only `b=2`, discards positions 0 and 2
- [ ] `def {x, _} $point` — compile error: `_` is meaningless in named destructuring (just omit the field)
- [ ] `_` does not create a binding — referencing `$_` after destructuring is an error
- [ ] Multiple `_` in the same pattern is allowed (they don't conflict)
- [ ] Compiles efficiently — skipped positions don't emit extraction bytecode
- [ ] Existing tests pass

### US-004: Rest patterns (`..rest`)
**Description:** As a JACL programmer, I want to use `..rest` to collect remaining elements during destructuring.

**Acceptance Criteria:**
- [ ] `def [head ..rest] [vec 1 2 3 4]` binds `head=1`, `rest=[vec 2 3 4]`
- [ ] `def [a b ..rest] [vec 1 2]` binds `a=1`, `b=2`, `rest=[vec]` (empty vector)
- [ ] `..rest` must be the last element in a positional pattern — compile error otherwise
- [ ] Named rest: `def {x, ..rest} $point` binds `x` from field, `rest` is a map of remaining fields
- [ ] `..` without a name (`def [head ..]`) is a compile error in positional patterns (use `..rest` or `_`)
- [ ] Compile-time error if `..rest` appears more than once in a pattern
- [ ] Rest collection creates a new vector/map (not a slice/view)
- [ ] Existing tests pass; new test file covers rest pattern cases

### US-005: Spread-all destructuring (`{..}`)
**Description:** As a JACL programmer, I want to use `def {..} $point` to import all fields of a struct into the current scope.

**Acceptance Criteria:**
- [ ] `def {..} $point` where Point has fields `x` and `y` creates bindings `x` and `y` in scope
- [ ] Only works when the compiler knows the concrete struct type (compile error on untyped/dyn values)
- [ ] `def {x, ..} $point` binds `x` explicitly, plus all other fields (`y` in this case)
- [ ] Shadowing rules apply: if `x` already exists in scope, `def {..} $point` is a compile error (same-scope shadow)
- [ ] Works with `use` imports: `use "shapes.jacl" {..}` imports all public names (separate story for module system)
- [ ] Existing tests pass; new test file covers spread-all cases

### US-006: Splat/spread in juxtaposition (`[+ ..$nums]`)
**Description:** As a JACL programmer, I want to spread a collection into separate arguments using `..` in `[]` juxtaposition mode.

**Acceptance Criteria:**
- [ ] `def nums [vec 1 2 3]; [+ ..$nums]` evaluates to `[+ 1 2 3]` → 6
- [ ] Works with any callable in head position: `[my-proc ..$args]`
- [ ] Can mix spread and regular args: `[+ 10 ..$nums 20]` → `[+ 10 1 2 3 20]`
- [ ] Multiple spreads in one call: `[+ ..$a ..$b]`
- [ ] Spreading a stream eagerly consumes it into arguments (once streams exist)
- [ ] Spreading an empty vector produces zero arguments
- [ ] Compile error if `..` is used without a following expression
- [ ] Runtime error if the spread target is not iterable (not a vector, not a stream)
- [ ] Runtime check before spreading: error if `stack_top + spread_count >= STACK_MAX` with clear message
- [ ] Existing tests pass; new test file covers splat cases

### US-007: Variadic proc parameters
**Description:** As a JACL programmer, I want to define procs that accept variable numbers of arguments using `..rest` in the param list.

**Acceptance Criteria:**
- [ ] `proc log {str level, ..msgs} { ... }` — `msgs` is bound as a vector of all remaining args
- [ ] `log "INFO" "a" "b" "c"` → `level="INFO"`, `msgs=[vec "a" "b" "c"]`
- [ ] Calling with zero variadic args: `log "INFO"` → `msgs=[vec]` (empty vector)
- [ ] `..rest` must be the last parameter — compile error otherwise
- [ ] Only one `..rest` per param list — compile error if multiple
- [ ] Typed variadic: `proc log {str level, ..str msgs}` — type-checks each variadic arg
- [ ] Variadic params compose with splat: `[log "INFO" ..$messages]`
- [ ] Arity checking adjusted: minimum arity = number of non-variadic params
- [ ] Existing tests pass; new test file covers variadic proc cases

### US-008: Stream value type and GC integration
**Description:** As a JACL runtime developer, I need a stream value type that represents a lazy sequence, integrated with the GC and value system.

**Acceptance Criteria:**
- [ ] New `JACL_TAG_STREAM` (0x15) in value tag space
- [ ] New `OBJ_STREAM` in GC object type enum
- [ ] `JaclStream` struct contains: next-function closure, state (pending/exhausted/error), cached current value
- [ ] GC traces stream's next-closure and cached value (no leaks)
- [ ] `jacl_is_stream()` predicate works correctly
- [ ] Stream type registered in compiler type system as `TYPE_STREAM`
- [ ] Compile-time distinction: passing a stream where a vector is expected is a type error (when types are known)
- [ ] `print` on a stream shows `<stream>` (not attempting to materialize)
- [ ] Equality: streams are identity-compared (reference equality), not structural
- [ ] Stream has `CONSUMED` state — once a consumer starts pulling, the stream is owned; second consumer gets runtime error
- [ ] Typed stream syntax `[stream i64]` accepted in type position; element type recorded but not enforced (phase 2)

### US-009: `OP_YIELD` opcode and stream creation
**Description:** As a JACL runtime developer, I need a yield mechanism that reuses the CPS transform to create streams from generator-style procs.

**Acceptance Criteria:**
- [ ] New `OP_YIELD` opcode: takes a value from the stack, creates a continuation for "resume here", packages (value, continuation) into a stream element
- [ ] `OP_YIELD` is the inverse of `OP_AWAIT`: where await says "I need a value, suspend", yield says "here's a value, suspend until consumer wants more"
- [ ] Suspension analysis (`compiler__suspension_analysis`) recognizes `yield` as a suspension point
- [ ] CPS transform handles yield points — creates continuation closures that resume production
- [ ] Generator-style stream creation: a proc containing `yield` calls becomes a stream producer
- [ ] Stream end: when the producer proc returns (falls off end or explicit return), the stream signals exhaustion
- [ ] Yield from within loops works correctly (continuation captures loop state)
- [ ] Existing CPS tests still pass; new tests cover yield mechanics

### US-010: `collect` — stream to vector materialization
**Description:** As a JACL programmer, I want to materialize a stream into a vector using `collect`.

**Acceptance Criteria:**
- [ ] `collect` on a stream eagerly consumes all elements and returns a vector
- [ ] `collect` on a vector is a no-op (returns the vector unchanged)
- [ ] Infinite streams + `collect` will loop forever (no implicit limit — user's responsibility to `take` first)
- [ ] `collect` preserves element order
- [ ] Type: `collect` on a `stream` returns `vec`; on a typed stream returns typed vec
- [ ] New `OP_COLLECT` opcode or implemented as builtin proc
- [ ] Existing tests pass; new test file covers collect cases

### US-011: Stream-aware `for`
**Description:** As a JACL programmer, I want `for` to iterate over streams lazily, one element at a time.

**Acceptance Criteria:**
- [ ] `for $stream item { body }` — pulls one element at a time, executes body, O(1) memory
- [ ] `for $vector item { body }` — unchanged behavior (eager iteration)
- [ ] `break` inside `for` on a stream stops consumption (stream is not fully consumed)
- [ ] `continue` skips to next element pull
- [ ] `return` from enclosing proc stops stream consumption
- [ ] Implicit binding (`$it`) works: `for $stream { print $it }`
- [ ] HOF callback form works: `for $stream $callback`
- [ ] Lambda form works: `for $stream [\\ print $it]`
- [ ] VM dispatches on value type (stream vs vector) at runtime for dyn-typed values
- [ ] Compiler emits stream-specific path when type is known at compile time
- [ ] Existing for-loop tests still pass

### US-012: Stream-aware `filter`
**Description:** As a JACL programmer, I want `filter` to work lazily on streams, producing a new stream.

**Acceptance Criteria:**
- [ ] `filter $stream $pred` returns a new stream that yields only elements passing the predicate
- [ ] `filter $vector $pred` returns a new vector (unchanged eager behavior)
- [ ] Lazy: `filter` on a stream doesn't consume any elements until the result stream is iterated
- [ ] Chained filters: `$s | filter $p1 | filter $p2` creates a chain of lazy streams
- [ ] Compose with pipes: `$stream | filter [\\ > $it 2]`
- [ ] Type: filtering a stream produces a stream; filtering a vector produces a vector
- [ ] Existing filter tests still pass

### US-013: Stream-aware `transform`
**Description:** As a JACL programmer, I want `transform` to work lazily on streams, producing a new stream.

**Acceptance Criteria:**
- [ ] `transform $stream $fn` returns a new stream that yields `$fn(element)` for each element
- [ ] `transform $vector $fn` returns a new vector (unchanged eager behavior)
- [ ] Lazy: no elements consumed until result stream is iterated
- [ ] Compose with pipes: `$stream | transform [\\ * $it 2]`
- [ ] Chaining: `$s | filter $p | transform $f | take 5 | collect` — fully lazy pipeline
- [ ] Type: transforming a stream produces a stream; transforming a vector produces a vector
- [ ] Existing transform tests still pass

### US-014: `count`, `take`, `first` for streams
**Description:** As a JACL programmer, I want sequence operations that work on both streams and vectors.

**Acceptance Criteria:**
- [ ] `count $stream` consumes the entire stream and returns element count (integer)
- [ ] `count $vector` returns vector length (unchanged, no consumption)
- [ ] `take N $stream` returns a new stream that yields at most N elements
- [ ] `take N $vector` returns a new vector with first N elements
- [ ] `first $stream` consumes one element and returns it (or nil/error if empty)
- [ ] `first $vector` returns first element (unchanged)
- [ ] All compose with pipes: `$stream | take 5 | collect`
- [ ] `take` on a stream is lazy — doesn't pull until iterated
- [ ] Existing tests pass; new test file covers all cases

### US-015: `lines` — string/stream to line stream
**Description:** As a JACL programmer, I want to split strings or byte streams into line streams.

**Acceptance Criteria:**
- [ ] `lines $string` returns a stream of line strings (split on `\n`)
- [ ] `lines $stream` returns a stream that splits an incoming byte/string stream on newline boundaries
- [ ] Handles `\r\n` and `\n` line endings
- [ ] Empty trailing line not emitted (consistent with common behavior)
- [ ] Compose with pipes: `"a\nb\nc" | lines | for [\\ print $it]`
- [ ] Lazy: `lines` on a stream doesn't buffer the entire input
- [ ] Existing tests pass; new test file covers lines cases

### US-016: Compile-time stream/vector type enforcement
**Description:** As a JACL programmer, I want the compiler to catch mistakes where I pass a stream to a vector-only operation.

**Acceptance Criteria:**
- [ ] `vec-get $stream 0` is a compile error when `$stream` is known to be a stream type
- [ ] `vec-set`, `vec-slice`, `vec-len`, `vec-push`, `vec-concat` all reject stream-typed arguments at compile time
- [ ] `collect $stream | vec-get 0` compiles successfully
- [ ] When type is `dyn`, no compile error — runtime dispatch handles it (runtime error if stream passed to vec-only op)
- [ ] Error messages are clear: "vec-get requires a vector; got stream (use 'collect' to materialize)"
- [ ] Existing typed code still compiles correctly

## Functional Requirements

- FR-1: Parser must recognize destructuring patterns in `def` LHS: `[a b c]` (positional), `{x, y}` (named), `_` (wildcard), `..name` (rest), `{..}` (spread-all)
- FR-2: Parser must recognize `..expr` as a spread operator in `[]` juxtaposition mode
- FR-3: Parser must recognize `..name` as a rest parameter in proc param lists `{}`
- FR-4: Compiler must emit efficient bytecode for destructuring — index-based extraction for vectors, field-offset extraction for structs, key lookup for maps
- FR-5: Compiler must emit spread bytecode that unpacks a collection onto the operand stack before a call
- FR-6: Compiler must adjust arity checking for variadic procs (minimum arity = fixed params)
- FR-7: Compiler must collect excess arguments into a vector for variadic rest parameters
- FR-8: Runtime must define `JaclStream` as a new GC-managed value type with tag `0x15`
- FR-9: `OP_YIELD` must create a CPS continuation capturing the producer's remaining computation and package (value, continuation) as a stream element
- FR-10: Suspension analysis must treat `yield` as a suspension point, same as `await`
- FR-11: CPS transform must handle yield points — converting them into continuation closures that resume production on next pull
- FR-12: `for`, `filter`, `transform` must dispatch on value type (stream vs vector) and use lazy consumption for streams
- FR-13: `collect` must eagerly materialize a stream into a vector
- FR-14: `count`, `take`, `first`, `lines` must work on both streams and vectors
- FR-15: Type system must distinguish `TYPE_STREAM` from `TYPE_VEC` and reject streams at compile time for vector-only operations
- FR-16: Spreading a stream (`[f ..$stream]`) must eagerly consume the stream into arguments

## Non-Goals

- Destructuring in proc parameter lists (do it in the body per spec; `mut` destructuring in body is supported)
- Async/suspending streams in this pass (no `await` inside stream producers yet — pure synchronous generators first; async streams are a follow-up once shell interop exists)
- Parallel stream consumption (`par-each`) — separate feature
- Shell interop (`!cmd` producing streams) — next PRD
- `$ctx` implicit context — next PRD
- Back-pressure or bounded buffering for streams
- Stream fusion / deforestation optimizations
- Match/case (benefits from destructuring but is a separate feature)
- `{..}` in `use` imports (requires module system work)

## Technical Considerations

### Destructuring compilation strategy

Destructuring `def [a b c] $expr` compiles to:
1. Compile RHS expression (pushes one value onto stack)
2. Emit `OP_DUP` to preserve the collection on stack
3. For each binding: emit extraction opcode (`OP_VEC_GET` with index for positional, `OP_STRUCT_GET_FIELD`/`OP_MAP_GET` for named)
4. Each extracted value becomes a local (same as regular `def`)
5. Pop the original collection

For rest patterns, emit `OP_VEC_SLICE` from rest-start to end, creating a new vector.

For `{..}`, the compiler must know the struct type at compile time to enumerate fields. Use `StructTypeRegistry` to look up field names and emit extractions for each.

### Splat compilation strategy

`[f ..$args]` compiles to:
1. Compile `f` (callable)
2. Compile any fixed args before the splat
3. Compile `$args`, emit `OP_SPREAD` which pushes each element onto the stack and pushes the count
4. Compile any fixed args after the splat
5. Emit `OP_CALL_SPREAD` (new opcode) which uses the dynamic argument count

This requires a new call convention variant that handles dynamic arity. The VM must sum fixed + spread counts to determine total argument count.

### Variadic proc compilation

At the call site, all arguments are pushed normally. At the proc entry, the compiler emits bytecode to:
1. Check `arg_count >= min_arity` (runtime error otherwise)
2. Collect arguments from `min_arity` to `arg_count` into a new vector
3. Bind that vector as the rest parameter's local slot

### Stream implementation (CPS-based yield)

**`JaclStream` struct:**
```
state: PENDING | EXHAUSTED | ERROR
next_fn: JaclClosure*    // CPS closure — call to produce next element
```

**Yield mechanics (reusing CPS infrastructure):**
- `OP_YIELD` is modeled on `OP_AWAIT` but in reverse:
  - `OP_AWAIT`: "I need a value from this future; here's my continuation — call it when ready"
  - `OP_YIELD`: "Here's a value for the consumer; here's my continuation — call it when they want more"
- The CPS transform converts yield points into continuation closures, same as await points
- `compiler__suspension_analysis()` marks procs containing `yield` as suspending
- `compiler__compile_cps_stmts()` handles yield the same way it handles await — creating a continuation for the code after the yield, but instead of registering as a future waiter, it packages (yielded_value, continuation) into a stream element

**Consumer pull protocol:**
- Calling `stream.next_fn()` resumes the producer's continuation
- Producer runs until next `yield` or return
- On `yield`: new (value, next_continuation) packaged; consumer receives value
- On return: stream marked EXHAUSTED; consumer sees end-of-stream

**Integration with existing scheduler:**
- Synchronous streams (this pass): producer and consumer run on same thread, no scheduling needed. `OP_YIELD` directly returns to the consumer's call frame.
- Future async streams: producer runs as a scheduled task, consumer awaits stream elements as futures. Same CPS + scheduler infrastructure handles this naturally.

### GC integration

- `OBJ_STREAM` added to GC object type enum
- GC trace function walks `next_fn` closure (which may capture arbitrary state via upvalues)
- Stream continuations are GC roots while the stream is live
- Exhausted streams can have their `next_fn` nulled to allow GC of producer state

### Type system integration

- `TYPE_STREAM` added to `JaclType` enum
- `compiler__resolve_type()` recognizes `"stream"` keyword
- Typed streams: `[stream i64]` parallels `[vec i64]` — element type known at compile time
- Vector-only builtins (`vec-get`, `vec-set`, `vec-slice`, `vec-len`, `vec-push`, `vec-concat`) reject `TYPE_STREAM` during compilation
- Sequence builtins (`for`, `filter`, `transform`, `count`, `take`, `first`, `collect`, `lines`) accept both `TYPE_VEC` and `TYPE_STREAM`

### New opcodes

| Opcode | Description |
|--------|-------------|
| `OP_DESTRUCTURE_VEC` | `N`: extract N elements from vector on stack, push each |
| `OP_DESTRUCTURE_VEC_REST` | `N`: extract first N elements, collect rest into vector |
| `OP_DESTRUCTURE_STRUCT` | Extract fields by name from struct on stack |
| `OP_SPREAD` | Unpack collection onto stack, push element count |
| `OP_CALL_SPREAD` | Call with dynamic argument count (fixed + spread) |
| `OP_COLLECT_VARIADIC` | `min_arity`: collect excess args into vector |
| `OP_YIELD` | Yield value to consumer, create resume continuation |
| `OP_STREAM_NEXT` | Pull next element from stream (may resume producer) |
| `OP_COLLECT` | Materialize stream into vector |

### Files to modify

| File | Changes |
|------|---------|
| `lexer.c` | No changes — `TOKEN_DOTDOT` already lexed |
| `parser.c` | Recognize destructuring patterns in `def`/`mut` LHS; `..expr` in `[]`; `..name` in `{}` params |
| `ast.c` | New AST node types: `AST_DESTRUCTURE_VEC`, `AST_DESTRUCTURE_NAMED`, `AST_SPREAD`, `AST_REST` |
| `compiler.c` | Destructuring compilation, spread compilation, variadic param handling, yield CPS transform, stream type checking |
| `bytecode.c` | New opcodes (see table above) |
| `vm.c` | Execute new opcodes, stream-aware `for`/`filter`/`transform` dispatch |
| `value.c` | `JACL_TAG_STREAM`, `jacl_is_stream()`, `jacl_stream_ptr()` |
| `gc.c` | `OBJ_STREAM`, `JaclStream` struct, trace function |
| `runtime.c` | Stream pull protocol (synchronous for this pass) |

## Success Metrics

- All 16 user stories pass their acceptance criteria
- Existing test suite passes with zero regressions
- Destructuring compiles to at most N+1 opcodes for N bindings (no intermediate allocations except for rest collection)
- Stream pipelines (`filter | transform | take N | collect`) consume O(1) memory for the pipeline itself (elements not buffered)
- Splat spreading has no overhead for zero-spread calls (normal call path unchanged)
- Type errors for stream-where-vector-expected are caught at compile time with clear messages

## Resolved Questions

1. **`OP_YIELD` synchronous semantics:** Return approach — `OP_YIELD` packages (value, continuation-closure) and returns control to the consumer, same pattern as `OP_AWAIT`. The continuation closure captures all local state via upvalues. No coroutine-switch mechanism needed. This reuses the existing CPS transform almost unchanged and naturally extends to async streams later.

2. **Stream identity and reuse:** Single-consumer with runtime error on reuse. Streams track a `CONSUMED` state. Once a consumer starts pulling, the stream is owned. A second consumer attempting to pull gets: "stream already consumed — use 'collect' to materialize if you need multiple passes." Consistent with the "no auto-collect" philosophy.

3. **Typed stream syntax:** `[stream i64]` paralleling `[vec i64]`. Syntax accepted and recorded in this pass, but element type enforcement deferred to phase 2 (matching the typed collections implementation path). Streams are `TYPE_STREAM` with `TYPE_DYN` elements for now.

4. **Destructuring in `mut` bindings:** Yes. `mut [a b] [vec 1 2]` creates mutable `a` and `b`. Same destructuring compilation logic, mutable flag flipped on each emitted local. Operator sugar follows: `[a b] : [vec 1 2]`.

5. **Spread count limit:** Runtime check before spreading. One comparison: `current_stack_top + spread_count < STACK_MAX`. Clear error on overflow: "spread exceeds stack capacity (N elements); pass the collection directly instead." Cheap insurance, good error messages.
