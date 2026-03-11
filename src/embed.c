/* JACL Embedding API — VM lifecycle
 *
 * Implements jacl_vm_new, jacl_vm_new_ex, jacl_vm_free.
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

  return jvm;
}

/* --- jacl_vm_free — destroy VM and free all memory --- */

static void jacl_vm_free(JaclVM* vm) {
  if (!vm) return;

  vm_destroy(&vm->vm);
  arena_destroy(&vm->arena);
  free(vm);
}

#endif /* EMBED_C */
