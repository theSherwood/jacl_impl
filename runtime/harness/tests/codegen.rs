//! P2.2 — codegen scaffold proof (real JACL source → SVM-IR → run).
//!
//! Builds the JACL frontend + the P2.2 codegen (`codegen/codegen.c`) into a driver
//! (`codegen/tests/emit_jacl.c`, compiled with gcc — the frontend's toolchain), which
//! parses a JACL snippet and emits the program as a v9 svm-encode **object** (decoded by
//! `decode_emitted`; `--text` still gives svm-text for the IR-inspection tests). Each
//! program is linked against the separately-translated runtime (the P2.0 path) and run on
//! interp + JIT; the result (an i32 JaclVal) is checked against the expected value.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use jacl_runtime_harness::{decode_emitted, translate_runtime};
use temen_interp::Value;
use temen_ir::{link_with_manifest, LinkUnit};

const CODEGEN_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../codegen");

/// Build the codegen driver once per test process (gcc: the unity frontend uses
/// pre-declaration ordering clang rejects under -std=c99).
///
/// `-DJACL_EMIT_ONLY` selects the LLVM-free emit-only frontend (`src/jacl_emit.c`) —
/// the same TU the product ships (jacl_emit.wasm). It drops the legacy `compiler_compile`
/// static-error oracle, so accept/reject here is exactly the SVM backend's own behavior
/// (typer + codegen), not the old bytecode compiler's. This is a prerequisite for deleting
/// `compiler.c`/`vm.c` (item 6, slice 3c): the oracle path links against them.
fn driver() -> &'static Path {
    static BIN: OnceLock<PathBuf> = OnceLock::new();
    BIN.get_or_init(|| {
        let out = std::env::temp_dir().join(format!("jacl_emit_jacl_{}", std::process::id()));
        let status = Command::new("gcc")
            .args(["-O0", "-w", "-D_DEFAULT_SOURCE", "-DJACL_EMIT_ONLY"])
            .arg(format!("{CODEGEN_DIR}/tests/emit_jacl.c"))
            .arg(format!("{CODEGEN_DIR}/codegen.c"))
            .arg(format!("{CODEGEN_DIR}/irbuilder.c"))
            .arg("-o")
            .arg(&out)
            .args(["-lm", "-lpthread"])
            .status()
            .expect("spawn gcc (is gcc installed?)");
        assert!(status.success(), "gcc failed to build the codegen driver");
        out
    })
    .as_path()
}

