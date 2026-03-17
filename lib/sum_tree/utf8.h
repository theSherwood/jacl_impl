/*
 * utf8.h — Compatibility forwarder
 *
 * Core UTF-8 codec functions have moved to unicode/unicode.h.
 * This header re-exports them and keeps the simplified grapheme helpers
 * for backward compatibility until UAX #29 grapheme segmentation (US-004)
 * replaces them.
 */

#ifndef UTF8_H
#define UTF8_H

#include "../unicode/unicode.h"

/* --- Simplified grapheme helpers (compat) --------------------------------
 * These will be replaced by proper UAX #29 segmentation in unicode.h.
 * Kept here so that existing consumers (rope.h, test_utf8.c) keep working. */

/* Returns true if codepoint is a combining mark (Extend property).
   Covers: U+0300-U+036F (Combining Diacritical Marks),
           U+1AB0-U+1AFF (Combining Diacritical Marks Extended),
           U+1DC0-U+1DFF (Combining Diacritical Marks Supplement),
           U+20D0-U+20FF (Combining Diacritical Marks for Symbols),
           U+FE20-U+FE2F (Combining Half Marks) */
static inline bool utf8_is_extend(uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) ||
         (cp >= 0x1AB0 && cp <= 0x1AFF) ||
         (cp >= 0x1DC0 && cp <= 0x1DFF) ||
         (cp >= 0x20D0 && cp <= 0x20FF) ||
         (cp >= 0xFE20 && cp <= 0xFE2F);
}

/* Returns true if codepoint is a regional indicator (U+1F1E6-U+1F1FF) */
static inline bool utf8_is_regional_indicator(uint32_t cp) {
  return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

/* Count grapheme clusters in a valid UTF-8 buffer.
   Simplified UAX #29: handles CRLF, combining marks, ZWJ, regional indicator pairs. */
static inline size_t utf8_grapheme_count(const uint8_t* src, size_t len) {
  size_t count = 0;
  size_t i = 0;
  bool prev_was_cr = false;
  bool prev_was_ri = false;

  while (i < len) {
    uint32_t cp;
    size_t consumed = utf8_decode(src + i, len - i, &cp);
    if (consumed == 0) {
      /* Continuation byte without lead — skip without counting */
      if (utf8_is_continuation(src[i])) {
        i++;
        continue;
      }
      /* Other invalid byte — treat as single grapheme */
      count++;
      i++;
      prev_was_cr = false;
      prev_was_ri = false;
      continue;
    }

    if (cp == 0x0A && prev_was_cr) {
      /* LF after CR: don't break, part of CRLF cluster */
      prev_was_cr = false;
      prev_was_ri = false;
      i += consumed;
      continue;
    }

    if (utf8_is_extend(cp) || cp == 0x200D) {
      /* Extend or ZWJ: don't break before these */
      prev_was_cr = false;
      prev_was_ri = false;
      i += consumed;
      continue;
    }

    if (utf8_is_regional_indicator(cp) && prev_was_ri) {
      /* Second RI in a pair: don't break */
      prev_was_ri = false;
      i += consumed;
      continue;
    }

    /* Break point — new grapheme cluster */
    count++;
    prev_was_cr = (cp == 0x0D);
    prev_was_ri = utf8_is_regional_indicator(cp);
    i += consumed;
  }
  return count;
}

/* Find the byte offset where the Nth grapheme (0-indexed) starts within a buffer.
   Returns the byte offset, or len if target >= total grapheme count. */
static inline size_t utf8_byte_offset_of_grapheme(const uint8_t* src, size_t len, size_t target) {
  if (target == 0) return 0;
  size_t count = 0;
  size_t i = 0;
  bool prev_was_cr = false;
  bool prev_was_ri = false;

  while (i < len) {
    uint32_t cp;
    size_t consumed = utf8_decode(src + i, len - i, &cp);
    if (consumed == 0) {
      if (utf8_is_continuation(src[i])) {
        i++;
        continue;
      }
      /* Other invalid byte — treat as grapheme start */
      count++;
      if (count > target) return i;
      i++;
      prev_was_cr = false;
      prev_was_ri = false;
      continue;
    }

    if (cp == 0x0A && prev_was_cr) {
      prev_was_cr = false;
      prev_was_ri = false;
      i += consumed;
      continue;
    }

    if (utf8_is_extend(cp) || cp == 0x200D) {
      prev_was_cr = false;
      prev_was_ri = false;
      i += consumed;
      continue;
    }

    if (utf8_is_regional_indicator(cp) && prev_was_ri) {
      prev_was_ri = false;
      i += consumed;
      continue;
    }

    /* Break point — new grapheme cluster */
    count++;
    if (count > target) return i;
    prev_was_cr = (cp == 0x0D);
    prev_was_ri = utf8_is_regional_indicator(cp);
    i += consumed;
  }
  return len; /* target exceeds total */
}

#endif /* UTF8_H */
