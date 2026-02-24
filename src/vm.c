/*
 * JACL Virtual Machine
 *
 * Stack-based bytecode interpreter. Executes BytecodeChunk instructions
 * using a fixed-size operand stack.
 */

#ifndef VM_C
#define VM_C

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* --- Stack size --- */

#define VM_STACK_MAX 256
#define VM_FRAMES_MAX 64

/* --- Environment initial capacity --- */

#define VM_ENV_INIT_CAP 16

/* --- Result codes --- */

typedef enum {
  VM_OK,
  VM_RUNTIME_ERROR,
  VM_STACK_OVERFLOW
} VMResult;

/* --- Print callback --- */

typedef void (*VMPrintFn)(const char* text, uint32_t len, void* ctx);

/* --- Environment --- */

typedef struct {
  JaclVal*  names;    /* inline string names */
  JaclVal*  values;   /* corresponding values */
  uint32_t  count;
  uint32_t  cap;
} Environment;

/* --- Call frame --- */

typedef struct {
  JaclClosure*   closure;     /* closure being executed */
  uint8_t*       return_ip;   /* caller's ip to restore on return */
  uint32_t       stack_base;  /* first slot for this frame's locals */
  BytecodeChunk* chunk;       /* chunk being executed */
} CallFrame;

/* --- Stack trace --- */

#define VM_STACK_TRACE_MAX 32

typedef struct {
  const char* function_name;
  uint32_t    line_number;
} StackTraceEntry;

typedef struct {
  StackTraceEntry entries[VM_STACK_TRACE_MAX];
  uint32_t        count;
} StackTrace;

/* --- VM state --- */

typedef struct {
  JaclVal        stack[VM_STACK_MAX];
  uint32_t       stack_top;   /* index of next free slot */
  CallFrame      frames[VM_FRAMES_MAX];
  uint32_t       frame_count;
  uint8_t*       ip;          /* instruction pointer */
  BytecodeChunk* chunk;
  VMPrintFn      print_fn;   /* output callback, defaults to stdout */
  void*          print_ctx;  /* user context for print callback */
  Environment    env;
  arena_t*       arena;
  JaclInternTable* intern_table;  /* shared intern table for concat/interning */
  const char*    error_message;  /* last error message, or NULL */
  uint32_t       error_line;     /* source line of last error */
  StackTrace     stack_trace;    /* most recent error's trace */
} VM;

/* --- API --- */

static void     vm_init(VM* vm, arena_t* arena);
static VMResult vm_exec(VM* vm, BytecodeChunk* chunk);

/* --- Pipeline convenience --- */

static VMResult jacl_run(const char* source, VM* vm, arena_t* arena);

/* --- Type name helper for error messages --- */

static const char* vm__type_name(JaclVal v) {
  if (jacl_is_nil(v))           return "nil";
  if (jacl_is_bool(v))          return "bool";
  if (jacl_is_i32(v))           return "i32";
  if (jacl_is_f32(v))           return "f32";
  if (jacl_is_string(v))        return "string";
  if (jacl_is_closure(v))       return "closure";
  if (jacl_is_vector(v))        return "vector";
  if (jacl_is_map(v))           return "map";
  return "unknown";
}

/* --- Error reporting helper --- */

static void vm__set_error(VM* vm, const char* fmt, ...) {
  va_list ap;
  char buf[256];
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) n = 0;
  uint32_t len = (uint32_t)n;
  char* msg = (char*)arena_alloc(vm->arena, len + 1);
  memcpy(msg, buf, len + 1);
  vm->error_message = msg;
}

/* --- Default print function: write to stdout --- */

static void vm__default_print(const char* text, uint32_t len, void* ctx) {
  (void)ctx;
  fwrite(text, 1, len, stdout);
}

/* --- Truthiness helper --- */

static bool vm__is_falsy(JaclVal v) {
  return jacl_is_nil(v) || v == JACL_FALSE;
}

/* --- Stack trace capture --- */

/**
 * Capture the current call frame chain into the VM's stack trace.
 * Walks frames from innermost to outermost.
 */
static void vm__capture_trace(VM* vm) {
  vm->stack_trace.count = 0;
  for (uint32_t i = vm->frame_count; i > 0 && vm->stack_trace.count < VM_STACK_TRACE_MAX; i--) {
    CallFrame* f = &vm->frames[i - 1];
    StackTraceEntry* entry = &vm->stack_trace.entries[vm->stack_trace.count++];
    entry->function_name = f->closure ? f->closure->name : NULL;

    if (i == vm->frame_count) {
      /* Current (innermost) frame: use the tracked error_line */
      entry->line_number = vm->error_line;
    } else {
      /* Parent frame: derive line from child's return_ip in this frame's chunk */
      CallFrame* child = &vm->frames[i];
      uint32_t offset = (uint32_t)(child->return_ip - f->chunk->code);
      if (offset > 0) offset--;
      entry->line_number = f->chunk->lines[offset];
    }
  }
}

/**
 * Initialize the VM to a clean state.
 * Arena is used for environment storage.
 */
static void vm_init(VM* vm, arena_t* arena) {
  memset(vm->stack, 0, sizeof(vm->stack));
  vm->stack_top = 0;
  vm->ip        = NULL;
  vm->chunk     = NULL;
  vm->print_fn  = vm__default_print;
  vm->print_ctx = NULL;
  vm->arena         = arena;
  vm->intern_table  = NULL;
  vm->frame_count   = 0;
  vm->error_message = NULL;
  vm->error_line    = 0;
  vm->stack_trace.count = 0;

  /* Ensure HAMT key handlers are wired up */
  collections__init();

  /* Initialize environment */
  vm->env.count  = 0;
  vm->env.cap    = VM_ENV_INIT_CAP;
  vm->env.names  = (JaclVal*)arena_alloc(arena, VM_ENV_INIT_CAP * sizeof(JaclVal));
  vm->env.values = (JaclVal*)arena_alloc(arena, VM_ENV_INIT_CAP * sizeof(JaclVal));

  /* Pre-populate: true, false, nil */
  vm->env.names[0]  = jacl_inline_string("true", 4);
  vm->env.values[0] = JACL_TRUE;
  vm->env.names[1]  = jacl_inline_string("false", 5);
  vm->env.values[1] = JACL_FALSE;
  vm->env.names[2]  = jacl_inline_string("nil", 3);
  vm->env.values[2] = JACL_NIL;
  vm->env.count = 3;
}

/* --- Stack helpers --- */

static VMResult vm__push(VM* vm, JaclVal value) {
  if (vm->stack_top >= VM_STACK_MAX) {
    vm->error_message = "stack overflow";
    return VM_STACK_OVERFLOW;
  }
  vm->stack[vm->stack_top++] = value;
  return VM_OK;
}

