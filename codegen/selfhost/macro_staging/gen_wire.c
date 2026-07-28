/* Compiler-side helper: parse a JACL snippet, encode nodes[0] to syn_wire bytes on
 * stdout. Used by the cross-codec test to feed the runtime side. */
#include "../../../src/jacl.c"
#include "syn_wire.c"
#include <stdio.h>
int main(int argc, char **argv) {
  if (argc < 2) return 2;
  arena_t a = {0};
  ParseResult pr = parser_parse(lexer_lex(argv[1], &a), &a);
  if (pr.count < 1 || !pr.nodes[0]) return 3;
  size_t n; unsigned char *b = synw_encode(pr.nodes[0], &n);
  if (!b) return 4;
  fwrite(b, 1, n, stdout);
  free(b);
  return 0;
}
