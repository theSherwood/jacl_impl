/* codegen.c — JACL AST → SVM-IR (P2.2–P2.4 scaffold). See codegen.h. */
#include "codegen.h"

#include "../src/jacl.h"   /* AstNode, AstNodeType, HeadId, JaclType */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JaclVal encodings (see runtime/jaclrt.h). */
#define JACL_TAG_I32_SHIFTED ((uint64_t)0x02 << 56)
#define JACLVAL_FALSE        ((int64_t)((uint64_t)0x01 << 56))   /* JACL_FALSE */
#define JACLVAL_NIL          ((int64_t)0)                        /* JACL_NIL   */
static int64_t jaclval_i32(int32_t x) {
  return (int64_t)(JACL_TAG_I32_SHIFTED | (uint64_t)(uint32_t)x);
}

/* Runtime function for a binary operator head, or NULL. Arithmetic folds for >2
 * args; comparisons are used binary (in conditions). */
static const char *binary_runtime_fn(uint8_t head_id) {
  switch (head_id) {
    case HEAD_PLUS:    return "jacl_add";
    case HEAD_MINUS:   return "jacl_sub";
    case HEAD_STAR:    return "jacl_mul";
    case HEAD_SLASH:   return "jacl_div";
    case HEAD_PERCENT: return "jacl_mod";
    case HEAD_LT:      return "jacl_lt";
    case HEAD_LE:      return "jacl_le";
    case HEAD_GT:      return "jacl_gt";
    case HEAD_GE:      return "jacl_ge";
    case HEAD_EQ_EQ:   return "jacl_eq";
    case HEAD_BANG_EQ: return "jacl_ne";
    default:           return NULL;
  }
}

/* ---- compile context: current block + lexical environment ----
 *
 * SVM is block-local SSA, so any value used in a block must be one of its params or
 * defined in it. Across control-flow edges we therefore thread a *frame* — the data-
 * stack pointer `sp` plus every live local — as block params: a branch passes the
 * current frame, and the target block rebinds `sp`/locals to its params. A `set`
 * inside a branch updates a local's SSA id in that block, so the merge block's param
 * for it naturally becomes the phi of the two edges. */
#define IRB_MAX_FRAME 256
#define CG_MAX_PARAMS 64

typedef struct {
  const char *name;
  uint32_t    len;
  IrVal       value;   /* SSA value, or (when is_cell) the cell pointer */
  int         is_mut;
  int         is_cell; /* a captured `mut`: backed by a heap cell, shared on capture */
} Binding;

/* A top-level proc: name -> its SVM function + arity. Registered before any body is
 * compiled, so (mutual) recursion resolves. */
typedef struct {
  const char *name;
  uint32_t    len;
  IrFunc     *func;
  int         arity;
  int         is_generator; /* body contains `yield` → a fiber generator (sp, arg) */
  AstNode    *def; /* the proc AST_COMMAND, recompiled in pass 2 */
} Proc;

/* A closure literal whose body is compiled after its creation site (a worklist, so
 * nested closures are handled). The closure function is `(i64 sp, i64 self, args…)`;
 * its body reads captures back from `self`. Names point into the (arena-lived) AST. */
typedef struct {
  IrFunc     *func;
  AstNode    *body;
  const char *pnames[CG_MAX_PARAMS]; uint32_t plens[CG_MAX_PARAMS]; int nparams;
  const char *cnames[CG_MAX_PARAMS]; uint32_t clens[CG_MAX_PARAMS]; int ccell[CG_MAX_PARAMS]; int ncaps;
} Pending;

typedef struct {
  char  *err;
  size_t errcap;
  int    failed;

  IrModule *m;  /* the module under construction (closures append functions) */
  IrFunc *f;
  IrBlock cur;  /* block currently being emitted into */
  IrVal   sp;   /* current value-id of the data-stack pointer in `cur` */

  Binding *locals; int nlocals, cap_locals; /* flat env (most-recent wins) */
  int     *marks;  int nmarks,  cap_marks;  /* scope start indices into locals */

  Proc *procs; int nprocs, cap_procs;       /* top-level proc table (persists) */
  Pending *pending; int npending, cap_pending; /* closure bodies to compile */

  /* Names captured by some closure in the function currently being compiled — a `mut`
   * among them is boxed in a heap cell so mutations are shared with the closure. */
  const char *capset[CG_MAX_PARAMS]; uint32_t capsetlen[CG_MAX_PARAMS]; int ncapset;
} Cx;

static void cx_fail(Cx *cx, const char *msg) {
  if (!cx->failed && cx->err && cx->errcap) snprintf(cx->err, cx->errcap, "codegen: %s", msg);
  cx->failed = 1;
}
static void cx_failf(Cx *cx, const char *fmt, const char *name, uint32_t len) {
  if (!cx->failed && cx->err && cx->errcap) snprintf(cx->err, cx->errcap, fmt, (int)len, name);
  cx->failed = 1;
}

static int name_eq(const Binding *bd, const char *name, uint32_t len) {
  return bd->len == len && memcmp(bd->name, name, len) == 0;
}

static void scope_enter(Cx *cx) {
  if (cx->nmarks == cx->cap_marks) {
    cx->cap_marks = cx->cap_marks ? cx->cap_marks * 2 : 8;
    cx->marks = realloc(cx->marks, (size_t)cx->cap_marks * sizeof(int));
    if (!cx->marks) { fprintf(stderr, "codegen: oom\n"); abort(); }
  }
  cx->marks[cx->nmarks++] = cx->nlocals;
}
static void scope_exit(Cx *cx) {
  cx->nlocals = cx->marks[--cx->nmarks];
}

static Binding *env_lookup(Cx *cx, const char *name, uint32_t len) {
  for (int i = cx->nlocals - 1; i >= 0; i--)
    if (name_eq(&cx->locals[i], name, len)) return &cx->locals[i];
  return NULL;
}

static void env_define(Cx *cx, const char *name, uint32_t len, IrVal value, int is_mut, int is_cell) {
  int mark = cx->nmarks ? cx->marks[cx->nmarks - 1] : 0;
  for (int i = mark; i < cx->nlocals; i++)
    if (name_eq(&cx->locals[i], name, len) && !cx->locals[i].is_mut) {
      cx_failf(cx, "codegen: variable '%.*s' already defined in this scope", name, len);
      return;
    }
  if (cx->nlocals == cx->cap_locals) {
    cx->cap_locals = cx->cap_locals ? cx->cap_locals * 2 : 8;
    cx->locals = realloc(cx->locals, (size_t)cx->cap_locals * sizeof(Binding));
    if (!cx->locals) { fprintf(stderr, "codegen: oom\n"); abort(); }
  }
  cx->locals[cx->nlocals++] = (Binding){name, len, value, is_mut, is_cell};
}

/* ---- top-level procs ---- */

/* Mirror of src/ast.c's type_keyword_table (kept local to avoid linking a static
 * frontend helper). Used only to skip type tokens when reading proc param names. */
static int cg_is_type_kw(const char *s, uint32_t n) {
  static const struct { const char *k; uint32_t n; } T[] = {
    {"i8", 2}, {"u8", 2}, {"i16", 3}, {"u16", 3}, {"i32", 3}, {"i64", 3},
    {"u32", 3}, {"u64", 3}, {"f32", 3}, {"f64", 3}, {"str", 3}, {"vec", 3},
    {"map", 3}, {"dyn", 3}, {"bool", 4}, {"stream", 6}};
  for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++)
    if (T[i].n == n && memcmp(T[i].k, s, n) == 0) return 1;
  return 0;
}

/* Read a proc's param NAMES from its params command (args[1]). Params are flattened
 * into the command's head + args; typed params interleave a type token before each
 * name (`{i32 x, i32 y}` -> i32 x i32 y), untyped are bare (`{x, y}` -> x y), and a
 * single empty-string token means zero params (`{}`). Returns the count, or -1 on
 * error. Types are skipped (codegen treats every value as an i64 JaclVal). */
