/*
 * JACL — Public header for libjacl.a
 *
 * Include this file in test and application code that links against libjacl.a.
 * All types and function declarations mirror the unity build (jacl.c).
 *
 * This file is hand-maintained. When adding new types or functions to the
 * source files, add corresponding declarations here.
 */
#ifndef JACL_H
#define JACL_H

/* ========================================================================
 * System headers
 * ======================================================================== */

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * Infrastructure (single-header libraries, header-only mode)
 * ======================================================================== */

#define ARENA_IMPLEMENTATION
#include "../lib/arena/arena.h"
#include "../lib/platform/platform.h"
#include "../lib/unicode/unicode.h"

/* ========================================================================
 * Types — value.c
 * ======================================================================== */

typedef uint64_t JaclVal;

#define JACL_TAG_SHIFT      56
#define JACL_TAG_MASK       ((uint64_t)0xFF << JACL_TAG_SHIFT)
#define JACL_PAYLOAD_MASK   ((UINT64_C(1) << JACL_TAG_SHIFT) - 1)

/* Pack a pointer payload into a tagged JaclVal. The debug-only assert
 * catches a pointer wider than the 56-bit payload — without it, the
 * mask would silently truncate high bits into the tag. The mask itself
 * stays for release-build defense; with the assert holding, it's a
 * no-op on every supported platform today (48-bit user-space VA).
 *
 * Note: cast to uint64_t *before* the shift — on wasm32 / other 32-bit
 * targets, uintptr_t is 32 bits, so a >> 56 on uintptr_t is UB. */
#define JACL_PACK_PTR(tag, p)                                                 \
    (assert(((uint64_t)(uintptr_t)(p) >> JACL_TAG_SHIFT) == 0),               \
     (tag) | ((uint64_t)(uintptr_t)(p) & JACL_PAYLOAD_MASK))

#define JACL_FLAG_TAINTED   ((uint64_t)1 << 63)
#define JACL_FLAG_SECRET    ((uint64_t)1 << 62)
#define JACL_FLAG_ERROR     ((uint64_t)1 << 61)
#define JACL_FLAGS_MASK     (JACL_FLAG_TAINTED | JACL_FLAG_SECRET | JACL_FLAG_ERROR)

#define JACL_TYPE_MASK      ((uint64_t)0x1F << JACL_TAG_SHIFT)

#define JACL_TAG_NIL            ((uint64_t)0x00 << JACL_TAG_SHIFT)
#define JACL_TAG_BOOL           ((uint64_t)0x01 << JACL_TAG_SHIFT)
#define JACL_TAG_I32            ((uint64_t)0x02 << JACL_TAG_SHIFT)
#define JACL_TAG_F32            ((uint64_t)0x03 << JACL_TAG_SHIFT)
#define JACL_TAG_INLINE_STRING  ((uint64_t)0x04 << JACL_TAG_SHIFT)
#define JACL_TAG_STRING         ((uint64_t)0x05 << JACL_TAG_SHIFT)
#define JACL_TAG_VECTOR         ((uint64_t)0x06 << JACL_TAG_SHIFT)
#define JACL_TAG_MAP            ((uint64_t)0x07 << JACL_TAG_SHIFT)
#define JACL_TAG_CLOSURE        ((uint64_t)0x08 << JACL_TAG_SHIFT)
#define JACL_TAG_BIGNUM         ((uint64_t)0x09 << JACL_TAG_SHIFT)
#define JACL_TAG_CELL           ((uint64_t)0x0A << JACL_TAG_SHIFT)
#define JACL_TAG_BOX            ((uint64_t)0x0B << JACL_TAG_SHIFT)
#define JACL_TAG_ATOM           ((uint64_t)0x0C << JACL_TAG_SHIFT)
#define JACL_TAG_U32            ((uint64_t)0x0D << JACL_TAG_SHIFT)
#define JACL_TAG_I64            ((uint64_t)0x0E << JACL_TAG_SHIFT)
#define JACL_TAG_U64            ((uint64_t)0x0F << JACL_TAG_SHIFT)
#define JACL_TAG_F64            ((uint64_t)0x10 << JACL_TAG_SHIFT)
#define JACL_TAG_FUTURE         ((uint64_t)0x11 << JACL_TAG_SHIFT)
#define JACL_TAG_STRUCT         ((uint64_t)0x12 << JACL_TAG_SHIFT)
#define JACL_TAG_NATIVE_FN     ((uint64_t)0x13 << JACL_TAG_SHIFT)
#define JACL_TAG_ROPE_STRING   ((uint64_t)0x14 << JACL_TAG_SHIFT)
#define JACL_TAG_STREAM        ((uint64_t)0x15 << JACL_TAG_SHIFT)
#define JACL_TAG_STATE_MACHINE ((uint64_t)0x16 << JACL_TAG_SHIFT)
#define JACL_TAG_SYNTAX        ((uint64_t)0x17 << JACL_TAG_SHIFT)
#define JACL_TAG_TYPED_VECTOR  ((uint64_t)0x18 << JACL_TAG_SHIFT)
#define JACL_TAG_TYPED_MAP     ((uint64_t)0x19 << JACL_TAG_SHIFT)
#define JACL_TAG_ARR           ((uint64_t)0x1A << JACL_TAG_SHIFT)

/* Bitmask of heap-managed tag indices (after >> JACL_TAG_SHIFT). Used by
 * jacl_is_heap_type for an O(1) predicate instead of an 18-way `||` chain
 * — gc_remembered_set_barrier sits on the hot path of every mutable-cell
 * write. Bits set: STRING(0x05) .. ATOM(0x0C), I64(0x0E) .. STRUCT(0x12),
 * ROPE_STRING(0x14), STATE_MACHINE(0x16) .. TYPED_MAP(0x19), ARR(0x1A). */
#define JACL_HEAP_TAG_MASK     (0x07D7DFE0u)

typedef struct { int64_t value; } JaclHeapI64;
typedef struct { uint64_t value; } JaclHeapU64;
typedef struct { double value; } JaclHeapF64;

#define JACL_NIL    ((JaclVal)JACL_TAG_NIL)
#define JACL_TRUE   ((JaclVal)(JACL_TAG_BOOL | UINT64_C(1)))
#define JACL_FALSE  ((JaclVal)(JACL_TAG_BOOL))

typedef struct {
    uint32_t type_idx;    /* 0 = plain JaclVal box, >0 = struct type index */
    uint32_t total_size;  /* size of data[] in bytes */
    uint8_t  data[];      /* flexible array: JaclVal (8 bytes) for type_idx==0, raw struct bytes for type_idx>0 */
} JaclMutableRef;

/* Access the JaclVal stored in a type_idx==0 MutableRef's data[] */
#define MREF_VAL(ref) (*(JaclVal*)(ref)->data)

/* --- Atom watchers ---
 * Atoms are allocated with an OBJ_ATOM_REF tag and an extra pointer slot
 * after MREF_VAL for the watcher list (NULL until first [watch ...]).
 * The pointer lives at data[sizeof(JaclVal)..]; ATOM_WATCHERS_SLOT yields
 * an addressable `JaclWatcherList**` for atomic load/store. The list itself
 * is a separate OBJ_WATCHER_LIST allocation traced by the GC.
 *
 * The watcher list uses an inline flexible array: entries[2*i] is a key,
 * entries[2*i+1] is a fn. Growth re-allocates the whole object (the GC
 * collects the old copy once the atom's slot is overwritten and no
 * in-flight snapshot pins it). */
#define ATOM_REF_DATA_SIZE  (sizeof(JaclVal) + sizeof(void*))
typedef struct JaclWatcherList JaclWatcherList;
#define ATOM_WATCHERS_SLOT(ref) \
  ((JaclWatcherList**)((ref)->data + sizeof(JaclVal)))

struct JaclWatcherList {
    uint32_t        count;     /* number of registered (key, fn) pairs */
    uint32_t        capacity;  /* entries[] holds 2 * capacity JaclVals */
    JaclVal         entries[]; /* [k0, f0, k1, f1, ...] — immutable after publish */
};
#define WATCHER_KEY(wl, i) ((wl)->entries[2*(i)])
#define WATCHER_FN(wl, i)  ((wl)->entries[2*(i) + 1])

typedef char jacl_assert_ptr_fits_payload_[(sizeof(void *) <= sizeof(uint64_t)) ? 1 : -1];

/* ========================================================================
 * Types — gc.c
 * ======================================================================== */

typedef enum {
    OBJ_CLOSURE,
    OBJ_STRING,
    OBJ_HEAP_I64,
    OBJ_HEAP_U64,
    OBJ_HEAP_F64,
    OBJ_MUTABLE_REF,
    OBJ_BIGNUM,
    OBJ_HAMT_INTERNAL,
    OBJ_HAMT_LEAF,
    OBJ_HAMT_COLLISION,
    OBJ_RRB_INTERNAL,
    OBJ_RRB_LEAF,
    OBJ_RRB_ROOT,
    OBJ_FUTURE,
    OBJ_FUTURE_WAITER,
    OBJ_PARALLEL_AGG,
    OBJ_RACE_AGG,
    OBJ_HEAP_RECORD,
    OBJ_ROPE_STRING,
    OBJ_ROPE_LEAF,
    OBJ_ROPE_INTERNAL,
    OBJ_STREAM,
    OBJ_STATE_MACHINE,
    OBJ_SYNTAX,
    OBJ_TYPED_RRB_LEAF,     /* typed vec leaf: raw struct bytes, no GC tracing */
    OBJ_TYPED_HAMT_LEAF,    /* typed map leaf: trace dyn key only, skip struct value bytes */
    OBJ_ATOM_REF,           /* atom variant of JaclMutableRef with trailing watcher-list slot */
    OBJ_WATCHER_LIST,       /* per-atom watcher list: parallel keys/fns arrays + mutex */
    OBJ_BOX_INLINE          /* compact untyped-box: payload is the JaclVal directly (no header fields) */
} GCObjType;

