//! probe_svmb — sizing experiment for docs/SVM_BROWSER_PLAN.md's "op coverage" blocker.
//!
//! Mirrors `emit_svmb` up to `synth_manifest_start`, then instead of encoding it
//! *measures* the linked module:
//!   1. Does the bytecode engine accept the linked module as-is? (`compile_module`)
//!   2. Does `svm_opt::optimize_module` (devirt + inter-fn DCE) prune it, and does
//!      the pruned module then fit the bytecode subset?
//!   3. Which *specific* declined ops/shapes remain, and are they reachable from _start?
//!   4. Does the wasm-JIT mixed tier accept it (`analyze` / after `outline_cap_calls`)?
//!
//!   probe_svmb <prog.jacl>
//!
//! Findings + method: docs/SVM_BROWSER_SPIKE_FINDINGS.md. As of the `vcpu.tls` lowering
//! and the `gc.roots + thread` veto removal (vendored svm), a linked JACL program now
//! **compiles and runs on the bytecode engine** with no engine patch — the EXECUTION
//! section below reports `compile_module ACCEPTED` and correct stdout directly.

use std::path::PathBuf;
use std::process::Command;

use std::sync::Arc;

use svm_ir::{Inst, LinkUnit, Module};
use svm_interp::{bytecode, cap_id, Region, Value};

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");
const CAP_SELF: u32 = u32::MAX;

fn build_driver() -> PathBuf {
    let out = std::env::temp_dir().join(format!("probe_svmb_driver_{}", std::process::id()));
    let status = Command::new("gcc")
        .args(["-O0", "-w", "-D_DEFAULT_SOURCE"])
        .arg(format!("{ROOT}/codegen/tests/emit_jacl.c"))
        .arg(format!("{ROOT}/codegen/codegen.c"))
        .arg(format!("{ROOT}/codegen/irbuilder.c"))
        .arg("-o")
        .arg(&out)
        .args(["-lm", "-lpthread"])
        .status()
        .expect("spawn gcc");
    assert!(status.success(), "gcc failed to build the codegen driver");
    out
}

fn link_program(prog: &PathBuf) -> Module {
    let driver = build_driver();
    let out = Command::new(&driver).arg("--file").arg(prog).output().expect("run emit driver");
    if !out.status.success() {
        eprint!("{}", String::from_utf8_lossy(&out.stderr));
        std::process::exit(1);
    }
    let program =
        jacl_runtime_harness::decode_emitted(&out.stdout).expect("decode emitted IR");

    let rt = jacl_runtime_harness::translate_runtime();
    let cat = jacl_runtime_harness::translate_catalog();
    let linked = svm_ir::link_with_manifest(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit { module: cat.module, exports: cat.exports, ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![("__jacl_entry".to_string(), 0)],
            ..Default::default()
        },
    ])
    .expect("link");
    let entry = linked.resolve_export("__jacl_entry").expect("entry export missing after link");
    svm_ir::synth_manifest_start(linked, entry, false).expect("powerbox")
}

/// Is this CapCall a shape the *bytecode* engine declines? (Mirrors
/// svm-interp/src/bytecode.rs compile_inst, arms at 1339-1450.)
fn declined_shape(type_id: u32, op: u32, nargs: usize, nresults: usize) -> Option<String> {
    match type_id {
        cap_id::INSTANTIATOR => {
            let ok = matches!(op, 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 14);
            if ok { None } else { Some(format!("INSTANTIATOR op {op} (unsupported variant)")) }
        }
        cap_id::YIELDER => {
            if op == 0 && nargs >= 1 { None } else { Some(format!("YIELDER op {op}")) }
        }
        cap_id::SHARED_REGION if op == 4 => Some("SHARED_REGION op 4 (grant into child)".into()),
        CAP_SELF if op == 9 || op == 10 => {
            if nresults == 1 && nargs == 0 {
                None // canonical svc.poll/svc.wait — supported
            } else {
                Some(format!("cap.self op {op} (svc.{}, non-canonical shape: {nargs} args / {nresults} results)",
                    if op == 9 { "poll" } else { "wait" }))
            }
        }
        _ => None,
    }
}

#[derive(Default, Clone, Copy)]
struct Seams {
    coro: bool, fiber: bool, thread: bool, instantiate: bool, gc: bool, svc: bool, park: bool,
}

