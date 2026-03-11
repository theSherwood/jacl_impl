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
  return t == TOKEN_NEWLINE || t == TOKEN_SEMICOLON || t == TOKEN_COMMA
      || t == TOKEN_EOF || t == TOKEN_RBRACE;
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
 * Internal: Parse defstruct declaration: defstruct Name [field :type] ...
 *
 * Called when the current token is TOKEN_DEFSTRUCT.
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

    /* Colon: TOKEN_OPERATOR starting with ':' */
    Token* colon = parser__peek(p);
    if (colon->type != TOKEN_OPERATOR || colon->length < 1 || colon->payload.text[0] != ':') {
      parser__error(p, "expected ':type' in inline struct field", colon);
      return NULL;
    }
    parser__advance(p);
    buf[pos++] = ':';

    if (colon->length > 1) {
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
      if (tname->type == TOKEN_WORD &&
          tname->length == 6 && memcmp(tname->payload.text, "struct", 6) == 0 &&
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
  Token* kw_tok = parser__advance(p); /* consume 'defstruct' */
  SourcePos start = parser__token_start(kw_tok);

  /* Expect struct name (a word) */
  Token* name_tok = parser__peek(p);
  if (name_tok->type != TOKEN_WORD) {
    return parser__error(p, "expected struct name after 'defstruct'", name_tok);
  }
  parser__advance(p);
  const char* struct_name = name_tok->payload.text;
  uint32_t struct_name_len = name_tok->length;

  /* Parse field definitions: [name :type] ... */
  NodeArray field_names_arr;
  parser__arr_init(&field_names_arr, p->arena);

  /* We'll collect field info as Token* pairs (name, type) stored in the array */
  const char** field_names = NULL;
  uint32_t* field_name_lens = NULL;
  const char** field_types = NULL;
  uint32_t* field_type_lens = NULL;
  uint32_t field_count = 0;

  /* Temporary storage - use a simple growable approach */
  #define DEFSTRUCT_MAX_FIELDS 64
  const char* tmp_fnames[DEFSTRUCT_MAX_FIELDS];
  uint32_t tmp_fname_lens[DEFSTRUCT_MAX_FIELDS];
  const char* tmp_ftypes[DEFSTRUCT_MAX_FIELDS];
  uint32_t tmp_ftype_lens[DEFSTRUCT_MAX_FIELDS];

  SourcePos last_end = parser__token_end(name_tok);

  while (!parser__at_end(p) && !parser__is_command_end(p)) {
    Token* open = parser__peek(p);
    if (open->type != TOKEN_LBRACKET) {
      return parser__error(p, "expected '[' for field definition", open);
    }
    parser__advance(p); /* consume '[' */

    /* Field name */
    Token* fname_tok = parser__peek(p);
    if (fname_tok->type != TOKEN_WORD) {
      AstNode* err = parser__error(p, "expected field name", fname_tok);
      parser__sync_bracket(p);
      return err;
    }
    parser__advance(p);

    /* Type annotation: expect a word starting with ':' or an operator ':' followed by word */
    Token* type_tok = parser__peek(p);
    const char* type_str = NULL;
    uint32_t type_len = 0;

    if (type_tok->type == TOKEN_OPERATOR &&
        type_tok->length >= 1 && type_tok->payload.text[0] == ':') {
      /* Operator token starting with ':' — type name is the rest */
      parser__advance(p);
      if (type_tok->length > 1) {
        /* :i32 etc — the colon and type are in one token */
        type_str = type_tok->payload.text + 1;
        type_len = type_tok->length - 1;
      } else {
        /* Just ':' — check for inline struct or type name */
        Token* tname = parser__peek(p);
        if (tname->type == TOKEN_WORD &&
            tname->length == 6 && memcmp(tname->payload.text, "struct", 6) == 0 &&
            p->pos + 1 < p->count &&
            p->tokens[p->pos + 1].type == TOKEN_LBRACE) {
          /* Inline struct type */
          uint32_t inline_len = 0;
          type_str = parser__parse_inline_struct_type(p, &inline_len);
          if (!type_str) {
            parser__sync_bracket(p);
            return parser__error(p, "invalid inline struct type", tname);
          }
          type_len = inline_len;
        } else if (tname->type != TOKEN_WORD) {
          AstNode* err = parser__error(p, "expected type name after ':'", tname);
          parser__sync_bracket(p);
          return err;
        } else {
          parser__advance(p);
          type_str = tname->payload.text;
          type_len = tname->length;
        }
      }
    } else {
      AstNode* err = parser__error(p, "expected ':type' annotation for field", type_tok);
      parser__sync_bracket(p);
      return err;
    }

    /* Expect closing ']' */
    Token* close = parser__peek(p);
    if (close->type != TOKEN_RBRACKET) {
      AstNode* err = parser__error(p, "expected ']' to close field definition", close);
      parser__sync_bracket(p);
      return err;
    }
    parser__advance(p);
    last_end = parser__token_end(close);

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

  /* Validate: at least one field */
  if (field_count == 0) {
    return parser__error(p, "struct must have at least one field", name_tok);
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
 * Internal: Parse a top-level bare command
 *
 * Reads the first expression as the command head, then collects subsequent
 * expressions as arguments until a command delimiter (newline, semicolon,
 * or EOF). If the head is already a bracketed command with no trailing
 * arguments, it is returned directly without wrapping.
 * ------------------------------------------------------------------------- */

static AstNode* parser__parse_bare_command(Parser* p) {
  TokenType head_token_type = parser__peek(p)->type;
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
    } else if (parser__peek(&p)->type == TOKEN_DEFSTRUCT) {
      cmd = parser__parse_defstruct(&p);
    } else {
      cmd = parser__parse_bare_command(&p);
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
