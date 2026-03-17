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

/* === grapheme_find_safe_split tests === */

int test_grapheme_split_ascii(void) {
  printf("Running test_grapheme_split_ascii... ");

  /* ASCII: every byte is a grapheme boundary, so split should be unchanged */
  const uint8_t* s = (const uint8_t*)"Hello, world!";
  size_t len = 13;

  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 5), 5);
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 0), 0);
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, len), len);

  TEST_PASS();
}

int test_grapheme_split_combining_marks(void) {
  printf("Running test_grapheme_split_combining_marks... ");

  /* "e" + U+0301 (combining acute accent) + "x"
     e = 0x65, U+0301 = 0xCC 0x81, x = 0x78 */
  const uint8_t s[] = {0x65, 0xCC, 0x81, 0x78};
  size_t len = 4;

  /* Split at pos=1 (middle of "e + combining") should go back to 0 */
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 1), 0);

  /* Split at pos=2 (continuation byte of U+0301) should go back to 0 */
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 2), 0);

  /* Split at pos=3 ("x") is a valid grapheme boundary */
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 3), 3);

  TEST_PASS();
}

int test_grapheme_split_multiple_combining(void) {
  printf("Running test_grapheme_split_multiple_combining... ");

  /* "a" + U+0301 (acute) + U+0327 (cedilla) + "b"
     a = 0x61, U+0301 = 0xCC 0x81, U+0327 = 0xCC 0xA7, b = 0x62 */
  const uint8_t s[] = {0x61, 0xCC, 0x81, 0xCC, 0xA7, 0x62};
  size_t len = 6;

  /* Split anywhere in the combining sequence should return 0 (start of base) */
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 1), 0);
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 2), 0);
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 3), 0);
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 4), 0);

  /* Split at "b" is fine */
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 5), 5);

  TEST_PASS();
}

int test_grapheme_split_zwj_sequence(void) {
  printf("Running test_grapheme_split_zwj_sequence... ");

  /* Simple ZWJ sequence: man emoji + ZWJ + heart emoji
     U+1F468 (man) = F0 9F 91 A8
     U+200D (ZWJ) = E2 80 8D
     U+2764 (heart) = E2 9D A4
     U+FE0F (VS16) = EF B8 8F
     This forms a single grapheme cluster: man + ZWJ + heart + VS16 */
  const uint8_t s[] = {
    0xF0, 0x9F, 0x91, 0xA8,  /* U+1F468 man */
    0xE2, 0x80, 0x8D,         /* U+200D ZWJ */
    0xE2, 0x9D, 0xA4,         /* U+2764 heavy black heart */
    0xEF, 0xB8, 0x8F,         /* U+FE0F VS16 */
  };
  size_t len = 13;

  /* Every position inside the cluster should resolve back to 0 */
  for (size_t i = 1; i < len; i++) {
    size_t result = grapheme_find_safe_split(s, len, i);
    if (result != 0) {
      printf("FAIL: split at %zu returned %zu, expected 0\n", i, result);
      return 1;
    }
  }

  TEST_PASS();
}

int test_grapheme_split_regional_indicators(void) {
  printf("Running test_grapheme_split_regional_indicators... ");

  /* Flag emoji: U+1F1FA U+1F1F8 (US flag) + "x"
     U+1F1FA = F0 9F 87 BA
     U+1F1F8 = F0 9F 87 B8
     x = 0x78 */
  const uint8_t s[] = {
    0xF0, 0x9F, 0x87, 0xBA,  /* U+1F1FA */
    0xF0, 0x9F, 0x87, 0xB8,  /* U+1F1F8 */
    0x78                       /* x */
  };
  size_t len = 9;

  /* Split inside the RI pair should go back to 0 */
  for (size_t i = 1; i <= 7; i++) {
    size_t result = grapheme_find_safe_split(s, len, i);
    if (result != 0) {
      printf("FAIL: split at %zu returned %zu, expected 0\n", i, result);
      return 1;
    }
  }

  /* Split at "x" is a valid boundary */
  ASSERT_INT_EQ(grapheme_find_safe_split(s, len, 8), 8);

  TEST_PASS();
}

