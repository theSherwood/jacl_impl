/* JACL Embedding API — VM lifecycle, eval, error handling, value constructors.
 *
 * Implements jacl_vm_new, jacl_vm_new_ex, jacl_vm_free,
 * jacl_eval, jacl_eval_file, jacl_is_error, jacl_error_message,
 * value constructors/extractors, and jacl_typeof.
 * Included after runtime.c in the unity build.
 */

#ifndef EMBED_C
#define EMBED_C

/* --- Emscripten export macro ---
 * In unity build: functions are static. For Emscripten WASM: exported. */
#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
  #define JACL_EMBED_FN EMSCRIPTEN_KEEPALIVE
#else
  #define JACL_EMBED_FN static
#endif

/* --- Types from jacl.h (cannot include directly due to redefinition conflicts) --- */

typedef struct JaclVM_s JaclVM;
typedef struct JaclTrampoline_s JaclTrampoline;

typedef struct {
  size_t   initial_heap_size;
  size_t   max_heap_size;
  uint32_t max_handles;
} JaclConfig;

/* --- Trampoline type IDs (for signature parsing and marshaling) --- */

#define TRAMP_TYPE_VOID    0
#define TRAMP_TYPE_I32     1
#define TRAMP_TYPE_I64     2
#define TRAMP_TYPE_U32     3
#define TRAMP_TYPE_U64     4
#define TRAMP_TYPE_F32     5
#define TRAMP_TYPE_F64     6
#define TRAMP_TYPE_PTR     7
#define TRAMP_TYPE_UNKNOWN 255

#ifdef JACL_HAS_LIBFFI
#include <ffi.h>
struct JaclTrampoline_s {
  JaclVM*         jvm;
  JaclVal         closure;         /* pinned closure value */
  uint32_t        handle_idx;      /* GC handle slot index */
  void*           ffi_closure;     /* libffi writable closure (for ffi_closure_free) */
  void*           code_ptr;        /* executable function pointer */
  ffi_cif         cif;
  ffi_type**      ffi_arg_types;   /* array of ffi_type pointers (malloc'd) */
  int             arg_types_id[16];
  int             arg_count;
  int             ret_type_id;
  JaclTrampoline* vm_next;         /* linked list node in JaclVM_s */
};
#else
struct JaclTrampoline_s { int _unused; }; /* stub — never allocated without libffi */
#endif

/* Forward declaration — defined at end of file in trampoline section */
static void embed__free_all_trampolines(JaclVM* jvm);

/* --- Native function signature and registry entry --- */

typedef JaclVal (*EmbedNativeFn)(JaclVM* vm, JaclVal* args, int argc);

typedef struct {
  EmbedNativeFn fn;     /* C function pointer */
  JaclVal       name;   /* inline string name */
  int8_t        arity;  /* expected arg count, -1 = variadic */
} NativeFnEntry;

#define NATIVE_FN_INIT_CAP 32

/* --- JaclVM wrapper (opaque to external callers) --- */

struct JaclVM_s {
  VM              vm;           /* internal VM */
  arena_t         arena;        /* owns all arena-allocated memory */
  JaclInternTable intern_table; /* persistent across evals */
  uint32_t        max_handles;  /* configured max handles */
  const char*     last_error;   /* last error message (arena-allocated) */
  /* GC handle storage */
  JaclVal*        handle_slots;      /* array of handle values (JACL_NIL = free) */
  uint32_t*       handle_free_list;  /* stack of free slot indices */
  uint32_t        handle_count;      /* number of allocated slots */
  uint32_t        handle_free_top;   /* top of free list stack */
  /* Native function registry */
  NativeFnEntry*  native_fns;        /* array of registered native functions */
  int8_t*         native_fn_arities; /* arity mirror for VM dispatch */
  uint32_t        native_fn_count;   /* number of registered functions */
  uint32_t        native_fn_cap;     /* capacity of native_fns array */
  /* Persistent struct registry — accumulates across all jacl_eval calls */
  StructTypeRegistry persistent_struct_registry;
  /* Live trampolines — freed on jacl_vm_free */
  JaclTrampoline* trampoline_list;
};

/* --- Native function dispatch callback (called from VM's OP_CALL) --- */

static JaclVal embed__call_native(void* ctx, uint32_t fn_index,
                                   JaclVal* args, int argc) {
  JaclVM* jvm = (JaclVM*)ctx;
  if (fn_index >= jvm->native_fn_count) return jacl_set_error(JACL_NIL);
  return jvm->native_fns[fn_index].fn(jvm, args, argc);
}

/* --- Forward declare StructTypeRegistry for persistent registry --- */
/* (StructTypeRegistry is defined in compiler.c, included before embed.c) */

/* --- Forward declare jacl_vm_new_ex so jacl_vm_new can call it --- */

JACL_EMBED_FN JaclVM* jacl_vm_new_ex(const JaclConfig* config);

/* --- jacl_vm_new — create VM with default settings --- */

JACL_EMBED_FN JaclVM* jacl_vm_new(void) {
  return jacl_vm_new_ex(NULL);
}

/* --- jacl_vm_new_ex — create VM with custom configuration --- */

