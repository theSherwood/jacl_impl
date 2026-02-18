#ifndef RDOC_H
#define RDOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "utf8.h"

/* ========================================================================
 * Sentinel byte encoding for rich text document (rdoc)
 *
 * Sentinel bytes use 0xFB-0xFF which are never valid UTF-8 lead bytes.
 * Each sentinel has a lead byte followed by a fixed-size payload.
 *
 * Block tokens:   [lead 1B] [id 4B LE]           = 5 bytes
 * Inline tokens:  [lead 1B] [id 4B LE]           = 5 bytes
 * Mark change:    [lead 1B] [flags 2B LE] [app_id 4B LE] = 7 bytes
 * ======================================================================== */

/* --- Sentinel lead bytes --- */

#define RDOC_SENTINEL_BLOCK_OPEN   ((uint8_t)0xFF)
#define RDOC_SENTINEL_BLOCK_CLOSE  ((uint8_t)0xFE)
#define RDOC_SENTINEL_INLINE_OPEN  ((uint8_t)0xFD)
#define RDOC_SENTINEL_INLINE_CLOSE ((uint8_t)0xFC)
#define RDOC_SENTINEL_MARK_CHANGE  ((uint8_t)0xFB)

/* --- Classification helpers --- */

static inline bool rdoc_is_sentinel(uint8_t byte) {
  return byte >= 0xFB;
}

/* Returns total sentinel sequence size for a lead byte, or 0 if not a sentinel */
static inline size_t rdoc_sentinel_size(uint8_t lead_byte) {
  if (lead_byte >= 0xFC) return 5;  /* block/inline open/close: 1 lead + 4 id */
  if (lead_byte == 0xFB) return 7;  /* mark change: 1 lead + 2 flags + 4 app_id */
  return 0;
}

/* --- Encode functions: write to caller-provided buffer, return bytes written --- */

static inline size_t rdoc_encode_block_open(uint8_t* buf, uint32_t id) {
  buf[0] = RDOC_SENTINEL_BLOCK_OPEN;
  memcpy(buf + 1, &id, 4);  /* LE on LE platforms; portable via memcpy */
  return 5;
}

static inline size_t rdoc_encode_block_close(uint8_t* buf, uint32_t id) {
  buf[0] = RDOC_SENTINEL_BLOCK_CLOSE;
  memcpy(buf + 1, &id, 4);
  return 5;
}

static inline size_t rdoc_encode_inline_open(uint8_t* buf, uint32_t id) {
  buf[0] = RDOC_SENTINEL_INLINE_OPEN;
  memcpy(buf + 1, &id, 4);
  return 5;
}

static inline size_t rdoc_encode_inline_close(uint8_t* buf, uint32_t id) {
  buf[0] = RDOC_SENTINEL_INLINE_CLOSE;
  memcpy(buf + 1, &id, 4);
  return 5;
}

static inline size_t rdoc_encode_mark_change(uint8_t* buf, uint16_t flags, uint32_t app_id) {
  buf[0] = RDOC_SENTINEL_MARK_CHANGE;
  memcpy(buf + 1, &flags, 2);
  memcpy(buf + 3, &app_id, 4);
  return 7;
}

/* --- Decode functions: read from buffer, return bytes consumed --- */

static inline size_t rdoc_decode_block_open(const uint8_t* buf, uint32_t* out_id) {
  if (buf[0] != RDOC_SENTINEL_BLOCK_OPEN) return 0;
  memcpy(out_id, buf + 1, 4);
  return 5;
}

static inline size_t rdoc_decode_block_close(const uint8_t* buf, uint32_t* out_id) {
  if (buf[0] != RDOC_SENTINEL_BLOCK_CLOSE) return 0;
  memcpy(out_id, buf + 1, 4);
  return 5;
}

static inline size_t rdoc_decode_inline_open(const uint8_t* buf, uint32_t* out_id) {
  if (buf[0] != RDOC_SENTINEL_INLINE_OPEN) return 0;
  memcpy(out_id, buf + 1, 4);
  return 5;
}

static inline size_t rdoc_decode_inline_close(const uint8_t* buf, uint32_t* out_id) {
  if (buf[0] != RDOC_SENTINEL_INLINE_CLOSE) return 0;
  memcpy(out_id, buf + 1, 4);
  return 5;
}

static inline size_t rdoc_decode_mark_change(const uint8_t* buf, uint16_t* out_flags, uint32_t* out_app_id) {
  if (buf[0] != RDOC_SENTINEL_MARK_CHANGE) return 0;
  memcpy(out_flags, buf + 1, 2);
  memcpy(out_app_id, buf + 3, 4);
  return 7;
}

/* --- Safe split: adjust position backward to avoid splitting sentinels or UTF-8 --- */

/* Given a byte buffer and a proposed split position, adjust backward so
   the split does not land inside a sentinel sequence or a multi-byte
   UTF-8 codepoint. Returns adjusted position. */
static inline size_t rdoc_find_safe_split(const uint8_t* buf, size_t len, size_t pos) {
  if (pos == 0 || pos >= len) return pos;

  /* Check if we're inside a sentinel sequence by scanning backward.
     Sentinel payload bytes can be ANY value, so we must check every
     position that could be a sentinel lead within range. Max sentinel
     is 7 bytes (mark_change), so a lead up to 6 bytes before pos
     could still contain pos in its payload. */
  for (size_t lookback = 1; lookback <= 6 && lookback <= pos; lookback++) {
    size_t candidate = pos - lookback;
    uint8_t byte = buf[candidate];
    if (rdoc_is_sentinel(byte)) {
      size_t seq_size = rdoc_sentinel_size(byte);
      /* If pos falls inside this sentinel's range, move before it */
      if (candidate + seq_size > pos) {
        pos = candidate;
      }
      break;  /* Found a sentinel lead; done */
    }
  }

  /* Now handle UTF-8 continuation bytes */
  return utf8_find_safe_split(buf, len, pos);
}

/* ========================================================================
 * rdoc_summary: monoid for sum_tree aggregation
 *
 * Tracks text metrics (bytes, chars, lines), structural element counts
 * (block/inline opens), depth deltas with prefix-sum min/max, and
 * last-writer-wins mark state.
 * ======================================================================== */

typedef struct rdoc_summary {
  size_t   text_bytes;          /* text bytes (excluding sentinels) */
  size_t   chars;               /* codepoint count of text bytes */
  size_t   lines;               /* newline count in text bytes */
  size_t   block_opens;         /* count of BLOCK_OPEN sentinels */
  size_t   inline_opens;        /* count of INLINE_OPEN sentinels */
  int32_t  block_depth_delta;   /* net block depth change (opens - closes) */
  int32_t  block_max_depth;     /* max relative block depth reached */
  int32_t  block_min_depth;     /* min relative block depth reached */
  int32_t  inline_depth_delta;  /* net inline depth change */
  int32_t  inline_max_depth;    /* max relative inline depth reached */
  int32_t  inline_min_depth;    /* min relative inline depth reached */
  uint16_t last_mark_flags;     /* flags from most recent MARK_CHANGE */
  uint32_t last_mark_id;        /* app_id from most recent MARK_CHANGE */
  bool     has_mark_change;     /* whether any MARK_CHANGE exists */
} rdoc_summary;

static inline rdoc_summary rdoc_summary_identity(void) {
  return (rdoc_summary){0};
}

static inline int32_t rdoc_max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t rdoc_min_i32(int32_t a, int32_t b) { return a < b ? a : b; }

static inline rdoc_summary rdoc_summary_combine(rdoc_summary a, rdoc_summary b) {
  return (rdoc_summary){
    .text_bytes         = a.text_bytes + b.text_bytes,
    .chars              = a.chars + b.chars,
    .lines              = a.lines + b.lines,
    .block_opens        = a.block_opens + b.block_opens,
    .inline_opens       = a.inline_opens + b.inline_opens,
    .block_depth_delta  = a.block_depth_delta + b.block_depth_delta,
    .block_max_depth    = rdoc_max_i32(a.block_max_depth, a.block_depth_delta + b.block_max_depth),
    .block_min_depth    = rdoc_min_i32(a.block_min_depth, a.block_depth_delta + b.block_min_depth),
    .inline_depth_delta = a.inline_depth_delta + b.inline_depth_delta,
    .inline_max_depth   = rdoc_max_i32(a.inline_max_depth, a.inline_depth_delta + b.inline_max_depth),
    .inline_min_depth   = rdoc_min_i32(a.inline_min_depth, a.inline_depth_delta + b.inline_min_depth),
    .last_mark_flags    = b.has_mark_change ? b.last_mark_flags : a.last_mark_flags,
    .last_mark_id       = b.has_mark_change ? b.last_mark_id : a.last_mark_id,
    .has_mark_change    = a.has_mark_change || b.has_mark_change
  };
}

/* Parse byte array, stepping over sentinels, to produce a summary */
static inline rdoc_summary rdoc_summary_summarize(const uint8_t* elems, size_t count) {
  rdoc_summary s = rdoc_summary_identity();
  size_t i = 0;
  int32_t block_depth = 0;
  int32_t inline_depth = 0;

  while (i < count) {
    if (rdoc_is_sentinel(elems[i])) {
      uint8_t lead = elems[i];
      size_t seq_size = rdoc_sentinel_size(lead);
      if (i + seq_size > count) break; /* truncated sentinel */

      switch (lead) {
        case RDOC_SENTINEL_BLOCK_OPEN:
          s.block_opens++;
          block_depth++;
          if (block_depth > s.block_max_depth) s.block_max_depth = block_depth;
          break;
        case RDOC_SENTINEL_BLOCK_CLOSE:
          block_depth--;
          if (block_depth < s.block_min_depth) s.block_min_depth = block_depth;
          break;
        case RDOC_SENTINEL_INLINE_OPEN:
          s.inline_opens++;
          inline_depth++;
          if (inline_depth > s.inline_max_depth) s.inline_max_depth = inline_depth;
          break;
        case RDOC_SENTINEL_INLINE_CLOSE:
          inline_depth--;
          if (inline_depth < s.inline_min_depth) s.inline_min_depth = inline_depth;
          break;
        case RDOC_SENTINEL_MARK_CHANGE: {
          uint16_t flags;
          uint32_t app_id;
          rdoc_decode_mark_change(elems + i, &flags, &app_id);
          s.has_mark_change = true;
          s.last_mark_flags = flags;
          s.last_mark_id = app_id;
          break;
        }
      }
      i += seq_size;
    } else {
      /* Text run: scan forward to end of text */
      size_t text_start = i;
      while (i < count && !rdoc_is_sentinel(elems[i])) {
        i++;
      }
      size_t text_len = i - text_start;
      s.text_bytes += text_len;
      s.chars += utf8_codepoint_count(elems + text_start, text_len);
      s.lines += utf8_line_count(elems + text_start, text_len);
    }
  }

  s.block_depth_delta = block_depth;
  s.inline_depth_delta = inline_depth;
  return s;
}

