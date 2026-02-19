/*
 * nfa.h — Thompson NFA regex engine
 *
 * Header-only: AST types, recursive descent parser, NFA compiler, and (future) simulator.
 * Define NFA_MALLOC/NFA_FREE before including to use custom allocators.
 */
#ifndef NFA_H
#define NFA_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifndef NFA_MALLOC
#include <stdlib.h>
#define NFA_MALLOC(sz) malloc(sz)
#define NFA_FREE(p)    free(p)
#endif

#include "../arena/arena.h" /* arena_t forward declaration */

/* ── Error codes ─────────────────────────────────────────────────────── */

#define NFA_OK                     0
#define NFA_ERR_UNMATCHED_LPAREN   1
#define NFA_ERR_UNMATCHED_RPAREN   2
#define NFA_ERR_TRAILING_BACKSLASH 3
#define NFA_ERR_INVALID_ESCAPE     4
#define NFA_ERR_UNMATCHED_LBRACKET 5
#define NFA_ERR_ALLOC              6
#define NFA_ERR_INVALID_GROUP      7
#define NFA_ERR_REPEAT_RANGE       8

/* ── Regex flags ─────────────────────────────────────────────────────── */

#define NFA_FLAG_DOT_ALL   0x01  /* s: dot matches \n */
#define NFA_FLAG_MULTILINE 0x02  /* m: ^ and $ are line-based (already default) */
#define NFA_FLAG_ICASE     0x04  /* i: case-insensitive (reserved for US-006) */

/* ── AST node types ──────────────────────────────────────────────────── */

typedef enum {
    NFA_AST_LITERAL,
    NFA_AST_DOT,
    NFA_AST_ANCHOR,
    NFA_AST_CHAR_CLASS,
    NFA_AST_CONCAT,
    NFA_AST_ALT,
    NFA_AST_REPEAT,
    NFA_AST_GROUP,
} nfa_ast_type;

typedef enum {
    NFA_ANCHOR_BOL,
    NFA_ANCHOR_EOL,
    NFA_ANCHOR_WORD,
    NFA_ANCHOR_NON_WORD,
} nfa_anchor_type;

typedef struct {
    uint8_t lo;
    uint8_t hi;
} nfa_range;

typedef struct nfa_ast nfa_ast;
struct nfa_ast {
    nfa_ast_type type;
    union {
        struct { uint8_t* bytes; size_t len; bool icase; }          literal;
        struct { bool dot_all; }                                  dot;
        nfa_anchor_type                                           anchor;
        struct { nfa_range* ranges; size_t count; bool negated; bool icase; } char_class;
        struct { nfa_ast** children; size_t count; }              concat;
        struct { nfa_ast** children; size_t count; }              alt;
        struct { nfa_ast* child; int min; int max; bool greedy; } repeat;
        struct { nfa_ast* child; int index; }                     group;
    };
};

/* ── Public API ──────────────────────────────────────────────────────── */

static int  nfa_parse(const uint8_t* pattern, size_t len, nfa_ast** out_ast);
static void nfa_ast_free(nfa_ast* ast);

/* Forward declarations for compiler (defined after parser) */
typedef struct nfa_program nfa_program;
static int  nfa_compile(nfa_ast* ast, nfa_program** out_prog);
static int  nfa_compile_pattern(const uint8_t* pattern, size_t len, nfa_program** out_prog);
static void nfa_program_free(nfa_program* prog);

/* Forward declarations for input abstraction (defined after compiler) */
typedef struct nfa_input nfa_input;
static nfa_input nfa_input_from_buf(const uint8_t* data, size_t len);
static void nfa_input_free(nfa_input* inp);

/* ── NFA match result ───────────────────────────────────────────────── */

typedef struct {
    bool    matched;
    size_t  start;      /* byte offset of match start */
    size_t  end;        /* byte offset of match end (exclusive) */
    size_t* captures;   /* capture slot array (NFA_MALLOC'd), or NULL */
    size_t  n_captures; /* number of capture slots (2 * group_count) */
} nfa_match;

static void nfa_match_free(nfa_match* m) {
    if (m && m->captures) {
        NFA_FREE(m->captures);
        m->captures = NULL;
    }
}

/* Forward declaration for simulator (defined after input abstraction) */
static nfa_match nfa_exec(nfa_program* prog, nfa_input* input);

/* Forward declarations for match iterator (defined after simulator) */
/* nfa_iter, nfa_iter_new, nfa_iter_next, nfa_iter_free */

/* ── Arena allocation helpers ────────────────────────────────────────── */

#define ARENA_IMPLEMENTATION
#include "../arena/arena.h"

static void* _nfa_tmp_alloc(arena_t* a, size_t sz) {
    return a ? arena_alloc(a, sz) : NFA_MALLOC(sz);
}
static void _nfa_tmp_free(arena_t* a, void* p) {
    if (!a) NFA_FREE(p);
}

/* ── AST construction helpers ────────────────────────────────────────── */

static nfa_ast* _nfa_ast_new(nfa_ast_type type, arena_t* a) {
    nfa_ast* n = (nfa_ast*)_nfa_tmp_alloc(a, sizeof(nfa_ast));
    if (!n) return NULL;
    memset(n, 0, sizeof(nfa_ast));
    n->type = type;
    return n;
}

static nfa_ast* nfa_ast_literal(const uint8_t* bytes, size_t len, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_LITERAL, a);
    if (!n) return NULL;
    n->literal.bytes = (uint8_t*)_nfa_tmp_alloc(a, len);
    if (!n->literal.bytes) { _nfa_tmp_free(a, n); return NULL; }
    memcpy(n->literal.bytes, bytes, len);
    n->literal.len = len;
    return n;
}

static nfa_ast* nfa_ast_dot(bool dot_all, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_DOT, a);
    if (n) n->dot.dot_all = dot_all;
    return n;
}

static nfa_ast* nfa_ast_anchor(nfa_anchor_type atype, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_ANCHOR, a);
    if (!n) return NULL;
    n->anchor = atype;
    return n;
}

static nfa_ast* nfa_ast_char_class(nfa_range* ranges, size_t count, bool negated, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_CHAR_CLASS, a);
    if (!n) return NULL;
    if (count > 0) {
        n->char_class.ranges = (nfa_range*)_nfa_tmp_alloc(a, count * sizeof(nfa_range));
        if (!n->char_class.ranges) { _nfa_tmp_free(a, n); return NULL; }
        memcpy(n->char_class.ranges, ranges, count * sizeof(nfa_range));
    }
    n->char_class.count   = count;
    n->char_class.negated = negated;
    return n;
}

static nfa_ast* nfa_ast_concat(nfa_ast** children, size_t count, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_CONCAT, a);
    if (!n) return NULL;
    if (count > 0) {
        n->concat.children = (nfa_ast**)_nfa_tmp_alloc(a, count * sizeof(nfa_ast*));
        if (!n->concat.children) { _nfa_tmp_free(a, n); return NULL; }
        memcpy(n->concat.children, children, count * sizeof(nfa_ast*));
    }
    n->concat.count = count;
    return n;
}

static nfa_ast* nfa_ast_alt(nfa_ast** children, size_t count, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_ALT, a);
    if (!n) return NULL;
    if (count > 0) {
        n->alt.children = (nfa_ast**)_nfa_tmp_alloc(a, count * sizeof(nfa_ast*));
        if (!n->alt.children) { _nfa_tmp_free(a, n); return NULL; }
        memcpy(n->alt.children, children, count * sizeof(nfa_ast*));
    }
    n->alt.count = count;
    return n;
}

static nfa_ast* nfa_ast_repeat(nfa_ast* child, int min, int max, bool greedy, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_REPEAT, a);
    if (!n) return NULL;
    n->repeat.child  = child;
    n->repeat.min    = min;
    n->repeat.max    = max;
    n->repeat.greedy = greedy;
    return n;
}

static nfa_ast* nfa_ast_group(nfa_ast* child, int index, arena_t* a) {
    nfa_ast* n = _nfa_ast_new(NFA_AST_GROUP, a);
    if (!n) return NULL;
    n->group.child = child;
    n->group.index = index;
    return n;
}

/* ── AST free ────────────────────────────────────────────────────────── */

static void nfa_ast_free(nfa_ast* ast) {
    if (!ast) return;
    switch (ast->type) {
    case NFA_AST_LITERAL:
        NFA_FREE(ast->literal.bytes);
        break;
    case NFA_AST_DOT:
    case NFA_AST_ANCHOR:
        break;
    case NFA_AST_CHAR_CLASS:
        NFA_FREE(ast->char_class.ranges);
        break;
    case NFA_AST_CONCAT:
        for (size_t i = 0; i < ast->concat.count; i++)
            nfa_ast_free(ast->concat.children[i]);
        NFA_FREE(ast->concat.children);
        break;
    case NFA_AST_ALT:
        for (size_t i = 0; i < ast->alt.count; i++)
            nfa_ast_free(ast->alt.children[i]);
        NFA_FREE(ast->alt.children);
        break;
    case NFA_AST_REPEAT:
        nfa_ast_free(ast->repeat.child);
        break;
    case NFA_AST_GROUP:
        nfa_ast_free(ast->group.child);
        break;
    }
    NFA_FREE(ast);
}

/* ── Word-character helper (for \b, \B, \w) ─────────────────────────── */

static bool _nfa_is_word_char(uint8_t b) {
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
           (b >= '0' && b <= '9') || b == '_';
}

/* ── Parser internals ────────────────────────────────────────────────── */

typedef struct {
    const uint8_t* pat;
    size_t         len;
    size_t         pos;
    int            group_count;
    int            error;
    uint8_t        flags;    /* NFA_FLAG_DOT_ALL | NFA_FLAG_MULTILINE | NFA_FLAG_ICASE */
    arena_t*       arena;   /* NULL = use NFA_MALLOC/NFA_FREE */
} _nfa_parser;

/* Dynamic child array for building concat/alt nodes */
typedef struct {
    nfa_ast** items;
    size_t    count;
    size_t    cap;
    arena_t*  arena;
} _nfa_vec;

static void _nfa_vec_init(_nfa_vec* v, arena_t* a) {
    v->items = NULL;
    v->count = 0;
    v->cap   = 0;
    v->arena = a;
}

static bool _nfa_vec_push(_nfa_vec* v, nfa_ast* item) {
    if (v->count == v->cap) {
        size_t newcap  = v->cap == 0 ? 4 : v->cap * 2;
        nfa_ast** buf  = (nfa_ast**)_nfa_tmp_alloc(v->arena, newcap * sizeof(nfa_ast*));
        if (!buf) return false;
        if (v->items) {
            memcpy(buf, v->items, v->count * sizeof(nfa_ast*));
            _nfa_tmp_free(v->arena, v->items);
        }
        v->items = buf;
        v->cap   = newcap;
    }
    v->items[v->count++] = item;
    return true;
}

