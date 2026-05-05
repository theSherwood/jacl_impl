/*
 * JACL AST Node Types
 *
 * Defines the Abstract Syntax Tree node types produced by the parser.
 * All AstNode structs are arena-allocated — no individual free required.
 */

#ifndef AST_C
#define AST_C

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * AST Node Types
 * ------------------------------------------------------------------------- */

typedef enum {
  AST_COMMAND,       /* [cmd arg1 arg2] or bare command */
  AST_LIT_INT,       /* integer literal: 42, 0xFF, 0b1010 */
  AST_LIT_FLOAT,     /* float literal: 3.14 */
  AST_LIT_STRING,    /* string literal: "hello" or bare word */
  AST_VAR_REF,       /* variable reference: $name */
  AST_BLOCK,         /* code block: { cmd1; cmd2 } */
  AST_INTERP_STRING, /* interpolated string: "hello $name" */
  AST_USE,           /* use "path" binding OR use "path" {name1, name2} */
  AST_DEFSTRUCT,     /* defstruct Name [field :type] ... */
  AST_DEFMACRO,      /* defmacro name {params} {body} */
  AST_QUOTE,         /* quote <expr> — unevaluated syntax */
  AST_SYNTAX_QUOTE,  /* syntax-quote <expr> — template with unquote holes */
  AST_UNQUOTE,       /* ~<expr> inside syntax-quote */
  AST_UNQUOTE_SPLICING, /* ~@<expr> inside syntax-quote */
  AST_BREAK,         /* break or break $value */
  AST_CONTINUE,      /* continue */
  AST_RETURN,        /* return or return $value */
  AST_DESTRUCTURE_VEC, /* [a b c] positional destructuring pattern */
  AST_DESTRUCTURE_NAMED, /* {x, y} named struct/map destructuring pattern */
  AST_SPREAD,        /* ..expr spread in command args */
  AST_SHELL_CMD,     /* !cmd args... external shell command */
  AST_CTX_DECL,      /* ctx [mut] Type name = default_expr */
  AST_ERROR          /* parse error with recovery */
} AstNodeType;

/* -------------------------------------------------------------------------
 * HeadId — interned identifier for well-known command head names.
 *
 * Most AST_COMMAND nodes have a head whose value is a fixed bare word
 * ("yield", "def", "+", "vec-get", ...). The compiler and several AST
 * walkers dispatch on those names via repeated `memcmp` chains. Stamping
 * `head_id` once at construction lets every consumer dispatch by integer
 * compare instead.
 *
 * HEAD_NONE is the catch-all: head is not AST_LIT_STRING (e.g. AST_VAR_REF
 * for `[$f x]`), or is a string outside this table (a user-defined call).
 * Any caller still needing the exact string can read it from the head node.
 * ------------------------------------------------------------------------- */

typedef enum {
  HEAD_NONE = 0,

  /* Operators */
  HEAD_PLUS, HEAD_MINUS, HEAD_STAR, HEAD_SLASH, HEAD_PERCENT,
  HEAD_LT, HEAD_GT, HEAD_LE, HEAD_GE, HEAD_EQ_EQ,
  HEAD_EQUALS, HEAD_COLON, HEAD_COLON_COLON,
  HEAD_PIPE, HEAD_PIPE_PIPE, HEAD_AMP_AMP, HEAD_TILDE,
  HEAD_DOTDOT_LT, HEAD_DOTDOT_EQ,
  HEAD_DOT, HEAD_QDOT,

  /* Bindings & defs */
  HEAD_DEF, HEAD_MUT, HEAD_SET, HEAD_PROC,
  HEAD_DEFSTRUCT, HEAD_DEFMACRO,

  /* Control flow */
  HEAD_IF, HEAD_WHILE, HEAD_FOR,
  HEAD_BREAK, HEAD_CONTINUE, HEAD_RETURN,
  HEAD_TRY, HEAD_WITH_CTX, HEAD_MATCH,

  /* Concurrency / suspension */
  HEAD_YIELD, HEAD_AWAIT, HEAD_SPAWN, HEAD_PARALLEL, HEAD_RACE,

  /* Vec ops */
  HEAD_VEC,
  HEAD_VEC_GET, HEAD_VEC_LEN, HEAD_VEC_PUSH, HEAD_VEC_SET,
  HEAD_VEC_CONCAT, HEAD_VEC_SLICE,

  /* Map ops */
  HEAD_MAP,
  HEAD_MAP_GET, HEAD_MAP_HAS, HEAD_MAP_LEN, HEAD_MAP_SET,
  HEAD_MAP_REMOVE, HEAD_MAP_KEYS, HEAD_MAP_VALS,

  /* Builtins */
  HEAD_PRINT, HEAD_LENGTH, HEAD_BYTE_LENGTH,
  HEAD_INDEX, HEAD_SLICE, HEAD_CONCAT,
  HEAD_HASH, HEAD_TO_STRING, HEAD_TRANSFORM, HEAD_FILTER,

  /* Errors */
  HEAD_ERROR, HEAD_ERROR_Q, HEAD_ERROR_VAL, HEAD_STACK_TRACE,

  /* Boxes / atoms / coercion */
  HEAD_BOX, HEAD_BOX_Q, HEAD_ATOM, HEAD_ATOM_Q, HEAD_FUTURE_Q,
  HEAD_DEREF, HEAD_UNBOX, HEAD_RESET, HEAD_SWAP, HEAD_TO,

  /* Streams / async / shell */
  HEAD_STREAM_NEXT, HEAD_COLLECT, HEAD_COUNT, HEAD_TAKE,
  HEAD_FIRST, HEAD_LINES, HEAD_EXEC, HEAD_SIGNAL, HEAD_CANCEL,

  /* Quote / syntax / macros */
  HEAD_QUOTE, HEAD_SYNTAX_QUOTE, HEAD_INTERPRET, HEAD_INTERPRET_PRELUDE,
  HEAD_SYNTAX_KIND, HEAD_SYNTAX_DATUM, HEAD_SYNTAX_HEAD,
  HEAD_SYNTAX_ARGS, HEAD_SYNTAX_COMMANDS, HEAD_SYNTAX_POS,
  HEAD_SYNTAX_STR, HEAD_MAKE_SYNTAX, HEAD_SYNTAX_ERROR,

  HEAD_ID_COUNT  /* sentinel; must fit in uint8_t */
} HeadId;