int test_grapheme_split_edge_cases(void) {
  printf("Running test_grapheme_split_edge_cases... ");

  /* pos == 0 returns 0 */
  const uint8_t s[] = {0x41};
  ASSERT_INT_EQ(grapheme_find_safe_split(s, 1, 0), 0);

  /* pos >= len returns pos */
  ASSERT_INT_EQ(grapheme_find_safe_split(s, 1, 1), 1);
  ASSERT_INT_EQ(grapheme_find_safe_split(s, 1, 5), 5);

  TEST_PASS();
}

/* === rope_from_str grapheme boundary tests === */

int test_rope_from_str_combining_near_boundary(void) {
  printf("Running test_rope_from_str_combining_near_boundary... ");
  tracker_reset();

  /* Create a string where a combining sequence falls near ROPE_LEAF_MAX (256).
     Fill with 'a' up to 254, then add "e" + U+0301 (combining acute).
     The combining mark at byte 255-256 must not be split from its base. */
  uint8_t buf[260];
  memset(buf, 'a', 254);
  buf[254] = 0x65;      /* 'e' */
  buf[255] = 0xCC;      /* U+0301 byte 1 */
  buf[256] = 0x81;      /* U+0301 byte 2 */
  buf[257] = 0x62;      /* 'b' */
  size_t len = 258;

  rope r = rope_from_str(buf, len);

  /* Verify total grapheme count matches flat count */
  size_t flat_graphemes = unicode_grapheme_count(buf, len);
  ASSERT_INT_EQ(rope_grapheme_count(r), flat_graphemes);

  /* Verify no leaf starts with an Extend/ZWJ/SpacingMark codepoint.
     Extract full content and verify round-trip. */
  uint8_t out[260];
  size_t written = rope_to_str(r, out, sizeof(out));
  ASSERT_INT_EQ(written, len);
  ASSERT(memcmp(buf, out, len) == 0);

  rope_unref(r);
  TEST_PASS();
}

int test_rope_from_str_multi_combining_at_boundary(void) {
  printf("Running test_rope_from_str_multi_combining_at_boundary... ");
  tracker_reset();

  /* String: 253 'a's + "e" + U+0301 + U+0327 (two combining marks) + "b"
     Base "e" at 253, combining at 254-257, "b" at 258.
     The leaf split must not separate e from its combining marks. */
  uint8_t buf[260];
  memset(buf, 'a', 253);
  buf[253] = 0x65;      /* 'e' */
  buf[254] = 0xCC;      /* U+0301 byte 1 */
  buf[255] = 0x81;      /* U+0301 byte 2 */
  buf[256] = 0xCC;      /* U+0327 byte 1 */
  buf[257] = 0xA7;      /* U+0327 byte 2 */
  buf[258] = 0x62;      /* 'b' */
  size_t len = 259;

  rope r = rope_from_str(buf, len);

  size_t flat_graphemes = unicode_grapheme_count(buf, len);
  ASSERT_INT_EQ(rope_grapheme_count(r), flat_graphemes);

  uint8_t out[260];
  size_t written = rope_to_str(r, out, sizeof(out));
  ASSERT_INT_EQ(written, len);
  ASSERT(memcmp(buf, out, len) == 0);

  rope_unref(r);
  TEST_PASS();
}

int test_rope_from_str_emoji_zwj_at_boundary(void) {
  printf("Running test_rope_from_str_emoji_zwj_at_boundary... ");
  tracker_reset();

  /* Place a ZWJ emoji sequence near byte 256.
     Fill with 'a' to byte 252, then:
     U+1F468 (4 bytes) at 252-255
     U+200D ZWJ (3 bytes) at 256-258
     U+2764 heart (3 bytes) at 259-261
     U+FE0F VS16 (3 bytes) at 262-264
     + "b" at 265
     The ZWJ sequence must not be split across leaves. */
  uint8_t buf[270];
  memset(buf, 'a', 252);
  /* U+1F468 man */
  buf[252] = 0xF0; buf[253] = 0x9F; buf[254] = 0x91; buf[255] = 0xA8;
  /* U+200D ZWJ */
  buf[256] = 0xE2; buf[257] = 0x80; buf[258] = 0x8D;
  /* U+2764 heart */
  buf[259] = 0xE2; buf[260] = 0x9D; buf[261] = 0xA4;
  /* U+FE0F VS16 */
  buf[262] = 0xEF; buf[263] = 0xB8; buf[264] = 0x8F;
  buf[265] = 0x62; /* 'b' */
  size_t len = 266;

  rope r = rope_from_str(buf, len);

  size_t flat_graphemes = unicode_grapheme_count(buf, len);
  ASSERT_INT_EQ(rope_grapheme_count(r), flat_graphemes);

  uint8_t out[270];
  size_t written = rope_to_str(r, out, sizeof(out));
  ASSERT_INT_EQ(written, len);
  ASSERT(memcmp(buf, out, len) == 0);

  rope_unref(r);
  TEST_PASS();
}

