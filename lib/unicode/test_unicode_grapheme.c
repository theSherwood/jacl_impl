/*
 * test_unicode_grapheme.c — Tests for UAX #29 grapheme cluster segmentation
 */

#include <stdio.h>
#include <string.h>

#include "../test/test_helpers.h"
#include "unicode.h"

/* Helper: encode codepoints to UTF-8 */
static size_t encode_cps(const uint32_t* cps, size_t count, uint8_t* buf) {
  size_t pos = 0;
  for (size_t i = 0; i < count; i++)
    pos += utf8_encode(cps[i], buf + pos);
  return pos;
}

/* === ASCII === */
int test_grapheme_ascii(void) {
  const uint8_t* s = (const uint8_t*)"hello";
  ASSERT_SIZE_EQ(unicode_grapheme_count(s, 5), 5);

  /* Each byte is its own grapheme boundary */
  ASSERT_SIZE_EQ(unicode_grapheme_next(s, 5, 0), 1);
  ASSERT_SIZE_EQ(unicode_grapheme_next(s, 5, 1), 2);
  ASSERT_SIZE_EQ(unicode_grapheme_next(s, 5, 4), 5);
  TEST_PASS();
}

/* === CRLF (GB3) === */
int test_grapheme_crlf(void) {
  const uint8_t s[] = {'a', '\r', '\n', 'b'};
  /* a | CR×LF | b = 3 graphemes */
  ASSERT_SIZE_EQ(unicode_grapheme_count(s, 4), 3);

  /* CR+LF is one cluster */
  ASSERT_SIZE_EQ(unicode_grapheme_next(s, 4, 1), 3);

  /* Lone CR */
  const uint8_t s2[] = {'a', '\r', 'b'};
  ASSERT_SIZE_EQ(unicode_grapheme_count(s2, 3), 3);
  TEST_PASS();
}

/* === Base + combining marks (GB9) === */
int test_grapheme_combining(void) {
  /* e + combining acute + combining cedilla = 1 grapheme cluster */
  uint32_t cps[] = { 0x0065, 0x0301, 0x0327 };
  uint8_t buf[16];
  size_t len = encode_cps(cps, 3, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);

  /* a + combining acute, b = 2 graphemes */
  uint32_t cps2[] = { 0x0061, 0x0301, 0x0062 };
  len = encode_cps(cps2, 3, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 2);
  TEST_PASS();
}

/* === Emoji flags (GB12/GB13 - Regional Indicator pairs) === */
int test_grapheme_flags(void) {
  /* US flag: U+1F1FA U+1F1F8 = 1 grapheme */
  uint32_t flag[] = { 0x1F1FA, 0x1F1F8 };
  uint8_t buf[16];
  size_t len = encode_cps(flag, 2, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);

  /* Three RI: pair + single = 2 graphemes */
  uint32_t three_ri[] = { 0x1F1FA, 0x1F1F8, 0x1F1EB };
  len = encode_cps(three_ri, 3, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 2);

  /* Four RI: two pairs = 2 graphemes */
  uint32_t four_ri[] = { 0x1F1FA, 0x1F1F8, 0x1F1EB, 0x1F1F7 };
  len = encode_cps(four_ri, 4, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 2);
  TEST_PASS();
}

/* === Emoji ZWJ sequences (GB11) === */
int test_grapheme_emoji_zwj(void) {
  /* Family emoji: person + ZWJ + person = 1 grapheme
     U+1F468 (man) + U+200D (ZWJ) + U+1F469 (woman) */
  uint32_t family[] = { 0x1F468, 0x200D, 0x1F469 };
  uint8_t buf[32];
  size_t len = encode_cps(family, 3, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);

  /* More complex: woman + ZWJ + heart + ZWJ + man
     U+1F469 + U+200D + U+2764 + U+FE0F + U+200D + U+1F468 */
  uint32_t complex[] = { 0x1F469, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468 };
  len = encode_cps(complex, 6, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);
  TEST_PASS();
}

