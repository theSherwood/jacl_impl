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
  BlockPool      block_pool;    /* GC block pool (owned by VM) */
  ThreadHeap     heap;          /* GC heap for runtime allocations */
  JaclInternTable* intern_table;  /* shared intern table for concat/interning */
  BytecodeChunk* top_chunk;       /* top-level chunk for GC root scanning */
  GreyBuffer*    grey_buf;       /* write barrier target (NULL in single-threaded) */
  volatile uint32_t *gc_active_ptr; /* pointer to runtime's gc_active (NULL in single-threaded) */
  void*          runtime;        /* Runtime pointer for concurrent GC trigger (NULL in single-threaded) */
  int            worker_id;      /* Worker thread ID for task pinning (-1 if not on a worker) */
  const char*    error_message;  /* last error message, or NULL */
  uint32_t       error_line;     /* source line of last error */
  StackTrace     stack_trace;    /* most recent error's trace */
} VM;

/* --- API --- */

static void     vm_init(VM* vm, arena_t* arena);
static void     vm_destroy(VM* vm);
static VMResult vm_exec(VM* vm, BytecodeChunk* chunk);

/* --- Pipeline convenience --- */

static VMResult jacl_run(const char* source, VM* vm, arena_t* arena);

/* --- GC collect (defined in gc_collect.c, after vm.c in unity build) --- */

static void gc_collect(ThreadHeap *heap, VM *vm);

/* --- Emergency GC callback for single-threaded mode --- */

static void vm__emergency_gc_single(void *ctx) {
    VM *vm = (VM *)ctx;
    gc_collect(&vm->heap, vm);
}

/* --- Concurrent GC trigger (defined in runtime.c, after gc_collect.c) --- */

static void gc_concurrent_trigger(void *runtime_ptr);

/* --- Runtime helpers (defined in runtime.c, after gc_collect.c) --- */

static JaclVal runtime__create_resolve_closure(ThreadHeap *heap, arena_t *arena,
                                                JaclVal future_val);
static void runtime__submit_spawn_task(void *runtime_ptr, JaclClosure *closure,
                                        JaclVal future_val, bool is_cps);
static void runtime__schedule_continuation(void *runtime_ptr,
                                            JaclClosure *continuation,
                                            JaclVal result);
static void runtime__schedule_waiters(void *runtime_ptr,
                                       FutureWaiter *waiters,
                                       JaclVal result);
static void runtime__submit_parallel_task(void *runtime_ptr,
                                           JaclClosure *closure,
                                           JaclVal agg_val,
                                           uint32_t index,
                                           bool is_cps);
static void runtime__submit_race_task(void *runtime_ptr,
                                       JaclClosure *closure,
                                       JaclVal agg_val,
                                       bool is_cps);
static void runtime__complete_parallel_slot(void *runtime_ptr,
                                             VM *vm,
                                             JaclVal agg_val,
                                             uint32_t index,
                                             JaclVal result);
static void runtime__complete_race_slot(void *runtime_ptr,
                                         VM *vm,
                                         JaclVal agg_val,
                                         JaclVal result);

/* --- Type name helper for error messages --- */