/* ========================================================================
 * sum_tree instantiation: rdoc_st (uint8_t elements, rdoc_summary)
 * ======================================================================== */

#ifndef RDOC_LEAF_MAX
#define RDOC_LEAF_MAX 256
#endif
#define RDOC_LEAF_BYTES RDOC_LEAF_MAX

#define STREE_T          uint8_t
#define STREE_SUMMARY_T  rdoc_summary
#define STREE_NAME       rdoc_st
#undef STREE_LEAF_MAX
#define STREE_LEAF_MAX   RDOC_LEAF_MAX

#include "sum_tree.h"

/* --- Public rdoc type --- */

typedef struct {
  rdoc_st_root root;
} rdoc;

/* --- Custom leaf search for sentinel-aware byte streams --- */

static inline size_t rdoc_leaf_search(const uint8_t* elements, size_t count,
                                       rdoc_summary* accumulated,
                                       size_t (*dimension_fn)(rdoc_summary),
                                       size_t target) {
  size_t pos = 0;
  while (pos < count) {
    if (rdoc_is_sentinel(elements[pos])) {
      size_t seq_size = rdoc_sentinel_size(elements[pos]);
      if (pos + seq_size > count) break;

      /* Build single-sentinel summary */
      rdoc_summary s = rdoc_summary_identity();
      switch (elements[pos]) {
        case RDOC_SENTINEL_BLOCK_OPEN:
          s.block_opens = 1;
          s.block_depth_delta = 1;
          s.block_max_depth = 1;
          break;
        case RDOC_SENTINEL_BLOCK_CLOSE:
          s.block_depth_delta = -1;
          s.block_min_depth = -1;
          break;
        case RDOC_SENTINEL_INLINE_OPEN:
          s.inline_opens = 1;
          s.inline_depth_delta = 1;
          s.inline_max_depth = 1;
          break;
        case RDOC_SENTINEL_INLINE_CLOSE:
          s.inline_depth_delta = -1;
          s.inline_min_depth = -1;
          break;
        case RDOC_SENTINEL_MARK_CHANGE: {
          uint16_t flags;
          uint32_t app_id;
          rdoc_decode_mark_change(elements + pos, &flags, &app_id);
          s.has_mark_change = true;
          s.last_mark_flags = flags;
          s.last_mark_id = app_id;
          break;
        }
      }

      rdoc_summary combined = rdoc_summary_combine(*accumulated, s);
      if (dimension_fn(combined) > target) return pos;
      *accumulated = combined;
      pos += seq_size;
    } else {
      /* Text run: scan forward to find extent */
      size_t text_start = pos;
      while (pos < count && !rdoc_is_sentinel(elements[pos])) pos++;
      size_t text_len = pos - text_start;

      /* Check if entire run crosses target */
      rdoc_summary run_s = rdoc_summary_identity();
      run_s.text_bytes = text_len;
      run_s.chars = utf8_codepoint_count(elements + text_start, text_len);
      run_s.lines = utf8_line_count(elements + text_start, text_len);

      rdoc_summary combined = rdoc_summary_combine(*accumulated, run_s);
      if (dimension_fn(combined) <= target) {
        /* Entire run doesn't cross — accumulate and continue */
        *accumulated = combined;
      } else {
        /* Target within this text run — scan byte by byte */
        for (size_t i = text_start; i < text_start + text_len; i++) {
          rdoc_summary byte_s = rdoc_summary_identity();
          byte_s.text_bytes = 1;
          byte_s.chars = utf8_is_continuation(elements[i]) ? 0 : 1;
          byte_s.lines = (elements[i] == '\n') ? 1 : 0;

          combined = rdoc_summary_combine(*accumulated, byte_s);
          if (dimension_fn(combined) > target) return i;
          *accumulated = combined;
        }
      }
    }
  }
  return count; /* Not found within leaf */
}

/* --- Lifecycle --- */

static inline rdoc rdoc_new(void) {
  rdoc_st_set_handlers((rdoc_st_handlers){
    .identity   = rdoc_summary_identity,
    .combine    = rdoc_summary_combine,
    .summarize  = rdoc_summary_summarize,
    .leaf_search = rdoc_leaf_search
  });
  return (rdoc){.root = rdoc_st_empty()};
}

static inline rdoc rdoc_ref(rdoc d) {
  return (rdoc){.root = rdoc_st_ref(d.root)};
}

static inline void rdoc_unref(rdoc d) {
  rdoc_st_unref(d.root);
}

/* --- Validation: check no truncated sentinels --- */

static inline bool rdoc_validate_buffer(const uint8_t* buf, size_t len) {
  size_t i = 0;
  while (i < len) {
    if (rdoc_is_sentinel(buf[i])) {
      size_t seq_size = rdoc_sentinel_size(buf[i]);
      if (i + seq_size > len) return false;
      i += seq_size;
    } else {
      i++;
    }
  }
  return true;
}

/* --- Construction from byte stream --- */

static inline rdoc rdoc_from_bytes(const uint8_t* buf, size_t len) {
  rdoc_st_set_handlers((rdoc_st_handlers){
    .identity   = rdoc_summary_identity,
    .combine    = rdoc_summary_combine,
    .summarize  = rdoc_summary_summarize,
    .leaf_search = rdoc_leaf_search
  });

  if (len == 0 || !rdoc_validate_buffer(buf, len)) {
    return (rdoc){.root = rdoc_st_empty()};
  }

  /* Build leaves with safe split boundaries */
  rdoc_st_node* stack_buf[512];
  rdoc_st_node** leaf_buf = stack_buf;
  size_t est_max = (len + RDOC_LEAF_BYTES - 1) / RDOC_LEAF_BYTES + 1;
  if (est_max > 512) {
    leaf_buf = (rdoc_st_node**)rdoc_st_allocator.alloc(
        rdoc_st_allocator.ctx, sizeof(rdoc_st_node*) * est_max);
  }

  size_t n_leaves = 0;
  size_t pos = 0;

  while (pos < len) {
    size_t end = pos + RDOC_LEAF_BYTES;
    if (end >= len) {
      end = len;
    } else {
      end = rdoc_find_safe_split(buf, len, end);
    }
    leaf_buf[n_leaves++] = (rdoc_st_node*)rdoc_st_mk_leaf(buf + pos, end - pos);
    pos = end;
  }

  /* Assemble tree from leaves */
  rdoc_st_root root = rdoc_st_mk_root(leaf_buf, n_leaves, 1);

  /* mk_root refs each node; release creation refs */
  for (size_t i = 0; i < n_leaves; i++) {
    rdoc_st_rc_unref(leaf_buf[i]);
  }

  if (leaf_buf != stack_buf) {
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, leaf_buf);
  }

  return (rdoc){.root = root};
}

/* --- O(1) summary queries --- */

static inline size_t rdoc_total_bytes(rdoc d) {
  return rdoc_st_count(d.root);
}

static inline size_t rdoc_text_byte_count(rdoc d) {
  return rdoc_st_summary(d.root).text_bytes;
}

static inline size_t rdoc_char_count(rdoc d) {
  return rdoc_st_summary(d.root).chars;
}

static inline size_t rdoc_line_count(rdoc d) {
  return rdoc_st_summary(d.root).lines;
}

static inline size_t rdoc_block_count(rdoc d) {
  return rdoc_st_summary(d.root).block_opens;
}

static inline size_t rdoc_inline_count(rdoc d) {
  return rdoc_st_summary(d.root).inline_opens;
}

/* ========================================================================
 * rdoc_builder: growable byte buffer for programmatic document construction
 *
 * Stack-allocated up to 1024 bytes, heap-allocated beyond that.
 * Does NOT validate structural correctness (matched open/close) —
 * that's the application's responsibility.
 * ======================================================================== */

#define RDOC_BUILDER_INLINE_CAP 1024

typedef struct {
  uint8_t  inline_buf[RDOC_BUILDER_INLINE_CAP];
  uint8_t* buf;     /* points to inline_buf or heap allocation */
  size_t   len;
  size_t   cap;
  bool     valid;
} rdoc_builder;

static inline rdoc_builder rdoc_builder_new(void) {
  rdoc_builder b;
  b.buf   = b.inline_buf;
  b.len   = 0;
  b.cap   = RDOC_BUILDER_INLINE_CAP;
  b.valid = true;
  return b;
}

static inline void rdoc_builder_free(rdoc_builder* b) {
  if (b->buf != b->inline_buf) {
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, b->buf);
  }
  b->buf   = b->inline_buf;
  b->len   = 0;
  b->cap   = RDOC_BUILDER_INLINE_CAP;
  b->valid = false;
}

/* Ensure capacity for `extra` more bytes */
static inline bool rdoc_builder_ensure(rdoc_builder* b, size_t extra) {
  if (b->len + extra <= b->cap) return true;
  size_t new_cap = b->cap * 2;
  if (new_cap < b->len + extra) new_cap = b->len + extra;
  uint8_t* new_buf = (uint8_t*)rdoc_st_allocator.alloc(
      rdoc_st_allocator.ctx, new_cap);
  if (!new_buf) return false;
  memcpy(new_buf, b->buf, b->len);
  if (b->buf != b->inline_buf) {
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, b->buf);
  }
  b->buf = new_buf;
  b->cap = new_cap;
  return true;
}

static inline void rdoc_builder_block_open(rdoc_builder* b, uint32_t id) {
  if (!b->valid) return;
  rdoc_builder_ensure(b, 5);
  b->len += rdoc_encode_block_open(b->buf + b->len, id);
}

static inline void rdoc_builder_block_close(rdoc_builder* b, uint32_t id) {
  if (!b->valid) return;
  rdoc_builder_ensure(b, 5);
  b->len += rdoc_encode_block_close(b->buf + b->len, id);
}

static inline void rdoc_builder_inline_open(rdoc_builder* b, uint32_t id) {
  if (!b->valid) return;
  rdoc_builder_ensure(b, 5);
  b->len += rdoc_encode_inline_open(b->buf + b->len, id);
}

static inline void rdoc_builder_inline_close(rdoc_builder* b, uint32_t id) {
  if (!b->valid) return;
  rdoc_builder_ensure(b, 5);
  b->len += rdoc_encode_inline_close(b->buf + b->len, id);
}

static inline void rdoc_builder_mark(rdoc_builder* b, uint16_t flags, uint32_t app_id) {
  if (!b->valid) return;
  rdoc_builder_ensure(b, 7);
  b->len += rdoc_encode_mark_change(b->buf + b->len, flags, app_id);
}

