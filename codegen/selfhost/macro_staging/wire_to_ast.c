/* Frontend helper: read syn_wire bytes on stdin, decode to an AstNode, pretty-print it.
 * The host side of reading a staged macro's result. */
#include "../../../src/jacl.c"
#include "syn_wire.c"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  size_t cap = 1 << 16, len = 0; unsigned char *buf = malloc(cap); int c;
  while ((c = getchar()) != EOF) { if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); } buf[len++] = (unsigned char)c; }
  arena_t a = {0};
  AstNode *n = synw_decode(buf, len, &a);
  if (!n) { fprintf(stderr, "decode failed\n"); return 1; }
  printf("%s\n", ast_pretty_print(n, &a));
  return 0;
}