static const char* vm__type_name(JaclVal v) {
  if (jacl_is_nil(v))           return "nil";
  if (jacl_is_bool(v))          return "bool";
  if (jacl_is_i32(v))           return "i32";
  if (jacl_is_u32(v))           return "u32";
  if (jacl_is_i64(v))           return "i64";
  if (jacl_is_u64(v))           return "u64";
  if (jacl_is_f32(v))           return "f32";
  if (jacl_is_f64(v))           return "f64";
  if (jacl_is_string(v))        return "string";
  if (jacl_is_closure(v))       return "closure";
  if (jacl_is_vector(v))        return "vector";
  if (jacl_is_map(v))           return "map";
  if (jacl_is_future(v))        return "future";
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
      if (child->return_ip == NULL || f->chunk == NULL) {
        entry->line_number = 0;
      } else {
        uint32_t offset = (uint32_t)(child->return_ip - f->chunk->code);
        if (offset > 0) offset--;
        if (offset < f->chunk->code_count) {
          entry->line_number = f->chunk->lines[offset];
        } else {
          entry->line_number = 0;
        }
      }
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
  vm->top_chunk     = NULL;
  vm->grey_buf      = NULL;
  vm->gc_active_ptr = NULL;
  vm->runtime       = NULL;
  vm->worker_id     = -1;
  vm->frame_count   = 0;
  vm->error_message = NULL;
  vm->error_line    = 0;
  vm->stack_trace.count = 0;

  /* Initialize GC heap and make it available for collection templates */
  gc_block_pool_init(&vm->block_pool);
  gc_heap_init(&vm->heap, &vm->block_pool);
  gc__current_heap = &vm->heap;

  /* Set emergency GC callback for single-threaded OOM escalation */
  gc__emergency_gc_fn  = vm__emergency_gc_single;
  gc__emergency_gc_ctx = vm;

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

/**
 * Destroy the VM's GC heap and block pool.
 * Call before arena_destroy().
 */
static void vm_destroy(VM* vm) {
  gc_heap_destroy(&vm->heap);
  gc_block_pool_destroy(&vm->block_pool);
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

#define VM__BINARY_NUMERIC_OP(fn_i32, fn_f32, fn_u32, op_name)               \
  do {                                                                        \
    JaclVal b, a;                                                             \
    result = vm__pop(vm, &b); if (result != VM_OK) return result;             \
    result = vm__pop(vm, &a); if (result != VM_OK) return result;             \
    JaclVal res;                                                              \
    if (jacl_is_i32(a) && jacl_is_i32(b)) {                                  \
      res = fn_i32(a, b);                                                     \
    } else if (jacl_is_f32(a) && jacl_is_f32(b)) {                           \
      res = fn_f32(a, b);                                                     \
    } else if (jacl_is_u32(a) && jacl_is_u32(b)) {                           \
      res = fn_u32(a, b);                                                     \
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
  } else if (jacl_is_u32(val)) {
    n = snprintf(tmp, sizeof(tmp), "%u", (unsigned)jacl_as_u32(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_f32(val)) {
    n = snprintf(tmp, sizeof(tmp), "%g", (double)jacl_as_f32(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_i64(val)) {
    n = snprintf(tmp, sizeof(tmp), "%" PRIi64, jacl_as_i64(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_u64(val)) {
    n = snprintf(tmp, sizeof(tmp), "%" PRIu64, jacl_as_u64(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_f64(val)) {
    n = snprintf(tmp, sizeof(tmp), "%g", jacl_as_f64(val));
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
  } else if (jacl_is_future(val)) {
    JaclFuture* fut = jacl_as_future(val);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);
    if (state == FUTURE_PENDING) {
      vm__fmt_append(buf, "<future: pending>", 17);
    } else if (state == FUTURE_RESOLVED) {
      vm__fmt_append(buf, "<future: resolved ", 18);
      vm__fmt_value(buf, (JaclVal)fut->result);
      vm__fmt_append(buf, ">", 1);
    } else {
      vm__fmt_append(buf, "<future: error ", 15);
      vm__fmt_value(buf, (JaclVal)fut->result);
      vm__fmt_append(buf, ">", 1);
    }
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

  vm->chunk     = chunk;
  vm->top_chunk = chunk;
  vm->ip        = chunk->code;

  return vm__run(vm, 0);
}

/**
 * Inner dispatch loop. Runs until OP_HALT or until frame_count drops
 * to min_frame (used by OP_EACH to execute closures inline).
 */
static VMResult vm__run(VM* vm, uint32_t min_frame) {
  CallFrame* frame = &vm->frames[vm->frame_count - 1];

  for (;;) {
    /* GC safepoint: collect if threshold exceeded */
    if (vm->heap.needs_gc) {
      if (!vm->runtime) {
        gc_collect(&vm->heap, vm);
      } else {
        vm->heap.needs_gc = false;
        vm->heap.bytes_since_gc = 0;
        gc_concurrent_trigger(vm->runtime);
      }
    }

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
        VM__BINARY_NUMERIC_OP(jacl_add_i32, jacl_add_f32, jacl_u32_add, "+");
        break;
      }

      case OP_SUB: {
        VM__BINARY_NUMERIC_OP(jacl_sub_i32, jacl_sub_f32, jacl_u32_sub, "-");
        break;
      }

      case OP_MUL: {
        VM__BINARY_NUMERIC_OP(jacl_mul_i32, jacl_mul_f32, jacl_u32_mul, "*");
        break;
      }

      case OP_DIV: {
        VM__BINARY_NUMERIC_OP(jacl_div_i32, jacl_div_f32, jacl_u32_div, "/");
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
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          JaclVal mod_res = jacl_u32_mod(a, b);
          if (jacl_is_error(mod_res) && !jacl_is_error(a) && !jacl_is_error(b))
            vm__capture_trace(vm);
          result = vm__push(vm, mod_res);
          if (result != VM_OK) return result;
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
        } else if (jacl_is_u32(a)) {
          res = jacl_u32_neg(a);
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
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_lt(a, b);
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
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_gt(a, b);
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
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_le(a, b);
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
        } else if (jacl_is_u32(a) && jacl_is_u32(b)) {
          res = jacl_u32_ge(a, b);
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
        } else if (jacl_is_u32(val)) {
          int n = snprintf(buf, sizeof(buf), "%u\n", (unsigned)jacl_as_u32(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_f32(val)) {
          int n = snprintf(buf, sizeof(buf), "%g\n", (double)jacl_as_f32(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_i64(val)) {
          int n = snprintf(buf, sizeof(buf), "%" PRIi64 "\n", jacl_as_i64(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_u64(val)) {
          int n = snprintf(buf, sizeof(buf), "%" PRIu64 "\n", jacl_as_u64(val));
          text = buf;
          len = (uint32_t)n;
        } else if (jacl_is_f64(val)) {
          int n = snprintf(buf, sizeof(buf), "%g\n", jacl_as_f64(val));
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
        } else if (jacl_is_vector(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val) || jacl_is_future(val)) {
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

        /* Allocate closure + inline upvalue array on GC heap */
        size_t uv_bytes = sizeof(JaclVal) * template->upvalue_count;
        JaclClosure* cl = (JaclClosure*)gc_alloc(&vm->heap, OBJ_CLOSURE,
                              sizeof(JaclClosure) + uv_bytes);
        cl->chunk        = template->chunk;
        cl->param_count  = template->param_count;
        cl->param_names  = template->param_names;
        cl->name         = template->name;
        cl->upvalue_count = template->upvalue_count;
        cl->min_args     = template->min_args;
        cl->variadic     = template->variadic;
        cl->pinned       = template->pinned;
        /* All pinned closures run on thread 0 (the main worker thread).
           This ensures all non-local mutable state reads and writes go
           through a single worker, avoiding per-worker env isolation issues. */
        cl->pin_worker_id = template->pinned ? 0 : -1;

        if (cl->upvalue_count > 0) {
          cl->upvalues = (JaclVal*)(cl + 1); /* trailing array */
          for (uint8_t i = 0; i < cl->upvalue_count; i++) {
            uint8_t is_local = vm__read_byte(vm);
            uint8_t uv_index = vm__read_byte(vm);
            if (is_local) {
              if (frame->stack_base + uv_index >= vm->stack_top) {
                vm__set_error(vm,
                  "OP_CLOSURE: local upvalue index %d out of bounds "
                  "(frame stack_base=%u, stack_top=%u)",
                  uv_index, frame->stack_base, vm->stack_top);
                return VM_RUNTIME_ERROR;
              }
              cl->upvalues[i] = vm->stack[frame->stack_base + uv_index];
            } else {
              if (uv_index >= frame->closure->upvalue_count) {
                vm__set_error(vm,
                  "OP_CLOSURE: upvalue index %d out of bounds "
                  "(parent has %d upvalues)",
                  uv_index, frame->closure->upvalue_count);
                return VM_RUNTIME_ERROR;
              }
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
          res = jacl_intern(&vm->heap, vm->intern_table, concat_buf, total);
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
          res = jacl_intern(&vm->heap, vm->intern_table,
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
        } else if (jacl_is_vector(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val) || jacl_is_future(val)) {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena);
          vm__fmt_value(&fmt, val);
          JaclVal str;
          if (fmt.len <= 7) {
            str = jacl_inline_string(fmt.data, fmt.len);
          } else {
            str = jacl_intern(&vm->heap, vm->intern_table, fmt.data, fmt.len);
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
          } else if (jacl_is_u32(val)) {
            n = snprintf(buf, sizeof(buf), "%u", (unsigned)jacl_as_u32(val));
          } else if (jacl_is_f32(val)) {
            n = snprintf(buf, sizeof(buf), "%g", (double)jacl_as_f32(val));
          } else if (jacl_is_i64(val)) {
            n = snprintf(buf, sizeof(buf), "%" PRIi64, jacl_as_i64(val));
          } else if (jacl_is_u64(val)) {
            n = snprintf(buf, sizeof(buf), "%" PRIu64, jacl_as_u64(val));
          } else if (jacl_is_f64(val)) {
            n = snprintf(buf, sizeof(buf), "%g", jacl_as_f64(val));
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
            str = jacl_intern(&vm->heap, vm->intern_table, buf, slen);
          }
          result = vm__push(vm, str);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_VEC: {
        uint8_t count = vm__read_byte(vm);
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        for (uint8_t i = 0; i < count; i++) {
          JaclVal elem = vm->stack[vm->stack_top - count + i];
          vec = jacl_vec_push_back(vec, elem);
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
        gc__current_heap = &vm->heap;
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
        gc__current_heap = &vm->heap;
        if ((uint32_t)idx < count) {
          /* In-bounds: replace element at index */
          jacl_vec_root* new_vec = jacl_vec_set(vec, (uint32_t)idx, elem);
          result = vm__push(vm, jacl_vector_ptr(new_vec));
        } else {
          /* Out-of-bounds: grow vector with nil fill, then set element */
          jacl_vec_root* new_vec = vec;
          for (uint32_t i = count; i < (uint32_t)idx; i++) {
            new_vec = jacl_vec_push_back(new_vec, JACL_NIL);
          }
          jacl_vec_root* final = jacl_vec_push_back(new_vec, elem);
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
        gc__current_heap = &vm->heap;
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
        gc__current_heap = &vm->heap;
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
        gc__current_heap = &vm->heap;
        for (uint8_t i = 0; i < pair_count; i++) {
          JaclVal key   = vm->stack[vm->stack_top - 2 * pair_count + 2 * i];
          JaclVal value = vm->stack[vm->stack_top - 2 * pair_count + 2 * i + 1];
          jacl_map_node* new_map = jacl_map_set(map, key, value);
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
        gc__current_heap = &vm->heap;
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
        gc__current_heap = &vm->heap;
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
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        jacl_map_iter it = jacl_map_iter_init(map);
        jacl_map_iter_result ir;
        for (;;) {
          ir = jacl_map_next_leaf(&it);
          if (ir.done) break;
          JaclVal key = jacl_map_key_from_leaf(ir.item);
          vec = jacl_vec_push_back(vec, key);
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
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        jacl_map_iter it = jacl_map_iter_init(map);
        jacl_map_iter_result ir;
        for (;;) {
          ir = jacl_map_next_leaf(&it);
          if (ir.done) break;
          JaclVal val = jacl_map_value_from_leaf(ir.item);
          vec = jacl_vec_push_back(vec, val);
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
          gc__current_heap = &vm->heap;
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

            gc__current_heap = &vm->heap;

            result_vec = jacl_vec_push_back(result_vec, ret);

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
          gc__current_heap = &vm->heap;
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
          gc__current_heap = &vm->heap;
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
              gc__current_heap = &vm->heap;
              result_vec = jacl_vec_push_back(result_vec, gr.value);
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
          gc__current_heap = &vm->heap;
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
            trace_str = jacl_intern(&vm->heap, vm->intern_table, fmt.data, fmt.len);
          }
          result = vm__push(vm, trace_str);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_MAKE_CELL: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
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
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                         ref->value, new_value);
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
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                         ref->value, new_value);
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
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
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
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef));
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

      case OP_IS_FUTURE: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        result = vm__push(vm, jacl_bool(jacl_is_future(value)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_AWAIT: {
        /* CPS await: pop continuation closure, pop future.
           Runtime mode: register waiter or schedule continuation, return.
           Single-threaded: call continuation inline (future must be resolved). */
        JaclVal continuation;
        result = vm__pop(vm, &continuation); if (result != VM_OK) return result;
        JaclVal future_val;
        result = vm__pop(vm, &future_val); if (result != VM_OK) return result;

        if (!jacl_is_closure(continuation)) {
          vm__set_error(vm, "OP_AWAIT: continuation is not a closure");
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_future(future_val)) {
          vm__set_error(vm, "await requires a future value, got %s",
                       vm__type_name(future_val));
          return VM_RUNTIME_ERROR;
        }

        JaclFuture *fut = jacl_as_future(future_val);
        JaclClosure *cont_cl = jacl_as_closure(continuation);
        uint32_t state = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);

        if (vm->runtime) {
          /* Runtime mode: suspend by returning from vm__run.
             Schedule or register the continuation depending on future state. */
          if (state == FUTURE_RESOLVED || state == FUTURE_ERROR) {
            /* Already settled — schedule continuation with result as task */
            JaclVal await_result = (JaclVal)fut->result;
            runtime__schedule_continuation(vm->runtime, cont_cl, await_result);
          } else {
            /* PENDING — register continuation as waiter; will be scheduled
               when the future resolves (via runtime__schedule_waiters). */
            bool added = jacl_future_add_waiter(fut, continuation, &vm->heap);
            if (!added) {
              /* Race: future resolved between our check and add_waiter.
                 Schedule continuation immediately. */
              JaclVal await_result = (JaclVal)fut->result;
              runtime__schedule_continuation(vm->runtime, cont_cl, await_result);
            }
          }
          /* Return from vm__run — current CPS segment is done.
             Worker task loop will pick up the next available task. */
          return VM_OK;
        }

        /* Single-threaded mode: call continuation inline.
           Future should be resolved (spawn runs synchronously). */
        {
          JaclVal await_result = JACL_NIL;
          if (state == FUTURE_RESOLVED || state == FUTURE_ERROR) {
            await_result = (JaclVal)fut->result;
          }
          /* If still PENDING in single-threaded mode, pass nil (shouldn't happen
             with well-formed programs since spawn resolves synchronously). */

          result = vm__push(vm, continuation);
          if (result != VM_OK) return result;
          result = vm__push(vm, await_result);
          if (result != VM_OK) return result;

          if (cont_cl->param_count != 1) {
            vm__set_error(vm, "OP_AWAIT: continuation expects %d args, need 1",
                         (int)cont_cl->param_count);
            return VM_RUNTIME_ERROR;
          }
          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_error(vm, "stack overflow");
            return VM_STACK_OVERFLOW;
          }
          CallFrame* new_frame = &vm->frames[vm->frame_count++];
          new_frame->closure    = cont_cl;
          new_frame->return_ip  = vm->ip;
          new_frame->stack_base = vm->stack_top - 1;
          new_frame->chunk      = &cont_cl->chunk;
          frame     = new_frame;
          vm->ip    = frame->chunk->code;
          vm->chunk = frame->chunk;
        }
        break;
      }

      case OP_SPAWN: {
        /* Pop closure, create pending future, execute closure (resolving
           future with result), push future. For CPS closures (param_count=1,
           __k hidden param), provides a resolve continuation as __k. */
        JaclVal closure_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "spawn requires a closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure *cl = jacl_as_closure(closure_val);
        JaclVal f = jacl_future(&vm->heap);
        JaclFuture *fut = jacl_as_future(f);
        bool is_cps = (cl->param_count == 1); /* 0 user args + hidden __k */

        if (vm->runtime) {
          /* Runtime mode: submit task to worker thread pool */
          runtime__submit_spawn_task(vm->runtime, cl, f, is_cps);
          result = vm__push(vm, f);
          if (result != VM_OK) return result;
        } else {
          /* Single-threaded mode: execute closure synchronously via
             recursive vm__run, then resolve the future with the result. */
          uint8_t *saved_ip = vm->ip;
          BytecodeChunk *saved_chunk = vm->chunk;
          uint32_t saved_frame_count = vm->frame_count;

          if (is_cps) {
            /* CPS closure: create resolve_k as __k parameter */
            JaclVal resolve_k = runtime__create_resolve_closure(
                &vm->heap, vm->arena, f);
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, resolve_k);
            if (result != VM_OK) return result;
          } else {
            /* Non-CPS closure: call with zero args */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
          }

          /* Set up frame for the closure */
          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_error(vm, "stack overflow");
            return VM_STACK_OVERFLOW;
          }
          CallFrame *sf = &vm->frames[vm->frame_count++];
          sf->closure    = cl;
          sf->return_ip  = saved_ip;
          sf->stack_base = vm->stack_top - cl->param_count;
          sf->chunk      = &cl->chunk;
          vm->ip    = cl->chunk.code;
          vm->chunk = &cl->chunk;

          /* Run closure synchronously */
          VMResult sub = vm__run(vm, saved_frame_count);

          /* Restore frame pointer */
          frame = &vm->frames[vm->frame_count - 1];
          vm->ip    = saved_ip;
          vm->chunk = saved_chunk;

          if (!is_cps) {
            /* Non-CPS: resolve future with the closure's return value */
            JaclVal spawn_result = JACL_NIL;
            if (sub == VM_OK && vm->stack_top > 0) {
              spawn_result = vm->stack[--vm->stack_top];
            } else if (sub != VM_OK) {
              JaclVal err = jacl_set_error(jacl_inline_string("error", 5));
              jacl_future_error(fut, err, vm->grey_buf, vm->gc_active_ptr);
              result = vm__push(vm, f);
              if (result != VM_OK) return result;
              break;
            }
            jacl_future_resolve(fut, spawn_result,
                                vm->grey_buf, vm->gc_active_ptr);
          } else {
            /* CPS: resolve_k already resolved the future during execution.
               Pop any leftover return value from the CPS chain. */
            if (vm->stack_top > 0) vm->stack_top--;
          }

          /* Push the future as spawn's result */
          result = vm__push(vm, f);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_RESOLVE_FUTURE: {
        /* Pop result value, pop future, resolve the future, push nil.
           Used by the resolve_k closure generated for CPS spawn.
           Schedules any registered waiters as tasks (runtime mode). */
        JaclVal resolve_result;
        result = vm__pop(vm, &resolve_result); if (result != VM_OK) return result;
        JaclVal future_val;
        result = vm__pop(vm, &future_val); if (result != VM_OK) return result;
        if (!jacl_is_future(future_val)) {
          vm__set_error(vm, "OP_RESOLVE_FUTURE: expected future, got %s",
                       vm__type_name(future_val));
          return VM_RUNTIME_ERROR;
        }
        JaclFuture *rfut = jacl_as_future(future_val);
        FutureWaiter *waiters = jacl_future_resolve(rfut, resolve_result,
                            vm->grey_buf, vm->gc_active_ptr);
        if (waiters && vm->runtime) {
          runtime__schedule_waiters(vm->runtime, waiters, resolve_result);
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_COMPLETE_PARALLEL: {
        /* Pop result, index (i32), agg_val. Complete parallel slot:
           store result, handle error, increment counter, maybe schedule join.
           Used by parallel_k closures for CPS parallel bodies. */
        JaclVal par_result;
        result = vm__pop(vm, &par_result); if (result != VM_OK) return result;
        JaclVal idx_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        JaclVal par_agg_val;
        result = vm__pop(vm, &par_agg_val); if (result != VM_OK) return result;

        uint32_t par_index = (uint32_t)(idx_val & JACL_PAYLOAD_MASK);

        if (vm->runtime) {
          runtime__complete_parallel_slot(vm->runtime, vm, par_agg_val,
                                          par_index, par_result);
        }

        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_COMPLETE_RACE: {
        /* Pop result, agg_val. CAS-settle race, winner schedules join.
           Used by race_k closures for CPS race bodies. */
        JaclVal race_result;
        result = vm__pop(vm, &race_result); if (result != VM_OK) return result;
        JaclVal race_agg_val;
        result = vm__pop(vm, &race_agg_val); if (result != VM_OK) return result;

        if (vm->runtime) {
          runtime__complete_race_slot(vm->runtime, vm, race_agg_val,
                                      race_result);
        }

        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_PARALLEL: {
        /* Parallel: read uint8_t N, pop continuation + N closures.
           Fork N tasks, suspend until all complete, continuation receives
           result vector [r0, r1, ..., r_{N-1}] in input order. */
        uint8_t n = vm__read_byte(vm);

        /* Pop continuation (top of stack) */
        JaclVal continuation;
        result = vm__pop(vm, &continuation); if (result != VM_OK) return result;

        /* Pop N closures (in reverse stack order to get original order) */
        JaclVal closures[256];
        for (int i = (int)n - 1; i >= 0; i--) {
          result = vm__pop(vm, &closures[i]);
          if (result != VM_OK) return result;
        }

        /* Validate types */
        if (!jacl_is_closure(continuation) && !jacl_is_nil(continuation)) {
          vm__set_error(vm, "OP_PARALLEL: continuation is not a closure");
          return VM_RUNTIME_ERROR;
        }
        for (uint8_t i = 0; i < n; i++) {
          if (!jacl_is_closure(closures[i])) {
            vm__set_error(vm, "parallel requires closures, arg %d is %s",
                         (int)i, vm__type_name(closures[i]));
            return VM_RUNTIME_ERROR;
          }
        }

        if (vm->runtime) {
          /* Runtime mode: create aggregate, submit N tasks, suspend */
          JaclVal agg_val = jacl_parallel_agg(&vm->heap, n, continuation);

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);
            bool body_cps = (cl->param_count == 1);
            runtime__submit_parallel_task(vm->runtime, cl, agg_val, i, body_cps);
          }

          /* Suspend: return from vm__run, worker picks up next task */
          return VM_OK;
        }

        /* Single-threaded mode: run each closure sequentially */
        {
          JaclVal results[256];
          bool has_error = false;
          JaclVal first_error = JACL_NIL;

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);
            bool body_cps = (cl->param_count == 1);

            uint8_t *saved_ip = vm->ip;
            BytecodeChunk *saved_chunk = vm->chunk;
            uint32_t saved_frame_count = vm->frame_count;
            uint32_t saved_stack_top = vm->stack_top;

            if (body_cps) {
              /* CPS: create future + resolve_k, call closure(resolve_k) */
              JaclVal fut_val = jacl_future(&vm->heap);
              JaclVal resolve_k = runtime__create_resolve_closure(
                  &vm->heap, vm->arena, fut_val);

              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;
              result = vm__push(vm, resolve_k);
              if (result != VM_OK) return result;

              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_error(vm, "stack overflow");
                return VM_STACK_OVERFLOW;
              }
              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top - 1;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              VMResult sub = vm__run(vm, saved_frame_count);

              /* Restore VM state: frame_count and stack may not have been
                 properly unwound if the body errored before OP_RETURN */
              vm->frame_count = saved_frame_count;
              vm->stack_top   = saved_stack_top;
              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              JaclFuture *fut = jacl_as_future(fut_val);
              uint32_t fstate = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED);
              if (fstate == FUTURE_RESOLVED) {
                results[i] = (JaclVal)fut->result;
                if (jacl_is_error(results[i]) && !has_error) {
                  has_error = true; first_error = results[i];
                }
              } else if (fstate == FUTURE_ERROR) {
                results[i] = (JaclVal)fut->result;
                if (!has_error) { has_error = true; first_error = results[i]; }
              } else if (sub != VM_OK) {
                results[i] = jacl_set_error(jacl_inline_string("error", 5));
                if (!has_error) { has_error = true; first_error = results[i]; }
              } else {
                results[i] = JACL_NIL;
              }
            } else {
              /* Non-CPS: call directly */
              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;

              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_error(vm, "stack overflow");
                return VM_STACK_OVERFLOW;
              }
              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              VMResult sub = vm__run(vm, saved_frame_count);

              /* Capture result before restoring stack */
              JaclVal body_result = JACL_NIL;
              bool body_ok = (sub == VM_OK && vm->stack_top > saved_stack_top);
              if (body_ok) {
                body_result = vm->stack[vm->stack_top - 1];
              }

              /* Restore VM state: frame_count and stack may not have been
                 properly unwound if the body errored before OP_RETURN */
              vm->frame_count = saved_frame_count;
              vm->stack_top   = saved_stack_top;
              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (body_ok) {
                results[i] = body_result;
                if (jacl_is_error(results[i]) && !has_error) {
                  has_error = true;
                  first_error = results[i];
                }
              } else {
                results[i] = jacl_set_error(jacl_inline_string("error", 5));
                if (!has_error) { has_error = true; first_error = results[i]; }
              }
            }
          }

          /* Build continuation argument */
          JaclVal cont_arg;
          if (has_error) {
            cont_arg = first_error;
          } else {
            gc__current_heap = &vm->heap;
            jacl_vec_root *vec = jacl_vec_empty();
            for (uint8_t i = 0; i < n; i++) {
              vec = jacl_vec_push_back(vec, results[i]);
            }
            cont_arg = jacl_vector_ptr(vec);
          }

          /* Call continuation(cont_arg) — set up inline frame */
          if (jacl_is_closure(continuation)) {
            JaclClosure *cont_cl = jacl_as_closure(continuation);
            result = vm__push(vm, continuation);
            if (result != VM_OK) return result;
            result = vm__push(vm, cont_arg);
            if (result != VM_OK) return result;

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_STACK_OVERFLOW;
            }
            CallFrame *cf = &vm->frames[vm->frame_count++];
            cf->closure    = cont_cl;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &cont_cl->chunk;
            frame     = cf;
            vm->ip    = frame->chunk->code;
            vm->chunk = frame->chunk;
          } else {
            /* nil continuation — push result directly */
            result = vm__push(vm, cont_arg);
            if (result != VM_OK) return result;
          }
        }
        break;
      }

      case OP_RACE: {
        /* Race: read uint8_t N, pop continuation + N closures.
           Fork N tasks, first to complete wins, continuation receives
           winner's result (or error). */
        uint8_t n = vm__read_byte(vm);

        /* Pop continuation (top of stack) */
        JaclVal continuation;
        result = vm__pop(vm, &continuation); if (result != VM_OK) return result;

        /* Pop N closures (in reverse stack order to get original order) */
        JaclVal closures[256];
        for (int i = (int)n - 1; i >= 0; i--) {
          result = vm__pop(vm, &closures[i]);
          if (result != VM_OK) return result;
        }

        /* Validate types */
        if (!jacl_is_closure(continuation) && !jacl_is_nil(continuation)) {
          vm__set_error(vm, "OP_RACE: continuation is not a closure");
          return VM_RUNTIME_ERROR;
        }
        for (uint8_t i = 0; i < n; i++) {
          if (!jacl_is_closure(closures[i])) {
            vm__set_error(vm, "race requires closures, arg %d is %s",
                         (int)i, vm__type_name(closures[i]));
            return VM_RUNTIME_ERROR;
          }
        }

        if (vm->runtime) {
          /* Runtime mode: create race aggregate, submit N tasks, suspend */
          JaclVal agg_val = jacl_race_agg(&vm->heap, continuation);

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);
            bool body_cps = (cl->param_count == 1);
            runtime__submit_race_task(vm->runtime, cl, agg_val, body_cps);
          }

          /* Suspend: return from vm__run, worker picks up next task */
          return VM_OK;
        }

        /* Single-threaded mode: run each closure sequentially, first result wins */
        {
          JaclVal winner_result = JACL_NIL;
          bool have_winner = false;

          for (uint8_t i = 0; i < n; i++) {
            JaclClosure *cl = jacl_as_closure(closures[i]);
            bool body_cps = (cl->param_count == 1);

            uint8_t *saved_ip = vm->ip;
            BytecodeChunk *saved_chunk = vm->chunk;
            uint32_t saved_frame_count = vm->frame_count;

            if (body_cps) {
              /* CPS: create future + resolve_k, call closure(resolve_k) */
              JaclVal fut_val = jacl_future(&vm->heap);
              JaclVal resolve_k = runtime__create_resolve_closure(
                  &vm->heap, vm->arena, fut_val);

              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;
              result = vm__push(vm, resolve_k);
              if (result != VM_OK) return result;

              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_error(vm, "stack overflow");
                return VM_STACK_OVERFLOW;
              }
              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top - 1;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              vm__run(vm, saved_frame_count);

              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (vm->stack_top > 0) vm->stack_top--;

              JaclFuture *fut = jacl_as_future(fut_val);
              uint32_t fstate = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_RELAXED);
              if (!have_winner) {
                have_winner = true;
                if (fstate == FUTURE_RESOLVED) {
                  winner_result = (JaclVal)fut->result;
                } else if (fstate == FUTURE_ERROR) {
                  winner_result = (JaclVal)fut->result;
                } else {
                  winner_result = JACL_NIL;
                }
              }
            } else {
              /* Non-CPS: call directly */
              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;

              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_error(vm, "stack overflow");
                return VM_STACK_OVERFLOW;
              }
              CallFrame *sf = &vm->frames[vm->frame_count++];
              sf->closure    = cl;
              sf->return_ip  = saved_ip;
              sf->stack_base = vm->stack_top;
              sf->chunk      = &cl->chunk;
              vm->ip    = cl->chunk.code;
              vm->chunk = &cl->chunk;

              VMResult sub = vm__run(vm, saved_frame_count);

              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (!have_winner) {
                have_winner = true;
                if (sub == VM_OK && vm->stack_top > 0) {
                  winner_result = vm->stack[--vm->stack_top];
                } else {
                  winner_result = jacl_set_error(jacl_inline_string("error", 5));
                }
              } else {
                /* Loser: discard result */
                if (sub == VM_OK && vm->stack_top > 0) vm->stack_top--;
              }
            }
          }

          /* Call continuation(winner_result) */
          if (jacl_is_closure(continuation)) {
            JaclClosure *cont_cl = jacl_as_closure(continuation);
            result = vm__push(vm, continuation);
            if (result != VM_OK) return result;
            result = vm__push(vm, winner_result);
            if (result != VM_OK) return result;

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_STACK_OVERFLOW;
            }
            CallFrame *cf = &vm->frames[vm->frame_count++];
            cf->closure    = cont_cl;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &cont_cl->chunk;
            frame     = cf;
            vm->ip    = frame->chunk->code;
            vm->chunk = frame->chunk;
          } else {
            /* nil continuation — push result directly */
            result = vm__push(vm, winner_result);
            if (result != VM_OK) return result;
          }
        }
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
        JaclVal deref_val;
        if (jacl_is_atom(container)) {
          deref_val = ATOMIC_LOAD_EXPLICIT(&ref->value, MEM_ACQUIRE);
        } else {
          deref_val = ref->value;
        }
        result = vm__push(vm, deref_val);
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
        if (jacl_is_atom(container)) {
          JaclVal reset_old = ATOMIC_LOAD_EXPLICIT(&ref->value, MEM_ACQUIRE);
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           reset_old, new_val);
          ATOMIC_STORE_EXPLICIT(&ref->value, new_val, MEM_RELEASE);
        } else {
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           ref->value, new_val);
          ref->value = new_val;
        }
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

        bool swap_is_atom = jacl_is_atom(container);
        uint8_t* saved_ip = vm->ip;
        BytecodeChunk* saved_chunk = vm->chunk;
        JaclVal swap_result;

        for (;;) {
          /* Read current value (atomic for atoms, plain for boxes) */
          JaclVal swap_old_val = swap_is_atom
            ? ATOMIC_LOAD_EXPLICIT(&ref->value, MEM_ACQUIRE)
            : ref->value;

          /* Push closure as callee slot + current value as argument */
          result = vm__push(vm, closure_val);
          if (result != VM_OK) return result;
          result = vm__push(vm, swap_old_val);
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
          vm->ip    = closure->chunk.code;
          vm->chunk = &closure->chunk;

          VMResult call_result = vm__run(vm, caller_frame_count);
          if (call_result != VM_OK) return call_result;

          /* Pop return value */
          result = vm__pop(vm, &swap_result);
          if (result != VM_OK) return result;

          if (swap_is_atom) {
            /* CAS loop: try to store result, retry if value changed */
            JaclVal expected = swap_old_val;
            if (ATOMIC_CAS(&ref->value, &expected, swap_result,
                           MEM_ACQ_REL, MEM_ACQUIRE)) {
              /* CAS succeeded — fire write barrier */
              gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                               swap_old_val, swap_result);
              break; /* exit retry loop */
            }
            /* CAS failed — swap_result becomes garbage, retry */
          } else {
            /* Box: non-atomic store, write barrier always fires */
            gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                             swap_old_val, swap_result);
            ref->value = swap_result;
            break; /* no retry for boxes */
          }
        }

        /* Restore state */
        vm->ip    = saved_ip;
        vm->chunk = saved_chunk;
        frame = &vm->frames[vm->frame_count - 1];

        result = vm__push(vm, swap_result);
        if (result != VM_OK) return result;
        break;
      }

      /* --- M11: i64 typed arithmetic/comparison opcodes --- */

      case OP_ADD_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, (uint64_t)(a + b));
        if (result != VM_OK) return result;
        break;
      }

      case OP_SUB_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, (uint64_t)(a - b));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MUL_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, (uint64_t)(a * b));
        if (result != VM_OK) return result;
        break;
      }

      case OP_DIV_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        if (b == 0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, (uint64_t)(a / b));
        if (result != VM_OK) return result;
        break;
      }

      case OP_MOD_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        if (b == 0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, (uint64_t)(a % b));
        if (result != VM_OK) return result;
        break;
      }

      case OP_NEG_I64: {
        JaclVal raw_a;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        result = vm__push(vm, (uint64_t)(-a));
        if (result != VM_OK) return result;
        break;
      }

      case OP_LT_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, a < b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GT_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, a > b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_LE_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, a <= b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GE_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, a >= b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_EQ_I64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        int64_t a = (int64_t)raw_a;
        int64_t b = (int64_t)raw_b;
        result = vm__push(vm, a == b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      /* --- M11: f64 typed arithmetic/comparison opcodes --- */

      case OP_ADD_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        double r = a + b;
        uint64_t raw_r;
        memcpy(&raw_r, &r, sizeof(uint64_t));
        result = vm__push(vm, raw_r);
        if (result != VM_OK) return result;
        break;
      }

      case OP_SUB_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        double r = a - b;
        uint64_t raw_r;
        memcpy(&raw_r, &r, sizeof(uint64_t));
        result = vm__push(vm, raw_r);
        if (result != VM_OK) return result;
        break;
      }

      case OP_MUL_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        double r = a * b;
        uint64_t raw_r;
        memcpy(&raw_r, &r, sizeof(uint64_t));
        result = vm__push(vm, raw_r);
        if (result != VM_OK) return result;
        break;
      }

      case OP_DIV_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        if (b == 0.0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        double r = a / b;
        uint64_t raw_r;
        memcpy(&raw_r, &r, sizeof(uint64_t));
        result = vm__push(vm, raw_r);
        if (result != VM_OK) return result;
        break;
      }

      case OP_MOD_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        if (b == 0.0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        double r = fmod(a, b);
        uint64_t raw_r;
        memcpy(&raw_r, &r, sizeof(uint64_t));
        result = vm__push(vm, raw_r);
        if (result != VM_OK) return result;
        break;
      }

      case OP_NEG_F64: {
        JaclVal raw_a;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a;
        memcpy(&a, &raw_a, sizeof(double));
        double r = -a;
        uint64_t raw_r;
        memcpy(&raw_r, &r, sizeof(uint64_t));
        result = vm__push(vm, raw_r);
        if (result != VM_OK) return result;
        break;
      }

      case OP_LT_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        result = vm__push(vm, a < b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GT_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        result = vm__push(vm, a > b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_LE_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        result = vm__push(vm, a <= b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GE_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        result = vm__push(vm, a >= b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_EQ_F64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        double a, b;
        memcpy(&a, &raw_a, sizeof(double));
        memcpy(&b, &raw_b, sizeof(double));
        result = vm__push(vm, a == b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      /* --- M11: u64 unsigned-specific opcodes --- */

      case OP_DIV_U64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        uint64_t a = raw_a;
        uint64_t b = raw_b;
        if (b == 0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, a / b);
        if (result != VM_OK) return result;
        break;
      }

      case OP_MOD_U64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        uint64_t a = raw_a;
        uint64_t b = raw_b;
        if (b == 0) {
          vm__set_error(vm, "division by zero");
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, a % b);
        if (result != VM_OK) return result;
        break;
      }

      case OP_LT_U64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a < raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GT_U64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a > raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_LE_U64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a <= raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GE_U64: {
        JaclVal raw_b, raw_a;
        result = vm__pop(vm, &raw_b); if (result != VM_OK) return result;
        result = vm__pop(vm, &raw_a); if (result != VM_OK) return result;
        result = vm__push(vm, raw_a >= raw_b ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      /* --- M11: Type conversion opcodes --- */

      case OP_TO_I32: {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        int32_t i;
        switch ((JaclType)src_type) {
          case TYPE_I32: { result = vm__push(vm, val); break; }
          case TYPE_U32: { i = (int32_t)jacl_as_u32(val); result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_I64: { i = (int32_t)(int64_t)val; result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_U64: { i = (int32_t)(uint64_t)val; result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_F32: { i = (int32_t)jacl_as_f32(val); result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); i = (int32_t)d; result = vm__push(vm, jacl_i32(i)); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { result = vm__push(vm, jacl_i32(jacl_as_i32(val))); }
            else if (jacl_is_u32(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_u32(val))); }
            else if (jacl_is_i64(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_i64(val))); }
            else if (jacl_is_u64(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_u64(val))); }
            else if (jacl_is_f32(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_f32(val))); }
            else if (jacl_is_f64(val)) { result = vm__push(vm, jacl_i32((int32_t)jacl_as_f64(val))); }
            else { vm__set_error(vm, "cannot convert %s to i32", vm__type_name(val)); return VM_RUNTIME_ERROR; }
            break;
          }
          default: { vm__set_error(vm, "invalid source type for to-i32"); return VM_RUNTIME_ERROR; }
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_TO_I64: {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        int64_t i;
        switch ((JaclType)src_type) {
          case TYPE_I32: { i = (int64_t)jacl_as_i32(val); result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_U32: { i = (int64_t)(uint32_t)jacl_as_u32(val); result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_I64: { result = vm__push(vm, val); break; }
          case TYPE_U64: { result = vm__push(vm, val); break; }
          case TYPE_F32: { i = (int64_t)jacl_as_f32(val); result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); i = (int64_t)d; result = vm__push(vm, (uint64_t)i); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { i = (int64_t)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { i = (int64_t)(uint32_t)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { i = jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { i = (int64_t)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { i = (int64_t)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { i = (int64_t)jacl_as_f64(val); }
            else { vm__set_error(vm, "cannot convert %s to i64", vm__type_name(val)); return VM_RUNTIME_ERROR; }
            result = vm__push(vm, (uint64_t)i);
            break;
          }
          default: { vm__set_error(vm, "invalid source type for to-i64"); return VM_RUNTIME_ERROR; }
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_TO_U32: {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        uint32_t u;
        switch ((JaclType)src_type) {
          case TYPE_I32: { u = (uint32_t)jacl_as_i32(val); result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_U32: { result = vm__push(vm, val); break; }
          case TYPE_I64: { u = (uint32_t)(int64_t)val; result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_U64: { u = (uint32_t)(uint64_t)val; result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_F32: { u = (uint32_t)jacl_as_f32(val); result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); u = (uint32_t)d; result = vm__push(vm, jacl_u32(u)); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { u = (uint32_t)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { u = jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { u = (uint32_t)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { u = (uint32_t)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { u = (uint32_t)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { u = (uint32_t)jacl_as_f64(val); }
            else { vm__set_error(vm, "cannot convert %s to u32", vm__type_name(val)); return VM_RUNTIME_ERROR; }
            result = vm__push(vm, jacl_u32(u));
            break;
          }
          default: { vm__set_error(vm, "invalid source type for to-u32"); return VM_RUNTIME_ERROR; }
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_TO_U64: {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        uint64_t u;
        switch ((JaclType)src_type) {
          case TYPE_I32: { u = (uint64_t)(int64_t)jacl_as_i32(val); result = vm__push(vm, u); break; }
          case TYPE_U32: { u = (uint64_t)jacl_as_u32(val); result = vm__push(vm, u); break; }
          case TYPE_I64: { result = vm__push(vm, val); break; }
          case TYPE_U64: { result = vm__push(vm, val); break; }
          case TYPE_F32: { u = (uint64_t)jacl_as_f32(val); result = vm__push(vm, u); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); u = (uint64_t)d; result = vm__push(vm, u); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { u = (uint64_t)(int64_t)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { u = (uint64_t)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { u = (uint64_t)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { u = jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { u = (uint64_t)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { u = (uint64_t)jacl_as_f64(val); }
            else { vm__set_error(vm, "cannot convert %s to u64", vm__type_name(val)); return VM_RUNTIME_ERROR; }
            result = vm__push(vm, u);
            break;
          }
          default: { vm__set_error(vm, "invalid source type for to-u64"); return VM_RUNTIME_ERROR; }
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_TO_F32: {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        float f;
        switch ((JaclType)src_type) {
          case TYPE_I32: { f = (float)jacl_as_i32(val); result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_U32: { f = (float)jacl_as_u32(val); result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_I64: { f = (float)(int64_t)val; result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_U64: { f = (float)(uint64_t)val; result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_F32: { result = vm__push(vm, val); break; }
          case TYPE_F64: { double d; memcpy(&d, &val, sizeof(double)); f = (float)d; result = vm__push(vm, jacl_f32(f)); break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { f = (float)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { f = (float)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { f = (float)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { f = (float)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { f = jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { f = (float)jacl_as_f64(val); }
            else { vm__set_error(vm, "cannot convert %s to f32", vm__type_name(val)); return VM_RUNTIME_ERROR; }
            result = vm__push(vm, jacl_f32(f));
            break;
          }
          default: { vm__set_error(vm, "invalid source type for to-f32"); return VM_RUNTIME_ERROR; }
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_TO_F64: {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        double d = 0.0;
        bool need_push = true;
        switch ((JaclType)src_type) {
          case TYPE_I32: { d = (double)jacl_as_i32(val); break; }
          case TYPE_U32: { d = (double)jacl_as_u32(val); break; }
          case TYPE_I64: { d = (double)(int64_t)val; break; }
          case TYPE_U64: { d = (double)(uint64_t)val; break; }
          case TYPE_F32: { d = (double)jacl_as_f32(val); break; }
          case TYPE_F64: { result = vm__push(vm, val); need_push = false; break; }
          case TYPE_DYN: {
            if (jacl_is_i32(val)) { d = (double)jacl_as_i32(val); }
            else if (jacl_is_u32(val)) { d = (double)jacl_as_u32(val); }
            else if (jacl_is_i64(val)) { d = (double)jacl_as_i64(val); }
            else if (jacl_is_u64(val)) { d = (double)jacl_as_u64(val); }
            else if (jacl_is_f32(val)) { d = (double)jacl_as_f32(val); }
            else if (jacl_is_f64(val)) { d = jacl_as_f64(val); }
            else { vm__set_error(vm, "cannot convert %s to f64", vm__type_name(val)); return VM_RUNTIME_ERROR; }
            break;
          }
          default: { vm__set_error(vm, "invalid source type for to-f64"); return VM_RUNTIME_ERROR; }
        }
        if (need_push) {
          uint64_t raw;
          memcpy(&raw, &d, sizeof(uint64_t));
          result = vm__push(vm, raw);
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_TO_DYN: {
        uint8_t src_type = vm__read_byte(vm);
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        switch ((JaclType)src_type) {
          case TYPE_I32:
          case TYPE_U32:
          case TYPE_F32:
          case TYPE_BOOL:
          case TYPE_NIL:
          case TYPE_STR:
          case TYPE_DYN: {
            /* Already tagged — push as-is */
            result = vm__push(vm, val);
            break;
          }
          case TYPE_I64: {
            int64_t i = (int64_t)val;
            result = vm__push(vm, jacl_i64(&vm->heap, i));
            break;
          }
          case TYPE_U64: {
            uint64_t u = val;
            result = vm__push(vm, jacl_u64(&vm->heap, u));
            break;
          }
          case TYPE_F64: {
            double d;
            memcpy(&d, &val, sizeof(double));
            result = vm__push(vm, jacl_f64(&vm->heap, d));
            break;
          }
          default: {
            result = vm__push(vm, val);
            break;
          }
        }
        if (result != VM_OK) return result;
        break;
      }

      /* --- M11: Typed constant opcodes --- */

      case OP_CONST_I64:
      case OP_CONST_U64:
      case OP_CONST_F64: {
        uint16_t idx = vm__read_u16(vm);
        /* Push raw 64-bit value from constant pool (no tag) */
        result = vm__push(vm, vm->chunk->constants[idx]);
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

  CompileResult cr = compiler_compile(parse, arena, &intern_table, &vm->heap);
  if (cr.error_count > 0) {
    vm->error_message = cr.error_message ? cr.error_message : "compile error";
    return VM_RUNTIME_ERROR;
  }

  vm->intern_table = &intern_table;

  if (cr.suspending) {
    /* Top-level code is CPS-transformed. The chunk contains OP_CLOSURE + OP_HALT
       which produces the main CPS closure on the stack. Execute the chunk to
       get the closure, then call it with a resolve_k continuation. */
    VMResult r = vm_exec(vm, &cr.chunk);
    if (r != VM_OK) return r;

    /* The main CPS closure is on the stack */
    JaclVal main_cl_val = vm->stack[0];
    if (!jacl_is_closure(main_cl_val)) {
      vm->error_message = "internal error: CPS top-level did not produce closure";
      return VM_RUNTIME_ERROR;
    }
    JaclClosure *main_cl = jacl_as_closure(main_cl_val);

    /* Create a completion future and resolve_k */
    JaclVal completion = jacl_future(&vm->heap);
    JaclVal resolve_k = runtime__create_resolve_closure(&vm->heap, arena,
                                                         completion);

    /* Set up the call: main_cl(resolve_k) */
    vm->stack_top = 0;
    vm->stack[0]  = main_cl_val;
    vm->stack[1]  = resolve_k;
    vm->stack_top = 2;

    JaclClosure top_closure_wrapper;
    memset(&top_closure_wrapper, 0, sizeof(top_closure_wrapper));
    top_closure_wrapper.chunk = cr.chunk;

    vm->frames[0].closure    = &top_closure_wrapper;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 0;
    vm->frames[0].chunk      = &cr.chunk;
    vm->frame_count = 1;

    /* Now call the main CPS closure */
    vm->frames[1].closure    = main_cl;
    vm->frames[1].return_ip  = NULL;
    vm->frames[1].stack_base = 1; /* 1 arg (resolve_k) */
    vm->frames[1].chunk      = &main_cl->chunk;
    vm->frame_count = 2;
    vm->ip    = main_cl->chunk.code;
    vm->chunk = &main_cl->chunk;
    vm->top_chunk = &main_cl->chunk;

    r = vm__run(vm, 1);

    /* After execution, the completion future should be resolved.
       Extract its result as the program's return value. */
    JaclFuture *cfut = jacl_as_future(completion);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_RESOLVED) {
      JaclVal final_result = (JaclVal)cfut->result;
      vm->stack[0] = final_result;
      vm->stack_top = 1;
    } else if (state == FUTURE_ERROR) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    }
    return r;
  }

  return vm_exec(vm, &cr.chunk);
}

#endif /* VM_C */
