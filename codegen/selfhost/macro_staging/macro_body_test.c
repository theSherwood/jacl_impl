/* IR-level test for svm_codegen_macro_body: a macro body compiles to a single
 * __jacl_macro(sp, params…) whose syntax-quote lowers to vec construction, and whose
 * ~unquote holes read the params (not constant vecs). For `twice {x} = [+ ~x ~x]` the
 * template has two non-hole nodes (command, "+") -> exactly 2 jacl_vec_empty; the two
 * ~x holes contribute none (they splice param x). */
#include "../../../src/jacl.c"
HeadId jacl_compute_head_id_bridge(AstNode *head) { return ast__compute_head_id(head); }
#include "../../codegen.h"
#include "../../irbuilder.h"
#include <stdio.h>

static int count(const char *h, const char *n) { int c = 0; const char *p = h; while ((p = strstr(p, n))) { c++; p++; } return c; }

int main(void) {
  const char *src = "defmacro twice {x} { syntax-quote [+ ~x ~x] }";
  arena_t a = {0};
  ParseResult pr = parser_parse(lexer_lex(src, &a), &a);
  AstNode *dm = NULL;
  for (uint32_t i = 0; i < pr.count; i++) if (pr.nodes[i]->type == AST_DEFMACRO) { dm = pr.nodes[i]; break; }
  if (!dm) { printf("FAIL: no defmacro parsed\n"); return 1; }
  char err[256] = {0}; uint32_t idx = 999;
  IrModule *m = svm_codegen_macro_body((const char **)dm->data.defmacro.param_names,
                                       dm->data.defmacro.param_name_lens,
                                       dm->data.defmacro.param_count,
                                       dm->data.defmacro.body, &idx, err, sizeof err);
  if (!m) { printf("FAIL: codegen: %s\n", err); return 1; }
  char *ir = irb_to_text(m);
  int empties = count(ir, "jacl_vec_empty");
  int pushes  = count(ir, "jacl_vec_push");
  printf("  func_idx=%u  vec_empty=%d  vec_push=%d\n", idx, empties, pushes);
  int ok = (empties == 2) && (pushes >= 5);   /* cmd(kind,sm,fl,hid,head,a0,a1)=7; head "+"=4 -> but ~x holes reduce */
  printf(ok ? "PASS: macro body lowered; ~unquote splices the param (2 constant nodes)\n"
            : "FAIL: unexpected vec structure\n");
  return ok ? 0 : 1;
}
