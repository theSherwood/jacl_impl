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

/* ---- lexical environment (compile-time) ----
 *
 * P2.3 is pre-control-flow (a single block), so a local is just its current SSA
 * value: `def`/`mut` introduce a binding, `set` updates one in place, and a var-ref
 * reads the most recent. Scopes nest for block bodies. (Cross-block mutation via
 * block args arrives with control flow in P2.4; address-taken locals → data-stack
 * slots later.) */
typedef struct {
  const char *name;
  uint32_t    len;
  IrVal       value;
  int         is_mut;
} Binding;

typedef struct Scope {
  Binding      *binds;
  int           n, cap;
  struct Scope *parent;
} Scope;

typedef struct {
  char  *err;
  size_t errcap;
  int    failed;
  Scope *scope; /* innermost */
} Cx;

static void cx_fail(Cx *cx, const char *msg) {
  if (!cx->failed && cx->err && cx->errcap) snprintf(cx->err, cx->errcap, "codegen: %s", msg);
  cx->failed = 1;
}
static void cx_failf(Cx *cx, const char *fmt, const char *name, uint32_t len) {
  if (!cx->failed && cx->err && cx->errcap)
    snprintf(cx->err, cx->errcap, fmt, (int)len, name);
  cx->failed = 1;
}

static int name_eq(const Binding *bd, const char *name, uint32_t len) {
  return bd->len == len && memcmp(bd->name, name, len) == 0;
}

static void scope_push(Cx *cx, Scope *s) {
  s->binds = NULL; s->n = 0; s->cap = 0; s->parent = cx->scope;
  cx->scope = s;
}
static void scope_pop(Cx *cx) {
  Scope *s = cx->scope;
  cx->scope = s->parent;
  free(s->binds);
}

/* Most-recent binding for `name` across the scope chain, or NULL. */
static Binding *env_lookup(Cx *cx, const char *name, uint32_t len) {
  for (Scope *s = cx->scope; s; s = s->parent)
    for (int i = s->n - 1; i >= 0; i--)
      if (name_eq(&s->binds[i], name, len)) return &s->binds[i];
  return NULL;
}

/* Add a binding to the current scope. Errors if an *immutable* binding of the same
 * name already exists at this depth (matches the old compiler's shadow rule). */
static void env_define(Cx *cx, const char *name, uint32_t len, IrVal value, int is_mut) {
  Scope *s = cx->scope;
  for (int i = 0; i < s->n; i++)
    if (name_eq(&s->binds[i], name, len) && !s->binds[i].is_mut) {
      cx_failf(cx, "codegen: variable '%.*s' already defined in this scope", name, len);
      return;
    }
  if (s->n == s->cap) {
    s->cap = s->cap ? s->cap * 2 : 4;
    s->binds = realloc(s->binds, (size_t)s->cap * sizeof(Binding));
    if (!s->binds) { fprintf(stderr, "codegen: out of memory\n"); abort(); }
  }
  s->binds[s->n++] = (Binding){name, len, value, is_mut};
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

/* The binding name of a def/mut/set: args[0] must be a bare word (AST_LIT_STRING). */
static int binding_name(Cx *cx, AstNode *cmd, const char **name, uint32_t *len) {
  if (cmd->data.command.arg_count < 2 ||
      cmd->data.command.args[0]->type != AST_LIT_STRING) {
    cx_fail(cx, "binding form needs a name and a value");
    return 0;
  }
  *name = cmd->data.command.args[0]->data.lit_string.value;
  *len = cmd->data.command.args[0]->data.lit_string.length;
  return 1;
}

/* Lower one expression to an i64 SSA value (a JaclVal). */
static IrVal compile_expr(Cx *cx, IrFunc *f, IrBlock b, IrVal sp, AstNode *node) {
  if (cx->failed) return 0;
  switch (node->type) {
    case AST_LIT_INT:
      return irb_const_i64(f, b, jaclval_i32(node->data.lit_int.value));

    case AST_VAR_REF: {
      Binding *bd = env_lookup(cx, node->data.var_ref.name, node->data.var_ref.length);
      if (!bd) {
        cx_failf(cx, "codegen: undefined variable '%.*s'",
                 node->data.var_ref.name, node->data.var_ref.length);
        return 0;
      }
      return bd->value;
    }

    case AST_BLOCK: {
      /* A block is a nested lexical scope; its value is the last form's value. */
      Scope s;
      scope_push(cx, &s);
      uint32_t bn = node->data.block.count;
      IrVal last = (bn == 0) ? irb_const_i64(f, b, jaclval_i32(0)) : 0; /* empty block ⇒ 0 */
      for (uint32_t i = 0; i < bn; i++) {
        last = compile_expr(cx, f, b, sp, node->data.block.commands[i]);
        if (cx->failed) break;
      }
      scope_pop(cx);
      return last;
    }

    case AST_COMMAND: {
      uint8_t hid = node->data.command.head_id;

      if (hid == HEAD_DEF || hid == HEAD_MUT) {
        const char *name; uint32_t len;
        if (!binding_name(cx, node, &name, &len)) return 0;
        IrVal val = compile_expr(cx, f, b, sp, node->data.command.args[1]);
        if (cx->failed) return 0;
        env_define(cx, name, len, val, hid == HEAD_MUT);
        return val; /* a binding form evaluates to the bound value */
      }
      if (hid == HEAD_SET) {
        const char *name; uint32_t len;
        if (!binding_name(cx, node, &name, &len)) return 0;
        IrVal val = compile_expr(cx, f, b, sp, node->data.command.args[1]);
        if (cx->failed) return 0;
        Binding *bd = env_lookup(cx, name, len);
        if (!bd) { cx_failf(cx, "codegen: set of undefined variable '%.*s'", name, len); return 0; }
        if (!bd->is_mut) { cx_failf(cx, "codegen: cannot set immutable variable '%.*s'", name, len); return 0; }
        bd->value = val;
        return val;
      }

      const char *fn = arith_runtime_fn(hid);
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
      cx_fail(cx, "unsupported command head (scaffold supports + - * / %, def, mut, set)");
      return 0;
    }

    default:
      cx_fail(cx, "unsupported AST node (scaffold supports literals, vars, arithmetic, bindings)");
      return 0;
  }
}

IrModule *svm_codegen_program(AstNode **nodes, uint32_t count, char *err, size_t errcap) {
  Cx cx = {err, errcap, 0, NULL};
  IrModule *m = irb_module_new();

  /* Entry: func (i64 sp) -> (i64). sp (v0) threads to runtime calls (§3d ABI). */
  IrType i64_1[] = {IRB_I64};
  IrType r1[] = {IRB_I64};
  IrFunc *f = irb_func_new(m, i64_1, 1, r1, 1);
  IrBlock b0 = irb_block(f, i64_1, 1);
  IrVal sp = 0; /* block0's only param */

  Scope root;
  scope_push(&cx, &root); /* top-level lexical scope */

  IrVal last = (count == 0) ? irb_const_i64(f, b0, jaclval_i32(0)) : 0; /* empty program ⇒ 0 */
  for (uint32_t i = 0; i < count; i++) {
    last = compile_expr(&cx, f, b0, sp, nodes[i]);
    if (cx.failed) break;
  }

  scope_pop(&cx);
  if (cx.failed) { irb_module_free(m); return NULL; }

  IrVal ret[] = {last};
  irb_return(f, b0, ret, 1);
  return m;
}