/* === Hangul jamo (GB6-GB8) === */
int test_grapheme_hangul(void) {
  /* L + V = 1 grapheme (GB6) */
  uint32_t lv[] = { 0x1100, 0x1161 };
  uint8_t buf[16];
  size_t len = encode_cps(lv, 2, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);

  /* L + V + T = 1 grapheme (GB6 + GB7 combined with GB8) */
  uint32_t lvt[] = { 0x1100, 0x1161, 0x11A8 };
  len = encode_cps(lvt, 3, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);

  /* LV syllable + T = 1 grapheme (GB8) */
  uint32_t lvt2[] = { 0xAC00, 0x11A8 };  /* 가 + T */
  len = encode_cps(lvt2, 2, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);

  /* Two separate L = 1 grapheme (GB6: L × L) */
  uint32_t ll[] = { 0x1100, 0x1101 };
  len = encode_cps(ll, 2, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 1);
  TEST_PASS();
}

/* === _prev backward walk === */
int test_grapheme_prev(void) {
  const uint8_t* s = (const uint8_t*)"hello";
  /* Walk backward through ASCII */
  ASSERT_SIZE_EQ(unicode_grapheme_prev(s, 5, 5), 4);
  ASSERT_SIZE_EQ(unicode_grapheme_prev(s, 5, 4), 3);
  ASSERT_SIZE_EQ(unicode_grapheme_prev(s, 5, 1), 0);
  ASSERT_SIZE_EQ(unicode_grapheme_prev(s, 5, 0), 0);

  /* e + combining acute = 1 cluster; prev from end goes to start */
  uint32_t cps[] = { 0x0065, 0x0301 };
  uint8_t buf[16];
  size_t len = encode_cps(cps, 2, buf);
  ASSERT_SIZE_EQ(unicode_grapheme_prev(buf, len, len), 0);

  /* "a" + "e\u0301" + "b": prev from start of "b" should go to start of "e\u0301" */
  uint32_t cps2[] = { 0x0061, 0x0065, 0x0301, 0x0062 };
  len = encode_cps(cps2, 4, buf);
  /* buf = "a" (1 byte) + "e" (1 byte) + combining acute (2 bytes) + "b" (1 byte) = 5 bytes */
  size_t b_pos = 4; /* "b" starts at byte 4 */
  size_t prev = unicode_grapheme_prev(buf, len, b_pos);
  ASSERT_SIZE_EQ(prev, 1); /* "e\u0301" starts at byte 1 */
  TEST_PASS();
}

/* === _next/_prev round-trip === */
int test_grapheme_roundtrip(void) {
  /* Walk forward collecting boundaries, then backward */
  uint32_t cps[] = {
    0x0061,  /* a */
    0x0065, 0x0301,  /* e + combining acute */
    0x1F1FA, 0x1F1F8,  /* US flag */
    0x0062,  /* b */
  };
  uint8_t buf[32];
  size_t len = encode_cps(cps, 6, buf);

  /* Forward: 4 graphemes */
  ASSERT_SIZE_EQ(unicode_grapheme_count(buf, len), 4);

  /* Collect boundaries going forward */
  size_t boundaries[8];
  size_t nb = 0;
  boundaries[nb++] = 0;
  size_t pos = 0;
  while (pos < len) {
    pos = unicode_grapheme_next(buf, len, pos);
    if (pos <= len) boundaries[nb++] = pos;
  }
  /* boundaries: 0, end_of_a, end_of_é, end_of_flag, end */

  /* Walk backward from end and verify same boundaries in reverse */
  size_t rev[8];
  size_t nr = 0;
  pos = len;
  rev[nr++] = pos;
  while (pos > 0) {
    pos = unicode_grapheme_prev(buf, len, pos);
    rev[nr++] = pos;
  }

  /* Check match */
  ASSERT_SIZE_EQ(nb, nr);
  for (size_t i = 0; i < nb; i++) {
    ASSERT_SIZE_EQ(boundaries[i], rev[nb - 1 - i]);
  }

  TEST_PASS();
}

/* === main === */
int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("%-40s ", #fn); \
    if (fn()) { pass++; } \
    else { printf("FAIL\n"); fail++; } \
  } while(0)

  printf("=== unicode/test_unicode_grapheme ===\n\n");

  RUN(test_grapheme_ascii);
  RUN(test_grapheme_crlf);
  RUN(test_grapheme_combining);
  RUN(test_grapheme_flags);
  RUN(test_grapheme_emoji_zwj);
  RUN(test_grapheme_hangul);
  RUN(test_grapheme_prev);
  RUN(test_grapheme_roundtrip);

  printf("\n%d/%d tests passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
