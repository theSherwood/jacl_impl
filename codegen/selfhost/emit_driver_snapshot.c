/* JACL compiler-guest — warm-snapshot two-phase driver (docs/SVM_WARM_COMPILER.md Slice 2/3).
 *
 * The single-shot `emit_driver.c` rebuilds the compiler's program-independent state (the prelude
 * macros, parsed+compiled once into statics) on every fresh guest Run — a fixed floor that dominates
 * a small compile. This driver splits that into the warm-snapshot shape the on-ramp reactor expects
 * (the `qjs_snapshot.c` twin), so the host runs `warmup` once, snapshots the post-init window, and
 * restores it before each `eval_run` — paying the prelude init once instead of per compile:
 *
 *   - `main`     — the one-shot cold path (read stdin -> compile -> print). The BASELINE.
 *   - `warmup`   — trigger the program-independent init (prelude/statics) via jacl_emit_warmup; no
 *                  stdin, no user compile. The host snapshots the window after this returns.
 *   - `eval_run` — read stdin (the user's JACL) and compile it over the warm, restored statics.
 *
 * Fresh-per-Run isolation holds: each Run restores the SAME post-warmup snapshot, and each compile
 * runs on a fresh local arena (jacl_emit_ir), so no user program's state leaks into the next. The
 * guest must be non-`vm_map`-growing (Slice 1's fixed-arena malloc) for the window to be
 * snapshot-restorable (temen#816). Built like emit_driver.c; the three exports are ordinary
 * `(i64 sp)` on-ramp entries the reactor drives. */
#include <stdio.h>
#include <stdlib.h>

extern long read(int fd, void *buf, long n);            /* on-ramp Stream.read */
extern char *jacl_emit_ir(const char *source);          /* cold compile (emit_jacl.c) */
extern void jacl_emit_warmup(void);                     /* trigger the prelude/statics init */
extern char *jacl_emit_compile(const char *source);     /* compile over the warm statics */

/* Install the in-guest macro-staging hook when this build links it (weak; safe if absent). */
extern void jacl_install_svm_stage_hook(void) __attribute__((weak));

/* Read all of stdin into a growable buffer (NUL-terminated); returns the buffer (caller frees). */
static char *read_stdin(void) {
  size_t cap = 1u << 20, len = 0;
  char *src = (char *)malloc(cap);
  if (!src) return NULL;
  for (;;) {
    if (len + 4096 > cap) {
      cap *= 2;
      char *n = (char *)realloc(src, cap);
      if (!n) { free(src); return NULL; }
      src = n;
    }
    long r = read(0, src + len, (long)(cap - 1 - len));
    if (r <= 0) break;
    len += (size_t)r;
  }
  src[len] = 0;
  return src;
}

static void emit_and_print(char *(*compile)(const char *), const char *src) {
  char *ir = compile(src);
  fputs(ir ? ir : "%%ERROR%%\n(codegen returned null)\n", stdout);
  fflush(stdout);
}

/* BASELINE (cold): the original one-shot flow — the emit_driver.c path. */
int main(void) {
  if (jacl_install_svm_stage_hook) jacl_install_svm_stage_hook();
  char *src = read_stdin();
  if (!src) return 1;
  emit_and_print(jacl_emit_ir, src);
  free(src);
  return 0;
}

/* WARM PHASE 1: init the program-independent state (prelude macros) into statics, then return. No
 * stdin, no user compile — the produced window is program-independent. The host snapshots here. */
int warmup(void) {
  if (jacl_install_svm_stage_hook) jacl_install_svm_stage_hook();
  jacl_emit_warmup();
  return 0;
}

/* WARM PHASE 2: read the user's JACL from stdin and compile it over the warm, restored statics.
 * Runs per Run over a window the host restored from the warmup snapshot — no prelude rebuild. */
int eval_run(void) {
  char *src = read_stdin();
  if (!src) return 1;
  emit_and_print(jacl_emit_compile, src);
  free(src);
  return 0;
}
