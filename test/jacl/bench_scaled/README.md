# Cross-language execution benchmarks

Scaled execution benchmarks comparing JACL's backends against reference language
runtimes. This directory holds the JACL source of truth (`*.jacl`); logical
mirrors live in `test/python/bench_scaled`, `test/js/bench_scaled`, and
`test/clojure/bench_scaled`. **Every mirror produces byte-identical output to the
JACL original** — that equality is the correctness gate for the comparison.

| Scenario | What it stresses | Output |
|---|---|---|
| `fib` | recursive call dispatch + integer arithmetic (fib 34, ~11.4M calls) | `5702887` |
| `sieve` | doubly-nested integer loop: `<=`, `%`, `==` (primes < 60000) | `6057` |
| `map_lookup` | hot hash-map reads (3M lookups over a 200-entry map) | `493733` |
| `box_churn` | short-lived single-slot allocations (3M) | `21000000` |
| `string_concat` | O(N²) immutable string building (400 rounds) | `160400` |

`_baseline.jacl` (`print 0`) exists only to measure the TEMEN JIT's per-run compile
tax; it is not a workload.

## What is measured

**Execution only.** Every runtime compiles/loads once, warms up, then the hot
path is timed N times and the **minimum** is reported (minimum = least
interference, closest to the machine's true floor). Process startup, parsing, and
one-time compilation are hoisted out of the timed region for *all* runtimes, so
the numbers are comparable.

| Runtime | Harness | One-time cost excluded |
|---|---|---|
| Old bytecode VM | `test/test_perf.c` (`.build/perf`) | compile to bytecode |
| **TEMEN JIT** (new) | `runtime/harness` → `bench_temen` | runtime translation + Cranelift compile |
| CPython 3.11 | `pytimer` (below) | interpreter startup, import |
| Node 22 (V8) | `nodetimer` (below) | process startup, TurboFan warm-up |
| Clojure 1.12 (JVM) | `_bench_runner.clj` (this suite) | JVM startup, HotSpot C2 warm-up |

## Results

Times in **milliseconds, minimum of timed runs, execution-only**. Single machine,
best-of-N; treat as order-of-magnitude with ±10–15% run-to-run noise.

| Scenario | Old VM | **TEMEN JIT** | Python 3.11 | Node 22 | Clojure/JVM |
|---|--:|--:|--:|--:|--:|
| `fib` | 3384 | **258** | 613 | 60 | 54 |
| `sieve` | 2954 | **255** | 342 | 21 | 5.0 |
| `map_lookup` | 1588 | **204** | 195 | 23 | 69 |
| `box_churn` | 3361 | **134** | 114 | 1.9 | 13 |
| `string_concat` | OOM¹ | **53** | 4.6 | 0.9 | 13 |

¹ The old VM exhausts its GC (1.5 MB threshold) on the scaled `string_concat` and
aborts. It completes at the unscaled size.

**TEMEN JIT** figures are execution-only with the harness's ~198 ms/run Cranelift
recompile subtracted (a one-time cost under real AOT use). Even *without*
subtracting — counting a full recompile on every run — the JIT still beats the old
VM ~7× on `fib`. The raw per-run JIT figures (incl. compile) are `fib` 455,
`sieve` 452, `map_lookup` 402, `box_churn` 331, `string_concat` 250 ms.

**Headline:** the migration from the bytecode VM to TEMEN made JACL **8–25× faster**
(13× on `fib`) with identical results, landing it around CPython. The two mature
JITs — V8 and the JVM's HotSpot — are still ahead.

## Results — 2026-09-11 re-run, all seven columns on one machine

The table above was measured on an earlier machine and lacks the wasm tiers and Lua. This run has
every column from a single container (4-core Xeon @ 2.80GHz), same protocol: load/compile once, warm,
time the hot path N times, report the **minimum**. Treat cross-machine comparisons with the table
above as invalid — this box is ~1.5× slower than that one (CPython `fib` 918 ms here vs 613 ms there).

Times in **milliseconds, execution-only** (per-run compile/decode tax subtracted via `_baseline`,
which is `print 0`; the raw figures and the tax are below).

| Scenario | JACL cranelift | JACL wasm coop | JACL wasm bytecode | Python 3.11 | Node 22 | Lua 5.4 | Clojure 1.12/JVM |
|---|--:|--:|--:|--:|--:|--:|--:|
| `fib` | **427** | 28676 | 28429 | 918 | 98 | 433 | 72 |
| `sieve` | **436** | 35429 | 36023 | 566 | 52 | 269 | 14 |
| `map_lookup` | **389** | 28360 | 50551 | 367 | 32 | 120 | 101 |
| `box_churn` | **225** | 19631 | 34606 | 210 | 2.7 | 285 | 12 |
| `string_concat` | **107** | 8793 | 16151 | 9.0 | 1.4 | 17 | 13 |

Per-run tax excluded from each JACL column (`_baseline` minimum): cranelift **271 ms** (Cranelift
recompiles the module every run in this harness), wasm coop **50 ms**, wasm bytecode **10.5 ms**.
Every column's output was checked against the documented result (`5702887` / `6057` / `493733` /
`21000000` / `160400`).

