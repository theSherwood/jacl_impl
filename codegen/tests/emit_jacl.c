/* emit_jacl.c — drives the P2.2 codegen from real JACL source. Parses a named
 * source snippet through the JACL frontend (lexer + parser), runs svm_codegen_program,
 * and prints the resulting svm-text to stdout so a Rust harness test can link it
 * against the runtime and run it on interp + JIT.
 *
 *   emit_jacl <case>
 *
 * Cases map to small arithmetic programs (see source_for). Built with gcc (the
 * frontend's toolchain); the unity src/jacl.c provides lexer_lex / parser_parse. */
#include "../../src/jacl.c"   /* full JACL frontend (defines the parse pipeline) */

#include "../codegen.h"
#include "../irbuilder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *source_for(const char *name) {
  if (!strcmp(name, "add"))      return "[+ 1 2]";
  if (!strcmp(name, "nested"))   return "[+ 1 [* 2 3]]";
  if (!strcmp(name, "submul"))   return "[- [* 4 5] 3]";
  if (!strcmp(name, "variadic")) return "[+ 1 2 3 4]";
  if (!strcmp(name, "div_mod"))  return "[+ [/ 20 6] [% 20 6]]";
  /* P2.3 bindings & scope */
  if (!strcmp(name, "bind_read"))    return "def x 5\n[+ $x 10]";
  if (!strcmp(name, "mutate"))       return "mut c 1\nset c 42\n[+ $c 0]";
  if (!strcmp(name, "reassign_expr"))return "mut c 1\nset c [+ $c 41]\n[+ $c 0]";
  if (!strcmp(name, "scoped_block")) return "def x 1\n{ def y 10\n[+ $x $y] }";
  /* error cases (driver exits nonzero) */
  if (!strcmp(name, "shadow_err"))   return "def x 1\ndef x 2";
  if (!strcmp(name, "set_undef_err"))return "set nope 1";
  if (!strcmp(name, "set_immut_err"))return "def x 1\nset x 2";
  /* P2.4 control flow */
  if (!strcmp(name, "if_true"))        return "[if [> 5 3] { 10 } else { 20 }]";
  if (!strcmp(name, "if_false"))       return "[if [> 1 3] { 10 } else { 20 }]";
  if (!strcmp(name, "if_noelse_true")) return "[if [> 5 3] { 7 }]";
  if (!strcmp(name, "if_noelse_false"))return "[if [> 1 3] { 7 }]";
  if (!strcmp(name, "if_sets_outer"))  return "mut x 1\n[if [> 5 3] { set x 100 }]\n[+ $x 0]";
  if (!strcmp(name, "while_sum"))
    return "mut i 1\nmut acc 0\n[while [<= $i 5] { set acc [+ $acc $i]\nset i [+ $i 1] }]\n[+ $acc 0]";
  if (!strcmp(name, "while_countdown"))
    return "mut n 3\nmut c 0\n[while [> $n 0] { set n [- $n 1]\nset c [+ $c 10] }]\n[+ $c 0]";
  if (!strcmp(name, "elif_chain"))
    return "[if [> 1 2] { 1 } elif [> 3 2] { 22 } else { 33 }]";
  if (!strcmp(name, "if_expr_bind"))
    return "def x [if [> 5 3] { 9 } else { 0 }]\n[+ $x 0]";
  if (!strcmp(name, "for_sum"))
    return "mut acc 0\n[for i in [range 0 5] { set acc [+ $acc $i] }]\n[+ $acc 0]";
  if (!strcmp(name, "for_range_cmd"))
    return "mut acc 0\n[for i in [range 1 4] { set acc [+ $acc $i] }]\n[+ $acc 0]";
  if (!strcmp(name, "for_dotdot"))
    return "mut acc 0\nfor i in 0..5 { set acc [+ $acc $i] }\n[+ $acc 0]";
  if (!strcmp(name, "for_with_if"))
    return "mut acc 0\n[for i in [range 0 5] { [if [> $i 2] { set acc [+ $acc $i] }] }]\n[+ $acc 0]";
  return NULL;
}

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: emit_jacl <case>\n"); return 2; }
  const char *src = source_for(argv[1]);
  if (!src) { fprintf(stderr, "unknown case: %s\n", argv[1]); return 2; }

  arena_t arena = {0};
  LexResult toks = lexer_lex(src, &arena);
  if (toks.error_count) { fprintf(stderr, "lex errors: %u\n", toks.error_count); return 1; }
  ParseResult parse = parser_parse(toks, &arena);
  if (parse.error_count) { fprintf(stderr, "parse errors: %u\n", parse.error_count); return 1; }

  char err[256] = {0};
  IrModule *m = svm_codegen_program(parse.nodes, parse.count, err, sizeof err);
  if (!m) { fprintf(stderr, "%s\n", err); arena_destroy(&arena); return 1; }

  char *text = irb_to_text(m);
  fputs(text, stdout);
  free(text);
  irb_module_free(m);
  arena_destroy(&arena);
  return 0;
}
