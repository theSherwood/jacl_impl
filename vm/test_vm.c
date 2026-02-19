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
  VM vm;
  vm_init(&vm);

  ASSERT_U32_EQ(vm.stack_top, 0);
  ASSERT(vm.ip == NULL);
  ASSERT(vm.chunk == NULL);
  ASSERT(vm.print_fn != NULL);  /* default print function set */
  ASSERT(vm.print_ctx == NULL);

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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_RUNTIME_ERROR);

  arena_destroy(&arena);
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* Test: configurable print function */
static int test_custom_print_fn(void) {
  VM vm;
  vm_init(&vm);

  /* Verify default is set */
  ASSERT(vm.print_fn != NULL);

  /* Set custom print function */
  static int called = 0;
  called = 0;
  /* We can't easily test print_fn without OP_PRINT (future story),
     but we can verify the fields are settable */
  int ctx_data = 42;
  vm.print_fn  = NULL;  /* clear to verify we can set it */
  vm.print_ctx = &ctx_data;

  ASSERT(vm.print_fn == NULL);
  ASSERT(vm.print_ctx == &ctx_data);

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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
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
  vm_init(&vm);
  VMResult result = vm_exec(&vm, &chunk);

  ASSERT_INT_EQ(result, VM_OK);
  ASSERT(jacl_is_f32(vm.stack[0]));
  float f = jacl_as_f32(vm.stack[0]);
  ASSERT(f > 6.9f && f < 7.1f);

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
