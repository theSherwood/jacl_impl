#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-014: E2E tests — native functions and jacl_call ===== */

/*
 * Native helper functions used across tests.
 * These simulate realistic C callbacks that might be registered from a host app.
 */

/* Multiplies two i32 args and returns their product */
static JaclVal e2e_native_mul(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm; (void)argc;
  int32_t a = jacl_as_i32(args[0]);
  int32_t b = jacl_as_i32(args[1]);
  return jacl_i32(a * b);
}

/* Returns the number of args passed (variadic argc check) */
static JaclVal e2e_native_argc_check(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm; (void)args;
  return jacl_i32(argc);
}

/* Sums all variadic i32 args */
static JaclVal e2e_native_sum(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm;
  int32_t total = 0;
  for (int i = 0; i < argc; i++) {
    total += jacl_as_i32(args[i]);
  }
  return jacl_i32(total);
}

/* Always returns an error-flagged value */
static JaclVal e2e_native_fail(JaclVM* vm, JaclVal* args, int argc) {
  (void)vm; (void)args; (void)argc;
  return jacl_set_error(JACL_NIL);
}

/* Re-entrant: calls jacl_call_named_val("triple", args, argc) back into JACL */
static JaclVal e2e_native_call_triple(JaclVM* vm, JaclVal* args, int argc) {
  return jacl_call_named_val(vm, "triple", args, argc);
}

/* Converts i32 to string via jacl_string_cstr_val */
static JaclVal e2e_native_i32str(JaclVM* vm, JaclVal* args, int argc) {
  (void)argc;
  int32_t n = jacl_as_i32(args[0]);
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d", n);
  return jacl_string_val(vm, buf, (size_t)len);
}

