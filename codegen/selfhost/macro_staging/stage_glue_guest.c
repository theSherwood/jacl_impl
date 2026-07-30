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

JaclVal synrt_gensym(JaclVal prefix);   /* defined below; referenced by the symtab helper */

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

/* ---- compile_linked symbol table (name -> table slot) ---- */

/* The staging hook hands this to `__vm_jit_compile_linked`: it binds each runtime symbol a staged
 * macro body may import (`call.sym "jacl_vec_push"`, …) to that function's **call_indirect slot**.
 * A module function's funcref index *is* its slot, and svm-llvm lowers `&fn` to that index, so a
 * slot is just `(unsigned long)(void *)&fn`. Wire form (svm-run::decode_symbol_table): uleb count,
 * then per entry uleb namelen + name bytes + kind byte 0 (Slot) + uleb slot. The compile_linked
 * resolver uses only the names the unit actually imports, so a superset is fine; a *missing* name
 * fails closed. The `#define` renames in jaclrt_staging_guest.c are transparent here — `SYM(name)`
 * stringizes the canonical import name but takes `&name`, which expands to the (possibly `jrtg_`-
 * renamed) definition. Extend the list as macros reach further into the runtime. */

static void symtab_uleb(unsigned char *out, size_t cap, size_t *pos, unsigned long v) {
  for (;;) {
    unsigned char b = (unsigned char)(v & 0x7f);
    v >>= 7;
    if (*pos < cap) out[*pos] = v ? (b | 0x80) : b;
    (*pos)++;
    if (!v) break;
  }
}
static void symtab_entry(unsigned char *out, size_t cap, size_t *pos, const char *name,
                         unsigned long slot) {
  size_t nl = 0;
  while (name[nl]) nl++;
  symtab_uleb(out, cap, pos, (unsigned long)nl);
  for (size_t i = 0; i < nl; i++) { if (*pos < cap) out[*pos] = (unsigned char)name[i]; (*pos)++; }
  if (*pos < cap) out[*pos] = 0; /* kind 0 = Slot */
  (*pos)++;
  symtab_uleb(out, cap, pos, slot);
}

/* Emit the symtab into `out` (up to `cap` bytes) and return its true length; if the return value
 * exceeds `cap`, `out` was too small (call again with a bigger buffer). `#SYM(f)` = one entry
 * binding the import name "f" to `Slot(&f)`. */
size_t synrt_build_symtab(unsigned char *out, size_t cap) {
  static const unsigned long COUNT_PLACEHOLDER = 0;
  (void)COUNT_PLACEHOLDER;
  /* Count entries first (so the leading uleb count is exact) by running the body twice. */
  size_t n = 0, pos = 0;
#define SYM(f) do { \
    if (counting) n++; \
    else symtab_entry(out, cap, &pos, #f, (unsigned long)(void *)&f); \
  } while (0)
#define SYMLIST \
    /* the staged-macro I/O glue */ \
    SYM(synrt_read_arg); SYM(synrt_read_rest); SYM(synrt_write_result); SYM(synrt_gensym); \
    /* vectors / arrays (the plain-data syntax representation) */ \
    SYM(jacl_vec_empty); SYM(jacl_vec_push); SYM(jacl_vec_push_v); SYM(jacl_vec_get); \
    SYM(jacl_vec_get_at); SYM(jacl_vec_set_at); SYM(jacl_vec_concat); SYM(jacl_vec_slice); \
    SYM(jacl_arr_new); SYM(jacl_arr_push); SYM(jacl_arr_get_at); \
    /* strings */ \
    SYM(jacl_str_new); SYM(jacl_str_concat); SYM(jacl_to_string); \
    /* scalars / misc used when a macro body computes */ \
    SYM(jacl_len); SYM(jacl_eq); SYM(jacl_add); SYM(jacl_sub); SYM(jacl_mul); \
    SYM(jacl_first); SYM(jacl_index_get)
  int counting = 1;
  SYMLIST;
  /* Rewind and emit for real, prefixed by the count. */
  counting = 0;
  pos = 0;
  symtab_uleb(out, cap, &pos, (unsigned long)n);
  SYMLIST;
#undef SYM
#undef SYMLIST
  return pos;
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
