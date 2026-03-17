#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../test/test_helpers.h"

/* Override allocator before including rope.h */
#define STREE_ALLOCATOR                 \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

#include "rope.h"

/* === rope_concat grapheme boundary tests === */

/* Right rope is just combining marks — bytes move to left */
int test_rope_concat_combining_moves_to_left(void) {
  printf("Running test_rope_concat_combining_moves_to_left... ");
  tracker_reset();

  /* Left: "e" */
  const uint8_t left_str[] = {0x65};
  rope left = rope_from_str(left_str, 1);

  /* Right: U+0301 (combining acute) + U+0327 (combining cedilla) */
  const uint8_t right_str[] = {0xCC, 0x81, 0xCC, 0xA7};
  rope right = rope_from_str(right_str, 4);

  rope result = rope_concat(left, right);

  /* Verify content is preserved */
  uint8_t out[10];
  size_t written = rope_to_str(result, out, sizeof(out));
  ASSERT_INT_EQ(written, 5);

  uint8_t expected[] = {0x65, 0xCC, 0x81, 0xCC, 0xA7};
  ASSERT(memcmp(out, expected, 5) == 0);

  /* Grapheme count should be 1 (e + combining acute + combining cedilla) */
  size_t flat_graphemes = unicode_grapheme_count(expected, 5);
  ASSERT_INT_EQ(rope_grapheme_count(result), flat_graphemes);
  ASSERT_INT_EQ(rope_grapheme_count(result), 1);

  rope_unref(left);
  rope_unref(right);
  rope_unref(result);
  TEST_PASS();
}

/* Right starts with a base character — no fix-up, counts sum correctly */
int test_rope_concat_no_fixup(void) {
  printf("Running test_rope_concat_no_fixup... ");
  tracker_reset();

  const uint8_t left_str[] = "Hello";
  rope left = rope_from_str(left_str, 5);

  const uint8_t right_str[] = " World";
  rope right = rope_from_str(right_str, 6);

  rope result = rope_concat(left, right);

  uint8_t out[20];
  size_t written = rope_to_str(result, out, sizeof(out));
  ASSERT_INT_EQ(written, 11);
  ASSERT(memcmp(out, "Hello World", 11) == 0);

  size_t flat_graphemes = unicode_grapheme_count(out, 11);
  ASSERT_INT_EQ(rope_grapheme_count(result), flat_graphemes);
  ASSERT_INT_EQ(rope_grapheme_count(result), 11);

  rope_unref(left);
  rope_unref(right);
  rope_unref(result);
  TEST_PASS();
}

/* Repeated concat operations maintain the invariant (no grapheme count drift) */
int test_rope_concat_repeated_invariant(void) {
  printf("Running test_rope_concat_repeated_invariant... ");
  tracker_reset();

  /* Build string: "a" + combining acute, repeated 10 times
     Each "a" + U+0301 is one grapheme cluster */
  rope result = rope_new();

  for (int i = 0; i < 10; i++) {
    /* Concat "a" */
    const uint8_t base[] = {0x61};
    rope base_rope = rope_from_str(base, 1);
    rope tmp = rope_concat(result, base_rope);
    rope_unref(result);
    rope_unref(base_rope);
    result = tmp;

    /* Concat combining acute (U+0301) */
    const uint8_t comb[] = {0xCC, 0x81};
    rope comb_rope = rope_from_str(comb, 2);
    tmp = rope_concat(result, comb_rope);
    rope_unref(result);
    rope_unref(comb_rope);
    result = tmp;
  }

  /* Should have 10 graphemes (each a + combining = 1 grapheme) */
  ASSERT_INT_EQ(rope_grapheme_count(result), 10);

  /* Verify total bytes: 10 * 3 = 30 */
  ASSERT_INT_EQ(rope_byte_count(result), 30);

  /* Verify against flat materialization */
  uint8_t flat[30];
  rope_to_str(result, flat, 30);
  ASSERT_INT_EQ(unicode_grapheme_count(flat, 30), 10);

  rope_unref(result);
  TEST_PASS();
}

/* Grapheme count after concat matches unicode_grapheme_count on flat string */
int test_rope_concat_grapheme_count_matches_flat(void) {
  printf("Running test_rope_concat_grapheme_count_matches_flat... ");
  tracker_reset();

  /* Left: 200 'a's + "e" */
  uint8_t left_buf[201];
  memset(left_buf, 'a', 200);
  left_buf[200] = 0x65; /* 'e' */
  rope left = rope_from_str(left_buf, 201);

  /* Right: U+0301 (combining acute) + 100 'b's */
  uint8_t right_buf[102];
  right_buf[0] = 0xCC;
  right_buf[1] = 0x81;
  memset(right_buf + 2, 'b', 100);
  rope right = rope_from_str(right_buf, 102);

  rope result = rope_concat(left, right);

  /* Verify content */
  uint8_t flat[303];
  size_t written = rope_to_str(result, flat, sizeof(flat));
  ASSERT_INT_EQ(written, 303);

  /* Verify grapheme count matches flat computation */
  size_t flat_graphemes = unicode_grapheme_count(flat, written);
  ASSERT_INT_EQ(rope_grapheme_count(result), flat_graphemes);

  /* Expected: 200 + 1 (e+combining = 1 grapheme) + 100 = 301 */
  ASSERT_INT_EQ(flat_graphemes, 301);

  rope_unref(left);
  rope_unref(right);
  rope_unref(result);
  TEST_PASS();
}