fn scan_module(m: &Module, reachable: &[bool]) -> (Seams, Seams, Vec<(usize, String, bool)>, [usize; 3]) {
    let mut s = Seams::default();       // over ALL functions (what scan_seams sees today)
    let mut sr = Seams::default();      // restricted to functions reachable from _start
    let mut declined: Vec<(usize, String, bool)> = Vec::new();
    let mut indirect = [0usize; 3];     // [call_indirect, cont.new, ref.func] among reachable
    for (fi, f) in m.funcs.iter().enumerate() {
        let r = reachable.get(fi).copied().unwrap_or(false);
        macro_rules! set { ($field:ident) => {{ s.$field = true; if r { sr.$field = true; } }} }
        for b in &f.blocks {
            if r {
                if matches!(b.term, svm_ir::Terminator::ReturnCallIndirect { .. }) { indirect[0] += 1; }
            }
            for inst in &b.insts {
                match inst {
                    Inst::CapCall { type_id, op, sig, args, .. } => {
                        match (*type_id, *op) {
                            (cap_id::INSTANTIATOR, 0 | 1 | 5) => set!(instantiate),
                            (cap_id::INSTANTIATOR, _) | (cap_id::YIELDER, _) => set!(coro),
                            (CAP_SELF, 9 | 10) => set!(svc),
                            _ => {}
                        }
                        if let Some(why) = declined_shape(*type_id, *op, args.len(), sig.results.len()) {
                            declined.push((fi, why, r));
                        }
                    }
                    Inst::CallIndirect { .. } => { if r { indirect[0] += 1; } }
                    Inst::CallImport { .. } | Inst::SetJmp { .. } | Inst::LongJmp { .. } => set!(park),
                    Inst::ContNew { .. } => { set!(fiber); if r { indirect[1] += 1; } }
                    Inst::ContResume { .. } | Inst::Suspend { .. } => set!(fiber),
                    Inst::ThreadSpawn { .. } | Inst::ThreadJoin { .. }
                    | Inst::MemoryWait { .. } | Inst::MemoryNotify { .. } => set!(thread),
                    Inst::GcRoots { .. } => set!(gc),
                    Inst::RefFunc { .. } => { if r { indirect[2] += 1; } }
                    _ => {}
                }
            }
        }
    }
    (s, sr, declined, indirect)
}

fn seam_veto(s: &Seams) -> Option<&'static str> {
    let svc_park = s.svc && (s.park || s.fiber || s.thread || s.coro || s.instantiate);
    if s.coro && (s.fiber || s.thread) { Some("coro + fiber/thread") }
    else if s.instantiate && s.fiber { Some("instantiate + fiber") }
    else if s.gc && s.thread { Some("gc.roots + thread") }
    else if svc_park { Some("svc.poll/wait coexisting with a park-capable seam (svc_park_veto)") }
    else { None }
}

fn report(tag: &str, m: &Module) {
    println!("\n===== {tag} =====");
    println!("functions: {}", m.funcs.len());

    let a = svm_wasm_jit::analyze(m);
    let reach: Vec<bool> = a.reachable.clone();
    let nreach = reach.iter().filter(|&&r| r).count();

    // bytecode engine verdict
    let bc = bytecode::compile_module(&m.funcs);
    println!("bytecode::compile_module (all-or-nothing): {}",
        if bc.is_some() { "ACCEPTED ✓" } else { "None ✗ (STATUS_UNSUPPORTED)" });

    // seams + declined census
    let (s, sr, declined, indirect) = scan_module(m, &reach);
    println!("seams (ALL funcs):       coro={} fiber={} thread={} instantiate={} gc={} svc={} park={}",
        s.coro as u8, s.fiber as u8, s.thread as u8, s.instantiate as u8, s.gc as u8, s.svc as u8, s.park as u8);
    println!("seams (REACHABLE only):  coro={} fiber={} thread={} instantiate={} gc={} svc={} park={}",
        sr.coro as u8, sr.fiber as u8, sr.thread as u8, sr.instantiate as u8, sr.gc as u8, sr.svc as u8, sr.park as u8);
    match seam_veto(&s) {
        Some(v) => println!("module veto (today, ALL funcs):     FIRES -> {v}"),
        None => println!("module veto (today, ALL funcs):     none"),
    }
    match seam_veto(&sr) {
        Some(v) => println!("module veto (if reachability-aware): FIRES -> {v}"),
        None => println!("module veto (if reachability-aware): NONE — would clear ✓"),
    }
    println!("indirect-dispatch (reachable): call_indirect={} cont.new={} ref.func={}  (any>0 disables DCE)",
        indirect[0], indirect[1], indirect[2]);
    println!("declined per-inst shapes: {} total ({} reachable from _start)",
        declined.len(), declined.iter().filter(|(_, _, r)| *r).count());
    // dedup by reason
    let mut seen = std::collections::BTreeMap::<String, (usize, usize)>::new();
    for (_, why, r) in &declined {
        let e = seen.entry(why.clone()).or_default();
        e.0 += 1;
        if *r { e.1 += 1; }
    }
    for (why, (cnt, rcnt)) in &seen {
        println!("   - {why}: {cnt} site(s), {rcnt} reachable");
    }

    // wasm-JIT mixed tier verdict
    let reach_neither = (0..m.funcs.len())
        .filter(|&i| reach[i] && !a.in_subset[i] && !a.interp_leaf[i])
        .count();
    println!("wasm-JIT analyze: mixed_ok={}  (reachable={}, reachable-neither-subset-nor-leaf={})",
        a.mixed_ok, nreach, reach_neither);

    // full census of CapCall (type_id, op) among REACHABLE functions
    let mut cap = std::collections::BTreeMap::<(u32, u32), usize>::new();
    for (fi, f) in m.funcs.iter().enumerate() {
        if !reach.get(fi).copied().unwrap_or(false) { continue; }
        for b in &f.blocks {
            for inst in &b.insts {
                if let Inst::CapCall { type_id, op, .. } = inst {
                    *cap.entry((*type_id, *op)).or_default() += 1;
                }
            }
        }
    }
    if !cap.is_empty() {
        print!("reachable cap.call census:");
        for ((t, o), c) in &cap {
            let name = match *t {
                cap_id::STREAM => "STREAM", cap_id::EXIT => "EXIT", cap_id::CLOCK => "CLOCK",
                cap_id::MEMORY => "MEMORY", cap_id::SHARED_REGION => "SHARED_REGION",
                cap_id::ADDRESS_SPACE => "ADDRSPACE", cap_id::INSTANTIATOR => "INSTANTIATOR",
                cap_id::YIELDER => "YIELDER", cap_id::MODULE => "MODULE", cap_id::JIT => "JIT",
                CAP_SELF => "cap.self", _ => "other",
            };
            print!("  {name}#{o}×{c}");
        }
        println!();
    }
}

