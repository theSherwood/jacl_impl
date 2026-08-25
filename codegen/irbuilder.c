/* irbuilder.c — implementation of the JACL-owned SVM-IR builder (P2.1). See irbuilder.h. */
#include "irbuilder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- internal representation (mirrors svm-ir's Inst / Terminator shape) ---- */

typedef enum {
  K_CONST_I32, K_CONST_I64, K_INTBIN, K_INTCMP, K_LOAD, K_STORE, K_CALL, K_CALL_IMPORT,
  K_REF_FUNC, K_CONVERT, K_CALL_INDIRECT, K_SUSPEND, K_DATA_SELF
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

typedef enum { T_NONE, T_BR, T_BRIF, T_RETURN, T_RETURN_CALL } TermKind;

typedef struct {
  TermKind kind;
  IrBlock  target;             /* br */
  IrVal   *args; int nargs;    /* br args */
  IrVal    cond;               /* br_if */
  IrBlock  then_blk; IrVal *then_args; int nthen;
  IrBlock  else_blk; IrVal *else_args; int nelse;
  IrVal   *ret_vals; int nret; /* return */
  uint32_t rc_func;            /* return_call: callee function index */
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

typedef struct { uint64_t off; char *bytes; uint32_t len; } DataSeg;

struct IrModule {
  IrFunc **funcs; int nfuncs, cap_funcs;
  int      has_memory; uint8_t mem_log2;
  DataSeg *data; int ndata, cap_data; uint64_t data_top;
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
  /* Zero the WHOLE struct: irb_module_free / irb_add_data also touch data/ndata/
   * cap_data, which a field-by-field init could miss — leaving heap garbage that
   * free()/realloc() blew up on whenever the allocator handed back a dirty chunk. */
  memset(m, 0, sizeof(*m));
  return m;
}

void irb_set_memory(IrModule *m, uint8_t size_log2) {
  m->has_memory = 1; m->mem_log2 = size_log2;
}

uint64_t irb_add_data(IrModule *m, const char *bytes, uint32_t len) {
  uint64_t off = (m->data_top + 7) & ~(uint64_t)7; /* 8-byte align */
  if (m->ndata == m->cap_data) {
    m->cap_data = m->cap_data ? m->cap_data * 2 : 8;
    m->data = realloc(m->data, (size_t)m->cap_data * sizeof(DataSeg));
    if (!m->data) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  }
  char *copy = xmalloc(len ? len : 1);
  if (len) memcpy(copy, bytes, len);
  m->data[m->ndata++] = (DataSeg){off, copy, len};
  m->data_top = off + len;
  return off;
}

IrVal irb_data_addr(IrFunc *f, IrBlock b, uint64_t local_off) {
  /* Link-form own-data address (v9 `data.self`): the object dialect carries the offset in
   * the instruction itself; `link` rewrites it to `i64.const (dbase + local_off)`. Replaces
   * the v8 `i64.const` + out-of-band SelfData relocation. */
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_DATA_SELF; in->nresults = 1; in->offset = local_off;
  return take_val(blk);
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
IrVal irb_ref_func(IrFunc *f, IrBlock b, const IrFunc *callee) {
  Block *blk = block_at(f, b);
  int idx = irb_func_index(f->module, callee);
  if (idx < 0) { fprintf(stderr, "irbuilder: ref.func to unknown function\n"); abort(); }
  Inst *in = push_inst(blk);
  in->kind = K_REF_FUNC; in->nresults = 1; in->callee = (uint32_t)idx;
  return take_val(blk);
}
IrVal irb_convert(IrFunc *f, IrBlock b, IrConvOp op, IrVal a) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_CONVERT; in->nresults = 1; in->op = (int)op; in->a = a;
  return take_val(blk);
}
IrVal irb_suspend(IrFunc *f, IrBlock b, IrVal value) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_SUSPEND; in->nresults = 1; in->a = value;
  return take_val(blk);
}
IrVal irb_call_indirect(IrFunc *f, IrBlock b,
                        const IrType *params, int nparams,
                        const IrType *results, int nresults,
                        IrVal idx, const IrVal *args, int nargs) {
  Block *blk = block_at(f, b);
  Inst *in = push_inst(blk);
  in->kind = K_CALL_INDIRECT;
  in->sig_params = dup_types(params, nparams); in->sig_np = nparams;
  in->sig_results = dup_types(results, nresults); in->sig_nr = nresults;
  in->addr = idx; /* reuse `addr` slot for the index operand */
  in->args = dup_vals(args, nargs); in->nargs = nargs;
  in->nresults = (uint8_t)(nresults == 1 ? 1 : 0);
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
void irb_return_call(IrFunc *f, IrBlock b, const IrFunc *callee, const IrVal *args, int nargs) {
  int idx = irb_func_index(f->module, callee);
  if (idx < 0) { fprintf(stderr, "irbuilder: return_call to unknown function\n"); abort(); }
  Block *blk = block_at(f, b);
  blk->term.kind = T_RETURN_CALL;
  blk->term.rc_func = (uint32_t)idx;
  blk->term.args = dup_vals(args, nargs); blk->term.nargs = nargs;
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
      /* Name-inline link-form symbolic call: `call.sym "name" (sig) v<h> (args)` — the
       * linker (`link_with_manifest`) resolves the name against the runtime's exports.
       * (`call.import <idx>` is now the manifest capability form, emitted by svm-llvm for
       * the runtime's own `write`/`exit`; the program only makes symbolic runtime calls.) */
      out_fmt(o, "call.sym \"%s\" (", in->name);
      out_typelist(o, in->sig_params, in->sig_np);
      out_str(o, ") -> (");
      out_typelist(o, in->sig_results, in->sig_nr);
      out_fmt(o, ") v%u", in->handle);
      out_arglist(o, in->args, in->nargs);
      break;
    case K_REF_FUNC:
      out_fmt(o, "ref.func %u", in->callee); break;
    case K_DATA_SELF:
      out_fmt(o, "data.self %llu", (unsigned long long)in->offset); break;
    case K_SUSPEND:
      out_fmt(o, "suspend v%u", in->a); break;
    case K_CONVERT: {
      static const char *cn[] = {"i64.extend_i32_s", "i64.extend_i32_u", "i32.wrap_i64"};
      out_fmt(o, "%s v%u", cn[in->op], in->a); break;
    }
    case K_CALL_INDIRECT:
      out_str(o, "call_indirect (");
      out_typelist(o, in->sig_params, in->sig_np);
      out_str(o, ") -> (");
      out_typelist(o, in->sig_results, in->sig_nr);
      out_fmt(o, ") v%u", in->addr);
      out_arglist(o, in->args, in->nargs);
      break;
  }
}

static void out_term(Out *o, const Term *t) {
  switch (t->kind) {
    case T_BR:
      out_fmt(o, "br %d", t->target); out_arglist(o, t->args, t->nargs); break;
    case T_BRIF:
      out_fmt(o, "br_if v%u %d", t->cond, t->then_blk);
      out_arglist(o, t->then_args, t->nthen);
      out_fmt(o, " %d", t->else_blk);
      out_arglist(o, t->else_args, t->nelse);
      break;
    case T_RETURN:
      if (t->nret == 0) { out_str(o, "return"); }
      else {
        out_str(o, "return ");
        for (int i = 0; i < t->nret; i++) out_fmt(o, "%sv%u", i ? ", " : "", t->ret_vals[i]);
      }
      break;
    case T_RETURN_CALL:
      out_fmt(o, "return_call %u", t->rc_func);
      out_arglist(o, t->args, t->nargs);
      break;
    case T_NONE:
    default:
      /* No terminator set — emit `unreachable` so the text still parses (a missing
       * terminator is a codegen bug the verifier would otherwise flag downstream). */
      out_str(o, "unreachable");
      break;
  }
}

static void out_func(Out *o, const IrFunc *f, int idx) {
  /* §3.5 settled surface (matches svm-text's canonical printer): every definition carries
   * its positional label and one brace construct nests everything —
   * `func N (…) -> (…) { block N (…) { … } }`. Blocks indent 2 spaces, their bodies 4; a
   * branch names its target block by bare index (`br <n>(args)` / `br_if v<c> <then>(args)
   * <else>(args)`). */
  out_fmt(o, "func %d (", idx);
  out_typelist(o, f->params, f->nparams);
  out_str(o, ") -> (");
  out_typelist(o, f->results, f->nresults);
  out_str(o, ") {\n");
  for (int bi = 0; bi < f->nblocks; bi++) {
    const Block *blk = &f->blocks[bi];
    out_fmt(o, "  block %d (", bi);
    for (int i = 0; i < blk->nparams; i++)
      out_fmt(o, "%sv%d: %s", i ? ", " : "", i, type_name(blk->params[i]));
    out_str(o, ") {\n");
    uint32_t next = (uint32_t)blk->nparams;   /* recompute numbering, as svm-text does */
    for (int ii = 0; ii < blk->ninsts; ii++) {
      const Inst *in = &blk->insts[ii];
      if (in->nresults == 0) { out_str(o, "    "); out_inst(o, in); out_str(o, "\n"); }
      else {
        out_fmt(o, "    v%u = ", next);
        out_inst(o, in);
        out_str(o, "\n");
        next += in->nresults;
      }
    }
    out_str(o, "    ");
    out_term(o, &blk->term);
    out_str(o, "\n  }\n");
  }
  out_str(o, "}\n");
}

/* Escape data bytes for svm-text: printable ASCII verbatim (except \ and "), else \xHH. */
static void out_data_bytes(Out *o, const char *b, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)b[i];
    if (c == '\\') out_str(o, "\\\\");
    else if (c == '"') out_str(o, "\\\"");
    else if (c >= 0x20 && c <= 0x7e) { char s[2] = {(char)c, 0}; out_str(o, s); }
    else out_fmt(o, "\\x%02x", c);
  }
}

