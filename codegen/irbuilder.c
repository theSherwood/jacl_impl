/* irbuilder.c — implementation of the JACL-owned SVM-IR builder (P2.1). See irbuilder.h. */
#include "irbuilder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- internal representation (mirrors svm-ir's Inst / Terminator shape) ---- */

typedef enum {
  K_CONST_I32, K_CONST_I64, K_INTBIN, K_INTCMP, K_LOAD, K_STORE, K_CALL, K_CALL_IMPORT
} InstKind;

typedef struct {
  InstKind kind;
  uint8_t  nresults;   /* 0 or 1: drives value numbering + the `vN =` binding */
  IrType   ty;         /* operand type for intbin/intcmp */
  int      op;         /* IrBinOp / IrCmpOp / IrLoadOp / IrStoreOp */
  int32_t  c32;
  int64_t  c64;
  IrVal    a, x;       /* binary/cmp operands */
  IrVal    addr, value;
  uint64_t offset;
  uint8_t  align;
  uint32_t callee;     /* direct-call function index */
  char    *name;       /* call.import name (owned) */
  IrType  *sig_params; int sig_np;
  IrType  *sig_results; int sig_nr;
  IrVal    handle;
  IrVal   *args; int nargs;
} Inst;

typedef enum { T_NONE, T_BR, T_BRIF, T_RETURN } TermKind;

typedef struct {
  TermKind kind;
  IrBlock  target;             /* br */
  IrVal   *args; int nargs;    /* br args */
  IrVal    cond;               /* br_if */
  IrBlock  then_blk; IrVal *then_args; int nthen;
  IrBlock  else_blk; IrVal *else_args; int nelse;
  IrVal   *ret_vals; int nret; /* return */
} Term;

typedef struct {
  IrType  *params; int nparams;
  uint32_t next_val;           /* next value id to assign in this block */
  Inst    *insts; int ninsts, cap_insts;
  Term     term;
} Block;

struct IrFunc {
  IrModule *module;
  IrType   *params; int nparams;
  IrType   *results; int nresults;
  Block    *blocks; int nblocks, cap_blocks;
};

struct IrModule {
  IrFunc **funcs; int nfuncs, cap_funcs;
  int      has_memory; uint8_t mem_log2;
};

/* ---- small helpers ---- */

static void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  return p;
}

static IrType *dup_types(const IrType *src, int n) {
  IrType *d = xmalloc((size_t)n * sizeof(IrType));
  if (n) memcpy(d, src, (size_t)n * sizeof(IrType));
  return d;
}
static IrVal *dup_vals(const IrVal *src, int n) {
  IrVal *d = xmalloc((size_t)n * sizeof(IrVal));
  if (n) memcpy(d, src, (size_t)n * sizeof(IrVal));
  return d;
}

static Block *block_at(IrFunc *f, IrBlock b) {
  if (b < 0 || b >= f->nblocks) { fprintf(stderr, "irbuilder: bad block %d\n", b); abort(); }
  return &f->blocks[b];
}

/* Append an instruction to a block; returns a pointer to fill in. */
static Inst *push_inst(Block *blk) {
  if (blk->ninsts == blk->cap_insts) {
    blk->cap_insts = blk->cap_insts ? blk->cap_insts * 2 : 8;
    blk->insts = realloc(blk->insts, (size_t)blk->cap_insts * sizeof(Inst));
    if (!blk->insts) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  }
  Inst *in = &blk->insts[blk->ninsts++];
  memset(in, 0, sizeof(*in));
  return in;
}

/* Reserve a result value id for a 1-result instruction. */
static IrVal take_val(Block *blk) { return blk->next_val++; }

/* ---- module / function / block ---- */

IrModule *irb_module_new(void) {
  IrModule *m = xmalloc(sizeof(*m));
  m->funcs = NULL; m->nfuncs = 0; m->cap_funcs = 0;
  m->has_memory = 0; m->mem_log2 = 0;
  return m;
}

void irb_set_memory(IrModule *m, uint8_t size_log2) {
  m->has_memory = 1; m->mem_log2 = size_log2;
}

IrFunc *irb_func_new(IrModule *m, const IrType *params, int nparams,
                     const IrType *results, int nresults) {
  IrFunc *f = xmalloc(sizeof(*f));
  f->module = m;
  f->params = dup_types(params, nparams);   f->nparams = nparams;
  f->results = dup_types(results, nresults); f->nresults = nresults;
  f->blocks = NULL; f->nblocks = 0; f->cap_blocks = 0;
  if (m->nfuncs == m->cap_funcs) {
    m->cap_funcs = m->cap_funcs ? m->cap_funcs * 2 : 4;
    m->funcs = realloc(m->funcs, (size_t)m->cap_funcs * sizeof(IrFunc *));
    if (!m->funcs) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  }
  m->funcs[m->nfuncs++] = f;
  return f;
}