/// Emit one case's program as the default v9 svm-encode **object** via the codegen.
/// Decoded by `decode_emitted` (`temen_encode::decode_unit`).
fn emit(case: &str) -> Vec<u8> {
    let out = Command::new(driver()).arg(case).output().expect("run emit_jacl");
    assert!(
        out.status.success(),
        "emit_jacl {case} failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    out.stdout
}

/// Emit one case's program as **svm-text** (`--text`), for tests that inspect the emitted IR.
fn emit_text(case: &str) -> String {
    let out = Command::new(driver()).arg("--text").arg(case).output().expect("run emit_jacl");
    assert!(
        out.status.success(),
        "emit_jacl --text {case} failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    String::from_utf8(out.stdout).expect("emit_jacl --text output is UTF-8")
}

/// Run a case expected to fail codegen; returns the emitted error (stderr).
fn emit_expecting_error(case: &str) -> String {
    let out = Command::new(driver()).arg(case).output().expect("run emit_jacl");
    assert!(!out.status.success(), "{case}: expected codegen to fail but it succeeded");
    String::from_utf8_lossy(&out.stderr).into_owned()
}

/// JaclVal encoding of an i32 (tag 0x02 high byte), matching the runtime + codegen.
fn i32_val(x: i32) -> i64 {
    ((0x02u64 << 56) | (x as u32 as u64)) as i64
}

/// Compile `case`, link it against the runtime, wrap it with svm's powerbox entry
/// (`synth_manifest_start`), instantiate, and run the powerbox entry on interp + JIT
/// (`run_diff` enforces they agree). Returns the program's returned JaclVal and captured
/// stdout. This is the uniform powerbox entry: every program runs through the reactor, so a
/// `[print …]` program's host I/O works the same way a pure-compute one returns a value.
fn run_case_full(case: &str) -> (i64, Vec<u8>) {
    let program =
        decode_emitted(&emit(case)).unwrap_or_else(|e| panic!("decode {case}: {e}"));

    let rt = translate_runtime();
    // Retain the runtime's manifest capability imports (`write`, …) through the link
    // (post-IMPORTS.md-phase-4): `link_with_manifest` keeps an import no unit exports, and the
    // host binds it at instantiate. The program's entry is its func 0 — export it so we can
    // resolve its final index after linking (mirrors bin/jacl_svm.rs).
    let linked = link_with_manifest(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![("__prog_entry".to_string(), 0)],
            ..Default::default()
        },
    ])
    .unwrap_or_else(|e| panic!("link {case}: {e:?}"));
    let entry = linked
        .resolve_export("__prog_entry")
        .unwrap_or_else(|| panic!("{case}: entry export missing after link"));

    // Wrap the program entry in the paramless powerbox `_start` (svm lays out the writable
    // stash + memory), then bind the powerbox capability names. `run_diff` grants them to both
    // backends identically and enforces interp == jit.
    let pb = temen_ir::synth_manifest_start(linked, entry, false)
        .unwrap_or_else(|e| panic!("synth_manifest_start {case}: {e}"));
    let imports = temen_run::Imports::new()
        .provide("write", temen_run::HostCap::stdout())
        .provide("exit", temen_run::HostCap::exit())
        .provide("stdin", temen_run::HostCap::stdin());
    let inst = temen_run::instantiate_with_imports(pb, imports)
        .unwrap_or_else(|e| panic!("instantiate {case}: {e}"));
    let run = inst
        .run_diff(&temen_run::RunConfig::default())
        .unwrap_or_else(|e| panic!("run {case}: {e}"));
    let result = match run.outcome {
        temen_run::Outcome::Returned(ref v) => match v.first() {
            Some(Value::I64(x)) => *x,
            other => panic!("{case}: unexpected returned value {other:?}"),
        },
        temen_run::Outcome::Exited(c) => panic!("{case}: program exited({c})"),
    };
    (result, run.stdout)
}

/// Compile + run `case`, asserting the returned JaclVal equals `want` (stdout unused).
fn run_case(case: &str, want: i64) {
    let (iv, _) = run_case_full(case);
    assert_eq!(iv, want, "{case}");
}

#[test]
fn arithmetic_add() {
    run_case("add", i32_val(3)); // [+ 1 2]
}

#[test]
fn arithmetic_nested() {
    run_case("nested", i32_val(7)); // [+ 1 [* 2 3]]
}

#[test]
fn arithmetic_sub_of_mul() {
    run_case("submul", i32_val(17)); // [- [* 4 5] 3]
}

#[test]
fn arithmetic_variadic_fold() {
    // SVM codegen folds `+` variadically (codegen.c, the compile_i32 / dynamic fold loops).
    // Now exercised: the driver is emit-only, so there is no legacy binary-`+` oracle to reject it.
    run_case("variadic", i32_val(10)); // [+ 1 2 3 4]
}

#[test]
fn arithmetic_div_and_mod() {
    run_case("div_mod", i32_val(5)); // [+ [/ 20 6] [% 20 6]] = 3 + 2
}

#[test]
fn binding_def_and_read() {
    run_case("bind_read", i32_val(15)); // def x 5; [+ $x 10]
}

#[test]
fn binding_mutate_with_set() {
    run_case("mutate", i32_val(42)); // mut c 1; set c 42; [+ $c 0]
}

#[test]
fn binding_set_to_expression() {
    run_case("reassign_expr", i32_val(42)); // mut c 1; set c [+ $c 41]; [+ $c 0]
}

#[test]
fn binding_block_scope() {
    run_case("scoped_block", i32_val(11)); // def x 1; { def y 10; [+ $x $y] }
}

