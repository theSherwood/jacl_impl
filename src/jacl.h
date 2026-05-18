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
    OBJ_TYPED_HAMT_LEAF      /* typed map leaf: trace dyn key only, skip struct value bytes */
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
  TOKEN_EQUALS,
  TOKEN_COLON,
  TOKEN_DOUBLE_COLON,
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
  HEAD_PLUS, HEAD_MINUS, HEAD_STAR, HEAD_SLASH, HEAD_PERCENT,
  HEAD_LT, HEAD_GT, HEAD_LE, HEAD_GE, HEAD_EQ_EQ,
  HEAD_EQUALS, HEAD_COLON, HEAD_COLON_COLON,
  HEAD_PIPE, HEAD_PIPE_PIPE, HEAD_AMP_AMP, HEAD_TILDE,
  HEAD_DOTDOT_LT, HEAD_DOTDOT_EQ,
  HEAD_DOT, HEAD_QDOT,
  HEAD_DEF, HEAD_MUT, HEAD_SET, HEAD_PROC,
  HEAD_DEFSTRUCT, HEAD_DEFMACRO,
  HEAD_IF, HEAD_WHILE, HEAD_FOR,
  HEAD_BREAK, HEAD_CONTINUE, HEAD_RETURN,
  HEAD_TRY, HEAD_WITH_CTX, HEAD_MATCH,
  HEAD_YIELD, HEAD_AWAIT, HEAD_SPAWN, HEAD_PARALLEL, HEAD_RACE, HEAD_SLEEP,
  HEAD_VEC,
  HEAD_VEC_GET, HEAD_VEC_LEN, HEAD_VEC_PUSH, HEAD_VEC_SET,
  HEAD_VEC_CONCAT, HEAD_VEC_SLICE,
  HEAD_MAP,
  HEAD_MAP_GET, HEAD_MAP_HAS, HEAD_MAP_LEN, HEAD_MAP_SET,
  HEAD_MAP_REMOVE, HEAD_MAP_KEYS, HEAD_MAP_VALS,
  HEAD_PRINT, HEAD_LENGTH, HEAD_BYTE_LENGTH,
  HEAD_INDEX, HEAD_SLICE, HEAD_CONCAT,
  HEAD_HASH, HEAD_TO_STRING, HEAD_TRANSFORM, HEAD_FILTER,
  HEAD_ERROR, HEAD_ERROR_Q, HEAD_ERROR_VAL, HEAD_STACK_TRACE,
  HEAD_BOX, HEAD_BOX_Q, HEAD_ATOM, HEAD_ATOM_Q, HEAD_FUTURE_Q,
  HEAD_DEREF, HEAD_UNBOX, HEAD_RESET, HEAD_SWAP, HEAD_TO,
  HEAD_PTR_CAST, HEAD_PTR_ADDR, HEAD_PTR_DEREF, HEAD_PTR_NULL,
  HEAD_PTR_OFFSET, HEAD_PTR_DIFF,
  HEAD_ADDR,
  HEAD_EXTERN,
  HEAD_STREAM_NEXT, HEAD_COLLECT, HEAD_COUNT, HEAD_TAKE,
  HEAD_FIRST, HEAD_LINES, HEAD_EXEC, HEAD_SIGNAL, HEAD_CANCEL,
  HEAD_QUOTE, HEAD_SYNTAX_QUOTE, HEAD_INTERPRET, HEAD_INTERPRET_PRELUDE,
  HEAD_SYNTAX_KIND, HEAD_SYNTAX_DATUM, HEAD_SYNTAX_HEAD,
  HEAD_SYNTAX_ARGS, HEAD_SYNTAX_COMMANDS, HEAD_SYNTAX_POS,
  HEAD_SYNTAX_STR, HEAD_MAKE_SYNTAX, HEAD_SYNTAX_ERROR,
  HEAD_ID_COUNT
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

/* ========================================================================
 * Types — bytecode.c
 * ======================================================================== */

#define BYTECODE_INIT_CODE_CAP  256
#define BYTECODE_INIT_CONST_CAP 64

/* OP_EXEC flags byte — combine with | for mixed modes */
#define EXEC_FLAG_FULL   0x01  /* Return map {stdout, stderr, exit} instead of stream */
#define EXEC_FLAG_STDIN  0x02  /* Pop stdin value from stack before args */
#define EXEC_FLAG_BG     0x04  /* Background: fork and return Job map immediately */
#define EXEC_FLAG_PIPE   0x08  /* Pipe mode: next byte is command count (2+) */

