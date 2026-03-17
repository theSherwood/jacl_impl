/*
 * test_unicode_utf8.c — Tests for UTF-8 codec functions in unicode/unicode.h
 */

#include <stdio.h>
#include <string.h>

#include "../test/test_helpers.h"
#include "unicode.h"

/* === utf8_encode === */

int test_encode_ascii(void) {
  uint8_t buf[4];
  ASSERT_SIZE_EQ(utf8_encode(0x41, buf), 1);  /* 'A' */
  ASSERT_INT_EQ(buf[0], 0x41);
  ASSERT_SIZE_EQ(utf8_encode(0x00, buf), 1);  /* NUL */
  ASSERT_INT_EQ(buf[0], 0x00);
  ASSERT_SIZE_EQ(utf8_encode(0x7F, buf), 1);  /* DEL */
  ASSERT_INT_EQ(buf[0], 0x7F);
  TEST_PASS();
}

int test_encode_2byte(void) {
  uint8_t buf[4];
  /* U+00E9 = é */
  ASSERT_SIZE_EQ(utf8_encode(0xE9, buf), 2);
  ASSERT_INT_EQ(buf[0], 0xC3);
  ASSERT_INT_EQ(buf[1], 0xA9);
  /* U+0080 - lowest 2-byte */
  ASSERT_SIZE_EQ(utf8_encode(0x80, buf), 2);
  ASSERT_INT_EQ(buf[0], 0xC2);
  ASSERT_INT_EQ(buf[1], 0x80);
  /* U+07FF - highest 2-byte */
  ASSERT_SIZE_EQ(utf8_encode(0x7FF, buf), 2);
  ASSERT_INT_EQ(buf[0], 0xDF);
  ASSERT_INT_EQ(buf[1], 0xBF);
  TEST_PASS();
}

int test_encode_3byte(void) {
  uint8_t buf[4];
  /* U+4E16 = 世 */
  ASSERT_SIZE_EQ(utf8_encode(0x4E16, buf), 3);
  ASSERT_INT_EQ(buf[0], 0xE4);
  ASSERT_INT_EQ(buf[1], 0xB8);
  ASSERT_INT_EQ(buf[2], 0x96);
  /* U+0800 - lowest 3-byte */
  ASSERT_SIZE_EQ(utf8_encode(0x800, buf), 3);
  ASSERT_INT_EQ(buf[0], 0xE0);
  ASSERT_INT_EQ(buf[1], 0xA0);
  ASSERT_INT_EQ(buf[2], 0x80);
  /* U+FFFF - highest 3-byte */
  ASSERT_SIZE_EQ(utf8_encode(0xFFFF, buf), 3);
  ASSERT_INT_EQ(buf[0], 0xEF);
  ASSERT_INT_EQ(buf[1], 0xBF);
  ASSERT_INT_EQ(buf[2], 0xBF);
  TEST_PASS();
}

int test_encode_4byte(void) {
  uint8_t buf[4];
  /* U+1F30D = 🌍 */
  ASSERT_SIZE_EQ(utf8_encode(0x1F30D, buf), 4);
  ASSERT_INT_EQ(buf[0], 0xF0);
  ASSERT_INT_EQ(buf[1], 0x9F);
  ASSERT_INT_EQ(buf[2], 0x8C);
  ASSERT_INT_EQ(buf[3], 0x8D);
  /* U+10000 - lowest 4-byte */
  ASSERT_SIZE_EQ(utf8_encode(0x10000, buf), 4);
  ASSERT_INT_EQ(buf[0], 0xF0);
  ASSERT_INT_EQ(buf[1], 0x90);
  ASSERT_INT_EQ(buf[2], 0x80);
  ASSERT_INT_EQ(buf[3], 0x80);
  /* U+10FFFF - highest valid codepoint */
  ASSERT_SIZE_EQ(utf8_encode(0x10FFFF, buf), 4);
  ASSERT_INT_EQ(buf[0], 0xF4);
  ASSERT_INT_EQ(buf[1], 0x8F);
  ASSERT_INT_EQ(buf[2], 0xBF);
  ASSERT_INT_EQ(buf[3], 0xBF);
  TEST_PASS();
}

int test_encode_invalid(void) {
  uint8_t buf[4];
  /* Surrogates must be rejected */
  ASSERT_SIZE_EQ(utf8_encode(0xD800, buf), 0);
  ASSERT_SIZE_EQ(utf8_encode(0xDBFF, buf), 0);
  ASSERT_SIZE_EQ(utf8_encode(0xDC00, buf), 0);
  ASSERT_SIZE_EQ(utf8_encode(0xDFFF, buf), 0);
  /* Out of range */
  ASSERT_SIZE_EQ(utf8_encode(0x110000, buf), 0);
  ASSERT_SIZE_EQ(utf8_encode(0xFFFFFFFF, buf), 0);
  TEST_PASS();
}

/* === encode/decode round-trip === */

