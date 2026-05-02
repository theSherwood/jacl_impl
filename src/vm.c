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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

/* --- Stack size --- */

#define VM_STACK_MAX 256
#define VM_FRAMES_MAX 64

/* --- Environment initial capacity --- */

#define VM_ENV_INIT_CAP 16

/* --- Result codes --- */

typedef enum {
  VM_OK,
  VM_RUNTIME_ERROR,
  VM_STACK_OVERFLOW,
  VM_YIELD          /* generator yielded a value */
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

/* Forward declaration: defined in syntax.c (later in unity build) */
JaclVal jacl_gensym_next(const char *prefix, uint32_t prefix_len,
                         ThreadHeap *heap, JaclInternTable *intern,
                         uint32_t *gensym_counter,
                         uint32_t scope_mark, const char **err);

/* --- Ctx Pool: GC-managed free-list of pre-allocated ctx structs --- */

#define CTX_POOL_INIT_SIZE 8

typedef struct {
    volatile uintptr_t free_list_head; /* atomic: pointer to first free HeapRecord, or 0 */
    uint32_t struct_size;              /* StructTypeDef->total_size (byte size of data[]) */
    uint32_t type_idx;                 /* ctx struct type_idx in StructTypeRegistry */
    StructTypeDef *sdef;               /* cached pointer to ctx StructTypeDef */
} JaclCtxPool;

void ctx_pool_init(JaclCtxPool *pool, ThreadHeap *heap,
                   StructTypeRegistry *reg) {
    uint32_t idx = reg->ctx_type_idx;
    StructTypeDef *sdef = reg->defs[idx];
    pool->free_list_head = 0;
    pool->struct_size = sdef->total_size;
    pool->type_idx = idx;
    pool->sdef = sdef;

    /* Pre-allocate CTX_POOL_INIT_SIZE ctx structs and link into free list */
    for (int i = 0; i < CTX_POOL_INIT_SIZE; i++) {
        HeapRecord *s = (HeapRecord *)gc_alloc(heap, OBJ_HEAP_RECORD,
                          sizeof(HeapRecord) + sdef->total_size);
        s->type_idx = idx;
        s->total_size = sdef->total_size;
        memset(s->data, 0, sdef->total_size);

        /* Link into free list: store next pointer at byte 0 of data[] */
        uintptr_t head;
        do {
            head = ATOMIC_LOAD_EXPLICIT(&pool->free_list_head, MEM_ACQUIRE);
            *(uintptr_t *)s->data = head;
        } while (!ATOMIC_CAS(&pool->free_list_head, &head,
                              (uintptr_t)s, MEM_RELEASE, MEM_RELAXED));
    }
}

HeapRecord *ctx_pool_alloc(JaclCtxPool *pool, ThreadHeap *heap) {
    /* Try to pop from free list via atomic CAS */
    uintptr_t head;
    for (;;) {
        head = ATOMIC_LOAD_EXPLICIT(&pool->free_list_head, MEM_ACQUIRE);
        if (head == 0) break;
        HeapRecord *s = (HeapRecord *)head;
        uintptr_t next = *(uintptr_t *)s->data;
        if (ATOMIC_CAS(&pool->free_list_head, &head, next,
                        MEM_ACQ_REL, MEM_RELAXED)) {
            /* Zero data before returning */
            memset(s->data, 0, pool->struct_size);
            return s;
        }
    }
    /* Pool empty: allocate fresh via gc_alloc */
    HeapRecord *s = (HeapRecord *)gc_alloc(heap, OBJ_HEAP_RECORD,
                      sizeof(HeapRecord) + pool->struct_size);
    s->type_idx = pool->type_idx;
    s->total_size = pool->struct_size;
    memset(s->data, 0, pool->struct_size);
    return s;
}

void ctx_pool_free(JaclCtxPool *pool, HeapRecord *s) {
    /* Clear reference fields to prevent stale GC pointers */
    StructTypeDef *sdef = pool->sdef;
    for (uint32_t i = 0; i < sdef->field_count; i++) {
        JaclType ft = sdef->fields[i].type;
        if (ft == TYPE_STR || ft == TYPE_VEC || ft == TYPE_MAP ||
            ft == TYPE_CLOSURE || ft == TYPE_DYN || ft == TYPE_STRUCT) {
            JaclVal nil_val = JACL_NIL;
            memcpy(s->data + sdef->fields[i].offset, &nil_val, sizeof(JaclVal));
        }
    }

    /* Push to free list: store next pointer at byte 0 of data[] */
    uintptr_t head;
    do {
        head = ATOMIC_LOAD_EXPLICIT(&pool->free_list_head, MEM_ACQUIRE);
        *(uintptr_t *)s->data = head;
    } while (!ATOMIC_CAS(&pool->free_list_head, &head,
                          (uintptr_t)s, MEM_RELEASE, MEM_RELAXED));
}

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
  RememberedSet* remembered_set; /* generational write barrier target (NULL in single-threaded) */
  volatile uint32_t *gc_active_ptr; /* pointer to runtime's gc_active (NULL in single-threaded) */
  void*          runtime;        /* Runtime pointer for concurrent GC trigger (NULL in single-threaded) */
  int            worker_id;      /* Worker thread ID for task pinning (-1 if not on a worker) */
  const char*    error_message;  /* last error message, or NULL */
  uint32_t       error_line;     /* source line of last error */
  StackTrace     stack_trace;    /* most recent error's trace */
  StructTypeRegistry* struct_registry; /* struct type metadata from compiler */
  JaclCtxPool *ctx_pool;       /* ctx struct pool (NULL until initialized) */
  JaclVal    ctx;              /* current implicit context struct (never NIL during execution) */
  JaclVal    saved_ctx[8];     /* with-ctx save stack for nested forks */
  uint8_t    saved_ctx_count;  /* number of entries in saved_ctx */
  JaclVal*   gc_handle_slots;  /* external GC root handles (owned by embedding layer) */
  uint32_t   gc_handle_count;  /* number of slots in gc_handle_slots */
  /* Native function registry (owned by embedding layer) */
  JaclVal    (*call_native)(void* ctx, uint32_t fn_index, JaclVal* args, int argc);
  void*      native_fn_ctx;     /* JaclVM_s* for dispatch callback */
  int8_t*    native_fn_arities; /* arity per native fn (-1 = variadic) */
  uint32_t   native_fn_count;   /* number of registered native functions */
  /* Spread count side buffer for OP_SPREAD / OP_CALL_SPREAD / OP_FOLD_SPREAD */
  uint32_t   spread_counts[32]; /* small stack of spread element counts */
  uint32_t   spread_count_top;  /* top index in spread_counts */
  /* Generator/yield support */
  JaclVal    yield_value;        /* yielded value (set by OP_YIELD_SM) */
  /* US-009: scope mark for hygiene in staged macro expansion */
  uint32_t   macro_scope_mark;   /* >0 during staged macro eval; make-syntax applies this */
  /* US-010: gensym counter pointer — set by expand__node before staged closure invocation */
  uint32_t  *gensym_counter_ptr; /* points into ExpandState.gensym_counter; NULL outside staged eval */
  /* US-014: bitmap marking stack slots that hold raw inline struct bytes (not GC-traceable JaclVals) */
  uint8_t    inline_slot_bitmap[VM_STACK_MAX / 8];
} VM;

/* --- jacl_context_t: reentrant execution context ---
 *
 * Owns arena, VM (which contains ThreadHeap), intern table, macro table.
 * Child contexts share parent's intern table but have their own arena/heap.
 * Macro expansion state (ExpandState) is per-context, eliminating the
 * file-static reentrancy hazards that previously lived in syntax.c.
 *
 * VM REENTRANCY AUDIT (US-005):
 * Audited vm_exec and all transitively called functions for state that
 * assumes a single in-flight execution. Findings:
 *
 * 1. vm.c: NO file-static mutable state. All VM state (stack, frames, ip,
 *    heap, environment) lives in the VM struct. vm_exec is reentrant as
 *    long as each invocation uses a separate VM instance.
 *
 * 2. compiler.c: NO file-static mutable state.
 *
 * 3. syntax.c: HAD 7 file-static variables (reentrancy hazards):
 *    - expand__error_msg/line/col: macro expansion error state
 *    - expand__frames[256]/frame_top: macro call stack for error reporting
 *    - expand__scope_counter: scope mark for hygiene
 *    - syntax__gensym_counter: unique symbol counter
 *    FIX: All moved into ExpandState struct, passed through expansion
 *    functions. jacl_gensym_next takes uint32_t* counter parameter.
 *
 * 4. runtime.c, string.c, value.c, embed.c: NO file-static mutable state.
 *
 * 5. No signal handlers or global instruction-pointer registers found.
 */

typedef struct jacl_context_s jacl_context_t;
struct jacl_context_s {
    arena_t          arena;
    VM               vm;
    JaclInternTable  intern_table;
    MacroTable       macro_table;
    uint64_t         restriction_set;     /* all-permissive = UINT64_MAX */
    ExpandState      expand;

    /* Parent context (NULL for root) */
    jacl_context_t  *parent;
    bool             owns_intern_table;   /* false when sharing parent's */
};

/* Scoped context switching: saves/restores gc__current_heap and the emergency
 * GC callback so that nested context operations are reentrant.
 *
 * jacl_ctx_save:  snapshot current thread-local GC state (call BEFORE ctx_new)
 * jacl_ctx_enter: save + switch gc__current_heap to a context's heap
 * jacl_ctx_restore: restore a previously saved snapshot */
typedef struct {
    ThreadHeap *heap;
    void       (*gc_fn)(void *);
    void        *gc_ctx;
} jacl_ctx_saved_t;

void jacl_ctx_save    (jacl_ctx_saved_t *saved);
void jacl_ctx_enter   (jacl_context_t *ctx, jacl_ctx_saved_t *saved);
void jacl_ctx_restore (jacl_ctx_saved_t saved);

/* --- JaclError: error out-param for internal run API --- */
#ifndef JACL_ERROR_DEFINED
#define JACL_ERROR_DEFINED
typedef enum {
    JACL_ERROR_NONE = 0,
    JACL_ERROR_COMPILE,
    JACL_ERROR_RUNTIME
} JaclErrorKind;

typedef struct {
    JaclErrorKind kind;
    const char   *message;
    uint32_t      line;
    uint32_t      col;
} JaclError;
#endif

/* Forward declarations for context lifecycle functions, used by OP_INTERPRET
 * and other opcodes before their definitions later in this file. */
jacl_context_t *jacl_ctx_new(jacl_context_t *parent);
void jacl_ctx_destroy(jacl_context_t *ctx);
JaclVal jacl_ctx_run_source(jacl_context_t *ctx, const char *src, size_t len,
                            uint64_t restriction_set, JaclError *err_out);
JaclVal source_to_closure_in_place(const char *src, size_t len,
                                   arena_t *arena, ThreadHeap *heap,
                                   JaclInternTable *intern_table,
                                   ExpandState *expand,
                                   JaclError *err_out,
                                   JaclVal prelude_map);

/* --- API --- */

void     vm_init(VM* vm, arena_t* arena);
void     vm_destroy(VM* vm);
VMResult vm_exec(VM* vm, BytecodeChunk* chunk);

/* --- Pipeline convenience --- */

VMResult jacl_run(const char* source, VM* vm, arena_t* arena);
VMResult jacl_exec_program(ProgramResult* program, VM* vm);

/* --- GC collect (defined in gc_collect.c, after vm.c in unity build) --- */

void gc_collect(ThreadHeap *heap, VM *vm);
void gc_collect_minor(ThreadHeap *heap, VM *vm,
                              RememberedSet *remembered_set);
bool gc_should_major(ThreadHeap *heap);

/* US-015: introspection kind-name helper (defined in syntax.c) */
const char *syntax_kind_name(uint8_t kind);

/* --- Emergency GC callback for single-threaded mode --- */

void vm__emergency_gc_single(void *ctx) {
    VM *vm = (VM *)ctx;
    gc_collect(&vm->heap, vm);
}

/* --- Concurrent GC trigger (defined in runtime.c, after gc_collect.c) --- */

void gc_concurrent_trigger(void *runtime_ptr);

/* --- Runtime helpers (defined in runtime.c, after gc_collect.c) --- */

JaclVal runtime__create_resolve_closure(ThreadHeap *heap, arena_t *arena,
                                                JaclVal future_val);
void runtime__submit_spawn_task(void *runtime_ptr, JaclClosure *closure,
                                        JaclVal future_val, JaclVal parent_ctx);
void runtime__schedule_continuation(void *runtime_ptr,
                                            JaclClosure *continuation,
                                            JaclVal result);
void runtime__schedule_sm_resumption(void *runtime_ptr,
                                             JaclVal state_machine,
                                             JaclVal result);
void runtime__schedule_waiters(void *runtime_ptr,
                                       FutureWaiter *waiters,
                                       JaclVal result);
void runtime__submit_parallel_task(void *runtime_ptr,
                                           JaclClosure *closure,
                                           JaclVal agg_val,
                                           uint32_t index,
                                           JaclVal parent_ctx);
void runtime__submit_race_task(void *runtime_ptr,
                                       JaclClosure *closure,
                                       JaclVal agg_val,
                                       JaclVal parent_ctx);
void runtime__complete_parallel_slot(void *runtime_ptr,
                                             VM *vm,
                                             JaclVal agg_val,
                                             uint32_t index,
                                             JaclVal result);
void runtime__complete_race_slot(void *runtime_ptr,
                                         VM *vm,
                                         JaclVal agg_val,
                                         JaclVal result);

/* --- Type name helper for error messages --- */

const char* vm__type_name(JaclVal v) {
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
  if (jacl_is_stream(v))       return "stream";
  if (jacl_is_native_fn(v))    return "native-fn";
  if (jacl_is_box(v))          return "box";
  if (jacl_is_atom(v))         return "atom";
  if (jacl_is_cell(v))         return "cell";
  if (jacl_is_struct(v))       return "struct";
  if (jacl_is_typed_vector(v)) return "typed-vec";
  if (jacl_is_typed_map(v))    return "typed-map";
  return "unknown";
}

/* --- Error reporting helper --- */

