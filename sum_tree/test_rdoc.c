#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

/* Override allocator before including headers that instantiate sum_tree */
#define STREE_ALLOCATOR                 \
  {                                     \
      .alloc = tracked_malloc_with_ctx, \
      .free  = tracked_free_with_ctx,   \
  }

/* Include rope.h to verify rdoc and rope coexist in the same translation unit */
#include "rope.h"
#include "rdoc.h"

/* === US-021: Sentinel encoding/decoding tests === */

int test_encode_decode_block_open(void) {
  printf("Running test_encode_decode_block_open... ");
  uint8_t buf[8];
  uint32_t id = 0xDEADBEEF;

  size_t written = rdoc_encode_block_open(buf, id);
  ASSERT_INT_EQ(written, 5);
  ASSERT(buf[0] == RDOC_SENTINEL_BLOCK_OPEN);

  uint32_t decoded_id = 0;
  size_t consumed = rdoc_decode_block_open(buf, &decoded_id);
  ASSERT_INT_EQ(consumed, 5);
  ASSERT(decoded_id == id);

  TEST_PASS();
}

int test_encode_decode_block_close(void) {
  printf("Running test_encode_decode_block_close... ");
  uint8_t buf[8];
  uint32_t id = 0x12345678;

  size_t written = rdoc_encode_block_close(buf, id);
  ASSERT_INT_EQ(written, 5);
  ASSERT(buf[0] == RDOC_SENTINEL_BLOCK_CLOSE);

  uint32_t decoded_id = 0;
  size_t consumed = rdoc_decode_block_close(buf, &decoded_id);
  ASSERT_INT_EQ(consumed, 5);
  ASSERT(decoded_id == id);

  TEST_PASS();
}

int test_encode_decode_inline_open(void) {
  printf("Running test_encode_decode_inline_open... ");
  uint8_t buf[8];
  uint32_t id = 0x00000001;

  size_t written = rdoc_encode_inline_open(buf, id);
  ASSERT_INT_EQ(written, 5);
  ASSERT(buf[0] == RDOC_SENTINEL_INLINE_OPEN);

  uint32_t decoded_id = 0;
  size_t consumed = rdoc_decode_inline_open(buf, &decoded_id);
  ASSERT_INT_EQ(consumed, 5);
  ASSERT(decoded_id == id);

  TEST_PASS();
}

int test_encode_decode_inline_close(void) {
  printf("Running test_encode_decode_inline_close... ");
  uint8_t buf[8];
  uint32_t id = 0xFFFFFFFF;

  size_t written = rdoc_encode_inline_close(buf, id);
  ASSERT_INT_EQ(written, 5);
  ASSERT(buf[0] == RDOC_SENTINEL_INLINE_CLOSE);

  uint32_t decoded_id = 0;
  size_t consumed = rdoc_decode_inline_close(buf, &decoded_id);
  ASSERT_INT_EQ(consumed, 5);
  ASSERT(decoded_id == id);

  TEST_PASS();
}

int test_encode_decode_mark_change(void) {
  printf("Running test_encode_decode_mark_change... ");
  uint8_t buf[8];
  uint16_t flags = 0x0003;     /* e.g. bold + italic */
  uint32_t app_id = 0xCAFEBABE;

  size_t written = rdoc_encode_mark_change(buf, flags, app_id);
  ASSERT_INT_EQ(written, 7);
  ASSERT(buf[0] == RDOC_SENTINEL_MARK_CHANGE);

  uint16_t decoded_flags = 0;
  uint32_t decoded_app_id = 0;
  size_t consumed = rdoc_decode_mark_change(buf, &decoded_flags, &decoded_app_id);
  ASSERT_INT_EQ(consumed, 7);
  ASSERT(decoded_flags == flags);
  ASSERT(decoded_app_id == app_id);

  TEST_PASS();
}

int test_is_sentinel(void) {
  printf("Running test_is_sentinel... ");

  /* 0xFB-0xFF should be sentinels */
  ASSERT(rdoc_is_sentinel(0xFB));
  ASSERT(rdoc_is_sentinel(0xFC));
  ASSERT(rdoc_is_sentinel(0xFD));
  ASSERT(rdoc_is_sentinel(0xFE));
  ASSERT(rdoc_is_sentinel(0xFF));

  /* 0x00-0xFA should not be sentinels */
  ASSERT(!rdoc_is_sentinel(0x00));
  ASSERT(!rdoc_is_sentinel(0x41));  /* 'A' */
  ASSERT(!rdoc_is_sentinel(0x7F));
  ASSERT(!rdoc_is_sentinel(0x80));  /* UTF-8 continuation */
  ASSERT(!rdoc_is_sentinel(0xC0));  /* UTF-8 2-byte lead */
  ASSERT(!rdoc_is_sentinel(0xE0));  /* UTF-8 3-byte lead */
  ASSERT(!rdoc_is_sentinel(0xF0));  /* UTF-8 4-byte lead */
  ASSERT(!rdoc_is_sentinel(0xFA));

  TEST_PASS();
}

int test_sentinel_size(void) {
  printf("Running test_sentinel_size... ");

  ASSERT_INT_EQ(rdoc_sentinel_size(RDOC_SENTINEL_BLOCK_OPEN), 5);
  ASSERT_INT_EQ(rdoc_sentinel_size(RDOC_SENTINEL_BLOCK_CLOSE), 5);
  ASSERT_INT_EQ(rdoc_sentinel_size(RDOC_SENTINEL_INLINE_OPEN), 5);
  ASSERT_INT_EQ(rdoc_sentinel_size(RDOC_SENTINEL_INLINE_CLOSE), 5);
  ASSERT_INT_EQ(rdoc_sentinel_size(RDOC_SENTINEL_MARK_CHANGE), 7);
  ASSERT_INT_EQ(rdoc_sentinel_size(0x00), 0);
  ASSERT_INT_EQ(rdoc_sentinel_size(0x41), 0);
  ASSERT_INT_EQ(rdoc_sentinel_size(0xFA), 0);

  TEST_PASS();
}

int test_find_safe_split_inside_sentinel(void) {
  printf("Running test_find_safe_split_inside_sentinel... ");

  /* Buffer: "AB" + BLOCK_OPEN(id=1) + "CD"
     = 'A' 'B' 0xFF 0x01 0x00 0x00 0x00 'C' 'D' */
  uint8_t buf[9];
  buf[0] = 'A';
  buf[1] = 'B';
  rdoc_encode_block_open(buf + 2, 1);
  buf[7] = 'C';
  buf[8] = 'D';

  /* Splitting at pos=4 lands inside the sentinel payload (byte 2 of id) */
  size_t safe = rdoc_find_safe_split(buf, 9, 4);
  ASSERT_INT_EQ(safe, 2);  /* Should move before sentinel lead */

  /* Splitting at pos=2 is exactly at sentinel lead — that's safe */
  safe = rdoc_find_safe_split(buf, 9, 2);
  ASSERT_INT_EQ(safe, 2);

  /* Splitting at pos=7 is right after sentinel — safe */
  safe = rdoc_find_safe_split(buf, 9, 7);
  ASSERT_INT_EQ(safe, 7);

  TEST_PASS();
}

int test_find_safe_split_inside_utf8(void) {
  printf("Running test_find_safe_split_inside_utf8... ");

  /* Buffer: "A" + U+00E9 (é = 0xC3 0xA9) + "B"
     = 'A' 0xC3 0xA9 'B' */
  uint8_t buf[4] = {0x41, 0xC3, 0xA9, 0x42};

  /* Splitting at pos=2 lands on continuation byte 0xA9 */
  size_t safe = rdoc_find_safe_split(buf, 4, 2);
  ASSERT_INT_EQ(safe, 1);  /* Should move to lead byte */

  /* Splitting at pos=1 is on lead byte — safe */
  safe = rdoc_find_safe_split(buf, 4, 1);
  ASSERT_INT_EQ(safe, 1);

  TEST_PASS();
}

int test_find_safe_split_already_safe(void) {
  printf("Running test_find_safe_split_already_safe... ");

  /* Pure ASCII buffer */
  uint8_t buf[5] = {'H', 'e', 'l', 'l', 'o'};

  /* Any position is safe in ASCII */
  ASSERT_INT_EQ(rdoc_find_safe_split(buf, 5, 0), 0);
  ASSERT_INT_EQ(rdoc_find_safe_split(buf, 5, 2), 2);
  ASSERT_INT_EQ(rdoc_find_safe_split(buf, 5, 5), 5);

  TEST_PASS();
}

int test_find_safe_split_mark_change(void) {
  printf("Running test_find_safe_split_mark_change... ");

  /* Buffer: "X" + MARK_CHANGE(flags=3, app_id=42) + "Y"
     = 'X' 0xFB 0x03 0x00 0x2A 0x00 0x00 0x00 'Y' */
  uint8_t buf[9];
  buf[0] = 'X';
  rdoc_encode_mark_change(buf + 1, 3, 42);
  buf[8] = 'Y';

  /* Splitting at pos=5 lands inside mark sentinel payload */
  size_t safe = rdoc_find_safe_split(buf, 9, 5);
  ASSERT_INT_EQ(safe, 1);  /* Should move before sentinel lead */

  /* Splitting at pos=1 is at sentinel lead — safe */
  safe = rdoc_find_safe_split(buf, 9, 1);
  ASSERT_INT_EQ(safe, 1);

  /* Splitting at pos=8 is after sentinel — safe */
  safe = rdoc_find_safe_split(buf, 9, 8);
  ASSERT_INT_EQ(safe, 8);

  TEST_PASS();
}

int test_decode_wrong_lead_byte(void) {
  printf("Running test_decode_wrong_lead_byte... ");

  uint8_t buf[8];
  rdoc_encode_block_open(buf, 42);

  /* Try decoding as wrong type — should return 0 */
  uint32_t id;
  ASSERT_INT_EQ(rdoc_decode_block_close(buf, &id), 0);
  ASSERT_INT_EQ(rdoc_decode_inline_open(buf, &id), 0);
  ASSERT_INT_EQ(rdoc_decode_inline_close(buf, &id), 0);

  uint16_t flags;
  uint32_t app_id;
  ASSERT_INT_EQ(rdoc_decode_mark_change(buf, &flags, &app_id), 0);

  TEST_PASS();
}