#[test]
#[ignore = "SVM codegen deliberately permits top-level `def x; def x` as reassignment \
            (env_define's module_scope carve-out in codegen.c), matching the old VM's top-level \
            semantics. The emit-only driver has no oracle to reject it, so this stays ignored \
            unless top-level redef detection is added as a deliberate semantic change (ISSUES.md)."]
fn binding_same_scope_shadow_is_an_error() {
    let err = emit_expecting_error("shadow_err"); // def x 1; def x 2
    assert!(err.contains("already defined in this scope"), "unexpected error: {err}");
}

#[test]
fn binding_set_undefined_is_an_error() {
    let err = emit_expecting_error("set_undef_err"); // set nope 1
    assert!(err.contains("undefined variable"), "unexpected error: {err}");
}

#[test]
fn binding_set_immutable_is_an_error() {
    let err = emit_expecting_error("set_immut_err"); // def x 1; set x 2
    assert!(err.contains("immutable"), "unexpected error: {err}");
}

// ---- P2.4 control flow ----

#[test]
fn if_then_taken() {
    run_case("if_true", i32_val(10)); // [if [> 5 3] { 10 } else { 20 }]
}

#[test]
fn if_else_taken() {
    run_case("if_false", i32_val(20)); // [if [> 1 3] { 10 } else { 20 }]
}

#[test]
fn if_without_else_taken() {
    run_case("if_noelse_true", i32_val(7)); // [if [> 5 3] { 7 }]
}

#[test]
fn if_without_else_falls_to_nil() {
    run_case("if_noelse_false", 0); // [if [> 1 3] { 7 }] => nil (JACL_NIL == 0)
}

#[test]
fn if_branch_mutates_outer_binding() {
    run_case("if_sets_outer", i32_val(100)); // mut x 1; [if [> 5 3] { set x 100 }]; [+ $x 0]
}

#[test]
fn if_used_as_expression() {
    run_case("if_expr_bind", i32_val(9)); // def x [if [> 5 3] { 9 } else { 0 }]; [+ $x 0]
}

#[test]
fn elif_chain_middle_clause() {
    run_case("elif_chain", i32_val(22)); // [if false { 1 } elif true { 22 } else { 33 }]
}

#[test]
fn while_accumulating_loop() {
    run_case("while_sum", i32_val(15)); // sum 1..=5
}

#[test]
fn while_countdown_loop() {
    run_case("while_countdown", i32_val(30)); // 3 iterations × 10
}

// `for` iteration, current syntax `[for COLLECTION binding { body }]` (collection first, no
// `in` keyword). The collection is a `[vec …]` literal so its i32 elements sum to an inline
// i32 the test can assert (a `[range A B]` collection yields i64/heap-wide — see ISSUES.md).
#[test]
fn for_over_range_command() {
    run_case("for_range_cmd", i32_val(6)); // for [vec 1 2 3] i: 1+2+3
}

#[test]
fn for_over_range_sum() {
    run_case("for_sum", i32_val(10)); // for [vec 0 1 2 3 4] i: 0+1+2+3+4
}

#[test]
#[ignore = "the `A..B` dotdot form is not a valid for-collection in the current syntax \
            (parses as separate args); use `[range A B]` / `[vec …]`. See ISSUES.md"]
fn for_over_dotdot_range() {
    run_case("for_dotdot", i32_val(10)); // for i in 0..5: 0+1+2+3+4
}

#[test]
fn for_with_nested_if() {
    run_case("for_with_if", i32_val(7)); // for [vec 0 1 2 3 4] i where i > 2: 3+4
}

// ---- P2.5 procs & calls ----

#[test]
fn proc_simple_call() {
    run_case("proc_add", i32_val(42)); // proc add {x,y}; [add 40 2]
}

#[test]
fn proc_typed_params_and_return() {
    run_case("proc_typed", i32_val(42)); // proc add {i32 x, i32 y} i32
}

#[test]
fn proc_zero_args() {
    run_case("proc_zero_arg", i32_val(42)); // [answer]
}

#[test]
fn proc_calls_another_proc() {
    run_case("proc_calls_proc", i32_val(42)); // [dbl [inc 20]]
}

#[test]
fn proc_recursive_factorial() {
    run_case("proc_recursion", i32_val(120)); // [fac 5]
}

#[test]
fn proc_explicit_return() {
    run_case("proc_return", i32_val(42)); // proc f {n} {return [+ $n 1]}; [f 41]
}

#[test]
fn proc_with_local_loop() {
    run_case("proc_local_loop", i32_val(15)); // proc sumto {n} { while … }; [sumto 5]
}

#[test]
fn proc_arity_mismatch_is_an_error() {
    let err = emit_expecting_error("proc_arity_err"); // [add 1] for a 2-arg proc
    assert!(err.contains("expects 2 arguments but got 1"), "unexpected error: {err}");
}

#[test]
fn proc_tail_recursion() {
    run_case("proc_tail_rec", i32_val(5)); // count 5 0 -> 5
}

#[test]
fn proc_tail_recursion_is_constant_stack() {
    // 200k tail calls: must not overflow the native stack — proves return_call is a
    // real tail call on both backends.
    run_case("proc_tail_deep", i32_val(200_000));
}

// ---- P2.6 closures ----

#[test]
fn closure_basic_lambda() {
    run_case("closure_basic", i32_val(42)); // def f [\ {x} {[+ $x 1]}]; [$f 41]
}

#[test]
fn closure_captures_outer_binding() {
    run_case("closure_capture", i32_val(42)); // captures y=10; [$f 32]
}

#[test]
fn closure_anonymous_proc() {
    run_case("closure_anon_proc", i32_val(42)); // def f [proc {x} {[* $x 2]}]; [$f 21]
}

#[test]
fn closure_multiple_captures() {
    run_case("closure_multi_capture", i32_val(122)); // (100+20)+2
}

#[test]
fn closure_returned_from_proc_captures_param() {
    run_case("closure_returned_from_proc", i32_val(42)); // adder(5) then add5(37)
}

#[test]
fn closure_nested_transitive_capture() {
    run_case("closure_nested", i32_val(42)); // (1+10)+31, inner captures a and x
}

// ---- P2.6b: captured `mut` is boxed in a shared cell ----

#[test]
fn closure_mutates_captured_variable() {
    // inc() twice mutates the shared cell; the outer scope sees c == 2.
    run_case("closure_mut_capture", i32_val(2));
}

#[test]
fn closure_counter_factory_keeps_state() {
    // each make() closure has its own cell; calling it 3× yields 3.
    run_case("closure_counter_factory", i32_val(3));
}

#[test]
fn closures_share_one_mutable_cell() {
    // get and setx capture the same cell; setx 42 is visible to get.
    run_case("closure_shared_mut", i32_val(42));
}

// ---- P2.7: type-driven (unboxed) lowering ----

#[test]
fn typed_arithmetic_lowers_to_native_i32() {
    // [+ 1 [* 2 3]] — the typer proves i32, so native i32 ops with no runtime calls.
    let ir = emit_text("nested");
    assert!(ir.contains("i32.mul") && ir.contains("i32.add"), "expected native i32 ops:\n{ir}");
    assert!(!ir.contains("jacl_mul") && !ir.contains("jacl_add"), "should not call the runtime:\n{ir}");
    run_case("nested", i32_val(7)); // and it's still correct
}

#[test]
fn dynamic_arithmetic_keeps_runtime_calls() {
    // def x 5; [+ $x 10] — x is dyn, so the dynamic jacl_add path is used.
    let ir = emit_text("bind_read");
    assert!(ir.contains("jacl_add"), "dynamic arithmetic should call jacl_add:\n{ir}");
    run_case("bind_read", i32_val(15));
}

// ---- P2.8: strings + collections ----

#[test]
fn string_literal_length() {
    run_case("str_len", i32_val(5)); // [length "hello"]
}

#[test]
fn string_concat_length() {
    run_case("str_concat_len", i32_val(5)); // [length [concat "ab" "cde"]]
}

#[test]
fn string_interpolation_length() {
    run_case("interp_len", i32_val(5)); // [length "val=$n"] with n=7 -> "val=7"
}

#[test]
fn vector_literal_length() {
    run_case("vec_len", i32_val(3)); // [length [vec 1 2 3]]
}

#[test]
fn vector_literal_length_four() {
    run_case("vec_len4", i32_val(4)); // [length [vec 10 20 30 40]]
}


// ---- P2.10: bring-up — combined programs, expected values frozen from the old bytecode VM ----
// The expected integers below were the old VM's outputs (Phase 6.3 retired the live `--oldvm`
// oracle; the values are frozen here so the check is preserved without linking `vm.c`). run_case
// still asserts interp == jit == want, so each stays a 3-way agreement against the reference.

#[test]
fn bringup_matches_old_vm() {
    for (case, want) in [
        ("bring_bindings", 42),     // def + dynamic arithmetic
        ("bring_nested_arith", 11), // typed (unboxed) nested arithmetic
        ("bring_factorial", 720),   // recursion + if/else
        ("bring_while_sum", 55),    // while + mut + set
        ("bring_if", 100),          // if expression
        ("bring_closure", 42),      // anonymous proc capturing an outer binding
        ("bring_counter", 3),       // anonymous proc capturing a mutable (cell)
        ("bring_str", 9),           // string concat + length
        ("bring_hof", 42),          // higher-order: proc taking a closure arg
        ("bring_proc_chain", 42),   // nested proc calls
        ("bring_recur_sum", 55),    // recursion + accumulation
    ] {
        run_case(case, i32_val(want));
    }
}

// ---- P3.1: generators (yield) on fibers ----

#[test]
fn generator_count_matches_old_vm() {
    // proc upto {n} { … yield $i … }; [for [upto 5] x { sum }] -> 0+1+2+3+4 = 10
    run_case("gen_count", i32_val(10));
}

#[test]
fn generator_squares_matches_old_vm() {
    // a generator yielding i*i, driven by for-over-generator -> 0+1+4+9+16 = 30
    run_case("gen_squares", i32_val(30));
}

// ---- P3.2: spawn / await (futures over fibers) ----

#[test]
fn spawn_await_matches_old_vm() {
    for (case, want) in [
        ("spawn_await", 42),
        ("spawn_capture", 42),
        ("spawn_two", 45),
        ("spawn_await_twice", 42),
    ] {
        run_case(case, i32_val(want));
    }
}

// ---- P3.3: parallel / race ----

#[test]
fn parallel_race_matches_old_vm() {
    // parallel returns a vector (consumed via destructuring `def [a b] …`); race returns
    // the first block's result.
    for (case, want) in [("parallel", 45), ("parallel3", 42), ("race", 42)] {
        run_case(case, i32_val(want));
    }
}

#[test]
fn typed_proc_body_is_unboxed() {
    // proc add {i32 x, i32 y} i32 {+ $x $y} — typed params, so the body uses i32.add.
    let ir = emit_text("proc_typed");
    assert!(ir.contains("i32.add"), "typed proc body should use native i32.add:\n{ir}");
    assert!(!ir.contains("jacl_add"), "typed proc body should not call jacl_add:\n{ir}");
    run_case("proc_typed", i32_val(42));
}

// ---- P3.5 host I/O: print through the powerbox (stdout captured, interp == jit) ----

#[test]
fn print_string_to_stdout() {
    let (_, out) = run_case_full("print_str"); // [print "hello"]
    assert_eq!(out, b"hello\n", "print of a string reaches stdout via the powerbox");
}

#[test]
fn print_int_to_stdout() {
    let (_, out) = run_case_full("print_int"); // [print [+ 40 2]]
    assert_eq!(out, b"42\n");
}

#[test]
fn print_two_statements() {
    let (_, out) = run_case_full("print_two"); // [print "hi"] [print 7]
    assert_eq!(out, b"hi\n7\n");
}

#[test]
fn nested_await() {
    // def a [spawn {42}]; def b [spawn {[await $a]}]; [await $b]  -> 42.
    // A task awaiting another task's future: demand-driven await runs the inner task on a
    // nested fiber while the outer task is suspended at the resume.
    let (iv, _) = run_case_full("nested_await");
    assert_eq!(iv, i32_val(42), "nested await result");
}

// --- P2.10 bring-up: combined programs across the supported subset, each returning an i32.
// These mirror the native bytecode test/*.c exec cases on the SVM backend (item 6, slice 3a):
// they run through the same emit -> link -> run_diff path (interp == JIT), so the SVM engine is
// the oracle the C exec tests are being retired in favor of.

#[test]
fn bring_bindings() {
    run_case("bring_bindings", i32_val(42)); // def x 5; def y 37; [+ $x $y]
}

#[test]
fn bring_nested_arith() {
    run_case("bring_nested_arith", i32_val(11)); // [+ 1 [* 2 [- 10 5]]]
}

#[test]
fn bring_factorial() {
    run_case("bring_factorial", i32_val(720)); // fac 6
}

#[test]
fn bring_while_sum() {
    run_case("bring_while_sum", i32_val(55)); // sum 1..10
}

#[test]
fn bring_if() {
    run_case("bring_if", i32_val(100)); // n=7 > 5 -> 100
}

#[test]
fn bring_closure() {
    run_case("bring_closure", i32_val(42)); // anon proc capturing $y=10, [$f 32]
}

#[test]
fn bring_counter() {
    run_case("bring_counter", i32_val(3)); // mutable-capturing proc invoked 3x
}

#[test]
fn bring_str() {
    run_case("bring_str", i32_val(9)); // [length [concat "foo" "barbaz"]]
}

#[test]
fn bring_hof() {
    run_case("bring_hof", i32_val(42)); // apply $dbl 21
}

#[test]
fn bring_proc_chain() {
    run_case("bring_proc_chain", i32_val(42)); // dbl(add 19 2)
}

#[test]
fn bring_recur_sum() {
    run_case("bring_recur_sum", i32_val(55)); // sumto 10, recursive
}

// --- Concurrency (spawn/await/parallel) on the SVM fiber scheduler. `race` is intentionally
// omitted: its result is the first block to finish, non-deterministic for pure-compute blocks.
#[test]
fn spawn_await() {
    run_case("spawn_await", i32_val(42)); // def f [spawn { [+ 40 2] }]; [await $f]
}

#[test]
fn spawn_two() {
    run_case("spawn_two", i32_val(45)); // [+ [await $a] [await $b]] -> 42 + 3
}

#[test]
fn spawn_capture() {
    run_case("spawn_capture", i32_val(42)); // spawn block captures $y=30; +12
}

#[test]
fn spawn_await_twice() {
    run_case("spawn_await_twice", i32_val(42)); // future resolves once, cached; 21+21
}

#[test]
fn parallel_two() {
    run_case("parallel", i32_val(45)); // def [a b] [parallel {…} {…}]; [+ $a $b]
}

#[test]
fn parallel_three() {
    run_case("parallel3", i32_val(42)); // 10 + 20 + 12
}

// --- Destructuring bind on SVM (item 6 slice 3a): retires test_destructure_*.c. Each is the
// native exec test's scenario rephrased to return an i32 so run_diff (interp == JIT) checks it.
#[test]
fn destructure_vec_positional() {
    run_case("destr_vec", i32_val(6)); // def [a b c] [vec 1 2 3]; a+b+c
}

#[test]
fn destructure_wildcard() {
    run_case("destr_wildcard", i32_val(20)); // def [_ b _] [vec 10 20 30]; b
}

#[test]
fn destructure_rest() {
    run_case("destr_rest", i32_val(4)); // def [head ..rest] [vec 1 2 3 4]; head + len(rest)
}

#[test]
fn destructure_named_from_map() {
    run_case("destr_named", i32_val(12)); // def m {x:5,y:7}; def {x,y} $m; x+y
}

#[test]
fn gen_sequential_yields() {
    run_case("gen_sequential", i32_val(6)); // proc with yield 1/2/3, summed over for -> 6
}

// --- Stream operators on SVM generators (probe for the test_stream_*/test_collect family).
#[test]
fn stream_collect_len() {
    run_case("stream_collect", i32_val(5)); // [length [collect [upto 5]]]
}

#[test]
fn stream_count() {
    run_case("stream_count", i32_val(7)); // [count [upto 7]]
}

// --- More stream operators on SVM (item 6 slice 3a): retires test_stream_transform/filter,
// the take/lines parts of test_stream_seq_ops/test_lines. Each runs on interp AND JIT.
#[test]
fn stream_transform_map() {
    run_case("stream_transform", i32_val(100)); // transform [upto 5] (*10), summed
}

#[test]
fn stream_filter_pred() {
    run_case("stream_filter", i32_val(30)); // filter [upto 10] (>5), summed: 6+7+8+9
}

#[test]
fn stream_take_n() {
    run_case("stream_take", i32_val(3)); // take 3 of [upto 100], summed: 0+1+2
}

#[test]
fn stream_lines_count() {
    run_case("stream_lines", i32_val(3)); // [count [lines "a\nb\nc"]]
}

#[test]
fn stream_first_elem() {
    run_case("stream_first", i32_val(6)); // first of filter([upto 10], >5) -> 6
}

// --- String equality + ordering on SVM (item 6 slice 3a): retires test_string_eq_cmp.c.
#[test]
fn string_equal() {
    run_case("string_eq", i32_val(1)); // [== "hello" "hello"] -> true
}

#[test]
fn string_not_equal() {
    run_case("string_neq", i32_val(0)); // [== "abc" "abd"] -> false
}

#[test]
fn string_less_than() {
    run_case("string_lt", i32_val(1)); // [< "abc" "abd"] -> true
}

#[test]
fn string_greater_than() {
    run_case("string_gt", i32_val(1)); // [> "xyz" "abc"] -> true
}

// --- index / slice builtins on SVM (item 6 slice 3a): retires test_index_builtin/test_slice_builtin.
#[test]
fn string_index() {
    run_case("string_index", i32_val(1)); // [index "hello" 1] == "e"
}

#[test]
fn vec_index() {
    run_case("vec_index", i32_val(20)); // [index [vec 10 20 30] 1]
}

#[test]
fn string_slice_range() {
    run_case("string_slice", i32_val(1)); // [slice "hello" 1 3] == "el"
}

#[test]
fn string_slice_from() {
    run_case("string_slice_from", i32_val(1)); // [slice "hello" 2] == "llo"
}

// ---- Syntax tour: the full single-file survey the README and AGENTS.md cite as the
// canonical, harness-verified picture of working syntax. Running it here makes that promise
// ("the harness runs it, so it cannot drift from the implementation") a real CI gate. ----

/// Absolute path to the canonical syntax tour.
const TOUR_JACL: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../test/jacl/tour.jacl");

/// Repo root (for the codegen sources + macro-staging bridge the staged driver links).
const REPO_ROOT: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../..");

/// JACL error flag — bit 61 of a `JaclVal` (mirrors `JACL_FLAG_ERROR` in `runtime/jaclrt.h`).
/// A failed `assert` expands to `panic`, which the SVM backend surfaces as an error *value*
/// that auto-returns out of the job (builtins.c `jacl_panic`) — so a tripped assert shows up
/// as an error-tagged return value, not a trap or a nonzero exit.
fn is_jacl_error(v: i64) -> bool {
    (v as u64) & (1u64 << 61) != 0
}

/// This crate's `target/<profile>` dir, deduced from the running test binary
/// (`target/<profile>/deps/<test>-<hash>`). Holds `libjacl_runtime_harness.a`.
fn target_profile_dir() -> PathBuf {
    let exe = std::env::current_exe().expect("current_exe");
    exe.ancestors()
        .nth(2)
        .expect("target/<profile> above deps/")
        .to_path_buf()
}

/// Build the **SVM-staged** frontend driver: `emit_jacl` + codegen + the macro-staging bridge
/// (`stage_bridge.c`), linked against this crate's `libjacl_runtime_harness.a` (which exports
/// `jacl_svm_stage`). This is the `codegen/selfhost/macro_staging/run_diff.sh --svm` recipe. It
/// is required for tour.jacl specifically: the tour defines and invokes a `defmacro`, and macro
/// bodies now expand by staging on the SVM engine (there is no legacy bytecode compiler to fall
/// back to), so the plain emit-only `driver()` rejects it with "emit-only build". Cached per
/// test process.
fn staged_driver() -> &'static Path {
    static BIN: OnceLock<PathBuf> = OnceLock::new();
    BIN.get_or_init(|| {
        let profile_dir = target_profile_dir();
        let staticlib = profile_dir.join("libjacl_runtime_harness.a");
        if !staticlib.exists() {
            // `cargo test` builds only the test binary + rlib, not the `[staticlib]` artifact the
            // C driver links against — materialize it (a no-op when already built).
            let mut cmd = Command::new(env!("CARGO"));
            cmd.args(["build", "--lib"]).current_dir(env!("CARGO_MANIFEST_DIR"));
            if profile_dir.file_name().and_then(|s| s.to_str()) == Some("release") {
                cmd.arg("--release");
            }
            let st = cmd.status().expect("spawn cargo build --lib");
            assert!(
                st.success() && staticlib.exists(),
                "could not build {} for the staged driver",
                staticlib.display()
            );
        }
        let out = std::env::temp_dir().join(format!("jacl_emit_staged_{}", std::process::id()));
        let status = Command::new("gcc")
            .args(["-DJACL_STAGE_ON_SVM_BUILD", "-std=gnu11", "-O1", "-w", "-D_GNU_SOURCE"])
            .arg("-I")
            .arg(format!("{REPO_ROOT}/codegen"))
            .arg(format!("{REPO_ROOT}/codegen/tests/emit_jacl.c"))
            .arg(format!("{REPO_ROOT}/codegen/codegen.c"))
            .arg(format!("{REPO_ROOT}/codegen/irbuilder.c"))
            .arg(format!("{REPO_ROOT}/codegen/selfhost/macro_staging/stage_bridge.c"))
            .arg("-L")
            .arg(&profile_dir)
            // The Rust staticlib pulls in libstd's system deps — the same link line run_diff.sh uses.
            .args([
                "-ljacl_runtime_harness", "-lgcc_s", "-lutil", "-lrt", "-lpthread", "-lm", "-ldl",
                "-lc",
            ])
            .arg("-o")
            .arg(&out)
            .status()
            .expect("spawn gcc (staged driver)");
        assert!(status.success(), "gcc failed to build the SVM-staged codegen driver");
        out
    })
    .as_path()
}

