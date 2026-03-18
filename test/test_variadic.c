#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-007: Variadic proc parameters ===== */

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

/* --- Test: basic variadic proc using spread in body --- */
static int test_variadic_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc sum {..nums} {\n"
    "  [+ ..$nums]\n"
    "}\n"
    "[print [sum 1 2 3]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "6\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: variadic with fixed params --- */
static int test_variadic_mixed(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc log {level, ..msgs} {\n"
    "  [print $level]\n"
    "  [print $msgs]\n"
    "}\n"
    "[log \"INFO\" \"hello\" \"world\"]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "INFO\n[vec \"hello\" \"world\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: zero variadic args produces empty vector --- */
static int test_variadic_zero(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc log {level, ..msgs} {\n"
    "  [print $level]\n"
    "  [print $msgs]\n"
    "}\n"
    "[log \"INFO\"]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "INFO\n[vec]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: too few args is runtime error --- */
static int test_variadic_too_few(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc log {level, ..msgs} {\n"
    "  [print $level]\n"
    "}\n"
    "[log]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: rest param not last is compile error --- */
static int test_variadic_not_last(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc bad {..rest, x} { $rest }\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: multiple rest params is compile error --- */
static int test_variadic_multiple_rest(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc bad {..a, ..b} { $a }\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: splat into variadic proc --- */
static int test_variadic_splat(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc sum {..nums} {\n"
    "  [+ ..$nums]\n"
    "}\n"
    "args = [vec 10 20 30]\n"
    "[print [sum ..$args]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "60\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: variadic with only rest param, no fixed --- */
static int test_variadic_rest_only(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc all {..items} {\n"
    "  [print $items]\n"
    "}\n"
    "[all]\n"
    "[all 1]\n"
    "[all 1 2 3 4 5]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec]\n[vec 1]\n[vec 1 2 3 4 5]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: splat mixed into variadic --- */
static int test_variadic_splat_mixed(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc log {level, ..msgs} {\n"
    "  [print $level]\n"
    "  [print [+ ..$msgs]]\n"
    "}\n"
    "extras = [vec 20 30]\n"
    "[log \"WARN\" 10 ..$extras]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "WARN\n60\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: variadic proc called from another proc --- */
static int test_variadic_nested(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc sum {..nums} {\n"
    "  [+ ..$nums]\n"
    "}\n"
    "proc wrapper {a, b} {\n"
    "  [sum $a $b 100]\n"
    "}\n"
    "[print [wrapper 1 2]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "103\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "variadic_basic",         test_variadic_basic },
    { "variadic_mixed",         test_variadic_mixed },
    { "variadic_zero",          test_variadic_zero },
    { "variadic_too_few",       test_variadic_too_few },
    { "variadic_not_last",      test_variadic_not_last },
    { "variadic_multiple_rest", test_variadic_multiple_rest },
    { "variadic_splat",         test_variadic_splat },
    { "variadic_rest_only",     test_variadic_rest_only },
    { "variadic_splat_mixed",   test_variadic_splat_mixed },
    { "variadic_nested",        test_variadic_nested },
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
