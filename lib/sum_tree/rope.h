#ifndef ROPE_H
#define ROPE_H

#include <stddef.h>
#include <stdint.h>

#include "utf8.h"

/* --- Rope summary: multi-dimensional text metrics --- */

typedef struct rope_summary {
  size_t bytes;
  size_t chars;
  size_t lines;
  size_t graphemes;
} rope_summary;

/* --- Monoid functions --- */

static inline rope_summary rope_summary_identity(void) {
  return (rope_summary){0, 0, 0, 0};
}

static inline rope_summary rope_summary_combine(rope_summary a, rope_summary b) {
  return (rope_summary){
    .bytes     = a.bytes + b.bytes,
    .chars     = a.chars + b.chars,
    .lines     = a.lines + b.lines,
    .graphemes = a.graphemes + b.graphemes
  };
}

static inline rope_summary rope_summary_summarize(const uint8_t* elems, size_t count) {
  return (rope_summary){
    .bytes     = count,
    .chars     = utf8_codepoint_count(elems, count),
    .lines     = utf8_line_count(elems, count),
    .graphemes = unicode_grapheme_count(elems, count)
  };
}

/* --- Instantiate sum tree with rope types --- */

#define STREE_T          uint8_t
#define STREE_SUMMARY_T  rope_summary
#define STREE_NAME       rope_st

/* Configurable leaf size: define ROPE_LEAF_MAX before including rope.h to override */
#ifndef ROPE_LEAF_MAX
#define ROPE_LEAF_MAX 256
#endif
#define STREE_LEAF_MAX ROPE_LEAF_MAX
#define ROPE_LEAF_BYTES ROPE_LEAF_MAX

#include "sum_tree.h"

/* --- Public rope type --- */

typedef struct {
  rope_st_root root;
} rope;

/* --- Lifecycle --- */

static inline rope rope_new(void) {
  rope_st_set_handlers((rope_st_handlers){
    .identity  = rope_summary_identity,
    .combine   = rope_summary_combine,
    .summarize = rope_summary_summarize
  });
  return (rope){.root = rope_st_empty()};
}

static inline rope rope_ref(rope r) {
  return r;
}

static inline void rope_unref(rope r) {
  (void)r;
}

/* --- O(1) summary queries --- */

static inline size_t rope_byte_count(rope r) {
  return rope_st_summary(r.root).bytes;
}

static inline size_t rope_char_count(rope r) {
  return rope_st_summary(r.root).chars;
}

static inline size_t rope_line_count(rope r) {
  return rope_st_summary(r.root).lines;
}

static inline size_t rope_grapheme_count(rope r) {
  return rope_st_summary(r.root).graphemes;
}

/* --- Construction from string --- */

static inline rope rope_from_str(const uint8_t* str, size_t byte_len) {
  rope_st_set_handlers((rope_st_handlers){
    .identity  = rope_summary_identity,
    .combine   = rope_summary_combine,
    .summarize = rope_summary_summarize
  });

  if (byte_len == 0 || !utf8_validate(str, byte_len)) {
    return (rope){.root = rope_st_empty()};
  }

  /* Build leaves with codepoint-aligned boundaries */
  rope_st_node* stack_buf[512];
  rope_st_node** leaf_buf = stack_buf;
  size_t est_max = (byte_len + ROPE_LEAF_BYTES - 1) / ROPE_LEAF_BYTES + 1;
  if (est_max > 512) {
    leaf_buf = (rope_st_node**)rope_st_allocator.alloc(
        rope_st_allocator.ctx, sizeof(rope_st_node*) * est_max);
  }

  size_t n_leaves = 0;
  size_t pos = 0;

  while (pos < byte_len) {
    size_t end = pos + ROPE_LEAF_BYTES;
    if (end >= byte_len) {
      end = byte_len;
    } else {
      end = grapheme_find_safe_split(str, byte_len, end);
    }
    leaf_buf[n_leaves++] = (rope_st_node*)rope_st_mk_leaf(str + pos, end - pos);
    pos = end;
  }

  /* Assemble tree from leaves */
  rope_st_root root = rope_st_mk_root(leaf_buf, n_leaves, 1);

  if (leaf_buf != stack_buf) {
    rope_st_allocator.free(rope_st_allocator.ctx, leaf_buf);
  }

  return (rope){.root = root};
}

/* --- Text extraction --- */

static inline size_t rope_to_str(rope r, uint8_t* buf, size_t buf_len) {
  size_t total = rope_byte_count(r);
  size_t to_copy = buf_len < total ? buf_len : total;
  return rope_st_copy_range(r.root, 0, to_copy, buf);
}

/* --- Grapheme-safe concat --- */

static inline rope rope_concat(rope left, rope right) {
  rope_st_set_handlers((rope_st_handlers){
    .identity  = rope_summary_identity,
    .combine   = rope_summary_combine,
    .summarize = rope_summary_summarize
  });

  size_t lb = rope_byte_count(left);
  size_t rb = rope_byte_count(right);

  if (lb == 0) return rope_ref(right);
  if (rb == 0) return rope_ref(left);

  /* O(1) check: decode first codepoint of right */
  uint8_t peek[4];
  size_t peek_n = rb < 4 ? rb : 4;
  rope_st_copy_range(right.root, 0, peek_n, peek);
  uint32_t first_cp;
  size_t first_cplen = utf8_decode(peek, peek_n, &first_cp);

  if (first_cplen > 0) {
    UnicodeGraphemeBreak gbp = unicode_grapheme_break(first_cp);
    if (gbp == GBP_EXTEND || gbp == GBP_ZWJ || gbp == GBP_SPACINGMARK) {
      /* Right starts with combining codepoints — fix-up needed.
         Move combining prefix from right to left's end so grapheme clusters
         at the junction are not split across separate leaves/subtrees. */

      /* Scan all leading combining codepoints in right */
      uint8_t scan_buf[128];
      size_t scan_len = rb < sizeof(scan_buf) ? rb : sizeof(scan_buf);
      rope_st_copy_range(right.root, 0, scan_len, scan_buf);

      size_t comb_len = 0;
      {
        size_t p = 0;
        while (p < scan_len) {
          uint32_t cp;
          size_t n = utf8_decode(scan_buf + p, scan_len - p, &cp);
          if (n == 0) break;
          UnicodeGraphemeBreak g = unicode_grapheme_break(cp);
          if (g != GBP_EXTEND && g != GBP_ZWJ && g != GBP_SPACINGMARK) break;
          p += n;
        }
        comb_len = p;
      }

      if (comb_len > 0) {
        /* Get left's rightmost leaf */
        rope_st_node* ln = left.root.node;
        while (ln->type == rope_st_NODE_INTERNAL) {
          rope_st_internal* ni = (rope_st_internal*)ln;
          ln = ni->children[ni->n_children - 1];
        }
        rope_st_leaf* left_last = (rope_st_leaf*)ln;

        /* Build junction buffer: left_last content + combining prefix */
        size_t junction_len = left_last->count + comb_len;
        uint8_t junction_buf[ROPE_LEAF_MAX + 128];
        memcpy(junction_buf, left_last->elements, left_last->count);
        memcpy(junction_buf + left_last->count, scan_buf, comb_len);

        /* Split left before its rightmost leaf */
        size_t left_prefix_bytes = lb - left_last->count;
        rope_st_split_result lsp = rope_st_split(left.root, left_prefix_bytes);

        /* Split right after combining prefix */
        rope_st_split_result rsp = rope_st_split(right.root, comb_len);

        /* Create junction rope with correct grapheme-safe leaf splitting */
        rope junction = rope_from_str(junction_buf, junction_len);

        /* Build result: left_prefix + junction + right_rest */
        rope_st_root tmp;
        if (lsp.left.node) {
          tmp = rope_st_concat(lsp.left, junction.root);
        } else {
          tmp = junction.root;
        }

        rope_st_root result;
        if (rsp.right.node) {
          result = rope_st_concat(tmp, rsp.right);
        } else {
          result = tmp;
        }

        return (rope){.root = result};
      }
    }
  }

  /* No fix-up needed — standard concat */
  return (rope){.root = rope_st_concat(left.root, right.root)};
}