JACL_EMBED_FN JaclVM* jacl_vm_new_ex(const JaclConfig* config) {
  JaclVM* jvm = (JaclVM*)malloc(sizeof(JaclVM));
  if (!jvm) return NULL;

  /* Zero-initialize arena (uses libc allocator by default) */
  jvm->arena = (arena_t){0};

  /* Initialize the internal VM */
  vm_init(&jvm->vm, &jvm->arena);

  /* Initialize persistent intern table */
  intern_table_init(&jvm->intern_table, &jvm->arena);
  jvm->vm.intern_table = &jvm->intern_table;

  /* Apply configuration */
  uint32_t max_handles = 1024;
  if (config) {
    max_handles = config->max_handles > 0 ? config->max_handles : 1024;

    if (config->max_heap_size > 0) {
      /* Convert bytes to 64KB blocks */
      uint32_t max_blocks = (uint32_t)(config->max_heap_size / GC_BLOCK_SIZE);
      if (max_blocks < 1) max_blocks = 1;
      jvm->vm.block_pool.max_blocks = max_blocks;
    }

    if (config->initial_heap_size > 0) {
      jvm->vm.heap.gc_threshold = config->initial_heap_size;
      if (jvm->vm.heap.gc_threshold < GC_THRESHOLD_MIN)
        jvm->vm.heap.gc_threshold = GC_THRESHOLD_MIN;
      if (jvm->vm.heap.gc_threshold > GC_THRESHOLD_MAX)
        jvm->vm.heap.gc_threshold = GC_THRESHOLD_MAX;
    }
  }
  jvm->max_handles = max_handles;
  jvm->last_error = NULL;

  /* Allocate handle storage */
  jvm->handle_slots = (JaclVal*)malloc(max_handles * sizeof(JaclVal));
  jvm->handle_free_list = (uint32_t*)malloc(max_handles * sizeof(uint32_t));
  jvm->handle_count = max_handles;
  jvm->handle_free_top = max_handles;
  /* Initialize all slots as free (JACL_NIL) and populate free list */
  for (uint32_t i = 0; i < max_handles; i++) {
    jvm->handle_slots[i] = JACL_NIL;
    jvm->handle_free_list[i] = i;
  }
  /* Wire handle slots into VM for GC root scanning */
  jvm->vm.gc_handle_slots = jvm->handle_slots;
  jvm->vm.gc_handle_count = max_handles;

  /* Initialize persistent struct registry (accumulates across evals) */
  jvm->persistent_struct_registry.count = 0;

  /* Initialize trampoline list */
  jvm->trampoline_list = NULL;

  /* Initialize native function registry */
  jvm->native_fns = (NativeFnEntry*)malloc(NATIVE_FN_INIT_CAP * sizeof(NativeFnEntry));
  jvm->native_fn_arities = (int8_t*)malloc(NATIVE_FN_INIT_CAP * sizeof(int8_t));
  jvm->native_fn_count = 0;
  jvm->native_fn_cap = NATIVE_FN_INIT_CAP;
  /* Wire dispatch callback into VM */
  jvm->vm.call_native       = embed__call_native;
  jvm->vm.native_fn_ctx     = jvm;
  jvm->vm.native_fn_arities = jvm->native_fn_arities;
  jvm->vm.native_fn_count   = 0;

  return jvm;
}

/* --- jacl_vm_free — destroy VM and free all memory --- */

JACL_EMBED_FN void jacl_vm_free(JaclVM* vm) {
  if (!vm) return;

  free(vm->handle_slots);
  free(vm->handle_free_list);
  vm->vm.gc_handle_slots = NULL;
  vm->vm.gc_handle_count = 0;
  free(vm->native_fns);
  free(vm->native_fn_arities);
  vm->vm.call_native = NULL;
  vm->vm.native_fn_count = 0;
  embed__free_all_trampolines(vm);
  vm_destroy(&vm->vm);
  arena_destroy(&vm->arena);
  free(vm);
}

/* --- Error value helpers --- */

static JaclVal embed__make_error(JaclVM* jvm, const char* msg) {
  /* Copy message into arena so it survives across calls */
  size_t len = strlen(msg);
  char* copy = (char*)arena_alloc(&jvm->arena, (uint32_t)(len + 1));
  memcpy(copy, msg, len + 1);
  jvm->last_error = copy;
  /* Return nil with error flag set */
  return jacl_set_error(JACL_NIL);
}

/* --- jacl_eval — parse, compile, execute source string --- */

JACL_EMBED_FN JaclVal jacl_eval(JaclVM* jvm, const char* source) {
  if (!jvm || !source) return jacl_set_error(JACL_NIL);

  VM* vm = &jvm->vm;

  /* Lex */
  LexResult tokens = lexer_lex(source, &jvm->arena);

  /* Parse */
  ParseResult parse = parser_parse(tokens, &jvm->arena);
  if (parse.error_count > 0) {
    return embed__make_error(jvm, "parse error");
  }

  /* Compile — use persistent intern table and seeded struct registry */
  CompileResult cr = compiler_compile(parse, &jvm->arena,
                                      &jvm->intern_table, &vm->heap,
                                      &jvm->persistent_struct_registry);
  if (cr.error_count > 0) {
    return embed__make_error(jvm, cr.error_message ? cr.error_message
                                                   : "compile error");
  }

  /* Update persistent struct registry with any new struct defs from this eval */
  if (cr.struct_registry) {
    jvm->persistent_struct_registry = *cr.struct_registry;
  }

  /* Save VM execution state for re-entrant calls */
  uint32_t saved_stack_top   = vm->stack_top;
  uint32_t saved_frame_count = vm->frame_count;
  uint8_t* saved_ip          = vm->ip;
  BytecodeChunk* saved_chunk = vm->chunk;
  BytecodeChunk* saved_top   = vm->top_chunk;
  void*    saved_struct_reg  = vm->struct_registry;

  /* For re-entrant calls: save active stack and frames on C stack */
  JaclVal saved_stack[VM_STACK_MAX];
  CallFrame saved_frames[VM_FRAMES_MAX];
  if (saved_stack_top > 0)
    memcpy(saved_stack, vm->stack, saved_stack_top * sizeof(JaclVal));
  if (saved_frame_count > 0)
    memcpy(saved_frames, vm->frames, saved_frame_count * sizeof(CallFrame));

  vm->struct_registry = cr.struct_registry;

  /* Reset stack for execution but preserve env (globals) */
  vm->stack_top = 0;
  vm->frame_count = 0;

  /* Helper macro to restore VM state before returning */
  #define EVAL_RESTORE() do { \
    if (saved_stack_top > 0) \
      memcpy(vm->stack, saved_stack, saved_stack_top * sizeof(JaclVal)); \
    if (saved_frame_count > 0) \
      memcpy(vm->frames, saved_frames, saved_frame_count * sizeof(CallFrame)); \
    vm->stack_top      = saved_stack_top; \
    vm->frame_count    = saved_frame_count; \
    vm->ip             = saved_ip; \
    vm->chunk          = saved_chunk; \
    vm->top_chunk      = saved_top; \
    vm->struct_registry = saved_struct_reg; \
  } while (0)

  JaclVal eval_result;

  if (cr.suspending) {
    /* CPS-transformed code — run to get main closure, then call with resolve_k */
    VMResult r = vm_exec(vm, &cr.chunk);
    if (r != VM_OK) {
      eval_result = embed__make_error(jvm, vm->error_message ? vm->error_message
                                                              : "runtime error");
      EVAL_RESTORE();
      return eval_result;
    }

    JaclVal main_cl_val = vm->stack[0];
    if (!jacl_is_closure(main_cl_val)) {
      eval_result = embed__make_error(jvm, "internal error: CPS top-level did not produce closure");
      EVAL_RESTORE();
      return eval_result;
    }
    JaclClosure *main_cl = jacl_as_closure(main_cl_val);

    JaclVal completion = jacl_future(&vm->heap);
    JaclVal resolve_k = runtime__create_resolve_closure(&vm->heap, &jvm->arena,
                                                         completion);

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

    vm->frames[1].closure    = main_cl;
    vm->frames[1].return_ip  = NULL;
    vm->frames[1].stack_base = 1;
    vm->frames[1].chunk      = &main_cl->chunk;
    vm->frame_count = 2;
    vm->ip    = main_cl->chunk.code;
    vm->chunk = &main_cl->chunk;
    vm->top_chunk = &main_cl->chunk;

    r = vm__run(vm, 1);
    if (r != VM_OK) {
      eval_result = embed__make_error(jvm, vm->error_message ? vm->error_message
                                                              : "runtime error");
      EVAL_RESTORE();
      return eval_result;
    }

    JaclFuture *cfut = jacl_as_future(completion);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_RESOLVED) {
      eval_result = (JaclVal)cfut->result;
    } else if (state == FUTURE_ERROR) {
      eval_result = embed__make_error(jvm, "runtime error in CPS execution");
    } else {
      eval_result = JACL_NIL;
    }
    EVAL_RESTORE();
    return eval_result;
  }

  /* Non-CPS: straightforward execution */
  VMResult r = vm_exec(vm, &cr.chunk);
  if (r != VM_OK) {
    eval_result = embed__make_error(jvm, vm->error_message ? vm->error_message
                                                            : "runtime error");
    EVAL_RESTORE();
    return eval_result;
  }

  /* Return the top-of-stack value, or nil if stack is empty */
  if (vm->stack_top > 0) {
    eval_result = vm->stack[vm->stack_top - 1];
  } else {
    eval_result = JACL_NIL;
  }
  EVAL_RESTORE();
  return eval_result;

  #undef EVAL_RESTORE
}

