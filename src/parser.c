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

void parser__arr_init(NodeArray* arr, arena_t* arena) {
  arr->cap   = PARSER_INITIAL_CAP;
  arr->count = 0;
  arr->arena = arena;
  arr->nodes = (AstNode**)arena_alloc(arena, sizeof(AstNode*) * arr->cap);
}

void parser__arr_push(NodeArray* arr, AstNode* node) {
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
  uint32_t syntax_quote_depth;  /* nesting depth for syntax-quote */
} Parser;

void parser__init(Parser* p, LexResult tokens, arena_t* arena) {
  p->tokens      = tokens.tokens;
  p->count       = tokens.count;
  p->pos         = 0;
  p->arena       = arena;
  p->error_count = 0;
  p->syntax_quote_depth = 0;
}

Token* parser__peek(Parser* p) {
  return &p->tokens[p->pos];
}

Token* parser__advance(Parser* p) {
  Token* tok = &p->tokens[p->pos];
  if (tok->type != TOKEN_EOF) {
    p->pos++;
  }
  return tok;
}

int parser__at_end(Parser* p) {
  return p->tokens[p->pos].type == TOKEN_EOF;
}

SourcePos parser__token_start(Token* tok) {
  SourcePos pos;
  pos.line   = tok->line;
  pos.column = tok->column;
  pos.offset = tok->offset;
  return pos;
}

SourcePos parser__token_end(Token* tok) {
  SourcePos pos;
  pos.line   = tok->line;
  pos.column = tok->column + tok->length;
  pos.offset = tok->offset + tok->length;
  return pos;
}

/* -------------------------------------------------------------------------
 * Internal: Error node creation
 * ------------------------------------------------------------------------- */

