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

/* ---- runner ---- */

typedef int (*test_fn)(void);

typedef struct {
  const char* name;
  test_fn     fn;
} TestEntry;

int main(void) {
  TestEntry tests[] = {
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