/* --- Dimension extractors for search-by-summary --- */

static inline size_t rope_dim_bytes(rope_summary s)     { return s.bytes; }
static inline size_t rope_dim_chars(rope_summary s)     { return s.chars; }
static inline size_t rope_dim_lines(rope_summary s)     { return s.lines; }
static inline size_t rope_dim_graphemes(rope_summary s) { return s.graphemes; }

/* --- Column dimension for multi-dimensional seek --- */

typedef enum {
  ROPE_DIM_BYTES,
  ROPE_DIM_CHARS,
  ROPE_DIM_GRAPHEMES
} rope_dimension;

/* --- Offset result for dimension conversions --- */

typedef struct rope_offset_result {
  size_t value;
  bool   found;
} rope_offset_result;

/* --- Seek by dimension: byte/char/line/grapheme conversions --- */

static inline rope_offset_result rope_byte_to_char(rope r, size_t byte_offset) {
  if (byte_offset == 0) return (rope_offset_result){0, true};
  rope_st_search_result sr = rope_st_search(r.root, rope_dim_bytes, byte_offset);
  if (!sr.found) return (rope_offset_result){0, false};
  return (rope_offset_result){sr.prefix_summary.chars, true};
}

static inline rope_offset_result rope_char_to_byte(rope r, size_t char_offset) {
  if (char_offset == 0) return (rope_offset_result){0, true};
  rope_st_search_result sr = rope_st_search(r.root, rope_dim_chars, char_offset);
  if (!sr.found) return (rope_offset_result){0, false};
  return (rope_offset_result){sr.prefix_summary.bytes, true};
}

static inline rope_offset_result rope_line_to_byte(rope r, size_t line_number) {
  if (line_number == 0) return (rope_offset_result){0, true};
  /* Search for the (line_number-1)th newline; the line starts at the byte after it */
  rope_st_search_result sr = rope_st_search(r.root, rope_dim_lines, line_number - 1);
  if (!sr.found) return (rope_offset_result){0, false};
  /* sr.index is the byte index of the newline; line starts at next byte */
  return (rope_offset_result){sr.index + 1, true};
}

static inline rope_offset_result rope_byte_to_line(rope r, size_t byte_offset) {
  if (byte_offset == 0) return (rope_offset_result){0, true};
  rope_st_search_result sr = rope_st_search(r.root, rope_dim_bytes, byte_offset);
  if (!sr.found) return (rope_offset_result){0, false};
  return (rope_offset_result){sr.prefix_summary.lines, true};
}

/* Grapheme conversions require custom tree walking because grapheme boundaries
   depend on codepoint values, which can't be determined from individual bytes.
   Node-level summaries are correct; only the leaf-level scan needs special handling. */

static inline rope_offset_result rope_grapheme_to_byte(rope r, size_t grapheme_offset) {
  if (grapheme_offset == 0) return (rope_offset_result){0, true};
  size_t total_graphemes = rope_grapheme_count(r);
  if (grapheme_offset >= total_graphemes) return (rope_offset_result){0, false};

  rope_st_node* node = r.root.node;
  size_t acc_graphemes = 0;
  size_t acc_bytes = 0;

  /* Navigate internal nodes using correct child summaries */
  while (node->type == rope_st_NODE_INTERNAL) {
    rope_st_internal* n = (rope_st_internal*)node;
    size_t i;
    for (i = 0; i < n->n_children; i++) {
      rope_summary child_sum;
      if (n->children[i]->type == rope_st_NODE_LEAF)
        child_sum = ((rope_st_leaf*)n->children[i])->summary;
      else
        child_sum = ((rope_st_internal*)n->children[i])->summary;
      if (acc_graphemes + child_sum.graphemes > grapheme_offset) {
        node = n->children[i];
        break;
      }
      acc_graphemes += child_sum.graphemes;
      acc_bytes += child_sum.bytes;
    }
    if (i == n->n_children) return (rope_offset_result){0, false};
  }

  /* Within leaf, use proper UTF-8 grapheme scanning */
  rope_st_leaf* leaf = (rope_st_leaf*)node;
  size_t target_in_leaf = grapheme_offset - acc_graphemes;
  size_t byte_in_leaf = unicode_grapheme_byte_offset(leaf->elements, leaf->count, target_in_leaf);
  return (rope_offset_result){acc_bytes + byte_in_leaf, true};
}

static inline rope_offset_result rope_byte_to_grapheme(rope r, size_t byte_offset) {
  if (byte_offset == 0) return (rope_offset_result){0, true};
  size_t total_bytes = rope_byte_count(r);
  if (byte_offset >= total_bytes) return (rope_offset_result){0, false};

  rope_st_node* node = r.root.node;
  size_t acc_graphemes = 0;
  size_t acc_bytes = 0;

  /* Navigate internal nodes using correct child summaries */
  while (node->type == rope_st_NODE_INTERNAL) {
    rope_st_internal* n = (rope_st_internal*)node;
    size_t i;
    for (i = 0; i < n->n_children; i++) {
      rope_summary child_sum;
      if (n->children[i]->type == rope_st_NODE_LEAF)
        child_sum = ((rope_st_leaf*)n->children[i])->summary;
      else
        child_sum = ((rope_st_internal*)n->children[i])->summary;
      if (acc_bytes + child_sum.bytes > byte_offset) {
        node = n->children[i];
        break;
      }
      acc_bytes += child_sum.bytes;
      acc_graphemes += child_sum.graphemes;
    }
    if (i == n->n_children) return (rope_offset_result){0, false};
  }

  /* Within leaf, count graphemes up to the target byte offset */
  rope_st_leaf* leaf = (rope_st_leaf*)node;
  size_t remaining = byte_offset - acc_bytes;
  acc_graphemes += unicode_grapheme_count(leaf->elements, remaining);
  return (rope_offset_result){acc_graphemes, true};
}

