#include "test_helpers.h"
#include "../src/jacl.h"

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

/* ===== US-007: End-to-end M6 line-based syntax integration tests ===== */

/* Test: bare command with args — 'puts hello' compiles and runs like '[puts hello]' */
static int test_bare_cmd_with_args(void) {
  TEST_SETUP();

  const char* program = "print hello";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  TEST_TEARDOWN();
}

/* Test: bare command no args — 'exit' produces AST_COMMAND with arg_count 0 */
static int test_bare_cmd_no_args_ast(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  LexResult tokens = lexer_lex("exit", &arena);
  ParseResult r = parser_parse(tokens, &arena);

  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT_STR_EQ(n->data.command.head->data.lit_string.value, "exit");
  ASSERT_U32_EQ(n->data.command.arg_count, 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: $var command — 'proc f {} { 42 }\ng = $f\n$g' evaluates to 42 */
static int test_var_cmd(void) {
  TEST_SETUP();

  const char* program =
    "proc f {} { 42 }\n"
    "g = $f\n"
    "print [$g]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  TEST_TEARDOWN();
}

/* Test: semicolon separator — 'def x 1; def y 2; [+ $x $y]' evaluates to 3 */
static int test_semicolon_sep(void) {
  TEST_SETUP();

  const char* program = "def x 1; def y 2; print [+ $x $y]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  TEST_TEARDOWN();
}

/* Test: comma separator — 'def x 10, def y 20, [+ $x $y]' evaluates to 30 */
static int test_comma_sep(void) {
  TEST_SETUP();

  const char* program = "def x 10, def y 20, print [+ $x $y]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  TEST_TEARDOWN();
}

/* Test: mixed separators — 'def a 1; def b 2, def c 3\n[+ [+ $a $b] $c]' evaluates to 6 */
static int test_mixed_sep(void) {
  TEST_SETUP();

  const char* program =
    "def a 1; def b 2, def c 3\n"
    "print [+ [+ $a $b] $c]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "6\n");

  TEST_TEARDOWN();
}

/* Test: backslash continuation — 'def x \\\n42\n$x' evaluates to 42 */
static int test_backslash_continuation(void) {
  TEST_SETUP();

  const char* program =
    "def x \\\n42\n"
    "print [+ $x 0]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  TEST_TEARDOWN();
}

/* Test: continuation in block — '{ def x 1, def y 2, + $x $y }' evaluates to 3 */
static int test_block_with_commas(void) {
  TEST_SETUP();

  const char* program =
    "print [if $true { def x 1, def y 2, + $x $y } { 0 }]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  TEST_TEARDOWN();
}

/* Test: nested brackets in line mode — 'puts [+ 1 [* 2 3]]' works correctly */
static int test_nested_brackets_in_line(void) {
  TEST_SETUP();

  const char* program = "print [+ 1 [* 2 3]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "7\n");

  TEST_TEARDOWN();
}

/* Test: explicit brackets still work — 'x = 42\n[+ $x 1]' evaluates to 43 */
static int test_explicit_brackets(void) {
  TEST_SETUP();

  const char* program =
    "x = 42\n"
    "[print [+ $x 1]]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "43\n");

  TEST_TEARDOWN();
}

/* Test: proc definition line-based — 'proc add {a, b} { + $a $b }\nadd 3 4' evaluates to 7 */
static int test_proc_line_based(void) {
  TEST_SETUP();

  const char* program =
    "proc add {a, b} { + $a $b }\n"
    "print [add 3 4]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "7\n");

  TEST_TEARDOWN();
}

/* Test: multi-line block — proc with multi-line body */
static int test_multiline_block(void) {
  TEST_SETUP();

  const char* program =
    "proc f {} {\n"
    "  x = 10\n"
    "  y = 20\n"
    "  + $x $y\n"
    "}\n"
    "print [f]";
  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  TEST_TEARDOWN();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-007: End-to-end M6 line-based syntax integration */
    { "bare_cmd_with_args",         test_bare_cmd_with_args },
    { "bare_cmd_no_args_ast",       test_bare_cmd_no_args_ast },
    { "var_cmd",                    test_var_cmd },
    { "semicolon_sep",              test_semicolon_sep },
    { "comma_sep",                  test_comma_sep },
    { "mixed_sep",                  test_mixed_sep },
    { "backslash_continuation",     test_backslash_continuation },
    { "block_with_commas",          test_block_with_commas },
    { "nested_brackets_in_line",    test_nested_brackets_in_line },
    { "explicit_brackets",          test_explicit_brackets },
    { "proc_line_based",            test_proc_line_based },
    { "multiline_block",            test_multiline_block },
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
