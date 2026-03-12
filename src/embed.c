/* JACL Embedding API — VM lifecycle, eval, error handling, value constructors.
 *
 * Implements jacl_vm_new, jacl_vm_new_ex, jacl_vm_free,
 * jacl_eval, jacl_eval_file, jacl_is_error, jacl_error_message,
 * value constructors/extractors, and jacl_typeof.
 * Included after runtime.c in the unity build.
 */

#ifndef EMBED_C
#define EMBED_C

/* --- Types from jacl.h (cannot include directly due to redefinition conflicts) --- */

typedef struct JaclVM_s JaclVM;

typedef struct {
  size_t   initial_heap_size;
  size_t   max_heap_size;
  uint32_t max_handles;
} JaclConfig;

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
};

/* --- Native function dispatch callback (called from VM's OP_CALL) --- */

static JaclVal embed__call_native(void* ctx, uint32_t fn_index,
                                   JaclVal* args, int argc) {
  JaclVM* jvm = (JaclVM*)ctx;
  if (fn_index >= jvm->native_fn_count) return jacl_set_error(JACL_NIL);
  return jvm->native_fns[fn_index].fn(jvm, args, argc);
}

/* --- Forward declare jacl_vm_new_ex so jacl_vm_new can call it --- */

static JaclVM* jacl_vm_new_ex(const JaclConfig* config);

/* --- jacl_vm_new — create VM with default settings --- */

static JaclVM* jacl_vm_new(void) {
  return jacl_vm_new_ex(NULL);
}

/* --- jacl_vm_new_ex — create VM with custom configuration --- */