char *irb_to_text(const IrModule *m) {
  Out o = {0};
  for (int i = 0; i < m->ndata; i++) {
    out_fmt(&o, "data ro %llu \"", (unsigned long long)m->data[i].off);
    out_data_bytes(&o, m->data[i].bytes, m->data[i].len);
    out_str(&o, "\"\n");
  }
  if (m->has_memory) out_fmt(&o, "memory %u\n\n", (unsigned)m->mem_log2);
  for (int i = 0; i < m->nfuncs; i++) {
    if (i > 0) out_str(&o, "\n");
    out_func(&o, m->funcs[i], i);
  }
  if (!o.buf) { o.buf = xmalloc(1); o.buf[0] = '\0'; }
  return o.buf;
}

/* ---- binary (svm-encode) serialization ---- */

/* Byte-oriented writers over the same growable buffer as the text path. Unlike
 * out_str these are NUL-safe (length-tracked), mirroring svm-encode's write_* helpers
 * (crates/svm-encode/src/lib.rs). The `\0` sentinel out_reserve keeps past `len` is
 * inert here — binary content is delimited by `len`, never by a terminator. */
static void out_u8(Out *o, uint8_t b) {
  out_reserve(o, 1);
  o->buf[o->len++] = (char)b;
  o->buf[o->len] = '\0';
}
static void out_raw(Out *o, const void *p, size_t n) {
  out_reserve(o, n);
  memcpy(o->buf + o->len, p, n);
  o->len += n;
  o->buf[o->len] = '\0';
}
/* Unsigned LEB128 (svm-encode `write_uleb`). */
static void out_uleb(Out *o, uint64_t v) {
  for (;;) {
    uint8_t byte = (uint8_t)(v & 0x7f);
    v >>= 7;
    if (v) out_u8(o, byte | 0x80);
    else { out_u8(o, byte); break; }
  }
}
/* Signed LEB128 (svm-encode `write_sleb`): arithmetic shift, sign-terminated. */
static void out_sleb(Out *o, int64_t v) {
  for (;;) {
    uint8_t byte = (uint8_t)(v & 0x7f);
    v >>= 7; /* arithmetic (sign-extending) shift */
    int sign = (byte & 0x40) != 0;
    if ((v == 0 && !sign) || (v == -1 && sign)) { out_u8(o, byte); break; }
    out_u8(o, byte | 0x80);
  }
}
/* Type list: count, then one tag per type. IrType's ordinal is the svm ValType tag
 * (I32=0,I64=1,F32=2,F64=3,V128=4,REF=5 — verified against svm-ir::ValType). */