/* Map a bare-word head string to its HeadId, or HEAD_NONE if not in the
 * well-known set. Dispatched by length to keep the hot path branch-light. */
static HeadId ast__head_id_for(const char* s, uint32_t len) {
  if (!s || len == 0 || len > 17) return HEAD_NONE;
  switch (len) {
    case 1:
      switch (s[0]) {
        case '+': return HEAD_PLUS;
        case '-': return HEAD_MINUS;
        case '*': return HEAD_STAR;
        case '/': return HEAD_SLASH;
        case '%': return HEAD_PERCENT;
        case '<': return HEAD_LT;
        case '>': return HEAD_GT;
        case '=': return HEAD_EQUALS;
        case ':': return HEAD_COLON;
        case '|': return HEAD_PIPE;
        case '~': return HEAD_TILDE;
        case '.': return HEAD_DOT;
      }
      return HEAD_NONE;
    case 2:
      if (s[0] == '=' && s[1] == '=') return HEAD_EQ_EQ;
      if (s[0] == '<' && s[1] == '=') return HEAD_LE;
      if (s[0] == '>' && s[1] == '=') return HEAD_GE;
      if (s[0] == ':' && s[1] == ':') return HEAD_COLON_COLON;
      if (s[0] == '|' && s[1] == '|') return HEAD_PIPE_PIPE;
      if (s[0] == '&' && s[1] == '&') return HEAD_AMP_AMP;
      if (s[0] == '?' && s[1] == '.') return HEAD_QDOT;
      if (s[0] == 'i' && s[1] == 'f') return HEAD_IF;
      if (s[0] == 't' && s[1] == 'o') return HEAD_TO;
      return HEAD_NONE;
    case 3:
      if (memcmp(s, "..<", 3) == 0) return HEAD_DOTDOT_LT;
      if (memcmp(s, "..=", 3) == 0) return HEAD_DOTDOT_EQ;
      if (memcmp(s, "def", 3) == 0) return HEAD_DEF;
      if (memcmp(s, "mut", 3) == 0) return HEAD_MUT;
      if (memcmp(s, "set", 3) == 0) return HEAD_SET;
      if (memcmp(s, "for", 3) == 0) return HEAD_FOR;
      if (memcmp(s, "try", 3) == 0) return HEAD_TRY;
      if (memcmp(s, "vec", 3) == 0) return HEAD_VEC;
      if (memcmp(s, "map", 3) == 0) return HEAD_MAP;
      if (memcmp(s, "box", 3) == 0) return HEAD_BOX;
      return HEAD_NONE;
    case 4:
      if (memcmp(s, "proc", 4) == 0) return HEAD_PROC;
      if (memcmp(s, "atom", 4) == 0) return HEAD_ATOM;
      if (memcmp(s, "box?", 4) == 0) return HEAD_BOX_Q;
      if (memcmp(s, "race", 4) == 0) return HEAD_RACE;
      if (memcmp(s, "exec", 4) == 0) return HEAD_EXEC;
      if (memcmp(s, "take", 4) == 0) return HEAD_TAKE;
      if (memcmp(s, "hash", 4) == 0) return HEAD_HASH;
      if (memcmp(s, "swap", 4) == 0) return HEAD_SWAP;
      return HEAD_NONE;
    case 5:
      if (memcmp(s, "while", 5) == 0) return HEAD_WHILE;
      if (memcmp(s, "yield", 5) == 0) return HEAD_YIELD;
      if (memcmp(s, "await", 5) == 0) return HEAD_AWAIT;
      if (memcmp(s, "spawn", 5) == 0) return HEAD_SPAWN;
      if (memcmp(s, "match", 5) == 0) return HEAD_MATCH;
      if (memcmp(s, "break", 5) == 0) return HEAD_BREAK;
      if (memcmp(s, "print", 5) == 0) return HEAD_PRINT;
      if (memcmp(s, "slice", 5) == 0) return HEAD_SLICE;
      if (memcmp(s, "index", 5) == 0) return HEAD_INDEX;
      if (memcmp(s, "error", 5) == 0) return HEAD_ERROR;
      if (memcmp(s, "atom?", 5) == 0) return HEAD_ATOM_Q;
      if (memcmp(s, "deref", 5) == 0) return HEAD_DEREF;
      if (memcmp(s, "unbox", 5) == 0) return HEAD_UNBOX;
      if (memcmp(s, "reset", 5) == 0) return HEAD_RESET;
      if (memcmp(s, "first", 5) == 0) return HEAD_FIRST;
      if (memcmp(s, "lines", 5) == 0) return HEAD_LINES;
      if (memcmp(s, "count", 5) == 0) return HEAD_COUNT;
      if (memcmp(s, "quote", 5) == 0) return HEAD_QUOTE;
      return HEAD_NONE;
    case 6:
      if (memcmp(s, "return", 6) == 0) return HEAD_RETURN;
      if (memcmp(s, "filter", 6) == 0) return HEAD_FILTER;
      if (memcmp(s, "concat", 6) == 0) return HEAD_CONCAT;
      if (memcmp(s, "length", 6) == 0) return HEAD_LENGTH;
      if (memcmp(s, "error?", 6) == 0) return HEAD_ERROR_Q;
      if (memcmp(s, "signal", 6) == 0) return HEAD_SIGNAL;
      if (memcmp(s, "cancel", 6) == 0) return HEAD_CANCEL;
      return HEAD_NONE;
    case 7:
      if (memcmp(s, "vec-get", 7) == 0) return HEAD_VEC_GET;
      if (memcmp(s, "vec-len", 7) == 0) return HEAD_VEC_LEN;
      if (memcmp(s, "vec-set", 7) == 0) return HEAD_VEC_SET;
      if (memcmp(s, "map-get", 7) == 0) return HEAD_MAP_GET;
      if (memcmp(s, "map-has", 7) == 0) return HEAD_MAP_HAS;
      if (memcmp(s, "map-len", 7) == 0) return HEAD_MAP_LEN;
      if (memcmp(s, "map-set", 7) == 0) return HEAD_MAP_SET;
      if (memcmp(s, "future?", 7) == 0) return HEAD_FUTURE_Q;
      if (memcmp(s, "collect", 7) == 0) return HEAD_COLLECT;
      return HEAD_NONE;
    case 8:
      if (memcmp(s, "continue", 8) == 0) return HEAD_CONTINUE;
      if (memcmp(s, "with-ctx", 8) == 0) return HEAD_WITH_CTX;
      if (memcmp(s, "parallel", 8) == 0) return HEAD_PARALLEL;
      if (memcmp(s, "vec-push", 8) == 0) return HEAD_VEC_PUSH;
      if (memcmp(s, "map-keys", 8) == 0) return HEAD_MAP_KEYS;
      if (memcmp(s, "map-vals", 8) == 0) return HEAD_MAP_VALS;
      if (memcmp(s, "defmacro", 8) == 0) return HEAD_DEFMACRO;
      return HEAD_NONE;
    case 9:
      if (memcmp(s, "transform", 9) == 0) return HEAD_TRANSFORM;
      if (memcmp(s, "to-string", 9) == 0) return HEAD_TO_STRING;
      if (memcmp(s, "vec-slice", 9) == 0) return HEAD_VEC_SLICE;
      if (memcmp(s, "error-val", 9) == 0) return HEAD_ERROR_VAL;
      if (memcmp(s, "interpret", 9) == 0) return HEAD_INTERPRET;
      if (memcmp(s, "defstruct", 9) == 0) return HEAD_DEFSTRUCT;
      return HEAD_NONE;
    case 10:
      if (memcmp(s, "vec-concat", 10) == 0) return HEAD_VEC_CONCAT;
      if (memcmp(s, "map-remove", 10) == 0) return HEAD_MAP_REMOVE;
      if (memcmp(s, "syntax-pos", 10) == 0) return HEAD_SYNTAX_POS;
      if (memcmp(s, "syntax-str", 10) == 0) return HEAD_SYNTAX_STR;
      return HEAD_NONE;
    case 11:
      if (memcmp(s, "byte-length", 11) == 0) return HEAD_BYTE_LENGTH;
      if (memcmp(s, "stack-trace", 11) == 0) return HEAD_STACK_TRACE;
      if (memcmp(s, "stream_next", 11) == 0) return HEAD_STREAM_NEXT;
      if (memcmp(s, "syntax-kind", 11) == 0) return HEAD_SYNTAX_KIND;
      if (memcmp(s, "syntax-head", 11) == 0) return HEAD_SYNTAX_HEAD;
      if (memcmp(s, "syntax-args", 11) == 0) return HEAD_SYNTAX_ARGS;
      if (memcmp(s, "make-syntax", 11) == 0) return HEAD_MAKE_SYNTAX;
      return HEAD_NONE;
    case 12:
      if (memcmp(s, "syntax-quote", 12) == 0) return HEAD_SYNTAX_QUOTE;
      if (memcmp(s, "syntax-datum", 12) == 0) return HEAD_SYNTAX_DATUM;
      if (memcmp(s, "syntax-error", 12) == 0) return HEAD_SYNTAX_ERROR;
      return HEAD_NONE;
    case 15:
      if (memcmp(s, "syntax-commands", 15) == 0) return HEAD_SYNTAX_COMMANDS;
      return HEAD_NONE;
    case 17:
      if (memcmp(s, "interpret-prelude", 17) == 0) return HEAD_INTERPRET_PRELUDE;
      return HEAD_NONE;
  }
  return HEAD_NONE;
}