static int extract_param_names(Cx *cx, AstNode *plist, const char **names, uint32_t *lens) {
  if (!plist || plist->type != AST_COMMAND) { cx_fail(cx, "proc params must be a { list }"); return -1; }
  AstNode *toks[CG_MAX_PARAMS * 2 + 2];
  int nt = 0;
  AstNode *h = plist->data.command.head;
  if (h && h->type == AST_LIT_STRING && h->data.lit_string.length == 0 &&
      plist->data.command.arg_count == 0)
    return 0; /* {} */
  if (h) toks[nt++] = h;
  for (uint32_t i = 0; i < plist->data.command.arg_count && nt < CG_MAX_PARAMS * 2; i++)
    toks[nt++] = plist->data.command.args[i];

  int np = 0, i = 0;
  while (i < nt) {
    if (toks[i]->type != AST_LIT_STRING) { cx_fail(cx, "proc param must be a name"); return -1; }
    const char *s = toks[i]->data.lit_string.value;
    uint32_t n = toks[i]->data.lit_string.length;
    AstNode *name_tok = toks[i];
    if (cg_is_type_kw(s, n) && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) {
      name_tok = toks[i + 1]; i += 2; /* `<type> <name>` */
    } else {
      i += 1;
    }
    if (np >= CG_MAX_PARAMS) { cx_fail(cx, "proc has too many params"); return -1; }
    names[np] = name_tok->data.lit_string.value;
    lens[np] = name_tok->data.lit_string.length;
    np++;
  }
  return np;
}

static Proc *proc_lookup(Cx *cx, const char *name, uint32_t len) {
  for (int i = 0; i < cx->nprocs; i++)
    if (cx->procs[i].len == len && memcmp(cx->procs[i].name, name, len) == 0) return &cx->procs[i];
  return NULL;
}

/* ---- frame threading helpers ---- */

static int frame_width(Cx *cx) { return 1 + cx->nlocals; } /* sp + locals */

/* Current carried value-ids: out[0]=sp, out[1..]=locals. */
static void fill_frame(Cx *cx, IrVal *out) {
  out[0] = cx->sp;
  for (int i = 0; i < cx->nlocals; i++) out[i + 1] = cx->locals[i].value;
}

/* A new block with `nparams` i64 params. */
static IrBlock new_i64_block(Cx *cx, int nparams) {
  IrType t[IRB_MAX_FRAME + 2];
  for (int i = 0; i < nparams; i++) t[i] = IRB_I64;
  return irb_block(cx->f, t, nparams);
}

/* Make `blk` current and rebind sp + locals to its first frame_width params. */
static void enter_frame_block(Cx *cx, IrBlock blk) {
  cx->cur = blk;
  cx->sp = 0;
  for (int i = 0; i < cx->nlocals; i++) cx->locals[i].value = (IrVal)(i + 1);
}

/* ---- expression / statement lowering ---- */

static IrVal compile_expr(Cx *cx, AstNode *node);

/* Emit a `call.import` to a runtime function with all-i64 args (args[0] is sp) and a
 * single i64 result. */
static IrVal emit_rt_call(Cx *cx, const char *fn, const IrVal *args, int nargs) {
  IrType sig[2 + CG_MAX_PARAMS];
  for (int i = 0; i < nargs; i++) sig[i] = IRB_I64;
  IrType r1[] = {IRB_I64};
  IrVal handle = irb_const_i32(cx->f, cx->cur, 0);
  return irb_call_import(cx->f, cx->cur, fn, sig, nargs, r1, 1, handle, args, nargs);
}

/* Emit `fn(sp, lhs, rhs)` — a binary runtime call returning an i64 JaclVal. */
static IrVal emit_binop_call(Cx *cx, const char *fn, IrVal lhs, IrVal rhs) {
  IrVal args[] = {cx->sp, lhs, rhs};
  return emit_rt_call(cx, fn, args, 3);
}

/* Emit a `void`-returning runtime call `fn(sp)` (e.g. an init routine). */
static void emit_void_call(Cx *cx, const char *fn) {
  IrType sig[] = {IRB_I64};
  IrVal h = irb_const_i32(cx->f, cx->cur, 0);
  IrVal args[] = {cx->sp};
  (void)irb_call_import(cx->f, cx->cur, fn, sig, 1, NULL, 0, h, args, 1);
}

/* ---- type-driven (unboxed) i32 arithmetic ----
 *
 * Where the typer proved an arithmetic expression is i32, compute it with native i32
 * ops instead of dynamic `jacl_*` calls. Values still flow as boxed JaclVals across
 * locals/calls/blocks; unboxing happens only *within* a typed arithmetic tree, with a
 * single box at the root — so the env/frame stay all-i64 and no rep tracking leaks out.
 * (Taint/secret flag propagation is dropped on the unboxed path; statically-typed pure
 * arithmetic is the intended trade.) */
static IrVal box_i32(Cx *cx, IrVal v) {
  IrVal ext = irb_convert(cx->f, cx->cur, IRB_EXTEND_I32U, v);
  IrVal tag = irb_const_i64(cx->f, cx->cur, (int64_t)JACL_TAG_I32_SHIFTED);
  return irb_intbin(cx->f, cx->cur, IRB_I64, IRB_OR, ext, tag);
}
static IrVal unbox_i32(Cx *cx, IrVal boxed) {
  return irb_convert(cx->f, cx->cur, IRB_WRAP_I64, boxed);
}

/* True when `[op …]` can be lowered to native i32 arithmetic (typer proved i32). */
static int i32_arith(AstNode *node) {
  if (node->type != AST_COMMAND || node->inferred_type != TYPE_I32) return 0;
  uint8_t hid = node->data.command.head_id;
  return hid == HEAD_PLUS || hid == HEAD_MINUS || hid == HEAD_STAR;
}

/* Lower `node` to a native i32 value. Typed `+ - *` stay unboxed (recursively);
 * literals are i32 constants; anything else is computed boxed then unboxed. */
static IrVal compile_i32(Cx *cx, AstNode *node) {
  if (cx->failed) return 0;
  if (node->type == AST_LIT_INT) return irb_const_i32(cx->f, cx->cur, node->data.lit_int.value);
  if (i32_arith(node)) {
    uint8_t hid = node->data.command.head_id;
    IrBinOp op = hid == HEAD_PLUS ? IRB_ADD : hid == HEAD_MINUS ? IRB_SUB : IRB_MUL;
    uint32_t argc = node->data.command.arg_count;
    IrVal acc = compile_i32(cx, node->data.command.args[0]);
    for (uint32_t i = 1; i < argc; i++) {
      IrVal r = compile_i32(cx, node->data.command.args[i]);
      if (cx->failed) return 0;
      acc = irb_intbin(cx->f, cx->cur, IRB_I32, op, acc, r);
    }
    return acc;
  }
  return unbox_i32(cx, compile_expr(cx, node)); /* var-refs, calls, dyn ops, … */
}

/* ---- strings & collections (P2.8) ---- */

/* Build a heap string from a literal: its bytes go in a data segment, and
 * jacl_str_new(ptr, len) copies them. `ptr` is a relocated data address; `len` is i32. */
static IrVal compile_string_literal(Cx *cx, const char *s, uint32_t len) {
  uint64_t off = irb_add_data(cx->m, s, len);
  IrVal addr = irb_data_addr(cx->f, cx->cur, off);
  IrVal lv = irb_const_i32(cx->f, cx->cur, (int32_t)len);
  IrType sig[] = {IRB_I64, IRB_I64, IRB_I32};
  IrType r1[] = {IRB_I64};
  IrVal h = irb_const_i32(cx->f, cx->cur, 0);
  IrVal args[] = {cx->sp, addr, lv};
  return irb_call_import(cx->f, cx->cur, "jacl_str_new", sig, 3, r1, 1, h, args, 3);
}

/* One segment of an interpolated string: a literal → a string; an expression →
 * jacl_to_string of its value. */
static IrVal compile_str_segment(Cx *cx, AstNode *seg) {
  if (seg->type == AST_LIT_STRING)
    return compile_string_literal(cx, seg->data.lit_string.value, seg->data.lit_string.length);
  IrVal v = compile_expr(cx, seg);
  IrVal a[] = {cx->sp, v};
  return emit_rt_call(cx, "jacl_to_string", a, 2);
}

/* Reduce a JaclVal condition to an i32 truth (1/0): truthy unless nil or false. */
static IrVal emit_truthy(Cx *cx, IrVal cond) {
  IrVal cfalse = irb_const_i64(cx->f, cx->cur, JACLVAL_FALSE);
  IrVal cnil = irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
  IrVal ne_false = irb_intcmp(cx->f, cx->cur, IRB_I64, IRB_NE, cond, cfalse);
  IrVal ne_nil = irb_intcmp(cx->f, cx->cur, IRB_I64, IRB_NE, cond, cnil);
  return irb_intbin(cx->f, cx->cur, IRB_I32, IRB_AND, ne_false, ne_nil);
}

static int frame_guard(Cx *cx) {
  if (frame_width(cx) > IRB_MAX_FRAME) { cx_fail(cx, "too many live locals for control flow"); return 0; }
  return 1;
}