int test_encode_decode_roundtrip(void) {
  /* Test every boundary codepoint */
  uint32_t test_cps[] = {
    0x0, 0x1, 0x7E, 0x7F,              /* 1-byte boundaries */
    0x80, 0x81, 0x7FE, 0x7FF,          /* 2-byte boundaries */
    0x800, 0x801, 0xD7FF,              /* 3-byte before surrogates */
    0xE000, 0xFFFD, 0xFFFF,           /* 3-byte after surrogates */
    0x10000, 0x10001, 0x10FFFE, 0x10FFFF,  /* 4-byte boundaries */
  };
  size_t n = sizeof(test_cps) / sizeof(test_cps[0]);

  for (size_t i = 0; i < n; i++) {
    uint32_t cp = test_cps[i];
    uint8_t buf[4];
    size_t enc = utf8_encode(cp, buf);
    ASSERT(enc > 0);
    ASSERT(enc <= 4);

    uint32_t decoded;
    size_t dec = utf8_decode(buf, enc, &decoded);
    ASSERT_SIZE_EQ(dec, enc);
    ASSERT(decoded == cp);
  }
  TEST_PASS();
}

int test_encode_decode_roundtrip_samples(void) {
  /* Sample from each plane */
  uint32_t samples[] = {
    'A', 0xE9, 0x0301, 0x4E16, 0x1F600, 0x1F1FA, 0x10FFFF,
  };
  size_t n = sizeof(samples) / sizeof(samples[0]);

  for (size_t i = 0; i < n; i++) {
    uint8_t buf[4];
    size_t enc = utf8_encode(samples[i], buf);
    ASSERT(enc > 0);

    uint32_t decoded;
    size_t dec = utf8_decode(buf, enc, &decoded);
    ASSERT_SIZE_EQ(dec, enc);
    ASSERT(decoded == samples[i]);
  }
  TEST_PASS();
}

/* === codec functions from original utf8.h === */

int test_codepoint_length(void) {
  ASSERT_SIZE_EQ(utf8_codepoint_length(0x00), 1);
  ASSERT_SIZE_EQ(utf8_codepoint_length('A'), 1);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0x7F), 1);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0xC3), 2);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0xE4), 3);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0xF0), 4);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0x80), 0);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0xBF), 0);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0xF8), 0);
  ASSERT_SIZE_EQ(utf8_codepoint_length(0xFF), 0);
  TEST_PASS();
}

int test_validate(void) {
  /* Valid ASCII */
  ASSERT(utf8_validate((const uint8_t*)"hello", 5));
  /* Valid multi-byte */
  const uint8_t cafe[] = {0xC3, 0xA9};  /* é */
  ASSERT(utf8_validate(cafe, 2));
  /* Empty is valid */
  ASSERT(utf8_validate((const uint8_t*)"", 0));
  /* Orphan continuation */
  const uint8_t bad[] = {0x80};
  ASSERT(!utf8_validate(bad, 1));
  /* Truncated */
  const uint8_t trunc[] = {0xC3};
  ASSERT(!utf8_validate(trunc, 1));
  TEST_PASS();
}

int test_codepoint_count(void) {
  /* ASCII */
  ASSERT_SIZE_EQ(utf8_codepoint_count((const uint8_t*)"hello", 5), 5);
  /* Mixed: "café" = c a f é = 4 codepoints, 5 bytes */
  const uint8_t cafe[] = {'c', 'a', 'f', 0xC3, 0xA9};
  ASSERT_SIZE_EQ(utf8_codepoint_count(cafe, 5), 4);
  /* Empty */
  ASSERT_SIZE_EQ(utf8_codepoint_count((const uint8_t*)"", 0), 0);
  TEST_PASS();
}

int test_line_count(void) {
  ASSERT_SIZE_EQ(utf8_line_count((const uint8_t*)"hello\nworld\n", 12), 2);
  ASSERT_SIZE_EQ(utf8_line_count((const uint8_t*)"hello", 5), 0);
  ASSERT_SIZE_EQ(utf8_line_count((const uint8_t*)"\n", 1), 1);
  TEST_PASS();
}

int test_find_safe_split(void) {
  /* "é" = C3 A9 — splitting at byte 1 should back up to 0 */
  const uint8_t buf[] = {0xC3, 0xA9, 0x41};
  ASSERT_SIZE_EQ(utf8_find_safe_split(buf, 3, 1), 0);
  /* Already on boundary */
  ASSERT_SIZE_EQ(utf8_find_safe_split(buf, 3, 2), 2);
  /* pos == 0 */
  ASSERT_SIZE_EQ(utf8_find_safe_split(buf, 3, 0), 0);
  /* pos >= len */
  ASSERT_SIZE_EQ(utf8_find_safe_split(buf, 3, 5), 5);
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

  printf("=== unicode/test_unicode_utf8 ===\n\n");

  RUN(test_encode_ascii);
  RUN(test_encode_2byte);
  RUN(test_encode_3byte);
  RUN(test_encode_4byte);
  RUN(test_encode_invalid);
  RUN(test_encode_decode_roundtrip);
  RUN(test_encode_decode_roundtrip_samples);
  RUN(test_codepoint_length);
  RUN(test_validate);
  RUN(test_codepoint_count);
  RUN(test_line_count);
  RUN(test_find_safe_split);

  printf("\n%d/%d tests passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
