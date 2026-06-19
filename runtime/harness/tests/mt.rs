//! P3.4b — concurrent allocation across real vCPU workers (thread.spawn) over the
//! per-vCPU heap allocator. Differentially checked interp == JIT by `run_test`.
use jacl_runtime_harness::run_test;

#[test]
fn alloc_mt() {
    assert_eq!(run_test("test_alloc_mt.c", 0), 777,
        "N vCPU workers allocate concurrently into per-vCPU heaps; counts + stamps intact");
}

#[test]
fn gc_sched() {
    let r = run_test("test_gc_sched.c", 0);
    assert_eq!(r, 999,
        "multi-vCPU STW over the fiber scheduler: each worker's keeper survives a \
         collection run by another worker; no mutual-exclusion violation (diag {r})");
}
