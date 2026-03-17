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

/* ===== US-005: GC handle API — pin values from C ===== */

/* Test: create handle, get value back */
static int test_handle_basic(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal val = jacl_i32_val(42);
  EmbedJaclHandle h = jacl_handle_new_val(vm, val);
  ASSERT(h.index != UINT32_MAX);

  JaclVal got = jacl_handle_get_val(vm, h);
  ASSERT(jacl_is_i32(got));
  ASSERT_INT_EQ(jacl_as_i32(got), 42);

  jacl_handle_free_val(vm, h);
  jacl_vm_free(vm);
  return 1;
}

/* Test: handle keeps heap value alive across GC */
static int test_handle_survives_gc(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Create a heap-allocated string (>7 bytes → heap) */
  JaclVal val = jacl_string_val(vm, "a longer string for GC test!!", 29);
  ASSERT(jacl_is_string(val));
  ASSERT(!jacl_is_inline_string(val));

  /* Pin it with a handle */
  EmbedJaclHandle h = jacl_handle_new_val(vm, val);
  ASSERT(h.index != UINT32_MAX);

  /* Force GC */
  gc_collect_minor(&vm->vm.heap, &vm->vm, NULL);

  /* Value should still be alive */
  JaclVal got = jacl_handle_get_val(vm, h);
  ASSERT(jacl_is_string(got));

  size_t len = 0;
  const char* s = jacl_as_cstr_val(vm, got, &len);
  ASSERT(s != NULL);
  ASSERT_SIZE_EQ(len, 29);
  ASSERT(memcmp(s, "a longer string for GC test!!", 29) == 0);

  jacl_handle_free_val(vm, h);
  jacl_vm_free(vm);
  return 1;
}

/* Test: free handle, slot is reusable */
static int test_handle_reuse(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal val1 = jacl_i32_val(10);
  EmbedJaclHandle h1 = jacl_handle_new_val(vm, val1);
  uint32_t idx1 = h1.index;

  jacl_handle_free_val(vm, h1);

  /* Next allocation should reuse the freed slot */
  JaclVal val2 = jacl_i32_val(20);
  EmbedJaclHandle h2 = jacl_handle_new_val(vm, val2);
  ASSERT(h2.index == idx1);

  JaclVal got = jacl_handle_get_val(vm, h2);
  ASSERT_INT_EQ(jacl_as_i32(got), 20);

  jacl_handle_free_val(vm, h2);
  jacl_vm_free(vm);
  return 1;
}

/* Test: many handles (100+) can be created */
static int test_handle_many(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  EmbedJaclHandle handles[200];
  for (int i = 0; i < 200; i++) {
    handles[i] = jacl_handle_new_val(vm, jacl_i32_val(i));
    ASSERT(handles[i].index != UINT32_MAX);
  }

  /* Verify all values */
  for (int i = 0; i < 200; i++) {
    JaclVal got = jacl_handle_get_val(vm, handles[i]);
    ASSERT_INT_EQ(jacl_as_i32(got), i);
  }

  /* Free all */
  for (int i = 0; i < 200; i++) {
    jacl_handle_free_val(vm, handles[i]);
  }

  jacl_vm_free(vm);
  return 1;
}

/* Test: handle exhaustion returns invalid handle */
static int test_handle_exhaustion(void) {
  JaclConfig config = { .max_handles = 4 };
  JaclVM* vm = jacl_vm_new_ex(&config);
  ASSERT(vm != NULL);

  EmbedJaclHandle h[4];
  for (int i = 0; i < 4; i++) {
    h[i] = jacl_handle_new_val(vm, jacl_i32_val(i));
    ASSERT(h[i].index != UINT32_MAX);
  }

  /* 5th handle should fail */
  EmbedJaclHandle overflow = jacl_handle_new_val(vm, jacl_i32_val(99));
  ASSERT(overflow.index == UINT32_MAX);

  /* Free one and retry — should succeed now */
  jacl_handle_free_val(vm, h[0]);
  EmbedJaclHandle retry = jacl_handle_new_val(vm, jacl_i32_val(99));
  ASSERT(retry.index != UINT32_MAX);
  ASSERT_INT_EQ(jacl_as_i32(jacl_handle_get_val(vm, retry)), 99);

  jacl_handle_free_val(vm, h[1]);
  jacl_handle_free_val(vm, h[2]);
  jacl_handle_free_val(vm, h[3]);
  jacl_handle_free_val(vm, retry);
  jacl_vm_free(vm);
  return 1;
}

