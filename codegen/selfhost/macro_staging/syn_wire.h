/* syn_wire — the staged-macro syntax-value ABI (compiler side).
 *
 * A macro re-hosted on TEMEN (docs/TEMEN_MACRO_STAGING_PLAN.md, option B) receives its
 * argument ASTs and returns its result AST across a host↔guest boundary. Rather than
 * share heap objects across two incompatible heaps (native pointers vs TEMEN window
 * offsets), we serialize an AST subtree to a flat, self-describing byte buffer and
 * reconstruct it on the other side. This is the compiler side: `AstNode ↔ bytes`; the
 * macro side (jaclrt plain-data ↔ the same bytes) is syn_rt.
 *
 * Hygiene fields (scope_mark, is_caret, is_gensym) are preserved: they must survive
 * the round-trip, which a textual re-print could not carry.
 *
 * Every node kind that can appear inside an expression crosses: commands, blocks,
 * literals, var-refs, interpolated strings, quote/syntax-quote/unquote forms, spreads,
 * break/continue/return and shell commands. The top-level declarations (use, struct,
 * defmacro, ctx) and a literal wider than 64 bits do not; encoding one fails.
 *
 * Positions do not travel as line/column numbers. Each node the compiler writes gets a
 * handle (1, 2, …) into a table kept in the `SynwCall` for that expansion, and the macro
 * carries handles through untouched. Handle 0 marks a node the macro itself made — a
 * template node, a gensym — and decoding gives such a node the macro call's position.
 *
 * Callers include the JACL frontend (for AstNode / MacroEntry / arena_t) before this
 * header.
 */
#ifndef SYN_WIRE_H
#define SYN_WIRE_H

#include <stddef.h>

/* One macro expansion's side of the wire. */
typedef struct {
  SourcePos *starts, *ends;        /* the positions of the nodes written: handle k is index k-1 */
  uint32_t   count, cap;
  SourcePos  call_start, call_end; /* the position a node the macro made (handle 0) takes */
  uint32_t   expansion;            /* this expansion's number; a gensym it mints is named for it */
  AstNode   *bad;                  /* encoding: the node that cannot cross, and why not */
  const char *why;
} SynwCall;

/* Start an expansion of `call` (an AST_COMMAND naming the macro). Numbers the expansion
 * uniquely in this process, so gensyms from two expansions never share a name. */
void synw_call_begin(SynwCall *c, AstNode *call);
void synw_call_end(SynwCall *c);

/* The macro error for an argument wire that could not be built, written into `buf`. */
const char *synw_call_refusal(const SynwCall *c, char *buf, size_t cap);

/* Encode `node` to a freshly malloc'd buffer (version byte, then the node); `*out_len`
 * gets its length. With `c`, each node's position is recorded and its handle written;
 * without, every handle is 0. NULL on a node that cannot cross (see `c->bad`) or on
 * allocation failure. Free with `free`. */
unsigned char *synw_encode(AstNode *node, size_t *out_len, SynwCall *c);

/* The argument wire for calling `entry` at `call`: the version byte; the call's line and
 * column; a u32 count of position handles and each one's line and column (so the macro can
 * answer `syntax-pos`); one node per fixed parameter; then — variadic only — a u32 count and
 * that many rest arguments. The caller has checked the arity. NULL as for `synw_encode`. */
unsigned char *synw_encode_args(MacroEntry *entry, AstNode *call, size_t *out_len, SynwCall *c);

/* Decode a buffer produced by `synw_encode` into an AstNode tree allocated in `arena`.
 * With `c`, positions are restored from its table (handle 0: the call's position) and a
 * gensym minted in this run is renamed for the expansion. NULL on a malformed buffer. */
AstNode *synw_decode(const unsigned char *buf, size_t len, arena_t *arena, SynwCall *c);

/* Decode a staged macro's result wire: the version byte, a status byte, then the
 * expansion (status 0) or an error message and the handle of the node it is about
 * (status 1). Returns the expansion, or NULL with `*err` set to the macro's message or to
 * why the wire was unreadable; for the macro's own error, `*err_at` (if given) is where it
 * points — the node's position, or the call's — and otherwise has line 0. A command the
 * macro built with `make-syntax` gets its head id here. */
AstNode *synw_decode_result(const unsigned char *buf, size_t len, arena_t *arena, SynwCall *c,
                            const char **err, SourcePos *err_at);

#endif /* SYN_WIRE_H */
