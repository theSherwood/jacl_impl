#include <stdio.h>
#include <string.h>

#include "../test/test_helpers.h"
#include "utf8.h"

/* === utf8_codepoint_length === */

int test_codepoint_length(void) {
  printf("Running test_codepoint_length... ");

  /* ASCII */
  ASSERT_INT_EQ(utf8_codepoint_length(0x00), 1);
  ASSERT_INT_EQ(utf8_codepoint_length('A'), 1);
  ASSERT_INT_EQ(utf8_codepoint_length(0x7F), 1);

  /* 2-byte lead */
  ASSERT_INT_EQ(utf8_codepoint_length(0xC3), 2);  /* e.g. U+00E9 */

  /* 3-byte lead */
  ASSERT_INT_EQ(utf8_codepoint_length(0xE4), 3);  /* e.g. U+4E16 */

  /* 4-byte lead */
  ASSERT_INT_EQ(utf8_codepoint_length(0xF0), 4);  /* e.g. U+1F30D */

  /* Continuation byte → 0 */
  ASSERT_INT_EQ(utf8_codepoint_length(0x80), 0);
  ASSERT_INT_EQ(utf8_codepoint_length(0xBF), 0);

  /* Invalid lead bytes → 0 */
  ASSERT_INT_EQ(utf8_codepoint_length(0xF8), 0);
  ASSERT_INT_EQ(utf8_codepoint_length(0xFF), 0);

  TEST_PASS();
}

/* === utf8_is_continuation === */

int test_is_continuation(void) {
  printf("Running test_is_continuation... ");

  ASSERT(utf8_is_continuation(0x80));
  ASSERT(utf8_is_continuation(0xBF));
  ASSERT(utf8_is_continuation(0xA0));
  ASSERT(!utf8_is_continuation(0x00));
  ASSERT(!utf8_is_continuation(0x7F));
  ASSERT(!utf8_is_continuation(0xC0));
  ASSERT(!utf8_is_continuation(0xFF));

  TEST_PASS();
}

/* === utf8_decode === */

int test_decode_ascii(void) {
  printf("Running test_decode_ascii... ");

  uint8_t buf[] = {'H', 'i'};
  uint32_t cp;
  size_t n = utf8_decode(buf, 2, &cp);
  ASSERT_INT_EQ(n, 1);
  ASSERT(cp == 'H');

  TEST_PASS();
}

int test_decode_2byte(void) {
  printf("Running test_decode_2byte... ");

  /* U+00E9 = e-acute = 0xC3 0xA9 */
  uint8_t buf[] = {0xC3, 0xA9};
  uint32_t cp;
  size_t n = utf8_decode(buf, 2, &cp);
  ASSERT_INT_EQ(n, 2);
  ASSERT(cp == 0x00E9);

  TEST_PASS();
}

int test_decode_3byte(void) {
  printf("Running test_decode_3byte... ");

  /* U+4E16 = 世 = 0xE4 0xB8 0x96 */
  uint8_t buf[] = {0xE4, 0xB8, 0x96};
  uint32_t cp;
  size_t n = utf8_decode(buf, 3, &cp);
  ASSERT_INT_EQ(n, 3);
  ASSERT(cp == 0x4E16);

  TEST_PASS();
}

int test_decode_4byte(void) {
  printf("Running test_decode_4byte... ");

  /* U+1F30D = globe emoji = 0xF0 0x9F 0x8C 0x8D */
  uint8_t buf[] = {0xF0, 0x9F, 0x8C, 0x8D};
  uint32_t cp;
  size_t n = utf8_decode(buf, 4, &cp);
  ASSERT_INT_EQ(n, 4);
  ASSERT(cp == 0x1F30D);

  TEST_PASS();
}

int test_decode_empty(void) {
  printf("Running test_decode_empty... ");

  uint32_t cp = 0xDEAD;
  size_t n = utf8_decode(NULL, 0, &cp);
  ASSERT_INT_EQ(n, 0);

  TEST_PASS();
}

int test_decode_truncated(void) {
  printf("Running test_decode_truncated... ");

  /* 3-byte lead but only 2 bytes available */
  uint8_t buf[] = {0xE4, 0xB8};
  uint32_t cp;
  size_t n = utf8_decode(buf, 2, &cp);
  ASSERT_INT_EQ(n, 0);

  TEST_PASS();
}

int test_decode_orphan_continuation(void) {
  printf("Running test_decode_orphan_continuation... ");

  uint8_t buf[] = {0x80};
  uint32_t cp;
  size_t n = utf8_decode(buf, 1, &cp);
  ASSERT_INT_EQ(n, 0);

  TEST_PASS();
}