/* --- Insert --- */

static inline rope rope_insert(rope r, size_t byte_offset, const uint8_t* str, size_t byte_len) {
  size_t total = rope_byte_count(r);

  /* Validate: offset must not exceed byte count */
  if (byte_offset > total) return rope_ref(r);

  /* Validate: offset must be on a codepoint boundary */
  if (byte_offset > 0 && byte_offset < total) {
    /* Read the byte at byte_offset to check if it's a continuation byte */
    rope_st_get_result gr = rope_st_get(r.root, byte_offset);
    if (gr.found && utf8_is_continuation(gr.value)) return rope_ref(r);
  }

  /* Empty insert returns equivalent rope */
  if (byte_len == 0) return rope_ref(r);

  rope inserted = rope_from_str(str, byte_len);
  if (rope_byte_count(inserted) == 0) {
    /* Invalid UTF-8 in str — from_str returned empty */
    return rope_ref(r);
  }

  rope_st_split_result sp = rope_st_split(r.root, byte_offset);
  rope_st_root tmp = rope_st_concat(sp.left, inserted.root);
  rope_st_root result = rope_st_concat(tmp, sp.right);

  return (rope){.root = result};
}

/* --- Delete --- */

static inline rope rope_delete(rope r, size_t byte_offset, size_t byte_len) {
  size_t total = rope_byte_count(r);

  /* Validate: offset must not exceed byte count */
  if (byte_offset > total) return rope_ref(r);

  /* Zero-length delete returns equivalent rope */
  if (byte_len == 0) return rope_ref(r);

  /* Clamp to available bytes */
  if (byte_offset + byte_len > total) byte_len = total - byte_offset;

  /* Validate: both boundaries must be on codepoint boundaries */
  if (byte_offset > 0 && byte_offset < total) {
    rope_st_get_result gr = rope_st_get(r.root, byte_offset);
    if (gr.found && utf8_is_continuation(gr.value)) return rope_ref(r);
  }
  size_t end = byte_offset + byte_len;
  if (end > 0 && end < total) {
    rope_st_get_result gr = rope_st_get(r.root, end);
    if (gr.found && utf8_is_continuation(gr.value)) return rope_ref(r);
  }

  rope_st_split_result sp1 = rope_st_split(r.root, byte_offset);
  rope_st_split_result sp2 = rope_st_split(sp1.right, byte_len);
  rope_st_root result = rope_st_concat(sp1.left, sp2.right);

  return (rope){.root = result};
}

/* --- Slice extraction --- */

static inline rope rope_slice(rope r, size_t byte_offset, size_t byte_len) {
  size_t total = rope_byte_count(r);

  /* Empty slice */
  if (byte_len == 0) return rope_new();

  /* Out of bounds */
  if (byte_offset >= total) return rope_new();

  /* Clamp to available bytes */
  if (byte_offset + byte_len > total) byte_len = total - byte_offset;

  /* Validate: both boundaries must be on codepoint boundaries */
  if (byte_offset > 0) {
    rope_st_get_result gr = rope_st_get(r.root, byte_offset);
    if (gr.found && utf8_is_continuation(gr.value)) return rope_new();
  }
  size_t end = byte_offset + byte_len;
  if (end < total) {
    rope_st_get_result gr = rope_st_get(r.root, end);
    if (gr.found && utf8_is_continuation(gr.value)) return rope_new();
  }

  rope_st_split_result sp1 = rope_st_split(r.root, byte_offset);
  rope_st_split_result sp2 = rope_st_split(sp1.right, byte_len);

  return (rope){.root = sp2.left};
}

static inline size_t rope_slice_to_str(rope r, size_t byte_offset, size_t byte_len,
                                        uint8_t* buf, size_t buf_len) {
  size_t total = rope_byte_count(r);
  if (byte_offset >= total) return 0;
  if (byte_offset + byte_len > total) byte_len = total - byte_offset;
  if (byte_len > buf_len) byte_len = buf_len;
  return rope_st_copy_range(r.root, byte_offset, byte_len, buf);
}

static inline rope rope_line_slice(rope r, size_t start_line, size_t end_line) {
  if (start_line >= end_line) return rope_new();

  rope_offset_result start_res = rope_line_to_byte(r, start_line);
  if (!start_res.found) return rope_new();

  size_t start_byte = start_res.value;
  size_t end_byte;

  rope_offset_result end_res = rope_line_to_byte(r, end_line);
  if (!end_res.found) {
    /* end_line is beyond last line — slice to end of rope */
    end_byte = rope_byte_count(r);
  } else {
    end_byte = end_res.value;
  }

  if (end_byte <= start_byte) return rope_new();
  return rope_slice(r, start_byte, end_byte - start_byte);
}

/* --- Convenience insert by char/line offset --- */

static inline rope rope_insert_at_char(rope r, size_t char_offset, const uint8_t* str, size_t byte_len) {
  rope_offset_result res = rope_char_to_byte(r, char_offset);
  if (!res.found) return rope_ref(r);
  return rope_insert(r, res.value, str, byte_len);
}

static inline rope rope_insert_at_line(rope r, size_t line_number, const uint8_t* str, size_t byte_len) {
  rope_offset_result res = rope_line_to_byte(r, line_number);
  if (!res.found) return rope_ref(r);
  return rope_insert(r, res.value, str, byte_len);
}

/* --- Convenience delete by char/line offset --- */

static inline rope rope_delete_at_char(rope r, size_t char_offset, size_t char_count) {
  if (char_count == 0) return rope_ref(r);
  rope_offset_result start_res = rope_char_to_byte(r, char_offset);
  if (!start_res.found) return rope_ref(r);
  rope_offset_result end_res = rope_char_to_byte(r, char_offset + char_count);
  size_t end_byte;
  if (!end_res.found) {
    /* char_offset + char_count past end — clamp to end of rope */
    end_byte = rope_byte_count(r);
  } else {
    end_byte = end_res.value;
  }
  return rope_delete(r, start_res.value, end_byte - start_res.value);
}