static void out_types_bin(Out *o, const IrType *ts, int n) {
  out_uleb(o, (uint64_t)n);
  for (int i = 0; i < n; i++) out_u8(o, (uint8_t)ts[i]);
}
/* Value-index list: count, then one uleb per index (svm-encode `write_idxs`). */
static void out_idxs(Out *o, const IrVal *idxs, int n) {
  out_uleb(o, (uint64_t)n);
  for (int i = 0; i < n; i++) out_uleb(o, idxs[i]);
}
/* Length-prefixed string (svm-encode `write_str`). */
static void out_str_bin(Out *o, const char *s) {
  size_t n = strlen(s);
  out_uleb(o, n);
  out_raw(o, s, n);
}

/* The import + type sections `svm_text::parse_module` interns from name-inline
 * `call.sym "name" (sig) v<h> (args)` — reconstructed here so the binary references the
 * same indices the text path would (round-trip equality). A `call.sym` first interns its
 * signature as a `TypeEntry::Func` (dedup by structural equality), then an import keyed by
 * (name, that type index); both in first-encounter order over funcs→blocks→insts, exactly
 * the source order irb_to_text emits and parse_module walks. */
typedef struct { IrType *params; int np; IrType *results; int nr; } SigEntry;
typedef struct { const char *name; uint32_t type_idx; } ImpEntry;
typedef struct {
  SigEntry *types; int ntypes, cap_types;
  ImpEntry *imports; int nimports, cap_imports;
} ImportTable;

