/* syn_wire — AST subtree <-> flat byte buffer. See syn_wire.h. */
#include "syn_wire.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* v3: every node carries a position handle, every expression node kind crosses, and the
 * result wire leads with a status byte. v2 widened AST_LIT_INT from 4 bytes to 8 (jacl
 * #113). Both ends of this wire are built from these sources in-tree, so there is no
 * old-reader compatibility to keep — but any prebuilt `.temen` that embeds the codec has
 * to be rebuilt. Keep in step with syn_rt.c. */
#define SYNW_VERSION 3

/* The result wire's status byte. */
#define SYNW_RESULT_OK    0
#define SYNW_RESULT_ERROR 1

/* ---- one expansion ---------------------------------------------------------- */

static uint32_t synw__expansions;   /* per process, so REPL lines never reuse a number */

void synw_call_begin(SynwCall *c, AstNode *call) {
  memset(c, 0, sizeof *c);
  c->call_start = call->start;
  c->call_end = call->end;
  c->expansion = ++synw__expansions;
}

void synw_call_end(SynwCall *c) {
  free(c->starts);
  free(c->ends);
  memset(c, 0, sizeof *c);
}

const char *synw_call_refusal(const SynwCall *c, char *buf, size_t cap) {
  if (c->bad)
    snprintf(buf, cap, "%s (at %u:%u) cannot be passed to a macro", c->why,
             c->bad->start.line, c->bad->start.column);
  else
    snprintf(buf, cap, "an argument could not be passed to the macro");
  return buf;
}

/* ---- encode ---------------------------------------------------------------- */

typedef struct { unsigned char *buf; size_t len, cap; int err; SynwCall *call; } W;

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
static void w_str(W *w, const char *s, uint32_t n) { w_u32(w, n); w_need(w, n); if (w->err) return; if (n) memcpy(w->buf + w->len, s, n); w->len += n; }

static void w_refuse(W *w, AstNode *n, const char *why) {
  if (!w->err && w->call) { w->call->bad = n; w->call->why = why; }
  w->err = 1;
}

/* Record where `n` is in the source and return its handle (0 without a call table). */
static uint32_t w_pos(W *w, AstNode *n) {
  SynwCall *c = w->call;
  if (!c || w->err) return 0;
  if (c->count == c->cap) {
    uint32_t cap = c->cap ? c->cap * 2 : 64;
    SourcePos *s = (SourcePos *)realloc(c->starts, cap * sizeof(SourcePos));
    if (!s) { w->err = 1; return 0; }
    c->starts = s;
    SourcePos *e = (SourcePos *)realloc(c->ends, cap * sizeof(SourcePos));
    if (!e) { w->err = 1; return 0; }
    c->ends = e;
    c->cap = cap;
  }
  c->starts[c->count] = n->start;
  c->ends[c->count] = n->end;
  return ++c->count;
}

static void w_node(W *w, AstNode *n);

static void w_opt(W *w, AstNode *n) { w_u8(w, n ? 1 : 0); if (n) w_node(w, n); }

static void w_nodes(W *w, AstNode **ns, uint32_t count) {
  w_u32(w, count);
  for (uint32_t i = 0; i < count; i++) w_node(w, ns[i]);
}

static void w_node(W *w, AstNode *n) {
  if (w->err) return;
  if (!n) { w->err = 1; return; }
  w_u8(w, (unsigned)n->type);
  w_u32(w, n->scope_mark);
  w_u8(w, (unsigned)((n->is_caret ? 1u : 0u) | (n->is_gensym ? 2u : 0u)));
  w_u32(w, w_pos(w, n));
  switch (n->type) {
    /* The staged form carries the literal as an i64 (syn_rt.c mirrors this wire byte for
     * byte), so every literal ordinary code accepts can also be quoted — jacl #113.
     *
     * A *bigint* literal still cannot: it has no 64-bit form at all, so the wire would have
     * to carry its digit span and the plain-data side a heap bigint. Refused loudly, which is
     * the #105 ruling — a literal that does not fit is an error, never a different number. */
    case AST_LIT_INT:
      if (n->data.lit_int.big) { w_refuse(w, n, "a literal wider than 64 bits"); return; }
      w_u64(w, (uint64_t)n->data.lit_int.value);
      break;
    case AST_LIT_FLOAT: { uint32_t b; memcpy(&b, &n->data.lit_float.value, 4); w_u32(w, b); } break;
    case AST_LIT_STRING: w_str(w, n->data.lit_string.value, n->data.lit_string.length); break;
    case AST_VAR_REF:    w_str(w, n->data.var_ref.name, n->data.var_ref.length); break;
    case AST_COMMAND:
      w_u8(w, n->data.command.head_id);
      w_opt(w, n->data.command.head);
      w_nodes(w, n->data.command.args, n->data.command.arg_count);
      break;
    case AST_BLOCK:
      w_u8(w, n->data.block.trailing_semi ? 1u : 0u);
      w_nodes(w, n->data.block.commands, n->data.block.count);
      break;
    case AST_INTERP_STRING:
      w_nodes(w, n->data.interp_string.segments, n->data.interp_string.count);
      break;
    case AST_QUOTE:            w_node(w, n->data.quote.child); break;
    case AST_SYNTAX_QUOTE:     w_node(w, n->data.syntax_quote.child); break;
    case AST_UNQUOTE:          w_node(w, n->data.unquote.child); break;
    case AST_UNQUOTE_SPLICING: w_node(w, n->data.unquote_splicing.child); break;
    case AST_SPREAD:           w_node(w, n->data.spread.expr); break;
    case AST_BREAK:            w_opt(w, n->data.break_stmt.value); break;
    case AST_RETURN:           w_opt(w, n->data.return_stmt.value); break;
    case AST_CONTINUE:         break;
    case AST_SHELL_CMD:
      w_u8(w, n->data.shell_cmd.head_id);
      w_u8(w, (unsigned)((n->data.shell_cmd.background ? 1u : 0u) | (n->data.shell_cmd.stream ? 2u : 0u)));
      w_node(w, n->data.shell_cmd.head);
      w_nodes(w, n->data.shell_cmd.args, n->data.shell_cmd.arg_count);
      break;
    /* The top-level declarations, the typer's destructuring patterns and parse errors never
     * appear inside an expression. */
    default: w_refuse(w, n, "this form"); break;
  }
}

unsigned char *synw_encode(AstNode *node, size_t *out_len, SynwCall *c) {
  W w = {0};
  w.call = c;
  w_u8(&w, SYNW_VERSION);
  w_node(&w, node);
  if (w.err) { free(w.buf); return NULL; }
  if (out_len) *out_len = w.len;
  return w.buf;
}

unsigned char *synw_encode_args(MacroEntry *entry, AstNode *call, size_t *out_len, SynwCall *c) {
  W w = {0};
  w.call = c;
  uint32_t argc = call->data.command.arg_count;
  uint32_t nfixed = (entry->variadic && entry->param_count > 0) ? entry->param_count - 1
                                                                : entry->param_count;
  if (nfixed > argc) nfixed = argc;
  w_u8(&w, SYNW_VERSION);
  for (uint32_t i = 0; i < nfixed; i++) w_node(&w, call->data.command.args[i]);
  if (entry->variadic)   /* no rest arguments: `args` may be NULL, so do not offset it */
    w_nodes(&w, argc > nfixed ? call->data.command.args + nfixed : NULL, argc - nfixed);
  if (w.err) { free(w.buf); return NULL; }
  if (out_len) *out_len = w.len;
  return w.buf;
}

/* ---- decode ---------------------------------------------------------------- */

typedef struct { const unsigned char *buf; size_t len, pos; int err; arena_t *arena; SynwCall *call; } R;

static unsigned r_u8(R *r) { if (r->err || r->pos + 1 > r->len) { r->err = 1; return 0; } return r->buf[r->pos++]; }
static uint32_t r_u32(R *r) { if (r->err || r->pos + 4 > r->len) { r->err = 1; return 0; } uint32_t v; memcpy(&v, r->buf + r->pos, 4); r->pos += 4; return v; }
static uint64_t r_u64(R *r) { if (r->err || r->pos + 8 > r->len) { r->err = 1; return 0; } uint64_t v; memcpy(&v, r->buf + r->pos, 8); r->pos += 8; return v; }
static const char *r_str(R *r, uint32_t *out_n) {
  uint32_t n = r_u32(r);
  if (r->err || r->pos + n > r->len) { r->err = 1; *out_n = 0; return NULL; }
  char *s = (char *)arena_alloc(r->arena, n + 1);
  if (n) memcpy(s, r->buf + r->pos, n);
  s[n] = 0; r->pos += n; *out_n = n; return s;
}

static AstNode *r_node(R *r);

static AstNode *r_opt(R *r) { return r_u8(r) ? r_node(r) : NULL; }

static AstNode **r_nodes(R *r, uint32_t *out_count) {
  uint32_t count = r_u32(r);
  /* Every node takes at least its 10-byte header, so a count the buffer cannot hold is
   * malformed — refuse it before allocating for it. */
  if (r->err || count > (r->len - r->pos) / 10) { r->err = 1; *out_count = 0; return NULL; }
  AstNode **ns = count ? (AstNode **)arena_alloc(r->arena, sizeof(AstNode *) * count) : NULL;
  for (uint32_t i = 0; i < count; i++) ns[i] = r_node(r);
  *out_count = count;
  return ns;
}

/* A node from the argument wire gets back the position it had there; one the macro made
 * takes the macro call's. */
static void r_place(R *r, AstNode *n, uint32_t handle) {
  SynwCall *c = r->call;
  if (!c) return;
  if (handle >= 1 && handle <= c->count) {
    n->start = c->starts[handle - 1];
    n->end = c->ends[handle - 1];
  } else {
    n->start = c->call_start;
    n->end = c->call_end;
  }
}

/* A gensym minted in this run is numbered from zero by the run, so the same names come
 * out of every expansion; suffixing the expansion's number makes them distinct. One that
 * came in as an argument has a handle, and keeps the name an earlier expansion gave it. */
static void r_name_gensym(R *r, AstNode *n, uint32_t handle) {
  if (!r->call || handle != 0 || !n->is_gensym || n->type != AST_VAR_REF) return;
  uint32_t len = n->data.var_ref.length;
  char *s = (char *)arena_alloc(r->arena, len + 12);
  int w = snprintf(s, len + 12, "%.*s_%u", (int)len, n->data.var_ref.name, r->call->expansion);
  n->data.var_ref.name = s;
  n->data.var_ref.length = (uint32_t)w;
}

static AstNode *r_node(R *r) {
  if (r->err) return NULL;
  AstNode *n = ast_alloc(r->arena);
  n->type = (AstNodeType)r_u8(r);
  n->scope_mark = r_u32(r);
  unsigned fl = r_u8(r);
  n->is_caret = (fl & 1u) ? 1 : 0;
  n->is_gensym = (fl & 2u) ? 1 : 0;
  uint32_t handle = r_u32(r);
  if (r->err) return NULL;
  r_place(r, n, handle);
  switch (n->type) {
    case AST_LIT_INT:
      n->data.lit_int.value = (int64_t)r_u64(r);
      n->data.lit_int.big = NULL; n->data.lit_int.big_len = 0;   /* never a bigint on the wire */
      break;
    case AST_LIT_FLOAT: { uint32_t b = r_u32(r); memcpy(&n->data.lit_float.value, &b, 4); } break;
    case AST_LIT_STRING: n->data.lit_string.value = r_str(r, &n->data.lit_string.length); break;
    case AST_VAR_REF:
      n->data.var_ref.name = r_str(r, &n->data.var_ref.length);
      if (!r->err) r_name_gensym(r, n, handle);
      break;
    case AST_COMMAND:
      n->data.command.head_id = (uint8_t)r_u8(r);
      n->data.command.head = r_opt(r);
      n->data.command.args = r_nodes(r, &n->data.command.arg_count);
      break;
    case AST_BLOCK:
      n->data.block.trailing_semi = r_u8(r) ? true : false;
      n->data.block.commands = r_nodes(r, &n->data.block.count);
      break;
    case AST_INTERP_STRING:
      n->data.interp_string.segments = r_nodes(r, &n->data.interp_string.count);
      break;
    case AST_QUOTE:            n->data.quote.child = r_node(r); break;
    case AST_SYNTAX_QUOTE:     n->data.syntax_quote.child = r_node(r); break;
    case AST_UNQUOTE:          n->data.unquote.child = r_node(r); break;
    case AST_UNQUOTE_SPLICING: n->data.unquote_splicing.child = r_node(r); break;
    case AST_SPREAD:           n->data.spread.expr = r_node(r); break;
    case AST_BREAK:            n->data.break_stmt.value = r_opt(r); break;
    case AST_RETURN:           n->data.return_stmt.value = r_opt(r); break;
    case AST_CONTINUE:         break;
    case AST_SHELL_CMD: {
      n->data.shell_cmd.head_id = (uint8_t)r_u8(r);
      unsigned bits = r_u8(r);
      n->data.shell_cmd.background = (bits & 1u) ? 1 : 0;
      n->data.shell_cmd.stream = (bits & 2u) ? 1 : 0;
      n->data.shell_cmd.head = r_node(r);
      n->data.shell_cmd.args = r_nodes(r, &n->data.shell_cmd.arg_count);
      break;
    }
    default: r->err = 1; break;
  }
  return r->err ? NULL : n;
}

AstNode *synw_decode(const unsigned char *buf, size_t len, arena_t *arena, SynwCall *c) {
  R r = { buf, len, 0, 0, arena, c };
  if (r_u8(&r) != SYNW_VERSION) return NULL;
  AstNode *n = r_node(&r);
  return r.err ? NULL : n;
}

AstNode *synw_decode_result(const unsigned char *buf, size_t len, arena_t *arena, SynwCall *c,
                            const char **err) {
  R r = { buf, len, 0, 0, arena, c };
  *err = NULL;
  if (!buf || len == 0) { *err = "the staged macro wrote no result"; return NULL; }
  if (r_u8(&r) != SYNW_VERSION) { *err = "the staged macro's result has the wrong wire version"; return NULL; }
  unsigned status = r_u8(&r);
  if (status == SYNW_RESULT_ERROR) {
    uint32_t n;
    const char *msg = r_str(&r, &n);
    *err = msg ? msg : "the staged macro failed";
    return NULL;
  }
  AstNode *n = (status == SYNW_RESULT_OK) ? r_node(&r) : NULL;
  if (r.err || !n) { *err = "staged expansion produced invalid syntax"; return NULL; }
  return n;
}
