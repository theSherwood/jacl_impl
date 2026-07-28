/* syn_wire — AST subtree <-> flat byte buffer. See syn_wire.h. */
#include "syn_wire.h"
#include <stdlib.h>
#include <string.h>

#define SYNW_VERSION 1

/* ---- encode ---------------------------------------------------------------- */

typedef struct { unsigned char *buf; size_t len, cap; int err; } W;

static void w_need(W *w, size_t n) {
  if (w->err) return;
  if (w->len + n > w->cap) {
    size_t c = w->cap ? w->cap : 64;
    while (c < w->len + n) c *= 2;
    unsigned char *nb = (unsigned char *)realloc(w->buf, c);
    if (!nb) { w->err = 1; return; }
    w->buf = nb; w->cap = c;
  }
}
static void w_u8(W *w, unsigned v) { w_need(w, 1); if (w->err) return; w->buf[w->len++] = (unsigned char)v; }
static void w_u32(W *w, uint32_t v) { w_need(w, 4); if (w->err) return; memcpy(w->buf + w->len, &v, 4); w->len += 4; }
static void w_str(W *w, const char *s, uint32_t n) { w_u32(w, n); w_need(w, n); if (w->err) return; if (n) memcpy(w->buf + w->len, s, n); w->len += n; }

static void w_node(W *w, AstNode *n);

static void w_opt(W *w, AstNode *n) { w_u8(w, n ? 1 : 0); if (n) w_node(w, n); }

static void w_node(W *w, AstNode *n) {
  if (w->err) return;
  if (!n) { w->err = 1; return; }
  w_u8(w, (unsigned)n->type);
  w_u32(w, n->scope_mark);
  w_u8(w, (unsigned)((n->is_caret ? 1u : 0u) | (n->is_gensym ? 2u : 0u)));
  switch (n->type) {
    case AST_LIT_INT:   w_u32(w, (uint32_t)n->data.lit_int.value); break;
    case AST_LIT_FLOAT: { uint32_t b; memcpy(&b, &n->data.lit_float.value, 4); w_u32(w, b); } break;
    case AST_LIT_STRING: w_str(w, n->data.lit_string.value, n->data.lit_string.length); break;
    case AST_VAR_REF:    w_str(w, n->data.var_ref.name, n->data.var_ref.length); break;
    case AST_COMMAND:
      w_u8(w, n->data.command.head_id);
      w_opt(w, n->data.command.head);
      w_u32(w, n->data.command.arg_count);
      for (uint32_t i = 0; i < n->data.command.arg_count; i++) w_node(w, n->data.command.args[i]);
      break;
    case AST_BLOCK:
      w_u8(w, n->data.block.trailing_semi ? 1u : 0u);
      w_u32(w, n->data.block.count);
      for (uint32_t i = 0; i < n->data.block.count; i++) w_node(w, n->data.block.commands[i]);
      break;
    default: w->err = 1; break;   /* unsupported kind — grow the codec */
  }
}

unsigned char *synw_encode(AstNode *node, size_t *out_len) {
  W w = {0};
  w_u8(&w, SYNW_VERSION);
  w_node(&w, node);
  if (w.err) { free(w.buf); return NULL; }
  if (out_len) *out_len = w.len;
  return w.buf;
}

/* ---- decode ---------------------------------------------------------------- */

typedef struct { const unsigned char *buf; size_t len, pos; int err; arena_t *arena; } R;

static unsigned r_u8(R *r) { if (r->err || r->pos + 1 > r->len) { r->err = 1; return 0; } return r->buf[r->pos++]; }
static uint32_t r_u32(R *r) { if (r->err || r->pos + 4 > r->len) { r->err = 1; return 0; } uint32_t v; memcpy(&v, r->buf + r->pos, 4); r->pos += 4; return v; }
static const char *r_str(R *r, uint32_t *out_n) {
  uint32_t n = r_u32(r);
  if (r->err || r->pos + n > r->len) { r->err = 1; *out_n = 0; return NULL; }
  char *s = (char *)arena_alloc(r->arena, n + 1);
  if (n) memcpy(s, r->buf + r->pos, n);
  s[n] = 0; r->pos += n; *out_n = n; return s;
}

static AstNode *r_node(R *r);

static AstNode *r_opt(R *r) { return r_u8(r) ? r_node(r) : NULL; }

static AstNode *r_node(R *r) {
  if (r->err) return NULL;
  AstNode *n = ast_alloc(r->arena);
  n->type = (AstNodeType)r_u8(r);
  n->scope_mark = r_u32(r);
  unsigned fl = r_u8(r);
  n->is_caret = (fl & 1u) ? 1 : 0;
  n->is_gensym = (fl & 2u) ? 1 : 0;
  switch (n->type) {
    case AST_LIT_INT:   n->data.lit_int.value = (int32_t)r_u32(r); break;
    case AST_LIT_FLOAT: { uint32_t b = r_u32(r); memcpy(&n->data.lit_float.value, &b, 4); } break;
    case AST_LIT_STRING: n->data.lit_string.value = r_str(r, &n->data.lit_string.length); break;
    case AST_VAR_REF:    n->data.var_ref.name = r_str(r, &n->data.var_ref.length); break;
    case AST_COMMAND: {
      n->data.command.head_id = (uint8_t)r_u8(r);
      n->data.command.head = r_opt(r);
      uint32_t ac = r_u32(r);
      n->data.command.arg_count = ac;
      n->data.command.args = ac ? (AstNode **)arena_alloc(r->arena, sizeof(AstNode *) * ac) : NULL;
      for (uint32_t i = 0; i < ac; i++) n->data.command.args[i] = r_node(r);
      break;
    }
    case AST_BLOCK: {
      n->data.block.trailing_semi = r_u8(r) ? true : false;
      uint32_t c = r_u32(r);
      n->data.block.count = c;
      n->data.block.commands = c ? (AstNode **)arena_alloc(r->arena, sizeof(AstNode *) * c) : NULL;
      for (uint32_t i = 0; i < c; i++) n->data.block.commands[i] = r_node(r);
      break;
    }
    default: r->err = 1; break;
  }
  return r->err ? NULL : n;
}

AstNode *synw_decode(const unsigned char *buf, size_t len, arena_t *arena) {
  R r = { buf, len, 0, 0, arena };
  if (r_u8(&r) != SYNW_VERSION) return NULL;
  AstNode *n = r_node(&r);
  return r.err ? NULL : n;
}