static int kw_is(AstNode *n, const char *kw, uint32_t kl) {
  return n->type == AST_LIT_STRING && n->data.lit_string.length == kl &&
         memcmp(n->data.lit_string.value, kw, kl) == 0;
}

/* ---- closures ---- */

#define CG_CAP_MAX CG_MAX_PARAMS

static int set_has(const char **ns, const uint32_t *ls, int n, const char *s, uint32_t l) {
  for (int i = 0; i < n; i++) if (ls[i] == l && memcmp(ns[i], s, l) == 0) return 1;
  return 0;
}
static void set_add(const char **ns, uint32_t *ls, int *n, const char *s, uint32_t l) {
  if (set_has(ns, ls, *n, s, l)) return;
  if (*n < CG_CAP_MAX) { ns[*n] = s; ls[*n] = l; (*n)++; }
}

/* Names bound *inside* a closure body (def/mut/set targets + for loop vars), collected
 * across the whole body, so the capture scan excludes them. Conservative w.r.t. inner
 * shadowing (rare). */
static void collect_bound(AstNode *node, const char **ns, uint32_t *ls, int *n) {
  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) collect_bound(node->data.block.commands[i], ns, ls, n);
  } else if (node->type == AST_RETURN) {
    if (node->data.return_stmt.value) collect_bound(node->data.return_stmt.value, ns, ls, n);
  } else if (node->type == AST_COMMAND) {
    uint8_t hid = node->data.command.head_id;
    AstNode **args = node->data.command.args; uint32_t argc = node->data.command.arg_count;
    /* def/mut/for introduce bindings; `set` only mutates an existing one (so it does
     * not make the name closure-local — a `set` on a captured var still captures). */
    if ((hid == HEAD_DEF || hid == HEAD_MUT || hid == HEAD_FOR) &&
        argc >= 1 && args[0]->type == AST_LIT_STRING)
      set_add(ns, ls, n, args[0]->data.lit_string.value, args[0]->data.lit_string.length);
    for (uint32_t i = 0; i < argc; i++) collect_bound(args[i], ns, ls, n);
  }
}

/* Free variables: var-refs in the body not bound locally but present in the enclosing
 * env (cx) — these become the closure's captured upvalues. Descends into nested
 * closures so a transitively-needed outer var is captured here too. */
static void collect_caps(Cx *cx, AstNode *node, const char **bound, uint32_t *blen, int nb,
                         const char **caps, uint32_t *clen, int *nc) {
  if (node->type == AST_VAR_REF) {
    const char *nm = node->data.var_ref.name; uint32_t nl = node->data.var_ref.length;
    if (!set_has(bound, blen, nb, nm, nl) && env_lookup(cx, nm, nl)) set_add(caps, clen, nc, nm, nl);
  } else if (node->type == AST_RETURN) {
    if (node->data.return_stmt.value) collect_caps(cx, node->data.return_stmt.value, bound, blen, nb, caps, clen, nc);
  } else if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) collect_caps(cx, node->data.block.commands[i], bound, blen, nb, caps, clen, nc);
  } else if (node->type == AST_COMMAND) {
    /* a `set NAME …` target is a reference (by name), so it captures too */
    if (node->data.command.head_id == HEAD_SET && node->data.command.arg_count >= 1 &&
        node->data.command.args[0]->type == AST_LIT_STRING) {
      const char *nm = node->data.command.args[0]->data.lit_string.value;
      uint32_t nl = node->data.command.args[0]->data.lit_string.length;
      if (!set_has(bound, blen, nb, nm, nl) && env_lookup(cx, nm, nl)) set_add(caps, clen, nc, nm, nl);
    }
    if (node->data.command.head) collect_caps(cx, node->data.command.head, bound, blen, nb, caps, clen, nc);
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) collect_caps(cx, node->data.command.args[i], bound, blen, nb, caps, clen, nc);
  }
}

/* Is `node` a closure literal? Lambda `[\ {params} {body}]` (params in a BLOCK) or
 * anonymous `[proc {params} {body}]` (empty name). Fills the out-params if so. */
static int closure_literal(AstNode *node, AstNode **pnode, int *in_block, AstNode **body) {
  if (node->type != AST_COMMAND) return 0;
  AstNode *h = node->data.command.head;
  uint32_t argc = node->data.command.arg_count;
  if (h && h->type == AST_LIT_STRING && h->data.lit_string.length == 1 && h->data.lit_string.value[0] == '\\') {
    if (argc == 2 && node->data.command.args[0]->type == AST_BLOCK && node->data.command.args[1]->type == AST_BLOCK) {
      *pnode = node->data.command.args[0]; *in_block = 1; *body = node->data.command.args[1]; return 1;
    }
    return 0;
  }
  if (node->data.command.head_id == HEAD_PROC && argc >= 3 &&
      node->data.command.args[0]->type == AST_LIT_STRING && node->data.command.args[0]->data.lit_string.length == 0) {
    *pnode = node->data.command.args[1]; *in_block = 0; *body = node->data.command.args[argc - 1]; return 1;
  }
  return 0;
}

/* Extract closure param names (tolerant — returns the count, 0 on malformed). Lambda
 * params arrive in a BLOCK (`[\ {x} …]`), anon proc params in a command
 * (`[proc {x} …]`); both flatten to type/name tokens (types skipped). */
static int closure_param_names(AstNode *pnode, int in_block, const char **ns, uint32_t *ls) {
  AstNode *toks[CG_MAX_PARAMS * 2 + 4]; int nt = 0;
  if (!pnode) return 0; /* a parameterless closure (e.g. a spawn block) */
  if (in_block) {
    if (pnode->type != AST_BLOCK) return 0;
    for (uint32_t i = 0; i < pnode->data.block.count; i++) {
      AstNode *cmd = pnode->data.block.commands[i];
      if (cmd->type != AST_COMMAND) return 0;
      if (cmd->data.command.head && nt < CG_MAX_PARAMS * 2) toks[nt++] = cmd->data.command.head;
      for (uint32_t j = 0; j < cmd->data.command.arg_count && nt < CG_MAX_PARAMS * 2; j++) toks[nt++] = cmd->data.command.args[j];
    }
  } else {
    if (pnode->type != AST_COMMAND) return 0;
    if (pnode->data.command.head) toks[nt++] = pnode->data.command.head;
    for (uint32_t j = 0; j < pnode->data.command.arg_count && nt < CG_MAX_PARAMS * 2; j++) toks[nt++] = pnode->data.command.args[j];
  }
  if (nt == 0) return 0;
  if (nt == 1 && toks[0]->type == AST_LIT_STRING && toks[0]->data.lit_string.length == 0) return 0;
  int np = 0, i = 0;
  while (i < nt && np < CG_MAX_PARAMS) {
    if (toks[i]->type != AST_LIT_STRING) return np;
    const char *s = toks[i]->data.lit_string.value; uint32_t n = toks[i]->data.lit_string.length;
    AstNode *nmtok = toks[i];
    if (cg_is_type_kw(s, n) && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) { nmtok = toks[i + 1]; i += 2; }
    else i += 1;
    ns[np] = nmtok->data.lit_string.value; ls[np] = nmtok->data.lit_string.length; np++;
  }
  return np;
}

/* Syntactic free var-refs of `node` (names referenced but not in `bound`) added to a set. */
static void add_free_varrefs(AstNode *node, const char **bound, uint32_t *blen, int nb,
                             const char **set, uint32_t *slen, int *sn) {
  if (node->type == AST_VAR_REF) {
    const char *nm = node->data.var_ref.name; uint32_t nl = node->data.var_ref.length;
    if (!set_has(bound, blen, nb, nm, nl)) set_add(set, slen, sn, nm, nl);
  } else if (node->type == AST_RETURN) {
    if (node->data.return_stmt.value) add_free_varrefs(node->data.return_stmt.value, bound, blen, nb, set, slen, sn);
  } else if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) add_free_varrefs(node->data.block.commands[i], bound, blen, nb, set, slen, sn);
  } else if (node->type == AST_COMMAND) {
    if (node->data.command.head_id == HEAD_SET && node->data.command.arg_count >= 1 &&
        node->data.command.args[0]->type == AST_LIT_STRING) {
      const char *nm = node->data.command.args[0]->data.lit_string.value;
      uint32_t nl = node->data.command.args[0]->data.lit_string.length;
      if (!set_has(bound, blen, nb, nm, nl)) set_add(set, slen, sn, nm, nl);
    }
    if (node->data.command.head) add_free_varrefs(node->data.command.head, bound, blen, nb, set, slen, sn);
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) add_free_varrefs(node->data.command.args[i], bound, blen, nb, set, slen, sn);
  }
}

