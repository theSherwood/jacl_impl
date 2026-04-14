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
  AST_USE,           /* use "path" [name1 name2 ...] */
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
  uint32_t    scope_mark;  /* hygiene: 0 = no macro context, >0 = macro expansion */
  uint8_t     is_caret;    /* US-013: ^name in syntax-quote — force scope mark 0 */
  uint8_t     is_gensym;   /* US-014: var-ref produced by gensym — accepted as binding name */
  union {
    struct { AstNode*  head; AstNode** args; uint32_t arg_count; } command;
    struct { int32_t   value; }                                    lit_int;
    struct { float     value; }                                    lit_float;
    struct { const char* value;   uint32_t length; }               lit_string;
    struct { const char* name;    uint32_t length; }               var_ref;
    struct { AstNode**   commands; uint32_t count; bool trailing_semi; } block;
    struct { AstNode**   segments; uint32_t count; }               interp_string;
    struct { const char* path; uint32_t path_len;
             const char** names; uint32_t* name_lens;
             uint32_t name_count; }                               use_decl;
    struct { const char* name; uint32_t name_len;
             const char** field_names; uint32_t* field_name_lens;
             const char** field_types; uint32_t* field_type_lens;
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
    struct { const char* message; }                                error;
  } data;
};

/* -------------------------------------------------------------------------
 * Arena helper for allocating AST nodes
 * ------------------------------------------------------------------------- */

AstNode* ast_alloc(arena_t* arena) {
  return (AstNode*)arena_alloc(arena, sizeof(AstNode));
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
      ast__buf_cstr(b, "\" [");
      for (uint32_t i = 0; i < node->data.use_decl.name_count; i++) {
        if (i > 0) ast__buf_char(b, ' ');
        ast__buf_str(b, node->data.use_decl.names[i],
                     node->data.use_decl.name_lens[i]);
      }
      ast__buf_char(b, ']');
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