typedef enum {
  OP_CONST,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_MOD,
  OP_NEG,
  OP_EQ,
  OP_LT,
  OP_GT,
  OP_LE,
  OP_GE,
  OP_PRINT,
  OP_DEF_GLOBAL,
  OP_GET_GLOBAL,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_UPVALUE,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_TAIL_CALL,
  OP_RETURN,
  OP_RETURN_WIDE,    /* return N inline struct slots: uint8_t width */
  OP_CLOSURE,
  OP_POP_N,
  OP_CONCAT,
  OP_STR_LEN,
  OP_STR_BYTE_LEN,
  OP_STR_INDEX,
  OP_STR_SLICE,
  OP_TO_STRING,
  OP_VEC,
  OP_VEC_GET,
  OP_VEC_LEN,
  OP_VEC_PUSH,
  OP_VEC_SET,
  OP_VEC_CONCAT,
  OP_VEC_SLICE,
  OP_VEC_SPREAD,
  OP_MAP,
  OP_MAP_GET,
  OP_MAP_HAS,
  OP_MAP_LEN,
  OP_MAP_SET,
  OP_MAP_REMOVE,
  OP_MAP_KEYS,
  OP_MAP_VALS,
  OP_EACH,
  OP_TRANSFORM,
  OP_FILTER,
  OP_ERROR,
  OP_IS_ERROR,
  OP_ERROR_VAL,
  OP_CHECK_ERROR,
  OP_JUMP_IF_ERROR,
  OP_STACK_TRACE,
  OP_MAKE_CELL,
  OP_GET_CELL_LOCAL,
  OP_SET_CELL_LOCAL,
  OP_GET_CELL_UPVALUE,
  OP_SET_CELL_UPVALUE,
  OP_SET_GLOBAL,
  OP_BOX,
  OP_BOX_STRUCT,     /* uint16_t type_idx; pop struct, wrap in typed box, push box */
  OP_ATOM,
  OP_DEREF,
  OP_RESET,
  OP_SWAP,
  OP_IS_BOX,
  OP_IS_BOX_TYPED,   /* uint16_t type_idx; pop value, push true if box with matching type_idx */
  OP_IS_ATOM,
  OP_IS_FUTURE,
  OP_AWAIT,
  OP_SPAWN,
  OP_RESOLVE_FUTURE,
  OP_PARALLEL,
  OP_RACE,
  OP_COMPLETE_PARALLEL,
  OP_COMPLETE_RACE,
  OP_ADD_I64,
  OP_SUB_I64,
  OP_MUL_I64,
  OP_DIV_I64,
  OP_MOD_I64,
  OP_NEG_I64,
  OP_LT_I64,
  OP_GT_I64,
  OP_LE_I64,
  OP_GE_I64,
  OP_EQ_I64,
  OP_DIV_U64,
  OP_MOD_U64,
  OP_LT_U64,
  OP_GT_U64,
  OP_LE_U64,
  OP_GE_U64,
  OP_ADD_F64,
  OP_SUB_F64,
  OP_MUL_F64,
  OP_DIV_F64,
  OP_MOD_F64,
  OP_NEG_F64,
  OP_LT_F64,
  OP_GT_F64,
  OP_LE_F64,
  OP_GE_F64,
  OP_EQ_F64,
  OP_TO_I32,
  OP_TO_I64,
  OP_TO_U32,
  OP_TO_U64,
  OP_TO_F32,
  OP_TO_F64,
  OP_TO_DYN,
  OP_CONST_I64,
  OP_CONST_U64,
  OP_CONST_F64,
  OP_HEAP_RECORD_NEW,
  OP_HEAP_RECORD_GET,
  OP_HEAP_RECORD_SET,
  OP_HEAP_RECORD_GET_DYN,
  OP_HEAP_RECORD_SET_DYN,
  OP_HEAP_RECORD_GET_INLINE,  /* u16 byte_offset, u16 sub_type_idx; pop heap record, push N inline slots from data+offset */
  OP_HEAP_RECORD_SET_INLINE,  /* u16 byte_offset, u16 sub_type_idx; pop N inline slots, pop heap record, copy to data+offset, push record back */
  OP_RESET_INLINE,            /* u16 type_idx; reset struct-box: copy N inline slots into box->data, shuffle box out, leave new bytes on TOS */
  OP_STRUCT_NEW_INLINE,
  OP_STRUCT_GET_INLINE,  /* uint8_t base_slot, uint16_t byte_offset, uint8_t field_type; read from stack-resident struct */
  OP_STRUCT_SET_INLINE,  /* uint8_t base_slot, uint16_t byte_offset, uint8_t field_type; write to stack-resident struct */
  OP_STRUCT_STORE_INLINE, /* uint8_t base_slot, uint16_t type_idx; de-materialize heap struct to inline stack slots */
  OP_STRUCT_GET_UPVALUE,  /* uint8_t base_uv_slot, uint16_t byte_offset, uint8_t field_type; read from closure-captured inline struct */
  OP_STRUCT_SET_UPVALUE,  /* uint8_t base_uv_slot, uint16_t byte_offset, uint8_t field_type; write to closure-captured inline struct */
  OP_LOAD_INLINE_LOCAL,    /* uint8_t base_slot, uint16_t type_idx; copy N inline slots from local to TOS */
  OP_LOAD_INLINE_UPVALUE,  /* uint8_t base_uv_slot, uint16_t type_idx; copy N inline slots from closure upvalue to TOS */
  OP_PRINT_STRUCT,         /* uint16_t type_idx; pop N inline slots and print Name{...} */
  OP_STRUCT_EQ_TOS,        /* uint16_t type_idx; pop two structs (inline or heap), memcmp, push bool */
  OP_STRUCT_GET_INLINE_TOS,/* uint16_t type_idx, u16 byte_offset, u8 field_type [+ u16 type_idx if STRUCT]; pop inline struct from TOS, push field */
  OP_STRUCT_EXPAND,      /* uint16_t type_idx; pop heap HeapRecord, push as N inline stack slots */
  OP_STRUCT_EQ_INLINE,   /* uint8_t base_a, uint8_t base_b, uint16_t total_size; memcmp two inline structs, push bool */
  OP_STRUCT_HASH_INLINE, /* uint8_t base_slot, uint16_t total_size, uint16_t type_idx; hash inline struct bytes, push i32 */
  OP_HASH,               /* pop value, push i32 hash via jacl_val_hash */
  OP_CLOSE_LOOP,
  OP_DESTRUCTURE_VEC,
  OP_DESTRUCTURE_NAMED,
  OP_DESTRUCTURE_VEC_REST,
  OP_DESTRUCTURE_NAMED_REST,
  OP_SPREAD,
  OP_CALL_SPREAD,
  OP_FOLD_SPREAD,
  OP_COLLECT_VARIADIC,
  OP_YIELD,
  OP_STREAM_NEXT,
  OP_COLLECT,
  OP_IS_STREAM_EXHAUSTED,
  OP_COUNT,
  OP_TAKE,
  OP_FIRST,
  OP_LINES,
  OP_GET_STATE_FIELD,
  OP_SET_STATE_FIELD,
  OP_GET_RESUME_POINT,
  OP_SET_RESUME_POINT,
  OP_YIELD_SM,
  OP_AWAIT_SM,
  OP_SLEEP_SM,    /* pop seconds (number); register timer with runtime, suspend SM */
  OP_SLEEP_BLOCK, /* pop seconds (number); nanosleep blocking, push nil — non-SM path */
  OP_CALL_SUSPEND,
  OP_GET_STATE_FIELD_CELL,
  OP_SET_STATE_FIELD_CELL,
  OP_GET_STATE_FIELD_WIDE,  /* uint8_t base_idx, uint8_t width; copy N slots from SM to stack */
  OP_SET_STATE_FIELD_WIDE,  /* uint8_t base_idx, uint8_t width; copy N slots from stack to SM */
  OP_SYNTAX_SPLICE,
  OP_SYNTAX_OP,    /* uint8_t subop; pops syntax object(s), pushes introspection result */
  OP_INTERPRET,    /* pop string, interpret as JACL source, push result */
  OP_INTERPRET_PRELUDE, /* push default permissive prelude map for [interpret] */
  OP_EXEC,         /* uint8_t flags; unified exec opcode — see EXEC_FLAG_* constants */
  OP_AWAIT_JOB,    /* pop value; if Job, waitpid + push result; if future, check resolved */
  OP_SIGNAL,       /* pop signal_name + job; send signal to pid, push $true/$false */
  OP_HALT,
  OP_GET_CTX,      /* push vm->ctx (implicit context struct) onto stack */
  OP_CTX_FORK,     /* save old ctx to VM stack, alloc new ctx from pool, memcpy data, swap vm->ctx */
  OP_CTX_RESTORE,  /* pop saved ctx from VM save stack, free forked ctx to pool, restore vm->ctx */
  OP_SET_CTX,      /* pop value from stack and store in vm->ctx */
  OP_RANGE,        /* uint8_t inclusive; pop end, pop start, push range stream */
  OP_OPTIONAL_GET, /* uint16_t const_idx (field name); pop struct, push field or nil if struct is nil */

  /* --- Typed vector operations --- */
  OP_TYPED_VEC,          /* uint16_t type_idx, uint8_t count; create typed vec from stack elements */
  OP_TYPED_VEC_PUSH,     /* uint16_t type_idx; pop struct, pop tvec; push new tvec */
  OP_TYPED_VEC_SET,      /* uint16_t type_idx; pop struct, pop idx, pop tvec; push new tvec */
  OP_TYPED_VEC_LEN,      /* pop tvec; push i32 count */

  OP_TYPED_MAP,          /* uint16_t type_idx, uint8_t pair_count; create typed map */
  OP_TYPED_MAP_SET,      /* uint16_t type_idx; pop struct, pop key, pop tmap; push new tmap */
  OP_TYPED_MAP_HAS,      /* pop key, pop tmap; push bool */
  OP_TYPED_MAP_REMOVE,   /* pop key, pop tmap; push new tmap */
  OP_TYPED_MAP_LEN,      /* pop tmap; push i32 count */
  OP_TYPED_MAP_KEYS,     /* pop tmap; push dyn vec of keys */
  OP_TYPED_MAP_VALS,     /* uint16_t type_idx; pop tmap; push typed vec of values */

  OP_TYPED_VEC_PRINT,    /* uint16_t type_idx; pop tvec; print elements with struct formatting */
  OP_TYPED_MAP_PRINT,    /* uint16_t type_idx; pop tmap; print entries with struct formatting */
  OP_TYPED_VEC_EQ,       /* uint16_t type_idx; pop tvec_b, pop tvec_a; push bool */
  OP_TYPED_MAP_EQ,       /* uint16_t type_idx; pop tmap_b, pop tmap_a; push bool */

  OP_IS_BOX_TYPED_VEC,   /* no operand; pop val; push bool (is box containing typed vec?) */
  OP_IS_BOX_TYPED_MAP,   /* no operand; pop val; push bool (is box containing typed map?) */

  OP_TYPED_VEC_CONCAT,   /* uint16_t type_idx; pop tvec_b, pop tvec_a; push concatenated tvec */
  OP_TYPED_VEC_SLICE,    /* uint16_t type_idx; pop end, pop start, pop tvec; push sliced tvec */
  OP_TYPED_EACH,         /* uint16_t type_idx; pop closure, pop typed coll; iterate with materialized elements */
  OP_TYPED_TRANSFORM,    /* uint16_t type_idx; pop closure, pop typed coll; transform → dyn vec */
  OP_TYPED_FILTER,       /* uint16_t type_idx; pop closure, pop typed coll; filter → typed vec */

  OP_TYPED_VEC_GET_INLINE, /* uint16_t type_idx; pop idx, pop tvec; push width inline slots */
  OP_TYPED_MAP_GET_INLINE, /* uint16_t type_idx; pop key, pop tmap; push width inline slots */
  OP_INLINE_TO_LOCAL,      /* uint8_t base_slot, uint16_t type_idx; copy width inline TOS to local, pop */
  OP_DEREF_INLINE,         /* uint16_t type_idx; pop box, push inline bytes from ref->data[] */

  /* Stage 5b — typed pointer load/store (appended to keep prior
   * opcode positions stable). */
  OP_PTR_LOAD,             /* u16 byte_offset, u8 field_type; pop u64 ptr, push value at *ptr+offset */
  OP_PTR_STORE,            /* u16 byte_offset, u8 field_type; pop value, pop u64 ptr, write to *ptr+offset, push ptr */

  /* Stage 5c — typed pointer arithmetic */
  OP_PTR_OFFSET,           /* u16 elem_size; pop i32/i64 n, pop u64 p, push p + n*elem_size */
  OP_PTR_DIFF,             /* u16 elem_size; pop u64 b, pop u64 a, push (i64)(a-b)/elem_size */

  /* Nested struct fields through pointers — load/store whole inline structs at *ptr+offset */
  OP_PTR_ADD_OFFSET,       /* u16 byte_offset; pop u64 ptr, push ptr+offset */
  OP_PTR_LOAD_INLINE,      /* u16 byte_offset, u16 sub_type_idx; pop u64 ptr, push N inline slots from *ptr+offset */
  OP_PTR_STORE_INLINE      /* u16 byte_offset, u16 sub_type_idx; pop N inline slots, pop u64 ptr, copy bytes to *ptr+offset, push ptr */
} OpCode;

