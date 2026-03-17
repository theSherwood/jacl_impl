/*
 * test_unicode_nfd.c — Tests for NFD normalization in unicode/unicode.h
 */

#include <stdio.h>
#include <string.h>

#include "../test/test_helpers.h"
#include "unicode.h"

/* Helper: encode codepoints to UTF-8 and return byte length */
static size_t encode_cps(const uint32_t* cps, size_t count, uint8_t* buf, size_t cap) {
  size_t pos = 0;
  for (size_t i = 0; i < count; i++) {
    size_t n = utf8_encode(cps[i], buf + pos);
    pos += n;
    (void)cap;
  }
  return pos;
}

/* Helper: check NFD of input codepoints matches expected codepoints */
static int check_nfd(const uint32_t* input, size_t in_count,
                     const uint32_t* expected, size_t exp_count,
                     const char* label) {
  uint8_t src[256], exp_buf[256], dst[256];
  size_t src_len = encode_cps(input, in_count, src, sizeof(src));
  size_t exp_len = encode_cps(expected, exp_count, exp_buf, sizeof(exp_buf));

  size_t out_len = 0;
  int rc = unicode_nfd(src, src_len, dst, sizeof(dst), &out_len);
  if (rc != 0) {
    printf("FAIL [%s]: unicode_nfd returned %d\n", label, rc);
    return 0;
  }
  if (out_len != exp_len) {
    printf("FAIL [%s]: expected %zu bytes, got %zu\n", label, exp_len, out_len);
    return 0;
  }
  if (memcmp(dst, exp_buf, exp_len) != 0) {
    printf("FAIL [%s]: output bytes differ\n", label);
    return 0;
  }
  return 1;
}

/* === ASCII passthrough === */
int test_nfd_ascii(void) {
  const uint8_t src[] = "Hello, world!";
  uint8_t dst[64];
  size_t out_len = 0;
  int rc = unicode_nfd(src, 13, dst, sizeof(dst), &out_len);
  ASSERT_INT_EQ(rc, 0);
  ASSERT_SIZE_EQ(out_len, 13);
  ASSERT(memcmp(dst, src, 13) == 0);
  TEST_PASS();
}

/* === Latin accented characters === */
int test_nfd_latin_accented(void) {
  /* U+00C9 (É) -> U+0045 U+0301 (E + combining acute) */
  uint32_t input[] = { 0x00C9 };
  uint32_t expected[] = { 0x0045, 0x0301 };
  ASSERT(check_nfd(input, 1, expected, 2, "É → E+acute"));

  /* U+00E9 (é) -> U+0065 U+0301 (e + combining acute) */
  uint32_t input2[] = { 0x00E9 };
  uint32_t expected2[] = { 0x0065, 0x0301 };
  ASSERT(check_nfd(input2, 1, expected2, 2, "é → e+acute"));

  /* U+00F1 (ñ) -> U+006E U+0303 (n + combining tilde) */
  uint32_t input3[] = { 0x00F1 };
  uint32_t expected3[] = { 0x006E, 0x0303 };
  ASSERT(check_nfd(input3, 1, expected3, 2, "ñ → n+tilde"));

  TEST_PASS();
}

/* === Multiple combining marks in canonical order === */
int test_nfd_combining_order(void) {
  /* U+0065 U+0301 U+0327  (e + acute + cedilla)
     CCC of U+0301 = 230, CCC of U+0327 = 202
     Canonical order: U+0327 (202) then U+0301 (230)
     -> U+0065 U+0327 U+0301 */
  uint32_t input[] = { 0x0065, 0x0301, 0x0327 };
  uint32_t expected[] = { 0x0065, 0x0327, 0x0301 };
  ASSERT(check_nfd(input, 3, expected, 3, "e+acute+cedilla reorder"));

  /* Already correct order: no change */
  uint32_t input2[] = { 0x0065, 0x0327, 0x0301 };
  uint32_t expected2[] = { 0x0065, 0x0327, 0x0301 };
  ASSERT(check_nfd(input2, 3, expected2, 3, "already ordered"));

  TEST_PASS();
}

/* === Hangul decomposition === */
int test_nfd_hangul(void) {
  /* U+AC00 (가) = LV syllable -> U+1100 U+1161 */
  uint32_t input[] = { 0xAC00 };
  uint32_t expected[] = { 0x1100, 0x1161 };
  ASSERT(check_nfd(input, 1, expected, 2, "가 → 가"));

  /* U+AC01 (각) = LVT syllable -> U+1100 U+1161 U+11A8 */
  uint32_t input2[] = { 0xAC01 };
  uint32_t expected2[] = { 0x1100, 0x1161, 0x11A8 };
  ASSERT(check_nfd(input2, 1, expected2, 3, "각 → 각"));

  /* U+D7A3 (last Hangul syllable) */
  uint32_t input3[] = { 0xD7A3 };
  uint32_t expected3[] = { 0x1112, 0x1175, 0x11C2 };
  ASSERT(check_nfd(input3, 1, expected3, 3, "last hangul syllable"));

  TEST_PASS();
}