static int sig_eq(const SigEntry *e, const IrType *p, int np, const IrType *r, int nr) {
  if (e->np != np || e->nr != nr) return 0;
  for (int i = 0; i < np; i++) if (e->params[i] != p[i]) return 0;
  for (int i = 0; i < nr; i++) if (e->results[i] != r[i]) return 0;
  return 1;
}
static uint32_t intern_type(ImportTable *t, const IrType *p, int np, const IrType *r, int nr) {
  for (int i = 0; i < t->ntypes; i++) if (sig_eq(&t->types[i], p, np, r, nr)) return (uint32_t)i;
  if (t->ntypes == t->cap_types) {
    t->cap_types = t->cap_types ? t->cap_types * 2 : 4;
    t->types = realloc(t->types, (size_t)t->cap_types * sizeof(SigEntry));
    if (!t->types) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  }
  SigEntry *e = &t->types[t->ntypes];
  e->params = dup_types(p, np); e->np = np;
  e->results = dup_types(r, nr); e->nr = nr;
  return (uint32_t)t->ntypes++;
}
static uint32_t intern_import(ImportTable *t, const char *name, uint32_t type_idx) {
  for (int i = 0; i < t->nimports; i++)
    if (t->imports[i].type_idx == type_idx && !strcmp(t->imports[i].name, name)) return (uint32_t)i;
  if (t->nimports == t->cap_imports) {
    t->cap_imports = t->cap_imports ? t->cap_imports * 2 : 4;
    t->imports = realloc(t->imports, (size_t)t->cap_imports * sizeof(ImpEntry));
    if (!t->imports) { fprintf(stderr, "irbuilder: out of memory\n"); abort(); }
  }
  t->imports[t->nimports] = (ImpEntry){name, type_idx};
  return (uint32_t)t->nimports++;
}

