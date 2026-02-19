/*
 * ---------------------------------------------------------------------------
 * JACL Compiler
 * ---------------------------------------------------------------------------
 * Translates AST (from parser) into bytecode chunks for the VM.
 *
 * Single-header convention:
 *   #ifndef COMPILER_H / #define COMPILER_H for declarations
 *   #ifdef COMPILER_IMPLEMENTATION for implementation
 */

#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>

#include "../parser/parser.h"
#include "./bytecode.h"

/* --- Compile Result --- */

typedef struct {
  BytecodeChunk chunk;
  uint32_t      error_count;
} CompileResult;

/* --- API --- */

static CompileResult compiler_compile(ParseResult parse, arena_t* arena);

#endif /* COMPILER_H */

/* =========================================================================
 * Implementation Section
 * Define COMPILER_IMPLEMENTATION before including to generate function bodies.
 * ========================================================================= */

#ifdef COMPILER_IMPLEMENTATION
#ifndef COMPILER_IMPL_GUARD_
#define COMPILER_IMPL_GUARD_

#include <string.h>

/* --- Internal: Compiler state --- */

typedef struct {
  BytecodeChunk* chunk;
  arena_t*       arena;
  uint32_t       error_count;
} Compiler;

static void compiler__init(Compiler* c, BytecodeChunk* chunk, arena_t* arena) {
  c->chunk       = chunk;
  c->arena       = arena;
  c->error_count = 0;
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

static void compiler__error(Compiler* c, const char* message) {
  (void)message;
  c->error_count++;
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

  /* Arithmetic builtins */
  if (compiler__head_matches(head, "+", 1)) {
    if (argc != 2) { compiler__error(c, "+ requires 2 arguments"); return; }
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
      compiler__error(c, "- requires 1 or 2 arguments");
    }
    return;
  }
  if (compiler__head_matches(head, "*", 1)) {
    if (argc != 2) { compiler__error(c, "* requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_MUL, line);
    return;
  }
  if (compiler__head_matches(head, "/", 1)) {
    if (argc != 2) { compiler__error(c, "/ requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_DIV, line);
    return;
  }
  if (compiler__head_matches(head, "%", 1)) {
    if (argc != 2) { compiler__error(c, "%% requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_MOD, line);
    return;
  }

  /* Comparison builtins */
  if (compiler__head_matches(head, "==", 2)) {
    if (argc != 2) { compiler__error(c, "== requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_EQ, line);
    return;
  }
  if (compiler__head_matches(head, "<", 1)) {
    if (argc != 2) { compiler__error(c, "< requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_LT, line);
    return;
  }
  if (compiler__head_matches(head, ">", 1)) {
    if (argc != 2) { compiler__error(c, "> requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_GT, line);
    return;
  }
  if (compiler__head_matches(head, "<=", 2)) {
    if (argc != 2) { compiler__error(c, "<= requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_LE, line);
    return;
  }
  if (compiler__head_matches(head, ">=", 2)) {
    if (argc != 2) { compiler__error(c, ">= requires 2 arguments"); return; }
    compiler__compile_binary(c, args, OP_GE, line);
    return;
  }

  /* Unknown command */
  compiler__error(c, "unknown command");
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
        compiler__error(c, "string literal exceeds 7-byte inline limit");
        break;
      }
      JaclVal val = jacl_inline_string(node->data.lit_string.value, len);
      compiler__emit_constant(c, val, line);
      break;
    }

    case AST_VAR_REF: {
      /* Will be implemented in US-007 */
      compiler__error(c, "variable references not yet supported");
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
      compiler__error(c, "blocks not yet supported");
      break;
    }

    case AST_INTERP_STRING: {
      /* Future story */
      compiler__error(c, "interpolated strings not yet supported");
      break;
    }

    case AST_ERROR: {
      compiler__error(c, "parse error in AST");
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

  result.error_count += c.error_count;
  return result;
}

#endif /* COMPILER_IMPL_GUARD_ */
#endif /* COMPILER_IMPLEMENTATION */
