/*
 * ---------------------------------------------------------------------------
 * JACL AST Node Types
 * ---------------------------------------------------------------------------
 * Defines the Abstract Syntax Tree node types produced by the parser.
 * All AstNode structs are arena-allocated — no individual free required.
 *
 * Single-header library: define AST_IMPLEMENTATION before including
 * in exactly one translation unit to generate function bodies.
 */

#ifndef AST_H
#define AST_H

#include <stdint.h>

#include "../arena/arena.h"

/* -------------------------------------------------------------------------
 * AST Node Types
 * ------------------------------------------------------------------------- */

typedef enum {
  AST_COMMAND,       /* [cmd arg1 arg2] or bare command */
  AST_LIT_INT,       /* integer literal: 42, 0xFF, 0b1010 */
  AST_LIT_FLOAT,     /* float literal: 3.14 */
  AST_LIT_STRING,    /* string literal: "hello" or bare word */
  AST_LIT_KEYWORD,   /* keyword literal: :foo */
  AST_VAR_REF,       /* variable reference: $name */
  AST_BLOCK,         /* code block: { cmd1; cmd2 } */
  AST_INTERP_STRING, /* interpolated string: "hello $name" */
  AST_ERROR          /* parse error with recovery */
} AstNodeType;

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
  union {
    struct { AstNode*  head; AstNode** args; uint32_t arg_count; } command;
    struct { int32_t   value; }                                    lit_int;
    struct { float     value; }                                    lit_float;
    struct { const char* value;   uint32_t length; }               lit_string;
    struct { const char* name;    uint32_t length; }               lit_keyword;
    struct { const char* name;    uint32_t length; }               var_ref;
    struct { AstNode**   commands; uint32_t count; }               block;
    struct { AstNode**   segments; uint32_t count; }               interp_string;
    struct { const char* message; }                                error;
  } data;
};

/* -------------------------------------------------------------------------
 * Arena helper for allocating AST nodes
 * ------------------------------------------------------------------------- */

static AstNode* ast_alloc(arena_t* arena) {
  return (AstNode*)arena_alloc(arena, sizeof(AstNode));
}

static AstNode** ast_alloc_array(arena_t* arena, uint32_t count) {
  return (AstNode**)arena_alloc(arena, sizeof(AstNode*) * count);
}

#endif /* AST_H */

/* =========================================================================
 * Implementation Section
 * Define AST_IMPLEMENTATION before including to generate function bodies.
 * ========================================================================= */

#ifdef AST_IMPLEMENTATION
#ifndef AST_IMPL_GUARD_
#define AST_IMPL_GUARD_

/* Reserved for future AST utility functions (pretty-printer, etc.) */

#endif /* AST_IMPL_GUARD_ */
#endif /* AST_IMPLEMENTATION */
