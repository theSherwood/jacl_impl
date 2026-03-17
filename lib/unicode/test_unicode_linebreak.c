/*
 * test_unicode_linebreak.c — Tests for UAX #14 line break detection
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

/* Helper: collect all line break opportunities */
static size_t collect_breaks(const uint8_t* src, size_t len,
                              size_t* out, size_t cap) {
  size_t n = 0;
  size_t pos = 0;
  while (pos < len && n < cap) {
    pos = unicode_linebreak_next(src, len, pos);
    out[n++] = pos;
  }
  return n;
}

/* === Break after spaces in English === */
int test_lb_spaces(void) {
  const uint8_t* s = (const uint8_t*)"Hello world foo";
  /* Break opportunities: after "Hello " (6), after "world " (12), at end (15) */
  size_t breaks[16];
  size_t nb = collect_breaks(s, 15, breaks, 16);
  ASSERT(nb >= 2);
  /* There should be break opportunities after the spaces */
  bool found_6 = false, found_12 = false;
  for (size_t i = 0; i < nb; i++) {
    if (breaks[i] == 6) found_6 = true;
    if (breaks[i] == 12) found_12 = true;
  }
  ASSERT(found_6);
  ASSERT(found_12);
  TEST_PASS();
}

/* === Mandatory breaks at newlines === */
int test_lb_newlines(void) {
  const uint8_t* s = (const uint8_t*)"abc\ndef\n";
  /* Mandatory breaks after \n (pos 4 and 8) */
  size_t brk = unicode_linebreak_next(s, 8, 0);
  /* First break should be at 4 (after "abc\n") */
  ASSERT_SIZE_EQ(brk, 4);
  ASSERT(unicode_linebreak_is_mandatory(s, 8, 4));

  brk = unicode_linebreak_next(s, 8, 4);
  ASSERT_SIZE_EQ(brk, 8);
  ASSERT(unicode_linebreak_is_mandatory(s, 8, 8));
  TEST_PASS();
}

/* === No break before closing punctuation (LB13) === */
int test_lb_no_break_before_close(void) {
  /* "a)" — no break between 'a' and ')' */
  const uint8_t* s = (const uint8_t*)"a)b";
  size_t brk = unicode_linebreak_next(s, 3, 0);
  /* Should NOT break at position 1 (between 'a' and ')') */
  ASSERT(brk > 1);
  TEST_PASS();
}

/* === No break after opening punctuation (LB14) === */
int test_lb_no_break_after_open(void) {
  /* "(a" — no break between '(' and 'a' */
  const uint8_t* s = (const uint8_t*)"(ab";
  size_t brk = unicode_linebreak_next(s, 3, 0);
  /* Should NOT break at position 1 (between '(' and 'a') */
  ASSERT(brk > 1);
  TEST_PASS();
}

/* === CJK ideograph breaks (LB31: any ÷ any) === */
int test_lb_cjk(void) {
  /* CJK ideographs generally allow breaks between them.
     U+4E16 (世) U+754C (界) — break allowed between them */
  uint32_t cps[] = { 0x4E16, 0x754C, 0x4EBA };
  uint8_t buf[16];
  size_t len = encode_cps(cps, 3, buf);

  /* Each ideograph is 3 bytes. Breaks after each. */
  size_t brk = unicode_linebreak_next(buf, len, 0);
  ASSERT_SIZE_EQ(brk, 3); /* break after first ideograph */

  brk = unicode_linebreak_next(buf, len, 3);
  ASSERT_SIZE_EQ(brk, 6); /* break after second */
  TEST_PASS();
}

/* === No break within numeric sequences (LB25) === */
int test_lb_numeric(void) {
  /* "$1,234.56" — no breaks within the numeric sequence.
     '$' is PR, digits are NU, ',' is IS, '.' is IS */
  const uint8_t* s = (const uint8_t*)"$12.34";
  size_t brk = unicode_linebreak_next(s, 6, 0);
  /* The whole sequence should hold together */
  ASSERT_SIZE_EQ(brk, 6);
  TEST_PASS();
}

/* === _prev backward === */
int test_lb_prev(void) {
  const uint8_t* s = (const uint8_t*)"Hello world";
  /* Line break prev from end (11) should find the break after "Hello " */
  size_t prev = unicode_linebreak_prev(s, 11, 11);
  ASSERT_SIZE_EQ(prev, 6);

  /* Prev from 6 should go to 0 (start of string) */
  prev = unicode_linebreak_prev(s, 11, 6);
  ASSERT_SIZE_EQ(prev, 0);
  TEST_PASS();
}

/* === CRLF handling === */
int test_lb_crlf(void) {
  const uint8_t* s = (const uint8_t*)"a\r\nb";
  /* CR LF is a single mandatory break — break at position 3 (after \r\n) */
  size_t brk = unicode_linebreak_next(s, 4, 0);
  ASSERT_SIZE_EQ(brk, 3);
  ASSERT(unicode_linebreak_is_mandatory(s, 4, 3));

  /* Should NOT be a mandatory break between CR and LF (position 2) */
  ASSERT(!unicode_linebreak_is_mandatory(s, 4, 2));
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

  printf("=== unicode/test_unicode_linebreak ===\n\n");

  RUN(test_lb_spaces);
  RUN(test_lb_newlines);
  RUN(test_lb_no_break_before_close);
  RUN(test_lb_no_break_after_open);
  RUN(test_lb_cjk);
  RUN(test_lb_numeric);
  RUN(test_lb_prev);
  RUN(test_lb_crlf);

  printf("\n%d/%d tests passed\n", pass, pass + fail);
  return fail > 0 ? 1 : 0;
}