/* Build the "captured names" set for the function whose body region is `node`: the free
 * variables of every closure literal directly within it (not descending into a closure,
 * whose own captures are computed when it is compiled). A `mut` among these is boxed. */
static void capset_scan(Cx *cx, AstNode *node) {
  AstNode *pnode, *body; int in_block;
  if (closure_literal(node, &pnode, &in_block, &body)) {
    const char *pn[CG_MAX_PARAMS]; uint32_t pl[CG_MAX_PARAMS];
    int np = closure_param_names(pnode, in_block, pn, pl);
    const char *bound[CG_CAP_MAX]; uint32_t blen[CG_CAP_MAX]; int nb = 0;
    for (int k = 0; k < np; k++) set_add(bound, blen, &nb, pn[k], pl[k]);
    collect_bound(body, bound, blen, &nb);
    add_free_varrefs(body, bound, blen, nb, cx->capset, cx->capsetlen, &cx->ncapset);
    return; /* don't descend into the closure */
  }
  if (node->type == AST_RETURN) { if (node->data.return_stmt.value) capset_scan(cx, node->data.return_stmt.value); return; }
  if (node->type == AST_BLOCK) { for (uint32_t i = 0; i < node->data.block.count; i++) capset_scan(cx, node->data.block.commands[i]); return; }
  if (node->type == AST_COMMAND) {
    if (node->data.command.head) capset_scan(cx, node->data.command.head);
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) capset_scan(cx, node->data.command.args[i]);
  }
}

static void capset_reset(Cx *cx) { cx->ncapset = 0; }
static int is_captured_name(Cx *cx, const char *s, uint32_t l) {
  return set_has(cx->capset, cx->capsetlen, cx->ncapset, s, l);
}

static void pending_add(Cx *cx, IrFunc *func, AstNode *body,
                        const char **pn, uint32_t *pl, int np,
                        const char **cn, uint32_t *cl, const int *cc, int nc) {
  if (cx->npending == cx->cap_pending) {
    cx->cap_pending = cx->cap_pending ? cx->cap_pending * 2 : 8;
    cx->pending = realloc(cx->pending, (size_t)cx->cap_pending * sizeof(Pending));
    if (!cx->pending) { fprintf(stderr, "codegen: oom\n"); abort(); }
  }
  Pending *p = &cx->pending[cx->npending++];
  p->func = func; p->body = body; p->nparams = np; p->ncaps = nc;
  for (int i = 0; i < np; i++) { p->pnames[i] = pn[i]; p->plens[i] = pl[i]; }
  for (int i = 0; i < nc; i++) { p->cnames[i] = cn[i]; p->clens[i] = cl[i]; p->ccell[i] = cc[i]; }
}

/* Compile a closure literal: build the closure object (ref.func + captured values) at
 * the current site, and enqueue its body for deferred compilation. `pnode` holds the
 * params (a BLOCK for `\`, a command for `proc`). Returns the closure JaclVal. */
static IrVal compile_closure(Cx *cx, AstNode *pnode, int in_block, AstNode *body) {
  const char *pn[CG_MAX_PARAMS]; uint32_t pl[CG_MAX_PARAMS];
  int np = closure_param_names(pnode, in_block, pn, pl);

  const char *bound[CG_CAP_MAX]; uint32_t blen[CG_CAP_MAX]; int nb = 0;
  for (int k = 0; k < np; k++) set_add(bound, blen, &nb, pn[k], pl[k]);
  collect_bound(body, bound, blen, &nb);
  const char *cn[CG_CAP_MAX]; uint32_t cl[CG_CAP_MAX]; int nc = 0;
  collect_caps(cx, body, bound, blen, nb, cn, cl, &nc);

  /* closure function: (i64 sp, i64 self, params…) -> i64 */
  IrType pt[2 + CG_MAX_PARAMS];
  pt[0] = IRB_I64; pt[1] = IRB_I64;
  for (int k = 0; k < np; k++) pt[2 + k] = IRB_I64;
  IrType r1[] = {IRB_I64};
  IrFunc *fn = irb_func_new(cx->m, pt, 2 + np, r1, 1);

  /* build the closure object at the creation site, capturing current values; a captured
   * `mut` is a cell, so we capture the shared cell pointer (mutations stay visible). */
  IrVal fnref = irb_ref_func(cx->f, cx->cur, fn);
  IrVal fnref64 = irb_convert(cx->f, cx->cur, IRB_EXTEND_I32U, fnref);
  IrVal nupv = irb_const_i64(cx->f, cx->cur, nc);
  IrVal new_args[] = {cx->sp, fnref64, nupv};
  IrVal clos = emit_rt_call(cx, "jacl_closure_new", new_args, 3);
  int ccell[CG_CAP_MAX];
  for (int i = 0; i < nc; i++) {
    Binding *b = env_lookup(cx, cn[i], cl[i]);
    if (!b) { cx_failf(cx, "codegen: cannot capture undefined '%.*s'", cn[i], cl[i]); return 0; }
    if (b->is_mut && !b->is_cell) { cx_fail(cx, "internal: captured mutable not boxed"); return 0; }
    ccell[i] = b->is_cell;
    IrVal idx = irb_const_i64(cx->f, cx->cur, i);
    IrVal set_args[] = {cx->sp, clos, idx, b->value}; /* cell pointer or by-value */
    (void)emit_rt_call(cx, "jacl_closure_set", set_args, 4);
  }
  pending_add(cx, fn, body, pn, pl, np, cn, cl, ccell, nc);
  return clos;
}

/* Compile one if/elif clause whose condition is args[start] and then-block is
 * args[start+1], with the remaining args[start+2..] describing the else part:
 *   (nothing)            -> nil
 *   { block }            -> else block (braces-only else)
 *   "else" { block }     -> else block
 *   "elif" cond { ... }  -> a nested if (recurse). Mirrors the flat parser shape.
 * Emits an SSA diamond joining on the frame + the clause's result value. */
static IrVal compile_if_chain(Cx *cx, AstNode **args, uint32_t argc, uint32_t start) {
  if (start + 1 >= argc || args[start + 1]->type != AST_BLOCK) {
    cx_fail(cx, "if/elif needs a condition and a { then-block }"); return 0;
  }
  if (!frame_guard(cx)) return 0;

  IrVal cond = compile_expr(cx, args[start]);
  if (cx->failed) return 0;
  IrVal truth = emit_truthy(cx, cond);

  int w = frame_width(cx);
  IrBlock then_blk = new_i64_block(cx, w);
  IrBlock else_blk = new_i64_block(cx, w);
  IrBlock join_blk = new_i64_block(cx, w + 1); /* +1: the clause's result */

  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br_if(cx->f, cx->cur, truth, then_blk, frame, w, else_blk, frame, w);

  /* then */
  enter_frame_block(cx, then_blk);
  IrVal then_res = compile_expr(cx, args[start + 1]); /* AST_BLOCK opens its own scope */
  if (cx->failed) return 0;
  IrVal targs[IRB_MAX_FRAME + 2];
  fill_frame(cx, targs); targs[w] = then_res;
  irb_br(cx->f, cx->cur, join_blk, targs, w + 1);

  /* else */
  enter_frame_block(cx, else_blk);
  IrVal else_res;
  uint32_t next = start + 2;
  if (next >= argc) {
    else_res = irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
  } else if (kw_is(args[next], "elif", 4)) {
    else_res = compile_if_chain(cx, args, argc, next + 1);
  } else if (kw_is(args[next], "else", 4)) {
    if (next + 1 >= argc || args[next + 1]->type != AST_BLOCK) { cx_fail(cx, "else needs a { block }"); return 0; }
    else_res = compile_expr(cx, args[next + 1]);
  } else if (args[next]->type == AST_BLOCK) {
    else_res = compile_expr(cx, args[next]);
  } else {
    cx_fail(cx, "malformed if (expected elif/else or an else block)"); return 0;
  }
  if (cx->failed) return 0;
  IrVal eargs[IRB_MAX_FRAME + 2];
  fill_frame(cx, eargs); eargs[w] = else_res;
  irb_br(cx->f, cx->cur, join_blk, eargs, w + 1);

  /* join: continue here; the result is the extra param at index w */
  enter_frame_block(cx, join_blk);
  return (IrVal)w;
}

static IrVal compile_if(Cx *cx, AstNode *node) {
  return compile_if_chain(cx, node->data.command.args, node->data.command.arg_count, 0);
}

/* [while COND { BODY }] — header carries the frame and merges the preheader edge with
 * the body back-edge; the loop value is nil. */
