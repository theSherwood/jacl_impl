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

/* === Tests === */

int test_empty_rope(void) {
  printf("Running test_empty_rope... ");
  tracker_reset();

  rope r = rope_new();
  ASSERT_INT_EQ(rope_byte_count(r), 0);
  ASSERT_INT_EQ(rope_char_count(r), 0);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 0);
  rope_unref(r);
  TEST_PASS();
}

int test_rope_ref_unref(void) {
  printf("Running test_rope_ref_unref... ");
  tracker_reset();

  rope r = rope_new();
  /* ref/unref on empty rope should be safe */
  rope r2 = rope_ref(r);
  rope_unref(r2);
  rope_unref(r);
  TEST_PASS();
}

int test_rope_compiles_alongside_sum_tree(void) {
  printf("Running test_rope_compiles_alongside_sum_tree... ");
  tracker_reset();

  /* Verify rope types exist and are usable */
  rope r = rope_new();
  rope_st_root root = r.root;
  ASSERT_INT_EQ(rope_st_count(root), 0);
  rope_unref(r);
  TEST_PASS();
}

/* === US-006 Tests: from_str / to_str === */

int test_from_str_ascii_roundtrip(void) {
  printf("Running test_from_str_ascii_roundtrip... ");
  tracker_reset();

  const uint8_t* str = (const uint8_t*)"hello world";
  size_t len = 11;
  rope r = rope_from_str(str, len);

  ASSERT_INT_EQ(rope_byte_count(r), 11);
  ASSERT_INT_EQ(rope_char_count(r), 11);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 11);

  uint8_t buf[64];
  size_t written = rope_to_str(r, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 11);
  ASSERT(memcmp(buf, str, 11) == 0);

  rope_unref(r);
  TEST_PASS();
}

int test_from_str_multibyte_roundtrip(void) {
  printf("Running test_from_str_multibyte_roundtrip... ");
  tracker_reset();

  /* "Hé世🌍" — H(1) + é(2) + 世(3) + 🌍(4) = 10 bytes, 4 chars */
  const uint8_t str[] = {
    'H',
    0xC3, 0xA9,             /* é U+00E9 */
    0xE4, 0xB8, 0x96,       /* 世 U+4E16 */
    0xF0, 0x9F, 0x8C, 0x8D  /* 🌍 U+1F30D */
  };
  size_t len = sizeof(str);
  rope r = rope_from_str(str, len);

  ASSERT_INT_EQ(rope_byte_count(r), 10);
  ASSERT_INT_EQ(rope_char_count(r), 4);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 4);

  uint8_t buf[64];
  size_t written = rope_to_str(r, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 10);
  ASSERT(memcmp(buf, str, 10) == 0);

  rope_unref(r);
  TEST_PASS();
}

int test_from_str_summary_dimensions(void) {
  printf("Running test_from_str_summary_dimensions... ");
  tracker_reset();

  /* "ab\ncd\né" — 8 bytes, 7 chars, 2 lines, 7 graphemes */
  const uint8_t str[] = {'a', 'b', '\n', 'c', 'd', '\n', 0xC3, 0xA9};
  size_t len = sizeof(str);
  rope r = rope_from_str(str, len);

  ASSERT_INT_EQ(rope_byte_count(r), 8);
  ASSERT_INT_EQ(rope_char_count(r), 7);
  ASSERT_INT_EQ(rope_line_count(r), 2);
  ASSERT_INT_EQ(rope_grapheme_count(r), 7);

  rope_unref(r);
  TEST_PASS();
}

int test_from_str_large_roundtrip(void) {
  printf("Running test_from_str_large_roundtrip... ");
  tracker_reset();

  /* Build a 10080-byte string with mixed ASCII and multi-byte chars */
  /* Pattern: 28 ASCII + 1 é (2 bytes) = 30 bytes per unit, repeat 336 times = 10080 bytes */
  size_t total_len = 10080;
  uint8_t* str = (uint8_t*)malloc(total_len);
  size_t pos = 0;
  for (size_t i = 0; i < 336; i++) {
    for (size_t j = 0; j < 28; j++) {
      str[pos++] = 'a' + (uint8_t)(j % 26);
    }
    str[pos++] = 0xC3;
    str[pos++] = 0xA9; /* é */
  }
  ASSERT_INT_EQ(pos, total_len);

  rope r = rope_from_str(str, total_len);
  ASSERT_INT_EQ(rope_byte_count(r), total_len);
  /* 28 ASCII + 1 multi-byte char = 29 chars per unit * 336 = 9744 chars */
  ASSERT_INT_EQ(rope_char_count(r), 29 * 336);

  uint8_t* buf = (uint8_t*)malloc(total_len);
  size_t written = rope_to_str(r, buf, total_len);
  ASSERT_INT_EQ(written, total_len);
  ASSERT(memcmp(buf, str, total_len) == 0);

  free(buf);
  free(str);
  rope_unref(r);
  TEST_PASS();
}

int test_from_str_empty(void) {
  printf("Running test_from_str_empty... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"", 0);
  ASSERT_INT_EQ(rope_byte_count(r), 0);
  ASSERT_INT_EQ(rope_char_count(r), 0);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 0);

  rope_unref(r);
  TEST_PASS();
}

int test_from_str_invalid_utf8(void) {
  printf("Running test_from_str_invalid_utf8... ");
  tracker_reset();

  /* Orphan continuation byte */
  const uint8_t bad[] = {0x80, 0x41};
  rope r = rope_from_str(bad, sizeof(bad));
  ASSERT_INT_EQ(rope_byte_count(r), 0);

  rope_unref(r);
  TEST_PASS();
}

/* === ASCII fast-path in rope_summary_summarize ===
 *
 * The summarize function takes a single-pass ASCII fast-path that fills
 * every summary field from byte/newline/CR-LF counts alone, bypassing
 * unicode_grapheme_count. These tests cover the boundaries of that path:
 * word-loop only, tail only, CR-LF detection within and across word
 * boundaries, and falling out of the fast-path on a high-bit byte at
 * each escape site. Outputs are checked against the same getters
 * (rope_byte_count etc.) the non-fast-path tests use, so a regression in
 * either branch is visible. */

