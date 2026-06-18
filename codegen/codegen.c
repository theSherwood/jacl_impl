/* codegen.c — JACL AST → SVM-IR (P2.2 scaffold). See codegen.h. */
#include "codegen.h"

#include "../src/jacl.h"   /* AstNode, AstNodeType, HeadId, JaclType */

#include <stdio.h>
#include <string.h>

/* JaclVal encoding of an i32 (tag 0x02 in the high byte), per runtime/jaclrt.h. */
#define JACL_TAG_I32_SHIFTED ((uint64_t)0x02 << 56)
static int64_t jaclval_i32(int32_t x) {
  return (int64_t)(JACL_TAG_I32_SHIFTED | (uint64_t)(uint32_t)x);
}

/* Runtime function for a binary arithmetic head, or NULL if not arithmetic. */
static const char *arith_runtime_fn(uint8_t head_id) {
  switch (head_id) {
    case HEAD_PLUS:    return "jacl_add";
    case HEAD_MINUS:   return "jacl_sub";
    case HEAD_STAR:    return "jacl_mul";
    case HEAD_SLASH:   return "jacl_div";
    case HEAD_PERCENT: return "jacl_mod";
    default:           return NULL;
  }
}

typedef struct {
  char  *err;
  size_t errcap;
  int    failed;
} Cx;

static void cx_fail(Cx *cx, const char *msg) {
  if (!cx->failed && cx->err && cx->errcap) snprintf(cx->err, cx->errcap, "codegen: %s", msg);
  cx->failed = 1;
}

/* Emit `fn(sp, lhs, rhs)` — a binary runtime call returning an i64 JaclVal. */
static IrVal emit_binop_call(IrFunc *f, IrBlock b, IrVal sp, const char *fn,
                             IrVal lhs, IrVal rhs) {
  IrType i64x3[] = {IRB_I64, IRB_I64, IRB_I64};
  IrType r1[] = {IRB_I64};
  IrVal handle = irb_const_i32(f, b, 0);
  IrVal args[] = {sp, lhs, rhs};
  return irb_call_import(f, b, fn, i64x3, 3, r1, 1, handle, args, 3);
}

/* Lower one expression to an i64 SSA value (a JaclVal). */
static IrVal compile_expr(Cx *cx, IrFunc *f, IrBlock b, IrVal sp, AstNode *node) {
  if (cx->failed) return 0;
  switch (node->type) {
    case AST_LIT_INT:
      return irb_const_i64(f, b, jaclval_i32(node->data.lit_int.value));

    case AST_COMMAND: {
      const char *fn = arith_runtime_fn(node->data.command.head_id);
      if (fn) {
        uint32_t argc = node->data.command.arg_count;
        AstNode **args = node->data.command.args;
        if (argc < 2) { cx_fail(cx, "arithmetic operator needs at least 2 arguments"); return 0; }
        /* Left-fold: [+ a b c] => add(add(a, b), c) — matches the binary runtime op. */
        IrVal acc = compile_expr(cx, f, b, sp, args[0]);
        for (uint32_t i = 1; i < argc; i++) {
          IrVal rhs = compile_expr(cx, f, b, sp, args[i]);
          if (cx->failed) return 0;
          acc = emit_binop_call(f, b, sp, fn, acc, rhs);
        }
        return acc;
      }
      cx_fail(cx, "unsupported command head (scaffold supports + - * / % only)");
      return 0;
    }

    default:
      cx_fail(cx, "unsupported AST node (scaffold supports int literals and arithmetic)");
      return 0;
  }
}

IrModule *svm_codegen_program(AstNode **nodes, uint32_t count, char *err, size_t errcap) {
  Cx cx = {err, errcap, 0};
  IrModule *m = irb_module_new();

  /* Entry: func (i64 sp) -> (i64). sp (v0) threads to runtime calls (§3d ABI). */
  IrType i64_1[] = {IRB_I64};
  IrType r1[] = {IRB_I64};
  IrFunc *f = irb_func_new(m, i64_1, 1, r1, 1);
  IrBlock b0 = irb_block(f, i64_1, 1);
  IrVal sp = 0; /* block0's only param */

  IrVal last;
  if (count == 0) {
    last = irb_const_i64(f, b0, jaclval_i32(0)); /* empty program ⇒ 0 */
  } else {
    for (uint32_t i = 0; i < count; i++) {
      last = compile_expr(&cx, f, b0, sp, nodes[i]);
      if (cx.failed) { irb_module_free(m); return NULL; }
    }
  }

  IrVal ret[] = {last};
  irb_return(f, b0, ret, 1);
  return m;
}
