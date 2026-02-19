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

/* --- Internal: Local variable tracking --- */

#define COMPILER_LOCALS_MAX 256

typedef struct {
  JaclVal name;     /* inline string name */
  int     depth;    /* scope depth when declared */
} Local;

/* --- Internal: Compiler state --- */

typedef struct {
  BytecodeChunk* chunk;
  arena_t*       arena;
  uint32_t       error_count;
  const char*    first_error;
  Local          locals[COMPILER_LOCALS_MAX];
  uint32_t       local_count;
  int            scope_depth;
} Compiler;

static void compiler__init(Compiler* c, BytecodeChunk* chunk, arena_t* arena) {
  c->chunk       = chunk;
  c->arena       = arena;
  c->error_count = 0;
  c->first_error = NULL;
  c->local_count = 0;
  c->scope_depth = 0;
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

/* --- Internal: Scope and local variable helpers --- */

static void compiler__begin_scope(Compiler* c) {
  c->scope_depth++;
}

static void compiler__end_scope(Compiler* c, uint32_t line) {
  c->scope_depth--;
  uint32_t pop_count = 0;
  while (c->local_count > 0 &&
         c->locals[c->local_count - 1].depth > c->scope_depth) {
    c->local_count--;
    pop_count++;
  }
  if (pop_count > 0) {
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)pop_count, line);
  }
}

static void compiler__add_local(Compiler* c, JaclVal name,
                                uint32_t line, uint32_t col) {
  if (c->local_count >= COMPILER_LOCALS_MAX) {
    compiler__error(c, line, col, "too many local variables in function");
    return;
  }
  Local* local = &c->locals[c->local_count++];
  local->name  = name;
  local->depth = c->scope_depth;
}

static int compiler__resolve_local(Compiler* c, JaclVal name) {
  for (int i = (int)c->local_count - 1; i >= 0; i--) {
    if (c->locals[i].name == name) {
      return i;
    }
  }
  return -1;
}

/* Forward declaration for compile_block_expr */
static void compiler__compile_node(Compiler* c, AstNode* node);

/* --- Internal: Jump patching helpers --- */

static uint32_t compiler__emit_jump(Compiler* c, uint8_t instruction,
                                     uint32_t line) {
  compiler__emit_byte(c, instruction, line);
  compiler__emit_byte(c, 0xFF, line);  /* placeholder high byte */
  compiler__emit_byte(c, 0xFF, line);  /* placeholder low byte */
  return c->chunk->code_count - 2;
}

static void compiler__patch_jump(Compiler* c, uint32_t offset) {
  uint32_t jump = c->chunk->code_count - offset - 2;
  c->chunk->code[offset]     = (uint8_t)((jump >> 8) & 0xFF);
  c->chunk->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

/* --- Internal: Compile block as expression (last stmt value stays on stack) --- */

static void compiler__compile_block_expr(Compiler* c, AstNode* block_node) {
  uint32_t line  = block_node->start.line;
  uint32_t count = block_node->data.block.count;
  uint32_t scope_start_locals = c->local_count;

  compiler__begin_scope(c);

  if (count == 0) {
    compiler__end_scope(c, line);
    compiler__emit_byte(c, OP_NIL, line);
    return;
  }

  for (uint32_t i = 0; i < count - 1; i++) {
    compiler__compile_node(c, block_node->data.block.commands[i]);
    compiler__emit_byte(c, OP_POP, line);
  }
  compiler__compile_node(c, block_node->data.block.commands[count - 1]);

  /* Clean up locals while preserving the result on the stack top */
  uint32_t pop_count = c->local_count - scope_start_locals;
  c->scope_depth--;
  c->local_count = scope_start_locals;

  if (pop_count > 0) {
    /* Result is on top, locals are below it. Save result into the first
       local's slot, then POP_N removes the rest plus the old top copy. */
    compiler__emit_byte(c, OP_SET_LOCAL, line);
    compiler__emit_byte(c, (uint8_t)scope_start_locals, line);
    compiler__emit_byte(c, OP_POP_N, line);
    compiler__emit_byte(c, (uint8_t)pop_count, line);
  }
}

/* --- Internal: Command head matching --- */

static int compiler__head_matches(AstNode* head, const char* name, uint32_t len) {
  return head->type == AST_LIT_STRING &&
         head->data.lit_string.length == len &&
         memcmp(head->data.lit_string.value, name, len) == 0;
}

/* --- Internal: Compile a binary operation --- */

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

    JaclVal name_val = jacl_inline_string(args[0]->data.lit_string.value, name_len);

    if (c->scope_depth > 0) {
      /* Local variable: value is on stack as the local slot */
      compiler__add_local(c, name_val, line, col);
      /* def returns nil */
      compiler__emit_byte(c, OP_NIL, line);
    } else {
      /* Global variable */
      uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
      compiler__emit_byte(c, OP_DEF_GLOBAL, line);
      compiler__emit_u16(c, name_idx, line);
    }
    return;
  }

  /* if conditional */
  if (compiler__head_matches(head, "if", 2)) {
    if (argc != 2 && argc != 3) {
      compiler__error(c, line, col, "if requires 2 or 3 arguments");
      return;
    }
    if (args[1]->type != AST_BLOCK) {
      compiler__error(c, line, col, "if then-branch must be a block");
      return;
    }
    if (argc == 3 && args[2]->type != AST_BLOCK) {
      compiler__error(c, line, col, "if else-branch must be a block");
      return;
    }

    /* Compile condition */
    compiler__compile_node(c, args[0]);

    /* OP_JUMP_IF_FALSE over then-body */
    uint32_t then_jump = compiler__emit_jump(c, OP_JUMP_IF_FALSE, line);

    /* Compile then-body as expression */
    compiler__compile_block_expr(c, args[1]);

    /* OP_JUMP over else-body */
    uint32_t else_jump = compiler__emit_jump(c, OP_JUMP, line);

    /* Patch JUMP_IF_FALSE to here */
    compiler__patch_jump(c, then_jump);

    if (argc == 3) {
      /* Compile else-body as expression */
      compiler__compile_block_expr(c, args[2]);
    } else {
      /* No else: push nil */
      compiler__emit_byte(c, OP_NIL, line);
    }

    /* Patch JUMP to here */
    compiler__patch_jump(c, else_jump);
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

      int local_slot = compiler__resolve_local(c, name_val);
      if (local_slot != -1) {
        compiler__emit_byte(c, OP_GET_LOCAL, line);
        compiler__emit_byte(c, (uint8_t)local_slot, line);
      } else {
        uint16_t name_idx = chunk_add_constant(c->chunk, name_val);
        compiler__emit_byte(c, OP_GET_GLOBAL, line);
        compiler__emit_u16(c, name_idx, line);
      }
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
      compiler__begin_scope(c);
      uint32_t count = node->data.block.count;
      for (uint32_t i = 0; i < count; i++) {
        compiler__compile_node(c, node->data.block.commands[i]);
        compiler__emit_byte(c, OP_POP, line);
      }
      compiler__end_scope(c, line);
      /* Block evaluates to nil */
      compiler__emit_byte(c, OP_NIL, line);
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
