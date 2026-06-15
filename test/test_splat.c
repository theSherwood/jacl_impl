#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-006: Splat/spread in juxtaposition ===== */

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

/* --- Test: basic spread with + --- */
static int test_spread_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def nums [vec 1 2 3]\n"
    "[print [+ ..$nums]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "6\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: mixed spread and regular args --- */
static int test_spread_mixed(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def nums [vec 1 2 3]\n"
    "[print [+ 10 ..$nums 20]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "36\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: multiple spreads --- */
static int test_spread_multiple(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def a [vec 1 2]\n"
    "def b [vec 3 4]\n"
    "[print [+ ..$a ..$b]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: empty spread produces zero arguments --- */
static int test_spread_empty(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def empty [vec]\n"
    "[print [+ 10 ..$empty 20]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread with * operator --- */
static int test_spread_multiply(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def nums [vec 2 3 4]\n"
    "[print [* ..$nums]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "24\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread into user-defined proc --- */
static int test_spread_proc(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc add3 {a, b, c} { + [+ $a $b] $c }\n"
    "def args [vec 10 20 30]\n"
    "[print [add3 ..$args]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "60\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: non-vector spread is runtime error --- */
static int test_spread_non_vector(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [+ ..42]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread with single-element vector --- */
static int test_spread_single(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def nums [vec 5]\n"
    "[print [+ 10 ..$nums]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread only (no fixed args) with single element --- */
static int test_spread_single_only(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def nums [vec 42]\n"
    "[print [+ ..$nums]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread into proc with mixed args --- */
static int test_spread_proc_mixed(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc greet {a, b, c} {\n"
    "  print $a\n"
    "  print $b\n"
    "  print $c\n"
    "}\n"
    "def rest [vec 2 3]\n"
    "[greet 1 ..$rest]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread into vec constructor --- */
static int test_spread_vec(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def g [vec 2 7]\n"
    "print [vec 10 100 ..$g]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 10 100 2 7]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread only args into vec --- */
static int test_spread_vec_only(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def a [vec 1 2]\n"
    "def b [vec 3 4]\n"
    "print [vec ..$a ..$b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2 3 4]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "spread_basic",        test_spread_basic },
    { "spread_mixed",        test_spread_mixed },
    { "spread_multiple",     test_spread_multiple },
    { "spread_empty",        test_spread_empty },
    { "spread_multiply",     test_spread_multiply },
    { "spread_proc",         test_spread_proc },
    { "spread_non_vector",   test_spread_non_vector },
    { "spread_single",       test_spread_single },
    { "spread_single_only",  test_spread_single_only },
    { "spread_proc_mixed",   test_spread_proc_mixed },
    { "spread_vec",          test_spread_vec },
    { "spread_vec_only",     test_spread_vec_only },
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