int test_encode_decode_zero_id(void) {
  printf("Running test_encode_decode_zero_id... ");

  uint8_t buf[8];
  uint32_t id = 0;

  rdoc_encode_block_open(buf, id);
  uint32_t decoded = 0xFFFFFFFF;
  rdoc_decode_block_open(buf, &decoded);
  ASSERT(decoded == 0);

  rdoc_encode_inline_close(buf, id);
  decoded = 0xFFFFFFFF;
  rdoc_decode_inline_close(buf, &decoded);
  ASSERT(decoded == 0);

  TEST_PASS();
}

int test_encode_decode_mark_zero_flags(void) {
  printf("Running test_encode_decode_mark_zero_flags... ");

  uint8_t buf[8];
  rdoc_encode_mark_change(buf, 0, 0);

  uint16_t flags = 0xFFFF;
  uint32_t app_id = 0xFFFFFFFF;
  rdoc_decode_mark_change(buf, &flags, &app_id);
  ASSERT(flags == 0);
  ASSERT(app_id == 0);

  TEST_PASS();
}

/* === US-022: rdoc_summary monoid tests === */

static bool rdoc_summary_eq(rdoc_summary a, rdoc_summary b) {
  return a.text_bytes == b.text_bytes &&
         a.chars == b.chars &&
         a.lines == b.lines &&
         a.block_opens == b.block_opens &&
         a.inline_opens == b.inline_opens &&
         a.block_depth_delta == b.block_depth_delta &&
         a.block_max_depth == b.block_max_depth &&
         a.block_min_depth == b.block_min_depth &&
         a.inline_depth_delta == b.inline_depth_delta &&
         a.inline_max_depth == b.inline_max_depth &&
         a.inline_min_depth == b.inline_min_depth &&
         a.last_mark_flags == b.last_mark_flags &&
         a.last_mark_id == b.last_mark_id &&
         a.has_mark_change == b.has_mark_change;
}

int test_summary_identity(void) {
  printf("Running test_summary_identity... ");

  rdoc_summary id = rdoc_summary_identity();

  /* Build a non-trivial summary: block open + text "Hi\n" + mark change */
  uint8_t buf[32];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  memcpy(buf + len, "Hi\n", 3);
  len += 3;
  len += rdoc_encode_mark_change(buf + len, 0x0001, 42);

  rdoc_summary x = rdoc_summary_summarize(buf, len);

  /* combine(identity, x) == x */
  rdoc_summary left = rdoc_summary_combine(id, x);
  ASSERT(rdoc_summary_eq(left, x));

  /* combine(x, identity) == x */
  rdoc_summary right = rdoc_summary_combine(x, id);
  ASSERT(rdoc_summary_eq(right, x));

  TEST_PASS();
}

int test_summary_combine_associative(void) {
  printf("Running test_summary_combine_associative... ");

  /* a: BLOCK_OPEN + "abc" */
  uint8_t buf_a[16];
  size_t len_a = 0;
  len_a += rdoc_encode_block_open(buf_a + len_a, 1);
  memcpy(buf_a + len_a, "abc", 3);
  len_a += 3;
  rdoc_summary a = rdoc_summary_summarize(buf_a, len_a);

  /* b: BLOCK_CLOSE + MARK_CHANGE(flags=3, id=99) + BLOCK_OPEN */
  uint8_t buf_b[32];
  size_t len_b = 0;
  len_b += rdoc_encode_block_close(buf_b + len_b, 1);
  len_b += rdoc_encode_mark_change(buf_b + len_b, 3, 99);
  len_b += rdoc_encode_block_open(buf_b + len_b, 2);
  rdoc_summary b = rdoc_summary_summarize(buf_b, len_b);

  /* c: "x\ny" + INLINE_OPEN + INLINE_CLOSE + BLOCK_CLOSE */
  uint8_t buf_c[32];
  size_t len_c = 0;
  memcpy(buf_c + len_c, "x\ny", 3);
  len_c += 3;
  len_c += rdoc_encode_inline_open(buf_c + len_c, 10);
  len_c += rdoc_encode_inline_close(buf_c + len_c, 10);
  len_c += rdoc_encode_block_close(buf_c + len_c, 2);
  rdoc_summary c = rdoc_summary_summarize(buf_c, len_c);

  /* combine(combine(a, b), c) == combine(a, combine(b, c)) */
  rdoc_summary ab = rdoc_summary_combine(a, b);
  rdoc_summary ab_c = rdoc_summary_combine(ab, c);
  rdoc_summary bc = rdoc_summary_combine(b, c);
  rdoc_summary a_bc = rdoc_summary_combine(a, bc);

  ASSERT(rdoc_summary_eq(ab_c, a_bc));

  TEST_PASS();
}

int test_summary_summarize_mixed(void) {
  printf("Running test_summary_summarize_mixed... ");

  /* Build: BLOCK_OPEN(1) "Hello\n" MARK_CHANGE(flags=1,id=7) "world"
            INLINE_OPEN(5) "!" INLINE_CLOSE(5) BLOCK_CLOSE(1) */
  uint8_t buf[64];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);          /* 5 bytes */
  memcpy(buf + len, "Hello\n", 6); len += 6;            /* 6 text bytes */
  len += rdoc_encode_mark_change(buf + len, 1, 7);      /* 7 bytes */
  memcpy(buf + len, "world", 5); len += 5;              /* 5 text bytes */
  len += rdoc_encode_inline_open(buf + len, 5);          /* 5 bytes */
  buf[len++] = '!';                                      /* 1 text byte */
  len += rdoc_encode_inline_close(buf + len, 5);         /* 5 bytes */
  len += rdoc_encode_block_close(buf + len, 1);          /* 5 bytes */

  rdoc_summary s = rdoc_summary_summarize(buf, len);

  /* Text: "Hello\n" + "world" + "!" = 12 bytes, 12 chars (ASCII), 1 newline */
  ASSERT_INT_EQ(s.text_bytes, 12);
  ASSERT_INT_EQ(s.chars, 12);
  ASSERT_INT_EQ(s.lines, 1);

  /* Block: 1 open, net delta = 0 (open then close), max = 1, min = 0 */
  ASSERT_INT_EQ(s.block_opens, 1);
  ASSERT_INT_EQ(s.block_depth_delta, 0);
  ASSERT_INT_EQ(s.block_max_depth, 1);
  ASSERT_INT_EQ(s.block_min_depth, 0);

  /* Inline: 1 open, net delta = 0, max = 1, min = 0 */
  ASSERT_INT_EQ(s.inline_opens, 1);
  ASSERT_INT_EQ(s.inline_depth_delta, 0);
  ASSERT_INT_EQ(s.inline_max_depth, 1);
  ASSERT_INT_EQ(s.inline_min_depth, 0);

  /* Mark: has mark change, last flags=1, last id=7 */
  ASSERT(s.has_mark_change);
  ASSERT(s.last_mark_flags == 1);
  ASSERT(s.last_mark_id == 7);

  TEST_PASS();
}

int test_summary_summarize_pure_text(void) {
  printf("Running test_summary_summarize_pure_text... ");

  /* Pure UTF-8 text: "café\nlatte" — multi-byte é (0xC3 0xA9) */
  const uint8_t text[] = {'c', 'a', 'f', 0xC3, 0xA9, '\n', 'l', 'a', 't', 't', 'e'};
  size_t len = sizeof(text);

  rdoc_summary s = rdoc_summary_summarize(text, len);

  /* text_bytes == len */
  ASSERT_INT_EQ(s.text_bytes, len);

  /* chars == utf8_codepoint_count */
  ASSERT_INT_EQ(s.chars, utf8_codepoint_count(text, len));
  ASSERT_INT_EQ(s.chars, 10); /* c-a-f-é-\n-l-a-t-t-e */

  /* lines == utf8_line_count */
  ASSERT_INT_EQ(s.lines, utf8_line_count(text, len));
  ASSERT_INT_EQ(s.lines, 1);

  /* All structural fields zero */
  ASSERT_INT_EQ(s.block_opens, 0);
  ASSERT_INT_EQ(s.inline_opens, 0);
  ASSERT_INT_EQ(s.block_depth_delta, 0);
  ASSERT_INT_EQ(s.block_max_depth, 0);
  ASSERT_INT_EQ(s.block_min_depth, 0);
  ASSERT_INT_EQ(s.inline_depth_delta, 0);
  ASSERT_INT_EQ(s.inline_max_depth, 0);
  ASSERT_INT_EQ(s.inline_min_depth, 0);
  ASSERT(!s.has_mark_change);
  ASSERT(s.last_mark_flags == 0);
  ASSERT(s.last_mark_id == 0);

  TEST_PASS();
}

int test_summary_summarize_sentinels_only(void) {
  printf("Running test_summary_summarize_sentinels_only... ");

  /* Buffer with only sentinels: BLOCK_OPEN(1) BLOCK_OPEN(2) INLINE_OPEN(3)
     INLINE_CLOSE(3) BLOCK_CLOSE(2) MARK_CHANGE(flags=5,id=77) BLOCK_CLOSE(1) */
  uint8_t buf[64];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  len += rdoc_encode_block_open(buf + len, 2);
  len += rdoc_encode_inline_open(buf + len, 3);
  len += rdoc_encode_inline_close(buf + len, 3);
  len += rdoc_encode_block_close(buf + len, 2);
  len += rdoc_encode_mark_change(buf + len, 5, 77);
  len += rdoc_encode_block_close(buf + len, 1);

  rdoc_summary s = rdoc_summary_summarize(buf, len);

  /* No text */
  ASSERT_INT_EQ(s.text_bytes, 0);
  ASSERT_INT_EQ(s.chars, 0);
  ASSERT_INT_EQ(s.lines, 0);

  /* Blocks: 2 opens, depth goes 0→1→2→2→1→1→0, delta=0, max=2, min=0 */
  ASSERT_INT_EQ(s.block_opens, 2);
  ASSERT_INT_EQ(s.block_depth_delta, 0);
  ASSERT_INT_EQ(s.block_max_depth, 2);
  ASSERT_INT_EQ(s.block_min_depth, 0);

  /* Inline: 1 open, depth goes 0→1→0, delta=0, max=1, min=0 */
  ASSERT_INT_EQ(s.inline_opens, 1);
  ASSERT_INT_EQ(s.inline_depth_delta, 0);
  ASSERT_INT_EQ(s.inline_max_depth, 1);
  ASSERT_INT_EQ(s.inline_min_depth, 0);

  /* Mark: has change, flags=5, id=77 */
  ASSERT(s.has_mark_change);
  ASSERT(s.last_mark_flags == 5);
  ASSERT(s.last_mark_id == 77);

  TEST_PASS();
}

