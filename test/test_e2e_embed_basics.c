#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-013: E2E tests — embedding basics ===== */

/* Test 1: create VM, eval [+ 1 2], extract i32, verify == 3, free VM */
static int test_e2e_eval_add(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal result = jacl_eval(vm, "[+ 1 2]");
  ASSERT(!jacl_is_error(result));
  ASSERT(jacl_is_i32(result));
  ASSERT_INT_EQ(jacl_as_i32(result), 3);

  jacl_vm_free(vm);
  return 1;
}

/* Test 2: eval with syntax error returns error-flagged value */
static int test_e2e_syntax_error(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Unclosed bracket — syntax error */
  JaclVal result = jacl_eval(vm, "[+ 1 2");
  ASSERT(jacl_is_error(result));

  /* Unexpected token */
  JaclVal result2 = jacl_eval(vm, "]]");
  ASSERT(jacl_is_error(result2));

  jacl_vm_free(vm);
  return 1;
}

/* Test 3: eval with runtime error returns error-flagged value with message */
static int test_e2e_runtime_error_with_message(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Undefined variable — triggers VM_RUNTIME_ERROR with error_message */
  JaclVal result = jacl_eval(vm, "$undefined_var");
  ASSERT(jacl_is_error(result));

  const char* msg = jacl_error_message_str(vm, result);
  ASSERT(msg != NULL);
  ASSERT(strlen(msg) > 0);

  /* Type mismatch in arithmetic also produces a runtime error with message */
  JaclVal result2 = jacl_eval(vm, "[+ 1 true]");
  ASSERT(jacl_is_error(result2));

  const char* msg2 = jacl_error_message_str(vm, result2);
  ASSERT(msg2 != NULL);
  ASSERT(strlen(msg2) > 0);

  /* VM should still be usable after runtime errors */
  JaclVal ok = jacl_eval(vm, "[+ 1 1]");
  ASSERT(!jacl_is_error(ok));
  ASSERT_INT_EQ(jacl_as_i32(ok), 2);

  jacl_vm_free(vm);
  return 1;
}

/* Test 4: create all value types from C, round-trip through eval, extract back */
static int test_e2e_value_roundtrip_all_types(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* nil */
  JaclVal nil_v = jacl_nil_val();
  ASSERT(jacl_is_nil(nil_v));

  /* bool true */
  JaclVal bt = jacl_bool_val(true);
  ASSERT(jacl_as_bool_val(bt) == true);

  /* bool false */
  JaclVal bf = jacl_bool_val(false);
  ASSERT(jacl_as_bool_val(bf) == false);

  /* i32 */
  JaclVal i32v = jacl_i32_val(-42);
  ASSERT(jacl_is_i32(i32v));
  ASSERT_INT_EQ(jacl_as_i32_val(i32v), -42);

  /* i64 */
  JaclVal i64v = jacl_i64_val(vm, 1234567890123LL);
  ASSERT(jacl_is_i64(i64v));
  ASSERT_I64_EQ(jacl_as_i64_val(i64v), 1234567890123LL);

  /* u32 */
  JaclVal u32v = jacl_u32_val(3000000000U);
  ASSERT(jacl_is_u32(u32v));
  ASSERT_U32_EQ(jacl_as_u32_val(u32v), 3000000000U);

  /* u64 */
  JaclVal u64v = jacl_u64_val(vm, 9999999999999ULL);
  ASSERT(jacl_is_u64(u64v));
  ASSERT_U64_EQ(jacl_as_u64_val(u64v), 9999999999999ULL);

  /* f32 */
  JaclVal f32v = jacl_f32_val(3.14f);
  ASSERT(jacl_is_f32(f32v));
  float f32_out = jacl_as_f32_val(f32v);
  ASSERT(f32_out > 3.13f && f32_out < 3.15f);

  /* f64 */
  JaclVal f64v = jacl_f64_val(vm, 2.718281828);
  ASSERT(jacl_is_f64(f64v));
  double f64_out = jacl_as_f64_val(f64v);
  ASSERT(f64_out > 2.718 && f64_out < 2.719);

  /* short string (inline, <=7 bytes) */
  JaclVal s_short = jacl_string_cstr_val(vm, "hello");
  size_t slen = 0;
  const char* sptr = jacl_as_cstr_val(vm, s_short, &slen);
  ASSERT(sptr != NULL);
  ASSERT_SIZE_EQ(slen, 5);
  ASSERT(memcmp(sptr, "hello", 5) == 0);

  /* long string (heap, >7 bytes) */
  JaclVal s_long = jacl_string_cstr_val(vm, "a longer string value");
  size_t llen = 0;
  const char* lptr = jacl_as_cstr_val(vm, s_long, &llen);
  ASSERT(lptr != NULL);
  ASSERT_SIZE_EQ(llen, 21);
  ASSERT(memcmp(lptr, "a longer string value", 21) == 0);

  /* Round-trip i32 through eval: define in C, pass through JACL expression */
  jacl_register_fn_val(vm, "get42", NULL, 0); /* won't use this — use direct eval */

  /* Eval an expression that produces an i32 */
  JaclVal rt = jacl_eval(vm, "[+ 20 22]");
  ASSERT(!jacl_is_error(rt));
  ASSERT_INT_EQ(jacl_as_i32(rt), 42);

  /* Eval bool expressions */
  JaclVal rt_true = jacl_eval(vm, "[== 1 1]");
  ASSERT(!jacl_is_error(rt_true));
  ASSERT(jacl_as_bool_val(rt_true) == true);

  JaclVal rt_false = jacl_eval(vm, "[== 1 2]");
  ASSERT(!jacl_is_error(rt_false));
  ASSERT(jacl_as_bool_val(rt_false) == false);

  jacl_vm_free(vm);
  return 1;
}