void vm__set_error(VM* vm, const char* fmt, ...) {
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

void vm__default_print(const char* text, uint32_t len, void* ctx) {
  (void)ctx;
  fwrite(text, 1, len, stdout);
}

/* --- Truthiness helper --- */

bool vm__is_falsy(JaclVal v) {
  return jacl_is_nil(v) || v == JACL_FALSE;
}

/* --- Stack trace capture --- */

/**
 * Capture the current call frame chain into the VM's stack trace.
 * Walks frames from innermost to outermost.
 */
void vm__capture_trace(VM* vm) {
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
void vm_init(VM* vm, arena_t* arena) {
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
  vm->remembered_set = NULL;
  vm->gc_active_ptr = NULL;
  vm->runtime       = NULL;
  vm->worker_id     = -1;
  vm->frame_count   = 0;
  vm->error_message = NULL;
  vm->error_line    = 0;
  vm->stack_trace.count = 0;
  vm->struct_registry = NULL;
  vm->ctx_pool        = NULL;
  vm->ctx             = JACL_NIL;
  vm->saved_ctx_count = 0;
  vm->gc_handle_slots = NULL;
  vm->gc_handle_count = 0;
  vm->call_native       = NULL;
  vm->native_fn_ctx     = NULL;
  vm->native_fn_arities = NULL;
  vm->native_fn_count   = 0;
  vm->spread_count_top  = 0;
  vm->yield_value        = JACL_NIL;
  memset(vm->inline_slot_bitmap, 0, sizeof(vm->inline_slot_bitmap));

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
void vm_destroy(VM* vm) {
  gc_heap_destroy(&vm->heap);
  gc_block_pool_destroy(&vm->block_pool);
}

/* --- Stack helpers --- */

VMResult vm__push(VM* vm, JaclVal value) {
  if (vm->stack_top >= VM_STACK_MAX) {
    vm->error_message = "stack overflow";
    return VM_STACK_OVERFLOW;
  }
  vm->stack[vm->stack_top++] = value;
  return VM_OK;
}

VMResult vm__pop(VM* vm, JaclVal* out) {
  if (vm->stack_top == 0) {
    vm->error_message = "stack underflow";
    return VM_RUNTIME_ERROR;
  }
  *out = vm->stack[--vm->stack_top];
  return VM_OK;
}

/* --- Instruction pointer helpers --- */

uint8_t vm__read_byte(VM* vm) {
  return *vm->ip++;
}

uint16_t vm__read_u16(VM* vm) {
  uint8_t hi = vm__read_byte(vm);
  uint8_t lo = vm__read_byte(vm);
  return (uint16_t)((hi << 8) | lo);
}

/* --- Typed collection helpers --- */

/* Compute struct slot width from type def */
static inline uint32_t vm__struct_width(StructTypeDef* sdef) {
  return (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
}

/* Extract struct raw bytes into a JaclVal slot array for strided push/set.
   Caller must provide slots[] with at least vm__struct_width(sdef) elements. */
static inline void vm__struct_to_slots(StructTypeDef* sdef, HeapRecord* s,
                                       JaclVal* slots, uint32_t width) {
  memset(slots, 0, width * sizeof(JaclVal));
  memcpy(slots, s->data, sdef->total_size);
}

/* Decompose a heap HeapRecord into a flat JaclVal slot array.
   Returns the slot width.  Caller provides out[] (VM_MAX_STRUCT_SLOTS). */
#define VM_MAX_STRUCT_SLOTS 16
static inline uint32_t vm__unpack_struct(VM* vm, uint16_t type_idx,
                                         JaclVal struct_val, JaclVal* out) {
  StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
  uint32_t w = vm__struct_width(sdef);
  vm__struct_to_slots(sdef, jacl_as_heap_record_ptr(struct_val), out, w);
  return w;
}

/* Pop a struct from TOS into out[]. Handles both representations:
 *   - Inline bytes (width consecutive slots, marked in inline_slot_bitmap)
 *   - Heap HeapRecord pointer (single slot)
 * Returns width. The out buffer must hold at least width slots. */
static inline uint32_t vm__pop_struct(VM* vm, uint16_t type_idx, JaclVal* out) {
  StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
  uint32_t width = vm__struct_width(sdef);
  uint32_t total_size = sdef->total_size;
  uint32_t buf_bytes = width * (uint32_t)sizeof(JaclVal);
  bool is_inline = vm->stack_top > 0 &&
                   BITMAP_GET(vm->inline_slot_bitmap, vm->stack_top - 1);
  if (is_inline) {
    memcpy(out, &vm->stack[vm->stack_top - width], total_size);
    if (buf_bytes > total_size) {
      memset((uint8_t*)out + total_size, 0, buf_bytes - total_size);
    }
    for (uint32_t si = 0; si < width; si++) {
      BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
    }
    vm->stack_top -= width;
  } else {
    JaclVal heap_val = vm->stack[--vm->stack_top];
    HeapRecord* s = jacl_as_heap_record_ptr(heap_val);
    memcpy(out, s->data, total_size);
    if (buf_bytes > total_size) {
      memset((uint8_t*)out + total_size, 0, buf_bytes - total_size);
    }
  }
  return width;
}

/* --- Environment helpers --- */

void vm__env_grow(VM* vm) {
  uint32_t new_cap = vm->env.cap * 2;
  JaclVal* new_names  = (JaclVal*)arena_alloc(vm->arena, new_cap * sizeof(JaclVal));
  JaclVal* new_values = (JaclVal*)arena_alloc(vm->arena, new_cap * sizeof(JaclVal));
  memcpy(new_names, vm->env.names, vm->env.count * sizeof(JaclVal));
  memcpy(new_values, vm->env.values, vm->env.count * sizeof(JaclVal));
  vm->env.names  = new_names;
  vm->env.values = new_values;
  vm->env.cap    = new_cap;
}

void vm__env_set(VM* vm, JaclVal name, JaclVal value) {
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

JaclVal vm__env_get(VM* vm, JaclVal name, bool* found) {
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
  StructTypeRegistry* registry;
} VMFormatBuf;

void vm__fmt_init(VMFormatBuf* buf, arena_t* arena, StructTypeRegistry* registry) {
  buf->arena = arena;
  buf->registry = registry;
  buf->len   = 0;
  buf->cap   = 128;
  buf->data  = (char*)arena_alloc(arena, 128);
}

void vm__fmt_ensure(VMFormatBuf* buf, uint32_t extra) {
  if (buf->len + extra <= buf->cap) return;
  uint32_t new_cap = buf->cap ? buf->cap * 2 : 128;
  while (new_cap < buf->len + extra) new_cap *= 2;
  char* new_data = (char*)arena_alloc(buf->arena, new_cap);
  if (buf->len) memcpy(new_data, buf->data, buf->len);
  buf->data = new_data;
  buf->cap  = new_cap;
}

void vm__fmt_append(VMFormatBuf* buf, const char* str, uint32_t len) {
  vm__fmt_ensure(buf, len);
  memcpy(buf->data + buf->len, str, len);
  buf->len += len;
}

/* Format a struct (Name{f1: v1, ...}) from raw C-ABI bytes.
 * Used by both heap-struct OP_PRINT path and inline OP_PRINT_STRUCT. */
void vm__fmt_struct_bytes(VMFormatBuf* buf, StructTypeDef* sdef, const uint8_t* data) {
  vm__fmt_append(buf, sdef->name, sdef->name_len);
  vm__fmt_append(buf, "{", 1);
  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    if (fi > 0) vm__fmt_append(buf, ", ", 2);
    vm__fmt_append(buf, sdef->fields[fi].name, sdef->fields[fi].name_len);
    vm__fmt_append(buf, ": ", 2);
    uint32_t off = sdef->fields[fi].offset;
    char fbuf[64];
    int flen;
    switch (sdef->fields[fi].type) {
      case TYPE_I32: { int32_t n; memcpy(&n, data + off, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%d", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_I64: { int64_t n; memcpy(&n, data + off, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIi64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_U32: { uint32_t n; memcpy(&n, data + off, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%u", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_U64: { uint64_t n; memcpy(&n, data + off, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIu64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_F32: { float f; memcpy(&f, data + off, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", (double)f);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_F64: { double d; memcpy(&d, data + off, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", d);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break; }
      case TYPE_BOOL: { uint8_t b = data[off];
        vm__fmt_append(buf, b ? "true" : "false", b ? 4 : 5); break; }
      case TYPE_STRUCT: {
        if (buf->registry && sdef->fields[fi].struct_type_idx < buf->registry->count) {
          StructTypeDef* nested = buf->registry->defs[sdef->fields[fi].struct_type_idx];
          if (nested) vm__fmt_struct_bytes(buf, nested, data + off);
          else vm__fmt_append(buf, "<struct>", 8);
        } else {
          vm__fmt_append(buf, "<struct>", 8);
        }
        break;
      }
      default: {
        JaclVal fval; memcpy(&fval, data + off, sizeof(JaclVal));
        vm__fmt_value(buf, fval); break;
      }
    }
  }
  vm__fmt_append(buf, "}", 1);
}

void vm__fmt_value(VMFormatBuf* buf, JaclVal val) {
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
    uint32_t slen = jacl_string_byte_len(val);
    vm__fmt_append(buf, "\"", 1);
    if (jacl_is_heap_string(val)) {
      JaclHeapString* hs = jacl_as_heap_string(val);
      vm__fmt_append(buf, hs->data, hs->byte_len);
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
  } else if (jacl_is_typed_vector(val)) {
    jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(val);
    uint32_t count = jacl_typed_vec_count(tvec);
    n = snprintf(tmp, sizeof(tmp), "<typed-vec %u>", count);
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_typed_map(val)) {
    n = snprintf(tmp, sizeof(tmp), "<typed-map>");
    vm__fmt_append(buf, tmp, (uint32_t)n);
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
    vm__fmt_value(buf, MREF_VAL(ref));
  } else if (jacl_is_box(val)) {
    JaclMutableRef* ref = jacl_as_box(val);
    if (ref->type_idx > 0 && buf->registry &&
        ref->type_idx < buf->registry->count) {
      /* Struct box: format with struct name and field values */
      StructTypeDef* sdef = buf->registry->defs[ref->type_idx];
      if (sdef) {
        vm__fmt_append(buf, "<box ", 5);
        vm__fmt_append(buf, sdef->name, sdef->name_len);
        vm__fmt_append(buf, ":", 1);
        for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
          StructTypeField* sf = &sdef->fields[fi];
          vm__fmt_append(buf, " ", 1);
          vm__fmt_append(buf, sf->name, sf->name_len);
          vm__fmt_append(buf, "=", 1);
          /* Read field value from raw data bytes */
          JaclVal fv;
          switch (sf->type) {
            case TYPE_BOOL: { uint8_t b = ref->data[sf->offset]; fv = jacl_bool(b); break; }
            case TYPE_I32: { int32_t n; memcpy(&n, ref->data + sf->offset, 4); fv = jacl_i32(n); break; }
            case TYPE_U32: { uint32_t n; memcpy(&n, ref->data + sf->offset, 4); fv = jacl_u32(n); break; }
            case TYPE_F32: { float f; memcpy(&f, ref->data + sf->offset, 4); fv = jacl_f32(f); break; }
            case TYPE_I64: { int64_t n; memcpy(&n, ref->data + sf->offset, 8);
              char tb[32]; int tn = snprintf(tb, sizeof(tb), "%" PRIi64, n);
              vm__fmt_append(buf, tb, (uint32_t)tn); continue; }
            case TYPE_U64: { uint64_t n; memcpy(&n, ref->data + sf->offset, 8);
              char tb[32]; int tn = snprintf(tb, sizeof(tb), "%" PRIu64, n);
              vm__fmt_append(buf, tb, (uint32_t)tn); continue; }
            case TYPE_F64: { double d; memcpy(&d, ref->data + sf->offset, 8);
              char tb[32]; int tn = snprintf(tb, sizeof(tb), "%g", d);
              vm__fmt_append(buf, tb, (uint32_t)tn); continue; }
            default: { memcpy(&fv, ref->data + sf->offset, sizeof(JaclVal)); break; }
          }
          vm__fmt_value(buf, fv);
        }
        vm__fmt_append(buf, ">", 1);
      } else {
        vm__fmt_append(buf, "<box: unknown-struct>", 21);
      }
    } else {
      vm__fmt_append(buf, "<box: ", 6);
      vm__fmt_value(buf, MREF_VAL(ref));
      vm__fmt_append(buf, ">", 1);
    }
  } else if (jacl_is_atom(val)) {
    JaclMutableRef* ref = jacl_as_atom(val);
    vm__fmt_append(buf, "<atom: ", 7);
    vm__fmt_value(buf, MREF_VAL(ref));
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
  } else if (jacl_is_stream(val)) {
    vm__fmt_append(buf, "<stream>", 8);
  } else if (jacl_is_native_fn(val)) {
    n = snprintf(tmp, sizeof(tmp), "<native-fn #%u>",
                 jacl_as_native_fn_index(val));
    vm__fmt_append(buf, tmp, (uint32_t)n);
  } else if (jacl_is_syntax(val)) {
    JaclSyntax *syn = jacl_as_syntax(val);
    switch (syn->kind) {
    case SYNTAX_COMMAND: {
      vm__fmt_append(buf, "<syntax:command [", 17);
      vm__fmt_value(buf, syn->data.command.head);
      jacl_vec_root *args = (jacl_vec_root *)jacl_as_ptr(syn->data.command.args);
      uint32_t argc = jacl_vec_count(args);
      for (uint32_t i = 0; i < argc; i++) {
        vm__fmt_append(buf, " ", 1);
        vm__fmt_value(buf, jacl_vec_get(args, i).value);
      }
      vm__fmt_append(buf, "]>", 2);
      break;
    }
    case SYNTAX_LIT_INT:
      n = snprintf(tmp, sizeof(tmp), "<syntax:lit-int %d>", syn->data.lit_int.value);
      vm__fmt_append(buf, tmp, (uint32_t)n);
      break;
    case SYNTAX_LIT_FLOAT:
      n = snprintf(tmp, sizeof(tmp), "<syntax:lit-float %g>", (double)syn->data.lit_float.value);
      vm__fmt_append(buf, tmp, (uint32_t)n);
      break;
    case SYNTAX_LIT_STRING: {
      vm__fmt_append(buf, "<syntax:lit-string ", 19);
      vm__fmt_value(buf, syn->data.lit_string.value);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_VAR_REF: {
      vm__fmt_append(buf, "<syntax:var-ref $", 17);
      JaclVal name = syn->data.var_ref.name;
      uint32_t slen = jacl_string_byte_len(name);
      if (jacl_is_heap_string(name)) {
        JaclHeapString *hs = jacl_as_heap_string(name);
        vm__fmt_append(buf, hs->data, hs->byte_len);
      } else {
        char sbuf[8];
        jacl_string_data(name, sbuf, slen);
        vm__fmt_append(buf, sbuf, slen);
      }
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_BLOCK: {
      vm__fmt_append(buf, "<syntax:block {", 15);
      jacl_vec_root *cmds = (jacl_vec_root *)jacl_as_ptr(syn->data.block.commands);
      uint32_t cnt = jacl_vec_count(cmds);
      for (uint32_t i = 0; i < cnt; i++) {
        if (i > 0) vm__fmt_append(buf, "; ", 2);
        vm__fmt_append(buf, " ", 1);
        vm__fmt_value(buf, jacl_vec_get(cmds, i).value);
      }
      vm__fmt_append(buf, " }>", 3);
      break;
    }
    case SYNTAX_DEFMACRO: {
      vm__fmt_append(buf, "<syntax:defmacro>", 17);
      break;
    }
    case SYNTAX_QUOTE: {
      vm__fmt_append(buf, "<syntax:quote ", 14);
      vm__fmt_value(buf, syn->data.quote.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_SYNTAX_QUOTE: {
      vm__fmt_append(buf, "<syntax:syntax-quote ", 21);
      vm__fmt_value(buf, syn->data.syntax_quote.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_UNQUOTE: {
      vm__fmt_append(buf, "<syntax:unquote ", 16);
      vm__fmt_value(buf, syn->data.unquote.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    case SYNTAX_UNQUOTE_SPLICING: {
      vm__fmt_append(buf, "<syntax:unquote-splicing ", 24);
      vm__fmt_value(buf, syn->data.unquote_splicing.child);
      vm__fmt_append(buf, ">", 1);
      break;
    }
    default:
      n = snprintf(tmp, sizeof(tmp), "<syntax:%d>", syn->kind);
      vm__fmt_append(buf, tmp, (uint32_t)n);
      break;
    }
  } else {
    vm__fmt_append(buf, "<unknown>", 9);
  }
}

/* Format struct fields from raw data bytes using type def */
static void vm__fmt_struct_data(VMFormatBuf* buf, StructTypeDef* sdef,
                                const uint8_t* data) {
  char fbuf[32];
  int flen;
  vm__fmt_append(buf, sdef->name, sdef->name_len);
  vm__fmt_append(buf, "{", 1);
  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    if (fi > 0) vm__fmt_append(buf, ", ", 2);
    vm__fmt_append(buf, sdef->fields[fi].name, sdef->fields[fi].name_len);
    vm__fmt_append(buf, ": ", 2);
    switch (sdef->fields[fi].type) {
      case TYPE_I32: {
        int32_t n; memcpy(&n, data + sdef->fields[fi].offset, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%d", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_I64: {
        int64_t n; memcpy(&n, data + sdef->fields[fi].offset, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIi64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_U32: {
        uint32_t n; memcpy(&n, data + sdef->fields[fi].offset, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%u", n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_U64: {
        uint64_t n; memcpy(&n, data + sdef->fields[fi].offset, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%" PRIu64, n);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_F32: {
        float f; memcpy(&f, data + sdef->fields[fi].offset, 4);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", (double)f);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_F64: {
        double d; memcpy(&d, data + sdef->fields[fi].offset, 8);
        flen = snprintf(fbuf, sizeof(fbuf), "%g", d);
        vm__fmt_append(buf, fbuf, (uint32_t)flen); break;
      }
      case TYPE_BOOL: {
        uint8_t b = data[sdef->fields[fi].offset];
        vm__fmt_append(buf, b ? "true" : "false", b ? 4 : 5); break;
      }
      default: {
        JaclVal fval; memcpy(&fval, data + sdef->fields[fi].offset, sizeof(JaclVal));
        vm__fmt_value(buf, fval); break;
      }
    }
  }
  vm__fmt_append(buf, "}", 1);
}

/* --- Deep structural equality for collections --- */

bool vm__deep_eq(JaclVal a, JaclVal b) {
  return jacl_val_eq(a, b);
}

/* --- Shared struct field marshaling helpers --- */

/* Read a field from struct data and return as JaclVal.
 * Pass heap=NULL for unboxed 64-bit types (raw bits, for typed arithmetic).
 * Pass heap!=NULL for boxed 64-bit types (heap-allocated, for dyn/embed). */
JaclVal vm__heap_record_read_field(ThreadHeap* heap, HeapRecord* s,
                                      uint32_t offset, int field_type) {
  switch ((JaclType)field_type) {
    case TYPE_BOOL: { uint8_t b = s->data[offset]; return jacl_bool(b); }
    case TYPE_I32: { int32_t n; memcpy(&n, s->data + offset, 4); return jacl_i32(n); }
    case TYPE_U32: { uint32_t n; memcpy(&n, s->data + offset, 4); return jacl_u32(n); }
    case TYPE_F32: { float f; memcpy(&f, s->data + offset, 4); return jacl_f32(f); }
    case TYPE_I64: {
      int64_t n; memcpy(&n, s->data + offset, 8);
      return heap ? jacl_i64(heap, n) : (JaclVal)n;
    }
    case TYPE_U64: {
      uint64_t n; memcpy(&n, s->data + offset, 8);
      return heap ? jacl_u64(heap, n) : (JaclVal)n;
    }
    case TYPE_F64: {
      double d; memcpy(&d, s->data + offset, 8);
      if (heap) return jacl_f64(heap, d);
      JaclVal v; memcpy(&v, &d, 8); return v;
    }
    default: {
      JaclVal val; memcpy(&val, s->data + offset, sizeof(JaclVal)); return val;
    }
  }
}

/* Write a JaclVal to a struct field (caller must have already type-checked).
 * For i64/u64/f64, val must contain raw bits (unboxed VM representation). */
void vm__heap_record_write_field(HeapRecord* s, uint32_t offset,
                                    int field_type, JaclVal val) {
  switch ((JaclType)field_type) {
    case TYPE_BOOL: { uint8_t b = jacl_as_bool(val) ? 1 : 0; s->data[offset] = b; break; }
    case TYPE_I32: { int32_t n = jacl_as_i32(val); memcpy(s->data + offset, &n, 4); break; }
    case TYPE_U32: { uint32_t n = jacl_as_u32(val); memcpy(s->data + offset, &n, 4); break; }
    case TYPE_F32: { float f = jacl_as_f32(val); memcpy(s->data + offset, &f, 4); break; }
    case TYPE_I64: { int64_t n = (int64_t)val; memcpy(s->data + offset, &n, 8); break; }
    case TYPE_U64: { uint64_t n = val; memcpy(s->data + offset, &n, 8); break; }
    case TYPE_F64: { double d; memcpy(&d, &val, 8); memcpy(s->data + offset, &d, 8); break; }
    default: { memcpy(s->data + offset, &val, sizeof(JaclVal)); break; }
  }
}

/* Phase 5c: After reifying inline bytes to a heap HeapRecord, fix up any
 * TYPE_STRUCT fields by allocating child heap structs from the raw bytes.
 * This ensures OP_STRUCT_GET sees tagged pointers (not raw data) for nested
 * struct fields — matching the convention established by OP_STRUCT_NEW (heap). */
static void vm__reify_nested_heap_records(VM* vm, HeapRecord* s, StructTypeDef* sdef) {
  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    if (sdef->fields[fi].type != TYPE_STRUCT) continue;
    uint32_t nidx = sdef->fields[fi].struct_type_idx;
    if (!vm->struct_registry || nidx >= vm->struct_registry->count) continue;
    StructTypeDef* nsdef = vm->struct_registry->defs[nidx];
    gc__current_heap = &vm->heap;
    HeapRecord* ns = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                             sizeof(HeapRecord) + nsdef->total_size);
    ns->type_idx = nidx;
    ns->total_size = nsdef->total_size;
    memcpy(ns->data, s->data + sdef->fields[fi].offset, nsdef->total_size);
    /* Recurse for nested-nested struct fields */
    vm__reify_nested_heap_records(vm, ns, nsdef);
    /* Store tagged pointer in parent's data at field offset */
    JaclVal nval = jacl_heap_record_val(ns);
    memcpy(s->data + sdef->fields[fi].offset, &nval, sizeof(JaclVal));
  }
}

/* --- ctx fork / unfork helpers ---
 *
 * ctx_fork:   allocates a new ctx from the pool, copies data from parent_ctx,
 *             sets vm->ctx to the fork.  Returns the previous vm->ctx.
 *             No-op (returns vm->ctx unchanged) when parent_ctx is nil or
 *             pool is unavailable.
 *
 * ctx_unfork: frees current vm->ctx back to pool (if it differs from
 *             saved_ctx) and restores vm->ctx.
 */
static JaclVal ctx_fork(VM *vm, JaclVal parent_ctx) {
    JaclVal saved = vm->ctx;
    if (parent_ctx == JACL_NIL || !vm->ctx_pool) return saved;
    HeapRecord *src = jacl_as_heap_record_ptr(parent_ctx);
    HeapRecord *dst = ctx_pool_alloc(vm->ctx_pool, &vm->heap);
    if (dst) {
        StructTypeRegistry *reg = vm->struct_registry;
        memcpy(dst->data, src->data, reg->defs[reg->ctx_type_idx]->total_size);
        vm->ctx = jacl_heap_record_val(dst);
    }
    return saved;
}

static void ctx_unfork(VM *vm, JaclVal saved_ctx) {
    if (vm->ctx != saved_ctx && vm->ctx != JACL_NIL && vm->ctx_pool) {
        ctx_pool_free(vm->ctx_pool, jacl_as_heap_record_ptr(vm->ctx));
    }
    vm->ctx = saved_ctx;
}

/* Forward declaration for recursive call from OP_EACH */
VMResult vm__run(VM* vm, uint32_t min_frame);

/**
 * Execute a bytecode chunk.
 * Returns VM_OK on successful completion (OP_HALT),
 * VM_RUNTIME_ERROR on stack underflow or unknown opcode,
 * VM_STACK_OVERFLOW on stack overflow.
 */
VMResult vm_exec(VM* vm, BytecodeChunk* chunk) {
  vm->error_message = NULL;
  vm->error_line    = 0;

  /* Wrap top-level code in an implicit closure/frame */
  JaclClosure top_closure;
  memset(&top_closure, 0, sizeof(top_closure));
  top_closure.chunk    = *chunk;
  top_closure.variadic = false;

  vm->stack_top = 0;  /* US-009: reset stack after staged macro expansion may have left values */
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

/* --- Stream pull helper: unified pull from any stream kind --- */

typedef enum {
    STREAM_PULL_VALUE,     /* Got a value */
    STREAM_PULL_EXHAUSTED, /* Stream is done */
    STREAM_PULL_ERROR      /* Error occurred (vm->error_msg set) */
} StreamPullResult;

/**
 * Pull one element from any stream kind (generator, filter, etc.).
 * Saves and restores VM caller context internally.
 * On STREAM_PULL_VALUE: *out_value = yielded element.
 * On STREAM_PULL_EXHAUSTED: *out_value = JACL_NIL.
 */
StreamPullResult vm__pull_stream_one(VM* vm, JaclVal stream_val,
                                            JaclVal* out_value) {
    JaclStream* stream = jacl_as_stream(stream_val);

    if (stream->state == STREAM_EXHAUSTED) {
        *out_value = JACL_NIL;
        return STREAM_PULL_EXHAUSTED;
    }

    /* --- Filter stream: pull from source, apply predicate, loop --- */
    if (stream->kind == STREAM_KIND_FILTER) {
        JaclVal source = stream->args[0];
        JaclVal predicate_val = stream->args[1];
        JaclClosure* predicate = jacl_as_closure(predicate_val);

        for (;;) {
            JaclVal elem;
            StreamPullResult pr = vm__pull_stream_one(vm, source, &elem);
            if (pr == STREAM_PULL_EXHAUSTED) {
                stream->state = STREAM_EXHAUSTED;
                *out_value = JACL_NIL;
                return STREAM_PULL_EXHAUSTED;
            }
            if (pr == STREAM_PULL_ERROR) return STREAM_PULL_ERROR;

            /* Call predicate closure with elem */
            VMResult r;
            r = vm__push(vm, predicate_val);
            if (r != VM_OK) return STREAM_PULL_ERROR;
            r = vm__push(vm, elem);
            if (r != VM_OK) return STREAM_PULL_ERROR;

            if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_error(vm, "stack overflow");
                return STREAM_PULL_ERROR;
            }
            uint32_t cf_count = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = predicate;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &predicate->chunk;

            uint8_t* save_ip = vm->ip;
            BytecodeChunk* save_chunk = vm->chunk;
            vm->ip    = predicate->chunk.code;
            vm->chunk = &predicate->chunk;

            VMResult inner = vm__run(vm, cf_count);
            if (inner != VM_OK) {
                stream->state = STREAM_ERROR;
                return STREAM_PULL_ERROR;
            }

            JaclVal pred_result;
            r = vm__pop(vm, &pred_result);
            if (r != VM_OK) return STREAM_PULL_ERROR;

            vm->ip    = save_ip;
            vm->chunk = save_chunk;

            if (!vm__is_falsy(pred_result)) {
                *out_value = elem;
                return STREAM_PULL_VALUE;
            }
            /* predicate falsy — try next element */
        }
    }

    /* --- Transform stream: pull from source, apply fn, return result --- */
    if (stream->kind == STREAM_KIND_TRANSFORM) {
        JaclVal source = stream->args[0];
        JaclVal fn_val = stream->args[1];
        JaclClosure* fn = jacl_as_closure(fn_val);

        JaclVal elem;
        StreamPullResult pr = vm__pull_stream_one(vm, source, &elem);
        if (pr == STREAM_PULL_EXHAUSTED) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }
        if (pr == STREAM_PULL_ERROR) return STREAM_PULL_ERROR;

        /* Call transform closure with elem */
        VMResult r;
        r = vm__push(vm, fn_val);
        if (r != VM_OK) return STREAM_PULL_ERROR;
        r = vm__push(vm, elem);
        if (r != VM_OK) return STREAM_PULL_ERROR;

        if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_error(vm, "stack overflow");
            return STREAM_PULL_ERROR;
        }
        uint32_t cf_count = vm->frame_count;
        CallFrame* cf = &vm->frames[vm->frame_count++];
        cf->closure    = fn;
        cf->return_ip  = vm->ip;
        cf->stack_base = vm->stack_top - 1;
        cf->chunk      = &fn->chunk;

        uint8_t* save_ip = vm->ip;
        BytecodeChunk* save_chunk = vm->chunk;
        vm->ip    = fn->chunk.code;
        vm->chunk = &fn->chunk;

        VMResult inner = vm__run(vm, cf_count);
        if (inner != VM_OK) {
            stream->state = STREAM_ERROR;
            return STREAM_PULL_ERROR;
        }

        JaclVal transformed;
        r = vm__pop(vm, &transformed);
        if (r != VM_OK) return STREAM_PULL_ERROR;

        vm->ip    = save_ip;
        vm->chunk = save_chunk;

        *out_value = transformed;
        return STREAM_PULL_VALUE;
    }

    /* --- Lines stream: yield one line at a time from source string --- */
    if (stream->kind == STREAM_KIND_LINES) {
        JaclVal src_str = stream->args[0];
        int32_t cur_idx = jacl_as_i32(stream->args[1]);
        uint32_t byte_len = jacl_string_byte_len(src_str);

        if ((uint32_t)cur_idx >= byte_len) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Copy string data to scan for newlines */
        char sbuf[4096];
        char* data = sbuf;
        if (byte_len > sizeof(sbuf)) {
            data = (char*)arena_alloc(vm->arena, byte_len);
        }
        jacl_string_data(src_str, data, byte_len);

        /* Find end of current line */
        uint32_t start = (uint32_t)cur_idx;
        uint32_t end = start;
        while (end < byte_len && data[end] != '\n') {
            end++;
        }
        /* Handle \r\n */
        uint32_t line_end = end;
        if (line_end > start && data[line_end - 1] == '\r') {
            line_end--;
        }
        /* Advance past the newline */
        uint32_t next_idx = end < byte_len ? end + 1 : end;

        /* Skip trailing empty line: if we're at the very end and line is empty */
        if (line_end == start && next_idx >= byte_len) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        stream->args[1] = jacl_i32((int32_t)next_idx);

        /* Create substring */
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           data + start, line_end - start);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Take stream: pull from source, decrement remaining count --- */
    if (stream->kind == STREAM_KIND_TAKE) {
        int32_t remaining = jacl_as_i32(stream->args[1]);

        if (remaining <= 0) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        JaclVal source = stream->args[0];
        JaclVal elem;
        StreamPullResult pr = vm__pull_stream_one(vm, source, &elem);
        if (pr == STREAM_PULL_EXHAUSTED) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }
        if (pr == STREAM_PULL_ERROR) return STREAM_PULL_ERROR;

        stream->args[1] = jacl_i32(remaining - 1);
        *out_value = elem;
        return STREAM_PULL_VALUE;
    }

    /* --- Exec stream (US-004/US-005): read stdout from spawned process --- */
    if (stream->kind == STREAM_KIND_EXEC) {
        FILE* fp = (FILE*)(uintptr_t)stream->args[0];
        if (!fp) {
            /* Already exhausted - check if we stored an error value */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
                *out_value = stream->cached_value;
                stream->cached_value = JACL_NIL;
                return STREAM_PULL_VALUE;  /* Return the error value */
            }
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Read a line from the process output */
        char line_buf[4096];
        if (fgets(line_buf, sizeof(line_buf), fp) == NULL) {
            /* EOF or error - close the pipe and check exit status */
            int pclose_status = pclose(fp);
            stream->args[0] = (JaclVal)0;  /* clear FILE* */

            /* US-005: Check exit code and handle errors */
            int exit_code = WIFEXITED(pclose_status) ? WEXITSTATUS(pclose_status) : 1;

            /* Read and clean up stderr temp file */
            char stderr_path[64];
            if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
                uint32_t path_len = jacl_string_byte_len(stream->args[2]);
                jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
                stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
            } else {
                stderr_path[0] = '\0';
            }

            /* US-007: Clean up stdin temp file if present */
            if (stream->arg_count >= 4 && jacl_is_string(stream->args[3])) {
                char stdin_path[64];
                uint32_t path_len = jacl_string_byte_len(stream->args[3]);
                jacl_string_data(stream->args[3], stdin_path, sizeof(stdin_path));
                stdin_path[path_len < sizeof(stdin_path) - 1 ? path_len : sizeof(stdin_path) - 1] = '\0';
                unlink(stdin_path);
            }

            if (exit_code != 0) {
                /* Read stderr content */
                char stderr_buf[4096] = "";
                if (stderr_path[0] != '\0') {
                    FILE* stderr_fp = fopen(stderr_path, "r");
                    if (stderr_fp) {
                        size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                        stderr_buf[nread] = '\0';
                        fclose(stderr_fp);
                    }
                    unlink(stderr_path);  /* clean up temp file */
                }

                /* Create error value with stderr as message */
                gc__current_heap = &vm->heap;
                JaclVal err_msg;
                if (stderr_buf[0] != '\0') {
                    /* Trim trailing newline */
                    size_t len = strlen(stderr_buf);
                    while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                        stderr_buf[--len] = '\0';
                    }
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              stderr_buf, (uint32_t)len);
                } else {
                    /* Default message if no stderr */
                    char default_msg[64];
                    snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              default_msg, (uint32_t)strlen(default_msg));
                }

                /* Return error value */
                stream->state = STREAM_ERROR;
                stream->cached_value = JACL_NIL;
                *out_value = jacl_set_error(err_msg);
                return STREAM_PULL_VALUE;  /* Return error as the final value */
            }

            /* Success - clean up stderr temp file */
            if (stderr_path[0] != '\0') {
                unlink(stderr_path);
            }

            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Create string from line (keep newline for now) */
        uint32_t line_len = (uint32_t)strlen(line_buf);
        gc__current_heap = &vm->heap;
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           line_buf, line_len);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Exec pipe stream (US-009): read stdout from pipeline of processes --- */
    if (stream->kind == STREAM_KIND_EXEC_PIPE) {
        FILE* fp = (FILE*)(uintptr_t)stream->args[0];
        if (!fp) {
            /* Already exhausted - check if we stored an error value */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
                *out_value = stream->cached_value;
                stream->cached_value = JACL_NIL;
                return STREAM_PULL_VALUE;
            }
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Read a line from the pipeline output */
        char line_buf[4096];
        if (fgets(line_buf, sizeof(line_buf), fp) == NULL) {
            /* EOF - close file and wait for all children */
            fclose(fp);
            stream->args[0] = (JaclVal)0;

            /* Parse PIDs from args[1]: "count,pid1,pid2,..." */
            int exit_code = 0;
            if (jacl_is_string(stream->args[1])) {
                char pid_buf[256];
                uint32_t pid_len = jacl_string_byte_len(stream->args[1]);
                jacl_string_data(stream->args[1], pid_buf, sizeof(pid_buf));
                pid_buf[pid_len < sizeof(pid_buf) - 1 ? pid_len : sizeof(pid_buf) - 1] = '\0';

                /* Parse count and PIDs */
                char* p = pid_buf;
                int cmd_count = (int)strtol(p, &p, 10);
                pid_t* pids = (pid_t*)arena_alloc(vm->arena, cmd_count * sizeof(pid_t));
                for (int i = 0; i < cmd_count && *p == ','; i++) {
                    p++;
                    pids[i] = (pid_t)strtol(p, &p, 10);
                }

                /* Wait for all children, get exit status of last */
                for (int i = 0; i < cmd_count; i++) {
                    int status;
                    waitpid(pids[i], &status, 0);
                    if (i == cmd_count - 1) {
                        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                    }
                }
            }

            /* Read and clean up stderr temp file */
            char stderr_path[64];
            if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
                uint32_t path_len = jacl_string_byte_len(stream->args[2]);
                jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
                stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
            } else {
                stderr_path[0] = '\0';
            }

            if (exit_code != 0) {
                /* Read stderr content */
                char stderr_buf[4096] = "";
                if (stderr_path[0] != '\0') {
                    FILE* stderr_fp = fopen(stderr_path, "r");
                    if (stderr_fp) {
                        size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                        stderr_buf[nread] = '\0';
                        fclose(stderr_fp);
                    }
                    unlink(stderr_path);
                }

                /* Create error value */
                gc__current_heap = &vm->heap;
                JaclVal err_msg;
                if (stderr_buf[0] != '\0') {
                    size_t len = strlen(stderr_buf);
                    while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                        stderr_buf[--len] = '\0';
                    }
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              stderr_buf, (uint32_t)len);
                } else {
                    char default_msg[64];
                    snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
                    err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                              default_msg, (uint32_t)strlen(default_msg));
                }

                stream->state = STREAM_ERROR;
                stream->cached_value = JACL_NIL;
                *out_value = jacl_set_error(err_msg);
                return STREAM_PULL_VALUE;
            }

            /* Success - clean up stderr temp file */
            if (stderr_path[0] != '\0') {
                unlink(stderr_path);
            }

            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Create string from line */
        uint32_t line_len = (uint32_t)strlen(line_buf);
        gc__current_heap = &vm->heap;
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           line_buf, line_len);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Exec buffer stream (US-006): yield lines from pre-collected stdout --- */
    if (stream->kind == STREAM_KIND_EXEC_BUFFER) {
        JaclVal src_str = stream->args[0];
        int32_t cur_idx = jacl_as_i32(stream->args[1]);
        uint32_t byte_len = jacl_string_byte_len(src_str);

        if ((uint32_t)cur_idx >= byte_len) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Copy string data to scan for newlines */
        char sbuf[4096];
        char* data = sbuf;
        if (byte_len > sizeof(sbuf)) {
            data = (char*)arena_alloc(vm->arena, byte_len);
        }
        jacl_string_data(src_str, data, byte_len);

        /* Find end of current line */
        uint32_t start = (uint32_t)cur_idx;
        uint32_t end = start;
        while (end < byte_len && data[end] != '\n') {
            end++;
        }
        /* Include the newline in the line (like STREAM_KIND_EXEC does) */
        if (end < byte_len) {
            end++;  /* include the \n */
        }
        uint32_t next_idx = end;

        /* Check for end of content */
        if (start >= byte_len || (start == end && next_idx >= byte_len)) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        stream->args[1] = jacl_i32((int32_t)next_idx);

        /* Create substring (includes newline) */
        gc__current_heap = &vm->heap;
        JaclVal line_str = jacl_string_new(&vm->heap, vm->intern_table,
                                           data + start, end - start);
        *out_value = line_str;
        return STREAM_PULL_VALUE;
    }

    /* --- Range stream: yield integers from start to end --- */
    if (stream->kind == STREAM_KIND_RANGE) {
        int64_t current   = jacl_as_i64(stream->args[0]);
        int64_t end_bound = jacl_as_i64(stream->args[1]);
        int32_t inclusive  = jacl_as_i32(stream->args[2]);

        bool in_range = inclusive ? (current <= end_bound) : (current < end_bound);
        if (!in_range) {
            stream->state = STREAM_EXHAUSTED;
            *out_value = JACL_NIL;
            return STREAM_PULL_EXHAUSTED;
        }

        /* Advance to next value */
        gc__current_heap = &vm->heap;
        stream->args[0] = jacl_i64(&vm->heap, current + 1);

        /* Return current as i64 (or i32 if it fits) */
        if (current >= INT32_MIN && current <= INT32_MAX) {
            *out_value = jacl_i32((int32_t)current);
        } else {
            *out_value = jacl_i64(&vm->heap, current);
        }
        return STREAM_PULL_VALUE;
    }

    /* --- Generator stream (state machine) --- */
    uint32_t caller_stack_top   = vm->stack_top;
    uint32_t caller_frame_count = vm->frame_count;
    uint8_t* caller_ip          = vm->ip;
    BytecodeChunk* caller_chunk = vm->chunk;
    VMResult r;

    /* State machine generator: call sm_closure(state_obj, nil) */
    JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
    JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);

    r = vm__push(vm, sm->sm_closure);
    if (r != VM_OK) return STREAM_PULL_ERROR;
    r = vm__push(vm, stream->state_machine);
    if (r != VM_OK) return STREAM_PULL_ERROR;
    r = vm__push(vm, JACL_NIL);
    if (r != VM_OK) return STREAM_PULL_ERROR;

    if (vm->frame_count >= VM_FRAMES_MAX) {
        vm__set_error(vm, "stack overflow");
        return STREAM_PULL_ERROR;
    }
    CallFrame* nf = &vm->frames[vm->frame_count++];
    nf->closure    = sm_cl;
    nf->return_ip  = NULL;
    nf->stack_base = vm->stack_top - 2;
    nf->chunk      = &sm_cl->chunk;
    vm->ip    = sm_cl->chunk.code;
    vm->chunk = &sm_cl->chunk;

    VMResult inner = vm__run(vm, caller_frame_count);

    if (inner == VM_YIELD) {
        stream->state        = STREAM_CONSUMED;
        stream->cached_value = vm->yield_value;
        vm->stack_top   = caller_stack_top;
        vm->frame_count = caller_frame_count;
        vm->ip          = caller_ip;
        vm->chunk       = caller_chunk;
        *out_value = vm->yield_value;
        return STREAM_PULL_VALUE;
    } else if (inner == VM_OK) {
        /* Check if SM function returned an error value */
        if (vm->stack_top > caller_stack_top) {
            JaclVal sm_ret = vm->stack[vm->stack_top - 1];
            if (jacl_is_error(sm_ret)) {
                stream->state = STREAM_ERROR;
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip          = caller_ip;
                vm->chunk       = caller_chunk;
                return STREAM_PULL_ERROR;
            }
        }
        stream->state        = STREAM_EXHAUSTED;
        stream->cached_value = JACL_NIL;
        vm->stack_top   = caller_stack_top;
        vm->frame_count = caller_frame_count;
        vm->ip          = caller_ip;
        vm->chunk       = caller_chunk;
        *out_value = JACL_NIL;
        return STREAM_PULL_EXHAUSTED;
    } else {
        stream->state = STREAM_ERROR;
        return STREAM_PULL_ERROR;
    }
}

/* Helper: Build shell command string from args vector.
 * Returns pointer to cmd_buf on success, NULL on error (error set in vm).
 * cmd_buf must be at least 4096 bytes. */
static char* vm__exec_build_cmd(VM* vm, JaclVal args_vec, char* cmd_buf, size_t cmd_buf_size) {
  if (!jacl_is_vector(args_vec)) {
    vm__set_error(vm, "exec requires a vector of arguments, got %s",
                 vm__type_name(args_vec));
    return NULL;
  }

  jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(args_vec);
  uint32_t argc = jacl_vec_count(vec);
  if (argc == 0) {
    vm__set_error(vm, "exec requires at least a command name");
    return NULL;
  }

  char* p = cmd_buf;
  char* end = cmd_buf + cmd_buf_size - 1;

  for (uint32_t i = 0; i < argc && p < end; i++) {
    jacl_vec_get_result gr = jacl_vec_get(vec, i);
    JaclVal arg_val = gr.value;
    if (!jacl_is_string(arg_val)) {
      vm__set_error(vm, "exec argument %d must be a string, got %s",
                   (int)i, vm__type_name(arg_val));
      return NULL;
    }
    uint32_t arg_len = jacl_string_byte_len(arg_val);
    char arg_buf[1024];
    if (arg_len >= sizeof(arg_buf)) arg_len = sizeof(arg_buf) - 1;
    jacl_string_data(arg_val, arg_buf, arg_len + 1);
    arg_buf[arg_len] = '\0';

    if (i > 0 && p < end) *p++ = ' ';

    /* Simple quoting: if the arg contains spaces or special chars, quote it */
    int needs_quote = 0;
    for (uint32_t j = 0; j < arg_len; j++) {
      char c = arg_buf[j];
      if (c == ' ' || c == '\t' || c == '"' || c == '\'' ||
          c == '\\' || c == '$' || c == '`' || c == '!' ||
          c == '*' || c == '?' || c == '[' || c == ']' ||
          c == '(' || c == ')' || c == '{' || c == '}' ||
          c == '<' || c == '>' || c == '|' || c == '&' ||
          c == ';' || c == '\n') {
        needs_quote = 1;
        break;
      }
    }

    if (needs_quote) {
      if (p < end) *p++ = '\'';
      for (uint32_t j = 0; j < arg_len && p < end; j++) {
        char c = arg_buf[j];
        if (c == '\'') {
          if (p + 4 <= end) {
            *p++ = '\'';
            *p++ = '\\';
            *p++ = '\'';
            *p++ = '\'';
          }
        } else {
          *p++ = c;
        }
      }
      if (p < end) *p++ = '\'';
    } else {
      for (uint32_t j = 0; j < arg_len && p < end; j++) {
        *p++ = arg_buf[j];
      }
    }
  }
  *p = '\0';
  return cmd_buf;
}

/* Helper: Collect stdin value (string or stream) into a buffer.
 * Returns 0 on success, -1 on error (pushed error to stack for stream errors).
 * On success, *out_buf and *out_len are set. */
static int vm__exec_collect_stdin(VM* vm, JaclVal stdin_val, char** out_buf, size_t* out_len) {
  size_t stdin_cap = 4096;
  size_t stdin_len = 0;
  char* stdin_buf = (char*)arena_alloc(vm->arena, stdin_cap);

  if (jacl_is_string(stdin_val)) {
    stdin_len = jacl_string_byte_len(stdin_val);
    if (stdin_len >= stdin_cap) {
      stdin_cap = stdin_len + 1;
      stdin_buf = (char*)arena_alloc(vm->arena, stdin_cap);
    }
    jacl_string_data(stdin_val, stdin_buf, (uint32_t)(stdin_cap));
  } else if (jacl_is_stream(stdin_val)) {
    JaclStream* src_stream = jacl_as_stream(stdin_val);

    while (src_stream->state != STREAM_EXHAUSTED) {
      JaclVal elem;
      StreamPullResult pr = vm__pull_stream_one(vm, stdin_val, &elem);
      if (pr == STREAM_PULL_ERROR) return -1;
      if (pr == STREAM_PULL_EXHAUSTED) break;

      /* Handle error values from upstream */
      if (jacl_is_error(elem)) {
        vm__push(vm, elem);
        return -2; /* special: error value pushed to stack */
      }

      /* Convert element to string */
      const char* elem_data = NULL;
      uint32_t elem_len = 0;
      char numbuf[32];

      if (jacl_is_string(elem)) {
        elem_len = jacl_string_byte_len(elem);
        char* tmp = (char*)arena_alloc(vm->arena, elem_len + 1);
        jacl_string_data(elem, tmp, elem_len + 1);
        elem_data = tmp;
      } else if (jacl_is_i32(elem)) {
        snprintf(numbuf, sizeof(numbuf), "%d", jacl_as_i32(elem));
        elem_data = numbuf;
        elem_len = (uint32_t)strlen(numbuf);
      } else if (jacl_is_f64(elem)) {
        snprintf(numbuf, sizeof(numbuf), "%g", jacl_as_f64(elem));
        elem_data = numbuf;
        elem_len = (uint32_t)strlen(numbuf);
      } else if (jacl_is_nil(elem)) {
        continue;
      } else {
        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_value(&fmt, elem);
        elem_data = fmt.data;
        elem_len = fmt.len;
      }

      int needs_newline = (elem_len == 0 || elem_data[elem_len - 1] != '\n');

      while (stdin_len + elem_len + 2 >= stdin_cap) {
        size_t new_cap = stdin_cap * 2;
        char* new_buf = (char*)arena_alloc(vm->arena, new_cap);
        memcpy(new_buf, stdin_buf, stdin_len);
        stdin_buf = new_buf;
        stdin_cap = new_cap;
      }

      memcpy(stdin_buf + stdin_len, elem_data, elem_len);
      stdin_len += elem_len;

      if (needs_newline) {
        stdin_buf[stdin_len++] = '\n';
      }
    }
  } else {
    vm__set_error(vm, "exec stdin must be a string or stream, got %s",
                 vm__type_name(stdin_val));
    return -1;
  }
  stdin_buf[stdin_len] = '\0';
  *out_buf = stdin_buf;
  *out_len = stdin_len;
  return 0;
}

/**
 * Inner dispatch loop. Runs until OP_HALT or until frame_count drops
 * to min_frame (used by OP_EACH to execute closures inline).
 */
VMResult vm__run(VM* vm, uint32_t min_frame) {
  CallFrame* frame = &vm->frames[vm->frame_count - 1];

  for (;;) {
    /* GC safepoint: collect if threshold exceeded */
    if (vm->heap.needs_gc) {
      if (!vm->runtime) {
        /* Single-threaded: choose minor or major GC */
        if (gc_should_major(&vm->heap)) {
          gc_collect(&vm->heap, vm);
        } else {
          gc_collect_minor(&vm->heap, vm, vm->remembered_set);
        }
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
          val = MREF_VAL(ref);
        }

        char buf[256];
        const char* text;
        uint32_t len;

        if (jacl_is_error(val)) {
          /* Format as <error: PAYLOAD> using the format buffer */
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          vm__fmt_append(&fmt, "<error: ", 8);
          JaclVal payload = jacl_clear_error(val);
          /* Print payload: strings without quotes, other types with fmt_value */
          if (jacl_is_string(payload)) {
            uint32_t slen = jacl_string_byte_len(payload);
            if (jacl_is_heap_string(payload)) {
              JaclHeapString* hs = jacl_as_heap_string(payload);
              vm__fmt_append(&fmt, hs->data, hs->byte_len);
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
          uint32_t slen = jacl_string_byte_len(val);
          if (slen + 1 <= sizeof(buf)) {
            jacl_string_data(val, buf, slen);
            buf[slen] = '\n';
            text = buf;
            len = slen + 1;
          } else {
            /* String too long for stack buffer: print data then newline */
            if (jacl_is_heap_string(val)) {
              JaclHeapString* hs = jacl_as_heap_string(val);
              vm->print_fn(hs->data, hs->byte_len, vm->print_ctx);
            } else if (jacl_is_rope_string(val)) {
              /* Stream rope content leaf-by-leaf via cursor */
              JaclRopeString* rs = jacl_as_rope_string(val);
              rope_cursor cur = rope_cursor_new(rs->r, 0);
              size_t remaining = slen;
              while (remaining > 0) {
                size_t chunk = remaining < sizeof(buf) ? remaining : sizeof(buf);
                size_t got = rope_cursor_read(&cur, (uint8_t*)buf, chunk);
                if (got == 0) break;
                vm->print_fn(buf, (uint32_t)got, vm->print_ctx);
                rope_cursor_advance_bytes(&cur, got);
                remaining -= got;
              }
              rope_cursor_free(cur);
            } else {
              /* Inline string (max 7 bytes, should never reach here) */
              jacl_string_data(val, buf, sizeof(buf));
              vm->print_fn(buf, slen, vm->print_ctx);
            }
            vm->print_fn("\n", 1, vm->print_ctx);
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
            break;
          }
        } else if (jacl_is_struct(val)) {
          HeapRecord* s = jacl_as_heap_record_ptr(val);
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          if (vm->struct_registry && s->type_idx < vm->struct_registry->count) {
            StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];
            vm__fmt_struct_bytes(&fmt, sdef, s->data);
          } else {
            vm__fmt_append(&fmt, "<struct>", 8);
          }
          vm__fmt_append(&fmt, "\n", 1);
          vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          break;
        } else if (jacl_is_vector(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val) || jacl_is_future(val) || jacl_is_stream(val) || jacl_is_typed_vector(val) || jacl_is_typed_map(val)) {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
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
          char name_buf[130];
          uint32_t nlen = jacl_string_data(name, name_buf, sizeof(name_buf) - 1);
          if (nlen >= sizeof(name_buf)) nlen = sizeof(name_buf) - 1;
          name_buf[nlen] = '\0';
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

        if (jacl_is_native_fn(callee)) {
          uint32_t fn_idx = jacl_as_native_fn_index(callee);
          if (fn_idx >= vm->native_fn_count || !vm->call_native) {
            vm__set_error(vm, "invalid native function index %u", fn_idx);
            return VM_RUNTIME_ERROR;
          }
          int8_t arity = vm->native_fn_arities[fn_idx];
          if (arity >= 0 && arg_count != (uint8_t)arity) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)arity, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal* args = &vm->stack[vm->stack_top - arg_count];
          JaclVal ret = vm->call_native(vm->native_fn_ctx, fn_idx,
                                         args, (int)arg_count);
          vm->stack_top -= (arg_count + 1); /* pop args + callee */
          result = vm__push(vm, ret);
          if (result != VM_OK) return result;
          break;
        }

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "cannot call %s value", vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(callee);

        /* Generator: calling a generator proc creates a stream instead of executing.
           Check this BEFORE arity check because generator has hidden __k/SM params. */
        if (closure->is_generator) {
          /* Arity check against min_args (excludes hidden __k/SM params) */
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->next_fn = callee;

          if (closure->is_sm_compiled) {
            /* State machine generator: allocate SM object, copy args into fields */
            JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
            JaclStateMachine* sm = jacl_as_state_machine(sm_val);
            sm->sm_closure = callee;
            for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
              sm->fields[i] = vm->stack[vm->stack_top - arg_count + i];
            }
            stream->state_machine = sm_val;
            stream->arg_count = 0;
          } else {
            /* CPS generator: save args for deferred first-call */
            stream->arg_count = (uint8_t)(arg_count <= STREAM_MAX_ARGS ? arg_count : STREAM_MAX_ARGS);
            for (uint8_t i = 0; i < stream->arg_count; i++) {
              stream->args[i] = vm->stack[vm->stack_top - arg_count + i];
            }
          }

          vm->stack_top -= (arg_count + 1); /* pop args + callee */
          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          break;
        }

        /* SM-compiled non-generator: transparently wrap user args into SM */
        if (closure->is_sm_compiled && !closure->is_generator) {
          /* Arity check against min_args (user's parameter count) */
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          /* Allocate state machine and copy user args into fields */
          JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
          JaclStateMachine* sm = jacl_as_state_machine(sm_val);
          sm->sm_closure = callee;
          for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
            sm->fields[i] = vm->stack[vm->stack_top - arg_count + i];
          }
          /* Propagate error_k from caller SM to inner SM so that the
             completion callback (resolve_k, parallel_k, race_k) is
             preserved across nested SM-to-SM calls.  This ensures
             the future/aggregator is resolved when the innermost SM
             in the call chain completes. */
          if (frame->closure->is_sm_compiled) {
            JaclVal outer_sm_val = vm->stack[frame->stack_base + 0];
            if (jacl_is_state_machine(outer_sm_val)) {
              JaclStateMachine *outer_sm = jacl_as_state_machine(outer_sm_val);
              sm->error_k = outer_sm->error_k;
            }
          }
          /* Replace stack args with (sm_val, JACL_NIL) */
          uint32_t callee_pos = vm->stack_top - arg_count - 1;
          vm->stack[callee_pos + 1] = sm_val;
          vm->stack[callee_pos + 2] = JACL_NIL;
          vm->stack_top = callee_pos + 3;
          arg_count = 2;
        }

        if (closure->variadic) {
          if (arg_count < closure->min_args) {
            vm__set_error(vm, "expected at least %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
        } else if (arg_count != closure->param_total_slots) {
          /* Phase 5a: arity check uses param_total_slots (includes multi-slot struct params) */
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

      case OP_TAIL_CALL: {
        uint8_t arg_count = vm__read_byte(vm);
        JaclVal callee = vm->stack[vm->stack_top - arg_count - 1];

        if (jacl_is_native_fn(callee)) {
          /* Native functions can't be tail-called; degrade to regular call */
          uint32_t fn_idx = jacl_as_native_fn_index(callee);
          if (fn_idx >= vm->native_fn_count || !vm->call_native) {
            vm__set_error(vm, "invalid native function index %u", fn_idx);
            return VM_RUNTIME_ERROR;
          }
          int8_t arity = vm->native_fn_arities[fn_idx];
          if (arity >= 0 && arg_count != (uint8_t)arity) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)arity, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal* args = &vm->stack[vm->stack_top - arg_count];
          JaclVal ret = vm->call_native(vm->native_fn_ctx, fn_idx,
                                         args, (int)arg_count);
          vm->stack_top -= (arg_count + 1);
          result = vm__push(vm, ret);
          if (result != VM_OK) return result;
          break;
        }

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "cannot call %s value", vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(callee);

        /* SM-compiled non-generator: transparently wrap user args into SM */
        if (closure->is_sm_compiled && !closure->is_generator) {
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
          JaclStateMachine* sm = jacl_as_state_machine(sm_val);
          sm->sm_closure = callee;
          for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
            sm->fields[i] = vm->stack[vm->stack_top - arg_count + i];
          }
          uint32_t callee_pos = vm->stack_top - arg_count - 1;
          vm->stack[callee_pos + 1] = sm_val;
          vm->stack[callee_pos + 2] = JACL_NIL;
          vm->stack_top = callee_pos + 3;
          arg_count = 2;
        }

        if (closure->variadic) {
          if (arg_count < closure->min_args) {
            vm__set_error(vm, "expected at least %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }
        } else if (arg_count != closure->param_total_slots) {
          /* Phase 5a: arity check uses param_total_slots */
          vm__set_error(vm, "expected %d arguments but got %d",
                       (int)closure->param_count, (int)arg_count);
          return VM_RUNTIME_ERROR;
        }

        /* Slide callee + args down to reuse current frame's stack region.
           stack_base - 1 is where the caller's closure sits. */
        uint32_t src = vm->stack_top - arg_count - 1; /* callee position */
        uint32_t dst = frame->stack_base - 1;         /* overwrite current frame's closure */
        /* US-014: clear stale bitmap bits for the old frame's entire range
           before overwriting with new args (which are regular JaclVals). */
        for (uint32_t si = dst; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }
        for (uint32_t i = 0; i <= (uint32_t)arg_count; i++) {
          vm->stack[dst + i] = vm->stack[src + i];
        }
        vm->stack_top = dst + 1 + arg_count;

        /* Reuse current frame — no frame_count change */
        frame->closure    = closure;
        frame->stack_base = dst + 1;
        frame->chunk      = &closure->chunk;

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

        /* US-014: clear inline struct bitmap bits for the callee's entire frame range */
        for (uint32_t si = callee_base; si < vm->stack_top + 1; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }

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

      case OP_RETURN_WIDE: {
        uint8_t width = vm__read_byte(vm);

        /* Read return slots from callee stack top */
        uint32_t ret_base = vm->stack_top - width;

        uint32_t callee_base = frame->stack_base;
        uint8_t* caller_ip   = frame->return_ip;

        /* Clear inline struct bitmap bits for the callee's entire frame range */
        for (uint32_t si = callee_base; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }

        vm->frame_count--;

        if (vm->frame_count == 0) {
          memmove(&vm->stack[0], &vm->stack[ret_base], width * sizeof(JaclVal));
          for (uint32_t si = 0; si < width; si++)
            BITMAP_SET(vm->inline_slot_bitmap, si);
          vm->stack_top = width;
          return VM_OK;
        }

        /* Copy return slots to where the callee's closure was */
        uint32_t dst = callee_base - 1;
        memmove(&vm->stack[dst], &vm->stack[ret_base], width * sizeof(JaclVal));
        for (uint32_t si = dst; si < dst + width; si++)
          BITMAP_SET(vm->inline_slot_bitmap, si);
        vm->stack_top = dst + width;

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

        /* US-008: allocate using upvalue_total_slots to support wide struct upvalues */
        uint16_t total_slots = template->upvalue_total_slots;
        if (total_slots == 0) total_slots = template->upvalue_count; /* fallback for pre-US008 */
        size_t uv_bytes = sizeof(JaclVal) * total_slots;
        JaclClosure* cl = (JaclClosure*)gc_alloc(&vm->heap, OBJ_CLOSURE,
                              sizeof(JaclClosure) + uv_bytes);
        cl->chunk        = template->chunk;
        cl->param_count  = template->param_count;
        cl->param_total_slots = template->param_total_slots;
        cl->has_inline_params = template->has_inline_params;
        cl->param_names  = template->param_names;
        cl->name         = template->name;
        cl->upvalue_count = template->upvalue_count;
        cl->upvalue_total_slots = template->upvalue_total_slots;
        cl->min_args     = template->min_args;
        cl->variadic     = template->variadic;
        cl->pinned       = template->pinned;
        cl->is_generator  = template->is_generator;
        cl->is_sm_compiled = template->is_sm_compiled;
        cl->sm_field_count = template->sm_field_count;
        /* US-014: initialize upvalue inline bitmap */
        memset(cl->upvalue_inline_bitmap, 0, sizeof(cl->upvalue_inline_bitmap));
        /* All pinned closures run on thread 0 (the main worker thread).
           This ensures all non-local mutable state reads and writes go
           through a single worker, avoiding per-worker env isolation issues. */
        cl->pin_worker_id = template->pinned ? 0 : -1;

        /* Root cl on the VM stack before the upvalue loop.  If a future
           upvalue path calls gc_alloc, emergency GC could sweep an
           unrooted closure.  Rooting early is defensive. */
        result = vm__push(vm, jacl_closure(cl));
        if (result != VM_OK) return result;

        if (cl->upvalue_count > 0) {
          cl->upvalues = (JaclVal*)(cl + 1); /* trailing array */
          /* US-008: zero-fill the upvalue array for wide struct upvalues */
          memset(cl->upvalues, 0, uv_bytes);
          uint16_t uv_slot = 0; /* current write position in upvalue array */
          for (uint8_t i = 0; i < cl->upvalue_count; i++) {
            uint8_t is_local = vm__read_byte(vm);
            uint8_t uv_index = vm__read_byte(vm);
            uint8_t width    = vm__read_byte(vm);
            if (width == 0) width = 1; /* safety fallback */
            if (is_local == 1) {
              /* US-008: copy width slots from enclosing local stack */
              for (uint8_t w = 0; w < width; w++) {
                uint32_t src = frame->stack_base + uv_index + w;
                if (src >= vm->stack_top - 1) {
                  vm__set_error(vm,
                    "OP_CLOSURE: local upvalue index %d+%d out of bounds "
                    "(frame stack_base=%u, stack_top=%u)",
                    uv_index, w, frame->stack_base, vm->stack_top);
                  return VM_RUNTIME_ERROR;
                }
                cl->upvalues[uv_slot + w] = vm->stack[src];
              }
              /* US-014: propagate bitmap from stack to closure upvalues */
              if (width > 1) {
                for (uint8_t w = 0; w < width; w++) {
                  uint32_t src = frame->stack_base + uv_index + w;
                  if (BITMAP_GET(vm->inline_slot_bitmap, src)) {
                    BITMAP_SET(cl->upvalue_inline_bitmap, uv_slot + w);
                  }
                }
              }
            } else if (is_local == 2) {
              /* SM state field upvalue: read from the state machine object
                 at slot 0 of the current frame. uv_index is the field index.
                 For mutable fields the SM field already holds a cell pointer,
                 so the closure gets shared identity (FR-5 semantics). */
              JaclVal sm_val = vm->stack[frame->stack_base + 0];
              JaclStateMachine* sm = jacl_as_state_machine(sm_val);
              if (uv_index >= sm->field_count) {
                vm__set_error(vm,
                  "OP_CLOSURE: SM field upvalue index %d out of bounds "
                  "(SM has %d fields)",
                  uv_index, sm->field_count);
                return VM_RUNTIME_ERROR;
              }
              cl->upvalues[uv_slot] = sm->fields[uv_index];
              /* US-014: SM field upvalue copies 1 slot only — propagate
                 bitmap from SM, not from stack width */
              if (BITMAP_GET(sm->field_inline_bitmap, uv_index)) {
                BITMAP_SET(cl->upvalue_inline_bitmap, uv_slot);
              }
            } else {
              /* US-008: copy width slots from parent closure upvalues */
              for (uint8_t w = 0; w < width; w++) {
                uint16_t parent_total = frame->closure->upvalue_total_slots
                  ? frame->closure->upvalue_total_slots
                  : frame->closure->upvalue_count;
              if (uv_index + w >= parent_total) {
                  vm__set_error(vm,
                    "OP_CLOSURE: upvalue index %d+%d out of bounds "
                    "(parent has %d upvalue slots)",
                    uv_index, w, frame->closure->upvalue_total_slots);
                  return VM_RUNTIME_ERROR;
                }
                cl->upvalues[uv_slot + w] = frame->closure->upvalues[uv_index + w];
              }
              /* US-014: propagate bitmap from parent closure upvalues */
              if (width > 1) {
                for (uint8_t w = 0; w < width; w++) {
                  if (BITMAP_GET(frame->closure->upvalue_inline_bitmap, uv_index + w)) {
                    BITMAP_SET(cl->upvalue_inline_bitmap, uv_slot + w);
                  }
                }
              }
            }
            uv_slot += width;
          }
        } else {
          cl->upvalues = NULL;
        }

        /* cl is already on the stack from the root push above */
        break;
      }

      case OP_POP_N: {
        uint8_t count = vm__read_byte(vm);
        if (vm->stack_top < count) {
          vm__set_error(vm, "stack underflow");
          return VM_RUNTIME_ERROR;
        }
        /* US-014: clear inline struct bitmap bits for popped slots */
        for (uint32_t si = vm->stack_top - count; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }
        vm->stack_top -= count;
        break;
      }

      case OP_CLOSE_LOOP: {
        /* Pop N values under the top-of-stack value.
           Used by break inside for-loops to clean up hidden locals
           while preserving the break value on top. */
        uint8_t count = vm__read_byte(vm);
        if (count > 0) {
          JaclVal top = vm->stack[vm->stack_top - 1];
          /* US-014: save top's bitmap state, clear removed range, restore */
          uint8_t top_is_inline = BITMAP_GET(vm->inline_slot_bitmap, vm->stack_top - 1);
          for (uint32_t si = vm->stack_top - 1 - count; si < vm->stack_top; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, si);
          }
          vm->stack_top -= count;
          vm->stack[vm->stack_top - 1] = top;
          if (top_is_inline) {
            BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top - 1);
          }
        }
        break;
      }

      case OP_DESTRUCTURE_VEC: {
        uint8_t n = vm__read_byte(vm);
        uint8_t skip_mask = vm__read_byte(vm);
        JaclVal vec_val;
        result = vm__pop(vm, &vec_val);
        if (result != VM_OK) return result;
        if (!jacl_is_vector(vec_val)) {
          vm__set_error(vm,
              "destructuring requires a vector, got %s",
              vm__type_name(vec_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        uint32_t vec_len = jacl_vec_count(vec);
        if (vec_len != n) {
          vm__set_error(vm,
              "destructuring length mismatch: expected %u elements, got %u",
              (unsigned)n, (unsigned)vec_len);
          return VM_RUNTIME_ERROR;
        }
        /* Push elements 0..N-1 onto stack, skipping positions in skip_mask */
        for (uint8_t i = 0; i < n; i++) {
          if (skip_mask & (1u << i)) continue; /* wildcard position */
          jacl_vec_get_result gr = jacl_vec_get(vec, (uint32_t)i);
          result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_DESTRUCTURE_VEC_REST: {
        uint8_t n = vm__read_byte(vm);
        JaclVal vec_val;
        result = vm__pop(vm, &vec_val);
        if (result != VM_OK) return result;
        if (!jacl_is_vector(vec_val)) {
          vm__set_error(vm,
              "destructuring requires a vector, got %s",
              vm__type_name(vec_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(vec_val);
        uint32_t vec_len = jacl_vec_count(vec);
        if (vec_len < n) {
          vm__set_error(vm,
              "destructuring rest: expected at least %u elements, got %u",
              (unsigned)n, (unsigned)vec_len);
          return VM_RUNTIME_ERROR;
        }
        /* Push first N elements individually */
        for (uint8_t i = 0; i < n; i++) {
          jacl_vec_get_result gr = jacl_vec_get(vec, (uint32_t)i);
          result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
          if (result != VM_OK) return result;
        }
        /* Collect remaining elements into a new vector */
        gc__current_heap = &vm->heap;
        jacl_vec_root* rest = jacl_vec_empty();
        for (uint32_t i = n; i < vec_len; i++) {
          jacl_vec_get_result gr = jacl_vec_get(vec, i);
          rest = jacl_vec_push_back(rest, gr.found ? gr.value : JACL_NIL);
        }
        result = vm__push(vm, jacl_vector_ptr(rest));
        if (result != VM_OK) return result;
        break;
      }

      case OP_DESTRUCTURE_NAMED: {
        uint8_t n = vm__read_byte(vm);
        /* Read N constant indices for field names */
        uint16_t name_indices[256];
        for (uint8_t i = 0; i < n; i++) {
          name_indices[i] = vm__read_u16(vm);
        }
        JaclVal src_val;
        result = vm__pop(vm, &src_val);
        if (result != VM_OK) return result;

        if (jacl_is_struct(src_val)) {
          HeapRecord* s = jacl_as_heap_record_ptr(src_val);
          StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];
          for (uint8_t i = 0; i < n; i++) {
            JaclVal name_val = frame->chunk->constants[name_indices[i]];
            char fname[64]; uint32_t flen;
            flen = jacl_string_data(name_val, fname, sizeof(fname));
            /* Find field by name */
            uint32_t fi;
            for (fi = 0; fi < sdef->field_count; fi++) {
              if (sdef->fields[fi].name_len == flen &&
                  memcmp(sdef->fields[fi].name, fname, flen) == 0) break;
            }
            if (fi == sdef->field_count) {
              vm__set_error(vm,
                  "destructuring: struct '%.*s' has no field '%.*s'",
                  (int)sdef->name_len, sdef->name, (int)flen, fname);
              return VM_RUNTIME_ERROR;
            }
            JaclVal field_val = vm__heap_record_read_field(&vm->heap, s,
                sdef->fields[fi].offset, sdef->fields[fi].type);
            result = vm__push(vm, field_val);
            if (result != VM_OK) return result;
          }
        } else if (jacl_is_map(src_val)) {
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(src_val);
          for (uint8_t i = 0; i < n; i++) {
            JaclVal key_val = frame->chunk->constants[name_indices[i]];
            if (!jacl_map_has(map, key_val)) {
              char fname[64]; uint32_t flen;
              flen = jacl_string_data(key_val, fname, sizeof(fname));
              vm__set_error(vm,
                  "destructuring: map has no key '%.*s'",
                  (int)flen, fname);
              return VM_RUNTIME_ERROR;
            }
            result = vm__push(vm, jacl_map_get(map, key_val));
            if (result != VM_OK) return result;
          }
        } else {
          vm__set_error(vm,
              "named destructuring requires a struct or map, got %s",
              vm__type_name(src_val));
          return VM_RUNTIME_ERROR;
        }
        break;
      }

      case OP_DESTRUCTURE_NAMED_REST: {
        uint8_t n = vm__read_byte(vm);
        /* Read N constant indices for explicit field names to extract */
        uint16_t name_indices[256];
        for (uint8_t i = 0; i < n; i++) {
          name_indices[i] = vm__read_u16(vm);
        }
        JaclVal src_val;
        result = vm__pop(vm, &src_val);
        if (result != VM_OK) return result;

        if (jacl_is_struct(src_val)) {
          HeapRecord* s = jacl_as_heap_record_ptr(src_val);
          StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];

          /* Push N explicit field values */
          for (uint8_t i = 0; i < n; i++) {
            JaclVal fname_val = frame->chunk->constants[name_indices[i]];
            char fname[64]; uint32_t flen;
            flen = jacl_string_data(fname_val, fname, sizeof(fname));
            uint32_t fi;
            for (fi = 0; fi < sdef->field_count; fi++) {
              if (sdef->fields[fi].name_len == flen &&
                  memcmp(sdef->fields[fi].name, fname, flen) == 0) break;
            }
            if (fi == sdef->field_count) {
              vm__set_error(vm,
                  "destructuring: struct '%.*s' has no field '%.*s'",
                  (int)sdef->name_len, sdef->name, (int)flen, fname);
              return VM_RUNTIME_ERROR;
            }
            JaclVal field_val = vm__heap_record_read_field(&vm->heap, s,
                sdef->fields[fi].offset, sdef->fields[fi].type);
            result = vm__push(vm, field_val);
            if (result != VM_OK) return result;
          }

          /* Build rest map from remaining fields */
          gc__current_heap = &vm->heap;
          jacl_map_node* rest_map = NULL;
          for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
            /* Check if this field is in the explicit list */
            int is_explicit = 0;
            for (uint8_t ei = 0; ei < n; ei++) {
              JaclVal ename_val = frame->chunk->constants[name_indices[ei]];
              char ename[64]; uint32_t elen;
              elen = jacl_string_data(ename_val, ename, sizeof(ename));
              if (sdef->fields[fi].name_len == elen &&
                  memcmp(sdef->fields[fi].name, ename, elen) == 0) {
                is_explicit = 1; break;
              }
            }
            if (!is_explicit) {
              JaclVal key = jacl_inline_string(sdef->fields[fi].name,
                                               sdef->fields[fi].name_len);
              JaclVal val = vm__heap_record_read_field(&vm->heap, s,
                  sdef->fields[fi].offset, sdef->fields[fi].type);
              rest_map = jacl_map_set(rest_map, key, val);
            }
          }
          result = vm__push(vm, jacl_map_ptr(rest_map));
          if (result != VM_OK) return result;

        } else if (jacl_is_map(src_val)) {
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(src_val);

          /* Push N explicit field values */
          for (uint8_t i = 0; i < n; i++) {
            JaclVal key_val = frame->chunk->constants[name_indices[i]];
            if (!jacl_map_has(map, key_val)) {
              char fname[64]; uint32_t flen;
              flen = jacl_string_data(key_val, fname, sizeof(fname));
              vm__set_error(vm,
                  "destructuring: map has no key '%.*s'",
                  (int)flen, fname);
              return VM_RUNTIME_ERROR;
            }
            result = vm__push(vm, jacl_map_get(map, key_val));
            if (result != VM_OK) return result;
          }

          /* Build rest map from remaining entries */
          gc__current_heap = &vm->heap;
          jacl_map_node* rest_map = NULL;
          jacl_map_iter it = jacl_map_iter_init(map);
          jacl_map_iter_result ir;
          do {
            ir = jacl_map_next_leaf(&it);
            if (!ir.item) break;
            JaclVal key = jacl_map_key_from_leaf(ir.item);
            /* Check if this key is in the explicit list */
            int is_explicit = 0;
            char kbuf[64]; uint32_t klen;
            klen = jacl_string_data(key, kbuf, sizeof(kbuf));
            for (uint8_t ei = 0; ei < n; ei++) {
              JaclVal ename_val = frame->chunk->constants[name_indices[ei]];
              char ebuf[64]; uint32_t elen;
              elen = jacl_string_data(ename_val, ebuf, sizeof(ebuf));
              if (klen == elen && memcmp(kbuf, ebuf, klen) == 0) {
                is_explicit = 1; break;
              }
            }
            if (!is_explicit) {
              JaclVal val = jacl_map_value_from_leaf(ir.item);
              rest_map = jacl_map_set(rest_map, key, val);
            }
          } while (ir.item);
          result = vm__push(vm, jacl_map_ptr(rest_map));
          if (result != VM_OK) return result;
        } else {
          vm__set_error(vm,
              "named destructuring requires a struct or map, got %s",
              vm__type_name(src_val));
          return VM_RUNTIME_ERROR;
        }
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

        uint32_t len_a = jacl_string_byte_len(a);
        uint32_t len_b = jacl_string_byte_len(b);
        uint32_t total = len_a + len_b;

        JaclVal res;
        if (total <= 128) {
          /* Small concat: extract bytes and route through jacl_string_new */
          char stack_buf[256];
          char* concat_buf = stack_buf;
          if (total > sizeof(stack_buf)) {
            concat_buf = (char*)arena_alloc(vm->arena, total);
          }
          jacl_string_data(a, concat_buf, len_a);
          jacl_string_data(b, concat_buf + len_a, len_b);
          /* Both inputs are already NFD, so concat is NFD — unicode_is_nfd
           * fast path will skip normalization inside jacl_string_new */
          res = jacl_string_new(&vm->heap, vm->intern_table, concat_buf, total);
        } else {
          /* Large concat: use rope_concat for O(log n) structural sharing */
          rope ra, rb;
          bool free_ra = false, free_rb = false;
          if (jacl_is_rope_string(a)) {
            ra = jacl_as_rope_string(a)->r;
            rope_ref(ra);
          } else {
            char bufa[128];
            jacl_string_data(a, bufa, len_a);
            ra = rope_from_str((const uint8_t*)bufa, len_a);
            free_ra = true;
          }
          if (jacl_is_rope_string(b)) {
            rb = jacl_as_rope_string(b)->r;
            rope_ref(rb);
          } else {
            char bufb[128];
            jacl_string_data(b, bufb, len_b);
            rb = rope_from_str((const uint8_t*)bufb, len_b);
            free_rb = true;
          }
          rope rc = rope_concat(ra, rb);
          uint32_t hash = rope_string__compute_hash(rc);
          JaclRopeString* rs = (JaclRopeString*)gc_alloc(
              &vm->heap, OBJ_ROPE_STRING, sizeof(JaclRopeString));
          rs->hash = hash;
          rs->r    = rc;
          res = jacl_rope_string_ptr(rs);
          /* Release our refs to operand ropes */
          rope_unref(ra);
          rope_unref(rb);
          (void)free_ra; (void)free_rb;
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

      case OP_STR_BYTE_LEN: {
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) { result = vm__push(vm, val); if (result != VM_OK) return result; break; }
        if (!jacl_is_string(val)) {
          vm__set_error(vm, "type error in 'byte-length': expected string, got %s",
                       vm__type_name(val));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, jacl_i32((int32_t)jacl_string_byte_len(val)));
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
        uint32_t glen = jacl_string_len(str_val);
        if (idx < 0 || (uint32_t)idx >= glen) {
          result = vm__push(vm, JACL_NIL);
        } else {
          if (jacl_is_rope_string(str_val)) {
            /* Rope: use rope_grapheme_to_byte for O(log n) seek */
            JaclRopeString* rs = jacl_as_rope_string(str_val);
            rope_offset_result start_res = rope_grapheme_to_byte(rs->r, (size_t)idx);
            rope_offset_result end_res = rope_grapheme_to_byte(rs->r, (size_t)idx + 1);
            size_t byte_start = start_res.found ? start_res.value : 0;
            size_t byte_end = end_res.found ? end_res.value : rope_byte_count(rs->r);
            size_t cluster_len = byte_end - byte_start;
            uint8_t cluster_buf[32];
            rope_slice_to_str(rs->r, byte_start, cluster_len, cluster_buf, sizeof(cluster_buf));
            result = vm__push(vm, jacl_string_new(&vm->heap, vm->intern_table,
                                                   (const char*)cluster_buf, cluster_len));
          } else {
            /* Inline or flat: extract bytes and scan graphemes */
            uint8_t buf[256];
            uint32_t byte_len = jacl_string_data(str_val, (char*)buf, sizeof(buf));
            size_t gs, ge;
            if (jacl_grapheme_nth(buf, byte_len, (size_t)idx, &gs, &ge)) {
              result = vm__push(vm, jacl_string_new(&vm->heap, vm->intern_table,
                                                     (const char*)(buf + gs), ge - gs));
            } else {
              result = vm__push(vm, JACL_NIL);
            }
          }
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
        /* Use grapheme count for bounds, not byte length */
        uint32_t glen = jacl_string_len(str_val);
        int32_t start = jacl_as_i32(start_val);
        int32_t end;
        if (jacl_is_nil(end_val)) {
          end = (int32_t)glen;  /* 2-arg form: slice to end */
        } else if (jacl_is_i32(end_val)) {
          end = jacl_as_i32(end_val);
        } else {
          vm__set_error(vm, "type error in 'slice': expected i32 end, got %s",
                       vm__type_name(end_val));
          return VM_RUNTIME_ERROR;
        }
        /* Clamp grapheme bounds */
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if ((uint32_t)start > glen) start = (int32_t)glen;
        if ((uint32_t)end > glen) end = (int32_t)glen;
        if (end < start) end = start;
        uint32_t grapheme_count = (uint32_t)(end - start);

        JaclVal res;
        if (grapheme_count == 0) {
          res = jacl_inline_string("", 0);
        } else if (jacl_is_rope_string(str_val)) {
          /* Rope: use rope_slice_by_graphemes for O(log n) slicing */
          JaclRopeString* rs = jacl_as_rope_string(str_val);
          rope sliced = rope_slice_by_graphemes(rs->r, (size_t)start, grapheme_count);
          size_t sliced_bytes = rope_byte_count(sliced);
          if (sliced_bytes == 0) {
            rope_unref(sliced);
            res = jacl_inline_string("", 0);
          } else if (sliced_bytes <= 128) {
            /* Materialize small rope slices to flat/inline tier */
            uint8_t tmp[128];
            rope_to_str(sliced, tmp, sliced_bytes);
            rope_unref(sliced);
            res = jacl_string_new(&vm->heap, vm->intern_table,
                                  (const char*)tmp, sliced_bytes);
          } else {
            /* Wrap large rope slice in JaclRopeString */
            uint32_t hash = 2166136261u;
            uint8_t hash_buf[256];
            size_t remaining = sliced_bytes;
            size_t offset = 0;
            while (remaining > 0) {
              size_t chunk = remaining < sizeof(hash_buf) ? remaining : sizeof(hash_buf);
              rope_slice_to_str(sliced, offset, chunk, hash_buf, chunk);
              for (size_t i = 0; i < chunk; i++) {
                hash ^= hash_buf[i];
                hash *= 16777619u;
              }
              offset += chunk;
              remaining -= chunk;
            }
            JaclRopeString* new_rs = (JaclRopeString*)gc_alloc(&vm->heap,
                sizeof(JaclRopeString), OBJ_ROPE_STRING);
            new_rs->hash = hash;
            new_rs->r = sliced;
            res = jacl_rope_string_ptr(new_rs);
          }
        } else {
          /* Inline or flat: extract bytes, convert grapheme range to byte range */
          uint8_t buf[256];
          uint32_t byte_len = jacl_string_data(str_val, (char*)buf, sizeof(buf));
          size_t byte_start = unicode_grapheme_byte_offset(buf, byte_len, (size_t)start);
          size_t byte_end = unicode_grapheme_byte_offset(buf, byte_len, (size_t)end);
          size_t slice_bytes = byte_end - byte_start;
          if (slice_bytes == 0) {
            res = jacl_inline_string("", 0);
          } else {
            res = jacl_string_new(&vm->heap, vm->intern_table,
                                  (const char*)(buf + byte_start), slice_bytes);
          }
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
          val = MREF_VAL(ref);
        }

        if (jacl_is_string(val)) {
          /* Already a string — push back unchanged */
          result = vm__push(vm, val);
          if (result != VM_OK) return result;
        } else if (jacl_is_vector(val) || jacl_is_map(val) || jacl_is_box(val) || jacl_is_atom(val) || jacl_is_future(val) || jacl_is_stream(val) || jacl_is_typed_vector(val) || jacl_is_typed_map(val)) {
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          vm__fmt_value(&fmt, val);
          JaclVal str = jacl_string_new(&vm->heap, vm->intern_table,
                                         fmt.data, fmt.len);
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

          if (n < 0) n = 0;
          JaclVal str = jacl_string_new(&vm->heap, vm->intern_table,
                                         buf, (size_t)n);
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

      case OP_VEC_SPREAD: {
        uint8_t fixed_args = vm__read_byte(vm);
        uint8_t num_spreads = vm__read_byte(vm);
        uint32_t total_args = fixed_args;
        for (uint8_t i = 0; i < num_spreads; i++) {
          if (vm->spread_count_top == 0) {
            vm__set_error(vm, "spread count underflow");
            return VM_RUNTIME_ERROR;
          }
          total_args += vm->spread_counts[--vm->spread_count_top];
        }
        uint32_t base = vm->stack_top - total_args;
        gc__current_heap = &vm->heap;
        jacl_vec_root* vec = jacl_vec_empty();
        for (uint32_t i = 0; i < total_args; i++) {
          vec = jacl_vec_push_back(vec, vm->stack[base + i]);
        }
        vm->stack_top = base;
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
          if (jacl_is_stream(vec_val))
            vm__set_error(vm, "vec-get requires a vector; got stream (use collect to materialize)");
          else
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
          if (jacl_is_stream(vec_val))
            vm__set_error(vm, "vec-len requires a vector; got stream (use collect to materialize)");
          else
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
          if (jacl_is_stream(vec_val))
            vm__set_error(vm, "vec-push requires a vector; got stream (use collect to materialize)");
          else
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
          if (jacl_is_stream(vec_val))
            vm__set_error(vm, "vec-set requires a vector; got stream (use collect to materialize)");
          else
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
          if (jacl_is_stream(a_val))
            vm__set_error(vm, "vec-concat requires a vector; got stream (use collect to materialize)");
          else
            vm__set_error(vm, "type error in 'vec-concat': expected vector, got %s",
                         vm__type_name(a_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_vector(b_val)) {
          if (jacl_is_stream(b_val))
            vm__set_error(vm, "vec-concat requires a vector; got stream (use collect to materialize)");
          else
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
          if (jacl_is_stream(vec_val))
            vm__set_error(vm, "vec-slice requires a vector; got stream (use collect to materialize)");
          else
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
        JaclVal error_val = JACL_NIL;  /* US-005: track error from stream */
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
        } else if (jacl_is_stream(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "each on stream requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }

          JaclStream* stream = jacl_as_stream(coll_val);

          /* Use unified pull helper for all stream kinds */
          while (stream->state != STREAM_EXHAUSTED) {
            JaclVal elem;
            StreamPullResult pr = vm__pull_stream_one(vm, coll_val, &elem);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) break;

            /* US-005: If stream yields error value, stop iteration and propagate */
            if (jacl_is_error(elem)) {
              error_val = elem;
              break;
            }

            /* Call callback with elem */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, elem);
            if (result != VM_OK) return result;

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t cb_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - 1;
            cf->chunk      = &closure->chunk;

            uint8_t* cb_ip = vm->ip;
            BytecodeChunk* cb_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, cb_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            vm->ip    = cb_ip;
            vm->chunk = cb_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else {
          vm__set_error(vm,
            "type error in 'each': expected vector, map, or stream, got %s",
            vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }

        /* US-005: each returns error if stream yielded one, otherwise nil */
        if (jacl_is_error(error_val)) {
          result = vm__push(vm, error_val);
        } else {
          result = vm__push(vm, JACL_NIL);
        }
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

        } else if (jacl_is_stream(coll_val)) {
          /* Lazy transform: create a new transform stream wrapping source + fn */
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "transform on stream requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal transform_stream_val = jacl_stream(&vm->heap);
          JaclStream* ts = jacl_as_stream(transform_stream_val);
          ts->kind      = STREAM_KIND_TRANSFORM;
          ts->args[0]   = coll_val;     /* source stream */
          ts->args[1]   = closure_val;  /* transform closure */
          ts->arg_count = 2;
          result = vm__push(vm, transform_stream_val);
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm,
            "type error in 'transform': expected vector, map, or stream, got %s",
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

        } else if (jacl_is_stream(coll_val)) {
          /* Lazy filter: create a new filter stream wrapping source + predicate */
          if (closure->param_count != 1) {
            vm__set_error(vm,
              "filter on stream requires a proc with 1 parameter, got %d",
              (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          JaclVal filter_stream_val = jacl_stream(&vm->heap);
          JaclStream* fs = jacl_as_stream(filter_stream_val);
          fs->kind      = STREAM_KIND_FILTER;
          fs->args[0]   = coll_val;     /* source stream */
          fs->args[1]   = closure_val;  /* predicate closure */
          fs->arg_count = 2;
          result = vm__push(vm, filter_stream_val);
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm,
            "type error in 'filter': expected vector, map, or stream, got %s",
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
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
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
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef) + sizeof(JaclVal));
        ref->type_idx = 0;
        ref->total_size = sizeof(JaclVal);
        MREF_VAL(ref) = value;
        result = vm__push(vm, jacl_cell_ptr(ref));
        if (result != VM_OK) return result;
        break;
      }

      case OP_GET_CELL_LOCAL: {
        uint8_t slot = vm__read_byte(vm);
        JaclVal cell = vm->stack[frame->stack_base + slot];
        JaclMutableRef* ref = jacl_as_cell(cell);
        result = vm__push(vm, MREF_VAL(ref));
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
                         MREF_VAL(ref), new_value);
        gc_remembered_set_barrier(vm->remembered_set, cell, new_value);
        MREF_VAL(ref) = new_value;
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GET_CELL_UPVALUE: {
        uint8_t index = vm__read_byte(vm);
        JaclVal cell = frame->closure->upvalues[index];
        JaclMutableRef* ref = jacl_as_cell(cell);
        result = vm__push(vm, MREF_VAL(ref));
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
                         MREF_VAL(ref), new_value);
        gc_remembered_set_barrier(vm->remembered_set, cell, new_value);
        MREF_VAL(ref) = new_value;
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
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef) + sizeof(JaclVal));
        ref->type_idx = 0;
        ref->total_size = sizeof(JaclVal);
        MREF_VAL(ref) = value;
        result = vm__push(vm, jacl_box_ptr(ref));
        if (result != VM_OK) return result;
        break;
      }

      case OP_BOX_STRUCT: {
        /* Wrap a struct value in a JaclMutableRef. Accepts either:
           - N inline struct slots on TOS (marked in inline_slot_bitmap), or
           - a single heap HeapRecord pointer on TOS (legacy global path). */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "box: invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top < 1) {
          vm__set_error(vm, "box: stack underflow");
          return VM_RUNTIME_ERROR;
        }
        bool tos_is_inline = BITMAP_GET(vm->inline_slot_bitmap, vm->stack_top - 1);
        gc__current_heap = &vm->heap;
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF,
                                sizeof(JaclMutableRef) + sdef->total_size);
        ref->type_idx = type_idx;
        ref->total_size = sdef->total_size;
        if (tos_is_inline) {
          if (vm->stack_top < width) {
            vm__set_error(vm, "box: stack underflow for inline struct");
            return VM_RUNTIME_ERROR;
          }
          memcpy(ref->data, &vm->stack[vm->stack_top - width], sdef->total_size);
          for (uint32_t si = 0; si < width; si++) {
            BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
          }
          vm->stack_top -= width;
        } else {
          JaclVal value = vm->stack[--vm->stack_top];
          if (jacl_is_error(value)) {
            result = vm__push(vm, value); if (result != VM_OK) return result;
            break;
          }
          HeapRecord* s = jacl_as_heap_record_ptr(value);
          memcpy(ref->data, s->data, sdef->total_size);
        }
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
        JaclMutableRef* ref = (JaclMutableRef*)gc_alloc(&vm->heap, OBJ_MUTABLE_REF, sizeof(JaclMutableRef) + sizeof(JaclVal));
        ref->type_idx = 0;
        ref->total_size = sizeof(JaclVal);
        MREF_VAL(ref) = value;
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

      case OP_IS_BOX_TYPED: {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        bool match = false;
        if (jacl_is_box(value)) {
          JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(value);
          match = (ref->type_idx == type_idx);
        }
        result = vm__push(vm, jacl_bool(match));
        if (result != VM_OK) return result;
        break;
      }

      case OP_IS_BOX_TYPED_VEC: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        bool match = false;
        if (jacl_is_box(value)) {
          JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(value);
          if (ref->type_idx == 0) match = jacl_is_typed_vector(MREF_VAL(ref));
        }
        result = vm__push(vm, jacl_bool(match));
        if (result != VM_OK) return result;
        break;
      }

      case OP_IS_BOX_TYPED_MAP: {
        JaclVal value;
        result = vm__pop(vm, &value); if (result != VM_OK) return result;
        bool match = false;
        if (jacl_is_box(value)) {
          JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(value);
          if (ref->type_idx == 0) match = jacl_is_typed_map(MREF_VAL(ref));
        }
        result = vm__push(vm, jacl_bool(match));
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
        /* CPS await removed — all async functions use OP_AWAIT_SM now */
        vm__set_error(vm, "CPS await not supported (use state machine path)");
        return VM_RUNTIME_ERROR;
      }

      case OP_SPAWN: {
        /* Pop closure, create pending future, execute closure (resolving
           future with result), push future. */
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

        if (vm->runtime) {
          /* Runtime mode: submit task to worker thread pool */
          runtime__submit_spawn_task(vm->runtime, cl, f, vm->ctx);
          result = vm__push(vm, f);
          if (result != VM_OK) return result;
        } else {
          /* Single-threaded mode: execute closure synchronously */

          JaclVal saved_ctx = ctx_fork(vm, vm->ctx);

          uint8_t *saved_ip = vm->ip;
          BytecodeChunk *saved_chunk = vm->chunk;
          uint32_t saved_frame_count = vm->frame_count;

          if (cl->is_sm_compiled) {
            /* SM closure: create state machine, call synchronously */
            JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
            JaclStateMachine *sm = jacl_as_state_machine(sm_val);
            sm->sm_closure = closure_val;

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, sm_val);
            if (result != VM_OK) return result;
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
          } else {
            /* Non-suspending closure: call directly */
            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
          }

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

          VMResult sub = vm__run(vm, saved_frame_count);

          frame = &vm->frames[vm->frame_count - 1];
          vm->ip    = saved_ip;
          vm->chunk = saved_chunk;

          /* Resolve future with the return value */
          JaclVal spawn_result = JACL_NIL;
          if (sub == VM_OK && vm->stack_top > 0) {
            spawn_result = vm->stack[--vm->stack_top];
          } else if (sub != VM_OK) {
            JaclVal err = jacl_set_error(jacl_inline_string("error", 5));
            jacl_future_error(fut, err, vm->grey_buf, vm->gc_active_ptr);
            ctx_unfork(vm, saved_ctx);
            result = vm__push(vm, f);
            if (result != VM_OK) return result;
            break;
          }
          jacl_future_resolve(fut, spawn_result,
                              vm->grey_buf, vm->gc_active_ptr);

          ctx_unfork(vm, saved_ctx);

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
           Used by parallel_k closures (SM body error_k). */
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
           Used by race_k closures (SM body error_k). */
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
        bool par_sm_mode = jacl_is_state_machine(continuation);
        if (!jacl_is_nil(continuation) && !par_sm_mode) {
          vm__set_error(vm, "OP_PARALLEL: continuation must be state machine or nil");
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
            runtime__submit_parallel_task(vm->runtime, cl, agg_val, i, vm->ctx);
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

            JaclVal par_saved_ctx = ctx_fork(vm, vm->ctx);

            uint8_t *saved_ip = vm->ip;
            BytecodeChunk *saved_chunk = vm->chunk;
            uint32_t saved_frame_count = vm->frame_count;
            uint32_t saved_stack_top = vm->stack_top;

            if (cl->is_sm_compiled) {
              /* SM: create state machine, call synchronously */
              JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
              JaclStateMachine *sm = jacl_as_state_machine(sm_val);
              sm->sm_closure = closures[i];

              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;
              result = vm__push(vm, sm_val);
              if (result != VM_OK) return result;
              result = vm__push(vm, JACL_NIL);
              if (result != VM_OK) return result;

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

              VMResult sub = vm__run(vm, saved_frame_count);

              /* Capture result before restoring stack */
              JaclVal body_result = JACL_NIL;
              bool body_ok = (sub == VM_OK && vm->stack_top > saved_stack_top);
              if (body_ok) {
                body_result = vm->stack[vm->stack_top - 1];
              }

              vm->frame_count = saved_frame_count;
              vm->stack_top   = saved_stack_top;
              frame = &vm->frames[vm->frame_count - 1];
              vm->ip    = saved_ip;
              vm->chunk = saved_chunk;

              if (body_ok) {
                results[i] = body_result;
                if (jacl_is_error(results[i]) && !has_error) {
                  has_error = true; first_error = results[i];
                }
              } else if (sub != VM_OK) {
                results[i] = jacl_set_error(jacl_inline_string("error", 5));
                if (!has_error) { has_error = true; first_error = results[i]; }
              } else {
                results[i] = JACL_NIL;
              }
            } else {
              /* Non-suspending closure: call directly */
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

            ctx_unfork(vm, par_saved_ctx);
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

          /* Push result directly (SM mode or nil continuation) */
          result = vm__push(vm, cont_arg);
          if (result != VM_OK) return result;
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
        bool race_sm_mode = jacl_is_state_machine(continuation);
        if (!jacl_is_nil(continuation) && !race_sm_mode) {
          vm__set_error(vm, "OP_RACE: continuation must be state machine or nil");
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
            runtime__submit_race_task(vm->runtime, cl, agg_val, vm->ctx);
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

            JaclVal race_saved_ctx = ctx_fork(vm, vm->ctx);

            uint8_t *saved_ip = vm->ip;
            BytecodeChunk *saved_chunk = vm->chunk;
            uint32_t saved_frame_count = vm->frame_count;

            if (cl->is_sm_compiled) {
              /* SM: create state machine, call synchronously */
              JaclVal sm_val = gc_alloc_state_machine(&vm->heap, cl->sm_field_count);
              JaclStateMachine *sm = jacl_as_state_machine(sm_val);
              sm->sm_closure = closures[i];

              result = vm__push(vm, closures[i]);
              if (result != VM_OK) return result;
              result = vm__push(vm, sm_val);
              if (result != VM_OK) return result;
              result = vm__push(vm, JACL_NIL);
              if (result != VM_OK) return result;

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
                if (sub == VM_OK && vm->stack_top > 0) vm->stack_top--;
              }
            } else {
              /* Non-suspending closure: call directly */
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

            ctx_unfork(vm, race_saved_ctx);
          }

          /* Push result directly (SM mode or nil continuation) */
          result = vm__push(vm, winner_result);
          if (result != VM_OK) return result;
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
        if (ref->type_idx > 0) {
          /* Struct box: materialize data[] into a heap HeapRecord */
          if (!vm->struct_registry || ref->type_idx >= vm->struct_registry->count) {
            vm__set_error(vm, "deref: invalid struct type index %u", (unsigned)ref->type_idx);
            return VM_RUNTIME_ERROR;
          }
          StructTypeDef* sdef = vm->struct_registry->defs[ref->type_idx];
          gc__current_heap = &vm->heap;
          HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                  sizeof(HeapRecord) + sdef->total_size);
          s->type_idx = ref->type_idx;
          s->total_size = sdef->total_size;
          memcpy(s->data, ref->data, sdef->total_size);
          result = vm__push(vm, jacl_heap_record_val(s));
          if (result != VM_OK) return result;
        } else {
          JaclVal deref_val;
          if (jacl_is_atom(container)) {
            deref_val = ATOMIC_LOAD_EXPLICIT(&MREF_VAL(ref), MEM_ACQUIRE);
          } else {
            deref_val = MREF_VAL(ref);
          }
          result = vm__push(vm, deref_val);
          if (result != VM_OK) return result;
        }
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
        if (ref->type_idx > 0) {
          /* Struct box: copy new struct data into box */
          HeapRecord* s = jacl_as_heap_record_ptr(new_val);
          if (!s || s->type_idx != ref->type_idx) {
            vm__set_error(vm, "reset!: struct type mismatch in box");
            return VM_RUNTIME_ERROR;
          }
          StructTypeDef* sdef = vm->struct_registry->defs[ref->type_idx];
          memcpy(ref->data, s->data, sdef->total_size);
          /* No GC write barrier needed — struct data has no GC references */
          result = vm__push(vm, new_val);
        } else if (jacl_is_atom(container)) {
          JaclVal reset_old = ATOMIC_LOAD_EXPLICIT(&MREF_VAL(ref), MEM_ACQUIRE);
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           reset_old, new_val);
          gc_remembered_set_barrier(vm->remembered_set, container, new_val);
          ATOMIC_STORE_EXPLICIT(&MREF_VAL(ref), new_val, MEM_RELEASE);
          result = vm__push(vm, new_val);
        } else {
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           MREF_VAL(ref), new_val);
          gc_remembered_set_barrier(vm->remembered_set, container, new_val);
          MREF_VAL(ref) = new_val;
          result = vm__push(vm, new_val);
        }
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

        if (ref->type_idx > 0) {
          /* Struct box swap: pass current value to closure, copy result back.
             Atoms cannot hold structs (compile error), so this is always non-atomic. */
          StructTypeDef* sdef = vm->struct_registry->defs[ref->type_idx];
          uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);

          /* Phase 5d: push inline bytes if closure expects struct param,
             otherwise materialize to heap for dyn param */
          result = vm__push(vm, closure_val);
          if (result != VM_OK) return result;

          if (closure->param_total_slots > closure->param_count) {
            /* Closure expects inline struct param — push raw bytes */
            if (vm->stack_top + width > VM_STACK_MAX) {
              vm__set_error(vm, "stack overflow (swap inline)");
              return VM_STACK_OVERFLOW;
            }
            memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
            memcpy(&vm->stack[vm->stack_top], ref->data, sdef->total_size);
            for (uint32_t si = 0; si < width; si++)
              BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
            vm->stack_top += width;
          } else {
            /* Closure expects dyn/heap param — materialize */
            gc__current_heap = &vm->heap;
            HeapRecord* old_s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                        sizeof(HeapRecord) + sdef->total_size);
            old_s->type_idx = ref->type_idx;
            old_s->total_size = sdef->total_size;
            memcpy(old_s->data, ref->data, sdef->total_size);
            result = vm__push(vm, jacl_heap_record_val(old_s));
            if (result != VM_OK) return result;
          }

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_error(vm, "stack overflow");
            return VM_RUNTIME_ERROR;
          }
          uint32_t caller_frame_count = vm->frame_count;
          CallFrame* cf = &vm->frames[vm->frame_count++];
          cf->closure    = closure;
          cf->return_ip  = vm->ip;
          cf->stack_base = vm->stack_top - closure->param_total_slots;
          cf->chunk      = &closure->chunk;
          vm->ip    = closure->chunk.code;
          vm->chunk = &closure->chunk;

          VMResult call_result = vm__run(vm, caller_frame_count);
          if (call_result != VM_OK) return call_result;

          /* Closure may return inline bytes (OP_RETURN_WIDE) or heap ptr.
             Check bitmap to determine which. */
          {
            uint32_t rb = vm->stack_top - width;
            if (BITMAP_GET(vm->inline_slot_bitmap, rb)) {
              /* Inline bytes — copy directly from stack to box */
              memcpy(ref->data, &vm->stack[rb], sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_CLR(vm->inline_slot_bitmap, rb + si);
              vm->stack_top = rb;
              /* swap_result = nil for struct swap (result is the box itself) */
              swap_result = JACL_NIL;
            } else {
              result = vm__pop(vm, &swap_result);
              if (result != VM_OK) return result;
              HeapRecord* new_s = jacl_as_heap_record_ptr(swap_result);
              if (!new_s || new_s->type_idx != ref->type_idx) {
                vm__set_error(vm, "swap!: struct type mismatch in box");
                return VM_RUNTIME_ERROR;
              }
              memcpy(ref->data, new_s->data, sdef->total_size);
            }
          }
        } else {
          for (;;) {
            /* Read current value (atomic for atoms, plain for boxes) */
            JaclVal swap_old_val = swap_is_atom
              ? ATOMIC_LOAD_EXPLICIT(&MREF_VAL(ref), MEM_ACQUIRE)
              : MREF_VAL(ref);

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
              if (ATOMIC_CAS(&MREF_VAL(ref), &expected, swap_result,
                             MEM_ACQ_REL, MEM_ACQUIRE)) {
                /* CAS succeeded — fire write barrier */
                gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                                 swap_old_val, swap_result);
                gc_remembered_set_barrier(vm->remembered_set,
                                          container, swap_result);
                break; /* exit retry loop */
              }
              /* CAS failed — swap_result becomes garbage, retry */
            } else {
              /* Box: non-atomic store, write barrier always fires */
              gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                               swap_old_val, swap_result);
              gc_remembered_set_barrier(vm->remembered_set,
                                        container, swap_result);
              MREF_VAL(ref) = swap_result;
              break; /* no retry for boxes */
            }
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

      /* --- VM-internal macros for typed arithmetic opcodes (vm.c only) --- */

/* i64 binary arithmetic: pop b then a, apply op, push raw i64 result */
#define VM__I64_BINOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    int64_t a_ = (int64_t)raw_a_, b_ = (int64_t)raw_b_;                      \
    result = vm__push(vm, (uint64_t)(a_ op b_));                              \
    if (result != VM_OK) return result;                                       \
} while(0)

/* i64 div/mod: pop b then a, zero-check b, apply op, push raw i64 result */
#define VM__I64_DIVOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    int64_t a_ = (int64_t)raw_a_, b_ = (int64_t)raw_b_;                      \
    if (b_ == 0) { vm__set_error(vm, "division by zero"); return VM_RUNTIME_ERROR; } \
    result = vm__push(vm, (uint64_t)(a_ op b_));                              \
    if (result != VM_OK) return result;                                       \
} while(0)

/* i64 unary negation: pop a, push -a */
#define VM__I64_NEG() do {                                                   \
    JaclVal raw_a_;                                                           \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    result = vm__push(vm, (uint64_t)(-(int64_t)raw_a_));                      \
    if (result != VM_OK) return result;                                       \
} while(0)

/* i64 comparison: pop b then a, apply op, push jacl_bool result */
#define VM__I64_CMPOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    int64_t a_ = (int64_t)raw_a_, b_ = (int64_t)raw_b_;                      \
    result = vm__push(vm, a_ op b_ ? JACL_TRUE : JACL_FALSE);                \
    if (result != VM_OK) return result;                                       \
} while(0)

/* f64 binary arithmetic: pop b then a via memcpy, apply op, push raw f64 result */
#define VM__F64_BINOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    double r_ = a_ op b_;                                                    \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 div: pop b then a, zero-check b, divide, push raw f64 result */
#define VM__F64_DIVOP() do {                                                 \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    if (b_ == 0.0) { vm__set_error(vm, "division by zero"); return VM_RUNTIME_ERROR; } \
    double r_ = a_ / b_;                                                     \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 mod: pop b then a, zero-check b, fmod, push raw f64 result */
#define VM__F64_MODOP() do {                                                 \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    if (b_ == 0.0) { vm__set_error(vm, "division by zero"); return VM_RUNTIME_ERROR; } \
    double r_ = fmod(a_, b_);                                                \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 unary negation: pop a via memcpy, push -a as raw f64 */
#define VM__F64_NEG() do {                                                   \
    JaclVal raw_a_;                                                           \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_; memcpy(&a_, &raw_a_, sizeof(double));                         \
    double r_ = -a_;                                                         \
    uint64_t raw_r_; memcpy(&raw_r_, &r_, sizeof(uint64_t));                 \
    result = vm__push(vm, raw_r_);                                           \
    if (result != VM_OK) return result;                                      \
} while(0)

/* f64 comparison: pop b then a via memcpy, apply op, push jacl_bool result */
#define VM__F64_CMPOP(op) do {                                               \
    JaclVal raw_b_, raw_a_;                                                   \
    result = vm__pop(vm, &raw_b_); if (result != VM_OK) return result;        \
    result = vm__pop(vm, &raw_a_); if (result != VM_OK) return result;        \
    double a_, b_;                                                            \
    memcpy(&a_, &raw_a_, sizeof(double)); memcpy(&b_, &raw_b_, sizeof(double));\
    result = vm__push(vm, a_ op b_ ? JACL_TRUE : JACL_FALSE);                \
    if (result != VM_OK) return result;                                      \
} while(0)

      /* --- M11: i64 typed arithmetic/comparison opcodes --- */

      case OP_ADD_I64: { VM__I64_BINOP(+); break; }
      case OP_SUB_I64: { VM__I64_BINOP(-); break; }
      case OP_MUL_I64: { VM__I64_BINOP(*); break; }
      case OP_DIV_I64: { VM__I64_DIVOP(/); break; }
      case OP_MOD_I64: { VM__I64_DIVOP(%); break; }
      case OP_NEG_I64: { VM__I64_NEG(); break; }
      case OP_LT_I64:  { VM__I64_CMPOP(<); break; }
      case OP_GT_I64:  { VM__I64_CMPOP(>); break; }
      case OP_LE_I64:  { VM__I64_CMPOP(<=); break; }
      case OP_GE_I64:  { VM__I64_CMPOP(>=); break; }
      case OP_EQ_I64:  { VM__I64_CMPOP(==); break; }

      /* --- M11: f64 typed arithmetic/comparison opcodes --- */

      case OP_ADD_F64: { VM__F64_BINOP(+); break; }
      case OP_SUB_F64: { VM__F64_BINOP(-); break; }
      case OP_MUL_F64: { VM__F64_BINOP(*); break; }
      case OP_DIV_F64: { VM__F64_DIVOP(); break; }
      case OP_MOD_F64: { VM__F64_MODOP(); break; }
      case OP_NEG_F64: { VM__F64_NEG(); break; }
      case OP_LT_F64:  { VM__F64_CMPOP(<); break; }
      case OP_GT_F64:  { VM__F64_CMPOP(>); break; }
      case OP_LE_F64:  { VM__F64_CMPOP(<=); break; }
      case OP_GE_F64:  { VM__F64_CMPOP(>=); break; }
      case OP_EQ_F64:  { VM__F64_CMPOP(==); break; }

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

      case OP_STRUCT_GET: {
        uint16_t field_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field access on non-struct value");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        /* NULL heap → unboxed 64-bit (raw bits for typed arithmetic) */
        JaclVal field_val = vm__heap_record_read_field(NULL, s, field_offset, field_type);
        result = vm__push(vm, field_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_SET: {
        uint16_t field_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field mutation on non-struct value");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        /* Write barrier for reference-type fields during active GC */
        if (field_type == TYPE_DYN || field_type == TYPE_STR ||
            field_type == TYPE_STRUCT || field_type == TYPE_MAP) {
          JaclVal old_val;
          memcpy(&old_val, s->data + field_offset, sizeof(JaclVal));
          gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                           old_val, new_val);
        }
        vm__heap_record_write_field(s, field_offset, field_type, new_val);
        /* Push struct value back (for chaining) */
        result = vm__push(vm, struct_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_GET_DYN: {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          break;
        }
        /* Handle maps: field access becomes map lookup */
        if (jacl_is_map(struct_val)) {
          JaclVal name_val = frame->chunk->constants[name_idx];
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(struct_val);
          if (jacl_map_has(map, name_val)) {
            result = vm__push(vm, jacl_map_get(map, name_val));
          } else {
            result = vm__push(vm, JACL_NIL);
          }
          if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field access requires struct or map");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* s = jacl_as_heap_record_ptr(struct_val);
        if (!vm->struct_registry || s->type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index");
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[s->type_idx];
        JaclVal name_val = frame->chunk->constants[name_idx];
        char fname[64]; uint32_t flen;
        flen = jacl_string_data(name_val, fname, sizeof(fname));
        uint32_t fi;
        for (fi = 0; fi < sdef->field_count; fi++) {
          if (sdef->fields[fi].name_len == flen &&
              memcmp(sdef->fields[fi].name, fname, flen) == 0) break;
        }
        if (fi == sdef->field_count) {
          vm__set_error(vm, "struct '%.*s' has no field '%.*s'",
                        (int)sdef->name_len, sdef->name, (int)flen, fname);
          return VM_RUNTIME_ERROR;
        }
        /* heap != NULL → boxed 64-bit types (always boxed for dyn context) */
        uint16_t foff = sdef->fields[fi].offset;
        JaclVal field_val = vm__heap_record_read_field(&vm->heap, s, foff,
                                                   (int)sdef->fields[fi].type);
        result = vm__push(vm, field_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_OPTIONAL_GET: {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        /* Nil-safe: if nil, just push nil and skip field access */
        if (jacl_is_nil(struct_val)) {
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          break;
        }
        /* Error propagation */
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          break;
        }
        /* Handle maps */
        if (jacl_is_map(struct_val)) {
          JaclVal name_val = frame->chunk->constants[name_idx];
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(struct_val);
          if (jacl_map_has(map, name_val)) {
            result = vm__push(vm, jacl_map_get(map, name_val));
          } else {
            result = vm__push(vm, JACL_NIL);
          }
          if (result != VM_OK) return result;
          break;
        }
        /* Structs must use -> not ?. */
        vm__set_error(vm, "?. cannot be used on %s; use -> for struct field access",
                      jacl_is_struct(struct_val) ? "structs" : "this type");
        return VM_RUNTIME_ERROR;
      }

      case OP_STRUCT_SET_DYN: {
        uint16_t name_idx = vm__read_u16(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        JaclVal struct_val;
        result = vm__pop(vm, &struct_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(struct_val)) {
          result = vm__push(vm, struct_val);
          if (result != VM_OK) return result;
          break;
        }
        /* Handle maps: field set becomes map-set (returns new map) */
        if (jacl_is_map(struct_val)) {
          JaclVal name_val = frame->chunk->constants[name_idx];
          jacl_map_node* map = (jacl_map_node*)jacl_as_ptr(struct_val);
          gc__current_heap = &vm->heap;
          jacl_map_node* new_map = jacl_map_set(map, name_val, new_val);
          result = vm__push(vm, jacl_map_ptr(new_map));
          if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_struct(struct_val)) {
          vm__set_error(vm, "field mutation requires struct or map");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* sd = jacl_as_heap_record_ptr(struct_val);
        if (!vm->struct_registry || sd->type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index");
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef2 = vm->struct_registry->defs[sd->type_idx];
        JaclVal name_val2 = frame->chunk->constants[name_idx];
        char fname2[64]; uint32_t flen2;
        flen2 = jacl_string_data(name_val2, fname2, sizeof(fname2));
        uint32_t fi2;
        for (fi2 = 0; fi2 < sdef2->field_count; fi2++) {
          if (sdef2->fields[fi2].name_len == flen2 &&
              memcmp(sdef2->fields[fi2].name, fname2, flen2) == 0) break;
        }
        if (fi2 == sdef2->field_count) {
          vm__set_error(vm, "struct '%.*s' has no field '%.*s'",
                        (int)sdef2->name_len, sdef2->name, (int)flen2, fname2);
          return VM_RUNTIME_ERROR;
        }
        if (!sdef2->fields[fi2].is_mutable) {
          vm__set_error(vm, "cannot mutate immutable field '%.*s' on struct '%.*s'",
                        (int)flen2, fname2, (int)sdef2->name_len, sdef2->name);
          return VM_RUNTIME_ERROR;
        }
        uint16_t foff2 = sdef2->fields[fi2].offset;
        vm__heap_record_write_field(sd, foff2, (int)sdef2->fields[fi2].type, new_val);
        result = vm__push(vm, struct_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_NEW: {
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t field_count = sdef->field_count;

        /* Allocate struct on GC heap */
        gc__current_heap = &vm->heap;
        HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                sizeof(HeapRecord) + sdef->total_size);
        s->type_idx = type_idx;
        s->total_size = sdef->total_size;
        memset(s->data, 0, sdef->total_size);

        /* Store each field value from the stack into struct data */
        for (uint32_t i = 0; i < field_count; i++) {
          JaclVal val = vm->stack[vm->stack_top - field_count + i];
          uint32_t off = sdef->fields[i].offset;
          switch (sdef->fields[i].type) {
            case TYPE_BOOL: {
              uint8_t b = jacl_as_bool(val) ? 1 : 0;
              s->data[off] = b;
              break;
            }
            case TYPE_I32: {
              int32_t n = jacl_as_i32(val);
              memcpy(s->data + off, &n, 4);
              break;
            }
            case TYPE_U32: {
              uint32_t n = jacl_as_u32(val);
              memcpy(s->data + off, &n, 4);
              break;
            }
            case TYPE_F32: {
              float f = jacl_as_f32(val);
              memcpy(s->data + off, &f, 4);
              break;
            }
            case TYPE_I64: {
              /* Raw i64 on stack (unboxed) */
              int64_t n = (int64_t)val;
              memcpy(s->data + off, &n, 8);
              break;
            }
            case TYPE_U64: {
              uint64_t n = val;
              memcpy(s->data + off, &n, 8);
              break;
            }
            case TYPE_F64: {
              double d;
              memcpy(&d, &val, 8);
              memcpy(s->data + off, &d, 8);
              break;
            }
            default: {
              /* str, vec, map, closure, dyn, struct — store full JaclVal */
              memcpy(s->data + off, &val, sizeof(JaclVal));
              break;
            }
          }
        }

        vm->stack_top -= field_count;
        result = vm__push(vm, jacl_heap_record_val(s));
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_NEW_INLINE: {
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t field_count = sdef->field_count;
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);

        /* Save field argument values before popping */
        JaclVal field_vals[256];
        for (uint32_t i = 0; i < field_count && i < 256; i++) {
          field_vals[i] = vm->stack[vm->stack_top - field_count + i];
        }

        /* Pop field arguments */
        vm->stack_top -= field_count;

        /* Check stack capacity for inline struct slots */
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_error(vm, "stack overflow (inline struct)");
          return VM_STACK_OVERFLOW;
        }

        /* Zero-initialize struct slots (ensures padding bytes are deterministic) */
        uint32_t base = vm->stack_top;
        memset(&vm->stack[base], 0, width * sizeof(JaclVal));

        /* Write each field at its byte offset within the stack slot region */
        uint8_t* struct_base = (uint8_t*)&vm->stack[base];
        for (uint32_t i = 0; i < field_count; i++) {
          uint32_t off = sdef->fields[i].offset;
          JaclVal val = field_vals[i];
          switch (sdef->fields[i].type) {
            case TYPE_BOOL: {
              uint8_t b = jacl_as_bool(val) ? 1 : 0;
              struct_base[off] = b;
              break;
            }
            case TYPE_I32: {
              int32_t n = jacl_as_i32(val);
              memcpy(struct_base + off, &n, 4);
              break;
            }
            case TYPE_U32: {
              uint32_t n = jacl_as_u32(val);
              memcpy(struct_base + off, &n, 4);
              break;
            }
            case TYPE_F32: {
              float f = jacl_as_f32(val);
              memcpy(struct_base + off, &f, 4);
              break;
            }
            case TYPE_I64: {
              int64_t n = (int64_t)val;
              memcpy(struct_base + off, &n, 8);
              break;
            }
            case TYPE_U64: {
              uint64_t n = val;
              memcpy(struct_base + off, &n, 8);
              break;
            }
            case TYPE_F64: {
              double d;
              memcpy(&d, &val, 8);
              memcpy(struct_base + off, &d, 8);
              break;
            }
            case TYPE_STRUCT: {
              /* Nested struct: copy raw data from heap-allocated HeapRecord */
              HeapRecord* nested = jacl_as_heap_record_ptr(val);
              if (nested) {
                memcpy(struct_base + off, nested->data, sdef->fields[i].size);
              }
              break;
            }
            default: break;
          }
        }

        vm->stack_top = base + width;
        /* US-014: mark these stack slots as raw struct bytes (not GC-traceable) */
        for (uint32_t si = base; si < base + width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, si);
        }
        break;
      }

      case OP_STRUCT_GET_INLINE: {
        /* Read a field from a stack-resident inline struct.
           Operands: uint8_t base_slot, uint16_t byte_offset, uint8_t field_type.
           The struct occupies consecutive stack slots starting at
           frame->stack_base + base_slot; we interpret them as raw bytes. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        uint8_t* struct_base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        JaclVal field_val;
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { uint8_t b = struct_base[byte_offset]; field_val = jacl_bool(b); break; }
          case TYPE_I32: { int32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_i32(n); break; }
          case TYPE_U32: { uint32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_u32(n); break; }
          case TYPE_F32: { float f; memcpy(&f, struct_base + byte_offset, 4); field_val = jacl_f32(f); break; }
          case TYPE_I64: { int64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_U64: { uint64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_F64: { double d; memcpy(&d, struct_base + byte_offset, 8); memcpy(&field_val, &d, 8); break; }
          case TYPE_STRUCT: {
            /* Sub-struct field: read additional uint16_t type_idx, materialize from inline bytes */
            uint16_t sub_type_idx = vm__read_u16(vm);
            if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
              vm__set_error(vm, "invalid struct type index %u for inline get", (unsigned)sub_type_idx);
              return VM_RUNTIME_ERROR;
            }
            StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
            gc__current_heap = &vm->heap;
            HeapRecord* sub_s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                       sizeof(HeapRecord) + sub_sdef->total_size);
            sub_s->type_idx = sub_type_idx;
            sub_s->total_size = sub_sdef->total_size;
            memcpy(sub_s->data, struct_base + byte_offset, sub_sdef->total_size);
            field_val = jacl_heap_record_val(sub_s);
            break;
          }
          default: { memcpy(&field_val, struct_base + byte_offset, sizeof(JaclVal)); break; }
        }
        result = vm__push(vm, field_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_SET_INLINE: {
        /* Write a field to a stack-resident inline struct.
           Operands: uint8_t base_slot, uint16_t byte_offset, uint8_t field_type.
           Pops the new value from stack, writes to the struct's byte region,
           pushes nil (mutation is in-place on the stack slots). */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        uint8_t* struct_base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { uint8_t b = jacl_as_bool(new_val) ? 1 : 0; struct_base[byte_offset] = b; break; }
          case TYPE_I32: { int32_t n = jacl_as_i32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_U32: { uint32_t n = jacl_as_u32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_F32: { float f = jacl_as_f32(new_val); memcpy(struct_base + byte_offset, &f, 4); break; }
          case TYPE_I64: { int64_t n = (int64_t)new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_U64: { uint64_t n = new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_F64: { double d; memcpy(&d, &new_val, 8); memcpy(struct_base + byte_offset, &d, 8); break; }
          default: { memcpy(struct_base + byte_offset, &new_val, sizeof(JaclVal)); break; }
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_MATERIALIZE: {
        /* Convert an inline (stack-resident) struct to a heap-allocated HeapRecord.
           Operands: uint8_t base_slot, uint16_t type_idx.
           Reads raw bytes from stack slots, allocates HeapRecord, copies data, pushes pointer. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for materialize", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        gc__current_heap = &vm->heap;
        HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                sizeof(HeapRecord) + sdef->total_size);
        s->type_idx = type_idx;
        s->total_size = sdef->total_size;
        /* Copy raw bytes from stack slot region into heap struct data */
        uint8_t* struct_base = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        memcpy(s->data, struct_base, sdef->total_size);
        result = vm__push(vm, jacl_heap_record_val(s));
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_COPY: {
        /* Dead opcode — struct pass-by-value is now handled inline (Phase 5a). */
        vm__set_error(vm, "OP_STRUCT_COPY is no longer emitted");
        return VM_RUNTIME_ERROR;
      }

      case OP_STRUCT_STORE_INLINE: {
        /* De-materialize a heap HeapRecord into N consecutive stack slots.
           Operands: uint8_t base_slot, uint16_t type_idx.
           Reads heap struct pointer from stack[base_slot], writes raw bytes
           across N slots starting at base_slot, adjusts stack_top. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for store_inline", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        uint32_t abs_base = frame->stack_base + base_slot;
        /* Read the heap struct pointer from the base slot */
        JaclVal heap_val = vm->stack[abs_base];
        if (!jacl_is_struct(heap_val)) {
          vm__set_error(vm, "OP_STRUCT_STORE_INLINE expects struct at slot %u", (unsigned)base_slot);
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* src = jacl_as_heap_record_ptr(heap_val);
        /* Zero-fill N slots then copy raw struct bytes */
        memset(&vm->stack[abs_base], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[abs_base], src->data, sdef->total_size);
        /* Adjust stack_top to account for the N slots */
        vm->stack_top = abs_base + width;
        /* US-014: mark these stack slots as raw struct bytes (not GC-traceable) */
        for (uint32_t si = abs_base; si < abs_base + width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, si);
        }
        break;
      }

      case OP_STRUCT_GET_UPVALUE: {
        /* US-008: Read a field from a closure-captured inline struct.
           Same as OP_STRUCT_GET_INLINE but base is frame->closure->upvalues[base_uv_slot]. */
        uint8_t base_uv_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        uint8_t* struct_base = (uint8_t*)&frame->closure->upvalues[base_uv_slot];
        JaclVal field_val;
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { uint8_t b = struct_base[byte_offset]; field_val = jacl_bool(b); break; }
          case TYPE_I32: { int32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_i32(n); break; }
          case TYPE_U32: { uint32_t n; memcpy(&n, struct_base + byte_offset, 4); field_val = jacl_u32(n); break; }
          case TYPE_F32: { float f; memcpy(&f, struct_base + byte_offset, 4); field_val = jacl_f32(f); break; }
          case TYPE_I64: { int64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_U64: { uint64_t n; memcpy(&n, struct_base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_F64: { double d; memcpy(&d, struct_base + byte_offset, 8); memcpy(&field_val, &d, 8); break; }
          case TYPE_STRUCT: {
            uint16_t sub_type_idx = vm__read_u16(vm);
            if (!vm->struct_registry || sub_type_idx >= vm->struct_registry->count) {
              vm__set_error(vm, "invalid struct type index %u for upvalue get", (unsigned)sub_type_idx);
              return VM_RUNTIME_ERROR;
            }
            StructTypeDef* sub_sdef = vm->struct_registry->defs[sub_type_idx];
            gc__current_heap = &vm->heap;
            HeapRecord* sub_s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                       sizeof(HeapRecord) + sub_sdef->total_size);
            sub_s->type_idx = sub_type_idx;
            sub_s->total_size = sub_sdef->total_size;
            memcpy(sub_s->data, struct_base + byte_offset, sub_sdef->total_size);
            field_val = jacl_heap_record_val(sub_s);
            break;
          }
          default: { memcpy(&field_val, struct_base + byte_offset, sizeof(JaclVal)); break; }
        }
        result = vm__push(vm, field_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_SET_UPVALUE: {
        /* US-008: Write a field to a closure-captured inline struct.
           Same as OP_STRUCT_SET_INLINE but base is frame->closure->upvalues[base_uv_slot]. */
        uint8_t base_uv_slot = vm__read_byte(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        uint8_t* struct_base = (uint8_t*)&frame->closure->upvalues[base_uv_slot];
        switch ((JaclType)field_type) {
          case TYPE_BOOL: { uint8_t b = jacl_as_bool(new_val) ? 1 : 0; struct_base[byte_offset] = b; break; }
          case TYPE_I32: { int32_t n = jacl_as_i32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_U32: { uint32_t n = jacl_as_u32(new_val); memcpy(struct_base + byte_offset, &n, 4); break; }
          case TYPE_F32: { float f = jacl_as_f32(new_val); memcpy(struct_base + byte_offset, &f, 4); break; }
          case TYPE_I64: { int64_t n = (int64_t)new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_U64: { uint64_t n = new_val; memcpy(struct_base + byte_offset, &n, 8); break; }
          case TYPE_F64: { double d; memcpy(&d, &new_val, 8); memcpy(struct_base + byte_offset, &d, 8); break; }
          default: { memcpy(struct_base + byte_offset, &new_val, sizeof(JaclVal)); break; }
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_MATERIALIZE_UPVALUE: {
        /* US-008: Convert a closure-captured inline struct to a heap-allocated HeapRecord.
           Same as OP_STRUCT_MATERIALIZE but base is frame->closure->upvalues[base_uv_slot]. */
        uint8_t base_uv_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for materialize_upvalue", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        gc__current_heap = &vm->heap;
        HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                sizeof(HeapRecord) + sdef->total_size);
        s->type_idx = type_idx;
        s->total_size = sdef->total_size;
        uint8_t* struct_base = (uint8_t*)&frame->closure->upvalues[base_uv_slot];
        memcpy(s->data, struct_base, sdef->total_size);
        result = vm__push(vm, jacl_heap_record_val(s));
        if (result != VM_OK) return result;
        break;
      }

      case OP_LOAD_INLINE_LOCAL: {
        /* Copy N inline struct slots from a local to TOS, no heap alloc.
           Operands: uint8_t base_slot, uint16_t type_idx. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for load_inline_local", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_error(vm, "stack overflow (load_inline_local)");
          return VM_STACK_OVERFLOW;
        }
        uint8_t* src = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], src, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        break;
      }

      case OP_STRUCT_GET_INLINE_TOS: {
        /* Pop an inline struct from TOS, push field value.
           Operands: u16 type_idx, u16 byte_offset, u8 field_type
           [+ u16 sub_type_idx if field_type == TYPE_STRUCT]. */
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t byte_offset = vm__read_u16(vm);
        uint8_t field_type = vm__read_byte(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for get_inline_tos", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, slots);
        uint8_t* base = (uint8_t*)slots;
        JaclVal field_val;
        switch ((JaclType)field_type) {
          case TYPE_BOOL: field_val = jacl_bool(base[byte_offset]); break;
          case TYPE_I32: { int32_t n; memcpy(&n, base + byte_offset, 4); field_val = jacl_i32(n); break; }
          case TYPE_U32: { uint32_t n; memcpy(&n, base + byte_offset, 4); field_val = jacl_u32(n); break; }
          case TYPE_F32: { float f; memcpy(&f, base + byte_offset, 4); field_val = jacl_f32(f); break; }
          case TYPE_I64: { int64_t n; memcpy(&n, base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_U64: { uint64_t n; memcpy(&n, base + byte_offset, 8); field_val = (JaclVal)n; break; }
          case TYPE_F64: { double d; memcpy(&d, base + byte_offset, 8); memcpy(&field_val, &d, 8); break; }
          case TYPE_STRUCT: {
            uint16_t sub_type_idx = vm__read_u16(vm);
            if (sub_type_idx >= vm->struct_registry->count) {
              vm__set_error(vm, "invalid sub-struct type index %u", (unsigned)sub_type_idx);
              return VM_RUNTIME_ERROR;
            }
            StructTypeDef* sub = vm->struct_registry->defs[sub_type_idx];
            uint32_t sub_width = (sub->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
            if (vm->stack_top + sub_width > VM_STACK_MAX) {
              vm__set_error(vm, "stack overflow (get_inline_tos nested)");
              return VM_STACK_OVERFLOW;
            }
            memset(&vm->stack[vm->stack_top], 0, sub_width * sizeof(JaclVal));
            memcpy(&vm->stack[vm->stack_top], base + byte_offset, sub->total_size);
            for (uint32_t si = 0; si < sub_width; si++) {
              BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
            }
            vm->stack_top += sub_width;
            break;  /* nothing else to push */
          }
          default: { memcpy(&field_val, base + byte_offset, sizeof(JaclVal)); break; }
        }
        if ((JaclType)field_type != TYPE_STRUCT) {
          result = vm__push(vm, field_val);
          if (result != VM_OK) return result;
        }
        (void)sdef;
        break;
      }

      case OP_STRUCT_EQ_TOS: {
        /* Pop two structs from TOS (rhs first, then lhs), memcmp, push bool.
           Each can be inline or heap — vm__pop_struct dispatches.
           Operand: uint16_t type_idx. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for struct_eq", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        JaclVal rhs[VM_MAX_STRUCT_SLOTS];
        JaclVal lhs[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, rhs);
        vm__pop_struct(vm, type_idx, lhs);
        bool eq = (memcmp(lhs, rhs, sdef->total_size) == 0);
        result = vm__push(vm, jacl_bool(eq));
        if (result != VM_OK) return result;
        break;
      }

      case OP_PRINT_STRUCT: {
        /* Pop N inline struct slots, format Name{f: v, ...}, print + newline.
           Operand: uint16_t type_idx. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for print_struct", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint8_t scratch[VM_MAX_STRUCT_SLOTS * sizeof(JaclVal)];
        if (sdef->total_size > sizeof(scratch)) {
          vm__set_error(vm, "print: struct too large");
          return VM_RUNTIME_ERROR;
        }
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, slots);
        memcpy(scratch, slots, sdef->total_size);
        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_struct_bytes(&fmt, sdef, scratch);
        vm__fmt_append(&fmt, "\n", 1);
        vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_LOAD_INLINE_UPVALUE: {
        /* Copy N inline struct slots from a closure upvalue to TOS, no heap alloc.
           Operands: uint8_t base_uv_slot, uint16_t type_idx. */
        uint8_t base_uv_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for load_inline_upvalue", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_error(vm, "stack overflow (load_inline_upvalue)");
          return VM_STACK_OVERFLOW;
        }
        uint8_t* src = (uint8_t*)&frame->closure->upvalues[base_uv_slot];
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], src, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        break;
      }

      case OP_STRUCT_EXPAND: {
        /* Phase 5a: Pop heap HeapRecord from stack top, push raw bytes as N inline slots.
           Operand: uint16_t type_idx. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for expand", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        /* Pop the heap struct pointer */
        JaclVal heap_val = vm->stack[--vm->stack_top];
        if (!jacl_is_struct(heap_val)) {
          vm__set_error(vm, "OP_STRUCT_EXPAND expects struct value");
          return VM_RUNTIME_ERROR;
        }
        HeapRecord* src = jacl_as_heap_record_ptr(heap_val);
        /* Push N inline slots (zero-fill then copy raw bytes) */
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], src->data, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        break;
      }

      case OP_STRUCT_REIFY: {
        /* Phase 5b: Convert TOS inline bytes to heap HeapRecord.
           Operand: uint16_t type_idx.
           For width==1 structs: pops 1 inline slot, pushes 1 heap pointer. */
        uint16_t type_idx = vm__read_u16(vm);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "invalid struct type index %u for reify", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        /* Read inline bytes from TOS */
        uint8_t* src = (uint8_t*)&vm->stack[vm->stack_top - width];
        /* Allocate heap struct */
        gc__current_heap = &vm->heap;
        HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                sizeof(HeapRecord) + sdef->total_size);
        s->type_idx = type_idx;
        s->total_size = sdef->total_size;
        memcpy(s->data, src, sdef->total_size);
        /* Phase 5c: fix up nested struct fields — convert raw data to
           heap HeapRecord* pointers so OP_STRUCT_GET works uniformly.
           Recursive: nested structs may themselves have nested fields. */
        vm__reify_nested_heap_records(vm, s, sdef);
        /* Clear inline bitmap, pop width slots, push heap pointer */
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, vm->stack_top - width + si);
        }
        vm->stack_top -= width;
        result = vm__push(vm, jacl_heap_record_val(s));
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_EQ_INLINE: {
        /* US-013: Compare two stack-resident inline structs via memcmp.
           Operands: uint8_t base_a, uint8_t base_b, uint16_t total_size.
           Pushes bool result. */
        uint8_t base_a = vm__read_byte(vm);
        uint8_t base_b = vm__read_byte(vm);
        uint16_t total_size = vm__read_u16(vm);
        uint8_t* bytes_a = (uint8_t*)&vm->stack[frame->stack_base + base_a];
        uint8_t* bytes_b = (uint8_t*)&vm->stack[frame->stack_base + base_b];
        bool eq = (memcmp(bytes_a, bytes_b, total_size) == 0);
        result = vm__push(vm, jacl_bool(eq));
        if (result != VM_OK) return result;
        break;
      }

      case OP_STRUCT_HASH_INLINE: {
        /* US-013: Hash a stack-resident inline struct's raw bytes.
           Operands: uint8_t base_slot, uint16_t total_size, uint16_t type_idx.
           Pushes i32 hash result. */
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t total_size = vm__read_u16(vm);
        uint16_t type_idx = vm__read_u16(vm);
        uint8_t* bytes = (uint8_t*)&vm->stack[frame->stack_base + base_slot];
        uint32_t h = (uint32_t)type_idx * 0x9E3779B9u;
        for (uint16_t i = 0; i < total_size; i++) {
          h = h * 31 + bytes[i];
        }
        result = vm__push(vm, jacl_i32((int32_t)h));
        if (result != VM_OK) return result;
        break;
      }

      case OP_HASH: {
        /* US-013: Generic hash — pop any value, push i32 hash. */
        JaclVal val;
        result = vm__pop(vm, &val);
        if (result != VM_OK) return result;
        uint32_t h = jacl_val_hash(val);
        result = vm__push(vm, jacl_i32((int32_t)h));
        if (result != VM_OK) return result;
        break;
      }

      case OP_SPREAD: {
        JaclVal spread_val;
        result = vm__pop(vm, &spread_val);
        if (result != VM_OK) return result;

        uint32_t len = 0;

        if (jacl_is_vector(spread_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(spread_val);
          len = jacl_vec_count(vec);
          if (vm->stack_top + len >= VM_STACK_MAX) {
            vm__set_error(vm, "spread exceeds stack capacity");
            return VM_RUNTIME_ERROR;
          }
          for (uint32_t i = 0; i < len; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);
            result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
            if (result != VM_OK) return result;
          }
        } else if (jacl_is_stream(spread_val)) {
          /* Eagerly consume stream, collect into vector, then push elements */
          JaclStream* stream = jacl_as_stream(spread_val);

          if (stream->kind != STREAM_KIND_GENERATOR) {
            /* Derived stream: use pull helper */
            gc__current_heap = &vm->heap;
            jacl_vec_root* tmp_vec = jacl_vec_empty();
            while (stream->state != STREAM_EXHAUSTED) {
              JaclVal elem;
              StreamPullResult pr = vm__pull_stream_one(vm, spread_val, &elem);
              if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
              if (pr == STREAM_PULL_EXHAUSTED) break;
              gc__current_heap = &vm->heap;
              tmp_vec = jacl_vec_push_back(tmp_vec, elem);
            }
            frame = &vm->frames[vm->frame_count - 1];
            len = jacl_vec_count(tmp_vec);
            if (vm->stack_top + len >= VM_STACK_MAX) {
              vm__set_error(vm, "spread exceeds stack capacity");
              return VM_RUNTIME_ERROR;
            }
            for (uint32_t i = 0; i < len; i++) {
              jacl_vec_get_result gr = jacl_vec_get(tmp_vec, i);
              result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
              if (result != VM_OK) return result;
            }
          } else {
            /* Generator stream: pull all values */
            if (jacl_is_nil(stream->state_machine)) {
              vm__set_error(vm, "CPS generator not supported (use state machine path)");
              return VM_RUNTIME_ERROR;
            }
            gc__current_heap = &vm->heap;
            jacl_vec_root* tmp_vec = jacl_vec_empty();

            while (stream->state != STREAM_EXHAUSTED) {
              uint32_t caller_stack_top = vm->stack_top;
              uint32_t caller_frame_count = vm->frame_count;
              uint8_t* caller_ip = vm->ip;
              BytecodeChunk* caller_chunk = vm->chunk;

              /* SM generator: call sm_closure(state_obj, nil) each time */
              JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
              JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);
              result = vm__push(vm, sm->sm_closure);
              if (result != VM_OK) return result;
              result = vm__push(vm, stream->state_machine);
              if (result != VM_OK) return result;
              result = vm__push(vm, JACL_NIL);
              if (result != VM_OK) return result;
              if (vm->frame_count >= VM_FRAMES_MAX) {
                vm__set_error(vm, "stack overflow");
                return VM_RUNTIME_ERROR;
              }
              CallFrame* new_frame = &vm->frames[vm->frame_count++];
              new_frame->closure    = sm_cl;
              new_frame->return_ip  = NULL;
              new_frame->stack_base = vm->stack_top - 2;
              new_frame->chunk      = &sm_cl->chunk;
              vm->ip    = sm_cl->chunk.code;
              vm->chunk = &sm_cl->chunk;

              VMResult inner = vm__run(vm, caller_frame_count);

              if (inner == VM_YIELD) {
                stream->state = STREAM_CONSUMED;
                stream->cached_value = vm->yield_value;
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip    = caller_ip;
                vm->chunk = caller_chunk;
                frame = &vm->frames[vm->frame_count - 1];
                gc__current_heap = &vm->heap;
                tmp_vec = jacl_vec_push_back(tmp_vec, vm->yield_value);
              } else if (inner == VM_OK) {
                stream->state = STREAM_EXHAUSTED;
                stream->next_fn = JACL_NIL;
                stream->cached_value = JACL_NIL;
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip    = caller_ip;
                vm->chunk = caller_chunk;
                frame = &vm->frames[vm->frame_count - 1];
              } else {
                stream->state = STREAM_ERROR;
                stream->next_fn = JACL_NIL;
                return inner;
              }
            }

            len = jacl_vec_count(tmp_vec);
            if (vm->stack_top + len >= VM_STACK_MAX) {
              vm__set_error(vm, "spread exceeds stack capacity");
              return VM_RUNTIME_ERROR;
            }
            for (uint32_t i = 0; i < len; i++) {
              jacl_vec_get_result gr = jacl_vec_get(tmp_vec, i);
              result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
              if (result != VM_OK) return result;
            }
          }
        } else {
          vm__set_error(vm, "spread requires a vector or stream, got %s",
                       vm__type_name(spread_val));
          return VM_RUNTIME_ERROR;
        }

        /* Save count in side buffer */
        if (vm->spread_count_top >= 32) {
          vm__set_error(vm, "too many nested spread operations");
          return VM_RUNTIME_ERROR;
        }
        vm->spread_counts[vm->spread_count_top++] = len;
        break;
      }

      case OP_CALL_SPREAD: {
        uint8_t fixed_args = vm__read_byte(vm);
        uint8_t num_spreads = vm__read_byte(vm);
        /* Compute total args */
        uint32_t total_args = fixed_args;
        for (uint8_t i = 0; i < num_spreads; i++) {
          if (vm->spread_count_top == 0) {
            vm__set_error(vm, "spread count underflow");
            return VM_RUNTIME_ERROR;
          }
          total_args += vm->spread_counts[--vm->spread_count_top];
        }
        /* Locate callee */
        JaclVal callee = vm->stack[vm->stack_top - total_args - 1];

        if (jacl_is_native_fn(callee)) {
          uint32_t fn_idx = jacl_as_native_fn_index(callee);
          if (fn_idx >= vm->native_fn_count || !vm->call_native) {
            vm__set_error(vm, "invalid native function index %u", fn_idx);
            return VM_RUNTIME_ERROR;
          }
          int8_t arity = vm->native_fn_arities[fn_idx];
          if (arity >= 0 && total_args != (uint32_t)arity) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)arity, (int)total_args);
            return VM_RUNTIME_ERROR;
          }
          JaclVal* args = &vm->stack[vm->stack_top - total_args];
          JaclVal ret = vm->call_native(vm->native_fn_ctx, fn_idx,
                                         args, (int)total_args);
          vm->stack_top -= (total_args + 1);
          result = vm__push(vm, ret);
          if (result != VM_OK) return result;
          break;
        }

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "cannot call %s value", vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure* closure = jacl_as_closure(callee);
        if (closure->variadic) {
          if (total_args < closure->min_args) {
            vm__set_error(vm, "expected at least %d arguments but got %d",
                         (int)closure->min_args, (int)total_args);
            return VM_RUNTIME_ERROR;
          }
        } else if (total_args != closure->param_count) {
          vm__set_error(vm, "expected %d arguments but got %d",
                       (int)closure->param_count, (int)total_args);
          return VM_RUNTIME_ERROR;
        }
        if (vm->frame_count >= VM_FRAMES_MAX) {
          vm__set_error(vm, "stack overflow");
          return VM_RUNTIME_ERROR;
        }

        CallFrame* new_frame = &vm->frames[vm->frame_count++];
        new_frame->closure    = closure;
        new_frame->return_ip  = vm->ip;
        new_frame->stack_base = vm->stack_top - total_args;
        new_frame->chunk      = &closure->chunk;
        frame     = new_frame;
        vm->ip    = frame->chunk->code;
        vm->chunk = frame->chunk;
        break;
      }

      case OP_FOLD_SPREAD: {
        uint8_t op_id = vm__read_byte(vm);
        uint8_t fixed_args = vm__read_byte(vm);
        uint8_t num_spreads = vm__read_byte(vm);
        /* Compute total args */
        uint32_t total_args = fixed_args;
        for (uint8_t i = 0; i < num_spreads; i++) {
          if (vm->spread_count_top == 0) {
            vm__set_error(vm, "spread count underflow");
            return VM_RUNTIME_ERROR;
          }
          total_args += vm->spread_counts[--vm->spread_count_top];
        }
        if (total_args < 1) {
          vm__set_error(vm, "fold requires at least 1 argument");
          return VM_RUNTIME_ERROR;
        }
        /* Fold values on stack left-to-right */
        uint32_t base = vm->stack_top - total_args;
        JaclVal acc = vm->stack[base];
        for (uint32_t i = 1; i < total_args; i++) {
          JaclVal val = vm->stack[base + i];
          JaclVal res;
          if (jacl_is_i32(acc) && jacl_is_i32(val)) {
            switch (op_id) {
              case 0: res = jacl_add_i32(acc, val); break;
              case 1: res = jacl_sub_i32(acc, val); break;
              case 2: res = jacl_mul_i32(acc, val); break;
              case 3: res = jacl_div_i32(acc, val); break;
              default: vm__set_error(vm, "unknown fold op"); return VM_RUNTIME_ERROR;
            }
          } else if (jacl_is_f32(acc) && jacl_is_f32(val)) {
            switch (op_id) {
              case 0: res = jacl_add_f32(acc, val); break;
              case 1: res = jacl_sub_f32(acc, val); break;
              case 2: res = jacl_mul_f32(acc, val); break;
              case 3: res = jacl_div_f32(acc, val); break;
              default: vm__set_error(vm, "unknown fold op"); return VM_RUNTIME_ERROR;
            }
          } else if (jacl_is_u32(acc) && jacl_is_u32(val)) {
            switch (op_id) {
              case 0: res = jacl_u32_add(acc, val); break;
              case 1: res = jacl_u32_sub(acc, val); break;
              case 2: res = jacl_u32_mul(acc, val); break;
              case 3: res = jacl_u32_div(acc, val); break;
              default: vm__set_error(vm, "unknown fold op"); return VM_RUNTIME_ERROR;
            }
          } else {
            const char* op_names[] = { "+", "-", "*", "/" };
            vm__set_error(vm,
              "type error in '%s': expected matching numeric types, got %s and %s",
              op_names[op_id < 4 ? op_id : 0], vm__type_name(acc), vm__type_name(val));
            return VM_RUNTIME_ERROR;
          }
          if (jacl_is_error(res)) {
            vm__capture_trace(vm);
          }
          acc = res;
        }
        vm->stack_top = base;
        result = vm__push(vm, acc);
        if (result != VM_OK) return result;
        break;
      }

      case OP_YIELD: {
        /* CPS yield removed — all generators use OP_YIELD_SM now */
        vm__set_error(vm, "CPS yield not supported (use state machine path)");
        return VM_RUNTIME_ERROR;
      }

      case OP_STREAM_NEXT: {
        /* stream_next: pulls next value from a generator stream.
           Supports both CPS-based and state-machine-based generators. */
        JaclVal stream_val;
        result = vm__pop(vm, &stream_val);
        if (result != VM_OK) return result;
        if (!jacl_is_stream(stream_val)) {
          vm__set_error(vm, "stream_next requires a stream, got %s",
                       vm__type_name(stream_val));
          return VM_RUNTIME_ERROR;
        }
        JaclStream* stream = jacl_as_stream(stream_val);
        if (stream->state == STREAM_EXHAUSTED) {
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
          break;
        }

        /* Derived streams (filter, etc.) use the unified helper */
        if (stream->kind != STREAM_KIND_GENERATOR) {
          JaclVal pulled;
          StreamPullResult pr = vm__pull_stream_one(vm, stream_val, &pulled);
          if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
          result = vm__push(vm, pulled);
          if (result != VM_OK) return result;
          frame = &vm->frames[vm->frame_count - 1];
          break;
        }

        /* Save caller context */
        uint32_t caller_stack_top = vm->stack_top;
        uint32_t caller_frame_count = vm->frame_count;
        uint8_t* caller_ip = vm->ip;
        BytecodeChunk* caller_chunk = vm->chunk;

        /* --- State machine generator path --- */
        if (!jacl_is_nil(stream->state_machine)) {
          JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
          JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);

          /* SM generators always call the same way: sm_closure(state_obj, nil) */
          result = vm__push(vm, sm->sm_closure);
          if (result != VM_OK) return result;
          result = vm__push(vm, stream->state_machine);
          if (result != VM_OK) return result;
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_error(vm, "stack overflow");
            return VM_RUNTIME_ERROR;
          }
          CallFrame* new_frame = &vm->frames[vm->frame_count++];
          new_frame->closure    = sm_cl;
          new_frame->return_ip  = NULL;
          new_frame->stack_base = vm->stack_top - 2; /* 2 params: state_obj + resume_value */
          new_frame->chunk      = &sm_cl->chunk;
          vm->ip    = sm_cl->chunk.code;
          vm->chunk = &sm_cl->chunk;

          VMResult inner = vm__run(vm, caller_frame_count);

          if (inner == VM_YIELD) {
            stream->state = STREAM_CONSUMED;
            stream->cached_value = vm->yield_value;
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, vm->yield_value);
            if (result != VM_OK) return result;
            break;
          } else if (inner == VM_OK) {
            /* Check if SM function returned an error value (error propagated
               through OP_CHECK_ERROR frame unwinding returns VM_OK). */
            JaclVal sm_ret = JACL_NIL;
            if (vm->stack_top > caller_stack_top) {
              sm_ret = vm->stack[vm->stack_top - 1];
            }
            if (jacl_is_error(sm_ret)) {
              stream->state = STREAM_ERROR;
              vm->stack_top   = caller_stack_top;
              vm->frame_count = caller_frame_count;
              vm->ip    = caller_ip;
              vm->chunk = caller_chunk;
              frame = &vm->frames[vm->frame_count - 1];
              result = vm__push(vm, sm_ret);
              if (result != VM_OK) return result;
              break;
            }
            stream->state = STREAM_EXHAUSTED;
            stream->cached_value = JACL_NIL;
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
            break;
          } else {
            stream->state = STREAM_ERROR;
            return inner;
          }
        }

        /* All generators use state machine path (CPS removed) */
        vm__set_error(vm, "generator stream missing state machine object");
        return VM_RUNTIME_ERROR;
      }

      case OP_COLLECT: {
        /* collect: materialize a stream into a vector, or identity on vectors */
        JaclVal coll_val;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;

        if (jacl_is_vector(coll_val)) {
          /* Identity: return vector unchanged */
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          break;
        }

        if (!jacl_is_stream(coll_val)) {
          vm__set_error(vm, "collect requires a stream or vector, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }

        JaclStream* stream = jacl_as_stream(coll_val);

        /* US-004/US-005: Exec streams collect to a single string (concatenated stdout)
         * or return an error value if the command fails */
        if (stream->kind == STREAM_KIND_EXEC) {
          FILE* fp = (FILE*)(uintptr_t)stream->args[0];
          if (!fp) {
            /* Already exhausted - check for cached error */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
              result = vm__push(vm, stream->cached_value);
              stream->cached_value = JACL_NIL;
              if (result != VM_OK) return result;
              break;
            }
            result = vm__push(vm, jacl_inline_string("", 0));
            if (result != VM_OK) return result;
            break;
          }

          /* Read all remaining output into arena-allocated buffer */
          size_t capacity = 4096;
          size_t length = 0;
          char* buffer = (char*)arena_alloc(vm->arena, capacity);

          while (1) {
            size_t avail = capacity - length - 1;
            if (avail < 256) {
              /* Grow buffer */
              size_t new_cap = capacity * 2;
              char* new_buf = (char*)arena_alloc(vm->arena, new_cap);
              memcpy(new_buf, buffer, length);
              buffer = new_buf;
              capacity = new_cap;
              avail = capacity - length - 1;
            }
            size_t nread = fread(buffer + length, 1, avail, fp);
            if (nread == 0) break;
            length += nread;
          }
          buffer[length] = '\0';

          /* Close the pipe and check exit status */
          int pclose_status = pclose(fp);
          stream->args[0] = (JaclVal)0;

          /* US-005: Check exit code and handle errors */
          int exit_code = WIFEXITED(pclose_status) ? WEXITSTATUS(pclose_status) : 1;

          /* Read and clean up stderr temp file */
          char stderr_path[64];
          if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
            uint32_t path_len = jacl_string_byte_len(stream->args[2]);
            jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
            stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
          } else {
            stderr_path[0] = '\0';
          }

          /* US-007: Clean up stdin temp file if present */
          if (stream->arg_count >= 4 && jacl_is_string(stream->args[3])) {
            char stdin_path[64];
            uint32_t path_len = jacl_string_byte_len(stream->args[3]);
            jacl_string_data(stream->args[3], stdin_path, sizeof(stdin_path));
            stdin_path[path_len < sizeof(stdin_path) - 1 ? path_len : sizeof(stdin_path) - 1] = '\0';
            unlink(stdin_path);
          }

          if (exit_code != 0) {
            /* Read stderr content */
            char stderr_buf[4096] = "";
            if (stderr_path[0] != '\0') {
              FILE* stderr_fp = fopen(stderr_path, "r");
              if (stderr_fp) {
                size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                stderr_buf[nread] = '\0';
                fclose(stderr_fp);
              }
              unlink(stderr_path);  /* clean up temp file */
            }

            /* Create error value with stderr as message */
            gc__current_heap = &vm->heap;
            JaclVal err_msg;
            if (stderr_buf[0] != '\0') {
              /* Trim trailing newline */
              size_t len = strlen(stderr_buf);
              while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                stderr_buf[--len] = '\0';
              }
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        stderr_buf, (uint32_t)len);
            } else {
              /* Default message if no stderr */
              char default_msg[64];
              snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        default_msg, (uint32_t)strlen(default_msg));
            }

            stream->state = STREAM_ERROR;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, jacl_set_error(err_msg));
            if (result != VM_OK) return result;
            break;
          }

          /* Success - clean up stderr temp file */
          if (stderr_path[0] != '\0') {
            unlink(stderr_path);
          }

          stream->state = STREAM_EXHAUSTED;

          /* Create the result string */
          gc__current_heap = &vm->heap;
          JaclVal result_str;
          if (length == 0) {
            result_str = jacl_inline_string("", 0);
          } else {
            result_str = jacl_string_new(&vm->heap, vm->intern_table,
                                         buffer, (uint32_t)length);
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, result_str);
          if (result != VM_OK) return result;
          break;
        }

        /* US-006: Exec buffer streams collect to the full string (not lines vector) */
        if (stream->kind == STREAM_KIND_EXEC_BUFFER) {
          /* args[0] contains the pre-collected stdout string */
          JaclVal stdout_str = stream->args[0];
          stream->state = STREAM_EXHAUSTED;
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, stdout_str);
          if (result != VM_OK) return result;
          break;
        }

        /* US-009: Exec pipe streams collect all output from pipeline */
        if (stream->kind == STREAM_KIND_EXEC_PIPE) {
          FILE* fp = (FILE*)(uintptr_t)stream->args[0];
          if (!fp) {
            /* Already exhausted - check for cached error */
            if (stream->state == STREAM_ERROR && stream->cached_value != JACL_NIL) {
              result = vm__push(vm, stream->cached_value);
              stream->cached_value = JACL_NIL;
              if (result != VM_OK) return result;
              break;
            }
            result = vm__push(vm, jacl_inline_string("", 0));
            if (result != VM_OK) return result;
            break;
          }

          /* Read all remaining output */
          size_t capacity = 4096;
          size_t length = 0;
          char* buffer = (char*)arena_alloc(vm->arena, capacity);

          while (1) {
            size_t avail = capacity - length - 1;
            if (avail < 256) {
              size_t new_cap = capacity * 2;
              char* new_buf = (char*)arena_alloc(vm->arena, new_cap);
              memcpy(new_buf, buffer, length);
              buffer = new_buf;
              capacity = new_cap;
              avail = capacity - length - 1;
            }
            size_t nread = fread(buffer + length, 1, avail, fp);
            if (nread == 0) break;
            length += nread;
          }
          buffer[length] = '\0';

          /* Close file and wait for all children */
          fclose(fp);
          stream->args[0] = (JaclVal)0;

          /* Parse PIDs and wait */
          int exit_code = 0;
          if (jacl_is_string(stream->args[1])) {
            char pid_buf[256];
            uint32_t pid_len = jacl_string_byte_len(stream->args[1]);
            jacl_string_data(stream->args[1], pid_buf, sizeof(pid_buf));
            pid_buf[pid_len < sizeof(pid_buf) - 1 ? pid_len : sizeof(pid_buf) - 1] = '\0';

            char* p = pid_buf;
            int cmd_count = (int)strtol(p, &p, 10);
            for (int i = 0; i < cmd_count && *p == ','; i++) {
              p++;
              pid_t pid = (pid_t)strtol(p, &p, 10);
              int status;
              waitpid(pid, &status, 0);
              if (i == cmd_count - 1) {
                exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
              }
            }
          }

          /* Handle stderr temp file */
          char stderr_path[64];
          if (stream->arg_count >= 3 && jacl_is_string(stream->args[2])) {
            uint32_t path_len = jacl_string_byte_len(stream->args[2]);
            jacl_string_data(stream->args[2], stderr_path, sizeof(stderr_path));
            stderr_path[path_len < sizeof(stderr_path) - 1 ? path_len : sizeof(stderr_path) - 1] = '\0';
          } else {
            stderr_path[0] = '\0';
          }

          if (exit_code != 0) {
            char stderr_buf[4096] = "";
            if (stderr_path[0] != '\0') {
              FILE* stderr_fp = fopen(stderr_path, "r");
              if (stderr_fp) {
                size_t nread = fread(stderr_buf, 1, sizeof(stderr_buf) - 1, stderr_fp);
                stderr_buf[nread] = '\0';
                fclose(stderr_fp);
              }
              unlink(stderr_path);
            }

            gc__current_heap = &vm->heap;
            JaclVal err_msg;
            if (stderr_buf[0] != '\0') {
              size_t len = strlen(stderr_buf);
              while (len > 0 && (stderr_buf[len-1] == '\n' || stderr_buf[len-1] == '\r')) {
                stderr_buf[--len] = '\0';
              }
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        stderr_buf, (uint32_t)len);
            } else {
              char default_msg[64];
              snprintf(default_msg, sizeof(default_msg), "command exited with code %d", exit_code);
              err_msg = jacl_string_new(&vm->heap, vm->intern_table,
                                        default_msg, (uint32_t)strlen(default_msg));
            }

            stream->state = STREAM_ERROR;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, jacl_set_error(err_msg));
            if (result != VM_OK) return result;
            break;
          }

          /* Success */
          if (stderr_path[0] != '\0') {
            unlink(stderr_path);
          }

          stream->state = STREAM_EXHAUSTED;
          gc__current_heap = &vm->heap;
          JaclVal result_str;
          if (length == 0) {
            result_str = jacl_inline_string("", 0);
          } else {
            result_str = jacl_string_new(&vm->heap, vm->intern_table,
                                         buffer, (uint32_t)length);
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, result_str);
          if (result != VM_OK) return result;
          break;
        }

        /* Derived streams (filter, etc.) use the unified pull helper */
        if (stream->kind != STREAM_KIND_GENERATOR) {
          gc__current_heap = &vm->heap;
          jacl_vec_root* collect_vec = jacl_vec_empty();
          while (stream->state != STREAM_EXHAUSTED) {
            JaclVal elem;
            StreamPullResult pr = vm__pull_stream_one(vm, coll_val, &elem);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) break;
            gc__current_heap = &vm->heap;
            collect_vec = jacl_vec_push_back(collect_vec, elem);
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, jacl_vector_ptr(collect_vec));
          if (result != VM_OK) return result;
          break;
        }

        gc__current_heap = &vm->heap;
        jacl_vec_root* collect_vec = jacl_vec_empty();
        JaclVal sm_error_val = JACL_NIL;

        while (stream->state != STREAM_EXHAUSTED) {
          /* Save caller context */
          uint32_t caller_stack_top = vm->stack_top;
          uint32_t caller_frame_count = vm->frame_count;
          uint8_t* caller_ip = vm->ip;
          BytecodeChunk* caller_chunk = vm->chunk;

          /* State machine generator: call sm_closure(state_obj, nil) */
          JaclStateMachine* sm = jacl_as_state_machine(stream->state_machine);
          JaclClosure* sm_cl = jacl_as_closure(sm->sm_closure);

          result = vm__push(vm, sm->sm_closure);
          if (result != VM_OK) return result;
          result = vm__push(vm, stream->state_machine);
          if (result != VM_OK) return result;
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_error(vm, "stack overflow");
            return VM_RUNTIME_ERROR;
          }
          CallFrame* new_frame = &vm->frames[vm->frame_count++];
          new_frame->closure    = sm_cl;
          new_frame->return_ip  = NULL;
          new_frame->stack_base = vm->stack_top - 2;
          new_frame->chunk      = &sm_cl->chunk;
          vm->ip    = sm_cl->chunk.code;
          vm->chunk = &sm_cl->chunk;

          VMResult inner = vm__run(vm, caller_frame_count);

          if (inner == VM_YIELD) {
            stream->state = STREAM_CONSUMED;
            stream->cached_value = vm->yield_value;
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
            gc__current_heap = &vm->heap;
            collect_vec = jacl_vec_push_back(collect_vec, vm->yield_value);
          } else if (inner == VM_OK) {
            /* Check if SM function returned an error value */
            if (vm->stack_top > caller_stack_top) {
              JaclVal sm_ret = vm->stack[vm->stack_top - 1];
              if (jacl_is_error(sm_ret)) {
                stream->state = STREAM_ERROR;
                sm_error_val = sm_ret;
                vm->stack_top   = caller_stack_top;
                vm->frame_count = caller_frame_count;
                vm->ip    = caller_ip;
                vm->chunk = caller_chunk;
                frame = &vm->frames[vm->frame_count - 1];
                break;
              }
            }
            stream->state = STREAM_EXHAUSTED;
            stream->cached_value = JACL_NIL;
            vm->stack_top   = caller_stack_top;
            vm->frame_count = caller_frame_count;
            vm->ip    = caller_ip;
            vm->chunk = caller_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          } else {
            stream->state = STREAM_ERROR;
            return inner;
          }
        }

        if (!jacl_is_nil(sm_error_val)) {
          result = vm__push(vm, sm_error_val);
        } else {
          result = vm__push(vm, jacl_vector_ptr(collect_vec));
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_IS_STREAM_EXHAUSTED: {
        /* Pop a value. Push true if it's an exhausted stream, else false. */
        JaclVal sv;
        result = vm__pop(vm, &sv);
        if (result != VM_OK) return result;
        bool exhausted = false;
        if (jacl_is_stream(sv)) {
          JaclStream* s = jacl_as_stream(sv);
          exhausted = (s->state == STREAM_EXHAUSTED);
        }
        result = vm__push(vm, exhausted ? JACL_TRUE : JACL_FALSE);
        if (result != VM_OK) return result;
        break;
      }

      case OP_COUNT: {
        JaclVal coll_val;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) {
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          break;
        }

        if (jacl_is_vector(coll_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          result = vm__push(vm, jacl_i32((int32_t)jacl_vec_count(vec)));
          if (result != VM_OK) return result;
          break;
        }

        if (jacl_is_stream(coll_val)) {
          JaclStream* stream = jacl_as_stream(coll_val);
          int32_t cnt = 0;

          /* Use unified pull helper for all stream kinds */
          while (stream->state != STREAM_EXHAUSTED) {
            JaclVal elem;
            StreamPullResult pr = vm__pull_stream_one(vm, coll_val, &elem);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) break;
            cnt++;
          }
          frame = &vm->frames[vm->frame_count - 1];
          result = vm__push(vm, jacl_i32(cnt));
          if (result != VM_OK) return result;
          break;
        }

        vm__set_error(vm, "count requires a vector or stream, got %s",
                     vm__type_name(coll_val));
        return VM_RUNTIME_ERROR;
      }

      case OP_TAKE: {
        JaclVal n_val, coll_val;
        result = vm__pop(vm, &n_val);
        if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) {
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          break;
        }
        if (jacl_is_error(n_val)) {
          result = vm__push(vm, n_val);
          if (result != VM_OK) return result;
          break;
        }

        if (!jacl_is_i32(n_val)) {
          vm__set_error(vm, "take requires an integer count, got %s",
                       vm__type_name(n_val));
          return VM_RUNTIME_ERROR;
        }
        int32_t n = jacl_as_i32(n_val);
        if (n < 0) n = 0;

        if (jacl_is_vector(coll_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          uint32_t len = jacl_vec_count(vec);
          uint32_t take_n = (uint32_t)n < len ? (uint32_t)n : len;
          if (take_n == 0) {
            gc__current_heap = &vm->heap;
            result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
          } else {
            gc__current_heap = &vm->heap;
            jacl_vec_root* new_vec = jacl_vec_slice(vec, 0, take_n);
            if (new_vec == NULL) {
              result = vm__push(vm, jacl_vector_ptr(jacl_vec_empty()));
            } else {
              result = vm__push(vm, jacl_vector_ptr(new_vec));
            }
          }
          if (result != VM_OK) return result;
          break;
        }

        if (jacl_is_stream(coll_val)) {
          JaclVal take_stream_val = jacl_stream(&vm->heap);
          JaclStream* ts = jacl_as_stream(take_stream_val);
          ts->kind      = STREAM_KIND_TAKE;
          ts->args[0]   = coll_val;     /* source stream */
          ts->args[1]   = jacl_i32(n);  /* remaining count */
          ts->arg_count  = 2;
          result = vm__push(vm, take_stream_val);
          if (result != VM_OK) return result;
          break;
        }

        vm__set_error(vm, "take requires a vector or stream, got %s",
                     vm__type_name(coll_val));
        return VM_RUNTIME_ERROR;
      }

      case OP_FIRST: {
        JaclVal coll_val;
        result = vm__pop(vm, &coll_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) {
          result = vm__push(vm, coll_val);
          if (result != VM_OK) return result;
          break;
        }

        if (jacl_is_vector(coll_val)) {
          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(coll_val);
          if (jacl_vec_count(vec) == 0) {
            result = vm__push(vm, JACL_NIL);
          } else {
            jacl_vec_get_result gr = jacl_vec_get(vec, 0);
            result = vm__push(vm, gr.found ? gr.value : JACL_NIL);
          }
          if (result != VM_OK) return result;
          break;
        }

        if (jacl_is_stream(coll_val)) {
          JaclStream* stream = jacl_as_stream(coll_val);

          if (stream->state == STREAM_EXHAUSTED) {
            result = vm__push(vm, JACL_NIL);
            if (result != VM_OK) return result;
            break;
          }

          /* Use unified pull helper for all stream kinds */
          {
            JaclVal pulled;
            StreamPullResult pr = vm__pull_stream_one(vm, coll_val, &pulled);
            if (pr == STREAM_PULL_ERROR) return VM_RUNTIME_ERROR;
            if (pr == STREAM_PULL_EXHAUSTED) pulled = JACL_NIL;
            frame = &vm->frames[vm->frame_count - 1];
            result = vm__push(vm, pulled);
            if (result != VM_OK) return result;
          }
          break;
        }

        vm__set_error(vm, "first requires a vector or stream, got %s",
                     vm__type_name(coll_val));
        return VM_RUNTIME_ERROR;
      }

      case OP_LINES: {
        JaclVal str_val;
        result = vm__pop(vm, &str_val);
        if (result != VM_OK) return result;
        if (jacl_is_error(str_val)) {
          result = vm__push(vm, str_val);
          if (result != VM_OK) return result;
          break;
        }

        if (!jacl_is_string(str_val)) {
          vm__set_error(vm, "lines requires a string, got %s",
                       vm__type_name(str_val));
          return VM_RUNTIME_ERROR;
        }

        JaclVal lines_stream_val = jacl_stream(&vm->heap);
        JaclStream* ls = jacl_as_stream(lines_stream_val);
        ls->kind      = STREAM_KIND_LINES;
        ls->args[0]   = str_val;       /* source string */
        ls->args[1]   = jacl_i32(0);   /* current byte index */
        ls->arg_count  = 2;
        result = vm__push(vm, lines_stream_val);
        if (result != VM_OK) return result;
        break;
      }

      case OP_COLLECT_VARIADIC: {
        uint8_t min_arity = vm__read_byte(vm);
        uint32_t actual_count = vm->stack_top - frame->stack_base;
        uint32_t extra = actual_count > min_arity ? actual_count - min_arity : 0;
        /* Create vector from excess args */
        jacl_vec_root* vec = jacl_vec_empty();
        for (uint32_t i = 0; i < extra; i++) {
          vec = jacl_vec_push_back(vec, vm->stack[frame->stack_base + min_arity + i]);
        }
        /* Collapse: set rest param slot and adjust stack_top */
        vm->stack[frame->stack_base + min_arity] = jacl_vector_ptr(vec);
        vm->stack_top = frame->stack_base + min_arity + 1;
        break;
      }

      case OP_GET_STATE_FIELD: {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        result = vm__push(vm, sm->fields[field_index]);
        if (result != VM_OK) return result;
        break;
      }

      case OP_SET_STATE_FIELD: {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal value;
        result = vm__pop(vm, &value);
        if (result != VM_OK) return result;
        sm->fields[field_index] = value;
        break;
      }

      case OP_GET_STATE_FIELD_CELL: {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal cell = sm->fields[field_index];
        JaclMutableRef *ref = jacl_as_cell(cell);
        result = vm__push(vm, MREF_VAL(ref));
        if (result != VM_OK) return result;
        break;
      }

      case OP_SET_STATE_FIELD_CELL: {
        uint8_t field_index = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal cell = sm->fields[field_index];
        JaclMutableRef *ref = jacl_as_cell(cell);
        JaclVal new_val;
        result = vm__pop(vm, &new_val);
        if (result != VM_OK) return result;
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr,
                         MREF_VAL(ref), new_val);
        gc_remembered_set_barrier(vm->remembered_set, cell, new_val);
        MREF_VAL(ref) = new_val;
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_GET_STATE_FIELD_WIDE: {
        /* Copy N consecutive JaclVal slots from SM fields to the stack.
           Used to load a struct stored inline across multiple SM slots. */
        uint8_t base_idx = vm__read_byte(vm);
        uint8_t width    = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        uint32_t push_base = vm->stack_top;
        for (uint8_t i = 0; i < width; i++) {
          result = vm__push(vm, sm->fields[base_idx + i]);
          if (result != VM_OK) return result;
        }
        /* US-014: mark pushed slots as raw struct bytes */
        for (uint32_t si = push_base; si < push_base + width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, si);
        }
        break;
      }

      case OP_SET_STATE_FIELD_WIDE: {
        /* Copy N consecutive JaclVal slots from stack top into SM fields.
           The top-of-stack holds the raw struct data across width slots.
           Byte-offset addressing: sm->fields[base_idx..base_idx+width-1]
           map to the struct's raw data as JaclVal-sized chunks. */
        uint8_t base_idx = vm__read_byte(vm);
        uint8_t width    = vm__read_byte(vm);
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        /* Copy from stack (bottom of the N-slot range first) */
        for (uint8_t i = 0; i < width; i++) {
          sm->fields[base_idx + i] = vm->stack[vm->stack_top - width + i];
        }
        /* US-014: mark SM field slots as raw struct bytes */
        for (uint8_t i = 0; i < width; i++) {
          BITMAP_SET(sm->field_inline_bitmap, base_idx + i);
        }
        vm->stack_top -= width;
        break;
      }

      case OP_GET_RESUME_POINT: {
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        result = vm__push(vm, jacl_i32((int32_t)sm->resume_point));
        if (result != VM_OK) return result;
        break;
      }

      case OP_SET_RESUME_POINT: {
        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclStateMachine *sm = jacl_as_state_machine(state_val);
        JaclVal value;
        result = vm__pop(vm, &value);
        if (result != VM_OK) return result;
        sm->resume_point = (uint32_t)jacl_as_i32(value);
        break;
      }

      case OP_YIELD_SM: {
        /* State machine yield: pop yielded value, return VM_YIELD.
           No continuation closure — the SM function is re-entered via
           the dispatch table on the next stream_next call. */
        JaclVal value;
        result = vm__pop(vm, &value);
        if (result != VM_OK) return result;
        vm->yield_value = value;
        return VM_YIELD;
      }

      case OP_AWAIT_SM: {
        /* State machine await: pop value from stack.
           - If it's a Job map: do blocking wait and return result
           - If it's a Future: handle via SM suspension semantics */
        JaclVal val;
        result = vm__pop(vm, &val);
        if (result != VM_OK) return result;

        /* US-010: Check if it's a Job map first */
        if (jacl_is_map(val)) {
          jacl_map_node* m = (jacl_map_node*)jacl_as_ptr(val);
          gc__current_heap = &vm->heap;
          JaclVal marker_key = jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7);
          JaclVal marker_val = jacl_map_get(m, marker_key);

          if (marker_val == JACL_TRUE) {
            /* It's a Job - extract pid and wait (blocking) */
            JaclVal pid_key = jacl_intern(&vm->heap, vm->intern_table, "pid", 3);
            JaclVal pid_val = jacl_map_get(m, pid_key);
            if (!jacl_is_i32(pid_val)) {
              vm__set_error(vm, "invalid job: missing pid");
              return VM_RUNTIME_ERROR;
            }
            pid_t pid = (pid_t)jacl_as_i32(pid_val);

            /* Wait for process to complete */
            int status;
            pid_t waited = waitpid(pid, &status, 0);
            if (waited < 0) {
              vm__set_error(vm, "waitpid failed: %s", strerror(errno));
              return VM_RUNTIME_ERROR;
            }

            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            /* Read stdout from temp file */
            JaclVal stdout_key = jacl_intern(&vm->heap, vm->intern_table, "_stdout_path", 12);
            JaclVal stdout_path_val = jacl_map_get(m, stdout_key);
            char stdout_path[128] = {0};
            if (jacl_is_string(stdout_path_val)) {
              uint32_t len = jacl_string_byte_len(stdout_path_val);
              if (len < sizeof(stdout_path)) {
                jacl_string_data(stdout_path_val, stdout_path, len + 1);
              }
            }

            char* stdout_buf = NULL;
            size_t stdout_len = 0;
            if (stdout_path[0]) {
              FILE* fp = fopen(stdout_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stdout_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stdout_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stdout_len + 1));
                fread(stdout_buf, 1, stdout_len, fp);
                stdout_buf[stdout_len] = '\0';
                fclose(fp);
              }
              unlink(stdout_path);
            }

            /* Read stderr from temp file */
            JaclVal stderr_key = jacl_intern(&vm->heap, vm->intern_table, "_stderr_path", 12);
            JaclVal stderr_path_val = jacl_map_get(m, stderr_key);
            char stderr_path[128] = {0};
            if (jacl_is_string(stderr_path_val)) {
              uint32_t len = jacl_string_byte_len(stderr_path_val);
              if (len < sizeof(stderr_path)) {
                jacl_string_data(stderr_path_val, stderr_path, len + 1);
              }
            }

            char* stderr_buf = NULL;
            size_t stderr_len = 0;
            if (stderr_path[0]) {
              FILE* fp = fopen(stderr_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stderr_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stderr_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stderr_len + 1));
                fread(stderr_buf, 1, stderr_len, fp);
                stderr_buf[stderr_len] = '\0';
                fclose(fp);
              }
              unlink(stderr_path);
            }

            /* Create result map {stdout, stderr, exit} */
            jacl_map_node* result_map = NULL;

            /* Create stdout as a stream (like OP_EXEC_FULL) */
            JaclVal stdout_stream_val = jacl_stream(&vm->heap);
            JaclStream* stdout_stream = jacl_as_stream(stdout_stream_val);
            stdout_stream->kind = STREAM_KIND_EXEC_BUFFER;
            stdout_stream->state = STREAM_PENDING;
            if (stdout_buf && stdout_len > 0) {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table,
                                                   stdout_buf, (uint32_t)stdout_len);
            } else {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table, "", 0);
            }
            stdout_stream->args[1] = jacl_i32(0); /* position index */
            stdout_stream->arg_count = 2;

            JaclVal out_key = jacl_intern(&vm->heap, vm->intern_table, "stdout", 6);
            result_map = jacl_map_set(result_map, out_key, stdout_stream_val);

            JaclVal err_key = jacl_intern(&vm->heap, vm->intern_table, "stderr", 6);
            JaclVal err_val = stderr_buf && stderr_len > 0
                ? jacl_intern(&vm->heap, vm->intern_table, stderr_buf, (uint32_t)stderr_len)
                : jacl_intern(&vm->heap, vm->intern_table, "", 0);
            result_map = jacl_map_set(result_map, err_key, err_val);

            JaclVal exit_key = jacl_intern(&vm->heap, vm->intern_table, "exit", 4);
            result_map = jacl_map_set(result_map, exit_key, jacl_i32(exit_code));

            result = vm__push(vm, jacl_map_ptr(result_map));
            if (result != VM_OK) return result;
            break;
          }
        }

        /* Not a job - must be a future */
        if (!jacl_is_future(val)) {
          vm__set_error(vm, "await requires a future or job, got %s",
                       vm__type_name(val));
          return VM_RUNTIME_ERROR;
        }

        JaclVal state_val = vm->stack[frame->stack_base + 0];
        JaclFuture *fut = jacl_as_future(val);
        uint32_t fstate = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);

        if (fstate == FUTURE_RESOLVED || fstate == FUTURE_ERROR) {
          /* Already settled — push result and continue inline. */
          JaclVal await_result = (JaclVal)fut->result;
          result = vm__push(vm, await_result);
          if (result != VM_OK) return result;
        } else if (vm->runtime) {
          /* PENDING in runtime mode — register state machine as waiter
             on the future and suspend by returning VM_OK. */
          bool added = jacl_future_add_waiter(fut, state_val, &vm->heap);
          if (!added) {
            /* Race: future resolved between our check and add_waiter.
               Push result and continue inline. */
            JaclVal await_result = (JaclVal)fut->result;
            result = vm__push(vm, await_result);
            if (result != VM_OK) return result;
          } else {
            /* Successfully registered — suspend current task. */
            return VM_OK;
          }
        } else {
          /* PENDING in single-threaded mode — shouldn't happen since
             spawn resolves synchronously.  Push nil as fallback. */
          result = vm__push(vm, JACL_NIL);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_CALL_SUSPEND: {
        /* Like OP_CALL, but for calls to known suspending procs in SM context.
           In concurrent mode: spawn the inner SM as a separate task with a
           future, push the future (OP_AWAIT_SM follows to handle suspension).
           In single-threaded mode: fall through to synchronous call. */
        uint8_t arg_count = vm__read_byte(vm);
        JaclVal callee = vm->stack[vm->stack_top - arg_count - 1];

        if (!jacl_is_closure(callee)) {
          vm__set_error(vm, "call_suspend requires a closure, got %s",
                       vm__type_name(callee));
          return VM_RUNTIME_ERROR;
        }

        JaclClosure *closure = jacl_as_closure(callee);

        if (vm->runtime && closure->is_sm_compiled && !closure->is_generator) {
          /* Concurrent mode with SM callee: spawn as separate task */

          /* Arity check against user parameter count */
          if (arg_count != closure->min_args) {
            vm__set_error(vm, "expected %d arguments but got %d",
                         (int)closure->min_args, (int)arg_count);
            return VM_RUNTIME_ERROR;
          }

          /* Allocate all GC objects first, rooting each on the stack
             to survive GC. Only write to them after all allocs complete,
             since GC evacuation can move objects and stale C pointers. */

          /* 1. Allocate inner SM (root on stack) */
          JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
          result = vm__push(vm, sm_val);
          if (result != VM_OK) return result;

          /* 2. Allocate future (root on stack) */
          JaclVal future_val = jacl_future(&vm->heap);
          result = vm__push(vm, future_val);
          if (result != VM_OK) return result;

          /* 3. Allocate resolve_k (sm_val + future_val rooted on stack) */
          JaclVal resolve_k = runtime__create_resolve_closure(
              &vm->heap, vm->arena, future_val);

          /* All allocs done — now safe to write via re-read pointers.
             GC may have moved objects, so re-derive C pointers from
             tagged values (which the GC updates on the stack). */
          sm_val = vm->stack[vm->stack_top - 2];     /* re-read after GC */
          future_val = vm->stack[vm->stack_top - 1];  /* re-read after GC */
          JaclStateMachine *inner_sm = jacl_as_state_machine(sm_val);
          inner_sm->sm_closure = callee;
          inner_sm->error_k = resolve_k;
          for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
            /* args at: stack_top - 2(roots) - arg_count + i */
            inner_sm->fields[i] = vm->stack[vm->stack_top - 2 - arg_count + i];
          }

          /* 4. Submit inner SM as a task */
          runtime__schedule_sm_resumption(vm->runtime, sm_val, JACL_NIL);

          /* 5. Pop rooted values, pop callee + args, push future */
          vm->stack_top -= 2; /* pop future_val, sm_val roots */
          vm->stack_top -= (arg_count + 1); /* pop callee + args */
          result = vm__push(vm, future_val);
          if (result != VM_OK) return result;
        } else {
          /* Single-threaded or non-SM callee: execute synchronously,
             wrap result in a resolved future (OP_AWAIT_SM follows). */
          uint8_t *saved_ip = vm->ip;
          BytecodeChunk *saved_chunk = vm->chunk;
          uint32_t saved_frame_count = vm->frame_count;

          /* SM-compiled non-generator: wrap args into SM */
          if (closure->is_sm_compiled && !closure->is_generator) {
            if (arg_count != closure->min_args) {
              vm__set_error(vm, "expected %d arguments but got %d",
                           (int)closure->min_args, (int)arg_count);
              return VM_RUNTIME_ERROR;
            }
            JaclVal sm_val = gc_alloc_state_machine(&vm->heap, closure->sm_field_count);
            JaclStateMachine* sm = jacl_as_state_machine(sm_val);
            sm->sm_closure = callee;
            for (uint8_t i = 0; i < arg_count && i < closure->sm_field_count; i++) {
              sm->fields[i] = vm->stack[vm->stack_top - arg_count + i];
            }
            uint32_t callee_pos = vm->stack_top - arg_count - 1;
            vm->stack[callee_pos + 1] = sm_val;
            vm->stack[callee_pos + 2] = JACL_NIL;
            vm->stack_top = callee_pos + 3;
            arg_count = 2;
          }

          if (vm->frame_count >= VM_FRAMES_MAX) {
            vm__set_error(vm, "stack overflow");
            return VM_STACK_OVERFLOW;
          }
          CallFrame *sf = &vm->frames[vm->frame_count++];
          sf->closure    = closure;
          sf->return_ip  = saved_ip;
          sf->stack_base = vm->stack_top - (closure->is_sm_compiled ? 2 : arg_count);
          sf->chunk      = &closure->chunk;
          vm->ip    = closure->chunk.code;
          vm->chunk = &closure->chunk;

          VMResult sub = vm__run(vm, saved_frame_count);

          frame = &vm->frames[vm->frame_count - 1];
          vm->ip    = saved_ip;
          vm->chunk = saved_chunk;

          /* Wrap result in a resolved future for OP_AWAIT_SM */
          JaclVal call_result = JACL_NIL;
          if (sub == VM_OK && vm->stack_top > 0) {
            call_result = vm->stack[--vm->stack_top];
          } else if (sub != VM_OK) {
            vm__set_error(vm, "call_suspend: inner call failed");
            return VM_RUNTIME_ERROR;
          }

          JaclVal f = jacl_future(&vm->heap);
          JaclFuture *fut = jacl_as_future(f);
          jacl_future_resolve(fut, call_result,
                              vm->grey_buf, vm->gc_active_ptr);
          result = vm__push(vm, f);
          if (result != VM_OK) return result;
        }
        break;
      }

      case OP_SYNTAX_SPLICE: {
        vm__set_error(vm, "OP_SYNTAX_SPLICE is no longer supported");
        return VM_RUNTIME_ERROR;
      }

      case OP_SYNTAX_OP: {
        /* US-015: syntax introspection. US-016: syntax construction.
         * US-017: user-raised syntax errors. Subops:
         *   0  = syntax-kind      (syntax → string)
         *   1  = syntax-datum     (literal/var-ref → raw datum)
         *   2  = syntax-head      (command → syntax)
         *   3  = syntax-args      (command → vec of syntax)
         *   4  = syntax-commands  (block → vec of syntax)
         *   5  = syntax-pos       (syntax → map {line, col})
         *   6  = syntax->string   (syntax → pretty-printed string)
         *   7  = make-syntax lit-int    (i32 → syntax)
         *   8  = make-syntax lit-float  (f32 → syntax)
         *   9  = make-syntax lit-string (string → syntax)
         *   10 = make-syntax var-ref    (string → syntax)
         *   11 = make-syntax command    (head, args-vec → syntax)
         *   12 = make-syntax block      (commands-vec → syntax)
         *   13 = syntax-error message       (string → halt)
         *   14 = syntax-error message+syn   (string, syntax → halt)
         *   15 = make-syntax var-ref-caret  (string → syntax, scope_mark=0, is_caret=1)
         *   16 = gensym                     (prefix-string → syntax var-ref, is_gensym=1)
         *   17 = validate-unquote           (peek TOS: must be syntax object)
         *   18 = validate-unquote-splice    (peek TOS: must be vec of syntax objects) */
        uint8_t subop = vm__read_byte(vm);
        gc__current_heap = &vm->heap;

        /* US-017: syntax-error — halt execution with a custom message and
         * (optionally) a source position from the provided syntax object.
         * Dispatched before both construction and introspection because
         * its operand types are string (or string+syntax), not a bare
         * syntax object on top of the stack. */
        if (subop == 13) {  /* syntax-error message */
          JaclVal msg;
          result = vm__pop(vm, &msg); if (result != VM_OK) return result;
          if (jacl_is_error(msg)) {
            result = vm__push(vm, msg);
            if (result != VM_OK) return result;
            break;
          }
          if (!jacl_is_string(msg)) {
            vm__set_error(vm, "type error in 'syntax-error': expected string message, got %s",
                         vm__type_name(msg));
            return VM_RUNTIME_ERROR;
          }
          char mbuf[192];
          uint32_t mlen = jacl_string_data(msg, mbuf, sizeof(mbuf) - 1);
          mbuf[mlen] = '\0';
          vm__set_error(vm, "syntax-error: %s", mbuf);
          return VM_RUNTIME_ERROR;
        }
        if (subop == 14) {  /* syntax-error message + syntax */
          JaclVal syn_val, msg;
          result = vm__pop(vm, &syn_val); if (result != VM_OK) return result;
          result = vm__pop(vm, &msg);     if (result != VM_OK) return result;
          if (jacl_is_error(syn_val)) {
            result = vm__push(vm, syn_val);
            if (result != VM_OK) return result;
            break;
          }
          if (jacl_is_error(msg)) {
            result = vm__push(vm, msg);
            if (result != VM_OK) return result;
            break;
          }
          if (!jacl_is_string(msg)) {
            vm__set_error(vm, "type error in 'syntax-error': expected string message, got %s",
                         vm__type_name(msg));
            return VM_RUNTIME_ERROR;
          }
          if (!jacl_is_syntax(syn_val)) {
            vm__set_error(vm, "type error in 'syntax-error': expected syntax object as second argument, got %s",
                         vm__type_name(syn_val));
            return VM_RUNTIME_ERROR;
          }
          JaclSyntax *syn = jacl_as_syntax(syn_val);
          char mbuf[192];
          uint32_t mlen = jacl_string_data(msg, mbuf, sizeof(mbuf) - 1);
          mbuf[mlen] = '\0';
          vm__set_error(vm, "syntax-error at %u:%u: %s",
                       syn->pos_line, syn->pos_col, mbuf);
          return VM_RUNTIME_ERROR;
        }

        /* US-016: construction subops — each kind pops its own operands,
         * allocates a fresh JaclSyntax, and pushes it. Dispatched before
         * the introspection path because construction ops don't expect
         * a syntax object as the operand on top of the stack. */
        if ((subop >= 7 && subop <= 12) || subop == 15 || subop == 16 ||
            subop == 17 || subop == 18 || subop == 19) {
          static const char *mk_names[] = {
            "make-syntax lit-int",    "make-syntax lit-float",
            "make-syntax lit-string", "make-syntax var-ref",
            "make-syntax command",    "make-syntax block"
          };
          const char *mk_name = (subop <= 12) ? mk_names[subop - 7]
                               : (subop == 16) ? "gensym"
                                              : "make-syntax var-ref-caret";

          switch (subop) {
          case 7: {  /* make-syntax lit-int */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_i32(v)) {
              vm__set_error(vm, "type error in '%s': expected i32, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_INT;
            rsyn->data.lit_int.value = jacl_as_i32(v);
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 8: {  /* make-syntax lit-float */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_f32(v)) {
              vm__set_error(vm, "type error in '%s': expected f32, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_FLOAT;
            rsyn->data.lit_float.value = jacl_as_f32(v);
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 9: {  /* make-syntax lit-string */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_STRING;
            rsyn->data.lit_string.value = v;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 10: {  /* make-syntax var-ref */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_VAR_REF;
            rsyn->data.var_ref.name = v;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 15: {  /* make-syntax var-ref-caret (^name: scope_mark=0, is_caret=1) */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_VAR_REF;
            rsyn->data.var_ref.name = v;
            rsyn->scope_mark = 0;  /* caret: bypass macro mark */
            rsyn->is_caret = 1;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 16: {  /* US-010: gensym (prefix-string → syntax var-ref) */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in '%s': expected string, got %s",
                            mk_name, vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            if (!vm->gensym_counter_ptr) {
              vm__set_error(vm, "gensym: no gensym counter available (not in macro expansion)");
              return VM_RUNTIME_ERROR;
            }
            char prefix_buf[65];
            uint32_t plen = jacl_string_byte_len(v);
            if (plen > 64) {
              vm__set_error(vm, "gensym: prefix too long (max 64 bytes)");
              return VM_RUNTIME_ERROR;
            }
            jacl_string_data(v, prefix_buf, sizeof(prefix_buf));
            prefix_buf[plen] = '\0';
            const char *gerr = NULL;
            JaclVal out = jacl_gensym_next(prefix_buf, plen,
                                           &vm->heap, vm->intern_table,
                                           vm->gensym_counter_ptr,
                                           vm->macro_scope_mark, &gerr);
            if (gerr) {
              vm__set_error(vm, "gensym: %s", gerr);
              return VM_RUNTIME_ERROR;
            }
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 17: {  /* US-012: validate-unquote (top-of-stack must be syntax) */
            JaclVal v = vm->stack[vm->stack_top - 1];
            if (!jacl_is_nil(v) && !jacl_is_syntax(v)) {
              vm__set_error(vm, "~ (unquote) requires a syntax object, got %s",
                            vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            break;
          }
          case 18: {  /* US-012: validate-unquote-splice (top-of-stack must be vec of syntax) */
            JaclVal v = vm->stack[vm->stack_top - 1];
            if (!jacl_is_vector(v)) {
              vm__set_error(vm, "~@ (unquote-splicing) requires a vec of syntax objects, got %s",
                            vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root *vr = (jacl_vec_root *)jacl_as_ptr(v);
            uint32_t vc = jacl_vec_count(vr);
            for (uint32_t i = 0; i < vc; i++) {
              JaclVal elem = jacl_vec_get(vr, i).value;
              if (!jacl_is_syntax(elem)) {
                vm__set_error(vm, "~@ (unquote-splicing) requires a vec of syntax objects, element %u is %s",
                              i, vm__type_name(elem));
                return VM_RUNTIME_ERROR;
              }
            }
            break;
          }
          case 19: {  /* make-syntax lit-string-caret (scope_mark=0 for anaphoric introduction) */
            JaclVal v;
            result = vm__pop(vm, &v); if (result != VM_OK) return result;
            if (jacl_is_error(v)) { result = vm__push(vm, v); if (result != VM_OK) return result; break; }
            if (!jacl_is_string(v)) {
              vm__set_error(vm, "type error in 'make-syntax lit-string-caret': expected string, got %s",
                            vm__type_name(v));
              return VM_RUNTIME_ERROR;
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_LIT_STRING;
            rsyn->data.lit_string.value = v;
            rsyn->scope_mark = 0;  /* caret: bypass macro mark */
            rsyn->is_caret = 1;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 11: {  /* make-syntax command (head, args-vec) */
            JaclVal args_vec, head_val;
            result = vm__pop(vm, &args_vec); if (result != VM_OK) return result;
            result = vm__pop(vm, &head_val); if (result != VM_OK) return result;
            if (jacl_is_error(args_vec)) { result = vm__push(vm, args_vec); if (result != VM_OK) return result; break; }
            if (jacl_is_error(head_val)) { result = vm__push(vm, head_val); if (result != VM_OK) return result; break; }
            if (!jacl_is_syntax(head_val)) {
              vm__set_error(vm, "type error in '%s': head must be a syntax object, got %s",
                            mk_name, vm__type_name(head_val));
              return VM_RUNTIME_ERROR;
            }
            if (!jacl_is_vector(args_vec)) {
              vm__set_error(vm, "type error in '%s': args must be a vec of syntax, got %s",
                            mk_name, vm__type_name(args_vec));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root *vroot = (jacl_vec_root *)jacl_as_ptr(args_vec);
            uint32_t vcount = jacl_vec_count(vroot);
            for (uint32_t i = 0; i < vcount; i++) {
              JaclVal elem = jacl_vec_get(vroot, i).value;
              if (!jacl_is_syntax(elem)) {
                vm__set_error(vm, "type error in '%s': args must be a vec of syntax, element %u is %s",
                              mk_name, i, vm__type_name(elem));
                return VM_RUNTIME_ERROR;
              }
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_COMMAND;
            rsyn->data.command.head = head_val;
            rsyn->data.command.args = args_vec;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          case 12: {  /* make-syntax block (commands-vec) */
            JaclVal cmds_vec;
            result = vm__pop(vm, &cmds_vec); if (result != VM_OK) return result;
            if (jacl_is_error(cmds_vec)) { result = vm__push(vm, cmds_vec); if (result != VM_OK) return result; break; }
            if (!jacl_is_vector(cmds_vec)) {
              vm__set_error(vm, "type error in '%s': commands must be a vec of syntax, got %s",
                            mk_name, vm__type_name(cmds_vec));
              return VM_RUNTIME_ERROR;
            }
            jacl_vec_root *vroot = (jacl_vec_root *)jacl_as_ptr(cmds_vec);
            uint32_t vcount = jacl_vec_count(vroot);
            for (uint32_t i = 0; i < vcount; i++) {
              JaclVal elem = jacl_vec_get(vroot, i).value;
              if (!jacl_is_syntax(elem)) {
                vm__set_error(vm, "type error in '%s': commands must be a vec of syntax, element %u is %s",
                              mk_name, i, vm__type_name(elem));
                return VM_RUNTIME_ERROR;
              }
            }
            JaclVal out = gc_alloc_syntax(&vm->heap);
            JaclSyntax *rsyn = jacl_as_syntax(out);
            rsyn->kind = SYNTAX_BLOCK;
            rsyn->data.block.commands = cmds_vec;
            rsyn->scope_mark = vm->macro_scope_mark;
            result = vm__push(vm, out);
            if (result != VM_OK) return result;
            break;
          }
          }
          break;
        }

        /* Introspection path (subops 0-6): pop one syntax value, dispatch. */
        JaclVal val;
        result = vm__pop(vm, &val); if (result != VM_OK) return result;
        if (jacl_is_error(val)) {
          result = vm__push(vm, val);
          if (result != VM_OK) return result;
          break;
        }
        if (!jacl_is_syntax(val)) {
          static const char *names[] = {
            "syntax-kind", "syntax-datum", "syntax-head", "syntax-args",
            "syntax-commands", "syntax-pos", "syntax-str"
          };
          const char *nm = (subop < 7) ? names[subop] : "syntax-op";
          vm__set_error(vm, "type error in '%s': expected syntax object, got %s",
                       nm, vm__type_name(val));
          return VM_RUNTIME_ERROR;
        }
        JaclSyntax *syn = jacl_as_syntax(val);

        switch (subop) {
        case 0: {  /* syntax-kind */
          const char *kn = syntax_kind_name(syn->kind);
          JaclVal s = jacl_string_new(&vm->heap, vm->intern_table,
                                      kn, strlen(kn));
          result = vm__push(vm, s);
          if (result != VM_OK) return result;
          break;
        }
        case 1: {  /* syntax-datum */
          JaclVal out;
          switch ((SyntaxKind)syn->kind) {
          case SYNTAX_LIT_INT:
            out = jacl_i32(syn->data.lit_int.value);
            break;
          case SYNTAX_LIT_FLOAT:
            out = jacl_f32(syn->data.lit_float.value);
            break;
          case SYNTAX_LIT_STRING:
            out = syn->data.lit_string.value;
            break;
          case SYNTAX_VAR_REF:
            out = syn->data.var_ref.name;
            break;
          default:
            vm__set_error(vm,
              "type error in 'syntax-datum': syntax object of kind '%s' has no datum",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, out);
          if (result != VM_OK) return result;
          break;
        }
        case 2: {  /* syntax-head */
          if (syn->kind != SYNTAX_COMMAND) {
            vm__set_error(vm,
              "type error in 'syntax-head': expected command syntax, got '%s'",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, syn->data.command.head);
          if (result != VM_OK) return result;
          break;
        }
        case 3: {  /* syntax-args */
          if (syn->kind != SYNTAX_COMMAND) {
            vm__set_error(vm,
              "type error in 'syntax-args': expected command syntax, got '%s'",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, syn->data.command.args);
          if (result != VM_OK) return result;
          break;
        }
        case 4: {  /* syntax-commands */
          if (syn->kind != SYNTAX_BLOCK) {
            vm__set_error(vm,
              "type error in 'syntax-commands': expected block syntax, got '%s'",
              syntax_kind_name(syn->kind));
            return VM_RUNTIME_ERROR;
          }
          result = vm__push(vm, syn->data.block.commands);
          if (result != VM_OK) return result;
          break;
        }
        case 5: {  /* syntax-pos — map {line, col} */
          jacl_map_node *m = NULL;
          JaclVal k_line = jacl_inline_string("line", 4);
          JaclVal k_col  = jacl_inline_string("col", 3);
          m = jacl_map_set(m, k_line, jacl_i32((int32_t)syn->pos_line));
          m = jacl_map_set(m, k_col,  jacl_i32((int32_t)syn->pos_col));
          result = vm__push(vm, jacl_map_ptr(m));
          if (result != VM_OK) return result;
          break;
        }
        case 6: {  /* syntax->string — pretty-printed */
          VMFormatBuf fmt;
          vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
          vm__fmt_value(&fmt, val);
          JaclVal s = jacl_string_new(&vm->heap, vm->intern_table,
                                       fmt.data, fmt.len);
          result = vm__push(vm, s);
          if (result != VM_OK) return result;
          break;
        }
        default:
          vm__set_error(vm, "unknown OP_SYNTAX_OP subop %u", subop);
          return VM_RUNTIME_ERROR;
        }
        break;
      }

      case OP_INTERPRET: {
        /* Read arity byte: 1 = [interpret $src], 2 = [interpret $prelude $src] */
        uint8_t interp_arity = *vm->ip++;

        /* Pop source string (always top of stack). */
        JaclVal src_val;
        result = vm__pop(vm, &src_val);
        if (result != VM_OK) return result;

        /* Pop prelude map for 2-arg form. */
        JaclVal prelude_val = JACL_NIL;
        if (interp_arity == 2) {
          result = vm__pop(vm, &prelude_val);
          if (result != VM_OK) return result;
        }

        /* Error passthrough */
        if (jacl_is_error(src_val)) {
          result = vm__push(vm, src_val);
          if (result != VM_OK) return result;
          break;
        }
        if (interp_arity == 2 && jacl_is_error(prelude_val)) {
          result = vm__push(vm, prelude_val);
          if (result != VM_OK) return result;
          break;
        }

        /* Type-check: source must be string — return error value, not exception */
        if (!jacl_is_string(src_val)) {
          vm__set_error(vm, "interpret: expected string, got %s",
                        vm__type_name(src_val));
          result = vm__push(vm, jacl_set_error(JACL_NIL));
          if (result != VM_OK) return result;
          break;
        }

        /* Type-check: prelude must be map or nil — return error value, not exception */
        if (interp_arity == 2 && !jacl_is_nil(prelude_val) && !jacl_is_map(prelude_val)) {
          vm__set_error(vm, "interpret: expected map for prelude, got %s",
                        vm__type_name(prelude_val));
          result = vm__push(vm, jacl_set_error(JACL_NIL));
          if (result != VM_OK) return result;
          break;
        }

        /* Extract source bytes into arena. */
        uint32_t slen = jacl_string_byte_len(src_val);
        char *src_buf = (char *)arena_alloc(vm->arena, slen + 1);
        jacl_string_data(src_val, src_buf, slen + 1);
        src_buf[slen] = '\0';

        /* For 2-arg form: populate VM env with prelude entries so
         * OP_GET_GLOBAL resolves them at runtime. Save env.count for
         * cleanup after execution. */
        uint32_t saved_env_count = vm->env.count;
        if (interp_arity == 2 && jacl_is_map(prelude_val)) {
          gc__current_heap = &vm->heap;
          jacl_map_node *pmap = (jacl_map_node *)jacl_as_ptr(prelude_val);
          jacl_map_iter pit = jacl_map_iter_init(pmap);
          jacl_map_iter_result pir;
          for (;;) {
            pir = jacl_map_next_leaf(&pit);
            if (!pir.item) break;
            JaclVal mkey = jacl_map_key_from_leaf(pir.item);
            JaclVal mval = jacl_map_value_from_leaf(pir.item);
            /* Prelude keys must be strings */
            if (!jacl_is_string(mkey)) {
              vm->error_message = "interpret: prelude map key must be a string";
              result = vm__push(vm, jacl_set_error(JACL_NIL));
              if (result != VM_OK) return result;
              goto interpret_done;
            }
            /* Derive env key matching compiler's representation:
             * ≤7 bytes → jacl_inline_string, >7 → jacl_intern */
            uint32_t klen = jacl_string_byte_len(mkey);
            if (klen == 0 || klen > 128) {
              vm->error_message = klen == 0
                ? "interpret: prelude map key cannot be empty"
                : "interpret: prelude map key exceeds 128-byte limit";
              result = vm__push(vm, jacl_set_error(JACL_NIL));
              if (result != VM_OK) return result;
              goto interpret_done;
            }
            char kbuf[128];
            jacl_string_data(mkey, kbuf, sizeof(kbuf));
            JaclVal env_key;
            if (klen <= 7) {
              env_key = jacl_inline_string(kbuf, klen);
            } else {
              env_key = jacl_intern(&vm->heap, vm->intern_table, kbuf, klen);
            }
            vm__env_set(vm, env_key, mval);
          }
        }

        /* Compile source into a closure on the parent VM's heap (in-place).
         * Pass prelude_val so the compiler enforces closed-world names. */
        ExpandState iexpand;
        memset(&iexpand, 0, sizeof(iexpand));
        JaclError ierr;
        JaclVal compile_prelude = (interp_arity == 2) ? prelude_val : JACL_NIL;
        JaclVal closure_val = source_to_closure_in_place(
            src_buf, slen, vm->arena, &vm->heap,
            vm->intern_table, &iexpand, &ierr, compile_prelude);

        if (ierr.kind != JACL_ERROR_NONE) {
          /* Compile/parse error → restore env and push error value. */
          vm->env.count = saved_env_count;
          vm->error_message = ierr.message ? ierr.message
                                           : "interpret error";
          result = vm__push(vm, jacl_set_error(JACL_NIL));
          if (result != VM_OK) return result;
          break;
        }

        /* Execute the compiled closure on the parent VM. */
        JaclClosure *icl = jacl_as_closure(closure_val);

        /* Save caller state. */
        uint32_t saved_stack_top   = vm->stack_top;
        uint32_t saved_frame_count = vm->frame_count;
        uint8_t *saved_ip          = vm->ip;
        BytecodeChunk *saved_chunk = vm->chunk;
        BytecodeChunk *saved_top   = vm->top_chunk;

        /* Push dummy callee slot so OP_RETURN / OP_CHECK_ERROR
           frame-unwinding writes into a safe position. */
        result = vm__push(vm, closure_val);
        if (result != VM_OK) return result;

        if (vm->frame_count >= VM_FRAMES_MAX) {
          vm->stack_top = saved_stack_top;
          vm->env.count = saved_env_count;
          vm__set_error(vm, "stack overflow");
          return VM_RUNTIME_ERROR;
        }

        JaclClosure interpret_wrapper;
        memset(&interpret_wrapper, 0, sizeof(interpret_wrapper));
        interpret_wrapper.chunk = icl->chunk;

        CallFrame *ifrm = &vm->frames[vm->frame_count++];
        ifrm->closure    = &interpret_wrapper;
        ifrm->return_ip  = NULL;
        ifrm->stack_base = vm->stack_top;
        ifrm->chunk      = &icl->chunk;

        vm->ip        = icl->chunk.code;
        vm->chunk     = &icl->chunk;
        vm->top_chunk = &icl->chunk;
        vm->error_message = NULL;

        VMResult inner = vm__run(vm, saved_frame_count);

        JaclVal out;
        if (inner != VM_OK) {
          /* Runtime error during interpreted execution. */
          out = jacl_set_error(JACL_NIL);
          /* vm->error_message already set by vm__run */
        } else {
          /* Success — result is on top of stack. */
          out = (vm->stack_top > saved_stack_top)
              ? vm->stack[vm->stack_top - 1]
              : JACL_NIL;
        }

        /* Restore caller state (including env for sandbox cleanup). */
        vm->stack_top   = saved_stack_top;
        vm->frame_count = saved_frame_count;
        vm->ip          = saved_ip;
        vm->chunk       = saved_chunk;
        vm->top_chunk   = saved_top;
        vm->env.count   = saved_env_count;
        frame = &vm->frames[vm->frame_count - 1];

        result = vm__push(vm, out);
        if (result != VM_OK) return result;
interpret_done:
        break;
      }

      /* US-004: [interpret-prelude] — returns the default permissive prelude
       * map containing an entry for every non-core builtin.  Core language
       * forms (arithmetic, comparison, control flow, binding, destructuring,
       * immutable data ops, vec/map/set ops, string ops, error handling,
       * type checks, syntax-quote introspection) are NOT included.
       *
       * Non-core / capability-sensitive builtins (the default key list):
       *   print, interpret, interpret-prelude,
       *   spawn, await, parallel, race, yield,
       *   make-syntax, syntax-error,
       *   box, atom, deref, reset, swap,
       *   lines, stream_next
       */
      case OP_INTERPRET_PRELUDE: {
        gc__current_heap = &vm->heap;
        jacl_map_node *m = NULL;

        /* Use shared non-core list from compiler.c.
         * Values are native fn refs — callable first-class function references
         * that the compiler can recognize for direct opcode emission. */
        for (int i = 0; jacl_non_core_builtins[i]; i++) {
          const char *name = jacl_non_core_builtins[i];
          uint32_t len = (uint32_t)strlen(name);
          JaclVal key;
          if (len <= 7) {
            key = jacl_inline_string(name, len);
          } else {
            key = jacl_intern(&vm->heap, vm->intern_table, name, len);
          }
          /* Native fn ref with index i — enables compile-time opcode emission */
          m = jacl_map_set(m, key, jacl_native_fn((uint32_t)i));
        }

        result = vm__push(vm, jacl_map_ptr(m));
        if (result != VM_OK) return result;
        break;
      }

      /* Unified OP_EXEC with flags byte dispatch
       * Flags: EXEC_FLAG_FULL=0x01, EXEC_FLAG_STDIN=0x02, EXEC_FLAG_BG=0x04, EXEC_FLAG_PIPE=0x08
       * If EXEC_FLAG_PIPE, next byte is command count.
       */
      case OP_EXEC: {
        uint8_t flags = vm__read_byte(vm);

        /* === PIPE MODE (flags & 0x08) === */
        if (flags & EXEC_FLAG_PIPE) {
          uint8_t cmd_count = vm__read_byte(vm);
          if (cmd_count < 2) {
            vm__set_error(vm, "exec pipe requires at least 2 commands");
            return VM_RUNTIME_ERROR;
          }

          /* Pop command vectors from stack (last command on top) */
          JaclVal* cmd_vecs = (JaclVal*)arena_alloc(vm->arena, cmd_count * sizeof(JaclVal));
          for (int i = cmd_count - 1; i >= 0; i--) {
            result = vm__pop(vm, &cmd_vecs[i]);
            if (result != VM_OK) return result;
            if (!jacl_is_vector(cmd_vecs[i])) {
              vm__set_error(vm, "exec pipe: command %d must be a vector", i);
              return VM_RUNTIME_ERROR;
            }
          }

          /* Build command string arrays for each process */
          char*** cmd_argv = (char***)arena_alloc(vm->arena, cmd_count * sizeof(char**));
          for (uint32_t i = 0; i < cmd_count; i++) {
            jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(cmd_vecs[i]);
            uint32_t argc = jacl_vec_count(vec);
            if (argc == 0) {
              vm__set_error(vm, "exec pipe: command %d has no arguments", (int)i);
              return VM_RUNTIME_ERROR;
            }
            cmd_argv[i] = (char**)arena_alloc(vm->arena, (argc + 1) * sizeof(char*));
            for (uint32_t j = 0; j < argc; j++) {
              jacl_vec_get_result gr = jacl_vec_get(vec, j);
              JaclVal arg_val = gr.value;
              if (!jacl_is_string(arg_val)) {
                vm__set_error(vm, "exec pipe: command %d arg %d must be string", (int)i, (int)j);
                return VM_RUNTIME_ERROR;
              }
              uint32_t arg_len = jacl_string_byte_len(arg_val);
              char* arg_buf = (char*)arena_alloc(vm->arena, arg_len + 1);
              jacl_string_data(arg_val, arg_buf, arg_len + 1);
              cmd_argv[i][j] = arg_buf;
            }
            cmd_argv[i][argc] = NULL;
          }

          /* Create pipes */
          int (*pipes)[2] = (int(*)[2])arena_alloc(vm->arena, cmd_count * sizeof(int[2]));
          for (uint32_t i = 0; i < cmd_count; i++) {
            if (pipe(pipes[i]) < 0) {
              vm__set_error(vm, "exec pipe: failed to create pipe");
              return VM_RUNTIME_ERROR;
            }
          }

          char stderr_path[64];
          snprintf(stderr_path, sizeof(stderr_path), "/tmp/jacl_stderr_%d_%lu",
                   (int)getpid(), (unsigned long)time(NULL));

          pid_t* pids = (pid_t*)arena_alloc(vm->arena, cmd_count * sizeof(pid_t));
          for (uint32_t i = 0; i < cmd_count; i++) {
            pids[i] = fork();
            if (pids[i] < 0) {
              vm__set_error(vm, "exec pipe: fork failed");
              return VM_RUNTIME_ERROR;
            }
            if (pids[i] == 0) {
              if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
              dup2(pipes[i][1], STDOUT_FILENO);
              if (i == cmd_count - 1) {
                int stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (stderr_fd >= 0) { dup2(stderr_fd, STDERR_FILENO); close(stderr_fd); }
              }
              for (uint32_t j = 0; j < cmd_count; j++) { close(pipes[j][0]); close(pipes[j][1]); }
              execvp(cmd_argv[i][0], cmd_argv[i]);
              _exit(127);
            }
          }

          for (uint32_t i = 0; i < cmd_count; i++) {
            close(pipes[i][1]);
            if (i < cmd_count - 1) close(pipes[i][0]);
          }

          int stdout_fd = pipes[cmd_count - 1][0];
          FILE* fp = fdopen(stdout_fd, "r");
          if (!fp) { close(stdout_fd); vm__set_error(vm, "exec pipe: fdopen failed"); return VM_RUNTIME_ERROR; }

          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->kind = STREAM_KIND_EXEC_PIPE;
          stream->state = STREAM_PENDING;
          stream->args[0] = (JaclVal)(uintptr_t)fp;
          gc__current_heap = &vm->heap;

          char pid_buf[256];
          int pid_offset = snprintf(pid_buf, sizeof(pid_buf), "%d", (int)cmd_count);
          for (uint32_t i = 0; i < cmd_count; i++) {
            pid_offset += snprintf(pid_buf + pid_offset, sizeof(pid_buf) - pid_offset, ",%d", (int)pids[i]);
          }
          stream->args[1] = jacl_intern(&vm->heap, vm->intern_table, pid_buf, (uint32_t)strlen(pid_buf));
          stream->args[2] = jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path));
          stream->args[3] = JACL_NIL;
          stream->arg_count = 4;

          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          break;
        }

        /* For non-PIPE modes, handle stdin first if EXEC_FLAG_STDIN */
        JaclVal stdin_val = JACL_NIL;
        char* stdin_buf = NULL;
        size_t stdin_len = 0;
        if (flags & EXEC_FLAG_STDIN) {
          result = vm__pop(vm, &stdin_val);
          if (result != VM_OK) return result;
        }

        /* Pop args vector */
        JaclVal args_vec;
        result = vm__pop(vm, &args_vec);
        if (result != VM_OK) return result;

        /* === BACKGROUND MODE (flags & 0x04) === */
        if (flags & EXEC_FLAG_BG) {
          if (!jacl_is_vector(args_vec)) {
            vm__set_error(vm, "exec requires a vector of arguments, got %s", vm__type_name(args_vec));
            return VM_RUNTIME_ERROR;
          }

          jacl_vec_root* vec = (jacl_vec_root*)jacl_as_ptr(args_vec);
          uint32_t argc = jacl_vec_count(vec);
          if (argc == 0) { vm__set_error(vm, "exec requires at least a command name"); return VM_RUNTIME_ERROR; }

          char** argv = (char**)arena_alloc(vm->arena, sizeof(char*) * (argc + 1));
          for (uint32_t i = 0; i < argc; i++) {
            jacl_vec_get_result gr = jacl_vec_get(vec, i);
            JaclVal arg_val = gr.value;
            if (!jacl_is_string(arg_val)) {
              vm__set_error(vm, "exec argument %d must be a string, got %s", (int)i, vm__type_name(arg_val));
              return VM_RUNTIME_ERROR;
            }
            uint32_t arg_len = jacl_string_byte_len(arg_val);
            char* arg_buf = (char*)arena_alloc(vm->arena, arg_len + 1);
            jacl_string_data(arg_val, arg_buf, arg_len + 1);
            argv[i] = arg_buf;
          }
          argv[argc] = NULL;

          char stdout_path[64], stderr_path[64];
          unsigned long hash = (unsigned long)time(NULL) ^ (unsigned long)(uintptr_t)argv;
          snprintf(stdout_path, sizeof(stdout_path), "/tmp/jacl_bg_out_%d_%lu", (int)getpid(), hash);
          snprintf(stderr_path, sizeof(stderr_path), "/tmp/jacl_bg_err_%d_%lu", (int)getpid(), hash);

          pid_t pid = fork();
          if (pid < 0) { vm__set_error(vm, "fork failed: %s", strerror(errno)); return VM_RUNTIME_ERROR; }

          if (pid == 0) {
            int stdout_fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            int stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (stdout_fd >= 0) { dup2(stdout_fd, STDOUT_FILENO); close(stdout_fd); }
            if (stderr_fd >= 0) { dup2(stderr_fd, STDERR_FILENO); close(stderr_fd); }
            execvp(argv[0], argv);
            fprintf(stderr, "execvp failed: %s\n", strerror(errno));
            _exit(127);
          }

          gc__current_heap = &vm->heap;
          jacl_map_node* m = NULL;
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "pid", 3), jacl_i32((int32_t)pid));
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7), JACL_TRUE);
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "_stdout_path", 12),
                          jacl_intern(&vm->heap, vm->intern_table, stdout_path, (uint32_t)strlen(stdout_path)));
          m = jacl_map_set(m, jacl_intern(&vm->heap, vm->intern_table, "_stderr_path", 12),
                          jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path)));

          result = vm__push(vm, jacl_map_ptr(m));
          if (result != VM_OK) return result;
          break;
        }

        /* === BUILD COMMAND STRING (shared by remaining modes) === */
        char cmd_buf[4096];
        if (!vm__exec_build_cmd(vm, args_vec, cmd_buf, sizeof(cmd_buf))) {
          return VM_RUNTIME_ERROR;
        }

        /* Collect stdin if needed */
        if (flags & EXEC_FLAG_STDIN) {
          int collect_result = vm__exec_collect_stdin(vm, stdin_val, &stdin_buf, &stdin_len);
          if (collect_result == -1) return VM_RUNTIME_ERROR;
          if (collect_result == -2) {
            /* Error value pushed to stack */
            frame = &vm->frames[vm->frame_count - 1];
            break;
          }
        }

        char stderr_path[64];
        snprintf(stderr_path, sizeof(stderr_path), "/tmp/jacl_stderr_%d_%lu",
                 (int)getpid(), (unsigned long)time(NULL) ^ (unsigned long)cmd_buf);

        char full_cmd[4256];
        if (flags & EXEC_FLAG_STDIN) {
          char stdin_path[64];
          snprintf(stdin_path, sizeof(stdin_path), "/tmp/jacl_stdin_%d_%lu",
                   (int)getpid(), (unsigned long)time(NULL) ^ (unsigned long)cmd_buf);
          FILE* stdin_fp = fopen(stdin_path, "w");
          if (!stdin_fp) { vm__set_error(vm, "exec: failed to create stdin temp file"); return VM_RUNTIME_ERROR; }
          fwrite(stdin_buf, 1, stdin_len, stdin_fp);
          fclose(stdin_fp);
          snprintf(full_cmd, sizeof(full_cmd), "%s <%s 2>%s", cmd_buf, stdin_path, stderr_path);

          FILE* fp = popen(full_cmd, "r");
          if (!fp) { unlink(stdin_path); unlink(stderr_path); vm__set_error(vm, "exec: failed to spawn process '%s'", cmd_buf); return VM_RUNTIME_ERROR; }

          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->kind = STREAM_KIND_EXEC;
          stream->state = STREAM_PENDING;
          stream->args[0] = (JaclVal)(uintptr_t)fp;
          gc__current_heap = &vm->heap;
          stream->args[1] = jacl_intern(&vm->heap, vm->intern_table, cmd_buf, (uint32_t)strlen(cmd_buf));
          stream->args[2] = jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path));
          stream->args[3] = jacl_intern(&vm->heap, vm->intern_table, stdin_path, (uint32_t)strlen(stdin_path));
          stream->arg_count = 4;

          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          break;
        }

        snprintf(full_cmd, sizeof(full_cmd), "%s 2>%s", cmd_buf, stderr_path);
        FILE* fp = popen(full_cmd, "r");
        if (!fp) { unlink(stderr_path); vm__set_error(vm, "exec: failed to spawn process '%s'", cmd_buf); return VM_RUNTIME_ERROR; }

        /* === FULL MODE (flags & 0x01) === */
        if (flags & EXEC_FLAG_FULL) {
          /* Read all stdout */
          char* stdout_buf = NULL;
          size_t stdout_len = 0;
          size_t stdout_cap = 0;
          char read_buf[4096];
          size_t n;
          while ((n = fread(read_buf, 1, sizeof(read_buf), fp)) > 0) {
            if (stdout_len + n > stdout_cap) {
              size_t new_cap = stdout_cap == 0 ? 4096 : stdout_cap * 2;
              while (new_cap < stdout_len + n) new_cap *= 2;
              char* new_buf = (char*)arena_alloc(vm->arena, (uint32_t)new_cap);
              if (stdout_buf) memcpy(new_buf, stdout_buf, stdout_len);
              stdout_buf = new_buf;
              stdout_cap = new_cap;
            }
            memcpy(stdout_buf + stdout_len, read_buf, n);
            stdout_len += n;
          }

          int status = pclose(fp);
          int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

          char stderr_buf_data[4096];
          size_t stderr_len = 0;
          FILE* stderr_fp = fopen(stderr_path, "r");
          if (stderr_fp) { stderr_len = fread(stderr_buf_data, 1, sizeof(stderr_buf_data) - 1, stderr_fp); fclose(stderr_fp); }
          stderr_buf_data[stderr_len] = '\0';
          unlink(stderr_path);

          gc__current_heap = &vm->heap;
          JaclVal stdout_str = jacl_intern(&vm->heap, vm->intern_table, stdout_buf ? stdout_buf : "", (uint32_t)stdout_len);
          JaclVal stdout_stream_val = jacl_stream(&vm->heap);
          JaclStream* stdout_stream = jacl_as_stream(stdout_stream_val);
          stdout_stream->kind = STREAM_KIND_EXEC_BUFFER;
          stdout_stream->state = STREAM_PENDING;
          stdout_stream->args[0] = stdout_str;
          stdout_stream->args[1] = jacl_i32(0);
          stdout_stream->arg_count = 2;

          jacl_map_node* result_map = jacl_map_set(NULL, jacl_intern(&vm->heap, vm->intern_table, "stdout", 6), stdout_stream_val);
          result_map = jacl_map_set(result_map, jacl_intern(&vm->heap, vm->intern_table, "stderr", 6),
                                    jacl_intern(&vm->heap, vm->intern_table, stderr_buf_data, (uint32_t)stderr_len));
          result_map = jacl_map_set(result_map, jacl_intern(&vm->heap, vm->intern_table, "exit", 4), jacl_i32(exit_code));

          result = vm__push(vm, jacl_map_ptr(result_map));
          if (result != VM_OK) return result;
          break;
        }

        /* === BASIC MODE (flags == 0) === */
        {
          JaclVal stream_val = jacl_stream(&vm->heap);
          JaclStream* stream = jacl_as_stream(stream_val);
          stream->kind = STREAM_KIND_EXEC;
          stream->state = STREAM_PENDING;
          stream->args[0] = (JaclVal)(uintptr_t)fp;
          gc__current_heap = &vm->heap;
          stream->args[1] = jacl_intern(&vm->heap, vm->intern_table, cmd_buf, (uint32_t)strlen(cmd_buf));
          stream->args[2] = jacl_intern(&vm->heap, vm->intern_table, stderr_path, (uint32_t)strlen(stderr_path));
          stream->arg_count = 3;

          result = vm__push(vm, stream_val);
          if (result != VM_OK) return result;
          break;
        }
      }

      /* US-010: Await job or future result.
       * Stack: [value] -> [result]
       * If value is a Job map (_is_job=true): waitpid, read files, return {stdout, stderr, exit}
       * If value is a resolved future: return result
       * If value is a pending future: error (non-SM context can't suspend)
       */
      case OP_AWAIT_JOB: {
        JaclVal val;
        result = vm__pop(vm, &val);
        if (result != VM_OK) return result;

        /* Check if it's a Job map */
        if (jacl_is_map(val)) {
          jacl_map_node* m = (jacl_map_node*)jacl_as_ptr(val);
          JaclVal marker_key = jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7);
          JaclVal marker_val = jacl_map_get(m, marker_key);

          if (marker_val == JACL_TRUE) {
            /* It's a Job - extract pid and wait */
            JaclVal pid_key = jacl_intern(&vm->heap, vm->intern_table, "pid", 3);
            JaclVal pid_val = jacl_map_get(m, pid_key);
            if (!jacl_is_i32(pid_val)) {
              vm__set_error(vm, "invalid job: missing pid");
              return VM_RUNTIME_ERROR;
            }
            pid_t pid = (pid_t)jacl_as_i32(pid_val);

            /* Wait for process to complete */
            int status;
            pid_t waited = waitpid(pid, &status, 0);
            if (waited < 0) {
              vm__set_error(vm, "waitpid failed: %s", strerror(errno));
              return VM_RUNTIME_ERROR;
            }

            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

            /* Read stdout from temp file */
            JaclVal stdout_key = jacl_intern(&vm->heap, vm->intern_table, "_stdout_path", 12);
            JaclVal stdout_path_val = jacl_map_get(m, stdout_key);
            char stdout_path[128] = {0};
            if (jacl_is_string(stdout_path_val)) {
              uint32_t len = jacl_string_byte_len(stdout_path_val);
              if (len < sizeof(stdout_path)) {
                jacl_string_data(stdout_path_val, stdout_path, len + 1);
              }
            }

            char* stdout_buf = NULL;
            size_t stdout_len = 0;
            if (stdout_path[0]) {
              FILE* fp = fopen(stdout_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stdout_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stdout_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stdout_len + 1));
                fread(stdout_buf, 1, stdout_len, fp);
                stdout_buf[stdout_len] = '\0';
                fclose(fp);
              }
              unlink(stdout_path);
            }

            /* Read stderr from temp file */
            JaclVal stderr_key = jacl_intern(&vm->heap, vm->intern_table, "_stderr_path", 12);
            JaclVal stderr_path_val = jacl_map_get(m, stderr_key);
            char stderr_path[128] = {0};
            if (jacl_is_string(stderr_path_val)) {
              uint32_t len = jacl_string_byte_len(stderr_path_val);
              if (len < sizeof(stderr_path)) {
                jacl_string_data(stderr_path_val, stderr_path, len + 1);
              }
            }

            char* stderr_buf = NULL;
            size_t stderr_len = 0;
            if (stderr_path[0]) {
              FILE* fp = fopen(stderr_path, "r");
              if (fp) {
                fseek(fp, 0, SEEK_END);
                stderr_len = (size_t)ftell(fp);
                fseek(fp, 0, SEEK_SET);
                stderr_buf = (char*)arena_alloc(vm->arena, (uint32_t)(stderr_len + 1));
                fread(stderr_buf, 1, stderr_len, fp);
                stderr_buf[stderr_len] = '\0';
                fclose(fp);
              }
              unlink(stderr_path);
            }

            /* Create result map {stdout, stderr, exit} */
            gc__current_heap = &vm->heap;
            jacl_map_node* result_map = NULL;

            /* Create stdout as a stream (like OP_EXEC_FULL) */
            JaclVal stdout_stream_val = jacl_stream(&vm->heap);
            JaclStream* stdout_stream = jacl_as_stream(stdout_stream_val);
            stdout_stream->kind = STREAM_KIND_EXEC_BUFFER;
            stdout_stream->state = STREAM_PENDING;
            if (stdout_buf && stdout_len > 0) {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table,
                                                   stdout_buf, (uint32_t)stdout_len);
            } else {
              stdout_stream->args[0] = jacl_intern(&vm->heap, vm->intern_table, "", 0);
            }
            stdout_stream->args[1] = jacl_i32(0); /* position index */
            stdout_stream->arg_count = 2;

            JaclVal out_key = jacl_intern(&vm->heap, vm->intern_table, "stdout", 6);
            result_map = jacl_map_set(result_map, out_key, stdout_stream_val);

            JaclVal err_key = jacl_intern(&vm->heap, vm->intern_table, "stderr", 6);
            JaclVal err_val = stderr_buf && stderr_len > 0
                ? jacl_intern(&vm->heap, vm->intern_table, stderr_buf, (uint32_t)stderr_len)
                : jacl_intern(&vm->heap, vm->intern_table, "", 0);
            result_map = jacl_map_set(result_map, err_key, err_val);

            JaclVal exit_key = jacl_intern(&vm->heap, vm->intern_table, "exit", 4);
            result_map = jacl_map_set(result_map, exit_key, jacl_i32(exit_code));

            result = vm__push(vm, jacl_map_ptr(result_map));
            if (result != VM_OK) return result;
            break;
          }
        }

        /* Check if it's a Future */
        if (jacl_is_future(val)) {
          JaclFuture* fut = jacl_as_future(val);
          uint32_t fstate = ATOMIC_LOAD_EXPLICIT(&fut->state, MEM_ACQUIRE);

          if (fstate == FUTURE_RESOLVED) {
            result = vm__push(vm, (JaclVal)fut->result);
            if (result != VM_OK) return result;
            break;
          } else if (fstate == FUTURE_ERROR) {
            JaclVal err = (JaclVal)fut->result;
            result = vm__push(vm, err | JACL_FLAG_ERROR);
            if (result != VM_OK) return result;
            break;
          } else {
            vm__set_error(vm, "cannot await pending future outside async context");
            return VM_RUNTIME_ERROR;
          }
        }

        vm__set_error(vm, "await requires a Job or Future, got %s",
                     vm__type_name(val));
        return VM_RUNTIME_ERROR;
      }

      /* US-011: Send signal to a background job.
       * Stack: [job_map, signal_name] -> [bool]
       * Returns $true if signal sent, $false if process already exited.
       */
      case OP_SIGNAL: {
        JaclVal sig_name_val;
        result = vm__pop(vm, &sig_name_val);
        if (result != VM_OK) return result;

        JaclVal job_val;
        result = vm__pop(vm, &job_val);
        if (result != VM_OK) return result;

        /* Validate signal name is a string */
        if (!jacl_is_string(sig_name_val)) {
          vm__set_error(vm, "signal requires a signal name string, got %s",
                       vm__type_name(sig_name_val));
          return VM_RUNTIME_ERROR;
        }

        /* Validate job is a map with _is_job marker */
        if (!jacl_is_map(job_val)) {
          vm__set_error(vm, "signal requires a Job map, got %s",
                       vm__type_name(job_val));
          return VM_RUNTIME_ERROR;
        }

        jacl_map_node* m = (jacl_map_node*)jacl_as_ptr(job_val);
        gc__current_heap = &vm->heap;
        JaclVal marker_key = jacl_intern(&vm->heap, vm->intern_table, "_is_job", 7);
        JaclVal marker_val = jacl_map_get(m, marker_key);

        if (marker_val != JACL_TRUE) {
          vm__set_error(vm, "signal requires a Job map (missing _is_job marker)");
          return VM_RUNTIME_ERROR;
        }

        /* Extract PID */
        JaclVal pid_key = jacl_intern(&vm->heap, vm->intern_table, "pid", 3);
        JaclVal pid_val = jacl_map_get(m, pid_key);
        if (!jacl_is_i32(pid_val)) {
          vm__set_error(vm, "invalid job: missing pid");
          return VM_RUNTIME_ERROR;
        }
        pid_t pid = (pid_t)jacl_as_i32(pid_val);

        /* Parse signal name */
        char sig_name[32];
        uint32_t sig_len = jacl_string_byte_len(sig_name_val);
        if (sig_len >= sizeof(sig_name)) {
          vm__set_error(vm, "signal name too long");
          return VM_RUNTIME_ERROR;
        }
        jacl_string_data(sig_name_val, sig_name, sig_len + 1);
        sig_name[sig_len] = '\0';

        int signum = -1;
        if (strcmp(sig_name, "SIGTERM") == 0) signum = SIGTERM;
        else if (strcmp(sig_name, "SIGKILL") == 0) signum = SIGKILL;
        else if (strcmp(sig_name, "SIGINT") == 0) signum = SIGINT;
        else if (strcmp(sig_name, "SIGHUP") == 0) signum = SIGHUP;
        else if (strcmp(sig_name, "SIGUSR1") == 0) signum = SIGUSR1;
        else if (strcmp(sig_name, "SIGUSR2") == 0) signum = SIGUSR2;
        else {
          vm__set_error(vm, "unknown signal: %s", sig_name);
          return VM_RUNTIME_ERROR;
        }

        /* Check if process still exists and send signal */
        int kill_result = kill(pid, signum);
        if (kill_result == 0) {
          /* Signal sent successfully */
          result = vm__push(vm, JACL_TRUE);
        } else if (errno == ESRCH) {
          /* Process already exited - return $false (no-op) */
          result = vm__push(vm, JACL_FALSE);
        } else {
          vm__set_error(vm, "kill failed: %s", strerror(errno));
          return VM_RUNTIME_ERROR;
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_HALT: {
        return VM_OK;
      }

      case OP_GET_CTX: {
        result = vm__push(vm, vm->ctx);
        if (result != VM_OK) return result;
        break;
      }

      case OP_CTX_FORK: {
        if (!vm->ctx_pool || vm->ctx == JACL_NIL) {
          vm__set_error(vm, "with-ctx: no ctx available");
          return VM_RUNTIME_ERROR;
        }
        if (vm->saved_ctx_count >= 8) {
          vm__set_error(vm, "with-ctx: nesting too deep (max 8)");
          return VM_RUNTIME_ERROR;
        }
        JaclVal old_ctx = ctx_fork(vm, vm->ctx);
        if (vm->ctx == old_ctx) {
          vm__set_error(vm, "with-ctx: failed to allocate ctx");
          return VM_RUNTIME_ERROR;
        }
        vm->saved_ctx[vm->saved_ctx_count++] = old_ctx;
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, old_ctx, vm->ctx);
        break;
      }

      case OP_CTX_RESTORE: {
        /* Pop saved ctx from VM save stack, free forked ctx to pool, restore */
        if (vm->saved_ctx_count == 0) {
          vm__set_error(vm, "ctx restore: no saved ctx");
          return VM_RUNTIME_ERROR;
        }
        ctx_unfork(vm, vm->saved_ctx[--vm->saved_ctx_count]);
        break;
      }

      case OP_SET_CTX: {
        /* Pop value from stack and store in vm->ctx (used by SM resume) */
        JaclVal new_ctx;
        result = vm__pop(vm, &new_ctx);
        if (result != VM_OK) return result;
        gc_write_barrier(vm->grey_buf, vm->gc_active_ptr, vm->ctx, new_ctx);
        vm->ctx = new_ctx;
        break;
      }

      case OP_RANGE: {
        /* Create a range stream: pop end, pop start, push stream.
         * Next byte: 0 = exclusive (..<), 1 = inclusive (..=) */
        uint8_t inclusive = *vm->ip++;
        JaclVal end_val, start_val;
        result = vm__pop(vm, &end_val);
        if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val);
        if (result != VM_OK) return result;

        /* Coerce to i64 for range bounds */
        int64_t start_i, end_i;
        if (jacl_is_i32(start_val)) start_i = (int64_t)jacl_as_i32(start_val);
        else if (jacl_is_i64(start_val)) start_i = jacl_as_i64(start_val);
        else {
          vm__set_error(vm, "range start must be an integer, got %s",
                       vm__type_name(start_val));
          return VM_RUNTIME_ERROR;
        }
        if (jacl_is_i32(end_val)) end_i = (int64_t)jacl_as_i32(end_val);
        else if (jacl_is_i64(end_val)) end_i = jacl_as_i64(end_val);
        else {
          vm__set_error(vm, "range end must be an integer, got %s",
                       vm__type_name(end_val));
          return VM_RUNTIME_ERROR;
        }

        JaclVal range_stream = jacl_stream(&vm->heap);
        JaclStream* rs = jacl_as_stream(range_stream);
        rs->kind      = STREAM_KIND_RANGE;
        rs->args[0]   = jacl_i64(&vm->heap, start_i);  /* current value */
        rs->args[1]   = jacl_i64(&vm->heap, end_i);    /* end bound */
        rs->args[2]   = jacl_i32((int32_t)inclusive); /* 0=excl, 1=incl */
        rs->arg_count = 3;
        result = vm__push(vm, range_stream);
        if (result != VM_OK) return result;
        break;
      }

      /* --- Typed vector operations --- */

      case OP_TYPED_VEC: {
        uint16_t type_idx = vm__read_u16(vm);
        uint8_t count = vm__read_byte(vm);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);

        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(width);

        /* Pop elements right-to-left into a buffer, then push left-to-right. */
        JaclVal scratch[VM_MAX_STRUCT_SLOTS * 256];
        if ((size_t)count * width > sizeof(scratch) / sizeof(JaclVal)) {
          vm__set_error(vm, "typed-vec literal too large");
          return VM_RUNTIME_ERROR;
        }
        for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
          vm__pop_struct(vm, type_idx, &scratch[(uint32_t)i * width]);
        }
        for (uint8_t i = 0; i < count; i++) {
          tvec = jacl_typed_vec_push_back_wide(tvec, &scratch[i * width]);
        }
        result = vm__push(vm, jacl_typed_vector_ptr(tvec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_VEC_GET: {
        /* Dead opcode — replaced by OP_TYPED_VEC_GET_INLINE (Phase 5f). */
        vm__set_error(vm, "OP_TYPED_VEC_GET is no longer emitted");
        return VM_RUNTIME_ERROR;
      }

      case OP_TYPED_VEC_GET_INLINE: {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal idx_val, tvec_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "vec-get: expected i32 index, got %s",
                       vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }

        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);
        int32_t idx = jacl_as_i32(idx_val);

        if (idx < 0 || (uint32_t)idx >= jacl_typed_vec_count(tvec)) {
          vm__set_error(vm, "vec-get: index %d out of bounds (length %u)",
                       idx, jacl_typed_vec_count(tvec));
          return VM_RUNTIME_ERROR;
        }

        const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, (uint32_t)idx);
        /* Push width inline slots directly from RRB leaf data */
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_error(vm, "stack overflow in typed vec get inline");
          return VM_RUNTIME_ERROR;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        break;
      }

      case OP_INLINE_TO_LOCAL: {
        uint8_t base_slot = vm__read_byte(vm);
        uint16_t type_idx = vm__read_u16(vm);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);
        uint32_t abs_base = frame->stack_base + base_slot;
        /* Copy width inline slots from TOS to local range */
        memcpy(&vm->stack[abs_base], &vm->stack[vm->stack_top - width],
               width * sizeof(JaclVal));
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, abs_base + si);
        }
        /* Pop the width slots from stack */
        for (uint32_t si = vm->stack_top - width; si < vm->stack_top; si++) {
          BITMAP_CLR(vm->inline_slot_bitmap, si);
        }
        vm->stack_top -= width;
        break;
      }

      case OP_TYPED_VEC_PUSH: {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, slots);
        JaclVal tvec_val;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_push_back_wide(tvec, slots);
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_VEC_SET: {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal slots[VM_MAX_STRUCT_SLOTS];
        vm__pop_struct(vm, type_idx, slots);
        JaclVal idx_val, tvec_val;
        result = vm__pop(vm, &idx_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        if (!jacl_is_i32(idx_val)) {
          vm__set_error(vm, "vec-set: expected i32 index, got %s",
                       vm__type_name(idx_val));
          return VM_RUNTIME_ERROR;
        }

        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        int32_t idx = jacl_as_i32(idx_val);

        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_set_wide(tvec, (uint32_t)idx, slots);
        if (!new_tvec) {
          vm__set_error(vm, "vec-set: index %d out of bounds", idx);
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_VEC_LEN: {
        JaclVal tvec_val;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_typed_vec_count(tvec)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_VEC_CONCAT: {
        uint16_t type_idx = vm__read_u16(vm);
        (void)type_idx;
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;
        jacl_typed_vec_root* a = (jacl_typed_vec_root*)jacl_as_ptr(a_val);
        jacl_typed_vec_root* b = (jacl_typed_vec_root*)jacl_as_ptr(b_val);
        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_concat(a, b);
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_VEC_SLICE: {
        uint16_t type_idx = vm__read_u16(vm);
        (void)type_idx;
        JaclVal end_val, start_val, tvec_val;
        result = vm__pop(vm, &end_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &start_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;
        if (!jacl_is_i32(start_val)) {
          vm__set_error(vm, "vec-slice: expected i32 start, got %s", vm__type_name(start_val));
          return VM_RUNTIME_ERROR;
        }
        if (!jacl_is_i32(end_val)) {
          vm__set_error(vm, "vec-slice: expected i32 end, got %s", vm__type_name(end_val));
          return VM_RUNTIME_ERROR;
        }
        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        int32_t start = jacl_as_i32(start_val);
        int32_t end = jacl_as_i32(end_val);
        uint32_t count = jacl_typed_vec_count(tvec);
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if ((uint32_t)start > count) start = (int32_t)count;
        if ((uint32_t)end > count) end = (int32_t)count;
        if (start > end) start = end;
        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* new_tvec = jacl_typed_vec_slice(tvec, (uint32_t)start, (uint32_t)end);
        result = vm__push(vm, jacl_typed_vector_ptr(new_tvec));
        if (result != VM_OK) return result;
        break;
      }

      /* ===== Typed HOF opcodes ===== */

      case OP_TYPED_EACH: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; break; }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "type error in 'each': expected closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure* closure = jacl_as_closure(closure_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];

        if (jacl_is_typed_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm, "each on typed vector requires a proc with 1 parameter, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_typed_vec_count(tvec);
          uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          /* Phase 5a: push inline if closure has inline struct params */
          bool push_inline = closure->has_inline_params;

          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              /* Untyped callback (e.g. lambda $it): materialize to heap struct */
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else if (jacl_is_typed_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm, "each on typed map requires a proc with 2 parameters, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(coll_val);
          StructTypeDef* kdef = (key_type_idx != 0xFFFF) ? vm->struct_registry->defs[key_type_idx] : NULL;
          uint32_t kwidth = kdef ? ((kdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal)) : 0;
          uint32_t vwidth = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          bool push_inline = closure->has_inline_params;
          jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
          jacl_typed_map_iter_result ir;

          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            /* Push key: inline for struct-typed param, heap struct or dyn otherwise */
            if (push_inline && kdef) {
              memset(&vm->stack[vm->stack_top], 0, kwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ir.item->slots, kdef->total_size);
              for (uint32_t si = 0; si < kwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += kwidth;
            } else if (kdef) {
              gc__current_heap = &vm->heap;
              HeapRecord* ks = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                      sizeof(HeapRecord) + kdef->total_size);
              ks->type_idx = key_type_idx;
              ks->total_size = kdef->total_size;
              memcpy(ks->data, ir.item->slots, kdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(ks));
              if (result != VM_OK) return result;
            } else {
              result = vm__push(vm, jacl_typed_map_key_from_leaf(ir.item));
              if (result != VM_OK) return result;
            }
            /* Push value: inline for struct-typed param, heap struct otherwise */
            const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, vwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
              for (uint32_t si = 0; si < vwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += vwidth;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, val_ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal discard;
            result = vm__pop(vm, &discard);
            if (result != VM_OK) return result;

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
        } else {
          vm__set_error(vm, "type error in 'each': expected typed vector or map, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_TRANSFORM: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; break; }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "type error in 'transform': expected closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure* closure = jacl_as_closure(closure_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];

        if (jacl_is_typed_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm, "transform on typed vector requires a proc with 1 parameter, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_typed_vec_count(tvec);
          uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          bool push_inline = closure->has_inline_params;
          gc__current_heap = &vm->heap;
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            gc__current_heap = &vm->heap;
            result_vec = jacl_vec_push_back(result_vec, ret);

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else if (jacl_is_typed_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm, "transform on typed map requires a proc with 2 parameters, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(coll_val);
          StructTypeDef* kdef = (key_type_idx != 0xFFFF) ? vm->struct_registry->defs[key_type_idx] : NULL;
          uint32_t kwidth = kdef ? ((kdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal)) : 0;
          uint32_t vwidth = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
          bool push_inline = closure->has_inline_params;
          jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
          jacl_typed_map_iter_result ir;
          gc__current_heap = &vm->heap;
          jacl_vec_root* result_vec = jacl_vec_empty();

          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline && kdef) {
              memset(&vm->stack[vm->stack_top], 0, kwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ir.item->slots, kdef->total_size);
              for (uint32_t si = 0; si < kwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += kwidth;
            } else if (kdef) {
              gc__current_heap = &vm->heap;
              HeapRecord* ks = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                      sizeof(HeapRecord) + kdef->total_size);
              ks->type_idx = key_type_idx;
              ks->total_size = kdef->total_size;
              memcpy(ks->data, ir.item->slots, kdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(ks));
              if (result != VM_OK) return result;
            } else {
              result = vm__push(vm, jacl_typed_map_key_from_leaf(ir.item));
              if (result != VM_OK) return result;
            }
            const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, vwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
              for (uint32_t si = 0; si < vwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += vwidth;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, val_ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            gc__current_heap = &vm->heap;
            result_vec = jacl_vec_push_back(result_vec, ret);

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_vector_ptr(result_vec));
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm, "type error in 'transform': expected typed vector or map, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        break;
      }

      case OP_TYPED_FILTER: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal closure_val, coll_val;
        result = vm__pop(vm, &closure_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &coll_val); if (result != VM_OK) return result;
        if (jacl_is_error(coll_val)) { result = vm__push(vm, coll_val); if (result != VM_OK) return result; break; }
        if (jacl_is_error(closure_val)) { result = vm__push(vm, closure_val); if (result != VM_OK) return result; break; }

        if (!jacl_is_closure(closure_val)) {
          vm__set_error(vm, "type error in 'filter': expected closure, got %s",
                       vm__type_name(closure_val));
          return VM_RUNTIME_ERROR;
        }
        JaclClosure* closure = jacl_as_closure(closure_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);

        if (jacl_is_typed_vector(coll_val)) {
          if (closure->param_count != 1) {
            vm__set_error(vm, "filter on typed vector requires a proc with 1 parameter, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(coll_val);
          uint32_t count = jacl_typed_vec_count(tvec);
          bool push_inline = closure->has_inline_params;
          gc__current_heap = &vm->heap;
          jacl_typed_vec_root* result_tvec = jacl_typed_vec_empty_strided(width);

          for (uint32_t i = 0; i < count; i++) {
            const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              const JaclVal* ptr2 = jacl_typed_vec_get_ptr(tvec, i);
              gc__current_heap = &vm->heap;
              result_tvec = jacl_typed_vec_push_back_wide(result_tvec, ptr2);
            }

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_typed_vector_ptr(result_tvec));
          if (result != VM_OK) return result;

        } else if (jacl_is_typed_map(coll_val)) {
          if (closure->param_count != 2) {
            vm__set_error(vm, "filter on typed map requires a proc with 2 parameters, got %d",
                         (int)closure->param_count);
            return VM_RUNTIME_ERROR;
          }
          jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(coll_val);
          StructTypeDef* kdef = (key_type_idx != 0xFFFF) ? vm->struct_registry->defs[key_type_idx] : NULL;
          uint32_t kwidth = kdef ? vm__struct_width(kdef) : 0;
          uint32_t key_stride = kdef ? kwidth : 1;
          bool push_inline = closure->has_inline_params;
          jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
          jacl_typed_map_iter_result ir;
          gc__current_heap = &vm->heap;
          jacl_typed_map_node* result_tmap = NULL;

          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;

            result = vm__push(vm, closure_val);
            if (result != VM_OK) return result;
            if (push_inline && kdef) {
              memset(&vm->stack[vm->stack_top], 0, kwidth * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], ir.item->slots, kdef->total_size);
              for (uint32_t si = 0; si < kwidth; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += kwidth;
            } else if (kdef) {
              gc__current_heap = &vm->heap;
              HeapRecord* ks = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                      sizeof(HeapRecord) + kdef->total_size);
              ks->type_idx = key_type_idx;
              ks->total_size = kdef->total_size;
              memcpy(ks->data, ir.item->slots, kdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(ks));
              if (result != VM_OK) return result;
            } else {
              result = vm__push(vm, jacl_typed_map_key_from_leaf(ir.item));
              if (result != VM_OK) return result;
            }
            const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
            if (push_inline) {
              memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
              memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
              for (uint32_t si = 0; si < width; si++)
                BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
              vm->stack_top += width;
            } else {
              gc__current_heap = &vm->heap;
              HeapRecord* s = (HeapRecord*)gc_alloc(&vm->heap, OBJ_HEAP_RECORD,
                                                     sizeof(HeapRecord) + sdef->total_size);
              s->type_idx = type_idx;
              s->total_size = sdef->total_size;
              memcpy(s->data, val_ptr, sdef->total_size);
              result = vm__push(vm, jacl_heap_record_val(s));
              if (result != VM_OK) return result;
            }

            if (vm->frame_count >= VM_FRAMES_MAX) {
              vm__set_error(vm, "stack overflow");
              return VM_RUNTIME_ERROR;
            }
            uint32_t caller_fc = vm->frame_count;
            CallFrame* cf = &vm->frames[vm->frame_count++];
            cf->closure    = closure;
            cf->return_ip  = vm->ip;
            cf->stack_base = vm->stack_top - closure->param_total_slots;
            cf->chunk      = &closure->chunk;

            uint8_t* saved_ip = vm->ip;
            BytecodeChunk* saved_chunk = vm->chunk;
            vm->ip    = closure->chunk.code;
            vm->chunk = &closure->chunk;

            VMResult call_result = vm__run(vm, caller_fc);
            if (call_result != VM_OK) return call_result;

            JaclVal ret;
            result = vm__pop(vm, &ret);
            if (result != VM_OK) return result;

            if (!vm__is_falsy(ret)) {
              const JaclVal* key_data = ir.item->slots;
              gc__current_heap = &vm->heap;
              result_tmap = jacl_typed_map_set_wide(result_tmap, key_data, key_stride, val_ptr, width);
            }

            vm->ip    = saved_ip;
            vm->chunk = saved_chunk;
            frame = &vm->frames[vm->frame_count - 1];
          }
          result = vm__push(vm, jacl_typed_map_ptr(result_tmap));
          if (result != VM_OK) return result;

        } else {
          vm__set_error(vm, "type error in 'filter': expected typed vector or map, got %s",
                       vm__type_name(coll_val));
          return VM_RUNTIME_ERROR;
        }
        break;
      }

      /* ===== Typed Map opcodes ===== */

      case OP_TYPED_MAP: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        uint8_t pair_count = vm__read_byte(vm);
        gc__current_heap = &vm->heap;
        jacl_typed_map_node* tmap = NULL;

        /* Pop pairs right-to-left into scratch buffers. */
        StructTypeDef* vsdef = vm->struct_registry->defs[type_idx];
        uint32_t vw = vm__struct_width(vsdef);
        uint32_t kw = 1;
        if (key_type_idx != 0xFFFF) {
          StructTypeDef* ksdef = vm->struct_registry->defs[key_type_idx];
          kw = vm__struct_width(ksdef);
        }
        if ((size_t)pair_count * (kw + vw) > VM_MAX_STRUCT_SLOTS * 256) {
          vm__set_error(vm, "typed-map literal too large");
          return VM_RUNTIME_ERROR;
        }
        JaclVal scratch[VM_MAX_STRUCT_SLOTS * 256];
        for (int32_t i = (int32_t)pair_count - 1; i >= 0; i--) {
          uint32_t off = (uint32_t)i * (kw + vw);
          vm__pop_struct(vm, type_idx, &scratch[off + kw]);
          if (key_type_idx == 0xFFFF) {
            JaclVal k;
            result = vm__pop(vm, &k); if (result != VM_OK) return result;
            scratch[off] = k;
          } else {
            vm__pop_struct(vm, key_type_idx, &scratch[off]);
          }
        }
        for (uint8_t i = 0; i < pair_count; i++) {
          uint32_t off = (uint32_t)i * (kw + vw);
          tmap = jacl_typed_map_set_wide(tmap, &scratch[off], kw,
                                         &scratch[off + kw], vw);
        }
        result = vm__push(vm, jacl_typed_map_ptr(tmap));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_GET: {
        /* Dead opcode — replaced by OP_TYPED_MAP_GET_INLINE (Phase 5f). */
        vm__set_error(vm, "OP_TYPED_MAP_GET is no longer emitted");
        return VM_RUNTIME_ERROR;
      }

      case OP_TYPED_MAP_GET_INLINE: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        jacl_typed_map_leaf* leaf;
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_struct(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        leaf = jacl_typed_map_get_leaf(tmap, key_slots, kw);

        if (!leaf) {
          vm__set_error(vm, "map-get: key not found in typed map");
          return VM_RUNTIME_ERROR;
        }

        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);
        const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(leaf);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_error(vm, "stack overflow in typed map get inline");
          return VM_RUNTIME_ERROR;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], val_ptr, sdef->total_size);
        for (uint32_t si = 0; si < width; si++) {
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        }
        vm->stack_top += width;
        break;
      }

      case OP_TYPED_MAP_SET: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal val_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t vw = vm__pop_struct(vm, type_idx, val_slots);
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_struct(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);

        gc__current_heap = &vm->heap;
        jacl_typed_map_node* new_tmap = jacl_typed_map_set_wide(tmap, key_slots, kw, val_slots, vw);
        result = vm__push(vm, jacl_typed_map_ptr(new_tmap));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_HAS: {
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_struct(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        bool found = (key_type_idx == 0xFFFF)
                       ? jacl_typed_map_has(tmap, key_slots[0])
                       : jacl_typed_map_has_wide(tmap, key_slots, kw);
        result = vm__push(vm, jacl_bool(found));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_REMOVE: {
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal key_slots[VM_MAX_STRUCT_SLOTS];
        uint32_t kw;
        if (key_type_idx == 0xFFFF) {
          result = vm__pop(vm, &key_slots[0]); if (result != VM_OK) return result;
          kw = 1;
        } else {
          kw = vm__pop_struct(vm, key_type_idx, key_slots);
        }
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        gc__current_heap = &vm->heap;
        jacl_typed_map_node* new_tmap = (key_type_idx == 0xFFFF)
                       ? jacl_typed_map_unset(tmap, key_slots[0])
                       : jacl_typed_map_unset_wide(tmap, key_slots, kw);
        result = vm__push(vm, jacl_typed_map_ptr(new_tmap));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_LEN: {
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;
        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        result = vm__push(vm, jacl_i32((int32_t)jacl_typed_map_count(tmap)));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_KEYS: {
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;

        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        gc__current_heap = &vm->heap;
        jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
        jacl_typed_map_iter_result ir;

        if (key_type_idx == 0xFFFF) {
          /* Dyn keys → return dyn vec */
          jacl_vec_root* vec = jacl_vec_empty();
          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;
            JaclVal key = jacl_typed_map_key_from_leaf(ir.item);
            vec = jacl_vec_push_back(vec, key);
          }
          result = vm__push(vm, jacl_vector_ptr(vec));
        } else {
          /* Struct keys → return typed vec */
          StructTypeDef* kdef = vm->struct_registry->defs[key_type_idx];
          uint32_t kw = vm__struct_width(kdef);
          jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(kw);
          for (;;) {
            ir = jacl_typed_map_next_leaf(&it);
            if (ir.done) break;
            const JaclVal* key_ptr = ir.item->slots;  /* raw key slots */
            tvec = jacl_typed_vec_push_back_wide(tvec, key_ptr);
          }
          result = vm__push(vm, jacl_typed_vector_ptr(tvec));
        }
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_VALS: {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;

        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = vm__struct_width(sdef);

        gc__current_heap = &vm->heap;
        jacl_typed_vec_root* tvec = jacl_typed_vec_empty_strided(width);
        jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
        jacl_typed_map_iter_result ir;
        for (;;) {
          ir = jacl_typed_map_next_leaf(&it);
          if (ir.done) break;
          const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
          tvec = jacl_typed_vec_push_back_wide(tvec, val_ptr);
        }
        result = vm__push(vm, jacl_typed_vector_ptr(tvec));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_VEC_PRINT: {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal tvec_val;
        result = vm__pop(vm, &tvec_val); if (result != VM_OK) return result;

        jacl_typed_vec_root* tvec = (jacl_typed_vec_root*)jacl_as_ptr(tvec_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t count = jacl_typed_vec_count(tvec);

        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_append(&fmt, "[", 1);
        for (uint32_t i = 0; i < count; i++) {
          if (i > 0) vm__fmt_append(&fmt, ", ", 2);
          const JaclVal* ptr = jacl_typed_vec_get_ptr(tvec, i);
          vm__fmt_struct_data(&fmt, sdef, (const uint8_t*)ptr);
        }
        vm__fmt_append(&fmt, "]\n", 2);
        vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_PRINT: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal tmap_val;
        result = vm__pop(vm, &tmap_val); if (result != VM_OK) return result;

        jacl_typed_map_node* tmap = (jacl_typed_map_node*)jacl_as_ptr(tmap_val);
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        StructTypeDef* kdef = (key_type_idx != 0xFFFF) ? vm->struct_registry->defs[key_type_idx] : NULL;

        VMFormatBuf fmt;
        vm__fmt_init(&fmt, vm->arena, vm->struct_registry);
        vm__fmt_append(&fmt, "{", 1);
        jacl_typed_map_iter it = jacl_typed_map_iter_init(tmap);
        jacl_typed_map_iter_result ir;
        uint32_t idx = 0;
        for (;;) {
          ir = jacl_typed_map_next_leaf(&it);
          if (ir.done) break;
          if (idx > 0) vm__fmt_append(&fmt, ", ", 2);
          if (kdef) {
            const JaclVal* key_ptr = ir.item->slots;
            vm__fmt_struct_data(&fmt, kdef, (const uint8_t*)key_ptr);
          } else {
            JaclVal key = jacl_typed_map_key_from_leaf(ir.item);
            vm__fmt_value(&fmt, key);
          }
          vm__fmt_append(&fmt, ": ", 2);
          const JaclVal* val_ptr = jacl_typed_map_value_ptr_from_leaf(ir.item);
          vm__fmt_struct_data(&fmt, sdef, (const uint8_t*)val_ptr);
          idx++;
        }
        vm__fmt_append(&fmt, "}\n", 2);
        vm->print_fn(fmt.data, fmt.len, vm->print_ctx);
        result = vm__push(vm, JACL_NIL);
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_VEC_EQ: {
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;

        jacl_typed_vec_root* a = (jacl_typed_vec_root*)jacl_as_ptr(a_val);
        jacl_typed_vec_root* b = (jacl_typed_vec_root*)jacl_as_ptr(b_val);

        if (a == b) {
          result = vm__push(vm, JACL_TRUE);
          if (result != VM_OK) return result;
          break;
        }

        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t count = jacl_typed_vec_count(a);
        if (count != jacl_typed_vec_count(b)) {
          result = vm__push(vm, JACL_FALSE);
          if (result != VM_OK) return result;
          break;
        }

        bool equal = true;
        for (uint32_t i = 0; i < count; i++) {
          const JaclVal* pa = jacl_typed_vec_get_ptr(a, i);
          const JaclVal* pb = jacl_typed_vec_get_ptr(b, i);
          if (memcmp(pa, pb, sdef->total_size) != 0) {
            equal = false;
            break;
          }
        }
        result = vm__push(vm, jacl_bool(equal));
        if (result != VM_OK) return result;
        break;
      }

      case OP_TYPED_MAP_EQ: {
        uint16_t type_idx = vm__read_u16(vm);
        uint16_t key_type_idx = vm__read_u16(vm);
        JaclVal b_val, a_val;
        result = vm__pop(vm, &b_val); if (result != VM_OK) return result;
        result = vm__pop(vm, &a_val); if (result != VM_OK) return result;

        jacl_typed_map_node* a = (jacl_typed_map_node*)jacl_as_ptr(a_val);
        jacl_typed_map_node* b = (jacl_typed_map_node*)jacl_as_ptr(b_val);

        if (a == b) {
          result = vm__push(vm, JACL_TRUE);
          if (result != VM_OK) return result;
          break;
        }

        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        if (jacl_typed_map_count(a) != jacl_typed_map_count(b)) {
          result = vm__push(vm, JACL_FALSE);
          if (result != VM_OK) return result;
          break;
        }

        uint32_t key_stride = 1;
        if (key_type_idx != 0xFFFF) {
          StructTypeDef* kdef = vm->struct_registry->defs[key_type_idx];
          key_stride = vm__struct_width(kdef);
        }

        bool equal = true;
        jacl_typed_map_iter it = jacl_typed_map_iter_init(a);
        jacl_typed_map_iter_result ir;
        for (;;) {
          ir = jacl_typed_map_next_leaf(&it);
          if (ir.done) break;
          jacl_typed_map_leaf* b_leaf;
          if (key_type_idx == 0xFFFF) {
            JaclVal key = jacl_typed_map_key_from_leaf(ir.item);
            b_leaf = jacl_typed_map_get_leaf(b, &key, 1);
          } else {
            const JaclVal* key_ptr = ir.item->slots;
            b_leaf = jacl_typed_map_get_leaf(b, key_ptr, key_stride);
          }
          if (!b_leaf) { equal = false; break; }
          const JaclVal* va = jacl_typed_map_value_ptr_from_leaf(ir.item);
          const JaclVal* vb = jacl_typed_map_value_ptr_from_leaf(b_leaf);
          if (memcmp(va, vb, sdef->total_size) != 0) {
            equal = false;
            break;
          }
        }
        result = vm__push(vm, jacl_bool(equal));
        if (result != VM_OK) return result;
        break;
      }

      case OP_DEREF_INLINE: {
        /* Phase 5d: Deref a struct box, push inline bytes directly.
           Operand: uint16_t type_idx.
           Avoids heap allocation (unlike OP_DEREF for struct boxes). */
        uint16_t type_idx = vm__read_u16(vm);
        JaclVal container;
        result = vm__pop(vm, &container);
        if (result != VM_OK) return result;
        if (!jacl_is_box(container)) {
          vm__set_error(vm, "deref: expected box, got %s", vm__type_name(container));
          return VM_RUNTIME_ERROR;
        }
        JaclMutableRef* ref = (JaclMutableRef*)jacl_as_ptr(container);
        if (!vm->struct_registry || type_idx >= vm->struct_registry->count) {
          vm__set_error(vm, "deref: invalid struct type index %u", (unsigned)type_idx);
          return VM_RUNTIME_ERROR;
        }
        StructTypeDef* sdef = vm->struct_registry->defs[type_idx];
        uint32_t width = (sdef->total_size + sizeof(JaclVal) - 1) / sizeof(JaclVal);
        if (vm->stack_top + width > VM_STACK_MAX) {
          vm__set_error(vm, "stack overflow (deref inline)");
          return VM_STACK_OVERFLOW;
        }
        memset(&vm->stack[vm->stack_top], 0, width * sizeof(JaclVal));
        memcpy(&vm->stack[vm->stack_top], ref->data, sdef->total_size);
        for (uint32_t si = 0; si < width; si++)
          BITMAP_SET(vm->inline_slot_bitmap, vm->stack_top + si);
        vm->stack_top += width;
        break;
      }

      default: {
        vm__set_error(vm, "unknown opcode %d", (int)instruction);
        return VM_RUNTIME_ERROR;
      }
    }
  }
}

#undef VM__BINARY_NUMERIC_OP

/* --- Multi-module execution: jacl_exec_program --- */

/* --- ctx subsystem startup ---
 *
 * Initialize the ctx pool and allocate the initial ctx struct for a VM.
 * Sets vm->ctx_pool and vm->ctx.  pool_storage must remain valid for
 * the VM's lifetime (typically stack-allocated by the caller).
 */
void ctx__init_vm(VM *vm, JaclCtxPool *pool_storage) {
    StructTypeRegistry *reg = vm->struct_registry;
    if (!reg || reg->ctx_type_idx == 0) return;

    ctx_pool_init(pool_storage, &vm->heap, reg);
    vm->ctx_pool = pool_storage;

    /* Allocate initial ctx from pool */
    HeapRecord *ctx_struct = ctx_pool_alloc(pool_storage, &vm->heap);
    StructTypeDef *sdef = reg->defs[reg->ctx_type_idx];

    /* Initialize all fields with their compile-time defaults */
    for (uint32_t i = 0; i < sdef->field_count; i++) {
        if (sdef->fields[i].default_val != JACL_NIL) {
            vm__heap_record_write_field(ctx_struct, sdef->fields[i].offset,
                                   sdef->fields[i].type,
                                   sdef->fields[i].default_val);
        }
    }

    /* Override built-in pwd field (field 0) with actual getcwd() */
    char cwd_buf[4096];
    if (getcwd(cwd_buf, sizeof(cwd_buf))) {
        size_t len = strlen(cwd_buf);
        JaclVal pwd_str;
        if (len <= 7) {
            pwd_str = jacl_inline_string(cwd_buf, len);
        } else {
            pwd_str = jacl_rope_string_create(&vm->heap,
                                              (const uint8_t *)cwd_buf, len);
        }
        vm__heap_record_write_field(ctx_struct, sdef->fields[0].offset,
                               sdef->fields[0].type, pwd_str);
    }

    vm->ctx = jacl_heap_record_val(ctx_struct);
}

/**
 * Execute a compiled multi-module program.
 * Initializes modules in topological order (dependencies first),
 * then executes the root module (last in the array).
 * For CPS-transformed root modules, handles continuation setup.
 */
VMResult jacl_exec_program(ProgramResult* program, VM* vm) {
  if (!program || program->module_count == 0) {
    vm->error_message = "no modules to execute";
    return VM_RUNTIME_ERROR;
  }

  vm->struct_registry = program->struct_registry;

  /* Initialize ctx subsystem (pool + initial ctx with pwd) */
  JaclCtxPool exec_ctx_pool;
  ctx__init_vm(vm, &exec_ctx_pool);

  /* Execute each module's chunk in topological order.
     Dependencies come first, root module is last. */
  uint32_t last = program->module_count - 1;

  /* Execute dependency modules (non-root) */
  for (uint32_t i = 0; i < last; i++) {
    Module* mod = program->modules[i];
    vm->stack_top = 0;
    VMResult r = vm_exec(vm, mod->chunk);
    if (r != VM_OK) {
      /* Wrap error with module context */
      const char* slash = strrchr(mod->path, '/');
      const char* basename = slash ? slash + 1 : mod->path;
      char buf[256];
      snprintf(buf, sizeof(buf), "error in module '%s': %s",
               basename,
               vm->error_message ? vm->error_message : "unknown error");
      char* msg = (char*)arena_alloc(vm->arena, (uint32_t)(strlen(buf) + 1));
      memcpy(msg, buf, strlen(buf) + 1);
      vm->error_message = msg;
      return r;
    }
  }

  /* Execute root module */
  Module* root = program->modules[last];
  vm->stack_top = 0;

  if (program->suspending) {
    /* Suspending root: chunk produces __main closure on stack.
       Execute chunk to get the closure, then call it. */
    VMResult r = vm_exec(vm, root->chunk);
    if (r != VM_OK) return r;

    JaclVal main_cl_val = vm->stack[0];
    if (!jacl_is_closure(main_cl_val)) {
      vm->error_message = "internal error: suspending top-level did not produce closure";
      return VM_RUNTIME_ERROR;
    }
    JaclClosure *main_cl = jacl_as_closure(main_cl_val);

    JaclClosure top_closure_wrapper;
    memset(&top_closure_wrapper, 0, sizeof(top_closure_wrapper));
    top_closure_wrapper.chunk = *root->chunk;

    if (main_cl->is_sm_compiled) {
      /* SM main closure: create state machine, call with (sm_val, nil) */
      JaclVal sm_val = gc_alloc_state_machine(&vm->heap, main_cl->sm_field_count);
      JaclStateMachine *sm = jacl_as_state_machine(sm_val);
      sm->sm_closure = main_cl_val;

      vm->stack_top = 0;
      vm->stack[0]  = main_cl_val;
      vm->stack[1]  = sm_val;
      vm->stack[2]  = JACL_NIL;
      vm->stack_top = 3;

      vm->frames[0].closure    = &top_closure_wrapper;
      vm->frames[0].return_ip  = NULL;
      vm->frames[0].stack_base = 0;
      vm->frames[0].chunk      = root->chunk;
      vm->frame_count = 1;

      vm->frames[1].closure    = main_cl;
      vm->frames[1].return_ip  = NULL;
      vm->frames[1].stack_base = 1; /* 2 args (__sm, __rv) */
      vm->frames[1].chunk      = &main_cl->chunk;
      vm->frame_count = 2;
      vm->ip    = main_cl->chunk.code;
      vm->chunk = &main_cl->chunk;
      vm->top_chunk = &main_cl->chunk;

      return vm__run(vm, 1);
    }

    /* CPS fallback */
    JaclVal completion = jacl_future(&vm->heap);
    JaclVal resolve_k = runtime__create_resolve_closure(&vm->heap, vm->arena,
                                                         completion);

    vm->stack_top = 0;
    vm->stack[0]  = main_cl_val;
    vm->stack[1]  = resolve_k;
    vm->stack_top = 2;

    vm->frames[0].closure    = &top_closure_wrapper;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 0;
    vm->frames[0].chunk      = root->chunk;
    vm->frame_count = 1;

    vm->frames[1].closure    = main_cl;
    vm->frames[1].return_ip  = NULL;
    vm->frames[1].stack_base = 1;
    vm->frames[1].chunk      = &main_cl->chunk;
    vm->frame_count = 2;
    vm->ip    = main_cl->chunk.code;
    vm->chunk = &main_cl->chunk;
    vm->top_chunk = &main_cl->chunk;

    r = vm__run(vm, 1);

    JaclFuture *cfut = jacl_as_future(completion);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_RESOLVED) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    } else if (state == FUTURE_ERROR) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    }
    return r;
  }

  return vm_exec(vm, root->chunk);
}

/* --- Context lifecycle --- */

jacl_context_t *jacl_ctx_new(jacl_context_t *parent) {
    jacl_context_t *ctx = (jacl_context_t *)calloc(1, sizeof(jacl_context_t));
    if (!ctx) return NULL;

    ctx->parent = parent;
    ctx->restriction_set = UINT64_MAX;  /* all-permissive */

    /* Arena: zero-initialized by calloc (uses default allocator) */

    /* VM */
    vm_init(&ctx->vm, &ctx->arena);

    if (parent) {
        /* Child: share parent's intern table */
        ctx->owns_intern_table = false;
        ctx->vm.intern_table = &parent->intern_table;
    } else {
        /* Root: own intern table */
        ctx->owns_intern_table = true;
        intern_table_init(&ctx->intern_table, &ctx->arena);
        ctx->vm.intern_table = &ctx->intern_table;
    }

    macro_table_init(&ctx->macro_table);

    /* Expansion state is zero-initialized by calloc */

    return ctx;
}

void jacl_ctx_destroy(jacl_context_t *ctx) {
    if (!ctx) return;
    /* Free the defs pointer array in the struct registry (StructTypeDefs are in the arena) */
    if (ctx->vm.struct_registry)
        struct_registry__destroy(ctx->vm.struct_registry);
    vm_destroy(&ctx->vm);
    if (ctx->owns_intern_table)
        intern_table_destroy(&ctx->intern_table);
    arena_destroy(&ctx->arena);
    free(ctx);
}

/* --- Scoped context switching --- */

void jacl_ctx_save(jacl_ctx_saved_t *saved) {
    saved->heap   = gc__current_heap;
    saved->gc_fn  = gc__emergency_gc_fn;
    saved->gc_ctx = gc__emergency_gc_ctx;
}

void jacl_ctx_enter(jacl_context_t *ctx, jacl_ctx_saved_t *saved) {
    jacl_ctx_save(saved);
    gc__current_heap     = &ctx->vm.heap;
    gc__emergency_gc_fn  = vm__emergency_gc_single;
    gc__emergency_gc_ctx = &ctx->vm;
}

void jacl_ctx_restore(jacl_ctx_saved_t saved) {
    gc__current_heap     = saved.heap;
    gc__emergency_gc_fn  = saved.gc_fn;
    gc__emergency_gc_ctx = saved.gc_ctx;
}

/* --- source_to_closure_in_place: compile source into a closure on caller's heap --- */

JaclVal source_to_closure_in_place(const char *src, size_t len,
                                   arena_t *arena, ThreadHeap *heap,
                                   JaclInternTable *intern_table,
                                   ExpandState *expand,
                                   JaclError *err_out,
                                   JaclVal prelude_map) {
    if (err_out) {
        err_out->kind = JACL_ERROR_NONE;
        err_out->message = NULL;
        err_out->line = 0;
        err_out->col = 0;
    }
    if (!src || len == 0) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = "empty source";
        }
        return JACL_NIL;
    }

    /* Copy source to arena so it's NUL-terminated for the lexer */
    char *buf = (char *)arena_alloc(arena, (uint32_t)(len + 1));
    if (!buf) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = "out of memory";
        }
        return JACL_NIL;
    }
    memcpy(buf, src, len);
    buf[len] = '\0';

    /* Lex */
    LexResult tokens = lexer_lex(buf, arena);

    /* Parse */
    ParseResult parse = parser_parse(tokens, arena);
    if (parse.error_count > 0) {
        const char *first_msg = "parse error";
        uint32_t err_line = 1, err_col = 1;
        for (uint32_t i = 0; i < parse.count; i++) {
            if (parse.nodes[i] && parse.nodes[i]->type == AST_ERROR) {
                first_msg = parse.nodes[i]->data.error.message;
                err_line = parse.nodes[i]->start.line;
                err_col = parse.nodes[i]->start.column;
                break;
            }
        }
        if (err_out) {
            /* Format with location like compile errors */
            char buf[256];
            int n = snprintf(buf, sizeof(buf), "line %u, col %u: %s",
                             err_line, err_col, first_msg);
            if (n < 0) n = 0;
            char *msg = (char *)arena_alloc(arena, (uint32_t)n + 1);
            if (msg) {
                memcpy(msg, buf, (uint32_t)n + 1);
                err_out->message = msg;
            } else {
                err_out->message = first_msg;
            }
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->line = err_line;
            err_out->col = err_col;
        }
        return JACL_NIL;
    }

    /* Compile (macro expansion happens inside compiler_compile) */
    CompileResult cr = compiler_compile(parse, arena, intern_table,
                                        heap, NULL, expand, prelude_map);
    if (cr.error_count > 0) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = cr.error_message ? cr.error_message : "compile error";
        }
        return JACL_NIL;
    }

    /* Wrap the compiled bytecode into a 0-arg closure on the arena */
    JaclClosure *closure = (JaclClosure *)arena_alloc(arena, sizeof(JaclClosure));
    if (!closure) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = "out of memory";
        }
        return JACL_NIL;
    }
    memset(closure, 0, sizeof(JaclClosure));
    closure->chunk       = cr.chunk;
    closure->param_count = 0;
    closure->min_args    = 0;
    closure->variadic    = false;
    closure->name        = "<interpret>";

    return jacl_closure(closure);
}

