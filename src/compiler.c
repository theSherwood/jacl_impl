/*
 * JACL Compiler
 *
 * Translates AST (from parser) into bytecode chunks for the VM.
 */

#ifndef COMPILER_C
#define COMPILER_C

#include <string.h>

/* --- Compile Result --- */

typedef struct {
  BytecodeChunk chunk;
  uint32_t      error_count;
  const char*   error_message;  /* first error message, or NULL */
} CompileResult;

/* --- API --- */

static CompileResult compiler_compile(ParseResult parse, arena_t* arena);

/* --- Internal: Compiler state --- */

typedef struct {
  BytecodeChunk* chunk;
  arena_t*       arena;
  uint32_t       error_count;
  const char*    first_error;
} Compiler;

static void compiler__init(Compiler* c, BytecodeChunk* chunk, arena_t* arena) {
  c->chunk       = chunk;
  c->arena       = arena;
  c->error_count = 0;
  c->first_error = NULL;
}

/* --- Internal: Emit helpers --- */

static void compiler__emit_byte(Compiler* c, uint8_t byte, uint32_t line) {
  chunk_write(c->chunk, byte, line);
}

static void compiler__emit_u16(Compiler* c, uint16_t value, uint32_t line) {
  chunk_write_u16(c->chunk, value, line);
}

static void compiler__emit_constant(Compiler* c, JaclVal value, uint32_t line) {
  uint16_t index = chunk_add_constant(c->chunk, value);
  compiler__emit_byte(c, OP_CONST, line);
  compiler__emit_u16(c, index, line);
}

/* --- Internal: Error reporting --- */

static void compiler__error(Compiler* c, uint32_t line, uint32_t col,
                            const char* message) {
  c->error_count++;
  if (!c->first_error) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "line %u, col %u: %s", line, col, message);
    if (n < 0) n = 0;
    char* msg = (char*)arena_alloc(c->arena, (uint32_t)n + 1);
    memcpy(msg, buf, (uint32_t)n + 1);
    c->first_error = msg;
  }
}

/* --- Internal: Command head matching --- */

static int compiler__head_matches(AstNode* head, const char* name, uint32_t len) {
  return head->type == AST_LIT_STRING &&
         head->data.lit_string.length == len &&
         memcmp(head->data.lit_string.value, name, len) == 0;
}

/* --- Internal: Compile a binary operation --- */

static void compiler__compile_node(Compiler* c, AstNode* node);

static void compiler__compile_binary(Compiler* c, AstNode** args,
                                     uint8_t op, uint32_t line) {
  compiler__compile_node(c, args[0]);
  compiler__compile_node(c, args[1]);
  compiler__emit_byte(c, op, line);
}

/* --- Internal: Compile a command invocation --- */