static void _nfa_vec_free_all(_nfa_vec* v) {
    if (!v->arena) {
        for (size_t i = 0; i < v->count; i++)
            nfa_ast_free(v->items[i]);
        NFA_FREE(v->items);
    }
}

static void _nfa_vec_free_shell(_nfa_vec* v) {
    _nfa_tmp_free(v->arena, v->items);
}

/* UTF-8 lead-byte → sequence length */
static size_t _nfa_utf8_len(uint8_t b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

/* Peek / advance helpers */
static uint8_t _nfa_peek(_nfa_parser* p) {
    return p->pos < p->len ? p->pat[p->pos] : 0;
}
static bool _nfa_at_end(_nfa_parser* p) {
    return p->pos >= p->len;
}
static void _nfa_advance(_nfa_parser* p) {
    p->pos++;
}

/* ── Dynamic range array for building char classes ───────────────────── */

typedef struct {
    nfa_range* items;
    size_t     count;
    size_t     cap;
    arena_t*   arena;
} _nfa_range_vec;

static void _nfa_range_vec_init(_nfa_range_vec* v, arena_t* a) {
    v->items = NULL;
    v->count = 0;
    v->cap   = 0;
    v->arena = a;
}

static bool _nfa_range_vec_push(_nfa_range_vec* v, uint8_t lo, uint8_t hi) {
    if (v->count == v->cap) {
        size_t newcap = v->cap == 0 ? 8 : v->cap * 2;
        nfa_range* buf = (nfa_range*)_nfa_tmp_alloc(v->arena, newcap * sizeof(nfa_range));
        if (!buf) return false;
        if (v->items) {
            memcpy(buf, v->items, v->count * sizeof(nfa_range));
            _nfa_tmp_free(v->arena, v->items);
        }
        v->items = buf;
        v->cap   = newcap;
    }
    v->items[v->count++] = (nfa_range){ lo, hi };
    return true;
}

static void _nfa_range_vec_free(_nfa_range_vec* v) {
    _nfa_tmp_free(v->arena, v->items);
}

/* Add shorthand class ranges (\d, \w, \s) into a range vector.
 * Returns true on success. */
static bool _nfa_add_shorthand(_nfa_range_vec* rv, uint8_t which) {
    switch (which) {
    case 'd':
        return _nfa_range_vec_push(rv, '0', '9');
    case 'w':
        return _nfa_range_vec_push(rv, 'a', 'z') &&
               _nfa_range_vec_push(rv, 'A', 'Z') &&
               _nfa_range_vec_push(rv, '0', '9') &&
               _nfa_range_vec_push(rv, '_', '_');
    case 's':
        return _nfa_range_vec_push(rv, ' ', ' ') &&
               _nfa_range_vec_push(rv, '\t', '\t') &&
               _nfa_range_vec_push(rv, '\n', '\n') &&
               _nfa_range_vec_push(rv, '\r', '\r') &&
               _nfa_range_vec_push(rv, '\f', '\f') &&
               _nfa_range_vec_push(rv, '\v', '\v');
    default:
        return false;
    }
}

/* Forward declarations for recursive descent */
static nfa_ast* _nfa_parse_regex(_nfa_parser* p);
static nfa_ast* _nfa_parse_concat(_nfa_parser* p);
static nfa_ast* _nfa_parse_repeat(_nfa_parser* p);
static nfa_ast* _nfa_parse_atom(_nfa_parser* p);

/* ── char class = '[' '^'? (range|shorthand|literal)* ']' ────────────── */

static nfa_ast* _nfa_parse_char_class(_nfa_parser* p) {
    /* Opening '[' already consumed by caller */
    bool negated = false;
    if (!_nfa_at_end(p) && _nfa_peek(p) == '^') {
        negated = true;
        _nfa_advance(p);
    }

    _nfa_range_vec rv;
    _nfa_range_vec_init(&rv, p->arena);

    /* Special: ']' at very start is literal, not class-close */
    bool first = true;

    while (!_nfa_at_end(p)) {
        uint8_t c = _nfa_peek(p);

        if (c == ']' && !first) break;

        first = false;

        if (c == '\\') {
            _nfa_advance(p);
            if (_nfa_at_end(p)) {
                p->error = NFA_ERR_TRAILING_BACKSLASH;
                _nfa_range_vec_free(&rv);
                return NULL;
            }
            uint8_t esc = _nfa_peek(p);
            _nfa_advance(p);
            /* Shorthand inside class */
            if (esc == 'd' || esc == 'w' || esc == 's') {
                if (!_nfa_add_shorthand(&rv, esc)) {
                    p->error = NFA_ERR_ALLOC;
                    _nfa_range_vec_free(&rv);
                    return NULL;
                }
                continue;
            }
            if (esc == 'D' || esc == 'W' || esc == 'S') {
                /* Negated shorthand inside class: add as plain ranges
                 * (the negation is the user's responsibility when mixing;
                 *  standard regex semantics: \D inside [] adds digit-complement ranges) */
                /* For simplicity, add the base ranges; negate handled at class level
                 * would require set algebra. Instead, expand to byte ranges: */
                /* Actually, the standard approach is to just expand \D to [^0-9] etc.
                 * Inside [...], we add the complementary ranges directly. */
                uint8_t base = (uint8_t)(esc + ('a' - 'A')); /* d, w, or s */
                /* We need to add all byte values NOT in the shorthand.
                 * This is complex. A simpler approach: for standalone \D etc,
                 * we create a negated char class. Inside [...], adding \D means
                 * we add all non-digit ranges. Let's expand them explicitly. */
                if (base == 'd') {
                    /* [^0-9] = [0-47,58-255] */
                    if (!_nfa_range_vec_push(&rv, 0x00, '0' - 1) ||
                        !_nfa_range_vec_push(&rv, '9' + 1, 0xFF)) {
                        p->error = NFA_ERR_ALLOC;
                        _nfa_range_vec_free(&rv);
                        return NULL;
                    }
                } else if (base == 'w') {
                    /* complement of [a-zA-Z0-9_] */
                    if (!_nfa_range_vec_push(&rv, 0x00, '0' - 1) ||
                        !_nfa_range_vec_push(&rv, '9' + 1, 'A' - 1) ||
                        !_nfa_range_vec_push(&rv, 'Z' + 1, '_' - 1) ||
                        !_nfa_range_vec_push(&rv, '_' + 1, 'a' - 1) ||
                        !_nfa_range_vec_push(&rv, 'z' + 1, 0xFF)) {
                        p->error = NFA_ERR_ALLOC;
                        _nfa_range_vec_free(&rv);
                        return NULL;
                    }
                } else { /* base == 's' */
                    /* complement of [ \t\n\r\f\v] = [\x00-\x08\x0E-\x1F\x21-\xFF] */
                    if (!_nfa_range_vec_push(&rv, 0x00, '\t' - 1) ||
                        !_nfa_range_vec_push(&rv, '\r' + 1, ' ' - 1) ||
                        !_nfa_range_vec_push(&rv, ' ' + 1, 0xFF)) {
                        p->error = NFA_ERR_ALLOC;
                        _nfa_range_vec_free(&rv);
                        return NULL;
                    }
                }
                continue;
            }
            /* Escaped metachar: treat as literal byte */
            c = esc;
        } else {
            _nfa_advance(p);
        }

        /* Check for range: c '-' d (where '-' is not at end) */
        if (!_nfa_at_end(p) && _nfa_peek(p) == '-') {
            /* Peek ahead: if next after '-' is ']' or end, '-' is literal */
            if (p->pos + 1 < p->len && p->pat[p->pos + 1] != ']') {
                _nfa_advance(p); /* skip '-' */
                uint8_t hi;
                if (_nfa_at_end(p)) {
                    p->error = NFA_ERR_UNMATCHED_LBRACKET;
                    _nfa_range_vec_free(&rv);
                    return NULL;
                }
                if (_nfa_peek(p) == '\\') {
                    _nfa_advance(p);
                    if (_nfa_at_end(p)) {
                        p->error = NFA_ERR_TRAILING_BACKSLASH;
                        _nfa_range_vec_free(&rv);
                        return NULL;
                    }
                    hi = _nfa_peek(p);
                    _nfa_advance(p);
                } else {
                    hi = _nfa_peek(p);
                    _nfa_advance(p);
                }
                if (!_nfa_range_vec_push(&rv, c, hi)) {
                    p->error = NFA_ERR_ALLOC;
                    _nfa_range_vec_free(&rv);
                    return NULL;
                }
                continue;
            }
        }

        /* Plain single-byte literal */
        if (!_nfa_range_vec_push(&rv, c, c)) {
            p->error = NFA_ERR_ALLOC;
            _nfa_range_vec_free(&rv);
            return NULL;
        }
    }

    if (_nfa_at_end(p)) {
        p->error = NFA_ERR_UNMATCHED_LBRACKET;
        _nfa_range_vec_free(&rv);
        return NULL;
    }

    _nfa_advance(p); /* consume ']' */

    nfa_ast* node = nfa_ast_char_class(rv.items, rv.count, negated, p->arena);
    _nfa_range_vec_free(&rv);
    if (!node) { p->error = NFA_ERR_ALLOC; return NULL; }
    node->char_class.icase = (p->flags & NFA_FLAG_ICASE) != 0;
    return node;
}

/* ── regex = concat ('|' concat)* ────────────────────────────────────── */

static nfa_ast* _nfa_parse_regex(_nfa_parser* p) {
    nfa_ast* first = _nfa_parse_concat(p);
    if (!first || p->error) return first;

    if (_nfa_at_end(p) || _nfa_peek(p) != '|')
        return first;

    _nfa_vec alts;
    _nfa_vec_init(&alts, p->arena);
    if (!_nfa_vec_push(&alts, first)) {
        p->error = NFA_ERR_ALLOC;
        if (!p->arena) nfa_ast_free(first);
        return NULL;
    }

    while (!_nfa_at_end(p) && _nfa_peek(p) == '|') {
        _nfa_advance(p);
        nfa_ast* branch = _nfa_parse_concat(p);
        if (p->error) {
            if (!p->arena) nfa_ast_free(branch);
            _nfa_vec_free_all(&alts);
            return NULL;
        }
        if (!branch) {
            branch = nfa_ast_concat(NULL, 0, p->arena);
            if (!branch) {
                p->error = NFA_ERR_ALLOC;
                _nfa_vec_free_all(&alts);
                return NULL;
            }
        }
        if (!_nfa_vec_push(&alts, branch)) {
            p->error = NFA_ERR_ALLOC;
            if (!p->arena) nfa_ast_free(branch);
            _nfa_vec_free_all(&alts);
            return NULL;
        }
    }

    nfa_ast* result = nfa_ast_alt(alts.items, alts.count, p->arena);
    _nfa_vec_free_shell(&alts);
    if (!result) p->error = NFA_ERR_ALLOC;
    return result;
}

/* ── concat = repeat* ────────────────────────────────────────────────── */

static nfa_ast* _nfa_parse_concat(_nfa_parser* p) {
    _nfa_vec parts;
    _nfa_vec_init(&parts, p->arena);

    while (!_nfa_at_end(p) && _nfa_peek(p) != '|' && _nfa_peek(p) != ')') {
        nfa_ast* part = _nfa_parse_repeat(p);
        if (p->error) {
            if (!p->arena) nfa_ast_free(part);
            _nfa_vec_free_all(&parts);
            return NULL;
        }
        if (!part) break;
        if (!_nfa_vec_push(&parts, part)) {
            p->error = NFA_ERR_ALLOC;
            if (!p->arena) nfa_ast_free(part);
            _nfa_vec_free_all(&parts);
            return NULL;
        }
    }

    if (parts.count == 0) {
        _nfa_vec_free_shell(&parts);
        return nfa_ast_concat(NULL, 0, p->arena);
    }
    if (parts.count == 1) {
        nfa_ast* single = parts.items[0];
        _nfa_vec_free_shell(&parts);
        return single;
    }

    nfa_ast* result = nfa_ast_concat(parts.items, parts.count, p->arena);
    _nfa_vec_free_shell(&parts);
    if (!result) p->error = NFA_ERR_ALLOC;
    return result;
}

/* ── repeat = atom ('*' | '+' | '?') '?'? ────────────────────────────── */

static nfa_ast* _nfa_parse_repeat(_nfa_parser* p) {
    nfa_ast* atom = _nfa_parse_atom(p);
    if (!atom || p->error) return atom;

    if (!_nfa_at_end(p)) {
        uint8_t c = _nfa_peek(p);
        int min = -1, max = -1;
        if      (c == '*') { min = 0; max = -1; }
        else if (c == '+') { min = 1; max = -1; }
        else if (c == '?') { min = 0; max =  1; }
        if (min >= 0) {
            _nfa_advance(p);
            bool greedy = true;
            if (!_nfa_at_end(p) && _nfa_peek(p) == '?') {
                greedy = false;
                _nfa_advance(p);
            }
            return nfa_ast_repeat(atom, min, max, greedy, p->arena);
        }

        /* Counted repetition: {n}, {n,m}, {n,} */
        if (c == '{') {
            size_t save = p->pos;
            _nfa_advance(p); /* past '{' */

            /* Parse first number n */
            if (_nfa_at_end(p) || _nfa_peek(p) < '0' || _nfa_peek(p) > '9') {
                p->pos = save;
                return atom;
            }
            int n = 0;
            while (!_nfa_at_end(p) && _nfa_peek(p) >= '0' && _nfa_peek(p) <= '9') {
                n = n * 10 + (_nfa_peek(p) - '0');
                if (n > 100000) { p->pos = save; return atom; }
                _nfa_advance(p);
            }
            if (_nfa_at_end(p)) { p->pos = save; return atom; }

            int rmin, rmax;
            uint8_t nc = _nfa_peek(p);

            if (nc == '}') {
                /* {n} exact */
                _nfa_advance(p);
                rmin = n; rmax = n;
            } else if (nc == ',') {
                _nfa_advance(p); /* past ',' */
                if (_nfa_at_end(p)) { p->pos = save; return atom; }
                if (_nfa_peek(p) == '}') {
                    /* {n,} unbounded */
                    _nfa_advance(p);
                    rmin = n; rmax = -1;
                } else if (_nfa_peek(p) >= '0' && _nfa_peek(p) <= '9') {
                    /* {n,m} */
                    int m = 0;
                    while (!_nfa_at_end(p) && _nfa_peek(p) >= '0' && _nfa_peek(p) <= '9') {
                        m = m * 10 + (_nfa_peek(p) - '0');
                        if (m > 100000) { p->pos = save; return atom; }
                        _nfa_advance(p);
                    }
                    if (_nfa_at_end(p) || _nfa_peek(p) != '}') {
                        p->pos = save;
                        return atom;
                    }
                    _nfa_advance(p); /* past '}' */
                    if (m < n) {
                        if (!p->arena) nfa_ast_free(atom);
                        p->error = NFA_ERR_REPEAT_RANGE;
                        return NULL;
                    }
                    rmin = n; rmax = m;
                } else {
                    p->pos = save;
                    return atom;
                }
            } else {
                p->pos = save;
                return atom;
            }

            /* Check for lazy modifier */
            bool rgreedy = true;
            if (!_nfa_at_end(p) && _nfa_peek(p) == '?') {
                rgreedy = false;
                _nfa_advance(p);
            }

            return nfa_ast_repeat(atom, rmin, rmax, rgreedy, p->arena);
        }
    }
    return atom;
}

/* ── atom = literal | '.' | '^' | '$' | '(' regex ')' | '\' meta ──── */

static nfa_ast* _nfa_parse_atom(_nfa_parser* p) {
    if (_nfa_at_end(p)) return NULL;

    uint8_t c = _nfa_peek(p);

    switch (c) {
    case '(': {
        _nfa_advance(p);

        /* Check for (?...) syntax: non-capturing groups and flags */
        if (!_nfa_at_end(p) && _nfa_peek(p) == '?') {
            _nfa_advance(p); /* past '?' */

            /* Parse flag characters (i, s, m) */
            uint8_t new_flags = p->flags;
            bool has_flags = false;
            while (!_nfa_at_end(p)) {
                uint8_t fc = _nfa_peek(p);
                if (fc == 's') {
                    new_flags |= NFA_FLAG_DOT_ALL;
                    has_flags = true;
                    _nfa_advance(p);
                } else if (fc == 'm') {
                    new_flags |= NFA_FLAG_MULTILINE;
                    has_flags = true;
                    _nfa_advance(p);
                } else if (fc == 'i') {
                    new_flags |= NFA_FLAG_ICASE;
                    has_flags = true;
                    _nfa_advance(p);
                } else {
                    break;
                }
            }

            if (_nfa_at_end(p)) {
                p->error = NFA_ERR_UNMATCHED_LPAREN;
                return NULL;
            }

            uint8_t nc = _nfa_peek(p);
            if (nc == ':') {
                /* Non-capturing group (?:...) or scoped flags (?s:...) */
                _nfa_advance(p); /* past ':' */
                uint8_t saved_flags = p->flags;
                p->flags = new_flags;
                nfa_ast* inner = _nfa_parse_regex(p);
                p->flags = saved_flags;
                if (p->error) { if (!p->arena) nfa_ast_free(inner); return NULL; }
                if (_nfa_at_end(p) || _nfa_peek(p) != ')') {
                    if (!p->arena) nfa_ast_free(inner);
                    p->error = NFA_ERR_UNMATCHED_LPAREN;
                    return NULL;
                }
                _nfa_advance(p); /* past ')' */
                if (!inner) inner = nfa_ast_concat(NULL, 0, p->arena);
                return inner;
            } else if (nc == ')' && has_flags) {
                /* Standalone flags (?s) — set for rest of pattern */
                _nfa_advance(p);
                p->flags = new_flags;
                return nfa_ast_concat(NULL, 0, p->arena);
            } else {
                p->error = NFA_ERR_INVALID_GROUP;
                return NULL;
            }
        }

        /* Regular capturing group */
        int idx = p->group_count++;
        nfa_ast* inner = _nfa_parse_regex(p);
        if (p->error) {
            if (!p->arena) nfa_ast_free(inner);
            return NULL;
        }
        if (_nfa_at_end(p) || _nfa_peek(p) != ')') {
            if (!p->arena) nfa_ast_free(inner);
            p->error = NFA_ERR_UNMATCHED_LPAREN;
            return NULL;
        }
        _nfa_advance(p);
        if (!inner) inner = nfa_ast_concat(NULL, 0, p->arena);
        return nfa_ast_group(inner, idx, p->arena);
    }

    case ')':
        return NULL; /* caller handles or nfa_parse detects unmatched */

    case '.':
        _nfa_advance(p);
        return nfa_ast_dot((p->flags & NFA_FLAG_DOT_ALL) != 0, p->arena);

    case '^':
        _nfa_advance(p);
        return nfa_ast_anchor(NFA_ANCHOR_BOL, p->arena);

    case '$':
        _nfa_advance(p);
        return nfa_ast_anchor(NFA_ANCHOR_EOL, p->arena);

    case '|':
        return NULL; /* handled by parse_regex */

    case '*': case '+': case '?':
        return NULL; /* quantifier without preceding atom */

    case '[': {
        _nfa_advance(p);
        return _nfa_parse_char_class(p);
    }

    case '\\': {
        _nfa_advance(p);
        if (_nfa_at_end(p)) {
            p->error = NFA_ERR_TRAILING_BACKSLASH;
            return NULL;
        }
        uint8_t esc = _nfa_peek(p);
        /* Shorthand character classes */
        if (esc == 'd' || esc == 'w' || esc == 's') {
            _nfa_advance(p);
            _nfa_range_vec rv;
            _nfa_range_vec_init(&rv, p->arena);
            if (!_nfa_add_shorthand(&rv, esc)) {
                p->error = NFA_ERR_ALLOC;
                _nfa_range_vec_free(&rv);
                return NULL;
            }
            nfa_ast* node = nfa_ast_char_class(rv.items, rv.count, false, p->arena);
            _nfa_range_vec_free(&rv);
            if (!node) { p->error = NFA_ERR_ALLOC; return NULL; }
            node->char_class.icase = (p->flags & NFA_FLAG_ICASE) != 0;
            return node;
        }
        if (esc == 'D' || esc == 'W' || esc == 'S') {
            _nfa_advance(p);
            uint8_t base = (uint8_t)(esc + ('a' - 'A'));
            _nfa_range_vec rv;
            _nfa_range_vec_init(&rv, p->arena);
            if (!_nfa_add_shorthand(&rv, base)) {
                p->error = NFA_ERR_ALLOC;
                _nfa_range_vec_free(&rv);
                return NULL;
            }
            nfa_ast* node = nfa_ast_char_class(rv.items, rv.count, true, p->arena);
            _nfa_range_vec_free(&rv);
            if (!node) { p->error = NFA_ERR_ALLOC; return NULL; }
            node->char_class.icase = (p->flags & NFA_FLAG_ICASE) != 0;
            return node;
        }
        if (esc == 'b') {
            _nfa_advance(p);
            return nfa_ast_anchor(NFA_ANCHOR_WORD, p->arena);
        }
        if (esc == 'B') {
            _nfa_advance(p);
            return nfa_ast_anchor(NFA_ANCHOR_NON_WORD, p->arena);
        }
        if (esc == '.' || esc == '\\' || esc == '|' ||
            esc == '(' || esc == ')' || esc == '*' ||
            esc == '+' || esc == '?' || esc == '[' ||
            esc == ']' || esc == '{' || esc == '}' ||
            esc == '^' || esc == '$') {
            _nfa_advance(p);
            nfa_ast* lit = nfa_ast_literal(&esc, 1, p->arena);
            if (lit) lit->literal.icase = (p->flags & NFA_FLAG_ICASE) != 0;
            return lit;
        }
        p->error = NFA_ERR_INVALID_ESCAPE;
        return NULL;
    }

    default: {
        /* Literal byte or multi-byte UTF-8 sequence */
        size_t slen = _nfa_utf8_len(c);
        if (p->pos + slen > p->len) slen = 1; /* truncated */
        const uint8_t* start = &p->pat[p->pos];
        p->pos += slen;
        nfa_ast* lit = nfa_ast_literal(start, slen, p->arena);
        if (lit) lit->literal.icase = (p->flags & NFA_FLAG_ICASE) != 0;
        return lit;
    }
    }
}

/* ── Public parse entry point ────────────────────────────────────────── */

static int nfa_parse(const uint8_t* pattern, size_t len, nfa_ast** out_ast) {
    _nfa_parser p = { .pat = pattern, .len = len, .pos = 0,
                      .group_count = 0, .error = NFA_OK, .flags = 0,
                      .arena = NULL };

    *out_ast = _nfa_parse_regex(&p);

    if (p.error) {
        nfa_ast_free(*out_ast);
        *out_ast = NULL;
        return p.error;
    }

    if (p.pos < p.len && p.pat[p.pos] == ')') {
        nfa_ast_free(*out_ast);
        *out_ast = NULL;
        return NFA_ERR_UNMATCHED_RPAREN;
    }

    if (!*out_ast) {
        *out_ast = nfa_ast_concat(NULL, 0, NULL);
        if (!*out_ast) return NFA_ERR_ALLOC;
    }

    return NFA_OK;
}

/* ── NFA instruction types ──────────────────────────────────────────── */

typedef enum {
    NFA_INST_LITERAL,    /* match specific byte */
    NFA_INST_RANGE,      /* match byte in [lo,hi] inclusive */
    NFA_INST_ANY,        /* match any byte except \n */
    NFA_INST_ANY_ALL,    /* match any byte including \n (dot-all mode) */
    NFA_INST_MATCH,      /* accept */
    NFA_INST_SPLIT,      /* epsilon to two targets */
    NFA_INST_JMP,        /* epsilon to one target */
    NFA_INST_ANCHOR_BOL,      /* beginning of line */
    NFA_INST_ANCHOR_EOL,      /* end of line */
    NFA_INST_ANCHOR_WORD,     /* word boundary (\b) */
    NFA_INST_ANCHOR_NON_WORD, /* non-word boundary (\B) */
    NFA_INST_SAVE,            /* capture slot marker (epsilon) */
} nfa_inst_type;

typedef struct {
    nfa_inst_type type;
    uint8_t byte;       /* LITERAL: byte to match */
    uint8_t lo, hi;     /* RANGE: byte range */
    size_t  out;        /* primary target */
    size_t  out1;       /* SPLIT: secondary target */
    size_t  slot;       /* SAVE: slot index */
} nfa_inst;

struct nfa_program {
    nfa_inst* code;       /* instruction array */
    size_t    len;        /* instruction count */
    size_t    start;      /* start instruction index */
    size_t    n_captures; /* number of capture slots = 2 * group_count */
    bool      greedy_match; /* true if all quantifiers greedy (prefer longest) */
    uint8_t   flags;      /* global flag defaults (NFA_FLAG_*) */
};

/* ── Compiler internals ─────────────────────────────────────────────── */

/* Patch location: instruction index + which output field */
typedef struct {
    size_t inst;   /* instruction index in code array */
    int    which;  /* 0 = out, 1 = out1 (for SPLIT) */
} _nfa_patchloc;

/* Fragment: entry point + list of unpatched exits */
typedef struct {
    size_t         start;   /* entry instruction index */
    _nfa_patchloc* patches; /* unpatched exit locations */
    size_t         pcount;
    size_t         pcap;
    arena_t*       arena;
} _nfa_frag;

/* Compiler state */
typedef struct {
    nfa_inst* code;
    size_t    len;
    size_t    cap;
    int       error;
    arena_t*  arena;
} _nfa_compiler;

static _nfa_frag _nfa_frag_make(size_t start, arena_t* a) {
    _nfa_frag f;
    f.start   = start;
    f.patches = NULL;
    f.pcount  = 0;
    f.pcap    = 0;
    f.arena   = a;
    return f;
}

static bool _nfa_frag_add(_nfa_frag* f, size_t inst, int which) {
    if (f->pcount == f->pcap) {
        size_t newcap = f->pcap == 0 ? 4 : f->pcap * 2;
        _nfa_patchloc* buf = (_nfa_patchloc*)_nfa_tmp_alloc(f->arena, newcap * sizeof(_nfa_patchloc));
        if (!buf) return false;
        if (f->patches) {
            memcpy(buf, f->patches, f->pcount * sizeof(_nfa_patchloc));
            _nfa_tmp_free(f->arena, f->patches);
        }
        f->patches = buf;
        f->pcap    = newcap;
    }
    f->patches[f->pcount++] = (_nfa_patchloc){ inst, which };
    return true;
}

static bool _nfa_frag_merge(_nfa_frag* dst, const _nfa_frag* src) {
    for (size_t i = 0; i < src->pcount; i++) {
        if (!_nfa_frag_add(dst, src->patches[i].inst, src->patches[i].which))
            return false;
    }
    return true;
}

static void _nfa_frag_free(_nfa_frag* f) {
    _nfa_tmp_free(f->arena, f->patches);
    f->patches = NULL;
    f->pcount  = 0;
    f->pcap    = 0;
}

/* Resolve all patch locations to point to target */
static void _nfa_frag_patch(_nfa_compiler* c, _nfa_frag* f, size_t target) {
    for (size_t i = 0; i < f->pcount; i++) {
        _nfa_patchloc* p = &f->patches[i];
        if (p->which == 0)
            c->code[p->inst].out = target;
        else
            c->code[p->inst].out1 = target;
    }
}

/* Emit instruction, return its index. All fields zeroed except type. */
static size_t _nfa_emit_inst(_nfa_compiler* c, nfa_inst_type type) {
    if (c->error) return 0;
    if (c->len == c->cap) {
        size_t newcap = c->cap == 0 ? 16 : c->cap * 2;
        nfa_inst* buf = (nfa_inst*)_nfa_tmp_alloc(c->arena, newcap * sizeof(nfa_inst));
        if (!buf) { c->error = NFA_ERR_ALLOC; return 0; }
        if (c->code) {
            memcpy(buf, c->code, c->len * sizeof(nfa_inst));
            _nfa_tmp_free(c->arena, c->code);
        }
        c->code = buf;
        c->cap  = newcap;
    }
    size_t idx = c->len++;
    memset(&c->code[idx], 0, sizeof(nfa_inst));
    c->code[idx].type = type;
    return idx;
}

/* Count groups in AST (returns max group index + 1) */
static int _nfa_count_groups(nfa_ast* ast) {
    if (!ast) return 0;
    switch (ast->type) {
    case NFA_AST_GROUP: {
        int m  = ast->group.index + 1;
        int cm = _nfa_count_groups(ast->group.child);
        return m > cm ? m : cm;
    }
    case NFA_AST_CONCAT: {
        int m = 0;
        for (size_t i = 0; i < ast->concat.count; i++) {
            int g = _nfa_count_groups(ast->concat.children[i]);
            if (g > m) m = g;
        }
        return m;
    }
    case NFA_AST_ALT: {
        int m = 0;
        for (size_t i = 0; i < ast->alt.count; i++) {
            int g = _nfa_count_groups(ast->alt.children[i]);
            if (g > m) m = g;
        }
        return m;
    }
    case NFA_AST_REPEAT:
        return _nfa_count_groups(ast->repeat.child);
    default:
        return 0;
    }
}

/* Compute complement of byte ranges for negated character classes.
 * Sorts, merges overlapping, then inverts. Returns count written to out.
 * out must have space for at least count+1 entries. */
static size_t _nfa_complement_ranges(const nfa_range* in, size_t count,
                                      nfa_range* out, size_t out_cap) {
    (void)out_cap;
    if (count == 0) {
        out[0] = (nfa_range){0, 255};
        return 1;
    }

    /* Sort by lo (insertion sort — count is small) */
    nfa_range sorted[256];
    size_t n = count < 256 ? count : 256;
    memcpy(sorted, in, n * sizeof(nfa_range));
    for (size_t i = 1; i < n; i++) {
        nfa_range key = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1].lo > key.lo) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }

    /* Merge overlapping/adjacent ranges */
    nfa_range merged[256];
    size_t m = 1;
    merged[0] = sorted[0];
    for (size_t i = 1; i < n; i++) {
        if (sorted[i].lo <= (int)merged[m - 1].hi + 1) {
            if (sorted[i].hi > merged[m - 1].hi)
                merged[m - 1].hi = sorted[i].hi;
        } else {
            merged[m++] = sorted[i];
        }
    }

    /* Compute complement */
    size_t c = 0;
    int prev = 0;
    for (size_t i = 0; i < m; i++) {
        if ((int)merged[i].lo > prev)
            out[c++] = (nfa_range){(uint8_t)prev, (uint8_t)(merged[i].lo - 1)};
        prev = (int)merged[i].hi + 1;
    }
    if (prev <= 255)
        out[c++] = (nfa_range){(uint8_t)prev, 255};
    return c;
}