/* --- jacl_eval_file — read file and eval contents --- */

JACL_EMBED_FN JaclVal jacl_eval_file(JaclVM* jvm, const char* path) {
  if (!jvm || !path) return jacl_set_error(JACL_NIL);

  FILE* f = fopen(path, "rb");
  if (!f) {
    return embed__make_error(jvm, "could not open file");
  }

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size < 0) {
    fclose(f);
    return embed__make_error(jvm, "could not read file size");
  }

  char* buf = (char*)arena_alloc(&jvm->arena, (uint32_t)(size + 1));
  size_t nread = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[nread] = '\0';

  return jacl_eval(jvm, buf);
}

/* --- jacl_is_error — exported wrapper for the inline in value.c --- */

JACL_EMBED_FN bool jacl_is_error_val(JaclVal v) {
  return jacl_is_error(v);
}

/* --- jacl_error_message — extract error message string --- */

JACL_EMBED_FN const char* jacl_error_message_str(JaclVM* jvm, JaclVal err) {
  if (!jvm) return NULL;
  if (!jacl_is_error(err)) return NULL;
  return jvm->last_error;
}

/* ===== US-004: Value constructors and extractors ===== */

/* --- Value constructors (public API wrappers) --- */

JACL_EMBED_FN JaclVal jacl_nil_val(void) {
  return JACL_NIL;
}

JACL_EMBED_FN JaclVal jacl_bool_val(bool b) {
  return jacl_bool(b);
}

JACL_EMBED_FN JaclVal jacl_i32_val(int32_t n) {
  return jacl_i32(n);
}

JACL_EMBED_FN JaclVal jacl_i64_val(JaclVM* jvm, int64_t n) {
  return jacl_i64(&jvm->vm.heap, n);
}

JACL_EMBED_FN JaclVal jacl_u32_val(uint32_t n) {
  return jacl_u32(n);
}

JACL_EMBED_FN JaclVal jacl_u64_val(JaclVM* jvm, uint64_t n) {
  return jacl_u64(&jvm->vm.heap, n);
}

JACL_EMBED_FN JaclVal jacl_f32_val(float f) {
  return jacl_f32(f);
}

JACL_EMBED_FN JaclVal jacl_f64_val(JaclVM* jvm, double d) {
  return jacl_f64(&jvm->vm.heap, d);
}

JACL_EMBED_FN JaclVal jacl_string_val(JaclVM* jvm, const char* s, size_t len) {
  if (!jvm || !s) return jacl_set_error(JACL_NIL);
  if (len <= 7) {
    return jacl_inline_string(s, len);
  }
  return jacl_intern(&jvm->vm.heap, &jvm->intern_table, s, (uint32_t)len);
}

JACL_EMBED_FN JaclVal jacl_string_cstr_val(JaclVM* jvm, const char* s) {
  if (!jvm || !s) return jacl_set_error(JACL_NIL);
  return jacl_string_val(jvm, s, strlen(s));
}

/* --- Value extractors (public API wrappers) --- */

JACL_EMBED_FN int32_t jacl_as_i32_val(JaclVal val) {
  return jacl_as_i32(val);
}

JACL_EMBED_FN int64_t jacl_as_i64_val(JaclVal val) {
  return jacl_as_i64(val);
}

JACL_EMBED_FN uint32_t jacl_as_u32_val(JaclVal val) {
  return jacl_as_u32(val);
}

JACL_EMBED_FN uint64_t jacl_as_u64_val(JaclVal val) {
  return jacl_as_u64(val);
}

JACL_EMBED_FN float jacl_as_f32_val(JaclVal val) {
  return jacl_as_f32(val);
}

JACL_EMBED_FN double jacl_as_f64_val(JaclVal val) {
  return jacl_as_f64(val);
}

JACL_EMBED_FN bool jacl_as_bool_val(JaclVal val) {
  return jacl_as_bool(val);
}

JACL_EMBED_FN const char* jacl_as_cstr_val(JaclVM* jvm, JaclVal val, size_t* len_out) {
  if (!jvm) return NULL;
  if (!jacl_is_string(val)) return NULL;

  if (jacl_is_inline_string(val)) {
    /* Copy inline string to arena buffer so we can return a stable C string */
    size_t len = jacl_inline_string_len(val);
    char* buf = (char*)arena_alloc(&jvm->arena, (uint32_t)(len + 1));
    jacl_inline_string_get(val, buf, len + 1);
    buf[len] = '\0';
    if (len_out) *len_out = len;
    return buf;
  }

  /* Heap string — data is in GC heap, return pointer directly */
  JaclHeapString* hs = jacl_as_heap_string(val);
  if (len_out) *len_out = hs->length;
  /* Heap strings are not null-terminated; copy to arena with null terminator */
  char* buf = (char*)arena_alloc(&jvm->arena, hs->length + 1);
  memcpy(buf, hs->data, hs->length);
  buf[hs->length] = '\0';
  return buf;
}