int test_decode_overlong(void) {
  printf("Running test_decode_overlong... ");

  /* Overlong encoding of U+002F (/) as 2-byte: 0xC0 0xAF */
  uint8_t buf[] = {0xC0, 0xAF};
  uint32_t cp;
  size_t n = utf8_decode(buf, 2, &cp);
  ASSERT_INT_EQ(n, 0);

  /* Overlong 3-byte encoding of U+0041 ('A'): 0xE0 0x81 0x81 */
  uint8_t buf2[] = {0xE0, 0x81, 0x81};
  n = utf8_decode(buf2, 3, &cp);
  ASSERT_INT_EQ(n, 0);

  TEST_PASS();
}

/* === utf8_validate === */

int test_validate_ascii(void) {
  printf("Running test_validate_ascii... ");

  const uint8_t* s = (const uint8_t*)"Hello, world!";
  ASSERT(utf8_validate(s, strlen((const char*)s)));

  TEST_PASS();
}

int test_validate_multibyte(void) {
  printf("Running test_validate_multibyte... ");

  /* "e\xCC\x81世\xF0\x9F\x8C\x8D" = e + combining acute + 世 + globe */
  uint8_t buf[] = {'e', 0xC3, 0xA9, 0xE4, 0xB8, 0x96, 0xF0, 0x9F, 0x8C, 0x8D};
  ASSERT(utf8_validate(buf, sizeof(buf)));

  TEST_PASS();
}

int test_validate_empty(void) {
  printf("Running test_validate_empty... ");

  ASSERT(utf8_validate((const uint8_t*)"", 0));

  TEST_PASS();
}

int test_validate_invalid(void) {
  printf("Running test_validate_invalid... ");

  /* Orphan continuation */
  uint8_t a[] = {0x80};
  ASSERT(!utf8_validate(a, 1));

  /* Truncated multi-byte */
  uint8_t b[] = {0xE4, 0xB8};
  ASSERT(!utf8_validate(b, 2));

  /* Overlong */
  uint8_t c[] = {0xC0, 0xAF};
  ASSERT(!utf8_validate(c, 2));

  TEST_PASS();
}

/* === utf8_codepoint_count === */

int test_codepoint_count_ascii(void) {
  printf("Running test_codepoint_count_ascii... ");

  const uint8_t* s = (const uint8_t*)"Hello";
  ASSERT_INT_EQ(utf8_codepoint_count(s, 5), 5);

  TEST_PASS();
}

int test_codepoint_count_multibyte(void) {
  printf("Running test_codepoint_count_multibyte... ");

  /* "e\xC3\xA9" = e + e-acute = 2 codepoints in 3 bytes */
  uint8_t buf[] = {'e', 0xC3, 0xA9};
  ASSERT_INT_EQ(utf8_codepoint_count(buf, 3), 2);

  /* "世界" = 2 codepoints in 6 bytes */
  uint8_t buf2[] = {0xE4, 0xB8, 0x96, 0xE7, 0x95, 0x8C};
  ASSERT_INT_EQ(utf8_codepoint_count(buf2, 6), 2);

  /* globe emoji = 1 codepoint in 4 bytes */
  uint8_t buf3[] = {0xF0, 0x9F, 0x8C, 0x8D};
  ASSERT_INT_EQ(utf8_codepoint_count(buf3, 4), 1);

  TEST_PASS();
}

int test_codepoint_count_empty(void) {
  printf("Running test_codepoint_count_empty... ");

  ASSERT_INT_EQ(utf8_codepoint_count((const uint8_t*)"", 0), 0);

  TEST_PASS();
}

/* === utf8_line_count === */

int test_line_count(void) {
  printf("Running test_line_count... ");

  const uint8_t* s = (const uint8_t*)"hello\nworld\n";
  ASSERT_INT_EQ(utf8_line_count(s, 12), 2);

  ASSERT_INT_EQ(utf8_line_count((const uint8_t*)"no newlines", 11), 0);
  ASSERT_INT_EQ(utf8_line_count((const uint8_t*)"", 0), 0);
  ASSERT_INT_EQ(utf8_line_count((const uint8_t*)"\n", 1), 1);

  TEST_PASS();
}

/* === utf8_find_safe_split === */

int test_safe_split_on_boundary(void) {
  printf("Running test_safe_split_on_boundary... ");

  /* "A世B" = 0x41 0xE4 0xB8 0x96 0x42 */
  uint8_t buf[] = {0x41, 0xE4, 0xB8, 0x96, 0x42};

  /* pos=0: already on boundary */
  ASSERT_INT_EQ(utf8_find_safe_split(buf, 5, 0), 0);
  /* pos=1: start of 3-byte char, on boundary */
  ASSERT_INT_EQ(utf8_find_safe_split(buf, 5, 1), 1);
  /* pos=4: start of 'B', on boundary */
  ASSERT_INT_EQ(utf8_find_safe_split(buf, 5, 4), 4);

  TEST_PASS();
}

int test_safe_split_mid_sequence(void) {
  printf("Running test_safe_split_mid_sequence... ");

  /* "A世B" = 0x41 0xE4 0xB8 0x96 0x42 */
  uint8_t buf[] = {0x41, 0xE4, 0xB8, 0x96, 0x42};

  /* pos=2: mid-sequence of 世, should back up to 1 */
  ASSERT_INT_EQ(utf8_find_safe_split(buf, 5, 2), 1);
  /* pos=3: mid-sequence of 世, should back up to 1 */
  ASSERT_INT_EQ(utf8_find_safe_split(buf, 5, 3), 1);

  TEST_PASS();
}

