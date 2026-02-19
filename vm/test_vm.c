#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../test/test_helpers.h"

#define ARENA_IMPLEMENTATION
#include "../arena/arena.h"

#define VALUE_IMPLEMENTATION
#include "../value/value.h"

#define BYTECODE_IMPLEMENTATION
#include "../compiler/bytecode.h"

#define VM_IMPLEMENTATION
#include "./vm.h"

/* ===== US-002: VM state and execution loop ===== */

/* Test: vm_init zeroes state and sets defaults */
static int test_vm_init(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  ASSERT_U32_EQ(vm.stack_top, 0);
  ASSERT(vm.ip == NULL);
  ASSERT(vm.chunk == NULL);
  ASSERT(vm.print_fn != NULL);  /* default print function set */
  ASSERT(vm.print_ctx == NULL);
  ASSERT(vm.arena == &arena);
  /* env is pre-populated with 3 entries: true, false, nil */
  ASSERT_U32_EQ(vm.env.count, 3);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: VMResult enum values */
static int test_vmresult_enum(void) {
  ASSERT_INT_EQ(VM_OK, 0);
  ASSERT_INT_EQ(VM_RUNTIME_ERROR, 1);
  ASSERT_INT_EQ(VM_STACK_OVERFLOW, 2);
  TEST_PASS();
}

/* Test: OP_HALT stops execution and returns VM_OK */
static int test_op_halt(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 0);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_CONST pushes constant onto stack */
static int test_op_const(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t idx = chunk_add_constant(&chunk, jacl_i32(42));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, idx, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 42);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_CONST with multiple constants */
static int test_op_const_multiple(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(10));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(3.14f));
  uint16_t c3 = chunk_add_constant(&chunk, jacl_inline_string("hi", 2));

  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 2);
  chunk_write_u16(&chunk, c2, 2);
  chunk_write(&chunk, OP_CONST, 3);
  chunk_write_u16(&chunk, c3, 3);
  chunk_write(&chunk, OP_HALT, 3);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 3);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 10);
  ASSERT(jacl_is_f32(vm.stack[1]));
  ASSERT(jacl_is_inline_string(vm.stack[2]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_NIL pushes nil */
static int test_op_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write(&chunk, OP_NIL, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_TRUE pushes true */
static int test_op_true(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write(&chunk, OP_TRUE, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_bool(vm.stack[0]));
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_FALSE pushes false */
static int test_op_false(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write(&chunk, OP_FALSE, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_bool(vm.stack[0]));
  ASSERT_U64_EQ(vm.stack[0], JACL_FALSE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_POP discards top of stack */
static int test_op_pop(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(100));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(200));

  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_POP, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 100);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: stack overflow returns VM_STACK_OVERFLOW */
static int test_stack_overflow(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Push VM_STACK_MAX + 1 values to trigger overflow */
  for (uint32_t i = 0; i <= VM_STACK_MAX; i++) {
    chunk_write(&chunk, OP_NIL, 1);
  }
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_STACK_OVERFLOW);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: stack underflow returns VM_RUNTIME_ERROR */
static int test_stack_underflow(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Pop from empty stack */
  chunk_write(&chunk, OP_POP, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: configurable print function */
static int test_custom_print_fn(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };

  VM vm;
  vm_init(&vm, &arena);

  /* Verify default is set */
  ASSERT(vm.print_fn != NULL);

  /* Set custom print function */
  int ctx_data = 42;
  vm.print_fn  = NULL;  /* clear to verify we can set it */
  vm.print_ctx = &ctx_data;

  ASSERT(vm.print_fn == NULL);
  ASSERT(vm.print_ctx == &ctx_data);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: combined literal push sequence */
static int test_literal_push_sequence(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* Push nil, true, false, constant - verify stack order */
  uint16_t c = chunk_add_constant(&chunk, jacl_i32(99));

  chunk_write(&chunk, OP_NIL, 1);
  chunk_write(&chunk, OP_TRUE, 2);
  chunk_write(&chunk, OP_FALSE, 3);
  chunk_write(&chunk, OP_CONST, 4);
  chunk_write_u16(&chunk, c, 4);
  chunk_write(&chunk, OP_HALT, 4);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 4);
  ASSERT(jacl_is_nil(vm.stack[0]));
  ASSERT_U64_EQ(vm.stack[1], JACL_TRUE);
  ASSERT_U64_EQ(vm.stack[2], JACL_FALSE);
  ASSERT(jacl_is_i32(vm.stack[3]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[3]), 99);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: push and pop interleaved */
static int test_push_pop_interleaved(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(2));
  uint16_t c3 = chunk_add_constant(&chunk, jacl_i32(3));

  /* Push 1, push 2, pop, push 3 -> stack should be [1, 3] */
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_POP, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c3, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 2);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 1);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[1]), 3);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-004: Arithmetic builtins (VM) ===== */

/* Test: OP_ADD with i32 values */
static int test_op_add_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(10));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(32));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_ADD, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 42);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_ADD with f32 values */
static int test_op_add_f32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_f32(1.5f));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(2.5f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_ADD, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_f32(vm.stack[0]));
  float f = jacl_as_f32(vm.stack[0]);
  ASSERT(f > 3.9f && f < 4.1f);  /* 1.5 + 2.5 = 4.0 */

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_SUB with i32 */
static int test_op_sub_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(10));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(3));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_SUB, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 7);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_MUL with i32 */
static int test_op_mul_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(4));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(5));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_MUL, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 20);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_DIV with i32 */
static int test_op_div_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(10));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(2));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_DIV, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 5);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_MOD with i32 */
static int test_op_mod_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(7));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(3));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_MOD, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 1);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_NEG with i32 */
static int test_op_neg_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c = chunk_add_constant(&chunk, jacl_i32(42));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c, 1);
  chunk_write(&chunk, OP_NEG, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), -42);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_NEG with f32 */