/* ast__compute_head_id is defined further down, after struct AstNode. */

/* -------------------------------------------------------------------------
 * Static type system enum + helpers
 *
 * Defined here (rather than in compiler.c) so the typer pass — included
 * before compiler.c in the unity build — can populate `node->inferred_type`
 * and use the keyword table without a forward declaration.
 * ------------------------------------------------------------------------- */

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
  TYPE_TYPED_MAP
} JaclType;

/* Typed-collection element encoding for struct_idx: real struct registry
 * indices live below 0xFF00; values in [0xFF00, 0x10000) encode a scalar
 * JaclType (used for [Vec i64], [Map i32 i64] etc.). Shared between
 * the compiler and the typer so both can speak the same ABI. */
#define JACL_SCALAR_VEC_BASE         0xFF00u
#define JACL_IS_SCALAR_TYPE_IDX(idx) ((idx) >= JACL_SCALAR_VEC_BASE && (idx) < 0x10000u)
#define JACL_SCALAR_TYPE_IDX(t)      (JACL_SCALAR_VEC_BASE + (uint32_t)(t))
#define JACL_TYPE_IDX_TO_SCALAR(idx) ((JaclType)((idx) - JACL_SCALAR_VEC_BASE))

/* Single table for type keyword recognition — keeps is_type_keyword and
   type_from_keyword in sync automatically. */