typedef struct {
  uint8_t*  code;
  uint32_t  code_count;
  uint32_t  code_cap;
  JaclVal*  constants;
  uint32_t  const_count;
  uint32_t  const_cap;
  uint32_t* lines;
  uint32_t  lines_cap;
  arena_t*  arena;
} BytecodeChunk;

typedef struct {
  BytecodeChunk chunk;
  uint8_t       param_count;
  uint8_t       param_total_slots;   /* total JaclVal slots for all params (sum of widths; >= param_count) */
  bool          has_inline_params;   /* true if any param is an inline struct (width > 1 or value-type struct) */
  JaclVal*      param_names;
  JaclVal*      upvalues;
  uint8_t       upvalue_count;
  uint16_t      upvalue_total_slots; /* total JaclVal slots in upvalues array (sum of widths) */
  const char*   name;
  uint8_t       min_args;
  bool          variadic;
  bool          pinned;
  int8_t        pin_worker_id;
  bool          is_generator;
  bool          is_sm_compiled;
  uint8_t       sm_field_count;
  uint8_t       upvalue_inline_bitmap[32]; /* US-014: marks which upvalue slots hold raw struct bytes */
} JaclClosure;

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
 * Types — compiler.c
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

typedef struct {
  BytecodeChunk chunk;
  uint32_t      error_count;
  const char*   error_message;
  bool          suspending;
  StructTypeRegistry* struct_registry;
  MacroTable*   macro_table;
} CompileResult;

/* Forward declaration (full definition below) */
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

/* --- jacl_context_t: reentrant execution context --- */

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
  TYPE_BOX        /* mutable box (cell); element idx in inferred_struct_idx */
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
  bool        is_mutable;  /* true if field can be written via set */
  JaclVal     default_val; /* default value for ctx fields (JACL_NIL if none) */
} StructTypeField;

typedef struct {
  const char* name;
  uint32_t    name_len;
  JaclVal     name_val;
  uint32_t    field_count;
  uint32_t    total_size;
  uint32_t    alignment;
  StructTypeField fields[];   /* flexible array member */
} StructTypeDef;

struct StructTypeRegistry {
  StructTypeDef** defs;       /* defs[type_idx] → StructTypeDef* (defs[0] = NULL, reserved) */
  uint32_t count;             /* next available type_idx (starts at 1; 0 is reserved) */
  uint32_t capacity;
  arena_t* arena;             /* arena for StructTypeDef allocations (not owned) */
  uint32_t ctx_type_idx;      /* type_idx of the ctx struct (0 = not yet registered) */
};

typedef struct {
  const char* name;
  uint32_t    name_len;
  int16_t     arity;
  bool        is_mutable;
  bool        suspends;
  JaclType    type;
  JaclType    return_type;
  JaclType    param_types[COMPILER_MAX_PROC_PARAMS];
  uint32_t    param_count;
} ExportEntry;

typedef struct {
  BytecodeChunk* chunk;
  const char*    path;
  const char*    source;
  ExportEntry*   exports;
  uint32_t       export_count;
  uint32_t       topo_order;
  bool           compiled;
} Module;

typedef struct {
  Module**    modules;
  uint32_t    module_count;
  uint32_t    error_count;
  const char* error_message;
  bool        suspending;
  StructTypeRegistry* struct_registry;
} ProgramResult;

typedef struct {
  Module*  modules[MODULE_CACHE_MAX];
  char*    paths[MODULE_CACHE_MAX];
  uint32_t count;
  uint32_t topo_counter;
  arena_t* arena;
} ModuleCache;

typedef struct {
  const char* paths[MODULE_IMPORT_STACK_MAX];
  uint32_t    count;
} ImportStack;

typedef struct {
  JaclVal   name;
  int       depth;
  int16_t   known_arity;
  bool      is_mutable;
  bool      is_param;
  bool      suspends;
  bool      captures_mutable;
  JaclType  type;
  uint32_t  struct_type_idx;
  uint32_t  key_struct_idx;   /* key struct index for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
  JaclType  return_type;
  uint32_t  return_struct_idx;
  JaclType* param_types;
  uint32_t  scope_mark;
  uint16_t  width;            /* stack slot count: 1 for scalars, N for inline structs */
  bool      is_inline;        /* true if struct is stored inline on stack (raw bytes, not heap pointer) */
} Local;

typedef struct {
  JaclVal   name;
  int16_t   known_arity;
  bool      is_mutable;
  bool      suspends;
  bool      captures_mutable;
  bool      prelude_is_native_fn;
  JaclType  type;
  uint32_t  struct_type_idx;
  uint32_t  key_struct_idx;   /* key struct index for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
  JaclType  return_type;
  uint32_t  return_struct_idx;
  JaclType  param_types[COMPILER_MAX_PROC_PARAMS];
} GlobalArity;

typedef struct {
  uint8_t   index;
  uint8_t   is_local;
  JaclVal   name;
  bool      is_mutable;
  bool      suspends;
  bool      captures_mutable;
  JaclType  type;
  uint32_t  struct_type_idx;
  uint32_t  key_struct_idx;   /* key struct index for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
  uint32_t  scope_mark;
  uint16_t  width;      /* JaclVal slot count: 1 for scalars, N for inline structs */
  bool      is_inline;  /* true if capturing an inline struct (raw bytes, not heap) */
  uint16_t  base_slot;  /* position in this closure's upvalue array */
} Upvalue;

typedef struct {
  JaclVal name;
  bool    suspends;
  bool    is_generator;
} SuspensionEntry;

typedef struct {
  SuspensionEntry entries[SUSPENSION_MAP_MAX];
  uint32_t count;
} SuspensionMap;

typedef struct {
  JaclVal  name;
  bool     direct_suspends;
  bool     has_yield;
  bool     has_indirect_call;
  JaclVal  callees[SUSPENSION_CALLEES_MAX];
  uint32_t callee_count;
} ProcSuspendInfo;

typedef struct {
  ProcSuspendInfo procs[MAX_PROC_INFOS];
  uint32_t count;
} ProcSuspendInfoList;

typedef enum {
  SUSPEND_YIELD,
  SUSPEND_AWAIT,
  SUSPEND_PARALLEL,
  SUSPEND_RACE,
  SUSPEND_CALL
} SuspensionPointType;

typedef struct {
  uint32_t            id;
  SuspensionPointType type;
  AstNode*            node;
  uint32_t            line;
  uint32_t            column;
  uint16_t            pre_stack_depth;
} SuspensionPoint;

typedef struct {
  JaclVal  name;
  uint32_t field_index;  /* base slot index in state machine fields[] */
  bool     is_mutable;
  bool     is_param;
  uint16_t width;        /* number of JaclVal slots occupied (default 1, N for inline structs) */
  uint32_t struct_type_idx; /* struct registry index when width > 1, else 0 */
} StateField;

typedef struct {
  uint32_t         field_count;
  uint32_t         total_slots;  /* sum of all field widths — actual fields[] size needed */
  StateField       fields[SM_MAX_STATE_FIELDS];
  ThreadHeap*      heap;          /* for interning names > 7 bytes */
  JaclInternTable* intern_table;  /* for interning names > 7 bytes */
} StateLayout;

typedef struct {
  uint32_t        suspension_count;
  SuspensionPoint suspension_points[SM_MAX_SUSPENSION_POINTS];
  StateLayout     state_layout;
  uint32_t        ctx_field_idx;  /* state field index for __ctx (UINT32_MAX if absent) */
  uint16_t        max_pre_stack_depth;
  uint16_t        spill_base_slot;
  uint16_t        scratch_slot;
} SuspensionAnalysis;

typedef struct {
  int32_t first_write;
  int32_t last_read;
} FieldLiveness;

typedef struct {
  uint32_t loop_start;
  uint32_t break_patches[COMPILER_BREAK_PATCHES_MAX];
  uint32_t break_patch_count;
  uint32_t continue_patches[COMPILER_CONTINUE_PATCHES_MAX];
  uint32_t continue_patch_count;
  uint32_t local_count_at_loop;
  bool     is_for_loop;
} LoopContext;

typedef struct {
  uint32_t label_patches[SM_MAX_SUSPENSION_POINTS];
  uint32_t label_count;
} SMDispatchContext;

#define COMPILER_MODULE_BINDINGS_MAX 64

typedef struct {
  JaclVal name;       /* local variable name (interned) */
  int     local_slot; /* slot in the locals array */
  Module* module;     /* the module it binds to */
} ModuleBinding;