/* --- jacl_ctx_run_source / jacl_ctx_run_closure (US-006) --- */

JaclVal jacl_ctx_run_source(jacl_context_t *ctx, const char *src, size_t len,
                            uint64_t restriction_set, JaclError *err_out) {
    if (err_out) { err_out->kind = JACL_ERROR_NONE; err_out->message = NULL; err_out->line = 0; err_out->col = 0; }
    if (!ctx || !src) return JACL_NIL;

    ctx->restriction_set = restriction_set;

    /* Copy source to arena so it's NUL-terminated for the lexer */
    char *buf = (char *)arena_alloc(&ctx->arena, (uint32_t)(len + 1));
    memcpy(buf, src, len);
    buf[len] = '\0';

    /* Lex */
    LexResult tokens = lexer_lex(buf, &ctx->arena);

    /* Parse */
    ParseResult parse = parser_parse(tokens, &ctx->arena);
    if (parse.error_count > 0) {
        const char *first_err = "parse error";
        for (uint32_t i = 0; i < parse.count; i++) {
            if (parse.nodes[i] && parse.nodes[i]->type == AST_ERROR) {
                first_err = parse.nodes[i]->data.error.message;
                break;
            }
        }
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = first_err;
        }
        return JACL_NIL;
    }

    /* Compile */
    JaclInternTable *itab = ctx->owns_intern_table ? &ctx->intern_table
                                                   : ctx->vm.intern_table;
    ctx->expand.ctx = ctx;
    CompileResult cr = compiler_compile(parse, &ctx->arena, itab,
                                        &ctx->vm.heap, NULL, &ctx->expand,
                                        JACL_NIL);
    if (cr.error_count > 0) {
        if (err_out) {
            err_out->kind = JACL_ERROR_COMPILE;
            err_out->message = cr.error_message ? cr.error_message : "compile error";
        }
        return JACL_NIL;
    }

    /* Execute */
    ctx->vm.intern_table   = itab;
    ctx->vm.struct_registry = cr.struct_registry;

    /* Initialize ctx subsystem (pool + initial ctx with pwd) */
    JaclCtxPool ctx_run_pool;
    ctx__init_vm(&ctx->vm, &ctx_run_pool);

    VMResult r = vm_exec(&ctx->vm, &cr.chunk);
    if (r != VM_OK) {
        if (err_out) {
            err_out->kind    = JACL_ERROR_RUNTIME;
            err_out->message = ctx->vm.error_message ? ctx->vm.error_message : "runtime error";
            err_out->line    = ctx->vm.error_line;
        }
        return JACL_NIL;
    }

    return (ctx->vm.stack_top > 0) ? ctx->vm.stack[0] : JACL_NIL;
}

