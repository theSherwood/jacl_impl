/*
 * test_unicode_word.c — Tests for UAX #29 word boundary segmentation
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

/* Helper: collect all word boundaries into array, return count */
static size_t collect_boundaries(const uint8_t* src, size_t len,
                                  size_t* out, size_t cap) {
  size_t n = 0;
  out[n++] = 0;
  size_t pos = 0;
  while (pos < len && n < cap) {
    pos = unicode_word_next(src, len, pos);
    out[n++] = pos;
  }
  return n;
}

/* === English words with spaces/punctuation === */
int test_word_english(void) {
  const uint8_t* s = (const uint8_t*)"Hello world";
  /* Boundaries: 0|Hello|_|world|11 -> [0, 5, 6, 11] */
  size_t bounds[16];
  size_t nb = collect_boundaries(s, 11, bounds, 16);
  ASSERT(nb >= 4);
  ASSERT_SIZE_EQ(bounds[0], 0);
  ASSERT_SIZE_EQ(bounds[1], 5);   /* end of "Hello" */
  ASSERT_SIZE_EQ(bounds[2], 6);   /* end of " " */
  ASSERT_SIZE_EQ(bounds[3], 11);  /* end of "world" */
  TEST_PASS();
}

/* === Numbers with decimal points (WB12: Numeric × MidNum × Numeric) === */
int test_word_numbers(void) {
  const uint8_t* s = (const uint8_t*)"3.14";
  /* "3.14" should be one word segment (Numeric MidNum Numeric) */
  size_t bounds[16];
  size_t nb = collect_boundaries(s, 4, bounds, 16);
  ASSERT(nb >= 2);
  ASSERT_SIZE_EQ(bounds[0], 0);
  ASSERT_SIZE_EQ(bounds[nb - 1], 4);
  /* Should be "3.14" as a single word */
  ASSERT_SIZE_EQ(bounds[1], 4);
  TEST_PASS();
}

/* === Contractions (WB6/WB7: AHLetter × MidLetter × AHLetter) === */
int test_word_contractions(void) {
  /* "don't" — the apostrophe (U+0027) is Single_Quote, which is MidNumLetQ.
     WB6: AHLetter × (MidLetter|MidNumLetQ) AHLetter
     So "don't" should be one word. */
  const uint8_t* s = (const uint8_t*)"don't";
  size_t bounds[16];
  size_t nb = collect_boundaries(s, 5, bounds, 16);
  ASSERT(nb >= 2);
  ASSERT_SIZE_EQ(bounds[0], 0);
  ASSERT_SIZE_EQ(bounds[1], 5);  /* entire "don't" */
  TEST_PASS();
}

/* === Email-like sequences === */
int test_word_email_like(void) {
  /* "foo@bar" — @ is not a word break character in UAX #29 terms,
     it's Other, so it breaks: "foo" | "@" | "bar" */
  const uint8_t* s = (const uint8_t*)"foo@bar";
  size_t bounds[16];
  size_t nb = collect_boundaries(s, 7, bounds, 16);
  ASSERT(nb >= 4);
  ASSERT_SIZE_EQ(bounds[0], 0);
  ASSERT_SIZE_EQ(bounds[1], 3);   /* end of "foo" */
  ASSERT_SIZE_EQ(bounds[2], 4);   /* end of "@" */
  ASSERT_SIZE_EQ(bounds[3], 7);   /* end of "bar" */
  TEST_PASS();
}

/* === Mixed script === */
int test_word_mixed_script(void) {
  /* "hello世界" — Latin letters then CJK ideographs.
     CJK ideographs have WBP_OTHER, so: "hello" | "世" | "界" */
  uint32_t cps[] = { 'h', 'e', 'l', 'l', 'o', 0x4E16, 0x754C };
  uint8_t buf[32];
  size_t len = encode_cps(cps, 7, buf);

  size_t bounds[16];
  size_t nb = collect_boundaries(buf, len, bounds, 16);
  /* "hello" (5 bytes) | 世 (3 bytes) | 界 (3 bytes) */
  ASSERT(nb >= 4);
  ASSERT_SIZE_EQ(bounds[0], 0);
  ASSERT_SIZE_EQ(bounds[1], 5);  /* end of "hello" */
  TEST_PASS();
}

/* === _prev backward === */
int test_word_prev(void) {
  const uint8_t* s = (const uint8_t*)"Hello world";
  /* Word prev from end (11) should go to 6 (start of "world") */
  ASSERT_SIZE_EQ(unicode_word_prev(s, 11, 11), 6);
  /* Word prev from 6 should go to 5 (start of " ") */
  ASSERT_SIZE_EQ(unicode_word_prev(s, 11, 6), 5);
  /* Word prev from 5 should go to 0 (start of "Hello") */
  ASSERT_SIZE_EQ(unicode_word_prev(s, 11, 5), 0);
  /* Word prev from 0 should be 0 */
  ASSERT_SIZE_EQ(unicode_word_prev(s, 11, 0), 0);
  TEST_PASS();
}

/* === word_type_at classification === */
int test_word_type_at(void) {
  const uint8_t* s = (const uint8_t*)"Hello 123 !";
  /* "Hello" at pos 0 -> LETTER */
  ASSERT_INT_EQ(unicode_word_type_at(s, 11, 0), UNICODE_WORD_LETTER);
  ASSERT_INT_EQ(unicode_word_type_at(s, 11, 2), UNICODE_WORD_LETTER);
  /* " " at pos 5 -> SPACE */
  ASSERT_INT_EQ(unicode_word_type_at(s, 11, 5), UNICODE_WORD_SPACE);
  /* "123" at pos 6 -> NUMERIC */
  ASSERT_INT_EQ(unicode_word_type_at(s, 11, 6), UNICODE_WORD_NUMERIC);
  /* " " at pos 9 -> SPACE */
  ASSERT_INT_EQ(unicode_word_type_at(s, 11, 9), UNICODE_WORD_SPACE);
  /* "!" at pos 10 -> OTHER (exclamation is not MidLetter etc.) */
  ASSERT_INT_EQ(unicode_word_type_at(s, 11, 10), UNICODE_WORD_OTHER);
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

  printf("=== unicode/test_unicode_word ===\n\n");

  RUN(test_word_english);
  RUN(test_word_numbers);
  RUN(test_word_contractions);
  RUN(test_word_email_like);
  RUN(test_word_mixed_script);
  RUN(test_word_prev);
  RUN(test_word_type_at);

  printf("\n%d/%d tests passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
