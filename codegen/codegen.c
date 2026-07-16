/* codegen.c — JACL AST → SVM-IR (P2.2–P2.4 scaffold). See codegen.h. */
#include "codegen.h"

#include "../src/jacl.h"   /* AstNode, AstNodeType, HeadId, JaclType */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JaclVal encodings (see runtime/jaclrt.h). */
#define JACL_TAG_I32_SHIFTED ((uint64_t)0x02 << 56)
#define JACLVAL_FALSE        ((int64_t)((uint64_t)0x01 << 56))   /* JACL_FALSE */
#define JACLVAL_TRUE         ((int64_t)(((uint64_t)0x01 << 56) | 1)) /* JACL_TRUE */
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
  /* static annotation carried from a typed [Arr T]/[Buf N T] def (compile-time checks) */
  const char *elem_type; uint32_t elem_type_len;   /* declared element type, or NULL */
  int32_t     buf_size;                            /* fixed Buf length, or -1 */
} Binding;

/* A top-level proc: name -> its SVM function + arity. Registered before any body is
 * compiled, so (mutual) recursion resolves. */
typedef struct {
  const char *name;
  uint32_t    len;
  IrFunc     *func;
  int         arity;
  int         is_generator; /* body contains `yield` → a fiber generator (sp, arg) */
  int         returns_stream; /* return annotation is `[Stream T]` → a typed stream */
  int         variadic;     /* last param is `..rest`: extra call args pack into a vec */
  int         fixed_arity;  /* count of fixed params before the rest (variadic only) */
  AstNode    *def; /* the proc AST_COMMAND, recompiled in pass 2 */
  IrFunc     *wrapper; /* lazily-created closure-ABI adapter for `$proc` value refs */
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

  /* innermost enclosing loop (break/continue targets); width = frame at loop entry */
  IrBlock loop_continue, loop_break; int loop_width; int in_loop;
  int loop_brk_ok;  /* innermost loop carries a `break V` result slot (BRK_SENTINEL) */
  /* innermost enclosing try (statement errors branch to the handler, not return) */
  IrBlock try_target; int try_width; int in_try;
  int cur_is_generator; /* the proc currently being compiled contains `yield` */

  /* struct declarations (dynamic name-based records): name -> ordered field names + types */
  struct SDef {
    const char *name; uint32_t len;
    const char *fn[CG_MAX_PARAMS]; uint32_t fl[CG_MAX_PARAMS];
    const char *ft[CG_MAX_PARAMS]; uint32_t ftl[CG_MAX_PARAMS];   /* field type name/len */
    int nf;
  } sdefs[32];
  int nsdefs;

  /* module globals — names bound at top level, visible from any proc/closure via the
   * runtime global map (jacl_global_get/set). Populated by a pre-scan of the top-level forms. */
  const char *globals[128]; uint32_t globalslen[128]; int nglobals;
  int at_top_level;   /* compiling the top-level forms → def/mut also mirror into the global map */
  int wants_trace;    /* program uses `[stack-trace]` → emit call-stack instrumentation */
  const char *cur_proc; uint32_t cur_proc_len;  /* name of the proc being compiled (for traces) */
} Cx;
typedef struct SDef SDef;

static int is_global_name(Cx *cx, const char *s, uint32_t l) {
  for (int i = 0; i < cx->nglobals; i++)
    if (cx->globalslen[i] == l && memcmp(cx->globals[i], s, l) == 0) return 1;
  return 0;
}
static void global_add(Cx *cx, const char *s, uint32_t l) {
  if (is_global_name(cx, s, l) || cx->nglobals >= 128) return;
  cx->globals[cx->nglobals] = s; cx->globalslen[cx->nglobals] = l; cx->nglobals++;
}

static void cx_fail(Cx *cx, const char *msg); /* fwd */

static SDef *sdef_lookup(Cx *cx, const char *name, uint32_t len) {
  for (int i = 0; i < cx->nsdefs; i++)
    if (cx->sdefs[i].len == len && memcmp(cx->sdefs[i].name, name, len) == 0) return &cx->sdefs[i];
  return NULL;
}
static void sdef_register(Cx *cx, AstNode *node) {
  if (sdef_lookup(cx, node->data.defstruct.name, node->data.defstruct.name_len)) return;
  if (cx->nsdefs >= 32) { cx_fail(cx, "too many struct declarations"); return; }
  SDef *sd = &cx->sdefs[cx->nsdefs++];
  sd->name = node->data.defstruct.name;
  sd->len = node->data.defstruct.name_len;
  sd->nf = 0;
  for (uint32_t k = 0; k < node->data.defstruct.field_count && sd->nf < CG_MAX_PARAMS; k++) {
    sd->fn[sd->nf] = node->data.defstruct.field_names[k];
    sd->fl[sd->nf] = node->data.defstruct.field_name_lens[k];
    sd->ft[sd->nf] = node->data.defstruct.field_types ? node->data.defstruct.field_types[k] : NULL;
    sd->ftl[sd->nf] = node->data.defstruct.field_type_lens ? node->data.defstruct.field_type_lens[k] : 0;
    sd->nf++;
  }
}

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
  /* Re-binding is allowed for the throwaway `_`, and at the outermost module scope where a
   * top-level `def` is reassignment (the old VM permits `def y 10` then `def y 42`). Inside
   * any nested block/proc scope, same-scope shadowing of an immutable binding is an error. */
  int module_scope = cx->at_top_level && cx->nmarks <= 1;
  if (!module_scope && !(len == 1 && name[0] == '_'))
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
  cx->locals[cx->nlocals++] = (Binding){name, len, value, is_mut, is_cell, NULL, 0, -1};
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
/* A scalar type prefix in a `<type> <name>` param: a builtin type keyword or a
 * Capitalized identifier (a struct/type name, e.g. `Point p`). Distinguishes a typed
 * param's type token from the name that follows it (param names are lowercase). */
static int cg_is_type_prefix(const char *s, uint32_t n) {
  return cg_is_type_kw(s, n) || (n > 0 && s[0] >= 'A' && s[0] <= 'Z');
}
/* An integer scalar type keyword — a `[Arr <int>]` back-fills OOB grows with 0, not nil. */
static int cg_is_int_scalar(const char *s, uint32_t n) {
  static const struct { const char *k; uint32_t n; } T[] = {
    {"i8", 2}, {"u8", 2}, {"i16", 3}, {"u16", 3}, {"i32", 3}, {"i64", 3}, {"u32", 3}, {"u64", 3}};
  for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); i++)
    if (T[i].n == n && memcmp(T[i].k, s, n) == 0) return 1;
  return 0;
}

/* Pre-scan the top-level forms for `def`/`mut` NAME bindings; those names become module
 * globals visible from any proc/closure. Runs before proc bodies compile so a proc that
 * references a top-level binding resolves it to the global map instead of erroring. */
static void scan_top_globals(Cx *cx, AstNode **nodes, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    AstNode *n = nodes[i];
    if (n->type != AST_COMMAND) continue;
    uint8_t hid = n->data.command.head_id;
    if (hid != HEAD_DEF && hid != HEAD_MUT) continue;
    AstNode **a = n->data.command.args; uint32_t ac = n->data.command.arg_count;
    if (ac < 2) continue;
    uint32_t ni = 0;   /* skip a leading type annotation: `def i64 x V` / `def [Vec T] xs V` */
    if (ac >= 3 && a[1]->type == AST_LIT_STRING &&
        (a[0]->type == AST_COMMAND ||
         (a[0]->type == AST_LIT_STRING &&
          cg_is_type_prefix(a[0]->data.lit_string.value, a[0]->data.lit_string.length))))
      ni = 1;
    if (a[ni]->type == AST_LIT_STRING)
      global_add(cx, a[ni]->data.lit_string.value, a[ni]->data.lit_string.length);
  }
}

/* Read a proc's param NAMES from its params command (args[1]). Params are flattened
 * into the command's head + args; typed params interleave a type token before each
 * name (`{i32 x, i32 y}` -> i32 x i32 y), untyped are bare (`{x, y}` -> x y), and a
 * single empty-string token means zero params (`{}`). Returns the count, or -1 on
 * error. Types are skipped (codegen treats every value as an i64 JaclVal). */