/* --- Type query --- */

typedef enum {
  EMBED_TYPE_DYN = 0,
  EMBED_TYPE_BOOL,
  EMBED_TYPE_NIL,
  EMBED_TYPE_I32,
  EMBED_TYPE_I64,
  EMBED_TYPE_U32,
  EMBED_TYPE_U64,
  EMBED_TYPE_F32,
  EMBED_TYPE_F64,
  EMBED_TYPE_STR,
  EMBED_TYPE_VEC,
  EMBED_TYPE_MAP,
  EMBED_TYPE_CLOSURE,
  EMBED_TYPE_STRUCT,
  EMBED_TYPE_NATIVE_FN
} EmbedJaclType;

JACL_EMBED_FN int jacl_typeof_val(JaclVal val) {
  uint64_t tag = val & JACL_TYPE_MASK;
  switch (tag) {
    case JACL_TAG_NIL:           return EMBED_TYPE_NIL;
    case JACL_TAG_BOOL:          return EMBED_TYPE_BOOL;
    case JACL_TAG_I32:           return EMBED_TYPE_I32;
    case JACL_TAG_I64:           return EMBED_TYPE_I64;
    case JACL_TAG_U32:           return EMBED_TYPE_U32;
    case JACL_TAG_U64:           return EMBED_TYPE_U64;
    case JACL_TAG_F32:           return EMBED_TYPE_F32;
    case JACL_TAG_F64:           return EMBED_TYPE_F64;
    case JACL_TAG_INLINE_STRING: return EMBED_TYPE_STR;
    case JACL_TAG_STRING:        return EMBED_TYPE_STR;
    case JACL_TAG_VECTOR:        return EMBED_TYPE_VEC;
    case JACL_TAG_MAP:           return EMBED_TYPE_MAP;
    case JACL_TAG_CLOSURE:       return EMBED_TYPE_CLOSURE;
    case JACL_TAG_STRUCT:        return EMBED_TYPE_STRUCT;
    case JACL_TAG_NATIVE_FN:    return EMBED_TYPE_NATIVE_FN;
    default:                     return EMBED_TYPE_DYN;
  }
}

/* ===== US-006: Native function registration (internal) ===== */

static uint32_t embed__register_native(JaclVM* jvm, const char* name,
                                        EmbedNativeFn fn, int8_t arity) {
  if (!jvm || !fn) return UINT32_MAX;

  /* Grow registry if needed */
  if (jvm->native_fn_count >= jvm->native_fn_cap) {
    uint32_t new_cap = jvm->native_fn_cap * 2;
    NativeFnEntry* new_fns = (NativeFnEntry*)realloc(
        jvm->native_fns, new_cap * sizeof(NativeFnEntry));
    int8_t* new_arities = (int8_t*)realloc(
        jvm->native_fn_arities, new_cap * sizeof(int8_t));
    if (!new_fns || !new_arities) return UINT32_MAX;
    jvm->native_fns = new_fns;
    jvm->native_fn_arities = new_arities;
    jvm->native_fn_cap = new_cap;
    /* Re-wire VM pointer since realloc may have moved the buffer */
    jvm->vm.native_fn_arities = jvm->native_fn_arities;
  }

  uint32_t idx = jvm->native_fn_count++;
  jvm->native_fns[idx].fn    = fn;
  jvm->native_fns[idx].name  = jacl_inline_string(name, strlen(name));
  jvm->native_fns[idx].arity = arity;
  jvm->native_fn_arities[idx] = arity;
  jvm->vm.native_fn_count     = jvm->native_fn_count;

  /* Register the native function value as a global in the VM's environment */
  JaclVal fn_val = jacl_native_fn(idx);
  JaclVal name_val = jacl_inline_string(name, strlen(name));
  Environment* env = &jvm->vm.env;

  /* Check if name already exists */
  for (uint32_t i = 0; i < env->count; i++) {
    if ((env->names[i] & (JACL_TYPE_MASK | JACL_PAYLOAD_MASK)) ==
        (name_val & (JACL_TYPE_MASK | JACL_PAYLOAD_MASK))) {
      env->values[i] = fn_val;
      return idx;
    }
  }

  /* Grow env if needed */
  if (env->count >= env->cap) {
    uint32_t new_cap = env->cap * 2;
    JaclVal* new_names = (JaclVal*)arena_alloc(jvm->vm.arena,
                                                new_cap * sizeof(JaclVal));
    JaclVal* new_values = (JaclVal*)arena_alloc(jvm->vm.arena,
                                                 new_cap * sizeof(JaclVal));
    memcpy(new_names, env->names, env->count * sizeof(JaclVal));
    memcpy(new_values, env->values, env->count * sizeof(JaclVal));
    env->names = new_names;
    env->values = new_values;
    env->cap = new_cap;
  }

  env->names[env->count]  = name_val;
  env->values[env->count] = fn_val;
  env->count++;
  return idx;
}

/* ===== US-007: jacl_register_fn — public API for native function registration ===== */

JACL_EMBED_FN bool jacl_register_fn_val(JaclVM* jvm, const char* name,
                                  EmbedNativeFn fn, int arity) {
  if (!jvm || !name || !fn) return false;
  /* Name must fit inline string (<=7 bytes) */
  size_t name_len = strlen(name);
  if (name_len == 0 || name_len > 7) return false;
  /* Arity must be -1 (variadic) or non-negative */
  if (arity < -1) return false;
  uint32_t idx = embed__register_native(jvm, name, fn, (int8_t)arity);
  return idx != UINT32_MAX;
}

/* ===== US-005: GC handle API — pin values from C ===== */

typedef struct { uint32_t index; } EmbedJaclHandle;

JACL_EMBED_FN EmbedJaclHandle jacl_handle_new_val(JaclVM* jvm, JaclVal val) {
  EmbedJaclHandle h = { .index = UINT32_MAX };
  if (!jvm) return h;

  if (jvm->handle_free_top == 0) {
    /* No free handles available */
    return h;
  }

  /* Pop a free slot from the free list */
  uint32_t idx = jvm->handle_free_list[--jvm->handle_free_top];
  jvm->handle_slots[idx] = val;
  h.index = idx;
  return h;
}

JACL_EMBED_FN JaclVal jacl_handle_get_val(JaclVM* jvm, EmbedJaclHandle h) {
  if (!jvm || h.index >= jvm->handle_count) return JACL_NIL;
  return jvm->handle_slots[h.index];
}