int test_rope_from_str_regional_indicators_at_boundary(void) {
  printf("Running test_rope_from_str_regional_indicators_at_boundary... ");
  tracker_reset();

  /* Place a flag emoji (RI pair) near byte 256.
     Fill with 'a' to byte 252, then:
     U+1F1FA (4 bytes) at 252-255
     U+1F1F8 (4 bytes) at 256-259
     + "b" at 260 */
  uint8_t buf[265];
  memset(buf, 'a', 252);
  buf[252] = 0xF0; buf[253] = 0x9F; buf[254] = 0x87; buf[255] = 0xBA;
  buf[256] = 0xF0; buf[257] = 0x9F; buf[258] = 0x87; buf[259] = 0xB8;
  buf[260] = 0x62;
  size_t len = 261;

  rope r = rope_from_str(buf, len);

  size_t flat_graphemes = unicode_grapheme_count(buf, len);
  ASSERT_INT_EQ(rope_grapheme_count(r), flat_graphemes);

  uint8_t out[265];
  size_t written = rope_to_str(r, out, sizeof(out));
  ASSERT_INT_EQ(written, len);
  ASSERT(memcmp(buf, out, len) == 0);

  rope_unref(r);
  TEST_PASS();
}

int test_rope_grapheme_count_matches_flat(void) {
  printf("Running test_rope_grapheme_count_matches_flat... ");
  tracker_reset();

  /* Build a large string with various Unicode grapheme clusters,
     create rope from it, and verify grapheme count matches flat. */
  uint8_t buf[600];
  size_t pos = 0;

  /* 200 'a' characters */
  memset(buf, 'a', 200);
  pos = 200;

  /* 20 combining sequences: e + U+0301 each (3 bytes, 1 grapheme) */
  for (int i = 0; i < 20; i++) {
    buf[pos++] = 0x65;  /* e */
    buf[pos++] = 0xCC;  /* U+0301 hi */
    buf[pos++] = 0x81;  /* U+0301 lo */
  }

  /* 100 more 'a's */
  memset(buf + pos, 'a', 100);
  pos += 100;

  /* 40 more 'b's */
  memset(buf + pos, 'b', 40);
  pos += 40;

  rope r = rope_from_str(buf, pos);
  size_t flat_count = unicode_grapheme_count(buf, pos);
  ASSERT_INT_EQ(rope_grapheme_count(r), flat_count);

  /* Expected: 200 + 20 + 100 + 40 = 360 graphemes */
  ASSERT_INT_EQ(flat_count, 360);

  uint8_t out[600];
  size_t written = rope_to_str(r, out, sizeof(out));
  ASSERT_INT_EQ(written, pos);
  ASSERT(memcmp(buf, out, pos) == 0);

  rope_unref(r);
  TEST_PASS();
}

/* === Main === */

int main(void) {
  int pass = 0, fail = 0;
  int total = 11;
  typedef int (*test_fn)(void);
  test_fn tests[] = {
    test_grapheme_split_ascii,
    test_grapheme_split_combining_marks,
    test_grapheme_split_multiple_combining,
    test_grapheme_split_zwj_sequence,
    test_grapheme_split_regional_indicators,
    test_grapheme_split_edge_cases,
    test_rope_from_str_combining_near_boundary,
    test_rope_from_str_multi_combining_at_boundary,
    test_rope_from_str_emoji_zwj_at_boundary,
    test_rope_from_str_regional_indicators_at_boundary,
    test_rope_grapheme_count_matches_flat,
  };

  for (int i = 0; i < total; i++) {
    if (tests[i]()) pass++;
    else fail++;
  }

  printf("\n  %d/%d tests passed\n", pass, total);
  return fail > 0 ? 1 : 0;
}