/* Empty rope concat edge cases */
int test_rope_concat_empty_cases(void) {
  printf("Running test_rope_concat_empty_cases... ");
  tracker_reset();

  rope empty = rope_new();
  rope hello = rope_from_str((const uint8_t*)"hello", 5);

  /* empty + hello = hello */
  rope r1 = rope_concat(empty, hello);
  ASSERT_INT_EQ(rope_byte_count(r1), 5);
  ASSERT_INT_EQ(rope_grapheme_count(r1), 5);

  /* hello + empty = hello */
  rope r2 = rope_concat(hello, empty);
  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  ASSERT_INT_EQ(rope_grapheme_count(r2), 5);

  /* empty + empty = empty */
  rope r3 = rope_concat(empty, empty);
  ASSERT_INT_EQ(rope_byte_count(r3), 0);

  rope_unref(empty);
  rope_unref(hello);
  rope_unref(r1);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

/* ZWJ at junction: right starts with ZWJ extending left's emoji */
int test_rope_concat_zwj_at_junction(void) {
  printf("Running test_rope_concat_zwj_at_junction... ");
  tracker_reset();

  /* Left: U+1F468 (man emoji) */
  const uint8_t left_str[] = {0xF0, 0x9F, 0x91, 0xA8};
  rope left = rope_from_str(left_str, 4);

  /* Right: ZWJ + heart + VS16 */
  const uint8_t right_str[] = {
    0xE2, 0x80, 0x8D,  /* U+200D ZWJ */
    0xE2, 0x9D, 0xA4,  /* U+2764 heart */
    0xEF, 0xB8, 0x8F   /* U+FE0F VS16 */
  };
  rope right = rope_from_str(right_str, 9);

  rope result = rope_concat(left, right);

  uint8_t out[20];
  size_t written = rope_to_str(result, out, sizeof(out));
  ASSERT_INT_EQ(written, 13);

  /* Grapheme count should match flat */
  size_t flat_graphemes = unicode_grapheme_count(out, written);
  ASSERT_INT_EQ(rope_grapheme_count(result), flat_graphemes);
  /* The ZWJ sequence forms 1 grapheme cluster */
  ASSERT_INT_EQ(flat_graphemes, 1);

  rope_unref(left);
  rope_unref(right);
  rope_unref(result);
  TEST_PASS();
}

/* Large multi-leaf ropes with combining at junction */
int test_rope_concat_large_with_combining(void) {
  printf("Running test_rope_concat_large_with_combining... ");
  tracker_reset();

  /* Left: 300 'a's + "e" (> ROPE_LEAF_MAX, multi-leaf rope) */
  uint8_t left_buf[301];
  memset(left_buf, 'a', 300);
  left_buf[300] = 0x65; /* 'e' */
  rope left = rope_from_str(left_buf, 301);

  /* Right: U+0301 + U+0327 + 300 'b's */
  uint8_t right_buf[304];
  right_buf[0] = 0xCC; right_buf[1] = 0x81; /* U+0301 */
  right_buf[2] = 0xCC; right_buf[3] = 0xA7; /* U+0327 */
  memset(right_buf + 4, 'b', 300);
  rope right = rope_from_str(right_buf, 304);

  rope result = rope_concat(left, right);

  /* Verify total bytes */
  ASSERT_INT_EQ(rope_byte_count(result), 605);

  /* Verify content round-trip */
  uint8_t flat[605];
  size_t written = rope_to_str(result, flat, sizeof(flat));
  ASSERT_INT_EQ(written, 605);

  /* Verify grapheme count matches flat */
  size_t flat_graphemes = unicode_grapheme_count(flat, written);
  ASSERT_INT_EQ(rope_grapheme_count(result), flat_graphemes);
  /* 300 + 1 (e+0301+0327) + 300 = 601 */
  ASSERT_INT_EQ(flat_graphemes, 601);

  rope_unref(left);
  rope_unref(right);
  rope_unref(result);
  TEST_PASS();
}

/* Right is entirely combining marks */
int test_rope_concat_right_all_combining(void) {
  printf("Running test_rope_concat_right_all_combining... ");
  tracker_reset();

  /* Left: "abc" */
  rope left = rope_from_str((const uint8_t*)"abc", 3);

  /* Right: U+0301 + U+0302 + U+0303 (three combining marks) */
  const uint8_t right_str[] = {
    0xCC, 0x81,  /* U+0301 combining acute */
    0xCC, 0x82,  /* U+0302 combining circumflex */
    0xCC, 0x83   /* U+0303 combining tilde */
  };
  rope right = rope_from_str(right_str, 6);

  rope result = rope_concat(left, right);

  uint8_t out[10];
  size_t written = rope_to_str(result, out, sizeof(out));
  ASSERT_INT_EQ(written, 9);

  /* Grapheme count: "a", "b", "c" + 3 combining = 3 graphemes
     (combining marks attach to "c") */
  size_t flat_graphemes = unicode_grapheme_count(out, written);
  ASSERT_INT_EQ(rope_grapheme_count(result), flat_graphemes);
  ASSERT_INT_EQ(flat_graphemes, 3);

  rope_unref(left);
  rope_unref(right);
  rope_unref(result);
  TEST_PASS();
}

/* === Main === */

int main(void) {
  int pass = 0, fail = 0;
  int total = 8;
  typedef int (*test_fn)(void);
  test_fn tests[] = {
    test_rope_concat_combining_moves_to_left,
    test_rope_concat_no_fixup,
    test_rope_concat_repeated_invariant,
    test_rope_concat_grapheme_count_matches_flat,
    test_rope_concat_empty_cases,
    test_rope_concat_zwj_at_junction,
    test_rope_concat_large_with_combining,
    test_rope_concat_right_all_combining,
  };

  for (int i = 0; i < total; i++) {
    if (tests[i]()) pass++;
    else fail++;
  }

  printf("\n  %d/%d tests passed\n", pass, total);
  return fail > 0 ? 1 : 0;
}
