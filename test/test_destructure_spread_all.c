#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-005: Spread-all destructuring ({..}) ===== */

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

/* --- Test: basic spread-all imports all fields --- */
static int test_spread_all_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  p = [Point 10 20]\n"
    "  [def {..} $p]\n"
    "  [print $x]\n"
    "  [print $y]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: explicit-plus-spread imports explicit + remaining --- */
static int test_spread_all_explicit_plus(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Vec3 {i32 x, i32 y, i32 z}\n"
    "proc main {} {\n"
    "  v = [Vec3 1 2 3]\n"
    "  [def {x, ..} $v]\n"
    "  [print $x]\n"
    "  [print $y]\n"
    "  [print $z]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread-all with different struct type --- */
static int test_spread_all_different_struct(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Color {i32 r, i32 g, i32 b}\n"
    "proc main {} {\n"
    "  c = [Color 255 128 0]\n"
    "  [def {..} $c]\n"
    "  [print $r]\n"
    "  [print $g]\n"
    "  [print $b]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "255\n128\n0\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: explicit-plus-spread no duplicate for explicit field --- */
static int test_spread_all_no_dup(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  p = [Point 10 20]\n"
    "  [def {y, ..} $p]\n"
    "  [print $x]\n"
    "  [print $y]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread-all requires known struct type (dyn error) --- */
static int test_spread_all_dyn_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc test {dyn d} {\n"
    "  [def {..} $d]\n"
    "}\n"
    "[test [Point 10 20]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: shadowing error when spread-all conflicts with existing variable --- */
static int test_spread_all_shadow_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc test {} {\n"
    "  x = 99\n"
    "  p = [Point 10 20]\n"
    "  [def {..} $p]\n"
    "  [print $x]\n"
    "}\n"
    "[test]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: top-level struct def is rejected (force boxing) --- */
static int test_spread_all_global(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "p = [Point 42 99]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: top-level struct def is rejected (force boxing) --- */
static int test_spread_all_explicit_global(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Vec3 {i32 x, i32 y, i32 z}\n"
    "v = [Vec3 5 6 7]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread-all in nested scope (proc body) --- */
static int test_spread_all_nested(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc show {Point p} {\n"
    "  [def {..} $p]\n"
    "  [print $x]\n"
    "  [print $y]\n"
    "}\n"
    "[show [Point 3 4]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n4\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread-all with single-field struct --- */
static int test_spread_all_single_field(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Wrapper {i64 val}\n"
    "proc main {} {\n"
    "  w = [Wrapper 100]\n"
    "  [def {..} $w]\n"
    "  [print $val]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "100\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "spread_all_basic",           test_spread_all_basic },
    { "spread_all_explicit_plus",   test_spread_all_explicit_plus },
    { "spread_all_different_struct", test_spread_all_different_struct },
    { "spread_all_no_dup",          test_spread_all_no_dup },
    { "spread_all_dyn_error",       test_spread_all_dyn_error },
    { "spread_all_shadow_error",    test_spread_all_shadow_error },
    { "spread_all_global",          test_spread_all_global },
    { "spread_all_explicit_global", test_spread_all_explicit_global },
    { "spread_all_nested",          test_spread_all_nested },
    { "spread_all_single_field",    test_spread_all_single_field },
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