static void compiler__compile_command(Compiler* c, AstNode* node) {
  AstNode* head = node->data.command.head;
  uint32_t argc = node->data.command.arg_count;
  AstNode** args = node->data.command.args;
  uint32_t line = node->start.line;
  uint32_t col  = node->start.column;

  /* Arithmetic builtins */
  if (compiler__head_matches(head, "+", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "+ requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_ADD, line);
    return;
  }
  if (compiler__head_matches(head, "-", 1)) {
    if (argc == 1) {
      compiler__compile_node(c, args[0]);
      compiler__emit_byte(c, OP_NEG, line);
    } else if (argc == 2) {
      compiler__compile_binary(c, args, OP_SUB, line);
    } else {
      compiler__error(c, line, col, "- requires 1 or 2 arguments");
    }
    return;
  }
  if (compiler__head_matches(head, "*", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "* requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_MUL, line);
    return;
  }
  if (compiler__head_matches(head, "/", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "/ requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_DIV, line);
    return;
  }
  if (compiler__head_matches(head, "%", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "%% requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_MOD, line);
    return;
  }

  /* Comparison builtins */
  if (compiler__head_matches(head, "==", 2)) {
    if (argc != 2) { compiler__error(c, line, col, "== requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_EQ, line);
    return;
  }
  if (compiler__head_matches(head, "<", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "< requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_LT, line);
    return;
  }
  if (compiler__head_matches(head, ">", 1)) {
    if (argc != 2) { compiler__error(c, line, col, "> requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_GT, line);
    return;
  }
  if (compiler__head_matches(head, "<=", 2)) {
    if (argc != 2) { compiler__error(c, line, col, "<= requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_LE, line);
    return;
  }
  if (compiler__head_matches(head, ">=", 2)) {
    if (argc != 2) { compiler__error(c, line, col, ">= requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_GE, line);
    return;
  }

  /* Print builtin */
  if (compiler__head_matches(head, "print", 5)) {
    if (argc != 1) { compiler__error(c, line, col, "print requires 1 argument"); return; }
    compiler__compile_node(c, args[0]);
    compiler__emit_byte(c, OP_PRINT, line);
    return;
  }

  /* def builtin */
  if (compiler__head_matches(head, "def", 3)) {
    if (argc != 2) { compiler__error(c, line, col, "def requires 2 arguments"); return; }
    if (args[0]->type != AST_LIT_STRING) {
      compiler__error(c, line, col, "def first argument must be a name");
      return;
    }
    uint32_t name_len = args[0]->data.lit_string.length;
    if (name_len > 7) {
      compiler__error(c, line, col, "variable name exceeds 7-byte inline limit");
      return;
    }
    /* Compile the value expression */
    compiler__compile_node(c, args[1]);
    /* Add name to constant pool and emit OP_DEF_GLOBAL */
    JaclVal name_val = jacl_inline_string(args[0]->data.lit_string.value, name_len);
    uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
    compiler__emit_byte(c, OP_DEF_GLOBAL, line);
    compiler__emit_u16(c, name_idx, line);
    return;
  }

  /* Unknown command */
  compiler__error(c, line, col, "unknown command");
}

/* --- Internal: Compile a single AST node --- */

static void compiler__compile_node(Compiler* c, AstNode* node) {
  uint32_t line = node->start.line;

  switch (node->type) {

    case AST_LIT_INT: {
      compiler__emit_constant(c, jacl_i32(node->data.lit_int.value), line);
      break;
    }

    case AST_LIT_FLOAT: {
      compiler__emit_constant(c, jacl_f32(node->data.lit_float.value), line);
      break;
    }

    case AST_LIT_STRING: {
      uint32_t len = node->data.lit_string.length;
      if (len > 7) {
        compiler__error(c, line, node->start.column,
                        "string literal exceeds 7-byte inline limit");
        break;
      }
      JaclVal val = jacl_inline_string(node->data.lit_string.value, len);
      compiler__emit_constant(c, val, line);
      break;
    }

    case AST_VAR_REF: {
      uint32_t name_len = node->data.var_ref.length;
      if (name_len > 7) {
        compiler__error(c, line, node->start.column,
                        "variable name exceeds 7-byte inline limit");
        break;
      }
      JaclVal name_val = jacl_inline_string(node->data.var_ref.name, name_len);
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_GET_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
      break;
    }

    case AST_COMMAND: {
      if (node->data.command.arg_count == 0) {
        /* Bare expression (e.g. bare literal at top level): compile head */
        compiler__compile_node(c, node->data.command.head);
      } else {
        compiler__compile_command(c, node);
      }
      break;
    }

    case AST_BLOCK: {
      /* Future story */
      compiler__error(c, line, node->start.column, "blocks not yet supported");
      break;
    }

    case AST_INTERP_STRING: {
      /* Future story */
      compiler__error(c, line, node->start.column,
                      "interpolated strings not yet supported");
      break;
    }

    case AST_ERROR: {
      compiler__error(c, line, node->start.column, "parse error in AST");
      break;
    }
  }
}

/* --- Public API --- */

static CompileResult compiler_compile(ParseResult parse, arena_t* arena) {
  CompileResult result;
  chunk_init(&result.chunk, arena);
  result.error_count = parse.error_count;

  Compiler c;
  compiler__init(&c, &result.chunk, arena);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&c, parse.nodes[i]);

    /* Emit OP_POP between statements to keep the stack clean */
    if (i < parse.count - 1) {
      compiler__emit_byte(&c, OP_POP, parse.nodes[i]->start.line);
    }
  }

  compiler__emit_byte(&c, OP_HALT,
                      parse.count > 0 ? parse.nodes[parse.count - 1]->start.line : 1);

  result.error_count  += c.error_count;
  result.error_message = c.first_error;
  return result;
}

#endif /* COMPILER_C */