/* Check if all Repeat nodes in the AST are greedy.
 * Returns false if any lazy quantifier exists. */
static bool _nfa_all_greedy(nfa_ast* ast) {
    if (!ast) return true;
    switch (ast->type) {
    case NFA_AST_REPEAT:
        return ast->repeat.greedy && _nfa_all_greedy(ast->repeat.child);
    case NFA_AST_CONCAT:
        for (size_t i = 0; i < ast->concat.count; i++)
            if (!_nfa_all_greedy(ast->concat.children[i])) return false;
        return true;
    case NFA_AST_ALT:
        for (size_t i = 0; i < ast->alt.count; i++)
            if (!_nfa_all_greedy(ast->alt.children[i])) return false;
        return true;
    case NFA_AST_GROUP:
        return _nfa_all_greedy(ast->group.child);
    default:
        return true;
    }
}

/* ── Case-insensitive helpers ────────────────────────────────────────── */

static bool _nfa_is_alpha(uint8_t b) {
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z');
}

/* Expand ranges for case-insensitive matching: for each range that overlaps
 * ASCII letters, add the corresponding case-mapped range.
 * Returns true on success. */
static bool _nfa_icase_expand(_nfa_range_vec* rv, const nfa_range* ranges, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!_nfa_range_vec_push(rv, ranges[i].lo, ranges[i].hi))
            return false;
        uint8_t lo = ranges[i].lo, hi = ranges[i].hi;
        /* Overlap with lowercase a-z → add uppercase A-Z portion */
        if (lo <= 'z' && hi >= 'a') {
            uint8_t alo = lo < 'a' ? 'a' : lo;
            uint8_t ahi = hi > 'z' ? 'z' : hi;
            if (!_nfa_range_vec_push(rv, (uint8_t)(alo - 32), (uint8_t)(ahi - 32)))
                return false;
        }
        /* Overlap with uppercase A-Z → add lowercase a-z portion */
        if (lo <= 'Z' && hi >= 'A') {
            uint8_t alo = lo < 'A' ? 'A' : lo;
            uint8_t ahi = hi > 'Z' ? 'Z' : hi;
            if (!_nfa_range_vec_push(rv, (uint8_t)(alo + 32), (uint8_t)(ahi + 32)))
                return false;
        }
    }
    return true;
}