AstNode* parser__error(Parser* p, const char* message, Token* tok) {
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

void parser__skip_newlines(Parser* p) {
  while (parser__peek(p)->type == TOKEN_NEWLINE ||
         parser__peek(p)->type == TOKEN_PRAGMA) {
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

void parser__sync_bracket(Parser* p) {
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

AstNode* parser__parse_atom(Parser* p) {
  Token* tok = parser__peek(p);

  switch (tok->type) {
    case TOKEN_INT: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_INT;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_int.value = tok->payload.int_val;
      /* Default to TYPE_I32 — matches the compiler's literal default
       * and the typer's default. The typer will overwrite during
       * its walk for narrowed contexts (e.g., declared i64 binding).
       * Important for macro-expanded literals the typer never visits. */
      node->inferred_type = TYPE_I32;
      return node;
    }
    case TOKEN_FLOAT: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_FLOAT;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_float.value = tok->payload.float_val;
      node->inferred_type = TYPE_F32;
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
      node->inferred_type = TYPE_STR;
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
    case TOKEN_OPERATOR:
    /* Operator tokens — usable as command names */
    case TOKEN_PIPE:
    case TOKEN_BACKSLASH:
    case TOKEN_BANG:
    case TOKEN_DOTDOT:
    case TOKEN_AMP:
    case TOKEN_AND:
    case TOKEN_OR:
    case TOKEN_NOT:
    case TOKEN_TILDE_AT:
    case TOKEN_EQUALS:
    case TOKEN_COLON:
    case TOKEN_DOUBLE_COLON: {
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_LIT_STRING;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.lit_string.value  = tok->payload.text;
      node->data.lit_string.length = tok->length;
      return node;
    }
    /* Keyword tokens — usable as command names */
    case TOKEN_STRUCT:
    case TOKEN_PROC:
    case TOKEN_DEFMACRO:
    case TOKEN_SYNTAX_QUOTE:
    case TOKEN_IF:
    case TOKEN_ELIF:
    case TOKEN_ELSE:
    case TOKEN_WHILE:
    case TOKEN_FOR:
    case TOKEN_DEF:
    case TOKEN_MUT:
    case TOKEN_SET:
    case TOKEN_MATCH:
    case TOKEN_RETURN:
    case TOKEN_BREAK:
    case TOKEN_CONTINUE:
    case TOKEN_TRY:
    case TOKEN_CTX:
    case TOKEN_EXTERN: {
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
    case TOKEN_CARET_WORD: {
      /* ^identifier — only valid inside syntax-quote. Produces a
         var-ref AST node flagged as caret so hygiene stamping skips
         it and the identifier resolves in the caller's scope (mark 0)
         rather than the macro's freshly-minted scope mark. */
      if (p->syntax_quote_depth == 0) {
        parser__advance(p);
        return parser__error(p, "'^' identifier can only be used inside syntax-quote", tok);
      }
      parser__advance(p);
      AstNode* node = ast_alloc(p->arena);
      node->type = AST_VAR_REF;
      node->start = parser__token_start(tok);
      node->end   = parser__token_end(tok);
      node->data.var_ref.name   = tok->payload.text;
      node->data.var_ref.length = tok->length - 1; /* exclude '^' */
      node->is_caret = 1;
      return node;
    }
    default:
      return NULL;
  }
}

/* -------------------------------------------------------------------------
 * Internal: Forward declarations for mutual recursion
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_expr(Parser* p);
AstNode* parser__parse_block(Parser* p);
AstNode* parser__parse_interp_string(Parser* p);
AstNode* parser__parse_infix(Parser* p);

/* -------------------------------------------------------------------------
 * Internal: Parse bracketed command [cmd arg1 arg2]
 *
 * Called when the current token is TOKEN_LBRACKET.
 * Returns AST_COMMAND on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_command(Parser* p) {
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
    node->data.command.head_id   = HEAD_NONE;
    node->data.command.args      = NULL;
    node->data.command.arg_count = 0;
    return node;
  }

  /* Parse head (command name) */
  TokenType head_token_type = parser__peek(p)->type;
  AstNode* head = parser__parse_expr(p);
  if (head == NULL) {
    AstNode* err = parser__error(p, "expected command name after '['", open);
    err->end = parser__token_end(parser__peek(p));
    parser__sync_bracket(p);
    return err;
  }

  /* Reject old-syntax forms inside brackets */
  if (head_token_type == TOKEN_PROC) {
    AstNode* err = parser__error(p, "proc must use command syntax: proc name {params} {body}", open);
    parser__sync_bracket(p);
    return err;
  }
  if (head_token_type == TOKEN_EXTERN) {
    AstNode* err = parser__error(p, "extern must be used at top level: extern [type] name {params}", open);
    parser__sync_bracket(p);
    return err;
  }
  if (head_token_type == TOKEN_OPERATOR &&
      head->type == AST_LIT_STRING &&
      head->data.lit_string.length == 1 &&
      head->data.lit_string.value[0] == '.') {
    AstNode* err = parser__error(p, "use $struct->field instead of [. $struct field]", open);
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
    /* Spread expression: ..expr */
    if (parser__peek(p)->type == TOKEN_DOTDOT) {
      Token* dotdot = parser__advance(p); /* consume '..' */
      AstNode* inner = parser__parse_expr(p);
      if (inner == NULL) {
        AstNode* err = parser__error(p, "expected expression after '..' in spread", dotdot);
        parser__sync_bracket(p);
        return err;
      }
      AstNode* spread = ast_alloc(p->arena);
      spread->type  = AST_SPREAD;
      spread->start = parser__token_start(dotdot);
      spread->end   = inner->end;
      spread->data.spread.expr = inner;
      parser__arr_push(&args, spread);
      continue;
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
  node->data.command.head_id   = ast__compute_head_id(head);
  node->data.command.args      = args.nodes;
  node->data.command.arg_count = args.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Sync to matching ')' for error recovery in infix mode
 * ------------------------------------------------------------------------- */

void parser__sync_paren(Parser* p) {
  int depth = 1;
  while (!parser__at_end(p)) {
    TokenType t = parser__peek(p)->type;
    if (t == TOKEN_LPAREN) {
      depth++;
    } else if (t == TOKEN_RPAREN) {
      depth--;
      if (depth == 0) {
        parser__advance(p);
        return;
      }
    } else if (t == TOKEN_NEWLINE) {
      return;
    }
    parser__advance(p);
  }
}

/* -------------------------------------------------------------------------
 * Internal: Check if a token is a symbolic operator
 *
 * Uniform operator set used in both () and {} modes:
 *   TOKEN_OPERATOR: +, -, *, /, %, ==, !=, <, >, <=, >=
 *   TOKEN_AND: &&   TOKEN_OR: ||
 *   TOKEN_PIPE: |   TOKEN_EQUALS: =
 *   TOKEN_COLON: :  TOKEN_DOUBLE_COLON: ::
 * ------------------------------------------------------------------------- */

int parser__is_operator(Token* tok) {
  return tok->type == TOKEN_OPERATOR
      || tok->type == TOKEN_AND
      || tok->type == TOKEN_OR
      || tok->type == TOKEN_PIPE
      || tok->type == TOKEN_EQUALS
      || tok->type == TOKEN_COLON
      || tok->type == TOKEN_DOUBLE_COLON
;
}

/* -------------------------------------------------------------------------
 * Internal: Postfix arrow field access ($expr->field)
 *
 * If the next token is TOKEN_ARROW, wraps expr in [. expr field].
 * Loops for chained access: $a->b->c becomes [. [. $a b] c].
 * Returns the original expr unchanged if no arrow follows.
 * ------------------------------------------------------------------------- */

AstNode* parser__maybe_arrow_access(Parser* p, AstNode* expr) {
  while (!parser__at_end(p) &&
         parser__peek(p)->type == TOKEN_ARROW) {
    Token* arrow = parser__advance(p); /* consume '->' */

    Token* field_tok = parser__peek(p);
    if (field_tok->type != TOKEN_WORD) {
      return parser__error(p, "expected field name after '->'", arrow);
    }
    parser__advance(p); /* consume field name */

    /* Build field name as AST_LIT_STRING */
    AstNode* field = ast_alloc(p->arena);
    field->type = AST_LIT_STRING;
    field->start = parser__token_start(field_tok);
    field->end   = parser__token_end(field_tok);
    field->data.lit_string.value  = field_tok->payload.text;
    field->data.lit_string.length = field_tok->length;

    /* Build "." head */
    AstNode* dot_head = ast_alloc(p->arena);
    dot_head->type = AST_LIT_STRING;
    dot_head->start = parser__token_start(arrow);
    dot_head->end   = parser__token_end(arrow);
    dot_head->data.lit_string.value  = ".";
    dot_head->data.lit_string.length = 1;

    /* Build [. expr field] command */
    AstNode** args = ast_alloc_array(p->arena, 2);
    args[0] = expr;
    args[1] = field;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = expr->start;
    node->end   = parser__token_end(field_tok);
    node->data.command.head      = dot_head;
    node->data.command.head_id   = HEAD_DOT;
    node->data.command.args      = args;
    node->data.command.arg_count = 2;

    expr = node;
  }
  return expr;
}

/* -------------------------------------------------------------------------
 * Internal: Parse an operand in infix mode
 *
 * Handles unary prefix operators (- for negation, ~ for logical not)
 * before dispatching to primary expression parsing.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_infix_operand(Parser* p) {
  Token* tok = parser__peek(p);

  /* Unary prefix: - (negation) */
  if (tok->type == TOKEN_OPERATOR &&
      tok->length == 1 && tok->payload.text[0] == '-') {
    Token* op = parser__advance(p);
    AstNode* operand = parser__parse_infix_operand(p);
    if (operand == NULL) {
      return parser__error(p, "expected operand after unary '-'", op);
    }

    AstNode* head = ast_alloc(p->arena);
    head->type = AST_LIT_STRING;
    head->start = parser__token_start(op);
    head->end   = parser__token_end(op);
    head->data.lit_string.value  = "-";
    head->data.lit_string.length = 1;

    AstNode** args = ast_alloc_array(p->arena, 1);
    args[0] = operand;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = parser__token_start(op);
    node->end   = operand->end;
    node->data.command.head      = head;
    node->data.command.head_id   = HEAD_MINUS;
    node->data.command.args      = args;
    node->data.command.arg_count = 1;
    return node;
  }

  /* Unary prefix: ~ (logical not) — or unquote inside syntax-quote */
  if (tok->type == TOKEN_NOT) {
    if (p->syntax_quote_depth == 1) {
      Token* op = parser__advance(p);
      p->syntax_quote_depth--;
      AstNode* child = parser__parse_infix_operand(p);
      p->syntax_quote_depth++;
      if (child == NULL) {
        return parser__error(p, "expected expression after '~'", op);
      }
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_UNQUOTE;
      node->start = parser__token_start(op);
      node->end   = child->end;
      node->data.unquote.child = child;
      return node;
    }
    /* depth 0 or > 1: normal logical not */
    Token* op = parser__advance(p);
    AstNode* operand = parser__parse_infix_operand(p);
    if (operand == NULL) {
      return parser__error(p, "expected operand after '~'", op);
    }

    AstNode* head = ast_alloc(p->arena);
    head->type = AST_LIT_STRING;
    head->start = parser__token_start(op);
    head->end   = parser__token_end(op);
    head->data.lit_string.value  = "~";
    head->data.lit_string.length = 1;

    AstNode** args = ast_alloc_array(p->arena, 1);
    args[0] = operand;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = parser__token_start(op);
    node->end   = operand->end;
    node->data.command.head      = head;
    node->data.command.head_id   = HEAD_TILDE;
    node->data.command.args      = args;
    node->data.command.arg_count = 1;
    return node;
  }

  /* ~@ (unquote-splicing) inside syntax-quote */
  if (tok->type == TOKEN_TILDE_AT) {
    if (p->syntax_quote_depth == 1) {
      Token* op = parser__advance(p);
      p->syntax_quote_depth--;
      AstNode* child = parser__parse_infix_operand(p);
      p->syntax_quote_depth++;
      if (child == NULL) {
        return parser__error(p, "expected expression after '~@'", op);
      }
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_UNQUOTE_SPLICING;
      node->start = parser__token_start(op);
      node->end   = child->end;
      node->data.unquote_splicing.child = child;
      return node;
    }
    if (p->syntax_quote_depth == 0) {
      parser__advance(p);
      return parser__error(p, "'~@' can only be used inside syntax-quote", tok);
    }
    /* depth > 1: treat as literal — fall through */
  }

  /* Primary expressions */
  AstNode* result = NULL;
  switch (tok->type) {
    case TOKEN_LPAREN:
      result = parser__parse_infix(p);
      break;
    case TOKEN_LBRACKET:
      result = parser__parse_command(p);
      break;
    case TOKEN_DOLLAR_BRACKET: {
      /* $[cmd args] inside infix / $() context */
      Token* db = parser__advance(p); /* consume $[ */
      AstNode* head = parser__parse_expr(p);
      if (head == NULL) {
        return parser__error(p, "expected expression after $[", db);
      }
      NodeArray args;
      parser__arr_init(&args, p->arena);
      while (!parser__at_end(p) &&
             parser__peek(p)->type != TOKEN_RBRACKET) {
        if (parser__peek(p)->type == TOKEN_NEWLINE) {
          parser__advance(p); continue;
        }
        AstNode* arg = parser__parse_expr(p);
        if (arg == NULL) break;
        parser__arr_push(&args, arg);
      }
      SourcePos end_pos;
      if (parser__peek(p)->type == TOKEN_RBRACKET) {
        Token* rb = parser__advance(p);
        end_pos = parser__token_end(rb);
      } else {
        end_pos = parser__token_end(parser__peek(p));
      }
      AstNode* cmd = ast_alloc(p->arena);
      cmd->type = AST_COMMAND;
      cmd->start = parser__token_start(db);
      cmd->end = end_pos;
      cmd->data.command.head = head;
      cmd->data.command.head_id = ast__compute_head_id(head);
      cmd->data.command.args = args.nodes;
      cmd->data.command.arg_count = args.count;
      result = cmd;
      break;
    }
    case TOKEN_LBRACE:
      result = parser__parse_block(p);
      break;
    case TOKEN_STRING_BEGIN:
      result = parser__parse_interp_string(p);
      break;
    case TOKEN_QUOTE: {
      Token* kw = parser__advance(p);
      AstNode* child = parser__parse_infix_operand(p);
      if (child == NULL) {
        return parser__error(p, "expected expression after 'quote'", kw);
      }
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_QUOTE;
      node->start = parser__token_start(kw);
      node->end   = child->end;
      node->data.quote.child = child;
      result = node;
      break;
    }
    case TOKEN_SYNTAX_QUOTE: {
      Token* kw = parser__advance(p);
      p->syntax_quote_depth++;
      AstNode* child = parser__parse_infix_operand(p);
      p->syntax_quote_depth--;
      if (child == NULL) {
        return parser__error(p, "expected expression after 'syntax-quote'", kw);
      }
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_SYNTAX_QUOTE;
      node->start = parser__token_start(kw);
      node->end   = child->end;
      node->data.syntax_quote.child = child;
      result = node;
      break;
    }
    case TOKEN_INT:
    case TOKEN_FLOAT:
    case TOKEN_WORD:
    case TOKEN_STRING:
    case TOKEN_VAR:
    case TOKEN_CARET_WORD:
    case TOKEN_STRUCT:
    case TOKEN_PROC:
    case TOKEN_DEFMACRO:
    case TOKEN_IF:
    case TOKEN_ELIF:
    case TOKEN_ELSE:
    case TOKEN_WHILE:
    case TOKEN_FOR:
    case TOKEN_DEF:
    case TOKEN_MUT:
    case TOKEN_SET:
    case TOKEN_MATCH:
    case TOKEN_RETURN:
    case TOKEN_BREAK:
    case TOKEN_CONTINUE:
    case TOKEN_TRY:
    case TOKEN_CTX:
    case TOKEN_EXTERN:
      result = parser__parse_atom(p);
      break;
    default:
      return NULL;
  }

  /* Postfix arrow field access: expr->field */
  if (result != NULL && result->type != AST_ERROR) {
    result = parser__maybe_arrow_access(p, result);
  }
  return result;
}

/* -------------------------------------------------------------------------
 * Internal: Parse infix mode expression: (operand op operand op ...)
 *
 * Called when the current token is TOKEN_LPAREN.
 * Parses left-to-right with no operator precedence.
 * Returns AST_COMMAND for binary/unary ops, or the bare operand for
 * simple grouping like ($x).
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_infix(Parser* p) {
  Token* open = parser__advance(p); /* consume '(' */

  /* Empty parens → error */
  if (parser__peek(p)->type == TOKEN_RPAREN) {
    parser__advance(p);
    return parser__error(p, "empty parentheses in infix expression", open);
  }

  /* Parse first operand */
  AstNode* left = parser__parse_infix_operand(p);
  if (left == NULL) {
    AstNode* err = parser__error(p,
        "expected expression after '('", open);
    parser__sync_paren(p);
    return err;
  }

  /* Loop: binary operator + right operand, left-to-right */
  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RPAREN) {
    Token* op_tok = parser__peek(p);
    if (!parser__is_operator(op_tok)) {
      break;
    }
    parser__advance(p); /* consume operator */

    /* Parse right operand */
    AstNode* right = parser__parse_infix_operand(p);
    if (right == NULL) {
      AstNode* err = parser__error(p,
          "expected operand after operator", op_tok);
      parser__sync_paren(p);
      return err;
    }

    /* Build AST_COMMAND: [op left right] */
    AstNode* head = ast_alloc(p->arena);
    head->type = AST_LIT_STRING;
    head->start = parser__token_start(op_tok);
    head->end   = parser__token_end(op_tok);
    head->data.lit_string.value  = op_tok->payload.text;
    head->data.lit_string.length = op_tok->length;

    AstNode** args = ast_alloc_array(p->arena, 2);
    args[0] = left;
    args[1] = right;

    AstNode* cmd = ast_alloc(p->arena);
    cmd->type  = AST_COMMAND;
    cmd->start = left->start;
    cmd->end   = right->end;
    cmd->data.command.head      = head;
    cmd->data.command.head_id   = ast__compute_head_id(head);
    cmd->data.command.args      = args;
    cmd->data.command.arg_count = 2;

    left = cmd;
  }

  /* Expect closing paren */
  if (parser__peek(p)->type != TOKEN_RPAREN) {
    AstNode* err = parser__error(p,
        "expected ')' to close infix expression", open);
    parser__sync_paren(p);
    return err;
  }
  parser__advance(p); /* consume ')' */

  return left;
}

/* -------------------------------------------------------------------------
 * Internal: Parse a single expression
 *
 * Dispatches based on the current token:
 *   TOKEN_LBRACKET  → parse_command
 *   TOKEN_LPAREN    → parse_infix
 *   atom tokens     → parse_atom
 *   otherwise       → NULL
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_expr(Parser* p) {
  Token* tok = parser__peek(p);
  AstNode* result = NULL;

  switch (tok->type) {
    case TOKEN_LBRACKET:
      result = parser__parse_command(p);
      break;

    case TOKEN_LPAREN:
      result = parser__parse_infix(p);
      break;

    case TOKEN_LBRACE:
      result = parser__parse_block(p);
      break;

    case TOKEN_STRING_BEGIN:
      result = parser__parse_interp_string(p);
      break;

    case TOKEN_QUOTE: {
      Token* kw = parser__advance(p);
      AstNode* child = parser__parse_expr(p);
      if (child == NULL) {
        return parser__error(p, "expected expression after 'quote'", kw);
      }
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_QUOTE;
      node->start = parser__token_start(kw);
      node->end   = child->end;
      node->data.quote.child = child;
      result = node;
      break;
    }

    case TOKEN_SYNTAX_QUOTE: {
      Token* kw = parser__advance(p);
      p->syntax_quote_depth++;
      AstNode* child = parser__parse_expr(p);
      p->syntax_quote_depth--;
      if (child == NULL) {
        return parser__error(p, "expected expression after 'syntax-quote'", kw);
      }
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_SYNTAX_QUOTE;
      node->start = parser__token_start(kw);
      node->end   = child->end;
      node->data.syntax_quote.child = child;
      result = node;
      break;
    }

    case TOKEN_NOT: {
      if (p->syntax_quote_depth == 1) {
        Token* op = parser__advance(p);
        p->syntax_quote_depth--;
        AstNode* child = parser__parse_expr(p);
        p->syntax_quote_depth++;
        if (child == NULL) {
          return parser__error(p, "expected expression after '~'", op);
        }
        AstNode* node = ast_alloc(p->arena);
        node->type  = AST_UNQUOTE;
        node->start = parser__token_start(op);
        node->end   = child->end;
        node->data.unquote.child = child;
        result = node;
        break;
      }
      /* depth 0 or > 1: fall through to atom (operator literal) */
      result = parser__parse_atom(p);
      break;
    }

    case TOKEN_TILDE_AT: {
      if (p->syntax_quote_depth == 1) {
        Token* op = parser__advance(p);
        p->syntax_quote_depth--;
        AstNode* child = parser__parse_expr(p);
        p->syntax_quote_depth++;
        if (child == NULL) {
          return parser__error(p, "expected expression after '~@'", op);
        }
        AstNode* node = ast_alloc(p->arena);
        node->type  = AST_UNQUOTE_SPLICING;
        node->start = parser__token_start(op);
        node->end   = child->end;
        node->data.unquote_splicing.child = child;
        result = node;
        break;
      }
      if (p->syntax_quote_depth == 0) {
        parser__advance(p);
        return parser__error(p, "'~@' can only be used inside syntax-quote", tok);
      }
      /* depth > 1: treat as literal */
      result = parser__parse_atom(p);
      break;
    }

    case TOKEN_INT:
    case TOKEN_FLOAT:
    case TOKEN_WORD:
    case TOKEN_STRING:
    case TOKEN_OPERATOR:
    case TOKEN_VAR:
    case TOKEN_CARET_WORD:
    /* New operator tokens */
    case TOKEN_PIPE:
    case TOKEN_BACKSLASH:
    case TOKEN_BANG:
    case TOKEN_DOTDOT:
    case TOKEN_AMP:
    case TOKEN_AND:
    case TOKEN_OR:
    case TOKEN_EQUALS:
    case TOKEN_COLON:
    case TOKEN_DOUBLE_COLON:
    /* New keyword tokens */
    case TOKEN_STRUCT:
    case TOKEN_PROC:
    case TOKEN_DEFMACRO:
    case TOKEN_IF:
    case TOKEN_ELIF:
    case TOKEN_ELSE:
    case TOKEN_WHILE:
    case TOKEN_FOR:
    case TOKEN_DEF:
    case TOKEN_MUT:
    case TOKEN_SET:
    case TOKEN_MATCH:
    case TOKEN_RETURN:
    case TOKEN_BREAK:
    case TOKEN_CONTINUE:
    case TOKEN_TRY:
    case TOKEN_CTX:
    case TOKEN_EXTERN:
      result = parser__parse_atom(p);
      break;

    default:
      return NULL;
  }

  /* Postfix arrow field access: expr->field */
  if (result != NULL && result->type != AST_ERROR) {
    result = parser__maybe_arrow_access(p, result);
  }
  return result;
}

