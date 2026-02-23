/*
 * Bytecode Chunk and Opcode Definitions
 *
 * Stack-based bytecode format for the JACL VM. A BytecodeChunk holds
 * compiled instructions, a constant pool, and a line-number table
 * for error reporting.
 */

#ifndef BYTECODE_C
#define BYTECODE_C

#include <string.h>

/* --- Opcodes --- */

typedef enum {
  OP_CONST,       /* push constant: followed by uint16_t index */
  OP_NIL,         /* push nil */
  OP_TRUE,        /* push true */
  OP_FALSE,       /* push false */
  OP_POP,         /* discard top of stack */
  OP_ADD,         /* pop two, push sum */
  OP_SUB,         /* pop two, push difference */
  OP_MUL,         /* pop two, push product */
  OP_DIV,         /* pop two, push quotient */
  OP_MOD,         /* pop two, push remainder */
  OP_NEG,         /* negate top of stack */
  OP_EQ,          /* pop two, push equality result */
  OP_LT,          /* pop two, push less-than result */
  OP_GT,          /* pop two, push greater-than result */
  OP_LE,          /* pop two, push less-or-equal result */
  OP_GE,          /* pop two, push greater-or-equal result */
  OP_PRINT,         /* pop one, print it */
  OP_DEF_GLOBAL,    /* define global: followed by uint16_t name index */
  OP_GET_GLOBAL,    /* get global: followed by uint16_t name index */
  OP_GET_LOCAL,     /* push local: followed by uint8_t slot index */
  OP_SET_LOCAL,     /* set local: followed by uint8_t slot index */
  OP_GET_UPVALUE,   /* push upvalue: followed by uint8_t upvalue index */
  OP_JUMP,          /* forward jump: followed by uint16_t offset */
  OP_JUMP_IF_FALSE, /* conditional jump: followed by uint16_t offset, pops condition */
  OP_LOOP,          /* backward jump: followed by uint16_t offset */
  OP_CALL,          /* call closure: followed by uint8_t arg count */
  OP_RETURN,        /* return from call */
  OP_CLOSURE,       /* create closure: followed by uint16_t const index, then N upvalue descriptors */
  OP_POP_N,         /* discard N values: followed by uint8_t count */
  OP_CONCAT,        /* pop two strings, push concatenation */
  OP_STR_LEN,       /* pop string, push i32 length */
  OP_STR_INDEX,     /* pop index + string, push char or nil */
  OP_STR_SLICE,     /* pop end + start + string, push substring */
  OP_TO_STRING,     /* pop value, push string representation */
  OP_VEC,           /* construct vector: followed by uint8_t element count */
  OP_VEC_GET,       /* pop index + vec, push element or nil */
  OP_VEC_LEN,       /* pop vec, push i32 count */
  OP_VEC_PUSH,      /* pop elem + vec, push new vec with elem appended */
  OP_VEC_SET,       /* pop elem + index + vec, push new vec or nil */
  OP_VEC_CONCAT,    /* pop vec2 + vec1, push concatenated vec */
  OP_VEC_SLICE,     /* pop end + start + vec, push sub-vector */
  OP_HALT           /* stop execution */
} OpCode;

/* --- Initial capacities --- */

#define BYTECODE_INIT_CODE_CAP  256
#define BYTECODE_INIT_CONST_CAP 64

/* --- BytecodeChunk --- */

typedef struct {
  uint8_t*  code;         /* bytecode array */
  uint32_t  code_count;   /* number of bytes written */
  uint32_t  code_cap;     /* allocated capacity */
  JaclVal*  constants;    /* constant pool */
  uint32_t  const_count;  /* number of constants */
  uint32_t  const_cap;    /* allocated capacity */
  uint32_t* lines;        /* source line per bytecode byte */
  uint32_t  lines_cap;    /* allocated capacity for lines */
  arena_t*  arena;        /* arena for all allocations */
} BytecodeChunk;

/* --- API --- */

static void     chunk_init(BytecodeChunk* chunk, arena_t* arena);
static void     chunk_write(BytecodeChunk* chunk, uint8_t byte, uint32_t line);
static void     chunk_write_u16(BytecodeChunk* chunk, uint16_t value, uint32_t line);
static uint16_t chunk_add_constant(BytecodeChunk* chunk, JaclVal value);

