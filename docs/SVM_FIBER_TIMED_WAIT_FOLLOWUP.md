# Follow-up for svm — fiber timed-wait contract (PR #442), from the jacl consumer

**Status.** PR #442 (`2a88253d`, "align TreeWalk / Bytecode / JIT on the §3.6 5a park
contract") resolved the tree-walker regression reported in
`SVM_INTERP_TIMED_WAIT_REGRESSION.md` — thank you. jacl has adopted the new contract (a
`memory.wait` inside a fiber reports `FIBER_PARKED` at `cont.resume`, and the resumer
re-polls). The 8 sleep/concurrency parity cases pass `interp == jit` again with svm pinned at
`a45d80cc`. jacl's scheduler change is small (only `sleep` produces a fiber-level wait; its
cooperative worker re-polls parked fibers at its existing 1 ms idle cadence, so no busy-spin).

Two items are worth a look upstream — one trivial, one a genuine (non-blocking) design gap.

## 1. Trivial but load-bearing: a stale lowering comment that caused the outage

`svm-llvm`'s `__vm_fiber_resume` lowering stores the `cont.resume` status straight into the
C `int *done`, but its comment still reads:

```rust
// crates/svm-llvm/src/lib.rs  (__vm_fiber_resume)
}); // *done = status (0 suspended / 1 returned)
```

Since `a45d80cc` that field can also be `FIBER_PARKED (3)`. Any consumer that took the comment
at face value and wrote `if (done) { /* returned */ }` — as jacl did — now reads a park as a
*return-with-value-0* and drops the fiber's continuation. That is exactly how jacl broke on
**both** backends after #442 (the JIT previously blocked the OS thread and never surfaced a
park, so the boolean test had been accidentally correct).

Ask: update the comment (and the `__vm_fiber_resume` contract doc, wherever the intrinsic is
specified) to enumerate the real set — `0 = suspended (guest yield), 1 = returned, 3 = parked
(re-poll)`. One line, but it is the difference between a consumer getting this right or
shipping the bug we just chased.

## 2. Design gap: no way for a resumer to idle until a parked fiber is due

The contract fires a timed wait's deadline **at the next `cont.resume` poll** of the parked
fiber (and, for svm's *own* scheduler, at scheduler idle). That is the right default, but it
leaves a hole for a **guest scheduler that owns the loop on its vCPU**: svm's "fire at
scheduler idle" path only triggers when svm's vCPU task is itself non-runnable, which never
happens while the guest's worker loop is running. So a guest with a parked fiber and *no other
runnable work* has only two options:

- **busy-spin `cont.resume`** (svm's own `poll_loop` test kernel does exactly this) — the poll
  fires the deadline so it terminates, but it burns a core for the whole sleep; or
- **approximate**: park its *own* vCPU on an unrelated `memory.wait` for a guessed duration,
  then re-poll (what jacl does, at a 1 ms granularity that happens to match its sleep chunking).

Neither is great in general. jacl gets away with the second because its sleeps are already
1 ms-quantized and single-vCPU-cooperative; a consumer wanting precise, low-latency, low-CPU
sleeps across many distinct deadlines has no clean primitive.

Suggested shape (only if/when a second consumer needs it — no urgency for jacl): a **blocking
resume variant** — `cont.resume` that, when the fiber is still parked, blocks *this* vCPU until
that fiber is due or woken, then delivers. The resumer opts in only when it has nothing else to
run (it knows that), so svm still never has to understand guest scheduling, and the
non-blocking poll stays the default for interleaving. That fills the gap without growing the
core's responsibilities.

## Minor notes (no action requested — flagging for the record)

- **Idle-time firing is not clock-portable.** Only the *poll-time* firing is wall-clock across
  all three backends; the *idle* firing is logical-clock on the bytecode-cooperative driver and
  the explorer vs wall-clock on the tree-walker. The guaranteed contract point (the poll) is
  consistent, so this is fine — worth a doc line so no one differential-pins the exact idle
  wake moment.
- **Explorer divergence for `FIBER_PARKED`-observing kernels.** The deterministic explorer
  keeps whole-vCPU logical-clock parks and never yields `FIBER_PARKED`, so a kernel that
  *counts* park transitions or measures inter-poll timing diverges between explorer and Real.
  Already sanctioned (invariant-9 tiering) and correctly kept off `run_scheduled` in your own
  suite; harmless for jacl (its parity uses `run_diff` = Real). Noting so it stays intentional.
- **I45 residues** (opt-in parallel bytecode driver + browser `Vcpu` driver still whole-vCPU
  park) are confirmed off the `Instance::run` path, so they don't affect jacl.
- **Unverified:** teardown of a **bytecode** `WaitParked` fiber (the JIT's #440 teardown
  preservation is explicit; the bytecode cooperative driver's behavior when a vCPU is torn down
  mid-`WaitParked` wasn't obvious from the diff). Low risk for jacl — a `run_diff` mismatch
  would surface it — but worth a confirming test upstream.

## Cross-references

- Original regression report: `docs/SVM_INTERP_TIMED_WAIT_REGRESSION.md`.
- The svm contract + tests: `crates/svm/tests/fiber_timed_wait.rs`,
  `crates/svm-interp/tests/fiber_parks.rs`.
- jacl's adoption: `runtime/sched.c` (`resume_job` / `worker_loop` `FIBER_PARKED` handling),
  this branch (`claude/svm-refresh-manifest-migration`, svm pinned at `a45d80cc`).
