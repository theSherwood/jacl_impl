/* Staged-macro I/O glue for the **in-guest** staging path (guest-JIT, docs/SVM_GUEST_JIT_STAGING.md
 * item 3/4). The difference from stage_glue.c (the native/separate-module path):
 *
 *   - No malloc/free/realloc override. This TU is linked INTO the compiler `.svmb`, which already
 *     has an allocator (the on-ramp's + emit_shim.c); redefining malloc here would clobber it.
 *   - synrt_read_arg / synrt_read_rest read the argument wire from an in-window **buffer** the
 *     staging hook sets (`synrt_set_arg_wire`), not from stdin — a JITed unit invoked via
 *     `__vm_jit_invoke2(code, sp, _)` has no stdin.
 *   - synrt_write_result appends the result wire to an in-window **buffer** the hook drains
 *     (`synrt_take_result`), not stdout.
 *
 * These synrt_* run in the *compiler's* domain (the JITed macro entry reaches them through the
 * shared call_indirect table by slot — see the symtab the hook builds), so the buffers are plain
 * compiler globals: set the arg wire, invoke, drain the result. The jacl_vec_* / jacl_str_* the
 * glue calls are the compiler's own linked runtime copies. */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>   /* malloc/realloc/free — the compiler's allocator */

#include "syn_rt.c"

/* ---- argument wire (hook -> macro), read by synrt_read_arg/read_rest ---- */

static const unsigned char *g_arg_buf;
static size_t g_arg_len, g_arg_pos;

/* Install the argument wire for the next staged run and reset the read cursor past the shared
 * version byte (a bad/empty header parks the cursor at end, so reads yield nil). Called by the
 * staging hook before `__vm_jit_invoke2`. */
void synrt_set_arg_wire(const unsigned char *buf, size_t len) {
  g_arg_buf = buf;
  g_arg_len = len;
  g_arg_pos = (len >= 1 && buf[0] == (unsigned char)synrt_wire_version()) ? 1 : len;
}

JaclVal synrt_read_arg(void) {
  return synrt_decode_node(g_arg_buf, g_arg_len, &g_arg_pos);
}

/* Read a variadic rest parameter: a u32 count then that many syntax values, into a jaclrt vec. */
JaclVal synrt_read_rest(void) {
  uint32_t count = 0;
  if (g_arg_pos + 4 <= g_arg_len) { memcpy(&count, g_arg_buf + g_arg_pos, 4); g_arg_pos += 4; }
  JaclVal vec = jacl_vec_empty();
  for (uint32_t i = 0; i < count; i++)
    vec = jacl_vec_push(vec, synrt_decode_node(g_arg_buf, g_arg_len, &g_arg_pos));
  return vec;
}

/* ---- result wire (macro -> hook), written by synrt_write_result, drained by synrt_take_result ---- */

static unsigned char *g_res_buf;
static size_t g_res_len;

/* Encode `v` to the result wire and stash it for the hook to drain. Returns `v` (a codegen
 * `call.sym` expects one i64 result; the caller ignores it). A macro writes its result once. */
JaclVal synrt_write_result(JaclVal v) {
  size_t n; unsigned char *out = synrt_encode(v, &n);
  if (out) { free(g_res_buf); g_res_buf = out; g_res_len = n; }
  return v;
}

/* Hand the staging hook the last result wire (ownership transfers — the hook frees it) and clear
 * the slot so a subsequent run starts empty. NULL with `*len == 0` if the macro wrote nothing. */
unsigned char *synrt_take_result(size_t *len) {
  unsigned char *out = g_res_buf;
  *len = g_res_len;
  g_res_buf = NULL;
  g_res_len = 0;
  return out;
}

/* `[gensym]` / `[gensym "prefix"]`: mint a fresh hygienic name `prefix__N` as a plain-data
 * var-ref syntax value `[VAR_REF, scope_mark=0, is_gensym, name]`. A per-run counter suffices —
 * gensym'd names are unique and end up dead in the emitted IR, so the exact N is unobservable. */
JaclVal synrt_gensym(JaclVal prefix) {
  static uint32_t g_gensym_ctr;
  static char name[80];
  uint32_t plen = 0;
  if (!jaclrt_is_nil(prefix)) {
    plen = jacl_str_len(prefix);
    if (plen > 64) plen = 64;
    jacl_str_bytes(prefix, name, sizeof name);
  }
  if (plen == 0) { name[0] = 'g'; plen = 1; }
  uint32_t nl = plen;
  name[nl++] = '_'; name[nl++] = '_';
  uint32_t n = g_gensym_ctr++;
  char d[16]; int dc = 0;
  if (n == 0) d[dc++] = '0';
  while (n > 0) { d[dc++] = (char)('0' + (n % 10)); n /= 10; }
  for (int i = dc - 1; i >= 0; i--) name[nl++] = d[i];
  JaclVal namev = jacl_str_new(name, nl);
  JaclVal v = jacl_vec_empty();
  v = jacl_vec_push(v, jaclrt_i32(4));   /* AST_VAR_REF */
  v = jacl_vec_push(v, jaclrt_i32(0));   /* scope_mark */
  v = jacl_vec_push(v, jaclrt_i32(2));   /* flags: is_gensym */
  v = jacl_vec_push(v, namev);
  return v;
}
