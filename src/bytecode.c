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
  OP_TAIL_CALL,     /* tail call: followed by uint8_t arg count; reuses current frame */
  OP_RETURN,        /* return from call */
  OP_CLOSURE,       /* create closure: followed by uint16_t const index, then N upvalue descriptors */
  OP_POP_N,         /* discard N values: followed by uint8_t count */
  OP_CONCAT,        /* pop two strings, push concatenation */
  OP_STR_LEN,       /* pop string, push i32 length */
  OP_STR_BYTE_LEN,  /* pop string, push i32 byte length */
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
  OP_MAP,           /* construct map: followed by uint8_t pair count */
  OP_MAP_GET,       /* pop key + map, push value or nil */
  OP_MAP_HAS,       /* pop key + map, push bool */
  OP_MAP_LEN,       /* pop map, push i32 count */
  OP_MAP_SET,       /* pop val + key + map, push new map */
  OP_MAP_REMOVE,    /* pop key + map, push new map */
  OP_MAP_KEYS,      /* pop map, push vector of keys */
  OP_MAP_VALS,      /* pop map, push vector of values */
  OP_EACH,          /* iterate: pop closure + collection, call closure per element */
  OP_TRANSFORM,     /* transform: pop closure + collection, call closure per element, build new collection */
  OP_FILTER,        /* filter: pop closure + collection, call closure per element, keep truthy results */
  OP_ERROR,         /* mark top-of-stack as error: peek value, set error flag, leave on stack */
  OP_IS_ERROR,      /* error predicate: pop value, push true if error-flagged, else false */
  OP_ERROR_VAL,     /* extract error payload: peek top-of-stack, clear error flag, leave on stack */
  OP_CHECK_ERROR,   /* check error: uint16_t offset; 0=return from frame, nonzero=jump to handler */
  OP_JUMP_IF_ERROR, /* conditional error jump: uint16_t offset; peek top, jump if error-flagged */
  OP_STACK_TRACE,   /* push stack trace string of most recent error */
  OP_MAKE_CELL,     /* pop value, wrap in JaclMutableRef cell, push cell */
  OP_GET_CELL_LOCAL,    /* read cell from local slot, push inner value */
  OP_SET_CELL_LOCAL,    /* pop value, store in cell at local slot, push nil */
  OP_GET_CELL_UPVALUE,  /* read cell from upvalue, push inner value */
  OP_SET_CELL_UPVALUE,  /* pop value, store in cell at upvalue, push nil */
  OP_SET_GLOBAL,    /* pop value, set global by name index, push nil */
  OP_BOX,           /* pop value, wrap in box, push box */
  OP_ATOM,          /* pop value, wrap in atom, push atom */
  OP_DEREF,         /* pop box/atom, push inner value */
  OP_RESET,         /* pop value + box/atom, store value, push new value */
  OP_SWAP,          /* pop closure + box/atom, apply closure to inner, store result */
  OP_IS_BOX,        /* pop value, push true if box, else false */
  OP_IS_ATOM,       /* pop value, push true if atom, else false */
  OP_IS_FUTURE,     /* pop value, push true if future, else false */
  /* M13 CPS and concurrency opcodes */
  OP_AWAIT,         /* pop continuation + future, suspend CPS chain */
  OP_SPAWN,         /* pop closure, create future + task, push future */
  OP_RESOLVE_FUTURE,/* pop result + future, resolve future, push nil */
  OP_PARALLEL,      /* parallel: uint8_t N; pop continuation + N closures, fork N tasks */
  OP_RACE,          /* race: uint8_t N; pop continuation + N closures, first-to-complete wins */
  OP_COMPLETE_PARALLEL, /* pop result + index + agg_val, complete parallel slot */
  OP_COMPLETE_RACE,     /* pop result + agg_val, CAS-settle race, schedule winner */
  /* M11 typed opcodes — i64 arithmetic and comparisons */
  OP_ADD_I64,       /* pop two raw i64, push sum */
  OP_SUB_I64,       /* pop two raw i64, push difference */
  OP_MUL_I64,       /* pop two raw i64, push product */
  OP_DIV_I64,       /* pop two raw i64, push quotient (div by zero = VM error) */
  OP_MOD_I64,       /* pop two raw i64, push remainder (div by zero = VM error) */
  OP_NEG_I64,       /* negate raw i64 */
  OP_LT_I64,        /* pop two raw i64, push bool */
  OP_GT_I64,        /* pop two raw i64, push bool */
  OP_LE_I64,        /* pop two raw i64, push bool */
  OP_GE_I64,        /* pop two raw i64, push bool */
  OP_EQ_I64,        /* pop two raw i64, push bool */
  /* u64 ops (only unsigned-specific: div, mod, comparisons) */
  OP_DIV_U64,       /* pop two raw u64, push quotient (unsigned) */
  OP_MOD_U64,       /* pop two raw u64, push remainder (unsigned) */
  OP_LT_U64,        /* pop two raw u64, push bool (unsigned) */
  OP_GT_U64,        /* pop two raw u64, push bool (unsigned) */
  OP_LE_U64,        /* pop two raw u64, push bool (unsigned) */
  OP_GE_U64,        /* pop two raw u64, push bool (unsigned) */
  /* f64 arithmetic and comparisons */
  OP_ADD_F64,       /* pop two raw f64, push sum */
  OP_SUB_F64,       /* pop two raw f64, push difference */
  OP_MUL_F64,       /* pop two raw f64, push product */
  OP_DIV_F64,       /* pop two raw f64, push quotient (div by zero = VM error) */
  OP_MOD_F64,       /* pop two raw f64, push remainder via fmod */
  OP_NEG_F64,       /* negate raw f64 */
  OP_LT_F64,        /* pop two raw f64, push bool */
  OP_GT_F64,        /* pop two raw f64, push bool */
  OP_LE_F64,        /* pop two raw f64, push bool */
  OP_GE_F64,        /* pop two raw f64, push bool */
  OP_EQ_F64,        /* pop two raw f64, push bool */
  /* Type conversion opcodes (each reads 1-byte source type operand) */
  OP_TO_I32,        /* convert to i32 */
  OP_TO_I64,        /* convert to raw i64 */
  OP_TO_U32,        /* convert to u32 */
  OP_TO_U64,        /* convert to raw u64 */
  OP_TO_F32,        /* convert to f32 */
  OP_TO_F64,        /* convert to raw f64 */
  OP_TO_DYN,        /* box raw value into tagged JaclVal */
  /* Typed constant opcodes (each reads uint16_t constant index) */
  OP_CONST_I64,     /* push raw i64 from constant pool */
  OP_CONST_U64,     /* push raw u64 from constant pool */
  OP_CONST_F64,     /* push raw f64 from constant pool */
  OP_STRUCT_NEW,    /* construct struct: followed by uint16_t struct_type_index */
  OP_STRUCT_GET,    /* field access: followed by uint16_t field_offset, uint8_t field_type */
  OP_STRUCT_SET,    /* field mutation: followed by uint16_t field_offset, uint8_t field_type */
  OP_STRUCT_GET_DYN,/* runtime field access: followed by uint16_t const_idx (field name) */
  OP_STRUCT_SET_DYN,/* runtime field mutation: followed by uint16_t const_idx (field name) */
  OP_CLOSE_LOOP,    /* pop N values under top-of-stack: followed by uint8_t count */
  OP_DESTRUCTURE_VEC, /* destructure vector: uint8_t N, uint8_t skip_mask; pop vec, push non-skipped elements */
  OP_DESTRUCTURE_NAMED, /* destructure struct/map by field names: uint8_t N, then N x uint16_t const_idx */
  OP_DESTRUCTURE_VEC_REST, /* destructure vector with rest: uint8_t N; pop vec, push N elements + rest vector */
  OP_DESTRUCTURE_NAMED_REST, /* destructure named with rest: uint8_t N, N x uint16_t const_idx; pop struct/map, push N fields + rest map */
  OP_SPREAD,          /* pop vector, push each element; save count in vm->spread_counts */
  OP_CALL_SPREAD,     /* call with spread: uint8_t fixed_args, uint8_t num_spreads */
  OP_FOLD_SPREAD,     /* fold binary op with spread: uint8_t op_id, uint8_t fixed_args, uint8_t num_spreads */
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
  bool          pinned;       /* true if closure must run on a specific worker thread
                                 (set when concurrent body touches mutable globals) */
  int8_t        pin_worker_id; /* worker ID to pin to (-1 = not yet assigned) */
} JaclClosure;

