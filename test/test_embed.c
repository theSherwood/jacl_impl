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

/* ===== US-003: Eval API — jacl_eval / jacl_eval_file ===== */

/* Test: eval [+ 1 2] returns i32 3 */
static int test_eval_basic(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_eval(vm, "[+ 1 2]");
  ASSERT(!jacl_is_error(result));
  ASSERT(jacl_is_i32(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 3);

  jacl_vm_free(vm);
  return 1;
}

/* Test: eval with syntax error returns error-flagged value */
static int test_eval_parse_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_eval(vm, "[+ 1");  /* unclosed bracket */
  ASSERT(jacl_is_error(result));

  const char* msg = jacl_error_message_str(vm, result);
  ASSERT(msg != NULL);

  jacl_vm_free(vm);
  return 1;
}

/* Test: eval with runtime error returns error-flagged value with message */
static int test_eval_runtime_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_eval(vm, "[/ 1 0]");  /* division by zero */
  ASSERT(jacl_is_error(result));

  jacl_vm_free(vm);
  return 1;
}

/* Test: multiple sequential evals share global state */
static int test_eval_globals_persist(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define a global variable */
  JaclVal r1 = jacl_eval(vm, "def x 42");
  ASSERT(!jacl_is_error(r1));

  /* Read back the global */
  JaclVal r2 = jacl_eval(vm, "$x");
  ASSERT(!jacl_is_error(r2));
  ASSERT(jacl_is_i32(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test: multiple independent VMs don't interfere */
static int test_eval_independent_vms(void) {
  JaclVM* vm1 = jacl_vm_new();
  JaclVM* vm2 = jacl_vm_new();
  ASSERT(vm1 != NULL);
  ASSERT(vm2 != NULL);

  /* Define x=10 in vm1 */
  JaclVal r1 = jacl_eval(vm1, "def x 10");
  ASSERT(!jacl_is_error(r1));

  /* Define x=20 in vm2 */
  JaclVal r2 = jacl_eval(vm2, "def x 20");
  ASSERT(!jacl_is_error(r2));

  /* Read from each — should be independent */
  JaclVal v1 = jacl_eval(vm1, "$x");
  ASSERT(!jacl_is_error(v1));
  ASSERT_INT_EQ(jacl_as_i32(v1), 10);

  JaclVal v2 = jacl_eval(vm2, "$x");
  ASSERT(!jacl_is_error(v2));
  ASSERT_INT_EQ(jacl_as_i32(v2), 20);

  jacl_vm_free(vm1);
  jacl_vm_free(vm2);
  return 1;
}

/* Test: jacl_is_error returns false for non-error values */
static int test_is_error_non_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_eval(vm, "42");
  ASSERT(!jacl_is_error(result));

  JaclVal zero_result = jacl_eval(vm, "0");
  ASSERT(!jacl_is_error(zero_result));

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_error_message returns NULL for non-error */
static int test_error_message_non_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_eval(vm, "42");
  const char* msg = jacl_error_message_str(vm, result);
  ASSERT(msg == NULL);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_eval_file reads and evaluates a file */
static int test_eval_file(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Write a temp file */
  const char* path = "/tmp/jacl_test_eval.jacl";
  FILE* f = fopen(path, "w");
  ASSERT(f != NULL);
  fprintf(f, "[+ 10 20]");
  fclose(f);

  JaclVal result = jacl_eval_file(vm, path);
  ASSERT(!jacl_is_error(result));
  ASSERT(jacl_is_i32(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 30);

  remove(path);
  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_eval_file with nonexistent file returns error */
static int test_eval_file_not_found(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_eval_file(vm, "/tmp/nonexistent_jacl_file.jacl");
  ASSERT(jacl_is_error(result));

  const char* msg = jacl_error_message_str(vm, result);
  ASSERT(msg != NULL);

  jacl_vm_free(vm);
  return 1;
}

/* Test: compile error returns error-flagged value with message */
static int test_eval_compile_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Call an undefined function */
  JaclVal result = jacl_eval(vm, "[undef 1]");
  ASSERT(jacl_is_error(result));

  const char* msg = jacl_error_message_str(vm, result);
  ASSERT(msg != NULL);

  jacl_vm_free(vm);
  return 1;
}

/* ===== US-004: Value constructors and extractors ===== */

/* Test: jacl_nil_val returns nil */
static int test_nil_val(void) {
  JaclVal v = jacl_nil_val();
  ASSERT(jacl_is_nil(v));
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_NIL);
  return 1;
}

/* Test: jacl_bool_val round-trips */
static int test_bool_val(void) {
  JaclVal t = jacl_bool_val(true);
  JaclVal f = jacl_bool_val(false);
  ASSERT(jacl_is_bool(t));
  ASSERT(jacl_is_bool(f));
  ASSERT(jacl_as_bool_val(t) == true);
  ASSERT(jacl_as_bool_val(f) == false);
  ASSERT_INT_EQ(jacl_typeof_val(t), EMBED_TYPE_BOOL);
  return 1;
}

/* Test: jacl_i32_val round-trips */
static int test_i32_val(void) {
  JaclVal v = jacl_i32_val(42);
  ASSERT(jacl_is_i32(v));
  ASSERT_INT_EQ(jacl_as_i32_val(v), 42);
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_I32);

  /* Negative value */
  JaclVal neg = jacl_i32_val(-100);
  ASSERT_INT_EQ(jacl_as_i32_val(neg), -100);
  return 1;
}

/* Test: jacl_i64_val round-trips via heap allocation */
static int test_i64_val(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  int64_t big = INT64_C(1234567890123);
  JaclVal v = jacl_i64_val(vm, big);
  ASSERT(jacl_is_i64(v));
  ASSERT(jacl_as_i64_val(v) == big);
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_I64);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_u32_val round-trips */
static int test_u32_val(void) {
  JaclVal v = jacl_u32_val(3000000000u);
  ASSERT(jacl_is_u32(v));
  ASSERT(jacl_as_u32_val(v) == 3000000000u);
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_U32);
  return 1;
}

/* Test: jacl_u64_val round-trips via heap allocation */
static int test_u64_val(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  uint64_t big = UINT64_C(9876543210123);
  JaclVal v = jacl_u64_val(vm, big);
  ASSERT(jacl_is_u64(v));
  ASSERT(jacl_as_u64_val(v) == big);
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_U64);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_f32_val round-trips */
static int test_f32_val(void) {
  JaclVal v = jacl_f32_val(3.14f);
  ASSERT(jacl_is_f32(v));
  float diff = jacl_as_f32_val(v) - 3.14f;
  ASSERT(diff < 0.001f && diff > -0.001f);
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_F32);
  return 1;
}

