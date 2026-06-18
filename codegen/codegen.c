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

typedef struct {
  const char *name;
  uint32_t    len;
  IrVal       value;
  int         is_mut;
} Binding;

typedef struct {
  char  *err;
  size_t errcap;
  int    failed;

  IrFunc *f;
  IrBlock cur;  /* block currently being emitted into */
  IrVal   sp;   /* current value-id of the data-stack pointer in `cur` */

  Binding *locals; int nlocals, cap_locals; /* flat env (most-recent wins) */
  int     *marks;  int nmarks,  cap_marks;  /* scope start indices into locals */
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

static void env_define(Cx *cx, const char *name, uint32_t len, IrVal value, int is_mut) {
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
  cx->locals[cx->nlocals++] = (Binding){name, len, value, is_mut};
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

/* Emit `fn(sp, lhs, rhs)` — a binary runtime call returning an i64 JaclVal. */
static IrVal emit_binop_call(Cx *cx, const char *fn, IrVal lhs, IrVal rhs) {
  IrType i64x3[] = {IRB_I64, IRB_I64, IRB_I64};
  IrType r1[] = {IRB_I64};
  IrVal handle = irb_const_i32(cx->f, cx->cur, 0);
  IrVal args[] = {cx->sp, lhs, rhs};
  return irb_call_import(cx->f, cx->cur, fn, i64x3, 3, r1, 1, handle, args, 3);
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
  env_define(cx, name, nlen, startv, /*is_mut=*/1);
  env_define(cx, FOR_END_SENTINEL, FOR_END_SENTINEL_LEN, endv, /*is_mut=*/0);
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
      return bd->value;
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

      if (hid == HEAD_DEF || hid == HEAD_MUT) {
        const char *name; uint32_t len;
        if (!binding_name(cx, node, &name, &len)) return 0;
        IrVal val = compile_expr(cx, node->data.command.args[1]);
        if (cx->failed) return 0;
        env_define(cx, name, len, val, hid == HEAD_MUT);
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
        bd->value = val;
        return val;
      }

      const char *fn = binary_runtime_fn(hid);
      if (fn) {
        uint32_t argc = node->data.command.arg_count;
        AstNode **args = node->data.command.args;
        if (argc < 2) { cx_fail(cx, "operator needs at least 2 arguments"); return 0; }
        IrVal acc = compile_expr(cx, args[0]);
        for (uint32_t i = 1; i < argc; i++) {
          IrVal rhs = compile_expr(cx, args[i]);
          if (cx->failed) return 0;
          acc = emit_binop_call(cx, fn, acc, rhs);
        }
        return acc;
      }
      cx_fail(cx, "unsupported command head (scaffold: + - * / %, comparisons, def/mut/set, if, while)");
      return 0;
    }

    default:
      cx_fail(cx, "unsupported AST node (scaffold: literals, vars, arithmetic, bindings, if, while)");
      return 0;
  }
}

IrModule *svm_codegen_program(AstNode **nodes, uint32_t count, char *err, size_t errcap) {
  Cx cx;
  memset(&cx, 0, sizeof cx);
  cx.err = err; cx.errcap = errcap;

  IrModule *m = irb_module_new();

  /* Entry: func (i64 sp) -> (i64). sp (v0) threads to runtime calls + child blocks. */
  IrType i64_1[] = {IRB_I64};
  IrType r1[] = {IRB_I64};
  cx.f = irb_func_new(m, i64_1, 1, r1, 1);
  cx.cur = irb_block(cx.f, i64_1, 1);
  cx.sp = 0; /* block0's only param */

  scope_enter(&cx); /* top-level lexical scope */
  IrVal last = (count == 0) ? irb_const_i64(cx.f, cx.cur, jaclval_i32(0)) : 0;
  for (uint32_t i = 0; i < count; i++) {
    last = compile_expr(&cx, nodes[i]);
    if (cx.failed) break;
  }
  scope_exit(&cx);

  int failed = cx.failed;
  if (!failed) {
    IrVal ret[] = {last};
    irb_return(cx.f, cx.cur, ret, 1); /* return in whatever block we ended up in */
  }
  free(cx.locals);
  free(cx.marks);
  if (failed) { irb_module_free(m); return NULL; }
  return m;
}