static inline rope rope_delete_at_line(rope r, size_t start_line, size_t end_line) {
  if (start_line >= end_line) return rope_ref(r);
  rope_offset_result start_res = rope_line_to_byte(r, start_line);
  if (!start_res.found) return rope_ref(r);
  rope_offset_result end_res = rope_line_to_byte(r, end_line);
  size_t end_byte;
  if (!end_res.found) {
    end_byte = rope_byte_count(r);
  } else {
    end_byte = end_res.value;
  }
  if (end_byte <= start_res.value) return rope_ref(r);
  return rope_delete(r, start_res.value, end_byte - start_res.value);
}

/* --- Convenience slice by char/grapheme offset --- */

static inline rope rope_slice_by_chars(rope r, size_t char_offset, size_t char_count) {
  if (char_count == 0) return rope_new();
  rope_offset_result start_res = rope_char_to_byte(r, char_offset);
  if (!start_res.found) return rope_new();
  rope_offset_result end_res = rope_char_to_byte(r, char_offset + char_count);
  size_t end_byte;
  if (!end_res.found) {
    end_byte = rope_byte_count(r);
  } else {
    end_byte = end_res.value;
  }
  if (end_byte <= start_res.value) return rope_new();
  return rope_slice(r, start_res.value, end_byte - start_res.value);
}

static inline rope rope_slice_by_graphemes(rope r, size_t grapheme_offset, size_t grapheme_count) {
  if (grapheme_count == 0) return rope_new();
  rope_offset_result start_res = rope_grapheme_to_byte(r, grapheme_offset);
  if (!start_res.found) return rope_new();
  rope_offset_result end_res = rope_grapheme_to_byte(r, grapheme_offset + grapheme_count);
  size_t end_byte;
  if (!end_res.found) {
    end_byte = rope_byte_count(r);
  } else {
    end_byte = end_res.value;
  }
  if (end_byte <= start_res.value) return rope_new();
  return rope_slice(r, start_res.value, end_byte - start_res.value);
}

/* --- Replace --- */

static inline rope rope_replace(rope r, size_t byte_offset, size_t byte_len,
                                 const uint8_t* str, size_t str_len) {
  rope after_delete = rope_delete(r, byte_offset, byte_len);
  return rope_insert(after_delete, byte_offset, str, str_len);
}

/* --- Cursor --- */

#define ROPE_CURSOR_MAX_DEPTH 16

typedef struct {
  rope_st_node* root;
  struct {
    rope_st_node* node;
    size_t child_index;
  } path[ROPE_CURSOR_MAX_DEPTH];
  int depth;
  rope_st_leaf* leaf;
  size_t leaf_offset;
  rope_summary prefix;
  bool valid;
} rope_cursor;

/* Helper: get summary from any node */
static inline rope_summary _rope_cursor_node_summary(rope_st_node* node) {
  if (node->type == rope_st_NODE_LEAF)
    return ((rope_st_leaf*)node)->summary;
  return ((rope_st_internal*)node)->summary;
}

static inline rope_cursor rope_cursor_new(rope r, size_t byte_offset) {
  rope_cursor c;
  memset(&c, 0, sizeof(c));
  c.prefix = rope_summary_identity();

  size_t total = rope_byte_count(r);
  if (byte_offset > total) {
    c.valid = false;
    return c;
  }

  c.root = r.root.node;
  c.valid = true;

  if (!c.root) {
    c.leaf = NULL;
    c.leaf_offset = 0;
    return c;
  }

  if (byte_offset == total) {
    /* Position at end: descend to rightmost leaf */
    rope_st_node* node = c.root;
    int depth = 0;
    while (node->type == rope_st_NODE_INTERNAL) {
      rope_st_internal* n = (rope_st_internal*)node;
      c.path[depth].node = node;
      for (size_t i = 0; i < n->n_children - 1; i++) {
        c.prefix = rope_summary_combine(c.prefix,
            _rope_cursor_node_summary(n->children[i]));
      }
      c.path[depth].child_index = n->n_children - 1;
      node = n->children[n->n_children - 1];
      depth++;
    }
    c.leaf = (rope_st_leaf*)node;
    c.leaf_offset = c.leaf->count;
    c.depth = depth;
    return c;
  }

  /* Normal case: descend to leaf containing byte_offset */
  rope_st_node* node = c.root;
  size_t remaining = byte_offset;
  int depth = 0;

  while (node->type == rope_st_NODE_INTERNAL) {
    rope_st_internal* n = (rope_st_internal*)node;
    c.path[depth].node = node;
    for (size_t i = 0; i < n->n_children; i++) {
      rope_summary child_sum = _rope_cursor_node_summary(n->children[i]);
      if (remaining < child_sum.bytes) {
        c.path[depth].child_index = i;
        node = n->children[i];
        break;
      }
      c.prefix = rope_summary_combine(c.prefix, child_sum);
      remaining -= child_sum.bytes;
    }
    depth++;
  }

  c.leaf = (rope_st_leaf*)node;
  c.leaf_offset = remaining;
  c.depth = depth;
  return c;
}

static inline void rope_cursor_free(rope_cursor c) {
  (void)c; /* no-op for stack-allocated cursor */
}

/* --- Cursor position queries --- */

static inline size_t rope_cursor_byte_offset(rope_cursor* c) {
  if (!c->valid) return 0;
  return c->prefix.bytes + c->leaf_offset;
}

static inline size_t rope_cursor_char_offset(rope_cursor* c) {
  if (!c->valid) return 0;
  if (!c->leaf) return 0;
  return c->prefix.chars + utf8_codepoint_count(c->leaf->elements, c->leaf_offset);
}

static inline size_t rope_cursor_line(rope_cursor* c) {
  if (!c->valid) return 0;
  if (!c->leaf) return 0;
  return c->prefix.lines + utf8_line_count(c->leaf->elements, c->leaf_offset);
}

static inline size_t rope_cursor_grapheme_offset(rope_cursor* c) {
  if (!c->valid) return 0;
  if (!c->leaf) return 0;
  return c->prefix.graphemes + unicode_grapheme_count(c->leaf->elements, c->leaf_offset);
}

/* --- Summary subtraction (for backward navigation) --- */

static inline rope_summary rope_summary_subtract(rope_summary a, rope_summary b) {
  return (rope_summary){
    .bytes     = a.bytes - b.bytes,
    .chars     = a.chars - b.chars,
    .lines     = a.lines - b.lines,
    .graphemes = a.graphemes - b.graphemes
  };
}

/* --- Cursor internal: move to next leaf --- */

