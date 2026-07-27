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

`_baseline.jacl` (`print 0`) exists only to measure the SVM JIT's per-run compile
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
| **SVM JIT** (new) | `runtime/harness` → `bench_svm` | runtime translation + Cranelift compile |
| CPython 3.11 | `pytimer` (below) | interpreter startup, import |
| Node 22 (V8) | `nodetimer` (below) | process startup, TurboFan warm-up |
| Clojure 1.12 (JVM) | `_bench_runner.clj` (this suite) | JVM startup, HotSpot C2 warm-up |

## Results

Times in **milliseconds, minimum of timed runs, execution-only**. Single machine,
best-of-N; treat as order-of-magnitude with ±10–15% run-to-run noise.

| Scenario | Old VM | **SVM JIT** | Python 3.11 | Node 22 | Clojure/JVM |
|---|--:|--:|--:|--:|--:|
| `fib` | 3384 | **258** | 613 | 60 | 54 |
| `sieve` | 2954 | **255** | 342 | 21 | 5.0 |
| `map_lookup` | 1588 | **204** | 195 | 23 | 69 |
| `box_churn` | 3361 | **134** | 114 | 1.9 | 13 |
| `string_concat` | OOM¹ | **53** | 4.6 | 0.9 | 13 |

¹ The old VM exhausts its GC (1.5 MB threshold) on the scaled `string_concat` and
aborts. It completes at the unscaled size.

**SVM JIT** figures are execution-only with the harness's ~198 ms/run Cranelift
recompile subtracted (a one-time cost under real AOT use). Even *without*
subtracting — counting a full recompile on every run — the JIT still beats the old
VM ~7× on `fib`. The raw per-run JIT figures (incl. compile) are `fib` 455,
`sieve` 452, `map_lookup` 402, `box_churn` 331, `string_concat` 250 ms.

**Headline:** the migration from the bytecode VM to SVM made JACL **8–25× faster**
(13× on `fib`) with identical results, landing it around CPython. The two mature
JITs — V8 and the JVM's HotSpot — are still ahead.

## Reproducing

From the repo root:

```sh
# Old bytecode VM (execution-only, compile-once/run-N)
./build.sh --test=perf
BENCH_SCENARIOS="fib_recursive,sieve_primes,map_lookup_hot" ./.build/perf
#   NOTE: test_perf's table points at the UNSCALED test/jacl/bench dir. To time
#   the scaled workloads, temporarily copy bench_scaled/<x>.jacl over the matching
#   bench/<name>.jacl the table expects, run, then restore.

# SVM JIT + interp (execution-only; interp is the slow correctness oracle)
cd runtime/harness
BENCH_NO_INTERP=1 cargo run --release --bin bench_svm -- \
  ../../test/jacl/bench_scaled/_baseline.jacl \
  ../../test/jacl/bench_scaled/fib.jacl  # … etc

# Clojure (warmed JVM)
clojure -M test/clojure/bench_scaled/_bench_runner.clj

# Python 3.11 — save as pytimer.py, then: python3 pytimer.py test/python/bench_scaled/fib.py
#   import module, warm 3×, time run() 10×, report min/median (perf_counter)
# Node 22   — save as nodetimer.mjs, then: node nodetimer.mjs <abs path>/fib.mjs
#   dynamic import, warm 3×, time run() 10×, report min/median (performance.now)
```

The `bench_svm` and `test_perf` harnesses are part of the SVM migration work; a
full run of all five columns requires that code to be present.

---

# Why is JACL so much slower than Clojure?

Clojure runs on the JVM — one of the most heavily optimized runtimes ever built —
so being 3–5× behind it (and ~50× on `sieve`) is not surprising in absolute
terms. But the *shape* of the gap points at specific, fixable things in the JACL →
SVM lowering, not at a fundamental ceiling. Concretely:

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
instructions (`cmp`, `idiv`/`irem`). Clojure finishes `sieve` in 5 ms; the SVM JIT
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

### 3. No cross-function inlining, and calls are the SVM calling convention

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
paths forward, not inherent limits of the SVM backend. The migration already
bought an 8–25× win over the old VM; closing more of the distance to the mature
JITs is mostly a matter of extending the typed-native lowering that `+ − *`
already demonstrates.