/**
 * Initialize a bytecode chunk. All storage is arena-allocated.
 */
static void chunk_init(BytecodeChunk* chunk, arena_t* arena) {
  chunk->arena       = arena;
  chunk->code_count  = 0;
  chunk->code_cap    = BYTECODE_INIT_CODE_CAP;
  chunk->code        = (uint8_t*)arena_alloc(arena, BYTECODE_INIT_CODE_CAP * sizeof(uint8_t));
  chunk->const_count = 0;
  chunk->const_cap   = BYTECODE_INIT_CONST_CAP;
  chunk->constants   = (JaclVal*)arena_alloc(arena, BYTECODE_INIT_CONST_CAP * sizeof(JaclVal));
  chunk->lines_cap   = BYTECODE_INIT_CODE_CAP;
  chunk->lines       = (uint32_t*)arena_alloc(arena, BYTECODE_INIT_CODE_CAP * sizeof(uint32_t));
}

/**
 * Internal: grow an arena-allocated array by doubling capacity.
 * Copies existing data to the new allocation.
 */
static void bytecode__grow_code(BytecodeChunk* chunk) {
  uint32_t new_cap = chunk->code_cap * 2;
  uint8_t* new_code = (uint8_t*)arena_alloc(chunk->arena, new_cap * sizeof(uint8_t));
  memcpy(new_code, chunk->code, chunk->code_count * sizeof(uint8_t));
  chunk->code = new_code;

  uint32_t* new_lines = (uint32_t*)arena_alloc(chunk->arena, new_cap * sizeof(uint32_t));
  memcpy(new_lines, chunk->lines, chunk->code_count * sizeof(uint32_t));
  chunk->lines = new_lines;

  chunk->code_cap  = new_cap;
  chunk->lines_cap = new_cap;
}

static void bytecode__grow_constants(BytecodeChunk* chunk) {
  uint32_t new_cap = chunk->const_cap * 2;
  JaclVal* new_consts = (JaclVal*)arena_alloc(chunk->arena, new_cap * sizeof(JaclVal));
  memcpy(new_consts, chunk->constants, chunk->const_count * sizeof(JaclVal));
  chunk->constants = new_consts;
  chunk->const_cap = new_cap;
}

/**
 * Append a single byte to the chunk's bytecode array.
 */
static void chunk_write(BytecodeChunk* chunk, uint8_t byte, uint32_t line) {
  if (chunk->code_count >= chunk->code_cap) {
    bytecode__grow_code(chunk);
  }
  chunk->code[chunk->code_count]  = byte;
  chunk->lines[chunk->code_count] = line;
  chunk->code_count++;
}

/**
 * Append a 2-byte big-endian value to the chunk's bytecode array.
 */
static void chunk_write_u16(BytecodeChunk* chunk, uint16_t value, uint32_t line) {
  chunk_write(chunk, (uint8_t)((value >> 8) & 0xFF), line);
  chunk_write(chunk, (uint8_t)(value & 0xFF), line);
}

/**
 * Add a constant to the constant pool.
 * Returns the index of the added constant.
 */
static uint16_t chunk_add_constant(BytecodeChunk* chunk, JaclVal value) {
  if (chunk->const_count >= chunk->const_cap) {
    bytecode__grow_constants(chunk);
  }
  uint16_t index = (uint16_t)chunk->const_count;
  chunk->constants[chunk->const_count] = value;
  chunk->const_count++;
  return index;
}

/* --- JaclClosure --- */

typedef struct {
  BytecodeChunk chunk;        /* compiled body */
  uint8_t       param_count;  /* number of parameters */
  JaclVal*      param_names;  /* inline string array (arena-allocated) */
  JaclVal*      upvalues;     /* captured values array (arena-allocated) */
  uint8_t       upvalue_count;/* number of upvalues */
  const char*   name;         /* procedure name for debug, may be NULL */
  uint8_t       min_args;     /* minimum argument count (== param_count for fixed-arity) */
  bool          variadic;     /* true if proc accepts variable args (future use) */
} JaclClosure;

static inline JaclVal jacl_closure(JaclClosure* cl) {
  return jacl_closure_ptr(cl);
}

static inline JaclClosure* jacl_as_closure(JaclVal v) {
  return (JaclClosure*)jacl_as_ptr(v);
}

#endif /* BYTECODE_C */