/* Test: handle with NULL vm is safe */
static int test_handle_null_vm(void) {
  EmbedJaclHandle h = jacl_handle_new_val(NULL, jacl_i32_val(1));
  ASSERT(h.index == UINT32_MAX);

  JaclVal got = jacl_handle_get_val(NULL, h);
  ASSERT(jacl_is_nil(got));

  jacl_handle_free_val(NULL, h); /* should not crash */
  return 1;
}

/* Test: default max_handles is 1024, configurable */
static int test_handle_config(void) {
  JaclVM* vm1 = jacl_vm_new();
  ASSERT(vm1 != NULL);
  ASSERT_U32_EQ(vm1->max_handles, 1024);
  ASSERT_U32_EQ(vm1->handle_count, 1024);
  jacl_vm_free(vm1);

  JaclConfig config = { .max_handles = 2048 };
  JaclVM* vm2 = jacl_vm_new_ex(&config);
  ASSERT(vm2 != NULL);
  ASSERT_U32_EQ(vm2->max_handles, 2048);
  ASSERT_U32_EQ(vm2->handle_count, 2048);
  jacl_vm_free(vm2);
  return 1;
}

/* ===== US-006: Native function tag and OP_CALL dispatch ===== */

/* Simple native function: add two i32s */
static JaclVal native_add(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm; (void)argc;
  return jacl_i32(jacl_as_i32(args[0]) + jacl_as_i32(args[1]));
}

/* Native function returning a constant */
static JaclVal native_fortytwo(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm; (void)args; (void)argc;
  return jacl_i32(42);
}

/* Variadic native function: sum all args */
static JaclVal native_sum(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm;
  int32_t total = 0;
  for (int i = 0; i < argc; i++) {
    total += jacl_as_i32(args[i]);
  }
  return jacl_i32(total);
}

/* Native function that returns an error */
static JaclVal native_fail(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm; (void)args; (void)argc;
  return jacl_set_error(JACL_NIL);
}