JACL_EMBED_FN void jacl_handle_free_val(JaclVM* jvm, EmbedJaclHandle h) {
  if (!jvm || h.index >= jvm->handle_count) return;
  /* Mark slot as free and push back to free list */
  jvm->handle_slots[h.index] = JACL_NIL;
  jvm->handle_free_list[jvm->handle_free_top++] = h.index;
}

/* ===== US-008: jacl_call / jacl_call_named — call JACL from C ===== */

/**
 * jacl_call_val — call a JACL closure or native function from C.
 *
 * For native functions: dispatches directly without touching the VM stack.
 * For closures: saves VM state, sets up a single call frame, runs to completion,
 *               then restores state. Supports re-entrant calls.
 */
JACL_EMBED_FN JaclVal jacl_call_val(JaclVM* jvm, JaclVal fn, JaclVal* args, int argc) {
  if (!jvm) return jacl_set_error(JACL_NIL);

  VM* vm = &jvm->vm;

  /* --- Native function path --- */
  if (jacl_is_native_fn(fn)) {
    uint32_t idx = jacl_as_native_fn_index(fn);
    if (idx >= jvm->native_fn_count) {
      return embed__make_error(jvm, "invalid native function index");
    }
    /* Validate arity */
    int8_t arity = jvm->native_fn_arities[idx];
    if (arity >= 0 && argc != (int)arity) {
      return embed__make_error(jvm, "argument count mismatch");
    }
    return jvm->native_fns[idx].fn(jvm, args, argc);
  }

  /* --- Closure path --- */
  if (!jacl_is_closure(fn)) {
    return embed__make_error(jvm, "not a callable value");
  }

  JaclClosure* closure = jacl_as_closure(fn);

  /* Validate arity */
  if (!closure->variadic) {
    if (argc < (int)closure->min_args || argc != (int)closure->param_count) {
      return embed__make_error(jvm, "argument count mismatch");
    }
  }

  /* --- Save VM execution state (re-entrancy support) --- */
  uint32_t saved_stack_top   = vm->stack_top;
  uint32_t saved_frame_count = vm->frame_count;
  uint8_t* saved_ip          = vm->ip;
  BytecodeChunk* saved_chunk = vm->chunk;
  BytecodeChunk* saved_top   = vm->top_chunk;
  void*    saved_struct_reg  = vm->struct_registry;

  JaclVal    saved_stack[VM_STACK_MAX];
  CallFrame  saved_frames[VM_FRAMES_MAX];
  if (saved_stack_top > 0)
    memcpy(saved_stack, vm->stack, saved_stack_top * sizeof(JaclVal));
  if (saved_frame_count > 0)
    memcpy(saved_frames, vm->frames, saved_frame_count * sizeof(CallFrame));

  /* --- Set up fresh stack: [fn, args...] --- */
  vm->stack[0] = fn;
  for (int i = 0; i < argc; i++) vm->stack[1 + i] = args[i];
  vm->stack_top = (uint32_t)(1 + argc);

  /* --- Set up a single call frame for the closure --- */
  vm->frames[0].closure    = closure;
  vm->frames[0].return_ip  = NULL;   /* no caller to return to */
  vm->frames[0].stack_base = 1;      /* locals/args start after the closure slot */
  vm->frames[0].chunk      = &closure->chunk;
  vm->frame_count = 1;

  vm->ip        = closure->chunk.code;
  vm->chunk     = &closure->chunk;
  vm->top_chunk = &closure->chunk;

  #define CALL_RESTORE() do { \
    if (saved_stack_top > 0) \
      memcpy(vm->stack, saved_stack, saved_stack_top * sizeof(JaclVal)); \
    if (saved_frame_count > 0) \
      memcpy(vm->frames, saved_frames, saved_frame_count * sizeof(CallFrame)); \
    vm->stack_top       = saved_stack_top; \
    vm->frame_count     = saved_frame_count; \
    vm->ip              = saved_ip; \
    vm->chunk           = saved_chunk; \
    vm->top_chunk       = saved_top; \
    vm->struct_registry = saved_struct_reg; \
  } while (0)

  VMResult r = vm__run(vm, 0);

  JaclVal call_result;
  if (r != VM_OK) {
    call_result = embed__make_error(jvm, vm->error_message ? vm->error_message
                                                            : "runtime error");
    CALL_RESTORE();
    return call_result;
  }

  /* OP_RETURN with frame_count==1 stores result in stack[0], sets stack_top=1 */
  call_result = (vm->stack_top > 0) ? vm->stack[0] : JACL_NIL;
  CALL_RESTORE();
  return call_result;

  #undef CALL_RESTORE
}

/**
 * jacl_call_named_val — look up a global proc by name and call it.
 *
 * Supports names ≤7 bytes (inline) and longer names (heap-interned).
 */
JACL_EMBED_FN JaclVal jacl_call_named_val(JaclVM* jvm, const char* name,
                                    JaclVal* args, int argc) {
  if (!jvm || !name) return jacl_set_error(JACL_NIL);

  size_t name_len = strlen(name);
  if (name_len == 0) return embed__make_error(jvm, "empty function name");

  /* Build the JaclVal key used in the environment */
  JaclVal name_val;
  if (name_len <= 7) {
    name_val = jacl_inline_string(name, name_len);
  } else {
    name_val = jacl_intern(&jvm->vm.heap, &jvm->intern_table,
                           name, (uint32_t)name_len);
  }

  bool found;
  JaclVal fn = vm__env_get(&jvm->vm, name_val, &found);
  if (!found) {
    return embed__make_error(jvm, "undefined function");
  }

  return jacl_call_val(jvm, fn, args, argc);
}

/* ===== US-009: Struct interop from C ===== */

/* Check whether a JaclVal matches a struct field type */
static bool embed__val_matches_field_type(JaclVal val, int field_type) {
  switch ((JaclType)field_type) {
    case TYPE_BOOL:    return jacl_is_bool(val);
    case TYPE_I32:     return jacl_is_i32(val);
    case TYPE_U32:     return jacl_is_u32(val);
    case TYPE_F32:     return jacl_is_f32(val);
    case TYPE_I64:     return jacl_is_i64(val);
    case TYPE_U64:     return jacl_is_u64(val);
    case TYPE_F64:     return jacl_is_f64(val);
    case TYPE_STR:     return jacl_is_string(val);
    case TYPE_VEC:     return jacl_is_vector(val);
    case TYPE_MAP:     return jacl_is_map(val);
    case TYPE_CLOSURE: return jacl_is_closure(val);
    case TYPE_STRUCT:  return jacl_is_struct(val);
    case TYPE_DYN:     return true; /* any value accepted */
    case TYPE_NIL:     return jacl_is_nil(val);
  }
  return false;
}

