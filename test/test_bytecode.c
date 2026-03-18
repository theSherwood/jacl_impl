#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-001: Bytecode chunk and opcode definitions ===== */

/* Test: OpCode enum defines all M3+M4 opcodes */
static int test_opcode_enum(void) {
  ASSERT_INT_EQ(OP_CONST,          0);
  ASSERT_INT_EQ(OP_NIL,            1);
  ASSERT_INT_EQ(OP_TRUE,           2);
  ASSERT_INT_EQ(OP_FALSE,          3);
  ASSERT_INT_EQ(OP_POP,            4);
  ASSERT_INT_EQ(OP_ADD,            5);
  ASSERT_INT_EQ(OP_SUB,            6);
  ASSERT_INT_EQ(OP_MUL,            7);
  ASSERT_INT_EQ(OP_DIV,            8);
  ASSERT_INT_EQ(OP_MOD,            9);
  ASSERT_INT_EQ(OP_NEG,           10);
  ASSERT_INT_EQ(OP_EQ,            11);
  ASSERT_INT_EQ(OP_LT,            12);
  ASSERT_INT_EQ(OP_GT,            13);
  ASSERT_INT_EQ(OP_LE,            14);
  ASSERT_INT_EQ(OP_GE,            15);
  ASSERT_INT_EQ(OP_PRINT,         16);
  ASSERT_INT_EQ(OP_DEF_GLOBAL,    17);
  ASSERT_INT_EQ(OP_GET_GLOBAL,    18);
  /* M4 opcodes */
  ASSERT_INT_EQ(OP_GET_LOCAL,      19);
  ASSERT_INT_EQ(OP_SET_LOCAL,      20);
  ASSERT_INT_EQ(OP_GET_UPVALUE,    21);
  ASSERT_INT_EQ(OP_JUMP,           22);
  ASSERT_INT_EQ(OP_JUMP_IF_FALSE,  23);
  ASSERT_INT_EQ(OP_LOOP,           24);
  ASSERT_INT_EQ(OP_CALL,           25);
  ASSERT_INT_EQ(OP_TAIL_CALL,      26);
  ASSERT_INT_EQ(OP_RETURN,         27);
  ASSERT_INT_EQ(OP_CLOSURE,        28);
  ASSERT_INT_EQ(OP_POP_N,          29);
  /* M5 opcodes */
  ASSERT_INT_EQ(OP_CONCAT,         30);
  ASSERT_INT_EQ(OP_STR_LEN,        31);
  ASSERT_INT_EQ(OP_STR_BYTE_LEN,   32);
  ASSERT_INT_EQ(OP_STR_INDEX,      33);
  ASSERT_INT_EQ(OP_STR_SLICE,      34);
  ASSERT_INT_EQ(OP_TO_STRING,      35);
  /* M8 opcodes */
  ASSERT_INT_EQ(OP_VEC,            36);
  ASSERT_INT_EQ(OP_VEC_GET,        37);
  ASSERT_INT_EQ(OP_VEC_LEN,        38);
  ASSERT_INT_EQ(OP_VEC_PUSH,       39);
  ASSERT_INT_EQ(OP_VEC_SET,        40);
  ASSERT_INT_EQ(OP_VEC_CONCAT,     41);
  ASSERT_INT_EQ(OP_VEC_SLICE,      42);
  /* M8 map opcodes */
  ASSERT_INT_EQ(OP_MAP,            43);
  ASSERT_INT_EQ(OP_MAP_GET,        44);
  ASSERT_INT_EQ(OP_MAP_HAS,        45);
  ASSERT_INT_EQ(OP_MAP_LEN,        46);
  ASSERT_INT_EQ(OP_MAP_SET,        47);
  ASSERT_INT_EQ(OP_MAP_REMOVE,     48);
  ASSERT_INT_EQ(OP_MAP_KEYS,       49);
  ASSERT_INT_EQ(OP_MAP_VALS,       50);
  ASSERT_INT_EQ(OP_EACH,           51);
  ASSERT_INT_EQ(OP_TRANSFORM,      52);
  ASSERT_INT_EQ(OP_FILTER,         53);
  /* M9 error handling opcodes */
  ASSERT_INT_EQ(OP_ERROR,          54);
  ASSERT_INT_EQ(OP_IS_ERROR,       55);
  ASSERT_INT_EQ(OP_ERROR_VAL,      56);
  ASSERT_INT_EQ(OP_CHECK_ERROR,    57);
  ASSERT_INT_EQ(OP_JUMP_IF_ERROR,  58);
  ASSERT_INT_EQ(OP_STACK_TRACE,    59);
  /* M10 mutable state opcodes */
  ASSERT_INT_EQ(OP_MAKE_CELL,       60);
  ASSERT_INT_EQ(OP_GET_CELL_LOCAL,   61);
  ASSERT_INT_EQ(OP_SET_CELL_LOCAL,   62);
  ASSERT_INT_EQ(OP_GET_CELL_UPVALUE, 63);
  ASSERT_INT_EQ(OP_SET_CELL_UPVALUE, 64);
  ASSERT_INT_EQ(OP_SET_GLOBAL,      65);
  ASSERT_INT_EQ(OP_BOX,             66);
  ASSERT_INT_EQ(OP_ATOM,            67);
  ASSERT_INT_EQ(OP_DEREF,           68);
  ASSERT_INT_EQ(OP_RESET,           69);
  ASSERT_INT_EQ(OP_SWAP,            70);
  ASSERT_INT_EQ(OP_IS_BOX,          71);
  ASSERT_INT_EQ(OP_IS_ATOM,         72);
  ASSERT_INT_EQ(OP_IS_FUTURE,       73);
  /* M13 CPS and concurrency opcodes */
  ASSERT_INT_EQ(OP_AWAIT,           74);
  ASSERT_INT_EQ(OP_SPAWN,           75);
  ASSERT_INT_EQ(OP_RESOLVE_FUTURE,  76);
  ASSERT_INT_EQ(OP_PARALLEL,        77);
  ASSERT_INT_EQ(OP_RACE,            78);
  ASSERT_INT_EQ(OP_COMPLETE_PARALLEL, 79);
  ASSERT_INT_EQ(OP_COMPLETE_RACE,    80);
  /* M11 typed opcodes */
  ASSERT_INT_EQ(OP_ADD_I64,         81);
  ASSERT_INT_EQ(OP_SUB_I64,         82);
  ASSERT_INT_EQ(OP_MUL_I64,         83);
  ASSERT_INT_EQ(OP_DIV_I64,         84);
  ASSERT_INT_EQ(OP_MOD_I64,         85);
  ASSERT_INT_EQ(OP_NEG_I64,         86);
  ASSERT_INT_EQ(OP_LT_I64,          87);
  ASSERT_INT_EQ(OP_GT_I64,          88);
  ASSERT_INT_EQ(OP_LE_I64,          89);
  ASSERT_INT_EQ(OP_GE_I64,          90);
  ASSERT_INT_EQ(OP_EQ_I64,          91);
  ASSERT_INT_EQ(OP_DIV_U64,         92);
  ASSERT_INT_EQ(OP_MOD_U64,         93);
  ASSERT_INT_EQ(OP_LT_U64,          94);
  ASSERT_INT_EQ(OP_GT_U64,          95);
  ASSERT_INT_EQ(OP_LE_U64,          96);
  ASSERT_INT_EQ(OP_GE_U64,          97);
  ASSERT_INT_EQ(OP_ADD_F64,         98);
  ASSERT_INT_EQ(OP_SUB_F64,         99);
  ASSERT_INT_EQ(OP_MUL_F64,        100);
  ASSERT_INT_EQ(OP_DIV_F64,        101);
  ASSERT_INT_EQ(OP_MOD_F64,        102);
  ASSERT_INT_EQ(OP_NEG_F64,        103);
  ASSERT_INT_EQ(OP_LT_F64,         104);
  ASSERT_INT_EQ(OP_GT_F64,         105);
  ASSERT_INT_EQ(OP_LE_F64,         106);
  ASSERT_INT_EQ(OP_GE_F64,         107);
  ASSERT_INT_EQ(OP_EQ_F64,         108);
  ASSERT_INT_EQ(OP_TO_I32,         109);
  ASSERT_INT_EQ(OP_TO_I64,         110);
  ASSERT_INT_EQ(OP_TO_U32,         111);
  ASSERT_INT_EQ(OP_TO_U64,         112);
  ASSERT_INT_EQ(OP_TO_F32,         113);
  ASSERT_INT_EQ(OP_TO_F64,         114);
  ASSERT_INT_EQ(OP_TO_DYN,         115);
  ASSERT_INT_EQ(OP_CONST_I64,      116);
  ASSERT_INT_EQ(OP_CONST_U64,      117);
  ASSERT_INT_EQ(OP_CONST_F64,      118);
  ASSERT_INT_EQ(OP_STRUCT_NEW,     119);
  ASSERT_INT_EQ(OP_STRUCT_GET,     120);
  ASSERT_INT_EQ(OP_STRUCT_SET,     121);
  ASSERT_INT_EQ(OP_STRUCT_GET_DYN, 122);
  ASSERT_INT_EQ(OP_STRUCT_SET_DYN, 123);
  ASSERT_INT_EQ(OP_CLOSE_LOOP,        124);
  ASSERT_INT_EQ(OP_DESTRUCTURE_VEC,  125);
  ASSERT_INT_EQ(OP_DESTRUCTURE_NAMED, 126);
  ASSERT_INT_EQ(OP_HALT,             127);
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
    OP_PRINT, OP_RETURN, OP_HALT
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

/* ===== US-002: JaclClosure struct and closure value helpers ===== */

/* Test: JaclClosure struct fields and arena allocation */
static int test_closure_struct(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclClosure* cl = (JaclClosure*)arena_alloc(&arena, sizeof(JaclClosure));
  chunk_init(&cl->chunk, &arena);
  cl->param_count = 2;
  cl->param_names = (JaclVal*)arena_alloc(&arena, 2 * sizeof(JaclVal));
  cl->param_names[0] = jacl_inline_string("a", 1);
  cl->param_names[1] = jacl_inline_string("b", 1);
  cl->upvalue_count = 1;
  cl->upvalues = (JaclVal*)arena_alloc(&arena, 1 * sizeof(JaclVal));
  cl->upvalues[0] = jacl_i32(42);
  cl->name = "add";

  ASSERT_INT_EQ(cl->param_count, 2);
  ASSERT_INT_EQ(cl->upvalue_count, 1);
  ASSERT_STR_EQ(cl->name, "add");
  ASSERT(cl->chunk.code != NULL);
  ASSERT(jacl_is_inline_string(cl->param_names[0]));
  ASSERT(jacl_is_inline_string(cl->param_names[1]));
  ASSERT_INT_EQ(jacl_as_i32(cl->upvalues[0]), 42);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_closure() constructor creates closure-tagged value */
static int test_jacl_closure_constructor(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclClosure* cl = (JaclClosure*)arena_alloc(&arena, sizeof(JaclClosure));
  chunk_init(&cl->chunk, &arena);
  cl->param_count = 0;
  cl->param_names = NULL;
  cl->upvalue_count = 0;
  cl->upvalues = NULL;
  cl->name = NULL;

  JaclVal val = jacl_closure(cl);

  ASSERT(jacl_is_closure(val));
  ASSERT(!jacl_is_nil(val));
  ASSERT(!jacl_is_i32(val));
  ASSERT(!jacl_is_bool(val));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: jacl_as_closure() extracts pointer back from tagged value */
static int test_jacl_as_closure_extractor(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclClosure* cl = (JaclClosure*)arena_alloc(&arena, sizeof(JaclClosure));
  chunk_init(&cl->chunk, &arena);
  cl->param_count = 3;
  cl->param_names = NULL;
  cl->upvalue_count = 0;
  cl->upvalues = NULL;
  cl->name = "test";

  JaclVal val = jacl_closure(cl);
  JaclClosure* extracted = jacl_as_closure(val);

  ASSERT_PTR_EQ(extracted, cl);
  ASSERT_INT_EQ(extracted->param_count, 3);
  ASSERT_STR_EQ(extracted->name, "test");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: closure with populated param_names and upvalues arrays */
static int test_closure_full_roundtrip(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclClosure* cl = (JaclClosure*)arena_alloc(&arena, sizeof(JaclClosure));
  chunk_init(&cl->chunk, &arena);
  cl->param_count = 2;
  cl->param_names = (JaclVal*)arena_alloc(&arena, 2 * sizeof(JaclVal));
  cl->param_names[0] = jacl_inline_string("x", 1);
  cl->param_names[1] = jacl_inline_string("y", 1);
  cl->upvalue_count = 2;
  cl->upvalues = (JaclVal*)arena_alloc(&arena, 2 * sizeof(JaclVal));
  cl->upvalues[0] = jacl_i32(10);
  cl->upvalues[1] = jacl_i32(20);
  cl->name = "adder";

  /* Write some bytecode into the closure's chunk */
  chunk_write(&cl->chunk, OP_GET_LOCAL, 1);
  chunk_write(&cl->chunk, 0, 1);
  chunk_write(&cl->chunk, OP_GET_LOCAL, 1);
  chunk_write(&cl->chunk, 1, 1);
  chunk_write(&cl->chunk, OP_ADD, 1);
  chunk_write(&cl->chunk, OP_RETURN, 1);

  /* Roundtrip through tagged value */
  JaclVal val = jacl_closure(cl);
  ASSERT(jacl_is_closure(val));

  JaclClosure* out = jacl_as_closure(val);
  ASSERT_PTR_EQ(out, cl);
  ASSERT_INT_EQ(out->param_count, 2);
  ASSERT_INT_EQ(out->upvalue_count, 2);
  ASSERT_STR_EQ(out->name, "adder");
  ASSERT_U32_EQ(out->chunk.code_count, 6);
  ASSERT_INT_EQ(out->chunk.code[0], OP_GET_LOCAL);
  ASSERT_INT_EQ(out->chunk.code[4], OP_ADD);
  ASSERT_INT_EQ(out->chunk.code[5], OP_RETURN);

  char buf[8];
  jacl_inline_string_get(out->param_names[0], buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "x");
  jacl_inline_string_get(out->param_names[1], buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "y");
  ASSERT_INT_EQ(jacl_as_i32(out->upvalues[0]), 10);
  ASSERT_INT_EQ(jacl_as_i32(out->upvalues[1]), 20);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: closure with NULL name (anonymous) */
static int test_closure_null_name(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  JaclClosure* cl = (JaclClosure*)arena_alloc(&arena, sizeof(JaclClosure));
  chunk_init(&cl->chunk, &arena);
  cl->param_count = 0;
  cl->param_names = NULL;
  cl->upvalue_count = 0;
  cl->upvalues = NULL;
  cl->name = NULL;

  JaclVal val = jacl_closure(cl);
  JaclClosure* out = jacl_as_closure(val);
  ASSERT_PTR_EQ(out->name, NULL);

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
    /* US-002: JaclClosure struct and closure value helpers */
    { "closure_struct",             test_closure_struct },
    { "jacl_closure_constructor",   test_jacl_closure_constructor },
    { "jacl_as_closure_extractor",  test_jacl_as_closure_extractor },
    { "closure_full_roundtrip",     test_closure_full_roundtrip },
    { "closure_null_name",          test_closure_null_name },
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
