/* Runtime-side helper: read a single-value syn_wire on stdin, decode it to the plain-data
 * syntax value and print `syntax-str` of it — for the test that syntax-str agrees with the
 * frontend's pretty-printer. Built native with the __vm_* shim + full runtime. */
#include "rt_native_shim.c"
#include "jaclrt.c"
#include "syn_rt.c"
#include <stdio.h>
#include <stdlib.h>
void synrt_load_args(void) {}   /* syn_rt's hook into a glue; this tool reads no argument wire */
int main(void) {
  jacl_heap_init(); jacl_intern_init(); jacl_map_init();
  size_t cap = 1 << 16, len = 0; unsigned char *buf = malloc(cap); int c;
  while ((c = getchar()) != EOF) { if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); } buf[len++] = (unsigned char)c; }
  JaclVal s = synrt_syntax_str(synrt_decode(buf, len));
  if (jaclrt_is_error(s) || !jaclrt_is_string(s)) return 1;
  uint32_t n = jacl_str_len(s);
  char *out = malloc(n + 1);
  jacl_str_bytes(s, out, n + 1);
  fwrite(out, 1, n, stdout);
  return 0;
}