#define GC_BLOCK_SIZE       65536
#define GC_LINE_SIZE        128
#define GC_LINES_PER_BLOCK  512

#ifndef GC_MAX_HEAP_BLOCKS
#define GC_MAX_HEAP_BLOCKS 4096
#endif

#define GC_LINE_FREE     0
#define GC_LINE_OCCUPIED 1

#define GC_THRESHOLD     (1024 * 1024)
#define GC_THRESHOLD_MIN (256 * 1024)
#define GC_THRESHOLD_MAX (16 * 1024 * 1024)

typedef struct {
    uint32_t epoch;
    uint8_t  mark          : 1;
    uint8_t  gen           : 1;
    uint8_t  survive_count : 2;
    uint8_t  _reserved     : 4;
    uint8_t  obj_type;
    uint16_t alloc_total;
} GCHeader;

typedef struct GCBlock {
    uint8_t         payload[GC_BLOCK_SIZE];
    uint8_t         line_map[GC_LINES_PER_BLOCK];
    struct GCBlock *next;
    /* §14 tier-2: per-block scan hint. Invariant: lines [0, first_free_line)
     * are all OCCUPIED. Updated by sweep (post line-map rebuild) and by
     * gc__find_fit_in_block on successful fit. Both writers run under
     * heap->blocks_mutex; bump-alloc fast path doesn't touch it. Lets the
     * slow-path block scan skip the long occupied prefix on aged blocks
     * (the dominant remaining cost in `gc__find_fit_in_block` after the
     * tier-1 cross-block anchor landed). */
    uint16_t        first_free_line;
    /* §14 Phase-B: largest contiguous FREE run in line_map. See gc.c
     * definition for semantics. MUST stay in sync (struct-size test). */
    uint16_t        max_free_run_lines;
} GCBlock;

typedef struct {
    GCBlock         *free_list;
    platform_mutex_t mutex;
    uint32_t         total_blocks_allocated;
    uint32_t         max_blocks;
} BlockPool;

typedef struct {
    GCBlock         *blocks;
    GCBlock         *current_block;
    uint8_t         *cursor;
    uint8_t         *limit;
    size_t           bytes_since_gc;
    size_t           gc_threshold;
    BlockPool       *pool;
    uint8_t          current_mark;
    bool             needs_gc;
    size_t           old_gen_bytes;
    size_t           last_major_old_gen_bytes;
    uint32_t         gc_cycle_count;
    /* Serializes `blocks` list mutation (owner head-insert in gc_alloc slow
     * path) with `gc_sweep_concurrent`'s walk + unlink. The bump-allocation
     * fast path doesn't touch the list. See AUDIT.md §§7, 15 and
     * SYNCHRONIZATION.md. MUST stay in sync with the definition in gc.c. */
    platform_mutex_t blocks_mutex;
    /* §14 slow-path resume anchor. When the bump-allocation fast path
     * exhausts the current free run, the slow path scans the blocks list
     * looking for another fit. Without this anchor, the scan restarts at
     * heap->blocks head every call — O(blocks × 512) of line-map churn
     * even though most blocks have already been observed full for this
     * allocation size. With this anchor, the scan resumes at the block
     * where the last fit was found; lines within that block are still
     * scanned from 0 so smaller free runs left over from larger
     * find-fit calls remain reachable. Cleared to NULL by sweep at end
     * of each GC cycle so freed runs near the list head are picked up
     * on the next pass, and on slow-path miss-through-end so we don't
     * grow the heap unnecessarily. */
    GCBlock         *search_block;
    /* Cumulative perf counters (owner is sole writer; perf-snapshot reads
     * across workers post-quiesce). Never reset by GC — bytes_since_gc is
     * the GC-trigger counter, this is the lifetime accumulator. */
    uint64_t         total_bytes_allocated;
    uint64_t         total_allocs;
    uint64_t         slow_path_allocs;
    /* §14 instrumentation; see gc.c definition for semantics. MUST
     * stay in sync with the gc.c definition (struct-size test enforces). */
    uint64_t         blocks_scanned;
    uint64_t         lines_scanned;
    uint64_t         blocks_rejected_fast;
} ThreadHeap;

/* HeapRecord — heap-allocated, pointer-accessed struct shape. Used by ctx
   (the lone builtin) and reachable from user code only via [box $val]
   (which uses JaclMutableRef instead). May contain reference fields; the
   GC traces fields via the StructTypeRegistry by type_idx. User defstructs
   are inline (raw bytes across stack slots), not HeapRecords. */
typedef struct {
    uint32_t type_idx;
    uint32_t total_size;   /* byte size of data[] (from StructTypeDef->total_size) */
    uint8_t  data[];
} HeapRecord;

typedef struct {
    JaclVal         *entries;
    uint32_t         count;
    uint32_t         cap;
    /* Serializes grey_buf_push (which may realloc entries) with the GC
     * thread's drain reads. Push is rare (only during gc_active && heap
     * value mutation), so mutex contention is bounded. See SYNCHRONIZATION.md
     * and AUDIT.md §3. MUST stay in sync with the definition in gc.c. */
    platform_mutex_t mutex;
} GreyBuffer;

typedef struct {
    JaclVal  *entries;
    uint32_t  count;
    uint32_t  cap;
} RememberedSet;

typedef struct FutureWaiter {
    JaclVal               continuation;
    struct FutureWaiter  *next;
} FutureWaiter;

typedef struct {
    uint32_t            state;       /* atomic — load/store via ATOMIC_*_EXPLICIT */
    uint64_t            result;      /* atomic */
    FutureWaiter       *waiters;
    uint32_t            lock;        /* atomic — CAS spinlock */
} JaclFuture;

#define FUTURE_PENDING      0
#define FUTURE_RESOLVED     1
#define FUTURE_ERROR        2

#define STREAM_PENDING      0
#define STREAM_CONSUMED     1
#define STREAM_EXHAUSTED    2
#define STREAM_ERROR        3
#define STREAM_KIND_GENERATOR  0
#define STREAM_KIND_FILTER     1
#define STREAM_KIND_TRANSFORM  2
#define STREAM_KIND_TAKE       3
#define STREAM_KIND_LINES      4
#define STREAM_KIND_EXEC       5
#define STREAM_KIND_EXEC_BUFFER 6
#define STREAM_KIND_EXEC_PIPE  7
#define STREAM_KIND_RANGE      8
#define STREAM_MAX_ARGS     8

typedef struct {
    uint32_t  state;
    uint32_t  kind;
    JaclVal   next_fn;
    JaclVal   cached_value;
    JaclVal   state_machine;
    JaclVal   args[STREAM_MAX_ARGS];
    uint8_t   arg_count;
    /* Element type, encoded like tvec/tarr: JACL_SCALAR_TYPE_IDX(t) for a
     * scalar element, a struct registry idx for struct elements, or
     * JACL_SCALAR_TYPE_IDX(TYPE_DYN) for an untyped [Stream dyn]. Drives
     * typed-yield value rep, GC tracing of cached_value, and consumer
     * narrowing. (Definition mirrored in gc.c — keep in sync.) */
    uint32_t  elem_idx;
    /* FILTER streams only: the rep the predicate's param expects. A wide
     * scalar enc (i64/u64/f64) means the inline predicate was monomorphized to
     * read the element wide, so the filter pull passes the element wide with no
     * box-for-call (TYPED_CLOSURES_DESIGN.md Phase A). 0xFF00 (dyn) = the
     * predicate wants a tagged value, so a wide element is boxed for the call
     * (named/dyn predicates, and all non-stream filters). */
    uint32_t  pred_elem_idx;
} JaclStream;

typedef struct {
    uint32_t          completed;     /* atomic — fetch_add for completion count */
    uint32_t          errored;       /* atomic — first-error-wins CAS */
    uint64_t          error_val;     /* atomic — set under errored CAS winner */
    uint32_t          count;
    JaclVal           state_machine;
    JaclVal           results[];
} ParallelAgg;

typedef struct {
    uint32_t          settled;       /* atomic — winner CAS */
    JaclVal           state_machine;
} RaceAgg;

typedef struct {
    uint32_t resume_point;
    uint32_t field_count;
    JaclVal  error_k;
    JaclVal  sm_closure;
    uint8_t  field_inline_bitmap[32]; /* US-014: marks which field slots hold raw struct bytes */
    JaclVal  fields[];
} JaclStateMachine;

typedef enum {
    SYNTAX_COMMAND,
    SYNTAX_LIT_INT,
    SYNTAX_LIT_FLOAT,
    SYNTAX_LIT_STRING,
    SYNTAX_VAR_REF,
    SYNTAX_BLOCK,
    SYNTAX_INTERP_STRING,
    SYNTAX_SPREAD,
    SYNTAX_USE,
    SYNTAX_DEFSTRUCT,
    SYNTAX_DEFMACRO,
    SYNTAX_QUOTE,
    SYNTAX_SYNTAX_QUOTE,
    SYNTAX_UNQUOTE,
    SYNTAX_UNQUOTE_SPLICING,
    SYNTAX_BREAK,
    SYNTAX_CONTINUE,
    SYNTAX_RETURN,
    SYNTAX_DESTRUCTURE_VEC,
    SYNTAX_DESTRUCTURE_NAMED
} SyntaxKind;