/* Forward declaration for recursive compilation */
static _nfa_frag _nfa_compile_node(_nfa_compiler* c, nfa_ast* ast);

static _nfa_frag _nfa_compile_node(_nfa_compiler* c, nfa_ast* ast) {
    _nfa_frag empty = _nfa_frag_make(0, c->arena);
    if (c->error || !ast) return empty;

    switch (ast->type) {

    case NFA_AST_LITERAL: {
        bool icase = ast->literal.icase;
        _nfa_frag result = _nfa_frag_make(0, c->arena);
        bool has_result = false;

        for (size_t i = 0; i < ast->literal.len; i++) {
            uint8_t b = ast->literal.bytes[i];
            _nfa_frag byte_frag;

            if (icase && _nfa_is_alpha(b)) {
                /* Case-insensitive alpha: SPLIT → RANGE(lower) / RANGE(upper) */
                uint8_t lo = (b >= 'a') ? b : (uint8_t)(b + 32);
                uint8_t hi = (b <= 'Z') ? b : (uint8_t)(b - 32);
                size_t sidx = _nfa_emit_inst(c, NFA_INST_SPLIT);
                size_t r0   = _nfa_emit_inst(c, NFA_INST_RANGE);
                size_t r1   = _nfa_emit_inst(c, NFA_INST_RANGE);
                if (c->error) { if (has_result) _nfa_frag_free(&result); return empty; }
                c->code[r0].lo = lo; c->code[r0].hi = lo;
                c->code[r1].lo = hi; c->code[r1].hi = hi;
                c->code[sidx].out  = r0;
                c->code[sidx].out1 = r1;
                byte_frag = _nfa_frag_make(sidx, c->arena);
                if (!_nfa_frag_add(&byte_frag, r0, 0) ||
                    !_nfa_frag_add(&byte_frag, r1, 0)) {
                    c->error = NFA_ERR_ALLOC;
                    _nfa_frag_free(&byte_frag);
                    if (has_result) _nfa_frag_free(&result);
                    return empty;
                }
            } else {
                size_t idx = _nfa_emit_inst(c, NFA_INST_LITERAL);
                if (c->error) { if (has_result) _nfa_frag_free(&result); return empty; }
                c->code[idx].byte = b;
                byte_frag = _nfa_frag_make(idx, c->arena);
                if (!_nfa_frag_add(&byte_frag, idx, 0)) {
                    c->error = NFA_ERR_ALLOC;
                    if (has_result) _nfa_frag_free(&result);
                    return empty;
                }
            }

            if (!has_result) {
                result = byte_frag;
                has_result = true;
            } else {
                _nfa_frag_patch(c, &result, byte_frag.start);
                _nfa_tmp_free(c->arena, result.patches);
                result.patches = byte_frag.patches;
                result.pcount  = byte_frag.pcount;
                result.pcap    = byte_frag.pcap;
            }
        }
        return result;
    }

    case NFA_AST_DOT: {
        nfa_inst_type itype = ast->dot.dot_all ? NFA_INST_ANY_ALL : NFA_INST_ANY;
        size_t idx = _nfa_emit_inst(c, itype);
        if (c->error) return empty;
        _nfa_frag f = _nfa_frag_make(idx, c->arena);
        if (!_nfa_frag_add(&f, idx, 0)) { c->error = NFA_ERR_ALLOC; return empty; }
        return f;
    }

    case NFA_AST_ANCHOR: {
        nfa_inst_type itype;
        switch (ast->anchor) {
        case NFA_ANCHOR_BOL:      itype = NFA_INST_ANCHOR_BOL;      break;
        case NFA_ANCHOR_EOL:      itype = NFA_INST_ANCHOR_EOL;      break;
        case NFA_ANCHOR_WORD:     itype = NFA_INST_ANCHOR_WORD;     break;
        case NFA_ANCHOR_NON_WORD: itype = NFA_INST_ANCHOR_NON_WORD; break;
        }
        size_t idx = _nfa_emit_inst(c, itype);
        if (c->error) return empty;
        _nfa_frag f = _nfa_frag_make(idx, c->arena);
        if (!_nfa_frag_add(&f, idx, 0)) { c->error = NFA_ERR_ALLOC; return empty; }
        return f;
    }

    case NFA_AST_CHAR_CLASS: {
        nfa_range* ranges = ast->char_class.ranges;
        size_t rcount = ast->char_class.count;

        /* Expand ranges for case-insensitive matching */
        _nfa_range_vec icase_rv;
        _nfa_range_vec_init(&icase_rv, c->arena);
        if (ast->char_class.icase) {
            if (!_nfa_icase_expand(&icase_rv, ranges, rcount)) {
                c->error = NFA_ERR_ALLOC;
                _nfa_range_vec_free(&icase_rv);
                return empty;
            }
            ranges = icase_rv.items;
            rcount = icase_rv.count;
        }

        /* Compute complement for negated classes */
        nfa_range comp[257];
        if (ast->char_class.negated) {
            rcount = _nfa_complement_ranges(ranges, rcount, comp, 257);
            ranges = comp;
        }

        if (rcount == 0) {
            /* Impossible match (e.g. [^\x00-\xFF]) */
            size_t idx = _nfa_emit_inst(c, NFA_INST_RANGE);
            if (c->error) { _nfa_range_vec_free(&icase_rv); return empty; }
            c->code[idx].lo = 1;
            c->code[idx].hi = 0;
            _nfa_frag f = _nfa_frag_make(idx, c->arena);
            if (!_nfa_frag_add(&f, idx, 0)) { c->error = NFA_ERR_ALLOC; _nfa_frag_free(&f); _nfa_range_vec_free(&icase_rv); return empty; }
            _nfa_range_vec_free(&icase_rv);
            return f;
        }

        if (rcount == 1) {
            size_t idx = _nfa_emit_inst(c, NFA_INST_RANGE);
            if (c->error) { _nfa_range_vec_free(&icase_rv); return empty; }
            c->code[idx].lo = ranges[0].lo;
            c->code[idx].hi = ranges[0].hi;
            _nfa_frag f = _nfa_frag_make(idx, c->arena);
            if (!_nfa_frag_add(&f, idx, 0)) { c->error = NFA_ERR_ALLOC; _nfa_frag_free(&f); _nfa_range_vec_free(&icase_rv); return empty; }
            _nfa_range_vec_free(&icase_rv);
            return f;
        }

        /* Multiple ranges: SPLIT chain
         * Layout: [SPLIT, RANGE, SPLIT, RANGE, ..., RANGE]
         * Each SPLIT.out → adjacent RANGE, SPLIT.out1 → next SPLIT (or last RANGE) */
        size_t base = c->len;
        for (size_t i = 0; i < rcount - 1; i++) {
            size_t sidx = _nfa_emit_inst(c, NFA_INST_SPLIT);
            size_t ridx = _nfa_emit_inst(c, NFA_INST_RANGE);
            if (c->error) { _nfa_range_vec_free(&icase_rv); return empty; }
            c->code[ridx].lo  = ranges[i].lo;
            c->code[ridx].hi  = ranges[i].hi;
            c->code[sidx].out = ridx;
        }
        size_t last = _nfa_emit_inst(c, NFA_INST_RANGE);
        if (c->error) { _nfa_range_vec_free(&icase_rv); return empty; }
        c->code[last].lo = ranges[rcount - 1].lo;
        c->code[last].hi = ranges[rcount - 1].hi;

        /* Wire SPLIT.out1 → next SPLIT or last RANGE */
        for (size_t i = 0; i < rcount - 1; i++) {
            size_t sidx = base + i * 2;
            c->code[sidx].out1 = (i + 1 < rcount - 1)
                ? base + (i + 1) * 2   /* next SPLIT */
                : last;                 /* last RANGE */
        }

        /* Build fragment: all RANGE.out fields are patchable exits */
        _nfa_frag f = _nfa_frag_make(base, c->arena);
        for (size_t i = 0; i < rcount - 1; i++) {
            if (!_nfa_frag_add(&f, base + i * 2 + 1, 0))
                { c->error = NFA_ERR_ALLOC; _nfa_frag_free(&f); _nfa_range_vec_free(&icase_rv); return empty; }
        }
        if (!_nfa_frag_add(&f, last, 0))
            { c->error = NFA_ERR_ALLOC; _nfa_frag_free(&f); _nfa_range_vec_free(&icase_rv); return empty; }
        _nfa_range_vec_free(&icase_rv);
        return f;
    }

    case NFA_AST_CONCAT: {
        if (ast->concat.count == 0) {
            /* Empty concat: matches empty string */
            size_t idx = _nfa_emit_inst(c, NFA_INST_JMP);
            if (c->error) return empty;
            _nfa_frag f = _nfa_frag_make(idx, c->arena);
            if (!_nfa_frag_add(&f, idx, 0)) { c->error = NFA_ERR_ALLOC; return empty; }
            return f;
        }

        _nfa_frag result = _nfa_compile_node(c, ast->concat.children[0]);
        if (c->error) { _nfa_frag_free(&result); return empty; }

        for (size_t i = 1; i < ast->concat.count; i++) {
            _nfa_frag next = _nfa_compile_node(c, ast->concat.children[i]);
            if (c->error) { _nfa_frag_free(&result); _nfa_frag_free(&next); return empty; }
            /* Patch result's exits → next's entry */
            _nfa_frag_patch(c, &result, next.start);
            /* Result keeps its start, takes next's patches */
            _nfa_tmp_free(c->arena, result.patches);
            result.patches = next.patches;
            result.pcount  = next.pcount;
            result.pcap    = next.pcap;
            /* next's patch array ownership transferred; don't free it */
        }
        return result;
    }

    case NFA_AST_ALT: {
        size_t k = ast->alt.count;
        if (k == 0) {
            size_t idx = _nfa_emit_inst(c, NFA_INST_JMP);
            if (c->error) return empty;
            _nfa_frag f = _nfa_frag_make(idx, c->arena);
            if (!_nfa_frag_add(&f, idx, 0)) { c->error = NFA_ERR_ALLOC; return empty; }
            return f;
        }
        if (k == 1)
            return _nfa_compile_node(c, ast->alt.children[0]);

        /* Compile all branches */
        _nfa_frag* frags = (_nfa_frag*)_nfa_tmp_alloc(c->arena, k * sizeof(_nfa_frag));
        if (!frags) { c->error = NFA_ERR_ALLOC; return empty; }
        for (size_t i = 0; i < k; i++) {
            frags[i] = _nfa_compile_node(c, ast->alt.children[i]);
            if (c->error) {
                for (size_t j = 0; j <= i; j++) _nfa_frag_free(&frags[j]);
                _nfa_tmp_free(c->arena, frags);
                return empty;
            }
        }

        /* Emit k-1 SPLIT instructions */
        size_t first_split = c->len;
        for (size_t i = 0; i < k - 1; i++) {
            _nfa_emit_inst(c, NFA_INST_SPLIT);
            if (c->error) {
                for (size_t j = 0; j < k; j++) _nfa_frag_free(&frags[j]);
                _nfa_tmp_free(c->arena, frags);
                return empty;
            }
        }

        /* Wire SPLITs: out → branch[i].start, out1 → next SPLIT or last branch */
        for (size_t i = 0; i < k - 1; i++) {
            size_t sidx = first_split + i;
            c->code[sidx].out = frags[i].start;
            c->code[sidx].out1 = (i + 1 < k - 1)
                ? first_split + i + 1
                : frags[k - 1].start;
        }

        /* Collect all patches from all branches */
        _nfa_frag result = _nfa_frag_make(first_split, c->arena);
        for (size_t i = 0; i < k; i++) {
            if (!_nfa_frag_merge(&result, &frags[i])) {
                c->error = NFA_ERR_ALLOC;
                _nfa_frag_free(&result);
                for (size_t j = 0; j < k; j++) _nfa_frag_free(&frags[j]);
                _nfa_tmp_free(c->arena, frags);
                return empty;
            }
            _nfa_frag_free(&frags[i]);
        }
        _nfa_tmp_free(c->arena, frags);
        return result;
    }

    case NFA_AST_REPEAT: {
        int min   = ast->repeat.min;
        int max   = ast->repeat.max;
        bool greedy = ast->repeat.greedy;

        if (min == 0 && max == -1) {
            /* a* : body → loop via SPLIT + JMP */
            _nfa_frag body = _nfa_compile_node(c, ast->repeat.child);
            if (c->error) { _nfa_frag_free(&body); return empty; }

            size_t sidx = _nfa_emit_inst(c, NFA_INST_SPLIT);
            if (c->error) { _nfa_frag_free(&body); return empty; }
            size_t jidx = _nfa_emit_inst(c, NFA_INST_JMP);
            if (c->error) { _nfa_frag_free(&body); return empty; }
            c->code[jidx].out = sidx; /* JMP loops back to SPLIT */

            if (greedy) {
                c->code[sidx].out = body.start; /* try body first */
                /* out1 = exit (patched later) */
            } else {
                /* out = exit (patched later) */
                c->code[sidx].out1 = body.start; /* try body second */
            }

            _nfa_frag_patch(c, &body, jidx); /* body exits → JMP */
            _nfa_frag_free(&body);

            _nfa_frag result = _nfa_frag_make(sidx, c->arena);
            if (!_nfa_frag_add(&result, sidx, greedy ? 1 : 0))
                { c->error = NFA_ERR_ALLOC; return empty; }
            return result;
        }

        if (min == 1 && max == -1) {
            /* a+ : body → SPLIT back or exit */
            _nfa_frag body = _nfa_compile_node(c, ast->repeat.child);
            if (c->error) { _nfa_frag_free(&body); return empty; }

            size_t body_start = body.start;
            size_t sidx = _nfa_emit_inst(c, NFA_INST_SPLIT);
            if (c->error) { _nfa_frag_free(&body); return empty; }

            if (greedy) {
                c->code[sidx].out = body_start; /* try body again */
                /* out1 = exit */
            } else {
                /* out = exit */
                c->code[sidx].out1 = body_start; /* try body again */
            }

            _nfa_frag_patch(c, &body, sidx); /* body exits → SPLIT */
            _nfa_frag_free(&body);

            _nfa_frag result = _nfa_frag_make(body_start, c->arena);
            if (!_nfa_frag_add(&result, sidx, greedy ? 1 : 0))
                { c->error = NFA_ERR_ALLOC; return empty; }
            return result;
        }

        if (min == 0 && max == 1) {
            /* a? : SPLIT → body or exit */
            _nfa_frag body = _nfa_compile_node(c, ast->repeat.child);
            if (c->error) { _nfa_frag_free(&body); return empty; }

            size_t sidx = _nfa_emit_inst(c, NFA_INST_SPLIT);
            if (c->error) { _nfa_frag_free(&body); return empty; }

            if (greedy) {
                c->code[sidx].out = body.start; /* try body first */
                /* out1 = exit */
            } else {
                /* out = exit */
                c->code[sidx].out1 = body.start; /* try body second */
            }

            _nfa_frag result = _nfa_frag_make(sidx, c->arena);
            /* Both body's patches and SPLIT's exit go to the same next thing */
            if (!_nfa_frag_merge(&result, &body)) {
                c->error = NFA_ERR_ALLOC;
                _nfa_frag_free(&body);
                _nfa_frag_free(&result);
                return empty;
            }
            _nfa_frag_free(&body);
            if (!_nfa_frag_add(&result, sidx, greedy ? 1 : 0))
                { c->error = NFA_ERR_ALLOC; _nfa_frag_free(&result); return empty; }
            return result;
        }

        /* General counted repetition: {n}, {n,m}, {n,} */
        {
            /* {0,0}: match empty string */
            if (min == 0 && max == 0) {
                size_t jidx = _nfa_emit_inst(c, NFA_INST_JMP);
                if (c->error) return empty;
                _nfa_frag f = _nfa_frag_make(jidx, c->arena);
                if (!_nfa_frag_add(&f, jidx, 0))
                    { c->error = NFA_ERR_ALLOC; return empty; }
                return f;
            }

            /* Step 1: emit min mandatory copies */
            _nfa_frag mand = _nfa_frag_make(0, c->arena);
            bool has_mand = false;
            size_t fstart = 0;

            for (int i = 0; i < min; i++) {
                _nfa_frag body = _nfa_compile_node(c, ast->repeat.child);
                if (c->error) {
                    if (has_mand) _nfa_frag_free(&mand);
                    _nfa_frag_free(&body);
                    return empty;
                }
                if (!has_mand) {
                    mand = body;
                    fstart = body.start;
                    has_mand = true;
                } else {
                    _nfa_frag_patch(c, &mand, body.start);
                    _nfa_tmp_free(c->arena, mand.patches);
                    mand.patches = body.patches;
                    mand.pcount  = body.pcount;
                    mand.pcap    = body.pcap;
                }
            }

            /* Exact count: no tail */
            if (max == min) {
                return mand;
            }

            /* Unbounded: *-style loop after mandatory copies */
            if (max == -1) {
                _nfa_frag body = _nfa_compile_node(c, ast->repeat.child);
                if (c->error) {
                    if (has_mand) _nfa_frag_free(&mand);
                    _nfa_frag_free(&body);
                    return empty;
                }
                size_t sidx = _nfa_emit_inst(c, NFA_INST_SPLIT);
                if (c->error) {
                    if (has_mand) _nfa_frag_free(&mand);
                    _nfa_frag_free(&body);
                    return empty;
                }
                size_t jidx = _nfa_emit_inst(c, NFA_INST_JMP);
                if (c->error) {
                    if (has_mand) _nfa_frag_free(&mand);
                    _nfa_frag_free(&body);
                    return empty;
                }
                c->code[jidx].out = sidx;
                if (greedy) {
                    c->code[sidx].out = body.start;
                } else {
                    c->code[sidx].out1 = body.start;
                }
                _nfa_frag_patch(c, &body, jidx);
                _nfa_frag_free(&body);

                _nfa_frag res;
                if (has_mand) {
                    _nfa_frag_patch(c, &mand, sidx);
                    _nfa_frag_free(&mand);
                    res = _nfa_frag_make(fstart, c->arena);
                } else {
                    res = _nfa_frag_make(sidx, c->arena);
                }
                if (!_nfa_frag_add(&res, sidx, greedy ? 1 : 0))
                    { c->error = NFA_ERR_ALLOC; _nfa_frag_free(&res); return empty; }
                return res;
            }

            /* Bounded optional: (max-min) SPLIT-wrapped copies */
            {
                int opt = max - min;
                _nfa_frag chain = mand;
                bool has_chain = has_mand;
                _nfa_frag exits = _nfa_frag_make(fstart, c->arena);

                for (int i = 0; i < opt; i++) {
                    size_t sidx = _nfa_emit_inst(c, NFA_INST_SPLIT);
                    if (c->error) {
                        if (has_chain) _nfa_frag_free(&chain);
                        _nfa_frag_free(&exits);
                        return empty;
                    }
                    _nfa_frag body = _nfa_compile_node(c, ast->repeat.child);
                    if (c->error) {
                        if (has_chain) _nfa_frag_free(&chain);
                        _nfa_frag_free(&body);
                        _nfa_frag_free(&exits);
                        return empty;
                    }
                    if (greedy) {
                        c->code[sidx].out = body.start;
                    } else {
                        c->code[sidx].out1 = body.start;
                    }
                    if (has_chain) {
                        _nfa_frag_patch(c, &chain, sidx);
                        _nfa_frag_free(&chain);
                    } else {
                        fstart = sidx;
                        exits.start = sidx;
                    }
                    if (!_nfa_frag_add(&exits, sidx, greedy ? 1 : 0)) {
                        c->error = NFA_ERR_ALLOC;
                        _nfa_frag_free(&body);
                        _nfa_frag_free(&exits);
                        return empty;
                    }
                    chain = body;
                    has_chain = true;
                }

                /* Last body's exits → final */
                if (has_chain) {
                    if (!_nfa_frag_merge(&exits, &chain)) {
                        c->error = NFA_ERR_ALLOC;
                        _nfa_frag_free(&chain);
                        _nfa_frag_free(&exits);
                        return empty;
                    }
                    _nfa_frag_free(&chain);
                }
                exits.start = fstart;
                return exits;
            }
        }
    }

    case NFA_AST_GROUP: {
        size_t open_slot  = (size_t)(2 * ast->group.index);
        size_t close_slot = (size_t)(2 * ast->group.index + 1);

        size_t open_idx = _nfa_emit_inst(c, NFA_INST_SAVE);
        if (c->error) return empty;
        c->code[open_idx].slot = open_slot;

        _nfa_frag body = _nfa_compile_node(c, ast->group.child);
        if (c->error) { _nfa_frag_free(&body); return empty; }

        size_t close_idx = _nfa_emit_inst(c, NFA_INST_SAVE);
        if (c->error) { _nfa_frag_free(&body); return empty; }
        c->code[close_idx].slot = close_slot;

        /* Wire: open → body, body → close */
        c->code[open_idx].out = body.start;
        _nfa_frag_patch(c, &body, close_idx);
        _nfa_frag_free(&body);

        _nfa_frag result = _nfa_frag_make(open_idx, c->arena);
        if (!_nfa_frag_add(&result, close_idx, 0))
            { c->error = NFA_ERR_ALLOC; return empty; }
        return result;
    }

    default:
        return empty;
    }
}