static const struct { const char* name; uint32_t len; JaclType type; } type_keyword_table[] = {
  { "i32",    3, TYPE_I32 },
  { "i64",    3, TYPE_I64 },
  { "u32",    3, TYPE_U32 },
  { "u64",    3, TYPE_U64 },
  { "f32",    3, TYPE_F32 },
  { "f64",    3, TYPE_F64 },
  { "str",    3, TYPE_STR },
  { "vec",    3, TYPE_VEC },
  { "map",    3, TYPE_MAP },
  { "dyn",    3, TYPE_DYN },
  { "bool",   4, TYPE_BOOL },
  { "stream", 6, TYPE_STREAM },
};
#define TYPE_KEYWORD_COUNT (sizeof(type_keyword_table) / sizeof(type_keyword_table[0]))

static bool is_type_keyword(const char* word, size_t len) {
  for (uint32_t i = 0; i < TYPE_KEYWORD_COUNT; i++) {
    if (type_keyword_table[i].len == (uint32_t)len &&
        memcmp(word, type_keyword_table[i].name, len) == 0)
      return true;
  }
  return false;
}

static JaclType type_from_keyword(const char* word, size_t len) {
  for (uint32_t i = 0; i < TYPE_KEYWORD_COUNT; i++) {
    if (type_keyword_table[i].len == (uint32_t)len &&
        memcmp(word, type_keyword_table[i].name, len) == 0)
      return type_keyword_table[i].type;
  }
  return TYPE_DYN;
}

const char* type_name(JaclType t) {
  switch (t) {
    case TYPE_DYN:     return "dyn";
    case TYPE_BOOL:    return "bool";
    case TYPE_NIL:     return "nil";
    case TYPE_I32:     return "i32";
    case TYPE_I64:     return "i64";
    case TYPE_U32:     return "u32";
    case TYPE_U64:     return "u64";
    case TYPE_F32:     return "f32";
    case TYPE_F64:     return "f64";
    case TYPE_STR:     return "str";
    case TYPE_VEC:     return "vec";
    case TYPE_MAP:     return "map";
    case TYPE_CLOSURE: return "closure";
    case TYPE_STRUCT:     return "struct";
    case TYPE_STREAM:    return "stream";
    case TYPE_TYPED_VEC: return "typed-vec";
    case TYPE_TYPED_MAP: return "typed-map";
  }
  return "unknown";
}

static bool is_numeric_type(JaclType t) {
  return t == TYPE_I32 || t == TYPE_I64 || t == TYPE_U32 ||
         t == TYPE_U64 || t == TYPE_F32 || t == TYPE_F64;
}

static bool is_unboxed_type(JaclType t) {
  return t == TYPE_I64 || t == TYPE_U64 || t == TYPE_F64;
}

static bool is_typed_collection(JaclType t) {
  return t == TYPE_TYPED_VEC || t == TYPE_TYPED_MAP;
}

/* -------------------------------------------------------------------------
 * Source Position
 * ------------------------------------------------------------------------- */

typedef struct {
  uint32_t line;    /* 1-based line number */
  uint32_t column;  /* 1-based column number */
  uint32_t offset;  /* byte offset from start of source */
} SourcePos;