static inline bool rdoc_builder_text(rdoc_builder* b, const uint8_t* str, size_t byte_len) {
  if (!b->valid) return false;
  if (byte_len == 0) return true;
  if (!utf8_validate(str, byte_len)) return false;
  rdoc_builder_ensure(b, byte_len);
  memcpy(b->buf + b->len, str, byte_len);
  b->len += byte_len;
  return true;
}

static inline rdoc rdoc_builder_finish(rdoc_builder* b) {
  if (!b->valid) return rdoc_new();
  rdoc d = rdoc_from_bytes(b->buf, b->len);
  /* Invalidate builder and release resources */
  if (b->buf != b->inline_buf) {
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, b->buf);
  }
  b->buf   = b->inline_buf;
  b->len   = 0;
  b->cap   = RDOC_BUILDER_INLINE_CAP;
  b->valid = false;
  return d;
}

/* ========================================================================
 * Dimensional seeking, position queries, and depth/mark queries
 * ======================================================================== */

/* --- Dimension extractors for search-by-summary --- */

static inline size_t rdoc_dim_text_bytes(rdoc_summary s)   { return s.text_bytes; }
static inline size_t rdoc_dim_chars(rdoc_summary s)        { return s.chars; }
static inline size_t rdoc_dim_lines(rdoc_summary s)        { return s.lines; }
static inline size_t rdoc_dim_block_opens(rdoc_summary s)  { return s.block_opens; }
static inline size_t rdoc_dim_inline_opens(rdoc_summary s) { return s.inline_opens; }

/* --- Seek result --- */

typedef struct {
  size_t raw_index;
  bool   found;
} rdoc_seek_result;

/* --- Seek functions: convert dimensional offset to raw byte index --- */

static inline rdoc_seek_result rdoc_text_byte_to_raw(rdoc d, size_t text_byte_offset) {
  rdoc_st_search_result sr = rdoc_st_search(d.root, rdoc_dim_text_bytes, text_byte_offset);
  if (!sr.found) return (rdoc_seek_result){0, false};
  return (rdoc_seek_result){sr.index, true};
}

static inline rdoc_seek_result rdoc_char_to_raw(rdoc d, size_t char_offset) {
  rdoc_st_search_result sr = rdoc_st_search(d.root, rdoc_dim_chars, char_offset);
  if (!sr.found) return (rdoc_seek_result){0, false};
  return (rdoc_seek_result){sr.index, true};
}

static inline rdoc_seek_result rdoc_line_to_raw(rdoc d, size_t line_number) {
  if (line_number == 0) return (rdoc_seek_result){0, true};
  /* Search for (line_number-1)th newline; line starts at byte after it */
  rdoc_st_search_result sr = rdoc_st_search(d.root, rdoc_dim_lines, line_number - 1);
  if (!sr.found) return (rdoc_seek_result){0, false};
  return (rdoc_seek_result){sr.index + 1, true};
}

static inline rdoc_seek_result rdoc_block_to_raw(rdoc d, size_t block_index) {
  rdoc_st_search_result sr = rdoc_st_search(d.root, rdoc_dim_block_opens, block_index);
  if (!sr.found) return (rdoc_seek_result){0, false};
  return (rdoc_seek_result){sr.index, true};
}

static inline rdoc_seek_result rdoc_inline_to_raw(rdoc d, size_t inline_index) {
  rdoc_st_search_result sr = rdoc_st_search(d.root, rdoc_dim_inline_opens, inline_index);
  if (!sr.found) return (rdoc_seek_result){0, false};
  return (rdoc_seek_result){sr.index, true};
}

/* --- Internal: compute prefix summary up to raw byte index (O(log n)) --- */

static inline rdoc_summary _rdoc_prefix_at(rdoc d, size_t raw_index) {
  rdoc_summary accumulated = rdoc_summary_identity();
  if (!d.root.node || raw_index == 0) return accumulated;

  size_t total = rdoc_total_bytes(d);
  if (raw_index >= total) return rdoc_st_summary(d.root);

  rdoc_st_node* node = d.root.node;
  size_t remaining = raw_index;

  while (node->type == rdoc_st_NODE_INTERNAL) {
    rdoc_st_internal* n = (rdoc_st_internal*)node;
    bool found_child = false;
    for (size_t i = 0; i < n->n_children; i++) {
      if (remaining < n->child_counts[i]) {
        node = n->children[i];
        found_child = true;
        break;
      }
      rdoc_summary child_sum;
      if (n->children[i]->type == rdoc_st_NODE_LEAF)
        child_sum = ((rdoc_st_leaf*)n->children[i])->summary;
      else
        child_sum = ((rdoc_st_internal*)n->children[i])->summary;
      accumulated = rdoc_summary_combine(accumulated, child_sum);
      remaining -= n->child_counts[i];
    }
    if (!found_child) return accumulated;
  }

  /* Within leaf, summarize up to remaining bytes */
  if (remaining > 0) {
    rdoc_st_leaf* leaf = (rdoc_st_leaf*)node;
    rdoc_summary leaf_prefix = rdoc_summary_summarize(leaf->elements, remaining);
    accumulated = rdoc_summary_combine(accumulated, leaf_prefix);
  }

  return accumulated;
}

/* --- Depth queries --- */

static inline int32_t rdoc_block_depth_at(rdoc d, size_t raw_index) {
  return _rdoc_prefix_at(d, raw_index).block_depth_delta;
}

static inline int32_t rdoc_inline_depth_at(rdoc d, size_t raw_index) {
  return _rdoc_prefix_at(d, raw_index).inline_depth_delta;
}

/* --- Mark queries --- */

typedef struct {
  uint16_t flags;
  uint32_t app_id;
} rdoc_mark_result;

static inline rdoc_mark_result rdoc_mark_at(rdoc d, size_t raw_index) {
  rdoc_summary prefix = _rdoc_prefix_at(d, raw_index);
  return (rdoc_mark_result){
    .flags  = prefix.last_mark_flags,
    .app_id = prefix.last_mark_id
  };
}

/* ========================================================================
 * Token cursor for rendering iteration
 *
 * Traverses the rdoc byte stream yielding decoded tokens. Follows the
 * same path-stack pattern as rope_cursor for O(log n) initialization
 * and O(1) amortized advancement across leaves.
 * ======================================================================== */

/* --- Token types --- */

typedef enum {
  RDOC_TOKEN_BLOCK_OPEN,
  RDOC_TOKEN_BLOCK_CLOSE,
  RDOC_TOKEN_INLINE_OPEN,
  RDOC_TOKEN_INLINE_CLOSE,
  RDOC_TOKEN_MARK_CHANGE,
  RDOC_TOKEN_TEXT
} rdoc_token_kind;

typedef struct {
  rdoc_token_kind  kind;
  uint32_t         id;          /* block/inline ID or mark app_id */
  uint16_t         mark_flags;  /* only for MARK_CHANGE */
  const uint8_t*   text_ptr;    /* only for TEXT */
  size_t           text_len;    /* only for TEXT */
} rdoc_token;

/* --- Cursor struct --- */

#define RDOC_CURSOR_MAX_DEPTH 16

typedef struct {
  rdoc_st_node* root;
  struct {
    rdoc_st_node* node;
    size_t child_index;
  } path[RDOC_CURSOR_MAX_DEPTH];
  int depth;
  rdoc_st_leaf* leaf;
  size_t leaf_offset;
  rdoc_summary prefix;       /* summary of all content before current leaf */
  uint16_t cur_mark_flags;   /* current mark state tracked during iteration */
  uint32_t cur_mark_id;      /* current mark app_id tracked during iteration */
  bool valid;
} rdoc_cursor;

/* --- Helper: get summary from any rdoc_st node --- */

static inline rdoc_summary _rdoc_cursor_node_summary(rdoc_st_node* node) {
  if (node->type == rdoc_st_NODE_LEAF)
    return ((rdoc_st_leaf*)node)->summary;
  return ((rdoc_st_internal*)node)->summary;
}

/* --- Internal: move to next leaf --- */

static inline bool _rdoc_cursor_next_leaf(rdoc_cursor* c) {
  int d = c->depth;
  while (d > 0) {
    d--;
    rdoc_st_internal* parent = (rdoc_st_internal*)c->path[d].node;
    size_t next_child = c->path[d].child_index + 1;
    if (next_child < parent->n_children) {
      c->path[d].child_index = next_child;
      rdoc_st_node* node = parent->children[next_child];
      d++;
      while (node->type == rdoc_st_NODE_INTERNAL) {
        rdoc_st_internal* n = (rdoc_st_internal*)node;
        c->path[d].node = node;
        c->path[d].child_index = 0;
        node = n->children[0];
        d++;
      }
      c->depth = d;
      c->leaf = (rdoc_st_leaf*)node;
      c->leaf_offset = 0;
      return true;
    }
  }
  return false;
}

/* --- Cursor creation --- */

static inline rdoc_cursor rdoc_cursor_new(rdoc d, size_t raw_byte_offset) {
  rdoc_cursor c;
  memset(&c, 0, sizeof(c));
  c.prefix = rdoc_summary_identity();
  c.cur_mark_flags = 0;
  c.cur_mark_id = 0;

  size_t total = rdoc_total_bytes(d);
  if (raw_byte_offset > total) {
    c.valid = false;
    return c;
  }

  c.root = d.root.node;
  c.valid = true;

  if (!c.root) {
    c.leaf = NULL;
    c.leaf_offset = 0;
    return c;
  }

  if (raw_byte_offset == total) {
    /* Position at end: descend to rightmost leaf */
    rdoc_st_node* node = c.root;
    int depth = 0;
    while (node->type == rdoc_st_NODE_INTERNAL) {
      rdoc_st_internal* n = (rdoc_st_internal*)node;
      c.path[depth].node = node;
      for (size_t i = 0; i < n->n_children - 1; i++) {
        c.prefix = rdoc_summary_combine(c.prefix,
            _rdoc_cursor_node_summary(n->children[i]));
      }
      c.path[depth].child_index = n->n_children - 1;
      node = n->children[n->n_children - 1];
      depth++;
    }
    c.leaf = (rdoc_st_leaf*)node;
    c.leaf_offset = c.leaf->count;
    c.depth = depth;
    /* Mark state from full prefix */
    c.cur_mark_flags = c.prefix.last_mark_flags;
    c.cur_mark_id = c.prefix.last_mark_id;
    /* Also account for mark state in the last leaf */
    rdoc_summary leaf_sum = c.leaf->summary;
    if (leaf_sum.has_mark_change) {
      c.cur_mark_flags = leaf_sum.last_mark_flags;
      c.cur_mark_id = leaf_sum.last_mark_id;
    }
    return c;
  }

  /* Normal case: descend to leaf containing raw_byte_offset */
  rdoc_st_node* node = c.root;
  size_t remaining = raw_byte_offset;
  int depth = 0;

  while (node->type == rdoc_st_NODE_INTERNAL) {
    rdoc_st_internal* n = (rdoc_st_internal*)node;
    c.path[depth].node = node;
    for (size_t i = 0; i < n->n_children; i++) {
      if (remaining < n->child_counts[i]) {
        c.path[depth].child_index = i;
        node = n->children[i];
        break;
      }
      c.prefix = rdoc_summary_combine(c.prefix,
          _rdoc_cursor_node_summary(n->children[i]));
      remaining -= n->child_counts[i];
    }
    depth++;
  }

  c.leaf = (rdoc_st_leaf*)node;
  c.leaf_offset = remaining;
  c.depth = depth;

  /* Compute initial mark state from prefix summary + leaf content up to offset */
  if (c.prefix.has_mark_change) {
    c.cur_mark_flags = c.prefix.last_mark_flags;
    c.cur_mark_id = c.prefix.last_mark_id;
  }
  /* Scan leaf content up to leaf_offset for mark changes */
  if (remaining > 0) {
    rdoc_summary leaf_prefix = rdoc_summary_summarize(c.leaf->elements, remaining);
    if (leaf_prefix.has_mark_change) {
      c.cur_mark_flags = leaf_prefix.last_mark_flags;
      c.cur_mark_id = leaf_prefix.last_mark_id;
    }
  }

  return c;
}

