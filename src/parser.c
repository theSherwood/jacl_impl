/*
 * JACL Parser
 *
 * Recursive descent parser producing arena-allocated AST from lexer token
 * stream, with panic mode error recovery.
 */

#ifndef PARSER_C
#define PARSER_C

#include <string.h>

/* -------------------------------------------------------------------------
 * Parse Result
 * ------------------------------------------------------------------------- */

typedef struct {
  AstNode** nodes;        /* arena-allocated array of top-level AST nodes */
  uint32_t  count;        /* number of top-level nodes */
  uint32_t  error_count;  /* number of parse errors */
} ParseResult;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * Parse a token stream into an AST.
 *
 * All AST nodes are allocated from the provided arena.
 * The returned node array always has count >= 0.
 */
ParseResult parser_parse(LexResult tokens, arena_t* arena);

/* -------------------------------------------------------------------------
 * Internal: Growable node array backed by arena
 * ------------------------------------------------------------------------- */

#define PARSER_INITIAL_CAP 64

typedef struct {
  AstNode** nodes;
  uint32_t  count;
  uint32_t  cap;
  arena_t*  arena;
} NodeArray;

static void parser__arr_init(NodeArray* arr, arena_t* arena) {
  arr->cap   = PARSER_INITIAL_CAP;
  arr->count = 0;
  arr->arena = arena;
  arr->nodes = (AstNode**)arena_alloc(arena, sizeof(AstNode*) * arr->cap);
}

static void parser__arr_push(NodeArray* arr, AstNode* node) {
  if (arr->count >= arr->cap) {
    uint32_t new_cap = arr->cap * 2;
    AstNode** new_nodes = (AstNode**)arena_alloc(arr->arena,
                                                  sizeof(AstNode*) * new_cap);
    memcpy(new_nodes, arr->nodes, sizeof(AstNode*) * arr->count);
    arr->nodes = new_nodes;
    arr->cap   = new_cap;
  }
  arr->nodes[arr->count++] = node;
}

/* -------------------------------------------------------------------------
 * Internal: Parser state
 * ------------------------------------------------------------------------- */

typedef struct {
  Token*   tokens;
  uint32_t count;
  uint32_t pos;
  arena_t* arena;
  uint32_t error_count;
} Parser;

static void parser__init(Parser* p, LexResult tokens, arena_t* arena) {
  p->tokens      = tokens.tokens;
  p->count       = tokens.count;
  p->pos         = 0;
  p->arena       = arena;
  p->error_count = 0;
}

static Token* parser__peek(Parser* p) {
  return &p->tokens[p->pos];
}

static Token* parser__advance(Parser* p) {
  Token* tok = &p->tokens[p->pos];
  if (tok->type != TOKEN_EOF) {
    p->pos++;
  }
  return tok;
}

static int parser__at_end(Parser* p) {
  return p->tokens[p->pos].type == TOKEN_EOF;
}

static SourcePos parser__token_start(Token* tok) {
  SourcePos pos;
  pos.line   = tok->line;
  pos.column = tok->column;
  pos.offset = tok->offset;
  return pos;
}

static SourcePos parser__token_end(Token* tok) {
  SourcePos pos;
  pos.line   = tok->line;
  pos.column = tok->column + tok->length;
  pos.offset = tok->offset + tok->length;
  return pos;
}

/* -------------------------------------------------------------------------
 * Internal: Error node creation
 * ------------------------------------------------------------------------- */

