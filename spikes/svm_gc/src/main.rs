//! Spike-2 — conservative, non-moving mark-sweep driven by `gc.roots`.
//!
//! The riskiest design point of the backend rewrite (`docs/SVM_BACKEND_PLAN.md` §2,
//! `docs/SVM_GC_CONTRACT.md`): prove that a guest-side **conservative, non-moving
//! mark-sweep** collector, finding its roots via svm's `gc.roots` op, **reclaims
//! garbage and retains the reachable graph (with intact data) across collections.**
//!
//! The collector is plain C compiled through svm-llvm (so it runs the *real*
//! `__vm_gc_roots` op on interp + Cranelift JIT). The heap is a fixed-slot array in
//! the window; roots are found conservatively from the control stack/registers via
//! `gc.roots`; heap edges are traced precisely (one child pointer per node).
//!
//! Scenario (`run(n)`):
//!   - `a` = a live object, held in a stack local across collection (a gc.roots root)
//!   - `c` = `a->child`, allocated in a helper and reachable **only** via `a`
//!     (so its survival proves *tracing*, not direct rooting)
//!   - `n` unreachable objects allocated in a helper whose frame is then popped
//!   - collect twice (proving swept slots are *reused*), asserting throughout that
//!     `a` and `c` survive intact and exactly `n` garbage objects are reclaimed.
//! Returns 230 iff every invariant held — independent of `n`. No native oracle:
//! `gc.roots` has no native equivalent, so interp==jit + the exact value is the check.

use std::path::PathBuf;
use std::process::Command;
use svm_interp::Value;
use svm_ir::ValType;
use svm_jit::JitOutcome;

const DRIVER_C: &str = r#"
long __vm_gc_roots(long heap_lo, long heap_hi, void *buf, long cap);

typedef struct Obj { long allocated; long mark; struct Obj *child; long value; } Obj;
#define NSLOTS   256
#define ROOT_CAP 192
static Obj gc_heap[NSLOTS];

/* forward decls so run() is the first DEFINED function (module index 0) */
int  run(int n);
static long heap_lo(void);
static long heap_hi(void);
static void gc_reset(void);
static Obj* gc_alloc(long v);
static long object_count(void);
static void link_child(Obj *parent, long v);
static void spawn_garbage(long n);
static long gc_collect(void);
static long ptr_to_int(void *p);

int run(int n) {
  gc_reset();
  Obj *a = gc_alloc(10);     /* live root, held across both collections below */
  link_child(a, 20);         /* c = a->child, reachable ONLY through a (tests tracing) */
  spawn_garbage(n);          /* n unreachable objects (helper frame popped on return) */

  long before = object_count();   /* a + c + n garbage = n + 2 */
  long swept1 = gc_collect();     /* finds a via gc.roots, traces to c, sweeps the n garbage */
  long after1 = object_count();   /* 2 */

  /* round 2: reuse swept slots, and re-survive a + c across a second collection */
  spawn_garbage(n);
  long mid = object_count();      /* 2 + n again — only possible if round-1 slots were reused */
  long swept2 = gc_collect();
  long after2 = object_count();   /* 2 */

  int ok = (before == n + 2)
        && (swept1 == n) && (after1 == 2)
        && (mid == n + 2)
        && (swept2 == n) && (after2 == 2)
        && (a->value == 10)        /* a intact */
        && (a->child != 0)         /* edge intact */
        && (a->child->value == 20);/* c intact, reached only via a */
  return ok ? (int)(after2 * 100 + a->value + a->child->value) : -1;  /* 230 iff correct */
}

/* Runtime ptr->int (noinline) — svm-llvm rejects a *constant* ptrtoint of a global
   address, so the cast must be an instruction on a passed-in pointer value. */
__attribute__((noinline)) static long ptr_to_int(void *p) { return (long)p; }
static long heap_lo(void) { return ptr_to_int(&gc_heap[0]); }
static long heap_hi(void) { return ptr_to_int(&gc_heap[NSLOTS]); }

static void gc_reset(void) {
  for (int i = 0; i < NSLOTS; i++) { gc_heap[i].allocated = 0; gc_heap[i].mark = 0; gc_heap[i].child = 0; gc_heap[i].value = 0; }
}