static inline void rdoc_cursor_free(rdoc_cursor c) {
  (void)c; /* no-op for stack-allocated cursor */
}

/* --- Token decoding at current position --- */

static inline rdoc_token rdoc_cursor_token(rdoc_cursor* c) {
  rdoc_token tok = {0};
  if (!c->valid || !c->leaf) return tok;
  if (c->leaf_offset >= c->leaf->count) return tok;

  const uint8_t* data = c->leaf->elements;
  size_t off = c->leaf_offset;
  size_t avail = c->leaf->count - off;
  uint8_t byte = data[off];

  if (rdoc_is_sentinel(byte)) {
    size_t seq_size = rdoc_sentinel_size(byte);
    if (seq_size > avail) return tok; /* truncated — shouldn't happen in valid doc */

    switch (byte) {
      case RDOC_SENTINEL_BLOCK_OPEN:
        tok.kind = RDOC_TOKEN_BLOCK_OPEN;
        memcpy(&tok.id, data + off + 1, 4);
        break;
      case RDOC_SENTINEL_BLOCK_CLOSE:
        tok.kind = RDOC_TOKEN_BLOCK_CLOSE;
        memcpy(&tok.id, data + off + 1, 4);
        break;
      case RDOC_SENTINEL_INLINE_OPEN:
        tok.kind = RDOC_TOKEN_INLINE_OPEN;
        memcpy(&tok.id, data + off + 1, 4);
        break;
      case RDOC_SENTINEL_INLINE_CLOSE:
        tok.kind = RDOC_TOKEN_INLINE_CLOSE;
        memcpy(&tok.id, data + off + 1, 4);
        break;
      case RDOC_SENTINEL_MARK_CHANGE:
        tok.kind = RDOC_TOKEN_MARK_CHANGE;
        memcpy(&tok.mark_flags, data + off + 1, 2);
        memcpy(&tok.id, data + off + 3, 4);
        break;
    }
  } else {
    /* Text token: scan forward to next sentinel or end of leaf */
    tok.kind = RDOC_TOKEN_TEXT;
    tok.text_ptr = data + off;
    size_t end = off;
    while (end < c->leaf->count && !rdoc_is_sentinel(data[end])) {
      end++;
    }
    tok.text_len = end - off;
  }

  return tok;
}

/* --- Advance past current token --- */

static inline bool rdoc_cursor_advance(rdoc_cursor* c) {
  if (!c->valid || !c->leaf) return false;
  if (c->leaf_offset >= c->leaf->count) {
    /* Try moving to next leaf */
    c->prefix = rdoc_summary_combine(c->prefix, c->leaf->summary);
    if (!_rdoc_cursor_next_leaf(c)) return false;
    if (c->leaf_offset >= c->leaf->count) return false;
  }

  const uint8_t* data = c->leaf->elements;
  size_t off = c->leaf_offset;
  uint8_t byte = data[off];

  if (rdoc_is_sentinel(byte)) {
    size_t seq_size = rdoc_sentinel_size(byte);
    /* Update mark state when passing a MARK_CHANGE */
    if (byte == RDOC_SENTINEL_MARK_CHANGE) {
      memcpy(&c->cur_mark_flags, data + off + 1, 2);
      memcpy(&c->cur_mark_id, data + off + 3, 4);
    }
    c->leaf_offset += seq_size;
  } else {
    /* Text: scan forward to next sentinel or end of leaf */
    while (c->leaf_offset < c->leaf->count &&
           !rdoc_is_sentinel(c->leaf->elements[c->leaf_offset])) {
      c->leaf_offset++;
    }
  }

  /* If we've consumed this leaf, move to next */
  if (c->leaf_offset >= c->leaf->count) {
    c->prefix = rdoc_summary_combine(c->prefix, c->leaf->summary);
    if (!_rdoc_cursor_next_leaf(c)) {
      /* End of document — still valid but no more tokens */
      return false;
    }
  }
  return true;
}

/* --- Mark state queries --- */

static inline uint16_t rdoc_cursor_mark_flags(rdoc_cursor* c) {
  return c->cur_mark_flags;
}

static inline uint32_t rdoc_cursor_mark_id(rdoc_cursor* c) {
  return c->cur_mark_id;
}

/* --- Position queries --- */

static inline size_t rdoc_cursor_raw_offset(rdoc_cursor* c) {
  if (!c->valid) return 0;
  /* Walk the path to compute total raw bytes before current leaf */
  size_t raw = 0;
  for (int d = 0; d < c->depth; d++) {
    rdoc_st_internal* n = (rdoc_st_internal*)c->path[d].node;
    for (size_t i = 0; i < c->path[d].child_index; i++) {
      raw += n->child_counts[i];
    }
  }
  return raw + c->leaf_offset;
}

static inline size_t rdoc_cursor_text_offset(rdoc_cursor* c) {
  if (!c->valid || !c->leaf) return 0;
  /* Text bytes in prefix + text bytes in leaf up to offset */
  rdoc_summary leaf_prefix = rdoc_summary_summarize(c->leaf->elements, c->leaf_offset);
  return c->prefix.text_bytes + leaf_prefix.text_bytes;
}

static inline size_t rdoc_cursor_char_offset(rdoc_cursor* c) {
  if (!c->valid || !c->leaf) return 0;
  rdoc_summary leaf_prefix = rdoc_summary_summarize(c->leaf->elements, c->leaf_offset);
  return c->prefix.chars + leaf_prefix.chars;
}

static inline size_t rdoc_cursor_line(rdoc_cursor* c) {
  if (!c->valid || !c->leaf) return 0;
  rdoc_summary leaf_prefix = rdoc_summary_summarize(c->leaf->elements, c->leaf_offset);
  return c->prefix.lines + leaf_prefix.lines;
}

static inline int32_t rdoc_cursor_block_depth(rdoc_cursor* c) {
  if (!c->valid || !c->leaf) return 0;
  rdoc_summary leaf_prefix = rdoc_summary_summarize(c->leaf->elements, c->leaf_offset);
  return c->prefix.block_depth_delta + leaf_prefix.block_depth_delta;
}

/* ========================================================================
 * Text editing: insert, delete, replace, and raw delete
 *
 * All edit operations return a new rdoc (persistent/immutable). The original
 * document is never modified. Callers must rdoc_unref both old and new.
 * ======================================================================== */

/* --- Internal: check if buffer contains any sentinel bytes --- */

static inline bool _rdoc_buf_has_sentinel(const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (rdoc_is_sentinel(buf[i])) return true;
  }
  return false;
}

/* --- Internal: check if a raw byte position is a safe boundary ---
   Returns true if raw_pos does not land inside a sentinel sequence
   or on a UTF-8 continuation byte. Walks tree to leaf, scans forward
   from leaf start for reliable sentinel parsing. O(log n + leaf_size). */

static inline bool _rdoc_is_safe_boundary(rdoc d, size_t raw_pos) {
  size_t total = rdoc_total_bytes(d);
  if (raw_pos == 0 || raw_pos >= total) return true;

  rdoc_st_node* node = d.root.node;
  if (!node) return true;
  size_t remaining = raw_pos;

  while (node->type == rdoc_st_NODE_INTERNAL) {
    rdoc_st_internal* n = (rdoc_st_internal*)node;
    for (size_t i = 0; i < n->n_children; i++) {
      if (remaining < n->child_counts[i]) {
        node = n->children[i];
        break;
      }
      remaining -= n->child_counts[i];
    }
  }

  rdoc_st_leaf* leaf = (rdoc_st_leaf*)node;

  /* Scan from leaf start to remaining, tracking sentinel sequences */
  size_t i = 0;
  while (i < remaining) {
    if (rdoc_is_sentinel(leaf->elements[i])) {
      size_t seq_size = rdoc_sentinel_size(leaf->elements[i]);
      if (i + seq_size > remaining) return false; /* inside sentinel */
      i += seq_size;
    } else {
      i++;
    }
  }

  /* At position: check UTF-8 continuation */
  if (remaining < leaf->count && !rdoc_is_sentinel(leaf->elements[remaining])
      && utf8_is_continuation(leaf->elements[remaining])) {
    return false;
  }

  return true;
}

/* --- rdoc_insert_text --- */

static inline rdoc rdoc_insert_text(rdoc d, size_t text_byte_offset,
                                     const uint8_t* str, size_t byte_len) {
  size_t total_text = rdoc_text_byte_count(d);

  /* Out of bounds */
  if (text_byte_offset > total_text) return rdoc_ref(d);

  /* Empty insert */
  if (byte_len == 0) return rdoc_ref(d);

  /* Validate UTF-8 */
  if (!utf8_validate(str, byte_len)) return rdoc_ref(d);

  /* Validate no sentinel bytes in input */
  if (_rdoc_buf_has_sentinel(str, byte_len)) return rdoc_ref(d);

  /* Find raw position */
  size_t raw_pos;
  if (text_byte_offset == total_text) {
    /* Append at end of document */
    raw_pos = rdoc_total_bytes(d);
  } else {
    rdoc_seek_result sr = rdoc_text_byte_to_raw(d, text_byte_offset);
    if (!sr.found) return rdoc_ref(d);
    raw_pos = sr.raw_index;
  }

  /* Build rdoc from inserted text */
  rdoc inserted = rdoc_from_bytes(str, byte_len);

  /* Split and concat: left + inserted + right */
  rdoc_st_split_result sp = rdoc_st_split(d.root, raw_pos);
  rdoc_st_root tmp = rdoc_st_concat(sp.left, inserted.root);
  rdoc_st_root result = rdoc_st_concat(tmp, sp.right);

  rdoc_st_unref(sp.left);
  rdoc_st_unref(sp.right);
  rdoc_st_unref(tmp);
  rdoc_unref(inserted);

  return (rdoc){.root = result};
}

