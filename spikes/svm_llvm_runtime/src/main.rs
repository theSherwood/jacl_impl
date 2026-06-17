//! Spike-1b(a) — compile a REAL JACL collection through `svm-llvm` and run it.
//!
//! Thesis under test: JACL's existing runtime C (the pointer-chasing, struct-heavy,
//! malloc-using systems code in `lib/`) can be **recompiled** through svm's LLVM
//! on-ramp rather than re-authored — the single biggest effort assumption of the
//! backend rewrite (`docs/SVM_BACKEND_PLAN.md` §8).
//!
//! What it does: a tiny driver `#include`s JACL's actual **`lib/rrb_vec/rrb_vec.h`**
//! (the RRB persistent vector, an include-as-template), builds a vector, and reads
//! it back. We compile it with stock `clang -O2 -emit-llvm`, translate the bitcode
//! with `svm_llvm::translate_bc_path`, verify, and run on **interp + Cranelift JIT**,
//! cross-checked against native `cc`.

use std::path::PathBuf;
use std::process::Command;
use svm_interp::Value;
use svm_ir::ValType;
use svm_jit::JitOutcome;

const LIB_RRB_VEC_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../lib/rrb_vec");

/// The driver: a self-contained Allocator + GC-alloc hook, then JACL's real
/// `rrb_vec.h` instantiated for `int`, then `run(n) = sum(0..n-1)` built via the
/// persistent vector and read back through `_get`. `main` lets native `cc` produce
/// an exit-code oracle for small n.
const DRIVER_C: &str = r#"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>   /* memcpy/memset — svm-llvm synthesizes */

/* Self-contained bump arena over a static buffer — keeps the spike focused on the
   collection logic, with no libc heap (so no `vm_map`/Memory-capability import to
   resolve; window growth is separately proven by svm-llvm's own memory tests). */
static unsigned char sp_heap[1 << 23];   /* 8 MiB */
static size_t sp_off = 0;
static void* sp_bump(size_t n){ n = (n + 7u) & ~(size_t)7u; void* p = &sp_heap[sp_off]; sp_off += n; return p; }
static void* sp_alloc(void* c, size_t n){ (void)c; return sp_bump(n); }
static void* sp_realloc(void* c, void* p, size_t n){ (void)c; (void)p; return sp_bump(n); }
static void  sp_free(void* c, void* p){ (void)c; (void)p; }   /* bump: no reclamation */

/* GC-alloc hook the template requires (zeroed; obj_type ignored for the spike). */
static void* sp_gc_alloc(int obj_type, size_t sz){ (void)obj_type; void* p = sp_bump(sz); memset(p, 0, sz); return p; }

/* `Allocator` comes from platform.h (pulled in by rrb_vec.h); fill it from our
   bump functions. */
#define RRB_VEC_T               int
#define RRB_VEC_NAME            ivec
#define RRB_VEC_GC_ALLOC(ot,sz) sp_gc_alloc((ot),(sz))
#define RRB_VEC_GC_OBJ_INTERNAL 1
#define RRB_VEC_GC_OBJ_LEAF     2
#define RRB_VEC_GC_OBJ_ROOT     3
#define RRB_VEC_ALLOCATOR       ((Allocator){ .alloc=sp_alloc, .realloc=sp_realloc, .free=sp_free, .ctx=0 })
#include "rrb_vec.h"

/* sum(0..n-1) built via the persistent vector, read back through _get. */
int run(int n) {
  ivec_root* v = ivec_empty();
  for (int i = 0; i < n; i++) v = ivec_push_back(v, i);
  long sum = 0;
  uint32_t cnt = ivec_count(v);
  for (uint32_t i = 0; i < cnt; i++) {
    ivec_get_result g = ivec_get(v, i);
    if (g.found) sum += g.value;
  }
  return (int)sum;
}

int main(void) { return run(16); }  /* 0+...+15 = 120, fits an exit code */
"#;

