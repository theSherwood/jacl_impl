/* syn_rt — syn_wire bytes <-> plain-data syntax value (jaclrt vector). See syn_rt.h.
 * The wire format MUST match syn_wire.c exactly. */
#include "syn_rt.h"
#include <stdlib.h>
#include <string.h>

/* Must match syn_wire.c. v4: the argument wire's header (the call's position and a line/col
 * per position handle) and the node an error result is about. v3: a position handle per node,
 * every expression node kind, and a status byte leading the result wire. v2 widened
 * AST_LIT_INT to 8 bytes (jacl #113). */
#define SYNW_VERSION 4
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

/* ---- the argument wire's header -------------------------------------------- */

/* Defined by the glue that includes this file: make sure the argument wire has been read, so
 * the header below is loaded (the native glue reads stdin on first use). */
void synrt_load_args(void);

static uint32_t g_call_line, g_call_col;    /* the macro call's position */
static const unsigned char *g_pos_table;    /* a u32 line and column per position handle */
static uint32_t g_pos_count;

/* The pending `syntax-error`: its message, and the handle of the node it is about. */
static char *g_err_msg;
static uint32_t g_err_len, g_err_handle;
static int g_err_set;

static void clear_syntax_error(void) {
  free(g_err_msg);
  g_err_msg = NULL;
  g_err_len = g_err_handle = 0;
  g_err_set = 0;
}

void synrt_read_header(const unsigned char *buf, size_t len, size_t *pos) {
  R r = { buf, len, *pos, 0 };
  uint32_t line = r_u32(&r), col = r_u32(&r), n = r_u32(&r);
  clear_syntax_error();
  if (r.err || n > (len - r.pos) / 8) {   /* malformed: no table, and nothing left to read */
    g_call_line = g_call_col = g_pos_count = 0;
    g_pos_table = NULL;
    *pos = len;
    return;
  }
  g_call_line = line;
  g_call_col = col;
  g_pos_table = buf + r.pos;
  g_pos_count = n;
  *pos = r.pos + (size_t)n * 8;
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

/* The result wire, status 1: the macro failed, says why, and names the node it is about
 * (`handle`; 0 for the call). */
static unsigned char *result_error(const char *msg, uint32_t n, uint32_t handle, size_t *out_len) {
  W w = {0};
  w_u8(&w, SYNW_VERSION);
  w_u8(&w, SYNW_RESULT_ERROR);
  w_bytes(&w, msg, n);
  w_u32(&w, handle);
  if (w.err) { free(w.buf); return NULL; }
  if (out_len) *out_len = w.len;
  return w.buf;
}

unsigned char *synrt_encode_result(JaclVal v, size_t *out_len) {
  /* A `syntax-error` fails the macro wherever it was called — in a loop body, whose value is
   * dropped, or caught — as it halts compilation; whatever the body went on to return. */
  if (g_err_set) {
    static const char failed[] = "syntax error";
    unsigned char *out = g_err_msg ? result_error(g_err_msg, g_err_len, g_err_handle, out_len)
                                   : result_error(failed, sizeof failed - 1, g_err_handle, out_len);
    clear_syntax_error();
    return out;
  }
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
        unsigned char *out = result_error(tmp, n, 0, out_len);
        free(tmp);
        return out;
      }
    }
    static const char failed[] = "the macro returned an error";
    return result_error(failed, sizeof failed - 1, 0, out_len);
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
  return result_error(not_syntax, sizeof not_syntax - 1, 0, out_len);
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

/* ---- the syntax builtins ----------------------------------------------------
 *
 * What a staged macro body calls to look at its arguments and build syntax: codegen lowers
 * `syntax-kind`, `syntax-datum`, `syntax-head`, `syntax-args`, `syntax-commands`, `syntax-pos`,
 * `syntax-str`, `make-syntax` and `syntax-error` to these (codegen.c, staged bodies only). Each
 * takes JaclVals and returns one; a misuse returns an error value, which fails the macro with
 * that message. */