typedef struct Compiler Compiler;
struct Compiler {
  BytecodeChunk*   chunk;
  arena_t*         arena;
  ThreadHeap*      heap;
  JaclInternTable* intern_table;
  uint32_t         error_count;
  const char*      first_error;
  Local            locals[COMPILER_LOCALS_MAX];
  uint32_t         local_count;
  int              scope_depth;
  Upvalue          upvalues[COMPILER_UPVALUES_MAX];
  uint32_t         upvalue_count;
  Compiler*        enclosing;
  GlobalArity      global_arities[COMPILER_GLOBAL_ARITIES_MAX];
  uint32_t         global_arity_count;
  uint32_t         try_patches[COMPILER_TRY_PATCHES_MAX];
  uint32_t         try_patch_count;
  bool             in_try_body;
  bool             in_non_suspending_callback;
  SuspensionMap*   suspension_map;
  bool             in_concurrent_body;
  bool             pin_all_closures;
  bool             force_global_procs;
  bool             ctx_pre_registered;
  JaclType         last_expr_type; /* see compiler.c for documentation */
  JaclType         return_type;
  uint32_t         return_struct_idx; /* struct registry index when return_type==TYPE_STRUCT */
  ModuleCache*     module_cache;
  Module*          current_module;
  ImportStack*     import_stack;
  const char*      module_prefix;
  uint32_t         module_prefix_len;
  StructTypeRegistry* struct_registry;
  LoopContext          loop_stack[COMPILER_LOOP_DEPTH_MAX];
  uint32_t             loop_depth;
  bool                 has_yield;
  uint32_t             sm_suspension_idx;
  SMDispatchContext     sm_dispatch;
  SuspensionAnalysis*  sm_analysis;
  MacroTable*          macro_table;
  uint32_t             current_scope_mark;
  bool                 has_prelude;
  uint8_t              inline_repr;   /* INLINE_NONE / INLINE_STACK / INLINE_REF */
  uint8_t              inline_ref_base;
  uint16_t             inline_ref_offset;
  bool                 shell_fallback;
  ModuleBinding        module_bindings[COMPILER_MODULE_BINDINGS_MAX];
  uint32_t             module_binding_count;
  /* Flow typing: type narrowings from box? guards in if-branches */
  struct {
    uint16_t local_slot;     /* which local variable is narrowed */
    uint32_t box_type_idx;   /* struct element type_idx (0=dyn, >0=struct/collection element) */
    uint32_t box_key_type_idx; /* key struct idx for TYPE_TYPED_MAP (UINT32_MAX=dyn) */
    JaclType box_type;       /* TYPE_STRUCT, TYPE_TYPED_VEC, TYPE_TYPED_MAP, or TYPE_DYN */
  } narrowings[8];
  uint32_t             narrowing_count;
  void*                ctx_fields;       /* CtxFieldList* (opaque in header) */
};

/* ========================================================================
 * Types — vm.c (ctx pool)
 * ======================================================================== */

#define CTX_POOL_INIT_SIZE 8

typedef struct {
    uintptr_t free_list_head; /* atomic: pointer to first free HeapRecord, or 0 */
    uint32_t struct_size;              /* StructTypeDef->total_size (byte size of data[]) */
    uint32_t type_idx;                 /* ctx struct type_idx in StructTypeRegistry */
    StructTypeDef *sdef;               /* cached pointer to ctx StructTypeDef */
} JaclCtxPool;

/* ========================================================================
 * Types — vm.c
 * ======================================================================== */

/* VM resource caps. These are hard limits — overflow surfaces as a
 * runtime error from the VM (operand-stack-overflow / call-depth-exceeded)
 * with a message naming the limit. See AUDIT.md §16.
 *   - VM_STACK_MAX  : operand-stack slot count. Hit by deep expression
 *                     nesting, wide inline structs, large spreads.
 *   - VM_FRAMES_MAX : call-frame depth. Hit by deep recursion. Convert
 *                     to a tail call, state machine, or trampoline. */
#define VM_STACK_MAX        1024
#define VM_FRAMES_MAX       256
#define VM_ENV_INIT_CAP     16
#define VM_STACK_TRACE_MAX  32

/* Bitmap helpers for inline struct slot tracking */
#define BITMAP_SET(bm, idx) ((bm)[(idx) / 8] |=  (uint8_t)(1u << ((idx) % 8)))
#define BITMAP_CLR(bm, idx) ((bm)[(idx) / 8] &= (uint8_t)~(1u << ((idx) % 8)))
#define BITMAP_GET(bm, idx) (((bm)[(idx) / 8] >> ((idx) % 8)) & 1u)

typedef enum {
  VM_OK,
  VM_RUNTIME_ERROR,
  VM_STACK_OVERFLOW,
  VM_YIELD
} VMResult;

typedef void (*VMPrintFn)(const char* text, uint32_t len, void* ctx);

typedef struct {
  JaclVal*  names;
  JaclVal*  values;
  uint32_t  count;
  uint32_t  cap;
} Environment;

typedef struct {
  JaclClosure*   closure;
  uint8_t*       return_ip;
  uint32_t       stack_base;
  BytecodeChunk* chunk;
} CallFrame;

typedef struct {
  const char* function_name;
  uint32_t    line_number;
} StackTraceEntry;

typedef struct {
  StackTraceEntry entries[VM_STACK_TRACE_MAX];
  uint32_t        count;
} StackTrace;

typedef struct {
  JaclVal        stack[VM_STACK_MAX];
  uint32_t       stack_top;
  CallFrame      frames[VM_FRAMES_MAX];
  uint32_t       frame_count;
  uint8_t*       ip;
  BytecodeChunk* chunk;
  VMPrintFn      print_fn;
  void*          print_ctx;
  Environment    env;
  arena_t*       arena;
  BlockPool      block_pool;
  ThreadHeap     heap;
  JaclInternTable* intern_table;
  BytecodeChunk* top_chunk;
  GreyBuffer*    grey_buf;
  RememberedSet* remembered_set;
  uint32_t  *gc_active_ptr;
  void*          runtime;
  int            worker_id;
  const char*    error_message;
  uint32_t       error_line;
  StackTrace     stack_trace;
  StructTypeRegistry* struct_registry;
  JaclCtxPool *ctx_pool;
  JaclVal    ctx;              /* current implicit context struct */
  JaclVal    saved_ctx[8];     /* with-ctx save stack for nested forks */
  uint8_t    saved_ctx_count;  /* number of entries in saved_ctx */
  JaclVal*   gc_handle_slots;
  uint32_t   gc_handle_count;
  JaclVal    (*call_native)(void* ctx, uint32_t fn_index, JaclVal* args, int argc);
  void*      native_fn_ctx;
  int8_t*    native_fn_arities;
  uint32_t   native_fn_count;
  uint32_t   spread_counts[32];
  uint32_t   spread_count_top;
  JaclVal    yield_value;
  uint32_t   macro_scope_mark;   /* >0 during staged macro eval; make-syntax applies this */
  /* US-010: gensym counter pointer — set by expand__node before staged closure invocation */
  uint32_t  *gensym_counter_ptr; /* points into ExpandState.gensym_counter; NULL outside staged eval */
  /* US-014: bitmap marking stack slots that hold raw inline struct bytes (not GC-traceable JaclVals) */
  uint8_t    inline_slot_bitmap[VM_STACK_MAX / 8];
} VM;

/* --- jacl_context_t: reentrant execution context (full definition) ---
 *
 * Owns arena, VM (which contains ThreadHeap), intern table, macro table.
 * Child contexts share parent's intern table but have their own arena/heap.
 * Macro expansion state (error, frames, scope counter, gensym counter) is
 * per-context, eliminating the file-static reentrancy hazards.
 */
struct jacl_context_s {
    arena_t          arena;
    VM               vm;
    JaclInternTable  intern_table;
    MacroTable       macro_table;
    uint64_t         restriction_set;     /* all-permissive = UINT64_MAX */
    ExpandState      expand;

    /* Parent context (NULL for root) */
    jacl_context_t  *parent;
    bool             owns_intern_table;   /* false when sharing parent's */
};

typedef struct {
  char*     data;
  uint32_t  len;
  uint32_t  cap;
  arena_t*  arena;
} VMFormatBuf;

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

/* ========================================================================
 * Types — runtime.c
 * ======================================================================== */

#define WORKER_IDLE ((uintptr_t)0)
#define WORKER_BUSY ((uintptr_t)1)

typedef struct {
    void (*fn)(void *data);
    void *data;
    JaclVal gc_root;
    JaclVal gc_root2;
    JaclVal gc_root3;
} RuntimeTask;

/* Per-worker counters (writer is the worker thread itself; readers are the
 * perf-snapshot API which reads after all workers are quiesced). All updates
 * are relaxed atomics for cross-thread visibility under TSAN. */
