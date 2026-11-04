/* syn_rt — the staged-macro syntax-value ABI (macro/runtime side).
 *
 * The mirror of `syn_wire` (the compiler side) for code running *inside* a staged
 * macro on the TEMEN runtime. It converts between the `syn_wire` byte format and the
 * **plain-data** representation of a syntax value (option B): a jaclrt **vector**
 *
 *     [ kind:i32, scope_mark:i32, flags:i32, pos:i32, ...payload ]
 *
 * `pos` is the position handle syn_wire gave the node (0: the macro made it); it is carried,
 * never interpreted. Payload by kind (kind = AstNodeType ordinal, kept in sync with
 * src/jacl.h):
 *   LIT_INT(1)/LIT_FLOAT(2):         [ .., value_or_bits ]
 *   LIT_STRING(3)/VAR_REF(4):        [ .., str ]
 *   COMMAND(0):                      [ .., head_id:i32, head:(syntax|nil), arg0, arg1, ... ]
 *   BLOCK(5):                        [ .., trailing_semi:i32, cmd0, cmd1, ... ]
 *   INTERP_STRING(6):                [ .., seg0, seg1, ... ]
 *   QUOTE(10)/SYNTAX_QUOTE(11)/UNQUOTE(12)/UNQUOTE_SPLICING(13)/SPREAD(19):  [ .., child ]
 *   BREAK(14)/RETURN(16):            [ .., value:(syntax|nil) ]
 *   CONTINUE(15):                    [ .. ]
 *   SHELL_CMD(20):                   [ .., head_id:i32, bits:i32 (background | stream << 1),
 *                                      head, arg0, arg1, ... ]
 *
 * The **wire format is identical** to syn_wire.c (version byte, LE u8/u32, string =
 * u32 len + bytes, optional node = presence u8 + node, node list = u32 count + nodes).
 * Keep the two in sync; the cross-codec round-trip test (run_rt_codec_test.sh) enforces
 * it byte-for-byte. codegen.c's compile_synquote builds the same vectors.
 *
 * Compiled to TEMEN as runtime code (via temen-llvm) for real staged macros, and built
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

/* A staged macro's result -> the result wire (malloc'd): the version byte, then status 0
 * and the expansion, or status 1 and a message — the error value's message, or why the
 * value is not syntax. NULL only on allocation failure. */
unsigned char *synrt_encode_result(JaclVal v, size_t *out_len);

/* The gensym var-ref `prefix__n` (prefix nil or "" → "g"; at most 64 bytes of it). */
JaclVal synrt_gensym_node(JaclVal prefix, uint32_t n);

#endif /* SYN_RT_H */
