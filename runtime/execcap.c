/* execcap.c — the "exec" named-capability adapter (docs/SVM_EXEC_ASK.md).
 *
 * Shell-out (`!cmd args`) resolves the "exec" capability by name and runs the command to
 * completion, returning its stdout as a string. Ungranted, it is a *catchable error*: a
 * subprocess is pure host authority, so — unlike the filesystem's guest VFS — there is no
 * in-guest fallback to emulate it. The embedder grants either the real `host_exec(allowlist)`
 * backend or a deterministic `scripted_exec` table (svm-run's `exec` cap; wire protocol in
 * crates/svm-exec). Structurally a mirror of fscap.c.
 *
 * `__vm_cap_resolve` / `__vm_host_call` are svm-llvm intrinsics lowered at translate time. */
#include <stdint.h>
#include <string.h>

extern int  __vm_cap_resolve(const char *name, long len);
extern long __vm_host_call(int h, int op, long a, long b, long c, long d);

/* exec wire ops (crates/svm-exec/src/lib.rs). */
#define JEXEC_RUN       0   /* run(argv_ptr, argv_len, stdin_ptr, stdin_len) -> job | -errno */
#define JEXEC_READ_OUT  1   /* read_out(job, buf, cap) -> n | 0 = EOF | -errno */
#define JEXEC_STATUS    3   /* status(job) -> exit code | -errno */
#define JEXEC_CLOSE     4   /* close(job) -> 0 | -errno */

/* Pointer→long across the noinline seam (matches fscap.c; keeps the address opaque to the
 * optimizer so the confinement mask on the host-call argument survives). */
__attribute__((noinline)) static long jacl_exec_pti(const void *p) { return (long)(uintptr_t)p; }

/* Probe once, cache the handle. -2 = unprobed; negative = not granted; >=0 = the handle. */
static int jacl_exec_handle = -2;
static int jacl_exec_h(void) {
  if (jacl_exec_handle == -2) jacl_exec_handle = __vm_cap_resolve("exec", 4);
  return jacl_exec_handle;
}
int jacl_exec_granted(void) { return jacl_exec_h() >= 0; }

/* `!cmd args...` — run the command and capture its stdout as a string. `argv` is a JACL vector
 * [name, arg1, ...] of strings (name = argv[0]). Ungranted, or a spawn/permission failure, is a
 * catchable error value. v1 semantics: run to completion, then return the whole stdout. */
JaclVal jacl_exec_capture(JaclVal argv) {
  if (jaclrt_is_error(argv)) return argv;
  int h = jacl_exec_h();
  if (h < 0) return jaclrt_error();

  /* Marshal argv into NUL-separated bytes: name\0arg1\0arg2\0... */
  char ab[4096];
  uint32_t alen = 0;
  int32_t n = jaclrt_as_i32(jacl_len(argv));
  for (int32_t i = 0; i < n; i++) {
    JaclVal e = jacl_vec_get_at(argv, jaclrt_i32(i));
    if (jaclrt_is_error(e)) return e;
    if (!jaclrt_is_string(e)) return jaclrt_error();
    uint32_t el = jacl_str_len(e);
    if (alen + el + 1 > sizeof ab) return jaclrt_error();
    jacl_str_bytes(e, ab + alen, (uint32_t)(sizeof ab - alen));
    alen += el;
    ab[alen++] = 0;
  }

  long job = __vm_host_call(h, JEXEC_RUN, jacl_exec_pti(ab), (long)alen, 0, 0);
  if (job < 0) return jaclrt_error();

  /* Drain stdout in chunks until EOF (0). */
  static char out[1 << 16];
  uint32_t got = 0;
  for (;;) {
    long r = __vm_host_call(h, JEXEC_READ_OUT, job, jacl_exec_pti(out + got),
                            (long)(sizeof out - got), 0);
    if (r <= 0) break;
    got += (uint32_t)r;
    if (got >= sizeof out) break;
  }
  (void)__vm_host_call(h, JEXEC_STATUS, job, 0, 0, 0);
  (void)__vm_host_call(h, JEXEC_CLOSE, job, 0, 0, 0);
  return jacl_str_new(out, got);
}
