#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

#define ARENA_IMPLEMENTATION
#include "../arena/arena.h"

#define VALUE_IMPLEMENTATION
#include "../value/value.h"

#define BYTECODE_IMPLEMENTATION
#include "./bytecode.h"

/* ===== US-001: Bytecode chunk and opcode definitions ===== */

/* Test: OpCode enum defines all M3 opcodes */
static int test_opcode_enum(void) {
  ASSERT_INT_EQ(OP_CONST,      0);
  ASSERT_INT_EQ(OP_NIL,        1);
  ASSERT_INT_EQ(OP_TRUE,       2);
  ASSERT_INT_EQ(OP_FALSE,      3);
  ASSERT_INT_EQ(OP_POP,        4);
  ASSERT_INT_EQ(OP_ADD,        5);
  ASSERT_INT_EQ(OP_SUB,        6);
  ASSERT_INT_EQ(OP_MUL,        7);
  ASSERT_INT_EQ(OP_DIV,        8);
  ASSERT_INT_EQ(OP_MOD,        9);
  ASSERT_INT_EQ(OP_NEG,       10);
  ASSERT_INT_EQ(OP_EQ,        11);
  ASSERT_INT_EQ(OP_LT,        12);
  ASSERT_INT_EQ(OP_GT,        13);
  ASSERT_INT_EQ(OP_LE,        14);
  ASSERT_INT_EQ(OP_GE,        15);
  ASSERT_INT_EQ(OP_PRINT,     16);
  ASSERT_INT_EQ(OP_DEF_GLOBAL,17);
  ASSERT_INT_EQ(OP_GET_GLOBAL,18);
  ASSERT_INT_EQ(OP_HALT,      19);
  TEST_PASS();
}

