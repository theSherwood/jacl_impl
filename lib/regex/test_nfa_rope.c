/*
 * test_nfa_rope.c — Tests for rope input adapter for NFA regex engine
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/test_helpers.h"

/* Wire up tracked allocator for both NFA and rope sum_tree */
#define NFA_MALLOC(sz) tracked_malloc(sz)
#define NFA_FREE(p)    tracked_free(p)

#define STREE_ALLOCATOR                 \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#include "nfa_rope.h"

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

/*
 * Reclaim a rope's backing sum-tree nodes.
 *
 * jacl_impl's rope (lib/sum_tree) is a persistent, structurally-shared,
 * GC/arena-owned structure: rope_ref/rope_unref are no-ops and there is no
 * public call that frees a rope's nodes — the GC normally reclaims them. This
 * test was ported from a repo whose rope was reference-counted, and it asserts
 * check_no_leaks(), which requires the nodes to be reclaimed. We reclaim them
 * by walking the tree(s) and freeing each node through the same allocator the
 * rope was built with (rope_st_allocator, wired to the tracker below).
 *
 * Because the rope is persistent, two ropes (e.g. r and rope_insert(r, ...))
 * can share subtree nodes. Freeing must therefore visit each unique node once
 * across ALL live ropes — exactly the liveness bookkeeping the GC does. We
 * track freed nodes in a small set and skip any node already freed; the set is
 * also consulted *before* dereferencing a node, so freeing shared trees in any
 * order is safe (no double-free, no use-after-free). Callers that hold several
 * ropes sharing structure must free them together via rope_free_deep_session.
 */
#define ROPE_FREED_MAX 4096
static rope_st_node* g_rope_freed[ROPE_FREED_MAX];
static size_t        g_rope_freed_n;

static int rope_node_already_freed(rope_st_node* node) {
    for (size_t i = 0; i < g_rope_freed_n; i++) {
        if (g_rope_freed[i] == node) return 1;
    }
    return 0;
}

static void rope_free_walk(rope_st_node* node) {
    if (!node || rope_node_already_freed(node)) return;
    if (node->type == rope_st_NODE_INTERNAL) {
        rope_st_internal* in = (rope_st_internal*)node;
        for (size_t i = 0; i < in->n_children; i++) {
            rope_free_walk(in->children[i]);
        }
    }
    if (g_rope_freed_n < ROPE_FREED_MAX) g_rope_freed[g_rope_freed_n++] = node;
    rope_st_allocator.free(rope_st_allocator.ctx, node);
}

/* Start a fresh free session: forget previously-freed pointers. */
static void rope_free_deep_session(void) { g_rope_freed_n = 0; }

/* Free one rope's tree in its own session (no sharing with other live ropes). */
static void rope_free_deep(rope r) {
    rope_free_deep_session();
    rope_free_walk(r.root.node);
}

/*
 * Match a pattern against both a flat buffer and a rope built from the
 * same text. Assert both produce identical results (first match).
 */
static int assert_rope_buf_match(const char* pattern, const char* text) {
    nfa_program* prog = compile_pattern(pattern);
    ASSERT(prog != NULL);

    size_t tlen = strlen(text);

    /* Buffer match */
    nfa_input buf_inp = nfa_input_from_buf((const uint8_t*)text, tlen);
    nfa_match buf_m = nfa_exec(prog, &buf_inp);
    nfa_input_free(&buf_inp);

    /* Rope match */
    rope r = rope_from_str((const uint8_t*)text, tlen);
    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_match rope_m = nfa_exec(prog, &rope_inp);
    nfa_input_rope_free(&rope_inp);
    rope_unref(r);
    rope_free_deep(r);

    /* Compare */
    if (buf_m.matched != rope_m.matched) {
        fprintf(stderr, "FAIL: pattern '%s' text '%s': buf.matched=%d rope.matched=%d\n",
                pattern, text, buf_m.matched, rope_m.matched);
        nfa_match_free(&buf_m);
        nfa_match_free(&rope_m);
        nfa_program_free(prog);
        return 0;
    }
    if (buf_m.matched) {
        if (buf_m.start != rope_m.start || buf_m.end != rope_m.end) {
            fprintf(stderr, "FAIL: pattern '%s' text '%s': buf=[%zu,%zu) rope=[%zu,%zu)\n",
                    pattern, text, buf_m.start, buf_m.end, rope_m.start, rope_m.end);
            nfa_match_free(&buf_m);
            nfa_match_free(&rope_m);
            nfa_program_free(prog);
            return 0;
        }
    }

    nfa_match_free(&buf_m);
    nfa_match_free(&rope_m);
    nfa_program_free(prog);
    return 1;
}