/* --- rdoc_delete_text ---
   Deletes text bytes in range [text_byte_offset, text_byte_offset + text_byte_len),
   preserving all sentinel sequences within the range. */

static inline rdoc rdoc_delete_text(rdoc d, size_t text_byte_offset,
                                     size_t text_byte_len) {
  size_t total_text = rdoc_text_byte_count(d);

  /* Out of bounds or empty delete */
  if (text_byte_offset >= total_text) return rdoc_ref(d);
  if (text_byte_len == 0) return rdoc_ref(d);

  /* Clamp */
  if (text_byte_offset + text_byte_len > total_text)
    text_byte_len = total_text - text_byte_offset;

  /* Find raw positions for start and end of text byte range */
  rdoc_seek_result sr_start = rdoc_text_byte_to_raw(d, text_byte_offset);
  if (!sr_start.found) return rdoc_ref(d);
  size_t raw_start = sr_start.raw_index;

  size_t raw_end;
  if (text_byte_offset + text_byte_len >= total_text) {
    raw_end = rdoc_total_bytes(d);
  } else {
    rdoc_seek_result sr_end = rdoc_text_byte_to_raw(d, text_byte_offset + text_byte_len);
    if (!sr_end.found) return rdoc_ref(d);
    raw_end = sr_end.raw_index;
  }

  /* Validate codepoint boundaries: bytes at raw_start and raw_end
     must not be UTF-8 continuation bytes */
  rdoc_st_get_result gr = rdoc_st_get(d.root, raw_start);
  if (gr.found && !rdoc_is_sentinel(gr.value) && utf8_is_continuation(gr.value))
    return rdoc_ref(d);

  if (raw_end < rdoc_total_bytes(d)) {
    gr = rdoc_st_get(d.root, raw_end);
    if (gr.found && !rdoc_is_sentinel(gr.value) && utf8_is_continuation(gr.value))
      return rdoc_ref(d);
  }

  /* Extract raw bytes from the mid section [raw_start, raw_end) */
  size_t mid_len = raw_end - raw_start;

  /* Stack alloc for small ranges, heap for large */
  uint8_t stack_mid[512];
  uint8_t* mid_buf = stack_mid;
  if (mid_len > 512) {
    mid_buf = (uint8_t*)rdoc_st_allocator.alloc(rdoc_st_allocator.ctx, mid_len);
  }

  rdoc_st_copy_range(d.root, raw_start, mid_len, mid_buf);

  /* Filter: keep only sentinel sequences, remove text bytes */
  uint8_t stack_sent[512];
  uint8_t* sent_buf = stack_sent;
  if (mid_len > 512) {
    sent_buf = (uint8_t*)rdoc_st_allocator.alloc(rdoc_st_allocator.ctx, mid_len);
  }

  size_t sent_len = 0;
  size_t mi = 0;
  while (mi < mid_len) {
    if (rdoc_is_sentinel(mid_buf[mi])) {
      size_t seq_size = rdoc_sentinel_size(mid_buf[mi]);
      if (mi + seq_size <= mid_len) {
        memcpy(sent_buf + sent_len, mid_buf + mi, seq_size);
        sent_len += seq_size;
      }
      mi += seq_size;
    } else {
      mi++; /* skip text byte */
    }
  }

  /* Split original at raw_start and raw_end */
  rdoc_st_split_result sp1 = rdoc_st_split(d.root, raw_start);
  rdoc_st_split_result sp2 = rdoc_st_split(sp1.right, raw_end - raw_start);

  /* Build rdoc from sentinel-only bytes (may be empty) */
  rdoc_st_root result;
  if (sent_len > 0) {
    rdoc sent_rdoc = rdoc_from_bytes(sent_buf, sent_len);
    rdoc_st_root tmp = rdoc_st_concat(sp1.left, sent_rdoc.root);
    result = rdoc_st_concat(tmp, sp2.right);
    rdoc_st_unref(tmp);
    rdoc_unref(sent_rdoc);
  } else {
    result = rdoc_st_concat(sp1.left, sp2.right);
  }

  rdoc_st_unref(sp1.left);
  rdoc_st_unref(sp1.right);
  rdoc_st_unref(sp2.left);
  rdoc_st_unref(sp2.right);

  if (mid_buf != stack_mid)
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, mid_buf);
  if (sent_buf != stack_sent)
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, sent_buf);

  return (rdoc){.root = result};
}

/* --- rdoc_replace_text --- */

static inline rdoc rdoc_replace_text(rdoc d, size_t text_byte_offset,
                                      size_t text_byte_len,
                                      const uint8_t* str, size_t str_len) {
  rdoc after_delete = rdoc_delete_text(d, text_byte_offset, text_byte_len);
  rdoc result = rdoc_insert_text(after_delete, text_byte_offset, str, str_len);
  rdoc_unref(after_delete);
  return result;
}

/* --- rdoc_delete_range ---
   Deletes everything in raw byte range including sentinels.
   Validates boundaries don't split sentinel sequences or UTF-8 codepoints. */

static inline rdoc rdoc_delete_range(rdoc d, size_t raw_byte_offset,
                                      size_t raw_byte_len) {
  size_t total = rdoc_total_bytes(d);

  /* Out of bounds or empty */
  if (raw_byte_offset >= total) return rdoc_ref(d);
  if (raw_byte_len == 0) return rdoc_ref(d);

  /* Clamp */
  if (raw_byte_offset + raw_byte_len > total)
    raw_byte_len = total - raw_byte_offset;

  /* Validate boundaries don't split sentinels or UTF-8 codepoints */
  if (!_rdoc_is_safe_boundary(d, raw_byte_offset)) return rdoc_ref(d);
  if (!_rdoc_is_safe_boundary(d, raw_byte_offset + raw_byte_len)) return rdoc_ref(d);

  /* Split at start, split right at len, concat left + remainder */
  rdoc_st_split_result sp1 = rdoc_st_split(d.root, raw_byte_offset);
  rdoc_st_split_result sp2 = rdoc_st_split(sp1.right, raw_byte_len);
  rdoc_st_root result = rdoc_st_concat(sp1.left, sp2.right);

  rdoc_st_unref(sp1.left);
  rdoc_st_unref(sp1.right);
  rdoc_st_unref(sp2.left);
  rdoc_st_unref(sp2.right);

  return (rdoc){.root = result};
}

/* ========================================================================
 * Block and inline block structural editing
 *
 * Insert/remove block and inline-block pairs, and wrap ranges.
 * All operations return new rdoc (persistent). Original unchanged.
 * ======================================================================== */

/* --- rdoc_insert_block: insert BLOCK_OPEN + BLOCK_CLOSE pair at raw position --- */

static inline rdoc rdoc_insert_block(rdoc d, size_t raw_index, uint32_t id) {
  size_t total = rdoc_total_bytes(d);
  if (raw_index > total) return rdoc_ref(d);
  if (raw_index < total && !_rdoc_is_safe_boundary(d, raw_index)) return rdoc_ref(d);

  uint8_t buf[10];
  rdoc_encode_block_open(buf, id);
  rdoc_encode_block_close(buf + 5, id);

  rdoc pair = rdoc_from_bytes(buf, 10);
  rdoc_st_split_result sp = rdoc_st_split(d.root, raw_index);
  rdoc_st_root tmp = rdoc_st_concat(sp.left, pair.root);
  rdoc_st_root result = rdoc_st_concat(tmp, sp.right);

  rdoc_st_unref(sp.left);
  rdoc_st_unref(sp.right);
  rdoc_st_unref(tmp);
  rdoc_unref(pair);

  return (rdoc){.root = result};
}

/* --- rdoc_insert_inline: insert INLINE_OPEN + INLINE_CLOSE pair at raw position --- */

static inline rdoc rdoc_insert_inline(rdoc d, size_t raw_index, uint32_t id) {
  size_t total = rdoc_total_bytes(d);
  if (raw_index > total) return rdoc_ref(d);
  if (raw_index < total && !_rdoc_is_safe_boundary(d, raw_index)) return rdoc_ref(d);

  uint8_t buf[10];
  rdoc_encode_inline_open(buf, id);
  rdoc_encode_inline_close(buf + 5, id);

  rdoc pair = rdoc_from_bytes(buf, 10);
  rdoc_st_split_result sp = rdoc_st_split(d.root, raw_index);
  rdoc_st_root tmp = rdoc_st_concat(sp.left, pair.root);
  rdoc_st_root result = rdoc_st_concat(tmp, sp.right);

  rdoc_st_unref(sp.left);
  rdoc_st_unref(sp.right);
  rdoc_st_unref(tmp);
  rdoc_unref(pair);

  return (rdoc){.root = result};
}

/* --- rdoc_remove_block: find and remove BLOCK_OPEN/CLOSE pair by ID --- */

static inline rdoc rdoc_remove_block(rdoc d, uint32_t id) {
  if (rdoc_total_bytes(d) == 0) return rdoc_ref(d);

  size_t open_raw = 0;
  bool found_open = false;
  size_t close_raw = 0;
  bool found_close = false;

  rdoc_cursor c = rdoc_cursor_new(d, 0);
  if (!c.valid) return rdoc_ref(d);

  do {
    rdoc_token tok = rdoc_cursor_token(&c);
    if (!found_open && tok.kind == RDOC_TOKEN_BLOCK_OPEN && tok.id == id) {
      open_raw = rdoc_cursor_raw_offset(&c);
      found_open = true;
    } else if (found_open && !found_close && tok.kind == RDOC_TOKEN_BLOCK_CLOSE && tok.id == id) {
      close_raw = rdoc_cursor_raw_offset(&c);
      found_close = true;
      break;
    }
  } while (rdoc_cursor_advance(&c));

  rdoc_cursor_free(c);

  if (!found_open || !found_close) return rdoc_ref(d);

  /* Delete close first (higher offset), then open */
  rdoc d2 = rdoc_delete_range(d, close_raw, 5);
  rdoc d3 = rdoc_delete_range(d2, open_raw, 5);
  rdoc_unref(d2);

  return d3;
}

/* --- rdoc_remove_inline: find and remove INLINE_OPEN/CLOSE pair by ID --- */

