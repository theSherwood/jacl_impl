#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== Print capture helper ===== */

typedef struct {
  char     buf[4096];
  uint32_t len;
} PrintCapture;

static void capture_print(const char* text, uint32_t len, void* ctx) {
  PrintCapture* cap = (PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

/* Helper: run program, capture output, assert success */
static int run_ok(const char* source, PrintCapture* cap, const char* expected) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  if (cap) { cap->len = 0; cap->buf[0] = '\0'; }
  VM vm;
  vm_init(&vm, &arena);
  if (cap) {
    vm.print_fn  = capture_print;
    vm.print_ctx = cap;
  }

  VMResult result = jacl_run(source, &vm, &arena);
  if (result != VM_OK) {
    fprintf(stderr, "  Expected VM_OK but got error: %s\n",
            vm.error_message ? vm.error_message : "(null)");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (expected && cap) {
    if (strcmp(cap->buf, expected) != 0) {
      fprintf(stderr, "  Output mismatch:\n  Actual:   '%s'\n  Expected: '%s'\n",
              cap->buf, expected);
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 0;
    }
  }

  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

/* Helper: run program, expect compile or runtime error containing substring */
static int run_err(const char* source, const char* err_substr) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  VMResult result = jacl_run(source, &vm, &arena);
  if (result != VM_RUNTIME_ERROR) {
    fprintf(stderr, "  Expected VM_RUNTIME_ERROR but got VM_OK\n");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (err_substr && (!vm.error_message || !strstr(vm.error_message, err_substr))) {
    fprintf(stderr, "  Error message '%s' does not contain '%s'\n",
            vm.error_message ? vm.error_message : "(null)", err_substr);
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }

  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

/* ===== US-002: mut local creation and reading ===== */

static int test_mut_local_int(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut x 42\n"
    "  print $x\n"
    "}\n"
    "[t]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_mut_local_zero(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut y 0\n"
    "  print $y\n"
    "}\n"
    "[t]",
    &cap, "0\n"));
  TEST_PASS();
}

static int test_mut_local_string(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut s \"hello\"\n"
    "  print $s\n"
    "}\n"
    "[t]",
    &cap, "hello\n"));
  TEST_PASS();
}

static int test_mut_local_nil(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut n $nil\n"
    "  print $n\n"
    "}\n"
    "[t]",
    &cap, "nil\n"));
  TEST_PASS();
}

static int test_mut_returns_nil(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  r = [mut x 42]\n"
    "  print $r\n"
    "}\n"
    "[t]",
    &cap, "nil\n"));
  TEST_PASS();
}

/* ===== US-003: set! on mutable locals ===== */

static int test_set_local_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut x 0\n"
    "  x :: 42\n"
    "  print $x\n"
    "}\n"
    "[t]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_set_local_expr(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut y 10\n"
    "  y :: [+ $y 5]\n"
    "  print $y\n"
    "}\n"
    "[t]",
    &cap, "15\n"));
  TEST_PASS();
}

static int test_set_local_multiple(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut x 0\n"
    "  x :: 1\n"
    "  x :: 2\n"
    "  x :: 3\n"
    "  print $x\n"
    "}\n"
    "[t]",
    &cap, "3\n"));
  TEST_PASS();
}

static int test_set_returns_nil(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut x 0\n"
    "  r = [set x 42]\n"
    "  print $r\n"
    "}\n"
    "[t]",
    &cap, "nil\n"));
  TEST_PASS();
}

/* ===== US-003: set! compile-time errors ===== */

static int test_set_immutable_def_error(void) {
  ASSERT(run_err(
    "proc t {} {\n"
    "  def x 1\n"
    "  x :: 2\n"
    "}\n",
    "cannot mutate immutable binding"));
  TEST_PASS();
}

static int test_set_param_error(void) {
  ASSERT(run_err(
    "proc t {x} {\n"
    "  x :: 2\n"
    "}\n",
    "cannot mutate parameter"));
  TEST_PASS();
}

static int test_set_undeclared_error(void) {
  ASSERT(run_err(
    "proc t {} {\n"
    "  zzz :: 1\n"
    "}\n",
    "undefined variable"));
  TEST_PASS();
}

/* ===== US-004: mut/set! on mutable globals ===== */

static int test_mut_global_basic(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "mut cnt 0\n"
    "cnt :: 1\n"
    "cnt :: [+ $cnt 1]\n"
    "[print $cnt]",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_mut_global_multiple_set(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "mut g 0\n"
    "g :: 10\n"
    "g :: 20\n"
    "g :: 30\n"
    "[print $g]",
    &cap, "30\n"));
  TEST_PASS();
}