/* Test: jacl_f64_val round-trips via heap allocation */
static int test_f64_val(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal v = jacl_f64_val(vm, 3.141592653589793);
  ASSERT(jacl_is_f64(v));
  double diff = jacl_as_f64_val(v) - 3.141592653589793;
  ASSERT(diff < 0.0000001 && diff > -0.0000001);
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_F64);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_string_val short string (inline) */
static int test_string_val_short(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal v = jacl_string_val(vm, "hello", 5);
  ASSERT(jacl_is_string(v));
  ASSERT(jacl_is_inline_string(v));
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_STR);

  size_t len = 0;
  const char* s = jacl_as_cstr_val(vm, v, &len);
  ASSERT(s != NULL);
  ASSERT_SIZE_EQ(len, 5);
  ASSERT(memcmp(s, "hello", 5) == 0);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_string_val long string (heap) */
static int test_string_val_long(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  const char* long_str = "a longer string that exceeds 7 bytes";
  size_t slen = strlen(long_str);
  JaclVal v = jacl_string_val(vm, long_str, slen);
  ASSERT(jacl_is_string(v));
  ASSERT(!jacl_is_inline_string(v));
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_STR);

  size_t len = 0;
  const char* s = jacl_as_cstr_val(vm, v, &len);
  ASSERT(s != NULL);
  ASSERT_SIZE_EQ(len, slen);
  ASSERT(memcmp(s, long_str, slen) == 0);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_string_cstr_val convenience */