static IrVal compile_while(Cx *cx, AstNode *node) {
  uint32_t argc = node->data.command.arg_count;
  AstNode **args = node->data.command.args;
  if (argc != 2 || args[1]->type != AST_BLOCK) { cx_fail(cx, "while needs a condition and a { body }"); return 0; }
  if (!frame_guard(cx)) return 0;

  int w = frame_width(cx);
  IrBlock header = new_i64_block(cx, w);
  IrBlock body = new_i64_block(cx, w);
  IrBlock exit_blk = new_i64_block(cx, w);

  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  /* header: test the condition */
  enter_frame_block(cx, header);
  IrVal cond = compile_expr(cx, args[0]);
  if (cx->failed) return 0;
  IrVal truth = emit_truthy(cx, cond);
  fill_frame(cx, frame);
  irb_br_if(cx->f, cx->cur, truth, body, frame, w, exit_blk, frame, w);

  /* body: run, then loop back with the updated frame */
  enter_frame_block(cx, body);
  (void)compile_expr(cx, args[1]); /* body value discarded */
  if (cx->failed) return 0;
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  /* exit: continue here */
  enter_frame_block(cx, exit_blk);
  return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
}

/* Hidden, un-nameable frame slot holding a for-loop's (loop-invariant) end bound.
 * The leading control byte can't appear in a `$ident` var-ref, so it never collides. */
#define FOR_END_SENTINEL "\x01""for-end"
#define FOR_END_SENTINEL_LEN 8
#define FOR_GEN_SENTINEL "\x01""for-gen"
#define FOR_GEN_SENTINEL_LEN 8

/* [for GEN_CALL name { body }] — drive a generator (a fiber): resume it each iteration,
 * bind `name` to the yielded value, stop when it returns (done). */
static IrVal compile_for_generator(Cx *cx, AstNode *gen_call, const char *name, uint32_t nlen, AstNode *body) {
  scope_enter(cx);
  IrVal gen = compile_expr(cx, gen_call); /* jacl_gen_new(...) — the generator object */
  if (cx->failed) { scope_exit(cx); return 0; }
  env_define(cx, FOR_GEN_SENTINEL, FOR_GEN_SENTINEL_LEN, gen, /*is_mut=*/0, /*is_cell=*/0);
  env_define(cx, name, nlen, gen, /*is_mut=*/1, /*is_cell=*/0); /* loop var, set each iter */
  if (cx->failed) { scope_exit(cx); return 0; }
  if (!frame_guard(cx)) { scope_exit(cx); return 0; }

  int w = frame_width(cx);
  IrBlock header = new_i64_block(cx, w);
  IrBlock body_blk = new_i64_block(cx, w);
  IrBlock exit_blk = new_i64_block(cx, w);

  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  /* header: v = gen_next(g); name = v; if gen_done(g) -> exit else body */
  enter_frame_block(cx, header);
  {
    Binding *bg = env_lookup(cx, FOR_GEN_SENTINEL, FOR_GEN_SENTINEL_LEN);
    IrVal na[] = {cx->sp, bg->value};
    IrVal v = emit_rt_call(cx, "jacl_gen_next", na, 2);
    env_lookup(cx, name, nlen)->value = v;
    IrVal da[] = {cx->sp, bg->value};
    IrVal d = emit_rt_call(cx, "jacl_gen_done", da, 2);
    IrVal truth = emit_truthy(cx, d);
    fill_frame(cx, frame);
    irb_br_if(cx->f, cx->cur, truth, exit_blk, frame, w, body_blk, frame, w);
  }

  /* body: run with $name bound, loop back */
  enter_frame_block(cx, body_blk);
  (void)compile_expr(cx, body);
  if (cx->failed) { scope_exit(cx); return 0; }
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  enter_frame_block(cx, exit_blk);
  scope_exit(cx);
  return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
}

/* [for NAME in RANGE { body }] over an integer range, desugared onto the loop
 * machinery: `i = start; while i < end { body; i = i + 1 }`. The induction var (bound
 * to NAME) and the end bound are threaded as locals, so the generic frame machinery
 * carries them across the header/body/exit edges. */
static IrVal compile_for_range(Cx *cx, const char *name, uint32_t nlen,
                               AstNode *start_node, AstNode *end_node, AstNode *body) {
  scope_enter(cx); /* loop scope: holds the induction var + end bound */
  IrVal startv = compile_expr(cx, start_node);
  IrVal endv = compile_expr(cx, end_node);
  if (cx->failed) { scope_exit(cx); return 0; }
  env_define(cx, name, nlen, startv, /*is_mut=*/1, /*is_cell=*/0);
  env_define(cx, FOR_END_SENTINEL, FOR_END_SENTINEL_LEN, endv, /*is_mut=*/0, /*is_cell=*/0);
  if (cx->failed) { scope_exit(cx); return 0; }
  if (!frame_guard(cx)) { scope_exit(cx); return 0; }

  int w = frame_width(cx);
  IrBlock header = new_i64_block(cx, w);
  IrBlock body_blk = new_i64_block(cx, w);
  IrBlock exit_blk = new_i64_block(cx, w);

  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  /* header: i < end ? */
  enter_frame_block(cx, header);
  {
    Binding *bi = env_lookup(cx, name, nlen);
    Binding *be = env_lookup(cx, FOR_END_SENTINEL, FOR_END_SENTINEL_LEN);
    IrVal cond = emit_binop_call(cx, "jacl_lt", bi->value, be->value);
    IrVal truth = emit_truthy(cx, cond);
    fill_frame(cx, frame);
    irb_br_if(cx->f, cx->cur, truth, body_blk, frame, w, exit_blk, frame, w);
  }

  /* body: run, then i = i + 1, loop back */
  enter_frame_block(cx, body_blk);
  (void)compile_expr(cx, body); /* body value discarded; may read NAME */
  if (cx->failed) { scope_exit(cx); return 0; }
  {
    Binding *bi = env_lookup(cx, name, nlen);
    IrVal one = irb_const_i64(cx->f, cx->cur, jaclval_i32(1));
    bi->value = emit_binop_call(cx, "jacl_add", bi->value, one);
    fill_frame(cx, frame);
    irb_br(cx->f, cx->cur, header, frame, w);
  }

  /* exit: rebind, then drop the loop scope; for evaluates to nil */
  enter_frame_block(cx, exit_blk);
  scope_exit(cx);
  return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
}

/* [for NAME in <range> { body }] — scaffold supports integer ranges only:
 * `in [range A B]` / `in [range-inclusive A B]` is rejected for now, and the flat
 * `A .. B` form. General iteration over collections (the stream protocol) is later. */
static IrVal compile_for(Cx *cx, AstNode *node) {
  uint32_t argc = node->data.command.arg_count;
  AstNode **args = node->data.command.args;
  /* Canonical `for COLLECTION name { body }` over a generator (collection is a call to
   * a generator proc). Other canonical collections are reconciled in Phase 4. */
  if (argc == 3 && args[1]->type == AST_LIT_STRING && args[2]->type == AST_BLOCK &&
      args[0]->type == AST_COMMAND && args[0]->data.command.head &&
      args[0]->data.command.head->type == AST_LIT_STRING) {
    Proc *p = proc_lookup(cx, args[0]->data.command.head->data.lit_string.value,
                          args[0]->data.command.head->data.lit_string.length);
    if (p && p->is_generator)
      return compile_for_generator(cx, args[0], args[1]->data.lit_string.value,
                                   args[1]->data.lit_string.length, args[2]);
  }
  if (argc >= 4 && args[0]->type == AST_LIT_STRING && kw_is(args[1], "in", 2)) {
    const char *name = args[0]->data.lit_string.value;
    uint32_t nlen = args[0]->data.lit_string.length;
    /* `in [range A B] { body }` */
    if (argc == 4 && args[2]->type == AST_COMMAND &&
        args[2]->data.command.head_id == HEAD_RANGE &&
        args[2]->data.command.arg_count == 2 && args[3]->type == AST_BLOCK) {
      return compile_for_range(cx, name, nlen, args[2]->data.command.args[0],
                               args[2]->data.command.args[1], args[3]);
    }
    /* `in A .. B { body }` */
    if (argc == 6 && kw_is(args[3], "..", 2) && args[5]->type == AST_BLOCK) {
      return compile_for_range(cx, name, nlen, args[2], args[4], args[5]);
    }
  }
  cx_fail(cx, "for: scaffold supports `for NAME in A..B { body }` (integer ranges) only");
  return 0;
}