static int test_set_global_immutable_error(void) {
  ASSERT(run_err(
    "def x 1\n"
    "x :: 2",
    "cannot mutate immutable binding"));
  TEST_PASS();
}

/* ===== US-005: mutable closure capture ===== */

static int test_closure_shared_mutation(void) {
  PrintCapture cap;
  /* Counter pattern: closure mutates shared variable */
  ASSERT(run_ok(
    "proc test {} {\n"
    "  mut n 0\n"
    "  proc inc {} { set n [+ $n 1] }\n"
    "  proc get {} { + $n 0 }\n"
    "  inc\n"
    "  inc\n"
    "  print [get]\n"
    "}\n"
    "[test]",
    &cap, "2\n"));
  TEST_PASS();
}

static int test_closure_bidirectional(void) {
  PrintCapture cap;
  /* Mutation in enclosing scope visible in closure */
  ASSERT(run_ok(
    "proc test {} {\n"
    "  mut x 0\n"
    "  proc get {} { + $x 0 }\n"
    "  x :: 42\n"
    "  print [get]\n"
    "}\n"
    "[test]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_closure_multiple_sharing(void) {
  PrintCapture cap;
  /* Two closures share the same mut local */
  ASSERT(run_ok(
    "proc test {} {\n"
    "  mut v 0\n"
    "  proc add1 {} { set v [+ $v 1] }\n"
    "  proc add2 {} { set v [+ $v 10] }\n"
    "  add1\n"
    "  add2\n"
    "  print [+ $v 0]\n"
    "}\n"
    "[test]",
    &cap, "11\n"));
  TEST_PASS();
}

static int test_closure_transitive(void) {
  PrintCapture cap;
  /* Inner closure captures mut from grandparent scope */
  ASSERT(run_ok(
    "proc outer {} {\n"
    "  mut x 0\n"
    "  proc mid {} {\n"
    "    proc inn {} { set x [+ $x 1] }\n"
    "    inn\n"
    "  }\n"
    "  mid\n"
    "  print [+ $x 0]\n"
    "}\n"
    "[outer]",
    &cap, "1\n"));
  TEST_PASS();
}

/* ===== US-006: box creation and box? predicate ===== */

static int test_box_create(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [box? [box 42]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_box_pred_false(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [box? 42]]", &cap, "false\n"));
  TEST_PASS();
}

static int test_box_error_propagation(void) {
  PrintCapture cap;
  /* box on error-flagged value propagates error */
  ASSERT(run_ok("[print [error? [box [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-006: atom creation and atom? predicate ===== */

static int test_atom_create(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [atom? [atom 0]]]", &cap, "true\n"));
  TEST_PASS();
}

static int test_atom_pred_false(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [atom? 0]]", &cap, "false\n"));
  TEST_PASS();
}

static int test_atom_error_propagation(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [atom [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-007: deref ===== */

static int test_deref_box(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [deref [box 42]]]", &cap, "42\n"));
  TEST_PASS();
}

static int test_deref_atom(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [deref [atom 99]]]", &cap, "99\n"));
  TEST_PASS();
}

static int test_deref_wrong_type(void) {
  ASSERT(run_err("[deref 42]", "deref: expected box or atom"));
  TEST_PASS();
}

static int test_deref_error_propagation(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [deref [error \"x\"]]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-007: reset! ===== */

static int test_reset_box(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def b [box 0]\n"
    "[reset $b 99]\n"
    "[print [deref $b]]",
    &cap, "99\n"));
  TEST_PASS();
}

static int test_reset_atom(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def a [atom 0]\n"
    "[reset $a 77]\n"
    "[print [deref $a]]",
    &cap, "77\n"));
  TEST_PASS();
}

static int test_reset_returns_new_value(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def b [box 0]\n"
    "[print [reset $b 42]]",
    &cap, "42\n"));
  TEST_PASS();
}

static int test_reset_wrong_type(void) {
  ASSERT(run_err("[reset 42 99]", "reset!: expected box or atom"));
  TEST_PASS();
}

static int test_reset_error_propagation(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [error? [reset [error \"x\"] 1]]]", &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-007: swap! ===== */

static int test_swap_box(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def b [box 0]\n"
    "proc inc {x} { + $x 1 }\n"
    "[swap $b $inc]\n"
    "[print [deref $b]]",
    &cap, "1\n"));
  TEST_PASS();
}

static int test_swap_atom(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def a [atom 10]\n"
    "proc dbl {x} { * $x 2 }\n"
    "[swap $a $dbl]\n"
    "[print [deref $a]]",
    &cap, "20\n"));
  TEST_PASS();
}

static int test_swap_returns_result(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "def b [box 5]\n"
    "proc inc {x} { + $x 1 }\n"
    "[print [swap $b $inc]]",
    &cap, "6\n"));
  TEST_PASS();
}

