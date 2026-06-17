//! Spike-1 — JACL → SVM emit/verify/link/run path.
//!
//! Goal: retire the riskiest unknowns of the SVM backend's *front door* before any
//! bulk work:
//!   1. Can a JACL-shaped program be emitted as SVM **text IR**, parse, and pass
//!      the **verifier**?  (text-format adequacy for our codegen output)
//!   2. Does it run correctly on **both** the interpreter and the Cranelift **JIT**?
//!   3. Does `svm_ir::link` **static-link** a separately-authored *runtime* unit
//!      against the *program* unit (cross-module call resolved by name)?
//!
//! The text IR here is built by `emit_*` functions — a stand-in for JACL's codegen
//! (which, in the real backend, is the C emitter). The runtime unit stands in for
//! the LLVM-compiled runtime; here it is hand-authored because the thing under test
//! is the *IR + link contract*, not the LLVM compile (already proven elsewhere).

use svm_interp::Value;
use svm_ir::{link, LinkUnit, Module};

/// Emit the "runtime" unit: one exported function `rt_add(i32,i32)->i32`.
/// Stands in for a function JACL's runtime library would provide.
fn emit_runtime() -> String {
    // A real runtime fn is more than `i32.add`, but the point here is the linked
    // *symbol*, not the body.
    "\
func (i32, i32) -> (i32) {
block0(v0: i32, v1: i32):
  v2 = i32.add v0 v1
  return v2
}
"
    .to_string()
}

/// Emit a straight-line program function `combine(a,b) = rt_add(a, b)` — proves a
/// cross-unit call resolves. `call.import \"rt_add\"` takes a handle operand
/// (const 0) before the arg tuple, per svm's text form; the linker rewrites the
/// import into a direct call.
fn emit_combine() -> String {
    "\
func (i32, i32) -> (i32) {
block0(v0: i32, v1: i32):
  v2 = i32.const 0
  v3 = call.import \"rt_add\" (i32, i32) -> (i32) v2 (v0, v1)
  return v3
}
"
    .to_string()
}

/// Emit `sum_to(n) = 1 + 2 + ... + n` as a real SSA loop with block arguments,
/// calling the runtime's `rt_add` *inside the loop*. Proves JACL-shaped control
/// flow (blocks/br/br_if/block-args) emits valid IR and that the cross-unit call
/// works across a back-edge.
///
/// svm uses **block-local SSA**: a block sees only its own params + local defs, so
/// `n` is threaded through every block as an argument (not referenced across blocks
/// by dominance). This is exactly the discipline JACL's codegen must follow.
///
///   block0(n):           acc=0, i=1                 -> block1(n,acc,i)
///   block1(n,acc,i):     if i<=n goto body else exit
///   block2(n,acc,i):     acc'=rt_add(acc,i); i'=i+1 -> block1(n,acc',i')
///   block3(acc):         return acc
fn emit_sum_to() -> String {
    "\
func (i32) -> (i32) {
block0(v0: i32):
  v1 = i32.const 0
  v2 = i32.const 1
  br block1(v0, v1, v2)
block1(v3: i32, v4: i32, v5: i32):
  v6 = i32.le_s v5 v3
  br_if v6 block2(v3, v4, v5) block3(v4)
block2(v7: i32, v8: i32, v9: i32):
  v10 = i32.const 0
  v11 = call.import \"rt_add\" (i32, i32) -> (i32) v10 (v8, v9)
  v12 = i32.const 1
  v13 = i32.add v9 v12
  br block1(v7, v11, v13)
block3(v14: i32):
  return v14
}
"
    .to_string()
}

fn unit(src: &str, exports: &[(&str, u32)]) -> LinkUnit {
    let module = match svm_text::parse_module(src) {
        Ok(m) => m,
        Err(e) => {
            eprintln!("--- parse failed: {e:?} ---\n{src}\n--- bytes: {:?}", src);
            panic!("parse unit");
        }
    };
    LinkUnit {
        module,
        exports: exports.iter().map(|(n, i)| (n.to_string(), *i)).collect(),
        ..Default::default()
    }
}

/// Run entry `idx` on interp + JIT, assert they agree, return the i32 result.
fn run_entry(m: &Module, idx: u32, args: &[i32]) -> i64 {
    let ivals: Vec<Value> = args.iter().map(|&x| Value::I32(x)).collect();
    let mut fuel = 10_000_000u64;
    let interp = svm_interp::run(m, idx, &ivals, &mut fuel).expect("interp run");
    let iv = match interp[0] {
        Value::I32(x) => x as i64,
        other => panic!("unexpected interp value {other:?}"),
    };
    let jargs: Vec<i64> = args.iter().map(|&x| x as i64).collect();
    let jit = match svm_jit::compile_and_run(m, idx, &jargs).expect("jit compile") {
        svm_jit::JitOutcome::Returned(v) => v,
        other => panic!("jit did not return: {other:?}"),
    };
    assert_eq!(
        iv as u32 as u64, jit[0] as u32 as u64,
        "interp != jit for entry {idx} args {args:?}"
    );
    iv
}

fn check(name: &str, got: i64, want: i64) {
    assert_eq!(got, want, "{name}: got {got}, want {want}");
    println!("  PASS  {name} = {got}");
}

fn main() {
    println!("Spike-1: JACL → SVM emit / verify / static-link / run\n");

    // 1. Emit (stand-in for JACL codegen) ------------------------------------
    let runtime_ir = emit_runtime();
    let combine_ir = emit_combine();
    let sum_to_ir = emit_sum_to();
    println!("[1] emitted runtime + program text IR ({} + {} + {} bytes)",
             runtime_ir.len(), combine_ir.len(), sum_to_ir.len());

    // 2. Static-link program units against the runtime unit ------------------
    // Runtime unit first (rt_add => global func 0); program funcs reindex after:
    //   combine => func 1,  sum_to => func 2.
    let linked = link(&[
        unit(&runtime_ir, &[("rt_add", 0)]),
        unit(&combine_ir, &[]),
        unit(&sum_to_ir, &[]),
    ])
    .expect("link");
    println!("[2] linked {} funcs; imports resolved: {}",
             linked.funcs.len(), linked.imports.is_empty());
    assert!(linked.imports.is_empty(), "all imports must resolve");

    // 3. Verify the linked module -------------------------------------------
    svm_verify::verify_module(&linked).expect("verify linked program");
    println!("[3] verifier accepted the linked module");

    // 4. Run on interp + JIT and check results ------------------------------
    println!("[4] run (interp == jit):");
    check("combine(40, 2)", run_entry(&linked, 1, &[40, 2]), 42);
    check("combine(3, 4)", run_entry(&linked, 1, &[3, 4]), 7);
    check("sum_to(5)", run_entry(&linked, 2, &[5]), 15);
    check("sum_to(10)", run_entry(&linked, 2, &[10]), 55);
    check("sum_to(100)", run_entry(&linked, 2, &[100]), 5050);

    println!("\nSpike-1 OK — emit → verify → static-link → run proven on interp and JIT.");
}