/* The binding name of a def/mut/set: args[0] must be a bare word (AST_LIT_STRING). */
static int binding_name(Cx *cx, AstNode *cmd, const char **name, uint32_t *len) {
  if (cmd->data.command.arg_count < 2 || cmd->data.command.args[0]->type != AST_LIT_STRING) {
    cx_fail(cx, "binding form needs a name and a value");
    return 0;
  }
  *name = cmd->data.command.args[0]->data.lit_string.value;
  *len = cmd->data.command.args[0]->data.lit_string.length;
  return 1;
}

/* Lower one expression to an i64 SSA value (a JaclVal) in the current block. */
static IrVal compile_expr(Cx *cx, AstNode *node) {
  if (cx->failed) return 0;
  switch (node->type) {
    case AST_LIT_INT:
      return irb_const_i64(cx->f, cx->cur, jaclval_i32(node->data.lit_int.value));

    case AST_VAR_REF: {
      Binding *bd = env_lookup(cx, node->data.var_ref.name, node->data.var_ref.length);
      if (!bd) { cx_failf(cx, "codegen: undefined variable '%.*s'",
                          node->data.var_ref.name, node->data.var_ref.length); return 0; }
      if (bd->is_cell) { IrVal a[] = {cx->sp, bd->value}; return emit_rt_call(cx, "jacl_cell_get", a, 2); }
      return bd->value;
    }

    case AST_RETURN:
      /* Tail-position return: evaluate to the value; the proc wraps the body value in
       * a single `return`. Early/mid-block return (out of loops) is not yet supported. */
      if (node->data.return_stmt.value) return compile_expr(cx, node->data.return_stmt.value);
      return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);

    case AST_LIT_STRING:
      return compile_string_literal(cx, node->data.lit_string.value, node->data.lit_string.length);

    case AST_INTERP_STRING: {
      uint32_t n = node->data.interp_string.count;
      if (n == 0) return compile_string_literal(cx, "", 0);
      IrVal acc = compile_str_segment(cx, node->data.interp_string.segments[0]);
      for (uint32_t i = 1; i < n; i++) {
        IrVal s = compile_str_segment(cx, node->data.interp_string.segments[i]);
        if (cx->failed) return 0;
        acc = emit_binop_call(cx, "jacl_str_concat", acc, s);
      }
      return acc;
    }

    case AST_BLOCK: {
      uint32_t bn = node->data.block.count;
      scope_enter(cx);
      IrVal last = (bn == 0) ? irb_const_i64(cx->f, cx->cur, jaclval_i32(0)) : 0;
      for (uint32_t i = 0; i < bn; i++) {
        last = compile_expr(cx, node->data.block.commands[i]);
        if (cx->failed) break;
      }
      scope_exit(cx);
      return last;
    }

    case AST_COMMAND: {
      uint8_t hid = node->data.command.head_id;

      if (hid == HEAD_IF) return compile_if(cx, node);
      if (hid == HEAD_WHILE) return compile_while(cx, node);
      if (hid == HEAD_FOR) return compile_for(cx, node);

      /* `yield V` — suspend the current fiber, yielding V; result is the resume arg. */
      if (hid == HEAD_YIELD) {
        IrVal v = (node->data.command.arg_count >= 1)
                      ? compile_expr(cx, node->data.command.args[0])
                      : irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
        if (cx->failed) return 0;
        return irb_suspend(cx->f, cx->cur, v);
      }

      /* `spawn { block }` — the block is a 0-param closure (capturing free vars) run on
       * a task fiber; returns a future. */
      if (hid == HEAD_SPAWN) {
        if (node->data.command.arg_count != 1 || node->data.command.args[0]->type != AST_BLOCK) {
          cx_fail(cx, "spawn needs a single { block }"); return 0;
        }
        IrVal clos = compile_closure(cx, NULL, 0, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, clos};
        return emit_rt_call(cx, "jacl_spawn", a, 2);
      }
      /* `await $f` — drive the future to completion, yielding its value. */
      if (hid == HEAD_AWAIT) {
        if (node->data.command.arg_count != 1) { cx_fail(cx, "await needs one argument"); return 0; }
        IrVal f = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, f};
        return emit_rt_call(cx, "jacl_await", a, 2);
      }

      /* Closure literals. Lambda `[\ {params} {body}]`: head is the bare word "\",
       * params in a BLOCK. Anonymous proc `[proc {params} {body}]`: an empty name. */
      {
        AstNode *h = node->data.command.head;
        uint32_t argc = node->data.command.arg_count;
        if (h && h->type == AST_LIT_STRING && h->data.lit_string.length == 1 &&
            h->data.lit_string.value[0] == '\\') {
          if (argc != 2 || node->data.command.args[0]->type != AST_BLOCK ||
              node->data.command.args[1]->type != AST_BLOCK) {
            cx_fail(cx, "lambda must be [\\ {params} {body}]"); return 0;
          }
          return compile_closure(cx, node->data.command.args[0], /*in_block=*/1,
                                 node->data.command.args[1]);
        }
        if (hid == HEAD_PROC) {
          int anon = argc >= 3 && node->data.command.args[0]->type == AST_LIT_STRING &&
                     node->data.command.args[0]->data.lit_string.length == 0;
          if (!anon) { cx_fail(cx, "nested named procs not supported (use a lambda / anonymous proc)"); return 0; }
          return compile_closure(cx, node->data.command.args[1], /*in_block=*/0,
                                 node->data.command.args[argc - 1]);
        }
      }

      /* Calling a closure value: the head is an expression (e.g. `[$f x]`). */
      if (node->data.command.head && node->data.command.head->type == AST_VAR_REF) {
        IrVal cval = compile_expr(cx, node->data.command.head);
        if (cx->failed) return 0;
        uint32_t argc = node->data.command.arg_count;
        IrVal fnargs[] = {cx->sp, cval};
        IrVal fn = emit_rt_call(cx, "jacl_closure_fn", fnargs, 2);
        IrVal fnw = irb_convert(cx->f, cx->cur, IRB_WRAP_I64, fn);
        IrVal cargs[2 + CG_MAX_PARAMS];
        IrType sig[2 + CG_MAX_PARAMS];
        cargs[0] = cx->sp; cargs[1] = cval; sig[0] = sig[1] = IRB_I64;
        for (uint32_t i = 0; i < argc && i < CG_MAX_PARAMS; i++) {
          cargs[2 + i] = compile_expr(cx, node->data.command.args[i]);
          sig[2 + i] = IRB_I64;
          if (cx->failed) return 0;
        }
        IrType r1[] = {IRB_I64};
        return irb_call_indirect(cx->f, cx->cur, sig, (int)argc + 2, r1, 1, fnw, cargs, (int)argc + 2);
      }

      if (hid == HEAD_DEF || hid == HEAD_MUT) {
        const char *name; uint32_t len;
        if (!binding_name(cx, node, &name, &len)) return 0;
        IrVal val = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        /* A `mut` captured by a closure is boxed in a heap cell so the mutation is
         * shared; `def` (immutable) and uncaptured `mut` stay plain SSA values. */
        if (hid == HEAD_MUT && is_captured_name(cx, name, len)) {
          IrVal a[] = {cx->sp, val};
          IrVal cell = emit_rt_call(cx, "jacl_cell_new", a, 2);
          env_define(cx, name, len, cell, /*is_mut=*/1, /*is_cell=*/1);
        } else {
          env_define(cx, name, len, val, hid == HEAD_MUT, /*is_cell=*/0);
        }
        return val;
      }
      if (hid == HEAD_SET) {
        const char *name; uint32_t len;
        if (!binding_name(cx, node, &name, &len)) return 0;
        IrVal val = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        Binding *bd = env_lookup(cx, name, len);
        if (!bd) { cx_failf(cx, "codegen: set of undefined variable '%.*s'", name, len); return 0; }
        if (!bd->is_mut) { cx_failf(cx, "codegen: cannot set immutable variable '%.*s'", name, len); return 0; }
        if (bd->is_cell) { IrVal a[] = {cx->sp, bd->value, val}; (void)emit_rt_call(cx, "jacl_cell_set", a, 3); }
        else bd->value = val; /* uncaptured: plain SSA rebind */
        return val;
      }

      /* Vector literal `[vec e0 e1 …]` → empty + push each element. */
      if (hid == HEAD_VEC) {
        IrVal empty[] = {cx->sp};
        IrVal acc = emit_rt_call(cx, "jacl_vec_empty", empty, 1);
        for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
          IrVal e = compile_expr(cx, node->data.command.args[i]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, acc, e};
          acc = emit_rt_call(cx, "jacl_vec_push", a, 3);
        }
        return acc;
      }
      /* `[length X]` → jacl_len (string / vec / map). */
      if (hid == HEAD_LENGTH && node->data.command.arg_count == 1) {
        IrVal v = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, v};
        return emit_rt_call(cx, "jacl_len", a, 2);
      }
      /* `[concat a b …]` → fold jacl_str_concat. */
      if (hid == HEAD_CONCAT && node->data.command.arg_count >= 2) {
        IrVal acc = compile_expr(cx, node->data.command.args[0]);
        for (uint32_t i = 1; i < node->data.command.arg_count; i++) {
          IrVal s = compile_expr(cx, node->data.command.args[i]);
          if (cx->failed) return 0;
          acc = emit_binop_call(cx, "jacl_str_concat", acc, s);
        }
        return acc;
      }

      const char *fn = binary_runtime_fn(hid);
      if (fn) {
        uint32_t argc = node->data.command.arg_count;
        AstNode **args = node->data.command.args;
        if (argc < 2) { cx_fail(cx, "operator needs at least 2 arguments"); return 0; }
        /* Type-driven: native i32 arithmetic when the typer proved i32 (boxed once). */
        if (i32_arith(node)) return box_i32(cx, compile_i32(cx, node));
        IrVal acc = compile_expr(cx, args[0]);
        for (uint32_t i = 1; i < argc; i++) {
          IrVal rhs = compile_expr(cx, args[i]);
          if (cx->failed) return 0;
          acc = emit_binop_call(cx, fn, acc, rhs);
        }
        return acc;
      }

      /* A call to a user-defined proc (matched by name — may collide with a builtin
       * head id like `count`, which we route to the user proc). */
      AstNode *head = node->data.command.head;
      if (head && head->type == AST_LIT_STRING) {
        Proc *p = proc_lookup(cx, head->data.lit_string.value, head->data.lit_string.length);
        if (p) {
          uint32_t argc = node->data.command.arg_count;
          if ((int)argc != p->arity) {
            cx_failf(cx, "codegen: proc '%.*s' arity mismatch",
                     head->data.lit_string.value, head->data.lit_string.length);
            return 0;
          }
          /* A generator proc-call constructs a generator object (a fiber over the
           * function), not a direct call: jacl_gen_new(ref.func, arg). */
          if (p->is_generator) {
            IrVal fnref = irb_ref_func(cx->f, cx->cur, p->func);
            IrVal fnref64 = irb_convert(cx->f, cx->cur, IRB_EXTEND_I32U, fnref);
            IrVal arg = (argc >= 1) ? compile_expr(cx, node->data.command.args[0])
                                    : irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
            if (cx->failed) return 0;
            IrVal a[] = {cx->sp, fnref64, arg};
            return emit_rt_call(cx, "jacl_gen_new", a, 3);
          }
          IrVal args[1 + CG_MAX_PARAMS];
          args[0] = cx->sp; /* data-SP ABI: thread sp as the leading argument */
          for (uint32_t i = 0; i < argc; i++) {
            args[i + 1] = compile_expr(cx, node->data.command.args[i]);
            if (cx->failed) return 0;
          }
          return irb_call(cx->f, cx->cur, p->func, args, (int)argc + 1);
        }
      }

      cx_fail(cx, "unsupported command head (scaffold: + - * / %, comparisons, def/mut/set, if/while/for, proc calls)");
      return 0;
    }

    default:
      cx_fail(cx, "unsupported AST node (scaffold: literals, vars, arithmetic, bindings, if, while)");
      return 0;
  }
}

