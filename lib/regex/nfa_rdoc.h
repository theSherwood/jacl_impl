/*
 * nfa_rdoc.h — Rdoc input adapter for the NFA regex engine
 *
 * Provides nfa_input_from_rdoc(rdoc d) which yields only text content.
 * Block sentinels (BLOCK_OPEN/CLOSE) inject a synthetic newline as a chunk,
 * making ^ $ and . work at block boundaries. Inline sentinels and mark
 * changes are skipped transparently. Positions reported in the virtual
 * stream space (text bytes + synthetic newlines from block sentinels).
 *
 * Prerequisites: define NFA_MALLOC/NFA_FREE and STREE_ALLOCATOR (if needed)
 * before including this header. Include order: test_helpers.h (optional),
 * then nfa_rdoc.h (which pulls in nfa.h and rdoc.h).
 */
#ifndef NFA_RDOC_H
#define NFA_RDOC_H

#include "nfa.h"
#include "../sum_tree/rdoc.h"

/* Static newline byte for synthetic chunks */
static const uint8_t _nfa_rdoc_newline = '\n';

/* ── Rdoc adapter context ─────────────────────────────────────────── */

typedef struct {
    rdoc         d;            /* reference-counted rdoc (we hold a ref) */
    rdoc_cursor  cursor;       /* current traversal cursor */
    size_t       text_pos;     /* text bytes consumed so far */
    size_t       virt_pos;     /* virtual position (text + synthetic newlines) */
    size_t       chunk_start;  /* virt_pos at start of most recently returned chunk */
    size_t       token_skip;   /* bytes to skip at start of current text token */
} _nfa_rdoc_ctx;

static nfa_chunk _nfa_rdoc_next_chunk(void* ctx) {
    _nfa_rdoc_ctx* rc = (_nfa_rdoc_ctx*)ctx;

    for (;;) {
        if (!rc->cursor.valid || !rc->cursor.leaf) return (nfa_chunk){ NULL, 0 };
        if (rc->cursor.leaf_offset >= rc->cursor.leaf->count) return (nfa_chunk){ NULL, 0 };

        rdoc_token tok = rdoc_cursor_token(&rc->cursor);

        switch (tok.kind) {
        case RDOC_TOKEN_TEXT: {
            const uint8_t* data = tok.text_ptr + rc->token_skip;
            size_t len = tok.text_len - rc->token_skip;
            rc->chunk_start = rc->virt_pos;
            rc->token_skip = 0;
            rc->text_pos += len;
            rc->virt_pos += len;
            rdoc_cursor_advance(&rc->cursor);
            return (nfa_chunk){ data, len };
        }
        case RDOC_TOKEN_BLOCK_OPEN:
        case RDOC_TOKEN_BLOCK_CLOSE:
            rc->chunk_start = rc->virt_pos;
            rc->virt_pos += 1;
            rdoc_cursor_advance(&rc->cursor);
            return (nfa_chunk){ &_nfa_rdoc_newline, 1 };

        default:
            /* INLINE_OPEN, INLINE_CLOSE, MARK_CHANGE — skip */
            rdoc_cursor_advance(&rc->cursor);
            break;
        }
    }
}

static size_t _nfa_rdoc_byte_offset(void* ctx) {
    _nfa_rdoc_ctx* rc = (_nfa_rdoc_ctx*)ctx;
    return rc->chunk_start;
}

static void _nfa_rdoc_reset(void* ctx) {
    _nfa_rdoc_ctx* rc = (_nfa_rdoc_ctx*)ctx;
    rc->cursor = rdoc_cursor_new(rc->d, 0);
    rc->text_pos = 0;
    rc->virt_pos = 0;
    rc->chunk_start = 0;
    rc->token_skip = 0;
}

static void _nfa_rdoc_seek(void* ctx, size_t target_vpos) {
    _nfa_rdoc_ctx* rc = (_nfa_rdoc_ctx*)ctx;

    /* Reset to beginning and walk forward to target virtual position */
    rc->cursor = rdoc_cursor_new(rc->d, 0);
    rc->text_pos = 0;
    rc->virt_pos = 0;
    rc->token_skip = 0;

    while (rc->cursor.valid && rc->cursor.leaf &&
           rc->cursor.leaf_offset < rc->cursor.leaf->count &&
           rc->virt_pos < target_vpos) {

        rdoc_token tok = rdoc_cursor_token(&rc->cursor);

        switch (tok.kind) {
        case RDOC_TOKEN_TEXT: {
            size_t remaining = target_vpos - rc->virt_pos;
            if (remaining < tok.text_len) {
                /* Target is within this text token */
                rc->token_skip = remaining;
                rc->text_pos += remaining;
                rc->virt_pos += remaining;
                return;
            }
            rc->text_pos += tok.text_len;
            rc->virt_pos += tok.text_len;
            rdoc_cursor_advance(&rc->cursor);
            break;
        }
        case RDOC_TOKEN_BLOCK_OPEN:
        case RDOC_TOKEN_BLOCK_CLOSE:
            rc->virt_pos += 1;
            rdoc_cursor_advance(&rc->cursor);
            break;

        default:
            /* Inline sentinels / mark changes — skip */
            rdoc_cursor_advance(&rc->cursor);
            break;
        }
    }

    rc->chunk_start = rc->virt_pos;
}

/* ── Public API ─────────────────────────────────────────────────────── */

static void _nfa_rdoc_ctx_free(void* ctx) {
    _nfa_rdoc_ctx* rc = (_nfa_rdoc_ctx*)ctx;
    rdoc_unref(rc->d);
}

static nfa_input nfa_input_from_rdoc(rdoc d) {
    nfa_input inp = { NULL, NULL, NULL, NULL, NULL };
    _nfa_rdoc_ctx* ctx = (_nfa_rdoc_ctx*)NFA_MALLOC(sizeof(_nfa_rdoc_ctx));
    if (!ctx) return inp;

    ctx->d           = rdoc_ref(d);
    ctx->cursor      = rdoc_cursor_new(ctx->d, 0);
    ctx->text_pos    = 0;
    ctx->virt_pos    = 0;
    ctx->chunk_start = 0;
    ctx->token_skip  = 0;

    inp.next_chunk  = _nfa_rdoc_next_chunk;
    inp.byte_offset = _nfa_rdoc_byte_offset;
    inp.reset       = _nfa_rdoc_reset;
    inp.seek        = _nfa_rdoc_seek;
    inp.ctx         = ctx;
    return inp;
}

/*
 * Free an rdoc-backed nfa_input. Must be used instead of plain nfa_input_free
 * to properly unref the rdoc before freeing the context.
 */
static void nfa_input_rdoc_free(nfa_input* inp) {
    if (inp && inp->ctx) {
        _nfa_rdoc_ctx_free(inp->ctx);
        NFA_FREE(inp->ctx);
        inp->ctx = NULL;
    }
}

#endif /* NFA_RDOC_H */
