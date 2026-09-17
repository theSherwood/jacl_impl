//! probe_temen — sizing experiment for docs/TEMEN_BROWSER_PLAN.md's "op coverage" blocker.
//!
//! Mirrors `emit_temen` up to `synth_manifest_start`, then instead of encoding it
//! *measures* the linked module:
//!   1. Does the bytecode engine accept the linked module as-is? (`compile_module`)
//!   2. Does `temen_opt::optimize_module` (devirt + inter-fn DCE) prune it, and does
//!      the pruned module then fit the bytecode subset?
//!   3. Which *specific* declined ops/shapes remain, and are they reachable from _start?
//!   4. Does the wasm-JIT mixed tier accept it (`analyze` / after `outline_cap_calls`)?
//!
//!   probe_temen <prog.jacl>
//!
//! Findings + method: docs/TEMEN_BROWSER_SPIKE_FINDINGS.md. As of the `vcpu.tls` lowering
//! and the `gc.roots + thread` veto removal (vendored temen), a linked JACL program now
//! **compiles and runs on the bytecode engine** with no engine patch — the EXECUTION
//! section below reports `compile_module ACCEPTED` and correct stdout directly.

use std::path::PathBuf;
use std::process::Command;

use std::sync::Arc;

use temen_ir::{Inst, LinkUnit, Module};
use temen_interp::{bytecode, cap_id, Region, Value};

const ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");
const CAP_SELF: u32 = u32::MAX;

