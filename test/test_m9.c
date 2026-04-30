#include "test_helpers.h"
#include "../src/jacl.h"
#include "test_run_helpers.h"

/* ===== US-001: Error creation with various payload types ===== */

static int test_error_create_string(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [error \"oops\"]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_error_create_int(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [error 42]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_error_create_nil(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [error $nil]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_error_create_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [error [vec 1 2 3]]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_error_create_map(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [error [map \"a\" 1]]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-001: error? predicate ===== */

static int test_error_pred_on_error(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [error \"x\"]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_error_pred_on_int(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? 42]]", &cap, "false\n"));
  TEST_PASS();
}

static int test_error_pred_on_nil(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? $nil]]", &cap, "false\n"));
  TEST_PASS();
}

static int test_error_pred_on_string(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? \"hello\"]]", &cap, "false\n"));
  TEST_PASS();
}

static int test_error_pred_on_bool(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [error? $true]]\n"
    "[print [error? $false]]",
    &cap, "false\nfalse\n"));
  TEST_PASS();
}

/* ===== US-001: error-val extraction ===== */

static int test_error_val_string(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def e [error \"oops\"]\n"
    "def v [error-val $e]\n"
    "[print $v]\n"
    "[print [error? $v]]",
    &cap, "oops\nfalse\n"));
  TEST_PASS();
}

static int test_error_val_int(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def e [error 42]\n"
    "def v [error-val $e]\n"
    "[print $v]\n"
    "[print [error? $v]]",
    &cap, "42\nfalse\n"));
  TEST_PASS();
}

static int test_error_val_noerr(void) {
  PrintCapture cap;
  /* error-val on non-error is a no-op */
  ASSERT(run_ok("[print [error-val 42]]", &cap, "42\n"));
  TEST_PASS();
}

/* ===== US-001: print of error values ===== */

static int test_print_error_string(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error \"oops\"]]", &cap, "<error: oops>\n"));
  TEST_PASS();
}

static int test_print_error_int(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error 42]]", &cap, "<error: 42>\n"));
  TEST_PASS();
}

static int test_print_error_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error [vec 1 2 3]]]", &cap, "<error: [vec 1 2 3]>\n"));
  TEST_PASS();
}

static int test_print_nonerror_unchanged(void) {
  PrintCapture cap;
  /* Verify non-error values print unchanged (no regression) */
  ASSERT(run_ok(
    "[print 42]\n"
    "[print \"hello\"]\n"
    "[print $nil]\n"
    "[print $true]",
    &cap, "42\nhello\nnil\ntrue\n"));
  TEST_PASS();
}

/* ===== US-002: Error propagation through arithmetic ops ===== */

static int test_prop_arithmetic_add(void) {
  PrintCapture cap;
  /* Error-flagged i32 propagates through + */
  ASSERT(run_ok(
    "[print [error? [+ [error 0] 1]]]\n"
    "[print [error? [+ 1 [error 0]]]]",
    &cap, "true\ntrue\n"));
  TEST_PASS();
}

