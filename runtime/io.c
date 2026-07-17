/* io.c — host I/O builtins via the svm powerbox (P3.5).
 *
 * `print` writes a value's text representation + a newline to stdout. svm-llvm lowers the
 * libc `write` to a `Stream.write` cap.call on the stashed stdout handle, and (because the
 * program uses a powerbox import + defines `main`) synthesizes the `_start` powerbox entry
 * that stashes the granted handles. So a JACL program that prints is a powerbox program:
 * the host grants stdout and observes the bytes. Matches the old VM's `print` (value text
 * + trailing newline).
 */
#include "jaclrt.h"
#include <stdint.h>

/* svm-llvm libc shim: lowers to Stream.write on the stashed stdout handle. */
int write(int fd, const char *buf, long n);

/* The value's 5-bit type index behind an OPAQUE boundary. Without this, clang reassociates
 * a cascade of `jaclrt_type_index(v) == K` tests into a single `switch (v & TYPE_MASK)`
 * whose cases are `K << 56` — a *sparse* switch (span ~2^57) svm-llvm rejects. Taking the
 * tag through a noinline call forces the dispatch onto the small index (0..31). */
__attribute__((noinline)) static uint32_t jacl_tag_of(JaclVal v) { return jaclrt_type_index(v); }

/* i64 → decimal into buf (no NUL); returns the byte length. */
static int jacl_itoa(long v, char *buf) {
  char tmp[24];
  int n = 0, len = 0, neg = (v < 0);
  unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1u : (unsigned long)v;
  do { tmp[n++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u);
  if (neg) buf[len++] = '-';
  for (int i = n - 1; i >= 0; i--) buf[len++] = tmp[i];
  return len;
}

/* ---- in-memory virtual filesystem ----
 * The svm powerbox exposes only stdout/stdin/exit — there is no host filesystem capability
 * (its sandbox deliberately excludes arbitrary FS). So file I/O is modelled guest-side: a
 * runtime map keyed by absolute path holds each file's contents. This reproduces the corpus's
 * observable behaviour (write→read round-trips, a missing read → error, a write into a
 * nonexistent directory → error) without touching the real host FS, and is identical on the
 * interpreter and the JIT (the map lives in guest memory, fresh per run). The global holds a
 * heap value, so it is a GC root (jacl_vfs_mark_root, from jacl_sched_mark_roots). */
JaclVal jacl_vfs;   /* path(string) -> content(string); nil == empty */
static JaclVal jacl_vfs_map(void) {
  if (jaclrt_is_nil(jacl_vfs)) jacl_vfs = jacl_map_empty();
  return jacl_vfs;
}

/* A write succeeds only if the path's parent directory "exists": the model knows just the
 * root and /tmp (where the corpus writes), so a path like /tmp/foo.txt is writable but
 * /tmp/no_such_dir/out.txt is not — mirroring open()'s ENOENT on a missing directory. */
static int jacl_vfs_parent_ok(JaclVal path) {
  char p[1024];
  uint32_t len = jacl_str_len(path);
  if (len >= sizeof p) return 0;
  jacl_str_bytes(path, p, sizeof p);
  p[len] = 0;
  int slash = -1;
  for (uint32_t i = 0; i < len; i++) if (p[i] == '/') slash = (int)i;
  if (slash < 0) return 0;                 /* no directory component */
  if (slash == 0) return 1;                /* parent is "/" */
  return slash == 4 && p[0] == '/' && p[1] == 't' && p[2] == 'm' && p[3] == 'p';  /* "/tmp" */
}

/* `[read-file PATH]` — the file's contents, or an error value on a missing file. With an
 * embedder-granted "fs" capability the read goes through real cap ops (fscap.c); without
 * one the pure-guest VFS map serves — same observable semantics either way. */
JaclVal jacl_read_file(JaclVal path) {
  if (jaclrt_is_error(path)) return path;
  if (!jaclrt_is_string(path)) return jaclrt_error();
  if (jacl_fs_granted()) return jacl_fscap_read_file(path);
  JaclVal v = jacl_map_get(jacl_vfs_map(), path);
  if (jaclrt_is_nil(v)) return jaclrt_error();   /* no such file → error (try/catch-able) */
  return v;
}

/* `[write-file CONTENT PATH]` — create/truncate; error if the parent directory is unknown. */
JaclVal jacl_write_file(JaclVal content, JaclVal path) {
  if (jaclrt_is_error(content)) return content;
  if (jaclrt_is_error(path)) return path;
  if (!jaclrt_is_string(path) || !jaclrt_is_string(content)) return jaclrt_error();
  if (jacl_fs_granted()) return jacl_fscap_write_file(content, path);
  if (!jacl_vfs_parent_ok(path)) return jaclrt_error();
  jacl_vfs = jacl_map_set(jacl_vfs_map(), path, content);
  return jaclrt_nil();
}

/* `[append-file CONTENT PATH]` — extend an existing file, or create it like write-file. */
JaclVal jacl_append_file(JaclVal content, JaclVal path) {
  if (jaclrt_is_error(content)) return content;
  if (jaclrt_is_error(path)) return path;
  if (!jaclrt_is_string(path) || !jaclrt_is_string(content)) return jaclrt_error();
  if (jacl_fs_granted()) return jacl_fscap_append_file(content, path);
  JaclVal cur = jacl_map_get(jacl_vfs_map(), path);
  if (jaclrt_is_nil(cur)) {                    /* new file: same rule as write-file */
    if (!jacl_vfs_parent_ok(path)) return jaclrt_error();
    jacl_vfs = jacl_map_set(jacl_vfs_map(), path, content);
  } else {
    jacl_vfs = jacl_map_set(jacl_vfs_map(), path, jacl_str_concat(cur, content));
  }
  return jaclrt_nil();
}

/* Is PATH one of the guest VFS's model directories ("/" and "/tmp" exist; nothing else)? */
static int jacl_vfs_is_dir(JaclVal path) {
  char p[16];
  uint32_t len = jacl_str_len(path);
  if (len >= sizeof p) return 0;
  jacl_str_bytes(path, p, sizeof p);
  if (len == 1 && p[0] == '/') return 1;
  return len == 4 && memcmp(p, "/tmp", 4) == 0;
}

/* `[delete-file PATH]` — remove; error value on a missing file (unlink's ENOENT). */
JaclVal jacl_delete_file(JaclVal path) {
  if (jaclrt_is_error(path)) return path;
  if (!jaclrt_is_string(path)) return jaclrt_error();
  if (jacl_fs_granted()) return jacl_fscap_delete_file(path);
  JaclVal v = jacl_map_get(jacl_vfs_map(), path);
  if (jaclrt_is_nil(v)) return jaclrt_error();
  jacl_vfs = jacl_map_remove(jacl_vfs_map(), path);
  return jaclrt_nil();
}

/* `[file-exists? PATH]` — a file or directory at PATH (bool; never errors on missing). */
JaclVal jacl_file_exists(JaclVal path) {
  if (jaclrt_is_error(path)) return path;
  if (!jaclrt_is_string(path)) return jaclrt_error();
  if (jacl_fs_granted()) return jacl_fscap_file_exists(path);
  if (!jaclrt_is_nil(jacl_map_get(jacl_vfs_map(), path))) return jaclrt_bool(1);
  return jaclrt_bool(jacl_vfs_is_dir(path));
}

/* `[list-dir PATH]` — sorted vector of PATH's immediate entry names (no "."/".."), or an
 * error value on a missing directory. On the guest VFS the children are the map keys one
 * level under PATH (deduped through their first '/'); the model dirs are "/" and "/tmp". */
JaclVal jacl_list_dir(JaclVal path) {
  if (jaclrt_is_error(path)) return path;
  if (!jaclrt_is_string(path)) return jaclrt_error();
  if (jacl_fs_granted()) return jacl_fscap_list_dir(path);
  if (!jacl_vfs_is_dir(path)) return jaclrt_error();
  char prefix[8];
  uint32_t plen = jacl_str_len(path);
  jacl_str_bytes(path, prefix, sizeof prefix);
  uint32_t pl = plen;                       /* bytes before a child name starts */
  if (!(plen == 1 && prefix[0] == '/')) prefix[pl++] = '/';   /* "/tmp" -> "/tmp/" */
  JaclVal keys = jacl_map_keys_v(jacl_vfs_map());
  JaclVal arr = jacl_arr_new();
  int32_t nk = jaclrt_as_i32(jacl_len(keys));
  for (int32_t i = 0; i < nk; i++) {
    JaclVal k = jacl_vec_get(keys, (uint32_t)i);
    char kb[1024];
    uint32_t klen = jacl_str_len(k);
    if (klen <= pl || klen >= sizeof kb) continue;
    jacl_str_bytes(k, kb, sizeof kb);
    if (memcmp(kb, prefix, pl) != 0) continue;
    uint32_t end = pl;                      /* the child name: up to the next '/' (a subdir) */
    while (end < klen && kb[end] != '/') end++;
    JaclVal name = jacl_str_new(kb + pl, end - pl);
    int dup = 0;                            /* dedupe (two files under one subdir) */
    int32_t na = jaclrt_as_i32(jacl_len(arr));
    for (int32_t j = 0; j < na && !dup; j++)
      dup = jacl_str_eq(jacl_arr_get_at(arr, jaclrt_i32(j)), name);
    if (!dup) (void)jacl_arr_push(arr, name);
  }
  return jacl_names_sorted_vec(arr);
}

/* `print V` — write V's text form + newline to stdout; returns nil (like the old VM). */
JaclVal jacl_print(JaclVal v) {
  char buf[64];
  /* Error-flagged values print as `<error: PAYLOAD>` (payload rendered by the shared
   * repr, flag cleared) — matches the old VM's error display. Checked before the tag
   * dispatch since the flag is orthogonal to the value's type index. */
  if (jaclrt_is_error(v)) {
    JaclVal payload = jacl_error_val(v);
    JaclVal s = jacl_to_string(payload);
    char sb[1024];
    uint32_t len = jaclrt_is_string(s) ? jacl_str_len(s) : 0;
    if (len > sizeof sb - 1) len = sizeof sb - 1;
    if (len) jacl_str_bytes(s, sb, sizeof sb);
    write(1, "<error: ", 8);
    write(1, sb, (long)len);
    write(1, ">\n", 2);
    return jaclrt_nil();
  }
  uint32_t t = jacl_tag_of(v);
  if (t == 0x04 || t == 0x05 || t == 0x14) {     /* inline / heap / rope string */
    uint32_t len = jacl_str_len(v);
    char sb[1024];
    if (len > sizeof sb - 1) len = sizeof sb - 1;
    jacl_str_bytes(v, sb, sizeof sb);
    write(1, sb, (long)len);
    write(1, "\n", 1);
  } else if (t == 0x02) {                          /* i32 */
    int len = jacl_itoa((long)jaclrt_as_i32(v), buf);
    buf[len++] = '\n';
    write(1, buf, len);
  } else if (t == 0x03) {                          /* f32: shared repr via to_string */
    JaclVal s = jacl_to_string(v);
    uint32_t len = jacl_str_len(s);
    char fb[64];
    if (len > sizeof fb - 1) len = sizeof fb - 1;
    jacl_str_bytes(v ? s : s, fb, sizeof fb);
    write(1, fb, (long)len);
    write(1, "\n", 1);
  } else if (t == 0x00) {                          /* nil */
    write(1, "nil\n", 4);
  } else if (t == 0x01) {                          /* bool */
    if (jaclrt_as_bool(v)) write(1, "true\n", 5);
    else write(1, "false\n", 6);
  } else if (t == 0x06 || t == 0x07 || t == 0x12 || t == 0x1A || t == 0x1B || t == 0x1C || t == 0x1E ||
             t == 0x08 || t == 0x0E || t == 0x0F || t == 0x10 ||
             t == 0x0B || t == 0x0C) {   /* collections (incl. typed vec/ptr/flat buf) + closure + wide + box/atom */
    JaclVal s = jacl_to_string(v);
    uint32_t len = jacl_str_len(s);
    char sb[2048];
    if (len > sizeof sb - 1) len = sizeof sb - 1;
    jacl_str_bytes(s, sb, sizeof sb);
    write(1, sb, (long)len);
    write(1, "\n", 1);
  } else {
    write(1, "?\n", 2);
  }
  return jaclrt_nil();
}
