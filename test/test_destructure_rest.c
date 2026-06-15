#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-004: Rest patterns in destructuring ===== */

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

/* --- Test: basic vector rest --- */
static int test_vec_rest_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [head ..rest] [vec 1 2 3 4]]\n"
    "[print $head]\n"
    "[print $rest]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n[vec 2 3 4]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: vector rest with two positional elements --- */
static int test_vec_rest_two_pos(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [a b ..rest] [vec 10 20 30 40 50]]\n"
    "[print $a]\n"
    "[print $b]\n"
    "[print $rest]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n[vec 30 40 50]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: vector rest with empty rest --- */
static int test_vec_rest_empty(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [a b ..rest] [vec 1 2]]\n"
    "[print $a]\n"
    "[print $b]\n"
    "[print $rest]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: vector rest with many elements --- */
static int test_vec_rest_many(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [h ..rest] [vec 1 2 3 4 5 6 7 8]]\n"
    "[print $h]\n"
    "[print [vec-len $rest]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n7\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: vector rest with mut --- */
static int test_vec_rest_mut(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[mut [head ..rest] [vec 10 20 30]]\n"
    "[print $head]\n"
    "[print $rest]\n"
    "set head 99\n"
    "[print $head]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n[vec 20 30]\n99\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: rest not last is parse error --- */
static int test_vec_rest_not_last(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [..rest a] [vec 1 2 3]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: duplicate rest is parse error --- */
static int test_vec_rest_duplicate(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [a ..r1 ..r2] [vec 1 2 3]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: rest without name is error --- */
static int test_vec_rest_unnamed(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [head ..] [vec 1 2 3]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: rest patterns are rejected on struct destructure --- */
static int test_named_rest_struct(void) {
  /* Building a rest map from a struct would require materializing the
     inline struct to a heap HeapRecord — an auto-allocation. The compiler
     rejects this and asks the user to list every field explicitly. */
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y, i32 z}\n"
    "proc main {} {\n"
    "  def p [Point x 10 y 20 z 30]\n"
    "  [def {x, ..rest} $p]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: named rest for map --- */
static int test_named_rest_map(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m [map \"x\" 100 \"y\" 200 \"z\" 300]\n"
    "[def {x, ..rest} $m]\n"
    "[print $x]\n"
    "[print [map-get $rest \"y\"]]\n"
    "[print [map-get $rest \"z\"]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "100\n200\n300\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: named rest with mut --- */
static int test_named_rest_mut(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m [map \"a\" 1 \"b\" 2 \"c\" 3]\n"
    "[mut {a, ..rest} $m]\n"
    "[print $a]\n"
    "set a 99\n"
    "[print $a]\n"
    "[print [map-get $rest \"b\"]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n99\n2\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: named rest excludes explicit fields from rest map --- */
static int test_named_rest_excludes(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m [map \"a\" 1 \"b\" 2 \"c\" 3]\n"
    "[def {a, b, ..rest} $m]\n"
    "[print $a]\n"
    "[print $b]\n"
    "[print [map-has $rest \"a\"]]\n"
    "[print [map-has $rest \"b\"]]\n"
    "[print [map-has $rest \"c\"]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\nfalse\nfalse\ntrue\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: vector rest in global scope --- */
static int test_vec_rest_global(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [head ..rest] [vec 5 6 7]]\n"
    "[print $head]\n"
    "[print $rest]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n[vec 6 7]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: named rest in global scope --- */
static int test_named_rest_global(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m [map \"x\" 10 \"y\" 20 \"z\" 30]\n"
    "[def {x, ..rest} $m]\n"
    "[print $x]\n"
    "[print [map-get $rest \"y\"]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "vec_rest_basic",           test_vec_rest_basic },
    { "vec_rest_two_pos",         test_vec_rest_two_pos },
    { "vec_rest_empty",           test_vec_rest_empty },
    { "vec_rest_many",            test_vec_rest_many },
    { "vec_rest_mut",             test_vec_rest_mut },
    { "vec_rest_not_last",        test_vec_rest_not_last },
    { "vec_rest_duplicate",       test_vec_rest_duplicate },
    { "vec_rest_unnamed",         test_vec_rest_unnamed },
    { "named_rest_struct",        test_named_rest_struct },
    { "named_rest_map",           test_named_rest_map },
    { "named_rest_mut",           test_named_rest_mut },
    { "named_rest_excludes",      test_named_rest_excludes },
    { "vec_rest_global",          test_vec_rest_global },
    { "named_rest_global",        test_named_rest_global },
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
