#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-012: Stream-aware filter ===== */

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

/* --- Test: lazy filter — only consumed elements pulled from source --- */
static int test_filter_stream_lazy(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "  yield 4\n"
    "  yield 5\n"
    "}\n"
    "def s [filter [gen] [\\ > $it 3]]\n"
    "print \"<stream>\"\n"
    "def v [collect $s]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  /* Filter should return a stream (lazy), not eagerly materialize */
  ASSERT_STR_EQ(cap.buf, "<stream>\n[vec 4 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: filter + collect produces correct vector --- */
static int test_filter_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 10\n"
    "  yield 20\n"
    "  yield 30\n"
    "  yield 40\n"
    "}\n"
    "def v [collect [filter [gen] [\\ > $it 15]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 20 30 40]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: chained filters --- */
static int test_filter_chained(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 10] {\n"
    "    yield $i\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def s1 [filter [gen] [\\ > $it 3]]\n"
    "def s2 [filter $s1 [\\ < $it 8]]\n"
    "def v [collect $s2]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 4 5 6 7]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: filter on vector returns vector (unchanged eager behavior) --- */
static int test_filter_vector_unchanged(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def v [filter [vec 1 2 3 4 5] [\\ > $it 2]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 3 4 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: no matches produces empty vector --- */
static int test_filter_no_matches(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "}\n"
    "def v [collect [filter [gen] [\\ > $it 100]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: pipe composition: $stream | filter predicate --- */
static int test_filter_pipe(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 5\n"
    "  yield 10\n"
    "  yield 15\n"
    "  yield 20\n"
    "}\n"
    "[gen] | filter [\\ > $it 7] | collect | print\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 10 15 20]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-filtered-stream via OP_EACH (HOF callback) --- */
static int test_filter_for_each(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "  yield 4\n"
    "}\n"
    "for [filter [gen] [\\ > $it 2]] [\\ print $it]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n4\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: for-over-filtered-stream with inline body --- */
static int test_filter_for_inline(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  yield 10\n"
    "  yield 20\n"
    "  yield 30\n"
    "}\n"
    "for [filter [gen] [\\ > $it 10]] {\n"
    "  print $it\n"
    "}\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "20\n30\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: filter stream with loop-based generator --- */
static int test_filter_loop_generator(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    yield $i\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "proc even {x} { == [% $x 2] 0 }\n"
    "def v [collect [filter [gen 8] $even]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 2 4 6]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: empty stream filter produces empty stream --- */
static int test_filter_empty_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 0] {\n"
    "    yield $i\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def v [collect [filter [gen] [\\ > $it 0]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */
typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "filter_stream_lazy",     test_filter_stream_lazy },
    { "filter_collect",         test_filter_collect },
    { "filter_chained",         test_filter_chained },
    { "filter_vector_unchanged",test_filter_vector_unchanged },
    { "filter_no_matches",      test_filter_no_matches },
    { "filter_pipe",            test_filter_pipe },
    { "filter_for_each",        test_filter_for_each },
    { "filter_for_inline",      test_filter_for_inline },
    { "filter_loop_generator",  test_filter_loop_generator },
    { "filter_empty_stream",    test_filter_empty_stream },
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