fn compile_to_bc() -> PathBuf {
    let dir = std::env::temp_dir();
    let c = dir.join(format!("jacl_rrb_spike_{}.c", std::process::id()));
    let bc = dir.join(format!("jacl_rrb_spike_{}.bc", std::process::id()));
    std::fs::write(&c, DRIVER_C).expect("write driver C");
    let status = Command::new("clang")
        .args(["-O2", "-emit-llvm", "-c", "-fno-vectorize", "-fno-slp-vectorize", "-DNDEBUG"])
        .arg("-I").arg(LIB_RRB_VEC_DIR)
        .arg(&c).arg("-o").arg(&bc)
        .status()
        .expect("spawn clang");
    assert!(status.success(), "clang failed to compile the rrb_vec driver");
    bc
}

fn native_run16() -> i32 {
    let dir = std::env::temp_dir();
    let c = dir.join(format!("jacl_rrb_spike_{}.c", std::process::id()));
    let exe = dir.join(format!("jacl_rrb_spike_native_{}", std::process::id()));
    let ok = Command::new("cc").arg(&c).arg("-I").arg(LIB_RRB_VEC_DIR).arg("-DNDEBUG")
        .arg("-o").arg(&exe).status().expect("spawn cc").success();
    assert!(ok, "native cc failed");
    Command::new(&exe).status().expect("run native").code().unwrap()
}

fn main() {
    println!("Spike-1b(a): real JACL lib/rrb_vec/rrb_vec.h through svm-llvm\n");

    // 1. clang -O2 -emit-llvm on the driver that includes JACL's real rrb_vec.h ---
    let bc = compile_to_bc();
    println!("[1] clang compiled the rrb_vec driver to LLVM bitcode");

    // 2. translate the bitcode to SVM IR via the on-ramp ----------------------
    let t = svm_llvm::translate_bc_path(&bc).expect("svm-llvm: translate bitcode");
    let module = t.module;
    println!("[2] svm-llvm translated it to SVM IR ({} funcs)", module.funcs.len());

    // 3. verify the translated module ----------------------------------------
    println!("    imports: {:?}", module.imports);
    svm_verify::verify_module(&module).expect("verify translated IR");
    println!("[3] verifier accepted the translated module");

    // 4. run run(n) on interp + JIT (entry = func 0; sig is (sp, n)) ----------
    let results = module.funcs[0].results.clone();
    let run_n = |n: i32| -> i32 {
        let full = vec![Value::I64(t.entry_sp as i64), Value::I32(n)];
        let mut fuel = 2_000_000_000u64;
        let interp = svm_interp::run(&module, 0, &full, &mut fuel).expect("interp run");
        let iv = match interp[0] { Value::I32(x) => x, o => panic!("interp gave {o:?}") };
        let slots: Vec<i64> = full.iter().map(|v| match v {
            Value::I64(x) => *x, Value::I32(x) => *x as i64, o => panic!("arg {o:?}"),
        }).collect();
        let jv = match svm_jit::compile_and_run(&module, 0, &slots).expect("jit run") {
            JitOutcome::Returned(s) => match results[0] {
                ValType::I32 => s[0] as i32,
                other => panic!("unexpected result type {other:?}"),
            },
            other => panic!("jit did not return: {other:?}"),
        };
        assert_eq!(iv, jv, "interp != jit for run({n})");
        iv
    };

    println!("[4] run (interp == jit), cross-checked vs closed form n*(n-1)/2:");
    for n in [16i32, 100, 1000] {
        let got = run_n(n);
        let want = n * (n - 1) / 2;
        assert_eq!(got, want, "run({n}) = {got}, want {want}");
        println!("  PASS  run({n}) = {got}");
    }

    // 5. native cc oracle for the small case ---------------------------------
    let native = native_run16();
    assert_eq!(native, 120, "native run(16) exit code");
    println!("[5] native cc oracle: run(16) exit code = {native}  (matches svm)");

    println!("\nSpike-1b(a) OK — JACL's real RRB persistent vector compiles through \
              svm-llvm and runs correctly on interp and JIT, matching native cc.");
}