/*
 * Match a pattern against both a flat buffer and a rope using the iterator.
 * Assert all matches are identical.
 */
static int assert_rope_buf_iter(const char* pattern, const char* text) {
    nfa_program* prog = compile_pattern(pattern);
    ASSERT(prog != NULL);

    size_t tlen = strlen(text);

    /* Buffer iterator */
    nfa_input buf_inp = nfa_input_from_buf((const uint8_t*)text, tlen);
    nfa_iter buf_it = nfa_iter_new(prog, buf_inp);

    /* Rope iterator */
    rope r = rope_from_str((const uint8_t*)text, tlen);
    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_iter rope_it = nfa_iter_new(prog, rope_inp);

    for (int i = 0; i < 1000; i++) { /* safety bound */
        nfa_match bm = nfa_iter_next(&buf_it);
        nfa_match rm = nfa_iter_next(&rope_it);

        if (bm.matched != rm.matched) {
            fprintf(stderr, "FAIL: iter %d pattern '%s' text '%s': buf.matched=%d rope.matched=%d\n",
                    i, pattern, text, bm.matched, rm.matched);
            nfa_match_free(&bm);
            nfa_match_free(&rm);
            /* Clean up with custom free for rope iter */
            _nfa_rope_ctx_free(rope_it.input.ctx);
            nfa_iter_free(&rope_it);
            nfa_iter_free(&buf_it);
            rope_unref(r);
            rope_free_deep(r);
            nfa_program_free(prog);
            return 0;
        }
        if (bm.matched) {
            if (bm.start != rm.start || bm.end != rm.end) {
                fprintf(stderr, "FAIL: iter %d pattern '%s' text '%s': buf=[%zu,%zu) rope=[%zu,%zu)\n",
                        i, pattern, text, bm.start, bm.end, rm.start, rm.end);
                nfa_match_free(&bm);
                nfa_match_free(&rm);
                _nfa_rope_ctx_free(rope_it.input.ctx);
                NFA_FREE(rope_it.input.ctx);
                rope_it.input.ctx = NULL;
                nfa_iter_free(&buf_it);
                rope_unref(r);
                rope_free_deep(r);
                nfa_program_free(prog);
                return 0;
            }
        }
        nfa_match_free(&bm);
        nfa_match_free(&rm);
        if (!bm.matched) break;
    }

    /* Clean up — unref rope, then iter_free handles buffers + ctx */
    _nfa_rope_ctx_free(rope_it.input.ctx);
    nfa_iter_free(&rope_it);
    nfa_iter_free(&buf_it);
    rope_unref(r);
    rope_free_deep(r);
    nfa_program_free(prog);
    return 1;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

/* Test 1: Simple literal match on rope */
static int test_rope_literal_match(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("hello", "say hello world"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 2: Alternation */
static int test_rope_alternation(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("cat|dog", "I have a dog"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 3: Character class range */
static int test_rope_char_class(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("[a-z]+", "HELLO world 123"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 4: Negated character class */
static int test_rope_negated_class(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("[^0-9]+", "abc123def"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 5: Repetition with star */
static int test_rope_star(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("ab*c", "xabbbc"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 6: Anchored pattern */
static int test_rope_anchored(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("^hello", "hello world"));
    ASSERT(assert_rope_buf_match("^hello", "say hello"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 7: EOL anchor */
static int test_rope_eol_anchor(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("world$", "hello world"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 8: Dot matches any */
static int test_rope_dot(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("h.llo", "hello"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 9: Shorthand classes */
static int test_rope_shorthand(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("\\d+", "abc 123 def"));
    ASSERT(assert_rope_buf_match("\\w+", "hello world"));
    ASSERT(assert_rope_buf_match("\\s+", "hello   world"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 10: Group with repetition */
static int test_rope_group(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("(ab)+", "xabababy"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 11: Plus quantifier */
static int test_rope_plus(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("[0-9]+", "abc 123 def 456"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 12: No match case */
static int test_rope_no_match(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_match("xyz", "abcdef"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 13: Iterator — multiple matches in rope */
static int test_rope_iter_multiple(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_iter("[0-9]+", "abc 123 def 456 ghi 789"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 14: Iterator — word matches */
static int test_rope_iter_words(void) {
    tracker_reset();
    ASSERT(assert_rope_buf_iter("[a-z]+", "Hello World Foo Bar"));
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/*
 * Test 15: Match spanning a leaf boundary.
 * ROPE_LEAF_MAX defaults to 256 bytes. Build a rope where a match
 * necessarily crosses a leaf split point.
 */
static int test_rope_cross_leaf(void) {
    tracker_reset();

    /* Build a string of 250 'x' then "FINDME" then 250 'x'.
     * The "FINDME" will span the ~256 byte leaf boundary. */
    size_t prefix_len = 250;
    size_t suffix_len = 250;
    size_t marker_len = 6;
    size_t total = prefix_len + marker_len + suffix_len;
    uint8_t* text = (uint8_t*)malloc(total);
    memset(text, 'x', prefix_len);
    memcpy(text + prefix_len, "FINDME", marker_len);
    memset(text + prefix_len + marker_len, 'x', suffix_len);

    /* Create rope */
    rope r = rope_from_str(text, total);
    ASSERT(rope_byte_count(r) == total);

    /* Compile pattern */
    nfa_program* prog = compile_pattern("FINDME");
    ASSERT(prog != NULL);

    /* Buffer match */
    nfa_input buf_inp = nfa_input_from_buf(text, total);
    nfa_match buf_m = nfa_exec(prog, &buf_inp);
    nfa_input_free(&buf_inp);
    ASSERT(buf_m.matched);
    ASSERT_SIZE_EQ(buf_m.start, prefix_len);
    ASSERT_SIZE_EQ(buf_m.end, prefix_len + marker_len);

    /* Rope match */
    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_match rope_m = nfa_exec(prog, &rope_inp);
    nfa_input_rope_free(&rope_inp);

    ASSERT(rope_m.matched);
    ASSERT_SIZE_EQ(rope_m.start, buf_m.start);
    ASSERT_SIZE_EQ(rope_m.end, buf_m.end);

    rope_unref(r);
    rope_free_deep(r);
    nfa_program_free(prog);
    free(text);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/*
 * Test 16: Multi-insert to force known leaf split.
 * Insert text character by character to create multiple small leaves
 * and ensure matches work across boundaries.
 */
static int test_rope_multi_insert_boundary(void) {
    tracker_reset();

    /* Build rope by inserting chunks that force multiple leaves */
    size_t chunk_sz = 200;
    uint8_t chunk_a[200];
    uint8_t chunk_b[200];
    memset(chunk_a, 'a', chunk_sz);
    memset(chunk_b, 'b', chunk_sz);

    /* "aaa...aaabbb...bbb" — 200 a's then 200 b's */
    rope r = rope_from_str(chunk_a, chunk_sz);
    rope r2 = rope_insert(r, chunk_sz, chunk_b, chunk_sz);
    rope_unref(r);

    /* Pattern "a+b+" should match across the leaf boundary */
    nfa_program* prog = compile_pattern("a+b+");
    ASSERT(prog != NULL);

    size_t total = chunk_sz * 2;
    uint8_t* flat = (uint8_t*)malloc(total);
    rope_to_str(r2, flat, total);

    nfa_input buf_inp = nfa_input_from_buf(flat, total);
    nfa_match buf_m = nfa_exec(prog, &buf_inp);
    nfa_input_free(&buf_inp);

    nfa_input rope_inp = nfa_input_from_rope(r2);
    nfa_match rope_m = nfa_exec(prog, &rope_inp);
    nfa_input_rope_free(&rope_inp);

    ASSERT(buf_m.matched);
    ASSERT(rope_m.matched);
    ASSERT_SIZE_EQ(rope_m.start, buf_m.start);
    ASSERT_SIZE_EQ(rope_m.end, buf_m.end);

    rope_unref(r2);
    /* r and r2 share subtree nodes (persistent rope_insert), and r2 was in use
       until just now — so free both in one session, after r2's last read, with
       a shared visited set that reclaims each shared node exactly once. */
    rope_free_deep_session();
    rope_free_walk(r2.root.node);
    rope_free_walk(r.root.node);
    nfa_program_free(prog);
    free(flat);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 17: Empty rope */
static int test_rope_empty(void) {
    tracker_reset();

    rope r = rope_new();
    nfa_program* prog = compile_pattern("a");
    ASSERT(prog != NULL);

    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_match m = nfa_exec(prog, &rope_inp);
    nfa_input_rope_free(&rope_inp);

    ASSERT(!m.matched);

    rope_unref(r);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 18: Single-character rope */
static int test_rope_single_char(void) {
    tracker_reset();

    rope r = rope_from_str((const uint8_t*)"a", 1);
    nfa_program* prog = compile_pattern("a");
    ASSERT(prog != NULL);

    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_match m = nfa_exec(prog, &rope_inp);
    nfa_input_rope_free(&rope_inp);

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 1);

    rope_unref(r);
    rope_free_deep(r);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 19: Rope with only newlines */
static int test_rope_only_newlines(void) {
    tracker_reset();

    rope r = rope_from_str((const uint8_t*)"\n\n\n", 3);

    /* ^ should match at each line start */
    nfa_program* prog = compile_pattern("^");
    ASSERT(prog != NULL);

    nfa_input buf_inp = nfa_input_from_buf((const uint8_t*)"\n\n\n", 3);
    nfa_match buf_m = nfa_exec(prog, &buf_inp);
    nfa_input_free(&buf_inp);

    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_match rope_m = nfa_exec(prog, &rope_inp);
    nfa_input_rope_free(&rope_inp);

    ASSERT(buf_m.matched == rope_m.matched);
    if (buf_m.matched) {
        ASSERT_SIZE_EQ(rope_m.start, buf_m.start);
        ASSERT_SIZE_EQ(rope_m.end, buf_m.end);
    }

    rope_unref(r);
    rope_free_deep(r);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 20: Comprehensive pattern battery — 10+ patterns across rope and buffer */
static int test_rope_pattern_battery(void) {
    tracker_reset();

    struct { const char* pat; const char* text; } cases[] = {
        { "hello",        "say hello world" },
        { "^first",       "first line\nsecond line" },
        { "line$",        "first line\nsecond line" },
        { "[A-Z][a-z]+",  "Hello World" },
        { "\\d\\d\\d",    "call 555-1234" },
        { "(foo|bar)+",   "foobarfoo" },
        { "a.b",          "axb ayb a\nb" },
        { "x*y",          "xxxy" },
        { "\\w+@\\w+",    "user@host" },
        { "[^aeiou]+",    "hello" },
        { "^$",           "" },
        { "a?b",          "b" },
    };
    size_t n = sizeof(cases) / sizeof(cases[0]);

    for (size_t i = 0; i < n; i++) {
        if (!assert_rope_buf_match(cases[i].pat, cases[i].text)) {
            fprintf(stderr, "  battery case %zu failed\n", i);
            return 0;
        }
    }

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 21: Iterator consistency across rope and buffer */
static int test_rope_iter_battery(void) {
    tracker_reset();

    struct { const char* pat; const char* text; } cases[] = {
        { "[0-9]+",   "abc 123 def 456 ghi 789" },
        { "[a-z]+",   "Hello World Foo Bar" },
        { "\\s+",     "a  b   c    d" },
        { ".",        "abcd" },
        { "ab",       "ababab" },
    };
    size_t n = sizeof(cases) / sizeof(cases[0]);

    for (size_t i = 0; i < n; i++) {
        if (!assert_rope_buf_iter(cases[i].pat, cases[i].text)) {
            fprintf(stderr, "  iter battery case %zu failed\n", i);
            return 0;
        }
    }

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Runner ──────────────────────────────────────────────────────────── */

int main(void) {
    printf("test_rope_literal_match        "); if (!test_rope_literal_match()) return 1;
    printf("test_rope_alternation          "); if (!test_rope_alternation()) return 1;
    printf("test_rope_char_class           "); if (!test_rope_char_class()) return 1;
    printf("test_rope_negated_class        "); if (!test_rope_negated_class()) return 1;
    printf("test_rope_star                 "); if (!test_rope_star()) return 1;
    printf("test_rope_anchored             "); if (!test_rope_anchored()) return 1;
    printf("test_rope_eol_anchor           "); if (!test_rope_eol_anchor()) return 1;
    printf("test_rope_dot                  "); if (!test_rope_dot()) return 1;
    printf("test_rope_shorthand            "); if (!test_rope_shorthand()) return 1;
    printf("test_rope_group                "); if (!test_rope_group()) return 1;
    printf("test_rope_plus                 "); if (!test_rope_plus()) return 1;
    printf("test_rope_no_match             "); if (!test_rope_no_match()) return 1;
    printf("test_rope_iter_multiple        "); if (!test_rope_iter_multiple()) return 1;
    printf("test_rope_iter_words           "); if (!test_rope_iter_words()) return 1;
    printf("test_rope_cross_leaf           "); if (!test_rope_cross_leaf()) return 1;
    printf("test_rope_multi_insert_boundary "); if (!test_rope_multi_insert_boundary()) return 1;
    printf("test_rope_empty                "); if (!test_rope_empty()) return 1;
    printf("test_rope_single_char          "); if (!test_rope_single_char()) return 1;
    printf("test_rope_only_newlines        "); if (!test_rope_only_newlines()) return 1;
    printf("test_rope_pattern_battery      "); if (!test_rope_pattern_battery()) return 1;
    printf("test_rope_iter_battery         "); if (!test_rope_iter_battery()) return 1;

    printf("\nAll 21 tests passed!\n");
    return 0;
}