static int extract_param_names_v(Cx *cx, AstNode *plist, const char **names, uint32_t *lens,
                                 int *variadic_out) {
  if (variadic_out) *variadic_out = 0;
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
    /* `..rest` — the trailing rest param collects extra call args into a vector. The
     * `..` arrives as its own bare token before the name; mark variadic and skip it. */
    if (toks[i]->type == AST_LIT_STRING && toks[i]->data.lit_string.length == 2 &&
        memcmp(toks[i]->data.lit_string.value, "..", 2) == 0) {
      if (variadic_out) *variadic_out = 1;
      i += 1;
      if (i >= nt) { cx_fail(cx, "proc `..` needs a rest-param name"); return -1; }
    }
    /* A compound type annotation (`[Vec T] xs`, `[Map K V] m`, `[Proc …] f`) arrives
     * as a COMMAND token before the name — skip it like a scalar type keyword. */
    if (toks[i]->type == AST_COMMAND && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) i += 1;
    if (toks[i]->type != AST_LIT_STRING) { cx_fail(cx, "proc param must be a name"); return -1; }
    const char *s = toks[i]->data.lit_string.value;
    uint32_t n = toks[i]->data.lit_string.length;
    AstNode *name_tok = toks[i];
    if (cg_is_type_prefix(s, n) && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) {
      name_tok = toks[i + 1]; i += 2; /* `<type> <name>` (incl. `Point p`) */
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
/* Back-compat wrapper for callers that don't care about the variadic flag. */
static int extract_param_names(Cx *cx, AstNode *plist, const char **names, uint32_t *lens) {
  return extract_param_names_v(cx, plist, names, lens, NULL);
}

/* Declared type token for each param (`i64 a` -> "i64", `Point p` -> "Point", bare `a` ->
 * NULL). Mirrors extract_param_names_v's token walk but captures the type, not the name;
 * used to format a proc/closure signature for runtime introspection. Returns the count. */
static int extract_param_types(AstNode *plist, const char **types, uint32_t *tlens) {
  if (!plist || plist->type != AST_COMMAND) return 0;
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
  while (i < nt && np < CG_MAX_PARAMS) {
    if (toks[i]->type == AST_LIT_STRING && toks[i]->data.lit_string.length == 2 &&
        memcmp(toks[i]->data.lit_string.value, "..", 2) == 0) {
      i += 1;
      if (i >= nt) break;
    }
    if (toks[i]->type == AST_COMMAND) {   /* compound type token (`[Buf N T] b`) */
      types[np] = NULL; tlens[np] = 0; np++;
      if (i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) i += 2; else i += 1;
      continue;
    }
    if (toks[i]->type != AST_LIT_STRING) { i += 1; continue; }
    const char *s = toks[i]->data.lit_string.value;
    uint32_t n = toks[i]->data.lit_string.length;
    if (cg_is_type_prefix(s, n) && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) {
      types[np] = s; tlens[np] = n; np++; i += 2;   /* `<type> <name>` */
    } else {
      types[np] = NULL; tlens[np] = 0; np++; i += 1; /* untyped `<name>` */
    }
  }
  return np;
}

/* Format a proc/closure signature into buf, returning its length. A typed subject (has a
 * return annotation or any typed param) reads `<proc name(t0, t1) -> ret>` — or `<closure
 * (…) -> ret>` when anonymous; an untyped one is just `<proc name>` / `<closure>`. */
static size_t format_proc_sig(char *buf, size_t cap, const char *name, uint32_t namelen,
                              int is_closure, AstNode *params, AstNode *ret) {
  const char *types[CG_MAX_PARAMS]; uint32_t tlens[CG_MAX_PARAMS];
  int np = extract_param_types(params, types, tlens);
  int ret_typed = ret && ret->type == AST_LIT_STRING;
  int any_typed = ret_typed;
  for (int k = 0; k < np; k++) if (types[k]) any_typed = 1;
  size_t o = 0;
#define SIG_PUT(str, n) do { for (uint32_t _i = 0; _i < (uint32_t)(n) && o < cap - 1; _i++) buf[o++] = (str)[_i]; } while (0)
  SIG_PUT("<", 1);
  if (is_closure) SIG_PUT("closure", 7);
  else { SIG_PUT("proc ", 5); SIG_PUT(name, namelen); }
  if (!any_typed) { SIG_PUT(">", 1); buf[o] = 0; return o; }
  SIG_PUT("(", 1);
  for (int k = 0; k < np; k++) {
    if (k) SIG_PUT(", ", 2);
    if (types[k]) SIG_PUT(types[k], tlens[k]); else SIG_PUT("dyn", 3);
  }
  SIG_PUT(")", 1);
  SIG_PUT(" -> ", 4);
  if (ret_typed) SIG_PUT(ret->data.lit_string.value, ret->data.lit_string.length);
  else SIG_PUT("dyn", 3);
  SIG_PUT(">", 1);
  buf[o] = 0;
#undef SIG_PUT
  return o;
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

static IrVal compile_string_literal(Cx *cx, const char *s, uint32_t len);  /* fwd */

/* Does the AST contain a `[stack-trace]`? When it does, the codegen instruments proc
 * entries and call sites to maintain a shadow call stack; otherwise that overhead (and its
 * regression surface) is skipped entirely. */
static int contains_stack_trace(AstNode *n) {
  if (!n) return 0;
  if (n->type == AST_COMMAND) {
    if (n->data.command.head_id == HEAD_STACK_TRACE) return 1;
    if (n->data.command.head && contains_stack_trace(n->data.command.head)) return 1;
    for (uint32_t i = 0; i < n->data.command.arg_count; i++)
      if (contains_stack_trace(n->data.command.args[i])) return 1;
    return 0;
  }
  if (n->type == AST_BLOCK) {
    for (uint32_t i = 0; i < n->data.block.count; i++)
      if (contains_stack_trace(n->data.block.commands[i])) return 1;
    return 0;
  }
  if (n->type == AST_RETURN)
    return n->data.return_stmt.value ? contains_stack_trace(n->data.return_stmt.value) : 0;
  return 0;
}
/* Push a call-stack frame for the proc/entry being entered (only when tracing). */
static void emit_trace_push(Cx *cx, const char *name, uint32_t len) {
  if (!cx->wants_trace) return;
  IrVal nm = compile_string_literal(cx, name, len);
  IrVal a[] = {cx->sp, nm};
  (void)emit_rt_call(cx, "jacl_trace_push", a, 2);
}
/* Record the current line on the top frame before a call / at an error (only when tracing). */
static void emit_trace_line(Cx *cx, uint32_t line) {
  if (!cx->wants_trace) return;
  IrVal lv = irb_const_i64(cx->f, cx->cur, jaclval_i32((int32_t)line));
  IrVal a[] = {cx->sp, lv};
  (void)emit_rt_call(cx, "jacl_trace_line", a, 2);
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

/* ---- struct field defaults ----
 * A struct field left unset by the constructor takes its TYPE's zero: numeric -> 0,
 * bool -> false, str -> "", a struct type -> a recursively zero-initialized struct,
 * everything else -> nil. Mirrors the old VM's `[Type]` (all-defaults) semantics. */
static IrVal emit_struct_zero(Cx *cx, SDef *sd, int depth);
static IrVal emit_field_default(Cx *cx, const char *tn, uint32_t tl, int depth) {
  if (!tn || tl == 0) return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
  /* A struct field of type `[Buf N T]` is stored by the parser as "Buf{N,T}"; zero-init
   * it as a fresh N-element buffer (recursively defaulting each element). */
  if (tl > 4 && memcmp(tn, "Buf{", 4) == 0 && depth < 8) {
    int32_t n = 0; uint32_t i = 4;
    while (i < tl && tn[i] >= '0' && tn[i] <= '9') { n = n * 10 + (tn[i] - '0'); i++; }
    if (i < tl && tn[i] == ',') i++;
    const char *et = tn + i; uint32_t etl = 0;
    while (i + etl < tl && tn[i + etl] != '}') etl++;
    IrVal ea[] = {cx->sp};
    IrVal av = emit_rt_call(cx, "jacl_arr_new", ea, 1);
    for (int32_t k = 0; k < n; k++) {
      IrVal e = emit_field_default(cx, et, etl, depth + 1);
      IrVal pa[] = {cx->sp, av, e};
      (void)emit_rt_call(cx, "jacl_arr_push", pa, 3);
    }
    return av;
  }
  if (cg_is_type_kw(tn, tl)) {
    if (tl == 4 && memcmp(tn, "bool", 4) == 0) return irb_const_i64(cx->f, cx->cur, JACLVAL_FALSE);
    if (tl == 3 && memcmp(tn, "str", 3) == 0) return compile_string_literal(cx, "", 0);
    if ((tl == 3 && (memcmp(tn, "vec", 3) == 0 || memcmp(tn, "map", 3) == 0 ||
                     memcmp(tn, "dyn", 3) == 0)) ||
        (tl == 6 && memcmp(tn, "stream", 6) == 0))
      return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
    return irb_const_i64(cx->f, cx->cur, jaclval_i32(0));   /* numeric scalar zero */
  }
  SDef *inner = sdef_lookup(cx, tn, tl);
  if (inner && depth < 8) return emit_struct_zero(cx, inner, depth + 1);
  return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
}
static IrVal emit_struct_zero(Cx *cx, SDef *sd, int depth) {
  IrVal tn = compile_string_literal(cx, sd->name, sd->len);
  IrVal nf = irb_const_i64(cx->f, cx->cur, jaclval_i32(sd->nf));
  IrVal na[] = {cx->sp, tn, nf};
  IrVal sv = emit_rt_call(cx, "jacl_struct_new", na, 3);
  for (int k = 0; k < sd->nf; k++) {
    IrVal fname = compile_string_literal(cx, sd->fn[k], sd->fl[k]);
    IrVal idx = irb_const_i64(cx->f, cx->cur, jaclval_i32(k));
    IrVal dv = emit_field_default(cx, sd->ft[k], sd->ftl[k], depth);
    IrVal ia[] = {cx->sp, sv, idx, fname, dv};
    sv = emit_rt_call(cx, "jacl_struct_init_field", ia, 5);
  }
  return sv;
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
      node->data.command.args[0]->type == AST_LIT_STRING) {
    /* Anonymous ([proc {x} {…}]) or NESTED NAMED procs — both compile as closures,
     * so both count for the enclosing function's capture scan. (Top-level named
     * procs never reach this: they're filtered by is_proc_def before scanning.) */
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
    if (toks[i]->type == AST_COMMAND && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) i += 1;
    if (toks[i]->type != AST_LIT_STRING) return np;
    const char *s = toks[i]->data.lit_string.value; uint32_t n = toks[i]->data.lit_string.length;
    AstNode *nmtok = toks[i];
    if (cg_is_type_prefix(s, n) && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) { nmtok = toks[i + 1]; i += 2; }
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
    uint8_t hid = node->data.command.head_id;
    /* spawn/parallel/race run each { block } as a 0-param closure (compile_closure). Treat
     * those blocks like closure bodies here so a `mut` they capture is boxed at its
     * declaration — otherwise the capture site hits "captured mutable not boxed". */
    if (hid == HEAD_SPAWN || hid == HEAD_PARALLEL || hid == HEAD_RACE) {
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        AstNode *blk = node->data.command.args[i];
        if (blk->type == AST_BLOCK) {
          const char *bound[CG_CAP_MAX]; uint32_t blen[CG_CAP_MAX]; int nb = 0;
          collect_bound(blk, bound, blen, &nb);
          add_free_varrefs(blk, bound, blen, nb, cx->capset, cx->capsetlen, &cx->ncapset);
        } else capset_scan(cx, blk);
      }
      return;
    }
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
static IrVal compile_closure(Cx *cx, AstNode *pnode, int in_block, AstNode *body, AstNode *ret_ann) {
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
  /* Record the signature for runtime introspection (print of the closure value). Only
   * proc-style params (a command list) carry type annotations; `\`-lambda params arrive as
   * a BLOCK and are untyped, so they fall back to `<closure>` at print time. */
  if (!in_block) {
    char sig[256];
    size_t sl = format_proc_sig(sig, sizeof sig, "", 0, /*is_closure=*/1, pnode, ret_ann);
    IrVal sigv = compile_string_literal(cx, sig, (uint32_t)sl);
    IrVal ra[] = {cx->sp, fnref64, sigv};
    (void)emit_rt_call(cx, "jacl_proc_sig_register", ra, 3);
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
  { int si = cx->in_loop; IrBlock sc = cx->loop_continue, sb = cx->loop_break; int sw = cx->loop_width; int sk = cx->loop_brk_ok;
    cx->in_loop = 1; cx->loop_continue = header; cx->loop_break = exit_blk; cx->loop_width = w; cx->loop_brk_ok = 0;
    (void)compile_expr(cx, args[1]); /* body value discarded */
    cx->in_loop = si; cx->loop_continue = sc; cx->loop_break = sb; cx->loop_width = sw; cx->loop_brk_ok = sk; }
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
  IrBlock step_blk = new_i64_block(cx, w);   /* i = i + 1 (continue lands here) */
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

  /* body: run, then fall into the step */
  enter_frame_block(cx, body_blk);
  { int si = cx->in_loop; IrBlock sc = cx->loop_continue, sb = cx->loop_break; int sw = cx->loop_width; int sk = cx->loop_brk_ok;
    cx->in_loop = 1; cx->loop_continue = step_blk; cx->loop_break = exit_blk; cx->loop_width = w; cx->loop_brk_ok = 0;
    (void)compile_expr(cx, body); /* body value discarded; may read NAME */
    cx->in_loop = si; cx->loop_continue = sc; cx->loop_break = sb; cx->loop_width = sw; cx->loop_brk_ok = sk; }
  if (cx->failed) { scope_exit(cx); return 0; }
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, step_blk, frame, w);

  /* step: i = i + 1, loop back */
  enter_frame_block(cx, step_blk);
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

/* Synthesized AST nodes (heap-allocated; live for the compile) for sugar forms. */
HeadId jacl_compute_head_id_bridge(AstNode *head); /* frontend (via the unity driver) */
static AstNode *synth_word(const char *v, uint32_t len) {
  AstNode *n = calloc(1, sizeof(AstNode));
  n->type = AST_LIT_STRING;
  n->data.lit_string.value = v;
  n->data.lit_string.length = len;
  return n;
}
static AstNode *synth_command(AstNode *head, AstNode **args, uint32_t argc) {
  AstNode *n = calloc(1, sizeof(AstNode));
  n->type = AST_COMMAND;
  n->data.command.head = head;
  n->data.command.args = args;
  n->data.command.arg_count = argc;
  n->data.command.head_id = jacl_compute_head_id_bridge(head);
  return n;
}

/* Is this node a call to a registered generator proc? */
static Proc *generator_call(Cx *cx, AstNode *n) {
  if (!n || n->type != AST_COMMAND || !n->data.command.head ||
      n->data.command.head->type != AST_LIT_STRING) return NULL;
  Proc *p = proc_lookup(cx, n->data.command.head->data.lit_string.value,
                        n->data.command.head->data.lit_string.length);
  return (p && p->is_generator) ? p : NULL;
}

/* Does this expression evaluate to a *typed* stream — i.e. one whose materialized
 * (collected/transformed/filtered/taken) form is a `[Vec T]` that prints comma-style?
 * The leaf is a generator proc annotated `[Stream T]`; the stream combinators preserve
 * the typedness of their source. Used to stamp the result vector with JACL_TAG_TVEC. */
static int expr_is_typed_stream(Cx *cx, AstNode *n) {
  if (!n || n->type != AST_COMMAND) return 0;
  uint8_t hid = n->data.command.head_id;
  AstNode **a = n->data.command.args;
  uint32_t ac = n->data.command.arg_count;
  if (hid == HEAD_COLLECT && ac == 1) return expr_is_typed_stream(cx, a[0]);
  if ((hid == HEAD_TRANSFORM || hid == HEAD_FILTER || hid == HEAD_TAKE) && ac == 2)
    return expr_is_typed_stream(cx, a[0]);
  Proc *p = generator_call(cx, n);
  return p && p->returns_stream;
}

/* Eagerly drain a source (an already-evaluated generator object or vec-like) into a
 * fresh vector, optionally applying a closure per element (transform) or using its
 * truthiness as a keep test (filter). Frame-threaded like the other loops. */
#define ST_SRC "\x01""st-src"
#define ST_ACC "\x01""st-acc"
#define ST_CLO "\x01""st-clo"
#define ST_IDX "\x01""st-idx"
static IrVal stream_into_vec(Cx *cx, IrVal src, int is_gen, IrVal clo, int is_filter) {
  scope_enter(cx);
  IrVal ea[] = {cx->sp};
  IrVal acc0 = emit_rt_call(cx, "jacl_vec_empty", ea, 1);
  env_define(cx, ST_SRC, 7, src, 0, 0);
  env_define(cx, ST_ACC, 7, acc0, 1, 0);
  if (clo) env_define(cx, ST_CLO, 7, clo, 0, 0);
  if (!is_gen) {
    IrVal zero = irb_const_i64(cx->f, cx->cur, jaclval_i32(0));
    env_define(cx, ST_IDX, 7, zero, 1, 0);
  }
  if (cx->failed || !frame_guard(cx)) { scope_exit(cx); return 0; }

  int w = frame_width(cx);
  IrBlock header = new_i64_block(cx, w);
  /* gen mode threads the header-produced element into the body as an extra param
   * (block-local SSA: header values are invisible in the body otherwise). */
  IrBlock body_blk = new_i64_block(cx, is_gen ? w + 1 : w);
  IrBlock push_blk = new_i64_block(cx, w + 1);   /* extra param: the value to push */
  IrBlock next_blk = new_i64_block(cx, w);
  IrBlock exit_blk = new_i64_block(cx, w);
  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  IrVal elem = 0;
  enter_frame_block(cx, header);
  if (is_gen) {
    Binding *bs = env_lookup(cx, ST_SRC, 7);
    IrVal na[] = {cx->sp, bs->value};
    elem = emit_rt_call(cx, "jacl_gen_next", na, 2);
    IrVal da[] = {cx->sp, bs->value};
    IrVal d = emit_rt_call(cx, "jacl_gen_done", da, 2);
    IrVal truth = emit_truthy(cx, d);
    fill_frame(cx, frame);
    frame[w] = elem;
    irb_br_if(cx->f, cx->cur, truth, exit_blk, frame, w, body_blk, frame, w + 1);
  } else {
    Binding *bs = env_lookup(cx, ST_SRC, 7);
    Binding *bi = env_lookup(cx, ST_IDX, 7);
    IrVal la[] = {cx->sp, bs->value};
    IrVal len = emit_rt_call(cx, "jacl_len", la, 2);
    IrVal cond = emit_binop_call(cx, "jacl_lt", bi->value, len);
    IrVal ctrue = irb_const_i64(cx->f, cx->cur, JACLVAL_TRUE);
    IrVal is_true = irb_intcmp(cx->f, cx->cur, IRB_I64, IRB_EQ, cond, ctrue);
    fill_frame(cx, frame);
    irb_br_if(cx->f, cx->cur, is_true, body_blk, frame, w, exit_blk, frame, w);
  }

  /* body: fetch elem (vec mode), apply the closure, decide push vs skip */
  enter_frame_block(cx, body_blk);
  {
    if (!is_gen) {
      Binding *bs = env_lookup(cx, ST_SRC, 7);
      Binding *bi = env_lookup(cx, ST_IDX, 7);
      IrVal ga[] = {cx->sp, bs->value, bi->value};
      elem = emit_rt_call(cx, "jacl_index_get", ga, 3);
    } else {
      elem = (IrVal)w;   /* the threaded extra block param (see body_blk creation) */
    }
    IrVal keep = elem;
    if (clo) {
      Binding *bc = env_lookup(cx, ST_CLO, 7);
      IrVal fa[] = {cx->sp, bc->value};
      IrVal fn = emit_rt_call(cx, "jacl_closure_fn", fa, 2);
      IrVal fnw = irb_convert(cx->f, cx->cur, IRB_WRAP_I64, fn);
      IrType sig[] = {IRB_I64, IRB_I64, IRB_I64};
      IrType r1[] = {IRB_I64};
      IrVal cargs[] = {cx->sp, bc->value, elem};
      IrVal r = irb_call_indirect(cx->f, cx->cur, sig, 3, r1, 1, fnw, cargs, 3);
      if (is_filter) {
        IrVal truth = emit_truthy(cx, r);
        fill_frame(cx, frame);
        frame[w] = elem;
        irb_br_if(cx->f, cx->cur, truth, push_blk, frame, w + 1, next_blk, frame, w);
        goto after_branch;
      }
      keep = r;
    }
    fill_frame(cx, frame);
    frame[w] = keep;
    irb_br(cx->f, cx->cur, push_blk, frame, w + 1);
  }
after_branch:

  /* push: acc = vec_push(acc, value-param) */
  enter_frame_block(cx, push_blk);
  {
    Binding *ba = env_lookup(cx, ST_ACC, 7);
    IrVal pushed = (IrVal)w;   /* the extra block param */
    IrVal pa[] = {cx->sp, ba->value, pushed};
    ba->value = emit_rt_call(cx, "jacl_vec_push", pa, 3);
    fill_frame(cx, frame);
    irb_br(cx->f, cx->cur, next_blk, frame, w);
  }

  /* next: advance (vec mode) and loop */
  enter_frame_block(cx, next_blk);
  {
    if (!is_gen) {
      Binding *bi = env_lookup(cx, ST_IDX, 7);
      IrVal one = irb_const_i64(cx->f, cx->cur, jaclval_i32(1));
      bi->value = emit_binop_call(cx, "jacl_add", bi->value, one);
    }
    fill_frame(cx, frame);
    irb_br(cx->f, cx->cur, header, frame, w);
  }

  enter_frame_block(cx, exit_blk);
  Binding *ba = env_lookup(cx, ST_ACC, 7);
  IrVal out = ba->value;
  scope_exit(cx);
  return out;
}

/* filter/transform over a MAP: iterate its entries (i-th key/value), call the 2-param
 * callback cb(k, v), and build a NEW map. filter keeps (k, v) where cb is truthy; transform
 * maps each entry to cb's returned `[vec newk newv]` pair. Mirrors the old VM, which
 * dispatches filter/transform on the source's runtime type (map -> map, vec -> vec). */
static IrVal stream_into_map(Cx *cx, IrVal src, IrVal clo, int is_filter) {
  scope_enter(cx);
  IrVal ea[] = {cx->sp};
  IrVal acc0 = emit_rt_call(cx, "jacl_map_empty", ea, 1);
  env_define(cx, ST_SRC, 7, src, 0, 0);
  env_define(cx, ST_ACC, 7, acc0, 1, 0);
  env_define(cx, ST_CLO, 7, clo, 0, 0);
  env_define(cx, ST_IDX, 7, irb_const_i64(cx->f, cx->cur, jaclval_i32(0)), 1, 0);
  if (cx->failed || !frame_guard(cx)) { scope_exit(cx); return 0; }

  int w = frame_width(cx);
  IrBlock header = new_i64_block(cx, w);
  IrBlock body_blk = new_i64_block(cx, w);
  IrBlock next_blk = new_i64_block(cx, w);
  IrBlock exit_blk = new_i64_block(cx, w);
  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  /* header: i < len(src) (entry count) */
  enter_frame_block(cx, header);
  {
    Binding *bs = env_lookup(cx, ST_SRC, 7);
    Binding *bi = env_lookup(cx, ST_IDX, 7);
    IrVal la[] = {cx->sp, bs->value};
    IrVal len = emit_rt_call(cx, "jacl_len", la, 2);
    IrVal cond = emit_binop_call(cx, "jacl_lt", bi->value, len);
    IrVal ctrue = irb_const_i64(cx->f, cx->cur, JACLVAL_TRUE);
    IrVal is_true = irb_intcmp(cx->f, cx->cur, IRB_I64, IRB_EQ, cond, ctrue);
    fill_frame(cx, frame);
    irb_br_if(cx->f, cx->cur, is_true, body_blk, frame, w, exit_blk, frame, w);
  }

  /* body: k/v = i-th entry; r = cb(sp, cb, k, v) */
  enter_frame_block(cx, body_blk);
  {
    Binding *bs = env_lookup(cx, ST_SRC, 7);
    Binding *bi = env_lookup(cx, ST_IDX, 7);
    Binding *bc = env_lookup(cx, ST_CLO, 7);
    IrVal ka[] = {cx->sp, bs->value, bi->value};
    IrVal key = emit_rt_call(cx, "jacl_iter_key_at", ka, 3);
    IrVal va[] = {cx->sp, bs->value, bi->value};
    IrVal val = emit_rt_call(cx, "jacl_iter_val_at", va, 3);
    IrVal fa[] = {cx->sp, bc->value};
    IrVal fn = emit_rt_call(cx, "jacl_closure_fn", fa, 2);
    IrVal fnw = irb_convert(cx->f, cx->cur, IRB_WRAP_I64, fn);
    IrType sig[] = {IRB_I64, IRB_I64, IRB_I64, IRB_I64};
    IrType r1[] = {IRB_I64};
    IrVal cargs[] = {cx->sp, bc->value, key, val};
    IrVal r = irb_call_indirect(cx->f, cx->cur, sig, 4, r1, 1, fnw, cargs, 4);
    if (is_filter) {
      /* keep the ORIGINAL (k, v) when cb is truthy. Recompute k/v in the keep block from
       * the frame-carried src + i (block-local SSA — body's key/val aren't visible there). */
      IrVal truth = emit_truthy(cx, r);
      IrBlock keep_blk = new_i64_block(cx, w);
      fill_frame(cx, frame);
      irb_br_if(cx->f, cx->cur, truth, keep_blk, frame, w, next_blk, frame, w);
      enter_frame_block(cx, keep_blk);
      Binding *ks = env_lookup(cx, ST_SRC, 7);
      Binding *ki = env_lookup(cx, ST_IDX, 7);
      Binding *ba = env_lookup(cx, ST_ACC, 7);
      IrVal ka2[] = {cx->sp, ks->value, ki->value};
      IrVal k2 = emit_rt_call(cx, "jacl_iter_key_at", ka2, 3);
      IrVal va2[] = {cx->sp, ks->value, ki->value};
      IrVal v2 = emit_rt_call(cx, "jacl_iter_val_at", va2, 3);
      IrVal sa[] = {cx->sp, ba->value, k2, v2};
      ba->value = emit_rt_call(cx, "jacl_map_set", sa, 4);
      fill_frame(cx, frame);
      irb_br(cx->f, cx->cur, next_blk, frame, w);
    } else {
      /* transform: cb returned [vec newk newv]; put (newk, newv) into the result map. */
      Binding *ba = env_lookup(cx, ST_ACC, 7);
      IrVal g0[] = {cx->sp, r, irb_const_i64(cx->f, cx->cur, jaclval_i32(0))};
      IrVal nk = emit_rt_call(cx, "jacl_vec_get_at", g0, 3);
      IrVal g1[] = {cx->sp, r, irb_const_i64(cx->f, cx->cur, jaclval_i32(1))};
      IrVal nv = emit_rt_call(cx, "jacl_vec_get_at", g1, 3);
      IrVal sa[] = {cx->sp, ba->value, nk, nv};
      ba->value = emit_rt_call(cx, "jacl_map_set", sa, 4);
      fill_frame(cx, frame);
      irb_br(cx->f, cx->cur, next_blk, frame, w);
    }
  }

  /* next: i = i + 1 */
  enter_frame_block(cx, next_blk);
  {
    Binding *bi = env_lookup(cx, ST_IDX, 7);
    IrVal one = irb_const_i64(cx->f, cx->cur, jaclval_i32(1));
    bi->value = emit_binop_call(cx, "jacl_add", bi->value, one);
    fill_frame(cx, frame);
    irb_br(cx->f, cx->cur, header, frame, w);
  }

  enter_frame_block(cx, exit_blk);
  IrVal out = env_lookup(cx, ST_ACC, 7)->value;
  scope_exit(cx);
  return out;
}

/* filter/transform: dispatch on the source's runtime type — a map builds a map (2-param
 * callback over key/value), anything else builds a vec via stream_into_vec. src and clo are
 * carried as frame locals so both branches can read them across the block split. */
static IrVal compile_filter_transform(Cx *cx, IrVal src, IrVal clo, int is_filter) {
  scope_enter(cx);
  env_define(cx, "\x01""ft-src", 7, src, 0, 0);
  env_define(cx, "\x01""ft-clo", 7, clo, 0, 0);
  if (cx->failed || !frame_guard(cx)) { scope_exit(cx); return 0; }
  int w = frame_width(cx);
  IrVal ma[] = {cx->sp, src};
  IrVal is_map = emit_rt_call(cx, "jacl_is_map_v", ma, 2);
  IrVal truth = emit_truthy(cx, is_map);
  IrBlock map_blk = new_i64_block(cx, w);
  IrBlock vec_blk = new_i64_block(cx, w);
  IrBlock merge = new_i64_block(cx, w + 1);   /* extra param: the result collection */
  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br_if(cx->f, cx->cur, truth, map_blk, frame, w, vec_blk, frame, w);

  enter_frame_block(cx, map_blk);
  {
    IrVal s = env_lookup(cx, "\x01""ft-src", 7)->value;
    IrVal c = env_lookup(cx, "\x01""ft-clo", 7)->value;
    IrVal rm = stream_into_map(cx, s, c, is_filter);
    fill_frame(cx, frame); frame[w] = rm;
    irb_br(cx->f, cx->cur, merge, frame, w + 1);
  }
  enter_frame_block(cx, vec_blk);
  {
    IrVal s = env_lookup(cx, "\x01""ft-src", 7)->value;
    IrVal c = env_lookup(cx, "\x01""ft-clo", 7)->value;
    IrVal rv = stream_into_vec(cx, s, /*is_gen=*/0, c, is_filter);
    fill_frame(cx, frame); frame[w] = rv;
    irb_br(cx->f, cx->cur, merge, frame, w + 1);
  }
  enter_frame_block(cx, merge);
  scope_exit(cx);
  return (IrVal)w;   /* the merge block's extra param = the result */
}

/* Statement-position error auto-return (old-VM semantics): if a non-tail statement
 * evaluates to an error-flagged value, return it from the enclosing function now.
 * Splits the current block: err path returns, cont path carries the frame on. */
static void emit_stmt_error_check(Cx *cx, IrVal v) {
  if (cx->failed || !frame_guard(cx)) return;
  IrVal ea[] = {cx->sp, v};
  IrVal e = emit_rt_call(cx, "jacl_is_error_v", ea, 2);
  IrVal truth = emit_truthy(cx, e);
  int w = frame_width(cx);
  IrBlock cont = new_i64_block(cx, w);
  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  if (cx->in_try) {
    /* inside [try …]: route the error (appended after the try-entry frame prefix)
     * to the handler block instead of returning from the function. */
    IrVal hf[IRB_MAX_FRAME + 2];
    for (int i = 0; i < cx->try_width; i++) hf[i] = frame[i];
    hf[cx->try_width] = v;
    irb_br_if(cx->f, cx->cur, truth, cx->try_target, hf, cx->try_width + 1, cont, frame, w);
  } else {
    IrBlock err_blk = new_i64_block(cx, 1);
    IrVal ef[1] = {v};
    irb_br_if(cx->f, cx->cur, truth, err_blk, ef, 1, cont, frame, w);
    cx->cur = err_blk;
    IrVal rv[1] = {(IrVal)0};      /* err_blk's single param: the error value */
    irb_return(cx->f, err_blk, rv, 1);
  }
  enter_frame_block(cx, cont);
}

/* Zero-filled fixed-size buffer (dynamic Buf: an arr with n i32-0 elements). */
static IrVal emit_type_default(Cx *cx, AstNode *tnode, int depth); /* fwd */
static IrVal emit_buf_new(Cx *cx, int32_t n, AstNode *elem_node) {
  IrVal ea[] = {cx->sp};
  IrVal av = emit_rt_call(cx, "jacl_arr_new", ea, 1);
  for (int32_t k = 0; k < n; k++) {
    /* Each slot takes its element type's zero: a struct-element buf zero-inits a fresh
     * struct per slot, a nested [Buf M U] element a fresh zero inner buffer, numeric
     * elements a plain 0. */
    IrVal zero = emit_type_default(cx, elem_node, 0);
    IrVal pa[] = {cx->sp, av, zero};
    (void)emit_rt_call(cx, "jacl_arr_push", pa, 3);
  }
  return av;
}
/* Record a typed collection annotation on the just-defined binding. */
static void bind_annotate(Cx *cx, const char *name, uint32_t len,
                          const char *et, uint32_t etl, int32_t bufn) {
  Binding *b = env_lookup(cx, name, len);
  if (!b) return;
  b->elem_type = et; b->elem_type_len = etl; b->buf_size = bufn;
}
/* The element-type token of an `[Arr T]` / `[Buf N T]` / `[Vec T]` annotation. */
static AstNode *ann_elem_type(AstNode *ann) {
  if (!ann || ann->type != AST_COMMAND || !ann->data.command.head ||
      ann->data.command.head->type != AST_LIT_STRING) return NULL;
  uint32_t hl = ann->data.command.head->data.lit_string.length;
  const char *hn = ann->data.command.head->data.lit_string.value;
  uint32_t ti = 0;
  if ((hl == 3 && (!memcmp(hn, "Arr", 3) || !memcmp(hn, "Vec", 3)))) ti = 0;
  else if (hl == 3 && !memcmp(hn, "Buf", 3)) ti = 1;      /* [Buf N T] */
  else return NULL;
  if (ann->data.command.arg_count <= ti) return NULL;
  AstNode *t = ann->data.command.args[ti];
  return (t->type == AST_LIT_STRING) ? t : NULL;
}
/* Statically check a literal element against a declared numeric/str element type.
 * Only literals are checked (everything else is dynamic). Fails compilation with
 * old-VM-shaped messages on a provable mismatch. */
static int check_elem_literal(Cx *cx, AstNode *e, const char *et, uint32_t etl) {
  int decl_str = (etl == 3 && memcmp(et, "str", 3) == 0);
  /* Only enforce for types we understand statically: numerics + str. `dyn` (and any
   * compound/unknown type) accepts everything. */
  if (!decl_str && !cg_is_type_kw(et, etl)) return 1;
  if (etl == 3 && memcmp(et, "dyn", 3) == 0) return 1;
  if (e->type == AST_LIT_INT && decl_str) {
    char msg[128];
    snprintf(msg, sizeof msg, "element type i32 does not match the declared %.*s element",
             (int)etl, et);
    cx_fail(cx, msg);
    return 0;
  }
  if (e->type == AST_LIT_STRING && !decl_str) {
    char msg[160];
    snprintf(msg, sizeof msg, "\"%.*s\" is not a %.*s value",
             (int)e->data.lit_string.length, e->data.lit_string.value, (int)etl, et);
    cx_fail(cx, msg);
    return 0;
  }
  return 1;
}

/* Is this annotation command a `[Buf N T]`? Returns N (or -1). */
static int32_t buf_ann_size(AstNode *ann) {
  if (!ann || ann->type != AST_COMMAND || !ann->data.command.head ||
      ann->data.command.head->type != AST_LIT_STRING) return -1;
  if (ann->data.command.head->data.lit_string.length != 3 ||
      memcmp(ann->data.command.head->data.lit_string.value, "Buf", 3) != 0) return -1;
  if (ann->data.command.arg_count < 1 || ann->data.command.args[0]->type != AST_LIT_INT) return -1;
  return (int32_t)ann->data.command.args[0]->data.lit_int.value;
}

/* Flag which params are declared `[Buf N T]` — those are passed by value (the callee
 * works on a copy). Walks the param tokens the same way extract_param_names_v counts them,
 * so is_buf[k] lines up with param k. */
static void scan_buf_params(AstNode *plist, int *is_buf, int cap) {
  for (int j = 0; j < cap; j++) is_buf[j] = 0;
  if (!plist || plist->type != AST_COMMAND) return;
  AstNode *toks[CG_MAX_PARAMS * 2]; int nt = 0;
  AstNode *h = plist->data.command.head;
  if (h) toks[nt++] = h;
  for (uint32_t i = 0; i < plist->data.command.arg_count && nt < CG_MAX_PARAMS * 2; i++)
    toks[nt++] = plist->data.command.args[i];
  int np = 0, i = 0;
  while (i < nt && np < cap) {
    if (toks[i]->type == AST_LIT_STRING && toks[i]->data.lit_string.length == 2 &&
        memcmp(toks[i]->data.lit_string.value, "..", 2) == 0) { i += 1; if (i >= nt) break; }
    int buf = 0;
    if (toks[i]->type == AST_COMMAND && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) {
      if (buf_ann_size(toks[i]) >= 0) buf = 1;   /* `[Buf N T] name` */
      i += 1;                                     /* skip the compound type; toks[i] is the name */
    }
    if (i >= nt || toks[i]->type != AST_LIT_STRING) return;
    const char *s = toks[i]->data.lit_string.value; uint32_t n = toks[i]->data.lit_string.length;
    if (cg_is_type_prefix(s, n) && i + 1 < nt && toks[i + 1]->type == AST_LIT_STRING) i += 2;
    else i += 1;
    is_buf[np++] = buf;
  }
}

/* Is `h` a type-constructor head — `[Arr T]` / `[Vec T]` / `[Map K V]` / `[Buf N T]`?
 * These share the AST_COMMAND-head shape with a closure-valued expression head
 * (e.g. `[vec-get $fns i]`), so the direct-call path must skip them and let the typed
 * collection-constructor path handle them. */
static int is_type_ctor_head(AstNode *h) {
  if (!h || h->type != AST_COMMAND) return 0;
  AstNode *ih = h->data.command.head;
  if (ih && ih->type == AST_LIT_STRING && ih->data.lit_string.length == 3) {
    const char *s = ih->data.lit_string.value;
    if (!memcmp(s, "Arr", 3) || !memcmp(s, "Vec", 3) || !memcmp(s, "Map", 3)) return 1;
  }
  return buf_ann_size(h) >= 0;   /* [Buf N T] */
}

/* The element type NODE of an `[Arr T]`/`[Vec T]`/`[Buf N T]` annotation — unlike
 * ann_elem_type, this returns compound element types (e.g. a nested `[Buf M U]`) too. */
static AstNode *ann_elem_node(AstNode *ann) {
  if (!ann || ann->type != AST_COMMAND || !ann->data.command.head ||
      ann->data.command.head->type != AST_LIT_STRING) return NULL;
  uint32_t hl = ann->data.command.head->data.lit_string.length;
  const char *hn = ann->data.command.head->data.lit_string.value;
  uint32_t ti;
  if (hl == 3 && (memcmp(hn, "Arr", 3) == 0 || memcmp(hn, "Vec", 3) == 0)) ti = 0;
  else if (hl == 3 && memcmp(hn, "Buf", 3) == 0) ti = 1;   /* [Buf N T] */
  else return NULL;
  return ann->data.command.arg_count > ti ? ann->data.command.args[ti] : NULL;
}

/* The zero value for a declared type NODE: a scalar/str/struct name via the field-
 * default path, and a nested `[Buf M U]` as a fresh zero inner buffer (recursively). */
static IrVal emit_type_default(Cx *cx, AstNode *tnode, int depth) {
  if (!tnode || depth > 8) return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
  if (tnode->type == AST_LIT_STRING)
    return emit_field_default(cx, tnode->data.lit_string.value,
                              tnode->data.lit_string.length, depth);
  if (tnode->type == AST_COMMAND) {
    int32_t m = buf_ann_size(tnode);
    if (m >= 0) {
      AstNode *inner = ann_elem_node(tnode);
      IrVal ea[] = {cx->sp};
      IrVal av = emit_rt_call(cx, "jacl_arr_new", ea, 1);
      for (int32_t k = 0; k < m; k++) {
        IrVal e = emit_type_default(cx, inner, depth + 1);
        IrVal pa[] = {cx->sp, av, e};
        (void)emit_rt_call(cx, "jacl_arr_push", pa, 3);
      }
      return av;
    }
  }
  return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
}

#define FOR_IDX_SENTINEL "\x01""for-idx"
#define FOR_IDX_SENTINEL_LEN 8
#define FOR_COLL_SENTINEL "\x01""for-coll"
#define FOR_COLL_SENTINEL_LEN 9
#define BRK_SENTINEL "\x01""brk"
#define BRK_SENTINEL_LEN 4

/* `for COLL name { body }` (bind each element) / `for COLL $cb` (call a closure per
 * element): an index loop over a VECTOR — i in 0..len, elem = coll[i]. The header
 * exits when `i < len` is false OR errors (a non-vector's jacl_len yields error, so
 * iteration over an unsupported collection terminates instead of spinning). */
static IrVal compile_for_each(Cx *cx, AstNode *coll_node, const char *name, uint32_t nlen,
                              AstNode *body, AstNode *cb_node,
                              const char *key_name, uint32_t klen) {
  scope_enter(cx);
  /* A generator source (`for [gen] $cb`, `for [gen] k v {body}`) has no jacl_len — drain
   * it into a vector first so the index-based iteration below sees the elements. The
   * canonical single-name generator forms are handled earlier by compile_for_generator;
   * this covers the callback and two-name shapes. */
  IrVal coll;
  if (generator_call(cx, coll_node)) {
    IrVal g = compile_expr(cx, coll_node);
    if (cx->failed) { scope_exit(cx); return 0; }
    coll = stream_into_vec(cx, g, /*is_gen=*/1, /*clo=*/0, /*is_filter=*/0);
  } else {
    coll = compile_expr(cx, coll_node);
  }
  if (cx->failed) { scope_exit(cx); return 0; }
  IrVal cb = 0;
  if (cb_node) {
    cb = compile_expr(cx, cb_node);
    if (cx->failed) { scope_exit(cx); return 0; }
  }
  IrVal la[] = {cx->sp, coll};
  IrVal len = emit_rt_call(cx, "jacl_len", la, 2);
  IrVal zero = irb_const_i64(cx->f, cx->cur, jaclval_i32(0));
  env_define(cx, FOR_IDX_SENTINEL, FOR_IDX_SENTINEL_LEN, zero, /*is_mut=*/1, /*is_cell=*/0);
  env_define(cx, FOR_END_SENTINEL, FOR_END_SENTINEL_LEN, len, 0, 0);
  env_define(cx, FOR_COLL_SENTINEL, FOR_COLL_SENTINEL_LEN, coll, 0, 0);
  if (cb_node) env_define(cx, "\x01""for-cb", 7, cb, 0, 0);
  /* Two-name form `for COLL k v { body }` binds the key alongside the value. */
  if (key_name) env_define(cx, key_name, klen, zero, /*is_mut=*/1, /*is_cell=*/0);
  if (name) env_define(cx, name, nlen, zero, /*is_mut=*/1, /*is_cell=*/0);
  /* `break V` result slot: a hidden mutable local (nil by default) that a `break`
   * inside the body writes; the loop evaluates to it. Threaded like any frame local. */
  env_define(cx, BRK_SENTINEL, BRK_SENTINEL_LEN, irb_const_i64(cx->f, cx->cur, JACLVAL_NIL),
             /*is_mut=*/1, /*is_cell=*/0);
  if (cx->failed || !frame_guard(cx)) { scope_exit(cx); return 0; }

  int w = frame_width(cx);
  IrBlock header = new_i64_block(cx, w);
  IrBlock body_blk = new_i64_block(cx, w);
  IrBlock step_blk = new_i64_block(cx, w);
  IrBlock exit_blk = new_i64_block(cx, w);
  IrVal frame[IRB_MAX_FRAME + 2];
  fill_frame(cx, frame);
  irb_br(cx->f, cx->cur, header, frame, w);

  /* header: continue iff (i < len) is exactly true (an error compare exits) */
  enter_frame_block(cx, header);
  {
    Binding *bi = env_lookup(cx, FOR_IDX_SENTINEL, FOR_IDX_SENTINEL_LEN);
    Binding *be = env_lookup(cx, FOR_END_SENTINEL, FOR_END_SENTINEL_LEN);
    IrVal cond = emit_binop_call(cx, "jacl_lt", bi->value, be->value);
    IrVal ctrue = irb_const_i64(cx->f, cx->cur, JACLVAL_TRUE);
    IrVal is_true = irb_intcmp(cx->f, cx->cur, IRB_I64, IRB_EQ, cond, ctrue);
    fill_frame(cx, frame);
    irb_br_if(cx->f, cx->cur, is_true, body_blk, frame, w, exit_blk, frame, w);
  }

  /* body: elem = coll[i]; run body / call cb; i = i + 1 */
  enter_frame_block(cx, body_blk);
  {
    Binding *bi = env_lookup(cx, FOR_IDX_SENTINEL, FOR_IDX_SENTINEL_LEN);
    Binding *bc = env_lookup(cx, FOR_COLL_SENTINEL, FOR_COLL_SENTINEL_LEN);
    /* iter-val-at handles vec/arr (index -> element) and map (i-th value) uniformly. */
    IrVal ga[] = {cx->sp, bc->value, bi->value};
    IrVal elem = emit_rt_call(cx, "jacl_iter_val_at", ga, 3);
    int si = cx->in_loop; IrBlock sc = cx->loop_continue, sb2 = cx->loop_break; int sw = cx->loop_width; int sk = cx->loop_brk_ok;
    cx->in_loop = 1; cx->loop_continue = step_blk; cx->loop_break = exit_blk; cx->loop_width = w; cx->loop_brk_ok = 1;
    if (name) {
      if (key_name) {   /* two-name: bind the key to the i-th key (map key / vec index) */
        IrVal ka[] = {cx->sp, bc->value, bi->value};
        env_lookup(cx, key_name, klen)->value = emit_rt_call(cx, "jacl_iter_key_at", ka, 3);
      }
      Binding *bn = env_lookup(cx, name, nlen);
      bn->value = elem;
      (void)compile_expr(cx, body);
    } else {
      Binding *bcb = env_lookup(cx, "\x01""for-cb", 7);
      /* A 2-param callback `cb {k, v}` gets (key, value) — over a map the key/value pair,
       * over a vec/arr the index/element; a 1-param callback gets just the value. Read the
       * arity from the callback proc when it is a named proc value; default to 1. */
      int cbarity = 1;
      if (cb_node->type == AST_VAR_REF) {
        Proc *cp = proc_lookup(cx, cb_node->data.var_ref.name, cb_node->data.var_ref.length);
        if (cp && !cp->variadic && cp->arity >= 1 && cp->arity <= 2) cbarity = cp->arity;
      }
      IrVal fa[] = {cx->sp, bcb->value};
      IrVal fn = emit_rt_call(cx, "jacl_closure_fn", fa, 2);
      IrVal fnw = irb_convert(cx->f, cx->cur, IRB_WRAP_I64, fn);
      IrType r1[] = {IRB_I64};
      if (cbarity == 2) {
        IrVal ka[] = {cx->sp, bc->value, bi->value};
        IrVal key = emit_rt_call(cx, "jacl_iter_key_at", ka, 3);
        IrType sig[] = {IRB_I64, IRB_I64, IRB_I64, IRB_I64};
        IrVal cargs[] = {cx->sp, bcb->value, key, elem};
        (void)irb_call_indirect(cx->f, cx->cur, sig, 4, r1, 1, fnw, cargs, 4);
      } else {
        IrType sig[] = {IRB_I64, IRB_I64, IRB_I64};
        IrVal cargs[] = {cx->sp, bcb->value, elem};
        (void)irb_call_indirect(cx->f, cx->cur, sig, 3, r1, 1, fnw, cargs, 3);
      }
    }
    cx->in_loop = si; cx->loop_continue = sc; cx->loop_break = sb2; cx->loop_width = sw; cx->loop_brk_ok = sk;
    if (cx->failed) { scope_exit(cx); return 0; }
    fill_frame(cx, frame);
    irb_br(cx->f, cx->cur, step_blk, frame, w);
  }

  /* step: i = i + 1, back to the header */
  enter_frame_block(cx, step_blk);
  {
    Binding *bi = env_lookup(cx, FOR_IDX_SENTINEL, FOR_IDX_SENTINEL_LEN);
    IrVal one = irb_const_i64(cx->f, cx->cur, jaclval_i32(1));
    bi->value = emit_binop_call(cx, "jacl_add", bi->value, one);
    fill_frame(cx, frame);
    irb_br(cx->f, cx->cur, header, frame, w);
  }

  enter_frame_block(cx, exit_blk);
  IrVal brkv = env_lookup(cx, BRK_SENTINEL, BRK_SENTINEL_LEN)->value;   /* break V result */
  scope_exit(cx);
  return brkv;
}

/* [try { body } e { handler }] — the body's value (or any statement-position
 * error inside it) routes to the handler with `e` bound to the raw error value;
 * otherwise try evaluates to the body's value. Join carries the result. */
static IrVal compile_try(Cx *cx, AstNode *node) {
  uint32_t argc = node->data.command.arg_count;
  AstNode **args = node->data.command.args;
  if (argc != 3) {
    char msg[128];
    snprintf(msg, sizeof msg, "builtin 'try' expects 3 arguments but got %u", argc);
    cx_fail(cx, msg);
    return 0;
  }
  if (args[0]->type != AST_BLOCK || args[1]->type != AST_LIT_STRING ||
      args[2]->type != AST_BLOCK) {
    cx_fail(cx, "try needs `{ body } name { handler }`");
    return 0;
  }
  if (!frame_guard(cx)) return 0;
  int w = frame_width(cx);
  IrBlock err_h = new_i64_block(cx, w + 1);      /* frame + the error value */
  IrBlock join = new_i64_block(cx, w + 1);       /* frame + the try result  */
  IrVal frame[IRB_MAX_FRAME + 2];

  int st = cx->in_try; IrBlock stt = cx->try_target; int stw = cx->try_width;
  cx->in_try = 1; cx->try_target = err_h; cx->try_width = w;
  IrVal bv = compile_expr(cx, args[0]);
  cx->in_try = st; cx->try_target = stt; cx->try_width = stw;
  if (cx->failed) return 0;
  /* the body VALUE may itself be an error: route it too */
  IrVal ea[] = {cx->sp, bv};
  IrVal e = emit_rt_call(cx, "jacl_is_error_v", ea, 2);
  IrVal truth = emit_truthy(cx, e);
  fill_frame(cx, frame);
  frame[w] = bv;
  irb_br_if(cx->f, cx->cur, truth, err_h, frame, w + 1, join, frame, w + 1);

  /* handler: e = error value (flag intact, so error-val / error? work on it) */
  enter_frame_block(cx, err_h);
  scope_enter(cx);
  env_define(cx, args[1]->data.lit_string.value, args[1]->data.lit_string.length,
             (IrVal)w, /*is_mut=*/0, /*is_cell=*/0);
  IrVal hv = compile_expr(cx, args[2]);
  scope_exit(cx);
  if (cx->failed) return 0;
  fill_frame(cx, frame);
  frame[w] = hv;
  irb_br(cx->f, cx->cur, join, frame, w + 1);

  enter_frame_block(cx, join);
  return (IrVal)w;                                /* the join's result param */
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
  /* `for COLL k v { body }` — two-name iteration (map: key + value; vec/arr: index +
   * element). Both names bind per iteration via the iter-key/iter-val accessors. */
  if (argc == 4 && args[1]->type == AST_LIT_STRING && args[2]->type == AST_LIT_STRING &&
      args[3]->type == AST_BLOCK) {
    return compile_for_each(cx, args[0], args[2]->data.lit_string.value,
                            args[2]->data.lit_string.length, args[3], NULL,
                            args[1]->data.lit_string.value, args[1]->data.lit_string.length);
  }
  /* `for COLL name { body }` — element iteration (COLL is any expression). */
  if (argc == 3 && args[1]->type == AST_LIT_STRING && args[2]->type == AST_BLOCK) {
    return compile_for_each(cx, args[0], args[1]->data.lit_string.value,
                            args[1]->data.lit_string.length, args[2], NULL, NULL, 0);
  }
  /* `for COLL { body }` — implicit `$it` element binding (generator or vec-like). */
  if (argc == 2 && args[1]->type == AST_BLOCK) {
    if (generator_call(cx, args[0]))
      return compile_for_generator(cx, args[0], "it", 2, args[1]);
    return compile_for_each(cx, args[0], "it", 2, args[1], NULL, NULL, 0);
  }
  /* `for COLL $cb` — call a closure per element. */
  if (argc == 2 && args[1]->type != AST_BLOCK && args[1]->type != AST_LIT_STRING) {
    return compile_for_each(cx, args[0], NULL, 0, NULL, args[1], NULL, 0);
  }
  cx_fail(cx, "for requires: $collection { body }, $collection [enum] value { body }, or $collection $callback");
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

/* A top-level proc referenced as a VALUE (`$cb`): a zero-capture closure over a
 * lazily-created adapter with the closure ABI (sp, self, a…) that calls the proc
 * (sp, a…). Built once per proc; safe mid-compilation (saves/restores cx). */
static IrFunc *proc_value_wrapper(Cx *cx, Proc *p) {
  if (p->wrapper) return p->wrapper;
  IrType pt[2 + CG_MAX_PARAMS];
  IrType r1[] = {IRB_I64};
  int n = 2 + p->arity;
  for (int k = 0; k < n; k++) pt[k] = IRB_I64;
  IrFunc *w = irb_func_new(cx->m, pt, n, r1, 1);
  IrFunc *sf = cx->f; IrBlock sb = cx->cur; IrVal ssp = cx->sp;
  cx->f = w;
  cx->cur = irb_block(w, pt, n);
  IrVal cargs[1 + CG_MAX_PARAMS];
  cargs[0] = 0;                                     /* sp (param 0; self at 1 is dropped) */
  for (int k = 0; k < p->arity; k++) cargs[1 + k] = (IrVal)(2 + k);
  IrVal r = irb_call(w, cx->cur, p->func, cargs, 1 + p->arity);
  irb_return(w, cx->cur, &r, 1);
  p->wrapper = w;
  cx->f = sf; cx->cur = sb; cx->sp = ssp;
  return w;
}

/* Lower one expression to an i64 SSA value (a JaclVal) in the current block. */
static IrVal compile_expr(Cx *cx, AstNode *node) {
  if (cx->failed) return 0;
  switch (node->type) {
    case AST_LIT_INT:
      return irb_const_i64(cx->f, cx->cur, jaclval_i32(node->data.lit_int.value));

    case AST_LIT_FLOAT: {
      /* inline f32 JaclVal: tag 0x03 over the float's bit pattern */
      union { float f; uint32_t u; } fb;
      fb.f = (float)node->data.lit_float.value;
      return irb_const_i64(cx->f, cx->cur, (int64_t)(((uint64_t)0x03 << 56) | fb.u));
    }

    case AST_VAR_REF: {
      Binding *bd = env_lookup(cx, node->data.var_ref.name, node->data.var_ref.length);
      if (!bd) {
        const char *nm = node->data.var_ref.name; uint32_t nl = node->data.var_ref.length;
        /* Bare constant words: true / false / nil. */
        if (nl == 4 && memcmp(nm, "true", 4) == 0)  return irb_const_i64(cx->f, cx->cur, JACLVAL_TRUE);
        if (nl == 5 && memcmp(nm, "false", 5) == 0) return irb_const_i64(cx->f, cx->cur, JACLVAL_FALSE);
        if (nl == 3 && memcmp(nm, "nil", 3) == 0)   return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
        if (nl == 3 && memcmp(nm, "ctx", 3) == 0) {   /* the ambient context map */
          IrVal a[] = {cx->sp};
          return emit_rt_call(cx, "jacl_ctx_get", a, 1);
        }
        /* A top-level proc as a value (`$cb`): a zero-capture closure over its adapter. */
        Proc *pv = proc_lookup(cx, nm, nl);
        if (pv && !pv->is_generator) {
          IrFunc *w = proc_value_wrapper(cx, pv);
          IrVal fnref = irb_ref_func(cx->f, cx->cur, w);
          IrVal fnref64 = irb_convert(cx->f, cx->cur, IRB_EXTEND_I32U, fnref);
          IrVal nupv = irb_const_i64(cx->f, cx->cur, 0);
          IrVal na[] = {cx->sp, fnref64, nupv};
          IrVal clos = emit_rt_call(cx, "jacl_closure_new", na, 3);
          /* Register the proc's signature so `print $proc` shows `<proc name(types) -> ret>`. */
          {
            uint32_t pargc = pv->def->data.command.arg_count;
            AstNode *params = pv->def->data.command.args[1];
            AstNode *ret_ann = (pargc >= 4) ? pv->def->data.command.args[pargc - 2] : NULL;
            char sig[256];
            size_t sl = format_proc_sig(sig, sizeof sig, pv->name, pv->len, /*is_closure=*/0, params, ret_ann);
            IrVal sigv = compile_string_literal(cx, sig, (uint32_t)sl);
            IrVal ra[] = {cx->sp, fnref64, sigv};
            (void)emit_rt_call(cx, "jacl_proc_sig_register", ra, 3);
          }
          return clos;
        }
        /* A top-level (module-global) binding, read from anywhere via the global map. */
        if (is_global_name(cx, nm, nl)) {
          IrVal key = compile_string_literal(cx, nm, nl);
          IrVal a[] = {cx->sp, key};
          return emit_rt_call(cx, "jacl_global_get", a, 2);
        }
        cx_failf(cx, "codegen: undefined variable '%.*s'", nm, nl);
        return 0;
      }
      if (bd->is_cell) { IrVal a[] = {cx->sp, bd->value}; return emit_rt_call(cx, "jacl_cell_get", a, 2); }
      return bd->value;
    }

    case AST_BREAK:
    case AST_CONTINUE: {
      if (!cx->in_loop) { cx_fail(cx, "break/continue outside a loop"); return 0; }
      /* `break V` — write the loop's hidden result slot before branching (only when the
       * innermost loop carries one; the value is captured into the exit-edge frame). */
      if (node->type == AST_BREAK && node->data.break_stmt.value && cx->loop_brk_ok) {
        IrVal bv = compile_expr(cx, node->data.break_stmt.value);
        if (cx->failed) return 0;
        Binding *bb = env_lookup(cx, BRK_SENTINEL, BRK_SENTINEL_LEN);
        if (bb) bb->value = bv;
      }
      IrVal frame[IRB_MAX_FRAME + 2];
      fill_frame(cx, frame);   /* first loop_width slots align with the loop's frame */
      irb_br(cx->f, cx->cur, node->type == AST_BREAK ? cx->loop_break : cx->loop_continue,
             frame, cx->loop_width);
      /* Continue emitting into an unreachable continuation block with the current
       * frame shape, so an enclosing if/loop join can still terminate normally. */
      IrBlock dead = new_i64_block(cx, frame_width(cx));
      enter_frame_block(cx, dead);
      return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
    }

    case AST_CTX_DECL: {
      /* `ctx [mut] TYPE name DEFAULT` — install the field into the ambient context. */
      IrVal dv = node->data.ctx_decl.default_expr
                     ? compile_expr(cx, node->data.ctx_decl.default_expr)
                     : irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
      if (cx->failed) return 0;
      IrVal fname = compile_string_literal(cx, node->data.ctx_decl.field_name,
                                           node->data.ctx_decl.field_name_len);
      IrVal a[] = {cx->sp, fname, dv};
      return emit_rt_call(cx, "jacl_ctx_set_field", a, 3);
    }

    case AST_DEFSTRUCT:
      sdef_register(cx, node);
      return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);

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
        if (i + 1 < bn) emit_stmt_error_check(cx, last);   /* error auto-return */
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
      if (hid == HEAD_TRY) return compile_try(cx, node);
      if (hid == HEAD_WITH_CTX && node->data.command.arg_count == 2 &&
          node->data.command.args[0]->type == AST_BLOCK &&
          node->data.command.args[1]->type == AST_BLOCK) {
        /* [with-ctx {field VAL …} { body }] — override fields for the block, restore
         * after. The saved `old` context must survive the body's control flow (a nested
         * with-ctx / if / loop opens new blocks), so it is bound as a frame-threaded
         * local rather than a bare SSA value in the entry block. */
        scope_enter(cx);
        IrVal ga[] = {cx->sp};
        IrVal old = emit_rt_call(cx, "jacl_ctx_get", ga, 1);
        env_define(cx, "\x01""wctx-old", 9, old, /*is_mut=*/0, /*is_cell=*/0);
        AstNode *ov = node->data.command.args[0];
        for (uint32_t i = 0; i < ov->data.block.count; i++) {
          AstNode *cmd = ov->data.block.commands[i];
          if (cmd->type != AST_COMMAND || !cmd->data.command.head ||
              cmd->data.command.head->type != AST_LIT_STRING || cmd->data.command.arg_count != 1) {
            cx_fail(cx, "with-ctx overrides must be `field VALUE` pairs");
            scope_exit(cx); return 0;
          }
          IrVal fname = compile_string_literal(cx, cmd->data.command.head->data.lit_string.value,
                                               cmd->data.command.head->data.lit_string.length);
          IrVal v = compile_expr(cx, cmd->data.command.args[0]);
          if (cx->failed) { scope_exit(cx); return 0; }
          IrVal sa[] = {cx->sp, fname, v};
          (void)emit_rt_call(cx, "jacl_ctx_set_field", sa, 3);
        }
        IrVal bv = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) { scope_exit(cx); return 0; }
        IrVal ra[] = {cx->sp, env_lookup(cx, "\x01""wctx-old", 9)->value};
        (void)emit_rt_call(cx, "jacl_ctx_swap", ra, 2);
        scope_exit(cx);
        return bv;
      }

      /* bracket-form `[return]` / `[return V]`. A value-returning return inside a
       * generator is illegal (stream consumers discard it). A top-level bare `[return]`
       * as a block statement is intercepted by compile_tail's block loop (which ends the
       * body / exhausts the generator); reaching here means a nested/mid-expression
       * position, where we mirror AST_RETURN's fallback: evaluate to the value (or nil). */
      if (hid == HEAD_RETURN) {
        AstNode *rv = node->data.command.arg_count >= 1 ? node->data.command.args[0] : NULL;
        if (cx->cur_is_generator && rv) {
          cx_fail(cx, "cannot return a value from a generator (proc contains `yield`)");
          return 0;
        }
        return rv ? compile_expr(cx, rv) : irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
      }

      /* `yield V` — suspend the current fiber, yielding V; result is the resume arg. */
      if (hid == HEAD_YIELD) {
        IrVal v = (node->data.command.arg_count >= 1)
                      ? compile_expr(cx, node->data.command.args[0])
                      : irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
        if (cx->failed) return 0;
        return irb_suspend(cx->f, cx->cur, v);
      }

      /* `[assert-type EXPR TYPE]` — a purely static check (the compiler proves the
       * inferred type matches). It has NO runtime effect and, crucially, must NOT
       * evaluate EXPR — so codegen drops it entirely, yielding nil. */
      if (hid == HEAD_ASSERT_TYPE) {
        return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
      }

      /* `print V` — write V's text + newline to stdout via the powerbox Stream capability
       * (jacl_print → libc write → Stream.write). The harness wraps the program with svm's
       * synth_powerbox_start, so the stdout handle is stashed before the program runs. */
      if (hid == HEAD_PRINT) {
        IrVal v = (node->data.command.arg_count >= 1)
                      ? compile_expr(cx, node->data.command.args[0])
                      : irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, v};
        return emit_rt_call(cx, "jacl_print", a, 2);
      }

      /* `spawn { block }` — the block is a 0-param closure (capturing free vars) run on
       * a task fiber; returns a future. */
      if (hid == HEAD_SPAWN) {
        if (node->data.command.arg_count != 1 || node->data.command.args[0]->type != AST_BLOCK) {
          cx_fail(cx, "spawn needs a single { block }"); return 0;
        }
        IrVal clos = compile_closure(cx, NULL, 0, node->data.command.args[0], NULL);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, clos};
        return emit_rt_call(cx, "jacl_spawn", a, 2);
      }
      /* `await $f` — suspend THIS fiber with the future's raw job pointer (the STREAM tag
       * masked off); the scheduler resumes it in place with the result. Emitting the
       * `suspend` op inline — rather than suspending inside the jacl_await import — is what
       * makes the resume continue past the await instead of replaying the fiber's prefix
       * (a suspend that unwinds through an import frame restarts the caller on resume; a
       * direct suspend op resumes at the suspend point, exactly as `yield` does). */
      if (hid == HEAD_AWAIT) {
        if (node->data.command.arg_count != 1) { cx_fail(cx, "await needs one argument"); return 0; }
        IrVal f = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal mask = irb_const_i64(cx->f, cx->cur, (int64_t)0x00FFFFFFFFFFFFFFLL);  /* JACL_PAYLOAD_MASK */
        IrVal raw = irb_intbin(cx->f, cx->cur, IRB_I64, IRB_AND, f, mask);
        return irb_suspend(cx->f, cx->cur, raw);
      }
      /* `parallel { } { } …` — run each block (a 0-param closure) on the worker pool,
       * returning a vector of their results in order. Real parallelism across vCPUs. */
      if (hid == HEAD_PARALLEL) {
        uint32_t n = node->data.command.arg_count;
        IrVal ve[] = {cx->sp};
        IrVal vec = emit_rt_call(cx, "jacl_vec_empty", ve, 1);
        for (uint32_t i = 0; i < n && i < CG_MAX_PARAMS; i++) {
          if (node->data.command.args[i]->type != AST_BLOCK) { cx_fail(cx, "parallel takes { blocks }"); return 0; }
          IrVal clos = compile_closure(cx, NULL, 0, node->data.command.args[i], NULL);
          if (cx->failed) return 0;
          IrVal pa[] = {cx->sp, vec, clos};
          vec = emit_rt_call(cx, "jacl_vec_push", pa, 3);
        }
        IrVal pa[] = {cx->sp, vec};
        return emit_rt_call(cx, "jacl_parallel", pa, 2);
      }
      /* `race { } { } …` — run each block on the pool; return the first block's result
       * (deterministic; cancellation of the losers is a follow-on). */
      if (hid == HEAD_RACE) {
        uint32_t n = node->data.command.arg_count;
        if (n == 0) return irb_const_i64(cx->f, cx->cur, JACLVAL_NIL);
        IrVal ve[] = {cx->sp};
        IrVal vec = emit_rt_call(cx, "jacl_vec_empty", ve, 1);
        for (uint32_t i = 0; i < n; i++) {
          if (node->data.command.args[i]->type != AST_BLOCK) { cx_fail(cx, "race takes { blocks }"); return 0; }
          IrVal clos = compile_closure(cx, NULL, 0, node->data.command.args[i], NULL);
          if (cx->failed) return 0;
          IrVal pa[] = {cx->sp, vec, clos};
          vec = emit_rt_call(cx, "jacl_vec_push", pa, 3);
        }
        IrVal pa[] = {cx->sp, vec};
        return emit_rt_call(cx, "jacl_race", pa, 2);
      }

      /* Closure literals. Lambda `[\ {params} {body}]`: head is the bare word "\",
       * params in a BLOCK. Anonymous proc `[proc {params} {body}]`: an empty name. */
      {
        AstNode *h = node->data.command.head;
        uint32_t argc = node->data.command.arg_count;
        if (h && h->type == AST_LIT_STRING && h->data.lit_string.length == 1 &&
            h->data.lit_string.value[0] == '\\') {
          if (argc >= 1 && node->data.command.args[0]->type != AST_BLOCK) {
            /* `[\\ * $it 2]` — implicit-`it` lambda whose body is the given command. */
            AstNode *body = synth_command(node->data.command.args[0],
                                          node->data.command.args + 1, argc - 1);
            AstNode *pn = synth_command(synth_word("it", 2), NULL, 0);
            return compile_closure(cx, pn, /*in_block=*/0, body, NULL);
          }
          if (argc != 2 || node->data.command.args[0]->type != AST_BLOCK ||
              node->data.command.args[1]->type != AST_BLOCK) {
            cx_fail(cx, "lambda must be [\\ {params} {body}]"); return 0;
          }
          return compile_closure(cx, node->data.command.args[0], /*in_block=*/1,
                                 node->data.command.args[1], NULL);
        }
        if (hid == HEAD_PROC) {
          int anon = argc >= 3 && node->data.command.args[0]->type == AST_LIT_STRING &&
                     node->data.command.args[0]->data.lit_string.length == 0;
          AstNode *ret_ann = (argc >= 4) ? node->data.command.args[argc - 2] : NULL;
          if (!anon) {
            /* Nested named proc: compile as a closure bound to the name. */
            IrVal cl = compile_closure(cx, node->data.command.args[1], /*in_block=*/0,
                                       node->data.command.args[argc - 1], ret_ann);
            if (cx->failed) return 0;
            env_define(cx, node->data.command.args[0]->data.lit_string.value,
                       node->data.command.args[0]->data.lit_string.length, cl, 0, 0);
            return cl;
          }
          return compile_closure(cx, node->data.command.args[1], /*in_block=*/0,
                                 node->data.command.args[argc - 1], ret_ann);
        }
      }

      /* Indexed arrow access `$b->7` — [. EXPR INT] over a buf/arr/vec. A constant
       * index against a fixed [Buf N T] binding is bounds-checked at compile time. */
      if (hid == HEAD_DOT && node->data.command.arg_count == 2 &&
          node->data.command.args[1]->type == AST_LIT_INT) {
        if (node->data.command.args[0]->type == AST_VAR_REF) {
          Binding *bb = env_lookup(cx, node->data.command.args[0]->data.var_ref.name,
                                   node->data.command.args[0]->data.var_ref.length);
          int32_t ix = (int32_t)node->data.command.args[1]->data.lit_int.value;
          if (bb && bb->buf_size >= 0 && (ix < 0 || ix >= bb->buf_size)) {
            char msg[128];
            snprintf(msg, sizeof msg, "buf index %d out of bounds for [Buf %d %.*s]",
                     ix, bb->buf_size, (int)bb->elem_type_len, bb->elem_type ? bb->elem_type : "");
            cx_fail(cx, msg);
            return 0;
          }
        }
        IrVal bv = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal idx = irb_const_i64(cx->f, cx->cur,
                                  jaclval_i32((int32_t)node->data.command.args[1]->data.lit_int.value));
        IrVal a[] = {cx->sp, bv, idx};
        return emit_rt_call(cx, "jacl_index_get", a, 3);
      }
      /* Dynamic dot `[. EXPR $k]` — the key expression decides index vs field. */
      if (hid == HEAD_DOT && node->data.command.arg_count == 2 &&
          node->data.command.args[1]->type != AST_LIT_STRING &&
          node->data.command.args[1]->type != AST_LIT_INT) {
        IrVal sv = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal kv = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, sv, kv};
        return emit_rt_call(cx, "jacl_dot_dyn", a, 3);
      }
      /* Field access `[. EXPR field]` (from `$e->field`). */
      if (hid == HEAD_DOT && node->data.command.arg_count == 2 &&
          node->data.command.args[1]->type == AST_LIT_STRING) {
        IrVal sv = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal fname = compile_string_literal(cx, node->data.command.args[1]->data.lit_string.value,
                                             node->data.command.args[1]->data.lit_string.length);
        IrVal a[] = {cx->sp, sv, fname};
        return emit_rt_call(cx, "jacl_field_get", a, 3);
      }
      /* 3-arg dot mutation `[. EXPR key VALUE]` (== `set EXPR->key VALUE`): a string
       * key writes a struct field, an int key writes an arr/buf element, and a dynamic
       * key routes through the runtime dispatch. */
      if (hid == HEAD_DOT && node->data.command.arg_count == 3) {
        AstNode *keyn = node->data.command.args[1];
        IrVal sv = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        if (keyn->type == AST_LIT_INT) {
          IrVal idx = irb_const_i64(cx->f, cx->cur, jaclval_i32((int32_t)keyn->data.lit_int.value));
          IrVal val = compile_expr(cx, node->data.command.args[2]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, sv, idx, val};
          return emit_rt_call(cx, "jacl_arr_set_at", a, 4);
        }
        if (keyn->type == AST_LIT_STRING) {
          IrVal fname = compile_string_literal(cx, keyn->data.lit_string.value,
                                               keyn->data.lit_string.length);
          IrVal val = compile_expr(cx, node->data.command.args[2]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, sv, fname, val};
          return emit_rt_call(cx, "jacl_struct_put", a, 4);
        }
        IrVal kv = compile_expr(cx, keyn);
        if (cx->failed) return 0;
        IrVal val = compile_expr(cx, node->data.command.args[2]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, sv, kv, val};
        return emit_rt_call(cx, "jacl_dot_dyn_set", a, 4);
      }

      /* Optional chaining `[?. EXPR key]` — nil short-circuits to nil, a map reads the
       * entry (missing -> nil). A bareword key compiles as a string (like `->`). */
      if (hid == HEAD_QDOT && node->data.command.arg_count == 2) {
        IrVal sv = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        AstNode *keyn = node->data.command.args[1];
        IrVal kv = (keyn->type == AST_LIT_STRING)
                       ? compile_string_literal(cx, keyn->data.lit_string.value,
                                                keyn->data.lit_string.length)
                       : compile_expr(cx, keyn);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, sv, kv};
        return emit_rt_call(cx, "jacl_qdot", a, 3);
      }

      /* Calling a closure value: the head is an expression (e.g. `[$f x]`). */
      /* A closure-valued head: either `[$f a…]` (var-ref) or a general expression head
       * `[[vec-get $fns i] a…]` / `[[pick-fn] a…]` (a command that evaluates to a
       * closure). Both compile the head to a value and call it through the closure ABI
       * (sp, self, args…) via call_indirect. */
      if (node->data.command.head && (node->data.command.head->type == AST_VAR_REF ||
              (node->data.command.head->type == AST_COMMAND &&
               !is_type_ctor_head(node->data.command.head)))) {
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

      /* `def [Buf N T] name` — zero-filled fixed buffer declaration (no initializer). */
      if (hid == HEAD_DEF && node->data.command.arg_count == 2 &&
          node->data.command.args[0]->type == AST_COMMAND &&
          node->data.command.args[1]->type == AST_LIT_STRING &&
          buf_ann_size(node->data.command.args[0]) >= 0) {
        IrVal bv = emit_buf_new(cx, buf_ann_size(node->data.command.args[0]),
                                ann_elem_node(node->data.command.args[0]));
        env_define(cx, node->data.command.args[1]->data.lit_string.value,
                   node->data.command.args[1]->data.lit_string.length, bv, 0, 0);
        {
          AstNode *tt = ann_elem_type(node->data.command.args[0]);
          if (tt) bind_annotate(cx, node->data.command.args[1]->data.lit_string.value,
                                node->data.command.args[1]->data.lit_string.length,
                                tt->data.lit_string.value, tt->data.lit_string.length,
                                buf_ann_size(node->data.command.args[0]));
        }
        return bv;
      }

      /* Named destructuring `def {x, y} V` — the target parses as a BLOCK of name
       * tokens (like a closure param list); bind each name to V's field/entry. */
      if (hid == HEAD_DEF && node->data.command.arg_count == 2 &&
          node->data.command.args[0]->type == AST_BLOCK &&
          node->data.command.args[1]->type != AST_BLOCK) {
        AstNode *blk = node->data.command.args[0];
        AstNode *toks[CG_MAX_PARAMS]; int nt = 0;
        for (uint32_t i = 0; i < blk->data.block.count; i++) {
          AstNode *cmd = blk->data.block.commands[i];
          if (cmd->type != AST_COMMAND) { nt = -1; break; }
          if (cmd->data.command.head && nt < CG_MAX_PARAMS) toks[nt++] = cmd->data.command.head;
          for (uint32_t j = 0; j < cmd->data.command.arg_count && nt < CG_MAX_PARAMS; j++)
            toks[nt++] = cmd->data.command.args[j];
        }
        int all_words = nt > 0;
        for (int i = 0; i < nt; i++) if (toks[i]->type != AST_LIT_STRING) all_words = 0;
        if (all_words) {
          IrVal v = compile_expr(cx, node->data.command.args[1]);
          if (cx->failed) return 0;
          for (int k = 0, pos = 0; k < nt; k++, pos++) {
            /* `..rest` — the trailing name collects V[pos..] into a fresh vector
             * (positional; the `..` marker token precedes the rest name). */
            if (toks[k]->data.lit_string.length == 2 &&
                memcmp(toks[k]->data.lit_string.value, "..", 2) == 0 && k + 1 < nt) {
              AstNode *rn = toks[k + 1];
              IrVal start = irb_const_i64(cx->f, cx->cur, jaclval_i32(pos));
              IrVal big = irb_const_i64(cx->f, cx->cur, jaclval_i32(0x3fffffff));
              IrVal sa[] = {cx->sp, v, start, big};
              IrVal rest = emit_rt_call(cx, "jacl_vec_slice", sa, 4);
              env_define(cx, rn->data.lit_string.value, rn->data.lit_string.length,
                         rest, /*is_mut=*/0, /*is_cell=*/0);
              break;   /* rest is always last */
            }
            IrVal fname = compile_string_literal(cx, toks[k]->data.lit_string.value,
                                                 toks[k]->data.lit_string.length);
            IrVal idx = irb_const_i64(cx->f, cx->cur, jaclval_i32(pos));
            IrVal ga[] = {cx->sp, v, fname, idx};   /* struct/map by name, vec/arr by pos */
            IrVal fv = emit_rt_call(cx, "jacl_field_or_index", ga, 4);
            env_define(cx, toks[k]->data.lit_string.value, toks[k]->data.lit_string.length,
                       fv, /*is_mut=*/0, /*is_cell=*/0);
            if (cx->failed) return 0;
          }
          return v;
        }
      }
      if (hid == HEAD_DEF && node->data.command.arg_count == 2 &&
          node->data.command.args[0]->type == AST_DESTRUCTURE_NAMED) {
        AstNode *d = node->data.command.args[0];
        IrVal v = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        for (uint32_t k = 0; k < d->data.destructure_named.count; k++) {
          const char *nm = d->data.destructure_named.names[k];
          uint32_t nl = d->data.destructure_named.name_lens[k];
          IrVal fname = compile_string_literal(cx, nm, nl);
          IrVal idx = irb_const_i64(cx->f, cx->cur, jaclval_i32((int32_t)k));
          IrVal ga[] = {cx->sp, v, fname, idx};   /* struct/map by name, vec/arr by pos */
          IrVal fv = emit_rt_call(cx, "jacl_field_or_index", ga, 4);
          env_define(cx, nm, nl, fv, /*is_mut=*/0, /*is_cell=*/0);
          if (cx->failed) return 0;
        }
        return v;
      }
      /* Node-form vec destructuring `def [a b …] V` (AST_DESTRUCTURE_VEC). */
      if (hid == HEAD_DEF && node->data.command.arg_count == 2 &&
          node->data.command.args[0]->type == AST_DESTRUCTURE_VEC) {
        AstNode *d = node->data.command.args[0];
        IrVal v = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        for (uint32_t k = 0; k < d->data.destructure_vec.count; k++) {
          const char *nm = d->data.destructure_vec.names[k];
          uint32_t nl = d->data.destructure_vec.name_lens[k];
          if (nl == 1 && nm[0] == '_') continue;           /* wildcard */
          IrVal idx = irb_const_i64(cx->f, cx->cur, jaclval_i32((int32_t)k));
          IrVal ga[] = {cx->sp, v, idx};
          IrVal ev = emit_rt_call(cx, "jacl_index_get", ga, 3);
          env_define(cx, nm, nl, ev, /*is_mut=*/0, /*is_cell=*/0);
          if (cx->failed) return 0;
        }
        return v;
      }

      /* Positional destructuring `def [a b …] V` — bind each name to V[i]. */
      if (hid == HEAD_DEF && node->data.command.arg_count == 2 &&
          node->data.command.args[0]->type == AST_COMMAND) {
        AstNode *tgt = node->data.command.args[0];
        IrVal v = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        AstNode *toks[CG_MAX_PARAMS]; int nt = 0;
        if (tgt->data.command.head && nt < CG_MAX_PARAMS) toks[nt++] = tgt->data.command.head;
        for (uint32_t i = 0; i < tgt->data.command.arg_count && nt < CG_MAX_PARAMS; i++)
          toks[nt++] = tgt->data.command.args[i];
        for (int i = 0; i < nt; i++) {
          /* `..rest` (an AST_SPREAD token) binds the remaining elements V[i..] as a
           * fresh vector; it is the final pattern element. */
          if (toks[i]->type == AST_SPREAD && toks[i]->data.spread.expr) {
            AstNode *rn = toks[i]->data.spread.expr;
            const char *rnm = rn->type == AST_VAR_REF ? rn->data.var_ref.name
                            : rn->type == AST_LIT_STRING ? rn->data.lit_string.value : NULL;
            uint32_t rnl = rn->type == AST_VAR_REF ? rn->data.var_ref.length
                         : rn->type == AST_LIT_STRING ? rn->data.lit_string.length : 0;
            if (!rnm) { cx_fail(cx, "rest pattern needs a name"); return 0; }
            IrVal start = irb_const_i64(cx->f, cx->cur, jaclval_i32(i));
            IrVal big = irb_const_i64(cx->f, cx->cur, jaclval_i32(0x3fffffff));
            IrVal sa[] = {cx->sp, v, start, big};
            IrVal rest = emit_rt_call(cx, "jacl_vec_slice", sa, 4);
            env_define(cx, rnm, rnl, rest, 0, 0);
            break;
          }
          if (toks[i]->type != AST_LIT_STRING) { cx_fail(cx, "destructuring name must be a bare word"); return 0; }
          IrType sig[] = {IRB_I64, IRB_I64, IRB_I32};
          IrType r1[] = {IRB_I64};
          IrVal h = irb_const_i32(cx->f, cx->cur, 0);
          IrVal idx = irb_const_i32(cx->f, cx->cur, i);
          IrVal ga[] = {cx->sp, v, idx};
          IrVal elem = irb_call_import(cx->f, cx->cur, "jacl_vec_get", sig, 3, r1, 1, h, ga, 3);
          env_define(cx, toks[i]->data.lit_string.value, toks[i]->data.lit_string.length, elem, /*is_mut=*/0, /*is_cell=*/0);
        }
        return v;
      }

      if (hid == HEAD_DEF || hid == HEAD_MUT) {
        /* Optional leading type annotation — `def i64 x V` (scalar keyword) or
         * `def [Vec T] xs V` (compound) — is skipped: every value is a uniform
         * JaclVal here; the typer's inferred types drive unboxed lowering. */
        AstNode **bargs = node->data.command.args;
        uint32_t bargc = node->data.command.arg_count;
        uint32_t tshift = 0;
        if (bargc >= 3 && bargs[1]->type == AST_LIT_STRING) {
          if (bargs[0]->type == AST_COMMAND) tshift = 1;
          else if (bargs[0]->type == AST_LIT_STRING &&
                   cg_is_type_prefix(bargs[0]->data.lit_string.value, bargs[0]->data.lit_string.length))
            tshift = 1;   /* `def i64 x V`, `def Point p V` (struct type prefix) */
        }
        if (bargc < 2 + tshift || bargs[tshift]->type != AST_LIT_STRING) {
          cx_fail(cx, "binding form needs a name and a value");
          return 0;
        }
        const char *name = bargs[tshift]->data.lit_string.value;
        uint32_t len = bargs[tshift]->data.lit_string.length;
        IrVal val = compile_expr(cx, bargs[tshift + 1]);
        if (cx->failed) return 0;
        /* `def i64/u64/f64 x V` — widen the value to the declared wide scalar so
         * arithmetic on it promotes past 32 bits. */
        if (tshift && bargs[0]->type == AST_LIT_STRING && bargs[0]->data.lit_string.length == 3) {
          const char *tw = bargs[0]->data.lit_string.value;
          int kind = !memcmp(tw, "i64", 3) ? 0x0E : !memcmp(tw, "u64", 3) ? 0x0F
                   : !memcmp(tw, "f64", 3) ? 0x10 : 0;
          if (kind) {
            IrVal kc = irb_const_i64(cx->f, cx->cur, jaclval_i32(kind));
            IrVal wa[] = {cx->sp, val, kc};
            val = emit_rt_call(cx, "jacl_widen_to", wa, 3);
          }
        }
        /* A `mut` captured by a closure is boxed in a heap cell so the mutation is
         * shared; `def` (immutable) and uncaptured `mut` stay plain SSA values. */
        if (hid == HEAD_MUT && is_captured_name(cx, name, len)) {
          IrVal a[] = {cx->sp, val};
          IrVal cell = emit_rt_call(cx, "jacl_cell_new", a, 2);
          env_define(cx, name, len, cell, /*is_mut=*/1, /*is_cell=*/1);
        } else {
          env_define(cx, name, len, val, hid == HEAD_MUT, /*is_cell=*/0);
          /* Carry a typed-collection stamp onto the binding: from the def's own
           * annotation (`def [Arr T] a V`) or from a typed constructor value
           * (`def a [[Arr T] …]` — the ctor's head IS the annotation). */
          AstNode *annsrc = NULL;
          if (tshift && bargs[0]->type == AST_COMMAND) annsrc = bargs[0];
          else if (bargs[tshift + 1]->type == AST_COMMAND &&
                   bargs[tshift + 1]->data.command.head &&
                   bargs[tshift + 1]->data.command.head->type == AST_COMMAND)
            annsrc = bargs[tshift + 1]->data.command.head;
          if (annsrc) {
            AstNode *tt = ann_elem_type(annsrc);
            if (tt) bind_annotate(cx, name, len, tt->data.lit_string.value,
                                  tt->data.lit_string.length, buf_ann_size(annsrc));
          }
        }
        /* A top-level binding mirrors into the module-global map so procs/closures see it. */
        if (cx->at_top_level && is_global_name(cx, name, len)) {
          IrVal key = compile_string_literal(cx, name, len);
          IrVal ga[] = {cx->sp, key, val};
          (void)emit_rt_call(cx, "jacl_global_set", ga, 3);
        }
        return val;
      }
      if (hid == HEAD_SET) {
        /* `set $b->7 V` — indexed in-place element mutation (buf/arr). */
        if (node->data.command.arg_count == 2 && node->data.command.args[0]->type == AST_COMMAND &&
            node->data.command.args[0]->data.command.head_id == HEAD_DOT &&
            node->data.command.args[0]->data.command.arg_count == 2 &&
            node->data.command.args[0]->data.command.args[1]->type == AST_LIT_INT) {
          AstNode *dot = node->data.command.args[0];
          IrVal bv = compile_expr(cx, dot->data.command.args[0]);
          if (cx->failed) return 0;
          IrVal idx = irb_const_i64(cx->f, cx->cur,
                                    jaclval_i32((int32_t)dot->data.command.args[1]->data.lit_int.value));
          IrVal val = compile_expr(cx, node->data.command.args[1]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, bv, idx, val};
          return emit_rt_call(cx, "jacl_arr_set_at", a, 4);
        }
        /* `set $b->$k V` — dynamic dot target (index or struct field). */
        if (node->data.command.arg_count == 2 && node->data.command.args[0]->type == AST_COMMAND &&
            node->data.command.args[0]->data.command.head_id == HEAD_DOT &&
            node->data.command.args[0]->data.command.arg_count == 2 &&
            node->data.command.args[0]->data.command.args[1]->type != AST_LIT_STRING &&
            node->data.command.args[0]->data.command.args[1]->type != AST_LIT_INT) {
          AstNode *dot = node->data.command.args[0];
          IrVal sv = compile_expr(cx, dot->data.command.args[0]);
          if (cx->failed) return 0;
          IrVal kv = compile_expr(cx, dot->data.command.args[1]);
          if (cx->failed) return 0;
          IrVal val = compile_expr(cx, node->data.command.args[1]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, sv, kv, val};
          return emit_rt_call(cx, "jacl_dot_dyn_set", a, 4);
        }
        /* `set $ctx->field V` — the ambient context is a map (not a struct), so route
         * to jacl_ctx_set_field, which updates the runtime-global ctx in place (so a
         * later `$ctx->field` read observes it). Only when `ctx` is the ambient keyword
         * (no local binding shadows it). */
        if (node->data.command.arg_count == 2 && node->data.command.args[0]->type == AST_COMMAND &&
            node->data.command.args[0]->data.command.head_id == HEAD_DOT &&
            node->data.command.args[0]->data.command.arg_count == 2 &&
            node->data.command.args[0]->data.command.args[1]->type == AST_LIT_STRING &&
            node->data.command.args[0]->data.command.args[0]->type == AST_VAR_REF &&
            node->data.command.args[0]->data.command.args[0]->data.var_ref.length == 3 &&
            memcmp(node->data.command.args[0]->data.command.args[0]->data.var_ref.name, "ctx", 3) == 0 &&
            !env_lookup(cx, "ctx", 3)) {
          AstNode *dot = node->data.command.args[0];
          IrVal fname = compile_string_literal(cx, dot->data.command.args[1]->data.lit_string.value,
                                               dot->data.command.args[1]->data.lit_string.length);
          IrVal val = compile_expr(cx, node->data.command.args[1]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, fname, val};
          return emit_rt_call(cx, "jacl_ctx_set_field", a, 3);
        }
        /* `set $p->x V` — in-place struct field mutation. */
        if (node->data.command.arg_count == 2 && node->data.command.args[0]->type == AST_COMMAND &&
            node->data.command.args[0]->data.command.head_id == HEAD_DOT &&
            node->data.command.args[0]->data.command.arg_count == 2 &&
            node->data.command.args[0]->data.command.args[1]->type == AST_LIT_STRING) {
          AstNode *dot = node->data.command.args[0];
          IrVal sv = compile_expr(cx, dot->data.command.args[0]);
          if (cx->failed) return 0;
          IrVal fname = compile_string_literal(cx, dot->data.command.args[1]->data.lit_string.value,
                                               dot->data.command.args[1]->data.lit_string.length);
          IrVal val = compile_expr(cx, node->data.command.args[1]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, sv, fname, val};
          return emit_rt_call(cx, "jacl_struct_put", a, 4);
        }
        const char *name; uint32_t len;
        if (!binding_name(cx, node, &name, &len)) return 0;
        IrVal val = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        Binding *bd = env_lookup(cx, name, len);
        if (!bd) {
          /* `set` of a top-level (module-global) binding: write the global map. */
          if (is_global_name(cx, name, len)) {
            IrVal key = compile_string_literal(cx, name, len);
            IrVal a[] = {cx->sp, key, val};
            return emit_rt_call(cx, "jacl_global_set", a, 3);
          }
          cx_failf(cx, "codegen: set of undefined variable '%.*s'", name, len); return 0;
        }
        if (!bd->is_mut) { cx_failf(cx, "codegen: cannot mutate immutable binding '%.*s'", name, len); return 0; }
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
      /* Map literal `[map k0 v0 k1 v1 …]` → empty + set each pair. */
      if (hid == HEAD_MAP) {
        if (node->data.command.arg_count % 2 != 0) {
          char msg[128];
          snprintf(msg, sizeof msg,
                   "builtin 'map' expects an even number of arguments but got %u",
                   node->data.command.arg_count);
          cx_fail(cx, msg);
          return 0;
        }
        IrVal empty[] = {cx->sp};
        IrVal acc = emit_rt_call(cx, "jacl_map_empty", empty, 1);
        for (uint32_t i = 0; i + 1 < node->data.command.arg_count; i += 2) {
          IrVal k = compile_expr(cx, node->data.command.args[i]);
          if (cx->failed) return 0;
          IrVal v = compile_expr(cx, node->data.command.args[i + 1]);
          if (cx->failed) return 0;
          IrVal a[] = {cx->sp, acc, k, v};
          acc = emit_rt_call(cx, "jacl_map_set", a, 4);
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
        /* Operator spread `[+ ..$v]` — fold the operator over the collection at runtime
         * (the element count is dynamic). Arithmetic operators only. */
        if (argc == 1 && args[0]->type == AST_SPREAD) {
          int opid = hid == HEAD_PLUS ? 0 : hid == HEAD_MINUS ? 1 : hid == HEAD_STAR ? 2
                   : hid == HEAD_SLASH ? 3 : hid == HEAD_PERCENT ? 4 : -1;
          if (opid >= 0) {
            IrVal v = compile_expr(cx, args[0]->data.spread.expr);
            if (cx->failed) return 0;
            IrVal oc = irb_const_i64(cx->f, cx->cur, jaclval_i32(opid));
            IrVal a[] = {cx->sp, v, oc};
            return emit_rt_call(cx, "jacl_vec_reduce", a, 3);
          }
        }
        /* Unary minus `[- x]` — negate via `0 - x`, so it inherits jacl_sub's full
         * numeric promotion (i32 → i64 on overflow, f64, …) instead of an i32-only path. */
        if (argc == 1 && hid == HEAD_MINUS) {
          IrVal x = compile_expr(cx, args[0]);
          if (cx->failed) return 0;
          IrVal zero = irb_const_i64(cx->f, cx->cur, jaclval_i32(0));
          return emit_binop_call(cx, "jacl_sub", zero, x);
        }
        if (argc < 2) {
          AstNode *h = node->data.command.head;
          if (h && h->type == AST_LIT_STRING) {
            char msg[128];
            snprintf(msg, sizeof msg, "builtin '%.*s' expects 2 arguments but got %u",
                     (int)h->data.lit_string.length, h->data.lit_string.value, argc);
            cx_fail(cx, msg);
          } else {
            cx_fail(cx, "operator needs at least 2 arguments");
          }
          return 0;
        }
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
          /* Variadic proc `[f a b c]` (proc has `..rest`): pass the fixed params
           * directly and pack the trailing args into the rest vector. */
          if (!p->is_generator && p->variadic) {
            if ((int)argc < p->fixed_arity) {
              char msg[160];
              snprintf(msg, sizeof msg, "proc '%.*s' expects at least %d arguments but got %u",
                       (int)head->data.lit_string.length, head->data.lit_string.value,
                       p->fixed_arity, argc);
              cx_fail(cx, msg);
              return 0;
            }
            IrVal cargs[1 + CG_MAX_PARAMS];
            cargs[0] = cx->sp;
            for (int i = 0; i < p->fixed_arity; i++) {
              cargs[1 + i] = compile_expr(cx, node->data.command.args[i]);
              if (cx->failed) return 0;
            }
            IrVal ve[] = {cx->sp};
            IrVal rest = emit_rt_call(cx, "jacl_vec_empty", ve, 1);
            for (uint32_t i = (uint32_t)p->fixed_arity; i < argc; i++) {
              IrVal e = compile_expr(cx, node->data.command.args[i]);
              if (cx->failed) return 0;
              IrVal pa[] = {cx->sp, rest, e};
              rest = emit_rt_call(cx, "jacl_vec_push", pa, 3);
            }
            cargs[1 + p->fixed_arity] = rest;
            emit_trace_line(cx, node->start.line);
            return irb_call(cx->f, cx->cur, p->func, cargs, p->fixed_arity + 2);
          }
          /* Spread into a fixed-arity proc `[add3 ..$v]` — the single spread supplies
           * all N params by index (v[0..N)); the runtime index-get errors if v is short. */
          if (!p->is_generator && argc == 1 &&
              node->data.command.args[0]->type == AST_SPREAD) {
            IrVal vec = compile_expr(cx, node->data.command.args[0]->data.spread.expr);
            if (cx->failed) return 0;
            IrVal cargs[1 + CG_MAX_PARAMS];
            cargs[0] = cx->sp;
            for (int i = 0; i < p->arity && i < CG_MAX_PARAMS; i++) {
              IrVal idx = irb_const_i64(cx->f, cx->cur, jaclval_i32(i));
              IrVal ga[] = {cx->sp, vec, idx};
              cargs[1 + i] = emit_rt_call(cx, "jacl_index_get", ga, 3);
            }
            emit_trace_line(cx, node->start.line);
            return irb_call(cx->f, cx->cur, p->func, cargs, p->arity + 1);
          }
          if ((int)argc != p->arity) {
            {
              char msg[160];
              snprintf(msg, sizeof msg, "proc '%.*s' expects %d arguments but got %u",
                       (int)head->data.lit_string.length, head->data.lit_string.value,
                       p->arity, argc);
              cx_fail(cx, msg);
            }
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
          emit_trace_line(cx, node->start.line);  /* record the call site on the caller frame */
          return irb_call(cx->f, cx->cur, p->func, args, (int)argc + 1);
        }
      }

      /* Calling a named BINDING that holds a closure (`def f [\\ …]; [f x]`): the head
       * word resolves in the environment (no proc matched above) → closure call. */
      if (node->data.command.head && node->data.command.head->type == AST_LIT_STRING) {
        Binding *hb = env_lookup(cx, node->data.command.head->data.lit_string.value,
                                 node->data.command.head->data.lit_string.length);
        if (hb) {
          IrVal cval = hb->value;
          if (hb->is_cell) { IrVal a[] = {cx->sp, cval}; cval = emit_rt_call(cx, "jacl_cell_get", a, 2); }
          uint32_t argc2 = node->data.command.arg_count;
          IrVal fnargs[] = {cx->sp, cval};
          IrVal fn = emit_rt_call(cx, "jacl_closure_fn", fnargs, 2);
          IrVal fnw = irb_convert(cx->f, cx->cur, IRB_WRAP_I64, fn);
          IrVal cargs[2 + CG_MAX_PARAMS];
          IrType sig[2 + CG_MAX_PARAMS];
          cargs[0] = cx->sp; cargs[1] = cval; sig[0] = sig[1] = IRB_I64;
          for (uint32_t i = 0; i < argc2 && i < CG_MAX_PARAMS; i++) {
            cargs[2 + i] = compile_expr(cx, node->data.command.args[i]);
            sig[2 + i] = IRB_I64;
            if (cx->failed) return 0;
          }
          IrType r1[] = {IRB_I64};
          return irb_call_indirect(cx->f, cx->cur, sig, (int)argc2 + 2, r1, 1, fnw, cargs, (int)argc2 + 2);
        }
      }

      /* Streams, EAGER for now: [collect S] / [transform S C] / [filter S C] evaluate
       * to a vector immediately (the corpus collects lazily-built pipelines right
       * away, so eager evaluation is output-equivalent; true laziness is a later
       * pass). S may be a generator call, a nested transform/filter, or vec-like. */
      if (hid == HEAD_COLLECT || hid == HEAD_TRANSFORM || hid == HEAD_FILTER) {
        uint32_t want = (hid == HEAD_COLLECT) ? 1 : 2;
        AstNode *h = node->data.command.head;
        if (node->data.command.arg_count != want && h && h->type == AST_LIT_STRING) {
          char msg[128];
          snprintf(msg, sizeof msg, "builtin '%.*s' expects %u argument%s but got %u",
                   (int)h->data.lit_string.length, h->data.lit_string.value,
                   want, want == 1 ? "" : "s", node->data.command.arg_count);
          cx_fail(cx, msg);
          return 0;
        }
      }
      if ((hid == HEAD_COLLECT && node->data.command.arg_count == 1) ||
          ((hid == HEAD_TRANSFORM || hid == HEAD_FILTER) && node->data.command.arg_count == 2)) {
        AstNode *srcn = node->data.command.args[0];
        /* Compile the SOURCE before the closure. A source that is itself a multi-block
         * stream expression (nested filter/transform, or a generator drain) leaves the
         * cursor in a later block; a closure compiled first would be an SSA value from an
         * earlier block, invalid there (block-local SSA) — the frame would carry garbage. */
        IrVal src;
        int is_gen = 0;
        if (generator_call(cx, srcn)) {
          src = compile_expr(cx, srcn);        /* jacl_gen_new(...) */
          is_gen = 1;
        } else {
          src = compile_expr(cx, srcn);        /* vec-like (incl. nested eager stream) */
        }
        if (cx->failed) return 0;
        IrVal clo = 0;
        if (hid != HEAD_COLLECT) {
          clo = compile_expr(cx, node->data.command.args[1]);
          if (cx->failed) return 0;
        }
        IrVal result;
        if (hid == HEAD_COLLECT && !is_gen) {
          result = src;   /* collect of a vec is itself */
        } else if ((hid == HEAD_FILTER || hid == HEAD_TRANSFORM) && !is_gen) {
          /* filter/transform over a non-generator source dispatch on runtime type: a map
           * builds a map (2-param key/value callback), else a vec. */
          result = compile_filter_transform(cx, src, clo, hid == HEAD_FILTER);
        } else {
          result = stream_into_vec(cx, src, is_gen, clo, hid == HEAD_FILTER);
        }
        if (cx->failed) return 0;
        /* Stamp the materialized vector as a typed vec when the underlying stream is typed
         * (a `[Stream T]` generator). jacl_tvec_mark only re-tags a plain vec, so a map
         * result or an already-typed source vec passes through unchanged (idempotent). */
        if (expr_is_typed_stream(cx, node)) {
          IrVal ta[] = {cx->sp, result};
          result = emit_rt_call(cx, "jacl_tvec_mark", ta, 2);
        }
        return result;
      }

      /* `[take SRC N]` — first N elements of a stream, eagerly. A generator source is
       * drained into a vector first (finite corpus generators), then sliced to N; a
       * vec-like source is sliced directly. Result is a vector (an eager stream). */
      if (hid == HEAD_TAKE && node->data.command.arg_count == 2) {
        IrVal src;
        if (generator_call(cx, node->data.command.args[0])) {
          IrVal g = compile_expr(cx, node->data.command.args[0]);
          if (cx->failed) return 0;
          src = stream_into_vec(cx, g, /*is_gen=*/1, /*clo=*/0, /*is_filter=*/0);
        } else {
          src = compile_expr(cx, node->data.command.args[0]);
        }
        if (cx->failed) return 0;
        IrVal n = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        IrVal zero = irb_const_i64(cx->f, cx->cur, jaclval_i32(0));
        IrVal a[] = {cx->sp, src, zero, n};
        return emit_rt_call(cx, "jacl_vec_slice", a, 4);
      }

      /* Typed pointers. `[ptr-null [Ptr T]]` is a null (0) address; `[ptr-cast [Ptr T]
       * $addr]` wraps a u64 address; both carry the pointee type name `T` so the value
       * prints `Ptr<T>(0xADDR)`. `[ptr-addr $p]` recovers the raw address. The name comes
       * from the `[Ptr T]` annotation (args[0]): a bracket command whose sole arg is T. */
      if ((hid == HEAD_PTR_NULL && node->data.command.arg_count == 1) ||
          (hid == HEAD_PTR_CAST && node->data.command.arg_count == 2)) {
        AstNode *ptr_ty = node->data.command.args[0];
        const char *pn = "?"; uint32_t pnl = 1;
        if (ptr_ty->type == AST_COMMAND && ptr_ty->data.command.arg_count >= 1 &&
            ptr_ty->data.command.args[0]->type == AST_LIT_STRING) {
          pn = ptr_ty->data.command.args[0]->data.lit_string.value;
          pnl = ptr_ty->data.command.args[0]->data.lit_string.length;
        }
        IrVal addr = (hid == HEAD_PTR_NULL) ? irb_const_i64(cx->f, cx->cur, jaclval_i32(0))
                                            : compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        IrVal name = compile_string_literal(cx, pn, pnl);
        IrVal a[] = {cx->sp, addr, name};
        return emit_rt_call(cx, "jacl_ptr_cast", a, 3);
      }
      if (hid == HEAD_PTR_ADDR && node->data.command.arg_count == 1) {
        IrVal v = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, v};
        return emit_rt_call(cx, "jacl_ptr_addr", a, 2);
      }
      /* `[addr $buf->i]` — a fat pointer { base, offset } over a buffer element. The
       * arg is a dot access `[. base idx]`; take base + index and build the pointer. */
      if (hid == HEAD_ADDR && node->data.command.arg_count == 1 &&
          node->data.command.args[0]->type == AST_COMMAND &&
          node->data.command.args[0]->data.command.head_id == HEAD_DOT &&
          node->data.command.args[0]->data.command.arg_count == 2) {
        AstNode *dot = node->data.command.args[0];
        IrVal base = compile_expr(cx, dot->data.command.args[0]);
        if (cx->failed) return 0;
        AstNode *ixn = dot->data.command.args[1];
        IrVal idx = (ixn->type == AST_LIT_INT)
                        ? irb_const_i64(cx->f, cx->cur, jaclval_i32((int32_t)ixn->data.lit_int.value))
                        : compile_expr(cx, ixn);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, base, idx};
        return emit_rt_call(cx, "jacl_addr_of", a, 3);
      }

      /* `[first SRC]` / `[count SRC]` — the first element / element count of a stream.
       * A generator source is drained into a vector first (finite corpus generators). */
      if ((hid == HEAD_FIRST || hid == HEAD_COUNT) && node->data.command.arg_count == 1) {
        IrVal src;
        if (generator_call(cx, node->data.command.args[0])) {
          IrVal g = compile_expr(cx, node->data.command.args[0]);
          if (cx->failed) return 0;
          src = stream_into_vec(cx, g, /*is_gen=*/1, /*clo=*/0, /*is_filter=*/0);
        } else {
          src = compile_expr(cx, node->data.command.args[0]);
        }
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, src};
        return emit_rt_call(cx, hid == HEAD_FIRST ? "jacl_first" : "jacl_len", a, 2);
      }

      /* Mutable-array constructor: `[[Arr T] e0 e1 …]` (typed head — element type is
       * dynamic for now) and the untyped literal `[arr e0 e1 …]`. */
      {
        AstNode *h2 = node->data.command.head;
        int is_arr_ctor = (hid == HEAD_ARR);
        int32_t bufn = -1;
        int is_vec_ctor = 0, is_map_ctor = 0;
        if (!is_arr_ctor && h2 && h2->type == AST_COMMAND && h2->data.command.head &&
            h2->data.command.head->type == AST_LIT_STRING &&
            h2->data.command.head->data.lit_string.length == 3) {
          if (memcmp(h2->data.command.head->data.lit_string.value, "Arr", 3) == 0) is_arr_ctor = 1;
          else if (memcmp(h2->data.command.head->data.lit_string.value, "Vec", 3) == 0) is_vec_ctor = 1;
          else if (memcmp(h2->data.command.head->data.lit_string.value, "Map", 3) == 0) is_map_ctor = 1;
        }
        if (!is_arr_ctor && !is_vec_ctor && !is_map_ctor) bufn = buf_ann_size(h2);
        if (is_map_ctor) {
          /* `[[Map K V] k0 v0 k1 v1 …]` — a typed map literal (element types are
           * carried typer-side; codegen builds the dynamic map, checking arity). */
          if (node->data.command.arg_count % 2 != 0) {
            cx_fail(cx, "typed map literal needs an even number of arguments (key value …)");
            return 0;
          }
          IrVal ea[] = {cx->sp};
          IrVal acc = emit_rt_call(cx, "jacl_map_empty", ea, 1);
          for (uint32_t i = 0; i + 1 < node->data.command.arg_count; i += 2) {
            IrVal k = compile_expr(cx, node->data.command.args[i]);
            if (cx->failed) return 0;
            IrVal v = compile_expr(cx, node->data.command.args[i + 1]);
            if (cx->failed) return 0;
            IrVal pa[] = {cx->sp, acc, k, v};
            acc = emit_rt_call(cx, "jacl_map_set", pa, 4);
          }
          return acc;
        }
        if (is_vec_ctor) {
          /* `[[Vec T] e0 e1 …]` — a persistent vector with literal element checks. */
          AstNode *vtt = ann_elem_type(h2);
          IrVal ea[] = {cx->sp};
          IrVal acc = emit_rt_call(cx, "jacl_vec_empty", ea, 1);
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            if (vtt && !check_elem_literal(cx, node->data.command.args[i],
                                           vtt->data.lit_string.value, vtt->data.lit_string.length))
              return 0;
            IrVal e = compile_expr(cx, node->data.command.args[i]);
            if (cx->failed) return 0;
            IrVal pa[] = {cx->sp, acc, e};
            acc = emit_rt_call(cx, "jacl_vec_push", pa, 3);
          }
          return acc;
        }
        if (bufn >= 0) {
          /* `[[Buf N T] e0 e1 …]`: fixed length N — given elements, rest zero-by-type
           * (nested `[Buf M U]` elements get fresh zero inner buffers, recursively). */
          AstNode *btt = ann_elem_type(h2);           /* scalar element name (for checks) */
          AstNode *bnode = ann_elem_node(h2);         /* element type node (incl. compound) */
          IrVal ea[] = {cx->sp};
          IrVal av = emit_rt_call(cx, "jacl_arr_new", ea, 1);
          uint32_t given = node->data.command.arg_count;
          for (int32_t k = 0; k < bufn; k++) {
            if ((uint32_t)k < given && btt &&
                !check_elem_literal(cx, node->data.command.args[k],
                                    btt->data.lit_string.value, btt->data.lit_string.length))
              return 0;
            IrVal e = ((uint32_t)k < given)
                          ? compile_expr(cx, node->data.command.args[k])
                          : emit_type_default(cx, bnode, 0);
            if (cx->failed) return 0;
            IrVal pa[] = {cx->sp, av, e};
            (void)emit_rt_call(cx, "jacl_arr_push", pa, 3);
          }
          return av;
        }
        if (is_arr_ctor) {
          AstNode *att = (h2 && h2->type == AST_COMMAND) ? ann_elem_type(h2) : NULL;
          IrVal ea[] = {cx->sp};
          IrVal av = emit_rt_call(cx, "jacl_arr_new", ea, 1);
          for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
            if (att && !check_elem_literal(cx, node->data.command.args[i],
                                           att->data.lit_string.value, att->data.lit_string.length))
              return 0;
            IrVal e = compile_expr(cx, node->data.command.args[i]);
            if (cx->failed) return 0;
            IrVal pa[] = {cx->sp, av, e};
            (void)emit_rt_call(cx, "jacl_arr_push", pa, 3);
          }
          return av;
        }
      }

      /* Struct constructor `[Point x 1 y 2]` — named (field value) pairs; declared
       * fields not given stay nil. Field slots are initialized in declaration order
       * (names + nil), then the provided pairs overwrite by name. */
      if (node->data.command.head && node->data.command.head->type == AST_LIT_STRING) {
        SDef *sd = sdef_lookup(cx, node->data.command.head->data.lit_string.value,
                               node->data.command.head->data.lit_string.length);
        if (sd) {
          /* Base: every field at its type's zero default (structs zero recursively). */
          IrVal sv = emit_struct_zero(cx, sd, 0);
          uint32_t argc2 = node->data.command.arg_count;
          for (uint32_t i = 0; i + 1 < argc2; i += 2) {
            if (node->data.command.args[i]->type != AST_LIT_STRING) {
              cx_fail(cx, "struct constructor expects `field value` pairs"); return 0;
            }
            IrVal fname = compile_string_literal(cx, node->data.command.args[i]->data.lit_string.value,
                                                 node->data.command.args[i]->data.lit_string.length);
            IrVal val = compile_expr(cx, node->data.command.args[i + 1]);
            if (cx->failed) return 0;
            IrVal pa[] = {cx->sp, sv, fname, val};
            (void)emit_rt_call(cx, "jacl_struct_put", pa, 4);
          }
          return sv;
        }
      }

      /* `[to TYPE V]` — cast (the TYPE word compiles as a string). */
      if (hid == HEAD_TO && node->data.command.arg_count == 2 &&
          node->data.command.args[0]->type == AST_LIT_STRING) {
        IrVal tn = compile_string_literal(cx, node->data.command.args[0]->data.lit_string.value,
                                          node->data.command.args[0]->data.lit_string.length);
        IrVal v = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, v, tn};
        return emit_rt_call(cx, "jacl_to_cast", a, 3);
      }
      /* `[swap $ref $f]` — apply the closure to the deref'd value, store it back,
       * and yield the new value (box or atom). */
      if (hid == HEAD_SWAP && node->data.command.arg_count == 2) {
        IrVal ref = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal clo = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        IrVal da[] = {cx->sp, ref};
        IrVal cur = emit_rt_call(cx, "jacl_box_get", da, 2);
        IrVal fa[] = {cx->sp, clo};
        IrVal fn = emit_rt_call(cx, "jacl_closure_fn", fa, 2);
        IrVal fnw = irb_convert(cx->f, cx->cur, IRB_WRAP_I64, fn);
        IrType sig[] = {IRB_I64, IRB_I64, IRB_I64};
        IrType r1[] = {IRB_I64};
        IrVal cargs[] = {cx->sp, clo, cur};
        IrVal nv = irb_call_indirect(cx->f, cx->cur, sig, 3, r1, 1, fnw, cargs, 3);
        IrVal sa[] = {cx->sp, ref, nv};
        (void)emit_rt_call(cx, "jacl_box_set", sa, 3);
        return nv;
      }

      /* `[stack-trace]` — the trace captured at the most recent `error` (empty string if
       * none). Frames are recorded by the call-stack instrumentation (gated on wants_trace). */
      if (hid == HEAD_STACK_TRACE && node->data.command.arg_count == 0) {
        IrVal a[] = {cx->sp};
        return emit_rt_call(cx, "jacl_stack_trace", a, 1);
      }

      /* `[not COND]` — by name (no interned head id): boolean negation via jacl_not. */
      if (node->data.command.head && node->data.command.head->type == AST_LIT_STRING &&
          node->data.command.head->data.lit_string.length == 3 &&
          memcmp(node->data.command.head->data.lit_string.value, "not", 3) == 0 &&
          node->data.command.arg_count == 1) {
        IrVal v = compile_expr(cx, node->data.command.args[0]);
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, v};
        return emit_rt_call(cx, "jacl_not", a, 2);
      }

      /* `[assert COND]` — by name (no interned head id): nil or an error (which the
       * statement-position auto-return then propagates). */
      if (node->data.command.head && node->data.command.head->type == AST_LIT_STRING &&
          node->data.command.head->data.lit_string.length == 6 &&
          memcmp(node->data.command.head->data.lit_string.value, "assert", 6) == 0 &&
          node->data.command.arg_count >= 1) {
        /* Two forms: `[assert EXPR]` (single truthy check) and the predicate form
         * `[assert OP a b …]`, which evaluates `[OP a b …]` and asserts its truth. */
        IrVal v;
        if (node->data.command.arg_count == 1) {
          v = compile_expr(cx, node->data.command.args[0]);
        } else {
          AstNode *cmd = synth_command(node->data.command.args[0],
                                       node->data.command.args + 1,
                                       node->data.command.arg_count - 1);
          v = compile_expr(cx, cmd);
        }
        if (cx->failed) return 0;
        IrVal a[] = {cx->sp, v};
        return emit_rt_call(cx, "jacl_assert", a, 2);
      }

      /* Fixed-arity builtins with JaclVal-uniform runtime entry points: [head a…] →
       * jacl_*(sp, a…). Checked after user procs, so a same-named proc wins. */
      {
        static const struct { HeadId hid; const char *fn; uint32_t arity; } BI[] = {
          {HEAD_TO_STRING,  "jacl_to_string",  1},
          {HEAD_VEC_GET,    "jacl_vec_get_at", 2},
          {HEAD_VEC_PUSH,   "jacl_vec_push_v", 2},
          {HEAD_VEC_SET,    "jacl_vec_set_at", 3},
          {HEAD_VEC_LEN,    "jacl_len",        1},
          {HEAD_VEC_SLICE,  "jacl_vec_slice",  3},
          {HEAD_RANGE_INCLUSIVE, "jacl_range_inclusive", 2},
          {HEAD_TILDE,      "jacl_not",        1},
          {HEAD_PTR_DEREF,  "jacl_ptr_deref",  1},
          {HEAD_PTR_OFFSET, "jacl_ptr_offset", 2},
          {HEAD_INDEX,      "jacl_index_op",   2},
          {HEAD_SLICE,      "jacl_slice_op",   3},
          {HEAD_MAP_GET,    "jacl_map_get",    2},
          {HEAD_MAP_SET,    "jacl_map_set",    3},
          {HEAD_MAP_HAS,    "jacl_map_has_v",  2},
          {HEAD_MAP_LEN,    "jacl_len",        1},
          {HEAD_MAP_REMOVE, "jacl_map_remove", 2},
          {HEAD_MAP_KEYS,   "jacl_map_keys_v", 1},
          {HEAD_MAP_VALS,   "jacl_map_vals_v", 1},
          {HEAD_ERROR_Q,    "jacl_is_error_v", 1},
          {HEAD_ERROR,      "jacl_error_new",  1},
          {HEAD_ERROR_VAL,  "jacl_error_val",  1},
          {HEAD_BOX,        "jacl_box_new",    1},
          {HEAD_DEREF,      "jacl_box_get",    1},
          {HEAD_UNBOX,      "jacl_box_get",    1},
          {HEAD_RESET,      "jacl_box_set",    2},
          {HEAD_BOX_Q,      "jacl_is_box_v",   1},
          {HEAD_ARR_GET,    "jacl_arr_get_at", 2},
          {HEAD_ARR_SET,    "jacl_arr_set_at", 3},
          {HEAD_ARR_PUSH,   "jacl_arr_push_v", 2},
          {HEAD_ARR_POP,    "jacl_arr_pop",    1},
          {HEAD_ARR_LEN,    "jacl_len",        1},
          {HEAD_BUF_GET,    "jacl_arr_get_at", 2},
          {HEAD_BUF_UGET,   "jacl_arr_get_at", 2},
          {HEAD_BUF_SET,    "jacl_arr_set_at", 3},
          {HEAD_BUF_USET,   "jacl_arr_set_at", 3},
          {HEAD_BUF_LEN,    "jacl_len",        1},
          {HEAD_SLEEP,      "jacl_sleep",      1},
          {HEAD_ATOM,       "jacl_atom_new",   1},
          {HEAD_ATOM_Q,     "jacl_is_atom_v",  1},
          {HEAD_FUTURE_Q,   "jacl_is_future_v", 1},
          {HEAD_READ_FILE,   "jacl_read_file",   1},
          {HEAD_WRITE_FILE,  "jacl_write_file",  2},
          {HEAD_APPEND_FILE, "jacl_append_file", 2},
          {HEAD_RANGE,      "jacl_range_vec",  2},
          {HEAD_ASSERT_TYPE,"jacl_assert_type", 2},
          {HEAD_VEC_CONCAT, "jacl_vec_concat", 2},
          {HEAD_LINES,      "jacl_lines",      1},
        };
        /* Stamped-element static check: [arr-push $a LIT] against a typed binding. */
        if ((HeadId)hid == HEAD_ARR_PUSH && node->data.command.arg_count == 2 &&
            node->data.command.args[0]->type == AST_VAR_REF) {
          Binding *ab = env_lookup(cx, node->data.command.args[0]->data.var_ref.name,
                                   node->data.command.args[0]->data.var_ref.length);
          int ann_dyn = ab && ab->elem_type && ab->elem_type_len == 3 &&
                        memcmp(ab->elem_type, "dyn", 3) == 0;
          if (ab && ab->elem_type && !ann_dyn && cg_is_type_kw(ab->elem_type, ab->elem_type_len)) {
            AstNode *e = node->data.command.args[1];
            int decl_str = (ab->elem_type_len == 3 && memcmp(ab->elem_type, "str", 3) == 0);
            if (e->type == AST_LIT_INT && decl_str) {
              char msg[160];
              snprintf(msg, sizeof msg,
                       "arr-push: element type i32 does not match the declared %.*s element",
                       (int)ab->elem_type_len, ab->elem_type);
              cx_fail(cx, msg);
              return 0;
            }
            if (e->type == AST_LIT_STRING && !decl_str) {
              char msg[160];
              snprintf(msg, sizeof msg,
                       "arr-push: element type str does not match the declared %.*s element",
                       (int)ab->elem_type_len, ab->elem_type);
              cx_fail(cx, msg);
              return 0;
            }
          }
        }
        /* `arr-set` into a typed integer-scalar array: an OOB set grows with 0 (the typed
         * default) rather than nil, so a later in-bounds read returns 0. */
        if ((HeadId)hid == HEAD_ARR_SET && node->data.command.arg_count == 3 &&
            node->data.command.args[0]->type == AST_VAR_REF) {
          Binding *ab = env_lookup(cx, node->data.command.args[0]->data.var_ref.name,
                                   node->data.command.args[0]->data.var_ref.length);
          if (ab && ab->elem_type && cg_is_int_scalar(ab->elem_type, ab->elem_type_len)) {
            IrVal av = compile_expr(cx, node->data.command.args[0]);
            if (cx->failed) return 0;
            IrVal iv = compile_expr(cx, node->data.command.args[1]);
            if (cx->failed) return 0;
            IrVal vv = compile_expr(cx, node->data.command.args[2]);
            if (cx->failed) return 0;
            IrVal a[] = {cx->sp, av, iv, vv};
            return emit_rt_call(cx, "jacl_arr_set_at_zero", a, 4);
          }
        }
        /* `error V` while tracing: stamp the error line on the top frame and snapshot the
         * call stack, so a later `[stack-trace]` in the handler shows where it was raised.
         * Falls through to the BI table, which emits the actual jacl_error_new. */
        if (cx->wants_trace && (HeadId)hid == HEAD_ERROR && node->data.command.arg_count == 1) {
          emit_trace_line(cx, node->start.line);
          IrVal sa[] = {cx->sp};
          (void)emit_rt_call(cx, "jacl_trace_snapshot", sa, 1);
        }
        for (size_t bi = 0; bi < sizeof(BI) / sizeof(BI[0]); bi++) {
          if (BI[bi].hid != (HeadId)hid) continue;
          if (node->data.command.arg_count != BI[bi].arity) {
            char msg[160];
            snprintf(msg, sizeof msg, "builtin '%.*s' expects %u argument%s but got %u",
                     (int)node->data.command.head->data.lit_string.length,
                     node->data.command.head->data.lit_string.value,
                     BI[bi].arity, BI[bi].arity == 1 ? "" : "s", node->data.command.arg_count);
            cx_fail(cx, msg);
            return 0;
          }
          IrVal a[1 + 4];
          a[0] = cx->sp;
          for (uint32_t i = 0; i < BI[bi].arity; i++) {
            AstNode *an = node->data.command.args[i];
            /* assert-type's TYPE argument is a bare word: pass it as a string. */
            if ((HeadId)hid == HEAD_ASSERT_TYPE && i == 1 && an->type == AST_LIT_STRING)
              a[1 + i] = compile_string_literal(cx, an->data.lit_string.value,
                                                an->data.lit_string.length);
            else
              a[1 + i] = compile_expr(cx, an);
            if (cx->failed) return 0;
          }
          return emit_rt_call(cx, BI[bi].fn, a, (int)BI[bi].arity + 1);
        }
      }

      if (node->data.command.head && node->data.command.head->type == AST_LIT_STRING)
        cx_failf(cx, "codegen: unsupported command head '%.*s'",
                 node->data.command.head->data.lit_string.value,
                 node->data.command.head->data.lit_string.length);
      else
        cx_fail(cx, "unsupported command head (non-name head)");
      return 0;
    }

    default:
      {
        char msg[64];
        snprintf(msg, sizeof msg, "unsupported AST node (type %d)", (int)node->type);
        cx_fail(cx, msg);
      }
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

  /* bracket-form `[return]` / `[return V]` in tail position (mirrors AST_RETURN). */
  if (node->type == AST_COMMAND && node->data.command.head_id == HEAD_RETURN) {
    AstNode *rv = node->data.command.arg_count >= 1 ? node->data.command.args[0] : NULL;
    if (cx->cur_is_generator && rv) {
      cx_fail(cx, "cannot return a value from a generator (proc contains `yield`)");
      return;
    }
    if (rv) compile_tail(cx, rv);
    else emit_return_value(cx, irb_const_i64(cx->f, cx->cur, JACLVAL_NIL));
    return;
  }

  if (node->type == AST_BLOCK) {
    uint32_t bn = node->data.block.count;
    if (bn == 0) { emit_return_value(cx, irb_const_i64(cx->f, cx->cur, jaclval_i32(0))); return; }
    scope_enter(cx);
    for (uint32_t i = 0; i + 1 < bn && !cx->failed; i++) {
      AstNode *stmt = node->data.block.commands[i];
      /* A top-level early `return` terminates the body here — emit the real return
       * (which, in a generator, ends the fiber, exhausting the stream) and drop the
       * rest of the block as unreachable. Early returns nested inside a loop/if are
       * still unsupported; only a direct block statement is handled. */
      if (stmt->type == AST_RETURN ||
          (stmt->type == AST_COMMAND && stmt->data.command.head_id == HEAD_RETURN)) {
        compile_tail(cx, stmt); scope_exit(cx); return;
      }
      IrVal sv = compile_expr(cx, stmt);
      /* A binding statement (def/mut/set) CAPTURES its value — an error bound to a name
       * does not auto-return; it propagates only when the name is later USED unhandled
       * (matching the old VM, where a def leaves no error on the stack for CHECK_ERROR).
       * So `def r [await $f]` / `def e [error …]` can be inspected with error?/error-val. */
      int is_binding = stmt->type == AST_COMMAND &&
                       (stmt->data.command.head_id == HEAD_DEF ||
                        stmt->data.command.head_id == HEAD_MUT ||
                        stmt->data.command.head_id == HEAD_SET);
      if (!cx->failed && !is_binding) emit_stmt_error_check(cx, sv);  /* error auto-return */
    }
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
      /* Variadic procs pack a rest vector at the call site (handled in compile_expr);
       * skip the fixed-arity tail-call fast path and use the value+return fallback. */
      if (p && !p->variadic) {
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
        emit_trace_line(cx, node->start.line);  /* tail call: record site on caller frame */
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

/* A `[Stream T]` type annotation: a bracket command whose head word is "Stream". */
static int node_is_stream_type(AstNode *n) {
  if (!n || n->type != AST_COMMAND) return 0;
  AstNode *h = n->data.command.head;
  return h && h->type == AST_LIT_STRING && h->data.lit_string.length == 6 &&
         memcmp(h->data.lit_string.value, "Stream", 6) == 0;
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
    int variadic = 0;
    int arity = extract_param_names_v(cx, def->data.command.args[1], names, lens, &variadic);
    if (arity < 0) return;

    /* A generator (body has `yield`) is a fiber function `(i64 sp, i64 arg) -> i64`:
     * the single declared param binds to the resume arg. Otherwise a plain proc
     * `(i64 sp, i64 a0…)`. A variadic proc's declared params already count the rest
     * slot (its last name), which the SVM function receives as the packed vector. */
    int gen = contains_yield(def->data.command.args[argc - 1]);
    /* Return annotation (between params and body) of the form `[Stream T]` marks a typed
     * stream: its collected/transformed/filtered results are typed vectors (comma print). */
    int returns_stream = (argc >= 4) && node_is_stream_type(def->data.command.args[argc - 2]);
    if (gen && arity > 1) { cx_failf(cx, "codegen: generator '%.*s' may take at most one parameter (yet)", pname, plen); return; }
    if (gen && variadic) { cx_failf(cx, "codegen: variadic generator '%.*s' unsupported", pname, plen); return; }
    int nparams = gen ? 1 : arity;
    ptypes[0] = IRB_I64; /* sp */
    for (int k = 0; k < nparams; k++) ptypes[k + 1] = IRB_I64;
    IrFunc *func = irb_func_new(m, ptypes, nparams + 1, r1, 1);

    if (cx->nprocs == cx->cap_procs) {
      cx->cap_procs = cx->cap_procs ? cx->cap_procs * 2 : 8;
      cx->procs = realloc(cx->procs, (size_t)cx->cap_procs * sizeof(Proc));
      if (!cx->procs) { fprintf(stderr, "codegen: oom\n"); abort(); }
    }
    cx->procs[cx->nprocs++] =
        (Proc){pname, plen, func, arity, gen, returns_stream, variadic, variadic ? arity - 1 : arity, def, NULL};
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
    cx->cur_is_generator = p->is_generator;
    env_reset(cx);
    capset_reset(cx); capset_scan(cx, body); /* which muts a nested closure captures */
    scope_enter(cx);
    /* params bind to fn params 1..; for a generator (arity<=1) the param is the arg */
    for (int k = 0; k < arity; k++)
      env_define(cx, names[k], lens[k], (IrVal)(k + 1), /*is_mut=*/0, /*is_cell=*/0);

    /* By-value `[Buf N T]` params: copy the caller's array into a fresh one so callee
     * mutations don't escape (BUFFER_DESIGN.md Tier 2). Non-array args pass through. */
    if (!p->is_generator) {
      int bufp[CG_MAX_PARAMS];
      scan_buf_params(def->data.command.args[1], bufp, arity);
      for (int k = 0; k < arity; k++) {
        if (!bufp[k]) continue;
        IrVal ca[] = {cx->sp, (IrVal)(k + 1)};
        IrVal copy = emit_rt_call(cx, "jacl_arr_copy", ca, 2);
        Binding *b = env_lookup(cx, names[k], lens[k]);
        if (b) b->value = copy;
      }
    }

    cx->cur_proc = p->name; cx->cur_proc_len = p->len;
    if (!p->is_generator) emit_trace_push(cx, p->name, p->len);  /* call-stack frame (tracing) */
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
    cx->cur_is_generator = 0;
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

  /* func 0 = the entry the harness runs. func 1 = __jacl_main(sp, arg), the program body.
   * The entry inits the runtime, then runs __jacl_main as the ROOT TASK on a fiber
   * (program-as-a-job): the program's roots then live on a scannable fiber stack, and the
   * body can be quiesced like any task — the prerequisite for sound GC under the M:N pool. */
  IrType i64_1[] = {IRB_I64};
  IrType i64_2[] = {IRB_I64, IRB_I64};
  IrType r1[] = {IRB_I64};
  IrFunc *entry = irb_func_new(m, i64_1, 1, r1, 1);
  IrBlock entry_block = irb_block(entry, i64_1, 1);
  IrFunc *mainf = irb_func_new(m, i64_2, 2, r1, 1);   /* (sp, arg) — arg is the fiber resume arg (unused) */
  IrBlock main_block = irb_block(mainf, i64_2, 2);

  for (uint32_t i = 0; i < count; i++)                /* pass 0: struct declarations */
    if (nodes[i]->type == AST_DEFSTRUCT) sdef_register(&cx, nodes[i]);
  scan_top_globals(&cx, nodes, count); /* top-level def/mut names → module globals */
  for (uint32_t i = 0; i < count && !cx.wants_trace; i++)   /* gate: instrument only if used */
    if (contains_stack_trace(nodes[i])) cx.wants_trace = 1;
  register_procs(&cx, m, nodes, count); /* pass 1 */
  if (!cx.failed) compile_procs(&cx);   /* pass 2: proc bodies */

  /* The program body — the non-proc top-level forms, in order; value = last — into __jacl_main. */
  if (!cx.failed) {
    cx.f = mainf; cx.cur = main_block; cx.sp = 0;
    /* Prime the root fiber: one entry-block `suspend(-1)` forces a full round-trip through
     * the scheduler loop before the body runs, so a later `await` in a non-entry block
     * resumes in place instead of replaying the fiber from the top (see worker_loop). */
    (void)irb_suspend(cx.f, cx.cur, irb_const_i64(cx.f, cx.cur, -1));
    env_reset(&cx);
    capset_reset(&cx);
    cx.cur_proc = "<main>"; cx.cur_proc_len = 6;
    emit_trace_push(&cx, "<main>", 6);   /* root frame for top-level code (only when tracing) */
    cx.at_top_level = 1;   /* top-level def/mut mirror into the module-global map */
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
    cx.at_top_level = 0;
    if (!cx.failed) { IrVal ret[] = {last}; irb_return(cx.f, cx.cur, ret, 1); }
  }

  /* The entry: init the runtime (before anything allocates), then run __jacl_main as the
   * root task on a fiber and return its result. */
  if (!cx.failed) {
    cx.f = entry; cx.cur = entry_block; cx.sp = 0;
    emit_void_call(&cx, "jacl_heap_init");
    emit_void_call(&cx, "jacl_intern_init");
    emit_void_call(&cx, "jacl_map_init");
    IrVal fnref = irb_ref_func(entry, entry_block, mainf);
    IrVal fnref64 = irb_convert(entry, entry_block, IRB_EXTEND_I32U, fnref); /* funcref i32 -> i64 (JaclVal) */
    IrVal a[] = {cx.sp, fnref64};
    IrVal v = emit_rt_call(&cx, "jacl_sched_run_main", a, 2);
    if (!cx.failed) { IrVal ret[] = {v}; irb_return(cx.f, cx.cur, ret, 1); }
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