/* Read a field from struct data and return as JaclVal (always boxed) */
static JaclVal embed__struct_read_field(JaclVM* jvm, JaclStruct* s,
                                         uint32_t offset, int field_type) {
  switch ((JaclType)field_type) {
    case TYPE_BOOL: {
      uint8_t b = s->data[offset];
      return jacl_bool(b);
    }
    case TYPE_I32: {
      int32_t n; memcpy(&n, s->data + offset, 4);
      return jacl_i32(n);
    }
    case TYPE_U32: {
      uint32_t n; memcpy(&n, s->data + offset, 4);
      return jacl_u32(n);
    }
    case TYPE_F32: {
      float f; memcpy(&f, s->data + offset, 4);
      return jacl_f32(f);
    }
    case TYPE_I64: {
      int64_t n; memcpy(&n, s->data + offset, 8);
      return jacl_i64(&jvm->vm.heap, n);
    }
    case TYPE_U64: {
      uint64_t n; memcpy(&n, s->data + offset, 8);
      return jacl_u64(&jvm->vm.heap, n);
    }
    case TYPE_F64: {
      double d; memcpy(&d, s->data + offset, 8);
      return jacl_f64(&jvm->vm.heap, d);
    }
    default: {
      /* str, vec, map, closure, dyn, struct — stored as full JaclVal */
      JaclVal val; memcpy(&val, s->data + offset, sizeof(JaclVal));
      return val;
    }
  }
}

/* Write a JaclVal to a struct field (caller must have already type-checked) */
static void embed__struct_write_field(JaclStruct* s, uint32_t offset,
                                       int field_type, JaclVal val) {
  switch ((JaclType)field_type) {
    case TYPE_BOOL: {
      uint8_t b = jacl_as_bool(val) ? 1 : 0;
      s->data[offset] = b;
      break;
    }
    case TYPE_I32: {
      int32_t n = jacl_as_i32(val);
      memcpy(s->data + offset, &n, 4);
      break;
    }
    case TYPE_U32: {
      uint32_t n = jacl_as_u32(val);
      memcpy(s->data + offset, &n, 4);
      break;
    }
    case TYPE_F32: {
      float f = jacl_as_f32(val);
      memcpy(s->data + offset, &f, 4);
      break;
    }
    case TYPE_I64: {
      int64_t n = jacl_as_i64(val);
      memcpy(s->data + offset, &n, 8);
      break;
    }
    case TYPE_U64: {
      uint64_t n = jacl_as_u64(val);
      memcpy(s->data + offset, &n, 8);
      break;
    }
    case TYPE_F64: {
      double d = jacl_as_f64(val);
      memcpy(s->data + offset, &d, 8);
      break;
    }
    default: {
      memcpy(s->data + offset, &val, sizeof(JaclVal));
      break;
    }
  }
}

/**
 * jacl_struct_new_val — instantiate a JACL struct from C.
 *
 * type_name: struct type name (must be registered via defstruct)
 * fields:    array of count field values (in declaration order)
 * count:     number of fields (must match struct's field_count)
 *
 * Returns the struct value, or error-flagged value on failure.
 */
JACL_EMBED_FN JaclVal jacl_struct_new_val(JaclVM* jvm, const char* type_name,
                                    JaclVal* fields, int count) {
  if (!jvm || !type_name) return jacl_set_error(JACL_NIL);

  StructTypeRegistry* reg = &jvm->persistent_struct_registry;
  uint32_t name_len = (uint32_t)strlen(type_name);
  uint32_t type_idx = struct_registry__find(reg, type_name, name_len);
  if (type_idx == UINT32_MAX) {
    return embed__make_error(jvm, "unknown struct type");
  }

  StructTypeDef* sdef = &reg->defs[type_idx];
  if (count != (int)sdef->field_count) {
    return embed__make_error(jvm, "field count mismatch");
  }

  /* Allocate struct on GC heap */
  JaclStruct* s = (JaclStruct*)gc_alloc(&jvm->vm.heap, OBJ_STRUCT,
                                          sizeof(JaclStruct) + sdef->total_size);
  if (!s) return embed__make_error(jvm, "allocation failed");

  s->type_idx = type_idx;
  s->_pad = 0;
  memset(s->data, 0, sdef->total_size);

  /* Store each field value */
  for (int i = 0; i < count; i++) {
    embed__struct_write_field(s, sdef->fields[i].offset,
                               (int)sdef->fields[i].type, fields[i]);
  }

  return jacl_struct_val(s);
}

/**
 * jacl_struct_get_val — read a struct field by name from C.
 *
 * Returns the field value (boxed), or error-flagged value on failure.
 */
JACL_EMBED_FN JaclVal jacl_struct_get_val(JaclVM* jvm, JaclVal s_val,
                                    const char* field_name) {
  if (!jvm || !field_name) return jacl_set_error(JACL_NIL);
  if (!jacl_is_struct(s_val)) {
    return embed__make_error(jvm, "not a struct value");
  }

  JaclStruct* s = jacl_as_struct_ptr(s_val);
  StructTypeRegistry* reg = &jvm->persistent_struct_registry;
  if (s->type_idx >= reg->count) {
    return embed__make_error(jvm, "invalid struct type index");
  }

  StructTypeDef* sdef = &reg->defs[s->type_idx];
  uint32_t fname_len = (uint32_t)strlen(field_name);

  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    if (sdef->fields[fi].name_len == fname_len &&
        memcmp(sdef->fields[fi].name, field_name, fname_len) == 0) {
      return embed__struct_read_field(jvm, s, sdef->fields[fi].offset,
                                      (int)sdef->fields[fi].type);
    }
  }

  return embed__make_error(jvm, "no such field");
}

/**
 * jacl_struct_set_val — write a struct field by name from C.
 *
 * Returns true on success, false if type mismatch or field not found.
 * Triggers GC write barrier for heap-typed fields.
 */