int irb_func_index(const IrModule *m, const IrFunc *f) {
  for (int i = 0; i < m->nfuncs; i++) if (m->funcs[i] == f) return i;
  return -1;
}

IrBlock irb_block(IrFunc *f, const IrType *params, int nparams) {
  if (f->nblocks == f->cap_blocks) {
    f->cap_blocks = f->cap_blocks ? f->cap_blocks * 2 : 4;
    f->blocks = realloc(f->blocks, (size_t)f->cap_blocks * sizeof(Block));
    if (!f->blocks) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  }
  Block *blk = &f->blocks[f->nblocks];
  memset(blk, 0, sizeof(*blk));
  blk->params = dup_types(params, nparams);
  blk->nparams = nparams;
  blk->next_val = (uint32_t)nparams;   /* params occupy v0..v{nparams-1} */
  blk->term.kind = T_NONE;
  return f->nblocks++;
}

/* ---- value-producing instructions ---- */

IrVal irb_const_i32(IrFunc *f, IrBlock b, int32_t c) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_CONST_I32; in->nresults = 1; in->c32 = c;
  return take_val(blk);
}
IrVal irb_const_i64(IrFunc *f, IrBlock b, int64_t c) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_CONST_I64; in->nresults = 1; in->c64 = c;
  return take_val(blk);
}
IrVal irb_intbin(IrFunc *f, IrBlock b, IrType ty, IrBinOp op, IrVal a, IrVal x) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_INTBIN; in->nresults = 1; in->ty = ty; in->op = (int)op; in->a = a; in->x = x;
  return take_val(blk);
}
IrVal irb_intcmp(IrFunc *f, IrBlock b, IrType ty, IrCmpOp op, IrVal a, IrVal x) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_INTCMP; in->nresults = 1; in->ty = ty; in->op = (int)op; in->a = a; in->x = x;
  return take_val(blk);
}
IrVal irb_load(IrFunc *f, IrBlock b, IrLoadOp op, IrVal addr, uint64_t offset, uint8_t align) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_LOAD; in->nresults = 1; in->op = (int)op;
  in->addr = addr; in->offset = offset; in->align = align;
  return take_val(blk);
}
void irb_store(IrFunc *f, IrBlock b, IrStoreOp op, IrVal addr, IrVal value,
               uint64_t offset, uint8_t align) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_STORE; in->nresults = 0; in->op = (int)op;
  in->addr = addr; in->value = value; in->offset = offset; in->align = align;
}
IrVal irb_call(IrFunc *f, IrBlock b, const IrFunc *callee, const IrVal *args, int nargs) {
  Block *blk = block_at(f, b);
  int idx = irb_func_index(f->module, callee);
  if (idx < 0) { fprintf(stderr, "irbuilder: call to unknown function\n"); abort(); }
  Inst *in = push_inst(blk);
  in->kind = K_CALL; in->callee = (uint32_t)idx;
  in->args = dup_vals(args, nargs); in->nargs = nargs;
  in->nresults = (uint8_t)(callee->nresults == 1 ? 1 : 0);
  return in->nresults ? take_val(blk) : 0;
}
IrVal irb_call_import(IrFunc *f, IrBlock b, const char *name,
                      const IrType *params, int nparams,
                      const IrType *results, int nresults,
                      IrVal handle, const IrVal *args, int nargs) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_CALL_IMPORT;
  in->name = xmalloc(strlen(name) + 1); strcpy(in->name, name);
  in->sig_params = dup_types(params, nparams); in->sig_np = nparams;
  in->sig_results = dup_types(results, nresults); in->sig_nr = nresults;
  in->handle = handle;
  in->args = dup_vals(args, nargs); in->nargs = nargs;
  in->nresults = (uint8_t)(nresults == 1 ? 1 : 0);
  return in->nresults ? take_val(blk) : 0;
}

/* ---- terminators ---- */

