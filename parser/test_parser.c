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

static AstNode* parse_expr(const char* source) {
  LexResult tokens = lexer_lex(source, &test_arena);
  Parser p;
  parser__init(&p, tokens, &test_arena);
  return parser__parse_expr(&p);
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

/* ---- US-004 tests ---- */

static int test_var_ref_single_char(void) {
  setup();
  AstNode* n = parse_atom("$x");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_VAR_REF);
  ASSERT_U32_EQ(n->data.var_ref.length, 1);
  ASSERT(memcmp(n->data.var_ref.name, "x", 1) == 0);
  /* Source position includes the $ prefix */
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 2);
  ASSERT_U32_EQ(n->end.column, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_ref_multichar(void) {
  setup();
  AstNode* n = parse_atom("$foo");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_VAR_REF);
  ASSERT_U32_EQ(n->data.var_ref.length, 3);
  ASSERT(memcmp(n->data.var_ref.name, "foo", 3) == 0);
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 4);
  ASSERT_U32_EQ(n->end.column, 5);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_ref_long_name(void) {
  setup();
  AstNode* n = parse_atom("$long_name");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_VAR_REF);
  ASSERT_U32_EQ(n->data.var_ref.length, 9);
  ASSERT(memcmp(n->data.var_ref.name, "long_name", 9) == 0);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 10);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-005 tests ---- */

