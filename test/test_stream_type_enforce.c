#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== US-016: Compile-time stream/vector type enforcement ===== */

typedef struct {
  char     buf[16384];
  uint32_t len;
} PrintCapture;

static void capture_print(const char* text, uint32_t len, void* ctx) {
  PrintCapture* cap = (PrintCapture*)ctx;
  uint32_t remaining = (uint32_t)sizeof(cap->buf) - cap->len - 1;
  uint32_t copy_len = len < remaining ? len : remaining;
  memcpy(cap->buf + cap->len, text, copy_len);
  cap->len += copy_len;
  cap->buf[cap->len] = '\0';
}

static VMResult run_capture(const char* src, PrintCapture* cap) {
  cap->len = 0;
  cap->buf[0] = '\0';
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);
  vm.print_fn = capture_print;
  vm.print_ctx = cap;
  VMResult result = jacl_run(src, &vm, &arena);
  vm_destroy(&vm);
  arena_destroy(&arena);
  return result;
}

/* Helper: run program, expect error containing substring */
static int run_err(const char* source, const char* err_substr) {
  tracker_reset();
  arena_t arena = { .allocator = tracked_allocator };
  VM vm;
  vm_init(&vm, &arena);
  VMResult result = jacl_run(source, &vm, &arena);
  if (result != VM_RUNTIME_ERROR) {
    fprintf(stderr, "  Expected VM_RUNTIME_ERROR but got VM_OK\n");
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  if (err_substr && (!vm.error_message || !strstr(vm.error_message, err_substr))) {
    fprintf(stderr, "  Error message '%s' does not contain '%s'\n",
            vm.error_message ? vm.error_message : "(null)", err_substr);
    vm_destroy(&vm);
    arena_destroy(&arena);
    return 0;
  }
  vm_destroy(&vm);
  arena_destroy(&arena);
  if (!check_no_leaks()) return 0;
  return 1;
}

/* --- Test: compile error — typed stream to vec-get --- */
static int test_compile_stream_vec_get(void) {
  /* lines returns TYPE_STREAM, so vec-get should be caught at compile time */
  ASSERT(run_err(
    "[vec-get [lines \"a\\nb\"] 0]\n",
    "vec-get requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: compile error — typed stream to vec-len --- */
static int test_compile_stream_vec_len(void) {
  ASSERT(run_err(
    "[vec-len [lines \"a\\nb\"]]\n",
    "vec-len requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: compile error — typed stream to vec-push --- */
static int test_compile_stream_vec_push(void) {
  ASSERT(run_err(
    "[vec-push [lines \"a\\nb\"] 42]\n",
    "vec-push requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: compile error — typed stream to vec-set --- */
static int test_compile_stream_vec_set(void) {
  ASSERT(run_err(
    "[vec-set [lines \"a\\nb\"] 0 42]\n",
    "vec-set requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: compile error — typed stream to vec-concat --- */
static int test_compile_stream_vec_concat(void) {
  ASSERT(run_err(
    "[vec-concat [lines \"a\\nb\"] [vec 1 2]]\n",
    "vec-concat requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: compile error — typed stream to vec-slice --- */
static int test_compile_stream_vec_slice(void) {
  ASSERT(run_err(
    "[vec-slice [lines \"a\\nb\"] 0 1]\n",
    "vec-slice requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: collect + vec-get compiles and works --- */
static int test_collect_then_vec_get(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [vec-get [collect [lines \"a\\nb\\nc\"]] 1]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "b\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: collect + vec-len compiles and works --- */
static int test_collect_then_vec_len(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [vec-len [collect [lines \"a\\nb\\nc\"]]]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: runtime error — dyn-typed stream to vec-get --- */
static int test_runtime_dyn_stream_vec_get(void) {
  /* Pass stream through a proc that returns dyn type */
  ASSERT(run_err(
    "proc id {x} { $x }\n"
    "[vec-get [id [lines \"a\\nb\"]] 0]\n",
    "vec-get requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: runtime error — dyn-typed stream to vec-len --- */
static int test_runtime_dyn_stream_vec_len(void) {
  ASSERT(run_err(
    "proc id {x} { $x }\n"
    "[vec-len [id [lines \"a\\nb\"]]]\n",
    "vec-len requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: runtime error — dyn-typed stream to vec-push --- */
static int test_runtime_dyn_stream_vec_push(void) {
  ASSERT(run_err(
    "proc id {x} { $x }\n"
    "[vec-push [id [lines \"a\\nb\"]] 42]\n",
    "vec-push requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: runtime error — dyn-typed stream to vec-set --- */
static int test_runtime_dyn_stream_vec_set(void) {
  ASSERT(run_err(
    "proc id {x} { $x }\n"
    "[vec-set [id [lines \"a\\nb\"]] 0 42]\n",
    "vec-set requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: runtime error — dyn-typed stream to vec-concat --- */
static int test_runtime_dyn_stream_vec_concat(void) {
  ASSERT(run_err(
    "proc id {x} { $x }\n"
    "[vec-concat [id [lines \"a\\nb\"]] [vec 1]]\n",
    "vec-concat requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: runtime error — dyn-typed stream to vec-slice --- */
static int test_runtime_dyn_stream_vec_slice(void) {
  ASSERT(run_err(
    "proc id {x} { $x }\n"
    "[vec-slice [id [lines \"a\\nb\"]] 0 1]\n",
    "vec-slice requires a vector; got stream"));
  TEST_PASS();
}

/* --- Test: spread of derived stream works --- */
static int test_spread_stream_derived(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "[print [collect [take [lines \"a\\nb\\nc\"] 2]]]\n",
    &cap);
  /* Verify take on lines stream works; then test spread below */
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec \"a\" \"b\"]\n");

  /* Now test spread with a filter stream producing numbers */
  cap.len = 0; cap.buf[0] = '\0';
  r = run_capture(
    "proc gen {} {\n"
    "  [yield 1]\n"
    "  [yield 2]\n"
    "  [yield 3]\n"
    "}\n"
    "s = [filter [gen] [\\ > $it 1]]\n"
    "[print [+ ..$s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "5\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: spread of generator stream works --- */
static int test_spread_stream_generator(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc gen {} {\n"
    "  [yield 10]\n"
    "  [yield 20]\n"
    "  [yield 30]\n"
    "}\n"
    "s = [gen]\n"
    "[print [+ ..$s]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "60\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- [Stream T] yield element-type enforcement ----------------- */

/* yielding a wrong typed value into [Stream i64] is a compile error. */
static int test_yield_str_into_stream_i64_errors(void) {
  ASSERT(run_err(
    "proc [Stream i64] gen {} { yield \"oops\" }\n"
    "gen\n",
    "yield expected i64"));
  TEST_PASS();
}

/* yielding a dyn value into [Stream i64] is a compile error (the
 * commitment-site rule applies — dyn-into-typed-slot needs an
 * explicit [to T $val] cast, parallel to typed-vec push). */
static int test_yield_dyn_into_stream_i64_errors(void) {
  ASSERT(run_err(
    "proc [Stream i64] gen {x} { yield $x }\n"
    "gen 1\n",
    "yield expected i64"));
  TEST_PASS();
}

/* int literal narrows to declared element type — `yield 42` into
 * [Stream i64] picks up the i64 element via expected_type. */
static int test_yield_literal_narrows_into_stream_i64(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc [Stream i64] gen {} { yield 42 }\n"
    "print [collect [gen]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 42]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* mixed yields into [Stream dyn] are lenient — same as unannotated. */
static int test_yield_mixed_into_stream_dyn_ok(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc [Stream dyn] gen {} {\n"
    "  yield 1\n"
    "  yield \"two\"\n"
    "}\n"
    "print [collect [gen]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "[vec 1 \"two\"]\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* for-loop over an annotated stream still works end-to-end. Element-
 * type narrowing on the loop binding is deferred (see NOT_IMPLEMENTED.md
 * §4 — would need unboxing on OP_STREAM_NEXT), so `$it` stays dyn even
 * for `[Stream i64]`. */
static int test_for_in_annotated_stream_iterates(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "proc [Stream i64] gen {} {\n"
    "  yield 1\n"
    "  yield 2\n"
    "  yield 3\n"
    "}\n"
    "for [gen] { print $it }\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "1\n2\n3\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test: existing typed vec code still works (no false positives) --- */
static int test_vec_ops_still_work(void) {
  PrintCapture cap;
  VMResult r = run_capture(
    "def v [vec 10 20 30]\n"
    "[print [vec-get $v 1]]\n"
    "[print [vec-len $v]]\n"
    "[print [vec-get [vec-push $v 40] 3]]\n"
    "[print [vec-get [vec-set $v 0 99] 0]]\n"
    "[print [vec-len [vec-concat $v [vec 40 50]]]]\n"
    "[print [vec-get [vec-slice $v 1 3] 0]]\n",
    &cap);
  ASSERT_INT_EQ(r, VM_OK);
  ASSERT_STR_EQ(cap.buf, "20\n3\n40\n99\n5\n20\n");
  ASSERT(check_no_leaks());
  TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    /* Compile-time errors for typed streams */
    { "compile_stream_vec_get",        test_compile_stream_vec_get },
    { "compile_stream_vec_len",        test_compile_stream_vec_len },
    { "compile_stream_vec_push",       test_compile_stream_vec_push },
    { "compile_stream_vec_set",        test_compile_stream_vec_set },
    { "compile_stream_vec_concat",     test_compile_stream_vec_concat },
    { "compile_stream_vec_slice",      test_compile_stream_vec_slice },
    /* Collect then vec-op works */
    { "collect_then_vec_get",          test_collect_then_vec_get },
    { "collect_then_vec_len",          test_collect_then_vec_len },
    /* Runtime errors for dyn-typed streams */
    { "runtime_dyn_stream_vec_get",    test_runtime_dyn_stream_vec_get },
    { "runtime_dyn_stream_vec_len",    test_runtime_dyn_stream_vec_len },
    { "runtime_dyn_stream_vec_push",   test_runtime_dyn_stream_vec_push },
    { "runtime_dyn_stream_vec_set",    test_runtime_dyn_stream_vec_set },
    { "runtime_dyn_stream_vec_concat", test_runtime_dyn_stream_vec_concat },
    { "runtime_dyn_stream_vec_slice",  test_runtime_dyn_stream_vec_slice },
    /* Spread accepts streams */
    { "spread_stream_derived",         test_spread_stream_derived },
    { "spread_stream_generator",       test_spread_stream_generator },
    /* [Stream T] yield element-type enforcement */
    { "yield_str_into_stream_i64_errors",   test_yield_str_into_stream_i64_errors },
    { "yield_dyn_into_stream_i64_errors",   test_yield_dyn_into_stream_i64_errors },
    { "yield_literal_narrows_into_stream_i64", test_yield_literal_narrows_into_stream_i64 },
    { "yield_mixed_into_stream_dyn_ok",     test_yield_mixed_into_stream_dyn_ok },
    { "for_in_annotated_stream_iterates",   test_for_in_annotated_stream_iterates },
    /* No false positives */
    { "vec_ops_still_work",            test_vec_ops_still_work },
  };

  int total = (int)(sizeof(tests) / sizeof(tests[0]));
  int passed = 0;
  int failed = 0;

  for (int i = 0; i < total; i++) {
    printf("  %-40s ", tests[i].name);
    if (tests[i].fn()) {
      passed++;
    } else {
      failed++;
    }
  }

  printf("\n  %d/%d passed\n", passed, total);
  return failed > 0 ? 1 : 0;
}