/* === US-023: rdoc document type, sum_tree instantiation, and lifecycle === */

int test_empty_rdoc(void) {
  printf("Running test_empty_rdoc... ");
  tracker_reset();

  rdoc d = rdoc_new();
  ASSERT_INT_EQ(rdoc_total_bytes(d), 0);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 0);
  ASSERT_INT_EQ(rdoc_char_count(d), 0);
  ASSERT_INT_EQ(rdoc_line_count(d), 0);
  ASSERT_INT_EQ(rdoc_block_count(d), 0);
  ASSERT_INT_EQ(rdoc_inline_count(d), 0);
  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_rdoc_from_bytes_valid(void) {
  printf("Running test_rdoc_from_bytes_valid... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello\n" MARK_CHANGE(flags=1,id=7) "world"
            INLINE_OPEN(5) "!" INLINE_CLOSE(5) BLOCK_CLOSE(1) */
  uint8_t buf[64];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);          /* 5 bytes */
  memcpy(buf + len, "Hello\n", 6); len += 6;            /* 6 text bytes */
  len += rdoc_encode_mark_change(buf + len, 1, 7);      /* 7 bytes */
  memcpy(buf + len, "world", 5); len += 5;              /* 5 text bytes */
  len += rdoc_encode_inline_open(buf + len, 5);          /* 5 bytes */
  buf[len++] = '!';                                      /* 1 text byte */
  len += rdoc_encode_inline_close(buf + len, 5);         /* 5 bytes */
  len += rdoc_encode_block_close(buf + len, 1);          /* 5 bytes */
  /* total: 5+6+7+5+5+1+5+5 = 39 raw bytes, 12 text bytes */

  rdoc d = rdoc_from_bytes(buf, len);

  ASSERT_INT_EQ(rdoc_total_bytes(d), 39);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 12);
  ASSERT_INT_EQ(rdoc_char_count(d), 12);
  ASSERT_INT_EQ(rdoc_line_count(d), 1);
  ASSERT_INT_EQ(rdoc_block_count(d), 1);
  ASSERT_INT_EQ(rdoc_inline_count(d), 1);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_rdoc_from_bytes_truncated(void) {
  printf("Running test_rdoc_from_bytes_truncated... ");
  tracker_reset();

  /* 0xFF followed by only 2 bytes instead of required 4 for BLOCK_OPEN */
  uint8_t buf[3] = {0xFF, 0x01, 0x00};
  rdoc d = rdoc_from_bytes(buf, 3);

  /* Should return empty rdoc */
  ASSERT_INT_EQ(rdoc_total_bytes(d), 0);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 0);
  ASSERT_INT_EQ(rdoc_block_count(d), 0);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_rdoc_coexists_with_rope(void) {
  printf("Running test_rdoc_coexists_with_rope... ");
  tracker_reset();

  /* Create a rope */
  rope r = rope_from_str((const uint8_t*)"Hello rope", 10);
  ASSERT_INT_EQ(rope_byte_count(r), 10);

  /* Create an rdoc */
  uint8_t buf[16];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  memcpy(buf + len, "Hi", 2); len += 2;
  len += rdoc_encode_block_close(buf + len, 1);

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 12);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 2);
  ASSERT_INT_EQ(rdoc_block_count(d), 1);

  /* Both coexist without interference */
  ASSERT_INT_EQ(rope_byte_count(r), 10);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 12);

  rope_unref(r);
  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-024: Builder API tests === */

int test_builder_complex_document(void) {
  printf("Running test_builder_complex_document... ");
  tracker_reset();

  /* Build: two paragraphs, bold text in first, inline block in second
     Para 1: BLOCK_OPEN(1) MARK_CHANGE(bold=1,id=0) "Hello" MARK_CHANGE(0,0) " world" BLOCK_CLOSE(1)
     Para 2: BLOCK_OPEN(2) "Check " INLINE_OPEN(10) "this" INLINE_CLOSE(10) " out" BLOCK_CLOSE(2) */
  rdoc_builder b = rdoc_builder_new();

  /* Paragraph 1 */
  rdoc_builder_block_open(&b, 1);
  rdoc_builder_mark(&b, 0x0001, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_mark(&b, 0x0000, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)" world", 6));
  rdoc_builder_block_close(&b, 1);

  /* Paragraph 2 */
  rdoc_builder_block_open(&b, 2);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Check ", 6));
  rdoc_builder_inline_open(&b, 10);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"this", 4));
  rdoc_builder_inline_close(&b, 10);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)" out", 4));
  rdoc_builder_block_close(&b, 2);

  rdoc d = rdoc_builder_finish(&b);

  /* Verify summary counts:
     Text: "Hello" + " world" + "Check " + "this" + " out" = 5+6+6+4+4 = 25 bytes
     Chars: 25 (all ASCII)
     Lines: 0 (no newlines)
     Blocks: 2 (para 1 + para 2)
     Inlines: 1
     Mark changes: 2 (bold on, bold off) — last flags=0, last id=0 */
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 25);
  ASSERT_INT_EQ(rdoc_char_count(d), 25);
  ASSERT_INT_EQ(rdoc_line_count(d), 0);
  ASSERT_INT_EQ(rdoc_block_count(d), 2);
  ASSERT_INT_EQ(rdoc_inline_count(d), 1);

  /* Total raw bytes: 2*(5+5) block sentinels + 2*7 mark sentinels + (5+5) inline sentinels + 25 text
     = 20 + 14 + 10 + 25 = 69 */
  ASSERT_INT_EQ(rdoc_total_bytes(d), 69);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_builder_empty(void) {
  printf("Running test_builder_empty... ");
  tracker_reset();

  rdoc_builder b = rdoc_builder_new();
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_total_bytes(d), 0);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 0);
  ASSERT_INT_EQ(rdoc_char_count(d), 0);
  ASSERT_INT_EQ(rdoc_line_count(d), 0);
  ASSERT_INT_EQ(rdoc_block_count(d), 0);
  ASSERT_INT_EQ(rdoc_inline_count(d), 0);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_builder_invalid_utf8(void) {
  printf("Running test_builder_invalid_utf8... ");
  tracker_reset();

  rdoc_builder b = rdoc_builder_new();

  /* Valid text first */
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"ok", 2));
  size_t len_before = b.len;

  /* Invalid UTF-8: truncated 2-byte sequence */
  uint8_t bad[] = {0xC3};
  ASSERT(!rdoc_builder_text(&b, bad, 1));

  /* Builder state unchanged after rejection */
  ASSERT_INT_EQ(b.len, len_before);

  /* Can still finish and get valid rdoc with just "ok" */
  rdoc d = rdoc_builder_finish(&b);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 2);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 2);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-025: Dimensional seeking, position queries, and custom leaf search === */

