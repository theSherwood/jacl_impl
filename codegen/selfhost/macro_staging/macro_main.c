/* Staged-macro wrapper (arity-1). The entry of a staged macro module: read the argument
 * syntax value from stdin (syn_rt wire), decode it, call the codegen'd __jacl_macro, and
 * write the encoded result syntax value to stdout. read(0)/write(1) are POSIX natively
 * and the on-ramp Stream caps on SVM, so the same source serves both. __jacl_macro is
 * resolved by name at link time (svm_codegen_macro_body's export). */
#include "jaclrt.h"
#include "syn_rt.h"
#include <unistd.h>
#include <stdlib.h>

extern JaclVal __jacl_macro(JaclVal arg0);

int main(void) {
  jacl_heap_init(); jacl_intern_init();
  size_t cap = 1 << 16, len = 0; unsigned char *buf = (unsigned char *)malloc(cap);
  for (;;) {
    if (len == cap) { cap *= 2; buf = (unsigned char *)realloc(buf, cap); }
    long r = read(0, buf + len, (long)(cap - len));
    if (r <= 0) break;
    len += (size_t)r;
  }
  JaclVal x = synrt_decode(buf, len);
  JaclVal res = __jacl_macro(x);
  size_t n; unsigned char *out = synrt_encode(res, &n);
  if (out) { ssize_t w = write(1, out, n); (void)w; }
  return 0;
}