static VMResult vm__pop(VM* vm, JaclVal* out) {
  if (vm->stack_top == 0) {
    vm->error_message = "stack underflow";
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

/* --- Environment helpers --- */

static void vm__env_grow(VM* vm) {
  uint32_t new_cap = vm->env.cap * 2;
  JaclVal* new_names  = (JaclVal*)arena_alloc(vm->arena, new_cap * sizeof(JaclVal));
  JaclVal* new_values = (JaclVal*)arena_alloc(vm->arena, new_cap * sizeof(JaclVal));
  memcpy(new_names, vm->env.names, vm->env.count * sizeof(JaclVal));
  memcpy(new_values, vm->env.values, vm->env.count * sizeof(JaclVal));
  vm->env.names  = new_names;
  vm->env.values = new_values;
  vm->env.cap    = new_cap;
}

static void vm__env_set(VM* vm, JaclVal name, JaclVal value) {
  /* Check if name already exists */
  for (uint32_t i = 0; i < vm->env.count; i++) {
    if (vm->env.names[i] == name) {
      vm->env.values[i] = value;
      return;
    }
  }
  /* New entry */
  if (vm->env.count >= vm->env.cap) {
    vm__env_grow(vm);
  }
  vm->env.names[vm->env.count]  = name;
  vm->env.values[vm->env.count] = value;
  vm->env.count++;
}

static JaclVal vm__env_get(VM* vm, JaclVal name, bool* found) {
  for (uint32_t i = 0; i < vm->env.count; i++) {
    if (vm->env.names[i] == name) {
      *found = true;
      return vm->env.values[i];
    }
  }
  *found = false;
  return JACL_NIL;
}

/* --- Binary numeric operation macro --- */

#define VM__BINARY_NUMERIC_OP(fn_i32, fn_f32, op_name)                       \
  do {                                                                        \
    JaclVal b, a;                                                             \
    result = vm__pop(vm, &b); if (result != VM_OK) return result;             \
    result = vm__pop(vm, &a); if (result != VM_OK) return result;             \
    JaclVal res;                                                              \
    if (jacl_is_i32(a) && jacl_is_i32(b)) {                                  \
      res = fn_i32(a, b);                                                     \
    } else if (jacl_is_f32(a) && jacl_is_f32(b)) {                           \
      res = fn_f32(a, b);                                                     \
    } else {                                                                  \
      vm__set_error(vm,                                                       \
        "type error in '%s': expected matching numeric types, got %s and %s", \
        op_name, vm__type_name(a), vm__type_name(b));                         \
      return VM_RUNTIME_ERROR;                                                \
    }                                                                         \
    if (jacl_is_error(res) && !jacl_is_error(a) && !jacl_is_error(b))        \
      vm__capture_trace(vm);                                                  \
    result = vm__push(vm, res); if (result != VM_OK) return result;           \
  } while (0)

/* --- Dynamic format buffer for collection display --- */

typedef struct {
  char*     data;
  uint32_t  len;
  uint32_t  cap;
  arena_t*  arena;
} VMFormatBuf;

static void vm__fmt_init(VMFormatBuf* buf, arena_t* arena) {
  buf->arena = arena;
  buf->len   = 0;
  buf->cap   = 128;
  buf->data  = (char*)arena_alloc(arena, 128);
}

static void vm__fmt_ensure(VMFormatBuf* buf, uint32_t extra) {
  if (buf->len + extra <= buf->cap) return;
  uint32_t new_cap = buf->cap * 2;
  while (new_cap < buf->len + extra) new_cap *= 2;
  char* new_data = (char*)arena_alloc(buf->arena, new_cap);
  memcpy(new_data, buf->data, buf->len);
  buf->data = new_data;
  buf->cap  = new_cap;
}

static void vm__fmt_append(VMFormatBuf* buf, const char* str, uint32_t len) {
  vm__fmt_ensure(buf, len);
  memcpy(buf->data + buf->len, str, len);
  buf->len += len;
}

static void vm__fmt_value(VMFormatBuf* buf, JaclVal val) {
  char tmp[64];
  int n;

  if (jacl_is_nil(val)) {
    vm__fmt_append(buf, "nil", 3);
  } else if (jacl_is_bool(val)) {
    if (val == JACL_TRUE) vm__fmt_append(buf, "true", 4);
    else vm__fmt_append(buf, "false", 5);
  } else if (jacl_is_i32(val)) {
    n = snprintf(tmp, sizeof(tmp), "%d", (int)jacl_as_i32(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_f32(val)) {
    n = snprintf(tmp, sizeof(tmp), "%g", (double)jacl_as_f32(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_string(val)) {
    uint32_t slen = jacl_string_len(val);
    vm__fmt_append(buf, "\"", 1);
    if (jacl_is_heap_string(val)) {
      JaclHeapString* hs = jacl_as_heap_string(val);
      vm__fmt_append(buf, hs->data, hs->length);
    } else {
      char sbuf[8];
      jacl_string_data(val, sbuf, slen);
      vm__fmt_append(buf, sbuf, slen);
    }
    vm__fmt_append(buf, "\"", 1);
  } else if (jacl_is_vector(val)) {
    jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(val);
    uint32_t count = jacl_vec_count(vec);
    vm__fmt_append(buf, "[vec", 4);
    for (uint32_t i = 0; i < count; i++) {
      vm__fmt_append(buf, " ", 1);
      jacl_vec_get_result gr = jacl_vec_get(vec, i);
      vm__fmt_value(buf, gr.value);
    }
    vm__fmt_append(buf, "]", 1);
  } else if (jacl_is_map(val)) {
    jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(val);
    vm__fmt_append(buf, "[map", 4);
    jacl_map_iter it = jacl_map_iter_init(map);
    jacl_map_iter_result ir;
    for (;;) {
      ir = jacl_map_next_leaf(&it);
      if (ir.done) break;
      JaclVal key = jacl_map_key_from_leaf(ir.item);
      JaclVal value = jacl_map_value_from_leaf(ir.item);
      vm__fmt_append(buf, " ", 1);
      vm__fmt_value(buf, key);
      vm__fmt_append(buf, " ", 1);
      vm__fmt_value(buf, value);
    }
    vm__fmt_append(buf, "]", 1);
  } else if (jacl_is_closure(val)) {
    JaclClosure* cl = jacl_as_closure(val);
    if (cl->name) {
      n = snprintf(tmp, sizeof(tmp), "<proc %s>", cl->name);
      vm__fmt_append(buf, tmp, (uint32_t)n);
    } else {
      vm__fmt_append(buf, "<closure>", 9);
    }
  } else if (jacl_is_cell(val)) {
    /* Cells are transparent — print the contained value directly */
    JaclMutableRef* ref = jacl_as_cell(val);
    vm__fmt_value(buf, ref->value);
  } else if (jacl_is_box(val)) {
    JaclMutableRef* ref = jacl_as_box(val);
    vm__fmt_append(buf, "<box: ", 6);
    vm__fmt_value(buf, ref->value);
    vm__fmt_append(buf, ">", 1);
  } else if (jacl_is_atom(val)) {
    JaclMutableRef* ref = jacl_as_atom(val);
    vm__fmt_append(buf, "<atom: ", 7);
    vm__fmt_value(buf, ref->value);
    vm__fmt_append(buf, ">", 1);
  } else {
    vm__fmt_append(buf, "<unknown>", 9);
  }
}

/* --- Deep structural equality for collections --- */

static bool vm__deep_eq(JaclVal a, JaclVal b) {
  return jacl_val_eq(a, b);
}

/* Forward declaration for recursive call from OP_EACH */
static VMResult vm__run(VM* vm, uint32_t min_frame);

/**
 * Execute a bytecode chunk.
 * Returns VM_OK on successful completion (OP_HALT),
 * VM_RUNTIME_ERROR on stack underflow or unknown opcode,
 * VM_STACK_OVERFLOW on stack overflow.
 */
static VMResult vm_exec(VM* vm, BytecodeChunk* chunk) {
  vm->error_message = NULL;
  vm->error_line    = 0;

  /* Wrap top-level code in an implicit closure/frame */
  JaclClosure top_closure;
  memset(&top_closure, 0, sizeof(top_closure));
  top_closure.chunk    = *chunk;
  top_closure.variadic = false;

  vm->frames[0].closure    = &top_closure;
  vm->frames[0].return_ip  = NULL;
  vm->frames[0].stack_base = 0;
  vm->frames[0].chunk      = chunk;
  vm->frame_count   = 1;

  vm->chunk = chunk;
  vm->ip    = chunk->code;

  return vm__run(vm, 0);
}

/**
 * Inner dispatch loop. Runs until OP_HALT or until frame_count drops
 * to min_frame (used by OP_EACH to execute closures inline).
 */
static VMResult vm__run(VM* vm, uint32_t min_frame) {
  CallFrame* frame = &vm->frames[vm->frame_count - 1];

  for (;;) {
    /* Track source line for error reporting */
    uint32_t instr_offset = (uint32_t)(vm->ip - vm->chunk->code);
    vm->error_line = vm->chunk->lines[instr_offset];

    uint8_t instruction = vm__read_byte(vm);
    VMResult result;

    switch (instruction) {

      case OP_CONST: {
        uint16_t index = vm__read_u16(vm);
        result = vm__push(vm, vm->chunk->constants[index]);
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

      case OP_ADD: {
        VM__BINARY_NUMERIC_OP(jacl_add_i32, jacl_add_f32, "+");
        break;
      }

      case OP_SUB: {
        VM__BINARY_NUMERIC_OP(jacl_sub_i32, jacl_sub_f32, "-");
        break;
      }

      case OP_MUL: {
        VM__BINARY_NUMERIC_OP(jacl_mul_i32, jacl_mul_f32, "*");
        break;
      }

      case OP_DIV: {
        VM__BINARY_NUMERIC_OP(jacl_div_i32, jacl_div_f32, "/");
        break;
      }

      case OP_MOD: {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          JaclVal mod_res = jacl_mod_i32(a, b);
          if (jacl_is_error(mod_res) && !jacl_is_error(a) && !jacl_is_error(b))
            vm__capture_trace(vm);
          result = vm__push(vm, mod_res);
          if (result != VM_OK) return result;
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          vm__set_error(vm,
            "type error in '%%': modulo is not supported for f32");
          return VM_RUNTIME_ERROR;
        } else {
          vm__set_error(vm,
            "type error in '%%': expected matching numeric types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
          return VM_RUNTIME_ERROR;
        }
        break;
      }

      case OP_NEG: {
        JaclVal a;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a)) {
          res = jacl_neg_i32(a);
        } else if (jacl_is_f32(a)) {
          res = jacl_neg_f32(a);
        } else {
          vm__set_error(vm,
            "type error in '-': expected numeric type, got %s",
            vm__type_name(a));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        break;
      }

      case OP_EQ: {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res = jacl_bool(vm__deep_eq(a, b));
        result = vm__push(vm, res);
        if (result != VM_OK) return result;
        break;
      }

      case OP_LT: {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_lt_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_lt_f32(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) < 0);
        } else {
          vm__set_error(vm,
            "type error in '<': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        break;
      }

      case OP_GT: {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_gt_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_gt_f32(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) > 0);
        } else {
          vm__set_error(vm,
            "type error in '>': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        break;
      }

      case OP_LE: {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_le_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_le_f32(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) <= 0);
        } else {
          vm__set_error(vm,
            "type error in '<=': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        break;
      }

      case OP_GE: {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        JaclVal res;
        if (jacl_is_i32(a) && jacl_is_i32(b)) {
          res = jacl_ge_i32(a, b);
        } else if (jacl_is_f32(a) && jacl_is_f32(b)) {
          res = jacl_ge_f32(a, b);
        } else if (jacl_is_string(a) && jacl_is_string(b)) {
          res = jacl_bool(jacl_string_cmp(a, b) >= 0);
        } else {
          vm__set_error(vm,
            "type error in '>=': expected matching types, got %s and %s",
            vm__type_name(a), vm__type_name(b));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        break;
      }

      case OP_PRINT: {
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;

        /* Cells are transparent — dereference before printing */
        if (jacl_is_cell(val)) {
          JaclMutableRef* ref = jacl_as_cell(val);
          val = ref->value;
        }

        char buf[256];
        const char* text;
        uint32_t len;

        if (jacl_is_error(val)) {
          /* Format as <error: PAYLOAD> using the format buffer */
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena);
          vm__fmt_append(&fmt, "<error: ", 8);
          JaclVal payload = jacl_clear_error(val);
          /* Print payload: strings without quotes, other types with fmt_value */
          if (jacl_is_string(payload)) {
            uint32_t slen = jacl_string_len(payload);
            if (jacl_is_heap_string(payload)) {
              JaclHeapString* hs = jacl_as_heap_string(payload);
              vm__fmt_append(&fmt, hs->data, hs->length);
            } else {
              char sbuf[8];
              jacl_string_data(payload, sbuf, slen);
              vm__fmt_append(&fmt, sbuf, slen);
            }
          } else {
            vm__fmt_value(&fmt, payload);
          }
          vm__fmt_append(&fmt, ">\n", 2);
          vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          break;
        } else if (jacl_is_nil(val)) {
          text = "nil\n";
          len = 4;
        } else if (jacl_is_bool(val)) {
          if (val == JACL_TRUE) { text = "true\n"; len = 5; }
          else { text = "false\n"; len = 6; }
        } else if (jacl_is_i32(val)) {
          int n = snprintf(buf, sizeof(buf), "%d\n", (int)jacl_as_i32(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_f32(val)) {
          int n = snprintf(buf, sizeof(buf), "%g\n", (double)jacl_as_f32(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_string(val)) {
          uint32_t slen = jacl_string_len(val);
          if (slen + 1 <= sizeof(buf)) {
            jacl_string_data(val, buf, slen);
            buf[slen] = '\n';
            text = buf;
            len = slen + 1;
          } else {
            /* String too long for stack buffer: print data then newline */
            if (jacl_is_heap_string(val)) {
              JaclHeapString* hs = jacl_as_heap_string(val);
              vm->print_fn(hs->data, hs->length, vm->print_ctx);
            } else {
              jacl_string_data(val, buf, sizeof(buf));
              vm->print_fn(buf, slen, vm->print_ctx);
            }
            vm->print_fn("\n", 1, vm->print_ctx);
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
            break;
          }
        } else if (jacl_is_vector(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val)) {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena);
          vm__fmt_value(&fmt, val);
          vm__fmt_append(&fmt, "\n", 1);
          vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          break;
        } else {
          text = "<unknown>\n";
          len = 10;
        }

        vm->print_fn(text, len, vm->print_ctx);

        /* print returns nil */
        result = vm__push(vm, JACL_NIL); if (result != VM_OK) return result;
        break;
      }

      case OP_DEF_GLOBAL: {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal name = vm->chunk->constants[name_idx];
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        vm__env_set(vm, name, value);
        /* def returns nil */
        result = vm__push(vm, JACL_NIL); if (result != VM_OK) return result;
        break;
      }

      case OP_GET_GLOBAL: {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal name = vm->chunk->constants[name_idx];
        bool found;
        JaclVal value = vm__env_get(vm, name, &found);
        if (!found) {
          char name_buf[8];
          jacl_inline_string_get(name, name_buf, sizeof(name_buf));
          vm__set_error(vm, "undefined variable '$%s'", name_buf);
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, value); if (result != VM_OK) return result;
        break;
      }

      case OP_GET_LOCAL: {
        uint8_t slot = vm__read_byte(vm);
        result = vm__push(vm, vm->stack[frame->stack_base + slot]);
        if (result != VM_OK) return result;
        break;
      }

      case OP_SET_LOCAL: {
        uint8_t slot = vm__read_byte(vm);
        vm->stack[frame->stack_base + slot] = vm->stack[vm->stack_top - 1];
        break;
      }

      case OP_GET_UPVALUE: {
        uint8_t index = vm__read_byte(vm);
        result = vm__push(vm, frame->closure->upvalues[index]);
        if (result != VM_OK) return result;
        break;
      }

      case OP_JUMP: {
        uint16_t offset = vm__read_u16(vm);
        vm->ip += offset;
        break;
      }

      case OP_JUMP_IF_FALSE: {
        uint16_t offset = vm__read_u16(vm);
        JaclVal condition;
        result = vm__pop(vm, &condition);
        if (result != VM_OK) return result;
        if (vm__is_falsy(condition)) {
          vm->ip += offset;
        }
        break;
      }

      case OP_LOOP: {
        uint16_t offset = vm__read_u16(vm);
        vm->ip -= offset;
        break;
      }

      case OP_CALL: {
        uint8_t arg_count = vm__read_byte(vm);
        JaclVal callee = vm->stack[vm->stack_top - arg_count - 1];

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "cannot call %s value", vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(callee);

        if (arg_count != closure->param_count) {
          vm__set_error(vm, "expected %d arguments but got %d",
                       (int)closure->param_count, (int)arg_count);
          return VM_RUNTIME_ERROR;
        }

        if (vm->frame_count >= VM_FRAMES_MAX) {
          vm__set_error(vm, "stack overflow");
          return VM_RUNTIME_ERROR;
        }

        CallFrame* new_frame = &vm->frames[vm->frame_count++];
        new_frame->closure    = closure;
        new_frame->return_ip  = vm->ip;
        new_frame->stack_base = vm->stack_top - arg_count;
        new_frame->chunk      = &closure->chunk;

        frame     = new_frame;
        vm->ip    = frame->chunk->code;
        vm->chunk = frame->chunk;
        break;
      }

      case OP_RETURN: {
        JaclVal return_value;
        result = vm__pop(vm, &return_value);
        if (result != VM_OK) return result;

        uint32_t callee_base = frame->stack_base;
        uint8_t* caller_ip   = frame->return_ip;

        vm->frame_count--;

        if (vm->frame_count == 0) {
          /* Returning from top-level */
          vm->stack[0] = return_value;
          vm->stack_top = 1;
          return VM_OK;
        }

        /* Place return value where the callee's closure was */
        vm->stack[callee_base - 1] = return_value;
        vm->stack_top = callee_base;

        frame     = &vm->frames[vm->frame_count - 1];
        vm->ip    = caller_ip;
        vm->chunk = frame->chunk;

        if (vm->frame_count <= min_frame) {
          return VM_OK;
        }
        break;
      }

      case OP_CLOSURE: {
        uint16_t index = vm__read_u16(vm);
        JaclClosure* template = jacl_as_closure(vm->chunk->constants[index]);

        /* Allocate a new closure instance with its own upvalue array */
        JaclClosure* cl = (JaclClosure*)arena_alloc(vm->arena, sizeof(JaclClosure));
        cl->chunk        = template->chunk;
        cl->param_count  = template->param_count;
        cl->param_names  = template->param_names;
        cl->name         = template->name;
        cl->upvalue_count = template->upvalue_count;
        cl->min_args     = template->min_args;
        cl->variadic     = template->variadic;

        if (cl->upvalue_count > 0) {
          cl->upvalues = (JaclVal*)arena_alloc(vm->arena,
                            sizeof(JaclVal) * cl->upvalue_count);
          for (uint8_t i = 0; i < cl->upvalue_count; i++) {
            uint8_t is_local = vm__read_byte(vm);
            uint8_t uv_index = vm__read_byte(vm);
            if (is_local) {
              cl->upvalues[i] = vm->stack[frame->stack_base + uv_index];
            } else {
              cl->upvalues[i] = frame->closure->upvalues[uv_index];
            }
          }
        } else {
          cl->upvalues = NULL;
        }

        result = vm__push(vm, jacl_closure(cl));
        if (result != VM_OK) return result;
        break;
      }

      case OP_POP_N: {
        uint8_t count = vm__read_byte(vm);
        if (vm->stack_top < count) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        vm->stack_top -= count;
        break;
      }

      case OP_CONCAT: {
        JaclVal b, a;
        result = vm__pop(vm, &b); if (result != VM_OK) return result;
        result = vm__pop(vm, &a); if (result != VM_OK) return result;
        if (jacl_is_error(a)) { result = vm__push(vm, a); if (result != VM_OK) return result; break; }
        if (jacl_is_error(b)) { result = vm__push(vm, b); if (result != VM_OK) return result; break; }

        if (!jacl_is_string(a) || !jacl_is_string(b)) {
          vm__set_error(vm,
            "type error in 'concat': expected strings, got %s and %s",
            vm__type_name(a), vm__type_name(b));
          return VM_RUNTIME_ERROR;
        }

        uint32_t len_a = jacl_string_len(a);
        uint32_t len_b = jacl_string_len(b);
        uint32_t total = len_a + len_b;

        JaclVal res;
        if (total <= 7) {
          char buf[8];
          jacl_string_data(a, buf, len_a);
          jacl_string_data(b, buf + len_a, len_b);
          res = jacl_inline_string(buf, total);
        } else {
          char stack_buf[256];
          char* concat_buf = stack_buf;
          if (total > sizeof(stack_buf)) {
            concat_buf = (char*)arena_alloc(vm->arena, total);
          }
          jacl_string_data(a, concat_buf, len_a);
          jacl_string_data(b, concat_buf + len_a, len_b);
          res = jacl_intern(vm->arena, vm->intern_table, concat_buf, total);
        }

        result = vm__push(vm, res); if (result != VM_OK) return result;
        break;
      }

      case OP_STR_LEN: {
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; break; }
        if (!jacl_is_string(val)) {
          vm__set_error(vm, "type error in 'length': expected string, got %s",
                       vm__type_name(val));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, jacl_i32((int32_t)jacl_string_len(val)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_STR_INDEX: {
        JaclVal idx_val, str_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &str_val); if (result != VM_OK) return result;
        if (jacl_is_error(str_val)) { result = vm__push(vm, str_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_string(str_val)) {
          vm__set_error(vm, "type error in 'index': expected string, got %s",
                       vm__type_name(str_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "type error in 'index': expected i32 index, got %s",
                       vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t idx = jacl_as_i32(idx_val);
        uint32_t slen = jacl_string_len(str_val);
        if (idx < 0 || (uint32_t)idx >= slen) {
          result = vm__push(vm, JACL_NIL);
        } else {
          char ch;
          if (jacl_is_heap_string(str_val)) {
            ch = jacl_as_heap_string(str_val)->data[idx];
          } else {
            char buf[8];
            jacl_string_data(str_val, buf, sizeof(buf));
            ch = buf[idx];
          }
          result = vm__push(vm, jacl_inline_string(&ch, 1));
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_STR_SLICE: {
        JaclVal end_val, start_val, str_val;
        result = vm__pop(vm, &end_val);   if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &str_val);   if (result != VM_OK) return result;
        if (jacl_is_error(str_val)) { result = vm__push(vm, str_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(start_val)) { result = vm__push(vm, start_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(end_val)) { result = vm__push(vm, end_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_string(str_val)) {
          vm__set_error(vm, "type error in 'slice': expected string, got %s",
                       vm__type_name(str_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(start_val)) {
          vm__set_error(vm, "type error in 'slice': expected i32 start, got %s",
                       vm__type_name(start_val));
          return VM_RUNTIME_ERROR;
        }
        uint32_t slen = jacl_string_len(str_val);
        int32_t start = jacl_as_i32(start_val);
        int32_t end;
        if (jacl_is_nil(end_val)) {
          end = (int32_t)slen;  /* 2-arg form: slice to end */
        } else if (jacl_is_i32(end_val)) {
          end = jacl_as_i32(end_val);
        } else {
          vm__set_error(vm, "type error in 'slice': expected i32 end, got %s",
                       vm__type_name(end_val));
          return VM_RUNTIME_ERROR;
        }
        /* Clamp bounds */
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if ((uint32_t)start > slen) start = (int32_t)slen;
        if ((uint32_t)end > slen) end = (int32_t)slen;
        if (end < start) end = start;
        uint32_t slice_len = (uint32_t)(end - start);

        JaclVal res;
        if (slice_len == 0) {
          res = jacl_inline_string("", 0);
        } else if (slice_len <= 7) {
          char buf[8];
          /* Get pointer to source data */
          if (jacl_is_heap_string(str_val)) {
            memcpy(buf, jacl_as_heap_string(str_val)->data + start, slice_len);
          } else {
            char src[8];
            jacl_string_data(str_val, src, sizeof(src));
            memcpy(buf, src + start, slice_len);
          }
          res = jacl_inline_string(buf, slice_len);
        } else {
          /* Heap-interned slice */
          const char* src_data;
          char src_buf[8];
          if (jacl_is_heap_string(str_val)) {
            src_data = jacl_as_heap_string(str_val)->data;
          } else {
            jacl_string_data(str_val, src_buf, sizeof(src_buf));
            src_data = src_buf;
          }
          res = jacl_intern(vm->arena, vm->intern_table,
                            src_data + start, slice_len);
        }
        result = vm__push(vm, res); if (result != VM_OK) return result;
        break;
      }

      case OP_TO_STRING: {
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; break; }

        /* Cells are transparent — dereference before converting */
        if (jacl_is_cell(val)) {
          JaclMutableRef* ref = jacl_as_cell(val);
          val = ref->value;
        }

        if (jacl_is_string(val)) {
          /* Already a string — push back unchanged */
          result = vm__push(vm, val);
          if (result != VM_OK) return result;
        } else if (jacl_is_vector(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val)) {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena);
          vm__fmt_value(&fmt, val);
          JaclVal str;
          if (fmt.len <= 7) {
            str = jacl_inline_string(fmt.data, fmt.len);
          } else {
            str = jacl_intern(vm->arena, vm->intern_table, fmt.data, fmt.len);
          }
          result = vm__push(vm, str);
          if (result != VM_OK) return result;
        } else {
          char buf[64];
          int n = 0;

          if (jacl_is_nil(val)) {
            memcpy(buf, "nil", 3);
            n = 3;
          } else if (jacl_is_bool(val)) {
            if (val == JACL_TRUE) {
              memcpy(buf, "true", 4);
              n = 4;
            } else {
              memcpy(buf, "false", 5);
              n = 5;
            }
          } else if (jacl_is_i32(val)) {
            n = snprintf(buf, sizeof(buf), "%d", (int)jacl_as_i32(val));
          } else if (jacl_is_f32(val)) {
            n = snprintf(buf, sizeof(buf), "%g", (double)jacl_as_f32(val));
          } else if (jacl_is_closure(val)) {
            JaclClosure* cl = jacl_as_closure(val);
            if (cl->name) {
              n = snprintf(buf, sizeof(buf), "<proc %s>", cl->name);
            } else {
              memcpy(buf, "<closure>", 9);
              n = 9;
            }
          } else {
            memcpy(buf, "<unknown>", 9);
            n = 9;
          }

          JaclVal str;
          if (n < 0) n = 0;
          uint32_t slen = (uint32_t)n;
          if (slen <= 7) {
            str = jacl_inline_string(buf, slen);
          } else {
            str = jacl_intern(vm->arena, vm->intern_table, buf, slen);
          }
          result = vm__push(vm, str);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_VEC: {
        uint8_t count = vm__read_byte(vm);
        jacl_vec_root* vec = jacl_vec_empty();
        for (uint8_t i = 0; i < count; i++) {
          JaclVal elem = vm->stack[vm->stack_top - count + i];
          jacl_vec_root* new_vec = jacl_vec_push_back(vec, elem);
          jacl_vec_unref(vec);
          vec = new_vec;
        }
        vm->stack_top -= count;
        result = vm__push(vm, jacl_vector_ptr(vec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_VEC_GET: {
        JaclVal idx_val, vec_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_vector(vec_val)) {
          vm__set_error(vm, "type error in 'vec-get': expected vector, got %s",
                       vm__type_name(vec_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "type error in 'vec-get': expected i32 index, got %s",
                       vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0) {
          result = vm__push(vm, JACL_NIL);
        } else {
          jacl_vec_get_result gr = jacl_vec_get(vec, (uint32_t)idx);
          result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_VEC_LEN: {
        JaclVal vec_val;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_vector(vec_val)) {
          vm__set_error(vm, "type error in 'vec-len': expected vector, got %s",
                       vm__type_name(vec_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_vec_count(vec)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_VEC_PUSH: {
        JaclVal elem, vec_val;
        result = vm__pop(vm, &elem); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(elem)) { result = vm__push(vm, elem); if (result != VM_OK) return result; break; }
        if (!jacl_is_vector(vec_val)) {
          vm__set_error(vm, "type error in 'vec-push': expected vector, got %s",
                       vm__type_name(vec_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        jacl_vec_root* new_vec = jacl_vec_push_back(vec, elem);
        result = vm__push(vm, jacl_vector_ptr(new_vec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_VEC_SET: {
        JaclVal elem, idx_val, vec_val;
        result = vm__pop(vm, &elem); if (result != VM_OK) return result;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(idx_val)) { result = vm__push(vm, idx_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(elem)) { result = vm__push(vm, elem); if (result != VM_OK) return result; break; }
        if (!jacl_is_vector(vec_val)) {
          vm__set_error(vm, "type error in 'vec-set': expected vector, got %s",
                       vm__type_name(vec_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "type error in 'vec-set': expected i32 index, got %s",
                       vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        int32_t idx = jacl_as_i32(idx_val);
        if (idx < 0) {
          vm__set_error(vm, "vec-set: negative index %d", (int)idx);
          return VM_RUNTIME_ERROR;
        }
        uint32_t count = jacl_vec_count(vec);
        if ((uint32_t)idx < count) {
          /* In-bounds: replace element at index */
          jacl_vec_root* new_vec = jacl_vec_set(vec, (uint32_t)idx, elem);
          result = vm__push(vm, jacl_vector_ptr(new_vec));
        } else {
          /* Out-of-bounds: grow vector with nil fill, then set element */
          jacl_vec_root* new_vec = vec;
          jacl_vec_ref(new_vec);
          for (uint32_t i = count; i < (uint32_t)idx; i++) {
            jacl_vec_root* tmp = jacl_vec_push_back(new_vec, JACL_NIL);
            jacl_vec_unref(new_vec);
            new_vec = tmp;
          }
          jacl_vec_root* final = jacl_vec_push_back(new_vec, elem);
          jacl_vec_unref(new_vec);
          result = vm__push(vm, jacl_vector_ptr(final));
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_VEC_CONCAT: {
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;
        if (jacl_is_error(a_val)) { result = vm__push(vm, a_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(b_val)) { result = vm__push(vm, b_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_vector(a_val)) {
          vm__set_error(vm, "type error in 'vec-concat': expected vector, got %s",
                       vm__type_name(a_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_vector(b_val)) {
          vm__set_error(vm, "type error in 'vec-concat': expected vector, got %s",
                       vm__type_name(b_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* va = (jacl_vec_root*)jacl_as_ptr(a_val);
        jacl_vec_root* vb = (jacl_vec_root*)jacl_as_ptr(b_val);
        jacl_vec_root* new_vec = jacl_vec_concat(va, vb);
        result = vm__push(vm, jacl_vector_ptr(new_vec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_VEC_SLICE: {
        JaclVal end_val, start_val, vec_val;
        result = vm__pop(vm, &end_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &vec_val); if (result != VM_OK) return result;
        if (jacl_is_error(vec_val)) { result = vm__push(vm, vec_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(start_val)) { result = vm__push(vm, start_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(end_val)) { result = vm__push(vm, end_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_vector(vec_val)) {
          vm__set_error(vm, "type error in 'vec-slice': expected vector, got %s",
                       vm__type_name(vec_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(start_val)) {
          vm__set_error(vm, "type error in 'vec-slice': expected i32 start, got %s",
                       vm__type_name(start_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(end_val)) {
          vm__set_error(vm, "type error in 'vec-slice': expected i32 end, got %s",
                       vm__type_name(end_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        int32_t start = jacl_as_i32(start_val);
        int32_t end = jacl_as_i32(end_val);
        uint32_t count = jacl_vec_count(vec);
        /* Clamp bounds */
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if ((uint32_t)start > count) start = (int32_t)count;
        if ((uint32_t)end > count) end = (int32_t)count;
        if (end <= start) {
          result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
        } else {
          jacl_vec_root* new_vec = jacl_vec_slice(vec, (uint32_t)start, (uint32_t)end);
          if (new_vec == NULL) {
            result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
          } else {
            result = vm__push(vm, jacl_vector_ptr(new_vec));
          }
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP: {
        uint8_t pair_count = vm__read_byte(vm);
        jacl_map_node* map = NULL;
        for (uint8_t i = 0; i < pair_count; i++) {
          JaclVal key   = vm->stack[vm->stack_top - 2 * pair_count + 2 * i];
          JaclVal value = vm->stack[vm->stack_top - 2 * pair_count + 2 * i + 1];
          jacl_map_node* new_map = jacl_map_set(map, key, value);
          jacl_map_unref(map);
          map = new_map;
        }
        vm->stack_top -= 2 * pair_count;
        result = vm__push(vm, jacl_map_ptr(map));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP_GET: {
        JaclVal key_val, map_val;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_map(map_val)) {
          vm__set_error(vm, "type error in 'map-get': expected map, got %s",
                       vm__type_name(map_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        if (jacl_map_has(map, key_val)) {
          result = vm__push(vm, jacl_map_get(map, key_val));
        } else {
          result = vm__push(vm, JACL_NIL);
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP_HAS: {
        JaclVal key_val, map_val;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_map(map_val)) {
          vm__set_error(vm, "type error in 'map-has': expected map, got %s",
                       vm__type_name(map_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        result = vm__push(vm, jacl_bool(jacl_map_has(map, key_val)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP_LEN: {
        JaclVal map_val;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_map(map_val)) {
          vm__set_error(vm, "type error in 'map-len': expected map, got %s",
                       vm__type_name(map_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_map_count(map)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP_SET: {
        JaclVal val, key_val, map_val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; break; }
        if (!jacl_is_map(map_val)) {
          vm__set_error(vm, "type error in 'map-set': expected map, got %s",
                       vm__type_name(map_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        jacl_map_node* new_map = jacl_map_set(map, key_val, val);
        result = vm__push(vm, jacl_map_ptr(new_map));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP_REMOVE: {
        JaclVal key_val, map_val;
        result = vm__pop(vm, &key_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(key_val)) { result = vm__push(vm, key_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_map(map_val)) {
          vm__set_error(vm, "type error in 'map-remove': expected map, got %s",
                       vm__type_name(map_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        jacl_map_node* new_map = jacl_map_unset(map, key_val);
        result = vm__push(vm, jacl_map_ptr(new_map));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP_KEYS: {
        JaclVal map_val;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_map(map_val)) {
          vm__set_error(vm, "type error in 'map-keys': expected map, got %s",
                       vm__type_name(map_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        jacl_vec_root* vec = jacl_vec_empty();
        jacl_map_iter it = jacl_map_iter_init(map);
        jacl_map_iter_result ir;
        for (;;) {
          ir = jacl_map_next_leaf(&it);
          if (ir.done) break;
          JaclVal key = jacl_map_key_from_leaf(ir.item);
          jacl_vec_root* new_vec = jacl_vec_push_back(vec, key);
          jacl_vec_unref(vec);
          vec = new_vec;
        }
        result = vm__push(vm, jacl_vector_ptr(vec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MAP_VALS: {
        JaclVal map_val;
        result = vm__pop(vm, &map_val); if (result != VM_OK) return result;
        if (jacl_is_error(map_val)) { result = vm__push(vm, map_val); if (result != VM_OK) return result; break; }
        if (!jacl_is_map(map_val)) {
          vm__set_error(vm, "type error in 'map-vals': expected map, got %s",
                       vm__type_name(map_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(map_val);
        jacl_vec_root* vec = jacl_vec_empty();
        jacl_map_iter it = jacl_map_iter_init(map);
        jacl_map_iter_result ir;
        for (;;) {
          ir = jacl_map_next_leaf(&it);
          if (ir.done) break;
          JaclVal val = jacl_map_value_from_leaf(ir.item);
          jacl_vec_root* new_vec = jacl_vec_push_back(vec, val);
          jacl_vec_unref(vec);
          vec = new_vec;
        }
        result = vm__push(vm, jacl_vector_ptr(vec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_EACH: {
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; break; }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm,
            "type error in 'each': expected closure as second argument, got %s",
            vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(closure_val);

        if (jacl_is_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "each on vector requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_vec_count(vec);

          for (uint32_t i = 0; i < count; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);

            /* Push closure as callee slot + argument */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, gr.value);
            if (result != VM_OK) return result;

            /* Set up call frame */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Discard return value */
            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else if (jacl_is_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm,
              "each on map requires a proc with 2 parameters, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(coll_val);
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;

          for (;;) {
            ir = jacl_map_next_leaf(&it);
            if (ir.done) break;

            JaclVal key = jacl_map_key_from_leaf(ir.item);
            JaclVal value = jacl_map_value_from_leaf(ir.item);

            /* Push closure as callee slot + key + value */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, key);
            if (result != VM_OK) return result;
            result = vm__push(vm, value);
            if (result != VM_OK) return result;

            /* Set up call frame */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 2;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Discard return value */
            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else {
          vm__set_error(vm,
            "type error in 'each': expected vector or map, got %s",
            vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }

        /* each returns nil */
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_TRANSFORM: {
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; break; }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm,
            "type error in 'transform': expected closure as second argument, got %s",
            vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(closure_val);

        if (jacl_is_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "transform on vector requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_vec_count(vec);
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (uint32_t i = 0; i < count; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);

            /* Push closure as callee slot + argument */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, gr.value);
            if (result != VM_OK) return result;

            /* Set up call frame */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            jacl_vec_root* new_result = jacl_vec_push_back(result_vec, ret);
            jacl_vec_unref(result_vec);
            result_vec = new_result;

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else if (jacl_is_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm,
              "transform on map requires a proc with 2 parameters, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(coll_val);
          jacl_map_node* result_map = NULL;
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;

          for (;;) {
            ir = jacl_map_next_leaf(&it);
            if (ir.done) break;

            JaclVal key = jacl_map_key_from_leaf(ir.item);
            JaclVal value = jacl_map_value_from_leaf(ir.item);

            /* Push closure as callee slot + key + value */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, key);
            if (result != VM_OK) return result;
            result = vm__push(vm, value);
            if (result != VM_OK) return result;

            /* Set up call frame */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 2;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value — must be a 2-element vector [key new-value] */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!jacl_is_vector(ret)) {
              vm__set_error(vm,
                "transform on map: proc must return a 2-element vector, got %s",
                vm__type_name(ret));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root* pair = (jacl_vec_root*)jacl_as_ptr(ret);
            if (jacl_vec_count(pair) != 2) {
              vm__set_error(vm,
                "transform on map: proc must return a 2-element vector, got %d elements",
                (int)jacl_vec_count(pair));
              return VM_RUNTIME_ERROR;
            }
            JaclVal new_key = jacl_vec_get(pair, 0).value;
            JaclVal new_val = jacl_vec_get(pair, 1).value;
            jacl_map_node* new_map = jacl_map_set(result_map, new_key, new_val);
            jacl_map_unref(result_map);
            result_map = new_map;

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_map_ptr(result_map));
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm,
            "type error in 'transform': expected vector or map as first argument, got %s",
            vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        break;
      }

      case OP_FILTER: {
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; break; }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm,
            "type error in 'filter': expected closure as second argument, got %s",
            vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(closure_val);

        if (jacl_is_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "filter on vector requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_vec_count(vec);
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (uint32_t i = 0; i < count; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);

            /* Push closure as callee slot + argument */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, gr.value);
            if (result != VM_OK) return result;

            /* Set up call frame */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value — keep element if truthy */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              jacl_vec_root* new_result = jacl_vec_push_back(result_vec, gr.value);
              jacl_vec_unref(result_vec);
              result_vec = new_result;
            }

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else if (jacl_is_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm,
              "filter on map requires a proc with 2 parameters, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(coll_val);
          jacl_map_node* result_map = NULL;
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;

          for (;;) {
            ir = jacl_map_next_leaf(&it);
            if (ir.done) break;

            JaclVal key = jacl_map_key_from_leaf(ir.item);
            JaclVal value = jacl_map_value_from_leaf(ir.item);

            /* Push closure as callee slot + key + value */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, key);
            if (result != VM_OK) return result;
            result = vm__push(vm, value);
            if (result != VM_OK) return result;

            /* Set up call frame */
            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_frame_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 2;
            cf->chunk      = &closure->chunk;

            /* Switch to closure code and execute */
            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_frame_count);
            if (call_result != VM_OK) return call_result;

            /* Collect return value — keep entry if truthy */
            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              jacl_map_node* new_map = jacl_map_set(result_map, key, value);
              jacl_map_unref(result_map);
              result_map = new_map;
            }

            /* Restore state for next iteration */
            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }

          result = vm__push(vm, jacl_map_ptr(result_map));
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm,
            "type error in 'filter': expected vector or map, got %s",
            vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        break;
      }

      case OP_ERROR: {
        /* Peek top-of-stack, set error flag, leave on stack */
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        vm->stack[vm->stack_top - 1] = jacl_set_error(vm->stack[vm->stack_top - 1]);
        vm__capture_trace(vm);
        break;
      }

      case OP_IS_ERROR: {
        /* Pop value, push true if error-flagged, else false */
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_error(val)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_ERROR_VAL: {
        /* Peek top-of-stack, clear error flag, leave on stack */
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        vm->stack[vm->stack_top - 1] = jacl_clear_error(vm->stack[vm->stack_top - 1]);
        break;
      }

      case OP_CHECK_ERROR: {
        uint16_t offset = vm__read_u16(vm);
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        JaclVal top = vm->stack[vm->stack_top - 1];
        if (jacl_is_error(top)) {
          if (offset == 0) {
            /* Return from current frame with the error value */
            JaclVal return_value;
            result = vm__pop(vm, &return_value);
            if (result != VM_OK) return result;

            uint32_t callee_base = frame->stack_base;
            uint8_t* caller_ip   = frame->return_ip;

            vm->frame_count--;

            if (vm->frame_count == 0) {
              /* Returning from top-level */
              vm->stack[0] = return_value;
              vm->stack_top = 1;
              return VM_OK;
            }

            /* Place return value where the callee's closure was */
            vm->stack[callee_base - 1] = return_value;
            vm->stack_top = callee_base;

            frame     = &vm->frames[vm->frame_count - 1];
            vm->ip    = caller_ip;
            vm->chunk = frame->chunk;

            if (vm->frame_count <= min_frame) {
              return VM_OK;
            }
          } else {
            /* Jump to handler (for try, US-004) */
            vm->ip += offset;
          }
        } else {
          /* Not an error: pop and continue */
          vm->stack_top--;
        }
        break;
      }

      case OP_JUMP_IF_ERROR: {
        uint16_t offset = vm__read_u16(vm);
        if (vm->stack_top == 0) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        JaclVal top = vm->stack[vm->stack_top - 1];
        if (jacl_is_error(top)) {
          vm->ip += offset;
        }
        break;
      }

      case OP_STACK_TRACE: {
        if (vm->stack_trace.count == 0) {
          /* No error has been created yet — push empty string */
          result = vm__push(vm, jacl_inline_string("", 0));
          if (result != VM_OK) return result;
        } else {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena);
          for (uint32_t i = 0; i < vm->stack_trace.count; i++) {
            StackTraceEntry* e = &vm->stack_trace.entries[i];
            char tmp[128];
            const char* name = e->function_name ? e->function_name : "<main>";
            int n = snprintf(tmp, sizeof(tmp), "  at %s (line %u)",
                             name, (unsigned)e->line_number);
            vm__fmt_append(&fmt, tmp, (uint32_t)n);
            if (i + 1 < vm->stack_trace.count) {
              vm__fmt_append(&fmt, "\n", 1);
            }
          }
          JaclVal trace_str;
          if (fmt.len <= 7) {
            trace_str = jacl_inline_string(fmt.data, fmt.len);
          } else {
            trace_str = jacl_intern(vm->arena, vm->intern_table, fmt.data, fmt.len);
          }
          result = vm__push(vm, trace_str);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_MAKE_CELL: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        JaclMutableRef* ref = (JaclMutableRef*)arena_alloc(vm->arena, sizeof(JaclMutableRef));
        ref->value = value;
        result = vm__push(vm, jacl_cell_ptr(ref));
        if (result != VM_OK) return result;
        break;
      }

      case OP_GET_CELL_LOCAL: {
        uint8_t slot = vm__read_byte(vm);
        JaclVal cell = vm->stack[frame->stack_base + slot];
        JaclMutableRef* ref = jacl_as_cell(cell);
        result = vm__push(vm, ref->value);
        if (result != VM_OK) return result;
        break;
      }

      case OP_SET_CELL_LOCAL: {
        uint8_t slot = vm__read_byte(vm);
        JaclVal new_value;
        result = vm__pop(vm, &new_value); if (result != VM_OK) return result;
        JaclVal cell = vm->stack[frame->stack_base + slot];
        JaclMutableRef* ref = jacl_as_cell(cell);
        ref->value = new_value;
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GET_CELL_UPVALUE: {
        uint8_t index = vm__read_byte(vm);
        JaclVal cell = frame->closure->upvalues[index];
        JaclMutableRef* ref = jacl_as_cell(cell);
        result = vm__push(vm, ref->value);
        if (result != VM_OK) return result;
        break;
      }

      case OP_SET_CELL_UPVALUE: {
        uint8_t index = vm__read_byte(vm);
        JaclVal new_value;
        result = vm__pop(vm, &new_value); if (result != VM_OK) return result;
        JaclVal cell = frame->closure->upvalues[index];
        JaclMutableRef* ref = jacl_as_cell(cell);
        ref->value = new_value;
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_SET_GLOBAL: {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal name = frame->chunk->constants[name_idx];
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        vm__env_set(vm, name, value);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_BOX: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        if (jacl_is_error(value)) {
          result = vm__push(vm, value); if (result != VM_OK) return result;
          break;
        }
        JaclMutableRef* ref = (JaclMutableRef*)arena_alloc(vm->arena, sizeof(JaclMutableRef));
        ref->value = value;
        result = vm__push(vm, jacl_box_ptr(ref));
        if (result != VM_OK) return result;
        break;
      }

      case OP_ATOM: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        if (jacl_is_error(value)) {
          result = vm__push(vm, value); if (result != VM_OK) return result;
          break;
        }
        JaclMutableRef* ref = (JaclMutableRef*)arena_alloc(vm->arena, sizeof(JaclMutableRef));
        ref->value = value;
        result = vm__push(vm, jacl_atom_ptr(ref));
        if (result != VM_OK) return result;
        break;
      }

      case OP_IS_BOX: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_box(value)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_IS_ATOM: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_atom(value)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_DEREF: {
        JaclVal container;
        result = vm__pop(vm, &container); if (result != VM_OK) return result;
        if (jacl_is_error(container)) {
          result = vm__push(vm, container); if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_box(container) && !jacl_is_atom(container)) {
          vm__set_error(vm, "deref: expected box or atom, got %s",
                       vm__type_name(container));
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(container);
        result = vm__push(vm, ref->value);
        if (result != VM_OK) return result;
        break;
      }

      case OP_RESET: {
        JaclVal new_val, container;
        result = vm__pop(vm, &new_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &container); if (result != VM_OK) return result;
        if (jacl_is_error(container)) {
          result = vm__push(vm, container); if (result != VM_OK) return result;
          break;
        }
        if (jacl_is_error(new_val)) {
          result = vm__push(vm, new_val); if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_box(container) && !jacl_is_atom(container)) {
          vm__set_error(vm, "reset!: expected box or atom, got %s",
                       vm__type_name(container));
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(container);
        ref->value = new_val;
        result = vm__push(vm, new_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_SWAP: {
        JaclVal closure_val, container;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &container); if (result != VM_OK) return result;
        if (jacl_is_error(container)) {
          result = vm__push(vm, container); if (result != VM_OK) return result;
          break;
        }
        if (jacl_is_error(closure_val)) {
          result = vm__push(vm, closure_val); if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_box(container) && !jacl_is_atom(container)) {
          vm__set_error(vm, "swap!: expected box or atom, got %s",
                       vm__type_name(container));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "swap!: expected closure as second argument, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(container);
        JaclClosure* closure = jacl_as_closure(closure_val);

        if (closure->param_count != 1) {
          vm__set_error(vm,
            "swap!: closure must take 1 parameter, got %d",
            (int)closure->param_count);
          return VM_RUNTIME_ERROR;
        }

        /* Push closure as callee slot + current value as argument */
        result = vm__push(vm, closure_val);
        if (result != VM_OK) return result;
        result = vm__push(vm, ref->value);
        if (result != VM_OK) return result;

        /* Set up call frame */
        if (vm->frame_count >= VM_FRAMES_MAX) {
          vm__set_error(vm, "stack overflow");
          return VM_RUNTIME_ERROR;
        }
        uint32_t caller_frame_count = vm->frame_count;
        CallFrame* cf = &vm->frames[vm->frame_count++];
        cf->closure    = closure;
        cf->return_ip  = vm->ip;
        cf->stack_base = vm->stack_top - 1;
        cf->chunk      = &closure->chunk;

        /* Switch to closure code and execute */
        uint8_t* saved_ip = vm->ip;
        BytecodeChunk* saved_chunk = vm->chunk;
        vm->ip    = closure->chunk.code;
        vm->chunk = &closure->chunk;

        VMResult call_result = vm__run(vm, caller_frame_count);
        if (call_result != VM_OK) return call_result;

        /* Pop return value, store in ref, push as result */
        JaclVal swap_result;
        result = vm__pop(vm, &swap_result);
        if (result != VM_OK) return result;

        ref->value = swap_result;

        /* Restore state */
        vm->ip    = saved_ip;
        vm->chunk = saved_chunk;
        frame = &vm->frames[vm->frame_count - 1];

        result = vm__push(vm, swap_result);
        if (result != VM_OK) return result;
        break;
      }

      case OP_HALT: {
        return VM_OK;
      }

      default: {
        vm__set_error(vm, "unknown opcode %d", (int)instruction);
        return VM_RUNTIME_ERROR;
      }
    }
  }
}

#undef VM__BINARY_NUMERIC_OP

/* --- Pipeline convenience: jacl_run --- */

/**
 * Source-to-execution pipeline.
 * Chains: lexer_lex -> parser_parse -> compiler_compile -> vm_exec.
 * Returns VM_RUNTIME_ERROR on parse or compile errors (message in vm->error_message).
 */
static VMResult jacl_run(const char* source, VM* vm, arena_t* arena) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  if (parse.error_count > 0) {
    vm->error_message = "parse error";
    return VM_RUNTIME_ERROR;
  }

  JaclInternTable intern_table;
  intern_table_init(&intern_table, arena);

  CompileResult cr = compiler_compile(parse, arena, &intern_table);
  if (cr.error_count > 0) {
    vm->error_message = cr.error_message ? cr.error_message : "compile error";
    return VM_RUNTIME_ERROR;
  }

  vm->intern_table = &intern_table;
  return vm_exec(vm, &cr.chunk);
}

#endif /* VM_C */