typedef struct {
    uint64_t tasks_executed;
    uint64_t steal_attempts;     /* one increment per pass through the steal loop */
    uint64_t steal_successes;
    uint64_t idle_sleep_ns;      /* cumulative time spent in backoff sleep */
    uint64_t inbox_pops;         /* tasks pulled from the global inbox */
    uint64_t pinned_inbox_pops;  /* tasks pulled from own pinned inbox */
} WorkerStats;

/* Runtime-wide GC cycle counters (writer is gc_concurrent_collect, called
 * serially via gc_running CAS; readers are the perf-snapshot API). */
typedef struct {
    uint64_t total_cycles;        /* concurrent_collect invocations */
    uint64_t total_cycle_ns;      /* sum of wall-clock cycle durations */
    uint64_t max_cycle_ns;        /* worst observed cycle duration */
    uint64_t total_mark_rounds;   /* sum of convergence-loop rounds */
} GCStats;

typedef struct Runtime Runtime;

/* DUAL DEFINITION: this struct is also defined in src/runtime.c for the
 * unity build. Any field change MUST be applied to both — and listed in
 * src/struct_drift_fields.h. See runtime.c for the rationale and
 * NOT_IMPLEMENTED.md §11 for the consolidation refactor that would
 * eliminate this duplication. */
typedef struct WorkerThread {
    rt_deque_deque    *public_deque;
    rt_deque_deque    *private_deque;
    GreyBuffer         grey_buf;
    RememberedSet      remembered_set;
    uintptr_t          currently_executing; /* atomic — see runtime.c */
    uint64_t           thread_epoch;         /* atomic */
    VM                 vm;
    Runtime           *runtime;
    int                id;
    thread_t           thread;
    arena_t            arena;
    int               *steal_ids;
    /* Pinned inbox: other workers route tasks here when escape analysis pins
     * a closure to this worker. The owner drains this into private_deque at
     * the top of each loop iteration, preserving private_deque's SPSC
     * contract. Mutex-protected MPSC. MUST stay in sync with runtime.c. */
    uintptr_t         *pinned_inbox;
    intptr_t           pinned_inbox_count;  /* atomic — MPSC counter */
    intptr_t           pinned_inbox_cap;
    platform_mutex_t   pinned_inbox_mutex;
    /* Retired tasks: epoch-deferred free list. See runtime.c for invariant. */
    RuntimeTask      **retired_tasks;
    uint64_t          *retired_epochs;
    uint32_t           retired_count;
    uint32_t           retired_cap;
    /* Perf counters — see WorkerStats. */
    WorkerStats        stats;
} WorkerThread;

struct Runtime {
    WorkerThread       *workers;
    int                 num_workers;
    uint64_t            global_epoch;   /* atomic — incremented at each GC start */
    uint32_t            gc_running;     /* atomic — CAS for GC entry */
    uint32_t            gc_active;      /* atomic — set during mark phase */
    BlockPool           block_pool;
    int                 shutdown;       /* atomic — shutdown signal */
    uintptr_t          *inbox;
    intptr_t            inbox_count;    /* atomic — MPSC counter */
    intptr_t            inbox_cap;
    platform_mutex_t    inbox_mutex;
    /* Wakeup CV — see runtime.c. MUST stay in sync. */
    platform_cond_t     work_cv;
    /* GC scanner thief slots — see runtime.c. MUST stay in sync. */
    int                *gc_thief_public_ids;
    int                *gc_thief_private_ids;
    /* External GC roots — JaclVals pinned by C embedders (e.g. completion
     * futures held by polling threads). Scanned by gc_enumerate_roots so
     * concurrent GC doesn't sweep values held only by raw C pointers. */
    JaclVal            *external_roots;
    uint32_t            external_root_count;
    uint32_t            external_root_cap;
    platform_mutex_t    external_roots_mutex;
    /* Sleep-timer state — deadline-sorted list polled by idle workers.
     * No dedicated thread; runtime__poll_timers runs on the worker loop.
     * See runtime.c. */
    platform_mutex_t    timer_mutex;
    struct TimerEntry  *timer_head;
    /* Perf counters — see GCStats. */
    GCStats             gc_stats;
};

extern JACL_THREAD_LOCAL int rt__worker_id;
extern JACL_THREAD_LOCAL WorkerThread *rt__current_worker;

/* ========================================================================
 * Types — embed.c
 * ======================================================================== */

typedef struct JaclVM_s JaclVM;
typedef struct JaclTrampoline_s JaclTrampoline;

typedef struct {
  size_t   initial_heap_size;
  size_t   max_heap_size;
  uint32_t max_handles;
} JaclConfig;

#define TRAMP_TYPE_VOID    0
#define TRAMP_TYPE_I32     1
#define TRAMP_TYPE_I64     2
#define TRAMP_TYPE_U32     3
#define TRAMP_TYPE_U64     4
#define TRAMP_TYPE_F32     5
#define TRAMP_TYPE_F64     6
#define TRAMP_TYPE_PTR     7
#define TRAMP_TYPE_UNKNOWN 255

#ifdef JACL_HAS_LIBFFI
#include <ffi.h>
struct JaclTrampoline_s {
  JaclVM*         jvm;
  JaclVal         closure;
  uint32_t        handle_idx;
  void*           ffi_closure;
  void*           code_ptr;
  ffi_cif         cif;
  ffi_type**      ffi_arg_types;
  int             arg_types_id[16];
  int             arg_count;
  int             ret_type_id;
  JaclTrampoline* vm_next;
};
#else
struct JaclTrampoline_s { int _unused; };
#endif

typedef JaclVal (*EmbedNativeFn)(JaclVM* vm, JaclVal* args, int argc);

typedef struct {
  EmbedNativeFn fn;
  JaclVal       name;
  int8_t        arity;
} NativeFnEntry;

#define NATIVE_FN_INIT_CAP 32

struct JaclVM_s {
  VM              vm;
  arena_t         arena;
  JaclInternTable intern_table;
  uint32_t        max_handles;
  const char*     last_error;
  JaclVal*        handle_slots;
  uint32_t*       handle_free_list;
  uint32_t        handle_count;
  uint32_t        handle_free_top;
  NativeFnEntry*  native_fns;
  int8_t*         native_fn_arities;
  uint32_t        native_fn_count;
  uint32_t        native_fn_cap;
  StructTypeRegistry* persistent_struct_registry;
  JaclTrampoline* trampoline_list;
};

typedef enum {
  EMBED_TYPE_DYN = 0,
  EMBED_TYPE_BOOL,
  EMBED_TYPE_NIL,
  EMBED_TYPE_I32,
  EMBED_TYPE_I64,
  EMBED_TYPE_U32,
  EMBED_TYPE_U64,
  EMBED_TYPE_F32,
  EMBED_TYPE_F64,
  EMBED_TYPE_STR,
  EMBED_TYPE_VEC,
  EMBED_TYPE_MAP,
  EMBED_TYPE_CLOSURE,
  EMBED_TYPE_STRUCT,
  EMBED_TYPE_NATIVE_FN
} EmbedJaclType;

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
extern JaclVal jacl_closure_ptr (void *p);
extern JaclVal jacl_bignum_ptr (void *p);
extern void *jacl_as_ptr (JaclVal v);
extern bool jacl_is_string (JaclVal v);
extern bool jacl_is_vector (JaclVal v);
extern bool jacl_is_map (JaclVal v);
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
extern void parser__sync_paren (Parser *p);
extern int parser__is_operator (Token *tok);
extern AstNode *parser__maybe_arrow_access (Parser *p, AstNode *expr);
extern AstNode *parser__parse_infix_operand (Parser *p);
extern AstNode *parser__parse_infix (Parser *p);
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
extern int parser__lookahead_is_destructure_binding (Parser *p);
extern AstNode *parser__parse_destructure_vec_pattern (Parser *p);
extern int parser__lookahead_is_named_destructure_binding (Parser *p);
extern AstNode *parser__parse_destructure_named_pattern (Parser *p);
extern AstNode *parser__parse_cmd_operand (Parser *p);
extern AstNode *parser__parse_cmd_expr (Parser *p);
extern AstNode *parser__parse_block (Parser *p);
extern AstNode *parser__parse_interp_string (Parser *p);
extern ParseResult parser_parse (LexResult tokens, arena_t *arena);

/* --- bytecode.c --- */
extern void chunk_init (BytecodeChunk *chunk, arena_t *arena);
extern void bytecode__grow_code (BytecodeChunk *chunk);
extern void bytecode__grow_constants (BytecodeChunk *chunk);
extern void chunk_write (BytecodeChunk *chunk, uint8_t byte, uint32_t line);
extern void chunk_write_u16 (BytecodeChunk *chunk, uint16_t value, uint32_t line);
extern uint16_t chunk_add_constant (BytecodeChunk *chunk, JaclVal value);
extern JaclVal jacl_closure (JaclClosure *cl);
extern JaclClosure *jacl_as_closure (JaclVal v);
extern const char *bytecode__opcode_name (uint8_t op);

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
 * (single-file compile, macro pre-typing, typer-only test harness). */