/* -------------------------------------------------------------------------
 * Internal: Check if current token ends a command
 * ------------------------------------------------------------------------- */

int parser__is_command_end(Parser* p) {
  TokenType t = parser__peek(p)->type;
  return t == TOKEN_NEWLINE || t == TOKEN_SEMICOLON || t == TOKEN_COMMA
      || t == TOKEN_EOF || t == TOKEN_RBRACE;
}

/* Check if current token ends a command operand (command-end OR operator) */
int parser__is_operand_end(Parser* p) {
  if (parser__is_command_end(p)) return 1;
  return parser__is_operator(parser__peek(p));
}

/* Check if current token ends a shell command operand.
 * Shell commands stop at pipe/and/or/amp but NOT at general operators like - */
int parser__is_shell_cmd_end(Parser* p) {
  if (parser__is_command_end(p)) return 1;
  TokenType t = parser__peek(p)->type;
  return t == TOKEN_PIPE || t == TOKEN_AND || t == TOKEN_OR || t == TOKEN_AMP;
}

/* -------------------------------------------------------------------------
 * Internal: Parse use declaration
 *
 * Two forms:
 *   use "./path.jacl" name        — module binding (bind module to name)
 *   use "./path.jacl" {a, b, c}   — destructuring (import specific names)
 *
 * Called when the current token is TOKEN_USE.
 * Returns AST_USE on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_use(Parser* p) {
  Token* use_tok = parser__advance(p); /* consume 'use' */
  SourcePos start = parser__token_start(use_tok);

  /* Expect string literal for path */
  Token* path_tok = parser__peek(p);
  if (path_tok->type != TOKEN_STRING) {
    return parser__error(p, "'use' path must be a string literal", path_tok);
  }
  parser__advance(p);
  const char* path = path_tok->payload.text;
  uint32_t path_len = (uint32_t)strlen(path);

  /* Check what follows the path */
  Token* next_tok = parser__peek(p);

  if (next_tok->type == TOKEN_WORD) {
    /* Module binding form: use "path" name */
    parser__advance(p);
    const char* binding_name = next_tok->payload.text;
    uint32_t binding_name_len = next_tok->length;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_USE;
    node->start = start;
    node->end   = parser__token_end(next_tok);
    node->data.use_decl.path             = path;
    node->data.use_decl.path_len         = path_len;
    node->data.use_decl.binding_name     = binding_name;
    node->data.use_decl.binding_name_len = binding_name_len;
    node->data.use_decl.names            = NULL;
    node->data.use_decl.name_lens        = NULL;
    node->data.use_decl.name_count       = 0;
    node->data.use_decl.is_module_binding = 1;
    return node;
  }

  if (next_tok->type == TOKEN_LBRACE) {
    /* Destructuring form: use "path" {name1, name2, ...} */
    Token* brace_tok = parser__advance(p); /* consume '{' */

    /* Check for empty name list */
    if (parser__peek(p)->type == TOKEN_RBRACE) {
      Token* close = parser__advance(p);
      AstNode* err = parser__error(p, "use destructuring must not be empty", close);
      err->start = start;
      return err;
    }

    /* Parse names until '}' */
    NodeArray names;
    parser__arr_init(&names, p->arena);

    while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
      Token* name_tok = parser__peek(p);
      if (name_tok->type != TOKEN_WORD) {
        return parser__error(p, "expected name in use destructuring", name_tok);
      }
      parser__advance(p);
      parser__arr_push(&names, (AstNode*)(void*)name_tok); /* temp: store Token* */

      /* Optional comma between names */
      if (parser__peek(p)->type == TOKEN_COMMA) {
        parser__advance(p);
      }
    }

    /* Expect closing '}' */
    if (parser__peek(p)->type != TOKEN_RBRACE) {
      return parser__error(p, "expected '}' to close use destructuring", brace_tok);
    }
    Token* close = parser__advance(p);

    /* Build name arrays */
    const char** name_strs = (const char**)arena_alloc(
        p->arena, sizeof(const char*) * names.count);
    uint32_t* name_lens = (uint32_t*)arena_alloc(
        p->arena, sizeof(uint32_t) * names.count);
    for (uint32_t i = 0; i < names.count; i++) {
      Token* t = (Token*)(void*)names.nodes[i];
      name_strs[i] = t->payload.text;
      name_lens[i] = t->length;
    }

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_USE;
    node->start = start;
    node->end   = parser__token_end(close);
    node->data.use_decl.path             = path;
    node->data.use_decl.path_len         = path_len;
    node->data.use_decl.binding_name     = NULL;
    node->data.use_decl.binding_name_len = 0;
    node->data.use_decl.names            = name_strs;
    node->data.use_decl.name_lens        = name_lens;
    node->data.use_decl.name_count       = names.count;
    node->data.use_decl.is_module_binding = 0;
    return node;
  }

  /* Neither word nor brace — error */
  return parser__error(p, "expected binding name or {names} after use path", next_tok);
}

/* -------------------------------------------------------------------------
 * Internal: Parse struct declaration: struct Name {type field, ...}
 *
 * Called when the current token is TOKEN_STRUCT.
 * Returns AST_DEFSTRUCT on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Internal: Parse inline struct type: struct{name:type,...}
 *
 * Called after ':' has been consumed and the current token is TOKEN_WORD
 * "struct" followed by TOKEN_LBRACE. Builds a canonical string like
 * "struct{x:i32,y:i32}" in the arena. Returns NULL on error.
 * ------------------------------------------------------------------------- */

const char* parser__parse_inline_struct_type(Parser* p, uint32_t* out_len) {
  /* Consume TOKEN_WORD("struct") */
  parser__advance(p);
  /* Consume TOKEN_LBRACE */
  if (parser__peek(p)->type != TOKEN_LBRACE) {
    parser__error(p, "expected '{' after 'struct' in inline type", parser__peek(p));
    return NULL;
  }
  parser__advance(p);

  /* Build canonical string in a stack buffer */
  char buf[512];
  uint32_t pos = 0;
  memcpy(buf + pos, "struct{", 7); pos += 7;

  int first = 1;
  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
    if (!first) {
      if (parser__peek(p)->type != TOKEN_COMMA) {
        parser__error(p, "expected ',' between inline struct fields", parser__peek(p));
        return NULL;
      }
      parser__advance(p); /* consume ',' */
      buf[pos++] = ',';
    }
    first = 0;

    /* Field name: TOKEN_WORD */
    Token* fname = parser__peek(p);
    if (fname->type != TOKEN_WORD) {
      parser__error(p, "expected field name in inline struct", fname);
      return NULL;
    }
    parser__advance(p);
    if (pos + fname->length + 1 >= sizeof(buf)) {
      parser__error(p, "inline struct type too long", fname);
      return NULL;
    }
    memcpy(buf + pos, fname->payload.text, fname->length);
    pos += fname->length;

    /* Colon: TOKEN_COLON or TOKEN_OPERATOR starting with ':' */
    Token* colon = parser__peek(p);
    int is_colon = (colon->type == TOKEN_COLON) ||
                   (colon->type == TOKEN_OPERATOR && colon->length >= 1 &&
                    colon->payload.text[0] == ':');
    if (!is_colon) {
      parser__error(p, "expected ':type' in inline struct field", colon);
      return NULL;
    }
    parser__advance(p);
    buf[pos++] = ':';

    if (colon->type == TOKEN_OPERATOR && colon->length > 1) {
      /* :i32 etc — type is rest of operator token */
      uint32_t tlen = colon->length - 1;
      const char* tstr = colon->payload.text + 1;
      if (pos + tlen >= sizeof(buf)) {
        parser__error(p, "inline struct type too long", colon);
        return NULL;
      }
      memcpy(buf + pos, tstr, tlen);
      pos += tlen;
    } else {
      /* Just ':' — check for nested struct or type name */
      Token* tname = parser__peek(p);
      if ((tname->type == TOKEN_STRUCT ||
           (tname->type == TOKEN_WORD && tname->length == 6 &&
            memcmp(tname->payload.text, "struct", 6) == 0)) &&
          p->pos + 1 < p->count &&
          p->tokens[p->pos + 1].type == TOKEN_LBRACE) {
        /* Recursive inline struct */
        uint32_t nested_len = 0;
        const char* nested = parser__parse_inline_struct_type(p, &nested_len);
        if (!nested) return NULL;
        if (pos + nested_len >= sizeof(buf)) {
          parser__error(p, "inline struct type too long", tname);
          return NULL;
        }
        memcpy(buf + pos, nested, nested_len);
        pos += nested_len;
      } else if (tname->type == TOKEN_WORD) {
        parser__advance(p);
        if (pos + tname->length >= sizeof(buf)) {
          parser__error(p, "inline struct type too long", tname);
          return NULL;
        }
        memcpy(buf + pos, tname->payload.text, tname->length);
        pos += tname->length;
      } else {
        parser__error(p, "expected type name in inline struct field", tname);
        return NULL;
      }
    }
  }

  /* Expect '}' */
  if (parser__at_end(p) || parser__peek(p)->type != TOKEN_RBRACE) {
    parser__error(p, "expected '}' to close inline struct type", parser__peek(p));
    return NULL;
  }
  parser__advance(p);
  buf[pos++] = '}';

  /* Copy to arena */
  char* result = (char*)arena_alloc(p->arena, pos + 1);
  memcpy(result, buf, pos);
  result[pos] = '\0';
  *out_len = pos;
  return result;
}