void irb_br(IrFunc *f, IrBlock b, IrBlock target, const IrVal *args, int nargs) {
  Block *blk = block_at(f, b);
  blk->term.kind = T_BR; blk->term.target = target;
  blk->term.args = dup_vals(args, nargs); blk->term.nargs = nargs;
}
void irb_br_if(IrFunc *f, IrBlock b, IrVal cond,
               IrBlock then_blk, const IrVal *then_args, int nthen,
               IrBlock else_blk, const IrVal *else_args, int nelse) {
  Block *blk = block_at(f, b);
  blk->term.kind = T_BRIF; blk->term.cond = cond;
  blk->term.then_blk = then_blk; blk->term.then_args = dup_vals(then_args, nthen); blk->term.nthen = nthen;
  blk->term.else_blk = else_blk; blk->term.else_args = dup_vals(else_args, nelse); blk->term.nelse = nelse;
}
void irb_return(IrFunc *f, IrBlock b, const IrVal *vals, int nvals) {
  Block *blk = block_at(f, b);
  blk->term.kind = T_RETURN;
  blk->term.ret_vals = dup_vals(vals, nvals); blk->term.nret = nvals;
}

/* ---- text serialization ---- */

/* A growable string buffer. */
typedef struct { char *buf; size_t len, cap; } Out;

static void out_reserve(Out *o, size_t extra) {
  if (o->len + extra + 1 > o->cap) {
    while (o->len + extra + 1 > o->cap) o->cap = o->cap ? o->cap * 2 : 256;
    o->buf = realloc(o->buf, o->cap);
    if (!o->buf) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  }
}
static void out_str(Out *o, const char *s) {
  size_t n = strlen(s);
  out_reserve(o, n);
  memcpy(o->buf + o->len, s, n);
  o->len += n;
  o->buf[o->len] = '\0';
}
static void out_fmt(Out *o, const char *fmt, ...) {
  char tmp[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0) { fprintf(stderr, "irbuilder: format error\n"); abort(); }
  if ((size_t)n < sizeof(tmp)) { out_str(o, tmp); return; }
  /* rare: longer than the stack buffer */
  char *big = xmalloc((size_t)n + 1);
  va_start(ap, fmt);
  vsnprintf(big, (size_t)n + 1, fmt, ap);
  va_end(ap);
  out_str(o, big);
  free(big);
}

static const char *type_name(IrType t) {
  switch (t) {
    case IRB_I32: return "i32";
    case IRB_I64: return "i64";
    case IRB_F32: return "f32";
    case IRB_F64: return "f64";
    case IRB_V128: return "v128";
    case IRB_REF: return "ref";
  }
  return "?";
}
static const char *binop_name(int op) {
  static const char *n[] = {"add","sub","mul","div_s","div_u","rem_s","rem_u",
                            "and","or","xor","shl","shr_s","shr_u","rotl","rotr"};
  return n[op];
}
static const char *cmpop_name(int op) {
  static const char *n[] = {"eq","ne","lt_s","lt_u","le_s","le_u","gt_s","gt_u","ge_s","ge_u"};
  return n[op];
}
static const char *loadop_name(int op) {
  static const char *n[] = {"i32.load","i64.load","f32.load","f64.load",
                            "i32.load8_s","i32.load8_u","i32.load16_s","i32.load16_u",
                            "i64.load8_s","i64.load8_u","i64.load16_s","i64.load16_u",
                            "i64.load32_s","i64.load32_u"};
  return n[op];
}
static const char *storeop_name(int op) {
  static const char *n[] = {"i32.store","i64.store","f32.store","f64.store",
                            "i32.store8","i32.store16","i64.store8","i64.store16","i64.store32"};
  return n[op];
}

/* `(v0, v1, ...)` — matches svm-text's arglist. */
static void out_arglist(Out *o, const IrVal *args, int n) {
  out_str(o, "(");
  for (int i = 0; i < n; i++) out_fmt(o, "%sv%u", i ? ", " : "", args[i]);
  out_str(o, ")");
}
static void out_typelist(Out *o, const IrType *ts, int n) {
  for (int i = 0; i < n; i++) out_fmt(o, "%s%s", i ? ", " : "", type_name(ts[i]));
}
/* ` offset=N align=N` (each omitted when zero) — matches svm-text's memarg. */
static void out_memarg(Out *o, uint64_t offset, uint8_t align) {
  if (offset != 0) out_fmt(o, " offset=%llu", (unsigned long long)offset);
  if (align != 0) out_fmt(o, " align=%u", (unsigned)align);
}