static int test_op_neg_f32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c = chunk_add_constant(&chunk, jacl_f32(3.14f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c, 1);
  chunk_write(&chunk, OP_NEG, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT(jacl_is_f32(vm.stack[0]));
  float f = jacl_as_f32(vm.stack[0]);
  ASSERT(f < -3.13f && f > -3.15f);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: division by zero produces error-flagged value */
static int test_op_div_by_zero(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(10));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(0));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_DIV, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_error(vm.stack[0]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_MOD with f32 produces runtime error */
static int test_op_mod_f32_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_f32(7.0f));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(3.0f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_MOD, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: mixed types (i32 + f32) produces runtime error */
static int test_op_add_mixed_type_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(2.0f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_ADD, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: nested arithmetic 1 + (2 * 3) = 7 via hand-assembled bytecode */
static int test_op_nested_arithmetic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(2));
  uint16_t c3 = chunk_add_constant(&chunk, jacl_i32(3));

  /* Push 1, push 2, push 3, mul, add -> 1 + (2*3) = 7 */
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c3, 1);
  chunk_write(&chunk, OP_MUL, 1);
  chunk_write(&chunk, OP_ADD, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 7);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: f32 arithmetic operations (sub, mul, div) */
static int test_op_f32_arithmetic(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  /* 10.0 - 3.0 = 7.0 */
  uint16_t c1 = chunk_add_constant(&chunk, jacl_f32(10.0f));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(3.0f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_SUB, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT(jacl_is_f32(vm.stack[0]));
  float f = jacl_as_f32(vm.stack[0]);
  ASSERT(f > 6.9f && f < 7.1f);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-005: Comparison builtins (VM) ===== */

/* Test: OP_EQ with equal i32 values -> JACL_TRUE */
static int test_op_eq_i32_true(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(1));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_EQ, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_EQ with unequal i32 values -> JACL_FALSE */
static int test_op_eq_i32_false(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(2));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_EQ, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_U64_EQ(vm.stack[0], JACL_FALSE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_EQ with different types -> JACL_FALSE (not error) */
static int test_op_eq_different_types(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(1.0f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_EQ, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT_U64_EQ(vm.stack[0], JACL_FALSE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_LT with i32 values */
static int test_op_lt_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(2));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_LT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  /* Reverse: 2 < 1 -> false */
  chunk_init(&chunk, &arena);
  c1 = chunk_add_constant(&chunk, jacl_i32(2));
  c2 = chunk_add_constant(&chunk, jacl_i32(1));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_LT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  vm_init(&vm, &arena);
  result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_FALSE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_GT with i32 values */
static int test_op_gt_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(5));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(3));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_GT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_LE with i32 values (equal case) */
static int test_op_le_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(3));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(3));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_LE, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_GE with i32 values (false case) */
static int test_op_ge_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(2));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(5));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_GE, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_FALSE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_LT with f32 values */
static int test_op_lt_f32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_f32(1.5f));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(2.5f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_LT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_GT with f32 values */
static int test_op_gt_f32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_f32(5.0f));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(3.0f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_GT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: comparison with mismatched numeric types produces runtime error */
static int test_op_lt_mixed_type_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(2.0f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_LT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_EQ with f32 equal values */
static int test_op_eq_f32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_f32(3.14f));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_f32(3.14f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_EQ, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-006: Print builtin (VM) ===== */

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

/* Test: OP_PRINT with i32 value */
static int test_op_print_i32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c = chunk_add_constant(&chunk, jacl_i32(42));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "42\n");
  /* print pushes nil as return value */
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_PRINT with f32 value */
static int test_op_print_f32(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c = chunk_add_constant(&chunk, jacl_f32(3.14f));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3.14\n");
  ASSERT(jacl_is_nil(vm.stack[0]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_PRINT with nil value */
static int test_op_print_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write(&chunk, OP_NIL, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "nil\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_PRINT with true and false */
static int test_op_print_bool(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  chunk_write(&chunk, OP_TRUE, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_POP, 1);  /* discard nil return */
  chunk_write(&chunk, OP_FALSE, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "true\nfalse\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_PRINT with inline string */
static int test_op_print_string(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c = chunk_add_constant(&chunk, jacl_inline_string("hello", 5));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "hello\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_PRINT with error-flagged value */
static int test_op_print_error(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c = chunk_add_constant(&chunk, jacl_set_error(jacl_i32(0)));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "<error>\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: print [+ 1 2] outputs 3\n via hand-assembled bytecode */
static int test_op_print_add_result(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c1 = chunk_add_constant(&chunk, jacl_i32(1));
  uint16_t c2 = chunk_add_constant(&chunk, jacl_i32(2));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c1, 1);
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c2, 1);
  chunk_write(&chunk, OP_ADD, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");
  ASSERT(jacl_is_nil(vm.stack[0]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_PRINT with negative i32 */
static int test_op_print_negative(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t c = chunk_add_constant(&chunk, jacl_i32(-7));
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, c, 1);
  chunk_write(&chunk, OP_PRINT, 1);
  chunk_write(&chunk, OP_HALT, 1);

  PrintCapture cap = { .len = 0 };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = &cap;
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_STR_EQ(cap.buf, "-7\n");

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* ===== US-007: Global environment, def, and variable references (VM) ===== */

/* Test: OP_DEF_GLOBAL + OP_GET_GLOBAL: define and retrieve a value */
static int test_op_def_get_global(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t val_idx  = chunk_add_constant(&chunk, jacl_i32(42));
  uint16_t name_idx = chunk_add_constant(&chunk, jacl_inline_string("x", 1));

  /* def x = 42 */
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, val_idx, 1);
  chunk_write(&chunk, OP_DEF_GLOBAL, 1);
  chunk_write_u16(&chunk, name_idx, 1);
  /* pop the nil return from def */
  chunk_write(&chunk, OP_POP, 1);
  /* get x */
  chunk_write(&chunk, OP_GET_GLOBAL, 2);
  chunk_write_u16(&chunk, name_idx, 2);
  chunk_write(&chunk, OP_HALT, 2);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 42);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_DEF_GLOBAL pushes JACL_NIL as return value */
static int test_op_def_global_returns_nil(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t val_idx  = chunk_add_constant(&chunk, jacl_i32(99));
  uint16_t name_idx = chunk_add_constant(&chunk, jacl_inline_string("y", 1));

  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, val_idx, 1);
  chunk_write(&chunk, OP_DEF_GLOBAL, 1);
  chunk_write_u16(&chunk, name_idx, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_nil(vm.stack[0]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_DEF_GLOBAL redefining an existing name overwrites silently */
static int test_op_def_global_redefine(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t v1_idx   = chunk_add_constant(&chunk, jacl_i32(10));
  uint16_t v2_idx   = chunk_add_constant(&chunk, jacl_i32(20));
  uint16_t name_idx = chunk_add_constant(&chunk, jacl_inline_string("z", 1));

  /* def z = 10 */
  chunk_write(&chunk, OP_CONST, 1);
  chunk_write_u16(&chunk, v1_idx, 1);
  chunk_write(&chunk, OP_DEF_GLOBAL, 1);
  chunk_write_u16(&chunk, name_idx, 1);
  chunk_write(&chunk, OP_POP, 1);
  /* def z = 20 (redefine) */
  chunk_write(&chunk, OP_CONST, 2);
  chunk_write_u16(&chunk, v2_idx, 2);
  chunk_write(&chunk, OP_DEF_GLOBAL, 2);
  chunk_write_u16(&chunk, name_idx, 2);
  chunk_write(&chunk, OP_POP, 2);
  /* get z -> should be 20 */
  chunk_write(&chunk, OP_GET_GLOBAL, 3);
  chunk_write_u16(&chunk, name_idx, 3);
  chunk_write(&chunk, OP_HALT, 3);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_i32(vm.stack[0]));
  ASSERT_INT_EQ(jacl_as_i32(vm.stack[0]), 20);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: OP_GET_GLOBAL for undefined variable pushes error-flagged value */
static int test_op_get_global_undefined(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t name_idx = chunk_add_constant(&chunk, jacl_inline_string("nope", 4));

  chunk_write(&chunk, OP_GET_GLOBAL, 1);
  chunk_write_u16(&chunk, name_idx, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 1);
  ASSERT(jacl_is_error(vm.stack[0]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: pre-populated env: $true, $false, $nil resolve correctly */
static int test_env_prepopulated(void) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  BytecodeChunk chunk;
  chunk_init(&chunk, &arena);

  uint16_t t_idx = chunk_add_constant(&chunk, jacl_inline_string("true", 4));
  uint16_t f_idx = chunk_add_constant(&chunk, jacl_inline_string("false", 5));
  uint16_t n_idx = chunk_add_constant(&chunk, jacl_inline_string("nil", 3));

  chunk_write(&chunk, OP_GET_GLOBAL, 1);
  chunk_write_u16(&chunk, t_idx, 1);
  chunk_write(&chunk, OP_GET_GLOBAL, 1);
  chunk_write_u16(&chunk, f_idx, 1);
  chunk_write(&chunk, OP_GET_GLOBAL, 1);
  chunk_write_u16(&chunk, n_idx, 1);
  chunk_write(&chunk, OP_HALT, 1);

  VM vm;
  vm_init(&vm, &arena);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT_U32_EQ(vm.stack_top, 3);
  ASSERT_U64_EQ(vm.stack[0], JACL_TRUE);
  ASSERT_U64_EQ(vm.stack[1], JACL_FALSE);
  ASSERT(jacl_is_nil(vm.stack[2]));

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* US-002: VM state and execution loop */
    { "vm_init",                  test_vm_init },
    { "vmresult_enum",            test_vmresult_enum },
    { "op_halt",                  test_op_halt },
    { "op_const",                 test_op_const },
    { "op_const_multiple",        test_op_const_multiple },
    { "op_nil",                   test_op_nil },
    { "op_true",                  test_op_true },
    { "op_false",                 test_op_false },
    { "op_pop",                   test_op_pop },
    { "stack_overflow",           test_stack_overflow },
    { "stack_underflow",          test_stack_underflow },
    { "custom_print_fn",          test_custom_print_fn },
    { "literal_push_sequence",    test_literal_push_sequence },
    { "push_pop_interleaved",     test_push_pop_interleaved },
    /* US-004: Arithmetic builtins (VM) */
    { "op_add_i32",               test_op_add_i32 },
    { "op_add_f32",               test_op_add_f32 },
    { "op_sub_i32",               test_op_sub_i32 },
    { "op_mul_i32",               test_op_mul_i32 },
    { "op_div_i32",               test_op_div_i32 },
    { "op_mod_i32",               test_op_mod_i32 },
    { "op_neg_i32",               test_op_neg_i32 },
    { "op_neg_f32",               test_op_neg_f32 },
    { "op_div_by_zero",           test_op_div_by_zero },
    { "op_mod_f32_error",         test_op_mod_f32_error },
    { "op_add_mixed_type_error",  test_op_add_mixed_type_error },
    { "op_nested_arithmetic",     test_op_nested_arithmetic },
    { "op_f32_arithmetic",        test_op_f32_arithmetic },
    /* US-005: Comparison builtins (VM) */
    { "op_eq_i32_true",           test_op_eq_i32_true },
    { "op_eq_i32_false",          test_op_eq_i32_false },
    { "op_eq_different_types",    test_op_eq_different_types },
    { "op_lt_i32",                test_op_lt_i32 },
    { "op_gt_i32",                test_op_gt_i32 },
    { "op_le_i32",                test_op_le_i32 },
    { "op_ge_i32",                test_op_ge_i32 },
    { "op_lt_f32",                test_op_lt_f32 },
    { "op_gt_f32",                test_op_gt_f32 },
    { "op_lt_mixed_type_error",   test_op_lt_mixed_type_error },
    { "op_eq_f32",                test_op_eq_f32 },
    /* US-006: Print builtin (VM) */
    { "op_print_i32",             test_op_print_i32 },
    { "op_print_f32",             test_op_print_f32 },
    { "op_print_nil",             test_op_print_nil },
    { "op_print_bool",            test_op_print_bool },
    { "op_print_string",          test_op_print_string },
    { "op_print_error",           test_op_print_error },
    { "op_print_add_result",      test_op_print_add_result },
    { "op_print_negative",        test_op_print_negative },
    /* US-007: Global environment, def, and variable references (VM) */
    { "op_def_get_global",        test_op_def_get_global },
    { "op_def_global_returns_nil", test_op_def_global_returns_nil },
    { "op_def_global_redefine",   test_op_def_global_redefine },
    { "op_get_global_undefined",  test_op_get_global_undefined },
    { "env_prepopulated",         test_env_prepopulated },
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