AstNode* parser__parse_defstruct(Parser* p) {
  Token* kw_tok = parser__advance(p); /* consume 'struct'/'defstruct' */
  SourcePos start = parser__token_start(kw_tok);

  /* Expect struct name (a word) */
  Token* name_tok = parser__peek(p);
  if (name_tok->type != TOKEN_WORD) {
    return parser__error(p, "expected struct name after 'struct'", name_tok);
  }
  parser__advance(p);
  const char* struct_name = name_tok->payload.text;
  uint32_t struct_name_len = name_tok->length;

  const char** field_names = NULL;
  uint32_t* field_name_lens = NULL;
  const char** field_types = NULL;
  uint32_t* field_type_lens = NULL;
  uint32_t field_count = 0;

  /* Temporary storage */
  #define DEFSTRUCT_MAX_FIELDS 64
  const char* tmp_fnames[DEFSTRUCT_MAX_FIELDS];
  uint32_t tmp_fname_lens[DEFSTRUCT_MAX_FIELDS];
  const char* tmp_ftypes[DEFSTRUCT_MAX_FIELDS];
  uint32_t tmp_ftype_lens[DEFSTRUCT_MAX_FIELDS];
  uint8_t tmp_fmutable[DEFSTRUCT_MAX_FIELDS];

  SourcePos last_end = parser__token_end(name_tok);

  /* ── New syntax: struct Name {type name, type name, ...} ── */
  if (!parser__at_end(p) && parser__peek(p)->type == TOKEN_LBRACE) {
    Token* open = parser__advance(p); /* consume '{' */

    while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
      /* Skip commas, newlines, and pragmas between fields */
      while (!parser__at_end(p) &&
             (parser__peek(p)->type == TOKEN_COMMA ||
              parser__peek(p)->type == TOKEN_NEWLINE ||
              parser__peek(p)->type == TOKEN_SEMICOLON ||
              parser__peek(p)->type == TOKEN_PRAGMA)) {
        parser__advance(p);
      }
      if (parser__at_end(p) || parser__peek(p)->type == TOKEN_RBRACE) break;

      /* Optional mut keyword before field type */
      uint8_t field_is_mutable = 0;
      if (!parser__at_end(p) && parser__peek(p)->type == TOKEN_MUT) {
        parser__advance(p);
        field_is_mutable = 1;
      }

      /* Field type — word or keyword token */
      Token* type_tok = parser__peek(p);
      const char* type_str = NULL;
      uint32_t type_len = 0;

      if (type_tok->type == TOKEN_WORD) {
        type_str = type_tok->payload.text;
        type_len = type_tok->length;
        parser__advance(p);
      } else if (type_tok->type == TOKEN_STRUCT &&
                 p->pos + 1 < p->count &&
                 p->tokens[p->pos + 1].type == TOKEN_LBRACE) {
        /* Inline struct type: struct{...} */
        uint32_t inline_len = 0;
        type_str = parser__parse_inline_struct_type(p, &inline_len);
        if (!type_str) {
          return parser__error(p, "invalid inline struct type", type_tok);
        }
        type_len = inline_len;
      } else {
        return parser__error(p, "expected field type", type_tok);
      }

      /* Field name — must be a word */
      Token* fname_tok = parser__peek(p);
      if (fname_tok->type != TOKEN_WORD) {
        return parser__error(p, "expected field name after type", fname_tok);
      }
      parser__advance(p);

      if (field_count >= DEFSTRUCT_MAX_FIELDS) {
        return parser__error(p, "too many fields in struct (max 64)", kw_tok);
      }

      /* Check for duplicate field names */
      for (uint32_t i = 0; i < field_count; i++) {
        if (tmp_fname_lens[i] == fname_tok->length &&
            memcmp(tmp_fnames[i], fname_tok->payload.text, fname_tok->length) == 0) {
          return parser__error(p, "duplicate field name in struct", fname_tok);
        }
      }

      tmp_fnames[field_count] = fname_tok->payload.text;
      tmp_fname_lens[field_count] = fname_tok->length;
      tmp_ftypes[field_count] = type_str;
      tmp_ftype_lens[field_count] = type_len;
      tmp_fmutable[field_count] = field_is_mutable;
      field_count++;
    }

    /* Expect closing brace */
    if (parser__at_end(p) || parser__peek(p)->type != TOKEN_RBRACE) {
      return parser__error(p, "expected '}' to close struct fields", open);
    }
    Token* close = parser__advance(p);
    last_end = parser__token_end(close);

    /* Validate: at least one field */
    if (field_count == 0) {
      return parser__error(p, "struct must have at least one field", name_tok);
    }
  }
  /* Old bracket syntax [field :type] removed — require braces */
  else {
    return parser__error(p, "expected '{' for struct fields (use struct Name {type field, ...})", name_tok);
  }

  /* Copy to arena-allocated arrays */
  field_names = (const char**)arena_alloc(p->arena, sizeof(const char*) * field_count);
  field_name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * field_count);
  field_types = (const char**)arena_alloc(p->arena, sizeof(const char*) * field_count);
  field_type_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * field_count);
  uint8_t* field_mutable = (uint8_t*)arena_alloc(p->arena, sizeof(uint8_t) * field_count);
  for (uint32_t i = 0; i < field_count; i++) {
    field_names[i] = tmp_fnames[i];
    field_name_lens[i] = tmp_fname_lens[i];
    field_types[i] = tmp_ftypes[i];
    field_type_lens[i] = tmp_ftype_lens[i];
    field_mutable[i] = tmp_fmutable[i];
  }

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_DEFSTRUCT;
  node->start = start;
  node->end   = last_end;
  node->data.defstruct.name = struct_name;
  node->data.defstruct.name_len = struct_name_len;
  node->data.defstruct.field_names = field_names;
  node->data.defstruct.field_name_lens = field_name_lens;
  node->data.defstruct.field_types = field_types;
  node->data.defstruct.field_type_lens = field_type_lens;
  node->data.defstruct.field_mutable = field_mutable;
  node->data.defstruct.field_count = field_count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Check if an AST node is a compile-time constant expression
 *
 * Compile-time constants: literal int/float/bool/string, or command with
 * only constant args (struct constructors).
 * ------------------------------------------------------------------------- */

static bool parser__is_constant_expr(AstNode* node) {
  if (!node) return false;
  switch (node->type) {
    case AST_LIT_INT:
    case AST_LIT_FLOAT:
    case AST_LIT_STRING:
      return true;
    case AST_COMMAND:
      /* Allow struct constructors: [Name lit1 lit2 ...] */
      for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
        if (!parser__is_constant_expr(node->data.command.args[i]))
          return false;
      }
      return true;
    default:
      return false;
  }
}

/* -------------------------------------------------------------------------
 * Internal: Parse ctx field declaration
 *
 * Syntax: ctx [mut] Type name = default_expr
 * Called when the current token is TOKEN_CTX.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_ctx_decl(Parser* p) {
  Token* kw_tok = parser__advance(p); /* consume 'ctx' */
  SourcePos start = parser__token_start(kw_tok);

  /* Optional 'mut' keyword */
  uint8_t is_mutable = 0;
  if (!parser__at_end(p) && parser__peek(p)->type == TOKEN_MUT) {
    parser__advance(p);
    is_mutable = 1;
  }

  /* Expect type name (a word) */
  Token* type_tok = parser__peek(p);
  if (type_tok->type != TOKEN_WORD) {
    return parser__error(p, "expected type name after 'ctx'", type_tok);
  }
  parser__advance(p);
  const char* type_name = type_tok->payload.text;
  uint32_t type_name_len = type_tok->length;

  /* Expect field name (a word) */
  Token* name_tok = parser__peek(p);
  if (name_tok->type != TOKEN_WORD) {
    return parser__error(p, "expected field name after type in ctx declaration", name_tok);
  }
  parser__advance(p);
  const char* field_name = name_tok->payload.text;
  uint32_t field_name_len = name_tok->length;

  /* Expect '=' */
  Token* eq_tok = parser__peek(p);
  if (eq_tok->type != TOKEN_EQUALS) {
    return parser__error(p, "ctx field must have a default value", eq_tok);
  }
  parser__advance(p); /* consume '=' */

  /* Parse default expression */
  AstNode* default_expr = parser__parse_expr(p);
  if (default_expr == NULL) {
    return parser__error(p, "expected expression after '=' in ctx declaration", eq_tok);
  }
  if (default_expr->type == AST_ERROR) return default_expr;

  /* Validate: default must be a compile-time constant */
  if (!parser__is_constant_expr(default_expr)) {
    return parser__error(p, "ctx field default must be a compile-time constant", eq_tok);
  }

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_CTX_DECL;
  node->start = start;
  node->end   = default_expr->end;
  node->data.ctx_decl.is_mutable     = is_mutable;
  node->data.ctx_decl.type_name      = type_name;
  node->data.ctx_decl.type_name_len  = type_name_len;
  node->data.ctx_decl.field_name     = field_name;
  node->data.ctx_decl.field_name_len = field_name_len;
  node->data.ctx_decl.default_expr   = default_expr;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse defmacro declaration
 *
 * Syntax: defmacro name {param1, param2, ...} { body }
 * Called when the current token is TOKEN_DEFMACRO.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_defmacro(Parser* p) {
  Token* kw_tok = parser__advance(p); /* consume 'defmacro' */
  SourcePos start = parser__token_start(kw_tok);

  /* Expect macro name (a word or operator token) */
  Token* name_tok = parser__peek(p);
  if (name_tok->type != TOKEN_WORD &&
      name_tok->type != TOKEN_OPERATOR &&
      name_tok->type != TOKEN_BACKSLASH &&
      name_tok->type != TOKEN_PIPE &&
      name_tok->type != TOKEN_BANG &&
      name_tok->type != TOKEN_AMP &&
      name_tok->type != TOKEN_DOTDOT &&
      name_tok->type != TOKEN_EQUALS &&
      name_tok->type != TOKEN_COLON &&
      name_tok->type != TOKEN_DOUBLE_COLON &&
      name_tok->type != TOKEN_AND &&
      name_tok->type != TOKEN_OR &&
      name_tok->type != TOKEN_NOT) {
    return parser__error(p, "expected macro name after 'defmacro'", name_tok);
  }
  parser__advance(p);
  const char* macro_name = name_tok->payload.text;
  uint32_t macro_name_len = name_tok->length;

  /* Expect parameter list in braces {param1, param2, ...} */
  if (parser__at_end(p) || parser__peek(p)->type != TOKEN_LBRACE) {
    return parser__error(p, "expected '{' for macro parameters", parser__peek(p));
  }
  Token* open_params = parser__advance(p); /* consume '{' */

  #define DEFMACRO_MAX_PARAMS 32
  const char* tmp_names[DEFMACRO_MAX_PARAMS];
  uint32_t tmp_name_lens[DEFMACRO_MAX_PARAMS];
  uint32_t param_count = 0;
  bool is_variadic = false;

  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
    /* Skip commas and newlines between params */
    while (!parser__at_end(p) &&
           (parser__peek(p)->type == TOKEN_COMMA ||
            parser__peek(p)->type == TOKEN_NEWLINE ||
            parser__peek(p)->type == TOKEN_SEMICOLON)) {
      parser__advance(p);
    }
    if (parser__at_end(p) || parser__peek(p)->type == TOKEN_RBRACE) break;

    /* Check for rest marker '..' */
    if (parser__peek(p)->type == TOKEN_DOTDOT) {
      parser__advance(p); /* consume '..' */
      if (is_variadic) {
        return parser__error(p, "multiple rest parameters not allowed in defmacro", kw_tok);
      }
      is_variadic = true;
      /* Next token must be the rest param name */
      Token* rest_tok = parser__peek(p);
      if (rest_tok->type != TOKEN_WORD) {
        return parser__error(p, "expected parameter name after '..' in defmacro", rest_tok);
      }
      parser__advance(p);
      if (param_count >= DEFMACRO_MAX_PARAMS) {
        return parser__error(p, "too many parameters in defmacro (max 32)", kw_tok);
      }
      tmp_names[param_count] = rest_tok->payload.text;
      tmp_name_lens[param_count] = rest_tok->length;
      param_count++;
      /* Rest param must be last — skip trailing commas/newlines and expect } */
      while (!parser__at_end(p) &&
             (parser__peek(p)->type == TOKEN_COMMA ||
              parser__peek(p)->type == TOKEN_NEWLINE ||
              parser__peek(p)->type == TOKEN_SEMICOLON)) {
        parser__advance(p);
      }
      if (parser__at_end(p) || parser__peek(p)->type != TOKEN_RBRACE) {
        return parser__error(p, "rest parameter must be last in defmacro", kw_tok);
      }
      break;
    }

    /* Param name — must be a word */
    Token* param_tok = parser__peek(p);
    if (param_tok->type != TOKEN_WORD) {
      return parser__error(p, "expected parameter name in macro parameter list", param_tok);
    }
    parser__advance(p);

    if (param_count >= DEFMACRO_MAX_PARAMS) {
      return parser__error(p, "too many parameters in defmacro (max 32)", kw_tok);
    }

    /* Check for duplicate parameter names */
    for (uint32_t i = 0; i < param_count; i++) {
      if (tmp_name_lens[i] == param_tok->length &&
          memcmp(tmp_names[i], param_tok->payload.text, param_tok->length) == 0) {
        return parser__error(p, "duplicate parameter name in defmacro", param_tok);
      }
    }

    tmp_names[param_count] = param_tok->payload.text;
    tmp_name_lens[param_count] = param_tok->length;
    param_count++;
  }

  /* Expect closing brace for params */
  if (parser__at_end(p) || parser__peek(p)->type != TOKEN_RBRACE) {
    return parser__error(p, "expected '}' to close macro parameters", open_params);
  }
  parser__advance(p); /* consume '}' */

  /* Skip optional whitespace/newlines between param list and body */
  while (!parser__at_end(p) && parser__peek(p)->type == TOKEN_NEWLINE) {
    parser__advance(p);
  }

  /* Expect body block { ... } */
  if (parser__at_end(p) || parser__peek(p)->type != TOKEN_LBRACE) {
    return parser__error(p, "expected '{' for macro body", parser__peek(p));
  }
  AstNode* body = parser__parse_block(p);
  if (body->type == AST_ERROR) return body;

  /* Copy param names to arena-allocated arrays */
  const char** param_names = NULL;
  uint32_t* param_name_lens = NULL;
  if (param_count > 0) {
    param_names = (const char**)arena_alloc(p->arena, sizeof(const char*) * param_count);
    param_name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * param_count);
    for (uint32_t i = 0; i < param_count; i++) {
      param_names[i] = tmp_names[i];
      param_name_lens[i] = tmp_name_lens[i];
    }
  }

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_DEFMACRO;
  node->start = start;
  node->end   = body->end;
  node->data.defmacro.name = macro_name;
  node->data.defmacro.name_len = macro_name_len;
  node->data.defmacro.param_names = param_names;
  node->data.defmacro.param_name_lens = param_name_lens;
  node->data.defmacro.param_count = param_count;
  node->data.defmacro.variadic = is_variadic;
  node->data.defmacro.body = body;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse proc parameter list in braces {type name, type name, ...}
 *
 * Called when the current token is TOKEN_LBRACE and we're parsing new-style
 * proc parameters. Returns AST_COMMAND (same shape as [param ...] lists)
 * so the compiler can process them identically.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_proc_params(Parser* p) {
  Token* open = parser__advance(p); /* consume '{' */
  SourcePos start = parser__token_start(open);

  NodeArray elems;
  parser__arr_init(&elems, p->arena);

  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
    /* Skip commas and newlines between parameters */
    while (parser__peek(p)->type == TOKEN_COMMA ||
           parser__peek(p)->type == TOKEN_NEWLINE ||
           parser__peek(p)->type == TOKEN_SEMICOLON) {
      parser__advance(p);
    }
    if (parser__at_end(p) || parser__peek(p)->type == TOKEN_RBRACE) break;

    /* Collect all words within one parameter slot (up to comma/brace).
       Each slot is "name", "type name", "& name", or "[Vec Type] name". */
    while (!parser__at_end(p) &&
           parser__peek(p)->type != TOKEN_COMMA &&
           parser__peek(p)->type != TOKEN_RBRACE &&
           parser__peek(p)->type != TOKEN_NEWLINE) {
      AstNode* elem;
      if (parser__peek(p)->type == TOKEN_LBRACKET) {
        /* Bracket expression for compound type: [Vec Point], [Map Point] */
        elem = parser__parse_expr(p);
      } else {
        elem = parser__parse_atom(p);
      }
      if (elem == NULL) {
        return parser__error(p, "expected parameter name or type", parser__peek(p));
      }
      parser__arr_push(&elems, elem);
    }
  }

  /* Expect closing brace */
  if (parser__peek(p)->type != TOKEN_RBRACE) {
    return parser__error(p, "expected '}' to close proc parameters", open);
  }
  Token* close = parser__advance(p); /* consume '}' */

  /* Build AST_COMMAND (same shape as [elem0 elem1 ...]) */
  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_COMMAND;
  node->start = start;
  node->end   = parser__token_end(close);

  if (elems.count == 0) {
    /* Empty params {} → equivalent to [] */
    AstNode* empty_head = ast_alloc(p->arena);
    empty_head->type = AST_LIT_STRING;
    empty_head->start = start;
    empty_head->end   = parser__token_end(close);
    empty_head->data.lit_string.value  = "";
    empty_head->data.lit_string.length = 0;

    node->data.command.head      = empty_head;
    node->data.command.head_id   = HEAD_NONE;
    node->data.command.args      = NULL;
    node->data.command.arg_count = 0;
  } else {
    /* head = first element, args = rest */
    node->data.command.head = elems.nodes[0];
    node->data.command.head_id = ast__compute_head_id(elems.nodes[0]);
    if (elems.count > 1) {
      AstNode** args_arr = ast_alloc_array(p->arena, elems.count - 1);
      for (uint32_t i = 1; i < elems.count; i++) {
        args_arr[i - 1] = elems.nodes[i];
      }
      node->data.command.args      = args_arr;
      node->data.command.arg_count = elems.count - 1;
    } else {
      node->data.command.args      = NULL;
      node->data.command.arg_count = 0;
    }
  }

  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse extern declaration
 *
 * Mirrors parser__parse_proc_form but without the body block. Handles:
 *   extern [return_type] name {params}
 *   extern              name {params}
 * Used for declaring host-provided native fns with typed signatures so
 * the typer can check call sites without per-site casts. The extern
 * statement itself produces a no-op at runtime; dispatch flows through
 * the existing native-fn registry by name.
 * ------------------------------------------------------------------------- */
