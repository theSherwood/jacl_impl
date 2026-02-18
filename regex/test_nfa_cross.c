/*
 * test_nfa_cross.c — Cross-input integration and stress tests for NFA regex engine
 *
 * Verifies that buffer, rope, and rdoc adapters produce identical results.
 * Includes pathological pattern stress tests proving linear-time execution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../test/test_helpers.h"

/* Wire up tracked allocator for both NFA and sum_tree */
#define NFA_MALLOC(sz) tracked_malloc(sz)
#define NFA_FREE(p)    tracked_free(p)

#define STREE_ALLOCATOR                 \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#include "nfa_rdoc.h"
#include "nfa_rope.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

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
 * Test harness: run same pattern against buffer, rope, and rdoc (plain text)
 * and assert identical match positions.
 */
static int assert_cross_match(const char* pattern, const char* text) {
    nfa_program* prog = compile_pattern(pattern);
    if (!prog) {
        fprintf(stderr, "FAIL: could not compile pattern '%s'\n", pattern);
        return 0;
    }
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

    /* Rdoc match (plain text, no sentinels) */
    rdoc d;
    if (tlen > 0) {
        rdoc_builder b = rdoc_builder_new();
        rdoc_builder_text(&b, (const uint8_t*)text, tlen);
        d = rdoc_builder_finish(&b);
    } else {
        d = rdoc_new();
    }
    nfa_input rdoc_inp = nfa_input_from_rdoc(d);
    nfa_match rdoc_m = nfa_exec(prog, &rdoc_inp);
    nfa_input_rdoc_free(&rdoc_inp);
    rdoc_unref(d);

    /* Compare all three */
    if (buf_m.matched != rope_m.matched || buf_m.matched != rdoc_m.matched) {
        fprintf(stderr, "FAIL: pattern '%s' text '%s': buf=%d rope=%d rdoc=%d\n",
                pattern, text, buf_m.matched, rope_m.matched, rdoc_m.matched);
        nfa_match_free(&buf_m);
        nfa_match_free(&rope_m);
        nfa_match_free(&rdoc_m);
        nfa_program_free(prog);
        return 0;
    }
    if (buf_m.matched) {
        if (buf_m.start != rope_m.start || buf_m.start != rdoc_m.start ||
            buf_m.end != rope_m.end || buf_m.end != rdoc_m.end) {
            fprintf(stderr, "FAIL: pattern '%s' text '%s':\n", pattern, text);
            fprintf(stderr, "  buf=[%zu,%zu) rope=[%zu,%zu) rdoc=[%zu,%zu)\n",
                    buf_m.start, buf_m.end, rope_m.start, rope_m.end,
                    rdoc_m.start, rdoc_m.end);
            nfa_match_free(&buf_m);
            nfa_match_free(&rope_m);
            nfa_match_free(&rdoc_m);
            nfa_program_free(prog);
            return 0;
        }
    }

    nfa_match_free(&buf_m);
    nfa_match_free(&rope_m);
    nfa_match_free(&rdoc_m);
    nfa_program_free(prog);
    return 1;
}

/*
 * Iterator harness: run same pattern against buffer, rope, and rdoc,
 * assert all matches are identical across all three.
 */