static AstNode* parser__error(Parser* p, const char* message, Token* tok) {
  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_ERROR;
  node->start = parser__token_start(tok);
  node->end   = parser__token_end(tok);
  node->data.error.message = message;
  p->error_count++;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Skip newlines
 * ------------------------------------------------------------------------- */

static void parser__skip_newlines(Parser* p) {
  while (parser__peek(p)->type == TOKEN_NEWLINE) {
    parser__advance(p);
  }
}

/* -------------------------------------------------------------------------
 * Internal: Panic mode — sync to matching bracket or newline
 *
 * Called after consuming an opening '[' when error recovery is needed.
 * Skips tokens until matching ']' (tracking bracket depth), newline,
 * or EOF. Respects brace depth to avoid consuming an enclosing '}'.
 * ------------------------------------------------------------------------- */

static void parser__sync_bracket(Parser* p) {
  int bracket_depth = 1;
  int brace_depth = 0;
  while (!parser__at_end(p)) {
    TokenType t = parser__peek(p)->type;
    if (t == TOKEN_LBRACKET) {
      bracket_depth++;
    } else if (t == TOKEN_RBRACKET) {
      bracket_depth--;
      if (bracket_depth == 0) {
        parser__advance(p); /* consume matching ] */
        return;
      }
    } else if (t == TOKEN_LBRACE) {
      brace_depth++;
    } else if (t == TOKEN_RBRACE) {
      if (brace_depth == 0) {
        return; /* don't consume — belongs to enclosing block */
      }
      brace_depth--;
    } else if (t == TOKEN_NEWLINE) {
      return; /* sync point for top-level recovery */
    }
    parser__advance(p);
  }
}

/* -------------------------------------------------------------------------
 * Internal: Parse a single atom (literal, variable reference, operator)
 *
 * Returns NULL if the current token is not an atom.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_atom(Parser* p) {
  Token* tok = parser__peek(p);

  switch (tok->type) {
    case TOKEN_INT: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_INT;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_int.value = tok->payload.int_val;
      return node;
    }
    case TOKEN_FLOAT: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_FLOAT;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_float.value = tok->payload.float_val;
      return node;
    }
    case TOKEN_WORD: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_STRING;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_string.value  = tok->payload.text;
      node->data.lit_string.length = tok->length;
      return node;
    }
    case TOKEN_STRING: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_STRING;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_string.value  = tok->payload.text;
      node->data.lit_string.length = (uint32_t)strlen(tok->payload.text);
      return node;
    }
    case TOKEN_OPERATOR: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_STRING;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_string.value  = tok->payload.text;
      node->data.lit_string.length = tok->length;
      return node;
    }
    case TOKEN_VAR: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_VAR_REF;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.var_ref.name   = tok->payload.text;
      node->data.var_ref.length = tok->length - 1; /* exclude '$' */
      return node;
    }
    default:
      return NULL;
  }
}

/* -------------------------------------------------------------------------
 * Internal: Forward declarations for mutual recursion
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_expr(Parser* p);
static AstNode* parser__parse_block(Parser* p);
static AstNode* parser__parse_interp_string(Parser* p);

/* -------------------------------------------------------------------------
 * Internal: Parse bracketed command [cmd arg1 arg2]
 *
 * Called when the current token is TOKEN_LBRACKET.
 * Returns AST_COMMAND on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_command(Parser* p) {
  Token* open = parser__advance(p); /* consume '[' */
  SourcePos cmd_start = parser__token_start(open);

  /* Empty brackets [] → empty command node (used for proc param lists) */
  if (parser__peek(p)->type == TOKEN_RBRACKET) {
    Token* close = parser__advance(p);
    AstNode* empty_head = ast_alloc(p->arena);
    empty_head->type = AST_LIT_STRING;
    empty_head->start = parser__token_start(open);
    empty_head->end   = parser__token_start(close);
    empty_head->data.lit_string.value  = "";
    empty_head->data.lit_string.length = 0;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = parser__token_start(open);
    node->end   = parser__token_end(close);
    node->data.command.head      = empty_head;
    node->data.command.args      = NULL;
    node->data.command.arg_count = 0;
    return node;
  }

  /* Parse head (command name) */
  AstNode* head = parser__parse_expr(p);
  if (head == NULL) {
    AstNode* err = parser__error(p, "expected command name after '['", open);
    err->end = parser__token_end(parser__peek(p));
    parser__sync_bracket(p);
    return err;
  }

  /* Parse arguments until ] or EOF */
  NodeArray args;
  parser__arr_init(&args, p->arena);

  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACKET) {
    /* On newline: peek ahead for ] to allow trailing newlines before ] */
    if (parser__peek(p)->type == TOKEN_NEWLINE) {
      uint32_t saved_pos = p->pos;
      while (parser__peek(p)->type == TOKEN_NEWLINE) {
        parser__advance(p);
      }
      if (parser__peek(p)->type == TOKEN_RBRACKET) {
        break; /* ] found after newlines */
      }
      /* No ] — unclosed bracket, restore to newline for recovery */
      p->pos = saved_pos;
      break;
    }
    AstNode* arg = parser__parse_expr(p);
    if (arg == NULL) {
      break;
    }
    parser__arr_push(&args, arg);
  }

  /* Expect closing bracket */
  if (parser__peek(p)->type != TOKEN_RBRACKET) {
    AstNode* err = parser__error(p, "expected ']' to close command", open);
    if (p->pos > 0) {
      err->end = parser__token_end(&p->tokens[p->pos - 1]);
    }
    parser__sync_bracket(p);
    return err;
  }
  Token* close = parser__advance(p); /* consume ']' */

  /* Build AST_COMMAND node */
  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_COMMAND;
  node->start = cmd_start;
  node->end   = parser__token_end(close);
  node->data.command.head      = head;
  node->data.command.args      = args.nodes;
  node->data.command.arg_count = args.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse a single expression
 *
 * Dispatches based on the current token:
 *   TOKEN_LBRACKET  → parse_command
 *   atom tokens     → parse_atom
 *   otherwise       → NULL
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_expr(Parser* p) {
  Token* tok = parser__peek(p);

  switch (tok->type) {
    case TOKEN_LBRACKET:
      return parser__parse_command(p);

    case TOKEN_LBRACE:
      return parser__parse_block(p);

    case TOKEN_STRING_BEGIN:
      return parser__parse_interp_string(p);

    case TOKEN_INT:
    case TOKEN_FLOAT:
    case TOKEN_WORD:
    case TOKEN_STRING:
    case TOKEN_OPERATOR:
    case TOKEN_VAR:
      return parser__parse_atom(p);

    default:
      return NULL;
  }
}