AstNode* parser__parse_extern_form(Parser* p, AstNode* extern_head) {
  SourcePos start = extern_head->start;
  NodeArray args;
  parser__arr_init(&args, p->arena);

  /* Disambiguate: word { → name (no return type); word(/[) word { →
   * return-type then name. Compound type returns ([Ptr T], [Future T])
   * use TOKEN_LBRACKET as the leading token. */
  if (p->tokens[p->pos + 1].type == TOKEN_LBRACE) {
    /* extern name {params} */
    AstNode* name = parser__parse_atom(p);
    parser__arr_push(&args, name);
  } else {
    /* extern <type-expr> name {params}. type-expr is either a single
     * word (TOKEN_WORD/TOKEN_STRUCT) or a bracketed compound
     * ([Ptr T], etc.) parsed as an expression. */
    AstNode* ret_type;
    if (parser__peek(p)->type == TOKEN_LBRACKET) {
      ret_type = parser__parse_expr(p);
    } else {
      ret_type = parser__parse_atom(p);
    }
    if (ret_type == NULL || ret_type->type == AST_ERROR) {
      return parser__error(p, "expected return type after 'extern'", parser__peek(p));
    }
    parser__arr_push(&args, ret_type);
    AstNode* name = parser__parse_atom(p);
    if (name == NULL || name->type == AST_ERROR) {
      return parser__error(p, "expected name after extern return type",
                           parser__peek(p));
    }
    parser__arr_push(&args, name);
  }

  if (parser__peek(p)->type != TOKEN_LBRACE) {
    return parser__error(p, "expected '{' for extern parameters", parser__peek(p));
  }
  AstNode* params = parser__parse_proc_params(p);
  if (params->type == AST_ERROR) return params;
  parser__arr_push(&args, params);

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_COMMAND;
  node->start = start;
  node->end   = params->end;
  node->data.command.head      = extern_head;
  node->data.command.head_id   = HEAD_EXTERN;
  node->data.command.args      = args.nodes;
  node->data.command.arg_count = args.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse new-style proc form
 *
 * Called after TOKEN_PROC has been consumed as the head in bare command
 * context. Handles: proc [return_type] name {params} {body}
 * Produces the same AST_COMMAND shape the compiler expects.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_proc_form(Parser* p, AstNode* proc_head) {
  SourcePos start = proc_head->start;
  NodeArray args;
  parser__arr_init(&args, p->arena);

  /* Detect pattern:
     { ...           → anonymous proc (current token is already '{')
     word { ...      → proc name {params} {body}
     word word { ... → proc type name {params} {body}
     p->pos points to the first token after 'proc'. */
  if (p->tokens[p->pos].type == TOKEN_LBRACE) {
    /* Anonymous proc: proc {params} {body} — synthesize empty name */
    AstNode* name = ast_alloc(p->arena);
    name->type = AST_LIT_STRING;
    name->start = proc_head->start;
    name->end   = proc_head->end;
    name->data.lit_string.value  = "";
    name->data.lit_string.length = 0;
    parser__arr_push(&args, name);
  } else if (p->tokens[p->pos + 1].type == TOKEN_LBRACE) {
    /* proc name {params} {body} — no return type */
    AstNode* name = parser__parse_atom(p);
    parser__arr_push(&args, name);
  } else if ((p->tokens[p->pos + 1].type == TOKEN_WORD ||
              p->tokens[p->pos + 1].type == TOKEN_STRUCT) &&
             p->tokens[p->pos + 2].type == TOKEN_LBRACE) {
    /* proc type name {params} {body} — with return type */
    AstNode* ret_type = parser__parse_atom(p);
    parser__arr_push(&args, ret_type);
    AstNode* name = parser__parse_atom(p);
    parser__arr_push(&args, name);
  } else {
    return parser__error(p, "expected proc name followed by '{' parameter list",
                         parser__peek(p));
  }

  /* Parse {params} */
  if (parser__peek(p)->type != TOKEN_LBRACE) {
    return parser__error(p, "expected '{' for proc parameters", parser__peek(p));
  }
  AstNode* params = parser__parse_proc_params(p);
  if (params->type == AST_ERROR) return params;
  parser__arr_push(&args, params);

  /* Parse {body} */
  if (parser__peek(p)->type != TOKEN_LBRACE) {
    return parser__error(p, "expected '{' for proc body", parser__peek(p));
  }
  AstNode* body = parser__parse_block(p);
  if (body->type == AST_ERROR) return body;
  parser__arr_push(&args, body);

  /* Build AST_COMMAND: [proc name params body] or [proc type name params body] */
  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_COMMAND;
  node->start = start;
  node->end   = body->end;
  node->data.command.head      = proc_head;
  node->data.command.head_id   = HEAD_PROC;
  node->data.command.args      = args.nodes;
  node->data.command.arg_count = args.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse new-syntax if/elif/else form
 *
 * if condition { then_body }
 * if condition { then_body } else { else_body }
 * if condition { then_body } elif condition2 { body2 } else { body3 }
 *
 * elif desugars to nested if/else. Produces AST_COMMAND with head="if",
 * 2 args (no else) or 3 args (with else block).
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_if_form(Parser* p, AstNode* if_head) {
  SourcePos start = if_head->start;

  /* Parse condition expression */
  AstNode* cond = parser__parse_expr(p);
  if (cond == NULL) {
    return parser__error(p, "expected condition after 'if'", parser__peek(p));
  }
  if (cond->type == AST_ERROR) return cond;

  /* Expect { then_body } */
  if (parser__peek(p)->type != TOKEN_LBRACE) {
    return parser__error(p, "expected '{' after if condition", parser__peek(p));
  }
  AstNode* then_block = parser__parse_block(p);
  if (then_block->type == AST_ERROR) return then_block;

  /* Skip newlines to check for elif/else */
  while (parser__peek(p)->type == TOKEN_NEWLINE) {
    parser__advance(p);
  }

  if (parser__peek(p)->type == TOKEN_ELIF) {
    /* elif → desugar to nested if/else */
    Token* elif_tok = parser__advance(p); /* consume 'elif' */

    /* Create "if" literal node for the nested if */
    AstNode* nested_if_head = ast_alloc(p->arena);
    nested_if_head->type = AST_LIT_STRING;
    nested_if_head->start = parser__token_start(elif_tok);
    nested_if_head->end   = parser__token_end(elif_tok);
    nested_if_head->data.lit_string.value  = "if";
    nested_if_head->data.lit_string.length = 2;

    /* Recursively parse the elif as an if form */
    AstNode* nested_if = parser__parse_if_form(p, nested_if_head);
    if (nested_if->type == AST_ERROR) return nested_if;

    /* Wrap nested if in a block for the else branch */
    AstNode* else_block = ast_alloc(p->arena);
    else_block->type  = AST_BLOCK;
    else_block->start = nested_if->start;
    else_block->end   = nested_if->end;
    AstNode** block_cmds = ast_alloc_array(p->arena, 1);
    block_cmds[0] = nested_if;
    else_block->data.block.commands = block_cmds;
    else_block->data.block.count    = 1;

    /* Build 3-arg if: [if cond then_block else_block] */
    AstNode** args = ast_alloc_array(p->arena, 3);
    args[0] = cond;
    args[1] = then_block;
    args[2] = else_block;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = start;
    node->end   = else_block->end;
    node->data.command.head      = if_head;
    node->data.command.head_id   = HEAD_IF;
    node->data.command.args      = args;
    node->data.command.arg_count = 3;
    return node;

  } else if (parser__peek(p)->type == TOKEN_ELSE) {
    parser__advance(p); /* consume 'else' */

    /* Skip newlines after else */
    while (parser__peek(p)->type == TOKEN_NEWLINE) {
      parser__advance(p);
    }

    /* Expect { else_body } */
    if (parser__peek(p)->type != TOKEN_LBRACE) {
      return parser__error(p, "expected '{' after 'else'", parser__peek(p));
    }
    AstNode* else_block = parser__parse_block(p);
    if (else_block->type == AST_ERROR) return else_block;

    /* Build 3-arg if: [if cond then_block else_block] */
    AstNode** args = ast_alloc_array(p->arena, 3);
    args[0] = cond;
    args[1] = then_block;
    args[2] = else_block;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = start;
    node->end   = else_block->end;
    node->data.command.head      = if_head;
    node->data.command.head_id   = HEAD_IF;
    node->data.command.args      = args;
    node->data.command.arg_count = 3;
    return node;

  } else if (parser__peek(p)->type == TOKEN_LBRACE) {
    /* Trailing block without 'else' keyword — treat as else branch
       so that `if cond {} {}` behaves the same as `[if cond {} {}]`. */
    AstNode* else_block = parser__parse_block(p);
    if (else_block->type == AST_ERROR) return else_block;

    AstNode** args = ast_alloc_array(p->arena, 3);
    args[0] = cond;
    args[1] = then_block;
    args[2] = else_block;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = start;
    node->end   = else_block->end;
    node->data.command.head      = if_head;
    node->data.command.head_id   = HEAD_IF;
    node->data.command.args      = args;
    node->data.command.arg_count = 3;
    return node;

  } else {
    /* No else clause — 2-arg if */
    AstNode** args = ast_alloc_array(p->arena, 2);
    args[0] = cond;
    args[1] = then_block;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = start;
    node->end   = then_block->end;
    node->data.command.head      = if_head;
    node->data.command.head_id   = HEAD_IF;
    node->data.command.args      = args;
    node->data.command.arg_count = 2;
    return node;
  }
}

/* -------------------------------------------------------------------------
 * Internal: Parse new-syntax while form
 *
 * while condition { body }
 *
 * Produces AST_COMMAND with head="while", 2 args (condition + body block).
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_while_form(Parser* p, AstNode* while_head) {
  SourcePos start = while_head->start;

  /* Parse condition expression */
  AstNode* cond = parser__parse_expr(p);
  if (cond == NULL) {
    return parser__error(p, "expected condition after 'while'", parser__peek(p));
  }
  if (cond->type == AST_ERROR) return cond;

  /* Expect { body } */
  if (parser__peek(p)->type != TOKEN_LBRACE) {
    return parser__error(p, "expected '{' after while condition", parser__peek(p));
  }
  AstNode* body = parser__parse_block(p);
  if (body->type == AST_ERROR) return body;

  /* Build 2-arg while: [while cond body_block] */
  AstNode** args = ast_alloc_array(p->arena, 2);
  args[0] = cond;
  args[1] = body;

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_COMMAND;
  node->start = start;
  node->end   = body->end;
  node->data.command.head      = while_head;
  node->data.command.head_id   = HEAD_WHILE;
  node->data.command.args      = args;
  node->data.command.arg_count = 2;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Check if current position starts a destructuring binding
 *
 * Returns true if the current TOKEN_LBRACKET is followed by a matching
 * TOKEN_RBRACKET and then TOKEN_EQUALS or TOKEN_COLON (e.g. [a b c] = expr).
 * Does NOT advance the parser position.
 * ------------------------------------------------------------------------- */

int parser__lookahead_is_destructure_binding(Parser* p) {
  if (parser__peek(p)->type != TOKEN_LBRACKET) return 0;
  uint32_t saved = p->pos;
  int depth = 0;
  int result = 0;
  while (p->pos < p->count) {
    TokenType t = p->tokens[p->pos].type;
    if (t == TOKEN_LBRACKET) depth++;
    else if (t == TOKEN_RBRACKET) {
      depth--;
      if (depth == 0) {
        p->pos++;
        if (p->pos < p->count) {
          TokenType after = p->tokens[p->pos].type;
          result = (after == TOKEN_EQUALS || after == TOKEN_COLON);
        }
        break;
      }
    } else if (t == TOKEN_EOF) {
      break;
    }
    p->pos++;
  }
  p->pos = saved;
  return result;
}

/* -------------------------------------------------------------------------
 * Internal: Parse a destructuring vector pattern [a b c] or [i64 a, i64 b]
 *
 * Called when we know the current token is '[' and this is a destructuring
 * context. Supports optional type prefixes and comma separators.
 * Returns AST_DESTRUCTURE_VEC node.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_destructure_vec_pattern(Parser* p) {
  Token* open = parser__advance(p); /* consume '[' */
  SourcePos start = parser__token_start(open);

  /* Collect binding entries */
  uint32_t cap = 8;
  const char** names = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  const char** types = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* type_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  uint32_t count = 0;
  const char* rest_name = NULL;
  uint32_t rest_name_len = 0;

  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACKET) {
    /* Skip commas and newlines */
    if (parser__peek(p)->type == TOKEN_COMMA) {
      parser__advance(p);
      continue;
    }
    if (parser__peek(p)->type == TOKEN_NEWLINE) {
      parser__advance(p);
      continue;
    }

    /* Check for rest pattern: ..name */
    if (parser__peek(p)->type == TOKEN_DOTDOT) {
      Token* dotdot = parser__advance(p); /* consume '..' */
      if (rest_name) {
        return parser__error(p, "duplicate rest pattern '..' in destructuring", dotdot);
      }
      if (parser__at_end(p) || parser__peek(p)->type != TOKEN_WORD) {
        return parser__error(p, "expected name after '..' in destructuring pattern", dotdot);
      }
      Token* rn = parser__advance(p); /* consume rest name */
      rest_name = rn->payload.text;
      rest_name_len = rn->length;
      continue;
    }

    /* Error if we already have a rest and there are more positional elements */
    if (rest_name) {
      return parser__error(p, "rest pattern '..' must be last in destructuring", parser__peek(p));
    }

    /* Check for typed entry: type name */
    const char* entry_type = NULL;
    uint32_t entry_type_len = 0;
    Token* tok = parser__peek(p);

    if (tok->type == TOKEN_WORD && p->pos + 1 < p->count) {
      Token* next = &p->tokens[p->pos + 1];
      /* If current is a type keyword and next is also a word, treat as typed */
      if (next->type == TOKEN_WORD &&
          (tok->length == 3 || tok->length == 4)) {
        /* Quick check for known type keywords */
        int is_type = 0;
        if (tok->length == 3) {
          is_type = (memcmp(tok->payload.text, "i32", 3) == 0 ||
                     memcmp(tok->payload.text, "i64", 3) == 0 ||
                     memcmp(tok->payload.text, "u32", 3) == 0 ||
                     memcmp(tok->payload.text, "u64", 3) == 0 ||
                     memcmp(tok->payload.text, "f32", 3) == 0 ||
                     memcmp(tok->payload.text, "f64", 3) == 0 ||
                     memcmp(tok->payload.text, "str", 3) == 0 ||
                     memcmp(tok->payload.text, "dyn", 3) == 0);
        } else if (tok->length == 4) {
          is_type = (memcmp(tok->payload.text, "bool", 4) == 0);
        }
        if (is_type) {
          entry_type = tok->payload.text;
          entry_type_len = tok->length;
          parser__advance(p); /* consume type */
          tok = parser__peek(p);
        }
      }
    }

    /* Expect a binding name (word) */
    if (tok->type != TOKEN_WORD) {
      return parser__error(p, "expected variable name in destructuring pattern", tok);
    }
    parser__advance(p); /* consume name */

    /* Grow arrays if needed */
    if (count >= cap) {
      uint32_t new_cap = cap * 2;
      const char** new_names = (const char**)arena_alloc(p->arena, sizeof(const char*) * new_cap);
      uint32_t* new_name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * new_cap);
      const char** new_types = (const char**)arena_alloc(p->arena, sizeof(const char*) * new_cap);
      uint32_t* new_type_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * new_cap);
      memcpy(new_names, names, sizeof(const char*) * count);
      memcpy(new_name_lens, name_lens, sizeof(uint32_t) * count);
      memcpy(new_types, types, sizeof(const char*) * count);
      memcpy(new_type_lens, type_lens, sizeof(uint32_t) * count);
      names = new_names;
      name_lens = new_name_lens;
      types = new_types;
      type_lens = new_type_lens;
      cap = new_cap;
    }

    names[count] = tok->payload.text;
    name_lens[count] = tok->length;
    types[count] = entry_type;
    type_lens[count] = entry_type_len;
    count++;
  }

  /* Expect closing bracket */
  if (parser__peek(p)->type != TOKEN_RBRACKET) {
    return parser__error(p, "expected ']' to close destructuring pattern", open);
  }
  Token* close = parser__advance(p);

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_DESTRUCTURE_VEC;
  node->start = start;
  node->end   = parser__token_end(close);
  node->data.destructure_vec.names         = names;
  node->data.destructure_vec.name_lens     = name_lens;
  node->data.destructure_vec.types         = types;
  node->data.destructure_vec.type_lens     = type_lens;
  node->data.destructure_vec.count         = count;
  node->data.destructure_vec.rest_name     = rest_name;
  node->data.destructure_vec.rest_name_len = rest_name_len;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Check if current position starts a named destructuring binding
 *
 * Returns true if the current TOKEN_LBRACE is followed by a matching
 * TOKEN_RBRACE and then TOKEN_EQUALS or TOKEN_COLON (e.g. {x, y} = expr).
 * Does NOT advance the parser position.
 * ------------------------------------------------------------------------- */