typedef struct {
    uint8_t   kind;        /* SyntaxKind */
    uint8_t   is_caret;    /* US-013: ^name — skip scope-mark stamping */
    uint8_t   is_gensym;   /* US-014: var-ref produced by gensym */
    uint32_t  pos_line;    /* source line */
    uint32_t  pos_col;     /* source column */
    uint32_t  pos_offset;  /* source offset */
    uint32_t  scope_mark;  /* hygiene: 0 = no macro context */
    union {
        struct { JaclVal head; JaclVal args; }  command;
        struct { int32_t value; }               lit_int;
        struct { float   value; }               lit_float;
        struct { JaclVal value; }               lit_string;
        struct { JaclVal name; }                var_ref;
        struct { JaclVal commands; }            block;
        struct { JaclVal segments; }            interp_string;
        struct { JaclVal child; }               spread;
        struct { JaclVal child; }               use_decl;
        struct { JaclVal child; }               defstruct;
        struct { JaclVal child; }               defmacro;
        struct { JaclVal child; }               quote;
        struct { JaclVal child; }               syntax_quote;
        struct { JaclVal child; }               unquote;
        struct { JaclVal child; }               unquote_splicing;
        struct { JaclVal value; }               break_stmt;
        struct { JaclVal value; }               return_stmt;
        struct { JaclVal names; }               destructure_vec;
        struct { JaclVal names; }               destructure_named;
    } data;
} JaclSyntax;

#define GREY_BUF_INIT_CAP   256
#define REMEMBERED_SET_INIT_CAP  256

/* Thread-local variables (defined in gc.c, string.c, runtime.c) */
extern JACL_THREAD_LOCAL uint32_t gc__thread_epoch;
extern JACL_THREAD_LOCAL ThreadHeap *gc__current_heap;
extern JACL_THREAD_LOCAL void (*gc__emergency_gc_fn)(void *ctx);
extern JACL_THREAD_LOCAL void *gc__emergency_gc_ctx;
extern JACL_THREAD_LOCAL bool gc__interning;

/* ========================================================================
 * Rope library (included after gc.c types, before string.c)
 *
 * The rope library is a template header that provides the `rope` type and
 * rope manipulation functions. In the unity build (jacl.c), it's included
 * with GC-mode macros so rope nodes are GC-allocated. Here we include it
 * the same way so tests see the same types.
 * ======================================================================== */

/* Forward-declare gc_alloc so the STREE_GC_ALLOC macro can reference it. */
extern void *gc_alloc(ThreadHeap *heap, uint8_t obj_type, size_t payload_size);

#define STREE_ALLOCATOR {libc_alloc, libc_realloc, libc_free, NULL}
#define STREE_GC_MODE
#define STREE_GC_ALLOC(t, sz)    gc_alloc(gc__current_heap, (t), (sz))
#define STREE_GC_OBJ_INTERNAL    OBJ_ROPE_INTERNAL
#define STREE_GC_OBJ_LEAF        OBJ_ROPE_LEAF
#include "../lib/sum_tree/rope.h"

/* ========================================================================
 * Types — string.c
 * ======================================================================== */

#define INTERN_INIT_CAP     16
#define INTERN_TOMBSTONE    ((JaclHeapString*)1)

typedef struct {
  uint32_t byte_len;
  uint32_t grapheme_len;
  uint32_t hash;
  char     data[];
} JaclHeapString;

typedef struct {
  JaclHeapString** entries;
  uint32_t         count;
  uint32_t         tombstone_count;
  uint32_t         cap;
  arena_t*         arena;
  platform_mutex_t lock;
} JaclInternTable;

typedef struct {
  uint32_t hash;
  rope     r;
} JaclRopeString;

/* ========================================================================
 * Types — lexer.c
 * ======================================================================== */

#define LEXER_INITIAL_CAP 256

typedef enum {
  TOKEN_LBRACKET,
  TOKEN_RBRACKET,
  TOKEN_LBRACE,
  TOKEN_RBRACE,
  TOKEN_LPAREN,
  TOKEN_RPAREN,
  TOKEN_SEMICOLON,
  TOKEN_COMMA,
  TOKEN_WORD,
  TOKEN_OPERATOR,
  TOKEN_INT,
  TOKEN_FLOAT,
  TOKEN_STRING,
  TOKEN_STRING_BEGIN,
  TOKEN_STRING_PART,
  TOKEN_STRING_END,
  TOKEN_INTERP_VAR,
  TOKEN_INTERP_EXPR_START,
  TOKEN_INTERP_EXPR_END,
  TOKEN_VAR,
  TOKEN_DOLLAR_BRACKET,
  TOKEN_DOLLAR_PAREN,
  TOKEN_USE,
  TOKEN_STRUCT,
  TOKEN_PIPE,
  TOKEN_ARROW,
  TOKEN_BACKSLASH,
  TOKEN_BANG,
  TOKEN_DOTDOT,
  TOKEN_AMP,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_NOT,
  TOKEN_TILDE_AT,
  TOKEN_PROC,
  TOKEN_DEFMACRO,
  TOKEN_QUOTE,
  TOKEN_SYNTAX_QUOTE,
  TOKEN_IF,
  TOKEN_ELIF,
  TOKEN_ELSE,
  TOKEN_WHILE,
  TOKEN_FOR,
  TOKEN_DEF,
  TOKEN_MUT,
  TOKEN_SET,
  TOKEN_MATCH,
  TOKEN_RETURN,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_TRY,
  TOKEN_CTX,
  TOKEN_EXTERN,
  TOKEN_CARET_WORD,
  TOKEN_NEWLINE,
  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  uint32_t  line;
  uint32_t  column;
  uint32_t  offset;
  uint32_t  length;
  union {
    const char* text;
    int32_t     int_val;
    float       float_val;
    const char* error_msg;
  } payload;
} Token;

typedef struct {
  Token*   tokens;
  uint32_t count;
  uint32_t error_count;
} LexResult;

typedef struct {
  Token*   tokens;
  uint32_t count;
  uint32_t cap;
  arena_t* arena;
} TokenArray;

typedef struct {
  const char* source;
  uint32_t    pos;
  uint32_t    line;
  uint32_t    col;
  arena_t*    arena;
} Lexer;

typedef struct {
  char*    data;
  uint32_t len;
  uint32_t cap;
  arena_t* arena;
} StringBuf;

/* ========================================================================
 * Types — ast.c
 * ======================================================================== */

#define AST__PP_INITIAL_CAP 256

typedef enum {
  AST_COMMAND,
  AST_LIT_INT,
  AST_LIT_FLOAT,
  AST_LIT_STRING,
  AST_VAR_REF,
  AST_BLOCK,
  AST_INTERP_STRING,
  AST_USE,
  AST_DEFSTRUCT,
  AST_DEFMACRO,
  AST_QUOTE,
  AST_SYNTAX_QUOTE,
  AST_UNQUOTE,
  AST_UNQUOTE_SPLICING,
  AST_BREAK,
  AST_CONTINUE,
  AST_RETURN,
  AST_DESTRUCTURE_VEC,
  AST_DESTRUCTURE_NAMED,
  AST_SPREAD,
  AST_SHELL_CMD,
  AST_CTX_DECL,
  AST_ERROR
} AstNodeType;

/* HeadId — interned identifier for well-known command head names.
 * See src/ast.c for the canonical definition; this header mirrors it for
 * external consumers. Keep these enums in sync. */
typedef enum {
  HEAD_NONE = 0,

  /* Operators */
  HEAD_PLUS, HEAD_MINUS, HEAD_STAR, HEAD_SLASH, HEAD_PERCENT,
  HEAD_LT, HEAD_GT, HEAD_LE, HEAD_GE, HEAD_EQ_EQ, HEAD_BANG_EQ,
  HEAD_PIPE, HEAD_PIPE_PIPE, HEAD_AMP_AMP, HEAD_TILDE,
  HEAD_RANGE, HEAD_RANGE_INCLUSIVE,
  HEAD_DOT, HEAD_QDOT,

  /* Bindings & defs */
  HEAD_DEF, HEAD_MUT, HEAD_SET, HEAD_PROC,
  HEAD_DEFSTRUCT, HEAD_DEFMACRO,

  /* Control flow */
  HEAD_IF, HEAD_WHILE, HEAD_FOR,
  HEAD_BREAK, HEAD_CONTINUE, HEAD_RETURN,
  HEAD_TRY, HEAD_WITH_CTX, HEAD_MATCH,

  /* Concurrency / suspension */
  HEAD_YIELD, HEAD_AWAIT, HEAD_SPAWN, HEAD_PARALLEL, HEAD_RACE, HEAD_SLEEP,

  /* Vec ops */
  HEAD_VEC,
  HEAD_VEC_GET, HEAD_VEC_LEN, HEAD_VEC_PUSH, HEAD_VEC_SET,
  HEAD_VEC_CONCAT, HEAD_VEC_SLICE,

  /* Arr ops (mutable [Arr T]; see ARR_DESIGN.md) */
  HEAD_ARR,
  HEAD_ARR_GET, HEAD_ARR_SET, HEAD_ARR_PUSH, HEAD_ARR_POP, HEAD_ARR_LEN,

  /* Map ops */
  HEAD_MAP,
  HEAD_MAP_GET, HEAD_MAP_HAS, HEAD_MAP_LEN, HEAD_MAP_SET,
  HEAD_MAP_REMOVE, HEAD_MAP_KEYS, HEAD_MAP_VALS,

  /* Builtins */
  HEAD_PRINT, HEAD_LENGTH, HEAD_BYTE_LENGTH,
  HEAD_INDEX, HEAD_SLICE, HEAD_CONCAT,
  HEAD_HASH, HEAD_TO_STRING, HEAD_TRANSFORM, HEAD_FILTER,

  /* Errors */
  HEAD_ERROR, HEAD_ERROR_Q, HEAD_ERROR_VAL, HEAD_STACK_TRACE, HEAD_PANIC,
  HEAD_ASSERT_TYPE,  /* compile-time static type check; emits no runtime code */

  /* Boxes / atoms / coercion */
  HEAD_BOX, HEAD_BOX_Q, HEAD_ATOM, HEAD_ATOM_Q, HEAD_FUTURE_Q,
  HEAD_DEREF, HEAD_UNBOX, HEAD_RESET, HEAD_SWAP, HEAD_TO,
  HEAD_WATCH, HEAD_UNWATCH,

  /* Typed pointers (Stage 5a/5b/5c) */
  HEAD_PTR_CAST, HEAD_PTR_ADDR, HEAD_PTR_DEREF,
  HEAD_PTR_OFFSET, HEAD_PTR_DIFF,
  HEAD_ADDR,  /* [addr $p->field->...] — takes the address of a chain leaf */
  HEAD_PTR_NULL,  /* [ptr-null [Ptr T]] — typed null pointer literal */

  /* Buffer ops (Stage 6 — see BUFFER_DESIGN.md) */
  HEAD_BUF_LEN,
  HEAD_BUF_GET,
  HEAD_BUF_SET,
  HEAD_BUF_UGET,  /* buf-unchecked-get — bounds-check-elided escape hatch */
  HEAD_BUF_USET,  /* buf-unchecked-set — bounds-check-elided escape hatch */

  /* Native fn declaration with typed signature (Stage 5a) */
  HEAD_EXTERN,

  /* Streams / async / shell */
  HEAD_STREAM_NEXT, HEAD_COLLECT, HEAD_COUNT, HEAD_TAKE,
  HEAD_FIRST, HEAD_LINES, HEAD_EXEC, HEAD_SIGNAL, HEAD_CANCEL,
  HEAD_READ_FILE, HEAD_WRITE_FILE, HEAD_APPEND_FILE,

  /* Quote / syntax / macros */
  HEAD_QUOTE, HEAD_SYNTAX_QUOTE, HEAD_INTERPRET, HEAD_INTERPRET_PRELUDE,
  HEAD_SYNTAX_KIND, HEAD_SYNTAX_DATUM, HEAD_SYNTAX_HEAD,
  HEAD_SYNTAX_ARGS, HEAD_SYNTAX_COMMANDS, HEAD_SYNTAX_POS,
  HEAD_SYNTAX_STR, HEAD_MAKE_SYNTAX, HEAD_SYNTAX_ERROR,

  /* File-system surface growth (docs/SVM_FS_DESIGN.md phase 4) */
  HEAD_DELETE_FILE, HEAD_FILE_EXISTS, HEAD_LIST_DIR,

  HEAD_ID_COUNT  /* sentinel; must fit in uint8_t */
} HeadId;

