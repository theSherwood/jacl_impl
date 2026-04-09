#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-014: Sequence operations — count, take, first ===== */

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

/* --- count tests --- */

static int test_count_vector(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [count [vec 1 2 3]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_count_empty_vector(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [count [vec]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_count_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "}\n"
    "[print [count [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_count_empty_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  i : 0\n"
    "  while [< $i 0] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [count [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_count_filtered_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "}\n"
    "[print [count [filter [gen] [\\ > $it 3]]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "2\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- take tests --- */

static int test_take_vector(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [take [vec 1 2 3 4 5] 3]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2 3]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_take_vector_exceeds(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [take [vec 1 2] 5]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_take_vector_zero(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [take [vec 1 2 3] 0]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_take_stream_lazy(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "}\n"
    "def s [take [gen] 3]\n"
    "print \"<stream>\"\n"
    "def v [collect $s]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "<stream>\n[vec 1 2 3]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_take_stream_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 10]\n"
    "  [yield 20]\n"
    "  [yield 30]\n"
    "  [yield 40]\n"
    "  [yield 50]\n"
    "}\n"
    "[print [collect [take [gen] 3]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 10 20 30]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_take_zero_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "}\n"
    "[print [collect [take [gen] 0]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- first tests --- */

static int test_first_vector(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [first [vec 10 20 30]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_first_empty_vector(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [if [== [first [vec]] $nil] { \"nil\" } { \"nope\" }]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_first_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 42]\n"
    "  [yield 99]\n"
    "}\n"
    "[print [first [gen]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_first_empty_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  i : 0\n"
    "  while [< $i 0] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "[print [if [== [first [gen]] $nil] { \"nil\" } { \"nope\" }]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- composition / pipeline tests --- */

static int test_pipe_take_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "}\n"
    "[gen] | take 3 | collect | print\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2 3]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_pipe_filter_take(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "  [yield 6]\n"
    "  [yield 7]\n"
    "}\n"
    "[gen] | filter [\\ > $it 3] | take 2 | collect | print\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 4 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_take_then_count(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "}\n"
    "[print [count [take [gen] 3]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_first_on_filtered(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "}\n"
    "[print [first [filter [gen] [\\ > $it 2]]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* count */
    { "count_vector",            test_count_vector },
    { "count_empty_vector",      test_count_empty_vector },
    { "count_stream",            test_count_stream },
    { "count_empty_stream",      test_count_empty_stream },
    { "count_filtered_stream",   test_count_filtered_stream },
    /* take */
    { "take_vector",             test_take_vector },
    { "take_vector_exceeds",     test_take_vector_exceeds },
    { "take_vector_zero",        test_take_vector_zero },
    { "take_stream_lazy",        test_take_stream_lazy },
    { "take_stream_collect",     test_take_stream_collect },
    { "take_zero_stream",        test_take_zero_stream },
    /* first */
    { "first_vector",            test_first_vector },
    { "first_empty_vector",      test_first_empty_vector },
    { "first_stream",            test_first_stream },
    { "first_empty_stream",      test_first_empty_stream },
    /* composition */
    { "pipe_take_collect",       test_pipe_take_collect },
    { "pipe_filter_take",        test_pipe_filter_take },
    { "take_then_count",         test_take_then_count },
    { "first_on_filtered",       test_first_on_filtered },
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
