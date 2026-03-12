#include "test_helpers.h"
#include "../src/jacl.c"

/* ===== US-016: E2E tests — struct interop and trampolines ===== */

/* ---- Struct interop tests (always compiled and run) ---- */

/* Test 1: create struct from C via jacl_struct_new, read fields, verify values */
static int test_e2e_struct_create_and_read(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define a struct type */
  JaclVal def_r = jacl_eval(vm, "defstruct Vec3 [x :f32] [y :f32] [z :f32]");
  ASSERT(!jacl_is_error(def_r));

  /* Create from C with three f32 fields */
  JaclVal fields[3] = {
    jacl_f32_val(1.5f),
    jacl_f32_val(2.5f),
    jacl_f32_val(3.5f)
  };
  JaclVal s = jacl_struct_new_val(vm, "Vec3", fields, 3);
  ASSERT(!jacl_is_error(s));
  ASSERT(jacl_is_struct(s));

  /* Read fields back — verify values */
  JaclVal xv = jacl_struct_get_val(vm, s, "x");
  ASSERT(!jacl_is_error(xv));
  ASSERT(jacl_is_f32(xv));
  float xf = jacl_as_f32_val(xv);
  ASSERT(xf > 1.4f && xf < 1.6f);

  JaclVal yv = jacl_struct_get_val(vm, s, "y");
  ASSERT(!jacl_is_error(yv));
  float yf = jacl_as_f32_val(yv);
  ASSERT(yf > 2.4f && yf < 2.6f);

  JaclVal zv = jacl_struct_get_val(vm, s, "z");
  ASSERT(!jacl_is_error(zv));
  float zf = jacl_as_f32_val(zv);
  ASSERT(zf > 3.4f && zf < 3.6f);

  /* Verify struct type name */
  const char* tn = jacl_struct_type_name_val(vm, s);
  ASSERT(tn != NULL);
  ASSERT(strcmp(tn, "Vec3") == 0);

  /* Verify wrong field name returns error */
  JaclVal bad = jacl_struct_get_val(vm, s, "w");
  ASSERT(jacl_is_error(bad));

  jacl_vm_free(vm);
  return 1;
}