/// Build a powerbox `Host` with the module's manifest imports bound by name (`write`→stdout,
/// `stdin`→stdin, `exit`→exit), mirroring `svm_run`'s manifest binding — so `print` actually routes
/// to a captured stdout buffer instead of faulting.
fn powerbox_host(m: &Module) -> svm_interp::Host {
    use svm_interp::{Host, StreamRole};
    let mut host = Host::new();
    let mut bindings = Vec::with_capacity(m.imports.len());
    for imp in &m.imports {
        let b = match imp.name.as_str() {
            "write" => (cap_id::STREAM, 1u32, host.grant_stream(StreamRole::Out)),
            "read" | "stdin" => (cap_id::STREAM, 0u32, host.grant_stream(StreamRole::In)),
            "exit" => (cap_id::EXIT, 0u32, host.grant_exit()),
            other => {
                eprintln!("probe: unbound import '{other}' — leaving CapFault");
                bindings.push(svm_interp::BoundImport {
                    type_id: 0, op: 0, handle: -1, bound: false, rebindable: false,
                });
                continue;
            }
        };
        bindings.push(svm_interp::BoundImport {
            type_id: b.0, op: b.1, handle: b.2, bound: true, rebindable: false,
        });
    }
    host.set_import_bindings(bindings);
    host
}