int test_seek_text_byte_with_sentinels(void) {
  printf("Running test_seek_text_byte_with_sentinels... ");
  tracker_reset();

  /* BLOCK_OPEN(1) "Hello" BLOCK_CLOSE(1)
     Raw: [0xFF,1,0,0,0] [H,e,l,l,o] [0xFE,1,0,0,0] = 15 bytes
     Text bytes: H=offset0 e=1 l=2 l=3 o=4, at raw positions 5,6,7,8,9 */
  uint8_t buf[15];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  memcpy(buf + len, "Hello", 5); len += 5;
  len += rdoc_encode_block_close(buf + len, 1);

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 15);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 5);

  /* Seek to text byte 0 → raw 5 (first text byte 'H') */
  rdoc_seek_result r = rdoc_text_byte_to_raw(d, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 5);

  /* Seek to text byte 2 → raw 7 ('l') */
  r = rdoc_text_byte_to_raw(d, 2);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 7);

  /* Seek to text byte 4 → raw 9 ('o') */
  r = rdoc_text_byte_to_raw(d, 4);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 9);

  /* Seek to text byte 5 → out of bounds */
  r = rdoc_text_byte_to_raw(d, 5);
  ASSERT(!r.found);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_seek_char_multibyte_utf8(void) {
  printf("Running test_seek_char_multibyte_utf8... ");
  tracker_reset();

  /* BLOCK_OPEN(1) "café" BLOCK_CLOSE(1)
     "café" = c(1B) a(1B) f(1B) é(2B: 0xC3 0xA9) = 5 text bytes, 4 chars
     Raw: [0xFF,1,0,0,0] [c,a,f,0xC3,0xA9] [0xFE,1,0,0,0] = 15 bytes */
  uint8_t buf[15];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  buf[len++] = 'c';
  buf[len++] = 'a';
  buf[len++] = 'f';
  buf[len++] = 0xC3;
  buf[len++] = 0xA9;
  len += rdoc_encode_block_close(buf + len, 1);

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 5);
  ASSERT_INT_EQ(rdoc_char_count(d), 4);

  /* Char 0 → raw 5 ('c') */
  rdoc_seek_result r = rdoc_char_to_raw(d, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 5);

  /* Char 2 → raw 7 ('f') */
  r = rdoc_char_to_raw(d, 2);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 7);

  /* Char 3 → raw 8 (start of 'é': 0xC3) */
  r = rdoc_char_to_raw(d, 3);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 8);

  /* Char 4 → out of bounds */
  r = rdoc_char_to_raw(d, 4);
  ASSERT(!r.found);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_seek_line_with_blocks(void) {
  printf("Running test_seek_line_with_blocks... ");
  tracker_reset();

  /* BLOCK_OPEN(1) "line1\n" BLOCK_CLOSE(1) BLOCK_OPEN(2) "line2\nline3" BLOCK_CLOSE(2)
     Raw layout:
       0-4:   BLOCK_OPEN(1)    [5]
       5-10:  "line1\n"        [6]
       11-15: BLOCK_CLOSE(1)   [5]
       16-20: BLOCK_OPEN(2)    [5]
       21-31: "line2\nline3"   [11]
       32-36: BLOCK_CLOSE(2)   [5]
     Total: 37 bytes, text: 17 bytes, 2 newlines */
  uint8_t buf[64];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  memcpy(buf + len, "line1\n", 6); len += 6;
  len += rdoc_encode_block_close(buf + len, 1);
  len += rdoc_encode_block_open(buf + len, 2);
  memcpy(buf + len, "line2\nline3", 11); len += 11;
  len += rdoc_encode_block_close(buf + len, 2);

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 37);
  ASSERT_INT_EQ(rdoc_line_count(d), 2);

  /* Line 0 → raw 0 (start of document) */
  rdoc_seek_result r = rdoc_line_to_raw(d, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 0);

  /* Line 1 → raw 11 (byte after '\n' at raw 10) */
  r = rdoc_line_to_raw(d, 1);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 11);

  /* Line 2 → raw 27 (byte after '\n' at raw 26) */
  r = rdoc_line_to_raw(d, 2);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 27);

  /* Line 3 → out of bounds (only 2 newlines) */
  r = rdoc_line_to_raw(d, 3);
  ASSERT(!r.found);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_seek_block_index(void) {
  printf("Running test_seek_block_index... ");
  tracker_reset();

  /* 5 nested blocks: BLOCK_OPEN(1)..BLOCK_OPEN(5) BLOCK_CLOSE(5)..BLOCK_CLOSE(1)
     Each sentinel = 5 bytes. Total = 50 bytes.
     Block opens at raw positions: 0, 5, 10, 15, 20 */
  uint8_t buf[64];
  size_t len = 0;
  for (uint32_t i = 1; i <= 5; i++)
    len += rdoc_encode_block_open(buf + len, i);
  for (uint32_t i = 5; i >= 1; i--)
    len += rdoc_encode_block_close(buf + len, i);

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 50);
  ASSERT_INT_EQ(rdoc_block_count(d), 5);

  /* Block 0 → raw 0 (1st block open) */
  rdoc_seek_result r = rdoc_block_to_raw(d, 0);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 0);

  /* Block 2 → raw 10 (3rd block open) */
  r = rdoc_block_to_raw(d, 2);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 10);

  /* Block 4 → raw 20 (5th block open) */
  r = rdoc_block_to_raw(d, 4);
  ASSERT(r.found);
  ASSERT_INT_EQ(r.raw_index, 20);

  /* Block 5 → out of bounds */
  r = rdoc_block_to_raw(d, 5);
  ASSERT(!r.found);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_block_depth_at(void) {
  printf("Running test_block_depth_at... ");
  tracker_reset();

  /* BLOCK_OPEN(1) "a" BLOCK_OPEN(2) "b" BLOCK_CLOSE(2) "c" BLOCK_CLOSE(1)
     Raw layout:
       0-4:   BLOCK_OPEN(1)    depth → 1
       5:     'a'              depth = 1
       6-10:  BLOCK_OPEN(2)    depth → 2
       11:    'b'              depth = 2
       12-16: BLOCK_CLOSE(2)   depth → 1
       17:    'c'              depth = 1
       18-22: BLOCK_CLOSE(1)   depth → 0 */
  uint8_t buf[32];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  buf[len++] = 'a';
  len += rdoc_encode_block_open(buf + len, 2);
  buf[len++] = 'b';
  len += rdoc_encode_block_close(buf + len, 2);
  buf[len++] = 'c';
  len += rdoc_encode_block_close(buf + len, 1);

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 23);

  /* Before any block: depth 0 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d, 0), 0);

  /* After BLOCK_OPEN(1): depth 1 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d, 5), 1);

  /* After BLOCK_OPEN(2): depth 2 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d, 11), 2);

  /* After BLOCK_CLOSE(2): depth 1 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d, 17), 1);

  /* After BLOCK_CLOSE(1): depth 0 (full summary) */
  ASSERT_INT_EQ(rdoc_block_depth_at(d, 23), 0);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_mark_at(void) {
  printf("Running test_mark_at... ");
  tracker_reset();

  /* "before" MARK_CHANGE(flags=1, app_id=42) "marked" MARK_CHANGE(flags=0, app_id=0) "after"
     Raw layout:
       0-5:   "before"              [6 bytes]
       6-12:  MARK_CHANGE(1, 42)    [7 bytes]
       13-18: "marked"              [6 bytes]
       19-25: MARK_CHANGE(0, 0)     [7 bytes]
       26-30: "after"               [5 bytes]
     Total: 31 bytes */
  uint8_t buf[32];
  size_t len = 0;
  memcpy(buf + len, "before", 6); len += 6;
  len += rdoc_encode_mark_change(buf + len, 1, 42);
  memcpy(buf + len, "marked", 6); len += 6;
  len += rdoc_encode_mark_change(buf + len, 0, 0);
  memcpy(buf + len, "after", 5); len += 5;

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 31);

  /* Before any mark: flags=0, app_id=0 */
  rdoc_mark_result m = rdoc_mark_at(d, 0);
  ASSERT(m.flags == 0);
  ASSERT(m.app_id == 0);

  /* Still before mark: position 3 */
  m = rdoc_mark_at(d, 3);
  ASSERT(m.flags == 0);
  ASSERT(m.app_id == 0);

  /* After first mark change: position 13 */
  m = rdoc_mark_at(d, 13);
  ASSERT(m.flags == 1);
  ASSERT(m.app_id == 42);

  /* After second mark change: position 26 */
  m = rdoc_mark_at(d, 26);
  ASSERT(m.flags == 0);
  ASSERT(m.app_id == 0);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-026: Token cursor for rendering iteration === */

int test_cursor_iterate_full_document(void) {
  printf("Running test_cursor_iterate_full_document... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello" MARK_CHANGE(bold=1,id=0) " world" BLOCK_CLOSE(1)
     Expected tokens: BLOCK_OPEN(1), TEXT("Hello"), MARK_CHANGE(1,0), TEXT(" world"), BLOCK_CLOSE(1) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_mark(&b, 0x0001, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)" world", 6));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  /* Collect all tokens */
  rdoc_token tokens[16];
  size_t n_tokens = 0;

  rdoc_cursor c = rdoc_cursor_new(d, 0);
  ASSERT(c.valid);
  do {
    tokens[n_tokens] = rdoc_cursor_token(&c);
    n_tokens++;
  } while (rdoc_cursor_advance(&c));

  ASSERT_INT_EQ(n_tokens, 5);

  /* Token 0: BLOCK_OPEN(1) */
  ASSERT(tokens[0].kind == RDOC_TOKEN_BLOCK_OPEN);
  ASSERT(tokens[0].id == 1);

  /* Token 1: TEXT("Hello") */
  ASSERT(tokens[1].kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tokens[1].text_len, 5);
  ASSERT(memcmp(tokens[1].text_ptr, "Hello", 5) == 0);

  /* Token 2: MARK_CHANGE(flags=1, app_id=0) */
  ASSERT(tokens[2].kind == RDOC_TOKEN_MARK_CHANGE);
  ASSERT(tokens[2].mark_flags == 1);
  ASSERT(tokens[2].id == 0);

  /* Token 3: TEXT(" world") */
  ASSERT(tokens[3].kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tokens[3].text_len, 6);
  ASSERT(memcmp(tokens[3].text_ptr, " world", 6) == 0);

  /* Token 4: BLOCK_CLOSE(1) */
  ASSERT(tokens[4].kind == RDOC_TOKEN_BLOCK_CLOSE);
  ASSERT(tokens[4].id == 1);

  rdoc_cursor_free(c);
  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_cursor_mark_state_tracking(void) {
  printf("Running test_cursor_mark_state_tracking... ");
  tracker_reset();

  /* "text" -> MARK_CHANGE(bold=1, id=42) -> "bold" -> MARK_CHANGE(0, 0) -> "plain" */
  rdoc_builder b = rdoc_builder_new();
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"text", 4));
  rdoc_builder_mark(&b, 0x0001, 42);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"bold", 4));
  rdoc_builder_mark(&b, 0x0000, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"plain", 5));
  rdoc d = rdoc_builder_finish(&b);

  rdoc_cursor c = rdoc_cursor_new(d, 0);
  ASSERT(c.valid);

  /* Token 0: TEXT("text") — mark flags should be 0 */
  rdoc_token tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_TEXT);
  ASSERT(rdoc_cursor_mark_flags(&c) == 0);
  ASSERT(rdoc_cursor_mark_id(&c) == 0);

  /* Advance past TEXT("text") */
  ASSERT(rdoc_cursor_advance(&c));

  /* Token 1: MARK_CHANGE(1, 42) — mark flags still 0 (haven't passed it yet) */
  tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_MARK_CHANGE);
  ASSERT(rdoc_cursor_mark_flags(&c) == 0);

  /* Advance past MARK_CHANGE — now mark state updates */
  ASSERT(rdoc_cursor_advance(&c));
  ASSERT(rdoc_cursor_mark_flags(&c) == 1);
  ASSERT(rdoc_cursor_mark_id(&c) == 42);

  /* Token 2: TEXT("bold") */
  tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_TEXT);

  /* Advance past TEXT("bold") */
  ASSERT(rdoc_cursor_advance(&c));

  /* Token 3: MARK_CHANGE(0, 0) */
  tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_MARK_CHANGE);
  ASSERT(rdoc_cursor_mark_flags(&c) == 1); /* still in bold state */

  /* Advance past MARK_CHANGE(0,0) */
  ASSERT(rdoc_cursor_advance(&c));
  ASSERT(rdoc_cursor_mark_flags(&c) == 0);
  ASSERT(rdoc_cursor_mark_id(&c) == 0);

  /* Token 4: TEXT("plain") */
  tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tok.text_len, 5);

  /* No more tokens */
  ASSERT(!rdoc_cursor_advance(&c));

  rdoc_cursor_free(c);
  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_cursor_complex_document(void) {
  printf("Running test_cursor_complex_document... ");
  tracker_reset();

  /* BLOCK_OPEN(1) "para1" BLOCK_CLOSE(1)
     BLOCK_OPEN(2) MARK_CHANGE(3,99) "styled" INLINE_OPEN(5) "inl" INLINE_CLOSE(5) "end" BLOCK_CLOSE(2) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"para1", 5));
  rdoc_builder_block_close(&b, 1);
  rdoc_builder_block_open(&b, 2);
  rdoc_builder_mark(&b, 3, 99);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"styled", 6));
  rdoc_builder_inline_open(&b, 5);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"inl", 3));
  rdoc_builder_inline_close(&b, 5);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"end", 3));
  rdoc_builder_block_close(&b, 2);
  rdoc d = rdoc_builder_finish(&b);

  /* Collect all tokens */
  rdoc_token tokens[16];
  size_t n_tokens = 0;
  rdoc_cursor c = rdoc_cursor_new(d, 0);
  ASSERT(c.valid);
  do {
    tokens[n_tokens] = rdoc_cursor_token(&c);
    n_tokens++;
  } while (rdoc_cursor_advance(&c));

  /* Expected: BLOCK_OPEN(1), TEXT("para1"), BLOCK_CLOSE(1),
               BLOCK_OPEN(2), MARK_CHANGE(3,99), TEXT("styled"),
               INLINE_OPEN(5), TEXT("inl"), INLINE_CLOSE(5), TEXT("end"), BLOCK_CLOSE(2) */
  ASSERT_INT_EQ(n_tokens, 11);

  ASSERT(tokens[0].kind == RDOC_TOKEN_BLOCK_OPEN);
  ASSERT(tokens[0].id == 1);
  ASSERT(tokens[1].kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tokens[1].text_len, 5);
  ASSERT(tokens[2].kind == RDOC_TOKEN_BLOCK_CLOSE);
  ASSERT(tokens[2].id == 1);
  ASSERT(tokens[3].kind == RDOC_TOKEN_BLOCK_OPEN);
  ASSERT(tokens[3].id == 2);
  ASSERT(tokens[4].kind == RDOC_TOKEN_MARK_CHANGE);
  ASSERT(tokens[4].mark_flags == 3);
  ASSERT(tokens[4].id == 99);
  ASSERT(tokens[5].kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tokens[5].text_len, 6);
  ASSERT(memcmp(tokens[5].text_ptr, "styled", 6) == 0);
  ASSERT(tokens[6].kind == RDOC_TOKEN_INLINE_OPEN);
  ASSERT(tokens[6].id == 5);
  ASSERT(tokens[7].kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tokens[7].text_len, 3);
  ASSERT(memcmp(tokens[7].text_ptr, "inl", 3) == 0);
  ASSERT(tokens[8].kind == RDOC_TOKEN_INLINE_CLOSE);
  ASSERT(tokens[8].id == 5);
  ASSERT(tokens[9].kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tokens[9].text_len, 3);
  ASSERT(memcmp(tokens[9].text_ptr, "end", 3) == 0);
  ASSERT(tokens[10].kind == RDOC_TOKEN_BLOCK_CLOSE);
  ASSERT(tokens[10].id == 2);

  rdoc_cursor_free(c);
  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_cursor_mid_document_start(void) {
  printf("Running test_cursor_mid_document_start... ");
  tracker_reset();

  /* BLOCK_OPEN(1) "Hello" MARK_CHANGE(1,42) " world" BLOCK_CLOSE(1)
     Raw layout:
       0-4:   BLOCK_OPEN(1)     [5]
       5-9:   "Hello"           [5]
       10-16: MARK_CHANGE(1,42) [7]
       17-22: " world"          [6]
       23-27: BLOCK_CLOSE(1)    [5]
     Total: 28 raw bytes */
  uint8_t buf[32];
  size_t len = 0;
  len += rdoc_encode_block_open(buf + len, 1);
  memcpy(buf + len, "Hello", 5); len += 5;
  len += rdoc_encode_mark_change(buf + len, 1, 42);
  memcpy(buf + len, " world", 6); len += 6;
  len += rdoc_encode_block_close(buf + len, 1);

  rdoc d = rdoc_from_bytes(buf, len);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 28);

  /* Start cursor at raw offset 10 (MARK_CHANGE sentinel) */
  rdoc_cursor c = rdoc_cursor_new(d, 10);
  ASSERT(c.valid);

  /* Mark state should be 0 (no mark changes before position 10) */
  ASSERT(rdoc_cursor_mark_flags(&c) == 0);

  /* Current token should be MARK_CHANGE */
  rdoc_token tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_MARK_CHANGE);
  ASSERT(tok.mark_flags == 1);
  ASSERT(tok.id == 42);

  /* Advance past MARK_CHANGE */
  ASSERT(rdoc_cursor_advance(&c));
  ASSERT(rdoc_cursor_mark_flags(&c) == 1);
  ASSERT(rdoc_cursor_mark_id(&c) == 42);

  /* Next: TEXT(" world") */
  tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tok.text_len, 6);

  /* Advance to BLOCK_CLOSE */
  ASSERT(rdoc_cursor_advance(&c));
  tok = rdoc_cursor_token(&c);
  ASSERT(tok.kind == RDOC_TOKEN_BLOCK_CLOSE);
  ASSERT(tok.id == 1);

  /* No more after BLOCK_CLOSE */
  ASSERT(!rdoc_cursor_advance(&c));

  /* Start cursor at raw offset 17 (start of " world" text) */
  rdoc_cursor c2 = rdoc_cursor_new(d, 17);
  ASSERT(c2.valid);

  /* Mark state should reflect the MARK_CHANGE at offset 10 */
  ASSERT(rdoc_cursor_mark_flags(&c2) == 1);
  ASSERT(rdoc_cursor_mark_id(&c2) == 42);

  /* Current token should be TEXT(" world") */
  tok = rdoc_cursor_token(&c2);
  ASSERT(tok.kind == RDOC_TOKEN_TEXT);
  ASSERT_INT_EQ(tok.text_len, 6);

  rdoc_cursor_free(c);
  rdoc_cursor_free(c2);
  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-027: Text editing (insert, delete, replace, and raw delete) === */