static int test_swap_wrong_container(void) {
  ASSERT(run_err(
    "proc f {x} { + $x 0 }\n"
    "[swap 42 $f]",
    "swap!: expected box or atom"));
  TEST_PASS();
}

static int test_swap_error_propagation(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc f {x} { + $x 0 }\n"
    "[print [error? [swap [error \"x\"] $f]]]",
    &cap, "true\n"));
  TEST_PASS();
}

/* ===== US-008: print formatting for box and atom ===== */

static int test_print_box_int(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [box 42]]", &cap, "<box: 42>\n"));
  TEST_PASS();
}

static int test_print_box_string(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [box \"hi\"]]", &cap, "<box: \"hi\">\n"));
  TEST_PASS();
}

static int test_print_box_vec(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [box [vec 1 2 3]]]", &cap, "<box: [vec 1 2 3]>\n"));
  TEST_PASS();
}

static int test_print_atom_int(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [atom 42]]", &cap, "<atom: 42>\n"));
  TEST_PASS();
}

static int test_print_atom_string(void) {
  PrintCapture cap;
  ASSERT(run_ok("[print [atom \"hi\"]]", &cap, "<atom: \"hi\">\n"));
  TEST_PASS();
}

static int test_print_cell_transparent(void) {
  PrintCapture cap;
  /* Cell (internal to mut) is transparent — prints the inner value directly */
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut x 42\n"
    "  print $x\n"
    "}\n"
    "[t]",
    &cap, "42\n"));
  TEST_PASS();
}

/* ===== Arity checks ===== */

static int test_arity_mut(void) {
  ASSERT(run_err("mut x", "mut"));
  TEST_PASS();
}

static int test_arity_set(void) {
  ASSERT(run_err(
    "proc t {} {\n"
    "  mut x 0\n"
    "  set x\n"
    "}\n",
    "set"));
  TEST_PASS();
}