static int test_string_cstr_val(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal v = jacl_string_cstr_val(vm, "test");
  ASSERT(jacl_is_string(v));

  size_t len = 0;
  const char* s = jacl_as_cstr_val(vm, v, &len);
  ASSERT(s != NULL);
  ASSERT_SIZE_EQ(len, 4);
  ASSERT(memcmp(s, "test", 4) == 0);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_as_cstr_val returns NULL for non-string */
static int test_as_cstr_non_string(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal v = jacl_i32_val(42);
  const char* s = jacl_as_cstr_val(vm, v, NULL);
  ASSERT(s == NULL);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_as_cstr_val with NULL len_out */
static int test_as_cstr_null_len(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal v = jacl_string_cstr_val(vm, "hi");
  const char* s = jacl_as_cstr_val(vm, v, NULL);
  ASSERT(s != NULL);
  ASSERT(s[0] == 'h');
  ASSERT(s[1] == 'i');

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_typeof_val for all types */
static int test_typeof_val(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  ASSERT_INT_EQ(jacl_typeof_val(jacl_nil_val()), EMBED_TYPE_NIL);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_bool_val(true)), EMBED_TYPE_BOOL);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_i32_val(1)), EMBED_TYPE_I32);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_u32_val(1)), EMBED_TYPE_U32);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_f32_val(1.0f)), EMBED_TYPE_F32);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_i64_val(vm, 1)), EMBED_TYPE_I64);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_u64_val(vm, 1)), EMBED_TYPE_U64);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_f64_val(vm, 1.0)), EMBED_TYPE_F64);
  ASSERT_INT_EQ(jacl_typeof_val(jacl_string_cstr_val(vm, "x")), EMBED_TYPE_STR);

  jacl_vm_free(vm);
  return 1;
}

/* Test: inline types (nil, bool, i32, f32, u32) require no VM pointer */
static int test_inline_no_vm(void) {
  /* These should all work without any VM */
  JaclVal nil = jacl_nil_val();
  JaclVal b = jacl_bool_val(true);
  JaclVal i = jacl_i32_val(99);
  JaclVal f = jacl_f32_val(1.5f);
  JaclVal u = jacl_u32_val(100);

  ASSERT(jacl_is_nil(nil));
  ASSERT(jacl_as_bool_val(b) == true);
  ASSERT_INT_EQ(jacl_as_i32_val(i), 99);
  ASSERT(jacl_as_u32_val(u) == 100);
  float fdiff = jacl_as_f32_val(f) - 1.5f;
  ASSERT(fdiff < 0.001f && fdiff > -0.001f);
  return 1;
}

/* Test: create values from C, round-trip through eval */
static int test_value_roundtrip_eval(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Eval an i32 expression and extract */
  JaclVal r = jacl_eval(vm, "[+ 10 20]");
  ASSERT(!jacl_is_error(r));
  ASSERT_INT_EQ(jacl_as_i32_val(r), 30);
  ASSERT_INT_EQ(jacl_typeof_val(r), EMBED_TYPE_I32);

  /* Eval a boolean expression */
  JaclVal rb = jacl_eval(vm, "[== 1 1]");
  ASSERT(!jacl_is_error(rb));
  ASSERT(jacl_as_bool_val(rb) == true);
  ASSERT_INT_EQ(jacl_typeof_val(rb), EMBED_TYPE_BOOL);

  jacl_vm_free(vm);
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

  printf("\n=== Embedding API: Eval ===\n");
  RUN(test_eval_basic);
  RUN(test_eval_parse_error);
  RUN(test_eval_runtime_error);
  RUN(test_eval_globals_persist);
  RUN(test_eval_independent_vms);
  RUN(test_is_error_non_error);
  RUN(test_error_message_non_error);
  RUN(test_eval_file);
  RUN(test_eval_file_not_found);
  RUN(test_eval_compile_error);

  printf("\n=== Embedding API: Value constructors/extractors ===\n");
  RUN(test_nil_val);
  RUN(test_bool_val);
  RUN(test_i32_val);
  RUN(test_i64_val);
  RUN(test_u32_val);
  RUN(test_u64_val);
  RUN(test_f32_val);
  RUN(test_f64_val);
  RUN(test_string_val_short);
  RUN(test_string_val_long);
  RUN(test_string_cstr_val);
  RUN(test_as_cstr_non_string);
  RUN(test_as_cstr_null_len);
  RUN(test_typeof_val);
  RUN(test_inline_no_vm);
  RUN(test_value_roundtrip_eval);

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