static inline rdoc rdoc_remove_inline(rdoc d, uint32_t id) {
  if (rdoc_total_bytes(d) == 0) return rdoc_ref(d);

  size_t open_raw = 0;
  bool found_open = false;
  size_t close_raw = 0;
  bool found_close = false;

  rdoc_cursor c = rdoc_cursor_new(d, 0);
  if (!c.valid) return rdoc_ref(d);

  do {
    rdoc_token tok = rdoc_cursor_token(&c);
    if (!found_open && tok.kind == RDOC_TOKEN_INLINE_OPEN && tok.id == id) {
      open_raw = rdoc_cursor_raw_offset(&c);
      found_open = true;
    } else if (found_open && !found_close && tok.kind == RDOC_TOKEN_INLINE_CLOSE && tok.id == id) {
      close_raw = rdoc_cursor_raw_offset(&c);
      found_close = true;
      break;
    }
  } while (rdoc_cursor_advance(&c));

  rdoc_cursor_free(c);

  if (!found_open || !found_close) return rdoc_ref(d);

  /* Delete close first (higher offset), then open */
  rdoc d2 = rdoc_delete_range(d, close_raw, 5);
  rdoc d3 = rdoc_delete_range(d2, open_raw, 5);
  rdoc_unref(d2);

  return d3;
}

/* --- rdoc_wrap_block: wrap range [start_raw, end_raw) with BLOCK_OPEN/CLOSE --- */

static inline rdoc rdoc_wrap_block(rdoc d, size_t start_raw, size_t end_raw, uint32_t id) {
  size_t total = rdoc_total_bytes(d);
  if (start_raw > total || end_raw > total || start_raw > end_raw) return rdoc_ref(d);
  if (start_raw < total && !_rdoc_is_safe_boundary(d, start_raw)) return rdoc_ref(d);
  if (end_raw < total && !_rdoc_is_safe_boundary(d, end_raw)) return rdoc_ref(d);

  /* Insert BLOCK_OPEN at start_raw */
  uint8_t open_buf[5];
  rdoc_encode_block_open(open_buf, id);
  rdoc open_rdoc = rdoc_from_bytes(open_buf, 5);

  rdoc_st_split_result sp1 = rdoc_st_split(d.root, start_raw);
  rdoc_st_root tmp1 = rdoc_st_concat(sp1.left, open_rdoc.root);
  rdoc_st_root after_open = rdoc_st_concat(tmp1, sp1.right);

  rdoc_st_unref(sp1.left);
  rdoc_st_unref(sp1.right);
  rdoc_st_unref(tmp1);
  rdoc_unref(open_rdoc);

  /* Insert BLOCK_CLOSE at end_raw + 5 (accounting for inserted BLOCK_OPEN) */
  uint8_t close_buf[5];
  rdoc_encode_block_close(close_buf, id);
  rdoc close_rdoc = rdoc_from_bytes(close_buf, 5);

  rdoc_st_split_result sp2 = rdoc_st_split(after_open, end_raw + 5);
  rdoc_st_root tmp2 = rdoc_st_concat(sp2.left, close_rdoc.root);
  rdoc_st_root result = rdoc_st_concat(tmp2, sp2.right);

  rdoc_st_unref(after_open);
  rdoc_st_unref(sp2.left);
  rdoc_st_unref(sp2.right);
  rdoc_st_unref(tmp2);
  rdoc_unref(close_rdoc);

  return (rdoc){.root = result};
}

/* --- rdoc_wrap_inline: wrap range [start_raw, end_raw) with INLINE_OPEN/CLOSE --- */

static inline rdoc rdoc_wrap_inline(rdoc d, size_t start_raw, size_t end_raw, uint32_t id) {
  size_t total = rdoc_total_bytes(d);
  if (start_raw > total || end_raw > total || start_raw > end_raw) return rdoc_ref(d);
  if (start_raw < total && !_rdoc_is_safe_boundary(d, start_raw)) return rdoc_ref(d);
  if (end_raw < total && !_rdoc_is_safe_boundary(d, end_raw)) return rdoc_ref(d);

  /* Insert INLINE_OPEN at start_raw */
  uint8_t open_buf[5];
  rdoc_encode_inline_open(open_buf, id);
  rdoc open_rdoc = rdoc_from_bytes(open_buf, 5);

  rdoc_st_split_result sp1 = rdoc_st_split(d.root, start_raw);
  rdoc_st_root tmp1 = rdoc_st_concat(sp1.left, open_rdoc.root);
  rdoc_st_root after_open = rdoc_st_concat(tmp1, sp1.right);

  rdoc_st_unref(sp1.left);
  rdoc_st_unref(sp1.right);
  rdoc_st_unref(tmp1);
  rdoc_unref(open_rdoc);

  /* Insert INLINE_CLOSE at end_raw + 5 (accounting for inserted INLINE_OPEN) */
  uint8_t close_buf[5];
  rdoc_encode_inline_close(close_buf, id);
  rdoc close_rdoc = rdoc_from_bytes(close_buf, 5);

  rdoc_st_split_result sp2 = rdoc_st_split(after_open, end_raw + 5);
  rdoc_st_root tmp2 = rdoc_st_concat(sp2.left, close_rdoc.root);
  rdoc_st_root result = rdoc_st_concat(tmp2, sp2.right);

  rdoc_st_unref(after_open);
  rdoc_st_unref(sp2.left);
  rdoc_st_unref(sp2.right);
  rdoc_st_unref(tmp2);
  rdoc_unref(close_rdoc);

  return (rdoc){.root = result};
}

/* ========================================================================
 * Mark toggle, set, and clear on text ranges
 *
 * Toggle, set, or clear formatting marks on a text range.
 * All operations return a new rdoc (persistent). Original unchanged.
 * Redundant mark boundaries are automatically cleaned up.
 * ======================================================================== */

/* Internal: apply mark operation to text range.
   op: 0=toggle(XOR), 1=set(OR), 2=clear(AND NOT), 3=set_absolute */
static inline rdoc _rdoc_mark_op(rdoc d, size_t text_byte_start, size_t text_byte_end,
                                  int op, uint16_t mark_bits, uint32_t mark_app_id) {
  if (text_byte_start >= text_byte_end) return rdoc_ref(d);
  size_t total_text = rdoc_text_byte_count(d);
  if (text_byte_start >= total_text) return rdoc_ref(d);
  if (text_byte_end > total_text) text_byte_end = total_text;

  /* toggle/set/clear with mark_bits==0 is a no-op */
  if (op < 3 && mark_bits == 0) return rdoc_ref(d);

  /* Get raw positions for text byte boundaries */
  size_t raw_start, raw_end;
  rdoc_seek_result sr = rdoc_text_byte_to_raw(d, text_byte_start);
  if (!sr.found) return rdoc_ref(d);
  raw_start = sr.raw_index;

  if (text_byte_end >= total_text) {
    raw_end = rdoc_total_bytes(d);
  } else {
    sr = rdoc_text_byte_to_raw(d, text_byte_end);
    if (!sr.found) return rdoc_ref(d);
    raw_end = sr.raw_index;
  }

  /* Mark state at text range boundaries */
  rdoc_mark_result pre_state = rdoc_mark_at(d, raw_start);
  rdoc_mark_result end_state = rdoc_mark_at(d, raw_end);

  /* Extend backward past adjacent MARK_CHANGE sentinels so that
     boundary marks participate in cleanup (collapse consecutive marks). */
  size_t adj_start = raw_start;
  while (adj_start >= 7) {
    size_t check = adj_start - 7;
    if (!_rdoc_is_safe_boundary(d, check)) break;
    rdoc_st_get_result gr = rdoc_st_get(d.root, check);
    if (!gr.found || gr.value != RDOC_SENTINEL_MARK_CHANGE) break;
    adj_start = check;
  }

  /* Extend forward past adjacent MARK_CHANGE sentinels */
  size_t adj_end = raw_end;
  {
    size_t total_bytes = rdoc_total_bytes(d);
    while (adj_end + 7 <= total_bytes) {
      rdoc_st_get_result gr = rdoc_st_get(d.root, adj_end);
      if (!gr.found || gr.value != RDOC_SENTINEL_MARK_CHANGE) break;
      adj_end += 7;
    }
  }

  /* Mark state at extended start (for cleanup tracking) */
  rdoc_mark_result adj_pre = rdoc_mark_at(d, adj_start);

  /* Extract extended range [adj_start, adj_end) */
  size_t ext_len = adj_end - adj_start;
  uint8_t stack_ext[512];
  uint8_t* ext_buf = stack_ext;
  if (ext_len > 512) {
    ext_buf = (uint8_t*)rdoc_st_allocator.alloc(rdoc_st_allocator.ctx, ext_len);
  }
  rdoc_st_copy_range(d.root, adj_start, ext_len, ext_buf);

  /* Build new buffer: prefix_marks + opening_mark + modified_mid + closing_mark + suffix_marks */
  size_t max_new = ext_len + 14;
  uint8_t stack_new[1024];
  uint8_t* new_buf = stack_new;
  if (max_new > 1024) {
    new_buf = (uint8_t*)rdoc_st_allocator.alloc(rdoc_st_allocator.ctx, max_new);
  }
  size_t new_len = 0;

  /* Copy marks from [adj_start, raw_start) unmodified */
  size_t prefix_len = raw_start - adj_start;
  if (prefix_len > 0) {
    memcpy(new_buf, ext_buf, prefix_len);
    new_len = prefix_len;
  }

  /* Opening mark */
  uint16_t open_flags;
  uint32_t open_app_id;
  switch (op) {
    case 0:  open_flags = pre_state.flags ^ mark_bits;
             open_app_id = pre_state.app_id; break;
    case 1:  open_flags = pre_state.flags | mark_bits;
             open_app_id = pre_state.app_id; break;
    case 2:  open_flags = (uint16_t)(pre_state.flags & ~(unsigned)mark_bits);
             open_app_id = pre_state.app_id; break;
    default: open_flags = mark_bits;
             open_app_id = mark_app_id; break;
  }
  new_len += rdoc_encode_mark_change(new_buf + new_len, open_flags, open_app_id);

  /* Process mid section [raw_start, raw_end): modify MARK_CHANGE flags, copy rest */
  size_t mi = prefix_len;
  size_t mid_end_in_ext = prefix_len + (raw_end - raw_start);
  while (mi < mid_end_in_ext) {
    if (rdoc_is_sentinel(ext_buf[mi])) {
      size_t seq_size = rdoc_sentinel_size(ext_buf[mi]);
      if (mi + seq_size > mid_end_in_ext) break;

      if (ext_buf[mi] == RDOC_SENTINEL_MARK_CHANGE) {
        uint16_t flags;
        uint32_t app_id;
        rdoc_decode_mark_change(ext_buf + mi, &flags, &app_id);
        switch (op) {
          case 0:  flags ^= mark_bits; break;
          case 1:  flags |= mark_bits; break;
          case 2:  flags = (uint16_t)(flags & ~(unsigned)mark_bits); break;
          default: flags = mark_bits; app_id = mark_app_id; break;
        }
        new_len += rdoc_encode_mark_change(new_buf + new_len, flags, app_id);
      } else {
        memcpy(new_buf + new_len, ext_buf + mi, seq_size);
        new_len += seq_size;
      }
      mi += seq_size;
    } else {
      new_buf[new_len++] = ext_buf[mi++];
    }
  }

  /* Closing mark: restore end state */
  new_len += rdoc_encode_mark_change(new_buf + new_len, end_state.flags, end_state.app_id);

  /* Copy marks from [raw_end, adj_end) unmodified */
  size_t suffix_start_in_ext = mid_end_in_ext;
  size_t suffix_len = ext_len - suffix_start_in_ext;
  if (suffix_len > 0) {
    memcpy(new_buf + new_len, ext_buf + suffix_start_in_ext, suffix_len);
    new_len += suffix_len;
  }

  /* Single-pass cleanup: collapse consecutive MARK_CHANGE sentinels and
     remove marks that don't change state from what was previously active. */
  uint8_t stack_clean[1024];
  uint8_t* clean_buf = stack_clean;
  if (new_len > 1024) {
    clean_buf = (uint8_t*)rdoc_st_allocator.alloc(rdoc_st_allocator.ctx, new_len);
  }
  size_t clean_len = 0;
  uint16_t track_flags = adj_pre.flags;
  uint32_t track_app_id = adj_pre.app_id;
  bool has_pending = false;
  uint16_t pend_flags = 0;
  uint32_t pend_app_id = 0;

  mi = 0;
  while (mi < new_len) {
    if (new_buf[mi] == RDOC_SENTINEL_MARK_CHANGE && mi + 7 <= new_len) {
      /* Consecutive marks: keep replacing pending with latest */
      rdoc_decode_mark_change(new_buf + mi, &pend_flags, &pend_app_id);
      has_pending = true;
      mi += 7;
    } else {
      /* Non-mark element: emit pending mark if it changes state */
      if (has_pending) {
        if (pend_flags != track_flags || pend_app_id != track_app_id) {
          clean_len += rdoc_encode_mark_change(clean_buf + clean_len,
                                                pend_flags, pend_app_id);
          track_flags = pend_flags;
          track_app_id = pend_app_id;
        }
        has_pending = false;
      }
      if (rdoc_is_sentinel(new_buf[mi])) {
        size_t seq_size = rdoc_sentinel_size(new_buf[mi]);
        memcpy(clean_buf + clean_len, new_buf + mi, seq_size);
        clean_len += seq_size;
        mi += seq_size;
      } else {
        clean_buf[clean_len++] = new_buf[mi++];
      }
    }
  }
  /* Handle trailing pending mark */
  if (has_pending) {
    if (pend_flags != track_flags || pend_app_id != track_app_id) {
      clean_len += rdoc_encode_mark_change(clean_buf + clean_len,
                                            pend_flags, pend_app_id);
    }
  }

  /* Replace [adj_start, adj_end) with cleaned content */
  rdoc_st_split_result sp1 = rdoc_st_split(d.root, adj_start);
  rdoc_st_split_result sp2 = rdoc_st_split(sp1.right, ext_len);

  rdoc_st_root result;
  if (clean_len > 0) {
    rdoc new_rdoc = rdoc_from_bytes(clean_buf, clean_len);
    rdoc_st_root tmp = rdoc_st_concat(sp1.left, new_rdoc.root);
    result = rdoc_st_concat(tmp, sp2.right);
    rdoc_st_unref(tmp);
    rdoc_unref(new_rdoc);
  } else {
    result = rdoc_st_concat(sp1.left, sp2.right);
  }

  rdoc_st_unref(sp1.left);
  rdoc_st_unref(sp1.right);
  rdoc_st_unref(sp2.left);
  rdoc_st_unref(sp2.right);

  if (ext_buf != stack_ext)
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, ext_buf);
  if (new_buf != stack_new)
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, new_buf);
  if (clean_buf != stack_clean)
    rdoc_st_allocator.free(rdoc_st_allocator.ctx, clean_buf);

  return (rdoc){.root = result};
}

