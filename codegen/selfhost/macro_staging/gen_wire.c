/* Compiler-side helper: parse each JACL snippet argument, encode its nodes[0] to
 * syn_wire bytes on stdout. With one argument this is a single-value wire (version
 * byte + node); with several it is the multi-arity macro-argument wire (one shared
 * version byte, then each node back-to-back). Used by the cross-codec / staging tests
 * to feed the runtime side. */
#include "../../../src/jacl.c"
#include "syn_wire.c"
#include <stdio.h>
int main(int argc, char **argv) {
  if (argc < 2) return 2;
  for (int i = 1; i < argc; i++) {
    arena_t a = {0};
    ParseResult pr = parser_parse(lexer_lex(argv[i], &a), &a);
    if (pr.count < 1 || !pr.nodes[0]) return 3;
    size_t n; unsigned char *b = synw_encode(pr.nodes[0], &n);
    if (!b) return 4;
    /* synw_encode prepends the version byte; keep it for the first node, strip it for
     * the rest so the stream is `version, node0, node1, …`. */
    if (i == 1) fwrite(b, 1, n, stdout);
    else if (n > 1) fwrite(b + 1, 1, n - 1, stdout);
    free(b);
  }
  return 0;
}
