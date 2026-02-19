/*
 * test_nfa_rdoc.c — Tests for rdoc input adapter for NFA regex engine
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/test_helpers.h"

/* Wire up tracked allocator for both NFA and rdoc sum_tree */
#define NFA_MALLOC(sz) tracked_malloc(sz)
#define NFA_FREE(p)    tracked_free(p)

#define STREE_ALLOCATOR                 \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#include "nfa_rdoc.h"

/* Compile a pattern, returning program. Frees AST. Returns NULL on error. */
static nfa_program* compile_pattern(const char* pat) {
    nfa_ast* ast = NULL;
    int rc = nfa_parse((const uint8_t*)pat, strlen(pat), &ast);
    if (rc != NFA_OK) return NULL;
    nfa_program* prog = NULL;
    rc = nfa_compile(ast, &prog);
    nfa_ast_free(ast);
    if (rc != NFA_OK) return NULL;
    return prog;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

/* Test 1: Pattern matches across inline sentinels */
static int test_match_through_inline(void) {
    tracker_reset();

    /* Build: hel<INLINE_OPEN id=1>lo<INLINE_CLOSE id=1> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"hel", 3);
    rdoc_builder_inline_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"lo", 2);
    rdoc_builder_inline_close(&b, 1);
    rdoc d = rdoc_builder_finish(&b);

    nfa_program* prog = compile_pattern("hello");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 5);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 2: Pattern does NOT match across block sentinels */
static int test_blocked_by_block(void) {
    tracker_reset();

    /* Build: hel<BLOCK_CLOSE id=1><BLOCK_OPEN id=2>lo */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"hel", 3);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"lo", 2);
    rdoc d = rdoc_builder_finish(&b);

    nfa_program* prog = compile_pattern("hello");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(!m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 3: Anchored ^hello$ matches text within a single block */
static int test_anchored_in_block(void) {
    tracker_reset();

    /* Build: <BLOCK_OPEN id=1>hello<BLOCK_CLOSE id=1> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"hello", 5);
    rdoc_builder_block_close(&b, 1);
    rdoc d = rdoc_builder_finish(&b);

    nfa_program* prog = compile_pattern("^hello$");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    /* Virtual stream: "\nhello\n" — ^hello$ matches starting after first \n */
    ASSERT(m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 4: Anchor behavior at block boundaries — ^ matches at block start */
static int test_anchor_bol_at_block_start(void) {
    tracker_reset();

    /* Build: <BLOCK_OPEN id=1>first<BLOCK_CLOSE id=1><BLOCK_OPEN id=2>second<BLOCK_CLOSE id=2> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"first", 5);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"second", 6);
    rdoc_builder_block_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual stream: "\nfirst\n\nsecond\n" */
    /* ^second should match (second is at start of a "line" after block sentinel \n) */
    nfa_program* prog = compile_pattern("^second");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 5: $ matches at block end */
static int test_anchor_eol_at_block_end(void) {
    tracker_reset();

    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"first", 5);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"second", 6);
    rdoc_builder_block_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\nfirst\n\nsecond\n" — first$ should match */
    nfa_program* prog = compile_pattern("first$");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 6: Match positions are in text-byte space not raw-byte space.
 * Use inline sentinels to verify positions exclude sentinel bytes. */
static int test_positions_text_byte_space(void) {
    tracker_reset();

    /* Build: abc <INLINE_OPEN id=1>hello<INLINE_CLOSE id=1> xyz */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"abc ", 4);
    rdoc_builder_inline_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"hello", 5);
    rdoc_builder_inline_close(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)" xyz", 4);
    rdoc d = rdoc_builder_finish(&b);

    /* Raw bytes: "abc "(4) + INLINE_OPEN(5) + "hello"(5) + INLINE_CLOSE(5) + " xyz"(4) = 23 bytes
     * Virtual stream: "abc hello xyz" — no block sentinels, so virtual == text-byte
     * Pattern "hello" should match at (4, 9) in text-byte space.
     * In raw-byte space "hello" starts at byte 9 (after 4 text + 5 sentinel).
     * We verify the match is at (4, 9) not (9, 14). */
    nfa_program* prog = compile_pattern("hello");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 4);
    ASSERT_SIZE_EQ(m.end, 9);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 7: Match positions with multiple inline sentinels before match */
static int test_positions_multiple_inline(void) {
    tracker_reset();

    /* Build: <INLINE_OPEN id=1><INLINE_CLOSE id=1><INLINE_OPEN id=2>target<INLINE_CLOSE id=2> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_inline_open(&b, 1);
    rdoc_builder_inline_close(&b, 1);
    rdoc_builder_inline_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"target", 6);
    rdoc_builder_inline_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual stream: "target" (all inline sentinels skipped) */
    nfa_program* prog = compile_pattern("target");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    /* In raw space, "target" starts at byte 15 (3 sentinels * 5 = 15).
     * In text-byte/virtual space, it starts at 0. */
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 6);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 8: rdoc with only sentinels (no text) */
static int test_only_sentinels(void) {
    tracker_reset();

    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_inline_open(&b, 2);
    rdoc_builder_inline_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    nfa_program* prog = compile_pattern("a");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(!m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 9: rdoc with no sentinels (plain text) */
static int test_no_sentinels(void) {
    tracker_reset();

    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"hello world", 11);
    rdoc d = rdoc_builder_finish(&b);

    nfa_program* prog = compile_pattern("world");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 6);
    ASSERT_SIZE_EQ(m.end, 11);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 10: Mixed — text with inline and block sentinels */
static int test_mixed_sentinels(void) {
    tracker_reset();

    /* Build: <BLOCK_OPEN id=1>Hello <INLINE_OPEN id=10>World<INLINE_CLOSE id=10><BLOCK_CLOSE id=1> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"Hello ", 6);
    rdoc_builder_inline_open(&b, 10);
    rdoc_builder_text(&b, (const uint8_t*)"World", 5);
    rdoc_builder_inline_close(&b, 10);
    rdoc_builder_block_close(&b, 1);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\nHello World\n" — inline sentinels skipped */
    nfa_program* prog = compile_pattern("Hello World");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 11: Mark change is skipped transparently */
static int test_mark_change_skipped(void) {
    tracker_reset();

    /* Build: hel<MARK_CHANGE flags=1 app_id=42>lo */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"hel", 3);
    rdoc_builder_mark(&b, 1, 42);
    rdoc_builder_text(&b, (const uint8_t*)"lo", 2);
    rdoc d = rdoc_builder_finish(&b);

    nfa_program* prog = compile_pattern("hello");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 5);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 12: Iterator finds multiple matches across blocks */
static int test_iter_across_blocks(void) {
    tracker_reset();

    /* Build: <BLOCK_OPEN>abc 123<BLOCK_CLOSE><BLOCK_OPEN>def 456<BLOCK_CLOSE> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"abc 123", 7);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"def 456", 7);
    rdoc_builder_block_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\nabc 123\n\ndef 456\n" */
    nfa_program* prog = compile_pattern("[0-9]+");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_iter it = nfa_iter_new(prog, inp);

    nfa_match m1 = nfa_iter_next(&it);
    ASSERT(m1.matched);

    nfa_match m2 = nfa_iter_next(&it);
    ASSERT(m2.matched);

    nfa_match m3 = nfa_iter_next(&it);
    ASSERT(!m3.matched);

    /* Clean up — must use custom free for rdoc iter */
    _nfa_rdoc_ctx_free(it.input.ctx);
    nfa_iter_free(&it);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 13: Iterator with inline sentinels */
static int test_iter_with_inline(void) {
    tracker_reset();

    /* Build: <INLINE_OPEN>ab<INLINE_CLOSE> cd <INLINE_OPEN>ef<INLINE_CLOSE> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_inline_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"ab", 2);
    rdoc_builder_inline_close(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)" cd ", 4);
    rdoc_builder_inline_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"ef", 2);
    rdoc_builder_inline_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "ab cd ef" */
    nfa_program* prog = compile_pattern("[a-z]+");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_iter it = nfa_iter_new(prog, inp);

    nfa_match m1 = nfa_iter_next(&it);
    ASSERT(m1.matched);
    ASSERT_SIZE_EQ(m1.start, 0);
    ASSERT_SIZE_EQ(m1.end, 2);

    nfa_match m2 = nfa_iter_next(&it);
    ASSERT(m2.matched);
    ASSERT_SIZE_EQ(m2.start, 3);
    ASSERT_SIZE_EQ(m2.end, 5);

    nfa_match m3 = nfa_iter_next(&it);
    ASSERT(m3.matched);
    ASSERT_SIZE_EQ(m3.start, 6);
    ASSERT_SIZE_EQ(m3.end, 8);

    nfa_match m4 = nfa_iter_next(&it);
    ASSERT(!m4.matched);

    _nfa_rdoc_ctx_free(it.input.ctx);
    nfa_iter_free(&it);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 14: Empty rdoc */
static int test_empty_rdoc(void) {
    tracker_reset();

    rdoc d = rdoc_new();
    nfa_program* prog = compile_pattern("a");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(!m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 15: Dot does not match synthetic newline from block sentinel */
static int test_dot_no_match_block_newline(void) {
    tracker_reset();

    /* Build: a<BLOCK_CLOSE id=1><BLOCK_OPEN id=2>b */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"a", 1);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"b", 1);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "a\n\nb" — pattern a.b should NOT match (dot doesn't match \n) */
    nfa_program* prog = compile_pattern("a.b");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(!m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 16: Dot matches across inline sentinel (text is continuous) */
static int test_dot_across_inline(void) {
    tracker_reset();

    /* Build: a<INLINE_OPEN id=1>b<INLINE_CLOSE id=1> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"a", 1);
    rdoc_builder_inline_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"b", 1);
    rdoc_builder_inline_close(&b, 1);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "ab" — pattern a.b should NOT match (only 2 bytes) */
    nfa_program* prog_no = compile_pattern("a.b");
    ASSERT(prog_no != NULL);
    nfa_input inp1 = nfa_input_from_rdoc(d);
    nfa_match m1 = nfa_exec(prog_no, &inp1);
    nfa_input_rdoc_free(&inp1);
    ASSERT(!m1.matched);
    nfa_program_free(prog_no);

    /* pattern a. should match */
    nfa_program* prog_yes = compile_pattern("a.");
    ASSERT(prog_yes != NULL);
    nfa_input inp2 = nfa_input_from_rdoc(d);
    nfa_match m2 = nfa_exec(prog_yes, &inp2);
    nfa_input_rdoc_free(&inp2);
    ASSERT(m2.matched);
    ASSERT_SIZE_EQ(m2.start, 0);
    ASSERT_SIZE_EQ(m2.end, 2);
    nfa_program_free(prog_yes);

    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 17: Character class matching in rdoc */
static int test_char_class_in_rdoc(void) {
    tracker_reset();

    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"abc 123 def", 11);
    rdoc_builder_block_close(&b, 1);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\nabc 123 def\n" */
    nfa_program* prog = compile_pattern("[0-9]+");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    /* "123" in virtual stream: after \n(1) + "abc "(4), so at vpos 5-8 */
    ASSERT_SIZE_EQ(m.start, 5);
    ASSERT_SIZE_EQ(m.end, 8);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 18: Shorthand classes work through inline sentinels */
static int test_shorthand_through_inline(void) {
    tracker_reset();

    /* Build: 12<INLINE_OPEN>34<INLINE_CLOSE>56 */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"12", 2);
    rdoc_builder_inline_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"34", 2);
    rdoc_builder_inline_close(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"56", 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "123456" */
    nfa_program* prog = compile_pattern("\\d+");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 6);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 19: Rdoc with only block sentinels, no text */
static int test_only_block_sentinels(void) {
    tracker_reset();

    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_block_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\n\n\n\n" (4 synthetic newlines, no text) */
    /* Pattern "a" should not match */
    nfa_program* prog = compile_pattern("a");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(!m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 20: Multiple blocks with anchored search per block */
static int test_multiblock_anchored(void) {
    tracker_reset();

    /* Build 3 blocks: "alpha", "beta", "gamma" */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"alpha", 5);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"beta", 4);
    rdoc_builder_block_close(&b, 2);
    rdoc_builder_block_open(&b, 3);
    rdoc_builder_text(&b, (const uint8_t*)"gamma", 5);
    rdoc_builder_block_close(&b, 3);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\nalpha\n\nbeta\n\ngamma\n" */
    /* ^beta$ should match */
    nfa_program* prog = compile_pattern("^beta$");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 21: Rdoc with mark changes interspersed */
static int test_marks_interspersed(void) {
    tracker_reset();

    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)"foo", 3);
    rdoc_builder_mark(&b, 0x01, 1);
    rdoc_builder_text(&b, (const uint8_t*)"bar", 3);
    rdoc_builder_mark(&b, 0x02, 2);
    rdoc_builder_text(&b, (const uint8_t*)"baz", 3);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "foobarbaz" (mark changes skipped) */
    nfa_program* prog = compile_pattern("barbaz");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_match m = nfa_exec(prog, &inp);
    nfa_input_rdoc_free(&inp);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 3);
    ASSERT_SIZE_EQ(m.end, 9);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 22: Complex mixed document with all sentinel types */
static int test_complex_mixed(void) {
    tracker_reset();

    /* Build: <BLOCK_OPEN 1>Hello <INLINE_OPEN 10><MARK_CHANGE>World<INLINE_CLOSE 10><BLOCK_CLOSE 1>
     *        <BLOCK_OPEN 2>Second<BLOCK_CLOSE 2> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"Hello ", 6);
    rdoc_builder_inline_open(&b, 10);
    rdoc_builder_mark(&b, 0x01, 42);
    rdoc_builder_text(&b, (const uint8_t*)"World", 5);
    rdoc_builder_inline_close(&b, 10);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"Second", 6);
    rdoc_builder_block_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\nHello World\n\nSecond\n" */

    /* "Hello World" matches across inline sentinel */
    nfa_program* prog1 = compile_pattern("Hello World");
    ASSERT(prog1 != NULL);
    nfa_input inp1 = nfa_input_from_rdoc(d);
    nfa_match m1 = nfa_exec(prog1, &inp1);
    nfa_input_rdoc_free(&inp1);
    ASSERT(m1.matched);
    nfa_program_free(prog1);

    /* "WorldSecond" does NOT match (block boundary between them) */
    nfa_program* prog2 = compile_pattern("WorldSecond");
    ASSERT(prog2 != NULL);
    nfa_input inp2 = nfa_input_from_rdoc(d);
    nfa_match m2 = nfa_exec(prog2, &inp2);
    nfa_input_rdoc_free(&inp2);
    ASSERT(!m2.matched);
    nfa_program_free(prog2);

    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 23: Iterator across blocks and inline sentinels */
static int test_iter_complex(void) {
    tracker_reset();

    /* Build: <BLOCK_OPEN>abc <INLINE_OPEN>123<INLINE_CLOSE> def<BLOCK_CLOSE>
     *        <BLOCK_OPEN>ghi <INLINE_OPEN>456<INLINE_CLOSE> jkl<BLOCK_CLOSE> */
    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_block_open(&b, 1);
    rdoc_builder_text(&b, (const uint8_t*)"abc ", 4);
    rdoc_builder_inline_open(&b, 10);
    rdoc_builder_text(&b, (const uint8_t*)"123", 3);
    rdoc_builder_inline_close(&b, 10);
    rdoc_builder_text(&b, (const uint8_t*)" def", 4);
    rdoc_builder_block_close(&b, 1);
    rdoc_builder_block_open(&b, 2);
    rdoc_builder_text(&b, (const uint8_t*)"ghi ", 4);
    rdoc_builder_inline_open(&b, 20);
    rdoc_builder_text(&b, (const uint8_t*)"456", 3);
    rdoc_builder_inline_close(&b, 20);
    rdoc_builder_text(&b, (const uint8_t*)" jkl", 4);
    rdoc_builder_block_close(&b, 2);
    rdoc d = rdoc_builder_finish(&b);

    /* Virtual: "\nabc 123 def\n\nghi 456 jkl\n" */
    nfa_program* prog = compile_pattern("[0-9]+");
    ASSERT(prog != NULL);

    nfa_input inp = nfa_input_from_rdoc(d);
    nfa_iter it = nfa_iter_new(prog, inp);

    nfa_match m1 = nfa_iter_next(&it);
    ASSERT(m1.matched);

    nfa_match m2 = nfa_iter_next(&it);
    ASSERT(m2.matched);

    nfa_match m3 = nfa_iter_next(&it);
    ASSERT(!m3.matched);

    _nfa_rdoc_ctx_free(it.input.ctx);
    nfa_iter_free(&it);

    nfa_program_free(prog);
    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 24: rdoc with no sentinels matches identically to flat buffer */
static int test_plain_text_matches_buffer(void) {
    tracker_reset();

    const char* text = "The quick brown fox jumps over the lazy dog";
    size_t tlen = strlen(text);

    rdoc_builder b = rdoc_builder_new();
    rdoc_builder_text(&b, (const uint8_t*)text, tlen);
    rdoc d = rdoc_builder_finish(&b);

    struct { const char* pat; } cases[] = {
        { "quick" },
        { "fox" },
        { "[a-z]+" },
        { "\\w+" },
        { "the" },
        { "^The" },
        { "dog$" },
    };
    size_t n = sizeof(cases) / sizeof(cases[0]);

    for (size_t i = 0; i < n; i++) {
        nfa_program* prog = compile_pattern(cases[i].pat);
        ASSERT(prog != NULL);

        /* Buffer match */
        nfa_input buf_inp = nfa_input_from_buf((const uint8_t*)text, tlen);
        nfa_match buf_m = nfa_exec(prog, &buf_inp);
        nfa_input_free(&buf_inp);

        /* Rdoc match */
        nfa_input rdoc_inp = nfa_input_from_rdoc(d);
        nfa_match rdoc_m = nfa_exec(prog, &rdoc_inp);
        nfa_input_rdoc_free(&rdoc_inp);

        ASSERT(buf_m.matched == rdoc_m.matched);
        if (buf_m.matched) {
            if (buf_m.start != rdoc_m.start || buf_m.end != rdoc_m.end) {
                fprintf(stderr, "FAIL: pattern '%s': buf=[%zu,%zu) rdoc=[%zu,%zu)\n",
                        cases[i].pat, buf_m.start, buf_m.end, rdoc_m.start, rdoc_m.end);
                nfa_match_free(&buf_m);
                nfa_match_free(&rdoc_m);
                nfa_program_free(prog);
                rdoc_unref(d);
                return 0;
            }
        }

        nfa_match_free(&buf_m);
        nfa_match_free(&rdoc_m);
        nfa_program_free(prog);
    }

    rdoc_unref(d);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Runner ──────────────────────────────────────────────────────────── */

int main(void) {
    printf("test_match_through_inline       "); if (!test_match_through_inline()) return 1;
    printf("test_blocked_by_block           "); if (!test_blocked_by_block()) return 1;
    printf("test_anchored_in_block          "); if (!test_anchored_in_block()) return 1;
    printf("test_anchor_bol_at_block_start  "); if (!test_anchor_bol_at_block_start()) return 1;
    printf("test_anchor_eol_at_block_end    "); if (!test_anchor_eol_at_block_end()) return 1;
    printf("test_positions_text_byte_space  "); if (!test_positions_text_byte_space()) return 1;
    printf("test_positions_multiple_inline  "); if (!test_positions_multiple_inline()) return 1;
    printf("test_only_sentinels             "); if (!test_only_sentinels()) return 1;
    printf("test_no_sentinels               "); if (!test_no_sentinels()) return 1;
    printf("test_mixed_sentinels            "); if (!test_mixed_sentinels()) return 1;
    printf("test_mark_change_skipped        "); if (!test_mark_change_skipped()) return 1;
    printf("test_iter_across_blocks         "); if (!test_iter_across_blocks()) return 1;
    printf("test_iter_with_inline           "); if (!test_iter_with_inline()) return 1;
    printf("test_empty_rdoc                 "); if (!test_empty_rdoc()) return 1;
    printf("test_dot_no_match_block_newline "); if (!test_dot_no_match_block_newline()) return 1;
    printf("test_dot_across_inline          "); if (!test_dot_across_inline()) return 1;
    printf("test_char_class_in_rdoc         "); if (!test_char_class_in_rdoc()) return 1;
    printf("test_shorthand_through_inline   "); if (!test_shorthand_through_inline()) return 1;
    printf("test_only_block_sentinels       "); if (!test_only_block_sentinels()) return 1;
    printf("test_multiblock_anchored        "); if (!test_multiblock_anchored()) return 1;
    printf("test_marks_interspersed         "); if (!test_marks_interspersed()) return 1;
    printf("test_complex_mixed              "); if (!test_complex_mixed()) return 1;
    printf("test_iter_complex               "); if (!test_iter_complex()) return 1;
    printf("test_plain_text_matches_buffer  "); if (!test_plain_text_matches_buffer()) return 1;

    printf("\nAll 24 tests passed!\n");
    return 0;
}