/* ── Public compiler API ────────────────────────────────────────────── */

static void nfa_program_free(nfa_program* prog) {
    if (!prog) return;
    NFA_FREE(prog->code);
    NFA_FREE(prog);
}

static int nfa_compile(nfa_ast* ast, nfa_program** out_prog) {
    *out_prog = NULL;

    _nfa_compiler c = { NULL, 0, 0, NFA_OK, NULL };

    _nfa_frag f = _nfa_compile_node(&c, ast);
    if (c.error) {
        _nfa_frag_free(&f);
        NFA_FREE(c.code);
        return c.error;
    }

    /* Emit MATCH at the end */
    size_t match_idx = _nfa_emit_inst(&c, NFA_INST_MATCH);
    if (c.error) {
        _nfa_frag_free(&f);
        NFA_FREE(c.code);
        return c.error;
    }

    /* Patch all exits to MATCH */
    size_t start = f.start;
    _nfa_frag_patch(&c, &f, match_idx);
    _nfa_frag_free(&f);

    /* Build program */
    nfa_program* prog = (nfa_program*)NFA_MALLOC(sizeof(nfa_program));
    if (!prog) {
        NFA_FREE(c.code);
        return NFA_ERR_ALLOC;
    }
    prog->code         = c.code;
    prog->len          = c.len;
    prog->start        = start;
    prog->n_captures   = (size_t)(2 * _nfa_count_groups(ast));
    prog->greedy_match = _nfa_all_greedy(ast);
    prog->flags        = 0;

    *out_prog = prog;
    return NFA_OK;
}