JaclVal jacl_ctx_run_closure(jacl_context_t *ctx, JaclClosure *closure,
                             JaclVal *args, uint32_t arg_count,
                             JaclError *err_out) {
    if (err_out) { err_out->kind = JACL_ERROR_NONE; err_out->message = NULL; err_out->line = 0; err_out->col = 0; }
    if (!ctx || !closure) return JACL_NIL;

    VM *vm = &ctx->vm;

    /* Set up stack: [closure_val, args...] */
    JaclVal closure_val = jacl_closure_ptr(closure);
    vm->stack[0] = closure_val;
    for (uint32_t i = 0; i < arg_count; i++)
        vm->stack[1 + i] = args[i];
    vm->stack_top = 1 + arg_count;

    /* Set up call frame */
    vm->frames[0].closure    = closure;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 1;
    vm->frames[0].chunk      = &closure->chunk;
    vm->frame_count = 1;

    vm->ip        = closure->chunk.code;
    vm->chunk     = &closure->chunk;
    vm->top_chunk = &closure->chunk;

    VMResult r = vm__run(vm, 0);

    if (r != VM_OK) {
        if (err_out) {
            err_out->kind    = JACL_ERROR_RUNTIME;
            err_out->message = vm->error_message ? vm->error_message : "runtime error";
            err_out->line    = vm->error_line;
        }
        return JACL_NIL;
    }

    return (vm->stack_top > 0) ? vm->stack[0] : JACL_NIL;
}

