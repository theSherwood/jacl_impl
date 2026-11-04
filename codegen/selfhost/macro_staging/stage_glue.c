/* Staged-macro I/O glue — compiled INTO the runtime module (see jaclrt_staging.c) so its
 * jacl_vec_* calls are in-unit and only read/write cross the boundary (the recognized
 * Stream caps). The codegen'd staged-macro entry calls these by name (call.sym).
 *   synrt_read_arg()          read the argument syntax value (wire) from stdin -> plain-data vec
 *   synrt_write_result(vec)   encode the result (expansion or error) -> wire -> stdout
 * See docs/TEMEN_MACRO_STAGING_PLAN.md ("final link"). Included after jaclrt.c, so the
 * runtime's `write` (io.c) is in scope; `read` isn't declared there, so declare it (the
 * on-ramp Stream.read prototype, as tcl_repl.c uses). */
#include <stddef.h>
#include <string.h>

/* The TEMEN build has no libc malloc here (jaclrt uses its own GC allocator, so the runtime
 * never references malloc; temen-llvm doesn't synthesize it in this translate). syn_rt's small,
 * short-lived wire buffers back onto a static bump arena instead. A 16-byte header records
 * each block's size so realloc copies correctly; free is a no-op (one-shot run). Native
 * builds of syn_rt.c use the real libc malloc — this file is only in the TEMEN staging unity. */
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

/* Stateful across calls: the argument wire is `version byte, header, then the parameter values`
 * (the header is syn_rt's: the call's position and each position handle's).
 * Fixed parameters are one syntax value each; a variadic macro's trailing rest parameter is
 * `u32 count, then count syntax values`. The codegen entry calls `synrt_read_arg` once per
 * fixed parameter (in order) and `synrt_read_rest` for the rest parameter. The first read
 * slurps stdin and skips the shared version byte. */
static unsigned char *g_arg_buf;
static size_t g_arg_len, g_arg_pos;
static int g_arg_ready;
static void synrt_args_ensure(void) {
  if (g_arg_ready) return;
  size_t cap = 1 << 16, len = 0; unsigned char *buf = (unsigned char *)malloc(cap);
  for (;;) {
    if (len == cap) { cap *= 2; buf = (unsigned char *)realloc(buf, cap); }
    long r = read(0, buf + len, (long)(cap - len));
    if (r <= 0) break;
    len += (size_t)r;
  }
  g_arg_buf = buf; g_arg_len = len; g_arg_ready = 1;
  /* Skip the shared version byte and read the header; a bad/empty wire leaves the cursor at end
   * (reads -> nil). */
  g_arg_pos = (len >= 1 && buf[0] == (unsigned char)synrt_wire_version()) ? 1 : len;
  if (g_arg_pos < len) synrt_read_header(buf, len, &g_arg_pos);
}
/* syn_rt's hook for `syntax-pos`, which needs the header even in a macro with no parameters. */
void synrt_load_args(void) { synrt_args_ensure(); }
JaclVal synrt_read_arg(void) {
  synrt_args_ensure();
  return synrt_decode_node(g_arg_buf, g_arg_len, &g_arg_pos);
}
/* Read a variadic rest parameter: a u32 count followed by that many syntax values, collected
 * into a jaclrt vector (the plain-data representation of a vec-of-syntax that `~@` splices). */
JaclVal synrt_read_rest(void) {
  synrt_args_ensure();
  uint32_t count = 0;
  if (g_arg_pos + 4 <= g_arg_len) { memcpy(&count, g_arg_buf + g_arg_pos, 4); g_arg_pos += 4; }
  JaclVal vec = jacl_vec_empty();
  for (uint32_t i = 0; i < count; i++)
    vec = jacl_vec_push(vec, synrt_decode_node(g_arg_buf, g_arg_len, &g_arg_pos));
  return vec;
}

/* `[gensym]` / `[gensym "prefix"]` inside a staged-macro syntax-quote: mint a fresh name and
 * return it as a plain-data var-ref syntax value (the codegen entry emits a `synrt_gensym`
 * call.sym here). `prefix__N`, N counting this run's gensyms; the host renames each for the
 * expansion that made it when the result comes back (syn_wire.c), since every expansion is a
 * fresh run of this module and would otherwise reuse `prefix__0`. */
JaclVal synrt_gensym(JaclVal prefix) {
  static uint32_t g_gensym_ctr;
  return synrt_gensym_node(prefix, g_gensym_ctr++);
}

/* Returns `v` (not void) so a codegen `call.sym` — which expects a single i64 result —
 * has a matching signature; the caller ignores it. */
JaclVal synrt_write_result(JaclVal v) {
  size_t n; unsigned char *out = synrt_encode_result(v, &n);
  if (out) { (void)write(1, (const char *)out, (long)n); }
  return v;
}

