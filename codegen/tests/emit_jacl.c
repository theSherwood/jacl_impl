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