/* --- Pipeline convenience: jacl_run --- */

/**
 * Source-to-execution pipeline.
 * Chains: lexer_lex -> parser_parse -> compiler_compile -> vm_exec.
 * Uses a stack-local ExpandState for reentrancy-safe macro expansion.
 * Returns VM_RUNTIME_ERROR on parse or compile errors (message in vm->error_message).
 */
static const char* jacl__find_parse_error(AstNode* node) {
  if (!node) return NULL;
  if (node->type == AST_ERROR) return node->data.error.message;
  /* Search inside blocks */
  if (node->type == AST_BLOCK) {
    for (uint32_t i = 0; i < node->data.block.count; i++) {
      const char* msg = jacl__find_parse_error(node->data.block.commands[i]);
      if (msg) return msg;
    }
  }
  /* Search inside commands (proc bodies, etc.) */
  if (node->type == AST_COMMAND) {
    const char* msg = jacl__find_parse_error(node->data.command.head);
    if (msg) return msg;
    for (uint32_t i = 0; i < node->data.command.arg_count; i++) {
      msg = jacl__find_parse_error(node->data.command.args[i]);
      if (msg) return msg;
    }
  }
  return NULL;
}

VMResult jacl_run(const char* source, VM* vm, arena_t* arena) {
  LexResult tokens = lexer_lex(source, arena);
  ParseResult parse = parser_parse(tokens, arena);
  if (parse.error_count > 0) {
    /* Extract first parse error message from AST_ERROR nodes (recursive) */
    const char* parse_err = NULL;
    for (uint32_t i = 0; i < parse.count && !parse_err; i++) {
      parse_err = jacl__find_parse_error(parse.nodes[i]);
    }
    if (parse_err) {
      /* Build "parse error: <detail>" message in arena */
      size_t prefix_len = 13; /* "parse error: " */
      size_t msg_len = strlen(parse_err);
      char* buf = (char*)arena_alloc(arena, (uint32_t)(prefix_len + msg_len + 1));
      memcpy(buf, "parse error: ", prefix_len);
      memcpy(buf + prefix_len, parse_err, msg_len + 1);
      vm->error_message = buf;
    } else {
      vm->error_message = "parse error";
    }
    return VM_RUNTIME_ERROR;
  }

  JaclInternTable intern_table;
  intern_table_init(&intern_table, arena);

  ExpandState es;
  memset(&es, 0, sizeof(es));

  /* Create a temporary context for macro closure execution. */
  jacl_ctx_saved_t macro_saved;
  jacl_ctx_save(&macro_saved);
  jacl_context_t *macro_ctx = jacl_ctx_new(NULL);
  es.ctx = macro_ctx;

  CompileResult cr = compiler_compile(parse, arena, &intern_table, &vm->heap, NULL, &es, JACL_NIL);

  jacl_ctx_destroy(macro_ctx);
  es.ctx = NULL;
  jacl_ctx_restore(macro_saved);

  if (cr.error_count > 0) {
    vm->error_message = cr.error_message ? cr.error_message : "compile error";
    struct_registry__destroy(cr.struct_registry);
    intern_table_destroy(&intern_table);
    return VM_RUNTIME_ERROR;
  }

  vm->intern_table = &intern_table;
  vm->struct_registry = cr.struct_registry;

  /* Initialize ctx subsystem (pool + initial ctx with pwd) */
  JaclCtxPool run_ctx_pool;
  ctx__init_vm(vm, &run_ctx_pool);

  if (cr.suspending) {
    /* Top-level code contains suspension. The chunk contains OP_CLOSURE + OP_HALT
       which produces the main closure on the stack. Execute the chunk to
       get the closure, then call it. */
    VMResult r = vm_exec(vm, &cr.chunk);
    if (r != VM_OK) {
      struct_registry__destroy(cr.struct_registry);
      intern_table_destroy(&intern_table);
      return r;
    }

    JaclVal main_cl_val = vm->stack[0];
    if (!jacl_is_closure(main_cl_val)) {
      vm->error_message = "internal error: suspending top-level did not produce closure";
      struct_registry__destroy(cr.struct_registry);
      intern_table_destroy(&intern_table);
      return VM_RUNTIME_ERROR;
    }
    JaclClosure *main_cl = jacl_as_closure(main_cl_val);

    JaclClosure top_closure_wrapper;
    memset(&top_closure_wrapper, 0, sizeof(top_closure_wrapper));
    top_closure_wrapper.chunk = cr.chunk;

    if (main_cl->is_sm_compiled) {
      /* SM main closure: create state machine, call with (sm_val, nil) */
      JaclVal sm_val = gc_alloc_state_machine(&vm->heap, main_cl->sm_field_count);
      JaclStateMachine *sm = jacl_as_state_machine(sm_val);
      sm->sm_closure = main_cl_val;

      vm->stack_top = 0;
      vm->stack[0]  = main_cl_val;
      vm->stack[1]  = sm_val;
      vm->stack[2]  = JACL_NIL;
      vm->stack_top = 3;

      vm->frames[0].closure    = &top_closure_wrapper;
      vm->frames[0].return_ip  = NULL;
      vm->frames[0].stack_base = 0;
      vm->frames[0].chunk      = &cr.chunk;
      vm->frame_count = 1;

      vm->frames[1].closure    = main_cl;
      vm->frames[1].return_ip  = NULL;
      vm->frames[1].stack_base = 1; /* 2 args (__sm, __rv) */
      vm->frames[1].chunk      = &main_cl->chunk;
      vm->frame_count = 2;
      vm->ip    = main_cl->chunk.code;
      vm->chunk = &main_cl->chunk;
      vm->top_chunk = &main_cl->chunk;

      r = vm__run(vm, 1);
      struct_registry__destroy(cr.struct_registry);
      intern_table_destroy(&intern_table);
      return r;
    }

    /* CPS fallback: call with resolve_k */
    JaclVal completion = jacl_future(&vm->heap);
    JaclVal resolve_k = runtime__create_resolve_closure(&vm->heap, arena,
                                                         completion);

    vm->stack_top = 0;
    vm->stack[0]  = main_cl_val;
    vm->stack[1]  = resolve_k;
    vm->stack_top = 2;

    vm->frames[0].closure    = &top_closure_wrapper;
    vm->frames[0].return_ip  = NULL;
    vm->frames[0].stack_base = 0;
    vm->frames[0].chunk      = &cr.chunk;
    vm->frame_count = 1;

    vm->frames[1].closure    = main_cl;
    vm->frames[1].return_ip  = NULL;
    vm->frames[1].stack_base = 1;
    vm->frames[1].chunk      = &main_cl->chunk;
    vm->frame_count = 2;
    vm->ip    = main_cl->chunk.code;
    vm->chunk = &main_cl->chunk;
    vm->top_chunk = &main_cl->chunk;

    r = vm__run(vm, 1);

    JaclFuture *cfut = jacl_as_future(completion);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_RESOLVED) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    } else if (state == FUTURE_ERROR) {
      vm->stack[0] = (JaclVal)cfut->result;
      vm->stack_top = 1;
    }
    struct_registry__destroy(cr.struct_registry);
    intern_table_destroy(&intern_table);
    return r;
  }

  VMResult result = vm_exec(vm, &cr.chunk);
  struct_registry__destroy(cr.struct_registry);
  intern_table_destroy(&intern_table);
  return result;
}

#endif /* VM_C */