/* ── Arena-backed parse+compile pipeline ─────────────────────────────── */

static int nfa_compile_pattern(const uint8_t* pattern, size_t len, nfa_program** out_prog) {
    *out_prog = NULL;
    arena_t arena = {0};

    /* Parse with arena */
    _nfa_parser p = { .pat = pattern, .len = len, .pos = 0,
                      .group_count = 0, .error = NFA_OK, .flags = 0,
                      .arena = &arena };

    nfa_ast* ast = _nfa_parse_regex(&p);

    if (p.error) {
        arena_reset(&arena);
        return p.error;
    }

    if (p.pos < p.len && p.pat[p.pos] == ')') {
        arena_reset(&arena);
        return NFA_ERR_UNMATCHED_RPAREN;
    }

    if (!ast) {
        ast = nfa_ast_concat(NULL, 0, &arena);
        if (!ast) {
            arena_reset(&arena);
            return NFA_ERR_ALLOC;
        }
    }

    /* Compile with arena */
    _nfa_compiler c = { NULL, 0, 0, NFA_OK, &arena };

    _nfa_frag f = _nfa_compile_node(&c, ast);
    if (c.error) {
        arena_reset(&arena);
        return c.error;
    }

    /* Emit MATCH at the end */
    size_t match_idx = _nfa_emit_inst(&c, NFA_INST_MATCH);
    if (c.error) {
        arena_reset(&arena);
        return c.error;
    }

    /* Patch all exits to MATCH */
    size_t start = f.start;
    _nfa_frag_patch(&c, &f, match_idx);

    /* Copy code array to heap (outlives arena) */
    nfa_inst* heap_code = (nfa_inst*)NFA_MALLOC(c.len * sizeof(nfa_inst));
    if (!heap_code) {
        arena_reset(&arena);
        return NFA_ERR_ALLOC;
    }
    memcpy(heap_code, c.code, c.len * sizeof(nfa_inst));

    /* Gather AST metadata before arena reset */
    size_t n_captures = (size_t)(2 * _nfa_count_groups(ast));
    bool greedy_match = _nfa_all_greedy(ast);

    /* Build program on heap */
    nfa_program* prog = (nfa_program*)NFA_MALLOC(sizeof(nfa_program));
    if (!prog) {
        NFA_FREE(heap_code);
        arena_reset(&arena);
        return NFA_ERR_ALLOC;
    }
    prog->code         = heap_code;
    prog->len          = c.len;
    prog->start        = start;
    prog->n_captures   = n_captures;
    prog->greedy_match = greedy_match;
    prog->flags        = 0;

    /* Free all temporary data */
    arena_reset(&arena);

    *out_prog = prog;
    return NFA_OK;
}