/* ---- tail-position compilation (proc bodies) ----
 *
 * Compiling a proc body in tail position lets a call that produces the body's value
 * become a `return_call` (a real tail call), and an `if` in tail position return from
 * each branch directly (no join). Anything else falls back to "compute a value, then
 * return it". */
static void compile_tail(Cx *cx, AstNode *node);

static void emit_return_value(Cx *cx, IrVal v) {
  IrVal r[] = {v};
  irb_return(cx->f, cx->cur, r, 1);
}

static void compile_if_tail(Cx *cx, AstNode **args, uint32_t argc, uint32_t start) {
  if (start + 1 >= argc || args[start + 1]->type != AST_BLOCK) { cx_fail(cx, "if/elif needs a condition and a { then-block }"); return; }
  if (!frame_guard(cx)) return;
  IrVal cond = compile_expr(cx, args[start]);
  if (cx->failed) return;
  IrVal truth = emit_truthy(cx, cond);

  int w = frame_width(cx);
  IrBlock then_blk = new_i64_block(cx, w);
  IrBlock else_blk = new_i64_block(cx, w);
  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br_if(cx->f, cx->cur, truth, then_blk, frame, w, else_blk, frame, w);

  enter_frame_block(cx, then_blk);
  compile_tail(cx, args[start + 1]);
  if (cx->failed) return;

  enter_frame_block(cx, else_blk);
  uint32_t next = start + 2;
  if (next >= argc) {
    emit_return_value(cx, irb_const_i64(cx->f, cx->cur, JACLVAL_NIL));
  } else if (kw_is(args[next], "elif", 4)) {
    compile_if_tail(cx, args, argc, next + 1);
  } else if (kw_is(args[next], "else", 4)) {
    if (next + 1 >= argc || args[next + 1]->type != AST_BLOCK) { cx_fail(cx, "else needs a { block }"); return; }
    compile_tail(cx, args[next + 1]);
  } else if (args[next]->type == AST_BLOCK) {
    compile_tail(cx, args[next]);
  } else {
    cx_fail(cx, "malformed if (expected elif/else or an else block)");
  }
}

static void compile_tail(Cx *cx, AstNode *node) {
  if (cx->failed) return;

  if (node->type == AST_RETURN) {
    if (node->data.return_stmt.value) compile_tail(cx, node->data.return_stmt.value);
    else emit_return_value(cx, irb_const_i64(cx->f, cx->cur, JACLVAL_NIL));
    return;
  }

  if (node->type == AST_BLOCK) {
    uint32_t bn = node->data.block.count;
    if (bn == 0) { emit_return_value(cx, irb_const_i64(cx->f, cx->cur, jaclval_i32(0))); return; }
    scope_enter(cx);
    for (uint32_t i = 0; i + 1 < bn && !cx->failed; i++) compile_expr(cx, node->data.block.commands[i]);
    if (!cx->failed) compile_tail(cx, node->data.block.commands[bn - 1]);
    scope_exit(cx);
    return;
  }

  if (node->type == AST_COMMAND) {
    uint8_t hid = node->data.command.head_id;
    if (hid == HEAD_IF) { compile_if_tail(cx, node->data.command.args, node->data.command.arg_count, 0); return; }
    /* A tail-position proc call becomes a real tail call. A user proc name may
     * collide with a builtin head id (e.g. `count`), so match by the proc table, not
     * head_id; non-procs fall through to the value+return fallback. */
    if (node->data.command.head && node->data.command.head->type == AST_LIT_STRING) {
      AstNode *head = node->data.command.head;
      Proc *p = proc_lookup(cx, head->data.lit_string.value, head->data.lit_string.length);
      if (p) {
        uint32_t argc = node->data.command.arg_count;
        if ((int)argc != p->arity) {
          cx_failf(cx, "codegen: proc '%.*s' arity mismatch",
                   head->data.lit_string.value, head->data.lit_string.length);
          return;
        }
        IrVal args[1 + CG_MAX_PARAMS];
        args[0] = cx->sp;
        for (uint32_t i = 0; i < argc; i++) {
          args[i + 1] = compile_expr(cx, node->data.command.args[i]);
          if (cx->failed) return;
        }
        irb_return_call(cx->f, cx->cur, p->func, args, (int)argc + 1);
        return;
      }
    }
  }

  /* Fallback: compute the value normally and return it. */
  IrVal v = compile_expr(cx, node);
  if (!cx->failed) emit_return_value(cx, v);
}

/* Does `node` contain a `yield` (making its proc a generator)? Does not descend into
 * nested procs/closures — their yields make *them* generators. */
static int contains_yield(AstNode *node) {
  if (!node) return 0;
  if (node->type == AST_COMMAND) {
    uint8_t hid = node->data.command.head_id;
    if (hid == HEAD_YIELD) return 1;
    if (hid == HEAD_PROC) return 0;
    AstNode *h = node->data.command.head;
    if (h && h->type == AST_LIT_STRING && h->data.lit_string.length == 1 && h->data.lit_string.value[0] == '\\') return 0;
    for (uint32_t i = 0; i < node->data.command.arg_count; i++)
      if (contains_yield(node->data.command.args[i])) return 1;
    return 0;
  }
  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++)
      if (contains_yield(node->data.block.commands[i])) return 1;
    return 0;
  }
  if (node->type == AST_RETURN)
    return node->data.return_stmt.value ? contains_yield(node->data.return_stmt.value) : 0;
  return 0;
}

