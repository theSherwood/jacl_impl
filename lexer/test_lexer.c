#define ARENA_IMPLEMENTATION
#define LEXER_IMPLEMENTATION
#include "../test/test_helpers.h"
#include "lexer.h"

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

static int test_keyword(void) {
  setup();
  LexResult r = lexer_lex(":key", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* KEYWORD + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_KEYWORD);
  ASSERT(token_text_eq(r.tokens[0], ":key"));
  ASSERT_U32_EQ(r.tokens[0].length, 4);
  ASSERT_U32_EQ(r.tokens[0].offset, 0);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_keyword_with_hyphens(void) {
  setup();
  LexResult r = lexer_lex(":my-thing", &test_arena);
  ASSERT_U32_EQ(r.count, 2); /* KEYWORD + EOF */
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_KEYWORD);
  ASSERT(token_text_eq(r.tokens[0], ":my-thing"));
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_EOF);
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
  const char* src = "hello :key >=";
  LexResult r = lexer_lex(src, &test_arena);
  ASSERT_U32_EQ(r.count, 4); /* WORD KEYWORD OPERATOR EOF */
  /* Verify payload.text points into the source buffer */
  ASSERT_PTR_EQ(r.tokens[0].payload.text, src + 0);
  ASSERT_PTR_EQ(r.tokens[1].payload.text, src + 6);
  ASSERT_PTR_EQ(r.tokens[2].payload.text, src + 11);
  teardown();
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_mixed_sequence(void) {
  setup();
  /* A realistic JACL fragment */
  LexResult r = lexer_lex("[map :key1 val1 :key2 val2]", &test_arena);
  /* [ WORD KEYWORD WORD KEYWORD WORD ] EOF = 8 */
  ASSERT_U32_EQ(r.count, 8);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_LBRACKET);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[1], "map"));
  ASSERT_INT_EQ(r.tokens[2].type, TOKEN_KEYWORD);
  ASSERT(token_text_eq(r.tokens[2], ":key1"));
  ASSERT_INT_EQ(r.tokens[3].type, TOKEN_WORD);
  ASSERT(token_text_eq(r.tokens[3], "val1"));
  ASSERT_INT_EQ(r.tokens[4].type, TOKEN_KEYWORD);
  ASSERT(token_text_eq(r.tokens[4], ":key2"));
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
  /* ERROR + OPERATOR + EOF = 3 ($ is error, ! is operator) */
  ASSERT_U32_EQ(r.count, 3);
  ASSERT_INT_EQ(r.tokens[0].type, TOKEN_ERROR);
  ASSERT_U32_EQ(r.tokens[0].length, 1);
  ASSERT_INT_EQ(r.tokens[1].type, TOKEN_OPERATOR);
  ASSERT(token_text_eq(r.tokens[1], "!"));
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
    {"keyword",                  test_keyword},
    {"keyword_with_hyphens",     test_keyword_with_hyphens},
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