int test_ascii_fastpath_word_loop_only(void) {
  printf("Running test_ascii_fastpath_word_loop_only... ");
  tracker_reset();

  /* 24 bytes, exact multiple of 8, no newlines — exercises only the
   * word loop, no tail. */
  const uint8_t* str = (const uint8_t*)"abcdefghijklmnopqrstuvwx";
  rope r = rope_from_str(str, 24);

  ASSERT_INT_EQ(rope_byte_count(r), 24);
  ASSERT_INT_EQ(rope_char_count(r), 24);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 24);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_short_tail_only(void) {
  printf("Running test_ascii_fastpath_short_tail_only... ");
  tracker_reset();

  /* 5 bytes — skips word loop entirely, runs only the tail loop. */
  const uint8_t* str = (const uint8_t*)"hi!\nx";
  rope r = rope_from_str(str, 5);

  ASSERT_INT_EQ(rope_byte_count(r), 5);
  ASSERT_INT_EQ(rope_char_count(r), 5);
  ASSERT_INT_EQ(rope_line_count(r), 1);
  ASSERT_INT_EQ(rope_grapheme_count(r), 5);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_lf_counts(void) {
  printf("Running test_ascii_fastpath_lf_counts... ");
  tracker_reset();

  /* Three '\n' bytes spread across word and tail regions. */
  const uint8_t* str = (const uint8_t*)"a\nbcdefgh\nij\n";  /* 13 bytes */
  rope r = rope_from_str(str, 13);

  ASSERT_INT_EQ(rope_byte_count(r), 13);
  ASSERT_INT_EQ(rope_char_count(r), 13);
  ASSERT_INT_EQ(rope_line_count(r), 3);
  ASSERT_INT_EQ(rope_grapheme_count(r), 13);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_crlf_within_word(void) {
  printf("Running test_ascii_fastpath_crlf_within_word... ");
  tracker_reset();

  /* "aa\r\nbb\r\n" — 8 bytes, two CR-LFs both inside the single word
   * iteration. graphemes = bytes - crlf_pairs = 8 - 2 = 6. */
  const uint8_t* str = (const uint8_t*)"aa\r\nbb\r\n";
  rope r = rope_from_str(str, 8);

  ASSERT_INT_EQ(rope_byte_count(r), 8);
  ASSERT_INT_EQ(rope_char_count(r), 8);
  ASSERT_INT_EQ(rope_line_count(r), 2);
  ASSERT_INT_EQ(rope_grapheme_count(r), 6);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_crlf_across_word_boundary(void) {
  printf("Running test_ascii_fastpath_crlf_across_word_boundary... ");
  tracker_reset();

  /* CR at byte 7 (last in first word), LF at byte 8 (first in second
   * word). The CR-LF lookback (elems[i+k-1]) must reach back across the
   * 8-byte iteration boundary; relies on indexing into elems, not a
   * local word buffer. graphemes = 16 - 1 = 15. */
  const uint8_t* str = (const uint8_t*)"abcdefg\r\nhijklmn";  /* 16 bytes */
  rope r = rope_from_str(str, 16);

  ASSERT_INT_EQ(rope_byte_count(r), 16);
  ASSERT_INT_EQ(rope_char_count(r), 16);
  ASSERT_INT_EQ(rope_line_count(r), 1);
  ASSERT_INT_EQ(rope_grapheme_count(r), 15);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_falls_back_on_high_bit_in_word(void) {
  printf("Running test_ascii_fastpath_falls_back_on_high_bit_in_word... ");
  tracker_reset();

  /* "abcdef" + "é" + "g" — high bit in word loop's first iteration
   * triggers escape. é(2 bytes) means bytes=9, chars=8, graphemes=8.
   * Cross-checks that the fallback path is reached and produces the
   * same answer the non-fast-path code would. */
  const uint8_t str[] = {
    'a', 'b', 'c', 'd', 'e', 'f',
    0xC3, 0xA9,  /* é */
    'g'
  };
  rope r = rope_from_str(str, sizeof(str));

  ASSERT_INT_EQ(rope_byte_count(r), 9);
  ASSERT_INT_EQ(rope_char_count(r), 8);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 8);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_falls_back_on_high_bit_in_tail(void) {
  printf("Running test_ascii_fastpath_falls_back_on_high_bit_in_tail... ");
  tracker_reset();

  /* 8 ASCII bytes (one full word iter, clean) followed by 'é' + 'x'
   * in the tail (high bit in tail loop's escape branch). */
  const uint8_t str[] = {
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',  /* full word, ascii */
    0xC3, 0xA9,                              /* é */
    'x'
  };
  rope r = rope_from_str(str, sizeof(str));

  ASSERT_INT_EQ(rope_byte_count(r), 11);
  ASSERT_INT_EQ(rope_char_count(r), 10);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 10);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_empty(void) {
  printf("Running test_ascii_fastpath_empty... ");
  tracker_reset();

  /* count==0 skips both loops; ascii stays true; returns zeroed
   * summary. Covers the degenerate-input corner. */
  rope r = rope_from_str((const uint8_t*)"", 0);

  ASSERT_INT_EQ(rope_byte_count(r), 0);
  ASSERT_INT_EQ(rope_char_count(r), 0);
  ASSERT_INT_EQ(rope_line_count(r), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r), 0);

  rope_unref(r);
  TEST_PASS();
}

int test_ascii_fastpath_lone_cr_not_counted_as_crlf(void) {
  printf("Running test_ascii_fastpath_lone_cr_not_counted_as_crlf... ");
  tracker_reset();

  /* CR with no following LF must not subtract from graphemes; LF with
   * no preceding CR also must not. */
  const uint8_t* str = (const uint8_t*)"a\rb\nc";  /* 5 bytes */
  rope r = rope_from_str(str, 5);

  ASSERT_INT_EQ(rope_byte_count(r), 5);
  ASSERT_INT_EQ(rope_char_count(r), 5);
  ASSERT_INT_EQ(rope_line_count(r), 1);
  ASSERT_INT_EQ(rope_grapheme_count(r), 5);

  rope_unref(r);
  TEST_PASS();
}

/* === US-007 Tests: insert / delete / replace === */

int test_insert_beginning(void) {
  printf("Running test_insert_beginning... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"world", 5);
  rope r2 = rope_insert(r, 0, (const uint8_t*)"hello ", 6);

  ASSERT_INT_EQ(rope_byte_count(r2), 11);
  ASSERT_INT_EQ(rope_char_count(r2), 11);

  uint8_t buf[64];
  size_t written = rope_to_str(r2, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 11);
  ASSERT(memcmp(buf, "hello world", 11) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_insert_middle(void) {
  printf("Running test_insert_middle... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"helo", 4);
  rope r2 = rope_insert(r, 2, (const uint8_t*)"l", 1);

  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_insert_end(void) {
  printf("Running test_insert_end... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope r2 = rope_insert(r, 5, (const uint8_t*)" world", 6);

  ASSERT_INT_EQ(rope_byte_count(r2), 11);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello world", 11) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_delete_beginning(void) {
  printf("Running test_delete_beginning... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope r2 = rope_delete(r, 0, 6);

  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "world", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_delete_middle(void) {
  printf("Running test_delete_middle... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope r2 = rope_delete(r, 5, 1);

  ASSERT_INT_EQ(rope_byte_count(r2), 10);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "helloworld", 10) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_delete_end(void) {
  printf("Running test_delete_end... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope r2 = rope_delete(r, 5, 6);

  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_replace_range(void) {
  printf("Running test_replace_range... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  /* Replace "world" with "there!" */
  rope r2 = rope_replace(r, 6, 5, (const uint8_t*)"there!", 6);

  ASSERT_INT_EQ(rope_byte_count(r2), 12);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello there!", 12) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_insert_summary_dimensions(void) {
  printf("Running test_insert_summary_dimensions... ");
  tracker_reset();

  /* Start with "ab\n" (3 bytes, 3 chars, 1 line, 3 graphemes) */
  rope r = rope_from_str((const uint8_t*)"ab\n", 3);
  /* Insert "cd\n" at end -> "ab\ncd\n" (6 bytes, 6 chars, 2 lines, 6 graphemes) */
  rope r2 = rope_insert(r, 3, (const uint8_t*)"cd\n", 3);

  ASSERT_INT_EQ(rope_byte_count(r2), 6);
  ASSERT_INT_EQ(rope_char_count(r2), 6);
  ASSERT_INT_EQ(rope_line_count(r2), 2);
  ASSERT_INT_EQ(rope_grapheme_count(r2), 6);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_delete_summary_dimensions(void) {
  printf("Running test_delete_summary_dimensions... ");
  tracker_reset();

  /* "ab\ncd\n" -> delete "cd\n" -> "ab\n" */
  rope r = rope_from_str((const uint8_t*)"ab\ncd\n", 6);
  rope r2 = rope_delete(r, 3, 3);

  ASSERT_INT_EQ(rope_byte_count(r2), 3);
  ASSERT_INT_EQ(rope_char_count(r2), 3);
  ASSERT_INT_EQ(rope_line_count(r2), 1);
  ASSERT_INT_EQ(rope_grapheme_count(r2), 3);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_persistence_after_mutation(void) {
  printf("Running test_persistence_after_mutation... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope r2 = rope_insert(r, 5, (const uint8_t*)" world", 6);

  /* Original rope unchanged */
  ASSERT_INT_EQ(rope_byte_count(r), 5);
  uint8_t buf[64];
  rope_to_str(r, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  /* New rope has the insertion */
  ASSERT_INT_EQ(rope_byte_count(r2), 11);
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello world", 11) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_insert_empty_string(void) {
  printf("Running test_insert_empty_string... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope r2 = rope_insert(r, 2, (const uint8_t*)"", 0);

  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_delete_zero_bytes(void) {
  printf("Running test_delete_zero_bytes... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope r2 = rope_delete(r, 2, 0);

  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_replace_different_length(void) {
  printf("Running test_replace_different_length... ");
  tracker_reset();

  /* Replace 1 byte with 3 bytes */
  rope r = rope_from_str((const uint8_t*)"abc", 3);
  rope r2 = rope_replace(r, 1, 1, (const uint8_t*)"xyz", 3);

  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "axyzc", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_insert_multibyte_summary(void) {
  printf("Running test_insert_multibyte_summary... ");
  tracker_reset();

  /* Insert multi-byte char into ASCII rope */
  rope r = rope_from_str((const uint8_t*)"ac", 2);
  /* Insert é (0xC3 0xA9) at position 1 */
  const uint8_t e_acute[] = {0xC3, 0xA9};
  rope r2 = rope_insert(r, 1, e_acute, 2);

  ASSERT_INT_EQ(rope_byte_count(r2), 4);
  ASSERT_INT_EQ(rope_char_count(r2), 3);
  ASSERT_INT_EQ(rope_grapheme_count(r2), 3);

  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  /* "a" + é + "c" */
  ASSERT(buf[0] == 'a');
  ASSERT(buf[1] == 0xC3);
  ASSERT(buf[2] == 0xA9);
  ASSERT(buf[3] == 'c');

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

/* === US-008 Tests: seek by dimension === */

int test_byte_char_ascii(void) {
  printf("Running test_byte_char_ascii... ");
  tracker_reset();

  /* ASCII: 1:1 mapping between bytes and chars */
  rope r = rope_from_str((const uint8_t*)"hello", 5);

  rope_offset_result res;
  res = rope_byte_to_char(r, 0);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 0);
  res = rope_byte_to_char(r, 3);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 3);
  res = rope_char_to_byte(r, 0);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 0);
  res = rope_char_to_byte(r, 3);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 3);

  rope_unref(r);
  TEST_PASS();
}

int test_byte_char_multibyte(void) {
  printf("Running test_byte_char_multibyte... ");
  tracker_reset();

  /* "Hello, 世界" — H(1)e(1)l(1)l(1)o(1),(1) (1)世(3)界(3) = 13 bytes, 9 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,  /* 世 */
    0xE7, 0x95, 0x8C   /* 界 */
  };
  rope r = rope_from_str(str, sizeof(str));
  ASSERT_INT_EQ(rope_byte_count(r), 13);
  ASSERT_INT_EQ(rope_char_count(r), 9);

  /* char 7 (世) starts at byte 7 */
  rope_offset_result res;
  res = rope_char_to_byte(r, 7);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 7);

  /* char 8 (界) starts at byte 10 */
  res = rope_char_to_byte(r, 8);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 10);

  /* byte 7 -> char 7 */
  res = rope_byte_to_char(r, 7);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 7);

  /* byte 10 -> char 8 */
  res = rope_byte_to_char(r, 10);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 8);

  rope_unref(r);
  TEST_PASS();
}

int test_line_to_byte(void) {
  printf("Running test_line_to_byte... ");
  tracker_reset();

  /* "ab\ncd\nef" — 8 bytes, lines: line 0 at byte 0, line 1 at byte 3, line 2 at byte 6 */
  rope r = rope_from_str((const uint8_t*)"ab\ncd\nef", 8);
  ASSERT_INT_EQ(rope_line_count(r), 2);

  rope_offset_result res;
  /* line 0 starts at byte 0 */
  res = rope_line_to_byte(r, 0);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 0);

  /* line 1 starts at byte 3 (after "ab\n") */
  res = rope_line_to_byte(r, 1);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 3);

  /* line 2 starts at byte 6 (after "cd\n") */
  res = rope_line_to_byte(r, 2);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 6);

  rope_unref(r);
  TEST_PASS();
}

int test_byte_to_line(void) {
  printf("Running test_byte_to_line... ");
  tracker_reset();

  /* "ab\ncd\nef" */
  rope r = rope_from_str((const uint8_t*)"ab\ncd\nef", 8);

  rope_offset_result res;
  /* byte 0 ('a') -> line 0 */
  res = rope_byte_to_line(r, 0);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 0);

  /* byte 2 ('\n') -> line 0 */
  res = rope_byte_to_line(r, 2);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 0);

  /* byte 3 ('c') -> line 1 (after first newline) */
  res = rope_byte_to_line(r, 3);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 1);

  /* byte 6 ('e') -> line 2 (after second newline) */
  res = rope_byte_to_line(r, 6);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 2);

  rope_unref(r);
  TEST_PASS();
}

int test_grapheme_conversions_combining(void) {
  printf("Running test_grapheme_conversions_combining... ");
  tracker_reset();

  /* "e" + combining acute (U+0301) + "a" = 2 graphemes, 3 chars, 4 bytes */
  const uint8_t str[] = {
    'e',
    0xCC, 0x81,  /* U+0301 combining acute */
    'a'
  };
  rope r = rope_from_str(str, sizeof(str));
  ASSERT_INT_EQ(rope_byte_count(r), 4);
  ASSERT_INT_EQ(rope_grapheme_count(r), 2);

  rope_offset_result res;
  /* grapheme 0 at byte 0 */
  res = rope_grapheme_to_byte(r, 0);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 0);

  /* grapheme 1 ("a") at byte 3 */
  res = rope_grapheme_to_byte(r, 1);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 3);

  /* byte 0 -> grapheme 0 */
  res = rope_byte_to_grapheme(r, 0);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 0);

  /* byte 3 -> grapheme 1 */
  res = rope_byte_to_grapheme(r, 3);
  ASSERT(res.found); ASSERT_INT_EQ(res.value, 1);

  rope_unref(r);
  TEST_PASS();
}

int test_seek_out_of_bounds(void) {
  printf("Running test_seek_out_of_bounds... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);

  rope_offset_result res;
  /* byte offset beyond total */
  res = rope_byte_to_char(r, 10);
  ASSERT(!res.found);
  res = rope_byte_to_line(r, 10);
  ASSERT(!res.found);
  res = rope_byte_to_grapheme(r, 10);
  ASSERT(!res.found);

  /* char offset beyond total */
  res = rope_char_to_byte(r, 10);
  ASSERT(!res.found);

  /* line offset beyond total */
  res = rope_line_to_byte(r, 10);
  ASSERT(!res.found);

  /* grapheme offset beyond total */
  res = rope_grapheme_to_byte(r, 10);
  ASSERT(!res.found);

  /* Also test empty rope */
  rope empty = rope_new();
  res = rope_byte_to_char(empty, 1);
  ASSERT(!res.found);
  res = rope_char_to_byte(empty, 1);
  ASSERT(!res.found);

  rope_unref(r);
  rope_unref(empty);
  TEST_PASS();
}

/* === US-009 Tests: slice extraction === */

int test_slice_beginning(void) {
  printf("Running test_slice_beginning... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope s = rope_slice(r, 0, 5);

  ASSERT_INT_EQ(rope_byte_count(s), 5);
  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_slice_middle(void) {
  printf("Running test_slice_middle... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope s = rope_slice(r, 6, 5);

  ASSERT_INT_EQ(rope_byte_count(s), 5);
  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, "world", 5) == 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_slice_end(void) {
  printf("Running test_slice_end... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope s = rope_slice(r, 5, 6);

  ASSERT_INT_EQ(rope_byte_count(s), 6);
  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, " world", 6) == 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_slice_entire_rope(void) {
  printf("Running test_slice_entire_rope... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope s = rope_slice(r, 0, 11);

  ASSERT_INT_EQ(rope_byte_count(s), 11);
  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello world", 11) == 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_slice_multibyte_boundaries(void) {
  printf("Running test_slice_multibyte_boundaries... ");
  tracker_reset();

  /* "Hé世🌍a" = H(1) é(2) 世(3) 🌍(4) a(1) = 11 bytes */
  const uint8_t str[] = {
    'H',
    0xC3, 0xA9,             /* é */
    0xE4, 0xB8, 0x96,       /* 世 */
    0xF0, 0x9F, 0x8C, 0x8D, /* 🌍 */
    'a'
  };
  rope r = rope_from_str(str, sizeof(str));

  /* Slice "世🌍" = bytes [3..10) = 7 bytes */
  rope s = rope_slice(r, 3, 7);
  ASSERT_INT_EQ(rope_byte_count(s), 7);
  ASSERT_INT_EQ(rope_char_count(s), 2);

  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, str + 3, 7) == 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_slice_to_str_copies(void) {
  printf("Running test_slice_to_str_copies... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  uint8_t buf[64];
  size_t written = rope_slice_to_str(r, 6, 5, buf, sizeof(buf));

  ASSERT_INT_EQ(written, 5);
  ASSERT(memcmp(buf, "world", 5) == 0);

  rope_unref(r);
  TEST_PASS();
}

int test_line_slice_extracts_lines(void) {
  printf("Running test_line_slice_extracts_lines... ");
  tracker_reset();

  /* "line0\nline1\nline2\nline3" */
  rope r = rope_from_str((const uint8_t*)"line0\nline1\nline2\nline3", 23);
  ASSERT_INT_EQ(rope_line_count(r), 3);

  /* Extract lines [1, 3) = "line1\nline2\n" */
  rope s = rope_line_slice(r, 1, 3);
  ASSERT_INT_EQ(rope_byte_count(s), 12);
  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, "line1\nline2\n", 12) == 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_slice_empty_range(void) {
  printf("Running test_slice_empty_range... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope s = rope_slice(r, 2, 0);

  ASSERT_INT_EQ(rope_byte_count(s), 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

/* === US-011 Tests: configurable leaf size === */

int test_large_string_256_leaves(void) {
  printf("Running test_large_string_256_leaves... ");
  tracker_reset();

  /* Build a 100KB+ string with mixed content:
     pattern per unit: 60 ASCII + newline + "café" (5 bytes, 4 chars) = 66 bytes
     repeat 1550 times = 102,300 bytes */
  size_t unit_len = 66;
  size_t n_units = 1550;
  size_t total_len = unit_len * n_units;
  uint8_t* str = (uint8_t*)malloc(total_len);

  size_t pos = 0;
  for (size_t i = 0; i < n_units; i++) {
    for (size_t j = 0; j < 60; j++) {
      str[pos++] = 'a' + (uint8_t)(j % 26);
    }
    str[pos++] = '\n';
    /* "café" = c a f 0xC3 0xA9 */
    str[pos++] = 'c';
    str[pos++] = 'a';
    str[pos++] = 'f';
    str[pos++] = 0xC3;
    str[pos++] = 0xA9; /* é */
  }
  ASSERT_INT_EQ(pos, total_len);

  rope r = rope_from_str(str, total_len);

  /* Verify bytes dimension */
  ASSERT_INT_EQ(rope_byte_count(r), total_len);

  /* Verify chars: 60 ASCII + 1 newline + 4 chars ("café") = 65 per unit */
  ASSERT_INT_EQ(rope_char_count(r), 65 * n_units);

  /* Verify lines: 1 newline per unit */
  ASSERT_INT_EQ(rope_line_count(r), n_units);

  /* Verify graphemes: same as chars (no combining marks) = 65 per unit */
  ASSERT_INT_EQ(rope_grapheme_count(r), 65 * n_units);

  /* Round-trip verification */
  uint8_t* buf = (uint8_t*)malloc(total_len);
  size_t written = rope_to_str(r, buf, total_len);
  ASSERT_INT_EQ(written, total_len);
  ASSERT(memcmp(buf, str, total_len) == 0);

  free(buf);
  free(str);
  rope_unref(r);
  TEST_PASS();
}

/* === US-012 Tests: convenience insert/delete/slice by char and line === */

int test_insert_at_char_ascii(void) {
  printf("Running test_insert_at_char_ascii... ");
  tracker_reset();

  /* ASCII text: 1:1 char/byte mapping */
  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope r2 = rope_insert_at_char(r, 5, (const uint8_t*)",", 1);

  ASSERT_INT_EQ(rope_byte_count(r2), 12);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello, world", 12) == 0);

  /* Verify matches rope_insert at same byte offset */
  rope r3 = rope_insert(r, 5, (const uint8_t*)",", 1);
  ASSERT_INT_EQ(rope_byte_count(r3), rope_byte_count(r2));
  uint8_t buf2[64];
  rope_to_str(r3, buf2, sizeof(buf2));
  ASSERT(memcmp(buf, buf2, 12) == 0);

  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_insert_at_char_multibyte(void) {
  printf("Running test_insert_at_char_multibyte... ");
  tracker_reset();

  /* "Hello, 世界" = 13 bytes, 9 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,  /* 世 */
    0xE7, 0x95, 0x8C   /* 界 */
  };
  rope r = rope_from_str(str, sizeof(str));

  /* Insert "!" after char 7 (after 世, before 界) — byte offset 10 */
  rope r2 = rope_insert_at_char(r, 8, (const uint8_t*)"!", 1);
  ASSERT_INT_EQ(rope_byte_count(r2), 14);
  ASSERT_INT_EQ(rope_char_count(r2), 10);

  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  /* Expected: "Hello, 世!界" */
  ASSERT(memcmp(buf, str, 10) == 0);     /* "Hello, 世" */
  ASSERT(buf[10] == '!');
  ASSERT(memcmp(buf + 11, str + 10, 3) == 0); /* 界 */

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_delete_at_char_multibyte(void) {
  printf("Running test_delete_at_char_multibyte... ");
  tracker_reset();

  /* "Hello, 世界" = 13 bytes, 9 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,  /* 世 */
    0xE7, 0x95, 0x8C   /* 界 */
  };
  rope r = rope_from_str(str, sizeof(str));

  /* Delete 2 chars starting at char 7 (世界) = 6 bytes */
  rope r2 = rope_delete_at_char(r, 7, 2);
  ASSERT_INT_EQ(rope_byte_count(r2), 7);
  ASSERT_INT_EQ(rope_char_count(r2), 7);

  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "Hello, ", 7) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_delete_at_line(void) {
  printf("Running test_delete_at_line... ");
  tracker_reset();

  /* "line0\nline1\nline2\nline3" = 23 bytes, 3 newlines */
  rope r = rope_from_str((const uint8_t*)"line0\nline1\nline2\nline3", 23);

  /* Delete lines [1, 3) = "line1\nline2\n" */
  rope r2 = rope_delete_at_line(r, 1, 3);
  ASSERT_INT_EQ(rope_byte_count(r2), 11);  /* "line0\n" + "line3" */

  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "line0\nline3", 11) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_slice_by_chars(void) {
  printf("Running test_slice_by_chars... ");
  tracker_reset();

  /* "Hello, 世界" = 13 bytes, 9 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,  /* 世 */
    0xE7, 0x95, 0x8C   /* 界 */
  };
  rope r = rope_from_str(str, sizeof(str));

  /* Slice chars [5, 9) = ", 世界" = 6 bytes */
  rope s = rope_slice_by_chars(r, 5, 4);
  ASSERT_INT_EQ(rope_byte_count(s), 8); /* , + space + 世(3) + 界(3) = 8 */
  ASSERT_INT_EQ(rope_char_count(s), 4);

  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, str + 5, 8) == 0);

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_slice_by_graphemes_combining(void) {
  printf("Running test_slice_by_graphemes_combining... ");
  tracker_reset();

  /* "ae" + combining acute on 'e' + "bc" = "a" + "é" + "b" + "c"
     = 4 graphemes, 5 chars, 6 bytes */
  const uint8_t str[] = {
    'a',
    'e', 0xCC, 0x81,  /* e + combining acute = é (2 codepoints, 1 grapheme) */
    'b',
    'c'
  };
  rope r = rope_from_str(str, sizeof(str));
  ASSERT_INT_EQ(rope_byte_count(r), 6);
  ASSERT_INT_EQ(rope_grapheme_count(r), 4);

  /* Slice graphemes [1, 3) = "é" + "b" = 4 bytes */
  rope s = rope_slice_by_graphemes(r, 1, 2);
  ASSERT_INT_EQ(rope_byte_count(s), 4);
  ASSERT_INT_EQ(rope_grapheme_count(s), 2);

  uint8_t buf[64];
  rope_to_str(s, buf, sizeof(buf));
  ASSERT(memcmp(buf, str + 1, 4) == 0); /* "e\xCC\x81b" */

  rope_unref(r);
  rope_unref(s);
  TEST_PASS();
}

int test_insert_at_char_out_of_bounds(void) {
  printf("Running test_insert_at_char_out_of_bounds... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);

  /* Out-of-bounds char offset returns unchanged rope */
  rope r2 = rope_insert_at_char(r, 100, (const uint8_t*)"x", 1);
  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  /* Out-of-bounds slice returns empty */
  rope s = rope_slice_by_chars(r, 100, 5);
  ASSERT_INT_EQ(rope_byte_count(s), 0);

  rope_unref(r);
  rope_unref(r2);
  rope_unref(s);
  TEST_PASS();
}

/* === US-013 Tests: cursor and forward navigation === */

int test_cursor_advance_chars_ascii(void) {
  printf("Running test_cursor_advance_chars_ascii... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope_cursor c = rope_cursor_new(r, 0);
  ASSERT(c.valid);

  /* Advance char-by-char, verify byte_offset at each step */
  for (size_t i = 0; i < 5; i++) {
    ASSERT_INT_EQ(rope_cursor_byte_offset(&c), i);
    ASSERT(rope_cursor_advance_chars(&c, 1));
  }
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 5);

  rope_cursor_free(c);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_advance_chars_multibyte(void) {
  printf("Running test_cursor_advance_chars_multibyte... ");
  tracker_reset();

  /* "Hello, 世界🌍" = 17 bytes, 10 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,              /* 世 */
    0xE7, 0x95, 0x8C,              /* 界 */
    0xF0, 0x9F, 0x8C, 0x8D         /* 🌍 */
  };
  rope r = rope_from_str(str, sizeof(str));
  ASSERT_INT_EQ(rope_byte_count(r), 17);
  ASSERT_INT_EQ(rope_char_count(r), 10);

  rope_cursor c = rope_cursor_new(r, 0);
  ASSERT(c.valid);

  /* Advance through each char, verify byte offsets match rope_char_to_byte */
  for (size_t i = 0; i < 10; i++) {
    rope_offset_result expected = rope_char_to_byte(r, i);
    ASSERT(expected.found);
    ASSERT_INT_EQ(rope_cursor_byte_offset(&c), expected.value);
    if (i < 9) {
      ASSERT(rope_cursor_advance_chars(&c, 1));
    }
  }

  rope_cursor_free(c);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_advance_lines(void) {
  printf("Running test_cursor_advance_lines... ");
  tracker_reset();

  /* "line0\nline1\nline2\nline3" = 23 bytes, 3 newlines */
  rope r = rope_from_str((const uint8_t*)"line0\nline1\nline2\nline3", 23);
  rope_cursor c = rope_cursor_new(r, 0);
  ASSERT(c.valid);

  /* At start: line 0 */
  ASSERT_INT_EQ(rope_cursor_line(&c), 0);

  /* Advance 1 line -> at start of line 1 */
  ASSERT(rope_cursor_advance_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_line(&c), 1);
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 6);

  /* Advance 1 more line -> at start of line 2 */
  ASSERT(rope_cursor_advance_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_line(&c), 2);
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 12);

  /* Advance 1 more line -> at start of line 3 */
  ASSERT(rope_cursor_advance_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_line(&c), 3);
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 18);

  /* No more lines to advance past */
  ASSERT(!rope_cursor_advance_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 18); /* unchanged */

  rope_cursor_free(c);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_advance_past_end(void) {
  printf("Running test_cursor_advance_past_end... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);

  /* Advance bytes past end */
  rope_cursor c1 = rope_cursor_new(r, 3);
  ASSERT(c1.valid);
  ASSERT(!rope_cursor_advance_bytes(&c1, 10));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c1), 3); /* unchanged */

  /* Advance chars past end */
  rope_cursor c2 = rope_cursor_new(r, 0);
  ASSERT(!rope_cursor_advance_chars(&c2, 10));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c2), 0); /* unchanged */

  /* Cursor at end: advance returns false */
  rope_cursor c3 = rope_cursor_new(r, 5);
  ASSERT(c3.valid);
  ASSERT(!rope_cursor_advance_bytes(&c3, 1));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c3), 5);

  rope_cursor_free(c1);
  rope_cursor_free(c2);
  rope_cursor_free(c3);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_peek(void) {
  printf("Running test_cursor_peek... ");
  tracker_reset();

  const uint8_t* str = (const uint8_t*)"abc";
  rope r = rope_from_str(str, 3);
  rope_cursor c = rope_cursor_new(r, 0);

  /* Peek at each position */
  ASSERT_INT_EQ(rope_cursor_peek(&c), 'a');
  ASSERT(rope_cursor_advance_bytes(&c, 1));
  ASSERT_INT_EQ(rope_cursor_peek(&c), 'b');
  ASSERT(rope_cursor_advance_bytes(&c, 1));
  ASSERT_INT_EQ(rope_cursor_peek(&c), 'c');
  ASSERT(rope_cursor_advance_bytes(&c, 1));
  /* At end */
  ASSERT_INT_EQ(rope_cursor_peek(&c), -1);

  rope_cursor_free(c);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_read(void) {
  printf("Running test_cursor_read... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope_cursor c = rope_cursor_new(r, 6);

  /* Read from cursor position without advancing */
  uint8_t buf[64];
  size_t n = rope_cursor_read(&c, buf, 5);
  ASSERT_INT_EQ(n, 5);
  ASSERT(memcmp(buf, "world", 5) == 0);

  /* Cursor position unchanged */
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 6);

  /* Read more than available */
  n = rope_cursor_read(&c, buf, 20);
  ASSERT_INT_EQ(n, 5); /* only 5 bytes left */

  /* Read from end */
  rope_cursor c2 = rope_cursor_new(r, 11);
  n = rope_cursor_read(&c2, buf, 10);
  ASSERT_INT_EQ(n, 0);

  rope_cursor_free(c);
  rope_cursor_free(c2);
  rope_unref(r);
  TEST_PASS();
}

/* === US-014 Tests: cursor backward navigation === */

int test_cursor_retreat_bytes(void) {
  printf("Running test_cursor_retreat_bytes... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope_cursor c = rope_cursor_new(r, 11); /* at end */
  ASSERT(c.valid);
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 11);

  /* Retreat byte-by-byte to beginning */
  for (size_t i = 11; i > 0; i--) {
    ASSERT(rope_cursor_retreat_bytes(&c, 1));
    ASSERT_INT_EQ(rope_cursor_byte_offset(&c), i - 1);
  }
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 0);

  rope_cursor_free(c);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_retreat_chars_multibyte(void) {
  printf("Running test_cursor_retreat_chars_multibyte... ");
  tracker_reset();

  /* "Hello, 世界🌍" = 17 bytes, 10 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,              /* 世 */
    0xE7, 0x95, 0x8C,              /* 界 */
    0xF0, 0x9F, 0x8C, 0x8D         /* 🌍 */
  };
  rope r = rope_from_str(str, sizeof(str));
  ASSERT_INT_EQ(rope_byte_count(r), 17);
  ASSERT_INT_EQ(rope_char_count(r), 10);

  /* Start at end, retreat char-by-char */
  rope_cursor c = rope_cursor_new(r, 17);
  ASSERT(c.valid);

  for (size_t i = 10; i > 0; i--) {
    ASSERT(rope_cursor_retreat_chars(&c, 1));
    rope_offset_result expected = rope_char_to_byte(r, i - 1);
    ASSERT(expected.found);
    ASSERT_INT_EQ(rope_cursor_byte_offset(&c), expected.value);
  }
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 0);

  rope_cursor_free(c);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_retreat_lines(void) {
  printf("Running test_cursor_retreat_lines... ");
  tracker_reset();

  /* "line0\nline1\nline2\nline3" = 23 bytes, 3 newlines */
  rope r = rope_from_str((const uint8_t*)"line0\nline1\nline2\nline3", 23);
  rope_cursor c = rope_cursor_new(r, 23); /* at end */
  ASSERT(c.valid);
  ASSERT_INT_EQ(rope_cursor_line(&c), 3);

  /* Retreat line by line */
  ASSERT(rope_cursor_retreat_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_line(&c), 2);
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 12);

  ASSERT(rope_cursor_retreat_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_line(&c), 1);
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 6);

  ASSERT(rope_cursor_retreat_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_line(&c), 0);
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 0);

  /* Can't retreat further */
  ASSERT(!rope_cursor_retreat_lines(&c, 1));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 0);

  rope_cursor_free(c);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_retreat_past_beginning(void) {
  printf("Running test_cursor_retreat_past_beginning... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);

  /* Retreat bytes past beginning */
  rope_cursor c1 = rope_cursor_new(r, 3);
  ASSERT(!rope_cursor_retreat_bytes(&c1, 10));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c1), 3); /* unchanged */

  /* Retreat chars past beginning */
  rope_cursor c2 = rope_cursor_new(r, 2);
  ASSERT(!rope_cursor_retreat_chars(&c2, 10));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c2), 2); /* unchanged */

  /* Retreat at position 0 */
  rope_cursor c3 = rope_cursor_new(r, 0);
  ASSERT(!rope_cursor_retreat_bytes(&c3, 1));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c3), 0);

  rope_cursor_free(c1);
  rope_cursor_free(c2);
  rope_cursor_free(c3);
  rope_unref(r);
  TEST_PASS();
}

int test_cursor_advance_then_retreat(void) {
  printf("Running test_cursor_advance_then_retreat... ");
  tracker_reset();

  /* "Hello, 世界🌍" = 17 bytes, 10 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,              /* 世 */
    0xE7, 0x95, 0x8C,              /* 界 */
    0xF0, 0x9F, 0x8C, 0x8D         /* 🌍 */
  };
  rope r = rope_from_str(str, sizeof(str));

  /* Advance bytes then retreat same amount */
  rope_cursor c1 = rope_cursor_new(r, 0);
  ASSERT(rope_cursor_advance_bytes(&c1, 7));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c1), 7);
  ASSERT(rope_cursor_retreat_bytes(&c1, 7));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c1), 0);

  /* Advance chars then retreat same amount */
  rope_cursor c2 = rope_cursor_new(r, 0);
  ASSERT(rope_cursor_advance_chars(&c2, 8));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c2), 10); /* after 界 start */
  ASSERT(rope_cursor_retreat_chars(&c2, 8));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c2), 0);

  /* Multi-line: advance lines then retreat */
  rope r2 = rope_from_str((const uint8_t*)"line0\nline1\nline2\nline3", 23);
  rope_cursor c3 = rope_cursor_new(r2, 0);
  ASSERT(rope_cursor_advance_lines(&c3, 2));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c3), 12);
  ASSERT(rope_cursor_retreat_lines(&c3, 2));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c3), 0);

  rope_cursor_free(c1);
  rope_cursor_free(c2);
  rope_cursor_free(c3);
  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