### Reading it

- **Native JACL (cranelift) lands around CPython.** Faster on the call- and loop-heavy scenarios
  (`fib` 427 vs 918, `sieve` 436 vs 566), level on `map_lookup` and `box_churn`, and ~12× behind on
  `string_concat` (CPython's `str` append hits a realloc-in-place fast path JACL's rope concat does
  not). Against Lua 5.4 it is a wash: level on `fib`, ahead on `box_churn`, behind on `sieve` and
  `map_lookup`.
- **The two mature JITs are still well ahead**: V8 4–10× faster than cranelift JACL, HotSpot 4–31×.
  The "why" analysis below (boxed values, out-of-line comparisons/`%`/`/`) is unchanged and is where
  the gap lives.
- **In the browser, JACL runs at interpreter speed — 65–130× off its own native tier.** That is the
  number to quote for the playground, not the cranelift column.
- **The wasm-JIT tier does not accelerate JACL user code.** `runJitModule` on a linked JACL program
  declines whole-program emit (its `_start` reaches caps/GC) and falls back to the cooperative
  tier-up driver, which is no better than the plain bytecode interpreter on `fib`/`sieve` and only
  ~1.8× better on the allocation/lookup scenarios. Consistent with jacl#83 lever 4a, closed as
  measured-not-worth-it. The playground's own latency (compile ~85 ms, run ~12 ms for the tour) comes
  from the compiler-guest tiering up, which is a different mechanism from running user code emitted.

### Harnesses used