/* --- Public mark editing functions --- */

static inline rdoc rdoc_toggle_mark(rdoc d, size_t text_byte_start,
                                     size_t text_byte_end, uint16_t mark_bits) {
  return _rdoc_mark_op(d, text_byte_start, text_byte_end, 0, mark_bits, 0);
}

static inline rdoc rdoc_set_mark(rdoc d, size_t text_byte_start,
                                  size_t text_byte_end, uint16_t mark_bits) {
  return _rdoc_mark_op(d, text_byte_start, text_byte_end, 1, mark_bits, 0);
}

static inline rdoc rdoc_clear_mark(rdoc d, size_t text_byte_start,
                                    size_t text_byte_end, uint16_t mark_bits) {
  return _rdoc_mark_op(d, text_byte_start, text_byte_end, 2, mark_bits, 0);
}

static inline rdoc rdoc_set_mark_id(rdoc d, size_t text_byte_start,
                                     size_t text_byte_end,
                                     uint16_t flags, uint32_t app_id) {
  return _rdoc_mark_op(d, text_byte_start, text_byte_end, 3, flags, app_id);
}

/* ========================================================================
 * Transient COW mode for batch edits
 *
 * Copy-on-write transient mode avoids unnecessary allocations when
 * performing many sequential edits. Follows the same pattern as
 * rope_transient: shared nodes are cloned on write, exclusive nodes
 * are mutated in place.
 * ======================================================================== */

typedef struct {
  rdoc_st_root root;
  bool valid;
} rdoc_transient;

/* Helper: clone an internal node (copies children pointers, refs each child) */
static inline rdoc_st_node* _rdoc_transient_clone_internal(rdoc_st_internal* old) {
  rdoc_st_internal* n = (rdoc_st_internal*)rdoc_st_rc_alloc(
      sizeof(rdoc_st_internal), rdoc_st_node_destroy);
  n->header = (rdoc_st_node){.type = rdoc_st_NODE_INTERNAL};
  n->n_children = old->n_children;
  n->count = old->count;
  n->summary = old->summary;
  memcpy(n->child_counts, old->child_counts, sizeof(size_t) * old->n_children);
  for (size_t i = 0; i < old->n_children; i++) {
    n->children[i] = old->children[i];
    rdoc_st_rc_ref(old->children[i]);
  }
  return (rdoc_st_node*)n;
}

/* Helper: ensure a node is exclusively owned; clone if shared, unref old */
static inline rdoc_st_node* _rdoc_transient_ensure_exclusive(rdoc_st_node* node) {
  if (rdoc_st_rc_count(node) <= 1) return node;

  rdoc_st_node* clone;
  if (node->type == rdoc_st_NODE_LEAF) {
    rdoc_st_leaf* old = (rdoc_st_leaf*)node;
    clone = (rdoc_st_node*)rdoc_st_mk_leaf(old->elements, old->count);
  } else {
    clone = _rdoc_transient_clone_internal((rdoc_st_internal*)node);
  }
  rdoc_st_rc_unref(node);
  return clone;
}

static inline rdoc_transient rdoc_to_transient(rdoc d) {
  rdoc_st_set_handlers((rdoc_st_handlers){
    .identity   = rdoc_summary_identity,
    .combine    = rdoc_summary_combine,
    .summarize  = rdoc_summary_summarize,
    .leaf_search = rdoc_leaf_search
  });
  return (rdoc_transient){
    .root = rdoc_st_ref(d.root),
    .valid = true
  };
}

static inline rdoc rdoc_transient_to_persistent(rdoc_transient* t) {
  if (!t->valid) return rdoc_new();
  rdoc result = (rdoc){.root = t->root};
  t->root = rdoc_st_empty();
  t->valid = false;
  return result;
}

static inline void rdoc_transient_free(rdoc_transient* t) {
  if (!t->valid) return;
  rdoc_st_unref(t->root);
  t->root = rdoc_st_empty();
  t->valid = false;
}

/* COW in-place insert at raw position: walk from root to target leaf, cloning shared nodes */
static inline void _rdoc_transient_cow_insert(rdoc_transient* t, size_t raw_offset,
                                               const uint8_t* str, size_t byte_len) {
  t->root.node = _rdoc_transient_ensure_exclusive(t->root.node);
  rdoc_st_node* node = t->root.node;

  /* Single-leaf tree */
  if (node->type == rdoc_st_NODE_LEAF) {
    rdoc_st_leaf* leaf = (rdoc_st_leaf*)node;
    memmove(leaf->elements + raw_offset + byte_len,
            leaf->elements + raw_offset,
            leaf->count - raw_offset);
    memcpy(leaf->elements + raw_offset, str, byte_len);
    leaf->count += byte_len;
    leaf->summary = rdoc_summary_summarize(leaf->elements, leaf->count);
    t->root.count += byte_len;
    return;
  }

  /* Multi-level tree: walk to target leaf */
  struct { rdoc_st_internal* node; size_t child_idx; } path[RDOC_CURSOR_MAX_DEPTH];
  int depth = 0;
  size_t remaining = raw_offset;

  while (node->type == rdoc_st_NODE_INTERNAL) {
    rdoc_st_internal* n = (rdoc_st_internal*)node;
    size_t ci;
    for (ci = 0; ci < n->n_children - 1; ci++) {
      if (remaining < n->child_counts[ci]) break;
      remaining -= n->child_counts[ci];
    }
    path[depth].node = n;
    path[depth].child_idx = ci;
    depth++;
    n->children[ci] = _rdoc_transient_ensure_exclusive(n->children[ci]);
    node = n->children[ci];
  }

  rdoc_st_leaf* leaf = (rdoc_st_leaf*)node;
  memmove(leaf->elements + remaining + byte_len,
          leaf->elements + remaining,
          leaf->count - remaining);
  memcpy(leaf->elements + remaining, str, byte_len);
  leaf->count += byte_len;
  leaf->summary = rdoc_summary_summarize(leaf->elements, leaf->count);

  /* Walk back up, recompute summaries and counts */
  for (int d = depth - 1; d >= 0; d--) {
    rdoc_st_internal* parent = path[d].node;
    size_t ci = path[d].child_idx;
    rdoc_st_node* child = parent->children[ci];
    parent->child_counts[ci] = (child->type == rdoc_st_NODE_LEAF)
        ? ((rdoc_st_leaf*)child)->count
        : ((rdoc_st_internal*)child)->count;
    parent->count = 0;
    parent->summary = rdoc_summary_identity();
    for (size_t i = 0; i < parent->n_children; i++) {
      parent->count += parent->child_counts[i];
      rdoc_summary cs = (parent->children[i]->type == rdoc_st_NODE_LEAF)
          ? ((rdoc_st_leaf*)parent->children[i])->summary
          : ((rdoc_st_internal*)parent->children[i])->summary;
      parent->summary = rdoc_summary_combine(parent->summary, cs);
    }
  }
  t->root.count += byte_len;
}

