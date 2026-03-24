#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-015: E2E tests — GC handles ===== */

/*
 * force_gc_now — directly invoke a full GC cycle on the VM.
 *
 * Since we include ../src/jacl.c (unity build), gc_collect is available.
 * gc_handle_slots are scanned as additional roots (US-005), so values
 * pinned by handles survive.
 */
static void force_gc_now(JaclVM* jvm) {
  gc_collect(&jvm->vm.heap, &jvm->vm);
}

/* Test 1: create handle, force GC, verify value survives */
static int test_e2e_gc_handle_survives(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Create a heap-allocated string (>7 bytes) */
  JaclVal str = jacl_string_cstr_val(vm, "this string must survive gc!");
  ASSERT(!jacl_is_nil(str));

  /* Pin it with a GC handle */
  EmbedJaclHandle h = jacl_handle_new_val(vm, str);
  ASSERT(h.index != UINT32_MAX);

  /* Force a full GC cycle */
  force_gc_now(vm);

  /* Value must have survived — same JaclVal bits */
  JaclVal recovered = jacl_handle_get_val(vm, h);
  ASSERT(recovered == str);

  /* String content must still be intact */
  size_t len = 0;
  const char* cstr = jacl_as_cstr_val(vm, recovered, &len);
  ASSERT(cstr != NULL);
  ASSERT_SIZE_EQ(len, 28);
  ASSERT(memcmp(cstr, "this string must survive gc!", 28) == 0);

  jacl_handle_free_val(vm, h);
  jacl_vm_free(vm);
  return 1;
}

/* Test 2: free handle, force GC, verify slot is released and reusable */
static int test_e2e_gc_handle_free_releases(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Create and pin a heap string */
  JaclVal str = jacl_string_cstr_val(vm, "temporary value here!");
  EmbedJaclHandle h = jacl_handle_new_val(vm, str);
  ASSERT(h.index != UINT32_MAX);
  uint32_t saved_idx = h.index;

  /* Free the handle — slot becomes NIL, pushed back to free list */
  jacl_handle_free_val(vm, h);

  /* Force GC — no crash verifies that freed-handle path is safe */
  force_gc_now(vm);

  /* Freed slot now returns JACL_NIL */
  EmbedJaclHandle freed_h = { .index = saved_idx };
  JaclVal slot_val = jacl_handle_get_val(vm, freed_h);
  ASSERT(jacl_is_nil(slot_val));

  /* The freed slot is the next one reused (LIFO free list) */
  JaclVal new_val = jacl_i32_val(999);
  EmbedJaclHandle h2 = jacl_handle_new_val(vm, new_val);
  ASSERT(h2.index == saved_idx);
  ASSERT_INT_EQ(jacl_as_i32(jacl_handle_get_val(vm, h2)), 999);

  /* Force GC again — pinned i32 handle safe */
  force_gc_now(vm);
  ASSERT_INT_EQ(jacl_as_i32(jacl_handle_get_val(vm, h2)), 999);

  jacl_handle_free_val(vm, h2);
  jacl_vm_free(vm);
  return 1;
}

/* Test 3: create many handles (150+), GC stress, verify all survive */
static int test_e2e_gc_many_handles(void) {
  #define N_GC_HANDLES 150

  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  EmbedJaclHandle handles[N_GC_HANDLES];
  JaclVal values[N_GC_HANDLES];

  /* Allocate 150 heap strings and pin each with a handle */
  for (int i = 0; i < N_GC_HANDLES; i++) {
    char buf[28];
    snprintf(buf, sizeof(buf), "gcstress-string-%d", i);
    values[i] = jacl_string_cstr_val(vm, buf);
    handles[i] = jacl_handle_new_val(vm, values[i]);
    ASSERT(handles[i].index != UINT32_MAX);
  }

  /* Force GC under handle stress */
  force_gc_now(vm);

  /* All 150 strings must survive with correct content */
  for (int i = 0; i < N_GC_HANDLES; i++) {
    JaclVal recovered = jacl_handle_get_val(vm, handles[i]);
    ASSERT(recovered == values[i]);

    char expected[28];
    int elen = snprintf(expected, sizeof(expected), "gcstress-string-%d", i);
    size_t len = 0;
    const char* cstr = jacl_as_cstr_val(vm, recovered, &len);
    ASSERT(cstr != NULL);
    ASSERT_SIZE_EQ(len, (size_t)elen);
    ASSERT(memcmp(cstr, expected, (size_t)elen) == 0);

    jacl_handle_free_val(vm, handles[i]);
  }

  jacl_vm_free(vm);
  return 1;

  #undef N_GC_HANDLES
}