int test_safe_split_4byte(void) {
  printf("Running test_safe_split_4byte... ");

  /* globe emoji = 0xF0 0x9F 0x8C 0x8D */
  uint8_t buf[] = {0xF0, 0x9F, 0x8C, 0x8D};

  ASSERT_INT_EQ(utf8_find_safe_split(buf, 4, 1), 0);
  ASSERT_INT_EQ(utf8_find_safe_split(buf, 4, 2), 0);
  ASSERT_INT_EQ(utf8_find_safe_split(buf, 4, 3), 0);

  TEST_PASS();
}

/* === utf8_grapheme_count === */

int test_grapheme_count_ascii(void) {
  printf("Running test_grapheme_count_ascii... ");

  const uint8_t* s = (const uint8_t*)"Hello";
  ASSERT_INT_EQ(utf8_grapheme_count(s, 5), 5);

  TEST_PASS();
}

int test_grapheme_count_combining(void) {
  printf("Running test_grapheme_count_combining... ");

  /* 'e' + combining acute (U+0301) = 1 grapheme */
  uint8_t buf[] = {'e', 0xCC, 0x81};  /* U+0301 = 0xCC 0x81 */
  ASSERT_INT_EQ(utf8_grapheme_count(buf, 3), 1);

  TEST_PASS();
}

int test_grapheme_count_crlf(void) {
  printf("Running test_grapheme_count_crlf... ");

  /* "a\r\nb" = 3 graphemes: 'a', CRLF, 'b' */
  const uint8_t* s = (const uint8_t*)"a\r\nb";
  ASSERT_INT_EQ(utf8_grapheme_count(s, 4), 3);

  TEST_PASS();
}

int test_grapheme_count_multiple_combining(void) {
  printf("Running test_grapheme_count_multiple_combining... ");

  /* 'a' + U+0300 (grave) + U+0301 (acute) = 1 grapheme */
  uint8_t buf[] = {'a', 0xCC, 0x80, 0xCC, 0x81};
  ASSERT_INT_EQ(utf8_grapheme_count(buf, 5), 1);

  TEST_PASS();
}

int test_grapheme_count_mixed(void) {
  printf("Running test_grapheme_count_mixed... ");

  /* "He" + combining acute + "lo" = 4 graphemes: H, e+combining, l, o */
  uint8_t buf[] = {'H', 'e', 0xCC, 0x81, 'l', 'o'};
  ASSERT_INT_EQ(utf8_grapheme_count(buf, 6), 4);

  TEST_PASS();
}

int test_grapheme_count_empty(void) {
  printf("Running test_grapheme_count_empty... ");

  ASSERT_INT_EQ(utf8_grapheme_count((const uint8_t*)"", 0), 0);

  TEST_PASS();
}

/* === main === */

int main(void) {
  int passed = 0;
  int failed = 0;

#define RUN_TEST(func) \
  do {                 \
    if (func()) {      \
      passed++;        \
    } else {           \
      failed++;        \
    }                  \
  } while (0)

  /* codepoint_length */
  RUN_TEST(test_codepoint_length);

  /* is_continuation */
  RUN_TEST(test_is_continuation);

  /* decode */
  RUN_TEST(test_decode_ascii);
  RUN_TEST(test_decode_2byte);
  RUN_TEST(test_decode_3byte);
  RUN_TEST(test_decode_4byte);
  RUN_TEST(test_decode_empty);
  RUN_TEST(test_decode_truncated);
  RUN_TEST(test_decode_orphan_continuation);
  RUN_TEST(test_decode_overlong);

  /* validate */
  RUN_TEST(test_validate_ascii);
  RUN_TEST(test_validate_multibyte);
  RUN_TEST(test_validate_empty);
  RUN_TEST(test_validate_invalid);

  /* codepoint_count */
  RUN_TEST(test_codepoint_count_ascii);
  RUN_TEST(test_codepoint_count_multibyte);
  RUN_TEST(test_codepoint_count_empty);

  /* line_count */
  RUN_TEST(test_line_count);

  /* safe_split */
  RUN_TEST(test_safe_split_on_boundary);
  RUN_TEST(test_safe_split_mid_sequence);
  RUN_TEST(test_safe_split_4byte);

  /* grapheme_count */
  RUN_TEST(test_grapheme_count_ascii);
  RUN_TEST(test_grapheme_count_combining);
  RUN_TEST(test_grapheme_count_crlf);
  RUN_TEST(test_grapheme_count_multiple_combining);
  RUN_TEST(test_grapheme_count_mixed);
  RUN_TEST(test_grapheme_count_empty);

  printf("\nutf8: %d passed, %d failed\n", passed, failed);
  return failed > 0 ? 1 : 0;
}
