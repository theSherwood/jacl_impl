/* Frontend helper: read a staged macro's result wire on stdin, decode the expansion to an
 * AstNode, pretty-print it. The host side of reading a staged macro's result. */
#include "../../../src/jacl.c"
#include "syn_wire.c"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  size_t cap = 1 << 16, len = 0; unsigned char *buf = malloc(cap); int c;
  while ((c = getchar()) != EOF) { if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); } buf[len++] = (unsigned char)c; }
  arena_t a = {0};
  const char *err = NULL;
  AstNode *n = synw_decode_result(buf, len, &a, NULL, &err);
  if (!n) { fprintf(stderr, "decode failed: %s\n", err ? err : "?"); return 1; }
  printf("%s\n", ast_pretty_print(n, &a));
  return 0;
}