static int test_cmd_basic(void) {
  setup();
  /* [cmd arg1 arg2] → AST_COMMAND head=string("cmd"), args=[string("arg1"), string("arg2")] */
  AstNode* n = parse_expr("[cmd arg1 arg2]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head != NULL);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "cmd", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "arg1", 4) == 0);
  ASSERT(n->data.command.args[1]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[1]->data.lit_string.value, "arg2", 4) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_operator_head(void) {
  setup();
  /* [+ 1 2] → AST_COMMAND head=string("+"), args=[int(1), int(2)] */
  AstNode* n = parse_expr("[+ 1 2]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "+", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.command.args[0]->data.lit_int.value, 1);
  ASSERT(n->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.command.args[1]->data.lit_int.value, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_nested(void) {
  setup();
  /* [print [+ 1 2]] → command whose second child is another command */
  AstNode* n = parse_expr("[print [+ 1 2]]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  AstNode* inner = n->data.command.args[0];
  ASSERT(inner->type == AST_COMMAND);
  ASSERT(inner->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(inner->data.command.head->data.lit_string.value, "+", 1) == 0);
  ASSERT_U32_EQ(inner->data.command.arg_count, 2);
  ASSERT(inner->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(inner->data.command.args[0]->data.lit_int.value, 1);
  ASSERT(inner->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(inner->data.command.args[1]->data.lit_int.value, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_deep_nesting(void) {
  setup();
  /* [a [b [c 1]]] → three-level tree */
  AstNode* n = parse_expr("[a [b [c 1]]]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "a", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  AstNode* mid = n->data.command.args[0];
  ASSERT(mid->type == AST_COMMAND);
  ASSERT(memcmp(mid->data.command.head->data.lit_string.value, "b", 1) == 0);
  ASSERT_U32_EQ(mid->data.command.arg_count, 1);
  AstNode* inner = mid->data.command.args[0];
  ASSERT(inner->type == AST_COMMAND);
  ASSERT(memcmp(inner->data.command.head->data.lit_string.value, "c", 1) == 0);
  ASSERT_U32_EQ(inner->data.command.arg_count, 1);
  ASSERT(inner->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(inner->data.command.args[0]->data.lit_int.value, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_var_arg(void) {
  setup();
  /* [print $x] → command with var_ref argument */
  AstNode* n = parse_expr("[print $x]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.command.args[0]->data.var_ref.name, "x", 1) == 0);
  ASSERT_U32_EQ(n->data.command.args[0]->data.var_ref.length, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_empty_brackets(void) {
  setup();
  /* [] → parse error */
  AstNode* n = parse_expr("[]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_ERROR);
  ASSERT(n->data.error.message != NULL);
  /* Source position spans the brackets */
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_mismatched_bracket(void) {
  setup();
  /* [cmd arg → parse error */
  AstNode* n = parse_expr("[cmd arg");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_ERROR);
  ASSERT(n->data.error.message != NULL);
  /* Error should start at the opening bracket */
  ASSERT_U32_EQ(n->start.offset, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_source_positions(void) {
  setup();
  /* [cmd arg1 arg2] → positions span from [ to ] */
  AstNode* n = parse_expr("[cmd arg1 arg2]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  /* Start at '[' (offset 0) */
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  /* End after ']' (offset 15) */
  ASSERT_U32_EQ(n->end.offset, 15);
  ASSERT_U32_EQ(n->end.column, 16);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_cmd_no_args(void) {
  setup();
  /* [foo] → command with head="foo", 0 args */
  AstNode* n = parse_expr("[foo]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "foo", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 0);
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->end.offset, 5);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-006 tests ---- */

static int test_bare_basic(void) {
  setup();
  /* "print hello world" → AST_COMMAND head="print", args=["hello","world"] */
  ParseResult r = parse("print hello world");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "hello", 5) == 0);
  ASSERT(n->data.command.args[1]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[1]->data.lit_string.value, "world", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_newline_delimited(void) {
  setup();
  /* "print a\nprint b" → two separate command nodes */
  ParseResult r = parse("print a\nprint b");
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  /* First command */
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[0]->data.command.arg_count, 1);
  ASSERT(memcmp(r.nodes[0]->data.command.args[0]->data.lit_string.value, "a", 1) == 0);
  /* Second command */
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 1);
  ASSERT(memcmp(r.nodes[1]->data.command.args[0]->data.lit_string.value, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_semicolon_delimited(void) {
  setup();
  /* "print a; print b" → two separate command nodes */
  ParseResult r = parse("print a; print b");
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  /* First command */
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[0]->data.command.arg_count, 1);
  ASSERT(memcmp(r.nodes[0]->data.command.args[0]->data.lit_string.value, "a", 1) == 0);
  /* Second command */
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 1);
  ASSERT(memcmp(r.nodes[1]->data.command.args[0]->data.lit_string.value, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_blank_lines(void) {
  setup();
  /* Blank lines (consecutive newlines) produce no empty commands */
  ParseResult r = parse("print a\n\n\nprint b");
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_nested_brackets(void) {
  setup();
  /* "print [+ 1 2]" → command with bracket sub-expression as arg */
  ParseResult r = parse("print [+ 1 2]");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  AstNode* inner = n->data.command.args[0];
  ASSERT(inner->type == AST_COMMAND);
  ASSERT(memcmp(inner->data.command.head->data.lit_string.value, "+", 1) == 0);
  ASSERT_U32_EQ(inner->data.command.arg_count, 2);
  ASSERT(inner->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(inner->data.command.args[0]->data.lit_int.value, 1);
  ASSERT(inner->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(inner->data.command.args[1]->data.lit_int.value, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_var_ref(void) {
  setup();
  /* "print $x" → command with var_ref argument */
  ParseResult r = parse("print $x");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.command.args[0]->data.var_ref.name, "x", 1) == 0);
  ASSERT_U32_EQ(n->data.command.args[0]->data.var_ref.length, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_single_word(void) {
  setup();
  /* "foo" → command with no arguments */
  ParseResult r = parse("foo");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "foo", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_source_positions(void) {
  setup();
  /* "print hello" → positions span from first to last token */
  ParseResult r = parse("print hello");
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  /* Start at "print" (offset 0, line 1, col 1) */
  ASSERT_U32_EQ(n->start.offset, 0);
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  /* End after "hello" (offset 11, line 1, col 12) */
  ASSERT_U32_EQ(n->end.offset, 11);
  ASSERT_U32_EQ(n->end.line, 1);
  ASSERT_U32_EQ(n->end.column, 12);
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
    /* US-004 */
    {"var_ref_single_char",  test_var_ref_single_char},
    {"var_ref_multichar",    test_var_ref_multichar},
    {"var_ref_long_name",    test_var_ref_long_name},
    /* US-005 */
    {"cmd_basic",            test_cmd_basic},
    {"cmd_operator_head",    test_cmd_operator_head},
    {"cmd_nested",           test_cmd_nested},
    {"cmd_deep_nesting",     test_cmd_deep_nesting},
    {"cmd_var_arg",          test_cmd_var_arg},
    {"cmd_empty_brackets",   test_cmd_empty_brackets},
    {"cmd_mismatched_bracket", test_cmd_mismatched_bracket},
    {"cmd_source_positions", test_cmd_source_positions},
    {"cmd_no_args",          test_cmd_no_args},
    /* US-006 */
    {"bare_basic",           test_bare_basic},
    {"bare_newline_delim",   test_bare_newline_delimited},
    {"bare_semicolon_delim", test_bare_semicolon_delimited},
    {"bare_blank_lines",     test_bare_blank_lines},
    {"bare_nested_brackets", test_bare_nested_brackets},
    {"bare_var_ref",         test_bare_var_ref},
    {"bare_single_word",     test_bare_single_word},
    {"bare_source_positions",test_bare_source_positions},
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
