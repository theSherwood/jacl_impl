/*
 * JACL Lexer
 *
 * Streaming tokenizer for JACL Phase 1 bracket syntax.
 * Produces an arena-allocated token array from byte buffer input.
 */

#ifndef LEXER_C
#define LEXER_C

#include <string.h>

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
  TOKEN_SEMICOLON,        /* ; */
  TOKEN_COMMA,            /* , */
  TOKEN_WORD,             /* bare word: command names, arguments */
  TOKEN_OPERATOR,         /* symbolic operators: +, -, >=, etc. */
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
  TOKEN_DOLLAR_PAREN,     /* $( infix-mode interpolation in strings */
  TOKEN_USE,              /* use keyword */
  TOKEN_STRUCT,           /* struct keyword */
  /* --- new operator tokens --- */
  TOKEN_PIPE,             /* | */
  TOKEN_ARROW,            /* -> */
  TOKEN_BACKSLASH,        /* \ (non-continuation) */
  TOKEN_BANG,             /* ! (standalone) */
  TOKEN_DOTDOT,           /* .. */
  TOKEN_AMP,              /* & (single) */
  TOKEN_AND,              /* && */
  TOKEN_OR,               /* || */
  TOKEN_NOT,              /* ~ */
  TOKEN_TILDE_AT,         /* ~@ */
  TOKEN_EQUALS,           /* = (single) */
  TOKEN_COLON,            /* : (single) */
  TOKEN_DOUBLE_COLON,     /* :: */
  /* --- new keyword tokens --- */
  TOKEN_PROC,             /* proc */
  TOKEN_DEFMACRO,         /* defmacro */
  TOKEN_QUOTE,            /* quote */
  TOKEN_SYNTAX_QUOTE,     /* syntax-quote */
  TOKEN_IF,               /* if */
  TOKEN_ELIF,             /* elif */
  TOKEN_ELSE,             /* else */
  TOKEN_WHILE,            /* while */
  TOKEN_FOR,              /* for */
  TOKEN_DEF,              /* def */
  TOKEN_MUT,              /* mut */
  TOKEN_SET,              /* set */
  TOKEN_MATCH,            /* match */
  TOKEN_RETURN,           /* return */
  TOKEN_BREAK,            /* break */
  TOKEN_CONTINUE,         /* continue */
  TOKEN_TRY,              /* try */
  TOKEN_CTX,              /* ctx */
  TOKEN_CARET_WORD,       /* ^identifier (caller-scope inside syntax-quote) */
  TOKEN_NEWLINE,          /* newline (\n or \r\n) */
  TOKEN_ERROR,            /* lexer error with descriptive message */
  TOKEN_EOF,              /* end of input */
  TOKEN_PRAGMA            /* #{ ... } pragma */
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
    const char* text;      /* TOKEN_WORD, TOKEN_STRING, etc. */
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
 */
LexResult lexer_lex(const char* source, arena_t* arena);

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

void lexer__arr_init(TokenArray* arr, arena_t* arena) {
  arr->cap    = LEXER_INITIAL_CAP;
  arr->count  = 0;
  arr->arena  = arena;
  arr->tokens = (Token*)arena_alloc(arena, sizeof(Token) * arr->cap);
}