static JaclVM* jacl_vm_new_ex(const JaclConfig* config) {
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

static void jacl_vm_free(JaclVM* vm) {
  if (!vm) return;

  free(vm->handle_slots);
  free(vm->handle_free_list);
  vm->vm.gc_handle_slots = NULL;
  vm->vm.gc_handle_count = 0;
  free(vm->native_fns);
  free(vm->native_fn_arities);
  vm->vm.call_native = NULL;
  vm->vm.native_fn_count = 0;
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

static JaclVal jacl_eval(JaclVM* jvm, const char* source) {
  if (!jvm || !source) return jacl_set_error(JACL_NIL);

  VM* vm = &jvm->vm;

  /* Lex */
  LexResult tokens = lexer_lex(source, &jvm->arena);

  /* Parse */
  ParseResult parse = parser_parse(tokens, &jvm->arena);
  if (parse.error_count > 0) {
    return embed__make_error(jvm, "parse error");
  }

  /* Compile — use persistent intern table */
  CompileResult cr = compiler_compile(parse, &jvm->arena,
                                      &jvm->intern_table, &vm->heap);
  if (cr.error_count > 0) {
    return embed__make_error(jvm, cr.error_message ? cr.error_message
                                                   : "compile error");
  }

  vm->struct_registry = cr.struct_registry;

  /* Reset stack for execution but preserve env (globals) */
  vm->stack_top = 0;
  vm->frame_count = 0;

  if (cr.suspending) {
    /* CPS-transformed code — run to get main closure, then call with resolve_k */
    VMResult r = vm_exec(vm, &cr.chunk);
    if (r != VM_OK) {
      return embed__make_error(jvm, vm->error_message ? vm->error_message
                                                      : "runtime error");
    }

    JaclVal main_cl_val = vm->stack[0];
    if (!jacl_is_closure(main_cl_val)) {
      return embed__make_error(jvm, "internal error: CPS top-level did not produce closure");
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
      return embed__make_error(jvm, vm->error_message ? vm->error_message
                                                      : "runtime error");
    }

    JaclFuture *cfut = jacl_as_future(completion);
    uint32_t state = ATOMIC_LOAD_EXPLICIT(&cfut->state, MEM_RELAXED);
    if (state == FUTURE_RESOLVED) {
      return (JaclVal)cfut->result;
    } else if (state == FUTURE_ERROR) {
      return embed__make_error(jvm, "runtime error in CPS execution");
    }
    return JACL_NIL;
  }

  /* Non-CPS: straightforward execution */
  VMResult r = vm_exec(vm, &cr.chunk);
  if (r != VM_OK) {
    return embed__make_error(jvm, vm->error_message ? vm->error_message
                                                    : "runtime error");
  }

  /* Return the top-of-stack value, or nil if stack is empty */
  if (vm->stack_top > 0) {
    return vm->stack[vm->stack_top - 1];
  }
  return JACL_NIL;
}

/* --- jacl_eval_file — read file and eval contents --- */

static JaclVal jacl_eval_file(JaclVM* jvm, const char* path) {
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

/* --- jacl_error_message — extract error message string --- */

static const char* jacl_error_message_str(JaclVM* jvm, JaclVal err) {
  if (!jvm) return NULL;
  if (!jacl_is_error(err)) return NULL;
  return jvm->last_error;
}

/* ===== US-004: Value constructors and extractors ===== */

/* --- Value constructors (public API wrappers) --- */

static JaclVal jacl_nil_val(void) {
  return JACL_NIL;
}

static JaclVal jacl_bool_val(bool b) {
  return jacl_bool(b);
}

static JaclVal jacl_i32_val(int32_t n) {
  return jacl_i32(n);
}

static JaclVal jacl_i64_val(JaclVM* jvm, int64_t n) {
  return jacl_i64(&jvm->vm.heap, n);
}

static JaclVal jacl_u32_val(uint32_t n) {
  return jacl_u32(n);
}

static JaclVal jacl_u64_val(JaclVM* jvm, uint64_t n) {
  return jacl_u64(&jvm->vm.heap, n);
}

static JaclVal jacl_f32_val(float f) {
  return jacl_f32(f);
}

static JaclVal jacl_f64_val(JaclVM* jvm, double d) {
  return jacl_f64(&jvm->vm.heap, d);
}

static JaclVal jacl_string_val(JaclVM* jvm, const char* s, size_t len) {
  if (!jvm || !s) return jacl_set_error(JACL_NIL);
  if (len <= 7) {
    return jacl_inline_string(s, len);
  }
  return jacl_intern(&jvm->vm.heap, &jvm->intern_table, s, (uint32_t)len);
}

static JaclVal jacl_string_cstr_val(JaclVM* jvm, const char* s) {
  if (!jvm || !s) return jacl_set_error(JACL_NIL);
  return jacl_string_val(jvm, s, strlen(s));
}

/* --- Value extractors (public API wrappers) --- */

static int32_t jacl_as_i32_val(JaclVal val) {
  return jacl_as_i32(val);
}

static int64_t jacl_as_i64_val(JaclVal val) {
  return jacl_as_i64(val);
}

static uint32_t jacl_as_u32_val(JaclVal val) {
  return jacl_as_u32(val);
}

static uint64_t jacl_as_u64_val(JaclVal val) {
  return jacl_as_u64(val);
}

static float jacl_as_f32_val(JaclVal val) {
  return jacl_as_f32(val);
}

static double jacl_as_f64_val(JaclVal val) {
  return jacl_as_f64(val);
}

static bool jacl_as_bool_val(JaclVal val) {
  return jacl_as_bool(val);
}

static const char* jacl_as_cstr_val(JaclVM* jvm, JaclVal val, size_t* len_out) {
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

static int jacl_typeof_val(JaclVal val) {
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

/* ===== US-005: GC handle API — pin values from C ===== */

typedef struct { uint32_t index; } EmbedJaclHandle;

static EmbedJaclHandle jacl_handle_new_val(JaclVM* jvm, JaclVal val) {
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

static JaclVal jacl_handle_get_val(JaclVM* jvm, EmbedJaclHandle h) {
  if (!jvm || h.index >= jvm->handle_count) return JACL_NIL;
  return jvm->handle_slots[h.index];
}

static void jacl_handle_free_val(JaclVM* jvm, EmbedJaclHandle h) {
  if (!jvm || h.index >= jvm->handle_count) return;
  /* Mark slot as free and push back to free list */
  jvm->handle_slots[h.index] = JACL_NIL;
  jvm->handle_free_list[jvm->handle_free_top++] = h.index;
}

#endif /* EMBED_C */