static void enc_inst(Out *o, const Inst *in, ImportTable *tab) {
  switch (in->kind) {
    case K_CONST_I32: out_u8(o, 0x10); out_sleb(o, (int64_t)in->c32); break;
    case K_CONST_I64: out_u8(o, 0x11); out_sleb(o, in->c64); break;
    case K_INTBIN:
      out_u8(o, (uint8_t)((in->ty == IRB_I32 ? 0x20 : 0x40) + in->op));
      out_uleb(o, in->a); out_uleb(o, in->x); break;
    case K_INTCMP:
      out_u8(o, (uint8_t)((in->ty == IRB_I32 ? 0x31 : 0x51) + in->op));
      out_uleb(o, in->a); out_uleb(o, in->x); break;
    case K_LOAD:
      out_u8(o, (uint8_t)(0xF0 + in->op));
      out_uleb(o, in->addr); out_uleb(o, in->offset); out_u8(o, in->align); break;
    case K_STORE:
      out_u8(o, (uint8_t)(0x84 + in->op));
      out_uleb(o, in->addr); out_uleb(o, in->value); out_uleb(o, in->offset); out_u8(o, in->align); break;
    case K_CALL:
      out_u8(o, 0x73); out_uleb(o, in->callee); out_idxs(o, in->args, in->nargs); break;
    case K_CALL_IMPORT: {
      /* Name-inline `call.sym` -> CALL_SYM (0x7B in the #900 consolidated call band) by
       * interned import index. */
      uint32_t ti = intern_type(tab, in->sig_params, in->sig_np, in->sig_results, in->sig_nr);
      uint32_t idx = intern_import(tab, in->name, ti);
      out_u8(o, 0x7B);
      out_uleb(o, idx);
      out_types_bin(o, in->sig_params, in->sig_np);
      out_types_bin(o, in->sig_results, in->sig_nr);
      out_uleb(o, in->handle);
      out_idxs(o, in->args, in->nargs);
      break;
    }
    case K_REF_FUNC: out_u8(o, 0x75); out_uleb(o, in->callee); break;
    case K_DATA_SELF: out_u8(o, 0x07); out_uleb(o, in->offset); break; /* object-dialect own-data addr */
    case K_CONVERT:
      out_u8(o, in->op == IRB_EXTEND_I32S ? 0x60 : in->op == IRB_EXTEND_I32U ? 0x61 : 0x62);
      out_uleb(o, in->a); break;
    case K_CALL_INDIRECT:
      out_u8(o, 0x74);
      out_types_bin(o, in->sig_params, in->sig_np);
      out_types_bin(o, in->sig_results, in->sig_nr);
      out_uleb(o, in->addr); /* the index operand reuses the `addr` slot */
      out_idxs(o, in->args, in->nargs); break;
    case K_SUSPEND: out_u8(o, 0xCC); out_uleb(o, in->a); break;
  }
}

static void enc_term(Out *o, const Term *t) {
  switch (t->kind) {
    case T_BR:
      out_u8(o, 0x80); out_uleb(o, (uint32_t)t->target); out_idxs(o, t->args, t->nargs); break;
    case T_BRIF:
      out_u8(o, 0x81); out_uleb(o, t->cond);
      out_uleb(o, (uint32_t)t->then_blk); out_idxs(o, t->then_args, t->nthen);
      out_uleb(o, (uint32_t)t->else_blk); out_idxs(o, t->else_args, t->nelse); break;
    case T_RETURN:
      out_u8(o, 0x83); out_idxs(o, t->ret_vals, t->nret); break;
    case T_RETURN_CALL:
      out_u8(o, 0x85); out_uleb(o, t->rc_func); out_idxs(o, t->args, t->nargs); break;
    case T_NONE:
    default:
      out_u8(o, 0x8F); break; /* UNREACHABLE — matches irb_to_text's missing-terminator fallback */
  }
}

static void enc_func(Out *o, const IrFunc *f, ImportTable *tab) {
  out_types_bin(o, f->params, f->nparams);
  out_types_bin(o, f->results, f->nresults);
  out_uleb(o, (uint64_t)f->nblocks);
  for (int bi = 0; bi < f->nblocks; bi++) {
    const Block *blk = &f->blocks[bi];
    out_types_bin(o, blk->params, blk->nparams);
    out_uleb(o, (uint64_t)blk->ninsts);
    for (int ii = 0; ii < blk->ninsts; ii++) enc_inst(o, &blk->insts[ii], tab);
    enc_term(o, &blk->term);
  }
}