__attribute__((noinline))
static Obj* gc_alloc(long v) {
  for (int i = 0; i < NSLOTS; i++) {
    if (!gc_heap[i].allocated) { gc_heap[i].allocated = 1; gc_heap[i].mark = 0; gc_heap[i].child = 0; gc_heap[i].value = v; return &gc_heap[i]; }
  }
  return 0; /* OOM — would fail the reuse invariant */
}

static long object_count(void) { long c = 0; for (int i = 0; i < NSLOTS; i++) if (gc_heap[i].allocated) c++; return c; }

/* child reachable only via parent->child — never returned to the caller's stack */
__attribute__((noinline))
static void link_child(Obj *parent, long v) { parent->child = gc_alloc(v); }

/* n unreachable objects; their pointers stay confined to this (then-popped) frame */
__attribute__((noinline))
static void spawn_garbage(long n) { for (long i = 0; i < n; i++) { Obj *g = gc_alloc(1000 + i); (void)g; } }

/* conservative mark from a candidate word (a window offset from gc.roots) */
static void mark_from(long word) {
  long lo = heap_lo(), hi = heap_hi();
  if (word < lo || word >= hi) return;            /* out of heap */
  long off = word - lo;
  if (off % (long)sizeof(Obj) != 0) return;       /* not a slot start (object-start check) */
  long i = off / (long)sizeof(Obj);
  if (!gc_heap[i].allocated || gc_heap[i].mark) return;
  gc_heap[i].mark = 1;
  mark_from((long)gc_heap[i].child);              /* precise trace of the one pointer field */
}

__attribute__((noinline))
static long gc_collect(void) {
  for (int i = 0; i < NSLOTS; i++) gc_heap[i].mark = 0;     /* clear marks */
  long buf[ROOT_CAP];
  long n = __vm_gc_roots(heap_lo(), heap_hi(), buf, ROOT_CAP);
  long scan = n < ROOT_CAP ? n : ROOT_CAP;
  for (long i = 0; i < scan; i++) mark_from(buf[i]);         /* mark from roots + trace */
  long swept = 0;
  for (int i = 0; i < NSLOTS; i++) {
    if (gc_heap[i].allocated && !gc_heap[i].mark) { gc_heap[i].allocated = 0; swept++; }  /* non-moving free */
  }
  return swept;
}
"#;

fn compile_to_bc() -> PathBuf {
    let dir = std::env::temp_dir();
    let c = dir.join(format!("jacl_gc_spike_{}.c", std::process::id()));
    let bc = dir.join(format!("jacl_gc_spike_{}.bc", std::process::id()));
    std::fs::write(&c, DRIVER_C).expect("write driver C");
    let status = Command::new("clang")
        .args(["-O2", "-emit-llvm", "-c", "-fno-vectorize", "-fno-slp-vectorize"])
        .arg(&c).arg("-o").arg(&bc)
        .status()
        .expect("spawn clang");
    assert!(status.success(), "clang failed to compile the GC driver");
    bc
}

fn main() {
    println!("Spike-2: conservative non-moving mark-sweep driven by gc.roots\n");

    let bc = compile_to_bc();
    println!("[1] clang compiled the GC runtime to LLVM bitcode");

    let t = svm_llvm::translate_bc_path(&bc).expect("svm-llvm: translate bitcode");
    let module = t.module;
    println!("[2] svm-llvm translated it ({} funcs); imports: {:?}",
             module.funcs.len(), module.imports);

    svm_verify::verify_module(&module).expect("verify translated IR");
    println!("[3] verifier accepted the translated module");

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
            JitOutcome::Returned(s) => match results[0] { ValType::I32 => s[0] as i32, o => panic!("result type {o:?}") },
            other => panic!("jit did not return: {other:?}"),
        };
        assert_eq!(iv, jv, "interp != jit for run({n})");
        iv
    };

    println!("[4] run (interp == jit): conservative mark-sweep, two cycles, reuse:");
    // n up to NSLOTS-2 = 254; use a spread incl. a large one to force slot reuse.
    for n in [1i32, 8, 100, 200] {
        let got = run_n(n);
        assert_eq!(got, 230,
            "run({n}) = {got}, want 230 (a+c survive intact across 2 collections; {n} garbage reclaimed each)");
        println!("  PASS  run({n}) = 230  ({n} garbage reclaimed x2, a & c survived intact)");
    }

    println!("\nSpike-2 OK — a conservative non-moving mark-sweep driven by gc.roots \
              reclaims garbage and retains the reachable graph (incl. trace-only \
              reachable nodes) with intact data across collections, on interp and JIT.");
}
