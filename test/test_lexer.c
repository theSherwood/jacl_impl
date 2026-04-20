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

/* ---- tests ---- */

static int test_empty_input(void) {
  setup();
  LexResult r = lexer_lex("", &test_arena);
  ASSERT_U32_EQ(r.count, 1);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 0);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_single_lbracket(void) {
  setup();
  LexResult r = lexer_lex("[", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_all_delimiters(void) {
  setup();
  LexResult r = lexer_lex("[]{}()", &test_arena);
  ASSERT_U32_EQ(r.count, 7); /* 6 delimiters + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_LBRACE);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_RBRACE);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_LPAREN);
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_RPAREN);
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_EOF);

  /* check positions */
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[1].column, 2);
  ASSERT_U32_EQ(r.tokens[2].column, 3);
  ASSERT_U32_EQ(r.tokens[3].column, 4);
  ASSERT_U32_EQ(r.tokens[4].column, 5);
  ASSERT_U32_EQ(r.tokens[5].column, 6);
  ASSERT_U32_EQ(r.tokens[6].column, 7);

  /* all on line 1 */
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[5].line, 1);

  /* byte offsets */
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[1].offset, 1);
  ASSERT_U32_EQ(r.tokens[5].offset, 5);

  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_whitespace_skipped(void) {
  setup();
  LexResult r = lexer_lex("  [ \t ] ", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* [ ] EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_U32_EQ(r.tokens[0].column, 3);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_RBRACKET);
  ASSERT_U32_EQ(r.tokens[1].column, 7);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_newline_lf(void) {
  setup();
  LexResult r = lexer_lex("\n", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* NEWLINE + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  /* EOF should be on line 2 */
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  ASSERT_U32_EQ(r.tokens[1].column, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_newline_crlf(void) {
  setup();
  LexResult r = lexer_lex("\r\n", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* single NEWLINE + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[0].length, 2); /* \r\n is 2 bytes */
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_consecutive_newlines(void) {
  setup();
  LexResult r = lexer_lex("\n\n\n", &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* 3 NEWLINEs + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_NEWLINE);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_NEWLINE);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_NEWLINE);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);

  /* lines */
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  ASSERT_U32_EQ(r.tokens[2].line, 3);
  ASSERT_U32_EQ(r.tokens[3].line, 4);

  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_delimiters_with_newlines(void) {
  setup();
  LexResult r = lexer_lex("[\n]\n{}", &test_arena);
  /* [ NL ] NL { } EOF = 7 */
  ASSERT_U32_EQ(r.count, 7);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[1].line, 1);
  ASSERT_U32_EQ(r.tokens[1].column, 2);

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_RBRACKET);
  ASSERT_U32_EQ(r.tokens[2].line, 2);
  ASSERT_U32_EQ(r.tokens[2].column, 1);

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[3].line, 2);
  ASSERT_U32_EQ(r.tokens[3].column, 2);

  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_LBRACE);
  ASSERT_U32_EQ(r.tokens[4].line, 3);
  ASSERT_U32_EQ(r.tokens[4].column, 1);

  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_RBRACE);
  ASSERT_U32_EQ(r.tokens[5].line, 3);
  ASSERT_U32_EQ(r.tokens[5].column, 2);

  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_EOF);

  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_only_whitespace(void) {
  setup();
  LexResult r = lexer_lex("   \t  \t  ", &test_arena);
  ASSERT_U32_EQ(r.count, 1); /* just EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 10);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_mixed_crlf_lf(void) {
  setup();
  LexResult r = lexer_lex("\r\n\n\r\n", &test_arena);
  /* 3 NEWLINEs + EOF */
  ASSERT_U32_EQ(r.count, 4);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[0].length, 2); /* \r\n */
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[1].length, 1); /* \n */
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[2].length, 2); /* \r\n */
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.tokens[3].line, 4);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_eof_position_after_content(void) {
  setup();
  LexResult r = lexer_lex("[]()", &test_arena);
  ASSERT_U32_EQ(r.count, 5);
  Token eof = r.tokens[4];
  ASSERT_INT_EQ(eof.type, TOKEN_EOF);
  ASSERT_U32_EQ(eof.offset, 4);
  ASSERT_U32_EQ(eof.line, 1);
  ASSERT_U32_EQ(eof.column, 5);
  ASSERT_U32_EQ(eof.length, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bare_cr(void) {
  setup();
  /* bare \r (not followed by \n) should still be a newline */
  LexResult r = lexer_lex("\r", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-003 helpers ---- */

static int token_text_eq(Token tok, const char* expected) {
  size_t expected_len = strlen(expected);
  return tok.length == (uint32_t)expected_len &&
         memcmp(tok.payload.text, expected, expected_len) == 0;
}

/* ---- US-003 tests ---- */

static int test_simple_word(void) {
  setup();
  LexResult r = lexer_lex("hello", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* WORD + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "hello"));
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 5);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multiple_words(void) {
  setup();
  LexResult r = lexer_lex("hello world", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* WORD WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "hello"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "world"));
  ASSERT_U32_EQ(r.tokens[1].column, 7);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_word_with_digits_and_hyphens(void) {
  setup();
  LexResult r = lexer_lex("foo42 my-thing foo_bar", &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* 3 WORDs + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "foo42"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "my-thing"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "foo_bar"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_utf8_word(void) {
  setup();
  /* "café" = 63 61 66 c3 a9 — 5 bytes */
  LexResult r = lexer_lex("caf\xC3\xA9", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* WORD + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT_U32_EQ(r.tokens[0].length, 5);
  ASSERT(token_text_eq(r.tokens[0], "caf\xC3\xA9"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_simple_operator(void) {
  setup();
  LexResult r = lexer_lex("+", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* OPERATOR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[0], "+"));
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multi_char_operator(void) {
  setup();
  LexResult r = lexer_lex(">= !=", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* OPERATOR OPERATOR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[0], ">="));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[1], "!="));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_comment_skipped(void) {
  setup();
  LexResult r = lexer_lex("# this is a comment", &test_arena);
  ASSERT_U32_EQ(r.count, 1); /* just EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_comment_before_newline(void) {
  setup();
  LexResult r = lexer_lex("# comment\nworld", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* NEWLINE WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "world"));
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_comment_after_tokens(void) {
  setup();
  LexResult r = lexer_lex("hello # comment", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "hello"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_word_terminates_at_delimiters(void) {
  setup();
  LexResult r = lexer_lex("foo[bar]baz", &test_arena);
  ASSERT_U32_EQ(r.count, 6); /* WORD [ WORD ] WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "foo"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "bar"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[4], "baz"));
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_word_terminates_at_hash(void) {
  setup();
  LexResult r = lexer_lex("foo#comment", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* WORD EOF (comment consumed) */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "foo"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_text_points_into_source(void) {
  setup();
  const char* src = "hello world >=";
  LexResult r = lexer_lex(src, &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* WORD WORD OPERATOR EOF */
  /* Verify payload.text points into the source buffer */
  ASSERT_PTR_EQ(r.tokens[0].payload.text, src + 0);
  ASSERT_PTR_EQ(r.tokens[1].payload.text, src + 6);
  ASSERT_PTR_EQ(r.tokens[2].payload.text, src + 12);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_mixed_sequence(void) {
  setup();
  /* A realistic JACL fragment */
  LexResult r = lexer_lex("[map key1 val1 key2 val2]", &test_arena);
  /* [ WORD WORD WORD WORD WORD ] EOF = 8 */
  ASSERT_U32_EQ(r.count, 8);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "map"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "key1"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[3], "val1"));
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[4], "key2"));
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[5], "val2"));
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[7].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multiline_with_comments(void) {
  setup();
  LexResult r = lexer_lex("print hello # greet\n[+ foo bar]", &test_arena);
  /* WORD WORD NEWLINE [ OPERATOR WORD WORD ] EOF = 9 */
  ASSERT_U32_EQ(r.count, 9);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "print"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "hello"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_NEWLINE);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_LBRACKET);
  ASSERT_U32_EQ(r.tokens[3].line, 2);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[4], "+"));
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[5], "foo"));
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[6], "bar"));
  ASSERT_INT_EQ(r.tokens[7].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[8].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-004 helpers ---- */

static int float_approx_eq(float a, float b) {
  float diff = a - b;
  if (diff < 0) diff = -diff;
  return diff < 0.0001f;
}

/* ---- US-004 tests ---- */

static int test_int_zero(void) {
  setup();
  LexResult r = lexer_lex("0", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* INT + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 0);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_int_positive(void) {
  setup();
  LexResult r = lexer_lex("42", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 42);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_float_basic(void) {
  setup();
  LexResult r = lexer_lex("3.14", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_FLOAT);
  ASSERT(float_approx_eq(r.tokens[0].payload.float_val, 3.14f));
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_float_leading_zero(void) {
  setup();
  LexResult r = lexer_lex("0.5", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_FLOAT);
  ASSERT(float_approx_eq(r.tokens[0].payload.float_val, 0.5f));
  ASSERT_U32_EQ(r.tokens[0].length, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hex_literal(void) {
  setup();
  LexResult r = lexer_lex("0xFF", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 255);
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hex_uppercase(void) {
  setup();
  LexResult r = lexer_lex("0X1A", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 26);
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_binary_literal(void) {
  setup();
  LexResult r = lexer_lex("0b1010", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 10);
  ASSERT_U32_EQ(r.tokens[0].length, 6);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_binary_zero(void) {
  setup();
  LexResult r = lexer_lex("0b0", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_invalid_suffix_on_int(void) {
  setup();
  LexResult r = lexer_lex("42abc", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 5);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_invalid_suffix_on_float(void) {
  setup();
  LexResult r = lexer_lex("3.14abc", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 7);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_invalid_suffix_on_hex(void) {
  setup();
  LexResult r = lexer_lex("0xFFgg", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_int32_max(void) {
  setup();
  LexResult r = lexer_lex("2147483647", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 2147483647);
  ASSERT_U32_EQ(r.tokens[0].length, 10);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* M12 US-001: integer overflow detection */

static int test_decimal_overflow(void) {
  setup();
  LexResult r = lexer_lex("2147483648", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT(strstr(r.tokens[0].payload.error_msg, "i32 range") != NULL);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hex_overflow_0xFFFFFFFF(void) {
  setup();
  LexResult r = lexer_lex("0xFFFFFFFF", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT(strstr(r.tokens[0].payload.error_msg, "i32 range") != NULL);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hex_overflow_0x80000000(void) {
  setup();
  LexResult r = lexer_lex("0x80000000", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT(strstr(r.tokens[0].payload.error_msg, "i32 range") != NULL);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_hex_max_valid(void) {
  setup();
  LexResult r = lexer_lex("0x7FFFFFFF", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 2147483647);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_binary_overflow_33bits(void) {
  setup();
  /* 33 ones = exceeds i32 */
  LexResult r = lexer_lex("0b111111111111111111111111111111111", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT(strstr(r.tokens[0].payload.error_msg, "i32 range") != NULL);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_binary_max_valid(void) {
  setup();
  /* 0x7FFFFFFF in binary = 31 ones */
  LexResult r = lexer_lex("0b1111111111111111111111111111111", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 2147483647);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_overflow_error_has_span(void) {
  setup();
  LexResult r = lexer_lex("2147483648", &test_arena);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 10);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_very_small_float(void) {
  setup();
  LexResult r = lexer_lex("0.001", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_FLOAT);
  ASSERT(float_approx_eq(r.tokens[0].payload.float_val, 0.001f));
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_numbers_in_expression(void) {
  setup();
  LexResult r = lexer_lex("[+ 1 2]", &test_arena);
  /* [ OPERATOR INT INT ] EOF = 6 */
  ASSERT_U32_EQ(r.count, 6);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[1], "+"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[2].payload.int_val, 1);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[3].payload.int_val, 2);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_multiple_numbers(void) {
  setup();
  LexResult r = lexer_lex("42 3.14 0xFF", &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* INT FLOAT INT EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[0].payload.int_val, 42);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_FLOAT);
  ASSERT(float_approx_eq(r.tokens[1].payload.float_val, 3.14f));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[2].payload.int_val, 255);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-005 helpers ---- */

static int var_text_eq(Token tok, const char* expected) {
  /* VAR payload.text points past '$', so length of identifier = token.length - 1 */
  size_t expected_len = strlen(expected);
  return (tok.length - 1) == (uint32_t)expected_len &&
         memcmp(tok.payload.text, expected, expected_len) == 0;
}

/* ---- US-005 tests ---- */

static int test_var_simple_single_char(void) {
  setup();
  LexResult r = lexer_lex("$x", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* VAR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[0], "x"));
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_simple_multichar(void) {
  setup();
  LexResult r = lexer_lex("$foo", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* VAR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[0], "foo"));
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_with_hyphen(void) {
  setup();
  LexResult r = lexer_lex("$my-var", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* VAR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[0], "my-var"));
  ASSERT_U32_EQ(r.tokens[0].length, 7);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_with_underscore(void) {
  setup();
  LexResult r = lexer_lex("$foo_bar", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* VAR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[0], "foo_bar"));
  ASSERT_U32_EQ(r.tokens[0].length, 8);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_with_digits(void) {
  setup();
  LexResult r = lexer_lex("$x1", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[0], "x1"));
  ASSERT_U32_EQ(r.tokens[0].length, 3);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_payload_points_into_source(void) {
  setup();
  const char* src = "$foo";
  LexResult r = lexer_lex(src, &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  /* payload.text should point past the '$' into source */
  ASSERT_PTR_EQ(r.tokens[0].payload.text, src + 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_dollar_bracket(void) {
  setup();
  LexResult r = lexer_lex("$[", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* DOLLAR_BRACKET + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DOLLAR_BRACKET);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_dollar_bracket_with_expr(void) {
  setup();
  LexResult r = lexer_lex("$[+ 1 2]", &test_arena);
  /* DOLLAR_BRACKET OPERATOR INT INT RBRACKET EOF = 6 */
  ASSERT_U32_EQ(r.count, 6);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DOLLAR_BRACKET);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[1], "+"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[2].payload.int_val, 1);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[3].payload.int_val, 2);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_dollar_at_eof(void) {
  setup();
  LexResult r = lexer_lex("$", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_dollar_digit(void) {
  setup();
  LexResult r = lexer_lex("$123", &test_arena);
  /* ERROR + INT + EOF = 3 ($ is error, 123 is separate number) */
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[1].payload.int_val, 123);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_dollar_bang(void) {
  setup();
  LexResult r = lexer_lex("$!", &test_arena);
  /* ERROR + TOKEN_BANG + EOF = 3 ($ is error, ! is TOKEN_BANG) */
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_BANG);
  ASSERT_INT_EQ(r.tokens[1].length, 1);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_terminates_at_delimiter(void) {
  setup();
  LexResult r = lexer_lex("$foo[bar]", &test_arena);
  /* VAR LBRACKET WORD RBRACKET EOF = 5 */
  ASSERT_U32_EQ(r.count, 5);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[0], "foo"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "bar"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_in_expression(void) {
  setup();
  LexResult r = lexer_lex("[+ $x $y]", &test_arena);
  /* [ OPERATOR VAR VAR ] EOF = 6 */
  ASSERT_U32_EQ(r.count, 6);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[1], "+"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[2], "x"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[3], "y"));
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_var_underscore_start(void) {
  setup();
  LexResult r = lexer_lex("$_private", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[0], "_private"));
  ASSERT_U32_EQ(r.tokens[0].length, 9);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-006 tests ---- */

static int test_string_empty(void) {
  setup();
  LexResult r = lexer_lex("\"\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* STRING + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "");
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 2); /* both quotes */
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_simple(void) {
  setup();
  LexResult r = lexer_lex("\"hello world\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* STRING + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "hello world");
  ASSERT_U32_EQ(r.tokens[0].length, 13); /* "hello world" = 13 bytes */
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_escape_backslash(void) {
  setup();
  /* JACL: "a\\b" */
  LexResult r = lexer_lex("\"a\\\\b\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "a\\b");
  ASSERT_U32_EQ(r.tokens[0].length, 6); /* "a\\b" = 6 source bytes */
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_escape_quote(void) {
  setup();
  /* JACL: "a\"b" */
  LexResult r = lexer_lex("\"a\\\"b\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "a\"b");
  ASSERT_U32_EQ(r.tokens[0].length, 6); /* "a\"b" = 6 source bytes */
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_escape_n_t_r(void) {
  setup();
  /* JACL: "\n\t\r" */
  LexResult r = lexer_lex("\"\\n\\t\\r\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "\n\t\r");
  ASSERT_U32_EQ(r.tokens[0].length, 8); /* "\n\t\r" = 8 source bytes */
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_escape_null(void) {
  setup();
  /* JACL: "a\0b" — contains embedded null byte */
  LexResult r = lexer_lex("\"a\\0b\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  /* Check bytes directly since null byte truncates strcmp */
  ASSERT(r.tokens[0].payload.text[0] == 'a');
  ASSERT(r.tokens[0].payload.text[1] == '\0');
  ASSERT(r.tokens[0].payload.text[2] == 'b');
  ASSERT_U32_EQ(r.tokens[0].length, 6); /* "a\0b" = 6 source bytes */
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_hex_escape(void) {
  setup();
  /* JACL: "\x41\x42\x43" → "ABC" */
  LexResult r = lexer_lex("\"\\x41\\x42\\x43\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "ABC");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_unicode_4digit(void) {
  setup();
  /* JACL: "\u0041" → "A" (U+0041 = 1-byte UTF-8) */
  LexResult r = lexer_lex("\"\\u0041\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "A");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_unicode_2byte(void) {
  setup();
  /* JACL: "\u00E9" → "é" (U+00E9 = 2-byte UTF-8: C3 A9) */
  LexResult r = lexer_lex("\"\\u00E9\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "\xC3\xA9");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_unicode_3byte(void) {
  setup();
  /* JACL: "\u4E16" → "世" (U+4E16 = 3-byte UTF-8: E4 B8 96) */
  LexResult r = lexer_lex("\"\\u4E16\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "\xE4\xB8\x96");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_unicode_8digit(void) {
  setup();
  /* JACL: "\U0001F600" → 😀 (U+1F600 = 4-byte UTF-8: F0 9F 98 80) */
  LexResult r = lexer_lex("\"\\U0001F600\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "\xF0\x9F\x98\x80");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_multiline(void) {
  setup();
  /* JACL: "hello\nworld" with actual embedded newline */
  LexResult r = lexer_lex("\"hello\nworld\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "hello\nworld");
  /* Token starts on line 1 */
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  /* EOF is on line 2 (after the embedded newline) */
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_unterminated(void) {
  setup();
  LexResult r = lexer_lex("\"hello", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_invalid_escape(void) {
  setup();
  /* JACL: "hello\q" — invalid escape, should error then continue */
  LexResult r = lexer_lex("\"hello\\q\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 9); /* entire "hello\q" */
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_invalid_escape_recovery(void) {
  setup();
  /* After invalid escape, lexing continues past the string */
  LexResult r = lexer_lex("\"bad\\q\" hello", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* ERROR WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "hello"));
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_mixed_escapes(void) {
  setup();
  /* JACL: "tab:\there\nnewline" */
  LexResult r = lexer_lex("\"tab:\\there\\nnewline\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "tab:\there\nnewline");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_in_expression(void) {
  setup();
  LexResult r = lexer_lex("[print \"hello\"]", &test_arena);
  /* [ WORD STRING ] EOF = 5 */
  ASSERT_U32_EQ(r.count, 5);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "print"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[2].payload.text, "hello");
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_position_tracking(void) {
  setup();
  /* Token after a multi-line string should have correct position */
  LexResult r = lexer_lex("\"line1\nline2\" foo", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* STRING WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  /* "foo" is on line 2 after the closing quote */
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "foo"));
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_string_backslash_at_eof(void) {
  setup();
  /* JACL: "hello\ — backslash at end of input */
  LexResult r = lexer_lex("\"hello\\", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-007 tests ---- */

static int test_interp_var_simple(void) {
  setup();
  /* "hello $name" → STRING_BEGIN INTERP_VAR STRING_END EOF */
  LexResult r = lexer_lex("\"hello $name\"", &test_arena);
  ASSERT_U32_EQ(r.count, 4);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "hello ");
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 7); /* "hello  (quote + 6 chars) */

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[1], "name"));
  ASSERT_U32_EQ(r.tokens[1].offset, 7);
  ASSERT_U32_EQ(r.tokens[1].length, 5); /* $name */

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[2].payload.text, "");
  ASSERT_U32_EQ(r.tokens[2].offset, 12);
  ASSERT_U32_EQ(r.tokens[2].length, 1); /* closing " */

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_expr_simple(void) {
  setup();
  /* "val: $[+ 1 2]" → STRING_BEGIN INTERP_EXPR_START OP INT INT INTERP_EXPR_END STRING_END EOF */
  LexResult r = lexer_lex("\"val: $[+ 1 2]\"", &test_arena);
  ASSERT_U32_EQ(r.count, 8);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "val: ");
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 6); /* "val:  */

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_EXPR_START);
  ASSERT_U32_EQ(r.tokens[1].offset, 6);
  ASSERT_U32_EQ(r.tokens[1].length, 2); /* $[ */

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[2], "+"));

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[3].payload.int_val, 1);

  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[4].payload.int_val, 2);

  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_INTERP_EXPR_END);
  ASSERT_U32_EQ(r.tokens[5].offset, 13);
  ASSERT_U32_EQ(r.tokens[5].length, 1);

  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[6].payload.text, "");
  ASSERT_U32_EQ(r.tokens[6].offset, 14);
  ASSERT_U32_EQ(r.tokens[6].length, 1);

  ASSERT_INT_EQ(r.tokens[7].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_multiple(void) {
  setup();
  /* "$a and $b" → STRING_BEGIN INTERP_VAR STRING_PART INTERP_VAR STRING_END EOF */
  LexResult r = lexer_lex("\"$a and $b\"", &test_arena);
  ASSERT_U32_EQ(r.count, 6);

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "");
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 1); /* just " */

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[1], "a"));
  ASSERT_U32_EQ(r.tokens[1].offset, 1);
  ASSERT_U32_EQ(r.tokens[1].length, 2);

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING_PART);
  ASSERT_STR_EQ(r.tokens[2].payload.text, " and ");
  ASSERT_U32_EQ(r.tokens[2].offset, 3);
  ASSERT_U32_EQ(r.tokens[2].length, 5);

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[3], "b"));
  ASSERT_U32_EQ(r.tokens[3].offset, 8);
  ASSERT_U32_EQ(r.tokens[3].length, 2);

  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[4].payload.text, "");
  ASSERT_U32_EQ(r.tokens[4].offset, 10);
  ASSERT_U32_EQ(r.tokens[4].length, 1);

  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_nested_brackets(void) {
  setup();
  /* "$[get [list]]" → STRING_BEGIN INTERP_EXPR_START WORD [ WORD ] INTERP_EXPR_END STRING_END EOF */
  LexResult r = lexer_lex("\"$[get [list]]\"", &test_arena);
  ASSERT_U32_EQ(r.count, 9);

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "");

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_EXPR_START);
  ASSERT_U32_EQ(r.tokens[1].offset, 1);
  ASSERT_U32_EQ(r.tokens[1].length, 2);

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "get"));

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_LBRACKET);

  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[4], "list"));

  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_RBRACKET);

  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_INTERP_EXPR_END);
  ASSERT_U32_EQ(r.tokens[6].offset, 13);

  ASSERT_INT_EQ(r.tokens[7].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[7].payload.text, "");

  ASSERT_INT_EQ(r.tokens[8].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_no_interp_still_string(void) {
  setup();
  /* String with no interpolation → single STRING token (not BEGIN/END) */
  LexResult r = lexer_lex("\"just text\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "just text");
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_mixed_escapes(void) {
  setup();
  /* "tab:\t$x\n" → STRING_BEGIN("tab:\t") INTERP_VAR(x) STRING_END("\n") */
  LexResult r = lexer_lex("\"tab:\\t$x\\n\"", &test_arena);
  ASSERT_U32_EQ(r.count, 4);

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "tab:\t");

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[1], "x"));

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[2].payload.text, "\n");

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_adjacent(void) {
  setup();
  /* "$a$b" → STRING_BEGIN("") INTERP_VAR(a) STRING_PART("") INTERP_VAR(b) STRING_END("") */
  LexResult r = lexer_lex("\"$a$b\"", &test_arena);
  ASSERT_U32_EQ(r.count, 6);

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "");

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[1], "a"));

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING_PART);
  ASSERT_STR_EQ(r.tokens[2].payload.text, "");
  ASSERT_U32_EQ(r.tokens[2].length, 0); /* empty segment */

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[3], "b"));

  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[4].payload.text, "");

  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_at_start(void) {
  setup();
  /* "$x end" → STRING_BEGIN("") INTERP_VAR(x) STRING_END(" end") */
  LexResult r = lexer_lex("\"$x end\"", &test_arena);
  ASSERT_U32_EQ(r.count, 4);

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "");
  ASSERT_U32_EQ(r.tokens[0].length, 1); /* just " */

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[1], "x"));

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[2].payload.text, " end");
  ASSERT_U32_EQ(r.tokens[2].offset, 3);

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_at_end(void) {
  setup();
  /* "start $x" → STRING_BEGIN("start ") INTERP_VAR(x) STRING_END("") */
  LexResult r = lexer_lex("\"start $x\"", &test_arena);
  ASSERT_U32_EQ(r.count, 4);

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "start ");

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[1], "x"));

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[2].payload.text, "");

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_dollar_not_var(void) {
  setup();
  /* "$5" is not interpolation — $ followed by digit → just a regular string */
  LexResult r = lexer_lex("\"price $5\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "price $5");
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_dollar_at_end_of_string(void) {
  setup();
  /* "hello $" — $ followed by closing " → not interpolation */
  LexResult r = lexer_lex("\"hello $\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "hello $");
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_expr_with_string(void) {
  setup();
  /* "$[concat "a" "b"]" → STRING_BEGIN INTERP_EXPR_START WORD STRING STRING INTERP_EXPR_END STRING_END */
  LexResult r = lexer_lex("\"$[concat \"a\" \"b\"]\"", &test_arena);
  ASSERT_U32_EQ(r.count, 8);

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_INTERP_EXPR_START);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "concat"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[3].payload.text, "a");
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[4].payload.text, "b");
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_INTERP_EXPR_END);
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_STRING_END);
  ASSERT_INT_EQ(r.tokens[7].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_interp_in_context(void) {
  setup();
  /* Interpolated string used in a command: [print "hello $name"] */
  LexResult r = lexer_lex("[print \"hello $name\"]", &test_arena);
  /* [ WORD STRING_BEGIN INTERP_VAR STRING_END ] EOF = 7 */
  ASSERT_U32_EQ(r.count, 7);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "print"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_STRING_BEGIN);
  ASSERT_STR_EQ(r.tokens[2].payload.text, "hello ");
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_INTERP_VAR);
  ASSERT(var_text_eq(r.tokens[3], "name"));
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_STRING_END);
  ASSERT_STR_EQ(r.tokens[4].payload.text, "");
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_RBRACKET);
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-008 tests ---- */

static int test_error_control_char(void) {
  setup();
  /* Stray control character should produce ERROR with descriptive message */
  LexResult r = lexer_lex("\x01", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* ERROR + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  /* Error message is arena-allocated with hex representation */
  ASSERT_STR_EQ(r.tokens[0].payload.error_msg, "unexpected character (0x01)");
  ASSERT_U32_EQ(r.error_count, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_recovery_continues(void) {
  setup();
  /* After error token, valid tokens are still lexed correctly */
  LexResult r = lexer_lex("\x01 hello 42", &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* ERROR WORD INT EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "hello"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[2].payload.int_val, 42);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 1);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_positions_after_errors(void) {
  setup();
  /* Multiple errors across lines — position tracking stays correct */
  const char* src = "\x01\nhello\n\x02\nworld";
  LexResult r = lexer_lex(src, &test_arena);
  ASSERT_U32_EQ(r.count, 8); /* ERROR NL WORD NL ERROR NL WORD EOF */

  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);

  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[1].line, 1);

  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "hello"));
  ASSERT_U32_EQ(r.tokens[2].line, 2);
  ASSERT_U32_EQ(r.tokens[2].column, 1);

  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_NEWLINE);
  ASSERT_U32_EQ(r.tokens[3].line, 2);

  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[4].line, 3);
  ASSERT_U32_EQ(r.tokens[4].column, 1);
  ASSERT_STR_EQ(r.tokens[4].payload.error_msg, "unexpected character (0x02)");

  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_NEWLINE);

  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[6], "world"));
  ASSERT_U32_EQ(r.tokens[6].line, 4);
  ASSERT_U32_EQ(r.tokens[6].column, 1);

  ASSERT_INT_EQ(r.tokens[7].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 2);

  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_count_reflects_total(void) {
  setup();
  /* Multiple different error types: control char, bad $, number suffix, DEL, bad escape */
  const char* src = "\x01 $ 42abc \x7F \"bad\\q\"";
  LexResult r = lexer_lex(src, &test_arena);
  ASSERT_U32_EQ(r.count, 6); /* 5 errors + EOF */
  ASSERT_U32_EQ(r.error_count, 5);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR); /* \x01 */
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_ERROR); /* $ */
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_ERROR); /* 42abc */
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_ERROR); /* \x7F */
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_ERROR); /* "bad\q" */
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_error_mixed_program(void) {
  setup();
  /* Multi-line program mixing valid and invalid constructs */
  const char* src =
    "set x 42\n"
    "\x01\n"
    "set y $\n"
    "set z 99abc\n"
    "[print $x]\n";

  LexResult r = lexer_lex(src, &test_arena);

  TokenType expected[] = {
    TOKEN_SET, TOKEN_WORD, TOKEN_INT, TOKEN_NEWLINE,
    TOKEN_ERROR, TOKEN_NEWLINE,
    TOKEN_SET, TOKEN_WORD, TOKEN_ERROR, TOKEN_NEWLINE,
    TOKEN_SET, TOKEN_WORD, TOKEN_ERROR, TOKEN_NEWLINE,
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_VAR, TOKEN_RBRACKET, TOKEN_NEWLINE,
    TOKEN_EOF
  };

  uint32_t expected_count = sizeof(expected) / sizeof(expected[0]);
  ASSERT_U32_EQ(r.count, expected_count);
  {
    uint32_t i;
    for (i = 0; i < expected_count && i < r.count; i++) {
      if ((int)r.tokens[i].type != (int)expected[i]) {
        fprintf(stderr, "FAIL: token[%u] type (Line %d)\n"
                "  Actual: %d\n  Expected: %d\n",
                i, __LINE__, r.tokens[i].type, expected[i]);
        return 0;
      }
    }
  }

  ASSERT_U32_EQ(r.error_count, 3);
  /* Position check: [print $x] starts on line 5 */
  ASSERT_U32_EQ(r.tokens[14].line, 5);
  ASSERT_U32_EQ(r.tokens[14].column, 1);

  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_integration_full_program(void) {
  setup();
  /* A realistic 58-line JACL program exercising all token types */
  const char* src =
    "# JACL Integration Test\n"
    "# Exercises all token types\n"
    "\n"
    "import http\n"
    "import json\n"
    "\n"
    "# Variables and literals\n"
    "set name \"world\"\n"
    "set count 42\n"
    "set ratio 3.14\n"
    "set mask 0xFF\n"
    "set flags 0b1010\n"
    "\n"
    "# Maps\n"
    "[map host \"localhost\" port 8080]\n"
    "\n"
    "# Variable references and operators\n"
    "set x $name\n"
    "[+ $count 1]\n"
    "[>= $x $name]\n"
    "\n"
    "# Dollar-bracket expressions\n"
    "set result $[+ 1 2]\n"
    "\n"
    "# String interpolation - variable\n"
    "[print \"Hello $name!\"]\n"
    "\n"
    "# String interpolation - expression\n"
    "[print \"Sum: $[+ 1 2]\"]\n"
    "\n"
    "# Multi-line expression\n"
    "[if [> $count 0]\n"
    "  [print \"positive\"]\n"
    "  [print \"negative\"]\n"
    "]\n"
    "\n"
    "# Nested braces and parens\n"
    "[define greet {person} (\n"
    "  set msg \"Hi $person\"\n"
    "  [print $msg]\n"
    ")]\n"
    "\n"
    "# Complex interpolation\n"
    "set report \"Found $count at $[now]\"\n"
    "\n"
    "# Mixed escapes in strings\n"
    "set path \"C:\\\\dir\\\\file\"\n"
    "set tabs \"a\\tb\\tc\"\n"
    "\n"
    "# All delimiter types\n"
    "[foo {bar} (baz)]\n"
    "\n"
    "# Multi-line string\n"
    "set multi \"line1\\nline2\\nline3\"\n"
    "[print $multi]\n"
    "\n"
    "# Final line\n"
    "[exit 0]\n";

  LexResult r = lexer_lex(src, &test_arena);

  /* Verify every token type in the program (190 tokens) */
  TokenType expected[] = {
    /* line 1-3: comments and blank */
    TOKEN_NEWLINE, TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 4-5: imports */
    TOKEN_WORD, TOKEN_WORD, TOKEN_NEWLINE,
    TOKEN_WORD, TOKEN_WORD, TOKEN_NEWLINE,
    /* line 6-7: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 8: set name "world" */
    TOKEN_SET, TOKEN_WORD, TOKEN_STRING, TOKEN_NEWLINE,
    /* line 9: set count 42 */
    TOKEN_SET, TOKEN_WORD, TOKEN_INT, TOKEN_NEWLINE,
    /* line 10: set ratio 3.14 */
    TOKEN_SET, TOKEN_WORD, TOKEN_FLOAT, TOKEN_NEWLINE,
    /* line 11: set mask 0xFF */
    TOKEN_SET, TOKEN_WORD, TOKEN_INT, TOKEN_NEWLINE,
    /* line 12: set flags 0b1010 */
    TOKEN_SET, TOKEN_WORD, TOKEN_INT, TOKEN_NEWLINE,
    /* line 13-14: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 15: [map host "localhost" port 8080] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_WORD, TOKEN_STRING,
    TOKEN_WORD, TOKEN_INT, TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 16-17: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 18: set x $name */
    TOKEN_SET, TOKEN_WORD, TOKEN_VAR, TOKEN_NEWLINE,
    /* line 19: [+ $count 1] */
    TOKEN_LBRACKET, TOKEN_OPERATOR, TOKEN_VAR, TOKEN_INT,
    TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 20: [>= $x $name] */
    TOKEN_LBRACKET, TOKEN_OPERATOR, TOKEN_VAR, TOKEN_VAR,
    TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 21-22: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 23: set result $[+ 1 2] */
    TOKEN_SET, TOKEN_WORD, TOKEN_DOLLAR_BRACKET, TOKEN_OPERATOR,
    TOKEN_INT, TOKEN_INT, TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 24-25: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 26: [print "Hello $name!"] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_STRING_BEGIN, TOKEN_INTERP_VAR,
    TOKEN_STRING_END, TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 27-28: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 29: [print "Sum: $[+ 1 2]"] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_STRING_BEGIN,
    TOKEN_INTERP_EXPR_START, TOKEN_OPERATOR, TOKEN_INT, TOKEN_INT,
    TOKEN_INTERP_EXPR_END, TOKEN_STRING_END, TOKEN_RBRACKET,
    TOKEN_NEWLINE,
    /* line 30-31: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 32: [if [> $count 0] */
    TOKEN_LBRACKET, TOKEN_IF, TOKEN_LBRACKET, TOKEN_OPERATOR,
    TOKEN_VAR, TOKEN_INT, TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 33: [print "positive"] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_STRING, TOKEN_RBRACKET,
    TOKEN_NEWLINE,
    /* line 34: [print "negative"] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_STRING, TOKEN_RBRACKET,
    TOKEN_NEWLINE,
    /* line 35: ] */
    TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 36-37: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 38: [define greet {person} ( */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_WORD, TOKEN_LBRACE, TOKEN_WORD,
    TOKEN_RBRACE, TOKEN_LPAREN, TOKEN_NEWLINE,
    /* line 39: set msg "Hi $person" */
    TOKEN_SET, TOKEN_WORD, TOKEN_STRING_BEGIN, TOKEN_INTERP_VAR,
    TOKEN_STRING_END, TOKEN_NEWLINE,
    /* line 40: [print $msg] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_VAR, TOKEN_RBRACKET,
    TOKEN_NEWLINE,
    /* line 41: )] */
    TOKEN_RPAREN, TOKEN_RBRACKET, TOKEN_NEWLINE,
    /* line 42-43: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 44: set report "Found $count at $[now]" */
    TOKEN_SET, TOKEN_WORD, TOKEN_STRING_BEGIN, TOKEN_INTERP_VAR,
    TOKEN_STRING_PART, TOKEN_INTERP_EXPR_START, TOKEN_WORD,
    TOKEN_INTERP_EXPR_END, TOKEN_STRING_END, TOKEN_NEWLINE,
    /* line 45-46: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 47: set path "C:\\dir\\file" */
    TOKEN_SET, TOKEN_WORD, TOKEN_STRING, TOKEN_NEWLINE,
    /* line 48: set tabs "a\tb\tc" */
    TOKEN_SET, TOKEN_WORD, TOKEN_STRING, TOKEN_NEWLINE,
    /* line 49-50: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 51: [foo {bar} (baz)] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_LBRACE, TOKEN_WORD, TOKEN_RBRACE,
    TOKEN_LPAREN, TOKEN_WORD, TOKEN_RPAREN, TOKEN_RBRACKET,
    TOKEN_NEWLINE,
    /* line 52-53: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 54: set multi "line1\nline2\nline3" */
    TOKEN_SET, TOKEN_WORD, TOKEN_STRING, TOKEN_NEWLINE,
    /* line 55: [print $multi] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_VAR, TOKEN_RBRACKET,
    TOKEN_NEWLINE,
    /* line 56-57: blank + comment */
    TOKEN_NEWLINE, TOKEN_NEWLINE,
    /* line 58: [exit 0] */
    TOKEN_LBRACKET, TOKEN_WORD, TOKEN_INT, TOKEN_RBRACKET,
    TOKEN_NEWLINE,
    /* EOF */
    TOKEN_EOF
  };

  uint32_t expected_count = sizeof(expected) / sizeof(expected[0]);
  ASSERT_U32_EQ(r.count, expected_count);
  {
    uint32_t i;
    for (i = 0; i < expected_count && i < r.count; i++) {
      if ((int)r.tokens[i].type != (int)expected[i]) {
        fprintf(stderr, "FAIL: token[%u] type (Line %d)\n"
                "  Actual: %d\n  Expected: %d\n",
                i, __LINE__, r.tokens[i].type, expected[i]);
        return 0;
      }
    }
  }

  ASSERT_U32_EQ(r.error_count, 0);

  /* Spot-check token content */
  ASSERT(token_text_eq(r.tokens[3], "import"));
  ASSERT_STR_EQ(r.tokens[13].payload.text, "world");
  ASSERT_INT_EQ(r.tokens[17].payload.int_val, 42);
  ASSERT(float_approx_eq(r.tokens[21].payload.float_val, 3.14f));
  ASSERT_INT_EQ(r.tokens[25].payload.int_val, 255);  /* 0xFF */
  ASSERT_INT_EQ(r.tokens[29].payload.int_val, 10);   /* 0b1010 */

  /* Position tracking at key points */
  ASSERT_U32_EQ(r.tokens[33].line, 15);   /* [map on line 15 */
  ASSERT_U32_EQ(r.tokens[33].column, 1);
  ASSERT_U32_EQ(r.tokens[184].line, 58);  /* [exit on line 58 */
  ASSERT_U32_EQ(r.tokens[184].column, 1);

  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- M6 US-001: Comma token ---- */

static int test_comma_separator(void) {
  setup();
  /* 'def x 1, def y 2' -> [DEF WORD INT COMMA DEF WORD INT EOF] */
  LexResult r = lexer_lex("def x 1, def y 2", &test_arena);
  ASSERT_U32_EQ(r.count, 8);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DEF);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_COMMA);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_DEF);
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_WORD);
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_INT);
  ASSERT_INT_EQ(r.tokens[7].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_comma_inside_string(void) {
  setup();
  /* '"a,b"' -> [STRING EOF] — comma inside string is not tokenized */
  LexResult r = lexer_lex("\"a,b\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "a,b");
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- M6 US-002 tests ---- */

static int test_backslash_newline_continuation(void) {
  setup();
  /* "puts \\\nhello" — backslash-newline joins lines */
  LexResult r = lexer_lex("puts \\\nhello", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* WORD WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "puts"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "hello"));
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  ASSERT_U32_EQ(r.tokens[1].column, 1);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_backslash_crlf_continuation(void) {
  setup();
  /* "puts \\\r\nhello" — backslash-CRLF joins lines */
  LexResult r = lexer_lex("puts \\\r\nhello", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* WORD WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[0], "puts"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "hello"));
  ASSERT_U32_EQ(r.tokens[1].line, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_backslash_not_continuation(void) {
  setup();
  /* "\\ " (backslash followed by space) — now TOKEN_BACKSLASH, not continuation */
  LexResult r = lexer_lex("\\ ", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* BACKSLASH EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BACKSLASH);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_backslash_string_unaffected(void) {
  setup();
  /* Backslash inside string is handled by string body, not continuation */
  LexResult r = lexer_lex("\"a\\nb\"", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* STRING EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[0].payload.text, "a\nb");
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-001: New operator tokens ---- */

static int test_token_pipe(void) {
  setup();
  LexResult r = lexer_lex("|", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_PIPE);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_or(void) {
  setup();
  LexResult r = lexer_lex("||", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_OR);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_amp(void) {
  setup();
  LexResult r = lexer_lex("&", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_AMP);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_and(void) {
  setup();
  LexResult r = lexer_lex("&&", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_AND);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_not(void) {
  setup();
  LexResult r = lexer_lex("~", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_NOT);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_equals(void) {
  setup();
  LexResult r = lexer_lex("=", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_EQUALS);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_eq_eq_stays_operator(void) {
  setup();
  /* == stays TOKEN_OPERATOR */
  LexResult r = lexer_lex("==", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_OPERATOR);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_colon(void) {
  setup();
  LexResult r = lexer_lex(":", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_COLON);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_double_colon(void) {
  setup();
  LexResult r = lexer_lex("::", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DOUBLE_COLON);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_bang(void) {
  setup();
  LexResult r = lexer_lex("!", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_neq_stays_operator(void) {
  setup();
  /* != stays TOKEN_OPERATOR */
  LexResult r = lexer_lex("!=", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_OPERATOR);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_arrow(void) {
  setup();
  LexResult r = lexer_lex("->", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ARROW);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_dotdot(void) {
  setup();
  LexResult r = lexer_lex("..", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DOTDOT);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_backslash(void) {
  setup();
  /* Backslash not followed by newline → TOKEN_BACKSLASH */
  LexResult r = lexer_lex("\\x", &test_arena);
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BACKSLASH);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_backslash_position(void) {
  setup();
  /* Position tracking for TOKEN_BACKSLASH */
  LexResult r = lexer_lex("foo \\", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* WORD BACKSLASH EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_BACKSLASH);
  ASSERT_U32_EQ(r.tokens[1].line, 1);
  ASSERT_U32_EQ(r.tokens[1].column, 5);
  ASSERT_U32_EQ(r.tokens[1].offset, 4);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_dollar_paren(void) {
  setup();
  /* $( outside a string → TOKEN_DOLLAR_PAREN */
  LexResult r = lexer_lex("$(", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DOLLAR_PAREN);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_token_dollar_paren_in_string(void) {
  setup();
  /* "$($x + 1)" — $( in string starts infix interpolation */
  LexResult r = lexer_lex("\"$($x + 1)\"", &test_arena);
  /* STRING_BEGIN DOLLAR_PAREN VAR OPERATOR INT RPAREN STRING_END EOF */
  ASSERT(r.count >= 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRING_BEGIN);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_DOLLAR_PAREN);
  ASSERT_U32_EQ(r.tokens[1].length, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-001: New keyword tokens ---- */

static int test_keyword_proc(void) {
  setup();
  LexResult r = lexer_lex("proc", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_PROC);
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_if(void) {
  setup();
  LexResult r = lexer_lex("if", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_IF);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_elif(void) {
  setup();
  LexResult r = lexer_lex("elif", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ELIF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_else(void) {
  setup();
  LexResult r = lexer_lex("else", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ELSE);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_while(void) {
  setup();
  LexResult r = lexer_lex("while", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WHILE);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_for(void) {
  setup();
  LexResult r = lexer_lex("for", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_FOR);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_def(void) {
  setup();
  LexResult r = lexer_lex("def", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DEF);
  ASSERT_U32_EQ(r.tokens[0].length, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_mut(void) {
  setup();
  LexResult r = lexer_lex("mut", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_MUT);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_set(void) {
  setup();
  LexResult r = lexer_lex("set", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_SET);
  ASSERT_U32_EQ(r.tokens[0].length, 3);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_match(void) {
  setup();
  LexResult r = lexer_lex("match", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_MATCH);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_return(void) {
  setup();
  LexResult r = lexer_lex("return", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_RETURN);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_break(void) {
  setup();
  LexResult r = lexer_lex("break", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BREAK);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_continue(void) {
  setup();
  LexResult r = lexer_lex("continue", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_CONTINUE);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_try(void) {
  setup();
  LexResult r = lexer_lex("try", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_TRY);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_struct(void) {
  setup();
  LexResult r = lexer_lex("struct", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_STRUCT);
  ASSERT_U32_EQ(r.tokens[0].length, 6);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_defstruct_removed(void) {
  setup();
  /* defstruct is no longer a keyword — should lex as TOKEN_WORD */
  LexResult r = lexer_lex("defstruct", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT_U32_EQ(r.tokens[0].length, 9);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keywords_have_correct_positions(void) {
  setup();
  /* "proc foo if" — verify positions are correct */
  LexResult r = lexer_lex("proc foo if", &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* PROC WORD IF EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_PROC);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT_U32_EQ(r.tokens[1].column, 6);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_IF);
  ASSERT_U32_EQ(r.tokens[2].column, 10);
  ASSERT_U32_EQ(r.tokens[2].length, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_prefix_not_keyword(void) {
  setup();
  /* "proca" is not TOKEN_PROC — only exact match */
  LexResult r = lexer_lex("proca", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT_U32_EQ(r.tokens[0].length, 5);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_with_hyphen_not_keyword(void) {
  setup();
  /* "set-foo" is not TOKEN_SET — hyphens extend the word */
  LexResult r = lexer_lex("set-foo", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT_U32_EQ(r.tokens[0].length, 7);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_operator_tokens_positions(void) {
  setup();
  /* "| & ->" — test positions of new operator tokens */
  LexResult r = lexer_lex("| & ->", &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* PIPE AMP ARROW EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_PIPE);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_AMP);
  ASSERT_U32_EQ(r.tokens[1].column, 3);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_ARROW);
  ASSERT_U32_EQ(r.tokens[2].column, 5);
  ASSERT_U32_EQ(r.tokens[2].length, 2);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_pipe_vs_or(void) {
  setup();
  /* "|" and "||" produce different tokens */
  LexResult r = lexer_lex("| ||", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* PIPE OR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_PIPE);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OR);
  ASSERT_U32_EQ(r.tokens[1].length, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_amp_vs_and(void) {
  setup();
  /* "&" and "&&" produce different tokens */
  LexResult r = lexer_lex("& &&", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* AMP AND EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_AMP);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_AND);
  ASSERT_U32_EQ(r.tokens[1].length, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_colon_vs_double_colon(void) {
  setup();
  /* ":" and "::" produce different tokens */
  LexResult r = lexer_lex(": ::", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* COLON DOUBLE_COLON EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_COLON);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_DOUBLE_COLON);
  ASSERT_U32_EQ(r.tokens[1].length, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_equals_vs_eq_eq(void) {
  setup();
  /* "=" is TOKEN_EQUALS, "==" is TOKEN_OPERATOR */
  LexResult r = lexer_lex("= ==", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* EQUALS OPERATOR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_EQUALS);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT_U32_EQ(r.tokens[1].length, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_vs_neq(void) {
  setup();
  /* "!" is TOKEN_BANG, "!=" is TOKEN_OPERATOR */
  LexResult r = lexer_lex("! !=", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* BANG OPERATOR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT_U32_EQ(r.tokens[1].length, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_arrow_vs_minus(void) {
  setup();
  /* "->" is TOKEN_ARROW, "-" is TOKEN_OPERATOR */
  LexResult r = lexer_lex("-> -", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* ARROW OPERATOR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ARROW);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT_U32_EQ(r.tokens[1].length, 1);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_dotdot_vs_dot(void) {
  setup();
  /* ".." is TOKEN_DOTDOT, "." is TOKEN_OPERATOR */
  LexResult r = lexer_lex(".. .", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* DOTDOT OPERATOR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_DOTDOT);
  ASSERT_U32_EQ(r.tokens[0].length, 2);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT_U32_EQ(r.tokens[1].length, 1);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_old_arithmetic_operators_unchanged(void) {
  setup();
  /* +, -, *, /, %, <=, >= stay TOKEN_OPERATOR */
  LexResult r = lexer_lex("+ - * / % <= >=", &test_arena);
  ASSERT_U32_EQ(r.count, 8); /* 7 operators + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_OPERATOR);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_OPERATOR);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_OPERATOR);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_OPERATOR);
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_OPERATOR);
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_OPERATOR);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_word_with_bang_suffix_still_word(void) {
  setup();
  /* "set!" is a word with ! suffix, not TOKEN_SET + TOKEN_BANG */
  LexResult r = lexer_lex("set!", &test_arena);
  ASSERT_U32_EQ(r.count, 2);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_WORD);
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- US-001: Shell Interop - ! prefix for external commands ---- */

static int test_bang_ls(void) {
  setup();
  /* "!ls" → TOKEN_BANG + TOKEN_WORD */
  LexResult r = lexer_lex("!ls", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* BANG WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_U32_EQ(r.tokens[0].line, 1);
  ASSERT_U32_EQ(r.tokens[0].column, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "ls"));
  ASSERT_U32_EQ(r.tokens[1].column, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_git_status(void) {
  setup();
  /* "!git status" → TOKEN_BANG + TOKEN_WORD + TOKEN_WORD */
  LexResult r = lexer_lex("!git status", &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* BANG WORD WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "git"));
  ASSERT_U32_EQ(r.tokens[1].column, 2);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[2], "status"));
  ASSERT_U32_EQ(r.tokens[2].column, 6);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_ls_la(void) {
  setup();
  /* "!ls -la" → TOKEN_BANG + TOKEN_WORD + TOKEN_OPERATOR (-) + TOKEN_WORD (la) */
  LexResult r = lexer_lex("!ls -la", &test_arena);
  ASSERT_U32_EQ(r.count, 5); /* BANG WORD OPERATOR WORD EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "ls"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[2], "-"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[3], "la"));
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_with_var(void) {
  setup();
  /* "!$cmd" → TOKEN_BANG + TOKEN_VAR */
  LexResult r = lexer_lex("!$cmd", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* BANG VAR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[1], "cmd"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_with_string(void) {
  setup();
  /* '!"my script.sh"' → TOKEN_BANG + TOKEN_STRING */
  LexResult r = lexer_lex("!\"my script.sh\"", &test_arena);
  ASSERT_U32_EQ(r.count, 3); /* BANG STRING EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_STRING);
  ASSERT_STR_EQ(r.tokens[1].payload.text, "my script.sh");
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_with_splat(void) {
  setup();
  /* "!cmd ..$args" → TOKEN_BANG + TOKEN_WORD + TOKEN_DOTDOT + TOKEN_VAR */
  LexResult r = lexer_lex("!cmd ..$args", &test_arena);
  ASSERT_U32_EQ(r.count, 5); /* BANG WORD DOTDOT VAR EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "cmd"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_DOTDOT);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_VAR);
  ASSERT(var_text_eq(r.tokens[3], "args"));
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_in_pipeline(void) {
  setup();
  /* "!ls | !grep foo" → BANG WORD PIPE BANG WORD WORD EOF */
  LexResult r = lexer_lex("!ls | !grep foo", &test_arena);
  ASSERT_U32_EQ(r.count, 7);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "ls"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_PIPE);
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_BANG);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[4], "grep"));
  ASSERT_INT_EQ(r.tokens[5].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[5], "foo"));
  ASSERT_INT_EQ(r.tokens[6].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_bang_neq_distinction(void) {
  setup();
  /* Verify ! and != are distinguished properly */
  /* "!ls != foo" → BANG WORD OPERATOR WORD EOF */
  LexResult r = lexer_lex("!ls != foo", &test_arena);
  ASSERT_U32_EQ(r.count, 5);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_BANG);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[2], "!="));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_WORD);
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_EOF);
  ASSERT_U32_EQ(r.error_count, 0);
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
    {"empty_input",              test_empty_input},
    {"single_lbracket",          test_single_lbracket},
    {"all_delimiters",           test_all_delimiters},
    {"whitespace_skipped",       test_whitespace_skipped},
    {"newline_lf",               test_newline_lf},
    {"newline_crlf",             test_newline_crlf},
    {"consecutive_newlines",     test_consecutive_newlines},
    {"delimiters_with_newlines", test_delimiters_with_newlines},
    {"only_whitespace",          test_only_whitespace},
    {"mixed_crlf_lf",           test_mixed_crlf_lf},
    {"eof_position_after_content", test_eof_position_after_content},
    {"bare_cr",                  test_bare_cr},
    /* US-003 */
    {"simple_word",              test_simple_word},
    {"multiple_words",           test_multiple_words},
    {"word_with_digits_hyphens", test_word_with_digits_and_hyphens},
    {"utf8_word",                test_utf8_word},
    {"simple_operator",          test_simple_operator},
    {"multi_char_operator",      test_multi_char_operator},
    {"comment_skipped",          test_comment_skipped},
    {"comment_before_newline",   test_comment_before_newline},
    {"comment_after_tokens",     test_comment_after_tokens},
    {"word_at_delimiters",       test_word_terminates_at_delimiters},
    {"word_at_hash",             test_word_terminates_at_hash},
    {"text_points_into_source",  test_text_points_into_source},
    {"mixed_sequence",           test_mixed_sequence},
    {"multiline_with_comments",  test_multiline_with_comments},
    /* US-004 */
    {"int_zero",                 test_int_zero},
    {"int_positive",             test_int_positive},
    {"float_basic",              test_float_basic},
    {"float_leading_zero",       test_float_leading_zero},
    {"hex_literal",              test_hex_literal},
    {"hex_uppercase",            test_hex_uppercase},
    {"binary_literal",           test_binary_literal},
    {"binary_zero",              test_binary_zero},
    {"invalid_suffix_int",       test_invalid_suffix_on_int},
    {"invalid_suffix_float",     test_invalid_suffix_on_float},
    {"invalid_suffix_hex",       test_invalid_suffix_on_hex},
    {"int32_max",                test_int32_max},
    /* M12 US-001: integer overflow */
    {"decimal_overflow",         test_decimal_overflow},
    {"hex_overflow_FFFFFFFF",    test_hex_overflow_0xFFFFFFFF},
    {"hex_overflow_80000000",    test_hex_overflow_0x80000000},
    {"hex_max_valid",            test_hex_max_valid},
    {"bin_overflow_33bits",      test_binary_overflow_33bits},
    {"bin_max_valid",            test_binary_max_valid},
    {"overflow_error_span",      test_overflow_error_has_span},
    {"very_small_float",         test_very_small_float},
    {"numbers_in_expression",    test_numbers_in_expression},
    {"multiple_numbers",         test_multiple_numbers},
    /* US-005 */
    {"var_simple_single_char",    test_var_simple_single_char},
    {"var_simple_multichar",      test_var_simple_multichar},
    {"var_with_hyphen",           test_var_with_hyphen},
    {"var_with_underscore",       test_var_with_underscore},
    {"var_with_digits",           test_var_with_digits},
    {"var_payload_into_source",   test_var_payload_points_into_source},
    {"dollar_bracket",            test_dollar_bracket},
    {"dollar_bracket_with_expr",  test_dollar_bracket_with_expr},
    {"dollar_at_eof",             test_dollar_at_eof},
    {"dollar_digit",              test_dollar_digit},
    {"dollar_bang",               test_dollar_bang},
    {"var_at_delimiter",          test_var_terminates_at_delimiter},
    {"var_in_expression",         test_var_in_expression},
    {"var_underscore_start",      test_var_underscore_start},
    /* US-006 */
    {"string_empty",              test_string_empty},
    {"string_simple",             test_string_simple},
    {"string_escape_backslash",   test_string_escape_backslash},
    {"string_escape_quote",       test_string_escape_quote},
    {"string_escape_n_t_r",       test_string_escape_n_t_r},
    {"string_escape_null",        test_string_escape_null},
    {"string_hex_escape",         test_string_hex_escape},
    {"string_unicode_4digit",     test_string_unicode_4digit},
    {"string_unicode_2byte",      test_string_unicode_2byte},
    {"string_unicode_3byte",      test_string_unicode_3byte},
    {"string_unicode_8digit",     test_string_unicode_8digit},
    {"string_multiline",          test_string_multiline},
    {"string_unterminated",       test_string_unterminated},
    {"string_invalid_escape",     test_string_invalid_escape},
    {"string_invalid_esc_recov",  test_string_invalid_escape_recovery},
    {"string_mixed_escapes",      test_string_mixed_escapes},
    {"string_in_expression",      test_string_in_expression},
    {"string_position_tracking",  test_string_position_tracking},
    {"string_backslash_at_eof",   test_string_backslash_at_eof},
    /* US-007 */
    {"interp_var_simple",          test_interp_var_simple},
    {"interp_expr_simple",         test_interp_expr_simple},
    {"interp_multiple",            test_interp_multiple},
    {"interp_nested_brackets",     test_interp_nested_brackets},
    {"interp_no_interp_string",    test_interp_no_interp_still_string},
    {"interp_mixed_escapes",       test_interp_mixed_escapes},
    {"interp_adjacent",            test_interp_adjacent},
    {"interp_at_start",            test_interp_at_start},
    {"interp_at_end",              test_interp_at_end},
    {"interp_dollar_not_var",      test_interp_dollar_not_var},
    {"interp_dollar_end_str",      test_interp_dollar_at_end_of_string},
    {"interp_expr_with_string",    test_interp_expr_with_string},
    {"interp_in_context",          test_interp_in_context},
    /* US-008 */
    {"error_control_char",          test_error_control_char},
    {"error_recovery_continues",    test_error_recovery_continues},
    {"error_positions_after_errs",  test_error_positions_after_errors},
    {"error_count_total",           test_error_count_reflects_total},
    {"error_mixed_program",         test_error_mixed_program},
    {"integration_full_program",    test_integration_full_program},
    /* M6 US-001 */
    {"comma_separator",              test_comma_separator},
    {"comma_inside_string",          test_comma_inside_string},
    /* M6 US-002 */
    {"backslash_newline_cont",       test_backslash_newline_continuation},
    {"backslash_crlf_cont",          test_backslash_crlf_continuation},
    {"backslash_not_continuation",   test_backslash_not_continuation},
    {"backslash_string_unaffected",  test_backslash_string_unaffected},
    /* US-001: new operator tokens */
    {"token_pipe",                   test_token_pipe},
    {"token_or",                     test_token_or},
    {"token_amp",                    test_token_amp},
    {"token_and",                    test_token_and},
    {"token_not",                    test_token_not},
    {"token_equals",                 test_token_equals},
    {"token_eq_eq_operator",         test_token_eq_eq_stays_operator},
    {"token_colon",                  test_token_colon},
    {"token_double_colon",           test_token_double_colon},
    {"token_bang",                   test_token_bang},
    {"token_neq_operator",           test_token_neq_stays_operator},
    {"token_arrow",                  test_token_arrow},
    {"token_dotdot",                 test_token_dotdot},
    {"token_backslash",              test_token_backslash},
    {"token_backslash_position",     test_token_backslash_position},
    {"token_dollar_paren",           test_token_dollar_paren},
    {"token_dollar_paren_in_string", test_token_dollar_paren_in_string},
    /* US-001: new keyword tokens */
    {"keyword_proc",                 test_keyword_proc},
    {"keyword_if",                   test_keyword_if},
    {"keyword_elif",                 test_keyword_elif},
    {"keyword_else",                 test_keyword_else},
    {"keyword_while",                test_keyword_while},
    {"keyword_for",                  test_keyword_for},
    {"keyword_def",                  test_keyword_def},
    {"keyword_mut",                  test_keyword_mut},
    {"keyword_set",                  test_keyword_set},
    {"keyword_match",                test_keyword_match},
    {"keyword_return",               test_keyword_return},
    {"keyword_break",                test_keyword_break},
    {"keyword_continue",             test_keyword_continue},
    {"keyword_try",                  test_keyword_try},
    {"keyword_struct",               test_keyword_struct},
    {"keyword_defstruct_removed",    test_keyword_defstruct_removed},
    {"keywords_positions",           test_keywords_have_correct_positions},
    {"keyword_prefix_not_keyword",   test_keyword_prefix_not_keyword},
    {"keyword_hyphen_not_keyword",   test_keyword_with_hyphen_not_keyword},
    {"operator_tokens_positions",    test_operator_tokens_positions},
    {"pipe_vs_or",                   test_pipe_vs_or},
    {"amp_vs_and",                   test_amp_vs_and},
    {"colon_vs_double_colon",        test_colon_vs_double_colon},
    {"equals_vs_eq_eq",              test_equals_vs_eq_eq},
    {"bang_vs_neq",                  test_bang_vs_neq},
    {"arrow_vs_minus",               test_arrow_vs_minus},
    {"dotdot_vs_dot",                test_dotdot_vs_dot},
    {"arithmetic_operators_unchanged", test_old_arithmetic_operators_unchanged},
    {"word_bang_suffix_word",        test_word_with_bang_suffix_still_word},
    /* US-001: Shell Interop - ! prefix */
    {"bang_ls",                      test_bang_ls},
    {"bang_git_status",              test_bang_git_status},
    {"bang_ls_la",                   test_bang_ls_la},
    {"bang_with_var",                test_bang_with_var},
    {"bang_with_string",             test_bang_with_string},
    {"bang_with_splat",              test_bang_with_splat},
    {"bang_in_pipeline",             test_bang_in_pipeline},
    {"bang_neq_distinction",         test_bang_neq_distinction},
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