typedef struct {
  uint32_t line;
  uint32_t column;
  uint32_t offset;
} SourcePos;

typedef struct AstNode AstNode;

struct AstNode {
  AstNodeType type;
  SourcePos   start;
  SourcePos   end;
  uint32_t    scope_mark;  /* hygiene: 0 = no macro context, >0 = macro expansion */
  uint8_t     is_caret;    /* US-013: ^name in syntax-quote — force scope mark 0 */
  uint8_t     is_gensym;   /* US-014: var-ref produced by gensym — accepted as binding name */
  uint8_t     inferred_type; /* JaclType (TYPE_DYN default), populated by typer pass */
  uint32_t    inferred_struct_idx; /* struct registry index when inferred_type==TYPE_STRUCT or typed-collection (elem idx), UINT32_MAX otherwise */
  uint32_t    inferred_key_struct_idx; /* key struct idx for TYPE_TYPED_MAP, UINT32_MAX otherwise */
  uint32_t    inferred_buf_len; /* N for TYPE_BUF; 0 otherwise. See BUFFER_DESIGN.md */
  uint32_t    inferred_buf_inner_len; /* M for TYPE_BUF nested form [Buf N [Buf M T]]; 0 otherwise (scalar/struct element). See BUFFER_DESIGN.md M4.2 */
  uint32_t    inferred_proc_shape_idx; /* step 2b: SHARED-registry TYPE_SHAPE_PROC idx
                                        * for the compiler to read a closure signature
                                        * directly; UINT32_MAX otherwise. Keep in sync
                                        * with struct AstNode in src/ast.c. */
  union {
    struct { AstNode*  head; AstNode** args; uint32_t arg_count;
             uint8_t   head_id; /* HeadId, stamped at construction; HEAD_NONE if unknown */
    } command;
    struct { int32_t   value; }                                    lit_int;
    struct { float     value; }                                    lit_float;
    struct { const char* value;   uint32_t length; }               lit_string;
    struct { const char* name;    uint32_t length; }               var_ref;
    struct { AstNode**   commands; uint32_t count; bool trailing_semi; } block;
    struct { AstNode**   segments; uint32_t count; }               interp_string;
    struct { const char* path; uint32_t path_len;
             const char* binding_name; uint32_t binding_name_len;
             const char** names; uint32_t* name_lens;
             uint32_t name_count;
             uint8_t is_module_binding; }                         use_decl;
    struct { const char* name; uint32_t name_len;
             const char** field_names; uint32_t* field_name_lens;
             const char** field_types; uint32_t* field_type_lens;
             uint8_t* field_mutable;
             uint32_t field_count; }                              defstruct;
    struct { const char* name; uint32_t name_len;
             const char** param_names; uint32_t* param_name_lens;
             uint32_t param_count; bool variadic;
             AstNode* body; }                                     defmacro;
    struct { AstNode* child; }                                     quote;
    struct { AstNode* child; }                                     syntax_quote;
    struct { AstNode* child; }                                     unquote;
    struct { AstNode* child; }                                     unquote_splicing;
    struct { AstNode* value; }              break_stmt;
    struct { AstNode* value; }              return_stmt;
    struct { const char** names; uint32_t* name_lens;
             const char** types; uint32_t* type_lens;
             uint32_t count;
             const char* rest_name; uint32_t rest_name_len; } destructure_vec;
    struct { const char** names; uint32_t* name_lens;
             const char** types; uint32_t* type_lens;
             uint32_t count;
             const char* rest_name; uint32_t rest_name_len;
             int spread_all; } destructure_named;
    struct { AstNode* expr; }                                      spread;
    struct { AstNode* head; AstNode** args; uint32_t arg_count;
             uint8_t background;
             uint8_t head_id; /* HeadId for shell heads (rarely well-known) */
    }  shell_cmd;
    struct { uint8_t is_mutable;
             const char* type_name; uint32_t type_name_len;
             const char* field_name; uint32_t field_name_len;
             AstNode* default_expr; }                             ctx_decl;
    struct { const char* message; }                                error;
  } data;
};

typedef struct {
  char*    buf;
  uint32_t len;
  uint32_t cap;
  arena_t* arena;
} AstStrBuf;

/* ========================================================================
 * Types — parser.c
 * ======================================================================== */

#define PARSER_INITIAL_CAP 64

typedef struct {
  AstNode** nodes;
  uint32_t  count;
  uint32_t  error_count;
} ParseResult;

typedef struct {
  AstNode** nodes;
  uint32_t  count;
  uint32_t  cap;
  arena_t*  arena;
} NodeArray;

typedef struct {
  Token*   tokens;
  uint32_t count;
  uint32_t pos;
  arena_t* arena;
  uint32_t error_count;
} Parser;

/* JaclClosure — legacy bytecode-VM closure; only a forward decl survives
 * (item 6): the sole remaining user is MacroEntry.closure (a pointer). */
typedef struct JaclClosure JaclClosure;

/* ========================================================================
 * Collection templates — collections.c
 *
 * The RRB vector and HAMT templates are instantiated with JaclVal types.
 * We include them here so the generated struct types (jacl_vec_root,
 * jacl_map_node, etc.) are visible to test code.
 * ======================================================================== */

#define JACL_COLL_ALLOCATOR { .alloc = libc_alloc, .free = libc_free, .ctx = NULL }

#define RRB_VEC_T                JaclVal
#define RRB_VEC_NAME             jacl_vec
#define RRB_VEC_ALLOCATOR        JACL_COLL_ALLOCATOR
#define RRB_VEC_GC_MODE
#define RRB_VEC_GC_ALLOC(t, sz)  gc_alloc(gc__current_heap, (t), (sz))
#define RRB_VEC_GC_OBJ_INTERNAL  OBJ_RRB_INTERNAL
#define RRB_VEC_GC_OBJ_LEAF      OBJ_RRB_LEAF
#define RRB_VEC_GC_OBJ_ROOT      OBJ_RRB_ROOT
#include "../lib/rrb_vec/rrb_vec.h"

#define HAMT_KEY_T             JaclVal
#define HAMT_VAL_T             JaclVal
#define HAMT_NAME              jacl_map
#define HAMT_GC_MODE
#define HAMT_GC_ALLOC(t, sz)   gc_alloc(gc__current_heap, (t), (sz))
#define HAMT_GC_OBJ_INTERNAL   OBJ_HAMT_INTERNAL
#define HAMT_GC_OBJ_LEAF       OBJ_HAMT_LEAF
#define HAMT_GC_OBJ_COLLISION  OBJ_HAMT_COLLISION
#include "../lib/hamt/hamt.h"


/* ========================================================================
 * Types — type-shape registry (shapes.c) + macro table (syntax.c)
 * ======================================================================== */

#define STRUCT_REGISTRY_INIT_CAP 32
#define COMPILER_MAX_PROC_PARAMS 16
#define MODULE_CACHE_MAX 64
#define MODULE_EXPORTS_MAX 64
#define COMPILER_LOCALS_MAX 256
#define COMPILER_UPVALUES_MAX 256
#define COMPILER_TRY_PATCHES_MAX 128
#define COMPILER_GLOBAL_ARITIES_MAX 64
#define MODULE_IMPORT_STACK_MAX 32
#define SUSPENSION_MAP_MAX 256
#define SUSPENSION_CALLEES_MAX 64
#define SM_MAX_SUSPENSION_POINTS 256
#define SM_MAX_STATE_FIELDS     256
#define MAX_PROC_INFOS 256
#define COMPILER_LOOP_DEPTH_MAX 16
#define COMPILER_BREAK_PATCHES_MAX 32
#define COMPILER_CONTINUE_PATCHES_MAX 32

