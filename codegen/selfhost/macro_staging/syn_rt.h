/* syn_rt — the staged-macro syntax-value ABI (macro/runtime side).
 *
 * The mirror of `syn_wire` (the compiler side) for code running *inside* a staged
 * macro on the SVM runtime. It converts between the `syn_wire` byte format and the
 * **plain-data** representation of a syntax value (option B): a jaclrt **vector**
 *
 *     [ kind:i32, scope_mark:i32, flags:i32, ...payload ]
 *
 * with payload by kind (kind = AstNodeType ordinal, kept in sync with src/jacl.h):
 *   LIT_INT(1)/LIT_FLOAT(2):  [ .., value_or_bits:i32 ]
 *   LIT_STRING(3)/VAR_REF(4): [ .., str ]
 *   COMMAND(0):               [ .., head_id:i32, head:(syntax|nil), arg0, arg1, ... ]
 *   BLOCK(5):                 [ .., trailing_semi:i32, cmd0, cmd1, ... ]
 *
 * The **wire format is identical** to syn_wire.c (version byte, LE u8/u32, string =
 * u32 len + bytes, command head = presence u8 + node). Keep the two in sync; the
 * cross-codec round-trip test (run_rt_codec_test.sh) enforces it byte-for-byte.
 *
 * Compiled to SVM as runtime code (via svm-llvm) for real staged macros, and built
 * natively (with an __vm_* shim) for the unit test.
 */
#ifndef SYN_RT_H
#define SYN_RT_H

#include "jaclrt.h"
#include <stddef.h>

/* syn_wire bytes -> a plain-data syntax value (jaclrt vector tree). JACL_NIL on a
 * malformed/truncated buffer. */
JaclVal synrt_decode(const unsigned char *buf, size_t len);

/* The wire-format version byte (the leading byte of a syn_wire buffer). */
int synrt_wire_version(void);

/* Decode ONE node from `buf` starting at *pos (no version byte); advances *pos. For
 * reading a sequence of N syntax values (multi-arity macro args). JACL_NIL on error. */
JaclVal synrt_decode_node(const unsigned char *buf, size_t len, size_t *pos);

/* A plain-data syntax value -> syn_wire bytes (malloc'd; free with `free`). NULL on
 * error. */
unsigned char *synrt_encode(JaclVal syn, size_t *out_len);

#endif /* SYN_RT_H */
