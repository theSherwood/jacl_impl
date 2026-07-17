/* extern_catalog.c — the test-only native `extern` catalog for the SVM parity harness.
 *
 * These are the C functions that `test/jacl/buf_extern_c.jacl` calls through `extern`
 * declarations. They are compiled to SVM IR on their own and linked as a third module
 * (after the runtime, before the program), so a JACL program's `call.import "t_sumi"`
 * resolves against them. The point is fidelity: each takes a *raw linear-memory address*
 * (the value a `[Buf N T]` / `[Ptr T]` decays to at a C-ABI boundary) and dereferences it
 * with genuine C pointer semantics — proving `[addr $buf]` is a real, stable pointer the
 * C side can read and write. The JACL heap *is* SVM linear memory and the collector is
 * non-moving, so the address stays valid across the call. See docs/SVM_BUFFERS.md.
 *
 * Names are <=7 bytes so they encode as inline strings (matches the old-VM harness). */
#include <stdint.h>

/* Sum `len` i32 elements starting at `addr`. Reads the caller's buffer through the
 * decayed pointer. */
int32_t t_sumi(int64_t addr, int32_t len) {
  const int32_t *p = (const int32_t *)(uintptr_t)addr;
  int64_t sum = 0;
  for (int32_t i = 0; i < len; i++) sum += p[i];
  return (int32_t)sum;
}

/* Fill `len` bytes at `addr` with `byte`. Writes through the decayed pointer; the caller
 * observes the mutation by reading the buffer back. Returns `len`. The `volatile` keeps
 * clang's loop-idiom pass from coalescing this into a variable-length `llvm.memset` (which
 * svm-llvm lowers to a synthesized `__svm_memset` helper that doesn't survive cross-module
 * linking) — a plain per-byte store loop is what we want here. */
int32_t t_fill(int64_t addr, int32_t len, int32_t byte) {
  volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;
  for (int32_t i = 0; i < len; i++) p[i] = (uint8_t)byte;
  return len;
}

/* dst[i] ^= src[i] for `len` bytes — two pointer args to verify both decay independently.
 * Returns `len`. */
int32_t t_xor(int64_t dst, int64_t src, int32_t len) {
  uint8_t *d = (uint8_t *)(uintptr_t)dst;
  const uint8_t *s = (const uint8_t *)(uintptr_t)src;
  for (int32_t i = 0; i < len; i++) d[i] ^= s[i];
  return len;
}
