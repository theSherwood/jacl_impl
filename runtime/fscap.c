/* fscap.c — the "fs" named-capability adapter (docs/SVM_FS_DESIGN.md, layer 2).
 *
 * At the first file builtin, probe the §7 capability-name directory for an embedder-granted
 * "fs" capability (svm-fs wire protocol: crates/svm-fs). Granted, `read-file` /
 * `write-file` / `append-file` route through real cap ops — against whatever backend the
 * host chose (in-memory mem_fs, a seeded data image, or host_fs rooted at a directory).
 * Not granted, the callers in io.c fall back to the pure-guest VFS map, so a program runs
 * identically on a host with no filesystem at all. Capability decides authority; the
 * language surface never changes.
 *
 * Path model: JACL's absolute namespace is rooted at the grant — the adapter strips the
 * leading '/'s, so JACL's "/tmp/x.txt" names "tmp/x.txt" inside the granted root. Both
 * svm-fs backends refuse absolute / ".." / empty paths host-side (-EACCES), so vetting is
 * not duplicated here. Errors (negative errnos) surface as JACL error values — the same
 * try/catch-able failures the guest VFS produces, so `# expect-error` oracles keep matching.
 *
 * `__vm_cap_resolve` / `__vm_host_call` are svm-llvm intrinsics lowered at translate time
 * to `cap.self.resolve` / `cap.call` (each call site passes its op as a literal — the
 * lowering requires a constant op). */
#include <string.h>

extern int  __vm_cap_resolve(const char *name, long len);
extern long __vm_host_call(int h, int op, long a, long b, long c, long d);

/* svm-fs protocol constants (crates/svm-fs/src/lib.rs). Ops are passed as literals at each
 * call site (constant-op requirement); these name the flag/whence values only. */
#define JFS_O_READ    1
#define JFS_O_WRITE   2
#define JFS_O_APPEND  4
#define JFS_O_TRUNC   8
#define JFS_O_CREATE 16
#define JFS_SEEK_SET  0
#define JFS_SEEK_END  2
#define JFS_STATBUF_LEN 72
#define JFS_S_IFMT  0170000u
#define JFS_S_IFDIR 0040000u

/* svm-llvm rejects a *constant* ptrtoint; route addresses through a noinline call so they
 * lower to runtime instructions (mirrors flatbuf.c's jacl_fb_pti). */
__attribute__((noinline)) static long jacl_fs_pti(const void *p) { return (long)(uintptr_t)p; }

/* Probe once, cache the handle. -2 = unprobed; negative = not granted; >=0 = the handle. */
static int jacl_fs_handle = -2;
static int jacl_fs(void) {
  if (jacl_fs_handle == -2) jacl_fs_handle = __vm_cap_resolve("fs", 2);
  return jacl_fs_handle;
}
int jacl_fs_granted(void) { return jacl_fs() >= 0; }

/* Copy a JACL path string into `buf`, mapping it into the granted root (strip leading
 * '/'s). Returns the mapped length, or -1 on an over-long path. */
static long jacl_fs_path(JaclVal path, char *buf, uint32_t cap) {
  uint32_t len = jacl_str_len(path);
  if (len + 1 > cap) return -1;
  jacl_str_bytes(path, buf, cap);
  buf[len] = 0;
  uint32_t s = 0;
  while (buf[s] == '/') s++;
  if (s) memmove(buf, buf + s, (size_t)(len - s) + 1);
  return (long)(len - s);
}

/* Parent-directory gate for a create-open: stat the mapped path's parent and require a
 * directory. A rootward path (no '/' — its parent is the granted root itself) passes.
 * Mirrors open()'s ENOENT on a missing directory — and the guest VFS's parent model — so a
 * granted and an ungranted run score identically (mem_fs's own open(O_CREATE) would happily
 * create at any depth). */