/* === US-015 Tests: cursor editing (insert, delete, replace) === */

int test_cursor_insert_matches_rope_insert(void) {
  printf("Running test_cursor_insert_matches_rope_insert... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);

  /* Insert via cursor at offset 5 */
  rope_cursor c = rope_cursor_new(r, 5);
  ASSERT(c.valid);
  rope r2 = rope_cursor_insert(&c, (const uint8_t*)", beautiful", 11);

  /* Insert via rope_insert at same offset */
  rope r3 = rope_insert(r, 5, (const uint8_t*)", beautiful", 11);

  /* Compare results */
  ASSERT_INT_EQ(rope_byte_count(r2), rope_byte_count(r3));
  uint8_t buf2[64], buf3[64];
  rope_to_str(r2, buf2, sizeof(buf2));
  rope_to_str(r3, buf3, sizeof(buf3));
  ASSERT(memcmp(buf2, buf3, rope_byte_count(r2)) == 0);

  /* Cursor should be invalidated */
  ASSERT(!c.valid);

  rope_cursor_free(c);
  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_cursor_delete_matches_rope_delete(void) {
  printf("Running test_cursor_delete_matches_rope_delete... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);

  /* Delete via cursor at offset 5 */
  rope_cursor c = rope_cursor_new(r, 5);
  ASSERT(c.valid);
  rope r2 = rope_cursor_delete(&c, 6); /* delete " world" */

  /* Delete via rope_delete at same offset */
  rope r3 = rope_delete(r, 5, 6);

  /* Compare results */
  ASSERT_INT_EQ(rope_byte_count(r2), rope_byte_count(r3));
  uint8_t buf2[64], buf3[64];
  rope_to_str(r2, buf2, sizeof(buf2));
  rope_to_str(r3, buf3, sizeof(buf3));
  ASSERT(memcmp(buf2, buf3, rope_byte_count(r2)) == 0);

  rope_cursor_free(c);
  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_cursor_replace_matches_rope_replace(void) {
  printf("Running test_cursor_replace_matches_rope_replace... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);

  /* Replace via cursor at offset 6: "world" -> "there!" */
  rope_cursor c = rope_cursor_new(r, 6);
  ASSERT(c.valid);
  rope r2 = rope_cursor_replace(&c, 5, (const uint8_t*)"there!", 6);

  /* Replace via rope_replace at same offset */
  rope r3 = rope_replace(r, 6, 5, (const uint8_t*)"there!", 6);

  /* Compare results */
  ASSERT_INT_EQ(rope_byte_count(r2), rope_byte_count(r3));
  uint8_t buf2[64], buf3[64];
  rope_to_str(r2, buf2, sizeof(buf2));
  rope_to_str(r3, buf3, sizeof(buf3));
  ASSERT(memcmp(buf2, buf3, rope_byte_count(r2)) == 0);

  rope_cursor_free(c);
  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_cursor_edit_multibyte(void) {
  printf("Running test_cursor_edit_multibyte... ");
  tracker_reset();

  /* "Hello, 世界" = 13 bytes, 9 chars */
  const uint8_t str[] = {
    'H', 'e', 'l', 'l', 'o', ',', ' ',
    0xE4, 0xB8, 0x96,  /* 世 */
    0xE7, 0x95, 0x8C   /* 界 */
  };
  rope r = rope_from_str(str, sizeof(str));

  /* Navigate to char 8 (after 世) = byte 10 */
  rope_cursor c = rope_cursor_new(r, 0);
  ASSERT(rope_cursor_advance_chars(&c, 8));
  ASSERT_INT_EQ(rope_cursor_byte_offset(&c), 10);

  /* Insert "美" (3 bytes) at cursor position */
  const uint8_t insert_str[] = {0xE7, 0xBE, 0x8E}; /* 美 */
  rope r2 = rope_cursor_insert(&c, insert_str, sizeof(insert_str));

  /* Verify correct dimensions */
  ASSERT_INT_EQ(rope_byte_count(r2), 16); /* 13 + 3 */
  ASSERT_INT_EQ(rope_char_count(r2), 10); /* 9 + 1 */

  /* Verify against rope_insert */
  rope r3 = rope_insert(r, 10, insert_str, sizeof(insert_str));
  ASSERT_INT_EQ(rope_byte_count(r2), rope_byte_count(r3));
  uint8_t buf2[64], buf3[64];
  rope_to_str(r2, buf2, sizeof(buf2));
  rope_to_str(r3, buf3, sizeof(buf3));
  ASSERT(memcmp(buf2, buf3, rope_byte_count(r2)) == 0);

  rope_cursor_free(c);
  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_cursor_invalidation(void) {
  printf("Running test_cursor_invalidation... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);

  /* Insert at cursor, then try to use cursor */
  rope_cursor c = rope_cursor_new(r, 5);
  rope r2 = rope_cursor_insert(&c, (const uint8_t*)"!", 1);

  /* Cursor is now invalid */
  ASSERT(!c.valid);

  /* advance returns false */
  ASSERT(!rope_cursor_advance_bytes(&c, 1));
  ASSERT(!rope_cursor_advance_chars(&c, 1));
  ASSERT(!rope_cursor_advance_lines(&c, 1));
  ASSERT(!rope_cursor_advance_graphemes(&c, 1));

  /* retreat returns false */
  ASSERT(!rope_cursor_retreat_bytes(&c, 1));
  ASSERT(!rope_cursor_retreat_chars(&c, 1));
  ASSERT(!rope_cursor_retreat_lines(&c, 1));

  /* peek returns -1 */
  ASSERT_INT_EQ(rope_cursor_peek(&c), -1);

  /* read returns 0 */
  uint8_t buf[64];
  ASSERT_INT_EQ(rope_cursor_read(&c, buf, sizeof(buf)), 0);

  /* edit on invalid cursor returns empty rope */
  rope r3 = rope_cursor_insert(&c, (const uint8_t*)"x", 1);
  ASSERT_INT_EQ(rope_byte_count(r3), 0);

  rope_cursor_free(c);
  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

/* === US-016: Multi-dimensional seek tests === */

int test_seek_line0_col0(void) {
  printf("Running test_seek_line0_col0... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"Hello\nWorld\n", 12);
  rope_offset_result res = rope_seek(r, 0, 0, ROPE_DIM_BYTES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 0);

  res = rope_seek(r, 0, 0, ROPE_DIM_CHARS);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 0);

  res = rope_seek(r, 0, 0, ROPE_DIM_GRAPHEMES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 0);

  rope_unref(r);
  TEST_PASS();
}

int test_seek_ascii_line_col(void) {
  printf("Running test_seek_ascii_line_col... ");
  tracker_reset();

  /* "Line 0\nLine 1\nLine 2 text\n" */
  const char* text = "Line 0\nLine 1\nLine 2 text\n";
  rope r = rope_from_str((const uint8_t*)text, strlen(text));

  /* Line 0 starts at byte 0, Line 1 at byte 7, Line 2 at byte 14 */
  /* seek(line 2, col 5, BYTES) = 14 + 5 = 19 */
  rope_offset_result res = rope_seek(r, 2, 5, ROPE_DIM_BYTES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 19);

  /* seek(line 1, col 4, BYTES) = 7 + 4 = 11 ('1') */
  res = rope_seek(r, 1, 4, ROPE_DIM_BYTES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 11);

  /* seek(line 0, col 0, BYTES) = 0 */
  res = rope_seek(r, 0, 0, ROPE_DIM_BYTES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 0);

  rope_unref(r);
  TEST_PASS();
}

int test_seek_chars_multibyte(void) {
  printf("Running test_seek_chars_multibyte... ");
  tracker_reset();

  /* "Hello\n你好世界\n" */
  /* Line 0: "Hello\n" = 6 bytes, starts at 0 */
  /* Line 1: "你好世界\n" = 13 bytes (4*3+1), starts at 6 */
  /* On line 1: char0=你(6), char1=好(9), char2=世(12), char3=界(15), char4=\n(18) */
  const uint8_t text[] = {
    'H', 'e', 'l', 'l', 'o', '\n',
    0xE4, 0xBD, 0xA0,  /* 你 */
    0xE5, 0xA5, 0xBD,  /* 好 */
    0xE4, 0xB8, 0x96,  /* 世 */
    0xE7, 0x95, 0x8C,  /* 界 */
    '\n'
  };
  rope r = rope_from_str(text, sizeof(text));

  /* seek(line 1, col 3, CHARS) = byte offset of 界 = 15 */
  rope_offset_result res = rope_seek(r, 1, 3, ROPE_DIM_CHARS);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 15);

  /* seek(line 1, col 0, CHARS) = byte 6 (start of line 1) */
  res = rope_seek(r, 1, 0, ROPE_DIM_CHARS);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 6);

  /* seek(line 0, col 3, CHARS) = byte 3 ('l') */
  res = rope_seek(r, 0, 3, ROPE_DIM_CHARS);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 3);

  rope_unref(r);
  TEST_PASS();
}

int test_seek_graphemes_combining(void) {
  printf("Running test_seek_graphemes_combining... ");
  tracker_reset();

  /* "abc\ne\xCC\x81fg\n" */
  /* Line 0: "abc\n" = 4 bytes */
  /* Line 1: "e\xCC\x81fg\n" = 6 bytes, starts at 4 */
  /*   grapheme 0: é (e + combining acute) = 3 bytes at 4 */
  /*   grapheme 1: f = 1 byte at 7 */
  /*   grapheme 2: g = 1 byte at 8 */
  /*   grapheme 3: \n = 1 byte at 9 */
  const uint8_t text[] = {
    'a', 'b', 'c', '\n',
    'e', 0xCC, 0x81,  /* é (e + combining acute) */
    'f', 'g', '\n'
  };
  rope r = rope_from_str(text, sizeof(text));

  /* seek(line 1, col 2, GRAPHEMES) = byte 8 (start of 'g') */
  rope_offset_result res = rope_seek(r, 1, 2, ROPE_DIM_GRAPHEMES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 8);

  /* seek(line 1, col 0, GRAPHEMES) = byte 4 (start of line 1) */
  res = rope_seek(r, 1, 0, ROPE_DIM_GRAPHEMES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 4);

  /* seek(line 1, col 1, GRAPHEMES) = byte 7 (start of 'f') */
  res = rope_seek(r, 1, 1, ROPE_DIM_GRAPHEMES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 7);

  rope_unref(r);
  TEST_PASS();
}

int test_seek_nonexistent_line(void) {
  printf("Running test_seek_nonexistent_line... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"abc\ndef\n", 8);

  /* Line 0: "abc\n", Line 1: "def\n", Line 2: "" (empty trailing) */
  /* Line 10 doesn't exist */
  rope_offset_result res = rope_seek(r, 10, 0, ROPE_DIM_BYTES);
  ASSERT(!res.found);

  res = rope_seek(r, 10, 5, ROPE_DIM_CHARS);
  ASSERT(!res.found);

  rope_unref(r);
  TEST_PASS();
}

int test_seek_column_clamping(void) {
  printf("Running test_seek_column_clamping... ");
  tracker_reset();

  /* "ab\ncd\nefghij" */
  /* Line 0: "ab\n" (3 bytes), starts at 0 */
  /* Line 1: "cd\n" (3 bytes), starts at 3 */
  /* Line 2: "efghij" (6 bytes, no trailing \n), starts at 6 */
  const char* text = "ab\ncd\nefghij";
  rope r = rope_from_str((const uint8_t*)text, strlen(text));

  /* Column 100 on line 1 (3 bytes long) clamps to line end = byte 6 */
  rope_offset_result res = rope_seek(r, 1, 100, ROPE_DIM_BYTES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 6);

  /* Column 100 on line 2 (6 bytes, last line) clamps to total = 12 */
  res = rope_seek(r, 2, 100, ROPE_DIM_BYTES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 12);

  /* Column 100 chars on line 1 also clamps to byte 6 */
  res = rope_seek(r, 1, 100, ROPE_DIM_CHARS);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 6);

  /* Column 100 graphemes on line 1 also clamps to byte 6 */
  res = rope_seek(r, 1, 100, ROPE_DIM_GRAPHEMES);
  ASSERT(res.found);
  ASSERT_INT_EQ(res.value, 6);

  rope_unref(r);
  TEST_PASS();
}

/* === US-017 Tests: Transient COW rope — creation and insert === */

int test_transient_no_edits(void) {
  printf("Running test_transient_no_edits... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope_transient t = rope_to_transient(r);
  rope r2 = rope_transient_to_persistent(&t);

  /* Content should match original */
  ASSERT_INT_EQ(rope_byte_count(r2), 11);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello world", 11) == 0);

  /* All dimensions match */
  ASSERT_INT_EQ(rope_char_count(r2), rope_char_count(r));
  ASSERT_INT_EQ(rope_line_count(r2), rope_line_count(r));
  ASSERT_INT_EQ(rope_grapheme_count(r2), rope_grapheme_count(r));

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_transient_insert(void) {
  printf("Running test_transient_insert... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope_transient t = rope_to_transient(r);
  rope_transient_insert(&t, 5, (const uint8_t*)",", 1);
  rope r2 = rope_transient_to_persistent(&t);

  /* Should match rope_insert at same offset */
  rope r3 = rope_insert(r, 5, (const uint8_t*)",", 1);
  ASSERT_INT_EQ(rope_byte_count(r2), rope_byte_count(r3));

  uint8_t buf2[64], buf3[64];
  rope_to_str(r2, buf2, sizeof(buf2));
  rope_to_str(r3, buf3, sizeof(buf3));
  ASSERT(memcmp(buf2, buf3, rope_byte_count(r2)) == 0);
  ASSERT(memcmp(buf2, "hello, world", 12) == 0);

  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_transient_two_inserts(void) {
  printf("Running test_transient_two_inserts... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope_transient t = rope_to_transient(r);
  rope_transient_insert(&t, 5, (const uint8_t*)" world", 6);   /* "hello world" */
  rope_transient_insert(&t, 5, (const uint8_t*)",", 1);         /* "hello, world" */
  rope r2 = rope_transient_to_persistent(&t);

  ASSERT_INT_EQ(rope_byte_count(r2), 12);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello, world", 12) == 0);

  /* Verify all dimensions */
  ASSERT_INT_EQ(rope_char_count(r2), 12);
  ASSERT_INT_EQ(rope_line_count(r2), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r2), 12);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_transient_persistence(void) {
  printf("Running test_transient_persistence... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello", 5);
  rope_transient t = rope_to_transient(r);
  rope_transient_insert(&t, 5, (const uint8_t*)" world", 6);
  rope r2 = rope_transient_to_persistent(&t);

  /* Original rope unchanged */
  ASSERT_INT_EQ(rope_byte_count(r), 5);
  uint8_t buf[64];
  rope_to_str(r, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello", 5) == 0);

  /* New rope has the edit */
  ASSERT_INT_EQ(rope_byte_count(r2), 11);
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello world", 11) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

/* === US-018 Tests: Transient COW rope — delete, replace, and performance === */

int test_transient_delete(void) {
  printf("Running test_transient_delete... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope_transient t = rope_to_transient(r);
  rope_transient_delete(&t, 5, 6); /* delete " world" */
  rope r2 = rope_transient_to_persistent(&t);

  /* Should match rope_delete at same offset */
  rope r3 = rope_delete(r, 5, 6);
  ASSERT_INT_EQ(rope_byte_count(r2), rope_byte_count(r3));

  uint8_t buf2[64], buf3[64];
  rope_to_str(r2, buf2, sizeof(buf2));
  rope_to_str(r3, buf3, sizeof(buf3));
  ASSERT(memcmp(buf2, buf3, rope_byte_count(r2)) == 0);
  ASSERT(memcmp(buf2, "hello", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_transient_replace(void) {
  printf("Running test_transient_replace... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope_transient t = rope_to_transient(r);
  rope_transient_replace(&t, 6, 5, (const uint8_t*)"there!", 6); /* "world" -> "there!" */
  rope r2 = rope_transient_to_persistent(&t);

  /* Should match rope_replace at same offset */
  rope r3 = rope_replace(r, 6, 5, (const uint8_t*)"there!", 6);
  ASSERT_INT_EQ(rope_byte_count(r2), rope_byte_count(r3));

  uint8_t buf2[64], buf3[64];
  rope_to_str(r2, buf2, sizeof(buf2));
  rope_to_str(r3, buf3, sizeof(buf3));
  ASSERT(memcmp(buf2, buf3, rope_byte_count(r2)) == 0);
  ASSERT(memcmp(buf2, "hello there!", 12) == 0);

  rope_unref(r);
  rope_unref(r2);
  rope_unref(r3);
  TEST_PASS();
}

int test_transient_interleaved(void) {
  printf("Running test_transient_interleaved... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"abcdefghij", 10);
  rope_transient t = rope_to_transient(r);

  /* Insert "X" at offset 3 -> "abcXdefghij" (11 bytes) */
  rope_transient_insert(&t, 3, (const uint8_t*)"X", 1);

  /* Delete 2 bytes at offset 5 -> "abcXdghij" (9 bytes) */
  rope_transient_delete(&t, 5, 2);

  /* Insert "YZ" at offset 0 -> "YZabcXdghij" (11 bytes) */
  rope_transient_insert(&t, 0, (const uint8_t*)"YZ", 2);

  rope r2 = rope_transient_to_persistent(&t);

  ASSERT_INT_EQ(rope_byte_count(r2), 11);
  uint8_t buf[64];
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "YZabcXdghij", 11) == 0);

  /* Verify all dimensions */
  ASSERT_INT_EQ(rope_char_count(r2), 11);
  ASSERT_INT_EQ(rope_line_count(r2), 0);
  ASSERT_INT_EQ(rope_grapheme_count(r2), 11);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

int test_transient_perf(void) {
  printf("Running test_transient_perf... ");
  tracker_reset();

  /* Build a rope large enough to have internal structure */
  size_t base_len = 2048;
  uint8_t* base = (uint8_t*)malloc(base_len);
  for (size_t i = 0; i < base_len; i++) base[i] = 'a' + (uint8_t)(i % 26);

  /* --- Measure persistent path: 100 sequential inserts --- */
  rope rp = rope_from_str(base, base_len);
  size_t seq_before_persistent = allocation_seq;

  rope current = rope_ref(rp);
  for (int i = 0; i < 100; i++) {
    rope next = rope_insert(current, 100, (const uint8_t*)"x", 1);
    rope_unref(current);
    current = next;
  }

  size_t persistent_allocs = allocation_seq - seq_before_persistent;

  /* Verify persistent result */
  ASSERT_INT_EQ(rope_byte_count(current), base_len + 100);
  rope_unref(current);
  rope_unref(rp);

  /* --- Measure transient path: 100 sequential inserts --- */
  rope rt = rope_from_str(base, base_len);
  size_t seq_before_transient = allocation_seq;

  rope_transient t = rope_to_transient(rt);
  for (int i = 0; i < 100; i++) {
    rope_transient_insert(&t, 100, (const uint8_t*)"x", 1);
  }
  rope result = rope_transient_to_persistent(&t);

  size_t transient_allocs = allocation_seq - seq_before_transient;

  /* Verify transient result */
  ASSERT_INT_EQ(rope_byte_count(result), base_len + 100);

  /* Transient should use fewer allocations */
  ASSERT(transient_allocs < persistent_allocs);

  rope_unref(rt);
  rope_unref(result);
  free(base);
  TEST_PASS();
}

int test_transient_delete_persistence(void) {
  printf("Running test_transient_delete_persistence... ");
  tracker_reset();

  rope r = rope_from_str((const uint8_t*)"hello world", 11);
  rope_transient t = rope_to_transient(r);
  rope_transient_delete(&t, 5, 6);
  rope_transient_replace(&t, 0, 2, (const uint8_t*)"HE", 2);
  rope r2 = rope_transient_to_persistent(&t);

  /* Original rope unchanged */
  ASSERT_INT_EQ(rope_byte_count(r), 11);
  uint8_t buf[64];
  rope_to_str(r, buf, sizeof(buf));
  ASSERT(memcmp(buf, "hello world", 11) == 0);

  /* New rope has the edits */
  ASSERT_INT_EQ(rope_byte_count(r2), 5);
  rope_to_str(r2, buf, sizeof(buf));
  ASSERT(memcmp(buf, "HEllo", 5) == 0);

  rope_unref(r);
  rope_unref(r2);
  TEST_PASS();
}

/* === Main === */

int main(void) {
  int pass = 0, fail = 0;
  int total = 85;
  typedef int (*test_fn)(void);
  test_fn tests[] = {
    test_empty_rope,
    test_rope_ref_unref,
    test_rope_compiles_alongside_sum_tree,
    test_from_str_ascii_roundtrip,
    test_from_str_multibyte_roundtrip,
    test_from_str_summary_dimensions,
    test_from_str_large_roundtrip,
    test_from_str_empty,
    test_from_str_invalid_utf8,
    /* ASCII fast-path */
    test_ascii_fastpath_word_loop_only,
    test_ascii_fastpath_short_tail_only,
    test_ascii_fastpath_lf_counts,
    test_ascii_fastpath_crlf_within_word,
    test_ascii_fastpath_crlf_across_word_boundary,
    test_ascii_fastpath_falls_back_on_high_bit_in_word,
    test_ascii_fastpath_falls_back_on_high_bit_in_tail,
    test_ascii_fastpath_empty,
    test_ascii_fastpath_lone_cr_not_counted_as_crlf,
    /* US-007 */
    test_insert_beginning,
    test_insert_middle,
    test_insert_end,
    test_delete_beginning,
    test_delete_middle,
    test_delete_end,
    test_replace_range,
    test_insert_summary_dimensions,
    test_delete_summary_dimensions,
    test_persistence_after_mutation,
    test_insert_empty_string,
    test_delete_zero_bytes,
    test_replace_different_length,
    test_insert_multibyte_summary,
    /* US-008 */
    test_byte_char_ascii,
    test_byte_char_multibyte,
    test_line_to_byte,
    test_byte_to_line,
    test_grapheme_conversions_combining,
    test_seek_out_of_bounds,
    /* US-009 */
    test_slice_beginning,
    test_slice_middle,
    test_slice_end,
    test_slice_entire_rope,
    test_slice_multibyte_boundaries,
    test_slice_to_str_copies,
    test_line_slice_extracts_lines,
    test_slice_empty_range,
    /* US-011 */
    test_large_string_256_leaves,
    /* US-012 */
    test_insert_at_char_ascii,
    test_insert_at_char_multibyte,
    test_delete_at_char_multibyte,
    test_delete_at_line,
    test_slice_by_chars,
    test_slice_by_graphemes_combining,
    test_insert_at_char_out_of_bounds,
    /* US-013 */
    test_cursor_advance_chars_ascii,
    test_cursor_advance_chars_multibyte,
    test_cursor_advance_lines,
    test_cursor_advance_past_end,
    test_cursor_peek,
    test_cursor_read,
    /* US-014 */
    test_cursor_retreat_bytes,
    test_cursor_retreat_chars_multibyte,
    test_cursor_retreat_lines,
    test_cursor_retreat_past_beginning,
    test_cursor_advance_then_retreat,
    /* US-015 */
    test_cursor_insert_matches_rope_insert,
    test_cursor_delete_matches_rope_delete,
    test_cursor_replace_matches_rope_replace,
    test_cursor_edit_multibyte,
    test_cursor_invalidation,
    /* US-016 */
    test_seek_line0_col0,
    test_seek_ascii_line_col,
    test_seek_chars_multibyte,
    test_seek_graphemes_combining,
    test_seek_nonexistent_line,
    test_seek_column_clamping,
    /* US-017 */
    test_transient_no_edits,
    test_transient_insert,
    test_transient_two_inserts,
    test_transient_persistence,
    /* US-018 */
    test_transient_delete,
    test_transient_replace,
    test_transient_interleaved,
    test_transient_perf,
    test_transient_delete_persistence,
  };

  for (int i = 0; i < total; i++) {
    if (tests[i]()) pass++;
    else fail++;
  }

  printf("\n%d/%d tests passed\n", pass, total);
  return fail > 0 ? 1 : 0;
}