static int test_arity_box(void) {
  ASSERT(run_err("[box]", "builtin 'box' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_atom(void) {
  ASSERT(run_err("[atom]", "builtin 'atom' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_deref(void) {
  ASSERT(run_err("[deref]", "builtin 'deref' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_reset(void) {
  ASSERT(run_err("[reset [box 0]]", "builtin 'reset' expects 2 arguments"));
  TEST_PASS();
}

static int test_arity_swap(void) {
  ASSERT(run_err("[swap [box 0]]", "builtin 'swap' expects 2 arguments"));
  TEST_PASS();
}

static int test_arity_box_pred(void) {
  ASSERT(run_err("[box?]", "builtin 'box?' expects 1 argument"));
  TEST_PASS();
}

static int test_arity_atom_pred(void) {
  ASSERT(run_err("[atom?]", "builtin 'atom?' expects 1 argument"));
  TEST_PASS();
}

/* ===== End-to-end integration ===== */

static int test_e2e_counter_pattern(void) {
  PrintCapture cap;
  /* Full counter pattern using mut + closures */
  ASSERT(run_ok(
    "proc mkctr {} {\n"
    "  mut n 0\n"
    "  proc inc {} { set n [+ $n 1] }\n"
    "  proc get {} { + $n 0 }\n"
    "  inc\n"
    "  inc\n"
    "  inc\n"
    "  + $n 0\n"
    "}\n"
    "[print [mkctr]]",
    &cap, "3\n"));
  TEST_PASS();
}

static int test_e2e_box_accumulator(void) {
  PrintCapture cap;
  /* Box as accumulator passed around */
  ASSERT(run_ok(
    "def b [box 0]\n"
    "proc add {x} { reset $b [+ [deref $b] $x] }\n"
    "[add 10]\n"
    "[add 20]\n"
    "[add 30]\n"
    "[print [deref $b]]",
    &cap, "60\n"));
  TEST_PASS();
}

static int test_e2e_swap_chain(void) {
  PrintCapture cap;
  /* Multiple swap! calls on same container */
  ASSERT(run_ok(
    "def b [box 1]\n"
    "proc dbl {x} { * $x 2 }\n"
    "[swap $b $dbl]\n"
    "[swap $b $dbl]\n"
    "[swap $b $dbl]\n"
    "[print [deref $b]]",
    &cap, "8\n"));
  TEST_PASS();
}

static int test_e2e_global_mut_with_proc(void) {
  PrintCapture cap;
  /* Global mut variable modified by procs */
  ASSERT(run_ok(
    "mut total 0\n"
    "proc add {x} { set total [+ $total $x] }\n"
    "[add 5]\n"
    "[add 15]\n"
    "[print $total]",
    &cap, "20\n"));
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-002: mut local creation and reading */
    { "mut_local_int",               test_mut_local_int },
    { "mut_local_zero",              test_mut_local_zero },
    { "mut_local_string",            test_mut_local_string },
    { "mut_local_nil",               test_mut_local_nil },
    { "mut_returns_nil",             test_mut_returns_nil },
    /* US-003: set! on mutable locals */
    { "set_local_basic",             test_set_local_basic },
    { "set_local_expr",              test_set_local_expr },
    { "set_local_multiple",          test_set_local_multiple },
    { "set_returns_nil",             test_set_returns_nil },
    /* US-003: set! compile-time errors */
    { "set_immutable_def_error",     test_set_immutable_def_error },
    { "set_param_error",             test_set_param_error },
    { "set_undeclared_error",        test_set_undeclared_error },
    /* US-004: mut/set! on mutable globals */
    { "mut_global_basic",            test_mut_global_basic },
    { "mut_global_multiple_set",     test_mut_global_multiple_set },
    { "set_global_immutable_error",  test_set_global_immutable_error },
    /* US-005: mutable closure capture */
    { "closure_shared_mutation",     test_closure_shared_mutation },
    { "closure_bidirectional",       test_closure_bidirectional },
    { "closure_multiple_sharing",    test_closure_multiple_sharing },
    { "closure_transitive",          test_closure_transitive },
    /* US-006: box creation and predicate */
    { "box_create",                  test_box_create },
    { "box_pred_false",              test_box_pred_false },
    { "box_error_propagation",       test_box_error_propagation },
    /* US-006: atom creation and predicate */
    { "atom_create",                 test_atom_create },
    { "atom_pred_false",             test_atom_pred_false },
    { "atom_error_propagation",      test_atom_error_propagation },
    /* US-007: deref */
    { "deref_box",                   test_deref_box },
    { "deref_atom",                  test_deref_atom },
    { "deref_wrong_type",            test_deref_wrong_type },
    { "deref_error_propagation",     test_deref_error_propagation },
    /* US-007: reset! */
    { "reset_box",                   test_reset_box },
    { "reset_atom",                  test_reset_atom },
    { "reset_returns_new_value",     test_reset_returns_new_value },
    { "reset_wrong_type",            test_reset_wrong_type },
    { "reset_error_propagation",     test_reset_error_propagation },
    /* US-007: swap! */
    { "swap_box",                    test_swap_box },
    { "swap_atom",                   test_swap_atom },
    { "swap_returns_result",         test_swap_returns_result },
    { "swap_wrong_container",        test_swap_wrong_container },
    { "swap_error_propagation",      test_swap_error_propagation },
    /* US-008: print formatting */
    { "print_box_int",               test_print_box_int },
    { "print_box_string",            test_print_box_string },
    { "print_box_vec",               test_print_box_vec },
    { "print_atom_int",              test_print_atom_int },
    { "print_atom_string",           test_print_atom_string },
    { "print_cell_transparent",      test_print_cell_transparent },
    /* Arity checks */
    { "arity_mut",                   test_arity_mut },
    { "arity_set",                   test_arity_set },
    { "arity_box",                   test_arity_box },
    { "arity_atom",                  test_arity_atom },
    { "arity_deref",                 test_arity_deref },
    { "arity_reset",                 test_arity_reset },
    { "arity_swap",                  test_arity_swap },
    { "arity_box_pred",              test_arity_box_pred },
    { "arity_atom_pred",             test_arity_atom_pred },
    /* End-to-end integration */
    { "e2e_counter_pattern",         test_e2e_counter_pattern },
    { "e2e_box_accumulator",         test_e2e_box_accumulator },
    { "e2e_swap_chain",              test_e2e_swap_chain },
    { "e2e_global_mut_with_proc",    test_e2e_global_mut_with_proc },
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
