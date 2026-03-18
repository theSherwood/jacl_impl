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
    case TOKEN_TRY: {
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
static AstNode* parser__parse_infix(Parser* p);

/* -------------------------------------------------------------------------
 * Internal: Parse bracketed command [cmd arg1 arg2]
 *
 * Called when the current token is TOKEN_LBRACKET.
 * Returns AST_COMMAND on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_command(Parser* p) {
  Token* open = parser__advance(p); /* consume '[' */
  SourcePos cmd_start = parser__token_start(open);

  /* Lambda shorthand: [\ body...] → [proc "" [it] { body }] */
  if (parser__peek(p)->type == TOKEN_BACKSLASH) {
    parser__advance(p); /* consume '\' */

    /* Parse body head */
    AstNode* body_head = parser__parse_expr(p);
    if (body_head == NULL) {
      AstNode* err = parser__error(p, "expected lambda body after '\\'", open);
      parser__sync_bracket(p);
      return err;
    }

    /* Collect body args until ] */
    NodeArray body_args;
    parser__arr_init(&body_args, p->arena);
    while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACKET) {
      if (parser__peek(p)->type == TOKEN_NEWLINE) {
        uint32_t saved_pos = p->pos;
        while (parser__peek(p)->type == TOKEN_NEWLINE) parser__advance(p);
        if (parser__peek(p)->type == TOKEN_RBRACKET) break;
        p->pos = saved_pos;
        break;
      }
      AstNode* arg = parser__parse_expr(p);
      if (arg == NULL) break;
      parser__arr_push(&body_args, arg);
    }

    /* Expect closing ] */
    if (parser__peek(p)->type != TOKEN_RBRACKET) {
      AstNode* err = parser__error(p, "expected ']' to close lambda", open);
      parser__sync_bracket(p);
      return err;
    }
    Token* close = parser__advance(p); /* consume ']' */

    /* Build body command: [head args...] */
    AstNode* body_cmd = ast_alloc(p->arena);
    body_cmd->type = AST_COMMAND;
    body_cmd->start = body_head->start;
    body_cmd->end   = parser__token_end(close);
    body_cmd->data.command.head      = body_head;
    body_cmd->data.command.args      = body_args.nodes;
    body_cmd->data.command.arg_count = body_args.count;

    /* Wrap body in AST_BLOCK */
    AstNode** block_stmts = ast_alloc_array(p->arena, 1);
    block_stmts[0] = body_cmd;
    AstNode* body_block = ast_alloc(p->arena);
    body_block->type  = AST_BLOCK;
    body_block->start = body_cmd->start;
    body_block->end   = body_cmd->end;
    body_block->data.block.commands = block_stmts;
    body_block->data.block.count    = 1;

    /* Build params: AST_COMMAND with head="it" */
    AstNode* it_name = ast_alloc(p->arena);
    it_name->type = AST_LIT_STRING;
    it_name->start = cmd_start;
    it_name->end   = cmd_start;
    it_name->data.lit_string.value  = "it";
    it_name->data.lit_string.length = 2;

    AstNode* params = ast_alloc(p->arena);
    params->type  = AST_COMMAND;
    params->start = it_name->start;
    params->end   = it_name->end;
    params->data.command.head      = it_name;
    params->data.command.args      = NULL;
    params->data.command.arg_count = 0;

    /* Build empty name for anonymous lambda */
    AstNode* empty_name = ast_alloc(p->arena);
    empty_name->type = AST_LIT_STRING;
    empty_name->start = cmd_start;
    empty_name->end   = cmd_start;
    empty_name->data.lit_string.value  = "";
    empty_name->data.lit_string.length = 0;

    /* Build [proc "" [it] {body}] */
    AstNode* proc_head = ast_alloc(p->arena);
    proc_head->type = AST_LIT_STRING;
    proc_head->start = cmd_start;
    proc_head->end   = cmd_start;
    proc_head->data.lit_string.value  = "proc";
    proc_head->data.lit_string.length = 4;

    AstNode** proc_args = ast_alloc_array(p->arena, 3);
    proc_args[0] = empty_name;  /* name (empty = anonymous) */
    proc_args[1] = params;      /* param list */
    proc_args[2] = body_block;  /* body */

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = cmd_start;
    node->end   = parser__token_end(close);
    node->data.command.head      = proc_head;
    node->data.command.args      = proc_args;
    node->data.command.arg_count = 3;
    return node;
  }

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
 * Internal: Sync to matching ')' for error recovery in infix mode
 * ------------------------------------------------------------------------- */