static int jacl_fs_parent_ok(int h, const char *p, long plen) {
  long slash = -1;
  for (long i = 0; i < plen; i++)
    if (p[i] == '/') slash = i;
  if (slash <= 0) return 1;                       /* parent is the granted root */
  char sb[JFS_STATBUF_LEN];
  long r = __vm_host_call(h, 14 /*stat*/, jacl_fs_pti(p), slash, jacl_fs_pti(sb), JFS_STATBUF_LEN);
  if (r != 0) return 0;
  uint32_t mode;
  memcpy(&mode, sb, 4);                           /* StatBuf offset 0: mode (LE u32) */
  return (mode & JFS_S_IFMT) == JFS_S_IFDIR;
}

/* `[read-file PATH]` through the cap: whole contents as a string, or an error value if the
 * open fails (missing file — matches the VFS's missing-read -> error). */
JaclVal jacl_fscap_read_file(JaclVal path) {
  int h = jacl_fs();
  char p[1024];
  long plen = jacl_fs_path(path, p, sizeof p);
  if (plen < 0) return jaclrt_error();
  long fd = __vm_host_call(h, 0 /*open*/, jacl_fs_pti(p), plen, JFS_O_READ, 0);
  if (fd < 0) return jaclrt_error();
  long size = __vm_host_call(h, 3 /*seek*/, fd, JFS_SEEK_END, 0, 0);
  (void)__vm_host_call(h, 3 /*seek*/, fd, JFS_SEEK_SET, 0, 0);
  if (size < 0) { (void)__vm_host_call(h, 4 /*close*/, fd, 0, 0, 0); return jaclrt_error(); }
  /* Contents land in an untraced heap blob (raw bytes; the local keeps it alive — the
   * collector is non-moving, so the address is stable across the host calls). */
  JaclObj *blob = (JaclObj *)jacl_alloc(JOBJ_BLOB, (uint32_t)(size ? size : 1));
  char *buf = (char *)jacl_obj_payload(blob);
  long got = 0;
  while (got < size) {
    long n = __vm_host_call(h, 1 /*read*/, fd, jacl_fs_pti(buf + got), size - got, 0);
    if (n <= 0) break;
    got += n;
  }
  (void)__vm_host_call(h, 4 /*close*/, fd, 0, 0, 0);
  return jacl_str_new(buf, (uint32_t)got);
}

/* Shared open+write-all+close for write-file / append-file. Parent-dir gated; short writes
 * and open failures surface as error values. Returns nil on success (like the VFS path). */
static JaclVal jacl_fscap_spill(JaclVal content, JaclVal path, long flags) {
  int h = jacl_fs();
  char p[1024];
  long plen = jacl_fs_path(path, p, sizeof p);
  if (plen < 0) return jaclrt_error();
  if (!jacl_fs_parent_ok(h, p, plen)) return jaclrt_error();
  uint32_t clen = jacl_str_len(content);
  JaclObj *blob = (JaclObj *)jacl_alloc(JOBJ_BLOB, clen + 1);
  char *buf = (char *)jacl_obj_payload(blob);
  jacl_str_bytes(content, buf, clen + 1);
  long fd = __vm_host_call(h, 0 /*open*/, jacl_fs_pti(p), plen, flags, 0);
  if (fd < 0) return jaclrt_error();
  long sent = 0;
  while (sent < (long)clen) {
    long n = __vm_host_call(h, 2 /*write*/, fd, jacl_fs_pti(buf + sent), (long)clen - sent, 0);
    if (n <= 0) { (void)__vm_host_call(h, 4 /*close*/, fd, 0, 0, 0); return jaclrt_error(); }
    sent += n;
  }
  (void)__vm_host_call(h, 4 /*close*/, fd, 0, 0, 0);
  return jaclrt_nil();
}
JaclVal jacl_fscap_write_file(JaclVal content, JaclVal path) {
  return jacl_fscap_spill(content, path, JFS_O_WRITE | JFS_O_CREATE | JFS_O_TRUNC);
}
JaclVal jacl_fscap_append_file(JaclVal content, JaclVal path) {
  return jacl_fscap_spill(content, path, JFS_O_WRITE | JFS_O_APPEND | JFS_O_CREATE);
}
