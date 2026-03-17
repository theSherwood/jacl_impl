/*
 * unicode.h — Core Unicode utilities for the ds project
 *
 * UTF-8 codec functions (encode, decode, validate) plus Unicode property
 * lookups from the generated tables.  All functions are static inline.
 */

#ifndef UNICODE_H
#define UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "unicode_tables.h"

/* -----------------------------------------------------------------------
 * UTF-8 codec
 * ----------------------------------------------------------------------- */

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

/* Encode a codepoint to UTF-8. Returns bytes written (1-4), 0 on invalid codepoint.
   dst must have room for at least 4 bytes. */
static inline size_t utf8_encode(uint32_t cp, uint8_t* dst) {
  if (cp < 0x80) {
    dst[0] = (uint8_t)cp;
    return 1;
  }
  if (cp < 0x800) {
    dst[0] = (uint8_t)(0xC0 | (cp >> 6));
    dst[1] = (uint8_t)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    /* Reject surrogates */
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    dst[0] = (uint8_t)(0xE0 | (cp >> 12));
    dst[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    dst[2] = (uint8_t)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp <= 0x10FFFF) {
    dst[0] = (uint8_t)(0xF0 | (cp >> 18));
    dst[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (uint8_t)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0;
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

/* -----------------------------------------------------------------------
 * NFD normalization
 * ----------------------------------------------------------------------- */

/* Hangul constants (UAX #15) */
#define UNICODE_SBASE  0xAC00
#define UNICODE_LBASE  0x1100
#define UNICODE_VBASE  0x1161
#define UNICODE_TBASE  0x11A7
#define UNICODE_LCOUNT 19
#define UNICODE_VCOUNT 21
#define UNICODE_TCOUNT 28
#define UNICODE_NCOUNT (UNICODE_VCOUNT * UNICODE_TCOUNT)  /* 588 */
#define UNICODE_SCOUNT (UNICODE_LCOUNT * UNICODE_NCOUNT)  /* 11172 */

/* Internal: decompose a single codepoint into NFD codepoints.
   Writes up to 18 codepoints to out[], returns count. Handles
   table-based and Hangul algorithmic decomposition. */
static inline size_t unicode__decompose_cp(uint32_t cp, uint32_t out[18]) {
  /* Hangul syllable decomposition */
  if (cp >= UNICODE_SBASE && cp < UNICODE_SBASE + UNICODE_SCOUNT) {
    uint32_t si = cp - UNICODE_SBASE;
    uint32_t l = UNICODE_LBASE + si / UNICODE_NCOUNT;
    uint32_t v = UNICODE_VBASE + (si % UNICODE_NCOUNT) / UNICODE_TCOUNT;
    uint32_t t = UNICODE_TBASE + si % UNICODE_TCOUNT;
    out[0] = l;
    out[1] = v;
    if (t != UNICODE_TBASE) {
      out[2] = t;
      return 3;
    }
    return 2;
  }

  /* Table-based decomposition */
  const uint32_t* d = unicode_nfd_decomp(cp);
  if (d) {
    uint32_t len = d[0];
    for (uint32_t i = 0; i < len; i++)
      out[i] = d[1 + i];
    return (size_t)len;
  }

  /* No decomposition */
  out[0] = cp;
  return 1;
}

/* Internal: sort combining marks by CCC using insertion sort.
   Only sorts the portion with CCC > 0 that follows a starter. */
static inline void unicode__canonical_order(uint32_t* cps, size_t len) {
  for (size_t i = 1; i < len; i++) {
    uint8_t ccc_i = unicode_ccc(cps[i]);
    if (ccc_i == 0) continue;
    size_t j = i;
    while (j > 0 && unicode_ccc(cps[j - 1]) > ccc_i) {
      uint32_t tmp = cps[j];
      cps[j] = cps[j - 1];
      cps[j - 1] = tmp;
      j--;
    }
  }
}

/* Decompose src into NFD form and write UTF-8 to dst.
   Returns 0 on success, -1 if dst_cap is too small.
   *out_len is always set to the required byte length. */
static inline int unicode_nfd(const uint8_t* src, size_t src_len,
                              uint8_t* dst, size_t dst_cap, size_t* out_len) {
  /* Two passes: first compute, then check capacity.
     We use a single pass with a buffer. */
  uint32_t buf[256]; /* codepoint buffer for decomposition + reordering */
  size_t buf_len = 0;
  size_t si = 0;

  /* Phase 1: decompose all codepoints */
  while (si < src_len) {
    /* ASCII fast path */
    if (src[si] < 0x80) {
      buf[buf_len++] = src[si++];
      if (buf_len >= 240) goto overflow;
      continue;
    }

    uint32_t cp;
    size_t consumed = utf8_decode(src + si, src_len - si, &cp);
    if (consumed == 0) { si++; continue; }
    si += consumed;

    uint32_t decomposed[18];
    size_t dlen = unicode__decompose_cp(cp, decomposed);
    if (buf_len + dlen >= 256) goto overflow;
    for (size_t k = 0; k < dlen; k++)
      buf[buf_len++] = decomposed[k];
  }

  /* Phase 2: canonical ordering */
  unicode__canonical_order(buf, buf_len);

  /* Phase 3: encode to UTF-8 and compute output length */
  {
    size_t total = 0;
    for (size_t i = 0; i < buf_len; i++) {
      uint8_t tmp[4];
      size_t enc = utf8_encode(buf[i], tmp);
      if (total + enc <= dst_cap) {
        for (size_t j = 0; j < enc; j++)
          dst[total + j] = tmp[j];
      }
      total += enc;
    }
    *out_len = total;
    return (total <= dst_cap) ? 0 : -1;
  }

overflow:
  /* Large string: use two-pass approach */
  {
    /* We need a dynamically-sized buffer. Since we don't have malloc,
       we'll compute in chunks. For simplicity with the static-inline
       constraint, fall back to counting output size first. */
    size_t total = 0;

    /* Pass 1: compute total size and decompose + reorder per "segment"
       (a starter + all following combining marks). */
    si = 0;
    while (si < src_len) {
      uint32_t seg[128];
      size_t seg_len = 0;

      /* Collect one segment: first codepoint + all following CCC>0 */
      while (si < src_len) {
        uint32_t cp;
        size_t consumed;
        if (src[si] < 0x80) {
          cp = src[si];
          consumed = 1;
        } else {
          consumed = utf8_decode(src + si, src_len - si, &cp);
          if (consumed == 0) { si++; continue; }
        }
        si += consumed;

        uint32_t decomposed[18];
        size_t dlen = unicode__decompose_cp(cp, decomposed);
        for (size_t k = 0; k < dlen && seg_len < 128; k++)
          seg[seg_len++] = decomposed[k];

        /* If next codepoint is a starter (CCC==0), end segment */
        if (si < src_len) {
          uint32_t next;
          size_t nc;
          if (src[si] < 0x80) {
            break; /* ASCII is always a starter */
          } else {
            nc = utf8_decode(src + si, src_len - si, &next);
            if (nc == 0) break;
            uint32_t nd[18];
            size_t ndl = unicode__decompose_cp(next, nd);
            if (ndl > 0 && unicode_ccc(nd[0]) == 0) break;
          }
        } else {
          break;
        }
      }

      unicode__canonical_order(seg, seg_len);

      for (size_t i = 0; i < seg_len; i++) {
        uint8_t tmp[4];
        size_t enc = utf8_encode(seg[i], tmp);
        if (total + enc <= dst_cap) {
          for (size_t j = 0; j < enc; j++)
            dst[total + j] = tmp[j];
        }
        total += enc;
      }
    }
    *out_len = total;
    return (total <= dst_cap) ? 0 : -1;
  }
}

/* Returns the byte length of the NFD form without writing output. */
static inline size_t unicode_nfd_length(const uint8_t* src, size_t src_len) {
  size_t out_len = 0;
  unicode_nfd(src, src_len, NULL, 0, &out_len);
  return out_len;
}

/* Quick check: returns true if src is already in NFD form.
   Uses the NFD_QC property from the UCD tables. */
static inline bool unicode_is_nfd(const uint8_t* src, size_t src_len) {
  size_t i = 0;
  uint8_t last_ccc = 0;

  while (i < src_len) {
    /* ASCII fast path */
    if (src[i] < 0x80) {
      last_ccc = 0;
      i++;
      continue;
    }

    uint32_t cp;
    size_t consumed = utf8_decode(src + i, src_len - i, &cp);
    if (consumed == 0) return false;
    i += consumed;

    /* Hangul syllables are not NFD */
    if (cp >= UNICODE_SBASE && cp < UNICODE_SBASE + UNICODE_SCOUNT)
      return false;

    /* NFD_QC == No means this codepoint has a canonical decomposition */
    if (unicode_nfd_qc(cp))
      return false;

    /* CCC must be non-decreasing within a combining sequence */
    uint8_t ccc = unicode_ccc(cp);
    if (ccc != 0 && last_ccc > ccc)
      return false;
    last_ccc = ccc;
  }
  return true;
}

/* -----------------------------------------------------------------------
 * Grapheme cluster segmentation (UAX #29)
 * ----------------------------------------------------------------------- */

/* Returns the byte offset of the next grapheme cluster boundary after pos,
   or len if at end. Implements full UAX #29 grapheme cluster break rules. */
static inline size_t unicode_grapheme_next(const uint8_t* src, size_t len, size_t pos) {
  if (pos >= len) return len;

  /* Decode current codepoint */
  uint32_t cp;
  size_t consumed = utf8_decode(src + pos, len - pos, &cp);
  if (consumed == 0) return pos + 1;  /* skip invalid byte */

  size_t cur = pos + consumed;
  UnicodeGraphemeBreak prev_gbp = unicode_grapheme_break(cp);

  /* GB3: CR × LF */
  if (prev_gbp == GBP_CR && cur < len) {
    uint32_t next;
    size_t nc = utf8_decode(src + cur, len - cur, &next);
    if (nc > 0 && unicode_grapheme_break(next) == GBP_LF)
      return cur + nc;
  }

  /* GB4: (Control | CR | LF) ÷ */
  if (prev_gbp == GBP_CONTROL || prev_gbp == GBP_CR || prev_gbp == GBP_LF)
    return cur;

  /* Track state for GB11 and GB12/GB13 */
  bool seen_ext_pic = (prev_gbp == GBP_EXTENDED_PICTOGRAPHIC);
  int ri_count = (prev_gbp == GBP_REGIONAL_INDICATOR) ? 1 : 0;

  while (cur < len) {
    uint32_t next_cp;
    size_t nc = utf8_decode(src + cur, len - cur, &next_cp);
    if (nc == 0) break;  /* break before invalid */

    UnicodeGraphemeBreak next_gbp = unicode_grapheme_break(next_cp);

    /* GB5: ÷ (Control | CR | LF) */
    if (next_gbp == GBP_CONTROL || next_gbp == GBP_CR || next_gbp == GBP_LF)
      break;

    /* GB6: L × (L | V | LV | LVT) */
    if (prev_gbp == GBP_L &&
        (next_gbp == GBP_L || next_gbp == GBP_V ||
         next_gbp == GBP_LV || next_gbp == GBP_LVT)) {
      prev_gbp = next_gbp;
      cur += nc;
      continue;
    }

    /* GB7: (LV | V) × (V | T) */
    if ((prev_gbp == GBP_LV || prev_gbp == GBP_V) &&
        (next_gbp == GBP_V || next_gbp == GBP_T)) {
      prev_gbp = next_gbp;
      cur += nc;
      continue;
    }

    /* GB8: (LVT | T) × T */
    if ((prev_gbp == GBP_LVT || prev_gbp == GBP_T) && next_gbp == GBP_T) {
      prev_gbp = next_gbp;
      cur += nc;
      continue;
    }

    /* GB9: × (Extend | ZWJ) */
    if (next_gbp == GBP_EXTEND || next_gbp == GBP_ZWJ) {
      if (next_gbp == GBP_ZWJ && seen_ext_pic) {
        /* Keep seen_ext_pic for GB11 */
      } else if (next_gbp == GBP_EXTEND) {
        /* Extend keeps seen_ext_pic */
      } else {
        seen_ext_pic = false;
      }
      prev_gbp = next_gbp;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* GB9a: × SpacingMark */
    if (next_gbp == GBP_SPACINGMARK) {
      prev_gbp = next_gbp;
      cur += nc;
      seen_ext_pic = false;
      ri_count = 0;
      continue;
    }

    /* GB9b: Prepend × */
    if (prev_gbp == GBP_PREPEND) {
      prev_gbp = next_gbp;
      seen_ext_pic = (next_gbp == GBP_EXTENDED_PICTOGRAPHIC);
      ri_count = (next_gbp == GBP_REGIONAL_INDICATOR) ? 1 : 0;
      cur += nc;
      continue;
    }

    /* GB11: ExtPict Extend* ZWJ × ExtPict */
    if (prev_gbp == GBP_ZWJ && seen_ext_pic &&
        next_gbp == GBP_EXTENDED_PICTOGRAPHIC) {
      prev_gbp = next_gbp;
      seen_ext_pic = true;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* GB12/GB13: Regional_Indicator × Regional_Indicator (pairs only) */
    if (prev_gbp == GBP_REGIONAL_INDICATOR &&
        next_gbp == GBP_REGIONAL_INDICATOR && (ri_count % 2) == 1) {
      ri_count++;
      prev_gbp = next_gbp;
      cur += nc;
      seen_ext_pic = false;
      continue;
    }

    /* GB999: ÷ Any */
    break;
  }

  return cur;
}

/* Returns the byte offset of the previous grapheme cluster boundary before pos,
   or 0 if at start. Walks forward from a safe point and returns the last
   boundary strictly before pos. */
static inline size_t unicode_grapheme_prev(const uint8_t* src, size_t len, size_t pos) {
  if (pos == 0 || pos > len) return 0;

  /* Find a safe starting point: walk backward to a known grapheme boundary.
     Control/CR/LF always form boundaries, as does start-of-string. */
  size_t safe = 0;
  {
    size_t s = pos;
    size_t limit = (pos > 256) ? pos - 256 : 0;
    while (s > limit) {
      /* Find start of previous codepoint */
      s--;
      while (s > limit && utf8_is_continuation(src[s]))
        s--;

      uint32_t cp2;
      size_t nc2 = utf8_decode(src + s, len - s, &cp2);
      if (nc2 == 0) { safe = s; break; }

      UnicodeGraphemeBreak gbp2 = unicode_grapheme_break(cp2);
      if (gbp2 == GBP_CONTROL || gbp2 == GBP_CR || gbp2 == GBP_LF) {
        /* The boundary is after this control character */
        safe = s + nc2;
        break;
      }
    }
    if (s <= limit) safe = limit;
  }

  /* Walk forward from safe collecting grapheme boundaries.
     Return the last boundary that is strictly less than pos. */
  size_t last_boundary = safe;
  size_t scan = safe;
  while (scan < pos && scan < len) {
    size_t next = unicode_grapheme_next(src, len, scan);
    if (next >= pos) break;
    last_boundary = next;
    if (next <= scan) break;  /* safety: avoid infinite loop */
    scan = next;
  }

  return last_boundary;
}

/* Count total grapheme clusters in a UTF-8 string. */
static inline size_t unicode_grapheme_count(const uint8_t* src, size_t len) {
  if (len == 0) return 0;
  size_t count = 0;
  size_t pos = 0;
  while (pos < len) {
    pos = unicode_grapheme_next(src, len, pos);
    count++;
  }
  return count;
}

/* Find the byte offset where the Nth grapheme (0-indexed) starts.
   Returns the byte offset, or len if target >= total grapheme count. */
static inline size_t unicode_grapheme_byte_offset(const uint8_t* src, size_t len, size_t target) {
  if (target == 0) return 0;
  size_t pos = 0;
  for (size_t i = 0; i < target && pos < len; i++)
    pos = unicode_grapheme_next(src, len, pos);
  return pos;
}

/* -----------------------------------------------------------------------
 * Word boundary segmentation (UAX #29)
 * ----------------------------------------------------------------------- */

/* Word segment type classification */
typedef enum {
  UNICODE_WORD_LETTER,
  UNICODE_WORD_NUMERIC,
  UNICODE_WORD_SPACE,
  UNICODE_WORD_PUNCTUATION,
  UNICODE_WORD_OTHER,
} UnicodeWordType;

/* Internal: skip Extend and Format codepoints in the word break algorithm.
   Returns the WBP of the next "significant" codepoint at or after *pos,
   advancing *pos past any Extend/Format. Returns WBP_OTHER if at end. */
static inline UnicodeWordBreak unicode__wb_skip_extend(
    const uint8_t* src, size_t len, size_t* pos) {
  while (*pos < len) {
    uint32_t cp;
    size_t nc = utf8_decode(src + *pos, len - *pos, &cp);
    if (nc == 0) { (*pos)++; continue; }
    UnicodeWordBreak wb = unicode_word_break(cp);
    if (wb != WBP_EXTEND && wb != WBP_FORMAT && wb != WBP_ZWJ)
      return wb;
    *pos += nc;
  }
  return WBP_OTHER;
}

/* Internal: peek at the WBP of the next significant codepoint after pos
   (skipping Extend/Format/ZWJ). Does not modify pos. */
static inline UnicodeWordBreak unicode__wb_peek_next(
    const uint8_t* src, size_t len, size_t pos) {
  size_t p = pos;
  return unicode__wb_skip_extend(src, len, &p);
}

/* Internal: look ahead past current + Extend* to find next significant WBP.
   Start from byte offset 'from'. Returns WBP_OTHER at end. */
static inline UnicodeWordBreak unicode__wb_lookahead(
    const uint8_t* src, size_t len, size_t from) {
  size_t p = from;
  /* Skip current codepoint */
  if (p < len) {
    uint32_t cp;
    size_t nc = utf8_decode(src + p, len - p, &cp);
    if (nc > 0) p += nc; else p++;
  }
  /* Skip Extend/Format/ZWJ */
  return unicode__wb_peek_next(src, len, p);
}

/* Returns the byte offset of the next word boundary after pos, or len. */
static inline size_t unicode_word_next(const uint8_t* src, size_t len, size_t pos) {
  if (pos >= len) return len;

  uint32_t cp;
  size_t consumed = utf8_decode(src + pos, len - pos, &cp);
  if (consumed == 0) return pos + 1;

  size_t cur = pos + consumed;
  UnicodeWordBreak prev_wb = unicode_word_break(cp);
  UnicodeWordBreak prev_prev_wb = WBP_OTHER; /* for lookbehind rules WB7/WB7c/WB11 */
  int ri_count = (prev_wb == WBP_REGIONAL_INDICATOR) ? 1 : 0;

  while (cur < len) {
    uint32_t next_cp;
    size_t nc = utf8_decode(src + cur, len - cur, &next_cp);
    if (nc == 0) break;

    UnicodeWordBreak next_wb = unicode_word_break(next_cp);

    /* WB3: CR × LF */
    if (prev_wb == WBP_CR && next_wb == WBP_LF) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      /* WB3a/3b: break after CRLF */
      break;
    }

    /* WB3a: (Newline | CR | LF) ÷ */
    if (prev_wb == WBP_NEWLINE || prev_wb == WBP_CR || prev_wb == WBP_LF)
      break;

    /* WB3b: ÷ (Newline | CR | LF) */
    if (next_wb == WBP_NEWLINE || next_wb == WBP_CR || next_wb == WBP_LF)
      break;

    /* WB3c: ZWJ × \p{Extended_Pictographic} */
    if (prev_wb == WBP_ZWJ) {
      UnicodeGraphemeBreak gbp = unicode_grapheme_break(next_cp);
      if (gbp == GBP_EXTENDED_PICTOGRAPHIC) {
        prev_prev_wb = prev_wb;
        prev_wb = next_wb;
        cur += nc;
        ri_count = 0;
        continue;
      }
    }

    /* WB3d: WSegSpace × WSegSpace */
    if (prev_wb == WBP_WSEGSPACE && next_wb == WBP_WSEGSPACE) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB4: X (Extend | Format | ZWJ)* → X (transparent) */
    if (next_wb == WBP_EXTEND || next_wb == WBP_FORMAT || next_wb == WBP_ZWJ) {
      /* Don't update prev_wb or prev_prev_wb */
      cur += nc;
      ri_count = 0;
      continue;
    }

    bool prev_ahletter = (prev_wb == WBP_ALETTER || prev_wb == WBP_HEBREW_LETTER);
    bool next_ahletter = (next_wb == WBP_ALETTER || next_wb == WBP_HEBREW_LETTER);

    /* WB5: AHLetter × AHLetter */
    if (prev_ahletter && next_ahletter) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB6: AHLetter × (MidLetter | MidNumLetQ) AHLetter */
    bool next_mid_letter = (next_wb == WBP_MIDLETTER || next_wb == WBP_MIDNUMLET ||
                            next_wb == WBP_SINGLE_QUOTE);
    if (prev_ahletter && next_mid_letter) {
      UnicodeWordBreak after = unicode__wb_lookahead(src, len, cur);
      if (after == WBP_ALETTER || after == WBP_HEBREW_LETTER) {
        prev_prev_wb = prev_wb;
        prev_wb = next_wb;
        cur += nc;
        ri_count = 0;
        continue;
      }
    }

    /* WB7: AHLetter (MidLetter | MidNumLetQ) × AHLetter
       prev_prev_wb was AHLetter, prev_wb is Mid, next is AHLetter */
    bool prev_mid_letter = (prev_wb == WBP_MIDLETTER || prev_wb == WBP_MIDNUMLET ||
                            prev_wb == WBP_SINGLE_QUOTE);
    bool prev_prev_ahletter = (prev_prev_wb == WBP_ALETTER ||
                               prev_prev_wb == WBP_HEBREW_LETTER);
    if (prev_prev_ahletter && prev_mid_letter && next_ahletter) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB7a: Hebrew_Letter × Single_Quote */
    if (prev_wb == WBP_HEBREW_LETTER && next_wb == WBP_SINGLE_QUOTE) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB7b: Hebrew_Letter × Double_Quote Hebrew_Letter (lookahead) */
    if (prev_wb == WBP_HEBREW_LETTER && next_wb == WBP_DOUBLE_QUOTE) {
      UnicodeWordBreak after = unicode__wb_lookahead(src, len, cur);
      if (after == WBP_HEBREW_LETTER) {
        prev_prev_wb = prev_wb;
        prev_wb = next_wb;
        cur += nc;
        ri_count = 0;
        continue;
      }
    }

    /* WB7c: Hebrew_Letter Double_Quote × Hebrew_Letter */
    if (prev_prev_wb == WBP_HEBREW_LETTER && prev_wb == WBP_DOUBLE_QUOTE &&
        next_wb == WBP_HEBREW_LETTER) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB8: Numeric × Numeric */
    if (prev_wb == WBP_NUMERIC && next_wb == WBP_NUMERIC) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB9: AHLetter × Numeric */
    if (prev_ahletter && next_wb == WBP_NUMERIC) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB10: Numeric × AHLetter */
    if (prev_wb == WBP_NUMERIC && next_ahletter) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB12: Numeric × (MidNum | MidNumLetQ) Numeric (lookahead) */
    bool next_mid_num = (next_wb == WBP_MIDNUM || next_wb == WBP_MIDNUMLET ||
                         next_wb == WBP_SINGLE_QUOTE);
    if (prev_wb == WBP_NUMERIC && next_mid_num) {
      UnicodeWordBreak after = unicode__wb_lookahead(src, len, cur);
      if (after == WBP_NUMERIC) {
        prev_prev_wb = prev_wb;
        prev_wb = next_wb;
        cur += nc;
        ri_count = 0;
        continue;
      }
    }

    /* WB11: Numeric (MidNum | MidNumLetQ) × Numeric
       prev_prev_wb was Numeric, prev_wb is Mid, next is Numeric */
    bool prev_mid_num = (prev_wb == WBP_MIDNUM || prev_wb == WBP_MIDNUMLET ||
                         prev_wb == WBP_SINGLE_QUOTE);
    if (prev_prev_wb == WBP_NUMERIC && prev_mid_num && next_wb == WBP_NUMERIC) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB13: Katakana × Katakana */
    if (prev_wb == WBP_KATAKANA && next_wb == WBP_KATAKANA) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB13a: (AHLetter | Numeric | Katakana | ExtendNumLet) × ExtendNumLet */
    if (next_wb == WBP_EXTENDNUMLET &&
        (prev_ahletter || prev_wb == WBP_NUMERIC ||
         prev_wb == WBP_KATAKANA || prev_wb == WBP_EXTENDNUMLET)) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB13b: ExtendNumLet × (AHLetter | Numeric | Katakana) */
    if (prev_wb == WBP_EXTENDNUMLET &&
        (next_ahletter || next_wb == WBP_NUMERIC || next_wb == WBP_KATAKANA)) {
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      ri_count = 0;
      continue;
    }

    /* WB15/WB16: Regional_Indicator × Regional_Indicator (pairs) */
    if (prev_wb == WBP_REGIONAL_INDICATOR &&
        next_wb == WBP_REGIONAL_INDICATOR && (ri_count % 2) == 1) {
      ri_count++;
      prev_prev_wb = prev_wb;
      prev_wb = next_wb;
      cur += nc;
      continue;
    }

    /* WB999: Otherwise ÷ */
    break;
  }

  return cur;
}

/* Returns the byte offset of the previous word boundary before pos, or 0. */
static inline size_t unicode_word_prev(const uint8_t* src, size_t len, size_t pos) {
  if (pos == 0 || pos > len) return 0;

  /* Walk forward from start, collecting word boundaries until we pass pos */
  /* For efficiency, find a safe start point first */
  size_t safe = 0;
  {
    size_t s = pos;
    size_t limit = (pos > 512) ? pos - 512 : 0;
    while (s > limit) {
      s--;
      while (s > limit && utf8_is_continuation(src[s]))
        s--;
      uint32_t cp2;
      size_t nc2 = utf8_decode(src + s, len - s, &cp2);
      if (nc2 == 0) break;
      UnicodeWordBreak wb2 = unicode_word_break(cp2);
      if (wb2 == WBP_NEWLINE || wb2 == WBP_CR || wb2 == WBP_LF) {
        safe = s + nc2;
        break;
      }
    }
    if (s <= limit) safe = limit;
  }

  size_t last_boundary = safe;
  size_t scan = safe;
  while (scan < pos && scan < len) {
    size_t next = unicode_word_next(src, len, scan);
    if (next >= pos) break;
    last_boundary = next;
    if (next <= scan) break;
    scan = next;
  }

  return last_boundary;
}

/* Returns the word type of the segment containing pos.
   Classifies based on the first significant codepoint in the segment. */
static inline UnicodeWordType unicode_word_type_at(
    const uint8_t* src, size_t len, size_t pos) {
  if (pos >= len) return UNICODE_WORD_OTHER;

  /* Find the start of the word segment containing pos.
     If pos is itself a boundary, it starts a new segment. */
  size_t start;
  if (pos == 0) {
    start = 0;
  } else {
    /* Find the boundary at or before pos by checking if the next boundary
       from the previous boundary lands at or after pos */
    size_t prev = unicode_word_prev(src, len, pos);
    size_t next = unicode_word_next(src, len, prev);
    start = (next <= pos) ? next : prev;
  }

  /* Decode the first significant codepoint in the segment */
  size_t p = start;
  while (p < len) {
    uint32_t cp;
    size_t nc = utf8_decode(src + p, len - p, &cp);
    if (nc == 0) { p++; continue; }

    UnicodeWordBreak wb = unicode_word_break(cp);

    /* Skip Extend/Format/ZWJ — they don't determine type */
    if (wb == WBP_EXTEND || wb == WBP_FORMAT || wb == WBP_ZWJ) {
      p += nc;
      continue;
    }

    switch (wb) {
      case WBP_ALETTER:
      case WBP_HEBREW_LETTER:
      case WBP_KATAKANA:
        return UNICODE_WORD_LETTER;
      case WBP_NUMERIC:
        return UNICODE_WORD_NUMERIC;
      case WBP_WSEGSPACE:
      case WBP_CR:
      case WBP_LF:
      case WBP_NEWLINE:
        return UNICODE_WORD_SPACE;
      case WBP_MIDLETTER:
      case WBP_MIDNUM:
      case WBP_MIDNUMLET:
      case WBP_SINGLE_QUOTE:
      case WBP_DOUBLE_QUOTE:
        return UNICODE_WORD_PUNCTUATION;
      default:
        return UNICODE_WORD_OTHER;
    }
  }
  return UNICODE_WORD_OTHER;
}

/* -----------------------------------------------------------------------
 * Line break opportunity detection (UAX #14)
 * ----------------------------------------------------------------------- */

/* Internal: resolve ambiguous/complex line break classes per UAX #14.
   AI, SA, CJ, XX -> AL for non-tailored breaking. SG -> AL. */
static inline UnicodeLineBreak unicode__lb_resolve(UnicodeLineBreak lb) {
  switch (lb) {
    case LBP_AI: return LBP_AL;
    case LBP_SA: return LBP_AL;
    case LBP_CJ: return LBP_NS;
    case LBP_XX: return LBP_AL;
    default: return lb;
  }
}

/* Internal: get resolved line break class for a codepoint */
static inline UnicodeLineBreak unicode__lb_class(uint32_t cp) {
  return unicode__lb_resolve(unicode_line_break(cp));
}

/* Returns the byte offset of the next line break opportunity after pos, or len.
   Implements core UAX #14 line break rules. */
static inline size_t unicode_linebreak_next(const uint8_t* src, size_t len, size_t pos) {
  if (pos >= len) return len;

  uint32_t cp;
  size_t consumed = utf8_decode(src + pos, len - pos, &cp);
  if (consumed == 0) return pos + 1;

  size_t cur = pos + consumed;
  UnicodeLineBreak prev_lb = unicode__lb_class(cp);

  /* LB4: BK ! — always break after BK */
  if (prev_lb == LBP_BK) return cur;

  /* LB5: CR × LF, CR ! , LF ! , NL ! */
  if (prev_lb == LBP_CR) {
    if (cur < len) {
      uint32_t next;
      size_t nc = utf8_decode(src + cur, len - cur, &next);
      if (nc > 0 && unicode__lb_class(next) == LBP_LF)
        return cur + nc; /* break after CR LF */
    }
    return cur; /* break after CR */
  }
  if (prev_lb == LBP_LF || prev_lb == LBP_NL)
    return cur;

  /* Track state for LB21a: HL (HY|BA) × */
  bool after_hl_hy_ba = false;
  int ri_count = (prev_lb == LBP_RI) ? 1 : 0;

  while (cur < len) {
    /* LB4/LB5: break after BK, CR, LF, NL (handling CR×LF) */
    if (prev_lb == LBP_BK) return cur;
    if (prev_lb == LBP_CR) {
      uint32_t peek;
      size_t pnc = utf8_decode(src + cur, len - cur, &peek);
      if (pnc > 0 && unicode__lb_class(peek) == LBP_LF) {
        return cur + pnc; /* break after CR LF */
      }
      return cur; /* break after lone CR */
    }
    if (prev_lb == LBP_LF || prev_lb == LBP_NL)
      return cur;

    uint32_t next_cp;
    size_t nc = utf8_decode(src + cur, len - cur, &next_cp);
    if (nc == 0) break;

    UnicodeLineBreak next_lb = unicode__lb_class(next_cp);

    /* LB6: × (BK | CR | LF | NL) — do not break before hard breaks */
    if (next_lb == LBP_BK || next_lb == LBP_CR ||
        next_lb == LBP_LF || next_lb == LBP_NL) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue; /* will break after via LB4/LB5 at top of loop */
    }

    /* LB7: × SP, × ZW — do not break before spaces/ZW */
    if (next_lb == LBP_SP || next_lb == LBP_ZW) {
      prev_lb = next_lb;
      cur += nc;
      /* Don't reset after_hl_hy_ba for SP */
      ri_count = 0;
      continue;
    }

    /* LB8: ZW SP* ÷ — break after ZW and any following spaces */
    if (prev_lb == LBP_ZW) {
      break; /* break opportunity after ZW SP* */
    }

    /* LB8a: ZWJ × — do not break after ZWJ */
    if (prev_lb == LBP_ZWJ) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = (next_lb == LBP_RI) ? 1 : 0;
      continue;
    }

    /* LB9: Treat X (CM|ZWJ)* as if it were X (where X is not SP, BK, CR, LF, NL, ZW) */
    if (next_lb == LBP_CM || next_lb == LBP_ZWJ) {
      if (prev_lb != LBP_SP) {
        /* Transparent — don't update prev_lb */
        cur += nc;
        ri_count = 0;
        continue;
      }
    }

    /* LB10: Treat any remaining CM or ZWJ as AL */
    if (next_lb == LBP_CM || next_lb == LBP_ZWJ) {
      next_lb = LBP_AL;
    }

    /* LB11: × WJ, WJ × */
    if (next_lb == LBP_WJ || prev_lb == LBP_WJ) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB12: GL × */
    if (prev_lb == LBP_GL) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = (next_lb == LBP_RI) ? 1 : 0;
      continue;
    }

    /* LB12a: [^SP BA HY] × GL */
    if (next_lb == LBP_GL &&
        prev_lb != LBP_SP && prev_lb != LBP_BA && prev_lb != LBP_HY) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB13: × CL, × CP, × EX, × IS, × SY */
    if (next_lb == LBP_CL || next_lb == LBP_CP ||
        next_lb == LBP_EX || next_lb == LBP_IS || next_lb == LBP_SY) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB14: OP SP* × */
    if (prev_lb == LBP_OP) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = (next_lb == LBP_RI) ? 1 : 0;
      continue;
    }

    /* LB15: QU SP* × OP */
    /* LB15 is complex — simplified: QU × OP */
    if (prev_lb == LBP_QU && next_lb == LBP_OP) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB16: (CL | CP) SP* × NS */
    if ((prev_lb == LBP_CL || prev_lb == LBP_CP) && next_lb == LBP_NS) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB17: B2 SP* × B2 */
    if (prev_lb == LBP_B2 && next_lb == LBP_B2) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB18: SP ÷ — break after spaces (that weren't caught by LB7/LB14/etc) */
    if (prev_lb == LBP_SP)
      break;

    /* LB19: × QU, QU × */
    if (next_lb == LBP_QU || prev_lb == LBP_QU) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = (next_lb == LBP_RI) ? 1 : 0;
      continue;
    }

    /* LB20: ÷ CB, CB ÷ */
    if (next_lb == LBP_CB || prev_lb == LBP_CB)
      break;

    /* LB21: × BA, × HY, × NS, BB × */
    if (next_lb == LBP_BA || next_lb == LBP_HY || next_lb == LBP_NS ||
        prev_lb == LBP_BB) {
      bool this_after = (prev_lb == LBP_HL &&
                         (next_lb == LBP_HY || next_lb == LBP_BA));
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = this_after;
      ri_count = 0;
      continue;
    }

    /* LB21a: HL (HY | BA) × */
    if (after_hl_hy_ba) {
      after_hl_hy_ba = false;
      prev_lb = next_lb;
      cur += nc;
      ri_count = (next_lb == LBP_RI) ? 1 : 0;
      continue;
    }

    /* LB21b: SY × HL */
    if (prev_lb == LBP_SY && next_lb == LBP_HL) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB22: × IN */
    if (next_lb == LBP_IN) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB23: (AL | HL) × NU, NU × (AL | HL) */
    if ((prev_lb == LBP_AL || prev_lb == LBP_HL) && next_lb == LBP_NU) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if (prev_lb == LBP_NU && (next_lb == LBP_AL || next_lb == LBP_HL)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB23a: PR × (ID | EB | EM), (ID | EB | EM) × PO */
    if (prev_lb == LBP_PR &&
        (next_lb == LBP_ID || next_lb == LBP_EB || next_lb == LBP_EM)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if ((prev_lb == LBP_ID || prev_lb == LBP_EB || prev_lb == LBP_EM) &&
        next_lb == LBP_PO) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB24: (PR | PO) × (AL | HL), (AL | HL) × (PR | PO) */
    if ((prev_lb == LBP_PR || prev_lb == LBP_PO) &&
        (next_lb == LBP_AL || next_lb == LBP_HL)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if ((prev_lb == LBP_AL || prev_lb == LBP_HL) &&
        (next_lb == LBP_PR || next_lb == LBP_PO)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB25: (CL | CP | NU) × (PO | PR),
             (PO | PR) × OP,
             (PO | PR | HY | IS | NU | SY) × NU */
    if ((prev_lb == LBP_CL || prev_lb == LBP_CP || prev_lb == LBP_NU) &&
        (next_lb == LBP_PO || next_lb == LBP_PR)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if ((prev_lb == LBP_PO || prev_lb == LBP_PR) && next_lb == LBP_OP) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if ((prev_lb == LBP_PO || prev_lb == LBP_PR || prev_lb == LBP_HY ||
         prev_lb == LBP_IS || prev_lb == LBP_NU || prev_lb == LBP_SY) &&
        next_lb == LBP_NU) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB26: JL × (JL | JV | H2 | H3), (JV | H2) × (JV | JT),
             (JT | H3) × JT */
    if (prev_lb == LBP_JL &&
        (next_lb == LBP_JL || next_lb == LBP_JV ||
         next_lb == LBP_H2 || next_lb == LBP_H3)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if ((prev_lb == LBP_JV || prev_lb == LBP_H2) &&
        (next_lb == LBP_JV || next_lb == LBP_JT)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if ((prev_lb == LBP_JT || prev_lb == LBP_H3) && next_lb == LBP_JT) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB27: (JL | JV | JT | H2 | H3) × PO, PR × (JL | JV | JT | H2 | H3) */
    if ((prev_lb == LBP_JL || prev_lb == LBP_JV || prev_lb == LBP_JT ||
         prev_lb == LBP_H2 || prev_lb == LBP_H3) && next_lb == LBP_PO) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if (prev_lb == LBP_PR &&
        (next_lb == LBP_JL || next_lb == LBP_JV || next_lb == LBP_JT ||
         next_lb == LBP_H2 || next_lb == LBP_H3)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB28: (AL | HL) × (AL | HL) */
    if ((prev_lb == LBP_AL || prev_lb == LBP_HL) &&
        (next_lb == LBP_AL || next_lb == LBP_HL)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB29: IS × (AL | HL) */
    if (prev_lb == LBP_IS && (next_lb == LBP_AL || next_lb == LBP_HL)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB30: (AL | HL | NU) × OP(ea=!F), CP(ea=!F) × (AL | HL | NU)
       Simplified: (AL | HL | NU) × OP, CP × (AL | HL | NU) */
    if ((prev_lb == LBP_AL || prev_lb == LBP_HL || prev_lb == LBP_NU) &&
        next_lb == LBP_OP) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }
    if (prev_lb == LBP_CP &&
        (next_lb == LBP_AL || next_lb == LBP_HL || next_lb == LBP_NU)) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB30a: RI × RI (pairs) */
    if (prev_lb == LBP_RI && next_lb == LBP_RI && (ri_count % 2) == 1) {
      ri_count++;
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      continue;
    }

    /* LB30b: EB × EM */
    if (prev_lb == LBP_EB && next_lb == LBP_EM) {
      prev_lb = next_lb;
      cur += nc;
      after_hl_hy_ba = false;
      ri_count = 0;
      continue;
    }

    /* LB31: ALL ÷ ALL — break allowed */
    break;
  }

  return cur;
}

/* Returns the byte offset of the previous line break opportunity before pos, or 0. */
static inline size_t unicode_linebreak_prev(const uint8_t* src, size_t len, size_t pos) {
  if (pos == 0 || pos > len) return 0;

  /* Walk forward from a safe starting point collecting break opportunities */
  size_t safe = 0;
  {
    size_t s = pos;
    size_t limit = (pos > 512) ? pos - 512 : 0;
    while (s > limit) {
      s--;
      while (s > limit && utf8_is_continuation(src[s]))
        s--;
      uint32_t cp2;
      size_t nc2 = utf8_decode(src + s, len - s, &cp2);
      if (nc2 == 0) break;
      UnicodeLineBreak lb2 = unicode__lb_class(cp2);
      if (lb2 == LBP_BK || lb2 == LBP_CR || lb2 == LBP_LF || lb2 == LBP_NL) {
        safe = s + nc2;
        break;
      }
    }
    if (s <= limit) safe = limit;
  }

  size_t last_boundary = safe;
  size_t scan = safe;
  while (scan < pos && scan < len) {
    size_t next = unicode_linebreak_next(src, len, scan);
    if (next >= pos) break;
    last_boundary = next;
    if (next <= scan) break;
    scan = next;
  }

  return last_boundary;
}

/* Returns true if the break at pos is mandatory (BK, CR, LF, NL). */
static inline bool unicode_linebreak_is_mandatory(const uint8_t* src, size_t len, size_t pos) {
  if (pos == 0 || pos > len) return false;

  /* Look at the codepoint just before pos */
  size_t p = pos;
  while (p > 0 && utf8_is_continuation(src[p - 1]))
    p--;
  if (p > 0) p--;

  uint32_t cp;
  size_t nc = utf8_decode(src + p, len - p, &cp);
  if (nc == 0) return false;

  UnicodeLineBreak lb = unicode__lb_class(cp);
  if (lb == LBP_BK || lb == LBP_LF || lb == LBP_NL)
    return true;

  /* CR is mandatory unless followed by LF (already consumed as CR LF) */
  if (lb == LBP_CR) {
    /* If pos is after CR, check if the CR was followed by LF */
    if (pos < len) {
      uint32_t next;
      size_t nc2 = utf8_decode(src + pos, len - pos, &next);
      if (nc2 > 0 && unicode__lb_class(next) == LBP_LF)
        return false; /* Not a mandatory break between CR and LF */
    }
    return true;
  }

  return false;
}

#endif /* UNICODE_H */
