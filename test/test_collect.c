#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-010: collect — stream to vector materialization ===== */

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

/* --- Test: collect on a yielded stream produces correct vector --- */
static int test_collect_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "def v [collect [gen]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 2 3]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect on a vector is identity --- */
static int test_collect_vector_identity(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def v [vec 10 20 30]\n"
    "def w [collect $v]\n"
    "[print $w]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 10 20 30]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect preserves element order --- */
static int test_collect_order(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 3]\n"
    "  [yield 1]\n"
    "  [yield 4]\n"
    "  [yield 1]\n"
    "  [yield 5]\n"
    "}\n"
    "def v [collect [gen]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 3 1 4 1 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: empty stream returns empty vector --- */
static int test_collect_empty_stream(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc range {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def v [collect [range 0]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect on non-collection errors --- */
static int test_collect_non_collection_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[collect 42]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect on string errors --- */
static int test_collect_string_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[collect \"hello\"]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect with loop-based generator (many elements) --- */
static int test_collect_loop_generator(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc range {n} {\n"
    "  mut i 0\n"
    "  while [< $i $n] {\n"
    "    [yield $i]\n"
    "    i :: [+ $i 1]\n"
    "  }\n"
    "}\n"
    "def v [collect [range 6]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 0 1 2 3 4 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect with string-yielding generator --- */
static int test_collect_strings(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc words {} {\n"
    "  [yield \"hello\"]\n"
    "  [yield \"world\"]\n"
    "}\n"
    "def v [collect [words]]\n"
    "[print $v]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"hello\" \"world\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect then vec-get to verify proper vector --- */
static int test_collect_then_index(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 10]\n"
    "  [yield 20]\n"
    "  [yield 30]\n"
    "}\n"
    "def v [collect [gen]]\n"
    "[print [vec-get $v 0]]\n"
    "[print [vec-get $v 1]]\n"
    "[print [vec-get $v 2]]\n"
    "[print [vec-len $v]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Main --- */
typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "collect_basic",                test_collect_basic },
    { "collect_vector_identity",      test_collect_vector_identity },
    { "collect_order",                test_collect_order },
    { "collect_empty_stream",         test_collect_empty_stream },
    { "collect_non_collection_error", test_collect_non_collection_error },
    { "collect_string_error",         test_collect_string_error },
    { "collect_loop_generator",       test_collect_loop_generator },
    { "collect_strings",              test_collect_strings },
    { "collect_then_index",           test_collect_then_index },
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