/* ── Chunk-based input abstraction ──────────────────────────────────── */

typedef struct {
    const uint8_t* data;
    size_t         len;   /* 0 means EOF */
} nfa_chunk;

struct nfa_input {
    nfa_chunk (*next_chunk)(void* ctx);
    size_t    (*byte_offset)(void* ctx);
    void      (*reset)(void* ctx);
    void      (*seek)(void* ctx, size_t offset);
    void*     ctx;
};

/* Free the context allocated by an nfa_input constructor. */
static void nfa_input_free(nfa_input* inp) {
    if (inp && inp->ctx) {
        NFA_FREE(inp->ctx);
        inp->ctx = NULL;
    }
}

/* ── Buffer adapter ────────────────────────────────────────────────── */

typedef struct {
    const uint8_t* data;
    size_t         len;
    size_t         pos;
    size_t         chunk_start;
    bool           returned;
} _nfa_buf_ctx;

static nfa_chunk _nfa_buf_next_chunk(void* ctx) {
    _nfa_buf_ctx* b = (_nfa_buf_ctx*)ctx;
    if (b->returned) return (nfa_chunk){ NULL, 0 };
    b->returned = true;
    b->chunk_start = b->pos;
    size_t remain = b->pos < b->len ? b->len - b->pos : 0;
    return (nfa_chunk){ b->data + b->pos, remain };
}

static size_t _nfa_buf_byte_offset(void* ctx) {
    _nfa_buf_ctx* b = (_nfa_buf_ctx*)ctx;
    return b->chunk_start;
}

static void _nfa_buf_reset(void* ctx) {
    _nfa_buf_ctx* b = (_nfa_buf_ctx*)ctx;
    b->pos = 0;
    b->chunk_start = 0;
    b->returned = false;
}

static void _nfa_buf_seek(void* ctx, size_t offset) {
    _nfa_buf_ctx* b = (_nfa_buf_ctx*)ctx;
    b->pos = offset;
    b->chunk_start = offset;
    b->returned = false;
}

static nfa_input nfa_input_from_buf(const uint8_t* data, size_t len) {
    nfa_input inp = { NULL, NULL, NULL, NULL, NULL };
    _nfa_buf_ctx* ctx = (_nfa_buf_ctx*)NFA_MALLOC(sizeof(_nfa_buf_ctx));
    if (!ctx) return inp;
    ctx->data = data;
    ctx->len = len;
    ctx->pos = 0;
    ctx->chunk_start = 0;
    ctx->returned = false;
    inp.next_chunk  = _nfa_buf_next_chunk;
    inp.byte_offset = _nfa_buf_byte_offset;
    inp.reset       = _nfa_buf_reset;
    inp.seek        = _nfa_buf_seek;
    inp.ctx         = ctx;
    return inp;
}

/* ── Arena allocator for per-thread capture arrays ──────────────────── */

/* ── NFA simulator ──────────────────────────────────────────────────── */

/* Thread: program counter + starting byte position + capture slots */
typedef struct {
    size_t  pc;
    size_t  start;
    size_t* captures; /* arena-allocated, n_captures slots (or NULL) */
} _nfa_thread;

/* Thread list with seen bitset for O(1) deduplication */
typedef struct {
    _nfa_thread* threads;
    size_t       count;
    uint8_t*     seen;   /* bitset indexed by pc */
    size_t       n;      /* program length */
} _nfa_tlist;

static void _nfa_tlist_clear(_nfa_tlist* l) {
    l->count = 0;
    memset(l->seen, 0, (l->n + 7) / 8);
}

/* Add state to thread list, recursively following epsilon transitions.
 * bol/eol describe line-anchor context; wb is true at word boundaries.
 * arena/captures/n_captures/sp support capture group extraction:
 *   arena     — arena allocator for capture arrays (may be NULL if n_captures==0)
 *   captures  — current thread's capture slot array (or NULL)
 *   n_captures — number of capture slots (2 * group_count)
 *   sp        — current byte position (recorded by SAVE instructions) */
