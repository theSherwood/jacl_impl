#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-013: Stream-aware transform ===== */

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

/* --- Test: lazy transform — returns stream, not eagerly materialized --- */
static int test_transform_stream_lazy(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "def s [transform [gen] [\\ $* $it 10]]\n"
    "print \"<stream>\"\n"
    "def v [collect $s]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "<stream>\n[vec 10 20 30]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: transform + collect produces correct vector --- */
static int test_transform_collect(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 5]\n"
    "  [yield 10]\n"
    "  [yield 15]\n"
    "}\n"
    "def v [collect [transform [gen] [\\ $+ $it 1]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 6 11 16]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: chained filter + transform pipeline --- */
static int test_transform_chained_pipeline(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 10] {\n"
    "    [yield $i]\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def v [collect [transform [filter [gen] [\\ $> $it 5]] [\\ $* $it 2]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 12 14 16 18]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: transform on vector returns vector (unchanged eager behavior) --- */
static int test_transform_vector_unchanged(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def v [transform [vec 1 2 3] [\\ $* $it 3]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 3 6 9]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: pipe composition --- */
static int test_transform_pipe(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "[gen] | transform [\\ $+ $it 100] | collect | print\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 101 102 103]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: transform with for-each iteration --- */
static int test_transform_for_each(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 10]\n"
    "  [yield 20]\n"
    "  [yield 30]\n"
    "}\n"
    "def s [transform [gen] [\\ $+ $it 5]]\n"
    "for $s { print $it }\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n25\n35\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: transform on empty stream --- */
static int test_transform_empty_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  mut i 0\n"
    "  while [< $i 0] {\n"
    "    [yield $i]\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def v [collect [transform [gen] [\\ $* $it 2]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: transform + filter chained (transform first) --- */
static int test_transform_then_filter(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "  [yield 4]\n"
    "  [yield 5]\n"
    "}\n"
    "def v [collect [filter [transform [gen] [\\ $* $it 10]] [\\ $> $it 25]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 30 40 50]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: loop generator with transform --- */
static int test_transform_loop_generator(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    set i [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def v [collect [transform [gen 5] [\\ $* $it $it]]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 1 4 9 16]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int main(void) {
  printf("test_stream_transform:\n");

  typedef struct { const char* name; int (*fn)(void); } Test;
  Test tests[] = {
    { "transform_stream_lazy",      test_transform_stream_lazy },
    { "transform_collect",          test_transform_collect },
    { "transform_chained_pipeline", test_transform_chained_pipeline },
    { "transform_vector_unchanged", test_transform_vector_unchanged },
    { "transform_pipe",             test_transform_pipe },
    { "transform_for_each",         test_transform_for_each },
    { "transform_empty_stream",     test_transform_empty_stream },
    { "transform_then_filter",      test_transform_then_filter },
    { "transform_loop_generator",   test_transform_loop_generator },
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