int parser__lookahead_is_named_destructure_binding(Parser* p) {
  if (parser__peek(p)->type != TOKEN_LBRACE) return 0;
  uint32_t saved = p->pos;
  int depth = 0;
  int result = 0;
  /* We need to distinguish {x, y} = expr from a block { cmd; cmd }.
     Named destructure patterns contain only words, commas, and optional
     type keywords — no commands, operators, or dollar signs. */
  int has_non_pattern = 0;
  while (p->pos < p->count) {
    TokenType t = p->tokens[p->pos].type;
    if (t == TOKEN_LBRACE) depth++;
    else if (t == TOKEN_RBRACE) {
      depth--;
      if (depth == 0) {
        p->pos++;
        if (p->pos < p->count && !has_non_pattern) {
          TokenType after = p->tokens[p->pos].type;
          result = (after == TOKEN_EQUALS || after == TOKEN_COLON);
        }
        break;
      }
    } else if (t == TOKEN_EOF) {
      break;
    } else if (depth == 1) {
      /* Inside the top-level braces: words, commas, newlines, and dotdot are valid */
      if (t != TOKEN_WORD && t != TOKEN_COMMA && t != TOKEN_NEWLINE &&
          t != TOKEN_DOTDOT) {
        has_non_pattern = 1;
      }
    }
    p->pos++;
  }
  p->pos = saved;
  return result;
}

