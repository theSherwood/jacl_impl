/* interpcap.c — the "interp" named-capability adapter for `[interpret …]` / `[interpret-prelude]`.
 *
 * `interpret` is metacircular eval: it compiles and runs JACL source in a sandboxed child VM at
 * runtime. The SVM backend is ahead-of-time — the guest is svm bytecode with no JACL compiler or
 * VM inside it — so unlike a pure-runtime builtin this resolves a host capability by name ("interp")
 * and ships the source (plus, for the 2-arg sandbox form, the set of allowed prelude names) to the
 * embedder, which runs a JACL evaluator on the host and marshals a scalar/error result back.
 * Structurally a sibling of execcap.c / fscap.c. Ungranted, `interpret` is a catchable error (a
 * compiler is pure host authority; there is no in-guest fallback).
 *
 * `interpret-prelude` needs no host round-trip: it just materializes, guest-side, the map of the
 * default permissive prelude (every non-core / capability-sensitive builtin name → a placeholder),
 * so a program can derive a restricted subset with map-remove / map-set and hand it back to
 * `interpret`. The name list MUST stay in sync with src/compiler.c's jacl_non_core_builtins.
 *
 * `__vm_cap_resolve` / `__vm_host_call` are svm-llvm intrinsics lowered at translate time. */
#include <stdint.h>
#include <string.h>

extern int  __vm_cap_resolve(const char *name, long len);
extern long __vm_host_call(int h, int op, long a, long b, long c, long d);

/* interp wire ops. run(req_ptr, src_len, names_len, out_ptr): the request buffer holds the source
 * bytes [0, src_len) followed by the NUL-separated allowed-name list [src_len, src_len+names_len);
 * the host writes an 8-byte tag + 8-byte value into the 16-byte out buffer. */
#define JINTERP_DEFAULT 0   /* [interpret SRC]          — default prelude (allow all) */
#define JINTERP_SANDBOX 1   /* [interpret PRELUDE SRC]  — closed-world to the shipped names */

/* Result tags the host writes to out[0]. */
#define JINTERP_TAG_ERROR 0
#define JINTERP_TAG_INT   1

/* The default permissive prelude: every non-core builtin name. Kept identical to
 * src/compiler.c's jacl_non_core_builtins (the reference OP_INTERPRET_PRELUDE source). */
static const char *const jacl_interp_prelude_names[] = {
  "print", "interpret", "interpret-prelude",
  "spawn", "await", "parallel", "race", "yield",
  "make-syntax", "syntax-error",
  "box", "atom", "deref", "reset", "swap",
  "watch", "unwatch",
  "lines", "stream_next",
  "exec", "signal", "cancel",
  "read-file", "write-file", "append-file",
  "delete-file", "file-exists?", "list-dir",
  0
};

__attribute__((noinline)) static long jacl_interp_pti(const void *p) { return (long)(uintptr_t)p; }

/* svm-llvm requires a *constant* `op` on __vm_host_call. A single call site branching on a
 * variable op gets tail-merged back into one variable-op call, so each form gets its own noinline
 * wrapper — two distinct bodies clang cannot merge, each carrying a literal op. */
__attribute__((noinline)) static long jacl_interp_call_default(int h, long req, long sl, long nl, long out) {
  return __vm_host_call(h, JINTERP_DEFAULT, req, sl, nl, out);
}
__attribute__((noinline)) static long jacl_interp_call_sandbox(int h, long req, long sl, long nl, long out) {
  return __vm_host_call(h, JINTERP_SANDBOX, req, sl, nl, out);
}

/* Probe once, cache the handle. -2 = unprobed; negative = not granted; >=0 = the handle. */
static int jacl_interp_handle = -2;
static int jacl_interp_h(void) {
  if (jacl_interp_handle == -2) jacl_interp_handle = __vm_cap_resolve("interp", 6);
  return jacl_interp_handle;
}
int jacl_interp_granted(void) { return jacl_interp_h() >= 0; }

/* `[interpret-prelude]` — the default prelude as a map {name: nil}. Pure guest-side; the values
 * are placeholders (interpret keys on the map's names, not its values). */
JaclVal jacl_interpret_prelude(void) {
  JaclVal m = jacl_map_empty();
  for (int i = 0; jacl_interp_prelude_names[i]; i++) {
    const char *nm = jacl_interp_prelude_names[i];
    m = jacl_map_set(m, jacl_str_new(nm, (uint32_t)strlen(nm)), JACL_NIL);
  }
  return m;
}

/* Shared marshal + host round-trip. `names` is NULL for the default form; otherwise a NUL-separated
 * blob of allowed names of length `names_len`. Returns the interpreted result as a JaclVal. */
static JaclVal jacl_interp_run(int op, JaclVal src, const char *names, uint32_t names_len) {
  if (jaclrt_is_error(src)) return src;
  if (!jaclrt_is_string(src)) return jaclrt_error();
  int h = jacl_interp_h();
  if (h < 0) return jaclrt_error();

  static char req[1 << 16];
  uint32_t src_len = jacl_str_len(src);
  if ((uint64_t)src_len + names_len > sizeof req) return jaclrt_error();
  jacl_str_bytes(src, req, sizeof req);
  if (names_len) memcpy(req + src_len, names, names_len);

  int64_t out[2] = {0, 0};
  long r = (op == JINTERP_SANDBOX)
             ? jacl_interp_call_sandbox(h, jacl_interp_pti(req), (long)src_len,
                                        (long)names_len, jacl_interp_pti(out))
             : jacl_interp_call_default(h, jacl_interp_pti(req), (long)src_len,
                                        (long)names_len, jacl_interp_pti(out));
  if (r < 0) return jaclrt_error();
  if (out[0] == JINTERP_TAG_INT) return jaclrt_i32((int32_t)out[1]);
  return jaclrt_error();
}

/* `[interpret SRC]` — run SRC with the default prelude. */
JaclVal jacl_interpret1(JaclVal src) {
  return jacl_interp_run(JINTERP_DEFAULT, src, 0, 0);
}

/* `[interpret PRELUDE SRC]` — run SRC closed-world to PRELUDE's keys (the allowed names). */
JaclVal jacl_interpret2(JaclVal prelude, JaclVal src) {
  if (jaclrt_is_error(prelude)) return prelude;
  if (jaclrt_is_error(src)) return src;
  if (!jaclrt_is_string(src)) return jaclrt_error();

  /* Marshal the prelude map's keys into a NUL-separated blob. */
  static char names[1 << 12];
  uint32_t nlen = 0;
  JaclVal keys = jacl_map_keys_v(prelude);
  if (jaclrt_is_error(keys)) return keys;
  int32_t nk = jaclrt_as_i32(jacl_len(keys));
  for (int32_t i = 0; i < nk; i++) {
    JaclVal k = jacl_vec_get_at(keys, jaclrt_i32(i));
    if (!jaclrt_is_string(k)) continue;
    uint32_t kl = jacl_str_len(k);
    if (nlen + kl + 1 > sizeof names) return jaclrt_error();
    jacl_str_bytes(k, names + nlen, (uint32_t)(sizeof names - nlen));
    nlen += kl;
    names[nlen++] = 0;
  }
  return jacl_interp_run(JINTERP_SANDBOX, src, names, nlen);
}
