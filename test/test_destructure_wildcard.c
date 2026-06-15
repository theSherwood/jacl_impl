#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-003: Wildcard pattern in destructuring ===== */

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

/* --- Test: basic wildcard skipping --- */
static int test_wildcard_basic(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def [_ b _] [vec 1 2 3]\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "2\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: wildcard with keyword form --- */
static int test_wildcard_keyword(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[def [_ b _] [vec 10 20 30]]\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "20\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: multiple wildcards --- */
static int test_wildcard_multiple(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def [_ _ c] [vec 1 2 3]\n"
    "[print $c]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: wildcard at end --- */
static int test_wildcard_at_end(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def [a _] [vec 42 99]\n"
    "[print $a]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: $_ reference is undefined variable error --- */
static int test_wildcard_reference_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def [_ b] [vec 1 2]\n"
    "[print $_]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: wildcard in named destructuring is compile error --- */
static int test_wildcard_named_error(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "struct Point { i32 x, i32 y }\n"
    "def p [Point x 1 y 2]\n"
    "def {x, _} $p\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: wildcard with mut --- */
static int test_wildcard_mut(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "mut [_ b] [vec 10 20]\n"
    "[print $b]\n"
    "set b 99\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "20\n99\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: wildcard in global scope --- */
static int test_wildcard_global(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def [_ b _] [vec 100 200 300]\n"
    "[print $b]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "200\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: wildcard still validates vector length --- */
static int test_wildcard_length_mismatch(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def [_ b _] [vec 1 2]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: all wildcards --- */
static int test_wildcard_all(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def [_ _] [vec 1 2]\n"
    "[print \"ok\"]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "ok\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "wildcard_basic",            test_wildcard_basic },
    { "wildcard_keyword",          test_wildcard_keyword },
    { "wildcard_multiple",         test_wildcard_multiple },
    { "wildcard_at_end",           test_wildcard_at_end },
    { "wildcard_reference_error",  test_wildcard_reference_error },
    { "wildcard_named_error",      test_wildcard_named_error },
    { "wildcard_mut",              test_wildcard_mut },
    { "wildcard_global",           test_wildcard_global },
    { "wildcard_length_mismatch",  test_wildcard_length_mismatch },
    { "wildcard_all",              test_wildcard_all },
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