/* -------------------------------------------------------------------------
 * Internal: Parse a named destructuring pattern {x, y} or {i32 x, i32 y}
 *
 * Called when we know the current token is '{' and this is a destructuring
 * context. Supports optional type prefixes and comma separators.
 * Returns AST_DESTRUCTURE_NAMED node.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_destructure_named_pattern(Parser* p) {
  Token* open = parser__advance(p); /* consume '{' */
  SourcePos start = parser__token_start(open);

  /* Collect field entries */
  uint32_t cap = 8;
  const char** names = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  const char** types = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* type_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  uint32_t count = 0;
  const char* rest_name = NULL;
  uint32_t rest_name_len = 0;
  int spread_all = 0;

  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
    /* Skip commas and newlines */
    if (parser__peek(p)->type == TOKEN_COMMA) {
      parser__advance(p);
      continue;
    }
    if (parser__peek(p)->type == TOKEN_NEWLINE) {
      parser__advance(p);
      continue;
    }

    /* Check for rest pattern: ..name, or spread-all: .. */
    if (parser__peek(p)->type == TOKEN_DOTDOT) {
      Token* dotdot = parser__advance(p); /* consume '..' */
      if (rest_name || spread_all) {
        return parser__error(p, "duplicate rest pattern '..' in destructuring", dotdot);
      }
      /* If next token is a word, this is a rest pattern ..name */
      if (!parser__at_end(p) && parser__peek(p)->type == TOKEN_WORD) {
        Token* rn = parser__advance(p); /* consume rest name */
        rest_name = rn->payload.text;
        rest_name_len = rn->length;
      } else {
        /* Spread-all pattern {..} or {x, ..} */
        spread_all = 1;
      }
      continue;
    }

    /* Check for typed entry: type name */
    const char* entry_type = NULL;
    uint32_t entry_type_len = 0;
    Token* tok = parser__peek(p);

    if (tok->type == TOKEN_WORD && p->pos + 1 < p->count) {
      Token* next = &p->tokens[p->pos + 1];
      /* If current is a type keyword and next is also a word, treat as typed */
      if (next->type == TOKEN_WORD &&
          (tok->length == 3 || tok->length == 4)) {
        /* Quick check for known type keywords */
        int is_type = 0;
        if (tok->length == 3) {
          is_type = (memcmp(tok->payload.text, "i32", 3) == 0 ||
                     memcmp(tok->payload.text, "i64", 3) == 0 ||
                     memcmp(tok->payload.text, "u32", 3) == 0 ||
                     memcmp(tok->payload.text, "u64", 3) == 0 ||
                     memcmp(tok->payload.text, "f32", 3) == 0 ||
                     memcmp(tok->payload.text, "f64", 3) == 0 ||
                     memcmp(tok->payload.text, "str", 3) == 0 ||
                     memcmp(tok->payload.text, "dyn", 3) == 0);
        } else if (tok->length == 4) {
          is_type = (memcmp(tok->payload.text, "bool", 4) == 0);
        }
        if (is_type) {
          entry_type = tok->payload.text;
          entry_type_len = tok->length;
          parser__advance(p); /* consume type */
          tok = parser__peek(p);
        }
      }
    }

    /* Expect a field name (word) */
    if (tok->type != TOKEN_WORD) {
      return parser__error(p, "expected field name in destructuring pattern", tok);
    }
    parser__advance(p); /* consume name */

    /* Grow arrays if needed */
    if (count >= cap) {
      uint32_t new_cap = cap * 2;
      const char** new_names = (const char**)arena_alloc(p->arena, sizeof(const char*) * new_cap);
      uint32_t* new_name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * new_cap);
      const char** new_types = (const char**)arena_alloc(p->arena, sizeof(const char*) * new_cap);
      uint32_t* new_type_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * new_cap);
      memcpy(new_names, names, sizeof(const char*) * count);
      memcpy(new_name_lens, name_lens, sizeof(uint32_t) * count);
      memcpy(new_types, types, sizeof(const char*) * count);
      memcpy(new_type_lens, type_lens, sizeof(uint32_t) * count);
      names = new_names;
      name_lens = new_name_lens;
      types = new_types;
      type_lens = new_type_lens;
      cap = new_cap;
    }

    names[count] = tok->payload.text;
    name_lens[count] = tok->length;
    types[count] = entry_type;
    type_lens[count] = entry_type_len;
    count++;
  }

  /* Expect closing brace */
  if (parser__peek(p)->type != TOKEN_RBRACE) {
    return parser__error(p, "expected '}' to close destructuring pattern", open);
  }
  Token* close = parser__advance(p);

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_DESTRUCTURE_NAMED;
  node->start = start;
  node->end   = parser__token_end(close);
  node->data.destructure_named.names         = names;
  node->data.destructure_named.name_lens     = name_lens;
  node->data.destructure_named.types         = types;
  node->data.destructure_named.type_lens     = type_lens;
  node->data.destructure_named.count         = count;
  node->data.destructure_named.rest_name     = rest_name;
  node->data.destructure_named.rest_name_len = rest_name_len;
  node->data.destructure_named.spread_all    = spread_all;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse a command-mode operand (head + args until operator or end)
 *
 * In {} mode, an operand is a command: head followed by arguments.
 * Stops at any operator token or command delimiter.
 * Bare words are wrapped as zero-arg commands (they are calls).
 * Non-word atoms ($var, literals, [], (), {}) are returned bare.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_cmd_operand(Parser* p) {
  /* ctx inside a block/proc — reject (must be at module top level) */
  Token* peek = parser__peek(p);
  if (peek->type == TOKEN_CTX) {
    Token* kw = parser__advance(p);
    return parser__error(p, "ctx declarations must be at module top level", kw);
  }

  /* break [value] → AST_BREAK */
  if (peek->type == TOKEN_BREAK ||
      (peek->type == TOKEN_WORD && peek->length == 5 &&
       memcmp(peek->payload.text, "break", 5) == 0)) {
    Token* kw = parser__advance(p);
    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_BREAK;
    node->start = parser__token_start(kw);
    node->end   = parser__token_end(kw);
    node->data.break_stmt.value = NULL;
    /* Optional value argument */
    if (!parser__is_operand_end(p)) {
      AstNode* val = parser__parse_expr(p);
      if (val != NULL) {
        node->data.break_stmt.value = val;
        node->end = val->end;
      }
    }
    return node;
  }

  /* continue → AST_CONTINUE */
  if (peek->type == TOKEN_CONTINUE ||
      (peek->type == TOKEN_WORD && peek->length == 8 &&
       memcmp(peek->payload.text, "continue", 8) == 0)) {
    Token* kw = parser__advance(p);
    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_CONTINUE;
    node->start = parser__token_start(kw);
    node->end   = parser__token_end(kw);
    return node;
  }

  /* return [value] → AST_RETURN */
  if (peek->type == TOKEN_RETURN ||
      (peek->type == TOKEN_WORD && peek->length == 6 &&
       memcmp(peek->payload.text, "return", 6) == 0)) {
    Token* kw = parser__advance(p);
    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_RETURN;
    node->start = parser__token_start(kw);
    node->end   = parser__token_end(kw);
    node->data.return_stmt.value = NULL;
    /* Optional value argument */
    if (!parser__is_operand_end(p)) {
      AstNode* val = parser__parse_expr(p);
      if (val != NULL) {
        node->data.return_stmt.value = val;
        node->end = val->end;
      }
    }
    return node;
  }

  /* !cmd args... → AST_SHELL_CMD (external shell command) */
  if (peek->type == TOKEN_BANG) {
    Token* bang = parser__advance(p); /* consume '!' */
    SourcePos start = parser__token_start(bang);

    /* Parse command name: word, string, or variable */
    Token* cmd_tok = parser__peek(p);
    if (cmd_tok->type != TOKEN_WORD && cmd_tok->type != TOKEN_STRING &&
        cmd_tok->type != TOKEN_VAR) {
      return parser__error(p, "expected command name after '!'", cmd_tok);
    }

    AstNode* head = parser__parse_expr(p);
    if (head == NULL) {
      return parser__error(p, "expected command name after '!'", bang);
    }

    /* Collect arguments until pipe/and/or/amp or command-end.
     * Unlike regular commands, shell commands don't stop at operators like -
     * since "-la" is a valid shell argument. */
    NodeArray args;
    parser__arr_init(&args, p->arena);

    while (!parser__is_shell_cmd_end(p)) {
      /* Spread expression: ..expr */
      if (parser__peek(p)->type == TOKEN_DOTDOT) {
        Token* dotdot = parser__advance(p); /* consume '..' */
        AstNode* inner = parser__parse_expr(p);
        if (inner == NULL) {
          return parser__error(p, "expected expression after '..' in shell command", dotdot);
        }
        AstNode* spread = ast_alloc(p->arena);
        spread->type  = AST_SPREAD;
        spread->start = parser__token_start(dotdot);
        spread->end   = inner->end;
        spread->data.spread.expr = inner;
        parser__arr_push(&args, spread);
        continue;
      }

      AstNode* arg = parser__parse_expr(p);
      if (arg == NULL) break;
      parser__arr_push(&args, arg);
    }

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_SHELL_CMD;
    node->start = start;
    node->end   = args.count > 0 ? args.nodes[args.count - 1]->end : head->end;
    node->data.shell_cmd.head      = head;
    node->data.shell_cmd.args      = args.nodes;
    node->data.shell_cmd.arg_count = args.count;

    /* Check for background execution: !cmd & */
    node->data.shell_cmd.background = 0;
    if (parser__peek(p)->type == TOKEN_AMP) {
      parser__advance(p); /* consume '&' */
      node->data.shell_cmd.background = 1;
      node->end = parser__token_end(&p->tokens[p->pos - 1]);
    }
    return node;
  }

  TokenType head_token_type = parser__peek(p)->type;
  AstNode* head = parser__parse_expr(p);
  if (head == NULL) return NULL;

  /* Error from sub-expression (e.g. unclosed bracket): propagate immediately */
  if (head->type == AST_ERROR) return head;

  /* proc syntax: proc [type] name {params} {body} — always requires two {} blocks */
  if (head_token_type == TOKEN_PROC && !parser__is_operand_end(p) &&
      parser__peek(p)->type == TOKEN_WORD) {
    return parser__parse_proc_form(p, head);
  }

  /* Anonymous proc: proc {params} {body} — no name, next token is { */
  if (head_token_type == TOKEN_PROC && !parser__is_operand_end(p) &&
      parser__peek(p)->type == TOKEN_LBRACE) {
    return parser__parse_proc_form(p, head);
  }

  /* extern declaration: extern [type] name {params} — no body. */
  if (head_token_type == TOKEN_EXTERN && !parser__is_operand_end(p) &&
      (parser__peek(p)->type == TOKEN_WORD ||
       parser__peek(p)->type == TOKEN_STRUCT ||
       parser__peek(p)->type == TOKEN_LBRACKET)) {
    return parser__parse_extern_form(p, head);
  }

  /* New if syntax: if condition { body } [elif condition { body }]* [else { body }] */
  if (head_token_type == TOKEN_IF && !parser__is_operand_end(p)) {
    return parser__parse_if_form(p, head);
  }

  /* New while syntax: while condition { body } */
  if (head_token_type == TOKEN_WHILE && !parser__is_operand_end(p)) {
    return parser__parse_while_form(p, head);
  }

  /* Reject removed 'defstruct' keyword — use 'struct' instead */
  if (head_token_type == TOKEN_WORD && head->type == AST_LIT_STRING &&
      head->data.lit_string.length == 9 &&
      memcmp(head->data.lit_string.value, "defstruct", 9) == 0) {
    return parser__error(p, "'defstruct' is removed — use 'struct Name {type field, ...}'",
                         &p->tokens[p->pos > 0 ? p->pos - 1 : 0]);
  }

  /* Collect arguments until operator or command-end */
  NodeArray args;
  parser__arr_init(&args, p->arena);

  while (!parser__is_operand_end(p)) {
    AstNode* arg = parser__parse_expr(p);
    if (arg == NULL) break;
    parser__arr_push(&args, arg);
  }

  /* Single expression with no trailing args */
  if (args.count == 0) {
    /* Bare word → zero-arg command call (e.g. 'exit' → '[exit]') */
    if (head_token_type == TOKEN_WORD) {
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_COMMAND;
      node->start = head->start;
      node->end   = head->end;
      node->data.command.head      = head;
      node->data.command.head_id   = ast__compute_head_id(head);
      node->data.command.args      = NULL;
      node->data.command.arg_count = 0;
      return node;
    }
    return head;
  }

  AstNode* node = ast_alloc(p->arena);
  node->type  = AST_COMMAND;
  node->start = head->start;
  node->end   = args.nodes[args.count - 1]->end;
  node->data.command.head      = head;
  node->data.command.head_id   = ast__compute_head_id(head);
  node->data.command.args      = args.nodes;
  node->data.command.arg_count = args.count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse command-mode expression with uniform operator loop
 *
 * Parses: operand [op operand]* left-to-right, no precedence.
 * Each operator becomes [op left right] in the AST.
 * In {} mode, operands are commands (head + args).
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_cmd_expr(Parser* p) {
  AstNode* left = parser__parse_cmd_operand(p);
  if (left == NULL || left->type == AST_ERROR) return left;

  while (!parser__at_end(p) && parser__is_operator(parser__peek(p))) {
    Token* op_tok = parser__advance(p); /* consume operator */

    AstNode* right = parser__parse_cmd_operand(p);
    if (right == NULL || right->type == AST_ERROR) return right;

    /* Build AST_COMMAND: [op left right] */
    AstNode* op_head = ast_alloc(p->arena);
    op_head->type = AST_LIT_STRING;
    op_head->start = parser__token_start(op_tok);
    op_head->end   = parser__token_end(op_tok);
    op_head->data.lit_string.value  = op_tok->payload.text;
    op_head->data.lit_string.length = op_tok->length;

    AstNode** args = ast_alloc_array(p->arena, 2);
    args[0] = left;
    args[1] = right;

    AstNode* cmd = ast_alloc(p->arena);
    cmd->type  = AST_COMMAND;
    cmd->start = left->start;
    cmd->end   = right->end;
    cmd->data.command.head      = op_head;
    cmd->data.command.head_id   = ast__compute_head_id(op_head);
    cmd->data.command.args      = args;
    cmd->data.command.arg_count = 2;

    left = cmd;
  }

  return left;
}

/* -------------------------------------------------------------------------
 * Internal: Parse code block { cmd1; cmd2; ... }
 *
 * Called when the current token is TOKEN_LBRACE.
 * Returns AST_BLOCK on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_block(Parser* p) {
  Token* open = parser__advance(p); /* consume '{' */
  SourcePos block_start = parser__token_start(open);

  NodeArray commands;
  parser__arr_init(&commands, p->arena);

  bool trailing_semi = false;
  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
    /* Skip newlines, semicolons, commas, and pragmas between commands */
    bool saw_semi = false;
    while (parser__peek(p)->type == TOKEN_NEWLINE ||
           parser__peek(p)->type == TOKEN_SEMICOLON ||
           parser__peek(p)->type == TOKEN_COMMA ||
           parser__peek(p)->type == TOKEN_PRAGMA) {
      if (parser__peek(p)->type == TOKEN_SEMICOLON)
        saw_semi = true;
      parser__advance(p);
    }
    if (parser__at_end(p) || parser__peek(p)->type == TOKEN_RBRACE) {
      trailing_semi = saw_semi;
      break;
    }

    AstNode* cmd = parser__parse_cmd_expr(p);
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
  node->data.block.commands      = commands.nodes;
  node->data.block.count         = commands.count;
  node->data.block.trailing_semi = trailing_semi;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse interpolated string "text $var text $[expr] text"
 *
 * Called when the current token is TOKEN_STRING_BEGIN.
 * Collects segments (string literals, var refs, commands) into
 * AST_INTERP_STRING. Empty text segments are skipped.
 * ------------------------------------------------------------------------- */

AstNode* parser__parse_interp_string(Parser* p) {
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
      cmd->data.command.head_id = ast__compute_head_id(head);
      cmd->data.command.args = args.nodes;
      cmd->data.command.arg_count = args.count;
      parser__arr_push(&segments, cmd);
    }
    else if (tok->type == TOKEN_DOLLAR_PAREN) {
      Token* dp_tok = parser__advance(p); /* consume TOKEN_DOLLAR_PAREN */

      /* Parse infix expression contents (same as parse_infix but no LPAREN) */
      AstNode* left = parser__parse_infix_operand(p);
      if (left == NULL) {
        AstNode* err = parser__error(p, "expected expression after $(", dp_tok);
        parser__arr_push(&segments, err);
        /* Skip to TOKEN_RPAREN */
        while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RPAREN) {
          parser__advance(p);
        }
        if (parser__peek(p)->type == TOKEN_RPAREN) {
          parser__advance(p);
        }
        continue;
      }

      /* Binary operator loop — left-to-right, no precedence */
      while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RPAREN) {
        Token* op_tok = parser__peek(p);
        if (!parser__is_operator(op_tok)) break;
        parser__advance(p); /* consume operator */

        AstNode* right = parser__parse_infix_operand(p);
        if (right == NULL) {
          AstNode* err = parser__error(p,
              "expected operand after operator in $()", op_tok);
          parser__sync_paren(p);
          parser__arr_push(&segments, err);
          goto dp_done;
        }

        AstNode* head = ast_alloc(p->arena);
        head->type = AST_LIT_STRING;
        head->start = parser__token_start(op_tok);
        head->end   = parser__token_end(op_tok);
        head->data.lit_string.value  = op_tok->payload.text;
        head->data.lit_string.length = op_tok->length;

        AstNode** args = ast_alloc_array(p->arena, 2);
        args[0] = left;
        args[1] = right;

        AstNode* cmd = ast_alloc(p->arena);
        cmd->type  = AST_COMMAND;
        cmd->start = left->start;
        cmd->end   = right->end;
        cmd->data.command.head      = head;
        cmd->data.command.head_id   = ast__compute_head_id(head);
        cmd->data.command.args      = args;
        cmd->data.command.arg_count = 2;

        left = cmd;
      }

      /* Expect closing ) */
      if (parser__peek(p)->type == TOKEN_RPAREN) {
        parser__advance(p); /* consume ')' */
      }
      parser__arr_push(&segments, left);
      dp_done: ;
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
    /* Skip newlines, semicolons, commas, and pragmas between commands */
    while (parser__peek(&p)->type == TOKEN_NEWLINE ||
           parser__peek(&p)->type == TOKEN_SEMICOLON ||
           parser__peek(&p)->type == TOKEN_COMMA ||
           parser__peek(&p)->type == TOKEN_PRAGMA) {
      parser__advance(&p);
    }
    if (parser__at_end(&p)) break;

    AstNode* cmd;
    if (parser__peek(&p)->type == TOKEN_USE) {
      cmd = parser__parse_use(&p);
    } else if (parser__peek(&p)->type == TOKEN_STRUCT) {
      cmd = parser__parse_defstruct(&p);
    } else if (parser__peek(&p)->type == TOKEN_DEFMACRO) {
      cmd = parser__parse_defmacro(&p);
    } else if (parser__peek(&p)->type == TOKEN_CTX) {
      cmd = parser__parse_ctx_decl(&p);
    } else {
      cmd = parser__parse_cmd_expr(&p);
    }
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

  /* --- Post-parse validation for use declarations --- */

  /* Check: use declarations must appear before any other statements */
  int seen_non_use = 0;
  for (uint32_t i = 0; i < top_level.count; i++) {
    AstNode* node = top_level.nodes[i];
    if (node->type == AST_ERROR) continue;
    if (node->type == AST_USE) {
      if (seen_non_use) {
        /* Replace this node with an error */
        AstNode* err = ast_alloc(arena);
        err->type  = AST_ERROR;
        err->start = node->start;
        err->end   = node->end;
        err->data.error.message = "'use' declarations must appear before all other statements";
        p.error_count++;
        top_level.nodes[i] = err;
      }
    } else {
      seen_non_use = 1;
    }
  }

  /* Check: duplicate use paths */
  for (uint32_t i = 0; i < top_level.count; i++) {
    if (top_level.nodes[i]->type != AST_USE) continue;
    for (uint32_t j = i + 1; j < top_level.count; j++) {
      if (top_level.nodes[j]->type != AST_USE) continue;
      if (top_level.nodes[i]->data.use_decl.path_len ==
              top_level.nodes[j]->data.use_decl.path_len &&
          memcmp(top_level.nodes[i]->data.use_decl.path,
                 top_level.nodes[j]->data.use_decl.path,
                 top_level.nodes[i]->data.use_decl.path_len) == 0) {
        AstNode* err = ast_alloc(arena);
        err->type  = AST_ERROR;
        err->start = top_level.nodes[j]->start;
        err->end   = top_level.nodes[j]->end;
        err->data.error.message = "duplicate use of the same module path";
        p.error_count++;
        top_level.nodes[j] = err;
      }
    }
  }

  /* Private names (underscore-prefixed) are now checked at compile time,
     not parse time, since we need module exports to validate. */

  /* Check: duplicate imported names across use declarations (destructuring form only) */
  for (uint32_t i = 0; i < top_level.count; i++) {
    if (top_level.nodes[i]->type != AST_USE) continue;
    AstNode* use_i = top_level.nodes[i];
    if (use_i->data.use_decl.is_module_binding) continue; /* skip module binding form */
    for (uint32_t ni = 0; ni < use_i->data.use_decl.name_count; ni++) {
      for (uint32_t j = i + 1; j < top_level.count; j++) {
        if (top_level.nodes[j]->type != AST_USE) continue;
        AstNode* use_j = top_level.nodes[j];
        if (use_j->data.use_decl.is_module_binding) continue; /* skip module binding form */
        for (uint32_t nj = 0; nj < use_j->data.use_decl.name_count; nj++) {
          if (use_i->data.use_decl.name_lens[ni] ==
                  use_j->data.use_decl.name_lens[nj] &&
              memcmp(use_i->data.use_decl.names[ni],
                     use_j->data.use_decl.names[nj],
                     use_i->data.use_decl.name_lens[ni]) == 0) {
            AstNode* err = ast_alloc(arena);
            err->type  = AST_ERROR;
            err->start = top_level.nodes[j]->start;
            err->end   = top_level.nodes[j]->end;
            err->data.error.message = "duplicate imported name across use declarations";
            p.error_count++;
            top_level.nodes[j] = err;
            goto next_use_j; /* skip remaining names in this use */
          }
        }
        next_use_j:;
      }
    }
  }

  ParseResult result;
  result.nodes       = top_level.nodes;
  result.count       = top_level.count;
  result.error_count = p.error_count;
  return result;
}

#endif /* PARSER_C */