typedef struct StructTypeRegistry StructTypeRegistry;

/* --- Macro table --- */

#define MACRO_TABLE_MAX 64

typedef struct {
  const char*   name;
  uint32_t      name_len;
  uint32_t      param_count;
  bool          variadic;    /* last param is rest-param (receives vec of remaining args) */
  bool          is_builtin;  /* true if registered in Phase 0 (can be overridden by user) */
  const char**  param_names;
  uint32_t*     param_name_lens;
  JaclClosure*  closure;
  AstNode*      body;        /* original body AST for macro body compilation */
} MacroEntry;

typedef struct MacroTable MacroTable;
struct MacroTable {
  MacroEntry entries[MACRO_TABLE_MAX];
  uint32_t   count;
};

/* Forward declaration — the full jacl_context_s definition (which embedded the
 * bytecode VM) was removed with the VM in item 6. The surviving emit-side stubs
 * (jacl_emit_support.c) and macro-staging consumers use it only by pointer. */
typedef struct jacl_context_s jacl_context_t;

/* --- ExpandFrame: tracks nested macro expansion for error reporting --- */

typedef struct {
    const char *name;
    uint32_t    name_len;
    uint32_t    line;
    uint32_t    col;
} ExpandFrame;

#define EXPAND_FRAME_MAX 256

/* --- ExpandState: per-compilation macro expansion state --- */

typedef struct {
    const char  *error_msg;
    uint32_t     error_line;
    uint32_t     error_col;
    ExpandFrame  frames[EXPAND_FRAME_MAX];
    uint32_t     frame_top;
    uint32_t     scope_counter;
    uint32_t     gensym_counter;
    jacl_context_t *ctx;              /* context for macro eval (NULL if N/A) */
} ExpandState;

/* --- The JaclType enum + type-shape registry types (shapes.c / typer.c) --- */

typedef enum {
  TYPE_DYN = 0,
  TYPE_BOOL,
  TYPE_NIL,
  TYPE_I32,
  TYPE_I64,
  TYPE_U32,
  TYPE_U64,
  TYPE_F32,
  TYPE_F64,
  TYPE_STR,
  TYPE_VEC,
  TYPE_MAP,
  TYPE_CLOSURE,
  TYPE_STRUCT,
  TYPE_STREAM,
  TYPE_TYPED_VEC,
  TYPE_TYPED_MAP,
  TYPE_FUTURE,
  TYPE_PTR,       /* typed pointer; pointee idx in inferred_struct_idx */
  TYPE_BOX,       /* mutable box (cell); element idx in inferred_struct_idx */
  TYPE_BUF,       /* fixed-size C-ABI array; elem idx in inferred_struct_idx,
                     N in inferred_buf_len. See BUFFER_DESIGN.md */
  /* Static-only small int types (no NaN-box rep; widen to i32/u32 at
   * dyn boundaries). Usable as [Buf N T] elements and struct fields. */
  TYPE_I8,
  TYPE_U8,
  TYPE_I16,
  TYPE_U16,
  /* Mutable, growable, heap-allocated reference array (segment_array-backed).
   * Sibling to TYPE_VEC/TYPE_TYPED_VEC but mutable + reference-identity.
   * See ARR_DESIGN.md. elem idx (typed form) in inferred_struct_idx. */
  TYPE_ARR,
  TYPE_TYPED_ARR
} JaclType;

/* Typed-collection element encoding for struct_idx: real struct registry
 * indices live below 0xFF00; values in [0xFF00, 0x10000) encode a scalar
 * JaclType (used for [Vec i64], [Map i32 i64] etc.). Shared between
 * the compiler and the typer so both can speak the same ABI. */
#define JACL_SCALAR_VEC_BASE        0xFF00u
#define JACL_IS_SCALAR_TYPE_IDX(idx) ((idx) >= JACL_SCALAR_VEC_BASE && (idx) < 0x10000u)
#define JACL_SCALAR_TYPE_IDX(t)     (JACL_SCALAR_VEC_BASE + (uint32_t)(t))
#define JACL_TYPE_IDX_TO_SCALAR(idx) ((JaclType)((idx) - JACL_SCALAR_VEC_BASE))

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclType    type;
  uint32_t    struct_type_idx;
  uint32_t    offset;
  uint32_t    size;
  bool        is_mutable;
  JaclVal     default_val;
  uint32_t    buf_len;  /* TYPE_BUF field: N elements. 0 otherwise. */
} StructTypeField;

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclVal     name_val;
  uint32_t    field_count;
  uint32_t    total_size;
  uint32_t    alignment;
  /* See src/shapes.c for layout rationale -- 32 bytes = 256 bits.
   * Must stay byte-for-byte in sync with shapes.c. */
  uint8_t     slot_ref_bitmap[32];
  StructTypeField fields[];   /* flexible array member */
} StructTypeDef;

/* TypeShape: see src/shapes.c (TYPE_REGISTRY_REFACTOR.md). The unity
 * build's source-of-truth lives in shapes.c; this mirror is for
 * external consumers (test binaries, embedders). Must stay byte-for-
 * byte identical to shapes.c or external reads grab wrong offsets. */
typedef enum {
  TYPE_SHAPE_NONE = 0,
  TYPE_SHAPE_STRUCT,
  TYPE_SHAPE_CTX,
  TYPE_SHAPE_TYPED_VEC,
  TYPE_SHAPE_TYPED_MAP,
  TYPE_SHAPE_BUF,
  TYPE_SHAPE_PTR,
  TYPE_SHAPE_FUTURE,
  TYPE_SHAPE_BOX,
  TYPE_SHAPE_TYPED_ARR,
  TYPE_SHAPE_PROC,            /* [Proc [P…] R]. See shapes.c / TYPED_CLOSURES_DESIGN B3. */
} TypeShapeKind;

typedef struct {
  TypeShapeKind kind;
  union {
    StructTypeDef* struct_def;
    struct { uint32_t elem_idx; } tvec;
    struct { uint32_t key_idx; uint32_t value_idx; } tmap;
    struct { uint32_t len; uint32_t elem_idx; } buf;
    struct { uint32_t pointee_idx; } ptr;
    struct { uint32_t resolves_to_idx; } future;
    struct { uint32_t boxes_idx; } box;
    struct { uint32_t elem_idx; } tarr;
    struct { uint32_t params_off; uint16_t param_count; uint32_t return_idx; } proc;
  } u;
} TypeShape;

struct StructTypeRegistry {
  StructTypeDef** defs;       /* defs[type_idx] → StructTypeDef* (defs[0] = NULL, reserved) */
  TypeShape*      shapes;     /* parallel view -- shapes[idx].kind + payload (Phase 1) */
  uint32_t count;             /* next available type_idx (starts at 1; 0 is reserved) */
  uint32_t capacity;
  arena_t* arena;             /* arena for StructTypeDef allocations (not owned) */
  uint32_t ctx_type_idx;      /* type_idx of the ctx struct (0 = not yet registered) */
  /* TYPE_SHAPE_PROC param-idx side pool — see shapes.c. Must stay in sync. */
  uint32_t* proc_param_pool;
  uint32_t  proc_param_count;
  uint32_t  proc_param_cap;
};








/* Arena-backed growable table of GlobalArity entries. Embedders that
 * call jacl_eval more than once stash one of these on their JaclVM so
 * proc declarations accumulate across calls; grown lazily by
 * compiler_compile when the per-compile table outpaces capacity. */














#define COMPILER_MODULE_BINDINGS_MAX 64



/* Bitmap helpers for inline struct slot tracking */
#define BITMAP_SET(bm, idx) ((bm)[(idx) / 8] |=  (uint8_t)(1u << ((idx) % 8)))
#define BITMAP_CLR(bm, idx) ((bm)[(idx) / 8] &= (uint8_t)~(1u << ((idx) % 8)))
#define BITMAP_GET(bm, idx) (((bm)[(idx) / 8] >> ((idx) % 8)) & 1u)





typedef enum {
    STREAM_PULL_VALUE,
    STREAM_PULL_EXHAUSTED,
    STREAM_PULL_ERROR
} StreamPullResult;

/* ========================================================================
 * Types — gc_collect.c
 * ======================================================================== */

#define GC_MARK_STACK_SIZE 4096

typedef struct {
    void  *fixed[GC_MARK_STACK_SIZE];
    int    top;
    void **overflow;
    int    ov_count;
    int    ov_cap;
} GCMarkStack;

/* ========================================================================
 * Chase-Lev deque template (instantiated before runtime.c types)
 * ======================================================================== */

#define CHASE_LEV_T    uintptr_t
#define CHASE_LEV_NAME rt_deque
#include "../lib/chase_lev/chase_lev.h"



/* DUAL DEFINITION: this struct is also defined in src/runtime.c for the
 * unity build. Any field change MUST be applied to both — and listed in
 * src/struct_drift_fields.h. See runtime.c for the rationale and
 * NOT_IMPLEMENTED.md §11 for the consolidation refactor that would
 * eliminate this duplication. */






typedef struct { uint32_t index; } EmbedJaclHandle;

/* ========================================================================
 * Function declarations
 *
 * Generated from: gcc -aux-info on src/jacl.c
 * Grouped by source file in unity build order.
 * ======================================================================== */

