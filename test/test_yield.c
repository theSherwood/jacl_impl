#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-009: OP_YIELD and stream creation ===== */

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

/* --- Test: basic generator yields 3 values then exhausts --- */
static int test_yield_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "def s [gen]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield in a loop --- */
static int test_yield_loop(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc counter {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [counter 4]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n2\n3\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: stream exhaustion returns nil repeatedly --- */
static int test_yield_exhaustion(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 42]\n"
    "}\n"
    "def s [gen]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\nnil\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: calling generator returns a stream (not executing body) --- */
static int test_yield_lazy(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [print \"body\"]\n"
    "  [yield 1]\n"
    "}\n"
    "def s [gen]\n"
    "[print \"before\"]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* Body executes on first stream_next, so "body" prints before "1" but after "before" */
  ASSERT_STR_EQ(cap.buf, "before\nbody\n1\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield with arguments to generator proc --- */
static int test_yield_with_args(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc range {start, end} {\n"
    "  mut i $start\n"
    "  while [< $i $end] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [range 3 6]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n4\n5\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield in nested conditionals --- */
static int test_yield_conditional(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc filt {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    if [== [% $i 2] 0] {\n"
    "      [yield $i]\n"
    "    }\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [filt 6]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n2\n4\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: print stream value shows <stream> --- */
static int test_yield_print_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "}\n"
    "def s [gen]\n"
    "[print $s]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "<stream>\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: multiple independent generators --- */
static int test_yield_multiple_streams(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {x} {\n"
    "  [yield $x]\n"
    "  [yield [+ $x 10]]\n"
    "}\n"
    "def a [gen 1]\n"
    "def b [gen 2]\n"
    "[print [stream_next $a]]\n"
    "[print [stream_next $b]]\n"
    "[print [stream_next $a]]\n"
    "[print [stream_next $b]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n11\n12\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: yield string values --- */
static int test_yield_strings(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc words {} {\n"
    "  [yield \"hello\"]\n"
    "  [yield \"world\"]\n"
    "}\n"
    "def s [words]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\nworld\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: zero-arg generator that yields computed values --- */
static int test_yield_computed(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc fibs {} {\n"
    "  mut a 0\n"
    "  mut b 1\n"
    "  [yield $a]\n"
    "  [yield $b]\n"
    "  mut i 0\n"
    "  while [< $i 3] {\n"
    "    def c [+ $a $b]\n"
    "    [yield $c]\n"
    "    a :: $b\n"
    "    b :: $c\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s [fibs]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n"
    "[print [stream_next $s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* fib: 0, 1, 1, 2, 3, then exhausted */
  ASSERT_STR_EQ(cap.buf, "0\n1\n1\n2\n3\nnil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */
typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "yield_basic",            test_yield_basic },
    { "yield_loop",             test_yield_loop },
    { "yield_exhaustion",       test_yield_exhaustion },
    { "yield_lazy",             test_yield_lazy },
    { "yield_with_args",        test_yield_with_args },
    { "yield_conditional",      test_yield_conditional },
    { "yield_print_stream",     test_yield_print_stream },
    { "yield_multiple_streams", test_yield_multiple_streams },
    { "yield_strings",          test_yield_strings },
    { "yield_computed",         test_yield_computed },
  };

  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0;
  int failed = 0;

  for (int i = 0; i < total; i++) {
    printf("  %-35s ", tests[i].name);
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n  %d/%d passed\n", passed, total);
  return failed > 0 ? 1 : 0;
}
