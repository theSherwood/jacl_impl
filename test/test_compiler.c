#include "test_helpers.h"
#include "../src/jacl.h"

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

/* ===== Helper: parse source and compile ===== */

static CompileResult compile_source(const char* source, arena_t* arena, ThreadHeap* heap) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, arena);
  return compiler_compile(parse, arena, &intern_table, heap, NULL, NULL, JACL_NIL, NULL, NULL);
}

/* ===== US-003: Compiler skeleton and literal compilation ===== */

/* Test: integer literal compiles to OP_CONST with jacl_i32 */
static int test_compile_int(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("42", &arena, &heap);

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

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: zero integer literal */
static int test_compile_zero_int(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("0", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_i32(cr.chunk.constants[0]));
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: float literal compiles to OP_CONST with jacl_f32 */
static int test_compile_float(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("3.14", &arena, &heap);

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

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: short string (<=7 bytes) compiles to OP_CONST with inline string */
static int test_compile_short_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("hello", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[0]));

  char buf[8];
  jacl_inline_string_get(cr.chunk.constants[0], buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "hello");

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: string exactly 7 bytes compiles OK */
static int test_compile_string_7_bytes(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("abcdefg", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[0]));

  char buf[8];
  jacl_inline_string_get(cr.chunk.constants[0], buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "abcdefg");

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: string >7 bytes compiles as heap-interned string (no error) */
static int test_compile_long_string_heap(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("\"abcdefgh\"", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.chunk.const_count >= 1);
  ASSERT(jacl_is_heap_string(cr.chunk.constants[0]));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-statement with OP_CHECK_ERROR between */
static int test_compile_multi_statement(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Two statements separated by newline: "42\n99" */
  CompileResult cr = compile_source("42\n99", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 42);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 99);

  /* Expected bytecode: OP_CONST u16(0) OP_CHECK_ERROR u16(0) OP_CONST u16(1) OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 10);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_CHECK_ERROR);
  ASSERT_INT_EQ(cr.chunk.code[4], 0);
  ASSERT_INT_EQ(cr.chunk.code[5], 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[7], 0);
  ASSERT_INT_EQ(cr.chunk.code[8], 1);
  ASSERT_INT_EQ(cr.chunk.code[9], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-statement with semicolons */
static int test_compile_multi_semicolons(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("1; 2; 3", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 3);

  /* Bytecode: CONST(0) CHECK_ERROR u16(0) CONST(1) CHECK_ERROR u16(0) CONST(2) HALT */
  /* 3+3+3+3+3+1 = 16 bytes */
  ASSERT_U32_EQ(cr.chunk.code_count, 16);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_CHECK_ERROR);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[9], OP_CHECK_ERROR);
  ASSERT_INT_EQ(cr.chunk.code[12], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[15], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: single statement has no trailing OP_POP */
static int test_compile_single_no_pop(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("100", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);

  /* Should only be: OP_CONST u16(0) OP_HALT - no OP_POP */
  ASSERT_U32_EQ(cr.chunk.code_count, 4);
  for (uint32_t i = 0; i < cr.chunk.code_count; i++) {
    ASSERT(cr.chunk.code[i] != OP_POP);
  }

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_HALT is always emitted at end */
static int test_compile_halt_at_end(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("1; 2", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.chunk.code_count > 0);
  ASSERT_INT_EQ(cr.chunk.code[cr.chunk.code_count - 1], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: empty program compiles to just OP_HALT */
static int test_compile_empty(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.code_count, 1);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: constant pool indices are correct for multiple constants */
static int test_compile_const_pool_indices(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("10\n20\n30", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 3);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 10);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 20);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[2]), 30);

  /* Verify each OP_CONST references the right index */
  /* Statement 0: code[0]=OP_CONST, code[1..2]=u16(0) */
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  /* Statement 1: code[6]=OP_CONST (after CHECK_ERROR u16), code[7..8]=u16(1) */
  ASSERT_INT_EQ(cr.chunk.code[7], 0);
  ASSERT_INT_EQ(cr.chunk.code[8], 1);
  /* Statement 2: code[12]=OP_CONST, code[13..14]=u16(2) */
  ASSERT_INT_EQ(cr.chunk.code[13], 0);
  ASSERT_INT_EQ(cr.chunk.code[14], 2);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: line numbers are tracked in bytecode */
static int test_compile_line_tracking(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("42\n99", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);

  /* First statement on line 1: OP_CONST(line 1), u16(line 1) */
  ASSERT_U32_EQ(cr.chunk.lines[0], 1);
  /* OP_CHECK_ERROR between statements (line 1) */
  ASSERT_U32_EQ(cr.chunk.lines[3], 1);
  /* Second statement on line 2: OP_CONST(line 2) after CHECK_ERROR u16 */
  ASSERT_U32_EQ(cr.chunk.lines[6], 2);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: parse errors propagate to CompileResult */
static int test_compile_parse_error_propagated(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* "[" is an unclosed bracket, should produce parse error */
  CompileResult cr = compile_source("[", &arena, &heap);

  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-004: Arithmetic builtins (compiler) ===== */

/* Test: [+ 1 2] compiles to OP_CONST(1), OP_CONST(2), OP_ADD */
static int test_compile_add(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[+ 1 2]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 1);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 2);

  /* Bytecode: OP_CONST u16(0) OP_CONST u16(1) OP_ADD OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 8);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[4], 0);
  ASSERT_INT_EQ(cr.chunk.code[5], 1);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_ADD);
  ASSERT_INT_EQ(cr.chunk.code[7], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [- 10 3] compiles to OP_CONST(10), OP_CONST(3), OP_SUB */
static int test_compile_sub(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[- 10 3]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 10);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 3);

  ASSERT_U32_EQ(cr.chunk.code_count, 8);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_SUB);
  ASSERT_INT_EQ(cr.chunk.code[7], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [* 4 5], [/ 10 2], [% 7 3] compile to correct opcodes */
static int test_compile_mul_div_mod(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Test * */
  CompileResult cr = compile_source("[* 4 5]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_MUL);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* Test / */
  tracker_reset();
  arena = (arena_t){ .allocator = tracked_allocator };
  gc_block_pool_init(&pool);
  gc_heap_init(&heap, &pool);
  cr = compile_source("[/ 10 2]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_DIV);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* Test % */
  tracker_reset();
  arena = (arena_t){ .allocator = tracked_allocator };
  gc_block_pool_init(&pool);
  gc_heap_init(&heap, &pool);
  cr = compile_source("[% 7 3]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_MOD);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: unary negation [- 5] compiles to OP_CONST(5), OP_NEG */
static int test_compile_unary_neg(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[- 5]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 5);

  /* Bytecode: OP_CONST u16(0) OP_NEG OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 5);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_NEG);
  ASSERT_INT_EQ(cr.chunk.code[4], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested [+ 1 [* 2 3]] compiles correctly */
static int test_compile_nested_arithmetic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[+ 1 [* 2 3]]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 3);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 1);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 2);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[2]), 3);

  /* Bytecode: CONST(1) CONST(2) CONST(3) MUL ADD HALT */
  /* = 3 + 3 + 3 + 1 + 1 + 1 = 12 bytes */
  ASSERT_U32_EQ(cr.chunk.code_count, 12);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);  /* 1 */
  ASSERT_INT_EQ(cr.chunk.code[3], OP_CONST);  /* 2 */
  ASSERT_INT_EQ(cr.chunk.code[6], OP_CONST);  /* 3 */
  ASSERT_INT_EQ(cr.chunk.code[9], OP_MUL);
  ASSERT_INT_EQ(cr.chunk.code[10], OP_ADD);
  ASSERT_INT_EQ(cr.chunk.code[11], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: f32 arithmetic compiles the same way as i32 */
static int test_compile_f32_arithmetic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[+ 1.5 2.5]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT(jacl_is_f32(cr.chunk.constants[0]));
  ASSERT(jacl_is_f32(cr.chunk.constants[1]));
  ASSERT_INT_EQ(cr.chunk.code[6], OP_ADD);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-005: Comparison builtins (compiler) ===== */

/* Test: [== 1 2] compiles to OP_CONST(1), OP_CONST(2), OP_EQ */
static int test_compile_eq(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[== 1 2]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 1);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 2);

  /* Bytecode: OP_CONST u16(0) OP_CONST u16(1) OP_EQ OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 8);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_EQ);
  ASSERT_INT_EQ(cr.chunk.code[7], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [< 1 2], [> 5 3], [<= 3 3], [>= 2 5] compile to correct opcodes */
static int test_compile_lt_gt_le_ge(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Test < */
  CompileResult cr = compile_source("[< 1 2]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_LT);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* Test > */
  tracker_reset();
  arena = (arena_t){ .allocator = tracked_allocator };
  gc_block_pool_init(&pool);
  gc_heap_init(&heap, &pool);
  cr = compile_source("[> 5 3]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_GT);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* Test <= */
  tracker_reset();
  arena = (arena_t){ .allocator = tracked_allocator };
  gc_block_pool_init(&pool);
  gc_heap_init(&heap, &pool);
  cr = compile_source("[<= 3 3]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_LE);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* Test >= */
  tracker_reset();
  arena = (arena_t){ .allocator = tracked_allocator };
  gc_block_pool_init(&pool);
  gc_heap_init(&heap, &pool);
  cr = compile_source("[>= 2 5]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_GE);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: f32 comparison compiles the same way */
static int test_compile_f32_comparison(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[< 1.5 2.5]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT(jacl_is_f32(cr.chunk.constants[0]));
  ASSERT(jacl_is_f32(cr.chunk.constants[1]));
  ASSERT_INT_EQ(cr.chunk.code[6], OP_LT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-006: Print builtin (compiler) ===== */

/* Test: [print 42] compiles to OP_CONST(42), OP_PRINT */
static int test_compile_print(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[print 42]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 42);

  /* Bytecode: OP_CONST u16(0) OP_PRINT OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 5);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_PRINT);
  ASSERT_INT_EQ(cr.chunk.code[4], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: print with no arguments is a compile error */
static int test_compile_print_no_args(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Bare "print" has no args - parser wraps as AST_COMMAND{head: "print", args: [], argc: 0}
     which goes to the bare-expression path, not compile_command.
     We need to use [print] to force a command with 0 args. */
  CompileResult cr = compile_source("[print]", &arena, &heap);

  /* This is actually a 0-arg command, which takes the bare expression path.
     But print with 0 args in brackets should error. Let's check. */
  /* Actually, [print] parses as command with head "print" and 0 args.
     The bare expression path compiles head only. This doesn't call compile_command.
     So to test "print requires 1 argument", we need [print 1 2] (too many args). */
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* Test with 2 arguments */
  tracker_reset();
  arena = (arena_t){ .allocator = tracked_allocator };
  gc_block_pool_init(&pool);
  gc_heap_init(&heap, &pool);
  cr = compile_source("[print 1 2]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: [print [+ 1 2]] compiles to nested expression with OP_PRINT */
static int test_compile_print_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[print [+ 1 2]]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 1);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 2);

  /* Bytecode: CONST(1) CONST(2) ADD PRINT HALT */
  /* = 3 + 3 + 1 + 1 + 1 = 9 bytes */
  ASSERT_U32_EQ(cr.chunk.code_count, 9);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_ADD);
  ASSERT_INT_EQ(cr.chunk.code[7], OP_PRINT);
  ASSERT_INT_EQ(cr.chunk.code[8], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-007: Global environment, def, and variable references (compiler) ===== */

/* Test: def x 42 compiles to OP_CONST(42), OP_DEF_GLOBAL(name_idx) */
static int test_compile_def(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("x = 42", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 2);
  /* constants[0] = i32(42), constants[1] = inline_string("x") */
  ASSERT(jacl_is_i32(cr.chunk.constants[0]));
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 42);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[1]));

  /* Bytecode: OP_CONST u16(0) OP_DEF_GLOBAL u16(1) OP_HALT */
  ASSERT_U32_EQ(cr.chunk.code_count, 7);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_DEF_GLOBAL);
  ASSERT_INT_EQ(cr.chunk.code[4], 0);
  ASSERT_INT_EQ(cr.chunk.code[5], 1);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: def x [+ 1 2] compiles value expression before OP_DEF_GLOBAL */
static int test_compile_def_expr(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("x = [+ 1 2]", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  /* constants: 1, 2, "x" */
  ASSERT_U32_EQ(cr.chunk.const_count, 3);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[0]), 1);
  ASSERT_INT_EQ(jacl_as_i32(cr.chunk.constants[1]), 2);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[2]));

  /* Bytecode: CONST(1) CONST(2) ADD DEF_GLOBAL(name) HALT */
  /* = 3 + 3 + 1 + 3 + 1 = 11 bytes */
  ASSERT_U32_EQ(cr.chunk.code_count, 11);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_CONST);
  ASSERT_INT_EQ(cr.chunk.code[6], OP_ADD);
  ASSERT_INT_EQ(cr.chunk.code[7], OP_DEF_GLOBAL);
  ASSERT_INT_EQ(cr.chunk.code[10], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: def with wrong argument count is a compile error */
static int test_compile_def_wrong_argc(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Too few args: def x */
  CompileResult cr = compile_source("def x", &arena, &heap);
  ASSERT(cr.error_count > 0);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* x = 1 2 is now [= [x] [1 2]] — valid parse, runtime error */
  /* (no compile-time error expected with new uniform operator parsing) */

  TEST_PASS();
}

/* Test: def first argument must be AST_LIT_STRING */
static int test_compile_def_non_string_name(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* def 42 10 - first arg is int, not string */
  CompileResult cr = compile_source("def 42 10", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: $x compiles to OP_GET_GLOBAL(name_idx) */
static int test_compile_var_ref(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("$x", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[0]));

  /* Bytecode: OP_GET_GLOBAL u16(name_idx=0) u16(ic_slot=0xFFFF) OP_HALT
     ($x at statement position is a value read, not a call) */
  ASSERT_U32_EQ(cr.chunk.code_count, 6);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_GET_GLOBAL);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], 0xFF);
  ASSERT_INT_EQ(cr.chunk.code[4], 0xFF);
  ASSERT_INT_EQ(cr.chunk.code[5], OP_HALT);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-007: Integration tests (compile + execute) ===== */

/* Capture buffer for testing print output */
typedef struct {
  char   buf[256];
  uint32_t len;
} PrintCapture;

static void capture_print(const char* text, uint32_t len, void* ctx) {
  PrintCapture* cap = (PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

/* Test: def x 42; print $x outputs "42\n" */
static int test_def_print_var(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("x = 42\n[print $x]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: def x [+ 1 2]; print $x outputs "3\n" */
static int test_def_expr_print_var(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("x = [+ 1 2]\n[print $x]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: $true evaluates to JACL_TRUE, $false to JACL_FALSE, $nil to JACL_NIL
   Note: $var at statement position desugars to a zero-arg call (M6 US-005),
   so we test these builtins as arguments in [if] expressions instead. */
static int test_var_true_false_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* $true used as condition → truthy */
  VMResult result = jacl_run("[if $true { print 1 } { print 0 }]",
                             &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n");

  /* $false used as condition → falsy */
  cap.len = 0;
  cap.buf[0] = '\0';
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  result = jacl_run("[if $false { print 1 } { print 0 }]",
                    &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");

  /* $nil used as condition → falsy */
  cap.len = 0;
  cap.buf[0] = '\0';
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  result = jacl_run("[if $nil { print 1 } { print 0 }]",
                    &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: undefined variable produces runtime error naming the variable */
static int test_var_undefined(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("$nope", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "nope") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-008: Nested subcommands and multi-statement programs ===== */

/* Test: [print [+ 1 [* 2 3]]] outputs "7\n" */
static int test_nested_print_add_mul(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[print [+ 1 [* 2 3]]]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "7\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [+ [* 2 3] [- 10 4]] evaluates to i32(12) */
static int test_nested_both_args(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[+ [* 2 3] [- 10 4]]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 12);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [+ 1 [+ 2 [+ 3 4]]] evaluates to i32(10) */
static int test_deeply_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[+ 1 [+ 2 [+ 3 4]]]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 10);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-statement with def: def x 10\ndef y 20\nprint [+ $x $y] outputs "30\n" */
static int test_multi_def_print(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("x = 10\ny = 20\n[print [+ $x $y]]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-statement with semicolons: def a 1; def b 2; print [+ $a $b] outputs "3\n" */
static int test_multi_semicolons_def_print(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("a = 1; b = 2; [print [+ $a $b]]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: intermediate statement results are popped (stack clean between stmts) */
static int test_intermediate_results_popped(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Three statements: 10, 20, 30 — only the last should remain on stack */
  CompileResult cr = compile_source("10\n20\n30", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 30);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: final statement result is available as execution result */
static int test_final_result_available(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("x = 5\n[+ $x 10]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 15);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-009: Runtime error reporting ===== */

/* Test: [+ $true 1] produces runtime error with type mismatch message */
static int test_runtime_error_add_bool_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[+ $true 1]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strlen(vm.error_message) > 0);
  ASSERT(strstr(vm.error_message, "bool") != NULL);
  ASSERT(strstr(vm.error_message, "i32") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [< $true 1] produces runtime error */
static int test_runtime_error_lt_bool_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[< $true 1]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "bool") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: print $undefined produces runtime error naming the variable */
static int test_runtime_error_print_undefined(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[print $undef]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "undef") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: runtime errors include source line number */
static int test_runtime_error_line_number(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Error on line 2: def x $true; [+ $x 1] */
  CompileResult cr = compile_source("x = $true\n[+ $x 1]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &cr.chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT_U32_EQ(vm.error_line, 2);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: unknown command name compiles as dynamic call, produces runtime error */
static int test_runtime_error_unknown_command(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* [foo 42] compiles to GET_GLOBAL("foo"), CONST(42), CALL(1) */
  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[foo 42]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "foo") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: string >7 bytes compiles as heap string (no error) */
static int test_compile_long_string_no_error_msg(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("\"abcdefgh\"", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.error_message == NULL);
  ASSERT(jacl_is_heap_string(cr.chunk.constants[0]));

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: vm_exec returns VM_RUNTIME_ERROR on runtime errors */
static int test_vm_returns_runtime_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Multiple error types all return VM_RUNTIME_ERROR */

  /* Type mismatch */
  CompileResult cr = compile_source("[* $true $false]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  VM vm;
  vm_init(&vm, &arena);
  ASSERT_INT_EQ(vm_exec(&vm, &cr.chunk), VM_RUNTIME_ERROR);

  /* Undefined var */
  cr = compile_source("$xyz", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  vm_init(&vm, &arena);
  ASSERT_INT_EQ(vm_exec(&vm, &cr.chunk), VM_RUNTIME_ERROR);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-004 (M4): Compiler local variable resolution ===== */

/* Test: def inside block creates local, $x resolves to OP_GET_LOCAL */
static int test_local_def_in_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* { x = 42; print $x } should print "42\n" */
  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run("{ x = 42; print $x }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: locals are not visible outside their block scope */
static int test_local_not_visible_outside(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("{ x = 42 }\n$x", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "x") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: local shadows global within block, global restored outside */
static int test_local_shadowing(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "x = 1\n{ x = 2; print $x }\n[print $x]",
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "2\n1\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested blocks with locals from outer scope accessible */
static int test_local_nested_scopes(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "{ a = 10; { b = 20; print [+ $a $b] }; print $a }",
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n10\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: same-scope shadowing is now a compile error */
static int test_local_same_scope_shadow(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  /* Redefining x in the same scope is a compile error */
  VMResult result = jacl_run(
    "{ x = 1; x = 2; print $x }",
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "already defined in this scope") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: top-level def still compiles to OP_DEF_GLOBAL */
static int test_local_top_level_still_global(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* At top level, def should use OP_DEF_GLOBAL */
  CompileResult cr = compile_source("x = 42", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  /* Bytecode: OP_CONST u16(0) OP_DEF_GLOBAL u16(1) OP_HALT */
  ASSERT_INT_EQ(cr.chunk.code[3], OP_DEF_GLOBAL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: top-level $x still compiles to OP_GET_GLOBAL */
static int test_local_top_level_var_ref_global(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("$x", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  /* Bytecode: OP_GET_GLOBAL u16(0) OP_HALT */
  ASSERT_INT_EQ(cr.chunk.code[0], OP_GET_GLOBAL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: exceeding 256 locals produces compile error */
static int test_local_max_exceeded(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Build source: { x0 = 0; x1 = 1; ...; x256 = 256 } — 257 unique defs */
  char source[16384];
  int pos = 0;
  pos += snprintf(source + pos, sizeof(source) - (size_t)pos, "{ ");
  for (int i = 0; i <= 256; i++) {
    if (i > 0) {
      pos += snprintf(source + pos, sizeof(source) - (size_t)pos, "; ");
    }
    pos += snprintf(source + pos, sizeof(source) - (size_t)pos, "x%d = %d", i, i);
  }
  pos += snprintf(source + pos, sizeof(source) - (size_t)pos, " }");

  CompileResult cr = compile_source(source, &arena, &heap);

  ASSERT(cr.error_count > 0);
  ASSERT(cr.error_message != NULL);
  ASSERT(strstr(cr.error_message, "too many local") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: empty block returns nil */
static int test_local_empty_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("{ }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-005 (M4): Compile and execute if conditional ===== */

/* Test: [if [> 5 3] { 1 } { 2 }] returns i32(1) — truthy condition takes then-branch */
static int test_if_then_branch(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if [> 5 3] { 1 } { 2 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 1);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [if $false { 1 } { 2 }] returns i32(2) — falsy condition takes else-branch */
static int test_if_else_branch(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $false { 1 } { 2 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 2);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: [if $false { 1 }] returns nil — no else-branch */
static int test_if_no_else_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $false { 1 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested if works — [if $true { if $false { 1 } { 2 } }] returns i32(2) */
static int test_if_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $true { if $false { 1 } { 2 } }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 2);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nil is falsy */
static int test_if_nil_falsy(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $nil { 1 } { 2 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 2);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: 0 is truthy (only nil and false are falsy) */
static int test_if_zero_truthy(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if 0 { 1 } { 2 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 1);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: empty string is truthy */
static int test_if_empty_string_truthy(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  /* Use "" for empty string — parser should handle it */
  VMResult result = jacl_run("[if \"\" { 1 } { 2 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 1);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: if is an expression — can be used in larger expressions */
static int test_if_as_expression(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[+ [if $true { 10 } { 20 }] 5]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 15);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: wrong argument count (1 arg) produces compile error */
static int test_if_wrong_argc_1(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $true]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: wrong argument count (4 args) produces compile error */
static int test_if_wrong_argc_4(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $true { 1 } { 2 } { 3 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: second arg must be AST_BLOCK */
static int test_if_then_not_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $true 42]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: third arg must be AST_BLOCK */
static int test_if_else_not_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $true { 1 } 42]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-006 (M4): Compile and execute proc definition ===== */

/* Test: proc add {a, b} { + $a $b } then [add 1 2] returns i32(3) */
static int test_proc_basic_call(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc add {a, b} { + $a $b }\n[add 1 2]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 3);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: proc greet {name} { print $name } then [greet hello] prints "hello\n" */
static int test_proc_print_param(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "proc greet {name} { print $name }\n[greet hello]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: zero-parameter procedure: proc answer {} { 42 } then [answer] returns i32(42) */
static int test_proc_zero_params(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc answer {} { 42 }\n[answer]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 42);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: proc returns the value of the last expression in the body */
static int test_proc_implicit_return(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc double {n} { * $n 2 }\n[double 5]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 10);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: proc with multiple body statements returns last one */
static int test_proc_multi_stmt_body(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "proc foo {x} { print $x; + $x 1 }\n[foo 10]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n");
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 11);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: redefining a proc name overwrites the binding */
static int test_proc_redefine(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc f {} { 1 }\nproc f {} { 2 }\n[f]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 2);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: wrong argument count for proc (not 3 args) produces compile error */
static int test_proc_wrong_argc(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Too few args */
  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("proc foo {a}", &vm, &arena);
  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  /* Too many args */
  vm_init(&vm, &arena);
  result = jacl_run("proc foo {a} { 1 } extra", &vm, &arena);
  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: calling with wrong arg count produces runtime error */
static int test_proc_call_arg_mismatch(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc add {a, b} { + $a $b }\n[add 1]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "argument") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: proc used as expression (value is nil from def) */
static int test_proc_def_returns_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("proc f {} { 42 }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  /* proc definition at global scope returns nil (from DEF_GLOBAL) */
  ASSERT(jacl_is_nil(vm.stack[0]));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== Syntax Redesign US-003: New proc syntax ===== */

/* Test: proc add {a, b} { + $a $b } then [add 1 2] returns 3 */
static int test_proc_new_syntax_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc add {a, b} { + $a $b }\n[add 1 2]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 3);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: proc with typed return and typed params */
static int test_proc_new_syntax_typed(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc add {i64 a, i64 b} i64 { + $a $b }\n[add 1 2]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 3);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: proc with zero params */
static int test_proc_new_syntax_zero_params(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc answer {} { 42 }\n[answer]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 42);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: proc with multi-statement body */
static int test_proc_new_syntax_multi_stmt(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "proc greet {name} {\n"
    "  print $name\n"
    "  42\n"
    "}\n"
    "[greet hello]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 42);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: closure capture still works with new proc syntax */
static int test_proc_new_syntax_closure(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "x = 10\n"
    "proc add_x {n} { + $n $x }\n"
    "[add_x 5]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 15);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: new proc syntax at top level, implicit return */
static int test_proc_new_syntax_implicit_return(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "proc double {n} { * $n 2 }\n[double 7]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 14);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-007 (M4): Compile and execute while loop ===== */

/* Test: while with false condition never executes body, returns nil */
static int test_while_zero_iterations(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[while $false { 999 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while with nil condition never executes body */
static int test_while_nil_falsy(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[while $nil { 999 }]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while loop iterates and exits correctly (counter-based) */
static int test_while_iterative_sum(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Sum 1..5 using mutable variables in while loop */
  const char* program =
    "i : 1\n"
    "sum : 0\n"
    "[while [<= $i 5] {\n"
    "  sum :: [+ $sum $i]\n"
    "  i :: [+ $i 1]\n"
    "}]\n"
    "[print $sum]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while returns nil when loop exits */
static int test_while_returns_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print the result of a while loop — should be nil */
  const char* program =
    "i : 0\n"
    "[print [while [< $i 3] {\n"
    "  i :: [+ $i 1]\n"
    "}]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while wrong arg count (1 arg) */
static int test_while_wrong_argc_1(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Under the []-flip the while form requires a brace body at parse time. */
  CompileResult cr = compile_source("[while $true]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "expected '{' after while condition") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while wrong arg count (3 args) */
static int test_while_wrong_argc_3(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Under the []-flip the trailing { 2 } is a second, unseparated statement —
   * a parse error rather than a while arity error. */
  CompileResult cr = compile_source("[while $true { 1 } { 2 }]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "between statements") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while body must be a block */
static int test_while_body_not_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Under the []-flip the while form requires a brace body at parse time. */
  CompileResult cr = compile_source("[while $true 42]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "expected '{' after while condition") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: while with counter that decrements to zero */
static int test_while_countdown(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "n : 3\n"
    "[while [> $n 0] {\n"
    "  print $n\n"
    "  n :: [- $n 1]\n"
    "}]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n2\n1\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-008 (M4): Closures and upvalue capture ===== */

/* Test: mkadder pattern — closure captures variable from enclosing scope */
static int test_closure_capture_local(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc mkadd {x} {\n"
    "  proc inner {y} { + $x $y }\n"
    "}\n"
    "add10 = [mkadd 10]\n"
    "[print [add10 5]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multiple closures capture independently */
static int test_closure_independent_captures(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc mkadd {x} {\n"
    "  proc inner {y} { + $x $y }\n"
    "}\n"
    "add5 = [mkadd 5]\n"
    "add20 = [mkadd 20]\n"
    "[print [add5 1]]\n"
    "[print [add20 1]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "6\n21\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested closures (3 levels) — transitive capture */
static int test_closure_nested_3_levels(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc outer {a} {\n"
    "  proc middle {b} {\n"
    "    proc inner {c} { + [+ $a $b] $c }\n"
    "  }\n"
    "}\n"
    "mid = [outer 100]\n"
    "inn = [mid 20]\n"
    "[print [inn 3]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "123\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: closure captures multiple variables */
static int test_closure_capture_multiple(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc make-fn {a, b} {\n"
    "  proc inner {c} { + [+ $a $b] $c }\n"
    "}\n"
    "f = [make-fn 10 20]\n"
    "[print [f 3]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "33\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: upvalue from enclosing function (not global) */
static int test_closure_not_global(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* x=99 at global, x=5 as param — inner should capture param x=5 */
  const char* program =
    "x = 99\n"
    "proc wrap {x} {\n"
    "  proc inner {} { + $x 0 }\n"
    "}\n"
    "f = [wrap 5]\n"
    "[print [f]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-009 (M4): Recursion ===== */

/* Test: factorial via recursion — fact(10) = 3628800 */
static int test_recursion_factorial(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc fact {n} {\n"
    "  if [== $n 0] { 1 } { * $n [fact [- $n 1]] }\n"
    "}\n"
    "[print [fact 10]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3628800\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: fibonacci via recursion — fib(10) = 55 */
static int test_recursion_fibonacci(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "proc fib {n} {\n"
    "  if [< $n 2] { + $n 0 } { + [fib [- $n 1]] [fib [- $n 2]] }\n"
    "}\n"
    "[print [fib 10]]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "55\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: recursion depth limit — exceeding VM_FRAMES_MAX produces stack overflow */
static int test_recursion_depth_limit(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);

  /* Infinite recursion with no base case */
  const char* program =
    "proc inf {n} { inf [+ $n 1] }\n"
    "[inf 0]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "call depth exceeded") != NULL);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-002 (M12/M13): Closure capturing mut pinned in spawn ===== */

/* Helper: find a closure constant by name in a chunk (recursive into nested closures) */
static JaclClosure* find_closure_by_name(BytecodeChunk* chunk, const char* name) {
  for (uint32_t i = 0; i < chunk->const_count; i++) {
    if (jacl_is_closure(chunk->constants[i])) {
      JaclClosure* cl = jacl_as_closure(chunk->constants[i]);
      if (cl->name && strcmp(cl->name, name) == 0) return cl;
      /* Recurse into nested closures */
      JaclClosure* nested = find_closure_by_name(&cl->chunk, name);
      if (nested) return nested;
    }
  }
  return NULL;
}

/* Test: spawn body that captures a mut var (no set!) should be pinned */
static int test_spawn_captures_mut_pinned(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* mut x captured via $x in spawn body (read only, no set!) */
  const char* program =
    "proc main {} {\n"
    "  x : 42\n"
    "  [spawn { + $x 1 }]\n"
    "}\n"
    "[main]";

  CompileResult cr = compile_source(program, &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Find the <spawn> closure — it should be pinned */
  JaclClosure* spawn_cl = find_closure_by_name(&cr.chunk, "<spawn>");
  ASSERT(spawn_cl != NULL);
  ASSERT(spawn_cl->pinned);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: spawn body with only local mut (no capture) should NOT be pinned */
static int test_spawn_local_mut_not_pinned(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* mut x declared inside spawn body — local, no pinning needed */
  const char* program =
    "proc main {} {\n"
    "  spawn {\n"
    "    x : 42\n"
    "    x :: 99\n"
    "    $x\n"
    "  }\n"
    "}\n"
    "[main]";

  CompileResult cr = compile_source(program, &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  JaclClosure* spawn_cl = find_closure_by_name(&cr.chunk, "<spawn>");
  ASSERT(spawn_cl != NULL);
  ASSERT(!spawn_cl->pinned);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: spawn body with def (non-mutable) capture should NOT be pinned */
static int test_spawn_captures_def_not_pinned(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  const char* program =
    "proc main {} {\n"
    "  x = 42\n"
    "  [spawn { + $x 1 }]\n"
    "}\n"
    "[main]";

  CompileResult cr = compile_source(program, &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  JaclClosure* spawn_cl = find_closure_by_name(&cr.chunk, "<spawn>");
  ASSERT(spawn_cl != NULL);
  ASSERT(!spawn_cl->pinned);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transitive capture — proc capturing a closure that captures mut,
   passed to spawn — should be pinned (US-003) */
static int test_spawn_transitive_capture_pinned(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* proc f captures mut x, proc g calls f, g is called in spawn body */
  const char* program =
    "proc main {} {\n"
    "  x : 42\n"
    "  proc f {} { $x }\n"
    "  [spawn { f }]\n"
    "}\n"
    "[main]";

  CompileResult cr = compile_source(program, &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* The <spawn> closure should be pinned because f captures mut x */
  JaclClosure* spawn_cl = find_closure_by_name(&cr.chunk, "<spawn>");
  ASSERT(spawn_cl != NULL);
  ASSERT(spawn_cl->pinned);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: transitive capture via $var ref — proc capturing a closure that
   captures mut, referenced via $f in spawn body — should be pinned */
static int test_spawn_transitive_varref_pinned(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* proc f captures mut x, spawn body uses $f (var ref) */
  const char* program =
    "proc main {} {\n"
    "  x : 42\n"
    "  proc f {} { $x }\n"
    "  [spawn { $f }]\n"
    "}\n"
    "[main]";

  CompileResult cr = compile_source(program, &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  JaclClosure* spawn_cl = find_closure_by_name(&cr.chunk, "<spawn>");
  ASSERT(spawn_cl != NULL);
  ASSERT(spawn_cl->pinned);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: non-mutable closure capture should NOT trigger pinning */
static int test_spawn_transitive_def_not_pinned(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* proc f captures def x (immutable), spawn calls f — NOT pinned */
  const char* program =
    "proc main {} {\n"
    "  x = 42\n"
    "  proc f {} { $x }\n"
    "  [spawn { f }]\n"
    "}\n"
    "[main]";

  CompileResult cr = compile_source(program, &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  JaclClosure* spawn_cl = find_closure_by_name(&cr.chunk, "<spawn>");
  ASSERT(spawn_cl != NULL);
  ASSERT(!spawn_cl->pinned);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-004 (M14): Path resolution and circular import detection ===== */

/* Test: module__resolve_path resolves relative to importer directory */
static int test_module_resolve_path(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  /* Use build.sh (which we know exists) as a test target */
  /* Resolve "build.sh" relative to src/jacl.c's parent directory */
  char importer[PATH_MAX];
  realpath("src/jacl.c", importer);

  /* "../../build.sh" from src/ should resolve to project root's build.sh */
  const char* resolved = module__resolve_path(importer, "../build.sh", &arena);
  ASSERT(resolved != NULL);
  /* The result should be a canonical path ending in /build.sh */
  const char* basename = strrchr(resolved, '/');
  ASSERT(basename != NULL);
  ASSERT_STR_EQ(basename + 1, "build.sh");

  /* Verify it's a canonical path (starts with /) */
  ASSERT(resolved[0] == '/');

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: module__resolve_path returns NULL for missing files */
static int test_module_resolve_path_not_found(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  char importer[PATH_MAX];
  realpath("src/jacl.c", importer);

  const char* resolved = module__resolve_path(importer, "nonexistent_file_xyz.jacl", &arena);
  ASSERT(resolved == NULL);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: ImportStack push/contains/pop */
static int test_import_stack_basic(void) {
  ImportStack stack;
  import_stack__init(&stack);

  ASSERT_U32_EQ(stack.count, 0);
  ASSERT(!import_stack__contains(&stack, "/a.jacl"));

  import_stack__push(&stack, "/a.jacl");
  ASSERT_U32_EQ(stack.count, 1);
  ASSERT(import_stack__contains(&stack, "/a.jacl"));
  ASSERT(!import_stack__contains(&stack, "/b.jacl"));

  import_stack__push(&stack, "/b.jacl");
  ASSERT_U32_EQ(stack.count, 2);
  ASSERT(import_stack__contains(&stack, "/b.jacl"));

  import_stack__pop(&stack);
  ASSERT_U32_EQ(stack.count, 1);
  ASSERT(!import_stack__contains(&stack, "/b.jacl"));
  ASSERT(import_stack__contains(&stack, "/a.jacl"));

  TEST_PASS();
}

/* Test: ImportStack detects circular imports */
static int test_import_stack_circular_detect(void) {
  ImportStack stack;
  import_stack__init(&stack);

  import_stack__push(&stack, "/src/a.jacl");
  import_stack__push(&stack, "/src/b.jacl");

  /* b.jacl trying to import a.jacl → circular */
  ASSERT(import_stack__contains(&stack, "/src/a.jacl"));
  /* c.jacl not in stack → no cycle */
  ASSERT(!import_stack__contains(&stack, "/src/c.jacl"));

  TEST_PASS();
}

/* Test: import_stack__chain_str produces readable cycle description */
static int test_import_stack_chain_str(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  ImportStack stack;
  import_stack__init(&stack);
  import_stack__push(&stack, "/project/a.jacl");
  import_stack__push(&stack, "/project/b.jacl");
  import_stack__push(&stack, "/project/c.jacl");

  /* c.jacl tries to import a.jacl: chain should be a.jacl -> b.jacl -> c.jacl -> a.jacl */
  const char* chain = import_stack__chain_str(&stack, "/project/a.jacl", &arena);
  ASSERT_STR_EQ(chain, "a.jacl -> b.jacl -> c.jacl -> a.jacl");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-005 (M14): Underscore-prefix privacy ===== */

static int test_module_is_private(void) {
  ASSERT(module__is_private("_helper", 7));
  ASSERT(module__is_private("_", 1));
  ASSERT(module__is_private("_internal", 9));
  ASSERT(!module__is_private("add", 3));
  ASSERT(!module__is_private("sub_thing", 9));
  ASSERT(!module__is_private("", 0));
  TEST_PASS();
}

static int test_use_private_compile_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Importing a private name should produce a parse error propagated to compile */
  CompileResult cr = compile_source("use \"mod.jacl\" {_helper}", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-006 (M14): Module compilation — one chunk per module with export population ===== */

/* Helper: write a string to a temp file, return the path */
static const char* write_temp_jacl(const char* dir, const char* filename,
                                    const char* content) {
  static char path[1024];
  snprintf(path, sizeof(path), "%s/%s", dir, filename);
  FILE* f = fopen(path, "w");
  if (!f) return NULL;
  fputs(content, f);
  fclose(f);
  return path;
}

/* Test: module__populate_exports builds correct export list */
static int test_module_populate_exports(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Compile a source with def, proc, mut, and private names */
  const char* source = "def x 42\n"
                       "proc add {a, b} { + $a $b }\n"
                       "mut cnt 0\n"
                       "def _priv 99\n";
  LexResult tokens = lexer_lex(source, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count, &heap, &intern_table);

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  Compiler c;
  compiler__init(&c, &chunk, &arena, &intern_table, &heap);
  c.suspension_map = &suspension_map;

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&c, parse.nodes[i]);
  }

  /* Create a module and populate exports */
  Module mod;
  mod.exports = NULL;
  mod.export_count = 0;
  module__populate_exports(&mod, &c);

  /* Should have 3 exports: x, add, cnt (not _priv) */
  ASSERT_U32_EQ(mod.export_count, 3);

  /* Find each export by name */
  int found_x = 0, found_add = 0, found_cnt = 0;
  for (uint32_t i = 0; i < mod.export_count; i++) {
    if (strcmp(mod.exports[i].name, "x") == 0) {
      found_x = 1;
      ASSERT_INT_EQ(mod.exports[i].arity, -1); /* def, not a proc */
      ASSERT(!mod.exports[i].is_mutable);
    } else if (strcmp(mod.exports[i].name, "add") == 0) {
      found_add = 1;
      ASSERT_INT_EQ(mod.exports[i].arity, 2);
      ASSERT(!mod.exports[i].is_mutable);
    } else if (strcmp(mod.exports[i].name, "cnt") == 0) {
      found_cnt = 1;
      ASSERT_INT_EQ(mod.exports[i].arity, -1);
      ASSERT(mod.exports[i].is_mutable);
    }
  }
  ASSERT(found_x);
  ASSERT(found_add);
  ASSERT(found_cnt);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: compiler__compile_module reads file, compiles, caches, and populates exports */
static int test_compile_module_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Create a temp module file */
  const char* dir = "/tmp/jacl_test_m14";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl", "def x 42\nproc add {a, b} { + $a $b }\n");

  /* Create an importer module path */
  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  /* Write a dummy main so realpath works */
  write_temp_jacl(dir, "main.jacl", "");

  char real_importer[PATH_MAX];
  realpath(importer_path, real_importer);

  /* Resolve lib.jacl canonical path */
  const char* lib_canonical = module__resolve_path(real_importer, "lib.jacl", &arena);
  ASSERT(lib_canonical != NULL);

  /* Set up module cache and import stack */
  ModuleCache cache;
  module_cache__init(&cache, &arena);
  ImportStack import_stack;
  import_stack__init(&import_stack);
  import_stack__push(&import_stack, real_importer);

  /* Set up a fake importer compiler */
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  Compiler importer;
  compiler__init(&importer, &chunk, &arena, &intern_table, &heap);
  importer.module_cache = &cache;
  importer.import_stack = &import_stack;

  Module importer_mod;
  importer_mod.path = real_importer;
  importer_mod.chunk = NULL;
  importer_mod.source = NULL;
  importer_mod.exports = NULL;
  importer_mod.export_count = 0;
  importer_mod.compiled = false;
  importer.current_module = &importer_mod;

  /* Compile the lib module */
  bool ok = compiler__compile_module(lib_canonical, &importer, 1, 1);
  ASSERT(ok);
  ASSERT_U32_EQ(importer.error_count, 0);

  /* Verify module is in cache */
  Module* lib_mod = module_cache__find(&cache, lib_canonical);
  ASSERT(lib_mod != NULL);
  ASSERT(lib_mod->compiled);
  ASSERT(lib_mod->chunk != NULL);

  /* Verify exports */
  ASSERT_U32_EQ(lib_mod->export_count, 2);

  /* Verify OP_HALT at end of module chunk */
  ASSERT(lib_mod->chunk->code_count > 0);
  ASSERT_INT_EQ(lib_mod->chunk->code[lib_mod->chunk->code_count - 1], OP_HALT);

  /* Clean up temp files */
  unlink("/tmp/jacl_test_m14/lib.jacl");
  unlink("/tmp/jacl_test_m14/main.jacl");
  rmdir("/tmp/jacl_test_m14");

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mutable exports have is_mutable flag set */
static int test_compile_module_mutable_export(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  const char* dir = "/tmp/jacl_test_m14b";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "state.jacl", "mut count 0\ndef name \"bob\"\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[PATH_MAX];
  realpath(importer_path, real_importer);

  const char* state_canonical = module__resolve_path(real_importer, "state.jacl", &arena);
  ASSERT(state_canonical != NULL);

  ModuleCache cache;
  module_cache__init(&cache, &arena);
  ImportStack import_stack;
  import_stack__init(&import_stack);
  import_stack__push(&import_stack, real_importer);

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  Compiler importer;
  compiler__init(&importer, &chunk, &arena, &intern_table, &heap);
  importer.module_cache = &cache;
  importer.import_stack = &import_stack;

  Module importer_mod;
  importer_mod.path = real_importer;
  importer_mod.chunk = NULL;
  importer_mod.source = NULL;
  importer_mod.exports = NULL;
  importer_mod.export_count = 0;
  importer_mod.compiled = false;
  importer.current_module = &importer_mod;

  bool ok = compiler__compile_module(state_canonical, &importer, 1, 1);
  ASSERT(ok);

  Module* state_mod = module_cache__find(&cache, state_canonical);
  ASSERT(state_mod != NULL);
  ASSERT_U32_EQ(state_mod->export_count, 2);

  /* Find the mutable export */
  int found_count = 0, found_name = 0;
  for (uint32_t i = 0; i < state_mod->export_count; i++) {
    if (strcmp(state_mod->exports[i].name, "count") == 0) {
      found_count = 1;
      ASSERT(state_mod->exports[i].is_mutable);
    } else if (strcmp(state_mod->exports[i].name, "name") == 0) {
      found_name = 1;
      ASSERT(!state_mod->exports[i].is_mutable);
    }
  }
  ASSERT(found_count);
  ASSERT(found_name);

  unlink("/tmp/jacl_test_m14b/state.jacl");
  unlink("/tmp/jacl_test_m14b/main.jacl");
  rmdir("/tmp/jacl_test_m14b");

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: private names excluded from module exports */
static int test_compile_module_private_excluded(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  const char* dir = "/tmp/jacl_test_m14c";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "priv.jacl",
                  "def pub 1\ndef _secret 2\nproc _help {x} { $x }\nproc greet {x} { $x }\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[PATH_MAX];
  realpath(importer_path, real_importer);

  const char* priv_canonical = module__resolve_path(real_importer, "priv.jacl", &arena);
  ASSERT(priv_canonical != NULL);

  ModuleCache cache;
  module_cache__init(&cache, &arena);
  ImportStack import_stack;
  import_stack__init(&import_stack);
  import_stack__push(&import_stack, real_importer);

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  Compiler importer;
  compiler__init(&importer, &chunk, &arena, &intern_table, &heap);
  importer.module_cache = &cache;
  importer.import_stack = &import_stack;

  Module importer_mod;
  importer_mod.path = real_importer;
  importer_mod.chunk = NULL;
  importer_mod.source = NULL;
  importer_mod.exports = NULL;
  importer_mod.export_count = 0;
  importer_mod.compiled = false;
  importer.current_module = &importer_mod;

  bool ok = compiler__compile_module(priv_canonical, &importer, 1, 1);
  ASSERT(ok);

  Module* priv_mod = module_cache__find(&cache, priv_canonical);
  ASSERT(priv_mod != NULL);
  /* Only pub and greet should be exported (not _secret, _help) */
  ASSERT_U32_EQ(priv_mod->export_count, 2);

  for (uint32_t i = 0; i < priv_mod->export_count; i++) {
    ASSERT(priv_mod->exports[i].name[0] != '_');
  }

  unlink("/tmp/jacl_test_m14c/priv.jacl");
  unlink("/tmp/jacl_test_m14c/main.jacl");
  rmdir("/tmp/jacl_test_m14c");

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: cached module is not recompiled */
static int test_compile_module_cached(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  const char* dir = "/tmp/jacl_test_m14d";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl", "def x 42\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[PATH_MAX];
  realpath(importer_path, real_importer);

  const char* lib_canonical = module__resolve_path(real_importer, "lib.jacl", &arena);

  ModuleCache cache;
  module_cache__init(&cache, &arena);
  ImportStack import_stack;
  import_stack__init(&import_stack);
  import_stack__push(&import_stack, real_importer);

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  Compiler importer;
  compiler__init(&importer, &chunk, &arena, &intern_table, &heap);
  importer.module_cache = &cache;
  importer.import_stack = &import_stack;

  Module importer_mod;
  importer_mod.path = real_importer;
  importer_mod.chunk = NULL;
  importer_mod.source = NULL;
  importer_mod.exports = NULL;
  importer_mod.export_count = 0;
  importer_mod.compiled = false;
  importer.current_module = &importer_mod;

  /* Compile once */
  bool ok = compiler__compile_module(lib_canonical, &importer, 1, 1);
  ASSERT(ok);
  ASSERT_U32_EQ(cache.count, 1);

  /* Module is already cached — cache lookup should find it */
  Module* cached = module_cache__find(&cache, lib_canonical);
  ASSERT(cached != NULL);
  ASSERT(cached->compiled);

  /* Cache count should still be 1 (not recompiled) */
  ASSERT_U32_EQ(cache.count, 1);

  unlink("/tmp/jacl_test_m14d/lib.jacl");
  unlink("/tmp/jacl_test_m14d/main.jacl");
  rmdir("/tmp/jacl_test_m14d");

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: typed proc exports carry full type info */
static int test_compile_module_typed_exports(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  const char* dir = "/tmp/jacl_test_m14e";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "math.jacl",
                  "proc add {i64 a, i64 b} i64 { + $a $b }\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[PATH_MAX];
  realpath(importer_path, real_importer);

  const char* math_canonical = module__resolve_path(real_importer, "math.jacl", &arena);

  ModuleCache cache;
  module_cache__init(&cache, &arena);
  ImportStack import_stack;
  import_stack__init(&import_stack);
  import_stack__push(&import_stack, real_importer);

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  Compiler importer;
  compiler__init(&importer, &chunk, &arena, &intern_table, &heap);
  importer.module_cache = &cache;
  importer.import_stack = &import_stack;

  Module importer_mod;
  importer_mod.path = real_importer;
  importer_mod.chunk = NULL;
  importer_mod.source = NULL;
  importer_mod.exports = NULL;
  importer_mod.export_count = 0;
  importer_mod.compiled = false;
  importer.current_module = &importer_mod;

  bool ok = compiler__compile_module(math_canonical, &importer, 1, 1);
  ASSERT(ok);

  Module* math_mod = module_cache__find(&cache, math_canonical);
  ASSERT(math_mod != NULL);
  ASSERT_U32_EQ(math_mod->export_count, 1);

  ExportEntry* e = &math_mod->exports[0];
  ASSERT_STR_EQ(e->name, "add");
  ASSERT_INT_EQ(e->arity, 2);
  ASSERT_INT_EQ((int)e->return_type, (int)TYPE_I64);
  ASSERT_INT_EQ((int)e->param_types[0], (int)TYPE_I64);
  ASSERT_INT_EQ((int)e->param_types[1], (int)TYPE_I64);
  ASSERT_U32_EQ(e->param_count, 2);

  unlink("/tmp/jacl_test_m14e/math.jacl");
  unlink("/tmp/jacl_test_m14e/main.jacl");
  rmdir("/tmp/jacl_test_m14e");

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-007 (M14): Import name resolution with strict typing ===== */

/* Helper: set up a module compilation context with temp files.
   Creates dir, writes lib file and dummy main, sets up cache/stack/compiler. */
static void setup_module_ctx(const char* dir, const char* lib_name,
                              const char* lib_src, arena_t* arena,
                              BlockPool* pool, ThreadHeap* heap,
                              ModuleCache* cache, ImportStack* istack,
                              BytecodeChunk* chunk, JaclInternTable* intern,
                              Compiler* importer, Module* importer_mod,
                              char* real_importer, size_t ri_size) {
  mkdir(dir, 0755);
  write_temp_jacl(dir, lib_name, lib_src);
  write_temp_jacl(dir, "main.jacl", "");
  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  realpath(importer_path, real_importer);

  gc_block_pool_init(pool);
  gc_heap_init(heap, pool);
  module_cache__init(cache, arena);
  import_stack__init(istack);
  import_stack__push(istack, real_importer);
  chunk_init(chunk, arena);
  intern_table_init(intern, arena);
  compiler__init(importer, chunk, arena, intern, heap);
  importer->module_cache = cache;
  importer->import_stack = istack;

  importer_mod->path = real_importer;
  importer_mod->chunk = NULL;
  importer_mod->source = NULL;
  importer_mod->exports = NULL;
  importer_mod->export_count = 0;
  importer_mod->compiled = false;
  importer->current_module = importer_mod;
}

/* Test: use "./lib.jacl" {add x} registers GlobalArity entries for imported names */
static int test_import_registers_globals(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us007a", "lib.jacl",
                   "def x 42\nproc add {a, b} { + $a $b }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Compile: use "./lib.jacl" {add x} */
  const char* src = "use \"lib.jacl\" {add x}\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  /* Compile the use declaration */
  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  /* Check that 'add' is registered with arity 2 */
  JaclVal add_name = jacl_inline_string("add", 3);
  GlobalArity* add_ga = compiler__find_global(&importer, add_name);
  int16_t add_arity = add_ga ? add_ga->known_arity : -1;
  ASSERT_INT_EQ((int)add_arity, 2);

  /* Check that 'x' is registered with arity -1 (non-proc) */
  JaclVal x_name = jacl_inline_string("x", 1);
  GlobalArity* x_ga = compiler__find_global(&importer, x_name);
  int16_t x_arity = x_ga ? x_ga->known_arity : -1;
  ASSERT_INT_EQ((int)x_arity, -1);

  unlink("/tmp/jacl_us007a/lib.jacl");
  unlink("/tmp/jacl_us007a/main.jacl");
  rmdir("/tmp/jacl_us007a");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: importing a name not in exports produces compile error */
static int test_import_unknown_export_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us007b", "lib.jacl",
                   "def x 42\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Try to import 'foo' which doesn't exist */
  const char* src = "use \"lib.jacl\" {foo}\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT(importer.error_count > 0);
  ASSERT(strstr(importer.first_error, "is not exported by") != NULL);

  unlink("/tmp/jacl_us007b/lib.jacl");
  unlink("/tmp/jacl_us007b/main.jacl");
  rmdir("/tmp/jacl_us007b");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: importing a name that conflicts with existing definition produces error */
static int test_import_conflict_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us007c", "lib.jacl",
                   "def x 42\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Pre-define 'x' in the importer */
  JaclVal x_name = jacl_inline_string("x", 1);
  compiler__set_global_arity(&importer, x_name, -1);

  /* Try to import 'x' — should conflict */
  const char* src = "use \"lib.jacl\" {x}\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT(importer.error_count > 0);
  ASSERT(strstr(importer.first_error, "is already defined") != NULL);

  unlink("/tmp/jacl_us007c/lib.jacl");
  unlink("/tmp/jacl_us007c/main.jacl");
  rmdir("/tmp/jacl_us007c");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: typed exports propagate full type info to importer GlobalArity */
static int test_import_typed_propagation(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us007d", "math.jacl",
                   "proc add {i64 a, i64 b} i64 { + $a $b }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"math.jacl\" {add}\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  /* Check full type info propagated */
  JaclVal add_name = jacl_inline_string("add", 3);
  GlobalArity* ga = compiler__find_global(&importer, add_name);
  ASSERT(ga != NULL);
  ASSERT_INT_EQ((int)ga->known_arity, 2);
  ASSERT_INT_EQ((int)ga->return_type, (int)TYPE_I64);
  ASSERT_INT_EQ((int)ga->param_types[0], (int)TYPE_I64);
  ASSERT_INT_EQ((int)ga->param_types[1], (int)TYPE_I64);

  unlink("/tmp/jacl_us007d/math.jacl");
  unlink("/tmp/jacl_us007d/main.jacl");
  rmdir("/tmp/jacl_us007d");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: arity checking works on imported procs — wrong arg count produces error */
static int test_import_arity_check(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us007e", "lib.jacl",
                   "proc add {a, b} { + $a $b }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Import add then call with wrong arity */
  const char* src = "use \"lib.jacl\" {add}\n[add 1]\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT(importer.error_count > 0);
  ASSERT(strstr(importer.first_error, "expects 2 arguments but got 1") != NULL);

  unlink("/tmp/jacl_us007e/lib.jacl");
  unlink("/tmp/jacl_us007e/main.jacl");
  rmdir("/tmp/jacl_us007e");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: type checking works on imported typed procs — wrong arg type produces error */
static int test_import_type_check(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us007f", "math.jacl",
                   "proc add {i64 a, i64 b} i64 { + $a $b }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Import add then call with wrong type (string instead of i64) */
  const char* src = "use \"math.jacl\" {add}\n[add \"hi\" 1]\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT(importer.error_count > 0);
  ASSERT(strstr(importer.first_error, "type error") != NULL);

  unlink("/tmp/jacl_us007f/math.jacl");
  unlink("/tmp/jacl_us007f/main.jacl");
  rmdir("/tmp/jacl_us007f");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mutable export registers as mutable in importer GlobalArity */
static int test_import_mutable_flag(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us007g", "state.jacl",
                   "mut count 0\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"state.jacl\" {count}\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  JaclVal count_name = jacl_inline_string("count", 5);
  GlobalArity* ga = compiler__find_global(&importer, count_name);
  ASSERT(ga != NULL);
  ASSERT(ga->is_mutable);

  unlink("/tmp/jacl_us007g/state.jacl");
  unlink("/tmp/jacl_us007g/main.jacl");
  rmdir("/tmp/jacl_us007g");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-008 (M14): Mutable imports as boxes */

/* Test: module-context mut emits OP_BOX before OP_DEF_GLOBAL */
static int test_module_mut_emits_box(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us008a", "state.jacl",
                   "mut count 0\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Compile the dependency module */
  const char* canonical = module__resolve_path(real_importer, "state.jacl", &arena);
  ASSERT(canonical != NULL);
  bool ok = compiler__compile_module(canonical, &importer, 1, 1);
  ASSERT(ok);

  Module* state_mod = module_cache__find(&cache, canonical);
  ASSERT(state_mod != NULL);

  /* Check bytecode contains OP_BOX before OP_DEF_GLOBAL */
  BytecodeChunk* mc = state_mod->chunk;
  bool found_box = false;
  for (uint32_t i = 0; i < mc->code_count; i++) {
    if (mc->code[i] == OP_BOX) {
      /* Next meaningful opcode should be OP_DEF_GLOBAL */
      ASSERT(i + 1 < mc->code_count);
      ASSERT(mc->code[i + 1] == OP_DEF_GLOBAL);
      found_box = true;
      break;
    }
  }
  ASSERT(found_box);

  unlink("/tmp/jacl_us008a/state.jacl");
  unlink("/tmp/jacl_us008a/main.jacl");
  rmdir("/tmp/jacl_us008a");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: non-module mut does NOT emit OP_BOX (backwards compat) */
static int test_nonmodule_mut_no_box(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  JaclInternTable intern;
  intern_table_init(&intern, &arena);
  Compiler c;
  compiler__init(&c, &chunk, &arena, &intern, &heap);
  /* No current_module — single-file mode */

  const char* src = "mut x 42\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&c, parse.nodes[i]);
  }
  ASSERT_U32_EQ(c.error_count, 0);

  /* Should NOT contain OP_BOX */
  for (uint32_t i = 0; i < chunk.code_count; i++) {
    ASSERT(chunk.code[i] != OP_BOX);
  }

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mutable export is_mutable flag propagates to importer */
static int test_module_mutable_import_is_box(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us008b", "state.jacl",
                   "mut count 0\ndef name \"bob\"\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"state.jacl\" {count name}\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  /* count should be mutable (box), name should not */
  JaclVal count_name = jacl_inline_string("count", 5);
  GlobalArity* ga_count = compiler__find_global(&importer, count_name);
  ASSERT(ga_count != NULL);
  ASSERT(ga_count->is_mutable);

  JaclVal name_name = jacl_inline_string("name", 4);
  GlobalArity* ga_name = compiler__find_global(&importer, name_name);
  ASSERT(ga_name != NULL);
  ASSERT(!ga_name->is_mutable);

  unlink("/tmp/jacl_us008b/state.jacl");
  unlink("/tmp/jacl_us008b/main.jacl");
  rmdir("/tmp/jacl_us008b");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set! on module mutable global emits GET_GLOBAL + OP_RESET */
static int test_module_set_emits_reset(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us008c", "state.jacl",
                   "mut count 0\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Compile use + set! */
  const char* src = "use \"state.jacl\" {count}\ncount :: 5\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  /* Check bytecode contains OP_GET_GLOBAL followed eventually by OP_RESET
     (not OP_SET_GLOBAL) for the set! statement */
  bool found_get_global = false;
  bool found_reset = false;
  bool found_set_global = false;
  for (uint32_t i = 0; i < chunk.code_count; i++) {
    if (chunk.code[i] == OP_GET_GLOBAL) found_get_global = true;
    if (chunk.code[i] == OP_RESET) found_reset = true;
    if (chunk.code[i] == OP_SET_GLOBAL) found_set_global = true;
  }
  ASSERT(found_get_global);
  ASSERT(found_reset);
  ASSERT(!found_set_global);

  unlink("/tmp/jacl_us008c/state.jacl");
  unlink("/tmp/jacl_us008c/main.jacl");
  rmdir("/tmp/jacl_us008c");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: module set! on immutable binding still errors */
static int test_module_set_immutable_errors(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[PATH_MAX];

  setup_module_ctx("/tmp/jacl_us008d", "lib.jacl",
                   "def x 42\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"lib.jacl\" {x}\nx :: 99\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  /* Should have compile error — x is immutable */
  ASSERT(importer.error_count > 0);

  unlink("/tmp/jacl_us008d/lib.jacl");
  unlink("/tmp/jacl_us008d/main.jacl");
  rmdir("/tmp/jacl_us008d");
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-009 (M14): Compiler API — multi-file entry point ===== */

/* Helper: recursively remove a directory of .jacl files */
static void cleanup_jacl_dir(const char* dir, const char** files, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, files[i]);
    unlink(path);
  }
  rmdir(dir);
}

/* Test: single-file program (no imports) compiles successfully */
static int test_compile_program_single_file(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009a";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "main.jacl", "def x 42\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us009a/main.jacl", &arena, &intern, &heap);

  ASSERT_U32_EQ(pr.error_count, 0);
  ASSERT_U32_EQ(pr.module_count, 1);
  ASSERT(pr.modules != NULL);
  ASSERT(pr.modules[0] != NULL);
  ASSERT(pr.modules[0]->compiled);
  ASSERT(pr.modules[0]->chunk != NULL);
  /* Root is last (and only) */
  ASSERT(pr.suspending == false);

  const char* files[] = { "main.jacl" };
  cleanup_jacl_dir(dir, files, 1);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multi-file program — root imports a library module */
static int test_compile_program_with_dep(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009b";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl", "def x 42\nproc add {a, b} { + $a $b }\n");
  write_temp_jacl(dir, "main.jacl", "use \"lib.jacl\" {add x}\n[add $x 1]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us009b/main.jacl", &arena, &intern, &heap);

  ASSERT_U32_EQ(pr.error_count, 0);
  ASSERT_U32_EQ(pr.module_count, 2);
  ASSERT(pr.modules != NULL);
  /* Dependencies first, root last */
  /* modules[0] should be lib.jacl (dependency) */
  ASSERT(pr.modules[0]->compiled);
  ASSERT(pr.modules[0]->export_count >= 2); /* add and x */
  /* modules[1] should be root (main.jacl) */
  ASSERT(pr.modules[1]->compiled);

  const char* files[] = { "lib.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: topological order — root is always last */
static int test_compile_program_topo_order(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009c";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "a.jacl", "def val_a 1\n");
  write_temp_jacl(dir, "b.jacl", "use \"a.jacl\" {val_a}\ndef val_b 2\n");
  write_temp_jacl(dir, "main.jacl", "use \"b.jacl\" {val_b}\n[+ $val_b 10]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us009c/main.jacl", &arena, &intern, &heap);

  ASSERT_U32_EQ(pr.error_count, 0);
  ASSERT_U32_EQ(pr.module_count, 3);
  /* Root (main.jacl) must be last */
  char main_resolved[PATH_MAX];
  realpath("/tmp/jacl_us009c/main.jacl", main_resolved);
  ASSERT_STR_EQ(pr.modules[2]->path, main_resolved);

  const char* files[] = { "a.jacl", "b.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 3);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: file not found produces error */
static int test_compile_program_not_found(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us009_nonexistent.jacl", &arena, &intern, &heap);

  ASSERT(pr.error_count > 0);
  ASSERT(pr.error_message != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: existing compiler_compile still works for single-file */
static int test_single_file_api_unchanged(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("def x 42\n[+ $x 1]\n", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.chunk.code_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: program with exports populates root module exports */
static int test_compile_program_root_exports(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009e";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "main.jacl", "def x 42\nproc add {a, b} { + $a $b }\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us009e/main.jacl", &arena, &intern, &heap);

  ASSERT_U32_EQ(pr.error_count, 0);
  /* Root module should have exports populated */
  Module* root = pr.modules[pr.module_count - 1];
  ASSERT(root->export_count >= 2); /* x and add */

  const char* files[] = { "main.jacl" };
  cleanup_jacl_dir(dir, files, 1);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-010 (M14): Module initialization and runtime execution ===== */

typedef struct {
  char buf[4096];
  uint32_t len;
} US010PrintCapture;

static void us010_capture_print(const char* text, uint32_t len, void* ctx) {
  US010PrintCapture* cap = (US010PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

/* Test: basic module execution — import proc from dep, call it */
static int test_exec_program_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010a";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl",
    "proc add {a, b} { + $a $b }\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"lib.jacl\" {add}\n[print [add 1 2]]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us010a/main.jacl", &arena, &intern, &heap);
  ASSERT_U32_EQ(pr.error_count, 0);

  US010PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = us010_capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &intern;

  VMResult r = jacl_exec_program(&pr, &vm);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  const char* files[] = { "lib.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: namespace-prefixed globals — module globals don't collide */
static int test_exec_program_namespace(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010b";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "a.jacl", "def x 10\n");
  write_temp_jacl(dir, "b.jacl", "def x 20\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"a.jacl\" {x}\n[print $x]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us010b/main.jacl", &arena, &intern, &heap);
  ASSERT_U32_EQ(pr.error_count, 0);

  US010PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = us010_capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &intern;

  VMResult r = jacl_exec_program(&pr, &vm);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n");

  vm_destroy(&vm);
  const char* files[] = { "a.jacl", "b.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 3);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: side effects run once — module print on load, imported by root */
static int test_exec_program_side_effects_once(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010c";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl",
    "[print \"loaded\"]\ndef x 42\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"lib.jacl\" {x}\n[print $x]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us010c/main.jacl", &arena, &intern, &heap);
  ASSERT_U32_EQ(pr.error_count, 0);

  US010PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = us010_capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &intern;

  VMResult r = jacl_exec_program(&pr, &vm);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "loaded\n42\n");

  vm_destroy(&vm);
  const char* files[] = { "lib.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: error in dependency module propagates with context */
static int test_exec_program_dep_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010d";
  mkdir(dir, 0755);
  /* lib.jacl accesses undefined variable, causing runtime error */
  write_temp_jacl(dir, "lib.jacl",
    "def x [+ $undef 1]\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"lib.jacl\" {x}\n[print $x]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us010d/main.jacl", &arena, &intern, &heap);
  ASSERT_U32_EQ(pr.error_count, 0);

  VM vm;
  vm_init(&vm, &arena);
  vm.intern_table = &intern;

  VMResult r = jacl_exec_program(&pr, &vm);
  ASSERT_INT_EQ(r, VM_RUNTIME_ERROR);
  /* Error message should contain module context */
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "lib.jacl") != NULL);

  vm_destroy(&vm);
  const char* files[] = { "lib.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mutable import as box — deref and reset work */
static int test_exec_program_mutable_import(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010e";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "state.jacl",
    "mut cnt 0\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"state.jacl\" {cnt}\n"
    "[print [deref $cnt]]\n"
    "[reset $cnt 5]\n"
    "[print [deref $cnt]]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us010e/main.jacl", &arena, &intern, &heap);
  ASSERT_U32_EQ(pr.error_count, 0);

  US010PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = us010_capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &intern;

  VMResult r = jacl_exec_program(&pr, &vm);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n5\n");

  vm_destroy(&vm);
  const char* files[] = { "state.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: existing single-file vm_exec works unchanged */
static int test_exec_single_file_unchanged(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  CompileResult cr = compile_source("[print [+ 1 2]]", &arena, NULL);
  ASSERT_U32_EQ(cr.error_count, 0);

  US010PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = us010_capture_print;
  vm.print_ctx = &cap;

  VMResult r = vm_exec(&vm, &cr.chunk);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-002 (Struct): Struct type registration ===== */

/* Test: basic struct compiles without errors */
static int test_defstruct_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: duplicate struct name produces error */
static int test_defstruct_duplicate_name(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "struct Point {f32 a, f32 b}", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: duplicate field name in struct produces error */
static int test_defstruct_duplicate_field(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Bad {i32 x, i32 x}", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: field referencing undefined struct type produces error */
static int test_defstruct_undefined_field_type(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Line {Point start, Point end}", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: struct field referencing previously-defined struct type works */
static int test_defstruct_nested_type(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "struct Line {Point start, Point end}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: struct name works as type annotation in def */
static int test_defstruct_type_annotation(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* def Point p [Point x 0 y 0] — struct name as type annotation.
     Verifies that 'Point' is accepted as a type keyword after struct.
     Wrapped in a proc since struct values cannot live at top level. */
  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} { def Point p [Point x 0 y 0] }",
      &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: all basic field types compile without error */
static int test_defstruct_all_field_types(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct All {i32 a, i64 b, u32 c, u64 d, f32 e, f64 f, bool g}",
      &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Reference-type fields are rejected — structs hold value-type bytes only.
   Use [box $val] to reference data through a struct. */
static int test_defstruct_ref_type_rejected(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* str field is rejected */
  CompileResult cr1 = compile_source(
      "struct WithStr {str name, i32 val}", &arena, &heap);
  ASSERT(cr1.error_count > 0);

  /* vec field is rejected */
  CompileResult cr2 = compile_source(
      "struct WithVec {i32 x, vec items}", &arena, &heap);
  ASSERT(cr2.error_count > 0);

  /* map field is rejected */
  CompileResult cr3 = compile_source(
      "struct WithMap {map data}", &arena, &heap);
  ASSERT(cr3.error_count > 0);

  /* dyn field is rejected */
  CompileResult cr4 = compile_source(
      "struct WithDyn {dyn val}", &arena, &heap);
  ASSERT(cr4.error_count > 0);

  /* inline anonymous struct with a ref field is rejected */
  CompileResult cr5 = compile_source(
      "struct Outer {struct [i32 x, str name] inner}", &arena, &heap);
  ASSERT(cr5.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: forward reference (using struct before defining it) produces error */
static int test_defstruct_forward_ref_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Line {Point start, Point end}\n"
      "struct Point {i32 x, i32 y}", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-003 (Struct): Struct instantiation */

static int test_struct_new_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} { def p [Point x 1 y 2] }\n"
      "main", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_arity_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "[Point 1]", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_type_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "[Point x 1 y \"hello\"]", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_runtime(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  print $p\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-003 (Value Types): Width tracking */

static int test_struct_width_tracking(void) {
  /* Verify the compiler computes slot width for struct types */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Point {i32, i32} = 8 bytes → width 1 */
  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} { def p [Point x 1 y 2] }\n"
      "main", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  /* struct_registry should exist and have the Point type */
  ASSERT(cr.struct_registry != NULL);
  ASSERT(cr.struct_registry->count >= 3); /* 0=reserved, 1=ctx-reserved, 2=Point */
  StructTypeDef* point_def = cr.struct_registry->defs[2];
  ASSERT(point_def != NULL);
  ASSERT_U32_EQ(point_def->total_size, 8);
  /* width = ceil(8/8) = 1 */
  uint32_t point_width = (point_def->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
  ASSERT_U32_EQ(point_width, 1);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_width_large(void) {
  /* Verify width for larger structs: {f64, f64, f64} = 24 bytes → width 3 */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Vec3 {f64 x, f64 y, f64 z}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.struct_registry != NULL);
  StructTypeDef* vec3_def = cr.struct_registry->defs[2];
  ASSERT(vec3_def != NULL);
  ASSERT_U32_EQ(vec3_def->total_size, 24);
  uint32_t vec3_width = (vec3_def->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
  ASSERT_U32_EQ(vec3_width, 3);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_width_nested(void) {
  /* Nested struct: Line {Point, Point} → 16 bytes → width 2 */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "struct Line {Point start, Point end}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  /* Line is at type_idx 3 (after reserved 0, ctx-reserved 1, Point 2) */
  StructTypeDef* line_def = cr.struct_registry->defs[3];
  ASSERT(line_def != NULL);
  ASSERT_U32_EQ(line_def->total_size, 16);
  uint32_t line_width = (line_def->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
  ASSERT_U32_EQ(line_width, 2);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_inline_opcode(void) {
  /* Test OP_STRUCT_NEW_INLINE writes correct data to stack slots */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* First compile to get a struct registry */
  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Build bytecode: push two i32 args, then OP_STRUCT_NEW_INLINE */
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  /* Push x=10 */
  chunk_add_constant(&chunk, jacl_i32(10));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, 0, 1);
  /* Push y=20 */
  chunk_add_constant(&chunk, jacl_i32(20));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, 1, 1);
  /* OP_STRUCT_NEW_INLINE type_idx=2 (Point — slot 1 reserved for ctx) */
  chunk_write(&chunk, OP_STRUCT_NEW_INLINE, 1);
  chunk_write_u16(&chunk, 2, 1);
  /* Pop inline data and halt */
  chunk_write(&chunk, OP_POP, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  vm.struct_registry = cr.struct_registry;
  VMResult r = vm_exec(&vm, &chunk);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_inline_data_integrity(void) {
  /* Verify inline struct data is laid out correctly on the stack */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Compile to get registry */
  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Build bytecode: push i32 args, OP_STRUCT_NEW_INLINE, then HALT (no pop) */
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  chunk_add_constant(&chunk, jacl_i32(42));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, 0, 1);
  chunk_add_constant(&chunk, jacl_i32(99));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, 1, 1);
  /* OP_STRUCT_NEW_INLINE type_idx=2 (Point — slot 1 reserved for ctx) */
  chunk_write(&chunk, OP_STRUCT_NEW_INLINE, 1);
  chunk_write_u16(&chunk, 2, 1);
  /* HALT with inline data still on stack */
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  vm.struct_registry = cr.struct_registry;
  VMResult r = vm_exec(&vm, &chunk);
  ASSERT(r == VM_OK);

  /* Point {i32, i32} = 8 bytes = 1 slot. Verify data at stack[0]. */
  uint8_t* data = (uint8_t*)&vm.stack[0];
  int32_t x, y;
  memcpy(&x, data + 0, 4);  /* field x at offset 0 */
  memcpy(&y, data + 4, 4);  /* field y at offset 4 */
  ASSERT_INT_EQ(x, 42);
  ASSERT_INT_EQ(y, 99);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_inline_wide(void) {
  /* Test OP_STRUCT_NEW_INLINE for a struct wider than 1 slot */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Vec3 {f64 x, f64 y, f64 z} = 24 bytes = 3 slots */
  CompileResult cr = compile_source(
      "struct Vec3 {f64 x, f64 y, f64 z}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  /* Push three f64 values as raw bits */
  double vals[3] = {1.5, 2.5, 3.5};
  for (int i = 0; i < 3; i++) {
    uint16_t ci = chunk_add_constant(&chunk, 0);
    memcpy(&chunk.constants[ci], &vals[i], sizeof(double));
    chunk_write(&chunk, OP_CONST_F64, 1);
    chunk_write_u16(&chunk, ci, 1);
  }
  /* OP_STRUCT_NEW_INLINE type_idx=2 (Vec3 — slot 1 reserved for ctx) */
  chunk_write(&chunk, OP_STRUCT_NEW_INLINE, 1);
  chunk_write_u16(&chunk, 2, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  vm.struct_registry = cr.struct_registry;
  VMResult r = vm_exec(&vm, &chunk);
  ASSERT(r == VM_OK);

  /* 3 slots on stack. Verify f64 data. */
  ASSERT_U32_EQ(vm.stack_top, 3);
  uint8_t* data = (uint8_t*)&vm.stack[0];
  double x, y, z;
  memcpy(&x, data + 0,  8);
  memcpy(&y, data + 8,  8);
  memcpy(&z, data + 16, 8);
  ASSERT(x == 1.5);
  ASSERT(y == 2.5);
  ASSERT(z == 3.5);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_inline_padding_zeroed(void) {
  /* Verify padding bytes are zeroed for deterministic memcmp */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Struct with padding: {bool a, i32 b} = bool@0(1B) + pad(3B) + i32@4(4B) = 8B */
  CompileResult cr = compile_source(
      "struct Padded {bool a, i32 b}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  StructTypeDef* sdef = cr.struct_registry->defs[2];
  ASSERT_U32_EQ(sdef->total_size, 8);
  /* bool at offset 0, i32 at offset 4 */
  ASSERT_U32_EQ(sdef->fields[0].offset, 0);
  ASSERT_U32_EQ(sdef->fields[1].offset, 4);

  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);
  /* Push bool true */
  chunk_add_constant(&chunk, JACL_TRUE);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, 0, 1);
  /* Push i32 value 42 */
  chunk_add_constant(&chunk, jacl_i32(42));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, 1, 1);
  /* OP_STRUCT_NEW_INLINE — Padded at slot 2 */
  chunk_write(&chunk, OP_STRUCT_NEW_INLINE, 1);
  chunk_write_u16(&chunk, 2, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  vm.struct_registry = cr.struct_registry;
  VMResult r = vm_exec(&vm, &chunk);
  ASSERT(r == VM_OK);

  /* Check padding bytes (offset 1-3) are zero */
  uint8_t* data = (uint8_t*)&vm.stack[0];
  ASSERT_INT_EQ(data[0], 1);  /* bool true */
  ASSERT_INT_EQ(data[1], 0);  /* padding */
  ASSERT_INT_EQ(data[2], 0);  /* padding */
  ASSERT_INT_EQ(data[3], 0);  /* padding */
  int32_t b;
  memcpy(&b, data + 4, 4);
  ASSERT_INT_EQ(b, 42);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_heap_path_unchanged(void) {
  /* Verify existing heap path (OP_HEAP_RECORD_NEW) still works for struct locals */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-004 (Value Types): Wide locals — state machine field storage */

static int test_sm_state_field_width_default(void) {
  /* Non-struct state fields get width=1 and sequential field_index */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  StateLayout layout;
  memset(&layout, 0, sizeof(layout));
  layout.heap = &heap;
  JaclInternTable intern;
  intern_table_init(&intern, &arena);
  layout.intern_table = &intern;

  JaclVal a = jacl_inline_string("a", 1);
  JaclVal b = jacl_inline_string("b", 1);
  JaclVal c = jacl_inline_string("c", 1);
  sm__add_state_field(&layout, a, false, false, 1, 0, 0, 0);
  sm__add_state_field(&layout, b, true,  false, 1, 0, 0, 0);
  sm__add_state_field(&layout, c, false, true,  1, 0, 0, 0);

  ASSERT_U32_EQ(layout.field_count, 3);
  ASSERT_U32_EQ(layout.total_slots, 3);
  ASSERT_U32_EQ(layout.fields[0].field_index, 0);
  ASSERT_U32_EQ(layout.fields[1].field_index, 1);
  ASSERT_U32_EQ(layout.fields[2].field_index, 2);
  ASSERT_U32_EQ(layout.fields[0].width, 1);
  ASSERT_U32_EQ(layout.fields[1].width, 1);
  ASSERT_U32_EQ(layout.fields[2].width, 1);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_sm_state_field_width_struct(void) {
  /* Struct state fields get width=N and field_index accounts for width */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  StateLayout layout;
  memset(&layout, 0, sizeof(layout));
  layout.heap = &heap;
  JaclInternTable intern;
  intern_table_init(&intern, &arena);
  layout.intern_table = &intern;

  JaclVal a = jacl_inline_string("a", 1);
  JaclVal s = jacl_inline_string("s", 1);
  JaclVal c = jacl_inline_string("c", 1);

  /* a: width 1 (scalar) */
  sm__add_state_field(&layout, a, false, false, 1, 0, 0, 0);
  /* s: width 3 (struct like Vec3 {f64 x, f64 y, f64 z}) */
  sm__add_state_field(&layout, s, false, false, 3, 1, 0, 0);
  /* c: width 1 (scalar) */
  sm__add_state_field(&layout, c, false, false, 1, 0, 0, 0);

  ASSERT_U32_EQ(layout.field_count, 3);
  ASSERT_U32_EQ(layout.total_slots, 5);  /* 1 + 3 + 1 */
  /* a at slot 0 */
  ASSERT_U32_EQ(layout.fields[0].field_index, 0);
  ASSERT_U32_EQ(layout.fields[0].width, 1);
  /* s at slot 1, occupies slots 1-3 */
  ASSERT_U32_EQ(layout.fields[1].field_index, 1);
  ASSERT_U32_EQ(layout.fields[1].width, 3);
  ASSERT_U32_EQ(layout.fields[1].struct_type_idx, 1);
  /* c at slot 4 */
  ASSERT_U32_EQ(layout.fields[2].field_index, 4);
  ASSERT_U32_EQ(layout.fields[2].width, 1);

  /* sm__find_field returns base slot index */
  ASSERT_INT_EQ(sm__find_field(&layout, a), 0);
  ASSERT_INT_EQ(sm__find_field(&layout, s), 1);
  ASSERT_INT_EQ(sm__find_field(&layout, c), 4);
  /* sm__get_field exposes the full StateField (incl. width) */
  ASSERT_U32_EQ(sm__get_field(&layout, s)->width, 3);
  ASSERT_U32_EQ(sm__get_field(&layout, a)->width, 1);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_sm_walk_locals_struct_width(void) {
  /* sm__walk_locals computes width for struct-typed locals */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Compile just to register the struct type in the registry */
  CompileResult cr = compile_source(
      "struct Vec3 {f64 x, f64 y, f64 z}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  /* Now parse a proc body that declares a Vec3 local */
  LexResult tokens = lexer_lex(
      "proc gen {} {\n"
      "  def Vec3 v [Vec3 x 1.0 y 2.0 z 3.0]\n"
      "  yield $v\n"
      "}", &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  JaclInternTable intern;
  intern_table_init(&intern, &arena);

  /* Analyze suspensions — this runs sm__walk_locals with struct registry */
  AstNode* proc_node = parse.nodes[0];
  ASSERT(proc_node->type == AST_COMMAND);
  /* proc gen {} { body } → args[0]=name, args[1]=params, args[2]=body */
  AstNode* body = proc_node->data.command.args[2];

  SuspensionMap map;
  memset(&map, 0, sizeof(map));
  SuspensionAnalysis analysis = compiler__analyze_suspensions(
      NULL, body, NULL, 0, false, &map, &heap, &intern, cr.struct_registry);

  /* Should have fields: v (width 3 for Vec3 = 24 bytes / 8 = 3 slots) */
  JaclVal v_name = jacl_inline_string("v", 1);
  const StateField* v_sf = sm__get_field(&analysis.state_layout, v_name);
  ASSERT(v_sf != NULL);
  ASSERT_U32_EQ(v_sf->width, 3);
  ASSERT_U32_EQ(analysis.state_layout.total_slots >= 3, 1);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_sm_optimize_atomic_wide_field(void) {
  /* Liveness optimization treats multi-slot fields as atomic units */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  StateLayout layout;
  memset(&layout, 0, sizeof(layout));
  layout.heap = &heap;
  JaclInternTable intern;
  intern_table_init(&intern, &arena);
  layout.intern_table = &intern;

  /* Add param (always kept), wide struct, and scalar */
  JaclVal p = jacl_inline_string("p", 1);
  JaclVal s = jacl_inline_string("s", 1);
  JaclVal t = jacl_inline_string("t", 1);
  sm__add_state_field(&layout, p, false, true,  1, 0, 0, 0);  /* param, width 1 */
  sm__add_state_field(&layout, s, false, false, 3, 1, 0, 0);  /* struct, width 3 */
  sm__add_state_field(&layout, t, false, false, 1, 0, 0, 0);  /* scalar, width 1 */

  ASSERT_U32_EQ(layout.total_slots, 5);

  /* Simulate compaction: remove t, keep p and s */
  StateField new_fields[3];
  uint32_t new_count = 0;
  uint32_t new_total = 0;
  for (uint32_t i = 0; i < layout.field_count; i++) {
    if (layout.fields[i].name != t) {
      new_fields[new_count] = layout.fields[i];
      new_fields[new_count].field_index = new_total;
      new_total += layout.fields[i].width;
      new_count++;
    }
  }
  memcpy(layout.fields, new_fields, sizeof(StateField) * new_count);
  layout.field_count = new_count;
  layout.total_slots = new_total;

  ASSERT_U32_EQ(layout.field_count, 2);
  ASSERT_U32_EQ(layout.total_slots, 4);  /* 1 + 3 */
  ASSERT_U32_EQ(layout.fields[0].field_index, 0);
  ASSERT_U32_EQ(layout.fields[0].width, 1);
  ASSERT_U32_EQ(layout.fields[1].field_index, 1);
  ASSERT_U32_EQ(layout.fields[1].width, 3);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_sm_wide_field_opcodes(void) {
  /* Test OP_SET_STATE_FIELD_WIDE and OP_GET_STATE_FIELD_WIDE */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Allocate a state machine with 5 slots */
  JaclVal sm_val = gc_alloc_state_machine(&heap, 5);
  JaclStateMachine* sm = jacl_as_state_machine(sm_val);

  /* Build bytecode:
     1. Push SM onto stack (frame slot 0)
     2. Push 3 raw values (simulating inline struct data)
     3. OP_SET_STATE_FIELD_WIDE base=1 width=3 (store into SM fields[1..3])
     4. OP_GET_STATE_FIELD_WIDE base=1 width=3 (load back onto stack)
     5. HALT */
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Push SM value as constant (slot 0 of frame) */
  uint16_t sm_ci = chunk_add_constant(&chunk, sm_val);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, sm_ci, 1);

  /* Push 3 raw slot values */
  uint16_t c0 = chunk_add_constant(&chunk, jacl_i32(111));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c0, 1);
  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(222));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(333));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);

  /* OP_SET_STATE_FIELD_WIDE base_idx=1, width=3 */
  chunk_write(&chunk, OP_SET_STATE_FIELD_WIDE, 1);
  chunk_write(&chunk, 1, 1);  /* base_idx */
  chunk_write(&chunk, 3, 1);  /* width */

  /* OP_GET_STATE_FIELD_WIDE base_idx=1, width=3 */
  chunk_write(&chunk, OP_GET_STATE_FIELD_WIDE, 1);
  chunk_write(&chunk, 1, 1);  /* base_idx */
  chunk_write(&chunk, 3, 1);  /* width */

  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = vm_exec(&vm, &chunk);
  ASSERT(r == VM_OK);

  /* Verify SM fields were written correctly */
  ASSERT(sm->fields[1] == jacl_i32(111));
  ASSERT(sm->fields[2] == jacl_i32(222));
  ASSERT(sm->fields[3] == jacl_i32(333));
  ASSERT(sm->fields[0] == JACL_NIL);  /* untouched */
  ASSERT(sm->fields[4] == JACL_NIL);  /* untouched */

  /* Verify loaded values are on stack (after the SM at slot 0) */
  /* Stack should be: [sm_val, 111, 222, 333] */
  ASSERT_U32_EQ(vm.stack_top, 4);  /* SM + 3 loaded values */
  ASSERT(vm.stack[1] == jacl_i32(111));
  ASSERT(vm.stack[2] == jacl_i32(222));
  ASSERT(vm.stack[3] == jacl_i32(333));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_sm_struct_generator_heap_path(void) {
  /* Struct locals in generators work correctly via heap path across suspension */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc gen {} [Stream i32] {\n"
      "  def p [Point x 10 y 20]\n"
      "  [yield $p->x]\n"
      "  [yield $p->y]\n"
      "}\n"
      "def s [gen]\n"
      "[print [stream_next $s]]\n"
      "[print [stream_next $s]]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_sm_total_slots_allocation(void) {
  /* sm_field_count uses total_slots, SM allocation sizes correctly */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Allocate with total_slots=5 (e.g., 1 scalar + 1 struct(width=3) + 1 scalar) */
  JaclVal sm_val = gc_alloc_state_machine(&heap, 5);
  JaclStateMachine* sm = jacl_as_state_machine(sm_val);
  ASSERT_U32_EQ(sm->field_count, 5);
  /* All fields initialized to NIL */
  for (uint32_t i = 0; i < 5; i++) {
    ASSERT(sm->fields[i] == JACL_NIL);
  }

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-005 (Struct): Inline field access — byte-offset addressing */

static int test_struct_inline_get_basic(void) {
  /* Inline struct field read: $p->x, $p->y use OP_STRUCT_GET_INLINE */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_set_basic(void) {
  /* Inline struct field write: [. $p x 99] uses OP_STRUCT_SET_INLINE */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  . $p x 99\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "99\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_nested_get(void) {
  /* Nested inline field access: $ln->start->x chains offsets at compile time */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "struct Line {Point start, Point end}\n"
      "proc test {} {\n"
      "  def ln [Line start [Point x 5 y 6] end [Point x 10 y 20]]\n"
      "  print $ln->start->x\n"
      "  print $ln->start->y\n"
      "  print $ln->end->x\n"
      "  print $ln->end->y\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n6\n10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_materialize(void) {
  /* Inline struct materialized to heap for non-field-access use (print $p) */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def p [Point x 1 y 2]\n"
      "  print $p\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_wide_field_types(void) {
  /* Inline struct with various field types: f64, bool, i64 */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Vec3 {f64 x, f64 y, f64 z}\n"
      "proc test {} {\n"
      "  def v [Vec3 x 1.5 y 2.5 z 3.5]\n"
      "  print $v->x\n"
      "  print $v->y\n"
      "  print $v->z\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "1.5\n2.5\n3.5\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_preserves_other_locals(void) {
  /* Verify inline struct slots don't corrupt other locals */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Vec3 {f64 x, f64 y, f64 z}\n"
      "proc test {} {\n"
      "  def i32 before 42\n"
      "  def v [Vec3 x 1.5 y 2.5 z 3.5]\n"
      "  def i32 after 99\n"
      "  print $before\n"
      "  print $v->y\n"
      "  print $after\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n2.5\n99\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-006: Struct arguments — pass by value */

static int test_struct_pass_by_value_basic(void) {
  /* Passing struct to a function copies it — mutation doesn't affect caller */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc mutate {Point p} {\n"
      "  . $p x 99\n"
      "  print $p->x\n"
      "}\n"
      "proc test {} {\n"
      "  def pt [Point x 1 y 2]\n"
      "  [mutate $pt]\n"
      "  print $pt->x\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  /* mutate sees 99, but caller's pt still has 1 */
  ASSERT_STR_EQ(cap.buf, "99\n1\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_pass_by_value_recursive(void) {
  /* Recursive calls each get their own copy */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc bump {Point p} Point {\n"
      "  . $p x [+ $p->x 10]\n"
      "  $p\n"
      "}\n"
      "proc test {} {\n"
      "  def pt [Point x 0 y 0]\n"
      "  def Point r1 [bump $pt]\n"
      "  def Point r2 [bump $r1]\n"
      "  def Point r3 [bump $r2]\n"
      "  print $pt->x\n"
      "  print $r1->x\n"
      "  print $r2->x\n"
      "  print $r3->x\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  /* Each bump gets a copy, mutates x+10, returns it.
   * pt stays at 0, r1=10, r2=20, r3=30 */
  ASSERT_STR_EQ(cap.buf, "0\n10\n20\n30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_pass_by_value_chain(void) {
  /* Struct received as param, passed to another function — still copied */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc inner {Point p} {\n"
      "  . $p x 99\n"
      "  print $p->x\n"
      "}\n"
      "proc outer {Point p} {\n"
      "  [inner $p]\n"
      "  print $p->x\n"
      "}\n"
      "proc test {} {\n"
      "  def pt [Point x 1 y 2]\n"
      "  [outer $pt]\n"
      "  print $pt->x\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  /* inner sees 99, outer still sees 1, test still sees 1 */
  ASSERT_STR_EQ(cap.buf, "99\n1\n1\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-007: Struct returns — pass by value */

static int test_struct_return_basic(void) {
  /* Typed proc returns struct, caller stores inline via OP_STRUCT_STORE_INLINE */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc mkpt {} Point {\n"
      "  [Point x 42 y 99]\n"
      "}\n"
      "proc test {} {\n"
      "  def Point p [mkpt]\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n99\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_return_nested_calls(void) {
  /* Nested: A calls B which returns struct, A receives it correctly */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc mkpt {i32 a, i32 b} Point {\n"
      "  [Point x $a y $b]\n"
      "}\n"
      "proc test {} {\n"
      "  def Point p1 [mkpt 10 20]\n"
      "  def Point p2 [mkpt $p1->x $p1->y]\n"
      "  print $p1->x\n"
      "  print $p1->y\n"
      "  print $p2->x\n"
      "  print $p2->y\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  /* p1 and p2 both get (10,20) through nested calls */
  ASSERT_STR_EQ(cap.buf, "10\n20\n10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_return_value_isolation(void) {
  /* Returned struct is isolated — mutation doesn't affect original */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc dup {Point p} Point {\n"
      "  [Point x $p->x y $p->y]\n"
      "}\n"
      "proc test {} {\n"
      "  def pt [Point x 1 y 2]\n"
      "  def Point copy [dup $pt]\n"
      "  . $copy x 99\n"
      "  print $pt->x\n"
      "  print $copy->x\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  /* pt.x unchanged at 1, copy.x mutated to 99 */
  ASSERT_STR_EQ(cap.buf, "1\n99\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-008: Closure capture of structs */

static int test_struct_closure_capture_basic(void) {
  /* Closure captures an inline struct local by value, reads fields */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b [box [Point x 10 y 20]]\n"
      "  proc inner {Point p} {\n"
      "    print $p->x\n"
      "    print $p->y\n"
      "  }\n"
      "  if [box? Point $b] {\n"
      "    [inner [unbox $b]]\n"
      "  }\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_closure_capture_isolation(void) {
  /* Mutation of captured struct inside closure doesn't affect original */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def p [Point x 1 y 2]\n"
      "  def b [box $p]\n"
      "  proc reader {Point q} {\n"
      "    print $q->x\n"
      "  }\n"
      "  . $p x 99\n"
      "  if [box? Point $b] {\n"
      "    [reader [unbox $b]]\n"
      "  }\n"
      "  print $p->x\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  /* reader sees boxed copy x=1, outer sees mutated x=99 */
  ASSERT_STR_EQ(cap.buf, "1\n99\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_closure_capture_materialize(void) {
  /* Captured struct can be materialized (passed to print, used as value) */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b [box [Point x 42 y 99]]\n"
      /* deref of a struct-element box now narrows to TYPE_STRUCT;
       * the wrapping proc therefore needs an explicit return type
       * (struct returns require a wide-return annotation). */
      "  proc getter {} Point { deref $b }\n"
      "  def q [getter]\n"
      "  print $q->x\n"
      "  print $q->y\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n99\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* US-004 (Struct): Field access */

static int test_struct_get_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_get_unknown_field(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "def p [Point x 1 y 2]\n"
      "print $p->z", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_get_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "struct Line {Point start, Point end}\n"
      "proc main {} {\n"
      "  def ln [Line start [Point x 5 y 6] end [Point x 10 y 20]]\n"
      "  print $ln->start->x\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-005 (Struct): Field mutation ===== */

static int test_struct_set_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  . $p x 99\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_set_unknown_field(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "def p [Point x 1 y 2]\n"
      ". $p z 99", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_set_type_mismatch(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {mut i32 x, i32 y}\n"
      "def p [Point x 1 y 2]\n"
      ". $p x \"hello\"", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_set_preserves_other_fields(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Pair {mut i32 a, i32 b}\n"
      "proc main {} {\n"
      "  def p [Pair a 1 b 2]\n"
      "  . $p a 42\n"
      "  print $p->b\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== Dyn-struct interop is rejected — see test_struct_dyn_rejected ===== */

static int test_struct_dyn_rejected(void) {
  /* `def dyn d [Point ...]` is a compile error per the always-inline design.
     Use [box $val] to cross dyn boundaries. */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "def dyn d [Point x 1 y 2]\n"
      "print $d",
      &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_in_vec(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def v [vec [box [Point x 1 y 2]] [box [Point x 3 y 4]]]\n"
      "  def p [deref [vec-get $v 1]]\n"
      "  print $p->x\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-010: Box struct values and deref/reset/swap ===== */

static int test_struct_box_roundtrip(void) {
  /* [box $struct_val] creates a struct box, [deref] reads it back */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  def b [box $p]\n"
      "  def q [deref $b]\n"
      "  print $q->x\n"
      "  print $q->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_box_reset(void) {
  /* [reset $boxed_struct $new_struct] replaces struct contents */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  def b [box $p]\n"
      "  def q [Point x 30 y 40]\n"
      "  reset $b $q\n"
      "  def r [deref $b]\n"
      "  print $r->x\n"
      "  print $r->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n40\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_box_swap(void) {
  /* [swap $boxed_struct {closure}] swaps struct, returns old value */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc swapper {dyn old} Point { [Point x 99 y 88] }\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  def b [box $p]\n"
      "  swap $b $swapper\n"
      "  def r [deref $b]\n"
      "  print $r->x\n"
      "  print $r->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "99\n88\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_box_deref_isolation(void) {
  /* Deref returns a copy — mutation of original box doesn't affect derefed value */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  def b [box $p]\n"
      "  def q [deref $b]\n"
      "  reset $b [Point x 99 y 88]\n"
      "  print $q->x\n"
      "  print $q->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_atom_rejected(void) {
  /* [atom $struct_val] is a compile error */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "def p [Point x 10 y 20]\n"
      "atom $p",
      &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-011: box? type checking with flow typing ===== */

static int test_box_typed_check_struct(void) {
  /* [box? Point $val] returns true for Point boxes, false for dyn boxes */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b1 [box [Point x 10 y 20]]\n"
      "  def b2 [box 42]\n"
      "  print [box? Point $b1]\n"
      "  print [box? Point $b2]\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\nfalse\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_box_typed_check_dyn(void) {
  /* [box? dyn $val] returns true for dyn boxes, false for struct boxes */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b1 [box 42]\n"
      "  def b2 [box [Point x 10 y 20]]\n"
      "  print [box? dyn $b1]\n"
      "  print [box? dyn $b2]\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\nfalse\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_box_flow_typing_unbox(void) {
  /* Flow typing: if [box? Point $b] { def Point p [unbox $b] } */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b [box [Point x 10 y 20]]\n"
      "  if [box? Point $b] {\n"
      "    def p [unbox $b]\n"
      "    print $p->x\n"
      "    print $p->y\n"
      "  }\n"
      "}\n"
      "[test]",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_unbox_outside_guard_rejected(void) {
  /* [unbox $val] outside a box?-guarded branch is a compile error */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b [box [Point x 10 y 20]]\n"
      "  unbox $b\n"
      "}\n",
      &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_box_typed_unknown_type_rejected(void) {
  /* [box? UnknownType $val] is a compile error */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "def b [box 42]\n"
      "box? Foo $b",
      &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-012: Generic operations on boxed structs ===== */

static int test_struct_box_equality(void) {
  /* Two boxes with same struct type and data are equal; different data are not */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b1 [box [Point x 10 y 20]]\n"
      "  def b2 [box [Point x 10 y 20]]\n"
      "  def b3 [box [Point x 10 y 99]]\n"
      "  def b4 [box 42]\n"
      "  print [== $b1 $b2]\n"
      "  print [== $b1 $b3]\n"
      "  print [== $b1 $b4]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\nfalse\nfalse\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_box_hashing(void) {
  /* Equal struct boxes produce same hash — usable as map keys */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b1 [box [Point x 10 y 20]]\n"
      "  def b2 [box [Point x 10 y 20]]\n"
      "  def m [map $b1 hello]\n"
      "  print [map-get $m $b2]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_box_stringify(void) {
  /* Stringify of boxed struct shows struct name and field values */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def b [box [Point x 10 y 20]]\n"
      "  print [to-string $b]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "<box Point: x=10 y=20>\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_box_stringify_dyn(void) {
  /* Dyn box stringify is unchanged */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "proc test {} {\n"
      "  def b [box 42]\n"
      "  print [to-string $b]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "<box: 42>\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_box_map_key(void) {
  /* Boxed structs usable as map keys with correct equality + hashing */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def k1 [box [Point x 1 y 2]]\n"
      "  def k2 [box [Point x 3 y 4]]\n"
      "  def k3 [box [Point x 1 y 2]]\n"
      "  def m [map $k1 alpha $k2 beta]\n"
      "  print [map-get $m $k3]\n"
      "  print [map-get $m $k2]\n"
      "  print [map-has $m $k1]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "alpha\nbeta\ntrue\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-013: Byte-level equality and hashing for unboxed structs ===== */

static int test_struct_inline_eq_same(void) {
  /* Two inline structs with identical data should be equal */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def Point a [Point x 10 y 20]\n"
      "  def Point b [Point x 10 y 20]\n"
      "  print [== $a $b]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_eq_different(void) {
  /* Two inline structs with different data should not be equal */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def Point a [Point x 10 y 20]\n"
      "  def Point b [Point x 10 y 99]\n"
      "  print [== $a $b]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "false\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_hash_equal(void) {
  /* Equal inline structs produce the same hash */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def Point a [Point x 10 y 20]\n"
      "  def Point b [Point x 10 y 20]\n"
      "  print [== [hash $a] [hash $b]]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_inline_hash_different(void) {
  /* Different inline structs produce different hashes (no collision expected) */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def Point a [Point x 10 y 20]\n"
      "  def Point b [Point x 10 y 99]\n"
      "  print [== [hash $a] [hash $b]]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "false\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_heap_equality(void) {
  /* Materialized (heap) structs from function returns compare correctly */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc mkpt {i32 a, i32 b} Point {\n"
      "  return [Point x $a y $b]\n"
      "}\n"
      "proc test {} {\n"
      "  def Point p1 [mkpt 5 10]\n"
      "  def Point p2 [mkpt 5 10]\n"
      "  def Point p3 [mkpt 5 99]\n"
      "  print [== $p1 $p2]\n"
      "  print [== $p1 $p3]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\nfalse\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_eq_padding_zeroed(void) {
  /* Padding bytes are zeroed so structs with same fields are equal
     even when padding exists (e.g., i32 field in 8-byte-aligned struct) */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  /* A struct with a single i32 field has 4 bytes data but occupies
     1 full JaclVal slot (8 bytes). Padding bytes must be zeroed. */
  VMResult r = jacl_run(
      "struct Small {i32 val}\n"
      "proc test {} {\n"
      "  def Small a [Small val 42]\n"
      "  def Small b [Small val 42]\n"
      "  print [== $a $b]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_hash_generic(void) {
  /* [hash $val] works on non-struct values too */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "proc test {} {\n"
      "  def h1 [hash 42]\n"
      "  def h2 [hash 42]\n"
      "  print [== $h1 $h2]\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-015: Migration scaffolding — dual path ===== */

static int test_dual_path_value_type_inline(void) {
  /* Value-type structs (only primitive fields) use inline storage */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_set_arrow_inline(void) {
  /* set n->field on value-type (inline) struct */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "proc test {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  set p->x 99\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "99\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_set_arrow_chained(void) {
  /* set ln->start->x on nested struct */
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {mut i32 x, i32 y}\n"
      "struct Line {mut Point start, Point end}\n"
      "proc test {} {\n"
      "  def ln [Line start [Point x 1 y 2] end [Point x 3 y 4]]\n"
      "  set ln->start->x 77\n"
      "  print $ln->start->x\n"
      "  print $ln->end->y\n"
      "}\n"
      "test",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "77\n4\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-008 (Struct): Inline anonymous struct types ===== */

static int test_inline_struct_runtime(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "struct Wrapper {struct [i32 x, i32 y] pos}\n"
      "proc main {} {\n"
      "  def w [Wrapper pos [Point x 42 y 10]]\n"
      "  print $w->pos->x\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_inline_struct_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Wrapper {struct [i32 x, i32 y] pos}", &arena, &heap);
  if (cr.error_count > 0) {
    printf("ERRORS: %u\n", cr.error_count);
  }
  ASSERT_U32_EQ(cr.error_count, 0);
  /* Registry should have 2 struct types: the anonymous struct and Wrapper
     (count includes reserved slot 0 + ctx struct, so count == 4) */
  ASSERT(cr.struct_registry != NULL);
  ASSERT(cr.struct_registry->count == 4);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_inline_struct_equivalence(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct A {struct [i32 x, i32 y] p}\n"
      "struct B {struct [i32 x, i32 y] q}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  /* Registry: anon_struct (shared), A, B = 3 struct types + ctx
     (count includes reserved slot 0, so count == 5) */
  ASSERT(cr.struct_registry != NULL);
  ASSERT(cr.struct_registry->count == 5);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_inline_struct_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct C {struct [struct [i32 x, i32 y] start, struct [i32 x, i32 y] end] d}",
      &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.struct_registry != NULL);
  /* inner struct (shared) + outer struct + C = 3 struct types + ctx
     (count includes reserved slot 0, so count == 5) */
  ASSERT(cr.struct_registry->count == 5);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-009 (Struct): Module export/import of struct types ===== */

static int test_struct_module_import(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009a";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "shapes.jacl",
    "struct Point {i32 x, i32 y}\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"shapes.jacl\" {Point}\n"
    "proc main {} {\n"
    "  def p [Point x 42 y 10]\n"
    "  print $p->x\n"
    "}\n"
    "main\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us009a/main.jacl", &arena, &intern, &heap);
  if (pr.error_count > 0) {
    printf("COMPILE ERROR: %s\n", pr.error_message ? pr.error_message : "(null)");
  }
  ASSERT_U32_EQ(pr.error_count, 0);

  US010PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = us010_capture_print;
  vm.print_ctx = &cap;
  vm.intern_table = &intern;

  VMResult r = jacl_exec_program(&pr, &vm);
  if (r != VM_OK) {
    printf("RUN ERROR: %s\n", vm.error_message ? vm.error_message : "(null)");
  }
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  const char* files[] = { "shapes.jacl", "main.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Syntax Redesign US-004: New struct syntax */

static int test_struct_new_syntax_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_syntax_runtime(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 10 y 20]\n"
      "  print $p->x\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_syntax_all_types(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct AllTypes {i32 a, i64 b, u32 c, u64 d, f32 e, f64 f, bool g}",
      &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_syntax_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "struct Line {Point start, Point end}",
      &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_syntax_construct_and_access(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn  = capture_print;
  vm.print_ctx = &cap;

  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  def p [Point x 3 y 7]\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT(r == VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n7\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_new_syntax_duplicate_field(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "struct Bad {i32 x, i32 x}", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Syntax Redesign US-005: New if/elif/else and while syntax --- */

/* if $true { 1 } → takes then branch, returns 1 */
static int test_if_new_then_branch(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("if $true { 1 }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  /* no else → nil when condition false, but condition is true → returns 1 */
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 1);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* if $false { 1 } else { 2 } → takes else branch, returns 2 */
static int test_if_new_else_branch(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("if $false { 1 } else { 2 }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 2);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* if $false { 1 } → no else, returns nil */
static int test_if_new_no_else_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("if $false { 1 }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* if ($n > 0) { "positive" } else { "non-positive" } — infix condition */
/* if $false { 1 } elif $true { 2 } else { 3 } → takes elif branch, returns 2 */
static int test_if_new_elif(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "if $false { 1 } elif $true { 2 } else { 3 }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 2);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* if $false { 1 } elif $false { 2 } else { 3 } → takes else branch, returns 3 */
static int test_if_new_elif_else(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "if $false { 1 } elif $false { 2 } else { 3 }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 3);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* if/elif chain with 3 branches — first match wins */
static int test_if_new_elif_chain(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "if $false { 1 } elif $false { 2 } elif $true { 3 } else { 4 }",
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 3);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* if is an expression — [+ if $true { 10 } else { 20 } 5] not directly testable
   but we can test via old bracket syntax embedding new if */
static int test_if_new_as_expression(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  /* Use def to capture value of new-syntax if as expression */
  VMResult result = jacl_run(
    "x = [if $true { 10 } { 20 }]\n"
    "[+ $x 5]",
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 15);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* while ($i < 5) { body } — new syntax iterative sum */
static int test_while_new_iterative_sum(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "i : 1\n"
    "sum : 0\n"
    "while [<= $i 5] {\n"
    "  sum :: [+ $sum $i]\n"
    "  i :: [+ $i 1]\n"
    "}\n"
    "[print $sum]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* while with infix condition: while ($i < 3) { ... } */
/* while returns nil */
static int test_while_new_returns_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("while $false { 999 }", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Combined if + while with new syntax */
static int test_if_while_combined_new(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "i : 1\n"
    "while [<= $i 4] {\n"
    "  if [== [% $i 2] 0] {\n"
    "    print \"even\"\n"
    "  } else {\n"
    "    print \"odd\"\n"
    "  }\n"
    "  i :: [+ $i 1]\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "odd\neven\nodd\neven\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ====================== Syntax Redesign US-006: for command ====================== */

/* for $items { body } — implicit $it binding */
static int test_for_implicit_it(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "for [vec 10 20 30] {\n"
    "  print $it\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for $items item { body } — explicit binding variable */
static int test_for_explicit_binding(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "for [vec 1 2 3] x {\n"
    "  print $x\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for $items [\  print $it] — HOF callback form (like each) */
static int test_for_hof_callback(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "v = [vec 10 20 30]\n"
    "proc p {x} { print $x }\n"
    "[for $v $p]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for on empty vector — no iteration */
static int test_for_empty_vec(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "for [vec] {\n"
    "  print $it\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for with variable collection — for $v { body } */
static int test_for_variable_collection(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "v = [vec 5 10 15]\n"
    "for $v {\n"
    "  print $it\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n10\n15\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for returns nil */
static int test_for_returns_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "v = [vec 1]\n"
    "proc p {x} { + $x 0 }\n"
    "[print [for $v $p]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for with upvalue capture — implicit $it body referencing outer scope */
static int test_for_upvalue_capture(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "offset = 100\n"
    "for [vec 1 2 3] {\n"
    "  print [+ $offset $it]\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "101\n102\n103\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* each still works as alias during transition */
static int test_each_still_works(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "v = [vec 10 20 30]\n"
    "proc p {x} { print $x }\n"
    "[for $v $p]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== For-loop PRD US-001: Break as control flow ===== */

/* Test: break exits while loop, loop evaluates to nil */
static int test_break_while_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "i : 0\n"
    "while $true {\n"
    "  if [>= $i 3] { break }\n"
    "  print $i\n"
    "  i :: [+ $i 1]\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n2\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: break $value returns value from while loop expression */
static int test_break_while_with_value(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "i : 0\n"
    "result = [while $true {\n"
    "  if [>= $i 5] { break $i }\n"
    "  i :: [+ $i 1]\n"
    "}]\n"
    "[print $result]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: break with no value makes loop evaluate to nil */
static int test_break_while_nil_value(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "result = [while $true {\n"
    "  break\n"
    "}]\n"
    "[print $result]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested loops — break exits only the innermost loop */
static int test_break_nested_loops(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Outer loop runs 3 times, inner loop breaks after 2 iterations */
  VMResult result = jacl_run(
    "i : 0\n"
    "while [< $i 3] {\n"
    "  j : 0\n"
    "  while $true {\n"
    "    if [>= $j 2] { break }\n"
    "    print $i\n"
    "    print $j\n"
    "    j :: [+ $j 1]\n"
    "  }\n"
    "  i :: [+ $i 1]\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  /* i=0,j=0; i=0,j=1; i=1,j=0; i=1,j=1; i=2,j=0; i=2,j=1 */
  ASSERT_STR_EQ(cap.buf, "0\n0\n0\n1\n1\n0\n1\n1\n2\n0\n2\n1\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: break outside loop is compile error */
static int test_break_outside_loop_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[break]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "break outside of loop") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: break outside loop in bare command form is compile error */
static int test_break_bare_outside_loop_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("break", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "break outside of loop") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: break with value using bare command form */
static int test_break_bare_with_value(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "i : 0\n"
    "result = [while $true {\n"
    "  if [>= $i 3] {\n"
    "    break $i\n"
    "  }\n"
    "  i :: [+ $i 1]\n"
    "}]\n"
    "[print $result]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: continue in while loop skips to next iteration */
static int test_continue_while_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print 0,1,3,4 — skip 2 */
  VMResult result = jacl_run(
    "i : 0\n"
    "while [< $i 5] {\n"
    "  old = $i\n"
    "  i :: [+ $i 1]\n"
    "  if [== $old 2] { continue }\n"
    "  print $old\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n3\n4\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: continue outside loop is compile error */
static int test_continue_outside_loop_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("[continue]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "continue outside of loop") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== For-loop PRD US-002: Continue as control flow ===== */

/* Test: for over empty collection — no iterations */
static int test_for_empty_collection(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "for [vec] {\n"
    "  print $it\n"
    "}\n"
    "print \"done\"\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "done\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: for with continue — skips element */
static int test_continue_for_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print 1, 2, 4, 5 — skip 3 */
  VMResult result = jacl_run(
    "for [vec 1 2 3 4 5] {\n"
    "  if [== $it 3] { continue }\n"
    "  print $it\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n4\n5\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: continue in for loop — bare syntax */
static int test_continue_for_bare(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print 1, 3 — skip 2 */
  VMResult result = jacl_run(
    "for [vec 1 2 3] {\n"
    "  if [== $it 2] { continue }\n"
    "  print $it\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested loops — continue affects only innermost */
static int test_continue_nested_loops(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Outer: 1,2  Inner: skip 20, print 10,30
     Expected: 10 30 10 30 */
  VMResult result = jacl_run(
    "for [vec 1 2] x {\n"
    "  for [vec 10 20 30] y {\n"
    "    if [== $y 20] { continue }\n"
    "    print $y\n"
    "  }\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n30\n10\n30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: for with break — exits loop */
static int test_break_for_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print 1, 2 — break at 3 */
  VMResult result = jacl_run(
    "for [vec 1 2 3 4 5] {\n"
    "  if [== $it 3] { break }\n"
    "  print $it\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: for with break value */
static int test_break_for_with_value(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* break with value from for loop — the for expression evaluates to 30 */
  VMResult result = jacl_run(
    "result = [for [vec 10 20 30 40] {\n"
    "  if [== $it 30] { break $it }\n"
    "}]\n"
    "[print $result]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: continue in while + for nested — only innermost affected */
static int test_continue_mixed_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Outer while iterates twice. Inner for skips element 2.
     Expected: 1 3 1 3 done */
  VMResult result = jacl_run(
    "n : 0\n"
    "while [< $n 2] {\n"
    "  n :: [+ $n 1]\n"
    "  for [vec 1 2 3] {\n"
    "    if [== $it 2] { continue }\n"
    "    print $it\n"
    "  }\n"
    "}\n"
    "print \"done\"\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n3\n1\n3\ndone\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== For-loop PRD US-003: Inline block-form for bodies — return semantics ===== */

/* Test: return from inside a for block exits the enclosing proc */
static int test_return_from_for_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* proc that returns early from a for loop */
  VMResult result = jacl_run(
    "proc find {items} {\n"
    "  for $items {\n"
    "    if [== $it 3] { return $it }\n"
    "  }\n"
    "  999\n"
    "}\n"
    "print [find [vec 1 2 3 4 5]]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: return $value from inside a for block returns the value from the proc */
static int test_return_value_from_for_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* proc that returns a computed value from inside a for loop */
  VMResult result = jacl_run(
    "proc sumto {items limit} {\n"
    "  acc : 0\n"
    "  for $items x {\n"
    "    if [> $x $limit] { return $acc }\n"
    "    set acc [+ $acc $x]\n"
    "  }\n"
    "  $acc\n"
    "}\n"
    "print [sumto [vec 1 2 3 4 5] 3]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "6\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: return from HOF callback for only exits the callback */
static int test_return_from_hof_for(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* HOF-form for: return exits the callback proc, not the enclosing proc.
     The callback returns early for x == 2 but the for loop continues. */
  VMResult result = jacl_run(
    "proc testfn {} {\n"
    "  proc cb {x} {\n"
    "    if [== $x 2] { return \"skip\" }\n"
    "    print $x\n"
    "  }\n"
    "  [for [vec 1 2 3] $cb]\n"
    "  print \"done\"\n"
    "}\n"
    "[testfn]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  /* return in callback exits only the callback, not testfn.
     So 1 prints, 2 is skipped (return exits callback), 3 prints, then "done" prints. */
  ASSERT_STR_EQ(cap.buf, "1\n3\ndone\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: bare return from inside a for block exits the enclosing proc */
static int test_return_bare_from_for_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* bare return with value from a for loop */
  VMResult result = jacl_run(
    "proc find {items} {\n"
    "  for $items {\n"
    "    if [== $it 3] { return $it }\n"
    "  }\n"
    "  999\n"
    "}\n"
    "print [find [vec 1 2 3 4 5]]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: return with no value from for block returns nil */
static int test_return_nil_from_for_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* return with no value exits proc returning nil */
  VMResult result = jacl_run(
    "proc find {items} {\n"
    "  for $items {\n"
    "    if [== $it 3] { return }\n"
    "  }\n"
    "  999\n"
    "}\n"
    "print [find [vec 1 2 3 4 5]]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== For-loop PRD US-004: C-style for loop ===== */

/* Test: basic counting C-style for loop prints 0 1 2 3 4 */
static int test_for_cstyle_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "[for {i : 0; [< $i 5]; set i [+ $i 1]} {\n"
    "  print $i\n"
    "}]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n2\n3\n4\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: C-style for with break exits early */
static int test_for_cstyle_break(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print 0 1 2 then break */
  VMResult result = jacl_run(
    "[for {i : 0; [< $i 10]; set i [+ $i 1]} {\n"
    "  if [== $i 3] { break }\n"
    "  print $i\n"
    "}]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n2\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: C-style for with continue skips iteration */
static int test_for_cstyle_continue(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print 0 1 3 4 — skip 2 */
  VMResult result = jacl_run(
    "[for {i : 0; [< $i 5]; set i [+ $i 1]} {\n"
    "  if [== $i 2] { continue }\n"
    "  print $i\n"
    "}]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n3\n4\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested C-style for loops */
static int test_for_cstyle_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Outer i=0..1, inner j=0..1: prints 00 01 10 11 */
  VMResult result = jacl_run(
    "[for {i : 0; [< $i 2]; set i [+ $i 1]} {\n"
    "  for {j : 0; [< $j 2]; set j [+ $j 1]} {\n"
    "    print [+ [* $i 10] $j]\n"
    "  }\n"
    "}]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n1\n10\n11\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: loop var from C-style for is not visible after loop ends (runtime error) */
static int test_for_cstyle_scope(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);

  /* $i should not be visible after the loop; this should be a runtime error */
  VMResult result = jacl_run(
    "[for {i : 0; [< $i 3]; set i [+ $i 1]} { print $i }]\n"
    "[print $i]\n",  /* $i is undefined here */
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== For-loop PRD US-005: Comprehensive e2e test suite ===== */

/* Test: break from for loop — loop expression evaluates to nil */
static int test_break_for_nil_value(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* break with no value — for expression evaluates to nil */
  VMResult result = jacl_run(
    "r = [for [vec 1 2 3] {\n"
    "  if [== $it 2] { break }\n"
    "}]\n"
    "[print $r]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: break in nested for-for loops exits only innermost */
static int test_break_nested_for_loops(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Outer for runs over 1,2. Inner for breaks at 20.
     Expected: 10 10 */
  VMResult result = jacl_run(
    "for [vec 1 2] x {\n"
    "  for [vec 10 20 30] y {\n"
    "    if [== $y 20] { break }\n"
    "    print $y\n"
    "  }\n"
    "}\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n10\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: continue bare form outside loop is compile error */
static int test_continue_bare_outside_loop_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("continue", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "continue outside of loop") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: return from C-style for body exits enclosing proc */
static int test_for_cstyle_return(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* proc returns from C-style for — should not reach "done" */
  VMResult result = jacl_run(
    "proc find {} {\n"
    "  for {i : 0; [< $i 10]; set i [+ $i 1]} {\n"
    "    if [== $i 3] { return $i }\n"
    "  }\n"
    "  999\n"
    "}\n"
    "print [find]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: break $value from C-style for loop expression */
static int test_for_cstyle_break_value(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* break with value — for expression evaluates to 42 */
  VMResult result = jacl_run(
    "r = [for {i : 0; [< $i 10]; set i [+ $i 1]} {\n"
    "  if [== $i 3] { break 42 }\n"
    "}]\n"
    "[print $r]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== Syntax Redesign US-007: Builtin renames (set/reset/swap) ===== */

/* Test: set (without !) works for mutable local reassignment */
static int test_set_without_bang(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "{\n"
    "  x : 10\n"
    "  set x 42\n"
    "  print $x\n"
    "}", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set! still works (backward compat) */
static int test_set_bang_still_works(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "{\n"
    "  x : 10\n"
    "  x :: 99\n"
    "  print $x\n"
    "}", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "99\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: reset (without !) works for atom/box reset */
static int test_reset_without_bang(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "b = [atom 10]\n"
    "[reset $b 42]\n"
    "[print [deref $b]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: reset! is removed — produces error */
static int test_reset_bang_still_works(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "b = [atom 10]\n"
    "[reset! $b 99]\n"
    "[print [deref $b]]", &vm, &arena);

  ASSERT(result != VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: swap (without !) works for atom/box swap */
static int test_swap_without_bang(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "proc inc {x} { + $x 1 }\n"
    "b = [atom 10]\n"
    "[swap $b $inc]\n"
    "[print [deref $b]]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "11\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: swap! is removed — produces error */
static int test_swap_bang_still_works(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "proc inc {x} { + $x 1 }\n"
    "b = [atom 10]\n"
    "[swap! $b $inc]\n"
    "[print [deref $b]]", &vm, &arena);

  ASSERT(result != VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set on mutable global works */
static int test_set_global_without_bang(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "cnt : 0\n"
    "[set cnt 1]\n"
    "[set cnt [+ $cnt 1]]\n"
    "[print $cnt]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "2\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: set with multiple reassignments */
static int test_set_multiple_reassign(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "{\n"
    "  x : 0\n"
    "  set x 1\n"
    "  set x 2\n"
    "  set x 3\n"
    "  print $x\n"
    "}", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Syntax Redesign US-008: Binding operators (=, :, ::) --- */

/* x = 5 → def x 5 — basic immutable binding */
static int test_bind_equals_def(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "x = 5\n"
    "[print $x]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* i64 x = 5 → def i64 x 5 — typed immutable binding */
static int test_bind_typed_equals(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "i64 x = 5\n"
    "[print $x]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* x : 0; set x 5 → mut + set — mutable binding */
static int test_bind_colon_mut(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "x : 0\n"
    "set x 5\n"
    "[print $x]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* i64 x : 0 → mut i64 x 0 — typed mutable binding */
static int test_bind_typed_colon(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "i64 x : 0\n"
    "set x 42\n"
    "[print $x]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* x :: [+ $x 1] → set x [+ $x 1] — reassignment via :: */
static int test_bind_double_colon_set(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "x : 10\n"
    "x :: [+ $x 5]\n"
    "[print $x]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Binding operators alongside def/mut/set commands */
static int test_bind_mixed_with_commands(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "a = 1\n"
    "def b 2\n"
    "c : 0\n"
    "mut d 0\n"
    "c :: 3\n"
    "set d 4\n"
    "[print [+ [+ $a $b] [+ $c $d]]]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Binding in block body */
static int test_bind_in_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "proc test {} {\n"
    "  x = 10\n"
    "  y = 20\n"
    "  print [+ $x $y]\n"
    "}\n"
    "[test]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Binding with expression values */
static int test_bind_expr_values(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "x = [+ 1 2]\n"
    "y = [* $x 3]\n"
    "[print $y]\n", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "9\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== Syntax Redesign US-009: Arrow field access (->) ===== */

static int test_arrow_basic_get(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  p = [Point x 10 y 20]\n"
      "  print $p->x\n"
      "  print $p->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_arrow_chained_get(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "struct Line {Point start, Point end}\n"
      "proc main {} {\n"
      "  ln = [Line start [Point x 5 y 6] end [Point x 10 y 20]]\n"
      "  print $ln->start->x\n"
      "  print $ln->end->y\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n20\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_arrow_on_expr_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc mkpt {} Point { [Point x 42 y 99] }\n"
      "print [mkpt]->x",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_arrow_in_infix_mode(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  p = [Point x 10 y 20]\n"
      "  print [+ $p->x $p->y]\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_arrow_in_bracket_cmd(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "struct Point {i32 x, i32 y}\n"
      "proc main {} {\n"
      "  p = [Point x 10 y 20]\n"
      "  print [+ $p->x $p->y]\n"
      "}\n"
      "main",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_arrow_old_dot_compat(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* [. $p x] inside brackets is now a parse error — use $p->x instead */
  CompileResult cr = compile_source(
      "struct Point {i32 x, i32 y}\n"
      "p = [Point x 10 y 20]\n"
      "print [. $p x]", &arena, &heap);
  ASSERT(cr.error_count > 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ---- Syntax Redesign US-010: Pipe threading (|) ---- */

/* foo $a | bar $b  →  [bar [foo $a] $b] (first-arg threading) */
static int test_pipe_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "+ 1 2 | * 3 | print",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "9\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Multi-stage pipe: a | b | c  →  [c [b [a]]] */
static int test_pipe_multi_stage(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  /* + 10 20 → 30, * 30 2 → 60, print 60 */
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "+ 10 20 | * 2 | print",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "60\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Pipe composes with vec builtins */
static int test_pipe_with_builtins(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "vec 1 2 3 | vec-len | print",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Pipe in block */
static int test_pipe_in_block(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "proc dbl {x} { * $x 2 }\n"
      "dbl 5 | print",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Pipe with variable references */
static int test_pipe_with_vars(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "x = 5\n"
      "y = 3\n"
      "+ $x $y | * 2 | print",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "16\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Pipe inside proc body block */
static int test_pipe_in_proc_body(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "proc f {} { + 3 4 | * 2 }\n"
      "f | print",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "14\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- US-011: Lambda shorthand (\) --- */

/* Basic lambda: [\\ $+ $it 2] called via for */
static int test_lambda_basic_call(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "for [vec 10 20 30] [print $it]",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "10\n20\n30\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Lambda with arithmetic */
static int test_lambda_arithmetic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "for [vec 1 2 3] [print [+ $it 10]]",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "11\n12\n13\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Lambda as HOF callback for for */
static int test_lambda_each_compat(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "for [vec 3 5 7] [print [* $it 2]]",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "6\n10\n14\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Lambda captures upvalue */
static int test_lambda_upvalue(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "x = 100\n"
      "for [vec 1 2 3] [print [+ $it $x]]",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "101\n102\n103\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Lambda used as expression value (passed to proc) */
static int test_lambda_as_value(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "proc apply {f, x} { $f $x }\n"
      "apply [\\ $+ $it 5] 10 | print",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Lambda in pipe chain */
static int test_lambda_in_pipe(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "for [vec 5 10 15] [print [* $it 3]]",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "15\n30\n45\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== Syntax Redesign US-012: $(expr) string interpolation & line continuation ===== */

/* Basic $(1 + 2) inside string */
/* $(expr) with variable references */
/* $(expr) nesting with $[cmd] */
/* Existing $var and $[expr] interpolation still works */
static int test_dollar_paren_compile_existing(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "def name \"world\"\nprint \"hello $name, $[+ 1 2]\"",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello world, 3\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Line continuation: \ at end of line */
static int test_line_continuation_compile(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "print \\\n  42",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Blank lines don't produce empty commands */
static int test_blank_lines_compile(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  VM vm;
  vm_init(&vm, &arena);
  PrintCapture cap = { .len = 0 };
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult r = jacl_run(
      "\n\nprint 1\n\n\nprint 2\n\n",
      &vm, &arena);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Shell Interop US-003: Compiler emits code for !cmd --- */

/* Helper: compile with prelude map */
static CompileResult compile_with_prelude(const char* source, arena_t* arena,
                                          ThreadHeap* heap, JaclVal prelude) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, arena);
  return compiler_compile(parse, arena, &intern_table, heap, NULL, NULL, prelude, NULL, NULL);
}

/* Test: !ls -la compiles without error (no prelude mode) */
static int test_shell_cmd_compile_simple(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("!ls -la", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  /* Bytecode should contain: OP_GET_GLOBAL (exec), OP_CONST (head), OP_CONST (args), OP_CALL */
  ASSERT(cr.chunk.code_count > 4);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: !$cmd $arg1 compiles with variable references */
static int test_shell_cmd_compile_vars(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("cmd = \"ls\"; arg1 = \"-la\"; !$cmd $arg1", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: !cmd ..$args compiles successfully (US-014: spread support) */
static int test_shell_cmd_compile_splat(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("args = [vec \"-l\" \"-a\"]; !ls ..$args", &arena, &heap);

  /* US-014: Spread in shell commands now compiles successfully */
  ASSERT_INT_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: !cmd with empty prelude (no exec) produces compile error */
static int test_shell_cmd_no_exec_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Create empty map for prelude */
  JaclVal empty_prelude = jacl_map_ptr(NULL);  /* empty map */

  CompileResult cr = compile_with_prelude("!ls", &arena, &heap, empty_prelude);

  ASSERT(cr.error_count > 0);
  /* Error message should be "exec not available" */

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: !cmd with exec in prelude compiles successfully */
static int test_shell_cmd_with_exec_prelude(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Create prelude map with exec key */
  JaclInternTable intern;
  intern_table_init(&intern, &arena);
  JaclVal exec_key = jacl_string_new(&heap, &intern, "exec", 4);
  jacl_map_node* map_root = jacl_map_set(NULL, exec_key, JACL_TRUE);
  JaclVal prelude = jacl_map_ptr(map_root);

  CompileResult cr = compile_with_prelude("!ls -la", &arena, &heap, prelude);

  ASSERT_U32_EQ(cr.error_count, 0);

  jacl_map_unref(map_root);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: !cmd quoted command name */
static int test_shell_cmd_quoted_name(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source("!\"my script.sh\"", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Implicit Context (US-003): Ctx field collection and struct registration --- */

/* Test: built-in pwd field present at offset 0 */
static int test_ctx_builtin_pwd(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  /* Even empty source should get ctx struct with pwd field */
  CompileResult cr = compile_source("42", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.struct_registry != NULL);
  ASSERT(cr.struct_registry->ctx_type_idx != 0);

  uint32_t ctx_idx = cr.struct_registry->ctx_type_idx;
  ASSERT(ctx_idx < cr.struct_registry->count);
  StructTypeDef* ctx_def = cr.struct_registry->defs[ctx_idx];
  ASSERT(ctx_def != NULL);
  ASSERT_U32_EQ(ctx_def->name_len, 3);
  ASSERT(memcmp(ctx_def->name, "ctx", 3) == 0);
  ASSERT(ctx_def->field_count >= 1);
  /* pwd is first field at offset 0 */
  ASSERT_U32_EQ(ctx_def->fields[0].offset, 0);
  ASSERT_U32_EQ(ctx_def->fields[0].name_len, 3);
  ASSERT(memcmp(ctx_def->fields[0].name, "pwd", 3) == 0);
  ASSERT(ctx_def->fields[0].type == TYPE_STR);
  ASSERT(ctx_def->fields[0].is_mutable == true);
  /* ctx is the lone HeapRecord — distinguished by reg->ctx_type_idx,
     not by a per-def flag (the is_value_type field was removed). */
  ASSERT_U32_EQ(cr.struct_registry->ctx_type_idx, ctx_idx);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: single user ctx field registered with proper offset */
static int test_ctx_single_user_field(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "ctx i32 count = 0", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.struct_registry != NULL);
  ASSERT(cr.struct_registry->ctx_type_idx != 0);

  uint32_t ctx_idx = cr.struct_registry->ctx_type_idx;
  StructTypeDef* ctx_def = cr.struct_registry->defs[ctx_idx];
  ASSERT(ctx_def != NULL);
  /* Should have 2 fields: pwd + count */
  ASSERT_U32_EQ(ctx_def->field_count, 2);
  /* pwd at offset 0, size 8 (str = JaclVal = 8 bytes) */
  ASSERT_U32_EQ(ctx_def->fields[0].offset, 0);
  ASSERT(memcmp(ctx_def->fields[0].name, "pwd", 3) == 0);
  /* count at offset 8, size 4 (i32) */
  ASSERT_U32_EQ(ctx_def->fields[1].offset, 8);
  ASSERT(memcmp(ctx_def->fields[1].name, "count", 5) == 0);
  ASSERT(ctx_def->fields[1].type == TYPE_I32);
  ASSERT(ctx_def->fields[1].is_mutable == false);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: multiple ctx fields from one module */
static int test_ctx_multiple_fields(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "ctx mut i32 count = 0\n"
      "ctx str name = \"default\"\n"
      "ctx f64 ratio = 1.5", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.struct_registry != NULL);

  uint32_t ctx_idx = cr.struct_registry->ctx_type_idx;
  StructTypeDef* ctx_def = cr.struct_registry->defs[ctx_idx];
  ASSERT(ctx_def != NULL);
  /* Should have 4 fields: pwd + count + name + ratio */
  ASSERT_U32_EQ(ctx_def->field_count, 4);
  /* Check field names and types */
  ASSERT(memcmp(ctx_def->fields[0].name, "pwd", 3) == 0);
  ASSERT(memcmp(ctx_def->fields[1].name, "count", 5) == 0);
  ASSERT(ctx_def->fields[1].is_mutable == true);
  ASSERT(ctx_def->fields[1].type == TYPE_I32);
  ASSERT(memcmp(ctx_def->fields[2].name, "name", 4) == 0);
  ASSERT(ctx_def->fields[2].type == TYPE_STR);
  ASSERT(ctx_def->fields[2].is_mutable == false);
  ASSERT(memcmp(ctx_def->fields[3].name, "ratio", 5) == 0);
  ASSERT(ctx_def->fields[3].type == TYPE_F64);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: duplicate ctx field name produces compile error */
static int test_ctx_duplicate_field_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "ctx i32 count = 0\n"
      "ctx str count = \"x\"", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(cr.error_message != NULL);
  ASSERT(strstr(cr.error_message, "ctx field 'count' already declared") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: duplicate with built-in pwd produces compile error */
static int test_ctx_duplicate_pwd_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;

  CompileResult cr = compile_source(
      "ctx str pwd = \"foo\"", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(cr.error_message != NULL);
  ASSERT(strstr(cr.error_message, "ctx field 'pwd' already declared") != NULL);

  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: fields from two modules via use — both registered */
static int test_ctx_fields_across_modules(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_ctx_mod";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl",
      "def lib_x 42\n"
      "ctx i32 lib_count = 0\n");
  write_temp_jacl(dir, "main.jacl",
      "use \"lib.jacl\" {lib_x}\n"
      "ctx str app_name = \"test\"\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_ctx_mod/main.jacl", &arena, &intern, &heap);

  ASSERT_U32_EQ(pr.error_count, 0);
  ASSERT(pr.struct_registry != NULL);
  ASSERT(pr.struct_registry->ctx_type_idx != 0);

  uint32_t ctx_idx = pr.struct_registry->ctx_type_idx;
  StructTypeDef* ctx_def = pr.struct_registry->defs[ctx_idx];
  ASSERT(ctx_def != NULL);
  /* Should have 3 fields: pwd (built-in) + lib_count (from lib) + app_name (from main) */
  ASSERT_U32_EQ(ctx_def->field_count, 3);
  ASSERT(memcmp(ctx_def->fields[0].name, "pwd", 3) == 0);
  ASSERT(memcmp(ctx_def->fields[1].name, "lib_count", 9) == 0);
  ASSERT(ctx_def->fields[1].type == TYPE_I32);
  ASSERT(memcmp(ctx_def->fields[2].name, "app_name", 8) == 0);
  ASSERT(ctx_def->fields[2].type == TYPE_STR);

  const char* files[] = { "main.jacl", "lib.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: duplicate ctx field across modules produces compile error */
static int test_ctx_duplicate_across_modules_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool); gc__current_heap = &heap;
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_ctx_dup";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl",
      "def lib_x 42\n"
      "ctx i32 count = 0\n");
  write_temp_jacl(dir, "main.jacl",
      "use \"lib.jacl\" {lib_x}\n"
      "ctx str count = \"x\"\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_ctx_dup/main.jacl", &arena, &intern, &heap);

  ASSERT(pr.error_count > 0);
  ASSERT(pr.error_message != NULL);
  ASSERT(strstr(pr.error_message, "ctx field 'count' already declared") != NULL);

  const char* files[] = { "main.jacl", "lib.jacl" };
  cleanup_jacl_dir(dir, files, 2);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
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
    { "compile_long_string_heap",    test_compile_long_string_heap },
    { "compile_multi_statement",     test_compile_multi_statement },
    { "compile_multi_semicolons",    test_compile_multi_semicolons },
    { "compile_single_no_pop",       test_compile_single_no_pop },
    { "compile_halt_at_end",         test_compile_halt_at_end },
    { "compile_empty",               test_compile_empty },
    { "compile_const_pool_indices",  test_compile_const_pool_indices },
    { "compile_line_tracking",       test_compile_line_tracking },
    { "compile_parse_error_propagated", test_compile_parse_error_propagated },
    /* US-004: Arithmetic builtins (compiler) */
    { "compile_add",                 test_compile_add },
    { "compile_sub",                 test_compile_sub },
    { "compile_mul_div_mod",         test_compile_mul_div_mod },
    { "compile_unary_neg",           test_compile_unary_neg },
    { "compile_nested_arithmetic",   test_compile_nested_arithmetic },
    { "compile_f32_arithmetic",      test_compile_f32_arithmetic },
    /* US-005: Comparison builtins (compiler) */
    { "compile_eq",                  test_compile_eq },
    { "compile_lt_gt_le_ge",         test_compile_lt_gt_le_ge },
    { "compile_f32_comparison",      test_compile_f32_comparison },
    /* US-006: Print builtin (compiler) */
    { "compile_print",               test_compile_print },
    { "compile_print_no_args",       test_compile_print_no_args },
    { "compile_print_nested",        test_compile_print_nested },
    /* US-007: Global environment, def, and variable references (compiler) */
    { "compile_def",                 test_compile_def },
    { "compile_def_expr",            test_compile_def_expr },
    { "compile_def_wrong_argc",      test_compile_def_wrong_argc },
    { "compile_def_non_string_name", test_compile_def_non_string_name },
    { "compile_var_ref",             test_compile_var_ref },
    /* US-007: Integration tests (compile + execute) */
    { "def_print_var",               test_def_print_var },
    { "def_expr_print_var",          test_def_expr_print_var },
    { "var_true_false_nil",          test_var_true_false_nil },
    { "var_undefined",               test_var_undefined },
    /* US-008: Nested subcommands and multi-statement programs */
    { "nested_print_add_mul",        test_nested_print_add_mul },
    { "nested_both_args",            test_nested_both_args },
    { "deeply_nested",               test_deeply_nested },
    { "multi_def_print",             test_multi_def_print },
    { "multi_semicolons_def_print",  test_multi_semicolons_def_print },
    { "intermediate_results_popped", test_intermediate_results_popped },
    { "final_result_available",      test_final_result_available },
    /* US-009: Runtime error reporting */
    { "runtime_error_add_bool_i32",  test_runtime_error_add_bool_i32 },
    { "runtime_error_lt_bool_i32",   test_runtime_error_lt_bool_i32 },
    { "runtime_error_print_undefined", test_runtime_error_print_undefined },
    { "runtime_error_line_number",   test_runtime_error_line_number },
    { "runtime_error_unknown_command", test_runtime_error_unknown_command },
    { "compile_long_string_no_error_msg", test_compile_long_string_no_error_msg },
    { "vm_returns_runtime_error",    test_vm_returns_runtime_error },
    /* US-004 (M4): Compiler local variable resolution */
    { "local_def_in_block",          test_local_def_in_block },
    { "local_not_visible_outside",   test_local_not_visible_outside },
    { "local_shadowing",             test_local_shadowing },
    { "local_nested_scopes",         test_local_nested_scopes },
    { "local_same_scope_shadow",     test_local_same_scope_shadow },
    { "local_top_level_still_global", test_local_top_level_still_global },
    { "local_top_level_var_ref_global", test_local_top_level_var_ref_global },
    { "local_max_exceeded",          test_local_max_exceeded },
    { "local_empty_block",           test_local_empty_block },
    /* US-005 (M4): Compile and execute if conditional */
    { "if_then_branch",              test_if_then_branch },
    { "if_else_branch",              test_if_else_branch },
    { "if_no_else_nil",              test_if_no_else_nil },
    { "if_nested",                   test_if_nested },
    { "if_nil_falsy",                test_if_nil_falsy },
    { "if_zero_truthy",              test_if_zero_truthy },
    { "if_empty_string_truthy",      test_if_empty_string_truthy },
    { "if_as_expression",            test_if_as_expression },
    { "if_wrong_argc_1",             test_if_wrong_argc_1 },
    { "if_wrong_argc_4",             test_if_wrong_argc_4 },
    { "if_then_not_block",           test_if_then_not_block },
    { "if_else_not_block",           test_if_else_not_block },
    /* US-006 (M4): Compile and execute proc definition */
    { "proc_basic_call",             test_proc_basic_call },
    { "proc_print_param",            test_proc_print_param },
    { "proc_zero_params",            test_proc_zero_params },
    { "proc_implicit_return",        test_proc_implicit_return },
    { "proc_multi_stmt_body",        test_proc_multi_stmt_body },
    { "proc_redefine",               test_proc_redefine },
    { "proc_wrong_argc",             test_proc_wrong_argc },
    { "proc_call_arg_mismatch",      test_proc_call_arg_mismatch },
    { "proc_def_returns_nil",        test_proc_def_returns_nil },
    /* Syntax Redesign US-003: New proc syntax */
    { "proc_new_basic",              test_proc_new_syntax_basic },
    { "proc_new_typed",              test_proc_new_syntax_typed },
    { "proc_new_zero_params",        test_proc_new_syntax_zero_params },
    { "proc_new_multi_stmt",         test_proc_new_syntax_multi_stmt },
    { "proc_new_closure",            test_proc_new_syntax_closure },
    { "proc_new_implicit_ret",       test_proc_new_syntax_implicit_return },
    /* US-007 (M4): Compile and execute while loop */
    { "while_zero_iterations",       test_while_zero_iterations },
    { "while_nil_falsy",             test_while_nil_falsy },
    { "while_iterative_sum",         test_while_iterative_sum },
    { "while_returns_nil",           test_while_returns_nil },
    { "while_wrong_argc_1",          test_while_wrong_argc_1 },
    { "while_wrong_argc_3",          test_while_wrong_argc_3 },
    { "while_body_not_block",        test_while_body_not_block },
    { "while_countdown",             test_while_countdown },
    /* US-008 (M4): Closures and upvalue capture */
    { "closure_capture_local",       test_closure_capture_local },
    { "closure_independent_captures", test_closure_independent_captures },
    { "closure_nested_3_levels",     test_closure_nested_3_levels },
    { "closure_capture_multiple",    test_closure_capture_multiple },
    { "closure_not_global",          test_closure_not_global },
    /* US-009 (M4): Recursion */
    { "recursion_factorial",         test_recursion_factorial },
    { "recursion_fibonacci",         test_recursion_fibonacci },
    { "recursion_depth_limit",       test_recursion_depth_limit },
    /* US-002 (M12/M13): Closure capturing mut pinned in spawn */
    { "spawn_captures_mut_pinned",   test_spawn_captures_mut_pinned },
    { "spawn_local_mut_not_pinned",  test_spawn_local_mut_not_pinned },
    { "spawn_captures_def_not_pinned", test_spawn_captures_def_not_pinned },
    /* US-003: Transitive capture — closure capturing closure with mut */
    { "spawn_transitive_capture_pinned", test_spawn_transitive_capture_pinned },
    { "spawn_transitive_varref_pinned",  test_spawn_transitive_varref_pinned },
    { "spawn_transitive_def_not_pinned", test_spawn_transitive_def_not_pinned },
    /* US-004 (M14): Path resolution and circular import detection */
    { "module_resolve_path",             test_module_resolve_path },
    { "module_resolve_path_not_found",   test_module_resolve_path_not_found },
    { "import_stack_basic",              test_import_stack_basic },
    { "import_stack_circular_detect",    test_import_stack_circular_detect },
    { "import_stack_chain_str",          test_import_stack_chain_str },
    /* US-005 (M14): Underscore-prefix privacy */
    { "module_is_private",               test_module_is_private },
    { "use_private_compile_err",         test_use_private_compile_error },
    /* US-006 (M14): Module compilation — export population */
    { "module_populate_exports",         test_module_populate_exports },
    { "compile_module_basic",            test_compile_module_basic },
    { "compile_module_mutable_export",   test_compile_module_mutable_export },
    { "compile_module_private_excluded", test_compile_module_private_excluded },
    { "compile_module_cached",           test_compile_module_cached },
    { "compile_module_typed_exports",    test_compile_module_typed_exports },
    /* US-007 (M14): Import name resolution with strict typing */
    { "import_registers_globals",        test_import_registers_globals },
    { "import_unknown_export_error",     test_import_unknown_export_error },
    { "import_conflict_error",           test_import_conflict_error },
    { "import_typed_propagation",        test_import_typed_propagation },
    { "import_arity_check",             test_import_arity_check },
    { "import_type_check",              test_import_type_check },
    { "import_mutable_flag",            test_import_mutable_flag },
    /* US-008 (M14): Mutable imports as boxes */
    { "module_mut_emits_box",            test_module_mut_emits_box },
    { "nonmodule_mut_no_box",            test_nonmodule_mut_no_box },
    { "module_mutable_import_is_box",    test_module_mutable_import_is_box },
    { "module_set_emits_reset",          test_module_set_emits_reset },
    { "module_set_immutable_errors",     test_module_set_immutable_errors },
    /* US-009 (M14): Compiler API — multi-file entry point */
    { "compile_program_single_file",     test_compile_program_single_file },
    { "compile_program_with_dep",        test_compile_program_with_dep },
    { "compile_program_topo_order",      test_compile_program_topo_order },
    { "compile_program_not_found",       test_compile_program_not_found },
    { "single_file_api_unchanged",       test_single_file_api_unchanged },
    { "compile_program_root_exports",    test_compile_program_root_exports },
    /* US-010 (M14): Module initialization and runtime execution */
    { "exec_program_basic",              test_exec_program_basic },
    { "exec_program_namespace",          test_exec_program_namespace },
    { "exec_program_side_effects_once",  test_exec_program_side_effects_once },
    { "exec_program_dep_error",          test_exec_program_dep_error },
    { "exec_program_mutable_import",     test_exec_program_mutable_import },
    { "exec_single_file_unchanged",      test_exec_single_file_unchanged },
    /* US-002 (Struct): Struct type registration */
    { "defstruct_basic",                 test_defstruct_basic },
    { "defstruct_duplicate_name",        test_defstruct_duplicate_name },
    { "defstruct_duplicate_field",       test_defstruct_duplicate_field },
    { "defstruct_undefined_field_type",  test_defstruct_undefined_field_type },
    { "defstruct_nested_type",           test_defstruct_nested_type },
    { "defstruct_type_annotation",       test_defstruct_type_annotation },
    { "defstruct_all_field_types",       test_defstruct_all_field_types },
    { "defstruct_ref_type_rejected",     test_defstruct_ref_type_rejected },
    { "defstruct_forward_ref_error",     test_defstruct_forward_ref_error },
    /* US-003 (Struct): Struct instantiation */
    { "struct_new_basic",                test_struct_new_basic },
    { "struct_new_arity_error",          test_struct_new_arity_error },
    { "struct_new_type_error",           test_struct_new_type_error },
    { "struct_new_runtime",              test_struct_new_runtime },
    /* US-003 (Value Types): Width tracking and inline construction */
    { "struct_width_tracking",           test_struct_width_tracking },
    { "struct_width_large",              test_struct_width_large },
    { "struct_width_nested",             test_struct_width_nested },
    { "struct_new_inline_opcode",        test_struct_new_inline_opcode },
    { "struct_new_inline_data",          test_struct_new_inline_data_integrity },
    { "struct_new_inline_wide",          test_struct_new_inline_wide },
    { "struct_new_inline_padding",       test_struct_new_inline_padding_zeroed },
    { "struct_heap_path_unchanged",      test_struct_new_heap_path_unchanged },
    /* US-004 (Value Types): Wide locals — state machine field storage */
    { "sm_field_width_default",          test_sm_state_field_width_default },
    { "sm_field_width_struct",           test_sm_state_field_width_struct },
    { "sm_walk_locals_struct_width",     test_sm_walk_locals_struct_width },
    { "sm_optimize_atomic_wide",        test_sm_optimize_atomic_wide_field },
    { "sm_wide_field_opcodes",           test_sm_wide_field_opcodes },
    { "sm_struct_gen_heap_path",         test_sm_struct_generator_heap_path },
    { "sm_total_slots_allocation",       test_sm_total_slots_allocation },
    /* US-005 (Value Types): Inline field access — byte-offset addressing */
    { "struct_inline_get_basic",         test_struct_inline_get_basic },
    { "struct_inline_set_basic",         test_struct_inline_set_basic },
    { "struct_inline_nested_get",        test_struct_inline_nested_get },
    { "struct_inline_materialize",       test_struct_inline_materialize },
    { "struct_inline_wide_types",        test_struct_inline_wide_field_types },
    { "struct_inline_preserves_locals",  test_struct_inline_preserves_other_locals },
    /* US-006: Struct arguments — pass by value */
    { "struct_pass_by_value_basic",      test_struct_pass_by_value_basic },
    { "struct_pass_by_value_recursive",  test_struct_pass_by_value_recursive },
    { "struct_pass_by_value_chain",      test_struct_pass_by_value_chain },
    /* US-007: Struct returns — pass by value */
    { "struct_return_basic",             test_struct_return_basic },
    { "struct_return_nested_calls",      test_struct_return_nested_calls },
    { "struct_return_value_isolation",   test_struct_return_value_isolation },
    /* US-008: Closure capture of structs */
    { "struct_closure_capture_basic",       test_struct_closure_capture_basic },
    { "struct_closure_capture_isolation",   test_struct_closure_capture_isolation },
    { "struct_closure_capture_materialize", test_struct_closure_capture_materialize },
    /* US-010: Box struct values and deref/reset/swap */
    { "struct_box_roundtrip",              test_struct_box_roundtrip },
    { "struct_box_reset",                  test_struct_box_reset },
    { "struct_box_swap",                   test_struct_box_swap },
    { "struct_box_deref_isolation",        test_struct_box_deref_isolation },
    { "struct_atom_rejected",              test_struct_atom_rejected },
    /* US-011: box? type checking with flow typing */
    { "box_typed_check_struct",            test_box_typed_check_struct },
    { "box_typed_check_dyn",               test_box_typed_check_dyn },
    { "box_flow_typing_unbox",             test_box_flow_typing_unbox },
    { "unbox_outside_guard_rejected",      test_unbox_outside_guard_rejected },
    { "box_typed_unknown_type_rejected",   test_box_typed_unknown_type_rejected },
    /* US-012: Generic operations on boxed structs */
    { "struct_box_equality",               test_struct_box_equality },
    { "struct_box_hashing",                test_struct_box_hashing },
    { "struct_box_stringify",              test_struct_box_stringify },
    { "struct_box_stringify_dyn",           test_struct_box_stringify_dyn },
    { "struct_box_map_key",                test_struct_box_map_key },
    /* US-013: Byte-level equality and hashing for unboxed structs */
    { "struct_inline_eq_same",             test_struct_inline_eq_same },
    { "struct_inline_eq_different",         test_struct_inline_eq_different },
    { "struct_inline_hash_equal",           test_struct_inline_hash_equal },
    { "struct_inline_hash_different",       test_struct_inline_hash_different },
    { "struct_heap_equality",              test_struct_heap_equality },
    { "struct_eq_padding_zeroed",          test_struct_eq_padding_zeroed },
    { "struct_hash_generic",               test_struct_hash_generic },
    { "dual_path_value_type_inline",       test_dual_path_value_type_inline },
    /* set n->field arrow syntax */
    { "set_arrow_inline",                  test_set_arrow_inline },
    { "set_arrow_chained",                 test_set_arrow_chained },
    /* US-004 (Struct): Field access */
    { "struct_get_basic",                test_struct_get_basic },
    { "struct_get_unknown_field",        test_struct_get_unknown_field },
    { "struct_get_nested",               test_struct_get_nested },
    /* US-005 (Struct): Field mutation */
    { "struct_set_basic",                test_struct_set_basic },
    { "struct_set_unknown_field",        test_struct_set_unknown_field },
    { "struct_set_type_mismatch",        test_struct_set_type_mismatch },
    { "struct_set_preserves_other",      test_struct_set_preserves_other_fields },
    /* US-006 (Struct): Boxing and dyn interop */
    { "struct_dyn_rejected",             test_struct_dyn_rejected },
    { "struct_in_vec",                   test_struct_in_vec },
    /* US-008 (Struct): Inline anonymous struct types */
    { "inline_struct_runtime",           test_inline_struct_runtime },
    { "inline_struct_basic",             test_inline_struct_basic },
    { "inline_struct_equivalence",       test_inline_struct_equivalence },
    { "inline_struct_nested",            test_inline_struct_nested },
    /* US-009 (Struct): Module export/import */
    { "struct_module_import",            test_struct_module_import },
    /* Syntax Redesign US-004: New struct syntax */
    { "struct_new_syntax_basic",         test_struct_new_syntax_basic },
    { "struct_new_syntax_runtime",       test_struct_new_syntax_runtime },
    { "struct_new_syntax_all_types",     test_struct_new_syntax_all_types },
    { "struct_new_syntax_nested",        test_struct_new_syntax_nested },
    { "struct_new_syntax_construct",     test_struct_new_syntax_construct_and_access },
    { "struct_new_syntax_dup_field",     test_struct_new_syntax_duplicate_field },
    /* Syntax Redesign US-005: New if/elif/else and while syntax */
    { "if_new_then_branch",            test_if_new_then_branch },
    { "if_new_else_branch",            test_if_new_else_branch },
    { "if_new_no_else_nil",            test_if_new_no_else_nil },
    { "if_new_elif",                   test_if_new_elif },
    { "if_new_elif_else",             test_if_new_elif_else },
    { "if_new_elif_chain",            test_if_new_elif_chain },
    { "if_new_as_expression",          test_if_new_as_expression },
    { "while_new_iterative_sum",       test_while_new_iterative_sum },
    { "while_new_returns_nil",         test_while_new_returns_nil },
    { "if_while_combined_new",         test_if_while_combined_new },
    /* Syntax Redesign US-006: for command */
    { "for_implicit_it",               test_for_implicit_it },
    { "for_explicit_binding",          test_for_explicit_binding },
    { "for_hof_callback",             test_for_hof_callback },
    { "for_empty_vec",                test_for_empty_vec },
    { "for_variable_collection",      test_for_variable_collection },
    { "for_returns_nil",              test_for_returns_nil },
    { "for_upvalue_capture",          test_for_upvalue_capture },
    { "each_still_works",            test_each_still_works },
    /* For-loop PRD US-001: Break as control flow */
    { "break_while_basic",               test_break_while_basic },
    { "break_while_with_value",          test_break_while_with_value },
    { "break_while_nil_value",           test_break_while_nil_value },
    { "break_nested_loops",              test_break_nested_loops },
    { "break_outside_loop_error",        test_break_outside_loop_error },
    { "break_bare_outside_loop_error",   test_break_bare_outside_loop_error },
    { "break_bare_with_value",           test_break_bare_with_value },
    { "continue_while_basic",            test_continue_while_basic },
    { "continue_outside_loop_error",     test_continue_outside_loop_error },
    /* For-loop PRD US-002: Continue as control flow */
    { "for_empty_collection",            test_for_empty_collection },
    { "continue_for_basic",              test_continue_for_basic },
    { "continue_for_bare",              test_continue_for_bare },
    { "continue_nested_loops",           test_continue_nested_loops },
    { "break_for_basic",                 test_break_for_basic },
    { "break_for_with_value",            test_break_for_with_value },
    { "continue_mixed_nested",           test_continue_mixed_nested },
    /* For-loop PRD US-003: Inline block-form for bodies — return semantics */
    { "return_from_for_block",           test_return_from_for_block },
    { "return_value_from_for_block",     test_return_value_from_for_block },
    { "return_from_hof_for",             test_return_from_hof_for },
    { "return_bare_from_for_block",      test_return_bare_from_for_block },
    { "return_nil_from_for_block",       test_return_nil_from_for_block },
    /* For-loop PRD US-004: C-style for loop */
    { "for_cstyle_basic",                test_for_cstyle_basic },
    { "for_cstyle_break",                test_for_cstyle_break },
    { "for_cstyle_continue",             test_for_cstyle_continue },
    { "for_cstyle_nested",               test_for_cstyle_nested },
    { "for_cstyle_scope",                test_for_cstyle_scope },
    /* For-loop PRD US-005: Comprehensive e2e test suite */
    { "break_for_nil_value",             test_break_for_nil_value },
    { "break_nested_for_loops",          test_break_nested_for_loops },
    { "continue_bare_outside_loop_error", test_continue_bare_outside_loop_error },
    { "for_cstyle_return",               test_for_cstyle_return },
    { "for_cstyle_break_value",          test_for_cstyle_break_value },
    /* Syntax Redesign US-007: Builtin renames (set/reset/swap) */
    { "set_without_bang",             test_set_without_bang },
    { "set_bang_still_works",         test_set_bang_still_works },
    { "reset_without_bang",           test_reset_without_bang },
    { "reset_bang_still_works",       test_reset_bang_still_works },
    { "swap_without_bang",            test_swap_without_bang },
    { "swap_bang_still_works",        test_swap_bang_still_works },
    { "set_global_without_bang",      test_set_global_without_bang },
    { "set_multiple_reassign",        test_set_multiple_reassign },
    /* Syntax Redesign US-008: Binding operators (=, :, ::) */
    { "bind_equals_def",              test_bind_equals_def },
    { "bind_typed_equals",            test_bind_typed_equals },
    { "bind_colon_mut",               test_bind_colon_mut },
    { "bind_typed_colon",             test_bind_typed_colon },
    { "bind_double_colon_set",        test_bind_double_colon_set },
    { "bind_mixed_with_commands",     test_bind_mixed_with_commands },
    { "bind_in_block",                test_bind_in_block },
    { "bind_expr_values",             test_bind_expr_values },
    /* Syntax Redesign US-009: Arrow field access (->) */
    { "arrow_basic_get",              test_arrow_basic_get },
    { "arrow_chained_get",            test_arrow_chained_get },
    { "arrow_on_expr_result",         test_arrow_on_expr_result },
    { "arrow_in_infix_mode",          test_arrow_in_infix_mode },
    { "arrow_in_bracket_cmd",         test_arrow_in_bracket_cmd },
    { "arrow_old_dot_compat",         test_arrow_old_dot_compat },
    /* Syntax Redesign US-010: Pipe threading (|) */
    { "pipe_basic",                    test_pipe_basic },
    { "pipe_multi_stage",              test_pipe_multi_stage },
    { "pipe_with_builtins",            test_pipe_with_builtins },
    { "pipe_in_block",                 test_pipe_in_block },
    { "pipe_with_vars",                test_pipe_with_vars },
    { "pipe_in_proc_body",             test_pipe_in_proc_body },
    /* Syntax Redesign US-011: Lambda shorthand (\) */
    { "lambda_basic_call",             test_lambda_basic_call },
    { "lambda_arithmetic",             test_lambda_arithmetic },
    { "lambda_each_compat",            test_lambda_each_compat },
    { "lambda_upvalue",                test_lambda_upvalue },
    { "lambda_as_value",               test_lambda_as_value },
    { "lambda_in_pipe",                test_lambda_in_pipe },
    /* Syntax Redesign US-012: $(expr) interpolation & line continuation */
    { "dollar_paren_existing",         test_dollar_paren_compile_existing },
    { "line_continuation",             test_line_continuation_compile },
    { "blank_lines",                   test_blank_lines_compile },
    /* Shell Interop US-003: Compiler emits code for !cmd */
    { "shell_cmd_compile_simple",      test_shell_cmd_compile_simple },
    { "shell_cmd_compile_vars",        test_shell_cmd_compile_vars },
    { "shell_cmd_compile_splat",       test_shell_cmd_compile_splat },
    { "shell_cmd_no_exec_error",       test_shell_cmd_no_exec_error },
    { "shell_cmd_with_exec_prelude",   test_shell_cmd_with_exec_prelude },
    { "shell_cmd_quoted_name",         test_shell_cmd_quoted_name },
    /* Implicit Context US-003: Ctx field collection and struct registration */
    { "ctx_builtin_pwd",              test_ctx_builtin_pwd },
    { "ctx_single_user_field",        test_ctx_single_user_field },
    { "ctx_multiple_fields",          test_ctx_multiple_fields },
    { "ctx_duplicate_field_error",    test_ctx_duplicate_field_error },
    { "ctx_duplicate_pwd_error",      test_ctx_duplicate_pwd_error },
    { "ctx_fields_across_modules",    test_ctx_fields_across_modules },
    { "ctx_dup_across_modules_error", test_ctx_duplicate_across_modules_error },
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
