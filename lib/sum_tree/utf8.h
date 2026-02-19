#ifndef UTF8_H
#define UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Returns 1-4 for valid lead bytes, 0 for continuation/invalid bytes */
static inline size_t utf8_codepoint_length(uint8_t first_byte) {
  if (first_byte < 0x80) return 1;
  if ((first_byte & 0xE0) == 0xC0) return 2;
  if ((first_byte & 0xF0) == 0xE0) return 3;
  if ((first_byte & 0xF8) == 0xF0) return 4;
  return 0;
}

/* Returns true if byte is a continuation byte (0x80-0xBF) */
static inline bool utf8_is_continuation(uint8_t b) {
  return (b & 0xC0) == 0x80;
}

/* Decodes one codepoint from src. Returns bytes consumed (0 on error or len==0).
   On success, writes decoded codepoint to *out_cp. */
static inline size_t utf8_decode(const uint8_t* src, size_t len, uint32_t* out_cp) {
  if (len == 0) return 0;

  uint8_t b0 = src[0];
  size_t cplen = utf8_codepoint_length(b0);
  if (cplen == 0 || cplen > len) return 0;

  uint32_t cp;
  switch (cplen) {
    case 1:
      cp = b0;
      break;
    case 2:
      if (!utf8_is_continuation(src[1])) return 0;
      cp = ((uint32_t)(b0 & 0x1F) << 6) | (src[1] & 0x3F);
      /* Reject overlong: 2-byte must encode >= U+0080 */
      if (cp < 0x80) return 0;
      break;
    case 3:
      if (!utf8_is_continuation(src[1]) || !utf8_is_continuation(src[2])) return 0;
      cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(src[1] & 0x3F) << 6) | (src[2] & 0x3F);
      /* Reject overlong: 3-byte must encode >= U+0800 */
      if (cp < 0x800) return 0;
      break;
    case 4:
      if (!utf8_is_continuation(src[1]) || !utf8_is_continuation(src[2]) || !utf8_is_continuation(src[3])) return 0;
      cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(src[1] & 0x3F) << 12) |
           ((uint32_t)(src[2] & 0x3F) << 6) | (src[3] & 0x3F);
      /* Reject overlong: 4-byte must encode >= U+10000 and <= U+10FFFF */
      if (cp < 0x10000 || cp > 0x10FFFF) return 0;
      break;
    default:
      return 0;
  }

  if (out_cp) *out_cp = cp;
  return cplen;
}

/* Returns true if the entire buffer is valid UTF-8 */
static inline bool utf8_validate(const uint8_t* src, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint32_t cp;
    size_t consumed = utf8_decode(src + i, len - i, &cp);
    if (consumed == 0) return false;
    /* Reject surrogates (U+D800..U+DFFF) */
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += consumed;
  }
  return true;
}

/* Count codepoints in a valid UTF-8 buffer */
static inline size_t utf8_codepoint_count(const uint8_t* src, size_t len) {
  size_t count = 0;
  for (size_t i = 0; i < len; i++) {
    if (!utf8_is_continuation(src[i])) count++;
  }
  return count;
}

/* Count newline bytes (0x0A) in buffer */
static inline size_t utf8_line_count(const uint8_t* src, size_t len) {
  size_t count = 0;
  for (size_t i = 0; i < len; i++) {
    if (src[i] == 0x0A) count++;
  }
  return count;
}

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

/* Given a byte position, adjust backward to a codepoint boundary.
   Returns adjusted position. Returns pos if already on boundary, 0 if pos is 0. */
static inline size_t utf8_find_safe_split(const uint8_t* src, size_t len, size_t pos) {
  if (pos == 0 || pos >= len) return pos;
  /* Walk backward while we're on a continuation byte */
  while (pos > 0 && utf8_is_continuation(src[pos])) {
    pos--;
  }
  return pos;
}

#endif /* UTF8_H */
