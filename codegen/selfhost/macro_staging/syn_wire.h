/* syn_wire — the staged-macro syntax-value ABI (compiler side).
 *
 * A macro re-hosted on SVM (docs/SVM_MACRO_STAGING_PLAN.md, option B) receives its
 * argument ASTs and returns its result AST across a host↔guest boundary. Rather than
 * share heap objects across two incompatible heaps (native pointers vs SVM window
 * offsets), we serialize an AST subtree to a flat, self-describing byte buffer and
 * reconstruct it on the other side. This is the compiler side: `AstNode ↔ bytes`.
 * (The macro side — jaclrt plain-data ↔ the same bytes — is a later brick.)
 *
 * Hygiene fields (scope_mark, is_caret, is_gensym) are preserved: they must survive
 * the round-trip, which a textual re-print could not carry.
 *
 * Corpus-scoped: covers the node kinds the macro-staging corpus exercises
 * (command / block / int|float|string literal / var-ref). Unsupported kinds make
 * `synw_encode` return NULL — grow the codec as the corpus grows.
 *
 * Callers include the JACL frontend (for AstNode / arena_t) before this header.
 */
#ifndef SYN_WIRE_H
#define SYN_WIRE_H

#include <stddef.h>

/* Encode `node` to a freshly malloc'd buffer; `*out_len` gets its length. Returns
 * NULL on an unsupported node kind or allocation failure. Free with `free`. */
unsigned char *synw_encode(AstNode *node, size_t *out_len);

/* Decode a buffer produced by `synw_encode` into an AstNode tree allocated in
 * `arena`. Returns NULL on a malformed/truncated buffer. */
AstNode *synw_decode(const unsigned char *buf, size_t len, arena_t *arena);

#endif /* SYN_WIRE_H */
