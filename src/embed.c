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

/* --- JaclVM wrapper (opaque to external callers) --- */

struct JaclVM_s {
  VM              vm;           /* internal VM */
  arena_t         arena;        /* owns all arena-allocated memory */
  JaclInternTable intern_table; /* persistent across evals */
  uint32_t        max_handles;  /* configured max handles (for future use) */
  const char*     last_error;   /* last error message (arena-allocated) */
};

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

  return jvm;
}

/* --- jacl_vm_free — destroy VM and free all memory --- */

static void jacl_vm_free(JaclVM* vm) {
  if (!vm) return;

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
  EMBED_TYPE_STRUCT
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
    default:                     return EMBED_TYPE_DYN;
  }
}

#endif /* EMBED_C */