extern void typer_infer (AstNode **nodes, uint32_t count,
                         TyperResult *result_or_null,
                         TyperImportProc *imports, uint32_t import_count);

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

/* --- compiler.c --- */
extern bool is_type_keyword (const char *word, size_t len);
extern JaclType type_from_keyword (const char *word, size_t len);
extern const char *type_name (JaclType t);
extern bool is_numeric_type (JaclType t);
extern bool is_unboxed_type (JaclType t);
extern uint32_t struct__type_size (JaclType t, StructTypeRegistry *reg, uint32_t struct_idx);
extern uint32_t struct__type_align (JaclType t, StructTypeRegistry *reg, uint32_t struct_idx);
extern uint32_t struct__align_up (uint32_t offset, uint32_t align);
extern uint32_t struct_registry__find (StructTypeRegistry *reg, const char *name, uint32_t name_len);
extern uint32_t compiler__register_inline_struct (StructTypeRegistry *reg, const char *spec, uint32_t spec_len);
extern void module_cache__init (ModuleCache *cache, arena_t *arena);
extern Module *module_cache__find (ModuleCache *cache, const char *canonical_path);
extern Module *module_cache__add (ModuleCache *cache, const char *canonical_path);
extern void import_stack__init (ImportStack *stack);
extern bool import_stack__contains (ImportStack *stack, const char *canonical_path);
extern bool import_stack__push (ImportStack *stack, const char *canonical_path);
extern void import_stack__pop (ImportStack *stack);
extern const char *import_stack__chain_str (ImportStack *stack, const char *cycle_path, arena_t *arena);
extern const char *module__resolve_path (const char *importer_path, const char *use_path, arena_t *arena);
extern bool module__is_private (const char *name, uint32_t name_len);
extern char *module__read_file (const char *path, arena_t *arena);
extern bool suspension_map_lookup (SuspensionMap *map, JaclVal name);
extern bool suspension_map_is_generator (SuspensionMap *map, JaclVal name);
extern void suspension_map_set (SuspensionMap *map, JaclVal name, bool suspends, bool is_generator);
extern void analyze__walk_body (AstNode *node, ProcSuspendInfo *info, ThreadHeap *heap, JaclInternTable *intern_table);
extern void analyze__collect_procs (AstNode *node, ProcSuspendInfoList *list, ThreadHeap *heap, JaclInternTable *intern_table);
extern SuspensionMap compiler__analyze_suspension (AstNode **nodes, uint32_t count, ThreadHeap *heap, JaclInternTable *intern_table);
extern void sm__walk_suspensions (AstNode *node, SuspensionAnalysis *analysis, SuspensionMap *map, ThreadHeap *heap, JaclInternTable *intern_table);
extern void sm__add_state_field (StateLayout *layout, JaclVal name, bool is_mutable, bool is_param, uint16_t width, uint32_t struct_type_idx);
extern const StateField* sm__get_field (const StateLayout *layout, JaclVal name);
extern int sm__find_field (const StateLayout *layout, JaclVal name);
extern bool sm__is_field_mutable (const StateLayout *layout, JaclVal name);
extern void sm__collect_destructure_vec_names (AstNode *dv, StateLayout *layout, bool is_mutable);
extern void sm__collect_destructure_named_names (AstNode *dn, StateLayout *layout, bool is_mutable);
extern void sm__collect_command_destructure_names (AstNode *pat, StateLayout *layout, bool is_mutable);
extern void sm__collect_block_destructure_names (AstNode *blk, StateLayout *layout, bool is_mutable);
extern void sm__walk_locals (AstNode *node, StateLayout *layout, StructTypeRegistry *reg);
extern void sm__liveness_mark_write (FieldLiveness *liveness, const StateLayout *layout, JaclVal name, int32_t segment);
extern void sm__liveness_mark_read (FieldLiveness *liveness, const StateLayout *layout, JaclVal name, int32_t segment);
extern JaclVal sm__lit_string_name (AstNode *node);
extern void sm__liveness_mark_binding_names (AstNode *pattern, const StateLayout *layout, FieldLiveness *liveness, int32_t segment);
extern bool sm__loop_body_suspends (AstNode *body);
extern void sm__liveness_walk (AstNode *node, const StateLayout *layout, FieldLiveness *liveness, int32_t *segment);
extern void sm__optimize_state_layout (SuspensionAnalysis *analysis, AstNode *body);
extern SuspensionAnalysis compiler__analyze_suspensions (AstNode *body, JaclVal *param_names, uint8_t param_count, bool optimize_liveness, SuspensionMap *map, ThreadHeap *heap, JaclInternTable *intern_table, StructTypeRegistry *struct_reg);
extern bool ast__contains_suspension (AstNode *node, SuspensionMap *map);
extern void ast__collect_local_muts (AstNode *node, JaclVal *names, uint32_t *count);
extern bool ast__contains_nonlocal_set_impl (AstNode *node, JaclVal *local_muts, uint32_t local_mut_count);
extern bool ast__contains_nonlocal_set (AstNode *block);
extern void compiler__init (Compiler *c, BytecodeChunk *chunk, arena_t *arena, JaclInternTable *intern_table, ThreadHeap *heap);
extern const char *module__build_prefix (const char *canonical_path, arena_t *arena, uint32_t *out_len);
extern JaclVal compiler__global_name_val (Compiler *c, const char *name, uint32_t name_len);
extern void compiler__emit_byte (Compiler *c, uint8_t byte, uint32_t line);
extern void compiler__emit_u16 (Compiler *c, uint16_t value, uint32_t line);
extern void compiler__emit_constant (Compiler *c, JaclVal value, uint32_t line);
extern void compiler__error (Compiler *c, uint32_t line, uint32_t col, const char *message);
extern void compiler__begin_scope (Compiler *c);
extern void compiler__end_scope (Compiler *c, uint32_t line);
extern void compiler__add_local (Compiler *c, JaclVal name, uint32_t line, uint32_t col);
extern int compiler__resolve_local (Compiler *c, JaclVal name);
extern GlobalArity *compiler__find_global (Compiler *c, JaclVal name);
extern void compiler__set_global_arity (Compiler *c, JaclVal name, int16_t arity);
extern StructTypeRegistry *compiler__get_struct_registry (Compiler *c);
extern bool compiler__resolve_type (Compiler *c, const char *word, uint32_t len, JaclType *out_type);
extern bool compiler__is_type_annotation (Compiler *c, const char *word, uint32_t len);
extern int compiler__add_upvalue (Compiler *c, uint8_t index, uint8_t is_local, JaclVal name);
extern int compiler__resolve_upvalue (Compiler *c, JaclVal name);
extern void ast__collect_local_names (AstNode *node, JaclVal *names, uint32_t *count);
extern bool compiler__name_touches_mutable (Compiler *enclosing, JaclVal name);
extern bool ast__refs_nonlocal_mutable_impl (AstNode *node, JaclVal *local_names, uint32_t local_name_count, Compiler *enclosing);
extern bool compiler__body_captures_mutable (Compiler *enclosing, AstNode *body_block);
extern uint32_t compiler__emit_jump (Compiler *c, uint8_t instruction, uint32_t line);
extern void compiler__patch_jump (Compiler *c, uint32_t offset);
extern void compiler__emit_check_error (Compiler *c, uint32_t line);
extern void compiler__emit_sm_dispatch_table (Compiler *c, uint32_t suspension_count, uint32_t line);
extern void compiler__compile_sm_stmts (Compiler *c, AstNode **stmts, uint32_t count, uint32_t line, bool return_last_value);
extern void compiler__compile_sm_body (Compiler *c, AstNode *body_block, uint32_t line);
extern void compiler__compile_parallel_body (Compiler *c, AstNode *body_block, uint32_t line, uint32_t col);
extern void compiler__compile_block_expr (Compiler *c, AstNode *block_node);
extern int compiler__head_matches (AstNode *head, const char *name, uint32_t len);
extern int16_t compiler__node_known_arity (Compiler *c, AstNode *node);
extern void compiler__builtin_arity_error (Compiler *c, uint32_t line, uint32_t col, const char *name, const char *expected_desc, uint32_t got);
extern void compiler__ensure_boxed (Compiler *c, uint32_t line);
extern uint8_t compiler__typed_op (uint8_t dyn_op, JaclType type);
extern void compiler__compile_binary (Compiler *c, AstNode **args, uint8_t op, const char *op_verb, uint32_t line, uint32_t col);
extern void compiler__compile_hof_builtin (Compiler *c, const char *name, AstNode **args, uint32_t argc, uint8_t opcode, uint32_t line, uint32_t col);
extern void compiler__compile_destructure_vec (Compiler *c, const char **d_names, uint32_t *d_name_lens, const char **d_types, uint32_t *d_type_lens, uint32_t d_count, const char *rest_name, uint32_t rest_name_len, AstNode *value_expr, bool is_mutable, uint32_t line, uint32_t col);
extern void compiler__compile_destructure_named (Compiler *c, const char **d_names, uint32_t *d_name_lens, const char **d_types, uint32_t *d_type_lens, uint32_t d_count, const char *rest_name, uint32_t rest_name_len, int spread_all, AstNode *value_expr, bool is_mutable, uint32_t line, uint32_t col);
extern void compiler__rewrite_binding_op (Compiler *c, AstNode *node, const char *target, uint32_t target_len);
extern void compiler__compile_pipe_op (Compiler *c, AstNode *node);
extern void compiler__compile_command (Compiler *c, AstNode *node);
extern void compiler__compile_node (Compiler *c, AstNode *node);
extern bool compiler__top_level_suspends (AstNode **stmts, uint32_t count, SuspensionMap *map);
extern bool compiler__is_core_builtin (const char *name, uint32_t len);
extern const char *jacl_non_core_builtins[];  /* NULL-terminated list */
extern CompileResult compiler_compile (ParseResult parse, arena_t *arena, JaclInternTable *intern_table, ThreadHeap *heap, StructTypeRegistry *seed_registry, ExpandState *es, JaclVal prelude_map);
extern void macro_table_init (MacroTable *t);
extern MacroEntry *macro_table_lookup (MacroTable *t, const char *name, uint32_t name_len);
extern bool macro__is_special_form (const char *name, uint32_t len);
extern void module__populate_exports (Module *mod, Compiler *c);
extern bool compiler__compile_module (const char *canonical_path, Compiler *importer, uint32_t line, uint32_t col);
extern ProgramResult jacl_compile_program (const char *root_path, arena_t *arena, JaclInternTable *intern_table, ThreadHeap *heap);