static inline bool _rope_cursor_next_leaf(rope_cursor* c) {
  int d = c->depth;
  while (d > 0) {
    d--;
    rope_st_internal* parent = (rope_st_internal*)c->path[d].node;
    size_t next_child = c->path[d].child_index + 1;
    if (next_child < parent->n_children) {
      c->path[d].child_index = next_child;
      rope_st_node* node = parent->children[next_child];
      d++;
      while (node->type == rope_st_NODE_INTERNAL) {
        rope_st_internal* n = (rope_st_internal*)node;
        c->path[d].node = node;
        c->path[d].child_index = 0;
        node = n->children[0];
        d++;
      }
      c->depth = d;
      c->leaf = (rope_st_leaf*)node;
      c->leaf_offset = 0;
      return true;
    }
  }
  return false;
}

/* --- Cursor internal: move to previous leaf --- */

static inline bool _rope_cursor_prev_leaf(rope_cursor* c) {
  int d = c->depth;
  while (d > 0) {
    d--;
    rope_st_internal* parent = (rope_st_internal*)c->path[d].node;
    if (c->path[d].child_index > 0) {
      c->path[d].child_index--;
      rope_st_node* node = parent->children[c->path[d].child_index];
      d++;
      while (node->type == rope_st_NODE_INTERNAL) {
        rope_st_internal* n = (rope_st_internal*)node;
        c->path[d].node = node;
        c->path[d].child_index = n->n_children - 1;
        node = n->children[n->n_children - 1];
        d++;
      }
      c->depth = d;
      c->leaf = (rope_st_leaf*)node;
      c->leaf_offset = c->leaf->count;
      return true;
    }
  }
  return false;
}

/* --- Cursor peek and read --- */

static inline int rope_cursor_peek(rope_cursor* c) {
  if (!c->valid) return -1;
  if (!c->leaf) return -1;
  if (c->leaf_offset < c->leaf->count) {
    return (int)c->leaf->elements[c->leaf_offset];
  }
  /* At end of leaf — check next leaf via temporary copy */
  rope_cursor tmp = *c;
  tmp.prefix = rope_summary_combine(tmp.prefix, tmp.leaf->summary);
  if (!_rope_cursor_next_leaf(&tmp)) return -1;
  return (int)tmp.leaf->elements[0];
}

static inline size_t rope_cursor_read(rope_cursor* c, uint8_t* buf, size_t max_bytes) {
  if (!c->valid || max_bytes == 0) return 0;
  if (!c->leaf) return 0;

  rope_cursor tmp = *c;
  size_t total_read = 0;

  while (total_read < max_bytes) {
    if (tmp.leaf_offset >= tmp.leaf->count) {
      tmp.prefix = rope_summary_combine(tmp.prefix, tmp.leaf->summary);
      if (!_rope_cursor_next_leaf(&tmp)) break;
    }
    size_t avail = tmp.leaf->count - tmp.leaf_offset;
    size_t to_copy = max_bytes - total_read;
    if (to_copy > avail) to_copy = avail;
    memcpy(buf + total_read, tmp.leaf->elements + tmp.leaf_offset, to_copy);
    total_read += to_copy;
    tmp.leaf_offset += to_copy;
  }

  return total_read;
}

/* --- Cursor advance functions --- */

static inline bool rope_cursor_advance_bytes(rope_cursor* c, size_t n) {
  if (!c->valid) return false;
  if (n == 0) return true;
  if (!c->leaf) return false;

  /* Pre-check: ensure we won't go past end */
  size_t total = _rope_cursor_node_summary(c->root).bytes;
  size_t current = c->prefix.bytes + c->leaf_offset;
  if (current + n > total) return false;

  size_t remaining = n;
  while (remaining > 0) {
    size_t avail = c->leaf->count - c->leaf_offset;
    if (remaining <= avail) {
      c->leaf_offset += remaining;
      return true;
    }
    remaining -= avail;
    c->prefix = rope_summary_combine(c->prefix, c->leaf->summary);
    _rope_cursor_next_leaf(c);
  }
  return true;
}

static inline bool rope_cursor_advance_chars(rope_cursor* c, size_t n) {
  if (!c->valid) return false;
  if (n == 0) return true;
  if (!c->leaf) return false;

  rope_cursor saved = *c;
  size_t remaining = n;

  while (remaining > 0) {
    size_t chars_in_rest = utf8_codepoint_count(
        c->leaf->elements + c->leaf_offset,
        c->leaf->count - c->leaf_offset);

    if (remaining <= chars_in_rest) {
      /* Advance within this leaf by remaining codepoints */
      size_t pos = c->leaf_offset;
      size_t count = 0;
      while (count < remaining && pos < c->leaf->count) {
        size_t cplen = utf8_codepoint_length(c->leaf->elements[pos]);
        if (cplen == 0) cplen = 1;
        pos += cplen;
        count++;
      }
      c->leaf_offset = pos;
      return true;
    }

    remaining -= chars_in_rest;
    c->prefix = rope_summary_combine(c->prefix, c->leaf->summary);
    if (!_rope_cursor_next_leaf(c)) {
      if (remaining == 0) return true;
      *c = saved;
      return false;
    }
  }
  return true;
}

static inline bool rope_cursor_advance_lines(rope_cursor* c, size_t n) {
  if (!c->valid) return false;
  if (n == 0) return true;
  if (!c->leaf) return false;

  rope_cursor saved = *c;
  size_t remaining = n;

  while (remaining > 0) {
    size_t lines_in_rest = utf8_line_count(
        c->leaf->elements + c->leaf_offset,
        c->leaf->count - c->leaf_offset);

    if (remaining <= lines_in_rest) {
      /* Find the remaining-th newline within this leaf */
      size_t pos = c->leaf_offset;
      size_t found = 0;
      while (pos < c->leaf->count) {
        if (c->leaf->elements[pos] == '\n') {
          found++;
          if (found == remaining) {
            c->leaf_offset = pos + 1;
            return true;
          }
        }
        pos++;
      }
    }

    remaining -= lines_in_rest;
    c->prefix = rope_summary_combine(c->prefix, c->leaf->summary);
    if (!_rope_cursor_next_leaf(c)) {
      if (remaining == 0) return true;
      *c = saved;
      return false;
    }
  }
  return true;
}