void lexer__arr_push(TokenArray* arr, Token tok) {
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

void lexer__init(Lexer* lex, const char* source, arena_t* arena) {
  lex->source = source;
  lex->pos    = 0;
  lex->line   = 1;
  lex->col    = 1;
  lex->arena  = arena;
}

char lexer__peek(Lexer* lex) {
  return lex->source[lex->pos];
}

char lexer__advance(Lexer* lex) {
  char c = lex->source[lex->pos++];
  lex->col++;
  return c;
}

Token lexer__make_token(Lexer* lex, TokenType type,
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
 * Internal: Growable string buffer backed by arena
 * ------------------------------------------------------------------------- */

typedef struct {
  char*    data;
  uint32_t len;
  uint32_t cap;
  arena_t* arena;
} StringBuf;

void strbuf_init(StringBuf* sb, arena_t* arena) {
  sb->cap   = 64;
  sb->len   = 0;
  sb->arena = arena;
  sb->data  = (char*)arena_alloc(arena, sb->cap);
}

void strbuf_push(StringBuf* sb, char c) {
  if (sb->len >= sb->cap) {
    uint32_t new_cap = sb->cap * 2;
    char* new_data = (char*)arena_alloc(sb->arena, new_cap);
    memcpy(new_data, sb->data, sb->len);
    sb->data = new_data;
    sb->cap  = new_cap;
  }
  sb->data[sb->len++] = c;
}

/* -------------------------------------------------------------------------
 * Internal: UTF-8 encoding helper
 * ------------------------------------------------------------------------- */

/* Encode a Unicode codepoint as UTF-8. Returns number of bytes written. */
uint32_t lexer__encode_utf8(uint32_t cp, char* out) {
  if (cp <= 0x7F) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp <= 0x10FFFF) {
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0; /* invalid codepoint */
}

/* Parse a single hex digit. Returns -1 if not a hex digit. */
int lexer__hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* -------------------------------------------------------------------------
 * Internal: Character classification helpers
 * ------------------------------------------------------------------------- */

int lexer__is_word_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'
         || ((unsigned char)c >= 0x80);
}

int lexer__is_word_char(char c) {
  return lexer__is_word_start(c) || (c >= '0' && c <= '9') || c == '-'
         || c == '?' || c == '!';
}

/* Check if current position is a word char, but stop before -> (arrow) */
int lexer__is_word_char_no_arrow(Lexer* lex) {
  char c = lex->source[lex->pos];
  if (!lexer__is_word_char(c)) return 0;
  if (c == '-' && lex->source[lex->pos + 1] == '>') return 0;
  return 1;
}

int lexer__is_operator_char(char c) {
  return c == '!' || c == '%' || c == '&' || c == '*' || c == '+' ||
         c == '-' || c == '.' || c == '/' || c == ':' || c == '<' ||
         c == '=' || c == '>' || c == '?' || c == '@' || c == '\\' ||
         c == '^' || c == '|' || c == '~';
}

/* -------------------------------------------------------------------------
 * Internal: Arena-allocated error message for unexpected characters
 * ------------------------------------------------------------------------- */

const char* lexer__unexpected_char_msg(Lexer* lex, char c) {
  const char hex[] = "0123456789ABCDEF";
  unsigned char uc = (unsigned char)c;
  /* "unexpected character (0xNN)" = 27 chars + null */
  char* msg = (char*)arena_alloc(lex->arena, 28);
  memcpy(msg, "unexpected character (0x", 24);
  msg[24] = hex[uc >> 4];
  msg[25] = hex[uc & 0x0F];
  msg[26] = ')';
  msg[27] = '\0';
  return msg;
}

/* -------------------------------------------------------------------------
 * Internal: Forward declarations for mutually recursive functions
 * ------------------------------------------------------------------------- */

void lexer__lex_string_body(Lexer* lex, TokenArray* arr,
                                    uint32_t* error_count,
                                    uint32_t str_start, uint32_t str_line,
                                    uint32_t str_col);
void lexer__lex_interp_expr(Lexer* lex, TokenArray* arr,
                                    uint32_t* error_count);
void lexer__lex_interp_infix(Lexer* lex, TokenArray* arr,
                                     uint32_t* error_count);
void lexer__lex_triple_string_body(Lexer* lex, TokenArray* arr,
                                   uint32_t* error_count,
                                   uint32_t str_start, uint32_t str_line,
                                   uint32_t str_col);

/* -------------------------------------------------------------------------
 * Internal: String escape sequence handler
 * Returns non-NULL error message on failure, NULL on success.
 * ------------------------------------------------------------------------- */

const char* lexer__handle_escape(Lexer* lex, StringBuf* sb) {
  char esc = lexer__peek(lex);

  if (esc == '\0') return "unterminated string";

  switch (esc) {
    case '\\': strbuf_push(sb, '\\'); lexer__advance(lex); return NULL;
    case '"':  strbuf_push(sb, '"');  lexer__advance(lex); return NULL;
    case 'n':  strbuf_push(sb, '\n'); lexer__advance(lex); return NULL;
    case 't':  strbuf_push(sb, '\t'); lexer__advance(lex); return NULL;
    case 'r':  strbuf_push(sb, '\r'); lexer__advance(lex); return NULL;
    case '0':  strbuf_push(sb, '\0'); lexer__advance(lex); return NULL;
    case 'x': {
      lexer__advance(lex);
      int d1 = lexer__hex_digit(lexer__peek(lex));
      if (d1 < 0) return "invalid hex escape";
      lexer__advance(lex);
      int d2 = lexer__hex_digit(lexer__peek(lex));
      if (d2 < 0) return "invalid hex escape";
      lexer__advance(lex);
      strbuf_push(sb, (char)(d1 * 16 + d2));
      return NULL;
    }
    case 'u': {
      lexer__advance(lex);
      uint32_t cp = 0;
      int i;
      for (i = 0; i < 4; i++) {
        int d = lexer__hex_digit(lexer__peek(lex));
        if (d < 0) return "invalid unicode escape";
        cp = cp * 16 + (uint32_t)d;
        lexer__advance(lex);
      }
      {
        char utf8[4];
        uint32_t n = lexer__encode_utf8(cp, utf8);
        uint32_t j;
        for (j = 0; j < n; j++) strbuf_push(sb, utf8[j]);
      }
      return NULL;
    }
    case 'U': {
      lexer__advance(lex);
      uint32_t cp = 0;
      int i;
      for (i = 0; i < 8; i++) {
        int d = lexer__hex_digit(lexer__peek(lex));
        if (d < 0) return "invalid unicode escape";
        cp = cp * 16 + (uint32_t)d;
        lexer__advance(lex);
      }
      if (cp > 0x10FFFF) return "unicode codepoint out of range";
      {
        char utf8[4];
        uint32_t n = lexer__encode_utf8(cp, utf8);
        uint32_t j;
        for (j = 0; j < n; j++) strbuf_push(sb, utf8[j]);
      }
      return NULL;
    }
    default:
      lexer__advance(lex);
      return "invalid escape sequence";
  }
}

/* -------------------------------------------------------------------------
 * Internal: Skip to end of string for error recovery
 * Handles \" escapes and embedded newlines during skip.
 * ------------------------------------------------------------------------- */

void lexer__skip_to_string_end(Lexer* lex) {
  while (lexer__peek(lex) != '\0' && lexer__peek(lex) != '"') {
    char skip = lexer__peek(lex);
    if (skip == '\\') {
      lexer__advance(lex);
      if (lexer__peek(lex) != '\0') lexer__advance(lex);
    } else if (skip == '\n' || skip == '\r') {
      lexer__advance(lex);
      if (skip == '\r' && lexer__peek(lex) == '\n')
        lexer__advance(lex);
      lex->line++;
      lex->col = 1;
    } else {
      lexer__advance(lex);
    }
  }
  if (lexer__peek(lex) == '"')
    lexer__advance(lex);
}

/* -------------------------------------------------------------------------
 * Internal: Number literal lexer
 * Called when current char is a digit. Pushes one token.
 * ------------------------------------------------------------------------- */

void lexer__lex_number(Lexer* lex, TokenArray* arr,
                               uint32_t* error_count) {
  char c = lexer__peek(lex);
  uint32_t start = lex->pos;
  uint32_t sline = lex->line;
  uint32_t scol  = lex->col;

  /* Hex: 0x... */
  if (c == '0' && (lex->source[lex->pos + 1] == 'x' ||
                   lex->source[lex->pos + 1] == 'X')) {
    lexer__advance(lex); /* '0' */
    lexer__advance(lex); /* 'x' */
    int64_t val = 0;
    int has_digits = 0;
    for (;;) {
      char h = lexer__peek(lex);
      if (h >= '0' && h <= '9')      { val = val * 16 + (h - '0'); }
      else if (h >= 'a' && h <= 'f') { val = val * 16 + (h - 'a' + 10); }
      else if (h >= 'A' && h <= 'F') { val = val * 16 + (h - 'A' + 10); }
      else break;
      has_digits = 1;
      lexer__advance(lex);
    }
    if (!has_digits || lexer__is_word_start(lexer__peek(lex))) {
      while (lexer__is_word_char(lexer__peek(lex)))
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg = !has_digits
        ? "hex literal with no digits"
        : "invalid suffix on number";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }
    if (val > INT32_MAX) {
      Token tok = lexer__make_token(lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg =
        "integer literal out of i32 range (use [i64 ...] or [u64 ...])";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }
    {
      Token tok = lexer__make_token(lex, TOKEN_INT, start, sline, scol);
      tok.payload.int_val = (int32_t)val;
      lexer__arr_push(arr, tok);
    }
    return;
  }

  /* Binary: 0b... */
  if (c == '0' && (lex->source[lex->pos + 1] == 'b' ||
                   lex->source[lex->pos + 1] == 'B')) {
    lexer__advance(lex); /* '0' */
    lexer__advance(lex); /* 'b' */
    int64_t val = 0;
    int has_digits = 0;
    while (lexer__peek(lex) == '0' || lexer__peek(lex) == '1') {
      val = val * 2 + (lexer__peek(lex) - '0');
      has_digits = 1;
      lexer__advance(lex);
    }
    if (!has_digits || lexer__is_word_start(lexer__peek(lex)) ||
        (lexer__peek(lex) >= '2' && lexer__peek(lex) <= '9')) {
      while (lexer__is_word_char(lexer__peek(lex)))
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg = !has_digits
        ? "binary literal with no digits"
        : "invalid suffix on number";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }
    if (val > INT32_MAX) {
      Token tok = lexer__make_token(lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg =
        "integer literal out of i32 range (use [i64 ...] or [u64 ...])";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }
    {
      Token tok = lexer__make_token(lex, TOKEN_INT, start, sline, scol);
      tok.payload.int_val = (int32_t)val;
      lexer__arr_push(arr, tok);
    }
    return;
  }

  /* Decimal integer or float */
  {
    int64_t int_val = 0;
    while (lexer__peek(lex) >= '0' && lexer__peek(lex) <= '9') {
      int_val = int_val * 10 + (lexer__peek(lex) - '0');
      lexer__advance(lex);
    }

    /* Float: digits '.' digits */
    if (lexer__peek(lex) == '.' &&
        lex->source[lex->pos + 1] >= '0' && lex->source[lex->pos + 1] <= '9') {
      lexer__advance(lex); /* consume '.' */
      int64_t frac = 0;
      int64_t frac_div = 1;
      while (lexer__peek(lex) >= '0' && lexer__peek(lex) <= '9') {
        frac = frac * 10 + (lexer__peek(lex) - '0');
        frac_div *= 10;
        lexer__advance(lex);
      }
      if (lexer__is_word_start(lexer__peek(lex))) {
        while (lexer__is_word_char(lexer__peek(lex)))
          lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_ERROR, start, sline, scol);
        tok.payload.error_msg = "invalid suffix on number";
        lexer__arr_push(arr, tok);
        (*error_count)++;
        return;
      }
      {
        Token tok = lexer__make_token(lex, TOKEN_FLOAT, start, sline, scol);
        tok.payload.float_val = (float)int_val + (float)frac / (float)frac_div;
        lexer__arr_push(arr, tok);
      }
      return;
    }

    /* Integer — check for invalid suffix */
    if (lexer__is_word_start(lexer__peek(lex))) {
      while (lexer__is_word_char(lexer__peek(lex)))
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg = "invalid suffix on number";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }
    if (int_val > INT32_MAX) {
      Token tok = lexer__make_token(lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg =
        "integer literal out of i32 range (use [i64 ...] or [u64 ...])";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }
    {
      Token tok = lexer__make_token(lex, TOKEN_INT, start, sline, scol);
      tok.payload.int_val = (int32_t)int_val;
      lexer__arr_push(arr, tok);
    }
  }
}

/* -------------------------------------------------------------------------
 * Internal: String body lexer (handles content after opening " consumed)
 *
 * Emits either:
 *   - A single TOKEN_STRING (no interpolation), or
 *   - STRING_BEGIN, INTERP_VAR/INTERP_EXPR_START...INTERP_EXPR_END,
 *     STRING_PART (repeated), STRING_END
 * On error: emits TOKEN_ERROR.
 * ------------------------------------------------------------------------- */

void lexer__lex_string_body(Lexer* lex, TokenArray* arr,
                                    uint32_t* error_count,
                                    uint32_t str_start, uint32_t str_line,
                                    uint32_t str_col) {
  StringBuf sb;
  strbuf_init(&sb, lex->arena);
  const char* str_error = NULL;
  int has_interp = 0;
  uint32_t seg_start = str_start;
  uint32_t seg_line  = str_line;
  uint32_t seg_col   = str_col;

  while (lexer__peek(lex) != '\0' && lexer__peek(lex) != '"' && !str_error) {
    char ch = lexer__peek(lex);

    /* Embedded newlines */
    if (ch == '\n' || ch == '\r') {
      lexer__advance(lex);
      if (ch == '\r' && lexer__peek(lex) == '\n')
        lexer__advance(lex);
      strbuf_push(&sb, '\n');
      lex->line++;
      lex->col = 1;
      continue;
    }

    /* Escape sequences */
    if (ch == '\\') {
      lexer__advance(lex); /* consume backslash */
      str_error = lexer__handle_escape(lex, &sb);
      continue;
    }

    /* Interpolation: $var, $[expr], or $(expr) */
    if (ch == '$') {
      char next_ch = lex->source[lex->pos + 1];

      if (lexer__is_word_start(next_ch) || next_ch == '[' || next_ch == '(') {
        /* Emit current text segment as STRING_BEGIN or STRING_PART */
        strbuf_push(&sb, '\0');
        {
          Token seg_tok;
          memset(&seg_tok, 0, sizeof(Token));
          seg_tok.type   = has_interp ? TOKEN_STRING_PART : TOKEN_STRING_BEGIN;
          seg_tok.line   = seg_line;
          seg_tok.column = seg_col;
          seg_tok.offset = seg_start;
          seg_tok.length = lex->pos - seg_start;
          seg_tok.payload.text = sb.data;
          lexer__arr_push(arr, seg_tok);
        }
        has_interp = 1;
        strbuf_init(&sb, lex->arena); /* reset for next segment */

        if (next_ch == '[') {
          /* $[expr] interpolation */
          uint32_t expr_start = lex->pos;
          uint32_t expr_line  = lex->line;
          uint32_t expr_col   = lex->col;
          lexer__advance(lex); /* consume '$' */
          lexer__advance(lex); /* consume '[' */
          {
            Token expr_tok = lexer__make_token(lex, TOKEN_INTERP_EXPR_START,
                                                expr_start, expr_line, expr_col);
            lexer__arr_push(arr, expr_tok);
          }
          lexer__lex_interp_expr(lex, arr, error_count);
        } else if (next_ch == '(') {
          /* $(expr) infix interpolation */
          uint32_t dp_start = lex->pos;
          uint32_t dp_line  = lex->line;
          uint32_t dp_col   = lex->col;
          lexer__advance(lex); /* consume '$' */
          lexer__advance(lex); /* consume '(' */
          {
            Token dp_tok = lexer__make_token(lex, TOKEN_DOLLAR_PAREN,
                                              dp_start, dp_line, dp_col);
            lexer__arr_push(arr, dp_tok);
          }
          lexer__lex_interp_infix(lex, arr, error_count);
        } else {
          /* $var interpolation */
          uint32_t var_start = lex->pos;
          uint32_t var_line  = lex->line;
          uint32_t var_col   = lex->col;
          lexer__advance(lex); /* consume '$' */
          while (lexer__is_word_char_no_arrow(lex))
            lexer__advance(lex);
          {
            Token var_tok = lexer__make_token(lex, TOKEN_INTERP_VAR,
                                               var_start, var_line, var_col);
            var_tok.payload.text = lex->source + var_start + 1; /* skip '$' */
            lexer__arr_push(arr, var_tok);
          }
        }

        seg_start = lex->pos;
        seg_line  = lex->line;
        seg_col   = lex->col;
        continue;
      }
    }

    /* Regular character (including $ not followed by identifier/bracket) */
    strbuf_push(&sb, ch);
    lexer__advance(lex);
  }

  /* Check for unterminated string */
  if (!str_error && lexer__peek(lex) == '\0') {
    str_error = "unterminated string";
  }

  if (str_error) {
    /* Skip remaining string content for error recovery */
    lexer__skip_to_string_end(lex);
    Token tok = lexer__make_token(lex, TOKEN_ERROR, str_start, str_line, str_col);
    tok.payload.error_msg = str_error;
    lexer__arr_push(arr, tok);
    (*error_count)++;
  } else {
    lexer__advance(lex); /* consume closing '"' */
    strbuf_push(&sb, '\0');

    if (has_interp) {
      Token end_tok;
      memset(&end_tok, 0, sizeof(Token));
      end_tok.type   = TOKEN_STRING_END;
      end_tok.line   = seg_line;
      end_tok.column = seg_col;
      end_tok.offset = seg_start;
      end_tok.length = lex->pos - seg_start;
      end_tok.payload.text = sb.data;
      lexer__arr_push(arr, end_tok);
    } else {
      Token tok = lexer__make_token(lex, TOKEN_STRING, str_start, str_line, str_col);
      tok.payload.text = sb.data;
      lexer__arr_push(arr, tok);
    }
  }
}

/* -------------------------------------------------------------------------
 * Internal: Triple-quoted string lexer ("""...""")
 *
 * Scans until closing """, supports interpolation ($var, $[expr], $(expr)).
 * Applies Kotlin-style indent stripping: the indentation of the closing """
 * is stripped from each content line.
 * Called after the opening """ has been consumed.
 * ------------------------------------------------------------------------- */

void lexer__lex_triple_string_body(Lexer* lex, TokenArray* arr,
                                   uint32_t* error_count,
                                   uint32_t str_start, uint32_t str_line,
                                   uint32_t str_col) {
  StringBuf sb;
  strbuf_init(&sb, lex->arena);
  const char* str_error = NULL;
  int has_interp = 0;
  uint32_t seg_start = str_start;
  uint32_t seg_line  = str_line;
  uint32_t seg_col   = str_col;

  /* Scan until closing """ */
  while (lexer__peek(lex) != '\0' && !str_error) {
    /* Check for closing """ */
    if (lexer__peek(lex) == '"' &&
        lex->source[lex->pos + 1] == '"' &&
        lex->source[lex->pos + 2] == '"') {
      break; /* found closing """ */
    }
    char ch = lexer__peek(lex);

    /* Embedded newlines */
    if (ch == '\n' || ch == '\r') {
      lexer__advance(lex);
      if (ch == '\r' && lexer__peek(lex) == '\n')
        lexer__advance(lex);
      strbuf_push(&sb, '\n');
      lex->line++;
      lex->col = 1;
      continue;
    }

    /* Escape sequences (same as regular strings) */
    if (ch == '\\') {
      lexer__advance(lex);
      str_error = lexer__handle_escape(lex, &sb);
      continue;
    }

    /* Interpolation: $var, $[expr], or $(expr) */
    if (ch == '$') {
      char next_ch = lex->source[lex->pos + 1];
      if (lexer__is_word_start(next_ch) || next_ch == '[' || next_ch == '(') {
        strbuf_push(&sb, '\0');
        {
          Token seg_tok;
          memset(&seg_tok, 0, sizeof(Token));
          seg_tok.type   = has_interp ? TOKEN_STRING_PART : TOKEN_STRING_BEGIN;
          seg_tok.line   = seg_line;
          seg_tok.column = seg_col;
          seg_tok.offset = seg_start;
          seg_tok.length = lex->pos - seg_start;
          seg_tok.payload.text = sb.data;
          lexer__arr_push(arr, seg_tok);
        }
        has_interp = 1;
        strbuf_init(&sb, lex->arena);

        if (next_ch == '[') {
          uint32_t expr_start = lex->pos;
          uint32_t expr_line  = lex->line;
          uint32_t expr_col   = lex->col;
          lexer__advance(lex); /* consume '$' */
          lexer__advance(lex); /* consume '[' */
          {
            Token expr_tok = lexer__make_token(lex, TOKEN_INTERP_EXPR_START,
                                               expr_start, expr_line, expr_col);
            lexer__arr_push(arr, expr_tok);
          }
          lexer__lex_interp_expr(lex, arr, error_count);
        } else if (next_ch == '(') {
          uint32_t dp_start = lex->pos;
          uint32_t dp_line  = lex->line;
          uint32_t dp_col   = lex->col;
          lexer__advance(lex); /* consume '$' */
          lexer__advance(lex); /* consume '(' */
          {
            Token dp_tok = lexer__make_token(lex, TOKEN_DOLLAR_PAREN,
                                             dp_start, dp_line, dp_col);
            lexer__arr_push(arr, dp_tok);
          }
          lexer__lex_interp_infix(lex, arr, error_count);
        } else {
          uint32_t var_start = lex->pos;
          uint32_t var_line  = lex->line;
          uint32_t var_col   = lex->col;
          lexer__advance(lex); /* consume '$' */
          while (lexer__is_word_char_no_arrow(lex))
            lexer__advance(lex);
          {
            Token var_tok = lexer__make_token(lex, TOKEN_INTERP_VAR,
                                              var_start, var_line, var_col);
            var_tok.payload.text = lex->source + var_start + 1;
            lexer__arr_push(arr, var_tok);
          }
        }

        seg_start = lex->pos;
        seg_line  = lex->line;
        seg_col   = lex->col;
        continue;
      }
    }

    /* Regular character */
    strbuf_push(&sb, ch);
    lexer__advance(lex);
  }

  /* Check for unterminated string */
  if (!str_error && lexer__peek(lex) == '\0') {
    str_error = "unterminated triple-quoted string";
  }

  if (str_error) {
    Token tok = lexer__make_token(lex, TOKEN_ERROR, str_start, str_line, str_col);
    tok.payload.error_msg = str_error;
    lexer__arr_push(arr, tok);
    (*error_count)++;
    return;
  }

  /* Consume closing """ */
  lexer__advance(lex); /* " */
  lexer__advance(lex); /* " */
  lexer__advance(lex); /* " */

  strbuf_push(&sb, '\0');

  /* --- Kotlin-style indent stripping ---
   * Find the indentation of the closing """ (its column - 1 gives the
   * number of leading spaces/tabs to strip from each line).
   * We look at lex->col which now points just past the closing """.
   * The closing """ started at col - 3, and the indent is col - 4.
   * Actually: we need the whitespace *before* the closing """.
   * The simplest approach: count whitespace on the last line of the
   * raw content (everything after the last \n before closing """). */

  /* Determine strip prefix from content: last line's leading whitespace */
  const char* raw = has_interp ? NULL : sb.data;
  if (!has_interp && raw != NULL) {
    /* Find the last newline in the string content */
    uint32_t content_len = (uint32_t)strlen(raw);
    int last_nl = -1;
    for (int i = (int)content_len - 1; i >= 0; i--) {
      if (raw[i] == '\n') { last_nl = i; break; }
    }

    if (last_nl >= 0) {
      /* Measure indent after last newline */
      uint32_t indent = 0;
      for (uint32_t i = (uint32_t)(last_nl + 1); i < content_len; i++) {
        if (raw[i] == ' ' || raw[i] == '\t') indent++;
        else break;
      }

      /* Check if last line is whitespace-only (closing """ on its own line) */
      bool last_line_blank = true;
      for (uint32_t i = (uint32_t)(last_nl + 1); i < content_len; i++) {
        if (raw[i] != ' ' && raw[i] != '\t') { last_line_blank = false; break; }
      }

      if (last_line_blank && indent > 0) {
        /* Strip 'indent' leading chars from each line, and remove the
         * trailing blank line (it's just the closing """'s indent). */
        StringBuf stripped;
        strbuf_init(&stripped, lex->arena);

        /* Skip leading newline if content starts with one */
        uint32_t ci = 0;
        if (ci < content_len && raw[ci] == '\n') ci++;

        while (ci < content_len) {
          /* Skip up to 'indent' whitespace chars */
          uint32_t skipped = 0;
          while (ci < content_len && skipped < indent &&
                 (raw[ci] == ' ' || raw[ci] == '\t')) {
            ci++;
            skipped++;
          }
          /* Copy rest of line */
          while (ci < content_len && raw[ci] != '\n') {
            strbuf_push(&stripped, raw[ci]);
            ci++;
          }
          if (ci < content_len && raw[ci] == '\n') {
            /* Check if this newline leads to the final blank line */
            bool is_final_newline = true;
            for (uint32_t j = ci + 1; j < content_len; j++) {
              if (raw[j] != ' ' && raw[j] != '\t') {
                is_final_newline = false;
                break;
              }
            }
            if (!is_final_newline) {
              strbuf_push(&stripped, '\n');
            }
            ci++; /* consume \n */
          }
        }
        strbuf_push(&stripped, '\0');
        /* Replace sb with stripped content */
        sb.data = stripped.data;
      }
    }
  }

  if (has_interp) {
    Token end_tok;
    memset(&end_tok, 0, sizeof(Token));
    end_tok.type   = TOKEN_STRING_END;
    end_tok.line   = seg_line;
    end_tok.column = seg_col;
    end_tok.offset = seg_start;
    end_tok.length = lex->pos - seg_start;
    end_tok.payload.text = sb.data;
    lexer__arr_push(arr, end_tok);
  } else {
    Token tok = lexer__make_token(lex, TOKEN_STRING, str_start, str_line, str_col);
    tok.payload.text = sb.data;
    lexer__arr_push(arr, tok);
  }
}

/* -------------------------------------------------------------------------
 * Internal: Interpolation expression lexer (inside $[...])
 *
 * Lexes tokens until matching ] is found, tracking bracket depth.
 * Emits INTERP_EXPR_END for the closing ].
 * Called after $[ has been consumed and INTERP_EXPR_START emitted.
 * ------------------------------------------------------------------------- */

void lexer__lex_interp_expr(Lexer* lex, TokenArray* arr,
                                    uint32_t* error_count) {
  int depth = 1;

  while (depth > 0) {
    char c = lexer__peek(lex);

    /* EOF — unterminated expression */
    if (c == '\0') {
      Token tok = lexer__make_token(lex, TOKEN_ERROR, lex->pos, lex->line, lex->col);
      tok.length = 0;
      tok.payload.error_msg = "unterminated interpolation expression";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }

    /* Skip whitespace */
    if (c == ' ' || c == '\t') {
      lexer__advance(lex);
      continue;
    }

    /* Comments */
    if (c == '#') {
      lexer__advance(lex);
      while (lexer__peek(lex) != '\0' && lexer__peek(lex) != '\n'
             && lexer__peek(lex) != '\r') {
        lexer__advance(lex);
      }
      continue;
    }

    /* Newlines */
    if (c == '\n' || c == '\r') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      if (c == '\r' && lexer__peek(lex) == '\n')
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_NEWLINE, s, sl, sc);
      lexer__arr_push(arr, tok);
      lex->line++;
      lex->col = 1;
      continue;
    }

    /* Closing bracket */
    if (c == ']') {
      depth--;
      if (depth == 0) {
        uint32_t s  = lex->pos;
        uint32_t sl = lex->line;
        uint32_t sc = lex->col;
        lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_INTERP_EXPR_END, s, sl, sc);
        lexer__arr_push(arr, tok);
        return;
      }
      /* Nested ] — emit RBRACKET */
      {
        uint32_t s  = lex->pos;
        uint32_t sl = lex->line;
        uint32_t sc = lex->col;
        lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_RBRACKET, s, sl, sc);
        lexer__arr_push(arr, tok);
      }
      continue;
    }

    /* Opening bracket */
    if (c == '[') {
      depth++;
      {
        uint32_t s  = lex->pos;
        uint32_t sl = lex->line;
        uint32_t sc = lex->col;
        lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_LBRACKET, s, sl, sc);
        lexer__arr_push(arr, tok);
      }
      continue;
    }

    /* Other delimiters */
    if (c == '{' || c == '}' || c == '(' || c == ')') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      TokenType type;
      switch (c) {
        case '{': type = TOKEN_LBRACE;  break;
        case '}': type = TOKEN_RBRACE;  break;
        case '(': type = TOKEN_LPAREN;  break;
        default:  type = TOKEN_RPAREN;  break;
      }
      Token tok = lexer__make_token(lex, type, s, sl, sc);
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Numbers */
    if (c >= '0' && c <= '9') {
      lexer__lex_number(lex, arr, error_count);
      continue;
    }

    /* Words */
    if (lexer__is_word_start(c)) {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      while (lexer__is_word_char_no_arrow(lex))
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_WORD, s, sl, sc);
      tok.payload.text = lex->source + s;
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Operators */
    if (lexer__is_operator_char(c)) {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      while (lexer__is_operator_char(lexer__peek(lex)))
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_OPERATOR, s, sl, sc);
      tok.payload.text = lex->source + s;
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Variable references: $identifier or $[ */
    if (c == '$') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      char next = lexer__peek(lex);

      if (next == '[') {
        lexer__advance(lex);
        depth++; /* $[ introduces another bracket level */
        Token tok = lexer__make_token(lex, TOKEN_DOLLAR_BRACKET, s, sl, sc);
        lexer__arr_push(arr, tok);
        continue;
      }
      if (lexer__is_word_start(next)) {
        while (lexer__is_word_char_no_arrow(lex))
          lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_VAR, s, sl, sc);
        tok.payload.text = lex->source + s + 1;
        lexer__arr_push(arr, tok);
        continue;
      }
      {
        Token tok = lexer__make_token(lex, TOKEN_ERROR, s, sl, sc);
        tok.payload.error_msg = next == '\0'
          ? "unexpected end of input after $"
          : "invalid character after $";
        lexer__arr_push(arr, tok);
        (*error_count)++;
      }
      continue;
    }

    /* Strings inside expressions (may recursively contain interpolation) */
    if (c == '"') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      lexer__lex_string_body(lex, arr, error_count, s, sl, sc);
      continue;
    }

    /* Unrecognized character */
    {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_ERROR, s, sl, sc);
      tok.payload.error_msg = lexer__unexpected_char_msg(lex, c);
      lexer__arr_push(arr, tok);
      (*error_count)++;
    }
  }
}

