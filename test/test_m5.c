#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== Print capture helper ===== */

typedef struct {
  char     buf[1024];
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

/* ===== Helper macro for standard test setup/teardown ===== */

#define TEST_SETUP()                                      \
  tracker_reset();                                        \
  arena_t arena = { .allocator = tracked_allocator };     \
  PrintCapture cap = { .len = 0 };                        \
  VM vm;                                                  \
  vm_init(&vm, &arena);                                   \
  vm.print_fn = capture_print;                            \
  vm.print_ctx = &cap

#define TEST_TEARDOWN()            \
  vm_destroy(&vm);                 \
  arena_destroy(&arena);           \
  ASSERT(check_no_leaks());        \
  TEST_PASS()

/* ===== US-010: End-to-end M5 string integration tests ===== */

/* Test: [print "hello world"] prints heap string literal */
static int test_print_heap_string(void) {
  TEST_SETUP();

  const char* program = "[print \"hello world\"]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  TEST_TEARDOWN();
}

/* Test: [def s "hello"] [print [concat $s " world"]] prints "hello world" */
static int test_concat_var(void) {
  TEST_SETUP();

  const char* program =
    "s = \"hello\"\n"
    "[print [concat $s \" world\"]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  TEST_TEARDOWN();
}

/* Test: [print [length "hello"]] prints 5 */
static int test_length_builtin(void) {
  TEST_SETUP();

  const char* program = "[print [length \"hello\"]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  TEST_TEARDOWN();
}

/* Test: [print [index "hello" 0]] prints "h" */
static int test_index_builtin(void) {
  TEST_SETUP();

  const char* program = "[print [index \"hello\" 0]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "h\n");

  TEST_TEARDOWN();
}

/* Test: [print [slice "hello world" 6 11]] prints "world" */
static int test_slice_builtin(void) {
  TEST_SETUP();

  const char* program = "[print [slice \"hello world\" 6 11]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "world\n");

  TEST_TEARDOWN();
}

/* Test: [def name "JACL"] [print "hello $name"] prints "hello JACL" */
static int test_interpolation_var(void) {
  TEST_SETUP();

  const char* program =
    "name = \"JACL\"\n"
    "[print \"hello $name\"]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello JACL\n");

  TEST_TEARDOWN();
}

/* Test: [print "1 + 2 = $[+ 1 2]"] prints "1 + 2 = 3" */
static int test_interpolation_expr(void) {
  TEST_SETUP();

  const char* program = "[print \"1 + 2 = $[+ 1 2]\"]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1 + 2 = 3\n");

  TEST_TEARDOWN();
}

/* Test: [== "hello world" "hello world"] returns true via interned pointer equality */
static int test_heap_string_equality(void) {
  TEST_SETUP();

  const char* program =
    "[print [if [== \"hello world\" \"hello world\"] { \"yes\" } { \"no\" }]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "yes\n");

  TEST_TEARDOWN();
}

/* Test: [< "abc" "xyz"] returns true */
static int test_string_ordering(void) {
  TEST_SETUP();

  const char* program =
    "[print [if [< \"abc\" \"xyz\"] { \"yes\" } { \"no\" }]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "yes\n");

  TEST_TEARDOWN();
}

/* Test: heap string as proc argument and return value */
static int test_heap_string_proc_arg_return(void) {
  TEST_SETUP();

  const char* program =
    "proc greet {name} {\n"
    "  [concat \"hello \" $name]\n"
    "}\n"
    "[print [greet \"world\"]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\n");

  TEST_TEARDOWN();
}

/* Test: closure captures a heap string value */
static int test_closure_captures_heap_string(void) {
  TEST_SETUP();

  const char* program =
    "proc mkgreet {prefix} {\n"
    "  proc inner {name} {\n"
    "    [concat $prefix $name]\n"
    "  }\n"
    "}\n"
    "hi = [mkgreet \"hello \"]\n"
    "[print [hi \"world\"]]\n"
    "[print [hi \"JACL\"]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world\nhello JACL\n");

  TEST_TEARDOWN();
}

/* Test: variadic concat end-to-end */
static int test_variadic_concat(void) {
  TEST_SETUP();

  const char* program = "[print [concat \"a\" \"b\" \"c\" \"d\"]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "abcd\n");

  TEST_TEARDOWN();
}

/* Test: slice one-arg form (from start to end) */
static int test_slice_one_arg(void) {
  TEST_SETUP();

  const char* program = "[print [slice \"hello world\" 6]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "world\n");

  TEST_TEARDOWN();
}

/* Test: string interpolation with mixed var + expr */
static int test_interpolation_mixed(void) {
  TEST_SETUP();

  const char* program =
    "x = 10\n"
    "[print \"$x times 2 is $[* $x 2]\"]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10 times 2 is 20\n");

  TEST_TEARDOWN();
}

/* Test: string comparison with == across types returns false */
static int test_string_vs_int_eq(void) {
  TEST_SETUP();

  const char* program =
    "[print [if [== \"42\" 42] { \"same\" } { \"diff\" }]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "diff\n");

  TEST_TEARDOWN();
}

/* Test: length on heap string */
static int test_length_heap_string(void) {
  TEST_SETUP();

  const char* program = "[print [length \"hello world this is long\"]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "24\n");

  TEST_TEARDOWN();
}

/* Test: index out-of-bounds returns nil */
static int test_index_oob_nil(void) {
  TEST_SETUP();

  const char* program =
    "[print [if [== [index \"hi\" 5] $nil] { \"nil\" } { \"nope\" }]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  TEST_TEARDOWN();
}

/* Test: string used in while loop condition */
static int test_string_while_loop(void) {
  TEST_SETUP();

  const char* program =
    "s = \"hello\"\n"
    "i : 0\n"
    "r : \"\"\n"
    "[while [< $i [length $s]] {\n"
    "  r :: [concat $r [index $s $i]]\n"
    "  i :: [+ $i 1]\n"
    "}]\n"
    "[print $r]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  TEST_TEARDOWN();
}

/* Test: to_string coercion via interpolation */
static int test_to_string_coercion(void) {
  TEST_SETUP();

  const char* program =
    "[print \"bool=$true num=$[+ 1 2] nil=$nil\"]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "bool=true num=3 nil=nil\n");

  TEST_TEARDOWN();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-010: End-to-end M5 string integration */
    { "print_heap_string",            test_print_heap_string },
    { "concat_var",                   test_concat_var },
    { "length_builtin",              test_length_builtin },
    { "index_builtin",               test_index_builtin },
    { "slice_builtin",               test_slice_builtin },
    { "interpolation_var",           test_interpolation_var },
    { "interpolation_expr",          test_interpolation_expr },
    { "heap_string_equality",        test_heap_string_equality },
    { "string_ordering",             test_string_ordering },
    { "heap_string_proc_arg_return", test_heap_string_proc_arg_return },
    { "closure_captures_heap_str",   test_closure_captures_heap_string },
    { "variadic_concat",             test_variadic_concat },
    { "slice_one_arg",               test_slice_one_arg },
    { "interpolation_mixed",         test_interpolation_mixed },
    { "string_vs_int_eq",            test_string_vs_int_eq },
    { "length_heap_string",          test_length_heap_string },
    { "index_oob_nil",               test_index_oob_nil },
    { "string_while_loop",           test_string_while_loop },
    { "to_string_coercion",          test_to_string_coercion },
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
