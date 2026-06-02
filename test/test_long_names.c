#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-003: Long variable name support (8-128 bytes) ===== */

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

static int run_ok(const char* source, PrintCapture* cap, const char* expected) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  if (cap) { cap->len = 0; cap->buf[0] = '\0'; }
  VM vm;
  vm_init(&vm, &arena);
  if (cap) {
    vm.print_fn = capture_print;
    vm.print_ctx = cap;
  }
  VMResult result = jacl_run(source, &vm, &arena);
  if (result != VM_OK) {
    fprintf(stderr, "  Expected VM_OK but got error: %s\n",
            vm.error_message ? vm.error_message : "(null)");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (expected && cap) {
    if (strcmp(cap->buf, expected) != 0) {
      fprintf(stderr, "  Output mismatch:\n  Actual:   '%s'\n  Expected: '%s'\n",
              cap->buf, expected);
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 0;
    }
  }
  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

static int run_err(const char* source, const char* err_substr) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(source, &vm, &arena);
  if (result == VM_OK) {
    fprintf(stderr, "  Expected error but got VM_OK\n");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (err_substr && vm.error_message) {
    if (!strstr(vm.error_message, err_substr)) {
      fprintf(stderr, "  Error msg '%s' does not contain '%s'\n",
              vm.error_message, err_substr);
      vm_destroy(&vm);
      arena_destroy(&arena);
      return 0;
    }
  }
  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

/* --- Test: def with long variable name (8-30 bytes) --- */
static int test_def_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "my_counter = 42\n"
    "[print $my_counter]",
    &cap, "42\n"));
  TEST_PASS();
}

/* --- Test: mut with long variable name --- */
static int test_mut_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut accumulated_value 10\n"
    "  set accumulated_value [+ $accumulated_value 5]\n"
    "  print $accumulated_value\n"
    "}\n"
    "[t]",
    &cap, "15\n"));
  TEST_PASS();
}

/* --- Test: set! with long variable name --- */
static int test_set_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  mut current_total 0\n"
    "  set current_total 99\n"
    "  print $current_total\n"
    "}\n"
    "[t]",
    &cap, "99\n"));
  TEST_PASS();
}

/* --- Test: proc with long name --- */
static int test_proc_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc calculate_sum {a b} {\n"
    "  [+ $a $b]\n"
    "}\n"
    "[print [calculate_sum 3 7]]",
    &cap, "10\n"));
  TEST_PASS();
}

/* --- Test: proc params with long names --- */
static int test_proc_params_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc add {first_value second_value} {\n"
    "  [+ $first_value $second_value]\n"
    "}\n"
    "[print [add 100 200]]",
    &cap, "300\n"));
  TEST_PASS();
}

/* --- Test: vec destructure with long names --- */
static int test_vec_destructure_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  def [first_item second_item] [vec 10 20]\n"
    "  print $first_item\n"
    "  print $second_item\n"
    "}\n"
    "[t]",
    &cap, "10\n20\n"));
  TEST_PASS();
}

/* --- Test: named destructure with long names --- */
static int test_named_destructure_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "struct PersonInfo {i64 user_id, i32 user_age}\n"
    "proc t {} {\n"
    "  def {user_id, user_age} [PersonInfo 12345 30]\n"
    "  print $user_id\n"
    "  print $user_age\n"
    "}\n"
    "[t]",
    &cap, "12345\n30\n"));
  TEST_PASS();
}

/* --- Test: for binding with long name --- */
static int test_for_binding_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  for current_element [vec 1 2 3] {\n"
    "    print $current_element\n"
    "  }\n"
    "}\n"
    "[t]",
    &cap, "1\n2\n3\n"));
  TEST_PASS();
}

/* --- Test: try binding with long name --- */
static int test_try_binding_long_name(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc t {} {\n"
    "  try {\n"
    "    [error \"oops\"]\n"
    "  } caught_error {\n"
    "    print [error-val $caught_error]\n"
    "  }\n"
    "}\n"
    "[t]",
    &cap, "oops\n"));
  TEST_PASS();
}

