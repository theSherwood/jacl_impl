/*
 * nfa_rope.h — Rope input adapter for the NFA regex engine
 *
 * Provides nfa_input_from_rope(rope r) which yields rope leaf data as
 * zero-copy chunks for the NFA simulator.
 *
 * Prerequisites: define NFA_MALLOC/NFA_FREE and STREE_ALLOCATOR (if needed)
 * before including this header. Include order: test_helpers.h (optional),
 * then nfa_rope.h (which pulls in nfa.h and rope.h).
 */
#ifndef NFA_ROPE_H
#define NFA_ROPE_H

#include "nfa.h"
#include "../sum_tree/rope.h"

/* ── Rope adapter context ───────────────────────────────────────────── */

typedef struct {
    rope         r;          /* reference-counted rope (we hold a ref) */
    rope_cursor  cursor;     /* current traversal cursor */
    size_t       chunk_start; /* byte offset of most recently returned chunk */
} _nfa_rope_ctx;

static nfa_chunk _nfa_rope_next_chunk(void* ctx) {
    _nfa_rope_ctx* rc = (_nfa_rope_ctx*)ctx;

    /* If cursor is invalid or has no leaf, EOF */
    if (!rc->cursor.valid || !rc->cursor.leaf)
        return (nfa_chunk){ NULL, 0 };

    /* If at end of current leaf, advance to next */
    if (rc->cursor.leaf_offset >= rc->cursor.leaf->count) {
        rc->cursor.prefix = rope_summary_combine(
            rc->cursor.prefix, rc->cursor.leaf->summary);
        if (!_rope_cursor_next_leaf(&rc->cursor))
            return (nfa_chunk){ NULL, 0 };
    }

    /* Record byte offset of this chunk's start */
    rc->chunk_start = rc->cursor.prefix.bytes + rc->cursor.leaf_offset;

    /* Yield remaining bytes in current leaf as a zero-copy chunk */
    const uint8_t* data = rc->cursor.leaf->elements + rc->cursor.leaf_offset;
    size_t len = rc->cursor.leaf->count - rc->cursor.leaf_offset;

    /* Advance past this leaf so next call moves to next leaf */
    rc->cursor.leaf_offset = rc->cursor.leaf->count;

    return (nfa_chunk){ data, len };
}

static size_t _nfa_rope_byte_offset(void* ctx) {
    _nfa_rope_ctx* rc = (_nfa_rope_ctx*)ctx;
    return rc->chunk_start;
}

static void _nfa_rope_reset(void* ctx) {
    _nfa_rope_ctx* rc = (_nfa_rope_ctx*)ctx;
    rc->cursor = rope_cursor_new(rc->r, 0);
    rc->chunk_start = 0;
}

static void _nfa_rope_seek(void* ctx, size_t offset) {
    _nfa_rope_ctx* rc = (_nfa_rope_ctx*)ctx;
    rc->cursor = rope_cursor_new(rc->r, offset);
    rc->chunk_start = offset;
}

/* ── Public API ─────────────────────────────────────────────────────── */

/*
 * Create an nfa_input backed by a rope. The adapter takes a reference
 * to the rope (via rope_ref) which is released by nfa_input_free.
 *
 * Note: nfa_input_free (from nfa.h) calls NFA_FREE on inp->ctx, so the
 * rope_ctx is allocated with NFA_MALLOC. We also need to unref the rope
 * inside the ctx before that free. To handle this, we provide a custom
 * free wrapper.
 */

static void _nfa_rope_ctx_free(void* ctx) {
    _nfa_rope_ctx* rc = (_nfa_rope_ctx*)ctx;
    rope_unref(rc->r);
}

static nfa_input nfa_input_from_rope(rope r) {
    nfa_input inp = { NULL, NULL, NULL, NULL, NULL };
    _nfa_rope_ctx* ctx = (_nfa_rope_ctx*)NFA_MALLOC(sizeof(_nfa_rope_ctx));
    if (!ctx) return inp;

    ctx->r           = rope_ref(r);
    ctx->cursor      = rope_cursor_new(ctx->r, 0);
    ctx->chunk_start = 0;

    inp.next_chunk  = _nfa_rope_next_chunk;
    inp.byte_offset = _nfa_rope_byte_offset;
    inp.reset       = _nfa_rope_reset;
    inp.seek        = _nfa_rope_seek;
    inp.ctx         = ctx;
    return inp;
}

/*
 * Free a rope-backed nfa_input. Must be used instead of plain nfa_input_free
 * to properly unref the rope before freeing the context.
 */
static void nfa_input_rope_free(nfa_input* inp) {
    if (inp && inp->ctx) {
        _nfa_rope_ctx_free(inp->ctx);
        NFA_FREE(inp->ctx);
        inp->ctx = NULL;
    }
}

#endif /* NFA_ROPE_H */