JACL_EMBED_FN bool jacl_struct_set_val(JaclVM* jvm, JaclVal s_val,
                                 const char* field_name, JaclVal value) {
  if (!jvm || !field_name) return false;
  if (!jacl_is_struct(s_val)) return false;

  JaclStruct* s = jacl_as_struct_ptr(s_val);
  StructTypeRegistry* reg = &jvm->persistent_struct_registry;
  if (s->type_idx >= reg->count) return false;

  StructTypeDef* sdef = &reg->defs[s->type_idx];
  uint32_t fname_len = (uint32_t)strlen(field_name);

  for (uint32_t fi = 0; fi < sdef->field_count; fi++) {
    if (sdef->fields[fi].name_len == fname_len &&
        memcmp(sdef->fields[fi].name, field_name, fname_len) == 0) {
      int field_type = (int)sdef->fields[fi].type;
      /* Type check */
      if (!embed__val_matches_field_type(value, field_type)) {
        return false;
      }
      /* GC write barrier for heap-typed fields (DYN, STR, VEC, MAP, CLOSURE, STRUCT) */
      if (field_type == TYPE_DYN || field_type == TYPE_STR ||
          field_type == TYPE_VEC || field_type == TYPE_MAP ||
          field_type == TYPE_CLOSURE || field_type == TYPE_STRUCT) {
        JaclVal old_val;
        memcpy(&old_val, s->data + sdef->fields[fi].offset, sizeof(JaclVal));
        gc_write_barrier(jvm->vm.grey_buf, jvm->vm.gc_active_ptr,
                         old_val, value);
      }
      embed__struct_write_field(s, sdef->fields[fi].offset, field_type, value);
      return true;
    }
  }

  return false; /* field not found */
}

/* ===== US-010: jacl_has_trampolines — runtime libffi availability query ===== */

JACL_EMBED_FN bool jacl_has_trampolines(void) {
#ifdef JACL_HAS_LIBFFI
  return true;
#else
  return false;
#endif
}

/* ===== US-011: Closure-to-function-pointer trampolines via libffi ===== */

#ifdef JACL_HAS_LIBFFI

/* Parse a single type token from signature string, advance *p */
static int tramp__parse_type(const char** p) {
  const char* s = *p;
  if      (strncmp(s, "void", 4) == 0) { *p += 4; return TRAMP_TYPE_VOID; }
  else if (strncmp(s, "i32",  3) == 0) { *p += 3; return TRAMP_TYPE_I32;  }
  else if (strncmp(s, "i64",  3) == 0) { *p += 3; return TRAMP_TYPE_I64;  }
  else if (strncmp(s, "u32",  3) == 0) { *p += 3; return TRAMP_TYPE_U32;  }
  else if (strncmp(s, "u64",  3) == 0) { *p += 3; return TRAMP_TYPE_U64;  }
  else if (strncmp(s, "f32",  3) == 0) { *p += 3; return TRAMP_TYPE_F32;  }
  else if (strncmp(s, "f64",  3) == 0) { *p += 3; return TRAMP_TYPE_F64;  }
  else if (strncmp(s, "ptr",  3) == 0) { *p += 3; return TRAMP_TYPE_PTR;  }
  return TRAMP_TYPE_UNKNOWN;
}

/* Map type ID to libffi type descriptor */
static ffi_type* tramp__ffi_type(int type_id) {
  switch (type_id) {
    case TRAMP_TYPE_VOID: return &ffi_type_void;
    case TRAMP_TYPE_I32:  return &ffi_type_sint32;
    case TRAMP_TYPE_I64:  return &ffi_type_sint64;
    case TRAMP_TYPE_U32:  return &ffi_type_uint32;
    case TRAMP_TYPE_U64:  return &ffi_type_uint64;
    case TRAMP_TYPE_F32:  return &ffi_type_float;
    case TRAMP_TYPE_F64:  return &ffi_type_double;
    case TRAMP_TYPE_PTR:  return &ffi_type_pointer;
    default:              return &ffi_type_void;
  }
}

/* libffi callback invoked when the trampoline's C function pointer is called */
static void tramp__callback(ffi_cif* cif, void* ret, void** args, void* user_data) {
  (void)cif;
  JaclTrampoline* t = (JaclTrampoline*)user_data;
  JaclVM* jvm = t->jvm;

  /* Marshal C args → JaclVal */
  JaclVal jacl_args[16];
  for (int i = 0; i < t->arg_count && i < 16; i++) {
    switch (t->arg_types_id[i]) {
      case TRAMP_TYPE_I32: jacl_args[i] = jacl_i32(*(int32_t*)args[i]); break;
      case TRAMP_TYPE_I64: jacl_args[i] = jacl_i64(&jvm->vm.heap, *(int64_t*)args[i]); break;
      case TRAMP_TYPE_U32: jacl_args[i] = jacl_u32(*(uint32_t*)args[i]); break;
      case TRAMP_TYPE_U64: jacl_args[i] = jacl_u64(&jvm->vm.heap, *(uint64_t*)args[i]); break;
      case TRAMP_TYPE_F32: jacl_args[i] = jacl_f32(*(float*)args[i]); break;
      case TRAMP_TYPE_F64: jacl_args[i] = jacl_f64(&jvm->vm.heap, *(double*)args[i]); break;
      case TRAMP_TYPE_PTR: jacl_args[i] = jacl_u64(&jvm->vm.heap,
                               (uint64_t)(uintptr_t)*(void**)args[i]); break;
      default:             jacl_args[i] = JACL_NIL; break;
    }
  }

  /* Invoke JACL closure */
  JaclVal result = jacl_call_val(jvm, t->closure, jacl_args, t->arg_count);

  /* Marshal JaclVal → C return value */
  if (!ret) return;
  switch (t->ret_type_id) {
    case TRAMP_TYPE_VOID: break;
    case TRAMP_TYPE_I32: *(int32_t*)ret  = jacl_is_i32(result) ? jacl_as_i32(result) : 0; break;
    case TRAMP_TYPE_I64: *(int64_t*)ret  = jacl_is_i64(result) ? jacl_as_i64(result) : 0; break;
    case TRAMP_TYPE_U32: *(uint32_t*)ret = jacl_is_u32(result) ? jacl_as_u32(result) : 0; break;
    case TRAMP_TYPE_U64: *(uint64_t*)ret = jacl_is_u64(result) ? jacl_as_u64(result) : 0; break;
    case TRAMP_TYPE_F32: *(float*)ret    = jacl_is_f32(result) ? jacl_as_f32(result) : 0.f; break;
    case TRAMP_TYPE_F64: *(double*)ret   = jacl_is_f64(result) ? jacl_as_f64(result) : 0.0; break;
    case TRAMP_TYPE_PTR: *(void**)ret    = jacl_is_u64(result)
                             ? (void*)(uintptr_t)jacl_as_u64(result) : NULL; break;
    default: break;
  }
}