/* --- vm.c --- */
extern void vm__emergency_gc_single (void *ctx);
extern const char *vm__type_name (JaclVal v);
extern void vm__set_error (VM *vm, const char *fmt, ...);
extern void vm__default_print (const char *text, uint32_t len, void *ctx);
extern bool vm__is_falsy (JaclVal v);
extern void vm__capture_trace (VM *vm);
extern void ctx_pool_init (JaclCtxPool *pool, ThreadHeap *heap, StructTypeRegistry *reg);
extern HeapRecord *ctx_pool_alloc (JaclCtxPool *pool, ThreadHeap *heap);
extern void ctx_pool_free (JaclCtxPool *pool, HeapRecord *s);
extern void ctx__init_vm (VM *vm, JaclCtxPool *pool_storage);
extern void vm_init (VM *vm, arena_t *arena);
extern void vm_destroy (VM *vm);
extern VMResult vm__push (VM *vm, JaclVal value);
extern VMResult vm__pop (VM *vm, JaclVal *out);
extern uint8_t vm__read_byte (VM *vm);
extern uint16_t vm__read_u16 (VM *vm);
extern void vm__env_grow (VM *vm);
extern void vm__env_set (VM *vm, JaclVal name, JaclVal value);
extern JaclVal vm__env_get (VM *vm, JaclVal name, bool *found);
extern void vm__fmt_init (VMFormatBuf *buf, arena_t *arena);
extern void vm__fmt_ensure (VMFormatBuf *buf, uint32_t extra);
extern void vm__fmt_append (VMFormatBuf *buf, const char *str, uint32_t len);
extern void vm__fmt_value (VMFormatBuf *buf, JaclVal val);
extern bool vm__deep_eq (JaclVal a, JaclVal b);
extern JaclVal vm__heap_record_read_field (ThreadHeap *heap, HeapRecord *s, uint32_t offset, int field_type);
extern void vm__heap_record_write_field (HeapRecord *s, uint32_t offset, int field_type, JaclVal val);
extern VMResult vm_exec (VM *vm, BytecodeChunk *chunk);
extern StreamPullResult vm__pull_stream_one (VM *vm, JaclVal stream_val, JaclVal *out_value);
extern VMResult vm__run (VM *vm, uint32_t min_frame);
extern VMResult jacl_exec_program (ProgramResult *program, VM *vm);
extern VMResult jacl_run (const char *source, VM *vm, arena_t *arena);

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
extern JaclVal         jacl_ctx_run_source (jacl_context_t *ctx, const char *src, size_t len, uint64_t restriction_set, JaclError *err_out);
extern JaclVal         jacl_ctx_run_closure (jacl_context_t *ctx, JaclClosure *closure, JaclVal *args, uint32_t arg_count, JaclError *err_out);

/* Compile source into a closure on the caller's heap without creating a
 * jacl_context_t.  Returns a closure JaclVal on success, or JACL_NIL with
 * err_out populated on lex/parse/compile error. */
extern JaclVal source_to_closure_in_place(const char *src, size_t len,
                                          arena_t *arena, ThreadHeap *heap,
                                          JaclInternTable *intern_table,
                                          ExpandState *expand,
                                          JaclError *err_out,
                                          JaclVal prelude_map);

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
extern void jacl_ctx_enter   (jacl_context_t *ctx, jacl_ctx_saved_t *saved);
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
extern void gc_mark (ThreadHeap *heap, VM *vm);
extern size_t gc_sweep (ThreadHeap *heap);
extern bool gc__ptr_in_heap (ThreadHeap *heap, void *ptr);
/* watermark != 0 enables concurrent-GC epoch protection: entries with
 * hdr->epoch >= watermark are kept alive (freshly interned, marker did not
 * observe them). Pass 0 from single-threaded callers. */
extern void gc_sweep_intern_table (JaclInternTable *table, ThreadHeap *heap, uint32_t watermark);
extern void gc__adjust_threshold (ThreadHeap *heap, size_t bytes_survived);
extern void gc_collect (ThreadHeap *heap, VM *vm);
extern void gc_mark_minor (ThreadHeap *heap, VM *vm, RememberedSet *remembered_set);
extern size_t gc_sweep_minor (ThreadHeap *heap);
extern void gc_collect_minor (ThreadHeap *heap, VM *vm, RememberedSet *remembered_set);
extern bool gc_should_major (ThreadHeap *heap);
extern size_t gc_sweep_concurrent (ThreadHeap *heap, GCBlock *skip_block, uint32_t watermark, uint8_t current_mark, BlockPool *pool);

/* --- runtime.c --- */
extern void runtime__init_worker_vm (WorkerThread *w);
extern void runtime__emergency_gc (void *ctx);
extern void *runtime__worker_loop (void *arg);
extern void runtime_init (Runtime *rt, int num_workers);
extern void runtime_destroy (Runtime *rt);
/* Lower-level init: set up Runtime state WITHOUT spawning worker threads.
 * Tests that drive the state machine deterministically use these. Production
 * code should use runtime_init / runtime_destroy. */
extern void runtime__init_state (Runtime *rt, int num_workers);
extern void runtime__start_threads (Runtime *rt);
extern void runtime__stop_threads (Runtime *rt);
extern void runtime__teardown_state (Runtime *rt);
extern void runtime__push_inbox (Runtime *rt, RuntimeTask *task);
extern void runtime__push_pinned (Runtime *rt, RuntimeTask *task, int worker_id);
extern void runtime_submit (Runtime *rt, void (*fn)(void *), void *data);
/* Pin a JaclVal as a GC root from outside any GC-scanned structure. Required
 * for completion futures held only by raw C pointers across GC cycles. */
extern uint32_t runtime_pin_value (Runtime *rt, JaclVal val);
extern void     runtime_unpin_value (Runtime *rt, uint32_t handle);
extern void runtime__setup_call (VM *vm, JaclClosure *cl, int argc, JaclVal *argv);
extern void runtime__exec_closure (void *data);
extern void runtime_submit_task (Runtime *rt, JaclClosure *closure, bool thread_local);
extern void gc__scan_deque (rt_deque_deque *dq, GCMarkStack *ms, int thief_id);
extern void gc_enumerate_roots (Runtime *rt, GCMarkStack *ms);
extern bool gc__drain_grey_bufs (Runtime *rt, GCMarkStack *ms, uint32_t *drained);
extern void gc_concurrent_collect (Runtime *rt);

/* --- Perf snapshot (runtime.c) ---
 * One struct, summed across workers. Reads after THREAD_JOIN / quiesce. */
