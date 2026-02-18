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

/* -------------------------------------------------------------------------
 * Internal: Growable token array backed by arena
 * ------------------------------------------------------------------------- */

#define LEXER_INITIAL_CAP 256

typedef struct {
  Token*   tokens;
  uint32_t count;
  uint32_t cap;
  arena_t* arena;
} TokenArray;

static void lexer__arr_init(TokenArray* arr, arena_t* arena) {
  arr->cap    = LEXER_INITIAL_CAP;
  arr->count  = 0;
  arr->arena  = arena;
  arr->tokens = (Token*)arena_alloc(arena, sizeof(Token) * arr->cap);
}

static void lexer__arr_push(TokenArray* arr, Token tok) {
  if (arr->count >= arr->cap) {
    uint32_t new_cap = arr->cap * 2;
    Token* new_tokens = (Token*)arena_alloc(arr->arena, sizeof(Token) * new_cap);
    memcpy(new_tokens, arr->tokens, sizeof(Token) * arr->count);
    arr->tokens = new_tokens;
    arr->cap    = new_cap;
  }
  arr->tokens[arr->count++] = tok;
}

/* -------------------------------------------------------------------------
 * Internal: Lexer state
 * ------------------------------------------------------------------------- */

typedef struct {
  const char* source;
  uint32_t    pos;
  uint32_t    line;
  uint32_t    col;
  arena_t*    arena;
} Lexer;

static void lexer__init(Lexer* lex, const char* source, arena_t* arena) {
  lex->source = source;
  lex->pos    = 0;
  lex->line   = 1;
  lex->col    = 1;
  lex->arena  = arena;
}

static char lexer__peek(Lexer* lex) {
  return lex->source[lex->pos];
}

static char lexer__advance(Lexer* lex) {
  char c = lex->source[lex->pos++];
  lex->col++;
  return c;
}

static Token lexer__make_token(Lexer* lex, TokenType type,
                               uint32_t start_offset, uint32_t start_line,
                               uint32_t start_col) {
  Token tok;
  memset(&tok, 0, sizeof(Token));
  tok.type   = type;
  tok.line   = start_line;
  tok.column = start_col;
  tok.offset = start_offset;
  tok.length = lex->pos - start_offset;
  return tok;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

LexResult lexer_lex(const char* source, arena_t* arena) {
  Lexer lex;
  lexer__init(&lex, source, arena);

  TokenArray arr;
  lexer__arr_init(&arr, arena);

  uint32_t error_count = 0;

  for (;;) {
    char c = lexer__peek(&lex);

    /* EOF */
    if (c == '\0') {
      Token tok = lexer__make_token(&lex, TOKEN_EOF, lex.pos, lex.line, lex.col);
      tok.length = 0;
      lexer__arr_push(&arr, tok);
      break;
    }

    /* Skip spaces and tabs */
    if (c == ' ' || c == '\t') {
      lexer__advance(&lex);
      continue;
    }

    /* Newlines */
    if (c == '\n' || c == '\r') {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex);
      if (c == '\r' && lexer__peek(&lex) == '\n') {
        lexer__advance(&lex);
      }
      Token tok = lexer__make_token(&lex, TOKEN_NEWLINE, start, sline, scol);
      lexer__arr_push(&arr, tok);
      lex.line++;
      lex.col = 1;
      continue;
    }

    /* Single-character delimiters */
    if (c == '[' || c == ']' || c == '{' || c == '}' || c == '(' || c == ')') {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex);
      TokenType type;
      switch (c) {
        case '[': type = TOKEN_LBRACKET; break;
        case ']': type = TOKEN_RBRACKET; break;
        case '{': type = TOKEN_LBRACE;   break;
        case '}': type = TOKEN_RBRACE;   break;
        case '(': type = TOKEN_LPAREN;   break;
        default:  type = TOKEN_RPAREN;   break;
      }
      Token tok = lexer__make_token(&lex, type, start, sline, scol);
      lexer__arr_push(&arr, tok);
      continue;
    }

    /* Unrecognized character — emit ERROR and advance */
    {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex);
      Token tok = lexer__make_token(&lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg = "unexpected character";
      lexer__arr_push(&arr, tok);
      error_count++;
    }
  }

  LexResult result;
  result.tokens      = arr.tokens;
  result.count       = arr.count;
  result.error_count = error_count;
  return result;
}

#endif /* LEXER_IMPL_GUARD_ */
#endif /* LEXER_IMPLEMENTATION */