static int assert_cross_iter(const char* pattern, const char* text) {
    nfa_program* prog = compile_pattern(pattern);
    if (!prog) {
        fprintf(stderr, "FAIL: could not compile pattern '%s'\n", pattern);
        return 0;
    }
    size_t tlen = strlen(text);

    /* Buffer iterator */
    nfa_input buf_inp = nfa_input_from_buf((const uint8_t*)text, tlen);
    nfa_iter buf_it = nfa_iter_new(prog, buf_inp);

    /* Rope iterator */
    rope r = rope_from_str((const uint8_t*)text, tlen);
    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_iter rope_it = nfa_iter_new(prog, rope_inp);

    /* Rdoc iterator (plain text) */
    rdoc doc;
    if (tlen > 0) {
        rdoc_builder b = rdoc_builder_new();
        rdoc_builder_text(&b, (const uint8_t*)text, tlen);
        doc = rdoc_builder_finish(&b);
    } else {
        doc = rdoc_new();
    }
    nfa_input rdoc_inp = nfa_input_from_rdoc(doc);
    nfa_iter rdoc_it = nfa_iter_new(prog, rdoc_inp);

    int ok = 1;
    for (int i = 0; i < 10000; i++) {
        nfa_match bm = nfa_iter_next(&buf_it);
        nfa_match rm = nfa_iter_next(&rope_it);
        nfa_match dm = nfa_iter_next(&rdoc_it);

        if (bm.matched != rm.matched || bm.matched != dm.matched) {
            fprintf(stderr, "FAIL: iter %d pattern '%s': buf=%d rope=%d rdoc=%d\n",
                    i, pattern, bm.matched, rm.matched, dm.matched);
            nfa_match_free(&bm);
            nfa_match_free(&rm);
            nfa_match_free(&dm);
            ok = 0;
            break;
        }
        if (bm.matched) {
            if (bm.start != rm.start || bm.start != dm.start ||
                bm.end != rm.end || bm.end != dm.end) {
                fprintf(stderr, "FAIL: iter %d pattern '%s':\n", i, pattern);
                fprintf(stderr, "  buf=[%zu,%zu) rope=[%zu,%zu) rdoc=[%zu,%zu)\n",
                        bm.start, bm.end, rm.start, rm.end, dm.start, dm.end);
                nfa_match_free(&bm);
                nfa_match_free(&rm);
                nfa_match_free(&dm);
                ok = 0;
                break;
            }
        }
        nfa_match_free(&bm);
        nfa_match_free(&rm);
        nfa_match_free(&dm);
        if (!bm.matched) break;
    }

    /* Cleanup — unref rope/rdoc, then iter_free handles buffers + ctx */
    nfa_iter_free(&buf_it);
    _nfa_rope_ctx_free(rope_it.input.ctx);
    nfa_iter_free(&rope_it);
    _nfa_rdoc_ctx_free(rdoc_it.input.ctx);
    nfa_iter_free(&rdoc_it);

    rope_unref(r);
    rdoc_unref(doc);
    nfa_program_free(prog);
    return ok;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

/* Test 1: 20+ patterns across all three inputs (first match) */
static int test_cross_pattern_battery(void) {
    tracker_reset();

    struct { const char* pat; const char* text; } cases[] = {
        /* Literals */
        { "hello",           "say hello world" },
        { "xyz",             "abcdef" },           /* no match */
        /* Concatenation */
        { "abc",             "xabcx" },
        /* Alternation */
        { "cat|dog",         "I have a dog" },
        { "foo|bar|baz",     "try baz" },
        /* Repetition */
        { "a*b",             "aaab" },
        { "ab+c",            "xabbbc" },
        { "colou?r",         "color" },
        { "colou?r",         "colour" },
        /* Dot */
        { "h.llo",           "hello" },
        { "a..b",            "axxb" },
        /* Anchors */
        { "^hello",          "hello world" },
        { "^hello",          "say hello" },
        { "world$",          "hello world" },
        { "^$",              "" },
        { "^line1$",         "line1\nline2" },
        /* Character classes */
        { "[a-z]+",          "HELLO world 123" },
        { "[A-Z][a-z]+",     "Hello World" },
        { "[^0-9]+",         "abc123" },
        { "[a-zA-Z0-9]+",   "test@host" },
        /* Shorthand classes */
        { "\\d+",            "abc 123 def" },
        { "\\w+",            "hello world" },
        { "\\s+",            "hello   world" },
        { "\\D+",            "123abc456" },
        /* Groups */
        { "(ab)+",           "xabababy" },
        { "(foo|bar)+",      "foobarfoo" },
        /* Complex */
        { "\\w+@\\w+",       "user@host" },
        { "[0-9]+\\.[0-9]+", "pi is 3.14" },
        { "^\\w+$",          "hello" },
        /* Multi-byte UTF-8 */
        { "\xc3\xa9",        "caf\xc3\xa9" },
    };
    size_t n = sizeof(cases) / sizeof(cases[0]);
    ASSERT(n >= 20); /* ensure at least 20 patterns */

    for (size_t i = 0; i < n; i++) {
        if (!assert_cross_match(cases[i].pat, cases[i].text)) {
            fprintf(stderr, "  battery case %zu failed: pattern='%s'\n",
                    i, cases[i].pat);
            return 0;
        }
    }

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 2: Iterator consistency across all three inputs */
static int test_cross_iter_battery(void) {
    tracker_reset();

    struct { const char* pat; const char* text; } cases[] = {
        { "[0-9]+",   "abc 123 def 456 ghi 789" },
        { "[a-z]+",   "Hello World Foo Bar" },
        { "\\s+",     "a  b   c    d" },
        { ".",        "abcd" },
        { "ab",       "ababab" },
        { "\\w+",     "one two three four five" },
        { "[A-Z]+",   "aAbBcCdD" },
        { "a*",       "bc" },  /* zero-length matches */
    };
    size_t n = sizeof(cases) / sizeof(cases[0]);

    for (size_t i = 0; i < n; i++) {
        if (!assert_cross_iter(cases[i].pat, cases[i].text)) {
            fprintf(stderr, "  iter battery case %zu failed\n", i);
            return 0;
        }
    }

    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 3: Pathological (a?){25}a{25} completes in linear time */
static int test_pathological(void) {
    tracker_reset();

    /* Build pattern: 25 "a?" followed by 25 "a" = a?a?a?...a?aaa...a */
    char pattern[76]; /* 25*2 + 25 + 1 */
    int pos = 0;
    for (int i = 0; i < 25; i++) {
        pattern[pos++] = 'a';
        pattern[pos++] = '?';
    }
    for (int i = 0; i < 25; i++) {
        pattern[pos++] = 'a';
    }
    pattern[pos] = '\0';

    /* Build input: 25 a's */
    char input_str[26];
    memset(input_str, 'a', 25);
    input_str[25] = '\0';

    nfa_program* prog = compile_pattern(pattern);
    ASSERT(prog != NULL);

    clock_t start = clock();

    nfa_input buf_inp = nfa_input_from_buf((const uint8_t*)input_str, 25);
    nfa_match m = nfa_exec(prog, &buf_inp);
    nfa_input_free(&buf_inp);

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    ASSERT(m.matched);
    ASSERT_SIZE_EQ(m.start, 0);
    ASSERT_SIZE_EQ(m.end, 25);
    nfa_match_free(&m);

    if (elapsed >= 1.0) {
        fprintf(stderr, "FAIL: pathological pattern took %.3f sec (limit 1.0s)\n", elapsed);
        nfa_program_free(prog);
        return 0;
    }

    /* Also verify on rope */
    rope r = rope_from_str((const uint8_t*)input_str, 25);
    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_match rm = nfa_exec(prog, &rope_inp);
    nfa_input_rope_free(&rope_inp);
    rope_unref(r);

    ASSERT(rm.matched);
    ASSERT_SIZE_EQ(rm.start, 0);
    ASSERT_SIZE_EQ(rm.end, 25);
    nfa_match_free(&rm);

    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 4: Large input (1MB) buffer and rope with many matches via iterator */
static int test_large_input(void) {
    tracker_reset();

    /* Build 1MB buffer: repeating "abc XYZ " (8 bytes) */
    size_t unit_len = 8;
    size_t target_size = 1024 * 1024;
    size_t count = target_size / unit_len;
    size_t total = count * unit_len;
    uint8_t* data = (uint8_t*)malloc(total);
    for (size_t i = 0; i < count; i++) {
        memcpy(data + i * unit_len, "abc XYZ ", unit_len);
    }

    nfa_program* prog = compile_pattern("XYZ");
    ASSERT(prog != NULL);

    /* Buffer iterator — count all matches */
    nfa_input buf_inp = nfa_input_from_buf(data, total);
    nfa_iter buf_it = nfa_iter_new(prog, buf_inp);
    size_t buf_count = 0;
    for (;;) {
        nfa_match m = nfa_iter_next(&buf_it);
        if (!m.matched) break;
        buf_count++;
    }
    nfa_iter_free(&buf_it);

    ASSERT_SIZE_EQ(buf_count, count);

    /* Rope iterator — count all matches */
    rope r = rope_from_str(data, total);
    nfa_input rope_inp = nfa_input_from_rope(r);
    nfa_iter rope_it = nfa_iter_new(prog, rope_inp);
    size_t rope_count = 0;
    for (;;) {
        nfa_match m = nfa_iter_next(&rope_it);
        if (!m.matched) break;
        rope_count++;
    }
    _nfa_rope_ctx_free(rope_it.input.ctx);
    nfa_iter_free(&rope_it);
    rope_unref(r);

    ASSERT_SIZE_EQ(rope_count, count);

    nfa_program_free(prog);
    free(data);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* Test 5: Rdoc-specific — same text with varying sentinel placement
 * produces consistent text-offset matches */
static int test_rdoc_sentinel_placement(void) {
    tracker_reset();

    const char* text = "The quick brown fox jumps over the lazy dog";
    size_t tlen = strlen(text);
    const char* pattern = "\\w+";

    nfa_program* prog = compile_pattern(pattern);
    ASSERT(prog != NULL);

    /* Variant 1: plain text, no sentinels */
    rdoc_builder b1 = rdoc_builder_new();
    rdoc_builder_text(&b1, (const uint8_t*)text, tlen);
    rdoc d1 = rdoc_builder_finish(&b1);

    /* Variant 2: inline sentinels wrapping each word */
    rdoc_builder b2 = rdoc_builder_new();
    {
        int id = 1;
        size_t i = 0;
        while (i < tlen) {
            /* Emit spaces as bare text */
            while (i < tlen && text[i] == ' ') {
                rdoc_builder_text(&b2, (const uint8_t*)&text[i], 1);
                i++;
            }
            if (i < tlen) {
                size_t wstart = i;
                while (i < tlen && text[i] != ' ') i++;
                rdoc_builder_inline_open(&b2, id);
                rdoc_builder_text(&b2, (const uint8_t*)&text[wstart], i - wstart);
                rdoc_builder_inline_close(&b2, id);
                id++;
            }
        }
    }
    rdoc d2 = rdoc_builder_finish(&b2);

    /* Variant 3: mark changes between each word */
    rdoc_builder b3 = rdoc_builder_new();
    {
        size_t i = 0;
        int mark_id = 1;
        while (i < tlen) {
            size_t wstart = i;
            while (i < tlen && text[i] != ' ') i++;
            rdoc_builder_text(&b3, (const uint8_t*)&text[wstart], i - wstart);
            if (i < tlen) {
                rdoc_builder_mark(&b3, 0x01, mark_id++);
                rdoc_builder_text(&b3, (const uint8_t*)&text[i], 1);
                i++;
            }
        }
    }
    rdoc d3 = rdoc_builder_finish(&b3);

    /* Collect matches from each variant using iterator */
    nfa_match matches_v1[20], matches_v2[20], matches_v3[20];
    size_t cnt1 = 0, cnt2 = 0, cnt3 = 0;

    nfa_input inp1 = nfa_input_from_rdoc(d1);
    nfa_iter it1 = nfa_iter_new(prog, inp1);
    for (size_t j = 0; j < 20; j++) {
        nfa_match m = nfa_iter_next(&it1);
        if (!m.matched) break;
        matches_v1[cnt1++] = m;
    }
    _nfa_rdoc_ctx_free(it1.input.ctx);
    nfa_iter_free(&it1);

    nfa_input inp2 = nfa_input_from_rdoc(d2);
    nfa_iter it2 = nfa_iter_new(prog, inp2);
    for (size_t j = 0; j < 20; j++) {
        nfa_match m = nfa_iter_next(&it2);
        if (!m.matched) break;
        matches_v2[cnt2++] = m;
    }
    _nfa_rdoc_ctx_free(it2.input.ctx);
    nfa_iter_free(&it2);

    nfa_input inp3 = nfa_input_from_rdoc(d3);
    nfa_iter it3 = nfa_iter_new(prog, inp3);
    for (size_t j = 0; j < 20; j++) {
        nfa_match m = nfa_iter_next(&it3);
        if (!m.matched) break;
        matches_v3[cnt3++] = m;
    }
    _nfa_rdoc_ctx_free(it3.input.ctx);
    nfa_iter_free(&it3);

    /* All three variants must produce same count and positions */
    ASSERT_SIZE_EQ(cnt2, cnt1);
    ASSERT_SIZE_EQ(cnt3, cnt1);
    ASSERT(cnt1 > 0); /* sanity: we should have found some words */

    for (size_t j = 0; j < cnt1; j++) {
        if (matches_v1[j].start != matches_v2[j].start ||
            matches_v1[j].end != matches_v2[j].end) {
            fprintf(stderr, "FAIL: match %zu: v1=[%zu,%zu) v2=[%zu,%zu)\n",
                    j, matches_v1[j].start, matches_v1[j].end,
                    matches_v2[j].start, matches_v2[j].end);
            rdoc_unref(d1); rdoc_unref(d2); rdoc_unref(d3);
            nfa_program_free(prog);
            return 0;
        }
        if (matches_v1[j].start != matches_v3[j].start ||
            matches_v1[j].end != matches_v3[j].end) {
            fprintf(stderr, "FAIL: match %zu: v1=[%zu,%zu) v3=[%zu,%zu)\n",
                    j, matches_v1[j].start, matches_v1[j].end,
                    matches_v3[j].start, matches_v3[j].end);
            rdoc_unref(d1); rdoc_unref(d2); rdoc_unref(d3);
            nfa_program_free(prog);
            return 0;
        }
    }

    rdoc_unref(d1);
    rdoc_unref(d2);
    rdoc_unref(d3);
    nfa_program_free(prog);
    ASSERT(check_no_leaks());
    TEST_PASS();
}

/* ── Runner ──────────────────────────────────────────────────────────── */

int main(void) {
    printf("test_cross_pattern_battery     "); if (!test_cross_pattern_battery()) return 1;
    printf("test_cross_iter_battery        "); if (!test_cross_iter_battery()) return 1;
    printf("test_pathological              "); if (!test_pathological()) return 1;
    printf("test_large_input               "); if (!test_large_input()) return 1;
    printf("test_rdoc_sentinel_placement   "); if (!test_rdoc_sentinel_placement()) return 1;

    printf("\nAll 5 tests passed!\n");
    return 0;
}
