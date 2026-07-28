/* JACL compiler-guest entry point.
 *
 * Reads a JACL program from **stdin** and writes the emitted SVM-IR text to
 * **stdout** — the shape the SVM on-ramp powerbox expects (stdin/stdout bound by
 * name). Compiled to SVM IR (via the vendor/svm LLVM on-ramp) it becomes
 * `jacl_compiler.svmb`: the JACL compiler running as an SVM guest, so the browser
 * playground can compile edited source with no Emscripten frontend. See
 * `docs/SVM_SELFHOST_FEASIBILITY.md` and this dir's README.
 *
 * `read(0, …)` is the on-ramp Stream.read capability — the same way the svm Tcl/Lua
 * REPL guests read stdin. The emitted IR (or a `%%ERROR%%\n<diagnostic>` on a
 * compile error) is handed back verbatim; the host then links it against
 * `jaclrt.svm` and runs it via `svm_link_run`. */
#include <stdio.h>
#include <stdlib.h>

extern long read(int fd, void *buf, long n);       /* on-ramp Stream.read */
extern char *jacl_emit_ir(const char *source);     /* the browser frontend entry (emit_jacl.c) */

int main(void) {
  size_t cap = 1u << 20, len = 0;                  /* grows as needed */
  char *src = (char *)malloc(cap);
  if (!src) return 1;
  for (;;) {
    if (len + 4096 > cap) {
      cap *= 2;
      char *n = (char *)realloc(src, cap);
      if (!n) { free(src); return 1; }
      src = n;
    }
    long r = read(0, src + len, (long)(cap - 1 - len));
    if (r <= 0) break;
    len += (size_t)r;
  }
  src[len] = 0;

  char *ir = jacl_emit_ir(src);
  fputs(ir ? ir : "%%ERROR%%\n(codegen returned null)\n", stdout);
  fflush(stdout);
  return 0;
}