/* Test 1: register native function, call from JACL, verify args and return value */
static int test_e2e_native_fn_register_and_call(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Register a native multiplier */
  bool ok = jacl_register_fn_val(vm, "mul", e2e_native_mul, 2);
  ASSERT(ok);

  /* Call it from JACL with literal args */
  JaclVal r1 = jacl_eval(vm, "[mul 6 7]");
  ASSERT(!jacl_is_error(r1));
  ASSERT(jacl_is_i32(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 42);

  /* Call with computed args */
  JaclVal r2 = jacl_eval(vm, "[mul [+ 3 4] [+ 5 5]]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 70); /* 7 * 10 */

  /* Use it inside a JACL proc */
  JaclVal r3 = jacl_eval(vm, "proc sq {n} { mul $n $n }");
  ASSERT(!jacl_is_error(r3));

  JaclVal r4 = jacl_eval(vm, "[sq 9]");
  ASSERT(!jacl_is_error(r4));
  ASSERT_INT_EQ(jacl_as_i32(r4), 81);

  jacl_vm_free(vm);
  return 1;
}

/* Test 2: variadic native function receives correct argc/argv */
static int test_e2e_variadic_native_fn(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Register variadic argc counter (arity=-1) */
  bool ok1 = jacl_register_fn_val(vm, "cnt", e2e_native_argc_check, -1);
  ASSERT(ok1);

  /* Register variadic sum */
  bool ok2 = jacl_register_fn_val(vm, "vsum", e2e_native_sum, -1);
  ASSERT(ok2);

  /* argc check: 0 args */
  JaclVal r0 = jacl_eval(vm, "[cnt]");
  ASSERT(!jacl_is_error(r0));
  ASSERT_INT_EQ(jacl_as_i32(r0), 0);

  /* argc check: 1 arg */
  JaclVal r1 = jacl_eval(vm, "[cnt 99]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 1);

  /* argc check: 4 args */
  JaclVal r4 = jacl_eval(vm, "[cnt 1 2 3 4]");
  ASSERT(!jacl_is_error(r4));
  ASSERT_INT_EQ(jacl_as_i32(r4), 4);

  /* Sum with 1 arg */
  JaclVal s1 = jacl_eval(vm, "[vsum 42]");
  ASSERT(!jacl_is_error(s1));
  ASSERT_INT_EQ(jacl_as_i32(s1), 42);

  /* Sum with 5 args */
  JaclVal s5 = jacl_eval(vm, "[vsum 1 2 3 4 5]");
  ASSERT(!jacl_is_error(s5));
  ASSERT_INT_EQ(jacl_as_i32(s5), 15);

  jacl_vm_free(vm);
  return 1;
}

/* Test 3: native function returns error value, JACL propagates error flag */
static int test_e2e_native_fn_error_propagation(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  jacl_register_fn_val(vm, "fail", e2e_native_fail, 0);

  /* Direct call: error is returned to eval */
  JaclVal r1 = jacl_eval(vm, "[fail]");
  ASSERT(jacl_is_error(r1));

  /* Error returned inside a proc propagates out */
  JaclVal def_r = jacl_eval(vm, "proc wrap {} { fail }");
  ASSERT(!jacl_is_error(def_r));

  JaclVal r2 = jacl_eval(vm, "[wrap]");
  ASSERT(jacl_is_error(r2));

  /* VM still usable after native error */
  JaclVal ok = jacl_eval(vm, "[+ 1 1]");
  ASSERT(!jacl_is_error(ok));
  ASSERT_INT_EQ(jacl_as_i32(ok), 2);

  jacl_vm_free(vm);
  return 1;
}

/* Test 4: native function calls jacl_call back into JACL (re-entrant) */
static int test_e2e_native_fn_reentrant(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define a JACL proc that the native will call */
  JaclVal def_r = jacl_eval(vm, "proc triple {x} { * $x 3 }");
  ASSERT(!jacl_is_error(def_r));

  /* Register native fn that re-enters JACL via jacl_call_named_val */
  jacl_register_fn_val(vm, "ctrpl", e2e_native_call_triple, 1);

  /* Call from JACL: native fn → jacl_call_named → JACL closure */
  JaclVal r1 = jacl_eval(vm, "[ctrpl 7]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 21);

  /* Chain: call native from a JACL proc that itself is called from C */
  JaclVal def2 = jacl_eval(vm, "proc chain {n} { ctrpl [+ $n 1] }");
  ASSERT(!jacl_is_error(def2));

  JaclVal r2 = jacl_eval(vm, "[chain 4]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 15); /* triple(4+1) = 15 */

  /* Call native directly from C using jacl_call_val */
  JaclVal fn = jacl_eval(vm, "$ctrpl");
  ASSERT(!jacl_is_error(fn));

  JaclVal args[1] = { jacl_i32(10) };
  JaclVal r3 = jacl_call_val(vm, fn, args, 1);
  ASSERT(!jacl_is_error(r3));
  ASSERT_INT_EQ(jacl_as_i32(r3), 30);

  jacl_vm_free(vm);
  return 1;
}

/* Test 5: jacl_call_named looks up and calls a global proc */
static int test_e2e_jacl_call_named(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Define several JACL procs */
  JaclVal d1 = jacl_eval(vm, "proc add {a, b} { + $a $b }");
  ASSERT(!jacl_is_error(d1));

  JaclVal d2 = jacl_eval(vm, "proc greet {n} { $n }");
  ASSERT(!jacl_is_error(d2));

  /* Call by name from C */
  JaclVal args1[2] = { jacl_i32(10), jacl_i32(32) };
  JaclVal r1 = jacl_call_named_val(vm, "add", args1, 2);
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 42);

  /* Call a proc that returns a string */
  JaclVal str_arg = jacl_string_cstr_val(vm, "hello");
  JaclVal args2[1] = { str_arg };
  JaclVal r2 = jacl_call_named_val(vm, "greet", args2, 1);
  ASSERT(!jacl_is_error(r2));
  size_t len = 0;
  const char* s = jacl_as_cstr_val(vm, r2, &len);
  ASSERT(s != NULL);
  ASSERT_SIZE_EQ(len, 5);
  ASSERT(memcmp(s, "hello", 5) == 0);

  /* Undefined name returns error */
  JaclVal r_err = jacl_call_named_val(vm, "bogus", NULL, 0);
  ASSERT(jacl_is_error(r_err));

  /* jacl_call_val on a closure obtained via eval */
  JaclVal fn = jacl_eval(vm, "$add");
  ASSERT(!jacl_is_error(fn));
  ASSERT(jacl_is_closure(fn));

  JaclVal args3[2] = { jacl_i32(100), jacl_i32(-58) };
  JaclVal r3 = jacl_call_val(vm, fn, args3, 2);
  ASSERT(!jacl_is_error(r3));
  ASSERT_INT_EQ(jacl_as_i32(r3), 42);

  jacl_vm_free(vm);
  return 1;
}

/* Test 6: register multiple native functions, call them in sequence */
static int test_e2e_multiple_native_fns_sequence(void) {
  JaclVM* vm = jacl_vm_new();
  ASSERT(vm != NULL);

  /* Register several functions with different arities */
  ASSERT(jacl_register_fn_val(vm, "mul2", e2e_native_mul, 2));
  ASSERT(jacl_register_fn_val(vm, "cnt", e2e_native_argc_check, -1));
  ASSERT(jacl_register_fn_val(vm, "vsum", e2e_native_sum, -1));
  ASSERT(jacl_register_fn_val(vm, "fail", e2e_native_fail, 0));
  ASSERT(jacl_register_fn_val(vm, "i32s", e2e_native_i32str, 1));

  /* Call each in sequence */
  JaclVal r1 = jacl_eval(vm, "[mul2 3 4]");
  ASSERT(!jacl_is_error(r1));
  ASSERT_INT_EQ(jacl_as_i32(r1), 12);

  JaclVal r2 = jacl_eval(vm, "[cnt 10 20 30]");
  ASSERT(!jacl_is_error(r2));
  ASSERT_INT_EQ(jacl_as_i32(r2), 3);

  JaclVal r3 = jacl_eval(vm, "[vsum 1 2 3 4 5 6 7 8 9 10]");
  ASSERT(!jacl_is_error(r3));
  ASSERT_INT_EQ(jacl_as_i32(r3), 55);

  JaclVal r4 = jacl_eval(vm, "[fail]");
  ASSERT(jacl_is_error(r4));

  /* String-returning native */
  JaclVal r5 = jacl_eval(vm, "[i32s 42]");
  ASSERT(!jacl_is_error(r5));
  size_t len = 0;
  const char* s = jacl_as_cstr_val(vm, r5, &len);
  ASSERT(s != NULL);
  ASSERT(memcmp(s, "42", 2) == 0);

  /* Use them together inside a JACL expression */
  JaclVal r6 = jacl_eval(vm, "[mul2 [vsum 1 2 3] [cnt 4 5]]");
  ASSERT(!jacl_is_error(r6));
  ASSERT_INT_EQ(jacl_as_i32(r6), 12); /* mul2(6, 2) = 12 */

  /* After an error, remaining calls still work */
  JaclVal r7 = jacl_eval(vm, "[mul2 7 6]");
  ASSERT(!jacl_is_error(r7));
  ASSERT_INT_EQ(jacl_as_i32(r7), 42);

  jacl_vm_free(vm);
  return 1;
}

int main(void) {
  int pass = 0, fail = 0;

  #define RUN(fn) do { \
    printf("  %-55s ", #fn); \
    if (fn()) { pass++; printf("PASS\n"); } else { fail++; printf("FAIL\n"); } \
  } while (0)

  printf("=== E2E: Native Functions and jacl_call (US-014) ===\n");
  RUN(test_e2e_native_fn_register_and_call);
  RUN(test_e2e_variadic_native_fn);
  RUN(test_e2e_native_fn_error_propagation);
  RUN(test_e2e_native_fn_reentrant);
  RUN(test_e2e_jacl_call_named);
  RUN(test_e2e_multiple_native_fns_sequence);

  printf("\n%d passed, %d failed\n", pass, fail);
  return fail > 0 ? 1 : 0;
}
