#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== Print capture helper ===== */

typedef struct {
  char     buf[1024];
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

/* ===== US-010: End-to-end M4 integration tests ===== */

/* Test: proc + call — x = 10 proc double {n} { [* $n 2] } [print [double $x]] */
static int test_proc_call_pipeline(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "x = 10\n"
    "proc double {n} { [* $n 2] }\n"
    "[print [double $x]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "20\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: factorial via recursion — fact(10) = 3628800 */
static int test_factorial(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc fact {n} {\n"
    "  [if [== $n 0] { 1 } { [* $n [fact [- $n 1]]] }]\n"
    "}\n"
    "[print [fact 10]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3628800\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: fibonacci via recursion — fib(10) = 55 */
static int test_fibonacci(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc fib {n} {\n"
    "  [if [< $n 2] { [+ $n 0] } { [+ [fib [- $n 1]] [fib [- $n 2]]] }]\n"
    "}\n"
    "[print [fib 10]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "55\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: closure captures variable from enclosing scope (make-adder) */
static int test_closure_capture(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc mkadd {x} {\n"
    "  proc inner {y} { [+ $x $y] }\n"
    "}\n"
    "add10 = [mkadd 10]\n"
    "[print [add10 5]]\n"
    "[print [add10 90]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n100\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: if/while with various truthy/falsy values */
static int test_truthy_falsy(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[print [if 0 { \"yes\" } { \"no\" }]]\n"          /* 0 is truthy */
    "[print [if $nil { \"yes\" } { \"no\" }]]\n"        /* nil is falsy */
    "[print [if $false { \"yes\" } { \"no\" }]]\n"      /* false is falsy */
    "[print [if $true { \"yes\" } { \"no\" }]]\n"       /* true is truthy */
    "[print [if 42 { \"yes\" } { \"no\" }]]";           /* positive int is truthy */

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "yes\nno\nno\nyes\nyes\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested procedure definitions and calls */
static int test_nested_procs(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc outer {a} {\n"
    "  proc mid {b} {\n"
    "    proc inner {c} { [+ [+ $a $b] $c] }\n"
    "  }\n"
    "}\n"
    "m = [outer 100]\n"
    "i = [m 20]\n"
    "[print [i 3]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "123\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: error cases — wrong arg count on call */
static int test_error_wrong_argc(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  const char* program =
    "proc add {a, b} { [+ $a $b] }\n"
    "[add 1]";  /* wrong: 1 arg instead of 2 */

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "argument") != NULL);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: error — calling non-function */
static int test_error_call_non_function(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  const char* program =
    "x = 42\n"
    "[x 1]";  /* x is i32, not callable */

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "cannot call") != NULL);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: error — undefined variable inside proc body */
static int test_error_undefined_in_proc(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  const char* program =
    "proc f {} { $nope }\n"
    "[f]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "nope") != NULL);

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while loop computes iterative sum using def rebinding */
static int test_while_iterative_sum(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "i : 1\n"
    "sum : 0\n"
    "[while [<= $i 10] {\n"
    "  sum :: [+ $sum $i]\n"
    "  i :: [+ $i 1]\n"
    "}]\n"
    "[print $sum]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "55\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-010: End-to-end M4 integration */
    { "proc_call_pipeline",       test_proc_call_pipeline },
    { "factorial",                test_factorial },
    { "fibonacci",                test_fibonacci },
    { "closure_capture",          test_closure_capture },
    { "truthy_falsy",             test_truthy_falsy },
    { "nested_procs",             test_nested_procs },
    { "error_wrong_argc",         test_error_wrong_argc },
    { "error_call_non_function",  test_error_call_non_function },
    { "error_undefined_in_proc",  test_error_undefined_in_proc },
    { "while_iterative_sum",      test_while_iterative_sum },
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
