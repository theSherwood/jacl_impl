/* syn_rt — syn_wire bytes <-> plain-data syntax value (jaclrt vector). See syn_rt.h.
 * The wire format MUST match syn_wire.c exactly. */
#include "syn_rt.h"
#include <stdlib.h>
#include <string.h>

#define SYNW_VERSION 1
/* kind = AstNodeType ordinal (src/jacl.h): keep in sync. */
#define K_COMMAND    0
#define K_LIT_INT    1
#define K_LIT_FLOAT  2
#define K_LIT_STRING 3
#define K_VAR_REF    4
#define K_BLOCK      5

/* ---- byte reader ----------------------------------------------------------- */

typedef struct { const unsigned char *buf; size_t len, pos; int err; } R;

static unsigned r_u8(R *r) {
  if (r->err || r->pos + 1 > r->len) { r->err = 1; return 0; }
  return r->buf[r->pos++];
}
static uint32_t r_u32(R *r) {
  if (r->err || r->pos + 4 > r->len) { r->err = 1; return 0; }
  uint32_t v; memcpy(&v, r->buf + r->pos, 4); r->pos += 4; return v;
}
static JaclVal r_str(R *r) {
  uint32_t n = r_u32(r);
  if (r->err || r->pos + n > r->len) { r->err = 1; return JACL_NIL; }
  JaclVal s = jacl_str_new((const char *)(r->buf + r->pos), n);
  r->pos += n;
  return s;
}

static JaclVal r_node(R *r) {
  if (r->err) return JACL_NIL;
  unsigned type = r_u8(r);
  uint32_t sm = r_u32(r);
  unsigned fl = r_u8(r);
  JaclVal v = jacl_vec_empty();
  v = jacl_vec_push(v, jaclrt_i32((int32_t)type));
  v = jacl_vec_push(v, jaclrt_i32((int32_t)sm));
  v = jacl_vec_push(v, jaclrt_i32((int32_t)fl));
  switch (type) {
    case K_LIT_INT:
    case K_LIT_FLOAT:
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u32(r)));
      break;
    case K_LIT_STRING:
    case K_VAR_REF:
      v = jacl_vec_push(v, r_str(r));
      break;
    case K_COMMAND: {
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u8(r)));        /* head_id */
      v = jacl_vec_push(v, r_u8(r) ? r_node(r) : JACL_NIL);      /* head (opt) */
      uint32_t ac = r_u32(r);
      for (uint32_t i = 0; i < ac; i++) v = jacl_vec_push(v, r_node(r));
      break;
    }
    case K_BLOCK: {
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u8(r)));        /* trailing_semi */
      uint32_t c = r_u32(r);
      for (uint32_t i = 0; i < c; i++) v = jacl_vec_push(v, r_node(r));
      break;
    }
    default: r->err = 1; break;
  }
  return r->err ? JACL_NIL : v;
}

JaclVal synrt_decode(const unsigned char *buf, size_t len) {
  R r = { buf, len, 0, 0 };
  if (r_u8(&r) != SYNW_VERSION) return JACL_NIL;
  JaclVal v = r_node(&r);
  return r.err ? JACL_NIL : v;
}

int synrt_wire_version(void) { return SYNW_VERSION; }

/* Decode ONE node from `buf` starting at *pos (no version byte); advances *pos past the
 * node. For reading a sequence of N syntax values from one buffer (multi-arity macro
 * args, whose wire is: version byte, then N nodes back-to-back). JACL_NIL on error. */
JaclVal synrt_decode_node(const unsigned char *buf, size_t len, size_t *pos) {
  R r = { buf, len, *pos, 0 };
  JaclVal v = r_node(&r);
  *pos = r.pos;
  return r.err ? JACL_NIL : v;
}

/* ---- byte writer ----------------------------------------------------------- */

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
static void w_str(W *w, JaclVal s) {
  uint32_t n = jacl_str_len(s);
  w_u32(w, n);
  w_need(w, n);
  if (w->err) return;
  if (n) {
    char *tmp = (char *)malloc(n + 1);
    if (!tmp) { w->err = 1; return; }
    jacl_str_bytes(s, tmp, n + 1);
    memcpy(w->buf + w->len, tmp, n);
    free(tmp);
  }
  w->len += n;
}

static void w_node(W *w, JaclVal v);

static void w_node(W *w, JaclVal v) {
  if (w->err) return;
  uint32_t count = jacl_vec_count(v);
  if (count < 3) { w->err = 1; return; }
  unsigned type = (unsigned)jaclrt_as_i32(jacl_vec_get(v, 0));
  uint32_t sm   = (uint32_t)jaclrt_as_i32(jacl_vec_get(v, 1));
  unsigned fl   = (unsigned)jaclrt_as_i32(jacl_vec_get(v, 2));
  w_u8(w, type);
  w_u32(w, sm);
  w_u8(w, fl);
  switch (type) {
    case K_LIT_INT:
    case K_LIT_FLOAT:
      w_u32(w, (uint32_t)jaclrt_as_i32(jacl_vec_get(v, 3)));
      break;
    case K_LIT_STRING:
    case K_VAR_REF:
      w_str(w, jacl_vec_get(v, 3));
      break;
    case K_COMMAND: {
      w_u8(w, (unsigned)jaclrt_as_i32(jacl_vec_get(v, 3)));      /* head_id */
      JaclVal head = jacl_vec_get(v, 4);
      if (jaclrt_is_nil(head)) { w_u8(w, 0); }
      else { w_u8(w, 1); w_node(w, head); }
      uint32_t ac = count > 5 ? count - 5 : 0;
      w_u32(w, ac);
      for (uint32_t i = 5; i < count; i++) w_node(w, jacl_vec_get(v, i));
      break;
    }
    case K_BLOCK: {
      w_u8(w, (unsigned)jaclrt_as_i32(jacl_vec_get(v, 3)));      /* trailing_semi */
      uint32_t c = count > 4 ? count - 4 : 0;
      w_u32(w, c);
      for (uint32_t i = 4; i < count; i++) w_node(w, jacl_vec_get(v, i));
      break;
    }
    default: w->err = 1; break;
  }
}

unsigned char *synrt_encode(JaclVal syn, size_t *out_len) {
  W w = {0};
  w_u8(&w, SYNW_VERSION);
  w_node(&w, syn);
  if (w.err) { free(w.buf); return NULL; }
  if (out_len) *out_len = w.len;
  return w.buf;
}