int test_insert_text_beginning_middle_end(void) {
  printf("Running test_insert_text_beginning_middle_end... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello" BLOCK_CLOSE(1) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_text_byte_count(d), 5);
  ASSERT_INT_EQ(rdoc_block_count(d), 1);

  /* Insert at beginning of text (text_byte_offset=0) */
  rdoc d1 = rdoc_insert_text(d, 0, (const uint8_t*)"Hi ", 3);
  ASSERT_INT_EQ(rdoc_text_byte_count(d1), 8);
  ASSERT_INT_EQ(rdoc_block_count(d1), 1);  /* blocks preserved */

  /* Insert at middle of text (text_byte_offset=5 in d1 = between "Hi " and "Hello") */
  rdoc d2 = rdoc_insert_text(d1, 5, (const uint8_t*)"XY", 2);
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 10);

  /* Insert at end of text */
  rdoc d3 = rdoc_insert_text(d, 5, (const uint8_t*)" End", 4);
  ASSERT_INT_EQ(rdoc_text_byte_count(d3), 9);
  ASSERT_INT_EQ(rdoc_block_count(d3), 1);

  /* Verify all summary counts on d3:
     Text: "Hello End" = 9 bytes, 9 chars, 0 lines
     Blocks: 1, Inlines: 0 */
  ASSERT_INT_EQ(rdoc_char_count(d3), 9);
  ASSERT_INT_EQ(rdoc_line_count(d3), 0);
  ASSERT_INT_EQ(rdoc_inline_count(d3), 0);

  rdoc_unref(d);
  rdoc_unref(d1);
  rdoc_unref(d2);
  rdoc_unref(d3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_insert_text_inside_marked_range(void) {
  printf("Running test_insert_text_inside_marked_range... ");
  tracker_reset();

  /* Build: "ab" MARK_CHANGE(bold=1, id=42) "cd" MARK_CHANGE(0, 0) "ef"
     Text: "abcdef" (6 text bytes)
     Marks: bold on [2,4), off at 4 */
  rdoc_builder b = rdoc_builder_new();
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"ab", 2));
  rdoc_builder_mark(&b, 0x0001, 42);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"cd", 2));
  rdoc_builder_mark(&b, 0x0000, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"ef", 2));
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_text_byte_count(d), 6);

  /* Insert "XX" at text_byte_offset=3 (between 'c' and 'd', inside bold range) */
  rdoc d2 = rdoc_insert_text(d, 3, (const uint8_t*)"XX", 2);
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 8);

  /* Verify mark context: text at offset 3 should still be in bold range.
     The inserted text inherits surrounding mark context (no extra marks inserted). */
  rdoc_seek_result sr = rdoc_text_byte_to_raw(d2, 3);
  ASSERT(sr.found);
  rdoc_mark_result m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 1);  /* still bold */
  ASSERT(m.app_id == 42);

  /* Text at offset 5 (original 'd' shifted right by 2) should still be bold */
  sr = rdoc_text_byte_to_raw(d2, 5);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 1);

  /* Text at offset 6 (original 'e' shifted right) should not be bold */
  sr = rdoc_text_byte_to_raw(d2, 6);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_delete_text_preserves_sentinel(void) {
  printf("Running test_delete_text_preserves_sentinel... ");
  tracker_reset();

  /* Build: "ab" MARK_CHANGE(bold=1, id=42) "cd" MARK_CHANGE(0, 0) "ef"
     Text bytes: a=0 b=1 c=2 d=3 e=4 f=5
     Delete text bytes [1, 5) = "bcde" — should preserve both MARK_CHANGE sentinels */
  rdoc_builder b = rdoc_builder_new();
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"ab", 2));
  rdoc_builder_mark(&b, 0x0001, 42);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"cd", 2));
  rdoc_builder_mark(&b, 0x0000, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"ef", 2));
  rdoc d = rdoc_builder_finish(&b);

  size_t orig_total = rdoc_total_bytes(d);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 6);

  /* Delete "bcde" (text bytes 1-4) */
  rdoc d2 = rdoc_delete_text(d, 1, 4);

  /* Text should be "a" + "f" = 2 bytes */
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 2);

  /* Both mark change sentinels should be preserved.
     Total raw = original - 4 (deleted text bytes).
     Original: 6 text + 14 sentinel (2 * 7) = 20 raw
     New: 2 text + 14 sentinel = 16 raw */
  ASSERT_INT_EQ(rdoc_total_bytes(d2), orig_total - 4);

  /* Verify mark state: 'a' should be unmarked, 'f' should be unmarked
     (mark on, then mark off, then 'f') */
  rdoc_seek_result sr = rdoc_text_byte_to_raw(d2, 0);
  ASSERT(sr.found);
  rdoc_mark_result m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0); /* 'a' before mark */

  sr = rdoc_text_byte_to_raw(d2, 1);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0); /* 'f' after mark off */

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_insert_delete_multibyte_utf8(void) {
  printf("Running test_insert_delete_multibyte_utf8... ");
  tracker_reset();

  /* Build: "café" — c(1B) a(1B) f(1B) é(2B: 0xC3 0xA9) = 5 text bytes, 4 chars */
  uint8_t cafe[] = {'c', 'a', 'f', 0xC3, 0xA9};
  rdoc d = rdoc_from_bytes(cafe, 5);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 5);
  ASSERT_INT_EQ(rdoc_char_count(d), 4);

  /* Insert multi-byte text at offset 3 (before é) */
  uint8_t umlaut[] = {0xC3, 0xBC}; /* ü = U+00FC */
  rdoc d2 = rdoc_insert_text(d, 3, umlaut, 2);
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 7); /* "cafücafé" → "caf" + "ü" + "é" */
  ASSERT_INT_EQ(rdoc_char_count(d2), 5);

  /* Delete "ü" (text bytes 3-4, the 2-byte ü) */
  rdoc d3 = rdoc_delete_text(d2, 3, 2);
  ASSERT_INT_EQ(rdoc_text_byte_count(d3), 5);
  ASSERT_INT_EQ(rdoc_char_count(d3), 4);

  /* Codepoint boundary enforcement: try to delete starting at byte 4
     in original (0xA9 = continuation byte of é) — should return unchanged */
  rdoc d4 = rdoc_delete_text(d, 4, 1);
  ASSERT_INT_EQ(rdoc_text_byte_count(d4), 5); /* unchanged */
  ASSERT_INT_EQ(rdoc_total_bytes(d4), rdoc_total_bytes(d));

  /* Insert invalid UTF-8 — should return unchanged */
  uint8_t bad_utf8[] = {0xC3}; /* truncated */
  rdoc d5 = rdoc_insert_text(d, 0, bad_utf8, 1);
  ASSERT_INT_EQ(rdoc_text_byte_count(d5), rdoc_text_byte_count(d));

  rdoc_unref(d);
  rdoc_unref(d2);
  rdoc_unref(d3);
  rdoc_unref(d4);
  rdoc_unref(d5);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_delete_range_removes_structure(void) {
  printf("Running test_delete_range_removes_structure... ");
  tracker_reset();

  /* Build: "before" BLOCK_OPEN(1) "inside" BLOCK_CLOSE(1) "after"
     Raw layout:
       0-5:   "before"       [6]
       6-10:  BLOCK_OPEN(1)  [5]
       11-16: "inside"       [6]
       17-21: BLOCK_CLOSE(1) [5]
       22-26: "after"        [5]
     Total: 27 bytes, text: 17, blocks: 1 */
  rdoc_builder b = rdoc_builder_new();
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"before", 6));
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"inside", 6));
  rdoc_builder_block_close(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"after", 5));
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_total_bytes(d), 27);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 17);
  ASSERT_INT_EQ(rdoc_block_count(d), 1);

  /* Delete BLOCK_OPEN(1) + "inside" + BLOCK_CLOSE(1) = raw bytes [6, 22) = 16 bytes */
  rdoc d2 = rdoc_delete_range(d, 6, 16);

  ASSERT_INT_EQ(rdoc_total_bytes(d2), 11);    /* 27 - 16 */
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 11); /* "before" + "after" */
  ASSERT_INT_EQ(rdoc_block_count(d2), 0);      /* block removed */

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_delete_range_rejects_split_sentinel(void) {
  printf("Running test_delete_range_rejects_split_sentinel... ");
  tracker_reset();

  /* Build: "ab" BLOCK_OPEN(1) "cd"
     Raw: [a, b, 0xFF, 1, 0, 0, 0, c, d] = 9 bytes
     BLOCK_OPEN sentinel at raw positions 2-6 */
  uint8_t buf[9];
  buf[0] = 'a';
  buf[1] = 'b';
  rdoc_encode_block_open(buf + 2, 1);
  buf[7] = 'c';
  buf[8] = 'd';

  rdoc d = rdoc_from_bytes(buf, 9);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 9);

  /* Try to delete range [4, 7) which starts inside BLOCK_OPEN sentinel payload —
     should return document unchanged */
  rdoc d2 = rdoc_delete_range(d, 4, 3);
  ASSERT_INT_EQ(rdoc_total_bytes(d2), 9); /* unchanged */

  /* Delete range [2, 7) — starts at sentinel lead, ends after sentinel — valid */
  rdoc d3 = rdoc_delete_range(d, 2, 5);
  ASSERT_INT_EQ(rdoc_total_bytes(d3), 4); /* "ab" + "cd" = 4 bytes */
  ASSERT_INT_EQ(rdoc_text_byte_count(d3), 4);
  ASSERT_INT_EQ(rdoc_block_count(d3), 0);

  rdoc_unref(d);
  rdoc_unref(d2);
  rdoc_unref(d3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_editing_persistence(void) {
  printf("Running test_editing_persistence... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello world" BLOCK_CLOSE(1) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello world", 11));
  rdoc_builder_block_close(&b, 1);
  rdoc original = rdoc_builder_finish(&b);

  size_t orig_text = rdoc_text_byte_count(original);
  size_t orig_total = rdoc_total_bytes(original);
  size_t orig_blocks = rdoc_block_count(original);

  ASSERT_INT_EQ(orig_text, 11);
  ASSERT_INT_EQ(orig_blocks, 1);

  /* Perform various editing operations */
  rdoc d1 = rdoc_insert_text(original, 5, (const uint8_t*)" beautiful", 10);
  rdoc d2 = rdoc_delete_text(original, 0, 6); /* delete "Hello " */
  rdoc d3 = rdoc_replace_text(original, 6, 5, (const uint8_t*)"there", 5);
  rdoc d4 = rdoc_delete_range(original, 0, 5); /* delete BLOCK_OPEN */

  /* Original document MUST be unchanged after all operations */
  ASSERT_INT_EQ(rdoc_text_byte_count(original), orig_text);
  ASSERT_INT_EQ(rdoc_total_bytes(original), orig_total);
  ASSERT_INT_EQ(rdoc_block_count(original), orig_blocks);

  /* Verify modified documents are different */
  ASSERT_INT_EQ(rdoc_text_byte_count(d1), 21);   /* "Hello beautiful world" */
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 5);    /* "world" */
  ASSERT_INT_EQ(rdoc_text_byte_count(d3), 11);   /* "Hello there" (same length) */
  ASSERT_INT_EQ(rdoc_block_count(d4), 0);        /* BLOCK_OPEN removed */

  rdoc_unref(original);
  rdoc_unref(d1);
  rdoc_unref(d2);
  rdoc_unref(d3);
  rdoc_unref(d4);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-028: Block and inline block structural editing === */

int test_insert_block_pair(void) {
  printf("Running test_insert_block_pair... ");
  tracker_reset();

  /* Build: "Hello" */
  rdoc d = rdoc_from_bytes((const uint8_t*)"Hello", 5);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 5);
  ASSERT_INT_EQ(rdoc_block_count(d), 0);

  /* Insert block pair at position 0
     Result: BLOCK_OPEN(1) BLOCK_CLOSE(1) "Hello"
     = [0xFF,1,0,0,0] [0xFE,1,0,0,0] [H,e,l,l,o] = 15 bytes */
  rdoc d2 = rdoc_insert_block(d, 0, 1);
  ASSERT_INT_EQ(rdoc_total_bytes(d2), 15);
  ASSERT_INT_EQ(rdoc_block_count(d2), 1);
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 5);

  /* Depth at position 5 (between BLOCK_OPEN and BLOCK_CLOSE) is 1 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d2, 5), 1);

  /* Depth before block (position 0) is 0 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d2, 0), 0);

  /* Depth after block (position 10, where "Hello" starts) is 0 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d2, 10), 0);

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_insert_nested_blocks(void) {
  printf("Running test_insert_nested_blocks... ");
  tracker_reset();

  /* Start with: BLOCK_OPEN(1) "content" BLOCK_CLOSE(1)
     Raw: [5] + [7] + [5] = 17 bytes */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"content", 7));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_block_count(d), 1);
  ASSERT_INT_EQ(rdoc_total_bytes(d), 17);

  /* Insert another block pair at raw 5 (right after BLOCK_OPEN(1), before "content")
     Result: BLOCK_OPEN(1) BLOCK_OPEN(2) BLOCK_CLOSE(2) "content" BLOCK_CLOSE(1)
     = 17 + 10 = 27 bytes */
  rdoc d2 = rdoc_insert_block(d, 5, 2);
  ASSERT_INT_EQ(rdoc_block_count(d2), 2);
  ASSERT_INT_EQ(rdoc_total_bytes(d2), 27);

  /* Depth at position 10 (between BLOCK_OPEN(2) and BLOCK_CLOSE(2)) is 2 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d2, 10), 2);

  /* Depth at position 5 (after BLOCK_OPEN(1), before BLOCK_OPEN(2)) is 1 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d2, 5), 1);

  /* Depth at position 15 (after BLOCK_CLOSE(2), before "content") is 1 */
  ASSERT_INT_EQ(rdoc_block_depth_at(d2, 15), 1);

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_remove_block_by_id(void) {
  printf("Running test_remove_block_by_id... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "before" BLOCK_OPEN(2) "inside" BLOCK_CLOSE(2) "after" BLOCK_CLOSE(1)
     Raw: 5+6+5+6+5+5+5 = 37 bytes, text: 17, blocks: 2 */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"before", 6));
  rdoc_builder_block_open(&b, 2);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"inside", 6));
  rdoc_builder_block_close(&b, 2);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"after", 5));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_block_count(d), 2);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 17);
  size_t orig_total = rdoc_total_bytes(d);

  /* Remove block 2 — keeps content between open/close as top-level within block 1 */
  rdoc d2 = rdoc_remove_block(d, 2);

  ASSERT_INT_EQ(rdoc_block_count(d2), 1);
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 17);  /* text preserved */
  ASSERT_INT_EQ(rdoc_total_bytes(d2), orig_total - 10);  /* removed 2 × 5-byte sentinels */

  /* Remove non-existent block — returns ref'd copy */
  rdoc d3 = rdoc_remove_block(d, 999);
  ASSERT_INT_EQ(rdoc_total_bytes(d3), rdoc_total_bytes(d));

  rdoc_unref(d);
  rdoc_unref(d2);
  rdoc_unref(d3);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_wrap_inline_block(void) {
  printf("Running test_wrap_inline_block... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello world" BLOCK_CLOSE(1)
     Raw: 5 + 11 + 5 = 21 bytes
     "world" is at raw positions 11-15 (5 bytes) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello world", 11));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_total_bytes(d), 21);
  ASSERT_INT_EQ(rdoc_inline_count(d), 0);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 11);

  /* Wrap "world" (raw bytes [11, 16)) with inline block
     Result: BLOCK_OPEN(1) "Hello " INLINE_OPEN(42) "world" INLINE_CLOSE(42) BLOCK_CLOSE(1)
     = 21 + 10 = 31 bytes */
  rdoc d2 = rdoc_wrap_inline(d, 11, 16, 42);

  ASSERT_INT_EQ(rdoc_inline_count(d2), 1);
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 11);  /* text unchanged */
  ASSERT_INT_EQ(rdoc_total_bytes(d2), 31);

  /* Verify the inline is inside block 1 by checking depth */
  /* INLINE_OPEN is at raw 11 (after shift: 11+0=11 for open insertion point)
     Actually after wrapping: BLOCK_OPEN(1) "Hello " INLINE_OPEN(42) "world" INLINE_CLOSE(42) BLOCK_CLOSE(1)
     Raw: [0-4]=BLOCK_OPEN, [5-10]="Hello ", [11-15]=INLINE_OPEN, [16-20]="world",
          [21-25]=INLINE_CLOSE, [26-30]=BLOCK_CLOSE */
  ASSERT_INT_EQ(rdoc_block_depth_at(d2, 16), 1);     /* inside block 1 */
  ASSERT_INT_EQ(rdoc_inline_depth_at(d2, 16), 1);    /* inside inline 42 */

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_structural_editing_persistence(void) {
  printf("Running test_structural_editing_persistence... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello" BLOCK_CLOSE(1) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_block_close(&b, 1);
  rdoc original = rdoc_builder_finish(&b);

  size_t orig_total = rdoc_total_bytes(original);
  size_t orig_text = rdoc_text_byte_count(original);
  size_t orig_blocks = rdoc_block_count(original);
  size_t orig_inlines = rdoc_inline_count(original);

  /* Various structural edits */
  rdoc d1 = rdoc_insert_block(original, 5, 2);
  rdoc d2 = rdoc_insert_inline(original, 5, 3);
  rdoc d3 = rdoc_remove_block(original, 1);
  rdoc d4 = rdoc_wrap_block(original, 5, 10, 4);
  rdoc d5 = rdoc_wrap_inline(original, 5, 10, 5);

  /* Original MUST be unchanged after all operations */
  ASSERT_INT_EQ(rdoc_total_bytes(original), orig_total);
  ASSERT_INT_EQ(rdoc_text_byte_count(original), orig_text);
  ASSERT_INT_EQ(rdoc_block_count(original), orig_blocks);
  ASSERT_INT_EQ(rdoc_inline_count(original), orig_inlines);

  /* Verify modified documents are different */
  ASSERT_INT_EQ(rdoc_block_count(d1), 2);   /* added block pair */
  ASSERT_INT_EQ(rdoc_inline_count(d2), 1);  /* added inline pair */
  ASSERT_INT_EQ(rdoc_block_count(d3), 0);   /* removed block 1 */
  ASSERT_INT_EQ(rdoc_block_count(d4), 2);   /* wrapped with block */
  ASSERT_INT_EQ(rdoc_inline_count(d5), 1);  /* wrapped with inline */

  rdoc_unref(original);
  rdoc_unref(d1);
  rdoc_unref(d2);
  rdoc_unref(d3);
  rdoc_unref(d4);
  rdoc_unref(d5);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-029: Mark toggle, set, and clear on text ranges === */

int test_toggle_mark_plain_text(void) {
  printf("Running test_toggle_mark_plain_text... ");
  tracker_reset();

  rdoc d = rdoc_from_bytes((const uint8_t*)"Hello world", 11);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 11);

  /* Toggle bold (bit 0x0001) on text range [2, 7) = "llo w" */
  rdoc d2 = rdoc_toggle_mark(d, 2, 7, 0x0001);

  /* Text byte 0 ("H") should be unmarked */
  rdoc_seek_result sr = rdoc_text_byte_to_raw(d2, 0);
  ASSERT(sr.found);
  rdoc_mark_result m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  /* Text byte 1 ("e") should be unmarked */
  sr = rdoc_text_byte_to_raw(d2, 1);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  /* Text byte 2 ("l") should be bold */
  sr = rdoc_text_byte_to_raw(d2, 2);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0001);

  /* Text byte 6 ("w") should still be bold */
  sr = rdoc_text_byte_to_raw(d2, 6);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0001);

  /* Text byte 7 ("o") should be unmarked (after range) */
  sr = rdoc_text_byte_to_raw(d2, 7);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  /* MARK_CHANGE sentinels inserted: total_bytes should be text + 2 marks (14) */
  ASSERT_INT_EQ(rdoc_total_bytes(d2), 11 + 14);

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_toggle_mark_already_bold(void) {
  printf("Running test_toggle_mark_already_bold... ");
  tracker_reset();

  rdoc d = rdoc_from_bytes((const uint8_t*)"Hello", 5);

  /* Apply bold to entire text */
  rdoc d1 = rdoc_toggle_mark(d, 0, 5, 0x0001);

  /* Verify bold applied */
  rdoc_seek_result sr = rdoc_text_byte_to_raw(d1, 0);
  ASSERT(sr.found);
  rdoc_mark_result m = rdoc_mark_at(d1, sr.raw_index);
  ASSERT(m.flags == 0x0001);

  sr = rdoc_text_byte_to_raw(d1, 4);
  ASSERT(sr.found);
  m = rdoc_mark_at(d1, sr.raw_index);
  ASSERT(m.flags == 0x0001);

  /* Toggle bold again on same range — XOR back to 0 */
  rdoc d2 = rdoc_toggle_mark(d1, 0, 5, 0x0001);

  /* Verify marks removed */
  sr = rdoc_text_byte_to_raw(d2, 0);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  sr = rdoc_text_byte_to_raw(d2, 4);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  /* Redundant boundaries cleaned up: no mark sentinels should remain */
  ASSERT_INT_EQ(rdoc_total_bytes(d2), rdoc_text_byte_count(d2));

  rdoc_unref(d);
  rdoc_unref(d1);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_toggle_mark_partial_overlap(void) {
  printf("Running test_toggle_mark_partial_overlap... ");
  tracker_reset();

  /* "abcdef" — 6 text bytes */
  rdoc d = rdoc_from_bytes((const uint8_t*)"abcdef", 6);

  /* Toggle bold on [1, 4) = "bcd" */
  rdoc d1 = rdoc_toggle_mark(d, 1, 4, 0x0001);

  /* Toggle italic on [2, 5) = "cde" — partially overlapping bold */
  rdoc d2 = rdoc_toggle_mark(d1, 2, 5, 0x0002);

  /* Verify segments:
     "a"  = flags 0      (no marks)
     "b"  = flags 0x0001 (bold only)
     "cd" = flags 0x0003 (bold + italic)
     "e"  = flags 0x0002 (italic only)
     "f"  = flags 0      (no marks) */
  rdoc_seek_result sr;
  rdoc_mark_result m;

  sr = rdoc_text_byte_to_raw(d2, 0);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  sr = rdoc_text_byte_to_raw(d2, 1);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0001);

  sr = rdoc_text_byte_to_raw(d2, 2);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0003);

  sr = rdoc_text_byte_to_raw(d2, 3);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0003);

  sr = rdoc_text_byte_to_raw(d2, 4);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0002);

  sr = rdoc_text_byte_to_raw(d2, 5);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  rdoc_unref(d);
  rdoc_unref(d1);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_mark_at_after_toggle(void) {
  printf("Running test_mark_at_after_toggle... ");
  tracker_reset();

  /* Build document with blocks: BLOCK_OPEN(1) "Hello world" BLOCK_CLOSE(1) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello world", 11));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  /* Toggle bold on text bytes [6, 11) = "world" */
  rdoc d2 = rdoc_toggle_mark(d, 6, 11, 0x0001);

  /* Text byte 0 ("H") — before range, should be 0 */
  rdoc_seek_result sr = rdoc_text_byte_to_raw(d2, 0);
  ASSERT(sr.found);
  rdoc_mark_result m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  /* Text byte 5 (" ") — just before range, should be 0 */
  sr = rdoc_text_byte_to_raw(d2, 5);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0);

  /* Text byte 6 ("w") — inside range, should be bold */
  sr = rdoc_text_byte_to_raw(d2, 6);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0001);

  /* Text byte 10 ("d") — last byte in range, should be bold */
  sr = rdoc_text_byte_to_raw(d2, 10);
  ASSERT(sr.found);
  m = rdoc_mark_at(d2, sr.raw_index);
  ASSERT(m.flags == 0x0001);

  /* Original document unchanged */
  ASSERT_INT_EQ(rdoc_total_bytes(d), 21);
  ASSERT_INT_EQ(rdoc_text_byte_count(d), 11);

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === US-031: Transient COW mode for batch edits === */

