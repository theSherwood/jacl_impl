/* Test-only stand-in for a codegen'd macro body: twice {x} = syntax-quote [+ ~x ~x].
 * Builds the plain-data syntax vec for [+ x x] by hand (mirrors what svm_codegen_macro_body
 * emits), so the wrapper's decode->call->encode data flow can be tested natively with the
 * real syn_rt codec. head_id 1 = "+" (matches the parser / the SVM e2e output). */
#include "jaclrt.h"
JaclVal __jacl_macro(JaclVal x) {
  JaclVal head = jacl_vec_empty();
  head = jacl_vec_push(head, jaclrt_i32(3));   /* LIT_STRING */
  head = jacl_vec_push(head, jaclrt_i32(0));
  head = jacl_vec_push(head, jaclrt_i32(0));
  head = jacl_vec_push(head, jacl_str_new("+", 1));
  JaclVal cmd = jacl_vec_empty();
  cmd = jacl_vec_push(cmd, jaclrt_i32(0));      /* COMMAND */
  cmd = jacl_vec_push(cmd, jaclrt_i32(0));      /* scope_mark */
  cmd = jacl_vec_push(cmd, jaclrt_i32(0));      /* flags */
  cmd = jacl_vec_push(cmd, jaclrt_i32(1));      /* head_id "+" */
  cmd = jacl_vec_push(cmd, head);
  cmd = jacl_vec_push(cmd, x);
  cmd = jacl_vec_push(cmd, x);
  return cmd;
}
