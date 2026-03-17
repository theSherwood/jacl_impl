#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-005: UTF-8 validation and NFD normalization pipeline ===== */

/* --- UTF-8 validation tests --- */

/* Test: valid ASCII passes validation */
static int test_validate_ascii(void) {
  ASSERT(jacl_utf8_validate("hello world", 11));
  ASSERT(jacl_utf8_validate("", 0));
  ASSERT(jacl_utf8_validate("0123456789", 10));
  ASSERT(jacl_utf8_validate("~!@#$%^&*()", 11));
  TEST_PASS();
}

/* Test: valid multi-byte UTF-8 passes */
static int test_validate_multibyte(void) {
  /* U+00E9 (e-acute) = 0xC3 0xA9 (2-byte) */
  ASSERT(jacl_utf8_validate("\xC3\xA9", 2));
  /* U+4E16 (CJK) = 0xE4 0xB8 0x96 (3-byte) */
  ASSERT(jacl_utf8_validate("\xE4\xB8\x96", 3));
  /* U+1F600 (grinning face) = 0xF0 0x9F 0x98 0x80 (4-byte) */
  ASSERT(jacl_utf8_validate("\xF0\x9F\x98\x80", 4));
  /* Mixed ASCII + multibyte */
  ASSERT(jacl_utf8_validate("caf\xC3\xA9", 5));
  TEST_PASS();
}

/* Test: overlong encodings rejected */
static int test_validate_reject_overlong(void) {
  /* Overlong 2-byte encoding of '/' (U+002F): 0xC0 0xAF */
  ASSERT(!jacl_utf8_validate("\xC0\xAF", 2));
  /* Overlong 3-byte encoding of '/' (U+002F): 0xE0 0x80 0xAF */
  ASSERT(!jacl_utf8_validate("\xE0\x80\xAF", 3));
  /* Overlong 2-byte encoding of NUL (U+0000): 0xC0 0x80 */
  ASSERT(!jacl_utf8_validate("\xC0\x80", 2));
  TEST_PASS();
}

/* Test: surrogate halves rejected (U+D800–U+DFFF) */
static int test_validate_reject_surrogates(void) {
  /* U+D800 (high surrogate) = 0xED 0xA0 0x80 */
  ASSERT(!jacl_utf8_validate("\xED\xA0\x80", 3));
  /* U+DBFF (high surrogate end) = 0xED 0xAF 0xBF */
  ASSERT(!jacl_utf8_validate("\xED\xAF\xBF", 3));
  /* U+DC00 (low surrogate) = 0xED 0xB0 0x80 */
  ASSERT(!jacl_utf8_validate("\xED\xB0\x80", 3));
  /* U+DFFF (low surrogate end) = 0xED 0xBF 0xBF */
  ASSERT(!jacl_utf8_validate("\xED\xBF\xBF", 3));
  /* Valid text before surrogate */
  ASSERT(!jacl_utf8_validate("abc\xED\xA0\x80", 6));
  TEST_PASS();
}

/* Test: truncated sequences rejected */
static int test_validate_reject_truncated(void) {
  /* 2-byte lead with no continuation */
  ASSERT(!jacl_utf8_validate("\xC3", 1));
  /* 3-byte lead with 1 continuation */
  ASSERT(!jacl_utf8_validate("\xE4\xB8", 2));
  /* 4-byte lead with 2 continuations */
  ASSERT(!jacl_utf8_validate("\xF0\x9F\x98", 3));
  /* Truncated in the middle of valid text */
  ASSERT(!jacl_utf8_validate("abc\xC3", 4));
  TEST_PASS();
}

/* Test: invalid continuation bytes rejected */
static int test_validate_reject_invalid_continuation(void) {
  /* Bare continuation byte */
  ASSERT(!jacl_utf8_validate("\x80", 1));
  ASSERT(!jacl_utf8_validate("\xBF", 1));
  /* 2-byte lead followed by non-continuation */
  ASSERT(!jacl_utf8_validate("\xC3\x28", 2));
  /* Invalid lead byte 0xFE */
  ASSERT(!jacl_utf8_validate("\xFE", 1));
  /* Invalid lead byte 0xFF */
  ASSERT(!jacl_utf8_validate("\xFF", 1));
  TEST_PASS();
}

/* Test: valid text with combining marks passes */
static int test_validate_combining_marks(void) {
  /* 'a' + U+0301 (combining acute accent) = 0x61 0xCC 0x81 */
  ASSERT(jacl_utf8_validate("a\xCC\x81", 3));
  /* 'e' + U+0301 + U+0327 (combining cedilla) */
  ASSERT(jacl_utf8_validate("e\xCC\x81\xCC\xA7", 5));
  TEST_PASS();
}

/* --- NFD normalization tests --- */