| Column | Command |
|---|---|
| JACL cranelift | `cd runtime/harness && BENCH_NO_INTERP=1 BENCH_ITERS=15 cargo run --release --bin bench_temen -- ../../test/jacl/bench_scaled/{_baseline,fib,…}.jacl` |
| JACL wasm coop / bytecode | `emit_temen <x>.jacl <x>.temen` per scenario, then `runJitModule` / `temen_run_onramp` in Node against the shipped browser cdylib, best-of-N with a stable cacheKey |
| Python 3.11 | `pytimer.py` (import once, warm 3×, time `run()` 10×) |
| Node 22 | `nodetimer.mjs` (dynamic import, warm 3×, time `run()` 10×) |
| Lua 5.4 | `luatimer.lua` (`dofile` once, warm 3×, time `run()` 10×, `os.clock`) |
| Clojure 1.12 | `clojure test/clojure/bench_scaled/_bench_runner.clj` (Debian's `clojure` takes no `-M`) |

The Lua mirrors live in `test/lua/bench_scaled/` and, like the other mirrors, produce byte-identical
output to the JACL originals.

## Reproducing

From the repo root:

```sh
# Old bytecode VM (execution-only, compile-once/run-N)
./build.sh --test=perf
BENCH_SCENARIOS="fib_recursive,sieve_primes,map_lookup_hot" ./.build/perf
#   NOTE: test_perf's table points at the UNSCALED test/jacl/bench dir. To time
#   the scaled workloads, temporarily copy bench_scaled/<x>.jacl over the matching
#   bench/<name>.jacl the table expects, run, then restore.

# TEMEN JIT + interp (execution-only; interp is the slow correctness oracle)
cd runtime/harness
BENCH_NO_INTERP=1 cargo run --release --bin bench_temen -- \
  ../../test/jacl/bench_scaled/_baseline.jacl \
  ../../test/jacl/bench_scaled/fib.jacl  # … etc

# Clojure (warmed JVM)
clojure -M test/clojure/bench_scaled/_bench_runner.clj

# Python 3.11 — save as pytimer.py, then: python3 pytimer.py test/python/bench_scaled/fib.py
#   import module, warm 3×, time run() 10×, report min/median (perf_counter)
# Node 22   — save as nodetimer.mjs, then: node nodetimer.mjs <abs path>/fib.mjs
#   dynamic import, warm 3×, time run() 10×, report min/median (performance.now)
```

The `bench_temen` and `test_perf` harnesses are part of the TEMEN migration work; a
full run of all five columns requires that code to be present.

---

# Why is JACL so much slower than Clojure?

Clojure runs on the JVM — one of the most heavily optimized runtimes ever built —
so being 3–5× behind it (and ~50× on `sieve`) is not surprising in absolute
terms. But the *shape* of the gap points at specific, fixable things in the JACL →
TEMEN lowering, not at a fundamental ceiling. Concretely:

### 1. Comparisons, `%`, and `/` are out-of-line runtime calls — the biggest gap

Only `+ − *` on expressions the typer **proved** to be `i32` get lowered to native
machine instructions (`codegen.c`, `compile_i32` / `i32_arith`). Every other
arithmetic-ish operation — `< <= > >= == !=`, `%`, `/` — lowers to a **C function
call** into the runtime (`emit_binop_call` → `jacl_lt`, `jacl_mod`, …). Each of
those calls takes boxed arguments, switches on the type tag, computes, and returns
a boxed result.

This is why `sieve` is the worst case: its inner loop is
`while (i*i <= k) { if (k % i == 0) … }`. The `<=`, `%`, and `==` are **three
function calls per inner iteration**, while HotSpot compiles them to three machine
instructions (`cmp`, `idiv`/`irem`). Clojure finishes `sieve` in 5 ms; the TEMEN JIT
takes 255 ms — a 51× gap that is almost entirely call overhead on the hottest ops.

> **Highest-leverage fix:** lower typed comparisons, `%`, and `/` to native `IRB_*`
> ops on the same path that already handles `+ − *`. This alone should move
> `sieve`, `map_lookup`, and `fib`'s base-case test substantially.

### 2. Values are boxed (tagged) across every local, argument, and return

JACL values are tagged 64-bit words (`JACL_TAG_I32_SHIFTED = 0x02 << 56`, payload
in the low 56 bits). The unboxed fast path is a narrow island: it unboxes at the
leaves of a typed `+ − *` tree and **re-boxes at the root** — "the env/frame stay
all-i64" by design. So any value that crosses a variable binding, a call boundary,
or a block boundary is a boxed, tagged word. Cranelift cannot see through the tag
to keep it in a register as a raw integer; HotSpot's escape analysis and type
feedback routinely unbox across exactly those boundaries. `fib` — which is nothing
but "pass a number to a call, get a number back" — pays this on every one of its
~11.4M calls.

### 3. No cross-function inlining, and calls are the TEMEN calling convention

`fib` calls `fib`. In this pipeline nothing inlines across function boundaries, so
each call is a full frame setup passing/returning boxed values, plus a `jacl_lt`
call for `< n 2`. HotSpot inlines small recursive callees a few frames deep and
OSR-compiles the hot region. Cranelift is a fast-compile, moderate-optimize
backend (built for Wasmtime): solid register allocation, limited GVN/LICM, **no
speculative optimization, no profile-guided deopt/reopt**. It generates decent
code from good input but does far less than C2/TurboFan.

### 4. Real allocation + GC where the JVM elides it entirely

`box_churn` allocates 3M short-lived single-slot containers. JACL actually
allocates each one on the heap and the GC later reclaims it. HotSpot's escape
analysis proves the equivalent `(vector 7)` doesn't escape and **scalar-replaces it
— the allocation disappears**. That is the whole 13 ms vs 134 ms difference: not
"the JVM allocates faster," but "the JVM doesn't allocate at all." A bump/arena
allocator for provably-non-escaping boxes, or escape analysis in the typer, would
close most of it.

### 5. The typer's coverage bounds how much of this ever gets fast

Every fast path above is gated on the static typer proving a concrete type
(`inferred_type == TYPE_I32`). Code the typer can't prove stays fully dynamic —
boxed values, runtime-call dispatch. Broadening type inference directly widens the
fraction of a program that can use native ops and unboxed values.

### Where the gap is smaller, and why

On `map_lookup` JACL (204 ms) is much closer to Clojure (69 ms) — because
Clojure's *own* weak spot shows up there: its immutable persistent hash-map costs
more per lookup than Python's `dict` or V8's object. When the workload leans on a
data structure rather than raw arithmetic, the arithmetic-lowering gap matters
less and the field levels out.

**Bottom line:** the ranking is dominated by items 1–2 — dynamic dispatch on hot
scalar ops and pervasive boxing — both of which are lowering choices with clear
paths forward, not inherent limits of the TEMEN backend. The migration already
bought an 8–25× win over the old VM; closing more of the distance to the mature
JITs is mostly a matter of extending the typed-native lowering that `+ − *`
already demonstrates.

## Results — after #98 (inline monomorphic guard + the three call-shaped costs)

#98 stopped codegen and the runtime from making an out-of-line call for work that is a few
instructions: the statement-position error check, i32 equality, the `box` snapshot deep-copy,
and — the big one — an inline tag guard so a dynamic binop on two plain i32s computes natively
and only calls `jacl_add` & co. on the paths the guard does not prove.

Same protocol as the section above (load/compile once, warm, minimum of 15 timed runs,
`_baseline` compile tax subtracted), all three columns measured back to back on the same
container. **Cross-machine comparison with the tables above is invalid** — this box measured
`sieve` at 431 ms where the 2026-09-11 table recorded 436 ms, so it is close, but re-measure
before comparing.

Times in **milliseconds, execution-only**.

| Scenario | before #98 | items 1–3 | items 1–4 | total |
|---|--:|--:|--:|--:|
| `fib` | 412 | 403 | **228** | 1.81× |
| `sieve` | 431 | 354 | **103** | 4.18× |
| `map_lookup` | 365 | 353 | **220** | 1.66× |
| `box_churn` | 212 | 182 | **132** | 1.61× |
| `string_concat` | 70 | 85 | **82** | ~1× (noise) |

`string_concat` builds strings with `jacl_str_concat`, which has no native lowering; its raw
per-run figures (364.3 / 366.4 / 368.9 ms including the ~285 ms compile tax) differ by 1.3%,
so the apparent regression is the baseline subtraction amplifying noise, not a real cost.

### Calls per operation

Counted with the `callprof` cdylib feature (`temen_callprof_reset` / `_dump`), which tallies
every runtime-helper call a guest makes, on the browser's bytecode engine:

| program | before #98 | items 1–3 | items 1–4 |
|---|--:|--:|--:|
| `sieve_10k` (10k primes) | 4,662,826 | 3,309,534 | **59** |
| `box_100k` (100k boxes) | 1,100,047 | 800,047 | **500,046** |

`sieve_10k`'s inner loop went from **seven calls per iteration to none** — `jacl_add`,
`jacl_mul`, `jacl_mod`, `jacl_le`, `jacl_eq`, `jacl_val_equal` and `jacl_is_error_v` all left
the profile; the 59 that remain are program start-up. Its wall clock on that engine went
3822 → 1694 ms.

`box_100k`'s remaining 500k are the allocation itself, five per iteration: `jacl_alloc`,
`jacl_alloc_off`, `jacl_gc_safepoint`, `jacl_box_new` and `jacl_box_get`. Nothing in #98
addresses those — that is the escape-analysis item in §4 above.

### Emitted size

The guard is a three-block diamond, so the emitted IR grows: `sieve_10k`'s program function
went 3,404 → 9,248 estimated emitted bytes. That is well inside the browser host's
proven-safe band (≤ 263,614 B per emitted function) and, incidentally, now above the
4,096 B coop tier-up floor that had kept it interpreted.

## Results — after #100 (sibling operands pinned; the guard reaches argument position)

#98's guard only fired where the consumer already tolerated a block move — statement
position, conditions, binding initializers, tail/return values — because an operand that
moves the emission point strands its siblings (jacl #100, a bug that predated the guard).
#100 pins sibling operands at every multi-operand site, which both fixes that and lets the
guard reach nested operand positions: `[fib [- $n 1]]`'s subtraction, a `[vec [+ $a $b] …]`
element, a proc-call argument.

Same protocol, both columns measured back to back on one container.

| Scenario | before #100 | after #100 | |
|---|--:|--:|--:|
| `fib` | 225 | **123** | 1.84× |
| `sieve` | 97.0 | 96.9 | — (already at zero calls after #98) |
| `map_lookup` | 223 | **208** | 1.07× |
| `box_churn` | 132 | **125** | 1.06× |
| `string_concat` | 77 | **73** | 1.06× |

ms, execution-only, minimum of 15 runs, `_baseline` compile tax subtracted.

`fib` is where argument-position arithmetic lives, and it nearly halves. Cumulatively
across #98 + #100 it is **412 → 123 ms, 3.35×** — past Lua 5.4 (433) and CPython (918),
within 1.25× of Node 22 (98), and 1.7× off Clojure/HotSpot (72).

### Calls per operation

`fib 20` on the browser bytecode engine, by helper:

| | before #100 | after #100 |
|---|--:|--:|
| `jacl_sub` (the two `[- $n …]` arguments) | 21,890 | **0** |
| recursive `fib` calls | 21,891 | 21,891 |
| total | 43,803 | **21,913** |

Every runtime-helper call is gone from `fib`; what remains is the recursion itself, which
is the function-call-dispatch item, not arithmetic. `sieve_10k` (59) and `box_100k`
(500,046) are unchanged — #98 had already taken their arithmetic inline.

### Emitted size

| program | before #100 | after #100 |
|---|--:|--:|
| `fib 20` | 2,268 | 3,522 |
| `sieve_10k` | 9,248 | 9,248 |
| `box_100k` | 3,176 | 3,176 |

Estimated emitted bytes for the program's own function. Only `fib` grows (it is the one
that gained guards), and every figure stays far inside the browser host's proven-safe band
(≤ 263,614 B per emitted function).