#define SYN_FAIL(lit) jacl_error_new(jacl_str_new(lit, (uint32_t)(sizeof(lit) - 1)))

static uint32_t cstr_len(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static JaclVal cstr(const char *s) { return jacl_str_new(s, cstr_len(s)); }

static int is_syntax(JaclVal v) { return is_vec(v) && jacl_vec_count(v) >= P; }
static unsigned kind_of(JaclVal v) { return (unsigned)jaclrt_as_i32(jacl_vec_get(v, 0)); }
static unsigned flags_of(JaclVal v) { return (unsigned)jaclrt_as_i32(jacl_vec_get(v, 2)); }

static const char *kind_name(unsigned k) {
  switch (k) {
    case K_COMMAND:          return "command";
    case K_LIT_INT:          return "lit-int";
    case K_LIT_FLOAT:        return "lit-float";
    case K_LIT_STRING:       return "lit-string";
    case K_VAR_REF:          return "var-ref";
    case K_BLOCK:            return "block";
    case K_INTERP_STRING:    return "interp-string";
    case K_QUOTE:            return "quote";
    case K_SYNTAX_QUOTE:     return "syntax-quote";
    case K_UNQUOTE:          return "unquote";
    case K_UNQUOTE_SPLICING: return "unquote-splicing";
    case K_BREAK:            return "break";
    case K_CONTINUE:         return "continue";
    case K_RETURN:           return "return";
    case K_SPREAD:           return "spread";
    case K_SHELL_CMD:        return "shell-cmd";
    default:                 return NULL;
  }
}

JaclVal synrt_syntax_kind(JaclVal s) {
  const char *n = is_syntax(s) ? kind_name(kind_of(s)) : NULL;
  return n ? cstr(n) : SYN_FAIL("syntax-kind: not a syntax value");
}

/* A literal's value, or a var-ref's name. */
JaclVal synrt_syntax_datum(JaclVal s) {
  if (is_syntax(s) && jacl_vec_count(s) > P) {
    switch (kind_of(s)) {
      case K_LIT_INT: case K_LIT_STRING: case K_VAR_REF:
        return jacl_vec_get(s, P);
      case K_LIT_FLOAT:   /* the f32's bits are held as an i32 */
        return JACL_TAG_F32 | (uint64_t)(uint32_t)jaclrt_as_i32(jacl_vec_get(s, P));
      default: break;
    }
  }
  return SYN_FAIL("syntax-datum: not a literal or a var-ref");
}

/* `s` from element `from` to its end, as a vec. */
static JaclVal tail(JaclVal s, uint32_t from) {
  uint32_t n = jacl_vec_count(s);
  if (from > n) from = n;
  return jacl_vec_slice(s, jaclrt_i32((int32_t)from), jaclrt_i32((int32_t)n));
}

JaclVal synrt_syntax_head(JaclVal s) {
  if (is_syntax(s)) {
    uint32_t n = jacl_vec_count(s);
    if (kind_of(s) == K_COMMAND && n >= P + 2) return jacl_vec_get(s, P + 1);
    if (kind_of(s) == K_SHELL_CMD && n >= P + 3) return jacl_vec_get(s, P + 2);
  }
  return SYN_FAIL("syntax-head: not a command");
}

/* A command's (or shell command's) arguments, or an interpolated string's segments. */
JaclVal synrt_syntax_args(JaclVal s) {
  if (is_syntax(s)) {
    switch (kind_of(s)) {
      case K_COMMAND:       return tail(s, P + 2);
      case K_SHELL_CMD:     return tail(s, P + 3);
      case K_INTERP_STRING: return tail(s, P);
      default: break;
    }
  }
  return SYN_FAIL("syntax-args: not a command or an interpolated string");
}

JaclVal synrt_syntax_commands(JaclVal s) {
  if (is_syntax(s) && kind_of(s) == K_BLOCK) return tail(s, P + 1);
  return SYN_FAIL("syntax-commands: not a block");
}

/* `[map line N col M]`: where `s` is in the source — the argument's own position, or the
 * macro call's for a node the macro made. */
JaclVal synrt_syntax_pos(JaclVal s) {
  if (!is_syntax(s)) return SYN_FAIL("syntax-pos: not a syntax value");
  synrt_load_args();
  uint32_t h = (uint32_t)jaclrt_as_i32(jacl_vec_get(s, 3));
  uint32_t line = g_call_line, col = g_call_col;
  if (h >= 1 && h <= g_pos_count && g_pos_table) {
    memcpy(&line, g_pos_table + (size_t)(h - 1) * 8, 4);
    memcpy(&col, g_pos_table + (size_t)(h - 1) * 8 + 4, 4);
  }
  JaclVal m = jacl_map_empty();
  m = jacl_map_set(m, cstr("line"), jaclrt_i32((int32_t)line));
  m = jacl_map_set(m, cstr("col"), jaclrt_i32((int32_t)col));
  return m;
}

/* `syntax-error MSG` / `syntax-error MSG NODE`: fail the macro with MSG, reported at NODE's
 * position (or the call's). The message and node are remembered for the result wire, which
 * reports them whatever the body returns; the error value it returns ends the body early where
 * error propagation reaches. */
static JaclVal syntax_error(JaclVal msg, JaclVal node) {
  JaclVal m = jaclrt_is_string(msg) ? msg : jacl_to_string(msg);
  if (jaclrt_is_error(m) || !jaclrt_is_string(m)) m = cstr("syntax error");
  clear_syntax_error();
  uint32_t n = jacl_str_len(m);
  g_err_msg = (char *)malloc(n + 1);
  if (g_err_msg) { jacl_str_bytes(m, g_err_msg, n + 1); g_err_len = n; }
  g_err_handle = is_syntax(node) ? (uint32_t)jaclrt_as_i32(jacl_vec_get(node, 3)) : 0;
  g_err_set = 1;
  return jacl_error_new(m);
}
JaclVal synrt_syntax_error1(JaclVal msg) { return syntax_error(msg, JACL_NIL); }
JaclVal synrt_syntax_error2(JaclVal msg, JaclVal node) { return syntax_error(msg, node); }

/* ---- make-syntax ------------------------------------------------------------ */

static int str_is(JaclVal s, const char *lit) {
  uint32_t n = cstr_len(lit);
  char buf[24];
  if (!jaclrt_is_string(s) || jacl_str_len(s) != n || n >= sizeof buf) return 0;
  jacl_str_bytes(s, buf, sizeof buf);
  return memcmp(buf, lit, n) == 0;
}

/* A node the macro made: scope mark 0 (marks are not stamped per expansion yet; see codegen.c's
 * compile_synquote) and position handle 0, so it takes the call's position. */
static JaclVal made(unsigned kind, unsigned flags) {
  JaclVal v = jacl_vec_empty();
  v = jacl_vec_push(v, jaclrt_i32((int32_t)kind));
  v = jacl_vec_push(v, jaclrt_i32(0));
  v = jacl_vec_push(v, jaclrt_i32((int32_t)flags));
  v = jacl_vec_push(v, jaclrt_i32(0));
  return v;
}

/* Whether `v` is a vec of syntax values. */
static int all_syntax(JaclVal v) {
  if (!is_vec(v)) return 0;
  uint32_t n = jacl_vec_count(v);
  for (uint32_t i = 0; i < n; i++)
    if (!is_syntax(jacl_vec_get(v, i))) return 0;
  return 1;
}

static JaclVal make_syntax(JaclVal kind, JaclVal a, JaclVal b, int argc) {
  if (str_is(kind, "command")) {
    if (argc != 3) return SYN_FAIL("make-syntax \"command\" takes a head and a vec of arguments");
    if (!is_syntax(a)) return SYN_FAIL("make-syntax \"command\": the head is not a syntax value");
    if (!all_syntax(b)) return SYN_FAIL("make-syntax \"command\": the arguments are not a vec of syntax values");
    JaclVal v = made(K_COMMAND, 0);
    v = jacl_vec_push(v, jaclrt_i32(0));   /* head id: the host gives it the parser's */
    v = jacl_vec_push(v, a);
    return jacl_vec_concat(v, b);
  }
  if (argc != 2) return SYN_FAIL("make-syntax: only \"command\" takes two values after the kind");
  if (str_is(kind, "block")) {
    if (!all_syntax(a)) return SYN_FAIL("make-syntax \"block\" takes a vec of syntax values");
    JaclVal v = made(K_BLOCK, 0);
    v = jacl_vec_push(v, jaclrt_i32(0));   /* trailing_semi */
    return jacl_vec_concat(v, a);
  }
  if (str_is(kind, "interp-string")) {
    if (!all_syntax(a)) return SYN_FAIL("make-syntax \"interp-string\" takes a vec of syntax values");
    return jacl_vec_concat(made(K_INTERP_STRING, 0), a);
  }
  /* The value's tag, read here rather than through builtins.c's inline helpers, since some
   * builds compile this file apart from the runtime: 0x02 i32, 0x03 f32, 0x0E a 64-bit int. */
  uint32_t t = jaclrt_is_error(a) ? 0xFF : jaclrt_type_index(a);
  if (str_is(kind, "lit-int")) {
    if (t != 0x02 && t != 0x0E) return SYN_FAIL("make-syntax \"lit-int\" takes an integer");
    return jacl_vec_push(made(K_LIT_INT, 0), jacl_i64_box(jacl_i64_unbox(a)));
  }
  if (str_is(kind, "lit-float")) {
    union { float f; uint32_t u; } c;
    if (t == 0x03) c.u = (uint32_t)jaclrt_as_i32(a);          /* an f32's bits */
    else if (t == 0x02) c.f = (float)jaclrt_as_i32(a);
    else return SYN_FAIL("make-syntax \"lit-float\" takes an f32 or an i32");
    return jacl_vec_push(made(K_LIT_FLOAT, 0), jaclrt_i32((int32_t)c.u));
  }
  int caret = str_is(kind, "lit-string-caret");
  int vref = str_is(kind, "var-ref");
  if (caret || vref || str_is(kind, "lit-string")) {
    if (!jaclrt_is_string(a)) return SYN_FAIL("make-syntax: \"lit-string\", \"lit-string-caret\" and \"var-ref\" take a string");
    return jacl_vec_push(made(vref ? K_VAR_REF : K_LIT_STRING, caret ? 1 : 0), a);
  }
  if (jaclrt_is_string(kind))
    return jacl_error_new(jacl_str_concat(cstr("make-syntax: unknown kind "), kind));
  return SYN_FAIL("make-syntax: the kind is not a string");
}
JaclVal synrt_make_syntax2(JaclVal kind, JaclVal a) { return make_syntax(kind, a, JACL_NIL, 2); }
JaclVal synrt_make_syntax3(JaclVal kind, JaclVal a, JaclVal b) { return make_syntax(kind, a, b, 3); }

/* ---- syntax-str ------------------------------------------------------------
 *
 * The same text src/ast.c's ast_pretty_print gives for the node once it is back in the
 * compiler: the rules below mirror ast__pp_node case for case (run_rt_codec_test.sh checks
 * the two agree). */

typedef struct { char *b; uint32_t n, cap; int err; } PB;

static void pb_put(PB *p, const char *s, uint32_t n) {
  if (p->err) return;
  if (p->n + n > p->cap) {
    uint32_t c = p->cap ? p->cap : 64;
    while (c < p->n + n) c *= 2;
    char *nb = (char *)realloc(p->b, c);
    if (!nb) { p->err = 1; return; }
    p->b = nb; p->cap = c;
  }
  memcpy(p->b + p->n, s, n);
  p->n += n;
}
static void pb_c(PB *p, char c) { pb_put(p, &c, 1); }
static void pb_s(PB *p, const char *s) { pb_put(p, s, cstr_len(s)); }

/* A string value's bytes (malloc'd, NUL-terminated); NULL if it is not a string. */
static char *str_bytes(JaclVal s, uint32_t *n) {
  if (!jaclrt_is_string(s)) return NULL;
  *n = jacl_str_len(s);
  char *b = (char *)malloc(*n + 1);
  if (b) jacl_str_bytes(s, b, *n + 1);
  return b;
}

/* ast__buf_escaped_char */
static void pb_escaped(PB *p, char c) {
  switch (c) {
    case '"':  pb_s(p, "\\\""); break;
    case '\\': pb_s(p, "\\\\"); break;
    case '\n': pb_s(p, "\\n");  break;
    case '\t': pb_s(p, "\\t");  break;
    case '\r': pb_s(p, "\\r");  break;
    case '$':  pb_s(p, "\\$");  break;
    default:   pb_c(p, c);      break;
  }
}

/* ast__needs_quoting */
static int needs_quoting(const char *s, uint32_t len) {
  if (len == 0) return 1;
  if (s[0] >= '0' && s[0] <= '9') return 1;
  for (uint32_t i = 0; i < len; i++) {
    char c = s[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '$' || c == '[' ||
        c == ']' || c == '{' || c == '}' || c == '(' || c == ')' || c == '#' || c == ';' ||
        c == '\\' || c < 32)
      return 1;
  }
  return 0;
}

/* A string-valued node's text, raw (`escape` 0) or escaped as inside quotes. */
static void pb_text(PB *p, JaclVal s, int escape) {
  uint32_t n = 0;
  char *b = str_bytes(s, &n);
  if (!b) { p->err = 1; return; }
  if (escape) for (uint32_t i = 0; i < n; i++) pb_escaped(p, b[i]);
  else pb_put(p, b, n);
  free(b);
}

static void pb_i64(PB *p, int64_t x) {
  char t[24]; int j = 0;
  uint64_t u = x < 0 ? (uint64_t)0 - (uint64_t)x : (uint64_t)x;
  do { t[j++] = (char)('0' + u % 10); u /= 10; } while (u);
  if (x < 0) pb_c(p, '-');
  while (j) pb_c(p, t[--j]);
}

/* printf's `%g` (six significant digits) for a float literal, then the `.0` ast__pp_node adds
 * when there is neither a point nor an exponent, so it re-reads as a float. */
static void pb_float(PB *p, float f) {
  char out[40]; int n = 0;
  double v = (double)f;
  if (v != v) { pb_s(p, "nan.0"); return; }
  if (v < 0 || (v == 0 && 1.0 / v < 0)) { out[n++] = '-'; v = -v; }
  if (v > 0 && v * 0.5 == v) { out[n++] = 'i'; out[n++] = 'n'; out[n++] = 'f'; }
  else if (v == 0) out[n++] = '0';
  else {
    int x = 0;
    double m = v;
    while (m >= 10.0) { m /= 10.0; x++; }
    while (m < 1.0) { m *= 10.0; x--; }
    uint64_t d = (uint64_t)(m * 100000.0 + 0.5);
    if (d >= 1000000u) { d /= 10; x++; }   /* rounding carried a decade */
    char dig[6];
    for (int i = 5; i >= 0; i--) { dig[i] = (char)('0' + d % 10); d /= 10; }
    int last = 5;
    if (x < -4 || x >= 6) {                 /* d.ddddde±XX */
      while (last >= 1 && dig[last] == '0') last--;
      out[n++] = dig[0];
      if (last >= 1) { out[n++] = '.'; for (int i = 1; i <= last; i++) out[n++] = dig[i]; }
      out[n++] = 'e';
      out[n++] = x < 0 ? '-' : '+';
      int ax = x < 0 ? -x : x;
      if (ax >= 100) out[n++] = (char)('0' + ax / 100);
      out[n++] = (char)('0' + (ax / 10) % 10);
      out[n++] = (char)('0' + ax % 10);
    } else if (x >= 0) {                    /* ddd.ddd */
      for (int i = 0; i <= x; i++) out[n++] = dig[i];
      while (last > x && dig[last] == '0') last--;
      if (last > x) { out[n++] = '.'; for (int i = x + 1; i <= last; i++) out[n++] = dig[i]; }
    } else {                                /* 0.000ddd */
      out[n++] = '0'; out[n++] = '.';
      for (int i = 0; i < -x - 1; i++) out[n++] = '0';
      while (last >= 1 && dig[last] == '0') last--;
      for (int i = 0; i <= last; i++) out[n++] = dig[i];
    }
  }
  pb_put(p, out, (uint32_t)n);
  for (int i = 0; i < n; i++)
    if (out[i] == '.' || out[i] == 'e' || out[i] == 'E') return;
  pb_s(p, ".0");
}

static void pp(PB *p, JaclVal v);

/* `[head a b …]`'s head and arguments, space-separated. */
static void pp_head_args(PB *p, JaclVal head, JaclVal v, uint32_t from) {
  if (!jaclrt_is_nil(head)) pp(p, head);
  uint32_t n = jacl_vec_count(v);
  for (uint32_t i = from; i < n; i++) { pb_c(p, ' '); pp(p, jacl_vec_get(v, i)); }
}

/* A node whose text is a name: a string literal's value, or a var-ref's name. */
static int is_named(JaclVal v, unsigned kind) {
  return is_syntax(v) && kind_of(v) == kind && jacl_vec_count(v) > P;
}

static void pp_command(PB *p, JaclVal v) {
  uint32_t n = jacl_vec_count(v);
  if (n < P + 2) { p->err = 1; return; }
  JaclVal head = jacl_vec_get(v, P + 1);
  uint32_t argc = n - (P + 2);
  #define ARG(i) jacl_vec_get(v, P + 2 + (i))
  /* `[. $x field]` prints as `$x->field` */
  if (is_named(head, K_LIT_STRING) && str_is(jacl_vec_get(head, P), ".") && argc == 2) {
    pp(p, ARG(0));
    pb_s(p, "->");
    JaclVal f = ARG(1);
    if (is_named(f, K_LIT_STRING) || is_named(f, K_VAR_REF)) pb_text(p, jacl_vec_get(f, P), 0);
    else pp(p, f);
    return;
  }
  /* `proc` prints as the statement form: a lambda as `[\ …]`, a named proc as
   * `proc NAME {PARAMS} BODY` */
  if (is_named(head, K_LIT_STRING) && str_is(jacl_vec_get(head, P), "proc") && argc >= 2) {
    JaclVal name = ARG(0);
    if (is_named(name, K_LIT_STRING) && jacl_str_len(jacl_vec_get(name, P)) == 0 && argc == 3) {
      JaclVal body = ARG(2);
      pb_s(p, "[\\ ");
      if (is_syntax(body) && kind_of(body) == K_BLOCK && jacl_vec_count(body) == P + 2) {
        JaclVal cmd = jacl_vec_get(body, P + 1);
        if (is_syntax(cmd) && kind_of(cmd) == K_COMMAND && jacl_vec_count(cmd) >= P + 2)
          pp_head_args(p, jacl_vec_get(cmd, P + 1), cmd, P + 2);
        else pp(p, cmd);
      } else {
        pp(p, body);
      }
      pb_c(p, ']');
      return;
    }
    pb_s(p, "proc");
    uint32_t params_idx = argc - 2;
    for (uint32_t i = 0; i < params_idx; i++) { pb_c(p, ' '); pp(p, ARG(i)); }
    JaclVal params = ARG(params_idx);
    pb_s(p, " {");
    if (is_syntax(params) && kind_of(params) == K_COMMAND && jacl_vec_count(params) >= P + 2) {
      JaclVal ph = jacl_vec_get(params, P + 1);
      if ((is_named(ph, K_LIT_STRING) || is_named(ph, K_VAR_REF)) &&
          jacl_str_len(jacl_vec_get(ph, P)) > 0)
        pp_head_args(p, ph, params, P + 2);
    }
    pb_c(p, '}');
    pb_c(p, ' ');
    pp(p, ARG(argc - 1));
    return;
  }
  #undef ARG
  pb_c(p, '[');
  pp_head_args(p, head, v, P + 2);
  pb_c(p, ']');
}

static void pp(PB *p, JaclVal v) {
  if (p->err) return;
  if (!is_syntax(v)) { p->err = 1; return; }
  uint32_t n = jacl_vec_count(v);
  unsigned k = kind_of(v);
  JaclVal x = n > P ? jacl_vec_get(v, P) : JACL_NIL;   /* the first payload element */
  switch (k) {
    case K_LIT_INT: pb_i64(p, jacl_i64_unbox(x)); break;
    case K_LIT_FLOAT: {
      union { uint32_t u; float f; } c;
      c.u = (uint32_t)jaclrt_as_i32(x);
      pb_float(p, c.f);
      break;
    }
    case K_LIT_STRING: {
      uint32_t len = 0;
      char *b = str_bytes(x, &len);
      if (!b) { p->err = 1; break; }
      if (needs_quoting(b, len)) {
        pb_c(p, '"');
        for (uint32_t i = 0; i < len; i++) pb_escaped(p, b[i]);
        pb_c(p, '"');
      } else {
        pb_put(p, b, len);
      }
      free(b);
      break;
    }
    case K_VAR_REF:
      pb_c(p, (flags_of(v) & 1u) ? '^' : '$');
      pb_text(p, x, 0);
      break;
    case K_COMMAND: pp_command(p, v); break;
    case K_BLOCK:
      pb_c(p, '{');
      if (n > P + 1) {
        pb_c(p, ' ');
        for (uint32_t i = P + 1; i < n; i++) {
          if (i > P + 1) pb_s(p, "; ");
          pp(p, jacl_vec_get(v, i));
        }
        pb_c(p, ' ');
      }
      pb_c(p, '}');
      break;
    case K_INTERP_STRING:
      pb_c(p, '"');
      for (uint32_t i = P; i < n; i++) {
        JaclVal seg = jacl_vec_get(v, i);
        if (!is_syntax(seg)) { p->err = 1; break; }
        if (kind_of(seg) == K_LIT_STRING && jacl_vec_count(seg) > P) {
          pb_text(p, jacl_vec_get(seg, P), 1);
        } else if (kind_of(seg) == K_VAR_REF && jacl_vec_count(seg) > P) {
          pb_c(p, '$');
          pb_text(p, jacl_vec_get(seg, P), 0);
        } else if (kind_of(seg) == K_COMMAND && jacl_vec_count(seg) >= P + 2) {
          pb_s(p, "$[");
          pp_head_args(p, jacl_vec_get(seg, P + 1), seg, P + 2);
          pb_c(p, ']');
        }
      }
      pb_c(p, '"');
      break;
    case K_QUOTE:            pb_s(p, "quote ");        pp(p, x); break;
    case K_SYNTAX_QUOTE:     pb_s(p, "syntax-quote "); pp(p, x); break;
    case K_UNQUOTE:          pb_c(p, '~');             pp(p, x); break;
    case K_UNQUOTE_SPLICING: pb_s(p, "~@");            pp(p, x); break;
    case K_SPREAD:           pb_s(p, "..");            pp(p, x); break;
    case K_BREAK:
    case K_RETURN:
      pb_s(p, k == K_BREAK ? "break" : "return");
      if (!jaclrt_is_nil(x)) { pb_c(p, ' '); pp(p, x); }
      break;
    case K_CONTINUE: pb_s(p, "continue"); break;
    case K_SHELL_CMD:
      if (n < P + 3) { p->err = 1; break; }
      pb_c(p, '!');
      pp_head_args(p, jacl_vec_get(v, P + 2), v, P + 3);
      break;
    default: p->err = 1; break;
  }
}

JaclVal synrt_syntax_str(JaclVal s) {
  if (!is_syntax(s)) return SYN_FAIL("syntax-str: not a syntax value");
  PB p = {0};
  pp(&p, s);
  if (p.err) { free(p.b); return SYN_FAIL("syntax-str: malformed syntax value"); }
  JaclVal out = jacl_str_new(p.b ? p.b : "", p.n);
  free(p.b);
  return out;
}
