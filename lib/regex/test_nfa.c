/*
 * test_nfa.c — Tests for NFA regex engine
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../test/test_helpers.h"

/* Wire up tracked allocator */
#define NFA_MALLOC(sz) tracked_malloc(sz)
#define NFA_FREE(p)    tracked_free(p)
#include "nfa.h"

/* ── Parse helper ────────────────────────────────────────────────────── */

/* Parse a C string literal pattern, asserting success. */
#define PARSE_OK(str, out)                                          \
  do {                                                              \
    int _rc = nfa_parse((const uint8_t*)(str), strlen(str), &(out));\
    ASSERT_INT_EQ(_rc, NFA_OK);                                     \
    ASSERT((out) != NULL);                                          \
  } while (0)

/* Parse a C string literal pattern, asserting specific error. */
#define PARSE_ERR(str, expected_err)                                 \
  do {                                                               \
    nfa_ast* _ast = NULL;                                            \
    int _rc = nfa_parse((const uint8_t*)(str), strlen(str), &_ast);  \
    ASSERT_INT_EQ(_rc, expected_err);                                \
    ASSERT(_ast == NULL);                                            \
  } while (0)

/* ── Tests ───────────────────────────────────────────────────────────── */

static int test_single_literal(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_SIZE_EQ(ast->literal.len, 1);
    ASSERT_INT_EQ(ast->literal.bytes[0], 'a');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_utf8_literal(void) {
    tracker_reset();
    /* "é" = 0xC3 0xA9 */
    const uint8_t pat[] = { 0xC3, 0xA9 };
    nfa_ast* ast;
    int rc = nfa_parse(pat, 2, &ast);
    ASSERT_INT_EQ(rc, NFA_OK);
    ASSERT(ast != NULL);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_SIZE_EQ(ast->literal.len, 2);
    ASSERT_INT_EQ(ast->literal.bytes[0], 0xC3);
    ASSERT_INT_EQ(ast->literal.bytes[1], 0xA9);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_utf8_3byte(void) {
    tracker_reset();
    /* "€" = 0xE2 0x82 0xAC */
    const uint8_t pat[] = { 0xE2, 0x82, 0xAC };
    nfa_ast* ast;
    int rc = nfa_parse(pat, 3, &ast);
    ASSERT_INT_EQ(rc, NFA_OK);
    ASSERT(ast != NULL);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_SIZE_EQ(ast->literal.len, 3);
    ASSERT_INT_EQ(ast->literal.bytes[0], 0xE2);
    ASSERT_INT_EQ(ast->literal.bytes[1], 0x82);
    ASSERT_INT_EQ(ast->literal.bytes[2], 0xAC);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_utf8_4byte(void) {
    tracker_reset();
    /* U+1F600 (😀) = 0xF0 0x9F 0x98 0x80 */
    const uint8_t pat[] = { 0xF0, 0x9F, 0x98, 0x80 };
    nfa_ast* ast;
    int rc = nfa_parse(pat, 4, &ast);
    ASSERT_INT_EQ(rc, NFA_OK);
    ASSERT(ast != NULL);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_SIZE_EQ(ast->literal.len, 4);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_concatenation(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("abc", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 3);
    ASSERT_INT_EQ(ast->concat.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->concat.children[0]->literal.bytes[0], 'a');
    ASSERT_INT_EQ(ast->concat.children[1]->literal.bytes[0], 'b');
    ASSERT_INT_EQ(ast->concat.children[2]->literal.bytes[0], 'c');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_alternation(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a|b", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ALT);
    ASSERT_SIZE_EQ(ast->alt.count, 2);
    ASSERT_INT_EQ(ast->alt.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->alt.children[0]->literal.bytes[0], 'a');
    ASSERT_INT_EQ(ast->alt.children[1]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->alt.children[1]->literal.bytes[0], 'b');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_nested_alternation(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a|b|c", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ALT);
    ASSERT_SIZE_EQ(ast->alt.count, 3);
    ASSERT_INT_EQ(ast->alt.children[0]->literal.bytes[0], 'a');
    ASSERT_INT_EQ(ast->alt.children[1]->literal.bytes[0], 'b');
    ASSERT_INT_EQ(ast->alt.children[2]->literal.bytes[0], 'c');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_alternation_concat(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("ab|cd", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ALT);
    ASSERT_SIZE_EQ(ast->alt.count, 2);
    ASSERT_INT_EQ(ast->alt.children[0]->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->alt.children[0]->concat.count, 2);
    ASSERT_INT_EQ(ast->alt.children[1]->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->alt.children[1]->concat.count, 2);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_star(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a*", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 0);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT(ast->repeat.greedy);
    ASSERT_INT_EQ(ast->repeat.child->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->repeat.child->literal.bytes[0], 'a');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_plus(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a+", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 1);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT(ast->repeat.greedy);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_question(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a?", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 0);
    ASSERT_INT_EQ(ast->repeat.max, 1);
    ASSERT(ast->repeat.greedy);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_nested_groups(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(a(b)c)", ast);
    /* Group(0, Concat([a, Group(1, b), c])) */
    ASSERT_INT_EQ(ast->type, NFA_AST_GROUP);
    ASSERT_INT_EQ(ast->group.index, 0);
    nfa_ast* inner = ast->group.child;
    ASSERT_INT_EQ(inner->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(inner->concat.count, 3);
    ASSERT_INT_EQ(inner->concat.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(inner->concat.children[1]->type, NFA_AST_GROUP);
    ASSERT_INT_EQ(inner->concat.children[1]->group.index, 1);
    ASSERT_INT_EQ(inner->concat.children[1]->group.child->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(inner->concat.children[1]->group.child->literal.bytes[0], 'b');
    ASSERT_INT_EQ(inner->concat.children[2]->type, NFA_AST_LITERAL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_group_repeat(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(ab)*", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 0);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT_INT_EQ(ast->repeat.child->type, NFA_AST_GROUP);
    ASSERT_INT_EQ(ast->repeat.child->group.child->type, NFA_AST_CONCAT);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_group_alternation(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a(b|c)d", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 3);
    ASSERT_INT_EQ(ast->concat.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->concat.children[1]->type, NFA_AST_GROUP);
    ASSERT_INT_EQ(ast->concat.children[1]->group.child->type, NFA_AST_ALT);
    ASSERT_INT_EQ(ast->concat.children[2]->type, NFA_AST_LITERAL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_dot(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK(".", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_DOT);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_bol_anchor(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("^", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->anchor, NFA_ANCHOR_BOL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_eol_anchor(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("$", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->anchor, NFA_ANCHOR_EOL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_anchored_pattern(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("^foo$", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 5);
    ASSERT_INT_EQ(ast->concat.children[0]->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->concat.children[0]->anchor, NFA_ANCHOR_BOL);
    ASSERT_INT_EQ(ast->concat.children[1]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->concat.children[4]->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->concat.children[4]->anchor, NFA_ANCHOR_EOL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Escaped metacharacters ──────────────────────────────────────────── */

static int test_escaped_dot(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\.", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_SIZE_EQ(ast->literal.len, 1);
    ASSERT_INT_EQ(ast->literal.bytes[0], '.');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_escaped_backslash(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\\\", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->literal.bytes[0], '\\');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_escaped_pipe(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\|", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->literal.bytes[0], '|');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_escaped_lparen(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\(", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->literal.bytes[0], '(');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_escaped_rparen(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\)", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->literal.bytes[0], ')');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_escaped_star(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\*", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->literal.bytes[0], '*');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_escaped_plus(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\+", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->literal.bytes[0], '+');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_escaped_question(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\?", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->literal.bytes[0], '?');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Error cases ─────────────────────────────────────────────────────── */

static int test_err_unmatched_lparen(void) {
    tracker_reset();
    PARSE_ERR("(abc", NFA_ERR_UNMATCHED_LPAREN);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_err_unmatched_rparen(void) {
    tracker_reset();
    PARSE_ERR("abc)", NFA_ERR_UNMATCHED_RPAREN);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_err_trailing_backslash(void) {
    tracker_reset();
    PARSE_ERR("abc\\", NFA_ERR_TRAILING_BACKSLASH);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_err_invalid_escape(void) {
    tracker_reset();
    PARSE_ERR("\\z", NFA_ERR_INVALID_ESCAPE);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_err_nested_unmatched(void) {
    tracker_reset();
    PARSE_ERR("(a(b)", NFA_ERR_UNMATCHED_LPAREN);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_err_rparen_only(void) {
    tracker_reset();
    PARSE_ERR(")", NFA_ERR_UNMATCHED_RPAREN);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Empty / edge cases ──────────────────────────────────────────────── */

static int test_empty_pattern(void) {
    tracker_reset();
    nfa_ast* ast;
    int rc = nfa_parse((const uint8_t*)"", 0, &ast);
    ASSERT_INT_EQ(rc, NFA_OK);
    ASSERT(ast != NULL);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 0);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_empty_group(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("()", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_GROUP);
    ASSERT_INT_EQ(ast->group.index, 0);
    ASSERT_INT_EQ(ast->group.child->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->group.child->concat.count, 0);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_empty_alternation(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a|", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ALT);
    ASSERT_SIZE_EQ(ast->alt.count, 2);
    ASSERT_INT_EQ(ast->alt.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->alt.children[1]->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->alt.children[1]->concat.count, 0);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_complex_pattern(void) {
    tracker_reset();
    nfa_ast* ast;
    /* (a+b)*|c?.d$ */
    PARSE_OK("(a+b)*|c?.d$", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ALT);
    ASSERT_SIZE_EQ(ast->alt.count, 2);
    /* First alt: (a+b)* → Repeat(Group(Concat([Repeat(a,1,-1), b]))) */
    nfa_ast* lhs = ast->alt.children[0];
    ASSERT_INT_EQ(lhs->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(lhs->repeat.child->type, NFA_AST_GROUP);
    /* Second alt: c?.d$ → Concat([Repeat(c,0,1), Dot, d, Anchor($)]) */
    nfa_ast* rhs = ast->alt.children[1];
    ASSERT_INT_EQ(rhs->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(rhs->concat.count, 4);
    ASSERT_INT_EQ(rhs->concat.children[0]->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(rhs->concat.children[1]->type, NFA_AST_DOT);
    ASSERT_INT_EQ(rhs->concat.children[2]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(rhs->concat.children[3]->type, NFA_AST_ANCHOR);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Character class tests (US-002) ──────────────────────────────────── */

static int test_char_class_explicit(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[abc]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 3);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[1].lo, 'b');
    ASSERT_INT_EQ(ast->char_class.ranges[1].hi, 'b');
    ASSERT_INT_EQ(ast->char_class.ranges[2].lo, 'c');
    ASSERT_INT_EQ(ast->char_class.ranges[2].hi, 'c');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_range(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[a-z]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 1);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, 'z');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_multi_range(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[a-zA-Z0-9]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 3);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, 'z');
    ASSERT_INT_EQ(ast->char_class.ranges[1].lo, 'A');
    ASSERT_INT_EQ(ast->char_class.ranges[1].hi, 'Z');
    ASSERT_INT_EQ(ast->char_class.ranges[2].lo, '0');
    ASSERT_INT_EQ(ast->char_class.ranges[2].hi, '9');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_negated(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[^abc]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 3);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, 'a');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_negated_range(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[^a-z]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 1);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, 'z');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_d(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\d", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 1);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, '0');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, '9');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_w(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\w", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 4); /* a-z, A-Z, 0-9, _ */
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, 'z');
    ASSERT_INT_EQ(ast->char_class.ranges[1].lo, 'A');
    ASSERT_INT_EQ(ast->char_class.ranges[1].hi, 'Z');
    ASSERT_INT_EQ(ast->char_class.ranges[2].lo, '0');
    ASSERT_INT_EQ(ast->char_class.ranges[2].hi, '9');
    ASSERT_INT_EQ(ast->char_class.ranges[3].lo, '_');
    ASSERT_INT_EQ(ast->char_class.ranges[3].hi, '_');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_s(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\s", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 6); /* space, \t, \n, \r, \f, \v */
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, ' ');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, ' ');
    ASSERT_INT_EQ(ast->char_class.ranges[1].lo, '\t');
    ASSERT_INT_EQ(ast->char_class.ranges[1].hi, '\t');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_D(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\D", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 1); /* same ranges as \d, but negated */
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, '0');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, '9');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_W(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\W", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 4);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_S(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\S", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 6);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_in_class(void) {
    tracker_reset();
    nfa_ast* ast;
    /* [\d\s] should combine digit and whitespace ranges */
    PARSE_OK("[\\d\\s]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    /* \d = 1 range (0-9), \s = 6 ranges */
    ASSERT_SIZE_EQ(ast->char_class.count, 7);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, '0');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, '9');
    ASSERT_INT_EQ(ast->char_class.ranges[1].lo, ' ');
    ASSERT_INT_EQ(ast->char_class.ranges[1].hi, ' ');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_literal_rbracket(void) {
    tracker_reset();
    nfa_ast* ast;
    /* []] — ] at start is literal */
    PARSE_OK("[]]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 1);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, ']');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, ']');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_literal_dash(void) {
    tracker_reset();
    nfa_ast* ast;
    /* [-a] — - at start is literal */
    PARSE_OK("[-a]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 2);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, '-');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, '-');
    ASSERT_INT_EQ(ast->char_class.ranges[1].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[1].hi, 'a');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_dash_at_end(void) {
    tracker_reset();
    nfa_ast* ast;
    /* [a-] — - at end is literal */
    PARSE_OK("[a-]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->char_class.negated);
    ASSERT_SIZE_EQ(ast->char_class.count, 2);
    ASSERT_INT_EQ(ast->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[0].hi, 'a');
    ASSERT_INT_EQ(ast->char_class.ranges[1].lo, '-');
    ASSERT_INT_EQ(ast->char_class.ranges[1].hi, '-');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_err_unmatched_lbracket(void) {
    tracker_reset();
    PARSE_ERR("[abc", NFA_ERR_UNMATCHED_LBRACKET);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_char_class_in_pattern(void) {
    tracker_reset();
    nfa_ast* ast;
    /* [a-z]+ — char class with quantifier in context */
    PARSE_OK("[a-z]+", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 1);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT_INT_EQ(ast->repeat.child->type, NFA_AST_CHAR_CLASS);
    ASSERT_SIZE_EQ(ast->repeat.child->char_class.count, 1);
    ASSERT_INT_EQ(ast->repeat.child->char_class.ranges[0].lo, 'a');
    ASSERT_INT_EQ(ast->repeat.child->char_class.ranges[0].hi, 'z');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_shorthand_in_concat(void) {
    tracker_reset();
    nfa_ast* ast;
    /* \d\w — two shorthand classes concatenated */
    PARSE_OK("\\d\\w", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 2);
    ASSERT_INT_EQ(ast->concat.children[0]->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->concat.children[0]->char_class.negated);
    ASSERT_SIZE_EQ(ast->concat.children[0]->char_class.count, 1); /* 0-9 */
    ASSERT_INT_EQ(ast->concat.children[1]->type, NFA_AST_CHAR_CLASS);
    ASSERT(!ast->concat.children[1]->char_class.negated);
    ASSERT_SIZE_EQ(ast->concat.children[1]->char_class.count, 4); /* a-z,A-Z,0-9,_ */
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Compiler tests (US-003) ──────────────────────────────────────────── */

/* Helper: parse + compile a pattern string */
#define COMPILE_OK(str, prog)                                        \
  do {                                                               \
    nfa_ast* _ast = NULL;                                            \
    int _rc = nfa_parse((const uint8_t*)(str), strlen(str), &_ast);  \
    ASSERT_INT_EQ(_rc, NFA_OK);                                      \
    ASSERT(_ast != NULL);                                            \
    _rc = nfa_compile(_ast, &(prog));                                \
    ASSERT_INT_EQ(_rc, NFA_OK);                                      \
    ASSERT((prog) != NULL);                                          \
    nfa_ast_free(_ast);                                              \
  } while (0)

static int test_compile_literal(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a", prog);
    ASSERT_SIZE_EQ(prog->len, 2); /* LITERAL + MATCH */
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[prog->start].byte, 'a');
    ASSERT_INT_EQ(prog->code[prog->len - 1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_concat(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("ab", prog);
    ASSERT_SIZE_EQ(prog->len, 3); /* 2 LITERAL + MATCH */
    ASSERT_INT_EQ(prog->code[0].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[0].byte, 'a');
    ASSERT_INT_EQ(prog->code[1].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[1].byte, 'b');
    ASSERT_INT_EQ(prog->code[2].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_alt(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a|b", prog);
    /* LITERAL 'a', LITERAL 'b', SPLIT, MATCH = 4 */
    ASSERT_SIZE_EQ(prog->len, 4);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_SPLIT);
    ASSERT_INT_EQ(prog->code[prog->len - 1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_star(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a*", prog);
    /* LITERAL, SPLIT, JMP, MATCH = 4 */
    ASSERT_SIZE_EQ(prog->len, 4);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_SPLIT);
    ASSERT_INT_EQ(prog->code[prog->len - 1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_plus(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a+", prog);
    /* LITERAL, SPLIT, MATCH = 3 */
    ASSERT_SIZE_EQ(prog->len, 3);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[prog->len - 1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_question(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a?", prog);
    /* LITERAL, SPLIT, MATCH = 3 */
    ASSERT_SIZE_EQ(prog->len, 3);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_SPLIT);
    ASSERT_INT_EQ(prog->code[prog->len - 1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_dot(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK(".", prog);
    ASSERT_SIZE_EQ(prog->len, 2); /* ANY + MATCH */
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_ANY);
    ASSERT_INT_EQ(prog->code[prog->len - 1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_anchor_bol(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("^", prog);
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_ANCHOR_BOL);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_anchor_eol(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("$", prog);
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_ANCHOR_EOL);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_char_class_single(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("[a-z]", prog);
    ASSERT_SIZE_EQ(prog->len, 2); /* RANGE + MATCH */
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_RANGE);
    ASSERT_INT_EQ(prog->code[prog->start].lo, 'a');
    ASSERT_INT_EQ(prog->code[prog->start].hi, 'z');
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_char_class_multi(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("[a-zA-Z]", prog);
    /* CLASS{[a-z],[A-Z]} + MATCH = 2 */
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_CLASS);
    ASSERT_SIZE_EQ(prog->code[prog->start].n_ranges, 2);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_negated_class(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("[^a-z]", prog);
    /* Complement of [a-z] = [0-96],[123-255] → CLASS{2 ranges} + MATCH = 2 */
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_CLASS);
    ASSERT_SIZE_EQ(prog->code[prog->start].n_ranges, 2);
    /* Verify complement ranges are correct */
    bool found_low = false, found_high = false;
    nfa_inst* ci = &prog->code[prog->start];
    for (size_t i = 0; i < ci->n_ranges; i++) {
        if (ci->ranges[i].lo == 0 && ci->ranges[i].hi == 96) found_low = true;
        if (ci->ranges[i].lo == 123 && ci->ranges[i].hi == 255) found_high = true;
    }
    ASSERT(found_low);
    ASSERT(found_high);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_negated_class_abc(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("[^abc]", prog);
    /* abc (97-99) merge to [97-99] → complement: [0-96],[100-255]
     * → CLASS{2 ranges} + MATCH = 2 */
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_CLASS);
    ASSERT_SIZE_EQ(prog->code[prog->start].n_ranges, 2);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_group(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("(a)", prog);
    /* SAVE(0), LITERAL, SAVE(1), MATCH = 4 */
    ASSERT_SIZE_EQ(prog->len, 4);
    ASSERT_SIZE_EQ(prog->n_captures, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_SAVE);
    ASSERT_SIZE_EQ(prog->code[prog->start].slot, 0);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_utf8_literal(void) {
    tracker_reset();
    /* "é" = 0xC3 0xA9 → 2 LITERAL instructions + MATCH */
    const uint8_t pat[] = { 0xC3, 0xA9 };
    nfa_ast* ast;
    int rc = nfa_parse(pat, 2, &ast);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_program* prog;
    rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);
    ASSERT_SIZE_EQ(prog->len, 3); /* 2 LITERAL + MATCH */
    ASSERT_INT_EQ(prog->code[0].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[0].byte, 0xC3);
    ASSERT_INT_EQ(prog->code[1].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[1].byte, 0xA9);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_roundtrip_ab_or_c(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("ab|c", prog);
    ASSERT_SIZE_EQ(prog->len, 5);
    /* Start is SPLIT */
    size_t s = prog->start;
    ASSERT_INT_EQ(prog->code[s].type, NFA_INST_SPLIT);
    /* SPLIT → 'a' branch and 'c' branch */
    size_t br_a = prog->code[s].out;
    size_t br_c = prog->code[s].out1;
    ASSERT_INT_EQ(prog->code[br_a].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[br_a].byte, 'a');
    ASSERT_INT_EQ(prog->code[br_c].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[br_c].byte, 'c');
    /* 'a' → 'b' */
    size_t b_idx = prog->code[br_a].out;
    ASSERT_INT_EQ(prog->code[b_idx].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[b_idx].byte, 'b');
    /* Both 'b' and 'c' → MATCH */
    ASSERT_SIZE_EQ(prog->code[b_idx].out, prog->len - 1);
    ASSERT_SIZE_EQ(prog->code[br_c].out, prog->len - 1);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_roundtrip_a_star(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a*", prog);
    ASSERT_SIZE_EQ(prog->len, 4);
    size_t s = prog->start;
    ASSERT_INT_EQ(prog->code[s].type, NFA_INST_SPLIT);
    /* SPLIT.out → LITERAL (greedy: try body first) */
    size_t body = prog->code[s].out;
    ASSERT_INT_EQ(prog->code[body].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[body].byte, 'a');
    /* SPLIT.out1 → MATCH */
    ASSERT_SIZE_EQ(prog->code[s].out1, prog->len - 1);
    ASSERT_INT_EQ(prog->code[prog->len - 1].type, NFA_INST_MATCH);
    /* LITERAL → JMP → SPLIT (loop) */
    size_t jmp_idx = prog->code[body].out;
    ASSERT_INT_EQ(prog->code[jmp_idx].type, NFA_INST_JMP);
    ASSERT_SIZE_EQ(prog->code[jmp_idx].out, s);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_roundtrip_char_class(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("[a-z]", prog);
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_RANGE);
    ASSERT_INT_EQ(prog->code[prog->start].lo, 'a');
    ASSERT_INT_EQ(prog->code[prog->start].hi, 'z');
    ASSERT_SIZE_EQ(prog->code[prog->start].out, 1);
    ASSERT_INT_EQ(prog->code[1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_roundtrip_anchored(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("^foo$", prog);
    /* ANCHOR_BOL, f, o, o, ANCHOR_EOL, MATCH = 6 */
    ASSERT_SIZE_EQ(prog->len, 6);
    ASSERT_INT_EQ(prog->code[0].type, NFA_INST_ANCHOR_BOL);
    ASSERT_INT_EQ(prog->code[1].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[1].byte, 'f');
    ASSERT_INT_EQ(prog->code[2].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[2].byte, 'o');
    ASSERT_INT_EQ(prog->code[3].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[3].byte, 'o');
    ASSERT_INT_EQ(prog->code[4].type, NFA_INST_ANCHOR_EOL);
    ASSERT_INT_EQ(prog->code[5].type, NFA_INST_MATCH);
    /* Chain: each points to next */
    ASSERT_SIZE_EQ(prog->code[0].out, 1);
    ASSERT_SIZE_EQ(prog->code[1].out, 2);
    ASSERT_SIZE_EQ(prog->code[2].out, 3);
    ASSERT_SIZE_EQ(prog->code[3].out, 4);
    ASSERT_SIZE_EQ(prog->code[4].out, 5);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_empty_pattern(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("", prog);
    /* JMP + MATCH = 2 */
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_JMP);
    ASSERT_INT_EQ(prog->code[1].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_shorthand_d(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("\\d", prog);
    /* \d = [0-9] = 1 range → RANGE + MATCH = 2 */
    ASSERT_SIZE_EQ(prog->len, 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_RANGE);
    ASSERT_INT_EQ(prog->code[prog->start].lo, '0');
    ASSERT_INT_EQ(prog->code[prog->start].hi, '9');
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_nested_groups(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("(a(b))", prog);
    ASSERT_SIZE_EQ(prog->n_captures, 4); /* 2 groups = 4 slots */
    int save_count = 0;
    for (size_t i = 0; i < prog->len; i++) {
        if (prog->code[i].type == NFA_INST_SAVE) save_count++;
    }
    ASSERT_INT_EQ(save_count, 4); /* open/close for each group */
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_linear_size(void) {
    tracker_reset();
    nfa_program* prog;
    /* 20 concatenated literals + MATCH = 21 instructions */
    COMPILE_OK("aaaaaaaaaaaaaaaaaaaa", prog);
    ASSERT_SIZE_EQ(prog->len, 21);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_instruction_count_bound(void) {
    tracker_reset();
    nfa_program* prog;
    /* Complex pattern: (a+b)*|c?.d$ — 12 chars, O(n) instructions */
    COMPILE_OK("(a+b)*|c?.d$", prog);
    ASSERT(prog->len <= 30); /* generous bound */
    ASSERT(prog->len >= 5);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Input abstraction tests (US-004) ─────────────────────────────────── */

static int test_buf_chunk_data_and_length(void) {
    tracker_reset();
    const uint8_t data[] = "hello";
    nfa_input inp = nfa_input_from_buf(data, 5);
    ASSERT(inp.ctx != NULL);

    nfa_chunk c = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c.len, 5);
    ASSERT(memcmp(c.data, "hello", 5) == 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_byte_offset(void) {
    tracker_reset();
    const uint8_t data[] = "hello world";
    nfa_input inp = nfa_input_from_buf(data, 11);
    ASSERT(inp.ctx != NULL);

    /* Before any chunk, byte_offset is 0 */
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 0);

    nfa_chunk c = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c.len, 11);
    /* After chunk, byte_offset returns chunk start */
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_eof_after_first_chunk(void) {
    tracker_reset();
    const uint8_t data[] = "abc";
    nfa_input inp = nfa_input_from_buf(data, 3);
    ASSERT(inp.ctx != NULL);

    nfa_chunk c1 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c1.len, 3);

    /* Second call returns EOF */
    nfa_chunk c2 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c2.len, 0);

    /* Third call still EOF */
    nfa_chunk c3 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c3.len, 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_reset(void) {
    tracker_reset();
    const uint8_t data[] = "abcdef";
    nfa_input inp = nfa_input_from_buf(data, 6);
    ASSERT(inp.ctx != NULL);

    /* Consume first chunk */
    nfa_chunk c1 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c1.len, 6);

    /* EOF */
    nfa_chunk c2 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c2.len, 0);

    /* Reset and read again */
    inp.reset(inp.ctx);
    nfa_chunk c3 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c3.len, 6);
    ASSERT(memcmp(c3.data, "abcdef", 6) == 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_seek(void) {
    tracker_reset();
    const uint8_t data[] = "hello world";
    nfa_input inp = nfa_input_from_buf(data, 11);
    ASSERT(inp.ctx != NULL);

    /* Seek to offset 6 */
    inp.seek(inp.ctx, 6);
    nfa_chunk c = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c.len, 5);
    ASSERT(memcmp(c.data, "world", 5) == 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 6);

    /* EOF after chunk */
    nfa_chunk c2 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c2.len, 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_seek_then_reset(void) {
    tracker_reset();
    const uint8_t data[] = "abcdefgh";
    nfa_input inp = nfa_input_from_buf(data, 8);
    ASSERT(inp.ctx != NULL);

    /* Seek to middle */
    inp.seek(inp.ctx, 4);
    nfa_chunk c1 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c1.len, 4);
    ASSERT(memcmp(c1.data, "efgh", 4) == 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 4);

    /* Reset goes back to start */
    inp.reset(inp.ctx);
    nfa_chunk c2 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c2.len, 8);
    ASSERT(memcmp(c2.data, "abcdefgh", 8) == 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_empty(void) {
    tracker_reset();
    nfa_input inp = nfa_input_from_buf(NULL, 0);
    ASSERT(inp.ctx != NULL);

    /* Empty buffer: first chunk has len 0 (EOF) */
    nfa_chunk c = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c.len, 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_seek_to_end(void) {
    tracker_reset();
    const uint8_t data[] = "abc";
    nfa_input inp = nfa_input_from_buf(data, 3);
    ASSERT(inp.ctx != NULL);

    /* Seek to end */
    inp.seek(inp.ctx, 3);
    nfa_chunk c = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c.len, 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 3);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_buf_multiple_seek(void) {
    tracker_reset();
    const uint8_t data[] = "0123456789";
    nfa_input inp = nfa_input_from_buf(data, 10);
    ASSERT(inp.ctx != NULL);

    /* Seek to 2, read */
    inp.seek(inp.ctx, 2);
    nfa_chunk c1 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c1.len, 8);
    ASSERT(memcmp(c1.data, "23456789", 8) == 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 2);

    /* Seek to 7, read */
    inp.seek(inp.ctx, 7);
    nfa_chunk c2 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c2.len, 3);
    ASSERT(memcmp(c2.data, "789", 3) == 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 7);

    /* Seek back to 0, read */
    inp.seek(inp.ctx, 0);
    nfa_chunk c3 = inp.next_chunk(inp.ctx);
    ASSERT_SIZE_EQ(c3.len, 10);
    ASSERT(memcmp(c3.data, "0123456789", 10) == 0);
    ASSERT_SIZE_EQ(inp.byte_offset(inp.ctx), 0);

    nfa_input_free(&inp);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Simulator test helpers (US-005) ──────────────────────────────────── */

/* Helper: parse + compile + execute against a C string */
/* EXEC_MATCH: parse, compile, exec, free — discards captures.
 * Use EXEC_MATCH_CAPS when you need to inspect captures (caller must free). */
#define EXEC_MATCH(pat, text, out_match)                                          \
  do {                                                                             \
    nfa_ast* _xast = NULL;                                                         \
    int _xrc = nfa_parse((const uint8_t*)(pat), strlen(pat), &_xast);              \
    ASSERT_INT_EQ(_xrc, NFA_OK);                                                   \
    ASSERT(_xast != NULL);                                                          \
    nfa_program* _xprog = NULL;                                                    \
    _xrc = nfa_compile(_xast, &_xprog);                                            \
    ASSERT_INT_EQ(_xrc, NFA_OK);                                                   \
    ASSERT(_xprog != NULL);                                                         \
    nfa_ast_free(_xast);                                                            \
    nfa_input _xinp = nfa_input_from_buf((const uint8_t*)(text), strlen(text));     \
    (out_match) = nfa_exec(_xprog, &_xinp);                                        \
    nfa_input_free(&_xinp);                                                         \
    nfa_program_free(_xprog);                                                       \
    nfa_match_free(&(out_match));                                                   \
  } while (0)

/* EXEC_MATCH_CAPS: like EXEC_MATCH but preserves captures for inspection.
 * Caller MUST call nfa_match_free(&out_match) when done. */
#define EXEC_MATCH_CAPS(pat, text, out_match)                                     \
  do {                                                                             \
    nfa_ast* _xast = NULL;                                                         \
    int _xrc = nfa_parse((const uint8_t*)(pat), strlen(pat), &_xast);              \
    ASSERT_INT_EQ(_xrc, NFA_OK);                                                   \
    ASSERT(_xast != NULL);                                                          \
    nfa_program* _xprog = NULL;                                                    \
    _xrc = nfa_compile(_xast, &_xprog);                                            \
    ASSERT_INT_EQ(_xrc, NFA_OK);                                                   \
    ASSERT(_xprog != NULL);                                                         \
    nfa_ast_free(_xast);                                                            \
    nfa_input _xinp = nfa_input_from_buf((const uint8_t*)(text), strlen(text));     \
    (out_match) = nfa_exec(_xprog, &_xinp);                                        \
    nfa_input_free(&_xinp);                                                         \
    nfa_program_free(_xprog);                                                       \
  } while (0)

/* ── Small-chunk input adapter (for cross-chunk tests) ───────────────── */

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
    size_t chunk_size;
    size_t chunk_start;
} _test_sc_ctx;

static nfa_chunk _test_sc_next(void* ctx) {
    _test_sc_ctx* sc = (_test_sc_ctx*)ctx;
    if (sc->pos >= sc->len) return (nfa_chunk){ NULL, 0 };
    sc->chunk_start = sc->pos;
    size_t rem = sc->len - sc->pos;
    size_t sz  = rem < sc->chunk_size ? rem : sc->chunk_size;
    const uint8_t* p = sc->data + sc->pos;
    sc->pos += sz;
    return (nfa_chunk){ p, sz };
}

static size_t _test_sc_offset(void* ctx) {
    return ((_test_sc_ctx*)ctx)->chunk_start;
}

static void _test_sc_reset(void* ctx) {
    _test_sc_ctx* sc = (_test_sc_ctx*)ctx;
    sc->pos = 0;
    sc->chunk_start = 0;
}

static void _test_sc_seek(void* ctx, size_t off) {
    _test_sc_ctx* sc = (_test_sc_ctx*)ctx;
    sc->pos = off;
    sc->chunk_start = off;
}

static nfa_input _test_small_chunk_input(const uint8_t* data, size_t len,
                                          size_t chunk_size) {
    nfa_input inp = { NULL, NULL, NULL, NULL, NULL };
    _test_sc_ctx* ctx = (_test_sc_ctx*)NFA_MALLOC(sizeof(_test_sc_ctx));
    if (!ctx) return inp;
    ctx->data        = data;
    ctx->len         = len;
    ctx->pos         = 0;
    ctx->chunk_size  = chunk_size;
    ctx->chunk_start = 0;
    inp.next_chunk   = _test_sc_next;
    inp.byte_offset  = _test_sc_offset;
    inp.reset        = _test_sc_reset;
    inp.seek         = _test_sc_seek;
    inp.ctx          = ctx;
    return inp;
}

/* ── Simulator tests (US-005) ─────────────────────────────────────────── */

static int test_exec_literal_match(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a", "a", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_no_match(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a", "bcd", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_alternation(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a|b", "b", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_star(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a*", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_plus(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a+", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_question(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a?", "a", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_anchored_bol(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("^foo", "foobar", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_anchored_bol_newline(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("^foo", "bar\nfoo", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 4);
    ASSERT_SIZE_EQ(m.end, 7);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_anchored_bol_no_match(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("^foo", "barfoo", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_anchored_eol(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("foo$", "foo", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_anchored_full(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("^foo$", "foo", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_match_at_start(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("foo", "foobar", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_match_at_middle(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("bar", "foobarbaz", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 3);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_match_at_end(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("baz", "foobarbaz", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 6);
    ASSERT_SIZE_EQ(m.end, 9);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_dot(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a.c", "abc", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_dot_no_newline(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a.c", "a\nc", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_char_class_range(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("[a-z]", "5x3", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 1);
    ASSERT_SIZE_EQ(m.end, 2);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_char_class_negated(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("[^0-9]", "123a", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 3);
    ASSERT_SIZE_EQ(m.end, 4);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_shorthand_d(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\d", "abc5def", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 3);
    ASSERT_SIZE_EQ(m.end, 4);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_shorthand_w(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\w+", "  hello  ", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 2);
    ASSERT_SIZE_EQ(m.end, 7);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_shorthand_s(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\s", "hello world", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 5);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_cross_chunk(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("abcd", ast);
    nfa_program* prog;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    /* "xxabcdyy" split into chunks of 2: "xx","ab","cd","yy" */
    const uint8_t data[] = "xxabcdyy";
    nfa_input inp = _test_small_chunk_input(data, 8, 2);
    nfa_match m = nfa_exec(prog, &inp);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 2);
    ASSERT_SIZE_EQ(m.end, 6);

    nfa_input_free(&inp);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_cross_chunk_single(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("hello", ast);
    nfa_program* prog;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    /* Every byte is its own chunk */
    const uint8_t data[] = "say hello world";
    nfa_input inp = _test_small_chunk_input(data, 15, 1);
    nfa_match m = nfa_exec(prog, &inp);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 4);
    ASSERT_SIZE_EQ(m.end, 9);

    nfa_input_free(&inp);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_pathological(void) {
    tracker_reset();
    /* Build pattern: a?a?a?...a? (30 times) followed by aaa...a (30 times) */
    char pat[121];
    size_t pi = 0;
    int i;
    for (i = 0; i < 30; i++) { pat[pi++] = 'a'; pat[pi++] = '?'; }
    for (i = 0; i < 30; i++) { pat[pi++] = 'a'; }
    pat[pi] = '\0';

    char txt[31];
    for (i = 0; i < 30; i++) txt[i] = 'a';
    txt[30] = '\0';

    clock_t t0 = clock();
    nfa_match m;
    EXEC_MATCH(pat, txt, m);
    clock_t t1 = clock();

    double elapsed = (double)(t1 - t0) / CLOCKS_PER_SEC;
    ASSERT(elapsed < 1.0);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 30);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_zero_length_bol(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("^", "abc", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Iterator tests (US-006) ──────────────────────────────────────────── */

static int test_iter_multiple_matches(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[0-9]+", ast);
    nfa_program* prog;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    const uint8_t text[] = "abc 123 def 456";
    nfa_input inp = nfa_input_from_buf(text, 15);
    nfa_iter it = nfa_iter_new(prog, inp);

    nfa_match m1 = nfa_iter_next(&it);
    ASSERT(m1.matched);
    ASSERT_SIZE_EQ(m1.start, 4);
    ASSERT_SIZE_EQ(m1.end, 7);

    nfa_match m2 = nfa_iter_next(&it);
    ASSERT(m2.matched);
    ASSERT_SIZE_EQ(m2.start, 12);
    ASSERT_SIZE_EQ(m2.end, 15);

    nfa_match m3 = nfa_iter_next(&it);
    ASSERT(!m3.matched);

    nfa_iter_free(&it);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_iter_adjacent_matches(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[a-z]", ast);
    nfa_program* prog;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    const uint8_t text[] = "abc";
    nfa_input inp = nfa_input_from_buf(text, 3);
    nfa_iter it = nfa_iter_new(prog, inp);

    nfa_match m1 = nfa_iter_next(&it);
    ASSERT(m1.matched);
    ASSERT_SIZE_EQ(m1.start, 0);
    ASSERT_SIZE_EQ(m1.end, 1);

    nfa_match m2 = nfa_iter_next(&it);
    ASSERT(m2.matched);
    ASSERT_SIZE_EQ(m2.start, 1);
    ASSERT_SIZE_EQ(m2.end, 2);

    nfa_match m3 = nfa_iter_next(&it);
    ASSERT(m3.matched);
    ASSERT_SIZE_EQ(m3.start, 2);
    ASSERT_SIZE_EQ(m3.end, 3);

    nfa_match m4 = nfa_iter_next(&it);
    ASSERT(!m4.matched);

    nfa_iter_free(&it);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_iter_zero_length_no_loop(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a*", ast);
    nfa_program* prog;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    /* "bc" has no 'a', so a* only matches empty strings.
     * Should get 3 zero-length matches at positions 0, 1, 2 then done. */
    const uint8_t text[] = "bc";
    nfa_input inp = nfa_input_from_buf(text, 2);
    nfa_iter it = nfa_iter_new(prog, inp);

    nfa_match m1 = nfa_iter_next(&it);
    ASSERT(m1.matched);
    ASSERT_SIZE_EQ(m1.start, 0);
    ASSERT_SIZE_EQ(m1.end, 0);

    nfa_match m2 = nfa_iter_next(&it);
    ASSERT(m2.matched);
    ASSERT_SIZE_EQ(m2.start, 1);
    ASSERT_SIZE_EQ(m2.end, 1);

    nfa_match m3 = nfa_iter_next(&it);
    ASSERT(m3.matched);
    ASSERT_SIZE_EQ(m3.start, 2);
    ASSERT_SIZE_EQ(m3.end, 2);

    nfa_match m4 = nfa_iter_next(&it);
    ASSERT(!m4.matched);

    nfa_iter_free(&it);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_iter_no_matches(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("[0-9]+", ast);
    nfa_program* prog;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    const uint8_t text[] = "no digits here";
    nfa_input inp = nfa_input_from_buf(text, 14);
    nfa_iter it = nfa_iter_new(prog, inp);

    nfa_match m = nfa_iter_next(&it);
    ASSERT(!m.matched);

    nfa_iter_free(&it);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Lazy quantifier tests (US-001 extension) ─────────────────────────── */

static int test_parse_lazy_star(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a*?", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 0);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT(!ast->repeat.greedy);
    ASSERT_INT_EQ(ast->repeat.child->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->repeat.child->literal.bytes[0], 'a');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_lazy_plus(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a+?", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 1);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT(!ast->repeat.greedy);
    ASSERT_INT_EQ(ast->repeat.child->type, NFA_AST_LITERAL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_lazy_question(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a??", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 0);
    ASSERT_INT_EQ(ast->repeat.max, 1);
    ASSERT(!ast->repeat.greedy);
    ASSERT_INT_EQ(ast->repeat.child->type, NFA_AST_LITERAL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_lazy_no_preceding_atom(void) {
    tracker_reset();
    /* "??" at start — no preceding atom, should not crash.
     * First '?' returns NULL from _nfa_parse_atom (quantifier without atom),
     * so _nfa_parse_repeat returns NULL. Second '?' same. Result: empty concat. */
    nfa_ast* ast;
    PARSE_OK("??", ast);
    /* Should parse as empty concat (both ? without atom) */
    ASSERT(ast != NULL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_lazy_star(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a*?", prog);
    /* Same instructions as a* (LITERAL, SPLIT, JMP, MATCH = 4) */
    ASSERT_SIZE_EQ(prog->len, 4);
    /* Start is SPLIT, but lazy: out=exit, out1=body */
    size_t s = prog->start;
    ASSERT_INT_EQ(prog->code[s].type, NFA_INST_SPLIT);
    /* Lazy: SPLIT.out → MATCH (exit), SPLIT.out1 → LITERAL (body) */
    ASSERT_SIZE_EQ(prog->code[s].out, prog->len - 1);  /* → MATCH */
    ASSERT_INT_EQ(prog->code[prog->code[s].out1].type, NFA_INST_LITERAL);
    ASSERT(!prog->greedy_match);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_lazy_star_matches_empty(void) {
    tracker_reset();
    nfa_match m;
    /* a*? on "aaa" should match empty string at position 0 */
    EXEC_MATCH("a*?", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_lazy_plus_matches_one(void) {
    tracker_reset();
    nfa_match m;
    /* a+? on "aaa" should match "a" at [0,1) */
    EXEC_MATCH("a+?", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_lazy_question_matches_empty(void) {
    tracker_reset();
    nfa_match m;
    /* a?? on "aaa" should match empty at position 0 */
    EXEC_MATCH("a??", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 0);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_greedy_vs_lazy_star(void) {
    tracker_reset();
    nfa_match greedy, lazy;
    EXEC_MATCH("a*", "aaa", greedy);
    EXEC_MATCH("a*?", "aaa", lazy);
    /* Greedy matches full "aaa", lazy matches empty */
    ASSERT(greedy.matched);
    ASSERT(lazy.matched);
    ASSERT_SIZE_EQ(greedy.start, 0);
    ASSERT_SIZE_EQ(lazy.start, 0);
    ASSERT_SIZE_EQ(greedy.end, 3);
    ASSERT_SIZE_EQ(lazy.end, 0);
    ASSERT(greedy.end != lazy.end); /* Different match lengths */
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_greedy_vs_lazy_plus(void) {
    tracker_reset();
    nfa_match greedy, lazy;
    EXEC_MATCH("a+", "aaa", greedy);
    EXEC_MATCH("a+?", "aaa", lazy);
    /* Greedy matches "aaa", lazy matches "a" */
    ASSERT(greedy.matched);
    ASSERT(lazy.matched);
    ASSERT_SIZE_EQ(greedy.end, 3);
    ASSERT_SIZE_EQ(lazy.end, 1);
    ASSERT(greedy.end != lazy.end);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_greedy_vs_lazy_question(void) {
    tracker_reset();
    nfa_match greedy, lazy;
    EXEC_MATCH("a?", "a", greedy);
    EXEC_MATCH("a??", "a", lazy);
    /* Greedy matches "a", lazy matches empty */
    ASSERT(greedy.matched);
    ASSERT(lazy.matched);
    ASSERT_SIZE_EQ(greedy.end, 1);
    ASSERT_SIZE_EQ(lazy.end, 0);
    ASSERT(greedy.end != lazy.end);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Non-capturing group tests (US-002 extension) ──────────────────────── */

static int test_parse_noncapture_group(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(?:ab)+", ast);
    /* Should be Repeat(Concat([Literal('a'), Literal('b')]), min=1, max=-1) */
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 1);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT(ast->repeat.greedy);
    /* Inner is Concat, NOT Group */
    nfa_ast* inner = ast->repeat.child;
    ASSERT_INT_EQ(inner->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(inner->concat.count, 2);
    ASSERT_INT_EQ(inner->concat.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(inner->concat.children[0]->literal.bytes[0], 'a');
    ASSERT_INT_EQ(inner->concat.children[1]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(inner->concat.children[1]->literal.bytes[0], 'b');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_noncapture_alt(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(?:a|b)c", ast);
    /* Should be Concat([Alt([Literal('a'), Literal('b')]), Literal('c')]) */
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 2);
    ASSERT_INT_EQ(ast->concat.children[0]->type, NFA_AST_ALT);
    nfa_ast* alt = ast->concat.children[0];
    ASSERT_SIZE_EQ(alt->alt.count, 2);
    ASSERT_INT_EQ(alt->alt.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(alt->alt.children[0]->literal.bytes[0], 'a');
    ASSERT_INT_EQ(alt->alt.children[1]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(alt->alt.children[1]->literal.bytes[0], 'b');
    ASSERT_INT_EQ(ast->concat.children[1]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->concat.children[1]->literal.bytes[0], 'c');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_noncapture_mixed(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(a)(?:b)(c)", ast);
    /* Should be Concat([Group(Literal('a'),0), Literal('b'), Group(Literal('c'),1)]) */
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 3);
    /* Group 0: (a) */
    nfa_ast* g0 = ast->concat.children[0];
    ASSERT_INT_EQ(g0->type, NFA_AST_GROUP);
    ASSERT_INT_EQ(g0->group.index, 0);
    ASSERT_INT_EQ(g0->group.child->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(g0->group.child->literal.bytes[0], 'a');
    /* Non-capturing: (?:b) → bare Literal, no Group wrapper */
    nfa_ast* nc = ast->concat.children[1];
    ASSERT_INT_EQ(nc->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(nc->literal.bytes[0], 'b');
    /* Group 1: (c) */
    nfa_ast* g1 = ast->concat.children[2];
    ASSERT_INT_EQ(g1->type, NFA_AST_GROUP);
    ASSERT_INT_EQ(g1->group.index, 1);
    ASSERT_INT_EQ(g1->group.child->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(g1->group.child->literal.bytes[0], 'c');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_noncapture_no_save(void) {
    tracker_reset();
    nfa_program *prog_cap, *prog_noncap;
    COMPILE_OK("(ab)+", prog_cap);
    COMPILE_OK("(?:ab)+", prog_noncap);
    /* Non-capturing should have fewer instructions (no SAVE) */
    ASSERT(prog_noncap->len < prog_cap->len);
    ASSERT_SIZE_EQ(prog_noncap->n_captures, 0);
    ASSERT_SIZE_EQ(prog_cap->n_captures, 2);
    /* Verify no SAVE instructions in noncapture version */
    for (size_t i = 0; i < prog_noncap->len; i++) {
        ASSERT(prog_noncap->code[i].type != NFA_INST_SAVE);
    }
    nfa_program_free(prog_cap);
    nfa_program_free(prog_noncap);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_noncapture_mixed_captures(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("(a)(?:b)(c)", prog);
    /* n_captures = 4 (2 capturing groups x 2 slots) */
    ASSERT_SIZE_EQ(prog->n_captures, 4);
    /* Count SAVE instructions: should be 4 (open/close for each capturing group) */
    int save_count = 0;
    for (size_t i = 0; i < prog->len; i++) {
        if (prog->code[i].type == NFA_INST_SAVE) save_count++;
    }
    ASSERT_INT_EQ(save_count, 4);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_noncapture_ab_plus(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?:ab)+", "ababab", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_noncapture_alt(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?:a|b)c", "bc", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 2);
    EXEC_MATCH("(?:a|b)c", "ac", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 2);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_noncapture_mixed(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(a)(?:b)(c)", "abc", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_err_invalid_group(void) {
    tracker_reset();
    PARSE_ERR("(?x)", NFA_ERR_INVALID_GROUP);
    PARSE_ERR("(?)", NFA_ERR_INVALID_GROUP);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Counted repetition tests (US-003 extension) ──────────────────────── */

static int test_parse_counted_exact(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a{3}", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 3);
    ASSERT_INT_EQ(ast->repeat.max, 3);
    ASSERT(ast->repeat.greedy);
    ASSERT_INT_EQ(ast->repeat.child->type, NFA_AST_LITERAL);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_counted_range(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a{2,4}", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 2);
    ASSERT_INT_EQ(ast->repeat.max, 4);
    ASSERT(ast->repeat.greedy);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_counted_unbounded(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a{2,}", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 2);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT(ast->repeat.greedy);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_counted_equiv(void) {
    tracker_reset();
    nfa_ast* ast;
    /* a{0,1} equiv to a? */
    PARSE_OK("a{0,1}", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 0);
    ASSERT_INT_EQ(ast->repeat.max, 1);
    nfa_ast_free(ast);
    /* a{0,} equiv to a* */
    PARSE_OK("a{0,}", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 0);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_counted_lazy(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a{2,4}?", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 2);
    ASSERT_INT_EQ(ast->repeat.max, 4);
    ASSERT(!ast->repeat.greedy);
    nfa_ast_free(ast);
    /* a{2,}? */
    PARSE_OK("a{2,}?", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_REPEAT);
    ASSERT_INT_EQ(ast->repeat.min, 2);
    ASSERT_INT_EQ(ast->repeat.max, -1);
    ASSERT(!ast->repeat.greedy);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_counted_literal(void) {
    tracker_reset();
    nfa_ast* ast;
    /* {abc} is not a valid repetition, { treated as literal */
    PARSE_OK("{abc}", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 5);
    ASSERT_INT_EQ(ast->concat.children[0]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->concat.children[0]->literal.bytes[0], '{');
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_parse_counted_err_range(void) {
    tracker_reset();
    PARSE_ERR("a{3,1}", NFA_ERR_REPEAT_RANGE);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_compile_counted_exact(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a{3}", prog);
    /* 3 LITERAL + MATCH = 4 */
    ASSERT_SIZE_EQ(prog->len, 4);
    ASSERT_INT_EQ(prog->code[0].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[1].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[2].type, NFA_INST_LITERAL);
    ASSERT_INT_EQ(prog->code[3].type, NFA_INST_MATCH);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_counted_exact(void) {
    tracker_reset();
    nfa_match m;
    /* a{3} on "aaa" matches */
    EXEC_MATCH("a{3}", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    /* a{3} on "aa" does not match */
    EXEC_MATCH("a{3}", "aa", m);
    ASSERT(!m.matched);
    /* a{3} on "aaaa" matches first 3 */
    EXEC_MATCH("a{3}", "aaaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_counted_range(void) {
    tracker_reset();
    nfa_match m;
    /* a{2,4} greedy on "aaaaa" matches longest (4) */
    EXEC_MATCH("a{2,4}", "aaaaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 4);
    /* a{2,4} on "a" no match (need at least 2) */
    EXEC_MATCH("a{2,4}", "a", m);
    ASSERT(!m.matched);
    /* a{2,4} on "aa" matches 2 */
    EXEC_MATCH("a{2,4}", "aa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 2);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_counted_unbounded(void) {
    tracker_reset();
    nfa_match m;
    /* a{2,} greedy on "aaaaa" matches all */
    EXEC_MATCH("a{2,}", "aaaaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 5);
    /* a{2,} on "a" no match */
    EXEC_MATCH("a{2,}", "a", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_counted_lazy(void) {
    tracker_reset();
    nfa_match m;
    /* a{2,4}? lazy on "aaaaa" matches shortest (2) */
    EXEC_MATCH("a{2,4}?", "aaaaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 2);
    /* a{2,}? lazy on "aaaaa" matches shortest (2) */
    EXEC_MATCH("a{2,}?", "aaaaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 2);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_exec_counted_literal_brace(void) {
    tracker_reset();
    nfa_match m;
    /* {abc} matches literal "{abc}" */
    EXEC_MATCH("{abc}", "x{abc}y", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 1);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── US-004 extension: Word boundary anchor tests ────────────────────── */

/* Parse \b as ANCHOR_WORD */
static int test_parse_word_boundary(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\b", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->anchor, NFA_ANCHOR_WORD);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Parse \B as ANCHOR_NON_WORD */
static int test_parse_non_word_boundary(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\B", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->anchor, NFA_ANCHOR_NON_WORD);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Parse \bfoo\b — concat of anchor, literals, anchor */
static int test_parse_word_boundary_in_pattern(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\bfoo\\b", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    ASSERT_SIZE_EQ(ast->concat.count, 5); /* \b f o o \b */
    ASSERT_INT_EQ(ast->concat.children[0]->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->concat.children[0]->anchor, NFA_ANCHOR_WORD);
    ASSERT_INT_EQ(ast->concat.children[1]->type, NFA_AST_LITERAL);
    ASSERT_INT_EQ(ast->concat.children[4]->type, NFA_AST_ANCHOR);
    ASSERT_INT_EQ(ast->concat.children[4]->anchor, NFA_ANCHOR_WORD);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Compile \b — should produce ANCHOR_WORD instruction */
static int test_compile_word_boundary(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("\\b", ast);
    nfa_program* prog = NULL;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    ASSERT(prog != NULL);
    /* Should have ANCHOR_WORD + MATCH */
    ASSERT(prog->len >= 2);
    ASSERT_INT_EQ(prog->code[prog->start].type, NFA_INST_ANCHOR_WORD);
    nfa_ast_free(ast);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \bword\b matches "word" in "a word here" */
static int test_exec_word_boundary_match(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\bword\\b", "a word here", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 2);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \bword\b does NOT match in "sword" */
static int test_exec_word_boundary_no_match_prefix(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\bword\\b", "sword", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \bword\b does NOT match in "wordy" */
static int test_exec_word_boundary_no_match_suffix(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\bword\\b", "wordy", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \bword\b matches "word" at start of input */
static int test_exec_word_boundary_at_start(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\bword\\b", "word here", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 4);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \bword\b matches "word" at end of input */
static int test_exec_word_boundary_at_end(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\bword\\b", "the word", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 4);
    ASSERT_SIZE_EQ(m.end, 8);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \bword\b matches when word is entire input */
static int test_exec_word_boundary_exact(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\bword\\b", "word", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 4);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \B matches at non-word boundaries */
static int test_exec_non_word_boundary(void) {
    tracker_reset();
    nfa_match m;
    /* \Boo matches "oo" in "foo" (position between f and o is not a boundary) */
    EXEC_MATCH("\\Boo", "foo", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 1);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \B does not match at word start */
static int test_exec_non_word_boundary_no_match(void) {
    tracker_reset();
    nfa_match m;
    /* \Bfoo should NOT match "foo" at start (start of input → non-word, f = word → boundary) */
    EXEC_MATCH("\\Bfoo\\B", " foo ", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \b with underscore (underscore is a word char) */
static int test_exec_word_boundary_underscore(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\b_var\\b", "x _var y", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 2);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \b with digits */
static int test_exec_word_boundary_digits(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("\\b42\\b", "x 42 y", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 2);
    ASSERT_SIZE_EQ(m.end, 4);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* \bfoo\b across chunk boundaries (small-chunk adapter) */
static int test_exec_word_boundary_cross_chunk(void) {
    tracker_reset();
    const char* text = "a word here";
    nfa_ast* ast = NULL;
    int rc = nfa_parse((const uint8_t*)"\\bword\\b", 8, &ast);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_program* prog = NULL;
    rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    /* Use small-chunk adapter with chunk_size=2 */
    nfa_input inp = _test_small_chunk_input((const uint8_t*)text, strlen(text), 2);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_free(&inp);
    nfa_program_free(prog);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 2);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Inline flags tests (US-005 extension) ─────────────────────────────── */

/* (?s)a.b matches "a\nb" — dot-all mode */
static int test_exec_dot_all_matches_newline(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?s)a.b", "a\nb", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Plain a.b does NOT match "a\nb" (no dot-all) */
static int test_exec_dot_no_newline_baseline(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("a.b", "a\nb", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* (?m)^foo$ works same as ^foo$ — multiline is already default */
static int test_exec_multiline_noop(void) {
    tracker_reset();
    nfa_match m1, m2;
    /* With (?m) flag */
    EXEC_MATCH("(?m)^foo$", "bar\nfoo", m1);
    /* Without flag */
    EXEC_MATCH("^foo$", "bar\nfoo", m2);
    ASSERT(m1.matched);
    ASSERT(m2.matched);
    ASSERT_SIZE_EQ(m1.start, m2.start);
    ASSERT_SIZE_EQ(m1.end, m2.end);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Scoped flags: (?s:a.b)c.d — first dot matches \n, second does not */
static int test_exec_scoped_dot_all(void) {
    tracker_reset();
    nfa_match m;
    /* First dot in (?s:...) should match \n, second dot outside should NOT */
    EXEC_MATCH("(?s:a.b)c.d", "a\nbcxd", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 6);

    /* Second dot should NOT match \n */
    EXEC_MATCH("(?s:a.b)c.d", "a\nbc\nd", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Combined flags: (?sm) parsed correctly — dot-all active */
static int test_exec_combined_flags(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?sm)a.b", "a\nb", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Parser rejects unsupported flags (?x) */
static int test_err_unsupported_flag(void) {
    tracker_reset();
    PARSE_ERR("(?x)", NFA_ERR_INVALID_GROUP);
    PARSE_ERR("(?x:abc)", NFA_ERR_INVALID_GROUP);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Parse (?s) — sets dot_all on subsequent dots in AST */
static int test_parse_dot_all_flag(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(?s)a.b", ast);
    /* Should be concat: [empty_concat, literal_a, dot(dot_all=true), literal_b] */
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    /* Find the DOT node */
    bool found_dot = false;
    for (size_t i = 0; i < ast->concat.count; i++) {
        if (ast->concat.children[i]->type == NFA_AST_DOT) {
            ASSERT(ast->concat.children[i]->dot.dot_all);
            found_dot = true;
        }
    }
    ASSERT(found_dot);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Parse plain dot without (?s) — dot_all=false */
static int test_parse_dot_no_flag(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("a.b", ast);
    /* Find the DOT node — should have dot_all=false */
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    bool found_dot = false;
    for (size_t i = 0; i < ast->concat.count; i++) {
        if (ast->concat.children[i]->type == NFA_AST_DOT) {
            ASSERT(!ast->concat.children[i]->dot.dot_all);
            found_dot = true;
        }
    }
    ASSERT(found_dot);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Scoped: (?s:a.b) followed by c.d — scope ends after group */
static int test_parse_scoped_flag_reset(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(?s:a.b)c.d", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    /* Walk all DOT nodes: first should be dot_all, second should not */
    int dot_count = 0;
    for (size_t i = 0; i < ast->concat.count; i++) {
        nfa_ast* child = ast->concat.children[i];
        if (child->type == NFA_AST_DOT) {
            if (dot_count == 0) ASSERT(!child->dot.dot_all); /* second dot, outside scope */
            dot_count++;
        } else if (child->type == NFA_AST_CONCAT) {
            /* The scoped group (?s:a.b) is inlined as concat */
            for (size_t j = 0; j < child->concat.count; j++) {
                if (child->concat.children[j]->type == NFA_AST_DOT) {
                    ASSERT(child->concat.children[j]->dot.dot_all);
                    dot_count++;
                }
            }
        }
    }
    ASSERT(dot_count >= 2);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Compile: (?s)a.b emits ANY_ALL instruction */
static int test_compile_dot_all(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("(?s)a.b", prog);
    /* Find the ANY_ALL instruction */
    bool found = false;
    for (size_t i = 0; i < prog->len; i++) {
        if (prog->code[i].type == NFA_INST_ANY_ALL) { found = true; break; }
    }
    ASSERT(found);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Compile: plain a.b emits ANY (not ANY_ALL) */
static int test_compile_dot_no_all(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("a.b", prog);
    /* Should have ANY but not ANY_ALL */
    bool has_any = false, has_any_all = false;
    for (size_t i = 0; i < prog->len; i++) {
        if (prog->code[i].type == NFA_INST_ANY) has_any = true;
        if (prog->code[i].type == NFA_INST_ANY_ALL) has_any_all = true;
    }
    ASSERT(has_any);
    ASSERT(!has_any_all);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* (?s)a.b matches across \n in longer input */
static int test_exec_dot_all_in_context(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?s)a.b", "xxa\nbyy", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 2);
    ASSERT_SIZE_EQ(m.end, 5);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* (?s) dot-all with star: .* matches across newlines */
static int test_exec_dot_all_star(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?s)a.*b", "a\n\n\nb", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 5);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── US-006 extension: Case-insensitive flag (?i) tests ──────────────── */

/* Parse: (?i)hello — literals should have icase=true */
static int test_parse_icase_literal(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(?i)hello", ast);
    /* Concat: [empty_concat, literal_h, literal_e, literal_l, literal_l, literal_o]
     * All literals after (?i) should have icase=true */
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    bool found_icase_literal = false;
    for (size_t i = 0; i < ast->concat.count; i++) {
        if (ast->concat.children[i]->type == NFA_AST_LITERAL) {
            ASSERT(ast->concat.children[i]->literal.icase);
            found_icase_literal = true;
        }
    }
    ASSERT(found_icase_literal);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Parse: plain hello — literals should have icase=false */
static int test_parse_no_icase_literal(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("hello", ast);
    /* Single literal "hello" (or concat of 5 single-byte literals) */
    if (ast->type == NFA_AST_LITERAL) {
        ASSERT(!ast->literal.icase);
    } else {
        ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
        for (size_t i = 0; i < ast->concat.count; i++) {
            if (ast->concat.children[i]->type == NFA_AST_LITERAL)
                ASSERT(!ast->concat.children[i]->literal.icase);
        }
    }
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Parse: (?i)[a-z] — char class should have icase=true */
static int test_parse_icase_char_class(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(?i)[a-z]", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    bool found_cc = false;
    for (size_t i = 0; i < ast->concat.count; i++) {
        if (ast->concat.children[i]->type == NFA_AST_CHAR_CLASS) {
            ASSERT(ast->concat.children[i]->char_class.icase);
            found_cc = true;
        }
    }
    ASSERT(found_cc);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Scoped: (?i:abc)def — abc icase, def not */
static int test_parse_scoped_icase(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(?i:abc)def", ast);
    ASSERT_INT_EQ(ast->type, NFA_AST_CONCAT);
    /* Inner (?i:abc) is inlined as concat, literals outside are non-icase */
    bool found_icase = false;
    bool found_normal = false;
    for (size_t i = 0; i < ast->concat.count; i++) {
        nfa_ast* child = ast->concat.children[i];
        if (child->type == NFA_AST_LITERAL) {
            ASSERT(!child->literal.icase);
            found_normal = true;
        } else if (child->type == NFA_AST_CONCAT) {
            for (size_t j = 0; j < child->concat.count; j++) {
                if (child->concat.children[j]->type == NFA_AST_LITERAL) {
                    ASSERT(child->concat.children[j]->literal.icase);
                    found_icase = true;
                }
            }
        }
    }
    ASSERT(found_icase);
    ASSERT(found_normal);
    nfa_ast_free(ast);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Compile: (?i)a produces RANGE instructions for case-insensitive match */
static int test_compile_icase_literal(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("(?i)a", prog);
    /* Should have RANGE instructions for both 'a' and 'A' */
    bool has_range_a = false, has_range_A = false;
    for (size_t i = 0; i < prog->len; i++) {
        if (prog->code[i].type == NFA_INST_RANGE) {
            if (prog->code[i].lo == 'a' && prog->code[i].hi == 'a') has_range_a = true;
            if (prog->code[i].lo == 'A' && prog->code[i].hi == 'A') has_range_A = true;
        }
    }
    ASSERT(has_range_a);
    ASSERT(has_range_A);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Compile: (?i)[a-z] produces a CLASS that covers a-z and A-Z */
static int test_compile_icase_char_class(void) {
    tracker_reset();
    nfa_program* prog;
    COMPILE_OK("(?i)[a-z]", prog);
    /* CLASS holding both [a-z] and [A-Z] */
    bool has_lower = false, has_upper = false;
    for (size_t i = 0; i < prog->len; i++) {
        nfa_inst* ci = &prog->code[i];
        if (ci->type == NFA_INST_CLASS) {
            for (size_t k = 0; k < ci->n_ranges; k++) {
                if (ci->ranges[k].lo == 'a' && ci->ranges[k].hi == 'z') has_lower = true;
                if (ci->ranges[k].lo == 'A' && ci->ranges[k].hi == 'Z') has_upper = true;
            }
        }
    }
    ASSERT(has_lower);
    ASSERT(has_upper);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* (?i)hello matches "HELLO", "Hello", "hElLo" */
static int test_exec_icase_hello(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?i)hello", "HELLO", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 5);

    EXEC_MATCH("(?i)hello", "Hello", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 5);

    EXEC_MATCH("(?i)hello", "hElLo", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 5);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* (?i)[a-z]+ matches "ABC" */
static int test_exec_icase_char_class_range(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?i)[a-z]+", "ABC", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Scoped: (?i:abc)def — "ABCdef" matches, "ABCDef" does not */
static int test_exec_scoped_icase(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?i:abc)def", "ABCdef", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 6);

    EXEC_MATCH("(?i:abc)def", "ABCDef", m);
    ASSERT(!m.matched);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Combined: (?is)a.b matches "A\nB" */
static int test_exec_combined_icase_dotall(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?is)a.b", "A\nB", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Non-alpha characters unaffected: (?i)a1b matches "A1B" but not "a2b" */
static int test_exec_icase_non_alpha(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?i)a1b", "A1B", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);

    EXEC_MATCH("(?i)a1b", "a2b", m);
    ASSERT(!m.matched);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* (?i)[A-F]+ matches lowercase "abcdef" */
static int test_exec_icase_upper_range(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?i)[A-F]+", "abcdef", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 6);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* (?i) inside context: find case-insensitive match in longer text */
static int test_exec_icase_in_context(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH("(?i)world", "Hello WORLD!", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 6);
    ASSERT_SIZE_EQ(m.end, 11);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Capture group extraction tests (US-007) ─────────────────────────── */

/* Single group: (a) on "a" → captures [0,1) */
static int test_captures_single_group(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("(a)", "a", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);
    ASSERT_SIZE_EQ(m.n_captures, 2);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], 0); /* group 0 open */
    ASSERT_SIZE_EQ(m.captures[1], 1); /* group 0 close */
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Multiple groups: (a)(b) on "ab" → group 0=[0,1), group 1=[1,2) */
static int test_captures_multiple_groups(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("(a)(b)", "ab", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 2);
    ASSERT_SIZE_EQ(m.n_captures, 4);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], 0); /* group 0 open */
    ASSERT_SIZE_EQ(m.captures[1], 1); /* group 0 close */
    ASSERT_SIZE_EQ(m.captures[2], 1); /* group 1 open */
    ASSERT_SIZE_EQ(m.captures[3], 2); /* group 1 close */
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Nested groups: ((a)b) on "ab" → group 0=[0,2), group 1=[0,1) */
static int test_captures_nested_groups(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("((a)b)", "ab", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 2);
    ASSERT_SIZE_EQ(m.n_captures, 4);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], 0); /* group 0 open */
    ASSERT_SIZE_EQ(m.captures[1], 2); /* group 0 close */
    ASSERT_SIZE_EQ(m.captures[2], 0); /* group 1 open */
    ASSERT_SIZE_EQ(m.captures[3], 1); /* group 1 close */
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Alternation with groups: (a)|(b) on "b" → group 0 unmatched, group 1=[0,1) */
static int test_captures_alt_groups(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("(a)|(b)", "b", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);
    ASSERT_SIZE_EQ(m.n_captures, 4);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], SIZE_MAX); /* group 0 open - unmatched */
    ASSERT_SIZE_EQ(m.captures[1], SIZE_MAX); /* group 0 close - unmatched */
    ASSERT_SIZE_EQ(m.captures[2], 0);        /* group 1 open */
    ASSERT_SIZE_EQ(m.captures[3], 1);        /* group 1 close */
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Non-capturing group: (?:a) produces no capture slots */
static int test_captures_noncapture_none(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("(?:a)+", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.n_captures, 0);
    ASSERT(m.captures == NULL);
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Mixed capturing and non-capturing: (a)(?:b)(c) on "abc" */
static int test_captures_mixed_groups(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("(a)(?:b)(c)", "abc", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.n_captures, 4);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], 0); /* group 0 = 'a' */
    ASSERT_SIZE_EQ(m.captures[1], 1);
    ASSERT_SIZE_EQ(m.captures[2], 2); /* group 1 = 'c' */
    ASSERT_SIZE_EQ(m.captures[3], 3);
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Unmatched optional group: (a)?b on "b" → group 0 is SIZE_MAX */
static int test_captures_unmatched_optional(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("(a)?b", "b", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);
    ASSERT_SIZE_EQ(m.n_captures, 2);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], SIZE_MAX);
    ASSERT_SIZE_EQ(m.captures[1], SIZE_MAX);
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* No groups at all: no capture slots */
static int test_captures_no_groups(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("abc", "abc", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.n_captures, 0);
    ASSERT(m.captures == NULL);
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Match in middle of string: x(ab)y on "xxaby" → group at [2,4) */
static int test_captures_middle_match(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("x(ab)y", "xxaby", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 1);
    ASSERT_SIZE_EQ(m.end, 5);
    ASSERT_SIZE_EQ(m.n_captures, 2);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], 2);  /* group 0 open */
    ASSERT_SIZE_EQ(m.captures[1], 4);  /* group 0 close */
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Iterator returns correct captures for each match */
static int test_captures_iterator(void) {
    tracker_reset();
    nfa_ast* ast;
    PARSE_OK("(\\d+)", ast);
    nfa_program* prog;
    int rc = nfa_compile(ast, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    nfa_ast_free(ast);

    const uint8_t text[] = "a12b345c6";
    nfa_input inp = nfa_input_from_buf(text, 9);
    nfa_iter it = nfa_iter_new(prog, inp);

    /* Match 1: "12" at [1,3), group 0 = [1,3) */
    nfa_match m1 = nfa_iter_next(&it);
    ASSERT(m1.matched);
    ASSERT_SIZE_EQ(m1.start, 1);
    ASSERT_SIZE_EQ(m1.end, 3);
    ASSERT_SIZE_EQ(m1.n_captures, 2);
    ASSERT(m1.captures != NULL);
    ASSERT_SIZE_EQ(m1.captures[0], 1);
    ASSERT_SIZE_EQ(m1.captures[1], 3);
    nfa_match_free(&m1);

    /* Match 2: "345" at [4,7), group 0 = [4,7) */
    nfa_match m2 = nfa_iter_next(&it);
    ASSERT(m2.matched);
    ASSERT_SIZE_EQ(m2.start, 4);
    ASSERT_SIZE_EQ(m2.end, 7);
    ASSERT_SIZE_EQ(m2.n_captures, 2);
    ASSERT(m2.captures != NULL);
    ASSERT_SIZE_EQ(m2.captures[0], 4);
    ASSERT_SIZE_EQ(m2.captures[1], 7);
    nfa_match_free(&m2);

    /* Match 3: "6" at [8,9), group 0 = [8,9) */
    nfa_match m3 = nfa_iter_next(&it);
    ASSERT(m3.matched);
    ASSERT_SIZE_EQ(m3.start, 8);
    ASSERT_SIZE_EQ(m3.end, 9);
    ASSERT_SIZE_EQ(m3.n_captures, 2);
    ASSERT(m3.captures != NULL);
    ASSERT_SIZE_EQ(m3.captures[0], 8);
    ASSERT_SIZE_EQ(m3.captures[1], 9);
    nfa_match_free(&m3);

    /* No more matches */
    nfa_match m4 = nfa_iter_next(&it);
    ASSERT(!m4.matched);
    nfa_match_free(&m4);

    nfa_iter_free(&it);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Group with repetition: (a)+ on "aaa" — group tracks last iteration */
static int test_captures_repeated_group(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("(a)+", "aaa", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 3);
    ASSERT_SIZE_EQ(m.n_captures, 2);
    ASSERT(m.captures != NULL);
    /* In leftmost-longest greedy match, SAVE records position as thread evolves.
     * With Thompson NFA, the group captures reflect the last iteration. */
    ASSERT_SIZE_EQ(m.captures[0], 2);  /* last 'a' open */
    ASSERT_SIZE_EQ(m.captures[1], 3);  /* last 'a' close */
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* nfa_match_free is no-op on pattern with no groups */
static int test_captures_free_noop(void) {
    tracker_reset();
    nfa_match m;
    EXEC_MATCH_CAPS("abc", "abc", m);
    ASSERT(m.matched);
    ASSERT(m.captures == NULL);
    nfa_match_free(&m); /* should be no-op */
    nfa_match_free(&m); /* double-free safe */
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Arena-backed nfa_compile_pattern tests ───────────────────────────── */

/* Helper: compile pattern via arena pipeline, exec, free */
#define EXEC_ARENA(pat, text, out_match)                                           \
  do {                                                                              \
    nfa_program* _xprog = NULL;                                                     \
    int _xrc = nfa_compile_pattern((const uint8_t*)(pat), strlen(pat), &_xprog);    \
    ASSERT_INT_EQ(_xrc, NFA_OK);                                                    \
    ASSERT(_xprog != NULL);                                                          \
    nfa_input _xinp = nfa_input_from_buf((const uint8_t*)(text), strlen(text));      \
    (out_match) = nfa_exec(_xprog, &_xinp);                                         \
    nfa_input_free(&_xinp);                                                          \
    nfa_program_free(_xprog);                                                        \
    nfa_match_free(&(out_match));                                                    \
  } while (0)

#define EXEC_ARENA_CAPS(pat, text, out_match)                                      \
  do {                                                                              \
    nfa_program* _xprog = NULL;                                                     \
    int _xrc = nfa_compile_pattern((const uint8_t*)(pat), strlen(pat), &_xprog);    \
    ASSERT_INT_EQ(_xrc, NFA_OK);                                                    \
    ASSERT(_xprog != NULL);                                                          \
    nfa_input _xinp = nfa_input_from_buf((const uint8_t*)(text), strlen(text));      \
    (out_match) = nfa_exec(_xprog, &_xinp);                                         \
    nfa_input_free(&_xinp);                                                          \
    nfa_program_free(_xprog);                                                        \
  } while (0)

static int test_arena_literal(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("hello", "say hello world", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_concat(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("abc", "xabcy", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_alt(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("cat|dog", "I have a dog", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_star(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("ab*c", "ac", m);
    ASSERT(m.matched);
    EXEC_ARENA("ab*c", "abbbbc", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_plus(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("ab+c", "ac", m);
    ASSERT(!m.matched);
    EXEC_ARENA("ab+c", "abc", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_question(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("ab?c", "ac", m);
    ASSERT(m.matched);
    EXEC_ARENA("ab?c", "abc", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_anchors(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("^hello$", "hello", m);
    ASSERT(m.matched);
    EXEC_ARENA("^hello$", "say hello", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_char_class(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("[a-z]+", "hello123", m);
    ASSERT(m.matched);
    EXEC_ARENA("[^0-9]+", "abc", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_shorthand(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("\\d+", "abc123def", m);
    ASSERT(m.matched);
    EXEC_ARENA("\\w+", "hello_world", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_captures(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA_CAPS("(a)(b)(c)", "abc", m);
    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.n_captures, 6);
    ASSERT(m.captures != NULL);
    ASSERT_SIZE_EQ(m.captures[0], 0); /* group 0 open */
    ASSERT_SIZE_EQ(m.captures[1], 1); /* group 0 close */
    ASSERT_SIZE_EQ(m.captures[2], 1); /* group 1 open */
    ASSERT_SIZE_EQ(m.captures[3], 2); /* group 1 close */
    ASSERT_SIZE_EQ(m.captures[4], 2); /* group 2 open */
    ASSERT_SIZE_EQ(m.captures[5], 3); /* group 2 close */
    nfa_match_free(&m);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_icase(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("(?i)hello", "HELLO", m);
    ASSERT(m.matched);
    EXEC_ARENA("(?i)[a-z]+", "ABC", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_dot_all(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("(?s)a.b", "a\nb", m);
    ASSERT(m.matched);
    EXEC_ARENA("a.b", "a\nb", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_error_cases(void) {
    tracker_reset();
    nfa_program* prog = NULL;

    /* Unmatched paren */
    int rc = nfa_compile_pattern((const uint8_t*)"(abc", 4, &prog);
    ASSERT_INT_EQ(rc, NFA_ERR_UNMATCHED_LPAREN);
    ASSERT(prog == NULL);

    /* Unmatched rparen */
    rc = nfa_compile_pattern((const uint8_t*)"abc)", 4, &prog);
    ASSERT_INT_EQ(rc, NFA_ERR_UNMATCHED_RPAREN);
    ASSERT(prog == NULL);

    /* Trailing backslash */
    rc = nfa_compile_pattern((const uint8_t*)"abc\\", 4, &prog);
    ASSERT_INT_EQ(rc, NFA_ERR_TRAILING_BACKSLASH);
    ASSERT(prog == NULL);

    /* Invalid escape */
    rc = nfa_compile_pattern((const uint8_t*)"\\z", 2, &prog);
    ASSERT_INT_EQ(rc, NFA_ERR_INVALID_ESCAPE);
    ASSERT(prog == NULL);

    /* Unmatched bracket */
    rc = nfa_compile_pattern((const uint8_t*)"[abc", 4, &prog);
    ASSERT_INT_EQ(rc, NFA_ERR_UNMATCHED_LBRACKET);
    ASSERT(prog == NULL);

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_matches_classic(void) {
    tracker_reset();
    /* Verify arena pipeline produces identical results to classic pipeline */
    const char* patterns[] = {
        "a(b|c)d", "(a+)(b*)", "^\\d{2,4}$", "(?:ab)+c",
        "a.*?b", "[A-Z][a-z]+", "\\bword\\b", NULL
    };
    const char* texts[] = {
        "abd", "aab", "123", "ababc",
        "aXXb", "Hello", "a word here", NULL
    };

    for (int i = 0; patterns[i]; i++) {
        const char* pat  = patterns[i];
        const char* text = texts[i];

        /* Classic path */
        nfa_ast* ast = NULL;
        int rc1 = nfa_parse((const uint8_t*)pat, strlen(pat), &ast);
        ASSERT_INT_EQ(rc1, NFA_OK);
        nfa_program* prog1 = NULL;
        rc1 = nfa_compile(ast, &prog1);
        ASSERT_INT_EQ(rc1, NFA_OK);
        nfa_ast_free(ast);

        nfa_input inp1 = nfa_input_from_buf((const uint8_t*)text, strlen(text));
        nfa_match m1 = nfa_exec(prog1, &inp1);
        nfa_input_free(&inp1);

        /* Arena path */
        nfa_program* prog2 = NULL;
        int rc2 = nfa_compile_pattern((const uint8_t*)pat, strlen(pat), &prog2);
        ASSERT_INT_EQ(rc2, NFA_OK);

        nfa_input inp2 = nfa_input_from_buf((const uint8_t*)text, strlen(text));
        nfa_match m2 = nfa_exec(prog2, &inp2);
        nfa_input_free(&inp2);

        /* Compare results */
        ASSERT(m1.matched == m2.matched);
        if (m1.matched) {
            ASSERT_SIZE_EQ(m2.start, m1.start);
            ASSERT_SIZE_EQ(m2.end, m1.end);
            ASSERT_SIZE_EQ(m2.n_captures, m1.n_captures);
            for (size_t j = 0; j < m1.n_captures; j++) {
                ASSERT_SIZE_EQ(m2.captures[j], m1.captures[j]);
            }
        }

        nfa_match_free(&m1);
        nfa_match_free(&m2);
        nfa_program_free(prog1);
        nfa_program_free(prog2);
    }

    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_counted_rep(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("a{3}", "aaa", m);
    ASSERT(m.matched);
    EXEC_ARENA("a{2,4}", "aaaa", m);
    ASSERT(m.matched);
    EXEC_ARENA("a{2,}", "aaaaaaa", m);
    ASSERT(m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_word_boundary(void) {
    tracker_reset();
    nfa_match m;
    EXEC_ARENA("\\bfoo\\b", "a foo b", m);
    ASSERT(m.matched);
    EXEC_ARENA("\\bfoo\\b", "foobar", m);
    ASSERT(!m.matched);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

static int test_arena_empty_pattern(void) {
    tracker_reset();
    nfa_program* prog = NULL;
    int rc = nfa_compile_pattern((const uint8_t*)"", 0, &prog);
    ASSERT_INT_EQ(rc, NFA_OK);
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_buf((const uint8_t*)"abc", 3);
    nfa_match m = nfa_exec(prog, &inp);
    ASSERT(m.matched);
    nfa_match_free(&m);
    nfa_input_free(&inp);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Runner ──────────────────────────────────────────────────────────── */

int main(void) {
    printf("test_single_literal       "); if (!test_single_literal()) return 1;
    printf("test_utf8_literal         "); if (!test_utf8_literal()) return 1;
    printf("test_utf8_3byte           "); if (!test_utf8_3byte()) return 1;
    printf("test_utf8_4byte           "); if (!test_utf8_4byte()) return 1;
    printf("test_concatenation        "); if (!test_concatenation()) return 1;
    printf("test_alternation          "); if (!test_alternation()) return 1;
    printf("test_nested_alternation   "); if (!test_nested_alternation()) return 1;
    printf("test_alternation_concat   "); if (!test_alternation_concat()) return 1;
    printf("test_star                 "); if (!test_star()) return 1;
    printf("test_plus                 "); if (!test_plus()) return 1;
    printf("test_question             "); if (!test_question()) return 1;
    printf("test_nested_groups        "); if (!test_nested_groups()) return 1;
    printf("test_group_repeat         "); if (!test_group_repeat()) return 1;
    printf("test_group_alternation    "); if (!test_group_alternation()) return 1;
    printf("test_dot                  "); if (!test_dot()) return 1;
    printf("test_bol_anchor           "); if (!test_bol_anchor()) return 1;
    printf("test_eol_anchor           "); if (!test_eol_anchor()) return 1;
    printf("test_anchored_pattern     "); if (!test_anchored_pattern()) return 1;
    printf("test_escaped_dot          "); if (!test_escaped_dot()) return 1;
    printf("test_escaped_backslash    "); if (!test_escaped_backslash()) return 1;
    printf("test_escaped_pipe         "); if (!test_escaped_pipe()) return 1;
    printf("test_escaped_lparen       "); if (!test_escaped_lparen()) return 1;
    printf("test_escaped_rparen       "); if (!test_escaped_rparen()) return 1;
    printf("test_escaped_star         "); if (!test_escaped_star()) return 1;
    printf("test_escaped_plus         "); if (!test_escaped_plus()) return 1;
    printf("test_escaped_question     "); if (!test_escaped_question()) return 1;
    printf("test_err_unmatched_lparen "); if (!test_err_unmatched_lparen()) return 1;
    printf("test_err_unmatched_rparen "); if (!test_err_unmatched_rparen()) return 1;
    printf("test_err_trailing_backslash "); if (!test_err_trailing_backslash()) return 1;
    printf("test_err_invalid_escape   "); if (!test_err_invalid_escape()) return 1;
    printf("test_err_nested_unmatched "); if (!test_err_nested_unmatched()) return 1;
    printf("test_err_rparen_only      "); if (!test_err_rparen_only()) return 1;
    printf("test_empty_pattern        "); if (!test_empty_pattern()) return 1;
    printf("test_empty_group          "); if (!test_empty_group()) return 1;
    printf("test_empty_alternation    "); if (!test_empty_alternation()) return 1;
    printf("test_complex_pattern      "); if (!test_complex_pattern()) return 1;

    /* US-002: Character class tests */
    printf("test_char_class_explicit  "); if (!test_char_class_explicit()) return 1;
    printf("test_char_class_range     "); if (!test_char_class_range()) return 1;
    printf("test_char_class_multi_range "); if (!test_char_class_multi_range()) return 1;
    printf("test_char_class_negated   "); if (!test_char_class_negated()) return 1;
    printf("test_char_class_negated_range "); if (!test_char_class_negated_range()) return 1;
    printf("test_shorthand_d          "); if (!test_shorthand_d()) return 1;
    printf("test_shorthand_w          "); if (!test_shorthand_w()) return 1;
    printf("test_shorthand_s          "); if (!test_shorthand_s()) return 1;
    printf("test_shorthand_D          "); if (!test_shorthand_D()) return 1;
    printf("test_shorthand_W          "); if (!test_shorthand_W()) return 1;
    printf("test_shorthand_S          "); if (!test_shorthand_S()) return 1;
    printf("test_shorthand_in_class   "); if (!test_shorthand_in_class()) return 1;
    printf("test_char_class_literal_rbracket "); if (!test_char_class_literal_rbracket()) return 1;
    printf("test_char_class_literal_dash "); if (!test_char_class_literal_dash()) return 1;
    printf("test_char_class_dash_at_end "); if (!test_char_class_dash_at_end()) return 1;
    printf("test_err_unmatched_lbracket "); if (!test_err_unmatched_lbracket()) return 1;
    printf("test_char_class_in_pattern "); if (!test_char_class_in_pattern()) return 1;
    printf("test_shorthand_in_concat  "); if (!test_shorthand_in_concat()) return 1;

    /* US-003: Compiler tests */
    printf("test_compile_literal      "); if (!test_compile_literal()) return 1;
    printf("test_compile_concat       "); if (!test_compile_concat()) return 1;
    printf("test_compile_alt          "); if (!test_compile_alt()) return 1;
    printf("test_compile_star         "); if (!test_compile_star()) return 1;
    printf("test_compile_plus         "); if (!test_compile_plus()) return 1;
    printf("test_compile_question     "); if (!test_compile_question()) return 1;
    printf("test_compile_dot          "); if (!test_compile_dot()) return 1;
    printf("test_compile_anchor_bol   "); if (!test_compile_anchor_bol()) return 1;
    printf("test_compile_anchor_eol   "); if (!test_compile_anchor_eol()) return 1;
    printf("test_compile_char_class_single "); if (!test_compile_char_class_single()) return 1;
    printf("test_compile_char_class_multi "); if (!test_compile_char_class_multi()) return 1;
    printf("test_compile_negated_class "); if (!test_compile_negated_class()) return 1;
    printf("test_compile_negated_class_abc "); if (!test_compile_negated_class_abc()) return 1;
    printf("test_compile_group        "); if (!test_compile_group()) return 1;
    printf("test_compile_utf8_literal "); if (!test_compile_utf8_literal()) return 1;
    printf("test_compile_roundtrip_ab_or_c "); if (!test_compile_roundtrip_ab_or_c()) return 1;
    printf("test_compile_roundtrip_a_star "); if (!test_compile_roundtrip_a_star()) return 1;
    printf("test_compile_roundtrip_char_class "); if (!test_compile_roundtrip_char_class()) return 1;
    printf("test_compile_roundtrip_anchored "); if (!test_compile_roundtrip_anchored()) return 1;
    printf("test_compile_empty_pattern "); if (!test_compile_empty_pattern()) return 1;
    printf("test_compile_shorthand_d  "); if (!test_compile_shorthand_d()) return 1;
    printf("test_compile_nested_groups "); if (!test_compile_nested_groups()) return 1;
    printf("test_compile_linear_size  "); if (!test_compile_linear_size()) return 1;
    printf("test_compile_instruction_count_bound "); if (!test_compile_instruction_count_bound()) return 1;

    /* US-004: Input abstraction tests */
    printf("test_buf_chunk_data_and_length "); if (!test_buf_chunk_data_and_length()) return 1;
    printf("test_buf_byte_offset     "); if (!test_buf_byte_offset()) return 1;
    printf("test_buf_eof_after_first_chunk "); if (!test_buf_eof_after_first_chunk()) return 1;
    printf("test_buf_reset           "); if (!test_buf_reset()) return 1;
    printf("test_buf_seek            "); if (!test_buf_seek()) return 1;
    printf("test_buf_seek_then_reset "); if (!test_buf_seek_then_reset()) return 1;
    printf("test_buf_empty           "); if (!test_buf_empty()) return 1;
    printf("test_buf_seek_to_end     "); if (!test_buf_seek_to_end()) return 1;
    printf("test_buf_multiple_seek   "); if (!test_buf_multiple_seek()) return 1;

    /* US-005: Simulator tests */
    printf("test_exec_literal_match    "); if (!test_exec_literal_match()) return 1;
    printf("test_exec_no_match         "); if (!test_exec_no_match()) return 1;
    printf("test_exec_alternation      "); if (!test_exec_alternation()) return 1;
    printf("test_exec_star             "); if (!test_exec_star()) return 1;
    printf("test_exec_plus             "); if (!test_exec_plus()) return 1;
    printf("test_exec_question         "); if (!test_exec_question()) return 1;
    printf("test_exec_anchored_bol     "); if (!test_exec_anchored_bol()) return 1;
    printf("test_exec_anchored_bol_newline "); if (!test_exec_anchored_bol_newline()) return 1;
    printf("test_exec_anchored_bol_no_match "); if (!test_exec_anchored_bol_no_match()) return 1;
    printf("test_exec_anchored_eol     "); if (!test_exec_anchored_eol()) return 1;
    printf("test_exec_anchored_full    "); if (!test_exec_anchored_full()) return 1;
    printf("test_exec_match_at_start   "); if (!test_exec_match_at_start()) return 1;
    printf("test_exec_match_at_middle  "); if (!test_exec_match_at_middle()) return 1;
    printf("test_exec_match_at_end     "); if (!test_exec_match_at_end()) return 1;
    printf("test_exec_dot              "); if (!test_exec_dot()) return 1;
    printf("test_exec_dot_no_newline   "); if (!test_exec_dot_no_newline()) return 1;
    printf("test_exec_char_class_range "); if (!test_exec_char_class_range()) return 1;
    printf("test_exec_char_class_negated "); if (!test_exec_char_class_negated()) return 1;
    printf("test_exec_shorthand_d      "); if (!test_exec_shorthand_d()) return 1;
    printf("test_exec_shorthand_w      "); if (!test_exec_shorthand_w()) return 1;
    printf("test_exec_shorthand_s      "); if (!test_exec_shorthand_s()) return 1;
    printf("test_exec_cross_chunk      "); if (!test_exec_cross_chunk()) return 1;
    printf("test_exec_cross_chunk_single "); if (!test_exec_cross_chunk_single()) return 1;
    printf("test_exec_pathological     "); if (!test_exec_pathological()) return 1;
    printf("test_exec_zero_length_bol  "); if (!test_exec_zero_length_bol()) return 1;

    /* US-006: Iterator tests */
    printf("test_iter_multiple_matches "); if (!test_iter_multiple_matches()) return 1;
    printf("test_iter_adjacent_matches "); if (!test_iter_adjacent_matches()) return 1;
    printf("test_iter_zero_length_no_loop "); if (!test_iter_zero_length_no_loop()) return 1;
    printf("test_iter_no_matches       "); if (!test_iter_no_matches()) return 1;

    /* US-001 extension: Lazy quantifier tests */
    printf("test_parse_lazy_star       "); if (!test_parse_lazy_star()) return 1;
    printf("test_parse_lazy_plus       "); if (!test_parse_lazy_plus()) return 1;
    printf("test_parse_lazy_question   "); if (!test_parse_lazy_question()) return 1;
    printf("test_parse_lazy_no_preceding_atom "); if (!test_parse_lazy_no_preceding_atom()) return 1;
    printf("test_compile_lazy_star     "); if (!test_compile_lazy_star()) return 1;
    printf("test_exec_lazy_star_matches_empty "); if (!test_exec_lazy_star_matches_empty()) return 1;
    printf("test_exec_lazy_plus_matches_one "); if (!test_exec_lazy_plus_matches_one()) return 1;
    printf("test_exec_lazy_question_matches_empty "); if (!test_exec_lazy_question_matches_empty()) return 1;
    printf("test_exec_greedy_vs_lazy_star "); if (!test_exec_greedy_vs_lazy_star()) return 1;
    printf("test_exec_greedy_vs_lazy_plus "); if (!test_exec_greedy_vs_lazy_plus()) return 1;
    printf("test_exec_greedy_vs_lazy_question "); if (!test_exec_greedy_vs_lazy_question()) return 1;

    /* US-002 extension: Non-capturing group tests */
    printf("test_parse_noncapture_group "); if (!test_parse_noncapture_group()) return 1;
    printf("test_parse_noncapture_alt  "); if (!test_parse_noncapture_alt()) return 1;
    printf("test_parse_noncapture_mixed "); if (!test_parse_noncapture_mixed()) return 1;
    printf("test_compile_noncapture_no_save "); if (!test_compile_noncapture_no_save()) return 1;
    printf("test_compile_noncapture_mixed_captures "); if (!test_compile_noncapture_mixed_captures()) return 1;
    printf("test_exec_noncapture_ab_plus "); if (!test_exec_noncapture_ab_plus()) return 1;
    printf("test_exec_noncapture_alt   "); if (!test_exec_noncapture_alt()) return 1;
    printf("test_exec_noncapture_mixed "); if (!test_exec_noncapture_mixed()) return 1;
    printf("test_err_invalid_group     "); if (!test_err_invalid_group()) return 1;

    /* US-003 extension: Counted repetition tests */
    printf("test_parse_counted_exact   "); if (!test_parse_counted_exact()) return 1;
    printf("test_parse_counted_range   "); if (!test_parse_counted_range()) return 1;
    printf("test_parse_counted_unbounded "); if (!test_parse_counted_unbounded()) return 1;
    printf("test_parse_counted_equiv   "); if (!test_parse_counted_equiv()) return 1;
    printf("test_parse_counted_lazy    "); if (!test_parse_counted_lazy()) return 1;
    printf("test_parse_counted_literal "); if (!test_parse_counted_literal()) return 1;
    printf("test_parse_counted_err_range "); if (!test_parse_counted_err_range()) return 1;
    printf("test_compile_counted_exact "); if (!test_compile_counted_exact()) return 1;
    printf("test_exec_counted_exact    "); if (!test_exec_counted_exact()) return 1;
    printf("test_exec_counted_range    "); if (!test_exec_counted_range()) return 1;
    printf("test_exec_counted_unbounded "); if (!test_exec_counted_unbounded()) return 1;
    printf("test_exec_counted_lazy     "); if (!test_exec_counted_lazy()) return 1;
    printf("test_exec_counted_literal_brace "); if (!test_exec_counted_literal_brace()) return 1;

    /* US-004 extension: Word boundary anchor tests */
    printf("test_parse_word_boundary   "); if (!test_parse_word_boundary()) return 1;
    printf("test_parse_non_word_boundary "); if (!test_parse_non_word_boundary()) return 1;
    printf("test_parse_word_boundary_in_pattern "); if (!test_parse_word_boundary_in_pattern()) return 1;
    printf("test_compile_word_boundary "); if (!test_compile_word_boundary()) return 1;
    printf("test_exec_word_boundary_match "); if (!test_exec_word_boundary_match()) return 1;
    printf("test_exec_word_boundary_no_match_prefix "); if (!test_exec_word_boundary_no_match_prefix()) return 1;
    printf("test_exec_word_boundary_no_match_suffix "); if (!test_exec_word_boundary_no_match_suffix()) return 1;
    printf("test_exec_word_boundary_at_start "); if (!test_exec_word_boundary_at_start()) return 1;
    printf("test_exec_word_boundary_at_end "); if (!test_exec_word_boundary_at_end()) return 1;
    printf("test_exec_word_boundary_exact "); if (!test_exec_word_boundary_exact()) return 1;
    printf("test_exec_non_word_boundary "); if (!test_exec_non_word_boundary()) return 1;
    printf("test_exec_non_word_boundary_no_match "); if (!test_exec_non_word_boundary_no_match()) return 1;
    printf("test_exec_word_boundary_underscore "); if (!test_exec_word_boundary_underscore()) return 1;
    printf("test_exec_word_boundary_digits "); if (!test_exec_word_boundary_digits()) return 1;
    printf("test_exec_word_boundary_cross_chunk "); if (!test_exec_word_boundary_cross_chunk()) return 1;

    /* US-005 extension: Inline flags tests */
    printf("test_exec_dot_all_matches_newline "); if (!test_exec_dot_all_matches_newline()) return 1;
    printf("test_exec_dot_no_newline_baseline "); if (!test_exec_dot_no_newline_baseline()) return 1;
    printf("test_exec_multiline_noop   "); if (!test_exec_multiline_noop()) return 1;
    printf("test_exec_scoped_dot_all   "); if (!test_exec_scoped_dot_all()) return 1;
    printf("test_exec_combined_flags   "); if (!test_exec_combined_flags()) return 1;
    printf("test_err_unsupported_flag  "); if (!test_err_unsupported_flag()) return 1;
    printf("test_parse_dot_all_flag    "); if (!test_parse_dot_all_flag()) return 1;
    printf("test_parse_dot_no_flag     "); if (!test_parse_dot_no_flag()) return 1;
    printf("test_parse_scoped_flag_reset "); if (!test_parse_scoped_flag_reset()) return 1;
    printf("test_compile_dot_all       "); if (!test_compile_dot_all()) return 1;
    printf("test_compile_dot_no_all    "); if (!test_compile_dot_no_all()) return 1;
    printf("test_exec_dot_all_in_context "); if (!test_exec_dot_all_in_context()) return 1;
    printf("test_exec_dot_all_star     "); if (!test_exec_dot_all_star()) return 1;

    /* US-006 extension: Case-insensitive flag (?i) tests */
    printf("test_parse_icase_literal   "); if (!test_parse_icase_literal()) return 1;
    printf("test_parse_no_icase_literal "); if (!test_parse_no_icase_literal()) return 1;
    printf("test_parse_icase_char_class "); if (!test_parse_icase_char_class()) return 1;
    printf("test_parse_scoped_icase    "); if (!test_parse_scoped_icase()) return 1;
    printf("test_compile_icase_literal "); if (!test_compile_icase_literal()) return 1;
    printf("test_compile_icase_char_class "); if (!test_compile_icase_char_class()) return 1;
    printf("test_exec_icase_hello      "); if (!test_exec_icase_hello()) return 1;
    printf("test_exec_icase_char_class_range "); if (!test_exec_icase_char_class_range()) return 1;
    printf("test_exec_scoped_icase     "); if (!test_exec_scoped_icase()) return 1;
    printf("test_exec_combined_icase_dotall "); if (!test_exec_combined_icase_dotall()) return 1;
    printf("test_exec_icase_non_alpha  "); if (!test_exec_icase_non_alpha()) return 1;
    printf("test_exec_icase_upper_range "); if (!test_exec_icase_upper_range()) return 1;
    printf("test_exec_icase_in_context "); if (!test_exec_icase_in_context()) return 1;

    /* US-007 extension: Capture group extraction tests */
    printf("test_captures_single_group "); if (!test_captures_single_group()) return 1;
    printf("test_captures_multiple_groups "); if (!test_captures_multiple_groups()) return 1;
    printf("test_captures_nested_groups "); if (!test_captures_nested_groups()) return 1;
    printf("test_captures_alt_groups   "); if (!test_captures_alt_groups()) return 1;
    printf("test_captures_noncapture_none "); if (!test_captures_noncapture_none()) return 1;
    printf("test_captures_mixed_groups "); if (!test_captures_mixed_groups()) return 1;
    printf("test_captures_unmatched_optional "); if (!test_captures_unmatched_optional()) return 1;
    printf("test_captures_no_groups    "); if (!test_captures_no_groups()) return 1;
    printf("test_captures_middle_match "); if (!test_captures_middle_match()) return 1;
    printf("test_captures_iterator     "); if (!test_captures_iterator()) return 1;
    printf("test_captures_repeated_group "); if (!test_captures_repeated_group()) return 1;
    printf("test_captures_free_noop    "); if (!test_captures_free_noop()) return 1;

    /* US-008: Arena-backed nfa_compile_pattern tests */
    printf("test_arena_literal         "); if (!test_arena_literal()) return 1;
    printf("test_arena_concat          "); if (!test_arena_concat()) return 1;
    printf("test_arena_alt             "); if (!test_arena_alt()) return 1;
    printf("test_arena_star            "); if (!test_arena_star()) return 1;
    printf("test_arena_plus            "); if (!test_arena_plus()) return 1;
    printf("test_arena_question        "); if (!test_arena_question()) return 1;
    printf("test_arena_anchors         "); if (!test_arena_anchors()) return 1;
    printf("test_arena_char_class      "); if (!test_arena_char_class()) return 1;
    printf("test_arena_shorthand       "); if (!test_arena_shorthand()) return 1;
    printf("test_arena_captures        "); if (!test_arena_captures()) return 1;
    printf("test_arena_icase           "); if (!test_arena_icase()) return 1;
    printf("test_arena_dot_all         "); if (!test_arena_dot_all()) return 1;
    printf("test_arena_error_cases     "); if (!test_arena_error_cases()) return 1;
    printf("test_arena_matches_classic "); if (!test_arena_matches_classic()) return 1;
    printf("test_arena_counted_rep     "); if (!test_arena_counted_rep()) return 1;
    printf("test_arena_word_boundary   "); if (!test_arena_word_boundary()) return 1;
    printf("test_arena_empty_pattern   "); if (!test_arena_empty_pattern()) return 1;

    printf("\nAll %d tests passed!\n", 219);
    return 0;
}