/// Emit an arbitrary `.jacl` *file* through the staged driver's `--file` mode (`JACL_STAGE_ON_SVM=1`
/// so macros expand on the SVM engine), then run it through the same link → powerbox → interp==jit
/// path as `run_case_full`. Returns the program's returned `JaclVal` and captured stdout.
fn run_jacl_file(path: &str) -> (i64, Vec<u8>) {
    let out = Command::new(staged_driver())
        .env("JACL_STAGE_ON_SVM", "1")
        .arg("--file")
        .arg(path)
        .output()
        .expect("run staged emit_jacl --file");
    assert!(
        out.status.success(),
        "staged emit_jacl --file {path} failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let program =
        decode_emitted(&out.stdout).unwrap_or_else(|e| panic!("decode {path}: {e}"));

    let rt = translate_runtime();
    let linked = link_with_manifest(&[
        LinkUnit { module: rt.module, exports: rt.exports, ..Default::default() },
        LinkUnit {
            module: program,
            exports: vec![("__prog_entry".to_string(), 0)],
            ..Default::default()
        },
    ])
    .unwrap_or_else(|e| panic!("link {path}: {e:?}"));
    let entry = linked
        .resolve_export("__prog_entry")
        .unwrap_or_else(|| panic!("{path}: entry export missing after link"));

    let pb = temen_ir::synth_manifest_start(linked, entry, false)
        .unwrap_or_else(|e| panic!("synth_manifest_start {path}: {e}"));
    let imports = temen_run::Imports::new()
        .provide("write", temen_run::HostCap::stdout())
        .provide("exit", temen_run::HostCap::exit())
        .provide("stdin", temen_run::HostCap::stdin());
    let inst = temen_run::instantiate_with_imports(pb, imports)
        .unwrap_or_else(|e| panic!("instantiate {path}: {e}"));
    let run = inst
        .run_diff(&temen_run::RunConfig::default())
        .unwrap_or_else(|e| panic!("run {path}: {e}"));
    let ret = match run.outcome {
        temen_run::Outcome::Returned(ref v) => match v.first() {
            Some(Value::I64(x)) => *x,
            other => panic!("{path}: unexpected returned value {other:?}"),
        },
        temen_run::Outcome::Exited(c) => panic!("{path}: exited({c}) instead of returning"),
    };
    (ret, run.stdout)
}

#[test]
fn syntax_tour_runs_clean_on_svm() {
    // tour.jacl exercises one feature area per section — values, bindings, the three modes,
    // interpolation, procs, lambdas, if/while/for, streams, destructuring, structs, maps,
    // mutable state, errors, macros, `ctx`, and spawn/await/parallel/race — each ending in
    // `assert`. A failed assert expands to `panic`, which the SVM backend returns as an error
    // *value* that propagates out as the job's result (no subsequent statement runs). So the
    // drift guard is twofold: the tour must run to completion on interp AND jit (`run_diff`
    // enforces they agree), and its returned value must not be an error. It prints nothing.
    let (ret, stdout) = run_jacl_file(TOUR_JACL);
    assert!(
        !is_jacl_error(ret),
        "tour.jacl returned a JACL error (0x{:016x}) — a syntax/feature regression tripped an \
         `assert` in the tour; bisect it against test/jacl/tour.jacl",
        ret as u64
    );
    assert!(
        stdout.is_empty(),
        "tour.jacl is assertion-only and must not print; got: {:?}",
        String::from_utf8_lossy(&stdout)
    );
}
