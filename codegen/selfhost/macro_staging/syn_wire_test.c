/* Round-trip test for syn_wire: parse a snippet -> AST -> bytes -> AST, and assert
 * the pretty-print and a re-encode are identical. Unity build over the frontend. */
#include "../../../src/jacl.c"
#include "syn_wire.c"
#include <stdio.h>

static int roundtrip(const char *src) {
  arena_t a = {0};
  ParseResult pr = parser_parse(lexer_lex(src, &a), &a);
  if (pr.count < 1 || !pr.nodes[0]) { printf("  PARSE FAIL: %s\n", src); return 1; }
  AstNode *orig = pr.nodes[0];
  size_t n1; unsigned char *b1 = synw_encode(orig, &n1);
  if (!b1) { printf("  ENCODE FAIL (unsupported kind): %s\n", src); return 1; }
  AstNode *dec = synw_decode(b1, n1, &a);
  if (!dec) { printf("  DECODE FAIL: %s\n", src); free(b1); return 1; }
  const char *p1 = ast_pretty_print(orig, &a), *p2 = ast_pretty_print(dec, &a);
  int ok_pp = p1 && p2 && strcmp(p1, p2) == 0;
  size_t n2; unsigned char *b2 = synw_encode(dec, &n2);
  int ok_bytes = b2 && n1 == n2 && memcmp(b1, b2, n1) == 0;
  free(b1); free(b2);
  if (ok_pp && ok_bytes) { printf("  PASS  %-32s (%zu bytes)\n", src, n1); return 0; }
  printf("  FAIL  %s\n    pp1=[%s]\n    pp2=[%s]\n    bytes_eq=%d\n", src, p1 ? p1 : "?", p2 ? p2 : "?", ok_bytes);
  return 1;
}

int main(void) {
  const char *cases[] = {
    "42", "foo", "[+ 1 2]", "[+ 1 [* 2 3]]",
    "[if [== 1 2] {} { set hit 5 }]", "{ set x 1 }", "[twice [twice 5]]",
  };
  int fails = 0;
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) fails += roundtrip(cases[i]);
  printf(fails ? "syn_wire: %d FAILED\n" : "syn_wire: all round-trips passed\n", fails);
  return fails ? 1 : 0;
}
