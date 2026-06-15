#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-011: Stream-aware for loop ===== */

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

/* --- Test: for-over-stream with implicit $it binding --- */
static int test_for_stream_implicit_it(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 10\n"
    "  yield 20\n"
    "  yield 30\n"
    "}\n"
    "for [gen] {\n"
    "  print $it\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-stream with explicit binding --- */
static int test_for_stream_explicit_binding(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "}\n"
    "for [gen] x {\n"
    "  print $x\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: break mid-stream --- */
static int test_for_stream_break(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "  yield 4\n"
    "}\n"
    "for [gen] {\n"
    "  if [> $it 2] {\n"
    "    break\n"
    "  }\n"
    "  print $it\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: continue skips to next pull --- */
static int test_for_stream_continue(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "  yield 4\n"
    "}\n"
    "for [gen] {\n"
    "  if [== $it 2] {\n"
    "    continue\n"
    "  }\n"
    "  print $it\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n3\n4\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: return from enclosing proc stops stream --- */
static int test_for_stream_return(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "}\n"
    "proc process {} {\n"
    "  for [gen] {\n"
    "    if [== $it 2] {\n"
    "      return $it\n"
    "    }\n"
    "  }\n"
    "}\n"
    "[print [process]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "2\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-stream with loop-based generator --- */
static int test_for_stream_loop_generator(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    yield $i\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "for [gen 5] {\n"
    "  print $it\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n2\n3\n4\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-vector unchanged --- */
static int test_for_vector_unchanged(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "for [vec 10 20 30] {\n"
    "  print $it\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-stream lambda/HOF callback form --- */
static int test_for_stream_lambda(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 5\n"
    "  yield 6\n"
    "  yield 7\n"
    "}\n"
    "for [gen] [\\ print $it]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n6\n7\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-stream HOF callback form --- */
static int test_for_stream_hof_callback(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 100\n"
    "  yield 200\n"
    "}\n"
    "proc p {x} { print $x }\n"
    "def s [gen]\n"
    "for $s $p\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "100\n200\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: empty stream yields no iterations --- */
static int test_for_stream_empty(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    yield $i\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "for [gen 0] {\n"
    "  print $it\n"
    "}\n"
    "[print \"done\"]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "done\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-stream with string values --- */
static int test_for_stream_strings(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc words {} {\n"
    "  yield \"hello\"\n"
    "  yield \"world\"\n"
    "}\n"
    "for [words] {\n"
    "  print $it\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\nworld\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */
typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "for_stream_implicit_it",     test_for_stream_implicit_it },
    { "for_stream_explicit_binding", test_for_stream_explicit_binding },
    { "for_stream_break",           test_for_stream_break },
    { "for_stream_continue",        test_for_stream_continue },
    { "for_stream_return",          test_for_stream_return },
    { "for_stream_loop_generator",  test_for_stream_loop_generator },
    { "for_vector_unchanged",       test_for_vector_unchanged },
    { "for_stream_lambda",          test_for_stream_lambda },
    { "for_stream_hof_callback",    test_for_stream_hof_callback },
    { "for_stream_empty",           test_for_stream_empty },
    { "for_stream_strings",         test_for_stream_strings },
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