static void out_inst(Out *o, const Inst *in) {
  switch (in->kind) {
    case K_CONST_I32: out_fmt(o, "i32.const %d", in->c32); break;
    case K_CONST_I64: out_fmt(o, "i64.const %lld", (long long)in->c64); break;
    case K_INTBIN:
      out_fmt(o, "%s.%s v%u v%u", type_name(in->ty), binop_name(in->op), in->a, in->x); break;
    case K_INTCMP:
      out_fmt(o, "%s.%s v%u v%u", type_name(in->ty), cmpop_name(in->op), in->a, in->x); break;
    case K_LOAD:
      out_fmt(o, "%s v%u", loadop_name(in->op), in->addr); out_memarg(o, in->offset, in->align); break;
    case K_STORE:
      out_fmt(o, "%s v%u v%u", storeop_name(in->op), in->addr, in->value);
      out_memarg(o, in->offset, in->align); break;
    case K_CALL:
      out_fmt(o, "call %u", in->callee); out_arglist(o, in->args, in->nargs); break;
    case K_CALL_IMPORT:
      out_fmt(o, "call.import \"%s\" (", in->name);
      out_typelist(o, in->sig_params, in->sig_np);
      out_str(o, ") -> (");
      out_typelist(o, in->sig_results, in->sig_nr);
      out_fmt(o, ") v%u", in->handle);
      out_arglist(o, in->args, in->nargs);
      break;
  }
}

static void out_term(Out *o, const Term *t) {
  switch (t->kind) {
    case T_BR:
      out_fmt(o, "br block%d", t->target); out_arglist(o, t->args, t->nargs); break;
    case T_BRIF:
      out_fmt(o, "br_if v%u block%d", t->cond, t->then_blk);
      out_arglist(o, t->then_args, t->nthen);
      out_fmt(o, " block%d", t->else_blk);
      out_arglist(o, t->else_args, t->nelse);
      break;
    case T_RETURN:
      if (t->nret == 0) { out_str(o, "return"); }
      else {
        out_str(o, "return ");
        for (int i = 0; i < t->nret; i++) out_fmt(o, "%sv%u", i ? ", " : "", t->ret_vals[i]);
      }
      break;
    case T_NONE:
    default:
      /* No terminator set — emit `unreachable` so the text still parses (a missing
       * terminator is a codegen bug the verifier would otherwise flag downstream). */
      out_str(o, "unreachable");
      break;
  }
}

static void out_func(Out *o, const IrFunc *f) {
  out_str(o, "func (");
  out_typelist(o, f->params, f->nparams);
  out_str(o, ") -> (");
  out_typelist(o, f->results, f->nresults);
  out_str(o, ") {\n");
  for (int bi = 0; bi < f->nblocks; bi++) {
    const Block *blk = &f->blocks[bi];
    out_fmt(o, "block%d(", bi);
    for (int i = 0; i < blk->nparams; i++)
      out_fmt(o, "%sv%d: %s", i ? ", " : "", i, type_name(blk->params[i]));
    out_str(o, "):\n");
    uint32_t next = (uint32_t)blk->nparams;   /* recompute numbering, as svm-text does */
    for (int ii = 0; ii < blk->ninsts; ii++) {
      const Inst *in = &blk->insts[ii];
      if (in->nresults == 0) { out_str(o, "  "); out_inst(o, in); out_str(o, "\n"); }
      else {
        out_fmt(o, "  v%u = ", next);
        out_inst(o, in);
        out_str(o, "\n");
        next += in->nresults;
      }
    }
    out_str(o, "  ");
    out_term(o, &blk->term);
    out_str(o, "\n");
  }
  out_str(o, "}\n");
}

char *irb_to_text(const IrModule *m) {
  Out o = {0};
  if (m->has_memory) out_fmt(&o, "memory %u\n\n", (unsigned)m->mem_log2);
  for (int i = 0; i < m->nfuncs; i++) {
    if (i > 0) out_str(&o, "\n");
    out_func(&o, m->funcs[i]);
  }
  if (!o.buf) { o.buf = xmalloc(1); o.buf[0] = '\0'; }
  return o.buf;
}

/* ---- teardown ---- */

static void free_inst(Inst *in) {
  free(in->name);
  free(in->sig_params);
  free(in->sig_results);
  free(in->args);
}
static void free_block(Block *blk) {
  for (int i = 0; i < blk->ninsts; i++) free_inst(&blk->insts[i]);
  free(blk->insts);
  free(blk->params);
  free(blk->term.args);
  free(blk->term.then_args);
  free(blk->term.else_args);
  free(blk->term.ret_vals);
}
void irb_module_free(IrModule *m) {
  if (!m) return;
  for (int i = 0; i < m->nfuncs; i++) {
    IrFunc *f = m->funcs[i];
    for (int b = 0; b < f->nblocks; b++) free_block(&f->blocks[b]);
    free(f->blocks);
    free(f->params);
    free(f->results);
    free(f);
  }
  free(m->funcs);
  free(m);
}