static void parser__sync_paren(Parser* p) {
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
 * Internal: Check if a token is a binary infix operator
 *
 * TOKEN_OPERATOR covers +, -, *, /, %, ==, !=, <, >, <=, >=
 * TOKEN_AND covers &&, TOKEN_OR covers ||
 * ------------------------------------------------------------------------- */

static int parser__is_infix_binary_op(Token* tok) {
  return tok->type == TOKEN_OPERATOR
      || tok->type == TOKEN_AND
      || tok->type == TOKEN_OR;
}

/* -------------------------------------------------------------------------
 * Internal: Postfix arrow field access ($expr->field)
 *
 * If the next token is TOKEN_ARROW, wraps expr in [. expr field].
 * Loops for chained access: $a->b->c becomes [. [. $a b] c].
 * Returns the original expr unchanged if no arrow follows.
 * ------------------------------------------------------------------------- */

static AstNode* parser__maybe_arrow_access(Parser* p, AstNode* expr) {
  while (!parser__at_end(p) && parser__peek(p)->type == TOKEN_ARROW) {
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

static AstNode* parser__parse_infix_operand(Parser* p) {
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
    head->data.lit_string.value  = "neg";
    head->data.lit_string.length = 3;

    AstNode** args = ast_alloc_array(p->arena, 1);
    args[0] = operand;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = parser__token_start(op);
    node->end   = operand->end;
    node->data.command.head      = head;
    node->data.command.args      = args;
    node->data.command.arg_count = 1;
    return node;
  }

  /* Unary prefix: ~ (logical not) */
  if (tok->type == TOKEN_NOT) {
    Token* op = parser__advance(p);
    AstNode* operand = parser__parse_infix_operand(p);
    if (operand == NULL) {
      return parser__error(p, "expected operand after '~'", op);
    }

    AstNode* head = ast_alloc(p->arena);
    head->type = AST_LIT_STRING;
    head->start = parser__token_start(op);
    head->end   = parser__token_end(op);
    head->data.lit_string.value  = "not";
    head->data.lit_string.length = 3;

    AstNode** args = ast_alloc_array(p->arena, 1);
    args[0] = operand;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = parser__token_start(op);
    node->end   = operand->end;
    node->data.command.head      = head;
    node->data.command.args      = args;
    node->data.command.arg_count = 1;
    return node;
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
    case TOKEN_INT:
    case TOKEN_FLOAT:
    case TOKEN_WORD:
    case TOKEN_STRING:
    case TOKEN_VAR:
    case TOKEN_STRUCT:
    case TOKEN_PROC:
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

static AstNode* parser__parse_infix(Parser* p) {
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
    if (!parser__is_infix_binary_op(op_tok)) {
      break;
    }
    parser__advance(p); /* consume operator */

    /* Determine operator name: && → "and", || → "or", else literal */
    const char* op_name;
    uint32_t op_name_len;
    if (op_tok->type == TOKEN_AND) {
      op_name = "and"; op_name_len = 3;
    } else if (op_tok->type == TOKEN_OR) {
      op_name = "or"; op_name_len = 2;
    } else {
      op_name = op_tok->payload.text;
      op_name_len = op_tok->length;
    }

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
    head->data.lit_string.value  = op_name;
    head->data.lit_string.length = op_name_len;

    AstNode** args = ast_alloc_array(p->arena, 2);
    args[0] = left;
    args[1] = right;

    AstNode* cmd = ast_alloc(p->arena);
    cmd->type  = AST_COMMAND;
    cmd->start = left->start;
    cmd->end   = right->end;
    cmd->data.command.head      = head;
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

static AstNode* parser__parse_expr(Parser* p) {
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

    case TOKEN_INT:
    case TOKEN_FLOAT:
    case TOKEN_WORD:
    case TOKEN_STRING:
    case TOKEN_OPERATOR:
    case TOKEN_VAR:
    /* New operator tokens */
    case TOKEN_PIPE:
    case TOKEN_BACKSLASH:
    case TOKEN_BANG:
    case TOKEN_DOTDOT:
    case TOKEN_AMP:
    case TOKEN_AND:
    case TOKEN_OR:
    case TOKEN_NOT:
    case TOKEN_EQUALS:
    case TOKEN_COLON:
    case TOKEN_DOUBLE_COLON:
    /* New keyword tokens */
    case TOKEN_STRUCT:
    case TOKEN_PROC:
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

static int parser__is_command_end(Parser* p) {
  TokenType t = parser__peek(p)->type;
  return t == TOKEN_NEWLINE || t == TOKEN_SEMICOLON || t == TOKEN_COMMA
      || t == TOKEN_EOF || t == TOKEN_RBRACE || t == TOKEN_PIPE;
}

/* -------------------------------------------------------------------------
 * Internal: Parse use declaration: use "path" [name1 name2 ...]
 *
 * Called when the current token is TOKEN_USE.
 * Returns AST_USE on success, AST_ERROR on failure.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_use(Parser* p) {
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

  /* Expect '[' for name list */
  Token* bracket_tok = parser__peek(p);
  if (bracket_tok->type != TOKEN_LBRACKET) {
    return parser__error(p, "expected '[' after use path", bracket_tok);
  }
  parser__advance(p); /* consume '[' */

  /* Check for empty name list */
  if (parser__peek(p)->type == TOKEN_RBRACKET) {
    Token* close = parser__advance(p);
    AstNode* err = parser__error(p, "use name list must not be empty", close);
    err->start = start;
    return err;
  }

  /* Parse names until ']' */
  NodeArray names;
  parser__arr_init(&names, p->arena);

  while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACKET) {
    Token* name_tok = parser__peek(p);
    if (name_tok->type != TOKEN_WORD) {
      return parser__error(p, "expected name in use list", name_tok);
    }
    parser__advance(p);
    parser__arr_push(&names, (AstNode*)(void*)name_tok); /* temp: store Token* */
  }

  /* Expect closing ']' */
  if (parser__peek(p)->type != TOKEN_RBRACKET) {
    return parser__error(p, "expected ']' to close use name list", bracket_tok);
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
  node->data.use_decl.path       = path;
  node->data.use_decl.path_len   = path_len;
  node->data.use_decl.names      = name_strs;
  node->data.use_decl.name_lens  = name_lens;
  node->data.use_decl.name_count = names.count;
  return node;
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

static const char* parser__parse_inline_struct_type(Parser* p, uint32_t* out_len) {
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

static AstNode* parser__parse_defstruct(Parser* p) {
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

  SourcePos last_end = parser__token_end(name_tok);

  /* ── New syntax: struct Name {type name, type name, ...} ── */
  if (!parser__at_end(p) && parser__peek(p)->type == TOKEN_LBRACE) {
    Token* open = parser__advance(p); /* consume '{' */

    while (!parser__at_end(p) && parser__peek(p)->type != TOKEN_RBRACE) {
      /* Skip commas and newlines between fields */
      while (!parser__at_end(p) &&
             (parser__peek(p)->type == TOKEN_COMMA ||
              parser__peek(p)->type == TOKEN_NEWLINE ||
              parser__peek(p)->type == TOKEN_SEMICOLON)) {
        parser__advance(p);
      }
      if (parser__at_end(p) || parser__peek(p)->type == TOKEN_RBRACE) break;

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
  for (uint32_t i = 0; i < field_count; i++) {
    field_names[i] = tmp_fnames[i];
    field_name_lens[i] = tmp_fname_lens[i];
    field_types[i] = tmp_ftypes[i];
    field_type_lens[i] = tmp_ftype_lens[i];
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
  node->data.defstruct.field_count = field_count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Parse proc parameter list in braces {type name, type name, ...}
 *
 * Called when the current token is TOKEN_LBRACE and we're parsing new-style
 * proc parameters. Returns AST_COMMAND (same shape as [param ...] lists)
 * so the compiler can process them identically.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_proc_params(Parser* p) {
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
       Each slot is "name", "type name", or "& name". */
    while (!parser__at_end(p) &&
           parser__peek(p)->type != TOKEN_COMMA &&
           parser__peek(p)->type != TOKEN_RBRACE &&
           parser__peek(p)->type != TOKEN_NEWLINE) {
      AstNode* elem = parser__parse_atom(p);
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
    node->data.command.args      = NULL;
    node->data.command.arg_count = 0;
  } else {
    /* head = first element, args = rest */
    node->data.command.head = elems.nodes[0];
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
 * Internal: Parse new-style proc form
 *
 * Called after TOKEN_PROC has been consumed as the head in bare command
 * context. Handles: proc [return_type] name {params} {body}
 * Produces the same AST_COMMAND shape the compiler expects.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_proc_form(Parser* p, AstNode* proc_head) {
  SourcePos start = proc_head->start;
  NodeArray args;
  parser__arr_init(&args, p->arena);

  /* Detect pattern: word { (name only) vs word word { (return_type + name).
     p->pos points to the first word after 'proc'. */
  if (p->tokens[p->pos + 1].type == TOKEN_LBRACE) {
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

static AstNode* parser__parse_if_form(Parser* p, AstNode* if_head) {
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

static AstNode* parser__parse_while_form(Parser* p, AstNode* while_head) {
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

static int parser__lookahead_is_destructure_binding(Parser* p) {
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

static AstNode* parser__parse_destructure_vec_pattern(Parser* p) {
  Token* open = parser__advance(p); /* consume '[' */
  SourcePos start = parser__token_start(open);

  /* Collect binding entries */
  uint32_t cap = 8;
  const char** names = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  const char** types = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* type_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  uint32_t count = 0;

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
  node->data.destructure_vec.names     = names;
  node->data.destructure_vec.name_lens = name_lens;
  node->data.destructure_vec.types     = types;
  node->data.destructure_vec.type_lens = type_lens;
  node->data.destructure_vec.count     = count;
  return node;
}

/* -------------------------------------------------------------------------
 * Internal: Check if current position starts a named destructuring binding
 *
 * Returns true if the current TOKEN_LBRACE is followed by a matching
 * TOKEN_RBRACE and then TOKEN_EQUALS or TOKEN_COLON (e.g. {x, y} = expr).
 * Does NOT advance the parser position.
 * ------------------------------------------------------------------------- */

static int parser__lookahead_is_named_destructure_binding(Parser* p) {
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
      /* Inside the top-level braces: only words, commas, newlines are valid */
      if (t != TOKEN_WORD && t != TOKEN_COMMA && t != TOKEN_NEWLINE) {
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

static AstNode* parser__parse_destructure_named_pattern(Parser* p) {
  Token* open = parser__advance(p); /* consume '{' */
  SourcePos start = parser__token_start(open);

  /* Collect field entries */
  uint32_t cap = 8;
  const char** names = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* name_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  const char** types = (const char**)arena_alloc(p->arena, sizeof(const char*) * cap);
  uint32_t* type_lens = (uint32_t*)arena_alloc(p->arena, sizeof(uint32_t) * cap);
  uint32_t count = 0;

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
  node->data.destructure_named.names     = names;
  node->data.destructure_named.name_lens = name_lens;
  node->data.destructure_named.types     = types;
  node->data.destructure_named.type_lens = type_lens;
  node->data.destructure_named.count     = count;
  return node;
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
  /* break [value] → AST_BREAK */
  Token* peek = parser__peek(p);
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
    if (!parser__is_command_end(p)) {
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
    if (!parser__is_command_end(p)) {
      AstNode* val = parser__parse_expr(p);
      if (val != NULL) {
        node->data.return_stmt.value = val;
        node->end = val->end;
      }
    }
    return node;
  }

  /* Destructuring binding: [a b c] = expr  or  [a b c] : expr
     Detects [pattern] followed by = or : and desugars to [def pattern expr]
     or [mut pattern expr]. */
  if (parser__lookahead_is_destructure_binding(p)) {
    AstNode* pattern = parser__parse_destructure_vec_pattern(p);
    if (pattern->type == AST_ERROR) return pattern;

    Token* op = parser__advance(p); /* consume = or : */
    const char* cmd_name;
    uint32_t cmd_len;
    if (op->type == TOKEN_EQUALS) { cmd_name = "def"; cmd_len = 3; }
    else                           { cmd_name = "mut"; cmd_len = 3; }

    NodeArray args;
    parser__arr_init(&args, p->arena);
    parser__arr_push(&args, pattern); /* destructure pattern */
    while (!parser__is_command_end(p)) {
      AstNode* arg = parser__parse_expr(p);
      if (arg == NULL) break;
      parser__arr_push(&args, arg);
    }

    AstNode* cmd_head = ast_alloc(p->arena);
    cmd_head->type = AST_LIT_STRING;
    cmd_head->start = pattern->start;
    cmd_head->end   = pattern->end;
    cmd_head->data.lit_string.value  = cmd_name;
    cmd_head->data.lit_string.length = cmd_len;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = pattern->start;
    node->end   = (args.count > 0) ? args.nodes[args.count - 1]->end : pattern->end;
    node->data.command.head      = cmd_head;
    node->data.command.args      = args.nodes;
    node->data.command.arg_count = args.count;
    return node;
  }

  /* Named destructuring binding: {x, y} = expr  or  {x, y} : expr
     Detects {pattern} followed by = or : and desugars to [def pattern expr]
     or [mut pattern expr]. */
  if (parser__lookahead_is_named_destructure_binding(p)) {
    AstNode* pattern = parser__parse_destructure_named_pattern(p);
    if (pattern->type == AST_ERROR) return pattern;

    Token* op = parser__advance(p); /* consume = or : */
    const char* cmd_name;
    uint32_t cmd_len;
    if (op->type == TOKEN_EQUALS) { cmd_name = "def"; cmd_len = 3; }
    else                           { cmd_name = "mut"; cmd_len = 3; }

    NodeArray args;
    parser__arr_init(&args, p->arena);
    parser__arr_push(&args, pattern); /* destructure pattern */
    while (!parser__is_command_end(p)) {
      AstNode* arg = parser__parse_expr(p);
      if (arg == NULL) break;
      parser__arr_push(&args, arg);
    }

    AstNode* cmd_head = ast_alloc(p->arena);
    cmd_head->type = AST_LIT_STRING;
    cmd_head->start = pattern->start;
    cmd_head->end   = pattern->end;
    cmd_head->data.lit_string.value  = cmd_name;
    cmd_head->data.lit_string.length = cmd_len;

    AstNode* node = ast_alloc(p->arena);
    node->type  = AST_COMMAND;
    node->start = pattern->start;
    node->end   = (args.count > 0) ? args.nodes[args.count - 1]->end : pattern->end;
    node->data.command.head      = cmd_head;
    node->data.command.args      = args.nodes;
    node->data.command.arg_count = args.count;
    return node;
  }

  TokenType head_token_type = parser__peek(p)->type;
  AstNode* head = parser__parse_expr(p);
  if (head == NULL) return NULL;

  /* Error from sub-expression (e.g. unclosed bracket): propagate immediately */
  if (head->type == AST_ERROR) return head;

  /* proc syntax: proc [type] name {params} {body} — always requires two {} blocks */
  if (head_token_type == TOKEN_PROC && !parser__is_command_end(p) &&
      parser__peek(p)->type == TOKEN_WORD) {
    return parser__parse_proc_form(p, head);
  }

  /* New if syntax: if condition { body } [elif condition { body }]* [else { body }] */
  if (head_token_type == TOKEN_IF && !parser__is_command_end(p)) {
    return parser__parse_if_form(p, head);
  }

  /* New while syntax: while condition { body } */
  if (head_token_type == TOKEN_WHILE && !parser__is_command_end(p)) {
    return parser__parse_while_form(p, head);
  }

  /* Reject removed 'defstruct' keyword — use 'struct' instead */
  if (head_token_type == TOKEN_WORD && head->type == AST_LIT_STRING &&
      head->data.lit_string.length == 9 &&
      memcmp(head->data.lit_string.value, "defstruct", 9) == 0) {
    return parser__error(p, "'defstruct' is removed — use 'struct Name {type field, ...}'",
                         &p->tokens[p->pos > 0 ? p->pos - 1 : 0]);
  }

  /* Binding operator desugaring (command mode only):
     name = val      →  [def name val]
     type name = val  →  [def type name val]
     name : val      →  [mut name val]
     type name : val  →  [mut type name val]
     name :: val     →  [set name val]
  */
  if (head_token_type == TOKEN_WORD) {
    Token* next = parser__peek(p);

    /* Untyped binding: name = val, name : val, name :: val */
    if (next->type == TOKEN_EQUALS || next->type == TOKEN_COLON ||
        next->type == TOKEN_DOUBLE_COLON) {
      const char* cmd_name;
      uint32_t cmd_len;
      if (next->type == TOKEN_EQUALS)       { cmd_name = "def"; cmd_len = 3; }
      else if (next->type == TOKEN_COLON)    { cmd_name = "mut"; cmd_len = 3; }
      else                                    { cmd_name = "set"; cmd_len = 3; }

      parser__advance(p); /* consume operator */

      NodeArray args;
      parser__arr_init(&args, p->arena);
      parser__arr_push(&args, head); /* name becomes first arg */
      while (!parser__is_command_end(p)) {
        AstNode* arg = parser__parse_expr(p);
        if (arg == NULL) break;
        parser__arr_push(&args, arg);
      }

      AstNode* cmd_head = ast_alloc(p->arena);
      cmd_head->type = AST_LIT_STRING;
      cmd_head->start = head->start;
      cmd_head->end   = head->end;
      cmd_head->data.lit_string.value  = cmd_name;
      cmd_head->data.lit_string.length = cmd_len;

      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_COMMAND;
      node->start = head->start;
      node->end   = (args.count > 0) ? args.nodes[args.count - 1]->end : head->end;
      node->data.command.head      = cmd_head;
      node->data.command.args      = args.nodes;
      node->data.command.arg_count = args.count;
      return node;
    }

    /* Typed binding: type name = val, type name : val */
    if (next->type == TOKEN_WORD && p->pos + 1 < p->count) {
      Token* after_name = &p->tokens[p->pos + 1];
      if (after_name->type == TOKEN_EQUALS || after_name->type == TOKEN_COLON) {
        const char* cmd_name;
        uint32_t cmd_len;
        if (after_name->type == TOKEN_EQUALS) { cmd_name = "def"; cmd_len = 3; }
        else                                   { cmd_name = "mut"; cmd_len = 3; }

        AstNode* name_node = parser__parse_expr(p); /* parse name */
        parser__advance(p); /* consume operator */

        NodeArray args;
        parser__arr_init(&args, p->arena);
        parser__arr_push(&args, head);      /* type */
        parser__arr_push(&args, name_node); /* name */
        while (!parser__is_command_end(p)) {
          AstNode* arg = parser__parse_expr(p);
          if (arg == NULL) break;
          parser__arr_push(&args, arg);
        }

        AstNode* cmd_head = ast_alloc(p->arena);
        cmd_head->type = AST_LIT_STRING;
        cmd_head->start = head->start;
        cmd_head->end   = head->end;
        cmd_head->data.lit_string.value  = cmd_name;
        cmd_head->data.lit_string.length = cmd_len;

        AstNode* node = ast_alloc(p->arena);
        node->type  = AST_COMMAND;
        node->start = head->start;
        node->end   = args.nodes[args.count - 1]->end;
        node->data.command.head      = cmd_head;
        node->data.command.args      = args.nodes;
        node->data.command.arg_count = args.count;
        return node;
      }
    }
  }

  NodeArray args;
  parser__arr_init(&args, p->arena);

  while (!parser__is_command_end(p)) {
    AstNode* arg = parser__parse_expr(p);
    if (arg == NULL) break;
    parser__arr_push(&args, arg);
  }

  /* Single expression with no trailing args */
  if (args.count == 0) {
    /* Bare word at statement position → zero-arg command call.
       'exit' on its own line desugars to '[exit]'.
       '$var' is a value read; use '[$var]' for invocation. */
    if (head_token_type == TOKEN_WORD) {
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_COMMAND;
      node->start = head->start;
      node->end   = head->end;
      node->data.command.head      = head;
      node->data.command.args      = NULL;
      node->data.command.arg_count = 0;
      return node;
    }
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
 * Internal: Parse piped command chain: cmd1 | cmd2 | cmd3
 *
 * Wraps parser__parse_bare_command, then handles pipe chaining.
 * Pipes thread the left result as the first argument of the right command:
 *   foo $a | bar $b  →  [bar [foo $a] $b]
 *   a | b | c        →  [c [b [a]]]
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_piped_command(Parser* p) {
  AstNode* left = parser__parse_bare_command(p);
  if (left == NULL || left->type == AST_ERROR) return left;

  while (parser__peek(p)->type == TOKEN_PIPE) {
    parser__advance(p); /* consume | */

    /* Skip newlines after pipe for multi-line pipe chains */
    while (parser__peek(p)->type == TOKEN_NEWLINE) {
      parser__advance(p);
    }

    AstNode* right = parser__parse_bare_command(p);
    if (right == NULL || right->type == AST_ERROR) return right;

    if (right->type == AST_COMMAND) {
      /* Insert left as first arg of right command */
      uint32_t old_count = right->data.command.arg_count;
      uint32_t new_count = old_count + 1;
      AstNode** new_args = (AstNode**)arena_alloc(p->arena,
                                                    sizeof(AstNode*) * new_count);
      new_args[0] = left;
      for (uint32_t i = 0; i < old_count; i++) {
        new_args[i + 1] = right->data.command.args[i];
      }
      right->data.command.args      = new_args;
      right->data.command.arg_count = new_count;
      right->start = left->start;
    } else {
      /* Wrap non-command as call with left as first arg */
      AstNode* node = ast_alloc(p->arena);
      node->type  = AST_COMMAND;
      node->start = left->start;
      node->end   = right->end;
      node->data.command.head = right;
      AstNode** new_args = (AstNode**)arena_alloc(p->arena, sizeof(AstNode*));
      new_args[0] = left;
      node->data.command.args      = new_args;
      node->data.command.arg_count = 1;
      right = node;
    }

    left = right;
  }

  return left;
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
    /* Skip newlines, semicolons, and commas between commands */
    while (parser__peek(p)->type == TOKEN_NEWLINE ||
           parser__peek(p)->type == TOKEN_SEMICOLON ||
           parser__peek(p)->type == TOKEN_COMMA) {
      parser__advance(p);
    }
    if (parser__at_end(p) || parser__peek(p)->type == TOKEN_RBRACE) break;

    AstNode* cmd = parser__parse_piped_command(p);
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
        if (!parser__is_infix_binary_op(op_tok)) break;
        parser__advance(p); /* consume operator */

        const char* op_name;
        uint32_t op_name_len;
        if (op_tok->type == TOKEN_AND) {
          op_name = "and"; op_name_len = 3;
        } else if (op_tok->type == TOKEN_OR) {
          op_name = "or"; op_name_len = 2;
        } else {
          op_name = op_tok->payload.text;
          op_name_len = op_tok->length;
        }

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
        head->data.lit_string.value  = op_name;
        head->data.lit_string.length = op_name_len;

        AstNode** args = ast_alloc_array(p->arena, 2);
        args[0] = left;
        args[1] = right;

        AstNode* cmd = ast_alloc(p->arena);
        cmd->type  = AST_COMMAND;
        cmd->start = left->start;
        cmd->end   = right->end;
        cmd->data.command.head      = head;
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
    /* Skip newlines, semicolons, and commas between commands */
    while (parser__peek(&p)->type == TOKEN_NEWLINE ||
           parser__peek(&p)->type == TOKEN_SEMICOLON ||
           parser__peek(&p)->type == TOKEN_COMMA) {
      parser__advance(&p);
    }
    if (parser__at_end(&p)) break;

    AstNode* cmd;
    if (parser__peek(&p)->type == TOKEN_USE) {
      cmd = parser__parse_use(&p);
    } else if (parser__peek(&p)->type == TOKEN_STRUCT) {
      cmd = parser__parse_defstruct(&p);
    } else {
      cmd = parser__parse_piped_command(&p);
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

  /* Check: private names (underscore-prefixed) in use declarations */
  for (uint32_t i = 0; i < top_level.count; i++) {
    if (top_level.nodes[i]->type != AST_USE) continue;
    AstNode* use_node = top_level.nodes[i];
    for (uint32_t ni = 0; ni < use_node->data.use_decl.name_count; ni++) {
      if (use_node->data.use_decl.names[ni][0] == '_') {
        AstNode* err = ast_alloc(arena);
        err->type  = AST_ERROR;
        err->start = use_node->start;
        err->end   = use_node->end;
        err->data.error.message = "cannot import private name (underscore-prefixed)";
        p.error_count++;
        top_level.nodes[i] = err;
        break; /* already replaced this node */
      }
    }
  }

  /* Check: duplicate imported names across use declarations */
  for (uint32_t i = 0; i < top_level.count; i++) {
    if (top_level.nodes[i]->type != AST_USE) continue;
    AstNode* use_i = top_level.nodes[i];
    for (uint32_t ni = 0; ni < use_i->data.use_decl.name_count; ni++) {
      for (uint32_t j = i + 1; j < top_level.count; j++) {
        if (top_level.nodes[j]->type != AST_USE) continue;
        AstNode* use_j = top_level.nodes[j];
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