fn build_driver() -> PathBuf {
    let out = std::env::temp_dir().join(format!("probe_temen_driver_{}", std::process::id()));
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
    let linked = temen_ir::link_with_manifest(&[
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
    temen_ir::synth_manifest_start(linked, entry, false).expect("powerbox")
}

/// Is this CapCall a shape the *bytecode* engine declines? (Mirrors
/// temen-interp/src/bytecode.rs compile_inst, arms at 1339-1450.)
fn declined_shape(type_id: u32, op: u32, nargs: usize, nresults: usize) -> Option<String> {
    match type_id {
        cap_id::INSTANTIATOR => {
            let ok = matches!(op, 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 14);
            if ok { None } else { Some(format!("INSTANTIATOR op {op} (unsupported variant)")) }
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
                if matches!(b.term, temen_ir::Terminator::ReturnCallIndirect { .. }) { indirect[0] += 1; }
            }
            for inst in &b.insts {
                match inst {
                    Inst::CapCall { type_id, op, sig, args, .. } => {
                        match (*type_id, *op) {
                            (cap_id::INSTANTIATOR, 0 | 1 | 5) => set!(instantiate),
                            (cap_id::INSTANTIATOR, _) => set!(coro),
                            (CAP_SELF, 9 | 10) => set!(svc),
                            _ => {}
                        }
                        // #922: CapCall.sig is a Module::types index, resolve it to the func type.
                        let nres = match m.types.get(*sig as usize) {
                            Some(temen_ir::TypeEntry::Func(ft)) => ft.results.len(),
                            _ => 0,
                        };
                        if let Some(why) = declined_shape(*type_id, *op, args.len(), nres) {
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

    let a = temen_wasm_jit::analyze(m);
    let reach: Vec<bool> = a.reachable.clone();
    let nreach = reach.iter().filter(|&&r| r).count();

    // bytecode engine verdict
    let bc = bytecode::compile_module(&m.funcs, &m.types, None);
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

    // Name the blockers. `analyze` yields booleans only, so the reason is re-derived here from the
    // disqualifying instruction kinds each one contains — which is the actionable part: it says
    // whether a function is out because of concurrency (a runtime-build question, jacl #96) or
    // because of a capability import or an unlowered shape (a different fix entirely).
    if reach_neither > 0 {
        let mut names = std::collections::BTreeMap::<usize, &str>::new();
        for e in &m.exports {
            names.insert(e.func as usize, e.name.as_str());
        }
        println!("   blockers (reachable, neither in-subset nor an interp leaf):");
        for i in 0..m.funcs.len() {
            if !(reach[i] && !a.in_subset[i] && !a.interp_leaf[i]) { continue; }
            let mut why: Vec<&str> = Vec::new();
            let push = |w: &'static str, v: &mut Vec<&'static str>| {
                if !v.contains(&w) { v.push(w); }
            };
            for b in &m.funcs[i].blocks {
                for inst in &b.insts {
                    match inst {
                        Inst::AtomicLoad { .. } | Inst::AtomicStore { .. }
                        | Inst::AtomicRmw { .. } | Inst::AtomicCmpxchg { .. } => push("atomics", &mut why),
                        Inst::MemoryWait { .. } | Inst::MemoryNotify { .. } => push("futex", &mut why),
                        Inst::ContNew { .. } | Inst::ContResume { .. }
                        | Inst::Suspend { .. } => push("fibers", &mut why),
                        Inst::ThreadSpawn { .. } | Inst::ThreadJoin { .. } => push("threads", &mut why),
                        Inst::GcRoots { .. } => push("gc.roots", &mut why),
                        Inst::CapCall { .. } => push("cap.call", &mut why),
                        Inst::CallImport { .. } => push("call.import", &mut why),
                        _ => {}
                    }
                }
            }
            if why.is_empty() { why.push("other (shape/size/non-subset callee)"); }
            println!("     f{:<4} {:<34} {}", i, names.get(&i).copied().unwrap_or("-"), why.join(", "));
        }
    }

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
                cap_id::SHARED_REGION => "SHARED_REGION",
                cap_id::ADDRESS_SPACE => "ADDRSPACE", cap_id::INSTANTIATOR => "INSTANTIATOR",
                cap_id::MODULE => "MODULE", cap_id::JIT => "JIT",
                CAP_SELF => "cap.self", _ => "other",
            };
            print!("  {name}#{o}×{c}");
        }
        println!();
    }
}

/// Build a powerbox `Host` with the module's manifest imports bound by name (`write`→stdout,
/// `stdin`→stdin, `exit`→exit), mirroring `temen_run`'s manifest binding — so `print` actually routes
/// to a captured stdout buffer instead of faulting.
fn powerbox_host(m: &Module) -> temen_interp::Host {
    use temen_interp::{Host, StreamRole};
    let mut host = Host::new();
    let mut bindings = Vec::with_capacity(m.imports.len());
    for imp in &m.imports {
        let b = match imp.name.as_str() {
            "write" => (cap_id::STREAM, 1u32, host.grant_stream(StreamRole::Out)),
            "read" | "stdin" => (cap_id::STREAM, 0u32, host.grant_stream(StreamRole::In)),
            "exit" => (cap_id::EXIT, 0u32, host.grant_exit()),
            other => {
                eprintln!("probe: unbound import '{other}' — leaving CapFault");
                bindings.push(temen_interp::BoundImport {
                    type_id: 0, op: 0, handle: -1, bound: false, rebindable: false,
                });
                continue;
            }
        };
        bindings.push(temen_interp::BoundImport {
            type_id: b.0, op: b.1, handle: b.2, bound: true, rebindable: false,
        });
    }
    host.set_import_bindings(bindings);
    host
}

/// Run `_start` (func 0) on the single-vCPU engine vs the multi-vCPU cooperative/parallel driver
/// (`drive_parallel`, the native stand-in for the browser's per-vCPU Workers), both over a real
/// powerbox with `write` bound to a captured stdout. With the vendored temen carrying the `vcpu.tls`
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

fn describe(r: &Option<Result<Vec<Value>, temen_interp::Trap>>) -> String {
    match r {
        None => "None (compile declined — a remaining seam veto fired)".into(),
        Some(Ok(v)) => format!("Ok({v:?})"),
        Some(Err(t)) => format!("Trap::{t:?}"),
    }
}

fn main() {
    let arg = std::env::args().nth(1).expect("usage: probe_temen <prog.jacl>");
    let module = link_program(&PathBuf::from(&arg));
    println!("### probe: {arg}");

    report("LINKED (as the spike measured it — no optimizer)", &module);

    let opt = temen_opt::optimize_module(&module);
    report("AFTER temen_opt::optimize_module (devirt + inter-fn DCE)", &opt);

    let mut outlined = opt.clone();
    temen_wasm_jit::outline_cap_calls(&mut outlined);
    report("AFTER optimize_module + outline_cap_calls (mixed-tier prep)", &outlined);

    coop_report(&outlined);

    run_compare(&module);
}

/// Replica of the browser driver's `coop_emit_for` eligibility chain (`browser/src/lib.rs`), so the
/// **cooperative tier-up** decision is measurable from this repo without a cdylib, a built asset set
/// or a JS driver.
///
/// Why this exists separately from `report`'s `analyze` line: they answer different questions and
/// only one of them is the question jacl #96 / #83 are actually about.
///
///   * `analyze` uses the **strict** `interp_leaf` table — a cross-tier callee must be a
///     memory-free, call-free leaf. It reports `mixed_ok`, which greps show gates nothing in the
///     engine (tests and one comment only).
///   * `coop_emit_for` uses the **#888 widened** B2 table — any `marshallable_sig` non-in-subset
///     function can be a cross-tier callee over the live bounce. This is what actually runs, and
///     what temen #1552 changed by adding `uses_gc_roots()` to `bounce_serviceable`'s seeds.
///
/// The chain, mirrored below in the driver's order: `outline_cap_calls` → pick paged / B2 / local
/// table → `compile_module_tierup*` → `emit[i]` → `emittable_leaf = emit && all-i64 sig` →
/// `eligible = leaf && est_emitted_size >= floor` → decline iff no leaf and no `vm_jit_*` import.
///
/// Two deliberate divergences from the driver, both noted in the output:
///   * `onramp_check` is browser-internal, so it is not applied here; the real driver gates on it
///     first and may decline a card this reports on.
///   * the driver bumps the emit module's `size_log2` to the *run window*; we keep the module's own,
///     since there is no window without a run.
fn coop_report(m0: &Module) {
    println!("\n===== COOP TIER-UP: the #888 widened B2 table (what `coop_emit_for` runs) =====");

    if m0.memory.is_none() {
        println!("no memory declared -> coop_emit_for returns STATUS_UNSUPPORTED");
        return;
    }

    let mut m = m0.clone();
    temen_wasm_jit::outline_cap_calls(&mut m);

    let scalar = |t: &temen_ir::ValType| {
        matches!(
            t,
            temen_ir::ValType::I32
                | temen_ir::ValType::I64
                | temen_ir::ValType::F32
                | temen_ir::ValType::F64
        )
    };
    let max_slots = temen_wasm_jit::XCALL_MAX_SLOTS;
    let all_shimmable = m.funcs.iter().all(|f| {
        f.params.iter().all(scalar)
            && f.results.iter().all(scalar)
            && f.params.len().max(f.results.len()) <= max_slots
    });
    // `tierup_table_log2` (browser/src/lib.rs): at least ONRAMP_JIT_TABLE_LOG2 = 10, grown so
    // `1 << log2` covers every function.
    let table_log2 =
        10u8.max((m.funcs.len().max(1) as u64).next_power_of_two().trailing_zeros() as u8);
    let paged = all_shimmable
        && (m.data.iter().any(|d| d.readonly) || temen_wasm_jit::module_uses_unmap_protect(&m));
    let page_log2 = temen_interp::host_page_size().trailing_zeros() as u8;

    let mode = if paged {
        "B2 paged"
    } else if all_shimmable {
        "B2"
    } else {
        "local table"
    };
    println!("emit mode: {mode}  (all_shimmable={all_shimmable}, paged={paged}, table_log2={table_log2})");

    let emitted_res = if paged {
        temen_wasm_jit::compile_module_tierup_b2_paged(&m, false, table_log2 as u32, page_log2)
    } else if all_shimmable {
        temen_wasm_jit::compile_module_tierup_b2(&m, false, table_log2 as u32)
    } else {
        temen_wasm_jit::compile_module_tierup(&m, false)
    };
    let (wasm, emit) = match emitted_res {
        Ok(v) => v,
        Err(e) => {
            println!("compile_module_tierup* FAILED ({e:?}) -> STATUS_UNSUPPORTED");
            return;
        }
    };

    let all_i64 =
        |ts: &[temen_ir::ValType]| ts.iter().all(|t| *t == temen_ir::ValType::I64);
    let emittable_leaf: Vec<bool> = m
        .funcs
        .iter()
        .enumerate()
        .map(|(i, f)| emit[i] && all_i64(&f.params) && all_i64(&f.results))
        .collect();

    let n_emit = emit.iter().filter(|&&e| e).count();
    let n_leaf = emittable_leaf.iter().filter(|&&e| e).count();
    println!(
        "emitted wasm: {} bytes | emit[]={}/{} funcs | emittable_leaf={} (emit && all-i64 sig)",
        wasm.len(),
        n_emit,
        m.funcs.len(),
        n_leaf
    );

    let jit_importer = m.imports.iter().any(|im| im.name.starts_with("vm_jit_"));
    if n_leaf == 0 && !jit_importer {
        println!("NO emittable leaf and no vm_jit_* import -> coop_emit_for DECLINES (STATUS_UNSUPPORTED)");
    } else if n_leaf == 0 {
        println!("no emittable leaf, but a vm_jit_* import is present -> opens for the §22-unit win only");
    }

    // The floor sweep. `eligible` is what the interpreter consults to raise a TierUp event, so
    // "how many functions would ever tier up" is a floor question on top of the emit set.
    let default_floor = temen_wasm_jit::MIN_TIERUP_EMITTED_FN_BYTES;
    println!("\ntier-up eligibility by floor (eligible = emittable_leaf && est_emitted_size >= floor):");
    for floor in [default_floor, 0usize] {
        let n_elig = m
            .funcs
            .iter()
            .zip(&emittable_leaf)
            .filter(|(f, &leaf)| leaf && temen_wasm_jit::est_emitted_size(f) >= floor)
            .count();
        let tag = if floor == default_floor { " (default)" } else { " (#83's experiment)" };
        println!("   floor={floor:<5}{tag:<22} eligible={n_elig}");
    }

    // Name what is eligible at floor 0 — #83's experiment forced exactly this set to tier up, and
    // the question temen #1552 raised is whether an allocating JACL program still has one.
    let mut names = std::collections::BTreeMap::<usize, &str>::new();
    for e in &m.exports {
        names.insert(e.func as usize, e.name.as_str());
    }
    let elig0: Vec<usize> = (0..m.funcs.len()).filter(|&i| emittable_leaf[i]).collect();
    if elig0.is_empty() {
        println!("\nat floor 0: NOTHING is eligible — no function can tier up at any floor.");
    } else {
        println!("\nat floor 0, these {} would tier up:", elig0.len());
        for i in elig0.iter().take(30) {
            println!(
                "   f{:<5} {:<34} est_emitted={}",
                i,
                names.get(i).copied().unwrap_or("-"),
                temen_wasm_jit::est_emitted_size(&m.funcs[*i])
            );
        }
        if elig0.len() > 30 {
            println!("   … and {} more", elig0.len() - 30);
        }
    }

    // The #1552 cascade, made visible: which functions carry `gc.roots`, and which functions were
    // taken off the emit set because they can reach one. `bounce_serviceable` seeds on
    // `uses_gc_roots()` and runs a monotone backward fixpoint, so a `gc.roots` user's callers stop
    // being serviceable cross-tier callees and the emit fixpoint drops them too.
    let gcr: Vec<usize> = (0..m.funcs.len()).filter(|&i| m.funcs[i].uses_gc_roots()).collect();
    println!("\ngc.roots-bearing functions: {}", gcr.len());
    for i in &gcr {
        println!(
            "   f{:<5} {:<34} emit={}",
            i,
            names.get(i).copied().unwrap_or("-"),
            emit[*i]
        );
    }
}
