/*
 * ---------------------------------------------------------------------------
 * JACL Virtual Machine
 * ---------------------------------------------------------------------------
 * Stack-based bytecode interpreter. Executes BytecodeChunk instructions
 * using a fixed-size operand stack.
 *
 * Single-header convention:
 *   #ifndef VM_H / #define VM_H for declarations
 *   #ifdef VM_IMPLEMENTATION for implementation
 */

#ifndef VM_H
#define VM_H

#include <stdint.h>

#include "../compiler/bytecode.h"

/* --- Stack size --- */

#define VM_STACK_MAX 256

/* --- Result codes --- */

typedef enum {
  VM_OK,
  VM_RUNTIME_ERROR,
  VM_STACK_OVERFLOW
} VMResult;

/* --- Print callback --- */

typedef void (*VMPrintFn)(const char* text, uint32_t len, void* ctx);

/* --- VM state --- */

typedef struct {
  JaclVal        stack[VM_STACK_MAX];
  uint32_t       stack_top;   /* index of next free slot */
  uint8_t*       ip;          /* instruction pointer */
  BytecodeChunk* chunk;
  VMPrintFn      print_fn;   /* output callback, defaults to stdout */
  void*          print_ctx;  /* user context for print callback */
} VM;

/* --- API --- */

static void     vm_init(VM* vm);
static VMResult vm_exec(VM* vm, BytecodeChunk* chunk);

#endif /* VM_H */

/* =========================================================================
 * Implementation Section
 * Define VM_IMPLEMENTATION before including to generate function bodies.
 * ========================================================================= */

#ifdef VM_IMPLEMENTATION
#ifndef VM_IMPL_GUARD_
#define VM_IMPL_GUARD_

#include <stdio.h>
#include <string.h>

/* --- Default print function: write to stdout --- */

static void vm__default_print(const char* text, uint32_t len, void* ctx) {
  (void)ctx;
  fwrite(text, 1, len, stdout);
}

/**
 * Initialize the VM to a clean state.
 */
static void vm_init(VM* vm) {
  memset(vm->stack, 0, sizeof(vm->stack));
  vm->stack_top = 0;
  vm->ip        = NULL;
  vm->chunk     = NULL;
  vm->print_fn  = vm__default_print;
  vm->print_ctx = NULL;
}

/* --- Stack helpers --- */

static VMResult vm__push(VM* vm, JaclVal value) {
  if (vm->stack_top >= VM_STACK_MAX) {
    return VM_STACK_OVERFLOW;
  }
  vm->stack[vm->stack_top++] = value;
  return VM_OK;
}

static VMResult vm__pop(VM* vm, JaclVal* out) {
  if (vm->stack_top == 0) {
    return VM_RUNTIME_ERROR;
  }
  *out = vm->stack[--vm->stack_top];
  return VM_OK;
}

/* --- Instruction pointer helpers --- */

static uint8_t vm__read_byte(VM* vm) {
  return *vm->ip++;
}

static uint16_t vm__read_u16(VM* vm) {
  uint8_t hi = vm__read_byte(vm);
  uint8_t lo = vm__read_byte(vm);
  return (uint16_t)((hi << 8) | lo);
}

/**
 * Execute a bytecode chunk.
 * Returns VM_OK on successful completion (OP_HALT),
 * VM_RUNTIME_ERROR on stack underflow or unknown opcode,
 * VM_STACK_OVERFLOW on stack overflow.
 */
static VMResult vm_exec(VM* vm, BytecodeChunk* chunk) {
  vm->chunk = chunk;
  vm->ip    = chunk->code;

  for (;;) {
    uint8_t instruction = vm__read_byte(vm);
    VMResult result;

    switch (instruction) {

      case OP_CONST: {
        uint16_t index = vm__read_u16(vm);
        result = vm__push(vm, chunk->constants[index]);
        if (result != VM_OK) return result;
        break;
      }

      case OP_NIL: {
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_TRUE: {
        result = vm__push(vm, JACL_TRUE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_FALSE: {
        result = vm__push(vm, JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_POP: {
        JaclVal discard;
        result = vm__pop(vm, &discard);
        if (result != VM_OK) return result;
        break;
      }

      case OP_HALT: {
        return VM_OK;
      }

      default: {
        /* Unknown or unimplemented opcode */
        return VM_RUNTIME_ERROR;
      }
    }
  }
}

#endif /* VM_IMPL_GUARD_ */
#endif /* VM_IMPLEMENTATION */
