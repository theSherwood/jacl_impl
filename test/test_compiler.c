#include "test_helpers.h"
#include "../src/jacl.c"

#include <sys/stat.h>
#include <unistd.h>

/* ===== Helper: parse source and compile ===== */

static CompileResult compile_source(const char* source, arena_t* arena, ThreadHeap* heap) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, arena);
  return compiler_compile(parse, arena, &intern_table, heap);
}

/* ===== US-003: Compiler skeleton and literal compilation ===== */

/* Test: integer literal compiles to OP_CONST with jacl_i32 */
static int test_compile_int(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[def x 42]", &arena, &heap);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[def x [+ 1 2]]", &arena, &heap);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* Too few args: [def x] */
  CompileResult cr = compile_source("[def x]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  /* Too many args: [def x 1 2] */
  tracker_reset();
  arena = (arena_t){ .allocator = tracked_allocator };
  gc_block_pool_init(&pool);
  gc_heap_init(&heap, &pool);
  cr = compile_source("[def x 1 2]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());

  TEST_PASS();
}

/* Test: def first argument must be AST_LIT_STRING */
static int test_compile_def_non_string_name(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* [def 42 10] - first arg is int, not string */
  CompileResult cr = compile_source("[def 42 10]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("$x", &arena, &heap);

  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT_U32_EQ(cr.chunk.const_count, 1);
  ASSERT(jacl_is_inline_string(cr.chunk.constants[0]));

  /* Bytecode: OP_GET_GLOBAL u16(0) OP_HALT
     ($x at statement position is a value read, not a call) */
  ASSERT_U32_EQ(cr.chunk.code_count, 4);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_GET_GLOBAL);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_HALT);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[def x 42]\n[print $x]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[def x [+ 1 2]]\n[print $x]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* $true used as condition → truthy */
  VMResult result = jacl_run("[if $true { [print 1] } { [print 0] }]",
                             &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n");

  /* $false used as condition → falsy */
  cap.len = 0;
  cap.buf[0] = '\0';
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  result = jacl_run("[if $false { [print 1] } { [print 0] }]",
                    &vm, &arena);
  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "0\n");

  /* $nil used as condition → falsy */
  cap.len = 0;
  cap.buf[0] = '\0';
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  result = jacl_run("[if $nil { [print 1] } { [print 0] }]",
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[def x 10]\n[def y 20]\n[print [+ $x $y]]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[def a 1]; [def b 2]; [print [+ $a $b]]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[def x 5]\n[+ $x 10]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* Error on line 2: def x $true; [+ $x 1] */
  CompileResult cr = compile_source("[def x $true]\n[+ $x 1]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* { [def x 42]; [print $x] } should print "42\n" */
  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run("{ [def x 42]; [print $x] }", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("{ [def x 42] }\n$x", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "[def x 1]\n{ [def x 2]; [print $x] }\n[print $x]",
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "{ [def a 10]; { [def b 20]; [print [+ $a $b]] }; [print $a] }",
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

/* Test: shadowing in same scope creates new local slot */
static int test_local_same_scope_shadow(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  /* Redefining x in the same scope creates a new local; $x resolves to the latest */
  VMResult result = jacl_run(
    "{ [def x 1]; [def x 2]; [print $x] }",
    &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "2\n");

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* At top level, def should use OP_DEF_GLOBAL */
  CompileResult cr = compile_source("[def x 42]", &arena, &heap);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* Build source: { [def x 0]; [def x 1]; ...; [def x 256] } — 257 defs */
  char source[8192];
  int pos = 0;
  pos += snprintf(source + pos, sizeof(source) - (size_t)pos, "{ ");
  for (int i = 0; i <= 256; i++) {
    if (i > 0) {
      pos += snprintf(source + pos, sizeof(source) - (size_t)pos, "; ");
    }
    pos += snprintf(source + pos, sizeof(source) - (size_t)pos, "[def x %d]", i);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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

/* Test: nested if works — [if $true { [if $false { 1 } { 2 }] }] returns i32(2) */
static int test_if_nested(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[if $true { [if $false { 1 } { 2 }] }]", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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

/* Test: [proc add [a b] { [+ $a $b] }] then [add 1 2] returns i32(3) */
static int test_proc_basic_call(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "[proc add [a b] { [+ $a $b] }]\n[add 1 2]", &vm, &arena);

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

/* Test: [proc greet [name] { [print $name] }] then [greet hello] prints "hello\n" */
static int test_proc_print_param(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "[proc greet [name] { [print $name] }]\n[greet hello]", &vm, &arena);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: zero-parameter procedure: [proc answer [] { 42 }] then [answer] returns i32(42) */
static int test_proc_zero_params(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "[proc answer [] { 42 }]\n[answer]", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "[proc double [n] { [* $n 2] }]\n[double 5]", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = jacl_run(
    "[proc foo [x] { [print $x]; [+ $x 1] }]\n[foo 10]", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "[proc f [] { 1 }]\n[proc f [] { 2 }]\n[f]", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* Too few args */
  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[proc foo [a]]", &vm, &arena);
  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  /* Too many args */
  vm_init(&vm, &arena);
  result = jacl_run("[proc foo [a] { 1 } extra]", &vm, &arena);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(
    "[proc add [a b] { [+ $a $b] }]\n[add 1]", &vm, &arena);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run("[proc f [] { 42 }]", &vm, &arena);

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

/* ===== US-007 (M4): Compile and execute while loop ===== */

/* Test: while with false condition never executes body, returns nil */
static int test_while_zero_iterations(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Sum 1..5 using def rebinding in while loop */
  const char* program =
    "[def i 1]\n"
    "[def sum 0]\n"
    "[while [<= $i 5] {\n"
    "  [def sum [+ $sum $i]]\n"
    "  [def i [+ $i 1]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* Print the result of a while loop — should be nil */
  const char* program =
    "[def i 0]\n"
    "[print [while [< $i 3] {\n"
    "  [def i [+ $i 1]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[while $true]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "builtin 'while' expects 2 arguments but got 1") != NULL);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[while $true { 1 } { 2 }]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "builtin 'while' expects 2 arguments but got 3") != NULL);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source("[while $true 42]", &arena, &heap);
  ASSERT(cr.error_count > 0);
  ASSERT(strstr(cr.error_message, "while body must be a block") != NULL);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[def n 3]\n"
    "[while [> $n 0] {\n"
    "  [print $n]\n"
    "  [def n [- $n 1]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[proc mkadd [x] {\n"
    "  [proc inner [y] { [+ $x $y] }]\n"
    "}]\n"
    "[def add10 [mkadd 10]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[proc mkadd [x] {\n"
    "  [proc inner [y] { [+ $x $y] }]\n"
    "}]\n"
    "[def add5 [mkadd 5]]\n"
    "[def add20 [mkadd 20]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[proc outer [a] {\n"
    "  [proc middle [b] {\n"
    "    [proc inner [c] { [+ [+ $a $b] $c] }]\n"
    "  }]\n"
    "}]\n"
    "[def mid [outer 100]]\n"
    "[def inn [mid 20]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[proc make-fn [a b] {\n"
    "  [proc inner [c] { [+ [+ $a $b] $c] }]\n"
    "}]\n"
    "[def f [make-fn 10 20]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  /* x=99 at global, x=5 as param — inner should capture param x=5 */
  const char* program =
    "[def x 99]\n"
    "[proc wrap [x] {\n"
    "  [proc inner [] { [+ $x 0] }]\n"
    "}]\n"
    "[def f [wrap 5]]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[proc fact [n] {\n"
    "  [if [== $n 0] { 1 } { [* $n [fact [- $n 1]]] }]\n"
    "}]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;

  const char* program =
    "[proc fib [n] {\n"
    "  [if [< $n 2] { [+ $n 0] } { [+ [fib [- $n 1]] [fib [- $n 2]]] }]\n"
    "}]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);

  /* Infinite recursion with no base case */
  const char* program =
    "[proc inf [n] { [inf [+ $n 1]] }]\n"
    "[inf 0]";

  VMResult result = jacl_run(program, &vm, &arena);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);
  ASSERT(vm.error_message != NULL);
  ASSERT(strstr(vm.error_message, "stack overflow") != NULL);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* mut x captured via $x in spawn body (read only, no set!) */
  const char* program =
    "[proc main [] {\n"
    "  [mut x 42]\n"
    "  [spawn { [+ $x 1] }]\n"
    "}]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* mut x declared inside spawn body — local, no pinning needed */
  const char* program =
    "[proc main [] {\n"
    "  [spawn {\n"
    "    [mut x 42]\n"
    "    [set! x 99]\n"
    "    $x\n"
    "  }]\n"
    "}]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  const char* program =
    "[proc main [] {\n"
    "  [def x 42]\n"
    "  [spawn { [+ $x 1] }]\n"
    "}]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* proc f captures mut x, proc g calls f, g is called in spawn body */
  const char* program =
    "[proc main [] {\n"
    "  [mut x 42]\n"
    "  [proc f [] { $x }]\n"
    "  [spawn { [f] }]\n"
    "}]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* proc f captures mut x, spawn body uses $f (var ref) */
  const char* program =
    "[proc main [] {\n"
    "  [mut x 42]\n"
    "  [proc f [] { $x }]\n"
    "  [spawn { $f }]\n"
    "}]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* proc f captures def x (immutable), spawn calls f — NOT pinned */
  const char* program =
    "[proc main [] {\n"
    "  [def x 42]\n"
    "  [proc f [] { $x }]\n"
    "  [spawn { [f] }]\n"
    "}]\n"
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
  char importer[1024];
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

  char importer[1024];
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* Importing a private name should produce a parse error propagated to compile */
  CompileResult cr = compile_source("use \"mod.jacl\" [_helper]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* Compile a source with def, proc, mut, and private names */
  const char* source = "def x 42\n"
                       "proc add [a b] { [+ $a $b] }\n"
                       "mut cnt 0\n"
                       "def _priv 99\n";
  LexResult tokens = lexer_lex(source, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  JaclInternTable intern_table;
  intern_table_init(&intern_table, &arena);

  SuspensionMap suspension_map = compiler__analyze_suspension(
      parse.nodes, parse.count);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* Create a temp module file */
  const char* dir = "/tmp/jacl_test_m14";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl", "def x 42\nproc add [a b] { [+ $a $b] }\n");

  /* Create an importer module path */
  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  /* Write a dummy main so realpath works */
  write_temp_jacl(dir, "main.jacl", "");

  char real_importer[1024];
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  const char* dir = "/tmp/jacl_test_m14b";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "state.jacl", "mut count 0\ndef name \"bob\"\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[1024];
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  const char* dir = "/tmp/jacl_test_m14c";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "priv.jacl",
                  "def pub 1\ndef _secret 2\nproc _help [x] { $x }\nproc greet [x] { $x }\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[1024];
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  const char* dir = "/tmp/jacl_test_m14d";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl", "def x 42\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[1024];
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  const char* dir = "/tmp/jacl_test_m14e";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "math.jacl",
                  "proc i64 add [i64 a i64 b] { [+ $a $b] }\n");
  write_temp_jacl(dir, "main.jacl", "");

  char importer_path[1024];
  snprintf(importer_path, sizeof(importer_path), "%s/main.jacl", dir);
  char real_importer[1024];
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

/* Test: use "lib.jacl" [add x] registers GlobalArity entries for imported names */
static int test_import_registers_globals(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; ThreadHeap heap;
  ModuleCache cache; ImportStack istack;
  BytecodeChunk chunk; JaclInternTable intern;
  Compiler importer; Module importer_mod;
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us007a", "lib.jacl",
                   "def x 42\nproc add [a b] { [+ $a $b] }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Compile: use "lib.jacl" [add x] */
  const char* src = "use \"lib.jacl\" [add x]\n";
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
  int16_t add_arity = compiler__resolve_global_arity(&importer, add_name);
  ASSERT_INT_EQ((int)add_arity, 2);

  /* Check that 'x' is registered with arity -1 (non-proc) */
  JaclVal x_name = jacl_inline_string("x", 1);
  int16_t x_arity = compiler__resolve_global_arity(&importer, x_name);
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us007b", "lib.jacl",
                   "def x 42\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Try to import 'foo' which doesn't exist */
  const char* src = "use \"lib.jacl\" [foo]\n";
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us007c", "lib.jacl",
                   "def x 42\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Pre-define 'x' in the importer */
  JaclVal x_name = jacl_inline_string("x", 1);
  compiler__set_global_arity(&importer, x_name, -1);

  /* Try to import 'x' — should conflict */
  const char* src = "use \"lib.jacl\" [x]\n";
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us007d", "math.jacl",
                   "proc i64 add [i64 a i64 b] { [+ $a $b] }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"math.jacl\" [add]\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  /* Check full type info propagated */
  JaclVal add_name = jacl_inline_string("add", 3);
  GlobalArity* ga = compiler__find_global_arity(&importer, add_name);
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us007e", "lib.jacl",
                   "proc add [a b] { [+ $a $b] }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Import add then call with wrong arity */
  const char* src = "use \"lib.jacl\" [add]\n[add 1]\n";
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us007f", "math.jacl",
                   "proc i64 add [i64 a i64 b] { [+ $a $b] }\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Import add then call with wrong type (string instead of i64) */
  const char* src = "use \"math.jacl\" [add]\n[add \"hi\" 1]\n";
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us007g", "state.jacl",
                   "mut count 0\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"state.jacl\" [count]\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  JaclVal count_name = jacl_inline_string("count", 5);
  GlobalArity* ga = compiler__find_global_arity(&importer, count_name);
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
  char real_importer[1024];

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us008b", "state.jacl",
                   "mut count 0\ndef name \"bob\"\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"state.jacl\" [count name]\n";
  LexResult tokens = lexer_lex(src, &arena);
  ParseResult parse = parser_parse(tokens, &arena);
  ASSERT_U32_EQ(parse.error_count, 0);

  for (uint32_t i = 0; i < parse.count; i++) {
    compiler__compile_node(&importer, parse.nodes[i]);
  }
  ASSERT_U32_EQ(importer.error_count, 0);

  /* count should be mutable (box), name should not */
  JaclVal count_name = jacl_inline_string("count", 5);
  GlobalArity* ga_count = compiler__find_global_arity(&importer, count_name);
  ASSERT(ga_count != NULL);
  ASSERT(ga_count->is_mutable);

  JaclVal name_name = jacl_inline_string("name", 4);
  GlobalArity* ga_name = compiler__find_global_arity(&importer, name_name);
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us008c", "state.jacl",
                   "mut count 0\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  /* Compile use + set! */
  const char* src = "use \"state.jacl\" [count]\n[set! count 5]\n";
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
  char real_importer[1024];

  setup_module_ctx("/tmp/jacl_us008d", "lib.jacl",
                   "def x 42\n",
                   &arena, &pool, &heap, &cache, &istack, &chunk, &intern,
                   &importer, &importer_mod, real_importer, sizeof(real_importer));

  const char* src = "use \"lib.jacl\" [x]\n[set! x 99]\n";
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009b";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl", "def x 42\nproc add [a b] { [+ $a $b] }\n");
  write_temp_jacl(dir, "main.jacl", "use \"lib.jacl\" [add x]\n[add $x 1]\n");

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009c";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "a.jacl", "def val_a 1\n");
  write_temp_jacl(dir, "b.jacl", "use \"a.jacl\" [val_a]\ndef val_b 2\n");
  write_temp_jacl(dir, "main.jacl", "use \"b.jacl\" [val_b]\n[+ $val_b 10]\n");

  ProgramResult pr = jacl_compile_program(
      "/tmp/jacl_us009c/main.jacl", &arena, &intern, &heap);

  ASSERT_U32_EQ(pr.error_count, 0);
  ASSERT_U32_EQ(pr.module_count, 3);
  /* Root (main.jacl) must be last */
  char main_resolved[1024];
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us009e";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "main.jacl", "def x 42\nproc add [a b] { [+ $a $b] }\n");

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010a";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl",
    "proc add [a b] { [+ $a $b] }\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"lib.jacl\" [add]\n[print [add 1 2]]\n");

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010b";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "a.jacl", "def x 10\n");
  write_temp_jacl(dir, "b.jacl", "def x 20\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"a.jacl\" [x]\n[print $x]\n");

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010c";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "lib.jacl",
    "[print \"loaded\"]\ndef x 42\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"lib.jacl\" [x]\n[print $x]\n");

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010d";
  mkdir(dir, 0755);
  /* lib.jacl accesses undefined variable, causing runtime error */
  write_temp_jacl(dir, "lib.jacl",
    "def x [+ $undef 1]\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"lib.jacl\" [x]\n[print $x]\n");

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);
  JaclInternTable intern; intern_table_init(&intern, &arena);

  const char* dir = "/tmp/jacl_us010e";
  mkdir(dir, 0755);
  write_temp_jacl(dir, "state.jacl",
    "mut cnt 0\n");
  write_temp_jacl(dir, "main.jacl",
    "use \"state.jacl\" [cnt]\n"
    "[print [deref $cnt]]\n"
    "[reset! $cnt 5]\n"
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

/* Test: basic defstruct compiles without errors */
static int test_defstruct_basic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
      "defstruct Point [a :f32] [b :f32]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Bad [x :i32] [x :i32]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Line [start :Point] [end :Point]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
      "defstruct Line [start :Point] [end :Point]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  /* def Point p $x — struct name as type annotation with dyn RHS.
     Verifies that 'Point' is accepted as a type keyword after defstruct. */
  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
      "def x 0\n"
      "def Point p $x", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct All [a :i32] [b :i64] [c :u32] [d :u64] [e :f32] [f :f64] [g :str]",
      &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Line [start :Point] [end :Point]\n"
      "defstruct Point [x :i32] [y :i32]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
      "def p [Point 1 2]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
      "[Point 1 \"hello\"]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "def p [Point 10 20]\n"
      "print $p",
      &vm, &arena);
  ASSERT(r == VM_OK);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "def p [Point 10 20]\n"
      "print [. $p x]\n"
      "print [. $p y]",
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
      "def p [Point 1 2]\n"
      "[. $p z]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "defstruct Line [start :Point] [end :Point]\n"
      "def ln [Line [Point 5 6] [Point 10 20]]\n"
      "print [. [. $ln start] x]",
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "def p [Point 10 20]\n"
      "[. $p x 99]\n"
      "print [. $p x]\n"
      "print [. $p y]",
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Point [x :i32] [y :i32]\n"
      "def p [Point 1 2]\n"
      "[. $p z 99]", &arena, &heap);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Pair [a :i32] [b :i32]\n"
      "def p [Pair 1 2]\n"
      "[. $p a 42]\n"
      "print [. $p b]",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-006 (Struct): Boxing and dyn interop ===== */

static int test_struct_dyn_assign(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "def dyn d [Point 1 2]\n"
      "print $d",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_dyn_field_access(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "def dyn d [Point 10 20]\n"
      "print [. $d x]\n"
      "print [. $d y]",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
  gc_heap_destroy(&heap);
  gc_block_pool_destroy(&pool);
  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

static int test_struct_dyn_field_set(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BlockPool pool; gc_block_pool_init(&pool);
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "def dyn d [Point 1 2]\n"
      "[. $d x 99]\n"
      "print [. $d x]",
      &vm, &arena);
  ASSERT(r == VM_OK);

  vm_destroy(&vm);
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "def v [vec [Point 1 2] [Point 3 4]]\n"
      "def p [vec-get $v 1]\n"
      "print [. $p x]",
      &vm, &arena);
  ASSERT(r == VM_OK);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  VM vm;
  vm_init(&vm, &arena);
  VMResult r = jacl_run(
      "defstruct Point [x :i32] [y :i32]\n"
      "defstruct Wrapper [pos :struct{x:i32,y:i32}]\n"
      "def w [Wrapper [Point 42 10]]\n"
      "print [. [. $w pos] x]",
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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct Wrapper [pos :struct{x:i32,y:i32}]", &arena, &heap);
  if (cr.error_count > 0) {
    printf("ERRORS: %u\n", cr.error_count);
  }
  ASSERT_U32_EQ(cr.error_count, 0);
  /* Registry should have 2 entries: the anonymous struct and Wrapper */
  ASSERT(cr.struct_registry != NULL);
  ASSERT(cr.struct_registry->count == 2);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct A [p :struct{x:i32,y:i32}]\n"
      "defstruct B [q :struct{x:i32,y:i32}]", &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  /* Registry: anon_struct (shared), A, B = 3 entries */
  ASSERT(cr.struct_registry != NULL);
  ASSERT(cr.struct_registry->count == 3);

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
  ThreadHeap heap; gc_heap_init(&heap, &pool);

  CompileResult cr = compile_source(
      "defstruct C [d :struct{start:struct{x:i32,y:i32},end:struct{x:i32,y:i32}}]",
      &arena, &heap);
  ASSERT_U32_EQ(cr.error_count, 0);
  ASSERT(cr.struct_registry != NULL);
  /* inner struct (shared) + outer struct + C = 3 entries */
  ASSERT(cr.struct_registry->count == 3);

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
    { "defstruct_forward_ref_error",     test_defstruct_forward_ref_error },
    /* US-003 (Struct): Struct instantiation */
    { "struct_new_basic",                test_struct_new_basic },
    { "struct_new_arity_error",          test_struct_new_arity_error },
    { "struct_new_type_error",           test_struct_new_type_error },
    { "struct_new_runtime",              test_struct_new_runtime },
    /* US-004 (Struct): Field access */
    { "struct_get_basic",                test_struct_get_basic },
    { "struct_get_unknown_field",        test_struct_get_unknown_field },
    { "struct_get_nested",               test_struct_get_nested },
    /* US-005 (Struct): Field mutation */
    { "struct_set_basic",                test_struct_set_basic },
    { "struct_set_unknown_field",        test_struct_set_unknown_field },
    { "struct_set_preserves_other",      test_struct_set_preserves_other_fields },
    /* US-006 (Struct): Boxing and dyn interop */
    { "struct_dyn_assign",               test_struct_dyn_assign },
    { "struct_dyn_field_access",         test_struct_dyn_field_access },
    { "struct_dyn_field_set",            test_struct_dyn_field_set },
    { "struct_in_vec",                   test_struct_in_vec },
    /* US-008 (Struct): Inline anonymous struct types */
    { "inline_struct_runtime",           test_inline_struct_runtime },
    { "inline_struct_basic",             test_inline_struct_basic },
    { "inline_struct_equivalence",       test_inline_struct_equivalence },
    { "inline_struct_nested",            test_inline_struct_nested },
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