static int is_proc_def(AstNode *n) {
  return n->type == AST_COMMAND && n->data.command.head_id == HEAD_PROC;
}

/* Pass 1: create an SVM function (data-SP ABI: `func (i64 sp, i64 a0…) -> i64`) for
 * each top-level proc and register name -> {func, arity}, so calls (incl. recursion)
 * resolve before any body is compiled. */
static void register_procs(Cx *cx, IrModule *m, AstNode **nodes, uint32_t count) {
  IrType ptypes[1 + CG_MAX_PARAMS];
  IrType r1[] = {IRB_I64};
  for (uint32_t i = 0; i < count && !cx->failed; i++) {
    if (!is_proc_def(nodes[i])) continue;
    AstNode *def = nodes[i];
    uint32_t argc = def->data.command.arg_count;
    if (argc < 3 || def->data.command.args[0]->type != AST_LIT_STRING ||
        def->data.command.args[argc - 1]->type != AST_BLOCK) {
      cx_fail(cx, "malformed proc (expected `proc name {params} {body}`)"); return;
    }
    const char *pname = def->data.command.args[0]->data.lit_string.value;
    uint32_t plen = def->data.command.args[0]->data.lit_string.length;
    if (proc_lookup(cx, pname, plen)) { cx_failf(cx, "codegen: proc '%.*s' already defined", pname, plen); return; }

    const char *names[CG_MAX_PARAMS]; uint32_t lens[CG_MAX_PARAMS];
    int arity = extract_param_names(cx, def->data.command.args[1], names, lens);
    if (arity < 0) return;

    /* A generator (body has `yield`) is a fiber function `(i64 sp, i64 arg) -> i64`:
     * the single declared param binds to the resume arg. Otherwise a plain proc
     * `(i64 sp, i64 a0…)`. */
    int gen = contains_yield(def->data.command.args[argc - 1]);
    if (gen && arity > 1) { cx_failf(cx, "codegen: generator '%.*s' may take at most one parameter (yet)", pname, plen); return; }
    int nparams = gen ? 1 : arity;
    ptypes[0] = IRB_I64; /* sp */
    for (int k = 0; k < nparams; k++) ptypes[k + 1] = IRB_I64;
    IrFunc *func = irb_func_new(m, ptypes, nparams + 1, r1, 1);

    if (cx->nprocs == cx->cap_procs) {
      cx->cap_procs = cx->cap_procs ? cx->cap_procs * 2 : 8;
      cx->procs = realloc(cx->procs, (size_t)cx->cap_procs * sizeof(Proc));
      if (!cx->procs) { fprintf(stderr, "codegen: oom\n"); abort(); }
    }
    cx->procs[cx->nprocs++] = (Proc){pname, plen, func, arity, gen, def};
  }
}

/* Reset the per-function lexical environment (the proc table persists). */
static void env_reset(Cx *cx) { cx->nlocals = 0; cx->nmarks = 0; }

/* Pass 2: compile each registered proc's body into its function. */
static void compile_procs(Cx *cx) {
  for (int i = 0; i < cx->nprocs && !cx->failed; i++) {
    Proc *p = &cx->procs[i];
    AstNode *def = p->def;
    uint32_t argc = def->data.command.arg_count;
    AstNode *body = def->data.command.args[argc - 1];

    const char *names[CG_MAX_PARAMS]; uint32_t lens[CG_MAX_PARAMS];
    int arity = extract_param_names(cx, def->data.command.args[1], names, lens);
    if (arity < 0) return;

    /* A generator function is (sp, arg); its single declared param binds to arg. */
    int nparams = p->is_generator ? 1 : arity;
    IrType ptypes[1 + CG_MAX_PARAMS];
    for (int k = 0; k <= nparams; k++) ptypes[k] = IRB_I64;
    cx->f = p->func;
    cx->cur = irb_block(p->func, ptypes, nparams + 1);
    cx->sp = 0; /* param 0 */
    env_reset(cx);
    capset_reset(cx); capset_scan(cx, body); /* which muts a nested closure captures */
    scope_enter(cx);
    /* params bind to fn params 1..; for a generator (arity<=1) the param is the arg */
    for (int k = 0; k < arity; k++)
      env_define(cx, names[k], lens[k], (IrVal)(k + 1), /*is_mut=*/0, /*is_cell=*/0);

    compile_tail(cx, body); /* emits the proc's return / return_call (done) */
    scope_exit(cx);
    if (cx->failed) return;
  }
}

/* Compile the bodies of closures enqueued during compilation (a worklist — nested
 * closures enqueue more). Each closure function is `(i64 sp, i64 self, params…)`; its
 * params bind to its args and its captures are read back from `self`. */
static void compile_pending_closures(Cx *cx) {
  for (int i = 0; i < cx->npending && !cx->failed; i++) {
    Pending p = cx->pending[i]; /* copy: the worklist may realloc as bodies enqueue */
    IrType ptypes[2 + CG_MAX_PARAMS];
    for (int k = 0; k < 2 + p.nparams; k++) ptypes[k] = IRB_I64;
    cx->f = p.func;
    cx->cur = irb_block(p.func, ptypes, 2 + p.nparams);
    cx->sp = 0; /* param 0; self is param 1 */
    env_reset(cx);
    capset_reset(cx); capset_scan(cx, p.body); /* muts captured by this closure's own nested closures */
    scope_enter(cx);
    for (int k = 0; k < p.nparams; k++) env_define(cx, p.pnames[k], p.plens[k], (IrVal)(2 + k), /*is_mut=*/0, /*is_cell=*/0);
    for (int j = 0; j < p.ncaps; j++) {
      IrVal idx = irb_const_i64(cx->f, cx->cur, j);
      IrVal uargs[] = {cx->sp, /*self=*/1, idx};
      IrVal uv = emit_rt_call(cx, "jacl_closure_upval", uargs, 3);
      /* a captured cell stays a cell (mutable + shared); a by-value capture is immutable */
      env_define(cx, p.cnames[j], p.clens[j], uv, /*is_mut=*/p.ccell[j], /*is_cell=*/p.ccell[j]);
    }
    compile_tail(cx, p.body);
    scope_exit(cx);
  }
}

IrModule *svm_codegen_program(AstNode **nodes, uint32_t count, char *err, size_t errcap) {
  Cx cx;
  memset(&cx, 0, sizeof cx);
  cx.err = err; cx.errcap = errcap;

  IrModule *m = irb_module_new();
  cx.m = m;

  /* Entry must be program function 0 (the harness runs it). Create it first, compile
   * its body last so it can call procs defined anywhere at top level. */
  IrType i64_1[] = {IRB_I64};
  IrType r1[] = {IRB_I64};
  IrFunc *entry = irb_func_new(m, i64_1, 1, r1, 1);
  IrBlock entry_block = irb_block(entry, i64_1, 1);

  register_procs(&cx, m, nodes, count); /* pass 1 */
  if (!cx.failed) compile_procs(&cx);   /* pass 2: proc bodies */

  /* Entry body: the non-proc top-level forms, in order; value = last. */
  if (!cx.failed) {
    cx.f = entry; cx.cur = entry_block; cx.sp = 0;
    /* Initialize the runtime before anything allocates: reset the heap (so GC's
     * sweep walks well-formed cells), the string intern table, and the map key
     * handlers. Without this the program runs on an uninitialized heap. */
    emit_void_call(&cx, "jacl_heap_init");
    emit_void_call(&cx, "jacl_intern_init");
    emit_void_call(&cx, "jacl_map_init");
    env_reset(&cx);
    capset_reset(&cx);
    for (uint32_t i = 0; i < count; i++) if (!is_proc_def(nodes[i])) capset_scan(&cx, nodes[i]);
    scope_enter(&cx);
    IrVal last = 0; int have = 0;
    for (uint32_t i = 0; i < count; i++) {
      if (is_proc_def(nodes[i])) continue;
      last = compile_expr(&cx, nodes[i]);
      have = 1;
      if (cx.failed) break;
    }
    if (!have && !cx.failed) last = irb_const_i64(cx.f, cx.cur, jaclval_i32(0));
    scope_exit(&cx);
    if (!cx.failed) { IrVal ret[] = {last}; irb_return(cx.f, cx.cur, ret, 1); }
  }

  if (!cx.failed) compile_pending_closures(&cx); /* closure bodies (incl. nested) */

  int failed = cx.failed;
  free(cx.locals);
  free(cx.marks);
  free(cx.procs);
  free(cx.pending);
  if (failed) { irb_module_free(m); return NULL; }
  return m;
}
