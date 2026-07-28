/* Runtime-side helper: read syn_wire bytes on stdin, synrt_decode -> plain-data vec,
 * synrt_encode -> bytes on stdout. Built native with the __vm_* shim + full runtime. */
#include "rt_native_shim.c"
#include "jaclrt.c"
#include "syn_rt.c"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
  jacl_heap_init(); jacl_intern_init();
  size_t cap = 1 << 16, len = 0; unsigned char *buf = malloc(cap); int c;
  while ((c = getchar()) != EOF) { if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); } buf[len++] = (unsigned char)c; }
  JaclVal v = synrt_decode(buf, len);
  size_t n; unsigned char *out = synrt_encode(v, &n);
  if (out) { fwrite(out, 1, n, stdout); free(out); return 0; }
  return 1;
}