/* COW in-place delete at raw position */
static inline void _rdoc_transient_cow_delete(rdoc_transient* t, size_t raw_offset,
                                               size_t byte_len) {
  t->root.node = _rdoc_transient_ensure_exclusive(t->root.node);
  rdoc_st_node* node = t->root.node;

  if (node->type == rdoc_st_NODE_LEAF) {
    rdoc_st_leaf* leaf = (rdoc_st_leaf*)node;
    memmove(leaf->elements + raw_offset,
            leaf->elements + raw_offset + byte_len,
            leaf->count - raw_offset - byte_len);
    leaf->count -= byte_len;
    leaf->summary = rdoc_summary_summarize(leaf->elements, leaf->count);
    t->root.count -= byte_len;
    return;
  }

  struct { rdoc_st_internal* node; size_t child_idx; } path[RDOC_CURSOR_MAX_DEPTH];
  int depth = 0;
  size_t remaining = raw_offset;

  while (node->type == rdoc_st_NODE_INTERNAL) {
    rdoc_st_internal* n = (rdoc_st_internal*)node;
    size_t ci;
    for (ci = 0; ci < n->n_children - 1; ci++) {
      if (remaining < n->child_counts[ci]) break;
      remaining -= n->child_counts[ci];
    }
    path[depth].node = n;
    path[depth].child_idx = ci;
    depth++;
    n->children[ci] = _rdoc_transient_ensure_exclusive(n->children[ci]);
    node = n->children[ci];
  }

  rdoc_st_leaf* leaf = (rdoc_st_leaf*)node;
  memmove(leaf->elements + remaining,
          leaf->elements + remaining + byte_len,
          leaf->count - remaining - byte_len);
  leaf->count -= byte_len;
  leaf->summary = rdoc_summary_summarize(leaf->elements, leaf->count);

  for (int d = depth - 1; d >= 0; d--) {
    rdoc_st_internal* parent = path[d].node;
    size_t ci = path[d].child_idx;
    rdoc_st_node* child = parent->children[ci];
    parent->child_counts[ci] = (child->type == rdoc_st_NODE_LEAF)
        ? ((rdoc_st_leaf*)child)->count
        : ((rdoc_st_internal*)child)->count;
    parent->count = 0;
    parent->summary = rdoc_summary_identity();
    for (size_t i = 0; i < parent->n_children; i++) {
      parent->count += parent->child_counts[i];
      rdoc_summary cs = (parent->children[i]->type == rdoc_st_NODE_LEAF)
          ? ((rdoc_st_leaf*)parent->children[i])->summary
          : ((rdoc_st_internal*)parent->children[i])->summary;
      parent->summary = rdoc_summary_combine(parent->summary, cs);
    }
  }
  t->root.count -= byte_len;
}

static inline void rdoc_transient_insert_text(rdoc_transient* t, size_t text_byte_offset,
                                               const uint8_t* str, size_t byte_len) {
  if (!t->valid) return;

  rdoc_st_set_handlers((rdoc_st_handlers){
    .identity   = rdoc_summary_identity,
    .combine    = rdoc_summary_combine,
    .summarize  = rdoc_summary_summarize,
    .leaf_search = rdoc_leaf_search
  });

  rdoc tmp = {.root = t->root};
  size_t total_text = rdoc_text_byte_count(tmp);
  if (text_byte_offset > total_text) return;
  if (byte_len == 0) return;
  if (!utf8_validate(str, byte_len)) return;
  if (_rdoc_buf_has_sentinel(str, byte_len)) return;

  /* Find raw position */
  size_t raw_pos;
  if (text_byte_offset == total_text) {
    raw_pos = rdoc_total_bytes(tmp);
  } else {
    rdoc_seek_result sr = rdoc_text_byte_to_raw(tmp, text_byte_offset);
    if (!sr.found) return;
    raw_pos = sr.raw_index;
  }

  /* Empty tree: create from string */
  if (!t->root.node) {
    rdoc inserted = rdoc_from_bytes(str, byte_len);
    t->root = inserted.root;
    return;
  }

  /* Check if insert fits in target leaf (enables COW path) */
  rdoc_st_node* check_node = t->root.node;
  size_t check_rem = raw_pos;
  while (check_node->type == rdoc_st_NODE_INTERNAL) {
    rdoc_st_internal* n = (rdoc_st_internal*)check_node;
    size_t ci;
    for (ci = 0; ci < n->n_children - 1; ci++) {
      if (check_rem < n->child_counts[ci]) break;
      check_rem -= n->child_counts[ci];
    }
    check_node = n->children[ci];
  }

  if (((rdoc_st_leaf*)check_node)->count + byte_len <= RDOC_LEAF_BYTES) {
    _rdoc_transient_cow_insert(t, raw_pos, str, byte_len);
  } else {
    /* Fallback: persistent insert + root swap */
    rdoc result = rdoc_insert_text(tmp, text_byte_offset, str, byte_len);
    rdoc_st_unref(t->root);
    t->root = result.root;
  }
}

static inline void rdoc_transient_delete_text(rdoc_transient* t, size_t text_byte_offset,
                                               size_t text_byte_len) {
  if (!t->valid) return;

  rdoc_st_set_handlers((rdoc_st_handlers){
    .identity   = rdoc_summary_identity,
    .combine    = rdoc_summary_combine,
    .summarize  = rdoc_summary_summarize,
    .leaf_search = rdoc_leaf_search
  });

  rdoc tmp = {.root = t->root};
  size_t total_text = rdoc_text_byte_count(tmp);
  if (text_byte_offset >= total_text) return;
  if (text_byte_len == 0) return;
  if (text_byte_offset + text_byte_len > total_text)
    text_byte_len = total_text - text_byte_offset;

  /* Find raw positions for start and end */
  rdoc_seek_result sr_start = rdoc_text_byte_to_raw(tmp, text_byte_offset);
  if (!sr_start.found) return;
  size_t raw_start = sr_start.raw_index;

  size_t raw_end;
  if (text_byte_offset + text_byte_len >= total_text) {
    raw_end = rdoc_total_bytes(tmp);
  } else {
    rdoc_seek_result sr_end = rdoc_text_byte_to_raw(tmp, text_byte_offset + text_byte_len);
    if (!sr_end.found) return;
    raw_end = sr_end.raw_index;
  }

  size_t raw_len = raw_end - raw_start;

  /* COW optimization: if raw_len == text_byte_len (no sentinels in range)
     and range fits within a single leaf, do COW in-place delete */
  if (raw_len == text_byte_len && t->root.node) {
    rdoc_st_node* check_node = t->root.node;
    size_t check_rem = raw_start;
    while (check_node->type == rdoc_st_NODE_INTERNAL) {
      rdoc_st_internal* n = (rdoc_st_internal*)check_node;
      size_t ci;
      for (ci = 0; ci < n->n_children - 1; ci++) {
        if (check_rem < n->child_counts[ci]) break;
        check_rem -= n->child_counts[ci];
      }
      check_node = n->children[ci];
    }

    if (check_rem + raw_len <= ((rdoc_st_leaf*)check_node)->count) {
      _rdoc_transient_cow_delete(t, raw_start, raw_len);
      return;
    }
  }

  /* Fallback: persistent delete + root swap */
  rdoc result = rdoc_delete_text(tmp, text_byte_offset, text_byte_len);
  rdoc_st_unref(t->root);
  t->root = result.root;
}

/* ========================================================================
 * Plain text extraction
 *
 * Extract text content from the document, stripping all sentinels.
 * ======================================================================== */

/* --- rdoc_text_to_buf: copy all text bytes to buffer, skipping sentinels --- */

static inline size_t rdoc_text_to_buf(rdoc d, uint8_t* buf, size_t buf_len) {
  if (buf_len == 0) return 0;
  size_t total = rdoc_total_bytes(d);
  if (total == 0) return 0;

  size_t written = 0;
  rdoc_cursor c = rdoc_cursor_new(d, 0);
  if (!c.valid) return 0;

  do {
    rdoc_token tok = rdoc_cursor_token(&c);
    if (tok.kind == RDOC_TOKEN_TEXT && tok.text_len > 0) {
      size_t to_copy = tok.text_len;
      if (written + to_copy > buf_len) to_copy = buf_len - written;
      memcpy(buf + written, tok.text_ptr, to_copy);
      written += to_copy;
      if (written >= buf_len) break;
    }
  } while (rdoc_cursor_advance(&c));

  rdoc_cursor_free(c);
  return written;
}

/* --- rdoc_text_slice_to_buf: copy text byte range to buffer --- */

static inline size_t rdoc_text_slice_to_buf(rdoc d, size_t text_byte_offset,
                                             size_t text_byte_len,
                                             uint8_t* buf, size_t buf_len) {
  if (buf_len == 0 || text_byte_len == 0) return 0;
  size_t total_text = rdoc_text_byte_count(d);
  if (text_byte_offset >= total_text) return 0;
  if (text_byte_offset + text_byte_len > total_text)
    text_byte_len = total_text - text_byte_offset;

  /* Seek to raw position for start */
  size_t raw_start;
  if (text_byte_offset == 0) {
    raw_start = 0;
  } else {
    rdoc_seek_result sr = rdoc_text_byte_to_raw(d, text_byte_offset);
    if (!sr.found) return 0;
    raw_start = sr.raw_index;
  }

  size_t written = 0;
  size_t text_remaining = text_byte_len;
  rdoc_cursor c = rdoc_cursor_new(d, raw_start);
  if (!c.valid) return 0;

  do {
    rdoc_token tok = rdoc_cursor_token(&c);
    if (tok.kind == RDOC_TOKEN_TEXT && tok.text_len > 0) {
      size_t to_copy = tok.text_len;
      if (to_copy > text_remaining) to_copy = text_remaining;
      if (written + to_copy > buf_len) to_copy = buf_len - written;
      memcpy(buf + written, tok.text_ptr, to_copy);
      written += to_copy;
      text_remaining -= to_copy;
      if (text_remaining == 0 || written >= buf_len) break;
    }
  } while (rdoc_cursor_advance(&c));

  rdoc_cursor_free(c);
  return written;
}

/* --- rdoc_text_to_rope: create plain rope from document text ---
   Only available when rope.h is included before rdoc.h. */

#ifdef ROPE_H
static inline rope rdoc_text_to_rope(rdoc d) {
  size_t text_len = rdoc_text_byte_count(d);
  if (text_len == 0) return rope_new();

  /* Allocate buffer, extract text, build rope */
  uint8_t* buf = (uint8_t*)rdoc_st_allocator.alloc(rdoc_st_allocator.ctx, text_len);
  size_t written = rdoc_text_to_buf(d, buf, text_len);
  rope r = rope_from_str(buf, written);
  rdoc_st_allocator.free(rdoc_st_allocator.ctx, buf);
  return r;
}
#endif /* ROPE_H */

#endif /* RDOC_H */
