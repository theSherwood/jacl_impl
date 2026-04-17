#include "test_helpers.h"
#include "../src/jacl.h"

/* ===== Print capture helper ===== */

typedef struct {
  char     buf[1024];
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

/* ===== Scenario 1: Untrusted expression evaluator =====
 * Caller passes an empty prelude (no I/O), runs arithmetic,
 * asserts result is correct and no side effects occurred. */
static int test_e2e_untrusted_expr(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* Empty prelude: only core builtins (arithmetic, control flow, etc.) */
    const char *src = "[interpret [map] \"[+ 1 [* 2 3]]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 7);

    /* No side effects: print capture buffer is empty */
    ASSERT_INT_EQ((int)cap.len, 0);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== Scenario 2: Audit-logged I/O =====
 * Caller wraps print with a logging closure that both logs (proving the
 * closure was called) and calls the real print.  Asserts both that the
 * custom closure ran (return value) and that print output was captured. */
static int test_e2e_audit_logged_io(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    PrintCapture cap = { .len = 0 };
    ctx->vm.print_fn = capture_print;
    ctx->vm.print_ctx = &cap;

    /* audit-print: calls the real print (OP_PRINT, non-sandbox parent scope)
     * then returns x + 1000 as proof it was called. */
    const char *src =
        "proc audit-print {x} {"
        "  print $x;"
        "  + $x 1000"
        "};"
        "[interpret [map-set [map] \"print\" $audit-print] \"[print 42]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src), UINT64_MAX, &err);

    ASSERT(err.kind == JACL_ERROR_NONE);
    /* Custom closure fired: returned 42 + 1000 = 1042 */
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 1042);
    /* Real print also fired: captured "42\n" */
    ASSERT_STR_EQ(cap.buf, "42\n");

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== Scenario 3: Nested interpret with restriction =====
 * Parent interprets code (1-arg, full access) that itself interprets
 * with a more-restricted prelude (empty map).  Proves capability
 * narrowing works transitively. */
static int test_e2e_nested_restriction(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Outer: 1-arg interpret (full access, interpret available)
     * Inner: 2-arg interpret with empty map → only core builtins */
    const char *src_ok =
        "[interpret \"[interpret [map] \\\"[+ 10 20]\\\"]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src_ok, strlen(src_ok),
                                         UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 30);

    jacl_ctx_destroy(ctx);

    /* Same structure, but innermost code uses non-core builtin → blocked */
    jacl_context_t *ctx2 = jacl_ctx_new(NULL);
    ASSERT(ctx2 != NULL);

    const char *src_blocked =
        "[interpret \"[interpret [map] \\\"[spawn { 42 }]\\\"]\"]";
    JaclVal result2 = jacl_ctx_run_source(ctx2, src_blocked,
                                          strlen(src_blocked),
                                          UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer succeeds */
    ASSERT(jacl_is_error(result2));        /* inner compile error: spawn blocked */

    jacl_ctx_destroy(ctx2);
    TEST_PASS();
}

/* ===== Scenario 4: Prelude derivation =====
 * [map-remove [interpret-prelude] "spawn"] produces a prelude that blocks
 * spawn in interpreted code; error value is detectable via jacl_is_error. */
static int test_e2e_prelude_derivation(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Derive prelude: remove "spawn" from default permissive prelude.
     * spawn should be blocked; core builtins still work. */
    const char *src =
        "def safe [map-remove [interpret-prelude] \"spawn\"];"
        "[interpret $safe \"[spawn { 42 }]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src),
                                         UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);   /* outer succeeds */
    ASSERT(jacl_is_error(result));          /* inner: spawn blocked → error */

    jacl_ctx_destroy(ctx);

    /* Core arithmetic still works with the derived prelude */
    jacl_context_t *ctx2 = jacl_ctx_new(NULL);
    ASSERT(ctx2 != NULL);

    const char *src2 =
        "def safe [map-remove [interpret-prelude] \"spawn\"];"
        "[interpret $safe \"[+ 100 200]\"]";
    JaclVal result2 = jacl_ctx_run_source(ctx2, src2, strlen(src2),
                                          UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result2));
    ASSERT(jacl_as_i32(result2) == 300);

    jacl_ctx_destroy(ctx2);
    TEST_PASS();
}

/* ===== Scenario 5: Parent closure captures =====
 * Prelude value is a closure that closes over parent-local mutable state
 * (a box).  Interpreted code calls the closure multiple times, mutating
 * the parent's state.  Parent observes the mutations after interpret
 * completes. */
static int test_e2e_parent_closure_captures(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Parent defines a box (mutable cell) and two closures:
     * - inc: increments the box and returns the new value
     * - get: returns the current box value
     * Interpreted code calls inc three times, then get.
     * After interpret, parent reads the box directly to confirm mutation. */
    const char *src =
        "def counter [box 0];"
        "proc inc {} { reset $counter [+ [deref $counter] 1]; deref $counter };"
        "proc get {} { deref $counter };"
        "def p [map \"inc\" $inc \"get\" $get];"
        "[interpret $p \"[inc]; [inc]; [inc]; [get]\"];"
        "[deref $counter]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src),
                                         UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    /* Parent observes 3 increments from interpreted code */
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 3);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== Scenario 6: Compile-time macro in interpreted code =====
 * Interpreted source defines its own macro with defmacro and uses it.
 * Everything is resolved at the compilation phase inside interpret.
 * Uses only core builtins (defmacro, syntax-quote, ~, +) so it works
 * with an empty prelude. */
static int test_e2e_macro_in_interpreted(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Inner source defines a macro "double" that expands [double x] to
     * [+ x x], then calls it.  All core forms, no prelude needed. */
    const char *src =
        "[interpret [map]"
        " \"defmacro double {x} { syntax-quote [+ ~x ~x] };"
        " [double 21]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src),
                                         UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);
    ASSERT(jacl_is_i32(result));
    ASSERT(jacl_as_i32(result) == 42);

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* ===== Scenario 7: Invalid prelude key rejection =====
 * Prelude map with invalid keys (non-string, empty, >128 bytes) should
 * produce error values, not crash. */
static int test_e2e_invalid_prelude_key(void) {
    jacl_context_t *ctx = jacl_ctx_new(NULL);
    ASSERT(ctx != NULL);

    /* Prelude with non-string key (integer 42) should error.
     * [map 42 "value"] creates a map with integer key. */
    const char *src = "[interpret [map 42 \"value\"] \"[+ 1 2]\"]";
    JaclError err;
    JaclVal result = jacl_ctx_run_source(ctx, src, strlen(src),
                                         UINT64_MAX, &err);
    ASSERT(err.kind == JACL_ERROR_NONE);  /* outer succeeds */
    ASSERT(jacl_is_error(result));         /* inner: prelude key error */

    jacl_ctx_destroy(ctx);
    TEST_PASS();
}

/* --- Test Runner --- */

typedef struct { const char* name; int (*fn)(void); } TestEntry;

int main(void) {
  TestEntry tests[] = {
    { "e2e_untrusted_expr",          test_e2e_untrusted_expr },
    { "e2e_audit_logged_io",         test_e2e_audit_logged_io },
    { "e2e_nested_restriction",      test_e2e_nested_restriction },
    { "e2e_prelude_derivation",      test_e2e_prelude_derivation },
    { "e2e_parent_closure_captures", test_e2e_parent_closure_captures },
    { "e2e_macro_in_interpreted",    test_e2e_macro_in_interpreted },
    { "e2e_invalid_prelude_key",     test_e2e_invalid_prelude_key },
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