/* Test 4: handle to a struct — struct and its heap fields survive GC */
static int test_e2e_gc_handle_struct(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define a struct with a string field and an i32 field */
  JaclVal def_r = jacl_eval(vm, "struct Item {str name, i32 val}");
  ASSERT(!jacl_is_error(def_r));

  /* Create struct from C with a heap string name (>7 bytes) */
  JaclVal name_str = jacl_string_cstr_val(vm, "test item name here!");
  JaclVal fields[2] = { name_str, jacl_i32_val(42) };
  JaclVal s = jacl_struct_new_val(vm, "Item", fields, 2);
  ASSERT(!jacl_is_error(s));
  ASSERT(jacl_is_struct(s));

  /* Pin the struct — handle is the root that keeps struct + fields alive */
  EmbedJaclHandle h = jacl_handle_new_val(vm, s);
  ASSERT(h.index != UINT32_MAX);

  /* Force GC — struct and its string field must be traced and kept alive */
  force_gc_now(vm);

  /* Struct must have survived */
  JaclVal s2 = jacl_handle_get_val(vm, h);
  ASSERT(s2 == s);
  ASSERT(jacl_is_struct(s2));

  /* String field must have survived with correct content */
  JaclVal name_out = jacl_struct_get_val(vm, s2, "name");
  ASSERT(!jacl_is_error(name_out));
  ASSERT(jacl_is_string(name_out));
  size_t len = 0;
  const char* name_cstr = jacl_as_cstr_val(vm, name_out, &len);
  ASSERT(name_cstr != NULL);
  ASSERT_SIZE_EQ(len, 20);
  ASSERT(memcmp(name_cstr, "test item name here!", 20) == 0);

  /* i32 field must have survived */
  JaclVal val_out = jacl_struct_get_val(vm, s2, "val");
  ASSERT(!jacl_is_error(val_out));
  ASSERT_INT_EQ(jacl_as_i32(val_out), 42);

  jacl_handle_free_val(vm, h);
  jacl_vm_free(vm);
  return 1;
}

/* Test 5: handle to a closure — closure and upvalues survive GC */
static int test_e2e_gc_handle_closure(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /*
   * Define a higher-order proc that returns a named inner closure.
   * The inner proc captures n (upvalue cell on GC heap) from the outer scope.
   * Pattern from procs.jacl: proc outer {n} { proc inner {x} { + $x $n } }
   */
  JaclVal r1 = jacl_eval(vm, "proc mkadd {n} { proc inner {x} { + $x $n } }");
  ASSERT(!jacl_is_error(r1));

  /* Call mkadd 10 — returns inner closure (only live in our C variable) */
  JaclVal arg = jacl_i32_val(10);
  JaclVal clo = jacl_call_named_val(vm, "mkadd", &arg, 1);
  ASSERT(!jacl_is_error(clo));
  ASSERT(jacl_is_closure(clo));

  /*
   * Pin the closure — the returned inner closure is NOT in any JACL global.
   * The handle is the only root keeping it (and its upvalue cells) alive.
   */
  EmbedJaclHandle h = jacl_handle_new_val(vm, clo);
  ASSERT(h.index != UINT32_MAX);

  /* Force GC — closure + upvalue cells must survive via the handle root */
  force_gc_now(vm);

  /* Retrieve closure from handle */
  JaclVal recovered_clo = jacl_handle_get_val(vm, h);
  ASSERT(recovered_clo == clo);
  ASSERT(jacl_is_closure(recovered_clo));

  /* Call the recovered closure: [+ 5 10] should return 15 */
  JaclVal call_arg = jacl_i32_val(5);
  JaclVal result = jacl_call_val(vm, recovered_clo, &call_arg, 1);
  ASSERT(!jacl_is_error(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 15);

  /* Force GC again while closure is still pinned */
  force_gc_now(vm);

  /* Call again — upvalue must still be intact */
  JaclVal result2 = jacl_call_val(vm, recovered_clo, &call_arg, 1);
  ASSERT(!jacl_is_error(result2));
  ASSERT_INT_EQ(jacl_as_i32(result2), 15);

  jacl_handle_free_val(vm, h);
  jacl_vm_free(vm);
  return 1;
}

int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("  %-54s ", #fn); \
    if (fn()) { pass++; printf("PASS\n"); } else { fail++; printf("FAIL\n"); } \
  } while (0)

  printf("=== E2E: GC Handles (US-015) ===\n");
  RUN(test_e2e_gc_handle_survives);
  RUN(test_e2e_gc_handle_free_releases);
  RUN(test_e2e_gc_many_handles);
  RUN(test_e2e_gc_handle_struct);
  RUN(test_e2e_gc_handle_closure);

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