static inline bool rope_cursor_advance_graphemes(rope_cursor* c, size_t n) {
  if (!c->valid) return false;
  if (n == 0) return true;
  if (!c->leaf) return false;

  rope_cursor saved = *c;
  size_t remaining = n;

  while (remaining > 0) {
    size_t graphemes_in_rest = unicode_grapheme_count(
        c->leaf->elements + c->leaf_offset,
        c->leaf->count - c->leaf_offset);

    if (remaining < graphemes_in_rest) {
      c->leaf_offset += unicode_grapheme_byte_offset(
          c->leaf->elements + c->leaf_offset,
          c->leaf->count - c->leaf_offset,
          remaining);
      return true;
    }

    remaining -= graphemes_in_rest;
    c->prefix = rope_summary_combine(c->prefix, c->leaf->summary);
    if (!_rope_cursor_next_leaf(c)) {
      if (remaining == 0) return true;
      *c = saved;
      return false;
    }

    /* Skip extending characters at start of new leaf (cross-leaf grapheme cluster) */
    while (c->leaf_offset < c->leaf->count) {
      uint32_t cp;
      size_t consumed = utf8_decode(
          c->leaf->elements + c->leaf_offset,
          c->leaf->count - c->leaf_offset, &cp);
      if (consumed == 0) break;
      UnicodeGraphemeBreak gbp = unicode_grapheme_break(cp);
      if (gbp == GBP_EXTEND || gbp == GBP_ZWJ || gbp == GBP_SPACINGMARK) {
        c->leaf_offset += consumed;
      } else {
        break;
      }
    }
  }
  return true;
}

/* --- Cursor editing functions --- */

static inline rope rope_cursor_insert(rope_cursor* c, const uint8_t* str, size_t byte_len) {
  if (!c->valid) return rope_new();
  size_t offset = rope_cursor_byte_offset(c);
  size_t total = c->root ? _rope_cursor_node_summary(c->root).bytes : 0;
  size_t height = c->root ? (size_t)(c->depth + 1) : 0;
  rope tmp = (rope){.root = (rope_st_root){.node = c->root, .count = total, .height = height}};
  rope result = rope_insert(tmp, offset, str, byte_len);
  c->valid = false;
  return result;
}

static inline rope rope_cursor_delete(rope_cursor* c, size_t byte_len) {
  if (!c->valid) return rope_new();
  size_t offset = rope_cursor_byte_offset(c);
  size_t total = c->root ? _rope_cursor_node_summary(c->root).bytes : 0;
  size_t height = c->root ? (size_t)(c->depth + 1) : 0;
  rope tmp = (rope){.root = (rope_st_root){.node = c->root, .count = total, .height = height}};
  rope result = rope_delete(tmp, offset, byte_len);
  c->valid = false;
  return result;
}

static inline rope rope_cursor_replace(rope_cursor* c, size_t delete_len,
                                        const uint8_t* str, size_t insert_len) {
  if (!c->valid) return rope_new();
  size_t offset = rope_cursor_byte_offset(c);
  size_t total = c->root ? _rope_cursor_node_summary(c->root).bytes : 0;
  size_t height = c->root ? (size_t)(c->depth + 1) : 0;
  rope tmp = (rope){.root = (rope_st_root){.node = c->root, .count = total, .height = height}};
  rope result = rope_replace(tmp, offset, delete_len, str, insert_len);
  c->valid = false;
  return result;
}

/* --- Cursor retreat functions --- */

static inline bool rope_cursor_retreat_bytes(rope_cursor* c, size_t n) {
  if (!c->valid) return false;
  if (n == 0) return true;
  if (!c->leaf) return false;

  size_t current = c->prefix.bytes + c->leaf_offset;
  if (n > current) return false;

  size_t remaining = n;
  while (remaining > 0) {
    if (remaining <= c->leaf_offset) {
      c->leaf_offset -= remaining;
      return true;
    }
    remaining -= c->leaf_offset;
    if (!_rope_cursor_prev_leaf(c)) return false;
    c->prefix = rope_summary_subtract(c->prefix, c->leaf->summary);
  }
  return true;
}

static inline bool rope_cursor_retreat_chars(rope_cursor* c, size_t n) {
  if (!c->valid) return false;
  if (n == 0) return true;
  if (!c->leaf) return false;

  size_t current_chars = rope_cursor_char_offset(c);
  if (n > current_chars) return false;

  size_t remaining = n;
  while (remaining > 0) {
    size_t chars_before = utf8_codepoint_count(c->leaf->elements, c->leaf_offset);
    if (remaining <= chars_before) {
      /* Position at (chars_before - remaining) codepoints from leaf start */
      size_t target = chars_before - remaining;
      size_t pos = 0;
      size_t count = 0;
      while (count < target) {
        size_t cplen = utf8_codepoint_length(c->leaf->elements[pos]);
        if (cplen == 0) cplen = 1;
        pos += cplen;
        count++;
      }
      c->leaf_offset = pos;
      return true;
    }
    remaining -= chars_before;
    if (!_rope_cursor_prev_leaf(c)) return false;
    c->prefix = rope_summary_subtract(c->prefix, c->leaf->summary);
  }
  return true;
}

static inline bool rope_cursor_retreat_lines(rope_cursor* c, size_t n) {
  if (!c->valid) return false;
  if (n == 0) return true;
  if (!c->leaf) return false;

  size_t current_line = rope_cursor_line(c);
  if (n > current_line) return false;

  size_t newlines_to_cross = n;

  /* Phase 1: cross n newlines going backward */
  while (newlines_to_cross > 0) {
    while (c->leaf_offset > 0) {
      c->leaf_offset--;
      if (c->leaf->elements[c->leaf_offset] == '\n') {
        newlines_to_cross--;
        if (newlines_to_cross == 0) goto find_line_start;
      }
    }
    if (!_rope_cursor_prev_leaf(c)) return false;
    c->prefix = rope_summary_subtract(c->prefix, c->leaf->summary);
  }

find_line_start:
  /* Phase 2: scan backward to find start of line (byte after previous '\n' or byte 0) */
  while (c->leaf_offset > 0) {
    c->leaf_offset--;
    if (c->leaf->elements[c->leaf_offset] == '\n') {
      c->leaf_offset++;
      return true;
    }
  }
  while (_rope_cursor_prev_leaf(c)) {
    c->prefix = rope_summary_subtract(c->prefix, c->leaf->summary);
    while (c->leaf_offset > 0) {
      c->leaf_offset--;
      if (c->leaf->elements[c->leaf_offset] == '\n') {
        c->leaf_offset++;
        return true;
      }
    }
  }
  /* Reached start of text */
  return true;
}

/* --- Transient COW Rope --- */

typedef struct {
  rope_st_root root;
  bool valid;
} rope_transient;

/* Helper: clone an internal node */
static inline rope_st_node* _rope_transient_clone_internal(rope_st_internal* old) {
  return (rope_st_node*)rope_st_mk_internal(old->children, old->n_children);
}

