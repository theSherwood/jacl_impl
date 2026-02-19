#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

#define ARENA_IMPLEMENTATION
#include "../arena/arena.h"

#define VALUE_IMPLEMENTATION
#include "../value/value.h"

#define LEXER_IMPLEMENTATION
#include "../lexer/lexer.h"

#define AST_IMPLEMENTATION
#include "../parser/ast.h"

#define PARSER_IMPLEMENTATION
#include "../parser/parser.h"

#define BYTECODE_IMPLEMENTATION
#include "../compiler/bytecode.h"

#define COMPILER_IMPLEMENTATION
#include "./compiler.h"

/* ===== Helper: parse source and compile ===== */

static CompileResult compile_source(const char* source, arena_t* arena) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  return compiler_compile(parse, arena);
}

/* ===== US-003: Compiler skeleton and literal compilation ===== */

/* Test: integer literal compiles to OP_CONST with jacl_i32 */
static int test_compile_int(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("42", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_i32(cr.chunk.constants[0]));
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 42);

  /* Bytecode: OP_CONST, u16(0), OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 4);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[1], 0); /* high byte */
  ASSERT_INT_EQ(cr.chunk.code[2], 0); /* low byte */
  ASSERT_INT_EQ(cr.chunk.code[3], OP_HALT);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: zero integer literal */
static int test_compile_zero_int(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("0", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_i32(cr.chunk.constants[0]));
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: float literal compiles to OP_CONST with jacl_f32 */
static int test_compile_float(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("3.14", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_f32(cr.chunk.constants[0]));
  /* Check float value approximately */
  float f = jacl_as_f32(cr.chunk.constants[0]);
  ASSERT(f > 3.13f && f < 3.15f);

  /* Bytecode: OP_CONST, u16(0), OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 4);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_HALT);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: short string (<=7 bytes) compiles to OP_CONST with inline string */
static int test_compile_short_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("hello", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[0]));

  char buf[8];
  jacl_inline_string_get(cr.chunk.constants[0], buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "hello");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: string exactly 7 bytes compiles OK */
static int test_compile_string_7_bytes(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("abcdefg", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[0]));

  char buf[8];
  jacl_inline_string_get(cr.chunk.constants[0], buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "abcdefg");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: string >7 bytes produces compile error */
static int test_compile_long_string_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("abcdefgh", &arena);

  ASSERT(cr.error_count > 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-statement with OP_POP between */
static int test_compile_multi_statement(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  /* Two statements separated by newline: "42\n99" */
  CompileResult cr = compile_source("42\n99", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 42);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 99);

  /* Expected bytecode: OP_CONST u16(0) OP_POP OP_CONST u16(1) OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 8);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_POP);
  ASSERT_INT_EQ(cr.chunk.code[4], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[5], 0);
  ASSERT_INT_EQ(cr.chunk.code[6], 1);
  ASSERT_INT_EQ(cr.chunk.code[7], OP_HALT);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-statement with semicolons */
static int test_compile_multi_semicolons(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("1; 2; 3", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 3);

  /* Bytecode: CONST(0) POP CONST(1) POP CONST(2) HALT */
  /* 3+1+3+1+3+1 = 12 bytes */
  ASSERT_U32_EQ(cr.chunk.code_count, 12);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_POP);
  ASSERT_INT_EQ(cr.chunk.code[4], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[7], OP_POP);
  ASSERT_INT_EQ(cr.chunk.code[8], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[11], OP_HALT);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: single statement has no trailing OP_POP */
static int test_compile_single_no_pop(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("100", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);

  /* Should only be: OP_CONST u16(0) OP_HALT - no OP_POP */
  ASSERT_U32_EQ(cr.chunk.code_count, 4);
  for (uint32_t i = 0; i < cr.chunk.code_count; i++) {
    ASSERT(cr.chunk.code[i] != OP_POP);
  }

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_HALT is always emitted at end */
static int test_compile_halt_at_end(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("1; 2", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.chunk.code_count > 0);
  ASSERT_INT_EQ(cr.chunk.code[cr.chunk.code_count - 1], OP_HALT);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: empty program compiles to just OP_HALT */
static int test_compile_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.code_count, 1);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_HALT);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: constant pool indices are correct for multiple constants */
static int test_compile_const_pool_indices(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("10\n20\n30", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 3);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 10);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 20);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[2]), 30);

  /* Verify each OP_CONST references the right index */
  /* Statement 0: code[0]=OP_CONST, code[1..2]=u16(0) */
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  /* Statement 1: code[4]=OP_CONST, code[5..6]=u16(1) */
  ASSERT_INT_EQ(cr.chunk.code[5], 0);
  ASSERT_INT_EQ(cr.chunk.code[6], 1);
  /* Statement 2: code[8]=OP_CONST, code[9..10]=u16(2) */
  ASSERT_INT_EQ(cr.chunk.code[9], 0);
  ASSERT_INT_EQ(cr.chunk.code[10], 2);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: line numbers are tracked in bytecode */
static int test_compile_line_tracking(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("42\n99", &arena);

  ASSERT_U32_EQ(cr.error_count, 0);

  /* First statement on line 1: OP_CONST(line 1), u16(line 1) */
  ASSERT_U32_EQ(cr.chunk.lines[0], 1);
  /* OP_POP between statements */
  ASSERT_U32_EQ(cr.chunk.lines[3], 1);
  /* Second statement on line 2: OP_CONST(line 2), u16(line 2) */
  ASSERT_U32_EQ(cr.chunk.lines[4], 2);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: parse errors propagate to CompileResult */
static int test_compile_parse_error_propagated(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  /* "[" is an unclosed bracket, should produce parse error */
  CompileResult cr = compile_source("[", &arena);

  ASSERT(cr.error_count > 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-003: Compiler skeleton and literal compilation */
    { "compile_int",                 test_compile_int },
    { "compile_zero_int",             test_compile_zero_int },
    { "compile_float",               test_compile_float },
    { "compile_short_string",        test_compile_short_string },
    { "compile_string_7_bytes",      test_compile_string_7_bytes },
    { "compile_long_string_error",   test_compile_long_string_error },
    { "compile_multi_statement",     test_compile_multi_statement },
    { "compile_multi_semicolons",    test_compile_multi_semicolons },
    { "compile_single_no_pop",       test_compile_single_no_pop },
    { "compile_halt_at_end",         test_compile_halt_at_end },
    { "compile_empty",               test_compile_empty },
    { "compile_const_pool_indices",  test_compile_const_pool_indices },
    { "compile_line_tracking",       test_compile_line_tracking },
    { "compile_parse_error_propagated", test_compile_parse_error_propagated },
  };

  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0;
  int failed = 0;

  for (int i = 0; i < total; i++) {
    printf("  %-40s ", tests[i].name);
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n  %d/%d passed\n", passed, total);
  return failed > 0 ? 1 : 0;
}
