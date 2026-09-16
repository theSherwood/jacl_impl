//! P3.4b — concurrent allocation across real vCPU workers (thread.spawn) over the
//! per-vCPU heap allocator. Differentially checked interp == JIT by `run_test`.
use jacl_runtime_harness::run_test;

#[test]
fn alloc_mt() {
    assert_eq!(run_test("test_alloc_mt.c", 0), 777,
        "N vCPU workers allocate concurrently into per-vCPU heaps; counts + stamps intact");
}

/* ---- jacl #141: a spawned vCPU needs its own in-window data stack ---- */

#[test]
fn a_spawned_vcpu_needs_its_own_data_stack() {
    // `thread.spawn` takes the new vCPU's data-stack base and reserves nothing for it — only the
    // root gets a stack carved out of the window. Every spawn in `sched.c` used to pass
    // `(void *)0`, which based the worker's stack at address 0; the first frame it needed faulted,
    // and the three scheduler tests below died with `MemoryFault` on **both** backends. It went
    // unnoticed for so long because the fault is conditional — a worker whose locals all stay in
    // SSA registers never touches its data stack, which is why `alloc_mt` passed regardless.
    //
    // Given a real per-worker stack, the same framed worker runs:
    assert_eq!(run_test("test_vcpu_stack.c", 0), 141,
        "a spawned worker with a real data stack computes its framed sum");

    // ...and with `(void *)0` it cannot. A trap ends the run, so this is asserted from out here
    // rather than from inside the guest. If a VM change ever makes a NULL stack work, this goes
    // red and the ruling gets revisited on purpose.
    let (interp, jit) = jacl_runtime_harness::run_test_outcome("test_vcpu_stack_null.c", 0);
    assert_eq!(interp, "Trap(MemoryFault)", "sp = 0 must fault on the interpreter");
    assert_eq!(jit, "Trap(MemoryFault)", "sp = 0 must fault on the JIT too — it is not a \
         simulation artifact");
}

/* The three multi-worker scheduler tests below were ignored twice over, for two stacked defects.
 * The first was the `MemoryFault` from the NULL vCPU data stacks fixed above (jacl #141). With that
 * gone they reached the runtime's stop-the-world GC barrier and livelocked there on BOTH backends
 * (jacl #142): `jacl_gc_collect` ran inside a task fiber, and a `memory.wait` issued from a fiber
 * parks the FIBER, not the OS thread (temen §3.6 slice 5a) — so the elected collector's quiesce
 * wait handed its vCPU back to its own scheduler loop with the collection unfinished and the GC
 * lock held, and that loop then parked the vCPU waiting for the `jacl_gc_done` its own parked
 * fiber owed it. Every other worker queued behind the held lock. The fix splits the election from
 * the waiting: an in-task winner suspends, and its scheduler loop runs the barrier + mark-sweep on
 * the OS thread (`jacl_gc_quiesce_and_sweep`). All three are gated here now, on interp AND JIT. */
#[test]
fn sched_batch() {
    let r = run_test("test_sched_mt.c", 0);
    assert_eq!(r, 888,
        "reusable worker pool runs a task batch across vCPUs, GC-safe; results + keepers \
         intact, no violation (diag {r})");
}

#[test]
fn batch_heap() {
    let r = run_test("test_batch_heap.c", 0);
    assert_eq!(r, 808,
        "batch scheduler roots in-flight heap args + heap results across a collection (diag {r})");
}

#[test]
fn gc_sched() {
    let r = run_test("test_gc_sched.c", 0);
    assert_eq!(r, 999,
        "multi-vCPU STW over the fiber scheduler: each worker's keeper survives a \
         collection run by another worker; no mutual-exclusion violation (diag {r})");
}

#[test]
fn par_min() {
    let r = run_test("test_par_min.c", 0);
    assert_eq!(r, 111, "continuation-pool jacl_parallel returns correct results (diag {r})");
}

#[test]
fn par_gc() {
    // The continuation pool is GC-sound under heavy concurrent collection: the job registry roots
    // every live job, so a job (incl. the program root) is never swept mid-collection. This was
    // JIT-only on the theory that the temen *interpreter*'s cooperative single-thread scheduler
    // livelocked on the pool's futex traffic. That was a misattribution: the livelock was jacl
    // #142 in our own GC barrier (see the note above), and with that fixed the interpreter drives
    // this case fine — so it is back on the differential oracle, where it belongs.
    let r = run_test("test_par_gc.c", 0);
    assert_eq!(r, 555,
        "continuation pool: parallel runs NT allocating blocks across pinned workers while they \
         force collections; every keeper survives, results match, no violation (diag {r})");
}


#[test]
fn job_gc() {
    // Jobs are plain GC objects: completed rounds get reclaimed (live-count bound) and a future
    // held across many collections stays re-awaitable. Differential again for the same reason as
    // par_gc — the "interp cooperative-scheduler livelock" this was pinned to was jacl #142.
    let r = run_test("test_job_gc.c", 0);
    assert_eq!(r, 555, "jobs are GC'd when dead, live while held (diag {r})");
}