/// Run `_start` (func 0) on the single-vCPU engine vs the multi-vCPU cooperative/parallel driver
/// (`drive_parallel`, the native stand-in for the browser's per-vCPU Workers), both over a real
/// powerbox with `write` bound to a captured stdout. With the vendored svm carrying the `vcpu.tls`
/// lowering and the `gc.roots + thread` veto removal, both engines run a linked JACL program to
/// completion (correct stdout); before those two slices they returned `None` / trapped `Malformed`.
fn run_compare(m: &Module) {
    println!("\n===== EXECUTION: single-vCPU vs multi-vCPU (powerbox bound) =====");
    println!("imports: {:?}", m.imports.iter().map(|i| i.name.as_str()).collect::<Vec<_>>());

    let mut host = powerbox_host(m);
    let mut fuel = u64::MAX;
    let single = bytecode::compile_and_run_with_host(m, 0, &[], &mut fuel, &mut host);
    println!("single-vCPU  (compile_and_run_with_host):  {}   stdout={:?}",
        describe(&single), String::from_utf8_lossy(&host.stdout));

    // Size the shared backing to the module's declared window (`1 << size_log2`); JACL declares a
    // multi-MiB heap, so a fixed small Region would let the guest write past it and segfault.
    let size = m.memory.as_ref().map_or(1usize << 20, |mc| 1usize << mc.size_log2);
    let layout = std::alloc::Layout::from_size_align(size, 8).unwrap();
    let base = unsafe { std::alloc::alloc_zeroed(layout) };
    assert!(!base.is_null());
    let back = Arc::new(unsafe { Region::shared(base, size as u64) });
    let mut host = powerbox_host(m);
    let mut fuel = u64::MAX;
    let par = bytecode::compile_and_run_capture_over_parallel_with_host(
        m, 0, &[], &mut fuel, &[], Arc::clone(&back), &mut host,
    ).map(|(r, _)| r);
    println!("multi-vCPU   (drive_parallel):             {}   stdout={:?}",
        describe(&par), String::from_utf8_lossy(&host.stdout));
    drop(back);
    unsafe { std::alloc::dealloc(base, layout) };

    println!("(correct stdout on the multi-vCPU path = the gc.roots+thread machinery ran green;");
    println!(" Malformed = that engine cannot service JACL's startup.)");

    // Locate the trap: traced single-vCPU run gives the backtrace (innermost frame first).
    let mut host = powerbox_host(m);
    let mut fuel = u64::MAX;
    match bytecode::compile_and_run_with_host_traced(m, 0, &[], &mut fuel, &mut host) {
        None => println!("\ntraced run: declined (None) — the trap is at/after a concurrency seam the traced path won't cross"),
        Some((res, bt, _)) => {
            println!("\ntraced run: {}", describe(&Some(res)));
            if let Some(pc) = bt.first() {
                let f = pc.func as usize;
                println!("trapping frame: func {} block {} inst {}", pc.func, pc.block, pc.inst);
                // What distinctive ops does the trapping function contain?
                if let Some(func) = m.funcs.get(f) {
                    let mut kinds = std::collections::BTreeSet::new();
                    for b in &func.blocks {
                        for inst in &b.insts {
                            let k = match inst {
                                Inst::GcRoots { .. } => "gc.roots",
                                Inst::ThreadSpawn { .. } => "thread.spawn",
                                Inst::ThreadJoin { .. } => "thread.join",
                                Inst::MemoryWait { .. } => "memory.wait",
                                Inst::ContNew { .. } => "cont.new",
                                Inst::ContResume { .. } => "cont.resume",
                                Inst::Suspend { .. } => "suspend",
                                Inst::CapCall { type_id, .. } => {
                                    if *type_id == cap_id::INSTANTIATOR { "cap.instantiator" }
                                    else if *type_id == cap_id::YIELDER { "cap.yielder" }
                                    else if *type_id == CAP_SELF { "cap.self" }
                                    else { "cap.call" }
                                }
                                Inst::CallImport { .. } => "call.import",
                                _ => continue,
                            };
                            kinds.insert(k);
                        }
                    }
                    println!("trapping func's notable ops: {:?}", kinds);
                }
                println!("backtrace (func indices, inner→outer): {:?}",
                    bt.iter().map(|p| p.func).collect::<Vec<_>>());
                // Dump the exact trapping instruction (and a couple around it).
                if let Some(func) = m.funcs.get(f) {
                    if let Some(blk) = func.blocks.get(pc.block) {
                        let i = pc.inst;
                        let lo = i.saturating_sub(2);
                        for (j, inst) in blk.insts.iter().enumerate().skip(lo).take(5) {
                            let marker = if j == i { " <== TRAP" } else { "" };
                            println!("   f{}.b{}.i{}: {:?}{}", f, pc.block, j, inst, marker);
                        }
                        if i >= blk.insts.len() {
                            println!("   (inst {i} is the block terminator: {:?})", blk.term);
                        }
                    }
                }
            }
        }
    }
}

fn describe(r: &Option<Result<Vec<Value>, svm_interp::Trap>>) -> String {
    match r {
        None => "None (compile declined — a remaining seam veto fired)".into(),
        Some(Ok(v)) => format!("Ok({v:?})"),
        Some(Err(t)) => format!("Trap::{t:?}"),
    }
}

fn main() {
    let arg = std::env::args().nth(1).expect("usage: probe_svmb <prog.jacl>");
    let module = link_program(&PathBuf::from(&arg));
    println!("### probe: {arg}");

    report("LINKED (as the spike measured it — no optimizer)", &module);

    let opt = svm_opt::optimize_module(&module);
    report("AFTER svm_opt::optimize_module (devirt + inter-fn DCE)", &opt);

    let mut outlined = opt.clone();
    svm_wasm_jit::outline_cap_calls(&mut outlined);
    report("AFTER optimize_module + outline_cap_calls (mixed-tier prep)", &outlined);

    run_compare(&module);
}
