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

/* `print V` — write V's text form + newline to stdout; returns nil (like the old VM). */
JaclVal jacl_print(JaclVal v) {
  char buf[64];
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
  } else if (t == 0x00) {                          /* nil */
    write(1, "nil\n", 4);
  } else if (t == 0x01) {                          /* bool */
    if (jaclrt_as_bool(v)) write(1, "true\n", 5);
    else write(1, "false\n", 6);
  } else {
    write(1, "?\n", 2);
  }
  return jaclrt_nil();
}
