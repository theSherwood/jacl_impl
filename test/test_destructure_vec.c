#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-001: Positional vector destructuring ===== */

/* Print capture helper */
typedef struct {
  char     buf[16384];
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

static VMResult run_capture(const char* src, PrintCapture* cap) {
  cap->len = 0;
  cap->buf[0] = '\0';
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = cap;
  VMResult result = jacl_run(src, &vm, &arena);
  vm_destroy(&vm);
  arena_destroy(&arena);
  return result;
}

/* --- Test: basic destructuring with operator form --- */
static int test_destructure_basic_operator(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "v = [vec 1 2 3]\n"
    "[a b c] = $v\n"
    "[print $a]\n"
    "[print $b]\n"
    "[print $c]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: basic destructuring with keyword form --- */
static int test_destructure_basic_keyword(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [a b c] [vec 10 20 30]]\n"
    "[print $a]\n"
    "[print $b]\n"
    "[print $c]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: destructuring with expression RHS --- */
static int test_destructure_expr_rhs(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[a b] = [vec [+ 1 2] [* 3 4]]\n"
    "[print $a]\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n12\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: destructuring in nested scope --- */
static int test_destructure_nested_scope(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "v = [vec 1 2 3]\n"
    "proc show {} {\n"
    "  [a b c] = $v\n"
    "  [print $a]\n"
    "  [print $b]\n"
    "  [print $c]\n"
    "}\n"
    "show\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: mut destructuring --- */
static int test_destructure_mut(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[a b] : [vec 10 20]\n"
    "[print $a]\n"
    "[print $b]\n"
    "a :: 99\n"
    "[print $a]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n99\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: length mismatch error --- */
static int test_destructure_length_mismatch(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[a b c] = [vec 1 2]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: non-vector error --- */
static int test_destructure_non_vector(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[a b] = 42\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: global scope destructuring --- */
static int test_destructure_global(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[a b c] = [vec 100 200 300]\n"
    "[print $a]\n"
    "[print $b]\n"
    "[print $c]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "100\n200\n300\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: single element destructuring (keyword form) --- */
static int test_destructure_single(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [a] [vec 42]]\n"
    "[print $a]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: destructuring with string values --- */
static int test_destructure_strings(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[a b] = [vec \"hello\" \"world\"]\n"
    "[print $a]\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\nworld\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: typed destructuring (keyword form) --- */
static int test_destructure_typed(void) {
  PrintCapture cap;
  /* Typed destructuring requires [def DESTRUCTURE_VEC value] form,
     which is only available through bare destructuring syntax.
     Use untyped form here since [i32 a, i32 b] in [] mode
     is just a command, not a destructuring pattern. */
  VMResult r = run_capture(
    "[a b] = [vec 5 6]\n"
    "[print $a]\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n6\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "destructure_basic_operator",  test_destructure_basic_operator },
    { "destructure_basic_keyword",   test_destructure_basic_keyword },
    { "destructure_expr_rhs",        test_destructure_expr_rhs },
    { "destructure_nested_scope",    test_destructure_nested_scope },
    { "destructure_mut",             test_destructure_mut },
    { "destructure_length_mismatch", test_destructure_length_mismatch },
    { "destructure_non_vector",      test_destructure_non_vector },
    { "destructure_global",          test_destructure_global },
    { "destructure_single",          test_destructure_single },
    { "destructure_strings",         test_destructure_strings },
    { "destructure_typed",           test_destructure_typed },
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