/* Test: register native fn and call from JACL */
static int test_native_fn_basic(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  uint32_t idx = embed__register_native(vm, "nadd", native_add, 2);
  ASSERT(idx != UINT32_MAX);

  JaclVal result = jacl_eval(vm, "[nadd 10 20]");
  ASSERT(!jacl_is_error(result));
  ASSERT(jacl_is_i32(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 30);

  jacl_vm_free(vm);
  return 1;
}

/* Test: zero-arg native function */
static int test_native_fn_zero_args(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  embed__register_native(vm, "ft", native_fortytwo, 0);

  JaclVal result = jacl_eval(vm, "[ft]");
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test: variadic native function (arity=-1) */
static int test_native_fn_variadic(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  embed__register_native(vm, "nsum", native_sum, -1);

  JaclVal r1 = jacl_eval(vm, "[nsum 1 2 3]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 6);

  JaclVal r2 = jacl_eval(vm, "[nsum 10]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 10);

  jacl_vm_free(vm);
  return 1;
}

/* Test: native function arity mismatch produces runtime error */
static int test_native_fn_arity_mismatch(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  embed__register_native(vm, "nadd", native_add, 2);

  /* Too few args — this should be a compile error or runtime error */
  JaclVal result = jacl_eval(vm, "[nadd 1]");
  ASSERT(jacl_is_error(result));

  jacl_vm_free(vm);
  return 1;
}

/* Test: native function returning error propagates */
static int test_native_fn_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  embed__register_native(vm, "nfail", native_fail, 0);

  JaclVal result = jacl_eval(vm, "[nfail]");
  ASSERT(jacl_is_error(result));

  jacl_vm_free(vm);
  return 1;
}

/* Test: JACL_TAG_NATIVE_FN value has correct type */
static int test_native_fn_tag(void) {
  JaclVal v = jacl_native_fn(0);
  ASSERT(jacl_is_native_fn(v));
  ASSERT(!jacl_is_closure(v));
  ASSERT(!jacl_is_nil(v));
  ASSERT_INT_EQ(jacl_as_native_fn_index(v), 0);

  JaclVal v2 = jacl_native_fn(42);
  ASSERT(jacl_is_native_fn(v2));
  ASSERT_INT_EQ(jacl_as_native_fn_index(v2), 42);
  return 1;
}

/* Test: native fn receives args from VM stack correctly */
static int test_native_fn_args(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  embed__register_native(vm, "nadd", native_add, 2);

  /* Call with computed args */
  JaclVal result = jacl_eval(vm, "[nadd [+ 1 2] [+ 3 4]]");
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 10); /* (1+2) + (3+4) = 10 */

  jacl_vm_free(vm);
  return 1;
}

/* Test: multiple native fns can coexist */
static int test_native_fn_multiple(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  embed__register_native(vm, "nadd", native_add, 2);
  embed__register_native(vm, "ft", native_fortytwo, 0);
  embed__register_native(vm, "nsum", native_sum, -1);

  JaclVal r1 = jacl_eval(vm, "[nadd 1 2]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 3);

  JaclVal r2 = jacl_eval(vm, "[ft]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 42);

  JaclVal r3 = jacl_eval(vm, "[nsum 1 2 3 4 5]");
  ASSERT(!jacl_is_error(r3));
  ASSERT_INT_EQ(jacl_as_i32(r3), 15);

  jacl_vm_free(vm);
  return 1;
}

/* Test: typeof returns EMBED_TYPE_NATIVE_FN for native fn values */
static int test_native_fn_typeof(void) {
  JaclVal v = jacl_native_fn(0);
  ASSERT_INT_EQ(jacl_typeof_val(v), EMBED_TYPE_NATIVE_FN);
  return 1;
}

/* ===== US-007: jacl_register_fn — register C functions ===== */

/* Test: register native fn via public API and call from JACL */
static int test_register_fn_basic(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  bool ok = jacl_register_fn_val(vm, "nadd2", native_add, 2);
  ASSERT(ok);

  JaclVal result = jacl_eval(vm, "[nadd2 3 7]");
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 10);

  jacl_vm_free(vm);
  return 1;
}

/* Test: registered function callable with expression syntax [fn args] */
static int test_register_fn_call_syntax(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_register_fn_val(vm, "ft2", native_fortytwo, 0);

  JaclVal result = jacl_eval(vm, "[ft2]");
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test: variadic native function (arity=-1) via public API */
static int test_register_fn_variadic(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  bool ok = jacl_register_fn_val(vm, "vsum", native_sum, -1);
  ASSERT(ok);

  JaclVal r1 = jacl_eval(vm, "[vsum 1 2 3 4]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 10);

  /* Single arg */
  JaclVal r2 = jacl_eval(vm, "[vsum 99]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 99);

  jacl_vm_free(vm);
  return 1;
}

/* Test: arity mismatch produces compile-time error */
static int test_register_fn_arity_mismatch(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_register_fn_val(vm, "nadd3", native_add, 2);

  /* Too few args */
  JaclVal r1 = jacl_eval(vm, "[nadd3 1]");
  ASSERT(jacl_is_error(r1));

  /* Too many args */
  JaclVal r2 = jacl_eval(vm, "[nadd3 1 2 3]");
  ASSERT(jacl_is_error(r2));

  jacl_vm_free(vm);
  return 1;
}

/* Test: native function returns error-flagged value, propagates to JACL */
static int test_register_fn_error_propagation(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_register_fn_val(vm, "nfail2", native_fail, 0);

  JaclVal result = jacl_eval(vm, "[nfail2]");
  ASSERT(jacl_is_error(result));

  jacl_vm_free(vm);
  return 1;
}

/* Native fn that calls jacl_eval (re-entrant) */
static JaclVal native_reenter(JaclVM* vm, JaclVal* args, int argc) {
  (void)args; (void)argc;
  return jacl_eval(vm, "[+ 100 200]");
}

/* Test: re-entrant — native function calls back into JACL via jacl_eval */
static int test_register_fn_reentrant(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_register_fn_val(vm, "reen", native_reenter, 0);

  JaclVal result = jacl_eval(vm, "[reen]");
  ASSERT(!jacl_is_error(result));
  ASSERT(jacl_is_i32(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 300);

  jacl_vm_free(vm);
  return 1;
}

/* Test: register multiple native functions, call them in sequence */
static int test_register_fn_multiple(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_register_fn_val(vm, "ra", native_add, 2);
  jacl_register_fn_val(vm, "rb", native_fortytwo, 0);
  jacl_register_fn_val(vm, "rc", native_sum, -1);

  JaclVal r1 = jacl_eval(vm, "[ra 5 5]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 10);

  JaclVal r2 = jacl_eval(vm, "[rb]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 42);

  JaclVal r3 = jacl_eval(vm, "[rc 1 2 3 4 5]");
  ASSERT(!jacl_is_error(r3));
  ASSERT_INT_EQ(jacl_as_i32(r3), 15);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_register_fn returns false for NULL/invalid inputs */
static int test_register_fn_null_safety(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  ASSERT(!jacl_register_fn_val(NULL, "test", native_add, 2));
  ASSERT(!jacl_register_fn_val(vm, NULL, native_add, 2));
  ASSERT(!jacl_register_fn_val(vm, "test", NULL, 2));
  /* Name too long (>7 bytes) */
  ASSERT(!jacl_register_fn_val(vm, "toolongname", native_add, 2));
  /* Empty name */
  ASSERT(!jacl_register_fn_val(vm, "", native_add, 2));

  jacl_vm_free(vm);
  return 1;
}

/* Test: re-registering same name updates the function */
static int test_register_fn_reregister(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_register_fn_val(vm, "rfn", native_fortytwo, 0);

  JaclVal r1 = jacl_eval(vm, "[rfn]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 42);

  /* Re-register with a different function — however, since embed__register_native
   * adds a new entry (existing name check updates env value), subsequent eval
   * should use the new fn. */
  jacl_register_fn_val(vm, "rfn", native_add, 2);

  JaclVal r2 = jacl_eval(vm, "[rfn 5 6]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 11);

  jacl_vm_free(vm);
  return 1;
}

/* ===== US-008: jacl_call / jacl_call_named ===== */

/* Test: call a JACL closure from C via jacl_call_val */
static int test_call_closure(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define a proc, then get its closure value */
  JaclVal def_r = jacl_eval(vm, "proc add2 {a, b} { [+ $a $b] }");
  ASSERT(!jacl_is_error(def_r));

  JaclVal fn = jacl_eval(vm, "$add2");
  ASSERT(!jacl_is_error(fn));
  ASSERT(jacl_is_closure(fn));

  JaclVal args[2] = { jacl_i32(3), jacl_i32(7) };
  JaclVal result = jacl_call_val(vm, fn, args, 2);
  ASSERT(!jacl_is_error(result));
  ASSERT(jacl_is_i32(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 10);

  jacl_vm_free(vm);
  return 1;
}

/* Test: call a native function via jacl_call_val */
static int test_call_native_fn(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  uint32_t idx = embed__register_native(vm, "nadd4", native_add, 2);
  ASSERT(idx != UINT32_MAX);

  /* Use the native fn value directly */
  JaclVal fn = jacl_native_fn(idx);
  ASSERT(jacl_is_native_fn(fn));

  JaclVal args[2] = { jacl_i32(10), jacl_i32(20) };
  JaclVal result = jacl_call_val(vm, fn, args, 2);
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 30);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_call_named_val looks up and calls a global proc */
static int test_call_named_proc(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "proc mul2 {a, b} { [* $a $b] }");
  ASSERT(!jacl_is_error(def_r));

  JaclVal args[2] = { jacl_i32(6), jacl_i32(7) };
  JaclVal result = jacl_call_named_val(vm, "mul2", args, 2);
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test: calling a proc that errors returns an error value */
static int test_call_returns_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "proc divz {a} { [/ $a 0] }");
  ASSERT(!jacl_is_error(def_r));

  JaclVal fn = jacl_eval(vm, "$divz");
  ASSERT(!jacl_is_error(fn));

  JaclVal args[1] = { jacl_i32(5) };
  JaclVal result = jacl_call_val(vm, fn, args, 1);
  ASSERT(jacl_is_error(result));

  jacl_vm_free(vm);
  return 1;
}

/* Native fn that calls jacl_call_named back into JACL */
static JaclVal native_call_jacl_fn(JaclVM* vm, JaclVal* args, int argc) {
  (void)argc;
  /* Call the JACL "double" proc with the given arg */
  return jacl_call_named_val(vm, "double", args, 1);
}

/* Test: re-entrant — native fn calls jacl_call back into JACL */
static int test_call_reentrant(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define a JACL proc */
  JaclVal def_r = jacl_eval(vm, "proc double {x} { [* $x 2] }");
  ASSERT(!jacl_is_error(def_r));

  /* Register a native fn that will call the JACL proc */
  embed__register_native(vm, "cdbl", native_call_jacl_fn, 1);

  /* Call from JACL: native fn → jacl_call_named → JACL closure */
  JaclVal result = jacl_eval(vm, "[cdbl 21]");
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_call_named with undefined name returns error */
static int test_call_named_undefined(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_call_named_val(vm, "nope", NULL, 0);
  ASSERT(jacl_is_error(result));

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_call with wrong arity returns error */
static int test_call_wrong_arity(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "proc add2b {a, b} { [+ $a $b] }");
  ASSERT(!jacl_is_error(def_r));
  JaclVal fn = jacl_eval(vm, "$add2b");
  ASSERT(!jacl_is_error(fn));
  ASSERT(jacl_is_closure(fn));

  /* Too few args */
  JaclVal args1[1] = { jacl_i32(1) };
  JaclVal r1 = jacl_call_val(vm, fn, args1, 1);
  ASSERT(jacl_is_error(r1));

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_call with NULL vm returns error */
static int test_call_null_safety(void) {
  JaclVal fn = jacl_i32_val(99); /* not a closure */
  JaclVal result = jacl_call_val(NULL, fn, NULL, 0);
  ASSERT(jacl_is_error(result));
  return 1;
}

/* Test: call proc multiple times, result is correct each time */
static int test_call_multiple(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "proc sq {n} { [* $n $n] }");
  ASSERT(!jacl_is_error(def_r));

  for (int i = 1; i <= 5; i++) {
    JaclVal args[1] = { jacl_i32(i) };
    JaclVal result = jacl_call_named_val(vm, "sq", args, 1);
    ASSERT(!jacl_is_error(result));
    ASSERT_INT_EQ(jacl_as_i32(result), i * i);
  }

  jacl_vm_free(vm);
  return 1;
}

/* ===== US-009: Struct interop from C ===== */

/* Test: create struct from C via jacl_struct_new_val, read fields, verify values */
static int test_struct_new_and_get(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define the struct type */
  JaclVal def_r = jacl_eval(vm, "struct Point {i32 x, i32 y}");
  ASSERT(!jacl_is_error(def_r));

  /* Create a struct from C */
  JaclVal fields[2] = { jacl_i32(10), jacl_i32(20) };
  JaclVal p = jacl_struct_new_val(vm, "Point", fields, 2);
  ASSERT(!jacl_is_error(p));
  ASSERT(jacl_is_struct(p));

  /* Read fields from C */
  JaclVal x = jacl_struct_get_val(vm, p, "x");
  ASSERT(!jacl_is_error(x));
  ASSERT(jacl_is_i32(x));
  ASSERT_INT_EQ(jacl_as_i32(x), 10);

  JaclVal y = jacl_struct_get_val(vm, p, "y");
  ASSERT(!jacl_is_error(y));
  ASSERT(jacl_is_i32(y));
  ASSERT_INT_EQ(jacl_as_i32(y), 20);

  jacl_vm_free(vm);
  return 1;
}

/* Test: mutate struct field from C, read back from JACL, verify change */
static int test_struct_set_and_read_from_jacl(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define struct and create instance in JACL */
  JaclVal def_r = jacl_eval(vm,
    "struct Box {i32 v}\n"
    "def b [Box 5]");
  ASSERT(!jacl_is_error(def_r));

  /* Get the struct value from JACL */
  JaclVal b = jacl_eval(vm, "$b");
  ASSERT(!jacl_is_error(b));
  ASSERT(jacl_is_struct(b));

  /* Mutate the field from C */
  bool ok = jacl_struct_set_val(vm, b, "v", jacl_i32(99));
  ASSERT(ok);

  /* Verify from C */
  JaclVal v = jacl_struct_get_val(vm, b, "v");
  ASSERT(!jacl_is_error(v));
  ASSERT_INT_EQ(jacl_as_i32(v), 99);

  /* Verify from JACL via dynamic field access */
  JaclVal r = jacl_eval(vm, "$b->v");
  ASSERT(!jacl_is_error(r));
  ASSERT_INT_EQ(jacl_as_i32(r), 99);

  jacl_vm_free(vm);
  return 1;
}

/* Test: type mismatch on struct set returns false */
static int test_struct_set_type_mismatch(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "struct Pt2 {i32 x, i32 y}");
  ASSERT(!jacl_is_error(def_r));

  JaclVal fields[2] = { jacl_i32(1), jacl_i32(2) };
  JaclVal p = jacl_struct_new_val(vm, "Pt2", fields, 2);
  ASSERT(!jacl_is_error(p));

  /* Setting an i32 field to a bool should fail */
  bool ok = jacl_struct_set_val(vm, p, "x", jacl_bool(true));
  ASSERT(!ok);

  /* Setting to correct type should succeed */
  ok = jacl_struct_set_val(vm, p, "x", jacl_i32(42));
  ASSERT(ok);
  JaclVal x = jacl_struct_get_val(vm, p, "x");
  ASSERT_INT_EQ(jacl_as_i32(x), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test: jacl_struct_type_name returns the struct type name */
static int test_struct_type_name(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "struct Vec2 {f32 x, f32 y}");
  ASSERT(!jacl_is_error(def_r));

  JaclVal fields[2] = { jacl_f32(1.0f), jacl_f32(2.0f) };
  JaclVal v = jacl_struct_new_val(vm, "Vec2", fields, 2);
  ASSERT(!jacl_is_error(v));

  const char* name = jacl_struct_type_name_val(vm, v);
  ASSERT(name != NULL);
  ASSERT(strcmp(name, "Vec2") == 0);

  jacl_vm_free(vm);
  return 1;
}

/* Test: unknown type name returns error */
static int test_struct_new_unknown_type(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal fields[1] = { jacl_i32(0) };
  JaclVal r = jacl_struct_new_val(vm, "NoSuch", fields, 1);
  ASSERT(jacl_is_error(r));

  jacl_vm_free(vm);
  return 1;
}

/* Test: wrong field count returns error */
static int test_struct_new_wrong_count(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "struct Pair {i32 a, i32 b}");
  ASSERT(!jacl_is_error(def_r));

  /* Pass wrong count (1 instead of 2) */
  JaclVal fields[1] = { jacl_i32(1) };
  JaclVal r = jacl_struct_new_val(vm, "Pair", fields, 1);
  ASSERT(jacl_is_error(r));

  jacl_vm_free(vm);
  return 1;
}

/* Test: struct registry persists across multiple eval calls */
static int test_struct_registry_persists(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define struct in eval 1 */
  JaclVal def_r = jacl_eval(vm, "struct Cnt {i32 n}");
  ASSERT(!jacl_is_error(def_r));

  /* Create from C using persistent registry */
  JaclVal fields[1] = { jacl_i32(7) };
  JaclVal c = jacl_struct_new_val(vm, "Cnt", fields, 1);
  ASSERT(!jacl_is_error(c));

  JaclVal n = jacl_struct_get_val(vm, c, "n");
  ASSERT_INT_EQ(jacl_as_i32(n), 7);

  jacl_vm_free(vm);
  return 1;
}

/* Test: null safety */
static int test_struct_null_safety(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* NULL vm */
  JaclVal r1 = jacl_struct_new_val(NULL, "Point", NULL, 0);
  ASSERT(jacl_is_error(r1));

  /* Not a struct */
  bool ok = jacl_struct_set_val(vm, jacl_i32(1), "x", jacl_i32(0));
  ASSERT(!ok);

  JaclVal r2 = jacl_struct_get_val(vm, jacl_i32(1), "x");
  ASSERT(jacl_is_error(r2));

  const char* name = jacl_struct_type_name_val(vm, jacl_i32(1));
  ASSERT(name == NULL);

  jacl_vm_free(vm);
  return 1;
}

/* ===== US-010: Build system / libffi detection ===== */

/* Test: jacl_has_trampolines returns a bool consistent with compile-time detection */
static int test_has_trampolines(void) {
#ifdef JACL_HAS_LIBFFI
  ASSERT(jacl_has_trampolines() == true);
#else
  ASSERT(jacl_has_trampolines() == false);
#endif
  return 1;
}

/* ===== US-011: Trampoline API ===== */

/* Test: trampoline_new returns NULL when libffi is unavailable */
static int test_trampoline_null_without_libffi(void) {
#ifndef JACL_HAS_LIBFFI
  JaclVM* vm = jacl_vm_new();
  jacl_eval(vm, "proc add {a, b} { [+ $a $b] }");
  JaclVal cl = jacl_eval(vm, "$add");
  JaclTrampoline* t = jacl_trampoline_new_val(vm, cl, "i32(i32,i32)");
  ASSERT(t == NULL);
  jacl_vm_free(vm);
#endif
  return 1;
}

/* Test: trampoline_ptr returns NULL for NULL trampoline */
static int test_trampoline_ptr_null(void) {
  void* p = jacl_trampoline_ptr_val(NULL);
  ASSERT(p == NULL);
  return 1;
}

/* Test: trampoline_free with NULL args is safe */
static int test_trampoline_free_null_safe(void) {
  JaclVM* vm = jacl_vm_new();
  jacl_trampoline_free_val(vm, NULL);   /* no crash */
  jacl_trampoline_free_val(NULL, NULL); /* no crash */
  jacl_vm_free(vm);
  return 1;
}

/* Test: VM with no trampolines — vm_free is clean */
static int test_trampoline_vm_free_no_trampolines(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm->trampoline_list == NULL);
  jacl_vm_free(vm); /* should not crash */
  return 1;
}

#ifdef JACL_HAS_LIBFFI

/* Test: basic i32(i32,i32) trampoline */
static int test_trampoline_i32_add(void) {
  JaclVM* vm = jacl_vm_new();
  jacl_eval(vm, "proc add {a, b} { [+ $a $b] }");
  JaclVal cl = jacl_eval(vm, "$add");
  ASSERT(jacl_is_closure(cl));

  JaclTrampoline* t = jacl_trampoline_new_val(vm, cl, "i32(i32,i32)");
  ASSERT(t != NULL);

  void* fptr = jacl_trampoline_ptr_val(t);
  ASSERT(fptr != NULL);

  /* Cast to function pointer and call */
  typedef int32_t (*AddFn)(int32_t, int32_t);
  AddFn fn;
  memcpy(&fn, &fptr, sizeof(fn));
  int32_t result = fn(3, 4);
  ASSERT_INT_EQ(result, 7);

  jacl_trampoline_free_val(vm, t);
  ASSERT(vm->trampoline_list == NULL);
  jacl_vm_free(vm);
  return 1;
}

/* Test: void() trampoline */
static int test_trampoline_void_no_args(void) {
  JaclVM* vm = jacl_vm_new();
  jacl_eval(vm, "proc noop {} { nil }");
  JaclVal cl = jacl_eval(vm, "$noop");

  JaclTrampoline* t = jacl_trampoline_new_val(vm, cl, "void()");
  ASSERT(t != NULL);

  typedef void (*VoidFn)(void);
  void* fptr = jacl_trampoline_ptr_val(t);
  VoidFn fn;
  memcpy(&fn, &fptr, sizeof(fn));
  fn(); /* should not crash */

  jacl_trampoline_free_val(vm, t);
  jacl_vm_free(vm);
  return 1;
}

/* Test: multiple trampolines — vm_free cleans all */
static int test_trampoline_vm_free_cleans_all(void) {
  JaclVM* vm = jacl_vm_new();
  jacl_eval(vm, "proc id {x} { $x }");
  JaclVal cl = jacl_eval(vm, "$id");

  JaclTrampoline* t1 = jacl_trampoline_new_val(vm, cl, "i32(i32)");
  JaclTrampoline* t2 = jacl_trampoline_new_val(vm, cl, "i32(i32)");
  ASSERT(t1 != NULL);
  ASSERT(t2 != NULL);
  ASSERT(vm->trampoline_list != NULL);
  /* vm_free should clean them all without crashing */
  jacl_vm_free(vm);
  return 1;
}

/* Test: invalid signature returns NULL */
static int test_trampoline_invalid_sig(void) {
  JaclVM* vm = jacl_vm_new();
  jacl_eval(vm, "proc f {x} { $x }");
  JaclVal cl = jacl_eval(vm, "$f");

  ASSERT(jacl_trampoline_new_val(vm, cl, "xyz(i32)")  == NULL); /* bad ret */
  ASSERT(jacl_trampoline_new_val(vm, cl, "i32[i32]")  == NULL); /* bad syntax */
  ASSERT(jacl_trampoline_new_val(vm, cl, "i32(xyz)")  == NULL); /* bad arg */
  ASSERT(jacl_trampoline_new_val(vm, cl, "i32(void)") == NULL); /* void as arg */
  ASSERT(jacl_trampoline_new_val(vm, cl, "i32(")      == NULL); /* truncated */

  jacl_vm_free(vm);
  return 1;
}

/* Test: non-closure input returns NULL */
static int test_trampoline_non_closure(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(jacl_trampoline_new_val(vm, jacl_i32(42), "i32(i32)") == NULL);
  ASSERT(jacl_trampoline_new_val(vm, JACL_NIL,     "i32(i32)") == NULL);
  ASSERT(jacl_trampoline_new_val(NULL, JACL_NIL,   "i32(i32)") == NULL);
  jacl_vm_free(vm);
  return 1;
}

/* Test: explicit trampoline_free removes from VM list */
static int test_trampoline_explicit_free(void) {
  JaclVM* vm = jacl_vm_new();
  jacl_eval(vm, "proc dbl {x} { [* $x 2] }");
  JaclVal cl = jacl_eval(vm, "$dbl");

  JaclTrampoline* t = jacl_trampoline_new_val(vm, cl, "i32(i32)");
  ASSERT(t != NULL);
  ASSERT(vm->trampoline_list == t);

  jacl_trampoline_free_val(vm, t);
  ASSERT(vm->trampoline_list == NULL);

  jacl_vm_free(vm);
  return 1;
}

#endif /* JACL_HAS_LIBFFI */

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

  printf("\n=== Embedding API: GC handles ===\n");
  RUN(test_handle_basic);
  RUN(test_handle_survives_gc);
  RUN(test_handle_reuse);
  RUN(test_handle_many);
  RUN(test_handle_exhaustion);
  RUN(test_handle_null_vm);
  RUN(test_handle_config);

  printf("\n=== Embedding API: Native function dispatch ===\n");
  RUN(test_native_fn_tag);
  RUN(test_native_fn_basic);
  RUN(test_native_fn_zero_args);
  RUN(test_native_fn_variadic);
  RUN(test_native_fn_arity_mismatch);
  RUN(test_native_fn_error);
  RUN(test_native_fn_args);
  RUN(test_native_fn_multiple);
  RUN(test_native_fn_typeof);

  printf("\n=== Embedding API: jacl_register_fn ===\n");
  RUN(test_register_fn_basic);
  RUN(test_register_fn_call_syntax);
  RUN(test_register_fn_variadic);
  RUN(test_register_fn_arity_mismatch);
  RUN(test_register_fn_error_propagation);
  RUN(test_register_fn_reentrant);
  RUN(test_register_fn_multiple);
  RUN(test_register_fn_null_safety);
  RUN(test_register_fn_reregister);

  printf("\n=== Embedding API: jacl_call / jacl_call_named ===\n");
  RUN(test_call_closure);
  RUN(test_call_native_fn);
  RUN(test_call_named_proc);
  RUN(test_call_returns_error);
  RUN(test_call_reentrant);
  RUN(test_call_named_undefined);
  RUN(test_call_wrong_arity);
  RUN(test_call_null_safety);
  RUN(test_call_multiple);

  printf("\n=== Embedding API: Struct interop ===\n");
  RUN(test_struct_new_and_get);
  RUN(test_struct_set_and_read_from_jacl);
  RUN(test_struct_set_type_mismatch);
  RUN(test_struct_type_name);
  RUN(test_struct_new_unknown_type);
  RUN(test_struct_new_wrong_count);
  RUN(test_struct_registry_persists);
  RUN(test_struct_null_safety);

  printf("\n=== Embedding API: Build system / libffi ===\n");
  RUN(test_has_trampolines);

  printf("\n=== Embedding API: Trampolines ===\n");
  RUN(test_trampoline_null_without_libffi);
  RUN(test_trampoline_ptr_null);
  RUN(test_trampoline_free_null_safe);
  RUN(test_trampoline_vm_free_no_trampolines);
#ifdef JACL_HAS_LIBFFI
  RUN(test_trampoline_i32_add);
  RUN(test_trampoline_void_no_args);
  RUN(test_trampoline_vm_free_cleans_all);
  RUN(test_trampoline_invalid_sig);
  RUN(test_trampoline_non_closure);
  RUN(test_trampoline_explicit_free);
#endif

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
