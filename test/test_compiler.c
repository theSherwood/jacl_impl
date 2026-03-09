#include "test_helpers.h"
#include "../src/jacl.c"

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

  /* Bytecode: OP_GET_GLOBAL u16(0) OP_CALL 0 OP_HALT
     ($x at statement position desugars to [$x] — zero-arg call) */
  ASSERT_U32_EQ(cr.chunk.code_count, 6);
  ASSERT_INT_EQ(cr.chunk.code[0], OP_GET_GLOBAL);
  ASSERT_INT_EQ(cr.chunk.code[1], 0);
  ASSERT_INT_EQ(cr.chunk.code[2], 0);
  ASSERT_INT_EQ(cr.chunk.code[3], OP_CALL);
  ASSERT_INT_EQ(cr.chunk.code[4], 0);
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