/* -------------------------------------------------------------------------
 * AST Node — tagged union
 * ------------------------------------------------------------------------- */

typedef struct AstNode AstNode;

struct AstNode {
  AstNodeType type;
  SourcePos   start;
  SourcePos   end;
  uint32_t    scope_mark;  /* hygiene: 0 = no macro context, >0 = macro expansion */
  uint8_t     is_caret;    /* US-013: ^name in syntax-quote — force scope mark 0 */
  uint8_t     is_gensym;   /* US-014: var-ref produced by gensym — accepted as binding name */
  uint8_t     inferred_type; /* JaclType (TYPE_DYN default), populated by typer pass */
  uint32_t    inferred_struct_idx; /* struct registry index when inferred_type==TYPE_STRUCT, UINT32_MAX otherwise */
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
             const char* binding_name; uint32_t binding_name_len; /* module binding form */
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
    struct { AstNode* value; /* NULL if no value */ }              break_stmt;
    struct { AstNode* value; /* NULL if no value */ }              return_stmt;
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

/* -------------------------------------------------------------------------
 * Arena helper for allocating AST nodes
 * ------------------------------------------------------------------------- */

AstNode* ast_alloc(arena_t* arena) {
  AstNode* node = (AstNode*)arena_alloc(arena, sizeof(AstNode));
  node->inferred_struct_idx = UINT32_MAX;
  return node;
}

/* Compute head_id from a head AstNode. Returns HEAD_NONE for non-string
 * heads (var-ref, spread, etc). Used at command-construction sites. */
static HeadId ast__compute_head_id(AstNode* head) {
  if (!head || head->type != AST_LIT_STRING) return HEAD_NONE;
  return ast__head_id_for(head->data.lit_string.value,
                          head->data.lit_string.length);
}

AstNode** ast_alloc_array(arena_t* arena, uint32_t count) {
  return (AstNode**)arena_alloc(arena, sizeof(AstNode*) * count);
}

/**
 * Pretty-print a single AST node to an arena-allocated string.
 */
const char* ast_pretty_print(AstNode* node, arena_t* arena);

/**
 * Pretty-print a program (array of top-level AST nodes).
 * Returns an arena-allocated string with newline-separated commands.
 */
const char* ast_pretty_print_program(AstNode** nodes, uint32_t count,
                                     arena_t* arena);

/* -------------------------------------------------------------------------
 * Internal: Growable string buffer for pretty-printing
 * ------------------------------------------------------------------------- */

#define AST__PP_INITIAL_CAP 256

typedef struct {
  char*    buf;
  uint32_t len;
  uint32_t cap;
  arena_t* arena;
} AstStrBuf;

void ast__buf_init(AstStrBuf* b, arena_t* arena) {
  b->cap   = AST__PP_INITIAL_CAP;
  b->len   = 0;
  b->arena = arena;
  b->buf   = (char*)arena_alloc(arena, b->cap);
}

void ast__buf_ensure(AstStrBuf* b, uint32_t extra) {
  if (b->len + extra >= b->cap) {
    uint32_t new_cap = b->cap;
    while (new_cap <= b->len + extra) {
      new_cap *= 2;
    }
    char* new_buf = (char*)arena_alloc(b->arena, new_cap);
    memcpy(new_buf, b->buf, b->len);
    b->buf = new_buf;
    b->cap = new_cap;
  }
}

void ast__buf_char(AstStrBuf* b, char c) {
  ast__buf_ensure(b, 1);
  b->buf[b->len++] = c;
}

void ast__buf_str(AstStrBuf* b, const char* s, uint32_t len) {
  ast__buf_ensure(b, len);
  memcpy(b->buf + b->len, s, len);
  b->len += len;
}

void ast__buf_cstr(AstStrBuf* b, const char* s) {
  ast__buf_str(b, s, (uint32_t)strlen(s));
}

const char* ast__buf_finish(AstStrBuf* b) {
  ast__buf_char(b, '\0');
  return b->buf;
}

/* -------------------------------------------------------------------------
 * Internal: Escape a character inside a quoted string
 * ------------------------------------------------------------------------- */

void ast__buf_escaped_char(AstStrBuf* b, char c) {
  switch (c) {
    case '"':  ast__buf_char(b, '\\'); ast__buf_char(b, '"');  break;
    case '\\': ast__buf_char(b, '\\'); ast__buf_char(b, '\\'); break;
    case '\n': ast__buf_char(b, '\\'); ast__buf_char(b, 'n');  break;
    case '\t': ast__buf_char(b, '\\'); ast__buf_char(b, 't');  break;
    case '\r': ast__buf_char(b, '\\'); ast__buf_char(b, 'r');  break;
    case '$':  ast__buf_char(b, '\\'); ast__buf_char(b, '$');  break;
    default:   ast__buf_char(b, c);                            break;
  }
}

/* -------------------------------------------------------------------------
 * Internal: Check whether an AST_LIT_STRING needs quoting
 *
 * Returns 1 if the string must be quoted to round-trip correctly.
 * Bare words and operators can be printed unquoted.
 * ------------------------------------------------------------------------- */

int ast__needs_quoting(const char* s, uint32_t len) {
  if (len == 0) return 1;
  /* Starts with digit → would lex as int/float */
  if (s[0] >= '0' && s[0] <= '9') return 1;
  for (uint32_t i = 0; i < len; i++) {
    char c = s[i];
    if (c == ' '  || c == '\t' || c == '\n' || c == '\r' ||
        c == '"'  || c == '$'  || c == '['  || c == ']'  ||
        c == '{'  || c == '}'  || c == '('  || c == ')'  ||
        c == '#'  || c == ';'  || c == '\\' || c < 32) {
      return 1;
    }
  }
  return 0;
}

/* -------------------------------------------------------------------------
 * Internal: Pretty-print a single AST node into a string buffer
 * ------------------------------------------------------------------------- */

void ast__pp_node(AstStrBuf* b, AstNode* node) {
  switch (node->type) {
    case AST_LIT_INT: {
      char tmp[32];
      int n = snprintf(tmp, sizeof(tmp), "%d", (int)node->data.lit_int.value);
      ast__buf_str(b, tmp, (uint32_t)n);
      break;
    }
    case AST_LIT_FLOAT: {
      char tmp[64];
      int n = snprintf(tmp, sizeof(tmp), "%g",
                       (double)node->data.lit_float.value);
      /* Ensure decimal point so it re-parses as float, not int */
      int has_dot = 0;
      for (int i = 0; i < n; i++) {
        if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E') {
          has_dot = 1;
          break;
        }
      }
      ast__buf_str(b, tmp, (uint32_t)n);
      if (!has_dot) {
        ast__buf_cstr(b, ".0");
      }
      break;
    }
    case AST_LIT_STRING: {
      if (ast__needs_quoting(node->data.lit_string.value,
                             node->data.lit_string.length)) {
        ast__buf_char(b, '"');
        for (uint32_t i = 0; i < node->data.lit_string.length; i++) {
          ast__buf_escaped_char(b, node->data.lit_string.value[i]);
        }
        ast__buf_char(b, '"');
      } else {
        ast__buf_str(b, node->data.lit_string.value,
                     node->data.lit_string.length);
      }
      break;
    }
    case AST_VAR_REF: {
      /* US-013: print caret-prefixed references as ^name (they appear only
         inside syntax-quote bodies, never in regular code). */
      if (node->is_caret) {
        ast__buf_char(b, '^');
      } else {
        ast__buf_char(b, '$');
      }
      ast__buf_str(b, node->data.var_ref.name, node->data.var_ref.length);
      break;
    }
    case AST_COMMAND: {
      /* Detect [. $var field] → print as $var->field to avoid rejected [. ...] */
      AstNode* head = node->data.command.head;
      if (head->type == AST_LIT_STRING &&
          head->data.lit_string.length == 1 &&
          head->data.lit_string.value[0] == '.' &&
          node->data.command.arg_count == 2) {
        ast__pp_node(b, node->data.command.args[0]);
        ast__buf_cstr(b, "->");
        ast__buf_str(b, node->data.command.args[1]->data.lit_string.value,
                     node->data.command.args[1]->data.lit_string.length);
        break;
      }
      /* Detect proc commands: print as bare command with {params} to avoid
         [proc ...] rejection. Format: proc [type] name {params} {body} */
      if (head->type == AST_LIT_STRING &&
          head->data.lit_string.length == 4 &&
          memcmp(head->data.lit_string.value, "proc", 4) == 0 &&
          node->data.command.arg_count >= 2) {
        /* Lambda (anonymous proc): name is "" → print as [\\ body...] */
        AstNode* name_node = node->data.command.args[0];
        if (name_node->type == AST_LIT_STRING &&
            name_node->data.lit_string.length == 0 &&
            node->data.command.arg_count == 3) {
          /* args: [0]=name(""), [1]=params, [2]=body_block */
          AstNode* body_block = node->data.command.args[2];
          ast__buf_cstr(b, "[\\ ");
          if (body_block->type == AST_BLOCK && body_block->data.block.count == 1) {
            /* Single-command body: unwrap the block and print command directly */
            AstNode* cmd = body_block->data.block.commands[0];
            if (cmd->type == AST_COMMAND) {
              ast__pp_node(b, cmd->data.command.head);
              for (uint32_t i = 0; i < cmd->data.command.arg_count; i++) {
                ast__buf_char(b, ' ');
                ast__pp_node(b, cmd->data.command.args[i]);
              }
            } else {
              ast__pp_node(b, cmd);
            }
          } else {
            ast__pp_node(b, body_block);
          }
          ast__buf_char(b, ']');
          break;
        }
        ast__buf_cstr(b, "proc");
        /* The last arg is the body block, the second-to-last is params.
           Everything before params is name (and optional return type). */
        uint32_t params_idx = node->data.command.arg_count - 2;
        uint32_t body_idx   = node->data.command.arg_count - 1;
        /* Print name (and optional return type) */
        for (uint32_t i = 0; i < params_idx; i++) {
          ast__buf_char(b, ' ');
          ast__pp_node(b, node->data.command.args[i]);
        }
        /* Print params as {head arg1 arg2 ...} */
        AstNode* params = node->data.command.args[params_idx];
        ast__buf_cstr(b, " {");
        if (params->type == AST_COMMAND &&
            params->data.command.head->data.lit_string.length > 0) {
          ast__pp_node(b, params->data.command.head);
          for (uint32_t i = 0; i < params->data.command.arg_count; i++) {
            ast__buf_char(b, ' ');
            ast__pp_node(b, params->data.command.args[i]);
          }
        }
        ast__buf_char(b, '}');
        /* Print body block */
        ast__buf_char(b, ' ');
        ast__pp_node(b, node->data.command.args[body_idx]);
        break;
      }
      ast__buf_char(b, '[');
      ast__pp_node(b, head);
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        ast__buf_char(b, ' ');
        ast__pp_node(b, node->data.command.args[i]);
      }
      ast__buf_char(b, ']');
      break;
    }
    case AST_BLOCK: {
      ast__buf_char(b, '{');
      if (node->data.block.count > 0) {
        ast__buf_char(b, ' ');
        for (uint32_t i = 0; i < node->data.block.count; i++) {
          if (i > 0) {
            ast__buf_cstr(b, "; ");
          }
          ast__pp_node(b, node->data.block.commands[i]);
        }
        ast__buf_char(b, ' ');
      }
      ast__buf_char(b, '}');
      break;
    }
    case AST_INTERP_STRING: {
      ast__buf_char(b, '"');
      for (uint32_t i = 0; i < node->data.interp_string.count; i++) {
        AstNode* seg = node->data.interp_string.segments[i];
        switch (seg->type) {
          case AST_LIT_STRING:
            for (uint32_t j = 0; j < seg->data.lit_string.length; j++) {
              ast__buf_escaped_char(b, seg->data.lit_string.value[j]);
            }
            break;
          case AST_VAR_REF:
            ast__buf_char(b, '$');
            ast__buf_str(b, seg->data.var_ref.name, seg->data.var_ref.length);
            break;
          case AST_COMMAND:
            ast__buf_cstr(b, "$[");
            ast__pp_node(b, seg->data.command.head);
            for (uint32_t j = 0; j < seg->data.command.arg_count; j++) {
              ast__buf_char(b, ' ');
              ast__pp_node(b, seg->data.command.args[j]);
            }
            ast__buf_char(b, ']');
            break;
          default:
            break;
        }
      }
      ast__buf_char(b, '"');
      break;
    }
    case AST_USE: {
      ast__buf_cstr(b, "use \"");
      ast__buf_str(b, node->data.use_decl.path, node->data.use_decl.path_len);
      ast__buf_cstr(b, "\" ");
      if (node->data.use_decl.is_module_binding) {
        /* Module binding form: use "path" name */
        ast__buf_str(b, node->data.use_decl.binding_name,
                     node->data.use_decl.binding_name_len);
      } else {
        /* Destructuring form: use "path" {name1, name2} */
        ast__buf_char(b, '{');
        for (uint32_t i = 0; i < node->data.use_decl.name_count; i++) {
          if (i > 0) ast__buf_cstr(b, ", ");
          ast__buf_str(b, node->data.use_decl.names[i],
                       node->data.use_decl.name_lens[i]);
        }
        ast__buf_char(b, '}');
      }
      break;
    }
    case AST_DEFSTRUCT: {
      ast__buf_cstr(b, "struct ");
      ast__buf_str(b, node->data.defstruct.name, node->data.defstruct.name_len);
      ast__buf_cstr(b, " {");
      for (uint32_t i = 0; i < node->data.defstruct.field_count; i++) {
        if (i > 0) ast__buf_char(b, ',');
        ast__buf_char(b, ' ');
        ast__buf_str(b, node->data.defstruct.field_types[i],
                     node->data.defstruct.field_type_lens[i]);
        ast__buf_char(b, ' ');
        ast__buf_str(b, node->data.defstruct.field_names[i],
                     node->data.defstruct.field_name_lens[i]);
      }
      ast__buf_char(b, '}');
      break;
    }
    case AST_DEFMACRO: {
      ast__buf_cstr(b, "defmacro ");
      ast__buf_str(b, node->data.defmacro.name, node->data.defmacro.name_len);
      ast__buf_cstr(b, " {");
      for (uint32_t i = 0; i < node->data.defmacro.param_count; i++) {
        if (i > 0) ast__buf_char(b, ',');
        ast__buf_char(b, ' ');
        ast__buf_str(b, node->data.defmacro.param_names[i],
                     node->data.defmacro.param_name_lens[i]);
      }
      ast__buf_cstr(b, "} ");
      ast__pp_node(b, node->data.defmacro.body);
      break;
    }
    case AST_QUOTE: {
      ast__buf_cstr(b, "quote ");
      ast__pp_node(b, node->data.quote.child);
      break;
    }
    case AST_SYNTAX_QUOTE: {
      ast__buf_cstr(b, "syntax-quote ");
      ast__pp_node(b, node->data.syntax_quote.child);
      break;
    }
    case AST_UNQUOTE: {
      ast__buf_char(b, '~');
      ast__pp_node(b, node->data.unquote.child);
      break;
    }
    case AST_UNQUOTE_SPLICING: {
      ast__buf_cstr(b, "~@");
      ast__pp_node(b, node->data.unquote_splicing.child);
      break;
    }
    case AST_BREAK: {
      ast__buf_cstr(b, "break");
      if (node->data.break_stmt.value) {
        ast__buf_char(b, ' ');
        ast__pp_node(b, node->data.break_stmt.value);
      }
      break;
    }
    case AST_CONTINUE: {
      ast__buf_cstr(b, "continue");
      break;
    }
    case AST_RETURN: {
      ast__buf_cstr(b, "return");
      if (node->data.return_stmt.value) {
        ast__buf_char(b, ' ');
        ast__pp_node(b, node->data.return_stmt.value);
      }
      break;
    }
    case AST_DESTRUCTURE_VEC: {
      ast__buf_char(b, '[');
      for (uint32_t i = 0; i < node->data.destructure_vec.count; i++) {
        if (i > 0) ast__buf_char(b, ' ');
        if (node->data.destructure_vec.types &&
            node->data.destructure_vec.types[i]) {
          ast__buf_str(b, node->data.destructure_vec.types[i],
                       node->data.destructure_vec.type_lens[i]);
          ast__buf_char(b, ' ');
        }
        ast__buf_str(b, node->data.destructure_vec.names[i],
                     node->data.destructure_vec.name_lens[i]);
      }
      if (node->data.destructure_vec.rest_name) {
        if (node->data.destructure_vec.count > 0) ast__buf_char(b, ' ');
        ast__buf_cstr(b, "..");
        ast__buf_str(b, node->data.destructure_vec.rest_name,
                     node->data.destructure_vec.rest_name_len);
      }
      ast__buf_char(b, ']');
      break;
    }
    case AST_DESTRUCTURE_NAMED: {
      ast__buf_char(b, '{');
      for (uint32_t i = 0; i < node->data.destructure_named.count; i++) {
        if (i > 0) ast__buf_cstr(b, ", ");
        if (node->data.destructure_named.types &&
            node->data.destructure_named.types[i]) {
          ast__buf_str(b, node->data.destructure_named.types[i],
                       node->data.destructure_named.type_lens[i]);
          ast__buf_char(b, ' ');
        }
        ast__buf_str(b, node->data.destructure_named.names[i],
                     node->data.destructure_named.name_lens[i]);
      }
      if (node->data.destructure_named.rest_name) {
        if (node->data.destructure_named.count > 0) ast__buf_cstr(b, ", ");
        ast__buf_cstr(b, "..");
        ast__buf_str(b, node->data.destructure_named.rest_name,
                     node->data.destructure_named.rest_name_len);
      } else if (node->data.destructure_named.spread_all) {
        if (node->data.destructure_named.count > 0) ast__buf_cstr(b, ", ");
        ast__buf_cstr(b, "..");
      }
      ast__buf_char(b, '}');
      break;
    }
    case AST_SPREAD: {
      ast__buf_cstr(b, "..");
      ast__pp_node(b, node->data.spread.expr);
      break;
    }
    case AST_SHELL_CMD: {
      ast__buf_char(b, '!');
      ast__pp_node(b, node->data.shell_cmd.head);
      for (uint32_t i = 0; i < node->data.shell_cmd.arg_count; i++) {
        ast__buf_char(b, ' ');
        ast__pp_node(b, node->data.shell_cmd.args[i]);
      }
      break;
    }
    case AST_CTX_DECL: {
      ast__buf_cstr(b, "ctx ");
      if (node->data.ctx_decl.is_mutable) ast__buf_cstr(b, "mut ");
      ast__buf_str(b, node->data.ctx_decl.type_name, node->data.ctx_decl.type_name_len);
      ast__buf_char(b, ' ');
      ast__buf_str(b, node->data.ctx_decl.field_name, node->data.ctx_decl.field_name_len);
      ast__buf_cstr(b, " = ");
      ast__pp_node(b, node->data.ctx_decl.default_expr);
      break;
    }
    case AST_ERROR: {
      ast__buf_cstr(b, "<error>");
      break;
    }
  }
}

/* -------------------------------------------------------------------------
 * Public API: Pretty-print functions
 * ------------------------------------------------------------------------- */

const char* ast_pretty_print(AstNode* node, arena_t* arena) {
  AstStrBuf buf;
  ast__buf_init(&buf, arena);
  ast__pp_node(&buf, node);
  return ast__buf_finish(&buf);
}

const char* ast_pretty_print_program(AstNode** nodes, uint32_t count,
                                     arena_t* arena) {
  AstStrBuf buf;
  ast__buf_init(&buf, arena);
  for (uint32_t i = 0; i < count; i++) {
    if (i > 0) {
      ast__buf_char(&buf, '\n');
    }
    ast__pp_node(&buf, nodes[i]);
  }
  return ast__buf_finish(&buf);
}

#endif /* AST_C */