/* -------------------------------------------------------------------------
 * Internal: Infix interpolation expression lexer (inside $(...))
 *
 * Lexes tokens until matching ) is found, tracking paren depth.
 * Emits TOKEN_RPAREN for the closing ).
 * Called after $( has been consumed and TOKEN_DOLLAR_PAREN emitted.
 * ------------------------------------------------------------------------- */

void lexer__lex_interp_infix(Lexer* lex, TokenArray* arr,
                                     uint32_t* error_count) {
  int depth = 1;

  while (depth > 0) {
    char c = lexer__peek(lex);

    /* EOF — unterminated expression */
    if (c == '\0') {
      Token tok = lexer__make_token(lex, TOKEN_ERROR, lex->pos, lex->line, lex->col);
      tok.length = 0;
      tok.payload.error_msg = "unterminated $() interpolation expression";
      lexer__arr_push(arr, tok);
      (*error_count)++;
      return;
    }

    /* Skip whitespace */
    if (c == ' ' || c == '\t') {
      lexer__advance(lex);
      continue;
    }

    /* Comments */
    if (c == '#') {
      lexer__advance(lex);
      while (lexer__peek(lex) != '\0' && lexer__peek(lex) != '\n'
             && lexer__peek(lex) != '\r') {
        lexer__advance(lex);
      }
      continue;
    }

    /* Newlines */
    if (c == '\n' || c == '\r') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      if (c == '\r' && lexer__peek(lex) == '\n')
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_NEWLINE, s, sl, sc);
      lexer__arr_push(arr, tok);
      lex->line++;
      lex->col = 1;
      continue;
    }

    /* Closing paren */
    if (c == ')') {
      depth--;
      if (depth == 0) {
        uint32_t s  = lex->pos;
        uint32_t sl = lex->line;
        uint32_t sc = lex->col;
        lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_RPAREN, s, sl, sc);
        lexer__arr_push(arr, tok);
        return;
      }
      /* Nested ) — emit RPAREN */
      {
        uint32_t s  = lex->pos;
        uint32_t sl = lex->line;
        uint32_t sc = lex->col;
        lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_RPAREN, s, sl, sc);
        lexer__arr_push(arr, tok);
      }
      continue;
    }

    /* Opening paren */
    if (c == '(') {
      depth++;
      {
        uint32_t s  = lex->pos;
        uint32_t sl = lex->line;
        uint32_t sc = lex->col;
        lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_LPAREN, s, sl, sc);
        lexer__arr_push(arr, tok);
      }
      continue;
    }

    /* Brackets and braces */
    if (c == '[' || c == ']' || c == '{' || c == '}') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      TokenType type;
      switch (c) {
        case '[': type = TOKEN_LBRACKET; break;
        case ']': type = TOKEN_RBRACKET; break;
        case '{': type = TOKEN_LBRACE;   break;
        default:  type = TOKEN_RBRACE;   break;
      }
      Token tok = lexer__make_token(lex, type, s, sl, sc);
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Numbers */
    if (c >= '0' && c <= '9') {
      lexer__lex_number(lex, arr, error_count);
      continue;
    }

    /* Words */
    if (lexer__is_word_start(c)) {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      while (lexer__is_word_char_no_arrow(lex))
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_WORD, s, sl, sc);
      tok.payload.text = lex->source + s;
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Caret-prefixed identifier: ^name (only valid inside syntax-quote;
       parser enforces context). Falls through to operator tokenization
       if '^' is not followed by a word-start character (e.g., '^=' or
       bare '^'). */
    if (c == '^' && lexer__is_word_start(lex->source[lex->pos + 1])) {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);  /* consume '^' */
      uint32_t word_start = lex->pos;
      while (lexer__is_word_char_no_arrow(lex))
        lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_CARET_WORD, s, sl, sc);
      tok.payload.text = lex->source + word_start;
      /* Override length to be the word portion (excluding '^') so that
         parser consumers can read the bare name directly from the
         payload pointer. The token's own length field describes the
         full span including the '^'. */
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Operators */
    if (lexer__is_operator_char(c)) {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      TokenType otype;

      switch (c) {
        case '|':
          lexer__advance(lex);
          if (lexer__peek(lex) == '|') {
            lexer__advance(lex);
            otype = TOKEN_OR;
          } else {
            otype = TOKEN_PIPE;
          }
          break;
        case '&':
          lexer__advance(lex);
          if (lexer__peek(lex) == '&') {
            lexer__advance(lex);
            otype = TOKEN_AND;
          } else {
            otype = TOKEN_AMP;
          }
          break;
        case '~':
          lexer__advance(lex);
          if (lexer__peek(lex) == '@') {
            lexer__advance(lex);
            otype = TOKEN_TILDE_AT;
          } else {
            otype = TOKEN_NOT;
          }
          break;
        case '=':
          lexer__advance(lex);
          if (lexer__peek(lex) == '=') {
            lexer__advance(lex);
            otype = TOKEN_OPERATOR;
          } else {
            otype = TOKEN_EQUALS;
          }
          break;
        case '-':
          lexer__advance(lex);
          if (lexer__peek(lex) == '>') {
            lexer__advance(lex);
            otype = TOKEN_ARROW;
          } else {
            while (lexer__is_operator_char(lexer__peek(lex)))
              lexer__advance(lex);
            otype = TOKEN_OPERATOR;
          }
          break;
        default:
          lexer__advance(lex);
          while (lexer__is_operator_char(lexer__peek(lex)))
            lexer__advance(lex);
          otype = TOKEN_OPERATOR;
          break;
      }

      Token tok = lexer__make_token(lex, otype, s, sl, sc);
      tok.payload.text = lex->source + s;
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Variable references: $identifier or $[ */
    if (c == '$') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      char next = lexer__peek(lex);

      if (next == '[') {
        lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_DOLLAR_BRACKET, s, sl, sc);
        lexer__arr_push(arr, tok);
        continue;
      }
      if (lexer__is_word_start(next)) {
        while (lexer__is_word_char_no_arrow(lex))
          lexer__advance(lex);
        Token tok = lexer__make_token(lex, TOKEN_VAR, s, sl, sc);
        tok.payload.text = lex->source + s + 1;
        lexer__arr_push(arr, tok);
        continue;
      }
      {
        Token tok = lexer__make_token(lex, TOKEN_ERROR, s, sl, sc);
        tok.payload.error_msg = next == '\0'
          ? "unexpected end of input after $"
          : "invalid character after $";
        lexer__arr_push(arr, tok);
        (*error_count)++;
      }
      continue;
    }

    /* Strings inside expressions */
    if (c == '"') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      lexer__lex_string_body(lex, arr, error_count, s, sl, sc);
      continue;
    }

    /* Comma */
    if (c == ',') {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_COMMA, s, sl, sc);
      lexer__arr_push(arr, tok);
      continue;
    }

    /* Unrecognized character */
    {
      uint32_t s  = lex->pos;
      uint32_t sl = lex->line;
      uint32_t sc = lex->col;
      lexer__advance(lex);
      Token tok = lexer__make_token(lex, TOKEN_ERROR, s, sl, sc);
      tok.payload.error_msg = lexer__unexpected_char_msg(lex, c);
      lexer__arr_push(arr, tok);
      (*error_count)++;
    }
  }
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
    if (c == '[' || c == ']' || c == '{' || c == '}' || c == '(' || c == ')' || c == ';' || c == ',') {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex);
      TokenType type;
      switch (c) {
        case '[': type = TOKEN_LBRACKET;  break;
        case ']': type = TOKEN_RBRACKET;  break;
        case '{': type = TOKEN_LBRACE;    break;
        case '}': type = TOKEN_RBRACE;    break;
        case '(': type = TOKEN_LPAREN;    break;
        case ';': type = TOKEN_SEMICOLON; break;
        case ',': type = TOKEN_COMMA;     break;
        default:  type = TOKEN_RPAREN;    break;
      }
      Token tok = lexer__make_token(&lex, type, start, sline, scol);
      lexer__arr_push(&arr, tok);
      continue;
    }

    /* Pragmas: #{ ... } or Comments: # to end of line */
    if (c == '#') {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex);

      if (lexer__peek(&lex) == '{') {
        /* Pragma: #{ ... } — scan to matching } */
        lexer__advance(&lex); /* consume '{' */
        StringBuf sb;
        strbuf_init(&sb, lex.arena);
        int depth = 1;
        while (lexer__peek(&lex) != '\0' && depth > 0) {
          char pc = lexer__peek(&lex);
          if (pc == '{') depth++;
          else if (pc == '}') { depth--; if (depth == 0) break; }
          if (pc == '\n' || pc == '\r') {
            lexer__advance(&lex);
            if (pc == '\r' && lexer__peek(&lex) == '\n')
              lexer__advance(&lex);
            strbuf_push(&sb, '\n');
            lex.line++;
            lex.col = 1;
            continue;
          }
          strbuf_push(&sb, pc);
          lexer__advance(&lex);
        }
        if (lexer__peek(&lex) == '}') {
          lexer__advance(&lex); /* consume closing '}' */
        } else {
          Token tok = lexer__make_token(&lex, TOKEN_ERROR, start, sline, scol);
          tok.payload.error_msg = "unterminated pragma";
          lexer__arr_push(&arr, tok);
          error_count++;
          continue;
        }
        strbuf_push(&sb, '\0');
        Token tok = lexer__make_token(&lex, TOKEN_PRAGMA, start, sline, scol);
        tok.payload.text = sb.data;
        lexer__arr_push(&arr, tok);
        continue;
      }

      /* Regular comment: skip to end of line */
      while (lexer__peek(&lex) != '\0' && lexer__peek(&lex) != '\n'
             && lexer__peek(&lex) != '\r') {
        lexer__advance(&lex);
      }
      continue;
    }

    /* Number literals: start with a digit */
    if (c >= '0' && c <= '9') {
      lexer__lex_number(&lex, &arr, &error_count);
      continue;
    }

    /* Words: start with letter, underscore, or UTF-8 byte */
    if (lexer__is_word_start(c)) {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      while (lexer__is_word_char_no_arrow(&lex)) {
        lexer__advance(&lex);
      }
      uint32_t wlen = lex.pos - start;
      const char* wstart = lex.source + start;
      TokenType wtype = TOKEN_WORD;
      switch (wlen) {
        case 2:
          if (memcmp(wstart, "if", 2) == 0)     wtype = TOKEN_IF;
          else if (memcmp(wstart, "or", 2) == 0) {} /* keep as TOKEN_WORD */
          break;
        case 3:
          if      (memcmp(wstart, "use", 3) == 0)  wtype = TOKEN_USE;
          else if (memcmp(wstart, "def", 3) == 0)  wtype = TOKEN_DEF;
          else if (memcmp(wstart, "mut", 3) == 0)  wtype = TOKEN_MUT;
          else if (memcmp(wstart, "set", 3) == 0)  wtype = TOKEN_SET;
          else if (memcmp(wstart, "for", 3) == 0)  wtype = TOKEN_FOR;
          else if (memcmp(wstart, "try", 3) == 0)  wtype = TOKEN_TRY;
          else if (memcmp(wstart, "ctx", 3) == 0)  wtype = TOKEN_CTX;
          break;
        case 4:
          if      (memcmp(wstart, "proc", 4) == 0) wtype = TOKEN_PROC;
          else if (memcmp(wstart, "elif", 4) == 0) wtype = TOKEN_ELIF;
          else if (memcmp(wstart, "else", 4) == 0) wtype = TOKEN_ELSE;
          break;
        case 5:
          if      (memcmp(wstart, "while", 5) == 0) wtype = TOKEN_WHILE;
          else if (memcmp(wstart, "match", 5) == 0) wtype = TOKEN_MATCH;
          else if (memcmp(wstart, "break", 5) == 0) wtype = TOKEN_BREAK;
          else if (memcmp(wstart, "quote", 5) == 0) wtype = TOKEN_QUOTE;
          break;
        case 6:
          if      (memcmp(wstart, "return", 6) == 0) wtype = TOKEN_RETURN;
          else if (memcmp(wstart, "struct", 6) == 0) wtype = TOKEN_STRUCT;
          break;
        case 8:
          if (memcmp(wstart, "continue", 8) == 0) wtype = TOKEN_CONTINUE;
          else if (memcmp(wstart, "defmacro", 8) == 0) wtype = TOKEN_DEFMACRO;
          break;
        case 12:
          if (memcmp(wstart, "syntax-quote", 12) == 0) wtype = TOKEN_SYNTAX_QUOTE;
          break;
        /* defstruct keyword removed — use struct instead */
        default:
          break;
      }
      Token tok = lexer__make_token(&lex, wtype, start, sline, scol);
      tok.payload.text = lex.source + start;
      lexer__arr_push(&arr, tok);
      continue;
    }

    /* Caret-prefixed identifier: ^name (only valid inside syntax-quote;
       parser enforces context). Falls through to operator tokenization
       if '^' is not followed by a word-start character. */
    if (c == '^' && lexer__is_word_start(lex.source[lex.pos + 1])) {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex);  /* consume '^' */
      uint32_t word_start = lex.pos;
      while (lexer__is_word_char_no_arrow(&lex)) {
        lexer__advance(&lex);
      }
      Token tok = lexer__make_token(&lex, TOKEN_CARET_WORD, start, sline, scol);
      tok.payload.text = lex.source + word_start;
      lexer__arr_push(&arr, tok);
      continue;
    }

    /* Backslash: line continuation if followed by newline, else TOKEN_BACKSLASH */
    if (c == '\\') {
      char next = lex.source[lex.pos + 1];
      if (next == '\n') {
        lexer__advance(&lex); /* consume '\' */
        lexer__advance(&lex); /* consume '\n' */
        lex.line++;
        lex.col = 1;
        continue;
      }
      if (next == '\r') {
        lexer__advance(&lex); /* consume '\' */
        lexer__advance(&lex); /* consume '\r' */
        if (lexer__peek(&lex) == '\n') {
          lexer__advance(&lex); /* consume '\n' */
        }
        lex.line++;
        lex.col = 1;
        continue;
      }
      /* Not a continuation — emit TOKEN_BACKSLASH */
      {
        uint32_t start = lex.pos;
        uint32_t sline = lex.line;
        uint32_t scol  = lex.col;
        lexer__advance(&lex);
        Token tok = lexer__make_token(&lex, TOKEN_BACKSLASH, start, sline, scol);
        tok.payload.text = lex.source + start;
        lexer__arr_push(&arr, tok);
        continue;
      }
    }

    /* Operators: specific tokens for new operators, greedy scan for the rest */
    if (lexer__is_operator_char(c)) {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      TokenType otype;

      switch (c) {
        case '|':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == '|') {
            lexer__advance(&lex);
            otype = TOKEN_OR;
          } else {
            otype = TOKEN_PIPE;
          }
          break;
        case '&':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == '&') {
            lexer__advance(&lex);
            otype = TOKEN_AND;
          } else {
            otype = TOKEN_AMP;
          }
          break;
        case '~':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == '@') {
            lexer__advance(&lex);
            otype = TOKEN_TILDE_AT;
          } else {
            otype = TOKEN_NOT;
          }
          break;
        case '=':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == '=') {
            lexer__advance(&lex);
            otype = TOKEN_OPERATOR; /* == stays TOKEN_OPERATOR */
          } else {
            otype = TOKEN_EQUALS;
          }
          break;
        case ':':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == ':') {
            lexer__advance(&lex);
            otype = TOKEN_DOUBLE_COLON;
          } else {
            otype = TOKEN_COLON;
          }
          break;
        case '!':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == '=') {
            lexer__advance(&lex);
            otype = TOKEN_OPERATOR; /* != stays TOKEN_OPERATOR */
          } else {
            otype = TOKEN_BANG;
          }
          break;
        case '-':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == '>') {
            lexer__advance(&lex);
            otype = TOKEN_ARROW;
          } else {
            /* greedy scan remaining operator chars */
            while (lexer__is_operator_char(lexer__peek(&lex)))
              lexer__advance(&lex);
            otype = TOKEN_OPERATOR;
          }
          break;
        case '.':
          lexer__advance(&lex);
          if (lexer__peek(&lex) == '.') {
            lexer__advance(&lex);
            if (lexer__is_operator_char(lexer__peek(&lex))) {
              /* ..<, ..=, etc. — greedy scan, regular operator */
              while (lexer__is_operator_char(lexer__peek(&lex)))
                lexer__advance(&lex);
              otype = TOKEN_OPERATOR;
            } else {
              otype = TOKEN_DOTDOT;
            }
          } else {
            /* greedy scan remaining operator chars */
            while (lexer__is_operator_char(lexer__peek(&lex)))
              lexer__advance(&lex);
            otype = TOKEN_OPERATOR;
          }
          break;
        default:
          lexer__advance(&lex);
          while (lexer__is_operator_char(lexer__peek(&lex)))
            lexer__advance(&lex);
          otype = TOKEN_OPERATOR;
          break;
      }

      Token tok = lexer__make_token(&lex, otype, start, sline, scol);
      tok.payload.text = lex.source + start;
      lexer__arr_push(&arr, tok);
      continue;
    }

    /* Variable references: $identifier, $[, or $( */
    if (c == '$') {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex); /* consume '$' */

      char next = lexer__peek(&lex);

      /* $[ → DOLLAR_BRACKET */
      if (next == '[') {
        lexer__advance(&lex); /* consume '[' */
        Token tok = lexer__make_token(&lex, TOKEN_DOLLAR_BRACKET, start, sline, scol);
        lexer__arr_push(&arr, tok);
        continue;
      }

      /* $( → DOLLAR_PAREN */
      if (next == '(') {
        lexer__advance(&lex); /* consume '(' */
        Token tok = lexer__make_token(&lex, TOKEN_DOLLAR_PAREN, start, sline, scol);
        lexer__arr_push(&arr, tok);
        continue;
      }

      /* $identifier → VAR */
      if (lexer__is_word_start(next)) {
        while (lexer__is_word_char_no_arrow(&lex)) {
          lexer__advance(&lex);
        }
        Token tok = lexer__make_token(&lex, TOKEN_VAR, start, sline, scol);
        tok.payload.text = lex.source + start + 1; /* skip '$' */
        lexer__arr_push(&arr, tok);
        continue;
      }

      /* $ followed by invalid char or EOF → ERROR */
      {
        Token tok = lexer__make_token(&lex, TOKEN_ERROR, start, sline, scol);
        if (next == '\0') {
          tok.payload.error_msg = "unexpected end of input after $";
        } else {
          tok.payload.error_msg = "invalid character after $";
        }
        lexer__arr_push(&arr, tok);
        error_count++;
        continue;
      }
    }

    /* Strings: "..." or """...""" with escape sequences and interpolation */
    if (c == '"') {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      if (lex.source[lex.pos + 1] == '"' && lex.source[lex.pos + 2] == '"') {
        /* Triple-quoted string */
        lexer__advance(&lex); /* consume first '"' */
        lexer__advance(&lex); /* consume second '"' */
        lexer__advance(&lex); /* consume third '"' */
        lexer__lex_triple_string_body(&lex, &arr, &error_count, start, sline, scol);
      } else {
        lexer__advance(&lex); /* consume opening '"' */
        lexer__lex_string_body(&lex, &arr, &error_count, start, sline, scol);
      }
      continue;
    }

    /* Unrecognized character — emit ERROR and advance */
    {
      uint32_t start = lex.pos;
      uint32_t sline = lex.line;
      uint32_t scol  = lex.col;
      lexer__advance(&lex);
      Token tok = lexer__make_token(&lex, TOKEN_ERROR, start, sline, scol);
      tok.payload.error_msg = lexer__unexpected_char_msg(&lex, c);
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

#endif /* LEXER_C */