/* --- value.c --- */
extern JaclVal jacl_bool (bool b);
extern JaclVal jacl_i32 (int32_t n);
extern JaclVal jacl_f32 (float f);
extern JaclVal jacl_u32 (uint32_t n);
extern uint64_t jacl_type_tag (JaclVal v);
extern bool jacl_is_nil (JaclVal v);
extern bool jacl_is_bool (JaclVal v);
extern bool jacl_is_i32 (JaclVal v);
extern bool jacl_is_f32 (JaclVal v);
extern bool jacl_is_u32 (JaclVal v);
extern bool jacl_is_i64 (JaclVal v);
extern bool jacl_is_u64 (JaclVal v);
extern bool jacl_is_f64 (JaclVal v);
extern JaclVal jacl_string_ptr (void *p);
extern JaclVal jacl_vector_ptr (void *p);
extern JaclVal jacl_map_ptr (void *p);
extern JaclVal jacl_arr_ptr (void *p);
extern JaclVal jacl_closure_ptr (void *p);
extern JaclVal jacl_bignum_ptr (void *p);
extern void *jacl_as_ptr (JaclVal v);
extern bool jacl_is_string (JaclVal v);
extern bool jacl_is_vector (JaclVal v);
extern bool jacl_is_map (JaclVal v);
extern bool jacl_is_arr (JaclVal v);
extern bool jacl_is_closure (JaclVal v);
extern bool jacl_is_bignum (JaclVal v);
extern JaclVal jacl_cell_ptr (JaclMutableRef *p);
extern JaclMutableRef *jacl_as_cell (JaclVal v);
extern bool jacl_is_cell (JaclVal v);
extern JaclVal jacl_box_ptr (JaclMutableRef *p);
extern JaclMutableRef *jacl_as_box (JaclVal v);
extern bool jacl_is_box (JaclVal v);
extern JaclVal jacl_atom_ptr (JaclMutableRef *p);
extern JaclMutableRef *jacl_as_atom (JaclVal v);
extern bool jacl_is_atom (JaclVal v);
extern bool jacl_is_future (JaclVal v);
extern bool jacl_is_struct (JaclVal v);
extern bool jacl_is_native_fn (JaclVal v);
extern JaclVal jacl_native_fn (uint32_t index);
extern uint32_t jacl_as_native_fn_index (JaclVal v);
extern bool jacl_is_stream (JaclVal v);
extern JaclVal jacl_stream_ptr (void *p);
extern bool jacl_is_state_machine (JaclVal v);
extern JaclVal jacl_state_machine_ptr (void *p);
extern bool jacl_is_syntax (JaclVal v);
extern JaclVal jacl_syntax_ptr (void *p);
extern bool jacl_is_typed_vector (JaclVal v);
extern JaclVal jacl_typed_vector_ptr (void *p);
extern bool jacl_is_typed_map (JaclVal v);
extern JaclVal jacl_typed_map_ptr (void *p);
extern JaclVal jacl_inline_string (const char *s, size_t len);
extern bool jacl_is_inline_string (JaclVal v);
extern size_t jacl_inline_string_len (JaclVal v);
extern void jacl_inline_string_get (JaclVal v, char *buf, size_t buflen);
extern bool jacl_is_tainted (JaclVal v);
extern JaclVal jacl_set_tainted (JaclVal v);
extern JaclVal jacl_clear_tainted (JaclVal v);
extern bool jacl_is_secret (JaclVal v);
extern JaclVal jacl_set_secret (JaclVal v);
extern JaclVal jacl_clear_secret (JaclVal v);
extern bool jacl_is_error (JaclVal v);
extern JaclVal jacl_set_error (JaclVal v);
extern JaclVal jacl_clear_error (JaclVal v);
extern uint64_t jacl_propagate_flags (JaclVal a, JaclVal b);
extern JaclVal jacl_apply_flags (JaclVal result, uint64_t flags);
extern bool jacl_as_bool (JaclVal v);
extern int32_t jacl_as_i32 (JaclVal v);
extern float jacl_as_f32 (JaclVal v);
extern uint32_t jacl_as_u32 (JaclVal v);
extern int64_t jacl_as_i64 (JaclVal v);
extern uint64_t jacl_as_u64 (JaclVal v);
extern double jacl_as_f64 (JaclVal v);
extern JaclVal jacl_add_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_sub_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_mul_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_div_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_mod_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_neg_i32 (JaclVal a);
extern JaclVal jacl_add_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_sub_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_mul_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_div_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_neg_f32 (JaclVal a);
extern JaclVal jacl_eq (JaclVal a, JaclVal b);
extern JaclVal jacl_lt_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_gt_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_le_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_ge_i32 (JaclVal a, JaclVal b);
extern JaclVal jacl_lt_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_gt_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_le_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_ge_f32 (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_add (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_sub (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_mul (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_div (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_mod (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_neg (JaclVal a);
extern JaclVal jacl_u32_lt (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_gt (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_le (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_ge (JaclVal a, JaclVal b);
extern JaclVal jacl_u32_eq (JaclVal a, JaclVal b);
extern JaclVal jacl_i64_lt (JaclVal a, JaclVal b);
extern JaclVal jacl_i64_gt (JaclVal a, JaclVal b);
extern JaclVal jacl_i64_le (JaclVal a, JaclVal b);
extern JaclVal jacl_i64_ge (JaclVal a, JaclVal b);
extern JaclVal jacl_i64_eq (JaclVal a, JaclVal b);
extern JaclVal jacl_u64_lt (JaclVal a, JaclVal b);
extern JaclVal jacl_u64_gt (JaclVal a, JaclVal b);
extern JaclVal jacl_u64_le (JaclVal a, JaclVal b);
extern JaclVal jacl_u64_ge (JaclVal a, JaclVal b);
extern JaclVal jacl_u64_eq (JaclVal a, JaclVal b);
extern JaclVal jacl_f64_lt (JaclVal a, JaclVal b);
extern JaclVal jacl_f64_gt (JaclVal a, JaclVal b);
extern JaclVal jacl_f64_le (JaclVal a, JaclVal b);
extern JaclVal jacl_f64_ge (JaclVal a, JaclVal b);
extern JaclVal jacl_f64_eq (JaclVal a, JaclVal b);

/* --- gc.c --- */
extern GCHeader *gc_header_of (void *payload);
extern bool jacl_is_heap_type (JaclVal v);
extern void gc_block_pool_init (BlockPool *pool);
extern GCBlock *gc_block_pool_get (BlockPool *pool);
extern void gc_block_pool_return (BlockPool *pool, GCBlock *block);
extern void gc_block_pool_destroy (BlockPool *pool);
extern void gc_heap_init (ThreadHeap *heap, BlockPool *pool);
extern void gc_heap_destroy (ThreadHeap *heap);
extern void gc__oom_panic_default (ThreadHeap *heap, size_t request_size);
extern void (*gc__oom_handler)(ThreadHeap *, size_t);
extern void gc__mark_lines (GCBlock *block, size_t offset, size_t size);
extern bool gc__find_free_run (GCBlock *block, int from, int *out_start, int *out_len);
extern bool gc__find_fit_in_block (ThreadHeap *heap, GCBlock *block, int needed_lines, int from);
extern void *gc__bump_alloc (ThreadHeap *heap, size_t total, uint8_t obj_type);
extern void *gc_alloc (ThreadHeap *heap, uint8_t obj_type, size_t payload_size);
extern HeapRecord *jacl_as_heap_record_ptr (JaclVal v);
extern JaclVal jacl_heap_record_val (HeapRecord *s);
extern JaclVal jacl_i64 (ThreadHeap *heap, int64_t n);
extern JaclVal jacl_u64 (ThreadHeap *heap, uint64_t n);
extern JaclVal jacl_f64 (ThreadHeap *heap, double d);
extern JaclVal jacl_i64_add (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_i64_sub (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_i64_mul (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_i64_div (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_i64_mod (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_i64_neg (ThreadHeap *heap, JaclVal a);
extern JaclVal jacl_u64_add (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_u64_sub (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_u64_mul (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_u64_div (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_u64_mod (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_u64_neg (ThreadHeap *heap, JaclVal a);
extern JaclVal jacl_f64_add (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_f64_sub (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_f64_mul (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_f64_div (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_f64_mod (ThreadHeap *heap, JaclVal a, JaclVal b);
extern JaclVal jacl_f64_neg (ThreadHeap *heap, JaclVal a);
extern void grey_buf_init (GreyBuffer *gb);
extern void grey_buf_push (GreyBuffer *gb, JaclVal v);
extern void grey_buf_destroy (GreyBuffer *gb);
extern void remembered_set_init (RememberedSet *rs);
extern void remembered_set_push (RememberedSet *rs, JaclVal container);
extern uint32_t remembered_set_drain (RememberedSet *rs, JaclVal *out_entries, uint32_t out_cap);
extern void remembered_set_destroy (RememberedSet *rs);
extern void gc_write_barrier (GreyBuffer *gb, uint32_t *gc_active_ptr, JaclVal old_val, JaclVal new_val);
extern void gc_remembered_set_barrier (RememberedSet *rs, JaclVal container, JaclVal new_val);
extern JaclVal jacl_future_ptr (JaclFuture *p);
extern JaclFuture *jacl_as_future (JaclVal v);
extern void future_lock (JaclFuture *f);
extern void future_unlock (JaclFuture *f);
extern JaclVal jacl_future (ThreadHeap *heap);
extern FutureWaiter *jacl_future_resolve (JaclFuture *f, JaclVal result, GreyBuffer *gb, uint32_t *gc_active_ptr);
extern FutureWaiter *jacl_future_error (JaclFuture *f, JaclVal error, GreyBuffer *gb, uint32_t *gc_active_ptr);
extern bool jacl_future_add_waiter (JaclFuture *f, JaclVal continuation, ThreadHeap *heap,
                                    GreyBuffer *gb, uint32_t *gc_active_ptr);
extern JaclStream *jacl_as_stream (JaclVal v);
extern JaclVal jacl_stream (ThreadHeap *heap);
extern JaclVal parallel_agg_ptr (ParallelAgg *p);
extern ParallelAgg *as_parallel_agg (JaclVal v);
extern JaclVal jacl_parallel_agg (ThreadHeap *heap, uint32_t count, JaclVal state_machine);
extern JaclVal race_agg_ptr (RaceAgg *r);
extern RaceAgg *as_race_agg (JaclVal v);
extern JaclVal jacl_race_agg (ThreadHeap *heap, JaclVal state_machine);
extern JaclStateMachine *jacl_as_state_machine (JaclVal v);
extern JaclVal gc_alloc_state_machine (ThreadHeap *heap, uint32_t field_count);
extern JaclSyntax *jacl_as_syntax (JaclVal v);
extern JaclVal gc_alloc_syntax (ThreadHeap *heap);

/* --- syntax.c --- */
extern JaclVal syntax_from_ast (AstNode *node, ThreadHeap *heap, JaclInternTable *intern);
extern AstNode *syntax_to_ast (JaclVal syn_val, arena_t *arena);
/* Phase 5 staging hook: install a function that expands a macro call by running its body on
 * the SVM engine (see syntax.c). When installed and `JACL_STAGE_ON_SVM` is set, the expander
 * uses it in place of the legacy VM. NULL by default — the driver (which owns codegen + the
 * SVM bridge) installs it, keeping src/ free of any codegen/SVM dependency. */
extern const char *(*jacl_macro_stage_hook)(MacroEntry *entry, AstNode **args, uint32_t argc,
                                            arena_t *arena, AstNode **out);
/* In-guest `[interpret SRC]` hook (docs/SVM_GUEST_JIT_STAGING.md §5, model B). When installed
 * (`jacl_install_svm_interpret_hook`, the driver that owns codegen + the SVM bridge),
 * `jacl_interpret1` compiles + runs SRC on the §22 Jit capability in this window instead of the host
 * `interp` capability, returning a live JaclVal. NULL by default (host-cap path), keeping src/ and
 * ordinary AOT programs free of any codegen/SVM dependency. */
extern JaclVal (*jacl_interpret_hook)(JaclVal src);
extern void jacl_install_svm_interpret_hook(void);
extern const char *ast_expand_macros(AstNode **program, uint32_t count,
                                     MacroTable *macros, ThreadHeap *heap,
                                     JaclInternTable *intern, arena_t *arena,
                                     ExpandState *es,
                                     uint32_t *out_error_line,
                                     uint32_t *out_error_col);
extern JaclVal jacl_gensym_next(const char *prefix, uint32_t prefix_len,
                                ThreadHeap *heap, JaclInternTable *intern,
                                uint32_t *gensym_counter,
                                uint32_t scope_mark, const char **err);
extern const char *syntax_kind_name(uint8_t kind);

/* --- string.c --- */
extern uint32_t string__fnv1a (const char *data, uint32_t length);
extern void intern_table_init (JaclInternTable *table, arena_t *arena);
extern void intern_table_destroy (JaclInternTable *table);
extern JaclHeapString **intern__find_slot (JaclHeapString **entries, uint32_t cap, const char *data, uint32_t length, uint32_t hash);
extern void intern__rehash (JaclInternTable *table, uint32_t new_cap);
extern void intern__resize (JaclInternTable *table);
extern void intern__compact (JaclInternTable *table);
extern JaclVal jacl_intern (ThreadHeap *heap, JaclInternTable *table, const char *data, uint32_t length);
extern bool jacl_is_heap_string (JaclVal v);
extern JaclHeapString *jacl_as_heap_string (JaclVal v);
extern bool jacl_is_rope_string (JaclVal v);
extern JaclRopeString *jacl_as_rope_string (JaclVal v);
extern JaclVal jacl_rope_string_ptr (JaclRopeString *p);
extern uint32_t jacl_string_len (JaclVal v);
extern uint32_t jacl_string_byte_len (JaclVal v);
extern uint32_t jacl_string_data (JaclVal v, char *buf, size_t buflen);
extern uint32_t jacl_string_hash (JaclVal v);
extern bool jacl_string_eq (JaclVal a, JaclVal b);
extern const char *jacl_string_flat_ptr (JaclVal v, char *buf, size_t buflen);
extern int jacl_string_cmp (JaclVal a, JaclVal b);
extern uint32_t rope_string__compute_hash (rope r);
extern uint32_t rope_string__hash_extend  (uint32_t prev_hash, rope r);
extern JaclVal jacl_rope_string_create (ThreadHeap *heap, const uint8_t *data, size_t len);
extern bool jacl_utf8_validate (const char *data, size_t len);
extern bool jacl_nfd_normalize (const char *data, size_t len, uint8_t *stack_buf, size_t stack_cap, uint8_t **out, size_t *out_len);
extern bool jacl_grapheme_nth (const uint8_t *data, size_t len, size_t n, size_t *out_start, size_t *out_end);
extern JaclVal jacl_string_new (ThreadHeap *heap, JaclInternTable *table, const char *data, size_t length);

/* --- lexer.c --- */
extern void lexer__arr_init (TokenArray *arr, arena_t *arena);
extern void lexer__arr_push (TokenArray *arr, Token tok);
extern void lexer__init (Lexer *lex, const char *source, arena_t *arena);
extern char lexer__peek (Lexer *lex);
extern char lexer__advance (Lexer *lex);
extern Token lexer__make_token (Lexer *lex, TokenType type, uint32_t start_offset, uint32_t start_line, uint32_t start_col);
extern void strbuf_init (StringBuf *sb, arena_t *arena);
extern void strbuf_push (StringBuf *sb, char c);
extern uint32_t lexer__encode_utf8 (uint32_t cp, char *out);
extern int lexer__hex_digit (char c);
extern int lexer__is_word_start (char c);
extern int lexer__is_word_char (char c);
extern int lexer__is_word_char_no_arrow (Lexer *lex);
extern int lexer__is_operator_char (char c);
extern const char *lexer__unexpected_char_msg (Lexer *lex, char c);
extern const char *lexer__handle_escape (Lexer *lex, StringBuf *sb);
extern void lexer__skip_to_string_end (Lexer *lex);
extern void lexer__lex_number (Lexer *lex, TokenArray *arr, uint32_t *error_count);
extern int lexer__maybe_lex_neg_number (Lexer *lex, TokenArray *arr, uint32_t *error_count);
extern void lexer__lex_string_body (Lexer *lex, TokenArray *arr, uint32_t *error_count, uint32_t str_start, uint32_t str_line, uint32_t str_col);
extern void lexer__lex_interp_expr (Lexer *lex, TokenArray *arr, uint32_t *error_count);
extern void lexer__lex_interp_infix (Lexer *lex, TokenArray *arr, uint32_t *error_count);
extern LexResult lexer_lex (const char *source, arena_t *arena);

/* --- ast.c --- */
extern AstNode *ast_alloc (arena_t *arena);
extern AstNode **ast_alloc_array (arena_t *arena, uint32_t count);
extern void ast__buf_init (AstStrBuf *b, arena_t *arena);
extern void ast__buf_ensure (AstStrBuf *b, uint32_t extra);
extern void ast__buf_char (AstStrBuf *b, char c);
extern void ast__buf_str (AstStrBuf *b, const char *s, uint32_t len);
extern void ast__buf_cstr (AstStrBuf *b, const char *s);
extern const char *ast__buf_finish (AstStrBuf *b);
extern void ast__buf_escaped_char (AstStrBuf *b, char c);
extern int ast__needs_quoting (const char *s, uint32_t len);
extern void ast__pp_node (AstStrBuf *b, AstNode *node);
extern const char *ast_pretty_print (AstNode *node, arena_t *arena);
extern const char *ast_pretty_print_program (AstNode **nodes, uint32_t count, arena_t *arena);

/* --- parser.c --- */
extern void parser__arr_init (NodeArray *arr, arena_t *arena);
extern void parser__arr_push (NodeArray *arr, AstNode *node);
extern void parser__init (Parser *p, LexResult tokens, arena_t *arena);
extern Token *parser__peek (Parser *p);
extern Token *parser__advance (Parser *p);
extern int parser__at_end (Parser *p);
extern SourcePos parser__token_start (Token *tok);
extern SourcePos parser__token_end (Token *tok);
extern AstNode *parser__error (Parser *p, const char *message, Token *tok);
extern void parser__skip_newlines (Parser *p);
extern void parser__sync_bracket (Parser *p);
extern AstNode *parser__parse_atom (Parser *p);
extern AstNode *parser__parse_command (Parser *p);
extern int parser__is_operator (Token *tok);
extern AstNode *parser__maybe_arrow_access (Parser *p, AstNode *expr);
extern AstNode *parser__parse_expr (Parser *p);
extern int parser__is_command_end (Parser *p);
extern int parser__is_operand_end (Parser *p);
extern AstNode *parser__parse_use (Parser *p);
extern const char *parser__parse_inline_struct_type (Parser *p, uint32_t *out_len);
extern AstNode *parser__parse_defstruct (Parser *p);
extern AstNode *parser__parse_proc_params (Parser *p);
extern AstNode *parser__parse_proc_form (Parser *p, AstNode *proc_head);
extern AstNode *parser__parse_if_form (Parser *p, AstNode *if_head);
extern AstNode *parser__parse_while_form (Parser *p, AstNode *while_head);
extern AstNode *parser__parse_cmd_operand (Parser *p);
extern AstNode *parser__parse_cmd_expr (Parser *p);
extern AstNode *parser__parse_block (Parser *p);
extern AstNode *parser__parse_interp_string (Parser *p);
extern ParseResult parser_parse (LexResult tokens, arena_t *arena);

/* --- collections.c --- */
extern uint32_t jacl_val_hash (JaclVal v);
extern bool jacl_val_eq (JaclVal a, JaclVal b);
extern void collections__init (void);

/* --- typer.c --- */

/* TyperResult: captured first error from a typer pass. Defined in typer.c
 * because the unity build pulls typer.c in before compiler.c. */
struct TyperResult;
typedef struct TyperResult TyperResult;
/* Imported proc signature passed to the typer so cross-module calls
 * narrow to the declared return type instead of dyn. The compiler
 * builds an array of these (one per imported proc name from a
 * `use "path" {names}` destructuring import) by walking AST_USE
 * nodes and reading each dependency module's exports. The typer
 * registers them in its internal proc table during its pre-pass.
 *
 * Definition lives in typer.c so it can be used at typer-internal
 * registration sites without an extra header pull-through. External
 * consumers (this header's clients) only need the opaque pointer. */
struct TyperImportProc;
typedef struct TyperImportProc TyperImportProc;

/* imports/import_count are NULL/0 when there is no module context
 * (single-file compile, macro pre-typing, typer-only test harness).
 * seed_registry is NULL for first-compile / single-shot use; pass a
 * persistent StructTypeRegistry to surface prior compiles' struct +
 * ctx-field declarations to the typer (required for multi-eval
 * embedders so cross-call struct/ctx use type-checks). */
extern void typer_infer (AstNode **nodes, uint32_t count,
                         TyperResult *result_or_null,
                         TyperImportProc *imports, uint32_t import_count,
                         StructTypeRegistry *seed_registry,
                         uint32_t seed_struct_count);

/* --- type_error.c — shared formatters used by both compiler and typer.
 * Each writes to a caller buffer in snprintf style; the reporting
 * machinery is per-pass (compiler__error / typer__error). See
 * TYPE_SYSTEM.md "Type errors — shared formatters, per-pass reporting". */
extern int jacl_format_assign_mismatch (char *buf, size_t bufsz,
                                        JaclType target, JaclType actual,
                                        const char *name, uint32_t name_len);
extern int jacl_format_assign_dyn_named (char *buf, size_t bufsz,
                                         JaclType target,
                                         const char *name, uint32_t name_len);
extern int jacl_format_assign_dyn_unnamed (char *buf, size_t bufsz,
                                           JaclType target);
extern int jacl_format_assign_struct_to_dyn (char *buf, size_t bufsz,
                                             const char *name, uint32_t name_len);
extern int jacl_format_field_mismatch (char *buf, size_t bufsz,
                                       const char *struct_name, uint32_t struct_name_len,
                                       const char *field_name, uint32_t field_name_len,
                                       JaclType expected, JaclType actual);
extern int jacl_format_field_dyn_assign (char *buf, size_t bufsz,
                                         const char *struct_name, uint32_t struct_name_len,
                                         const char *field_name, uint32_t field_name_len,
                                         JaclType expected);
extern int jacl_format_proc_return_mismatch (char *buf, size_t bufsz,
                                             const char *proc_name, uint32_t proc_name_len,
                                             JaclType declared, JaclType actual);
extern int jacl_format_ptr_assign_pointee_mismatch (char *buf, size_t bufsz,
                                                    const char *name, uint32_t name_len);
extern int jacl_format_ptr_arithmetic (char *buf, size_t bufsz);
extern int jacl_format_ptr_compare_pointee_mismatch (char *buf, size_t bufsz);
extern int jacl_format_ptr_cast_bad_first_arg (char *buf, size_t bufsz);
extern int jacl_format_ptr_cast_value_not_u64 (char *buf, size_t bufsz, JaclType actual);
extern int jacl_format_ptr_op_expects_ptr (char *buf, size_t bufsz,
                                           const char *op_name, JaclType actual);
extern int jacl_format_ptr_offset_non_numeric (char *buf, size_t bufsz, JaclType actual);
extern int jacl_format_ptr_diff_expects_two (char *buf, size_t bufsz,
                                             JaclType a, JaclType b);
extern int jacl_format_ptr_diff_pointee_mismatch (char *buf, size_t bufsz);
extern int jacl_format_ptr_deref_struct (char *buf, size_t bufsz);
extern int jacl_format_ptr_null_bad_arg (char *buf, size_t bufsz);

/* --- type keyword / struct-layout helpers (ast.c, shapes.c) --- */
extern bool is_type_keyword (const char *word, size_t len);
extern JaclType type_from_keyword (const char *word, size_t len);
extern const char *type_name (JaclType t);
extern bool is_numeric_type (JaclType t);
extern bool is_unboxed_type (JaclType t);
extern uint32_t struct__type_size (JaclType t, StructTypeRegistry *reg, uint32_t struct_idx);
extern const char *jacl_non_core_builtins[];  /* NULL-terminated list */
/* persistent_arities is NULL when there is no persistent embedder
 * context. When non-NULL, compiler_compile reads its (entries, count)
 * on entry to seed the per-compile table, and on a successful commit
 * grows the persistent buffer (allocating from persistent_arena) if
 * the compiler accumulated more entries than the persistent capacity
 * holds. Typed proc signatures declared in earlier jacl_eval calls
 * stay visible to later compiles via this mechanism. */
extern void macro_table_init (MacroTable *t);
extern MacroEntry *macro_table_lookup (MacroTable *t, const char *name, uint32_t name_len);
extern bool macro__is_special_form (const char *name, uint32_t len);

/* --- context lifecycle and internal run API --- */

typedef enum {
    JACL_ERROR_NONE = 0,
    JACL_ERROR_COMPILE,
    JACL_ERROR_RUNTIME
} JaclErrorKind;

typedef struct {
    JaclErrorKind kind;
    const char   *message;
    uint32_t      line;
    uint32_t      col;
} JaclError;

extern jacl_context_t *jacl_ctx_new (jacl_context_t *parent);
extern void            jacl_ctx_destroy (jacl_context_t *ctx);


/* Scoped context switching: saves/restores gc__current_heap and the emergency
 * GC callback so that nested context operations are reentrant.
 *
 * jacl_ctx_save:    snapshot current thread-local GC state (call BEFORE ctx_new)
 * jacl_ctx_enter:   save + switch gc__current_heap to a context's heap
 * jacl_ctx_restore: restore a previously saved snapshot */
typedef struct {
    ThreadHeap *heap;
    void       (*gc_fn)(void *);
    void        *gc_ctx;
} jacl_ctx_saved_t;

extern void jacl_ctx_save    (jacl_ctx_saved_t *saved);
extern void jacl_ctx_restore (jacl_ctx_saved_t saved);

/* --- gc_collect.c --- */
extern void gc__ms_init (GCMarkStack *ms);
extern void gc__ms_push (GCMarkStack *ms, void *ptr);
extern bool gc__ms_pop (GCMarkStack *ms, void **out);
extern void gc__ms_destroy (GCMarkStack *ms);
extern void gc__ms_push_val (GCMarkStack *ms, JaclVal v);
extern void gc__ms_push_const (GCMarkStack *ms, JaclVal v);
extern void gc__finalize_dead (GCHeader *hdr);
extern void gc__trace_object (void *payload, GCMarkStack *ms);
/* Root-enumeration callback: a provider pushes its live roots onto `ms`. `ctx` is the
 * provider (the VM*). Lets the collector stay decoupled from the bytecode VM type; the VM's
 * enumerators are vm__gc_roots_major / vm__gc_roots_minor below. */
typedef void (*GcRootEnumerator)(ThreadHeap *heap, GCMarkStack *ms, void *ctx);

extern void gc_mark (ThreadHeap *heap, GcRootEnumerator enum_roots, void *ctx);
extern size_t gc_sweep (ThreadHeap *heap);
extern bool gc__ptr_in_heap (ThreadHeap *heap, void *ptr);
/* watermark != 0 enables concurrent-GC epoch protection: entries with
 * hdr->epoch >= watermark are kept alive (freshly interned, marker did not
 * observe them). Pass 0 from single-threaded callers. */
extern void gc_sweep_intern_table (JaclInternTable *table, ThreadHeap *heap, uint32_t watermark);
extern void gc__adjust_threshold (ThreadHeap *heap, size_t bytes_survived);
extern void gc_collect (ThreadHeap *heap, GcRootEnumerator enum_roots, void *ctx,
                        StructTypeRegistry *struct_registry, JaclInternTable *intern_table);
extern void gc_mark_minor (ThreadHeap *heap, GcRootEnumerator enum_roots, void *ctx, RememberedSet *remembered_set);
extern size_t gc_sweep_minor (ThreadHeap *heap);
extern void gc_collect_minor (ThreadHeap *heap, GcRootEnumerator enum_roots, void *ctx,
                              StructTypeRegistry *struct_registry, RememberedSet *remembered_set);
extern bool gc_should_major (ThreadHeap *heap);
extern size_t gc_sweep_concurrent (ThreadHeap *heap, GCBlock *skip_block, uint32_t watermark, uint8_t current_mark, BlockPool *pool);

/* ========================================================================
 * Auto-initialization constructor
 *
 * Template libraries (HAMT, RRB-vec, rope sum_tree) use per-compilation-unit
 * static function pointers for hash/eq/summarize handlers. When tests are
 * compiled as a separate compilation unit from libjacl.a, the test's copy
 * of these function pointers starts as NULL. This constructor runs at
 * program startup to wire them up.
 * ======================================================================== */

__attribute__((constructor))
static void jacl__init_template_handlers(void) {
    /* HAMT key handlers */
    jacl_map_set_key_handlers(jacl_val_hash, jacl_val_eq);
    /* RRB-vec hash handler */
    jacl_vec_set_hash_handler(jacl_val_hash);
    /* HAMT hash handlers (for map-of-maps) */
    jacl_map_set_hash_handlers(jacl_val_hash, jacl_val_hash);
    /* Rope sum_tree handlers */
    rope_st_set_handlers((rope_st_handlers){
        .identity  = rope_summary_identity,
        .combine   = rope_summary_combine,
        .summarize = rope_summary_summarize
    });
}


#endif /* JACL_H */
