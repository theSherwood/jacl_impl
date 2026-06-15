#include "test_helpers.h"
#include "../src/jacl.h"

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
  /* [] → empty command (used for proc param lists) */
  AstNode* n = parse_expr("[]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head != NULL);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT_U32_EQ(n->data.command.head->data.lit_string.length, 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 0);
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
  /* "foo" → bare word at statement position wraps to zero-arg AST_COMMAND */
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

static int test_bare_comma_delimited(void) {
  setup();
  /* "def x 1, def y 2" → two separate command nodes */
  ParseResult r = parse("def x 1, def y 2");
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  /* First command: def x 1 */
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[0]->data.command.arg_count, 2);
  ASSERT(memcmp(r.nodes[0]->data.command.args[0]->data.lit_string.value, "x", 1) == 0);
  ASSERT(r.nodes[0]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(r.nodes[0]->data.command.args[1]->data.lit_int.value, 1);
  /* Second command: def y 2 */
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 2);
  ASSERT(memcmp(r.nodes[1]->data.command.args[0]->data.lit_string.value, "y", 1) == 0);
  ASSERT(r.nodes[1]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(r.nodes[1]->data.command.args[1]->data.lit_int.value, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_comma_delimited(void) {
  setup();
  /* { def a 10, def b 20 } → block with two commands */
  AstNode* n = parse_expr("{ def a 10, def b 20 }");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.block.count, 2);
  ASSERT(n->data.block.commands[0]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.block.commands[0]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(n->data.block.commands[0]->data.command.arg_count, 2);
  ASSERT(memcmp(n->data.block.commands[0]->data.command.args[0]->data.lit_string.value, "a", 1) == 0);
  ASSERT(n->data.block.commands[0]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.block.commands[0]->data.command.args[1]->data.lit_int.value, 10);
  ASSERT(n->data.block.commands[1]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.block.commands[1]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(n->data.block.commands[1]->data.command.arg_count, 2);
  ASSERT(memcmp(n->data.block.commands[1]->data.command.args[0]->data.lit_string.value, "b", 1) == 0);
  ASSERT(n->data.block.commands[1]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.block.commands[1]->data.command.args[1]->data.lit_int.value, 20);
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

/* ---- M6 US-004 tests: Bare words as zero-arg command calls ---- */

static int test_bare_word_zero_arg(void) {
  setup();
  /* 'exit' alone at statement position → AST_COMMAND { head: "exit", arg_count: 0 } */
  ParseResult r = parse("exit");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "exit", 4) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_word_multi_arg_unchanged(void) {
  setup();
  /* 'puts hello' with args → AST_COMMAND with arg_count 1 (unchanged) */
  ParseResult r = parse("puts hello");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "puts", 4) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "hello", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_word_inside_brackets(void) {
  setup();
  /* In '[puts exit]', 'exit' is parsed as AST_LIT_STRING arg — NOT wrapped */
  AstNode* n = parse_expr("[puts exit]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "exit", 4) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_int_not_wrapped(void) {
  setup();
  /* 42 alone → remains AST_LIT_INT, not wrapped */
  ParseResult r = parse("42");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(r.nodes[0]->data.lit_int.value, 42);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_float_not_wrapped(void) {
  setup();
  /* 3.14 alone → remains AST_LIT_FLOAT, not wrapped */
  ParseResult r = parse("3.14");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes[0]->type == AST_LIT_FLOAT);
  ASSERT(r.nodes[0]->data.lit_float.value > 3.13f && r.nodes[0]->data.lit_float.value < 3.15f);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_quoted_string_not_wrapped(void) {
  setup();
  /* "hello" alone → remains AST_LIT_STRING, not wrapped */
  ParseResult r = parse("\"hello\"");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(r.nodes[0]->data.lit_string.value, "hello", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_bracket_cmd_not_double_wrapped(void) {
  setup();
  /* [foo] alone → returned directly as AST_COMMAND, not double-wrapped */
  ParseResult r = parse("[foo]");
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

static int test_bare_block_not_wrapped(void) {
  setup();
  /* { print hello } alone → returned directly as AST_BLOCK, not wrapped */
  ParseResult r = parse("{ print hello }");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes[0]->type == AST_BLOCK);
  ASSERT_U32_EQ(r.nodes[0]->data.block.count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- M6 US-005 tests: $var as value expression ---- */

static int test_var_ref_zero_arg(void) {
  setup();
  /* '$fn' alone at statement position → AST_VAR_REF (value read, not a call) */
  ParseResult r = parse("$fn");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.var_ref.name, "fn", 2) == 0);
  ASSERT_U32_EQ(n->data.var_ref.length, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_ref_with_args(void) {
  setup();
  /* '$fn 1 2' → AST_COMMAND { head: AST_VAR_REF("fn"), args: [1, 2] } */
  ParseResult r = parse("$fn 1 2");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.command.head->data.var_ref.name, "fn", 2) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.command.args[0]->data.lit_int.value, 1);
  ASSERT(n->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(n->data.command.args[1]->data.lit_int.value, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_ref_inside_brackets_unchanged(void) {
  setup();
  /* In '[puts $x]', $x is parsed as AST_VAR_REF — NOT wrapped */
  AstNode* n = parse_expr("[puts $x]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.command.args[0]->data.var_ref.name, "x", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-007 tests ---- */

static int test_block_single_cmd(void) {
  setup();
  /* { print hello } → AST_BLOCK containing one command */
  AstNode* n = parse_expr("{ print hello }");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.block.count, 1);
  AstNode* cmd = n->data.block.commands[0];
  ASSERT(cmd->type == AST_COMMAND);
  ASSERT(memcmp(cmd->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(cmd->data.command.arg_count, 1);
  ASSERT(memcmp(cmd->data.command.args[0]->data.lit_string.value, "hello", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_multiline(void) {
  setup();
  /* Multi-line block: newline-delimited commands */
  AstNode* n = parse_expr("{ print a\nprint b }");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.block.count, 2);
  ASSERT(n->data.block.commands[0]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.block.commands[0]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(memcmp(n->data.block.commands[0]->data.command.args[0]->data.lit_string.value, "a", 1) == 0);
  ASSERT(n->data.block.commands[1]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.block.commands[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(memcmp(n->data.block.commands[1]->data.command.args[0]->data.lit_string.value, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_semicolon(void) {
  setup();
  /* Semicolons within blocks: { print a; print b } → two commands */
  AstNode* n = parse_expr("{ print a; print b }");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.block.count, 2);
  ASSERT(n->data.block.commands[0]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.block.commands[0]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(memcmp(n->data.block.commands[0]->data.command.args[0]->data.lit_string.value, "a", 1) == 0);
  ASSERT(n->data.block.commands[1]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.block.commands[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(memcmp(n->data.block.commands[1]->data.command.args[0]->data.lit_string.value, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_as_cmd_arg(void) {
  setup();
  /* [if $cond { print yes } { print no }] → command with two block args */
  AstNode* n = parse_expr("[if $cond { print yes } { print no }]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 3); /* $cond, block1, block2 */
  /* First arg: $cond */
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.command.args[0]->data.var_ref.name, "cond", 4) == 0);
  /* Second arg: { print yes } */
  AstNode* b1 = n->data.command.args[1];
  ASSERT(b1->type == AST_BLOCK);
  ASSERT_U32_EQ(b1->data.block.count, 1);
  ASSERT(b1->data.block.commands[0]->type == AST_COMMAND);
  ASSERT(memcmp(b1->data.block.commands[0]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(memcmp(b1->data.block.commands[0]->data.command.args[0]->data.lit_string.value, "yes", 3) == 0);
  /* Third arg: { print no } */
  AstNode* b2 = n->data.command.args[2];
  ASSERT(b2->type == AST_BLOCK);
  ASSERT_U32_EQ(b2->data.block.count, 1);
  ASSERT(b2->data.block.commands[0]->type == AST_COMMAND);
  ASSERT(memcmp(b2->data.block.commands[0]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(memcmp(b2->data.block.commands[0]->data.command.args[0]->data.lit_string.value, "no", 2) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_nested(void) {
  setup();
  /* { if $x { inner } } → nested blocks */
  AstNode* n = parse_expr("{ if $x { inner } }");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.block.count, 1);
  AstNode* cmd = n->data.block.commands[0];
  ASSERT(cmd->type == AST_COMMAND);
  ASSERT(memcmp(cmd->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(cmd->data.command.arg_count, 2); /* $x, {inner} */
  ASSERT(cmd->data.command.args[0]->type == AST_VAR_REF);
  AstNode* inner_block = cmd->data.command.args[1];
  ASSERT(inner_block->type == AST_BLOCK);
  ASSERT_U32_EQ(inner_block->data.block.count, 1);
  /* Bare word "inner" at statement position wraps to zero-arg AST_COMMAND */
  ASSERT(inner_block->data.block.commands[0]->type == AST_COMMAND);
  ASSERT(inner_block->data.block.commands[0]->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(inner_block->data.block.commands[0]->data.command.head->data.lit_string.value, "inner", 5) == 0);
  ASSERT_U32_EQ(inner_block->data.block.commands[0]->data.command.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_empty(void) {
  setup();
  /* {} → AST_BLOCK with zero commands */
  AstNode* n = parse_expr("{}");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.block.count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_mismatched(void) {
  setup();
  /* { cmd → parse error */
  AstNode* n = parse_expr("{ cmd");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_ERROR);
  ASSERT(n->data.error.message != NULL);
  /* Error should start at the opening brace */
  ASSERT_U32_EQ(n->start.offset, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_source_positions(void) {
  setup();
  /* { print hello } → positions span from { to } */
  AstNode* n = parse_expr("{ print hello }");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_BLOCK);
  /* Start at '{' (offset 0, line 1, col 1) */
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  /* End after '}' (offset 15, line 1, col 16) */
  ASSERT_U32_EQ(n->end.offset, 15);
  ASSERT_U32_EQ(n->end.column, 16);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_block_bare_cmd_arg(void) {
  setup();
  /* Top-level bare command with block args: if $x { yes } */
  ParseResult r = parse("if $x { yes }");
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2); /* $x, {yes} */
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  AstNode* blk = n->data.command.args[1];
  ASSERT(blk->type == AST_BLOCK);
  ASSERT_U32_EQ(blk->data.block.count, 1);
  ASSERT(blk->data.block.commands[0]->type == AST_COMMAND);
  ASSERT(blk->data.block.commands[0]->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(blk->data.block.commands[0]->data.command.head->data.lit_string.value, "yes", 3) == 0);
  ASSERT_U32_EQ(blk->data.block.commands[0]->data.command.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-008 tests ---- */

static int test_interp_non_interpolated(void) {
  setup();
  /* Non-interpolated "hello" produces plain AST_LIT_STRING (not interp string) */
  AstNode* n = parse_expr("\"hello\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_LIT_STRING);
  ASSERT_U32_EQ(n->data.lit_string.length, 5);
  ASSERT(memcmp(n->data.lit_string.value, "hello", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_var(void) {
  setup();
  /* "hello $name" → AST_INTERP_STRING [string("hello "), var_ref("name")] */
  AstNode* n = parse_expr("\"hello $name\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_INTERP_STRING);
  ASSERT_U32_EQ(n->data.interp_string.count, 2);
  /* Segment 0: string("hello ") */
  AstNode* s0 = n->data.interp_string.segments[0];
  ASSERT(s0->type == AST_LIT_STRING);
  ASSERT_U32_EQ(s0->data.lit_string.length, 6);
  ASSERT(memcmp(s0->data.lit_string.value, "hello ", 6) == 0);
  /* Segment 1: var_ref("name") */
  AstNode* s1 = n->data.interp_string.segments[1];
  ASSERT(s1->type == AST_VAR_REF);
  ASSERT_U32_EQ(s1->data.var_ref.length, 4);
  ASSERT(memcmp(s1->data.var_ref.name, "name", 4) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_expr(void) {
  setup();
  /* "result: $[+ 1 2]" → AST_INTERP_STRING [string("result: "), command(+ 1 2)] */
  AstNode* n = parse_expr("\"result: $[+ 1 2]\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_INTERP_STRING);
  ASSERT_U32_EQ(n->data.interp_string.count, 2);
  /* Segment 0: string("result: ") */
  AstNode* s0 = n->data.interp_string.segments[0];
  ASSERT(s0->type == AST_LIT_STRING);
  ASSERT_U32_EQ(s0->data.lit_string.length, 8);
  ASSERT(memcmp(s0->data.lit_string.value, "result: ", 8) == 0);
  /* Segment 1: command(+ 1 2) */
  AstNode* s1 = n->data.interp_string.segments[1];
  ASSERT(s1->type == AST_COMMAND);
  ASSERT(s1->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(s1->data.command.head->data.lit_string.value, "+", 1) == 0);
  ASSERT_U32_EQ(s1->data.command.arg_count, 2);
  ASSERT(s1->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(s1->data.command.args[0]->data.lit_int.value, 1);
  ASSERT(s1->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(s1->data.command.args[1]->data.lit_int.value, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_multiple(void) {
  setup();
  /* "$a and $b" → [var_ref("a"), string(" and "), var_ref("b")] */
  AstNode* n = parse_expr("\"$a and $b\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_INTERP_STRING);
  ASSERT_U32_EQ(n->data.interp_string.count, 3);
  /* var_ref("a") */
  ASSERT(n->data.interp_string.segments[0]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.interp_string.segments[0]->data.var_ref.name, "a", 1) == 0);
  ASSERT_U32_EQ(n->data.interp_string.segments[0]->data.var_ref.length, 1);
  /* string(" and ") */
  ASSERT(n->data.interp_string.segments[1]->type == AST_LIT_STRING);
  ASSERT_U32_EQ(n->data.interp_string.segments[1]->data.lit_string.length, 5);
  ASSERT(memcmp(n->data.interp_string.segments[1]->data.lit_string.value, " and ", 5) == 0);
  /* var_ref("b") */
  ASSERT(n->data.interp_string.segments[2]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.interp_string.segments[2]->data.var_ref.name, "b", 1) == 0);
  ASSERT_U32_EQ(n->data.interp_string.segments[2]->data.var_ref.length, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_adjacent(void) {
  setup();
  /* "$a$b" → [var_ref("a"), var_ref("b")] — empty segments skipped */
  AstNode* n = parse_expr("\"$a$b\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_INTERP_STRING);
  ASSERT_U32_EQ(n->data.interp_string.count, 2);
  ASSERT(n->data.interp_string.segments[0]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.interp_string.segments[0]->data.var_ref.name, "a", 1) == 0);
  ASSERT(n->data.interp_string.segments[1]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.interp_string.segments[1]->data.var_ref.name, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_source_positions(void) {
  setup();
  /* "hello $name" → positions cover full string including quotes */
  AstNode* n = parse_expr("\"hello $name\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_INTERP_STRING);
  /* Start at opening " (offset 0, line 1, col 1) */
  ASSERT_U32_EQ(n->start.line, 1);
  ASSERT_U32_EQ(n->start.column, 1);
  ASSERT_U32_EQ(n->start.offset, 0);
  /* End after closing " (offset 13, line 1, col 14) */
  ASSERT_U32_EQ(n->end.offset, 13);
  ASSERT_U32_EQ(n->end.column, 14);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-009 tests ---- */

static int test_multi_basic(void) {
  setup();
  /* Multi-line program produces correct array of top-level command nodes */
  ParseResult r = parse(
    "def x 42\n"
    "def y 10\n"
    "print $x\n"
  );
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_U32_EQ(r.error_count, 0);
  /* cmd 0: def x 42 */
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[0]->data.command.arg_count, 2);
  ASSERT(r.nodes[0]->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(r.nodes[0]->data.command.args[0]->data.lit_string.value, "x", 1) == 0);
  ASSERT(r.nodes[0]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(r.nodes[0]->data.command.args[1]->data.lit_int.value, 42);
  /* cmd 1: def y 10 */
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 2);
  ASSERT(r.nodes[1]->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(r.nodes[1]->data.command.args[0]->data.lit_string.value, "y", 1) == 0);
  ASSERT(r.nodes[1]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(r.nodes[1]->data.command.args[1]->data.lit_int.value, 10);
  /* cmd 2: print $x */
  ASSERT(r.nodes[2]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[2]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[2]->data.command.arg_count, 1);
  ASSERT(r.nodes[2]->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(r.nodes[2]->data.command.args[0]->data.var_ref.name, "x", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multi_mixed_syntax(void) {
  setup();
  /* Mixed syntax: bare commands, bracketed commands, and block arguments */
  ParseResult r = parse(
    "def x [+ 1 2]\n"
    "[print $x]\n"
    "if $x { print yes }\n"
  );
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_U32_EQ(r.error_count, 0);
  /* cmd 0: def x [+ 1 2] — bare command with bracketed sub-expression */
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[0]->data.command.arg_count, 2);
  ASSERT(r.nodes[0]->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(r.nodes[0]->data.command.args[0]->data.lit_string.value, "x", 1) == 0);
  AstNode* add = r.nodes[0]->data.command.args[1];
  ASSERT(add->type == AST_COMMAND);
  ASSERT(memcmp(add->data.command.head->data.lit_string.value, "+", 1) == 0);
  ASSERT_U32_EQ(add->data.command.arg_count, 2);
  /* cmd 1: [print $x] — top-level bracketed command */
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 1);
  ASSERT(r.nodes[1]->data.command.args[0]->type == AST_VAR_REF);
  /* cmd 2: if $x { print yes } — bare command with block arg */
  ASSERT(r.nodes[2]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[2]->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(r.nodes[2]->data.command.arg_count, 2);
  ASSERT(r.nodes[2]->data.command.args[0]->type == AST_VAR_REF);
  AstNode* blk = r.nodes[2]->data.command.args[1];
  ASSERT(blk->type == AST_BLOCK);
  ASSERT_U32_EQ(blk->data.block.count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multi_realistic_program(void) {
  setup();
  /* Realistic 10+ line program mixing def, proc, if, arithmetic, interp strings */
  ParseResult r = parse(
    "# Variable definitions\n"
    "def x 42\n"
    "def name \"world\"\n"
    "\n"
    "# Arithmetic\n"
    "def result [+ $x 10]\n"
    "\n"
    "# Procedure definition\n"
    "proc greet {} {\n"
    "  print \"hello $name\"\n"
    "}\n"
    "\n"
    "# Conditional\n"
    "if [> $x 0] {\n"
    "  print \"positive\"\n"
    "} else {\n"
    "  print \"non-positive\"\n"
    "}\n"
    "\n"
    "# Call the procedure\n"
    "greet\n"
  );
  ASSERT_U32_EQ(r.error_count, 0);
  /* Expected top-level: def x, def name, def result, proc greet, if, greet */
  ASSERT_U32_EQ(r.count, 6);

  /* 0: def x 42 */
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[0]->data.command.arg_count, 2);
  ASSERT(r.nodes[0]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(r.nodes[0]->data.command.args[1]->data.lit_int.value, 42);

  /* 1: def name "world" */
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 2);
  ASSERT(r.nodes[1]->data.command.args[1]->type == AST_LIT_STRING);
  ASSERT(memcmp(r.nodes[1]->data.command.args[1]->data.lit_string.value, "world", 5) == 0);

  /* 2: def result [+ $x 10] */
  ASSERT(r.nodes[2]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[2]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(r.nodes[2]->data.command.arg_count, 2);
  AstNode* arith = r.nodes[2]->data.command.args[1];
  ASSERT(arith->type == AST_COMMAND);
  ASSERT(memcmp(arith->data.command.head->data.lit_string.value, "+", 1) == 0);
  ASSERT_U32_EQ(arith->data.command.arg_count, 2);
  ASSERT(arith->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(arith->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(arith->data.command.args[1]->data.lit_int.value, 10);

  /* 3: proc greet {} { print "hello $name" } */
  ASSERT(r.nodes[3]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[3]->data.command.head->data.lit_string.value, "proc", 4) == 0);
  ASSERT_U32_EQ(r.nodes[3]->data.command.arg_count, 3);
  ASSERT(r.nodes[3]->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(r.nodes[3]->data.command.args[0]->data.lit_string.value, "greet", 5) == 0);
  AstNode* proc_body = r.nodes[3]->data.command.args[2];
  ASSERT(proc_body->type == AST_BLOCK);
  ASSERT_U32_EQ(proc_body->data.block.count, 1);
  /* The print command inside the block */
  AstNode* print_cmd = proc_body->data.block.commands[0];
  ASSERT(print_cmd->type == AST_COMMAND);
  ASSERT(memcmp(print_cmd->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(print_cmd->data.command.arg_count, 1);
  ASSERT(print_cmd->data.command.args[0]->type == AST_INTERP_STRING);

  /* 4: if [> $x 0] { print "positive" } else { print "non-positive" } */
  ASSERT(r.nodes[4]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[4]->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(r.nodes[4]->data.command.arg_count, 3);
  /* condition: [> $x 0] */
  AstNode* cond = r.nodes[4]->data.command.args[0];
  ASSERT(cond->type == AST_COMMAND);
  ASSERT(memcmp(cond->data.command.head->data.lit_string.value, ">", 1) == 0);
  ASSERT_U32_EQ(cond->data.command.arg_count, 2);
  /* then block */
  AstNode* then_blk = r.nodes[4]->data.command.args[1];
  ASSERT(then_blk->type == AST_BLOCK);
  ASSERT_U32_EQ(then_blk->data.block.count, 1);
  /* else block */
  AstNode* else_blk = r.nodes[4]->data.command.args[2];
  ASSERT(else_blk->type == AST_BLOCK);
  ASSERT_U32_EQ(else_blk->data.block.count, 1);

  /* 5: greet (bare word, no args → zero-arg AST_COMMAND) */
  ASSERT(r.nodes[5]->type == AST_COMMAND);
  ASSERT(r.nodes[5]->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(r.nodes[5]->data.command.head->data.lit_string.value, "greet", 5) == 0);
  ASSERT_U32_EQ(r.nodes[5]->data.command.arg_count, 0);

  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multi_source_positions(void) {
  setup();
  /* Source positions correct across many lines */
  ParseResult r = parse(
    "print a\n"
    "print b\n"
    "print c\n"
  );
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_U32_EQ(r.error_count, 0);
  /* cmd 0: line 1 */
  ASSERT_U32_EQ(r.nodes[0]->start.line, 1);
  ASSERT_U32_EQ(r.nodes[0]->start.column, 1);
  ASSERT_U32_EQ(r.nodes[0]->start.offset, 0);
  /* cmd 1: line 2 */
  ASSERT_U32_EQ(r.nodes[1]->start.line, 2);
  ASSERT_U32_EQ(r.nodes[1]->start.column, 1);
  ASSERT_U32_EQ(r.nodes[1]->start.offset, 8);
  /* cmd 2: line 3 */
  ASSERT_U32_EQ(r.nodes[2]->start.line, 3);
  ASSERT_U32_EQ(r.nodes[2]->start.column, 1);
  ASSERT_U32_EQ(r.nodes[2]->start.offset, 16);
  /* Verify end positions too */
  ASSERT_U32_EQ(r.nodes[0]->end.line, 1);
  ASSERT_U32_EQ(r.nodes[0]->end.offset, 7);  /* "print a" ends at offset 7 */
  ASSERT_U32_EQ(r.nodes[1]->end.line, 2);
  ASSERT_U32_EQ(r.nodes[1]->end.offset, 15); /* "print b" ends at offset 15 */
  ASSERT_U32_EQ(r.nodes[2]->end.line, 3);
  ASSERT_U32_EQ(r.nodes[2]->end.offset, 23); /* "print c" ends at offset 23 */
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multi_comments_between(void) {
  setup();
  /* Comments interspersed with code are skipped */
  ParseResult r = parse(
    "# header comment\n"
    "def x 1\n"
    "# middle comment\n"
    "def y 2\n"
    "# trailing comment\n"
  );
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "def", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multi_semicolons_and_newlines(void) {
  setup();
  /* Mixed semicolons and newlines as delimiters */
  ParseResult r = parse("def a 1; def b 2\ndef c 3");
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(r.nodes[2]->type == AST_COMMAND);
  ASSERT(r.nodes[0]->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(r.nodes[0]->data.command.args[1]->data.lit_int.value, 1);
  ASSERT_INT_EQ(r.nodes[1]->data.command.args[1]->data.lit_int.value, 2);
  ASSERT_INT_EQ(r.nodes[2]->data.command.args[1]->data.lit_int.value, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-010 tests ---- */

static int test_error_missing_bracket(void) {
  setup();
  /* Missing ] → error, recovery continues to next command */
  ParseResult r = parse("[print hello\nprint bye");
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT(r.nodes[0]->type == AST_ERROR);
  ASSERT(r.nodes[0]->data.error.message != NULL);
  /* Valid command after error */
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 1);
  ASSERT(memcmp(r.nodes[1]->data.command.args[0]->data.lit_string.value, "bye", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_missing_brace(void) {
  setup();
  /* Missing } → error */
  ParseResult r = parse("{ print hello");
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_U32_EQ(r.count, 1);
  ASSERT(r.nodes[0]->type == AST_ERROR);
  ASSERT(r.nodes[0]->data.error.message != NULL);
  ASSERT_U32_EQ(r.nodes[0]->start.offset, 0); /* starts at { */
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_unexpected_token(void) {
  setup();
  /* ] at top level → error, continue to next command */
  ParseResult r = parse("]\nprint bye");
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT(r.nodes[0]->type == AST_ERROR);
  ASSERT(r.nodes[0]->data.error.message != NULL);
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(r.nodes[1]->data.command.arg_count, 1);
  ASSERT(memcmp(r.nodes[1]->data.command.args[0]->data.lit_string.value, "bye", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_multiple(void) {
  setup();
  /* Multiple errors: two unclosed brackets with valid code between */
  ParseResult r = parse("[unclosed\nprint ok\n[also unclosed");
  ASSERT_U32_EQ(r.error_count, 2);
  ASSERT_U32_EQ(r.count, 3);
  ASSERT(r.nodes[0]->type == AST_ERROR);
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(r.nodes[2]->type == AST_ERROR);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_panic_bracket_sync(void) {
  setup();
  /* Error inside brackets: panic mode syncs to ] and continues */
  ParseResult r = parse("[cmd )]\nprint ok");
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT(r.nodes[0]->type == AST_ERROR);
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[1]->data.command.head->data.lit_string.value, "print", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_interleaved(void) {
  setup();
  /* Errors interleaved with valid code */
  ParseResult r = parse("print a\n[bad\nprint b\n]\nprint c");
  ASSERT_U32_EQ(r.error_count, 2);
  ASSERT_U32_EQ(r.count, 5);
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[0]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(r.nodes[1]->type == AST_ERROR);
  ASSERT(r.nodes[2]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[2]->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT(r.nodes[3]->type == AST_ERROR);
  ASSERT(r.nodes[4]->type == AST_COMMAND);
  ASSERT(memcmp(r.nodes[4]->data.command.head->data.lit_string.value, "print", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_positions(void) {
  setup();
  /* Error nodes have correct source positions */
  ParseResult r = parse("[unclosed arg");
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* err = r.nodes[0];
  ASSERT(err->type == AST_ERROR);
  /* Start at [ (offset 0, line 1, col 1) */
  ASSERT_U32_EQ(err->start.line, 1);
  ASSERT_U32_EQ(err->start.column, 1);
  ASSERT_U32_EQ(err->start.offset, 0);
  /* End should be past the last parsed token */
  ASSERT(err->end.offset > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_valid_after(void) {
  setup();
  /* Valid code after multiple errors is correctly parsed */
  ParseResult r = parse("]\n}\nprint hello\n[+ 1 2]\nif $x { yes }");
  ASSERT_U32_EQ(r.error_count, 2);
  ASSERT_U32_EQ(r.count, 5);
  /* First two: errors for ] and } */
  ASSERT(r.nodes[0]->type == AST_ERROR);
  ASSERT(r.nodes[1]->type == AST_ERROR);
  /* Next three: valid commands */
  ASSERT(r.nodes[2]->type == AST_COMMAND);
  ASSERT(r.nodes[3]->type == AST_COMMAND);
  ASSERT(r.nodes[4]->type == AST_COMMAND);
  /* Verify last command is fully parsed */
  ASSERT(memcmp(r.nodes[4]->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(r.nodes[4]->data.command.arg_count, 2);
  ASSERT(r.nodes[4]->data.command.args[1]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-011 tests ---- */

static int roundtrip_ok(const char* input) {
  /* Parse → pretty-print → re-parse → pretty-print → compare strings */
  LexResult t1 = lexer_lex(input, &test_arena);
  ParseResult r1 = parser_parse(t1, &test_arena);
  if (r1.error_count > 0) return 0;
  const char* pp1 = ast_pretty_print_program(r1.nodes, r1.count, &test_arena);
  LexResult t2 = lexer_lex(pp1, &test_arena);
  ParseResult r2 = parser_parse(t2, &test_arena);
  if (r2.error_count > 0) return 0;
  if (r2.count != r1.count) return 0;
  const char* pp2 = ast_pretty_print_program(r2.nodes, r2.count, &test_arena);
  return strcmp(pp1, pp2) == 0;
}

static int test_pp_command_format(void) {
  setup();
  /* Commands print in explicit bracket form */
  AstNode* n = parse_expr("[cmd arg1 arg2]");
  const char* s = ast_pretty_print(n, &test_arena);
  ASSERT(strcmp(s, "[cmd arg1 arg2]") == 0);
  /* No-arg command */
  AstNode* n2 = parse_expr("[foo]");
  const char* s2 = ast_pretty_print(n2, &test_arena);
  ASSERT(strcmp(s2, "[foo]") == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_pp_literals_format(void) {
  setup();
  /* Integer */
  AstNode* n1 = parse_atom("42");
  ASSERT(strcmp(ast_pretty_print(n1, &test_arena), "42") == 0);
  /* Float */
  AstNode* n2 = parse_atom("3.14");
  ASSERT(strcmp(ast_pretty_print(n2, &test_arena), "3.14") == 0);
  /* Bare word */
  AstNode* n3 = parse_atom("hello");
  ASSERT(strcmp(ast_pretty_print(n3, &test_arena), "hello") == 0);
  /* Quoted string with spaces */
  AstNode* n4 = parse_atom("\"hello world\"");
  ASSERT(strcmp(ast_pretty_print(n4, &test_arena), "\"hello world\"") == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_pp_var_ref_format(void) {
  setup();
  AstNode* n = parse_atom("$name");
  ASSERT(strcmp(ast_pretty_print(n, &test_arena), "$name") == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_pp_block_format(void) {
  setup();
  /* Block with two commands: { [cmd1]; [cmd2] } */
  AstNode* n = parse_expr("{ print a; print b }");
  const char* s = ast_pretty_print(n, &test_arena);
  ASSERT(strcmp(s, "{ [print a]; [print b] }") == 0);
  /* Empty block */
  AstNode* n2 = parse_expr("{}");
  ASSERT(strcmp(ast_pretty_print(n2, &test_arena), "{}") == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_pp_interp_format(void) {
  setup();
  /* Interpolated string with var and expr */
  AstNode* n = parse_expr("\"hello $name, result: $[+ 1 2]\"");
  const char* s = ast_pretty_print(n, &test_arena);
  ASSERT(strcmp(s, "\"hello $name, result: $[+ 1 2]\"") == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_pp_program_format(void) {
  setup();
  /* Multi-node program separated by newlines */
  ParseResult r = parse("def x 42\nprint $x");
  ASSERT_U32_EQ(r.count, 2);
  const char* s = ast_pretty_print_program(r.nodes, r.count, &test_arena);
  ASSERT(strcmp(s, "[def x 42]\n[print $x]") == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_literals(void) {
  setup();
  ASSERT(roundtrip_ok("def x 42"));
  ASSERT(roundtrip_ok("def pi 3.14"));
  ASSERT(roundtrip_ok("print hello"));
  ASSERT(roundtrip_ok("print \"hello world\""));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_commands(void) {
  setup();
  ASSERT(roundtrip_ok("[+ 1 2]"));
  ASSERT(roundtrip_ok("[print [+ 1 2]]"));
  ASSERT(roundtrip_ok("[a [b [c 1]]]"));
  ASSERT(roundtrip_ok("print [+ [* 2 3] 4]"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_blocks(void) {
  setup();
  ASSERT(roundtrip_ok("if $cond { print yes }"));
  ASSERT(roundtrip_ok("if $x { print yes; print no }"));
  ASSERT(roundtrip_ok("proc greet {} { print hello }"));
  ASSERT(roundtrip_ok("if $x { if $y { inner } }"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_interp(void) {
  setup();
  ASSERT(roundtrip_ok("print \"hello $name\""));
  ASSERT(roundtrip_ok("print \"result: $[+ 1 2]\""));
  ASSERT(roundtrip_ok("print \"$a and $b\""));
  ASSERT(roundtrip_ok("print \"$a$b\""));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_complex(void) {
  setup();
  ASSERT(roundtrip_ok(
    "def x 42\n"
    "def name \"world\"\n"
    "def result [+ $x 10]\n"
    "proc greet {} {\n"
    "  print \"hello $name\"\n"
    "}\n"
    "if [> $x 0] {\n"
    "  print \"positive\"\n"
    "} else {\n"
    "  print \"non-positive\"\n"
    "}\n"
    "greet\n"
  ));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-013: Comprehensive new-syntax roundtrip and nesting tests ---- */

static int parse_fails(const char* input) {
  LexResult t = lexer_lex(input, &test_arena);
  ParseResult r = parser_parse(t, &test_arena);
  return r.error_count > 0;
}

static int test_roundtrip_binding_ops(void) {
  setup();
  /* Binding operators were removed: bindings are name-first prefix commands. */
  ASSERT(roundtrip_ok("def x 5"));
  ASSERT(roundtrip_ok("mut x 0"));
  ASSERT(roundtrip_ok("set x 10"));
  ASSERT(roundtrip_ok("def x [+ 1 2]"));
  /* The old infix operators no longer lex/parse. */
  ASSERT(parse_fails("x = 5"));
  ASSERT(parse_fails("x : 0"));
  ASSERT(parse_fails("x :: 10"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_pipe(void) {
  setup();
  /* Pipe desugars to nested commands */
  ASSERT(roundtrip_ok("get-data | filter | print"));
  ASSERT(roundtrip_ok("a | b $x"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_lambda(void) {
  setup();
  /* Lambda desugars to proc — roundtrip on desugared form */
  ASSERT(roundtrip_ok("for [vec 1 2 3] [\\ print $it]"));
  ASSERT(roundtrip_ok("[\\ + $it 1]"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_arrow(void) {
  setup();
  ASSERT(roundtrip_ok("print $p->x"));
  ASSERT(roundtrip_ok("print $p->x->y"));
  ASSERT(roundtrip_ok("[foo $point->name]"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_new_proc(void) {
  setup();
  ASSERT(roundtrip_ok("proc add {i32 a, i32 b} { [+ $a $b] }"));
  ASSERT(roundtrip_ok("proc greet {} { print hello }"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_new_struct(void) {
  setup();
  ASSERT(roundtrip_ok("struct Pt {i32 x, i32 y}"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_roundtrip_new_if_while(void) {
  setup();
  ASSERT(roundtrip_ok("if $cond { print yes } else { print no }"));
  ASSERT(roundtrip_ok("while $running { tick }"));
  ASSERT(roundtrip_ok("if $a { 1 } elif $b { 2 } else { 3 }"));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_nesting_block_with_bracket(void) {
  /* {} containing [] */
  setup();
  ParseResult r = parse("if [> $x 0] { print [+ $x 1] }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  /* condition is bracket command */
  ASSERT(n->data.command.args[0]->type == AST_COMMAND);
  /* body block inner print has nested bracket command arg */
  AstNode* body = n->data.command.args[1];
  ASSERT(body->type == AST_BLOCK);
  AstNode* print_cmd = body->data.block.commands[0];
  ASSERT(print_cmd->data.command.args[0]->type == AST_COMMAND);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_nesting_pipe_with_lambda(void) {
  /* Pipe + lambda: data | map [\ + $it 1] */
  setup();
  ParseResult r = parse("data | map [\\ + $it 1]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  /* Pipe kept as [| [data] [map [\...]]] — compiler desugars */
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* right side is [map [\...]] */
  AstNode* right = n->data.command.args[1];
  ASSERT(right->type == AST_COMMAND);
  ASSERT(memcmp(right->data.command.head->data.lit_string.value, "map", 3) == 0);
  /* the lambda is an arg to map (now a regular command with head=\) */
  AstNode* lambda = right->data.command.args[0];
  ASSERT(lambda->type == AST_COMMAND);
  ASSERT(lambda->data.command.head->data.lit_string.value[0] == '\\');
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- M14 US-002: use declarations ---- */

static int test_use_basic(void) {
  setup();
  ParseResult r = parse("use \"./math.jacl\" {add, sub, mul}");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_USE);
  ASSERT_U32_EQ(n->data.use_decl.path_len, 11);
  ASSERT(memcmp(n->data.use_decl.path, "./math.jacl", 11) == 0);
  ASSERT_U32_EQ(n->data.use_decl.is_module_binding, 0);
  ASSERT_U32_EQ(n->data.use_decl.name_count, 3);
  ASSERT(memcmp(n->data.use_decl.names[0], "add", 3) == 0);
  ASSERT(memcmp(n->data.use_decl.names[1], "sub", 3) == 0);
  ASSERT(memcmp(n->data.use_decl.names[2], "mul", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_single_name(void) {
  setup();
  ParseResult r = parse("use \"./lib.jacl\" {foo}");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_USE);
  ASSERT_U32_EQ(n->data.use_decl.is_module_binding, 0);
  ASSERT_U32_EQ(n->data.use_decl.name_count, 1);
  ASSERT(memcmp(n->data.use_decl.names[0], "foo", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_before_statements(void) {
  setup();
  ParseResult r = parse("use \"./lib.jacl\" {add}\n[add 1 2]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT(r.nodes[0]->type == AST_USE);
  ASSERT(r.nodes[1]->type == AST_COMMAND);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_after_statement_error(void) {
  setup();
  ParseResult r = parse("[add 1 2]\nuse \"./lib.jacl\" {add}");
  ASSERT(r.error_count > 0);
  /* First node is a command, second should be error */
  ASSERT(r.nodes[0]->type == AST_COMMAND);
  ASSERT(r.nodes[1]->type == AST_ERROR);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_empty_list_error(void) {
  setup();
  ParseResult r = parse("use \"./lib.jacl\" {}");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_non_string_path_error(void) {
  setup();
  ParseResult r = parse("use lib {add}");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_duplicate_path_error(void) {
  setup();
  ParseResult r = parse("use \"./lib.jacl\" {add}\nuse \"./lib.jacl\" {sub}");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_duplicate_name_error(void) {
  setup();
  ParseResult r = parse("use \"./a.jacl\" {add}\nuse \"./b.jacl\" {add}");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_multiple_modules(void) {
  setup();
  ParseResult r = parse("use \"./math.jacl\" {add}\nuse \"./str.jacl\" {concat}");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT(r.nodes[0]->type == AST_USE);
  ASSERT(r.nodes[1]->type == AST_USE);
  ASSERT(memcmp(r.nodes[0]->data.use_decl.path, "./math.jacl", 11) == 0);
  ASSERT(memcmp(r.nodes[1]->data.use_decl.path, "./str.jacl", 10) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_pretty_print(void) {
  setup();
  ParseResult r = parse("use \"./math.jacl\" {add, sub}");
  ASSERT_U32_EQ(r.error_count, 0);
  const char* pp = ast_pretty_print(r.nodes[0], &test_arena);
  ASSERT(strcmp(pp, "use \"./math.jacl\" {add, sub}") == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- M14 US-005: underscore-prefix privacy ---- */

static int test_use_private_name_error(void) {
  setup();
  /* Private name error is now caught at compile time, not parse time.
     Parser should accept this and compiler rejects it. */
  ParseResult r = parse("use \"./mod.jacl\" {_helper}");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_private_name_mixed(void) {
  setup();
  /* Private names mixed with public — parser accepts, compiler rejects */
  ParseResult r = parse("use \"./mod.jacl\" {add, _internal, sub}");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_use_non_private_ok(void) {
  setup();
  /* Names not starting with _ should be fine */
  ParseResult r = parse("use \"./mod.jacl\" {add, sub_thing, mul}");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Syntax Redesign US-002: Infix mode () ---- */

/* ---- Syntax Redesign US-003: New proc syntax ---- */

/* Helper: verify AST_COMMAND shape for proc. Returns 1 on success, 0 on failure. */
static int assert_proc_head(AstNode* n) {
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "proc", 4) == 0);
  return 1;
}

static int test_proc_basic_new_syntax(void) {
  setup();
  /* proc add {a, b} {+ $a $b} → AST_COMMAND(proc, [add, params, body]) */
  ParseResult r = parse("proc add {a, b} { + $a $b }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(assert_proc_head(n));
  ASSERT_U32_EQ(n->data.command.arg_count, 3); /* name, params, body */
  /* arg[0]: name "add" */
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "add", 3) == 0);
  /* arg[1]: params AST_COMMAND with flat list [a b] */
  AstNode* params = n->data.command.args[1];
  ASSERT(params->type == AST_COMMAND);
  ASSERT(memcmp(params->data.command.head->data.lit_string.value, "a", 1) == 0);
  ASSERT_U32_EQ(params->data.command.arg_count, 1);
  ASSERT(memcmp(params->data.command.args[0]->data.lit_string.value, "b", 1) == 0);
  /* arg[2]: body AST_BLOCK */
  ASSERT(n->data.command.args[2]->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.command.args[2]->data.block.count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_typed_params(void) {
  setup();
  /* proc add {i64 a, i64 b} i64 { + $a $b } → 4 args
     New layout (June 2026 redesign §4): [name, params, ret_type, body] */
  ParseResult r = parse("proc add {i64 a, i64 b} i64 { + $a $b }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(assert_proc_head(n));
  ASSERT_U32_EQ(n->data.command.arg_count, 4); /* name, params, ret_type, body */
  /* arg[0]: name "add" */
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "add", 3) == 0);
  /* arg[1]: params flat list */
  AstNode* params = n->data.command.args[1];
  ASSERT(params->type == AST_COMMAND);
  ASSERT(memcmp(params->data.command.head->data.lit_string.value, "i64", 3) == 0);
  ASSERT_U32_EQ(params->data.command.arg_count, 3);
  ASSERT(memcmp(params->data.command.args[0]->data.lit_string.value, "a", 1) == 0);
  ASSERT(memcmp(params->data.command.args[1]->data.lit_string.value, "i64", 3) == 0);
  ASSERT(memcmp(params->data.command.args[2]->data.lit_string.value, "b", 1) == 0);
  /* arg[2]: return type "i64" */
  ASSERT(memcmp(n->data.command.args[2]->data.lit_string.value, "i64", 3) == 0);
  /* arg[3]: body AST_BLOCK */
  ASSERT(n->data.command.args[3]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_zero_params_new(void) {
  setup();
  /* proc greet {} { print "hello" } → empty params */
  ParseResult r = parse("proc greet {} { print \"hello\" }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(assert_proc_head(n));
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  /* params: empty AST_COMMAND */
  AstNode* params = n->data.command.args[1];
  ASSERT(params->type == AST_COMMAND);
  ASSERT_U32_EQ(params->data.command.head->data.lit_string.length, 0);
  ASSERT_U32_EQ(params->data.command.arg_count, 0);
  /* body has one command */
  ASSERT(n->data.command.args[2]->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.command.args[2]->data.block.count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_untyped_params(void) {
  setup();
  /* proc add {a, b} {+ $a $b} — untyped params */
  ParseResult r = parse("proc add {a, b} { + $a $b }");
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* params = r.nodes[0]->data.command.args[1];
  ASSERT(params->type == AST_COMMAND);
  /* Flat list: head="a", args=["b"] */
  ASSERT(memcmp(params->data.command.head->data.lit_string.value, "a", 1) == 0);
  ASSERT_U32_EQ(params->data.command.arg_count, 1);
  ASSERT(memcmp(params->data.command.args[0]->data.lit_string.value, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_old_syntax_rejected(void) {
  setup();
  /* Old syntax: [proc add [a b] { + $a $b }] — now rejected */
  ParseResult r = parse("[proc add [a b] { + $a $b }]");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_in_block(void) {
  setup();
  /* proc defined inside a block */
  ParseResult r = parse("{ proc add {a, b} { + $a $b } }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* blk = r.nodes[0];
  ASSERT(blk->type == AST_BLOCK);
  ASSERT_U32_EQ(blk->data.block.count, 1);
  AstNode* proc_cmd = blk->data.block.commands[0];
  assert_proc_head(proc_cmd);
  ASSERT_U32_EQ(proc_cmd->data.command.arg_count, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_multiline(void) {
  setup();
  /* Multiline proc definition */
  ParseResult r = parse(
    "proc add {a, b} {\n"
    "  [+ $a $b]\n"
    "}\n"
  );
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(assert_proc_head(n));
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_mixed_typed_untyped(void) {
  setup();
  /* proc foo {i64 a, b} → flat list [i64 a b] */
  ParseResult r = parse("proc foo {i64 a, b} { $a }");
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* params = r.nodes[0]->data.command.args[1];
  ASSERT(params->type == AST_COMMAND);
  /* head="i64", args=["a", "b"] */
  ASSERT(memcmp(params->data.command.head->data.lit_string.value, "i64", 3) == 0);
  ASSERT_U32_EQ(params->data.command.arg_count, 2);
  ASSERT(memcmp(params->data.command.args[0]->data.lit_string.value, "a", 1) == 0);
  ASSERT(memcmp(params->data.command.args[1]->data.lit_string.value, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_single_param(void) {
  setup();
  /* proc double {n} {* $n 2} → single param */
  ParseResult r = parse("proc double {n} { * $n 2 }");
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* params = r.nodes[0]->data.command.args[1];
  ASSERT(params->type == AST_COMMAND);
  ASSERT(memcmp(params->data.command.head->data.lit_string.value, "n", 1) == 0);
  ASSERT_U32_EQ(params->data.command.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_proc_bare_cmd_one_block_not_new(void) {
  setup();
  /* proc greet { print hello } — only one block → now rejected (proc requires two {} blocks) */
  ParseResult r = parse("proc greet { print hello }");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Syntax Redesign US-004: New struct syntax ---- */

static int test_struct_basic_new_syntax(void) {
  setup();
  /* struct Point {i32 x, i32 y} */
  ParseResult r = parse("struct Point {i32 x, i32 y}");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_DEFSTRUCT);
  ASSERT_U32_EQ(n->data.defstruct.field_count, 2);
  ASSERT(memcmp(n->data.defstruct.name, "Point", 5) == 0);
  ASSERT(memcmp(n->data.defstruct.field_types[0], "i32", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[0], "x", 1) == 0);
  ASSERT(memcmp(n->data.defstruct.field_types[1], "i32", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[1], "y", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_mixed_types(void) {
  setup();
  /* struct Record {i64 id, str name, f32 score} */
  ParseResult r = parse("struct Record {i64 id, str name, f32 score}");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_DEFSTRUCT);
  ASSERT_U32_EQ(n->data.defstruct.field_count, 3);
  ASSERT(memcmp(n->data.defstruct.field_types[0], "i64", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[0], "id", 2) == 0);
  ASSERT(memcmp(n->data.defstruct.field_types[1], "str", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[1], "name", 4) == 0);
  ASSERT(memcmp(n->data.defstruct.field_types[2], "f32", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[2], "score", 5) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_single_field(void) {
  setup();
  /* struct Wrapper {i32 val} */
  ParseResult r = parse("struct Wrapper {i32 val}");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_DEFSTRUCT);
  ASSERT_U32_EQ(n->data.defstruct.field_count, 1);
  ASSERT(memcmp(n->data.defstruct.field_types[0], "i32", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[0], "val", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_old_syntax_rejected(void) {
  setup();
  /* Old defstruct syntax is now rejected */
  ParseResult r = parse("defstruct Point [x :i32] [y :i32]");
  ASSERT(r.error_count > 0);
  /* Also test struct with bracket fields → error */
  ParseResult r2 = parse("struct Point [x :i32]");
  ASSERT(r2.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_duplicate_field_new(void) {
  setup();
  /* Duplicate field name → error */
  ParseResult r = parse("struct Bad {i32 x, i32 x}");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_empty_fields_error(void) {
  setup();
  /* Empty fields → error */
  ParseResult r = parse("struct Empty {}");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_struct_type_field(void) {
  setup();
  /* A struct field referencing another struct by name */
  ParseResult r = parse("struct Line {Point start, Point end}");
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_DEFSTRUCT);
  ASSERT_U32_EQ(n->data.defstruct.field_count, 2);
  ASSERT(memcmp(n->data.defstruct.field_types[0], "Point", 5) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[0], "start", 5) == 0);
  ASSERT(memcmp(n->data.defstruct.field_types[1], "Point", 5) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[1], "end", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_multiline_new(void) {
  setup();
  /* Multiline struct definition */
  ParseResult r = parse(
    "struct Vec3 {\n"
    "  f32 x,\n"
    "  f32 y,\n"
    "  f32 z\n"
    "}\n"
  );
  ASSERT_U32_EQ(r.error_count, 0);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_DEFSTRUCT);
  ASSERT_U32_EQ(n->data.defstruct.field_count, 3);
  ASSERT(memcmp(n->data.defstruct.field_types[0], "f32", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[0], "x", 1) == 0);
  ASSERT(memcmp(n->data.defstruct.field_types[2], "f32", 3) == 0);
  ASSERT(memcmp(n->data.defstruct.field_names[2], "z", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Syntax Redesign US-005: New if/elif/else and while syntax ---- */

static int test_if_basic_new(void) {
  setup();
  /* if $cond { body } — no else, 2-arg if */
  ParseResult r = parse("if $true { 42 }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* condition: $true */
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  /* then block */
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  ASSERT_U32_EQ(n->data.command.args[1]->data.block.count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_else_new(void) {
  setup();
  /* if $cond { 1 } else { 2 } — 3-arg if */
  ParseResult r = parse("if $true { 1 } else { 2 }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  ASSERT(n->data.command.args[2]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_elif_else_new(void) {
  setup();
  /* if $a { 1 } elif $b { 2 } else { 3 }
     Desugars to: [if $a {1} {[if $b {2} {3}]}] */
  ParseResult r = parse("if $a { 1 } elif $b { 2 } else { 3 }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  /* else branch is a block containing the nested if */
  AstNode* else_blk = n->data.command.args[2];
  ASSERT(else_blk->type == AST_BLOCK);
  ASSERT_U32_EQ(else_blk->data.block.count, 1);
  AstNode* nested = else_blk->data.block.commands[0];
  ASSERT(nested->type == AST_COMMAND);
  ASSERT(memcmp(nested->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(nested->data.command.arg_count, 3);
  ASSERT(nested->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(nested->data.command.args[1]->type == AST_BLOCK);
  ASSERT(nested->data.command.args[2]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_elif_chain_new(void) {
  setup();
  /* if $a { 1 } elif $b { 2 } elif $c { 3 } else { 4 } — double elif */
  ParseResult r = parse("if $a { 1 } elif $b { 2 } elif $c { 3 } else { 4 }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  /* First elif desugars to nested if in else block */
  AstNode* elif1_blk = n->data.command.args[2];
  ASSERT(elif1_blk->type == AST_BLOCK);
  AstNode* elif1 = elif1_blk->data.block.commands[0];
  ASSERT(elif1->type == AST_COMMAND);
  ASSERT_U32_EQ(elif1->data.command.arg_count, 3);
  /* Second elif desugars to nested if in else block */
  AstNode* elif2_blk = elif1->data.command.args[2];
  ASSERT(elif2_blk->type == AST_BLOCK);
  AstNode* elif2 = elif2_blk->data.block.commands[0];
  ASSERT(elif2->type == AST_COMMAND);
  ASSERT_U32_EQ(elif2->data.command.arg_count, 3);
  ASSERT(elif2->data.command.args[2]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_command_condition(void) {
  setup();
  /* if [> $x 0] { 1 } else { 2 } — bracket command condition */
  ParseResult r = parse("if [> $x 0] { 1 } else { 2 }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  AstNode* cond = n->data.command.args[0];
  ASSERT(cond->type == AST_COMMAND);
  ASSERT(memcmp(cond->data.command.head->data.lit_string.value, ">", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_multiline_new(void) {
  setup();
  /* Multiline if/else with newlines between } else { */
  ParseResult r = parse(
    "if $cond {\n"
    "  print \"yes\"\n"
    "}\n"
    "else {\n"
    "  print \"no\"\n"
    "}\n"
  );
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  ASSERT(n->data.command.args[2]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_nested_new(void) {
  setup();
  /* Nested if inside then branch */
  ParseResult r = parse(
    "if $outer {\n"
    "  if $inner { 1 } else { 2 }\n"
    "}\n"
  );
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  AstNode* then_blk = n->data.command.args[1];
  ASSERT(then_blk->type == AST_BLOCK);
  ASSERT_U32_EQ(then_blk->data.block.count, 1);
  AstNode* inner = then_blk->data.block.commands[0];
  ASSERT(inner->type == AST_COMMAND);
  ASSERT(memcmp(inner->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(inner->data.command.arg_count, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_old_bracket_compat(void) {
  setup();
  /* Old bracket syntax still works: [if cond {then} {else}] */
  AstNode* n = parse_expr("[if $true { 1 } { 2 }]");
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  ASSERT(n->data.command.args[2]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_while_basic_new(void) {
  setup();
  /* while $cond { body } — 2-arg while */
  ParseResult r = parse("while $running { print \"loop\" }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "while", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_while_multiline_new(void) {
  setup();
  /* Multiline while */
  ParseResult r = parse(
    "while [< $i 5] {\n"
    "  print $i\n"
    "  def i [+ $i 1]\n"
    "}\n"
  );
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "while", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* body has 2 commands */
  AstNode* body = n->data.command.args[1];
  ASSERT(body->type == AST_BLOCK);
  ASSERT_U32_EQ(body->data.block.count, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_while_old_bracket_compat(void) {
  setup();
  /* Old bracket syntax still works: [while cond {body}] */
  AstNode* n = parse_expr("[while $true { 1 }]");
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "while", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_if_elif_no_else(void) {
  setup();
  /* if $a { 1 } elif $b { 2 } — no else, elif still desugars to nested if */
  ParseResult r = parse("if $a { 1 } elif $b { 2 }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  AstNode* else_blk = n->data.command.args[2];
  ASSERT(else_blk->type == AST_BLOCK);
  AstNode* nested = else_blk->data.block.commands[0];
  ASSERT(nested->type == AST_COMMAND);
  ASSERT(memcmp(nested->data.command.head->data.lit_string.value, "if", 2) == 0);
  ASSERT_U32_EQ(nested->data.command.arg_count, 2); /* no else on nested */
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ====================== Syntax Redesign US-006: for command ====================== */

/* for $items { body } — implicit $it form: parses as 2-arg command */
static int test_for_implicit_it(void) {
  setup();
  ParseResult r = parse("for $v { print $it }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "for", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for $items name { body } — explicit binding: parses as 3-arg command */
static int test_for_explicit_binding(void) {
  setup();
  ParseResult r = parse("for $v item { print $item }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "for", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[1]->data.lit_string.value, "item", 4) == 0);
  ASSERT(n->data.command.args[2]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for $items $callback — HOF form: parses as 2-arg command */
static int test_for_hof_form(void) {
  setup();
  ParseResult r = parse("[for $v $cb]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "for", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[1]->type == AST_VAR_REF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for with inline collection: for [vec 1 2 3] { body } */
static int test_for_inline_collection(void) {
  setup();
  ParseResult r = parse("for [vec 1 2 3] { print $it }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "for", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_COMMAND); /* [vec 1 2 3] */
  ASSERT(n->data.command.args[1]->type == AST_BLOCK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Binding operators (=, :, ::) were removed ---- */

/* x = 5 → lex/parse error: '=' is no longer a binding operator */
static int test_binding_equals_def(void) {
  setup();
  ParseResult r = parse("x = 5");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* i64 x = 5 → parse error */
static int test_binding_typed_equals(void) {
  setup();
  ParseResult r = parse("i64 x = 5");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* x : 0 → parse error */
static int test_binding_colon_mut(void) {
  setup();
  ParseResult r = parse("x : 0");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* i64 x : 0 → parse error */
static int test_binding_typed_colon(void) {
  setup();
  ParseResult r = parse("i64 x : 0");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* x :: [+ $x 1] → parse error */
static int test_binding_double_colon_set(void) {
  setup();
  ParseResult r = parse("x :: [+ $x 1]");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Binding operators inside {} block → parse error */
static int test_binding_in_block(void) {
  setup();
  ParseResult r = parse("{ x = 5; y : 0 }");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* def/mut/set command forms are the only binding syntax */
static int test_binding_def_cmd_still_works(void) {
  setup();
  ParseResult r = parse("def x 5");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Binding with expression value uses prefix form: def x [vec 1 2 3] */
static int test_binding_expr_value(void) {
  setup();
  ParseResult r = parse("def x [vec 1 2 3]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "def", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[1]->type == AST_COMMAND); /* [vec 1 2 3] */
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* '=' is an error token even inside [] */
static int test_binding_not_in_brackets(void) {
  setup();
  ParseResult r = parse("[x = 5]");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Syntax Redesign US-009: Arrow field access (->) ---- */

/* $point->x parses as [. $point x] internally */
static int test_arrow_basic(void) {
  setup();
  /* [. $point x] in brackets is now rejected — use $point->x instead */
  ParseResult r = parse("[. $point x]");
  ASSERT(r.error_count > 0);
  /* Arrow syntax works */
  ParseResult r2 = parse("$point->x");
  ASSERT_U32_EQ(r2.error_count, 0);
  ASSERT_U32_EQ(r2.count, 1);
  AstNode* n = r2.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, ".", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.command.args[0]->data.var_ref.name, "point", 5) == 0);
  ASSERT(n->data.command.args[1]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[1]->data.lit_string.value, "x", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* $a->b->c parses as [. [. $a b] c] — chained access */
static int test_arrow_chained(void) {
  setup();
  ParseResult r = parse("$a->b->c");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* outer = r.nodes[0];
  ASSERT(outer->type == AST_COMMAND);
  ASSERT(memcmp(outer->data.command.head->data.lit_string.value, ".", 1) == 0);
  ASSERT_U32_EQ(outer->data.command.arg_count, 2);
  /* outer field is "c" */
  ASSERT(memcmp(outer->data.command.args[1]->data.lit_string.value, "c", 1) == 0);
  /* inner is [. $a b] */
  AstNode* inner = outer->data.command.args[0];
  ASSERT(inner->type == AST_COMMAND);
  ASSERT(memcmp(inner->data.command.head->data.lit_string.value, ".", 1) == 0);
  ASSERT_U32_EQ(inner->data.command.arg_count, 2);
  ASSERT(inner->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(inner->data.command.args[1]->data.lit_string.value, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* [get-point]->x — field access on command result */
static int test_arrow_on_command(void) {
  setup();
  ParseResult r = parse("[foo $a]->x");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, ".", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* first arg is the [foo $a] command */
  ASSERT(n->data.command.args[0]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.args[0]->data.command.head->data.lit_string.value, "foo", 3) == 0);
  /* second arg is field name "x" */
  ASSERT(memcmp(n->data.command.args[1]->data.lit_string.value, "x", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ($point->x + 1) — arrow inside infix mode */
/* [foo $point->x] — arrow inside bracket command args */
static int test_arrow_in_bracket_cmd(void) {
  setup();
  ParseResult r = parse("[foo $point->x]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "foo", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  /* the arg is [. $point x] */
  AstNode* dot = n->data.command.args[0];
  ASSERT(dot->type == AST_COMMAND);
  ASSERT(memcmp(dot->data.command.head->data.lit_string.value, ".", 1) == 0);
  ASSERT(dot->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(dot->data.command.args[1]->data.lit_string.value, "x", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Old [. $struct field] syntax is now rejected */
static int test_arrow_old_dot_rejected(void) {
  setup();
  ParseResult r = parse("[. $p x]");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Syntax Redesign US-010: Pipe threading (|) ---- */

/* foo $a | bar $b  →  [| [foo $a] [bar $b]] — compiler desugars pipe */
static int test_pipe_basic(void) {
  setup();
  ParseResult r = parse("foo $a | bar $b");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* left: [foo $a] */
  AstNode* left = n->data.command.args[0];
  ASSERT(left->type == AST_COMMAND);
  ASSERT(memcmp(left->data.command.head->data.lit_string.value, "foo", 3) == 0);
  ASSERT_U32_EQ(left->data.command.arg_count, 1);
  ASSERT(left->data.command.args[0]->type == AST_VAR_REF);
  /* right: [bar $b] */
  AstNode* right = n->data.command.args[1];
  ASSERT(right->type == AST_COMMAND);
  ASSERT(memcmp(right->data.command.head->data.lit_string.value, "bar", 3) == 0);
  ASSERT_U32_EQ(right->data.command.arg_count, 1);
  ASSERT(right->data.command.args[0]->type == AST_VAR_REF);
  ASSERT(memcmp(right->data.command.args[0]->data.var_ref.name, "b", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* a | b | c  →  [| [| [a] [b]] [c]] — left-to-right, compiler desugars */
static int test_pipe_multi_stage(void) {
  setup();
  ParseResult r = parse("a | b | c");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  /* Outermost: [| ... [c]] */
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* right: [c] */
  AstNode* right = n->data.command.args[1];
  ASSERT(right->type == AST_COMMAND);
  ASSERT(memcmp(right->data.command.head->data.lit_string.value, "c", 1) == 0);
  ASSERT_U32_EQ(right->data.command.arg_count, 0);
  /* left: [| [a] [b]] */
  AstNode* left = n->data.command.args[0];
  ASSERT(left->type == AST_COMMAND);
  ASSERT(memcmp(left->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(left->data.command.arg_count, 2);
  /* left-left: [a] */
  AstNode* ll = left->data.command.args[0];
  ASSERT(ll->type == AST_COMMAND);
  ASSERT(memcmp(ll->data.command.head->data.lit_string.value, "a", 1) == 0);
  ASSERT_U32_EQ(ll->data.command.arg_count, 0);
  /* left-right: [b] */
  AstNode* lr = left->data.command.args[1];
  ASSERT(lr->type == AST_COMMAND);
  ASSERT(memcmp(lr->data.command.head->data.lit_string.value, "b", 1) == 0);
  ASSERT_U32_EQ(lr->data.command.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* + 1 2 | * 3  →  [| [+ 1 2] [* 3]] — compiler desugars pipe */
static int test_pipe_with_args(void) {
  setup();
  ParseResult r = parse("+ 1 2 | * 3");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* left: [+ 1 2] */
  AstNode* left = n->data.command.args[0];
  ASSERT(left->type == AST_COMMAND);
  ASSERT(memcmp(left->data.command.head->data.lit_string.value, "+", 1) == 0);
  ASSERT_U32_EQ(left->data.command.arg_count, 2);
  ASSERT(left->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(left->data.command.args[0]->data.lit_int.value, 1);
  ASSERT(left->data.command.args[1]->type == AST_LIT_INT);
  ASSERT_INT_EQ(left->data.command.args[1]->data.lit_int.value, 2);
  /* right: [* 3] */
  AstNode* right = n->data.command.args[1];
  ASSERT(right->type == AST_COMMAND);
  ASSERT(memcmp(right->data.command.head->data.lit_string.value, "*", 1) == 0);
  ASSERT_U32_EQ(right->data.command.arg_count, 1);
  ASSERT(right->data.command.args[0]->type == AST_LIT_INT);
  ASSERT_INT_EQ(right->data.command.args[0]->data.lit_int.value, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Pipe in block: { foo $a | bar $b } → [| [foo $a] [bar $b]] */
static int test_pipe_in_block(void) {
  setup();
  ParseResult r = parse("{ foo $a | bar $b }");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* block = r.nodes[0];
  ASSERT(block->type == AST_BLOCK);
  ASSERT_U32_EQ(block->data.block.count, 1);
  AstNode* n = block->data.block.commands[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_COMMAND); /* [foo $a] */
  ASSERT(n->data.command.args[1]->type == AST_COMMAND); /* [bar $b] */
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Zero-arg pipe: vec 1 2 3 | vlen  →  [| [vec 1 2 3] [vlen]] */
static int test_pipe_zero_arg_right(void) {
  setup();
  ParseResult r = parse("vec 1 2 3 | vlen");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* left: [vec 1 2 3] */
  AstNode* left = n->data.command.args[0];
  ASSERT(left->type == AST_COMMAND);
  ASSERT(memcmp(left->data.command.head->data.lit_string.value, "vec", 3) == 0);
  ASSERT_U32_EQ(left->data.command.arg_count, 3);
  /* right: [vlen] */
  AstNode* right = n->data.command.args[1];
  ASSERT(right->type == AST_COMMAND);
  ASSERT(memcmp(right->data.command.head->data.lit_string.value, "vlen", 4) == 0);
  ASSERT_U32_EQ(right->data.command.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Pipe inside [] still treats | as literal (no pipe semantics) */
static int test_pipe_literal_in_brackets(void) {
  setup();
  ParseResult r = parse("[foo | bar]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "foo", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* | is parsed as literal string "|" inside brackets */
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "|", 1) == 0);
  ASSERT(n->data.command.args[1]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[1]->data.lit_string.value, "bar", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-011: Lambda shorthand (\\) ---- */

static int test_lambda_basic(void) {
  /* [\\ + $it 2] → [\ + $it 2] (macro handles desugaring now) */
  setup();
  AstNode* n = parse_expr("[\\ + $it 2]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  /* head = "\\" */
  ASSERT(n->data.command.head->type == AST_LIT_STRING);
  ASSERT(n->data.command.head->data.lit_string.length == 1);
  ASSERT(n->data.command.head->data.lit_string.value[0] == '\\');
  /* 3 args: +, $it, 2 */
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "+", 1) == 0);
  ASSERT(n->data.command.args[1]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[2]->type == AST_LIT_INT);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lambda_as_callback(void) {
  /* for $items [\\ print $it] — lambda used as callback */
  setup();
  ParseResult r = parse("for $items [\\ print $it]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "for", 3) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* second arg is lambda command [\ print $it] */
  AstNode* lambda = n->data.command.args[1];
  ASSERT(lambda->type == AST_COMMAND);
  ASSERT(lambda->data.command.head->data.lit_string.value[0] == '\\');
  ASSERT_U32_EQ(lambda->data.command.arg_count, 2); /* print, $it */
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lambda_single_arg(void) {
  /* [\\ print $it] — single-arg body (now just a regular command) */
  setup();
  AstNode* n = parse_expr("[\\ print $it]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->data.lit_string.value[0] == '\\');
  /* 2 args: print, $it */
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.command.args[0]->data.lit_string.value, "print", 5) == 0);
  ASSERT(n->data.command.args[1]->type == AST_VAR_REF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lambda_nested_bracket(void) {
  /* [\\ + $it [len $v]] — nested [] inside lambda (now regular command) */
  setup();
  AstNode* n = parse_expr("[\\ + $it [len $v]]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->data.lit_string.value[0] == '\\');
  /* 3 args: +, $it, [len $v] */
  ASSERT_U32_EQ(n->data.command.arg_count, 3);
  ASSERT(n->data.command.args[0]->type == AST_LIT_STRING);
  ASSERT(n->data.command.args[1]->type == AST_VAR_REF);
  ASSERT(n->data.command.args[2]->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.args[2]->data.command.head->data.lit_string.value, "len", 3) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lambda_in_pipe(void) {
  /* Lambda used in pipe: vec 1 2 3 | filter [\\ > $it 2] */
  setup();
  ParseResult r = parse("vec 1 2 3 | filter [\\ > $it 2]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  /* pipe kept as [| [vec 1 2 3] [filter [\\ > $it 2]]] */
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* right side: [filter [\...]] */
  AstNode* right = n->data.command.args[1];
  ASSERT(right->type == AST_COMMAND);
  ASSERT(memcmp(right->data.command.head->data.lit_string.value, "filter", 6) == 0);
  /* filter's arg is lambda (now regular command with head=\) */
  AstNode* lambda = right->data.command.args[0];
  ASSERT(lambda->type == AST_COMMAND);
  ASSERT(lambda->data.command.head->data.lit_string.value[0] == '\\');
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_lambda_no_args(void) {
  /* [\\ $it] — lambda with just a variable ref (now regular command) */
  setup();
  AstNode* n = parse_expr("[\\ $it]");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_COMMAND);
  ASSERT(n->data.command.head->data.lit_string.value[0] == '\\');
  /* 1 arg: $it */
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  ASSERT(n->data.command.args[0]->type == AST_VAR_REF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-012: $(expr) string interpolation and line continuation ---- */

static int test_dollar_paren_existing_interp(void) {
  /* Existing $var interp still works */
  setup();
  AstNode* n = parse_expr("\"hello $name\"");
  ASSERT(n != NULL);
  ASSERT(n->type == AST_INTERP_STRING);
  ASSERT_U32_EQ(n->data.interp_string.count, 2);
  ASSERT(n->data.interp_string.segments[1]->type == AST_VAR_REF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_line_continuation(void) {
  /* \ at end of line continues command */
  setup();
  ParseResult r = parse("print \\\n  42");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "print", 5) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 1);
  ASSERT(n->data.command.args[0]->type == AST_LIT_INT);
  ASSERT(n->data.command.args[0]->data.lit_int.value == 42);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_blank_lines_no_empty(void) {
  /* Blank lines (multiple newlines) don't produce empty commands */
  setup();
  ParseResult r = parse("\n\n\nprint 1\n\n\nprint 2\n\n");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Shell Interop US-002: Parser handles !cmd syntax ---- */

/* !ls → AST_SHELL_CMD with head="ls", args=[] */
static int test_shell_cmd_simple(void) {
  setup();
  ParseResult r = parse("!ls");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_SHELL_CMD);
  ASSERT(n->data.shell_cmd.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.shell_cmd.head->data.lit_string.value, "ls", 2) == 0);
  ASSERT_U32_EQ(n->data.shell_cmd.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* !git status → AST_SHELL_CMD with head="git", args=["status"] */
static int test_shell_cmd_with_arg(void) {
  setup();
  ParseResult r = parse("!git status");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_SHELL_CMD);
  ASSERT(memcmp(n->data.shell_cmd.head->data.lit_string.value, "git", 3) == 0);
  ASSERT_U32_EQ(n->data.shell_cmd.arg_count, 1);
  ASSERT(n->data.shell_cmd.args[0]->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.shell_cmd.args[0]->data.lit_string.value, "status", 6) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* !git status --short → multiple args */
static int test_shell_cmd_multiple_args(void) {
  setup();
  ParseResult r = parse("!git status --short");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_SHELL_CMD);
  ASSERT(memcmp(n->data.shell_cmd.head->data.lit_string.value, "git", 3) == 0);
  /* --short lexes as OPERATOR(--) + WORD(short), so 3 args */
  ASSERT(n->data.shell_cmd.arg_count >= 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* !ls -la → handles flag-style args (-la is OPERATOR + WORD) */
static int test_shell_cmd_flag_args(void) {
  setup();
  ParseResult r = parse("!ls -la");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_SHELL_CMD);
  ASSERT(memcmp(n->data.shell_cmd.head->data.lit_string.value, "ls", 2) == 0);
  /* -la lexes as OPERATOR(-) + WORD(la), so 2 args */
  ASSERT_U32_EQ(n->data.shell_cmd.arg_count, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* !"my script.sh" → quoted command name */
static int test_shell_cmd_quoted_name(void) {
  setup();
  ParseResult r = parse("!\"my script.sh\"");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_SHELL_CMD);
  ASSERT(n->data.shell_cmd.head->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.shell_cmd.head->data.lit_string.value, "my script.sh", 12) == 0);
  ASSERT_U32_EQ(n->data.shell_cmd.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* !$cmd → variable as command name */
static int test_shell_cmd_var_name(void) {
  setup();
  ParseResult r = parse("!$cmd");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_SHELL_CMD);
  ASSERT(n->data.shell_cmd.head->type == AST_VAR_REF);
  ASSERT(memcmp(n->data.shell_cmd.head->data.var_ref.name, "cmd", 3) == 0);
  ASSERT_U32_EQ(n->data.shell_cmd.arg_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* !cmd ..$args → splat for args */
static int test_shell_cmd_splat_args(void) {
  setup();
  ParseResult r = parse("!cmd ..$args");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_SHELL_CMD);
  ASSERT(memcmp(n->data.shell_cmd.head->data.lit_string.value, "cmd", 3) == 0);
  ASSERT_U32_EQ(n->data.shell_cmd.arg_count, 1);
  ASSERT(n->data.shell_cmd.args[0]->type == AST_SPREAD);
  AstNode* spread = n->data.shell_cmd.args[0];
  ASSERT(spread->data.spread.expr->type == AST_VAR_REF);
  ASSERT(memcmp(spread->data.spread.expr->data.var_ref.name, "args", 4) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* !ls | !grep foo → shell commands in pipeline */
static int test_shell_cmd_in_pipeline(void) {
  setup();
  ParseResult r = parse("!ls | !grep foo");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  /* This is a pipe: [| [!ls] [!grep foo]] */
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "|", 1) == 0);
  ASSERT_U32_EQ(n->data.command.arg_count, 2);
  /* left: !ls */
  AstNode* left = n->data.command.args[0];
  ASSERT(left->type == AST_SHELL_CMD);
  ASSERT(memcmp(left->data.shell_cmd.head->data.lit_string.value, "ls", 2) == 0);
  /* right: !grep foo */
  AstNode* right = n->data.command.args[1];
  ASSERT(right->type == AST_SHELL_CMD);
  ASSERT(memcmp(right->data.shell_cmd.head->data.lit_string.value, "grep", 4) == 0);
  ASSERT_U32_EQ(right->data.shell_cmd.arg_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ! followed by nothing → error */
static int test_shell_cmd_error_no_name(void) {
  setup();
  ParseResult r = parse("!");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* != stays as operator, not shell command */
static int test_shell_cmd_neq_not_shell(void) {
  setup();
  /* In command context, != parses as operator between two operands */
  ParseResult r = parse("1 != 2");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  /* Should be [!= 1 2] command, not shell command */
  ASSERT(n->type == AST_COMMAND);
  ASSERT(memcmp(n->data.command.head->data.lit_string.value, "!=", 2) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Implicit Context US-002: ctx declarations ---- */

static int test_ctx_decl_basic(void) {
  setup();
  /* ctx i32 count 0 */
  ParseResult r = parse("ctx i32 count 0");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_CTX_DECL);
  ASSERT(n->data.ctx_decl.is_mutable == 0);
  ASSERT(n->data.ctx_decl.type_name_len == 3);
  ASSERT(memcmp(n->data.ctx_decl.type_name, "i32", 3) == 0);
  ASSERT(n->data.ctx_decl.field_name_len == 5);
  ASSERT(memcmp(n->data.ctx_decl.field_name, "count", 5) == 0);
  ASSERT(n->data.ctx_decl.default_expr != NULL);
  ASSERT(n->data.ctx_decl.default_expr->type == AST_LIT_INT);
  ASSERT(n->data.ctx_decl.default_expr->data.lit_int.value == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_mutable(void) {
  setup();
  /* ctx mut str name "default" */
  ParseResult r = parse("ctx mut str name \"default\"");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_CTX_DECL);
  ASSERT(n->data.ctx_decl.is_mutable == 1);
  ASSERT(n->data.ctx_decl.type_name_len == 3);
  ASSERT(memcmp(n->data.ctx_decl.type_name, "str", 3) == 0);
  ASSERT(n->data.ctx_decl.field_name_len == 4);
  ASSERT(memcmp(n->data.ctx_decl.field_name, "name", 4) == 0);
  ASSERT(n->data.ctx_decl.default_expr != NULL);
  ASSERT(n->data.ctx_decl.default_expr->type == AST_LIT_STRING);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_float_default(void) {
  setup();
  ParseResult r = parse("ctx f32 score 3.14");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_CTX_DECL);
  ASSERT(n->data.ctx_decl.is_mutable == 0);
  ASSERT(memcmp(n->data.ctx_decl.type_name, "f32", 3) == 0);
  ASSERT(memcmp(n->data.ctx_decl.field_name, "score", 5) == 0);
  ASSERT(n->data.ctx_decl.default_expr->type == AST_LIT_FLOAT);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_bool_default(void) {
  setup();
  ParseResult r = parse("ctx bool verbose true");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_CTX_DECL);
  ASSERT(memcmp(n->data.ctx_decl.type_name, "bool", 4) == 0);
  ASSERT(memcmp(n->data.ctx_decl.field_name, "verbose", 7) == 0);
  ASSERT(n->data.ctx_decl.default_expr->type == AST_LIT_STRING);
  ASSERT(memcmp(n->data.ctx_decl.default_expr->data.lit_string.value, "true", 4) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_multiple(void) {
  setup();
  ParseResult r = parse("ctx i32 x 1\nctx mut str y \"hi\"");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT(r.nodes[0]->type == AST_CTX_DECL);
  ASSERT(r.nodes[1]->type == AST_CTX_DECL);
  ASSERT(memcmp(r.nodes[0]->data.ctx_decl.field_name, "x", 1) == 0);
  ASSERT(r.nodes[1]->data.ctx_decl.is_mutable == 1);
  ASSERT(memcmp(r.nodes[1]->data.ctx_decl.field_name, "y", 1) == 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_missing_default(void) {
  setup();
  /* Missing default value: should produce error */
  ParseResult r = parse("ctx i32 count");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_non_constant(void) {
  setup();
  /* Variable reference as default — not a constant */
  ParseResult r = parse("ctx i32 count $x");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_nested_in_block(void) {
  setup();
  /* ctx inside a block should produce error */
  ParseResult r = parse("proc foo {} { ctx i32 x 0 }");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_nested_in_bare_block(void) {
  setup();
  /* ctx inside a bare block should produce error */
  ParseResult r = parse("if true { ctx i32 x 0 }");
  ASSERT(r.error_count > 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_ctx_decl_struct_constructor_default(void) {
  setup();
  /* Struct constructor with literal args is a valid constant default */
  ParseResult r = parse("ctx Point origin [Point x 0 y 0]");
  ASSERT_U32_EQ(r.error_count, 0);
  ASSERT_U32_EQ(r.count, 1);
  AstNode* n = r.nodes[0];
  ASSERT(n->type == AST_CTX_DECL);
  ASSERT(memcmp(n->data.ctx_decl.type_name, "Point", 5) == 0);
  ASSERT(memcmp(n->data.ctx_decl.field_name, "origin", 6) == 0);
  ASSERT(n->data.ctx_decl.default_expr->type == AST_COMMAND);
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
    {"bare_comma_delim",     test_bare_comma_delimited},
    {"block_comma_delim",    test_block_comma_delimited},
    {"bare_source_positions",test_bare_source_positions},
    /* M6 US-004: Bare words as zero-arg command calls */
    {"bare_word_zero_arg",    test_bare_word_zero_arg},
    {"bare_word_multi_arg",   test_bare_word_multi_arg_unchanged},
    {"bare_word_in_brackets", test_bare_word_inside_brackets},
    {"bare_int_not_wrapped",  test_bare_int_not_wrapped},
    {"bare_float_not_wrapped",test_bare_float_not_wrapped},
    {"bare_quoted_not_wrapped",test_bare_quoted_string_not_wrapped},
    {"bare_bracket_no_double",test_bare_bracket_cmd_not_double_wrapped},
    {"bare_block_not_wrapped",test_bare_block_not_wrapped},
    /* M6 US-005: $var lines as zero-arg command calls */
    {"var_ref_zero_arg",      test_var_ref_zero_arg},
    {"var_ref_with_args",     test_var_ref_with_args},
    {"var_ref_in_brackets",   test_var_ref_inside_brackets_unchanged},
    /* US-007 */
    {"block_single_cmd",     test_block_single_cmd},
    {"block_multiline",      test_block_multiline},
    {"block_semicolon",      test_block_semicolon},
    {"block_as_cmd_arg",     test_block_as_cmd_arg},
    {"block_nested",         test_block_nested},
    {"block_empty",          test_block_empty},
    {"block_mismatched",     test_block_mismatched},
    {"block_source_pos",     test_block_source_positions},
    {"block_bare_cmd_arg",   test_block_bare_cmd_arg},
    /* US-008 */
    {"interp_non_interpolated", test_interp_non_interpolated},
    {"interp_var",           test_interp_var},
    {"interp_expr",          test_interp_expr},
    {"interp_multiple",      test_interp_multiple},
    {"interp_adjacent",      test_interp_adjacent},
    {"interp_source_pos",    test_interp_source_positions},
    /* US-009 */
    {"multi_basic",          test_multi_basic},
    {"multi_mixed_syntax",   test_multi_mixed_syntax},
    {"multi_realistic",      test_multi_realistic_program},
    {"multi_source_pos",     test_multi_source_positions},
    {"multi_comments",       test_multi_comments_between},
    {"multi_semi_newlines",  test_multi_semicolons_and_newlines},
    /* US-010 */
    {"error_missing_bracket", test_error_missing_bracket},
    {"error_missing_brace",   test_error_missing_brace},
    {"error_unexpected_tok",  test_error_unexpected_token},
    {"error_multiple",        test_error_multiple},
    {"error_panic_sync",      test_error_panic_bracket_sync},
    {"error_interleaved",     test_error_interleaved},
    {"error_positions",       test_error_positions},
    {"error_valid_after",     test_error_valid_after},
    /* US-011 */
    {"pp_command_format",     test_pp_command_format},
    {"pp_literals_format",    test_pp_literals_format},
    {"pp_var_ref_format",     test_pp_var_ref_format},
    {"pp_block_format",       test_pp_block_format},
    {"pp_interp_format",      test_pp_interp_format},
    {"pp_program_format",     test_pp_program_format},
    {"roundtrip_literals",    test_roundtrip_literals},
    {"roundtrip_commands",    test_roundtrip_commands},
    {"roundtrip_blocks",      test_roundtrip_blocks},
    {"roundtrip_interp",      test_roundtrip_interp},
    {"roundtrip_complex",     test_roundtrip_complex},
    /* US-013: New-syntax roundtrip and cross-mode nesting */
    {"rt_binding_ops",        test_roundtrip_binding_ops},
    {"rt_pipe",               test_roundtrip_pipe},
    {"rt_lambda",             test_roundtrip_lambda},
    {"rt_arrow",              test_roundtrip_arrow},
    {"rt_new_proc",           test_roundtrip_new_proc},
    {"rt_new_struct",         test_roundtrip_new_struct},
    {"rt_new_if_while",       test_roundtrip_new_if_while},
    {"nest_block_bracket",    test_nesting_block_with_bracket},
    {"nest_pipe_lambda",      test_nesting_pipe_with_lambda},
    /* M14 US-002: use declarations */
    {"use_basic",             test_use_basic},
    {"use_single_name",       test_use_single_name},
    {"use_before_stmts",      test_use_before_statements},
    {"use_after_stmt_err",    test_use_after_statement_error},
    {"use_empty_list_err",    test_use_empty_list_error},
    {"use_non_string_path",   test_use_non_string_path_error},
    {"use_dup_path_err",      test_use_duplicate_path_error},
    {"use_dup_name_err",      test_use_duplicate_name_error},
    {"use_multi_modules",     test_use_multiple_modules},
    {"use_pretty_print",      test_use_pretty_print},
    /* M14 US-005: underscore-prefix privacy */
    {"use_private_err",       test_use_private_name_error},
    {"use_private_mixed",     test_use_private_name_mixed},
    {"use_non_private_ok",    test_use_non_private_ok},
    /* Syntax Redesign US-002: Infix mode () */
    /* Syntax Redesign US-003: New proc syntax */
    {"proc_basic_new",        test_proc_basic_new_syntax},
    {"proc_typed_params",     test_proc_typed_params},
    {"proc_zero_params_new",  test_proc_zero_params_new},
    {"proc_untyped_params",   test_proc_untyped_params},
    {"proc_old_syntax_rejected",test_proc_old_syntax_rejected},
    {"proc_in_block",         test_proc_in_block},
    {"proc_multiline",        test_proc_multiline},
    {"proc_mixed_type_untype",test_proc_mixed_typed_untyped},
    {"proc_single_param",     test_proc_single_param},
    {"proc_one_block_old",    test_proc_bare_cmd_one_block_not_new},
    /* Syntax Redesign US-004: New struct syntax */
    {"struct_basic_new",      test_struct_basic_new_syntax},
    {"struct_mixed_types",    test_struct_mixed_types},
    {"struct_single_field",   test_struct_single_field},
    {"struct_old_rejected",   test_struct_old_syntax_rejected},
    {"struct_dup_field_new",  test_struct_duplicate_field_new},
    {"struct_empty_err",      test_struct_empty_fields_error},
    {"struct_type_field",     test_struct_struct_type_field},
    {"struct_multiline_new",  test_struct_multiline_new},
    /* Syntax Redesign US-005: New if/elif/else and while syntax */
    {"if_basic_new",          test_if_basic_new},
    {"if_else_new",           test_if_else_new},
    {"if_elif_else_new",      test_if_elif_else_new},
    {"if_elif_chain_new",     test_if_elif_chain_new},
    {"if_cmd_condition",      test_if_command_condition},
    {"if_multiline_new",      test_if_multiline_new},
    {"if_nested_new",         test_if_nested_new},
    {"if_old_bracket_compat", test_if_old_bracket_compat},
    {"if_elif_no_else",       test_if_elif_no_else},
    {"while_basic_new",       test_while_basic_new},
    {"while_multiline_new",   test_while_multiline_new},
    {"while_old_bracket_compat", test_while_old_bracket_compat},
    /* Syntax Redesign US-006: for command */
    {"for_implicit_it",         test_for_implicit_it},
    {"for_explicit_binding",    test_for_explicit_binding},
    {"for_hof_form",            test_for_hof_form},
    {"for_inline_collection",   test_for_inline_collection},
    /* Syntax Redesign US-008: Binding operators (=, :, ::) */
    {"bind_equals_def",         test_binding_equals_def},
    {"bind_typed_equals",       test_binding_typed_equals},
    {"bind_colon_mut",          test_binding_colon_mut},
    {"bind_typed_colon",        test_binding_typed_colon},
    {"bind_dcolon_set",         test_binding_double_colon_set},
    {"bind_in_block",           test_binding_in_block},
    {"bind_def_cmd_works",      test_binding_def_cmd_still_works},
    {"bind_expr_value",         test_binding_expr_value},
    {"bind_not_in_brackets",    test_binding_not_in_brackets},
    /* Syntax Redesign US-009: Arrow field access (->) */
    {"arrow_basic",             test_arrow_basic},
    {"arrow_chained",           test_arrow_chained},
    {"arrow_on_command",        test_arrow_on_command},
    {"arrow_in_bracket_cmd",    test_arrow_in_bracket_cmd},
    {"arrow_old_dot_rejected",  test_arrow_old_dot_rejected},
    /* Syntax Redesign US-010: Pipe threading (|) */
    {"pipe_basic",              test_pipe_basic},
    {"pipe_multi_stage",        test_pipe_multi_stage},
    {"pipe_with_args",          test_pipe_with_args},
    {"pipe_in_block",           test_pipe_in_block},
    {"pipe_zero_arg_right",     test_pipe_zero_arg_right},
    {"pipe_literal_in_bracket", test_pipe_literal_in_brackets},
    /* Syntax Redesign US-011: Lambda shorthand (\) */
    {"lambda_basic",            test_lambda_basic},
    {"lambda_as_callback",      test_lambda_as_callback},
    {"lambda_single_arg",       test_lambda_single_arg},
    {"lambda_nested_bracket",   test_lambda_nested_bracket},
    {"lambda_in_pipe",          test_lambda_in_pipe},
    {"lambda_no_args",          test_lambda_no_args},
    /* Syntax Redesign US-012: $(expr) interpolation & line continuation */
    {"dollar_paren_existing",   test_dollar_paren_existing_interp},
    {"line_continuation",       test_line_continuation},
    {"blank_lines_no_empty",    test_blank_lines_no_empty},
    /* Shell Interop US-002: Parser handles !cmd syntax */
    {"shell_cmd_simple",        test_shell_cmd_simple},
    {"shell_cmd_with_arg",      test_shell_cmd_with_arg},
    {"shell_cmd_multiple_args", test_shell_cmd_multiple_args},
    {"shell_cmd_flag_args",     test_shell_cmd_flag_args},
    {"shell_cmd_quoted_name",   test_shell_cmd_quoted_name},
    {"shell_cmd_var_name",      test_shell_cmd_var_name},
    {"shell_cmd_splat_args",    test_shell_cmd_splat_args},
    {"shell_cmd_in_pipeline",   test_shell_cmd_in_pipeline},
    {"shell_cmd_error_no_name", test_shell_cmd_error_no_name},
    {"shell_cmd_neq_not_shell", test_shell_cmd_neq_not_shell},
    /* Implicit Context US-002: ctx declarations */
    {"ctx_decl_basic",         test_ctx_decl_basic},
    {"ctx_decl_mutable",       test_ctx_decl_mutable},
    {"ctx_decl_float_default", test_ctx_decl_float_default},
    {"ctx_decl_bool_default",  test_ctx_decl_bool_default},
    {"ctx_decl_multiple",      test_ctx_decl_multiple},
    {"ctx_decl_missing_default", test_ctx_decl_missing_default},
    {"ctx_decl_non_constant",  test_ctx_decl_non_constant},
    {"ctx_decl_nested_block",  test_ctx_decl_nested_in_block},
    {"ctx_decl_nested_bare",   test_ctx_decl_nested_in_bare_block},
    {"ctx_decl_struct_ctor",   test_ctx_decl_struct_constructor_default},
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