static inline JaclVal jacl_closure(JaclClosure* cl) {
  return jacl_closure_ptr(cl);
}

static inline JaclClosure* jacl_as_closure(JaclVal v) {
  return (JaclClosure*)jacl_as_ptr(v);
}

/* --- Opcode name lookup (debug/error messages) --- */

static const char* bytecode__opcode_name(uint8_t op) {
  switch ((OpCode)op) {
    case OP_CONST:           return "OP_CONST";
    case OP_NIL:             return "OP_NIL";
    case OP_TRUE:            return "OP_TRUE";
    case OP_FALSE:           return "OP_FALSE";
    case OP_POP:             return "OP_POP";
    case OP_ADD:             return "OP_ADD";
    case OP_SUB:             return "OP_SUB";
    case OP_MUL:             return "OP_MUL";
    case OP_DIV:             return "OP_DIV";
    case OP_MOD:             return "OP_MOD";
    case OP_NEG:             return "OP_NEG";
    case OP_EQ:              return "OP_EQ";
    case OP_LT:              return "OP_LT";
    case OP_GT:              return "OP_GT";
    case OP_LE:              return "OP_LE";
    case OP_GE:              return "OP_GE";
    case OP_PRINT:           return "OP_PRINT";
    case OP_DEF_GLOBAL:      return "OP_DEF_GLOBAL";
    case OP_GET_GLOBAL:      return "OP_GET_GLOBAL";
    case OP_GET_LOCAL:       return "OP_GET_LOCAL";
    case OP_SET_LOCAL:       return "OP_SET_LOCAL";
    case OP_GET_UPVALUE:     return "OP_GET_UPVALUE";
    case OP_JUMP:            return "OP_JUMP";
    case OP_JUMP_IF_FALSE:   return "OP_JUMP_IF_FALSE";
    case OP_LOOP:            return "OP_LOOP";
    case OP_CALL:            return "OP_CALL";
    case OP_TAIL_CALL:       return "OP_TAIL_CALL";
    case OP_RETURN:          return "OP_RETURN";
    case OP_CLOSURE:         return "OP_CLOSURE";
    case OP_POP_N:           return "OP_POP_N";
    case OP_CONCAT:          return "OP_CONCAT";
    case OP_STR_LEN:         return "OP_STR_LEN";
    case OP_STR_BYTE_LEN:   return "OP_STR_BYTE_LEN";
    case OP_STR_INDEX:       return "OP_STR_INDEX";
    case OP_STR_SLICE:       return "OP_STR_SLICE";
    case OP_TO_STRING:       return "OP_TO_STRING";
    case OP_VEC:             return "OP_VEC";
    case OP_VEC_GET:         return "OP_VEC_GET";
    case OP_VEC_LEN:         return "OP_VEC_LEN";
    case OP_VEC_PUSH:        return "OP_VEC_PUSH";
    case OP_VEC_SET:         return "OP_VEC_SET";
    case OP_VEC_CONCAT:      return "OP_VEC_CONCAT";
    case OP_VEC_SLICE:       return "OP_VEC_SLICE";
    case OP_MAP:             return "OP_MAP";
    case OP_MAP_GET:         return "OP_MAP_GET";
    case OP_MAP_HAS:         return "OP_MAP_HAS";
    case OP_MAP_LEN:         return "OP_MAP_LEN";
    case OP_MAP_SET:         return "OP_MAP_SET";
    case OP_MAP_REMOVE:      return "OP_MAP_REMOVE";
    case OP_MAP_KEYS:        return "OP_MAP_KEYS";
    case OP_MAP_VALS:        return "OP_MAP_VALS";
    case OP_EACH:            return "OP_EACH";
    case OP_TRANSFORM:       return "OP_TRANSFORM";
    case OP_FILTER:          return "OP_FILTER";
    case OP_ERROR:           return "OP_ERROR";
    case OP_IS_ERROR:        return "OP_IS_ERROR";
    case OP_ERROR_VAL:       return "OP_ERROR_VAL";
    case OP_CHECK_ERROR:     return "OP_CHECK_ERROR";
    case OP_JUMP_IF_ERROR:   return "OP_JUMP_IF_ERROR";
    case OP_STACK_TRACE:     return "OP_STACK_TRACE";
    case OP_MAKE_CELL:       return "OP_MAKE_CELL";
    case OP_GET_CELL_LOCAL:  return "OP_GET_CELL_LOCAL";
    case OP_SET_CELL_LOCAL:  return "OP_SET_CELL_LOCAL";
    case OP_GET_CELL_UPVALUE: return "OP_GET_CELL_UPVALUE";
    case OP_SET_CELL_UPVALUE: return "OP_SET_CELL_UPVALUE";
    case OP_SET_GLOBAL:      return "OP_SET_GLOBAL";
    case OP_BOX:             return "OP_BOX";
    case OP_ATOM:            return "OP_ATOM";
    case OP_DEREF:           return "OP_DEREF";
    case OP_RESET:           return "OP_RESET";
    case OP_SWAP:            return "OP_SWAP";
    case OP_IS_BOX:          return "OP_IS_BOX";
    case OP_IS_ATOM:         return "OP_IS_ATOM";
    case OP_IS_FUTURE:       return "OP_IS_FUTURE";
    case OP_AWAIT:           return "OP_AWAIT";
    case OP_SPAWN:           return "OP_SPAWN";
    case OP_RESOLVE_FUTURE:  return "OP_RESOLVE_FUTURE";
    case OP_PARALLEL:        return "OP_PARALLEL";
    case OP_RACE:            return "OP_RACE";
    case OP_COMPLETE_PARALLEL: return "OP_COMPLETE_PARALLEL";
    case OP_COMPLETE_RACE:     return "OP_COMPLETE_RACE";
    case OP_ADD_I64:         return "OP_ADD_I64";
    case OP_SUB_I64:         return "OP_SUB_I64";
    case OP_MUL_I64:         return "OP_MUL_I64";
    case OP_DIV_I64:         return "OP_DIV_I64";
    case OP_MOD_I64:         return "OP_MOD_I64";
    case OP_NEG_I64:         return "OP_NEG_I64";
    case OP_LT_I64:          return "OP_LT_I64";
    case OP_GT_I64:          return "OP_GT_I64";
    case OP_LE_I64:          return "OP_LE_I64";
    case OP_GE_I64:          return "OP_GE_I64";
    case OP_EQ_I64:          return "OP_EQ_I64";
    case OP_DIV_U64:         return "OP_DIV_U64";
    case OP_MOD_U64:         return "OP_MOD_U64";
    case OP_LT_U64:          return "OP_LT_U64";
    case OP_GT_U64:          return "OP_GT_U64";
    case OP_LE_U64:          return "OP_LE_U64";
    case OP_GE_U64:          return "OP_GE_U64";
    case OP_ADD_F64:         return "OP_ADD_F64";
    case OP_SUB_F64:         return "OP_SUB_F64";
    case OP_MUL_F64:         return "OP_MUL_F64";
    case OP_DIV_F64:         return "OP_DIV_F64";
    case OP_MOD_F64:         return "OP_MOD_F64";
    case OP_NEG_F64:         return "OP_NEG_F64";
    case OP_LT_F64:          return "OP_LT_F64";
    case OP_GT_F64:          return "OP_GT_F64";
    case OP_LE_F64:          return "OP_LE_F64";
    case OP_GE_F64:          return "OP_GE_F64";
    case OP_EQ_F64:          return "OP_EQ_F64";
    case OP_TO_I32:          return "OP_TO_I32";
    case OP_TO_I64:          return "OP_TO_I64";
    case OP_TO_U32:          return "OP_TO_U32";
    case OP_TO_U64:          return "OP_TO_U64";
    case OP_TO_F32:          return "OP_TO_F32";
    case OP_TO_F64:          return "OP_TO_F64";
    case OP_TO_DYN:          return "OP_TO_DYN";
    case OP_CONST_I64:       return "OP_CONST_I64";
    case OP_CONST_U64:       return "OP_CONST_U64";
    case OP_CONST_F64:       return "OP_CONST_F64";
    case OP_STRUCT_NEW:      return "OP_STRUCT_NEW";
    case OP_STRUCT_GET:      return "OP_STRUCT_GET";
    case OP_STRUCT_SET:      return "OP_STRUCT_SET";
    case OP_STRUCT_GET_DYN:  return "OP_STRUCT_GET_DYN";
    case OP_STRUCT_SET_DYN:  return "OP_STRUCT_SET_DYN";
    case OP_CLOSE_LOOP:      return "OP_CLOSE_LOOP";
    case OP_DESTRUCTURE_VEC: return "OP_DESTRUCTURE_VEC";
    case OP_DESTRUCTURE_NAMED: return "OP_DESTRUCTURE_NAMED";
    case OP_DESTRUCTURE_VEC_REST: return "OP_DESTRUCTURE_VEC_REST";
    case OP_DESTRUCTURE_NAMED_REST: return "OP_DESTRUCTURE_NAMED_REST";
    case OP_SPREAD:          return "OP_SPREAD";
    case OP_CALL_SPREAD:     return "OP_CALL_SPREAD";
    case OP_FOLD_SPREAD:     return "OP_FOLD_SPREAD";
    case OP_HALT:            return "OP_HALT";
  }
  return "OP_UNKNOWN";
}

#endif /* BYTECODE_C */