uint8_t *irb_to_encoded(const IrModule *m, size_t *out_len) {
  /* Pre-pass: build the import/type tables (first-encounter order) before writing the
   * sections, since they precede the funcs on the wire but are discovered from call sites. */
  ImportTable tab; memset(&tab, 0, sizeof tab);
  int has_data_self = 0;
  for (int fi = 0; fi < m->nfuncs; fi++) {
    IrFunc *f = m->funcs[fi];
    for (int bi = 0; bi < f->nblocks; bi++) {
      Block *blk = &f->blocks[bi];
      for (int ii = 0; ii < blk->ninsts; ii++) {
        const Inst *in = &blk->insts[ii];
        if (in->kind == K_CALL_IMPORT) {
          uint32_t ti = intern_type(&tab, in->sig_params, in->sig_np, in->sig_results, in->sig_nr);
          intern_import(&tab, in->name, ti);
        } else if (in->kind == K_DATA_SELF) {
          has_data_self = 1;
        }
      }
    }
  }

  Out o = {0};
  static const uint8_t magic[4] = {'S', 'V', 'M', 0};
  out_raw(&o, magic, 4);
  out_u8(&o, 10);                                  /* VERSION (v10) — v10 adds a per-offer
                                                    * impl-export policy byte, but we emit zero
                                                    * impl-exports, so the image is otherwise
                                                    * byte-identical to v9. */
  /* v9 flags byte: bit 0 marks the **object dialect** (a pre-link unit carrying `data.self`
   * link-form addresses, decoded by `decode_unit`). Emit it only when the module actually has a
   * `data.self` — otherwise the module is a plain runnable unit (unresolved `call.sym` imports
   * are legal in the runnable dialect), which `decode_module` also accepts. This lets the §22
   * `Jit` capability (`compile_linked` -> `decode_module`) consume a data-free guest module; a
   * module with own-data addresses still needs the object dialect (and `link` to resolve them). */
  out_u8(&o, has_data_self ? 0x01 : 0x00);
  /* Memory descriptor: presence flag, then size_log2. */
  if (m->has_memory) { out_u8(&o, 1); out_u8(&o, m->mem_log2); }
  else out_u8(&o, 0);
  /* Data segments: count, then each `readonly` flag (irb data is always `ro`), offset,
   * length-prefixed bytes. */
  out_uleb(&o, (uint64_t)m->ndata);
  for (int i = 0; i < m->ndata; i++) {
    out_u8(&o, 1);
    out_uleb(&o, m->data[i].off);
    out_uleb(&o, m->data[i].len);
    out_raw(&o, m->data[i].bytes, m->data[i].len);
  }
  /* Object-only `data.ptr` relocation section (v9), written only in the object dialect: none —
   * irb never embeds a raw pointer inside the data image (own-data addresses are `data.self`
   * instructions in code). Omitted for a runnable module, which has no such section. */
  if (has_data_self) out_uleb(&o, 0);
  /* Object-only `data.funcref` relocation section (v9), written next to `data.ptr` in the object
   * dialect: none — irb never embeds a data->code reference in the data image (funcrefs live in
   * code as `ref.func`, not as data-image relocations). Omitted for a runnable module. */
  if (has_data_self) out_uleb(&o, 0);
  /* Import section: name, shape tag (0=func) + type index, mode byte (0=required). */
  out_uleb(&o, (uint64_t)tab.nimports);
  for (int i = 0; i < tab.nimports; i++) {
    out_str_bin(&o, tab.imports[i].name);
    out_u8(&o, 0);
    out_uleb(&o, tab.imports[i].type_idx);
    out_u8(&o, 0);
  }
  /* Export section: none (irb emits no named entry points). */
  out_uleb(&o, 0);
  /* Object-only data-export section (v9), written only in the object dialect: none. */
  if (has_data_self) out_uleb(&o, 0);
  /* Type section: each entry tagged (0=func), then param + result type lists. */
  out_uleb(&o, (uint64_t)tab.ntypes);
  for (int i = 0; i < tab.ntypes; i++) {
    out_u8(&o, 0);
    out_types_bin(&o, tab.types[i].params, tab.types[i].np);
    out_types_bin(&o, tab.types[i].results, tab.types[i].nr);
  }
  /* Impl-export section: none. */
  out_uleb(&o, 0);
  /* Functions. */
  out_uleb(&o, (uint64_t)m->nfuncs);
  for (int i = 0; i < m->nfuncs; i++) enc_func(&o, m->funcs[i], &tab);
  /* No debug section: decode treats "no bytes after funcs" as no debug info. */

  for (int i = 0; i < tab.ntypes; i++) { free(tab.types[i].params); free(tab.types[i].results); }
  free(tab.types);
  free(tab.imports);

  if (!o.buf) { o.buf = xmalloc(1); }
  *out_len = o.len;
  return (uint8_t *)o.buf;
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
  for (int i = 0; i < m->ndata; i++) free(m->data[i].bytes);
  free(m->data);
  free(m->funcs);
  free(m);
}