int test_transient_no_edits(void) {
  printf("Running test_transient_no_edits... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello" BLOCK_CLOSE(1) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  /* Create transient, no edits, convert back */
  rdoc_transient t = rdoc_to_transient(d);
  ASSERT(t.valid);
  rdoc d2 = rdoc_transient_to_persistent(&t);
  ASSERT(!t.valid);

  /* Result should be equivalent */
  ASSERT_INT_EQ(rdoc_text_byte_count(d2), 5);
  ASSERT_INT_EQ(rdoc_block_count(d2), 1);
  ASSERT_INT_EQ(rdoc_total_bytes(d2), rdoc_total_bytes(d));

  rdoc_unref(d);
  rdoc_unref(d2);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_multiple_inserts(void) {
  printf("Running test_transient_multiple_inserts... ");
  tracker_reset();

  rdoc d = rdoc_from_bytes((const uint8_t*)"Hello", 5);
  rdoc_transient t = rdoc_to_transient(d);

  /* Sequential inserts at end */
  rdoc_transient_insert_text(&t, 5, (const uint8_t*)" ", 1);
  rdoc_transient_insert_text(&t, 6, (const uint8_t*)"w", 1);
  rdoc_transient_insert_text(&t, 7, (const uint8_t*)"o", 1);
  rdoc_transient_insert_text(&t, 8, (const uint8_t*)"r", 1);
  rdoc_transient_insert_text(&t, 9, (const uint8_t*)"l", 1);
  rdoc_transient_insert_text(&t, 10, (const uint8_t*)"d", 1);

  rdoc result = rdoc_transient_to_persistent(&t);
  ASSERT_INT_EQ(rdoc_text_byte_count(result), 11);

  /* Verify content */
  uint8_t buf[16];
  size_t written = rdoc_text_to_buf(result, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 11);
  ASSERT(memcmp(buf, "Hello world", 11) == 0);

  rdoc_unref(d);
  rdoc_unref(result);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_original_unchanged(void) {
  printf("Running test_transient_original_unchanged... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello" BLOCK_CLOSE(1) */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  size_t orig_text = rdoc_text_byte_count(d);
  size_t orig_total = rdoc_total_bytes(d);
  size_t orig_blocks = rdoc_block_count(d);

  /* Transient edits */
  rdoc_transient t = rdoc_to_transient(d);
  rdoc_transient_insert_text(&t, 5, (const uint8_t*)" world", 6);
  rdoc_transient_delete_text(&t, 0, 5); /* delete "Hello" */
  rdoc result = rdoc_transient_to_persistent(&t);

  /* Original MUST be unchanged */
  ASSERT_INT_EQ(rdoc_text_byte_count(d), orig_text);
  ASSERT_INT_EQ(rdoc_total_bytes(d), orig_total);
  ASSERT_INT_EQ(rdoc_block_count(d), orig_blocks);

  /* Result should reflect edits */
  ASSERT_INT_EQ(rdoc_text_byte_count(result), 6); /* " world" */

  rdoc_unref(d);
  rdoc_unref(result);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_transient_fewer_allocations(void) {
  printf("Running test_transient_fewer_allocations... ");

  /* Phase 1: Measure allocations for 100 persistent inserts */
  tracker_reset();
  {
    rdoc d = rdoc_from_bytes((const uint8_t*)"abcd", 4);
    rdoc persistent = rdoc_ref(d);
    for (int i = 0; i < 100; i++) {
      rdoc next = rdoc_insert_text(persistent, rdoc_text_byte_count(persistent),
                                    (const uint8_t*)"x", 1);
      rdoc_unref(persistent);
      persistent = next;
    }
    ASSERT_INT_EQ(rdoc_text_byte_count(persistent), 104);
    rdoc_unref(persistent);
    rdoc_unref(d);
  }
  ASSERT(check_no_leaks());
  size_t persistent_allocs = allocation_seq;

  /* Phase 2: Measure allocations for 100 transient inserts */
  tracker_reset();
  {
    rdoc d = rdoc_from_bytes((const uint8_t*)"abcd", 4);
    rdoc_transient t = rdoc_to_transient(d);
    for (int i = 0; i < 100; i++) {
      size_t text_len = 4 + (size_t)i;
      rdoc_transient_insert_text(&t, text_len, (const uint8_t*)"x", 1);
    }
    rdoc result = rdoc_transient_to_persistent(&t);
    ASSERT_INT_EQ(rdoc_text_byte_count(result), 104);
    rdoc_unref(d);
    rdoc_unref(result);
  }
  ASSERT(check_no_leaks());
  size_t transient_allocs = allocation_seq;

  /* Transient should allocate fewer nodes */
  ASSERT(transient_allocs < persistent_allocs);

  TEST_PASS();
}

/* === US-030: Plain text extraction === */

int test_text_to_buf_strips_sentinels(void) {
  printf("Running test_text_to_buf_strips_sentinels... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello" MARK_CHANGE(1,0) " " INLINE_OPEN(2) "world" INLINE_CLOSE(2) BLOCK_CLOSE(1)
     Text content: "Hello world" = 11 text bytes */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_mark(&b, 0x0001, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)" ", 1));
  rdoc_builder_inline_open(&b, 2);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"world", 5));
  rdoc_builder_inline_close(&b, 2);
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_text_byte_count(d), 11);
  ASSERT_INT_EQ(rdoc_block_count(d), 1);
  ASSERT_INT_EQ(rdoc_inline_count(d), 1);

  /* Extract all text */
  uint8_t buf[64];
  size_t written = rdoc_text_to_buf(d, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 11);
  ASSERT(memcmp(buf, "Hello world", 11) == 0);

  /* Partial buffer — should truncate */
  uint8_t small_buf[5];
  written = rdoc_text_to_buf(d, small_buf, 5);
  ASSERT_INT_EQ(written, 5);
  ASSERT(memcmp(small_buf, "Hello", 5) == 0);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_text_slice_to_buf(void) {
  printf("Running test_text_slice_to_buf... ");
  tracker_reset();

  /* Build: "abc" MARK_CHANGE(1,0) "def" MARK_CHANGE(0,0) "ghi"
     Text content: "abcdefghi" = 9 text bytes */
  rdoc_builder b = rdoc_builder_new();
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"abc", 3));
  rdoc_builder_mark(&b, 0x0001, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"def", 3));
  rdoc_builder_mark(&b, 0x0000, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"ghi", 3));
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_text_byte_count(d), 9);

  /* Slice from middle: text bytes [2, 7) = "cdefg" */
  uint8_t buf[64];
  size_t written = rdoc_text_slice_to_buf(d, 2, 5, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 5);
  ASSERT(memcmp(buf, "cdefg", 5) == 0);

  /* Slice from start: text bytes [0, 3) = "abc" */
  written = rdoc_text_slice_to_buf(d, 0, 3, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 3);
  ASSERT(memcmp(buf, "abc", 3) == 0);

  /* Slice from end: text bytes [6, 9) = "ghi" */
  written = rdoc_text_slice_to_buf(d, 6, 3, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 3);
  ASSERT(memcmp(buf, "ghi", 3) == 0);

  /* Slice with small buffer — truncate */
  written = rdoc_text_slice_to_buf(d, 2, 5, buf, 3);
  ASSERT_INT_EQ(written, 3);
  ASSERT(memcmp(buf, "cde", 3) == 0);

  /* Out of bounds offset — returns 0 */
  written = rdoc_text_slice_to_buf(d, 20, 5, buf, sizeof(buf));
  ASSERT_INT_EQ(written, 0);

  rdoc_unref(d);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

