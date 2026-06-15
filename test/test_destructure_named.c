#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-002: Named struct/map destructuring ===== */

/* Print capture helper */
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

/* --- Test: struct destructuring with keyword form (basic) --- */
static int test_struct_destructure_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def p [Point x 10 y 20]\n"
    "  [def {x, y} $p]\n"
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

/* --- Test: struct destructuring with keyword form --- */
static int test_struct_destructure_keyword(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def p [Point x 10 y 20]\n"
    "  [def {x, y} $p]\n"
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

/* --- Test: map destructuring --- */
static int test_map_destructure_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m [map \"x\" 100 \"y\" 200]\n"
    "[def {x, y} $m]\n"
    "[print $x]\n"
    "[print $y]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "100\n200\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: field order independence --- */
static int test_field_order_independence(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def p [Point x 10 y 20]\n"
    "  [def {y, x} $p]\n"
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

/* --- Test: mut destructuring --- */
static int test_mut_destructure(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def p [Point x 10 y 20]\n"
    "  [mut {x, y} $p]\n"
    "  [print $x]\n"
    "  [print $y]\n"
    "  set x 99\n"
    "  [print $x]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n99\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: typed field destructuring --- */
static int test_typed_destructure(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def p [Point x 5 y 6]\n"
    "  [def {i32 x, i32 y} $p]\n"
    "  [print $x]\n"
    "  [print $y]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n6\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: struct destructuring in nested scope --- */
static int test_nested_scope(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc show {Point, p} {\n"
    "  [def {x, y} $p]\n"
    "  [print $x]\n"
    "  [print $y]\n"
    "}\n"
    "proc main {} {\n"
    "  def p [Point x 1 y 2]\n"
    "  show $p\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: compile-time error for missing struct field --- */
static int test_missing_struct_field_compile_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "proc main {} {\n"
    "  def p [Point x 1 y 2]\n"
    "  [def {x, z} $p]\n"
    "}\n"
    "main\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: runtime error for missing map key --- */
static int test_missing_map_key_runtime_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m [map \"x\" 100]\n"
    "[def {x, y} $m]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: runtime error for non-struct/map value --- */
static int test_non_struct_map_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m 42\n"
    "[def {x, y} $m]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: top-level struct def is rejected (force boxing) --- */
static int test_global_scope_destructure(void) {
  /* Top-level struct values are no longer allowed — must wrap in a proc
     or use [box]. This test confirms the rejection. */
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point {i32 x, i32 y}\n"
    "def p [Point x 100 y 200]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: map destructuring with keyword form --- */
static int test_map_destructure_keyword(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def m [map \"a\" 1 \"b\" 2]\n"
    "[def {a, b} $m]\n"
    "[print $a]\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "struct_destructure_basic",      test_struct_destructure_basic },
    { "struct_destructure_keyword",    test_struct_destructure_keyword },
    { "map_destructure_basic",         test_map_destructure_basic },
    { "field_order_independence",      test_field_order_independence },
    { "mut_destructure",               test_mut_destructure },
    { "typed_destructure",             test_typed_destructure },
    { "nested_scope",                  test_nested_scope },
    { "missing_struct_field_error",    test_missing_struct_field_compile_error },
    { "missing_map_key_error",         test_missing_map_key_runtime_error },
    { "non_struct_map_error",          test_non_struct_map_error },
    { "global_scope_destructure",      test_global_scope_destructure },
    { "map_destructure_keyword",       test_map_destructure_keyword },
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