/* Helper: always clone a node for exclusive mutation (GC has no refcount) */
static inline rope_st_node* _rope_transient_ensure_exclusive(rope_st_node* node) {
  if (node->type == rope_st_NODE_LEAF) {
    rope_st_leaf* old = (rope_st_leaf*)node;
    return (rope_st_node*)rope_st_mk_leaf(old->elements, old->count);
  }
  return _rope_transient_clone_internal((rope_st_internal*)node);
}

static inline rope_transient rope_to_transient(rope r) {
  rope_st_set_handlers((rope_st_handlers){
    .identity  = rope_summary_identity,
    .combine   = rope_summary_combine,
    .summarize = rope_summary_summarize
  });
  return (rope_transient){
    .root = r.root,
    .valid = true
  };
}

static inline rope rope_transient_to_persistent(rope_transient* t) {
  if (!t->valid) return rope_new();
  rope result = (rope){.root = t->root};
  t->root = rope_st_empty();
  t->valid = false;
  return result;
}

static inline void rope_transient_free(rope_transient* t) {
  if (!t->valid) return;
  t->root = rope_st_empty();
  t->valid = false;
}

/* COW in-place insert: walk from root to target leaf, cloning shared nodes */
static inline void _rope_transient_cow_insert(rope_transient* t, size_t byte_offset,
                                               const uint8_t* str, size_t byte_len) {
  /* Ensure root exclusive */
  t->root.node = _rope_transient_ensure_exclusive(t->root.node);
  rope_st_node* node = t->root.node;

  /* Single-leaf tree: modify directly */
  if (node->type == rope_st_NODE_LEAF) {
    rope_st_leaf* leaf = (rope_st_leaf*)node;
    memmove(leaf->elements + byte_offset + byte_len,
            leaf->elements + byte_offset,
            leaf->count - byte_offset);
    memcpy(leaf->elements + byte_offset, str, byte_len);
    leaf->count += byte_len;
    leaf->summary = rope_summary_summarize(leaf->elements, leaf->count);
    t->root.count += byte_len;
    return;
  }

  /* Multi-level tree: walk to target leaf */
  struct { rope_st_internal* node; size_t child_idx; } path[ROPE_CURSOR_MAX_DEPTH];
  int depth = 0;
  size_t remaining = byte_offset;

  while (node->type == rope_st_NODE_INTERNAL) {
    rope_st_internal* n = (rope_st_internal*)node;
    size_t ci;
    for (ci = 0; ci < n->n_children - 1; ci++) {
      if (remaining < n->child_counts[ci]) break;
      remaining -= n->child_counts[ci];
    }

    path[depth].node = n;
    path[depth].child_idx = ci;
    depth++;

    /* Ensure child exclusive */
    n->children[ci] = _rope_transient_ensure_exclusive(n->children[ci]);
    node = n->children[ci];
  }

  /* Modify leaf in place */
  rope_st_leaf* leaf = (rope_st_leaf*)node;
  memmove(leaf->elements + remaining + byte_len,
          leaf->elements + remaining,
          leaf->count - remaining);
  memcpy(leaf->elements + remaining, str, byte_len);
  leaf->count += byte_len;
  leaf->summary = rope_summary_summarize(leaf->elements, leaf->count);

  /* Walk back up, recompute summaries and counts */
  for (int d = depth - 1; d >= 0; d--) {
    rope_st_internal* parent = path[d].node;
    size_t ci = path[d].child_idx;

    rope_st_node* child = parent->children[ci];
    parent->child_counts[ci] = (child->type == rope_st_NODE_LEAF)
        ? ((rope_st_leaf*)child)->count
        : ((rope_st_internal*)child)->count;

    parent->count = 0;
    parent->summary = rope_summary_identity();
    for (size_t i = 0; i < parent->n_children; i++) {
      parent->count += parent->child_counts[i];
      rope_summary cs = (parent->children[i]->type == rope_st_NODE_LEAF)
          ? ((rope_st_leaf*)parent->children[i])->summary
          : ((rope_st_internal*)parent->children[i])->summary;
      parent->summary = rope_summary_combine(parent->summary, cs);
    }
  }

  t->root.count += byte_len;
}

static inline void rope_transient_insert(rope_transient* t, size_t byte_offset,
                                          const uint8_t* str, size_t byte_len) {
  if (!t->valid) return;

  rope_st_set_handlers((rope_st_handlers){
    .identity  = rope_summary_identity,
    .combine   = rope_summary_combine,
    .summarize = rope_summary_summarize
  });

  size_t total = t->root.count;
  if (byte_offset > total) return;
  if (byte_len == 0) return;
  if (!utf8_validate(str, byte_len)) return;

  /* Validate codepoint boundary */
  if (byte_offset > 0 && byte_offset < total) {
    rope_st_get_result gr = rope_st_get(t->root, byte_offset);
    if (gr.found && utf8_is_continuation(gr.value)) return;
  }

  /* Empty tree: create from string */
  if (!t->root.node) {
    rope tmp = rope_from_str(str, byte_len);
    t->root = tmp.root;
    return;
  }

  /* Check if insert fits in target leaf (enables COW path) */
  rope_st_node* check_node = t->root.node;
  size_t check_rem = byte_offset;
  while (check_node->type == rope_st_NODE_INTERNAL) {
    rope_st_internal* n = (rope_st_internal*)check_node;
    size_t ci;
    for (ci = 0; ci < n->n_children - 1; ci++) {
      if (check_rem < n->child_counts[ci]) break;
      check_rem -= n->child_counts[ci];
    }
    check_node = n->children[ci];
  }

  if (((rope_st_leaf*)check_node)->count + byte_len <= ROPE_LEAF_BYTES) {
    /* COW in-place insert */
    _rope_transient_cow_insert(t, byte_offset, str, byte_len);
  } else {
    /* Fallback: persistent insert + root swap */
    rope tmp_rope = {.root = t->root};
    rope result = rope_insert(tmp_rope, byte_offset, str, byte_len);
    t->root = result.root;
  }
}