static int test_prop_arithmetic_sub(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [- [error 0] 1]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_arithmetic_mul(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [* [error 0] 1]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_arithmetic_div(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [/ [error 0] 1]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_arithmetic_mod(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [% [error 0] 1]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-002: Error propagation through vec ops ===== */

static int test_prop_vec_get(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [vec-get [error \"x\"] 0]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_vec_push(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [error? [vec-push [error \"x\"] 1]]]\n"
    "[print [error? [vec-push [vec] [error \"y\"]]]]",
    &cap, "true\ntrue\n"));
  TEST_PASS();
}

static int test_prop_vec_set(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [vec-set [error \"x\"] 0 1]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_vec_len(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [vec-len [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_vec_concat(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [vec-concat [error \"x\"] [vec]]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_vec_slice(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [vec-slice [error \"x\"] 0 1]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-002: Error propagation through map ops ===== */

static int test_prop_map_get(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [map-get [error \"x\"] \"k\"]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_map_set(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [map-set [error \"x\"] \"k\" 1]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_map_remove(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [map-remove [error \"x\"] \"k\"]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_map_has(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [map-has [error \"x\"] \"k\"]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_map_len(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [map-len [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_map_keys(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [map-keys [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_map_vals(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [map-vals [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-002: Error propagation through string ops ===== */

static int test_prop_concat(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [error? [concat [error \"x\"] \"y\"]]]\n"
    "[print [error? [concat \"a\" [error \"y\"]]]]",
    &cap, "true\ntrue\n"));
  TEST_PASS();
}

static int test_prop_length(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [length [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_prop_to_string(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [to-string [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-003: OP_CHECK_ERROR auto-return from procs ===== */

static int test_auto_return_basic(void) {
  PrintCapture cap;
  /* Error in non-final position causes early return */
  ASSERT(run_ok(
    "proc foo {} {\n"
    "  error \"fail\"\n"
    "  print \"never\"\n"
    "  42\n"
    "}\n"
    "[print [error? [foo]]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_auto_return_no_print(void) {
  PrintCapture cap;
  /* Verify "never" is truly not printed */
  ASSERT(run_ok(
    "proc foo {} {\n"
    "  error \"fail\"\n"
    "  print \"never\"\n"
    "  42\n"
    "}\n"
    "def r [foo]\n"
    "[print [error-val $r]]",
    &cap, "fail\n"));
  TEST_PASS();
}

static int test_auto_return_nested(void) {
  PrintCapture cap;
  /* Errors from nested calls propagate up */
  ASSERT(run_ok(
    "proc inner {} { error \"deep\" }\n"
    "proc outer {} {\n"
    "  inner\n"
    "  print \"never\"\n"
    "  99\n"
    "}\n"
    "[print [error? [outer]]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_auto_return_final_expr(void) {
  PrintCapture cap;
  /* Final expression is NOT affected — error flag returned normally */
  ASSERT(run_ok(
    "proc foo {} { error \"end\" }\n"
    "[print [error? [foo]]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_auto_return_nonerror_continues(void) {
  PrintCapture cap;
  /* Non-error statements pop and continue normally */
  ASSERT(run_ok(
    "proc foo {} {\n"
    "  + 1 2\n"
    "  print \"reached\"\n"
    "  42\n"
    "}\n"
    "[print [foo]]",
    &cap, "reached\n42\n"));
  TEST_PASS();
}

/* ===== US-004: try basic catch ===== */

static int test_try_basic_catch(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "[print [try { error \"oops\" } e { concat \"caught: \" [error-val $e] }]]",
    &cap, "caught: oops\n"));
  TEST_PASS();
}

static int test_try_no_error(void) {
  PrintCapture cap;
  /* No error: body value returned, handler not invoked */
  ASSERT(run_ok("[print [try { 42 } e { 0 }]]", &cap, "42\n"));
  TEST_PASS();
}

static int test_try_error_binding(void) {
  PrintCapture cap;
  /* Handler has access to the error value via binding */
  ASSERT(run_ok(
    "[print [try { error 99 } e { error-val $e }]]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_try_multi_stmt_body(void) {
  PrintCapture cap;
  /* Error in first statement catches, subsequent statements skipped */
  ASSERT(run_ok(
    "[print [try {\n"
    "  error \"x\"\n"
    "  print \"skip\"\n"
    "  1\n"
    "} e { error-val $e }]]",
    &cap, "x\n"));
  TEST_PASS();
}

static int test_try_nested(void) {
  PrintCapture cap;
  /* Inner try catches before outer try */
  ASSERT(run_ok(
    "[print [try {\n"
    "  try { error \"inner\" } e { concat \"caught-inner: \" [error-val $e] }\n"
    "} e { \"outer\" }]]",
    &cap, "caught-inner: inner\n"));
  TEST_PASS();
}

static int test_try_nested_outer_catches(void) {
  PrintCapture cap;
  /* Inner handler re-throws, outer catches */
  ASSERT(run_ok(
    "[print [try {\n"
    "  try { error \"x\" } e { error \"re\" }\n"
    "} e { concat \"outer: \" [error-val $e] }]]",
    &cap, "outer: re\n"));
  TEST_PASS();
}

/* ===== US-005: stack-trace ===== */

static int test_stack_trace_empty(void) {
  PrintCapture cap;
  /* No error created yet: empty string */
  ASSERT(run_ok(
    "def t [stack-trace]\n"
    "[print [== $t \"\"]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_stack_trace_basic(void) {
  PrintCapture cap;
  /* Stack trace from error in a function shows the function name */
  ASSERT(run_ok(
    "proc foo {} { error \"x\" }\n"
    "[try { foo } e {\n"
    "  def t [stack-trace]\n"
    "  print [length $t]\n"
    "}]",
    &cap, NULL));
  /* Just verify stack-trace returns a non-empty string */
  ASSERT(cap.len > 0);
  int len_val = atoi(cap.buf);
  ASSERT(len_val > 0);
  TEST_PASS();
}

static int test_stack_trace_nested(void) {
  PrintCapture cap;
  /* Nested call chain produces multi-line trace */
  ASSERT(run_ok(
    "proc inner {} { error \"x\" }\n"
    "proc outer {} { inner }\n"
    "[try { outer } e {\n"
    "  def t [stack-trace]\n"
    "  print $t\n"
    "}]",
    &cap, NULL));
  /* Should contain both function names */
  ASSERT(strstr(cap.buf, "inner") != NULL);
  ASSERT(strstr(cap.buf, "outer") != NULL);
  TEST_PASS();
}

static int test_stack_trace_in_handler(void) {
  PrintCapture cap;
  /* stack-trace inside try handler returns the caught error's trace */
  ASSERT(run_ok(
    "proc fail {} { error \"boom\" }\n"
    "[try { fail } e {\n"
    "  def t [stack-trace]\n"
    "  print [error? $e]\n"
    "}]",
    &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-005: div-by-zero produces error with stack trace ===== */

static int test_div_zero_error(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def r [/ 1 0]\n"
    "[print [error? $r]]",
    &cap, "true\n"));
  TEST_PASS();
}

static int test_div_zero_stack_trace(void) {
  PrintCapture cap;
  /* Div-by-zero captures a stack trace */
  ASSERT(run_ok(
    "proc divbad {} { / 1 0 }\n"
    "[try { divbad } e {\n"
    "  def t [stack-trace]\n"
    "  print [length $t]\n"
    "}]",
    &cap, NULL));
  int len_val = atoi(cap.buf);
  ASSERT(len_val > 0);
  TEST_PASS();
}

static int test_mod_zero_error(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def r [% 1 0]\n"
    "[print [error? $r]]",
    &cap, "true\n"));
  TEST_PASS();
}

/* ===== Arity checks ===== */

static int test_arity_error_1arg(void) {
  /* error expects exactly 1 arg */
  ASSERT(run_err("[error]", "builtin 'error' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_error_2arg(void) {
  ASSERT(run_err("[error 1 2]", "builtin 'error' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_error_pred(void) {
  ASSERT(run_err("[error?]", "builtin 'error?' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_error_val(void) {
  ASSERT(run_err("[error-val]", "builtin 'error-val' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_stack_trace(void) {
  ASSERT(run_err("[stack-trace 1]", "builtin 'stack-trace' expects 0 arguments"));
  TEST_PASS();
}

static int test_arity_try(void) {
  ASSERT(run_err("[try { 1 }]", "try"));
  TEST_PASS();
}

/* ===== End-to-end integration: error through full pipeline ===== */

static int test_e2e_error_in_def(void) {
  PrintCapture cap;
  /* Store error in variable, retrieve it, extract payload */
  ASSERT(run_ok(
    "def e [error \"stored\"]\n"
    "[print [error? $e]]\n"
    "[print [error-val $e]]",
    &cap, "true\nstored\n"));
  TEST_PASS();
}

static int test_e2e_error_prop_chain(void) {
  PrintCapture cap;
  /* Error propagates through a chain of operations */
  ASSERT(run_ok(
    "def e [error 10]\n"
    "[print [error? [+ $e 1]]]\n"
    "[print [error-val [+ $e 1]]]",
    &cap, "true\n10\n"));
  TEST_PASS();
}

static int test_e2e_try_with_proc(void) {
  PrintCapture cap;
  /* Full pipeline: proc that errors, caught by try, payload extracted */
  ASSERT(run_ok(
    "proc risky {} {\n"
    "  error \"danger\"\n"
    "  42\n"
    "}\n"
    "def result [try { risky } e { error-val $e }]\n"
    "[print $result]",
    &cap, "danger\n"));
  TEST_PASS();
}

static int test_e2e_try_no_error_passthrough(void) {
  PrintCapture cap;
  /* Proc succeeds, try returns body value */
  ASSERT(run_ok(
    "proc safe {} { 42 }\n"
    "def result [try { safe } e { 0 }]\n"
    "[print $result]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_e2e_nested_try_propagation(void) {
  PrintCapture cap;
  /* Inner try catches, returns clean value; outer try not triggered */
  ASSERT(run_ok(
    "proc fail {} { error \"x\" }\n"
    "def r [try {\n"
    "  def inner [try { fail } e { \"recovered\" }]\n"
    "  concat $inner \"!\"\n"
    "} e { \"outer-caught\" }]\n"
    "[print $r]",
    &cap, "recovered!\n"));
  TEST_PASS();
}

static int test_e2e_error_val_preserves_type(void) {
  PrintCapture cap;
  /* error-val preserves the underlying type */
  ASSERT(run_ok(
    "def e1 [error 42]\n"
    "def e2 [error \"hello\"]\n"
    "def e3 [error $nil]\n"
    "[print [+ [error-val $e1] 1]]\n"
    "[print [concat [error-val $e2] \"!\"]]\n"
    "[print [error-val $e3]]",
    &cap, "43\nhello!\nnil\n"));
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-001: Error creation */
    { "error_create_string",        test_error_create_string },
    { "error_create_int",           test_error_create_int },
    { "error_create_nil",           test_error_create_nil },
    { "error_create_vec",           test_error_create_vec },
    { "error_create_map",           test_error_create_map },
    /* US-001: error? predicate */
    { "error_pred_on_error",        test_error_pred_on_error },
    { "error_pred_on_int",          test_error_pred_on_int },
    { "error_pred_on_nil",          test_error_pred_on_nil },
    { "error_pred_on_string",       test_error_pred_on_string },
    { "error_pred_on_bool",         test_error_pred_on_bool },
    /* US-001: error-val extraction */
    { "error_val_string",           test_error_val_string },
    { "error_val_int",              test_error_val_int },
    { "error_val_noerr",            test_error_val_noerr },
    /* US-001: print error values */
    { "print_error_string",         test_print_error_string },
    { "print_error_int",            test_print_error_int },
    { "print_error_vec",            test_print_error_vec },
    { "print_nonerror_unchanged",   test_print_nonerror_unchanged },
    /* US-002: Propagation through arithmetic */
    { "prop_arithmetic_add",        test_prop_arithmetic_add },
    { "prop_arithmetic_sub",        test_prop_arithmetic_sub },
    { "prop_arithmetic_mul",        test_prop_arithmetic_mul },
    { "prop_arithmetic_div",        test_prop_arithmetic_div },
    { "prop_arithmetic_mod",        test_prop_arithmetic_mod },
    /* US-002: Propagation through vec ops */
    { "prop_vec_get",               test_prop_vec_get },
    { "prop_vec_push",              test_prop_vec_push },
    { "prop_vec_set",               test_prop_vec_set },
    { "prop_vec_len",               test_prop_vec_len },
    { "prop_vec_concat",            test_prop_vec_concat },
    { "prop_vec_slice",             test_prop_vec_slice },
    /* US-002: Propagation through map ops */
    { "prop_map_get",               test_prop_map_get },
    { "prop_map_set",               test_prop_map_set },
    { "prop_map_remove",            test_prop_map_remove },
    { "prop_map_has",               test_prop_map_has },
    { "prop_map_len",               test_prop_map_len },
    { "prop_map_keys",              test_prop_map_keys },
    { "prop_map_vals",              test_prop_map_vals },
    /* US-002: Propagation through string ops */
    { "prop_concat",                test_prop_concat },
    { "prop_length",                test_prop_length },
    { "prop_to_string",             test_prop_to_string },
    /* US-003: Auto-return from procs */
    { "auto_return_basic",          test_auto_return_basic },
    { "auto_return_no_print",       test_auto_return_no_print },
    { "auto_return_nested",         test_auto_return_nested },
    { "auto_return_final_expr",     test_auto_return_final_expr },
    { "auto_return_nonerror_continues", test_auto_return_nonerror_continues },
    /* US-004: try catch */
    { "try_basic_catch",            test_try_basic_catch },
    { "try_no_error",               test_try_no_error },
    { "try_error_binding",          test_try_error_binding },
    { "try_multi_stmt_body",        test_try_multi_stmt_body },
    { "try_nested",                 test_try_nested },
    { "try_nested_outer_catches",   test_try_nested_outer_catches },
    /* US-005: Stack trace */
    { "stack_trace_empty",          test_stack_trace_empty },
    { "stack_trace_basic",          test_stack_trace_basic },
    { "stack_trace_nested",         test_stack_trace_nested },
    { "stack_trace_in_handler",     test_stack_trace_in_handler },
    /* US-005: Div-by-zero */
    { "div_zero_error",             test_div_zero_error },
    { "div_zero_stack_trace",       test_div_zero_stack_trace },
    { "mod_zero_error",             test_mod_zero_error },
    /* Arity checks */
    { "arity_error_1arg",           test_arity_error_1arg },
    { "arity_error_2arg",           test_arity_error_2arg },
    { "arity_error_pred",           test_arity_error_pred },
    { "arity_error_val",            test_arity_error_val },
    { "arity_stack_trace",          test_arity_stack_trace },
    { "arity_try",                  test_arity_try },
    /* End-to-end integration */
    { "e2e_error_in_def",           test_e2e_error_in_def },
    { "e2e_error_prop_chain",       test_e2e_error_prop_chain },
    { "e2e_try_with_proc",          test_e2e_try_with_proc },
    { "e2e_try_no_error_passthrough", test_e2e_try_no_error_passthrough },
    { "e2e_nested_try_propagation", test_e2e_nested_try_propagation },
    { "e2e_error_val_preserves_type", test_e2e_error_val_preserves_type },
  };

  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0;
  int failed = 0;

  for (int i = 0; i < total; i++) {
    printf("  %-40s ", tests[i].name);
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n  %d/%d passed\n", passed, total);
  return failed > 0 ? 1 : 0;
}