static void _nfa_addstate(_nfa_tlist* l, nfa_inst* code,
                           size_t pc, size_t start,
                           bool bol, bool eol, bool wb,
                           arena_t* arena, size_t* captures,
                           size_t n_captures, size_t sp) {
    if (l->seen[pc / 8] & (1 << (pc % 8))) return;
    l->seen[pc / 8] |= (uint8_t)(1 << (pc % 8));
    switch (code[pc].type) {
    case NFA_INST_SPLIT:
        _nfa_addstate(l, code, code[pc].out,  start, bol, eol, wb,
                      arena, captures, n_captures, sp);
        _nfa_addstate(l, code, code[pc].out1, start, bol, eol, wb,
                      arena, captures, n_captures, sp);
        return;
    case NFA_INST_JMP:
        _nfa_addstate(l, code, code[pc].out, start, bol, eol, wb,
                      arena, captures, n_captures, sp);
        return;
    case NFA_INST_SAVE: {
        size_t* new_caps = captures;
        if (n_captures > 0) {
            new_caps = (size_t*)arena_alloc(arena, n_captures * sizeof(size_t));
            if (new_caps) {
                if (captures)
                    memcpy(new_caps, captures, n_captures * sizeof(size_t));
                else
                    memset(new_caps, 0xFF, n_captures * sizeof(size_t)); /* SIZE_MAX */
                size_t slot = code[pc].slot;
                if (slot < n_captures)
                    new_caps[slot] = sp;
            }
        }
        _nfa_addstate(l, code, code[pc].out, start, bol, eol, wb,
                      arena, new_caps, n_captures, sp);
        return;
    }
    case NFA_INST_ANCHOR_BOL:
        if (bol) _nfa_addstate(l, code, code[pc].out, start, bol, eol, wb,
                               arena, captures, n_captures, sp);
        return;
    case NFA_INST_ANCHOR_EOL:
        if (eol) _nfa_addstate(l, code, code[pc].out, start, bol, eol, wb,
                               arena, captures, n_captures, sp);
        return;
    case NFA_INST_ANCHOR_WORD:
        if (wb) _nfa_addstate(l, code, code[pc].out, start, bol, eol, wb,
                              arena, captures, n_captures, sp);
        return;
    case NFA_INST_ANCHOR_NON_WORD:
        if (!wb) _nfa_addstate(l, code, code[pc].out, start, bol, eol, wb,
                               arena, captures, n_captures, sp);
        return;
    case NFA_INST_LITERAL:
    case NFA_INST_RANGE:
    case NFA_INST_ANY:
    case NFA_INST_ANY_ALL:
    case NFA_INST_MATCH:
        l->threads[l->count++] = (_nfa_thread){ pc, start, captures };
        return;
    }
}

/* Core: execute NFA from a given byte offset using pre-allocated buffers.
 * cbuf/cseen/pending must be sized for prog->len.
 * arena is used for per-thread capture slot arrays (may be NULL if n_captures==0). */
static nfa_match _nfa_exec_core(nfa_program* prog, nfa_input* input, size_t from,
                                 _nfa_thread* cbuf, uint8_t* cseen,
                                 _nfa_thread* pending, arena_t* arena) {
    nfa_match result = { false, 0, 0, NULL, 0 };
    size_t ncap = prog->n_captures;

    size_t n       = prog->len;
    _nfa_tlist clist = { cbuf, 0, cseen, n };
    memset(cseen, 0, (n + 7) / 8);

    /* Determine prev_b/has_prev for anchor context at starting position */
    uint8_t  prev_b   = 0;
    bool     has_prev = false;
    if (from > 0) {
        input->seek(input->ctx, from - 1);
        nfa_chunk peek = input->next_chunk(input->ctx);
        if (peek.len > 0) {
            prev_b   = peek.data[0];
            has_prev = true;
        } else {
            return result;
        }
    }

    /* Seek to from and fetch first chunk */
    input->seek(input->ctx, from);
    nfa_chunk chunk = input->next_chunk(input->ctx);
    size_t ci = 0;

    size_t   sp       = from;
    size_t   pcount   = 0;
    bool     matched  = false;
    size_t   m_start  = 0;
    size_t   m_end    = 0;
    size_t*  m_caps   = NULL; /* winning thread's captures (arena ptr) */

    for (;;) {
        /* Advance past exhausted chunks */
        bool at_end = false;
        while (ci >= chunk.len) {
            chunk = input->next_chunk(input->ctx);
            if (chunk.len == 0) { at_end = true; break; }
            ci = 0;
        }

        uint8_t cur = at_end ? 0 : chunk.data[ci];

        /* Anchor context at position sp */
        bool bol = (!has_prev || prev_b == '\n');
        bool eol = (at_end   || cur == '\n');

        /* Word boundary: prev_is_word XOR cur_is_word */
        bool prev_word = has_prev && _nfa_is_word_char(prev_b);
        bool cur_word  = !at_end  && _nfa_is_word_char(cur);
        bool wb = (prev_word != cur_word);

        /* Build clist: expand pending from previous step, then add start.
         * Pending threads are expanded first so earlier-starting threads
         * have priority (their PCs claim the seen-set slots). */
        _nfa_tlist_clear(&clist);
        for (size_t i = 0; i < pcount; i++)
            _nfa_addstate(&clist, prog->code, pending[i].pc,
                          pending[i].start, bol, eol, wb,
                          arena, pending[i].captures, ncap, sp);
        if (!matched)
            _nfa_addstate(&clist, prog->code, prog->start, sp, bol, eol, wb,
                          arena, NULL, ncap, sp);

        /* Scan for MATCH — update best.
         * Greedy:  leftmost start, then longest end.
         * Non-greedy (lazy): leftmost start, then shortest end. */
        for (size_t i = 0; i < clist.count; i++) {
            if (prog->code[clist.threads[i].pc].type == NFA_INST_MATCH) {
                size_t s = clist.threads[i].start;
                if (!matched || s < m_start ||
                    (s == m_start && sp > m_end && prog->greedy_match)) {
                    matched = true;
                    m_start = s;
                    m_end   = sp;
                    m_caps  = clist.threads[i].captures;
                }
            }
        }

        if (at_end) break;

        /* Step: consume current byte, build pending for next position */
        pcount = 0;
        for (size_t i = 0; i < clist.count; i++) {
            nfa_inst* inst = &prog->code[clist.threads[i].pc];
            bool m = false;
            switch (inst->type) {
            case NFA_INST_LITERAL: m = (cur == inst->byte);                    break;
            case NFA_INST_RANGE:   m = (cur >= inst->lo && cur <= inst->hi);   break;
            case NFA_INST_ANY:     m = (cur != '\n');                           break;
            case NFA_INST_ANY_ALL: m = true;                                   break;
            default: break;
            }
            if (m)
                pending[pcount++] = (_nfa_thread){
                    inst->out, clist.threads[i].start, clist.threads[i].captures
                };
        }

        /* Early exit: matched and no surviving threads */
        if (matched && pcount == 0) break;

        prev_b   = cur;
        has_prev = true;
        ci++;
        sp++;
    }

    if (matched) {
        result.matched    = true;
        result.start      = m_start;
        result.end        = m_end;
        result.n_captures = ncap;
        /* Copy winning captures to heap memory (outlives arena) */
        if (ncap > 0) {
            result.captures = (size_t*)NFA_MALLOC(ncap * sizeof(size_t));
            if (result.captures) {
                if (m_caps)
                    memcpy(result.captures, m_caps, ncap * sizeof(size_t));
                else
                    memset(result.captures, 0xFF, ncap * sizeof(size_t)); /* SIZE_MAX */
            }
        }
    }

    return result;
}

/* Internal: execute NFA from a given byte offset (allocates its own buffers).
 * Returns the first (leftmost, then longest) match starting at or after 'from'. */
static nfa_match _nfa_exec_from(nfa_program* prog, nfa_input* input, size_t from) {
    nfa_match result = { false, 0, 0, NULL, 0 };

    size_t n       = prog->len;
    size_t seen_sz = (n + 7) / 8;
    size_t tsz     = n * sizeof(_nfa_thread);

    _nfa_thread* cbuf    = (_nfa_thread*)NFA_MALLOC(tsz);
    uint8_t*     cseen   = (uint8_t*)NFA_MALLOC(seen_sz);
    _nfa_thread* pending = (_nfa_thread*)NFA_MALLOC(tsz);
    if (!cbuf || !cseen || !pending) {
        NFA_FREE(cbuf); NFA_FREE(cseen); NFA_FREE(pending);
        return result;
    }

    arena_t arena = {0};
    result = _nfa_exec_core(prog, input, from, cbuf, cseen, pending, &arena);
    arena_reset(&arena);

    NFA_FREE(cbuf);
    NFA_FREE(cseen);
    NFA_FREE(pending);
    return result;
}

/* Execute NFA program against chunk-based input.
 * Returns the first (leftmost, then longest) match, or matched=false.
 * Uses Thompson's multi-state simulation: all active states are tracked
 * simultaneously via two state sets swapped each byte. */
static nfa_match nfa_exec(nfa_program* prog, nfa_input* input) {
    if (!prog || !input || !input->ctx || prog->len == 0)
        return (nfa_match){ false, 0, 0, NULL, 0 };
    input->reset(input->ctx);
    return _nfa_exec_from(prog, input, 0);
}

/* ── Match iterator ─────────────────────────────────────────────────── */

typedef struct nfa_iter nfa_iter;
struct nfa_iter {
    nfa_program* prog;
    nfa_input    input;
    size_t       pos;
    bool         done;
    _nfa_thread* cbuf;     /* pre-allocated thread list */
    uint8_t*     cseen;    /* pre-allocated dedup bitset */
    _nfa_thread* pending;  /* pre-allocated pending array */
    arena_t      arena;    /* arena for per-thread capture arrays */
};

static nfa_iter nfa_iter_new(nfa_program* prog, nfa_input input) {
    size_t n   = prog->len;
    size_t tsz = n * sizeof(_nfa_thread);
    nfa_iter it;
    memset(&it, 0, sizeof(it));
    it.prog    = prog;
    it.input   = input;
    it.pos     = 0;
    it.done    = false;
    it.cbuf    = (_nfa_thread*)NFA_MALLOC(tsz);
    it.cseen   = (uint8_t*)NFA_MALLOC((n + 7) / 8);
    it.pending = (_nfa_thread*)NFA_MALLOC(tsz);
    if (!it.cbuf || !it.cseen || !it.pending) {
        NFA_FREE(it.cbuf); NFA_FREE(it.cseen); NFA_FREE(it.pending);
        it.cbuf = NULL; it.cseen = NULL; it.pending = NULL;
        it.done = true;
    }
    return it;
}

static nfa_match nfa_iter_next(nfa_iter* it) {
    nfa_match result = { false, 0, 0, NULL, 0 };
    if (it->done) return result;

    arena_reset(&it->arena); /* reset arena between calls */
    result = _nfa_exec_core(it->prog, &it->input, it->pos,
                            it->cbuf, it->cseen, it->pending, &it->arena);

    if (result.matched) {
        if (result.end > result.start)
            it->pos = result.end;
        else
            it->pos = result.end + 1; /* advance past zero-length match */
    } else {
        it->done = true;
    }

    return result;
}

static void nfa_iter_free(nfa_iter* it) {
    NFA_FREE(it->cbuf);
    NFA_FREE(it->cseen);
    NFA_FREE(it->pending);
    it->cbuf = NULL; it->cseen = NULL; it->pending = NULL;
    arena_reset(&it->arena);
    nfa_input_free(&it->input);
}

#endif /* NFA_H */