int test_text_to_rope(void) {
  printf("Running test_text_to_rope... ");
  tracker_reset();

  /* Build: BLOCK_OPEN(1) "Hello" MARK_CHANGE(1,0) " world" BLOCK_CLOSE(1)
     Text content: "Hello world" = 11 text bytes */
  rdoc_builder b = rdoc_builder_new();
  rdoc_builder_block_open(&b, 1);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)"Hello", 5));
  rdoc_builder_mark(&b, 0x0001, 0);
  ASSERT(rdoc_builder_text(&b, (const uint8_t*)" world", 6));
  rdoc_builder_block_close(&b, 1);
  rdoc d = rdoc_builder_finish(&b);

  ASSERT_INT_EQ(rdoc_text_byte_count(d), 11);

  /* Convert to rope */
  rope r = rdoc_text_to_rope(d);
  ASSERT_INT_EQ(rope_byte_count(r), 11);

  /* Verify rope content matches rdoc_text_to_buf output */
  uint8_t rdoc_buf[64];
  size_t rdoc_written = rdoc_text_to_buf(d, rdoc_buf, sizeof(rdoc_buf));
  ASSERT_INT_EQ(rdoc_written, 11);

  uint8_t rope_buf[64];
  rope_st_copy_range(r.root, 0, 11, rope_buf);
  ASSERT(memcmp(rdoc_buf, rope_buf, 11) == 0);

  /* Empty document -> empty rope */
  rdoc empty = rdoc_new();
  rope r_empty = rdoc_text_to_rope(empty);
  ASSERT_INT_EQ(rope_byte_count(r_empty), 0);

  rdoc_unref(d);
  rope_unref(r);
  rdoc_unref(empty);
  rope_unref(r_empty);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* === Main === */

