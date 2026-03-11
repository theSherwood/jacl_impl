/* JACL Embedding API — VM lifecycle, eval, error handling.
 *
 * Implements jacl_vm_new, jacl_vm_new_ex, jacl_vm_free,
 * jacl_eval, jacl_eval_file, jacl_is_error, jacl_error_message.
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

#endif /* EMBED_C */
