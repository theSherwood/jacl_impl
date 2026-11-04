/* syn_rt — syn_wire bytes <-> plain-data syntax value (jaclrt vector). See syn_rt.h.
 * The wire format MUST match syn_wire.c exactly. */
#include "syn_rt.h"
#include <stdlib.h>
#include <string.h>

/* Must match syn_wire.c. v3: a position handle per node, every expression node kind, and a
 * status byte leading the result wire. v2 widened AST_LIT_INT to 8 bytes (jacl #113). */
#define SYNW_VERSION 3
#define SYNW_RESULT_OK    0
#define SYNW_RESULT_ERROR 1

/* kind = AstNodeType ordinal (src/jacl.h): keep in sync. */
#define K_COMMAND           0
#define K_LIT_INT           1
#define K_LIT_FLOAT         2
#define K_LIT_STRING        3
#define K_VAR_REF           4
#define K_BLOCK             5
#define K_INTERP_STRING     6
#define K_QUOTE            10
#define K_SYNTAX_QUOTE     11
#define K_UNQUOTE          12
#define K_UNQUOTE_SPLICING 13
#define K_BREAK            14
#define K_CONTINUE         15
#define K_RETURN           16
#define K_SPREAD           19
#define K_SHELL_CMD        20

/* The plain-data header: [kind, scope_mark, flags, pos, …payload]. */
#define P 4   /* where the payload starts */

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
static uint64_t r_u64(R *r) {
  if (r->err || r->pos + 8 > r->len) { r->err = 1; return 0; }
  uint64_t v; memcpy(&v, r->buf + r->pos, 8); r->pos += 8; return v;
}
static JaclVal r_str(R *r) {
  uint32_t n = r_u32(r);
  if (r->err || r->pos + n > r->len) { r->err = 1; return JACL_NIL; }
  JaclVal s = jacl_str_new((const char *)(r->buf + r->pos), n);
  r->pos += n;
  return s;
}

static JaclVal r_node(R *r);

static JaclVal r_opt(R *r) { return r_u8(r) ? r_node(r) : JACL_NIL; }

/* A u32 count, then that many nodes, pushed onto `v`. */
static JaclVal r_nodes(R *r, JaclVal v) {
  uint32_t c = r_u32(r);
  for (uint32_t i = 0; i < c && !r->err; i++) v = jacl_vec_push(v, r_node(r));
  return v;
}

