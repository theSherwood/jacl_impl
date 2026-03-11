#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-002: VM lifecycle — jacl_vm_new / jacl_vm_free ===== */

/* Test: jacl_vm_new creates a valid VM */
static int test_vm_new(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* VM should be initialized with default state */
  ASSERT(vm->vm.stack_top == 0);
  ASSERT(vm->vm.ip == NULL);
  ASSERT(vm->vm.chunk == NULL);
  ASSERT(vm->vm.print_fn != NULL);
  ASSERT(vm->vm.arena == &vm->arena);
  ASSERT(vm->vm.intern_table == &vm->intern_table);
  /* env pre-populated with true, false, nil */
  ASSERT_U32_EQ(vm->vm.env.count, 3);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_vm_free on NULL is a safe no-op */
static int test_vm_free_null(void) {
  jacl_vm_free(NULL);  /* should not crash */
  return 1;
}

/* Test: jacl_vm_new_ex with NULL config uses defaults */
static int test_vm_new_ex_null_config(void) {
  JaclVM* vm = jacl_vm_new_ex(NULL);
  ASSERT(vm != NULL);
  ASSERT_U32_EQ(vm->max_handles, 1024);
  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_vm_new_ex with custom config */
static int test_vm_new_ex_custom(void) {
  JaclConfig config = {
    .initial_heap_size = 512 * 1024,  /* 512KB */
    .max_heap_size = 4 * 1024 * 1024, /* 4MB */
    .max_handles = 2048
  };
  JaclVM* vm = jacl_vm_new_ex(&config);
  ASSERT(vm != NULL);
  ASSERT_U32_EQ(vm->max_handles, 2048);
  /* max_heap_size = 4MB / 64KB = 64 blocks */
  ASSERT_U32_EQ(vm->vm.block_pool.max_blocks, 64);
  /* initial_heap_size sets gc_threshold */
  ASSERT_SIZE_EQ(vm->vm.heap.gc_threshold, 512 * 1024);
  jacl_vm_free(vm);
  return 1;
}

/* Test: multiple VMs can coexist */
static int test_multiple_vms(void) {
  JaclVM* vm1 = jacl_vm_new();
  JaclVM* vm2 = jacl_vm_new();
  ASSERT(vm1 != NULL);
  ASSERT(vm2 != NULL);
  ASSERT(vm1 != vm2);

  /* Each VM has its own arena and heap */
  ASSERT(vm1->vm.arena != vm2->vm.arena);
  ASSERT(&vm1->vm.block_pool != &vm2->vm.block_pool);

  jacl_vm_free(vm1);
  jacl_vm_free(vm2);
  return 1;
}

/* Test: jacl_vm_new_ex with zero max_handles uses default 1024 */
static int test_vm_new_ex_zero_handles(void) {
  JaclConfig config = {0};
  JaclVM* vm = jacl_vm_new_ex(&config);
  ASSERT(vm != NULL);
  ASSERT_U32_EQ(vm->max_handles, 1024);
  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_vm_new_ex gc_threshold clamped to min/max */
static int test_vm_new_ex_threshold_clamping(void) {
  /* Very small initial_heap_size should clamp to GC_THRESHOLD_MIN */
  JaclConfig config_small = { .initial_heap_size = 100 };
  JaclVM* vm1 = jacl_vm_new_ex(&config_small);
  ASSERT(vm1 != NULL);
  ASSERT(vm1->vm.heap.gc_threshold >= 256 * 1024);  /* GC_THRESHOLD_MIN */
  jacl_vm_free(vm1);

  /* Very large initial_heap_size should clamp to GC_THRESHOLD_MAX */
  JaclConfig config_large = { .initial_heap_size = 1024 * 1024 * 1024 };
  JaclVM* vm2 = jacl_vm_new_ex(&config_large);
  ASSERT(vm2 != NULL);
  ASSERT(vm2->vm.heap.gc_threshold <= 16 * 1024 * 1024);  /* GC_THRESHOLD_MAX */
  jacl_vm_free(vm2);
  return 1;
}

int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("  %-45s ", #fn); \
    if (fn()) { pass++; } else { fail++; } \
  } while (0)

  printf("=== Embedding API: VM lifecycle ===\n");
  RUN(test_vm_new);
  RUN(test_vm_free_null);
  RUN(test_vm_new_ex_null_config);
  RUN(test_vm_new_ex_custom);
  RUN(test_multiple_vms);
  RUN(test_vm_new_ex_zero_handles);
  RUN(test_vm_new_ex_threshold_clamping);

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