/* -------------------------------------------------------------------------
 * Internal: Check if current token ends a command
 * ------------------------------------------------------------------------- */

static int parser__is_command_end(Parser* p) {
  TokenType t = parser__peek(p)->type;
  return t == TOKEN_NEWLINE || t == TOKEN_SEMICOLON || t == TOKEN_EOF
      || t == TOKEN_RBRACE;
}

/* -------------------------------------------------------------------------
 * Internal: Parse a top-level bare command
 *
 * Reads the first expression as the command head, then collects subsequent
 * expressions as arguments until a command delimiter (newline, semicolon,
 * or EOF). If the head is already a bracketed command with no trailing
 * arguments, it is returned directly without wrapping.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_bare_command(Parser* p) {
  AstNode* head = parser__parse_expr(p);
  if (head == NULL) return NULL;

  /* Error from sub-expression (e.g. unclosed bracket): propagate immediately */
  if (head->type == AST_ERROR) return head;

  NodeArray args;
  parser__arr_init(&args, p->arena);

  while (!parser__is_command_end(p)) {
    AstNode* arg = parser__parse_expr(p);
    if (arg == NULL) break;
    parser__arr_push(&args, arg);
  }

  /* Single expression with no trailing args: return as-is.
     This distinguishes bare `hello` (AST_LIT_STRING) from `[hello]` (AST_COMMAND)
     so the compiler can treat bracketed zero-arg forms as calls. */
  if (args.count == 0) {
    return head;
  }

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_COMMAND;
  node->start = head->start;
  if (args.count > 0) {
    node->end = args.nodes[args.count - 1]->end;
  } else {
    node->end = head->end;
  }
  node->data.command.head      = head;
  node->data.command.args      = args.nodes;
  node->data.command.arg_count = args.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse code block { cmd1; cmd2; ... }
 *
 * Called when the current token is TOKEN_LBRACE.
 * Returns AST_BLOCK on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_block(Parser* p) {
  Token* open = parser__advance(p); /* consume '{' */
  SourcePos block_start = parser__token_start(open);

  NodeArray commands;
  parser__arr_init(&commands, p->arena);

  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
    /* Skip newlines and semicolons between commands */
    while (parser__peek(p)->type == TOKEN_NEWLINE ||
           parser__peek(p)->type == TOKEN_SEMICOLON) {
      parser__advance(p);
    }
    if (parser__at_end(p) || parser__peek(p)->type == TOKEN_RBRACE) break;

    AstNode* cmd = parser__parse_bare_command(p);
    if (cmd != NULL) {
      parser__arr_push(&commands, cmd);
    } else {
      /* Skip unrecognized token to avoid infinite loop */
      parser__advance(p);
    }
  }

  /* Expect closing brace */
  if (parser__peek(p)->type != TOKEN_RBRACE) {
    AstNode* err = parser__error(p, "expected '}' to close block", open);
    err->end = parser__token_end(parser__peek(p));
    return err;
  }
  Token* close = parser__advance(p); /* consume '}' */

  /* Build AST_BLOCK node */
  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_BLOCK;
  node->start = block_start;
  node->end   = parser__token_end(close);
  node->data.block.commands = commands.nodes;
  node->data.block.count    = commands.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse interpolated string "text $var text $[expr] text"
 *
 * Called when the current token is TOKEN_STRING_BEGIN.
 * Collects segments (string literals, var refs, commands) into
 * AST_INTERP_STRING. Empty text segments are skipped.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_interp_string(Parser* p) {
  Token* begin = parser__advance(p); /* consume TOKEN_STRING_BEGIN */
  SourcePos str_start = parser__token_start(begin);

  NodeArray segments;
  parser__arr_init(&segments, p->arena);

  /* Add initial text segment from STRING_BEGIN if non-empty */
  if (begin->payload.text[0] != '\0') {
    AstNode* seg = ast_alloc(p->arena);
    seg->type = AST_LIT_STRING;
    seg->start = parser__token_start(begin);
    seg->end = parser__token_end(begin);
    seg->data.lit_string.value = begin->payload.text;
    seg->data.lit_string.length = (uint32_t)strlen(begin->payload.text);
    parser__arr_push(&segments, seg);
  }

  SourcePos str_end = parser__token_end(begin);

  for (;;) {
    Token* tok = parser__peek(p);

    if (tok->type == TOKEN_INTERP_VAR) {
      parser__advance(p);
      AstNode* var = ast_alloc(p->arena);
      var->type = AST_VAR_REF;
      var->start = parser__token_start(tok);
      var->end = parser__token_end(tok);
      var->data.var_ref.name = tok->payload.text;
      var->data.var_ref.length = tok->length - 1; /* exclude $ */
      parser__arr_push(&segments, var);
    }
    else if (tok->type == TOKEN_INTERP_EXPR_START) {
      SourcePos cmd_start = parser__token_start(tok);
      parser__advance(p); /* consume INTERP_EXPR_START */

      /* Parse head expression */
      AstNode* head = parser__parse_expr(p);
      if (head == NULL) {
        AstNode* err = parser__error(p, "expected expression after $[", tok);
        parser__arr_push(&segments, err);
        /* Skip to INTERP_EXPR_END */
        while (!parser__at_end(p) &&
               parser__peek(p)->type != TOKEN_INTERP_EXPR_END) {
          parser__advance(p);
        }
        if (parser__peek(p)->type == TOKEN_INTERP_EXPR_END) {
          parser__advance(p);
        }
        continue;
      }

      /* Parse args until INTERP_EXPR_END */
      NodeArray args;
      parser__arr_init(&args, p->arena);
      while (!parser__at_end(p) &&
             parser__peek(p)->type != TOKEN_INTERP_EXPR_END) {
        if (parser__peek(p)->type == TOKEN_NEWLINE) {
          parser__advance(p);
          continue;
        }
        AstNode* arg = parser__parse_expr(p);
        if (arg == NULL) break;
        parser__arr_push(&args, arg);
      }

      SourcePos cmd_end;
      if (parser__peek(p)->type == TOKEN_INTERP_EXPR_END) {
        Token* end_tok = parser__advance(p);
        cmd_end = parser__token_end(end_tok);
      } else {
        cmd_end = parser__token_end(parser__peek(p));
      }

      AstNode* cmd = ast_alloc(p->arena);
      cmd->type = AST_COMMAND;
      cmd->start = cmd_start;
      cmd->end = cmd_end;
      cmd->data.command.head = head;
      cmd->data.command.args = args.nodes;
      cmd->data.command.arg_count = args.count;
      parser__arr_push(&segments, cmd);
    }
    else if (tok->type == TOKEN_STRING_PART) {
      parser__advance(p);
      if (tok->payload.text[0] != '\0') {
        AstNode* seg = ast_alloc(p->arena);
        seg->type = AST_LIT_STRING;
        seg->start = parser__token_start(tok);
        seg->end = parser__token_end(tok);
        seg->data.lit_string.value = tok->payload.text;
        seg->data.lit_string.length = (uint32_t)strlen(tok->payload.text);
        parser__arr_push(&segments, seg);
      }
    }
    else if (tok->type == TOKEN_STRING_END) {
      parser__advance(p);
      if (tok->payload.text[0] != '\0') {
        AstNode* seg = ast_alloc(p->arena);
        seg->type = AST_LIT_STRING;
        seg->start = parser__token_start(tok);
        seg->end = parser__token_end(tok);
        seg->data.lit_string.value = tok->payload.text;
        seg->data.lit_string.length = (uint32_t)strlen(tok->payload.text);
        parser__arr_push(&segments, seg);
      }
      str_end = parser__token_end(tok);
      break;
    }
    else {
      /* EOF or unexpected token */
      str_end = parser__token_end(tok);
      break;
    }
  }

  AstNode* node = ast_alloc(p->arena);
  node->type = AST_INTERP_STRING;
  node->start = str_start;
  node->end = str_end;
  node->data.interp_string.segments = segments.nodes;
  node->data.interp_string.count = segments.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

ParseResult parser_parse(LexResult tokens, arena_t* arena) {
  Parser p;
  parser__init(&p, tokens, arena);

  NodeArray top_level;
  parser__arr_init(&top_level, arena);

  while (!parser__at_end(&p)) {
    /* Skip newlines and semicolons between commands */
    while (parser__peek(&p)->type == TOKEN_NEWLINE ||
           parser__peek(&p)->type == TOKEN_SEMICOLON) {
      parser__advance(&p);
    }
    if (parser__at_end(&p)) break;

    AstNode* cmd = parser__parse_bare_command(&p);
    if (cmd != NULL) {
      parser__arr_push(&top_level, cmd);
    } else {
      /* Unexpected token at top level — create error node and skip */
      Token* bad = parser__peek(&p);
      AstNode* err = parser__error(&p, "unexpected token", bad);
      parser__advance(&p);
      parser__arr_push(&top_level, err);
    }
  }

  ParseResult result;
  result.nodes       = top_level.nodes;
  result.count       = top_level.count;
  result.error_count = p.error_count;
  return result;
}

#endif /* PARSER_C */