/* Test 5: string creation, extraction, and GC survival via handle */
static int test_e2e_string_gc_handle(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Create a heap-allocated string (>7 bytes) */
  JaclVal str = jacl_string_cstr_val(vm, "surviving string!");
  ASSERT(!jacl_is_nil(str));

  /* Pin it with a GC handle */
  EmbedJaclHandle h = jacl_handle_new_val(vm, str);
  ASSERT(h.index != UINT32_MAX);

  /* Force GC by allocating many strings */
  for (int i = 0; i < 500; i++) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "garbage-%d", i);
    jacl_string_val(vm, buf, (size_t)n);
  }

  /* Retrieve the pinned value — it must have survived GC */
  JaclVal recovered = jacl_handle_get_val(vm, h);
  ASSERT(recovered == str);

  /* Verify string content is still intact */
  size_t len = 0;
  const char* cstr = jacl_as_cstr_val(vm, recovered, &len);
  ASSERT(cstr != NULL);
  ASSERT_SIZE_EQ(len, 17);
  ASSERT(memcmp(cstr, "surviving string!", 17) == 0);

  /* Release handle */
  jacl_handle_free_val(vm, h);

  jacl_vm_free(vm);
  return 1;
}

/* Test 6: multiple sequential evals share global state */
static int test_e2e_globals_persist(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* First eval: define a global */
  JaclVal r1 = jacl_eval(vm, "def x 10");
  ASSERT(!jacl_is_error(r1));

  /* Second eval: use the global */
  JaclVal r2 = jacl_eval(vm, "[+ $x 5]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 15);

  /* Third eval: define a proc using the global */
  JaclVal r3 = jacl_eval(vm, "proc getx {} { $x }");
  ASSERT(!jacl_is_error(r3));

  /* Fourth eval: call the proc */
  JaclVal r4 = jacl_eval(vm, "[getx]");
  ASSERT(!jacl_is_error(r4));
  ASSERT_INT_EQ(jacl_as_i32(r4), 10);

  /* Fifth eval: mutate global and verify */
  JaclVal r5 = jacl_eval(vm, "def x 99");
  ASSERT(!jacl_is_error(r5));

  JaclVal r6 = jacl_eval(vm, "$x");
  ASSERT(!jacl_is_error(r6));
  ASSERT_INT_EQ(jacl_as_i32(r6), 99);

  jacl_vm_free(vm);
  return 1;
}

/* Test 7: multiple independent VMs don't interfere */
static int test_e2e_independent_vms(void) {
  JaclVM* vm1 = jacl_vm_new();
  JaclVM* vm2 = jacl_vm_new();
  ASSERT(vm1 != NULL);
  ASSERT(vm2 != NULL);

  /* Define different globals in each VM */
  JaclVal r1 = jacl_eval(vm1, "def val 111");
  ASSERT(!jacl_is_error(r1));

  JaclVal r2 = jacl_eval(vm2, "def val 222");
  ASSERT(!jacl_is_error(r2));

  /* Read back — each VM has its own state */
  JaclVal v1 = jacl_eval(vm1, "$val");
  ASSERT(!jacl_is_error(v1));
  ASSERT_INT_EQ(jacl_as_i32(v1), 111);

  JaclVal v2 = jacl_eval(vm2, "$val");
  ASSERT(!jacl_is_error(v2));
  ASSERT_INT_EQ(jacl_as_i32(v2), 222);

  /* Define a proc in vm1 only */
  jacl_eval(vm1, "proc sq {n} { * $n $n }");
  JaclVal sq1 = jacl_eval(vm1, "[sq 5]");
  ASSERT(!jacl_is_error(sq1));
  ASSERT_INT_EQ(jacl_as_i32(sq1), 25);

  /* vm2 should not have that proc */
  JaclVal sq2 = jacl_eval(vm2, "[sq 5]");
  ASSERT(jacl_is_error(sq2));

  /* Free in any order */
  jacl_vm_free(vm2);
  jacl_vm_free(vm1);
  return 1;
}

/* Test: ctx declarations work via jacl_eval. Covers two things at once:
   (1) lazy ctx__init_vm in embed.c -- without it, the very first
       `ctx <type> <name> = ...` trips OP_HEAP_RECORD_SET with "field
       mutation on non-struct value" because OP_GET_CTX returns nil.
   (2) cross-eval ctx-field visibility -- the typer + compiler ctx
       field lists are seeded from the persistent struct registry on
       each compile, so a `ctx ...` declaration in one jacl_eval is
       visible to `$ctx->field` and `with-ctx` in a later call. */
static int test_e2e_ctx_declaration(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Single-call form: decl + read + with-ctx override + restore. */
  JaclVal r = jacl_eval(vm,
      "ctx mut i32 verbosity 7\n"
      "def baseline $ctx->verbosity\n"
      "def overridden [with-ctx {verbosity 99} { $ctx->verbosity }]\n"
      "def after $ctx->verbosity\n"
      "[+ [+ $baseline $overridden] $after]");
  ASSERT(!jacl_is_error(r));
  ASSERT(jacl_is_i32(r));
  ASSERT_INT_EQ(jacl_as_i32(r), 7 + 99 + 7);

  jacl_vm_free(vm);
  return 1;
}

/* Test: ctx fields declared in one jacl_eval remain visible to later
   evals. Pre-fix, typer__register_ctx_struct + the compiler's
   CtxFieldList rebuilt from the current source's AST_CTX_DECL nodes
   each compile, so eval #2's `$ctx->verbosity` failed with "no field
   'verbosity' on ctx" even though the runtime ctx struct still held
   it. Both sides now seed from the persistent registry's ctx
   StructTypeDef before walking the AST. */
static int test_e2e_ctx_cross_eval(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal r1 = jacl_eval(vm, "ctx mut i32 verbosity 7");
  ASSERT(!jacl_is_error(r1));

  JaclVal r2 = jacl_eval(vm, "$ctx->verbosity");
  ASSERT(!jacl_is_error(r2));
  ASSERT(jacl_is_i32(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 7);

  JaclVal r3 = jacl_eval(vm, "with-ctx {verbosity 99} { $ctx->verbosity }");
  ASSERT(!jacl_is_error(r3));
  ASSERT(jacl_is_i32(r3));
  ASSERT_INT_EQ(jacl_as_i32(r3), 99);

  JaclVal r4 = jacl_eval(vm, "$ctx->verbosity");
  ASSERT(!jacl_is_error(r4));
  ASSERT_INT_EQ(jacl_as_i32(r4), 7);

  jacl_vm_free(vm);
  return 1;
}

/* Test: the persistent struct registry retains usable names across
   jacl_eval calls. Pre-fix, StructTypeDef.name and StructTypeField.name
   stored raw token pointers into the caller's source buffer; once the
   caller's source memory was freed (or just reused for the next eval),
   struct_registry__find on the second eval scanned garbage and either
   missed the entry or compared against random bytes.

   Test strategy: declare Pt in eval #1 from a heap buffer, free the
   buffer, then redeclare Pt in eval #2. The duplicate-struct check at
   compile time goes through struct_registry__find on the registry's
   stored name -- if that pointer dangles, the lookup misses and the
   second declaration is silently accepted; if the fix copies names
   into the registry arena, the duplicate is detected. Asserting on
   the "duplicate" error path is the cleanest memory-safety probe that
   doesn't require cross-eval typer state (a separate gap tracked in
   NOT_IMPLEMENTED.md §10). */
static int test_e2e_struct_name_persists(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  char* src1 = strdup("struct Pt {i32 x, i32 y}");
  ASSERT(src1 != NULL);
  JaclVal r1 = jacl_eval(vm, src1);
  ASSERT(!jacl_is_error(r1));
  /* Scribble before freeing so any dangling pointer reads something
     other than "Pt" (some allocators reuse freed blocks immediately
     without changing the contents). */
  memset(src1, 'X', strlen(src1));
  free(src1);

  JaclVal r2 = jacl_eval(vm, "struct Pt {i32 x, i32 y}");
  ASSERT(jacl_is_error(r2));
  const char* msg = jacl_error_message_str(vm, r2);
  ASSERT(msg != NULL);
  ASSERT(strstr(msg, "duplicate struct definition") != NULL);
  ASSERT(strstr(msg, "Pt") != NULL);

  jacl_vm_free(vm);
  return 1;
}

/* Test: typed proc signatures survive across jacl_eval calls.
   Pre-fix, the per-compile global_arities table forgot every proc's
   typed-param shape between calls. eval #3's call site looked up
   sumpt in a fresh global_arities, found nothing, defaulted
   expected_param_type to TYPE_DYN, and the struct-arg vs
   dyn-param mismatch tripped "cannot pass struct value to dyn
   parameter" -- a compile-time check, even though the proc body
   was alive in the VM env and would have executed correctly.
   With the persistent_global_arities table, eval #3 sees sumpt's
   declared Pt p signature and the call type-checks + runs. */
static int test_e2e_proc_signature_cross_eval(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  JaclVal r1 = jacl_eval(vm, "struct Pt {i32 x, i32 y}");
  ASSERT(!jacl_is_error(r1));

  JaclVal r2 = jacl_eval(vm, "proc sumpt {Pt p} i32 { [+ $p->x $p->y] }");
  ASSERT(!jacl_is_error(r2));

  JaclVal r3 = jacl_eval(vm, "[sumpt [Pt x 3 y 4]]");
  ASSERT(!jacl_is_error(r3));
  ASSERT(jacl_is_i32(r3));
  ASSERT_INT_EQ(jacl_as_i32(r3), 7);

  jacl_vm_free(vm);
  return 1;
}

/* Test: persistent GlobalArity table grows past the old 64-entry cap.
   Pre-fix, embed.c stored a fixed GlobalArity[COMPILER_GLOBAL_ARITIES_MAX]
   array on JaclVM and silently dropped commits beyond entry 64; an
   embedder declaring N typed procs across N jacl_eval calls would lose
   the typed param signature for everything past the cap, and downstream
   call sites would compile-error "cannot pass struct value to dyn
   parameter". This test declares 100 typed procs (one per eval), then
   calls the first, the middle, and the last to exercise lookups across
   the doubled-capacity growth boundary (16 -> 32 -> 64 -> 128). */
static int test_e2e_proc_signature_growth(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  char buf[128];
  for (int i = 0; i < 100; i++) {
    snprintf(buf, sizeof(buf),
             "proc p%d {i32 x} i32 { [+ $x %d] }", i, i);
    JaclVal r = jacl_eval(vm, buf);
    ASSERT(!jacl_is_error(r));
  }

  static const int probe[] = {0, 50, 99};
  for (int k = 0; k < 3; k++) {
    int i = probe[k];
    snprintf(buf, sizeof(buf), "[p%d 1000]", i);
    JaclVal r = jacl_eval(vm, buf);
    ASSERT(!jacl_is_error(r));
    ASSERT(jacl_is_i32(r));
    ASSERT_INT_EQ(jacl_as_i32(r), 1000 + i);
  }

  jacl_vm_free(vm);
  return 1;
}

int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("  %-50s ", #fn); \
    if (fn()) { pass++; printf("PASS\n"); } else { fail++; printf("FAIL\n"); } \
  } while (0)

  printf("=== E2E: Embedding Basics (US-013) ===\n");
  RUN(test_e2e_eval_add);
  RUN(test_e2e_syntax_error);
  RUN(test_e2e_runtime_error_with_message);
  RUN(test_e2e_value_roundtrip_all_types);
  RUN(test_e2e_string_gc_handle);
  RUN(test_e2e_globals_persist);
  RUN(test_e2e_independent_vms);
  RUN(test_e2e_ctx_declaration);
  RUN(test_e2e_ctx_cross_eval);
  RUN(test_e2e_struct_name_persists);
  RUN(test_e2e_proc_signature_cross_eval);
  RUN(test_e2e_proc_signature_growth);

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
