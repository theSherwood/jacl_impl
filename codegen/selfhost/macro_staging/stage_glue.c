/* Staged-macro I/O glue — compiled INTO the runtime module (see jaclrt_staging.c) so its
 * jacl_vec_* calls are in-unit and only read/write cross the boundary (the recognized
 * Stream caps). The codegen'd staged-macro entry calls these by name (call.sym).
 *   synrt_read_arg()          read the argument syntax value (wire) from stdin -> plain-data vec
 *   synrt_write_result(vec)   encode the result syntax value -> wire -> stdout
 * See docs/SVM_MACRO_STAGING_PLAN.md ("final link"). Included after jaclrt.c, so the
 * runtime's `write` (io.c) is in scope; `read` isn't declared there, so declare it (the
 * on-ramp Stream.read prototype, as tcl_repl.c uses). */
#include <stddef.h>
#include <string.h>

/* The SVM build has no libc malloc here (jaclrt uses its own GC allocator, so the runtime
 * never references malloc; svm-llvm doesn't synthesize it in this translate). syn_rt's small,
 * short-lived wire buffers back onto a static bump arena instead. A 16-byte header records
 * each block's size so realloc copies correctly; free is a no-op (one-shot run). Native
 * builds of syn_rt.c use the real libc malloc — this file is only in the SVM staging unity. */
static __attribute__((aligned(16))) char g_arena[1u << 18];
static size_t g_off;
void *malloc(size_t n) {
  size_t t = (n + 16 + 15) & ~(size_t)15;
  if (g_off + t > sizeof g_arena) return NULL;
  char *base = &g_arena[g_off]; g_off += t;
  *(size_t *)base = n;
  return base + 16;
}
void free(void *p) { (void)p; }
void *realloc(void *p, size_t n) {
  void *q = malloc(n);
  if (p && q) { size_t old = *(size_t *)((char *)p - 16); memcpy(q, p, old < n ? old : n); }
  return q;
}

#include "syn_rt.c"

extern long read(int fd, void *buf, long n);

JaclVal synrt_read_arg(void) {
  size_t cap = 1 << 16, len = 0; unsigned char *buf = (unsigned char *)malloc(cap);
  for (;;) {
    if (len == cap) { cap *= 2; buf = (unsigned char *)realloc(buf, cap); }
    long r = read(0, buf + len, (long)(cap - len));
    if (r <= 0) break;
    len += (size_t)r;
  }
  return synrt_decode(buf, len);
}

/* Returns `v` (not void) so a codegen `call.sym` — which expects a single i64 result —
 * has a matching signature; the caller ignores it. */
JaclVal synrt_write_result(JaclVal v) {
  size_t n; unsigned char *out = synrt_encode(v, &n);
  if (out) { (void)write(1, (const char *)out, (long)n); }
  return v;
}