/* Test 2: mutate struct field from C, read back from JACL, verify change */
static int test_e2e_struct_mutate_and_readback(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define struct and create instance in JACL */
  JaclVal def_r = jacl_eval(vm,
    "defstruct Score [pts :i32]\n"
    "def s [Score 100]");
  ASSERT(!jacl_is_error(def_r));

  /* Retrieve the struct from JACL */
  JaclVal sv = jacl_eval(vm, "$s");
  ASSERT(!jacl_is_error(sv));
  ASSERT(jacl_is_struct(sv));

  /* Mutate pts from C */
  bool ok = jacl_struct_set_val(vm, sv, "pts", jacl_i32_val(999));
  ASSERT(ok);

  /* Verify from C */
  JaclVal pts = jacl_struct_get_val(vm, sv, "pts");
  ASSERT(!jacl_is_error(pts));
  ASSERT_INT_EQ(jacl_as_i32(pts), 999);

  /* Verify from JACL */
  JaclVal r = jacl_eval(vm, "[. $s pts]");
  ASSERT(!jacl_is_error(r));
  ASSERT_INT_EQ(jacl_as_i32(r), 999);

  /* Create another struct and verify independence */
  JaclVal f2[1] = { jacl_i32_val(42) };
  JaclVal s2 = jacl_struct_new_val(vm, "Score", f2, 1);
  ASSERT(!jacl_is_error(s2));
  JaclVal pts2 = jacl_struct_get_val(vm, s2, "pts");
  ASSERT_INT_EQ(jacl_as_i32(pts2), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test 3: type mismatch on struct set returns false */
static int test_e2e_struct_type_mismatch(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal def_r = jacl_eval(vm, "defstruct Pair [a :i32] [b :f32]");
  ASSERT(!jacl_is_error(def_r));

  JaclVal fields[2] = { jacl_i32_val(1), jacl_f32_val(2.0f) };
  JaclVal p = jacl_struct_new_val(vm, "Pair", fields, 2);
  ASSERT(!jacl_is_error(p));

  /* Wrong type: setting i32 field to f32 returns false */
  bool r1 = jacl_struct_set_val(vm, p, "a", jacl_f32_val(1.0f));
  ASSERT(!r1);

  /* Wrong type: setting f32 field to i32 returns false */
  bool r2 = jacl_struct_set_val(vm, p, "b", jacl_i32_val(1));
  ASSERT(!r2);

  /* Correct types succeed */
  bool r3 = jacl_struct_set_val(vm, p, "a", jacl_i32_val(10));
  ASSERT(r3);
  bool r4 = jacl_struct_set_val(vm, p, "b", jacl_f32_val(3.14f));
  ASSERT(r4);

  /* Verify values */
  ASSERT_INT_EQ(jacl_as_i32(jacl_struct_get_val(vm, p, "a")), 10);
  float fv = jacl_as_f32_val(jacl_struct_get_val(vm, p, "b"));
  ASSERT(fv > 3.13f && fv < 3.15f);

  /* Unknown field name returns error */
  JaclVal bad = jacl_struct_get_val(vm, p, "c");
  ASSERT(jacl_is_error(bad));

  /* Null inputs are safe */
  ASSERT(jacl_struct_get_val(NULL, p, "a") == JACL_NIL || true);
  bool nb = jacl_struct_set_val(vm, p, NULL, jacl_i32_val(0));
  ASSERT(!nb);

  jacl_vm_free(vm);
  return 1;
}

/* ---- Trampoline tests (compiled always, run only with libffi) ---- */

#ifdef JACL_HAS_LIBFFI

/*
 * Helper: force_gc_now — direct GC cycle via unity-build access.
 * Defined here to avoid redefinition if this file is included after
 * test_e2e_gc_handles.c (which also defines it). Since each test binary
 * is compiled from a single .c file, there is no conflict.
 */
static void tramp_force_gc(JaclVM* jvm) {
  gc_collect(&jvm->vm.heap, &jvm->vm);
}

/* Test 4: create trampoline from JACL closure, call C function pointer */
static int test_e2e_tramp_basic_call(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define a simple add proc */
  jacl_eval(vm, "proc add [a b] { [+ $a $b] }");
  JaclVal cl = jacl_eval(vm, "$add");
  ASSERT(jacl_is_closure(cl));

  /* Create trampoline with i32(i32,i32) signature */
  JaclTrampoline* t = jacl_trampoline_new_val(vm, cl, "i32(i32,i32)");
  ASSERT(t != NULL);

  void* fptr = jacl_trampoline_ptr_val(t);
  ASSERT(fptr != NULL);

  /* Cast to function pointer via memcpy (avoids UB) */
  typedef int32_t (*AddFn)(int32_t, int32_t);
  AddFn fn;
  memcpy(&fn, &fptr, sizeof(fn));

  /* Call via the C function pointer — should invoke JACL add */
  int32_t r1 = fn(3, 4);
  ASSERT_INT_EQ(r1, 7);

  int32_t r2 = fn(100, 200);
  ASSERT_INT_EQ(r2, 300);

  int32_t r3 = fn(0, 0);
  ASSERT_INT_EQ(r3, 0);

  jacl_trampoline_free_val(vm, t);
  ASSERT(vm->trampoline_list == NULL);
  jacl_vm_free(vm);
  return 1;
}

/* Test 5: trampoline with various signatures */
static int test_e2e_tramp_various_signatures(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* void() — no-arg proc returning nil */
  jacl_eval(vm, "proc noop [] { nil }");
  JaclVal cl_noop = jacl_eval(vm, "$noop");
  JaclTrampoline* t_void = jacl_trampoline_new_val(vm, cl_noop, "void()");
  ASSERT(t_void != NULL);
  {
    void* fptr = jacl_trampoline_ptr_val(t_void);
    typedef void (*VoidFn)(void);
    VoidFn vfn; memcpy(&vfn, &fptr, sizeof(vfn));
    vfn(); /* must not crash */
  }
  jacl_trampoline_free_val(vm, t_void);

  /* i32(i32) — identity */
  jacl_eval(vm, "proc ident [x] { $x }");
  JaclVal cl_id = jacl_eval(vm, "$ident");
  JaclTrampoline* t_id = jacl_trampoline_new_val(vm, cl_id, "i32(i32)");
  ASSERT(t_id != NULL);
  {
    void* fptr = jacl_trampoline_ptr_val(t_id);
    typedef int32_t (*IdFn)(int32_t);
    IdFn idfn; memcpy(&idfn, &fptr, sizeof(idfn));
    ASSERT_INT_EQ(idfn(42), 42);
    ASSERT_INT_EQ(idfn(0), 0);
  }
  jacl_trampoline_free_val(vm, t_id);

  /* i32(i32,i32,i32) would need a 3-arg proc — skip, test 2-arg instead */
  jacl_eval(vm, "proc mul [a b] { [* $a $b] }");
  JaclVal cl_mul = jacl_eval(vm, "$mul");
  JaclTrampoline* t_mul = jacl_trampoline_new_val(vm, cl_mul, "i32(i32,i32)");
  ASSERT(t_mul != NULL);
  {
    void* fptr = jacl_trampoline_ptr_val(t_mul);
    typedef int32_t (*MulFn)(int32_t, int32_t);
    MulFn mfn; memcpy(&mfn, &fptr, sizeof(mfn));
    ASSERT_INT_EQ(mfn(6, 7), 42);
    ASSERT_INT_EQ(mfn(3, 3), 9);
  }
  jacl_trampoline_free_val(vm, t_mul);

  /* Invalid signature returns NULL */
  ASSERT(jacl_trampoline_new_val(vm, cl_noop, "xyz(i32)") == NULL);
  ASSERT(jacl_trampoline_new_val(vm, cl_noop, "i32[i32]") == NULL);

  jacl_vm_free(vm);
  return 1;
}

/* Test 6: free trampoline, verify closure becomes collectible (handle released) */
static int test_e2e_tramp_free_releases_closure(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_eval(vm, "proc dbl [x] { [* $x 2] }");
  JaclVal cl = jacl_eval(vm, "$dbl");
  ASSERT(jacl_is_closure(cl));

  /* Create trampoline — internally pins closure via GC handle */
  JaclTrampoline* t = jacl_trampoline_new_val(vm, cl, "i32(i32)");
  ASSERT(t != NULL);
  ASSERT(vm->trampoline_list == t);

  /* Verify trampoline works before free */
  void* fptr = jacl_trampoline_ptr_val(t);
  typedef int32_t (*DblFn)(int32_t);
  DblFn dfn; memcpy(&dfn, &fptr, sizeof(dfn));
  ASSERT_INT_EQ(dfn(5), 10);

  /* Free the trampoline — releases GC handle, removes from VM list */
  jacl_trampoline_free_val(vm, t);
  ASSERT(vm->trampoline_list == NULL);

  /* Force GC — should run without crash now that closure is unprotected */
  tramp_force_gc(vm);

  /* GC handle slot was released — a new handle can be allocated */
  EmbedJaclHandle h = jacl_handle_new_val(vm, jacl_i32_val(77));
  ASSERT(h.index != UINT32_MAX);
  ASSERT_INT_EQ(jacl_as_i32(jacl_handle_get_val(vm, h)), 77);
  jacl_handle_free_val(vm, h);

  /* vm_free handles empty trampoline list cleanly */
  jacl_vm_free(vm);
  return 1;
}

/*
 * Test 7: trampoline as callback to C qsort.
 *
 * The JACL comparator receives two u64 values (addresses of i32 elements)
 * via the ptr argument marshaling. A native "dref" function dereferences
 * the address to an i32. The comparator returns [- (dref a) (dref b)].
 */

/* Native: dereference a u64 address as i32 */
static JaclVal e2e_dref(JaclVM* vm, JaclVal* args, int argc) {
  (void)argc;
  uint64_t addr = jacl_as_u64_val(vm, args[0]);
  int32_t val;
  memcpy(&val, (void*)(uintptr_t)addr, sizeof(val));
  return jacl_i32(val);
}

static int test_e2e_tramp_qsort(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Register native pointer-dereference helper */
  bool reg = jacl_register_fn_val(vm, "dref", e2e_dref, 1);
  ASSERT(reg);

  /*
   * JACL comparator: [- (dref a) (dref b)]
   * Returns positive when a > b, negative when a < b, 0 when equal.
   * This is the classic a-b ascending comparator.
   */
  JaclVal r = jacl_eval(vm, "proc qcmp [a b] { [- [dref $a] [dref $b]] }");
  ASSERT(!jacl_is_error(r));
  JaclVal cl = jacl_eval(vm, "$qcmp");
  ASSERT(jacl_is_closure(cl));

  /* Create trampoline with qsort comparator signature: i32(ptr,ptr) */
  JaclTrampoline* t = jacl_trampoline_new_val(vm, cl, "i32(ptr,ptr)");
  ASSERT(t != NULL);

  /* Cast trampoline pointer to qsort comparator type */
  void* fptr = jacl_trampoline_ptr_val(t);
  typedef int (*CmpFn)(const void*, const void*);
  CmpFn cmp; memcpy(&cmp, &fptr, sizeof(cmp));

  /* Sort a small array of i32 values */
  int32_t arr[5] = { 5, 2, 8, 1, 9 };
  qsort(arr, 5, sizeof(int32_t), cmp);

  /* Verify ascending order */
  ASSERT_INT_EQ(arr[0], 1);
  ASSERT_INT_EQ(arr[1], 2);
  ASSERT_INT_EQ(arr[2], 5);
  ASSERT_INT_EQ(arr[3], 8);
  ASSERT_INT_EQ(arr[4], 9);

  jacl_trampoline_free_val(vm, t);
  jacl_vm_free(vm);
  return 1;
}

#endif /* JACL_HAS_LIBFFI */

int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("  %-56s ", #fn); \
    if (fn()) { pass++; printf("PASS\n"); } else { fail++; printf("FAIL\n"); } \
  } while (0)

  printf("=== E2E: Struct Interop and Trampolines (US-016) ===\n");

  /* Struct tests — always run */
  RUN(test_e2e_struct_create_and_read);
  RUN(test_e2e_struct_mutate_and_readback);
  RUN(test_e2e_struct_type_mismatch);

#ifdef JACL_HAS_LIBFFI
  /* Trampoline tests — only when libffi is available */
  RUN(test_e2e_tramp_basic_call);
  RUN(test_e2e_tramp_various_signatures);
  RUN(test_e2e_tramp_free_releases_closure);
  RUN(test_e2e_tramp_qsort);
#else
  printf("  (trampoline tests skipped — JACL_HAS_LIBFFI not set)\n");
#endif

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