typedef struct {
    int      num_workers;
    /* GC cycle stats */
    GCStats  gc;
    /* Allocation stats summed across worker heaps */
    uint64_t total_bytes_allocated;
    uint64_t total_allocs;
    uint64_t slow_path_allocs;
    /* §14 instrumentation: per-call work done in gc_alloc's slow path.
     * blocks_scanned counts blocks that passed the max_free_run_lines
     * reject and entered the in-block line scan. blocks_rejected_fast
     * counts cheap O(1) rejects. lines_scanned tallies line_map bytes
     * inspected inside the inner scan. blocks_scanned +
     * blocks_rejected_fast = total outer-loop iterations. */
    uint64_t blocks_scanned;
    uint64_t lines_scanned;
    uint64_t blocks_rejected_fast;
    /* Worker stats summed across workers */
    WorkerStats workers_total;
    /* Instantaneous state */
    uint32_t current_heap_blocks;
    intptr_t current_inbox_depth;
} JaclPerfSnapshot;

extern JaclPerfSnapshot jacl_perf_snapshot (Runtime *rt);
extern void jacl_perf_snapshot_print_json (FILE *out, const JaclPerfSnapshot *snap);
extern void gc__concurrent_task (void *data);
extern void gc_concurrent_trigger (void *runtime_ptr);
extern JaclVal runtime__create_resolve_closure (ThreadHeap *heap, arena_t *arena, JaclVal future_val);
extern JaclVal runtime__create_parallel_k (ThreadHeap *heap, arena_t *arena, JaclVal agg_val, uint32_t index);
extern void runtime__complete_parallel_slot (void *runtime_ptr, VM *vm, JaclVal agg_val, uint32_t index, JaclVal task_result);
extern JaclVal runtime__create_race_k (ThreadHeap *heap, arena_t *arena, JaclVal agg_val);
extern void runtime__complete_race_slot (void *runtime_ptr, VM *vm, JaclVal agg_val, JaclVal task_result);
extern void runtime__continuation_task_exec (void *data);
extern void runtime__schedule_continuation (void *runtime_ptr, JaclClosure *continuation, JaclVal result);
extern void runtime__state_machine_task_exec (void *data);
extern void runtime__schedule_sm_resumption (void *runtime_ptr, JaclVal state_machine, JaclVal result);
extern void runtime__schedule_waiters (void *runtime_ptr, FutureWaiter *waiters, JaclVal result);
extern void runtime__spawn_task_exec (void *data);
extern void runtime__submit_spawn_task (void *runtime_ptr, JaclClosure *closure, JaclVal future_val, JaclVal parent_ctx);
extern void runtime__parallel_task_exec (void *data);
extern void runtime__submit_parallel_task (void *runtime_ptr, JaclClosure *closure, JaclVal agg_val, uint32_t index, JaclVal parent_ctx);
extern void runtime__race_task_exec (void *data);
extern void runtime__submit_race_task (void *runtime_ptr, JaclClosure *closure, JaclVal agg_val, JaclVal parent_ctx);
extern VMResult rt_run_to_completion (Runtime *rt, JaclClosure *closure, arena_t *arena);

/* --- embed.c --- */
extern JaclVal embed__call_native (void *ctx, uint32_t fn_index, JaclVal *args, int argc);
extern JaclVM *jacl_vm_new (void);
extern JaclVM *jacl_vm_new_ex (const JaclConfig *config);
extern void jacl_vm_free (JaclVM *vm);
extern JaclVal embed__make_error (JaclVM *jvm, const char *msg);
extern JaclVal jacl_eval (JaclVM *jvm, const char *source);
extern JaclVal jacl_eval_file (JaclVM *jvm, const char *path);
extern bool jacl_is_error_val (JaclVal v);
extern const char *jacl_error_message_str (JaclVM *jvm, JaclVal err);
extern JaclVal jacl_nil_val (void);
extern JaclVal jacl_bool_val (bool b);
extern JaclVal jacl_i32_val (int32_t n);
extern JaclVal jacl_i64_val (JaclVM *jvm, int64_t n);
extern JaclVal jacl_u32_val (uint32_t n);
extern JaclVal jacl_u64_val (JaclVM *jvm, uint64_t n);
extern JaclVal jacl_f32_val (float f);
extern JaclVal jacl_f64_val (JaclVM *jvm, double d);
extern JaclVal jacl_string_val (JaclVM *jvm, const char *s, size_t len);
extern JaclVal jacl_string_cstr_val (JaclVM *jvm, const char *s);
extern int32_t jacl_as_i32_val (JaclVal val);
extern int64_t jacl_as_i64_val (JaclVal val);
extern uint32_t jacl_as_u32_val (JaclVal val);
extern uint64_t jacl_as_u64_val (JaclVal val);
extern float jacl_as_f32_val (JaclVal val);
extern double jacl_as_f64_val (JaclVal val);
extern bool jacl_as_bool_val (JaclVal val);
extern const char *jacl_as_cstr_val (JaclVM *jvm, JaclVal val, size_t *len_out);
extern int jacl_typeof_val (JaclVal val);
extern uint32_t embed__register_native (JaclVM *jvm, const char *name, EmbedNativeFn fn, int8_t arity);
extern bool jacl_register_fn_val (JaclVM *jvm, const char *name, EmbedNativeFn fn, int arity);
extern EmbedJaclHandle jacl_handle_new_val (JaclVM *jvm, JaclVal val);
extern JaclVal jacl_handle_get_val (JaclVM *jvm, EmbedJaclHandle h);
extern void jacl_handle_free_val (JaclVM *jvm, EmbedJaclHandle h);
extern JaclVal jacl_call_val (JaclVM *jvm, JaclVal fn, JaclVal *args, int argc);
extern JaclVal jacl_call_named_val (JaclVM *jvm, const char *name, JaclVal *args, int argc);
extern bool embed__val_matches_field_type (JaclVal val, int field_type);
extern JaclVal embed__heap_record_read_field (JaclVM *jvm, HeapRecord *s, uint32_t offset, int field_type);
extern void embed__heap_record_write_field (HeapRecord *s, uint32_t offset, int field_type, JaclVal val);
extern JaclVal jacl_struct_new_val (JaclVM *jvm, const char *type_name, JaclVal *fields, int count);
extern JaclVal jacl_struct_get_val (JaclVM *jvm, JaclVal s_val, const char *field_name);
extern bool jacl_struct_set_val (JaclVM *jvm, JaclVal s_val, const char *field_name, JaclVal value);
extern bool jacl_has_trampolines (void);
extern JaclTrampoline *jacl_trampoline_new_val (JaclVM *jvm, JaclVal closure, const char *sig);
extern void *jacl_trampoline_ptr_val (JaclTrampoline *t);
extern void jacl_trampoline_free_val (JaclVM *jvm, JaclTrampoline *t);
extern void embed__free_all_trampolines (JaclVM *jvm);
extern const char *jacl_struct_type_name_val (JaclVM *jvm, JaclVal s_val);

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

/* --- Struct size consistency checks (embed.c) ---
 * Returns sizeof(Type) from the unity build's .c definitions.
 * Tests compiled against this header compare these against their own sizeof()
 * to detect struct definition drift between .c files and jacl.h. */
extern size_t jacl__sizeof_vm (void);
extern size_t jacl__sizeof_compiler (void);
extern size_t jacl__sizeof_compile_result (void);
extern size_t jacl__sizeof_struct_type_field (void);
extern size_t jacl__sizeof_struct_type_def (void);
extern size_t jacl__sizeof_struct_type_registry (void);
extern size_t jacl__sizeof_state_layout (void);
extern size_t jacl__sizeof_suspension_analysis (void);
extern size_t jacl__sizeof_call_frame (void);
extern size_t jacl__sizeof_ctx_pool (void);
extern size_t jacl__sizeof_environment (void);
extern size_t jacl__sizeof_stack_trace (void);
extern size_t jacl__sizeof_stack_trace_entry (void);

/* Runtime / GC structs added after the AUDIT.md fixes — see embed.c. */
extern size_t jacl__sizeof_worker_thread (void);
extern size_t jacl__sizeof_runtime (void);
extern size_t jacl__sizeof_runtime_task (void);
extern size_t jacl__sizeof_thread_heap (void);
extern size_t jacl__sizeof_grey_buffer (void);
extern size_t jacl__sizeof_remembered_set (void);

/* Per-field offset getters for Runtime + WorkerThread, generated from
 * src/struct_drift_fields.h. test_struct_sizes.c compares these (returning
 * the unity-build view) against offsetof from the jacl.h view (same struct
 * names, used here). Any drift between the two definitions fails the test. */
#include "struct_drift_fields.h"
#define JACL_DRIFT_DECLARE_OFFSETOF(STRUCT, FIELD) \
    extern size_t jacl__offsetof_##STRUCT##_##FIELD (void);
RUNTIME_FIELDS(JACL_DRIFT_DECLARE_OFFSETOF)
WORKER_FIELDS(JACL_DRIFT_DECLARE_OFFSETOF)
#undef JACL_DRIFT_DECLARE_OFFSETOF

extern size_t jacl__offsetof_runtime_task_gc_root3 (void);

#endif /* JACL_H */