/* Test: ASCII is already NFD — fast path, no copy */
static int test_nfd_ascii_passthrough(void) {
  const char* input = "hello world";
  uint8_t stack_buf[256];
  uint8_t* out = NULL;
  size_t out_len = 0;

  ASSERT(jacl_nfd_normalize(input, 11, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  /* Fast path: output pointer should be the input pointer (no copy) */
  ASSERT(out == (uint8_t*)input);
  ASSERT_SIZE_EQ(out_len, 11);
  TEST_PASS();
}

/* Test: NFC e-acute (U+00E9, 2 bytes) decomposes to e + U+0301 (3 bytes NFD) */
static int test_nfd_nfc_decompose(void) {
  /* U+00E9 = 0xC3 0xA9 */
  const char* input = "\xC3\xA9";
  uint8_t stack_buf[256];
  uint8_t* out = NULL;
  size_t out_len = 0;

  ASSERT(jacl_nfd_normalize(input, 2, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  /* NFD: 'e' (0x65) + U+0301 (0xCC 0x81) = 3 bytes */
  ASSERT_SIZE_EQ(out_len, 3);
  ASSERT(out[0] == 0x65);     /* 'e' */
  ASSERT(out[1] == 0xCC);     /* U+0301 high byte */
  ASSERT(out[2] == 0x81);     /* U+0301 low byte */
  /* Output should be in stack buffer (not input pointer, since normalization happened) */
  ASSERT(out != (uint8_t*)input);
  TEST_PASS();
}

/* Test: already NFD text (e + combining acute) passes through */
static int test_nfd_already_nfd(void) {
  /* Already NFD: 'e' + U+0301 */
  const char* input = "e\xCC\x81";
  uint8_t stack_buf[256];
  uint8_t* out = NULL;
  size_t out_len = 0;

  ASSERT(jacl_nfd_normalize(input, 3, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  /* Should return input pointer (already NFD, no copy) */
  ASSERT(out == (uint8_t*)input);
  ASSERT_SIZE_EQ(out_len, 3);
  TEST_PASS();
}

/* Test: unicode_is_nfd fast path works for ASCII */
static int test_nfd_is_nfd_fast_path(void) {
  /* Pure ASCII should be detected as NFD instantly */
  const uint8_t ascii[] = "The quick brown fox jumps over the lazy dog";
  ASSERT(unicode_is_nfd(ascii, sizeof(ascii) - 1));

  /* NFC input should NOT be detected as NFD */
  const uint8_t nfc[] = { 0xC3, 0xA9 }; /* U+00E9 */
  ASSERT(!unicode_is_nfd(nfc, 2));

  TEST_PASS();
}

/* Test: Hangul syllable decomposition */
static int test_nfd_hangul_decompose(void) {
  /* U+AC00 (가, Hangul syllable GA) = 0xEA 0xB0 0x80
   * NFD: U+1100 (ᄀ) + U+1161 (ᅡ)
   * U+1100 = 0xE1 0x84 0x80, U+1161 = 0xE1 0x85 0xA1
   * = 6 bytes NFD */
  const char* input = "\xEA\xB0\x80";
  uint8_t stack_buf[256];
  uint8_t* out = NULL;
  size_t out_len = 0;

  ASSERT(jacl_nfd_normalize(input, 3, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  /* NFD should be longer than NFC Hangul */
  ASSERT_SIZE_EQ(out_len, 6);
  /* Verify U+1100 */
  ASSERT(out[0] == 0xE1 && out[1] == 0x84 && out[2] == 0x80);
  /* Verify U+1161 */
  ASSERT(out[3] == 0xE1 && out[4] == 0x85 && out[5] == 0xA1);
  TEST_PASS();
}

/* Test: combining sequences are canonically ordered after normalization */
static int test_nfd_canonical_ordering(void) {
  /* U+0327 (combining cedilla, CCC=202) + U+0301 (combining acute, CCC=230)
   * Applied to 'c': c + cedilla + acute
   * In NFD these should stay in same order (202 < 230 is already correct) */
  const char* input = "c\xCC\xA7\xCC\x81";
  uint8_t stack_buf[256];
  uint8_t* out = NULL;
  size_t out_len = 0;

  ASSERT(jacl_nfd_normalize(input, 5, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  /* Already NFD - should pass through */
  ASSERT(out == (uint8_t*)input);
  ASSERT_SIZE_EQ(out_len, 5);

  /* Now test wrong order: U+0301 (CCC=230) + U+0327 (CCC=202)
   * NFD canonical ordering should reorder to U+0327 + U+0301 */
  const char* wrong_order = "c\xCC\x81\xCC\xA7";
  out = NULL; out_len = 0;
  ASSERT(jacl_nfd_normalize(wrong_order, 5, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  ASSERT_SIZE_EQ(out_len, 5);
  /* After canonical ordering: 'c' + U+0327 + U+0301 */
  ASSERT(out[0] == 'c');
  ASSERT(out[1] == 0xCC && out[2] == 0xA7);  /* U+0327 */
  ASSERT(out[3] == 0xCC && out[4] == 0x81);  /* U+0301 */
  TEST_PASS();
}

/* Test: 128-byte tier threshold applies to post-NFD byte length.
 * An NFC string that's ≤128 bytes can expand to >128 bytes in NFD. */
static int test_nfd_tier_threshold(void) {
  /* Build an NFC string of ~120 bytes that expands under NFD.
   * U+00E9 is 2 bytes NFC, 3 bytes NFD.
   * 60 copies = 120 bytes NFC → 180 bytes NFD (>128, would be rope tier). */
  char nfc_buf[120];
  for (int i = 0; i < 60; i++) {
    nfc_buf[i * 2]     = (char)0xC3;
    nfc_buf[i * 2 + 1] = (char)0xA9;
  }

  uint8_t stack_buf[256];
  uint8_t* out = NULL;
  size_t out_len = 0;

  ASSERT(jacl_nfd_normalize(nfc_buf, 120, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  /* Post-NFD length should be 180 bytes (>128 byte threshold) */
  ASSERT_SIZE_EQ(out_len, 180);
  /* This demonstrates that tier routing must use post-NFD length */
  TEST_PASS();
}

/* Test: large NFD output uses malloc when exceeding stack buffer */
static int test_nfd_large_malloc_fallback(void) {
  /* Build NFC string: 200 copies of U+00E9 = 400 bytes NFC → 600 bytes NFD.
   * Use a small stack buffer to force malloc path. */
  char nfc_buf[400];
  for (int i = 0; i < 200; i++) {
    nfc_buf[i * 2]     = (char)0xC3;
    nfc_buf[i * 2 + 1] = (char)0xA9;
  }

  uint8_t small_stack[32];  /* Too small for 600 bytes output */
  uint8_t* out = NULL;
  size_t out_len = 0;

  ASSERT(jacl_nfd_normalize(nfc_buf, 400, small_stack, sizeof(small_stack),
                             &out, &out_len));
  ASSERT_SIZE_EQ(out_len, 600);
  /* Output should not be input (normalization happened) */
  ASSERT(out != (uint8_t*)nfc_buf);
  /* Output should not be stack buffer (too small) */
  ASSERT(out != small_stack);

  /* Verify content: each triplet should be 'e' + U+0301 */
  for (int i = 0; i < 200; i++) {
    ASSERT(out[i * 3]     == 0x65);
    ASSERT(out[i * 3 + 1] == 0xCC);
    ASSERT(out[i * 3 + 2] == 0x81);
  }

  /* Must free the malloc'd buffer */
  free(out);
  TEST_PASS();
}

/* Test: empty string validates and normalizes correctly */
static int test_empty_string(void) {
  ASSERT(jacl_utf8_validate("", 0));

  uint8_t stack_buf[256];
  uint8_t* out = NULL;
  size_t out_len = 0;
  ASSERT(jacl_nfd_normalize("", 0, stack_buf, sizeof(stack_buf),
                             &out, &out_len));
  ASSERT_SIZE_EQ(out_len, 0);
  TEST_PASS();
}

/* Test: mixed valid/invalid UTF-8 — validation stops at first error */
static int test_validate_mixed(void) {
  /* Valid prefix + invalid byte in middle + valid suffix */
  ASSERT(!jacl_utf8_validate("abc\xFF" "def", 7));
  /* Valid multibyte then bare continuation */
  ASSERT(!jacl_utf8_validate("\xC3\xA9\x80", 3));
  TEST_PASS();
}

/* --- Test runner --- */

typedef int (*test_fn)(void);

int main(void) {
  test_fn tests[] = {
    test_validate_ascii,
    test_validate_multibyte,
    test_validate_reject_overlong,
    test_validate_reject_surrogates,
    test_validate_reject_truncated,
    test_validate_reject_invalid_continuation,
    test_validate_combining_marks,
    test_nfd_ascii_passthrough,
    test_nfd_nfc_decompose,
    test_nfd_already_nfd,
    test_nfd_is_nfd_fast_path,
    test_nfd_hangul_decompose,
    test_nfd_canonical_ordering,
    test_nfd_tier_threshold,
    test_nfd_large_malloc_fallback,
    test_empty_string,
    test_validate_mixed,
  };

  const char* names[] = {
    "test_validate_ascii",
    "test_validate_multibyte",
    "test_validate_reject_overlong",
    "test_validate_reject_surrogates",
    "test_validate_reject_truncated",
    "test_validate_reject_invalid_continuation",
    "test_validate_combining_marks",
    "test_nfd_ascii_passthrough",
    "test_nfd_nfc_decompose",
    "test_nfd_already_nfd",
    "test_nfd_is_nfd_fast_path",
    "test_nfd_hangul_decompose",
    "test_nfd_canonical_ordering",
    "test_nfd_tier_threshold",
    "test_nfd_large_malloc_fallback",
    "test_empty_string",
    "test_validate_mixed",
  };

  int n = (int)(sizeof(tests) / sizeof(tests[0]));
  int pass = 0, fail = 0;

  printf("=== US-005: UTF-8 validation and NFD normalization ===\n");
  for (int i = 0; i < n; i++) {
    printf("  %-50s ", names[i]);
    if (tests[i]()) {
      printf("PASS\n");
      pass++;
    } else {
      printf("FAIL\n");
      fail++;
    }
  }

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
