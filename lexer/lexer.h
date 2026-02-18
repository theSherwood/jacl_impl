/*
 * ---------------------------------------------------------------------------
 * JACL Lexer
 * ---------------------------------------------------------------------------
 * Streaming tokenizer for JACL Phase 1 bracket syntax.
 * Produces an arena-allocated token array from byte buffer input.
 *
 * Single-header library: define LEXER_IMPLEMENTATION before including
 * in exactly one translation unit to generate function bodies.
 *
 * Usage:
 * ```
 * arena_t arena = {0};
 * LexResult result = lexer_lex("print hello", &arena);
 * // use result.tokens[0..result.count-1]
 * arena_destroy(&arena);
 * ```
 */

#ifndef LEXER_H
#define LEXER_H

#include "../arena/arena.h"
#include "../platform/platform.h"

/* -------------------------------------------------------------------------
 * Token Types
 * ------------------------------------------------------------------------- */

typedef enum {
  TOKEN_LBRACKET,         /* [ */
  TOKEN_RBRACKET,         /* ] */
  TOKEN_LBRACE,           /* { */
  TOKEN_RBRACE,           /* } */
  TOKEN_LPAREN,           /* ( */
  TOKEN_RPAREN,           /* ) */
  TOKEN_WORD,             /* bare word: command names, arguments */
  TOKEN_OPERATOR,         /* symbolic operators: +, -, >=, etc. */
  TOKEN_KEYWORD,          /* :key, :my-thing */
  TOKEN_INT,              /* integer literal: 42, 0xFF, 0b1010 */
  TOKEN_FLOAT,            /* float literal: 3.14, 0.5 */
  TOKEN_STRING,           /* complete string with no interpolation */
  TOKEN_STRING_BEGIN,     /* start of interpolated string */
  TOKEN_STRING_PART,      /* middle segment of interpolated string */
  TOKEN_STRING_END,       /* end of interpolated string */
  TOKEN_INTERP_VAR,       /* $var inside a string */
  TOKEN_INTERP_EXPR_START,/* $[ inside a string */
  TOKEN_INTERP_EXPR_END,  /* ] closing interpolated expression */
  TOKEN_VAR,              /* $identifier variable reference */
  TOKEN_DOLLAR_BRACKET,   /* $[ subcommand expression */
  TOKEN_NEWLINE,          /* newline (\n or \r\n) */
  TOKEN_ERROR,            /* lexer error with descriptive message */
  TOKEN_EOF               /* end of input */
} TokenType;

/* -------------------------------------------------------------------------
 * Token
 * ------------------------------------------------------------------------- */

typedef struct {
  TokenType type;
  uint32_t  line;     /* 1-based line number */
  uint32_t  column;   /* 1-based column number */
  uint32_t  offset;   /* byte offset from start of source */
  uint32_t  length;   /* length in bytes in the source */
  union {
    const char* text;      /* TOKEN_WORD, TOKEN_KEYWORD, TOKEN_STRING, etc. */
    int32_t     int_val;   /* TOKEN_INT */
    float       float_val; /* TOKEN_FLOAT */
    const char* error_msg; /* TOKEN_ERROR */
  } payload;
} Token;

/* -------------------------------------------------------------------------
 * Lex Result
 * ------------------------------------------------------------------------- */

typedef struct {
  Token*   tokens;      /* arena-allocated array of tokens */
  uint32_t count;       /* number of tokens in the array */
  uint32_t error_count; /* number of TOKEN_ERROR tokens */
} LexResult;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * Tokenize a null-terminated source string.
 *
 * All tokens and auxiliary data are allocated from the provided arena.
 * The returned token array always ends with a TOKEN_EOF token.
 *
 * @param source  Null-terminated JACL source text.
 * @param arena   Arena allocator for token storage.
 * @return LexResult containing the token array, count, and error count.
 */
LexResult lexer_lex(const char* source, arena_t* arena);

#endif /* LEXER_H */

/* =========================================================================
 * Implementation Section
 * Define LEXER_IMPLEMENTATION before including to generate function bodies.
 * ========================================================================= */

#ifdef LEXER_IMPLEMENTATION
#ifndef LEXER_IMPL_GUARD_
#define LEXER_IMPL_GUARD_

#include <string.h>

/* Stub implementation — produces only an EOF token.
 * Full lexing logic will be added in subsequent stories. */
LexResult lexer_lex(const char* source, arena_t* arena) {
  (void)source;
  Token* tokens = (Token*)arena_alloc(arena, sizeof(Token));
  tokens[0].type     = TOKEN_EOF;
  tokens[0].line     = 1;
  tokens[0].column   = 1;
  tokens[0].offset   = 0;
  tokens[0].length   = 0;
  tokens[0].payload.text = NULL;

  LexResult result;
  result.tokens      = tokens;
  result.count       = 1;
  result.error_count = 0;
  return result;
}

#endif /* LEXER_IMPL_GUARD_ */
#endif /* LEXER_IMPLEMENTATION */
