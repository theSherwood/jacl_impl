#define ARENA_IMPLEMENTATION
#define LEXER_IMPLEMENTATION
#define AST_IMPLEMENTATION
#define PARSER_IMPLEMENTATION
#include "../test/test_helpers.h"
#include "parser.h"

/* ---- helpers ---- */

static arena_t test_arena;

static void setup(void) {
  tracker_reset();
  test_arena = (arena_t){ .allocator = tracked_allocator };
}

static void teardown(void) {
  arena_destroy(&test_arena);
}

static ParseResult parse(const char* source) {
  LexResult tokens = lexer_lex(source, &test_arena);
  return parser_parse(tokens, &test_arena);
}

static AstNode* parse_atom(const char* source) {
  LexResult tokens = lexer_lex(source, &test_arena);
  Parser p;
  parser__init(&p, tokens, &test_arena);
  return parser__parse_atom(&p);
}

/* ---- US-002 tests ---- */

static int test_empty_input(void) {
  setup();
  ParseResult r = parse("");
  ASSERT_U32_EQ(r.count, 0);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes != NULL);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_newline_only(void) {
  setup();
  ParseResult r = parse("\n\n\n");
  ASSERT_U32_EQ(r.count, 0);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_comment_only(void) {
  setup();
  ParseResult r = parse("# this is a comment\n# another comment\n");
  ASSERT_U32_EQ(r.count, 0);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_parse_result_fields(void) {
  setup();
  ParseResult r = parse("");
  /* Verify struct fields exist and are correctly typed */
  AstNode** nodes = r.nodes;
  uint32_t count = r.count;
  uint32_t errors = r.error_count;
  ASSERT(nodes != NULL);
  ASSERT_U32_EQ(count, 0);
  ASSERT_U32_EQ(errors, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-003 tests ---- */

static int test_lit_int_decimal(void) {
  setup();
  AstNode* n = parse_atom("42");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.lit_int.value, 42);
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 2);
  ASSERT_U32_EQ(n->end.column, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lit_int_hex(void) {
  setup();
  AstNode* n = parse_atom("0xFF");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.lit_int.value, 255);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 4);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lit_int_binary(void) {
  setup();
  AstNode* n = parse_atom("0b1010");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.lit_int.value, 10);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 6);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lit_float(void) {
  setup();
  AstNode* n = parse_atom("3.14");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_FLOAT);
  ASSERT(n->data.lit_float.value > 3.13f && n->data.lit_float.value < 3.15f);
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 4);
  ASSERT_U32_EQ(n->end.column, 5);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lit_word(void) {
  setup();
  AstNode* n = parse_atom("hello");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_STRING);
  ASSERT_U32_EQ(n->data.lit_string.length, 5);
  ASSERT(memcmp(n->data.lit_string.value, "hello", 5) == 0);
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 5);
  ASSERT_U32_EQ(n->end.column, 6);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lit_word_foo(void) {
  setup();
  AstNode* n = parse_atom("foo");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_STRING);
  ASSERT_U32_EQ(n->data.lit_string.length, 3);
  ASSERT(memcmp(n->data.lit_string.value, "foo", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lit_string_quoted(void) {
  setup();
  AstNode* n = parse_atom("\"hello\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_STRING);
  ASSERT_U32_EQ(n->data.lit_string.length, 5);
  ASSERT(memcmp(n->data.lit_string.value, "hello", 5) == 0);
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 7);
  ASSERT_U32_EQ(n->end.column, 8);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lit_string_with_escapes(void) {
  setup();
  AstNode* n = parse_atom("\"a\\nb\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_STRING);
  ASSERT_U32_EQ(n->data.lit_string.length, 3);
  ASSERT(n->data.lit_string.value[0] == 'a');
  ASSERT(n->data.lit_string.value[1] == '\n');
  ASSERT(n->data.lit_string.value[2] == 'b');
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- runner ---- */

typedef int (*test_fn)(void);

typedef struct {
  const char* name;
  test_fn     fn;
} TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-002 */
    {"empty_input",          test_empty_input},
    {"newline_only",         test_newline_only},
    {"comment_only",         test_comment_only},
    {"parse_result_fields",  test_parse_result_fields},
    /* US-003 */
    {"lit_int_decimal",      test_lit_int_decimal},
    {"lit_int_hex",          test_lit_int_hex},
    {"lit_int_binary",       test_lit_int_binary},
    {"lit_float",            test_lit_float},
    {"lit_word",             test_lit_word},
    {"lit_word_foo",         test_lit_word_foo},
    {"lit_string_quoted",    test_lit_string_quoted},
    {"lit_string_escapes",   test_lit_string_with_escapes},
  };
  int n = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0;
  int failed = 0;
  printf("\n");
  for (int i = 0; i < n; i++) {
    printf("  %-36s", tests[i].name);
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }
  printf("\n  %d/%d tests passed\n", passed, n);
  return failed > 0 ? 1 : 0;
}