static JaclVal r_node(R *r) {
  if (r->err) return JACL_NIL;
  unsigned type = r_u8(r);
  uint32_t sm = r_u32(r);
  unsigned fl = r_u8(r);
  uint32_t pos = r_u32(r);
  JaclVal v = jacl_vec_empty();
  v = jacl_vec_push(v, jaclrt_i32((int32_t)type));
  v = jacl_vec_push(v, jaclrt_i32((int32_t)sm));
  v = jacl_vec_push(v, jaclrt_i32((int32_t)fl));
  v = jacl_vec_push(v, jaclrt_i32((int32_t)pos));
  switch (type) {
    /* The integer goes through jacl_i64_box, which canonicalizes — so the plain-data form
     * holds an inline i32 wherever one fits and a boxed i64 only where it must (jacl #107,
     * #113). Without that, a quoted 37 computed wide would not equal an inline 37. */
    case K_LIT_INT:
      v = jacl_vec_push(v, jacl_i64_box((int64_t)r_u64(r)));
      break;
    case K_LIT_FLOAT:
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u32(r)));   /* f32 bits, still 4 */
      break;
    case K_LIT_STRING:
    case K_VAR_REF:
      v = jacl_vec_push(v, r_str(r));
      break;
    case K_COMMAND:
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u8(r)));        /* head_id */
      v = jacl_vec_push(v, r_opt(r));                             /* head (opt) */
      v = r_nodes(r, v);                                          /* args */
      break;
    case K_BLOCK:
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u8(r)));        /* trailing_semi */
      v = r_nodes(r, v);                                          /* commands */
      break;
    case K_INTERP_STRING:
      v = r_nodes(r, v);                                          /* segments */
      break;
    case K_QUOTE:
    case K_SYNTAX_QUOTE:
    case K_UNQUOTE:
    case K_UNQUOTE_SPLICING:
    case K_SPREAD:
      v = jacl_vec_push(v, r_node(r));                            /* child */
      break;
    case K_BREAK:
    case K_RETURN:
      v = jacl_vec_push(v, r_opt(r));                             /* value (opt) */
      break;
    case K_CONTINUE:
      break;
    case K_SHELL_CMD:
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u8(r)));        /* head_id */
      v = jacl_vec_push(v, jaclrt_i32((int32_t)r_u8(r)));        /* background | stream << 1 */
      v = jacl_vec_push(v, r_node(r));                            /* head */
      v = r_nodes(r, v);                                          /* args */
      break;
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
static void w_u64(W *w, uint64_t v) { w_need(w, 8); if (w->err) return; memcpy(w->buf + w->len, &v, 8); w->len += 8; }
static void w_bytes(W *w, const char *s, uint32_t n) {
  w_u32(w, n);
  w_need(w, n);
  if (w->err) return;
  if (n) memcpy(w->buf + w->len, s, n);
  w->len += n;
}
static void w_str(W *w, JaclVal s) {
  if (!jaclrt_is_string(s)) { w->err = 1; return; }
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

/* A macro can hand back any value, so every field is checked before it is read: a syntax
 * value is a vector holding the header and its kind's payload. */
static int is_vec(JaclVal v) { return !jaclrt_is_error(v) && jaclrt_type_index(v) == 0x06; }

static void w_node(W *w, JaclVal v);

static void w_opt(W *w, JaclVal v) {
  if (jaclrt_is_nil(v)) { w_u8(w, 0); return; }
  w_u8(w, 1);
  w_node(w, v);
}

/* The elements from `from` to the end of `v`, as a u32 count then that many nodes. */
static void w_rest(W *w, JaclVal v, uint32_t from, uint32_t count) {
  uint32_t n = count > from ? count - from : 0;
  w_u32(w, n);
  for (uint32_t i = from; i < count && !w->err; i++) w_node(w, jacl_vec_get(v, i));
}

static void w_node(W *w, JaclVal v) {
  if (w->err) return;
  if (!is_vec(v)) { w->err = 1; return; }
  uint32_t count = jacl_vec_count(v);
  if (count < P) { w->err = 1; return; }
  unsigned type = (unsigned)jaclrt_as_i32(jacl_vec_get(v, 0));
  w_u8(w, type);
  w_u32(w, (uint32_t)jaclrt_as_i32(jacl_vec_get(v, 1)));
  w_u8(w, (unsigned)jaclrt_as_i32(jacl_vec_get(v, 2)));
  w_u32(w, (uint32_t)jaclrt_as_i32(jacl_vec_get(v, 3)));
  switch (type) {
    /* jacl_i64_unbox is the exact inverse of the jacl_i64_box the reader applies, so an
     * inline i32 and a boxed i64 both come back as the number they are (jacl #113). */
    case K_LIT_INT:
      if (count < P + 1) { w->err = 1; return; }
      w_u64(w, (uint64_t)jacl_i64_unbox(jacl_vec_get(v, P)));
      break;
    case K_LIT_FLOAT:
      if (count < P + 1) { w->err = 1; return; }
      w_u32(w, (uint32_t)jaclrt_as_i32(jacl_vec_get(v, P)));   /* f32 bits, still 4 */
      break;
    case K_LIT_STRING:
    case K_VAR_REF:
      if (count < P + 1) { w->err = 1; return; }
      w_str(w, jacl_vec_get(v, P));
      break;
    case K_COMMAND:
      if (count < P + 2) { w->err = 1; return; }
      w_u8(w, (unsigned)jaclrt_as_i32(jacl_vec_get(v, P)));      /* head_id */
      w_opt(w, jacl_vec_get(v, P + 1));                          /* head */
      w_rest(w, v, P + 2, count);                                /* args */
      break;
    case K_BLOCK:
      if (count < P + 1) { w->err = 1; return; }
      w_u8(w, (unsigned)jaclrt_as_i32(jacl_vec_get(v, P)));      /* trailing_semi */
      w_rest(w, v, P + 1, count);                                /* commands */
      break;
    case K_INTERP_STRING:
      w_rest(w, v, P, count);                                    /* segments */
      break;
    case K_QUOTE:
    case K_SYNTAX_QUOTE:
    case K_UNQUOTE:
    case K_UNQUOTE_SPLICING:
    case K_SPREAD:
      if (count < P + 1) { w->err = 1; return; }
      w_node(w, jacl_vec_get(v, P));                             /* child */
      break;
    case K_BREAK:
    case K_RETURN:
      if (count < P + 1) { w->err = 1; return; }
      w_opt(w, jacl_vec_get(v, P));                              /* value */
      break;
    case K_CONTINUE:
      break;
    case K_SHELL_CMD:
      if (count < P + 3) { w->err = 1; return; }
      w_u8(w, (unsigned)jaclrt_as_i32(jacl_vec_get(v, P)));      /* head_id */
      w_u8(w, (unsigned)jaclrt_as_i32(jacl_vec_get(v, P + 1)));  /* background | stream << 1 */
      w_node(w, jacl_vec_get(v, P + 2));                         /* head */
      w_rest(w, v, P + 3, count);                                /* args */
      break;
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

/* The result wire, status 1: the macro failed, and says why. */
static unsigned char *result_error(const char *msg, uint32_t n, size_t *out_len) {
  W w = {0};
  w_u8(&w, SYNW_VERSION);
  w_u8(&w, SYNW_RESULT_ERROR);
  w_bytes(&w, msg, n);
  if (w.err) { free(w.buf); return NULL; }
  if (out_len) *out_len = w.len;
  return w.buf;
}

unsigned char *synrt_encode_result(JaclVal v, size_t *out_len) {
  if (jaclrt_is_error(v)) {
    /* An error value's payload is its message: `[error "msg"]`, or whatever the failing
     * operation carried. */
    JaclVal payload = jacl_error_val(v);
    JaclVal msg = jaclrt_is_nil(payload) ? JACL_NIL : jacl_to_string(payload);
    if (!jaclrt_is_error(msg) && jaclrt_is_string(msg)) {
      uint32_t n = jacl_str_len(msg);
      char *tmp = (char *)malloc(n + 1);
      if (tmp) {
        jacl_str_bytes(msg, tmp, n + 1);
        unsigned char *out = result_error(tmp, n, out_len);
        free(tmp);
        return out;
      }
    }
    static const char failed[] = "the macro returned an error";
    return result_error(failed, sizeof failed - 1, out_len);
  }
  W w = {0};
  w_u8(&w, SYNW_VERSION);
  w_u8(&w, SYNW_RESULT_OK);
  w_node(&w, v);
  if (!w.err) {
    if (out_len) *out_len = w.len;
    return w.buf;
  }
  free(w.buf);
  static const char not_syntax[] = "the macro returned a value that is not syntax";
  return result_error(not_syntax, sizeof not_syntax - 1, out_len);
}

/* The var-ref `prefix__n`, flagged as a gensym, with position handle 0: the host names it for
 * the expansion when the result comes back (syn_wire.c), so `n` only has to be unique within
 * one run. */
JaclVal synrt_gensym_node(JaclVal prefix, uint32_t n) {
  char name[80];
  uint32_t plen = 0;
  if (jaclrt_is_string(prefix)) {
    plen = jacl_str_len(prefix);
    if (plen > 64) plen = 64;
    jacl_str_bytes(prefix, name, 65);   /* prefix bytes land at name[0..plen) */
  }
  if (plen == 0) { name[0] = 'g'; plen = 1; }
  uint32_t nl = plen;
  name[nl++] = '_'; name[nl++] = '_';
  char d[16]; int dc = 0;
  if (n == 0) d[dc++] = '0';
  while (n > 0) { d[dc++] = (char)('0' + (n % 10)); n /= 10; }
  for (int i = dc - 1; i >= 0; i--) name[nl++] = d[i];
  JaclVal v = jacl_vec_empty();
  v = jacl_vec_push(v, jaclrt_i32(K_VAR_REF));
  v = jacl_vec_push(v, jaclrt_i32(0));   /* scope_mark (the name is unique, hence hygienic) */
  v = jacl_vec_push(v, jaclrt_i32(2));   /* flags: is_gensym */
  v = jacl_vec_push(v, jaclrt_i32(0));   /* position handle: made by the macro */
  v = jacl_vec_push(v, jacl_str_new(name, nl));
  return v;
}