/* === Recursive decomposition === */
int test_nfd_recursive(void) {
  /* U+01D5 (Ǖ) -> U+00DC U+0304 -> U+0055 U+0308 U+0304
     (U with diaeresis and macron) */
  uint32_t input[] = { 0x01D5 };
  uint32_t expected[] = { 0x0055, 0x0308, 0x0304 };
  ASSERT(check_nfd(input, 1, expected, 3, "Ǖ recursive decomp"));

  /* U+1E69 (ṩ) -> U+0073 U+0323 U+0307
     (s with dot below and dot above) */
  uint32_t input2[] = { 0x1E69 };
  uint32_t expected2[] = { 0x0073, 0x0323, 0x0307 };
  ASSERT(check_nfd(input2, 1, expected2, 3, "ṩ recursive decomp"));

  TEST_PASS();
}

/* === Quick check === */
int test_nfd_quick_check(void) {
  /* Pure ASCII is NFD */
  ASSERT(unicode_is_nfd((const uint8_t*)"hello", 5));

  /* Already-decomposed text is NFD: e + combining acute */
  uint8_t decomposed[8];
  size_t dlen = 0;
  dlen += utf8_encode(0x0065, decomposed + dlen);
  dlen += utf8_encode(0x0301, decomposed + dlen);
  ASSERT(unicode_is_nfd(decomposed, dlen));

  /* Precomposed é is NOT NFD */
  uint8_t precomposed[4];
  size_t plen = utf8_encode(0x00E9, precomposed);
  ASSERT(!unicode_is_nfd(precomposed, plen));

  /* Hangul syllable is NOT NFD */
  uint8_t hangul[4];
  size_t hlen = utf8_encode(0xAC00, hangul);
  ASSERT(!unicode_is_nfd(hangul, hlen));

  /* Empty is NFD */
  ASSERT(unicode_is_nfd((const uint8_t*)"", 0));

  /* Wrong CCC order is not NFD */
  uint8_t bad_order[16];
  size_t blen = 0;
  blen += utf8_encode(0x0065, bad_order + blen);  /* e */
  blen += utf8_encode(0x0301, bad_order + blen);  /* acute (CCC 230) */
  blen += utf8_encode(0x0327, bad_order + blen);  /* cedilla (CCC 202) */
  ASSERT(!unicode_is_nfd(bad_order, blen));

  TEST_PASS();
}

/* === Buffer too small === */
int test_nfd_buffer_too_small(void) {
  /* É needs at least 3 bytes in NFD (E=1 + combining acute=2) */
  uint8_t src[4];
  size_t slen = utf8_encode(0x00C9, src);  /* É */
  uint8_t small[2];
  size_t out_len = 0;
  int rc = unicode_nfd(src, slen, small, sizeof(small), &out_len);
  ASSERT_INT_EQ(rc, -1);
  /* out_len should be the required size (3 bytes: E + 2-byte combining acute) */
  ASSERT_SIZE_EQ(out_len, 3);

  /* Now with enough space */
  uint8_t ok[8];
  rc = unicode_nfd(src, slen, ok, sizeof(ok), &out_len);
  ASSERT_INT_EQ(rc, 0);
  ASSERT_SIZE_EQ(out_len, 3);
  TEST_PASS();
}

/* === unicode_nfd_length === */
int test_nfd_length(void) {
  /* ASCII: same length */
  ASSERT_SIZE_EQ(unicode_nfd_length((const uint8_t*)"hello", 5), 5);

  /* É -> E + combining acute = 1 + 2 = 3 bytes */
  uint8_t src[4];
  size_t slen = utf8_encode(0x00C9, src);
  ASSERT_SIZE_EQ(unicode_nfd_length(src, slen), 3);

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

  printf("=== unicode/test_unicode_nfd ===\n\n");

  RUN(test_nfd_ascii);
  RUN(test_nfd_latin_accented);
  RUN(test_nfd_combining_order);
  RUN(test_nfd_hangul);
  RUN(test_nfd_recursive);
  RUN(test_nfd_quick_check);
  RUN(test_nfd_buffer_too_small);
  RUN(test_nfd_length);

  printf("\n%d/%d tests passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