/* Test: chunk_init allocates storage and zeroes counts */
static int test_chunk_init(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;

  chunk_init(&chunk, &arena);

  ASSERT(chunk.code != NULL);
  ASSERT(chunk.constants != NULL);
  ASSERT(chunk.lines != NULL);
  ASSERT_U32_EQ(chunk.code_count, 0);
  ASSERT_U32_EQ(chunk.const_count, 0);
  ASSERT_U32_EQ(chunk.code_cap, BYTECODE_INIT_CODE_CAP);
  ASSERT_U32_EQ(chunk.const_cap, BYTECODE_INIT_CONST_CAP);
  ASSERT(chunk.arena == &arena);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: chunk_write appends bytes and tracks line numbers */
static int test_chunk_write(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write(&chunk, OP_NIL, 1);
  chunk_write(&chunk, OP_TRUE, 2);
  chunk_write(&chunk, OP_FALSE, 3);

  ASSERT_U32_EQ(chunk.code_count, 3);
  ASSERT_INT_EQ(chunk.code[0], OP_NIL);
  ASSERT_INT_EQ(chunk.code[1], OP_TRUE);
  ASSERT_INT_EQ(chunk.code[2], OP_FALSE);
  ASSERT_U32_EQ(chunk.lines[0], 1);
  ASSERT_U32_EQ(chunk.lines[1], 2);
  ASSERT_U32_EQ(chunk.lines[2], 3);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: chunk_write_u16 appends 2 bytes big-endian */
static int test_chunk_write_u16(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write_u16(&chunk, 0x1234, 5);

  ASSERT_U32_EQ(chunk.code_count, 2);
  ASSERT_INT_EQ(chunk.code[0], 0x12);
  ASSERT_INT_EQ(chunk.code[1], 0x34);
  ASSERT_U32_EQ(chunk.lines[0], 5);
  ASSERT_U32_EQ(chunk.lines[1], 5);

  /* Test a second value */
  chunk_write_u16(&chunk, 0x0001, 7);
  ASSERT_U32_EQ(chunk.code_count, 4);
  ASSERT_INT_EQ(chunk.code[2], 0x00);
  ASSERT_INT_EQ(chunk.code[3], 0x01);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: chunk_add_constant stores values and returns indices */
static int test_chunk_add_constant(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t idx0 = chunk_add_constant(&chunk, jacl_i32(42));
  uint16_t idx1 = chunk_add_constant(&chunk, jacl_f32(3.14f));
  uint16_t idx2 = chunk_add_constant(&chunk, JACL_NIL);

  ASSERT_INT_EQ(idx0, 0);
  ASSERT_INT_EQ(idx1, 1);
  ASSERT_INT_EQ(idx2, 2);
  ASSERT_U32_EQ(chunk.const_count, 3);

  ASSERT(jacl_is_i32(chunk.constants[0]));
  ASSERT_INT_EQ(jacl_as_i32(chunk.constants[0]), 42);
  ASSERT(jacl_is_f32(chunk.constants[1]));
  ASSERT(jacl_is_nil(chunk.constants[2]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_CONST instruction format: opcode + uint16_t index */
static int test_op_const_format(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t idx = chunk_add_constant(&chunk, jacl_i32(99));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, idx, 1);

  /* OP_CONST followed by 2-byte big-endian index */
  ASSERT_U32_EQ(chunk.code_count, 3);
  ASSERT_INT_EQ(chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(chunk.code[1], 0x00);
  ASSERT_INT_EQ(chunk.code[2], 0x00);

  /* Verify constant pool */
  ASSERT(jacl_is_i32(chunk.constants[idx]));
  ASSERT_INT_EQ(jacl_as_i32(chunk.constants[idx]), 99);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_DEF_GLOBAL instruction format: opcode + uint16_t name index */
static int test_op_def_global_format(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Store variable name as inline string constant */
  uint16_t name_idx = chunk_add_constant(&chunk, jacl_inline_string("x", 1));
  chunk_write(&chunk, OP_DEF_GLOBAL, 1);
  chunk_write_u16(&chunk, name_idx, 1);

  ASSERT_U32_EQ(chunk.code_count, 3);
  ASSERT_INT_EQ(chunk.code[0], OP_DEF_GLOBAL);
  /* name index is big-endian 0x0000 */
  ASSERT_INT_EQ(chunk.code[1], 0x00);
  ASSERT_INT_EQ(chunk.code[2], 0x00);

  /* Verify the name is in the constant pool */
  JaclVal name_val = chunk.constants[name_idx];
  ASSERT(jacl_is_inline_string(name_val));
  char buf[8];
  jacl_inline_string_get(name_val, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "x");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_GET_GLOBAL instruction format: opcode + uint16_t name index */
static int test_op_get_global_format(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t name_idx = chunk_add_constant(&chunk, jacl_inline_string("foo", 3));
  chunk_write(&chunk, OP_GET_GLOBAL, 2);
  chunk_write_u16(&chunk, name_idx, 2);

  ASSERT_U32_EQ(chunk.code_count, 3);
  ASSERT_INT_EQ(chunk.code[0], OP_GET_GLOBAL);

  JaclVal name_val = chunk.constants[name_idx];
  ASSERT(jacl_is_inline_string(name_val));
  char buf[8];
  jacl_inline_string_get(name_val, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "foo");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: single-byte opcodes have no operands */
static int test_single_byte_opcodes(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Write all single-byte opcodes */
  OpCode single_byte[] = {
    OP_NIL, OP_TRUE, OP_FALSE, OP_POP,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_EQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_PRINT, OP_HALT
  };
  uint32_t count = sizeof(single_byte) / sizeof(single_byte[0]);

  for (uint32_t i = 0; i < count; i++) {
    chunk_write(&chunk, (uint8_t)single_byte[i], i + 1);
  }

  ASSERT_U32_EQ(chunk.code_count, count);
  for (uint32_t i = 0; i < count; i++) {
    ASSERT_INT_EQ(chunk.code[i], (int)single_byte[i]);
    ASSERT_U32_EQ(chunk.lines[i], i + 1);
  }

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: code array grows when capacity exceeded */
static int test_code_growth(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Write more bytes than initial capacity */
  uint32_t n = BYTECODE_INIT_CODE_CAP + 100;
  for (uint32_t i = 0; i < n; i++) {
    chunk_write(&chunk, (uint8_t)(i & 0xFF), i);
  }

  ASSERT_U32_EQ(chunk.code_count, n);
  ASSERT(chunk.code_cap >= n);

  /* Verify data integrity after growth */
  for (uint32_t i = 0; i < n; i++) {
    ASSERT_INT_EQ(chunk.code[i], (int)(i & 0xFF));
    ASSERT_U32_EQ(chunk.lines[i], i);
  }

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: constant pool grows when capacity exceeded */
static int test_constant_growth(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint32_t n = BYTECODE_INIT_CONST_CAP + 20;
  for (uint32_t i = 0; i < n; i++) {
    uint16_t idx = chunk_add_constant(&chunk, jacl_i32((int32_t)i));
    ASSERT_INT_EQ(idx, (int)i);
  }

  ASSERT_U32_EQ(chunk.const_count, n);
  ASSERT(chunk.const_cap >= n);

  /* Verify data integrity after growth */
  for (uint32_t i = 0; i < n; i++) {
    ASSERT(jacl_is_i32(chunk.constants[i]));
    ASSERT_INT_EQ(jacl_as_i32(chunk.constants[i]), (int)i);
  }

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: complete hand-assembled bytecode sequence */
static int test_complete_bytecode_sequence(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Assemble: [+ 1 2] which is OP_CONST(1), OP_CONST(2), OP_ADD, OP_HALT */
  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(2));

  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_ADD, 1);
  chunk_write(&chunk, OP_HALT, 1);

  /* Verify bytecode: CONST 00 00 CONST 00 01 ADD HALT */
  ASSERT_U32_EQ(chunk.code_count, 8);
  ASSERT_INT_EQ(chunk.code[0], OP_CONST);
  ASSERT_INT_EQ(chunk.code[1], 0x00);
  ASSERT_INT_EQ(chunk.code[2], 0x00);
  ASSERT_INT_EQ(chunk.code[3], OP_CONST);
  ASSERT_INT_EQ(chunk.code[4], 0x00);
  ASSERT_INT_EQ(chunk.code[5], 0x01);
  ASSERT_INT_EQ(chunk.code[6], OP_ADD);
  ASSERT_INT_EQ(chunk.code[7], OP_HALT);

  /* Verify constants */
  ASSERT_U32_EQ(chunk.const_count, 2);
  ASSERT_INT_EQ(jacl_as_i32(chunk.constants[c1]), 1);
  ASSERT_INT_EQ(jacl_as_i32(chunk.constants[c2]), 2);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: inline string constant in pool */
static int test_inline_string_constant(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t idx = chunk_add_constant(&chunk, jacl_inline_string("hello", 5));

  JaclVal val = chunk.constants[idx];
  ASSERT(jacl_is_inline_string(val));
  ASSERT_SIZE_EQ(jacl_inline_string_len(val), 5);
  char buf[8];
  jacl_inline_string_get(val, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "hello");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-001: Bytecode chunk and opcode definitions */
    { "opcode_enum",               test_opcode_enum },
    { "chunk_init",                test_chunk_init },
    { "chunk_write",               test_chunk_write },
    { "chunk_write_u16",           test_chunk_write_u16 },
    { "chunk_add_constant",        test_chunk_add_constant },
    { "op_const_format",           test_op_const_format },
    { "op_def_global_format",      test_op_def_global_format },
    { "op_get_global_format",      test_op_get_global_format },
    { "single_byte_opcodes",       test_single_byte_opcodes },
    { "code_growth",               test_code_growth },
    { "constant_growth",           test_constant_growth },
    { "complete_bytecode_sequence",test_complete_bytecode_sequence },
    { "inline_string_constant",    test_inline_string_constant },
  };

  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0;
  int failed = 0;

  for (int i = 0; i < total; i++) {
    printf("  %-35s ", tests[i].name);
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n  %d/%d passed\n", passed, total);
  return failed > 0 ? 1 : 0;
}