int main(void) {
  int pass = 0, fail = 0;
  int total = 59;
  typedef int (*test_fn)(void);
  test_fn tests[] = {
    /* US-021 */
    test_encode_decode_block_open,
    test_encode_decode_block_close,
    test_encode_decode_inline_open,
    test_encode_decode_inline_close,
    test_encode_decode_mark_change,
    test_is_sentinel,
    test_sentinel_size,
    test_find_safe_split_inside_sentinel,
    test_find_safe_split_inside_utf8,
    test_find_safe_split_already_safe,
    test_find_safe_split_mark_change,
    test_decode_wrong_lead_byte,
    test_encode_decode_zero_id,
    test_encode_decode_mark_zero_flags,
    /* US-022 */
    test_summary_identity,
    test_summary_combine_associative,
    test_summary_summarize_mixed,
    test_summary_summarize_pure_text,
    test_summary_summarize_sentinels_only,
    /* US-023 */
    test_empty_rdoc,
    test_rdoc_from_bytes_valid,
    test_rdoc_from_bytes_truncated,
    test_rdoc_coexists_with_rope,
    /* US-024 */
    test_builder_complex_document,
    test_builder_empty,
    test_builder_invalid_utf8,
    /* US-025 */
    test_seek_text_byte_with_sentinels,
    test_seek_char_multibyte_utf8,
    test_seek_line_with_blocks,
    test_seek_block_index,
    test_block_depth_at,
    test_mark_at,
    /* US-026 */
    test_cursor_iterate_full_document,
    test_cursor_mark_state_tracking,
    test_cursor_complex_document,
    test_cursor_mid_document_start,
    /* US-027 */
    test_insert_text_beginning_middle_end,
    test_insert_text_inside_marked_range,
    test_delete_text_preserves_sentinel,
    test_insert_delete_multibyte_utf8,
    test_delete_range_removes_structure,
    test_delete_range_rejects_split_sentinel,
    test_editing_persistence,
    /* US-028 */
    test_insert_block_pair,
    test_insert_nested_blocks,
    test_remove_block_by_id,
    test_wrap_inline_block,
    test_structural_editing_persistence,
    /* US-029 */
    test_toggle_mark_plain_text,
    test_toggle_mark_already_bold,
    test_toggle_mark_partial_overlap,
    test_mark_at_after_toggle,
    /* US-030 */
    test_text_to_buf_strips_sentinels,
    test_text_slice_to_buf,
    test_text_to_rope,
    /* US-031 */
    test_transient_no_edits,
    test_transient_multiple_inserts,
    test_transient_original_unchanged,
    test_transient_fewer_allocations,
  };

  for (int i = 0; i < total; i++) {
    if (tests[i]()) pass++;
    else fail++;
  }

  printf("\n%d/%d tests passed\n", pass, total);
  return fail > 0 ? 1 : 0;
}
