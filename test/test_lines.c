#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-015: lines — string to line stream ===== */

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

/* --- Test: basic lines — split "a\nb\nc" --- */
static int test_lines_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [lines \"a\\nb\\nc\"]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"a\" \"b\" \"c\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: \\r\\n handling --- */
static int test_lines_crlf(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [lines \"hello\\r\\nworld\"]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"hello\" \"world\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: no trailing empty line --- */
static int test_lines_no_trailing_empty(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [lines \"a\\nb\\n\"]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"a\" \"b\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: lines + for iteration --- */
static int test_lines_for(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "for [lines \"one\\ntwo\\nthree\"] {\n"
    "  [print $it]\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "one\ntwo\nthree\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: lines + collect --- */
static int test_lines_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def v [collect [lines \"x\\ny\\nz\"]]\n"
    "[print [count $v]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: lines + filter pipeline --- */
static int test_lines_filter(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [filter [lines \"aa\\nbb\\ncc\\ndd\"] [\\ > $it \"bb\"]]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"cc\" \"dd\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: empty string yields empty stream --- */
static int test_lines_empty_string(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [lines \"\"]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: single line no newline --- */
static int test_lines_single(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [lines \"hello\"]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"hello\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: lines + take pipeline --- */
static int test_lines_take(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [take [lines \"a\\nb\\nc\\nd\\ne\"] 3]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"a\" \"b\" \"c\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: lines + first --- */
static int test_lines_first(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [first [lines \"hello\\nworld\"]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: lines + count --- */
static int test_lines_count(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [count [lines \"a\\nb\\nc\"]]]\n",
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
    { "lines_basic",              test_lines_basic },
    { "lines_crlf",               test_lines_crlf },
    { "lines_no_trailing_empty",  test_lines_no_trailing_empty },
    { "lines_for",                test_lines_for },
    { "lines_collect",            test_lines_collect },
    { "lines_filter",             test_lines_filter },
    { "lines_empty_string",       test_lines_empty_string },
    { "lines_single",             test_lines_single },
    { "lines_take",               test_lines_take },
    { "lines_first",              test_lines_first },
    { "lines_count",              test_lines_count },
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