JACL_EMBED_FN JaclTrampoline* jacl_trampoline_new_val(JaclVM* jvm, JaclVal closure,
                                                const char* sig) {
  if (!jvm || !sig || !jacl_is_closure(closure)) return NULL;

  /* Parse signature: rettype(argtype,...) */
  const char* p = sig;
  int ret_type = tramp__parse_type(&p);
  if (ret_type == TRAMP_TYPE_UNKNOWN) return NULL;
  if (*p != '(') return NULL;
  p++; /* skip '(' */

  int arg_types_id[16];
  int arg_count = 0;
  if (*p != ')') {
    while (*p) {
      if (arg_count >= 16) return NULL;
      int at = tramp__parse_type(&p);
      if (at == TRAMP_TYPE_UNKNOWN || at == TRAMP_TYPE_VOID) return NULL;
      arg_types_id[arg_count++] = at;
      if      (*p == ',') { p++; }
      else if (*p == ')') { break; }
      else { return NULL; }
    }
  }
  if (*p != ')') return NULL;

  /* Allocate trampoline struct */
  JaclTrampoline* t = (JaclTrampoline*)malloc(sizeof(JaclTrampoline));
  if (!t) return NULL;
  memset(t, 0, sizeof(JaclTrampoline));

  t->jvm        = jvm;
  t->closure    = closure;
  t->ret_type_id = ret_type;
  t->arg_count   = arg_count;
  memcpy(t->arg_types_id, arg_types_id, (size_t)arg_count * sizeof(int));

  /* Pin closure via a GC handle slot */
  if (jvm->handle_free_top == 0) { free(t); return NULL; }
  uint32_t hidx = jvm->handle_free_list[--jvm->handle_free_top];
  jvm->handle_slots[hidx] = closure;
  t->handle_idx = hidx;

  /* Build ffi_type pointer array */
  size_t atypes_sz = (size_t)(arg_count > 0 ? arg_count : 1);
  t->ffi_arg_types = (ffi_type**)malloc(atypes_sz * sizeof(ffi_type*));
  if (!t->ffi_arg_types) goto fail_handle;
  for (int i = 0; i < arg_count; i++) {
    t->ffi_arg_types[i] = tramp__ffi_type(arg_types_id[i]);
  }

  /* Prepare call interface */
  {
    ffi_status st = ffi_prep_cif(&t->cif, FFI_DEFAULT_ABI,
                                  (unsigned int)arg_count,
                                  tramp__ffi_type(ret_type),
                                  arg_count > 0 ? t->ffi_arg_types : NULL);
    if (st != FFI_OK) goto fail_atypes;
  }

  /* Allocate executable closure memory */
  t->ffi_closure = ffi_closure_alloc(sizeof(ffi_closure), &t->code_ptr);
  if (!t->ffi_closure) goto fail_atypes;

  /* Prepare the closure */
  {
    ffi_status st = ffi_prep_closure_loc((ffi_closure*)t->ffi_closure,
                                          &t->cif, tramp__callback,
                                          t, t->code_ptr);
    if (st != FFI_OK) goto fail_ffi_closure;
  }

  /* Track in VM's linked list */
  t->vm_next = jvm->trampoline_list;
  jvm->trampoline_list = t;
  return t;

fail_ffi_closure:
  ffi_closure_free(t->ffi_closure);
fail_atypes:
  free(t->ffi_arg_types);
fail_handle:
  jvm->handle_slots[hidx] = JACL_NIL;
  jvm->handle_free_list[jvm->handle_free_top++] = hidx;
  free(t);
  return NULL;
}

JACL_EMBED_FN void* jacl_trampoline_ptr_val(JaclTrampoline* t) {
  if (!t) return NULL;
  return t->code_ptr;
}

/* Destroy a single trampoline without removing from VM list (for batch cleanup) */
static void embed__trampoline_destroy(JaclVM* jvm, JaclTrampoline* t) {
  ffi_closure_free(t->ffi_closure);
  free(t->ffi_arg_types);
  /* Release GC handle */
  jvm->handle_slots[t->handle_idx] = JACL_NIL;
  jvm->handle_free_list[jvm->handle_free_top++] = t->handle_idx;
  free(t);
}

JACL_EMBED_FN void jacl_trampoline_free_val(JaclVM* jvm, JaclTrampoline* t) {
  if (!jvm || !t) return;
  /* Remove from VM's linked list */
  JaclTrampoline** prev = &jvm->trampoline_list;
  while (*prev) {
    if (*prev == t) { *prev = t->vm_next; break; }
    prev = &(*prev)->vm_next;
  }
  embed__trampoline_destroy(jvm, t);
}

static void embed__free_all_trampolines(JaclVM* jvm) {
  JaclTrampoline* t = jvm->trampoline_list;
  while (t) {
    JaclTrampoline* next = t->vm_next;
    embed__trampoline_destroy(jvm, t);
    t = next;
  }
  jvm->trampoline_list = NULL;
}

#else /* !JACL_HAS_LIBFFI */

JACL_EMBED_FN JaclTrampoline* jacl_trampoline_new_val(JaclVM* jvm, JaclVal closure,
                                                const char* sig) {
  (void)jvm; (void)closure; (void)sig; return NULL;
}

JACL_EMBED_FN void* jacl_trampoline_ptr_val(JaclTrampoline* t) { (void)t; return NULL; }

JACL_EMBED_FN void jacl_trampoline_free_val(JaclVM* jvm, JaclTrampoline* t) {
  (void)jvm; (void)t;
}

static void embed__free_all_trampolines(JaclVM* jvm) { (void)jvm; }

#endif /* JACL_HAS_LIBFFI */

/**
 * jacl_struct_type_name_val — get the struct type name from C.
 *
 * Returns an arena-allocated null-terminated string, or NULL on failure.
 */
JACL_EMBED_FN const char* jacl_struct_type_name_val(JaclVM* jvm, JaclVal s_val) {
  if (!jvm) return NULL;
  if (!jacl_is_struct(s_val)) return NULL;

  JaclStruct* s = jacl_as_struct_ptr(s_val);
  StructTypeRegistry* reg = &jvm->persistent_struct_registry;
  if (s->type_idx >= reg->count) return NULL;

  StructTypeDef* sdef = &reg->defs[s->type_idx];
  /* Copy name to arena with null terminator for safe C string return */
  char* buf = (char*)arena_alloc(&jvm->arena, sdef->name_len + 1);
  memcpy(buf, sdef->name, sdef->name_len);
  buf[sdef->name_len] = '\0';
  return buf;
}

#endif /* EMBED_C */