/* --- Test: 128-byte hard cap rejects with expected error --- */
static int test_128_byte_cap_rejects(void) {
  /* 129-byte name should be rejected */
  ASSERT(run_err(
    "def aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh"
    "iiiiiiiijjjjjjjjkkkkkkkkllllllllmmmmmmmmnnnnnnnnooooooooppppppppq 42",
    "128-byte"));
  TEST_PASS();
}

/* --- Test: two def sites at different scopes with same long name --- */
static int test_same_long_name_different_scopes(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc outer {} {\n"
    "  def result_value 100\n"
    "  proc inner {} {\n"
    "    def result_value 200\n"
    "    print $result_value\n"
    "  }\n"
    "  [inner]\n"
    "  print $result_value\n"
    "}\n"
    "[outer]",
    &cap, "200\n100\n"));
  TEST_PASS();
}

/* --- Test: interned string pointer stability (same long name resolves correctly) --- */
static int test_interned_pointer_stability(void) {
  PrintCapture cap;
  /* Two procs both define and use the same long name independently */
  ASSERT(run_ok(
    "proc first_fn {} {\n"
    "  def shared_long_identifier 111\n"
    "  $shared_long_identifier\n"
    "}\n"
    "proc second_fn {} {\n"
    "  def shared_long_identifier 222\n"
    "  $shared_long_identifier\n"
    "}\n"
    "[print [first_fn]]\n"
    "[print [second_fn]]",
    &cap, "111\n222\n"));
  TEST_PASS();
}

/* --- Test: names exactly at boundaries (7 bytes, 8 bytes) --- */
static int test_name_boundary_7_bytes(void) {
  PrintCapture cap;
  /* 7 bytes: should use inline string */
  ASSERT(run_ok(
    "abcdefg = 7\n"
    "[print $abcdefg]",
    &cap, "7\n"));
  TEST_PASS();
}

static int test_name_boundary_8_bytes(void) {
  PrintCapture cap;
  /* 8 bytes: should use interned string */
  ASSERT(run_ok(
    "abcdefgh = 8\n"
    "[print $abcdefgh]",
    &cap, "8\n"));
  TEST_PASS();
}

/* --- Test: name exactly 128 bytes (should work) --- */
static int test_name_exactly_128_bytes(void) {
  PrintCapture cap;
  /* 128-byte name: should be accepted */
  ASSERT(run_ok(
    "aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhhiiiiiiiijjjjjjjjkkkkkkkkllllllllmmmmmmmmnnnnnnnnoooooooopppppppp = 128\n"
    "[print $aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhhiiiiiiiijjjjjjjjkkkkkkkkllllllllmmmmmmmmnnnnnnnnoooooooopppppppp]",
    &cap, "128\n"));
  TEST_PASS();
}

/* --- Test: variadic proc param with long name --- */
static int test_variadic_long_param(void) {
  PrintCapture cap;
  ASSERT(run_ok(
    "proc my_func {first_param .. remaining_args} {\n"
    "  print $first_param\n"
    "  print [count $remaining_args]\n"
    "}\n"
    "[my_func 1 2 3]",
    &cap, "1\n2\n"));
  TEST_PASS();
}

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "def_long_name",                test_def_long_name },
    { "mut_long_name",                test_mut_long_name },
    { "set_long_name",                test_set_long_name },
    { "proc_long_name",              test_proc_long_name },
    { "proc_params_long_name",       test_proc_params_long_name },
    { "vec_destructure_long_name",   test_vec_destructure_long_name },
    { "named_destructure_long_name", test_named_destructure_long_name },
    { "for_binding_long_name",       test_for_binding_long_name },
    { "try_binding_long_name",       test_try_binding_long_name },
    { "128_byte_cap_rejects",        test_128_byte_cap_rejects },
    { "same_long_name_diff_scopes",  test_same_long_name_different_scopes },
    { "interned_pointer_stability",  test_interned_pointer_stability },
    { "name_boundary_7_bytes",       test_name_boundary_7_bytes },
    { "name_boundary_8_bytes",       test_name_boundary_8_bytes },
    { "name_exactly_128_bytes",      test_name_exactly_128_bytes },
    { "variadic_long_param",         test_variadic_long_param },
  };

  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0, failed = 0;

  printf("=== US-003: Long variable name support ===\n");
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