/* COW in-place delete: walk from root to target leaf, cloning shared nodes */
static inline void _rope_transient_cow_delete(rope_transient* t, size_t byte_offset,
                                               size_t byte_len) {
  /* Ensure root exclusive */
  t->root.node = _rope_transient_ensure_exclusive(t->root.node);
  rope_st_node* node = t->root.node;

  /* Single-leaf tree: modify directly */
  if (node->type == rope_st_NODE_LEAF) {
    rope_st_leaf* leaf = (rope_st_leaf*)node;
    memmove(leaf->elements + byte_offset,
            leaf->elements + byte_offset + byte_len,
            leaf->count - byte_offset - byte_len);
    leaf->count -= byte_len;
    leaf->summary = rope_summary_summarize(leaf->elements, leaf->count);
    t->root.count -= byte_len;
    return;
  }

  /* Multi-level tree: walk to target leaf */
  struct { rope_st_internal* node; size_t child_idx; } path[ROPE_CURSOR_MAX_DEPTH];
  int depth = 0;
  size_t remaining = byte_offset;

  while (node->type == rope_st_NODE_INTERNAL) {
    rope_st_internal* n = (rope_st_internal*)node;
    size_t ci;
    for (ci = 0; ci < n->n_children - 1; ci++) {
      if (remaining < n->child_counts[ci]) break;
      remaining -= n->child_counts[ci];
    }

    path[depth].node = n;
    path[depth].child_idx = ci;
    depth++;

    /* Ensure child exclusive */
    n->children[ci] = _rope_transient_ensure_exclusive(n->children[ci]);
    node = n->children[ci];
  }

  /* Modify leaf in place */
  rope_st_leaf* leaf = (rope_st_leaf*)node;
  memmove(leaf->elements + remaining,
          leaf->elements + remaining + byte_len,
          leaf->count - remaining - byte_len);
  leaf->count -= byte_len;
  leaf->summary = rope_summary_summarize(leaf->elements, leaf->count);

  /* Walk back up, recompute summaries and counts */
  for (int d = depth - 1; d >= 0; d--) {
    rope_st_internal* parent = path[d].node;
    size_t ci = path[d].child_idx;

    rope_st_node* child = parent->children[ci];
    parent->child_counts[ci] = (child->type == rope_st_NODE_LEAF)
        ? ((rope_st_leaf*)child)->count
        : ((rope_st_internal*)child)->count;

    parent->count = 0;
    parent->summary = rope_summary_identity();
    for (size_t i = 0; i < parent->n_children; i++) {
      parent->count += parent->child_counts[i];
      rope_summary cs = (parent->children[i]->type == rope_st_NODE_LEAF)
          ? ((rope_st_leaf*)parent->children[i])->summary
          : ((rope_st_internal*)parent->children[i])->summary;
      parent->summary = rope_summary_combine(parent->summary, cs);
    }
  }

  t->root.count -= byte_len;
}

static inline void rope_transient_delete(rope_transient* t, size_t byte_offset,
                                          size_t byte_len) {
  if (!t->valid) return;

  rope_st_set_handlers((rope_st_handlers){
    .identity  = rope_summary_identity,
    .combine   = rope_summary_combine,
    .summarize = rope_summary_summarize
  });

  size_t total = t->root.count;
  if (byte_offset > total) return;
  if (byte_len == 0) return;
  if (byte_offset + byte_len > total) byte_len = total - byte_offset;

  /* Validate codepoint boundaries */
  if (byte_offset > 0 && byte_offset < total) {
    rope_st_get_result gr = rope_st_get(t->root, byte_offset);
    if (gr.found && utf8_is_continuation(gr.value)) return;
  }
  size_t end = byte_offset + byte_len;
  if (end > 0 && end < total) {
    rope_st_get_result gr = rope_st_get(t->root, end);
    if (gr.found && utf8_is_continuation(gr.value)) return;
  }

  if (!t->root.node) return;

  /* Check if delete is within a single leaf */
  rope_st_node* check_node = t->root.node;
  size_t check_rem = byte_offset;
  while (check_node->type == rope_st_NODE_INTERNAL) {
    rope_st_internal* n = (rope_st_internal*)check_node;
    size_t ci;
    for (ci = 0; ci < n->n_children - 1; ci++) {
      if (check_rem < n->child_counts[ci]) break;
      check_rem -= n->child_counts[ci];
    }
    check_node = n->children[ci];
  }

  if (check_rem + byte_len <= ((rope_st_leaf*)check_node)->count) {
    /* COW in-place delete */
    _rope_transient_cow_delete(t, byte_offset, byte_len);
  } else {
    /* Fallback: persistent delete + root swap */
    rope tmp_rope = {.root = t->root};
    rope result = rope_delete(tmp_rope, byte_offset, byte_len);
    t->root = result.root;
  }
}

static inline void rope_transient_replace(rope_transient* t, size_t byte_offset,
                                            size_t delete_len, const uint8_t* str,
                                            size_t insert_len) {
  if (!t->valid) return;

  rope_st_set_handlers((rope_st_handlers){
    .identity  = rope_summary_identity,
    .combine   = rope_summary_combine,
    .summarize = rope_summary_summarize
  });

  /* Delegate to delete then insert — each has its own COW optimization */
  rope_transient_delete(t, byte_offset, delete_len);
  if (insert_len > 0 && t->valid) {
    rope_transient_insert(t, byte_offset, str, insert_len);
  }
}

/* --- Multi-dimensional seek (line + column) --- */

static inline rope_offset_result rope_seek(rope r, size_t line, size_t column,
                                            rope_dimension column_dimension) {
  /* Get line start byte offset */
  rope_offset_result line_res = rope_line_to_byte(r, line);
  if (!line_res.found) return (rope_offset_result){0, false};

  size_t line_start = line_res.value;

  if (column == 0) return (rope_offset_result){line_start, true};

  /* Find line end byte offset (start of next line, or total for last line) */
  size_t total = rope_byte_count(r);
  size_t line_end;
  rope_offset_result next_line_res = rope_line_to_byte(r, line + 1);
  if (next_line_res.found) {
    line_end = next_line_res.value;
  } else {
    line_end = total;
  }

  /* Empty line — column clamps to 0 */
  if (line_end == line_start) return (rope_offset_result){line_start, true};

  switch (column_dimension) {
    case ROPE_DIM_BYTES: {
      size_t max_col = line_end - line_start;
      size_t col = column < max_col ? column : max_col;
      return (rope_offset_result){line_start + col, true};
    }
    case ROPE_DIM_CHARS: {
      rope_cursor c = rope_cursor_new(r, line_start);
      if (!rope_cursor_advance_chars(&c, column)) {
        return (rope_offset_result){line_end, true};
      }
      size_t pos = rope_cursor_byte_offset(&c);
      if (pos > line_end) pos = line_end;
      return (rope_offset_result){pos, true};
    }
    case ROPE_DIM_GRAPHEMES: {
      rope_cursor c = rope_cursor_new(r, line_start);
      if (!rope_cursor_advance_graphemes(&c, column)) {
        return (rope_offset_result){line_end, true};
      }
      size_t pos = rope_cursor_byte_offset(&c);
      if (pos > line_end) pos = line_end;
      return (rope_offset_result){pos, true};
    }
  }
  return (rope_offset_result){0, false};
}

#endif /* ROPE_H */
